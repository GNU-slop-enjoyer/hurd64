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
| Boot with >3.5 GB RAM | **FAIL** | `ext2fs: part:2:device:wd0: Input/output error` at 4 GB, fine at 2 GB. Blocks the real machine (30 GB) |

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

## The hang, as seen in kdb (2026-10-29)
Every hang dump has the same shape:
- A runnable kernel thread (`io_done`, `R..ON.P`) is on **no run queue** (thread->runq == 0).
- CPU 0 (default pset): `state = PROCESSOR_DISPATCHING`, `next_thread` = that thread.
  CPU 0 runs its own `idle/0`, stopped right after `hlt` in idle_thread_continue.
- APs 1-3: PROCESSOR_IDLE, in slave_pset (parked).
- ext2fs and netdde threads sleep in gsync_wait behind the stuck I/O completion.
- CPU 0 does get clock ticks (elapsed_ticks +403 in ~4 s).

So CPU 0 is handed a thread and keeps going back to sleep. Tried, none fixes it:

| Change (patches/gnumach/) | Result |
|---|---|
| idle-race: check for work with interrupts off, then `sti; hlt` | still hangs (kept, it closes a real window) |
| bsp-lapic-timer: re-apply upstream f41f8801 (reverted upstream in ddb0766a; LAPIC timer on CPU 0 too) | still hangs; CPU 0 timer then periodic and counting |
| idle-reread EXPERIMENT: re-read current_processor() every loop pass | still hangs, so a clobbered register is not it |

Without f41f8801, CPU 0's LAPIC timer is left masked in one-shot mode by calibrate_lapic_timer
(LVT 0x10030) and CPU 0's clock is the PIT, by upstream design.

## Fallback API
Groq configured, tested 2026-10-29 (openai/gpt-oss-120b answered OK). See docs/API_AND_TOOLS.md.

## Not started
NVMe driver, RTL8125 driver, UEFI boot on the real machine, display/input, GPU, Wayland, install.
