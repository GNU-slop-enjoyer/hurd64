#!/usr/bin/env python3
# type.sh "text" : types text into the VM console through the QEMU monitor (\n = Enter)
import socket, sys, time, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
M = {' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '/': 'slash', '.': 'dot', ',': 'comma',
     ';': 'semicolon', "'": 'apostrophe', '\\': 'backslash', '[': 'bracket_left', ']': 'bracket_right', '`': 'grave_accent'}
S = {'_': 'minus', '+': 'equal', '?': 'slash', '>': 'dot', '<': 'comma', ':': 'semicolon', '"': 'apostrophe',
     '|': 'backslash', '{': 'bracket_left', '}': 'bracket_right', '~': 'grave_accent', '!': '1', '@': '2', '#': '3',
     '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0'}
s = socket.socket(socket.AF_UNIX); s.connect('mon.sock'); s.recv(4096)
for ch in sys.argv[1].replace('\\n', '\n'):
    if ch in M: k = M[ch]
    elif ch in S: k = 'shift-' + S[ch]
    elif ch.isupper(): k = 'shift-' + ch.lower()
    else: k = ch
    s.send(('sendkey %s\n' % k).encode()); time.sleep(0.03); s.recv(65536)
