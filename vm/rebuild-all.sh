#!/bin/sh
# rebuild-all.sh : build tuxkbd, rumpdisk, rumpnet and acpi (hurd branch tux/quiet) in the build VM and
#   install them into prepared-install3.qcow2 and esp-install-fb/. Ends with an offline fsck.
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
off
./build-vm.sh
tar -C ../hurd64/utils -cf - tuxkbd.c tuxkbd-events.h Makefile.tuxkbd | ./ssh.sh 'cd /build/tuxkbd && tar -xf - && rm -f tuxkbd && make -f Makefile.tuxkbd 2>&1 | grep -E "error|undefined" || true; test -x tuxkbd'
(cd ../upstream/hurd && git archive --prefix=hurd-q/ tux/quiet rumpdisk rumpnet acpi) | ./ssh.sh 'tar -C /build -xf - && cd /build/hurd-q/b && rm -f rumpdisk/rumpdisk.static rumpnet/rumpnet rumpnet/rumpnet.static acpi/acpi.static && make -j2 rumpdisk rumpnet acpi >/build/mkall.log 2>&1 && make -C rumpdisk rumpdisk.static >>/build/mkall.log 2>&1 && make -C rumpnet rumpnet.static >>/build/mkall.log 2>&1 && make -C acpi acpi.static >>/build/mkall.log 2>&1 || { grep -m5 -E "error:|undefined" /build/mkall.log; exit 1; }'
./ssh.sh 'tar -cf - -C /build/tuxkbd tuxkbd -C /build/hurd-q/b rumpdisk/rumpdisk.static rumpnet/rumpnet rumpnet/rumpnet.static acpi/acpi.static' | tar -C tux3-out -xf -
off
cp tux3-out/acpi/acpi.static esp-install-fb/acpi-efi.static
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
tar -C tux3-out -cf - tuxkbd rumpdisk/rumpdisk.static rumpnet/rumpnet rumpnet/rumpnet.static | ./ssh.sh 'set -e; mkdir -p /root/u && tar -C /root/u -xf - && cd /root/u
install -m 755 tuxkbd /usr/local/sbin/tuxkbd.new && mv /usr/local/sbin/tuxkbd.new /usr/local/sbin/tuxkbd
install -m 755 rumpdisk/rumpdisk.static /hurd/rumpdisk-nvme.static
install -m 755 rumpnet/rumpnet /hurd/rumpnet.new && mv /hurd/rumpnet.new /hurd/rumpnet
install -m 755 rumpnet/rumpnet.static /hurd/rumpnet.static
cd / && rm -rf /root/u
# the console is 8-bit: the Debian motd logo is UTF-8 box art
printf "\nHurd64: GNU/Hurd, 64-bit. Log in as root.\n\n" > /etc/motd
echo hurd64 > /etc/hostname
sed -i "s/\\btuxhurd\\b/hurd64/g" /etc/hosts
rm -f /var/log/tuxkbd.log; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "rebuild-all done"
