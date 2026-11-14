#!/usr/bin/env python3
"""Converts a platform_snapshot file (a line "W H", then W*H*4 bytes BGRA, rows top to bottom) to a PNG.
Usage: port/tools_raw_to_png.py IN.raw OUT.png [X Y W H [SCALE]]   (a crop, enlarged SCALE times with nearest pixels)"""
import sys, zlib, struct

def main(argv):
    data = open(argv[1], 'rb').read()
    nl = data.index(b'\n')
    w, h = [int(x) for x in data[:nl].split()]
    px = data[nl + 1:]
    if len(argv) > 6:                                              # crop (and enlarge)
        cx, cy, cw, ch = [int(v) for v in argv[3:7]]; sc = int(argv[7]) if len(argv) > 7 else 1
        out = bytearray()
        for y in range(cy, min(cy + ch, h)):
            line = bytearray()
            for x in range(cx, min(cx + cw, w)):
                line += px[(y * w + x) * 4:(y * w + x) * 4 + 4] * sc
            out += bytes(line) * sc
        w, h = min(cw, w - cx) * sc, min(ch, h - cy) * sc
        px = bytes(out)
    rows = []
    for y in range(h):
        row = bytearray(b'\x00')
        base = y * w * 4
        for x in range(w):
            i = base + x * 4
            row += bytes((px[i + 2], px[i + 1], px[i], 255))      # BGRA -> RGBA
        rows.append(bytes(row))
    raw = b''.join(rows)
    def chunk(tag, body):
        c = tag + body
        return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(argv[2], 'wb').write(png)
    print('wrote', argv[2], w, h)

if __name__ == '__main__':
    main(sys.argv)
