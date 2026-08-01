"""Specification tests for the describe-pack provenance report.

All packs here are synthetic: they are written from the pinned attestation
constants and never touch a retail install. The report must stay useful for
broken packs — a missing or corrupt manifest yields a report plus plain
errors, never a traceback.
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import tempfile
from pathlib import Path
import unittest

from openbfme_importer import cli
from openbfme_importer.catalog import KNOWN_SLICE_ARCHIVE_SHA256
from openbfme_importer.pack_report import (
    REPORT_SCHEMA,
    REPORT_SCHEMA_VERSION,
    describe_pack,
    render_pack_report,
)
from openbfme_importer.pipeline import (
    MEN_FORDS_SOURCE_ENTRY_COUNT,
    RETAIL_PROVENANCE_CONTRACT,
    _canonical_pack_inventory,
)
from openbfme_importer.big import sha256_file
from openbfme_importer.util import write_json_atomic


GIT_COMMIT = "e" * 40


def _recipe_block(
    *,
    provenance_source: str | None = "git-exact-root",
    include_release_identity: bool = False,
    pre_provenance: bool = False,
) -> dict:
    files = [
        {"path": "importer/example.py", "size": 1, "sha256": "a" * 64},
        {"path": "importer/requirements-win.txt", "size": 2, "sha256": "f" * 64},
    ]
    if include_release_identity:
        files.append(
            {"path": "release-identity.json", "size": 3, "sha256": "9" * 64}
        )
    digest = hashlib.sha256(
        b"".join(
            b"%s\0%d\0%s\n"
            % (
                item["path"].encode("utf-8"),
                item["size"],
                item["sha256"].encode("ascii"),
            )
            for item in files
        )
    ).hexdigest()
    block = {
        "tree_sha256": digest,
        "files": files,
        "git_commit": GIT_COMMIT,
        "git_worktree_clean": True,
    }
    if not pre_provenance:
        block["provenance_source"] = provenance_source
        block["requirements_files"] = ["importer/requirements-win.txt"]
    return block


def _pinned_tools() -> dict:
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

    return {
        "blender": {
            "version": "4.2.0",
            "sha256": BLENDER_EXE_SHA256,
            "tree_sha256": BLENDER_TREE_SHA256,
        },
        "ffmpeg": {
            "version": "8.1.1",
            "sha256": FFMPEG_EXE_SHA256,
            "ffprobe_sha256": FFPROBE_EXE_SHA256,
        },
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
    }


def _write_retail_pack(
    root: Path,
    *,
    recipe: dict | None = None,
    tools: dict | None = None,
) -> None:
    """Write a synthetic on-disk Fords retail pack that passes the full audit."""

    write_json_atomic(
        root / "pack.json",
        {
            "id": "test-fords-pack",
            "version": "1",
            "title": "Synthetic Fords provenance fixture",
            "local_retail_import": True,
            "redistributable": False,
            "profile_build_complete": True,
            "provenance_contract": RETAIL_PROVENANCE_CONTRACT,
        },
    )
    write_json_atomic(root / "data" / "objects.json", {"objects": []})
    output_path = root / "data" / "objects.json"

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
    kinds = ("data", "model", "texture", "audio")
    converters = ("hash-only", "w3d-model", "texture", "audio")
    entries = []
    for index in range(MEN_FORDS_SOURCE_ENTRY_COUNT):
        entries.append(
            {
                "resource_id": "source-%d" % index,
                "kind": kinds[index % len(kinds)],
                "converter": converters[index % len(converters)],
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
    entries[0]["outputs"] = [
        {
            "path": "data/objects.json",
            "size": output_path.stat().st_size,
            "sha256": sha256_file(output_path),
        }
    ]

    manifest = {
        "format": 1,
        "contract": RETAIL_PROVENANCE_CONTRACT,
        "importer_version": "test-importer-version",
        "profile": "men-fords-v0",
        "profile_sha256": "d" * 64,
        "importer_recipe": recipe if recipe is not None else _recipe_block(),
        "source_game": "bfme2-retail-user-owned",
        "source_archives": archives,
        "redistributable": False,
        "tools": tools if tools is not None else _pinned_tools(),
        "incomplete": [],
        "entries": entries,
        "bundle_files": _canonical_pack_inventory(root),
    }
    write_json_atomic(root / "provenance" / "manifest.json", manifest)


class DescribePackTests(unittest.TestCase):
    def test_valid_pack_reports_every_section(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(root)
            report = describe_pack(root)

            self.assertTrue(
                report["health"]["valid"], report["health"]["audit"]["errors"]
            )
            self.assertEqual(report["report_errors"], [])

            identity = report["identity"]
            self.assertEqual(identity["pack_id"], "test-fords-pack")
            self.assertEqual(identity["profile"], "men-fords-v0")
            self.assertEqual(identity["importer_version"], "test-importer-version")
            self.assertIn(
                "Battle for Middle-earth II", identity["source_game_description"]
            )
            self.assertIs(identity["redistributable"], False)
            self.assertIsNone(identity["built_at"])
            self.assertEqual(identity["declared_file_count"], 2)
            self.assertTrue(identity["declared_content_digest_sha256"])

            origin = report["origin"]
            self.assertEqual(origin["git_commit"], GIT_COMMIT)
            self.assertIs(origin["git_worktree_clean"], True)
            self.assertEqual(origin["provenance_source"], "git-exact-root")
            self.assertFalse(origin["pre_provenance_recipe"])
            self.assertIn(
                "development checkout", origin["provenance_source_description"]
            )
            # The release/checkout distinction is the whole point of the
            # provenance_source field: a git-exact-root pack must say in
            # words that it was NOT a stamped release.
            self.assertIn(
                "not a stamped release",
                origin["provenance_source_description"],
            )
            self.assertIn(
                "exact", origin["provenance_source_description"].casefold()
            )
            self.assertEqual(
                origin["requirements_files"], ["importer/requirements-win.txt"]
            )

            self.assertEqual(len(report["tools"]), 5)
            for tool in report["tools"]:
                self.assertEqual(
                    tool["verification"]["status"], "verified", tool["name"]
                )

            self.assertEqual(
                report["source_archives"]["count"],
                len(KNOWN_SLICE_ARCHIVE_SHA256),
            )
            self.assertEqual(
                report["source_archives"]["total_bytes"],
                16 * len(KNOWN_SLICE_ARCHIVE_SHA256),
            )

            contents = report["contents"]
            self.assertEqual(
                contents["conversion_entry_count"], MEN_FORDS_SOURCE_ENTRY_COUNT
            )
            self.assertEqual(contents["declared_output_file_count"], 1)
            kinds = {bucket["kind"] for bucket in contents["by_kind"]}
            self.assertEqual(kinds, {"data", "model", "texture", "audio"})
            self.assertIn("pack.json", contents["importer_written_files"])
            self.assertEqual(contents["incomplete"], [])

            joined = " ".join(report["not_known"])
            self.assertIn("timestamp", joined)
            self.assertIn("release", joined)

    def test_valid_pack_text_is_sectioned_and_hides_bulk_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(root)
            report = describe_pack(root)
            text = render_pack_report(report)

            for heading in (
                "PACK PROVENANCE REPORT",
                "WHAT THIS PACK IS",
                "WHERE IT CAME FROM",
                "WHAT PRODUCED IT (TOOLS)",
                "WHAT WENT INTO IT (RETAIL SOURCES)",
                "WHAT IS IN IT (CONVERTED CONTENT)",
                "HEALTH (AUDIT VERDICT)",
                "WHAT THIS REPORT CANNOT TELL YOU",
            ):
                self.assertIn(heading, text)
            self.assertIn("VALID", text)
            self.assertIn("verified against the pinned tool", text)
            # Short hash prefixes only; the full 40/64-hex values live in
            # --json, not in the human report.
            self.assertIn(GIT_COMMIT[:12], text)
            self.assertNotIn(GIT_COMMIT, text)
            recipe_tree = report["origin"]["recipe_tree_sha256"]
            self.assertIn(recipe_tree[:12], text)
            self.assertNotIn(recipe_tree, text)
            self.assertIn("--json", text)

    def test_release_identity_source_is_explained_as_stamped(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(
                root,
                recipe=_recipe_block(
                    provenance_source="release-identity",
                    include_release_identity=True,
                ),
            )
            report = describe_pack(root)
            self.assertTrue(
                report["health"]["valid"], report["health"]["audit"]["errors"]
            )
            description = report["origin"]["provenance_source_description"]
            self.assertIn("stamped", description)
            self.assertIn("release", description)
            self.assertNotIn("development checkout", description)
            # A stamped release does not raise the unpublished-commit gap.
            joined = " ".join(report["not_known"])
            self.assertNotIn("development checkout", joined)
            text = render_pack_report(report)
            self.assertIn("stamped", text)

    def test_pre_provenance_pack_names_reimport_as_the_fix(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(root, recipe=_recipe_block(pre_provenance=True))
            report = describe_pack(root)

            self.assertFalse(report["health"]["valid"])
            origin = report["origin"]
            self.assertTrue(origin["pre_provenance_recipe"])
            self.assertIsNone(origin["provenance_source"])
            self.assertIn(
                "Not declared", origin["provenance_source_description"]
            )
            explanations = [
                item["explanation"]
                for item in report["health"]["errors_explained"]
                if item["explanation"]
            ]
            self.assertTrue(
                any("re-import" in explanation for explanation in explanations),
                explanations,
            )
            self.assertTrue(
                any("before packs recorded" in text for text in explanations)
            )

            text = render_pack_report(report)
            self.assertIn("re-import", text)
            self.assertIn("built before packs recorded", text)
            self.assertIn("INVALID", text)
            joined = " ".join(report["not_known"])
            self.assertIn("Pre-provenance", joined)

    def test_corrupt_manifest_yields_report_not_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            (root / "provenance").mkdir(parents=True)
            write_json_atomic(
                root / "pack.json",
                {"id": "broken", "version": "1", "redistributable": False},
            )
            (root / "provenance" / "manifest.json").write_text(
                "{ this is not json", encoding="utf-8"
            )
            report = describe_pack(root)
            self.assertFalse(report["health"]["valid"])
            self.assertTrue(
                any(
                    "could not be read as JSON" in error
                    for error in report["report_errors"]
                ),
                report["report_errors"],
            )
            explanations = [
                item["explanation"] or item["error"]
                for item in report["health"]["errors_explained"]
            ]
            self.assertTrue(
                any("corrupt or truncated" in text for text in explanations),
                explanations,
            )
            text = render_pack_report(report)
            self.assertIn("PROBLEM READING PACK", text)
            self.assertIn("corrupt or truncated", text)
            # Identity still reports what pack.json alone could say.
            self.assertIn("broken", text)

    def test_missing_manifest_yields_report_not_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            root.mkdir(parents=True)
            report = describe_pack(root)
            self.assertFalse(report["health"]["valid"])
            self.assertFalse(report["origin"]["recorded"])
            joined = " ".join(report["not_known"])
            self.assertIn("Almost everything", joined)
            text = render_pack_report(report)
            self.assertIn("WHAT THIS REPORT CANNOT TELL YOU", text)
            self.assertIn("Almost everything", text)

            missing_root = Path(raw) / "does-not-exist"
            missing_report = describe_pack(missing_root)
            self.assertFalse(missing_report["health"]["valid"])
            self.assertTrue(
                any(
                    "not a directory" in error
                    for error in missing_report["report_errors"]
                )
            )
            render_pack_report(missing_report)

    def test_missing_tool_attestation_is_flagged_by_name(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            tools = _pinned_tools()
            del tools["blender"]
            _write_retail_pack(root, tools=tools)
            report = describe_pack(root)
            self.assertFalse(report["health"]["valid"])
            blender = next(
                tool for tool in report["tools"] if tool["name"] == "blender"
            )
            self.assertEqual(
                blender["verification"]["status"], "missing-attestation"
            )
            self.assertIsNone(blender["recorded"])
            text = render_pack_report(report)
            self.assertIn("MISSING", text)
            self.assertIn("blender", text)

    def test_json_document_is_stable_and_self_describing(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(root)
            first = describe_pack(root)
            second = describe_pack(root)

            # Literal values, not the module constants: tooling in the wild
            # keys on these exact strings, so renaming the constant must
            # redden this test.
            self.assertEqual(first["schema"], "openbfme.pack-provenance-report")
            self.assertEqual(first["schemaVersion"], 1)
            self.assertEqual(first["schema"], REPORT_SCHEMA)
            self.assertEqual(first["schemaVersion"], REPORT_SCHEMA_VERSION)
            self.assertEqual(
                sorted(first),
                sorted(
                    [
                        "schema",
                        "schemaVersion",
                        "pack_root",
                        "identity",
                        "origin",
                        "tools",
                        "source_archives",
                        "contents",
                        "health",
                        "not_known",
                        "report_errors",
                    ]
                ),
            )
            # Serializable and deterministic across runs.
            self.assertEqual(
                json.dumps(first, sort_keys=True),
                json.dumps(second, sort_keys=True),
            )

    def test_cli_describe_pack_text_json_and_exit_codes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            _write_retail_pack(root)

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = cli.main(["describe-pack", str(root)])
            self.assertEqual(code, 0)
            self.assertIn("PACK PROVENANCE REPORT", stdout.getvalue())

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = cli.main(["--json", "describe-pack", str(root)])
            self.assertEqual(code, 0)
            document = json.loads(stdout.getvalue())
            self.assertEqual(document["schema"], "openbfme.pack-provenance-report")

            broken = Path(raw) / "broken"
            (broken / "provenance").mkdir(parents=True)
            (broken / "provenance" / "manifest.json").write_text(
                "not json", encoding="utf-8"
            )
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = cli.main(["describe-pack", str(broken)])
            self.assertEqual(code, 3)
            self.assertIn("PACK PROVENANCE REPORT", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
