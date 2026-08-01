"""Seal the exact retail audio closure used by Men building lifecycles.

The emitted contract is payload-free.  It retains definition and media hashes,
archive provenance, byte spans, source parameters, and a minimal unintegrated
profile-fragment proposal.  It deliberately does not translate SAGE mixer,
spatialization, random-pool, or loop semantics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Iterable, Mapping, Sequence

from .paths import safe_relative_parts
from .sage_ini import IniBlock, parse_flat_named_blocks
from .util import write_json_atomic


CONTRACT_SCHEMA = "openbfme.retail-men-building-damage-audio"
CONTRACT_SCHEMA_VERSION = 0
ROOT_IDS = (
    "BuildingSink",
    "BuildingBigConstructionLoop",
    "GondorArcheryRangeArrowQuiver",
    "GondorArcheryRangeBows",
    "GondorArcheryRangeDrawBow",
    "GondorArcheryRangeVoiceFire",
    "GondorArcheryRangeVoiceAim",
    "GondorArcheryRangeHits1",
    "GondorArcheryRangeHits2",
)

_MAX_JSON_BYTES = 64 * 1024 * 1024
_MAX_INI_FILES = 4096
_MAX_TOTAL_INI_BYTES = 256 * 1024 * 1024
_IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255}$")
_EVENT_HEADER = re.compile(
    rb"(?im)^[ \t]*AudioEvent[ \t]+"
    rb"(?P<id>[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255})"
    rb"(?P<tail>[^\r\n]*)(?:\r?\n|$)"
)
_END_LINE = re.compile(rb"(?im)^[ \t]*End[ \t]*(?:;[^\r\n]*)?(?:\r?\n|$)")
_WEIGHTED_REFERENCE = re.compile(
    r"^(?P<id>[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255})"
    r"(?::(?P<weight>[0-9]+))?$"
)


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
    if source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{label} exceeds the bounded size")
    value = json.loads(source.read_text("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value, source


def _identifier(value: str, label: str) -> str:
    if _IDENTIFIER.fullmatch(value) is None:
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _manifest_files(document: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    if (
        document.get("schema") != "openbfme.effective-assets-manifest"
        or document.get("schema_version") != 0
    ):
        raise ValueError("unsupported effective-assets manifest")
    raw_files = document.get("files")
    if not isinstance(raw_files, list):
        raise ValueError("effective-assets manifest files must be an array")
    totals = document.get("totals")
    if not isinstance(totals, dict) or totals.get("files") != len(raw_files):
        raise ValueError("effective-assets manifest file total changed")
    result: dict[str, dict[str, Any]] = {}
    folded: set[str] = set()
    for value in raw_files:
        if not isinstance(value, dict):
            raise ValueError("effective-assets manifest contains an invalid file")
        path = value.get("path")
        if not isinstance(path, str):
            raise ValueError("effective-assets file path must be a string")
        normalized = "/".join(safe_relative_parts(path))
        key = normalized.casefold()
        if key in folded:
            raise ValueError(f"duplicate effective path: {normalized}")
        folded.add(key)
        if any(
            isinstance(value.get(field), bool)
            or not isinstance(value.get(field), int)
            or int(value[field]) < 0
            for field in ("offset", "precedence", "size")
        ):
            raise ValueError(f"invalid manifest numeric provenance: {normalized}")
        digest = value.get("sha256")
        archive = value.get("archive")
        if (
            not isinstance(digest, str)
            or re.fullmatch(r"[0-9a-f]{64}", digest) is None
            or not isinstance(archive, str)
        ):
            raise ValueError(f"invalid manifest provenance: {normalized}")
        result[normalized] = dict(value)
    return result


def _verified_bytes(
    root: Path, virtual_path: str, record: Mapping[str, Any]
) -> bytes:
    source = root.joinpath(*safe_relative_parts(virtual_path))
    payload = source.read_bytes()
    if len(payload) != record["size"]:
        raise ValueError(f"effective asset size changed: {virtual_path}")
    if hashlib.sha256(payload).hexdigest() != record["sha256"]:
        raise ValueError(f"effective asset digest changed: {virtual_path}")
    return payload


def _strip_header_comment(value: bytes) -> bytes:
    semicolon = value.find(b";")
    slash = value.find(b"//")
    indexes = [item for item in (semicolon, slash) if item >= 0]
    return value[: min(indexes)].strip() if indexes else value.strip()


def _event_blocks(payload: bytes, path: str) -> list[dict[str, Any]]:
    """Return exact half-open byte spans for flat AudioEvent blocks."""

    result: list[dict[str, Any]] = []
    headers = list(_EVENT_HEADER.finditer(payload))
    for index, header in enumerate(headers):
        search_end = headers[index + 1].start() if index + 1 < len(headers) else len(payload)
        end = _END_LINE.search(payload, header.end(), search_end)
        if end is None:
            name = header.group("id").decode("ascii")
            raise ValueError(f"unterminated AudioEvent {name!r} in {path}")
        block_end = end.end()
        block_payload = payload[header.start() : block_end]
        parsed = parse_flat_named_blocks(block_payload, "AudioEvent")
        if len(parsed) != 1:
            raise ValueError(f"invalid AudioEvent block in {path}")
        tail = _strip_header_comment(header.group("tail"))
        result.append(
            {
                "block": parsed[0],
                "byteEndExclusive": block_end,
                "byteStart": header.start(),
                "lineEnd": payload.count(b"\n", 0, end.start()) + 1,
                "lineStart": payload.count(b"\n", 0, header.start()) + 1,
                "parentSyntax": tail.decode("cp1252") if tail else None,
                "sha256": hashlib.sha256(block_payload).hexdigest(),
            }
        )
    return result


def _reference_rows(values: Iterable[str], field: str) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for value in values:
        for token in value.split():
            match = _WEIGHTED_REFERENCE.fullmatch(token)
            if match is None:
                raise ValueError(f"unsafe {field} audio reference: {token!r}")
            row: dict[str, Any] = {"id": match.group("id"), "role": field}
            if match.group("weight") is not None:
                row["weight"] = int(match.group("weight"))
            result.append(row)
    return result


def _parameter_rows(block: IniBlock) -> list[dict[str, str]]:
    return [
        {"field": field, "value": value}
        for field, value in block.assignments
        if field.casefold() != "sounds"
    ]


def _control_fields(block: IniBlock) -> dict[str, list[str]]:
    wanted = (
        "Volume",
        "VolumeShift",
        "Pitch",
        "PitchShift",
        "Delay",
        "Priority",
        "Limit",
        "Visibility",
        "Type",
        "Control",
        "PlayPercent",
        "MinRange",
        "MaxRange",
        "SubmixSlider",
    )
    return {name: list(block.values(name)) for name in wanted}


def _catalog_entries(
    catalog: Mapping[str, Any], virtual_path: str
) -> list[dict[str, Any]]:
    entries = catalog.get("entries")
    if not isinstance(entries, list):
        raise ValueError("catalog entries must be an array")
    selected = [
        dict(value)
        for value in entries
        if isinstance(value, dict)
        and str(value.get("name", "")).casefold() == virtual_path.casefold()
    ]
    return sorted(
        selected,
        key=lambda value: (
            int(value.get("precedence", 2**31 - 1)),
            str(value.get("archive", "")).casefold(),
        ),
    )


def _file_precedence(
    *,
    virtual_path: str,
    winner: Mapping[str, Any],
    catalog: Mapping[str, Any],
) -> dict[str, Any]:
    candidates = _catalog_entries(catalog, virtual_path)
    exact = [
        value
        for value in candidates
        if value.get("archive") == winner.get("archive")
        and value.get("offset") == winner.get("offset")
        and value.get("precedence") == winner.get("precedence")
        and value.get("size") == winner.get("size")
    ]
    if len(exact) != 1:
        raise ValueError(f"manifest winner is not exact in catalog: {virtual_path}")
    return {
        "candidateCount": len(candidates),
        "ignoredLowerPrecedenceEntries": [
            {
                "archive": value["archive"],
                "offset": value["offset"],
                "precedence": value["precedence"],
                "size": value["size"],
            }
            for value in candidates
            if value is not exact[0]
        ],
        "selectionAuthority": "effective-assets-manifest-winner",
        "winner": {
            "archive": winner["archive"],
            "offset": winner["offset"],
            "precedence": winner["precedence"],
            "size": winner["size"],
        },
    }


def _sample_index(
    manifest_files: Mapping[str, Mapping[str, Any]],
) -> dict[str, list[tuple[str, Mapping[str, Any]]]]:
    result: dict[str, list[tuple[str, Mapping[str, Any]]]] = {}
    for path, record in manifest_files.items():
        suffix = PurePosixPath(path).suffix.casefold()
        if not path.casefold().startswith("data/audio/") or suffix not in {
            ".wav",
            ".mp3",
        }:
            continue
        result.setdefault(PurePosixPath(path).stem.casefold(), []).append((path, record))
    return result


def _profile_indexes(
    profile: Mapping[str, Any],
) -> tuple[dict[str, list[str]], dict[str, str], dict[str, Any]]:
    resources = profile.get("resources")
    runtime_data = profile.get("runtime_data")
    if not isinstance(resources, list) or not isinstance(runtime_data, dict):
        raise ValueError("complete profile structure changed")
    registry = runtime_data.get("data/audio_events.json")
    if not isinstance(registry, dict):
        raise ValueError("complete profile audio registry is absent")
    samples = registry.get("samples")
    events = registry.get("events")
    if not isinstance(samples, dict) or not isinstance(events, dict):
        raise ValueError("complete profile audio registry is invalid")
    owners: dict[str, list[str]] = {}
    for raw in resources:
        if not isinstance(raw, dict) or not isinstance(raw.get("patterns"), list):
            raise ValueError("complete profile resource is invalid")
        resource_id = str(raw.get("id", ""))
        for value in raw["patterns"]:
            if not isinstance(value, str):
                raise ValueError("complete profile pattern is invalid")
            normalized = "/".join(safe_relative_parts(value))
            owners.setdefault(normalized.casefold(), []).append(resource_id)
    return (
        {key: sorted(set(value)) for key, value in owners.items()},
        {str(key).casefold(): str(value) for key, value in samples.items()},
        {str(key).casefold(): value for key, value in events.items()},
    )


def _event_registry_value(block: IniBlock) -> dict[str, Any]:
    return {
        "parameters": _parameter_rows(block),
        "sounds": [
            {key: value for key, value in row.items() if key != "role"}
            for row in _reference_rows(block.values("Sounds"), "body")
        ],
    }


def _minimal_fragment(missing: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    samples = {
        str(row["id"]): (
            "assets/audio/men-building-lifecycle/"
            f"{PurePosixPath(str(row['path'])).stem}.wav"
        )
        for row in missing
    }
    resources: list[dict[str, Any]] = []
    if missing:
        paths = sorted({str(row["path"]) for row in missing}, key=str.casefold)
        resources.append(
            {
                "converter": "audio",
                "expected_count": len(paths),
                "id": "men-building-lifecycle-envelope-audio-leaves",
                "kind": "audio",
                "limit": len(paths),
                "options": {"force_pcm": True},
                "output": "assets/audio/men-building-lifecycle/{stem}.wav",
                "patterns": paths,
            }
        )
    return {
        "integrationStatus": "proposal-only-not-integrated",
        "resources": resources,
        "runtimeDataMerge": {
            "data/audio_events.json": {
                "samples": dict(sorted(samples.items(), key=lambda item: item[0].casefold()))
            }
        },
    }


def compose_men_damage_audio_contract(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    complete_profile_path: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve()
    manifest, manifest_source = _load_json(manifest_path, "effective-assets manifest")
    catalog, catalog_source = _load_json(catalog_path, "catalog")
    profile, profile_source = _load_json(complete_profile_path, "complete profile")
    manifest_files = _manifest_files(manifest)
    owners, registry_samples, registry_events = _profile_indexes(profile)

    ini_rows = sorted(
        (
            (path, record)
            for path, record in manifest_files.items()
            if path.casefold().startswith("data/ini/")
            and path.casefold().endswith(".ini")
        ),
        key=lambda item: item[0].casefold(),
    )
    if len(ini_rows) > _MAX_INI_FILES:
        raise ValueError("effective INI file count exceeds safety bound")
    wanted = {value.casefold(): value for value in ROOT_IDS}
    matches: dict[str, list[tuple[str, Mapping[str, Any], dict[str, Any]]]] = {
        key: [] for key in wanted
    }
    total_ini_bytes = 0
    for path, record in ini_rows:
        payload = _verified_bytes(root, path, record)
        total_ini_bytes += len(payload)
        if total_ini_bytes > _MAX_TOTAL_INI_BYTES:
            raise ValueError("effective INI bytes exceed safety bound")
        for span in _event_blocks(payload, path):
            block = span["block"]
            assert isinstance(block, IniBlock)
            key = block.name.casefold()
            if key in wanted:
                matches[key].append((path, record, span))

    sample_candidates = _sample_index(manifest_files)
    definitions: list[dict[str, Any]] = []
    all_samples: dict[str, dict[str, Any]] = {}
    blockers: list[dict[str, Any]] = []
    missing_definitions: list[str] = []
    for requested in ROOT_IDS:
        located = matches[requested.casefold()]
        if len(located) != 1:
            code = (
                "missing-audio-definition"
                if not located
                else "ambiguous-effective-audio-definition"
            )
            blockers.append(
                {
                    "code": code,
                    "id": requested,
                    "locations": [item[0] for item in located],
                }
            )
            missing_definitions.append(requested)
            continue
        path, source_record, span = located[0]
        block = span["block"]
        assert isinstance(block, IniBlock)
        parent_syntax = span["parentSyntax"]
        if parent_syntax is not None:
            blockers.append(
                {
                    "code": "unresolved-audio-inheritance-syntax",
                    "id": requested,
                    "syntax": parent_syntax,
                }
            )
        references = []
        references.extend(_reference_rows(block.values("Sounds"), "body"))
        references.extend(_reference_rows(block.values("Attack"), "attack"))
        references.extend(_reference_rows(block.values("Decay"), "decay"))
        sample_rows: list[dict[str, Any]] = []
        for reference in references:
            sample_id = str(reference["id"])
            candidates = sorted(
                sample_candidates.get(sample_id.casefold(), []),
                key=lambda item: item[0].casefold(),
            )
            if len(candidates) != 1:
                blockers.append(
                    {
                        "code": (
                            "missing-audio-leaf"
                            if not candidates
                            else "ambiguous-audio-leaf"
                        ),
                        "eventId": block.name,
                        "sampleId": sample_id,
                        "paths": [item[0] for item in candidates],
                    }
                )
                continue
            sample_path, sample_record = candidates[0]
            _verified_bytes(root, sample_path, sample_record)
            registry_output = registry_samples.get(sample_id.casefold())
            resource_ids = owners.get(sample_path.casefold(), [])
            row = {
                **reference,
                "archive": sample_record["archive"],
                "filename": PurePosixPath(sample_path).name,
                "offset": sample_record["offset"],
                "path": sample_path,
                "precedence": sample_record["precedence"],
                "profileAudioRegistryOutput": registry_output,
                "profileResourceIds": resource_ids,
                "registryPresent": registry_output is not None,
                "sha256": sample_record["sha256"],
                "size": sample_record["size"],
            }
            sample_rows.append(row)
            existing = all_samples.get(sample_id.casefold())
            if existing is not None and (
                existing["path"] != sample_path or existing["id"] != sample_id
            ):
                raise ValueError(f"sample identity changed by case: {sample_id}")
            all_samples[sample_id.casefold()] = row
        expected_registry = _event_registry_value(block)
        actual_registry = registry_events.get(block.name.casefold())
        registry_exact = actual_registry == expected_registry
        if not registry_exact:
            blockers.append(
                {
                    "code": "profile-audio-event-registry-mismatch",
                    "id": block.name,
                }
            )
        byte_start = int(span["byteStart"])
        byte_end = int(span["byteEndExclusive"])
        definitions.append(
            {
                "caseExact": requested == block.name,
                "controlFields": _control_fields(block),
                "declaredId": block.name,
                "definitionPrecedence": {
                    "effectiveDefinitionCount": len(located),
                    "inheritanceParent": parent_syntax,
                    "policy": (
                        "unique-effective-definition-no-inheritance"
                        if parent_syntax is None
                        else "unresolved-inheritance-syntax"
                    ),
                    "sourceFile": _file_precedence(
                        virtual_path=path,
                        winner=source_record,
                        catalog=catalog,
                    ),
                },
                "parametersInSourceOrder": _parameter_rows(block),
                "profileEventRegistryExact": registry_exact,
                "requestedId": requested,
                "samplesInSourceOrder": sample_rows,
                "source": {
                    "archive": source_record["archive"],
                    "archiveByteEndExclusive": int(source_record["offset"]) + byte_end,
                    "archiveByteStart": int(source_record["offset"]) + byte_start,
                    "blockByteLength": byte_end - byte_start,
                    "blockSha256": span["sha256"],
                    "byteEndExclusive": byte_end,
                    "byteStart": byte_start,
                    "fileSha256": source_record["sha256"],
                    "lineEnd": span["lineEnd"],
                    "lineStart": span["lineStart"],
                    "offset": source_record["offset"],
                    "path": path,
                    "precedence": source_record["precedence"],
                },
            }
        )

    definitions.sort(key=lambda item: str(item["requestedId"]).casefold())
    unique_samples = sorted(all_samples.values(), key=lambda item: str(item["id"]).casefold())
    missing_registry = [row for row in unique_samples if not row["registryPresent"]]
    missing_owned = [row for row in unique_samples if not row["profileResourceIds"]]
    for row in missing_registry:
        blockers.append(
            {
                "code": "profile-audio-registry-leaf-missing",
                "path": row["path"],
                "sampleId": row["id"],
            }
        )
    profile_resources = profile.get("resources")
    assert isinstance(profile_resources, list)
    fragment = _minimal_fragment(missing_registry)
    proposed_resources = fragment["resources"]
    assert isinstance(proposed_resources, list)
    contract: dict[str, Any] = {
        "blockers": sorted(
            blockers,
            key=lambda item: (
                str(item.get("code", "")).casefold(),
                str(item.get("id", item.get("sampleId", ""))).casefold(),
            ),
        ),
        "definitions": definitions,
        "mixerSemanticBoundary": {
            "proven": (
                "source fields, ordered pools, attack/decay membership, exact media "
                "leaves, and archive provenance"
            ),
            "unresolved": [
                "SAGE random-pool selection algorithm and seed",
                "duplicate-field evaluation semantics",
                "volume and pitch numeric-unit translation",
                "range attenuation and world-space spatialization",
                "Type shroud audience and voice gating",
                "Priority Limit and PlayPercent arbitration",
                "Control loop attack body decay stitching and stop timing",
                "SubmixSlider routing and final mastering",
            ],
        },
        "profileFragmentProposal": fragment,
        "schema": CONTRACT_SCHEMA,
        "schemaVersion": CONTRACT_SCHEMA_VERSION,
        "scope": {
            "profileId": profile.get("id"),
            "rootIds": list(ROOT_IDS),
            "source": "five Men schema-v1 building lifecycle contracts",
        },
        "sourceEvidence": {
            "catalogSha256": _file_sha256(catalog_source),
            "completeProfileSha256": _file_sha256(profile_source),
            "effectiveAssetsAggregateSha256": manifest.get("aggregate_sha256"),
            "effectiveAssetsManifestSha256": _file_sha256(manifest_source),
            "policy": "payload-free-hashes-and-provenance-only",
        },
        "summary": {
            "accountedIdentifierCount": len(definitions),
            "definitionBlockerCount": sum(
                row["code"]
                in {
                    "missing-audio-definition",
                    "ambiguous-effective-audio-definition",
                    "unresolved-audio-inheritance-syntax",
                }
                for row in blockers
            ),
            "identifierCount": len(ROOT_IDS),
            "missingDefinitionCount": len(missing_definitions),
            "missingLeafCount": sum(
                row["code"] in {"missing-audio-leaf", "ambiguous-audio-leaf"}
                for row in blockers
            ),
            "profileAudioRegistryMissingByteCount": sum(
                int(row["size"]) for row in missing_registry
            ),
            "profileAudioRegistryMissingLeafCount": len(missing_registry),
            "profileResourceCount": len(profile_resources),
            "profileResourceMissingLeafCount": len(missing_owned),
            "proposedResourceCount": len(proposed_resources),
            "sourceReferenceCount": sum(
                len(row["samplesInSourceOrder"]) for row in definitions
            ),
            "uniqueSampleCount": len(unique_samples),
        },
        "uniqueSamples": unique_samples,
    }
    contract["aggregateSha256"] = _canonical_sha256(contract)
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets-root", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--complete-profile", required=True)
    parser.add_argument("--output", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = compose_men_damage_audio_contract(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        complete_profile_path=args.complete_profile,
    )
    output = Path(args.output)
    write_json_atomic(output, contract)
    print(
        json.dumps(
            {
                "aggregateSha256": contract["aggregateSha256"],
                "output": str(output.resolve()),
                "summary": contract["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
