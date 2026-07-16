"""Exact Road-definition and texture closure for retail SAGE maps.

The effective-assets tree is treated as a read-only winning virtual file
system.  Only ``data/ini/roads.ini`` is parsed and only explicitly requested
``Road`` definitions are selected.  Texture resolution is deliberately exact:
an authored filename/stem may identify one physical visual leaf, with the sole
representation bridge being an absent authored TGA to one exact-stem DDS.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
from typing import Iterable

from .paths import safe_relative_parts
from .sage_cst import strip_sage_comments
from .visual_leaf import VisualLeafRequest, diagnose_visual_leaves


REPORT_SCHEMA = "openbfme.retail-road-closure"
REPORT_SCHEMA_VERSION = 1
ROADS_VIRTUAL_PATH = "data/ini/roads.ini"

MAX_ASSET_FILES = 100_000
MAX_ROAD_IDS = 4_096
MAX_ROAD_ID_LENGTH = 255
MAX_ROADS_SOURCE_BYTES = 16 * 1024 * 1024

_VISUAL_SUFFIXES = frozenset({".dds", ".tga", ".jpg", ".png"})
_ROAD_HEADER = re.compile(r"^Road\s+(\S+)\s*$", re.IGNORECASE)
_ASSIGNMENT = re.compile(r"^([A-Za-z][A-Za-z0-9]*)\s*=\s*(.*?)\s*$")
_DECIMAL = re.compile(r"^(?:\d+(?:\.\d*)?|\.\d+)$")
_SAFE_ROAD_ID = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
_REQUIRED_FIELDS = (
    ("texture", "Texture"),
    ("roadwidth", "RoadWidth"),
    ("roadwidthintexture", "RoadWidthInTexture"),
)


def _sort_text(value: str) -> tuple[str, str]:
    return value.casefold(), value


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


@dataclass(frozen=True, slots=True)
class _AssetFile:
    virtual_path: str
    physical_path: Path
    byte_length: int


@dataclass(frozen=True, slots=True)
class _RoadField:
    key: str
    value: str
    line: int

    def neutral(self, virtual_path: str) -> dict[str, object]:
        return {
            "authoredKey": self.key,
            "authoredValue": self.value,
            "provenance": {
                "virtualPath": virtual_path,
                "line": self.line,
            },
        }


@dataclass(frozen=True, slots=True)
class _RoadDefinition:
    road_id: str
    line: int
    end_line: int
    fields: tuple[_RoadField, ...]

    def occurrences(self, key: str) -> tuple[_RoadField, ...]:
        folded = key.casefold()
        return tuple(item for item in self.fields if item.key.casefold() == folded)

    def provenance(self) -> dict[str, object]:
        return {
            "virtualPath": ROADS_VIRTUAL_PATH,
            "line": self.line,
            "endLine": self.end_line,
        }


def _validated_road_ids(road_ids: Iterable[str]) -> tuple[str, ...]:
    selected: dict[str, str] = {}
    for value in road_ids:
        if len(selected) >= MAX_ROAD_IDS:
            raise ValueError(f"target Road count exceeds {MAX_ROAD_IDS} limit")
        if (
            not isinstance(value, str)
            or not value
            or value != value.strip()
            or len(value) > MAX_ROAD_ID_LENGTH
            or ".." in value
            or _SAFE_ROAD_ID.fullmatch(value) is None
        ):
            raise ValueError(f"unsafe target Road id: {value!r}")
        key = value.casefold()
        if key in selected:
            raise ValueError(f"duplicate target Road id: {value!r}")
        selected[key] = value
    if not selected:
        raise ValueError("at least one target Road id is required")
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

    assets: list[_AssetFile] = []
    folded_paths: dict[str, str] = {}
    for raw_directory, directory_names, file_names in os.walk(
        resolved, topdown=True, followlinks=False
    ):
        directory = Path(raw_directory)
        directory_names.sort(key=_sort_text)
        file_names.sort(key=_sort_text)
        for name in directory_names:
            child = directory / name
            if _is_link_like(child):
                raise ValueError(f"effective-assets tree contains a link: {child}")
            safe_relative_parts(child.relative_to(resolved).as_posix())
        for name in file_names:
            physical = directory / name
            if _is_link_like(physical):
                raise ValueError(f"effective-assets tree contains a link: {physical}")
            virtual_path = "/".join(
                safe_relative_parts(physical.relative_to(resolved).as_posix())
            )
            folded = virtual_path.casefold()
            previous = folded_paths.get(folded)
            if previous is not None:
                raise ValueError(
                    "case-ambiguous effective asset paths: "
                    f"{previous!r}, {virtual_path!r}"
                )
            folded_paths[folded] = virtual_path
            try:
                byte_length = physical.stat().st_size
            except OSError as exc:
                raise ValueError(f"cannot stat effective asset: {virtual_path}") from exc
            assets.append(_AssetFile(virtual_path, physical, byte_length))
            if len(assets) > MAX_ASSET_FILES:
                raise ValueError(
                    f"effective asset count exceeds {MAX_ASSET_FILES} limit"
                )
    assets.sort(key=lambda item: _sort_text(item.virtual_path))
    return tuple(assets)


def _read_stable(asset: _AssetFile) -> bytes:
    source = asset.physical_path.read_bytes()
    if len(source) != asset.byte_length:
        raise ValueError(
            f"effective asset changed while reading: {asset.virtual_path}"
        )
    return source


def _roads_source(assets: tuple[_AssetFile, ...]) -> _AssetFile:
    candidates = tuple(
        item
        for item in assets
        if item.virtual_path.casefold() == ROADS_VIRTUAL_PATH.casefold()
    )
    if not candidates:
        raise ValueError(f"missing required retail source: {ROADS_VIRTUAL_PATH}")
    if len(candidates) != 1:
        paths = ", ".join(item.virtual_path for item in candidates)
        raise ValueError(f"ambiguous retail roads source: {paths}")
    source = candidates[0]
    if source.byte_length > MAX_ROADS_SOURCE_BYTES:
        raise ValueError(
            f"roads source exceeds {MAX_ROADS_SOURCE_BYTES} byte limit"
        )
    return source


def _parse_roads(source: bytes) -> tuple[_RoadDefinition, ...]:
    if b"\0" in source:
        raise ValueError(f"SAGE source contains a NUL byte: {ROADS_VIRTUAL_PATH}")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError(
            f"SAGE source is not valid CP1252: {ROADS_VIRTUAL_PATH}"
        ) from exc

    definitions: list[_RoadDefinition] = []
    current_id: str | None = None
    current_line = 0
    current_fields: list[_RoadField] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = strip_sage_comments(raw_line).strip()
        if not line:
            continue
        if current_id is None:
            match = _ROAD_HEADER.fullmatch(line)
            if match is not None:
                current_id = match.group(1)
                current_line = line_number
                current_fields = []
            continue

        if line.casefold() == "end":
            definitions.append(
                _RoadDefinition(
                    current_id,
                    current_line,
                    line_number,
                    tuple(current_fields),
                )
            )
            current_id = None
            current_line = 0
            current_fields = []
            continue
        if _ROAD_HEADER.fullmatch(line) is not None:
            raise ValueError(
                f"nested Road block at {ROADS_VIRTUAL_PATH}:{line_number}"
            )
        assignment = _ASSIGNMENT.fullmatch(line)
        if assignment is not None:
            current_fields.append(
                _RoadField(assignment.group(1), assignment.group(2), line_number)
            )

    if current_id is not None:
        raise ValueError(
            f"unterminated Road {current_id!r} at "
            f"{ROADS_VIRTUAL_PATH}:{current_line}"
        )
    definitions.sort(
        key=lambda item: (_sort_text(item.road_id), item.line, item.end_line)
    )
    return tuple(definitions)


def _definition_index(
    definitions: tuple[_RoadDefinition, ...],
) -> dict[str, tuple[_RoadDefinition, ...]]:
    grouped: dict[str, list[_RoadDefinition]] = {}
    for definition in definitions:
        grouped.setdefault(definition.road_id.casefold(), []).append(definition)
    return {key: tuple(values) for key, values in grouped.items()}


def _diagnostic(
    code: str,
    road_id: str,
    message: str,
    **values: object,
) -> dict[str, object]:
    result: dict[str, object] = {
        "code": code,
        "roadId": road_id,
        "message": message,
    }
    result.update(values)
    return result


def _normalized_positive_decimal(value: str) -> str | None:
    if _DECIMAL.fullmatch(value) is None:
        return None
    try:
        parsed = Decimal(value)
    except InvalidOperation:
        return None
    if not parsed.is_finite() or parsed <= 0:
        return None
    return format(parsed.normalize(), "f")


def _field_records(
    definition: _RoadDefinition,
) -> tuple[dict[str, object], tuple[dict[str, object], ...], _RoadField | None]:
    fields: dict[str, object] = {}
    diagnostics: list[dict[str, object]] = []
    texture: _RoadField | None = None
    for folded, canonical in _REQUIRED_FIELDS:
        occurrences = definition.occurrences(folded)
        if not occurrences:
            diagnostics.append(
                _diagnostic(
                    "missing-road-field",
                    definition.road_id,
                    f"Road definition is missing required {canonical} field",
                    field=canonical,
                    provenance=definition.provenance(),
                )
            )
            continue
        if len(occurrences) != 1:
            diagnostics.append(
                _diagnostic(
                    "duplicate-road-field",
                    definition.road_id,
                    f"Road definition has multiple {canonical} fields",
                    field=canonical,
                    occurrences=[
                        item.neutral(ROADS_VIRTUAL_PATH) for item in occurrences
                    ],
                )
            )
            continue
        field = occurrences[0]
        record = field.neutral(ROADS_VIRTUAL_PATH)
        if folded == "texture":
            texture = field
        else:
            normalized = _normalized_positive_decimal(field.value)
            if normalized is None:
                diagnostics.append(
                    _diagnostic(
                        "malformed-road-width",
                        definition.road_id,
                        f"{canonical} must be a positive finite decimal",
                        field=canonical,
                        authoredValue=field.value,
                        provenance=record["provenance"],
                    )
                )
            else:
                record["normalizedValue"] = normalized
        fields[canonical] = record
    return fields, tuple(diagnostics), texture


def _texture_closure(
    texture: _RoadField,
    visual_paths: tuple[str, ...],
    assets_by_path: dict[str, _AssetFile],
) -> tuple[dict[str, object], dict[str, object] | None]:
    requests = [VisualLeafRequest(texture.value, "texture")]
    bridge_index: int | None = None
    pure = PurePosixPath(texture.value)
    if pure.suffix.casefold() == ".tga":
        bridge_index = len(requests)
        requests.append(VisualLeafRequest(pure.with_suffix("").as_posix(), "texture"))
    batch = diagnose_visual_leaves(visual_paths, requests)
    diagnostics = {item.request_index: item for item in batch.diagnostics}
    resolution = batch.resolutions[0]
    failure = diagnostics.get(0)
    bridge_evidence: tuple[str, ...] = ()

    if (
        resolution is None
        and failure is not None
        and failure.status == "missing"
        and bridge_index is not None
    ):
        bridge = batch.resolutions[bridge_index]
        bridge_failure = diagnostics.get(bridge_index)
        bridge_paths = (
            tuple(leaf.virtual_path for leaf in bridge.leaves)
            if bridge is not None
            else ()
        )
        if (
            len(bridge_paths) == 1
            and PurePosixPath(bridge_paths[0]).suffix.casefold() == ".dds"
        ):
            resolution = bridge
            failure = None
            bridge_evidence = (
                "sage-road-compiled-texture:exact-tga-stem-to-dds",
            )
        elif bridge_failure is not None and bridge_failure.status == "ambiguous":
            failure = bridge_failure

    provenance = {
        "virtualPath": ROADS_VIRTUAL_PATH,
        "line": texture.line,
        "field": texture.key,
    }
    if resolution is not None:
        if len(resolution.leaves) != 1:
            raise ValueError("Road texture resolver returned multiple physical leaves")
        leaf = resolution.leaves[0]
        asset = assets_by_path.get(leaf.virtual_path.casefold())
        if asset is None:
            raise ValueError(
                f"resolved Road texture is absent from inventory: {leaf.virtual_path}"
            )
        payload = _read_stable(asset)
        return (
            {
                "identifier": texture.value,
                "status": "resolved",
                "physicalVirtualPath": leaf.virtual_path,
                "byteLength": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "evidence": list(bridge_evidence)
                + [f"{leaf.role}:{leaf.evidence}"],
                "provenance": provenance,
            },
            None,
        )

    if failure is None:
        status = "invalid"
        message = "Road texture resolver returned no result"
        candidates: list[str] = []
    else:
        status = failure.status
        message = failure.message
        candidates = list(failure.candidates)
    record = {
        "identifier": texture.value,
        "status": status,
        "candidateCount": len(candidates),
        "candidates": candidates,
        "reason": message,
        "provenance": provenance,
    }
    return (
        record,
        _diagnostic(
            f"{status}-road-texture",
            "",
            message,
            identifier=texture.value,
            candidates=candidates,
            provenance=provenance,
        ),
    )


def default_road_closure_report_name(road_ids: Iterable[str]) -> str:
    """Return a stable private report filename for a case-insensitive target set."""

    targets = _validated_road_ids(road_ids)
    identity = _canonical_sha256([item.casefold() for item in targets])[:16]
    return f"retail-road-closure-{identity}.json"


def build_road_closure(
    effective_assets_root: Path | str,
    road_ids: Iterable[str],
) -> dict[str, object]:
    """Build a deterministic exact Road-to-texture conversion closure."""

    targets = _validated_road_ids(road_ids)
    assets = _inventory_assets(Path(effective_assets_root))
    source_asset = _roads_source(assets)
    source_payload = _read_stable(source_asset)
    definitions = _parse_roads(source_payload)
    index = _definition_index(definitions)
    visual_paths = tuple(
        item.virtual_path
        for item in assets
        if PurePosixPath(item.virtual_path).suffix.casefold() in _VISUAL_SUFFIXES
    )
    assets_by_path = {item.virtual_path.casefold(): item for item in assets}

    roads: list[dict[str, object]] = []
    diagnostics: list[dict[str, object]] = []
    for requested_id in targets:
        candidates = index.get(requested_id.casefold(), ())
        if not candidates:
            item = {
                "requestedId": requested_id,
                "status": "missing-definition",
            }
            roads.append(item)
            diagnostics.append(
                _diagnostic(
                    "missing-road-definition",
                    requested_id,
                    f"missing exact Road definition {requested_id!r}",
                )
            )
            continue
        if len(candidates) != 1:
            locations = [item.provenance() for item in candidates]
            roads.append(
                {
                    "requestedId": requested_id,
                    "status": "ambiguous-definition",
                    "candidates": [
                        {"id": item.road_id, "provenance": item.provenance()}
                        for item in candidates
                    ],
                }
            )
            diagnostics.append(
                _diagnostic(
                    "ambiguous-road-definition",
                    requested_id,
                    f"multiple case-insensitive exact Road definitions for {requested_id!r}",
                    candidates=locations,
                )
            )
            continue

        definition = candidates[0]
        fields, field_diagnostics, texture = _field_records(definition)
        record: dict[str, object] = {
            "requestedId": requested_id,
            "id": definition.road_id,
            "provenance": definition.provenance(),
            "fields": fields,
        }
        diagnostics.extend(field_diagnostics)
        if texture is None:
            record["status"] = "invalid-definition"
            roads.append(record)
            continue

        texture_record, texture_diagnostic = _texture_closure(
            texture, visual_paths, assets_by_path
        )
        record["textureLeaf"] = texture_record
        if texture_diagnostic is not None:
            texture_diagnostic["roadId"] = definition.road_id
            diagnostics.append(texture_diagnostic)
            record["status"] = (
                "invalid-definition"
                if field_diagnostics
                else "unresolved-texture"
            )
        elif field_diagnostics:
            record["status"] = "invalid-definition"
        else:
            record["status"] = "resolved"
        roads.append(record)

    diagnostics.sort(
        key=lambda item: (
            _sort_text(str(item["roadId"])),
            str(item["code"]),
            int(dict(item.get("provenance", {})).get("line", 0)),
            str(item["message"]),
        )
    )
    status_counts: dict[str, int] = {}
    for item in roads:
        status = str(item["status"])
        status_counts[status] = status_counts.get(status, 0) + 1
    resolved_texture_count = sum(
        1
        for item in roads
        if isinstance(item.get("textureLeaf"), dict)
        and dict(item["textureLeaf"])["status"] == "resolved"
    )
    unresolved_texture_count = sum(
        1
        for item in roads
        if isinstance(item.get("textureLeaf"), dict)
        and dict(item["textureLeaf"])["status"] != "resolved"
    )
    report: dict[str, object] = {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "source": {
            "virtualPath": source_asset.virtual_path,
            "byteLength": len(source_payload),
            "sha256": hashlib.sha256(source_payload).hexdigest(),
        },
        "resolutionPolicy": {
            "roadId": "case-insensitive-exact-unique-definition",
            "texture": "exact-filename-or-stem",
            "representationBridge": "absent-authored-tga-to-unique-exact-stem-dds-only",
        },
        "catalog": {
            "assetPathCount": len(assets),
            "visualPathCount": len(visual_paths),
            "roadDefinitionCount": len(definitions),
        },
        "roads": roads,
        "diagnostics": diagnostics,
        "summary": {
            "targetCount": len(targets),
            "resolvedRoadCount": status_counts.get("resolved", 0),
            "resolvedTextureCount": resolved_texture_count,
            "missingDefinitionCount": status_counts.get("missing-definition", 0),
            "ambiguousDefinitionCount": status_counts.get(
                "ambiguous-definition", 0
            ),
            "invalidDefinitionCount": status_counts.get("invalid-definition", 0),
            "unresolvedTextureCount": unresolved_texture_count,
            "gapCount": len(diagnostics),
            "ready": not diagnostics
            and status_counts.get("resolved", 0) == len(targets),
        },
    }
    report["aggregateSha256"] = _canonical_sha256(report)
    return report


__all__ = [
    "REPORT_SCHEMA",
    "REPORT_SCHEMA_VERSION",
    "ROADS_VIRTUAL_PATH",
    "build_road_closure",
    "default_road_closure_report_name",
]
