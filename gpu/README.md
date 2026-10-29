# amdgpu on the Hurd

The plan: run AMD's real Linux driver (amdgpu, Linux 6.12) unchanged in a
Hurd server, by way of LKL ("Linux Kernel Library", Linux built as a library,
https://github.com/lkl/linux). LKL already does the hard part, Linux's
scheduler, memory, timers, PCI core and DMA API on top of plain host threads
and memory. What a host has to give it is small: threads, timers, RAM, and
for a PCI device its config space, BARs, interrupt and DMA addresses. On
Linux LKL gets those from VFIO; here they come from libpciaccess and GNU
Mach (`lkl-hurd/hurd_host.c`).

The first target is the Ryzen 9000 iGPU (Granite Ridge, 1002:13c0: GC 10.3.6,
DCN 3.1.5, PSP 13.0.5, SDMA 5.2.6, VCN 4.0.2). The RX 9070 XT (Navi 48,
RDNA4) needs a newer Linux than LKL has (6.12) and is the host's display, so
it can't be tested.

## Pieces

| file | what |
|---|---|
| `../patches/lkl/0001-...patch` | LKL: kernel FPU support (AMD's display code needs it), `tux_defconfig` = LKL defconfig + `lkl-amdgpu.config` |
| `lkl-amdgpu.config` | MMU, PCI without MSI, DRM, amdgpu + DC, the iGPU firmware built in |
| `lkl-hurd/hurd_host.c` | Hurd host side: contiguous wired kernel RAM (DMA address = physical), BARs mapped directly, INTx via `device_intr_register`, timers (no `timer_create` on the Hurd) |
| `build-lkl.sh` | builds `lkl.o` + headers on the Linux host |

## Steps

1. Build LKL with amdgpu on the Linux host (`build-lkl.sh`). **Not run yet.**
2. Test it there first: LKL's own VFIO host on the iGPU (it has nothing
   plugged in and its own IOMMU group, 31). Needs amdgpu unbound from
   13:00.0 on the host and bound to vfio-pci.
3. Build the host library on the Hurd, run the same test in the PC-shaped VM
   with the iGPU passed through (VFIO).
4. Expected trouble: the VBIOS (no ACPI/VFCT inside LKL; read it from VRAM or
   load it as a firmware file), INTx only, LKL's single-CPU kernel.
5. A Hurd translator for the DRM device (KMS first: modesetting and dumb
   buffers for a software-drawn desktop), then render nodes for Mesa.
