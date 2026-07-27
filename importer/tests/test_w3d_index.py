from __future__ import annotations

import json
import unittest
from unittest import mock

from openbfme_importer.w3d_index import (
    W3DFileHeaders,
    W3DReferenceRequest,
    W3DResolutionError,
    build_w3d_index,
    reject_ambiguous_w3d_trims,
    resolve_w3d_reference,
    resolve_w3d_references,
    resolve_w3d_references_partial,
    trim_w3d_identifier,
)


class W3DIndexTests(unittest.TestCase):
    def test_resolves_container_subobject_with_explicit_evidence(self) -> None:
        index = build_w3d_index(["Art/W3D/GBWatchTower.w3d"])

        resolved = resolve_w3d_reference(
            index, "model", "gbwatchtower.ROOF"
        )

        self.assertEqual(
            resolved.physical_virtual_path, "Art/W3D/GBWatchTower.w3d"
        )
        self.assertEqual(resolved.logical_id, "gbwatchtower.ROOF")
        self.assertEqual(resolved.container_id, "GBWatchTower")
        self.assertEqual(resolved.subobject_id, "ROOF")
        self.assertIsNone(resolved.header_id)
        self.assertEqual(
            [item.rule for item in resolved.evidence], ["container-subobject"]
        )

    def test_resolves_exact_physical_path_without_basename_guessing(self) -> None:
        index = build_w3d_index(
            ["art/w3d/a/shared.w3d", "art/w3d/b/shared.w3d"]
        )

        resolved = resolve_w3d_reference(
            index, "file", "ART\\W3D\\A\\SHARED.W3D"
        )
        partial = resolve_w3d_references_partial(
            index, [W3DReferenceRequest("file", "shared.w3d")]
        )

        self.assertEqual(
            resolved.physical_virtual_path, "art/w3d/a/shared.w3d"
        )
        self.assertEqual(
            [item.rule for item in resolved.evidence], ["exact-virtual-path"]
        )
        self.assertFalse(partial.complete)
        self.assertFalse(partial.missing)
        self.assertEqual(len(partial.ambiguous), 1)
        self.assertEqual(
            [item.physical_virtual_path for item in partial.ambiguous[0].candidates],
            ["art/w3d/a/shared.w3d", "art/w3d/b/shared.w3d"],
        )

    def test_resolves_model_animation_and_hierarchy_header_ids(self) -> None:
        index = build_w3d_index(
            ["art/w3d/GUHero.w3d", "art/w3d/GUHero_SKL.w3d"],
            [
                W3DFileHeaders(
                    "ART/W3D/guhero.w3d",
                    model_ids=("GUHero", "GUHero.SWORD"),
                ),
                W3DFileHeaders(
                    "art/w3d/GUHero_SKL.w3d",
                    animation_ids=(
                        "GUHero_SKL.GUHero_RUNA",
                        "GUHero_SKL.GUHero_IDLA",
                    ),
                    hierarchy_ids=("GUHero_SKL",),
                ),
            ],
        )

        model = resolve_w3d_reference(index, "model", "guhero.sword")
        animation = resolve_w3d_reference(
            index, "animation", "guhero_skl.guhero_idla"
        )
        hierarchy = resolve_w3d_reference(index, "hierarchy", "guhero_skl")

        self.assertEqual(model.header_id, "GUHero.SWORD")
        self.assertEqual(model.physical_virtual_path, "art/w3d/GUHero.w3d")
        self.assertEqual(animation.header_id, "GUHero_SKL.GUHero_IDLA")
        self.assertEqual(
            animation.physical_virtual_path, "art/w3d/GUHero_SKL.w3d"
        )
        self.assertEqual(hierarchy.header_id, "GUHero_SKL")
        self.assertEqual(
            {item.rule for item in hierarchy.evidence},
            {"exact-stem", "header-id"},
        )

    def test_unique_stem_is_exact_but_multiple_stems_are_ambiguous(self) -> None:
        unique = build_w3d_index(["art/w3d/GBFarm.w3d"])
        resolved = resolve_w3d_reference(unique, "model", "gbfarm")
        self.assertEqual(resolved.physical_virtual_path, "art/w3d/GBFarm.w3d")
        self.assertEqual([item.rule for item in resolved.evidence], ["exact-stem"])

        ambiguous = build_w3d_index(
            ["art/w3d/units/GBFarm.w3d", "art/w3d/buildings/gbfarm.W3D"]
        )
        result = resolve_w3d_references_partial(
            ambiguous, [W3DReferenceRequest("model", "GBFARM")]
        )
        self.assertEqual(len(result.ambiguous), 1)
        with self.assertRaisesRegex(W3DResolutionError, "ambiguous W3D model"):
            resolve_w3d_reference(ambiguous, "model", "GBFARM")

    def test_duplicate_header_ids_are_retained_as_ambiguity_or_strictly_rejected(self) -> None:
        paths = ["art/w3d/a.w3d", "art/w3d/b.w3d"]
        headers = [
            W3DFileHeaders("art/w3d/a.w3d", animation_ids=("FAMILY.IDLE",)),
            W3DFileHeaders("art/w3d/b.w3d", animation_ids=("family.idle",)),
        ]
        index = build_w3d_index(paths, headers)

        self.assertEqual(len(index.duplicate_header_ids), 1)
        self.assertEqual(index.duplicate_header_ids[0].kind, "animation")
        result = resolve_w3d_references_partial(
            index, [W3DReferenceRequest("animation", "Family.Idle")]
        )
        self.assertEqual(len(result.ambiguous), 1)
        self.assertEqual(len(result.ambiguous[0].candidates), 2)

        with self.assertRaisesRegex(ValueError, "duplicate animation header ID"):
            build_w3d_index(
                ["a.w3d"],
                [
                    W3DFileHeaders(
                        "a.w3d", animation_ids=("FAMILY.IDLE", "family.idle")
                    )
                ],
            )
        with self.assertRaisesRegex(ValueError, "across W3D files"):
            build_w3d_index(
                paths, headers, reject_duplicate_header_ids=True
            )

    def test_semantic_tokens_require_explicit_caller_classification(self) -> None:
        index = build_w3d_index(["art/w3d/real.w3d"])
        normal = resolve_w3d_references_partial(
            index,
            [
                W3DReferenceRequest("model", "None"),
                W3DReferenceRequest("hierarchy", "MODEL"),
            ],
        )
        self.assertEqual(
            [(item.kind, item.requested_id) for item in normal.missing],
            [("model", "None"), ("hierarchy", "MODEL")],
        )

        semantic = resolve_w3d_references(
            index,
            [
                W3DReferenceRequest("model", "None", semantic=True),
                W3DReferenceRequest("hierarchy", "MODEL", semantic=True),
            ],
        )
        self.assertTrue(all(item.semantic for item in semantic))
        self.assertTrue(all(item.physical_virtual_path is None for item in semantic))
        self.assertEqual(
            [[evidence.rule for evidence in item.evidence] for item in semantic],
            [["caller-semantic"], ["caller-semantic"]],
        )

    def test_batch_retains_missing_and_ambiguous_and_exact_fails_closed(self) -> None:
        index = build_w3d_index(
            ["a/common.w3d", "b/common.w3d", "models/known.w3d"]
        )
        requests = [
            W3DReferenceRequest("model", "missing"),
            W3DReferenceRequest("file", "common.w3d"),
            W3DReferenceRequest("model", "known"),
        ]
        result = resolve_w3d_references_partial(index, requests)

        self.assertEqual(len(result.resolved), 1)
        self.assertEqual(result.resolved[0].requested_id, "known")
        self.assertEqual(len(result.missing), 1)
        self.assertEqual(result.missing[0].reason, "missing")
        self.assertEqual(len(result.ambiguous), 1)
        self.assertEqual(result.ambiguous[0].reason, "ambiguous")
        self.assertEqual(len(result.ambiguous[0].candidates), 2)
        with self.assertRaisesRegex(W3DResolutionError, "missing W3D model") as caught:
            resolve_w3d_references(index, requests)
        self.assertEqual(caught.exception.diagnostic.requested_id, "missing")

    def test_input_order_does_not_change_index_or_batch_output(self) -> None:
        paths = ["z/Zeta.w3d", "a/Alpha.w3d", "m/Middle.w3d"]
        headers = [
            W3DFileHeaders(
                "z/Zeta.w3d",
                model_ids=("Zeta.B", "Zeta.A"),
                animation_ids=("Z_SKL.RUN", "Z_SKL.IDLE"),
            ),
            W3DFileHeaders("a/Alpha.w3d", hierarchy_ids=("A_SKL",)),
        ]
        first = build_w3d_index(paths, headers)
        second = build_w3d_index(list(reversed(paths)), list(reversed(headers)))
        requests = [
            W3DReferenceRequest("hierarchy", "a_skl"),
            W3DReferenceRequest("model", "middle"),
            W3DReferenceRequest("animation", "z_skl.idle"),
        ]

        first_json = json.dumps(first.neutral(), sort_keys=True, separators=(",", ":"))
        second_json = json.dumps(second.neutral(), sort_keys=True, separators=(",", ":"))
        self.assertEqual(first_json, second_json)
        self.assertEqual(
            resolve_w3d_references_partial(first, requests).neutral(),
            resolve_w3d_references_partial(second, reversed(requests)).neutral(),
        )

    def test_rejects_unsafe_duplicate_case_ambiguous_and_non_w3d_paths(self) -> None:
        cases = [
            (["../escape.w3d"], "unsafe W3D virtual path"),
            (["C:\\retail\\unit.w3d"], "unsafe W3D virtual path"),
            (["art/unit.bin"], "must end in .w3d"),
            (["art/unit.w3d", "art/unit.w3d"], "duplicate W3D virtual path"),
            (
                ["art/Unit.w3d", "ART/unit.W3D"],
                "case-ambiguous W3D virtual paths",
            ),
        ]
        for paths, message in cases:
            with self.subTest(paths=paths), self.assertRaisesRegex(ValueError, message):
                build_w3d_index(paths)

        with self.assertRaisesRegex(ValueError, "no physical virtual path"):
            build_w3d_index(
                ["present.w3d"], [W3DFileHeaders("missing.w3d")]
            )

    def test_enforces_path_header_identifier_and_request_bounds(self) -> None:
        with mock.patch("openbfme_importer.w3d_index.MAX_W3D_PATHS", 1):
            with self.assertRaisesRegex(ValueError, "path count"):
                build_w3d_index(["a.w3d", "b.w3d"])
        with mock.patch("openbfme_importer.w3d_index.MAX_W3D_HEADER_FILES", 0):
            with self.assertRaisesRegex(ValueError, "header file count"):
                build_w3d_index(["a.w3d"], [W3DFileHeaders("a.w3d")])
        with mock.patch("openbfme_importer.w3d_index.MAX_W3D_HEADER_IDS_PER_FILE", 1):
            with self.assertRaisesRegex(ValueError, "header ID count"):
                build_w3d_index(
                    ["a.w3d"],
                    [W3DFileHeaders("a.w3d", model_ids=("A", "B"))],
                )
        with mock.patch("openbfme_importer.w3d_index.MAX_W3D_TOTAL_HEADER_IDS", 1):
            with self.assertRaisesRegex(ValueError, "total header ID count"):
                build_w3d_index(
                    ["a.w3d"],
                    [
                        W3DFileHeaders(
                            "a.w3d", model_ids=("A",), hierarchy_ids=("H",)
                        )
                    ],
                )
        with mock.patch("openbfme_importer.w3d_index.MAX_W3D_REQUESTS", 1):
            index = build_w3d_index(["a.w3d"])
            with self.assertRaisesRegex(ValueError, "request count"):
                resolve_w3d_references_partial(
                    index,
                    [
                        W3DReferenceRequest("model", "A"),
                        W3DReferenceRequest("model", "B"),
                    ],
                )

        too_long_path = "a" * 509 + ".w3d"
        with self.assertRaisesRegex(ValueError, "character limit"):
            build_w3d_index([too_long_path])
        too_long_id = "A" * 256
        with self.assertRaisesRegex(ValueError, "unsafe model header ID"):
            build_w3d_index(
                ["a.w3d"],
                [W3DFileHeaders("a.w3d", model_ids=(too_long_id,))],
            )
        with self.assertRaisesRegex(ValueError, "unsafe W3D request identifier"):
            resolve_w3d_reference(build_w3d_index(["a.w3d"]), "model", "A..B")

    def test_rejects_bad_request_shapes_and_duplicates(self) -> None:
        index = build_w3d_index(["a.w3d"])
        with self.assertRaisesRegex(ValueError, "unsupported W3D reference kind"):
            resolve_w3d_reference(index, "texture", "a")
        with self.assertRaisesRegex(ValueError, "file reference must end"):
            resolve_w3d_reference(index, "file", "a")
        with self.assertRaisesRegex(ValueError, "duplicate W3D model request"):
            resolve_w3d_references_partial(
                index,
                [
                    W3DReferenceRequest("model", "A"),
                    W3DReferenceRequest("model", "a", semantic=True),
                ],
            )
        with self.assertRaisesRegex(ValueError, "request count"):
            resolve_w3d_references_partial(index, [])


class W3DTrimAdmissionTests(unittest.TestCase):
    def test_trim_w3d_identifier_trims_each_component_and_fails_closed(
        self,
    ) -> None:
        self.assertEqual(trim_w3d_identifier("TOWER_01 "), "TOWER_01")
        self.assertEqual(trim_w3d_identifier("\tROCK_01"), "ROCK_01")
        # Padding on the first component of a composite sits in the interior
        # of the joined string; each dot-separated component is trimmed.
        self.assertEqual(trim_w3d_identifier("DBMINE_A .ROCK_24\t"), "DBMINE_A.ROCK_24")
        # Interior whitespace inside one component is preserved, not repaired.
        self.assertEqual(
            trim_w3d_identifier("GB.ROTATE CONTROL "), "GB.ROTATE CONTROL"
        )
        # An identifier that is not padding damage stays fail-closed.
        for value in ("", " ", "\t", "A. ", " .B", ". ."):
            with self.assertRaises(ValueError):
                trim_w3d_identifier(value)
        with self.assertRaises(TypeError):
            trim_w3d_identifier(None)  # type: ignore[arg-type]

    def test_reject_ambiguous_w3d_trims_is_exact_and_order_independent(
        self,
    ) -> None:
        occupied = {
            ("model", "tower.body"): {"a.w3d", "b.w3d"},
            ("model", "unique.body"): {"a.w3d"},
            ("hierarchy", "rig"): {"c.w3d"},
        }
        # A trim whose target only the trimming file itself claims is unique.
        self.assertEqual(
            reject_ambiguous_w3d_trims(
                occupied, {"a.w3d": {("model", "unique.body")}}
            ),
            frozenset(),
        )
        # A trim colliding with any other claimant fails closed.
        self.assertEqual(
            reject_ambiguous_w3d_trims(
                occupied, {"a.w3d": {("model", "tower.body")}}
            ),
            frozenset({"a.w3d"}),
        )
        # Kind namespaces are separate: a model trim never collides with a
        # hierarchy id of the same spelling.
        self.assertEqual(
            reject_ambiguous_w3d_trims(occupied, {"a.w3d": {("model", "rig")}}),
            frozenset(),
        )
        # Two files trimming to one logical id are both rejected, and the
        # result does not depend on mapping order.
        both = {
            "a.w3d": {("model", "tower.body")},
            "b.w3d": {("model", "tower.body")},
        }
        self.assertEqual(
            reject_ambiguous_w3d_trims(occupied, both),
            frozenset({"a.w3d", "b.w3d"}),
        )
        self.assertEqual(
            reject_ambiguous_w3d_trims(occupied, dict(reversed(both.items()))),
            frozenset({"a.w3d", "b.w3d"}),
        )


if __name__ == "__main__":
    unittest.main()
