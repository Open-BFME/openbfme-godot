"""Attest the zero-guess Fords of Isen II water-reflection contract.

This module deliberately stops at evidence.  It proves the authored reflection
plane, the four standing-water polygons, the placed reflection skydome, and the
skydome's exact W3D/texture closure.  It does not choose a Godot planar-
reflection technique or treat the reflection skydome as the world sky.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import struct
from typing import Any, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .retail_visual_profile import _validate_effective_manifest
from .sage_map import parse_sage_map_bytes
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


SCHEMA = "openbfme.retail-fords-water-reflection-oracle"
SCHEMA_VERSION = 0

FORDS_MAP_PATH = "maps/map mp fords of isen ii/map mp fords of isen ii.map"
FORDS_MAP_INI_PATH = "maps/map mp fords of isen ii/map.ini"
GLOBAL_WATER_INI_PATH = "data/ini/water.ini"
NATURE_PROP_INI_PATH = "data/ini/object/nature/natureprop.ini"
REFLECTION_MODEL_PATH = "art/w3d/wt/wtrsky_grohan.w3d"
REFLECTION_TEXTURE_PATH = (
    "art/compiledtextures/wt/wtrskydome_gapofrohan.dds"
)
WORLD_SKY_MODEL_PATH = "art/w3d/ne/new_skybox.w3d"

REFLECTION_OBJECT = "WaterReflectionSkydome_GapOfRohan"
REFLECTION_MODEL_ID = "WtrSky_GRohan"
REFLECTION_TEXTURE_ID = "WtrSkydome_GapOfRohan.tga"

_MAX_JSON_BYTES = 32 * 1024 * 1024
_MAX_TEXT_BYTES = 8 * 1024 * 1024
_ASSIGNMENT_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _file_evidence(path: Path, *, role: str) -> dict[str, Any]:
    payload = path.read_bytes()
    return {
        "byteCount": len(payload),
        "role": role,
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def _load_json(path: Path | str, label: str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"missing {label}: {source}")
    if source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{label} exceeds bounded size")
    try:
        value = json.loads(source.read_text("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value, source


def _validate_declared_digest(document: Mapping[str, Any], label: str) -> str:
    declared = document.get("aggregateSha256")
    if not isinstance(declared, str) or re.fullmatch(r"[0-9a-f]{64}", declared) is None:
        raise ValueError(f"{label} has no valid aggregateSha256")
    payload = dict(document)
    payload.pop("aggregateSha256", None)
    actual = _canonical_sha256(payload)
    if declared != actual:
        raise ValueError(f"{label} aggregateSha256 mismatch")
    return declared


def _verified_asset(
    *,
    root: Path,
    virtual_path: str,
    files: Mapping[str, Mapping[str, Any]],
    catalog: InstallCatalog,
) -> tuple[bytes, dict[str, Any]]:
    record = files.get(virtual_path)
    if record is None:
        folded = [path for path in files if path.casefold() == virtual_path.casefold()]
        if folded:
            raise ValueError(
                f"effective path case mismatch: {virtual_path!r} != {folded[0]!r}"
            )
        raise ValueError(f"effective asset is missing: {virtual_path}")
    physical = root.joinpath(*safe_relative_parts(virtual_path))
    payload = physical.read_bytes()
    if len(payload) != record["size"]:
        raise ValueError(f"effective asset size changed: {virtual_path}")
    digest = hashlib.sha256(payload).hexdigest()
    if digest != record["sha256"]:
        raise ValueError(f"effective asset digest changed: {virtual_path}")
    winner = catalog.resolve_exact(virtual_path)
    if winner is None:
        raise ValueError(f"catalog winner is missing: {virtual_path}")
    expected_catalog = {
        "archive": record["archive"],
        "offset": record["offset"],
        "precedence": record["precedence"],
        "size": record["size"],
    }
    actual_catalog = {
        "archive": winner.archive,
        "offset": winner.offset,
        "precedence": winner.precedence,
        "size": winner.size,
    }
    if actual_catalog != expected_catalog:
        raise ValueError(f"catalog/manifest winner mismatch: {virtual_path}")
    return payload, {
        "archive": winner.archive,
        "byteCount": len(payload),
        "offset": winner.offset,
        "precedence": winner.precedence,
        "sha256": digest,
        "virtualPath": virtual_path,
    }


def _decode_text(payload: bytes, label: str) -> str:
    if len(payload) > _MAX_TEXT_BYTES:
        raise ValueError(f"{label} exceeds bounded size")
    return payload.decode("cp1252")


def _strip_comment(line: str) -> str:
    positions = [
        position
        for position in (line.find(";"), line.find("//"))
        if position >= 0
    ]
    return line[: min(positions)] if positions else line


def _named_section(text: str, name: str) -> dict[str, Any]:
    lines = text.splitlines()
    starts = [
        index
        for index, raw in enumerate(lines)
        if _strip_comment(raw).strip().casefold() == name.casefold()
    ]
    if len(starts) != 1:
        raise ValueError(f"expected exactly one {name} section, got {len(starts)}")
    start = starts[0]
    stop = next(
        (
            index
            for index in range(start + 1, len(lines))
            if _strip_comment(lines[index]).strip().casefold() == "end"
        ),
        None,
    )
    if stop is None:
        raise ValueError(f"unterminated {name} section")
    assignments: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index in range(start + 1, stop):
        clean = _strip_comment(lines[index]).strip()
        if not clean:
            continue
        match = _ASSIGNMENT_RE.fullmatch(clean)
        if match is None:
            raise ValueError(f"unsupported {name} row at line {index + 1}")
        key = match.group(1)
        if key.casefold() in seen:
            raise ValueError(f"duplicate {name} assignment: {key}")
        seen.add(key.casefold())
        assignments.append(
            {"key": key, "line": index + 1, "rawValue": match.group(2).strip()}
        )
    return {
        "assignments": assignments,
        "endLine": stop + 1,
        "headerLine": start + 1,
        "section": name,
    }


def _reflection_values(section: Mapping[str, Any], label: str) -> dict[str, Any]:
    rows = {
        str(row["key"]).casefold(): row
        for row in section.get("assignments", [])
        if isinstance(row, dict) and isinstance(row.get("key"), str)
    }
    plane = rows.get("reflectionplanez")
    enabled = rows.get("reflectionon")
    if plane is None or enabled is None:
        raise ValueError(f"{label} is missing ReflectionPlaneZ/ReflectionOn")
    try:
        plane_value = float(str(plane["rawValue"]))
    except ValueError as exc:
        raise ValueError(f"{label} ReflectionPlaneZ is not numeric") from exc
    on_folded = str(enabled["rawValue"]).casefold()
    if on_folded not in {"yes", "no"}:
        raise ValueError(f"{label} ReflectionOn is not Yes/No")
    return {
        "reflectionOn": on_folded == "yes",
        "reflectionOnLine": enabled["line"],
        "reflectionPlaneZ": plane_value,
        "reflectionPlaneZLine": plane["line"],
    }


def _object_declaration(text: str, name: str) -> dict[str, Any]:
    lines = text.splitlines()
    object_re = re.compile(r"^\s*Object\s+([^\s;]+)\s*$", re.IGNORECASE)
    declarations = [
        (index, match.group(1))
        for index, raw in enumerate(lines)
        if (match := object_re.fullmatch(_strip_comment(raw))) is not None
    ]
    matches = [item for item in declarations if item[1].casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one object {name!r}, got {len(matches)}")
    start, authored_name = matches[0]
    later = [index for index, _authored in declarations if index > start]
    stop = min(later) if later else len(lines)
    rows: dict[str, list[dict[str, Any]]] = {}
    for index in range(start + 1, stop):
        clean = _strip_comment(lines[index]).strip()
        match = _ASSIGNMENT_RE.fullmatch(clean)
        if match is None:
            continue
        rows.setdefault(match.group(1).casefold(), []).append(
            {
                "key": match.group(1),
                "line": index + 1,
                "rawValue": match.group(2).strip(),
            }
        )
    required = {"model", "editorsorting", "browser", "kindof"}
    if not required.issubset(rows) or any(len(rows[key]) != 1 for key in required):
        raise ValueError(f"reflection object {name!r} has an ambiguous declaration")
    model = rows["model"][0]
    kind_of = rows["kindof"][0]
    return {
        "browser": rows["browser"][0]["rawValue"],
        "editorSorting": rows["editorsorting"][0]["rawValue"],
        "kindOf": str(kind_of["rawValue"]).split(),
        "kindOfLine": kind_of["line"],
        "line": start + 1,
        "model": model["rawValue"],
        "modelLine": model["line"],
        "name": authored_name,
    }


def _dds_header(payload: bytes) -> dict[str, Any]:
    if len(payload) < 128 or payload[:4] != b"DDS ":
        raise ValueError("reflection texture is not a bounded DDS")
    if struct.unpack_from("<I", payload, 4)[0] != 124:
        raise ValueError("reflection DDS has an unsupported header size")
    height, width = struct.unpack_from("<II", payload, 12)
    mip_count = struct.unpack_from("<I", payload, 28)[0]
    pixel_flags = struct.unpack_from("<I", payload, 80)[0]
    fourcc = payload[84:88]
    if pixel_flags & 0x4 == 0:
        raise ValueError("reflection DDS does not declare a FourCC")
    try:
        fourcc_text = fourcc.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("reflection DDS has a non-ASCII FourCC") from exc
    return {
        "compressionFourCC": fourcc_text,
        "height": height,
        "mipCount": mip_count,
        "width": width,
    }


def _single(rows: Sequence[Mapping[str, Any]], label: str) -> dict[str, Any]:
    if len(rows) != 1:
        raise ValueError(f"expected exactly one {label}, got {len(rows)}")
    return deepcopy(dict(rows[0]))


def _report_evidence(document: Mapping[str, Any], path: Path, role: str) -> dict[str, Any]:
    declared = _validate_declared_digest(document, role)
    evidence = _file_evidence(path, role=role)
    evidence["aggregateSha256"] = declared
    evidence["schema"] = document.get("schema")
    evidence["schemaVersion"] = document.get("schemaVersion")
    return evidence


def compose_fords_water_reflection_oracle(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    environment_report_path: Path | str,
    visual_closure_path: Path | str,
    static_prop_plan_path: Path | str,
    cooked_map_directory: Path | str,
) -> dict[str, Any]:
    """Compose a deterministic, fail-closed reflection evidence contract."""

    root = Path(effective_assets_root).expanduser().resolve()
    manifest, manifest_source = _load_json(manifest_path, "effective manifest")
    files, manifest_identity = _validate_effective_manifest(manifest)
    catalog_source = Path(catalog_path).expanduser().resolve()
    catalog = InstallCatalog.load(catalog_source)

    required_paths = (
        FORDS_MAP_PATH,
        FORDS_MAP_INI_PATH,
        GLOBAL_WATER_INI_PATH,
        NATURE_PROP_INI_PATH,
        REFLECTION_MODEL_PATH,
        REFLECTION_TEXTURE_PATH,
        WORLD_SKY_MODEL_PATH,
    )
    payloads: dict[str, bytes] = {}
    retail_sources: list[dict[str, Any]] = []
    for virtual_path in required_paths:
        payload, evidence = _verified_asset(
            root=root,
            virtual_path=virtual_path,
            files=files,
            catalog=catalog,
        )
        payloads[virtual_path] = payload
        retail_sources.append(evidence)

    parsed = parse_sage_map_bytes(payloads[FORDS_MAP_PATH], profile="multiplayer")
    if parsed.source_sha256 != files[FORDS_MAP_PATH]["sha256"]:
        raise ValueError("strict map parser source digest mismatch")
    if len(parsed.standing_water) != 4:
        raise ValueError("Fords no longer has exactly four StandingWaterAreas")

    map_ini = _named_section(
        _decode_text(payloads[FORDS_MAP_INI_PATH], "Fords map.ini"),
        "WaterTransparency",
    )
    global_water = _named_section(
        _decode_text(payloads[GLOBAL_WATER_INI_PATH], "global water.ini"),
        "WaterTransparency",
    )
    map_reflection = _reflection_values(map_ini, "Fords WaterTransparency")
    global_reflection = _reflection_values(global_water, "global WaterTransparency")
    if map_reflection != {
        "reflectionOn": True,
        "reflectionOnLine": 18,
        "reflectionPlaneZ": 294.0,
        "reflectionPlaneZLine": 17,
    }:
        raise ValueError("Fords map-local reflection override drifted")
    if global_reflection != {
        "reflectionOn": False,
        "reflectionOnLine": 76,
        "reflectionPlaneZ": 59.0,
        "reflectionPlaneZLine": 75,
    }:
        raise ValueError("global reflection defaults drifted")

    declaration = _object_declaration(
        _decode_text(payloads[NATURE_PROP_INI_PATH], "natureprop.ini"),
        REFLECTION_OBJECT,
    )
    if declaration["model"] != REFLECTION_MODEL_ID:
        raise ValueError("reflection object model drifted")
    if declaration["editorSorting"] != "SYSTEM" or declaration["browser"] != "SKYBOXES":
        raise ValueError("reflection object editor classification drifted")
    if declaration["kindOf"] != ["SKYBOX", "INERT", "CAN_CAST_REFLECTIONS"]:
        raise ValueError("reflection object KindOf flags drifted")

    source_placements = [
        item for item in parsed.objects if item.get("typeName") == REFLECTION_OBJECT
    ]
    source_placement = _single(source_placements, "reflection skydome placement")

    cooked_root = Path(cooked_map_directory).expanduser().resolve()
    cooked_objects, cooked_objects_path = _load_json(
        cooked_root / "objects.json", "strict cooked objects"
    )
    cooked_water, cooked_water_path = _load_json(
        cooked_root / "water.json", "strict cooked water"
    )
    if cooked_objects.get("schema") != "openbfme.sage-map-objects" or cooked_objects.get(
        "schemaVersion"
    ) != 0:
        raise ValueError("unsupported strict cooked objects contract")
    if cooked_water.get("schema") != "openbfme.sage-water" or cooked_water.get(
        "schemaVersion"
    ) != 0:
        raise ValueError("unsupported strict cooked water contract")
    if cooked_objects.get("objects") != parsed.objects:
        raise ValueError("strict cooked objects do not match the current retail map")
    if cooked_water.get("standingAreas") != parsed.standing_water:
        raise ValueError("strict cooked water does not match the current retail map")
    cooked_placement = _single(
        [
            item
            for item in cooked_objects["objects"]
            if item.get("typeName") == REFLECTION_OBJECT
        ],
        "strict cooked reflection skydome placement",
    )
    if cooked_placement != source_placement:
        raise ValueError("reflection skydome placement changed during strict cooking")

    environment, environment_path = _load_json(
        environment_report_path, "Fords environment evidence"
    )
    if environment.get("schema") != "openbfme.sage-fords-environment-evidence" or environment.get(
        "schemaVersion"
    ) != 0:
        raise ValueError("unsupported Fords environment evidence")
    if environment.get("mapId") != "fords-of-isen-ii":
        raise ValueError("environment evidence map identity drifted")
    environment_water = environment.get("water")
    if not isinstance(environment_water, dict):
        raise ValueError("environment evidence has no water section")
    overlay = environment_water.get("transparencyOverlayEvidence")
    if not isinstance(overlay, dict):
        raise ValueError("environment evidence has no water overlay")
    effective_rows = {
        str(row.get("key")): row
        for row in overlay.get("effectiveAssignments", [])
        if isinstance(row, dict)
    }
    if effective_rows.get("ReflectionOn", {}).get("value") is not True:
        raise ValueError("environment evidence does not attest ReflectionOn=Yes")
    if effective_rows.get("ReflectionPlaneZ", {}).get("value") != "294.0":
        raise ValueError("environment evidence does not attest ReflectionPlaneZ=294.0")
    material_rows = environment_water.get("mapMaterialRows")
    if not isinstance(material_rows, dict) or material_rows.get(
        "standingWaterAreaCount"
    ) != 4:
        raise ValueError("environment evidence standing-water census drifted")
    material_areas = material_rows.get("standingWaterMaterials")
    if not isinstance(material_areas, list) or len(material_areas) != 4:
        raise ValueError("environment evidence standing-water materials drifted")

    visual, visual_path = _load_json(visual_closure_path, "retail visual closure")
    if visual.get("schema") != "openbfme.retail-visual-closure" or visual.get(
        "schemaVersion"
    ) != 1:
        raise ValueError("unsupported retail visual closure")
    visual_target = _single(
        [row for row in visual.get("targets", []) if row.get("name") == REFLECTION_OBJECT],
        "visual-closure reflection target",
    )
    if visual_target.get("status") != "resolved":
        raise ValueError("reflection target is not resolved")
    visual_leaf = _single(
        [
            row
            for row in visual.get("exactLeaves", [])
            if row.get("targetObject") == REFLECTION_OBJECT
            and row.get("identifier") == REFLECTION_MODEL_ID
        ],
        "visual-closure reflection model leaf",
    )
    if visual_leaf.get("physicalVirtualPaths") != [REFLECTION_MODEL_PATH]:
        raise ValueError("reflection model leaf resolution drifted")
    embedded = _single(
        [
            row
            for row in visual.get("w3dDependencyClosure", {}).get(
                "embeddedTextures", []
            )
            if row.get("sourceW3dVirtualPath") == REFLECTION_MODEL_PATH
            and row.get("identifier") == REFLECTION_TEXTURE_ID
        ],
        "reflection W3D embedded texture",
    )
    if embedded.get("physicalVirtualPaths") != [REFLECTION_TEXTURE_PATH]:
        raise ValueError("reflection texture representation bridge drifted")

    static_plan, static_path = _load_json(
        static_prop_plan_path, "retail static-prop plan"
    )
    if static_plan.get("schema") != "openbfme.retail-static-prop-plan" or static_plan.get(
        "schemaVersion"
    ) != 0:
        raise ValueError("unsupported retail static-prop plan")
    conversion_group = _single(
        [
            row
            for row in static_plan.get("conversionGroups", [])
            if row.get("modelSource", {}).get("virtualPath") == REFLECTION_MODEL_PATH
        ],
        "reflection static conversion group",
    )
    if conversion_group.get("targetObjects") != [REFLECTION_OBJECT]:
        raise ValueError("reflection static conversion target drifted")
    texture_sources = conversion_group.get("textureSources")
    if not isinstance(texture_sources, list) or [
        row.get("virtualPath") for row in texture_sources
    ] != [REFLECTION_TEXTURE_PATH]:
        raise ValueError("reflection static conversion texture drifted")
    resources = static_plan.get("profileFragment", {}).get("resources", [])
    model_resource = _single(
        [
            row
            for row in resources
            if row.get("id") == conversion_group.get("modelResourceId")
        ],
        "reflection model resource",
    )
    texture_resource_id = model_resource.get("options", {}).get("inputResourceIds", [None])[0]
    texture_resource = _single(
        [row for row in resources if row.get("id") == texture_resource_id],
        "reflection texture resource",
    )

    metadata = scan_w3d_metadata(
        payloads[REFLECTION_MODEL_PATH], virtual_path=REFLECTION_MODEL_PATH
    )
    if metadata.warnings:
        raise ValueError("reflection W3D metadata scan produced warnings")
    mesh = _single(
        [
            {
                "attributes": row.attributes,
                "faceCount": row.face_count,
                "identifier": row.identifier,
                "materialCount": row.material_count,
                "meshName": row.mesh_name,
                "vertexCount": row.vertex_count,
                "versionMajor": row.version_major,
                "versionMinor": row.version_minor,
            }
            for row in metadata.mesh_headers
        ],
        "reflection W3D mesh header",
    )
    if mesh != {
        "attributes": 0,
        "faceCount": 98,
        "identifier": "WTRSKY_GROHAN",
        "materialCount": 1,
        "meshName": "WTRSKY_GROHAN",
        "vertexCount": 57,
        "versionMajor": 5,
        "versionMinor": 0,
    }:
        raise ValueError("reflection W3D mesh contract drifted")
    shader = _single(
        [
            {
                "technique": row.technique,
                "typeName": row.type_name,
                "version": row.version,
            }
            for row in metadata.shader_material_headers
        ],
        "reflection W3D shader header",
    )
    properties = {
        row.name: list(row.value) if isinstance(row.value, tuple) else row.value
        for row in metadata.shader_material_properties
    }
    expected_properties = {
        "AlphaBlendingEnable": False,
        "ColorEmissive": [1.0, 1.0, 1.0, 0.0],
        "DepthWriteEnable": True,
        "FogEnable": False,
        "TexCoordTransform_0": [1.0, 1.0, 0.0, 0.0],
        "Texture_0": REFLECTION_TEXTURE_ID,
    }
    if shader != {"technique": 0, "typeName": "Simple.fx", "version": 1}:
        raise ValueError("reflection W3D shader header drifted")
    if properties != expected_properties:
        raise ValueError("reflection W3D shader properties drifted")

    sky_env_candidates = sorted(
        path
        for path in files
        if Path(path).name.casefold() in {"skyenv.tga", "skyenv.dds"}
    )
    if sky_env_candidates:
        raise ValueError("SkyEnv.tga unexpectedly resolved in the effective tree")
    if any(area.get("skyTexture") != "SkyEnv.tga" for area in parsed.standing_water):
        raise ValueError("standing-water skyTexture declarations drifted")

    plane = map_reflection["reflectionPlaneZ"]
    matching_area_ids = [
        int(area["id"])
        for area in parsed.standing_water
        if float(area["waterHeight"]) == plane
    ]
    blockers = [
        {
            "id": "reflection-pass-camera-and-clip-semantics",
            "reason": "retail data enables one SAGE reflection plane but does not encode the reflection camera, clip plane, culling, or render-target rules",
            "requiredProof": "retail executable trace or original-render oracle covering the reflection pass",
        },
        {
            "id": "reflection-only-skydome-visibility-semantics",
            "reason": "SKYBOX and CAN_CAST_REFLECTIONS identify the source object, but do not prove whether or how it is excluded from the main camera in Godot",
            "requiredProof": "retail render-pass visibility evidence for the placed object",
        },
        {
            "id": "single-plane-to-four-water-area-compositing",
            "reason": "the authored plane is Z=294 while the four standing-water heights are 294, 364, 365, and 245; the retail compositing rule is not present in map data",
            "requiredProof": "retail renderer evidence showing how the single reflection result is sampled by every water area",
        },
        {
            "id": "standing-water-skyenv-input",
            "reason": "all four water areas request SkyEnv.tga, which has no exact file or compiled DDS stem counterpart in the effective BFME2 tree",
            "requiredProof": "retail runtime resolution evidence or an independently verified source for SkyEnv.tga",
        },
    ]

    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "mapId": "bfme2.map.fords-of-isen-ii",
        "sourceEvidence": {
            "catalog": _file_evidence(catalog_source, role="BFME2 install catalog"),
            "cooked": {
                "objects": _file_evidence(
                    cooked_objects_path, role="strict cooked objects"
                ),
                "water": _file_evidence(cooked_water_path, role="strict cooked water"),
            },
            "effectiveManifest": {
                **_file_evidence(
                    manifest_source, role="effective retail winner manifest"
                ),
                **manifest_identity,
            },
            "reports": [
                _report_evidence(
                    environment, environment_path, "Fords environment evidence"
                ),
                _report_evidence(visual, visual_path, "retail visual closure"),
                _report_evidence(static_plan, static_path, "retail static-prop plan"),
            ],
            "retailFiles": retail_sources,
            "strictMap": {
                "bodySha256": parsed.body_sha256,
                "envelope": deepcopy(parsed.envelope),
                "profile": deepcopy(parsed.profile),
                "sourceSha256": parsed.source_sha256,
                "status": "strict-parser-and-cooked-output-byte-exact",
            },
        },
        "reflectionSettings": {
            "effective": {
                "reflectionOn": True,
                "reflectionPlaneSageZ": plane,
                "source": FORDS_MAP_INI_PATH,
            },
            "globalOverridden": {
                "reflectionOn": global_reflection["reflectionOn"],
                "reflectionPlaneSageZ": global_reflection["reflectionPlaneZ"],
                "source": GLOBAL_WATER_INI_PATH,
            },
            "godotCoordinateHandoff": {
                "godotAxis": "Y",
                "godotPlaneValue": plane,
                "sourceAxis": "SAGE Z",
                "status": "exact-coordinate-remap-only-not-a-renderer-technique",
                "transform": "godot=(sage.x,sage.z,-sage.y)",
            },
        },
        "standingWater": {
            "areaCount": len(parsed.standing_water),
            "areas": deepcopy(parsed.standing_water),
            "authoredHeightSet": sorted(
                {int(area["waterHeight"]) for area in parsed.standing_water}
            ),
            "planeMatchingAreaIds": matching_area_ids,
            "pointCount": sum(len(area["sagePoints"]) for area in parsed.standing_water),
            "skyEnv": {
                "requestedName": "SkyEnv.tga",
                "resolvedCandidates": sky_env_candidates,
                "status": "unresolved-in-effective-tree",
            },
        },
        "reflectionSkydome": {
            "classification": "water-reflection-skydome-not-world-sky",
            "objectDeclaration": declaration,
            "placement": source_placement,
            "retailModel": {
                "mesh": mesh,
                "shader": shader,
                "shaderProperties": properties,
                "virtualPath": REFLECTION_MODEL_PATH,
            },
            "retailTexture": {
                **_dds_header(payloads[REFLECTION_TEXTURE_PATH]),
                "authoredIdentifier": REFLECTION_TEXTURE_ID,
                "representationBridge": "exact-authored-tga-stem-to-one-effective-dds",
                "virtualPath": REFLECTION_TEXTURE_PATH,
            },
            "staticConversionPlan": {
                "modelOutput": model_resource.get("output"),
                "modelResourceId": model_resource.get("id"),
                "status": "exact-native-assets-planned-render-pass-semantics-unresolved",
                "textureOutput": texture_resource.get("output"),
                "textureResourceId": texture_resource.get("id"),
            },
        },
        "worldSkyDistinction": {
            "relationshipStatus": "no-retail-evidence-that-world-sky-and-water-reflection-skydome-are-interchangeable",
            "waterReflectionModel": REFLECTION_MODEL_PATH,
            "worldSkyModel": WORLD_SKY_MODEL_PATH,
            "worldSkyModelIsDifferentRetailFile": True,
            "standingWaterSkyTextureIsDifferentInput": True,
        },
        "godotRenderer": {
            "blockers": blockers,
            "parityReady": False,
            "techniqueSelected": None,
            "zeroGuessPolicy": "do-not-show-the-reflection-skydome-as-world-sky-and-do-not-select-SSR-planar-probe-or-subviewport-without-oracle-proof",
        },
    }
    contract["summary"] = {
        "rendererBlockerCount": len(blockers),
        "reflectionEnabled": True,
        "reflectionPlaneSageZ": plane,
        "reflectionSkydomePlacementCount": 1,
        "standingWaterAreaCount": len(parsed.standing_water),
        "standingWaterPointCount": sum(
            len(area["sagePoints"]) for area in parsed.standing_water
        ),
        "staticNativeAssetClosureResolved": True,
        "worldSkySeparated": True,
    }
    contract["aggregateSha256"] = _canonical_sha256(contract)
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compose the exact Fords water-reflection evidence contract"
    )
    parser.add_argument("--effective-assets-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--environment-report", type=Path, required=True)
    parser.add_argument("--visual-closure", type=Path, required=True)
    parser.add_argument("--static-prop-plan", type=Path, required=True)
    parser.add_argument("--cooked-map-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = compose_fords_water_reflection_oracle(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        environment_report_path=args.environment_report,
        visual_closure_path=args.visual_closure,
        static_prop_plan_path=args.static_prop_plan,
        cooked_map_directory=args.cooked_map_directory,
    )
    write_json_atomic(args.output, contract)
    print(json.dumps(contract["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
