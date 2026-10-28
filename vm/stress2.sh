#!/bin/sh
# stress2.sh [runs] : reads + writes: 8x ls -lR /usr/share and 4x cp -a /usr/share/doc, then rm + sync.
#   300 s limit per run. Prints ms per run or HANG.
cd "$(dirname "$0")"
for r in $(seq ${1:-3}); do
  timeout 300 ./ssh.sh 'ms(){ echo $(( $(date +%s%N) / 1000000 )); }; s=$(ms)
    for n in $(seq 8); do ls -lR /usr/share >/dev/null 2>&1 & done
    for n in 1 2 3 4; do cp -a /usr/share/doc /tmp/c$n & done; wait
    rm -rf /tmp/c1 /tmp/c2 /tmp/c3 /tmp/c4; sync; e=$(ms); echo "run: $((e-s)) ms"' || { echo "HANG on run $r"; exit 1; }
done
