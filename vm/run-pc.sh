#!/bin/sh
# run-pc.sh [cpus] : test VM shaped like the real machine.
#   q35 + UEFI, no PS/2 (i8042=off), every device behind a PCIe root port:
#   3 NVMe (blank, blank, Hurd = ld2), e1000e NIC, xHCI with only a USB keyboard.
#   DISK= Hurd disk (default work.qcow2), ESPDIR= ESP folder (default esp-install-fb), MEM= RAM.
cd "$(dirname "$0")"
D=${DISK:-work.qcow2}; E=${ESPDIR:-esp-install-fb}
exec qemu-system-x86_64 -pidfile qemu.pid -enable-kvm -cpu host -m "${MEM:-4G}" -smp "${1:-2}" \
  -machine q35,i8042=off \
  -drive if=pflash,format=qcow2,readonly=on,file=/usr/share/edk2-ovmf/OVMF_CODE_4M.qcow2 \
  -drive if=pflash,format=qcow2,file=efi/vars-pc.qcow2 \
  -device pcie-root-port,id=rp1,chassis=1,slot=1 -device pcie-root-port,id=rp2,chassis=2,slot=2 \
  -device pcie-root-port,id=rp3,chassis=3,slot=3 -device pcie-root-port,id=rp4,chassis=4,slot=4 \
  -device pcie-root-port,id=rp5,chassis=5,slot=5 \
  -drive id=na,if=none,file=blank-a.qcow2,format=qcow2 -device nvme,serial=blanka,drive=na,bus=rp1 \
  -drive id=nb,if=none,file=blank-b.qcow2,format=qcow2 -device nvme,serial=blankb,drive=nb,bus=rp2 \
  -drive id=nh,if=none,file="$D",format=qcow2,cache=writeback -device nvme,serial=hurd96nvme0,drive=nh,bus=rp3 \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 -device e1000e,netdev=n0,bus=rp4 \
  -device qemu-xhci,id=xhci,bus=rp5 -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 \
  -drive file=fat:rw:"$E",format=raw,if=none,id=esp -device ide-hd,drive=esp,bus=ide.0 \
  -display none -vga std \
  -chardev socket,id=s0,path=ser.sock,server=on,wait=off,logfile=serial.log -serial chardev:s0 \
  -monitor unix:mon.sock,server,nowait $EXTRA
