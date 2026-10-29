#!/bin/sh
# mkesp.sh [cfg] [espdir] : <espdir>/EFI/BOOT/BOOTX64.EFI = standalone GRUB with cfg built in,
#   plus the kernel and EFI-aware acpi server next to it (defaults: efi/grub.cfg, esp)
set -e
cd "$(dirname "$0")"
CFG=${1:-efi/grub.cfg}; ESP=${2:-esp}
mkdir -p "$ESP/EFI/BOOT"
grub-mkstandalone -O x86_64-efi -o "$ESP/EFI/BOOT/BOOTX64.EFI" \
  --modules="part_msdos ext2 gzio multiboot multiboot2 efi_gop all_video search search_fs_uuid search_fs_file fat normal" \
  "boot/grub/grub.cfg=$CFG"
[ "$ESP" = esp ] || cp esp/gnumach-efi.gz esp/acpi-efi.static "$ESP/"
ls -la "$ESP" "$ESP/EFI/BOOT"
