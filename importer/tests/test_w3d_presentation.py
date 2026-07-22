from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import types
import unittest


def load_adapter_module():
    fake_bpy = types.SimpleNamespace()
    previous = sys.modules.get("bpy")
    sys.modules["bpy"] = fake_bpy
    try:
        path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
        spec = importlib.util.spec_from_file_location("openbfme_test_w3d_to_glb", path)
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load W3D adapter for fixture tests")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            sys.modules.pop("bpy", None)
        else:
            sys.modules["bpy"] = previous
    return module, fake_bpy


ADAPTER, FAKE_BPY = load_adapter_module()


class FakeProperties:
    def __init__(self, **properties):
        self._properties = properties

    def keys(self):
        return self._properties.keys()

    def __getitem__(self, key):
        return self._properties[key]


class FakeAssignment:
    def __init__(self, group: int, weight: float):
        self.group = group
        self.weight = weight


class FakeVertex:
    def __init__(self, assignments: list[FakeAssignment], coordinate=None):
        self.groups = assignments
        if coordinate is not None:
            self.co = coordinate


class FakeVertexGroup:
    def __init__(self, index: int, name: str):
        self.index = index
        self.name = name


class FakeMaterial:
    def __init__(self, name: str, users: int = 1):
        self.name = name
        self.users = users


class FakeMesh(FakeProperties):
    def __init__(
        self,
        name: str,
        *,
        object_type: str | None = "MESH",
        vertex_count: int = 4,
        triangle_count: int = 2,
        group_index: int = 0,
        coordinates: list[tuple[float, float, float]] | None = None,
        materials: list[FakeMaterial] | None = None,
        users: int = 1,
        **properties,
    ):
        super().__init__(**properties)
        self.name = name
        self.object_type = object_type
        if coordinates is None:
            self.vertices = [
                FakeVertex([FakeAssignment(group_index, 1.0)])
                for _ in range(vertex_count)
            ]
        else:
            self.vertices = [
                FakeVertex([FakeAssignment(group_index, 1.0)], coordinate)
                for coordinate in coordinates
            ]
        self.loop_triangles = [object() for _ in range(triangle_count)]
        self.materials = materials or []
        self.users = users

    def calc_loop_triangles(self):
        return None


class FakeObject(FakeProperties):
    def __init__(
        self,
        name: str,
        data: FakeMesh,
        *,
        group_name: str = "B_ROOT",
        parent_bone: str = "",
        skinned: bool = True,
        **properties,
    ):
        super().__init__(**properties)
        self.type = "MESH"
        self.name = name
        self.data = data
        self.vertex_groups = [FakeVertexGroup(0, group_name)]
        self.modifiers = [types.SimpleNamespace(type="ARMATURE")] if skinned else []
        self.parent_type = "BONE" if parent_bone else "OBJECT"
        self.parent_bone = parent_bone


class FakeCollection(list):
    def remove(self, value, do_unlink=False):
        super().remove(value)


class W3dPresentationFixtureTests(unittest.TestCase):
    def test_collision_and_volume_cubes_are_removed_before_export(self) -> None:
        body_mesh = FakeMesh("fighter_geometry", users=1)
        collision_mesh = FakeMesh(
            "opaque_square_geometry", object_type="COLLISION_BOX", users=0
        )
        volume_mesh = FakeMesh("trigger_volume_proxy", object_type="MESH", users=0)
        body = FakeObject("fighter_body", body_mesh)
        collision = FakeObject("opaque_square", collision_mesh, skinned=False)
        volume = FakeObject("trigger_volume_proxy", volume_mesh, skinned=False)
        FAKE_BPY.data = types.SimpleNamespace(
            objects=FakeCollection([body, collision, volume]),
            meshes=FakeCollection([body_mesh, collision_mesh, volume_mesh]),
            materials=FakeCollection([]),
        )

        report = ADAPTER.remove_non_render_geometry()

        self.assertEqual(list(FAKE_BPY.data.objects), [body])
        self.assertEqual(report["count"], 2)
        self.assertEqual(
            {item["reason"] for item in report["reasons"]},
            {"helper-semantic", "non-render-object-type"},
        )
        self.assertNotIn("opaque_square", str(report))

    def test_disabled_proxy_words_in_retail_user_text_do_not_delete_render_mesh(
        self,
    ) -> None:
        waterfall_mesh = FakeMesh(
            "PMWIDEWTRFALL01",
            object_type="MESH",
            vertex_count=152,
            triangle_count=206,
            userText=(
                "Simulation_Geometry = 2\r\n"
                "Proxy_Geometry = <none>\r\n"
                "Use_Display_Proxy = 0\r\n"
                "Disable_Collisions = 0\r\n"
            ),
        )
        waterfall = FakeObject("PMWIDEWTRFALL01", waterfall_mesh, skinned=False)
        FAKE_BPY.data = types.SimpleNamespace(
            objects=FakeCollection([waterfall]),
            meshes=FakeCollection([waterfall_mesh]),
            materials=FakeCollection([]),
        )

        report = ADAPTER.remove_non_render_geometry()

        self.assertEqual(list(FAKE_BPY.data.objects), [waterfall])
        self.assertEqual(report["count"], 0)
        self.assertEqual(report["reasons"], [])

    def test_inventory_distinguishes_right_weapon_and_left_shield(self) -> None:
        body = FakeObject(
            "fighter_body", FakeMesh("fighter_geometry"), group_name="B_ROOT"
        )
        sword = FakeObject(
            "long_sword",
            FakeMesh("weapon_geometry", vertex_count=8, triangle_count=12),
            group_name="B_HAND_R",
        )
        shield = FakeObject(
            "round_shield",
            FakeMesh("shield_geometry", vertex_count=16, triangle_count=18),
            group_name="B_SHIELDBONE",
            parent_bone="B_SHIELDBONE",
        )

        inventory, equipment = ADAPTER.build_mesh_inventory(
            [shield, sword, body], ["right-hand-weapon", "left-hand-shield"]
        )

        self.assertEqual([item["index"] for item in inventory], [0, 1, 2])
        self.assertEqual(
            {item["semantic_role"] for item in inventory},
            {"character-mesh", "right-hand-weapon", "left-hand-shield"},
        )
        self.assertEqual(equipment["right-hand-weapon"]["attachment"], "right-hand")
        self.assertEqual(equipment["left-hand-shield"]["attachment"], "left-hand")
        self.assertIn(
            "dominant-weight-group", equipment["right-hand-weapon"]["proof_methods"]
        )
        self.assertIn("parent-bone", equipment["left-hand-shield"]["proof_methods"])

    def test_unclassified_mesh_and_unproven_equipment_fail_closed(self) -> None:
        unclassified = FakeObject(
            "body", FakeMesh("body_geometry", object_type=None), group_name="B_ROOT"
        )
        with self.assertRaisesRegex(RuntimeError, "could not prove"):
            ADAPTER.build_mesh_inventory([unclassified], [])

        unproven_shield = FakeObject(
            "round_shield", FakeMesh("shield_geometry"), group_name="B_ROOT"
        )
        with self.assertRaisesRegex(RuntimeError, "no proven left-hand"):
            ADAPTER.build_mesh_inventory([unproven_shield], ["left-hand-shield"])

    def test_source_typed_render_cube_survives_as_a_weapon_prop(self) -> None:
        coordinates = [
            (x, y, z) for x in (-1.0, 1.0) for y in (-0.1, 0.1) for z in (-0.1, 0.1)
        ]
        square_weapon = FakeObject(
            "sword",
            FakeMesh(
                "weapon_geometry",
                coordinates=coordinates,
                triangle_count=12,
                materials=[FakeMaterial("metal")],
            ),
            group_name="B_HAND_R",
        )

        inventory, equipment = ADAPTER.build_mesh_inventory(
            [square_weapon], ["right-hand-weapon"]
        )

        self.assertEqual(inventory[0]["semantic_role"], "right-hand-weapon")
        self.assertEqual(equipment["right-hand-weapon"]["mesh_count"], 1)

    def test_preserve_all_does_not_infer_optional_equipment(self) -> None:
        ambiguous = FakeObject(
            "sword_shield",
            FakeMesh("weapon_shield_geometry"),
            group_name="B_ROOT",
            skinned=False,
        )

        inventory, equipment = ADAPTER.build_mesh_inventory([ambiguous], [])

        self.assertEqual(inventory[0]["semantic_role"], "character-mesh")
        self.assertEqual(inventory[0]["attachment"], "scene")
        self.assertEqual(equipment, {})
        self.assertEqual(
            ADAPTER.canonicalize_required_rigid_attachments([ambiguous], [], object()),
            0,
        )

    def test_exact_optional_mesh_exclusion_is_fingerprinted_and_removed(self) -> None:
        body_mesh = FakeMesh("fighter_geometry")
        upgrade_mesh = FakeMesh(
            "upgrade_banner_geometry", vertex_count=6, triangle_count=4
        )
        body = FakeObject("fighter_body", body_mesh)
        upgrade = FakeObject("upgrade_banner", upgrade_mesh)
        FAKE_BPY.data = types.SimpleNamespace(
            objects=FakeCollection([body, upgrade]),
            meshes=FakeCollection([body_mesh, upgrade_mesh]),
            materials=FakeCollection([]),
        )
        expected_geometry_sha256 = ADAPTER._digest_fingerprint_payload(
            ADAPTER._geometry_payload(upgrade)
        )
        expected_materials_sha256 = ADAPTER._digest_fingerprint_payload([])

        report = ADAPTER.exclude_optional_render_meshes(
            [body, upgrade], ["upgrade_banner"], [], None
        )

        self.assertEqual(list(FAKE_BPY.data.objects), [body])
        self.assertEqual(len(report), 1)
        self.assertEqual(report[0]["identifier"], "upgrade_banner")
        self.assertEqual(report[0]["vertices"], 6)
        self.assertEqual(report[0]["triangles"], 4)
        self.assertEqual(report[0]["geometry_sha256"], expected_geometry_sha256)
        self.assertEqual(report[0]["materials_sha256"], expected_materials_sha256)
        self.assertEqual(
            ADAPTER.exclude_optional_render_meshes([body], [], [], None), []
        )

    def test_optional_mesh_exclusions_fail_closed_without_semantic_weakening(
        self,
    ) -> None:
        def install(*items: FakeObject) -> None:
            FAKE_BPY.data = types.SimpleNamespace(
                objects=FakeCollection(items),
                meshes=FakeCollection([item.data for item in items]),
                materials=FakeCollection([]),
            )

        body = FakeObject("fighter_body", FakeMesh("fighter_geometry"))
        install(body)
        with self.assertRaisesRegex(RuntimeError, "matched 0"):
            ADAPTER.exclude_optional_render_meshes([body], ["fighter"], [], None)

        first = FakeObject("upgrade-banner", FakeMesh("first_upgrade_geometry"))
        second = FakeObject("upgrade banner", FakeMesh("second_upgrade_geometry"))
        install(body, first, second)
        with self.assertRaisesRegex(RuntimeError, "matched 2"):
            ADAPTER.exclude_optional_render_meshes(
                [body, first, second], ["upgrade_banner"], [], None
            )
        with self.assertRaisesRegex(ValueError, "contain duplicates"):
            ADAPTER.exclude_optional_render_meshes(
                [body, first],
                ["upgrade_banner", "upgrade_banner"],
                [],
                None,
            )

        sword = FakeObject(
            "upgrade_sword",
            FakeMesh("upgrade_weapon_geometry"),
            group_name="B_HAND_R",
        )
        for required, message in (
            ([], "proven equipment"),
            (["right-hand-weapon"], "required equipment"),
        ):
            with self.subTest(required=required):
                install(body, sword)
                with self.assertRaisesRegex(RuntimeError, message):
                    ADAPTER.exclude_optional_render_meshes(
                        [body, sword], ["upgrade_sword"], required, None
                    )

        install(body)
        with self.assertRaisesRegex(RuntimeError, "last character mesh"):
            ADAPTER.exclude_optional_render_meshes([body], ["fighter_body"], [], None)

        coordinates = [
            (x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)
        ]
        ambiguous_box = FakeObject(
            "upgrade_box",
            FakeMesh(
                "upgrade_box_geometry",
                coordinates=coordinates,
                triangle_count=12,
            ),
        )
        install(body, ambiguous_box)
        report = ADAPTER.exclude_optional_render_meshes(
            [body, ambiguous_box], ["upgrade_box"], [], None
        )
        self.assertEqual(list(FAKE_BPY.data.objects), [body])
        self.assertEqual(report[0]["identifier"], "upgrade_box")

    def test_armature_free_static_mesh_keeps_scene_attachment(self) -> None:
        structure = FakeObject(
            "gondor_barracks",
            FakeMesh("gondor_barracks_geometry"),
            skinned=False,
        )

        inventory, equipment = ADAPTER.build_mesh_inventory([structure], [])

        self.assertEqual(len(inventory), 1)
        self.assertEqual(inventory[0]["semantic_role"], "character-mesh")
        self.assertEqual(inventory[0]["attachment"], "scene")
        self.assertFalse(inventory[0]["skinned"])
        self.assertEqual(equipment, {})

    def test_zero_clip_hierarchical_mesh_keeps_skinning(self) -> None:
        structure = FakeObject(
            "gondor_fortress",
            FakeMesh("gondor_fortress_geometry"),
            skinned=True,
        )

        inventory, equipment = ADAPTER.build_mesh_inventory([structure], [])

        self.assertEqual(len(inventory), 1)
        self.assertEqual(inventory[0]["semantic_role"], "character-mesh")
        self.assertEqual(inventory[0]["attachment"], "skeletal")
        self.assertTrue(inventory[0]["skinned"])
        self.assertEqual(equipment, {})

    def test_hierarchical_request_and_rig_contract_fail_closed(self) -> None:
        ADAPTER.validate_asset_kind_request("hierarchical", [], [])
        ADAPTER.validate_asset_kind_request(
            "hierarchical", [], [], proven_root_rigid_bake=True
        )
        with self.assertRaisesRegex(ValueError, "does not accept animations"):
            ADAPTER.validate_asset_kind_request(
                "hierarchical", [Path("accidental.w3d")], []
            )
        with self.assertRaisesRegex(ValueError, "does not accept required equipment"):
            ADAPTER.validate_asset_kind_request(
                "hierarchical", [], ["right-hand-weapon"]
            )
        with self.assertRaisesRegex(ValueError, "only for hierarchical"):
            ADAPTER.validate_asset_kind_request(
                "static", [], [], proven_root_rigid_bake=True
            )

        FAKE_BPY.data = types.SimpleNamespace(objects=[])
        with self.assertRaisesRegex(RuntimeError, "found 0"):
            ADAPTER.find_single_rig()

        first = types.SimpleNamespace(type="ARMATURE")
        second = types.SimpleNamespace(type="ARMATURE")
        FAKE_BPY.data.objects = [first, second]
        with self.assertRaisesRegex(RuntimeError, "found 2"):
            ADAPTER.find_single_rig()

        FAKE_BPY.data.objects = [first]
        self.assertIs(ADAPTER.find_single_rig(), first)

    def test_proven_root_rigid_bake_preserves_world_transform_and_removes_carrier(
        self,
    ) -> None:
        matrix = [
            [1.0, 0.0, 0.0, 7.0],
            [0.0, 1.0, 0.0, 8.0],
            [0.0, 0.0, 1.0, 9.0],
            [0.0, 0.0, 0.0, 1.0],
        ]
        rig = types.SimpleNamespace(
            type="ARMATURE",
            data=types.SimpleNamespace(bones=[], animation_data=None),
            parent=None,
            parent_type="OBJECT",
            parent_bone="",
            modifiers=[],
            constraints=[],
            animation_data=None,
        )
        mesh = types.SimpleNamespace(
            type="MESH",
            data=types.SimpleNamespace(animation_data=None),
            parent=rig,
            parent_type="ARMATURE",
            parent_bone="",
            modifiers=[],
            vertex_groups=[],
            matrix_world=[row[:] for row in matrix],
            animation_data=None,
        )
        objects = FakeCollection([mesh, rig])
        FAKE_BPY.data = types.SimpleNamespace(objects=objects, actions=[])

        report = ADAPTER.bake_proven_root_rigid_hierarchy(
            "hierarchical", True, rig, [mesh], objects
        )

        self.assertEqual(list(objects), [mesh])
        self.assertIsNone(mesh.parent)
        self.assertEqual(mesh.parent_type, "OBJECT")
        self.assertEqual(mesh.matrix_world, matrix)
        self.assertEqual(
            report,
            {
                "requested": True,
                "applied": True,
                "removed_carriers": 1,
                "baked_meshes": 1,
                "world_transforms_preserved": True,
                "deform_ambiguity_absent": True,
            },
        )

    def test_proven_root_rigid_bake_rejects_missing_opt_in_and_malformed_scene(
        self,
    ) -> None:
        def scene():
            rig = types.SimpleNamespace(
                type="ARMATURE",
                data=types.SimpleNamespace(bones=[], animation_data=None),
                pose=types.SimpleNamespace(bones=[]),
                parent=None,
                parent_type="OBJECT",
                parent_bone="",
                modifiers=[],
                constraints=[],
                animation_data=None,
            )
            mesh = types.SimpleNamespace(
                type="MESH",
                data=types.SimpleNamespace(animation_data=None),
                parent=rig,
                parent_type="ARMATURE",
                parent_bone="",
                modifiers=[],
                vertex_groups=[],
                matrix_world=[
                    [1.0, 0.0, 0.0, 0.0],
                    [0.0, 1.0, 0.0, 0.0],
                    [0.0, 0.0, 1.0, 0.0],
                    [0.0, 0.0, 0.0, 1.0],
                ],
                animation_data=None,
            )
            objects = FakeCollection([mesh, rig])
            FAKE_BPY.data = types.SimpleNamespace(objects=objects, actions=[])
            return rig, mesh, objects

        rig, mesh, objects = scene()
        with self.assertRaisesRegex(RuntimeError, "explicit proven root-rigid"):
            ADAPTER.bake_proven_root_rigid_hierarchy(
                "hierarchical", False, rig, [mesh], objects
            )

        malformed_cases = (
            (
                "foreign-parent",
                lambda rig, mesh, objects: setattr(mesh, "parent", objects[0]),
                "rigidly parented",
            ),
            (
                "vertex-groups",
                lambda rig, mesh, objects: setattr(mesh, "vertex_groups", [object()]),
                "ambiguous deformation",
            ),
            (
                "deforming-modifier",
                lambda rig, mesh, objects: setattr(
                    mesh, "modifiers", [types.SimpleNamespace(type="SUBSURF")]
                ),
                "ambiguous deformation",
            ),
            (
                "bone-constraints",
                lambda rig, mesh, objects: rig.pose.bones.append(
                    types.SimpleNamespace(constraints=[object()])
                ),
                "bone has constraints",
            ),
        )
        for name, mutate, message in malformed_cases:
            with self.subTest(name=name):
                rig, mesh, objects = scene()
                mutate(rig, mesh, objects)
                with self.assertRaisesRegex(RuntimeError, message):
                    ADAPTER.bake_proven_root_rigid_hierarchy(
                        "hierarchical", True, rig, [mesh], objects
                    )

        rig, mesh, objects = scene()
        FAKE_BPY.data.actions = [object()]
        with self.assertRaisesRegex(RuntimeError, "contains animation actions"):
            ADAPTER.bake_proven_root_rigid_hierarchy(
                "hierarchical", True, rig, [mesh], objects
            )

    def test_proven_root_rigid_bake_accepts_rigid_multi_pivot_carrier(self) -> None:
        def scene():
            bones = [
                types.SimpleNamespace(name="ROOT", constraints=[]),
                types.SimpleNamespace(name="DOOR", constraints=[]),
            ]
            rig = types.SimpleNamespace(
                type="ARMATURE",
                data=types.SimpleNamespace(bones=bones, animation_data=None),
                pose=types.SimpleNamespace(bones=bones),
                parent=None,
                parent_type="OBJECT",
                parent_bone="",
                modifiers=[],
                constraints=[],
                animation_data=None,
            )

            def mesh(parent, parent_type, parent_bone, modifiers, offset):
                return types.SimpleNamespace(
                    type="MESH",
                    data=types.SimpleNamespace(animation_data=None),
                    parent=parent,
                    parent_type=parent_type,
                    parent_bone=parent_bone,
                    modifiers=modifiers,
                    vertex_groups=[],
                    matrix_world=[
                        [1.0, 0.0, 0.0, offset],
                        [0.0, 1.0, 0.0, 0.0],
                        [0.0, 0.0, 1.0, 0.0],
                        [0.0, 0.0, 0.0, 1.0],
                    ],
                    animation_data=None,
                )

            bone_parented = mesh(
                rig,
                "BONE",
                "DOOR",
                [],
                1.0,
            )
            carrier_parented = mesh(
                rig,
                "ARMATURE",
                "",
                [types.SimpleNamespace(type="ARMATURE")],
                2.0,
            )
            unparented = mesh(None, "OBJECT", "", [], 3.0)
            meshes = [bone_parented, carrier_parented, unparented]
            objects = FakeCollection([*meshes, rig])
            FAKE_BPY.data = types.SimpleNamespace(objects=objects, actions=[])
            return rig, meshes, objects

        rig, meshes, objects = scene()
        report = ADAPTER.bake_proven_root_rigid_hierarchy(
            "hierarchical", True, rig, meshes, objects
        )

        self.assertTrue(report["applied"])
        self.assertEqual(report["removed_carriers"], 1)
        self.assertEqual(report["baked_meshes"], 3)
        for item, offset in zip(meshes, (1.0, 2.0, 3.0)):
            self.assertIsNone(item.parent)
            self.assertEqual(item.parent_type, "OBJECT")
            self.assertEqual(item.parent_bone, "")
            self.assertEqual(item.modifiers, [])
            self.assertEqual(item.matrix_world[0][3], offset)
        self.assertNotIn(rig, list(objects))
        self.assertEqual(list(objects), meshes)

    def test_hierarchical_scene_rejects_accidental_actions(self) -> None:
        FAKE_BPY.data = types.SimpleNamespace(objects=[], actions=[object()])
        with self.assertRaisesRegex(RuntimeError, "contains animation actions"):
            ADAPTER.assert_non_animated_scene_has_no_actions("hierarchical")

        owner = types.SimpleNamespace(
            type="ARMATURE",
            data=types.SimpleNamespace(animation_data=None),
            animation_data=types.SimpleNamespace(action=object()),
        )
        FAKE_BPY.data = types.SimpleNamespace(objects=[owner], actions=[])
        with self.assertRaisesRegex(RuntimeError, "contains animation actions"):
            ADAPTER.assert_non_animated_scene_has_no_actions("hierarchical")

    def test_static_rig_check_fails_closed(self) -> None:
        FAKE_BPY.data = types.SimpleNamespace(
            objects=[types.SimpleNamespace(type="MESH")]
        )
        self.assertIsNone(ADAPTER.find_static_rig())

        FAKE_BPY.data.objects.append(types.SimpleNamespace(type="ARMATURE"))
        with self.assertRaisesRegex(RuntimeError, "must be armature-free"):
            ADAPTER.find_static_rig()


if __name__ == "__main__":
    unittest.main()
