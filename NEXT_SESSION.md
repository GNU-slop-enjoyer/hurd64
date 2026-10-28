# Next session

## Resume
"continue" is enough. Read PROJECT_STATUS.md first, then this list. VM scripts: ~/src/hurd64/vm
(copies in vm/). Stop a VM with `vm/stop.sh` (uses qemu.pid). Never `pkill -f` a pattern.

## Where things are (2026-10-28 22:45)
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

## Next commands
0. SMP hang (see PROJECT_STATUS.md "The hang, as seen in kdb"). Open question: why does
   CPU 0 not pick up next_thread although it is awake every tick? Next checks, in order:
   a. In kdb on the hang, `x/x` the idle loop's view: read %gs:0 for CPU 0 (percpu self) and
      compare with percpu_array[0]; read processor state again after `c` + 3 s to see whether
      state/next_thread are stable or being rewritten (a dispatch/requeue loop).
   b. Add a counter in the idle loop exit path and in thread_setrun's DISPATCHING branch
      (kdb-readable globals) to see whether CPU 0 ever leaves the loop after the hang.
   c. Read kern/sched_prim.c after `retry:` (state DISPATCHING handling, pset lock) and
      processor_assign/action_thread: parked APs have pending assignment state (MACH_HOST).
   Kernel branches: ~/src/hurd64/upstream/gnumach tux/idle-race (base), tux/bsp-lapic-timer,
   tux/idle-reread. Build dirs ~/src/hurd64/build/gnumach-<name>, layers vm/mklayer.sh <name>.
   Earlier isolation (see "Hang isolation"). Reproduce: `vm/boot.sh smp4 4` with NOSMP=1,
   then `vm/stress2.sh 4`. Layers: `vm/mklayer.sh up|upapic|smp4|stock`; kernels in
   ~/src/hurd64/build/gnumach-<k>. Next: in kdb on the hang, check each CPU (`show all threads`
   per processor, pmap shootdown state) and read i386/intel/pmap.c shootdown + kern/sched_prim.c
   idle wakeup for a path that can wait on a parked AP. Then try ncpus=2 on 2 vCPUs.
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
7. Free API: the user said they will add a Groq key (`freellm key groq`). Test with a "Reply OK" request before any use.

Limits: builds -j8 at most on the host, -j4 in the VM; VM 2 GB RAM.
