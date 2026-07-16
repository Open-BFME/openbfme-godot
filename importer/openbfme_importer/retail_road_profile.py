"""Derive a private ImportProfile from an exact retail Road closure report.

This module is intentionally a profile generator, not a resolver.  It accepts
only the evidence already frozen by :mod:`openbfme_importer.sage_roads`, adds
one exact DDS-to-PNG resource for each unique physical Road texture, and emits
the runtime material table consumed beside the cooked Fords map.  No texture,
width, identifier, or output collision is guessed through.
"""

from __future__ import annotations

import copy
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Iterable, Mapping

from .paths import safe_relative_parts
from .profile import (
    ImportProfile,
    MAX_PATH_LENGTH,
    MAX_PROFILE_BYTES,
    MAX_RESOURCES,
    SLUG_PATTERN,
)
from .sage_roads import REPORT_SCHEMA, REPORT_SCHEMA_VERSION, ROADS_VIRTUAL_PATH


RUNTIME_SCHEMA = "openbfme.sage-road-materials"
RUNTIME_SCHEMA_VERSION = 0
FORDS_MAP_OUTPUT = "maps/fords-of-isen-ii"
ROAD_MATERIALS_RELATIVE_PATH = "road-materials.json"
ROAD_MATERIALS_RUNTIME_PATH = (
    f"{FORDS_MAP_OUTPUT}/{ROAD_MATERIALS_RELATIVE_PATH}"
)
ROAD_TEXTURE_OUTPUT_ROOT = f"{FORDS_MAP_OUTPUT}/road-materials/textures"

MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_ROADS = 4_096
MAX_WIDTH = Decimal("1000000")
MAX_WIDTH_IN_TEXTURE = Decimal("1")

_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_SAFE_OUTPUT_STEM = re.compile(r"^[a-z0-9][a-z0-9._-]{0,127}$")
_EXPECTED_RESOLUTION_POLICY = {
    "roadId": "case-insensitive-exact-unique-definition",
    "texture": "exact-filename-or-stem",
    "representationBridge": (
        "absent-authored-tga-to-unique-exact-stem-dds-only"
    ),
}


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number is not allowed: {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON object key: {key!r}")
        result[key] = value
    return result


def _load_json_object(path: Path | str, *, maximum: int, label: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    try:
        size = source.stat().st_size
    except OSError as exc:
        raise ValueError(f"{label} is unavailable: {source}") from exc
    if size > maximum:
        raise ValueError(f"{label} exceeds {maximum} byte limit: {source}")
    try:
        value = json.loads(
            source.read_bytes().decode("utf-8"),
            parse_constant=_reject_json_constant,
            object_pairs_hook=_unique_object,
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label} JSON in {source}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} root must be an object")
    return value


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _integer(value: object, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return value


def _sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or _SHA256.fullmatch(value) is None:
        raise ValueError(f"{label} must be a lowercase SHA-256 digest")
    return value


def _safe_path(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) > MAX_PATH_LENGTH
        or "\\" in value
        or any(character in value for character in '<>"|?*')
    ):
        raise ValueError(f"{label} must be a bounded relative path")
    try:
        parts = safe_relative_parts(value)
    except ValueError as exc:
        raise ValueError(f"unsafe {label}: {value!r}") from exc
    normalized = "/".join(parts)
    if normalized != value:
        raise ValueError(f"{label} must be a canonical relative path")
    return normalized


def _safe_road_id(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or value != value.strip()
        or len(value) > 255
        or ".." in value
        or re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", value) is None
    ):
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _expected_ids(values: Iterable[str] | None) -> tuple[str, ...] | None:
    if values is None:
        return None
    result: dict[str, str] = {}
    for raw in values:
        value = _safe_road_id(raw, "expected Road id")
        folded = value.casefold()
        if folded in result:
            raise ValueError(f"duplicate expected Road id: {value!r}")
        result[folded] = value
        if len(result) > MAX_ROADS:
            raise ValueError(f"expected Road count exceeds {MAX_ROADS} limit")
    if not result:
        raise ValueError("expected Road ids cannot be empty")
    return tuple(sorted(result.values(), key=lambda item: (item.casefold(), item)))


def _canonical_decimal(
    value: object,
    label: str,
    *,
    maximum: Decimal,
) -> str:
    if not isinstance(value, str) or not value or len(value) > 64:
        raise ValueError(f"{label} must be a canonical decimal string")
    if re.fullmatch(r"(?:0|[1-9][0-9]*)(?:\.[0-9]+)?", value) is None:
        raise ValueError(f"{label} must be a canonical decimal string")
    try:
        decimal = Decimal(value)
    except InvalidOperation as exc:
        raise ValueError(f"{label} is not a supported decimal") from exc
    if not decimal.is_finite() or decimal <= 0 or decimal > maximum:
        raise ValueError(f"{label} is outside the supported range")
    normalized = format(decimal.normalize(), "f")
    if normalized != value:
        raise ValueError(f"{label} is not canonically normalized")
    return value


def _validate_authored_decimal(
    field: Mapping[str, Any],
    normalized: str,
    label: str,
) -> None:
    authored = field.get("authoredValue")
    if (
        not isinstance(authored, str)
        or not authored
        or len(authored) > 64
        or re.fullmatch(r"(?:\d+(?:\.\d*)?|\.\d+)", authored) is None
    ):
        raise ValueError(f"{label} has an unsupported authored decimal")
    try:
        authored_decimal = Decimal(authored)
        normalized_decimal = Decimal(normalized)
    except InvalidOperation as exc:
        raise ValueError(f"{label} has an unsupported authored decimal") from exc
    if authored_decimal != normalized_decimal:
        raise ValueError(f"{label} authored and normalized values disagree")


def _validate_report(
    report: Mapping[str, Any],
    expected_road_ids: Iterable[str] | None,
) -> tuple[list[dict[str, Any]], str]:
    if report.get("schema") != REPORT_SCHEMA:
        raise ValueError("unsupported Road closure report schema")
    if report.get("schemaVersion") != REPORT_SCHEMA_VERSION:
        raise ValueError("unsupported Road closure report schemaVersion")

    aggregate = _sha256(
        report.get("aggregateSha256"), "Road closure aggregateSha256"
    )
    hash_basis = copy.deepcopy(dict(report))
    hash_basis.pop("aggregateSha256", None)
    actual_aggregate = _canonical_sha256(hash_basis)
    if actual_aggregate != aggregate:
        raise ValueError("Road closure report aggregate digest mismatch")

    if report.get("resolutionPolicy") != _EXPECTED_RESOLUTION_POLICY:
        raise ValueError("Road closure report uses an unsupported resolution policy")
    source = report.get("source")
    if not isinstance(source, dict):
        raise ValueError("Road closure report source must be an object")
    if source.get("virtualPath") != ROADS_VIRTUAL_PATH:
        raise ValueError("Road closure report has an unexpected source path")
    _integer(source.get("byteLength"), "Road closure source byteLength", minimum=1)
    _sha256(source.get("sha256"), "Road closure source sha256")

    catalog = report.get("catalog")
    if not isinstance(catalog, dict):
        raise ValueError("Road closure catalog must be an object")
    asset_path_count = _integer(
        catalog.get("assetPathCount"), "Road closure catalog.assetPathCount", minimum=1
    )
    visual_path_count = _integer(
        catalog.get("visualPathCount"), "Road closure catalog.visualPathCount", minimum=1
    )
    road_definition_count = _integer(
        catalog.get("roadDefinitionCount"),
        "Road closure catalog.roadDefinitionCount",
        minimum=1,
    )
    if visual_path_count > asset_path_count:
        raise ValueError("Road closure catalog visual path count is inconsistent")

    diagnostics = report.get("diagnostics")
    if not isinstance(diagnostics, list):
        raise ValueError("Road closure diagnostics must be an array")
    if diagnostics:
        raise ValueError("Road closure report contains conversion gaps")
    roads_value = report.get("roads")
    if (
        not isinstance(roads_value, list)
        or not roads_value
        or len(roads_value) > MAX_ROADS
    ):
        raise ValueError(f"Road closure roads must contain 1..{MAX_ROADS} records")

    summary = report.get("summary")
    if not isinstance(summary, dict):
        raise ValueError("Road closure summary must be an object")
    if summary.get("ready") is not True:
        raise ValueError("Road closure report is not ready")
    zero_fields = (
        "missingDefinitionCount",
        "ambiguousDefinitionCount",
        "invalidDefinitionCount",
        "unresolvedTextureCount",
        "gapCount",
    )
    for key in zero_fields:
        if _integer(summary.get(key), f"Road closure summary.{key}") != 0:
            raise ValueError(f"Road closure summary.{key} reports a gap")
    road_count = len(roads_value)
    if road_definition_count < road_count:
        raise ValueError("Road closure catalog Road definition count is inconsistent")
    for key in ("targetCount", "resolvedRoadCount", "resolvedTextureCount"):
        if _integer(summary.get(key), f"Road closure summary.{key}") != road_count:
            raise ValueError(f"Road closure summary.{key} count mismatch")

    expected = _expected_ids(expected_road_ids)
    road_ids: dict[str, str] = {}
    texture_paths: dict[str, str] = {}
    texture_facts: dict[str, tuple[int, str]] = {}
    roads: list[dict[str, Any]] = []
    for index, raw in enumerate(roads_value):
        if not isinstance(raw, dict):
            raise ValueError(f"Road closure record {index} must be an object")
        road_id = _safe_road_id(raw.get("id"), f"Road record {index} id")
        requested_id = _safe_road_id(
            raw.get("requestedId"), f"Road record {index} requestedId"
        )
        if requested_id.casefold() != road_id.casefold():
            raise ValueError(f"Road {road_id!r} requestedId is inconsistent")
        folded_id = road_id.casefold()
        if folded_id in road_ids:
            raise ValueError(
                f"duplicate or case-ambiguous Road id: {road_ids[folded_id]!r}, {road_id!r}"
            )
        road_ids[folded_id] = road_id
        if raw.get("status") != "resolved":
            raise ValueError(f"Road {road_id!r} is not exactly resolved")

        fields = raw.get("fields")
        if not isinstance(fields, dict):
            raise ValueError(f"Road {road_id!r} fields must be an object")
        texture_field = fields.get("Texture")
        width_field = fields.get("RoadWidth")
        width_texture_field = fields.get("RoadWidthInTexture")
        if not all(
            isinstance(value, dict)
            for value in (texture_field, width_field, width_texture_field)
        ):
            raise ValueError(f"Road {road_id!r} is missing required fields")
        if (
            texture_field.get("authoredKey") != "Texture"
            or width_field.get("authoredKey") != "RoadWidth"
            or width_texture_field.get("authoredKey") != "RoadWidthInTexture"
        ):
            raise ValueError(f"Road {road_id!r} has inconsistent authored field keys")

        texture_leaf = raw.get("textureLeaf")
        if not isinstance(texture_leaf, dict) or texture_leaf.get("status") != "resolved":
            raise ValueError(f"Road {road_id!r} has an unresolved texture")
        identifier = texture_leaf.get("identifier")
        if not isinstance(identifier, str) or not identifier or len(identifier) > MAX_PATH_LENGTH:
            raise ValueError(f"Road {road_id!r} has an unsafe texture identifier")
        _safe_path(identifier, f"Road {road_id!r} texture identifier")
        if texture_field.get("authoredValue") != identifier:
            raise ValueError(f"Road {road_id!r} texture identifier is inconsistent")

        evidence = texture_leaf.get("evidence")
        if (
            not isinstance(evidence, list)
            or not evidence
            or any(not isinstance(item, str) or not item for item in evidence)
            or not any(item.startswith("texture:") for item in evidence)
        ):
            raise ValueError(f"Road {road_id!r} texture evidence is incomplete")

        physical_path = _safe_path(
            texture_leaf.get("physicalVirtualPath"),
            f"Road {road_id!r} physical texture path",
        )
        if PurePosixPath(physical_path).suffix.casefold() != ".dds":
            raise ValueError(
                f"Road {road_id!r} texture is not an exact resolved DDS"
            )
        if (
            PurePosixPath(identifier).suffix.casefold() == ".tga"
            and "sage-road-compiled-texture:exact-tga-stem-to-dds" not in evidence
        ):
            raise ValueError(
                f"Road {road_id!r} lacks the exact TGA-to-DDS bridge evidence"
            )
        path_key = physical_path.casefold()
        previous_path = texture_paths.get(path_key)
        if previous_path is not None and previous_path != physical_path:
            raise ValueError(
                "case-ambiguous Road texture paths: "
                f"{previous_path!r}, {physical_path!r}"
            )
        texture_paths[path_key] = physical_path
        byte_length = _integer(
            texture_leaf.get("byteLength"),
            f"Road {road_id!r} texture byteLength",
            minimum=1,
        )
        source_sha256 = _sha256(
            texture_leaf.get("sha256"), f"Road {road_id!r} texture sha256"
        )
        facts = (byte_length, source_sha256)
        previous_facts = texture_facts.get(path_key)
        if previous_facts is not None and previous_facts != facts:
            raise ValueError(
                f"Road texture facts disagree for exact path {physical_path!r}"
            )
        texture_facts[path_key] = facts

        road_width = _canonical_decimal(
            width_field.get("normalizedValue"),
            f"Road {road_id!r} RoadWidth",
            maximum=MAX_WIDTH,
        )
        road_width_in_texture = _canonical_decimal(
            width_texture_field.get("normalizedValue"),
            f"Road {road_id!r} RoadWidthInTexture",
            maximum=MAX_WIDTH_IN_TEXTURE,
        )
        _validate_authored_decimal(
            width_field, road_width, f"Road {road_id!r} RoadWidth"
        )
        _validate_authored_decimal(
            width_texture_field,
            road_width_in_texture,
            f"Road {road_id!r} RoadWidthInTexture",
        )
        roads.append(
            {
                "id": road_id,
                "textureIdentifier": identifier,
                "sourceVirtualPath": physical_path,
                "sourceSha256": source_sha256,
                "sourceByteLength": byte_length,
                "RoadWidth": road_width,
                "RoadWidthInTexture": road_width_in_texture,
            }
        )

    roads.sort(key=lambda item: (str(item["id"]).casefold(), str(item["id"])))
    actual_ids = tuple(str(item["id"]) for item in roads)
    if expected is not None and actual_ids != expected:
        raise ValueError(
            "Road closure ids do not exactly match the explicitly expected ids"
        )
    return roads, aggregate


def _resource_for_texture(source_path: str) -> tuple[dict[str, Any], str]:
    stem = PurePosixPath(source_path).stem.casefold()
    if _SAFE_OUTPUT_STEM.fullmatch(stem) is None:
        raise ValueError(
            f"Road DDS basename cannot form a stable output path: {source_path!r}"
        )
    resource_id = f"fords-road-texture-{stem}"
    if SLUG_PATTERN.fullmatch(resource_id) is None:
        raise ValueError(
            f"Road DDS basename cannot form a bounded resource id: {source_path!r}"
        )
    output = _safe_path(
        f"{ROAD_TEXTURE_OUTPUT_ROOT}/{stem}.png", "Road texture output"
    )
    return (
        {
            "id": resource_id,
            "kind": "texture",
            "converter": "texture",
            "patterns": [source_path],
            "output": output,
            "limit": 1,
            "expected_count": 1,
        },
        output,
    )


def _derive_payload(
    base_profile: Mapping[str, Any],
    report: Mapping[str, Any],
    *,
    profile_id: str | None,
    pack_id: str | None,
    expected_road_ids: Iterable[str] | None,
) -> dict[str, Any]:
    roads, aggregate = _validate_report(report, expected_road_ids)
    derived = copy.deepcopy(dict(base_profile))

    if profile_id is not None:
        if not isinstance(profile_id, str) or SLUG_PATTERN.fullmatch(profile_id) is None:
            raise ValueError(f"invalid explicit profile id: {profile_id!r}")
        derived["id"] = profile_id
    if pack_id is not None:
        if not isinstance(pack_id, str) or SLUG_PATTERN.fullmatch(pack_id) is None:
            raise ValueError(f"invalid explicit pack id: {pack_id!r}")
        pack = derived.get("pack")
        if not isinstance(pack, dict):
            raise ValueError("base profile pack must be an object")
        pack["id"] = pack_id

    resources = derived.get("resources")
    if not isinstance(resources, list):
        raise ValueError("base profile resources must be an array")
    existing_ids: dict[str, str] = {}
    existing_outputs: dict[str, str] = {}
    for index, resource in enumerate(resources):
        if not isinstance(resource, dict):
            raise ValueError(f"base profile resource {index} must be an object")
        resource_id = resource.get("id")
        if not isinstance(resource_id, str):
            raise ValueError(f"base profile resource {index} has no id")
        id_key = resource_id.casefold()
        previous_id = existing_ids.get(id_key)
        if previous_id is not None and previous_id != resource_id:
            raise ValueError(
                f"case-ambiguous base resource ids: {previous_id!r}, {resource_id!r}"
            )
        existing_ids[id_key] = resource_id
        output_value = resource.get("output")
        if output_value is not None:
            output = _safe_path(output_value, f"base resource {resource_id!r} output")
            key = output.casefold()
            previous_output = existing_outputs.get(key)
            if previous_output is not None and previous_output != output:
                raise ValueError(
                    "case-ambiguous base resource outputs: "
                    f"{previous_output!r}, {output!r}"
                )
            existing_outputs[key] = output

    texture_outputs: dict[str, str] = {}
    texture_resources: list[dict[str, Any]] = []
    path_to_output: dict[str, str] = {}
    unique_sources = sorted(
        {str(road["sourceVirtualPath"]) for road in roads},
        key=lambda value: (value.casefold(), value),
    )
    for source_path in unique_sources:
        resource, output = _resource_for_texture(source_path)
        resource_id = str(resource["id"])
        if resource_id.casefold() in existing_ids:
            raise ValueError(f"Road texture resource id collision: {resource_id!r}")
        if any(
            resource_id.casefold() == str(item["id"]).casefold()
            for item in texture_resources
        ):
            raise ValueError(f"Road texture resource id collision: {resource_id!r}")
        output_key = output.casefold()
        if output_key in existing_outputs:
            raise ValueError(
                f"Road texture output collides with base profile: {output!r}"
            )
        previous_output = texture_outputs.get(output_key)
        if previous_output is not None:
            raise ValueError(
                f"Road DDS basenames collide at output {previous_output!r}"
            )
        texture_outputs[output_key] = output
        path_to_output[source_path.casefold()] = output
        texture_resources.append(resource)

    if len(resources) + len(texture_resources) > MAX_RESOURCES:
        raise ValueError(
            f"derived profile exceeds {MAX_RESOURCES} resource limit"
        )
    resources.extend(texture_resources)

    runtime_data = derived.get("runtime_data")
    if not isinstance(runtime_data, dict):
        raise ValueError("base profile runtime_data must be an object")
    runtime_keys: dict[str, str] = {}
    for raw_key in runtime_data:
        key = _safe_path(raw_key, "base runtime_data output")
        folded = key.casefold()
        previous = runtime_keys.get(folded)
        if previous is not None:
            raise ValueError(
                f"case-ambiguous base runtime_data paths: {previous!r}, {key!r}"
            )
        runtime_keys[folded] = key
    if ROAD_MATERIALS_RUNTIME_PATH.casefold() in runtime_keys:
        raise ValueError(
            f"Road materials runtime output collision: {ROAD_MATERIALS_RUNTIME_PATH!r}"
        )

    runtime_roads: list[dict[str, Any]] = []
    for road in roads:
        source_path = str(road["sourceVirtualPath"])
        runtime_road = dict(road)
        runtime_road["texturePng"] = path_to_output[source_path.casefold()]
        runtime_roads.append(runtime_road)
    runtime_data[ROAD_MATERIALS_RUNTIME_PATH] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "sourceReportAggregateSha256": aggregate,
        "roadCount": len(runtime_roads),
        "roads": runtime_roads,
    }

    map_candidates = [
        item
        for item in resources
        if isinstance(item, dict)
        and item.get("converter") == "sage-map"
        and item.get("output") == FORDS_MAP_OUTPUT
    ]
    if len(map_candidates) != 1:
        raise ValueError(
            "base profile must contain exactly one exact Fords sage-map resource"
        )
    map_resource = map_candidates[0]
    options = map_resource.get("options")
    if not isinstance(options, dict):
        raise ValueError("Fords sage-map resource options must be an object")
    metadata = options.get("metadata")
    if not isinstance(metadata, dict):
        raise ValueError("Fords sage-map resource metadata must be an object")
    existing_road_material_keys = [
        key for key in metadata if key.casefold() == "roadmaterials"
    ]
    if existing_road_material_keys:
        raise ValueError("Fords sage-map metadata already declares roadMaterials")
    _safe_path(ROAD_MATERIALS_RELATIVE_PATH, "roadMaterials metadata path")
    metadata["roadMaterials"] = ROAD_MATERIALS_RELATIVE_PATH

    encoded = json.dumps(
        derived,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    if len(encoded) > MAX_PROFILE_BYTES:
        raise ValueError(
            f"derived profile exceeds {MAX_PROFILE_BYTES} byte limit"
        )
    return derived


def build_road_profile(
    base_profile_path: Path | str,
    road_closure_report_path: Path | str,
    *,
    profile_id: str | None = None,
    pack_id: str | None = None,
    expected_road_ids: Iterable[str] | None = None,
) -> dict[str, Any]:
    """Return a complete deterministic derived ImportProfile payload.

    ``profile_id`` and ``pack_id`` are the only mechanisms that change those
    identities.  When omitted, the exact base values are retained.  Passing
    ``expected_road_ids`` makes the report's canonical Road ids an exact gate;
    no Fords-specific ids are assumed when that argument is omitted.
    """

    base_path = Path(base_profile_path).expanduser().resolve()
    # Validate the entire base with the authoritative profile parser before
    # preserving its neutral JSON payload.
    ImportProfile.load(base_path)
    base_profile = _load_json_object(
        base_path, maximum=MAX_PROFILE_BYTES, label="base ImportProfile"
    )
    report = _load_json_object(
        road_closure_report_path,
        maximum=MAX_REPORT_BYTES,
        label="Road closure report",
    )
    return _derive_payload(
        base_profile,
        report,
        profile_id=profile_id,
        pack_id=pack_id,
        expected_road_ids=expected_road_ids,
    )


__all__ = [
    "FORDS_MAP_OUTPUT",
    "ROAD_MATERIALS_RELATIVE_PATH",
    "ROAD_MATERIALS_RUNTIME_PATH",
    "ROAD_TEXTURE_OUTPUT_ROOT",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "build_road_profile",
]
