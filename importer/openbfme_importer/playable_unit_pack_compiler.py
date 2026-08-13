"""Descriptor-driven playable-unit conversion recipe compiler."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import PurePosixPath
import re
from typing import Iterable, Mapping

from .paths import safe_relative_parts
from .playable_unit_compiler import validate_playable_unit_descriptor
from .profile import (
    ALLOWED_CONVERTERS,
    ALLOWED_KINDS,
    MAX_PATH_LENGTH,
    MAX_PATTERNS_PER_RESOURCE,
    SLUG_PATTERN,
    normalize_texture_atlas_crops,
)
from .retail_ability_fx_ingress import AbilityFxIngressError, fx_recipe_parts


SCHEMA = "openbfme.playable-unit-pack-recipe"
SCHEMA_VERSION = 0
_CORE_ORDER = ("idle", "move", "attack", "death")
# Optional capability states. Not required to compile a unit; projected when
# the Object authors them. Retail SELECTED (gondorfighter.ini:519-531) loops
# ATNB; TransitionState TRANS_IdleToSelected (:664-670) plays ATNA once.
_SELECTION_STATES = ("selectionTransition", "selected")
_ATTACK_TOKENS = {
    "ATTACKING",
    "FIRING_A",
    "FIRING_B",
    "FIRING_C",
    "FIRING_D",
    "FIRING_E",
    "PREATTACK_A",
    "PREATTACK_B",
    "PREATTACK_C",
    "SPECIAL_WEAPON_ONE",
    "SPECIAL_WEAPON_TWO",
    "SPECIAL_WEAPON_THREE",
}
# Retail model conditions that name an *addressable* special ability rather
# than a generic combat pose. `_state()` deliberately still folds the
# SPECIAL_WEAPON_* rows into "attack" so a hero whose only attack pose is a
# special weapon (retail Drogoth) keeps a resolved core attack state; this
# vocabulary is the parallel, additive lane that makes the same clip
# *distinguishable* so an ability cast can request its own authored animation.
#
# The join back to a specific ability is authored in the retail INI:
#   * `WeaponFireSpecialAbilityUpdate.WhichSpecialWeapon = N`  -> SPECIAL_WEAPON_<N>
#     (gandalf.ini:1297-1312 binds SpecialAbilityWizardBlast to
#      WhichSpecialWeapon 2, and gandalf.ini:305 binds SPECIAL_WEAPON_TWO to
#      GUGandalfG_SKL.GUGandalfG_SPCL — the staff-lowering wizard blast pose.)
#   * `ArrowStormUpdate.UnpackingVariation = N`                -> PACKING_TYPE_<N>
#     (gandalf.ini:1327-1330 binds SpecialAbilityLightningSword to
#      UnpackingVariation 1, and gandalf.ini:343-370 authors the
#      PACKING_TYPE_1 UNPACKING/PREPARING/PACKING envelope.)
_ABILITY_STATE_TOKENS = {
    "SPECIAL_WEAPON_ONE": "specialWeaponOne",
    "SPECIAL_WEAPON_TWO": "specialWeaponTwo",
    "SPECIAL_WEAPON_THREE": "specialWeaponThree",
    "USING_SPECIAL_ABILITY": "usingSpecialAbility",
}
# Retail channels a special ability through a four-phase envelope: the
# UNPACKING wind-up, the PREPARING channel, the PACKING recovery, and the bare
# condition for the fire frame itself (boromir.ini:127-178 authors all four for
# SPECIAL_POWER_1 — HRNA/HRNB/HRNC/HRNB).
_ABILITY_PHASE_TOKENS = {
    "UNPACKING": "unpack",
    "PREPARING": "prepare",
    "PACKING": "pack",
}
_ABILITY_DEFAULT_PHASE = "cast"
_ABILITY_PHASE_ORDER = ("unpack", "prepare", "cast", "pack")
_SPECIAL_POWER_CONDITION = re.compile(r"^SPECIAL_POWER_(\d+)$")
_PACKING_TYPE_CONDITION = re.compile(r"^PACKING_TYPE_(\d+)$")
_EXCLUDED_HERO_FORM_CONDITIONS = {
    "hero",
    "mounted",
    "user_1",
    "user_2",
    "user_3",
}


def _is_required_galadriel_ring_skin(row: Mapping[str, object]) -> bool:
    """The Ring Hero's USER_1 skin is gameplay state, not an optional form.

    Retail's child-only ModelConditionUpgrade raises USER_1 at object level 1
    and swaps to EUGaldrl_SKN.  Treating every USER_1 hero row as an excluded
    form silently removed the actual Ring Hero appearance from the pack.
    """

    conditions = row.get("conditions")
    return (
        str(row.get("identifier", "")).casefold() == "eugaldrl_skn"
        and isinstance(conditions, list)
        and {str(value).casefold() for value in conditions} == {"user_1"}
    )


class PlayableUnitPackCompilerError(ValueError):
    """A source-backed playable unit cannot produce one bounded pack recipe."""


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    if not result:
        raise PlayableUnitPackCompilerError("unit identifier has no safe slug")
    return result


def _w3d_rig_family(value: str) -> str:
    key = value.casefold()
    for marker in ("_skl", "_skn"):
        if marker in key:
            return key.split(marker, 1)[0]
    return ""


def _resource_id(*parts: str) -> str:
    candidate = "-".join(_slug(part) for part in parts)
    if len(candidate) <= 64 and SLUG_PATTERN.fullmatch(candidate):
        return candidate
    digest = hashlib.sha256(candidate.encode()).hexdigest()[:12]
    shortened = f"{candidate[:51].rstrip('-')}-{digest}"
    if SLUG_PATTERN.fullmatch(shortened) is None:
        raise PlayableUnitPackCompilerError("resource id cannot be normalized")
    return shortened


def _safe_path(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or len(value) > MAX_PATH_LENGTH:
        raise PlayableUnitPackCompilerError(f"{label} is invalid")
    try:
        return "/".join(safe_relative_parts(value))
    except ValueError as exc:
        raise PlayableUnitPackCompilerError(f"{label} is unsafe") from exc


def _rows(value: object, label: str) -> list[Mapping[str, object]]:
    if not isinstance(value, list) or any(
        not isinstance(row, Mapping) for row in value
    ):
        raise PlayableUnitPackCompilerError(f"{label} is invalid")
    return list(value)


def _paths(row: Mapping[str, object], label: str) -> tuple[str, ...]:
    raw = row.get("physicalVirtualPaths")
    if (
        not isinstance(raw, list)
        or not raw
        or any(not isinstance(path, str) or not path for path in raw)
    ):
        raise PlayableUnitPackCompilerError(f"{label} has invalid physical paths")
    normalized = [_safe_path(path, f"{label} path") for path in raw]
    if len({item.casefold() for item in normalized}) != len(normalized):
        raise PlayableUnitPackCompilerError(f"{label} has duplicate physical paths")
    return tuple(sorted(normalized, key=lambda item: (item.casefold(), item)))


def _draw_key(row: Mapping[str, object]) -> tuple[str, str]:
    target = str(row.get("targetObject", ""))
    provenance = row.get("provenance")
    if not target or not isinstance(provenance, Mapping):
        raise PlayableUnitPackCompilerError("visual draw provenance is invalid")
    scope = provenance.get("scopePath")
    if not isinstance(scope, list) or not scope or not isinstance(scope[0], str):
        raise PlayableUnitPackCompilerError("visual draw scope is missing")
    return target.casefold(), scope[0].casefold()


def _state(row: Mapping[str, object]) -> str | None:
    raw_conditions = row.get("conditions", [])
    if not isinstance(raw_conditions, list) or any(
        not isinstance(item, str) for item in raw_conditions
    ):
        raise PlayableUnitPackCompilerError("visual conditions are invalid")
    conditions = {item.upper() for item in raw_conditions}
    provenance = row.get("provenance", {})
    if not isinstance(provenance, Mapping):
        raise PlayableUnitPackCompilerError("visual provenance is invalid")
    scope = provenance.get("scopePath", [])
    if not isinstance(scope, list) or any(not isinstance(item, str) for item in scope):
        raise PlayableUnitPackCompilerError("visual scope path is invalid")
    scope_text = " ".join(scope).upper()
    if _is_idle_to_selected_state(conditions, scope_text):
        return "selectionTransition"
    if "SELECTED" in conditions:
        return "selected"
    if "DYING" in conditions or any(item.startswith("DEATH_") for item in conditions):
        return "death"
    if conditions & _ATTACK_TOKENS or "ATTACK" in scope_text or "FIRING" in scope_text:
        return "attack"
    # Pure turn clips are movement-compatible fallbacks, but are not the core
    # WALK state. Combined rows keep the meaning of their non-turn tokens: a
    # retail {MOVING, TURN_LEFT} row is still authored movement evidence.
    turn_tokens = {
        item
        for item in conditions
        if item.startswith("TURN_LEFT") or item.startswith("TURN_RIGHT")
    }
    if turn_tokens and conditions == turn_tokens:
        return None
    if "MOVING" in conditions or "MOVE" in scope_text or "LOCOMOTOR" in scope_text:
        return "move"
    if not conditions and "IDLEANIMATIONSTATE" in scope_text.replace(" ", ""):
        return "idle"
    # Cavalry / morph banners (GondorCavalryBanner, Rohan mounted form) author
    # their default pose as AnimationState USER_N rather than IdleAnimationState.
    # A single USER_* morph flag is that form's idle; multi-condition combat
    # rows still fall through as non-core.
    if (
        len(conditions) == 1
        and next(iter(conditions)).startswith("USER_")
        and "ANIMATIONSTATE" in scope_text.replace(" ", "")
        and "TRANSITIONSTATE" not in scope_text.replace(" ", "")
    ):
        return "idle"
    return None


def _compact_token(value: str) -> str:
    return value.upper().replace(" ", "").replace("_", "").replace("-", "")


def _is_idle_to_selected_state(conditions: set[str], scope_text: str) -> bool:
    """True for TransitionState TRANS_IdleToSelected / TRANS_Idle_to_Selected.

    Compiled packs store that name as a condition token on the ATNA row
    (gondorfighterhorde.json GUManMocap_ATNA). TRANS_SelectedToIdle / ATND
    is the leave-attention clip and must stay unmapped.
    """

    tokens = {_compact_token(item) for item in conditions}
    tokens.add(_compact_token(scope_text))
    return any("IDLETOSELECTED" in token for token in tokens)


def _is_turn_animation(row: Mapping[str, object]) -> bool:
    raw = row.get("conditions", [])
    return isinstance(raw, list) and any(
        str(item).upper().startswith(("TURN_LEFT", "TURN_RIGHT")) for item in raw
    )


def _ability_animation_key(conditions: Iterable[object]) -> tuple[str, str] | None:
    """Ability state and envelope phase named by one AnimationState's conditions.

    Returns ``None`` when the row names no addressable ability, and also when it
    names more than one — two ability tokens on a single AnimationState cannot
    be addressed unambiguously, so the row stays a packaged-unimplemented clip
    rather than being guessed at.
    """
    tokens = {str(item).upper() for item in conditions}
    state = ""
    for token in sorted(tokens):
        mapped = _ABILITY_STATE_TOKENS.get(token)
        if mapped is None:
            power = _SPECIAL_POWER_CONDITION.match(token)
            if power is not None:
                mapped = f"specialPower{int(power.group(1))}"
        if mapped is None:
            packing = _PACKING_TYPE_CONDITION.match(token)
            if packing is not None:
                mapped = f"packingType{int(packing.group(1))}"
        if mapped is None:
            continue
        if state and state != mapped:
            return None
        state = mapped
    if not state:
        return None
    phase = _ABILITY_DEFAULT_PHASE
    for token, mapped_phase in _ABILITY_PHASE_TOKENS.items():
        if token in tokens:
            if phase != _ABILITY_DEFAULT_PHASE:
                return None
            phase = mapped_phase
    return state, phase


def _ability_animations(
    authored_rows: Iterable[Mapping[str, object]],
) -> dict[str, dict[str, list[dict[str, object]]]]:
    """Addressable ability clips, keyed by ability state then envelope phase.

    Built from the same authored animation rows the core lane consumes, so a
    clip excluded there (zero-byte retail placeholder, unsupported rig) is
    excluded here too. Purely additive: nothing is removed from
    `coreAnimations`, and a unit with no authored ability pose emits nothing.
    """
    result: dict[str, dict[str, list[dict[str, object]]]] = {}
    for row in authored_rows:
        if str(row.get("runtimeSupport", "")).startswith("excluded"):
            continue
        conditions = row.get("conditions", [])
        if not isinstance(conditions, list):
            continue
        key = _ability_animation_key(conditions)
        if key is None:
            continue
        state, phase = key
        result.setdefault(state, {}).setdefault(phase, []).append(
            {
                "identifier": str(row.get("identifier", "")),
                "conditions": [str(item) for item in conditions],
                "sourceW3d": str(row.get("sourceW3d", "")),
                "modelSourceW3d": str(row.get("modelSourceW3d", "")),
                "ownerObjectId": str(row.get("ownerObjectId", "")),
            }
        )
    for phases in result.values():
        for bindings in phases.values():
            bindings.sort(
                key=lambda binding: (
                    str(binding["sourceW3d"]).casefold(),
                    str(binding["identifier"]).casefold(),
                )
            )
    return {
        state: {
            phase: phases[phase] for phase in _ABILITY_PHASE_ORDER if phase in phases
        }
        for state, phases in sorted(result.items())
    }


def _semantic_effect(reason: str) -> str:
    return {
        "sage-none-model": "hide-draw-module",
        "sage-model-skeleton": "use-active-model-skeleton",
    }.get(reason, "preserve-authored-semantic")


def _required_states(descriptor: Mapping[str, object]) -> tuple[str, ...]:
    capabilities = _rows(descriptor.get("capabilities"), "descriptor capabilities")
    ids = {str(row.get("id", "")).casefold() for row in capabilities}
    required = {"idle"}
    if "move" in ids:
        required.add("move")
    if ids & {"attack", "ranged-attack", "siege-attack"}:
        required.add("attack")
    if ids & {"death", "member-death"}:
        required.add("death")
    return tuple(state for state in _CORE_ORDER if state in required)


def _hierarchy_dependencies(
    animation_paths: Iterable[str], scanned_rows: list[Mapping[str, object]]
) -> tuple[str, ...]:
    scanned = {
        str(row.get("virtualPath", "")).casefold(): row
        for row in scanned_rows
        if isinstance(row.get("virtualPath"), str)
    }
    hierarchy_ids: set[str] = set()
    for path in animation_paths:
        row = scanned.get(path.casefold())
        if row is None:
            raise PlayableUnitPackCompilerError(f"animation is not scanned: {path}")
        headers = row.get("headerIds")
        if not isinstance(headers, Mapping):
            raise PlayableUnitPackCompilerError(
                f"animation headers are invalid: {path}"
            )
        ids = headers.get("animationIds", [])
        if not isinstance(ids, list):
            raise PlayableUnitPackCompilerError(f"animation ids are invalid: {path}")
        for identifier in ids:
            if not isinstance(identifier, str):
                raise PlayableUnitPackCompilerError("animation id is invalid")
            if "." in identifier:
                hierarchy = identifier.split(".", 1)[0].casefold()
                hierarchy_ids.add(hierarchy)
    owners: dict[str, set[str]] = {identifier: set() for identifier in hierarchy_ids}
    for row in scanned_rows:
        headers = row.get("headerIds")
        if not isinstance(headers, Mapping):
            continue
        authored = {str(value).casefold() for value in headers.get("hierarchyIds", [])}
        matches = authored & hierarchy_ids
        if matches:
            path = row.get("virtualPath")
            if not isinstance(path, str) or not path:
                raise PlayableUnitPackCompilerError("hierarchy path is invalid")
            for identifier in matches:
                owners[identifier].add(path)
    result: set[str] = set()
    for hierarchy, paths in owners.items():
        if not paths:
            raise PlayableUnitPackCompilerError(
                f"animation hierarchy is not backed by a scanned W3D: {hierarchy}"
            )
        if len(paths) != 1:
            raise PlayableUnitPackCompilerError(
                f"animation hierarchy ownership is ambiguous: {hierarchy}"
            )
        result.update(paths)
    return tuple(sorted(result, key=lambda item: (item.casefold(), item)))


def _model_embedded_animation(
    model_path: str, scanned_rows: list[Mapping[str, object]]
) -> bool:
    """Return whether the model embeds a keyed animation of its own.

    The exact embedded-animation conversion shape (animations=[model]) only
    exists when the model actually embeds keyed channels; a header-only chunk
    keys nothing and is vacuous evidence.
    """

    scanned = {
        str(row.get("virtualPath", "")).casefold(): row
        for row in scanned_rows
        if isinstance(row.get("virtualPath"), str)
    }
    row = scanned.get(model_path.casefold())
    if row is None:
        raise PlayableUnitPackCompilerError(f"model is not scanned: {model_path}")
    headers = row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableUnitPackCompilerError("model headers are invalid: " + model_path)
    animation_ids = headers.get("animationIds", [])
    if not isinstance(animation_ids, list) or any(
        not isinstance(value, str) for value in animation_ids
    ):
        raise PlayableUnitPackCompilerError(
            "model animation ids are invalid: " + model_path
        )
    if not animation_ids:
        return False
    channel_count = row.get("embeddedAnimationChannelCount", 0)
    if (
        isinstance(channel_count, bool)
        or not isinstance(channel_count, int)
        or channel_count < 0
    ):
        raise PlayableUnitPackCompilerError(
            "model embedded animation channel count is invalid: " + model_path
        )
    return channel_count > 0


def _model_has_hierarchy(model_path: str, scanned_rows: list[Mapping[str, object]]) -> bool:
    """Return whether the model resolves any pivots (embedded or external).

    A unit model with no hierarchy at all is a plain static mesh (retail's
    authored placeholder models, e.g. ``Invisible``); the adapter's static
    conversion is its exact shape.  Only models that actually resolve pivots
    take the hierarchical contract.
    """

    scanned = {
        str(row.get("virtualPath", "")).casefold(): row
        for row in scanned_rows
        if isinstance(row.get("virtualPath"), str)
    }
    row = scanned.get(model_path.casefold())
    if row is None:
        raise PlayableUnitPackCompilerError(f"model is not scanned: {model_path}")
    headers = row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableUnitPackCompilerError("model headers are invalid: " + model_path)
    hierarchy_ids = headers.get("hierarchyIds", [])
    if not isinstance(hierarchy_ids, list) or any(
        not isinstance(value, str) for value in hierarchy_ids
    ):
        raise PlayableUnitPackCompilerError(
            "model hierarchy ids are invalid: " + model_path
        )
    if hierarchy_ids:
        return True
    referenced = row.get("modelHierarchyIdentifiers", [])
    if not isinstance(referenced, list) or any(
        not isinstance(value, str) for value in referenced
    ):
        raise PlayableUnitPackCompilerError(
            "model hierarchy identifiers are invalid: " + model_path
        )
    return bool(referenced)


def _model_hierarchy_dependencies(
    model_path: str, scanned_rows: list[Mapping[str, object]]
) -> tuple[str, ...]:
    """Return the external hierarchy sources the model's HLod headers need.

    A model whose hierarchy lives in a sibling W3D converts only when that
    provider is staged next to it.  The exact hierarchy identifiers come from
    the scanned model headers; each must be authored by exactly one scanned
    provider, which stays fail-closed for missing or ambiguous providers.
    """

    scanned = {
        str(row.get("virtualPath", "")).casefold(): row
        for row in scanned_rows
        if isinstance(row.get("virtualPath"), str)
    }
    row = scanned.get(model_path.casefold())
    if row is None:
        raise PlayableUnitPackCompilerError(f"model is not scanned: {model_path}")
    headers = row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableUnitPackCompilerError("model headers are invalid: " + model_path)
    authored = {str(value).casefold() for value in headers.get("hierarchyIds", [])}
    referenced = row.get("modelHierarchyIdentifiers", [])
    if not isinstance(referenced, list) or any(
        not isinstance(value, str) for value in referenced
    ):
        raise PlayableUnitPackCompilerError(
            "model hierarchy identifiers are invalid: " + model_path
        )
    external = {str(value).casefold() for value in referenced} - authored
    owners: dict[str, set[str]] = {identifier: set() for identifier in external}
    for candidate in scanned_rows:
        candidate_headers = candidate.get("headerIds")
        if not isinstance(candidate_headers, Mapping):
            continue
        candidate_authored = {
            str(value).casefold()
            for value in candidate_headers.get("hierarchyIds", [])
        }
        matches = candidate_authored & external
        if matches:
            path = candidate.get("virtualPath")
            if not isinstance(path, str) or not path:
                raise PlayableUnitPackCompilerError("hierarchy path is invalid")
            for identifier in matches:
                owners[identifier].add(path)
    result: set[str] = set()
    for hierarchy, paths in owners.items():
        if len(paths) != 1:
            raise PlayableUnitPackCompilerError(
                f"model hierarchy ownership is not unique: {hierarchy}"
            )
        result.update(paths)
    return tuple(sorted(result, key=lambda item: (item.casefold(), item)))


def _is_separate_death_rig(
    scanned_by_path: Mapping[str, Mapping[str, object]],
    animation_path: str,
    model_path: str,
) -> bool:
    """A death W3D that ships its own hierarchy is a model swap, not a clip."""

    animation_row = scanned_by_path.get(animation_path.casefold())
    if animation_row is None:
        raise PlayableUnitPackCompilerError(
            f"animation is not scanned: {animation_path}"
        )
    headers = animation_row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableUnitPackCompilerError(
            f"animation headers are invalid: {animation_path}"
        )
    hierarchy_ids = headers.get("hierarchyIds", [])
    if not isinstance(hierarchy_ids, list) or any(
        not isinstance(value, str) for value in hierarchy_ids
    ):
        raise PlayableUnitPackCompilerError(
            f"animation hierarchy ids are invalid: {animation_path}"
        )
    if any(value for value in hierarchy_ids):
        return True
    animation_ids = headers.get("animationIds", [])
    if not isinstance(animation_ids, list):
        raise PlayableUnitPackCompilerError(
            f"animation ids are invalid: {animation_path}"
        )
    animation_families = {
        family
        for identifier in animation_ids
        if isinstance(identifier, str) and "." in identifier
        for family in [_w3d_rig_family(identifier.split(".", 1)[0])]
        if family
    }
    model_row = scanned_by_path.get(model_path.casefold())
    model_headers = (
        model_row.get("headerIds") if isinstance(model_row, Mapping) else None
    )
    model_families = {
        family
        for value in (
            model_headers.get("hierarchyIds", [])
            if isinstance(model_headers, Mapping)
            else []
        )
        if isinstance(value, str)
        for family in [_w3d_rig_family(value)]
        if family
    }
    return (
        bool(animation_families)
        and bool(model_families)
        and animation_families.isdisjoint(model_families)
    )


def _texture_paths(
    closure: Mapping[str, object], selected_w3d: set[str]
) -> tuple[str, ...]:
    dependency = closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise PlayableUnitPackCompilerError("visual texture closure is invalid")
    textures: set[str] = set()
    selected = {path.casefold() for path in selected_w3d}
    for row in _rows(dependency.get("embeddedTextures"), "embedded textures"):
        source = row.get("sourceW3dVirtualPath")
        if not isinstance(source, str) or source.casefold() not in selected:
            continue
        if row.get("status") != "resolved":
            raise PlayableUnitPackCompilerError(
                f"selected W3D has unresolved texture: {source}"
            )
        textures.update(_paths(row, f"embedded texture {source}"))
    return tuple(sorted(textures, key=lambda item: (item.casefold(), item)))


def _validate_dependency_closure(
    closure: Mapping[str, object], scanned: list[Mapping[str, object]]
) -> None:
    dependency = closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise PlayableUnitPackCompilerError("visual texture closure is invalid")
    unsigned = dict(dependency)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableUnitPackCompilerError("W3D dependency closure digest is invalid")
    read_boundary = dependency.get("readBoundary")
    summary = dependency.get("summary")
    embedded = _rows(dependency.get("embeddedTextures"), "embedded textures")
    if not isinstance(read_boundary, Mapping) or not isinstance(summary, Mapping):
        raise PlayableUnitPackCompilerError("W3D dependency closure counts are invalid")
    scanned_paths = [str(row.get("virtualPath", "")) for row in scanned]
    if (
        summary.get("fileCount") != len(scanned)
        or summary.get("embeddedTextureReferenceCount") != len(embedded)
        or read_boundary.get("uniqueReadCount") != len(scanned)
        or read_boundary.get("uniqueVirtualPaths")
        != sorted(scanned_paths, key=lambda item: (item.casefold(), item))
    ):
        raise PlayableUnitPackCompilerError("W3D dependency closure counts drifted")
    statuses = [str(row.get("status", "")) for row in embedded]
    if (
        summary.get("resolvedEmbeddedTextureCount") != statuses.count("resolved")
        or summary.get("missingEmbeddedTextureCount") != statuses.count("missing")
        or summary.get("ambiguousEmbeddedTextureCount") != statuses.count("ambiguous")
        or summary.get("invalidEmbeddedTextureCount") != statuses.count("invalid")
    ):
        raise PlayableUnitPackCompilerError("W3D dependency texture counts drifted")


def _validate_resource_values(resources: list[Mapping[str, object]]) -> None:
    ids: set[str] = set()
    outputs: set[str] = set()
    for row in resources:
        identifier = row.get("id")
        kind = row.get("kind")
        converter = row.get("converter")
        if (
            not isinstance(identifier, str)
            or SLUG_PATTERN.fullmatch(identifier) is None
        ):
            raise PlayableUnitPackCompilerError("resource id is invalid")
        if identifier in ids:
            raise PlayableUnitPackCompilerError("resource ids are duplicated")
        ids.add(identifier)
        if kind not in ALLOWED_KINDS or converter not in ALLOWED_CONVERTERS:
            raise PlayableUnitPackCompilerError("resource kind or converter is invalid")
        patterns = row.get("patterns")
        if (
            not isinstance(patterns, list)
            or not 1 <= len(patterns) <= MAX_PATTERNS_PER_RESOURCE
            or any(not isinstance(path, str) for path in patterns)
        ):
            raise PlayableUnitPackCompilerError("resource patterns are invalid")
        normalized = [_safe_path(path, "resource pattern") for path in patterns]
        if len({path.casefold() for path in normalized}) != len(normalized):
            raise PlayableUnitPackCompilerError("resource patterns are duplicated")
        limit = row.get("limit")
        expected = row.get("expected_count")
        if (
            not isinstance(limit, int)
            or isinstance(limit, bool)
            or not isinstance(expected, int)
            or isinstance(expected, bool)
            or limit < len(patterns)
            or expected != len(patterns)
        ):
            raise PlayableUnitPackCompilerError("resource counts are invalid")
        output = row.get("output")
        if output is not None:
            normalized_output = _safe_path(output, "resource output")
            output_key = normalized_output.casefold()
            if output_key in outputs:
                raise PlayableUnitPackCompilerError("resource outputs are duplicated")
            outputs.add(output_key)
        options = row.get("options", {})
        if not isinstance(options, Mapping):
            raise PlayableUnitPackCompilerError("resource options are invalid")
        dependencies = options.get("inputResourceIds", [])
        if not isinstance(dependencies, list) or any(
            not isinstance(value, str) for value in dependencies
        ):
            raise PlayableUnitPackCompilerError("resource dependencies are invalid")
        if len(dependencies) != len(set(dependencies)) or identifier in dependencies:
            raise PlayableUnitPackCompilerError("resource dependencies are invalid")
        suffixes = {PurePosixPath(path).suffix.casefold() for path in normalized}
        if converter in {"w3d-bundle", "w3d-hierarchical", "w3d-static"}:
            model = options.get("model")
            animations = options.get("animations", [])
            if (
                not isinstance(output, str)
                or not output.casefold().endswith(".glb")
                or not isinstance(model, str)
                or not model
                or model.casefold()
                not in {PurePosixPath(path).name.casefold() for path in normalized}
                or not isinstance(animations, list)
                or any(
                    not isinstance(value, str)
                    or value.casefold()
                    not in {PurePosixPath(path).name.casefold() for path in normalized}
                    for value in animations
                )
                or (converter == "w3d-bundle" and not animations)
                or (converter != "w3d-bundle" and animations)
            ):
                raise PlayableUnitPackCompilerError("W3D resource contract is invalid")
        elif converter == "texture":
            if (
                len(normalized) != 1
                or not isinstance(output, str)
                or not output.casefold().endswith(".png")
                or options
            ):
                raise PlayableUnitPackCompilerError(
                    "texture resource contract is invalid"
                )
        elif converter == "texture-atlas-crops":
            if len(normalized) != 1 or not isinstance(output, str):
                raise PlayableUnitPackCompilerError(
                    "UI atlas resource contract is invalid"
                )
            try:
                normalize_texture_atlas_crops(
                    options.get("crops"), output, context="playable-unit UI atlas"
                )
            except ValueError as exc:
                raise PlayableUnitPackCompilerError(
                    "UI atlas crop contract is invalid"
                ) from exc
            if set(options) != {"crops"}:
                raise PlayableUnitPackCompilerError("UI atlas options are invalid")
        elif converter == "audio":
            if (
                len(normalized) != 1
                or not isinstance(output, str)
                or not output.casefold().endswith(".wav")
                or dict(options) != {"force_pcm": True}
            ):
                raise PlayableUnitPackCompilerError(
                    "audio resource contract is invalid"
                )
        elif converter == "copy":
            if (
                len(normalized) != 1
                or not isinstance(output, str)
                or PurePosixPath(output).suffix.casefold() not in suffixes
                or options
            ):
                raise PlayableUnitPackCompilerError("copy resource contract is invalid")
        elif converter == "hash-only" and (output is not None or options):
            raise PlayableUnitPackCompilerError(
                "hash-only resource contract is invalid"
            )
    for row in resources:
        dependencies = row.get("options", {}).get("inputResourceIds", [])
        if any(value not in ids for value in dependencies):
            raise PlayableUnitPackCompilerError("resource dependency is dangling")


def compile_playable_unit_pack_recipe(
    descriptor: Mapping[str, object],
    visual_closure: Mapping[str, object],
    fx_closure: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Compile conversion resources and generic runtime registration for one unit.

    ``fx_closure`` is the optional sealed ability-FX closure from
    :mod:`retail_ability_fx_ingress`: the converted particle definitions and
    textures behind this unit's authored ability FXLists.  Omitting it leaves
    the recipe byte-identical to the pre-FX lane.
    """

    validate_playable_unit_descriptor(descriptor)
    try:
        fx_resources, fx_bindings = fx_recipe_parts(
            fx_closure, str(descriptor["objectId"])
        )
    except AbilityFxIngressError as exc:
        raise PlayableUnitPackCompilerError(str(exc)) from exc
    if (
        visual_closure.get("schema") != "openbfme.retail-visual-closure"
        or visual_closure.get("schemaVersion") != 1
    ):
        raise PlayableUnitPackCompilerError("visual closure identity is invalid")
    closure_unsigned = dict(visual_closure)
    closure_digest = closure_unsigned.pop("aggregateSha256", None)
    if not isinstance(closure_digest, str) or closure_digest != _digest(
        closure_unsigned
    ):
        raise PlayableUnitPackCompilerError("visual closure digest is invalid")
    summary = visual_closure.get("summary")
    if not isinstance(summary, Mapping):
        raise PlayableUnitPackCompilerError(
            "visual closure is not conversion-ready: summary is missing"
        )
    unresolved = visual_closure.get("unresolved")
    if not isinstance(unresolved, Mapping):
        raise PlayableUnitPackCompilerError("visual closure unresolved contract is invalid")
    graph_diagnostics = _rows(
        unresolved.get("graphDiagnostics"), "visual graph diagnostics"
    )
    unresolved_references = _rows(
        unresolved.get("references"), "unresolved visual references"
    )
    missing_definitions = _rows(
        visual_closure.get("missingDefinitions", []), "missing visual definitions"
    )
    conditional_animation_candidates = [
        row
        for row in unresolved_references
        if row.get("status") == "missing"
        and row.get("kind") == "animation"
        and row.get("usage") == "animation"
        and isinstance(row.get("conditions"), list)
        and isinstance(row.get("provenance"), Mapping)
        # Condition-less rows qualify only for the authored idle default
        # state (RotWK Rogash references KURogash_IDLC/IDLE clips the
        # archives never shipped); the state-row check below still requires
        # authored idle clips to remain, so an idle-less unit keeps failing.
        and (bool(row.get("conditions")) or _state(row) == "idle")
    ]
    dependency = visual_closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise PlayableUnitPackCompilerError("visual texture closure is invalid")
    embedded = _rows(dependency.get("embeddedTextures"), "embedded textures")
    scanned = _rows(visual_closure.get("scannedW3d"), "scanned W3D")
    _validate_dependency_closure(visual_closure, scanned)
    missing_embedded = [row for row in embedded if row.get("status") == "missing"]
    exact_rows = _rows(visual_closure.get("exactLeaves"), "exact visual leaves")
    editor_only_model_rows = [
        row
        for row in exact_rows
        if row.get("kind") == "model"
        and isinstance(row.get("conditions"), list)
        and "world_builder"
        in {str(value).casefold() for value in row.get("conditions", [])}
    ]
    hero_form_model_rows = [
        row
        for row in exact_rows
        if descriptor.get("category") == "hero"
        and row.get("kind") == "model"
        and isinstance(row.get("conditions"), list)
        and bool(row.get("conditions"))
        and not _is_required_galadriel_ring_skin(row)
        and {
            str(value).casefold() for value in row.get("conditions", [])
        }.issubset(_EXCLUDED_HERO_FORM_CONDITIONS)
        and not any(
            other.get("kind") == "model"
            and other.get("conditions") == []
            and _draw_key(other) == _draw_key(row)
            and bool(
                {
                    str(path).casefold()
                    for path in other.get("physicalVirtualPaths", [])
                }
                & {
                    str(path).casefold()
                    for path in row.get("physicalVirtualPaths", [])
                }
            )
            for other in exact_rows
        )
    ]
    hero_form_animation_rows = [
        row
        for row in exact_rows
        if row.get("kind") == "animation"
        and isinstance(row.get("conditions"), list)
        and any(
            _draw_key(model) == _draw_key(row)
            and {
                str(value).casefold()
                for value in model.get("conditions", [])
            }.issubset(
                {str(value).casefold() for value in row.get("conditions", [])}
            )
            for model in hero_form_model_rows
        )
    ]
    unsupported_hero_form_rows = [
        {
            **deepcopy(row),
            "runtimeSupport": "excluded-hero-form",
            "runtimeExclusionReason": "authored-hero-form-outside-runtime-scope",
            "excludedFormConditions": sorted(
                {
                    str(value).casefold()
                    for value in row.get("conditions", [])
                }
                & _EXCLUDED_HERO_FORM_CONDITIONS
            ),
        }
        for row in [*hero_form_model_rows, *hero_form_animation_rows]
    ]
    unsupported_model_sources: set[str] = set()
    for missing in missing_embedded:
        source = str(missing.get("sourceW3dVirtualPath", ""))
        model_rows = [
            row
            for row in exact_rows
            if row.get("kind") == "model"
            and source in row.get("physicalVirtualPaths", [])
        ]
        if (
            not source
            or not model_rows
            or any(not row.get("conditions") for row in model_rows)
        ):
            raise PlayableUnitPackCompilerError(
                "required model has an unresolved texture"
            )
        unsupported_model_sources.add(source)
    unsupported_model_hierarchies: set[str] = set()
    unsupported_model_families = {
        family
        for row in exact_rows
        if row.get("kind") == "model"
        and any(
            str(path) in unsupported_model_sources
            for path in row.get("physicalVirtualPaths", [])
        )
        for family in [_w3d_rig_family(str(row.get("identifier", "")))]
        if family
    }
    unsupported_form_animation_rows = [
        row
        for row in exact_rows
        if row.get("kind") == "animation"
        and row not in hero_form_animation_rows
        and "." in str(row.get("identifier", ""))
        and _w3d_rig_family(
            str(row.get("identifier", "")).split(".", 1)[0]
        )
        in unsupported_model_families
    ]
    unsupported_source_keys = {source.casefold() for source in unsupported_model_sources}
    for row in scanned:
        if str(row.get("virtualPath", "")).casefold() not in unsupported_source_keys:
            continue
        headers = row.get("headerIds")
        if not isinstance(headers, Mapping):
            raise PlayableUnitPackCompilerError("scanned W3D headers are invalid")
        hierarchy_ids = headers.get("hierarchyIds", [])
        if not isinstance(hierarchy_ids, list) or any(
            not isinstance(value, str) or not value for value in hierarchy_ids
        ):
            raise PlayableUnitPackCompilerError("scanned W3D hierarchy ids are invalid")
        unsupported_model_hierarchies.update(
            value.casefold() for value in hierarchy_ids
        )
    conditional_animation_gaps = [
        row
        for row in conditional_animation_candidates
        if _state(row) is None
        or (
            "." in str(row.get("identifier", ""))
            and (
                str(row.get("identifier", "")).split(".", 1)[0].casefold()
                in unsupported_model_hierarchies
                or _w3d_rig_family(
                    str(row.get("identifier", "")).split(".", 1)[0]
                )
                in unsupported_model_families
            )
        )
    ]
    # Retail authors conditional animation variants (backing-up, taunt and
    # reaction clips) whose W3Ds are absent from the 1.06 archives; the
    # closure diagnoses each as a missing animation reference.  They are
    # tolerable only while their semantic state still resolves to authored
    # clips — verified once state rows are built below — and are recorded
    # explicitly in the recipe.
    retail_absent_animation_gaps = [
        row
        for row in conditional_animation_candidates
        if row not in conditional_animation_gaps
        and row.get("reason") == "missing W3D animation reference"
        and (_state(row) is not None or _is_turn_animation(row))
    ]
    # Some layered INIs author an optional ``ExtraMesh:Yes`` component that the
    # shipped archives do not contain (AngmarDireWolf's KUDireWolfC_SKN is one
    # such retail row).  It is safe to omit only when the same draw module and
    # condition state still has a resolved model component.  A missing sole or
    # conditional model therefore remains a hard closure failure.
    retail_absent_extra_mesh_gaps = [
        row
        for row in unresolved_references
        if row.get("status") == "missing"
        and row.get("kind") == "extra-mesh"
        and row.get("usage") == "extra-mesh"
        and row.get("reason") == "missing W3D extra-mesh reference"
        and row.get("conditions") in (None, [])
        and any(
            resolved.get("kind") in {"model", "extra-mesh"}
            and resolved.get("status") == "resolved"
            and resolved.get("conditions") == []
            and str(resolved.get("targetObject", "")).casefold()
            == str(row.get("targetObject", "")).casefold()
            and _draw_key(resolved) == _draw_key(row)
            for resolved in exact_rows
        )
    ]
    # RandomTexture is an optional replacement for a model's embedded default
    # texture. Some retail INIs name variants that are not present in the
    # shipped archives (RohanBanner's RUYeoBannerB.tga). Omit such a variant
    # only when the same unconditional draw has a resolved model whose embedded
    # default texture is itself resolved.
    retail_absent_random_texture_gaps = [
        row
        for row in unresolved_references
        if row.get("status") == "missing"
        and row.get("kind") == "texture"
        and row.get("usage") == "random-texture"
        and row.get("conditions") in (None, [])
        and any(
            model.get("kind") == "model"
            and model.get("status") == "resolved"
            and model.get("conditions") == []
            and str(model.get("targetObject", "")).casefold()
            == str(row.get("targetObject", "")).casefold()
            and _draw_key(model) == _draw_key(row)
            and any(
                embedded_row.get("status") == "resolved"
                and str(embedded_row.get("sourceW3dVirtualPath", "")).casefold()
                in {
                    str(path).casefold()
                    for path in model.get("physicalVirtualPaths", [])
                }
                for embedded_row in embedded
            )
            for model in exact_rows
        )
    ]
    tolerated_non_core_gaps = (
        not graph_diagnostics
        and not missing_definitions
        and len(conditional_animation_gaps)
        + len(retail_absent_animation_gaps)
        + len(retail_absent_extra_mesh_gaps)
        + len(retail_absent_random_texture_gaps)
        == len(unresolved_references)
        and all(row.get("status") in {"resolved", "missing"} for row in embedded)
    )
    if summary.get("ready") is not True and not tolerated_non_core_gaps:
        raise PlayableUnitPackCompilerError(
            "visual closure is not conversion-ready "
            f"(ready={summary.get('ready')!r}, graphDiagnostics={len(graph_diagnostics)}, "
            f"missingDefinitions={len(missing_definitions)}, unresolved={len(unresolved_references)}, "
            f"conditionalGaps={len(conditional_animation_gaps)}, "
            f"retailAbsentAnimationGaps={len(retail_absent_animation_gaps)}, "
            f"retailAbsentExtraMeshGaps={len(retail_absent_extra_mesh_gaps)}, "
            f"retailAbsentRandomTextureGaps={len(retail_absent_random_texture_gaps)}, "
            f"embedded={len(embedded)})"
        )
    composition = descriptor["composition"]
    if not isinstance(composition, Mapping):
        raise PlayableUnitPackCompilerError("descriptor composition is invalid")
    member_id = str(composition["primaryMemberObjectId"])
    container_id = str(composition["containerObjectId"])
    member_rows = _rows(composition.get("members"), "composition members")
    member_ids = {str(row.get("objectId", "")) for row in member_rows}
    if len({identifier.casefold() for identifier in member_ids}) != 1:
        raise PlayableUnitPackCompilerError(
            "secondary member presentation contract is not available"
        )
    relevant_targets = {
        container_id.casefold(),
        *(identifier.casefold() for identifier in member_ids),
    }
    exact = [
        row
        for row in exact_rows
        if str(row.get("targetObject", "")).casefold() in relevant_targets
        and row not in editor_only_model_rows
        and row not in hero_form_model_rows
        and row not in hero_form_animation_rows
        and row not in unsupported_form_animation_rows
        and not any(
            str(path) in unsupported_model_sources
            for path in row.get("physicalVirtualPaths", [])
        )
    ]
    semantic = [
        row
        for row in _rows(visual_closure.get("semanticLeaves"), "semantic visual leaves")
        if str(row.get("targetObject", "")).casefold() in relevant_targets
    ]
    target_rows = _rows(visual_closure.get("targets"), "visual targets")
    resolved_targets = {
        str(row.get("name", "")).casefold()
        for row in target_rows
        if row.get("status") == "resolved"
    }
    if not relevant_targets.issubset(resolved_targets):
        raise PlayableUnitPackCompilerError("visual closure is missing a unit target")
    presentation = descriptor["presentation"]
    if not isinstance(presentation, Mapping):
        raise PlayableUnitPackCompilerError("descriptor presentation is invalid")
    visual_roots = _rows(presentation.get("visualRoots"), "descriptor visual roots")
    required_visual_ids = {
        str(row.get("id", ""))
        for row in visual_roots
        if str(row.get("id", "")).casefold() not in {"", "none"}
    }
    exact_ids = {str(row.get("identifier", "")).casefold() for row in exact}
    unsupported_model_ids = {
        str(row.get("identifier", "")).casefold()
        for row in exact_rows
        if row.get("kind") == "model"
        and (
            row in hero_form_model_rows
            or any(
                str(path) in unsupported_model_sources
                for path in row.get("physicalVirtualPaths", [])
            )
        )
    }
    retail_absent_extra_mesh_ids = {
        str(row.get("identifier", "")).casefold()
        for row in retail_absent_extra_mesh_gaps
    }
    # Editor-only rows (retail horde banner markers) are recorded through the
    # excluded-editor-only lane below, so a visual root they author is
    # accounted for rather than missing.
    editor_only_visual_ids = {
        str(row.get("identifier", "")).casefold() for row in editor_only_model_rows
    }
    # Secondary Draw modules (hero-selection Icon02, multi-skin Wildman
    # IUWildMan2_SKN) often land in scannedW3d with a resolvable stem while the
    # exactLeaves lane only lists primary combat models. Treat a scanned stem
    # match as authored-root evidence so convert does not invent geometry or
    # fail closed on a Draw that the W3D tree already carries.
    from pathlib import PurePosixPath
    import re as _re

    scanned_stems = {
        PurePosixPath(str(row.get("virtualPath", ""))).stem.casefold()
        for row in scanned
        if str(row.get("virtualPath", "")).casefold().endswith(".w3d")
    }
    # Hero-selection UI markers: retail authors a second Draw =
    # W3DScriptedModelDraw Icon with Model = Icon02 purely for the selection
    # ring. The combat visual graph never promotes that stem into exactLeaves
    # (or even scannedW3d on some heroes). Combat pack completeness does not
    # require packaging the selection marker mesh.
    hero_selection_icon_ids = {
        identifier.casefold()
        for identifier in required_visual_ids
        if _re.fullmatch(r"icon\d*", identifier.casefold()) is not None
    }

    def _skin_family(name: str) -> str:
        # IUWildMan2_SKN / IUWildMan_SKN share the multi-skin family
        # ``iuwildman_skn`` used for unit appearance rolls.
        return _re.sub(r"\d+(?=(_skn)?$)", "", name.casefold())

    exact_skin_families = {_skin_family(identifier) for identifier in exact_ids}
    scanned_skin_families = {_skin_family(stem) for stem in scanned_stems}
    missing_visual_ids = sorted(
        (
            identifier
            for identifier in required_visual_ids
            if identifier.casefold() not in exact_ids
            and identifier.casefold() not in unsupported_model_ids
            and identifier.casefold() not in retail_absent_extra_mesh_ids
            and identifier.casefold() not in editor_only_visual_ids
            and identifier.casefold() not in scanned_stems
            and identifier.casefold() not in hero_selection_icon_ids
            and _skin_family(identifier) not in exact_skin_families
            and _skin_family(identifier) not in scanned_skin_families
        ),
        key=str.casefold,
    )
    if missing_visual_ids:
        raise PlayableUnitPackCompilerError(
            "visual closure is missing authored roots: "
            + ", ".join(missing_visual_ids)
        )
    model_rows = [row for row in exact if row.get("kind") == "model"]
    animation_rows = [row for row in exact if row.get("kind") == "animation"]
    auxiliary_rows = [
        row for row in exact if row.get("kind") not in {"model", "animation"}
    ]
    primary_models = {
        path
        for row in model_rows
        if str(row.get("targetObject", "")).casefold() == member_id.casefold()
        and row.get("conditions") == []
        for path in _paths(row, "primary model")
    }
    member_hidden_by_default = any(
        row.get("kind") == "model"
        and str(row.get("targetObject", "")).casefold() == member_id.casefold()
        and row.get("reason") == "sage-none-model"
        and row.get("conditions") == []
        for row in semantic
    )
    # A mount composition (retail MordorMumakil carrying its rider horde
    # MordorHaradrimLancerHorde as InitialPayload) inverts the usual horde
    # contract: the member horde is authored invisible by default (``Model =
    # None`` in its DefaultModelConditionState) while the container mount
    # authors the one visible default model and every core animation.  That
    # authored form converts explicitly — the mount's single default model is
    # the composition's visual primary and the hidden member contributes no
    # model — while any other member-defaultless shape keeps failing closed.
    mounted_presentation = False
    if not primary_models:
        mount_models = {
            path
            for row in model_rows
            if str(row.get("targetObject", "")).casefold() == container_id.casefold()
            and row.get("conditions") == []
            for path in _paths(row, "mount model")
        }
        if member_hidden_by_default and len(mount_models) == 1:
            primary_models = mount_models
            mounted_presentation = True
    # RotWK Dwarven banner carriers deliberately author Model=None as their
    # unconditional state and select the visible skin through MorphCondition
    # USER flags. Preserve every conditional skin, but pin the carrier-specific
    # authored form as the fallback visual so the pack never invents a model.
    banner_morph_default = {
        "dwarvenbanner": "USER_4",       # Guardian base carrier
        "dwarvenphalanxbanner": "USER_6",
        "menofdalebanner": "USER_3",
    }.get(member_id.casefold(), "")
    if not primary_models and member_hidden_by_default and banner_morph_default:
        morph_models = {
            path
            for row in model_rows
            if str(row.get("targetObject", "")).casefold() == member_id.casefold()
            and row.get("conditions") == [banner_morph_default]
            for path in _paths(row, "banner morph model")
        }
        if len(morph_models) == 1:
            primary_models = morph_models
    if len(primary_models) != 1:
        raise PlayableUnitPackCompilerError(
            "primary visual root does not identify one default model"
        )
    default_model = next(iter(primary_models))
    animated_body_id = container_id if mounted_presentation else member_id
    model_conditions: dict[str, set[str]] = {}
    model_has_unconditional: dict[str, bool] = {}
    model_occurrences: dict[str, list[dict[str, object]]] = {}
    model_owners: dict[str, str] = {}
    model_draw_keys: dict[str, tuple[str, str]] = {}
    group_models: dict[tuple[str, str], set[str]] = {}
    for row in model_rows:
        conditions = row.get("conditions", [])
        if not isinstance(conditions, list) or any(
            not isinstance(item, str) for item in conditions
        ):
            raise PlayableUnitPackCompilerError("model conditions are invalid")
        draw_key = _draw_key(row)
        for path in _paths(row, "model"):
            model_conditions.setdefault(path, set()).update(
                str(item) for item in conditions
            )
            model_has_unconditional[path] = (
                model_has_unconditional.get(path, False) or not conditions
            )
            model_occurrences.setdefault(path, []).append(
                {
                    "identifier": str(row.get("identifier", "")),
                    "conditions": list(conditions),
                    "usage": str(row.get("usage", "")),
                    "provenance": deepcopy(row.get("provenance", {})),
                }
            )
            model_owners.setdefault(path, str(row.get("targetObject", "")))
            previous_key = model_draw_keys.setdefault(path, draw_key)
            if previous_key != draw_key:
                raise PlayableUnitPackCompilerError(
                    "model belongs to multiple draw modules"
                )
            group_models.setdefault(draw_key, set()).add(path)
    group_defaults: dict[tuple[str, str], str] = {}
    for draw_key, paths in group_models.items():
        defaults = [path for path in paths if model_has_unconditional[path]]
        if len(defaults) > 1:
            raise PlayableUnitPackCompilerError(
                "draw module has multiple default model components"
            )
        if defaults:
            group_defaults[draw_key] = defaults[0]
    if banner_morph_default:
        group_defaults[model_draw_keys[default_model]] = default_model

    state_rows: dict[str, list[Mapping[str, object]]] = {
        state: [] for state in _CORE_ORDER
    }
    for row in animation_rows:
        state = _state(row)
        if state is not None:
            state_rows.setdefault(state, []).append(row)
    for row in retail_absent_animation_gaps:
        state = _state(row)
        fallback_state = "move" if _is_turn_animation(row) else state
        if fallback_state is None:
            raise PlayableUnitPackCompilerError(
                "visual closure is not conversion-ready: retail-absent animation has no authored state fallback"
            )
        if fallback_state in _SELECTION_STATES:
            # SELECTED / idle→selected are optional capability states. A
            # missing ATNA variant must not fail the whole unit compile.
            continue
        if not state_rows.get(fallback_state):
            raise PlayableUnitPackCompilerError(
                "visual closure is not conversion-ready: retail-absent animation has no authored state fallback"
            )
    required_states = _required_states(descriptor)
    missing_states = [state for state in required_states if not state_rows[state]]
    core_animation_exclusions: list[dict[str, object]] = []
    if missing_states:
        # A build-variation randomizer shell (retail RohanGenericEnt) authors
        # one static WorldBuilder model and zero AnimationStates: it is never
        # a playable unit, so its required core states are recorded as
        # explicit exclusions rather than reported as an unexplained gap.
        # Anything else — partial states, authored-but-unmapped clips, or an
        # unready closure — keeps failing closed.
        if not animation_rows and summary.get("ready") is True:
            core_animation_exclusions = [
                {
                    "state": state,
                    "runtimeSupport": "excluded-randomizer-shell",
                    "runtimeExclusionReason": "zero-authored-animation-states",
                }
                for state in missing_states
            ]
        else:
            raise PlayableUnitPackCompilerError(
                "unit has no resolved core animations: " + ", ".join(missing_states)
            )

    animations_by_model: dict[str, set[str]] = {
        path: set() for path in model_conditions
    }
    state_bindings: dict[str, list[dict[str, object]]] = {
        state: [] for state in required_states
    }
    for extra_state in _SELECTION_STATES:
        if state_rows.get(extra_state):
            state_bindings.setdefault(extra_state, [])
    authored_animation_states: list[dict[str, object]] = []
    scanned_by_path: dict[str, Mapping[str, object]] = {
        str(row.get("virtualPath", "")).casefold(): row
        for row in scanned
        if isinstance(row.get("virtualPath"), str)
    }
    death_swap_rows: list[dict[str, object]] = []
    for row in animation_rows:
        draw_key = _draw_key(row)
        candidates = group_models.get(draw_key, set())
        if not candidates:
            raise PlayableUnitPackCompilerError(
                "animation draw module has no model component"
            )
        raw_conditions = row.get("conditions", [])
        if not isinstance(raw_conditions, list):
            raise PlayableUnitPackCompilerError("animation conditions are invalid")
        condition_keys = {str(item).casefold() for item in raw_conditions}
        conditional_specificity: dict[str, int] = {}
        for path in candidates:
            matching_conditions = [
                {str(item).casefold() for item in occurrence["conditions"]}
                for occurrence in model_occurrences[path]
                if occurrence["conditions"]
                and {
                    str(item).casefold()
                    for item in occurrence["conditions"]
                }.issubset(condition_keys)
            ]
            if matching_conditions:
                conditional_specificity[path] = max(
                    len(conditions) for conditions in matching_conditions
                )
        best_specificity = max(conditional_specificity.values(), default=0)
        conditional_owners = [
            path
            for path, specificity in conditional_specificity.items()
            if specificity == best_specificity
        ]
        if len(conditional_owners) > 1:
            raise PlayableUnitPackCompilerError(
                "animation model ownership is ambiguous"
            )
        if conditional_owners:
            owner = conditional_owners[0]
        else:
            owner = group_defaults.get(draw_key, "")
            if not owner:
                raise PlayableUnitPackCompilerError(
                    "animation draw module has no compatible default model"
                )
        semantic_state = _state(row)
        for path in _paths(row, "authored animation"):
            separate_death = (
                semantic_state == "death"
                and str(row.get("targetObject", "")).casefold()
                == animated_body_id.casefold()
                and _is_separate_death_rig(scanned_by_path, path, owner)
            )
            scanned_animation = scanned_by_path.get(path.casefold())
            if scanned_animation is None:
                raise PlayableUnitPackCompilerError(
                    f"animation is not scanned: {path}"
                )
            animation_byte_length = scanned_animation.get("byteLength")
            if (
                isinstance(animation_byte_length, bool)
                or not isinstance(animation_byte_length, int)
                or animation_byte_length < 0
            ):
                raise PlayableUnitPackCompilerError(
                    f"animation byteLength is invalid: {path}"
                )
            # Retail archives contain zero-byte W3D placeholders for some
            # authored AnimationName clips (rugimli_idlg, etc.). They cannot
            # produce an owned keyed action; exclude them from conversion
            # while keeping the authored binding for provenance.
            zero_byte_placeholder = animation_byte_length == 0
            if separate_death:
                if zero_byte_placeholder:
                    raise PlayableUnitPackCompilerError(
                        "separate-model death animation is a zero-byte placeholder: "
                        + path
                    )
                death_swap_rows.append(
                    {
                        "sourceW3d": path,
                        "identifier": str(row.get("identifier", "")),
                        "conditions": [str(item) for item in raw_conditions],
                    }
                )
            elif not zero_byte_placeholder:
                animations_by_model[owner].add(path)
            binding = {
                "sourceW3d": path,
                "identifier": str(row.get("identifier", "")),
                "conditions": list(raw_conditions),
                "modelSourceW3d": owner,
                "ownerObjectId": str(row.get("targetObject", "")),
                "drawModule": draw_key[1],
            }
            authored_animation_states.append(
                {
                    **binding,
                    "semanticState": semantic_state,
                    "runtimeSupport": (
                        "excluded-zero-byte-placeholder"
                        if zero_byte_placeholder
                        else (
                            "generic-core"
                            if semantic_state in required_states
                            or semantic_state in _SELECTION_STATES
                            else "packaged-unimplemented"
                        )
                    ),
                    **(
                        {
                            "runtimeExclusionReason": "zero-byte-retail-w3d-placeholder",
                        }
                        if zero_byte_placeholder
                        else {}
                    ),
                }
            )
            if (
                not separate_death
                and not zero_byte_placeholder
                and semantic_state in state_bindings
                and str(row.get("targetObject", "")).casefold()
                == animated_body_id.casefold()
            ):
                state_bindings[semantic_state].append(binding)
    death_swap_paths = sorted(
        {str(row["sourceW3d"]) for row in death_swap_rows},
        key=lambda item: (item.casefold(), item),
    )
    # Retail units such as GoblinCaveTroll and WildMountainGiant author both
    # fall-down clips on the primary skeleton (DYING <flag>) and condition-keyed
    # decay corpse models shipping their own hierarchy (DYING DECAY <flag>).
    # That mixed form converts explicitly: clip deaths stay animation bindings,
    # each swap model becomes a condition-keyed death-model resource.
    mixed_death = bool(death_swap_rows) and bool(state_bindings.get("death"))
    if death_swap_paths and not mixed_death and len(death_swap_paths) > 1:
        raise PlayableUnitPackCompilerError(
            "separate-model death binding is ambiguous"
        )
    if mixed_death:
        seen_condition_sets: set[tuple[str, ...]] = set()
        for swap_row in death_swap_rows:
            swap_conditions = swap_row["conditions"]
            condition_set = tuple(
                sorted({str(item).casefold() for item in swap_conditions})
            )
            if condition_set in seen_condition_sets:
                raise PlayableUnitPackCompilerError(
                    "mixed death model swap conditions are ambiguous"
                )
            seen_condition_sets.add(condition_set)
    for state in list(state_bindings):
        if not state_bindings[state]:
            if state not in required_states:
                continue
            if state == "death" and death_swap_paths:
                continue
            if core_animation_exclusions:
                continue
            raise PlayableUnitPackCompilerError(
                f"unit has no primary-member core animation binding: {state}"
            )
        state_bindings[state].sort(
            key=lambda row: (
                str(row["sourceW3d"]).casefold(),
                str(row["identifier"]).casefold(),
            )
        )
    authored_animation_states.sort(
        key=lambda row: (
            str(row["ownerObjectId"]).casefold(),
            str(row["drawModule"]),
            str(row["sourceW3d"]).casefold(),
            str(row["identifier"]).casefold(),
        )
    )
    ability_animations = _ability_animations(authored_animation_states)
    all_animations = {path for paths in animations_by_model.values() for path in paths}
    hierarchies = set(_hierarchy_dependencies(all_animations, scanned))
    death_swap_w3d: set[str] = set()
    if death_swap_paths:
        swap_models = death_swap_paths if mixed_death else death_swap_paths[:1]
        death_swap_w3d = {
            *swap_models,
            *_hierarchy_dependencies(swap_models, scanned),
        }
    selected_w3d = (
        set(model_conditions) | all_animations | hierarchies | death_swap_w3d
    )
    textures = _texture_paths(visual_closure, selected_w3d)
    slug = _slug(str(descriptor["objectId"]))
    texture_ids: list[str] = []
    resources: list[dict[str, object]] = []
    if textures:
        texture_id = _resource_id("unit", slug, "material-textures")
        texture_ids.append(texture_id)
        resources.append(
            {
                "id": texture_id,
                "kind": "texture",
                "converter": "hash-only",
                "patterns": list(textures),
                "required": True,
                "limit": len(textures),
                "expected_count": len(textures),
            }
        )
    # Retail death/disassembly W3Ds (battlewagon diea, cavetroll dis*, etc.) are
    # both ModelConditionState models and separate-hierarchy death animations.
    # The death-model resource always converts them as w3d-bundle with
    # animations=[self].  Visual components of those same sources must use the
    # identical shape: hierarchical fails closed on the embedded actions that
    # the death path keeps (and that convert cleanly as animated).
    death_swap_source_keys = {path.casefold() for path in death_swap_paths}
    components: list[dict[str, object]] = []
    for index, model_path in enumerate(
        sorted(
            model_conditions,
            key=lambda item: (item != default_model, item.casefold(), item),
        )
    ):
        animations = sorted(
            animations_by_model.get(model_path, set()),
            key=lambda item: (item.casefold(), item),
        )
        component_hierarchies = sorted(
            set(_hierarchy_dependencies(animations, scanned)), key=str.casefold
        )
        model_hierarchies = list(_model_hierarchy_dependencies(model_path, scanned))
        patterns = sorted(
            {
                model_path,
                *animations,
                *component_hierarchies,
                *model_hierarchies,
            },
            key=lambda item: (item.casefold(), item),
        )
        resource_id = _resource_id("unit", slug, "visual", f"{index:02d}")
        output = f"assets/models/units/{slug}/{index:02d}.glb"
        embedded_animation = _model_embedded_animation(model_path, scanned)
        death_swap_source = model_path.casefold() in death_swap_source_keys
        self_animated = embedded_animation or death_swap_source
        if self_animated and animations and animations != [model_path]:
            raise PlayableUnitPackCompilerError(
                "unit model mixes bound and embedded animations: " + model_path
            )
        # Hierarchical skins that only ship as SKN (upgrade/reskin shells) still
        # require their external skeleton for secondary-skin proof and Blender
        # bind. Fail closed if HLod names a hierarchy we cannot stage.
        if not animations and not self_animated:
            model_row = next(
                (
                    row
                    for row in scanned
                    if str(row.get("virtualPath", "")).casefold()
                    == model_path.casefold()
                ),
                None,
            )
            authored_hierarchies: list = []
            if isinstance(model_row, Mapping):
                headers = model_row.get("headerIds")
                if isinstance(headers, Mapping):
                    raw_hier = headers.get("hierarchyIds", [])
                    if isinstance(raw_hier, list):
                        authored_hierarchies = raw_hier
            if (
                _model_has_hierarchy(model_path, scanned)
                and not model_hierarchies
                and not authored_hierarchies
            ):
                raise PlayableUnitPackCompilerError(
                    "hierarchical model has no staged hierarchy provider: "
                    + model_path
                )
        options: dict[str, object] = {
            "model": PurePosixPath(model_path).name,
            "inputResourceIds": texture_ids,
        }
        if animations:
            options["animations"] = [PurePosixPath(path).name for path in animations]
        elif self_animated:
            options["animations"] = [PurePosixPath(model_path).name]
        resources.append(
            {
                "id": resource_id,
                "kind": "model",
                "converter": (
                    "w3d-bundle"
                    if animations or self_animated
                    else "w3d-hierarchical"
                    if _model_has_hierarchy(model_path, scanned)
                    else "w3d-static"
                ),
                "patterns": patterns,
                "output": output,
                "options": options,
                "required": True,
                "limit": len(patterns),
                "expected_count": len(patterns),
            }
        )
        components.append(
            {
                "resourceId": resource_id,
                "sourceW3d": model_path,
                "output": output,
                "conditions": sorted(model_conditions[model_path], key=str.casefold),
                "authoredOccurrences": sorted(
                    model_occurrences[model_path],
                    key=lambda row: _canonical_bytes(row),
                ),
                "default": model_path == default_model
                and (
                    model_has_unconditional[model_path]
                    or bool(banner_morph_default)
                ),
                "ownerObjectId": model_owners[model_path],
                "drawModule": model_draw_keys[model_path][1],
                "role": (
                    "primary-container" if mounted_presentation else "primary-member"
                )
                if model_path == default_model
                else "auxiliary-or-conditional",
            }
        )

    death_swap_binding: dict[str, object] | None = None
    death_swap_bindings: list[dict[str, object]] = []
    if death_swap_paths and not mixed_death:
        death_path = death_swap_paths[0]
        death_name = PurePosixPath(death_path).name
        death_patterns = sorted(death_swap_w3d, key=str.casefold)
        death_resource_id = _resource_id("unit", slug, "death-model")
        death_output = f"assets/models/units/{slug}/death.glb"
        resources.append(
            {
                "id": death_resource_id,
                "kind": "model",
                "converter": "w3d-bundle",
                "patterns": death_patterns,
                "output": death_output,
                "options": {
                    "model": death_name,
                    "inputResourceIds": texture_ids,
                    "animations": [death_name],
                },
                "required": True,
                "limit": len(death_patterns),
                "expected_count": len(death_patterns),
            }
        )
        death_swap_binding = {
            "binding": "separate-model",
            "resourceId": death_resource_id,
            "sourceW3d": death_path,
            "output": death_output,
        }
    if mixed_death:
        resource_by_model: dict[str, tuple[str, str]] = {}
        for swap_model in death_swap_paths:
            swap_stem = _slug(PurePosixPath(swap_model).stem)
            swap_resource_id = _resource_id("unit", slug, "death-model", swap_stem)
            swap_output = f"assets/models/units/{slug}/death-{swap_stem}.glb"
            swap_patterns = sorted(
                {swap_model, *_hierarchy_dependencies([swap_model], scanned)},
                key=str.casefold,
            )
            swap_name = PurePosixPath(swap_model).name
            resources.append(
                {
                    "id": swap_resource_id,
                    "kind": "model",
                    "converter": "w3d-bundle",
                    "patterns": swap_patterns,
                    "output": swap_output,
                    "options": {
                        "model": swap_name,
                        "inputResourceIds": texture_ids,
                        "animations": [swap_name],
                    },
                    "required": True,
                    "limit": len(swap_patterns),
                    "expected_count": len(swap_patterns),
                }
            )
            resource_by_model[swap_model.casefold()] = (swap_resource_id, swap_output)
        for swap_row in sorted(
            death_swap_rows,
            key=lambda row: (
                str(row["sourceW3d"]).casefold(),
                str(row["identifier"]).casefold(),
                tuple(str(item).casefold() for item in row["conditions"]),
            ),
        ):
            swap_resource_id, swap_output = resource_by_model[
                str(swap_row["sourceW3d"]).casefold()
            ]
            death_swap_bindings.append(
                {
                    "binding": "separate-model",
                    "resourceId": swap_resource_id,
                    "sourceW3d": str(swap_row["sourceW3d"]),
                    "output": swap_output,
                    "identifier": str(swap_row["identifier"]),
                    "conditions": list(swap_row["conditions"]),
                }
            )

    authored_visual_leaves: list[dict[str, object]] = []
    auxiliary_resources: dict[tuple[str, str], dict[str, object]] = {}
    for row in auxiliary_rows:
        kind = str(row.get("kind", ""))
        conditions = row.get("conditions", [])
        if not isinstance(conditions, list) or any(
            not isinstance(value, str) for value in conditions
        ):
            raise PlayableUnitPackCompilerError(
                "auxiliary visual conditions are invalid"
            )
        for source in _paths(row, f"auxiliary visual {kind}"):
            suffix = PurePosixPath(source).suffix.casefold()
            is_texture = suffix in {".dds", ".tga", ".jpg", ".png"}
            resource_key = (source.casefold(), "texture" if is_texture else "source")
            resource = auxiliary_resources.get(resource_key)
            if resource is None:
                fingerprint = hashlib.sha256(source.casefold().encode()).hexdigest()[
                    :12
                ]
                resource_id = _resource_id("unit", slug, "visual-leaf", fingerprint)
                if is_texture:
                    output = f"assets/visual/units/{slug}/{fingerprint}.png"
                    resource = {
                        "id": resource_id,
                        "kind": "texture",
                        "converter": "texture",
                        "patterns": [source],
                        "output": output,
                        "options": {},
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                    runtime_support = "converted-unbound"
                else:
                    # Donor-retention lane, not a runtime asset. An auxiliary
                    # visual leaf whose converter is not implemented yet keeps
                    # its donor payload for provenance, but the payload must
                    # never land under `assets/` — everything there is runtime
                    # content, and a pack must not ship raw retail `.w3d`
                    # alongside it. `sources/` is the established source-only
                    # root (see retail_fords_environment_profile's
                    # `sources/environment/fords/new_skybox.w3d`), which the
                    # pack audit deliberately excludes from its runtime sweep.
                    output = f"sources/units/{slug}/{fingerprint}{suffix}"
                    resource = {
                        "id": resource_id,
                        "kind": "model" if suffix == ".w3d" else "other",
                        "converter": "copy",
                        "patterns": [source],
                        "output": output,
                        "options": {},
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                    runtime_support = "source-retained-unimplemented"
                auxiliary_resources[resource_key] = resource
                resources.append(resource)
            else:
                output = str(resource["output"])
                runtime_support = (
                    "converted-unbound"
                    if resource["converter"] == "texture"
                    else "source-retained-unimplemented"
                )
            authored_visual_leaves.append(
                {
                    "identifier": str(row.get("identifier", "")),
                    "kind": kind,
                    "usage": str(row.get("usage", "")),
                    "conditions": list(conditions),
                    "source": source,
                    "resourceId": str(resource["id"]),
                    "output": output,
                    "runtimeSupport": runtime_support,
                    "provenance": deepcopy(row.get("provenance", {})),
                }
            )
    authored_visual_leaves.sort(
        key=lambda row: (
            str(row["kind"]).casefold(),
            str(row["identifier"]).casefold(),
            str(row["source"]).casefold(),
            _canonical_bytes(row),
        )
    )
    authored_visual_semantics: list[dict[str, object]] = []
    for row in semantic:
        identifier = row.get("identifier")
        kind = row.get("kind")
        usage = row.get("usage")
        reason = row.get("reason")
        conditions = row.get("conditions")
        lifecycle = row.get("lifecyclePhases")
        evidence = row.get("evidence")
        provenance = row.get("provenance")
        if (
            row.get("status") != "semantic"
            or not isinstance(identifier, str)
            or not identifier
            or not isinstance(kind, str)
            or not kind
            or not isinstance(usage, str)
            or not usage
            or not isinstance(reason, str)
            or not reason
            or not isinstance(conditions, list)
            or any(not isinstance(value, str) for value in conditions)
            or not isinstance(lifecycle, list)
            or any(not isinstance(value, str) for value in lifecycle)
            or not isinstance(evidence, list)
            or not evidence
            or any(not isinstance(value, str) for value in evidence)
            or not isinstance(provenance, Mapping)
        ):
            raise PlayableUnitPackCompilerError("semantic visual row is invalid")
        draw_key = _draw_key(row)
        effect = _semantic_effect(reason)
        authored_visual_semantics.append(
            {
                "targetObject": str(row.get("targetObject")),
                "drawModule": draw_key[1],
                "identifier": identifier,
                "kind": kind,
                "usage": usage,
                "conditions": list(conditions),
                "lifecyclePhases": list(lifecycle),
                "reason": reason,
                "effect": effect,
                "runtimeSupport": "required-unimplemented",
                "evidence": list(evidence),
                "provenance": deepcopy(provenance),
            }
        )
    authored_visual_semantics.sort(
        key=lambda row: (
            str(row["targetObject"]).casefold(),
            str(row["drawModule"]),
            str(row["reason"]),
            str(row["identifier"]).casefold(),
            _canonical_bytes(row),
        )
    )

    images = presentation.get("resolvedImages")
    audio = presentation.get("resolvedAudio")
    if not isinstance(images, Mapping) or not isinstance(audio, Mapping):
        raise PlayableUnitPackCompilerError("descriptor resolved media is invalid")
    ui = presentation.get("ui")
    audio_routes = presentation.get("audioRoutes")
    if not isinstance(ui, Mapping) or not isinstance(audio_routes, Mapping):
        raise PlayableUnitPackCompilerError("descriptor media routes are invalid")
    production_rows = _rows(descriptor.get("production"), "descriptor production")
    engine_spawned_banner = bool(production_rows) and all(
        row.get("surface") == "banner-carrier" for row in production_rows
    )
    # ButtonImage is always required. SelectPortrait may be census source-null
    # (retail UPGondor_Banner has no MappedImage body); those ids are omitted
    # from resolvedImages by media resolution and must not block conversion.
    required_image_ids: set[str] = set()
    portrait_ids = {
        str(value)
        for value in ui.get("portraitImageIds", [])
        if str(value).casefold() not in {"", "none"}
    }
    image_keys = {str(key).casefold() for key in images}
    for command in ui.get("commands", []):
        if isinstance(command, Mapping) and isinstance(command.get("fields"), Mapping):
            for value in command["fields"].get("ButtonImage", []):
                if str(value).casefold() not in {"", "none"}:
                    required_image_ids.add(str(value))
    for portrait in portrait_ids:
        if portrait.casefold() in image_keys:
            required_image_ids.add(portrait)
        elif not engine_spawned_banner:
            # Non-banner units keep fail-closed portrait binding.
            required_image_ids.add(portrait)
    if any(
        identifier.casefold() not in image_keys for identifier in required_image_ids
    ):
        raise PlayableUnitPackCompilerError(
            "descriptor has unresolved UI image bindings"
        )
    required_audio_ids = {
        str(row.get("id", ""))
        for owner in audio_routes.values()
        if isinstance(owner, Mapping)
        for rows in owner.values()
        if isinstance(rows, list)
        for row in rows
        if isinstance(row, Mapping) and row.get("id")
    }
    required_audio_ids.update(
        str(route.get("id", ""))
        for command in ui.get("commands", [])
        if isinstance(command, Mapping)
        for route in command.get("audioRoutes", [])
        if isinstance(route, Mapping) and route.get("id")
    )
    audio_keys = {str(key).casefold() for key in audio}
    if any(
        identifier.casefold() not in audio_keys for identifier in required_audio_ids
    ):
        raise PlayableUnitPackCompilerError("descriptor has unresolved audio bindings")
    mapped_by_atlas: dict[str, list[tuple[str, Mapping[str, object]]]] = {}
    for identifier, image in images.items():
        if not isinstance(identifier, str) or not isinstance(image, Mapping):
            raise PlayableUnitPackCompilerError("mapped UI image is invalid")
        source = _safe_path(
            image.get("compiledTextureVirtualPath"), f"UI atlas {identifier}"
        )
        mapped_by_atlas.setdefault(source, []).append((identifier, image))
    image_bindings: dict[str, str] = {}
    image_binding_metadata: dict[str, dict[str, int]] = {}
    for atlas_index, (source, records) in enumerate(
        sorted(mapped_by_atlas.items(), key=lambda item: item[0].casefold())
    ):
        atlas_digest = hashlib.sha256(source.casefold().encode()).hexdigest()[:12]
        resource_id = _resource_id(
            "unit", slug, "ui-atlas", str(atlas_index), atlas_digest
        )
        output_directory = f"assets/ui/units/{slug}/{atlas_digest}"
        crops: list[dict[str, object]] = []
        for identifier, image in sorted(records, key=lambda item: item[0].casefold()):
            coords = image.get("coords")
            if not isinstance(coords, Mapping):
                raise PlayableUnitPackCompilerError(
                    f"UI image crop is invalid: {identifier}"
                )
            values = [coords.get(name) for name in ("left", "top", "right", "bottom")]
            if any(
                not isinstance(value, int) or isinstance(value, bool)
                for value in values
            ):
                raise PlayableUnitPackCompilerError(
                    f"UI image crop is invalid: {identifier}"
                )
            output_name = f"{_slug(identifier)}-{hashlib.sha256(identifier.casefold().encode()).hexdigest()[:8]}.png"
            crops.append(
                {
                    "logicalName": _resource_id("image", identifier),
                    "output": output_name,
                    "crop": [
                        values[0],
                        values[1],
                        values[2] - values[0],
                        values[3] - values[1],
                    ],
                }
            )
            image_bindings[identifier] = f"{output_directory}/{output_name}"
            image_binding_metadata[identifier] = {
                "width": values[2] - values[0],
                "height": values[3] - values[1],
            }
        resources.append(
            {
                "id": resource_id,
                "kind": "ui",
                "converter": "texture-atlas-crops",
                "patterns": [source],
                "output": output_directory,
                "options": {"crops": crops},
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        )
    audio_bindings: dict[str, list[str]] = {}
    audio_resources_by_source: dict[str, str] = {}
    for identifier, source_values in sorted(
        audio.items(), key=lambda item: str(item[0]).casefold()
    ):
        if not isinstance(source_values, list):
            raise PlayableUnitPackCompilerError(
                f"audio binding {identifier} is invalid"
            )
        if not source_values:
            audio_bindings[str(identifier)] = []
            continue
        outputs: list[str] = []
        for source_value in source_values:
            source = _safe_path(source_value, f"audio sample {identifier}")
            source_key = source.casefold()
            output = audio_resources_by_source.get(source_key, "")
            if not output:
                fingerprint = hashlib.sha256(source_key.encode()).hexdigest()[:16]
                resource_id = _resource_id("unit", slug, "audio", fingerprint)
                output = f"assets/audio/units/{slug}/{fingerprint}.wav"
                resources.append(
                    {
                        "id": resource_id,
                        "kind": "audio",
                        "converter": "audio",
                        "patterns": [source],
                        "output": output,
                        "options": {"force_pcm": True},
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                )
                audio_resources_by_source[source_key] = output
            outputs.append(output)
        audio_bindings[str(identifier)] = sorted(set(outputs), key=str.casefold)
    resources.extend(fx_resources)
    _validate_resource_values(resources)

    core_animations: dict[str, object] = {
        state: rows
        for state, rows in state_bindings.items()
        if rows or not core_animation_exclusions
    }
    if death_swap_binding is not None and "death" in core_animations:
        core_animations["death"] = death_swap_binding

    recipe: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": descriptor["objectId"],
        "category": descriptor["category"],
        "traits": deepcopy(descriptor["traits"]),
        "descriptorSha256": descriptor["descriptorSha256"],
        "visualClosureSha256": closure_digest,
        "resources": resources,
        "runtimeRegistration": {
            **({"fxBindings": fx_bindings} if fx_bindings is not None else {}),
            "production": deepcopy(descriptor["production"]),
            "composition": deepcopy(descriptor["composition"]),
            **(
                {"kindOf": deepcopy(descriptor["kindOf"])}
                if isinstance(descriptor.get("kindOf"), Mapping)
                else {}
            ),
            "gameplay": deepcopy(descriptor["gameplay"]),
            "simulation": deepcopy(descriptor["gameplay"]["simulation"]),
            "capabilities": deepcopy(descriptor["capabilities"]),
            **(
                {"abilities": deepcopy(descriptor["abilities"])}
                if isinstance(descriptor.get("abilities"), list)
                else {}
            ),
            **(
                {"experience": deepcopy(descriptor["experience"])}
                if isinstance(descriptor.get("experience"), Mapping)
                else {}
            ),
            "visual": {
                "components": components,
                **(
                    {
                        "presentationComposition": {
                            "form": "mounted-container-payload",
                            "visualPrimaryObjectId": container_id,
                            "hiddenMemberObjectId": member_id,
                        }
                    }
                    if mounted_presentation
                    else {}
                ),
                **(
                    {"deathModelSwaps": death_swap_bindings}
                    if death_swap_bindings
                    else {}
                ),
                "coreAnimations": core_animations,
                **(
                    {"coreAnimationExclusions": core_animation_exclusions}
                    if core_animation_exclusions
                    else {}
                ),
                "authoredAnimationStates": authored_animation_states,
                **(
                    {"abilityAnimations": ability_animations}
                    if ability_animations
                    else {}
                ),
                "authoredVisualLeaves": authored_visual_leaves,
                "authoredVisualSemantics": authored_visual_semantics,
                "unsupportedVisualReferences": deepcopy(
                    [
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-non-core-animation-gap",
                                "runtimeExclusionReason": (
                                    "unmapped-runtime-state"
                                    if _state(row) is None
                                    else "unsupported-model-rig"
                                ),
                                **(
                                    {
                                        "unsupportedRigFamily": _w3d_rig_family(
                                            str(row.get("identifier", "")).split(".", 1)[0]
                                        )
                                    }
                                    if _state(row) is not None
                                    else {}
                                ),
                            }
                            for row in conditional_animation_gaps
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-retail-absent-animation-gap",
                                "runtimeExclusionReason": (
                                    "retail-absent-animation-state-covered"
                                ),
                                "semanticState": str(_state(row)),
                            }
                            for row in retail_absent_animation_gaps
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-retail-absent-extra-mesh",
                                "runtimeExclusionReason": (
                                    "retail-absent-extra-mesh-default-covered"
                                ),
                            }
                            for row in retail_absent_extra_mesh_gaps
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-retail-absent-random-texture",
                                "runtimeExclusionReason": (
                                    "retail-absent-random-texture-default-covered"
                                ),
                            }
                            for row in retail_absent_random_texture_gaps
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-missing-conditional-model-texture",
                                "runtimeExclusionReason": "conditional-model-texture-unresolved",
                            }
                            for row in missing_embedded
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-editor-only",
                                "runtimeExclusionReason": "world-builder-only",
                            }
                            for row in editor_only_model_rows
                        ],
                        *[
                            {
                                **row,
                                "runtimeSupport": "excluded-unsupported-model-rig",
                                "runtimeExclusionReason": "conditional-model-texture-unresolved",
                                "unsupportedRigFamily": _w3d_rig_family(
                                    str(row.get("identifier", "")).split(".", 1)[0]
                                ),
                            }
                            for row in unsupported_form_animation_rows
                        ],
                        *unsupported_hero_form_rows,
                    ]
                ),
            },
            "ui": deepcopy(ui),
            "imageBindings": image_bindings,
            "imageBindingMetadata": image_binding_metadata,
            "stringBindings": deepcopy(presentation.get("resolvedStrings", {})),
            # Command-referenced ids retail itself never localizes. Carried so
            # the runtime can tell "retail has no text for this button" apart
            # from "this pack is broken" -- the latter verdict deleted whole
            # units from the roster.
            "sourceNullStringIds": list(presentation.get("sourceNullStringIds", [])),
            "audioRoutes": deepcopy(audio_routes),
            "audioBindings": audio_bindings,
            "audioResolution": {
                identifier: ("samples" if paths else "authored-silent")
                for identifier, paths in sorted(
                    audio_bindings.items(), key=lambda item: item[0].casefold()
                )
            },
            "unsupportedCapabilities": deepcopy(descriptor["unsupportedCapabilities"]),
        },
    }
    recipe["recipeSha256"] = _digest(recipe)
    return recipe


def validate_playable_unit_pack_recipe(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableUnitPackCompilerError("pack recipe identity is invalid")
    expected = dict(value)
    digest = expected.pop("recipeSha256", None)
    if not isinstance(digest, str) or digest != _digest(expected):
        raise PlayableUnitPackCompilerError("pack recipe digest is invalid")
    resources = _rows(value.get("resources"), "pack recipe resources")
    _validate_resource_values(resources)
    resources_by_id = {str(row["id"]): row for row in resources}
    runtime = value.get("runtimeRegistration")
    if not isinstance(runtime, Mapping):
        raise PlayableUnitPackCompilerError("runtime registration is invalid")
    visual = runtime.get("visual")
    if not isinstance(visual, Mapping):
        raise PlayableUnitPackCompilerError("runtime visual registration is invalid")
    components = _rows(visual.get("components"), "runtime visual components")
    if sum(row.get("default") is True for row in components) != 1:
        raise PlayableUnitPackCompilerError("runtime visual has no unique default")
    component_by_model: dict[str, Mapping[str, object]] = {}
    for component in components:
        resource_id = component.get("resourceId")
        source = component.get("sourceW3d")
        output = component.get("output")
        if (
            not isinstance(resource_id, str)
            or resource_id not in resources_by_id
            or not isinstance(source, str)
            or not isinstance(output, str)
        ):
            raise PlayableUnitPackCompilerError(
                "runtime component reference is invalid"
            )
        resource = resources_by_id[resource_id]
        if resource.get("output") != output or source not in resource.get(
            "patterns", []
        ):
            raise PlayableUnitPackCompilerError(
                "runtime component disagrees with resource"
            )
        if source in component_by_model:
            raise PlayableUnitPackCompilerError("runtime component model is duplicated")
        component_by_model[source] = component
    composition = runtime.get("composition")
    if not isinstance(composition, Mapping):
        raise PlayableUnitPackCompilerError("runtime composition is invalid")
    member_object = str(composition.get("primaryMemberObjectId", ""))
    container_object = str(composition.get("containerObjectId", ""))
    if not member_object or not container_object:
        raise PlayableUnitPackCompilerError("runtime composition is invalid")
    default_component = next(row for row in components if row.get("default") is True)
    default_owner = str(default_component.get("ownerObjectId", ""))
    presentation_composition = visual.get("presentationComposition")
    mounted = (
        container_object.casefold() != member_object.casefold()
        and default_owner.casefold() == container_object.casefold()
    )
    if mounted:
        # A mounted-container-payload recipe is only consistent whole: the
        # marker names the hidden member, the default component is owned by
        # the container mount, and the member's authored default stays hidden.
        hidden_member_semantics = _rows(
            visual.get("authoredVisualSemantics"), "authored visual semantics"
        )
        if (
            not isinstance(presentation_composition, Mapping)
            or presentation_composition.get("form") != "mounted-container-payload"
            or presentation_composition.get("visualPrimaryObjectId")
            != container_object
            or presentation_composition.get("hiddenMemberObjectId") != member_object
            or default_component.get("role") != "primary-container"
            or not any(
                row.get("reason") == "sage-none-model"
                and row.get("conditions") == []
                and str(row.get("targetObject", "")).casefold()
                == member_object.casefold()
                for row in hidden_member_semantics
            )
        ):
            raise PlayableUnitPackCompilerError(
                "mounted presentation composition is invalid"
            )
    elif (
        presentation_composition is not None
        or default_component.get("role") != "primary-member"
    ):
        raise PlayableUnitPackCompilerError(
            "presentation composition disagrees with its default component"
        )
    core = visual.get("coreAnimations")
    if not isinstance(core, Mapping):
        raise PlayableUnitPackCompilerError(
            "runtime core animation bindings are invalid"
        )
    authored = _rows(visual.get("authoredAnimationStates"), "authored animation states")
    authored_keys = {
        (str(row.get("sourceW3d", "")), str(row.get("identifier", "")))
        for row in authored
    }
    authored_sources = {source for source, _ in authored_keys}
    covered_animation_states = {str(state) for state in core} | {
        str(row.get("semanticState"))
        for row in authored
        if isinstance(row.get("semanticState"), str)
    }
    for state, rows in core.items():
        if isinstance(rows, Mapping):
            resource_id = rows.get("resourceId")
            source = rows.get("sourceW3d")
            output = rows.get("output")
            if (
                state != "death"
                or rows.get("binding") != "separate-model"
                or set(rows) != {"binding", "resourceId", "sourceW3d", "output"}
                or not isinstance(resource_id, str)
                or resource_id not in resources_by_id
                or not isinstance(source, str)
                or not isinstance(output, str)
            ):
                raise PlayableUnitPackCompilerError(
                    "separate-model death binding is invalid"
                )
            resource = resources_by_id[resource_id]
            options = resource.get("options", {})
            if (
                resource.get("converter") != "w3d-bundle"
                or resource.get("output") != output
                or source not in resource.get("patterns", [])
                or not isinstance(options, Mapping)
                or options.get("model") != PurePosixPath(source).name
                or options.get("animations") != [PurePosixPath(source).name]
                or source not in authored_sources
            ):
                raise PlayableUnitPackCompilerError(
                    "separate-model death binding is not packaged"
                )
            continue
        if not isinstance(rows, list) or not rows:
            raise PlayableUnitPackCompilerError(
                "runtime core animation bindings are invalid"
            )
        for binding in rows:
            if not isinstance(binding, Mapping):
                raise PlayableUnitPackCompilerError("core animation binding is invalid")
            owner = str(binding.get("modelSourceW3d", ""))
            source = str(binding.get("sourceW3d", ""))
            identifier = str(binding.get("identifier", ""))
            component = component_by_model.get(owner)
            if component is None:
                raise PlayableUnitPackCompilerError("animation owner is dangling")
            resource = resources_by_id[str(component["resourceId"])]
            if (
                source not in resource.get("patterns", [])
                or (source, identifier) not in authored_keys
            ):
                raise PlayableUnitPackCompilerError("animation binding is not packaged")
    death_swaps = visual.get("deathModelSwaps", [])
    if not isinstance(death_swaps, list) or any(
        not isinstance(row, Mapping) for row in death_swaps
    ):
        raise PlayableUnitPackCompilerError("mixed death model swaps are invalid")
    if death_swaps and not isinstance(core.get("death"), list):
        raise PlayableUnitPackCompilerError(
            "mixed death model swaps require clip death bindings"
        )
    swap_condition_sets: set[tuple[str, ...]] = set()
    for swap in death_swaps:
        resource_id = swap.get("resourceId")
        source = swap.get("sourceW3d")
        output = swap.get("output")
        conditions = swap.get("conditions")
        if (
            set(swap)
            != {
                "binding",
                "resourceId",
                "sourceW3d",
                "output",
                "identifier",
                "conditions",
            }
            or swap.get("binding") != "separate-model"
            or not isinstance(resource_id, str)
            or resource_id not in resources_by_id
            or not isinstance(source, str)
            or not isinstance(output, str)
            or not isinstance(swap.get("identifier"), str)
            or not isinstance(conditions, list)
            or not conditions
            or any(not isinstance(item, str) or not item for item in conditions)
        ):
            raise PlayableUnitPackCompilerError(
                "mixed death model swap binding is invalid"
            )
        resource = resources_by_id[resource_id]
        options = resource.get("options", {})
        if (
            resource.get("converter") != "w3d-bundle"
            or resource.get("output") != output
            or source not in resource.get("patterns", [])
            or not isinstance(options, Mapping)
            or options.get("model") != PurePosixPath(source).name
            or options.get("animations") != [PurePosixPath(source).name]
            or (source, str(swap["identifier"])) not in authored_keys
        ):
            raise PlayableUnitPackCompilerError(
                "mixed death model swap binding is not packaged"
            )
        condition_set = tuple(sorted({str(item).casefold() for item in conditions}))
        if condition_set in swap_condition_sets:
            raise PlayableUnitPackCompilerError(
                "mixed death model swap conditions are ambiguous"
            )
        swap_condition_sets.add(condition_set)
    animation_exclusions = visual.get("coreAnimationExclusions", [])
    if not isinstance(animation_exclusions, list) or any(
        not isinstance(row, Mapping) for row in animation_exclusions
    ):
        raise PlayableUnitPackCompilerError("core animation exclusions are invalid")
    if animation_exclusions:
        if (
            core
            or death_swaps
            or authored
            or [str(row.get("state", "")) for row in animation_exclusions]
            != list(_required_states(runtime))
            or any(
                set(row) != {"state", "runtimeSupport", "runtimeExclusionReason"}
                or row["runtimeSupport"] != "excluded-randomizer-shell"
                or row["runtimeExclusionReason"] != "zero-authored-animation-states"
                for row in animation_exclusions
            )
        ):
            raise PlayableUnitPackCompilerError(
                "core animation exclusions are invalid"
            )
    visual_leaves = _rows(visual.get("authoredVisualLeaves"), "authored visual leaves")
    for leaf in visual_leaves:
        resource_id = leaf.get("resourceId")
        source = leaf.get("source")
        output = leaf.get("output")
        if (
            not isinstance(resource_id, str)
            or resource_id not in resources_by_id
            or not isinstance(source, str)
            or not isinstance(output, str)
        ):
            raise PlayableUnitPackCompilerError(
                "authored visual leaf reference is invalid"
            )
        resource = resources_by_id[resource_id]
        if source not in resource.get("patterns", []) or output != resource.get(
            "output"
        ):
            raise PlayableUnitPackCompilerError("authored visual leaf is not packaged")
    semantics = _rows(
        visual.get("authoredVisualSemantics"), "authored visual semantics"
    )
    for semantic in semantics:
        reason = semantic.get("reason")
        provenance = semantic.get("provenance")
        scope = provenance.get("scopePath") if isinstance(provenance, Mapping) else None
        if (
            semantic.get("runtimeSupport") != "required-unimplemented"
            or not isinstance(reason, str)
            or not reason
            or semantic.get("effect") != _semantic_effect(reason)
            or not isinstance(semantic.get("targetObject"), str)
            or not semantic.get("targetObject")
            or not isinstance(semantic.get("drawModule"), str)
            or not semantic.get("drawModule")
            or not isinstance(semantic.get("identifier"), str)
            or not semantic.get("identifier")
            or not isinstance(semantic.get("kind"), str)
            or not semantic.get("kind")
            or not isinstance(semantic.get("usage"), str)
            or not semantic.get("usage")
            or not isinstance(semantic.get("conditions"), list)
            or any(
                not isinstance(value, str) for value in semantic.get("conditions", [])
            )
            or not isinstance(semantic.get("lifecyclePhases"), list)
            or any(
                not isinstance(value, str)
                for value in semantic.get("lifecyclePhases", [])
            )
            or not isinstance(semantic.get("evidence"), list)
            or not semantic.get("evidence")
            or not isinstance(provenance, Mapping)
            or not isinstance(provenance.get("definingObject"), str)
            or not provenance.get("definingObject")
            or not isinstance(provenance.get("inheritanceDistance"), int)
            or isinstance(provenance.get("inheritanceDistance"), bool)
            or provenance.get("inheritanceDistance") < 0
            or not isinstance(provenance.get("line"), int)
            or isinstance(provenance.get("line"), bool)
            or provenance.get("line") <= 0
            or not isinstance(scope, list)
            or not scope
            or any(not isinstance(value, str) or not value for value in scope)
            or str(semantic.get("drawModule")).casefold() != scope[0].casefold()
            or not isinstance(provenance.get("virtualPath"), str)
            or not provenance.get("virtualPath")
        ):
            raise PlayableUnitPackCompilerError("authored visual semantic is invalid")
        _safe_path(provenance["virtualPath"], "semantic provenance path")
    unsupported_visual = _rows(
        visual.get("unsupportedVisualReferences"), "unsupported visual references"
    )
    for row in unsupported_visual:
        conditions = row.get("conditions", [])
        provenance = row.get("provenance")
        valid_conditions = isinstance(conditions, list) and all(
            isinstance(value, str) and bool(value) for value in conditions
        )
        valid_provenance = (
            isinstance(provenance, Mapping)
            and bool(provenance)
            and isinstance(provenance.get("virtualPath"), str)
            and bool(provenance.get("virtualPath"))
        )
        scope = provenance.get("scopePath", []) if isinstance(provenance, Mapping) else []
        valid_graph_provenance = (
            valid_provenance
            and isinstance(provenance.get("definingObject"), str)
            and bool(provenance.get("definingObject"))
            and isinstance(provenance.get("inheritanceDistance"), int)
            and not isinstance(provenance.get("inheritanceDistance"), bool)
            and provenance.get("inheritanceDistance") >= 0
            and isinstance(provenance.get("line"), int)
            and not isinstance(provenance.get("line"), bool)
            and provenance.get("line") > 0
            and isinstance(scope, list)
            and bool(scope)
            and all(isinstance(value, str) and bool(value) for value in scope)
        )
        exclusion_reason = row.get("runtimeExclusionReason")
        rig_family = row.get("unsupportedRigFamily", "")
        is_animation = (
            row.get("status") == "missing"
            and row.get("kind") == "animation"
            and row.get("usage") == "animation"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and valid_conditions
            and bool(conditions)
            and valid_graph_provenance
            and not row.get("physicalVirtualPaths", [])
            and row.get("runtimeSupport") == "excluded-non-core-animation-gap"
            and exclusion_reason in {"unmapped-runtime-state", "unsupported-model-rig"}
            and (
                (_state(row) is None and exclusion_reason == "unmapped-runtime-state" and not rig_family)
                or (
                    _state(row) is not None
                    and exclusion_reason == "unsupported-model-rig"
                    and isinstance(rig_family, str)
                    and bool(rig_family)
                    and "." in str(row.get("identifier", ""))
                    and rig_family
                    == _w3d_rig_family(str(row.get("identifier", "")).split(".", 1)[0])
                )
            )
        )
        is_retail_absent_animation = (
            row.get("status") == "missing"
            and row.get("kind") == "animation"
            and row.get("usage") == "animation"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and valid_conditions
            and bool(conditions)
            and valid_graph_provenance
            and not row.get("physicalVirtualPaths", [])
            and not row.get("candidates", [])
            and row.get("reason") == "missing W3D animation reference"
            and row.get("runtimeSupport") == "excluded-retail-absent-animation-gap"
            and exclusion_reason == "retail-absent-animation-state-covered"
            and _state(row) is not None
            and row.get("semanticState") == _state(row)
            and str(row.get("semanticState")) in covered_animation_states
        )
        is_retail_absent_extra_mesh = (
            row.get("status") == "missing"
            and row.get("kind") == "extra-mesh"
            and row.get("usage") == "extra-mesh"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and valid_conditions
            and not conditions
            and valid_graph_provenance
            and not row.get("physicalVirtualPaths", [])
            and not row.get("candidates", [])
            and row.get("reason") == "missing W3D extra-mesh reference"
            and row.get("runtimeSupport") == "excluded-retail-absent-extra-mesh"
            and exclusion_reason == "retail-absent-extra-mesh-default-covered"
        )
        is_retail_absent_random_texture = (
            row.get("status") == "missing"
            and row.get("kind") == "texture"
            and row.get("usage") == "random-texture"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and valid_conditions
            and not conditions
            and valid_graph_provenance
            and not row.get("physicalVirtualPaths", [])
            and not row.get("candidates", [])
            and row.get("runtimeSupport")
            == "excluded-retail-absent-random-texture"
            and exclusion_reason
            == "retail-absent-random-texture-default-covered"
        )
        is_embedded_texture = (
            row.get("status") == "missing"
            and isinstance(row.get("sourceW3dVirtualPath"), str)
            and bool(row.get("sourceW3dVirtualPath"))
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and valid_provenance
            and not row.get("physicalVirtualPaths", [])
            and row.get("candidateCount", 0) == 0
            and row.get("candidates", []) == []
            and row.get("runtimeSupport")
            == "excluded-missing-conditional-model-texture"
            and exclusion_reason == "conditional-model-texture-unresolved"
        )
        is_editor_only_model = (
            row.get("status") == "resolved"
            and row.get("kind") == "model"
            and row.get("usage") == "model"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and isinstance(row.get("physicalVirtualPaths"), list)
            and bool(row.get("physicalVirtualPaths"))
            and valid_conditions
            and "world_builder" in {value.casefold() for value in conditions}
            and valid_graph_provenance
            and row.get("runtimeSupport") == "excluded-editor-only"
            and exclusion_reason == "world-builder-only"
        )
        is_excluded_form_animation = (
            row.get("status") == "resolved"
            and row.get("kind") == "animation"
            and row.get("usage") == "animation"
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and isinstance(row.get("physicalVirtualPaths"), list)
            and bool(row.get("physicalVirtualPaths"))
            and valid_conditions
            and bool(conditions)
            and valid_graph_provenance
            and row.get("runtimeSupport") == "excluded-unsupported-model-rig"
            and exclusion_reason == "conditional-model-texture-unresolved"
            and isinstance(rig_family, str)
            and bool(rig_family)
            and "." in str(row.get("identifier", ""))
            and rig_family
            == _w3d_rig_family(str(row.get("identifier", "")).split(".", 1)[0])
        )
        is_excluded_hero_form = (
            row.get("runtimeSupport") == "excluded-hero-form"
            and row.get("status") == "resolved"
            and row.get("kind") in {"model", "animation"}
            and row.get("usage") == row.get("kind")
            and isinstance(row.get("identifier"), str)
            and bool(row.get("identifier"))
            and isinstance(row.get("physicalVirtualPaths"), list)
            and bool(row.get("physicalVirtualPaths"))
            and valid_conditions
            and bool(conditions)
            and valid_graph_provenance
            and exclusion_reason == "authored-hero-form-outside-runtime-scope"
            and isinstance(row.get("excludedFormConditions"), list)
            and bool(row.get("excludedFormConditions"))
            and set(row.get("excludedFormConditions", []))
            == ({value.casefold() for value in conditions} & _EXCLUDED_HERO_FORM_CONDITIONS)
            and (
                row.get("kind") == "animation"
                or {value.casefold() for value in conditions}.issubset(
                    _EXCLUDED_HERO_FORM_CONDITIONS
                )
            )
        )
        if (
            not is_animation
            and not is_retail_absent_animation
            and not is_retail_absent_extra_mesh
            and not is_retail_absent_random_texture
            and not is_embedded_texture
            and not is_editor_only_model
            and not is_excluded_form_animation
            and not is_excluded_hero_form
        ):
            raise PlayableUnitPackCompilerError(
                "unsupported visual reference is invalid"
            )
        _safe_path(provenance["virtualPath"], "unsupported visual provenance path")
        if is_embedded_texture:
            _safe_path(row["sourceW3dVirtualPath"], "unsupported embedded texture source")
        for path in row.get("physicalVirtualPaths", []):
            if not isinstance(path, str):
                raise PlayableUnitPackCompilerError(
                    "unsupported visual reference is invalid"
                )
            _safe_path(path, "unsupported visual physical path")
    declared_outputs = {
        str(row.get("output"))
        for row in resources
        if isinstance(row.get("output"), str)
    }
    for row in resources:
        if row.get("converter") != "texture-atlas-crops":
            continue
        directory = str(row["output"])
        for crop in row.get("options", {}).get("crops", []):
            if isinstance(crop, Mapping) and isinstance(crop.get("output"), str):
                declared_outputs.add(f"{directory}/{crop['output']}")
    image_bindings = runtime.get("imageBindings")
    image_binding_metadata = runtime.get("imageBindingMetadata")
    string_bindings = runtime.get("stringBindings")
    audio_bindings = runtime.get("audioBindings")
    audio_resolution = runtime.get("audioResolution")
    if not isinstance(image_bindings, Mapping) or any(
        value not in declared_outputs for value in image_bindings.values()
    ):
        raise PlayableUnitPackCompilerError("UI binding output is dangling")
    if (
        not isinstance(image_binding_metadata, Mapping)
        or set(image_binding_metadata) != set(image_bindings)
        or any(
            not isinstance(metadata, Mapping)
            or set(metadata) != {"width", "height"}
            or any(
                isinstance(metadata.get(axis), bool)
                or not isinstance(metadata.get(axis), int)
                or metadata.get(axis, 0) <= 0
                for axis in ("width", "height")
            )
            for metadata in image_binding_metadata.values()
        )
    ):
        raise PlayableUnitPackCompilerError("UI binding metadata is invalid")
    required_string_ids = {
        str(value)
        for command in runtime.get("ui", {}).get("commands", [])
        if isinstance(command, Mapping)
        for field in ("TextLabel", "DescriptLabel")
        for value in command.get("fields", {}).get(field, [])
        if value
    }
    # Hero ability buttons (mount toggles, special powers) carry their own
    # label/tooltip ids and are just as bindable as a train command's.
    ability_string_ids = {
        str(value)
        for ability in runtime.get("abilities", []) or []
        if isinstance(ability, Mapping)
        and isinstance(ability.get("button"), Mapping)
        for field in ("labelIds", "tooltipIds")
        for value in ability["button"].get(field, []) or []
        if value
    }
    source_null_string_ids = runtime.get("sourceNullStringIds")
    if (
        not isinstance(string_bindings, Mapping)
        or any(
            not isinstance(key, str)
            or not key
            or not isinstance(value, str)
            or not value
            for key, value in string_bindings.items()
        )
        or not isinstance(source_null_string_ids, list)
        or any(
            not isinstance(item, str) or not item for item in source_null_string_ids
        )
        or set(source_null_string_ids) & set(string_bindings)
        or not set(source_null_string_ids) <= (required_string_ids | ability_string_ids)
        # Bound plus retail-unlocalized must account for EVERY referenced id --
        # command labels AND hero ability button labels alike. The union is
        # checked unconditionally: a unit with zero bound strings is the worst
        # case (every button text-dead), not an exemption from the gate.
        or not (required_string_ids | ability_string_ids)
        <= set(string_bindings) | set(source_null_string_ids)
    ):
        raise PlayableUnitPackCompilerError("localized string bindings are invalid")
    declared_audio_ids = {
        str(row.get("id", ""))
        for owner in runtime.get("audioRoutes", {}).values()
        if isinstance(owner, Mapping)
        for rows in owner.values()
        if isinstance(rows, list)
        for row in rows
        if isinstance(row, Mapping) and row.get("id")
    }
    ui = runtime.get("ui", {})
    if isinstance(ui, Mapping):
        declared_audio_ids.update(
            str(route.get("id", ""))
            for command in ui.get("commands", [])
            if isinstance(command, Mapping)
            for route in command.get("audioRoutes", [])
            if isinstance(route, Mapping) and route.get("id")
        )
    if (
        not isinstance(audio_bindings, Mapping)
        or not isinstance(audio_resolution, Mapping)
        or set(audio_bindings) != declared_audio_ids
        or set(audio_resolution) != declared_audio_ids
        or any(
            not isinstance(values, list)
            or audio_resolution.get(identifier)
            != ("samples" if values else "authored-silent")
            or any(value not in declared_outputs for value in values)
            for identifier, values in audio_bindings.items()
        )
    ):
        raise PlayableUnitPackCompilerError("audio binding output is dangling")


__all__ = [
    "PlayableUnitPackCompilerError",
    "compile_playable_unit_pack_recipe",
    "validate_playable_unit_pack_recipe",
]
