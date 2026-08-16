"""Compose source-backed neutral prop descriptors and conversion recipes.

The twelve bounded neutral props are scenario scenery, not units or buildings.
They still need the same retail W3D dependency closure and pinned conversion
resources as other visible objects.  This module keeps that ownership in a
dedicated pack artifact and emits a descriptor that ContentDB can load through
its ``neutralProp.*`` lane.
"""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy
import re

from .neutral_prop_compiler import (
    NeutralPropCompilerError,
    compile_neutral_prop_descriptor,
    validate_neutral_prop_descriptor,
)
from .playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    _digest,
    compile_structure_visual_recipe,
    validate_structure_visual_recipe,
)
from .playable_unit_pack_compiler import PlayableUnitPackCompilerError
from .profile import assert_input_resource_references_resolve
from .playable_unit_compiler import PlayableUnitCompilerInputs
from .neutral_prop_death_fx import (
    NeutralPropDeathFxError,
    compile_neutral_prop_death_fx,
    validate_neutral_prop_death_fx,
)


SCHEMA = "openbfme.neutral-prop-pack-artifact"
SCHEMA_VERSION = 0
RECIPE_SCHEMA = "openbfme.neutral-prop-pack-recipe"
RECIPE_SCHEMA_VERSION = 0
CONVERSION_PROVENANCE_SCHEMA = "openbfme.neutral-prop-conversion-provenance"


class NeutralPropPackCompilerError(ValueError):
    """A neutral prop cannot be packaged from exact retail evidence."""


def _remap_resource_references(node: object, identifiers: Mapping[str, str]) -> None:
    """Rewrite every declared resource-reference field in a recipe tree."""

    if isinstance(node, list):
        for item in node:
            _remap_resource_references(item, identifiers)
        return
    if not isinstance(node, dict):
        return
    for key, value in list(node.items()):
        if key != "id" and key.endswith(("ResourceId", "resourceId")):
            if not isinstance(value, str) or value not in identifiers:
                raise NeutralPropPackCompilerError(
                    f"visual recipe references an unknown resource: {value!r}"
                )
            node[key] = identifiers[value]
            continue
        if key.endswith(("ResourceIds", "resourceIds")):
            if (
                not isinstance(value, list)
                or any(not isinstance(item, str) or item not in identifiers for item in value)
            ):
                raise NeutralPropPackCompilerError(
                    "visual recipe contains an invalid resource reference list"
                )
            node[key] = [identifiers[item] for item in value]
            continue
        _remap_resource_references(value, identifiers)


def _neutralize_recipe(value: Mapping[str, object]) -> dict[str, object]:
    """Move a validated generic visual recipe into neutral-prop ownership."""

    recipe = deepcopy(value)
    recipe["schema"] = RECIPE_SCHEMA
    recipe["schemaVersion"] = RECIPE_SCHEMA_VERSION
    identifiers: dict[str, str] = {}
    for resource in recipe.get("resources", []):
        old_id = str(resource.get("id", ""))
        if not old_id.startswith("structure-"):
            raise NeutralPropPackCompilerError(
                "generic visual resource is outside structure recipe ownership"
            )
        new_id = "neutral-prop-" + old_id.removeprefix("structure-")
        identifiers[old_id] = new_id
        resource["id"] = new_id
        output = resource.get("output")
        if output is not None:
            prefix = "assets/models/structures/"
            if not isinstance(output, str) or not output.startswith(prefix):
                raise NeutralPropPackCompilerError(
                    "generic model output is outside structure recipe ownership"
                )
            resource["output"] = "assets/models/neutral-props/" + output.removeprefix(
                prefix
            )
    _remap_resource_references(recipe, identifiers)
    for field in ("lifecycleStates", "bibStates"):
        for state in recipe.get(field, []):
            output = state.get("output")
            if output is not None:
                state["output"] = "assets/models/neutral-props/" + str(
                    output
                ).removeprefix("assets/models/structures/")
    recipe.pop("recipeSha256", None)
    recipe["recipeSha256"] = _digest(recipe)
    return recipe


def _structure_recipe(value: Mapping[str, object]) -> dict[str, object]:
    """Recover the generic form solely to reuse its strict validator."""

    recipe = deepcopy(value)
    recipe["schema"] = "openbfme.playable-structure-pack-recipe"
    recipe["schemaVersion"] = 1
    identifiers: dict[str, str] = {}
    fx_resources = [
        row for row in recipe.get("resources", [])
        if isinstance(row, Mapping)
        and row.get("converter") in {"texture", "sage-particle-definition", "audio"}
    ]
    recipe["resources"] = [
        row for row in recipe.get("resources", [])
        if row not in fx_resources
    ]
    recipe.pop("deathFxBinding", None)
    for resource in recipe.get("resources", []):
        old_id = str(resource.get("id", ""))
        if not old_id.startswith("neutral-prop-"):
            raise NeutralPropPackCompilerError(
                "neutral prop resource id is outside dedicated ownership"
            )
        new_id = "structure-" + old_id.removeprefix("neutral-prop-")
        identifiers[old_id] = new_id
        resource["id"] = new_id
        output = resource.get("output")
        if output is not None:
            prefix = "assets/models/neutral-props/"
            if not isinstance(output, str) or not output.startswith(prefix):
                raise NeutralPropPackCompilerError(
                    "neutral prop model output is outside dedicated ownership"
                )
            resource["output"] = "assets/models/structures/" + output.removeprefix(
                prefix
            )
    _remap_resource_references(recipe, identifiers)
    for field in ("lifecycleStates", "bibStates"):
        for state in recipe.get(field, []):
            output = state.get("output")
            if output is not None:
                prefix = "assets/models/neutral-props/"
                if not isinstance(output, str) or not output.startswith(prefix):
                    raise NeutralPropPackCompilerError(
                        "neutral prop state output is outside dedicated ownership"
                    )
                state["output"] = "assets/models/structures/" + output.removeprefix(
                    prefix
                )
    recipe.pop("recipeSha256", None)
    recipe["recipeSha256"] = _digest(recipe)
    return recipe


def validate_neutral_prop_visual_recipe(value: Mapping[str, object]) -> None:
    if value.get("schema") != RECIPE_SCHEMA or value.get(
        "schemaVersion"
    ) != RECIPE_SCHEMA_VERSION:
        raise NeutralPropPackCompilerError("neutral prop recipe schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("recipeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralPropPackCompilerError("neutral prop recipe digest is invalid")
    try:
        resources = value.get("resources")
        if not isinstance(resources, list):
            raise NeutralPropPackCompilerError(
                "neutral prop recipe resources are invalid"
            )
        assert_input_resource_references_resolve(
            resources, label="neutral prop visual recipe"
        )
        validate_structure_visual_recipe(_structure_recipe(value))
    except (
        ValueError,
        PlayableStructurePackCompilerError,
        PlayableUnitPackCompilerError,
    ) as exc:
        raise NeutralPropPackCompilerError(
            f"neutral prop recipe contains invalid conversion evidence: {exc}"
        ) from exc
    exclusions = value.get("exclusions")
    if not isinstance(exclusions, list):
        raise NeutralPropPackCompilerError("neutral prop recipe exclusions are invalid")
    unresolved_render_inputs = [
        row
        for row in exclusions
        if isinstance(row, Mapping) and row.get("kind") in {"model", "texture"}
    ]
    if unresolved_render_inputs:
        raise NeutralPropPackCompilerError(
            "neutral prop recipe contains a missing or excluded model/texture"
        )
    fx_binding = value.get("deathFxBinding")
    fx_resources = [
        row for row in value.get("resources", [])
        if isinstance(row, Mapping)
        and row.get("converter") in {"texture", "sage-particle-definition", "audio"}
    ]
    if fx_binding is None:
        if fx_resources:
            raise NeutralPropPackCompilerError("neutral prop has unbound FX resources")
    elif not isinstance(fx_binding, Mapping) or not fx_resources:
        raise NeutralPropPackCompilerError("neutral prop death FX recipe is incomplete")


def _sha(value: object, label: str) -> str:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise NeutralPropPackCompilerError(f"{label} is invalid")
    return value


def _model_state(recipe: Mapping[str, object], object_id: str) -> Mapping[str, object]:
    raw_states = recipe.get("lifecycleStates")
    if not isinstance(raw_states, list):
        raise NeutralPropPackCompilerError("neutral prop visual states are invalid")
    states = [
        row
        for row in raw_states
        if isinstance(row, Mapping) and row.get("phases") == ["intact"]
    ]
    if len(states) != 1 or len(raw_states) != 1:
        raise NeutralPropPackCompilerError(
            f"neutral prop {object_id} does not have one exact intact model"
        )
    state = states[0]
    if (
        not isinstance(state.get("sourceW3d"), str)
        or not state.get("sourceW3d")
        or not isinstance(state.get("resourceId"), str)
        or not state.get("resourceId")
        or not isinstance(state.get("output"), str)
        or not str(state.get("output")).casefold().endswith(".glb")
    ):
        raise NeutralPropPackCompilerError("neutral prop intact model is invalid")
    return state


def _resource_ownership(
    recipe: Mapping[str, object], state: Mapping[str, object]
) -> dict[str, object]:
    raw_resources = recipe.get("resources")
    if not isinstance(raw_resources, list) or not raw_resources:
        raise NeutralPropPackCompilerError("neutral prop has no conversion resources")
    resources: list[dict[str, object]] = []
    identifiers: set[str] = set()
    outputs: set[str] = set()
    for raw in raw_resources:
        if not isinstance(raw, Mapping):
            raise NeutralPropPackCompilerError("neutral prop resource is invalid")
        identifier = raw.get("id")
        kind = raw.get("kind")
        converter = raw.get("converter")
        patterns = raw.get("patterns")
        if (
            not isinstance(identifier, str)
            or not identifier
            or identifier.casefold() in identifiers
            or kind not in {"model", "texture", "data", "audio"}
            or converter not in {
                "hash-only",
                "w3d-static",
                "w3d-hierarchical",
                "w3d-bundle",
                "texture",
                "sage-particle-definition",
                "audio",
            }
            or not isinstance(patterns, list)
            or not patterns
            or any(not isinstance(path, str) or not path for path in patterns)
            or raw.get("required") is not True
            or raw.get("limit") != len(patterns)
            or raw.get("expected_count") != len(patterns)
        ):
            raise NeutralPropPackCompilerError(
                "neutral prop conversion resource contract is invalid"
            )
        identifiers.add(identifier.casefold())
        row: dict[str, object] = {
            "id": identifier,
            "kind": kind,
            "converter": converter,
            "sourceVirtualPaths": list(patterns),
        }
        output = raw.get("output")
        if kind == "model":
            if (
                not isinstance(output, str)
                or not output.casefold().endswith(".glb")
                or output.casefold() in outputs
            ):
                raise NeutralPropPackCompilerError(
                    "neutral prop model output ownership is invalid"
                )
            outputs.add(output.casefold())
            row["output"] = output
        elif kind == "texture":
            if converter == "texture":
                if not isinstance(output, str) or not output.casefold().endswith(".png"):
                    raise NeutralPropPackCompilerError("neutral prop FX texture output is invalid")
                row["output"] = output
            elif output is not None:
                raise NeutralPropPackCompilerError(
                    "neutral prop visual texture cannot own an output"
                )
        elif kind == "data":
            if (
                converter != "sage-particle-definition"
                or not isinstance(output, str)
                or not output.casefold().endswith(".json")
            ):
                raise NeutralPropPackCompilerError("neutral prop FX definition output is invalid")
            row["output"] = output
        elif kind == "audio":
            if (
                converter != "audio"
                or not isinstance(output, str)
                or not output.casefold().endswith(".wav")
            ):
                raise NeutralPropPackCompilerError("neutral prop FX audio output is invalid")
            row["output"] = output
        resources.append(row)
    model_id = str(state["resourceId"])
    if model_id.casefold() not in identifiers:
        raise NeutralPropPackCompilerError(
            "neutral prop intact state references an unowned model resource"
        )
    return {
        "resourceIds": sorted(
            (str(row["id"]) for row in resources), key=str.casefold
        ),
        "modelResourceId": model_id,
        "resources": resources,
    }


def compile_neutral_prop_pack_artifact(
    target_id: str,
    documents: Mapping[str, bytes],
    visual_closure: Mapping[str, object],
    *,
    prepared: PlayableUnitCompilerInputs | None = None,
    game: str = "bfme2",
    effect_documents: Mapping[str, bytes] | None = None,
    fx_texture_index: Mapping[str, str] | None = None,
    audio_sample_index: Mapping[str, str] | None = None,
) -> dict[str, object]:
    """Compile one ContentDB-loadable prop runtime plus its owned resources."""

    try:
        descriptor = compile_neutral_prop_descriptor(
            target_id, documents, prepared=prepared, game=game
        )
        generic_recipe = compile_structure_visual_recipe(target_id, visual_closure)
        validate_structure_visual_recipe(generic_recipe)
        recipe = _neutralize_recipe(generic_recipe)
        death_fx = None
        if any(
            isinstance(row, Mapping) and row.get("module") == "FXListDie"
            for row in descriptor.get("moduleContracts", [])
        ):
            if (
                effect_documents is None
                or fx_texture_index is None
                or audio_sample_index is None
            ):
                raise NeutralPropPackCompilerError(
                    "neutral prop FXListDie requires effective FX documents and texture index"
                )
            death_fx = compile_neutral_prop_death_fx(
                descriptor, effect_documents, texture_index=fx_texture_index,
                audio_sample_index=audio_sample_index,
            )
            assert death_fx is not None
            recipe["resources"].extend(deepcopy(death_fx["resources"]))
            recipe["deathFxBinding"] = deepcopy(death_fx["runtimeBinding"])
            recipe.pop("recipeSha256", None)
            recipe["recipeSha256"] = _digest(recipe)
        validate_neutral_prop_visual_recipe(recipe)
        state = _model_state(recipe, target_id)
        ownership = _resource_ownership(recipe, state)
    except NeutralPropPackCompilerError:
        raise
    except (
        NeutralPropCompilerError,
        PlayableStructurePackCompilerError,
        PlayableUnitPackCompilerError,
        NeutralPropDeathFxError,
    ) as exc:
        raise NeutralPropPackCompilerError(
            f"neutral prop {target_id} is not pack-ready: {exc}"
        ) from exc

    runtime = deepcopy(descriptor)
    presentation = runtime.get("presentation")
    assert isinstance(presentation, dict)
    presentation["convertedVisual"] = {
        "mode": "glb",
        "glb": state["output"],
        "modelResourceId": state["resourceId"],
        "sourceW3d": state["sourceW3d"],
        "sourceConditionSets": deepcopy(state.get("sourceConditionSets", [])),
        "animationClipIds": deepcopy(state.get("animationClipIds", [])),
    }
    if death_fx is not None:
        presentation["deathFxBinding"] = deepcopy(death_fx["runtimeBinding"])
    runtime["resourceOwnership"] = ownership
    runtime["conversionProvenance"] = {
        "schema": CONVERSION_PROVENANCE_SCHEMA,
        "visualClosureSha256": recipe["visualClosureSha256"],
        "recipeSha256": recipe["recipeSha256"],
        "sourceW3d": state["sourceW3d"],
        "sourceTextureVirtualPaths": sorted(
            {
                path
                for row in ownership["resources"]
                if row["kind"] == "texture"
                for path in row["sourceVirtualPaths"]
            },
            key=str.casefold,
        ),
    }
    runtime.pop("descriptorSha256", None)
    runtime["descriptorSha256"] = _digest(runtime)
    validate_neutral_prop_descriptor(runtime)

    artifact: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "objectId": target_id,
        "descriptor": descriptor,
        "visualRecipe": recipe,
        "runtime": runtime,
        "resourceOwnership": ownership,
        "provenance": {
            "descriptorSha256": descriptor["descriptorSha256"],
            "runtimeDescriptorSha256": runtime["descriptorSha256"],
            "visualClosureSha256": recipe["visualClosureSha256"],
            "recipeSha256": recipe["recipeSha256"],
            **(
                {"deathFxClosureSha256": death_fx["aggregateSha256"]}
                if death_fx is not None else {}
            ),
        },
        **({"deathFxClosure": death_fx} if death_fx is not None else {}),
    }
    artifact["artifactSha256"] = _digest(artifact)
    validate_neutral_prop_pack_artifact(artifact)
    return artifact


def validate_neutral_prop_pack_artifact(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralPropPackCompilerError("neutral prop artifact schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("artifactSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralPropPackCompilerError("neutral prop artifact digest is invalid")
    object_id = value.get("objectId")
    if value.get("game") not in {"bfme2", "rotwk"} or not isinstance(object_id, str):
        raise NeutralPropPackCompilerError("neutral prop artifact identity is invalid")
    descriptor = value.get("descriptor")
    recipe = value.get("visualRecipe")
    runtime = value.get("runtime")
    if not all(isinstance(row, Mapping) for row in (descriptor, recipe, runtime)):
        raise NeutralPropPackCompilerError("neutral prop artifact documents are invalid")
    assert isinstance(descriptor, Mapping)
    assert isinstance(recipe, Mapping)
    assert isinstance(runtime, Mapping)
    try:
        validate_neutral_prop_descriptor(descriptor)
        validate_neutral_prop_descriptor(runtime)
        validate_neutral_prop_visual_recipe(recipe)
    except (
        NeutralPropCompilerError,
        PlayableStructurePackCompilerError,
        PlayableUnitPackCompilerError,
    ) as exc:
        raise NeutralPropPackCompilerError(
            f"neutral prop artifact contains invalid evidence: {exc}"
        ) from exc
    if {
        str(descriptor.get("objectId", "")),
        str(recipe.get("objectId", "")),
        str(runtime.get("objectId", "")),
    } != {object_id}:
        raise NeutralPropPackCompilerError("neutral prop artifact identity drifted")
    for field in ("moduleContracts", "runtimeModuleEvidence", "runtimeCapabilities"):
        if runtime.get(field) != descriptor.get(field):
            raise NeutralPropPackCompilerError(
                f"neutral prop runtime {field} drifted from source descriptor"
            )
    state = _model_state(recipe, object_id)
    ownership = _resource_ownership(recipe, state)
    if value.get("resourceOwnership") != ownership or runtime.get(
        "resourceOwnership"
    ) != ownership:
        raise NeutralPropPackCompilerError("neutral prop resource ownership drifted")
    converted = runtime.get("presentation", {}).get("convertedVisual")
    if not isinstance(converted, Mapping) or converted != {
        "mode": "glb",
        "glb": state["output"],
        "modelResourceId": state["resourceId"],
        "sourceW3d": state["sourceW3d"],
        "sourceConditionSets": state.get("sourceConditionSets", []),
        "animationClipIds": state.get("animationClipIds", []),
    }:
        raise NeutralPropPackCompilerError("neutral prop converted visual drifted")
    death_fx = value.get("deathFxClosure")
    recipe_death = recipe.get("deathFxBinding")
    runtime_death = runtime.get("presentation", {}).get("deathFxBinding")
    if death_fx is None:
        if recipe_death is not None or runtime_death is not None:
            raise NeutralPropPackCompilerError("neutral prop death FX binding is unowned")
    else:
        if not isinstance(death_fx, Mapping):
            raise NeutralPropPackCompilerError("neutral prop death FX closure is invalid")
        validate_neutral_prop_death_fx(death_fx)
        if recipe_death != death_fx.get("runtimeBinding") or runtime_death != recipe_death:
            raise NeutralPropPackCompilerError("neutral prop death FX binding drifted")
    conversion = runtime.get("conversionProvenance")
    provenance = value.get("provenance")
    expected_textures = sorted(
        {
            path
            for row in ownership["resources"]
            if row["kind"] == "texture"
            for path in row["sourceVirtualPaths"]
        },
        key=str.casefold,
    )
    if (
        not isinstance(conversion, Mapping)
        or conversion.get("schema") != CONVERSION_PROVENANCE_SCHEMA
        or conversion.get("visualClosureSha256") != recipe.get("visualClosureSha256")
        or conversion.get("recipeSha256") != recipe.get("recipeSha256")
        or conversion.get("sourceW3d") != state.get("sourceW3d")
        or conversion.get("sourceTextureVirtualPaths") != expected_textures
        or not isinstance(provenance, Mapping)
        or provenance
        != {
            "descriptorSha256": descriptor.get("descriptorSha256"),
            "runtimeDescriptorSha256": runtime.get("descriptorSha256"),
            "visualClosureSha256": recipe.get("visualClosureSha256"),
            "recipeSha256": recipe.get("recipeSha256"),
            **(
                {"deathFxClosureSha256": death_fx.get("aggregateSha256")}
                if isinstance(death_fx, Mapping) else {}
            ),
        }
    ):
        raise NeutralPropPackCompilerError("neutral prop conversion provenance drifted")
    for key in ("descriptorSha256", "runtimeDescriptorSha256", "visualClosureSha256", "recipeSha256"):
        _sha(provenance.get(key), f"neutral prop provenance {key}")
    if isinstance(death_fx, Mapping):
        _sha(provenance.get("deathFxClosureSha256"), "neutral prop death FX provenance")


__all__ = [
    "CONVERSION_PROVENANCE_SCHEMA",
    "NeutralPropPackCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "RECIPE_SCHEMA",
    "RECIPE_SCHEMA_VERSION",
    "compile_neutral_prop_pack_artifact",
    "validate_neutral_prop_pack_artifact",
    "validate_neutral_prop_visual_recipe",
]
