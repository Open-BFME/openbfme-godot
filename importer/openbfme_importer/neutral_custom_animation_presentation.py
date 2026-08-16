"""Sealed resources for neutral CommandSetUpgrade presentation requests.

Retail neutral lairs request USER_2 while that visual state consists solely of
two ParticleSysBone attachments.  Exact animation timing is not yet proven, so
this module packages the authored particle closure and provenance but emits an
explicitly deferred, non-activating request.
"""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy

from .playable_structure_pack_compiler import _digest
from .retail_ability_fx_ingress import (
    AbilityFxIngressError,
    build_ability_fx_closure,
    validate_ability_fx_closure,
)


SCHEMA = "openbfme.neutral-custom-animation-presentation"
SCHEMA_VERSION = 0
PARTICLE_IDS = ("UntamedAllegiance", "UntamedAllegiance2")
DEFERRED_REASON = "custom-animation-timing-oracle-unresolved"


class NeutralCustomAnimationPresentationError(ValueError):
    pass


def _custom_edges(descriptor: Mapping[str, object]) -> list[dict[str, object]]:
    gameplay = descriptor.get("gameplay")
    graph = gameplay.get("upgradeEffects") if isinstance(gameplay, Mapping) else None
    effects = graph.get("effects") if isinstance(graph, Mapping) else None
    if not isinstance(effects, list):
        return []
    return [
        deepcopy(dict(row))
        for row in effects
        if isinstance(row, Mapping) and isinstance(row.get("customAnimation"), Mapping)
    ]


def _attachments(evidence: Mapping[str, object]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    states = evidence.get("visualStates")
    if not isinstance(states, list):
        raise NeutralCustomAnimationPresentationError("visual state evidence is invalid")
    for state in states:
        if not isinstance(state, Mapping) or state.get("conditions") != ["USER_2"]:
            continue
        assignments = state.get("assignments")
        if not isinstance(assignments, list):
            raise NeutralCustomAnimationPresentationError("USER_2 assignments are invalid")
        for assignment in assignments:
            if not isinstance(assignment, Mapping) or assignment.get("key") != "ParticleSysBone":
                continue
            authored = str(assignment.get("rawValue", ""))
            parts = authored.split()
            if len(parts) != 3 or parts[0] != "None" or parts[2] != "HouseColor:Yes":
                raise NeutralCustomAnimationPresentationError(
                    "USER_2 particle attachment grammar is unsupported"
                )
            provenance = assignment.get("provenance")
            source_ini = provenance.get("virtualPath") if isinstance(provenance, Mapping) else None
            line = provenance.get("line") if isinstance(provenance, Mapping) else None
            if not isinstance(source_ini, str) or not source_ini or not isinstance(line, int) or line <= 0:
                raise NeutralCustomAnimationPresentationError(
                    "USER_2 particle attachment provenance is invalid"
                )
            result.append({
                "particleSystemId": parts[1],
                "bone": parts[0],
                "options": [parts[2]],
                "authored": authored,
                "sourceObject": state.get("sourceObject"),
                "sourceIni": source_ini,
                "line": line,
            })
    result.sort(key=lambda row: str(row["particleSystemId"]).casefold())
    return result


def compile_neutral_custom_animation_presentation(
    descriptor: Mapping[str, object],
    lifecycle_evidence: Mapping[str, object],
    effect_documents: Mapping[str, bytes],
    *,
    texture_index: Mapping[str, str],
    game: str,
) -> dict[str, object] | None:
    edges = _custom_edges(descriptor)
    if not edges:
        return None
    object_id = str(descriptor.get("objectId", ""))
    for edge in edges:
        custom = edge.get("customAnimation")
        if not isinstance(custom, Mapping) or custom.get("animState") != "USER_2" or float(custom.get("animTimeMs", -1)) != 0.0:
            raise NeutralCustomAnimationPresentationError(
                f"{object_id} custom animation request is outside the proven neutral grammar"
            )
    attachments = _attachments(lifecycle_evidence)
    if [row["particleSystemId"] for row in attachments] != list(PARTICLE_IDS):
        raise NeutralCustomAnimationPresentationError(
            f"{object_id} USER_2 does not own the exact UntamedAllegiance attachments"
        )
    try:
        closure = build_ability_fx_closure(
            effect_documents,
            [],
            namespace=f"neutral-custom-animation-{game}",
            texture_index=texture_index,
            particle_ids=PARTICLE_IDS,
        )
        validate_ability_fx_closure(closure)
    except AbilityFxIngressError as exc:
        raise NeutralCustomAnimationPresentationError(str(exc)) from exc
    bindings = closure["runtimeBindings"]
    if (
        bindings.get("authoredParticleSystemIds") != list(PARTICLE_IDS)
        or bindings.get("presentableParticleSystemIds") != list(PARTICLE_IDS)
        or bindings.get("unresolved") != []
    ):
        raise NeutralCustomAnimationPresentationError(
            "UntamedAllegiance particle closure is incomplete"
        )
    request: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "objectId": object_id,
        "animState": "USER_2",
        "animTimeMs": 0.0,
        "edgeIds": sorted(
            [f"{row['effectId']}:{row['upgradeId']}" for row in edges],
            key=str.casefold,
        ),
        "attachments": attachments,
        "particleClosure": closure,
        "runtimeStatus": "deferred",
        "deferredReason": DEFERRED_REASON,
        "activationAllowed": False,
        "particleEmissionAllowed": False,
        "fabricatedClip": False,
    }
    request["requestSha256"] = _digest(request)
    validate_neutral_custom_animation_presentation(request)
    return request


def validate_neutral_custom_animation_presentation(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralCustomAnimationPresentationError("custom animation prerequisite schema is invalid")
    unsigned = dict(value)
    declared = unsigned.pop("requestSha256", None)
    if not isinstance(declared, str) or declared != _digest(unsigned):
        raise NeutralCustomAnimationPresentationError("custom animation prerequisite digest is invalid")
    if (
        value.get("game") not in {"bfme2", "rotwk"}
        or not isinstance(value.get("objectId"), str)
        or value.get("animState") != "USER_2"
        or float(value.get("animTimeMs", -1)) != 0.0
        or value.get("runtimeStatus") != "deferred"
        or value.get("deferredReason") != DEFERRED_REASON
        or value.get("activationAllowed") is not False
        or value.get("particleEmissionAllowed") is not False
        or value.get("fabricatedClip") is not False
    ):
        raise NeutralCustomAnimationPresentationError("custom animation prerequisite activation contract is invalid")
    attachments = value.get("attachments")
    edges = value.get("edgeIds")
    if (
        not isinstance(attachments, list)
        or not isinstance(edges, list)
        or not edges
        or any(not isinstance(edge, str) or not edge for edge in edges)
        or len({str(edge).casefold() for edge in edges}) != len(edges)
    ):
        raise NeutralCustomAnimationPresentationError("custom animation prerequisite evidence is invalid")
    if [row.get("particleSystemId") for row in attachments if isinstance(row, Mapping)] != list(PARTICLE_IDS):
        raise NeutralCustomAnimationPresentationError("custom animation attachment set is invalid")
    if len(attachments) != len(PARTICLE_IDS) or any(
        not isinstance(row, Mapping)
        or row.get("bone") != "None"
        or row.get("options") != ["HouseColor:Yes"]
        or row.get("authored") != f"None {row.get('particleSystemId')} HouseColor:Yes"
        or not isinstance(row.get("sourceIni"), str)
        or int(row.get("line", 0)) <= 0
        for row in attachments
    ):
        raise NeutralCustomAnimationPresentationError("custom animation attachment provenance is invalid")
    closure = value.get("particleClosure")
    if not isinstance(closure, Mapping):
        raise NeutralCustomAnimationPresentationError("custom animation particle closure is missing")
    try:
        validate_ability_fx_closure(closure)
    except AbilityFxIngressError as exc:
        raise NeutralCustomAnimationPresentationError(str(exc)) from exc
    bindings = closure.get("runtimeBindings")
    if not isinstance(bindings, Mapping) or bindings.get("presentableParticleSystemIds") != list(PARTICLE_IDS) or bindings.get("unresolved") != []:
        raise NeutralCustomAnimationPresentationError("custom animation particle closure is not presentable")


__all__ = [
    "DEFERRED_REASON", "PARTICLE_IDS", "SCHEMA", "SCHEMA_VERSION",
    "NeutralCustomAnimationPresentationError",
    "compile_neutral_custom_animation_presentation",
    "validate_neutral_custom_animation_presentation",
]
