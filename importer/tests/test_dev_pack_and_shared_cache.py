"""Tests for --dev pack path and shared media/W3D DDC keys."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
import unittest
from unittest import mock

from openbfme_importer import cli
from openbfme_importer.faction_census import PlayableFaction
from openbfme_importer.pipeline import (
    ImportPipeline,
    _media_conversion_cache_key,
    _w3d_conversion_cache_key,
    audit_pack,
)
from importer.tests.test_integrity import make_audited_pack


class SharedMediaCacheKeyTests(unittest.TestCase):
    def test_media_key_ignores_pack_relative_path_prefix(self) -> None:
        common = dict(
            source_sha256="a" * 64,
            converter="texture",
            options={"level": 6},
            tool_token="pillow:12.2.0:png6",
        )
        men = _media_conversion_cache_key(
            **common, relative_output="packs/men/art/foo.png"
        )
        elves = _media_conversion_cache_key(
            **common, relative_output="packs/elves/art/foo.png"
        )
        self.assertEqual(men, elves)
        # Suffix still matters (png vs dds)
        dds = _media_conversion_cache_key(
            **common, relative_output="packs/men/art/foo.dds"
        )
        self.assertNotEqual(men, dds)

    def test_media_key_changes_with_source_and_tool(self) -> None:
        base = dict(
            source_sha256="a" * 64,
            converter="texture",
            options={},
            tool_token="pillow:12.2.0:png9",
            relative_output="out.png",
        )
        k = _media_conversion_cache_key(**base)
        self.assertNotEqual(
            k,
            _media_conversion_cache_key(
                **{**base, "source_sha256": "b" * 64}
            ),
        )
        self.assertNotEqual(
            k,
            _media_conversion_cache_key(
                **{**base, "tool_token": "pillow:12.2.0:png6"}
            ),
        )

    def test_w3d_logical_key_ignores_absolute_output_path(self) -> None:
        source_hashes = {"model.w3d": "a" * 64}
        common = dict(
            source_hashes=source_hashes,
            adapter_sha256="c" * 64,
            plugin_attestation_sha256="d" * 64,
            blender_tree_sha256="e" * 64,
        )
        logical = {
            "asset_kind": "static",
            "model_name": "keep",
            "animation_names": [],
            "required_equipment": [],
            "excluded_optional_meshes": [],
            "proven_root_rigid_bake": False,
            "options": {},
        }
        # Legacy argument_vector path would embed absolute --output paths.
        path_a = _w3d_conversion_cache_key(
            **common,
            argument_vector=["blender", "--output", "C:/packs/men/keep.glb"],
        )
        path_b = _w3d_conversion_cache_key(
            **common,
            argument_vector=["blender", "--output", "C:/packs/elves/keep.glb"],
        )
        self.assertNotEqual(path_a, path_b)

        logical_a = _w3d_conversion_cache_key(**common, logical=logical)
        logical_b = _w3d_conversion_cache_key(**common, logical=logical)
        self.assertEqual(logical_a, logical_b)
        # Logical key is stable across what would have been path-partitioned keys.
        self.assertNotEqual(logical_a, path_a)


class LightAuditTests(unittest.TestCase):
    def test_light_audit_skips_hash_but_catches_size(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            make_audited_pack(root)
            target = root / "data" / "objects.json"
            original = target.read_bytes()
            # Same size, different content — full audit fails, light passes.
            flipped = bytes((b ^ 0xFF) for b in original)
            assert len(flipped) == len(original)
            target.write_bytes(flipped)

            full = audit_pack(root, light=False)
            light = audit_pack(root, light=True)
            self.assertFalse(full["valid"])
            self.assertFalse(full["light"])
            self.assertTrue(
                any("hash mismatch" in e for e in full["errors"]),
                full["errors"],
            )
            self.assertTrue(light["valid"], light["errors"])
            self.assertTrue(light["light"])

            # Size mismatch still fails in light mode.
            target.write_bytes(original + b"x")
            light_size = audit_pack(root, light=True)
            self.assertFalse(light_size["valid"])
            self.assertTrue(any("size mismatch" in e for e in light_size["errors"]))

    def test_dev_env_enables_light_audit_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "pack"
            make_audited_pack(root)
            target = root / "data" / "objects.json"
            original = target.read_bytes()
            target.write_bytes(bytes((b ^ 0x01) for b in original))
            prev = os.environ.get("OPENBFME_DEV")
            try:
                os.environ["OPENBFME_DEV"] = "1"
                result = audit_pack(root)  # light inferred from env
                self.assertTrue(result["valid"], result["errors"])
            finally:
                if prev is None:
                    os.environ.pop("OPENBFME_DEV", None)
                else:
                    os.environ["OPENBFME_DEV"] = prev


class SoftToolAttestTests(unittest.TestCase):
    def test_soft_fingerprint_changes_when_script_changes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            blender = root / "blender.exe"
            blender.write_bytes(b"exe")
            # Real portable layout uses <version>/scripts, not only root/scripts.
            scripts = root / "4.2" / "scripts"
            scripts.mkdir(parents=True)
            target = scripts / "startup" / "bl_operators"
            target.mkdir(parents=True)
            module = target / "object.py"
            module.write_text("a = 1\n", encoding="utf-8")
            pipeline = ImportPipeline.__new__(ImportPipeline)
            first = pipeline._blender_soft_tree_fingerprint(blender)
            module.write_text("a = 2\n", encoding="utf-8")
            second = pipeline._blender_soft_tree_fingerprint(blender)
            self.assertNotEqual(first, second)
            # Same-size content swap under versioned scripts must invalidate.
            module.write_text("a = 3\n", encoding="utf-8")
            same_size = pipeline._blender_soft_tree_fingerprint(blender)
            self.assertNotEqual(second, same_size)
            # Nested native swap under versioned tree.
            pyd = root / "4.2" / "python" / "lib" / "site-packages" / "x.pyd"
            pyd.parent.mkdir(parents=True)
            pyd.write_bytes(b"pyd1")
            with_pyd = pipeline._blender_soft_tree_fingerprint(blender)
            pyd.write_bytes(b"pyd2")
            swapped_pyd = pipeline._blender_soft_tree_fingerprint(blender)
            self.assertNotEqual(with_pyd, swapped_pyd)
            # Exe-only change also matters.
            blender.write_bytes(b"exe2")
            third = pipeline._blender_soft_tree_fingerprint(blender)
            self.assertNotEqual(swapped_pyd, third)

    def test_shipping_end_attest_defaults_to_full_not_soft(self) -> None:
        """Non-dev batches re-hash the tree unless SOFT is explicitly set."""
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            from openbfme_importer.catalog import InstallCatalog

            pipeline = ImportPipeline(InstallCatalog(install, (), ()), root / "state")
            pipeline.dev_mode = False
            blender = root / "tools" / "blender" / "blender.exe"
            blender.parent.mkdir(parents=True)
            blender.write_bytes(b"x" * 32)
            (blender.parent / "scripts").mkdir()
            plugin = root / "plugin"
            plugin.mkdir()
            (plugin / "io_mesh_w3d").mkdir()
            (plugin / "io_mesh_w3d" / "__init__.py").write_text("#", encoding="utf-8")
            pipeline._w3d_batch_tools = {
                "blender": blender,
                "plugin": plugin,
                "blender_tree_sha256": "0" * 64,
            }
            pipeline._blender_soft_tree_fingerprint_value = (
                pipeline._blender_soft_tree_fingerprint(blender)
            )
            # Soft fingerprint still matches, but shipping must not trust it alone
            # when soft is disabled — full hash path runs and fails vs pin.
            with (
                mock.patch.dict(os.environ, {}, clear=False),
                mock.patch(
                    "openbfme_importer.bootstrap._reject_tree_links"
                ),
                mock.patch(
                    "openbfme_importer.bootstrap._reject_python_bytecode"
                ),
                mock.patch(
                    "openbfme_importer.bootstrap._attest_opensage_plugin_checkout",
                    return_value={"clean": True},
                ),
                mock.patch(
                    "openbfme_importer.bootstrap.BLENDER_TREE_SHA256",
                    "cafebabe" * 8,
                ),
                mock.patch(
                    "openbfme_importer.pipeline.git_worktree_clean",
                    return_value=True,
                ),
                mock.patch(
                    "openbfme_importer.pipeline.directory_tree_sha256",
                    return_value="deadbeef" * 8,
                ) as tree_hash,
            ):
                # Ensure soft/strict env do not force soft.
                os.environ.pop("OPENBFME_SOFT_TOOL_ATTEST", None)
                os.environ.pop("OPENBFME_STRICT_TOOL_ATTEST", None)
                os.environ.pop("OPENBFME_DEV", None)
                with self.assertRaisesRegex(RuntimeError, "Blender portable tree changed"):
                    pipeline._end_w3d_conversion_batch()
                tree_hash.assert_called()


class DevCliTests(unittest.TestCase):
    def test_apply_dev_mode_sets_env_defaults(self) -> None:
        keys = ("OPENBFME_DEV", "OPENBFME_PNG_LEVEL", "OPENBFME_DEV_AUDIT")
        saved = {k: os.environ.get(k) for k in keys}
        try:
            for k in keys:
                os.environ.pop(k, None)
            prior = cli._apply_dev_mode(True)
            self.assertEqual(os.environ["OPENBFME_DEV"], "1")
            self.assertEqual(os.environ["OPENBFME_PNG_LEVEL"], "6")
            self.assertEqual(os.environ["OPENBFME_DEV_AUDIT"], "light")
            cli._restore_env(prior)
            self.assertIsNone(os.environ.get("OPENBFME_DEV"))
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v

    def test_parser_accepts_dev_and_convert_jobs(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args(
            [
                "import-faction",
                "--install",
                "C:/BFME2",
                "--faction",
                "men",
                "--convert",
                "--dev",
                "--convert-jobs",
                "4",
            ]
        )
        self.assertTrue(args.dev)
        self.assertEqual(args.convert_jobs, 4)

        build_args = parser.parse_args(
            [
                "build",
                "--install",
                "C:/BFME2",
                "--profile",
                "men-fords-v0",
                "--dev",
            ]
        )
        self.assertTrue(build_args.dev)

    def test_import_faction_convert_forwards_jobs_and_dev(self) -> None:
        coverage = {
            "aggregateSha256": "a" * 64,
            "summary": {
                "conversionComplete": True,
                "convertedCount": 1,
                "objectCount": 1,
                "excludedCount": 0,
                "converterGapCount": 0,
                "unresolvedLeafCount": 0,
            },
        }
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            effective_root = state_root / "cache" / "effective-assets"
            manifest = effective_root / "manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("{}", encoding="utf-8")
            pipeline = mock.Mock()
            pipeline._effective_asset_paths.return_value = (
                effective_root,
                manifest,
                Path(raw) / "staging",
                Path(raw) / "backup",
            )
            convert = mock.Mock(return_value=coverage)
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(cli, "ImportPipeline", return_value=pipeline),
                mock.patch.object(
                    cli,
                    "resolve_playable_faction",
                    return_value=PlayableFaction("FactionMen", "Men", 57),
                ),
                mock.patch.object(cli, "convert_faction_import", convert),
            ):
                # Clear so _apply_dev_mode can set defaults.
                for key in ("OPENBFME_DEV", "OPENBFME_PNG_LEVEL", "OPENBFME_DEV_AUDIT"):
                    os.environ.pop(key, None)
                result = cli.main(
                    [
                        "--state-root",
                        str(state_root),
                        "import-faction",
                        "--install",
                        "C:/BFME2",
                        "--faction",
                        "men",
                        "--convert",
                        "--dev",
                        "--convert-jobs",
                        "3",
                    ]
                )
            self.assertEqual(result, 0)
            convert.assert_called_once()
            kwargs = convert.call_args.kwargs
            self.assertEqual(kwargs.get("convert_jobs"), 3)
            self.assertEqual(kwargs.get("game"), "bfme2")
            self.assertEqual(Path(kwargs["state_root"]), state_root)
            # --dev is scoped to main(): env restored after the command.
            self.assertNotEqual(os.environ.get("OPENBFME_DEV"), "1")
