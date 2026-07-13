#!/usr/bin/env python3
"""Encode snd-widget-shots .rgba dumps into PNGs for the website gallery.

Usage: encode.py <inDir-with-.rgba> <outDir-for-.png>

Each .rgba file is an ASCII "W H\\n" header followed by W*H*4 premultiplied
RGBA bytes (rows top-to-bottom, straight from the GL framebuffer). We
un-premultiply to straight alpha and write a standard RGBA PNG.
"""
import glob
import os
import struct
import sys
import zlib


def unpremultiply(px: bytes) -> bytearray:
    out = bytearray(px)
    for i in range(0, len(out), 4):
        a = out[i + 3]
        if a == 0:
            out[i] = out[i + 1] = out[i + 2] = 0
        elif a != 255:
            out[i] = min(255, (out[i] * 255 + a // 2) // a)
            out[i + 1] = min(255, (out[i + 1] * 255 + a // 2) // a)
            out[i + 2] = min(255, (out[i + 2] * 255 + a // 2) // a)
    return out


def write_png(path: str, w: int, h: int, rgba: bytes) -> None:
    def chunk(typ: bytes, data: bytes) -> bytes:
        body = typ + data
        return struct.pack(">I", len(data)) + body + struct.pack(
            ">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = w * 4
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (None)
        raw += rgba[y * stride:(y + 1) * stride]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main() -> int:
    in_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for path in sorted(glob.glob(os.path.join(in_dir, "*.rgba"))):
        with open(path, "rb") as f:
            header = f.readline().decode("ascii").split()
            w, h = int(header[0]), int(header[1])
            px = f.read(w * h * 4)
        straight = unpremultiply(px)
        name = os.path.splitext(os.path.basename(path))[0] + ".png"
        write_png(os.path.join(out_dir, name), w, h, bytes(straight))
        n += 1
    print(f"encoded {n} PNGs -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
