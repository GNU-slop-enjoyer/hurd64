#!/bin/sh
# stop.sh : clean shutdown if ssh answers, else hard stop
cd "$(dirname "$0")"
./ssh.sh 'sync; poweroff' 2>/dev/null
for i in $(seq 30); do pgrep -x qemu-system-x86_64 >/dev/null || exit 0; sleep 1; done
./ssh.sh sync 2>/dev/null; ./mon.sh quit >/dev/null 2>&1; sleep 1
