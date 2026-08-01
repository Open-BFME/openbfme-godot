from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest


def load_adapter_module():
    fake_bpy = types.SimpleNamespace()
    previous = sys.modules.get("bpy")
    sys.modules["bpy"] = fake_bpy
    try:
        path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
        spec = importlib.util.spec_from_file_location(
            "openbfme_test_w3d_to_glb_fingerprints", path
        )
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load W3D adapter fixture")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            sys.modules.pop("bpy", None)
        else:
            sys.modules["bpy"] = previous
    return module


ADAPTER = load_adapter_module()


class FakeProperties:
    def __init__(self, **properties):
        self._properties = properties

    def keys(self):
        return self._properties.keys()

    def __getitem__(self, key):
        return self._properties[key]

    def __setitem__(self, key, value):
        self._properties[key] = value


class FakeAssignment:
    def __init__(self, group: int, weight: float):
        self.group = group
        self.weight = weight


class FakeVertex:
    def __init__(self, coordinate, group: int = 0, weight: float = 1.0):
        self.co = tuple(coordinate)
        self.normal = (0.0, 0.0, 1.0)
        self.groups = [FakeAssignment(group, weight)]


class FakeVertexGroup:
    def __init__(self, index: int, name: str):
        self.index = index
        self.name = name
        self.lock_weight = False


class FakeTriangle:
    def __init__(self, vertices):
        self.vertices = tuple(vertices)
        self.loops = tuple(vertices)
        self.material_index = 0


class FakeMaterial(FakeProperties):
    def __init__(self, name: str):
        super().__init__()
        self.name = name
        self.diffuse_color = [0.4, 0.5, 0.6, 1.0]
        self.metallic = 0.2
        self.roughness = 0.7
        self.use_nodes = False
        self.node_tree = None
        self.blend_method = "OPAQUE"


class FakeShaderProperty:
    def __init__(self, name: str, value, *, property_type: int = 7):
        self.name = name
        self.value = value
        self.type = property_type


class FakePixels(list):
    def __init__(self, values, write_transform=None):
        super().__init__(values)
        self.write_transform = write_transform

    def foreach_set(self, values):
        if self.write_transform is None:
            self[:] = values
        else:
            self[:] = [self.write_transform(value) for value in values]


class FakeImage:
    def __init__(self, pixels, *, users: int = 1, write_transform=None):
        self.pixels = FakePixels(pixels, write_transform=write_transform)
        self.users = users
        self.updated = False
        self.write_transform = write_transform

    def copy(self):
        return FakeImage(
            list(self.pixels), users=1, write_transform=self.write_transform
        )

    def update(self):
        self.updated = True


class FakeSocket:
    _next_pointer = 1

    def __init__(self, name: str, node, *, pointer: int | None = None):
        self.name = name
        self.node = node
        self.default_value = 0.0
        if pointer is None:
            pointer = FakeSocket._next_pointer
            FakeSocket._next_pointer += 1
        self._pointer = pointer

    def as_pointer(self):
        return self._pointer

    def wrapper(self):
        return FakeSocket(self.name, self.node, pointer=self._pointer)


class FakeSocketCollection(dict):
    def __init__(self, *args, wrapped_get: bool = False, **kwargs):
        super().__init__(*args, **kwargs)
        self.wrapped_get = wrapped_get

    def get(self, key, default=None):
        value = super().get(key, default)
        if self.wrapped_get and isinstance(value, FakeSocket):
            return value.wrapper()
        return value


class FakeNode:
    def __init__(self, node_type: str, *, image=None):
        self.type = node_type
        self.image = image
        self.inputs = {}
        self.outputs = {}
        if node_type == "TEX_IMAGE":
            self.outputs = FakeSocketCollection(
                {name: FakeSocket(name, self) for name in ("Color", "Alpha")}
            )
        elif node_type == "BSDF_PRINCIPLED":
            self.inputs = FakeSocketCollection(
                {name: FakeSocket(name, self) for name in ("Base Color", "Alpha")}
            )


class FakeLink:
    def __init__(self, from_socket: FakeSocket, to_socket: FakeSocket):
        self.from_socket = from_socket
        self.to_socket = to_socket
        self.from_node = from_socket.node
        self.to_node = to_socket.node


class FakeLinks(list):
    def new(self, from_socket: FakeSocket, to_socket: FakeSocket):
        link = FakeLink(from_socket, to_socket)
        self.append(link)
        return link


class FakeNodeMaterial(FakeProperties):
    def __init__(self, image: FakeImage, *, shader_properties=None, **properties):
        super().__init__(**properties)
        self.name = "fixture_additive_material"
        self.use_nodes = True
        if shader_properties is not None:
            self.shader = types.SimpleNamespace(**shader_properties)
        self.image_node = FakeNode("TEX_IMAGE", image=image)
        self.principled = FakeNode("BSDF_PRINCIPLED")
        links = FakeLinks()
        links.new(
            self.image_node.outputs["Color"],
            self.principled.inputs["Base Color"],
        )
        self.node_tree = types.SimpleNamespace(
            nodes=[self.image_node, self.principled], links=links
        )


class FakeMesh(FakeProperties):
    def __init__(self, name: str, material: FakeMaterial):
        super().__init__()
        self.name = name
        self.object_type = "MESH"
        self.vertices = [
            FakeVertex((0.0, 0.0, 0.0)),
            FakeVertex((1.0, 0.0, 0.0)),
            FakeVertex((0.0, 1.0, 0.0)),
            FakeVertex((0.0, 0.0, 1.0)),
        ]
        self.edges = []
        self.polygons = []
        self.loop_triangles = [FakeTriangle((0, 1, 2)), FakeTriangle((0, 2, 3))]
        self.uv_layers = []
        self.materials = [material]

    def calc_loop_triangles(self):
        return None


class FakeParent:
    pass


class FakeVector:
    def __init__(self, x: float, y: float, z: float):
        self.values = (float(x), float(y), float(z))

    def __getitem__(self, index):
        return self.values[index]

    def __iter__(self):
        return iter(self.values)

    def copy(self):
        return FakeVector(*self.values)

    def __add__(self, other):
        return FakeVector(*(self[index] + other[index] for index in range(3)))

    def __sub__(self, other):
        return FakeVector(*(self[index] - other[index] for index in range(3)))

    def __mul__(self, value):
        return FakeVector(*(self[index] * float(value) for index in range(3)))

    def __truediv__(self, value):
        return FakeVector(*(self[index] / float(value) for index in range(3)))

    @property
    def length(self):
        return sum(value * value for value in self.values) ** 0.5


class FakeMatrix:
    def __init__(self, rows):
        self.rows = tuple(tuple(float(value) for value in row) for row in rows)

    def __iter__(self):
        return iter(self.rows)

    def __eq__(self, other):
        return isinstance(other, FakeMatrix) and self.rows == other.rows

    def copy(self):
        return FakeMatrix(self.rows)

    def to_list(self):
        return [list(row) for row in self.rows]

    def __matmul__(self, point):
        x, y, z = (float(point[index]) for index in range(3))
        source = (x, y, z, 1.0)
        return FakeVector(
            *(
                sum(self.rows[row][column] * source[column] for column in range(4))
                for row in range(3)
            )
        )


class FakeBone:
    def __init__(self, name: str, position):
        self.name = name
        self.head_local = FakeVector(*position)
        self.tail_local = FakeVector(position[0], position[1] + 0.1, position[2])


class FakeRig:
    def __init__(self, bone_specs):
        bones = []
        for value in bone_specs:
            if isinstance(value, tuple):
                name, position = value
            else:
                name = value
                position = (4.25, 5.25, 6.25)
            bones.append(FakeBone(name, position))
        self.data = types.SimpleNamespace(bones=bones)
        self.matrix_world = FakeMatrix(
            (
                (1.0, 0.0, 0.0, 0.0),
                (0.0, 1.0, 0.0, 0.0),
                (0.0, 0.0, 1.0, 0.0),
                (0.0, 0.0, 0.0, 1.0),
            )
        )


DEFAULT_PARENT = object()


class FakeObject(FakeProperties):
    def __init__(
        self, name: str = "fixture_sword", parent=DEFAULT_PARENT, skinned: bool = True
    ):
        super().__init__()
        self.type = "MESH"
        self.name = name
        geometry_name = (
            "fixture_shield_geometry" if "shield" in name else "fixture_weapon_geometry"
        )
        self.data = FakeMesh(geometry_name, FakeMaterial("fixture_metal"))
        # The root-only weights make the parent bone the sole attachment proof.
        self.vertex_groups = [FakeVertexGroup(0, "B_ROOT")] if skinned else []
        self.modifiers = (
            [
                types.SimpleNamespace(
                    name="fixture_armature", type="ARMATURE", show_render=True
                )
            ]
            if skinned
            else []
        )
        self.parent = FakeParent() if parent is DEFAULT_PARENT else parent
        self.parent_type = "BONE" if self.parent is not None else "OBJECT"
        self.parent_bone = "B_HAND_R" if self.parent is not None else ""
        self.parent_inverse_round_trip_drift = 0.0
        self.local_round_trip_drift = 0.0
        self.matrix_parent_inverse = (
            (1.0, 0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0, 0.0),
            (0.0, 0.0, 1.0, 0.0),
            (0.0, 0.0, 0.0, 1.0),
        )
        self.matrix_basis = (
            (1.0, 0.0, 0.0, 0.25),
            (0.0, 1.0, 0.0, 0.5),
            (0.0, 0.0, 1.0, 0.75),
            (0.0, 0.0, 0.0, 1.0),
        )
        self.matrix_world = FakeMatrix(
            (
                (1.0, 0.0, 0.0, 4.0),
                (0.0, 1.0, 0.0, 5.0),
                (0.0, 0.0, 1.0, 6.0),
                (0.0, 0.0, 0.0, 1.0),
            )
        )

    @staticmethod
    def _with_drift(value, drift: float):
        rows = [list(row) for row in value]
        if rows and rows[0]:
            rows[0][0] = float(rows[0][0]) + drift
        return tuple(tuple(row) for row in rows)

    @property
    def matrix_parent_inverse(self):
        return self._matrix_parent_inverse

    @matrix_parent_inverse.setter
    def matrix_parent_inverse(self, value):
        self._matrix_parent_inverse = self._with_drift(
            value, self.parent_inverse_round_trip_drift
        )

    @property
    def matrix_basis(self):
        return self._matrix_basis

    @matrix_basis.setter
    def matrix_basis(self, value):
        self._matrix_basis = self._with_drift(value, self.local_round_trip_drift)


def capture_one(item: FakeObject):
    inventory, equipment = ADAPTER.build_mesh_inventory([item], ["right-hand-weapon"])
    return inventory, equipment, ADAPTER.capture_render_geometry_proof([item])


class W3dAdditiveMaterialTests(unittest.TestCase):
    def test_zero_source_alpha_does_not_erase_proven_additive_rgb(self) -> None:
        source_pixels = [
            0.0,
            0.0,
            0.0,
            0.0,
            0.8,
            0.4,
            0.2,
            0.0,
        ]
        image = FakeImage(source_pixels)
        material = FakeNodeMaterial(
            image,
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        converted = list(material.image_node.image.pixels)
        self.assertEqual(converted[:4], [0.0, 0.0, 0.0, 0.0])
        self.assertAlmostEqual(converted[4], 1.0)
        self.assertAlmostEqual(converted[5], 0.5)
        self.assertAlmostEqual(converted[6], 0.25)
        self.assertAlmostEqual(converted[7], 0.8)
        for channel in range(3):
            self.assertAlmostEqual(
                converted[4 + channel] * converted[7],
                source_pixels[4 + channel],
            )

    def test_exact_additive_shader_converts_shared_image_and_connects_alpha(
        self,
    ) -> None:
        source_pixels = [
            0.0,
            0.0,
            0.0,
            1.0,
            0.02,
            0.01,
            0.0,
            1.0,
            0.8,
            0.4,
            0.2,
            1.0,
        ]
        source_image = FakeImage(source_pixels, users=2)
        material = FakeNodeMaterial(
            source_image,
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        self.assertEqual(report["duplicated_images"], 1)
        self.assertGreaterEqual(report["changed_alpha_pixels"], 1)
        self.assertGreaterEqual(report["transparent_pixels"], 1)
        self.assertEqual(list(source_image.pixels), source_pixels)
        self.assertIsNot(material.image_node.image, source_image)
        converted = list(material.image_node.image.pixels)
        self.assertEqual(converted[0:4], [0.0, 0.0, 0.0, 0.0])
        self.assertAlmostEqual(converted[4], 1.0)
        self.assertAlmostEqual(converted[5], 0.5)
        self.assertAlmostEqual(converted[7], 0.02)
        self.assertAlmostEqual(converted[8], 1.0)
        self.assertAlmostEqual(converted[9], 0.5)
        self.assertAlmostEqual(converted[10], 0.25)
        self.assertAlmostEqual(converted[11], 0.8)
        for offset in range(0, len(source_pixels), 4):
            for channel in range(3):
                self.assertAlmostEqual(
                    converted[offset + channel] * converted[offset + 3],
                    source_pixels[offset + channel] * source_pixels[offset + 3],
                )
        alpha_links = [
            link
            for link in material.node_tree.links
            if link.to_socket is material.principled.inputs["Alpha"]
        ]
        self.assertEqual(len(alpha_links), 1)
        self.assertIs(alpha_links[0].from_node, material.image_node)

    def test_unproven_and_non_additive_materials_are_untouched(self) -> None:
        cases = (
            None,
            {"src_blend": "0", "dest_blend": "0"},
        )
        for shader_properties in cases:
            with self.subTest(shader_properties=shader_properties):
                source_pixels = [0.1, 0.2, 0.3, 1.0]
                image = FakeImage(source_pixels, users=2)
                material = FakeNodeMaterial(image, shader_properties=shader_properties)

                report = ADAPTER.convert_proven_additive_materials([material])

                self.assertEqual(report["converted_materials"], 0)
                self.assertIs(material.image_node.image, image)
                self.assertEqual(list(image.pixels), source_pixels)
                self.assertEqual(len(material.node_tree.links), 1)

    def test_incomplete_or_coerced_shader_proof_fails_closed(self) -> None:
        cases = (
            {"src_blend": 1},
            {"src_blend": "01", "dest_blend": "1"},
            {"src_blend": 1.0, "dest_blend": "1"},
            {"src_blend": True, "dest_blend": 1},
        )
        for shader_properties in cases:
            with self.subTest(shader_properties=shader_properties):
                material = FakeNodeMaterial(
                    FakeImage([0.0, 0.0, 0.0, 1.0]),
                    shader_properties=shader_properties,
                )
                with self.assertRaises(RuntimeError):
                    ADAPTER.convert_proven_additive_materials([material])

    def test_one_8bit_step_of_image_round_trip_quantization_is_allowed(self) -> None:
        def quantize_8bit(value):
            return round(float(value) * 255.0) / 255.0

        image = FakeImage(
            [0.0, 0.0, 0.0, 1.0, 0.573, 0.218, 0.091, 1.0],
            write_transform=quantize_8bit,
        )
        material = FakeNodeMaterial(
            image,
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        self.assertTrue(image.updated)
        self.assertLess(image.pixels[3], 1.0)
        self.assertGreater(image.pixels[7], 0.0)

    def test_image_round_trip_error_beyond_one_8bit_step_fails(self) -> None:
        def exceed_tolerance(value):
            return min(1.0, float(value) + (2.0 / 255.0))

        image = FakeImage(
            [0.0, 0.0, 0.0, 1.0, 0.573, 0.218, 0.091, 1.0],
            write_transform=exceed_tolerance,
        )
        material = FakeNodeMaterial(
            image,
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )

        with self.assertRaisesRegex(RuntimeError, "did not round trip"):
            ADAPTER.convert_proven_additive_materials([material])

    def test_direct_image_link_uses_blender_pointer_identity(self) -> None:
        source = FakeImage([0.2, 0.1, 0.0, 1.0])
        material = FakeNodeMaterial(
            source,
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )
        distractor = FakeNode("TEX_IMAGE", image=FakeImage([0.9, 0.8, 0.7, 1.0]))
        material.node_tree.nodes.insert(0, distractor)
        material.principled.inputs.wrapped_get = True
        material.image_node.outputs.wrapped_get = True

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        self.assertIs(material.node_tree.nodes[0], distractor)
        self.assertEqual(list(distractor.image.pixels), [0.9, 0.8, 0.7, 1.0])
        alpha_links = [
            link
            for link in material.node_tree.links
            if ADAPTER._same_runtime_identity(
                link.to_socket, material.principled.inputs["Alpha"]
            )
        ]
        self.assertEqual(len(alpha_links), 1)
        self.assertTrue(
            ADAPTER._same_runtime_identity(
                alpha_links[0].from_socket,
                material.image_node.outputs["Alpha"],
            )
        )

    def test_multiple_images_without_a_direct_color_link_fail_closed(self) -> None:
        material = FakeNodeMaterial(
            FakeImage([0.2, 0.1, 0.0, 1.0]),
            shader_properties={"src_blend": "1", "dest_blend": "1"},
        )
        material.node_tree.nodes.insert(
            0, FakeNode("TEX_IMAGE", image=FakeImage([0.9, 0.8, 0.7, 1.0]))
        )
        material.node_tree.links.clear()

        with self.assertRaisesRegex(RuntimeError, "ambiguous color image"):
            ADAPTER.convert_proven_additive_materials([material])


class W3dOpaqueMaterialNormalizationTests(unittest.TestCase):
    @staticmethod
    def _material(source: str, destination: str, alpha_test: str = "0"):
        material = FakeNodeMaterial(
            FakeImage([0.2, 0.4, 0.6, 0.5]),
            shader_properties={
                "src_blend": source,
                "dest_blend": destination,
                "alpha_test": alpha_test,
            },
        )
        material.node_tree.links.new(
            material.image_node.outputs["Alpha"],
            material.principled.inputs["Alpha"],
        )
        material.blend_method = "CLIP"
        return material

    def test_exact_one_zero_state_disconnects_texture_alpha(self) -> None:
        material = self._material("1", "0")

        report = ADAPTER.normalize_proven_opaque_materials([material])

        self.assertEqual(report["normalized_material_count"], 1)
        self.assertEqual(report["removed_alpha_link_count"], 1)
        self.assertTrue(report["source_blend_state_preserved"])
        self.assertEqual(material.blend_method, "OPAQUE")
        self.assertEqual(material.principled.inputs["Alpha"].default_value, 1.0)
        self.assertEqual(
            [
                link
                for link in material.node_tree.links
                if ADAPTER._same_runtime_identity(
                    link.to_socket, material.principled.inputs["Alpha"]
                )
            ],
            [],
        )

    def test_additive_and_alpha_blended_states_remain_connected(self) -> None:
        additive = self._material("1", "1")
        alpha_blended = self._material("2", "5")

        report = ADAPTER.normalize_proven_opaque_materials(
            [alpha_blended, additive]
        )

        self.assertEqual(report["normalized_material_count"], 0)
        self.assertEqual(len(additive.node_tree.links), 2)
        self.assertEqual(len(alpha_blended.node_tree.links), 2)
        self.assertEqual(additive.blend_method, "CLIP")
        self.assertEqual(alpha_blended.blend_method, "CLIP")

    def test_alpha_tested_one_zero_state_remains_connected(self) -> None:
        material = self._material("1", "0", "1")

        report = ADAPTER.normalize_proven_opaque_materials([material])

        self.assertEqual(report["normalized_material_count"], 0)
        self.assertEqual(len(material.node_tree.links), 2)

    def test_incomplete_source_blend_state_fails_closed(self) -> None:
        material = self._material("1", "0")
        del material.shader.dest_blend

        with self.assertRaisesRegex(RuntimeError, "blend proof is incomplete"):
            ADAPTER.normalize_proven_opaque_materials([material])


class W3dShaderMaterialCompatibilityTests(unittest.TestCase):
    @staticmethod
    def _invoke(properties):
        seen = []
        material = FakeMaterial("fixture_shader")
        principled = object()

        def original(_context, _name, shader_material):
            seen.extend(prop.name for prop in shader_material.properties)
            return material, principled

        wrapped = ADAPTER._shader_material_compatibility_importer(original)
        result = wrapped(
            object(), "fixture", types.SimpleNamespace(properties=properties)
        )
        return seen, material, principled, result

    def test_exact_flags_are_mapped_and_preserved_in_canonical_proof(self) -> None:
        seen, material, principled, result = self._invoke(
            [
                FakeShaderProperty("AlphaBlendingEnable", True),
                FakeShaderProperty("DiffuseTexture", "fixture.dds", property_type=1),
                FakeShaderProperty("FogEnable", False),
            ]
        )

        self.assertEqual(seen, ["DiffuseTexture"])
        self.assertEqual(result, (material, principled))
        self.assertIs(material["openbfme_w3d_alpha_blending_enable"], True)
        self.assertIs(material["openbfme_w3d_fog_enable"], False)
        self.assertEqual(material.blend_method, "BLEND")
        self.assertEqual(
            ADAPTER.collect_shader_material_compatibility([material]),
            {
                "mapped_materials": [
                    {
                        "material": "fixture_shader",
                        "properties": {
                            "AlphaBlendingEnable": True,
                            "FogEnable": False,
                        },
                    }
                ],
                "mapped_material_count": 1,
                "mapped_property_count": 2,
                "alpha_blending_enable_count": 1,
                "fog_enable_count": 1,
                "source_flags_preserved": True,
            },
        )

    def test_false_alpha_flag_maps_to_opaque(self) -> None:
        _seen, material, _principled, _result = self._invoke(
            [FakeShaderProperty("AlphaBlendingEnable", False)]
        )

        self.assertEqual(material.blend_method, "OPAQUE")
        self.assertIs(material["openbfme_w3d_alpha_blending_enable"], False)

    def test_duplicate_or_non_boolean_compatibility_flags_fail_closed(self) -> None:
        cases = (
            [
                FakeShaderProperty("FogEnable", True),
                FakeShaderProperty("FogEnable", False),
            ],
            [FakeShaderProperty("FogEnable", 1)],
            [FakeShaderProperty("FogEnable", True, property_type=6)],
        )
        for properties in cases:
            with self.subTest(properties=properties):
                with self.assertRaises(RuntimeError):
                    self._invoke(properties)

    def test_unknown_shader_property_reaches_the_pinned_importer(self) -> None:
        unknown = FakeShaderProperty("FutureRetailProperty", 3, property_type=6)

        def original(_context, _name, shader_material):
            self.assertEqual(shader_material.properties, [unknown])
            raise RuntimeError("pinned importer rejected unknown property")

        wrapped = ADAPTER._shader_material_compatibility_importer(original)
        with self.assertRaisesRegex(RuntimeError, "rejected unknown property"):
            wrapped(
                object(),
                "fixture",
                types.SimpleNamespace(properties=[unknown]),
            )


class W3dEquipmentRoleProofTests(unittest.TestCase):
    def test_attachment_label_cannot_invent_weapon_role(self) -> None:
        item = FakeObject(name="fixture_geometry")
        item.data.name = "fixture_geometry"
        item.data.materials[0].name = "fixture_metal"
        item.parent_bone = "B_SWORD"

        role, attachment, proof_methods = ADAPTER._equipment_classification(item)

        self.assertEqual(role, "character-mesh")
        self.assertEqual(attachment, "skeletal")
        self.assertEqual(proof_methods, [])

    def test_mesh_or_material_role_still_requires_matching_hand_attachment(
        self,
    ) -> None:
        weapon = FakeObject(name="fixture_sword")
        role, attachment, proofs = ADAPTER._equipment_classification(weapon)
        self.assertEqual((role, attachment), ("right-hand-weapon", "right-hand"))
        self.assertIn("mesh-semantic", proofs)
        self.assertNotIn("attachment-semantic", proofs)

        shield = FakeObject(name="fixture_geometry")
        shield.data.name = "fixture_geometry"
        shield.data.materials[0].name = "fixture_shield_metal"
        shield.parent_bone = "B_HAND_L"
        role, attachment, proofs = ADAPTER._equipment_classification(shield)
        self.assertEqual((role, attachment), ("left-hand-shield", "left-hand"))
        self.assertIn("material-semantic", proofs)
        self.assertNotIn("attachment-semantic", proofs)

    def test_authored_equipment_pivot_proves_attachment_without_inventing_role(
        self,
    ) -> None:
        weapon = FakeObject(name="forged_blade")
        weapon.parent_bone = "FORGED_BLADE"
        role, attachment, proofs = ADAPTER._equipment_classification(weapon)
        self.assertEqual((role, attachment), ("right-hand-weapon", "right-hand"))
        self.assertIn("source-equipment-pivot", proofs)
        self.assertNotIn("parent-bone", proofs)

        shield = FakeObject(name="bat_shield")
        shield.parent_bone = "BAT_SHIELD"
        role, attachment, proofs = ADAPTER._equipment_classification(shield)
        self.assertEqual((role, attachment), ("left-hand-shield", "left-hand"))
        self.assertIn("source-equipment-pivot", proofs)
        self.assertNotIn("parent-bone", proofs)

        untyped = FakeObject(name="fixture_geometry")
        untyped.data.name = "fixture_geometry"
        untyped.data.materials[0].name = "fixture_metal"
        untyped.parent_bone = "BAT_SHIELD"
        role, attachment, proofs = ADAPTER._equipment_classification(untyped)
        self.assertEqual((role, attachment), ("character-mesh", "skeletal"))
        self.assertEqual(proofs, [])

    def test_authored_equipment_pivot_is_not_reparented_to_a_nearby_hand(self) -> None:
        rig = FakeRig(
            [
                ("B_HAND_L", (4.25, 5.25, 6.25)),
                ("BAT_SHIELD", (4.25, 5.25, 6.25)),
                ("B_HAND_R", (20.0, 20.0, 20.0)),
            ]
        )
        shield = FakeObject(name="bat_shield", parent=rig, skinned=False)
        shield.parent_bone = "BAT_SHIELD"
        expected_parent = shield.parent
        expected_world = shield.matrix_world

        promoted = ADAPTER.canonicalize_required_rigid_attachments(
            [shield], ["left-hand-shield"], rig
        )

        self.assertEqual(promoted, 0)
        self.assertIs(shield.parent, expected_parent)
        self.assertEqual(shield.parent_bone, "BAT_SHIELD")
        self.assertEqual(shield.matrix_world, expected_world)

    def test_unproven_weapon_still_fails_with_bounded_sanitized_diagnostics(
        self,
    ) -> None:
        weapon = FakeObject(name="private_retail_sword", parent=None)
        weapon.data.name = "private_retail_weapon_geometry"
        weapon.data.materials[0].name = "private_retail_metal"
        weapon._properties = {"attachment_private": "secret_retail_socket"}

        with self.assertRaises(RuntimeError) as raised:
            ADAPTER._equipment_classification(weapon)

        message = str(raised.exception)
        prefix = "weapon-like render mesh has no proven right-hand attachment: "
        self.assertTrue(message.startswith(prefix))
        self.assertLess(len(message), 768)
        self.assertNotIn("private_retail", message)
        self.assertNotIn("secret_retail_socket", message)
        diagnostics = json.loads(message[len(prefix) :])
        self.assertEqual(
            set(diagnostics),
            {
                "skinned",
                "vertex_group_count",
                "parent_right",
                "parent_left",
                "dominant_right",
                "dominant_left",
                "weighted_right",
                "weighted_left",
                "right_hand_weight_share",
                "left_hand_weight_share",
                "custom_right",
                "custom_left",
                "rig_right_candidate_count",
                "rig_left_candidate_count",
                "rest_pose_attachment",
            },
        )
        self.assertFalse(diagnostics["parent_right"])
        self.assertEqual(diagnostics["right_hand_weight_share"], 0.0)
        self.assertEqual(diagnostics["rest_pose_attachment"], "ambiguous")


class W3dAnimationImportOutputTests(unittest.TestCase):
    @staticmethod
    def _capture_os_output(
        operation,
        *,
        replay_failure: bool = False,
        phase_checkpoint=None,
        operation_phase: str = "animation-import",
        max_bytes: int = ADAPTER.MAX_ANIMATION_IMPORT_CAPTURE_BYTES,
    ):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            stdout_path = root / "stdout.bin"
            stderr_path = root / "stderr.bin"
            binary_flag = getattr(os, "O_BINARY", 0)
            stdout_fd = os.open(
                stdout_path,
                os.O_WRONLY | os.O_CREAT | os.O_TRUNC | binary_flag,
                0o600,
            )
            stderr_fd = os.open(
                stderr_path,
                os.O_WRONLY | os.O_CREAT | os.O_TRUNC | binary_flag,
                0o600,
            )
            saved_stdout = os.dup(1)
            saved_stderr = os.dup(2)
            ledger = None
            try:
                ADAPTER._flush_process_output()
                os.dup2(stdout_fd, 1)
                os.dup2(stderr_fd, 2)
                os.close(stdout_fd)
                os.close(stderr_fd)
                stdout_fd = -1
                stderr_fd = -1

                ledger = ADAPTER.AnimationImportOutputLedger(
                    temp_dir=root,
                    max_bytes=max_bytes,
                )
                result = ledger.capture(
                    operation,
                    operation_phase=operation_phase,
                    phase_checkpoint=phase_checkpoint,
                )
                if replay_failure:
                    ledger.replay_failure()
                    suppressed = None
                else:
                    suppressed = ledger.replay_success()
            finally:
                if ledger is not None and not ledger._finished:
                    ledger.replay_failure()
                ADAPTER._flush_process_output()
                os.dup2(saved_stdout, 1)
                os.dup2(saved_stderr, 2)
                os.close(saved_stdout)
                os.close(saved_stderr)
                if stdout_fd >= 0:
                    os.close(stdout_fd)
                if stderr_fd >= 0:
                    os.close(stderr_fd)
            return (
                result,
                suppressed,
                stdout_path.read_bytes(),
                stderr_path.read_bytes(),
            )

    def test_exact_warning_lines_are_the_only_matches(self) -> None:
        warning = ADAPTER.REDUNDANT_KEYFRAME_WARNING
        payload = b"before\n" + warning + b"\n" + warning + b"\r\nafter\n"

        filtered, suppressed = ADAPTER.filter_redundant_keyframe_warning_bytes(payload)

        self.assertEqual(filtered, b"before\nafter\n")
        self.assertEqual(suppressed, 2)

    def test_near_matches_and_other_blender_warnings_are_preserved(self) -> None:
        warning = ADAPTER.REDUNDANT_KEYFRAME_WARNING
        gltf_warning = (
            b"WARNING: Baking animation because the number of keyframes is not "
            b"equal for all channel tracks"
        )
        payload = b"".join(
            (
                b" " + warning + b"\n",
                warning + b" \n",
                warning.replace(b"1 keyframe", b"2 keyframe") + b"\n",
                gltf_warning + b"\n",
                warning,
            )
        )

        filtered, suppressed = ADAPTER.filter_redundant_keyframe_warning_bytes(payload)

        self.assertEqual(filtered, payload)
        self.assertEqual(suppressed, 0)

    def test_success_filters_os_descriptor_output_and_reports_count(self) -> None:
        warning = ADAPTER.REDUNDANT_KEYFRAME_WARNING
        gltf_warning = (
            b"WARNING: Baking animation because the number of keyframes is not "
            b"equal for all channel tracks\n"
        )

        def operation():
            os.write(1, b"stdout-before\n" + warning + b"\n" + gltf_warning)
            os.write(2, warning + b"\r\nstderr-after\n")
            return {"FINISHED"}

        result, suppressed, stdout, stderr = self._capture_os_output(operation)

        self.assertEqual(result, {"FINISHED"})
        self.assertEqual(suppressed, 2)
        self.assertEqual(stdout, b"stdout-before\n" + gltf_warning)
        self.assertEqual(stderr, b"stderr-after\n")

    def test_capture_checkpoints_cover_success_and_accounting_failure(self) -> None:
        class Checkpoint:
            def __init__(self) -> None:
                self.phases: list[str] = []

            def set(self, phase: str) -> None:
                self.phases.append(phase)

        success = Checkpoint()
        result, _suppressed, _stdout, _stderr = self._capture_os_output(
            lambda: {"FINISHED"},
            phase_checkpoint=success,
        )
        self.assertEqual(result, {"FINISHED"})
        self.assertEqual(
            success.phases,
            [
                "animation-output-capture-setup",
                "animation-import",
                "animation-output-capture-restore",
                "animation-output-capture-accounting",
            ],
        )

        accounting_failure = Checkpoint()
        with self.assertRaisesRegex(RuntimeError, "bounded job capture"):
            self._capture_os_output(
                lambda: os.write(1, b"too large"),
                phase_checkpoint=accounting_failure,
                max_bytes=1,
            )
        self.assertEqual(
            accounting_failure.phases[-1],
            "animation-output-capture-accounting",
        )

    def test_capture_does_not_overwrite_operation_failure_phase(self) -> None:
        class Checkpoint:
            def __init__(self) -> None:
                self.phases: list[str] = []

            def set(self, phase: str) -> None:
                self.phases.append(phase)

        checkpoint = Checkpoint()
        secret = "PRIVATE_CAPTURE_OPERATION_FAILURE"

        def operation():
            checkpoint.set("model-animation-keyframe-write")
            raise RuntimeError(secret)

        with self.assertRaisesRegex(RuntimeError, secret):
            self._capture_os_output(
                operation,
                phase_checkpoint=checkpoint,
            )
        self.assertEqual(
            checkpoint.phases,
            [
                "animation-output-capture-setup",
                "animation-import",
                "model-animation-keyframe-write",
            ],
        )

    def test_later_adapter_failure_replays_every_raw_warning_byte(self) -> None:
        warning = ADAPTER.REDUNDANT_KEYFRAME_WARNING
        expected_stdout = b"stdout-before\n" + warning + b"\nstdout-after\n"
        expected_stderr = warning + b"\r\nstderr-after\n"

        def operation():
            os.write(1, expected_stdout)
            os.write(2, expected_stderr)
            return {"FINISHED"}

        result, suppressed, stdout, stderr = self._capture_os_output(
            operation, replay_failure=True
        )

        self.assertEqual(result, {"FINISHED"})
        self.assertIsNone(suppressed)
        # Capture compaction strips only the byte-exact redundant keyframe
        # warning class (its count is tracked for the success report); every
        # other byte survives failure replay exactly.
        self.assertEqual(stdout, b"stdout-before\nstdout-after\n")
        self.assertEqual(stderr, b"stderr-after\n")


class W3dAnimationGeometryProofTests(unittest.TestCase):
    @staticmethod
    def _fake_action(name: str, key_count: int = 1):
        points = [
            types.SimpleNamespace(
                co=(float(index), float(index)),
                interpolation="LINEAR",
            )
            for index in range(key_count)
        ]
        curve = types.SimpleNamespace(
            array_index=0,
            data_path="location",
            keyframe_points=points,
        )
        return types.SimpleNamespace(name=name, fcurves=[curve], use_fake_user=False)

    def test_embedded_model_animation_requires_exact_active_split_pair(self) -> None:
        object_action = self._fake_action("door")
        data_action = self._fake_action("doorAction.001", 2)
        rig = types.SimpleNamespace(
            animation_data=types.SimpleNamespace(action=object_action),
            data=types.SimpleNamespace(
                animation_data=types.SimpleNamespace(action=data_action)
            ),
        )

        captured = ADAPTER.capture_split_w3d_animation_actions(
            rig, [data_action, object_action]
        )

        self.assertEqual(captured, [object_action, data_action])
        self.assertTrue(object_action.use_fake_user)
        self.assertTrue(data_action.use_fake_user)

        rig.data.animation_data.action = None
        with self.assertRaisesRegex(RuntimeError, "outside its proven owner set"):
            ADAPTER.capture_split_w3d_animation_actions(
                rig, [object_action, data_action]
            )

        rig.data.animation_data.action = data_action
        with self.assertRaisesRegex(RuntimeError, "outside its proven owner set"):
            ADAPTER.capture_split_w3d_animation_actions(
                rig, [object_action, data_action, self._fake_action("extra")]
            )

        empty_action = self._fake_action("empty", 0)
        rig.data.animation_data.action = empty_action
        with self.assertRaisesRegex(RuntimeError, "no keyed curves"):
            ADAPTER.capture_split_w3d_animation_actions(
                rig, [object_action, empty_action]
            )
        rig.data.animation_data.action = object_action
        with self.assertRaisesRegex(RuntimeError, "not distinct"):
            ADAPTER.capture_split_w3d_animation_actions(
                rig, [object_action, object_action]
            )

    def test_embedded_animation_glb_proof_requires_one_named_nonempty_clip(
        self,
    ) -> None:
        document = {
            "asset": {"version": "2.0"},
            "nodes": [{"mesh": 0, "skin": 0}],
            "skins": [{"joints": [0]}],
            "animations": [
                {
                    "name": "GBFDOOR_DRC",
                    "channels": [
                        {
                            "sampler": 0,
                            "target": {"node": 0, "path": "rotation"},
                        }
                    ],
                    "samplers": [{"input": 0, "output": 1}],
                }
            ],
        }

        def glb_bytes(value):
            payload = json.dumps(value, separators=(",", ":")).encode("utf-8")
            payload += b" " * ((-len(payload)) % 4)
            total = 12 + 8 + len(payload)
            return (
                struct.pack("<4sII", b"glTF", 2, total)
                + struct.pack("<II", len(payload), 0x4E4F534A)
                + payload
            )

        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "door.glb"
            path.write_bytes(glb_bytes(document))
            report = ADAPTER.validate_embedded_animation_glb(path, "gbfdoor_drc")
            self.assertEqual(
                report,
                {
                    "animations": 1,
                    "channels": 1,
                    "samplers": 1,
                    "skins": 1,
                    "skeletal_meshes": 1,
                    "visibility_channels": 0,
                    "visibility_keys": 0,
                    "visibility_only_animations": 0,
                },
            )

            document["animations"][0]["name"] = "wrong"
            path.write_bytes(glb_bytes(document))
            with self.assertRaisesRegex(RuntimeError, "name changed"):
                ADAPTER.validate_embedded_animation_glb(path, "gbfdoor_drc")

    def test_multiple_left_candidates_choose_materially_nearest_without_world_drift(
        self,
    ) -> None:
        rig = FakeRig(
            [
                ("B_HAND_L", (4.25, 5.25, 6.25)),
                ("L_HAND", (8.0, 8.0, 8.0)),
                ("B_HAND_R", (20.0, 20.0, 20.0)),
            ]
        )
        item = FakeObject(name="fixture_shield", parent=None, skinned=False)
        expected_world = item.matrix_world
        promoted = ADAPTER.canonicalize_required_rigid_attachments(
            [item], ["left-hand-shield"], rig
        )

        self.assertEqual(promoted, 1)
        self.assertIs(item.parent, rig)
        self.assertEqual(item.parent_type, "BONE")
        self.assertEqual(item.parent_bone, "B_HAND_L")
        self.assertEqual(item.matrix_world, expected_world)
        inventory, equipment = ADAPTER.build_mesh_inventory(
            [item], ["left-hand-shield"], rig
        )
        self.assertEqual(inventory[0]["attachment"], "left-hand")
        self.assertIn("parent-bone", inventory[0]["proof_methods"])
        self.assertEqual(equipment["left-hand-shield"]["mesh_count"], 1)

    def test_unique_explicit_shield_bone_outranks_colocated_generic_hand(self) -> None:
        rig = FakeRig(
            [
                ("B_HAND_L", (4.25, 5.25, 6.25)),
                ("B_SHIELD", (4.25, 5.25, 6.25)),
                ("B_HAND_R", (20.0, 20.0, 20.0)),
            ]
        )
        item = FakeObject(name="fixture_shield", parent=None, skinned=False)

        promoted = ADAPTER.canonicalize_required_rigid_attachments(
            [item], ["left-hand-shield"], rig
        )

        self.assertEqual(promoted, 1)
        self.assertIs(item.parent, rig)
        self.assertEqual(item.parent_bone, "B_SHIELD")

        # A source-named required rigid shield with no parent, weight, custom,
        # or rest-pose attachment proof may still use the one canonical left
        # hand bone. The post-promotion inventory must prove the exact parent.
        midpoint_rig = FakeRig(
            [
                ("B_SHIELD", (0.0, 0.0, 0.0)),
                ("B_HAND_R", (8.5, 10.5, 12.5)),
            ]
        )
        midpoint_item = FakeObject(name="fixture_shield", parent=None, skinned=False)
        midpoint_world = midpoint_item.matrix_world
        self.assertEqual(
            ADAPTER._rest_pose_hand_attachment(midpoint_item, midpoint_rig), ""
        )
        self.assertEqual(
            ADAPTER.canonicalize_required_rigid_attachments(
                [midpoint_item], ["left-hand-shield"], midpoint_rig
            ),
            1,
        )
        self.assertEqual(midpoint_item.parent_bone, "B_SHIELD")
        self.assertEqual(midpoint_item.matrix_world, midpoint_world)
        midpoint_inventory, midpoint_equipment = ADAPTER.build_mesh_inventory(
            [midpoint_item], ["left-hand-shield"], midpoint_rig
        )
        self.assertIn("parent-bone", midpoint_inventory[0]["proof_methods"])
        self.assertEqual(midpoint_equipment["left-hand-shield"]["mesh_count"], 1)

    def test_ambiguous_or_skinned_rest_only_attachment_is_rejected(self) -> None:
        ambiguous = FakeObject(name="fixture_shield", parent=None, skinned=False)
        near_tie_rig = FakeRig(
            [
                ("B_SHIELD", (4.20, 5.25, 6.25)),
                ("SHIELDBONE", (4.30, 5.25, 6.25)),
                ("B_HAND_R", (20.0, 20.0, 20.0)),
            ]
        )
        with self.assertRaisesRegex(RuntimeError, "not materially separated"):
            ADAPTER.canonicalize_required_rigid_attachments(
                [ambiguous], ["left-hand-shield"], near_tie_rig
            )
        self.assertIsNone(ambiguous.parent)

        skinned = FakeObject(name="fixture_shield", parent=None, skinned=True)
        unique_rig = FakeRig(
            [
                ("B_HAND_L", (4.25, 5.25, 6.25)),
                ("B_HAND_R", (20.0, 20.0, 20.0)),
            ]
        )
        with self.assertRaisesRegex(RuntimeError, "skinned equipment"):
            ADAPTER.canonicalize_required_rigid_attachments(
                [skinned], ["left-hand-shield"], unique_rig
            )
        self.assertIsNone(skinned.parent)

    def test_cleared_bone_parent_is_restored_before_semantic_revalidation(self) -> None:
        item = FakeObject()
        inventory, equipment, proof = capture_one(item)
        attachment_proof = ADAPTER.capture_render_attachment_proof([item])
        expected_parent = item.parent
        expected_parent_inverse = item.matrix_parent_inverse
        expected_local_transform = item.matrix_basis
        repeated = ADAPTER.capture_render_geometry_proof([item])
        self.assertEqual(proof[0]["fingerprints"], repeated[0]["fingerprints"])
        self.assertEqual(inventory[0]["semantic_role"], "right-hand-weapon")
        self.assertEqual(equipment["right-hand-weapon"]["attachment"], "right-hand")

        item.parent = None
        item.parent_type = "OBJECT"
        item.parent_bone = ""
        item.matrix_parent_inverse = ((0.0,),)
        item.matrix_basis = ((0.0,),)

        with self.assertRaisesRegex(RuntimeError, "no proven right-hand"):
            ADAPTER.build_mesh_inventory([item], ["right-hand-weapon"])
        ADAPTER.restore_render_attachments(
            attachment_proof, [item], [item, expected_parent]
        )
        self.assertIs(item.parent, expected_parent)
        self.assertEqual(item.parent_type, "BONE")
        self.assertEqual(item.parent_bone, "B_HAND_R")
        self.assertEqual(item.matrix_parent_inverse, expected_parent_inverse)
        self.assertEqual(item.matrix_basis, expected_local_transform)
        ADAPTER.assert_render_geometry_unchanged(proof, [item])
        ADAPTER.revalidate_restored_inventory(
            [item],
            ["right-hand-weapon"],
            None,
            inventory,
            equipment,
        )

    def test_wrong_or_unavailable_parent_cannot_satisfy_restoration(self) -> None:
        expected_parent = FakeParent()
        wrong_parent = FakeParent()
        item = FakeObject(parent=expected_parent)
        attachment_proof = ADAPTER.capture_render_attachment_proof([item])
        item.parent = wrong_parent
        item.parent_type = "BONE"
        item.parent_bone = "B_HAND_L"

        with self.assertRaisesRegex(RuntimeError, "parent is unavailable"):
            ADAPTER.restore_render_attachments(
                attachment_proof, [item], [item, wrong_parent]
            )
        self.assertIs(item.parent, wrong_parent)

    def test_harmless_matrix_round_trip_drift_is_tolerated(self) -> None:
        item = FakeObject()
        attachment_proof = ADAPTER.capture_render_attachment_proof([item])
        expected_parent = item.parent
        item.parent_inverse_round_trip_drift = 0.5e-6
        item.local_round_trip_drift = -0.5e-6
        item.parent = None
        item.parent_type = "OBJECT"
        item.parent_bone = ""
        item.matrix_parent_inverse = ((0.0, 0.0, 0.0, 0.0),) * 4
        item.matrix_basis = ((0.0, 0.0, 0.0, 0.0),) * 4

        ADAPTER.restore_render_attachments(
            attachment_proof, [item], [item, expected_parent]
        )
        self.assertTrue(
            ADAPTER._private_transforms_close(
                item.matrix_parent_inverse,
                attachment_proof[0]["parent_inverse"],
            )
        )
        self.assertTrue(
            ADAPTER._private_transforms_close(
                item.matrix_basis,
                attachment_proof[0]["local_transform"],
            )
        )

    def test_material_matrix_mismatch_fails_with_specific_safe_error(self) -> None:
        cases = [
            ("parent-inverse", 2.0e-6, 0.0, "parent-inverse matrix"),
            ("local", 0.0, -2.0e-6, "local transform"),
        ]
        for name, parent_drift, local_drift, message in cases:
            with self.subTest(name=name):
                item = FakeObject()
                attachment_proof = ADAPTER.capture_render_attachment_proof([item])
                expected_parent = item.parent
                item.parent_inverse_round_trip_drift = parent_drift
                item.local_round_trip_drift = local_drift
                item.parent = None
                item.parent_type = "OBJECT"
                item.parent_bone = ""
                with self.assertRaisesRegex(RuntimeError, message):
                    ADAPTER.restore_render_attachments(
                        attachment_proof, [item], [item, expected_parent]
                    )

    def test_post_restoration_semantic_revalidation_catches_mismatch(self) -> None:
        item = FakeObject()
        inventory, equipment, _proof = capture_one(item)
        attachment_proof = ADAPTER.capture_render_attachment_proof([item])
        expected_parent = item.parent
        item.parent = None
        item.parent_type = "OBJECT"
        item.parent_bone = ""
        ADAPTER.restore_render_attachments(
            attachment_proof, [item], [item, expected_parent]
        )

        item.parent_bone = "B_HAND_L"
        with self.assertRaisesRegex(RuntimeError, "semantic revalidation failed"):
            ADAPTER.revalidate_restored_inventory(
                [item],
                ["right-hand-weapon"],
                None,
                inventory,
                equipment,
            )

    def test_geometry_material_weight_and_object_data_mutations_fail_closed(
        self,
    ) -> None:
        cases = []

        geometry = FakeObject()
        _, _, geometry_proof = capture_one(geometry)
        geometry.data.vertices[0].co = (0.25, 0.0, 0.0)
        cases.append(("geometry", geometry_proof, geometry, "geometry"))

        material = FakeObject()
        _, _, material_proof = capture_one(material)
        material.data.materials[0].diffuse_color[0] = 0.9
        cases.append(("material", material_proof, material, "materials"))

        weight = FakeObject()
        _, _, weight_proof = capture_one(weight)
        weight.data.vertices[0].groups[0].weight = 0.5
        cases.append(("weight", weight_proof, weight, "weights"))

        object_data = FakeObject()
        _, _, object_data_proof = capture_one(object_data)
        object_data.data.name = "mutated_geometry"
        cases.append(("object-data", object_data_proof, object_data, "object_data"))

        for name, proof, item, message in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, message):
                    ADAPTER.assert_render_geometry_unchanged(proof, [item])

    def test_object_mesh_data_and_material_replacement_fail_closed(self) -> None:
        original = FakeObject()
        _, _, proof = capture_one(original)
        replacement = FakeObject()
        replacement.data = original.data
        with self.assertRaisesRegex(RuntimeError, "added, removed, or replaced"):
            ADAPTER.assert_render_geometry_unchanged(proof, [replacement])

        data_owner = FakeObject()
        _, _, data_proof = capture_one(data_owner)
        old_material = data_owner.data.materials[0]
        data_owner.data = FakeMesh(data_owner.data.name, old_material)
        with self.assertRaisesRegex(RuntimeError, "added, removed, or replaced"):
            ADAPTER.assert_render_geometry_unchanged(data_proof, [data_owner])

        material_owner = FakeObject()
        _, _, material_proof = capture_one(material_owner)
        material_owner.data.materials[0] = FakeMaterial("fixture_metal")
        with self.assertRaisesRegex(RuntimeError, "replaced render material"):
            ADAPTER.assert_render_geometry_unchanged(material_proof, [material_owner])


class W3dAdditiveVertexMaterialTests(unittest.TestCase):
    def _material(self):
        material = types.SimpleNamespace(
            name="fixture_additive_vertex_material",
            use_nodes=True,
            shader=types.SimpleNamespace(src_blend="1", dest_blend="1"),
        )
        principled = FakeNode("BSDF_PRINCIPLED")
        principled.inputs["Base Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        material.principled = principled
        material.node_tree = types.SimpleNamespace(
            nodes=FakeNodeCollection([principled]), links=FakeLinks()
        )
        return material

    def _mesh(self, material, colors):
        attribute_data = FakeColorAttributeData(colors)
        attribute = types.SimpleNamespace(
            name="DCG_0", data=attribute_data, domain="CORNER", data_type="BYTE_COLOR"
        )
        mesh_data = types.SimpleNamespace(
            color_attributes=types.SimpleNamespace(active_color=attribute)
        )
        mesh = types.SimpleNamespace(
            type="MESH",
            data=mesh_data,
            material_slots=[types.SimpleNamespace(material=material)],
        )
        return mesh, attribute_data

    def _scene(self, material, meshes, images=None):
        ADAPTER.bpy.data = types.SimpleNamespace(
            objects=list(meshes),
            materials=[material],
            images=list(images or []),
        )

    def test_textureless_additive_material_converts_vertex_colors(self) -> None:
        material = self._material()
        mesh, attribute_data = self._mesh(
            material, [(0.0, 0.0, 0.0, 1.0), (0.8, 0.4, 0.2, 1.0)]
        )
        self._scene(material, [mesh])

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        converted = list(attribute_data)
        self.assertEqual(converted[0], (0.0, 0.0, 0.0, 0.0))
        self.assertAlmostEqual(converted[1][0], 1.0)
        self.assertAlmostEqual(converted[1][1], 0.5)
        self.assertAlmostEqual(converted[1][2], 0.25)
        self.assertAlmostEqual(converted[1][3], 0.8)
        base_color = material.principled.inputs["Base Color"].default_value
        self.assertEqual(tuple(base_color), (1.0, 1.0, 1.0, 1.0))
        color_links = [
            link
            for link in material.node_tree.links
            if link.to_socket is material.principled.inputs["Base Color"]
        ]
        self.assertEqual(len(color_links), 1)
        self.assertEqual(color_links[0].from_node.layer_name, "DCG_0")
        alpha_links = [
            link
            for link in material.node_tree.links
            if link.to_socket is material.principled.inputs["Alpha"]
        ]
        self.assertEqual(len(alpha_links), 1)
        self.assertIs(alpha_links[0].from_node, color_links[0].from_node)

    def test_textureless_additive_material_without_colors_uses_constant(self) -> None:
        material = self._material()
        mesh = types.SimpleNamespace(
            type="MESH",
            data=types.SimpleNamespace(color_attributes=None),
            material_slots=[types.SimpleNamespace(material=material)],
        )
        self._scene(material, [mesh])

        report = ADAPTER.convert_proven_additive_materials([material])

        self.assertEqual(report["converted_materials"], 1)
        base_color = material.principled.inputs["Base Color"].default_value
        self.assertEqual(tuple(base_color), (0.0, 0.0, 0.0, 1.0))
        self.assertEqual(material.principled.inputs["Alpha"].default_value, 0.0)

    def test_textureless_additive_material_with_nonblack_constant_fails(self) -> None:
        material = self._material()
        material.principled.inputs["Base Color"].default_value = (0.5, 0.0, 0.0, 1.0)
        mesh, _attribute_data = self._mesh(material, [(1.0, 1.0, 1.0, 1.0)])
        self._scene(material, [mesh])
        with self.assertRaisesRegex(RuntimeError, "ambiguous color source"):
            ADAPTER.convert_proven_additive_materials([material])

    def test_textureless_additive_material_shared_mesh_fails(self) -> None:
        material = self._material()
        other = types.SimpleNamespace(name="other")
        mesh, _attribute_data = self._mesh(material, [(1.0, 1.0, 1.0, 1.0)])
        mesh.material_slots.append(types.SimpleNamespace(material=other))
        self._scene(material, [mesh])
        with self.assertRaisesRegex(RuntimeError, "shares its render mesh"):
            ADAPTER.convert_proven_additive_materials([material])

    def test_textureless_additive_material_without_meshes_fails(self) -> None:
        material = self._material()
        self._scene(material, [])
        with self.assertRaisesRegex(RuntimeError, "no render mesh"):
            ADAPTER.convert_proven_additive_materials([material])


class FakeNodeCollection(list):
    def new(self, node_type: str):
        node = FakeNode(node_type)
        if node_type == "ShaderNodeVertexColor":
            node.outputs = FakeSocketCollection(
                {name: FakeSocket(name, node) for name in ("Color", "Alpha")}
            )
        self.append(node)
        return node


class FakeColorAttributeData(list):
    def foreach_get(self, attribute: str, buffer: list[float]) -> None:
        assert attribute == "color"
        buffer[:] = [channel for row in self for channel in row]

    def foreach_set(self, attribute: str, values) -> None:
        assert attribute == "color"
        flat = list(values)
        self[:] = [
            tuple(flat[offset : offset + 4]) for offset in range(0, len(flat), 4)
        ]


class W3dRetailAbsentTextureTests(unittest.TestCase):
    def test_normalize_rejects_unsafe_or_duplicate_basenames(self) -> None:
        with self.assertRaises(ValueError):
            ADAPTER.normalize_retail_absent_textures(["a.tga", "a.tga"])
        with self.assertRaises(ValueError):
            ADAPTER.normalize_retail_absent_textures(["../escape.tga"])
        with self.assertRaises(ValueError):
            ADAPTER.normalize_retail_absent_textures(["no_extension"])
        with self.assertRaises(ValueError):
            ADAPTER.normalize_retail_absent_textures(["evil.exe"])
        self.assertEqual(
            ADAPTER.normalize_retail_absent_textures(["B.tga", "a.dds"]),
            ["B.tga", "a.dds"],
        )

    def test_clear_only_tolerated_generated_placeholders(self) -> None:
        placeholder = types.SimpleNamespace(name="NBElvnBarx_D_NRM.dds", source="GENERATED")
        other = types.SimpleNamespace(name="other_missing.dds", source="GENERATED")
        staged = types.SimpleNamespace(name="staged.dds", source="FILE")
        image_node = types.SimpleNamespace(type="TEX_IMAGE", image=placeholder)
        other_node = types.SimpleNamespace(type="TEX_IMAGE", image=other)
        nodes = FakeNodeCollection([image_node, other_node])
        ADAPTER.bpy.data = types.SimpleNamespace(
            images=FakeImageCollection([placeholder, other, staged]),
            materials=[
                types.SimpleNamespace(
                    node_tree=types.SimpleNamespace(nodes=nodes, links=[])
                )
            ],
        )

        cleared = ADAPTER.clear_retail_absent_textures(["NBElvnBarx_D_NRM.tga"])

        self.assertEqual(cleared, ["NBElvnBarx_D_NRM.dds"])
        self.assertNotIn(placeholder, ADAPTER.bpy.data.images)
        self.assertIn(other, ADAPTER.bpy.data.images)
        self.assertIn(staged, ADAPTER.bpy.data.images)
        self.assertNotIn(image_node, nodes)
        self.assertIn(other_node, nodes)

    def test_unmatched_tolerated_name_fails_closed(self) -> None:
        ADAPTER.bpy.data = types.SimpleNamespace(images=[], materials=[])
        with self.assertRaisesRegex(RuntimeError, "did not match a generated placeholder"):
            ADAPTER.clear_retail_absent_textures(["absent.tga"])


class FakeImageCollection(list):
    def remove(self, value):
        super().remove(value)


if __name__ == "__main__":
    unittest.main()
