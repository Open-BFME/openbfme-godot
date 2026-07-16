"""Upgrade the five Men structure presentations from sealed retail evidence.

This module does not discover new retail content.  It validates the sealed
Men lifecycle report, proves that every referenced GLB is owned by an existing
profile resource whose W3D inputs occur in that report, and rewrites only the
five Men runtime objects to the schema-version 1 presentation shape.
"""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
import re
from typing import Any, Mapping, Sequence

from .paths import safe_relative_parts


REPORT_SCHEMA = "openbfme.retail-men-building-lifecycle"
REPORT_SCHEMA_VERSION = 0
REPORT_AGGREGATE_SHA256 = (
    "3a2f8d15ebfbf1b058a9b7efc7d1a386e9b8994964eb78fd81f56f286ba9d321"
)

EXPECTED_SUMMARY = {
    "boundAssetByteCount": 32503119,
    "boundAssetCount": 152,
    "embeddedTextureBindingCount": 757,
    "fortressHandoffUnresolvedCount": 1,
    "placementAssignmentCount": 295,
    "resolvedVisualReferenceCount": 242,
    "runtimeModuleCount": 122,
    "runtimeStructureCount": 5,
    "semanticVisualReferenceCount": 21,
    "sourceEvidenceFileCount": 8,
    "sourceObjectCount": 6,
    "unresolvedVisualReferenceCount": 0,
    "visualBindingGroupCount": 159,
    "visualReferenceCount": 263,
    "visualStateCount": 176,
}

MEN_OBJECT_IDS = (
    "bfme2.object.men-fortress",
    "bfme2.object.men-farm",
    "bfme2.object.men-barracks",
    "bfme2.object.men-archery-range",
    "bfme2.object.men-stable",
)

EXPECTED_THRESHOLDS = {
    "bfme2.object.men-fortress": (7500, 2500, 1250),
    "bfme2.object.men-farm": (2000, 1333, 667),
    "bfme2.object.men-barracks": (3000, 2000, 1000),
    "bfme2.object.men-archery-range": (3000, 2000, 1000),
    "bfme2.object.men-stable": (3000, 2000, 1000),
}

EXPECTED_BODY_SYMBOLS = {
    "bfme2.object.men-fortress": (
        "MEN_FORTRESS_HEALTH",
        "MEN_FORTRESS_HEALTH_DAMAGED",
        "MEN_FORTRESS_HEALTH_REALLY_DAMAGED",
    ),
    "bfme2.object.men-farm": (
        "GONDOR_FARM_HEALTH",
        "GONDOR_FARM_HEALTH_DAMAGED",
        "GONDOR_FARM_HEALTH_REALLY_DAMAGED",
    ),
    "bfme2.object.men-barracks": (
        "GONDOR_BARRACKS_HEALTH",
        "GONDOR_BARRACKS_HEALTH_DAMAGED",
        "GONDOR_BARRACKS_HEALTH_REALLY_DAMAGED",
    ),
    "bfme2.object.men-archery-range": (
        "GONDOR_ARCHERYRANGE_HEALTH",
        "GONDOR_ARCHERYRANGE_HEALTH_DAMAGED",
        "GONDOR_ARCHERYRANGE_HEALTH_REALLY_DAMAGED",
    ),
    "bfme2.object.men-stable": (
        "GONDOR_STABLES_HEALTH",
        "GONDOR_STABLES_HEALTH_DAMAGED",
        "GONDOR_STABLES_HEALTH_REALLY_DAMAGED",
    ),
}

EXPECTED_REQUIREMENTS = {
    "distinct-lifecycle-state-block": 176,
    "entering-state-fx": 17,
    "manual-construction-animation-scrub": 12,
    "opaque-sage-draw-script": 20,
    "particle-system-binding": 29,
}

_ANIMATION_SOUND_RE = re.compile(
    r"^Sound:\s+(?P<event>\S+)\s+Animation:(?P<animation>\S+)\s+Frames:\s+(?P<frames>[0-9 ]+)$"
)
_MODEL_CONDITION_SOUND_RE = re.compile(r"^(?P<condition>.+?)\s+Sound:(?P<event>\S+)$")


@dataclass(frozen=True, slots=True)
class MenLifecycleUpgrade:
    profile: dict[str, Any]
    aggregate_sha256: str
    report_aggregate_sha256: str
    structure_count: int
    phase_count: int
    no_render_phase_count: int
    particle_attachment_count: int
    entering_state_fx_binding_count: int
    audio_binding_count: int
    blocker_count: int


def _canonical_sha256(value: Any) -> str:
    payload = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _mapping(value: object, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _array(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a nonempty string")
    return value


def _validate_report(report: Mapping[str, Any]) -> None:
    if (
        report.get("schema") != REPORT_SCHEMA
        or report.get("schemaVersion") != REPORT_SCHEMA_VERSION
        or report.get("summary") != EXPECTED_SUMMARY
    ):
        raise ValueError("Men lifecycle report schema or exact summary changed")
    declared = report.get("aggregateSha256")
    calculated = _canonical_sha256(
        {key: value for key, value in report.items() if key != "aggregateSha256"}
    )
    if declared != calculated or declared != REPORT_AGGREGATE_SHA256:
        raise ValueError("Men lifecycle report aggregate digest changed")
    requirements = {
        _text(item.get("feature"), "runtime requirement feature"): item
        for item in _array(report.get("runtimeRequirements"), "runtime requirements")
        if isinstance(item, dict)
    }
    if set(requirements) != set(EXPECTED_REQUIREMENTS):
        raise ValueError("Men lifecycle runtime requirement closure changed")
    for feature, count in EXPECTED_REQUIREMENTS.items():
        item = requirements[feature]
        if (
            item.get("evidenceCount") != count
            or item.get("handoffStatus")
            != "requires-runtime-implementation-and-render-proof"
        ):
            raise ValueError(f"Men lifecycle requirement changed: {feature}")
    unsupported = _mapping(
        report.get("unresolvedOrUnsupported"), "unresolvedOrUnsupported"
    )
    if set(unsupported) != {
        "opaqueScriptPolicy",
        "visualReferences",
    } or unsupported.get("opaqueScriptPolicy") != (
        "BeginScript statements are source-located and SHA-256 sealed but not "
        "interpreted by this evidence pass."
    ):
        raise ValueError("Men lifecycle unsupported-evidence policy changed")
    visual_references = _mapping(
        unsupported.get("visualReferences"), "unsupported visualReferences"
    )
    if visual_references != {"graphDiagnostics": [], "references": []}:
        raise ValueError("Men lifecycle unresolved visual reference closure changed")
    handoffs = _mapping(report.get("sourceProvenHandoffs"), "sourceProvenHandoffs")
    if set(handoffs) != {"fortressObjectHandoff"}:
        raise ValueError("Men lifecycle source-proven handoff closure changed")
    fortress_handoff = _mapping(
        handoffs.get("fortressObjectHandoff"), "fortressObjectHandoff"
    )
    if (
        fortress_handoff.get("status")
        != "source-proven-template-to-operational-object-link"
        or fortress_handoff.get("controllerObject") != "MenFortress"
        or fortress_handoff.get("operationalObject") != "MenFortressCitadel"
        or fortress_handoff.get("castleTemplateToken") != "Fortress_Men"
    ):
        raise ValueError("Men fortress source-proven handoff changed")


def _report_structures(report: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    structures: dict[str, dict[str, Any]] = {}
    for raw in _array(report.get("structures"), "Men lifecycle structures"):
        structure = _mapping(raw, "Men lifecycle structure")
        object_id = _text(structure.get("projectRuntimeId"), "projectRuntimeId")
        if object_id in structures:
            raise ValueError(f"duplicate Men lifecycle structure: {object_id!r}")
        structures[object_id] = structure
    if set(structures) != set(MEN_OBJECT_IDS):
        raise ValueError("Men lifecycle structure identity closure changed")
    return structures


def _resource_indexes(
    profile: Mapping[str, Any],
) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    by_id: dict[str, dict[str, Any]] = {}
    by_output: dict[str, dict[str, Any]] = {}
    for raw in _array(profile.get("resources"), "profile resources"):
        resource = _mapping(raw, "profile resource")
        resource_id = _text(resource.get("id"), "profile resource id")
        if resource_id in by_id:
            raise ValueError(f"duplicate profile resource id: {resource_id!r}")
        by_id[resource_id] = resource
        output = resource.get("output")
        if isinstance(output, str):
            safe_relative_parts(output)
            if "{" in output or "}" in output:
                continue
            if output in by_output:
                raise ValueError(f"duplicate profile resource output: {output!r}")
            by_output[output] = resource
    return by_id, by_output


def _bound_asset_paths(report: Mapping[str, Any]) -> set[str]:
    paths: set[str] = set()
    for raw in _array(report.get("boundAssetEvidence"), "bound asset evidence"):
        item = _mapping(raw, "bound asset evidence item")
        path = _text(item.get("virtualPath"), "bound asset virtualPath")
        safe_relative_parts(path)
        paths.add(path.casefold())
    if len(paths) != EXPECTED_SUMMARY["boundAssetCount"]:
        raise ValueError("Men lifecycle bound asset identity closure changed")
    for raw_structure in _array(report.get("structures"), "Men lifecycle structures"):
        structure = _mapping(raw_structure, "Men lifecycle structure")
        for raw_binding in _array(structure.get("visualBindings"), "visual bindings"):
            binding = _mapping(raw_binding, "visual binding")
            for raw_reference in _array(binding.get("references"), "visual references"):
                reference = _mapping(raw_reference, "visual reference")
                identifier = reference.get("identifier")
                if isinstance(identifier, str):
                    paths.update(
                        f"@{part.casefold()}" for part in identifier.split(".")
                    )
    return paths


def _resource_visual(
    path: str,
    by_output: Mapping[str, Mapping[str, Any]],
    bound_paths: set[str],
) -> dict[str, str]:
    safe_relative_parts(path)
    resource = by_output.get(path)
    if resource is None:
        raise ValueError(f"Men lifecycle GLB has no owning resource: {path!r}")
    resource_id = _text(resource.get("id"), f"resource for {path!r} id")
    patterns = [
        _text(value, f"resource {resource_id!r} pattern")
        for value in _array(
            resource.get("patterns"), f"resource {resource_id!r} patterns"
        )
    ]
    if not patterns or any(
        value.casefold() not in bound_paths
        and f"@{value.rsplit('/', 1)[-1].rsplit('.', 1)[0].casefold()}"
        not in bound_paths
        for value in patterns
    ):
        raise ValueError(
            f"Men lifecycle resource is outside sealed bound assets: {resource_id!r}"
        )
    return {"mode": "glb", "glb": path, "modelResourceId": resource_id}


def _assignments(module_or_state: Mapping[str, Any]) -> list[dict[str, Any]]:
    return [
        _mapping(value, "source assignment")
        for value in _array(module_or_state.get("assignments"), "source assignments")
    ]


def _source_objects(structure: Mapping[str, Any]) -> list[dict[str, Any]]:
    return [
        _mapping(value, "source object evidence")
        for value in _array(structure.get("sourceObjectEvidence"), "source objects")
    ]


def _body_symbols(structure: Mapping[str, Any]) -> tuple[str, str, str]:
    found: list[tuple[str, str, str]] = []
    for source_object in _source_objects(structure):
        for raw_module in _array(
            source_object.get("runtimeModules"), "runtime modules"
        ):
            module = _mapping(raw_module, "runtime module")
            if module.get("moduleKind") not in {"StructureBody", "ActiveBody"}:
                continue
            values: dict[str, str] = {}
            for assignment in _assignments(module):
                key = assignment.get("key")
                if key in {
                    "MaxHealth",
                    "MaxHealthDamaged",
                    "MaxHealthReallyDamaged",
                }:
                    values[str(key)] = _text(
                        assignment.get("rawValue"), f"{key} source value"
                    )
            if set(values) == {
                "MaxHealth",
                "MaxHealthDamaged",
                "MaxHealthReallyDamaged",
            }:
                found.append(
                    (
                        values["MaxHealth"],
                        values["MaxHealthDamaged"],
                        values["MaxHealthReallyDamaged"],
                    )
                )
    unique = list(dict.fromkeys(found))
    if len(unique) != 1:
        raise ValueError("Men lifecycle body threshold source is not unique")
    return unique[0]


def _phase_condition_sets(structure: Mapping[str, Any], phase: str) -> list[list[str]]:
    sets: set[tuple[str, ...]] = set()
    for raw in _array(structure.get("visualBindings"), "visual bindings"):
        binding = _mapping(raw, "visual binding")
        phases = _array(binding.get("lifecyclePhases"), "binding lifecyclePhases")
        if phase not in phases:
            continue
        conditions = tuple(
            _text(value, "source condition")
            for value in _array(binding.get("conditions"), "binding conditions")
        )
        condition_set = tuple(
            value
            for value in conditions
            if value
            not in {
                "SNOW",
                "WORLD_BUILDER",
                "BUILD_PLACEMENT_CURSOR",
                "PHANTOM_STRUCTURE",
            }
            and not value.startswith("UPGRADE_")
            and not value.startswith("DOOR_")
            and not value.startswith("USER_")
        )
        sets.add(condition_set)
    return [list(value) for value in sorted(sets, key=lambda item: (len(item), item))]


def _visual_state_records(structure: Mapping[str, Any]) -> list[dict[str, Any]]:
    states: list[dict[str, Any]] = []
    for source_object in _source_objects(structure):
        object_name = _text(source_object.get("name"), "source object name")
        for raw_state in _array(source_object.get("visualStates"), "visual states"):
            state = deepcopy(_mapping(raw_state, "visual state"))
            state["sourceObject"] = object_name
            states.append(state)
    return states


def _particle_attachments(states: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    attachments: list[dict[str, Any]] = []
    seen: set[tuple[str, str, tuple[str, ...], tuple[str, ...], str]] = set()
    for state in states:
        conditions = tuple(
            _text(value, "particle source condition")
            for value in _array(state.get("conditions"), "particle conditions")
        )
        source_object = _text(state.get("sourceObject"), "particle source object")
        for assignment in _assignments(state):
            if assignment.get("key") != "ParticleSysBone":
                continue
            raw_value = _text(assignment.get("rawValue"), "ParticleSysBone value")
            parts = raw_value.split()
            if len(parts) < 2:
                raise ValueError(f"invalid ParticleSysBone value: {raw_value!r}")
            bone, particle_id, *options = parts
            key = (bone, particle_id, tuple(options), conditions, source_object)
            if key in seen:
                continue
            seen.add(key)
            attachments.append(
                {
                    "bone": bone,
                    "options": options,
                    "particleSystemId": particle_id,
                    "sourceConditions": list(conditions),
                    "sourceObject": source_object,
                }
            )
    return attachments


def _entering_state_fx(
    states: Sequence[Mapping[str, Any]],
) -> tuple[dict[str, str], list[dict[str, Any]]]:
    lifecycle: dict[str, str] = {}
    records: list[dict[str, Any]] = []
    for state in states:
        conditions = [
            _text(value, "FX source condition")
            for value in _array(state.get("conditions"), "FX conditions")
        ]
        for assignment in _assignments(state):
            if assignment.get("key") != "EnteringStateFX":
                continue
            fx_id = _text(assignment.get("rawValue"), "EnteringStateFX value")
            records.append(
                {
                    "fxListId": fx_id,
                    "sourceConditions": conditions,
                    "sourceObject": _text(
                        state.get("sourceObject"), "FX source object"
                    ),
                }
            )
            base_conditions = [
                value
                for value in conditions
                if not value.startswith("UPGRADE_") and not value.startswith("USER_")
            ]
            if base_conditions == ["DAMAGED"]:
                lifecycle["damaged"] = fx_id
            elif base_conditions == ["REALLYDAMAGED"]:
                lifecycle["really-damaged"] = fx_id
            elif base_conditions == ["RUBBLE"]:
                lifecycle["collapsing"] = fx_id
    return lifecycle, records


def _runtime_modules(structure: Mapping[str, Any]) -> list[dict[str, Any]]:
    modules: list[dict[str, Any]] = []
    for source_object in _source_objects(structure):
        object_name = _text(source_object.get("name"), "runtime source object")
        for raw in _array(source_object.get("runtimeModules"), "runtime modules"):
            module = deepcopy(_mapping(raw, "runtime module"))
            module["sourceObject"] = object_name
            modules.append(module)
    return modules


def _collapse_contract(modules: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    candidates: list[dict[str, Any]] = []
    for module in modules:
        if module.get("moduleKind") != "StructureCollapseUpdate":
            continue
        values: dict[str, Any] = {}
        fx: dict[str, str] = {}
        for assignment in _assignments(module):
            key = assignment.get("key")
            raw = _text(assignment.get("rawValue"), "collapse assignment value")
            if key == "FXList":
                parts = raw.split()
                if len(parts) != 2:
                    raise ValueError(f"invalid collapse FXList: {raw!r}")
                fx[parts[0].casefold().replace("_", "-")] = parts[1]
            elif key in {
                "MinCollapseDelay",
                "MaxCollapseDelay",
                "MinBurstDelay",
                "MaxBurstDelay",
                "BigBurstFrequency",
                "CollapseHeight",
            }:
                values[key] = int(raw)
            elif key in {"CollapseDamping", "MaxShudder"}:
                values[key] = float(raw)
            elif key == "DestroyObjectWhenDone":
                if raw not in {"Yes", "No"}:
                    raise ValueError("invalid DestroyObjectWhenDone value")
                values[key] = raw == "Yes"
        required = {
            "MinCollapseDelay",
            "MaxCollapseDelay",
            "CollapseDamping",
            "MaxShudder",
            "MinBurstDelay",
            "MaxBurstDelay",
            "BigBurstFrequency",
            "DestroyObjectWhenDone",
            "CollapseHeight",
        }
        if set(values) != required or not fx:
            raise ValueError("incomplete StructureCollapseUpdate evidence")
        candidates.append(
            {
                "module": "StructureCollapseUpdate",
                "sourceObject": _text(
                    module.get("sourceObject"), "collapse source object"
                ),
                "minCollapseDelayMilliseconds": values["MinCollapseDelay"],
                "maxCollapseDelayMilliseconds": values["MaxCollapseDelay"],
                "collapseDamping": values["CollapseDamping"],
                "maxShudder": values["MaxShudder"],
                "minBurstDelayMilliseconds": values["MinBurstDelay"],
                "maxBurstDelayMilliseconds": values["MaxBurstDelay"],
                "bigBurstFrequency": values["BigBurstFrequency"],
                "destroyObjectWhenDone": values["DestroyObjectWhenDone"],
                "collapseHeight": values["CollapseHeight"],
                "fxLists": fx,
                "exactTotalTimingStatus": "blocked-on-bfme2-runtime-oracle",
            }
        )
    unique_payloads = {
        _canonical_sha256({k: v for k, v in value.items() if k != "sourceObject"})
        for value in candidates
    }
    if not candidates or len(unique_payloads) != 1:
        raise ValueError("StructureCollapseUpdate evidence is absent or contradictory")
    return candidates[-1]


def _audio_bindings(
    modules: Sequence[Mapping[str, Any]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    summary: dict[str, Any] = {"collapse": None, "construction": None}
    bindings: list[dict[str, Any]] = []
    for module in modules:
        kind = module.get("moduleKind")
        source_object = _text(module.get("sourceObject"), "audio source object")
        for assignment in _assignments(module):
            key = assignment.get("key")
            raw = _text(assignment.get("rawValue"), "audio assignment value")
            if (
                kind == "ModelConditionAudioLoopClientBehavior"
                and key == "ModelCondition"
            ):
                match = _MODEL_CONDITION_SOUND_RE.fullmatch(raw)
                if match is None:
                    raise ValueError(f"invalid ModelCondition audio value: {raw!r}")
                event = match.group("event")
                bindings.append(
                    {
                        "eventId": event,
                        "kind": "model-condition-loop",
                        "sourceConditionExpression": match.group("condition"),
                        "sourceObject": source_object,
                    }
                )
                if "RUBBLE" in match.group("condition"):
                    summary["collapse"] = event
            elif kind == "CastleMemberBehavior" and key == "BeingBuiltSound":
                bindings.append(
                    {
                        "eventId": raw,
                        "kind": "construction-loop",
                        "sourceObject": source_object,
                    }
                )
                summary["construction"] = raw
            elif kind == "AnimationSoundClientBehavior" and key == "AnimationSound":
                match = _ANIMATION_SOUND_RE.fullmatch(raw)
                if match is None:
                    raise ValueError(f"invalid AnimationSound value: {raw!r}")
                bindings.append(
                    {
                        "animation": match.group("animation"),
                        "eventId": match.group("event"),
                        "frames": [
                            int(value) for value in match.group("frames").split()
                        ],
                        "kind": "animation-frame",
                        "sourceObject": source_object,
                    }
                )
    return summary, bindings


def _requirement_counts(structure: Mapping[str, Any]) -> dict[str, int]:
    states = _visual_state_records(structure)
    return {
        "distinct-lifecycle-state-block": len(states),
        "entering-state-fx": sum(
            any(item.get("key") == "EnteringStateFX" for item in _assignments(state))
            for state in states
        ),
        "manual-construction-animation-scrub": sum(
            any(
                item.get("key") == "AnimationMode"
                and str(item.get("rawValue")).upper() == "MANUAL"
                for item in _assignments(state)
            )
            for state in states
        ),
        "opaque-sage-draw-script": sum(
            len(_array(state.get("opaqueScripts"), "opaque scripts"))
            for state in states
        ),
        "particle-system-binding": sum(
            any(item.get("key") == "ParticleSysBone" for item in _assignments(state))
            for state in states
        ),
    }


def _phase(
    *,
    name: str,
    visual: Mapping[str, Any],
    animation: Mapping[str, Any],
    next_phase: str | None,
    condition_sets: Sequence[Sequence[str]],
) -> dict[str, Any]:
    return {
        "phase": name,
        "sourceConditionSets": [list(value) for value in condition_sets],
        "transitionAuthority": "deterministic-simulation",
        "visual": deepcopy(dict(visual)),
        "animation": deepcopy(dict(animation)),
        "nextPhase": next_phase,
    }


def _animation(old: Mapping[str, Any], key: str, *, phase: str) -> dict[str, Any]:
    record = _mapping(old.get(key), f"v0 {key} clip")
    mode = _text(record.get("mode"), f"v0 {key} clip mode")
    names = [
        _text(value, f"v0 {key} clip name")
        for value in _array(record.get("names"), f"v0 {key} clip names")
    ]
    if phase == "construction" and (mode != "manual-progress" or len(names) != 1):
        raise ValueError("Men construction clip contract changed")
    if phase in {"really-damaged", "collapsing"} and (
        mode != "once" or len(names) != 1
    ):
        raise ValueError(f"Men {phase} clip contract changed")
    if not names:
        return {"clip": None, "mode": "none"}
    result: dict[str, Any] = {"clip": names[0], "mode": mode}
    if len(names) > 1:
        result["alternateClips"] = names[1:]
    return result


def _texture_resource_ids(
    visuals: Sequence[Mapping[str, Any]],
    by_id: Mapping[str, Mapping[str, Any]],
) -> list[str]:
    dependencies: set[str] = set()
    for visual in visuals:
        resource_id = _text(visual.get("modelResourceId"), "model resource id")
        resource = _mapping(by_id.get(resource_id), f"model resource {resource_id!r}")
        options = _mapping(
            resource.get("options"), f"model resource {resource_id!r} options"
        )
        for raw in _array(options.get("inputResourceIds"), "inputResourceIds"):
            dependency_id = _text(raw, "inputResourceId")
            dependency = _mapping(
                by_id.get(dependency_id), f"dependency {dependency_id!r}"
            )
            if (
                dependency.get("kind") == "texture"
                or dependency.get("converter") == "texture"
            ):
                dependencies.add(dependency_id)
    return sorted(dependencies)


def _upgrade_one(
    runtime_object: Mapping[str, Any],
    structure: Mapping[str, Any],
    by_id: Mapping[str, Mapping[str, Any]],
    by_output: Mapping[str, Mapping[str, Any]],
    bound_paths: set[str],
    requirements: Mapping[str, Mapping[str, Any]],
    opaque_script_policy: str,
    fortress_handoff: Mapping[str, Any],
) -> tuple[dict[str, Any], dict[str, int]]:
    result = deepcopy(dict(runtime_object))
    object_id = _text(result.get("id"), "Men runtime object id")
    if (
        object_id == "bfme2.object.men-fortress"
        and structure.get("authoredHandoff") != fortress_handoff
    ):
        raise ValueError("Men fortress structure and global handoff evidence disagree")
    presentation = _mapping(result.get("presentation"), f"{object_id} presentation")
    old = _mapping(presentation.get("buildingLifecycle"), f"{object_id} lifecycle")
    if (
        old.get("schema") != "openbfme.building-lifecycle-presentation"
        or old.get("schemaVersion") != 0
    ):
        raise ValueError(f"{object_id} is not the sealed v0 lifecycle contract")
    thresholds = (
        old.get("maxHealth"),
        old.get("damagedHealth"),
        old.get("reallyDamagedHealth"),
    )
    if thresholds != EXPECTED_THRESHOLDS[object_id]:
        raise ValueError(f"{object_id} numeric threshold contract changed")
    symbols = _body_symbols(structure)
    if symbols != EXPECTED_BODY_SYMBOLS[object_id]:
        raise ValueError(f"{object_id} source threshold symbols changed")

    paths = _mapping(old.get("paths"), f"{object_id} lifecycle paths")
    expected_path_keys = {
        "construction",
        "intact",
        "damaged",
        "reallyDamaged",
        "rubble",
        "bib",
    }
    if set(paths) != expected_path_keys:
        raise ValueError(f"{object_id} lifecycle path closure changed")
    visuals = {
        key: _resource_visual(
            _text(paths.get(key), f"{object_id} {key} path"), by_output, bound_paths
        )
        for key in expected_path_keys
    }
    clips = _mapping(old.get("clips"), f"{object_id} lifecycle clips")
    construction_animation = _animation(clips, "construction", phase="construction")
    intact_animation = _animation(clips, "intact", phase="intact")
    really_animation = _animation(clips, "reallyDamaged", phase="really-damaged")
    collapse_animation = _animation(clips, "rubble", phase="collapsing")

    construction_conditions = _phase_condition_sets(structure, "construction")
    construction_conditions = [
        value
        for value in construction_conditions
        if value
        in (
            ["AWAITING_CONSTRUCTION"],
            ["ACTIVELY_BEING_CONSTRUCTED"],
            ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
        )
    ]
    if not construction_conditions:
        raise ValueError(f"{object_id} lacks exact construction conditions")

    states = _visual_state_records(structure)
    particles = _particle_attachments(states)
    entering_fx, entering_records = _entering_state_fx(states)
    modules = _runtime_modules(structure)
    collapse = _collapse_contract(modules)
    audio_events, audio_bindings = _audio_bindings(modules)
    counts = _requirement_counts(structure)

    no_render = {"mode": "no-render", "sourceIdentifier": "None"}
    if object_id == "bfme2.object.men-fortress":
        no_render = {
            "mode": "no-render",
            "sourceIdentifier": "DestroyObjectWhenDone",
        }
    phases = [
        _phase(
            name="construction",
            visual=visuals["construction"],
            animation=construction_animation,
            next_phase="intact",
            condition_sets=construction_conditions,
        ),
        _phase(
            name="intact",
            visual=visuals["intact"],
            animation=intact_animation,
            next_phase="damaged",
            condition_sets=[[]],
        ),
        _phase(
            name="damaged",
            visual=visuals["damaged"],
            animation={"clip": None, "mode": "none"},
            next_phase="really-damaged",
            condition_sets=[["DAMAGED"]],
        ),
        _phase(
            name="really-damaged",
            visual=visuals["reallyDamaged"],
            animation=really_animation,
            next_phase="collapsing",
            condition_sets=[["REALLYDAMAGED"]],
        ),
        _phase(
            name="collapsing",
            visual=visuals["rubble"],
            animation=collapse_animation,
            next_phase="rubble",
            condition_sets=[["COLLAPSING"]],
        ),
        _phase(
            name="rubble",
            visual=visuals["rubble"],
            animation={"clip": None, "mode": "none"},
            next_phase="post-rubble",
            condition_sets=[["RUBBLE"]],
        ),
        _phase(
            name="post-rubble",
            visual=no_render,
            animation={"clip": None, "mode": "none"},
            next_phase=None,
            condition_sets=[["POST_RUBBLE"]],
        ),
        _phase(
            name="post-collapse",
            visual=no_render,
            animation={"clip": None, "mode": "none"},
            next_phase=None,
            condition_sets=[["POST_COLLAPSE"]],
        ),
    ]

    requirement_entries = []
    for feature in EXPECTED_REQUIREMENTS:
        requirement = requirements[feature]
        requirement_entries.append(
            {
                "feature": feature,
                "evidenceCount": counts[feature],
                "handoffStatus": requirement["handoffStatus"],
            }
        )
    blockers = [
        {
            "code": feature,
            "evidenceCount": counts[feature],
            "status": requirements[feature]["handoffStatus"],
        }
        for feature in EXPECTED_REQUIREMENTS
        if counts[feature]
    ]
    blockers.extend(
        [
            {
                "code": "damage-threshold-boundary-oracle-pending",
                "status": "blocked-on-bfme2-executable-oracle",
            },
            {
                "code": "collapse-total-timing-oracle-pending",
                "status": collapse["exactTotalTimingStatus"],
            },
            {
                "code": "audio-definition-runtime-closure-pending",
                "authoredBindingCount": len(audio_bindings),
                "status": "ids-proven-definitions-and-mixing-not-attached",
            },
        ]
    )

    lifecycle: dict[str, Any] = {
        "schema": "openbfme.building-lifecycle-presentation",
        "schemaVersion": 1,
        "objectId": object_id,
        "initialPhase": "intact",
        "phases": phases,
        "bib": {
            "drawModule": "W3DFloorDraw",
            "duringConstruction": bool(old.get("bibDuringConstruction")),
            "hideIfModelConditions": [
                "AWAITING_CONSTRUCTION",
                "PARTIALLY_CONSTRUCTED",
            ]
            if old.get("bibDuringConstruction") is False
            else [],
            "sourceConditions": [],
            "startHiddenAuthored": False,
            "visibility": "condition-driven-authored-floor-draw",
            "visual": visuals["bib"],
        },
        "components": deepcopy(old.get("components", {})),
        "normalWeatherModelTextureResourceIds": _texture_resource_ids(
            list(visuals.values()), by_id
        ),
        "effects": {
            "collapseUpdateFx": deepcopy(collapse["fxLists"]),
            "definitionTranslationStatus": "requires-exact-definition-runtime-binding",
            "enteringStateFx": entering_fx,
            "enteringStateBindings": entering_records,
            "particleAttachments": particles,
        },
        "audioEvents": audio_events,
        "audioBindings": audio_bindings,
        "simulationFacts": {
            "maximumHealth": thresholds[0],
            "damageStateRule": {
                "damagedThreshold": thresholds[1],
                "reallyDamagedThreshold": thresholds[2],
                "rubbleThreshold": 0,
                "sourceSymbols": {
                    "maximumHealth": symbols[0],
                    "damagedThreshold": symbols[1],
                    "reallyDamagedThreshold": symbols[2],
                },
                "boundaryStatus": (
                    "numeric values retained from the sealed v0 simulation contract; "
                    "inclusive BFME2 boundary behavior still requires executable-oracle proof"
                ),
            },
            "collapse": collapse,
        },
        "runtimeRequirements": requirement_entries,
        "unsupportedEvidencePolicy": opaque_script_policy,
        "sourceHandoff": (
            deepcopy(dict(fortress_handoff))
            if object_id == "bfme2.object.men-fortress"
            else None
        ),
        "blockers": blockers,
        "unresolved": deepcopy(_array(old.get("unresolved"), "v0 unresolved")),
    }
    presentation["buildingLifecycle"] = lifecycle
    presentation["model"] = visuals["intact"]["glb"]
    return result, {
        "phaseCount": len(phases),
        "noRenderPhaseCount": sum(
            phase["visual"]["mode"] == "no-render" for phase in phases
        ),
        "particleAttachmentCount": len(particles),
        "enteringStateFxBindingCount": len(entering_records),
        "audioBindingCount": len(audio_bindings),
        "blockerCount": len(blockers),
    }


def upgrade_men_building_lifecycles(
    profile: Mapping[str, Any], report: Mapping[str, Any]
) -> MenLifecycleUpgrade:
    """Return a copy with exactly five source-proven schema-v1 lifecycles."""

    _validate_report(report)
    report_structures = _report_structures(report)
    by_id, by_output = _resource_indexes(profile)
    bound_paths = _bound_asset_paths(report)
    requirements = {
        _text(item.get("feature"), "runtime requirement feature"): item
        for item in _array(report.get("runtimeRequirements"), "runtime requirements")
        if isinstance(item, dict)
    }
    unsupported = _mapping(
        report.get("unresolvedOrUnsupported"), "unresolvedOrUnsupported"
    )
    opaque_script_policy = _text(
        unsupported.get("opaqueScriptPolicy"), "opaqueScriptPolicy"
    )
    fortress_handoff = _mapping(
        _mapping(report.get("sourceProvenHandoffs"), "sourceProvenHandoffs").get(
            "fortressObjectHandoff"
        ),
        "fortressObjectHandoff",
    )

    result = deepcopy(dict(profile))
    runtime_data = _mapping(result.get("runtime_data"), "profile runtime_data")
    objects_document = _mapping(
        runtime_data.get("data/objects.json"), "profile objects document"
    )
    objects = _array(objects_document.get("objects"), "profile runtime objects")
    original_non_men = [
        deepcopy(value)
        for value in objects
        if not isinstance(value, dict) or value.get("id") not in MEN_OBJECT_IDS
    ]
    found: set[str] = set()
    totals = {
        "phaseCount": 0,
        "noRenderPhaseCount": 0,
        "particleAttachmentCount": 0,
        "enteringStateFxBindingCount": 0,
        "audioBindingCount": 0,
        "blockerCount": 0,
    }
    upgraded_objects: list[Any] = []
    requirement_totals = {feature: 0 for feature in EXPECTED_REQUIREMENTS}
    for raw in objects:
        if not isinstance(raw, dict) or raw.get("id") not in MEN_OBJECT_IDS:
            upgraded_objects.append(raw)
            continue
        object_id = _text(raw.get("id"), "Men runtime object id")
        if object_id in found:
            raise ValueError(f"duplicate Men runtime object: {object_id!r}")
        found.add(object_id)
        structure = report_structures[object_id]
        upgraded, counts = _upgrade_one(
            raw,
            structure,
            by_id,
            by_output,
            bound_paths,
            requirements,
            opaque_script_policy,
            fortress_handoff,
        )
        upgraded_objects.append(upgraded)
        for key in totals:
            totals[key] += counts[key]
        for feature, count in _requirement_counts(structure).items():
            requirement_totals[feature] += count
    if found != set(MEN_OBJECT_IDS):
        raise ValueError("profile Men runtime object closure changed")
    if requirement_totals != EXPECTED_REQUIREMENTS:
        raise ValueError(
            "per-structure runtime requirement counts do not reproduce the sealed report"
        )
    objects_document["objects"] = upgraded_objects
    resulting_non_men = [
        deepcopy(value)
        for value in upgraded_objects
        if not isinstance(value, dict) or value.get("id") not in MEN_OBJECT_IDS
    ]
    if original_non_men != resulting_non_men:
        raise RuntimeError("Men lifecycle upgrade changed a non-Men runtime object")

    contracts = {
        object_id: next(
            value["presentation"]["buildingLifecycle"]
            for value in upgraded_objects
            if isinstance(value, dict) and value.get("id") == object_id
        )
        for object_id in MEN_OBJECT_IDS
    }
    aggregate = _canonical_sha256(contracts)
    return MenLifecycleUpgrade(
        profile=result,
        aggregate_sha256=aggregate,
        report_aggregate_sha256=REPORT_AGGREGATE_SHA256,
        structure_count=len(contracts),
        phase_count=totals["phaseCount"],
        no_render_phase_count=totals["noRenderPhaseCount"],
        particle_attachment_count=totals["particleAttachmentCount"],
        entering_state_fx_binding_count=totals["enteringStateFxBindingCount"],
        audio_binding_count=totals["audioBindingCount"],
        blocker_count=totals["blockerCount"],
    )


__all__ = [
    "MEN_OBJECT_IDS",
    "MenLifecycleUpgrade",
    "REPORT_AGGREGATE_SHA256",
    "upgrade_men_building_lifecycles",
]
