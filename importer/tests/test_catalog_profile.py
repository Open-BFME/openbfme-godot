from __future__ import annotations

import json
import struct
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.pipeline import _render_output_template
from openbfme_importer.profile import ImportProfile, resolve_profile
from openbfme_importer.util import write_json_atomic


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
    path.write_bytes(b"BIG4" + struct.pack("<I", offset) + struct.pack(">II", len(encoded), header_size) + records + bodies)


class CatalogProfileTests(unittest.TestCase):
    def test_men_fords_w3d_contract_requires_equipment_without_expanding_closure(self) -> None:
        profile = ImportProfile.load(
            Path(__file__).parents[1] / "profiles" / "men-fords-v0.json"
        )
        rule = next(
            item for item in profile.resources if item.id == "gondor-fighter-rig-and-core-clips"
        )

        self.assertEqual(rule.converter, "w3d-bundle")
        self.assertEqual(len(rule.patterns), 27)
        self.assertEqual(rule.limit, 27)
        self.assertEqual(
            set(rule.options["required_equipment"]),
            {"right-hand-weapon", "left-hand-shield"},
        )

    def test_patch_archive_wins_and_profile_is_bounded(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            make_big(root / "ini.big", {"data/thing.ini": b"base", "data/other.ini": b"other"})
            make_big(root / "_patch103.big", {"data/thing.ini": b"patch"})
            catalog = InstallCatalog.build(root)
            winner = catalog.resolve_exact("DATA/THING.INI")
            self.assertIsNotNone(winner)
            self.assertEqual(winner.archive, "_patch103.big")
            profile_path = root / "profile.json"
            write_json_atomic(
                profile_path,
                {
                    "format": 1,
                    "id": "test",
                    "pack": {"id": "test-pack"},
                    "resources": [
                        {
                            "id": "inis",
                            "kind": "data",
                            "patterns": ["data/*.ini"],
                            "limit": 1,
                        }
                    ],
                },
            )
            resolved = resolve_profile(ImportProfile.load(profile_path), catalog)
            self.assertEqual(len(resolved.resources[0].entries), 1)

    def test_language_patch_wins_base_and_unsafe_pack_id_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "lang").mkdir()
            make_big(root / "lang" / "English.big", {"data/lotr.str": b"base"})
            make_big(root / "lang" / "EnglishPatch105.big", {"data/lotr.str": b"patch"})
            catalog = InstallCatalog.build(root)
            winner = catalog.resolve_exact("data/lotr.str")
            self.assertIsNotNone(winner)
            self.assertEqual(winner.archive.casefold(), "lang/englishpatch105.big")

            profile_path = root / "unsafe.json"
            write_json_atomic(
                profile_path,
                {
                    "format": 1,
                    "id": "safe-profile",
                    "pack": {"id": "../../escape"},
                    "resources": [
                        {
                            "id": "data",
                            "kind": "data",
                            "converter": "hash-only",
                            "patterns": ["data/lotr.str"],
                        }
                    ],
                },
            )
            with self.assertRaises(ValueError):
                ImportProfile.load(profile_path)

    def test_required_rule_reports_each_missing_exact_pattern(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            make_big(root / "ini.big", {"data/present.ini": b"present"})
            profile_path = root / "partial.json"
            write_json_atomic(
                profile_path,
                {
                    "format": 1,
                    "id": "partial",
                    "pack": {"id": "partial-pack"},
                    "resources": [
                        {
                            "id": "closure",
                            "kind": "data",
                            "converter": "hash-only",
                            "patterns": ["data/present.ini", "data/missing.ini"],
                            "limit": 2,
                        }
                    ],
                },
            )
            resolved = resolve_profile(ImportProfile.load(profile_path), InstallCatalog.build(root))
            self.assertEqual(resolved.missing_required, ("closure",))
            self.assertEqual(resolved.resources[0].missing_patterns, ("data/missing.ini",))

    def test_catalog_detects_new_archive_and_same_metadata_directory_change(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base = root / "ini.big"
            make_big(base, {"data/one.ini": b"AAAA"})
            catalog = InstallCatalog.build(root)
            make_big(root / "_patch106.big", {"data/one.ini": b"patch"})
            self.assertTrue(any(reason.startswith("new archive:") for reason in catalog.stale_reasons()))

            (root / "_patch106.big").unlink()
            original_stat = base.stat()
            payload = bytearray(base.read_bytes())
            name_offset = payload.index(b"data/one.ini")
            payload[name_offset : name_offset + len(b"data/one.ini")] = b"data/two.ini"
            base.write_bytes(payload)
            base.touch()
            import os

            os.utime(base, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
            self.assertEqual(base.stat().st_size, original_stat.st_size)
            self.assertTrue(
                any(reason.startswith("changed archive directory:") for reason in catalog.stale_reasons())
            )

    def test_loaded_catalog_entries_are_bound_to_reopened_big_directory(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            make_big(root / "ini.big", {"data/good.ini": b"GOOD", "data/other.ini": b"EVIL"})
            catalog_path = root / "catalog.json"
            InstallCatalog.build(root).save(catalog_path)
            value = json.loads(catalog_path.read_text(encoding="utf-8"))
            good = next(item for item in value["entries"] if item["name"] == "data/good.ini")
            other = next(item for item in value["entries"] if item["name"] == "data/other.ini")
            good["offset"] = other["offset"]
            good["size"] = other["size"]
            write_json_atomic(catalog_path, value)

            tampered = InstallCatalog.load(catalog_path)
            self.assertTrue(
                any(reason.startswith("catalog directory mismatch:") for reason in tampered.stale_reasons())
            )
            entry = tampered.resolve_exact("data/good.ini")
            self.assertIsNotNone(entry)
            with self.assertRaises(ValueError):
                tampered.open_archive_for(entry)

    def test_loaded_catalog_rejects_forged_archive_precedence(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            make_big(root / "a.big", {"data/shared.ini": b"A"})
            make_big(root / "b.big", {"data/shared.ini": b"B"})
            catalog_path = root / "catalog.json"
            InstallCatalog.build(root).save(catalog_path)
            value = json.loads(catalog_path.read_text(encoding="utf-8"))
            for entry in value["entries"]:
                entry["precedence"] = 0 if entry["archive"] == "b.big" else 999
            write_json_atomic(catalog_path, value)

            with self.assertRaisesRegex(ValueError, "precedence"):
                InstallCatalog.load(catalog_path)

    def test_profile_rejects_duplicate_patterns_unsafe_outputs_and_nonfinite_json(self) -> None:
        base = {
            "format": 1,
            "id": "strict-profile",
            "pack": {"id": "strict-pack"},
            "resources": [
                {
                    "id": "strict-resource",
                    "kind": "data",
                    "patterns": ["data/file.ini"],
                    "converter": "copy",
                    "output": "data/file.ini",
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            cases = []
            duplicate = json.loads(json.dumps(base))
            duplicate["resources"][0]["patterns"] = ["data/file.ini", "DATA/FILE.INI"]
            cases.append(("duplicate", duplicate))
            unsafe = json.loads(json.dumps(base))
            unsafe["resources"][0]["output"] = "../escape.bin"
            cases.append(("unsafe", unsafe))
            nonfinite = json.loads(json.dumps(base))
            nonfinite["pack"]["priority"] = float("nan")
            cases.append(("nonfinite", nonfinite))
            for name, value in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    write_json_atomic(path, value)
                    with self.assertRaises(ValueError):
                        ImportProfile.load(path)
            self.assertEqual(
                _render_output_template(
                    "assets/{name}", index=2, stem="voice", name="voice.wav"
                ),
                "assets/voice.wav",
            )
            with self.assertRaisesRegex(ValueError, "format expression"):
                _render_output_template(
                    "assets/{index:100000000}.wav",
                    index=2,
                    stem="voice",
                    name="voice.wav",
                )


if __name__ == "__main__":
    unittest.main()
