#!/bin/sh
# shm-tmpfs.sh : in the running PC VM, put /dev/shm (shm_open) and /run/user
#   (XDG_RUNTIME_DIR) on tmpfs, as passive translators so they come back on
#   every boot. Without this, Wayland's shared frame buffers live in files on
#   the ext2 root and every frame is written to disk.
set -e
cd "$(dirname "$0")"
./ssh.sh 'set -e
if [ -L /dev/shm ]; then rm /dev/shm; fi
mkdir -p /dev/shm /run/user
settrans -fgp /dev/shm /hurd/tmpfs 1G
settrans -fgp /run/user /hurd/tmpfs 256M
chmod 1777 /dev/shm
ls -ld /dev/shm /run/user; showtrans /dev/shm /run/user'
