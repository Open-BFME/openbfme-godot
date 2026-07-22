"""Zero-byte retail animation placeholders must not become conversion clips."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.pipeline import ImportPipeline


class W3DEmptyPlaceholderAnimationTests(unittest.TestCase):
    def test_prepare_excludes_zero_byte_animation_and_keeps_real_clips(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            pipeline = ImportPipeline(
                InstallCatalog(install, archives=(), entries=()),
                root / "state",
            )
            blender = root / "blender.exe"
            blender.write_bytes(b"blender")
            plugin = root / "plugin"
            plugin.mkdir()
            pipeline._w3d_batch_tools = {
                "blender": blender,
                "plugin": plugin,
                "blender_tree_sha256": "a" * 64,
                "plugin_attestation_sha256": "b" * 64,
            }

            pack = root / "pack"
            pack.mkdir()
            sources = root / "sources"
            sources.mkdir()
            model = sources / "rugimli_skn.w3d"
            real_clip = sources / "rugimli_idla.w3d"
            empty_clip = sources / "rugimli_idlg.w3d"
            model.write_bytes(b"MODEL" * 32)
            real_clip.write_bytes(b"CLIP" * 32)
            empty_clip.write_bytes(b"")

            with (
                mock.patch.object(
                    pipeline,
                    "_copy_w3d_cache_hit",
                    return_value=None,
                ),
                mock.patch(
                    "openbfme_importer.pipeline._prepare_w3d_no_motion_animations",
                    return_value=None,
                ),
                mock.patch(
                    "openbfme_importer.pipeline._prepare_w3d_secondary_skin_streams",
                    return_value=None,
                ),
                mock.patch(
                    "openbfme_importer.pipeline._apply_w3d_texture_overrides",
                    return_value=None,
                ),
            ):
                prepared = pipeline._prepare_w3d_bundle_job(
                    0,
                    [model, real_clip, empty_clip],
                    "assets/models/units/gimli/00.glb",
                    {
                        "model": "rugimli_skn.w3d",
                        "animations": [
                            "rugimli_idla.w3d",
                            "rugimli_idlg.w3d",
                        ],
                    },
                    pack,
                    "test-profile",
                    "unit-dwarvengimli-visual-00",
                    "animated",
                )

            self.assertEqual(prepared["animation_names"], ["rugimli_idla.w3d"])
            self.assertEqual(
                prepared["empty_placeholder_animations"], ["rugimli_idlg.w3d"]
            )
            self.assertEqual(len(prepared["animations"]), 1)
            self.assertEqual(prepared["animations"][0].name, "rugimli_idla.w3d")
            self.assertTrue(
                any(
                    str(part).casefold().endswith("rugimli_idla.w3d")
                    for part in prepared["command"]
                )
            )
            self.assertFalse(
                any(
                    str(part).casefold().endswith("rugimli_idlg.w3d")
                    for part in prepared["command"]
                )
            )

    def test_prepare_fails_closed_when_every_animation_is_zero_byte(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            pipeline = ImportPipeline(
                InstallCatalog(install, archives=(), entries=()),
                root / "state",
            )
            blender = root / "blender.exe"
            blender.write_bytes(b"blender")
            plugin = root / "plugin"
            plugin.mkdir()
            pipeline._w3d_batch_tools = {
                "blender": blender,
                "plugin": plugin,
                "blender_tree_sha256": "a" * 64,
                "plugin_attestation_sha256": "b" * 64,
            }
            pack = root / "pack"
            pack.mkdir()
            sources = root / "sources"
            sources.mkdir()
            model = sources / "model.w3d"
            empty_clip = sources / "empty.w3d"
            model.write_bytes(b"MODEL" * 32)
            empty_clip.write_bytes(b"")

            with (
                mock.patch(
                    "openbfme_importer.pipeline._prepare_w3d_no_motion_animations",
                    return_value=None,
                ),
                mock.patch(
                    "openbfme_importer.pipeline._prepare_w3d_secondary_skin_streams",
                    return_value=None,
                ),
                mock.patch(
                    "openbfme_importer.pipeline._apply_w3d_texture_overrides",
                    return_value=None,
                ),
                self.assertRaisesRegex(ValueError, "zero-byte retail placeholders"),
            ):
                pipeline._prepare_w3d_bundle_job(
                    0,
                    [model, empty_clip],
                    "assets/models/units/x/00.glb",
                    {
                        "model": "model.w3d",
                        "animations": ["empty.w3d"],
                    },
                    pack,
                    "test-profile",
                    "unit-x-visual-00",
                    "animated",
                )


if __name__ == "__main__":
    unittest.main()
