#!/bin/sh
# mon.sh "cmd" : one QEMU monitor command (quit, sendkey ret, info pci, ...)
cd "$(dirname "$0")" && exec ./vmctl mon "$1"
