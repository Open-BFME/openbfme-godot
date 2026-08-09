from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import openbfme_importer.w3d_input_stage as stage_module

from openbfme_importer.w3d_input_stage import (
    MAX_W3D_STAGE_BYTES,
    MAX_W3D_STAGE_FILES,
    W3D_INPUT_STAGE_MANIFEST,
    W3DInputStageError,
    W3DInputStageLimitError,
    W3DInputStageReuseError,
    build_w3d_input_stage,
)


def _aggregate(files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _manifest(files: dict[str, bytes]) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 32
    for precedence, (path, payload) in enumerate(
        sorted(files.items(), key=lambda item: (item[0].casefold(), item[0]))
    ):
        inventory.append(
            {
                "archive": "fixture/asset.big",
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )
        offset += len(payload)
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": len(inventory),
            "format": 4,
            "identity_sha256": "a" * 64,
        },
        "install": {
            "identity_sha256": "b" * 64,
            "root": "synthetic-retail-install",
        },
        "totals": {
            "bytes": sum(len(payload) for payload in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _write_manifest(root: Path, manifest: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_bytes(
        (
            json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")
    )


def _write_source(root: Path, files: dict[str, bytes]) -> dict[str, object]:
    root.mkdir(parents=True, exist_ok=True)
    for relative, payload in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    manifest = _manifest(files)
    _write_manifest(root, manifest)
    return manifest


def _manifest_path(root: Path) -> Path:
    return root.joinpath(*W3D_INPUT_STAGE_MANIFEST.split("/"))


def _ordinary_files(root: Path) -> list[str]:
    return sorted(
        (
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
        ),
        key=lambda value: (value.casefold(), value),
    )


class W3DInputStageTests(unittest.TestCase):
    def test_stages_every_w3d_with_stable_path_mapping_and_private_manifest(
        self,
    ) -> None:
        files = {
            "art/w3d/Hero.w3d": b"model-bytes",
            "art/w3d/Hero_Skeleton.W3D": b"hierarchy-bytes",
            "art/w3d/anims/Hero_Run.w3d": b"animation-bytes",
            "art/textures/Hero_D.dds": b"texture-is-verified-not-copied",
            "data/readme.txt": b"also-verified",
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective-assets"
            output = base / "w3d-inputs"
            _write_source(source, files)

            report = build_w3d_input_stage(source, output)

            expected = {
                "art/w3d/Hero.w3d": "art/w3d/Hero.w3d",
                "art/w3d/Hero_Skeleton.W3D": "art/w3d/Hero_Skeleton.W3D",
                "art/w3d/anims/Hero_Run.w3d": "art/w3d/anims/Hero_Run.w3d",
            }
            self.assertFalse(report.reused)
            self.assertEqual(report.staged_inputs, expected)
            self.assertEqual(report.file_count, 3)
            self.assertEqual(
                report.total_bytes,
                sum(len(files[path]) for path in expected),
            )
            self.assertEqual(
                _ordinary_files(output),
                [
                    ".openbfme/w3d-input-stage.json",
                    "art/w3d/anims/Hero_Run.w3d",
                    "art/w3d/Hero.w3d",
                    "art/w3d/Hero_Skeleton.W3D",
                ],
            )
            for source_path, staged_path in expected.items():
                self.assertEqual(
                    output.joinpath(*staged_path.split("/")).read_bytes(),
                    files[source_path],
                )
                self.assertEqual(
                    output.joinpath(*staged_path.split("/")).stat().st_nlink,
                    1,
                )
            document = json.loads(_manifest_path(output).read_text(encoding="utf-8"))
            self.assertEqual(document["schema"], "openbfme.w3d-input-stage")
            self.assertEqual(
                document["summary"], {"bytes": report.total_bytes, "files": 3}
            )
            self.assertEqual(document["outputTreeSha256"], report.output_tree_sha256)
            self.assertEqual(document["requestSha256"], report.request_sha256)
            self.assertEqual(document["identitySha256"], report.identity_sha256)
            self.assertNotIn(str(source), json.dumps(report.neutral(), sort_keys=True))
            json.dumps(report.json_ready(), sort_keys=True)

    def test_matching_output_is_exact_noop_reuse(self) -> None:
        files = {"art/a.w3d": b"a", "art/b.W3D": b"bb", "other.bin": b"x"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            first = build_w3d_input_stage(
                source, output, max_files=9, max_total_bytes=99
            )
            before_manifest = _manifest_path(output).read_bytes()
            before_stats = {path: path.stat().st_mtime_ns for path in output.rglob("*")}

            second = build_w3d_input_stage(
                source, output, max_files=9, max_total_bytes=99
            )

            self.assertTrue(second.reused)
            self.assertFalse(first.reused)
            self.assertEqual(first.identity_sha256, second.identity_sha256)
            self.assertEqual(first.request_sha256, second.request_sha256)
            self.assertEqual(first.staged_inputs, second.staged_inputs)
            self.assertEqual(_manifest_path(output).read_bytes(), before_manifest)
            self.assertEqual(
                {path: path.stat().st_mtime_ns for path in output.rglob("*")},
                before_stats,
            )

    def test_force_rebuilds_tampered_or_extraneous_output(self) -> None:
        files = {"art/model.w3d": b"trusted-model"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            build_w3d_input_stage(source, output)
            model = output / "art" / "model.w3d"
            model.write_bytes(b"X" * len(files["art/model.w3d"]))

            with self.assertRaisesRegex(W3DInputStageReuseError, "failed verification"):
                build_w3d_input_stage(source, output)
            rebuilt = build_w3d_input_stage(source, output, force=True)
            self.assertFalse(rebuilt.reused)
            self.assertEqual(model.read_bytes(), files["art/model.w3d"])

            extra = output / "undeclared.bin"
            extra.write_bytes(b"extra")
            with self.assertRaisesRegex(
                W3DInputStageReuseError, "exact declared files"
            ):
                build_w3d_input_stage(source, output)
            build_w3d_input_stage(source, output, force=True)
            self.assertFalse(extra.exists())

    def test_rejects_source_payload_tamper_and_exact_tree_extras(self) -> None:
        files = {"art/model.w3d": b"model", "textures/model.dds": b"tex00"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            (source / "textures" / "model.dds").write_bytes(b"tex99")
            with self.assertRaisesRegex(W3DInputStageError, "SHA-256"):
                build_w3d_input_stage(source, output)
            self.assertFalse(output.exists())

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            (source / "undeclared.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(W3DInputStageError, "undeclared files"):
                build_w3d_input_stage(source, output)
            self.assertFalse(output.exists())

    def test_stale_output_from_another_verified_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            first_source = base / "source-a"
            second_source = base / "source-b"
            output = base / "stage"
            _write_source(first_source, {"art/model.w3d": b"first"})
            _write_source(second_source, {"art/model.w3d": b"other"})
            first = build_w3d_input_stage(first_source, output)
            original = _manifest_path(output).read_bytes()

            with self.assertRaisesRegex(W3DInputStageReuseError, "verified request"):
                build_w3d_input_stage(second_source, output)

            self.assertEqual(_manifest_path(output).read_bytes(), original)
            self.assertEqual((output / "art" / "model.w3d").read_bytes(), b"first")
            self.assertFalse(first.reused)

    def test_force_publish_failure_rolls_back_the_prior_stage(self) -> None:
        files = {"art/model.w3d": b"original"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            original_report = build_w3d_input_stage(source, output)
            original_manifest = _manifest_path(output).read_bytes()
            real_replace = os.replace

            def fail_stage_publish(source_path: object, target_path: object) -> None:
                source_value = Path(source_path)  # type: ignore[arg-type]
                target_value = Path(target_path)  # type: ignore[arg-type]
                if (
                    source_value.name.startswith(f".{output.name}.staging-")
                    and target_value == output
                ):
                    raise OSError("synthetic publication failure")
                real_replace(source_path, target_path)

            with mock.patch.object(
                stage_module.os, "replace", side_effect=fail_stage_publish
            ):
                with self.assertRaisesRegex(
                    W3DInputStageError, "prior output was preserved"
                ):
                    build_w3d_input_stage(source, output, force=True)

            restored = build_w3d_input_stage(source, output)
            self.assertTrue(restored.reused)
            self.assertEqual(restored.identity_sha256, original_report.identity_sha256)
            self.assertEqual(_manifest_path(output).read_bytes(), original_manifest)
            self.assertEqual((output / "art" / "model.w3d").read_bytes(), b"original")
            self.assertFalse(
                any(path.name.startswith(".stage.") for path in base.iterdir())
            )

    def test_limits_fail_closed_without_truncation_and_cannot_raise_hard_caps(
        self,
    ) -> None:
        files = {"art/a.w3d": b"1234", "art/b.w3d": b"5678"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            with self.assertRaisesRegex(W3DInputStageLimitError, "selects 2 files"):
                build_w3d_input_stage(source, output, max_files=1)
            with self.assertRaisesRegex(W3DInputStageLimitError, "selects 8 bytes"):
                build_w3d_input_stage(source, output, max_total_bytes=7)
            with self.assertRaises(ValueError):
                build_w3d_input_stage(source, output, max_files=MAX_W3D_STAGE_FILES + 1)
            with self.assertRaises(ValueError):
                build_w3d_input_stage(
                    source, output, max_total_bytes=MAX_W3D_STAGE_BYTES + 1
                )
            with self.assertRaises(TypeError):
                build_w3d_input_stage(source, output, max_files=True)
            with self.assertRaises(TypeError):
                build_w3d_input_stage(source, output, force=1)  # type: ignore[arg-type]
            self.assertFalse(output.exists())

    def test_rejects_manifest_path_escape_case_collision_and_noncanonical_json(
        self,
    ) -> None:
        payload = b"model"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "source"
            root.mkdir()
            manifest = _manifest({"art/model.w3d": payload})
            entry = manifest["files"][0]  # type: ignore[index]
            entry["path"] = "../escape.w3d"  # type: ignore[index]
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])  # type: ignore[arg-type,index]
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DInputStageError, "unsafe"):
                build_w3d_input_stage(root, Path(raw) / "output")

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "source"
            root.mkdir()
            manifest = _manifest({"Art/Model.w3d": payload, "art/model.W3D": payload})
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DInputStageError, "case-colliding paths"):
                build_w3d_input_stage(root, Path(raw) / "output")

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "source"
            _write_source(root, {"art/model.w3d": payload})
            manifest_path = root / ".openbfme" / "manifest.json"
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest_path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(W3DInputStageError, "not canonical"):
                build_w3d_input_stage(root, Path(raw) / "output")

    def test_rejects_links_and_hardlinks(self) -> None:
        files = {"art/model.w3d": b"model"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            declared = source / "art" / "model.w3d"
            external = base / "external.w3d"
            external.write_bytes(files["art/model.w3d"])
            declared.unlink()
            try:
                os.symlink(external, declared)
            except OSError:
                declared.write_bytes(files["art/model.w3d"])
                original = stage_module._is_link_like
                with mock.patch.object(
                    stage_module,
                    "_is_link_like",
                    side_effect=lambda path: path == declared or original(path),
                ):
                    with self.assertRaisesRegex(W3DInputStageError, "contains a link"):
                        build_w3d_input_stage(source, output)
            else:
                with self.assertRaisesRegex(W3DInputStageError, "contains a link"):
                    build_w3d_input_stage(source, output)

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, files)
            declared = source / "art" / "model.w3d"
            hardlink = source / "hardlink.w3d"
            try:
                os.link(declared, hardlink)
            except OSError:
                self.skipTest("platform cannot create a hard link")
            with self.assertRaisesRegex(W3DInputStageError, "hard-linked file"):
                build_w3d_input_stage(source, output)

    def test_rejects_source_output_overlap_and_linked_output_root(self) -> None:
        files = {"art/model.w3d": b"model"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            _write_source(source, files)
            with self.assertRaisesRegex(W3DInputStageError, "must not overlap"):
                build_w3d_input_stage(source, source / "nested-output")
            self.assertFalse((source / "nested-output").exists())
            with self.assertRaisesRegex(W3DInputStageError, "must not overlap"):
                build_w3d_input_stage(source, base)

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            actual_output = base / "actual-output"
            linked_output = base / "linked-output"
            _write_source(source, files)
            actual_output.mkdir()
            try:
                os.symlink(actual_output, linked_output, target_is_directory=True)
            except OSError:
                linked_output.mkdir()
                original = stage_module._is_link_like
                with mock.patch.object(
                    stage_module,
                    "_is_link_like",
                    side_effect=lambda path: path == linked_output or original(path),
                ):
                    with self.assertRaisesRegex(
                        W3DInputStageError, "must not be linked"
                    ):
                        build_w3d_input_stage(source, linked_output)
            else:
                # A real symlinked output root hits the same guard as the
                # mocked branch above (`W3D input stage root must not be
                # linked`); "is linked" is the _refuse_link_chain wording for
                # report/manifest paths, which this path never reaches.
                with self.assertRaisesRegex(
                    W3DInputStageError, "must not be linked"
                ):
                    build_w3d_input_stage(source, linked_output)

    def test_rejects_retail_derived_output_in_public_repository_space(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            _write_source(source, {"art/model.w3d": b"model"})
            output = stage_module.repo_root_from_module() / (
                f".w3d-input-stage-forbidden-{os.getpid()}"
            )
            self.assertFalse(output.exists())
            with self.assertRaisesRegex(
                W3DInputStageError, "must stay under its ignored .private"
            ):
                build_w3d_input_stage(source, output)
            self.assertFalse(output.exists())

    def test_path_mapping_and_hashes_are_stable_across_host_roots(self) -> None:
        files = {
            "art/w3d/Faction/Unit.w3d": b"unit",
            "art/w3d/Faction/Unit_Run.W3D": b"run",
            "art/textures/Unit.tga": b"pixels",
        }
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as second_raw,
        ):
            first = Path(first_raw)
            second = Path(second_raw)
            first_source = first / "source"
            second_source = second / "another-source-name"
            _write_source(first_source, files)
            _write_source(second_source, files)

            first_report = build_w3d_input_stage(first_source, first / "output")
            second_report = build_w3d_input_stage(
                second_source, second / "different-output"
            )

            self.assertEqual(first_report.staged_inputs, second_report.staged_inputs)
            self.assertEqual(
                first_report.selected_inventory_sha256,
                second_report.selected_inventory_sha256,
            )
            self.assertEqual(
                first_report.selected_root_sha256,
                second_report.selected_root_sha256,
            )
            self.assertEqual(
                first_report.output_tree_sha256, second_report.output_tree_sha256
            )
            self.assertEqual(first_report.request_sha256, second_report.request_sha256)
            self.assertEqual(
                first_report.identity_sha256, second_report.identity_sha256
            )
            self.assertEqual(
                first_report.manifest_sha256, second_report.manifest_sha256
            )
            self.assertEqual(first_report.neutral(), second_report.neutral())

    def test_directory_case_aliases_normalize_without_splitting_hierarchy_neighbors(
        self,
    ) -> None:
        files = {
            "Art/W3D/Model.w3d": b"model",
            "art/w3d/Hierarchy.W3D": b"hierarchy",
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            source.mkdir()
            physical = source / "Art" / "W3D"
            physical.mkdir(parents=True)
            (physical / "Model.w3d").write_bytes(files["Art/W3D/Model.w3d"])
            (physical / "Hierarchy.W3D").write_bytes(files["art/w3d/Hierarchy.W3D"])
            _write_manifest(source, _manifest(files))

            report = build_w3d_input_stage(source, base / "stage")

            self.assertEqual(
                report.staged_inputs,
                {
                    "art/w3d/Hierarchy.W3D": "Art/W3D/Hierarchy.W3D",
                    "Art/W3D/Model.w3d": "Art/W3D/Model.w3d",
                },
            )
            parents = {
                staged_path.rsplit("/", 1)[0]
                for staged_path in report.staged_inputs.values()
            }
            self.assertEqual(parents, {"Art/W3D"})
            self.assertEqual(
                _ordinary_files(base / "stage"),
                [
                    ".openbfme/w3d-input-stage.json",
                    "Art/W3D/Hierarchy.W3D",
                    "Art/W3D/Model.w3d",
                ],
            )

    def test_empty_w3d_selection_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "stage"
            _write_source(source, {"art/texture.dds": b"texture"})
            with self.assertRaisesRegex(W3DInputStageError, "declares no W3D"):
                build_w3d_input_stage(source, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
