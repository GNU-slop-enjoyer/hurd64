# Next session

## Resume
"continue" is enough. Read PROJECT_STATUS.md first, then this list. VM scripts: ~/src/hurd64/vm
(copies in vm/). Stop a VM with `vm/stop.sh` (uses qemu.pid). Never `pkill -f` a pattern.

## GPU port (started 2026-10-29): see gpu/README.md
amdgpu via LKL (Linux 6.12 as a library) in a Hurd server. DONE: LKL+amdgpu builds on the host (gpu/build-lkl.sh),
runs inside a Hurd process (lkl-hurd/, test/lkl-boot.c), and the Hurd PCI host code is verified end to end with
vm/build-vm-vt.sh: Linux virtio-blk in LKL read the partition table of a QEMU virtio disk (BAR MMIO, DMA via 4 MiB
chunks, INTx via libirqhelp). virtio needs iommu_platform=on (else it bypasses the DMA API and scribbles over RAM).
NEXT: user runs `sudo gpu/igpu-vfio.sh` + reboots -> iGPU 13:00.0 on vfio-pci, VFCT copied; rerun gpu/build-lkl.sh
(builds the VBIOS in), then vm/build-vm-gpu.sh and `./lkl-boot 1024 lkl_pci=00:0b.0` in /build/lkl/lkl-hurd.
PUSH BLOCKED: commit c1bd9c4 contains a 317 MB binary (gpu/test/lkl-boot); removing it needs a history rewrite.

## Where things are (updated 2026-10-29)
- Upstream checkouts: ~/src/hurd64/upstream/{gnumach,mig,hurd}. Tools: ~/src/hurd64/tools (x86_64-gnu-mig).
- Kernel: ~/src/hurd64/build/gnumach-smp/gnumach.gz (SMP, ncpus 64, kdb).
- VM layers: base.img (Debian 2026-03-14, read-only) <- prepared.qcow2 (ssh key, tux-smp default,
  smpctl, console=com0) <- work.qcow2 (throwaway, recreated by boot-smp.sh).
- build.img (20 GB, made by Hurd's own mke2fs, label hurdbuild): holds /build/hurd (upstream source).
  Attach with EXTRA="-drive file=build.img,format=raw,if=ide,index=1,cache=writeback" ./run.sh 4,
  then in the VM: `settrans -ac /build /hurd/ext2fs /dev/wd1`.
- The build deps (build-essential autoconf automake mig texinfo libtool pkg-config libbz2-dev
  zlib1g-dev uuid-dev libblkid-dev) were installed into work.qcow2 only. Install them into
  prepared.qcow2 instead, or reinstall each boot.

## REAL HARDWARE BOOT (user, 2026-10-30): FAILED, user wants everything fixed before testing again
Photo: kernel + fb console worked; rumpdisk printed every PCI device "not configured" (spam), then
`ext2fs: part:2:device:ld2: No such device or address`; no typing (USB keyboard).
Causes: (1) rump pci only attaches bus 0 (pci_at_mainbus, pba_bus = 0) and has no ppb driver; real
NVMe/NIC/xHCI sit behind PCIe bridges. (2) Hurd reads only the PS/2 keyboard. (3) autoconf
"not configured" lines go to the console.
RULE NOW: test only on a QEMU machine shaped like the PC: `-machine q35,i8042=off`, devices behind
pcie-root-port, xHCI + usb-kbd only, UEFI. The user does not want to boot again until it is a usable
login with a working keyboard. Then continue through all items incl. iGPU + Wayland.
DONE 2026-10-30: USB KEYBOARD LOGIN WORKS on the PC-shaped VM (login root, run commands). tuxkbd from inittab
(c:23 line), rump ukbd (rumpkernel -10tux3), /dev/wskbd had the wrong major so tuxkbd mknods /dev/tuxwskbd0
from devsw_name2chr("wskbd"). ACPICA tracing off (hurd tux/quiet: acpi main.c). Log: /var/log/tuxkbd.log.
Console cleaned up (2026-10-30): rump AB_SILENT, ACPICA tracing off + libacpica stdout discarded, no per-IRQ
printf in gnumach (patches/gnumach/quiet-intr), ASCII /etc/motd. vm/rebuild-all.sh rebuilds+installs all.
Left on screen: Debian rcS "Cleaning up temporary files... failed!" + startpar bootclean failure.
ext2fs mitigation (2026-10-30): an idle boot + clean poweroff leaves 3 "deleted inode has zero dtime"
(files unlinked while open, e.g. /run pids, /dev/log). Root fs now checked every boot (tune2fs -c 1 -i 0)
with FSCKFIX=yes, so it repairs itself and never stops in a maintenance shell (2 boots tested).
Root cause (processes still running when ext2fs syncs at shutdown) is still open.
DISK REINSTALLED 2026-10-30 from install4.raw = PC-VM-tested image (USB keyboard login, quiet console).
SMP SHIPPED 2026-10-30 (disk reinstalled from install5.raw): gnumach ncpus=4 (build/gnumach-smp4fb) on the ESP,
/etc/boot.d/tux-smp -> /usr/local/sbin/tux-smp: all Hurd servers (/hurd/* + boot servers) into a 1-CPU set on the
last CPU (utils/cpuset), then the other CPUs into the default set (utils/smpctl -x). Tested on run-pc.sh with
8 vCPUs: 4 started, 3 for programs, stress2 5/5, 3 jobs parallel, apt OK, USB keyboard login OK.
OPEN: (a) with more than 4 CPUs STARTED (ncpus=8 or 32 on 8 vCPUs) adding even one AP to the default set kills
the system with no load: kernel bug that scales with started CPUs. (b) the ext2fs race itself. (c) fs-heavy
work is slower (servers share one CPU).
Earlier: SMP ROOT CAUSE FOUND 2026-10-30: it is a race in ext2fs (libdiskfs/libpager), not the kernel.
utils/cpuset.c puts a task in its own 1-CPU processor set (task_assign). With root ext2fs (pid 8) pinned to
cpu 3 and everything else on cpus 0-2: 0 "invalid address", no hang, stress2 4/4 (36 s each). Without the
pin: 86 errors + hang. Workaround: at boot smpctl + cpuset all ext2fs. Real fix: find the race.
Earlier: SMP retest 2026-10-30 (ncpus=4 kernel with all fixes, ESP vm/esp-smp, `ESPDIR=esp-smp vm/run-pc.sh 4` + smpctl):
stress2 run 1 => hundreds of `cp: error writing ...: (os/kern) invalid address` (KERN_INVALID_ADDRESS on
io_write data), then a hang. So with several active CPUs, a task's memory looks unmapped to the kernel
while copying: suspect the cross-CPU pmap/TLB shootdown on x86_64 (i386/intel/pmap.c: pmap_update_interrupt,
signal_cpus, cpus_active handling, and PCID if used). NEXT: read that code, write a small C stress test
(threads on all CPUs writing to a pipe/file while another thread mmaps/munmaps) to reproduce fast.
Then iGPU (Granite Ridge), then Wayland. Realtek rge is built in but untested.
Earlier: PC-shaped VM, vm/run-pc.sh: boots to "tuxhurd console login:" with root ld2 behind a PCIe
root port, e1000e behind a root port with DHCP + ssh, no PS/2. Fixes: ppb-rump.diff (rumpkernel -10tux2,
rumpdisk-nvme.static + rumpnet relinked), gnumach no-i8042 (kdinit + PS/2 mouse open). NEXT: USB keyboard
(ukbd-rump.diff -> rumpkernel -10tux3, build tuxkbd, inittab), then quiet the autoconf spam.
Old notes: PC-shaped VM vm/run-pc.sh reproduces the photo exactly. kd hung with no i8042
(fixed: patches/gnumach/no-i8042). ppb-rump.diff written, rumpkernel 0~20250111-10tux2 building in the
build VM (vm/build-vm.sh; build disk is now a 2nd NVMe ld1, IDE panicked in pciide_dma). Every layer is
repaired offline first (vm/fsck-layer.sh). After the build: dpkg -i the debs into prepared-install3,
RELINK rumpdisk-nvme.static and rumpnet(.static) against them (static libs!), test with run-pc.sh.
Fix order: a) ppb in rump pci (ppb-rump.diff: libpci SRCS += ppb.c, PCI.ioconf `ppb* at pci?` +
`pci* at ppb? bus ?`), check IRQs behind bridges; b) quiet autoconf; c) USB keyboard. PLAN: rump component libukbd (uhidev.c ukbd.c + dev/hid parser; ioconf
`uhidev* at uhub? port ? configuration ? interface ?`, `ukbd* at uhidev? reportid ?`, `wskbd* at ukbd? console ? mux ?`;
xhci is already in libpci_usbhc, wskbd in libwscons). C daemon tuxkbd (in tuxhurd/utils): links rump +
pci_usbhc + usb + ukbd + wscons like rumpdisk, rump_sys_open("/dev/wskbd0"), read wscons_event, US layout +
shift/ctrl/caps -> bytes into a pty master whose slave runs /bin/login; pty output -> /dev/console (kdfb).
Replace the console getty with it (inittab).
NOTE: rumpusbdisk also wants xHCI; tuxkbd owns it, so USB storage is out for now; d) ext2fs fsck errors; e) SMP; f) iGPU; g) Wayland.

## Order (user, 2026-10-29)
1 SMP hang, 2 >3.5 GB RAM, 3 NVMe, 4 RTL8125, 5 UEFI boot on the real PC, 6 install (wipes tuxmin, final OK first),
then GPU on the Granite Ridge iGPU only, then Wayland.

## State 2026-10-29 evening: items 2, 3, 5, 6 done in QEMU and installed on the disk (UEFI, NVMe root ld2, framebuffer console). Open: item 1 (SMP hang), item 4 (RTL8125 rge component for rumpnet), real-hardware boot test (user), NVMe+AHCI shared-IRQ freeze, USB keyboard.
Item 4 (rge): DONE as far as QEMU allows (built, linked, installed; see PROJECT_STATUS.md). Real test = boot
  the machine and check `dmesg`-style console lines for "rge0 at pci" and DHCP on /dev/rge0.
  Old steps, for reference:
  1. Build VM: `DISK=prepared-nvme.qcow2 ESPDIR=esp-fb FW=efi NVME_ADDR=0x6 BUS=nvme MEM=3G
     EXTRA="-drive file=build.img,format=raw,if=ide,index=2,cache=writeback" vm/run.sh 2`, then in the
     VM `settrans -ac /build /hurd/ext2fs /dev/wd1` (wd1 = label hurdbuild). Log: /build/rk-build.log
     (ends with EXIT=0 on success); .debs land in /build.
  2. `dpkg -i /build/librump*_0~20250111-10tux1_*.deb`; check /usr/lib/librumpdev_pci_if_rge*.
  3. Build rumpnet from ~/src/hurd64/upstream/hurd branch tux/rumpnet-rge (git archive | ssh,
     configure, `make rumpnet`), copy rumpnet.static to /hurd/rumpnet, check `nm | grep rge_attach`.
  4. Do steps 2-3 in prepared-install.qcow2, add `settrans -c /dev/rge0 /hurd/devnode -M /dev/net rge0`
     and `auto /dev/rge0` + `iface /dev/rge0 inet dhcp` to /etc/network/interfaces, then redo the
     install image (qemu-img convert) and run install/install.sh, or copy just the changed files
     with `tuxdisk mount 2` + `tuxdisk put 2 <dir>`.

Item 3: WORKS in the VM (see PROJECT_STATUS.md). Remaining bug: NVMe+AHCI sharing an IRQ line freezes on the next PCI driver start (device/intr.c masking lead). Old notes:
  the stall after `entropy: best effort` in rumpdisk's rump kernel with NVMe loaded.
  a. Done: AHCI root on the same layer boots, so the hurd package isn't it. A shared IRQ
     with the NIC isn't the whole cause (see PROJECT_STATUS.md).
  a2. Read NetBSD sys/kern/kern_entropy.c (rumpkernel tree) for what prints "best effort" and
     what can block after it; check whether nvme(4) or ld(4) attach adds an entropy source that
     waits for a callout/softint which the Hurd rumpuser never runs.
  NOTE: booting prepared-nvme.qcow2 directly uses up its one-shot next_entry. Re-arm with
  `grub-reboot tux-up-nvme` from an AHCI boot of it. NVME_ADDR=0x6 puts NVMe on IRQ 10;
  give AHCI addr=0x5 to keep it on IRQ 10 too.
  b. Boot NVMe root with networking off (rename /etc/network/interfaces) to see if the rest boots.
  c. kdb is unreachable there (console=com0 masks the kbd pin); use `console=com0` off or `-s`
     single-user for the next try.
  NVMe test command (vm/): BUS=nvme MEM=4G with
  EXTRA="-device ahci,id=ahci -drive id=b0,if=none,file=bootstub.qcow2,format=qcow2,cache=writeback -device ide-hd,drive=b0,bus=ahci.0,bootindex=0"
  on fresh work.qcow2 + bootstub.qcow2 overlays of prepared-nvme.qcow2.
Item 4 (RTL8125): NetBSD if_rge.c is in the rumpkernel tree (buildrump.sh/src/sys/dev/pci/if_rge.c);
  needs a new rump component libpci_if_rge modeled on libpci_if_wm, then rumpnet linking it.
Item 5 (UEFI): GNU Mach has multiboot2 + EFI system table work upstream (Sept/Oct 2026 commits);
  next is GRUB EFI + multiboot2 on OVMF in QEMU.
Item 6: install wipes tuxmin; show the plan and get the user's OK first.

## Next commands
0. SMP hang (PROJECT_STATUS.md "The hang, as seen in kdb (corrected)"). Find which userspace
   locks the ext2fs/netdde threads wait on and who should wake them:
   a. In kdb, walk gsync_buckets (kern/gsync.c) to list waiters' keys (task + address), then map
      the addresses to symbols in ext2fs.static / netdde (unstripped builds or Debian dbgsym).
   b. Check gsync_wake under SMP: the GSYNC_MUTATE path, the remote (task != current_task)
      temp_mapping path, and TLB shootdown for temp_mapping when a second CPU exists.
   c. What a parked AP still does in the kernel: LAPIC timer -> clock_interrupt on APs, pmap
      shootdown IPIs. A missed shootdown could leave a stale kernel mapping on CPU 0.
   Kernel branches: tux/idle-race (base), tux/bsp-lapic-timer, tux/idle-reread, tux/idle-dbg.
   Build dirs ~/src/hurd64/build/gnumach-<name>; layers `vm/mklayer.sh <name>`;
   test `NOSMP=1 vm/boot.sh <name> 4 && vm/stress2.sh 4`. Debug-counter addresses: `nm` the build.
1. Build upstream Hurd in the VM, APs parked (stable):
   `cd /build/hurd && autoreconf -fi && mkdir -p b && cd b && ../configure --without-parted
   --disable-profile && make -j4 ext2fs` ; then boot with the new ext2fs.static (copy to /hurd,
   keep the old as ext2fs.static.debian) and rerun `vm/boot-smp.sh 2 && vm/stress.sh`.
2. If it still hangs: `trace/tu` on an ext2fs thread in gsync_wait (kdb), map addresses with
   the unstripped build.
3. >3.5 GB RAM I/O error in rumpdisk (DMA address limit).
4. NVMe for rumpdisk (NetBSD nvme(4)), rge(4) for the RTL8125.
5. Published: https://github.com/GNU-slop-enjoyer/tuxhurd (public, user approved 2026-10-28). Push after each verified step.
6. Install decision (user, 2026-10-29): may wipe tuxmin (Netac ...205715) and install WITHOUT asking again, once the image passes boot + disk tests from NVMe in the VM. RAM budget while the user is away: up to 25 GB, never freeze the host.
7. Free API: Groq works (openai/gpt-oss-120b, tested 2026-10-29). `freellm models groq` for ids.

Limits: builds -j8 max on the host; RAM up to 25 GB total (user, 2026-10-29). No Python anywhere (user): C for tools, sh for glue. vm/vmctl.c replaces the old Python helpers (build: gcc -O2 -o vm/vmctl vm/vmctl.c).
