#!/bin/sh
# update-desk.sh : build hurd96kbd, lilith, 9cynth and fbconsole in the build VM
#   (wlroots 0.19 installed there, see hurd96/wayland/README.md) and put them with the
#   Debian libraries they need into prepared-install3.qcow2 (booted as the PC VM), and
#   the SMP kernel with the fb device (build/gnumach-smp4fb) on its ESP (esp-install-fb).
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
PKGS="libexpat1 libffi8 libfontconfig1 libfreetype6 libfcft4t64 libharfbuzz0b libpixman-1-0 libutf8proc3 libxkbcommon0 xkb-data fonts-dejavu-core ncurses-term libbsd0 libmd0 libfontenc1 libxau6 libxdmcp6 libxcb1 libxcb-composite0 libxcb-ewmh2 libxcb-icccm4 libxcb-render0 libxcb-res0 libxcb-xfixes0 libxcvt0 libxfont2 libxshmfence1 x11-xkb-utils xfonts-base libxcb-errors0 libpango-1.0-0 libpangocairo-1.0-0 libcairo2 libglib2.0-0t64 libavformat63 libavcodec63 libavutil61 libswresample7 libswscale10 libpulse0 tty-clock dmz-cursor-theme"
# programs for the desktop: X11 ones run through Xwayland
APPS="suckless-tools xterm galculator mousepad thunar mc htop nano w3m less"
off
./build-vm.sh
tar -C ../hurd96/utils -cf - hurd96kbd.c hurd96kbd-events.h Makefile.hurd96kbd fbconsole.c | ./ssh.sh 'mkdir -p /build/hurd96kbd && cd /build/hurd96kbd && tar -xf - && rm -f hurd96kbd && make -f Makefile.hurd96kbd 2>&1 | grep -E "error|undefined" || true; cc -O2 -o fbconsole fbconsole.c && test -x hurd96kbd'
tar -C ../hurd96 -cf - wayland/lilith utils/hurd96kbd-events.h | ./ssh.sh 'mkdir -p /build/l96 && cd /build/l96 && rm -rf wayland utils && tar xf - && cd wayland/lilith && make -j2 2>&1 | grep -E " error|\*\*\*" || true; test -x lilith && test -x 9cynth && make install >/dev/null'
./ssh.sh 'tar cf - -C /build/hurd96kbd hurd96kbd fbconsole' > out/kbd.tar
./ssh.sh 'cd /usr/local && tar cf - bin/lilith bin/9cynth bin/foot bin/footclient bin/Xwayland share/applications/9cynth.desktop lib/libepoll-shim.so* lib/x86_64-gnu/libwayland-*.so* lib/x86_64-gnu/libdrm.so* lib/x86_64-gnu/libwlroots-0.19.so' > out/wayland.tar
off
perl -pe 's#/home/[a-z]{3}/src/hurd(64|96)/#/usr/src/hurd96/hurd/#g' ../build/gnumach-smp4fb/gnumach | gzip -9c > esp-install-fb/gnumach-fb.gz
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
./ssh.sh "apt-get -q update >/dev/null 2>&1; DEBIAN_FRONTEND=noninteractive apt-get -y -q install $PKGS $APPS 2>&1 | tail -2"
./ssh.sh 'tar xf - -C /usr/local/sbin' < out/kbd.tar
./ssh.sh 'rm -f /usr/local/bin/hurdwm /usr/local/lib/x86_64-gnu/libwlroots-0.18.so; tar xf - -C /usr/local && ldconfig && mkdir -p /run/user' < out/wayland.tar
$SCP ../hurd96/utils/desk ../hurd96/utils/hurd96-term root@127.0.0.1:/usr/local/bin/
./ssh.sh 'chmod 755 /usr/local/bin/desk /usr/local/bin/hurd96-term /usr/local/sbin/hurd96kbd /usr/local/sbin/fbconsole
rm -f /var/log/hurd96kbd.log
ldd /usr/local/bin/lilith /usr/local/bin/9cynth /usr/local/bin/foot | grep "not found"; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "desktop updated"
