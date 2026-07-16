from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from openbfme_importer import cli
from openbfme_importer import retail_visual_closure as closure


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    if len(encoded) > size:
        raise ValueError("fixture identifier is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    size = len(payload) | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, size) + payload


def _animation_w3d(identifier: str, hierarchy: str) -> bytes:
    hierarchy_header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(hierarchy, 16),
        0,
        0.0,
        0.0,
        0.0,
    )
    header = struct.pack(
        "<I16s16sII",
        0x00040001,
        _fixed(identifier, 16),
        _fixed(hierarchy, 16),
        30,
        15,
    )
    return _chunk(
        0x100, _chunk(0x101, hierarchy_header), children=True
    ) + _chunk(0x200, _chunk(0x201, header), children=True)


def _textured_w3d(*identifiers: str) -> bytes:
    textures = b"".join(
        _chunk(
            0x31,
            _chunk(0x32, identifier.encode("cp1252") + b"\0"),
            children=True,
        )
        for identifier in identifiers
    )
    return _chunk(
        0x00000000,
        _chunk(0x30, textures, children=True),
        children=True,
    )


def _shader_string_property(name: str, value: str) -> bytes:
    raw_name = name.encode("cp1252") + b"\0"
    raw_value = value.encode("cp1252") + b"\0"
    return (
        struct.pack("<ii", 1, len(raw_name))
        + raw_name
        + struct.pack("<i", len(raw_value))
        + raw_value
    )


def _shader_integer_property(name: str, value: int) -> bytes:
    raw_name = name.encode("cp1252") + b"\0"
    return (
        struct.pack("<ii", 6, len(raw_name))
        + raw_name
        + struct.pack("<i", value)
    )


def _shader_material_w3d(*properties: bytes) -> bytes:
    material = _chunk(
        0x51,
        b"".join(_chunk(0x53, prop) for prop in properties),
        children=True,
    )
    return _chunk(
        0x00000000,
        _chunk(0x50, material, children=True),
        children=True,
    )


def _write(root: Path, relative: str, source: bytes) -> None:
    target = root.joinpath(*relative.split("/"))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source)


def _fixture(root: Path) -> None:
    _write(
        root,
        "data/ini/object/base.ini",
        b"""
Object VisualBase
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = BaseModel
      Skeleton = BASE_SKL
      Texture = BaseColor.tga
    End
    AnimationState = MOVING
      Animation = Move
        AnimationName = BASE_SKL.BASE_IDLE
      End
      Animation = Missing
        AnimationName = MissingRaw
      End
    End
  End
End
""",
    )
    _write(
        root,
        "data/ini/object/child.ini",
        b"""
ChildObject VisualChild VisualBase
  #include "child-visual.inc"
End
""",
    )
    _write(
        root,
        "data/ini/object/child-visual.inc",
        b"""
Draw = W3DScriptedModelDraw ModuleTag_Child
  DefaultModelConditionState
    Model = ChildModel
  End
  ModelConditionState = POST_RUBBLE
    Model = None
  End
End
""",
    )
    _write(root, "art/w3d/basemodel.w3d", b"")
    _write(root, "art/w3d/childmodel.w3d", b"")
    _write(root, "art/w3d/base_idle.w3d", _animation_w3d("BASE_IDLE", "BASE_SKL"))
    _write(root, "art/w3d/never-read.w3d", b"this is deliberately not W3D")
    _write(root, "art/compiled/basecolor.dds", b"dds fixture")


class RetailVisualClosureTests(unittest.TestCase):
    def test_object_assignment_is_not_misclassified_as_a_declaration(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write(
                root,
                "data/ini/commandbutton.ini",
                b"CommandButton BuildOne\n  Object = RealTarget\nEnd\n",
            )
            _write(
                root,
                "data/ini/object/real.ini",
                b"Object RealTarget\nEnd\n",
            )
            _write(root, "art/w3d/placeholder.w3d", b"")

            report = closure.build_retail_visual_closure(root, ["RealTarget"])

            self.assertTrue(report["summary"]["ready"])
            self.assertEqual(
                [item["virtualPath"] for item in report["definitionClosure"]],
                ["data/ini/object/real.ini"],
            )

    def test_unrelated_map_local_override_does_not_ambiguate_base_object(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write(
                root,
                "data/ini/object/neutral/warglair.ini",
                b"Object WargLair\nEnd\n",
            )
            _write(
                root,
                "maps/other-map/map.ini",
                b"Object WargLair\nEnd\n",
            )
            _write(root, "art/w3d/placeholder.w3d", b"")

            report = closure.build_retail_visual_closure(root, ["WargLair"])

            self.assertTrue(report["summary"]["ready"])
            self.assertEqual(
                report["targets"][0]["virtualPath"],
                "data/ini/object/neutral/warglair.ini",
            )

    def test_arbitrary_72_object_prop_batch_needs_no_model_payload_reads(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            names = [f"MapProp{index:03d}" for index in range(72)]
            source = "\n".join(f"Object {name}\nEnd" for name in names).encode(
                "ascii"
            )
            _write(root, "data/ini/object/map-props.ini", source)
            _write(root, "art/w3d/placeholder.w3d", b"")

            with mock.patch.object(
                closure,
                "_read_target_w3d_bytes",
                side_effect=AssertionError("no W3D candidate should be opened"),
            ):
                report = closure.build_retail_visual_closure(
                    root, reversed(names)
                )

            self.assertTrue(report["summary"]["ready"])
            self.assertEqual(report["summary"]["targetCount"], 72)
            self.assertEqual(report["summary"]["resolvedTargetCount"], 72)
            self.assertEqual(report["summary"]["scannedW3dCount"], 0)
            self.assertEqual(
                [item["name"] for item in report["objects"]], names
            )

    def test_exact_closure_targeted_header_scan_and_deterministic_hash(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            _fixture(root)
            reads: list[str] = []

            def guarded_read(asset: object) -> bytes:
                self.assertIsInstance(asset, closure._AssetFile)
                record = asset
                reads.append(record.virtual_path)
                self.assertNotEqual(record.virtual_path, "art/w3d/never-read.w3d")
                return record.physical_path.read_bytes()

            with mock.patch.object(
                closure, "_read_target_w3d_bytes", side_effect=guarded_read
            ):
                first = closure.build_retail_visual_closure(
                    root, ["VisualChild", "VisualBase"]
                )
                second = closure.build_retail_visual_closure(
                    root, ["VisualBase", "VisualChild"]
                )

            self.assertEqual(first, second)
            self.assertEqual(
                reads,
                [
                    "art/w3d/base_idle.w3d",
                    "art/w3d/basemodel.w3d",
                    "art/w3d/childmodel.w3d",
                    "art/w3d/base_idle.w3d",
                    "art/w3d/basemodel.w3d",
                    "art/w3d/childmodel.w3d",
                ],
            )
            self.assertFalse(first["summary"]["ready"])
            self.assertEqual(first["summary"]["scannedW3dCount"], 3)
            self.assertEqual(
                [item["virtualPath"] for item in first["scannedW3d"]],
                [
                    "art/w3d/base_idle.w3d",
                    "art/w3d/basemodel.w3d",
                    "art/w3d/childmodel.w3d",
                ],
            )
            scanned = first["scannedW3d"][0]
            self.assertEqual(
                scanned["sha256"],
                hashlib.sha256(
                    root.joinpath("art/w3d/base_idle.w3d").read_bytes()
                ).hexdigest(),
            )
            self.assertEqual(scanned["warnings"], [])

            definitions = {
                item["name"]: item for item in first["definitionClosure"]
            }
            self.assertEqual(set(definitions), {"VisualBase", "VisualChild"})
            self.assertEqual(definitions["VisualChild"]["parent"], "VisualBase")
            child = next(
                item for item in first["objects"] if item["name"] == "VisualChild"
            )
            self.assertEqual(child["ancestry"], ["VisualBase", "VisualChild"])
            self.assertEqual(child["drawModuleCount"], 2)
            self.assertIn("post-rubble", child["lifecycleCoverage"])

            closure_paths = {
                item["virtualPath"] for item in first["sourceClosure"]["paths"]
            }
            self.assertEqual(
                closure_paths,
                {
                    "data/ini/object/base.ini",
                    "data/ini/object/child.ini",
                    "data/ini/object/child-visual.inc",
                },
            )
            self.assertEqual(
                first["sourceClosure"]["includes"],
                [
                    {
                        "sourceVirtualPath": "data/ini/object/child.ini",
                        "line": 3,
                        "rawRef": "child-visual.inc",
                        "resolvedVirtualPath": "data/ini/object/child-visual.inc",
                    }
                ],
            )

            exact_by_id: dict[str, list[dict[str, object]]] = {}
            for item in first["exactLeaves"]:
                exact_by_id.setdefault(item["identifier"], []).append(item)
            compiled = exact_by_id["BaseColor.tga"][0]
            self.assertEqual(
                compiled["physicalVirtualPaths"], ["art/compiled/basecolor.dds"]
            )
            self.assertIn(
                "sage-compiled-texture:exact-tga-stem-to-dds",
                compiled["evidence"],
            )
            dotted = exact_by_id["BASE_SKL.BASE_IDLE"][0]
            self.assertEqual(
                dotted["physicalVirtualPaths"], ["art/w3d/base_idle.w3d"]
            )
            skeleton = exact_by_id["BASE_SKL"][0]
            self.assertEqual(
                skeleton["physicalVirtualPaths"], ["art/w3d/base_idle.w3d"]
            )
            self.assertTrue(
                any(item["identifier"] == "None" for item in first["semanticLeaves"])
            )

            unresolved = first["unresolved"]["references"]
            missing_raw = [
                item for item in unresolved if item["identifier"] == "MissingRaw"
            ]
            self.assertEqual(len(missing_raw), 2)
            self.assertTrue(all(item["status"] == "missing" for item in missing_raw))
            self.assertTrue(all("candidates" not in item for item in missing_raw))

            payload = dict(first)
            aggregate = payload.pop("aggregateSha256")
            canonical = json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            self.assertEqual(aggregate, hashlib.sha256(canonical).hexdigest())

    def test_resolved_model_w3d_embedded_texture_closure_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            _write(
                root,
                "data/ini/object/textured.ini",
                b"""
Object TexturedTarget
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = TargetModel
    End
  End
End
""",
            )
            _write(
                root,
                "art/w3d/targetmodel.w3d",
                _textured_w3d(
                    "Direct.png",
                    "CompiledOnly.tga",
                    "Ambiguous.tga",
                    "Missing.tga",
                ),
            )
            _write(root, "art/w3d/unrelated.w3d", b"not a W3D payload")
            _write(root, "art/textures/direct.png", b"direct PNG")
            _write(root, "art/compiled/compiledonly.dds", b"compiled DDS")
            _write(root, "art/a/ambiguous.dds", b"first DDS")
            _write(root, "art/b/ambiguous.dds", b"second DDS")
            reads: list[str] = []

            def guarded_read(asset: object) -> bytes:
                self.assertIsInstance(asset, closure._AssetFile)
                record = asset
                reads.append(record.virtual_path)
                self.assertNotEqual(record.virtual_path, "art/w3d/unrelated.w3d")
                return record.physical_path.read_bytes()

            with mock.patch.object(
                closure, "_read_target_w3d_bytes", side_effect=guarded_read
            ):
                report = closure.build_retail_visual_closure(
                    root, ["TexturedTarget"]
                )

            self.assertEqual(reads, ["art/w3d/targetmodel.w3d"])
            self.assertEqual(report["summary"]["scannedW3dCount"], 1)
            self.assertEqual(report["summary"]["unresolvedReferenceCount"], 0)
            self.assertEqual(report["summary"]["embeddedTextureReferenceCount"], 4)
            self.assertEqual(report["summary"]["resolvedEmbeddedTextureCount"], 2)
            self.assertEqual(report["summary"]["unresolvedEmbeddedTextureCount"], 2)
            self.assertFalse(report["summary"]["ready"])

            dependency = report["w3dDependencyClosure"]
            self.assertEqual(
                dependency["readBoundary"],
                {
                    "policy": (
                        "resolved-target-w3d-leaves-plus-exact-unresolved-candidates"
                    ),
                    "uniqueVirtualPaths": ["art/w3d/targetmodel.w3d"],
                    "uniqueReadCount": 1,
                    "byteLength": len(
                        root.joinpath("art/w3d/targetmodel.w3d").read_bytes()
                    ),
                },
            )
            by_identifier = {
                item["identifier"]: item
                for item in dependency["embeddedTextures"]
            }
            self.assertEqual(
                by_identifier["Direct.png"]["physicalVirtualPaths"],
                ["art/textures/direct.png"],
            )
            compiled = by_identifier["CompiledOnly.tga"]
            self.assertEqual(
                compiled["physicalVirtualPaths"],
                ["art/compiled/compiledonly.dds"],
            )
            self.assertIn(
                "w3d-compiled-texture:exact-tga-stem-to-dds",
                compiled["evidence"],
            )
            ambiguous = by_identifier["Ambiguous.tga"]
            self.assertEqual(ambiguous["status"], "ambiguous")
            self.assertEqual(
                ambiguous["candidates"],
                ["art/a/ambiguous.dds", "art/b/ambiguous.dds"],
            )
            missing = by_identifier["Missing.tga"]
            self.assertEqual(missing["status"], "missing")
            self.assertEqual(missing["candidates"], [])

            section_payload = dict(dependency)
            section_hash = section_payload.pop("aggregateSha256")
            canonical = json.dumps(
                section_payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            self.assertEqual(
                section_hash, hashlib.sha256(canonical).hexdigest()
            )

    def test_shader_material_texture_properties_join_embedded_closure(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            _write(
                root,
                "data/ini/object/shader.ini",
                b"""
Object ShaderTarget
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = ShaderModel
    End
  End
End
""",
            )
            diffuse = _shader_string_property("DiffuseTexture", "PRGrey.tga")
            normal = _shader_string_property("NormalMap", "PRGrey_NRM.tga")
            indexed = _shader_string_property("Texture_0", "Sky.tga")
            ignored = _shader_string_property("TechniqueName", "NotAnAsset.tga")
            source = _shader_material_w3d(diffuse, normal, indexed, ignored)
            _write(root, "art/w3d/shadermodel.w3d", source)
            _write(root, "art/compiledtextures/pr/prgrey.dds", b"compiled diffuse")
            _write(root, "art/textures/prgrey_nrm.tga", b"authored normal")
            _write(root, "art/textures/sky.tga", b"authored sky")

            report = closure.build_retail_visual_closure(root, ["ShaderTarget"])

            self.assertTrue(report["summary"]["ready"])
            self.assertEqual(report["summary"]["embeddedTextureReferenceCount"], 3)
            by_property = {
                item["shaderMaterialPropertyName"]: item
                for item in report["w3dDependencyClosure"]["embeddedTextures"]
            }
            self.assertEqual(
                set(by_property), {"DiffuseTexture", "NormalMap", "Texture_0"}
            )
            self.assertEqual(
                by_property["DiffuseTexture"]["physicalVirtualPaths"],
                ["art/compiledtextures/pr/prgrey.dds"],
            )
            self.assertIn(
                "w3d-compiled-texture:exact-tga-stem-to-dds",
                by_property["DiffuseTexture"]["evidence"],
            )
            self.assertEqual(
                by_property["NormalMap"]["physicalVirtualPaths"],
                ["art/textures/prgrey_nrm.tga"],
            )
            self.assertEqual(
                by_property["Texture_0"]["physicalVirtualPaths"],
                ["art/textures/sky.tga"],
            )
            self.assertNotIn(
                "NotAnAsset.tga",
                {item["identifier"] for item in by_property.values()},
            )

            for payload, name in (
                (diffuse, "DiffuseTexture"),
                (normal, "NormalMap"),
                (indexed, "Texture_0"),
            ):
                dependency = by_property[name]
                provenance = dependency["provenance"]
                self.assertEqual(provenance["chunkId"], 0x53)
                self.assertEqual(provenance["valueOffset"], source.index(payload))
                self.assertEqual(provenance["valueSize"], len(payload))
                self.assertEqual(
                    dependency["referenceEvidence"],
                    ["w3d-shader-material-property:texture-bearing-name"],
                )

    def test_shader_material_missing_and_ambiguous_textures_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            _write(
                root,
                "data/ini/object/shader.ini",
                b"""
Object ShaderTarget
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = ShaderModel
    End
  End
End
""",
            )
            source = _shader_material_w3d(
                _shader_string_property("DiffuseTexture", "Missing.tga"),
                _shader_string_property("NormalMap", "Shared.tga"),
                _shader_string_property("Description", "IgnoredMissing.tga"),
            )
            _write(root, "art/w3d/shadermodel.w3d", source)
            _write(root, "art/a/shared.dds", b"first compiled candidate")
            _write(root, "art/b/shared.dds", b"second compiled candidate")

            report = closure.build_retail_visual_closure(root, ["ShaderTarget"])

            self.assertFalse(report["summary"]["ready"])
            self.assertEqual(report["summary"]["embeddedTextureReferenceCount"], 2)
            self.assertEqual(report["summary"]["unresolvedEmbeddedTextureCount"], 2)
            by_property = {
                item["shaderMaterialPropertyName"]: item
                for item in report["w3dDependencyClosure"]["embeddedTextures"]
            }
            self.assertEqual(by_property["DiffuseTexture"]["status"], "missing")
            self.assertEqual(by_property["DiffuseTexture"]["candidates"], [])
            self.assertEqual(by_property["NormalMap"]["status"], "ambiguous")
            self.assertEqual(
                by_property["NormalMap"]["candidates"],
                ["art/a/shared.dds", "art/b/shared.dds"],
            )

    def test_non_string_texture_property_is_rejected_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            _write(
                root,
                "data/ini/object/shader.ini",
                b"""
Object ShaderTarget
  Draw = W3DScriptedModelDraw ModuleTag_Main
    DefaultModelConditionState
      Model = ShaderModel
    End
  End
End
""",
            )
            _write(
                root,
                "art/w3d/shadermodel.w3d",
                _shader_material_w3d(
                    _shader_integer_property("NormalMap", 7)
                ),
            )

            with self.assertRaisesRegex(
                ValueError,
                "texture-bearing shader material property must have a non-empty string",
            ):
                closure.build_retail_visual_closure(root, ["ShaderTarget"])

    def test_ambiguous_definitions_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write(root, "data/ini/object/a.ini", b"Object Duplicate\nEnd\n")
            _write(root, "data/ini/object/b.ini", b"Object duplicate\nEnd\n")
            _write(root, "art/w3d/placeholder.w3d", b"")

            with self.assertRaisesRegex(ValueError, "ambiguous Object definition"):
                closure.build_retail_visual_closure(root, ["Duplicate"])

    def test_bounds_and_unsafe_inputs_are_rejected_before_model_reads(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "assets"
            _write(root, "one.ini", b"Object One\nEnd\n")
            _write(root, "one.w3d", b"")

            with self.assertRaisesRegex(ValueError, "unsafe target Object"):
                closure.build_retail_visual_closure(root, ["../escape"])
            with mock.patch.object(closure, "MAX_ASSET_FILES", 1):
                with self.assertRaisesRegex(ValueError, "asset count exceeds"):
                    closure.build_retail_visual_closure(root, ["One"])

            not_a_root = Path(raw) / "file.bin"
            not_a_root.write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "not a directory"):
                closure.build_retail_visual_closure(not_a_root, ["One"])

    def test_cli_parser_exposes_repeatable_object_targets(self) -> None:
        args = cli.build_parser().parse_args(
            [
                "visual-closure",
                "--assets-root",
                "C:/private/effective-assets",
                "--object",
                "One",
                "--object",
                "Two",
            ]
        )
        self.assertEqual(args.command, "visual-closure")
        self.assertEqual(args.objects, ["One", "Two"])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            state = Path(raw) / "private-state"
            _fixture(root)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "visual-closure",
                        "--assets-root",
                        str(root),
                        "--object",
                        "VisualBase",
                    ]
                )
            self.assertEqual(result, 6)
            output = json.loads(stdout.getvalue())
            self.assertFalse(output["ready"])
            report_path = Path(output["report"])
            self.assertEqual(report_path.parent, state / "reports")
            self.assertTrue(report_path.is_file())
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(output["aggregate_sha256"], report["aggregateSha256"])


if __name__ == "__main__":
    unittest.main()
