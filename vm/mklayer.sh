#!/bin/sh
# mklayer.sh up|smp4|stock : prepared-<k>.qcow2 on prepared.qcow2, booting that kernel by default.
#   up/smp4 = upstream GNU Mach from ../build/gnumach-<k>; stock = Debian's kernel (GRUB entry 0)
#   (smp needs no layer: prepared.qcow2 already boots tux-smp)
set -e
cd "$(dirname "$0")"
k=${1:?kernel}; L=prepared-$k.qcow2
./stop.sh
rm -f $L; qemu-img create -q -f qcow2 -b prepared.qcow2 -F qcow2 $L
(DISK=$L setsid ./run.sh 1 >qemu.log 2>&1 &)
./wait-ssh.sh 180
if [ "$k" = stock ]; then
  ./ssh.sh 'grub-set-default 0'
else
  scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    ../build/gnumach-$k/gnumach.gz root@127.0.0.1:/boot/gnumach-tux-$k.gz
  ./ssh.sh "K=$k sh -s" <<'R'
set -e
awk '/^menuentry "tux-smp"/{p=1} p{print} p&&/^}/{exit}' /etc/grub.d/40_custom | sed "s|tux-smp|tux-$K|g" >> /etc/grub.d/40_custom
update-grub >/dev/null 2>&1
grub-set-default tux-$K
R
fi
./ssh.sh 'grep saved_entry /boot/grub/grubenv; grep -c "gnumach-tux" /boot/grub/grub.cfg; sync'
./stop.sh
echo "made $L"
