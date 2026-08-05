from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile

import pytest

from openbfme_importer.w3d_glb_validation import (
    W3DGLBSemanticMismatchError,
    W3DGLBValidationError,
    W3DGLBSemanticSummary,
    validate_w3d_glb_semantics,
)


def _visibility_channel() -> dict[str, object]:
    return {
        "owner": "object",
        "data_path": "hide_viewport",
        "array_index": 0,
        "keys": [{"frame": 0.0, "value": 1.0, "interpolation": "CONSTANT"}],
    }


def _valid_document_and_bin() -> tuple[dict[str, object], bytes]:
    positions = struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    indices = struct.pack("<3H", 0, 1, 2) + b"\0\0"
    inverse_bind = struct.pack("<16f", *([1.0] * 16))
    animation_input = struct.pack("<2f", 0.0, 1.0)
    animation_output = struct.pack("<6f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    image = b"PNG!"
    payload = (
        positions + indices + inverse_bind + animation_input + animation_output + image
    )
    document: dict[str, object] = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(payload)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
            {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963},
            {"buffer": 0, "byteOffset": 44, "byteLength": 64},
            {"buffer": 0, "byteOffset": 108, "byteLength": 8},
            {"buffer": 0, "byteOffset": 116, "byteLength": 24},
            {"buffer": 0, "byteOffset": 140, "byteLength": 4},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": 1, "type": "MAT4"},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "SCALAR"},
            {"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
        "images": [{"bufferView": 5, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "meshes": [
            {
                "primitives": [
                    {"attributes": {"POSITION": 0}, "indices": 1, "material": 0}
                ]
            }
        ],
        "nodes": [{"mesh": 0, "skin": 0}, {}],
        "skins": [{"joints": [1], "inverseBindMatrices": 2, "skeleton": 1}],
        "animations": [
            {
                "samplers": [{"input": 3, "output": 4, "interpolation": "LINEAR"}],
                "channels": [
                    {"sampler": 0, "target": {"node": 1, "path": "translation"}}
                ],
            }
        ],
        "scenes": [{"nodes": [0, 1]}],
        "scene": 0,
        "extras": {
            "openbfme_w3d_visibility_only_animations": {
                "schema": "openbfme.w3d-visibility-only-animations",
                "version": 1,
                "animations": [
                    {
                        "name": "authored-name-not-returned",
                        "shape": "visibility-only",
                        "channels": [_visibility_channel()],
                    }
                ],
            }
        },
    }
    return document, payload


def _glb(document: dict[str, object], payload: bytes | None) -> bytes:
    encoded = json.dumps(
        document, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode()
    encoded += b" " * (-len(encoded) % 4)
    chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    if payload is not None:
        padded = payload + (b"\0" * (-len(payload) % 4))
        chunks += struct.pack("<II", len(padded), 0x004E4942) + padded
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def _validate(
    tmp_path: Path,
    document: dict[str, object],
    payload: bytes | None,
    expected: dict[str, int] | None = None,
) -> W3DGLBSemanticSummary:
    output = tmp_path / "candidate.glb"
    output.write_bytes(_glb(document, payload))
    return validate_w3d_glb_semantics(
        output,
        expected
        or {
            "meshes": 1,
            "materials": 1,
            "images": 1,
            "skeletons": 1,
            "animations": 2,
            "vertices": 3,
            "triangles": 1,
            "skinned_meshes": 1,
            "bones": 1,
        },
    )


def test_validates_indexed_geometry_and_visibility_sidecar_without_names(
    tmp_path: Path,
) -> None:
    document, payload = _valid_document_and_bin()

    summary = _validate(tmp_path, document, payload)

    assert summary == W3DGLBSemanticSummary(
        mesh_count=1,
        primitive_count=1,
        material_count=1,
        image_count=1,
        skin_count=1,
        gltf_animation_count=1,
        visibility_only_animation_count=1,
        animation_count=2,
        vertex_count=3,
        triangle_count=1,
        skinned_mesh_node_count=1,
        joint_count=1,
    )
    assert "authored-name" not in repr(summary)


def test_counts_non_indexed_triangles(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    primitive = document["meshes"][0]["primitives"][0]  # type: ignore[index]
    del primitive["indices"]

    summary = _validate(tmp_path, document, payload)

    assert summary.triangle_count == 1


@pytest.mark.parametrize("resource", ["buffer", "image"])
def test_rejects_external_resource_uris(tmp_path: Path, resource: str) -> None:
    document, payload = _valid_document_and_bin()
    if resource == "buffer":
        document["buffers"][0]["uri"] = "outside.bin"  # type: ignore[index]
        payload = None
    else:
        document["images"] = [{"uri": "outside.png"}]

    with pytest.raises(W3DGLBValidationError, match="external resource URI"):
        _validate(tmp_path, document, payload)


def test_rejects_buffer_view_and_accessor_overruns(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    document["bufferViews"][0]["byteLength"] = len(payload) + 1  # type: ignore[index]
    with pytest.raises(W3DGLBValidationError, match="bufferView exceeds"):
        _validate(tmp_path, document, payload)

    document, payload = _valid_document_and_bin()
    document["accessors"][0]["count"] = 4  # type: ignore[index]
    with pytest.raises(W3DGLBValidationError, match="accessor exceeds"):
        _validate(tmp_path, document, payload)


def test_rejects_out_of_range_triangle_index(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    broken = bytearray(payload)
    struct.pack_into("<H", broken, 40, 3)

    with pytest.raises(W3DGLBValidationError, match="POSITION bounds"):
        _validate(tmp_path, document, bytes(broken))


def test_rejects_malformed_visibility_only_contract_without_echoing_name(
    tmp_path: Path,
) -> None:
    document, payload = _valid_document_and_bin()
    sidecar = document["extras"][  # type: ignore[index]
        "openbfme_w3d_visibility_only_animations"
    ]
    sidecar["animations"][0]["shape"] = "wrong"  # type: ignore[index]

    with pytest.raises(W3DGLBValidationError) as caught:
        _validate(tmp_path, document, payload)
    assert "authored-name" not in str(caught.value)


def test_raises_dedicated_error_for_exact_count_mismatch(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()

    with pytest.raises(W3DGLBSemanticMismatchError, match="triangle_count"):
        _validate(tmp_path, document, payload, {"triangle_count": 2})


def test_asset_only_glb_cannot_claim_geometry(tmp_path: Path) -> None:
    output = tmp_path / "asset-only.glb"
    output.write_bytes(_glb({"asset": {"version": "2.0"}}, None))

    with pytest.raises(W3DGLBValidationError, match="renderable TRIANGLES"):
        validate_w3d_glb_semantics(output, {"meshes": 1, "vertices": 3, "triangles": 1})


def test_explicit_zero_geometry_contract_accepts_pivot_only_hierarchy(
    tmp_path: Path,
) -> None:
    inverse_bind = struct.pack("<16f", *([1.0] * 16))
    document: dict[str, object] = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(inverse_bind)}],
        "bufferViews": [{"buffer": 0, "byteLength": len(inverse_bind)}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 1, "type": "MAT4"}
        ],
        "nodes": [{}, {"children": [0]}],
        "skins": [{"joints": [0], "inverseBindMatrices": 0}],
        "scenes": [{"nodes": [1]}],
        "scene": 0,
    }

    summary = _validate(
        tmp_path,
        document,
        inverse_bind,
        {
            "mesh_count": 0,
            "vertex_count": 0,
            "triangle_count": 0,
            "skin_count": 1,
            "joint_count": 1,
            "animation_count": 0,
        },
    )

    assert summary.mesh_count == 0
    assert summary.skin_count == 1
    assert summary.joint_count == 1


def test_partial_zero_geometry_contract_still_rejects_asset_only_glb(
    tmp_path: Path,
) -> None:
    output = tmp_path / "asset-only.glb"
    output.write_bytes(_glb({"asset": {"version": "2.0"}}, None))

    with pytest.raises(W3DGLBValidationError, match="renderable TRIANGLES"):
        validate_w3d_glb_semantics(output, {"mesh_count": 0})


def test_rejects_malformed_container_length(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    malformed = bytearray(_glb(document, payload))
    struct.pack_into("<I", malformed, 8, len(malformed) - 1)
    output = tmp_path / "malformed.glb"
    output.write_bytes(malformed)

    with pytest.raises(W3DGLBValidationError, match="header"):
        validate_w3d_glb_semantics(output, {"mesh_count": 1})


def test_rejects_sparse_accessor_and_empty_expectations(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    document["accessors"][0]["sparse"] = {"count": 1}  # type: ignore[index]
    with pytest.raises(W3DGLBValidationError, match="sparse accessor"):
        _validate(tmp_path, document, payload)

    output = tmp_path / "valid.glb"
    output.write_bytes(_glb(*_valid_document_and_bin()))
    with pytest.raises(W3DGLBValidationError, match="expected count contract"):
        validate_w3d_glb_semantics(output, {})


def test_does_not_require_payload_names_in_error_or_summary(tmp_path: Path) -> None:
    document, payload = _valid_document_and_bin()
    summary = _validate(tmp_path, document, payload, {"animation_count": 2})
    assert summary.animation_count == 2
    assert "name" not in repr(summary).casefold()


def test_path_must_be_an_ordinary_file() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        with pytest.raises(W3DGLBValidationError, match="ordinary file"):
            validate_w3d_glb_semantics(Path(temporary), {"mesh_count": 1})
