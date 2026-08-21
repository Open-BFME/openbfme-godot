"""Generic, map-agnostic prop binding for every converted retail map.

``options.objectBindings`` used to exist only as a hand-composed Fords of Isen
II artefact: :mod:`retail_slice_profile` and
:mod:`retail_fords_completion_profile` compose one map's 54 model/structure rows
and 26 logical rows behind walls of exact-count assertions, so every other
converted map bound zero props.  The producer chain underneath those composers
(:mod:`retail_visual_closure` into the static, hierarchical and animated prop
planners) was already map-agnostic, so this module is the missing plumbing: it
feeds one map's own recorded placement type set through that chain and returns
the resulting bindings and conversion resources.

Nothing here is Fords-specific and nothing is substituted.  A type the chain
cannot prove stays out of the bindings and is recorded with the planner's own
rejection reasons, where the cooked ``object-bindings.json`` reports it as
``unresolved`` exactly as it does today.
"""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
from typing import Any, Mapping

from .profile import assert_input_resource_references_resolve
from .retail_animated_prop_profile import build_retail_animated_prop_plan
from .retail_hierarchical_profile import build_retail_hierarchical_prop_plan
from .retail_visual_closure import build_retail_visual_closure
from .retail_visual_profile import build_retail_static_prop_plan
from .playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    compile_structure_visual_recipe,
)


MAP_PROP_BINDING_SCHEMA = "openbfme.map-prop-binding-evidence"
MAP_PROP_BINDING_SCHEMA_VERSION = 0

#: SAGE logical namespaces.  A map placement whose type name starts with ``*``
#: names an editor/simulation namespace (``*Waypoints/Waypoint``,
#: ``*GenericAIObjects/GenericAIObject``), not an Object definition.  There is
#: no renderable behind one, so they are declared logical rather than fed to the
#: visual closure, whose target validator rejects the characters they contain.
PSEUDO_TYPE_PREFIX = "*"

#: The static planner's exclusive rejection reason for a fully resolved Object
#: that authors no geometry.  On its own it is proof of non-renderability.
NO_AUTHORED_MODEL_REASON = "no-physical-model-w3d"
NO_AUTHORED_MODEL_CLASSIFICATION = "no-authored-model"

_FORBIDDEN_TARGET_CHARACTERS = frozenset('/\\:*?"<>|')
# Split ``GenericAIObject`` as Generic/AI/Object, not Generic/AIObject: the
# second alternative closes an acronym run before a following capitalised word.
_CAMEL_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
_CLASSIFICATION_UNSAFE = re.compile(r"[^A-Za-z0-9._-]+")
_CLASSIFICATION = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}")

MAP_OBJECTS_SCHEMA = "openbfme.sage-map-objects"
MAP_OBJECTS_SCHEMA_VERSION = 0


def pseudo_type_classification(type_name: str) -> str:
    """Return the logical classification for one ``*Namespace/Type`` name.

    The classification is derived from the retail namespace itself rather than
    from a hand-maintained table: ``*Waypoints/Waypoint`` becomes ``waypoint``
    and ``*GenericAIObjects/GenericAIObject`` becomes ``generic-ai-object``.
    """

    tail = type_name.lstrip(PSEUDO_TYPE_PREFIX).replace("\\", "/").rsplit("/", 1)[-1]
    kebab = _CAMEL_BOUNDARY.sub("-", tail).casefold()
    classification = _CLASSIFICATION_UNSAFE.sub("-", kebab).strip("-._")
    if not classification or not _CLASSIFICATION.fullmatch(classification):
        raise ValueError(f"unclassifiable logical placement type: {type_name!r}")
    return classification


def _is_safe_visual_target(value: str) -> bool:
    return not (
        not value
        or value != value.strip()
        or len(value) > 255
        or value in {".", ".."}
        or value.startswith(".")
        or value.endswith(".")
        or ".." in value
        or any(character.isspace() for character in value)
        or any(character in _FORBIDDEN_TARGET_CHARACTERS for character in value)
        or any(ord(character) < 32 for character in value)
    )


def partition_placement_types(parsed: Any) -> tuple[list[str], list[str], list[str]]:
    """Split a parsed map's non-road placement types into the three lanes.

    Returns ``(visual targets, logical pseudo-types, unsafe names)`` in
    deterministic order.  Road control points are excluded exactly as the
    profile's ``unboundObjectTypes`` evidence already excludes them.
    """

    names = sorted(
        {
            str(item["typeName"])
            for item in parsed.objects
            if int(item["roadType"]) == 0
        },
        key=lambda value: (value.casefold(), value),
    )
    targets: list[str] = []
    pseudo: list[str] = []
    unsafe: list[str] = []
    for name in names:
        if name.startswith(PSEUDO_TYPE_PREFIX):
            pseudo.append(name)
        elif _is_safe_visual_target(name):
            targets.append(name)
        else:
            unsafe.append(name)
    return targets, pseudo, unsafe


def _map_objects_document(parsed: Any) -> dict[str, Any]:
    """Rebuild the cooked ``objects.json`` contract the planners consume.

    The hierarchical and animated planners count exact placements from the
    cooked map objects document.  A profile is generated before any cook, so it
    is produced here from the very same ``parse_sage_map_bytes`` rows the
    converter writes.
    """

    return {
        "schema": MAP_OBJECTS_SCHEMA,
        "schemaVersion": MAP_OBJECTS_SCHEMA_VERSION,
        "count": len(parsed.objects),
        "objects": deepcopy(parsed.objects),
    }


def _synthetic_base_profile(
    map_pattern: str,
    output_root: str,
    logical_rows: list[dict[str, str]],
) -> dict[str, Any]:
    """Return the minimal base profile the animated planner validates against.

    The animated planner needs to know which closure targets a caller has
    already claimed as logical so it never plans an animation for one.  Only
    that logical list is read, so the map resource is reproduced exactly rather
    than a whole profile being threaded through this lane.
    """

    return {
        "format": 1,
        "id": "map-prop-binding-base",
        "pack": {"id": "map-prop-binding-base-pack"},
        "resources": [
            {
                "id": "map-binary",
                "kind": "map",
                "converter": "sage-map",
                "patterns": [map_pattern],
                "output": output_root,
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "objectBindings": {
                        "logical": deepcopy(logical_rows),
                        "models": [],
                    }
                },
            }
        ],
    }


def _texture_owner_key(resource: Mapping[str, Any]) -> str | None:
    if str(resource.get("kind")) != "texture":
        return None
    patterns = resource.get("patterns")
    if not isinstance(patterns, list) or len(patterns) != 1:
        return None
    return str(patterns[0]).casefold()


def _merge_stage_resources(
    accumulated: list[dict[str, Any]],
    texture_owners: dict[str, dict[str, Any]],
    stage_resources: list[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    """Add one planner stage's resources, keeping one owner per texture source.

    The stages deliberately do not know about each other, so a texture used by
    both a static and a hierarchical model is declared twice under two ids and
    two cooked outputs.  The Fords composer resolves that by hand; here the
    first stage to claim a source stays its owner and the later stage's model
    dependency is rewritten to it, so a source is cooked once.

    ``texture_owners`` maps a retail texture source to the *whole* owning
    resource, not just its id, and is shared across every map in a profile.  A
    later map that reaches the same source must still declare that owner --
    recording only the id let a map inherit an owner from a map whose bindings
    were later dropped whole, leaving its models pointing at a resource nothing
    declared.  Re-declaring the stored owner is byte-identical to the original
    declaration, so the profile still collapses it to one resource.
    """

    rewritten: dict[str, str] = {}
    added: list[dict[str, Any]] = []
    declared = {str(item["id"]) for item in accumulated}
    for resource in stage_resources:
        key = _texture_owner_key(resource)
        if key is None:
            continue
        owner = texture_owners.get(key)
        if owner is None:
            texture_owners[key] = deepcopy(dict(resource))
            added.append(deepcopy(dict(resource)))
            declared.add(str(resource["id"]))
            continue
        owner_id = str(owner["id"])
        if owner_id != str(resource["id"]):
            rewritten[str(resource["id"])] = owner_id
        if owner_id not in declared:
            added.append(deepcopy(owner))
            declared.add(owner_id)
    for resource in stage_resources:
        if _texture_owner_key(resource) is not None:
            continue
        record = deepcopy(dict(resource))
        options = record.get("options")
        if isinstance(options, dict):
            input_ids = options.get("inputResourceIds")
            if isinstance(input_ids, list):
                options["inputResourceIds"] = [
                    rewritten.get(str(value), str(value)) for value in input_ids
                ]
        added.append(record)
    accumulated.extend(added)
    return added


def _default_lifecycle_visual_binding(
    target: str, recipe: Mapping[str, Any]
) -> dict[str, Any]:
    defaults = [
        state
        for state in recipe.get("lifecycleStates", [])
        if isinstance(state, Mapping)
        and "intact" in state.get("phases", [])
        and [] in state.get("sourceConditionSets", [])
    ]
    if len(defaults) != 1:
        raise PlayableStructurePackCompilerError(
            f"expected one default intact model, found {len(defaults)}"
        )
    default = defaults[0]
    binding: dict[str, Any] = {
        "typeName": target,
        "sourceVirtualModel": str(default["sourceW3d"]),
        "glb": str(default["output"]),
        "matchMethod": "exact-type-name",
    }
    walk_sources = recipe.get("walkSurfaceSources")
    if isinstance(walk_sources, Mapping) and walk_sources:
        binding["walkSurfaceSources"] = {
            str(mesh_name): {
                "sourceVirtualModel": str(source["sourceW3d"]),
                "glb": str(source["glb"]),
            }
            for mesh_name, source in walk_sources.items()
            if isinstance(source, Mapping)
        }
    return binding


def build_map_prop_binding_plan(
    parsed: Any,
    *,
    effective_assets_root: Path | str,
    effective_assets_manifest: Mapping[str, Any],
    map_pattern: str,
    output_root: str,
    texture_owners: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Plan one map's object bindings and their conversion resources.

    Every fact comes from the map's own placement rows and the effective-assets
    tree.  The three planner stages partition the map's target types; a type no
    stage can prove is left unbound with its recorded reasons.

    ``texture_owners`` should be shared across every map in one profile.  Two
    maps that place the same prop derive the same resource id from the same
    retail source, but which *stage* first claims a shared texture differs per
    map (Fords' static rocks own ``shadowi``; Weather Hills reaches it only
    through the animated wolf).  Sharing the owner map keeps one owner per
    retail source across the whole profile, so a shared model resource is
    declared identically wherever it appears.
    """

    targets, pseudo, unsafe = partition_placement_types(parsed)
    logical_rows = [
        {"typeName": name, "classification": pseudo_type_classification(name)}
        for name in pseudo
    ]
    evidence: dict[str, Any] = {
        "schema": MAP_PROP_BINDING_SCHEMA,
        "schemaVersion": MAP_PROP_BINDING_SCHEMA_VERSION,
        "policy": {
            "selection": "map-recorded-placement-types-only",
            "substitutesAllowed": False,
            "stages": ["static", "hierarchical", "animated", "lifecycle-visual"],
            "logicalRule": "sage-logical-namespace-prefix",
        },
        "placementTypeCount": len(targets) + len(pseudo) + len(unsafe),
        "visualTargetTypeCount": len(targets),
        "logicalTypeCount": len(pseudo),
        "unsafeTypeNames": unsafe,
    }
    if not targets:
        evidence["stages"] = {}
        evidence["boundTypeCount"] = 0
        evidence["unboundTypeNames"] = []
        return {
            "objectBindings": {"logical": logical_rows, "models": []},
            "resources": [],
            "evidence": evidence,
        }

    closure = build_retail_visual_closure(effective_assets_root, targets)
    static_plan = build_retail_static_prop_plan(closure, effective_assets_manifest)
    map_objects = _map_objects_document(parsed)
    hierarchical_plan = build_retail_hierarchical_prop_plan(
        closure, static_plan, effective_assets_manifest, map_objects
    )
    animated_plan = build_retail_animated_prop_plan(
        closure,
        static_plan,
        hierarchical_plan,
        effective_assets_manifest,
        map_objects,
        _synthetic_base_profile(map_pattern, output_root, logical_rows),
        effective_assets_root,
    )

    resources: list[dict[str, Any]] = []
    owners = {} if texture_owners is None else texture_owners
    model_rows: list[dict[str, Any]] = []
    stage_evidence: dict[str, Any] = {}
    for name, plan in (
        ("static", static_plan),
        ("hierarchical", hierarchical_plan),
        ("animated", animated_plan),
    ):
        fragment = plan["profileFragment"]
        added = _merge_stage_resources(
            resources, owners, list(fragment["resources"])
        )
        rows = list(fragment["objectBindings"]["models"])
        model_rows.extend(deepcopy(row) for row in rows)
        stage_evidence[name] = {
            "planAggregateSha256": str(plan["aggregateSha256"]),
            "bindingRowCount": len(rows),
            "declaredResourceCount": len(fragment["resources"]),
            "addedResourceCount": len(added),
        }

    # Multi-state retail objects (lairs, signal fires, flags, and similar
    # neutral structures) are intentionally rejected by the ordinary prop
    # planners. For map visibility, reuse the generic structure recipe only
    # when it proves one unambiguous default/intact model. This emits a normal
    # renderable binding: lifecycle gameplay remains unwired and is never
    # claimed by the map pack.
    already_bound = {str(row["typeName"]) for row in model_rows}
    lifecycle_rows: list[dict[str, Any]] = []
    lifecycle_rejections: list[dict[str, str]] = []
    for target in targets:
        if target in already_bound:
            continue
        try:
            recipe = compile_structure_visual_recipe(target, closure)
            binding = _default_lifecycle_visual_binding(target, recipe)
            _merge_stage_resources(
                resources, owners, list(recipe.get("resources", []))
            )
            lifecycle_rows.append(binding)
        except (PlayableStructurePackCompilerError, ValueError, KeyError) as exc:
            lifecycle_rejections.append(
                {"typeName": target, "reason": str(exc)[:300]}
            )
    model_rows.extend(lifecycle_rows)
    stage_evidence["lifecycle-visual"] = {
        "bindingRowCount": len(lifecycle_rows),
        "rejectionCount": len(lifecycle_rejections),
        "rejections": lifecycle_rejections,
        "claim": "default-retail-visual-only-lifecycle-gameplay-unwired",
    }

    bound = {str(row["typeName"]) for row in model_rows}
    # A target the static planner rejected for exactly one reason -- it has no
    # physical model W3D at all -- is proven non-renderable rather than merely
    # unplanned: its definition resolved, its inheritance is complete, it raised
    # no graph diagnostic, and it authored zero geometry. Retail's ambient audio
    # emitters and skirmish AI markers are exactly this shape, and the Fords
    # profile declares all 26 of them logical by hand. Declaring them logical
    # here is the same fact, derived instead of authored.
    non_renderable = sorted(
        str(item["targetObject"])
        for item in static_plan["ineligibleTargets"]
        if str(item["targetObject"]) not in bound
        and [str(reason["code"]) for reason in item["reasons"]]
        == [NO_AUTHORED_MODEL_REASON]
    )
    logical_rows.extend(
        {"typeName": name, "classification": NO_AUTHORED_MODEL_CLASSIFICATION}
        for name in non_renderable
    )
    logical_rows.sort(
        key=lambda row: (str(row["typeName"]).casefold(), str(row["typeName"]))
    )
    duplicates = sorted(
        name
        for name in {str(row["typeName"]).casefold() for row in model_rows}
        if sum(
            1 for row in model_rows if str(row["typeName"]).casefold() == name
        )
        > 1
    )
    if duplicates:
        raise ValueError(
            "map prop stages produced overlapping model bindings: "
            + ", ".join(duplicates)
        )
    model_rows.sort(
        key=lambda row: (str(row["typeName"]).casefold(), str(row["typeName"]))
    )
    logical_names = {str(row["typeName"]) for row in logical_rows}
    unbound = [
        name for name in targets if name not in bound and name not in logical_names
    ]
    stage_evidence["static"]["ineligibleReasonCounts"] = list(
        static_plan["summary"]["ineligibleReasonCounts"]
    )
    stage_evidence["hierarchical"]["rejectionReasonCounts"] = list(
        hierarchical_plan["summary"]["rejectionReasonCounts"]
    )
    stage_evidence["animated"]["rejectionReasonCounts"] = list(
        animated_plan["summary"]["rejectionReasonCounts"]
    )
    evidence["stages"] = stage_evidence
    evidence["boundTypeCount"] = len(bound)
    evidence["unboundTypeNames"] = unbound
    evidence["visualClosureAggregateSha256"] = str(closure["aggregateSha256"])
    evidence["resourceCount"] = len(resources)
    placement_counts: dict[str, int] = {}
    for item in parsed.objects:
        if int(item["roadType"]) != 0:
            continue
        name = str(item["typeName"])
        placement_counts[name] = placement_counts.get(name, 0) + 1
    evidence["boundPlacementCount"] = sum(
        placement_counts.get(name, 0) for name in bound
    )
    evidence["logicalTypeCount"] = len(logical_rows)
    evidence["derivedNonRenderableTypeNames"] = non_renderable
    evidence["logicalPlacementCount"] = sum(
        placement_counts.get(name, 0) for name in logical_names
    )
    evidence["unboundPlacementCount"] = sum(
        placement_counts.get(name, 0) for name in unbound
    )
    assert_input_resource_references_resolve(resources, label=map_pattern)
    return {
        "objectBindings": {"logical": logical_rows, "models": model_rows},
        "resources": resources,
        "evidence": evidence,
    }


def load_effective_assets_manifest(
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    """Read the manifest ``extract-all-assets`` writes beside an assets tree."""

    path = Path(effective_assets_root).expanduser() / ".openbfme" / "manifest.json"
    if not path.is_file():
        raise ValueError(f"effective-assets manifest is missing: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("effective-assets manifest root must be an object")
    return value


__all__ = [
    "MAP_PROP_BINDING_SCHEMA",
    "MAP_PROP_BINDING_SCHEMA_VERSION",
    "PSEUDO_TYPE_PREFIX",
    "build_map_prop_binding_plan",
    "load_effective_assets_manifest",
    "partition_placement_types",
    "pseudo_type_classification",
]
