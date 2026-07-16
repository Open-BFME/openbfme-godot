"""Plan the exact retail ambient-audio closure used by Fords of Isen II.

The planner is deliberately payload-free.  It reads verified files from a
caller-provided private effective-assets tree, resolves the seven logical
ambient roots through SAGE definitions to unique media leaves, and emits only
profile rules, runtime registry data, hashes, and source parameters.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Iterable, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .profile import ImportProfile, resolve_profile
from .retail_visual_profile import _validate_effective_manifest
from .sage_ini import IniBlock, parse_flat_named_blocks
from .util import write_json_atomic


PLAN_SCHEMA = "openbfme.retail-fords-ambient-audio-plan"
PLAN_SCHEMA_VERSION = 0
REGISTRY_SCHEMA = "openbfme.audio-events"
REGISTRY_SCHEMA_VERSION = 1

ROOT_IDS = (
    "Amb_BirdsAmonHen1",
    "Amb_BirdsAmonHen2",
    "Amb_MTBirds1Loop",
    "Amb_MTBirds2Loop",
    "Amb_CritterDesert1",
    "Amb_WaterRiver1Loop",
    "AmbientAmonHenForest1Stream",
)
TYPE_EVENT_IDS = {
    "Amb_BirdsAmonHen1": "Amb_BirdsAmonHen1",
    "Amb_BirdsAmonHen2": "Amb_BirdsAmonHen2",
    "Amb_BirdsIthilien1Loop": "Amb_MTBirds1Loop",
    "Amb_BirdsIthilien2Loop": "Amb_MTBirds2Loop",
    "Amb_CritterDesert1": "Amb_CritterDesert1",
    "Amb_WaterRiver1Loop": "Amb_WaterRiver1Loop",
    "AmbStream_AmonHenForest1": "AmbientAmonHenForest1Stream",
}
EXPECTED_PLACEMENT_COUNTS = {
    "Amb_BirdsAmonHen1": 16,
    "Amb_BirdsAmonHen2": 11,
    "Amb_BirdsIthilien1Loop": 3,
    "Amb_BirdsIthilien2Loop": 3,
    "Amb_CritterDesert1": 6,
    "Amb_WaterRiver1Loop": 10,
    "AmbStream_AmonHenForest1": 1,
}
EXPECTED_EVENT_COUNT = 6
EXPECTED_STREAM_COUNT = 1
EXPECTED_MULTISOUND_COUNT = 0
EXPECTED_SAMPLE_COUNT = 57
EXPECTED_WAV_COUNT = 56
EXPECTED_MP3_COUNT = 1

_MAX_DOCUMENT_BYTES = 64 * 1024 * 1024
_MAX_INI_FILE_COUNT = 4096
_MAX_TOTAL_INI_BYTES = 256 * 1024 * 1024
_IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255}$")
_WEIGHTED_REFERENCE = re.compile(
    r"^(?P<id>[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255})(?::(?P<weight>[0-9]+))?$"
)
_DECLARATION = r"(?im)^[ \t]*{kind}[ \t]+{name}(?=[ \t;\r\n]|$)"
_DEFINE = re.compile(
    rb"(?im)^[ \t]*#define[ \t]+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    rb"[ \t]+(?P<value>[0-9]+(?:\.[0-9]+)?)[ \t]*(?:;.*)?$"
)


@dataclass(frozen=True, slots=True)
class DefinitionLocation:
    kind: str
    block: IniBlock
    path: str
    sha256: str
    line: int


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


def _identifier(value: str, label: str) -> str:
    if _IDENTIFIER.fullmatch(value) is None:
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _weighted_references(values: Iterable[str], label: str) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for value in values:
        for token in value.split():
            match = _WEIGHTED_REFERENCE.fullmatch(token)
            if match is None:
                raise ValueError(f"unsafe {label} reference: {token!r}")
            row: dict[str, Any] = {"id": match.group("id")}
            if match.group("weight") is not None:
                weight = int(match.group("weight"))
                if not 0 < weight <= 1_000_000:
                    raise ValueError(f"invalid {label} weight")
                row["weight"] = weight
            result.append(row)
    return result


def _parameter_rows(block: IniBlock, omitted: set[str]) -> list[dict[str, str]]:
    return [
        {"field": field, "value": value}
        for field, value in block.assignments
        if field.casefold() not in omitted
    ]


def _read_verified_ini_sources(
    root: Path, files: Mapping[str, Mapping[str, Any]]
) -> dict[str, bytes]:
    candidates = sorted(
        (
            path
            for path in files
            if path.casefold().startswith("data/ini/")
            and path.casefold().endswith(".ini")
        ),
        key=str.casefold,
    )
    if len(candidates) > _MAX_INI_FILE_COUNT:
        raise ValueError("effective-assets INI file count exceeds safety bound")
    result: dict[str, bytes] = {}
    total = 0
    for virtual_path in candidates:
        source = root.joinpath(*safe_relative_parts(virtual_path))
        payload = source.read_bytes()
        total += len(payload)
        if total > _MAX_TOTAL_INI_BYTES:
            raise ValueError("effective-assets INI bytes exceed safety bound")
        record = files[virtual_path]
        if len(payload) != record["size"]:
            raise ValueError(f"effective asset size changed: {virtual_path}")
        if hashlib.sha256(payload).hexdigest() != record["sha256"]:
            raise ValueError(f"effective asset digest changed: {virtual_path}")
        result[virtual_path] = payload
    return result


def _find_definition(
    identifier: str,
    ini_sources: Mapping[str, bytes],
    manifest_files: Mapping[str, Mapping[str, Any]],
) -> DefinitionLocation:
    _identifier(identifier, "audio definition")
    matches: list[DefinitionLocation] = []
    for path, payload in ini_sources.items():
        text = payload.decode("cp1252")
        for kind in ("AudioEvent", "Multisound", "AmbientStream"):
            declaration = re.compile(
                _DECLARATION.format(
                    kind=re.escape(kind), name=re.escape(identifier)
                )
            )
            found = list(declaration.finditer(text))
            if not found:
                continue
            blocks = [
                block
                for block in parse_flat_named_blocks(payload, kind)
                if block.name.casefold() == identifier.casefold()
            ]
            if len(found) != 1 or len(blocks) != 1:
                raise ValueError(
                    f"ambiguous {kind} definition for {identifier!r} in {path}"
                )
            matches.append(
                DefinitionLocation(
                    kind,
                    blocks[0],
                    path,
                    str(manifest_files[path]["sha256"]),
                    text.count("\n", 0, found[0].start()) + 1,
                )
            )
    if not matches:
        raise ValueError(f"unresolved audio definition: {identifier!r}")
    if len(matches) != 1:
        locations = ", ".join(f"{item.kind}:{item.path}" for item in matches)
        raise ValueError(f"ambiguous audio definition {identifier!r}: {locations}")
    return matches[0]


def _macro_values(ini_sources: Mapping[str, bytes]) -> dict[str, str]:
    values: dict[str, str] = {}
    for payload in ini_sources.values():
        for match in _DEFINE.finditer(payload):
            name = match.group("name").decode("ascii")
            value = match.group("value").decode("ascii")
            previous = values.get(name.casefold())
            if previous is not None and previous != value:
                raise ValueError(f"ambiguous numeric INI macro: {name}")
            values[name.casefold()] = value
    return values


def _resolve_parameter_value(value: str, macros: Mapping[str, str]) -> str:
    stripped = value.strip()
    return macros.get(stripped.casefold(), stripped)


def _runtime_parameters(
    rows: list[dict[str, str]], macros: Mapping[str, str]
) -> list[dict[str, str]]:
    return [
        {
            "field": row["field"],
            "value": (
                _resolve_parameter_value(row["value"], macros)
                if row["field"].casefold() in {"minrange", "maxrange"}
                else row["value"]
            ),
        }
        for row in rows
    ]


def _one_parameter(rows: list[dict[str, str]], field: str) -> str | None:
    values = [row["value"] for row in rows if row["field"].casefold() == field]
    if len(values) > 1:
        raise ValueError(f"duplicate audio parameter: {field}")
    return values[0] if values else None


def _runtime_mapping(source: str) -> dict[str, str]:
    match = re.search(
        r"const\s+FORDS_AMBIENT_TYPE_EVENT_IDS\s*:\s*Dictionary\s*=\s*\{(?P<body>.*?)^\}",
        source,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise ValueError("runtime Fords ambient mapping is missing")
    pairs = re.findall(r'^\s*"([^"]+)"\s*:\s*"([^"]+)"\s*,?\s*$', match.group("body"), re.MULTILINE)
    mapping: dict[str, str] = {}
    for key, value in pairs:
        if key.casefold() in {item.casefold() for item in mapping}:
            raise ValueError("runtime Fords ambient mapping has duplicate keys")
        mapping[key] = value
    if mapping != TYPE_EVENT_IDS:
        raise ValueError("runtime Fords ambient mapping changed")
    return mapping


def _validate_map_objects(document: Mapping[str, Any]) -> dict[str, Any]:
    if document.get("schema") != "openbfme.sage-map-objects":
        raise ValueError("unsupported generated map objects contract")
    objects = document.get("objects")
    if not isinstance(objects, list) or document.get("count") != len(objects):
        raise ValueError("invalid generated map objects contract")
    selected: list[dict[str, Any]] = []
    counts: Counter[str] = Counter()
    seen_indices: set[int] = set()
    for value in objects:
        if not isinstance(value, dict):
            raise ValueError("invalid map object row")
        type_name = value.get("typeName")
        if type_name not in TYPE_EVENT_IDS:
            continue
        index = value.get("index")
        position = value.get("godotPosition")
        if (
            isinstance(index, bool)
            or not isinstance(index, int)
            or index < 0
            or index in seen_indices
            or not isinstance(position, list)
            or len(position) != 3
            or any(isinstance(item, bool) or not isinstance(item, (int, float)) for item in position)
        ):
            raise ValueError("invalid ambient placement in generated map objects")
        seen_indices.add(index)
        counts[str(type_name)] += 1
        selected.append(
            {
                "eventId": TYPE_EVENT_IDS[str(type_name)],
                "godotPosition": position,
                "index": index,
                "typeName": type_name,
            }
        )
    if dict(sorted(counts.items())) != dict(sorted(EXPECTED_PLACEMENT_COUNTS.items())):
        raise ValueError(f"Fords ambient placement closure changed: {dict(counts)}")
    selected.sort(key=lambda item: item["index"])
    return {
        "count": len(selected),
        "countsByType": dict(sorted(counts.items(), key=lambda item: item[0].casefold())),
        "indicesByType": {
            type_name: [row["index"] for row in selected if row["typeName"] == type_name]
            for type_name in sorted(TYPE_EVENT_IDS, key=str.casefold)
        },
        "placementDigest": _canonical_sha256(selected),
        "typeEventIds": dict(sorted(TYPE_EVENT_IDS.items(), key=lambda item: item[0].casefold())),
    }


def _resolve_sample_records(
    sample_ids: Iterable[str], manifest_files: Mapping[str, Mapping[str, Any]]
) -> dict[str, dict[str, Any]]:
    candidates: dict[str, list[tuple[str, Mapping[str, Any]]]] = {}
    for path, record in manifest_files.items():
        suffix = PurePosixPath(path).suffix.casefold()
        if not path.casefold().startswith("data/audio/") or suffix not in {".wav", ".mp3"}:
            continue
        candidates.setdefault(PurePosixPath(path).stem.casefold(), []).append((path, record))
    result: dict[str, dict[str, Any]] = {}
    folded: set[str] = set()
    for sample_id in sample_ids:
        _identifier(sample_id, "audio sample")
        key = sample_id.casefold()
        if key in folded:
            continue
        folded.add(key)
        matches = sorted(candidates.get(key, []), key=lambda item: item[0].casefold())
        if not matches:
            raise ValueError(f"unresolved audio sample: {sample_id!r}")
        if len(matches) != 1:
            raise ValueError(f"ambiguous audio sample: {sample_id!r}")
        path, record = matches[0]
        result[sample_id] = {
            "path": path,
            "sha256": record["sha256"],
            "size": record["size"],
        }
    return result


def _owned_source_paths(profile: Mapping[str, Any]) -> dict[str, list[str]]:
    resources = profile.get("resources")
    if not isinstance(resources, list):
        raise ValueError("current complete profile has invalid resources")
    result: dict[str, list[str]] = {}
    for value in resources:
        if not isinstance(value, dict) or not isinstance(value.get("patterns"), list):
            raise ValueError("current complete profile has invalid resource")
        resource_id = str(value.get("id", ""))
        for raw_path in value["patterns"]:
            if not isinstance(raw_path, str):
                raise ValueError("current complete profile has invalid pattern")
            path = "/".join(safe_relative_parts(raw_path))
            key = path.casefold()
            owners = result.setdefault(key, [])
            if resource_id not in owners:
                owners.append(resource_id)
                owners.sort()
    return result


def _profile_sample_paths(profile: Mapping[str, Any]) -> dict[str, str]:
    runtime = profile.get("runtime_data")
    if not isinstance(runtime, dict):
        raise ValueError("current complete profile has invalid runtime_data")
    audio = runtime.get("data/audio_events.json", {})
    if not isinstance(audio, dict) or not isinstance(audio.get("samples"), dict):
        raise ValueError("current complete profile has invalid audio registry")
    return {str(key).casefold(): str(value) for key, value in audio["samples"].items()}


def _fragment_profile(resources: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "format": 1,
        "id": "retail-fords-ambient-audio-fragment",
        "title": "Retail Fords ambient audio fragment",
        "pack": {"id": "bfme2-men-vslice", "version": "1.06-v0"},
        "resources": resources,
    }


def _validate_catalog_resolution(
    resources: list[dict[str, Any]], catalog_path: Path | str
) -> dict[str, Any]:
    # ImportProfile's parser is the contract.  Use a bounded temporary file only
    # in the caller's private scratch tree via the public validation helper below.
    catalog = InstallCatalog.load(catalog_path)
    profile_value = _fragment_profile(resources)
    import tempfile

    with tempfile.TemporaryDirectory(prefix="openbfme-fords-audio-") as temp:
        profile_path = Path(temp) / "profile.json"
        profile_path.write_bytes(_canonical_bytes(profile_value))
        parsed = ImportProfile.load(profile_path)
    resolved = resolve_profile(parsed, catalog)
    if resolved.missing_required:
        raise ValueError(
            "ambient-audio fragment has missing catalog resources: "
            + ", ".join(resolved.missing_required)
        )
    return {
        "missingRequired": [],
        "resourceCount": len(resolved.resources),
        "selectedEntryCount": len(resolved.selected_entries),
    }


def compose_fords_ambient_audio_plan(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    map_objects_path: Path | str,
    complete_profile_path: Path | str,
    runtime_audio_path: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve()
    manifest, manifest_source = _load_json(manifest_path, "effective-assets manifest")
    manifest_files, manifest_evidence = _validate_effective_manifest(manifest)
    map_objects, map_source = _load_json(map_objects_path, "generated map objects")
    complete_profile, profile_source = _load_json(complete_profile_path, "complete profile")
    runtime_source = Path(runtime_audio_path).expanduser().resolve()
    runtime_mapping = _runtime_mapping(runtime_source.read_text("utf-8"))
    placement_evidence = _validate_map_objects(map_objects)
    ini_sources = _read_verified_ini_sources(root, manifest_files)
    macros = _macro_values(ini_sources)

    selected_events: dict[str, dict[str, Any]] = {}
    selected_multisounds: dict[str, dict[str, Any]] = {}
    source_definitions: list[dict[str, Any]] = []
    sample_ids: list[str] = []
    visiting: set[str] = set()

    def visit(identifier: str, *, root_id: str) -> None:
        key = identifier.casefold()
        if key in selected_events or key in selected_multisounds:
            return
        if key in visiting:
            raise ValueError(f"Multisound dependency cycle at {identifier!r}")
        location = _find_definition(identifier, ini_sources, manifest_files)
        block = location.block
        if location.kind == "AudioEvent":
            sounds = _weighted_references(
                block.values("Sounds"), f"AudioEvent {block.name} Sounds"
            )
            if not sounds:
                raise ValueError(f"AudioEvent {block.name!r} has no Sounds")
            parameters = _parameter_rows(block, {"sounds"})
            runtime_parameters = _runtime_parameters(parameters, macros)
            envelope: list[str] = []
            for field in ("attack", "decay"):
                value = _one_parameter(parameters, field)
                if value is not None:
                    refs = _weighted_references([value], f"AudioEvent {block.name} {field}")
                    envelope.extend(str(row["id"]) for row in refs)
            body = [str(row["id"]) for row in sounds]
            sample_ids.extend(body)
            sample_ids.extend(envelope)
            selected_events[key] = {
                "id": block.name,
                "parameters": runtime_parameters,
                "sounds": sounds,
            }
            source_definitions.append(
                _source_definition_evidence(
                    location,
                    root_id=root_id,
                    body_samples=body,
                    envelope_samples=envelope,
                    source_parameters=parameters,
                    runtime_parameters=runtime_parameters,
                    macros=macros,
                )
            )
            return
        if location.kind == "AmbientStream":
            filenames = block.values("Filename")
            if len(filenames) != 1 or len(filenames[0].split()) != 1:
                raise ValueError(f"AmbientStream {block.name!r} has invalid Filename")
            filename = filenames[0].strip()
            suffix = PurePosixPath(filename).suffix.casefold()
            if suffix not in {".wav", ".mp3"}:
                raise ValueError(f"AmbientStream {block.name!r} has unsupported Filename")
            sample_id = PurePosixPath(filename).stem
            _identifier(sample_id, "AmbientStream sample")
            parameters = _parameter_rows(block, {"filename"})
            runtime_parameters = _runtime_parameters(parameters, macros)
            sample_ids.append(sample_id)
            selected_events[key] = {
                "id": block.name,
                "parameters": runtime_parameters,
                "sounds": [{"id": sample_id}],
            }
            source_definitions.append(
                _source_definition_evidence(
                    location,
                    root_id=root_id,
                    body_samples=[sample_id],
                    envelope_samples=[],
                    source_parameters=parameters,
                    runtime_parameters=runtime_parameters,
                    macros=macros,
                )
            )
            return
        if location.kind != "Multisound":
            raise AssertionError(location.kind)
        subsounds = _weighted_references(
            block.values("Subsounds"), f"Multisound {block.name} Subsounds"
        )
        if not subsounds:
            raise ValueError(f"Multisound {block.name!r} has no Subsounds")
        visiting.add(key)
        for row in subsounds:
            visit(str(row["id"]), root_id=root_id)
        visiting.remove(key)
        parameters = _parameter_rows(block, {"subsounds"})
        selected_multisounds[key] = {
            "id": block.name,
            "parameters": _runtime_parameters(parameters, macros),
            "subsounds": subsounds,
        }
        source_definitions.append(
            _source_definition_evidence(
                location,
                root_id=root_id,
                body_samples=[],
                envelope_samples=[],
                source_parameters=parameters,
                runtime_parameters=_runtime_parameters(parameters, macros),
                macros=macros,
            )
        )

    for root_id in ROOT_IDS:
        visit(root_id, root_id=root_id)

    event_rows = sorted(selected_events.values(), key=lambda item: str(item["id"]).casefold())
    multisound_rows = sorted(
        selected_multisounds.values(), key=lambda item: str(item["id"]).casefold()
    )
    source_definitions.sort(key=lambda item: str(item["id"]).casefold())
    sample_records = _resolve_sample_records(sample_ids, manifest_files)
    if (
        len(event_rows) != EXPECTED_EVENT_COUNT + EXPECTED_STREAM_COUNT
        or sum(row["sourceKind"] == "AudioEvent" for row in source_definitions)
        != EXPECTED_EVENT_COUNT
        or sum(row["sourceKind"] == "AmbientStream" for row in source_definitions)
        != EXPECTED_STREAM_COUNT
        or len(multisound_rows) != EXPECTED_MULTISOUND_COUNT
        or len(sample_records) != EXPECTED_SAMPLE_COUNT
    ):
        raise ValueError("exact Fords ambient definition closure changed")

    owned = _owned_source_paths(complete_profile)
    owned_sample_paths = _profile_sample_paths(complete_profile)
    reused: list[dict[str, Any]] = []
    additions: list[tuple[str, dict[str, Any]]] = []
    runtime_samples: dict[str, str] = {}
    for sample_id, record in sorted(sample_records.items(), key=lambda item: item[0].casefold()):
        source_path = str(record["path"])
        owners = owned.get(source_path.casefold())
        if owners is not None:
            existing_output = owned_sample_paths.get(sample_id.casefold())
            if existing_output is None:
                raise ValueError(
                    f"owned ambient source lacks exact runtime sample row: {sample_id}"
                )
            runtime_samples[sample_id] = existing_output
            reused.append(
                {
                    "resourceIds": owners,
                    "sampleId": sample_id,
                    "sha256": record["sha256"],
                    "source": source_path,
                }
            )
            continue
        suffix = PurePosixPath(source_path).suffix.casefold()
        output = (
            f"assets/audio/ambient/{PurePosixPath(source_path).stem}.wav"
            if suffix == ".wav"
            else f"assets/audio/ambient/{PurePosixPath(source_path).stem}.mp3"
        )
        runtime_samples[sample_id] = output
        additions.append((sample_id, record))

    wav_paths = sorted(
        (str(record["path"]) for _, record in additions if str(record["path"]).casefold().endswith(".wav")),
        key=str.casefold,
    )
    mp3_paths = sorted(
        (str(record["path"]) for _, record in additions if str(record["path"]).casefold().endswith(".mp3")),
        key=str.casefold,
    )
    if len(wav_paths) != EXPECTED_WAV_COUNT or len(mp3_paths) != EXPECTED_MP3_COUNT:
        raise ValueError("exact Fords ambient resource closure changed")
    resources = [
        {
            "converter": "audio",
            "expected_count": len(wav_paths),
            "id": "fords-ambient-audio-wav-leaves",
            "kind": "audio",
            "limit": len(wav_paths),
            "options": {"force_pcm": True},
            "output": "assets/audio/ambient/{stem}.wav",
            "patterns": wav_paths,
        },
        {
            "converter": "copy",
            "expected_count": 1,
            "id": "fords-ambient-audio-stream",
            "kind": "audio",
            "limit": 1,
            "output": "assets/audio/ambient/waamonh_ambien1.mp3",
            "patterns": mp3_paths,
        },
    ]
    catalog_resolution = _validate_catalog_resolution(resources, catalog_path)
    registry = {
        "complete": True,
        "events": {
            str(row["id"]): {
                "parameters": row["parameters"], "sounds": row["sounds"]
            }
            for row in event_rows
        },
        "multisounds": {
            str(row["id"]): {
                "parameters": row["parameters"], "subsounds": row["subsounds"]
            }
            for row in multisound_rows
        },
        "rootIds": sorted(ROOT_IDS, key=str.casefold),
        "samples": dict(sorted(runtime_samples.items(), key=lambda item: item[0].casefold())),
        "schema": REGISTRY_SCHEMA,
        "schemaVersion": REGISTRY_SCHEMA_VERSION,
    }
    summary = {
        "audioEventCount": EXPECTED_EVENT_COUNT,
        "ambientStreamCount": EXPECTED_STREAM_COUNT,
        "catalogSelectedEntryCount": catalog_resolution["selectedEntryCount"],
        "mapPlacementCount": placement_evidence["count"],
        "mp3SampleCount": EXPECTED_MP3_COUNT,
        "multisoundCount": EXPECTED_MULTISOUND_COUNT,
        "profileResourceCount": len(resources),
        "reusedSampleCount": len(reused),
        "rootCount": len(ROOT_IDS),
        "sampleCount": len(sample_records),
        "wavSampleCount": EXPECTED_WAV_COUNT,
    }
    plan: dict[str, Any] = {
        "profileFragment": {
            "resources": resources,
            "runtimeDataMerge": {"data/audio_events.json": registry},
        },
        "runtimeAudioRegistryAddition": registry,
        "schema": PLAN_SCHEMA,
        "schemaVersion": PLAN_SCHEMA_VERSION,
        "scope": {
            "mapId": "bfme2.map.fords-of-isen-ii",
            "placementEvidence": placement_evidence,
            "rootIds": sorted(ROOT_IDS, key=str.casefold),
            "runtimeMapping": runtime_mapping,
        },
        "sourceDefinitions": source_definitions,
        "sourceEvidence": {
            "catalog": {"path": str(Path(catalog_path).name), "sha256": _file_sha256(Path(catalog_path))},
            "completeProfile": {"id": complete_profile.get("id"), "sha256": _file_sha256(profile_source)},
            "definitionFiles": [
                {"path": path, "sha256": manifest_files[path]["sha256"]}
                for path in sorted({row["source"]["path"] for row in source_definitions}, key=str.casefold)
            ],
            "effectiveAssets": manifest_evidence,
            "manifestSha256": _file_sha256(manifest_source),
            "mapObjectsSha256": _file_sha256(map_source),
            "runtimeAudioSha256": _file_sha256(runtime_source),
        },
        "sourceSamples": [
            {"id": sample_id, **record}
            for sample_id, record in sorted(sample_records.items(), key=lambda item: item[0].casefold())
        ],
        "summary": summary,
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def _source_definition_evidence(
    location: DefinitionLocation,
    *,
    root_id: str,
    body_samples: list[str],
    envelope_samples: list[str],
    source_parameters: list[dict[str, str]],
    runtime_parameters: list[dict[str, str]],
    macros: Mapping[str, str],
) -> dict[str, Any]:
    source_min = _one_parameter(source_parameters, "minrange")
    source_max = _one_parameter(source_parameters, "maxrange")
    control = _one_parameter(source_parameters, "control")
    return {
        "bodySampleIds": body_samples,
        "envelopeSampleIds": envelope_samples,
        "id": location.block.name,
        "rangeAndLoop": {
            "control": control,
            "loop": control is not None and control.casefold() == "loop",
            "resolvedMaxRange": _resolve_parameter_value(source_max, macros) if source_max is not None else None,
            "resolvedMinRange": _resolve_parameter_value(source_min, macros) if source_min is not None else None,
            "sourceMaxRange": source_max,
            "sourceMinRange": source_min,
        },
        "rootId": root_id,
        "runtimeParameters": runtime_parameters,
        "source": {"line": location.line, "path": location.path, "sha256": location.sha256},
        "sourceKind": location.kind,
        "sourceParameters": source_parameters,
    }


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets-root", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--map-objects", required=True)
    parser.add_argument("--complete-profile", required=True)
    parser.add_argument("--runtime-audio", required=True)
    parser.add_argument("--output", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    plan = compose_fords_ambient_audio_plan(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        map_objects_path=args.map_objects,
        complete_profile_path=args.complete_profile,
        runtime_audio_path=args.runtime_audio,
    )
    write_json_atomic(Path(args.output), plan)
    print(json.dumps({"aggregateSha256": plan["aggregateSha256"], "output": str(Path(args.output).resolve()), "summary": plan["summary"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
