from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.big import BigArchive, BigEntry, BigFormatError, ExtractionLimitError


def make_big(path: Path, files: dict[str, bytes]) -> None:
    encoded = [(name.encode("latin-1") + b"\0", payload) for name, payload in files.items()]
    header_size = 16 + sum(8 + len(name) for name, _ in encoded)
    offset = header_size
    records = bytearray()
    bodies = bytearray()
    for name, payload in encoded:
        records.extend(struct.pack(">II", offset, len(payload)))
        records.extend(name)
        bodies.extend(payload)
        offset += len(payload)
    archive = b"BIGF" + struct.pack("<I", offset) + struct.pack(">II", len(encoded), header_size) + records + bodies
    path.write_bytes(archive)


class BigArchiveTests(unittest.TestCase):
    def test_stream_parse_and_exact_extract(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            archive_path = root / "sample.big"
            make_big(archive_path, {"data\\one.txt": b"one", "DATA/two.bin": b"two"})
            archive = BigArchive.open(archive_path)
            self.assertEqual([entry.name for entry in archive.entries], ["data/one.txt", "DATA/two.bin"])
            selected = archive.find_exact(["DATA/ONE.TXT"])
            result = archive.extract(selected, root / "out")
            self.assertEqual((root / "out" / "data" / "one.txt").read_bytes(), b"one")
            self.assertEqual(len(result[0].sha256), 64)

    def test_bounded_in_memory_read_revalidates_entry(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            path = root / "sample.big"
            make_big(path, {"data/good.bin": b"GOOD", "data/other.bin": b"EVIL"})
            archive = BigArchive.open(path)
            good, other = archive.entries
            self.assertEqual(archive.read_entry(good, max_bytes=4), b"GOOD")
            with self.assertRaises(ExtractionLimitError):
                archive.read_entry(good, max_bytes=3)
            forged = BigEntry(good.name, other.offset, other.size)
            with self.assertRaises(BigFormatError):
                archive.read_entry(forged, max_bytes=4)

    def test_rejects_parent_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "unsafe.big"
            make_big(path, {"../escape.txt": b"bad"})
            with self.assertRaises(BigFormatError):
                BigArchive.open(path)

    def test_rejects_windows_device_and_ads_paths(self) -> None:
        for unsafe in [
            "CON",
            "data/aux.txt",
            "data/file.txt:stream",
            "data/trailing. ",
            "data/COM¹",
            "data/LPT².txt",
            "data/CONIN$",
            "data/CONOUT$",
            "data/COM1 .txt",
        ]:
            with self.subTest(unsafe=unsafe), tempfile.TemporaryDirectory() as raw:
                path = Path(raw) / "unsafe.big"
                make_big(path, {unsafe: b"bad"})
                with self.assertRaises(BigFormatError):
                    BigArchive.open(path)

    def test_extraction_limits_apply_before_writes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            path = root / "sample.big"
            make_big(path, {"data/large.bin": b"123456"})
            archive = BigArchive.open(path)
            with self.assertRaises(ExtractionLimitError):
                archive.extract(archive.entries, root / "out", max_bytes=5)
            self.assertFalse((root / "out").exists())

    def test_cached_bytes_are_verified_against_current_archive(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            path = root / "sample.big"
            make_big(path, {"data/file.bin": b"AAAA"})
            archive = BigArchive.open(path)
            archive.extract(archive.entries, root / "out")
            changed = bytearray(path.read_bytes())
            changed[-1] = ord("B")
            path.write_bytes(changed)
            changed_archive = BigArchive.open(path)
            previous = os.environ.get("OPENBFME_EXTRACT_VERIFY")
            os.environ["OPENBFME_EXTRACT_VERIFY"] = "full"
            try:
                with self.assertRaises(FileExistsError):
                    changed_archive.extract(changed_archive.entries, root / "out")
            finally:
                if previous is None:
                    os.environ.pop("OPENBFME_EXTRACT_VERIFY", None)
                else:
                    os.environ["OPENBFME_EXTRACT_VERIFY"] = previous

    def test_rejects_case_colliding_extraction_targets(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            path = root / "sample.big"
            make_big(path, {"Data/file.bin": b"one", "data/FILE.bin": b"two"})
            archive = BigArchive.open(path)
            with self.assertRaises(BigFormatError):
                archive.extract(archive.entries, root / "out")

    def test_rejects_forged_entry_metadata_not_in_directory(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            path = root / "sample.big"
            make_big(path, {"data/good.bin": b"GOOD", "data/other.bin": b"EVIL"})
            archive = BigArchive.open(path)
            other = archive.entries[1]
            forged = BigEntry("data/good.bin", other.offset, other.size)
            with self.assertRaises(BigFormatError):
                archive.extract([forged], root / "out")


if __name__ == "__main__":
    unittest.main()
