"""Image files for the tests of the loaders: a TGA writer with every option that the exe loader reads. The files are
made from a seed so that the tests do not change from one run to the next."""
import random
import struct


def pattern(seed, count):
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(count))


def tga_header_bytes(image_type, width, height, depth, color_map_type=0, first=0, length=0, entry=0, descriptor=0,
                     id_length=0):
    """The 18 header bytes: id length, colour map type, image type, colour map first (2), length (2), entry size, x origin
    (2), y origin (2), width (2), height (2), pixel depth, descriptor."""
    return bytes([id_length, color_map_type, image_type, first & 255, first >> 8, length & 255, length >> 8, entry, 0, 0, 0, 0,
                  width & 255, width >> 8, height & 255, height >> 8, depth, descriptor])


def raw_packets(pixels, bytes_per_pixel, rng):
    """Run length packets for a list of pixels (each a bytes object): a mix of runs and raw packets."""
    out = bytearray()
    i = 0
    while i < len(pixels):
        run = 1
        while i + run < len(pixels) and run < 128 and pixels[i + run] == pixels[i]:
            run += 1
        if run > 1 and (run > 2 or rng.random() < 0.5):
            out.append(0x80 + run - 1)
            out += pixels[i]
            i += run
        else:
            count = 1
            while i + count < len(pixels) and count < 128 and pixels[i + count] != pixels[i + count - 1] and rng.random() < 0.7:
                count += 1
            out.append(count - 1)
            for k in range(count):
                out += pixels[i + k]
            i += count
    return bytes(out)


def make_tga(image_type=2, width=4, height=4, depth=24, seed=1, descriptor=0, id_text=b"", color_map=None,
             map_first=0, entry=24, rle=False, pixels=None, truncated=0, color_map_type=None):
    """A TGA file. color_map: number of entries (for the types 1 and 9; also written for other types when given).
    rle: use run length packets. pixels: the bytes of the pixels (default from the seed)."""
    rng = random.Random(seed)
    cm_type = color_map_type if color_map_type is not None else (1 if color_map is not None else 0)
    entries = color_map or 0
    header = tga_header_bytes(image_type, width, height, depth, cm_type, map_first, entries, entry if cm_type else 0,
                              descriptor, len(id_text))
    body = bytearray(header) + id_text
    if entries:
        body += pattern(seed + 100, entries * ((entry + 7) // 8))
    per_pixel = (depth + 7) // 8
    if pixels is None:
        # a picture with runs, so that the packets of the run length files have runs
        data = []
        for _ in range(width * height):
            if data and rng.random() < 0.4:
                data.append(data[-1])
            else:
                data.append(bytes(rng.randrange(256) for _ in range(per_pixel)))
    else:
        data = [pixels[i:i + per_pixel] for i in range(0, len(pixels), per_pixel)]
    if rle:
        body += raw_packets(data, per_pixel, rng)
    else:
        body += b"".join(data)
    if truncated:
        body = body[:-truncated]
    return bytes(body)
