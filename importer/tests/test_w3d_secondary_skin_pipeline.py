from __future__ import annotations

import hashlib
from pathlib import Path
import tempfile
import unittest

from importer.openbfme_importer.pipeline import (
    W3D_SECONDARY_SKIN_CHUNKS,
    _prepare_w3d_secondary_skin_streams,
)
from importer.openbfme_importer.w3d_metadata import scan_w3d_metadata
from importer.tests.test_w3d_secondary_skin import _hierarchy, _model


class W3DSecondarySkinPipelineTests(unittest.TestCase):
    def _inputs(
        self,
        root: Path,
        *,
        model_bytes: bytes | None = None,
        hierarchy_bytes: bytes | None = None,
    ) -> tuple[dict[str, Path], Path]:
        model = root / "model.w3d"
        hierarchy = root / "rig.w3d"
        texture = root / "texture.dds"
        model.write_bytes(_model() if model_bytes is None else model_bytes)
        hierarchy.write_bytes(
            _hierarchy() if hierarchy_bytes is None else hierarchy_bytes
        )
        texture.write_bytes(b"unrelated texture")
        return (
            {
                "model.w3d": model,
                "rig.w3d": hierarchy,
                "texture.dds": texture,
            },
            model,
        )

    def test_job_local_transform_selects_one_hierarchy_and_preserves_other_inputs(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            copied, model = self._inputs(root)
            hierarchy_before = copied["rig.w3d"].read_bytes()
            texture_before = copied["texture.dds"].read_bytes()

            proof = _prepare_w3d_secondary_skin_streams(copied, model)

            self.assertIsNotNone(proof)
            assert proof is not None
            self.assertEqual(proof["schema"], "openbfme.w3d-secondary-skin-proof")
            self.assertEqual(proof["hierarchyInputBasename"], "rig.w3d")
            self.assertEqual(
                proof["outputModelSha256"],
                hashlib.sha256(model.read_bytes()).hexdigest(),
            )
            self.assertNotEqual(
                proof["stagedClosureBeforeSha256"],
                proof["stagedClosureAfterSha256"],
            )
            self.assertEqual(copied["rig.w3d"].read_bytes(), hierarchy_before)
            self.assertEqual(copied["texture.dds"].read_bytes(), texture_before)
            metadata = scan_w3d_metadata(model.read_bytes(), model.name)
            self.assertFalse(
                W3D_SECONDARY_SKIN_CHUNKS
                & {chunk.chunk_id for chunk in metadata.chunks}
            )

    def test_model_without_secondary_streams_is_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            copied, model = self._inputs(root, model_bytes=_hierarchy())
            before = model.read_bytes()
            self.assertIsNone(_prepare_w3d_secondary_skin_streams(copied, model))
            self.assertEqual(model.read_bytes(), before)

    def test_missing_or_ambiguous_compatible_hierarchy_fails_before_output(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            copied, model = self._inputs(
                root,
                hierarchy_bytes=_hierarchy(identity="OTHER_SKL"),
            )
            before = model.read_bytes()
            with self.assertRaisesRegex(RuntimeError, "found 0"):
                _prepare_w3d_secondary_skin_streams(copied, model)
            self.assertEqual(model.read_bytes(), before)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            copied, model = self._inputs(root)
            duplicate = root / "duplicate-rig.w3d"
            duplicate.write_bytes(copied["rig.w3d"].read_bytes())
            copied["duplicate-rig.w3d"] = duplicate
            before = model.read_bytes()
            with self.assertRaisesRegex(RuntimeError, "found 2"):
                _prepare_w3d_secondary_skin_streams(copied, model)
            self.assertEqual(model.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
