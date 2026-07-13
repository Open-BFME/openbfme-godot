from __future__ import annotations

import copy
import json
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.pipeline import _validated_w3d_metadata, bundle_digest


W3D_REPORT_FIXTURE = Path(__file__).parent / "fixtures" / "w3d_adapter_report.json"


class BundleDigestTests(unittest.TestCase):
    def test_digest_is_order_independent_and_byte_sensitive(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "nested").mkdir()
            (root / "b.txt").write_bytes(b"b")
            (root / "nested" / "a.txt").write_bytes(b"a")
            first = bundle_digest(root)
            second = bundle_digest(root)
            self.assertEqual(first, second)
            (root / "nested" / "a.txt").write_bytes(b"A")
            self.assertNotEqual(first, bundle_digest(root))


class W3dBundleMetadataTests(unittest.TestCase):
    def _report(self) -> dict:
        report = json.loads(W3D_REPORT_FIXTURE.read_text(encoding="utf-8"))
        report["equipment_attachments_canonicalized_restored_and_revalidated"] = True
        return report

    def test_fixture_produces_semantic_payload_free_bundle_metadata(self) -> None:
        report = self._report()
        report["model"] = "private-model-name.w3d"
        report["output"] = "private/output/path.glb"
        report["mesh_inventory"][0]["source_name"] = "private-mesh-name"
        report["private_attachment_proof"] = {
            "parent": "private-parent-name",
            "transform": [1.0, 2.0, 3.0],
        }
        metadata = _validated_w3d_metadata(
            report,
            ["right-hand-weapon", "left-hand-shield"],
            expected_animation_count=2,
        )

        self.assertEqual(metadata["schema"], "openbfme.w3d-presentation-capabilities")
        self.assertTrue(metadata["capabilities"]["nonRenderGeometryExcluded"])
        self.assertTrue(metadata["capabilities"]["requiredEquipmentProven"])
        self.assertTrue(
            metadata["capabilities"][
                "equipmentAttachmentsCanonicalizedRestoredAndRevalidated"
            ]
        )
        self.assertEqual(
            metadata["equipment"]["right-hand-weapon"]["attachment"], "right-hand"
        )
        self.assertEqual(
            metadata["equipment"]["left-hand-shield"]["attachment"], "left-hand"
        )
        self.assertEqual(metadata["metrics"]["filteredNonRenderGeometryCount"], 2)
        serialized = json.dumps(metadata, sort_keys=True)
        self.assertNotIn("private-model-name", serialized)
        self.assertNotIn("private/output", serialized)
        self.assertNotIn("private-mesh-name", serialized)
        self.assertNotIn("private-parent-name", serialized)

    def test_report_fails_closed_on_remaining_helpers_wrong_hand_or_missing_role(self) -> None:
        cases: list[tuple[str, dict, str]] = []

        remaining_helper = self._report()
        remaining_helper["remaining_non_render_geometry"] = 1
        cases.append(("helper", remaining_helper, "collision or helper"))

        ambiguous_box = self._report()
        ambiguous_box["remaining_ambiguous_box_geometry"] = 1
        cases.append(("box", ambiguous_box, "box-shaped"))

        attachment_not_restored = self._report()
        attachment_not_restored[
            "equipment_attachments_canonicalized_restored_and_revalidated"
        ] = False
        cases.append(
            (
                "attachment-restoration",
                attachment_not_restored,
                "canonicalize, restore, and revalidate requested equipment",
            )
        )

        wrong_hand = self._report()
        wrong_hand["mesh_inventory"][1]["attachment"] = "left-hand"
        cases.append(("wrong-hand", wrong_hand, "right hand"))

        missing_shield = self._report()
        missing_shield["mesh_inventory"][2].update(
            {
                "semantic_role": "character-mesh",
                "attachment": "skeletal",
                "proof_methods": [],
            }
        )
        missing_shield["equipment"].pop("left-hand-shield")
        cases.append(("missing-shield", missing_shield, "did not prove required equipment"))

        for name, report, message in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, message):
                    _validated_w3d_metadata(
                        copy.deepcopy(report),
                        ["right-hand-weapon", "left-hand-shield"],
                        expected_animation_count=2,
                    )

    def test_report_rejects_noncanonical_inventory_and_unrequested_capability(self) -> None:
        report = self._report()
        report["mesh_inventory"][0]["index"] = 7
        with self.assertRaisesRegex(RuntimeError, "order is not canonical"):
            _validated_w3d_metadata(
                report,
                ["right-hand-weapon", "left-hand-shield"],
                expected_animation_count=2,
            )

        mismatched = self._report()
        mismatched["required_equipment"] = ["right-hand-weapon"]
        with self.assertRaisesRegex(RuntimeError, "did not enforce"):
            _validated_w3d_metadata(
                mismatched,
                ["right-hand-weapon", "left-hand-shield"],
                expected_animation_count=2,
            )


if __name__ == "__main__":
    unittest.main()
