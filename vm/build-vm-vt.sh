#!/bin/sh
# build-vm.sh : boot the build VM (prepared-nvme.qcow2, UEFI, NVMe root, build.img on IDE as wd1),
#   mount /build. The build disk is a second NVMe (ld1); rump pciide panics on some ext2fs reads.
set -e
cd "$(dirname "$0")"
./stop.sh
./fsck-layer.sh prepared-nvme.qcow2
/usr/sbin/e2fsck -fy build.img >/dev/null 2>&1 || true
rm -f serial.log
(DISK=prepared-nvme.qcow2 ESPDIR=esp-fb FW=efi NVME_ADDR=0x6 BUS=nvme MEM=3G \
  EXTRA="-drive id=bd,if=none,file=build.img,format=raw,cache=writeback -device nvme,serial=tuxbuild,drive=bd,addr=0x9 -drive id=vt,if=none,file=vtest.img,format=raw -device virtio-blk-pci,drive=vt,addr=0xa,disable-legacy=on,iommu_platform=on" setsid ./run.sh 2 >qemu.log 2>&1 &)
sleep 25
# OVMF's saved boot order may land in its shell: start GRUB from the ESP (fs0:)
./vmctl type 'fs0:\EFI\BOOT\BOOTX64.EFI\n' || true
./wait-ssh.sh 300
./ssh.sh '[ -e /dev/ld1 ] || settrans -c /dev/ld1 /hurd/storeio @/dev/disk:ld1; settrans -ac /build /hurd/ext2fs /dev/ld1 && fsysopts /build | cut -c1-30'
