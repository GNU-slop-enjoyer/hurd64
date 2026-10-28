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

## Fallback API
Not configured (no key stored). See docs/API_AND_TOOLS.md.

## Not started
NVMe driver, RTL8125 driver, UEFI boot on the real machine, display/input, GPU, Wayland, install.
