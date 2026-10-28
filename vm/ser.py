#!/usr/bin/env python3
# ser.py [text] [seconds] : send text (\n = Enter) to the VM serial console and print what comes back
import socket, sys, time, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
s = socket.socket(socket.AF_UNIX); s.connect('ser.sock'); s.settimeout(0.3)
if len(sys.argv) > 1 and sys.argv[1]:
    for ch in sys.argv[1].replace('\\n', '\r'):
        s.send(ch.encode()); time.sleep(0.01)
end, out = time.time() + float(sys.argv[2] if len(sys.argv) > 2 else 2), b''
while time.time() < end:
    try: out += s.recv(65536)
    except socket.timeout: pass
with open('serial.log', 'ab') as f: f.write(out)
sys.stdout.write(out.decode(errors='replace'))
