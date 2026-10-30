# Installation

## What is on the disk (2026-10-30)
Target: Netac NV3000, serial AA20260908500G205715 (Linux names it nvme2n1, PCI 12:00.0, so Hurd
sees it as ld2: NetBSD's ld numbering follows PCI order, checked in QEMU with three NVMe disks).

| Part | Size | Contents |
|---|---|---|
| p1 | 953 MiB | swap |
| p2 | 32 GiB | root, ext2 (Debian GNU/Hurd amd64 2026-03-14 + Hurd 20261028 packages + patches) |
| p3 | 512 MiB | EFI system partition, FAT32, label HURD96: EFI/BOOT/BOOTX64.EFI (GRUB, config built in: gnumach-fb.gz, gfxpayload=keep), gnumach-fb.gz, acpi-efi.static |

Made by `DISK_HELPER=<helper> install/reinstall.sh` (= install.sh on a raw copy of the tested PC VM image and its ESP folder; earlier `install/install.sh vm/install.raw vm/esp-install.tar`). install.raw = `qemu-img convert`
of the VM layer prepared-install.qcow2. ESP = `vm/mkesp.sh vm/efi/grub-install.cfg esp-install`.
Every disk step goes through the root helper in DISK_HELPER (only that serial, refuses mounted partitions).

## Booting it
The two Netac drives (this one and the OpenBSD one) have the same name in the firmware boot menu.
Run `sudo install/boot-entry.sh` once on Linux: it adds a firmware entry and a Gentoo GRUB entry,
both called hurd96. Afterwards `sudo install/boot-entry.sh next` boots hurd96 once on the next restart.
The partition table has a fixed id (0x96961996), so the entry survives reinstalls.

## Using it
Log in as root at the console prompt (USB keyboard works through hurd96kbd), then type `desk`:
the lilith compositor starts on the screen the firmware set up. See wayland/README.md for its keys.
A new window waits for you to sweep a box for it with the mouse. If the screen stays dark after a
crash, type `fbconsole` blind and press Enter. The Debian keyboard layout (/etc/default/keyboard) is used.

## Known limits on the real machine
- Only the screen mode the firmware picked; no GPU acceleration (amdgpu via LKL is work in
  progress, see gpu/README.md).
- No driver for the RTL8125 is confirmed working yet, so expect no network.
- No sound driver.
- 4 CPUs used; the Hurd servers share one of them (see PROJECT_STATUS.md, SMP).
- If NVMe and AHCI end up on one shared legacy IRQ line, rumpdisk can freeze (see PROJECT_STATUS.md).
- US keyboard layout on the console; the desktop takes /etc/default/keyboard (XKBLAYOUT).

## Recovery
Boot Gentoo as usual (the Hurd disk is not in its boot path). To reinstall, rerun install.sh.
To check the root filesystem from Linux, use the fsck command of the disk helper on partition 2.
