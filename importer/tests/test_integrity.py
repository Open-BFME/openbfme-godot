from __future__ import annotations

import json
import hashlib
import struct
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.catalog import InstallCatalog, KNOWN_SLICE_ARCHIVE_SHA256
from openbfme_importer.pipeline import (
    ImportPipeline,
    MEN_FORDS_SOURCE_ENTRY_COUNT,
    RETAIL_PROVENANCE_CONTRACT,
    _isolated_blender_environment,
    _audit_retail_provenance,
    _canonical_pack_inventory,
    audit_pack,
)
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
    path.write_bytes(b"BIGF" + struct.pack("<I", offset) + struct.pack(">II", len(encoded), header_size) + records + bodies)


def make_audited_pack(root: Path) -> None:
    write_json_atomic(
        root / "pack.json",
        {
            "id": "test-generic-pack",
            "version": "1",
            "local_retail_import": False,
            "redistributable": False,
            "profile_build_complete": True,
        },
    )
    write_json_atomic(root / "data" / "objects.json", {"objects": []})
    manifest = {
        "format": 1,
        "redistributable": False,
        "entries": [],
        "bundle_files": _canonical_pack_inventory(root),
    }
    write_json_atomic(root / "provenance" / "manifest.json", manifest)


class IntegrityTests(unittest.TestCase):
    def test_blender_environment_drops_host_python_and_blender_overrides(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            job_root = Path(raw) / "job"
            environment = _isolated_blender_environment(
                {
                    "PATH": "safe-path",
                    "PYTHONPATH": "attacker-python-path",
                    "pythonhome": "attacker-python-home",
                    "BLENDER_SYSTEM_SCRIPTS": "attacker-blender-scripts",
                    "blender_user_config": "attacker-blender-config",
                },
                job_root,
            )

            self.assertEqual(environment["PATH"], "safe-path")
            inherited_names = {
                name.upper()
                for name in environment
                if name.upper().startswith("PYTHON") or name.upper().startswith("BLENDER_")
            }
            self.assertEqual(
                inherited_names,
                {
                    "PYTHONDONTWRITEBYTECODE",
                    "PYTHONNOUSERSITE",
                    "BLENDER_USER_CONFIG",
                    "BLENDER_USER_SCRIPTS",
                    "BLENDER_USER_DATAFILES",
                },
            )
            self.assertEqual(environment["PYTHONDONTWRITEBYTECODE"], "1")
            self.assertEqual(environment["PYTHONNOUSERSITE"], "1")
            for name in ["config", "scripts", "datafiles", "temp"]:
                self.assertTrue((job_root / "blender-user" / name).is_dir())

    def test_generated_python_bytecode_is_rejected_from_pinned_tools(self) -> None:
        from openbfme_importer.bootstrap import _reject_python_bytecode

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "source.py").write_text("VALUE = 1\n", encoding="utf-8")
            _reject_python_bytecode(root, "test tool")

            for relative in [
                Path("__pycache__") / "source.cpython-311.pyc",
                Path("cache") / "source.pyo",
                Path("nested") / "__pycache__" / "unexpected.txt",
            ]:
                with self.subTest(relative=relative.as_posix()):
                    target = root / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(b"generated")
                    with self.assertRaisesRegex(RuntimeError, "generated Python bytecode"):
                        _reject_python_bytecode(root, "test tool")
                    target.unlink()

    def test_fords_semantic_contract_binds_sources_recipe_and_tools(self) -> None:
        from openbfme_importer.bootstrap import (
            BLENDER_EXE_SHA256,
            BLENDER_TREE_SHA256,
            FFMPEG_EXE_SHA256,
            FFPROBE_EXE_SHA256,
            PILLOW_TREE_SHA256,
            PLUGIN_COMMIT,
            PLUGIN_SUBMODULE_COMMIT,
            PYTHON_BASE_DLL_SHA256,
            PYTHON_LAUNCHER_SHA256,
            PYTHON_RUNTIME_TREE_SHA256,
            PYTHON_VERSION,
        )

        recipe_file = {"path": "importer/example.py", "size": 1, "sha256": "a" * 64}
        recipe_digest = hashlib.sha256(
            ("importer/example.py\0" + "1\0" + "a" * 64 + "\n").encode("utf-8")
        ).hexdigest()
        archives = [
            {
                "relative_path": relative,
                "size": 16,
                "sha256": expected,
                "expected_sha256": expected,
                "matches_reference": True,
            }
            for relative, expected in KNOWN_SLICE_ARCHIVE_SHA256.items()
        ]
        archive_names = list(KNOWN_SLICE_ARCHIVE_SHA256)
        entries = []
        for index in range(MEN_FORDS_SOURCE_ENTRY_COUNT):
            entries.append(
                {
                    "resource_id": "source-%d" % index,
                    "kind": "data",
                    "converter": "hash-only",
                    "source": {
                        "archive": archive_names[index % len(archive_names)],
                        "virtual_path": "data/file-%d.bin" % index,
                        "offset": index,
                        "size": 1,
                        "sha256": "b" * 64,
                        "cache_key": "c" * 20,
                    },
                    "outputs": [],
                }
            )
        # Retail inputs may intentionally fan out into multiple resource
        # conversions. Exercise both distinguishing identity components while
        # keeping the fixed Fords source-entry count unchanged.
        entries[1]["source"] = json.loads(json.dumps(entries[0]["source"]))
        entries[2]["source"] = json.loads(json.dumps(entries[0]["source"]))
        entries[2]["resource_id"] = entries[0]["resource_id"]
        entries[2]["converter"] = "text"
        manifest = {
            "format": 1,
            "contract": RETAIL_PROVENANCE_CONTRACT,
            "importer_version": "test",
            "profile": "men-fords-v0",
            "profile_sha256": "d" * 64,
            "importer_recipe": {
                "tree_sha256": recipe_digest,
                "files": [recipe_file],
                "git_commit": "e" * 40,
                "git_worktree_clean": False,
            },
            "source_game": "bfme2-retail-user-owned",
            "source_archives": archives,
            "redistributable": False,
            "tools": {
                "blender": {"sha256": BLENDER_EXE_SHA256, "tree_sha256": BLENDER_TREE_SHA256},
                "ffmpeg": {"sha256": FFMPEG_EXE_SHA256, "ffprobe_sha256": FFPROBE_EXE_SHA256},
                "opensage_w3d_plugin": {
                    "commit": PLUGIN_COMMIT,
                    "submodule_commit": PLUGIN_SUBMODULE_COMMIT,
                    "worktree_clean": True,
                    "python_bytecode_free": True,
                },
                "pillow": {"version": "12.2.0", "tree_sha256": PILLOW_TREE_SHA256},
                "python": {
                    "version": PYTHON_VERSION,
                    "launcher_sha256": PYTHON_LAUNCHER_SHA256,
                    "base_dll_sha256": PYTHON_BASE_DLL_SHA256,
                    "tree_sha256": PYTHON_RUNTIME_TREE_SHA256,
                    "file_count": 1,
                    "total_bytes": 1,
                    "excludes": [],
                },
            },
            "incomplete": [],
            "entries": entries,
        }
        pack = {
            "local_retail_import": True,
            "redistributable": False,
            "profile_build_complete": True,
            "provenance_contract": RETAIL_PROVENANCE_CONTRACT,
        }
        errors: list[str] = []
        summary = _audit_retail_provenance(manifest, pack, errors)
        self.assertEqual(errors, [])
        self.assertTrue(summary["semantic_provenance"])
        self.assertEqual(
            summary["source_archive_count"], len(KNOWN_SLICE_ARCHIVE_SHA256)
        )
        self.assertEqual(
            summary["provenance_entry_count"], MEN_FORDS_SOURCE_ENTRY_COUNT
        )

        duplicate_conversion = json.loads(json.dumps(manifest))
        duplicate_conversion["entries"][2]["converter"] = entries[0]["converter"]
        errors = []
        self.assertFalse(
            _audit_retail_provenance(duplicate_conversion, pack, errors)[
                "semantic_provenance"
            ]
        )
        self.assertTrue(
            any("duplicate retail provenance conversion entry" in error for error in errors)
        )

        malformed_source = json.loads(json.dumps(manifest))
        malformed_source["entries"][3]["source"]["virtual_path"] = "../tampered.bin"
        errors = []
        self.assertFalse(
            _audit_retail_provenance(malformed_source, pack, errors)[
                "semantic_provenance"
            ]
        )
        self.assertTrue(
            any("invalid retail provenance source path" in error for error in errors)
        )

        for identity_field in ("resource_id", "converter"):
            with self.subTest(identity_field=identity_field):
                malformed_identity = json.loads(json.dumps(manifest))
                malformed_identity["entries"][3][identity_field] = ""
                errors = []
                self.assertFalse(
                    _audit_retail_provenance(malformed_identity, pack, errors)[
                        "semantic_provenance"
                    ]
                )
                self.assertIn("invalid retail provenance source entry", errors)

        tampered = json.loads(json.dumps(manifest))
        tampered["tools"]["python"]["tree_sha256"] = "0" * 64
        tampered["tools"]["opensage_w3d_plugin"]["python_bytecode_free"] = False
        tampered["source_archives"][0]["matches_reference"] = False
        errors = []
        self.assertFalse(_audit_retail_provenance(tampered, pack, errors)["semantic_provenance"])
        self.assertTrue(any("Python" in error for error in errors))
        self.assertTrue(any("OpenSAGE" in error for error in errors))
        self.assertTrue(any("source archive" in error for error in errors))

    def test_local_retail_pack_cannot_downgrade_semantic_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            make_audited_pack(root)
            pack = json.loads((root / "pack.json").read_text(encoding="utf-8"))
            pack["local_retail_import"] = True
            write_json_atomic(root / "pack.json", pack)
            manifest = json.loads(
                (root / "provenance" / "manifest.json").read_text(encoding="utf-8")
            )
            manifest["bundle_files"] = _canonical_pack_inventory(root)
            write_json_atomic(root / "provenance" / "manifest.json", manifest)

            result = audit_pack(root)
            self.assertFalse(result["valid"])
            self.assertFalse(result["semantic_provenance"])
            self.assertTrue(any("contract" in error for error in result["errors"]))

    def test_audit_covers_runtime_json_and_rejects_extra_files(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            make_audited_pack(root)
            self.assertTrue(audit_pack(root)["valid"])
            write_json_atomic(root / "data" / "objects.json", {"objects": [{"tampered": True}]})
            self.assertFalse(audit_pack(root)["valid"])
            make_audited_pack(root)
            (root / "unexpected.bin").write_bytes(b"extra")
            self.assertFalse(audit_pack(root)["valid"])

    def test_audit_binds_each_conversion_hash_and_rejects_malformed_entries(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            make_audited_pack(root)
            manifest_path = root / "provenance" / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            inventory = next(item for item in manifest["bundle_files"] if item["path"] == "data/objects.json")
            manifest["entries"] = [
                {
                    "resource_id": "objects",
                    "outputs": [
                        {
                            "path": inventory["path"],
                            "size": inventory["size"],
                            "sha256": "0" * 64,
                        }
                    ],
                }
            ]
            write_json_atomic(manifest_path, manifest)
            result = audit_pack(root)
            self.assertFalse(result["valid"])
            self.assertTrue(any("hash disagrees" in error for error in result["errors"]))

            manifest["entries"] = [None, {"outputs": [{"not_path": True}]}]
            write_json_atomic(manifest_path, manifest)
            malformed = audit_pack(root)
            self.assertFalse(malformed["valid"])
            self.assertTrue(any("invalid provenance" in error or "invalid converted" in error for error in malformed["errors"]))

    def test_publish_revalidates_preexisting_digest_directory(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            make_audited_pack(source)
            pipeline = ImportPipeline(None, root / "state")  # publish does not need a catalog
            published = pipeline.publish_to_godot(source, root / "content")
            destination = Path(published["published_pack"])
            (destination / "attacker.bin").write_bytes(b"tampered")
            with self.assertRaises(RuntimeError):
                pipeline.publish_to_godot(source, root / "content")

    def test_allow_incomplete_never_marks_or_publishes_complete(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            make_big(install / "ini.big", {"data/present.ini": b"present"})
            profile_path = root / "profile.json"
            write_json_atomic(
                profile_path,
                {
                    "format": 1,
                    "id": "incomplete",
                    "pack": {
                        "id": "incomplete-pack",
                        "local_retail_import": False,
                        "redistributable": True,
                    },
                    "resources": [
                        {
                            "id": "required",
                            "kind": "data",
                            "converter": "hash-only",
                            "patterns": ["data/missing.ini"],
                        }
                    ],
                },
            )
            catalog = InstallCatalog.build(install)
            resolved = resolve_profile(ImportProfile.load(profile_path), catalog)
            pipeline = ImportPipeline(catalog, root / "state")
            pack = pipeline.build(resolved, allow_incomplete=True)
            pack_data = json.loads((pack / "pack.json").read_text(encoding="utf-8"))
            self.assertFalse(pack_data["profile_build_complete"])
            self.assertTrue(pack_data["local_retail_import"])
            self.assertFalse(pack_data["redistributable"])
            built_audit = audit_pack(pack)
            self.assertTrue(built_audit["valid"], built_audit["errors"])
            self.assertTrue(built_audit["semantic_provenance"])
            with self.assertRaises(RuntimeError):
                pipeline.publish_to_godot(pack, root / "content")

            manifest_path = pack / "provenance" / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest.pop("importer_recipe")
            write_json_atomic(manifest_path, manifest)
            missing_recipe = audit_pack(pack)
            self.assertFalse(missing_recipe["valid"])
            self.assertTrue(any("recipe" in error for error in missing_recipe["errors"]))


if __name__ == "__main__":
    unittest.main()
