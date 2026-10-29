#!/bin/sh
# type.sh "text" : types text on the VM keyboard (\n = Enter)
cd "$(dirname "$0")" && exec ./vmctl type "$1"
