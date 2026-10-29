#!/bin/sh
# ser.sh "text" [seconds] : send text to the serial console (\n = Enter), print the reply
cd "$(dirname "$0")" && exec ./vmctl ser "$1" "${2:-2}"
