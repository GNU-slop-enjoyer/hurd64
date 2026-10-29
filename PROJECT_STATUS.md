# Project status

Last updated 2026-10-28. Each claim names how it was checked.

## Baseline

| Part | Source | State |
|---|---|---|
| GNU Mach | upstream git fa3026b9 (2026-10-28) | x86_64 port, SMP (APIC, per-CPU LAPIC timers), early UEFI (system table address) exist upstream |
| MIG | upstream git cb48044 | builds for x86_64-gnu with host gcc |
| Hurd | upstream git 6581877d | disks: rumpdisk with AHCI/IDE only (no NVMe). NICs: rumpnet with Intel wm only, netdde. No display/GPU stack |
| Userland for tests | Debian GNU/Hurd amd64 image 2026-03-14 | boots in KVM. Its stock kernel is `1.8+git20260224-up-amd64` (uniprocessor) |

Target machine: Ryzen 9 9950X (16c/32t), 30 GB RAM, UEFI only. Target disk: Netac NVMe serial
...205715 (NVMe). NICs: Realtek RTL8125, Intel AX210. GPUs: RX 9070 (RDNA4), Granite Ridge iGPU.

## Verified

| Feature | Result | Evidence |
|---|---|---|
| 64-bit SMP kernel build | pass | `--host=x86_64-gnu --enable-ncpus=64 --enable-apic --enable-kdb`, ELF64, see docs/BUILDING.md |
| Boots Debian userland with 8 vCPUs | pass | `uname -v` GNU-Mach 1.8, 10 APIC IDs logged on serial |
| APs run user tasks | pass after `smpctl` | upstream parks APs in a separate processor set (aadb4339); `smpctl` moves them to the default set. 8 CPU-bound jobs: 1642 ms before, 273 ms after (1 job: ~210 ms) |
| Parallel fs load, stock UP kernel | pass 3/3 | `vm/stress.sh`: 16x `ls -lR /usr/share`, 9-13 s |
| Parallel fs load, SMP kernel, APs parked | pass 3/3 | 10-15 s |
| Parallel fs load, SMP kernel, APs active | **FAIL** (hang), 2 and 8 CPUs | all CPUs idle; ext2fs threads asleep in `gsync_wait`, rumpdisk idle in `mach_msg`. See docs/TESTING.md |
| Boot with >3.5 GB RAM, IDE disk | FAIL (IDE only) | `ext2fs: ... Input/output error` at 4 GB: rump's PCI DMA memory lands above 4 GB and IDE DMA is 32-bit |
| Boot with 4/8/16 GB RAM, AHCI disk | **pass** | `BUS=ahci MEM=16G vm/boot.sh up 2`: 15950 MB seen, 200 MB dd write, stress2 2/2 (18-26 s). Real machine uses NVMe/AHCI (64-bit DMA), so item 2 does not block it |

## Hang isolation (2026-10-29), `vm/stress2.sh` (8x ls -lR + 4x cp -a /usr/share/doc + rm + sync)

All kernels below are upstream gnumach fa3026b9 unless marked Debian. 4 vCPUs unless noted. 2 GB RAM.

| Kernel build | CPUs running tasks | Result |
|---|---|---|
| Debian stock 1.8+git20260224 UP | 1 | **hang, run 1** |
| upstream UP (no APIC) | 1 | pass 3/3 (22-30 s) |
| upstream UP + `--enable-apic` | 1 | pass 4/4 |
| upstream `ncpus=4` on a **1-vCPU** VM | 1 | pass 4/4 |
| upstream `ncpus=4`, APs parked | 1 | hang, run 3 |
| upstream `ncpus=64`, APs parked | 1 | KERN_NO_SPACE from cp, then hang, run 1 |
| upstream `ncpus=64`, APs active | 4 | KERN_INVALID_ADDRESS from cp, then hang, run 1 |

Conclusions so far:
- Debian's kernel has a write-load hang that current upstream no longer has (UP build passes).
- The SMP problem needs a second CPU to *exist*: it happens even when the APs run no user
  tasks, and not with an NCPUS>1 kernel on one CPU. Device IRQs are routed to CPU 0 only
  (ioapic.c: FIXED, physical dest = BSP APIC id), so misrouted IRQs are ruled out.
  Suspects: cross-CPU signalling (TLB shootdown IPIs, cause_ast_check / idle wakeup) and
  ncpus-sized kernel memory (ncpus=64 fails much faster than ncpus=4).

## The hang, as seen in kdb (2026-10-29, corrected)
- Stuck: ext2fs threads (and netdde threads) asleep in `gsync_wait` (userspace locks), nobody
  waking them. All CPUs idle. Happens as soon as a second CPU exists, even with APs parked.
- NOT stuck: the `io_done` kernel thread. Earlier dumps showed CPU 0 `DISPATCHING` with
  next_thread = io_done; counters (patches/gnumach/idle-dbg-EXPERIMENT) show CPU 0's idle loop
  exits ~80x/s, each time a CPU0->CPU0 handoff of io_done, no IPI. Entering kdb by keyboard
  completes console I/O and wakes io_done in the same interrupt, so the snapshot always caught
  it mid-handoff. That earlier conclusion was an observation artifact.
- CPU 0 gets clock ticks (elapsed_ticks advances).

Kernel changes tried (patches/gnumach/), none fixes the hang:
| Change | Result |
|---|---|
| idle-race: check for work with interrupts off, then `sti; hlt` | still hangs (kept: closes a real window) |
| bsp-lapic-timer: re-apply f41f8801 (reverted upstream, ddb0766a) | still hangs |
| idle-reread EXPERIMENT | still hangs |

## Fallback API
Groq configured, tested 2026-10-29 (openai/gpt-oss-120b answered OK). See docs/API_AND_TOOLS.md.

## NVMe (item 3), 2026-10-29
| Step | Result | Evidence |
|---|---|---|
| NVMe rump components | available | Debian rumpkernel 0~20250111-10 (debian-ports) ships librumpdev_nvme, _ld, _ld_nvme |
| rumpdisk with NVMe | built | patches/hurd/rumpdisk-nvme: links rumpdev_ld/nvme/ld_nvme, accepts `ld*` names |
| NVMe attach + root mount | **pass** | QEMU nvme device: `nvme0: NVMe 1.4`, `ld0 at nvme0 nsid 1`, `ext2fs[part:2:device:ld0]` bootstrapped |
| Full boot with root on NVMe | **pass** (2026-10-29) | networking via rumpnet (/dev/net -> rumpnet, /dev/wm0, interfaces uses /dev/wm0); NVMe alone on its IRQ. DHCP at boot, ssh, apt-get update over the Internet, 300 MB dd at 332 MB/s, stress2 2/2 |
| NVMe sharing an IRQ with AHCI | FAIL (freeze) | open bug: when NVMe and AHCI share a line inside rumpdisk, starting another PCI driver freezes the system; see lead below |

Stall narrowed down (2026-10-29):
- Same layer and new hurd package, root on AHCI (IRQ 10, e1000 on IRQ 11): boots to login.
  So the stall needs the NVMe driver; the hurd package alone is not the cause.
- NVMe sharing IRQ 11 with e1000: stall. NVMe and AHCI on IRQ 10, e1000 alone on 11: still a
  stall, so a shared line between rumpdisk and netdde is not the (whole) cause.
- Last console line before every stall: `[6.09] entropy: best effort` from the rump kernel in
  rumpdisk. Serial, disk image and the IRQ 10 count all stop right after it.

Further (same day):
- With boot-time networking off, NVMe root boots to login in ~15 s; root is writable on
  part:2:device:ld0, 300 MB dd at 367 MB/s, md5 correct.
- Opening /dev/eth* (starts netdde) freezes it. Replacing netdde with rumpnet (/dev/net ->
  /hurd/symlink rumpnet): boot completes, wm0 attaches; but `pfinet -i /dev/wm0` freezes it
  again. So: NVMe in rumpdisk + a second PCI driver starting = freeze; interrupt counts stop.
- Lead: device/intr.c masks a line in queue_intr until userspace acks (irq_acknowledge). A
  shared or never-acked line stays masked and silences NVMe. Check __disable_irq nesting and
  how rump irqhelp acks when NVMe+AHCI share IRQ 10.

Test setup: SeaBIOS cannot boot NVMe, so GRUB boots from an AHCI copy (bootstub.qcow2) and the
root is the NVMe disk. The build layer prepared-nvme.qcow2 also got Debian's newer `hurd`
package (0.9.git20261028-2) as a dependency, so the stall may come from that and not from NVMe.

## UEFI boot (item 5), 2026-10-29: pass in QEMU/OVMF
| Step | Result | Evidence |
|---|---|---|
| GRUB EFI from FAT ESP | pass | vm/mkesp.sh: standalone BOOTX64.EFI with vm/efi/grub.cfg |
| GNU Mach via multiboot2 | pass after fix | upstream panicked: biosmem_bootalloc "unable to allocate memory" (heap search bounded by mem_upper). Fix patches/gnumach/efi-heap uses the memory map |
| ACPI RSDP on UEFI | pass after fix | upstream acpi server scans the BIOS area ("A valid RSDP was not found"). Fix patches/hurd/acpi-efi walks the EFI config table via host_info(HOST_EFI_SYSTAB): `ACPI: RSDP 0xBFB7E014 (v02 BOCHS)` |
| Root on NVMe, no BIOS disk | pass | /dev/ld0, ld0s1, ld0s2 storeio nodes; fstab uses /dev/ld0s2 (wd0s2 made fsck fail) |
| Full system | pass | `FW=efi BUS=nvme NVME_ADDR=0x6 vm/run.sh 2`: ssh, 300 MB dd 377 MB/s, apt-get update, stress2 2/2 |

Framebuffer console (patches/gnumach/fbcon): kd draws its 80x25 grid on the multiboot2 RGB framebuffer (24/32 bpp, 8x16 font). Verified in QEMU/OVMF by screenshot: boot messages, colours, wrapping, "console login:". Installed on the disk 2026-10-29 (ESP updated, files byte-identical, fsck.fat clean).

## Install (item 6), 2026-10-29: done
See INSTALLATION.md. Verified: partition table, e2fsck -f clean after resize (it fixed leaked inodes left by Hurd ext2fs first), ESP files byte-identical to the build, root has fstab on ld2 and rumpnet. Not yet booted on the real machine.

## RTL8125 (item 4), in progress 2026-10-29
- patches/rumpkernel/rge-rump.diff: rump component libpci_if_rge (NetBSD if_rge.c, no firmware), added
  to RUMPPCIDEVS; Debian rumpkernel 0~20250111-10tux1 built in the VM (dpkg-buildpackage, EXIT=0).
- Installed on the disk 2026-10-29: new rump libs, rumpnet linking librumpdev_pci_if_rge.so.0,
  /dev/rge0 devnode, `allow-hotplug /dev/rge0` + dhcp. Same image tested in QEMU (3 NVMe, ld2): new
  rumpnet runs wm0 with DHCP, apt-get update, stress2 1/1. rge itself untested (no emulation).
- patches/hurd/rumpnet-rge: rumpnet links rumpdev_pci_if_rge.
- QEMU cannot emulate an RTL8125, so only the build and loading can be tested here; the
  real test is on the machine.

## Data-safety issue: Hurd ext2fs leaves inconsistencies
An image taken after a CLEAN poweroff still fails `e2fsck -fn` (exit 4: leaked inodes, wrong free
counts). Hard VM stops made it worse: one layer later crashed ext2fs (`file_pager_write_pages:
Assertion blk failed` after "bit already cleared for inode"). install.sh repairs the image before
writing it (resizefs runs e2fsck -f -y). On the real machine run `sudo tuxdisk fsck 2` from Linux
now and then. Root cause not investigated yet.

## Not started
NVMe driver, RTL8125 driver, UEFI boot on the real machine, display/input, GPU, Wayland, install.
