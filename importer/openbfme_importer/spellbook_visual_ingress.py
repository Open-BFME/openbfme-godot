"""Convert the models a spellbook's effect objects author into pack GLBs.

Spellbook leaves carried gameplay and (since the Draw-evidence pass) authored
model *names*, but nothing ever turned those names into converted geometry.
Every summoned Oathbreaker, Rohirrim, Ent, Balrog and Elven-Wood tree therefore
reached the runtime with no art binding at all and presented as the synthetic
multi-part "kit" fallback in ``AssetFactory`` — the blue units the owner sees.

This module closes that gap with the same generic machinery the playable-unit
lane already uses:

* :func:`build_spellbook_visual_closures` seals one
  :func:`~openbfme_importer.retail_visual_closure.build_retail_visual_closure`
  per model-authoring leaf object.  Per-object closures (rather than one batch)
  keep a single unparsable definition source from costing the whole faction its
  spellbook art, and they record the failure verbatim instead of dropping it.
* :func:`spellbook_visual_recipe_parts` turns those closures into ordinary
  ``w3d-static`` / ``w3d-hierarchical`` / ``w3d-bundle`` resources plus the
  payload-free bindings the runtime document carries.

Three authoring shapes are honoured exactly and never papered over:

``Model = None``
    Retail authors CloudBreakSunbeam, ElvenGrove, SunflareSunbeam and TaintLand
    with no geometry at all; their whole appearance is a ParticleSysBone.  They
    are recorded as ``authored-invisible`` and stay invisible.
horde containers
    A leaf with a ``HordeContain`` block draws nothing itself — its
    ``MemberObject`` is what a player sees.  Its binding points at the member's
    model and says so (``status: "horde-member"``); the container's own
    horde-marker W3D is never converted.
unconvertible evidence
    A leaf whose closure cannot be built, whose default model state is missing,
    or whose default model is ambiguous across draw modules is recorded with the
    reason.  Nothing is substituted.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path, PurePosixPath

from .playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    _digest,
    _draw_key,
    _hierarchy_dependencies,
    _model_embedded_animation,
    _model_has_hierarchy,
    _model_hierarchy_dependencies,
    _paths,
    _resource_id,
    _rows,
    _safe_path,
    _slug,
)
from .retail_visual_closure import build_retail_visual_closure


class SpellbookVisualIngressError(ValueError):
    """A spellbook's authored effect geometry cannot convert as one bounded set."""


# A spellbook is a bounded effect closure, not a roster; this ceiling only
# guards against a malformed descriptor asking for an unbounded scan.
MAX_VISUAL_OBJECTS = 64


def _leaf_objects(descriptor: Mapping[str, object]) -> list[Mapping[str, object]]:
    leaves = descriptor.get("leaves")
    if not isinstance(leaves, Mapping):
        raise SpellbookVisualIngressError("spellbook descriptor leaves are invalid")
    return _rows(leaves.get("objects", []), "spellbook object leaves")


def _leaf_index(descriptor: Mapping[str, object]) -> dict[str, Mapping[str, object]]:
    index: dict[str, Mapping[str, object]] = {}
    for row in _leaf_objects(descriptor):
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise SpellbookVisualIngressError("spellbook object leaf has no id")
        index[identifier] = row
    return index


def _authored_models(leaf: Mapping[str, object]) -> list[str]:
    """Model names the leaf's Draw states author, in authored order."""

    draw = leaf.get("draw")
    if not isinstance(draw, list):
        return []
    found: list[str] = []
    for state in draw:
        if not isinstance(state, Mapping):
            continue
        models = state.get("models")
        if not isinstance(models, list):
            continue
        for value in models:
            if isinstance(value, str) and value and value not in found:
                found.append(value)
    return found


def _horde_member(leaf: Mapping[str, object]) -> str:
    horde = leaf.get("horde")
    if not isinstance(horde, Mapping):
        return ""
    member = horde.get("memberObject")
    return member if isinstance(member, str) and member else ""


def visual_object_ids(descriptor: Mapping[str, object]) -> list[str]:
    """Leaf object ids whose own Draw evidence names at least one model.

    Horde containers are excluded: their authored model is a horde marker and
    the presented art is their ``MemberObject``'s (bound in
    :func:`spellbook_visual_recipe_parts`).
    """

    result: list[str] = []
    for row in _leaf_objects(descriptor):
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier:
            continue
        if _horde_member(row):
            continue
        if not _authored_models(row):
            continue
        result.append(identifier)
    ordered = sorted(set(result), key=lambda item: (item.casefold(), item))
    if len(ordered) > MAX_VISUAL_OBJECTS:
        raise SpellbookVisualIngressError(
            "spellbook authors more effect models than one pack recipe bounds: "
            f"{len(ordered)}"
        )
    return ordered


def build_spellbook_visual_closures(
    descriptor: Mapping[str, object],
    effective_root: Path | str,
) -> tuple[dict[str, dict[str, object]], dict[str, str]]:
    """Seal one visual closure per model-authoring spellbook leaf object.

    Returns ``(closures, failures)``.  ``failures`` maps an object id to the
    verbatim reason its closure could not be sealed — retail ships at least one
    definition source (``data/ini/object/system/system.ini``) that the CST
    rejects outside its macro context, and losing one projectile's art must not
    cost a faction its whole spellbook.
    """

    closures: dict[str, dict[str, object]] = {}
    failures: dict[str, str] = {}
    for object_id in visual_object_ids(descriptor):
        try:
            closures[object_id] = build_retail_visual_closure(
                effective_root, [object_id]
            )
        except Exception as exc:  # noqa: BLE001 - recorded, never substituted
            failures[object_id] = f"{type(exc).__name__}: {exc}"
    return closures, failures


def _validate_closure(object_id: str, closure: Mapping[str, object]) -> None:
    if (
        closure.get("schema") != "openbfme.retail-visual-closure"
        or closure.get("schemaVersion") != 1
    ):
        raise SpellbookVisualIngressError(
            f"spellbook visual closure identity is invalid: {object_id}"
        )
    unsigned = dict(closure)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise SpellbookVisualIngressError(
            f"spellbook visual closure digest is invalid: {object_id}"
        )
    targets = _rows(closure.get("targets"), "spellbook visual targets")
    resolved = {
        str(row.get("name", "")).casefold()
        for row in targets
        if row.get("status") == "resolved"
    }
    if object_id.casefold() not in resolved:
        raise SpellbookVisualIngressError(
            f"spellbook visual closure did not resolve its object: {object_id}"
        )


# SAGE spells the empty condition set two ways: `DefaultModelConditionState`
# (no condition list at all) and `ModelConditionState = NONE`.  Both mean "draw
# this when nothing else applies", so both are the default state here — reading
# only the first would have left the Balrog (whose default body is authored as
# `ModelConditionState = NONE`) with no convertible geometry.
_UNCONDITIONAL_TOKENS = frozenset({"none"})
# WORLD_BUILDER geometry exists only inside the map editor; retail draws nothing
# for it in a match.  Retail authors the summon eggs (WyrmEgg,
# SummonedDragonEgg, WatcherEgg) with WORLD_BUILDER as their ONLY model state,
# which is an authored in-game absence, not a conversion gap.
_EDITOR_ONLY_CONDITION = "world_builder"
# A W3DFloorDraw module is the ground bib a structure-shaped object stamps on
# the terrain, not the object's body.  The structure lane converts bibs
# separately; here it is only ever an extra unconditional source that would make
# the body look ambiguous.
_NON_BODY_DRAW_MODULES = ("w3dfloordraw",)


def _condition_set(row: Mapping[str, object]) -> set[str]:
    raw = row.get("conditions")
    if not isinstance(raw, list):
        return set()
    return {str(value).casefold() for value in raw}


def _default_model_path(
    object_id: str, closure: Mapping[str, object]
) -> tuple[str, str, str]:
    """Return ``(virtualPath, reason, status)`` for one object's body model.

    The selected model is the unconditional model-condition-state geometry — the
    pose retail draws when nothing else applies.  Condition-keyed variants are
    deliberately left out: the runtime has no condition machinery for effect
    objects, and picking one arbitrarily would be an invented choice.
    """

    exact = _rows(closure.get("exactLeaves"), "spellbook exact visual leaves")
    model_rows = [
        row
        for row in exact
        if row.get("kind") == "model"
        and str(row.get("targetObject", "")).casefold() == object_id.casefold()
        and isinstance(row.get("conditions"), list)
    ]
    candidates = [
        row for row in model_rows if not (_condition_set(row) - _UNCONDITIONAL_TOKENS)
    ]
    if not candidates:
        if model_rows and all(
            _EDITOR_ONLY_CONDITION in _condition_set(row) for row in model_rows
        ):
            return (
                "",
                "retail authors geometry only under WORLD_BUILDER; the object "
                "draws nothing in a match",
                "authored-invisible",
            )
        return "", "no unconditional model condition state resolved", "unconverted"
    unresolved = [row for row in candidates if row.get("status") != "resolved"]
    if unresolved:
        return (
            "",
            "default model reference is unresolved: "
            + str(unresolved[0].get("identifier", "")),
            "unconverted",
        )
    body = [
        row
        for row in candidates
        if not str(_draw_key(row)[1]).casefold().startswith(_NON_BODY_DRAW_MODULES)
    ] or candidates
    selected: set[str] = set()
    for row in body:
        selected.update(_paths(row, "spellbook model leaf"))
    if len(selected) != 1:
        draws = sorted({_draw_key(row)[1] for row in body})
        return (
            "",
            "default model is ambiguous across "
            f"{len(selected)} sources / draw modules {draws}",
            "unconverted",
        )
    return next(iter(selected)), "", "model"


def spellbook_visual_recipe_parts(
    descriptor: Mapping[str, object],
    slug: str,
    closures: Mapping[str, Mapping[str, object]] | None,
    failures: Mapping[str, str] | None = None,
) -> tuple[list[dict[str, object]], dict[str, object] | None]:
    """Return ``(resources, bindings)`` for one spellbook's effect geometry.

    ``closures`` is ``None`` (or empty) for callers with no effective-assets
    root — ``import-spellbook`` and every fixture — and then both halves are
    empty/``None`` so the recipe stays byte-identical to the pre-visual lane.
    """

    if not closures:
        return [], None
    index = _leaf_index(descriptor)
    failures = dict(failures or {})

    resources: list[dict[str, object]] = []
    objects: dict[str, dict[str, object]] = {}
    texture_paths: set[str] = set()
    model_rows: list[tuple[str, str, tuple[str, ...], str]] = []

    for object_id in sorted(closures, key=lambda item: (item.casefold(), item)):
        closure = closures[object_id]
        _validate_closure(object_id, closure)
        scanned = _rows(closure.get("scannedW3d"), "spellbook scanned W3D")
        model_path, reason, status = _default_model_path(object_id, closure)
        if not model_path:
            objects[object_id] = {"status": status, "reason": reason}
            continue
        try:
            embedded = _model_embedded_animation(model_path, scanned)
            hierarchies = list(_model_hierarchy_dependencies(model_path, scanned))
            if embedded:
                hierarchies.extend(_hierarchy_dependencies([model_path], scanned))
            has_hierarchy = _model_has_hierarchy(model_path, scanned)
        except PlayableUnitPackCompilerError as exc:
            objects[object_id] = {"status": "unconverted", "reason": str(exc)}
            continue
        patterns = sorted(
            {model_path, *hierarchies}, key=lambda item: (item.casefold(), item)
        )
        textures, texture_reason = _object_textures(closure, patterns)
        if texture_reason:
            # One unresolved material never becomes an invented one, and it
            # never costs the rest of the spellbook its art either.
            objects[object_id] = {
                "status": "unconverted",
                "reason": texture_reason,
            }
            continue
        converter = (
            "w3d-bundle"
            if embedded
            else "w3d-hierarchical"
            if has_hierarchy
            else "w3d-static"
        )
        texture_paths.update(textures)
        model_rows.append((object_id, model_path, tuple(patterns), converter))

    for object_id, reason in sorted(failures.items()):
        objects.setdefault(object_id, {"status": "unconverted", "reason": reason})

    texture_ids: list[str] = []
    if model_rows and texture_paths:
        ordered_textures = sorted(
            texture_paths, key=lambda item: (item.casefold(), item)
        )
        texture_id = _resource_id("spellbook", slug, "model-textures")
        texture_ids.append(texture_id)
        resources.append(
            {
                "id": texture_id,
                "kind": "texture",
                "converter": "hash-only",
                "patterns": ordered_textures,
                "required": True,
                "limit": len(ordered_textures),
                "expected_count": len(ordered_textures),
            }
        )

    for object_id, model_path, patterns, converter in model_rows:
        object_slug = _slug(object_id)
        resource_id = _resource_id("spellbook", slug, "visual", object_slug)
        output = f"assets/models/spellbook/{slug}/{object_slug}.glb"
        options: dict[str, object] = {
            "model": PurePosixPath(model_path).name,
            "inputResourceIds": list(texture_ids),
        }
        if converter == "w3d-bundle":
            options["animations"] = [PurePosixPath(model_path).name]
        resources.append(
            {
                "id": resource_id,
                "kind": "model",
                "converter": converter,
                "patterns": list(patterns),
                "output": output,
                "options": options,
                "required": True,
                "limit": len(patterns),
                "expected_count": len(patterns),
            }
        )
        objects[object_id] = {
            "status": "model",
            "resourceId": resource_id,
            "model": output,
            "sourceW3d": model_path,
            "converter": converter,
        }

    # Objects retail authors with `Model = None` keep their authored absence.
    for object_id, leaf in index.items():
        if object_id in objects:
            continue
        draw = leaf.get("draw")
        if not isinstance(draw, list) or not draw:
            continue
        if _authored_models(leaf) or _horde_member(leaf):
            continue
        objects[object_id] = {
            "status": "authored-invisible",
            "reason": "retail authors no Model; the object's appearance is its "
            "ParticleSysBone",
        }

    # A horde container draws its MemberObject, not its own horde marker.
    for object_id, leaf in index.items():
        member = _horde_member(leaf)
        if not member:
            continue
        bound = objects.get(member)
        if not isinstance(bound, Mapping) or bound.get("status") != "model":
            objects.setdefault(
                object_id,
                {
                    "status": "unconverted",
                    "reason": f"horde member '{member}' has no converted model",
                    "memberObjectId": member,
                },
            )
            continue
        objects[object_id] = {
            "status": "horde-member",
            "memberObjectId": member,
            "resourceId": bound["resourceId"],
            "model": bound["model"],
            "sourceW3d": bound["sourceW3d"],
            "converter": bound["converter"],
        }

    bindings: dict[str, object] = {
        "objects": {key: objects[key] for key in sorted(objects, key=str.casefold)},
        "summary": {
            "modelCount": sum(
                1 for row in objects.values() if row.get("status") == "model"
            ),
            "hordeMemberCount": sum(
                1 for row in objects.values() if row.get("status") == "horde-member"
            ),
            "authoredInvisibleCount": sum(
                1
                for row in objects.values()
                if row.get("status") == "authored-invisible"
            ),
            "unconvertedCount": sum(
                1 for row in objects.values() if row.get("status") == "unconverted"
            ),
        },
    }
    return resources, bindings


def _object_textures(
    closure: Mapping[str, object], patterns: Sequence[str]
) -> tuple[tuple[str, ...], str]:
    """Return ``(texturePaths, reason)`` for one object's selected W3D set.

    Mirrors the unit lane's ``_texture_paths`` contract but reports an
    unresolved material as a per-object reason instead of raising, so a single
    missing texture costs exactly that object's art and nothing else.
    """

    dependency = closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise SpellbookVisualIngressError(
            "spellbook visual texture closure is invalid"
        )
    selected = {path.casefold() for path in patterns}
    textures: set[str] = set()
    for row in _rows(dependency.get("embeddedTextures"), "embedded textures"):
        source = row.get("sourceW3dVirtualPath")
        if not isinstance(source, str) or source.casefold() not in selected:
            continue
        if row.get("status") != "resolved":
            return (), f"selected W3D has unresolved texture: {source}"
        try:
            textures.update(_paths(row, f"embedded texture {source}"))
        except PlayableUnitPackCompilerError as exc:
            return (), str(exc)
    return tuple(sorted(textures, key=lambda item: (item.casefold(), item))), ""


def validate_spellbook_visual_bindings(value: object) -> None:
    """Reject a visual-binding block that drifted from its recipe shape."""

    if value is None:
        return
    if not isinstance(value, Mapping):
        raise SpellbookVisualIngressError("spellbook visual bindings are invalid")
    objects = value.get("objects")
    if not isinstance(objects, Mapping):
        raise SpellbookVisualIngressError(
            "spellbook visual binding objects are invalid"
        )
    for object_id, row in objects.items():
        if not isinstance(object_id, str) or not object_id or not isinstance(row, Mapping):
            raise SpellbookVisualIngressError(
                "spellbook visual binding row is invalid"
            )
        status = row.get("status")
        if status not in {
            "model",
            "horde-member",
            "authored-invisible",
            "unconverted",
        }:
            raise SpellbookVisualIngressError(
                f"spellbook visual binding status is invalid: {object_id}"
            )
        if status in {"model", "horde-member"}:
            model = row.get("model")
            if not isinstance(model, str) or not model.endswith(".glb"):
                raise SpellbookVisualIngressError(
                    f"spellbook visual binding has no model output: {object_id}"
                )
            _safe_path(model, "spellbook visual binding model")


__all__ = [
    "MAX_VISUAL_OBJECTS",
    "SpellbookVisualIngressError",
    "build_spellbook_visual_closures",
    "spellbook_visual_recipe_parts",
    "validate_spellbook_visual_bindings",
    "visual_object_ids",
]
