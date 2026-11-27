#!/usr/bin/env python3
"""Losslessly compact PKSP sprite packs for faster .nds loading on DS/DSi.

PKS2 is a tiny 16-bit run/literal codec with bounded 128-pixel chunks; no
runtime zlib or external sprite files required. Older PKS1 readers still
work with previous builds. Each packet has 8-byte header (PKS2, w, h), then
control byte (bit 7=repeat, low 7=count-1) and one 16-bit colour or that
many literal 16-bit colours. Output is bit-perfect RGB555.
"""
import pathlib
import struct
import sys


def compact_sprite(blob: bytes) -> bytes:
    if blob[:4] != b"PKS1" or len(blob) < 8:
        return blob
    width, height = struct.unpack_from("<HH", blob, 4)
    count = width * height
    if not 0 < count <= 128 * 128 or len(blob) != 8 + count * 2:
        return blob
    pixels = struct.unpack_from(f"<{count}H", blob, 8)
    out = bytearray(b"PKS2" + blob[4:8])
    index = 0
    while index < count:
        run = 1
        while (run < 128 and index + run < count and
               pixels[index + run] == pixels[index]):
            run += 1
        if run >= 3:
            out.append(0x80 | (run - 1))
            out += struct.pack("<H", pixels[index])
            index += run
            continue
        start = index
        index += run
        while index < count and index - start < 128:
            run = 1
            while (run < 128 and index + run < count and
                   pixels[index + run] == pixels[index]):
                run += 1
            if run >= 3 or index + run - start > 128:
                break
            index += run
        n = index - start
        out.append(n - 1)
        out += struct.pack(f"<{n}H", *pixels[start:index])
    return bytes(out) if len(out) < len(blob) else blob


def compact_pack(path: pathlib.Path) -> tuple[int, int]:
    old = path.read_bytes()
    if len(old) < 16 or old[:4] != b"PKSP":
        raise ValueError(f"{path}: bad PKSP header")
    count = struct.unpack_from("<H", old, 4)[0]
    header_size = 8 + 4 * (count + 2)
    if len(old) < header_size:
        raise ValueError(f"{path}: incomplete index")
    offsets = struct.unpack_from(f"<{count+2}I", old, 8)
    if offsets[0] < header_size or offsets[-1] > len(old) or any(
        offsets[i] > offsets[i+1] for i in range(len(offsets)-1)
    ):
        raise ValueError(f"{path}: invalid offsets")
    table = []
    body = bytearray()
    for i in range(count + 1):
        table.append(header_size + len(body))
        body += compact_sprite(old[offsets[i]:offsets[i + 1]])
    table.append(header_size + len(body))
    packed = b"PKSP" + old[4:8] + struct.pack(f"<{len(table)}I", *table) + body
    if len(packed) < len(old):
        path.write_bytes(packed)
    return len(old), min(len(old), len(packed))


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: compact_sprite_packs.py <sprites_directory>")
    root = pathlib.Path(sys.argv[1])
    before = after = 0
    for path in sorted(root.glob("*.pak")):
        old, new = compact_pack(path)
        before += old
        after += new
        print(f"{path.name}: {old:,} -> {new:,} bytes")
    print(f"Lossless PKS2: {before:,} -> {after:,} bytes "
          f"({before-after:,} bytes saved before the .nds load)")


if __name__ == "__main__":
    main()
