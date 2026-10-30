#!/bin/sh
# install.sh [-n] IMAGE ESP.tar : put hurd96 on the target of DISK_HELPER (the root helper that accepts one NVMe disk,
#   chosen by serial). Wipes the disk. -n only prints what would run.
#   p1 swap (from the image), p2 root ext2 grown to 32 GiB, p3 EFI system partition 512 MiB.
set -e
N=""; [ "$1" = -n ] && { N=-n; shift; }
IMG=${1:?image}; ESP=${2:?esp tar}
T="sudo -n ${DISK_HELPER:?set DISK_HELPER to the disk helper} $N"

$T info
$T umount all
for p in 3 2 1; do $T wipe $p 2>/dev/null || true; done
$T wipe disk
$T write disk < "$IMG"
$T partition <<'P'
label: dos
label-id: 0x96961996
start=2048, size=1951744, type=82
start=1953792, size=67108864, type=83
start=69062656, size=1048576, type=ef, bootable
P
$T resizefs 2
$T mkfs vfat 3 HURD96
$T put 3 < "$ESP"
$T umount all
$T fsck 2
$T info
