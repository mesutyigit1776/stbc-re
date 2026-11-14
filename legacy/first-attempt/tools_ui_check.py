#!/usr/bin/env python3
"""Checks the snapshots of the menu test (port/appc/test_menus.sh): each .raw file (a line "W H", then BGRA rows) must show a UI: more than
4 percent and less than 85 percent of its pixels clearly lit. Writes a PNG beside each file. Usage: port/tools_ui_check.py DIR"""
import sys, os, struct, zlib

def load(path):
    d = open(path, 'rb').read(); nl = d.index(b'\n'); w, h = [int(x) for x in d[:nl].split()]; return w, h, d[nl + 1:]

def png(path, w, h, px):
    rows = []
    for y in range(h):
        r = bytearray(b'\x00')
        for x in range(w):
            i = (y * w + x) * 4; r += bytes((px[i + 2], px[i + 1], px[i], 255))
        rows.append(bytes(r))
    def ch(t, b):
        c = t + b; return struct.pack('>I', len(b)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(b''.join(rows), 6)) + ch(b'IEND', b''))

def main(folder):
    bad = 0
    for f in sorted(os.listdir(folder)):
        if not f.endswith('.raw'):
            continue
        w, h, px = load(os.path.join(folder, f))
        lit = 0; first = w; last = -1
        for y in range(0, h, 2):
            for x in range(0, w, 2):
                i = (y * w + x) * 4
                if px[i] + px[i + 1] + px[i + 2] > 150:
                    lit += 1
                    if x < first: first = x
                    if x > last: last = x
        frac = lit / float((h // 2) * (w // 2))
        left, right = first, w - 1 - last
        ok = 0.04 < frac < 0.85
        print('%s %-48s lit %.0f%% margins %d/%d' % ('PASS' if ok else 'FAIL', f, frac * 100, left, right))
        if not ok:
            bad += 1
        png(os.path.join(folder, f[:-4] + '.png'), w, h, px)
    return bad

if __name__ == '__main__':
    sys.exit(1 if main(sys.argv[1]) else 0)
