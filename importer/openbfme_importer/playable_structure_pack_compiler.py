"""Closure-driven playable-structure conversion recipe compiler.

Structures convert from the same retail visual closure as playable units, but
their pack recipes are keyed by building lifecycle phase instead of unit core
animation states.  This module owns the source-backed visual recipe and the
descriptor+evidence-bound runtime composition.

Version note: the runtime envelope (``openbfme.playable-structure-runtime``)
stays at schemaVersion 0 — its identity fields and registration wrapper are
unchanged, and the publication lane pins that version.  The embedded
``openbfme.building-lifecycle-presentation`` document is composed at
schemaVersion 1 in the exact presenter-grade shape RetailStructure validates,
carrying ``evidenceProfile: "composed-structure-runtime"`` so validators can
distinguish it from the sealed Men and neutral evidence lanes.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy
from pathlib import PurePosixPath
import re

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
SCHEMA_VERSION = 1
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


def _animation_clip_ids(row: Mapping[str, object]) -> tuple[str, ...]:
    clips: set[str] = set()
    for identifier in _header_ids(row, "animationIds"):
        if "." not in identifier:
            continue
        clips.add(identifier.split(".", 1)[1].casefold())
    return tuple(sorted(clips))


def _phase_rows(
    visual_closure: Mapping[str, object], target_object_id: str
) -> tuple[
    dict[str, set[str]],
    dict[str, set[tuple[str, ...]]],
    dict[str, set[str]],
    dict[str, set[tuple[str, ...]]],
    list[dict[str, object]],
]:
    target_key = target_object_id.casefold()
    model_phases: dict[str, set[str]] = {}
    model_conditions: dict[str, set[tuple[str, ...]]] = {}
    model_draw_modules: dict[str, set[str]] = {}
    animation_phases: dict[str, set[str]] = {}
    bib_conditions: dict[str, set[tuple[str, ...]]] = {}
    exclusions: list[dict[str, object]] = []
    for row in _rows(visual_closure.get("exactLeaves"), "exact visual leaves"):
        if str(row.get("targetObject", "")).casefold() != target_key:
            continue
        kind = str(row.get("kind", "")).casefold()
        if kind not in {"model", "animation"}:
            continue
        conditions = row.get("conditions", [])
        if not isinstance(conditions, list) or any(
            not isinstance(value, str) for value in conditions
        ):
            raise PlayableStructurePackCompilerError(
                f"visual leaf conditions are invalid: {row.get('identifier')}"
            )
        condition_keys = {str(value).casefold() for value in conditions}
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
        condition_tuple = tuple(str(value) for value in conditions)
        if kind == "model" and str(row.get("usage", "")) == "floor-model":
            for path in paths:
                bib_conditions.setdefault(path, set()).add(condition_tuple)
            continue
        provenance = row.get("provenance", {})
        scope_path = (
            provenance.get("scopePath", [])
            if isinstance(provenance, Mapping)
            else []
        )
        draw_module = (
            str(scope_path[0])
            if isinstance(scope_path, list) and scope_path
            else ""
        )
        destination = model_phases if kind == "model" else animation_phases
        for path in paths:
            destination.setdefault(path, set()).update(raw_phases)
            if kind == "model":
                model_conditions.setdefault(path, set()).add(condition_tuple)
                model_draw_modules.setdefault(path, set()).add(draw_module)
    return (
        model_phases,
        model_conditions,
        model_draw_modules,
        animation_phases,
        bib_conditions,
        exclusions,
    )


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


def _condition_sets_list(sets: set[tuple[str, ...]]) -> list[list[str]]:
    return [
        list(value)
        for value in sorted(sets, key=lambda item: (len(item), item))
    ]


def compile_structure_visual_recipe(
    target_object_id: str, visual_closure: Mapping[str, object]
) -> dict[str, object]:
    """Compile one structure's phase-keyed visual pack recipe or fail closed."""

    if not target_object_id or len(target_object_id) > 256:
        raise PlayableStructurePackCompilerError("target Object id is invalid")
    closure_digest = _closure_identity(visual_closure)
    scanned = _scanned_index(visual_closure)
    (
        model_phases,
        model_conditions,
        model_draw_modules,
        animation_phases,
        bib_conditions,
        exclusions,
    ) = _phase_rows(visual_closure, target_object_id)
    if not model_phases:
        raise PlayableStructurePackCompilerError(
            f"structure has no resolved lifecycle model: {target_object_id}"
        )
    slug = _slug(target_object_id)

    model_hierarchy_ids: dict[str, frozenset[str]] = {}
    for model_path in (*model_phases, *bib_conditions):
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
            "clipIds": _animation_clip_ids(row),
        }

    resources: list[dict[str, object]] = []
    states: list[dict[str, object]] = []
    bib_states: list[dict[str, object]] = []
    attached_animations: set[str] = set()
    selected_w3d: set[str] = set()

    def _compile_model(
        model_path: str,
        *,
        output: str,
        bind_animations: bool,
    ) -> tuple[str, list[str], list[str]]:
        own_hierarchies = model_hierarchy_ids[model_path]
        animations: list[str] = []
        clip_ids: set[str] = set()
        hierarchy_patterns: set[str] = set()
        if bind_animations:
            phases = model_phases[model_path]
            for animation_path, binding in animation_bindings.items():
                if not animation_phases[animation_path] & phases:
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
                binding_clips = binding["clipIds"]
                assert isinstance(binding_clips, tuple)
                clip_ids.update(binding_clips)
                hierarchy_patterns.update(required_hierarchy_files)
                attached_animations.add(animation_path)
        animations.sort(key=lambda item: (item.casefold(), item))
        patterns = sorted(
            {model_path, *animations, *hierarchy_patterns},
            key=lambda item: (item.casefold(), item),
        )
        # A lifecycle model with no animation binding is not automatically a
        # skinned hierarchy. A model-authored hierarchy is a rigid carrier and
        # must use the adapter's explicit, validated root-rigid bake; a model
        # without one is a static mesh. Calling both shapes merely
        # ``w3d-hierarchical`` deferred the distinction until Blender and made
        # real bib models fail at skin validation.
        converter = (
            "w3d-bundle"
            if animations
            else "w3d-hierarchical"
            if own_hierarchies
            else "w3d-static"
        )
        resource_id = _resource_id("structure", slug, PurePosixPath(model_path).stem)
        options: dict[str, object] = {"model": PurePosixPath(model_path).name}
        if animations:
            options["animations"] = [
                PurePosixPath(path).name for path in animations
            ]
        elif own_hierarchies:
            options["provenRootRigidBake"] = True
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
        return resource_id, animations, sorted(clip_ids)

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
        stem = PurePosixPath(model_path).stem
        output = (
            f"assets/models/structures/{slug}/"
            f"{_phase_slug(phases)}-{_slug(stem)}.glb"
        )
        resource_id, animations, clip_ids = _compile_model(
            model_path, output=output, bind_animations=True
        )
        states.append(
            {
                "phases": list(phases),
                "sourceW3d": model_path,
                "sourceConditionSets": _condition_sets_list(
                    model_conditions.get(model_path, set())
                ),
                "drawModules": sorted(model_draw_modules.get(model_path, set())),
                "animations": animations,
                "animationClipIds": clip_ids,
                "resourceId": resource_id,
                "output": output,
            }
        )

    for bib_path in sorted(bib_conditions, key=lambda item: (item.casefold(), item)):
        stem = PurePosixPath(bib_path).stem
        output = f"assets/models/structures/{slug}/bib-{_slug(stem)}.glb"
        resource_id, _animations, _clips = _compile_model(
            bib_path, output=output, bind_animations=False
        )
        bib_states.append(
            {
                "sourceW3d": bib_path,
                "sourceConditionSets": _condition_sets_list(
                    bib_conditions[bib_path]
                ),
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
        "bibStates": bib_states,
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
    resource_ids = {identifier.casefold() for identifier in identifiers}
    states = _rows(value.get("lifecycleStates"), "structure lifecycle states")
    if not states:
        raise PlayableStructurePackCompilerError(
            "structure recipe has no lifecycle states"
        )
    bib_states = value.get("bibStates")
    if not isinstance(bib_states, list):
        raise PlayableStructurePackCompilerError(
            "structure recipe bib states are invalid"
        )
    for state in (*states, *bib_states):
        if not isinstance(state, Mapping):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state is invalid"
            )
        reference = str(state.get("resourceId", ""))
        if reference.casefold() not in resource_ids:
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state references an unknown resource"
            )
        condition_sets = state.get("sourceConditionSets")
        if not isinstance(condition_sets, list) or any(
            not isinstance(conditions, list)
            or any(not isinstance(token, str) for token in conditions)
            for conditions in condition_sets
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state condition sets are invalid"
            )
    for state in states:
        phases = state.get("phases")
        if not isinstance(phases, list) or any(
            phase not in LIFECYCLE_PHASE_ORDER for phase in phases
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state phases are invalid"
            )
        clip_ids = state.get("animationClipIds")
        if not isinstance(clip_ids, list) or any(
            not isinstance(clip, str) or not clip for clip in clip_ids
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state clip ids are invalid"
            )
        draw_modules = state.get("drawModules")
        if not isinstance(draw_modules, list) or any(
            not isinstance(module, str) for module in draw_modules
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state draw modules are invalid"
            )


RUNTIME_SCHEMA = "openbfme.playable-structure-runtime"
RUNTIME_SCHEMA_VERSION = 0
LIFECYCLE_PRESENTATION_SCHEMA = "openbfme.building-lifecycle-presentation"
LIFECYCLE_PRESENTATION_SCHEMA_VERSION = 1
COMPOSED_EVIDENCE_PROFILE = "composed-structure-runtime"
PRESENTED_PHASE_ORDER = (
    "construction",
    "intact",
    "damaged",
    "really-damaged",
    "collapsing",
    "rubble",
    "post-rubble",
    "post-collapse",
)

_EXCLUDED_CONDITION_TOKENS = frozenset(
    {"SNOW", "WORLD_BUILDER", "BUILD_PLACEMENT_CURSOR", "PHANTOM_STRUCTURE"}
)
_EXCLUDED_CONDITION_PREFIXES = ("UPGRADE_", "DOOR_", "USER_", "WEAPONSET_")
_CONSTRUCTION_CONDITIONS = frozenset(
    {
        "AWAITING_CONSTRUCTION",
        "ACTIVELY_BEING_CONSTRUCTED",
        "PARTIALLY_CONSTRUCTED",
        "CONSTRUCTION_COMPLETE",
    }
)
_POST_RUBBLE_CONDITIONS = frozenset({"POST_RUBBLE", "POST_COLLAPSE"})
_ANIMATION_MODE_MAP = {
    "MANUAL": "manual-progress",
    "LOOP": "loop",
    "ONCE": "once",
}
_CANONICAL_PHASE_LABELS = {
    "intact": [[]],
    "damaged": [["DAMAGED"]],
    "really-damaged": [["REALLYDAMAGED"]],
    "collapsing": [["COLLAPSING"]],
    "rubble": [["RUBBLE"]],
    "post-rubble": [["POST_RUBBLE"]],
    "post-collapse": [["POST_COLLAPSE"]],
}


def _runtime_object_id(source_id: str) -> str:
    """Mirror the runtime's camel-splitting bundle id rule exactly."""

    output: list[str] = []
    previous_dash = False
    for index, character in enumerate(source_id):
        code = ord(character)
        is_upper = 65 <= code <= 90
        is_lower = 97 <= code <= 122
        is_digit = 48 <= code <= 57
        if is_upper and index > 0 and not previous_dash:
            previous = ord(source_id[index - 1])
            if 97 <= previous <= 122 or 48 <= previous <= 57:
                output.append("-")
        if is_upper or is_lower or is_digit:
            output.append(character.lower())
            previous_dash = False
        elif not previous_dash and output:
            output.append("-")
            previous_dash = True
    slug = "".join(output).rstrip("-")
    if not slug:
        raise PlayableStructurePackCompilerError(
            f"structure object id has no safe runtime id: {source_id!r}"
        )
    return "bfme2.object." + slug


def _filtered_condition_set(conditions: Sequence[str]) -> tuple[str, ...]:
    result = []
    for value in conditions:
        folded = str(value).upper()
        if folded in _EXCLUDED_CONDITION_TOKENS:
            continue
        if folded.startswith(_EXCLUDED_CONDITION_PREFIXES):
            continue
        result.append(folded)
    return tuple(sorted(result))


def _canonical_match(phase: str, condition_set: tuple[str, ...]) -> bool:
    values = set(condition_set)
    if phase == "construction":
        return bool(values) and values <= _CONSTRUCTION_CONDITIONS
    if phase == "intact":
        return not values
    if phase == "damaged":
        return values == {"DAMAGED"}
    if phase == "really-damaged":
        return values == {"REALLYDAMAGED"}
    if phase == "rubble":
        return values == {"RUBBLE"}
    if phase == "post-rubble":
        return bool(values) and values <= _POST_RUBBLE_CONDITIONS
    return False


def _primary_draw_module(
    states: Sequence[Mapping[str, object]],
    notes: list[dict[str, object]],
) -> tuple[Mapping[str, object], ...]:
    """Restrict phase selection to the primary lifecycle draw module.

    Retail structures render several draw modules at once (body, house-color
    banner, floor bib).  The presenter contract carries exactly one body per
    phase, so the module authoring the widest lifecycle coverage is the body;
    every other module's models stay packed but are recorded as unpresented
    secondary visuals instead of silently competing for phase slots.
    """

    module_phases: dict[str, set[str]] = {}
    for state in states:
        modules = state.get("drawModules", [])
        assert isinstance(modules, list)
        for module in modules or [""]:
            module_phases.setdefault(str(module), set()).update(
                str(phase) for phase in state["phases"]
            )
    if not module_phases:
        raise PlayableStructurePackCompilerError(
            "structure has no lifecycle draw module evidence"
        )
    best_coverage = max(len(phases) for phases in module_phases.values())
    winners = sorted(
        module
        for module, phases in module_phases.items()
        if len(phases) == best_coverage
    )
    if len(winners) != 1:
        raise PlayableStructurePackCompilerError(
            "structure primary lifecycle draw module is ambiguous: "
            + ", ".join(winners)
        )
    primary = winners[0]
    result: list[Mapping[str, object]] = []
    for state in states:
        modules = state.get("drawModules", [])
        assert isinstance(modules, list)
        if primary in {str(module) for module in modules or [""]}:
            result.append(state)
        else:
            notes.append(
                {
                    "kind": "phase-visual",
                    "reason": "secondary-draw-module-visual",
                    "sourceW3d": str(state.get("sourceW3d", "")),
                    "drawModules": [str(module) for module in modules],
                }
            )
    return tuple(result)


def _select_phase_states(
    states: Sequence[Mapping[str, object]],
    notes: list[dict[str, object]],
) -> dict[str, Mapping[str, object]]:
    """Pick the canonical recipe state per authored phase or fail closed."""

    primary_states = _primary_draw_module(states, notes)
    selected: dict[str, Mapping[str, object]] = {}
    for phase in LIFECYCLE_PHASE_ORDER:
        candidates: list[Mapping[str, object]] = []
        for state in primary_states:
            if phase not in state["phases"]:
                continue
            condition_sets = state.get("sourceConditionSets", [])
            assert isinstance(condition_sets, list)
            if any(
                _canonical_match(phase, _filtered_condition_set(conditions))
                for conditions in condition_sets
            ):
                candidates.append(state)
        outputs = {str(state["output"]) for state in candidates}
        if len(outputs) > 1:
            raise PlayableStructurePackCompilerError(
                f"structure phase visual is ambiguous after weather/editor "
                f"filtering: {phase}: " + ", ".join(sorted(outputs))
            )
        if candidates:
            selected[phase] = candidates[0]
    return selected


def _evidence_state_clips(
    state: Mapping[str, object],
) -> list[dict[str, object]]:
    """Group one evidence state's Animation sub-blocks into clip records."""

    groups: dict[tuple[str, ...], dict[str, object]] = {}
    order: list[tuple[str, ...]] = []
    for assignment in state.get("assignments", []):
        if not isinstance(assignment, Mapping):
            continue
        key = str(assignment.get("key", ""))
        if key not in {"AnimationName", "AnimationMode"}:
            continue
        provenance = assignment.get("provenance", {})
        scope = tuple(
            str(part)
            for part in (
                provenance.get("scopePath", [])
                if isinstance(provenance, Mapping)
                else []
            )
        )
        group = groups.get(scope)
        if group is None:
            group = {"names": [], "mode": None}
            groups[scope] = group
            order.append(scope)
        raw = str(assignment.get("rawValue", "")).strip()
        if not raw:
            continue
        if key == "AnimationMode":
            group["mode"] = raw.upper()
        else:
            names = group["names"]
            assert isinstance(names, list)
            names.append(raw.split(".")[-1].lower())
    clips: list[dict[str, object]] = []
    for scope in order:
        group = groups[scope]
        mode = group["mode"]
        names = group["names"]
        assert isinstance(names, list)
        for name in names:
            clips.append(
                {
                    "clip": name,
                    "rawMode": mode if isinstance(mode, str) else "ONCE",
                    "modeSource": "authored" if isinstance(mode, str) else (
                        "engine-default-once"
                    ),
                }
            )
    return clips


def _phase_evidence_clips(
    evidence_states: Sequence[Mapping[str, object]],
    phase: str,
) -> tuple[list[dict[str, object]], bool]:
    """Return (clips, from_idle_family) for one lifecycle phase."""

    clips: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    idle_family = False
    for state in evidence_states:
        conditions = state.get("conditions", [])
        if not isinstance(conditions, list):
            continue
        filtered = _filtered_condition_set(
            [str(value) for value in conditions]
        )
        if not _canonical_match(phase, filtered):
            continue
        family = str(state.get("family", "")).casefold()
        state_clips = _evidence_state_clips(state)
        if state_clips and family == "idleanimationstate":
            idle_family = True
        for clip in state_clips:
            key = (str(clip["clip"]), str(clip["rawMode"]))
            if key in seen:
                continue
            seen.add(key)
            clips.append(clip)
    return clips, idle_family


def _phase_animation(
    evidence_states: Sequence[Mapping[str, object]],
    phase: str,
    bundled_clip_ids: set[str],
    notes: list[dict[str, object]],
) -> dict[str, object]:
    """Derive one phase's declared animation from state evidence, fail closed."""

    source_phase = "rubble" if phase == "collapsing" else phase
    clips, idle_family = _phase_evidence_clips(evidence_states, source_phase)
    if phase == "rubble":
        # The rubble-entry clip is presented on the collapsing phase (the Men
        # contract); retained rubble is static.
        return {"clip": None, "mode": "none"}
    available: list[dict[str, object]] = []
    for clip in clips:
        name = str(clip["clip"])
        if name not in bundled_clip_ids:
            notes.append(
                {
                    "kind": "animation-clip",
                    "phase": phase,
                    "clip": name,
                    "reason": "clip-not-bundled-for-phase-model",
                }
            )
            continue
        raw_mode = str(clip["rawMode"])
        mode = _ANIMATION_MODE_MAP.get(raw_mode)
        if mode is None:
            notes.append(
                {
                    "kind": "animation-clip",
                    "phase": phase,
                    "clip": name,
                    "reason": f"unsupported-animation-mode-{raw_mode.lower()}",
                }
            )
            continue
        available.append({"clip": name, "mode": mode})
    if phase == "construction":
        manual = sorted(
            {
                str(clip["clip"])
                for clip in available
                if clip["mode"] == "manual-progress"
            }
        )
        if len(manual) != 1:
            raise PlayableStructurePackCompilerError(
                "structure construction phase requires exactly one bundled "
                f"MANUAL animation clip, found {len(manual)}"
            )
        return {"clip": manual[0], "mode": "manual-progress"}
    if not available:
        return {"clip": None, "mode": "none"}
    if idle_family:
        names = list(dict.fromkeys(str(clip["clip"]) for clip in available))
        result: dict[str, object] = {"clip": names[0], "mode": "loop-random"}
        if len(names) > 1:
            result["alternateClips"] = names[1:]
        return result
    names = list(dict.fromkeys(str(clip["clip"]) for clip in available))
    if len(names) > 1:
        notes.append(
            {
                "kind": "animation-clip",
                "phase": phase,
                "reason": "ambiguous-phase-clips",
                "clips": names,
            }
        )
        return {"clip": None, "mode": "none"}
    mode = next(
        str(clip["mode"]) for clip in available if str(clip["clip"]) == names[0]
    )
    return {"clip": names[0], "mode": mode}


def _floor_draw_bib(
    recipe_bib_states: Sequence[Mapping[str, object]],
    floor_draws: Sequence[Mapping[str, object]],
) -> dict[str, object] | None:
    if not recipe_bib_states and not floor_draws:
        return None
    if bool(recipe_bib_states) != bool(floor_draws):
        raise PlayableStructurePackCompilerError(
            "structure floor-draw evidence and bib model closure disagree"
        )
    candidates = [
        state
        for state in recipe_bib_states
        if any(
            not _filtered_condition_set([str(v) for v in conditions])
            for conditions in state.get("sourceConditionSets", [])
        )
    ]
    outputs = {str(state["output"]) for state in candidates}
    if len(outputs) != 1:
        raise PlayableStructurePackCompilerError(
            "structure bib visual is absent or ambiguous after weather/editor "
            "filtering: " + ", ".join(sorted(outputs))
        )
    selected = candidates[0]
    hide_conditions: set[str] = set()
    start_hidden = False
    draw_modules: set[str] = set()
    for draw in floor_draws:
        draw_modules.add(str(draw.get("moduleKind", "")))
        for assignment in draw.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = str(assignment.get("key", ""))
            raw = str(assignment.get("rawValue", "")).strip()
            if key == "HideIfModelConditions":
                hide_conditions.update(token.upper() for token in raw.split())
            elif key == "StartHidden":
                if raw not in {"Yes", "No"}:
                    raise PlayableStructurePackCompilerError(
                        "structure floor draw StartHidden value is invalid"
                    )
                start_hidden = raw == "Yes"
    during_construction = not (
        {"AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED"} & hide_conditions
    )
    return {
        "drawModule": "/".join(sorted(draw_modules)),
        "duringConstruction": during_construction,
        "hideIfModelConditions": sorted(hide_conditions),
        "sourceConditions": [],
        "startHiddenAuthored": start_hidden,
        "visibility": "condition-driven-authored-floor-draw",
        "visual": {
            "mode": "glb",
            "glb": str(selected["output"]),
            "modelResourceId": str(selected["resourceId"]),
        },
    }


def _scalar_number(
    descriptor: Mapping[str, object], field: str
) -> float:
    gameplay = descriptor.get("gameplay")
    assert isinstance(gameplay, Mapping)
    scalar_fields = gameplay.get("scalarFields")
    row = (
        scalar_fields.get(field) if isinstance(scalar_fields, Mapping) else None
    )
    if not isinstance(row, Mapping):
        raise PlayableStructurePackCompilerError(
            f"structure descriptor lacks a {field} scalar field"
        )
    raw = row.get("value", row.get("expression"))
    try:
        value = float(str(raw).strip())
    except ValueError:
        raise PlayableStructurePackCompilerError(
            f"structure {field} is not a resolved number: {raw!r}"
        ) from None
    if not value > 0.0:
        raise PlayableStructurePackCompilerError(
            f"structure {field} is not positive: {raw!r}"
        )
    return value


_COLLAPSE_INTEGER_FIELDS = {
    "MinCollapseDelay": "minCollapseDelayMilliseconds",
    "MaxCollapseDelay": "maxCollapseDelayMilliseconds",
    "MinBurstDelay": "minBurstDelayMilliseconds",
    "MaxBurstDelay": "maxBurstDelayMilliseconds",
    "BigBurstFrequency": "bigBurstFrequency",
    "CollapseHeight": "collapseHeight",
}
_COLLAPSE_FLOAT_FIELDS = {
    "CollapseDamping": "collapseDamping",
    "MaxShudder": "maxShudder",
}


_ANIMATION_SOUND_RE = re.compile(
    r"^Sound:\s*(?P<event>\S+)\s+Animation:\s*(?P<animation>\S+)\s+"
    r"Frames:\s*(?P<frames>[0-9 ]+)$"
)
_MODEL_CONDITION_SOUND_RE = re.compile(
    r"^(?P<condition>.+?)\s+Sound:\s*(?P<event>\S+)$"
)


def _generic_audio_bindings(
    modules: Sequence[Mapping[str, object]],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    """Mirror the sealed Men audio-behavior reading with retail-wide spacing."""

    summary: dict[str, object] = {"collapse": None, "construction": None}
    bindings: list[dict[str, object]] = []
    for module in modules:
        kind = module.get("moduleKind")
        source_object = str(module.get("sourceObject", ""))
        for assignment in module.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = assignment.get("key")
            raw = str(assignment.get("rawValue", "")).strip()
            if not raw:
                continue
            if (
                kind == "ModelConditionAudioLoopClientBehavior"
                and key == "ModelCondition"
            ):
                match = _MODEL_CONDITION_SOUND_RE.fullmatch(raw)
                if match is None:
                    raise PlayableStructurePackCompilerError(
                        f"invalid ModelCondition audio value: {raw!r}"
                    )
                event = match.group("event")
                bindings.append(
                    {
                        "eventId": event,
                        "kind": "model-condition-loop",
                        "sourceConditionExpression": match.group("condition"),
                        "sourceObject": source_object,
                    }
                )
                if "RUBBLE" in match.group("condition").upper():
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
                    raise PlayableStructurePackCompilerError(
                        f"invalid AnimationSound value: {raw!r}"
                    )
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


def _generic_collapse_contract(
    modules: Sequence[Mapping[str, object]],
) -> dict[str, object] | None:
    """Read authored StructureCollapseUpdate facts without requiring the full
    Men field set; unauthored fields stay absent instead of being invented."""

    candidates: list[dict[str, object]] = []
    for module in modules:
        if module.get("moduleKind") != "StructureCollapseUpdate":
            continue
        contract: dict[str, object] = {
            "module": "StructureCollapseUpdate",
            "sourceObject": str(module.get("sourceObject", "")),
            "fxLists": {},
            "exactTotalTimingStatus": "blocked-on-bfme2-runtime-oracle",
        }
        for assignment in module.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = str(assignment.get("key", ""))
            raw = str(assignment.get("rawValue", "")).strip()
            if key == "FXList":
                parts = raw.split()
                if len(parts) != 2:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse FXList: {raw!r}"
                    )
                fx = contract["fxLists"]
                assert isinstance(fx, dict)
                fx[parts[0].casefold().replace("_", "-")] = parts[1]
            elif key in _COLLAPSE_INTEGER_FIELDS:
                try:
                    contract[_COLLAPSE_INTEGER_FIELDS[key]] = int(raw)
                except ValueError:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse {key} value: {raw!r}"
                    ) from None
            elif key in _COLLAPSE_FLOAT_FIELDS:
                try:
                    contract[_COLLAPSE_FLOAT_FIELDS[key]] = float(raw)
                except ValueError:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse {key} value: {raw!r}"
                    ) from None
            elif key == "DestroyObjectWhenDone":
                if raw not in {"Yes", "No"}:
                    raise PlayableStructurePackCompilerError(
                        "invalid DestroyObjectWhenDone value"
                    )
                contract["destroyObjectWhenDone"] = raw == "Yes"
        candidates.append(contract)
    if not candidates:
        return None
    payloads = {
        _digest({k: v for k, v in value.items() if k != "sourceObject"})
        for value in candidates
    }
    if len(payloads) != 1:
        raise PlayableStructurePackCompilerError(
            "StructureCollapseUpdate evidence is contradictory"
        )
    return candidates[-1]


def _unique_notes(notes: list[dict[str, object]]) -> list[dict[str, object]]:
    ordered = sorted(
        notes,
        key=lambda row: (
            str(row.get("kind", "")),
            str(row.get("phase", "")),
            str(row.get("reason", "")),
            str(row.get("clip", "")),
        ),
    )
    result: list[dict[str, object]] = []
    for note in ordered:
        if note not in result:
            result.append(note)
    return result


def _phase_row(
    *,
    phase: str,
    condition_sets: list[list[str]],
    visual: Mapping[str, object],
    animation: Mapping[str, object],
    next_phase: str | None,
) -> dict[str, object]:
    return {
        "phase": phase,
        "sourceConditionSets": [list(value) for value in condition_sets],
        "transitionAuthority": "deterministic-simulation",
        "visual": deepcopy(dict(visual)),
        "animation": deepcopy(dict(animation)),
        "nextPhase": next_phase,
    }


def compose_structure_runtime_document(
    descriptor: Mapping[str, object],
    visual_recipe: Mapping[str, object],
    lifecycle_evidence: Mapping[str, object],
) -> dict[str, object]:
    """Join one structure descriptor, visual recipe, and lifecycle evidence
    into a runtime document carrying the presenter-grade version-1
    building-lifecycle presentation."""

    from .playable_structure_compiler import (
        validate_playable_structure_descriptor,
    )
    from .playable_structure_lifecycle_evidence import (
        validate_structure_lifecycle_evidence,
    )
    from .retail_men_lifecycle_profile import (
        _entering_state_fx,
        _particle_attachments,
    )

    validate_playable_structure_descriptor(descriptor)
    validate_structure_visual_recipe(visual_recipe)
    validate_structure_lifecycle_evidence(lifecycle_evidence)
    identities = {
        str(descriptor["objectId"]).casefold(),
        str(visual_recipe["objectId"]).casefold(),
        str(lifecycle_evidence["objectId"]).casefold(),
    }
    if len(identities) != 1:
        raise PlayableStructurePackCompilerError(
            "structure descriptor, visual recipe, and lifecycle evidence "
            "identities differ"
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
    damaged = health.get("maxHealthDamaged")
    really_damaged = health.get("maxHealthReallyDamaged")
    if not isinstance(max_health, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor lacks a resolved MaxHealth"
        )
    if not isinstance(damaged, Mapping) or not isinstance(really_damaged, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor lacks authored damage thresholds required "
            "by the presenter lifecycle"
        )

    runtime_id = _runtime_object_id(str(descriptor["objectId"]))
    states = visual_recipe["lifecycleStates"]
    assert isinstance(states, list)
    notes: list[dict[str, object]] = []
    selected = _select_phase_states(states, notes)
    intact_state = selected.get("intact")
    if intact_state is None:
        raise PlayableStructurePackCompilerError(
            "structure has no canonical default-state intact visual"
        )
    construction_state = selected.get("construction")
    if construction_state is None:
        raise PlayableStructurePackCompilerError(
            "structure has no dedicated construction visual"
        )

    def _visual_for(phase: str) -> tuple[Mapping[str, object], dict[str, object]]:
        state = selected.get(phase)
        if state is not None:
            return state, {
                "mode": "glb",
                "glb": str(state["output"]),
                "modelResourceId": str(state["resourceId"]),
            }
        # SAGE model-condition fallback: with no dedicated state authored, the
        # default (intact) model keeps rendering for this condition.
        notes.append(
            {
                "kind": "phase-visual",
                "phase": phase,
                "reason": "default-model-condition-state-fallback",
            }
        )
        return intact_state, {
            "mode": "glb",
            "glb": str(intact_state["output"]),
            "modelResourceId": str(intact_state["resourceId"]),
            "visualFallback": "default-model-condition-state",
        }

    no_render = {"mode": "no-render", "sourceIdentifier": "None"}
    evidence_states = lifecycle_evidence["visualStates"]
    assert isinstance(evidence_states, list)
    evidence_modules = lifecycle_evidence["runtimeModules"]
    assert isinstance(evidence_modules, list)
    floor_draws = lifecycle_evidence["floorDraws"]
    assert isinstance(floor_draws, list)

    def _bundled_clips(state: Mapping[str, object]) -> set[str]:
        clip_ids = state.get("animationClipIds", [])
        assert isinstance(clip_ids, list)
        return {str(value) for value in clip_ids}

    construction_condition_sets = [
        [str(token) for token in conditions]
        for conditions in construction_state.get("sourceConditionSets", [])
        if _canonical_match(
            "construction",
            _filtered_condition_set([str(token) for token in conditions]),
        )
    ]
    if not construction_condition_sets:
        raise PlayableStructurePackCompilerError(
            "structure lacks exact construction conditions"
        )

    phase_rows: list[dict[str, object]] = []
    for index, phase in enumerate(PRESENTED_PHASE_ORDER):
        next_phase = (
            PRESENTED_PHASE_ORDER[index + 1]
            if phase not in {"post-rubble", "post-collapse"}
            else None
        )
        if phase in {"post-rubble", "post-collapse"}:
            state = selected.get("post-rubble") if phase == "post-rubble" else None
            if state is not None:
                visual: dict[str, object] = {
                    "mode": "glb",
                    "glb": str(state["output"]),
                    "modelResourceId": str(state["resourceId"]),
                }
            else:
                visual = dict(no_render)
            phase_rows.append(
                _phase_row(
                    phase=phase,
                    condition_sets=_CANONICAL_PHASE_LABELS[phase],
                    visual=visual,
                    animation={"clip": None, "mode": "none"},
                    next_phase=None,
                )
            )
            continue
        if phase == "collapsing":
            state, visual = _visual_for("rubble")
        else:
            state, visual = _visual_for(phase)
        animation = _phase_animation(
            evidence_states, phase, _bundled_clips(state), notes
        )
        condition_sets = (
            construction_condition_sets
            if phase == "construction"
            else _CANONICAL_PHASE_LABELS[phase]
        )
        phase_rows.append(
            _phase_row(
                phase=phase,
                condition_sets=condition_sets,
                visual=visual,
                animation=animation,
                next_phase=next_phase,
            )
        )

    construction_row = phase_rows[0]
    construction_animation = construction_row["animation"]
    assert isinstance(construction_animation, Mapping)

    collapse = _generic_collapse_contract(
        [module for module in evidence_modules if isinstance(module, Mapping)]
    )
    if collapse is not None:
        collapse_facts: dict[str, object] = deepcopy(collapse)
        collapse_update_fx = deepcopy(collapse["fxLists"])
        terminal = (
            "destroy-object-when-collapse-done"
            if collapse.get("destroyObjectWhenDone") is True
            else "retained-until-explicit-destruction"
        )
    else:
        collapse_facts = {
            "module": None,
            "status": "no-authored-structure-collapse-update",
        }
        collapse_update_fx = {}
        terminal = "retained-until-explicit-destruction"

    entering_fx, entering_records = _entering_state_fx(
        [state for state in evidence_states if isinstance(state, Mapping)]
    )
    particles = _particle_attachments(
        [state for state in evidence_states if isinstance(state, Mapping)]
    )
    audio_events, audio_bindings = _generic_audio_bindings(
        [module for module in evidence_modules if isinstance(module, Mapping)]
    )

    bib_states = visual_recipe.get("bibStates", [])
    assert isinstance(bib_states, list)
    bib = _floor_draw_bib(bib_states, floor_draws)

    simulation_facts: dict[str, object] = {
        "maximumHealth": max_health["value"],
        "damageStateRule": {
            "damagedThreshold": damaged["value"],
            "reallyDamagedThreshold": really_damaged["value"],
        },
        "construction": {
            "buildTimeSeconds": _scalar_number(descriptor, "BuildTime"),
            "animationMode": "MANUAL",
            "animation": construction_animation["clip"],
        },
        "collapse": collapse_facts,
        "postRubble": {"terminalDuration": terminal},
    }

    lifecycle: dict[str, object] = {
        "schema": LIFECYCLE_PRESENTATION_SCHEMA,
        "schemaVersion": LIFECYCLE_PRESENTATION_SCHEMA_VERSION,
        "evidenceProfile": COMPOSED_EVIDENCE_PROFILE,
        "objectId": runtime_id,
        "initialPhase": "intact",
        "phases": phase_rows,
        "phaseCoverage": deepcopy(visual_recipe["phaseCoverage"]),
        "bib": bib,
        "audioEvents": {
            "collapse": audio_events.get("collapse"),
            "construction": audio_events.get("construction"),
        },
        "audioBindings": audio_bindings,
        "effects": {
            "collapseUpdateFx": collapse_update_fx,
            "definitionTranslationStatus": (
                "requires-exact-definition-runtime-binding"
            ),
            "enteringStateFx": entering_fx,
            "enteringStateBindings": entering_records,
            "particleAttachments": particles,
        },
        "simulationFacts": simulation_facts,
        "rebuildHole": None,
        "compositionExclusions": _unique_notes(notes),
    }

    document: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "objectId": descriptor["objectId"],
        "slug": visual_recipe["slug"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": visual_recipe["recipeSha256"],
        "lifecycleEvidenceSha256": lifecycle_evidence["evidenceSha256"],
        "registration": {
            "production": deepcopy(descriptor["production"]),
            "gameplay": deepcopy(descriptor["gameplay"]),
            "presentation": {
                "buildingLifecycle": lifecycle,
                "ui": deepcopy(descriptor["presentation"]["ui"]),
                "audioRoutes": deepcopy(descriptor["presentation"]["audioRoutes"]),
            },
            "unsupportedVisualReferences": deepcopy(visual_recipe["exclusions"]),
        },
    }
    document["runtimeSha256"] = _digest(document)
    return document


__all__ = [
    "COMPOSED_EVIDENCE_PROFILE",
    "LIFECYCLE_PHASE_ORDER",
    "LIFECYCLE_PRESENTATION_SCHEMA",
    "PRESENTED_PHASE_ORDER",
    "PlayableStructurePackCompilerError",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_structure_visual_recipe",
    "compose_structure_runtime_document",
    "validate_structure_visual_recipe",
]
