#!/bin/sh
# reinstall.sh : put the tested PC VM image (vm/prepared-install3.qcow2 and the
#   ESP folder vm/esp-install-fb) on the Hurd disk, through install.sh and
#   the disk helper in DISK_HELPER (only the Netac disk, by serial). Wipes that disk. -n: dry run.
set -e
N=""; [ "${1:-}" = -n ] && N=-n
here=$(cd "$(dirname "$0")" && pwd)
vm=${VM:-$HOME/src/hurd96/vm}
cd "$vm"
./stop.sh
./fsck-layer.sh prepared-install3.qcow2
qemu-img convert -O raw prepared-install3.qcow2 install-new.raw
tar -C esp-install-fb --owner=0 --group=0 --numeric-owner -cf esp-install-new.tar .
"$here/install.sh" $N install-new.raw esp-install-new.tar
