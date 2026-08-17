"""Bounded composition for retail scenario-neutral structure artifacts.

Neutral lairs and authored scenario structures use the ordinary structure
descriptor, visual-closure recipe, and lifecycle runtime contracts.  This
module is the admission boundary which joins those three generic compilers.
It never invents a construct route; a map-rooted retail-buildable Object keeps
the exact route already authored for it.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy
import hashlib
import json
import re

from .neutral_custom_animation_presentation import (
    NeutralCustomAnimationPresentationError,
    compile_neutral_custom_animation_presentation,
    validate_neutral_custom_animation_presentation,
)

from .playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from .playable_structure_lifecycle_evidence import (
    PlayableStructureLifecycleEvidenceError,
    compile_structure_lifecycle_evidence,
    validate_structure_lifecycle_evidence,
)
from .playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    _digest,
    compile_structure_visual_recipe,
    compose_structure_runtime_document,
    validate_structure_visual_recipe,
)
from .playable_unit_compiler import (
    PlayableUnitCompilerInputs,
    prepare_playable_unit_compiler,
)


SCHEMA = "openbfme.neutral-structure-pack-artifact"
SCHEMA_VERSION = 0
ROLES = frozenset({"lair", "neutral-structure"})
SURFACE_ORDER = (
    "map-placement",
    "script-spawn",
    "object-creation-list",
    "lair-spawn",
)


class NeutralStructurePackCompilerError(ValueError):
    """A neutral structure cannot be packaged from exact authored evidence."""


MAP_PLACEMENT_EVIDENCE_SCHEMA = (
    "openbfme.neutral-structure-map-placement-evidence"
)
MAP_PLACEMENT_EVIDENCE_SCHEMA_VERSION = 0
_MAP_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,255}")
_SHA256 = re.compile(r"[0-9a-f]{64}")


def compile_neutral_structure_map_placement_evidence(
    target_id: str, sources: Sequence[Mapping[str, object]]
) -> dict[str, object]:
    """Seal exact placement indices from one or more cooked retail maps.

    Each source supplies ``mapId`` and the exact ``objects.json`` bytes as
    ``objectsBytes``.  Consuming bytes rather than an already-decoded mapping
    preserves the source digest and prevents a caller from silently normalizing
    or filtering the retail placement table before it reaches this boundary.
    """

    if not isinstance(target_id, str) or not target_id or len(target_id) > 256:
        raise NeutralStructurePackCompilerError(
            "map placement target identity is invalid"
        )
    if isinstance(sources, (str, bytes)) or not sources:
        raise NeutralStructurePackCompilerError(
            "map placement sources are invalid"
        )
    maps: list[dict[str, object]] = []
    seen_map_ids: set[str] = set()
    for source in sources:
        if not isinstance(source, Mapping) or set(source) != {
            "mapId",
            "objectsBytes",
        }:
            raise NeutralStructurePackCompilerError(
                "map placement source is invalid"
            )
        map_id = source.get("mapId")
        objects_bytes = source.get("objectsBytes")
        if not isinstance(map_id, str) or _MAP_ID.fullmatch(map_id) is None:
            raise NeutralStructurePackCompilerError(
                "map placement source map identity is invalid"
            )
        folded_map_id = map_id.casefold()
        if folded_map_id in seen_map_ids:
            raise NeutralStructurePackCompilerError(
                "map placement source contains a duplicate map identity"
            )
        seen_map_ids.add(folded_map_id)
        if not isinstance(objects_bytes, bytes):
            raise NeutralStructurePackCompilerError(
                "map placement source objects bytes are invalid"
            )
        try:
            document = json.loads(objects_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise NeutralStructurePackCompilerError(
                f"map placement source objects document is invalid: {exc}"
            ) from exc
        if (
            not isinstance(document, Mapping)
            or document.get("schema") != "openbfme.sage-map-objects"
            or document.get("schemaVersion") != 0
            or not isinstance(document.get("objects"), list)
            or document.get("count") != len(document["objects"])
        ):
            raise NeutralStructurePackCompilerError(
                "map placement source objects contract is invalid"
            )
        source_indices: list[int] = []
        seen_indices: set[int] = set()
        for row in document["objects"]:
            if not isinstance(row, Mapping):
                raise NeutralStructurePackCompilerError(
                    "map placement source contains an invalid object row"
                )
            type_name = row.get("typeName")
            road_type = row.get("roadType")
            source_index = row.get("index")
            if (
                not isinstance(type_name, str)
                or not isinstance(road_type, int)
                or isinstance(road_type, bool)
                or not isinstance(source_index, int)
                or isinstance(source_index, bool)
                or source_index < 0
                or source_index in seen_indices
            ):
                raise NeutralStructurePackCompilerError(
                    "map placement source object row contract is invalid"
                )
            seen_indices.add(source_index)
            if type_name.casefold() == target_id.casefold() and type_name != target_id:
                raise NeutralStructurePackCompilerError(
                    "map placement target has a case-colliding retail identity"
                )
            if type_name == target_id and road_type == 0:
                source_indices.append(source_index)
        if not source_indices:
            raise NeutralStructurePackCompilerError(
                f"map placement source {map_id} contains no {target_id} placements"
            )
        source_indices.sort()
        maps.append(
            {
                "mapId": map_id,
                "objectsSha256": hashlib.sha256(objects_bytes).hexdigest(),
                "placementCount": len(source_indices),
                "sourceIndices": source_indices,
            }
        )
    maps.sort(key=lambda row: (str(row["mapId"]).casefold(), str(row["mapId"])))
    evidence: dict[str, object] = {
        "schema": MAP_PLACEMENT_EVIDENCE_SCHEMA,
        "schemaVersion": MAP_PLACEMENT_EVIDENCE_SCHEMA_VERSION,
        "objectId": target_id,
        "mapCount": len(maps),
        "placementCount": sum(int(row["placementCount"]) for row in maps),
        "maps": maps,
    }
    evidence["evidenceSha256"] = _digest(evidence)
    validate_neutral_structure_map_placement_evidence(evidence)
    return evidence


def validate_neutral_structure_map_placement_evidence(
    value: Mapping[str, object],
) -> None:
    """Reject altered placement counts, indices, map ids, or source digests."""

    if (
        value.get("schema") != MAP_PLACEMENT_EVIDENCE_SCHEMA
        or value.get("schemaVersion") != MAP_PLACEMENT_EVIDENCE_SCHEMA_VERSION
    ):
        raise NeutralStructurePackCompilerError(
            "map placement evidence schema is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("evidenceSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralStructurePackCompilerError(
            "map placement evidence digest is invalid"
        )
    object_id = value.get("objectId")
    maps = value.get("maps")
    if not isinstance(object_id, str) or not object_id or not isinstance(maps, list) or not maps:
        raise NeutralStructurePackCompilerError(
            "map placement evidence identity or maps are invalid"
        )
    seen: set[str] = set()
    placement_count = 0
    for row in maps:
        if not isinstance(row, Mapping) or set(row) != {
            "mapId",
            "objectsSha256",
            "placementCount",
            "sourceIndices",
        }:
            raise NeutralStructurePackCompilerError(
                "map placement evidence contains an invalid map row"
            )
        map_id = row.get("mapId")
        objects_sha256 = row.get("objectsSha256")
        count = row.get("placementCount")
        indices = row.get("sourceIndices")
        if (
            not isinstance(map_id, str)
            or _MAP_ID.fullmatch(map_id) is None
            or map_id.casefold() in seen
            or not isinstance(objects_sha256, str)
            or _SHA256.fullmatch(objects_sha256) is None
            or not isinstance(count, int)
            or isinstance(count, bool)
            or count <= 0
            or not isinstance(indices, list)
            or indices != sorted(set(indices))
            or any(
                not isinstance(index, int) or isinstance(index, bool) or index < 0
                for index in indices
            )
            or count != len(indices)
        ):
            raise NeutralStructurePackCompilerError(
                "map placement evidence map row contract is invalid"
            )
        seen.add(map_id.casefold())
        placement_count += count
    if (
        value.get("mapCount") != len(maps)
        or value.get("placementCount") != placement_count
    ):
        raise NeutralStructurePackCompilerError(
            "map placement evidence summary is invalid"
        )


def _normalized_admission(
    role: str, surfaces: Sequence[str]
) -> dict[str, object]:
    if role not in ROLES or isinstance(surfaces, (str, bytes)):
        raise NeutralStructurePackCompilerError(
            "neutral structure role or surfaces are invalid"
        )
    if any(not isinstance(value, str) for value in surfaces):
        raise NeutralStructurePackCompilerError(
            "neutral structure surfaces are invalid"
        )
    normalized = [value for value in SURFACE_ORDER if value in surfaces]
    if (
        not normalized
        or len(normalized) != len(surfaces)
        or len(set(surfaces)) != len(surfaces)
    ):
        raise NeutralStructurePackCompilerError(
            "neutral structure contains an unsupported or duplicate surface"
        )
    if role == "lair" and "lair-spawn" not in normalized:
        raise NeutralStructurePackCompilerError(
            "neutral lair admission must retain the lair-spawn surface"
        )
    if role == "neutral-structure" and "lair-spawn" in normalized:
        raise NeutralStructurePackCompilerError(
            "non-lair neutral structure cannot claim the lair-spawn surface"
        )
    return {"role": role, "surfaces": normalized}


def compile_neutral_structure_pack_artifact(
    target_id: str,
    documents: Mapping[str, bytes],
    visual_closure: Mapping[str, object],
    *,
    role: str,
    surfaces: Sequence[str],
    prepared: PlayableUnitCompilerInputs | None = None,
    game: str = "bfme2",
    resolved_images: Mapping[str, Mapping[str, object]] | None = None,
    image_binding_gaps: Sequence[Mapping[str, object]] | None = None,
    effect_documents: Mapping[str, bytes] | None = None,
    fx_texture_index: Mapping[str, str] | None = None,
    map_placement_sources: Sequence[Mapping[str, object]] = (),
) -> dict[str, object]:
    """Compile one exact neutral structure pack artifact or fail closed.

    The caller supplies a sealed retail visual closure. This function never
    searches a fallback asset root. A map-rooted Object that already has exact
    authored production retains those routes; only route-less roots receive
    the scenario-nonbuildable admission contract.
    """

    if game not in {"bfme2", "rotwk"}:
        raise NeutralStructurePackCompilerError(f"unsupported game {game!r}")
    if not isinstance(target_id, str) or not target_id or len(target_id) > 256:
        raise NeutralStructurePackCompilerError("neutral structure identity is invalid")
    admission = _normalized_admission(role, surfaces)
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise NeutralStructurePackCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    target = prepared.objects.get(target_id.casefold())
    if target is None:
        raise NeutralStructurePackCompilerError(
            f"neutral structure effective Object is missing: {target_id}"
        )
    try:
        descriptor = compile_playable_structure_descriptor(
            target_id,
            documents,
            prepared=prepared,
            scenario_admission=admission,
            game=game,
        )
        if descriptor.get("objectId") != target_id:
            raise NeutralStructurePackCompilerError(
                "neutral structure identity changed during effective Object resolution"
            )
        recipe = compile_structure_visual_recipe(
            target_id,
            visual_closure,
            resolved_images=resolved_images,
            image_binding_gaps=image_binding_gaps,
        )
        evidence = compile_structure_lifecycle_evidence(
            target_id, documents, prepared=prepared
        )
        runtime = compose_structure_runtime_document(descriptor, recipe, evidence)
        gameplay = descriptor.get("gameplay", {})
        graph = gameplay.get("upgradeEffects", {}) if isinstance(gameplay, Mapping) else {}
        effects = graph.get("effects", []) if isinstance(graph, Mapping) else []
        has_custom_animation = any(
            isinstance(row, Mapping) and isinstance(row.get("customAnimation"), Mapping)
            for row in effects if isinstance(effects, list)
        )
        if has_custom_animation and (effect_documents is None or fx_texture_index is None):
            raise NeutralStructurePackCompilerError(
                f"neutral structure {target_id} custom animation requires effective particle documents and texture index"
            )
        custom_presentation = (
            compile_neutral_custom_animation_presentation(
                descriptor, evidence, effect_documents or {},
                texture_index=fx_texture_index or {}, game=game,
            )
            if has_custom_animation else None
        )
        if custom_presentation is not None:
            runtime = deepcopy(runtime)
            runtime["registration"]["presentation"]["deferredCustomAnimationRequest"] = deepcopy(custom_presentation)
            runtime.pop("runtimeSha256", None)
            runtime["runtimeSha256"] = _digest(runtime)
        map_placement_evidence = None
        if map_placement_sources:
            if "map-placement" not in admission["surfaces"]:
                raise NeutralStructurePackCompilerError(
                    "map placement evidence requires the map-placement surface"
                )
            map_placement_evidence = (
                compile_neutral_structure_map_placement_evidence(
                    target_id, map_placement_sources
                )
            )
            runtime = deepcopy(runtime)
            runtime["registration"]["mapPlacementEvidence"] = deepcopy(
                map_placement_evidence
            )
            runtime.pop("runtimeSha256", None)
            runtime["runtimeSha256"] = _digest(runtime)
    except NeutralStructurePackCompilerError:
        raise
    except (
        PlayableStructureCompilerError,
        PlayableStructureLifecycleEvidenceError,
        PlayableStructurePackCompilerError,
        NeutralCustomAnimationPresentationError,
    ) as exc:
        raise NeutralStructurePackCompilerError(
            f"neutral structure {target_id} is not package-ready: {exc}"
        ) from exc

    source_identity = {
        "declarationKind": target.kind,
        "sourceIni": target.source_virtual_path,
        "line": target.line,
    }
    artifact: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "objectId": target_id,
        "role": role,
        "surfaces": list(admission["surfaces"]),
        "sourceIdentity": source_identity,
        "descriptor": descriptor,
        "visualRecipe": recipe,
        "lifecycleEvidence": evidence,
        "runtime": runtime,
    }
    if custom_presentation is not None:
        artifact["customAnimationPresentation"] = custom_presentation
    if map_placement_evidence is not None:
        artifact["mapPlacementEvidence"] = map_placement_evidence
    artifact["artifactSha256"] = _digest(artifact)
    validate_neutral_structure_pack_artifact(artifact)
    return artifact


def validate_neutral_structure_pack_artifact(value: Mapping[str, object]) -> None:
    """Reject drift, invented construction, or cross-object asset binding."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact schema is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("artifactSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact digest is invalid"
        )
    game = value.get("game")
    object_id = value.get("objectId")
    role = value.get("role")
    surfaces = value.get("surfaces")
    if game not in {"bfme2", "rotwk"} or not isinstance(object_id, str) or not object_id:
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact identity is invalid"
        )
    if not isinstance(surfaces, list):
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact surfaces are invalid"
        )
    admission = _normalized_admission(str(role), surfaces)

    descriptor = value.get("descriptor")
    recipe = value.get("visualRecipe")
    evidence = value.get("lifecycleEvidence")
    runtime = value.get("runtime")
    if not all(isinstance(item, Mapping) for item in (descriptor, recipe, evidence, runtime)):
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact documents are invalid"
        )
    assert isinstance(descriptor, Mapping)
    assert isinstance(recipe, Mapping)
    assert isinstance(evidence, Mapping)
    assert isinstance(runtime, Mapping)
    try:
        validate_playable_structure_descriptor(descriptor)
        validate_structure_visual_recipe(recipe)
        validate_structure_lifecycle_evidence(evidence)
    except (
        PlayableStructureCompilerError,
        PlayableStructureLifecycleEvidenceError,
        PlayableStructurePackCompilerError,
        NeutralCustomAnimationPresentationError,
    ) as exc:
        raise NeutralStructurePackCompilerError(
            f"neutral structure artifact contains invalid evidence: {exc}"
        ) from exc

    identities = {
        str(descriptor.get("objectId", "")),
        str(recipe.get("objectId", "")),
        str(evidence.get("objectId", "")),
        str(runtime.get("objectId", "")),
    }
    if identities != {object_id}:
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact cross-document identity is invalid"
        )
    production = descriptor.get("production")
    scenario = descriptor.get("scenarioAdmission")
    source_identity = value.get("sourceIdentity")
    source_documents = descriptor.get("sourceDocuments")
    valid_source_identity = (
        isinstance(source_identity, Mapping)
        and set(source_identity) == {"declarationKind", "sourceIni", "line"}
        and source_identity.get("declarationKind") in {"Object", "ChildObject"}
        and isinstance(source_identity.get("sourceIni"), str)
        and bool(source_identity.get("sourceIni"))
        and isinstance(source_identity.get("line"), int)
        and not isinstance(source_identity.get("line"), bool)
        and int(source_identity.get("line", 0)) > 0
        and isinstance(source_documents, list)
        and source_identity.get("sourceIni")
        in {
            row.get("virtualPath")
            for row in source_documents
            if isinstance(row, Mapping)
        }
    )
    scenario_nonbuildable = (
        isinstance(production, Mapping)
        and production.get("evidence") == "authored-neutral-map"
        and production.get("routes") == []
        and isinstance(scenario, Mapping)
        and scenario.get("role") == admission["role"]
        and scenario.get("surfaces") == admission["surfaces"]
        and scenario.get("buildCommandExposed") is False
        and source_identity
        == {
            "declarationKind": scenario.get("declarationKind"),
            "sourceIni": scenario.get("sourceIni"),
            "line": scenario.get("line"),
        }
    )
    authored_buildable = (
        isinstance(production, Mapping)
        and production.get("evidence")
        in {"authored-construct-command", "authored-wall-upgrade-command"}
        and isinstance(production.get("routes"), list)
        and bool(production.get("routes"))
        and scenario is None
    )
    if not valid_source_identity or not (scenario_nonbuildable or authored_buildable):
        raise NeutralStructurePackCompilerError(
            "neutral structure artifact admission or source provenance is invalid"
        )
    registration = runtime.get("registration")
    if (
        runtime.get("schema") != "openbfme.playable-structure-runtime"
        or not isinstance(registration, Mapping)
        or registration.get("production") != production
        or registration.get("scenarioAdmission") != scenario
        or runtime.get("descriptorSha256") != descriptor.get("descriptorSha256")
        or runtime.get("recipeSha256") != recipe.get("recipeSha256")
        or runtime.get("lifecycleEvidenceSha256") != evidence.get("evidenceSha256")
    ):
        raise NeutralStructurePackCompilerError(
            "neutral structure runtime binding is invalid"
        )
    runtime_unsigned = dict(runtime)
    runtime_digest = runtime_unsigned.pop("runtimeSha256", None)
    if not isinstance(runtime_digest, str) or runtime_digest != _digest(runtime_unsigned):
        raise NeutralStructurePackCompilerError(
            "neutral structure runtime digest is invalid"
        )
    custom = value.get("customAnimationPresentation")
    presentation = registration.get("presentation")
    embedded = presentation.get("deferredCustomAnimationRequest") if isinstance(presentation, Mapping) else None
    if custom is None:
        if embedded is not None:
            raise NeutralStructurePackCompilerError(
                "neutral structure has an unsealed custom animation request"
            )
    else:
        if not isinstance(custom, Mapping) or embedded != custom:
            raise NeutralStructurePackCompilerError(
                "neutral structure custom animation runtime binding drifted"
            )
        try:
            validate_neutral_custom_animation_presentation(custom)
        except NeutralCustomAnimationPresentationError as exc:
            raise NeutralStructurePackCompilerError(
                f"neutral structure custom animation prerequisite is invalid: {exc}"
            ) from exc
        gameplay = descriptor.get("gameplay", {})
        graph = gameplay.get("upgradeEffects", {}) if isinstance(gameplay, Mapping) else {}
        effects = graph.get("effects", []) if isinstance(graph, Mapping) else []
        edge_count = sum(
            1 for row in effects
            if isinstance(row, Mapping) and isinstance(row.get("customAnimation"), Mapping)
        ) if isinstance(effects, list) else 0
        if len(custom.get("edgeIds", [])) != edge_count:
            raise NeutralStructurePackCompilerError(
                "neutral structure custom animation edge binding drifted"
            )
    map_placement = value.get("mapPlacementEvidence")
    embedded_map_placement = registration.get("mapPlacementEvidence")
    if map_placement is None:
        if embedded_map_placement is not None:
            raise NeutralStructurePackCompilerError(
                "neutral structure has unsealed map placement evidence"
            )
    else:
        if (
            not isinstance(map_placement, Mapping)
            or embedded_map_placement != map_placement
            or map_placement.get("objectId") != object_id
            or "map-placement" not in admission["surfaces"]
        ):
            raise NeutralStructurePackCompilerError(
                "neutral structure map placement binding drifted"
            )
        validate_neutral_structure_map_placement_evidence(map_placement)


__all__ = [
    "NeutralStructurePackCompilerError",
    "ROLES",
    "SCHEMA",
    "SCHEMA_VERSION",
    "SURFACE_ORDER",
    "compile_neutral_structure_map_placement_evidence",
    "compile_neutral_structure_pack_artifact",
    "validate_neutral_structure_map_placement_evidence",
    "validate_neutral_structure_pack_artifact",
]
