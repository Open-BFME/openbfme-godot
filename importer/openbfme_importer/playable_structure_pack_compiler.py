"""Closure-driven playable-structure conversion recipe compiler.

Structures convert from the same retail visual closure as playable units, but
their pack recipes are keyed by building lifecycle phase instead of unit core
animation states.  This module owns only the source-backed visual recipe; the
descriptor-bound registration wrapper joins it once the structure descriptor
lane exists.
"""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy
from pathlib import PurePosixPath

from .playable_unit_pack_compiler import (
    _digest,
    _paths,
    _resource_id,
    _rows,
    _slug,
    _validate_dependency_closure,
)
from .profile import MAX_PATTERNS_PER_RESOURCE


SCHEMA = "openbfme.playable-structure-pack-recipe"
SCHEMA_VERSION = 0
LIFECYCLE_PHASE_ORDER = (
    "construction",
    "intact",
    "damaged",
    "really-damaged",
    "rubble",
    "post-rubble",
)


class PlayableStructurePackCompilerError(ValueError):
    """A source-backed structure cannot produce one bounded pack recipe."""


def _closure_identity(visual_closure: Mapping[str, object]) -> str:
    if (
        visual_closure.get("schema") != "openbfme.retail-visual-closure"
        or visual_closure.get("schemaVersion") != 1
    ):
        raise PlayableStructurePackCompilerError("visual closure identity is invalid")
    unsigned = dict(visual_closure)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructurePackCompilerError("visual closure digest is invalid")
    return digest


def _scanned_index(
    visual_closure: Mapping[str, object],
) -> dict[str, Mapping[str, object]]:
    scanned = _rows(visual_closure.get("scannedW3d"), "scanned W3D")
    _validate_dependency_closure(visual_closure, scanned)
    result: dict[str, Mapping[str, object]] = {}
    for row in scanned:
        path = str(row.get("virtualPath", ""))
        if not path or path.casefold() in result:
            raise PlayableStructurePackCompilerError("scanned W3D index is invalid")
        result[path.casefold()] = row
    return result


def _header_ids(row: Mapping[str, object], field: str) -> tuple[str, ...]:
    headers = row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableStructurePackCompilerError("scanned W3D header ids are missing")
    values = headers.get(field, [])
    if not isinstance(values, list) or any(
        not isinstance(value, str) for value in values
    ):
        raise PlayableStructurePackCompilerError(f"scanned W3D {field} are invalid")
    return tuple(values)


def _animation_hierarchies(row: Mapping[str, object], path: str) -> frozenset[str]:
    prefixes: set[str] = set()
    for identifier in _header_ids(row, "animationIds"):
        if "." not in identifier:
            continue
        prefixes.add(identifier.split(".", 1)[0].casefold())
    if not prefixes:
        raise PlayableStructurePackCompilerError(
            f"animation W3D declares no hierarchy header: {path}"
        )
    return frozenset(prefixes)


def _phase_rows(
    visual_closure: Mapping[str, object], target_object_id: str
) -> tuple[dict[str, set[str]], dict[str, set[str]], list[dict[str, object]]]:
    target_key = target_object_id.casefold()
    model_phases: dict[str, set[str]] = {}
    animation_phases: dict[str, set[str]] = {}
    exclusions: list[dict[str, object]] = []
    for row in _rows(visual_closure.get("exactLeaves"), "exact visual leaves"):
        if str(row.get("targetObject", "")).casefold() != target_key:
            continue
        kind = str(row.get("kind", "")).casefold()
        if kind not in {"model", "animation"}:
            continue
        conditions = row.get("conditions", [])
        condition_keys = (
            {str(value).casefold() for value in conditions}
            if isinstance(conditions, list)
            else set()
        )
        raw_phases = row.get("lifecyclePhases", [])
        if not isinstance(raw_phases, list) or any(
            not isinstance(value, str) for value in raw_phases
        ):
            raise PlayableStructurePackCompilerError(
                f"visual leaf lifecycle phases are invalid: {row.get('identifier')}"
            )
        unknown = [
            value for value in raw_phases if value not in LIFECYCLE_PHASE_ORDER
        ]
        if unknown:
            raise PlayableStructurePackCompilerError(
                "visual leaf declares an unknown lifecycle phase: "
                + ", ".join(sorted(unknown))
            )
        paths = _paths(row, f"structure {kind} leaf")
        if "world_builder" in condition_keys:
            for path in paths:
                exclusions.append(
                    {
                        "kind": kind,
                        "sourceW3d": path,
                        "reason": "editor-only-model",
                    }
                )
            continue
        destination = model_phases if kind == "model" else animation_phases
        for path in paths:
            destination.setdefault(path, set()).update(raw_phases)
    return model_phases, animation_phases, exclusions


def _hierarchy_providers(
    prefixes: frozenset[str], scanned: Mapping[str, Mapping[str, object]]
) -> dict[str, tuple[str, ...]]:
    providers: dict[str, list[str]] = {prefix: [] for prefix in prefixes}
    for row in scanned.values():
        authored = {
            value.casefold() for value in _header_ids(row, "hierarchyIds")
        }
        for prefix in prefixes & authored:
            providers[prefix].append(str(row["virtualPath"]))
    return {
        prefix: tuple(sorted(paths, key=lambda item: (item.casefold(), item)))
        for prefix, paths in providers.items()
    }


def _phase_slug(phases: tuple[str, ...]) -> str:
    if not phases:
        return "unphased"
    return "-".join(_slug(value) for value in phases)


def compile_structure_visual_recipe(
    target_object_id: str, visual_closure: Mapping[str, object]
) -> dict[str, object]:
    """Compile one structure's phase-keyed visual pack recipe or fail closed."""

    if not target_object_id or len(target_object_id) > 256:
        raise PlayableStructurePackCompilerError("target Object id is invalid")
    closure_digest = _closure_identity(visual_closure)
    scanned = _scanned_index(visual_closure)
    model_phases, animation_phases, exclusions = _phase_rows(
        visual_closure, target_object_id
    )
    if not model_phases:
        raise PlayableStructurePackCompilerError(
            f"structure has no resolved lifecycle model: {target_object_id}"
        )
    slug = _slug(target_object_id)

    model_hierarchy_ids: dict[str, frozenset[str]] = {}
    for model_path in model_phases:
        row = scanned.get(model_path.casefold())
        if row is None:
            raise PlayableStructurePackCompilerError(
                f"structure model is absent from scannedW3d: {model_path}"
            )
        model_hierarchy_ids[model_path] = frozenset(
            value.casefold() for value in _header_ids(row, "hierarchyIds")
        )
    target_model_keys = {path.casefold() for path in model_phases}

    animation_bindings: dict[str, dict[str, object]] = {}
    for animation_path in sorted(
        animation_phases, key=lambda item: (item.casefold(), item)
    ):
        row = scanned.get(animation_path.casefold())
        if row is None:
            raise PlayableStructurePackCompilerError(
                f"structure animation is absent from scannedW3d: {animation_path}"
            )
        prefixes = _animation_hierarchies(row, animation_path)
        providers = _hierarchy_providers(prefixes, scanned)
        unprovided = sorted(
            prefix for prefix, paths in providers.items() if not paths
        )
        if unprovided:
            exclusions.append(
                {
                    "kind": "animation",
                    "sourceW3d": animation_path,
                    "reason": "animation-hierarchy-unresolved",
                    "hierarchyIds": unprovided,
                }
            )
            continue
        animation_bindings[animation_path] = {
            "prefixes": prefixes,
            "providers": providers,
        }

    resources: list[dict[str, object]] = []
    states: list[dict[str, object]] = []
    attached_animations: set[str] = set()
    selected_w3d: set[str] = set()
    for model_path in sorted(model_phases, key=lambda item: (item.casefold(), item)):
        phases = tuple(
            sorted(
                model_phases[model_path],
                key=lambda value: (
                    LIFECYCLE_PHASE_ORDER.index(value),
                    value,
                ),
            )
        )
        own_hierarchies = model_hierarchy_ids[model_path]
        animations: list[str] = []
        hierarchy_patterns: set[str] = set()
        for animation_path, binding in animation_bindings.items():
            if not animation_phases[animation_path] & set(phases):
                continue
            prefixes = binding["prefixes"]
            assert isinstance(prefixes, frozenset)
            providers = binding["providers"]
            assert isinstance(providers, Mapping)
            compatible = True
            required_hierarchy_files: set[str] = set()
            for prefix in prefixes:
                if prefix in own_hierarchies:
                    continue
                if own_hierarchies:
                    compatible = False
                    break
                dedicated = [
                    path
                    for path in providers[prefix]
                    if path.casefold() not in target_model_keys
                ]
                if not dedicated:
                    compatible = False
                    break
                required_hierarchy_files.update(dedicated)
            if not compatible:
                continue
            animations.append(animation_path)
            hierarchy_patterns.update(required_hierarchy_files)
            attached_animations.add(animation_path)
        animations.sort(key=lambda item: (item.casefold(), item))
        stem = PurePosixPath(model_path).stem
        output = (
            f"assets/models/structures/{slug}/"
            f"{_phase_slug(phases)}-{_slug(stem)}.glb"
        )
        patterns = sorted(
            {model_path, *animations, *hierarchy_patterns},
            key=lambda item: (item.casefold(), item),
        )
        converter = "w3d-bundle" if animations else "w3d-hierarchical"
        resource_id = _resource_id("structure", slug, PurePosixPath(model_path).stem)
        options: dict[str, object] = {"model": PurePosixPath(model_path).name}
        if animations:
            options["animations"] = [
                PurePosixPath(path).name for path in animations
            ]
        selected_w3d.update(patterns)
        resources.append(
            {
                "id": resource_id,
                "kind": "model",
                "converter": converter,
                "patterns": patterns,
                "output": output,
                "options": options,
                "required": True,
                "limit": len(patterns),
                "expected_count": len(patterns),
            }
        )
        states.append(
            {
                "phases": list(phases),
                "sourceW3d": model_path,
                "animations": animations,
                "resourceId": resource_id,
                "output": output,
            }
        )

    for animation_path in sorted(
        set(animation_bindings) - attached_animations,
        key=lambda item: (item.casefold(), item),
    ):
        exclusions.append(
            {
                "kind": "animation",
                "sourceW3d": animation_path,
                "reason": "animation-unattached",
            }
        )

    dependency = visual_closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise PlayableStructurePackCompilerError("visual texture closure is invalid")
    selected_keys = {path.casefold() for path in selected_w3d}
    textures: set[str] = set()
    for row in _rows(dependency.get("embeddedTextures"), "embedded textures"):
        source = row.get("sourceW3dVirtualPath")
        if not isinstance(source, str) or source.casefold() not in selected_keys:
            continue
        if row.get("status") != "resolved":
            raise PlayableStructurePackCompilerError(
                f"selected structure W3D has unresolved texture: {source}"
            )
        textures.update(_paths(row, f"embedded texture {source}"))
    texture_paths = tuple(sorted(textures, key=lambda item: (item.casefold(), item)))
    texture_ids: list[str] = []
    texture_resources: list[dict[str, object]] = []
    for offset in range(0, len(texture_paths), MAX_PATTERNS_PER_RESOURCE):
        batch = texture_paths[offset : offset + MAX_PATTERNS_PER_RESOURCE]
        identifier = _resource_id(
            "structure", slug, f"material-textures-{offset // MAX_PATTERNS_PER_RESOURCE:03d}"
        )
        texture_ids.append(identifier)
        texture_resources.append(
            {
                "id": identifier,
                "kind": "texture",
                "converter": "hash-only",
                "patterns": list(batch),
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
            }
        )
    for resource in resources:
        options = resource["options"]
        assert isinstance(options, dict)
        options["inputResourceIds"] = list(texture_ids)

    covered_phases = sorted(
        {phase for state in states for phase in state["phases"]},
        key=LIFECYCLE_PHASE_ORDER.index,
    )
    exclusions.sort(
        key=lambda row: (
            str(row["reason"]),
            str(row["sourceW3d"]).casefold(),
            str(row["sourceW3d"]),
        )
    )
    identifiers = [str(row["id"]) for row in (*texture_resources, *resources)]
    if len({value.casefold() for value in identifiers}) != len(identifiers):
        raise PlayableStructurePackCompilerError(
            "structure recipe produced colliding resource ids"
        )

    document: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": target_object_id,
        "slug": slug,
        "visualClosureSha256": closure_digest,
        "resources": [*texture_resources, *resources],
        "lifecycleStates": states,
        "phaseCoverage": {
            "covered": covered_phases,
            "missing": [
                phase
                for phase in LIFECYCLE_PHASE_ORDER
                if phase not in covered_phases
            ],
        },
        "exclusions": exclusions,
    }
    document["recipeSha256"] = _digest(document)
    return document


def validate_structure_visual_recipe(value: Mapping[str, object]) -> None:
    """Reject any structure visual recipe that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableStructurePackCompilerError(
            "structure recipe identity is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("recipeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructurePackCompilerError("structure recipe digest is invalid")
    for field in ("objectId", "slug", "visualClosureSha256"):
        if not isinstance(value.get(field), str) or not value[field]:
            raise PlayableStructurePackCompilerError(
                f"structure recipe {field} is invalid"
            )
    if value["slug"] != _slug(str(value["objectId"])):
        raise PlayableStructurePackCompilerError(
            "structure recipe slug does not match its object id"
        )
    resources = _rows(value.get("resources"), "structure recipe resources")
    identifiers = [str(row.get("id", "")) for row in resources]
    if not identifiers or len(
        {item.casefold() for item in identifiers}
    ) != len(identifiers):
        raise PlayableStructurePackCompilerError(
            "structure recipe resource ids are invalid"
        )
    states = _rows(value.get("lifecycleStates"), "structure lifecycle states")
    if not states:
        raise PlayableStructurePackCompilerError(
            "structure recipe has no lifecycle states"
        )
    resource_ids = {identifier.casefold() for identifier in identifiers}
    for state in states:
        reference = str(state.get("resourceId", ""))
        if reference.casefold() not in resource_ids:
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state references an unknown resource"
            )
        phases = state.get("phases")
        if not isinstance(phases, list) or any(
            phase not in LIFECYCLE_PHASE_ORDER for phase in phases
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state phases are invalid"
            )


RUNTIME_SCHEMA = "openbfme.playable-structure-runtime"
RUNTIME_SCHEMA_VERSION = 0
LIFECYCLE_PRESENTATION_SCHEMA = "openbfme.building-lifecycle-presentation"
LIFECYCLE_PRESENTATION_SCHEMA_VERSION = 1


def compose_structure_runtime_document(
    descriptor: Mapping[str, object], visual_recipe: Mapping[str, object]
) -> dict[str, object]:
    """Join one structure descriptor and visual recipe into a runtime document."""

    from .playable_structure_compiler import (
        validate_playable_structure_descriptor,
    )

    validate_playable_structure_descriptor(descriptor)
    validate_structure_visual_recipe(visual_recipe)
    if str(descriptor["objectId"]).casefold() != str(
        visual_recipe["objectId"]
    ).casefold():
        raise PlayableStructurePackCompilerError(
            "structure descriptor and visual recipe identities differ"
        )

    health_contract = descriptor["gameplay"]["health"]
    if health_contract is None:
        raise PlayableStructurePackCompilerError(
            "foundation-only structures have no runtime lifecycle document"
        )
    health = health_contract["primary"]
    if not isinstance(health, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor health contract is invalid"
        )
    max_health = health.get("maxHealth")
    if not isinstance(max_health, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor lacks a resolved MaxHealth"
        )
    simulation_facts: dict[str, object] = {"maxHealth": max_health["value"]}
    damaged = health.get("maxHealthDamaged")
    really_damaged = health.get("maxHealthReallyDamaged")
    if isinstance(damaged, Mapping) and isinstance(really_damaged, Mapping):
        simulation_facts["damageStateRule"] = {
            "damagedThreshold": damaged["value"],
            "reallyDamagedThreshold": really_damaged["value"],
        }

    states = visual_recipe["lifecycleStates"]
    assert isinstance(states, list)
    by_phase: dict[str, dict[str, object]] = {}
    for state in states:
        assert isinstance(state, Mapping)
        for phase in state["phases"]:
            by_phase.setdefault(
                str(phase),
                {
                    "visual": state["output"],
                    "resourceId": state["resourceId"],
                    "animations": [
                        PurePosixPath(str(path)).name
                        for path in state["animations"]
                    ],
                },
            )
    covered = [phase for phase in LIFECYCLE_PHASE_ORDER if phase in by_phase]
    phases: list[dict[str, object]] = []
    for index, phase in enumerate(covered):
        entry: dict[str, object] = {"phase": phase, **by_phase[phase]}
        if index + 1 < len(covered):
            entry["nextPhase"] = covered[index + 1]
        phases.append(entry)

    document: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "objectId": descriptor["objectId"],
        "slug": visual_recipe["slug"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": visual_recipe["recipeSha256"],
        "registration": {
            "production": deepcopy(descriptor["production"]),
            "gameplay": deepcopy(descriptor["gameplay"]),
            "presentation": {
                "buildingLifecycle": {
                    "schema": LIFECYCLE_PRESENTATION_SCHEMA,
                    "schemaVersion": LIFECYCLE_PRESENTATION_SCHEMA_VERSION,
                    "phases": phases,
                    "phaseCoverage": deepcopy(visual_recipe["phaseCoverage"]),
                    "simulationFacts": simulation_facts,
                },
                "ui": deepcopy(descriptor["presentation"]["ui"]),
                "audioRoutes": deepcopy(descriptor["presentation"]["audioRoutes"]),
            },
            "unsupportedVisualReferences": deepcopy(visual_recipe["exclusions"]),
        },
    }
    document["runtimeSha256"] = _digest(document)
    return document


__all__ = [
    "LIFECYCLE_PHASE_ORDER",
    "LIFECYCLE_PRESENTATION_SCHEMA",
    "PlayableStructurePackCompilerError",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_structure_visual_recipe",
    "compose_structure_runtime_document",
    "validate_structure_visual_recipe",
]
