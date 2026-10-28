# Next session

State of the VM: `~/src/hurd64/vm`. `prepared.qcow2` boots tux-smp by default; tests run on a
throwaway `work.qcow2` (`vm/boot-smp.sh N`). Debian's stock UP kernel is still entry 0 in GRUB.

1. Build current upstream Hurd natively in the VM (UP kernel, `-j4`), install to a prefix,
   boot with the new ext2fs/libdiskfs, rerun `vm/stress.sh` on 2 CPUs. Debian's Hurd is a
   2026-03 snapshot; upstream has many ext2fs/libdiskfs changes since.
2. If it still hangs: find the userspace lock. `trace/tu` in kdb on an ext2fs thread
   sleeping in gsync_wait, map addresses with a debug build of ext2fs.static.
3. >3.5 GB RAM I/O error: look at rumpdisk DMA buffer allocation (physical address limit).
4. NVMe for rumpdisk (NetBSD nvme(4)), then rge(4) for the RTL8125.

Limits on this host: builds `-j8` at most, VM 2 GB RAM. Don't `pkill -f` patterns.
