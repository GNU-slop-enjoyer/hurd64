#!/bin/sh
# mkesp.sh : esp/EFI/BOOT/BOOTX64.EFI = standalone GRUB with efi/grub.cfg built in
set -e
cd "$(dirname "$0")"
mkdir -p esp/EFI/BOOT
grub-mkstandalone -O x86_64-efi -o esp/EFI/BOOT/BOOTX64.EFI \
  --modules="part_msdos ext2 gzio multiboot multiboot2 efi_gop all_video search search_fs_uuid search_fs_file fat normal" \
  "boot/grub/grub.cfg=efi/grub.cfg"
ls -la esp/EFI/BOOT/BOOTX64.EFI
