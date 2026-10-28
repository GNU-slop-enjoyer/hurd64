#!/bin/sh
# boot-smp.sh [cpus] : fresh work.qcow2 on prepared.qcow2 (default entry tux-smp), boot, enable all CPUs
#   NOSMP=1 leaves the APs parked
set -e
cd "$(dirname "$0")"
./stop.sh
rm -f work.qcow2 serial.log; qemu-img create -q -f qcow2 -b prepared.qcow2 -F qcow2 work.qcow2
(setsid ./run.sh "${1:-8}" >qemu.log 2>&1 &)
./wait-ssh.sh 180
if [ "$NOSMP" ]; then ./ssh.sh 'uname -v; smpctl -s'; else ./ssh.sh 'uname -v; smpctl >/dev/null 2>&1; sleep 1; smpctl -s'; fi
