"""Checks that compact NitroFS art retains every RGB555 pixel and index."""
import pathlib
import struct
import tempfile
import unittest

from compact_sprite_packs import compact_sprite, compact_pack


def decode(blob):
    if blob[:4] == b"PKS1":
        w, h = struct.unpack_from("<HH", blob, 4)
        return list(struct.unpack_from(f"<{w*h}H", blob, 8))
    assert blob[:4] == b"PKS2"
    w, h = struct.unpack_from("<HH", blob, 4)
    n = w * h
    pos = 8
    pixels = []
    while len(pixels) < n:
        code = blob[pos]
        pos += 1
        count = (code & 127) + 1
        if code & 128:
            value = struct.unpack_from("<H", blob, pos)[0]
            pos += 2
            pixels += [value] * count
        else:
            pixels += list(struct.unpack_from(f"<{count}H", blob, pos))
            pos += count * 2
    assert pos == len(blob) and len(pixels) == n
    return pixels


def raw(pixels, width, height):
    return b"PKS1" + struct.pack("<HH", width, height) + struct.pack(
        f"<{len(pixels)}H", *pixels
    )


class TestCompactPacks(unittest.TestCase):
    def test_runs_and_literals(self):
        pixels = ([0] * 90 + list(range(31)) + [0x801F] * 60 +
                  [0xFFFF, 0x8000] + [0] * 73)
        source = raw(pixels, 128, 2)
        packed = compact_sprite(source)
        self.assertEqual(packed[:4], b"PKS2")
        self.assertLess(len(packed), len(source))
        self.assertEqual(decode(packed), pixels)

    def test_incompressible_stays_original(self):
        pixels = list(range(128))
        original = raw(pixels, 128, 1)
        self.assertEqual(compact_sprite(original), original)

    def test_pack_offsets_and_idempotence(self):
        sprites = {1: raw([0x801F]*20, 5, 4),
                   3: raw([0, 0x8000, 0, 0x7FFF], 2, 2)}
        count = 3
        offset = 8 + (count + 2) * 4
        offsets, body = [], bytearray()
        for i in range(count+1):
            offsets.append(offset+len(body))
            body += sprites.get(i, b"")
        offsets.append(offset+len(body))
        original = b"PKSP" + struct.pack("<HH", count, 0) + struct.pack(
            f"<{len(offsets)}I", *offsets) + body
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "icons.pak"
            path.write_bytes(original)
            before, after = compact_pack(path)
            self.assertLess(after, before)
            result = path.read_bytes()
            indexed = struct.unpack_from(f"<{count+2}I", result, 8)
            for i, old in sprites.items():
                self.assertEqual(decode(result[indexed[i]:indexed[i+1]]), decode(old))
            self.assertEqual(indexed[0], 8+(count+2)*4)
            self.assertEqual(indexed[-1], len(result))
            self.assertEqual(compact_pack(path), (len(result), len(result)))


if __name__ == "__main__":
    unittest.main()
