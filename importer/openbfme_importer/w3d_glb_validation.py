"""Independent, aggregate-only semantic validation for exported W3D GLBs.

This validator intentionally does not retain authored names or return per-object
inventories.  Geometry metrics describe the exported glTF representation:
``vertex_count`` is the sum of POSITION accessor counts across TRIANGLES
primitives, so it can exceed Blender's pre-export vertex count when export
splits vertices or material primitives.  Expected counts are therefore an
explicit, selective contract; every supplied count is compared exactly.

The W3D Blender path does not emit sparse accessors or mesh-compression
extensions.  They are rejected rather than accepted without independently
reconstructing their geometry.
"""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass, fields
import json
import math
import mmap
from pathlib import Path
import struct
from typing import Any, Mapping


_GLB_HEADER = struct.Struct("<4sII")
_CHUNK_HEADER = struct.Struct("<II")
_JSON_CHUNK = 0x4E4F534A
_BIN_CHUNK = 0x004E4942
_MAX_JSON_BYTES = 64 * 1024 * 1024
_MAX_DATA_URI_BYTES = 64 * 1024 * 1024

_COMPONENT_FORMATS = {
    5120: ("b", 1),
    5121: ("B", 1),
    5122: ("h", 2),
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}
_TYPE_SHAPES = {
    "SCALAR": (1, 1),
    "VEC2": (1, 2),
    "VEC3": (1, 3),
    "VEC4": (1, 4),
    "MAT2": (2, 2),
    "MAT3": (3, 3),
    "MAT4": (4, 4),
}
_ADAPTER_COUNT_ALIASES = {
    "meshes": "mesh_count",
    "materials": "material_count",
    "images": "image_count",
    "skeletons": "skin_count",
    "animations": "animation_count",
    "vertices": "vertex_count",
    "triangles": "triangle_count",
    "skinned_meshes": "skinned_mesh_node_count",
    "bones": "joint_count",
}


class W3DGLBValidationError(ValueError):
    """The GLB container or its aggregate W3D semantics failed closed."""


class W3DGLBSemanticMismatchError(W3DGLBValidationError):
    """A validated aggregate did not equal an explicitly expected count."""


@dataclass(frozen=True, slots=True)
class W3DGLBSemanticSummary:
    """Immutable, path-free and name-free evidence about one exported GLB."""

    mesh_count: int
    primitive_count: int
    material_count: int
    image_count: int
    skin_count: int
    gltf_animation_count: int
    visibility_only_animation_count: int
    animation_count: int
    vertex_count: int
    triangle_count: int
    skinned_mesh_node_count: int
    joint_count: int


@dataclass(frozen=True, slots=True)
class _Buffer:
    source: bytes | mmap.mmap
    source_offset: int
    byte_length: int


@dataclass(frozen=True, slots=True)
class _BufferView:
    buffer: int
    byte_offset: int
    byte_length: int
    byte_stride: int | None


@dataclass(frozen=True, slots=True)
class _Accessor:
    buffer_view: int
    byte_offset: int
    component_type: int
    count: int
    value_type: str
    element_size: int
    component_size: int


def _fail(message: str) -> W3DGLBValidationError:
    return W3DGLBValidationError(message)


def _plain_int(value: Any, label: str, *, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise _fail(f"GLB {label} is invalid")
    return value


def _array(document: Mapping[str, Any], key: str) -> list[Any]:
    value = document.get(key, [])
    if not isinstance(value, list):
        raise _fail(f"GLB {key} array is invalid")
    return value


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise _fail(f"GLB {label} is invalid")
    return value


def _reference(value: Any, length: int, label: str) -> int:
    index = _plain_int(value, label)
    if index >= length:
        raise _fail(f"GLB {label} is out of bounds")
    return index


def _decode_data_uri(uri: str, *, media_prefix: str | None = None) -> bytes:
    if not uri.startswith("data:"):
        raise _fail("GLB contains an external resource URI")
    header, separator, payload = uri.partition(",")
    parameters = header[5:].split(";")
    if not separator or "base64" not in {item.casefold() for item in parameters[1:]}:
        raise _fail("GLB embedded resource URI is invalid")
    media_type = header[5:].split(";", 1)[0].casefold()
    if media_prefix is not None and not media_type.startswith(media_prefix):
        raise _fail("GLB embedded resource media type is invalid")
    if len(payload) > (_MAX_DATA_URI_BYTES * 4 // 3) + 4:
        raise _fail("GLB embedded resource exceeds the validation bound")
    try:
        decoded = base64.b64decode(payload, validate=True)
    except (ValueError, binascii.Error):
        raise _fail("GLB embedded resource URI is invalid") from None
    if len(decoded) > _MAX_DATA_URI_BYTES:
        raise _fail("GLB embedded resource exceeds the validation bound")
    return decoded


def _reject_json_constant(_value: str) -> None:
    raise ValueError


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError
        result[key] = value
    return result


def _read_document(blob: mmap.mmap) -> tuple[Mapping[str, Any], tuple[int, int] | None]:
    size = len(blob)
    if size < _GLB_HEADER.size + _CHUNK_HEADER.size:
        raise _fail("GLB container is truncated")
    magic, version, declared_length = _GLB_HEADER.unpack_from(blob)
    if magic != b"glTF" or version != 2 or declared_length != size:
        raise _fail("GLB v2 header is invalid")

    offset = _GLB_HEADER.size
    chunks: list[tuple[int, int, int]] = []
    while offset < size:
        if size - offset < _CHUNK_HEADER.size:
            raise _fail("GLB chunk header is truncated")
        chunk_length, chunk_type = _CHUNK_HEADER.unpack_from(blob, offset)
        offset += _CHUNK_HEADER.size
        if chunk_length % 4 or chunk_length > size - offset:
            raise _fail("GLB chunk bounds are invalid")
        chunks.append((chunk_type, offset, chunk_length))
        offset += chunk_length
    if offset != size or not 1 <= len(chunks) <= 2:
        raise _fail("GLB chunk layout is invalid")

    json_type, json_offset, json_length = chunks[0]
    if (
        json_type != _JSON_CHUNK
        or not 1 <= json_length <= _MAX_JSON_BYTES
        or (len(chunks) == 2 and chunks[1][0] != _BIN_CHUNK)
    ):
        raise _fail("GLB chunk order is invalid")
    try:
        document = json.loads(
            bytes(blob[json_offset : json_offset + json_length]),
            object_pairs_hook=_unique_json_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        raise _fail("GLB JSON chunk is invalid") from None
    if not isinstance(document, Mapping):
        raise _fail("GLB JSON root is invalid")
    asset = _object(document.get("asset"), "asset declaration")
    if asset.get("version") != "2.0":
        raise _fail("GLB asset version is incompatible")
    bin_chunk = None if len(chunks) == 1 else (chunks[1][1], chunks[1][2])
    return document, bin_chunk


def _validate_buffers(
    document: Mapping[str, Any], blob: mmap.mmap, bin_chunk: tuple[int, int] | None
) -> tuple[_Buffer, ...]:
    declarations = _array(document, "buffers")
    if bin_chunk is not None and not declarations:
        raise _fail("GLB BIN chunk has no buffer declaration")

    buffers: list[_Buffer] = []
    embedded_bin_claimed = False
    for index, raw in enumerate(declarations):
        declaration = _object(raw, "buffer declaration")
        byte_length = _plain_int(
            declaration.get("byteLength"), "buffer byte length", minimum=1
        )
        uri = declaration.get("uri")
        if uri is None:
            if index != 0 or bin_chunk is None or embedded_bin_claimed:
                raise _fail("GLB buffer has no embedded payload")
            source_offset, available = bin_chunk
            if not byte_length <= available <= byte_length + 3:
                raise _fail("GLB BIN chunk length disagrees with its buffer")
            padding = bytes(
                blob[source_offset + byte_length : source_offset + available]
            )
            if any(padding):
                raise _fail("GLB BIN chunk padding is invalid")
            buffers.append(_Buffer(blob, source_offset, byte_length))
            embedded_bin_claimed = True
            continue
        if not isinstance(uri, str):
            raise _fail("GLB buffer URI is invalid")
        payload = _decode_data_uri(uri)
        if len(payload) != byte_length:
            raise _fail("GLB embedded buffer length is invalid")
        buffers.append(_Buffer(payload, 0, byte_length))
    if bin_chunk is not None and not embedded_bin_claimed:
        raise _fail("GLB BIN chunk is unclaimed")
    return tuple(buffers)


def _validate_buffer_views(
    document: Mapping[str, Any], buffers: tuple[_Buffer, ...]
) -> tuple[_BufferView, ...]:
    result: list[_BufferView] = []
    for raw in _array(document, "bufferViews"):
        view = _object(raw, "bufferView declaration")
        buffer_index = _reference(view.get("buffer"), len(buffers), "bufferView buffer")
        byte_offset = _plain_int(view.get("byteOffset", 0), "bufferView byte offset")
        byte_length = _plain_int(
            view.get("byteLength"), "bufferView byte length", minimum=1
        )
        if byte_offset + byte_length > buffers[buffer_index].byte_length:
            raise _fail("GLB bufferView exceeds its buffer")
        stride = view.get("byteStride")
        if stride is not None:
            stride = _plain_int(stride, "bufferView byte stride", minimum=4)
            if stride > 252 or stride % 4:
                raise _fail("GLB bufferView byte stride is invalid")
        target = view.get("target")
        if target is not None and target not in {34962, 34963}:
            raise _fail("GLB bufferView target is invalid")
        extensions = view.get("extensions", {})
        if isinstance(extensions, Mapping) and "EXT_meshopt_compression" in extensions:
            raise _fail("GLB compressed bufferView is unsupported")
        result.append(_BufferView(buffer_index, byte_offset, byte_length, stride))
    return tuple(result)


def _element_size(value_type: str, component_size: int) -> int:
    columns, rows = _TYPE_SHAPES[value_type]
    column_bytes = rows * component_size
    if columns > 1:
        column_bytes = (column_bytes + 3) & ~3
    return columns * column_bytes


def _validate_numeric_bounds(raw: Mapping[str, Any], value_type: str) -> None:
    component_count = _TYPE_SHAPES[value_type][0] * _TYPE_SHAPES[value_type][1]
    for key in ("min", "max"):
        values = raw.get(key)
        if values is None:
            continue
        if (
            not isinstance(values, list)
            or len(values) != component_count
            or any(
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                for value in values
            )
        ):
            raise _fail("GLB accessor numeric bound is invalid")


def _validate_accessors(
    document: Mapping[str, Any],
    buffers: tuple[_Buffer, ...],
    views: tuple[_BufferView, ...],
) -> tuple[_Accessor, ...]:
    result: list[_Accessor] = []
    for raw in _array(document, "accessors"):
        accessor = _object(raw, "accessor declaration")
        if "sparse" in accessor:
            raise _fail("GLB sparse accessor is unsupported")
        view_index = _reference(
            accessor.get("bufferView"), len(views), "accessor bufferView"
        )
        component_type = accessor.get("componentType")
        if component_type not in _COMPONENT_FORMATS:
            raise _fail("GLB accessor component type is invalid")
        value_type = accessor.get("type")
        if value_type not in _TYPE_SHAPES:
            raise _fail("GLB accessor value type is invalid")
        count = _plain_int(accessor.get("count"), "accessor count", minimum=1)
        byte_offset = _plain_int(accessor.get("byteOffset", 0), "accessor byte offset")
        component_size = _COMPONENT_FORMATS[component_type][1]
        element_size = _element_size(value_type, component_size)
        view = views[view_index]
        absolute_offset = (
            buffers[view.buffer].source_offset + view.byte_offset + byte_offset
        )
        if byte_offset % component_size or absolute_offset % component_size:
            raise _fail("GLB accessor alignment is invalid")
        stride = view.byte_stride or element_size
        if stride < element_size or stride % component_size:
            raise _fail("GLB accessor stride is invalid")
        end = byte_offset + ((count - 1) * stride) + element_size
        if end > view.byte_length:
            raise _fail("GLB accessor exceeds its bufferView")
        normalized = accessor.get("normalized")
        if normalized is not None and not isinstance(normalized, bool):
            raise _fail("GLB accessor normalization flag is invalid")
        if normalized is True and component_type not in {5120, 5121, 5122, 5123}:
            raise _fail("GLB accessor normalization is incompatible")
        if value_type.startswith("MAT") and absolute_offset % 4:
            raise _fail("GLB matrix accessor alignment is invalid")
        _validate_numeric_bounds(accessor, value_type)
        result.append(
            _Accessor(
                view_index,
                byte_offset,
                component_type,
                count,
                value_type,
                element_size,
                component_size,
            )
        )
    return tuple(result)


def _validate_images_and_materials(
    document: Mapping[str, Any], views: tuple[_BufferView, ...]
) -> tuple[int, int]:
    images = _array(document, "images")
    for raw in images:
        image = _object(raw, "image declaration")
        uri = image.get("uri")
        buffer_view = image.get("bufferView")
        if (uri is None) == (buffer_view is None):
            raise _fail("GLB image payload declaration is invalid")
        if uri is not None:
            if not isinstance(uri, str):
                raise _fail("GLB image URI is invalid")
            _decode_data_uri(uri, media_prefix="image/")
        else:
            _reference(buffer_view, len(views), "image bufferView")
            mime_type = image.get("mimeType")
            if mime_type not in {"image/jpeg", "image/png"}:
                raise _fail("GLB embedded image media type is invalid")

    samplers = _array(document, "samplers")
    for raw in samplers:
        sampler = _object(raw, "texture sampler")
        for key, allowed in (
            ("magFilter", {9728, 9729}),
            ("minFilter", {9728, 9729, 9984, 9985, 9986, 9987}),
            ("wrapS", {33071, 33648, 10497}),
            ("wrapT", {33071, 33648, 10497}),
        ):
            if key in sampler and sampler[key] not in allowed:
                raise _fail("GLB texture sampler value is invalid")

    textures = _array(document, "textures")
    for raw in textures:
        texture = _object(raw, "texture declaration")
        if "source" in texture:
            _reference(texture["source"], len(images), "texture image")
        if "sampler" in texture:
            _reference(texture["sampler"], len(samplers), "texture sampler")

    materials = _array(document, "materials")
    for raw in materials:
        _object(raw, "material declaration")
    return len(images), len(materials)


def _accessor_data_location(
    accessor: _Accessor,
    buffers: tuple[_Buffer, ...],
    views: tuple[_BufferView, ...],
) -> tuple[bytes | mmap.mmap, int, int]:
    view = views[accessor.buffer_view]
    buffer = buffers[view.buffer]
    offset = buffer.source_offset + view.byte_offset + accessor.byte_offset
    return buffer.source, offset, view.byte_stride or accessor.element_size


def _validate_indices(
    accessor: _Accessor,
    vertex_count: int,
    buffers: tuple[_Buffer, ...],
    views: tuple[_BufferView, ...],
) -> None:
    if (
        accessor.component_type not in {5121, 5123, 5125}
        or accessor.value_type != "SCALAR"
    ):
        raise _fail("GLB TRIANGLES index accessor is invalid")
    if views[accessor.buffer_view].byte_stride is not None:
        raise _fail("GLB TRIANGLES index accessor is strided")
    source, offset, stride = _accessor_data_location(accessor, buffers, views)
    code = _COMPONENT_FORMATS[accessor.component_type][0]
    decoder = struct.Struct("<" + code)
    for ordinal in range(accessor.count):
        value = decoder.unpack_from(source, offset + (ordinal * stride))[0]
        if value >= vertex_count:
            raise _fail("GLB TRIANGLES index exceeds POSITION bounds")


def _validate_meshes(
    document: Mapping[str, Any],
    accessors: tuple[_Accessor, ...],
    buffers: tuple[_Buffer, ...],
    views: tuple[_BufferView, ...],
    material_count: int,
) -> tuple[int, int, int]:
    meshes = _array(document, "meshes")
    primitive_count = vertex_count = triangle_count = 0
    for raw_mesh in meshes:
        mesh = _object(raw_mesh, "mesh declaration")
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list) or not primitives:
            raise _fail("GLB mesh has no primitives")
        for raw_primitive in primitives:
            primitive = _object(raw_primitive, "mesh primitive")
            extensions = primitive.get("extensions", {})
            if (
                isinstance(extensions, Mapping)
                and "KHR_draco_mesh_compression" in extensions
            ):
                raise _fail("GLB compressed mesh primitive is unsupported")
            if primitive.get("mode", 4) != 4:
                raise _fail("GLB mesh primitive is not TRIANGLES")
            attributes = _object(primitive.get("attributes"), "primitive attributes")
            position_index = _reference(
                attributes.get("POSITION"), len(accessors), "POSITION accessor"
            )
            position = accessors[position_index]
            if position.component_type != 5126 or position.value_type != "VEC3":
                raise _fail("GLB POSITION accessor is incompatible")
            for semantic, accessor_index in attributes.items():
                if not isinstance(semantic, str):
                    raise _fail("GLB primitive attribute semantic is invalid")
                attribute = accessors[
                    _reference(accessor_index, len(accessors), "attribute accessor")
                ]
                if attribute.count != position.count:
                    raise _fail("GLB primitive attribute counts disagree")
            targets = primitive.get("targets", [])
            if not isinstance(targets, list):
                raise _fail("GLB morph target array is invalid")
            for raw_target in targets:
                target = _object(raw_target, "morph target")
                for accessor_index in target.values():
                    target_accessor = accessors[
                        _reference(
                            accessor_index, len(accessors), "morph target accessor"
                        )
                    ]
                    if target_accessor.count != position.count:
                        raise _fail("GLB morph target counts disagree")
            if "material" in primitive:
                _reference(primitive["material"], material_count, "primitive material")
            index_value = primitive.get("indices")
            if index_value is None:
                if position.count % 3:
                    raise _fail("GLB non-indexed TRIANGLES count is invalid")
                primitive_triangles = position.count // 3
            else:
                index_accessor = accessors[
                    _reference(index_value, len(accessors), "TRIANGLES index accessor")
                ]
                if index_accessor.count % 3:
                    raise _fail("GLB indexed TRIANGLES count is invalid")
                _validate_indices(index_accessor, position.count, buffers, views)
                primitive_triangles = index_accessor.count // 3
            primitive_count += 1
            vertex_count += position.count
            triangle_count += primitive_triangles
    if not meshes or not primitive_count or not vertex_count or not triangle_count:
        raise _fail("GLB contains no renderable TRIANGLES geometry")
    return primitive_count, vertex_count, triangle_count


def _validate_nodes_and_scenes(
    document: Mapping[str, Any], mesh_count: int
) -> tuple[list[Mapping[str, Any]], int, set[int]]:
    nodes = [_object(raw, "node declaration") for raw in _array(document, "nodes")]
    parents: dict[int, int] = {}
    skinned_mesh_nodes = 0
    for node_index, node in enumerate(nodes):
        mesh = node.get("mesh")
        if mesh is not None:
            _reference(mesh, mesh_count, "node mesh")
        skin = node.get("skin")
        if skin is not None:
            if mesh is None:
                raise _fail("GLB node skin has no mesh")
            skinned_mesh_nodes += 1
        children = node.get("children", [])
        if not isinstance(children, list):
            raise _fail("GLB node children are invalid")
        resolved_children = [
            _reference(value, len(nodes), "node child") for value in children
        ]
        if len(resolved_children) != len(set(resolved_children)):
            raise _fail("GLB node children are invalid")
        for child in resolved_children:
            if child == node_index or child in parents:
                raise _fail("GLB node hierarchy is invalid")
            parents[child] = node_index

    state = [0] * len(nodes)

    def visit(index: int) -> None:
        if state[index] == 1:
            raise _fail("GLB node hierarchy contains a cycle")
        if state[index] == 2:
            return
        state[index] = 1
        for child in nodes[index].get("children", []):
            visit(child)
        state[index] = 2

    for node_index in range(len(nodes)):
        visit(node_index)

    scenes = _array(document, "scenes")
    if not scenes:
        raise _fail("GLB has no scene")
    roots: set[int] = set()
    for raw_scene in scenes:
        scene = _object(raw_scene, "scene declaration")
        scene_nodes = scene.get("nodes", [])
        if not isinstance(scene_nodes, list):
            raise _fail("GLB scene roots are invalid")
        resolved_roots = [
            _reference(value, len(nodes), "scene root") for value in scene_nodes
        ]
        if len(resolved_roots) != len(set(resolved_roots)):
            raise _fail("GLB scene roots are invalid")
        roots.update(resolved_roots)
    if "scene" in document:
        _reference(document["scene"], len(scenes), "default scene")
    reachable: set[int] = set()
    pending = list(roots)
    while pending:
        index = pending.pop()
        if index in reachable:
            continue
        reachable.add(index)
        pending.extend(nodes[index].get("children", []))
    referenced_meshes = {
        nodes[index]["mesh"] for index in reachable if "mesh" in nodes[index]
    }
    if referenced_meshes != set(range(mesh_count)):
        raise _fail("GLB scene does not reference every mesh")
    return nodes, skinned_mesh_nodes, reachable


def _validate_skins(
    document: Mapping[str, Any],
    nodes: list[Mapping[str, Any]],
    accessors: tuple[_Accessor, ...],
) -> tuple[int, int]:
    skins = _array(document, "skins")
    joint_nodes: set[int] = set()
    for raw in skins:
        skin = _object(raw, "skin declaration")
        joints = skin.get("joints")
        if not isinstance(joints, list) or not joints:
            raise _fail("GLB skin joints are invalid")
        resolved = [_reference(value, len(nodes), "skin joint") for value in joints]
        if len(resolved) != len(set(resolved)):
            raise _fail("GLB skin joints are invalid")
        joint_nodes.update(resolved)
        if "skeleton" in skin:
            _reference(skin["skeleton"], len(nodes), "skin skeleton")
        if "inverseBindMatrices" in skin:
            accessor = accessors[
                _reference(
                    skin["inverseBindMatrices"],
                    len(accessors),
                    "inverse bind accessor",
                )
            ]
            if (
                accessor.component_type != 5126
                or accessor.value_type != "MAT4"
                or accessor.count != len(resolved)
            ):
                raise _fail("GLB inverse bind accessor is incompatible")
    for node in nodes:
        if "skin" in node:
            _reference(node["skin"], len(skins), "node skin")
    return len(skins), len(joint_nodes)


def _finite_number(value: Any) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(value)
    )


def _validate_visibility_channels(value: Any) -> None:
    if not isinstance(value, list) or not value:
        raise _fail("GLB W3D visibility channel array is invalid")
    for raw_channel in value:
        channel = _object(raw_channel, "W3D visibility channel")
        if set(channel) != {"owner", "data_path", "array_index", "keys"}:
            raise _fail("GLB W3D visibility channel contract is invalid")
        path = channel.get("data_path")
        if (
            channel.get("owner") not in {"object", "armature"}
            or not isinstance(path, str)
            or not path
            or type(channel.get("array_index")) is not int
        ):
            raise _fail("GLB W3D visibility channel value is invalid")
        if path != "hide_viewport" and not (
            path.startswith('bones["') and path.endswith('"].visibility')
        ):
            raise _fail("GLB W3D visibility channel path is invalid")
        keys = channel.get("keys")
        if not isinstance(keys, list) or not keys:
            raise _fail("GLB W3D visibility key array is invalid")
        for raw_key in keys:
            key = _object(raw_key, "W3D visibility key")
            if (
                set(key) != {"frame", "value", "interpolation"}
                or not _finite_number(key.get("frame"))
                or not _finite_number(key.get("value"))
                or not isinstance(key.get("interpolation"), str)
                or not key["interpolation"]
            ):
                raise _fail("GLB W3D visibility key is invalid")


def _validate_animation_visibility_extras(animation: Mapping[str, Any]) -> None:
    extras = animation.get("extras", {})
    if not isinstance(extras, Mapping):
        raise _fail("GLB animation extras are invalid")
    contract = extras.get("openbfme_w3d_visibility")
    if contract is None:
        return
    contract = _object(contract, "W3D visibility contract")
    if (
        set(contract) != {"schema", "version", "channels"}
        or contract.get("schema") != "openbfme.w3d-visibility-channels"
        or contract.get("version") != 1
    ):
        raise _fail("GLB W3D visibility contract is incompatible")
    _validate_visibility_channels(contract.get("channels"))


def _visibility_only_count(document: Mapping[str, Any]) -> int:
    extras = document.get("extras", {})
    if not isinstance(extras, Mapping):
        raise _fail("GLB root extras are invalid")
    contract = extras.get("openbfme_w3d_visibility_only_animations")
    if contract is None:
        return 0
    contract = _object(contract, "W3D visibility-only contract")
    if (
        set(contract) != {"schema", "version", "animations"}
        or contract.get("schema") != "openbfme.w3d-visibility-only-animations"
        or contract.get("version") != 1
        or not isinstance(contract.get("animations"), list)
        or not contract["animations"]
    ):
        raise _fail("GLB W3D visibility-only contract is incompatible")
    names: set[str] = set()
    for raw in contract["animations"]:
        animation = _object(raw, "W3D visibility-only animation")
        name = animation.get("name")
        if (
            set(animation) != {"name", "shape", "channels"}
            or not isinstance(name, str)
            or not name
            or name.casefold() in names
            or animation.get("shape") != "visibility-only"
        ):
            raise _fail("GLB W3D visibility-only animation is invalid")
        names.add(name.casefold())
        _validate_visibility_channels(animation.get("channels"))
    return len(contract["animations"])


def _validate_animations(
    document: Mapping[str, Any],
    accessors: tuple[_Accessor, ...],
    nodes: list[Mapping[str, Any]],
) -> tuple[int, int]:
    animations = _array(document, "animations")
    for raw in animations:
        animation = _object(raw, "animation declaration")
        samplers = animation.get("samplers")
        channels = animation.get("channels")
        if not isinstance(samplers, list) or not samplers:
            raise _fail("GLB animation samplers are invalid")
        if not isinstance(channels, list) or not channels:
            raise _fail("GLB animation channels are invalid")
        resolved_samplers: list[tuple[_Accessor, _Accessor, str]] = []
        for raw_sampler in samplers:
            sampler = _object(raw_sampler, "animation sampler")
            input_accessor = accessors[
                _reference(sampler.get("input"), len(accessors), "animation input")
            ]
            output_accessor = accessors[
                _reference(sampler.get("output"), len(accessors), "animation output")
            ]
            interpolation = sampler.get("interpolation", "LINEAR")
            if (
                input_accessor.component_type != 5126
                or input_accessor.value_type != "SCALAR"
                or interpolation not in {"LINEAR", "STEP", "CUBICSPLINE"}
            ):
                raise _fail("GLB animation sampler is incompatible")
            resolved_samplers.append((input_accessor, output_accessor, interpolation))
        targets: set[tuple[int, str]] = set()
        for raw_channel in channels:
            channel = _object(raw_channel, "animation channel")
            sampler_index = _reference(
                channel.get("sampler"), len(resolved_samplers), "animation sampler"
            )
            target = _object(channel.get("target"), "animation target")
            node_index = _reference(target.get("node"), len(nodes), "animation node")
            path = target.get("path")
            if path not in {"translation", "rotation", "scale", "weights"}:
                raise _fail("GLB animation target path is invalid")
            if (node_index, path) in targets:
                raise _fail("GLB animation target is duplicated")
            targets.add((node_index, path))
            input_accessor, output_accessor, interpolation = resolved_samplers[
                sampler_index
            ]
            if path in {"translation", "scale", "rotation"}:
                expected_type = "VEC4" if path == "rotation" else "VEC3"
                multiplier = 3 if interpolation == "CUBICSPLINE" else 1
                if (
                    output_accessor.component_type != 5126
                    or output_accessor.value_type != expected_type
                    or output_accessor.count != input_accessor.count * multiplier
                ):
                    raise _fail("GLB animation output accessor is incompatible")
            else:
                multiplier = 3 if interpolation == "CUBICSPLINE" else 1
                minimum = input_accessor.count * multiplier
                if (
                    output_accessor.component_type != 5126
                    or output_accessor.count < minimum
                    or output_accessor.count % minimum
                ):
                    raise _fail("GLB animation weight output is incompatible")
        _validate_animation_visibility_extras(animation)
    return len(animations), _visibility_only_count(document)


def _normalized_expected_counts(expected_counts: Mapping[str, int]) -> dict[str, int]:
    if not isinstance(expected_counts, Mapping) or not expected_counts:
        raise _fail("GLB expected count contract is empty or invalid")
    fields_by_name = {item.name for item in fields(W3DGLBSemanticSummary)}
    normalized: dict[str, int] = {}
    for raw_key, value in expected_counts.items():
        if not isinstance(raw_key, str):
            raise _fail("GLB expected count key is invalid")
        key = _ADAPTER_COUNT_ALIASES.get(raw_key, raw_key)
        if key not in fields_by_name or key in normalized:
            raise _fail("GLB expected count key is unsupported or duplicated")
        normalized[key] = _plain_int(value, "expected count")
    return normalized


def validate_w3d_glb_semantics(
    path: Path, expected_counts: Mapping[str, int]
) -> W3DGLBSemanticSummary:
    """Validate one W3D GLB and compare every explicitly expected aggregate.

    Both summary field names and the adapter report aliases (``meshes``,
    ``animations``, ``bones``, ``skeletons``, ``vertices``, ``triangles``,
    ``skinned_meshes``, ``materials``, and ``images``) are accepted.  The
    mapping may be selective so callers do not equate pre-export Blender counts
    with representation-dependent GLB counts without an explicit decision.
    """

    expected = _normalized_expected_counts(expected_counts)
    if not isinstance(path, Path):
        raise _fail("GLB path is invalid")
    try:
        if not path.is_file() or path.is_symlink():
            raise _fail("GLB is not an ordinary file")
        if path.stat().st_size < _GLB_HEADER.size + _CHUNK_HEADER.size:
            raise _fail("GLB container is truncated")
        with (
            path.open("rb") as handle,
            mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as blob,
        ):
            document, bin_chunk = _read_document(blob)
            buffers = _validate_buffers(document, blob, bin_chunk)
            views = _validate_buffer_views(document, buffers)
            accessors = _validate_accessors(document, buffers, views)
            image_count, material_count = _validate_images_and_materials(
                document, views
            )
            meshes = _array(document, "meshes")
            primitive_count, vertex_count, triangle_count = _validate_meshes(
                document,
                accessors,
                buffers,
                views,
                material_count,
            )
            nodes, skinned_mesh_nodes, _reachable = _validate_nodes_and_scenes(
                document, len(meshes)
            )
            skin_count, joint_count = _validate_skins(document, nodes, accessors)
            gltf_animation_count, visibility_only_count = _validate_animations(
                document, accessors, nodes
            )
    except OSError:
        raise _fail("GLB file could not be read") from None

    summary = W3DGLBSemanticSummary(
        mesh_count=len(meshes),
        primitive_count=primitive_count,
        material_count=material_count,
        image_count=image_count,
        skin_count=skin_count,
        gltf_animation_count=gltf_animation_count,
        visibility_only_animation_count=visibility_only_count,
        animation_count=gltf_animation_count + visibility_only_count,
        vertex_count=vertex_count,
        triangle_count=triangle_count,
        skinned_mesh_node_count=skinned_mesh_nodes,
        joint_count=joint_count,
    )
    for key, expected_value in expected.items():
        if getattr(summary, key) != expected_value:
            raise W3DGLBSemanticMismatchError(f"GLB semantic count mismatch for {key}")
    return summary
