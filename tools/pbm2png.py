#!/usr/bin/env python3
"""Convert the sim's P4 PBM screenshots into viewable PNGs + an index page.

The headless radio (tests/sim_radio.c) writes screenshots/*.pbm; this turns
them into screenshots/*.png (1-bit grayscale, no dependencies beyond zlib)
and a screenshots/index.html contact sheet for the CI artifact.
"""
import os
import struct
import zlib

SRC = os.path.join(os.path.dirname(__file__), "..", "screenshots")
W, H = 128, 64


def pbm_rows(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data.startswith(b"P4\n")
    body = data.split(b"\n", 2)[2]
    rows = []
    for row in range(H):
        bits = []
        for x in range(W):
            byte = body[row * 16 + x // 8]
            bits.append(1 if (byte >> (7 - x % 8)) & 1 else 0)
        rows.append(bits)
    return rows


def png_bytes(rows):
    raw = b""
    for row in rows:
        raw += b"\x00" + bytes(255 if b else 0 for b in row)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def main():
    os.makedirs(SRC, exist_ok=True)
    pages = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".pbm"):
            continue
        stem = name[:-4]
        rows = pbm_rows(os.path.join(SRC, name))
        with open(os.path.join(SRC, stem + ".png"), "wb") as f:
            f.write(png_bytes(rows))
        pages.append(stem)

    with open(os.path.join(SRC, "index.html"), "w") as f:
        f.write("<html><head><title>SCAN 01 — screenshots</title>"
                "<style>body{font-family:sans-serif;background:#222;color:#eee}"
                "h2{font-weight:normal}.shot{display:inline-block;margin:8px}"
                "img{image-rendering:pixelated;background:#000;border:1px solid #555}"
                ".name{font-size:12px;text-align:center}</style></head><body>")
        f.write("<h1>SCAN 01 — the headless radio's screenshots</h1>")
        for stem in pages:
            f.write(f'<div class="shot"><img src="{stem}.png" width="256" height="128"'
                    f'alt="{stem}"><div class="name">{stem}</div></div>')
        f.write("</body></html>")
    print(f"wrote {len(pages)} PNGs + index.html to {SRC}")


if __name__ == "__main__":
    main()
