"""Per-map castle/siege capability derivation (lane L1).

Replaces the admission lane's single five-blocker seal across all ten castle
maps with a per-map requirement set derived from retail data:

1. :func:`build_retail_object_index` registers every ``Object``,
   ``ChildObject`` and ``ObjectReskin`` definition site under
   ``data/ini/object/**`` and resolves ``ChildObject``/``ObjectReskin``
   parent inheritance.  (The design lane's own index missed ``ObjectReskin``
   entirely; the 759 reskin definitions are most of its "unresolved" residue.)
2. :func:`derive_map_capabilities` joins the index against a compiled map's
   placed ``typeName`` rows and counts the placements that need each
   gameplay capability.

Detection keys are retail-authored data (docs/castle-siege-design.md 1.2):

- gate          = ``BLOCKING_GATE``/``WALL_GATE`` KindOf or a
                  ``GateOpenAndCloseBehavior`` module
- garrison      = a ``HordeGarrisonContain``/``GarrisonContain`` module.
                  NEVER the ``GARRISON`` KindOf:
                  retail's ``Erebortower2`` carries the KindOf plus an
                  exit-garrison command set but authors no contain module and
                  can never hold anyone.  That is a retail defect this index
                  replicates, not one it fixes.  And never a bare ``*Contain``
                  suffix match either: keeps/thrones/strongholds author
                  ``CitadelSlaughterHordeContain``, which is not a garrison.
- walk-on-top   = ``WALK_ON_TOP_OF_WALL`` KindOf
- scaleable     = ``SCALEABLE_WALL`` KindOf
- wall-mounted  = ``WALL_UPGRADE``/``WALL_HUB`` KindOf

``skirmish-ai-libraries`` is deliberately NOT derivable here: it is a
family-level requirement evidenced by ``maps/mapcache.ini`` multiplayer
registry rows and per-map ``AIBase`` bindings (design 2.5), not by placed
objects.  The contract layer adds it; see ``map_profile``.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Iterable, Mapping, Sequence

from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    _ancestry,
    _effective_top_blocks,
    _kind_of,
    _object_index,
)


#: Canonical capability vocabulary and its canonical order.  The v1 blocker
#: order is preserved as a subsequence; ``scaleable-walls`` slots in next to
#: its sibling wall-traversal capability ``walkable-walls``.
CAPABILITY_VOCABULARY: tuple[str, ...] = (
    "walkable-walls",
    "scaleable-walls",
    "defendable-gates",
    "wall-garrisons",
    "wall-mounted-defenses",
    "skirmish-ai-libraries",
)

_VOCABULARY_RANK = {name: rank for rank, name in enumerate(CAPABILITY_VOCABULARY)}

_GATE_KIND_OF = frozenset({"BLOCKING_GATE", "WALL_GATE"})
_GATE_BEHAVIOR = "gateopenandclosebehavior"
_WALL_MOUNTED_KIND_OF = frozenset({"WALL_UPGRADE", "WALL_HUB"})
_WALK_ON_TOP_KIND_OF = "WALK_ON_TOP_OF_WALL"
_SCALEABLE_KIND_OF = "SCALEABLE_WALL"
#: The real garrison modules.  A bare ``*Contain`` suffix overcounts: every
#: castle-map keep/throne/stronghold authors ``CitadelSlaughterHordeContain``,
#: which is the citadel slaughter-house contain, never a player garrison.
#: (Design 1.2's "a real ``*Contain`` module" key is imprecise here; its own
#: matrix only balances with these two module kinds.)
_GARRISON_MODULES = frozenset({"hordegarrisoncontain", "garrisoncontain"})


class CastleCapabilityError(ValueError):
    """The object corpus or a requested capability subset is malformed."""


@dataclass(frozen=True, slots=True)
class ObjectTypeInfo:
    """One effective retail object definition, inheritance resolved."""

    name: str
    definition: str
    kind_of: tuple[str, ...]
    behaviors: tuple[str, ...]
    source_virtual_path: str


@dataclass(frozen=True, slots=True)
class MapCapabilityDerivation:
    """Placement counts and derived requirements for one compiled map."""

    gates: int
    garrisons: int
    walk_on_top: int
    scaleable: int
    wall_mounted: int
    unresolved: tuple[str, ...]
    required: tuple[str, ...]


def build_retail_object_index(
    documents: Mapping[str, bytes]
) -> dict[str, ObjectTypeInfo]:
    """Index every object definition under ``data/ini/object/**``.

    ``documents`` maps posix virtual paths to source bytes (the whole
    effective-assets tree, or any superset of the object corpus).  The
    returned mapping is keyed by case-folded type name; duplicate names keep
    the last declaration, matching retail's last-declaration-wins semantic.
    """

    try:
        raw = _object_index(documents)
    except PlayableUnitCompilerError as exc:
        raise CastleCapabilityError(str(exc)) from exc
    index: dict[str, ObjectTypeInfo] = {}
    for folded, item in raw.items():
        target = item
        if (
            item.parent is not None
            and item.kind.casefold() == "object"
            and item.parent.casefold() not in raw
        ):
            # A plain ``Object`` header's second token that names no defined
            # object is a retail editor annotation (``Object Water Plane``,
            # ``Object MoriaDebrisPileA (Rocks)``), never a template.  Only
            # ``ChildObject``/``ObjectReskin`` headers carry real parents, and
            # those still fail closed below when unresolvable.
            target = replace(item, parent=None)
        try:
            ancestry = _ancestry(raw, target)
        except PlayableUnitCompilerError as exc:
            raise CastleCapabilityError(str(exc)) from exc
        behaviors = tuple(
            block.kind
            for block in _effective_top_blocks(ancestry)
            if (block.header_key or "").casefold() == "behavior"
        )
        index[folded] = ObjectTypeInfo(
            name=item.name,
            definition=item.kind,
            kind_of=_kind_of(ancestry),
            behaviors=behaviors,
            source_virtual_path=item.source_virtual_path,
        )
    return index


def _is_gate(info: ObjectTypeInfo) -> bool:
    return bool(_GATE_KIND_OF.intersection(info.kind_of)) or any(
        behavior.casefold() == _GATE_BEHAVIOR for behavior in info.behaviors
    )


def _is_garrison(info: ObjectTypeInfo) -> bool:
    return any(
        behavior.casefold() in _GARRISON_MODULES for behavior in info.behaviors
    )


def derive_map_capabilities(
    index: Mapping[str, ObjectTypeInfo], type_names: Iterable[str]
) -> MapCapabilityDerivation:
    """Count capability-bearing placements for one map's ``typeName`` rows.

    Placement rows are counted, not unique types: Dol Guldur's 251 scaleable
    wall pieces are 251 placements of a handful of types.  ``type_names``
    lookup is case-insensitive; unresolved names are reported once each,
    keeping the first-seen authored spelling.
    """

    gates = garrisons = walk_on_top = scaleable = wall_mounted = 0
    unresolved: dict[str, str] = {}
    for type_name in type_names:
        info = index.get(str(type_name).casefold())
        if info is None:
            unresolved.setdefault(str(type_name).casefold(), str(type_name))
            continue
        if _is_gate(info):
            gates += 1
        if _is_garrison(info):
            garrisons += 1
        if _WALK_ON_TOP_KIND_OF in info.kind_of:
            walk_on_top += 1
        if _SCALEABLE_KIND_OF in info.kind_of:
            scaleable += 1
        if _WALL_MOUNTED_KIND_OF.intersection(info.kind_of):
            wall_mounted += 1
    flags = {
        "walkable-walls": walk_on_top > 0,
        "scaleable-walls": scaleable > 0,
        "defendable-gates": gates > 0,
        "wall-garrisons": garrisons > 0,
        "wall-mounted-defenses": wall_mounted > 0,
    }
    return MapCapabilityDerivation(
        gates=gates,
        garrisons=garrisons,
        walk_on_top=walk_on_top,
        scaleable=scaleable,
        wall_mounted=wall_mounted,
        unresolved=tuple(
            spelling
            for _, spelling in sorted(unresolved.items(), key=lambda row: row[0])
        ),
        required=tuple(name for name in CAPABILITY_VOCABULARY if flags.get(name, False)),
    )


def validate_capability_subset(required: Sequence[object]) -> tuple[str, ...]:
    """Return ``required`` as a canonical tuple, or fail closed.

    A valid subset is non-empty, all-strings, free of duplicates, drawn from
    :data:`CAPABILITY_VOCABULARY`, and listed in canonical order.
    """

    if isinstance(required, (str, bytes)) or not isinstance(required, Sequence):
        raise CastleCapabilityError("castle capability subset must be a list of names")
    names: list[str] = []
    for value in required:
        if not isinstance(value, str) or value not in _VOCABULARY_RANK:
            raise CastleCapabilityError(
                f"unknown castle capability name: {value!r}"
            )
        names.append(value)
    if not names:
        raise CastleCapabilityError("castle capability subset must not be empty")
    if len(set(names)) != len(names):
        raise CastleCapabilityError("castle capability subset has duplicates")
    if [rank for rank in (_VOCABULARY_RANK[name] for name in names)] != sorted(
        _VOCABULARY_RANK[name] for name in names
    ):
        raise CastleCapabilityError(
            "castle capability subset is not in canonical order"
        )
    return tuple(names)


def castle_siege_contract_v2(required: Sequence[str]) -> dict[str, object]:
    """Build the version-2 ``castleSiege`` metadata contract.

    The v2 document carries the derived per-map requirement set; the runtime
    computes ``blockers = required - implemented`` at load time, so no
    blocker list is authored here at all.
    """

    return {
        "version": 2,
        "family": "retail-castle-siege-skirmish",
        "gameplayStatus": "blocked-named-gaps",
        "admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
        "required": list(validate_capability_subset(required)),
    }


__all__ = [
    "CAPABILITY_VOCABULARY",
    "CastleCapabilityError",
    "MapCapabilityDerivation",
    "ObjectTypeInfo",
    "build_retail_object_index",
    "castle_siege_contract_v2",
    "derive_map_capabilities",
    "validate_capability_subset",
]
