"""Deterministic Object-to-physical visual closure over an extracted asset tree.

The effective-asset tree is already the winning retail virtual filesystem.  This
module treats it as a read-only input: INI/INC sources are parsed to find the
requested Object inheritance/include closure, while W3D files are initially
catalogued by path only.  W3D bytes are opened only for exact physical target
leaves and exact candidates needed to resolve a logical header reference that
the path-only pass could not answer.  Embedded texture identifiers from those
files are then resolved into the same bounded conversion dependency report.

No fallback or fuzzy asset policy lives here.  Missing authored leaves remain
in the returned neutral report, and ambiguous Object definitions fail closed.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import threading
from typing import Iterable

from .paths import safe_relative_parts
from .sage_cst import (
    ResolvedSageCst,
    SageObject,
    resolve_sage_documents,
    strip_sage_comments,
)
from .typed_visual_graph import (
    TypedAssetReference,
    TypedVisualGraph,
    resolve_typed_visual_graph,
)
from .visual_leaf import VisualLeafRequest, diagnose_visual_leaves
from .w3d_index import W3DFileHeaders, build_w3d_index
from .w3d_metadata import (
    W3DMetadata,
    W3DSourceProvenance,
    scan_w3d_metadata,
)


REPORT_SCHEMA = "openbfme.retail-visual-closure"
REPORT_SCHEMA_VERSION = 1
MAX_ASSET_FILES = 100_000
MAX_SAGE_SOURCE_FILES = 25_000

# Per-process inventory/index memo for one effective-assets root. Rebuilds of
# many object closures in one convert run were re-walking ~40k files each call.
_ASSET_CONTEXT_CACHE: dict[str, tuple[object, ...]] = {}
# Catalog-filtered path catalogs + path-only W3D index. Convert walks many
# objects against one catalog; rebuilding the filter/index each call dominated
# cold convert cost after the pack-identity filter was introduced.
_PACK_FILTERED_CONTEXT_CACHE: dict[tuple[str, str], tuple[object, ...]] = {}
# Single-flight builders so 16 convert workers do not stampede the same filter
# key (lock protects lookup + publish; construction waits on the Event).
_PACK_FILTERED_INFLIGHT: dict[tuple[str, str], threading.Event] = {}
_ASSET_CONTEXT_LOCK = threading.Lock()
MAX_SAGE_SOURCE_BYTES = 256 * 1024 * 1024
MAX_TARGET_OBJECTS = 4_096
MAX_TARGET_W3D_SCANS = 4_096
MAX_OBJECT_NAME_LENGTH = 255

_SAGE_SUFFIXES = frozenset({".ini", ".inc"})
_VISUAL_SUFFIXES = frozenset({".dds", ".tga", ".jpg", ".png", ".w3d"})
_W3D_LEAF_KINDS = frozenset(
    {"model", "hierarchy", "animation", "attached-model", "particle"}
)
_OBJECT_HEADER = re.compile(
    # ``Object = SomeUnit`` is a common scalar assignment inside
    # CommandButton blocks, not an Object declaration.  Exclude an equals
    # token before deciding a source document needs the Object CST parser.
    r"^(Object|ChildObject|ObjectReskin)\s+(?!=)(\S+)(?:\s+(\S+))?\s*$",
    re.IGNORECASE,
)
_FORBIDDEN_OBJECT_CHARACTERS = frozenset('/\\:*?"<>|')
_SYNTHETIC_ENTRY = "__openbfme__/retail-visual-closure.ini"
_SHADER_TEXTURE_PROPERTY = re.compile(
    r"(?:texture|map)(?:_?\d+)?$",
    re.IGNORECASE,
)


#: Windows sets this on symlinks, junctions and every other reparse point.
#: Used only to decide whether the authoritative link check is worth a syscall.
_FILE_ATTRIBUTE_REPARSE_POINT = 0x400


def _sort_text(value: str) -> tuple[str, str]:
    return value.casefold(), value


def _canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _entry_is_link_like(entry: os.DirEntry) -> bool:
    """``_is_link_like`` for a scandir entry, without the extra syscalls.

    ``_is_link_like`` costs two filesystem round-trips per path
    (``is_symlink`` + ``is_junction``). Over the effective-assets tree - 40,130
    files - that plus a ``stat`` for the size was ~120,000 syscalls and 15 s of
    every faction publish, to answer a question about three projectiles.

    A ``DirEntry`` already carries the data the directory enumeration returned,
    so the common answer costs nothing. This is NOT a weaker check: a path that
    is not a reparse point cannot be a symlink or a junction, so the fast path
    and ``_is_link_like`` agree by construction, and anything that IS flagged
    as a reparse point falls through to the original function unchanged.
    """

    try:
        info = entry.stat(follow_symlinks=False)
    except OSError:
        # Undecidable cheaply - ask the authoritative check.
        return _is_link_like(Path(entry.path))
    attributes = getattr(info, "st_file_attributes", None)
    if attributes is None:
        # POSIX: no reparse points, so a symlink is the whole question and
        # DirEntry answers it from cached data.
        return entry.is_symlink()
    if not attributes & _FILE_ATTRIBUTE_REPARSE_POINT:
        return False
    return _is_link_like(Path(entry.path))


@dataclass(frozen=True, slots=True)
class _AssetFile:
    virtual_path: str
    physical_path: Path
    byte_length: int


@dataclass(frozen=True, slots=True)
class _ObjectDefinition:
    kind: str
    name: str
    parent: str | None
    virtual_path: str
    line: int

    @classmethod
    def from_sage(cls, value: SageObject) -> "_ObjectDefinition":
        return cls(
            value.kind,
            value.name,
            value.parent,
            value.source_virtual_path,
            value.line,
        )

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "kind": self.kind,
            "name": self.name,
            "virtualPath": self.virtual_path,
            "line": self.line,
        }
        if self.parent is not None:
            result["parent"] = self.parent
        return result


@dataclass(frozen=True, slots=True)
class _EmbeddedTextureReference:
    metadata: W3DMetadata
    identifier: str
    provenance: W3DSourceProvenance
    texture_chunk_header_offset: int | None = None
    shader_property_name: str | None = None
    shader_material_chunk_header_offset: int | None = None


def _validated_targets(object_names: Iterable[str]) -> tuple[str, ...]:
    selected: dict[str, str] = {}
    for value in object_names:
        if len(selected) >= MAX_TARGET_OBJECTS:
            raise ValueError(
                f"target Object count exceeds {MAX_TARGET_OBJECTS} limit"
            )
        if (
            not isinstance(value, str)
            or not value
            or value != value.strip()
            or len(value) > MAX_OBJECT_NAME_LENGTH
            or value in {".", ".."}
            or value.startswith(".")
            or value.endswith(".")
            or ".." in value
            or any(character.isspace() for character in value)
            or any(character in _FORBIDDEN_OBJECT_CHARACTERS for character in value)
            or any(ord(character) < 32 for character in value)
        ):
            raise ValueError(f"unsafe target Object name: {value!r}")
        key = value.casefold()
        if key in selected:
            raise ValueError(f"duplicate target Object name: {value!r}")
        selected[key] = value
    if not selected:
        raise ValueError("at least one target Object name is required")
    return tuple(sorted(selected.values(), key=_sort_text))


def _inventory_assets(root: Path) -> tuple[_AssetFile, ...]:
    expanded = root.expanduser()
    if _is_link_like(expanded):
        raise ValueError(f"effective-assets root cannot be a link: {expanded}")
    try:
        resolved = expanded.resolve(strict=True)
    except OSError as exc:
        raise ValueError(f"effective-assets root is unavailable: {root}") from exc
    if not resolved.is_dir():
        raise ValueError(f"effective-assets root is not a directory: {resolved}")
    if _is_link_like(resolved):
        raise ValueError(f"effective-assets root cannot be a link: {resolved}")

    result: list[_AssetFile] = []
    folded_paths: dict[str, str] = {}
    # scandir, not os.walk + Path.stat: the enumeration already returns each
    # entry's type, link status and size, so the previous three syscalls per
    # file become zero. Same traversal order, same checks, same errors - only
    # the number of round-trips to the filesystem changes. (Identical reasoning
    # to `_pack_files` in pipeline.py, which measured 4x on a 16k-file pack;
    # this tree is 40k files and was costing ~15 s per faction publish.)
    pending: list[Path] = [resolved]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as scan:
                entries = sorted(scan, key=lambda item: _sort_text(item.name))
        except OSError as exc:
            raise ValueError(f"cannot read effective assets: {directory}") from exc
        directories: list[Path] = []
        for entry in entries:
            path = Path(entry.path)
            try:
                is_directory = entry.is_dir(follow_symlinks=False)
            except OSError as exc:
                raise ValueError(f"cannot stat effective asset: {path}") from exc
            if is_directory:
                if _entry_is_link_like(entry):
                    raise ValueError(
                        f"effective-assets tree contains a link: {path}"
                    )
                relative = path.relative_to(resolved).as_posix()
                safe_relative_parts(relative)
                directories.append(path)
                continue
            if _entry_is_link_like(entry):
                raise ValueError(f"effective-assets tree contains a link: {path}")
            relative = path.relative_to(resolved).as_posix()
            canonical = "/".join(safe_relative_parts(relative))
            key = canonical.casefold()
            previous = folded_paths.get(key)
            if previous is not None:
                raise ValueError(
                    "case-ambiguous effective asset paths: "
                    f"{previous!r}, {canonical!r}"
                )
            folded_paths[key] = canonical
            try:
                size = entry.stat(follow_symlinks=False).st_size
            except OSError as exc:
                raise ValueError(f"cannot stat effective asset: {canonical}") from exc
            if size < 0:
                raise ValueError(f"effective asset has invalid size: {canonical}")
            result.append(_AssetFile(canonical, path, size))
            if len(result) > MAX_ASSET_FILES:
                raise ValueError(
                    f"effective asset count exceeds {MAX_ASSET_FILES} limit"
                )
        # Reversed, because `pending` is a stack: this keeps sibling
        # directories visited in sorted order, as os.walk(topdown=True) did.
        pending.extend(reversed(directories))
    result.sort(key=lambda item: _sort_text(item.virtual_path))
    return tuple(result)


def _read_sage_source(asset: _AssetFile) -> bytes:
    return asset.physical_path.read_bytes()


def _read_target_w3d_bytes(asset: _AssetFile) -> bytes:
    """Single auditable byte-read boundary for exact W3D scan candidates."""

    return asset.physical_path.read_bytes()


def _sage_sources(
    assets: tuple[_AssetFile, ...],
) -> dict[str, bytes]:
    selected = [
        asset
        for asset in assets
        if PurePosixPath(asset.virtual_path).suffix.casefold() in _SAGE_SUFFIXES
    ]
    if len(selected) > MAX_SAGE_SOURCE_FILES:
        raise ValueError(
            f"SAGE source count exceeds {MAX_SAGE_SOURCE_FILES} limit"
        )
    total_bytes = sum(item.byte_length for item in selected)
    if total_bytes > MAX_SAGE_SOURCE_BYTES:
        raise ValueError(
            f"SAGE source bytes exceed {MAX_SAGE_SOURCE_BYTES} limit"
        )
    sources: dict[str, bytes] = {}
    for asset in selected:
        source = _read_sage_source(asset)
        if len(source) != asset.byte_length:
            raise ValueError(
                f"effective asset changed while reading: {asset.virtual_path}"
            )
        sources[asset.virtual_path] = source
    return sources


def _contains_object_header(source: bytes, virtual_path: str) -> bool:
    if b"\0" in source:
        raise ValueError(f"SAGE source contains a NUL byte: {virtual_path}")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError(f"SAGE source is not valid CP1252: {virtual_path}") from exc
    return any(
        _OBJECT_HEADER.fullmatch(strip_sage_comments(line).strip())
        for line in text.splitlines()
    )


def _definition_index(
    sources: dict[str, bytes],
) -> dict[str, tuple[_ObjectDefinition, ...]]:
    index: dict[str, list[_ObjectDefinition]] = {}
    for virtual_path in sorted(sources, key=_sort_text):
        # Map-local INIs are scoped to their own scenario. They are not part
        # of the base-game Object namespace and an unrelated map override must
        # not make a retail base Object ambiguous (for example WargLair).
        if virtual_path.casefold().startswith("maps/"):
            continue
        source = sources[virtual_path]
        if not _contains_object_header(source, virtual_path):
            continue
        # Definition discovery is deliberately lexical. Retail INIs such as
        # crate.ini legitimately mix other top-level block families with
        # Object declarations; running the Object CST over the whole file at
        # inventory time would reject unrelated ``End`` tokens before we even
        # know whether that file belongs to the requested target closure.
        text = source.decode("cp1252")
        for line_number, raw_line in enumerate(text.splitlines(), start=1):
            match = _OBJECT_HEADER.fullmatch(
                strip_sage_comments(raw_line).strip()
            )
            if match is None:
                continue
            definition = _ObjectDefinition(
                match.group(1),
                match.group(2),
                match.group(3),
                virtual_path,
                line_number,
            )
            index.setdefault(definition.name.casefold(), []).append(definition)
    result: dict[str, tuple[_ObjectDefinition, ...]] = {}
    for key, values in index.items():
        values.sort(
            key=lambda item: (
                _sort_text(item.virtual_path),
                item.line,
                _sort_text(item.name),
            )
        )
        result[key] = tuple(values)
    return result


def _ambiguous_definition(name: str, values: tuple[_ObjectDefinition, ...]) -> ValueError:
    locations = ", ".join(f"{item.virtual_path}:{item.line}" for item in values)
    return ValueError(f"ambiguous Object definition {name!r}: {locations}")


def _definition_closure(
    targets: tuple[str, ...],
    index: dict[str, tuple[_ObjectDefinition, ...]],
) -> tuple[
    tuple[dict[str, object], ...],
    tuple[_ObjectDefinition, ...],
    tuple[dict[str, object], ...],
]:
    target_records: list[dict[str, object]] = []
    closure: dict[tuple[str, str, int], _ObjectDefinition] = {}
    missing: dict[str, dict[str, object]] = {}

    for target in targets:
        candidates = index.get(target.casefold(), ())
        if len(candidates) > 1:
            raise _ambiguous_definition(target, candidates)
        if not candidates:
            target_records.append(
                {"requestedName": target, "status": "missing-definition"}
            )
            missing.setdefault(
                target.casefold(),
                {"name": target, "requiredBy": target, "role": "target"},
            )
            continue

        target_definition = candidates[0]
        target_record = {"requestedName": target, "status": "resolved"}
        target_record.update(target_definition.neutral())
        target_records.append(target_record)

        current = target_definition
        visited: set[str] = set()
        while True:
            key = current.name.casefold()
            if key in visited:
                break
            visited.add(key)
            closure[(key, current.virtual_path.casefold(), current.line)] = current
            if current.parent is None:
                break
            parent_candidates = index.get(current.parent.casefold(), ())
            if len(parent_candidates) > 1:
                raise _ambiguous_definition(current.parent, parent_candidates)
            if not parent_candidates:
                missing.setdefault(
                    current.parent.casefold(),
                    {
                        "name": current.parent,
                        "requiredBy": current.name,
                        "role": "parent",
                    },
                )
                break
            current = parent_candidates[0]

    ordered_closure = tuple(
        sorted(
            closure.values(),
            key=lambda item: (
                _sort_text(item.name),
                _sort_text(item.virtual_path),
                item.line,
            ),
        )
    )
    ordered_missing = tuple(
        sorted(
            missing.values(),
            key=lambda item: (
                str(item["name"]).casefold(),
                str(item["name"]),
                str(item["requiredBy"]).casefold(),
            ),
        )
    )
    return tuple(target_records), ordered_closure, ordered_missing


def _synthetic_entry(source_paths: Iterable[str]) -> bytes:
    lines = [f'#include "{path}"' for path in sorted(set(source_paths), key=_sort_text)]
    if not lines:
        lines.append("; no resolved target definitions")
    return ("\n".join(lines) + "\n").encode("cp1252")


def _path_catalogs(
    assets: tuple[_AssetFile, ...],
) -> tuple[tuple[str, ...], tuple[str, ...], dict[str, _AssetFile]]:
    w3d: list[str] = []
    visual: list[str] = []
    records: dict[str, _AssetFile] = {}
    for asset in assets:
        suffix = PurePosixPath(asset.virtual_path).suffix.casefold()
        if suffix not in _VISUAL_SUFFIXES:
            continue
        visual.append(asset.virtual_path)
        if suffix == ".w3d":
            w3d.append(asset.virtual_path)
            records[asset.virtual_path.casefold()] = asset
    if not w3d:
        raise ValueError("effective-assets root contains no W3D files")
    return tuple(w3d), tuple(visual), records


def _scan_candidate_paths(
    graph: TypedVisualGraph,
    w3d_paths: tuple[str, ...],
) -> tuple[str, ...]:
    by_stem: dict[str, list[str]] = {}
    for path in w3d_paths:
        by_stem.setdefault(PurePosixPath(path).stem.casefold(), []).append(path)

    selected: set[str] = set()
    references = (
        reference
        for item in graph.objects
        for reference in item.all_references()
    )
    for reference in references:
        if reference.kind not in _W3D_LEAF_KINDS:
            continue

        # A path-only resolution is already exact dependency evidence.  It is
        # also the most important file to inspect: converters need metadata
        # and embedded material leaves from every selected model, not only
        # from a file that happened to repair an unresolved header reference.
        if reference.status == "resolved":
            for path in reference.physical_virtual_paths:
                if path.casefold().endswith(".w3d"):
                    selected.add(path)
            continue

        for path in reference.candidates:
            if path.casefold().endswith(".w3d"):
                selected.add(path)

        # Visual-leaf kinds have already searched the complete path catalog by
        # their exact authored basename/path.  Only the typed W3D references
        # need this additional exact-stem candidate step before headers exist.
        if reference.kind not in {"model", "animation", "hierarchy"}:
            continue

        identifier = reference.identifier
        if "/" in identifier or "\\" in identifier:
            continue
        if reference.kind == "animation" and "." in identifier:
            stem = identifier.split(".", 1)[1]
        elif reference.kind == "model" and "." in identifier:
            stem = identifier.split(".", 1)[0]
        else:
            stem = identifier
        if not stem or stem in {".", ".."}:
            continue
        selected.update(by_stem.get(stem.casefold(), ()))

    ordered = tuple(sorted(selected, key=_sort_text))
    if len(ordered) > MAX_TARGET_W3D_SCANS:
        raise ValueError(
            f"targeted W3D scan count exceeds {MAX_TARGET_W3D_SCANS} limit"
        )
    return ordered


def _scan_w3d_candidates(
    virtual_paths: tuple[str, ...],
    records: dict[str, _AssetFile],
) -> tuple[W3DMetadata, ...]:
    result: list[W3DMetadata] = []
    for virtual_path in virtual_paths:
        asset = records.get(virtual_path.casefold())
        if asset is None:
            raise ValueError(f"W3D candidate is absent from asset inventory: {virtual_path}")
        source = _read_target_w3d_bytes(asset)
        if len(source) != asset.byte_length:
            raise ValueError(f"effective asset changed while reading: {virtual_path}")
        result.append(scan_w3d_metadata(source, virtual_path))
    return tuple(result)


def _trimmed_file_headers(metadata: W3DMetadata) -> W3DFileHeaders:
    """Return scanned header ids with retail fixed-width padding trimmed.

    W3D chunk name fields are fixed-width strings and retail exporters
    sometimes pad them with spaces or tabs (for example
    ``EBSTABLE_A.EBBSTABLES_1 `` or ``DBMINE_A.ROCK_24\\t``).  SAGE logical
    references never contain whitespace, so padding at a name component's
    edges is not part of the logical identifier; composite ``hierarchy.clip``
    ids are trimmed per component because either raw component may carry the
    padding.  Trimming is the only deterministic normalization; a component
    that trims to nothing stays fail-closed here, and an id that collides
    after trimming stays fail-closed in :func:`build_w3d_index`.
    """

    headers = metadata.file_headers()

    def _trim(kind: str, values: tuple[str, ...]) -> tuple[str, ...]:
        result: list[str] = []
        for value in values:
            parts = [part.strip() for part in value.split(".")]
            if any(not part for part in parts):
                raise ValueError(
                    f"W3D {kind} header id has a whitespace-only component "
                    f"after retail padding trim: {metadata.virtual_path}"
                )
            result.append(".".join(parts))
        return tuple(result)

    return W3DFileHeaders(
        virtual_path=headers.virtual_path,
        model_ids=_trim("model", headers.model_ids),
        animation_ids=_trim("animation", headers.animation_ids),
        hierarchy_ids=_trim("hierarchy", headers.hierarchy_ids),
    )


def _model_hierarchy_identifiers(metadata: W3DMetadata) -> list[str]:
    """Return the exact hierarchy identifiers the model's HLod headers need.

    A model whose HLod hierarchy identifier is not authored inside its own
    file depends on the external hierarchy source that authors it (the pinned
    importer resolves that source as a sibling W3D at conversion time).  The
    recipe lanes need the exact identifier to stage that provider; trim the
    same retail fixed-width padding as every other header id.
    """

    identifiers: set[str] = set()
    for header in metadata.model_headers:
        parts = [part.strip() for part in header.hierarchy_identifier.split(".")]
        if any(not part for part in parts):
            raise ValueError(
                "W3D model hierarchy id has a whitespace-only component "
                f"after retail padding trim: {metadata.virtual_path}"
            )
        identifiers.add(".".join(parts))
    return sorted(identifiers, key=str.casefold)


_ANIMATION_CHANNEL_CHUNK_NAMES = frozenset(
    {
        "animation-channel",
        "compressed-animation-channel",
        "compressed-animation-motion-channel",
    }
)

def _skinned_mesh_count(metadata: W3DMetadata) -> int:
    """Count meshes with a vertex-influences stream (actual bone deformation).

    The mesh-header skin geometry flag alone is not proof: retail authors
    rigid meshes whose header retains the flag without any weights.  The
    vertex-influences stream is the data the pinned importer deforms with, so
    it is the only exact skinning evidence.
    """

    return sum(
        1 for chunk in metadata.chunks if chunk.chunk_name == "vertex-influences"
    )


def _embedded_animation_channel_count(metadata: W3DMetadata) -> int:
    """Count keyed channels embedded in the file's own animation chunks.

    Some retail models embed a header-only animation chunk (no channels at
    all): the clip keys nothing, so it is vacuous evidence rather than an
    embedded-animation conversion shape.
    """

    return sum(
        1
        for chunk in metadata.chunks
        if chunk.chunk_name in _ANIMATION_CHANNEL_CHUNK_NAMES
    )


def _scan_hierarchy_candidate_paths(
    scanned: tuple[W3DMetadata, ...],
    w3d_paths: tuple[str, ...],
    already_selected: tuple[str, ...],
) -> tuple[str, ...]:
    """Select exact-stem hierarchy files discovered from animation headers.

    A model's HLod headers reference their pivot source by exact hierarchy
    identifier, and the pinned importer resolves that source as the
    exact-stem sibling W3D at conversion time.  Models therefore require
    those providers exactly the way animation headers do.
    """

    by_stem: dict[str, list[str]] = {}
    for path in w3d_paths:
        by_stem.setdefault(PurePosixPath(path).stem.casefold(), []).append(path)

    authored_hierarchies: set[str] = set()
    required_hierarchies: set[str] = set()
    for item in scanned:
        headers = _trimmed_file_headers(item)
        authored = {
            identifier.casefold() for identifier in headers.hierarchy_ids
        }
        authored_hierarchies.update(authored)
        for identifier in headers.animation_ids:
            if "." in identifier:
                required_hierarchies.add(identifier.split(".", 1)[0].casefold())
        required_hierarchies.update(
            identifier.casefold()
            for identifier in _model_hierarchy_identifiers(item)
            if identifier.casefold() not in authored
        )

    selected_keys = {path.casefold() for path in already_selected}
    follow_up = {
        path
        for identifier in required_hierarchies - authored_hierarchies
        for path in by_stem.get(identifier, ())
        if path.casefold() not in selected_keys
    }
    if len(already_selected) + len(follow_up) > MAX_TARGET_W3D_SCANS:
        raise ValueError(
            f"targeted W3D scan count exceeds {MAX_TARGET_W3D_SCANS} limit"
        )
    return tuple(sorted(follow_up, key=_sort_text))


def _embedded_texture_dependencies(
    scanned: tuple[W3DMetadata, ...],
    visual_paths: tuple[str, ...],
) -> list[dict[str, object]]:
    """Resolve W3D-authored texture identifiers without representation guesses.

    The primary request retains its authored extension.  The one deliberate
    retail bridge mirrors the SAGE graph policy: when an authored TGA is
    absent, its exact extensionless stem may identify one and only one compiled
    DDS.  A PNG/JPG does not substitute for that TGA, and multiple exact DDS
    candidates remain ambiguous.
    """

    references: list[_EmbeddedTextureReference] = []
    for metadata in scanned:
        references.extend(
            _EmbeddedTextureReference(
                metadata,
                reference.identifier,
                reference.provenance,
                texture_chunk_header_offset=(
                    reference.texture_chunk_header_offset
                ),
            )
            for reference in metadata.texture_references
        )
        for prop in metadata.shader_material_properties:
            if prop.name is None or _SHADER_TEXTURE_PROPERTY.search(prop.name) is None:
                continue
            if prop.property_type_name != "string" or not isinstance(
                prop.value, str
            ) or not prop.value:
                raise ValueError(
                    "texture-bearing shader material property must have a "
                    f"non-empty string value: {metadata.virtual_path!r} "
                    f"property {prop.name!r} at byte "
                    f"{prop.provenance.value_offset}"
                )
            references.append(
                _EmbeddedTextureReference(
                    metadata,
                    prop.value,
                    prop.provenance,
                    shader_property_name=prop.name,
                    shader_material_chunk_header_offset=(
                        prop.material_chunk_header_offset
                    ),
                )
            )
    if not references:
        return []

    requests: list[VisualLeafRequest] = []
    primary_indexes: list[int] = []
    compiled_indexes: dict[int, int] = {}
    for position, reference in enumerate(references):
        primary_indexes.append(len(requests))
        requests.append(VisualLeafRequest(reference.identifier, "texture"))
        pure = PurePosixPath(reference.identifier)
        if pure.suffix.casefold() == ".tga":
            compiled_identifier = pure.with_suffix("").as_posix()
            if compiled_identifier.endswith((" ", ".")):
                # RotWK 2.01 retail ships space-bearing texture names
                # ("EXIceMunitionsAlpha .tga" beside its compiled
                # "exicemunitionsalpha .dds"). A trailing-space stem is not a
                # safe relative path, so this bridge asks for the explicit
                # compiled .dds basename instead; ordinary names keep the
                # historical extensionless-stem request byte-for-byte.
                compiled_identifier = pure.with_suffix(".dds").as_posix()
            compiled_indexes[position] = len(requests)
            requests.append(VisualLeafRequest(compiled_identifier, "texture"))

    batch = diagnose_visual_leaves(visual_paths, requests)
    diagnostics = {item.request_index: item for item in batch.diagnostics}
    result: list[dict[str, object]] = []
    for position, reference in enumerate(references):
        primary_index = primary_indexes[position]
        resolution = batch.resolutions[primary_index]
        diagnostic = diagnostics.get(primary_index)
        bridge_evidence: tuple[str, ...] = ()

        compiled_index = compiled_indexes.get(position)
        if (
            resolution is None
            and diagnostic is not None
            and diagnostic.status == "missing"
            and compiled_index is not None
        ):
            compiled_resolution = batch.resolutions[compiled_index]
            compiled_diagnostic = diagnostics.get(compiled_index)
            compiled_paths = (
                tuple(leaf.virtual_path for leaf in compiled_resolution.leaves)
                if compiled_resolution is not None
                else ()
            )
            if (
                len(compiled_paths) == 1
                and PurePosixPath(compiled_paths[0]).suffix.casefold() == ".dds"
            ):
                resolution = compiled_resolution
                diagnostic = None
                bridge_evidence = (
                    "w3d-compiled-texture:exact-tga-stem-to-dds",
                )
            elif (
                compiled_diagnostic is not None
                and compiled_diagnostic.status == "ambiguous"
                and compiled_diagnostic.candidates
                and all(
                    PurePosixPath(candidate).suffix.casefold() == ".dds"
                    for candidate in compiled_diagnostic.candidates
                )
            ):
                diagnostic = compiled_diagnostic

        record: dict[str, object] = {
            "sourceW3dVirtualPath": reference.metadata.virtual_path,
            "identifier": reference.identifier,
            "provenance": reference.provenance.neutral(),
        }
        if reference.texture_chunk_header_offset is not None:
            record["textureChunkHeaderOffset"] = (
                reference.texture_chunk_header_offset
            )
        if reference.shader_property_name is not None:
            record.update(
                {
                    "shaderMaterialPropertyName": reference.shader_property_name,
                    "referenceEvidence": [
                        "w3d-shader-material-property:texture-bearing-name"
                    ],
                }
            )
        if reference.shader_material_chunk_header_offset is not None:
            record["shaderMaterialChunkHeaderOffset"] = (
                reference.shader_material_chunk_header_offset
            )
        if resolution is not None:
            authored_evidence = (
                ["w3d-shader-material-property:texture-bearing-name"]
                if reference.shader_property_name is not None
                else []
            )
            record.update(
                {
                    "status": "resolved",
                    "physicalVirtualPaths": [
                        leaf.virtual_path for leaf in resolution.leaves
                    ],
                    "evidence": authored_evidence
                    + list(bridge_evidence)
                    + [
                        f"{leaf.role}:{leaf.evidence}"
                        for leaf in resolution.leaves
                    ],
                }
            )
        elif diagnostic is not None:
            record.update(
                {
                    "status": diagnostic.status,
                    "reason": diagnostic.message,
                    "candidateCount": diagnostic.candidate_count,
                    "candidates": list(diagnostic.candidates),
                }
            )
        else:
            # The visual resolver contract always returns either a resolution
            # or a diagnostic.  Retain an explicit invalid dependency if that
            # invariant is ever violated instead of silently dropping it.
            record.update(
                {
                    "status": "invalid",
                    "reason": "visual resolver returned no result",
                    "candidateCount": 0,
                    "candidates": [],
                }
            )
        result.append(record)

    def sort_key(item: dict[str, object]) -> tuple[object, ...]:
        provenance = item["provenance"]
        assert isinstance(provenance, dict)
        return (
            _sort_text(str(item["sourceW3dVirtualPath"])),
            int(provenance.get("valueOffset", 0)),
            _sort_text(str(item["identifier"])),
        )

    result.sort(key=sort_key)
    return result


def _reference_with_target(
    target_name: str, reference: TypedAssetReference
) -> dict[str, object]:
    result = {"targetObject": target_name}
    result.update(reference.neutral())
    return result


def _reference_lists(
    graph: TypedVisualGraph,
) -> tuple[list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]]:
    exact: list[dict[str, object]] = []
    semantic: list[dict[str, object]] = []
    unresolved: list[dict[str, object]] = []
    for item in graph.objects:
        for reference in item.all_references():
            record = _reference_with_target(item.name, reference)
            if reference.status == "resolved":
                exact.append(record)
            elif reference.status == "semantic":
                semantic.append(record)
            else:
                unresolved.append(record)

    def key(value: dict[str, object]) -> tuple[object, ...]:
        provenance = value["provenance"]
        assert isinstance(provenance, dict)
        return (
            str(value["targetObject"]).casefold(),
            str(value["targetObject"]),
            str(provenance["virtualPath"]).casefold(),
            str(provenance["virtualPath"]),
            int(provenance["line"]),
            str(value["kind"]),
            str(value["identifier"]).casefold(),
            str(value["identifier"]),
            str(value["usage"]),
        )

    exact.sort(key=key)
    semantic.sort(key=key)
    unresolved.sort(key=key)
    return exact, semantic, unresolved


def _object_summaries(graph: TypedVisualGraph) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for item in graph.objects:
        counts = Counter(reference.status for reference in item.all_references())
        modules: list[dict[str, object]] = []
        for module in item.draw_modules:
            module_record: dict[str, object] = {
                "moduleKind": module.module_kind,
                "inherited": module.inherited,
                "provenance": module.provenance.neutral(),
                "referenceCount": len(module.references)
                + sum(len(state.references) for state in module.states),
                "drawableActions": [
                    action.neutral() for action in module.drawable_actions
                ],
                "states": [
                    {
                        "family": state.family,
                        "conditions": list(state.conditions),
                        "lifecyclePhases": list(state.lifecycle_phases),
                        "referenceCount": len(state.references),
                        "drawableActions": [
                            action.neutral() for action in state.drawable_actions
                        ],
                        "properties": [
                            prop.neutral() for prop in state.properties
                        ],
                        "provenance": state.provenance.neutral(),
                    }
                    for state in module.states
                ],
            }
            if module.instance_tag is not None:
                module_record["instanceTag"] = module.instance_tag
            modules.append(module_record)
        result.append(
            {
                "name": item.name,
                "objectKind": item.object_kind,
                "source": {
                    "virtualPath": item.source.virtual_path,
                    "line": item.source.line,
                },
                "ancestry": list(item.ancestry),
                "inheritanceComplete": item.inheritance_complete,
                "lifecycleCoverage": list(item.lifecycle_coverage),
                "drawModuleCount": len(item.draw_modules),
                "drawModules": modules,
                "referenceSummary": {
                    key: counts.get(key, 0)
                    for key in (
                        "resolved",
                        "semantic",
                        "missing",
                        "ambiguous",
                        "invalid",
                    )
                },
            }
        )
    return result


def _closure_sources(
    cst: ResolvedSageCst,
    sources: dict[str, bytes],
) -> list[dict[str, object]]:
    roles: dict[str, set[str]] = {}
    for document in cst.documents:
        if document.virtual_path != _SYNTHETIC_ENTRY:
            roles.setdefault(document.virtual_path, set()).add("document")
    for fragment in cst.fragments:
        roles.setdefault(fragment.virtual_path, set()).add("body-fragment")

    result: list[dict[str, object]] = []
    for path in sorted(roles, key=_sort_text):
        source = sources.get(path)
        if source is None:
            raise ValueError(f"resolved SAGE source is absent from inventory: {path}")
        result.append(
            {
                "virtualPath": path,
                "roles": sorted(roles[path]),
                "byteLength": len(source),
                "sha256": hashlib.sha256(source).hexdigest(),
            }
        )
    return result


def _include_records(cst: ResolvedSageCst) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for item in cst.includes:
        if item.source_virtual_path == _SYNTHETIC_ENTRY:
            continue
        result.append(
            {
                "sourceVirtualPath": item.source_virtual_path,
                "line": item.line,
                "rawRef": item.raw_ref,
                "resolvedVirtualPath": item.resolved_virtual_path,
            }
        )
    result.sort(
        key=lambda item: (
            _sort_text(str(item["sourceVirtualPath"])),
            int(item["line"]),
            _sort_text(str(item["resolvedVirtualPath"])),
        )
    )
    return result


def default_visual_closure_report_name(object_names: Iterable[str]) -> str:
    """Return a stable private report filename for a target set."""

    targets = _validated_targets(object_names)
    identity = _canonical_sha256([item.casefold() for item in targets])[:16]
    return f"retail-visual-closure-{identity}.json"


def _assets_root_fingerprint(resolved: Path) -> str:
    """Stable-ish identity for process-local inventory memoization.

    Prefer the effective-assets manifest (written by extract-all-assets). Fall
    back to a shallow directory scan fingerprint so in-tree file changes that
    do not bump the root directory mtime still invalidate the cache.
    """

    key_parts = [str(resolved).casefold()]
    manifest = resolved / ".openbfme" / "manifest.json"
    if manifest.is_file() and not _is_link_like(manifest):
        try:
            st = manifest.stat()
            key_parts.append(f"manifest:{st.st_mtime_ns}:{st.st_size}")
            # Cheap content peek for totals if present (no full inventory walk).
            raw = manifest.read_bytes()[:4096]
            key_parts.append(f"mh:{hashlib.sha256(raw).hexdigest()[:16]}")
            return "|".join(key_parts)
        except OSError:
            pass

    # Bounded shallow walk (depth <= 2, max 2k entries) so deep-only edits
    # under common retail folders still invalidate the process-local memo.
    entries: list[tuple[str, int, int]] = []
    try:
        for dirpath, dirnames, filenames in os.walk(resolved):
            relative_dir = Path(dirpath).relative_to(resolved)
            depth = len(relative_dir.parts)
            if depth >= 2:
                dirnames[:] = []
            dirnames.sort(key=str.casefold)
            filenames.sort(key=str.casefold)
            for name in filenames:
                path = Path(dirpath) / name
                try:
                    st = path.stat()
                    rel = path.relative_to(resolved).as_posix().casefold()
                    entries.append((rel, int(st.st_mtime_ns), int(st.st_size)))
                except OSError:
                    continue
                if len(entries) >= 2000:
                    break
            if len(entries) >= 2000:
                break
    except OSError:
        try:
            st = resolved.stat()
            return f"{key_parts[0]}|root:{st.st_mtime_ns}"
        except OSError:
            return f"{key_parts[0]}|missing"
    entries.sort()
    digest = hashlib.sha256(repr(entries).encode("utf-8")).hexdigest()[:32]
    return f"{key_parts[0]}|scan:{digest}|n:{len(entries)}"


def _asset_context(effective_assets_root: Path | str) -> tuple[object, ...]:
    """Return cached inventory + SAGE/W3D catalogs for one assets root."""

    root = Path(effective_assets_root).expanduser()
    try:
        resolved = root.resolve(strict=True)
    except OSError as exc:
        raise ValueError(f"effective-assets root is unavailable: {root}") from exc
    cache_key = _assets_root_fingerprint(resolved)
    with _ASSET_CONTEXT_LOCK:
        cached = _ASSET_CONTEXT_CACHE.get(cache_key)
        if cached is not None:
            return cached
        # Drop other roots so a long-lived process cannot retain unbounded maps.
        _ASSET_CONTEXT_CACHE.clear()
        assets = _inventory_assets(resolved)
        sources = _sage_sources(assets)
        definitions = _definition_index(sources)
        w3d_paths, visual_paths, w3d_records = _path_catalogs(assets)
        initial_index = build_w3d_index(w3d_paths)
        packed = (
            assets,
            sources,
            definitions,
            w3d_paths,
            visual_paths,
            w3d_records,
            initial_index,
        )
        _ASSET_CONTEXT_CACHE[cache_key] = packed
        return packed


def clear_visual_closure_asset_cache() -> None:
    """Test helper: drop the process-local effective-assets cache."""

    with _ASSET_CONTEXT_LOCK:
        _ASSET_CONTEXT_CACHE.clear()
        _PACK_FILTERED_CONTEXT_CACHE.clear()
        _PACK_FILTERED_INFLIGHT.clear()


def _catalog_pack_filter_token(catalog: object) -> str:
    """Stable process key for one install catalog identity."""

    identity = getattr(catalog, "identity_sha256", None)
    if callable(identity):
        try:
            token = identity()
        except Exception:  # noqa: BLE001 — fall back to object id
            token = None
        if isinstance(token, str) and token:
            return token.casefold()
    return f"id:{id(catalog)}"


def _build_pack_filtered_context(
    effective_assets_root: Path | str,
    catalog: object,
) -> tuple[object, ...]:
    """Construct one catalog-filtered asset context (no memo; may be heavy)."""

    resolve_exact = getattr(catalog, "resolve_exact", None)
    if not callable(resolve_exact):
        raise TypeError("catalog must provide resolve_exact(virtual_path)")

    (
        assets,
        sources,
        definitions,
        w3d_paths,
        visual_paths,
        w3d_records,
        _initial_index,
    ) = _asset_context(effective_assets_root)

    # Build a casefolded winner set once. resolve_exact is O(1) but calling it
    # hundreds of thousands of times across objects was still measurable; one
    # set comprehension keeps the filter cheap and cacheable.
    if hasattr(catalog, "entries"):
        winners = {
            str(getattr(entry, "name", "")).replace("\\", "/").casefold()
            for entry in catalog.entries  # type: ignore[attr-defined]
            if getattr(entry, "name", None)
        }

        def _in_catalog(path: str) -> bool:
            return path.replace("\\", "/").casefold() in winners

    else:

        def _in_catalog(path: str) -> bool:
            return resolve_exact(path) is not None

    filtered_w3d = tuple(path for path in w3d_paths if _in_catalog(path))
    filtered_visual = tuple(path for path in visual_paths if _in_catalog(path))
    filtered_records = {
        path: record
        for path, record in w3d_records.items()
        if _in_catalog(path)
    }
    filtered_index = build_w3d_index(filtered_w3d, ())
    return (
        assets,
        sources,
        definitions,
        filtered_w3d,
        filtered_visual,
        filtered_records,
        filtered_index,
    )


def _pack_filtered_asset_context(
    effective_assets_root: Path | str,
    catalog: object,
    *,
    catalog_identity_sha256: str | None = None,
) -> tuple[object, ...]:
    """Return asset context with W3D/visual inventory intersected to catalog.

    Unfiltered inventory stays in ``_ASSET_CONTEXT_CACHE``. Filtered path sets
    and the path-only W3D index are memoized per (assets root, catalog id) so
    multi-object faction convert does not re-filter ~tens of thousands of paths
    and rebuild the index on every object.

    Construction is single-flight per key: concurrent convert workers wait on
    one builder instead of stampeding identical filter/index rebuilds.
    """

    root = Path(effective_assets_root).expanduser()
    try:
        resolved = root.resolve(strict=True)
    except OSError as exc:
        raise ValueError(f"effective-assets root is unavailable: {root}") from exc
    assets_key = _assets_root_fingerprint(resolved)
    if isinstance(catalog_identity_sha256, str) and catalog_identity_sha256.strip():
        catalog_token = catalog_identity_sha256.casefold().strip()
    else:
        catalog_token = _catalog_pack_filter_token(catalog)
    filter_key = (assets_key, catalog_token)

    with _ASSET_CONTEXT_LOCK:
        cached = _PACK_FILTERED_CONTEXT_CACHE.get(filter_key)
        if cached is not None:
            return cached
        inflight = _PACK_FILTERED_INFLIGHT.get(filter_key)
        if inflight is None:
            inflight = threading.Event()
            _PACK_FILTERED_INFLIGHT[filter_key] = inflight
            is_builder = True
        else:
            is_builder = False

    if not is_builder:
        # Wait for the builder; re-check cache after wake.
        if not inflight.wait(timeout=900):
            raise TimeoutError(
                "timed out waiting for pack-filtered visual inventory build"
            )
        with _ASSET_CONTEXT_LOCK:
            cached = _PACK_FILTERED_CONTEXT_CACHE.get(filter_key)
            if cached is None:
                raise RuntimeError(
                    "pack-filtered visual inventory build failed in another worker"
                )
            return cached

    try:
        packed = _build_pack_filtered_context(effective_assets_root, catalog)
        with _ASSET_CONTEXT_LOCK:
            # Bound growth: one assets root + a few catalogs is the convert shape.
            if len(_PACK_FILTERED_CONTEXT_CACHE) > 8:
                _PACK_FILTERED_CONTEXT_CACHE.clear()
            _PACK_FILTERED_CONTEXT_CACHE[filter_key] = packed
            return packed
    finally:
        with _ASSET_CONTEXT_LOCK:
            _PACK_FILTERED_INFLIGHT.pop(filter_key, None)
        inflight.set()


def effective_assets_fingerprint(effective_assets_root: Path | str) -> str:
    """Public fingerprint for DDC keys that depend on the asset tree."""

    root = Path(effective_assets_root).expanduser()
    try:
        resolved = root.resolve(strict=True)
    except OSError:
        return f"missing:{root}"
    return _assets_root_fingerprint(resolved)


def build_retail_visual_closure(
    effective_assets_root: Path | str,
    object_names: Iterable[str],
    *,
    catalog: object | None = None,
    catalog_identity_sha256: str | None = None,
) -> dict[str, object]:
    """Build a neutral conversion closure for exact Object targets.

    The return value contains no source/model bytes and no absolute input path.
    ``ready`` means the typed visual graph and scanned W3D texture dependency
    closure have neither diagnostics nor missing, ambiguous, or invalid leaves.
    A non-ready report is still useful evidence and intentionally retains every
    unresolved authored reference.

    When ``catalog`` is provided (InstallCatalog-like with ``resolve_exact``),
    physical visual/W3D inventory is intersected with archive winners. Paths
    that exist only on the effective-assets tree (orphan extract debris) are
    treated as absent so pack recipes never bind un-cookable patterns.

    Pass ``catalog_identity_sha256`` from the convert batch (already computed
    once per run) so every object does not re-serialize the full catalog.
    """

    targets = _validated_targets(object_names)
    if catalog is not None:
        (
            assets,
            sources,
            definitions,
            w3d_paths,
            visual_paths,
            w3d_records,
            initial_index,
        ) = _pack_filtered_asset_context(
            effective_assets_root,
            catalog,
            catalog_identity_sha256=catalog_identity_sha256,
        )
    else:
        (
            assets,
            sources,
            definitions,
            w3d_paths,
            visual_paths,
            w3d_records,
            initial_index,
        ) = _asset_context(effective_assets_root)
    target_records, definition_closure, missing_definitions = _definition_closure(
        targets, definitions
    )

    selected_source_paths = tuple(
        sorted(
            {item.virtual_path for item in definition_closure},
            key=_sort_text,
        )
    )
    entry_source = _synthetic_entry(selected_source_paths)
    if _SYNTHETIC_ENTRY.casefold() in {path.casefold() for path in sources}:
        raise ValueError(
            f"effective asset collides with reserved entry path: {_SYNTHETIC_ENTRY}"
        )
    documents = dict(sources)
    documents[_SYNTHETIC_ENTRY] = entry_source
    cst = resolve_sage_documents(_SYNTHETIC_ENTRY, documents)
    initial_graph = resolve_typed_visual_graph(
        cst, targets, initial_index, visual_paths
    )
    initial_candidate_paths = _scan_candidate_paths(initial_graph, w3d_paths)
    initial_scanned = _scan_w3d_candidates(initial_candidate_paths, w3d_records)
    hierarchy_candidate_paths = _scan_hierarchy_candidate_paths(
        initial_scanned, w3d_paths, initial_candidate_paths
    )
    hierarchy_scanned = _scan_w3d_candidates(
        hierarchy_candidate_paths, w3d_records
    )
    candidate_paths = tuple(
        sorted(
            {*initial_candidate_paths, *hierarchy_candidate_paths}, key=_sort_text
        )
    )
    scanned = tuple(
        sorted((*initial_scanned, *hierarchy_scanned), key=lambda item: _sort_text(item.virtual_path))
    )
    headers: tuple[W3DFileHeaders, ...] = tuple(
        _trimmed_file_headers(item) for item in scanned
    )
    final_index = build_w3d_index(w3d_paths, headers)
    graph = resolve_typed_visual_graph(cst, targets, final_index, visual_paths)

    ambiguous_objects = [
        item for item in graph.diagnostics if item.code == "ambiguous-object"
    ]
    if ambiguous_objects:
        first = ambiguous_objects[0]
        raise ValueError(
            f"ambiguous Object definition after include expansion: {first.object_name}"
        )

    exact, semantic, unresolved = _reference_lists(graph)
    graph_diagnostics = [item.neutral() for item in graph.diagnostics]
    closure_sources = _closure_sources(cst, documents)
    embedded_textures = _embedded_texture_dependencies(scanned, visual_paths)
    scanned_records = [
        {
            "virtualPath": item.virtual_path,
            "byteLength": item.byte_length,
            "sha256": item.source_sha256,
            "headerIds": _trimmed_file_headers(item).neutral(),
            "modelHierarchyIdentifiers": _model_hierarchy_identifiers(item),
            "embeddedAnimationChannelCount": _embedded_animation_channel_count(item),
            "skinnedMeshCount": _skinned_mesh_count(item),
            "meshCount": len(item.mesh_headers),
            # W3D_MESH_FLAG_HIDDEN (0x1000): retail walk-surface / docking
            # proxies (P1/P2/R1 decks and ramps). Drives the no-bake decision
            # for HLOD structures whose named pivots must survive.
            "hiddenMeshCount": sum(
                1 for header in item.mesh_headers if int(header.attributes) & 0x00001000
            ),
            "modelReferences": [
                reference.neutral() for reference in item.model_references
            ],
            "secondaryGeometryStreamCount": len(item.secondary_geometry_streams),
            "warnings": [warning.neutral() for warning in item.warnings],
        }
        for item in scanned
    ]
    embedded_texture_counts = Counter(
        str(item["status"]) for item in embedded_textures
    )
    dependency_closure: dict[str, object] = {
        "readBoundary": {
            "policy": (
                "resolved-target-w3d-leaves-plus-exact-unresolved-candidates"
            ),
            "uniqueVirtualPaths": list(candidate_paths),
            "uniqueReadCount": len(scanned_records),
            "byteLength": sum(item.byte_length for item in scanned),
        },
        "embeddedTextures": embedded_textures,
        "summary": {
            "fileCount": len(scanned_records),
            "embeddedTextureReferenceCount": len(embedded_textures),
            "resolvedEmbeddedTextureCount": embedded_texture_counts.get(
                "resolved", 0
            ),
            "missingEmbeddedTextureCount": embedded_texture_counts.get(
                "missing", 0
            ),
            "ambiguousEmbeddedTextureCount": embedded_texture_counts.get(
                "ambiguous", 0
            ),
            "invalidEmbeddedTextureCount": embedded_texture_counts.get(
                "invalid", 0
            ),
        },
    }
    dependency_closure["aggregateSha256"] = _canonical_sha256(
        dependency_closure
    )
    embedded_texture_gap_count = sum(
        count
        for status, count in embedded_texture_counts.items()
        if status != "resolved"
    )
    report: dict[str, object] = {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "targets": list(target_records),
        "definitionClosure": [item.neutral() for item in definition_closure],
        "missingDefinitions": list(missing_definitions),
        "sourceClosure": {
            "paths": closure_sources,
            "includes": _include_records(cst),
        },
        "catalog": {
            "assetPathCount": len(assets),
            "sageSourcePathCount": len(sources),
            "w3dPathCount": len(w3d_paths),
            "visualPathCount": len(visual_paths),
            "w3dCatalogMode": "path-only-plus-targeted-headers",
            "packArchiveIdentityFilter": catalog is not None,
        },
        "objects": _object_summaries(graph),
        "exactLeaves": exact,
        "semanticLeaves": semantic,
        "unresolved": {
            "graphDiagnostics": graph_diagnostics,
            "references": unresolved,
        },
        "scannedW3d": scanned_records,
        "w3dDependencyClosure": dependency_closure,
        "summary": {
            "targetCount": len(targets),
            "resolvedTargetCount": len(graph.objects),
            "definitionClosureCount": len(definition_closure),
            "sourceClosureCount": len(closure_sources),
            "exactLeafCount": len(exact),
            "semanticLeafCount": len(semantic),
            "unresolvedReferenceCount": len(unresolved),
            "graphDiagnosticCount": len(graph_diagnostics),
            "scannedW3dCount": len(scanned_records),
            "scannedW3dByteLength": sum(item.byte_length for item in scanned),
            "embeddedTextureReferenceCount": len(embedded_textures),
            "resolvedEmbeddedTextureCount": embedded_texture_counts.get(
                "resolved", 0
            ),
            "unresolvedEmbeddedTextureCount": embedded_texture_gap_count,
            "ready": bool(
                not missing_definitions
                and not graph_diagnostics
                and not unresolved
                and not embedded_texture_gap_count
                and len(graph.objects) == len(targets)
            ),
        },
    }
    report["aggregateSha256"] = _canonical_sha256(report)
    return report


__all__ = [
    "REPORT_SCHEMA",
    "REPORT_SCHEMA_VERSION",
    "build_retail_visual_closure",
    "clear_visual_closure_asset_cache",
    "default_visual_closure_report_name",
    "effective_assets_fingerprint",
]
