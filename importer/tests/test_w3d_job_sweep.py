"""Gate the standalone W3D job sweep's staging and accounting."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def load_sweep_module():
    path = REPO_ROOT / "tools" / "w3d_job_sweep.py"
    spec = importlib.util.spec_from_file_location("openbfme_test_w3d_job_sweep", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load w3d_job_sweep.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["openbfme_test_w3d_job_sweep"] = module
    spec.loader.exec_module(module)
    return module


SWEEP = load_sweep_module()


class W3dJobSweepStagingTests(unittest.TestCase):
    def _profile(self) -> dict:
        return {
            "resources": [
                {
                    "id": "prop-textures",
                    "converter": "texture",
                    "patterns": ["art/tex/prop.dds"],
                },
                {
                    "id": "prop-model",
                    "converter": "w3d-hierarchical",
                    "patterns": ["art/w3d/prop.w3d"],
                    "options": {
                        "model": "prop.w3d",
                        "inputResourceIds": ["prop-textures"],
                        "provenRootRigidBake": True,
                    },
                },
                {
                    "id": "unit-model",
                    "converter": "w3d-bundle",
                    "patterns": ["art/w3d/unit*.w3d"],
                    "options": {
                        "model": "unit.w3d",
                        "animations": ["unit_idle.w3d", "unit_empty.w3d"],
                    },
                },
            ]
        }

    def test_staging_pulls_the_declared_closure_and_maps_the_asset_kind(self) -> None:
        profile = self._profile()
        by_id = {item["id"]: item for item in profile["resources"]}
        with tempfile.TemporaryDirectory() as raw:
            assets = Path(raw) / "assets"
            (assets / "art" / "tex").mkdir(parents=True)
            (assets / "art" / "w3d").mkdir(parents=True)
            (assets / "art" / "tex" / "prop.dds").write_bytes(b"texture")
            (assets / "art" / "w3d" / "prop.w3d").write_bytes(b"model")
            batch_root = Path(raw) / "batch"

            job = SWEEP.stage_job(
                by_id["prop-model"], by_id, assets, batch_root
            )

            self.assertEqual(job["job_id"], "prop-model")
            self.assertEqual(job["asset_kind"], "hierarchical")
            self.assertIs(job["proven_root_rigid_bake"], True)
            self.assertEqual(job["animations"], [])
            staged = sorted(
                path.name for path in (batch_root / "prop-model" / "input").iterdir()
            )
            # The texture owner's payload must be staged beside the model, or
            # the pinned importer would generate a placeholder the build does
            # not see.
            self.assertEqual(staged, ["prop.dds", "prop.w3d"])

    def test_zero_byte_retail_animation_placeholders_are_dropped(self) -> None:
        profile = self._profile()
        by_id = {item["id"]: item for item in profile["resources"]}
        with tempfile.TemporaryDirectory() as raw:
            assets = Path(raw) / "assets"
            (assets / "art" / "w3d").mkdir(parents=True)
            (assets / "art" / "w3d" / "unit.w3d").write_bytes(b"model")
            (assets / "art" / "w3d" / "unit_idle.w3d").write_bytes(b"clip")
            (assets / "art" / "w3d" / "unit_empty.w3d").write_bytes(b"")
            batch_root = Path(raw) / "batch"

            job = SWEEP.stage_job(by_id["unit-model"], by_id, assets, batch_root)

            self.assertEqual(job["asset_kind"], "animated")
            self.assertEqual(
                [Path(item).name for item in job["animations"]], ["unit_idle.w3d"]
            )

    def test_missing_model_and_unknown_owner_fail_closed(self) -> None:
        profile = self._profile()
        by_id = {item["id"]: item for item in profile["resources"]}
        with tempfile.TemporaryDirectory() as raw:
            assets = Path(raw) / "assets"
            assets.mkdir()
            with self.assertRaisesRegex(RuntimeError, "model not staged"):
                SWEEP.stage_job(by_id["prop-model"], by_id, assets, Path(raw) / "b1")
            orphan = dict(by_id["prop-model"])
            orphan["options"] = dict(orphan["options"])
            orphan["options"]["inputResourceIds"] = ["absent-owner"]
            with self.assertRaisesRegex(RuntimeError, "unknown input resource"):
                SWEEP.stage_job(orphan, by_id, assets, Path(raw) / "b2")


class W3dJobSweepMarkerTests(unittest.TestCase):
    def test_markers_are_parsed_from_a_real_shaped_log(self) -> None:
        log = (
            'OPENBFME_W3D_JOB_OK {"job_id": "alpha", "report": {"meshes": 1}}\r\n'
            'OPENBFME_W3D_JOB_FAIL {"error": "boom", "failure_phase": '
            '"skin-validation", "job_id": "beta"}\r\n'
            'OPENBFME_W3D_JOB_OK {"job_id": "gamma"}\r\n'
        )
        ok = set(SWEEP.OK_MARKER.findall(log))
        failures = {
            json.loads(raw)["job_id"]: json.loads(raw)
            for raw in SWEEP.FAIL_MARKER.findall(log)
        }

        self.assertEqual(ok, {"alpha", "gamma"})
        self.assertEqual(set(failures), {"beta"})
        self.assertEqual(failures["beta"]["failure_phase"], "skin-validation")


if __name__ == "__main__":
    unittest.main()
