#!/bin/sh
# stop.sh : clean shutdown if ssh answers, else hard stop (only for throwaway layers)
cd "$(dirname "$0")"
alive() { [ -f qemu.pid ] && kill -0 "$(cat qemu.pid)" 2>/dev/null; }
alive || exit 0
timeout 20 ./ssh.sh 'sync; poweroff' 2>/dev/null
for i in $(seq 60); do alive || exit 0; sleep 1; done
./mon.sh quit >/dev/null 2>&1; sleep 2
alive && kill "$(cat qemu.pid)"
exit 0
