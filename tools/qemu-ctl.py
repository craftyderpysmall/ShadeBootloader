#!/usr/bin/env python3
"""Drive a headless QEMU (make run-headless) through its monitor socket.

usage: qemu-ctl.py SOCKET CMD [ARGS] [CMD [ARGS] ...]
  shot FILE.png      screendump to PNG
  key NAME           sendkey (e.g. right, ret, esc, tab, f3)
  mouse X Y          move the absolute pointer to pixel X,Y
  click              left click at the current position
  wait MS            sleep
"""
import os
import socket
import sys
import tempfile
import time


def send(sock, line):
    sock.sendall((line + '\n').encode())
    time.sleep(0.15)
    try:
        sock.recv(65536)
    except BlockingIOError:
        pass


def main():
    path, args = sys.argv[1], sys.argv[2:]
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    s.setblocking(False)
    time.sleep(0.2)
    try:
        s.recv(65536)
    except BlockingIOError:
        pass
    i = 0
    while i < len(args):
        cmd = args[i]
        if cmd == 'shot':
            out = args[i + 1]
            fd, ppm = tempfile.mkstemp(suffix='.ppm')
            os.close(fd)
            send(s, f'screendump {ppm}')
            time.sleep(0.5)
            from PIL import Image
            Image.open(ppm).save(out)
            os.remove(ppm)
            print('saved', out)
            i += 2
        elif cmd == 'key':
            send(s, f'sendkey {args[i + 1]}')
            i += 2
        elif cmd == 'mouse':
            send(s, f'mouse_move {args[i + 1]} {args[i + 2]}')
            i += 3
        elif cmd == 'click':
            send(s, 'mouse_button 1')
            send(s, 'mouse_button 0')
            i += 1
        elif cmd == 'wait':
            time.sleep(int(args[i + 1]) / 1000)
            i += 2
        else:
            sys.exit(f'unknown command {cmd}')


if __name__ == '__main__':
    main()
