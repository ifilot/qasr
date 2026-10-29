# SPDX-License-Identifier: GPL-3.0-only
"""Minimal independent decoder for non-interlaced RGB8/RGBA8 output."""
import struct
import zlib


def decode_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    compressed = bytearray()
    width = height = None
    ended = False
    while offset < len(data):
        n, = struct.unpack_from(">I", data, offset)
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + n]
        crc, = struct.unpack_from(">I", data, offset + 8 + n)
        assert zlib.crc32(kind + payload) == crc
        if kind == b"IHDR":
            width, height, bits, color, comp, filt, interlace = struct.unpack(">IIBBBBB", payload)
            assert (bits, comp, filt, interlace) == (8, 0, 0, 0) and color in (2,6)
            channels=4 if color==6 else 3
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            ended = True
        offset += n + 12
    assert ended and offset == len(data)
    raw = zlib.decompress(compressed)
    stride = channels * width
    assert len(raw) == height * (stride + 1)
    pixels = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        mode = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if mode == 0:
                correction = 0
            elif mode == 1:
                correction = left
            elif mode == 2:
                correction = up
            elif mode == 3:
                correction = (left + up) // 2
            else:
                assert mode == 4
                p = left + up - corner
                distances = [abs(p - left), abs(p - up), abs(p - corner)]
                correction = [left, up, corner][distances.index(min(distances))]
            row[i] = (row[i] + correction) & 255
        if channels==4:
            pixels.extend(row)
        else:
            for x in range(width):
                pixels.extend(row[3*x:3*x+3]);pixels.append(255)
        previous = row
    return width, height, bytes(pixels)
