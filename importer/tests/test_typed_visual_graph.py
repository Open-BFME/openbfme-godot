from __future__ import annotations

import json
import unittest

try:
    from openbfme_importer.typed_visual_graph import (
        TypedVisualGraphError,
        require_complete_typed_visual_graph,
        resolve_typed_visual_documents,
        resolve_typed_visual_graph,
    )
    from openbfme_importer.w3d_index import W3DFileHeaders, build_w3d_index
except ModuleNotFoundError as exc:
    if exc.name != "openbfme_importer":
        raise
    from importer.openbfme_importer.typed_visual_graph import (
        TypedVisualGraphError,
        require_complete_typed_visual_graph,
        resolve_typed_visual_documents,
        resolve_typed_visual_graph,
    )
    from importer.openbfme_importer.w3d_index import W3DFileHeaders, build_w3d_index

try:
    from openbfme_importer.sage_cst import ResolvedSageCst, SageObject
except ModuleNotFoundError as exc:
    if exc.name != "openbfme_importer":
        raise
    from importer.openbfme_importer.sage_cst import ResolvedSageCst, SageObject


def _index(*, ambiguous: bool = False):
    paths = [
        "art/w3d/base.w3d",
        "art/w3d/base_skl.w3d",
        "art/w3d/overlay.w3d",
        "art/w3d/replacement.w3d",
        "art/w3d/construction.w3d",
        "art/w3d/damaged.w3d",
        "art/w3d/reallydamaged.w3d",
        "art/w3d/rubble.w3d",
    ]
    headers = [
        W3DFileHeaders("art/w3d/base.w3d", model_ids=("BaseModel",)),
        W3DFileHeaders(
            "art/w3d/base_skl.w3d",
            animation_ids=("BASE_SKL.BASE_IDLE", "BASE_SKL.BASE_DAMAGE"),
            hierarchy_ids=("BASE_SKL",),
        ),
        W3DFileHeaders("art/w3d/overlay.w3d", model_ids=("OverlayModel",)),
        W3DFileHeaders(
            "art/w3d/replacement.w3d", model_ids=("ReplacementModel",)
        ),
        W3DFileHeaders(
            "art/w3d/construction.w3d", model_ids=("ConstructionModel",)
        ),
        W3DFileHeaders("art/w3d/damaged.w3d", model_ids=("DamagedModel",)),
        W3DFileHeaders(
            "art/w3d/reallydamaged.w3d", model_ids=("ReallyDamagedModel",)
        ),
        W3DFileHeaders("art/w3d/rubble.w3d", model_ids=("RubbleModel",)),
    ]
    if ambiguous:
        paths.extend(("duplicate/a.w3d", "duplicate/b.w3d"))
        headers.extend(
            (
                W3DFileHeaders("duplicate/a.w3d", model_ids=("AmbiguousModel",)),
                W3DFileHeaders("duplicate/b.w3d", model_ids=("ambiguousmodel",)),
            )
        )
    return build_w3d_index(paths, headers)


class TypedVisualGraphTests(unittest.TestCase):
    def test_include_inheritance_resolves_tagged_draws_and_source_provenance(self) -> None:
        documents = {
            "data/entry.ini": b'#include "base.ini"\n#include "child.ini"\n',
            "data/base.ini": b"""
Object VisualBase
  Draw = W3DScriptedModelDraw ModuleTag_Main
    OkToChangeModelColor = Yes
    DefaultModelConditionState
      Model = BaseModel
      Skeleton = BASE_SKL
      Texture = BaseColor.tga BaseSnow.tga
    End
    IdleAnimationState
      Animation = Idle
        AnimationName = BASE_SKL.BASE_IDLE
      End
    End
  End
  Shadow = SHADOW_DECAL
  ShadowTexture = ShadowI
End
""",
            "data/child.ini": b"""
ChildObject VisualChild VisualBase
  Draw = W3DScriptedModelDraw ModuleTag_Overlay
    DefaultModelConditionState
      Model = OverlayModel
    End
  End
End
""",
        }
        catalog = [
            "art/textures/BaseColor.tga",
            "art/textures/BaseSnow.tga",
            "art/shadows/ShadowI.tga",
        ]
        graph = resolve_typed_visual_documents(
            "DATA/ENTRY.INI", documents, ["visualchild"], _index(), catalog
        )

        self.assertTrue(graph.complete)
        item = graph.objects[0]
        self.assertEqual(item.name, "VisualChild")
        self.assertEqual(item.ancestry, ("VisualBase", "VisualChild"))
        self.assertEqual(
            [module.instance_tag for module in item.draw_modules],
            ["ModuleTag_Main", "ModuleTag_Overlay"],
        )
        self.assertTrue(item.draw_modules[0].inherited)
        self.assertFalse(item.draw_modules[1].inherited)
        model = next(
            ref for ref in graph.references if ref.identifier == "BaseModel"
        )
        self.assertEqual(model.status, "resolved")
        self.assertEqual(model.physical_virtual_paths, ("art/w3d/base.w3d",))
        self.assertEqual(model.provenance.defining_object, "VisualBase")
        self.assertEqual(model.provenance.virtual_path, "data/base.ini")
        self.assertEqual(model.provenance.inheritance_distance, 1)
        self.assertEqual(
            next(ref for ref in item.references if ref.kind == "shadow").physical_virtual_paths,
            ("art/shadows/ShadowI.tga",),
        )
        self.assertTrue(
            next(
                prop
                for prop in item.draw_modules[0].properties
                if prop.key == "OkToChangeModelColor"
            ).enabled
        )

    def test_same_tag_replaces_the_inherited_module_without_asset_fallback(self) -> None:
        documents = {
            "entry.ini": b"""
Object Base
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = BaseModel
    End
  End
End
ChildObject Replacement Base
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = ReplacementModel
    End
  End
End
"""
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["Replacement"], _index()
        )

        self.assertTrue(graph.complete)
        module = graph.objects[0].draw_modules[0]
        self.assertFalse(module.inherited)
        self.assertEqual(module.provenance.defining_object, "Replacement")
        self.assertEqual(
            [item.identifier for item in graph.references], ["ReplacementModel"]
        )

    def test_remove_module_removes_only_the_exact_inherited_draw_tag(self) -> None:
        documents = {
            "entry.ini": b"""
Object Base
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = BaseModel
    End
  End
  Draw = W3DScriptedModelDraw ModuleTag_Overlay
    DefaultModelConditionState
      Model = OverlayModel
    End
  End
End
ChildObject Child Base
  RemoveModule ModuleTag_Main
End
"""
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["Child"], _index()
        )

        self.assertEqual(
            [item.instance_tag for item in graph.objects[0].draw_modules],
            ["ModuleTag_Overlay"],
        )
        self.assertEqual([item.identifier for item in graph.references], ["OverlayModel"])

    def test_lifecycle_states_cover_construction_damage_rubble_and_post_rubble(self) -> None:
        documents = {
            "entry.ini": b"""
Object Building
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = BaseModel
    End
    ModelConditionState = AWAITING_CONSTRUCTION
      Model = ConstructionModel
    End
    ModelConditionState DAMAGED
      Model = DamagedModel
    End
    ModelConditionState = REALLYDAMAGED
      Model = ReallyDamagedModel
    End
    ModelConditionState = RUBBLE
      Model = RubbleModel
    End
    ModelConditionState = POST_RUBBLE
      Model = None
    End
  End
End
"""
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["Building"], _index()
        )

        self.assertEqual(
            graph.objects[0].lifecycle_coverage,
            (
                "intact",
                "construction",
                "damaged",
                "really-damaged",
                "rubble",
                "post-rubble",
            ),
        )
        state_phases = {
            state.conditions: state.lifecycle_phases
            for state in graph.objects[0].draw_modules[0].states
        }
        self.assertEqual(state_phases[("DAMAGED",)], ("damaged",))
        none = next(item for item in graph.references if item.identifier == "None")
        self.assertEqual(none.status, "semantic")
        self.assertEqual(none.reason, "sage-none-model")
        self.assertFalse(none.physical_virtual_paths)
        self.assertTrue(graph.complete)

    def test_resolves_visual_roles_and_preserves_uncoerced_shadow_recolour_flags(self) -> None:
        documents = {
            "entry.ini": b"""
Object VisualRoles
  Draw = W3DScriptedModelDraw ModuleTag_Main
    ModelName = ReplacementModel
    RandomTexture = RandomA.tga 0 Diffuse.dds
    UpgradeTexture = Diffuse.dds 0 Upgrade.tga
    WeatherTexture = SNOWY Snow.tga
    DefaultModelConditionState
      Model = BaseModel
      Skeleton = MODEL
      Texture = Diffuse.dds Snow.tga
      Texture = "textures\\Quoted.tga"
      AttachedModel = Sword.w3d
      ParticleName = Dust.tga
      RecolorHouse = Maybe
      Shadow = SHADOW_ALPHA_DECAL
      ShadowTexture = Blob
    End
  End
  HouseColor = TeamColor
End
"""
        }
        catalog = [
            "textures/Diffuse.dds",
            "textures/Snow.tga",
            "textures/Quoted.tga",
            "textures/RandomA.tga",
            "textures/Upgrade.tga",
            "textures/TeamColor.jpg",
            "textures/TeamColor.png",
            "models/Sword.w3d",
            "particles/Dust.tga",
            "shadows/Blob.png",
        ]
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["VisualRoles"], _index(), catalog
        )

        by_identifier = {item.identifier: item for item in graph.references}
        self.assertEqual(by_identifier["MODEL"].status, "semantic")
        self.assertEqual(by_identifier["MODEL"].reason, "sage-model-skeleton")
        self.assertEqual(
            by_identifier["TeamColor"].physical_virtual_paths,
            ("textures/TeamColor.jpg", "textures/TeamColor.png"),
        )
        self.assertEqual(by_identifier["Sword.w3d"].kind, "attached-model")
        self.assertEqual(
            by_identifier["ReplacementModel"].physical_virtual_paths,
            ("art/w3d/replacement.w3d",),
        )
        self.assertEqual(by_identifier["RandomA.tga"].usage, "random-texture")
        self.assertEqual(by_identifier["Upgrade.tga"].usage, "upgrade-texture")
        self.assertEqual(
            by_identifier["textures\\Quoted.tga"].physical_virtual_paths,
            ("textures/Quoted.tga",),
        )
        self.assertEqual(by_identifier["Dust.tga"].kind, "particle")
        state = graph.objects[0].draw_modules[0].states[0]
        recolour = next(item for item in state.properties if item.key == "RecolorHouse")
        self.assertEqual(recolour.value, "Maybe")
        self.assertIsNone(recolour.enabled)
        self.assertEqual(
            next(item for item in state.properties if item.key == "Shadow").value,
            "SHADOW_ALPHA_DECAL",
        )
        self.assertTrue(graph.complete)

    def test_missing_ambiguous_and_invalid_tokens_remain_explicit_at_source_line(self) -> None:
        documents = {
            "entry.ini": b"""
Object Broken
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = MissingModel
      Model = AmbiguousModel
      Texture = MissingTexture.tga
      AttachedModel = invalid.bmp
      WeatherTexture = SNOWY too-many.tga extra
    End
  End
End
"""
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["Broken"], _index(ambiguous=True), []
        )

        self.assertFalse(graph.complete)
        outcomes = {item.identifier: item for item in graph.unresolved}
        self.assertEqual(outcomes["MissingModel"].status, "missing")
        self.assertEqual(outcomes["AmbiguousModel"].status, "ambiguous")
        self.assertEqual(
            outcomes["AmbiguousModel"].candidates,
            ("duplicate/a.w3d", "duplicate/b.w3d"),
        )
        self.assertEqual(outcomes["MissingTexture.tga"].status, "missing")
        self.assertEqual(outcomes["invalid.bmp"].status, "invalid")
        self.assertEqual(
            outcomes["SNOWY too-many.tga extra"].status, "invalid"
        )
        self.assertEqual(outcomes["MissingModel"].provenance.virtual_path, "entry.ini")
        self.assertEqual(outcomes["MissingModel"].provenance.line, 5)
        with self.assertRaises(TypedVisualGraphError) as caught:
            require_complete_typed_visual_graph(graph)
        self.assertIs(caught.exception.graph, graph)

    def test_inheritance_failures_are_diagnostic_and_never_hide_local_visuals(self) -> None:
        documents = {
            "entry.ini": b"""
ChildObject MissingParent DoesNotExist
  Draw = W3DScriptedModelDraw ModuleTag_Local
    DefaultModelConditionState
      Model = OverlayModel
    End
  End
End
ChildObject CycleA CycleB
End
ChildObject CycleB CycleA
End
"""
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", documents, ["CycleA", "MissingParent"], _index()
        )

        self.assertEqual(
            [(item.object_name, item.code) for item in graph.diagnostics],
            [("CycleA", "inheritance-cycle"), ("MissingParent", "missing-parent")],
        )
        local = next(item for item in graph.objects if item.name == "MissingParent")
        self.assertFalse(local.inheritance_complete)
        self.assertEqual(local.draw_modules[0].instance_tag, "ModuleTag_Local")
        self.assertEqual(
            next(item for item in local.all_references()).physical_virtual_paths,
            ("art/w3d/overlay.w3d",),
        )

    def test_output_is_deterministic_across_target_document_and_catalog_order(self) -> None:
        pairs = [
            ("entry.ini", b'#include "a.ini"\n#include "b.ini"\n'),
            (
                "a.ini",
                b"Object A\n Draw = W3DScriptedModelDraw Tag_A\n  DefaultModelConditionState\n   Model = BaseModel\n   Texture = BaseColor.tga\n  End\n End\nEnd",
            ),
            (
                "b.ini",
                b"Object B\n Draw = W3DScriptedModelDraw Tag_B\n  DefaultModelConditionState\n   Model = OverlayModel\n   Texture = BaseSnow.tga\n  End\n End\nEnd",
            ),
        ]
        catalog = ["z/BaseSnow.tga", "a/BaseColor.tga"]
        first = resolve_typed_visual_documents(
            "entry.ini", pairs, ["B", "A"], _index(), catalog
        )
        second = resolve_typed_visual_documents(
            "ENTRY.INI", list(reversed(pairs)), ["A", "B"], _index(), reversed(catalog)
        )

        self.assertEqual(
            json.dumps(first.neutral(), sort_keys=True, separators=(",", ":")),
            json.dumps(second.neutral(), sort_keys=True, separators=(",", ":")),
        )

    def test_duplicate_or_missing_targets_fail_closed_and_bad_requests_are_rejected(self) -> None:
        duplicates = {
            "entry.ini": b"Object Same\nEnd\nObject Same\nEnd\n"
        }
        graph = resolve_typed_visual_documents(
            "entry.ini", duplicates, ["Same", "Absent"], _index()
        )
        self.assertEqual(
            [(item.object_name, item.code) for item in graph.diagnostics],
            [("Absent", "missing-object"), ("Same", "ambiguous-object")],
        )
        self.assertFalse(graph.objects)
        with self.assertRaisesRegex(ValueError, "duplicate target"):
            resolve_typed_visual_documents(
                "entry.ini", duplicates, ["Same", "same"], _index()
            )
        with self.assertRaisesRegex(ValueError, "at least one"):
            resolve_typed_visual_documents("entry.ini", duplicates, [], _index())

    def test_unresolved_body_markers_are_never_silently_filtered(self) -> None:
        marker = object()
        malformed = SageObject(
            "Object", "Marked", None, (marker,), 0, "entry.ini", 1
        )
        cst = ResolvedSageCst("entry.ini", (), (malformed,), ())

        with self.assertRaisesRegex(
            TypeError, "unexpected SAGE body item type.*assignments/blocks"
        ):
            resolve_typed_visual_graph(cst, ["Marked"], _index())


if __name__ == "__main__":
    unittest.main()
