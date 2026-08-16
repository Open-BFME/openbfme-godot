"""Bounded composition for retail scenario-neutral structure artifacts.

Neutral lairs and authored scenario structures use the ordinary structure
descriptor, visual-closure recipe, and lifecycle runtime contracts.  This
module is the admission boundary which joins those three generic compilers
without putting the objects on a faction build menu or inventing a construct
route.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy

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
from .playable_unit_compiler import PlayableUnitCompilerInputs


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
) -> dict[str, object]:
    """Compile one exact neutral structure pack artifact or fail closed.

    The caller supplies a sealed retail visual closure.  This function never
    searches a fallback asset root and never admits a production route.
    """

    if game not in {"bfme2", "rotwk"}:
        raise NeutralStructurePackCompilerError(f"unsupported game {game!r}")
    if not isinstance(target_id, str) or not target_id or len(target_id) > 256:
        raise NeutralStructurePackCompilerError("neutral structure identity is invalid")
    admission = _normalized_admission(role, surfaces)
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

    scenario = descriptor["scenarioAdmission"]
    artifact: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "objectId": target_id,
        "role": role,
        "surfaces": list(admission["surfaces"]),
        "sourceIdentity": {
            "declarationKind": scenario["declarationKind"],
            "sourceIni": scenario["sourceIni"],
            "line": scenario["line"],
        },
        "descriptor": descriptor,
        "visualRecipe": recipe,
        "lifecycleEvidence": evidence,
        "runtime": runtime,
    }
    if custom_presentation is not None:
        artifact["customAnimationPresentation"] = custom_presentation
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
    if (
        not isinstance(production, Mapping)
        or production.get("evidence") != "authored-neutral-map"
        or production.get("routes") != []
        or not isinstance(scenario, Mapping)
        or scenario.get("role") != admission["role"]
        or scenario.get("surfaces") != admission["surfaces"]
        or scenario.get("buildCommandExposed") is not False
        or not isinstance(source_identity, Mapping)
        or source_identity
        != {
            "declarationKind": scenario.get("declarationKind"),
            "sourceIni": scenario.get("sourceIni"),
            "line": scenario.get("line"),
        }
    ):
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


__all__ = [
    "NeutralStructurePackCompilerError",
    "ROLES",
    "SCHEMA",
    "SCHEMA_VERSION",
    "SURFACE_ORDER",
    "compile_neutral_structure_pack_artifact",
    "validate_neutral_structure_pack_artifact",
]
