from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from tools.release.compare_import_bundles import compare


COMMIT = "0123456789abcdef0123456789abcdef01234567"


def png_bytes() -> bytes:
    signature = b"\x89PNG\r\n\x1a\n"
    raw = b"\x00\xff\x00\x00\xff"
    chunks = []
    for kind, payload in (
        (b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)),
        (b"IDAT", zlib.compress(raw)),
        (b"IEND", b""),
    ):
        chunks.append(
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )
    return signature + b"".join(chunks)


def glb_bytes(*, label: str = "idle", animated: bool = False, skinned: bool = False) -> bytes:
    document: dict[str, object] = {
        "asset": {"version": "2.0"},
        "scenes": [{"nodes": [0]}],
        "nodes": [{}],
    }
    if animated:
        document["animations"] = [{"name": label, "channels": [], "samplers": []}]
    if skinned:
        document["skins"] = [{"joints": [0]}]
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    return (
        struct.pack("<4sII", b"glTF", 2, 20 + len(encoded))
        + struct.pack("<II", len(encoded), 0x4E4F534A)
        + encoded
    )


class CompareImportBundlesTests(unittest.TestCase):
    def fixture(self, root: Path, *, animation_label: str = "idle") -> None:
        files = {
            "assets/textures/a.png": png_bytes(),
            "assets/models/unit.glb": glb_bytes(),
            "assets/animations/unit_idle.glb": glb_bytes(
                label=animation_label, animated=True
            ),
            "assets/skeletons/unit_rig.glb": glb_bytes(skinned=True),
            "assets/audio/voice.wav": b"audio",
            "maps/fords/map.json": b"map",
            "ui/portrait.png": png_bytes(),
            "rules/object.json": b"rules",
        }
        for relative, content in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)

    def test_identical_complete_trees_pass(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            first, second = root / "a", root / "b"
            self.fixture(first)
            self.fixture(second)
            receipt = compare(first, second, "bfme2", "men", COMMIT, {
                "textures": 1, "models": 1, "animations": 1, "skeletons": 1,
                "audio": 1, "maps": 1, "ui": 1, "rules": 1,
            })
            self.assertTrue(receipt["identical"])
            self.assertEqual(receipt["bundleDigestA"], receipt["bundleDigestB"])
            self.assertNotIn(str(root), str(receipt))

    def test_one_changed_animation_fails(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            first, second = root / "a", root / "b"
            self.fixture(first)
            self.fixture(second, animation_label="changed")
            with self.assertRaisesRegex(ValueError, "changed=1"):
                compare(first, second, "bfme2", "men", COMMIT, {})

    def test_missing_required_family_fails(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            first, second = root / "a", root / "b"
            self.fixture(first)
            self.fixture(second)
            with self.assertRaisesRegex(ValueError, "textures"):
                compare(first, second, "bfme2", "men", COMMIT, {"textures": 3})

    def test_malformed_texture_and_animation_fail(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            first, second = root / "a", root / "b"
            self.fixture(first)
            self.fixture(second)
            (first / "assets/textures/a.png").write_bytes(b"not a png")
            (second / "assets/textures/a.png").write_bytes(b"not a png")
            with self.assertRaisesRegex(ValueError, "invalid PNG"):
                compare(first, second, "bfme2", "men", COMMIT, {})

            self.fixture(first)
            self.fixture(second)
            (first / "assets/animations/unit_idle.glb").write_bytes(b"not a glb")
            (second / "assets/animations/unit_idle.glb").write_bytes(b"not a glb")
            with self.assertRaisesRegex(ValueError, "invalid GLB"):
                compare(first, second, "bfme2", "men", COMMIT, {})


if __name__ == "__main__":
    unittest.main()
