#!/bin/sh
# update-wayland.sh : copy the Wayland stack built in the build VM (/usr/local:
#   epoll-shim, libwayland, libdrm, wlroots, foot, hurdwm) into
#   prepared-install3.qcow2 and install the Debian libraries it needs there.
#   Needs the build VM image to have them installed (see hurd64/wayland/README.md).
set -e
cd "$(dirname "$0")"
SCP="scp -q -i id_hurd -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
off() { ./ssh.sh 'sync; poweroff' 2>/dev/null || true; for i in $(seq 90); do kill -0 "$(cat qemu.pid 2>/dev/null)" 2>/dev/null || break; sleep 1; done; ./stop.sh; }
PKGS="libexpat1 libffi8 libfontconfig1 libfreetype6 libfcft4t64 libharfbuzz0b libpixman-1-0 libutf8proc3 libxkbcommon0 xkb-data fonts-dejavu-core ncurses-term libbsd0 libmd0 libfontenc1 libxau6 libxdmcp6 libxcb1 libxcb-composite0 libxcb-ewmh2 libxcb-icccm4 libxcb-render0 libxcb-res0 libxcb-xfixes0 libxcvt0 libxfont2 libxshmfence1 x11-xkb-utils xfonts-base"
# programs for the desktop: X11 ones run through Xwayland
APPS="xterm galculator mousepad thunar mc htop nano w3m less"
off
./build-vm.sh
./ssh.sh 'cd /usr/local && tar cf - bin/hurdwm bin/foot bin/footclient bin/Xwayland lib/libepoll-shim.so* lib/x86_64-gnu/libwayland-*.so* lib/x86_64-gnu/libdrm.so* lib/x86_64-gnu/libwlroots-0.18.so' > tux3-out/wayland.tar
off
./fsck-layer.sh prepared-install3.qcow2
rm -f serial.log; (DISK=prepared-install3.qcow2 setsid ./run-pc.sh 2 >qemu.log 2>&1 &)
./wait-ssh.sh 240
./ssh.sh "apt-get -q update >/dev/null 2>&1; DEBIAN_FRONTEND=noninteractive apt-get -y -q install $PKGS $APPS 2>&1 | tail -2"
./ssh.sh 'tar xf - -C /usr/local && ldconfig && mkdir -p /run/user && ls /usr/local/bin' < tux3-out/wayland.tar
$SCP ../hurd64/utils/desk root@127.0.0.1:/usr/local/bin/desk
./ssh.sh 'echo hurd64 > /etc/hostname; sed -i "s/\\btuxhurd\\b/hurd64/g" /etc/hosts /etc/motd 2>/dev/null; true'
./ssh.sh 'chmod 755 /usr/local/bin/desk; ldd /usr/local/bin/hurdwm /usr/local/bin/foot | grep "not found"; sync'
off
./fsck-layer.sh prepared-install3.qcow2
echo "wayland stack installed"
