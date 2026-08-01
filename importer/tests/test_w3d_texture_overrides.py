from __future__ import annotations

from io import BytesIO
import json
from pathlib import Path
import struct
import tempfile
import unittest

from PIL import Image

from importer.openbfme_importer.pipeline import (
    _apply_w3d_texture_overrides,
    _stage_w3d_sources,
    _validate_w3d_texture_override_glb,
    _w3d_texture_references,
)
from importer.openbfme_importer.profile import (
    ImportProfile,
    normalize_w3d_texture_overrides,
)


def chunk(chunk_type: int, payload: bytes, *, children: bool = False) -> bytes:
    size = len(payload) | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_type, size) + payload


def model_with_textures(*references: str) -> bytes:
    textures = b"".join(
        chunk(
            0x31,
            chunk(0x32, reference.encode("utf-8") + b"\0"),
            children=True,
        )
        for reference in references
    )
    return chunk(0x30, textures, children=True)


def shader_texture_property(name: str, value: str) -> bytes:
    payload = b"".join(
        (
            struct.pack("<I", 1),
            struct.pack("<I", len(name) + 1),
            name.encode("utf-8") + b"\0",
            struct.pack("<I", len(value) + 1),
            value.encode("utf-8") + b"\0",
        )
    )
    return chunk(0x51, chunk(0x53, payload), children=True)


def png_bytes(color: tuple[int, int, int, int]) -> bytes:
    output = BytesIO()
    Image.new("RGBA", (2, 2), color).save(output, format="PNG")
    return output.getvalue()


def glb_with_image(
    embedded: bytes,
    *,
    image_name: str = "fortress1",
    base_color: bool = True,
    texture_source: object = 0,
    base_color_index: object = 0,
) -> bytes:
    binary = embedded + b"\0" * ((-len(embedded)) % 4)
    material = (
        {"pbrMetallicRoughness": {"baseColorTexture": {"index": base_color_index}}}
        if base_color
        else {"pbrMetallicRoughness": {}}
    )
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteLength": len(embedded)}],
        "images": [
            {
                "bufferView": 0,
                "mimeType": "image/png",
                "name": image_name,
            }
        ],
        "textures": [{"source": texture_source}],
        "materials": [material],
    }
    encoded_json = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded_json += b" " * ((-len(encoded_json)) % 4)
    total = 12 + 8 + len(encoded_json) + 8 + len(binary)
    return b"".join(
        (
            struct.pack("<4sII", b"glTF", 2, total),
            struct.pack("<II", len(encoded_json), 0x4E4F534A),
            encoded_json,
            struct.pack("<II", len(binary), 0x004E4942),
            binary,
        )
    )


class W3DTextureOverrideProfileTests(unittest.TestCase):
    def test_normalizes_exact_records_and_order(self) -> None:
        actual = normalize_w3d_texture_overrides(
            [
                {
                    "authored": "ZTexture.TGA",
                    "target": "ZTexture.DDS",
                    "source": "ZTextureD.DDS",
                },
                {
                    "authored": "ATexture.tga",
                    "target": "ATexture.dds",
                    "source": "ATextureD.dds",
                },
            ]
        )
        self.assertEqual(
            actual,
            [
                {
                    "authored": "atexture.tga",
                    "target": "atexture.dds",
                    "source": "atextured.dds",
                },
                {
                    "authored": "ztexture.tga",
                    "target": "ztexture.dds",
                    "source": "ztextured.dds",
                },
            ],
        )

    def test_rejects_unsafe_ambiguous_or_chained_records(self) -> None:
        invalid = [
            [],
            [{"authored": "x.tga", "target": "x.dds"}],
            [
                {
                    "authored": "../x.tga",
                    "target": "x.dds",
                    "source": "xd.dds",
                }
            ],
            [
                {
                    "authored": "con.tga",
                    "target": "con.dds",
                    "source": "cond.dds",
                }
            ],
            [
                {
                    "authored": "x.tga",
                    "target": "y.dds",
                    "source": "yd.dds",
                }
            ],
            [
                {
                    "authored": "x.tga",
                    "target": "x.dds",
                    "source": "xd.png",
                }
            ],
            [
                {"authored": "x.tga", "target": "x.dds", "source": "y.dds"},
                {"authored": "y.tga", "target": "y.dds", "source": "z.dds"},
            ],
        ]
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(ValueError):
                normalize_w3d_texture_overrides(value)

    def test_profile_allows_overrides_only_on_explicit_w3d_closure(self) -> None:
        def profile(converter: str, include_closure: bool) -> dict:
            options = {
                "model": "model.w3d",
                "textureOverrides": [
                    {
                        "authored": "fortress1.tga",
                        "target": "fortress1.dds",
                        "source": "fortress1d.dds",
                    }
                ],
            }
            if include_closure:
                options["inputResourceIds"] = ["textures"]
            return {
                "format": 1,
                "id": "test-profile",
                "pack": {"id": "test-pack"},
                "resources": [
                    {
                        "id": "model",
                        "kind": "model",
                        "converter": converter,
                        "output": "model.glb",
                        "patterns": ["model.w3d"],
                        "options": options,
                    },
                    {
                        "id": "textures",
                        "kind": "texture",
                        "converter": "hash-only",
                        "patterns": ["fortress1.dds", "fortress1d.dds"],
                    },
                ],
            }

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "profile.json"
            path.write_text(json.dumps(profile("w3d-hierarchical", True)), "utf-8")
            loaded = ImportProfile.load(path)
            self.assertEqual(
                loaded.resources[0].options["textureOverrides"][0]["target"],
                "fortress1.dds",
            )
            for converter, closure in (("copy", True), ("w3d-hierarchical", False)):
                with self.subTest(converter=converter, closure=closure):
                    path.write_text(json.dumps(profile(converter, closure)), "utf-8")
                    with self.assertRaises(ValueError):
                        ImportProfile.load(path)


class W3DTextureOverrideStagingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.model_source = self.root / "model.w3d"
        self.target_source = self.root / "fortress1.dds"
        self.override_source = self.root / "fortress1d.dds"
        self.model_source.write_bytes(
            model_with_textures("Fortress1.tga", "fortress1.TGA")
        )
        self.target_source.write_bytes(b"intact texture")
        self.override_source.write_bytes(b"damaged texture")
        self.copied = _stage_w3d_sources(
            [self.model_source, self.target_source, self.override_source],
            self.root / "job" / "input",
        )
        self.options = [
            {
                "authored": "fortress1.tga",
                "target": "fortress1.dds",
                "source": "fortress1d.dds",
            }
        ]

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_reads_only_structural_texture_name_chunks(self) -> None:
        self.assertEqual(
            _w3d_texture_references(self.copied["model.w3d"]),
            ["Fortress1.tga", "fortress1.TGA"],
        )
        shader_model = self.root / "shader-model.w3d"
        shader_model.write_bytes(
            shader_texture_property("DiffuseTexture", "Fortress1.tga")
        )
        self.assertEqual(_w3d_texture_references(shader_model), ["Fortress1.tga"])
        malformed = self.root / "malformed.w3d"
        malformed.write_bytes(struct.pack("<II", 0x30, 0x80000020) + b"short")
        with self.assertRaises(RuntimeError):
            _w3d_texture_references(malformed)

    def test_replaces_only_job_local_target_and_emits_hash_proof(self) -> None:
        before_source = self.copied["fortress1d.dds"].read_bytes()
        proof = _apply_w3d_texture_overrides(
            self.copied,
            self.copied["model.w3d"],
            self.options,
        )
        self.assertIsNotNone(proof)
        assert proof is not None
        self.assertEqual(self.copied["fortress1.dds"].read_bytes(), before_source)
        self.assertEqual(self.copied["fortress1d.dds"].read_bytes(), before_source)
        self.assertEqual(proof["entries"][0]["authoredReferenceCount"], 2)
        self.assertEqual(
            proof["entries"][0]["sourceSha256"],
            proof["entries"][0]["stagedTargetSha256"],
        )
        self.assertNotEqual(
            proof["stagedClosureBeforeSha256"],
            proof["stagedClosureAfterSha256"],
        )

    def test_rejects_missing_same_bytes_and_reference_ambiguity(self) -> None:
        missing = [dict(self.options[0], source="missing.dds")]
        with self.assertRaises(RuntimeError):
            _apply_w3d_texture_overrides(self.copied, self.copied["model.w3d"], missing)

        self.copied["fortress1.dds"].write_bytes(b"damaged texture")
        with self.assertRaises(RuntimeError):
            _apply_w3d_texture_overrides(
                self.copied, self.copied["model.w3d"], self.options
            )

        self.copied["fortress1.dds"].write_bytes(b"intact texture")
        self.copied["model.w3d"].write_bytes(
            model_with_textures("fortress1.tga", "fortress1.png")
        )
        with self.assertRaises(RuntimeError):
            _apply_w3d_texture_overrides(
                self.copied, self.copied["model.w3d"], self.options
            )

class W3DTextureOverrideGlbTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        model = self.root / "model.w3d"
        target = self.root / "fortress1.png"
        source = self.root / "fortress1d.png"
        model.write_bytes(model_with_textures("fortress1.tga"))
        target.write_bytes(png_bytes((255, 0, 0, 255)))
        source.write_bytes(png_bytes((0, 0, 255, 255)))
        self.copied = _stage_w3d_sources(
            [model, target, source], self.root / "job" / "input"
        )
        self.proof = _apply_w3d_texture_overrides(
            self.copied,
            self.copied["model.w3d"],
            [
                {
                    "authored": "fortress1.tga",
                    "target": "fortress1.png",
                    "source": "fortress1d.png",
                }
            ],
        )
        assert self.proof is not None

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_proves_exact_embedded_base_color_image(self) -> None:
        glb = self.root / "result.glb"
        glb.write_bytes(glb_with_image(self.copied["fortress1d.png"].read_bytes()))
        actual = _validate_w3d_texture_override_glb(glb, self.copied, self.proof)
        self.assertIsNotNone(actual)
        assert actual is not None
        entry = actual["entries"][0]
        self.assertTrue(actual["complete"])
        self.assertTrue(entry["decodedRgbaExact"])
        self.assertEqual(entry["maxRgbChannelDelta"], 0)
        self.assertEqual(entry["baseColorTextureIndices"], [0])
        self.assertEqual(entry["baseColorMaterialIndices"], [0])

    def test_proof_contract_does_not_depend_on_adapter_path_logging(self) -> None:
        glb = self.root / "result.glb"
        glb.write_bytes(glb_with_image(self.copied["fortress1d.png"].read_bytes()))
        actual = _validate_w3d_texture_override_glb(glb, self.copied, self.proof)
        self.assertIsNotNone(actual)
        assert actual is not None
        entry = actual["entries"][0]
        self.assertNotIn("adapterLoadEvidenceCount", entry)
        self.assertEqual(
            entry["sourceDecodedRgbaSha256"],
            entry["embeddedDecodedRgbaSha256"],
        )
        self.assertEqual(entry["baseColorMaterialIndices"], [0])

    def test_rejects_unconsumed_or_wrong_embedded_image(self) -> None:
        glb = self.root / "result.glb"
        glb.write_bytes(
            glb_with_image(self.copied["fortress1d.png"].read_bytes(), base_color=False)
        )
        with self.assertRaises(RuntimeError):
            _validate_w3d_texture_override_glb(glb, self.copied, self.proof)

        glb.write_bytes(glb_with_image(png_bytes((0, 255, 0, 255))))
        with self.assertRaises(RuntimeError):
            _validate_w3d_texture_override_glb(glb, self.copied, self.proof)

    def test_rejects_boolean_indices_that_alias_integer_zero(self) -> None:
        glb = self.root / "result.glb"
        embedded = self.copied["fortress1d.png"].read_bytes()
        glb.write_bytes(glb_with_image(embedded, texture_source=False))
        with self.assertRaises(RuntimeError):
            _validate_w3d_texture_override_glb(glb, self.copied, self.proof)

        glb.write_bytes(glb_with_image(embedded, base_color_index=False))
        with self.assertRaises(RuntimeError):
            _validate_w3d_texture_override_glb(glb, self.copied, self.proof)


if __name__ == "__main__":
    unittest.main()
