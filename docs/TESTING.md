# Testing

VM: QEMU/KVM, `-cpu host`, 2 GB RAM, IDE disk, e1000. Scripts in `vm/`. Base image: Debian GNU/Hurd
amd64 2026-03-14 (md5 91dc71608fcfc9f237028f1fe87c39c2 for the .tar.xz).

| Test | Command | Expected | Result |
|---|---|---|---|
| parallel CPU | 8x `sh spin.sh` (400k loop) vs 1x | ~same wall time | 273 ms vs ~210 ms with smpctl: pass |
| fs stress | `vm/stress.sh 3` | no hang | UP: pass. SMP parked: pass. SMP active (2 or 8 CPUs): hang |

## The SMP hang (open)
- Reproduce: `vm/boot-smp.sh 2 && vm/stress.sh` (hangs on run 1).
- In kdb (`sendkey ctrl-alt-d`, console on com0 via `vm/ser.py`; `set $lines = 100000`,
  `show all threads`): every CPU idle; 13 ext2fs threads `.W...F.` in
  `gsync_wait` <- `_Xgsync_wait` <- `mach_msg_trap`; all rumpdisk threads waiting for messages.
- gsync_wait/gsync_wake both hold the bucket kmutex around the value check and the dequeue.
  The race may be in userspace (ext2fs/libdiskfs locking) since only true parallelism triggers it.
