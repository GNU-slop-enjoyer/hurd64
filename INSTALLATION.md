# Installation

## What is on the disk (2026-10-29)
Target: Netac NV3000, serial AA20260908500G205715 (Linux names it nvme2n1, PCI 12:00.0, so Hurd
sees it as ld2: NetBSD's ld numbering follows PCI order, checked in QEMU with three NVMe disks).
tuxmin was wiped, with the user's OK.

| Part | Size | Contents |
|---|---|---|
| p1 | 953 MiB | swap |
| p2 | 32 GiB | root, ext2 (Debian GNU/Hurd amd64 2026-03-14 + Hurd 20261028 packages + patches) |
| p3 | 512 MiB | EFI system partition, FAT32, label HURD64: EFI/BOOT/BOOTX64.EFI (GRUB, config built in: gnumach-fb.gz, gfxpayload=keep), gnumach-fb.gz, acpi-efi.static (gnumach-efi.gz is the older serial-console kernel, unused) |

Made by `install/install.sh vm/install.raw vm/esp-install.tar`. install.raw = `qemu-img convert`
of the VM layer prepared-install.qcow2. ESP = `vm/mkesp.sh vm/efi/grub-install.cfg esp-install`.
Every disk step goes through `sudo tuxdisk` (only that serial, refuses mounted partitions).

## Booting it
No NVRAM boot entry was added (tuxdisk does not touch firmware variables). Use the firmware boot
menu (F8/F11/F12 on most boards) and pick the Netac drive, or the removable path
EFI/BOOT/BOOTX64.EFI. Linux on the other disk is untouched.

## Known limits on the real machine
- The kernel console draws on the UEFI framebuffer (patches/gnumach/fbcon): boot messages and
  a "hurd64 console login:" prompt. Hurd's own console client (VGA text) shows nothing on real
  UEFI hardware; log in at the console prompt. Checked in QEMU/OVMF only.
- No driver for the RTL8125 yet (rumpnet only has wm), so no network.
- USB keyboards need firmware PS/2 emulation; untested.
- If NVMe and AHCI end up on one shared legacy IRQ line, rumpdisk can freeze (see PROJECT_STATUS.md).

## Recovery
Boot Gentoo as usual (the Hurd disk is not in its boot path). To reinstall, rerun install.sh.
To check the root filesystem from Linux: `sudo tuxdisk fsck 2`.
