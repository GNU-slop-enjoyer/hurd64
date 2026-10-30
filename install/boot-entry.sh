#!/bin/sh
# boot-entry.sh : run once with sudo on Linux. The two Netac disks look the same in the
#   firmware boot menu, so this makes hurd96 reachable by name:
#   - a firmware boot entry called "hurd96" (kept behind Gentoo in the boot order)
#   - a "hurd96" line in Gentoo's GRUB menu
#   Afterwards `sudo install/boot-entry.sh next` boots hurd96 once on the next restart.
set -e
serial=AA20260908500G205715
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
disk=
for d in /sys/block/nvme*n1; do
	[ "$(tr -d ' ' < "$d/device/serial")" = "$serial" ] && disk=/dev/${d##*/}
done
[ -n "$disk" ] || { echo "the hurd96 disk (serial $serial) is not there" >&2; exit 1; }
[ "$(blkid -o value -s LABEL "${disk}p3")" = HURD96 ] || { echo "${disk}p3 is not the HURD96 ESP" >&2; exit 1; }

num=$(efibootmgr | sed -n 's/^Boot\([0-9A-F]\{4\}\)\*\? hurd96.*/\1/p' | head -1)
if [ "${1:-}" = next ]; then
	[ -n "$num" ] || { echo "no hurd96 entry yet: run this without arguments first" >&2; exit 1; }
	efibootmgr -q -n "$num"
	echo "the next restart boots hurd96 ($num), once"
	exit 0
fi

if [ -z "$num" ]; then
	order=$(efibootmgr | sed -n 's/^BootOrder: //p')
	efibootmgr -q -c -d "$disk" -p 3 -L hurd96 -l '\EFI\BOOT\BOOTX64.EFI'
	num=$(efibootmgr | sed -n 's/^Boot\([0-9A-F]\{4\}\)\*\? hurd96.*/\1/p' | head -1)
	# the same boot order as before, hurd96 last
	efibootmgr -q -o "$order,$num"
fi

cat > /etc/grub.d/42_hurd96 <<'G'
#!/bin/sh
cat <<'M'
menuentry "hurd96" {
	insmod part_msdos
	insmod fat
	insmod chain
	search --no-floppy --label --set=root HURD96
	chainloader /EFI/BOOT/BOOTX64.EFI
}
M
G
chmod 755 /etc/grub.d/42_hurd96
cp -n /boot/grub/grub.cfg /boot/grub/grub.cfg.before-hurd96
grub-mkconfig -o /boot/grub/grub.cfg
efibootmgr
echo "done: pick hurd96 in Gentoo's GRUB menu or in the firmware boot menu"
