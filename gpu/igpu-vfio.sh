#!/bin/sh
# Give the Ryzen iGPU (13:00.0, 1002:13c0, no monitor attached) to vfio-pci
# at boot instead of amdgpu, so the Hurd test VM can drive it. The RX 9070 XT
# (03:00.0) and your screens are not touched. Takes effect on the next reboot
# (and login); nothing changes now.
#
#   sudo gpu/igpu-vfio.sh          set up
#   sudo gpu/igpu-vfio.sh undo     back to amdgpu (after a reboot)
#
# What it writes:
#   /etc/modprobe.d/hurd96-igpu-vfio.conf     vfio-pci claims 1002:13c0 before amdgpu
#   /etc/udev/rules.d/72-hurd96-vfio.rules    the user may open /dev/vfio/* (QEMU runs as you)
#   /etc/security/limits.d/hurd96-vfio.conf   QEMU may lock the VM's RAM (VFIO pins it): 8 GiB
set -eu
user=${SUDO_USER:?run with sudo}
mod=/etc/modprobe.d/hurd96-igpu-vfio.conf
rules=/etc/udev/rules.d/72-hurd96-vfio.rules
limits=/etc/security/limits.d/hurd96-vfio.conf
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
if [ "${1:-}" = undo ]; then
	rm -f "$mod" "$rules" "$limits"
	echo "removed; the iGPU goes back to amdgpu after a reboot"
	exit 0
fi
cat > "$mod" <<'EOF'
# hurd96 GPU port: iGPU for VMs (gpu/igpu-vfio.sh; "undo" removes this)
options vfio-pci ids=1002:13c0
softdep amdgpu pre: vfio-pci
EOF
cat > "$rules" <<EOF
# hurd96 GPU port (gpu/igpu-vfio.sh)
SUBSYSTEM=="vfio", OWNER="$user", MODE="0600"
EOF
mkdir -p /etc/security/limits.d
cat > "$limits" <<EOF
# hurd96 GPU port (gpu/igpu-vfio.sh): QEMU with VFIO pins guest RAM
$user soft memlock 8388608
$user hard memlock 8388608
EOF
# amdgpu inside LKL has no ACPI: keep a copy of the VFCT table (the
# iGPU's VBIOS) where the build can read it (gpu/vfct-extract.c)
vfct=/home/$user/src/hurd96/build/lkl/VFCT
if [ -r /sys/firmware/acpi/tables/VFCT ]; then
	mkdir -p "$(dirname "$vfct")"
	cp /sys/firmware/acpi/tables/VFCT "$vfct"
	chown "$user" "$vfct"
	echo "copied the VFCT table to $vfct"
fi
echo "done. After the next reboot:"
echo "  lspci -ks 13:00.0      should say: Kernel driver in use: vfio-pci"
echo "  ulimit -l              should say: 8388608"
