#!/bin/sh
# update-hurd96kbd.sh : build hurd96kbd in the build VM, put it into prepared-install3.qcow2 (booted as the PC VM)
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
off
./build-vm.sh
tar -C ../hurd96/utils -cf - hurd96kbd.c hurd96kbd-events.h Makefile.hurd96kbd | ./ssh.sh 'cd /build/hurd96kbd && tar -xf - && rm -f hurd96kbd && make -f Makefile.hurd96kbd 2>&1 | grep -E "error|undefined" || true; test -x hurd96kbd'
$SCP root@127.0.0.1:/build/hurd96kbd/hurd96kbd out/hurd96kbd
off
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
$SCP out/hurd96kbd root@127.0.0.1:/usr/local/sbin/hurd96kbd.new
./ssh.sh 'mv /usr/local/sbin/hurd96kbd.new /usr/local/sbin/hurd96kbd; chmod 755 /usr/local/sbin/hurd96kbd; rm -f /var/log/hurd96kbd.log; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "hurd96kbd updated"
