#!/bin/sh
# shot.sh name : screendump of the running VM to name.png
cd "$(dirname "$0")"
./vmctl mon "screendump /tmp/hv.ppm" >/dev/null && sleep 1 && ffmpeg -loglevel error -y -i /tmp/hv.ppm "$1.png" && rm -f /tmp/hv.ppm
