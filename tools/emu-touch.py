#!/usr/bin/env python3
"""Inject touch gestures into the Pebble emulator through QEMU's VNC console.

The emery (Pebble Time 2) and gabbro (Pebble Round 2) emulators expose a
touch controller that takes mouse input from the QEMU display. Start the
emulator with VNC enabled and this script drives that display over RFB:

    pebble install --emulator emery --vnc
    python3 tools/emu-touch.py tap 100 208            # tap a footer button
    python3 tools/emu-touch.py swipe 100 180 100 80   # flick up
    python3 tools/emu-touch.py pan 100 160 100 60     # slow drag (scroll)

Keep passing --vnc to every later `pebble` command (screenshot, emu-button,
logs); pebble-tool restarts the emulator when the VNC flag changes.

Usage:
  emu-touch.py info
  emu-touch.py tap X Y [hold_ms]
  emu-touch.py swipe X1 Y1 X2 Y2 [duration_ms]      (fast flick, default 120ms)
  emu-touch.py pan   X1 Y1 X2 Y2 [duration_ms]      (slow drag, default 600ms)
  emu-touch.py hold  X Y ms
Coordinates are in watch display pixels. Only the Python standard library is
needed; the VNC server is QEMU's own on localhost:5901.
"""
import socket, struct, sys, time

class RFB:
    def __init__(self, host='localhost', port=5901):
        self.s = socket.create_connection((host, port), timeout=10)
        ver = self._recv(12)
        self.s.sendall(b'RFB 003.008\n')
        n = self._recv(1)[0]
        if n == 0:
            rl = struct.unpack('>I', self._recv(4))[0]
            raise RuntimeError('server refused: ' + self._recv(rl).decode())
        types = self._recv(n)
        if 1 not in types:
            raise RuntimeError('security type None not offered: %r' % list(types))
        self.s.sendall(b'\x01')
        if struct.unpack('>I', self._recv(4))[0] != 0:
            raise RuntimeError('security handshake failed')
        self.s.sendall(b'\x01')  # ClientInit: shared
        self.w, self.h = struct.unpack('>HH', self._recv(4))
        self._recv(16)
        nl = struct.unpack('>I', self._recv(4))[0]
        self.name = self._recv(nl).decode(errors='replace')
        # QEMU pads the framebuffer width to a multiple of 8; the touch device
        # scales absolute coords to the real display size, so map accordingly.
        if (self.w, self.h) == (208, 228):
            self.dw, self.dh = 200, 228          # emery
        elif self.h == 260:
            self.dw, self.dh = 260, 260          # gabbro
        else:
            self.dw, self.dh = self.w, self.h
        # SetEncodings makes QEMU re-check pointer mode -> absolute (touch device)
        self.s.sendall(struct.pack('>BBHi', 2, 0, 1, 0))
        time.sleep(0.05)
        self.s.setblocking(False)
        self._drain()
        self.s.setblocking(True)

    def _drain(self):
        try:
            while self.s.recv(65536):
                pass
        except (BlockingIOError, socket.timeout):
            pass

    def _recv(self, n):
        buf = b''
        while len(buf) < n:
            c = self.s.recv(n - len(buf))
            if not c:
                raise RuntimeError('connection closed')
            buf += c
        return buf

    def pointer(self, mask, x, y):
        x = max(0, min(self.w - 1, int(round(x * self.w / self.dw))))
        y = max(0, min(self.h - 1, int(round(y * self.h / self.dh))))
        self.s.sendall(struct.pack('>BBHH', 5, mask, x, y))

    def close(self):
        self.s.close()

def tap(r, x, y, hold_ms=60):
    r.pointer(0, x, y); time.sleep(0.03)
    r.pointer(1, x, y); time.sleep(hold_ms / 1000.0)
    r.pointer(0, x, y)

def drag(r, x1, y1, x2, y2, duration_ms):
    steps = max(4, int(duration_ms / 16))
    r.pointer(0, x1, y1); time.sleep(0.03)
    r.pointer(1, x1, y1); time.sleep(0.03)
    for i in range(1, steps + 1):
        t = i / steps
        r.pointer(1, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t)
        time.sleep(duration_ms / 1000.0 / steps)
    r.pointer(0, x2, y2)

def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return 1
    r = RFB()
    try:
        cmd = a[0]
        if cmd == 'info':
            print('framebuffer %dx%d display %dx%d name=%r' % (r.w, r.h, r.dw, r.dh, r.name))
        elif cmd == 'tap':
            tap(r, int(a[1]), int(a[2]), int(a[3]) if len(a) > 3 else 60)
        elif cmd == 'hold':
            tap(r, int(a[1]), int(a[2]), int(a[3]))
        elif cmd == 'swipe':
            drag(r, int(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]) if len(a) > 5 else 120)
        elif cmd == 'pan':
            drag(r, int(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]) if len(a) > 5 else 600)
        else:
            print(__doc__); return 1
        time.sleep(0.1)
    finally:
        r.close()
    return 0

if __name__ == '__main__':
    sys.exit(main())
