#!/bin/sh
# Hurd test VM. RAM and CPUs are capped so the host stays usable.
#   [DISK=x.qcow2] [MEM=2G] ./run.sh [cpus]     default work.qcow2, 2G, 4 CPUs
#   monitor: mon.sock (mon.sh)  serial: ser.sock (ser.sh), logged to serial.log  ssh: ssh.sh
cd "$(dirname "$0")"
# BUS=ahci puts the disk on an AHCI (SATA) controller, BUS=nvme on an NVMe controller, instead of IDE
D="${DISK:-work.qcow2}"
if [ "$BUS" = nvme ]; then
  DRIVE="-drive id=d0,if=none,file=$D,format=qcow2,cache=writeback -device nvme,serial=tuxnvme0,drive=d0${NVME_ADDR:+,addr=$NVME_ADDR}"
elif [ "$BUS" = ahci ]; then
  DRIVE="-device ahci,id=ahci -drive id=d0,if=none,file=$D,format=qcow2,cache=writeback -device ide-hd,drive=d0,bus=ahci.0"
else
  DRIVE="-drive file=$D,format=qcow2,if=ide,cache=writeback"
fi
exec qemu-system-x86_64 -pidfile qemu.pid -enable-kvm -cpu host -m "${MEM:-2G}" -smp "${1:-4}" \
  $DRIVE \
  ${EXTRA:+$EXTRA} -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2222-:22 \
  -display none -vga std \
  -chardev socket,id=s0,path=ser.sock,server=on,wait=off,logfile=serial.log -serial chardev:s0 \
  -monitor unix:mon.sock,server,nowait
