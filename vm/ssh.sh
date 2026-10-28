#!/bin/sh
# ssh.sh [cmd] : root shell in the VM
cd "$(dirname "$0")"
exec ssh -q -i id_hurd -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 root@127.0.0.1 "$@"
