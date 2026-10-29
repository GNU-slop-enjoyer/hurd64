#!/bin/sh
# update-tuxkbd.sh : build tuxkbd in the build VM, put it into prepared-install3.qcow2 (booted as the PC VM)
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
off
./build-vm.sh
tar -C ../tuxhurd/utils -cf - tuxkbd.c Makefile.tuxkbd | ./ssh.sh 'cd /build/tuxkbd && tar -xf - && rm -f tuxkbd && make -f Makefile.tuxkbd 2>&1 | grep -E "error|undefined" || true; test -x tuxkbd'
$SCP root@127.0.0.1:/build/tuxkbd/tuxkbd tux3-out/tuxkbd
off
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
$SCP tux3-out/tuxkbd root@127.0.0.1:/usr/local/sbin/tuxkbd.new
./ssh.sh 'mv /usr/local/sbin/tuxkbd.new /usr/local/sbin/tuxkbd; chmod 755 /usr/local/sbin/tuxkbd; rm -f /var/log/tuxkbd.log; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "tuxkbd updated"
