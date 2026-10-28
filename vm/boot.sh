#!/bin/sh
# boot.sh smp|smp4|up|stock [cpus] : fresh work.qcow2 on that kernel's prepared layer, wait for ssh.
#   smp*: also runs smpctl unless NOSMP=1
set -e
cd "$(dirname "$0")"
k=${1:?kernel}; L=prepared-$k.qcow2
[ "$k" = smp ] && L=prepared.qcow2
[ -f "$L" ] || { echo "no layer $L (mklayer.sh $k)" >&2; exit 1; }
./stop.sh
rm -f work.qcow2 serial.log; qemu-img create -q -f qcow2 -b $L -F qcow2 work.qcow2
(setsid ./run.sh "${2:-4}" >qemu.log 2>&1 &)
./wait-ssh.sh 180
case "$k" in smp*) [ -z "$NOSMP" ] && ./ssh.sh 'smpctl >/dev/null 2>&1; sleep 1' ;; esac
./ssh.sh 'echo "$(uname -v) | $(smpctl -s 2>/dev/null)"'
