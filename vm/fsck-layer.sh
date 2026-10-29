#!/bin/sh
# fsck-layer.sh LAYER.qcow2 : repair the Hurd root (partition 2) of a standalone qcow2 offline.
#   Hurd ext2fs leaves errors even after clean shutdowns, and hard stops make it worse.
set -e
cd "$(dirname "$0")"
L=${1:?layer}; R=$L.fsck.raw
qemu-img convert -O raw "$L" "$R"
/usr/sbin/e2fsck -fy "$R?offset=$((1953792*512))" >/dev/null 2>&1 || true
/usr/sbin/e2fsck -fn "$R?offset=$((1953792*512))" >/dev/null 2>&1 || { echo "fsck-layer: $L still has errors" >&2; rm -f "$R"; exit 1; }
qemu-img convert -O qcow2 "$R" "$L.new" && mv "$L.new" "$L" && rm -f "$R"
echo "fsck-layer: $L clean"
