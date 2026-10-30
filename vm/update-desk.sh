#!/bin/sh
# update-desk.sh : build tuxkbd and tuxdesk in the build VM, put them into
#   prepared-install3.qcow2 (booted as the PC VM), and the SMP kernel with the
#   fb device (build/gnumach-smp4fb) on its ESP (esp-install-fb)
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
off
./build-vm.sh
tar -C ../hurd64/utils -cf - tuxkbd.c tuxkbd-events.h Makefile.tuxkbd | ./ssh.sh 'cd /build/tuxkbd && tar -xf - && rm -f tuxkbd && make -f Makefile.tuxkbd 2>&1 | grep -E "error|undefined" || true; test -x tuxkbd'
tar -C ../hurd64 -cf - gpu/desk utils/tuxkbd-events.h | ./ssh.sh 'mkdir -p /build/lkl/src && cd /build/lkl/src && rm -rf gpu && tar -xf - && cd gpu/desk && rm -f tuxdesk && ln -sfn /build/lkl/lkl-hurd ../lkl-hurd && make LKL=/build/lkl 2>&1 | grep -E "error|undefined" || true; test -x tuxdesk'
mkdir -p tux3-out
$SCP root@127.0.0.1:/build/tuxkbd/tuxkbd tux3-out/tuxkbd
$SCP root@127.0.0.1:/build/lkl/src/gpu/desk/tuxdesk tux3-out/tuxdesk
off
gzip -9c ../build/gnumach-smp4fb/gnumach > esp-install-fb/gnumach-fb.gz
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
$SCP tux3-out/tuxkbd root@127.0.0.1:/usr/local/sbin/tuxkbd.new
$SCP tux3-out/tuxdesk root@127.0.0.1:/usr/local/bin/tuxdesk.new
$SCP ../hurd64/utils/desk root@127.0.0.1:/usr/local/bin/desk
./ssh.sh 'mv /usr/local/sbin/tuxkbd.new /usr/local/sbin/tuxkbd; chmod 755 /usr/local/sbin/tuxkbd
mv /usr/local/bin/tuxdesk.new /usr/local/bin/tuxdesk; chmod 755 /usr/local/bin/tuxdesk
chmod 755 /usr/local/bin/desk
rm -f /var/log/tuxkbd.log; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "tuxkbd and tuxdesk updated"
