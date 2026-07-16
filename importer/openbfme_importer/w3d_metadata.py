"""Bounded, read-only metadata scanning for Westwood W3D binaries.

The conversion pipeline needs exact identifiers and dependency evidence before
it invokes Blender or any other converter.  This module therefore inspects only
caller-supplied bytes, records every chunk boundary, and extracts the small
header/material records that are useful for planning.  It does not decode mesh
geometry, mutate files, search the filesystem, or invent replacement names.

Malformed and unsupported payloads are retained as structured warnings.  A
truncated record never produces a guessed identifier.  The resulting metadata
can be passed to :mod:`w3d_index` through :meth:`W3DMetadata.file_headers`.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
from typing import Iterable

from .paths import safe_relative_parts
from .w3d_index import W3DFileHeaders


MAX_W3D_METADATA_BYTES = 512 * 1024 * 1024
MAX_W3D_METADATA_CHUNKS = 1_000_000
MAX_W3D_METADATA_DEPTH = 64
MAX_W3D_METADATA_PATH_LENGTH = 512

_CHUNK_HEADER_SIZE = 8
_SUBCHUNK_FLAG = 0x80000000
_SIZE_MASK = 0x7FFFFFFF


# Chunk IDs and layouts are the stable W3D binary contract used by the retail
# files.  Names are intentionally kept here rather than imported from Blender,
# so census/index work has no runtime dependency on the conversion toolchain.
_CHUNK_NAMES: dict[int, str] = {
    0x00000000: "mesh",
    0x00000002: "vertices",
    0x00000003: "vertex-normals",
    0x0000000C: "mesh-user-text",
    0x0000000E: "vertex-influences",
    0x0000001F: "mesh-header",
    0x00000020: "triangles",
    0x00000022: "vertex-shade-indices",
    0x00000023: "prelit-unlit",
    0x00000024: "prelit-vertex",
    0x00000025: "prelit-lightmap-multi-pass",
    0x00000026: "prelit-lightmap-multi-texture",
    0x00000028: "material-info",
    0x00000029: "shaders",
    0x0000002A: "vertex-materials",
    0x0000002B: "vertex-material",
    0x0000002C: "vertex-material-name",
    0x0000002D: "vertex-material-info",
    0x0000002E: "vertex-mapper-args-0",
    0x0000002F: "vertex-mapper-args-1",
    0x00000030: "textures",
    0x00000031: "texture",
    0x00000032: "texture-name",
    0x00000033: "texture-info",
    0x00000038: "material-pass",
    0x00000039: "vertex-material-ids",
    0x0000003A: "shader-ids",
    0x0000003B: "diffuse-color-values",
    0x0000003C: "diffuse-illumination-values",
    0x0000003E: "specular-color-values",
    0x0000003F: "shader-material-ids",
    0x00000048: "texture-stage",
    0x00000049: "texture-ids",
    0x0000004A: "stage-texcoords",
    0x0000004B: "per-face-texcoord-ids",
    0x00000050: "shader-materials",
    0x00000051: "shader-material",
    0x00000052: "shader-material-header",
    0x00000053: "shader-material-property",
    0x00000058: "deform",
    0x00000060: "tangents",
    0x00000061: "bitangents",
    0x00000080: "ps2-shaders",
    0x00000090: "aabb-tree",
    0x00000091: "aabb-tree-header",
    0x00000092: "aabb-tree-poly-indices",
    0x00000093: "aabb-tree-nodes",
    0x00000100: "hierarchy",
    0x00000101: "hierarchy-header",
    0x00000102: "pivots",
    0x00000103: "pivot-fixups",
    0x00000200: "animation",
    0x00000201: "animation-header",
    0x00000202: "animation-channel",
    0x00000203: "animation-bit-channel",
    0x00000280: "compressed-animation",
    0x00000281: "compressed-animation-header",
    0x00000282: "compressed-animation-channel",
    0x00000283: "compressed-bit-channel",
    0x00000284: "compressed-animation-motion-channel",
    0x000002C0: "morph-animation",
    0x00000300: "hierarchical-model",
    0x00000400: "lod-model",
    0x00000420: "collection",
    0x00000440: "points",
    0x00000460: "light",
    0x00000500: "emitter",
    0x00000501: "emitter-header",
    0x00000502: "emitter-user-data",
    0x00000503: "emitter-info",
    0x00000504: "emitter-info-v2",
    0x00000505: "emitter-properties",
    0x00000509: "emitter-line-properties",
    0x0000050A: "emitter-rotation-keyframes",
    0x0000050B: "emitter-frame-keyframes",
    0x0000050C: "emitter-blur-time-keyframes",
    0x0000050D: "emitter-extra-info",
    0x00000600: "aggregate",
    0x00000700: "hlod",
    0x00000701: "hlod-header",
    0x00000702: "hlod-lod-array",
    0x00000703: "hlod-sub-object-array-header",
    0x00000704: "hlod-sub-object",
    0x00000705: "hlod-aggregate-array",
    0x00000706: "hlod-proxy-array",
    0x00000740: "collision-box",
    0x00000750: "null-object",
    0x00000800: "lightscape",
    0x00000900: "dazzle",
    0x00000901: "dazzle-name",
    0x00000902: "dazzle-type-name",
    0x00000A00: "sound-render-object",
    0x00000C00: "vertices-2",
    0x00000C01: "normals-2",
}

_CONTAINER_CHUNKS = frozenset(
    {
        0x00000000,
        0x00000023,
        0x00000024,
        0x00000025,
        0x00000026,
        0x0000002A,
        0x0000002B,
        0x00000030,
        0x00000031,
        0x00000038,
        0x00000048,
        0x00000050,
        0x00000051,
        0x00000090,
        0x00000100,
        0x00000200,
        0x00000280,
        0x00000500,
        0x00000700,
        0x00000702,
        0x00000705,
        0x00000706,
        0x00000900,
    }
)

_UNSUPPORTED_CHUNKS = frozenset(
    {
        0x00000058,
        0x00000080,
        0x000002C0,
        0x00000300,
        0x00000400,
        0x00000420,
        0x00000440,
        0x00000460,
        0x00000600,
        0x00000750,
        0x00000800,
        0x00000A00,
        0x00000C00,
        0x00000C01,
    }
)

_METADATA_CHUNKS = frozenset(
    {
        0x0000001F,
        0x00000028,
        0x00000029,
        0x0000002C,
        0x0000002D,
        0x0000002E,
        0x0000002F,
        0x00000032,
        0x00000033,
        0x00000039,
        0x0000003A,
        0x0000003F,
        0x00000049,
        0x00000052,
        0x00000053,
        0x00000101,
        0x00000102,
        0x00000201,
        0x00000281,
        0x00000701,
        0x00000703,
        0x00000704,
        0x00000740,
    }
)

_SHADER_FIELD_NAMES = (
    "depthCompare",
    "depthMask",
    "colorMask",
    "destinationBlend",
    "fogFunction",
    "primaryGradient",
    "secondaryGradient",
    "sourceBlend",
    "texturing",
    "detailColorFunction",
    "detailAlphaFunction",
    "shaderPreset",
    "alphaTest",
    "postDetailColorFunction",
    "postDetailAlphaFunction",
    "padding",
)

_PROPERTY_TYPE_NAMES = {
    1: "string",
    2: "float",
    3: "vector2",
    4: "vector3",
    5: "vector4",
    6: "integer",
    7: "boolean",
}


class W3DMetadataLimitError(ValueError):
    """Raised when caller input exceeds a deterministic scanner bound."""


@dataclass(frozen=True, slots=True)
class W3DSourceProvenance:
    virtual_path: str
    chunk_id: int
    chunk_name: str | None
    chunk_header_offset: int
    chunk_payload_offset: int
    chunk_payload_size: int
    value_offset: int
    value_size: int
    parent_chunk_header_offset: int | None

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "virtualPath": self.virtual_path,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "chunkHeaderOffset": self.chunk_header_offset,
            "chunkPayloadOffset": self.chunk_payload_offset,
            "chunkPayloadSize": self.chunk_payload_size,
            "valueOffset": self.value_offset,
            "valueSize": self.value_size,
        }
        if self.chunk_name is not None:
            value["chunkName"] = self.chunk_name
        if self.parent_chunk_header_offset is not None:
            value["parentChunkHeaderOffset"] = self.parent_chunk_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DChunkMetadata:
    chunk_id: int
    chunk_name: str | None
    header_offset: int
    payload_offset: int
    declared_payload_size: int
    available_payload_size: int
    has_subchunks_flag: bool
    scanned_as_container: bool
    depth: int
    parent_header_offset: int | None
    classification: str

    @property
    def truncated(self) -> bool:
        return self.available_payload_size < self.declared_payload_size

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "headerOffset": self.header_offset,
            "payloadOffset": self.payload_offset,
            "declaredPayloadSize": self.declared_payload_size,
            "availablePayloadSize": self.available_payload_size,
            "hasSubchunksFlag": self.has_subchunks_flag,
            "scannedAsContainer": self.scanned_as_container,
            "depth": self.depth,
            "classification": self.classification,
            "truncated": self.truncated,
        }
        if self.chunk_name is not None:
            value["chunkName"] = self.chunk_name
        if self.parent_header_offset is not None:
            value["parentHeaderOffset"] = self.parent_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DMetadataWarning:
    code: str
    message: str
    offset: int
    chunk_id: int | None = None
    parent_chunk_header_offset: int | None = None

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "code": self.code,
            "message": self.message,
            "offset": self.offset,
        }
        if self.chunk_id is not None:
            value["chunkId"] = self.chunk_id
            value["chunkIdHex"] = f"0x{self.chunk_id:08X}"
        if self.parent_chunk_header_offset is not None:
            value["parentChunkHeaderOffset"] = self.parent_chunk_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DMeshHeader:
    identifier: str
    mesh_name: str
    container_name: str
    version_major: int
    version_minor: int
    attributes: int
    face_count: int
    vertex_count: int
    material_count: int
    damage_stage_count: int
    sort_level: int
    vertex_channel_flags: int
    face_channel_flags: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "meshName": self.mesh_name,
            "containerName": self.container_name,
            "version": [self.version_major, self.version_minor],
            "attributes": self.attributes,
            "faceCount": self.face_count,
            "vertexCount": self.vertex_count,
            "materialCount": self.material_count,
            "damageStageCount": self.damage_stage_count,
            "sortLevel": self.sort_level,
            "vertexChannelFlags": self.vertex_channel_flags,
            "faceChannelFlags": self.face_channel_flags,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DHierarchyHeader:
    identifier: str
    version_major: int
    version_minor: int
    pivot_count: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "version": [self.version_major, self.version_minor],
            "pivotCount": self.pivot_count,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DHierarchyPivot:
    identifier: str
    index: int
    parent_index: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "index": self.index,
            "parentIndex": self.parent_index,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DAnimationHeader:
    identifier: str
    hierarchy_identifier: str
    version_major: int
    version_minor: int
    frame_count: int
    frame_rate: int
    compressed: bool
    flavor: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "identifier": self.identifier,
            "hierarchyIdentifier": self.hierarchy_identifier,
            "version": [self.version_major, self.version_minor],
            "frameCount": self.frame_count,
            "frameRate": self.frame_rate,
            "compressed": self.compressed,
            "provenance": self.provenance.neutral(),
        }
        if self.flavor is not None:
            value["flavor"] = self.flavor
        return value


@dataclass(frozen=True, slots=True)
class W3DModelHeader:
    identifier: str
    hierarchy_identifier: str
    version_major: int
    version_minor: int
    lod_count: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "hierarchyIdentifier": self.hierarchy_identifier,
            "version": [self.version_major, self.version_minor],
            "lodCount": self.lod_count,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DModelReference:
    identifier: str
    bone_index: int
    role: str
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "boneIndex": self.bone_index,
            "role": self.role,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DCollisionBox:
    identifier: str
    version_major: int
    version_minor: int
    flags: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "version": [self.version_major, self.version_minor],
            "flags": self.flags,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DTextureReference:
    identifier: str
    texture_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "identifier": self.identifier,
            "provenance": self.provenance.neutral(),
        }
        if self.texture_chunk_header_offset is not None:
            value["textureChunkHeaderOffset"] = self.texture_chunk_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DTextureInfo:
    attributes: int
    animation_type: int
    frame_count: int
    frame_rate: float
    texture_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "attributes": self.attributes,
            "animationType": self.animation_type,
            "frameCount": self.frame_count,
            "frameRate": self.frame_rate,
            "provenance": self.provenance.neutral(),
        }
        if self.texture_chunk_header_offset is not None:
            value["textureChunkHeaderOffset"] = self.texture_chunk_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DMaterialInfo:
    pass_count: int
    vertex_material_count: int
    shader_count: int
    texture_count: int
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "passCount": self.pass_count,
            "vertexMaterialCount": self.vertex_material_count,
            "shaderCount": self.shader_count,
            "textureCount": self.texture_count,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DMaterialString:
    kind: str
    value: str
    material_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "kind": self.kind,
            "value": self.value,
            "provenance": self.provenance.neutral(),
        }
        if self.material_chunk_header_offset is not None:
            result["materialChunkHeaderOffset"] = self.material_chunk_header_offset
        return result


@dataclass(frozen=True, slots=True)
class W3DVertexMaterialInfo:
    attributes: int
    ambient: tuple[int, int, int, int]
    diffuse: tuple[int, int, int, int]
    specular: tuple[int, int, int, int]
    emissive: tuple[int, int, int, int]
    shininess: float
    opacity: float
    translucency: float
    material_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "attributes": self.attributes,
            "ambient": list(self.ambient),
            "diffuse": list(self.diffuse),
            "specular": list(self.specular),
            "emissive": list(self.emissive),
            "shininess": self.shininess,
            "opacity": self.opacity,
            "translucency": self.translucency,
            "provenance": self.provenance.neutral(),
        }
        if self.material_chunk_header_offset is not None:
            value["materialChunkHeaderOffset"] = self.material_chunk_header_offset
        return value


@dataclass(frozen=True, slots=True)
class W3DShader:
    index: int
    fields: tuple[tuple[str, int], ...]
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "index": self.index,
            "fields": {name: value for name, value in self.fields},
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DShaderMaterialHeader:
    version: int
    type_name: str
    technique: int
    material_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "version": self.version,
            "typeName": self.type_name,
            "technique": self.technique,
            "provenance": self.provenance.neutral(),
        }
        if self.material_chunk_header_offset is not None:
            value["materialChunkHeaderOffset"] = self.material_chunk_header_offset
        return value


ShaderPropertyValue = str | int | float | bool | tuple[float, ...] | None


@dataclass(frozen=True, slots=True)
class W3DShaderMaterialProperty:
    property_type: int
    property_type_name: str | None
    name: str | None
    value: ShaderPropertyValue
    material_chunk_header_offset: int | None
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        encoded_value: object = (
            list(self.value) if isinstance(self.value, tuple) else self.value
        )
        result: dict[str, object] = {
            "propertyType": self.property_type,
            "name": self.name,
            "value": encoded_value,
            "provenance": self.provenance.neutral(),
        }
        if self.property_type_name is not None:
            result["propertyTypeName"] = self.property_type_name
        if self.material_chunk_header_offset is not None:
            result["materialChunkHeaderOffset"] = self.material_chunk_header_offset
        return result


@dataclass(frozen=True, slots=True)
class W3DIndexReference:
    kind: str
    indices: tuple[int, ...]
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "indices": list(self.indices),
            "provenance": self.provenance.neutral(),
        }


def _nonempty(values: Iterable[str]) -> tuple[str, ...]:
    # Preserve duplicates as evidence.  build_w3d_index deliberately rejects a
    # duplicate logical header inside one file, so silently coalescing it here
    # would weaken that fail-closed contract.
    return tuple(value for value in values if value)


@dataclass(frozen=True, slots=True)
class W3DMetadata:
    virtual_path: str
    byte_length: int
    source_sha256: str
    chunks: tuple[W3DChunkMetadata, ...]
    mesh_headers: tuple[W3DMeshHeader, ...]
    model_headers: tuple[W3DModelHeader, ...]
    hierarchy_headers: tuple[W3DHierarchyHeader, ...]
    hierarchy_pivots: tuple[W3DHierarchyPivot, ...]
    animation_headers: tuple[W3DAnimationHeader, ...]
    model_references: tuple[W3DModelReference, ...]
    collision_boxes: tuple[W3DCollisionBox, ...]
    texture_references: tuple[W3DTextureReference, ...]
    texture_infos: tuple[W3DTextureInfo, ...]
    material_infos: tuple[W3DMaterialInfo, ...]
    material_strings: tuple[W3DMaterialString, ...]
    vertex_material_infos: tuple[W3DVertexMaterialInfo, ...]
    shaders: tuple[W3DShader, ...]
    shader_material_headers: tuple[W3DShaderMaterialHeader, ...]
    shader_material_properties: tuple[W3DShaderMaterialProperty, ...]
    index_references: tuple[W3DIndexReference, ...]
    warnings: tuple[W3DMetadataWarning, ...]

    @property
    def model_ids(self) -> tuple[str, ...]:
        records = [
            (header.provenance.value_offset, header.identifier)
            for header in self.model_headers
        ] + [
            (header.provenance.value_offset, header.identifier)
            for header in self.mesh_headers
        ]
        records.sort(key=lambda item: item[0])
        return _nonempty(identifier for _, identifier in records)

    @property
    def hierarchy_ids(self) -> tuple[str, ...]:
        return _nonempty(
            header.identifier for header in self.hierarchy_headers
        )

    @property
    def animation_ids(self) -> tuple[str, ...]:
        return _nonempty(
            header.identifier for header in self.animation_headers
        )

    def file_headers(self) -> W3DFileHeaders:
        """Return exact logical IDs for :func:`build_w3d_index`.

        SAGE animation references occur in both raw ``AnimationName`` form and
        ``Hierarchy.AnimationName`` form.  Both components are authored in the
        W3D animation header, so expose both deterministic spellings rather
        than making a later pass guess from physical filenames.
        """

        animation_ids: list[str] = []
        for header in self.animation_headers:
            animation_ids.append(header.identifier)
            hierarchy = header.hierarchy_identifier
            if (
                hierarchy
                and not header.identifier.casefold().startswith(
                    hierarchy.casefold() + "."
                )
            ):
                animation_ids.append(f"{hierarchy}.{header.identifier}")

        return W3DFileHeaders(
            virtual_path=self.virtual_path,
            model_ids=self.model_ids,
            animation_ids=tuple(animation_ids),
            hierarchy_ids=self.hierarchy_ids,
        )

    def neutral(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-metadata",
            "schemaVersion": 0,
            "virtualPath": self.virtual_path,
            "byteLength": self.byte_length,
            "sourceSha256": self.source_sha256,
            "headerIds": {
                "models": list(self.model_ids),
                "hierarchies": list(self.hierarchy_ids),
                "animations": list(self.animation_ids),
            },
            "chunks": [item.neutral() for item in self.chunks],
            "meshHeaders": [item.neutral() for item in self.mesh_headers],
            "modelHeaders": [item.neutral() for item in self.model_headers],
            "hierarchyHeaders": [
                item.neutral() for item in self.hierarchy_headers
            ],
            "hierarchyPivots": [item.neutral() for item in self.hierarchy_pivots],
            "animationHeaders": [
                item.neutral() for item in self.animation_headers
            ],
            "modelReferences": [item.neutral() for item in self.model_references],
            "collisionBoxes": [item.neutral() for item in self.collision_boxes],
            "textureReferences": [
                item.neutral() for item in self.texture_references
            ],
            "textureInfos": [item.neutral() for item in self.texture_infos],
            "materialInfos": [item.neutral() for item in self.material_infos],
            "materialStrings": [
                item.neutral() for item in self.material_strings
            ],
            "vertexMaterialInfos": [
                item.neutral() for item in self.vertex_material_infos
            ],
            "shaders": [item.neutral() for item in self.shaders],
            "shaderMaterialHeaders": [
                item.neutral() for item in self.shader_material_headers
            ],
            "shaderMaterialProperties": [
                item.neutral() for item in self.shader_material_properties
            ],
            "indexReferences": [
                item.neutral() for item in self.index_references
            ],
            "warnings": [item.neutral() for item in self.warnings],
        }


@dataclass(frozen=True, slots=True)
class _Chunk:
    chunk_id: int
    header_offset: int
    payload_offset: int
    declared_size: int
    available_size: int
    parent_header_offset: int | None
    parent_chunk_id: int | None


def _normalized_w3d_path(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("W3D metadata virtual path must be a string")
    if len(value) > MAX_W3D_METADATA_PATH_LENGTH:
        raise ValueError(
            "W3D metadata virtual path exceeds "
            f"{MAX_W3D_METADATA_PATH_LENGTH} character limit"
        )
    try:
        normalized = "/".join(safe_relative_parts(value))
    except ValueError as exc:
        raise ValueError(f"unsafe W3D metadata virtual path: {value!r}") from exc
    if not normalized.casefold().endswith(".w3d"):
        raise ValueError("W3D metadata virtual path must end in .w3d")
    return normalized


def _version(raw: int) -> tuple[int, int]:
    return raw >> 16, raw & 0xFFFF


def _fixed_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("cp1252")


class _Scanner:
    def __init__(self, source: bytes, virtual_path: str):
        self.source = source
        self.virtual_path = virtual_path
        self.chunks: list[W3DChunkMetadata] = []
        self.mesh_headers: list[W3DMeshHeader] = []
        self.model_headers: list[W3DModelHeader] = []
        self.hierarchy_headers: list[W3DHierarchyHeader] = []
        self.hierarchy_pivots: list[W3DHierarchyPivot] = []
        self.animation_headers: list[W3DAnimationHeader] = []
        self.model_references: list[W3DModelReference] = []
        self.collision_boxes: list[W3DCollisionBox] = []
        self.texture_references: list[W3DTextureReference] = []
        self.texture_infos: list[W3DTextureInfo] = []
        self.material_infos: list[W3DMaterialInfo] = []
        self.material_strings: list[W3DMaterialString] = []
        self.vertex_material_infos: list[W3DVertexMaterialInfo] = []
        self.shaders: list[W3DShader] = []
        self.shader_material_headers: list[W3DShaderMaterialHeader] = []
        self.shader_material_properties: list[W3DShaderMaterialProperty] = []
        self.index_references: list[W3DIndexReference] = []
        self.warnings: list[W3DMetadataWarning] = []

    def warning(
        self,
        code: str,
        message: str,
        offset: int,
        chunk: _Chunk | None = None,
    ) -> None:
        self.warnings.append(
            W3DMetadataWarning(
                code,
                message,
                offset,
                chunk.chunk_id if chunk is not None else None,
                chunk.parent_header_offset if chunk is not None else None,
            )
        )

    def provenance(
        self,
        chunk: _Chunk,
        *,
        value_offset: int | None = None,
        value_size: int | None = None,
    ) -> W3DSourceProvenance:
        offset = chunk.payload_offset if value_offset is None else value_offset
        size = chunk.available_size if value_size is None else value_size
        return W3DSourceProvenance(
            self.virtual_path,
            chunk.chunk_id,
            _CHUNK_NAMES.get(chunk.chunk_id),
            chunk.header_offset,
            chunk.payload_offset,
            chunk.available_size,
            offset,
            size,
            chunk.parent_header_offset,
        )

    def require(self, chunk: _Chunk, size: int, label: str) -> bool:
        if chunk.available_size >= size and chunk.declared_size >= size:
            return True
        self.warning(
            "truncated-metadata-record",
            f"{label} requires {size} payload bytes; "
            f"{min(chunk.available_size, chunk.declared_size)} available",
            chunk.payload_offset,
            chunk,
        )
        return False

    def variable_string(self, chunk: _Chunk, label: str) -> str:
        raw = self.source[
            chunk.payload_offset : chunk.payload_offset + chunk.available_size
        ]
        terminator = raw.find(b"\0")
        if terminator < 0:
            self.warning(
                "unterminated-string",
                f"{label} has no NUL terminator inside its chunk",
                chunk.payload_offset,
                chunk,
            )
            return raw.decode("cp1252")
        trailing = raw[terminator + 1 :]
        if any(trailing):
            self.warning(
                "trailing-string-bytes",
                f"{label} has non-NUL bytes after its terminator",
                chunk.payload_offset + terminator + 1,
                chunk,
            )
        return raw[:terminator].decode("cp1252")

    def counted_string(
        self, chunk: _Chunk, position: int, count: int, label: str
    ) -> tuple[str | None, int]:
        end = chunk.payload_offset + chunk.available_size
        if count <= 0:
            self.warning(
                "invalid-string-length",
                f"{label} declares non-positive byte count {count}",
                position,
                chunk,
            )
            return None, position
        if position + count > end:
            self.warning(
                "truncated-metadata-record",
                f"{label} declares {count} bytes beyond its chunk boundary",
                position,
                chunk,
            )
            return None, end
        raw = self.source[position : position + count]
        terminator = raw.find(b"\0")
        if terminator < 0:
            self.warning(
                "unterminated-string",
                f"{label} has no NUL terminator in its declared byte count",
                position,
                chunk,
            )
            return raw.decode("cp1252"), position + count
        if terminator != count - 1 and any(raw[terminator + 1 :]):
            self.warning(
                "trailing-string-bytes",
                f"{label} contains non-NUL bytes after its terminator",
                position + terminator + 1,
                chunk,
            )
        return raw[:terminator].decode("cp1252"), position + count

    def scan_region(
        self,
        start: int,
        end: int,
        *,
        depth: int,
        parent_header_offset: int | None,
        parent_chunk_id: int | None,
    ) -> None:
        if depth > MAX_W3D_METADATA_DEPTH:
            self.warning(
                "nesting-limit",
                f"chunk nesting exceeds {MAX_W3D_METADATA_DEPTH}",
                start,
            )
            return
        position = start
        while position < end:
            remaining = end - position
            if remaining < _CHUNK_HEADER_SIZE:
                self.warning(
                    "truncated-chunk-header",
                    f"{remaining} trailing byte(s) cannot form a chunk header",
                    position,
                )
                break
            if len(self.chunks) >= MAX_W3D_METADATA_CHUNKS:
                raise W3DMetadataLimitError(
                    "W3D metadata chunk count exceeds "
                    f"{MAX_W3D_METADATA_CHUNKS}"
                )

            chunk_id, raw_size = struct.unpack_from("<II", self.source, position)
            declared_size = raw_size & _SIZE_MASK
            has_subchunks = bool(raw_size & _SUBCHUNK_FLAG)
            payload_offset = position + _CHUNK_HEADER_SIZE
            available_size = min(declared_size, end - payload_offset)
            chunk = _Chunk(
                chunk_id,
                position,
                payload_offset,
                declared_size,
                available_size,
                parent_header_offset,
                parent_chunk_id,
            )
            # Retail emitter roots are containers only when the W3D
            # subchunk flag says their payload is partitioned.  Preserve the
            # historical unsupported diagnosis for unflagged opaque 0x500
            # payloads instead of recursively interpreting arbitrary bytes.
            opaque_emitter = chunk_id == 0x00000500 and not has_subchunks
            known_container = chunk_id in _CONTAINER_CHUNKS and not opaque_emitter
            scan_children = known_container or has_subchunks
            if chunk_id in _METADATA_CHUNKS:
                classification = "metadata"
            elif chunk_id in _UNSUPPORTED_CHUNKS or opaque_emitter:
                classification = "unsupported"
            elif chunk_id in _CHUNK_NAMES:
                classification = "container" if scan_children else "known-data"
            else:
                classification = "unknown"
            self.chunks.append(
                W3DChunkMetadata(
                    chunk_id,
                    _CHUNK_NAMES.get(chunk_id),
                    position,
                    payload_offset,
                    declared_size,
                    available_size,
                    has_subchunks,
                    scan_children,
                    depth,
                    parent_header_offset,
                    classification,
                )
            )

            if declared_size > available_size:
                self.warning(
                    "truncated-chunk-payload",
                    f"chunk declares {declared_size} payload bytes; "
                    f"only {available_size} remain in its parent",
                    position,
                    chunk,
                )
            if classification == "unsupported":
                self.warning(
                    "unsupported-chunk",
                    f"metadata scanner does not interpret {_CHUNK_NAMES[chunk_id]}",
                    position,
                    chunk,
                )
            elif classification == "unknown":
                self.warning(
                    "unknown-chunk",
                    f"unrecognized W3D chunk 0x{chunk_id:08X}",
                    position,
                    chunk,
                )

            self.extract(chunk)
            payload_end = payload_offset + available_size
            if scan_children and available_size:
                self.scan_region(
                    payload_offset,
                    payload_end,
                    depth=depth + 1,
                    parent_header_offset=position,
                    parent_chunk_id=chunk_id,
                )
            position = payload_end
            if declared_size > available_size:
                break

    def extract(self, chunk: _Chunk) -> None:
        chunk_id = chunk.chunk_id
        if chunk_id == 0x0000001F:
            self.extract_mesh_header(chunk)
        elif chunk_id == 0x00000101:
            self.extract_hierarchy_header(chunk)
        elif chunk_id == 0x00000102:
            self.extract_pivots(chunk)
        elif chunk_id in {0x00000201, 0x00000281}:
            self.extract_animation_header(chunk)
        elif chunk_id == 0x00000701:
            self.extract_model_header(chunk)
        elif chunk_id == 0x00000704:
            self.extract_model_reference(chunk)
        elif chunk_id == 0x00000740:
            self.extract_collision_box(chunk)
        elif chunk_id == 0x00000032:
            identifier = self.variable_string(chunk, "texture name")
            self.texture_references.append(
                W3DTextureReference(
                    identifier,
                    chunk.parent_header_offset,
                    self.provenance(chunk),
                )
            )
        elif chunk_id == 0x00000033:
            self.extract_texture_info(chunk)
        elif chunk_id == 0x00000028:
            self.extract_material_info(chunk)
        elif chunk_id in {0x0000002C, 0x0000002E, 0x0000002F}:
            kind = {
                0x0000002C: "vertex-material-name",
                0x0000002E: "vertex-mapper-args-0",
                0x0000002F: "vertex-mapper-args-1",
            }[chunk_id]
            self.material_strings.append(
                W3DMaterialString(
                    kind,
                    self.variable_string(chunk, kind),
                    chunk.parent_header_offset,
                    self.provenance(chunk),
                )
            )
        elif chunk_id == 0x0000002D:
            self.extract_vertex_material_info(chunk)
        elif chunk_id == 0x00000029:
            self.extract_shaders(chunk)
        elif chunk_id == 0x00000052:
            self.extract_shader_material_header(chunk)
        elif chunk_id == 0x00000053:
            self.extract_shader_material_property(chunk)
        elif chunk_id in {
            0x00000039,
            0x0000003A,
            0x0000003F,
            0x00000049,
        }:
            self.extract_index_reference(chunk)

    def extract_mesh_header(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 116, "mesh header"):
            return
        values = struct.unpack_from("<II16s16s9I10f", self.source, chunk.payload_offset)
        major, minor = _version(values[0])
        mesh_name = _fixed_string(values[2])
        container_name = _fixed_string(values[3])
        if container_name and mesh_name:
            identifier = f"{container_name}.{mesh_name}"
        else:
            identifier = container_name or mesh_name
        if not identifier:
            self.warning(
                "empty-identifier",
                "mesh header has no mesh or container identifier",
                chunk.payload_offset + 8,
                chunk,
            )
        counts = values[4:13]
        self.mesh_headers.append(
            W3DMeshHeader(
                identifier,
                mesh_name,
                container_name,
                major,
                minor,
                values[1],
                counts[0],
                counts[1],
                counts[2],
                counts[3],
                counts[4],
                counts[7],
                counts[8],
                self.provenance(chunk, value_size=116),
            )
        )

    def extract_hierarchy_header(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 36, "hierarchy header"):
            return
        raw_version, raw_name, pivot_count = struct.unpack_from(
            "<I16sI", self.source, chunk.payload_offset
        )
        major, minor = _version(raw_version)
        identifier = _fixed_string(raw_name)
        if not identifier:
            self.warning(
                "empty-identifier",
                "hierarchy header has an empty identifier",
                chunk.payload_offset + 4,
                chunk,
            )
        self.hierarchy_headers.append(
            W3DHierarchyHeader(
                identifier,
                major,
                minor,
                pivot_count,
                self.provenance(chunk, value_size=36),
            )
        )

    def extract_pivots(self, chunk: _Chunk) -> None:
        record_size = 60
        count, remainder = divmod(chunk.available_size, record_size)
        for index in range(count):
            offset = chunk.payload_offset + index * record_size
            raw_name, parent_index = struct.unpack_from("<16si", self.source, offset)
            self.hierarchy_pivots.append(
                W3DHierarchyPivot(
                    _fixed_string(raw_name),
                    index,
                    parent_index,
                    self.provenance(
                        chunk, value_offset=offset, value_size=record_size
                    ),
                )
            )
        if remainder:
            self.warning(
                "truncated-record-tail",
                f"pivots chunk has {remainder} byte(s) after {count} full records",
                chunk.payload_offset + count * record_size,
                chunk,
            )

    def extract_animation_header(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 44, "animation header"):
            return
        raw_version, raw_name, raw_hierarchy, frame_count = struct.unpack_from(
            "<I16s16sI", self.source, chunk.payload_offset
        )
        if chunk.chunk_id == 0x00000281:
            frame_rate, flavor = struct.unpack_from(
                "<HH", self.source, chunk.payload_offset + 40
            )
            compressed = True
        else:
            frame_rate = struct.unpack_from(
                "<I", self.source, chunk.payload_offset + 40
            )[0]
            flavor = None
            compressed = False
        major, minor = _version(raw_version)
        identifier = _fixed_string(raw_name)
        hierarchy = _fixed_string(raw_hierarchy)
        if not identifier:
            self.warning(
                "empty-identifier",
                "animation header has an empty identifier",
                chunk.payload_offset + 4,
                chunk,
            )
        self.animation_headers.append(
            W3DAnimationHeader(
                identifier,
                hierarchy,
                major,
                minor,
                frame_count,
                frame_rate,
                compressed,
                flavor,
                self.provenance(chunk, value_size=44),
            )
        )

    def extract_model_header(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 40, "HLOD header"):
            return
        raw_version, lod_count, raw_name, raw_hierarchy = struct.unpack_from(
            "<II16s16s", self.source, chunk.payload_offset
        )
        major, minor = _version(raw_version)
        identifier = _fixed_string(raw_name)
        hierarchy = _fixed_string(raw_hierarchy)
        if not identifier:
            self.warning(
                "empty-identifier",
                "HLOD header has an empty model identifier",
                chunk.payload_offset + 8,
                chunk,
            )
        self.model_headers.append(
            W3DModelHeader(
                identifier,
                hierarchy,
                major,
                minor,
                lod_count,
                self.provenance(chunk, value_size=40),
            )
        )

    def extract_model_reference(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 36, "HLOD sub-object"):
            return
        bone_index, raw_identifier = struct.unpack_from(
            "<I32s", self.source, chunk.payload_offset
        )
        role = {
            0x00000702: "lod",
            0x00000705: "aggregate",
            0x00000706: "proxy",
        }.get(chunk.parent_chunk_id, "unclassified")
        self.model_references.append(
            W3DModelReference(
                _fixed_string(raw_identifier),
                bone_index,
                role,
                self.provenance(chunk, value_size=36),
            )
        )

    def extract_collision_box(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 68, "collision box"):
            return
        raw_version, flags, raw_name = struct.unpack_from(
            "<II32s", self.source, chunk.payload_offset
        )
        major, minor = _version(raw_version)
        self.collision_boxes.append(
            W3DCollisionBox(
                _fixed_string(raw_name),
                major,
                minor,
                flags,
                self.provenance(chunk, value_size=68),
            )
        )

    def extract_texture_info(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 12, "texture info"):
            return
        attributes, animation_type, frame_count, frame_rate = struct.unpack_from(
            "<HHIf", self.source, chunk.payload_offset
        )
        self.texture_infos.append(
            W3DTextureInfo(
                attributes,
                animation_type,
                frame_count,
                frame_rate,
                chunk.parent_header_offset,
                self.provenance(chunk, value_size=12),
            )
        )

    def extract_material_info(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 16, "material info"):
            return
        values = struct.unpack_from("<4I", self.source, chunk.payload_offset)
        self.material_infos.append(
            W3DMaterialInfo(*values, self.provenance(chunk, value_size=16))
        )

    def extract_vertex_material_info(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 32, "vertex material info"):
            return
        values = struct.unpack_from("<i4s4s4s4sfff", self.source, chunk.payload_offset)
        colors = [tuple(raw) for raw in values[1:5]]
        self.vertex_material_infos.append(
            W3DVertexMaterialInfo(
                values[0],
                colors[0],  # type: ignore[arg-type]
                colors[1],  # type: ignore[arg-type]
                colors[2],  # type: ignore[arg-type]
                colors[3],  # type: ignore[arg-type]
                values[5],
                values[6],
                values[7],
                chunk.parent_header_offset,
                self.provenance(chunk, value_size=32),
            )
        )

    def extract_shaders(self, chunk: _Chunk) -> None:
        record_size = 16
        count, remainder = divmod(chunk.available_size, record_size)
        for index in range(count):
            offset = chunk.payload_offset + index * record_size
            values = struct.unpack_from("<16B", self.source, offset)
            self.shaders.append(
                W3DShader(
                    index,
                    tuple(zip(_SHADER_FIELD_NAMES, values)),
                    self.provenance(
                        chunk, value_offset=offset, value_size=record_size
                    ),
                )
            )
        if remainder:
            self.warning(
                "truncated-record-tail",
                f"shader chunk has {remainder} byte(s) after {count} full records",
                chunk.payload_offset + count * record_size,
                chunk,
            )

    def extract_shader_material_header(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 37, "shader material header"):
            return
        version, raw_type, technique = struct.unpack_from(
            "<B32si", self.source, chunk.payload_offset
        )
        self.shader_material_headers.append(
            W3DShaderMaterialHeader(
                version,
                _fixed_string(raw_type),
                technique,
                chunk.parent_header_offset,
                self.provenance(chunk, value_size=37),
            )
        )

    def extract_shader_material_property(self, chunk: _Chunk) -> None:
        if not self.require(chunk, 8, "shader material property header"):
            return
        property_type, name_count = struct.unpack_from(
            "<ii", self.source, chunk.payload_offset
        )
        position = chunk.payload_offset + 8
        name, position = self.counted_string(
            chunk, position, name_count, "shader material property name"
        )
        end = chunk.payload_offset + chunk.available_size
        property_type_name = _PROPERTY_TYPE_NAMES.get(property_type)
        value: ShaderPropertyValue = None
        if name is not None:
            if property_type == 1:
                if position + 4 > end:
                    self.warning(
                        "truncated-metadata-record",
                        "shader string property lacks its value byte count",
                        position,
                        chunk,
                    )
                else:
                    value_count = struct.unpack_from("<i", self.source, position)[0]
                    value, position = self.counted_string(
                        chunk,
                        position + 4,
                        value_count,
                        "shader material string value",
                    )
            elif property_type in {2, 3, 4, 5}:
                element_count = property_type - 1
                value_size = element_count * 4
                if position + value_size > end:
                    self.warning(
                        "truncated-metadata-record",
                        f"shader {property_type_name} property lacks {value_size} bytes",
                        position,
                        chunk,
                    )
                else:
                    numbers = struct.unpack_from(
                        f"<{element_count}f", self.source, position
                    )
                    value = numbers[0] if element_count == 1 else tuple(numbers)
                    position += value_size
            elif property_type == 6:
                if position + 4 > end:
                    self.warning(
                        "truncated-metadata-record",
                        "shader integer property lacks 4 bytes",
                        position,
                        chunk,
                    )
                else:
                    value = struct.unpack_from("<i", self.source, position)[0]
                    position += 4
            elif property_type == 7:
                if position + 1 > end:
                    self.warning(
                        "truncated-metadata-record",
                        "shader boolean property lacks 1 byte",
                        position,
                        chunk,
                    )
                else:
                    value = bool(self.source[position])
                    position += 1
            else:
                self.warning(
                    "unsupported-shader-property-type",
                    f"unsupported shader property type {property_type}",
                    chunk.payload_offset,
                    chunk,
                )
        self.shader_material_properties.append(
            W3DShaderMaterialProperty(
                property_type,
                property_type_name,
                name,
                value,
                chunk.parent_header_offset,
                self.provenance(chunk),
            )
        )

    def extract_index_reference(self, chunk: _Chunk) -> None:
        kind, signed = {
            0x00000039: ("vertex-material", False),
            0x0000003A: ("shader", False),
            0x0000003F: ("shader-material", False),
            0x00000049: ("texture", True),
        }[chunk.chunk_id]
        count, remainder = divmod(chunk.available_size, 4)
        if count:
            code = "i" if signed else "I"
            indices = struct.unpack_from(
                f"<{count}{code}", self.source, chunk.payload_offset
            )
        else:
            indices = ()
        self.index_references.append(
            W3DIndexReference(kind, tuple(indices), self.provenance(chunk))
        )
        if remainder:
            self.warning(
                "truncated-record-tail",
                f"{kind} index chunk has {remainder} trailing byte(s)",
                chunk.payload_offset + count * 4,
                chunk,
            )

    def validate_header_counts(self) -> None:
        for header in self.hierarchy_headers:
            owner = header.provenance.parent_chunk_header_offset
            actual = sum(
                1
                for pivot in self.hierarchy_pivots
                if pivot.provenance.parent_chunk_header_offset == owner
            )
            if actual != header.pivot_count:
                self.warning(
                    "count-mismatch",
                    f"hierarchy header declares {header.pivot_count} pivots; "
                    f"{actual} complete pivot records were scanned",
                    header.provenance.value_offset,
                )

    def result(self) -> W3DMetadata:
        self.validate_header_counts()
        warnings = tuple(
            sorted(
                self.warnings,
                key=lambda item: (
                    item.offset,
                    item.code,
                    item.chunk_id if item.chunk_id is not None else -1,
                    item.message,
                ),
            )
        )
        return W3DMetadata(
            self.virtual_path,
            len(self.source),
            hashlib.sha256(self.source).hexdigest(),
            tuple(self.chunks),
            tuple(self.mesh_headers),
            tuple(self.model_headers),
            tuple(self.hierarchy_headers),
            tuple(self.hierarchy_pivots),
            tuple(self.animation_headers),
            tuple(self.model_references),
            tuple(self.collision_boxes),
            tuple(self.texture_references),
            tuple(self.texture_infos),
            tuple(self.material_infos),
            tuple(self.material_strings),
            tuple(self.vertex_material_infos),
            tuple(self.shaders),
            tuple(self.shader_material_headers),
            tuple(self.shader_material_properties),
            tuple(self.index_references),
            warnings,
        )


def scan_w3d_metadata(source: bytes, virtual_path: str) -> W3DMetadata:
    """Scan one caller-owned W3D byte string without filesystem access.

    Structural damage is reported in ``result.warnings`` so a census can retain
    all evidence.  Type, containment, and hard resource-limit violations raise
    immediately because no trustworthy bounded scan can proceed.
    """

    if not isinstance(source, bytes):
        raise TypeError("W3D metadata source must be bytes")
    if len(source) > MAX_W3D_METADATA_BYTES:
        raise W3DMetadataLimitError(
            f"W3D metadata source exceeds {MAX_W3D_METADATA_BYTES} byte limit"
        )
    path = _normalized_w3d_path(virtual_path)
    scanner = _Scanner(source, path)
    if not source:
        scanner.warning("empty-source", "W3D source is empty", 0)
    else:
        scanner.scan_region(
            0,
            len(source),
            depth=0,
            parent_header_offset=None,
            parent_chunk_id=None,
        )
    return scanner.result()


# Short alias for callers that already operate in a W3D-specific module.
scan_metadata = scan_w3d_metadata
