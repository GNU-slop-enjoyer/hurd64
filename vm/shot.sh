#!/bin/sh
# shot.sh name : screendump of the running VM to name.png
cd "$(dirname "$0")"
python3 -c "
import socket,time;s=socket.socket(socket.AF_UNIX);s.connect('mon.sock');s.recv(4096);s.send(b'screendump /tmp/hv.ppm\n');time.sleep(1)"
ffmpeg -loglevel error -y -i /tmp/hv.ppm "$1.png" && rm -f /tmp/hv.ppm
