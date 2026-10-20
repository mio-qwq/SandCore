"""Copyright (c) 2026 mio. Original classic globe/window icon in native SCB2."""
import math
from pathlib import Path
import struct


def make_icon():
    pixels = [0] * (32 * 32)

    def dot(x, y, rgb):
        if 0 <= x < 32 and 0 <= y < 32:
            pixels[y * 32 + x] = 0xff000000 | rgb

    def rect(x, y, w, h, rgb):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                dot(xx, yy, rgb)

    rect(2, 11, 25, 19, 0x344258)
    rect(1, 10, 25, 19, 0xc0c0c0)
    rect(2, 11, 23, 1, 0xffffff)
    rect(2, 12, 1, 15, 0xffffff)
    rect(3, 13, 21, 3, 0x000080)
    rect(4, 17, 19, 9, 0xffffff)
    for y, w in ((19, 8), (22, 12), (24, 7)):
        rect(6, y, w, 1, 0x7185a1)
    for y in range(-11, 12):
        for x in range(-11, 12):
            distance = x*x + y*y
            if distance <= 121:
                shade = max(0.28, math.sqrt(1-distance/121)*0.65 + 0.35 - x/100)
                dot(19+x, 12+y, (int(28*shade) << 16) |
                    (int(104*shade) << 8) | int(204*shade))
                if y in (-6, 0, 6) or abs(x) == (5 if abs(y) < 8 else 3):
                    dot(19+x, 12+y, 0x96cbe6)
    for x, y, w, h in ((14, 5, 4, 3), (13, 8, 3, 3), (17, 13, 4, 3),
                        (19, 16, 3, 4), (24, 8, 3, 4)):
        rect(x, y, w, h, 0x5d9e68)
    for degrees in range(15, 195, 3):
        radians = math.radians(degrees)
        x = round(18 + 14 * math.cos(radians))
        y = round(13 + 7 * math.sin(radians) - 4 * math.cos(radians))
        dot(x, y, 0xd0a848)
        dot(x, y-1, 0xffe08a)
    data = b'SCB2MIO\0' + struct.pack('<6I', 32, 32, 2, 0, 32*32*4, 0x004f494d)
    data += struct.pack('<1024I', *pixels)
    target = Path(__file__).resolve().parents[1] / 'resources/BROWSER.SCB'
    target.write_bytes(data)
    return data


if __name__ == '__main__':
    print('BROWSER.SCB:', len(make_icon()), 'bytes')
