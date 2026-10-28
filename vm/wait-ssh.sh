#!/bin/sh
# wait-ssh.sh [seconds] : wait until the VM answers on ssh
cd "$(dirname "$0")"
t=${1:-150}; while [ $t -gt 0 ]; do ./ssh.sh true 2>/dev/null && exit 0; sleep 5; t=$((t-5)); done; echo "no ssh" >&2; exit 1
