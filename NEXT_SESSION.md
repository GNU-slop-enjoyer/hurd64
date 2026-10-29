# Next session

## Resume
"continue" is enough. Read PROJECT_STATUS.md first, then this list. VM scripts: ~/src/hurd64/vm
(copies in vm/). Stop a VM with `vm/stop.sh` (uses qemu.pid). Never `pkill -f` a pattern.

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

## Order (user, 2026-10-29)
1 SMP hang, 2 >3.5 GB RAM, 3 NVMe, 4 RTL8125, 5 UEFI boot on the real PC, 6 install (wipes tuxmin, final OK first),
then GPU on the Granite Ridge iGPU only, then Wayland.

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
6. Install decision (user, 2026-10-28): wipe tuxmin and give Hurd the whole Netac ...205715 disk. Still show the exact plan and get a final OK right before writing.
7. Free API: Groq works (openai/gpt-oss-120b, tested 2026-10-29). `freellm models groq` for ids.

Limits: builds -j8 at most on the host, -j4 in the VM; VM 2 GB RAM.
