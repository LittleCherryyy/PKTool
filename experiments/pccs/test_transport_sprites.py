#!/usr/bin/env python3
"""VPT031: offline checks for pinned transport art manifests and PNG conversion."""
import importlib.util
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("build_transport_sprites", ROOT / "tools" / "build_transport_sprites.py")
sprites = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sprites)

required = {
    "transport-redgreen-jp": "generation-i/red-green-japan",
    "transport-redblue-int": "generation-i/red-blue/transparent",
    "transport-yellow": "generation-i/yellow/transparent",
    "transport-gold": "generation-ii/gold/transparent",
    "transport-silver": "generation-ii/silver/transparent",
    "transport-crystal": "generation-ii/crystal/transparent",
    "transport-rs": "generation-iii/ruby-sapphire",
    "transport-frlg": "generation-iii/firered-leafgreen",
    "transport-emerald": "generation-iii/emerald",
}
assert len(sprites.KINDS) == 15
for kind, folder in required.items():
    assert sprites.KINDS[kind][0] == folder
    if kind in ("transport-gold", "transport-silver", "transport-crystal",
                "transport-rs", "transport-frlg", "transport-emerald"):
        assert sprites.KINDS[kind + "-shiny"][0] == folder + "/shiny"
for kind, (folder, count) in sprites.KINDS.items():
    assert count == (151 if folder.startswith("generation-i/") else 251)
    assert folder and ".." not in folder and not folder.startswith("/")
assert "transport-blue-jp" not in sprites.KINDS  # No verified pinned sheet; opt-in manual pack.


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


# 2x1 RGBA image: transparent pixel followed by opaque green. Cropping must
# preserve the second pixel and write DS BGR555 with the opaque flag set.
ihdr = struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0)
pixels = b"\x00" + bytes([0, 0, 0, 0, 0, 255, 0, 255])
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
       chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
result = sprites.convert(png)
assert result[:4] == b"PKS1"
assert struct.unpack_from("<HH", result, 4) == (1, 1)
assert struct.unpack_from("<H", result, 8) == (0x8000 | (31 << 5),)
print("PASS: 15 exact edition art packs, distinct shiny sheets, safe JP Blue fallback, PNG/PKS1 conversion")
