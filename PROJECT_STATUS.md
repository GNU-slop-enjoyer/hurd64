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
| Full boot with root on NVMe | **FAIL** (stall) | stops at "Configuring network interfaces" (dhclient); serial and disk stop changing 11 s after start; CPU halted with IF=1 |

Stall narrowed down (2026-10-29):
- Same layer and new hurd package, root on AHCI (IRQ 10, e1000 on IRQ 11): boots to login.
  So the stall needs the NVMe driver; the hurd package alone is not the cause.
- NVMe sharing IRQ 11 with e1000: stall. NVMe and AHCI on IRQ 10, e1000 alone on 11: still a
  stall, so a shared line between rumpdisk and netdde is not the (whole) cause.
- Last console line before every stall: `[6.09] entropy: best effort` from the rump kernel in
  rumpdisk. Serial, disk image and the IRQ 10 count all stop right after it.

Test setup: SeaBIOS cannot boot NVMe, so GRUB boots from an AHCI copy (bootstub.qcow2) and the
root is the NVMe disk. The build layer prepared-nvme.qcow2 also got Debian's newer `hurd`
package (0.9.git20261028-2) as a dependency, so the stall may come from that and not from NVMe.

## Not started
NVMe driver, RTL8125 driver, UEFI boot on the real machine, display/input, GPU, Wayland, install.
