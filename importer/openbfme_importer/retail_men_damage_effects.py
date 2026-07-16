"""Seal the exact Men building-damage FX and particle-definition closure.

This audit consumes only the composed private profile and the manifest-bound
effective retail tree.  It does not convert, publish, or modify a profile.  The
result preserves definition scalars and provenance but never embeds source file
bytes or render-asset payloads.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
from fnmatch import fnmatchcase
import hashlib
import json
from pathlib import Path
from pathlib import PurePosixPath
import re
from typing import Any, Iterable, Mapping

from .paths import safe_relative_parts
from .retail_visual_profile import (
    _canonical_sha256,
    _mapping,
    _source_record,
    _stable_slug,
    _validate_effective_manifest,
)
from .sage_particles import (
    ParticleAssignment,
    ParticleBlock,
    ParticleDefinition,
    ParticleEntry,
    parse_particle_definitions,
)
from .util import write_json_atomic


SCHEMA = "openbfme.retail-men-damage-effects-closure"
SCHEMA_VERSION = 0
FRAGMENT_SCHEMA = "openbfme.retail-men-damage-effects-profile-fragment-proposal"
FRAGMENT_SCHEMA_VERSION = 0
PROFILE_SCHEMA = "openbfme.building-lifecycle-presentation"
PROFILE_SCHEMA_VERSION = 1
PARTICLE_BINDINGS_PATH = "effects/fords-particle-bindings.json"
OBJECTS_PATH = "data/objects.json"
FX_LIST_PATH = "data/ini/fxlist.ini"
PARTICLE_PATHS = {
    "ParticleSystem": "data/ini/particlesystem.ini",
    "FXParticleSystem": "data/ini/fxparticlesystem.ini",
}
EXPECTED_OBJECT_IDS = (
    "bfme2.object.men-fortress",
    "bfme2.object.men-farm",
    "bfme2.object.men-barracks",
    "bfme2.object.men-archery-range",
    "bfme2.object.men-stable",
)
EXPECTED_BINDING_COUNT = 17
EXPECTED_ATTACHMENT_COUNT = 88
EXPECTED_PARTICLE_STATE_BLOCK_COUNT = 29
MAX_JSON_BYTES = 64 * 1024 * 1024
_FX_HEADER = re.compile(r"^FXList\s+(\S+)\s*$", re.IGNORECASE)


def _load_json(path: Path, label: str) -> dict[str, Any]:
    source = path.expanduser().resolve()
    if not source.is_file() or source.stat().st_size > MAX_JSON_BYTES:
        raise ValueError(f"{label} is missing or exceeds {MAX_JSON_BYTES} bytes")
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} root must be an object")
    return value


def _safe_root(root: Path) -> Path:
    resolved = root.expanduser().resolve()
    manifest = resolved / ".openbfme" / "manifest.json"
    if not resolved.is_dir() or not manifest.is_file():
        raise ValueError("effective-assets root is missing its private manifest")
    return resolved


def _read_bound(
    root: Path, virtual_path: str, sources: Mapping[str, Mapping[str, Any]]
) -> bytes:
    record = sources.get(virtual_path)
    if record is None:
        raise ValueError(f"manifest-bound source is missing: {virtual_path}")
    candidate = root.joinpath(*safe_relative_parts(virtual_path)).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise ValueError("manifest-bound source escaped effective-assets root") from exc
    payload = candidate.read_bytes()
    if len(payload) != record["size"]:
        raise ValueError(f"manifest-bound source size mismatch: {virtual_path}")
    if hashlib.sha256(payload).hexdigest() != record["sha256"]:
        raise ValueError(f"manifest-bound source digest mismatch: {virtual_path}")
    return payload


def _strip_comment(text: str) -> str:
    quoted = False
    for index, character in enumerate(text):
        if character == '"':
            quoted = not quoted
        elif not quoted and character == ";":
            return text[:index].rstrip()
        elif not quoted and text[index : index + 2] == "//":
            return text[:index].rstrip()
    return text.rstrip()


def _line_spans(payload: bytes) -> list[tuple[int, int, int, str]]:
    rows: list[tuple[int, int, int, str]] = []
    offset = 0
    for line_number, raw in enumerate(payload.splitlines(keepends=True), start=1):
        try:
            text = raw.decode("cp1252").rstrip("\r\n")
        except UnicodeDecodeError as exc:
            raise ValueError(
                f"FX-list source is not CP1252 at line {line_number}"
            ) from exc
        rows.append((line_number, offset, offset + len(raw), text))
        offset += len(raw)
    return rows


def _span(
    payload: bytes, start: int, end: int, start_line: int, end_line: int
) -> dict[str, Any]:
    block = payload[start:end]
    return {
        "startLine": start_line,
        "endLine": end_line,
        "startByte": start,
        "endByte": end,
        "byteLength": len(block),
        "sha256": hashlib.sha256(block).hexdigest(),
    }


def parse_fx_lists(payload: bytes) -> dict[str, dict[str, Any]]:
    """Parse top-level FXList blocks while preserving nested section order."""

    rows = _line_spans(payload)
    result: dict[str, dict[str, Any]] = {}
    index = 0
    while index < len(rows):
        line_number, start_byte, _, raw = rows[index]
        content = _strip_comment(raw).strip()
        match = _FX_HEADER.fullmatch(content)
        if match is None:
            index += 1
            continue
        name = match.group(1)
        key = name.casefold()
        if key in result:
            raise ValueError(f"duplicate FXList definition: {name}")
        sections: list[dict[str, Any]] = []
        assignments: list[dict[str, Any]] = []
        stack: list[dict[str, Any]] = []
        cursor = index + 1
        while cursor < len(rows):
            number, child_start, child_end, child_raw = rows[cursor]
            child = _strip_comment(child_raw).strip()
            if not child:
                cursor += 1
                continue
            if child.casefold() == "end":
                if not stack:
                    source = _span(payload, start_byte, child_end, line_number, number)
                    result[key] = {
                        "fxListId": name,
                        "assignments": assignments,
                        "sections": sections,
                        "sourceSpan": source,
                    }
                    break
                opened = stack.pop()
                opened["sourceSpan"] = _span(
                    payload,
                    int(opened.pop("_startByte")),
                    child_end,
                    int(opened.pop("_startLine")),
                    number,
                )
                cursor += 1
                continue
            if "=" in child:
                field, value = (part.strip() for part in child.split("=", 1))
                assignment = {
                    "field": field,
                    "value": value,
                    "sourceSpan": _span(
                        payload, child_start, child_end, number, number
                    ),
                }
                target = stack[-1]["assignments"] if stack else assignments
                target.append(assignment)
                cursor += 1
                continue
            section = {
                "kind": child,
                "assignments": [],
                "_startByte": child_start,
                "_startLine": number,
            }
            if stack:
                stack[-1].setdefault("sections", []).append(section)
            else:
                sections.append(section)
            stack.append(section)
            cursor += 1
        else:
            raise ValueError(f"unterminated FXList definition: {name}")
        index = cursor + 1
    return result


def _section_names(record: Mapping[str, Any], kind: str) -> list[str]:
    result: list[str] = []
    for section in record["sections"]:
        if str(section["kind"]).casefold() != kind.casefold():
            continue
        names = [
            item["value"]
            for item in section["assignments"]
            if str(item["field"]).casefold() == "name"
        ]
        if len(names) != 1:
            raise ValueError(
                f"{record['fxListId']} {section['kind']} section must have one Name"
            )
        result.append(str(names[0]))
    return result


def _source_span_document(source: Any) -> dict[str, Any]:
    return {
        "startLine": source.start_line,
        "endLine": source.end_line,
        "startByte": source.start_byte,
        "endByte": source.end_byte,
        "byteLength": source.byte_length,
        "sha256": source.sha256,
    }


def _particle_entry_document(entry: ParticleEntry) -> dict[str, Any]:
    if isinstance(entry, ParticleAssignment):
        return {
            "type": "assignment",
            "field": entry.field,
            "value": entry.value,
            "sourceSpan": _source_span_document(entry.source),
        }
    if not isinstance(entry, ParticleBlock):
        raise TypeError("unsupported particle entry")
    return {
        "type": "block",
        "field": entry.field,
        "selector": entry.selector,
        "entries": [_particle_entry_document(child) for child in entry.entries],
        "sourceSpan": _source_span_document(entry.source),
    }


def _walk_assignments(
    entries: Iterable[ParticleEntry], scope: tuple[str, ...] = ()
) -> Iterable[tuple[tuple[str, ...], ParticleAssignment]]:
    for entry in entries:
        if isinstance(entry, ParticleAssignment):
            yield scope, entry
        elif isinstance(entry, ParticleBlock):
            label = (
                entry.field
                if entry.selector is None
                else f"{entry.field}={entry.selector}"
            )
            yield from _walk_assignments(entry.entries, (*scope, label))


def _profile_resources(profile: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    resources = profile.get("resources")
    if not isinstance(resources, list) or not all(
        isinstance(row, dict) for row in resources
    ):
        raise ValueError("complete profile resources must be an array of objects")
    return resources


def _matching_resources(resources: Iterable[Mapping[str, Any]], path: str) -> list[str]:
    matches: list[str] = []
    for resource in resources:
        patterns = resource.get("patterns", [])
        if not isinstance(patterns, list):
            continue
        if any(
            isinstance(pattern, str)
            and fnmatchcase(path.casefold(), pattern.casefold())
            for pattern in patterns
        ):
            matches.append(str(resource.get("id")))
    return sorted(matches, key=lambda value: (value.casefold(), value))


def _definition_resources(
    resources: Iterable[Mapping[str, Any]], kind: str, name: str
) -> list[str]:
    result: list[str] = []
    for resource in resources:
        options = resource.get("options")
        if (
            resource.get("converter") == "sage-particle-definition"
            and isinstance(options, dict)
            and str(options.get("kind", "")).casefold() == kind.casefold()
            and str(options.get("name", "")).casefold() == name.casefold()
        ):
            result.append(str(resource.get("id")))
    return sorted(result, key=lambda value: (value.casefold(), value))


def _lifecycle_contracts(profile: Mapping[str, Any]) -> list[dict[str, Any]]:
    runtime = profile.get("runtime_data")
    if not isinstance(runtime, dict):
        raise ValueError("complete profile runtime_data must be an object")
    objects_doc = runtime.get(OBJECTS_PATH)
    if not isinstance(objects_doc, dict) or not isinstance(
        objects_doc.get("objects"), list
    ):
        raise ValueError("complete profile is missing data/objects.json")
    by_id: dict[str, dict[str, Any]] = {}
    for raw in objects_doc["objects"]:
        if not isinstance(raw, dict):
            continue
        lifecycle = raw.get("presentation", {}).get("buildingLifecycle")
        if not isinstance(lifecycle, dict):
            continue
        object_id = lifecycle.get("objectId")
        if object_id in EXPECTED_OBJECT_IDS:
            if object_id in by_id:
                raise ValueError(f"duplicate Men lifecycle object: {object_id}")
            if (
                lifecycle.get("schema") != PROFILE_SCHEMA
                or lifecycle.get("schemaVersion") != PROFILE_SCHEMA_VERSION
            ):
                raise ValueError(f"unsupported Men lifecycle contract: {object_id}")
            by_id[str(object_id)] = lifecycle
    if tuple(by_id) != EXPECTED_OBJECT_IDS:
        raise ValueError(f"Men lifecycle object order mismatch: {tuple(by_id)!r}")
    return [by_id[object_id] for object_id in EXPECTED_OBJECT_IDS]


def _bindings(
    contracts: Iterable[Mapping[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int]:
    fx_rows: list[dict[str, Any]] = []
    attachments: list[dict[str, Any]] = []
    state_blocks = 0
    for contract in contracts:
        effects = _mapping(contract.get("effects"), "Men lifecycle effects")
        for row in effects.get("enteringStateBindings", []):
            if not isinstance(row, dict):
                raise ValueError("invalid entering-state FX binding")
            fx_rows.append(deepcopy(row))
        for row in effects.get("particleAttachments", []):
            if not isinstance(row, dict):
                raise ValueError("invalid particle attachment")
            attachments.append(deepcopy(row))
        blockers = contract.get("blockers", [])
        block = next(
            (
                row
                for row in blockers
                if isinstance(row, dict)
                and row.get("code") == "particle-system-binding"
            ),
            None,
        )
        if not isinstance(block, dict) or type(block.get("evidenceCount")) is not int:
            raise ValueError("Men lifecycle particle block evidence is missing")
        state_blocks += int(block["evidenceCount"])
    if len(fx_rows) != EXPECTED_BINDING_COUNT:
        raise ValueError(
            f"expected {EXPECTED_BINDING_COUNT} entering-state FX bindings"
        )
    if len(attachments) != EXPECTED_ATTACHMENT_COUNT:
        raise ValueError(f"expected {EXPECTED_ATTACHMENT_COUNT} particle attachments")
    if state_blocks != EXPECTED_PARTICLE_STATE_BLOCK_COUNT:
        raise ValueError(
            f"expected {EXPECTED_PARTICLE_STATE_BLOCK_COUNT} particle-bearing state blocks"
        )
    return fx_rows, attachments, state_blocks


def _sorted_unique(values: Iterable[str]) -> list[str]:
    return sorted(set(values), key=lambda value: (value.casefold(), value))


def build_contract(
    effective_root: Path | str,
    complete_profile: Mapping[str, Any],
    effective_manifest: Mapping[str, Any],
) -> dict[str, Any]:
    """Return the deterministic payload-free Men damage-effects contract."""

    root = _safe_root(Path(effective_root))
    sources, manifest_evidence = _validate_effective_manifest(effective_manifest)
    resources = _profile_resources(complete_profile)
    contracts = _lifecycle_contracts(complete_profile)
    fx_bindings, attachments, state_block_count = _bindings(contracts)
    state_groups: dict[tuple[str, tuple[str, ...]], list[int]] = {}
    for position, attachment in enumerate(attachments):
        key = (
            str(attachment["sourceObject"]),
            tuple(str(value) for value in attachment["sourceConditions"]),
        )
        state_groups.setdefault(key, []).append(position)
    if len(state_groups) != state_block_count:
        raise ValueError("particle attachment state-block grouping mismatch")
    particle_state_blocks = [
        {
            "sourceObject": key[0],
            "sourceConditions": list(key[1]),
            "attachmentIndexes": positions,
            "attachmentCount": len(positions),
        }
        for key, positions in sorted(
            state_groups.items(),
            key=lambda item: (item[0][0].casefold(), item[0][1], item[0][0]),
        )
    ]
    runtime = _mapping(
        complete_profile.get("runtime_data"), "complete profile runtime_data"
    )
    current_bindings = _mapping(
        runtime.get(PARTICLE_BINDINGS_PATH), "Fords particle bindings"
    )

    fx_payload = _read_bound(root, FX_LIST_PATH, sources)
    all_fx_lists = parse_fx_lists(fx_payload)
    root_fx_ids = _sorted_unique(str(row["fxListId"]) for row in fx_bindings)
    fx_queue = list(root_fx_ids)
    selected_fx: dict[str, dict[str, Any]] = {}
    while fx_queue:
        requested = fx_queue.pop(0)
        record = all_fx_lists.get(requested.casefold())
        if record is None:
            raise ValueError(f"missing FXList definition: {requested}")
        canonical = str(record["fxListId"])
        if canonical in selected_fx:
            continue
        nested = _section_names(record, "FXList")
        particles = _section_names(record, "ParticleSystem")
        selected_fx[canonical] = {
            **deepcopy(record),
            "particleSystemIds": particles,
            "nestedFxListIds": nested,
            "audioEventIds": _section_names(record, "Sound"),
            "hasViewShake": any(
                str(section["kind"]).casefold() == "viewshake"
                for section in record["sections"]
            ),
            "source": _source_record(FX_LIST_PATH, sources),
        }
        fx_queue.extend(nested)

    direct_system_ids = _sorted_unique(
        str(row["particleSystemId"]) for row in attachments
    )
    fx_system_ids = _sorted_unique(
        system
        for record in selected_fx.values()
        for system in record["particleSystemIds"]
    )
    system_ids = _sorted_unique([*direct_system_ids, *fx_system_ids])

    parsed_by_kind: dict[str, tuple[ParticleDefinition, ...]] = {}
    for kind, path in PARTICLE_PATHS.items():
        parsed_by_kind[kind] = parse_particle_definitions(
            _read_bound(root, path, sources)
        )

    manifest_by_stem: dict[str, list[str]] = {}
    for path in sources:
        suffix = PurePosixPath(path).suffix.casefold()
        if suffix not in {".dds", ".tga", ".w3d"}:
            continue
        manifest_by_stem.setdefault(PurePosixPath(path).stem.casefold(), []).append(
            path
        )

    current_registry = current_bindings.get("definitionRegistry", [])
    if not isinstance(current_registry, list):
        raise ValueError("Fords particle definitionRegistry must be an array")
    registry_keys = {
        (
            str(row.get("kind", "")).casefold(),
            str(row.get("definitionId", "")).casefold(),
        ): row
        for row in current_registry
        if isinstance(row, dict)
    }
    current_fx_ids = {
        str(row.get("fxListId", "")).casefold()
        for row in current_bindings.get("fxLists", [])
        if isinstance(row, dict)
    }

    leaves_by_path: dict[str, dict[str, Any]] = {}
    definition_rows: list[dict[str, Any]] = []
    missing_definition_resources: list[dict[str, Any]] = []
    duplicate_ids: list[str] = []
    for system_id in system_ids:
        candidates: list[dict[str, Any]] = []
        for kind in ("ParticleSystem", "FXParticleSystem"):
            definitions = [
                row
                for row in parsed_by_kind[kind]
                if row.name.casefold() == system_id.casefold()
            ]
            if len(definitions) > 1:
                raise ValueError(f"ambiguous {kind} particle definition: {system_id}")
            if not definitions:
                continue
            definition = definitions[0]
            texture_paths: list[str] = []
            w3d_paths: list[str] = []
            control_assignments: list[dict[str, Any]] = []
            for scope, assignment in _walk_assignments(definition.entries):
                if assignment.field.casefold() == "particlename":
                    stem = PurePosixPath(assignment.value).stem.casefold()
                    matches = manifest_by_stem.get(stem, [])
                    render_matches = [
                        path
                        for path in matches
                        if PurePosixPath(path).suffix.casefold()
                        in {".dds", ".tga", ".w3d"}
                    ]
                    if len(render_matches) != 1:
                        raise ValueError(
                            f"particle render leaf is not unique: {assignment.value!r} -> {render_matches!r}"
                        )
                    leaf_path = render_matches[0]
                    leaf = leaves_by_path.setdefault(
                        leaf_path,
                        {
                            "virtualPath": leaf_path,
                            "kind": (
                                "w3d"
                                if PurePosixPath(leaf_path).suffix.casefold() == ".w3d"
                                else "texture"
                            ),
                            "authoredIdentifiers": [],
                            "source": _source_record(leaf_path, sources),
                            "profileResourceIds": _matching_resources(
                                resources, leaf_path
                            ),
                        },
                    )
                    if assignment.value not in leaf["authoredIdentifiers"]:
                        leaf["authoredIdentifiers"].append(assignment.value)
                    (w3d_paths if leaf["kind"] == "w3d" else texture_paths).append(
                        leaf_path
                    )
                else:
                    control_assignments.append(
                        {
                            "scope": list(scope),
                            "field": assignment.field,
                            "value": assignment.value,
                            "sourceSpan": _source_span_document(assignment.source),
                        }
                    )
            source_path = PARTICLE_PATHS[kind]
            resource_ids = _definition_resources(resources, kind, system_id)
            registry = registry_keys.get((kind.casefold(), system_id.casefold()))
            resource_id = _stable_slug(
                "fords-particle-def", f"{kind}/{definition.name}"
            )
            candidate = {
                "kind": kind,
                "definitionId": definition.name,
                "source": _source_record(source_path, sources),
                "sourceSpan": _source_span_document(definition.source),
                "entries": [
                    _particle_entry_document(entry) for entry in definition.entries
                ],
                "emissionAndControlAssignments": control_assignments,
                "textureVirtualPaths": _sorted_unique(texture_paths),
                "w3dVirtualPaths": _sorted_unique(w3d_paths),
                "profileCoverage": {
                    "definitionResourceIds": resource_ids,
                    "fordsParticleBindingRegistryPresent": registry is not None,
                    "fordsParticleBindingRegistryResourceId": (
                        registry.get("definitionResourceId") if registry else None
                    ),
                },
                "proposedResourceId": resource_id,
            }
            candidates.append(candidate)
            if not resource_ids or registry is None:
                missing_definition_resources.append(
                    {
                        "id": resource_id,
                        "kind": "data",
                        "patterns": [source_path],
                        "required": True,
                        "converter": "sage-particle-definition",
                        "output": f"effects/particles/definitions/{resource_id}.json",
                        "limit": 1,
                        "expected_count": 1,
                        "options": {"kind": kind, "name": definition.name},
                    }
                )
        if not candidates:
            raise ValueError(
                f"missing particle definition in both families: {system_id}"
            )
        if len(candidates) == 1:
            resolution = {
                "status": "exact-single-authored-family",
                "selectedKind": candidates[0]["kind"],
                "crossFamilyPrecedenceRequired": False,
            }
        else:
            duplicate_ids.append(system_id)
            resolution = {
                "status": "unresolved-cross-family-precedence",
                "selectedKind": None,
                "crossFamilyPrecedenceProven": False,
                "requiredHandling": "preserve-both-candidates-fail-closed",
            }
        definition_rows.append(
            {
                "particleSystemId": system_id,
                "referenceOrigins": {
                    "directAttachment": system_id in direct_system_ids,
                    "fxList": system_id in fx_system_ids,
                },
                "definitionCandidates": candidates,
                "familyResolution": resolution,
            }
        )

    leaf_rows = [
        leaves_by_path[path]
        for path in sorted(leaves_by_path, key=lambda value: (value.casefold(), value))
    ]
    missing_leaf_resources: list[dict[str, Any]] = []
    for leaf in leaf_rows:
        leaf["authoredIdentifiers"].sort(key=lambda value: (value.casefold(), value))
        if leaf["profileResourceIds"]:
            continue
        path = str(leaf["virtualPath"])
        if leaf["kind"] == "w3d":
            raise ValueError(
                f"W3D particle render leaf needs a scoped converter decision: {path}"
            )
        resource_id = _stable_slug("fords-particle-texture", path)
        leaf["proposedResourceId"] = resource_id
        missing_leaf_resources.append(
            {
                "id": resource_id,
                "kind": "texture",
                "patterns": [path],
                "required": True,
                "converter": "texture",
                "output": f"assets/textures/effects/{resource_id}.png",
                "limit": 1,
                "expected_count": 1,
            }
        )

    missing_definition_resources.sort(
        key=lambda row: (
            str(row["options"]["name"]).casefold(),
            str(row["options"]["kind"]),
        )
    )
    missing_leaf_resources.sort(key=lambda row: str(row["patterns"][0]).casefold())
    existing_fx = [fx_id for fx_id in selected_fx if fx_id.casefold() in current_fx_ids]
    missing_fx = [
        fx_id for fx_id in selected_fx if fx_id.casefold() not in current_fx_ids
    ]

    current_family = _mapping(
        current_bindings.get("familyResolution"), "Fords familyResolution"
    )
    runtime_patch = {
        "targetRuntimeDataPath": PARTICLE_BINDINGS_PATH,
        "definitionRegistryAppend": [
            {
                "kind": candidate["kind"],
                "definitionId": candidate["definitionId"],
                "definitionResourceId": candidate["proposedResourceId"],
                "definitionOutputJson": (
                    "effects/particles/definitions/"
                    f"{candidate['proposedResourceId']}.json"
                ),
                "sourceBlockSha256": candidate["sourceSpan"]["sha256"],
                "textureResourceIds": [
                    next(
                        (
                            row["profileResourceIds"][0]
                            if row["profileResourceIds"]
                            else row["proposedResourceId"]
                        )
                        for row in leaf_rows
                        if row["virtualPath"] == path
                    )
                    for path in candidate["textureVirtualPaths"]
                ],
            }
            for row in definition_rows
            for candidate in row["definitionCandidates"]
            if not candidate["profileCoverage"]["fordsParticleBindingRegistryPresent"]
        ],
        "fxListsUpsert": [selected_fx[fx_id] for fx_id in missing_fx],
        "objectBindingsAppend": [
            {
                "objectId": contract["objectId"],
                "enteringStateBindings": deepcopy(
                    contract["effects"]["enteringStateBindings"]
                ),
                "particleAttachments": deepcopy(
                    contract["effects"]["particleAttachments"]
                ),
            }
            for contract in contracts
        ],
        "unresolvedDuplicateIdentifierSystemIdsAppend": [
            system_id
            for system_id in duplicate_ids
            if system_id.casefold()
            not in {
                str(value).casefold()
                for value in current_family.get(
                    "unresolvedDuplicateIdentifierSystemIds", []
                )
            }
        ],
    }

    profile_fragment = {
        "integrationStatus": "proposal-only-not-integrated",
        "resources": [*missing_leaf_resources, *missing_definition_resources],
        "runtimeDataPatch": runtime_patch,
    }
    blockers = [
        {
            "code": "cross-family-precedence-unresolved",
            "particleSystemIds": duplicate_ids,
            "status": "preserve-both-candidates-fail-closed",
            "evidence": deepcopy(current_family),
        },
        {
            "code": "dynamic-emitter-parameter-runtime-unimplemented",
            "status": "definition-fields-sealed-render-semantics-not-proven",
        },
        {
            "code": "attachment-follow-bone-and-lifecycle-runtime-unimplemented",
            "status": "88-bindings-sealed-no-transform-or-start-stop-timing-guessed",
        },
        {
            "code": "fx-list-side-effects-runtime-unimplemented",
            "status": "offset-sound-view-shake-and-section-order-sealed-not-executed",
        },
    ]
    document: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sourceEvidence": {
            "effectiveAssets": manifest_evidence,
            "completeProfile": {
                "id": complete_profile.get("id"),
                "resourceCount": len(resources),
                "sha256": _canonical_sha256(complete_profile),
            },
            "definitionSources": [
                _source_record(path, sources)
                for path in (FX_LIST_PATH, *PARTICLE_PATHS.values())
            ],
        },
        "bindings": {
            "enteringStateFx": fx_bindings,
            "particleAttachments": attachments,
            "particleStateBlocks": particle_state_blocks,
        },
        "fxLists": [
            selected_fx[key]
            for key in sorted(selected_fx, key=lambda value: (value.casefold(), value))
        ],
        "particleDefinitions": definition_rows,
        "renderLeaves": leaf_rows,
        "profileCoverage": {
            "existingFxListIds": existing_fx,
            "missingFxListIds": missing_fx,
            "missingDefinitionResourceCount": len(missing_definition_resources),
            "missingTextureResourceCount": len(missing_leaf_resources),
            "missingW3dResourceCount": sum(
                leaf["kind"] == "w3d" and not leaf["profileResourceIds"]
                for leaf in leaf_rows
            ),
            "resourceDeltaCount": len(profile_fragment["resources"]),
        },
        "profileFragmentProposal": profile_fragment,
        "blockers": blockers,
        "summary": {
            "menObjectCount": len(contracts),
            "enteringStateFxBindingCount": len(fx_bindings),
            "uniqueFxListCount": len(selected_fx),
            "particleAttachmentCount": len(attachments),
            "particleBearingStateBlockCount": state_block_count,
            "directParticleSystemIdCount": len(direct_system_ids),
            "fxListParticleSystemIdCount": len(fx_system_ids),
            "uniqueParticleSystemIdCount": len(system_ids),
            "particleDefinitionCandidateCount": sum(
                len(row["definitionCandidates"]) for row in definition_rows
            ),
            "duplicateFamilySystemCount": len(duplicate_ids),
            "textureLeafCount": sum(leaf["kind"] == "texture" for leaf in leaf_rows),
            "w3dLeafCount": sum(leaf["kind"] == "w3d" for leaf in leaf_rows),
            "allBindingsAccounted": True,
        },
    }
    document["aggregateSha256"] = _canonical_sha256(document)
    return document


def write_contract(path: Path | str, document: Mapping[str, Any]) -> None:
    if (
        document.get("schema") != SCHEMA
        or document.get("schemaVersion") != SCHEMA_VERSION
    ):
        raise ValueError("cannot write unsupported Men damage-effects contract")
    payload = dict(document)
    declared = payload.pop("aggregateSha256", None)
    if declared != _canonical_sha256(payload):
        raise ValueError("Men damage-effects contract aggregate mismatch")
    write_json_atomic(Path(path), deepcopy(dict(document)))


def profile_fragment_document(document: Mapping[str, Any]) -> dict[str, Any]:
    """Extract a separately sealed, proposal-only profile fragment."""

    if (
        document.get("schema") != SCHEMA
        or document.get("schemaVersion") != SCHEMA_VERSION
    ):
        raise ValueError("unsupported Men damage-effects contract")
    proposal: dict[str, Any] = {
        "schema": FRAGMENT_SCHEMA,
        "schemaVersion": FRAGMENT_SCHEMA_VERSION,
        "sourceContractAggregateSha256": document.get("aggregateSha256"),
        **deepcopy(
            _mapping(document.get("profileFragmentProposal"), "profile fragment")
        ),
    }
    proposal["aggregateSha256"] = _canonical_sha256(proposal)
    return proposal


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("effective_assets_root", type=Path)
    parser.add_argument("complete_profile", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile-fragment-output", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    root = _safe_root(args.effective_assets_root)
    profile = _load_json(args.complete_profile, "complete profile")
    manifest = _load_json(
        root / ".openbfme" / "manifest.json", "effective-assets manifest"
    )
    document = build_contract(root, profile, manifest)
    write_contract(args.output, document)
    fragment_output = None
    if args.profile_fragment_output is not None:
        fragment = profile_fragment_document(document)
        write_json_atomic(args.profile_fragment_output, fragment)
        fragment_output = str(args.profile_fragment_output)
    print(
        json.dumps(
            {
                "ok": True,
                "output": str(args.output),
                "profileFragmentOutput": fragment_output,
                "summary": document["summary"],
                "aggregateSha256": document["aggregateSha256"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "build_contract",
    "main",
    "parse_fx_lists",
    "profile_fragment_document",
    "write_contract",
]
