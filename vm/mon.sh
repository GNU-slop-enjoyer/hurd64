#!/bin/sh
# mon.sh "cmd" : one QEMU monitor command (quit, sendkey ret, ...)
cd "$(dirname "$0")"
python3 -c "
import socket,time,sys;s=socket.socket(socket.AF_UNIX);s.connect('mon.sock');s.recv(4096);s.send((sys.argv[1]+'\n').encode());time.sleep(0.5);print(s.recv(65536).decode(errors='replace').split('\n',1)[-1].rsplit('(qemu)',1)[0].strip())" "$1"
