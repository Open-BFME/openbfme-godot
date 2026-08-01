from __future__ import annotations

import unittest
from unittest import mock

try:
    import openbfme_importer.visual_leaf as visual_leaf
    from openbfme_importer.visual_leaf import (
        VisualLeafBatchError,
        VisualLeafRequest,
        VisualLeafResolutionError,
        diagnose_visual_leaves,
        resolve_visual_leaf,
        resolve_visual_leaves,
    )
except ModuleNotFoundError as exc:
    if exc.name != "openbfme_importer":
        raise
    import importer.openbfme_importer.visual_leaf as visual_leaf
    from importer.openbfme_importer.visual_leaf import (
        VisualLeafBatchError,
        VisualLeafRequest,
        VisualLeafResolutionError,
        diagnose_visual_leaves,
        resolve_visual_leaf,
        resolve_visual_leaves,
    )


class VisualLeafTests(unittest.TestCase):
    def test_resolves_each_single_leaf_role_and_preserves_canonical_path(self) -> None:
        catalog = [
            "Art/Textures/Stone.DDS",
            "Art/Shadows/UnitShadow.TGA",
            "Art/Models/Sword.W3D",
            "data/unrelated.ini",
        ]
        texture = resolve_visual_leaf(catalog, "art/textures/stone", "texture")
        shadow = resolve_visual_leaf(catalog, "unitshadow.tga", "shadow")
        model = resolve_visual_leaf(catalog, "sWoRd", "attached-model")

        self.assertEqual(texture.representation, "texture")
        self.assertEqual(texture.leaves[0].virtual_path, "Art/Textures/Stone.DDS")
        self.assertEqual(texture.leaves[0].role, "texture")
        self.assertIn("extensionless-stem", texture.leaves[0].evidence)
        self.assertEqual(shadow.leaves[0].role, "shadow-texture")
        self.assertEqual(shadow.leaves[0].virtual_path, "Art/Shadows/UnitShadow.TGA")
        self.assertEqual(model.representation, "w3d")
        self.assertEqual(model.leaves[0].role, "attached-model")
        self.assertEqual(model.leaves[0].virtual_path, "Art/Models/Sword.W3D")

    def test_house_color_accepts_tga_or_complete_jpg_png_pair(self) -> None:
        catalog = [
            "art/hc/solid.tga",
            "art/hc/pair.PNG",
            "art/hc/pair.JPG",
        ]
        tga = resolve_visual_leaf(catalog, "SOLID", "house-color")
        pair = resolve_visual_leaf(catalog, "art/hc/PAIR", "house-color")

        self.assertEqual(tga.representation, "tga")
        self.assertEqual(tga.leaves[0].role, "house-color-texture")
        self.assertEqual(pair.representation, "jpg-png")
        self.assertEqual(
            [(leaf.role, leaf.virtual_path) for leaf in pair.leaves],
            [
                ("house-color-color", "art/hc/pair.JPG"),
                ("house-color-alpha", "art/hc/pair.PNG"),
            ],
        )
        self.assertTrue(all("complete-jpg-png-pair" in leaf.evidence for leaf in pair.leaves))

    def test_house_color_rejects_partial_and_multiple_alternatives(self) -> None:
        with self.assertRaisesRegex(VisualLeafResolutionError, "missing.*incomplete") as partial:
            resolve_visual_leaf(["art/hc/team.jpg"], "team", "house-color")
        self.assertEqual(partial.exception.status, "missing")
        with self.assertRaisesRegex(VisualLeafResolutionError, "ambiguous.*coexist") as mixed:
            resolve_visual_leaf(
                ["art/hc/team.tga", "art/hc/team.jpg", "art/hc/team.png"],
                "team",
                "house-color",
            )
        self.assertEqual(mixed.exception.status, "ambiguous")
        with self.assertRaisesRegex(VisualLeafResolutionError, "multiple house-color"):
            resolve_visual_leaf(
                ["art/a/team.tga", "art/b/team.tga"], "team", "house-color"
            )

    def test_explicit_house_color_pair_is_anchored_without_format_fallback(self) -> None:
        catalog = [
            "art/hc/team.tga",
            "art/hc/team.jpg",
            "art/hc/team.png",
        ]
        pair = resolve_visual_leaf(catalog, "art/hc/TEAM.JPG", "house-color")
        tga = resolve_visual_leaf(catalog, "art/hc/team.tga", "house-color")
        self.assertEqual(pair.representation, "jpg-png")
        self.assertEqual(tga.representation, "tga")
        with self.assertRaisesRegex(VisualLeafResolutionError, "missing.*missing .png"):
            resolve_visual_leaf(
                ["art/hc/lone.jpg", "other/lone.png"],
                "art/hc/lone.jpg",
                "house-color",
            )

    def test_particle_requires_unique_representation_or_caller_pin(self) -> None:
        catalog = ["art/fx/dust.tga", "art/fx/dust.w3d"]
        with self.assertRaisesRegex(VisualLeafResolutionError, "ambiguous") as unpinned:
            resolve_visual_leaf(catalog, "dust", "particle")
        self.assertEqual(unpinned.exception.candidate_count, 2)

        texture = resolve_visual_leaf(
            catalog, "dust", "particle", representation="texture"
        )
        model = resolve_visual_leaf(catalog, "dust", "particle", representation="w3d")
        explicit = resolve_visual_leaf(catalog, "DUST.W3D", "particle")
        self.assertEqual(texture.leaves[0].role, "particle-texture")
        self.assertEqual(model.leaves[0].role, "particle-model")
        self.assertEqual(explicit.leaves[0].virtual_path, "art/fx/dust.w3d")
        with self.assertRaisesRegex(VisualLeafResolutionError, "not allowed"):
            resolve_visual_leaf(
                catalog, "dust.w3d", "particle", representation="texture"
            )

    def test_extensionless_and_explicit_resolution_never_guess(self) -> None:
        catalog = [
            "art/a/wall.dds",
            "art/a/wall.tga",
            "art/b/wall.tga",
        ]
        with self.assertRaisesRegex(VisualLeafResolutionError, "ambiguous"):
            resolve_visual_leaf(catalog, "wall", "texture")
        self.assertEqual(
            resolve_visual_leaf(catalog, "art/a/wall.dds", "texture")
            .leaves[0]
            .virtual_path,
            "art/a/wall.dds",
        )
        with self.assertRaisesRegex(VisualLeafResolutionError, "ambiguous"):
            resolve_visual_leaf(catalog, "wall.tga", "texture")
        with self.assertRaisesRegex(VisualLeafResolutionError, "missing") as missing:
            resolve_visual_leaf(catalog, "not-wall", "texture")
        self.assertEqual(missing.exception.status, "missing")

    def test_rejects_case_ambiguous_and_duplicate_catalog_paths(self) -> None:
        with self.assertRaisesRegex(ValueError, "case-ambiguous"):
            resolve_visual_leaf(
                ["art/Texture.TGA", "ART/texture.tga"], "texture", "texture"
            )
        with self.assertRaisesRegex(ValueError, "duplicate visual catalog path"):
            resolve_visual_leaf(
                ["art/texture.tga", "art/texture.tga"], "texture", "texture"
            )

    def test_rejects_unsafe_inputs_and_kind_extension_mismatches(self) -> None:
        cases = [
            (["../escape.tga"], "safe", "texture", None, "unsafe visual catalog"),
            (["art/safe.tga"], "../safe", "texture", None, "unsafe visual identifier"),
            (["art/safe.tga"], "*.tga", "texture", None, "wildcard"),
            (["art/safe.tga"], "safe.bmp", "texture", None, "unsupported explicit"),
            (["art/safe.tga"], "safe", "unknown", None, "unsupported visual kind"),
            (["art/safe.tga"], "safe", "texture", "w3d", "only valid for particle"),
            (["art/safe.tga"], "safe.tga", "attached-model", None, "not allowed"),
        ]
        for catalog, identifier, kind, representation, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    resolve_visual_leaf(
                        catalog,
                        identifier,
                        kind,
                        representation=representation,
                    )

    def test_batch_diagnostics_are_classified_bounded_and_deterministic(self) -> None:
        catalog = [
            "art/a/shared.tga",
            "art/b/shared.png",
            "art/model/unit.w3d",
        ]
        requests = [
            VisualLeafRequest("unit", "attached-model"),
            VisualLeafRequest("missing", "texture"),
            VisualLeafRequest("shared", "texture"),
        ]
        first = diagnose_visual_leaves(catalog, requests)
        second = diagnose_visual_leaves(reversed(catalog), requests)
        self.assertEqual(first, second)
        self.assertEqual([item.status for item in first.diagnostics], ["missing", "ambiguous"])
        self.assertEqual([item.request_index for item in first.diagnostics], [1, 2])
        self.assertEqual(first.diagnostics[1].candidate_count, 2)
        self.assertEqual(
            first.diagnostics[1].candidates,
            ("art/a/shared.tga", "art/b/shared.png"),
        )
        self.assertIsNotNone(first.resolutions[0])
        self.assertIsNone(first.resolutions[1])
        with self.assertRaises(VisualLeafBatchError) as strict:
            resolve_visual_leaves(catalog, requests)
        self.assertEqual(strict.exception.diagnostics, first.diagnostics)

    def test_strict_batch_returns_request_order_and_neutral_data(self) -> None:
        catalog = ["z/second.w3d", "a/first.tga"]
        requests = [
            VisualLeafRequest("second", "particle", "w3d"),
            VisualLeafRequest("first", "shadow"),
        ]
        resolved = resolve_visual_leaves(catalog, requests)
        self.assertEqual(
            [item.request.identifier for item in resolved], ["second", "first"]
        )
        neutral = diagnose_visual_leaves(catalog, requests).neutral()
        self.assertEqual(neutral["diagnostics"], [])
        self.assertNotIn("source", str(neutral).casefold())

    def test_enforces_catalog_request_path_identifier_and_diagnostic_bounds(self) -> None:
        with mock.patch.object(visual_leaf, "MAX_VISUAL_CATALOG_PATHS", 1):
            with self.assertRaisesRegex(ValueError, "catalog path.*exceeds"):
                resolve_visual_leaf(["a/a.tga", "b/b.tga"], "a", "texture")
        with mock.patch.object(visual_leaf, "MAX_VISUAL_REQUESTS", 1):
            with self.assertRaisesRegex(ValueError, "visual request count exceeds"):
                diagnose_visual_leaves(
                    ["a/a.tga"],
                    [
                        VisualLeafRequest("a", "texture"),
                        VisualLeafRequest("b", "texture"),
                    ],
                )
        with mock.patch.object(visual_leaf, "MAX_VISUAL_PATH_LENGTH", 8):
            with self.assertRaisesRegex(ValueError, "visual catalog path exceeds"):
                resolve_visual_leaf(["long/path/a.tga"], "a", "texture")
        with mock.patch.object(visual_leaf, "MAX_VISUAL_IDENTIFIER_LENGTH", 3):
            with self.assertRaisesRegex(ValueError, "visual identifier exceeds"):
                resolve_visual_leaf(["a.tga"], "long", "texture")
        with mock.patch.object(visual_leaf, "MAX_VISUAL_DIAGNOSTIC_CANDIDATES", 1):
            with self.assertRaises(VisualLeafResolutionError) as ambiguous:
                resolve_visual_leaf(
                    ["a/x.tga", "b/x.png"], "x", "texture"
                )
            self.assertEqual(ambiguous.exception.candidate_count, 2)
            self.assertEqual(ambiguous.exception.candidates, ("a/x.tga",))


if __name__ == "__main__":
    unittest.main()
