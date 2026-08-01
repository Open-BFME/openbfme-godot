"""Plan the zero-guess retail environment closure for Fords of Isen II.

The planner composes the existing bounded SAGE environment evidence into pack
resources and a runtime contract.  It deliberately does not derive a skybox
texture-set name from time-of-day: BFME II's Fords map does not author that
selection, so doing so would silently turn an executable/oracle gap into art.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .profile import ImportProfile, resolve_profile
from .retail_visual_profile import _validate_effective_manifest
from .sage_environment import SCHEMA as EVIDENCE_SCHEMA
from .sage_environment import SCHEMA_VERSION as EVIDENCE_SCHEMA_VERSION
from .sage_ini import parse_flat_named_blocks
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


PLAN_SCHEMA = "openbfme.retail-fords-environment-plan"
PLAN_SCHEMA_VERSION = 0
RUNTIME_SCHEMA = "openbfme.fords-environment"
RUNTIME_SCHEMA_VERSION = 0
RUNTIME_DATA_PATH = "maps/fords-of-isen-ii/environment.json"

ENVIRONMENT_INI_PATH = "data/ini/environment.ini"
WORLD_SKY_MODEL_PATH = "art/w3d/ne/new_skybox.w3d"
ACTIVE_CLOUD_PATH = "art/compiledtextures/ts/tscloudmed.dds"
ACTIVE_MACRO_PATH = "art/compiledtextures/ts/tsnoiseurb.dds"

_SKY_FACE_FIELDS = (
    "SkyboxTextureN",
    "SkyboxTextureE",
    "SkyboxTextureS",
    "SkyboxTextureW",
    "SkyboxTextureT",
)
_MAX_DOCUMENT_BYTES = 16 * 1024 * 1024


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


def _file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _load_json(path: Path | str, label: str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > _MAX_DOCUMENT_BYTES:
        raise ValueError(f"{label} exceeds the bounded document size")
    try:
        value = json.loads(source.read_text("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value, source


def _validate_declared_digest(document: Mapping[str, Any], label: str) -> str:
    declared = document.get("aggregateSha256")
    if not isinstance(declared, str) or len(declared) != 64:
        raise ValueError(f"{label} has no valid aggregateSha256")
    payload = dict(document)
    payload.pop("aggregateSha256", None)
    calculated = _canonical_sha256(payload)
    if declared != calculated:
        raise ValueError(f"{label} aggregateSha256 mismatch")
    return declared


def _verified_read(
    root: Path, virtual_path: str, files: Mapping[str, Mapping[str, Any]]
) -> bytes:
    record = files.get(virtual_path)
    if record is None:
        folded = [path for path in files if path.casefold() == virtual_path.casefold()]
        if folded:
            raise ValueError(
                f"effective path case mismatch: {virtual_path!r} != {folded[0]!r}"
            )
        raise ValueError(f"effective asset is missing: {virtual_path}")
    source = root.joinpath(*safe_relative_parts(virtual_path))
    payload = source.read_bytes()
    if len(payload) != record["size"]:
        raise ValueError(f"effective asset size changed: {virtual_path}")
    if hashlib.sha256(payload).hexdigest() != record["sha256"]:
        raise ValueError(f"effective asset digest changed: {virtual_path}")
    return payload


def _assignment_map(block: Mapping[str, Any], label: str) -> dict[str, Any]:
    assignments = block.get("assignments")
    if not isinstance(assignments, list):
        raise ValueError(f"{label} has invalid assignments")
    result: dict[str, Any] = {}
    for position, raw in enumerate(assignments):
        if not isinstance(raw, dict) or not isinstance(raw.get("key"), str):
            raise ValueError(f"{label} assignment {position} is invalid")
        key = raw["key"]
        folded = key.casefold()
        if folded in result:
            raise ValueError(f"{label} has duplicate assignment {key!r}")
        result[folded] = deepcopy(raw)
    return result


def _required_assignment(
    block: Mapping[str, Any], field: str, expected: object, label: str
) -> dict[str, Any]:
    row = _assignment_map(block, label).get(field.casefold())
    if row is None:
        raise ValueError(f"{label} is missing {field}")
    if row.get("value") != expected:
        raise ValueError(f"{label} {field} drifted from the BFME II 1.06 oracle")
    return row


def _resolve_texture_name(
    requested: str, files: Mapping[str, Mapping[str, Any]]
) -> dict[str, Any]:
    requested_name = PurePosixPath(requested).name.casefold()
    requested_stem = PurePosixPath(requested).stem.casefold()
    exact = sorted(
        (
            path
            for path in files
            if PurePosixPath(path).name.casefold() == requested_name
        ),
        key=str.casefold,
    )
    compiled = sorted(
        (
            path
            for path in files
            if PurePosixPath(path).suffix.casefold() == ".dds"
            and PurePosixPath(path).stem.casefold() == requested_stem
        ),
        key=str.casefold,
    )
    candidates = exact if exact else compiled
    if len(candidates) == 1:
        status = "one-exact-filename-candidate" if exact else "one-compiled-dds-stem-counterpart"
        selected = candidates[0]
    elif candidates:
        status = "ambiguous-effective-tree-candidates"
        selected = None
    else:
        status = "unresolved-in-effective-tree"
        selected = None
    return {
        "candidates": [
            {
                "path": path,
                "sha256": files[path]["sha256"],
                "size": files[path]["size"],
            }
            for path in candidates
        ],
        "requestedName": requested,
        "selectedVirtualPath": selected,
        "status": status,
    }


def _skybox_sets(
    payload: bytes, files: Mapping[str, Mapping[str, Any]]
) -> list[dict[str, Any]]:
    blocks = parse_flat_named_blocks(payload, "SkyboxTextureSet")
    if not blocks:
        raise ValueError("environment.ini has no SkyboxTextureSet declarations")
    seen: set[str] = set()
    result: list[dict[str, Any]] = []
    for block in blocks:
        folded_name = block.name.casefold()
        if folded_name in seen:
            raise ValueError(f"duplicate SkyboxTextureSet {block.name!r}")
        seen.add(folded_name)
        assignments: dict[str, str] = {}
        for field, value in block.assignments:
            if field.casefold() not in {item.casefold() for item in _SKY_FACE_FIELDS}:
                raise ValueError(
                    f"SkyboxTextureSet {block.name!r} has unsupported field {field!r}"
                )
            canonical = next(
                item for item in _SKY_FACE_FIELDS if item.casefold() == field.casefold()
            )
            if canonical in assignments:
                raise ValueError(
                    f"SkyboxTextureSet {block.name!r} duplicates {canonical}"
                )
            assignments[canonical] = value.strip()
        if set(assignments) != set(_SKY_FACE_FIELDS):
            raise ValueError(
                f"SkyboxTextureSet {block.name!r} does not define exactly five faces"
            )
        faces = [
            {
                "face": field.removeprefix("SkyboxTexture"),
                **_resolve_texture_name(assignments[field], files),
            }
            for field in _SKY_FACE_FIELDS
        ]
        result.append(
            {
                "complete": all(face["selectedVirtualPath"] is not None for face in faces),
                "faces": faces,
                "name": block.name,
            }
        )
    return result


def _validate_report_sources(
    report: Mapping[str, Any],
    root: Path,
    files: Mapping[str, Mapping[str, Any]],
) -> list[dict[str, Any]]:
    provenance = report.get("sourceProvenance")
    if not isinstance(provenance, dict) or not isinstance(provenance.get("files"), list):
        raise ValueError("environment evidence has invalid source provenance")
    rows: list[dict[str, Any]] = []
    for raw in provenance["files"]:
        if not isinstance(raw, dict):
            raise ValueError("environment evidence source row is invalid")
        path = raw.get("virtualPath")
        if not isinstance(path, str) or path not in files:
            raise ValueError("environment evidence source is absent from manifest")
        record = files[path]
        if raw.get("byteCount") != record["size"] or raw.get("sha256") != record["sha256"]:
            raise ValueError(f"environment evidence source drifted: {path}")
        _verified_read(root, path, files)
        rows.append(deepcopy(raw))
    return rows


def _environment_runtime(
    report: Mapping[str, Any],
    sky_sets: Sequence[Mapping[str, Any]],
    model_metadata: Mapping[str, Any],
) -> dict[str, Any]:
    time_weather = report.get("timeAndWeather")
    fog = report.get("fog")
    lighting = report.get("lightingAndShadows")
    sky = report.get("skyAndClouds")
    if not all(isinstance(value, dict) for value in (time_weather, fog, lighting, sky)):
        raise ValueError("environment evidence is missing required sections")

    active_time = time_weather["activeTimeOfDay"]
    if active_time != {"name": "AFTERNOON", "wireValue": 2}:
        raise ValueError("Fords active time-of-day drifted from AFTERNOON")
    map_weather = time_weather.get("mapWeather")
    if map_weather != {"name": "NORMAL", "wireValue": 0}:
        raise ValueError("Fords map weather drifted from NORMAL")

    map_fog = fog.get("mapLocal")
    if not isinstance(map_fog, dict):
        raise ValueError("Fords map-local fog evidence is missing")
    fog_rows = {
        field: _required_assignment(map_fog, field, expected, "Fords map fog")
        for field, expected in (
            ("HardwareFogColor", {"B": 235.0, "G": 226.0, "R": 220.0}),
            ("HardwareFogEnable", True),
            ("HardwareFogStart", 350),
            ("HardwareFogEnd", 2000),
        )
    }

    game_defaults = lighting.get("globalGameDataDefaults")
    if not isinstance(game_defaults, dict):
        raise ValueError("global GameData environment evidence is missing")
    game_rows = {
        field: _required_assignment(game_defaults, field, expected, "global GameData")
        for field, expected in (
            ("DrawSkyBox", True),
            ("UseCloudMap", True),
            ("UseCloudPlane", True),
            ("UseShadowVolumes", True),
            ("UseShadowDecals", True),
            ("UseShadowMapping", False),
        )
    }

    global_lighting = lighting.get("mapGlobalLighting")
    if not isinstance(global_lighting, dict):
        raise ValueError("map GlobalLighting evidence is missing")
    configurations = global_lighting.get("configurations")
    if not isinstance(configurations, dict) or not isinstance(
        configurations.get("AFTERNOON"), dict
    ):
        raise ValueError("active AFTERNOON light configuration is missing")
    active_lights = deepcopy(configurations["AFTERNOON"])
    expected_names = {
        "terrainSun",
        "objectSun",
        "infantrySun",
        "terrainAccent1",
        "objectAccent1",
        "infantryAccent1",
        "terrainAccent2",
        "objectAccent2",
        "infantryAccent2",
    }
    if set(active_lights) != expected_names:
        raise ValueError("active light domains drifted from the nine-row SAGE contract")

    shadow_color = global_lighting.get("shadowColor")
    if shadow_color != {"a": 64, "b": 0, "g": 0, "packedArgb": 1073741824, "r": 0}:
        raise ValueError("Fords shadow color drifted from the retail map")

    environment_data = sky.get("mapEnvironmentData")
    if not isinstance(environment_data, dict):
        raise ValueError("map EnvironmentData evidence is missing")
    if environment_data.get("cloudTexture") != "TSCloudMed.tga":
        raise ValueError("active cloud texture drifted")
    if environment_data.get("macroTexture") != "TSNoiseUrb.tga":
        raise ValueError("active macro texture drifted")

    blockers = [
        {
            "id": "world-sky-texture-set-selection",
            "reason": "Fords authors no SkyboxTextureSet override and the executable selection rule is not present in the retail map or INIs",
            "requiredProof": "retail executable trace or independent original-render oracle identifying the named five-face set",
        },
        {
            "id": "sage-to-godot-light-basis",
            "reason": "the nine source light vectors are exact but their Godot basis transform is not encoded in the retail data",
            "requiredProof": "render-oracle validation of the coordinate transform",
        },
        {
            "id": "linear-fog-renderer-equivalence",
            "reason": "SAGE authors linear start/end fog while Godot's built-in environment fog is exponential/height based",
            "requiredProof": "a linear depth-fog implementation validated against the retail render",
        },
        {
            "id": "shadow-volume-decal-renderer-equivalence",
            "reason": "SAGE enables volumes and decals and disables shadow mapping; Godot's directional shadows use shadow maps",
            "requiredProof": "a source-faithful shadow implementation or an approved render-equivalent mapping",
        },
    ]
    return {
        "activeTimeOfDay": deepcopy(active_time),
        "blockers": blockers,
        "cloudAndMacro": {
            "cloudMapEnabled": game_rows["UseCloudMap"]["value"],
            "cloudPackPath": "assets/textures/environment/fords/tscloudmed.png",
            "cloudPlaneEnabled": game_rows["UseCloudPlane"]["value"],
            "cloudSource": ACTIVE_CLOUD_PATH,
            "environmentData": deepcopy(environment_data),
            "macroPackPath": "assets/textures/environment/fords/tsnoiseurb.png",
            "macroSource": ACTIVE_MACRO_PATH,
        },
        "complete": False,
        "fog": {
            "colorRgb8": [220, 226, 235],
            "enabled": fog_rows["HardwareFogEnable"]["value"],
            "end": fog_rows["HardwareFogEnd"]["value"],
            "mode": "linear",
            "rendererBinding": None,
            "source": {
                "assignments": [deepcopy(fog_rows[field]) for field in (
                    "HardwareFogColor", "HardwareFogEnable", "HardwareFogStart", "HardwareFogEnd"
                )],
                "path": map_fog.get("virtualPath"),
            },
            "start": fog_rows["HardwareFogStart"]["value"],
        },
        "lighting": {
            "activeSourceRows": active_lights,
            "godotBasisTransform": None,
            "sourceChunk": deepcopy(global_lighting.get("chunk")),
            "sourceCoordinateSystem": "SAGE-map-binary",
        },
        "mapId": "bfme2.map.fords-of-isen-ii",
        "mapWeather": deepcopy(map_weather),
        "parityReady": False,
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "shadows": {
            "godotRendererBinding": None,
            "shadowColor": deepcopy(shadow_color),
            "useShadowDecals": game_rows["UseShadowDecals"]["value"],
            "useShadowMapping": game_rows["UseShadowMapping"]["value"],
            "useShadowVolumes": game_rows["UseShadowVolumes"]["value"],
        },
        "worldSky": {
            "drawSkyBox": game_rows["DrawSkyBox"]["value"],
            "model": deepcopy(dict(model_metadata)),
            "namedTextureSets": deepcopy(list(sky_sets)),
            "selectedTextureSet": None,
            "selectionStatus": "unresolved-no-map-override-or-executable-rule",
        },
    }


def _profile_resources() -> list[dict[str, Any]]:
    return [
        {
            "converter": "copy",
            "expected_count": 1,
            "id": "fords-world-sky-source-model",
            "kind": "model",
            "limit": 1,
            "options": {"sourceOnlyUntilTextureSetSelectionIsProven": True},
            "output": "sources/environment/fords/new_skybox.w3d",
            "patterns": [WORLD_SKY_MODEL_PATH],
        },
        {
            "converter": "texture",
            "expected_count": 1,
            "id": "fords-active-cloud-texture",
            "kind": "texture",
            "limit": 1,
            "output": "assets/textures/environment/fords/tscloudmed.png",
            "patterns": [ACTIVE_CLOUD_PATH],
        },
        {
            "converter": "texture",
            "expected_count": 1,
            "id": "fords-active-macro-texture",
            "kind": "texture",
            "limit": 1,
            "output": "assets/textures/environment/fords/tsnoiseurb.png",
            "patterns": [ACTIVE_MACRO_PATH],
        },
    ]


def _validate_catalog(
    resources: list[dict[str, Any]], catalog_path: Path | str
) -> dict[str, Any]:
    document = {
        "format": 1,
        "id": "retail-fords-environment-fragment",
        "title": "Retail Fords exact environment fragment",
        "pack": {"id": "bfme2-men-vslice", "version": "1.06-v0"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-fords-environment-") as raw:
        path = Path(raw) / "profile.json"
        path.write_bytes(_canonical_bytes(document))
        profile = ImportProfile.load(path)
    resolved = resolve_profile(profile, InstallCatalog.load(catalog_path))
    if resolved.missing_required:
        raise ValueError(
            "environment fragment has missing catalog resources: "
            + ", ".join(resolved.missing_required)
        )
    return {
        "missingRequired": [],
        "resourceCount": len(resolved.resources),
        "selectedEntryCount": len(resolved.selected_entries),
    }


def compose_fords_environment_plan(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    environment_report_path: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve()
    manifest, manifest_source = _load_json(manifest_path, "effective-assets manifest")
    files, manifest_evidence = _validate_effective_manifest(manifest)
    report, report_source = _load_json(environment_report_path, "environment evidence")
    if report.get("schema") != EVIDENCE_SCHEMA or report.get("schemaVersion") != EVIDENCE_SCHEMA_VERSION:
        raise ValueError("unsupported environment evidence schema")
    report_aggregate = _validate_declared_digest(report, "environment evidence")
    source_rows = _validate_report_sources(report, root, files)

    environment_ini = _verified_read(root, ENVIRONMENT_INI_PATH, files)
    model_payload = _verified_read(root, WORLD_SKY_MODEL_PATH, files)
    _verified_read(root, ACTIVE_CLOUD_PATH, files)
    _verified_read(root, ACTIVE_MACRO_PATH, files)
    sky_sets = _skybox_sets(environment_ini, files)

    metadata = scan_w3d_metadata(model_payload, WORLD_SKY_MODEL_PATH)
    texture_references = [item.identifier for item in metadata.texture_references]
    if [item.casefold() for item in texture_references] != [
        "skybox_01.tga", "skybox_04.tga", "skybox_03.tga", "skybox_02.tga"
    ]:
        raise ValueError("new_skybox.w3d placeholder texture contract drifted")
    model_metadata = {
        "animationIds": list(metadata.animation_ids),
        "hierarchyIds": list(metadata.hierarchy_ids),
        "modelIds": list(metadata.model_ids),
        "sourceOnlyPackPath": "sources/environment/fords/new_skybox.w3d",
        "sourceVirtualPath": WORLD_SKY_MODEL_PATH,
        "textureReferences": texture_references,
    }
    runtime = _environment_runtime(report, sky_sets, model_metadata)
    resources = _profile_resources()
    resolution = _validate_catalog(resources, catalog_path)

    complete_sets = sum(bool(value["complete"]) for value in sky_sets)
    resolved_faces = sum(
        face["selectedVirtualPath"] is not None
        for value in sky_sets
        for face in value["faces"]
    )
    unique_resolved_faces = {
        face["selectedVirtualPath"]
        for value in sky_sets
        for face in value["faces"]
        if face["selectedVirtualPath"] is not None
    }
    plan: dict[str, Any] = {
        "profileFragment": {
            "resources": resources,
            "runtimeDataMerge": {RUNTIME_DATA_PATH: runtime},
        },
        "runtimeEnvironment": runtime,
        "schema": PLAN_SCHEMA,
        "schemaVersion": PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "catalog": {
                "path": Path(catalog_path).name,
                "sha256": _file_sha256(Path(catalog_path).expanduser().resolve()),
            },
            "effectiveAssets": manifest_evidence,
            "environmentIni": {
                "path": ENVIRONMENT_INI_PATH,
                "sha256": files[ENVIRONMENT_INI_PATH]["sha256"],
                "size": files[ENVIRONMENT_INI_PATH]["size"],
            },
            "environmentReport": {
                "aggregateSha256": report_aggregate,
                "path": report_source.name,
                "sha256": _file_sha256(report_source),
            },
            "manifestSha256": _file_sha256(manifest_source),
            "reportRetailSources": source_rows,
        },
        "summary": {
            "blockerCount": len(runtime["blockers"]),
            "catalogSelectedEntryCount": resolution["selectedEntryCount"],
            "completeSkyboxTextureSetCount": complete_sets,
            "profileResourceCount": len(resources),
            "resolvedSkyboxFaceAssignmentCount": resolved_faces,
            "skyboxTextureSetCount": len(sky_sets),
            "uniqueResolvedSkyboxFaceFileCount": len(unique_resolved_faces),
            "worldSkySelectionResolved": False,
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets-root", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--environment-report", required=True)
    parser.add_argument("--output", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    plan = compose_fords_environment_plan(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        environment_report_path=args.environment_report,
    )
    write_json_atomic(Path(args.output), plan)
    print(
        json.dumps(
            {
                "aggregateSha256": plan["aggregateSha256"],
                "output": str(Path(args.output).resolve()),
                "summary": plan["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
