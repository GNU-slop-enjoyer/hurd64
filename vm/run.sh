#!/bin/sh
# Hurd test VM. RAM and CPUs are capped so the host stays usable.
#   [DISK=x.qcow2] [MEM=2G] ./run.sh [cpus]     default work.qcow2, 2G, 4 CPUs
#   monitor: mon.sock (mon.sh)  serial: ser.sock (ser.py), logged to serial.log  ssh: ssh.sh
cd "$(dirname "$0")"
exec qemu-system-x86_64 -enable-kvm -cpu host -m "${MEM:-2G}" -smp "${1:-4}" \
  -drive file="${DISK:-work.qcow2}",format=qcow2,if=ide,cache=writeback \
  -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2222-:22 \
  -display none -vga std \
  -chardev socket,id=s0,path=ser.sock,server=on,wait=off,logfile=serial.log -serial chardev:s0 \
  -monitor unix:mon.sock,server,nowait
