#!/bin/sh
# prepare.sh : prepared.qcow2 = Debian image + ssh key + tux kernel/tools. Tests run on work.qcow2 above it.
set -e
cd "$(dirname "$0")"
K=../build/gnumach-smp/gnumach.gz
rm -f prepared.qcow2 work.qcow2 serial.log
qemu-img create -q -f qcow2 -b base.img -F raw prepared.qcow2
(DISK=prepared.qcow2 setsid ./run.sh 4 >qemu.log 2>&1 &)
sleep 55
./type.sh 'root\n'; sleep 3
./type.sh "mkdir -p /root/.ssh; echo '$(cat id_hurd.pub)' > /root/.ssh/authorized_keys; chmod 700 /root/.ssh; chmod 600 /root/.ssh/authorized_keys\n"
./wait-ssh.sh
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
$SCP "$K" root@127.0.0.1:/boot/gnumach-tux-smp.gz
$SCP ../hurd64/utils/smpctl.c root@127.0.0.1:/root/
./ssh.sh 'set -e
gcc -O2 -Wall -o /usr/local/sbin/smpctl /root/smpctl.c
awk "/^menuentry .Debian GNU\/Hurd GNU\/Hurd/{p=1} p{print} p&&/^}/{exit}" /boot/grub/grub.cfg |
  sed "s|^menuentry .Debian GNU/Hurd GNU/Hurd. [^{]*{|menuentry \"tux-smp\" --id tux-smp {|; s|/boot/gnumach-1.8-amd64-up.gz root=part:2:device:wd0|/boot/gnumach-tux-smp.gz root=part:2:device:wd0 console=com0|" >> /etc/grub.d/40_custom
sed -i "s/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/" /etc/default/grub
update-grub >/dev/null 2>&1; grub-set-default 0; grep -c "tux-smp.gz.*console=com0" /boot/grub/grub.cfg'
./stop.sh
qemu-img create -q -f qcow2 -b prepared.qcow2 -F qcow2 work.qcow2
echo prepared
