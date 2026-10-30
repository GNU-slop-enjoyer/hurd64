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

Made by `install/reinstall.sh` (= install.sh on a raw copy of the tested PC VM image and its ESP folder; earlier `install/install.sh vm/install.raw vm/esp-install.tar`). install.raw = `qemu-img convert`
of the VM layer prepared-install.qcow2. ESP = `vm/mkesp.sh vm/efi/grub-install.cfg esp-install`.
Every disk step goes through `sudo tuxdisk` (only that serial, refuses mounted partitions).

## Booting it
No NVRAM boot entry was added (tuxdisk does not touch firmware variables). Use the firmware boot
menu (F8/F11/F12 on most boards) and pick the Netac drive, or the removable path
EFI/BOOT/BOOTX64.EFI. Linux on the other disk is untouched.

## Using it
Log in as root at the console prompt (USB keyboard works through tuxkbd), then type `desk`:
the Wayland desktop (hurdwm) starts on the screen the firmware set up, with a foot terminal.
Super+Enter terminal, Super+D run a program (xterm, galculator, mousepad, thunar, mc, htop...),
Super+1..9 workspaces, Super+J/K focus, Super+F full screen, Super+Q close, Super+Esc back to
the console. A USB mouse works too (click to focus). If the screen stays dark after a crash,
type `tuxdesk -r` blind and press Enter. `desk --simple` starts the small built-in desktop.

## Known limits on the real machine
- Only the screen mode the firmware picked; no GPU acceleration (amdgpu via LKL is work in
  progress, see gpu/README.md).
- No driver for the RTL8125 yet (rumpnet only has wm), so no network.
- 4 CPUs used; the Hurd servers share one of them (see PROJECT_STATUS.md, SMP).
- If NVMe and AHCI end up on one shared legacy IRQ line, rumpdisk can freeze (see PROJECT_STATUS.md).
- US keyboard layout on the console; the desktop takes /etc/default/keyboard (XKBLAYOUT).

## Recovery
Boot Gentoo as usual (the Hurd disk is not in its boot path). To reinstall, rerun install.sh.
To check the root filesystem from Linux: `sudo tuxdisk fsck 2`.
