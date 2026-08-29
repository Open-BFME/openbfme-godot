from __future__ import annotations

import json
import hashlib
import os
import struct
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.catalog import (
    ArchivePolicy,
    DEFAULT_BFME2_ARCHIVE_POLICY,
    DEFAULT_ROTWK_201_ARCHIVE_POLICY,
    DEFAULT_ROTWK_202_OVERLAY_POLICY,
    InstallCatalog,
    default_rotwk_202_archive_policy,
)
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
    def test_tracked_bfme2_106_policy_has_receipt_bound_107_archive_set(self) -> None:
        policy = ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY)
        self.assertEqual(policy.game, "bfme2")
        self.assertEqual(policy.patch, "1.06")
        self.assertEqual(policy.package_guid, "original-BFME2")
        self.assertEqual(policy.language, "EN")
        self.assertEqual(len(policy.archives), 107)
        self.assertEqual(
            policy.policy_sha256,
            "98707c52862f378ec22a02ad0572cb131faa56fbfd34ff5f1f0845fd33931d47",
        )

    def test_tracked_rotwk_202_policy_composes_exact_versioned_layers(self) -> None:
        base = ArchivePolicy.load(DEFAULT_ROTWK_201_ARCHIVE_POLICY)
        overlay = ArchivePolicy.load(DEFAULT_ROTWK_202_OVERLAY_POLICY)
        policy = default_rotwk_202_archive_policy()
        self.assertEqual(base.patch, "2.01")
        self.assertEqual(len(base.archives), 106)
        self.assertEqual(overlay.patch, "2.02 v9.7.7")
        self.assertEqual(overlay.package_guid, "official-2")
        self.assertEqual(len(overlay.archives), 4)
        self.assertEqual(policy.patch, "2.02 v9.7.7")
        self.assertEqual(len(policy.archives), 217)
        prefixes = {row.path.split("/", 1)[0] for row in policy.archives}
        self.assertEqual(
            prefixes,
            {"layer-0-patch202", "layer-1-rotwk", "layer-2-bfme2"},
        )

    def test_policy_catalog_ignores_extras_and_rejects_member_payload_drift(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base = root / "INI.big"
            make_big(base, {"data/ranger.ini": b"retail"})
            make_big(root / "###mod.big", {"data/ranger.ini": b"modded"})
            member_path = "ini.big"
            member_md5 = hashlib.md5(base.read_bytes()).hexdigest()
            canonical_rows = f"{member_path}|{member_md5}|{base.stat().st_size}|ALL\n"
            policy_path = root / "policy.json"
            write_json_atomic(
                policy_path,
                {
                    "schema": "openbfme.retail-archive-policy",
                    "schemaVersion": 1,
                    "game": "bfme2",
                    "patch": "1.06",
                    "package": {
                        "guid": "original-BFME2",
                        "name": "Vanilla (1.06)",
                        "version": "1.0.0",
                        "receiptSha256": "1" * 64,
                    },
                    "language": "EN",
                    "policySha256": hashlib.sha256(
                        canonical_rows.encode("utf-8")
                    ).hexdigest(),
                    "archives": [
                        {
                            "path": member_path,
                            "md5": member_md5,
                            "size": base.stat().st_size,
                            "language": "ALL",
                        }
                    ],
                },
            )
            policy = ArchivePolicy.load(policy_path)
            catalog = InstallCatalog.build(root, source_policy=policy)
            winner = catalog.resolve_exact("data/ranger.ini")
            self.assertIsNotNone(winner)
            self.assertEqual(winner.archive, "INI.big")
            self.assertEqual(len(catalog.archives), 1)
            self.assertEqual(catalog.stale_reasons(), [])

            saved = root / "catalog.json"
            catalog.save(saved)
            loaded = InstallCatalog.load(saved)
            self.assertEqual(loaded.identity_sha256(), catalog.identity_sha256())
            self.assertEqual(loaded.source_policy_sha256, policy.policy_sha256)

            original_stat = base.stat()
            payload = bytearray(base.read_bytes())
            payload[-1] ^= 1
            base.write_bytes(payload)
            os.utime(base, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
            # Payload sample canary catches bit-flips without full MD5.
            reasons = catalog.stale_reasons()
            self.assertTrue(
                any(
                    reason.startswith("changed archive payload sample: INI.big")
                    or reason.startswith("changed archive payload: INI.big")
                    for reason in reasons
                ),
                reasons,
            )
            # Deep path still reports full policy MD5 mismatch.
            previous = os.environ.get("OPENBFME_CATALOG_DEEP")
            os.environ["OPENBFME_CATALOG_DEEP"] = "1"
            try:
                deep_reasons = catalog.stale_reasons()
                self.assertTrue(
                    any(
                        "changed archive payload" in reason for reason in deep_reasons
                    ),
                    deep_reasons,
                )
            finally:
                if previous is None:
                    os.environ.pop("OPENBFME_CATALOG_DEEP", None)
                else:
                    os.environ["OPENBFME_CATALOG_DEEP"] = previous
            with self.assertRaisesRegex(ValueError, "digest changed"):
                InstallCatalog.build(root, source_policy=policy)

    def test_policy_catalog_identity_is_portable_across_install_roots(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            installs = [root / "windows-copy", root / "linux-copy"]
            for install in installs:
                install.mkdir()
                make_big(install / "INI.big", {"data/source.ini": b"retail"})
            archive = installs[0] / "INI.big"
            md5 = hashlib.md5(archive.read_bytes()).hexdigest()
            row = f"ini.big|{md5}|{archive.stat().st_size}|ALL\n"
            policy_path = root / "policy.json"
            write_json_atomic(
                policy_path,
                {
                    "schema": "openbfme.retail-archive-policy",
                    "schemaVersion": 1,
                    "game": "bfme2",
                    "patch": "1.06",
                    "package": {
                        "guid": "original-BFME2",
                        "name": "Vanilla (1.06)",
                        "version": "1.0.0",
                        "receiptSha256": "2" * 64,
                    },
                    "language": "EN",
                    "policySha256": hashlib.sha256(row.encode("utf-8")).hexdigest(),
                    "archives": [
                        {
                            "path": "ini.big",
                            "md5": md5,
                            "size": archive.stat().st_size,
                            "language": "ALL",
                        }
                    ],
                },
            )
            policy = ArchivePolicy.load(policy_path)
            identities = [
                InstallCatalog.build(install, source_policy=policy).identity_sha256()
                for install in installs
            ]
            self.assertEqual(identities[0], identities[1])

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
            os.utime(base, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
            self.assertEqual(base.stat().st_size, original_stat.st_size)
            reasons = catalog.stale_reasons()
            # Sample canary or deep directory re-parse both detect directory edits.
            self.assertTrue(
                any(
                    reason.startswith("changed archive payload sample:")
                    or reason.startswith("changed archive directory:")
                    for reason in reasons
                ),
                reasons,
            )
            previous = os.environ.get("OPENBFME_CATALOG_DEEP")
            os.environ["OPENBFME_CATALOG_DEEP"] = "1"
            try:
                deep_reasons = catalog.stale_reasons()
                self.assertTrue(
                    any(
                        reason.startswith("changed archive payload sample:")
                        or reason.startswith("changed archive directory:")
                        for reason in deep_reasons
                    ),
                    deep_reasons,
                )
            finally:
                if previous is None:
                    os.environ.pop("OPENBFME_CATALOG_DEEP", None)
                else:
                    os.environ["OPENBFME_CATALOG_DEEP"] = previous

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
