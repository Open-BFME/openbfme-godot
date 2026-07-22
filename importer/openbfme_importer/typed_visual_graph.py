"""Typed, provenance-preserving Object visual dependency resolution.

This pass sits between the include-aware :mod:`sage_cst` syntax tree and the
physical leaf resolvers.  It deliberately implements only the Object visual
contract required by BFME2 content: Object-family inheritance, tagged ``Draw``
module replacement/removal, model-condition and animation states, W3D model /
skeleton / animation identifiers, visual texture leaves, and shadow/recolour
properties.

There is no fallback asset policy here.  Every authored token is either tied
to exact physical evidence, classified as a source-language semantic token,
or retained as an explicit diagnostic with its source location.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Iterable, Literal, Mapping

from .sage_cst import (
    ResolvedSageCst,
    SageAssignment,
    SageBlock,
    SageIncludeRef,
    SageObject,
    SageScript,
    SageSourceLocation,
    resolve_sage_documents,
)
from .visual_leaf import (
    VisualLeafRequest,
    diagnose_visual_leaves,
)
from .w3d_index import (
    W3DIndex,
    W3DReferenceRequest,
    resolve_w3d_references_partial,
)


MAX_TARGET_OBJECTS = 16_384
MAX_OBJECT_NAME_LENGTH = 255
MAX_INHERITANCE_DEPTH = 256
MAX_TYPED_REFERENCES = 100_000
MAX_VALUE_TOKENS = 256

AssetStatus = Literal["resolved", "semantic", "missing", "ambiguous", "invalid"]

_STATE_KINDS = frozenset(
    {
        "defaultmodelconditionstate",
        "modelconditionstate",
        "conditionstate",
        "animationstate",
        "idleanimationstate",
        "transitionstate",
    }
)
_REFERENCE_KEYS = {
    "model": ("w3d", "model", "model"),
    "modelname": ("w3d", "model", "floor-model"),
    "skeleton": ("w3d", "hierarchy", "skeleton"),
    "animationname": ("w3d", "animation", "animation"),
    "texture": ("visual", "texture", "texture"),
    "randomtexture": ("visual", "texture", "random-texture"),
    "upgradetexture": ("visual", "texture", "upgrade-texture"),
    "weathertexture": ("visual", "texture", "weather-texture"),
    "shadowtexture": ("visual", "shadow", "shadow-texture"),
    "housecolor": ("visual", "house-color", "house-color"),
    "attachedmodel": ("visual", "attached-model", "attached-model"),
    "particlename": ("visual", "particle", "particle"),
}
_RECOLOUR_KEYS = frozenset({"recolorhouse", "oktochangemodelcolor"})
_BOOLEAN_KEYS = frozenset(
    {"recolorhouse", "oktochangemodelcolor", "shadowoverridelodvisibility"}
)
_LIFECYCLE_ORDER = {
    "intact": 0,
    "construction": 1,
    "damaged": 2,
    "really-damaged": 3,
    "rubble": 4,
    "post-rubble": 5,
}
_CONSTRUCTION_CONDITIONS = frozenset(
    {
        "AWAITING_CONSTRUCTION",
        "ACTIVELY_BEING_CONSTRUCTED",
        "PARTIALLY_CONSTRUCTED",
        "CONSTRUCTION_COMPLETE",
        "BUILD_PLACEMENT_CURSOR",
        "PHANTOM_STRUCTURE",
    }
)
_POST_RUBBLE_CONDITIONS = frozenset({"POST_RUBBLE", "POST_COLLAPSE"})


def _sort_text(value: str) -> tuple[str, str]:
    return value.casefold(), value


@dataclass(frozen=True, slots=True)
class VisualProvenance:
    """The exact Object and source line that authored a resolved fact."""

    defining_object: str
    virtual_path: str
    line: int
    inheritance_distance: int
    scope_path: tuple[str, ...] = ()

    def neutral(self) -> dict[str, object]:
        return {
            "definingObject": self.defining_object,
            "virtualPath": self.virtual_path,
            "line": self.line,
            "inheritanceDistance": self.inheritance_distance,
            "scopePath": list(self.scope_path),
        }


@dataclass(frozen=True, slots=True)
class TypedAssetReference:
    """One authored visual token and its exact physical/semantic outcome."""

    identifier: str
    kind: str
    usage: str
    status: AssetStatus
    provenance: VisualProvenance
    conditions: tuple[str, ...]
    lifecycle_phases: tuple[str, ...]
    physical_virtual_paths: tuple[str, ...] = ()
    evidence: tuple[str, ...] = ()
    reason: str | None = None
    candidates: tuple[str, ...] = ()

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "identifier": self.identifier,
            "kind": self.kind,
            "usage": self.usage,
            "status": self.status,
            "conditions": list(self.conditions),
            "lifecyclePhases": list(self.lifecycle_phases),
            "provenance": self.provenance.neutral(),
        }
        if self.physical_virtual_paths:
            result["physicalVirtualPaths"] = list(self.physical_virtual_paths)
        if self.evidence:
            result["evidence"] = list(self.evidence)
        if self.reason is not None:
            result["reason"] = self.reason
        if self.candidates:
            result["candidates"] = list(self.candidates)
        return result


@dataclass(frozen=True, slots=True)
class TypedVisualProperty:
    """An authored shadow or recolour property, without guessed coercion."""

    key: str
    value: str
    enabled: bool | None
    provenance: VisualProvenance
    conditions: tuple[str, ...]
    lifecycle_phases: tuple[str, ...]

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "key": self.key,
            "value": self.value,
            "conditions": list(self.conditions),
            "lifecyclePhases": list(self.lifecycle_phases),
            "provenance": self.provenance.neutral(),
        }
        if self.enabled is not None:
            result["enabled"] = self.enabled
        return result


@dataclass(frozen=True, slots=True)
class TypedVisualState:
    family: str
    conditions: tuple[str, ...]
    lifecycle_phases: tuple[str, ...]
    provenance: VisualProvenance
    references: tuple[TypedAssetReference, ...]
    properties: tuple[TypedVisualProperty, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "family": self.family,
            "conditions": list(self.conditions),
            "lifecyclePhases": list(self.lifecycle_phases),
            "provenance": self.provenance.neutral(),
            "references": [item.neutral() for item in self.references],
            "properties": [item.neutral() for item in self.properties],
        }


@dataclass(frozen=True, slots=True)
class TypedDrawModule:
    module_kind: str
    instance_tag: str | None
    provenance: VisualProvenance
    inherited: bool
    states: tuple[TypedVisualState, ...]
    references: tuple[TypedAssetReference, ...]
    properties: tuple[TypedVisualProperty, ...]

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "moduleKind": self.module_kind,
            "inherited": self.inherited,
            "provenance": self.provenance.neutral(),
            "states": [item.neutral() for item in self.states],
            "references": [item.neutral() for item in self.references],
            "properties": [item.neutral() for item in self.properties],
        }
        if self.instance_tag is not None:
            result["instanceTag"] = self.instance_tag
        return result


@dataclass(frozen=True, slots=True)
class TypedVisualObject:
    name: str
    object_kind: str
    source: SageSourceLocation
    ancestry: tuple[str, ...]
    inheritance_complete: bool
    lifecycle_coverage: tuple[str, ...]
    draw_modules: tuple[TypedDrawModule, ...]
    references: tuple[TypedAssetReference, ...]
    properties: tuple[TypedVisualProperty, ...]

    def all_references(self) -> tuple[TypedAssetReference, ...]:
        result = list(self.references)
        for module in self.draw_modules:
            result.extend(module.references)
            for state in module.states:
                result.extend(state.references)
        return tuple(result)

    def neutral(self) -> dict[str, object]:
        return {
            "name": self.name,
            "objectKind": self.object_kind,
            "source": {
                "virtualPath": self.source.virtual_path,
                "line": self.source.line,
            },
            "ancestry": list(self.ancestry),
            "inheritanceComplete": self.inheritance_complete,
            "lifecycleCoverage": list(self.lifecycle_coverage),
            "drawModules": [item.neutral() for item in self.draw_modules],
            "references": [item.neutral() for item in self.references],
            "properties": [item.neutral() for item in self.properties],
        }


@dataclass(frozen=True, slots=True)
class TypedVisualDiagnostic:
    code: str
    message: str
    object_name: str
    virtual_path: str | None = None
    line: int | None = None
    candidates: tuple[str, ...] = ()

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "code": self.code,
            "message": self.message,
            "objectName": self.object_name,
        }
        if self.virtual_path is not None:
            result["virtualPath"] = self.virtual_path
        if self.line is not None:
            result["line"] = self.line
        if self.candidates:
            result["candidates"] = list(self.candidates)
        return result


@dataclass(frozen=True, slots=True)
class TypedVisualGraph:
    entry_virtual_path: str
    objects: tuple[TypedVisualObject, ...]
    diagnostics: tuple[TypedVisualDiagnostic, ...]

    @property
    def references(self) -> tuple[TypedAssetReference, ...]:
        result: list[TypedAssetReference] = []
        for item in self.objects:
            result.extend(item.all_references())
        return tuple(result)

    @property
    def unresolved(self) -> tuple[TypedAssetReference, ...]:
        return tuple(
            item
            for item in self.references
            if item.status in {"missing", "ambiguous", "invalid"}
        )

    @property
    def complete(self) -> bool:
        return not self.diagnostics and not self.unresolved

    def neutral(self) -> dict[str, object]:
        references = self.references
        unresolved = self.unresolved
        return {
            "schema": "openbfme.typed-visual-graph",
            "schemaVersion": 0,
            "entryVirtualPath": self.entry_virtual_path,
            "objectCount": len(self.objects),
            "referenceCount": len(references),
            "unresolvedCount": len(unresolved),
            "complete": self.complete,
            "objects": [item.neutral() for item in self.objects],
            "diagnostics": [item.neutral() for item in self.diagnostics],
        }


class TypedVisualGraphError(ValueError):
    """Raised by :func:`require_complete_typed_visual_graph`."""

    def __init__(self, graph: TypedVisualGraph):
        self.graph = graph
        summary = [item.code for item in graph.diagnostics]
        summary.extend(f"{item.status}:{item.kind}:{item.identifier}" for item in graph.unresolved)
        super().__init__("typed visual graph is incomplete: " + ", ".join(summary))


@dataclass(frozen=True, slots=True)
class _PendingReference:
    identifier: str
    kind: str
    usage: str
    resolver: str
    provenance: VisualProvenance
    conditions: tuple[str, ...]
    lifecycle_phases: tuple[str, ...]
    semantic: bool = False
    semantic_reason: str | None = None


@dataclass(slots=True)
class _PendingState:
    family: str
    conditions: tuple[str, ...]
    lifecycle_phases: tuple[str, ...]
    provenance: VisualProvenance
    reference_indexes: list[int]
    properties: list[TypedVisualProperty]


@dataclass(slots=True)
class _PendingModule:
    module_kind: str
    instance_tag: str | None
    provenance: VisualProvenance
    inherited: bool
    states: list[_PendingState]
    reference_indexes: list[int]
    properties: list[TypedVisualProperty]


@dataclass(slots=True)
class _PendingObject:
    source_object: SageObject
    ancestry: tuple[str, ...]
    inheritance_complete: bool
    modules: list[_PendingModule]
    reference_indexes: list[int]
    properties: list[TypedVisualProperty]


@dataclass(frozen=True, slots=True)
class _EffectiveDraw:
    block: SageBlock
    defining_object: SageObject
    inheritance_distance: int


def _validated_targets(object_names: Iterable[str]) -> tuple[str, ...]:
    result: dict[str, str] = {}
    for raw_name in object_names:
        if len(result) >= MAX_TARGET_OBJECTS:
            raise ValueError(f"target object count exceeds {MAX_TARGET_OBJECTS} limit")
        if (
            not isinstance(raw_name, str)
            or not raw_name
            or raw_name != raw_name.strip()
            or len(raw_name) > MAX_OBJECT_NAME_LENGTH
            or any(character.isspace() for character in raw_name)
        ):
            raise ValueError(f"unsafe target object name: {raw_name!r}")
        key = raw_name.casefold()
        if key in result:
            raise ValueError(f"duplicate target object name: {raw_name!r}")
        result[key] = raw_name
    if not result:
        raise ValueError("at least one target object name is required")
    return tuple(sorted(result.values(), key=_sort_text))


def _tokens(value: str) -> tuple[str, ...]:
    """Split a SAGE assignment value without treating backslashes as escapes."""

    result: list[str] = []
    current: list[str] = []
    quote: str | None = None
    index = 0
    while index < len(value):
        character = value[index]
        if quote is not None:
            if (
                character == "\\"
                and index + 1 < len(value)
                and value[index + 1] in {quote, "\\"}
            ):
                current.append(value[index + 1])
                index += 2
                continue
            elif character == quote:
                quote = None
            else:
                current.append(character)
        elif character in {'"', "'"}:
            quote = character
        elif character.isspace():
            if current:
                result.append("".join(current))
                current = []
        else:
            current.append(character)
        if len(result) > MAX_VALUE_TOKENS:
            raise ValueError(f"assignment value exceeds {MAX_VALUE_TOKENS} token limit")
        index += 1
    if quote is not None:
        raise ValueError("unterminated quote in assignment value")
    if current:
        result.append("".join(current))
    if len(result) > MAX_VALUE_TOKENS:
        raise ValueError(f"assignment value exceeds {MAX_VALUE_TOKENS} token limit")
    return tuple(result)


def _lifecycle(conditions: tuple[str, ...]) -> tuple[str, ...]:
    folded = {item.upper() for item in conditions}
    phases: set[str] = set()
    if folded & _CONSTRUCTION_CONDITIONS:
        phases.add("construction")
    if "DAMAGED" in folded:
        phases.add("damaged")
    if "REALLYDAMAGED" in folded:
        phases.add("really-damaged")
    if "RUBBLE" in folded:
        phases.add("rubble")
    if folded & _POST_RUBBLE_CONDITIONS:
        phases.add("post-rubble")
    if not phases:
        phases.add("intact")
    return tuple(sorted(phases, key=lambda item: _LIFECYCLE_ORDER[item]))


def _enabled(key: str, value: str) -> bool | None:
    if key.casefold() not in _BOOLEAN_KEYS:
        return None
    tokens = _tokens(value)
    if len(tokens) != 1:
        return None
    folded = tokens[0].casefold()
    if folded in {"yes", "true", "1"}:
        return True
    if folded in {"no", "false", "0"}:
        return False
    return None


def _is_visual_property(key: str) -> bool:
    folded = key.casefold()
    return folded.startswith("shadow") or folded in _RECOLOUR_KEYS


def _scope_name(block: SageBlock) -> str:
    value = block.kind
    if block.instance_tag:
        value += f" {block.instance_tag}"
    elif block.header_tokens:
        value += " " + " ".join(block.header_tokens)
    return value


def _unexpected_body_item(item: object, owner: str) -> TypeError:
    virtual_path = getattr(item, "source_virtual_path", None)
    line = getattr(item, "line", None)
    location = (
        f" at {virtual_path}:{line}"
        if isinstance(virtual_path, str) and isinstance(line, int)
        else ""
    )
    return TypeError(
        f"unexpected SAGE body item type in {owner}{location}: "
        f"{type(item).__name__}; inline fragments must resolve to assignments/blocks"
    )


def _is_nonphysical_cst_evidence(item: object) -> bool:
    """Recognize CST evidence that cannot name an external visual leaf.

    ``resolve_sage_documents`` deliberately leaves the directive beside the
    spliced assignments/blocks so provenance is auditable.  It is not itself a
    visual statement, but an unresolved directive must never disappear here.
    Opaque drawable scripts can alter subobjects/transitions, yet the CST keeps
    their exact source and they cannot add a model, texture, or animation file
    dependency without a parsed assignment.
    """

    return (
        isinstance(item, SageScript)
        or isinstance(item, SageIncludeRef)
        and item.resolved_virtual_path is not None
    )


def _provenance(
    defining_object: SageObject,
    source: SageAssignment | SageBlock,
    inheritance_distance: int,
    scope_path: tuple[str, ...],
) -> VisualProvenance:
    return VisualProvenance(
        defining_object=defining_object.name,
        virtual_path=source.source_virtual_path,
        line=source.line,
        inheritance_distance=inheritance_distance,
        scope_path=scope_path,
    )


def _append_reference(
    pending: list[_PendingReference],
    assignment: SageAssignment,
    defining_object: SageObject,
    inheritance_distance: int,
    scope_path: tuple[str, ...],
    conditions: tuple[str, ...],
    lifecycle_phases: tuple[str, ...],
) -> list[int]:
    folded_key = assignment.key.casefold()
    rule = _REFERENCE_KEYS.get(folded_key)
    if rule is None:
        return []
    resolver, kind, usage = rule
    try:
        tokens = _tokens(assignment.value)
    except ValueError as exc:
        tokens = (assignment.value,)
        token_error = str(exc)
    else:
        token_error = None
        if folded_key in {"randomtexture", "upgradetexture"}:
            if len(tokens) != 3 or not tokens[1].isdigit():
                tokens = (assignment.value,)
                token_error = (
                    f"{assignment.key} requires texture, numeric slot, texture"
                )
            else:
                tokens = (tokens[0], tokens[2])
        elif folded_key == "weathertexture":
            if len(tokens) != 2:
                tokens = (assignment.value,)
                token_error = "WeatherTexture requires weather condition and texture"
            else:
                tokens = (tokens[1],)
        elif folded_key == "model" and len(tokens) == 2:
            # BFME2 model-condition states stack additional meshes beside the
            # primary model with the authored ``ExtraMesh:Yes`` suffix (for
            # example the dead-orc corpse piles in evilfactionprops.ini).  The
            # model id still resolves exactly; the marker routes the row out
            # of the single-default-model contract into explicit extra-mesh
            # handling.  Any other suffix stays invalid-authored.
            if tokens[1].casefold() == "extramesh:yes":
                kind, usage = "extra-mesh", "extra-mesh"
                tokens = (tokens[0],)
            else:
                tokens = (assignment.value,)
                token_error = f"{assignment.key} requires exactly one reference"
        elif folded_key != "texture" and len(tokens) != 1:
            tokens = (assignment.value,)
            token_error = f"{assignment.key} requires exactly one reference"
        elif not tokens:
            tokens = ("",)
            token_error = "empty visual reference"

    indexes: list[int] = []
    for token in tokens:
        if len(pending) >= MAX_TYPED_REFERENCES:
            raise ValueError(f"typed visual reference count exceeds {MAX_TYPED_REFERENCES} limit")
        semantic = False
        semantic_reason: str | None = None
        if resolver == "w3d" and kind == "model" and token.casefold() == "none":
            semantic = True
            semantic_reason = "sage-none-model"
        elif resolver == "w3d" and kind == "hierarchy" and token.casefold() == "model":
            semantic = True
            semantic_reason = "sage-model-skeleton"
        if token_error is not None:
            resolver_value = "invalid"
            semantic_reason = token_error
        else:
            resolver_value = resolver
        indexes.append(len(pending))
        pending.append(
            _PendingReference(
                identifier=token,
                kind=kind,
                usage=usage,
                resolver=resolver_value,
                provenance=_provenance(
                    defining_object,
                    assignment,
                    inheritance_distance,
                    scope_path,
                ),
                conditions=conditions,
                lifecycle_phases=lifecycle_phases,
                semantic=semantic,
                semantic_reason=semantic_reason,
            )
        )
    return indexes


def _append_property(
    properties: list[TypedVisualProperty],
    assignment: SageAssignment,
    defining_object: SageObject,
    inheritance_distance: int,
    scope_path: tuple[str, ...],
    conditions: tuple[str, ...],
    lifecycle_phases: tuple[str, ...],
) -> None:
    if not _is_visual_property(assignment.key):
        return
    properties.append(
        TypedVisualProperty(
            key=assignment.key,
            value=assignment.value,
            enabled=_enabled(assignment.key, assignment.value),
            provenance=_provenance(
                defining_object, assignment, inheritance_distance, scope_path
            ),
            conditions=conditions,
            lifecycle_phases=lifecycle_phases,
        )
    )


def _walk_assignments(
    items: tuple[object, ...],
    *,
    defining_object: SageObject,
    inheritance_distance: int,
    scope_path: tuple[str, ...],
    conditions: tuple[str, ...],
    lifecycle_phases: tuple[str, ...],
    pending_references: list[_PendingReference],
    reference_indexes: list[int],
    properties: list[TypedVisualProperty],
    stop_at_states: bool,
) -> None:
    for item in items:
        if isinstance(item, SageAssignment):
            reference_indexes.extend(
                _append_reference(
                    pending_references,
                    item,
                    defining_object,
                    inheritance_distance,
                    scope_path,
                    conditions,
                    lifecycle_phases,
                )
            )
            _append_property(
                properties,
                item,
                defining_object,
                inheritance_distance,
                scope_path,
                conditions,
                lifecycle_phases,
            )
            continue
        if _is_nonphysical_cst_evidence(item):
            continue
        if not isinstance(item, SageBlock):
            raise _unexpected_body_item(item, "Draw traversal")
        if stop_at_states and item.kind.casefold() in _STATE_KINDS:
            continue
        _walk_assignments(
            item.items,
            defining_object=defining_object,
            inheritance_distance=inheritance_distance,
            scope_path=(*scope_path, _scope_name(item)),
            conditions=conditions,
            lifecycle_phases=lifecycle_phases,
            pending_references=pending_references,
            reference_indexes=reference_indexes,
            properties=properties,
            stop_at_states=stop_at_states,
        )


def _collect_states(
    block: SageBlock,
    *,
    defining_object: SageObject,
    inheritance_distance: int,
    module_scope: tuple[str, ...],
    pending_references: list[_PendingReference],
) -> list[_PendingState]:
    states: list[_PendingState] = []
    for item in block.items:
        if isinstance(item, SageAssignment):
            continue
        if _is_nonphysical_cst_evidence(item):
            continue
        if not isinstance(item, SageBlock):
            raise _unexpected_body_item(item, f"{block.kind} state traversal")
        folded = item.kind.casefold()
        if folded in _STATE_KINDS:
            conditions = tuple(item.header_tokens)
            phases = _lifecycle(conditions)
            scope = (*module_scope, _scope_name(item))
            references: list[int] = []
            properties: list[TypedVisualProperty] = []
            _walk_assignments(
                item.items,
                defining_object=defining_object,
                inheritance_distance=inheritance_distance,
                scope_path=scope,
                conditions=conditions,
                lifecycle_phases=phases,
                pending_references=pending_references,
                reference_indexes=references,
                properties=properties,
                stop_at_states=True,
            )
            states.append(
                _PendingState(
                    family=item.kind,
                    conditions=conditions,
                    lifecycle_phases=phases,
                    provenance=_provenance(
                        defining_object, item, inheritance_distance, scope
                    ),
                    reference_indexes=references,
                    properties=properties,
                )
            )
            continue
        states.extend(
            _collect_states(
                item,
                defining_object=defining_object,
                inheritance_distance=inheritance_distance,
                module_scope=(*module_scope, _scope_name(item)),
                pending_references=pending_references,
            )
        )
    return states


def _effective_draws(
    ancestry: tuple[SageObject, ...],
    diagnostics: list[TypedVisualDiagnostic],
    target_name: str,
) -> list[_EffectiveDraw]:
    result: list[_EffectiveDraw] = []
    tag_indexes: dict[str, int] = {}
    total = len(ancestry)
    for object_index, item in enumerate(ancestry):
        distance = total - object_index - 1
        for assignment in item.assignments:
            if assignment.key.casefold() != "removemodule":
                continue
            tokens = _tokens(assignment.value)
            if len(tokens) != 1:
                diagnostics.append(
                    TypedVisualDiagnostic(
                        "invalid-remove-module",
                        f"RemoveModule requires exactly one module tag: {assignment.value!r}",
                        target_name,
                        assignment.source_virtual_path,
                        assignment.line,
                    )
                )
                continue
            index = tag_indexes.get(tokens[0].casefold())
            if index is None:
                continue
            result.pop(index)
            tag_indexes = {
                value.block.instance_tag.casefold(): position
                for position, value in enumerate(result)
                if value.block.instance_tag is not None
            }

        local_tags: set[str] = set()
        for body_item in item.items:
            if isinstance(body_item, SageAssignment):
                continue
            if _is_nonphysical_cst_evidence(body_item):
                continue
            if not isinstance(body_item, SageBlock):
                raise _unexpected_body_item(
                    body_item, f"Object {item.name} Draw traversal"
                )
            draw = body_item
            if draw.header_key is None or draw.header_key.casefold() != "draw":
                continue
            effective = _EffectiveDraw(draw, item, distance)
            if draw.instance_tag is None:
                result.append(effective)
                continue
            tag_key = draw.instance_tag.casefold()
            if tag_key in local_tags:
                diagnostics.append(
                    TypedVisualDiagnostic(
                        "duplicate-draw-module-tag",
                        f"duplicate Draw module tag {draw.instance_tag!r}",
                        target_name,
                        draw.source_virtual_path,
                        draw.line,
                    )
                )
            local_tags.add(tag_key)
            previous = tag_indexes.get(tag_key)
            if previous is None:
                tag_indexes[tag_key] = len(result)
                result.append(effective)
            else:
                result[previous] = effective
    return result


def _object_index(objects: tuple[SageObject, ...]) -> dict[str, tuple[SageObject, ...]]:
    grouped: dict[str, list[SageObject]] = {}
    for item in objects:
        grouped.setdefault(item.name.casefold(), []).append(item)
    return {
        key: tuple(
            sorted(
                values,
                key=lambda item: (
                    _sort_text(item.source_virtual_path),
                    item.line,
                    _sort_text(item.name),
                ),
            )
        )
        for key, values in grouped.items()
    }


def _ancestry(
    target: SageObject,
    index: Mapping[str, tuple[SageObject, ...]],
    diagnostics: list[TypedVisualDiagnostic],
) -> tuple[tuple[SageObject, ...], bool]:
    child_to_root: list[SageObject] = []
    seen: dict[str, SageObject] = {}
    current = target
    complete = True
    while True:
        if len(child_to_root) >= MAX_INHERITANCE_DEPTH:
            diagnostics.append(
                TypedVisualDiagnostic(
                    "inheritance-depth",
                    f"inheritance depth exceeds {MAX_INHERITANCE_DEPTH} limit",
                    target.name,
                    current.source_virtual_path,
                    current.line,
                )
            )
            complete = False
            break
        key = current.name.casefold()
        if key in seen:
            cycle = tuple(item.name for item in (*child_to_root, current))
            diagnostics.append(
                TypedVisualDiagnostic(
                    "inheritance-cycle",
                    "Object inheritance cycle: " + " -> ".join(cycle),
                    target.name,
                    current.source_virtual_path,
                    current.line,
                    cycle,
                )
            )
            complete = False
            break
        seen[key] = current
        child_to_root.append(current)
        if current.parent is None:
            break
        candidates = index.get(current.parent.casefold(), ())
        if not candidates:
            diagnostics.append(
                TypedVisualDiagnostic(
                    "missing-parent",
                    f"missing parent Object {current.parent!r}",
                    target.name,
                    current.source_virtual_path,
                    current.line,
                )
            )
            complete = False
            break
        if len(candidates) != 1:
            labels = tuple(
                f"{item.source_virtual_path}:{item.line}" for item in candidates
            )
            diagnostics.append(
                TypedVisualDiagnostic(
                    "ambiguous-parent",
                    f"ambiguous parent Object {current.parent!r}",
                    target.name,
                    current.source_virtual_path,
                    current.line,
                    labels,
                )
            )
            complete = False
            break
        current = candidates[0]
    return tuple(reversed(child_to_root)), complete


def _resolve_pending(
    pending: list[_PendingReference],
    w3d: W3DIndex,
    visual_catalog_virtual_paths: Iterable[str],
) -> tuple[TypedAssetReference, ...]:
    visual_primary_indexes: dict[int, int] = {}
    visual_compiled_indexes: dict[int, int] = {}
    visual_requests: list[VisualLeafRequest] = []
    for position, item in enumerate(pending):
        if item.resolver == "visual":
            visual_primary_indexes[position] = len(visual_requests)
            visual_requests.append(VisualLeafRequest(item.identifier, item.kind))
            pure = PurePosixPath(item.identifier)
            if (
                item.kind in {"texture", "shadow"}
                and pure.suffix.casefold() == ".tga"
            ):
                # Retail BIGs normally carry the compiled DDS leaf while the
                # Object INI retains its authored TGA identifier.  Probe the
                # exact extensionless stem as separate evidence; it is accepted
                # below only when it identifies one DDS and the authored TGA
                # itself was absent.
                visual_compiled_indexes[position] = len(visual_requests)
                visual_requests.append(
                    VisualLeafRequest(pure.with_suffix("").as_posix(), item.kind)
                )
    visual_batch = diagnose_visual_leaves(
        visual_catalog_virtual_paths, visual_requests
    )
    visual_diagnostics = {
        item.request_index: item for item in visual_batch.diagnostics
    }
    visual_results = {}
    for position, request_index in visual_primary_indexes.items():
        compiled_index = visual_compiled_indexes.get(position)
        visual_results[position] = (
            visual_batch.resolutions[request_index],
            visual_diagnostics.get(request_index),
            (
                visual_batch.resolutions[compiled_index]
                if compiled_index is not None
                else None
            ),
            (
                visual_diagnostics.get(compiled_index)
                if compiled_index is not None
                else None
            ),
        )

    results: list[TypedAssetReference] = []
    for position, item in enumerate(pending):
        if item.resolver == "invalid":
            results.append(
                TypedAssetReference(
                    item.identifier,
                    item.kind,
                    item.usage,
                    "invalid",
                    item.provenance,
                    item.conditions,
                    item.lifecycle_phases,
                    reason=item.semantic_reason or "invalid authored token",
                )
            )
            continue
        if item.semantic:
            results.append(
                TypedAssetReference(
                    item.identifier,
                    item.kind,
                    item.usage,
                    "semantic",
                    item.provenance,
                    item.conditions,
                    item.lifecycle_phases,
                    evidence=("source-semantic-token",),
                    reason=item.semantic_reason,
                )
            )
            continue
        if item.resolver == "visual":
            resolution, diagnostic, compiled_resolution, compiled_diagnostic = (
                visual_results[position]
            )
            compiled_evidence: tuple[str, ...] = ()
            if (
                resolution is None
                and diagnostic is not None
                and diagnostic.status == "missing"
            ):
                compiled_paths = (
                    tuple(leaf.virtual_path for leaf in compiled_resolution.leaves)
                    if compiled_resolution is not None
                    else ()
                )
                if (
                    len(compiled_paths) == 1
                    and PurePosixPath(compiled_paths[0]).suffix.casefold() == ".dds"
                ):
                    resolution = compiled_resolution
                    diagnostic = None
                    compiled_evidence = (
                        "sage-compiled-texture:exact-tga-stem-to-dds",
                    )
                elif (
                    compiled_diagnostic is not None
                    and compiled_diagnostic.status == "ambiguous"
                    and compiled_diagnostic.candidates
                    and all(
                        PurePosixPath(candidate).suffix.casefold() == ".dds"
                        for candidate in compiled_diagnostic.candidates
                    )
                ):
                    diagnostic = compiled_diagnostic
            if resolution is not None:
                results.append(
                    TypedAssetReference(
                        item.identifier,
                        item.kind,
                        item.usage,
                        "resolved",
                        item.provenance,
                        item.conditions,
                        item.lifecycle_phases,
                        physical_virtual_paths=tuple(
                            leaf.virtual_path for leaf in resolution.leaves
                        ),
                        evidence=compiled_evidence + tuple(
                            f"{leaf.role}:{leaf.evidence}" for leaf in resolution.leaves
                        ),
                    )
                )
            else:
                if diagnostic is None:
                    results.append(
                        TypedAssetReference(
                            item.identifier,
                            item.kind,
                            item.usage,
                            "invalid",
                            item.provenance,
                            item.conditions,
                            item.lifecycle_phases,
                            reason="visual resolver returned no resolution or diagnostic",
                        )
                    )
                else:
                    results.append(
                        TypedAssetReference(
                            item.identifier,
                            item.kind,
                            item.usage,
                            diagnostic.status,
                            item.provenance,
                            item.conditions,
                            item.lifecycle_phases,
                            reason=diagnostic.message,
                            candidates=diagnostic.candidates,
                        )
                    )
            continue

        try:
            batch = resolve_w3d_references_partial(
                w3d,
                [
                    W3DReferenceRequest(
                        (
                            "model"
                            if item.kind == "extra-mesh"
                            else item.kind
                        ),
                        item.identifier,
                    )
                ],
            )
        except (TypeError, ValueError) as exc:
            results.append(
                TypedAssetReference(
                    item.identifier,
                    item.kind,
                    item.usage,
                    "invalid",
                    item.provenance,
                    item.conditions,
                    item.lifecycle_phases,
                    reason=str(exc),
                )
            )
            continue
        if (
            batch.missing
            and not batch.ambiguous
            and item.kind == "animation"
            and "." in item.identifier
            and "/" not in item.identifier
            and "\\" not in item.identifier
        ):
            # Retail animation convention: a ``Hierarchy.Clip`` reference loads
            # the file ``<Clip>.w3d``.  The authored clip id inside that file
            # can legitimately differ from the file stem (retail clip-id drift,
            # e.g. ``RUELROND_SKL.RUELROND_IDLCT3`` ships inside
            # ``ruelrond_idlct3.w3d`` as the ``RUELROND_IDLC_T`` clip).  The
            # full identifier already failed, so the exact clip file stem is
            # the only retail-consistent resolution left.  A stem match is
            # only an animation when the file actually authors animation ids:
            # retail also points animation states at static model files (for
            # example GBWallrampart.GBWallrampart), which the engine treats as
            # a tolerated missing clip, never as a clip.
            try:
                fallback = resolve_w3d_references_partial(
                    w3d,
                    [
                        W3DReferenceRequest(
                            item.kind, item.identifier.split(".", 1)[1]
                        )
                    ],
                )
            except (TypeError, ValueError):
                fallback = None
            if fallback is not None and fallback.resolved:
                resolved_path = fallback.resolved[0].physical_virtual_path
                headers = next(
                    (
                        header
                        for header in w3d.file_headers
                        if header.virtual_path == resolved_path
                    ),
                    None,
                )
                if headers is not None and headers.animation_ids:
                    batch = fallback
            elif fallback is not None and fallback.ambiguous:
                batch = fallback
        if batch.resolved:
            resolved = batch.resolved[0]
            paths = (
                (resolved.physical_virtual_path,)
                if resolved.physical_virtual_path is not None
                else ()
            )
            results.append(
                TypedAssetReference(
                    item.identifier,
                    item.kind,
                    item.usage,
                    "resolved",
                    item.provenance,
                    item.conditions,
                    item.lifecycle_phases,
                    physical_virtual_paths=paths,
                    evidence=tuple(
                        f"{evidence.rule}:{evidence.matched_value}"
                        for evidence in resolved.evidence
                    ),
                )
            )
        else:
            unresolved = (batch.missing or batch.ambiguous)[0]
            candidates = tuple(
                candidate.physical_virtual_path
                for candidate in unresolved.candidates
            )
            results.append(
                TypedAssetReference(
                    item.identifier,
                    item.kind,
                    item.usage,
                    unresolved.reason,
                    item.provenance,
                    item.conditions,
                    item.lifecycle_phases,
                    reason=f"{unresolved.reason} W3D {item.kind} reference",
                    candidates=candidates,
                )
            )
    return tuple(results)


def resolve_typed_visual_graph(
    cst: ResolvedSageCst,
    object_names: Iterable[str],
    w3d_index: W3DIndex,
    visual_catalog_virtual_paths: Iterable[str] = (),
) -> TypedVisualGraph:
    """Resolve selected Object visual graphs from an expanded SAGE CST.

    Target ordering is canonical and independent of caller order.  Authored
    module/state ordering is retained because it is runtime-significant.
    """

    if not isinstance(cst, ResolvedSageCst):
        raise TypeError("cst must be a ResolvedSageCst")
    if not isinstance(w3d_index, W3DIndex):
        raise TypeError("w3d_index must be a W3DIndex")
    targets = _validated_targets(object_names)
    index = _object_index(cst.objects)
    diagnostics: list[TypedVisualDiagnostic] = []
    pending_references: list[_PendingReference] = []
    pending_objects: list[_PendingObject] = []

    for requested_name in targets:
        candidates = index.get(requested_name.casefold(), ())
        if not candidates:
            diagnostics.append(
                TypedVisualDiagnostic(
                    "missing-object",
                    f"missing target Object {requested_name!r}",
                    requested_name,
                )
            )
            continue
        if len(candidates) != 1:
            labels = tuple(
                f"{item.source_virtual_path}:{item.line}" for item in candidates
            )
            diagnostics.append(
                TypedVisualDiagnostic(
                    "ambiguous-object",
                    f"ambiguous target Object {requested_name!r}",
                    requested_name,
                    candidates[0].source_virtual_path,
                    candidates[0].line,
                    labels,
                )
            )
            continue
        target = candidates[0]
        ancestry, inheritance_complete = _ancestry(target, index, diagnostics)

        effective_assignments: dict[str, tuple[SageAssignment, SageObject, int]] = {}
        for ancestor_index, ancestor in enumerate(ancestry):
            distance = len(ancestry) - ancestor_index - 1
            for assignment in ancestor.assignments:
                if (
                    _is_visual_property(assignment.key)
                    or assignment.key.casefold() in _REFERENCE_KEYS
                ):
                    effective_assignments[assignment.key.casefold()] = (
                        assignment,
                        ancestor,
                        distance,
                    )
        object_properties: list[TypedVisualProperty] = []
        object_reference_indexes: list[int] = []
        for key in sorted(effective_assignments):
            assignment, defining_object, distance = effective_assignments[key]
            scope = ("Object",)
            _append_property(
                object_properties,
                assignment,
                defining_object,
                distance,
                scope,
                (),
                ("intact",),
            )
            object_reference_indexes.extend(
                _append_reference(
                    pending_references,
                    assignment,
                    defining_object,
                    distance,
                    scope,
                    (),
                    ("intact",),
                )
            )

        pending_modules: list[_PendingModule] = []
        for effective in _effective_draws(ancestry, diagnostics, target.name):
            block = effective.block
            scope = (_scope_name(block),)
            module_reference_indexes: list[int] = []
            module_properties: list[TypedVisualProperty] = []
            _walk_assignments(
                block.items,
                defining_object=effective.defining_object,
                inheritance_distance=effective.inheritance_distance,
                scope_path=scope,
                conditions=(),
                lifecycle_phases=("intact",),
                pending_references=pending_references,
                reference_indexes=module_reference_indexes,
                properties=module_properties,
                stop_at_states=True,
            )
            pending_modules.append(
                _PendingModule(
                    module_kind=block.kind,
                    instance_tag=block.instance_tag,
                    provenance=_provenance(
                        effective.defining_object,
                        block,
                        effective.inheritance_distance,
                        scope,
                    ),
                    inherited=effective.inheritance_distance > 0,
                    states=_collect_states(
                        block,
                        defining_object=effective.defining_object,
                        inheritance_distance=effective.inheritance_distance,
                        module_scope=scope,
                        pending_references=pending_references,
                    ),
                    reference_indexes=module_reference_indexes,
                    properties=module_properties,
                )
            )
        pending_objects.append(
            _PendingObject(
                source_object=target,
                ancestry=tuple(item.name for item in ancestry),
                inheritance_complete=inheritance_complete,
                modules=pending_modules,
                reference_indexes=object_reference_indexes,
                properties=object_properties,
            )
        )

    resolved = _resolve_pending(
        pending_references, w3d_index, visual_catalog_virtual_paths
    )
    objects: list[TypedVisualObject] = []
    for pending_object in pending_objects:
        modules: list[TypedDrawModule] = []
        coverage: set[str] = set()
        for module in pending_object.modules:
            states: list[TypedVisualState] = []
            for state in module.states:
                coverage.update(state.lifecycle_phases)
                states.append(
                    TypedVisualState(
                        state.family,
                        state.conditions,
                        state.lifecycle_phases,
                        state.provenance,
                        tuple(resolved[index] for index in state.reference_indexes),
                        tuple(state.properties),
                    )
                )
            modules.append(
                TypedDrawModule(
                    module.module_kind,
                    module.instance_tag,
                    module.provenance,
                    module.inherited,
                    tuple(states),
                    tuple(resolved[index] for index in module.reference_indexes),
                    tuple(module.properties),
                )
            )
        if not coverage:
            coverage.add("intact")
        target = pending_object.source_object
        objects.append(
            TypedVisualObject(
                target.name,
                target.kind,
                target.location,
                pending_object.ancestry,
                pending_object.inheritance_complete,
                tuple(sorted(coverage, key=lambda item: _LIFECYCLE_ORDER[item])),
                tuple(modules),
                tuple(resolved[index] for index in pending_object.reference_indexes),
                tuple(pending_object.properties),
            )
        )
    diagnostics.sort(
        key=lambda item: (
            _sort_text(item.object_name),
            item.code,
            item.virtual_path or "",
            item.line or 0,
            item.message,
        )
    )
    return TypedVisualGraph(
        cst.entry_virtual_path, tuple(objects), tuple(diagnostics)
    )


def resolve_typed_visual_documents(
    entry_virtual_path: str,
    documents: Mapping[str, bytes] | Iterable[tuple[str, bytes]],
    object_names: Iterable[str],
    w3d_index: W3DIndex,
    visual_catalog_virtual_paths: Iterable[str] = (),
) -> TypedVisualGraph:
    """Include-aware convenience boundary over :func:`resolve_sage_documents`."""

    return resolve_typed_visual_graph(
        resolve_sage_documents(entry_virtual_path, documents),
        object_names,
        w3d_index,
        visual_catalog_virtual_paths,
    )


def require_complete_typed_visual_graph(graph: TypedVisualGraph) -> TypedVisualGraph:
    """Fail closed if inheritance or any physical leaf remains unresolved."""

    if not isinstance(graph, TypedVisualGraph):
        raise TypeError("graph must be a TypedVisualGraph")
    if not graph.complete:
        raise TypedVisualGraphError(graph)
    return graph
