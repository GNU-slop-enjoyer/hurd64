#!/bin/sh
# stress.sh [runs] : 16 parallel ls -lR of /usr/share, 240 s limit per run. Prints ms or HANG.
cd "$(dirname "$0")"
for r in $(seq ${1:-3}); do
  timeout 240 ./ssh.sh 'ms(){ echo $(( $(date +%s%N) / 1000000 )); }; s=$(ms); for n in $(seq 16); do ls -lR /usr/share >/dev/null 2>&1 & done; wait; e=$(ms); echo "run: $((e-s)) ms"' || { echo "HANG on run $r"; exit 1; }
done
