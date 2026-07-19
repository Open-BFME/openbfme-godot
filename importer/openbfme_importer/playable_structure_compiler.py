"""Compile BFME2 retail references into one playable-structure descriptor.

Structures share the playable-unit corpus preparation but classify by the
STRUCTURE KindOf family, produce through authored construct commands instead of
UNIT_BUILD sockets, and carry lifecycle health facts instead of unit core
animation states.  This module is object-name agnostic; engine-spawned fortress
composites are admitted only through the caller-owned implicit-root policy.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Iterable, Mapping, Sequence
import hashlib
import json

from .playable_unit_compiler import (
    PlayableUnitCompilerInputs,
    _ancestry,
    _audio_routes,
    _block_values,
    _command_slots,
    _effective_top_blocks,
    _effective_values,
    _first,
    _kind_of,
    _scalar_fields,
    _tokens,
    _walk_blocks,
    prepare_playable_unit_compiler,
)
from .sage_cst import SageObject


SCHEMA = "openbfme.playable-structure-descriptor"
SCHEMA_VERSION = 0
STRUCTURE_KIND_TOKENS = frozenset(
    {"STRUCTURE", "BASE_FOUNDATION", "FS_BASE_DEFENSE"}
)
_CONSTRUCT_COMMANDS = (
    {"dozer_construct"},
    {"porter_construct"},
    {"foundation_construct"},
)
_HEALTH_FIELDS = ("MaxHealth", "MaxHealthDamaged", "MaxHealthReallyDamaged")


class PlayableStructureCompilerError(ValueError):
    """The requested structure descriptor cannot be derived without guessing."""


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _numeric_value(
    token: str, defines: Mapping[str, int | float], label: str
) -> int | float:
    text = token.strip().rstrip("%")
    try:
        return float(text) if "." in text else int(text)
    except ValueError:
        resolved = defines.get(text.casefold())
        if resolved is None:
            raise PlayableStructureCompilerError(
                f"{label} references an unresolved GameData constant: {token}"
            )
        return resolved


def _construct_routes(
    target_id: str,
    objects: Mapping[str, SageObject],
    command_sets: Mapping[str, object],
    command_buttons: Mapping[str, object],
) -> list[dict[str, object]]:
    construct_commands: dict[str, dict[str, object]] = {}
    for button in command_buttons.values():
        commands = {value.casefold() for value in _block_values(button, "Command")}
        if commands not in _CONSTRUCT_COMMANDS:
            continue
        targets = tuple(
            filter(
                None, (_first((value,)) for value in _block_values(button, "Object"))
            )
        )
        if any(value.casefold() == target_id.casefold() for value in targets):
            construct_commands[button.name.casefold()] = {
                "id": button.name,
                "command": next(iter(commands)),
                "button": button,
            }
    if not construct_commands:
        return []

    set_bindings: list[tuple[object, int, dict[str, object]]] = []
    for command_set in command_sets.values():
        for slot, command_id in _command_slots(command_set):
            command = construct_commands.get(command_id.casefold())
            if command is not None:
                set_bindings.append((command_set, slot, command))

    routes: list[dict[str, object]] = []
    for builder in objects.values():
        try:
            lineage = _ancestry(objects, builder)
        except ValueError:
            continue
        direct_sets = {
            value.casefold()
            for value in (
                _first((row.value,))
                for row in _effective_values(lineage, "CommandSet")
            )
            if value
        }
        for command_set, slot, command in set_bindings:
            if command_set.name.casefold() not in direct_sets:
                continue
            button = command["button"]
            prerequisites = sorted(
                {
                    token
                    for field in ("NeededUpgrade", "Upgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if token.startswith(("Upgrade_", "SCIENCE_"))
                },
                key=str.casefold,
            )
            routes.append(
                {
                    "surface": "construct",
                    "commandId": str(command["id"]),
                    "commandKind": str(command["command"]),
                    "builderObjectId": builder.name,
                    "commandSetId": command_set.name,
                    "slot": slot,
                    "prerequisites": prerequisites,
                }
            )
    routes.sort(
        key=lambda row: (
            str(row["builderObjectId"]).casefold(),
            str(row["commandSetId"]).casefold(),
            int(row["slot"]),
            str(row["commandId"]).casefold(),
        )
    )
    return routes


def _health_contract(
    lineage: Sequence[SageObject],
    defines: Mapping[str, int | float],
    target_id: str,
) -> dict[str, object] | None:
    bodies: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        words = block.kind.casefold().split()
        if not words or "body" not in words[0]:
            continue
        fields: dict[str, object] = {}
        for field in _HEALTH_FIELDS:
            token = _first(block.values(field))
            if token is None:
                continue
            fields[field[0].lower() + field[1:]] = {
                "authored": token,
                "value": _numeric_value(
                    token, defines, f"{target_id} {block.kind} {field}"
                ),
            }
        if fields:
            bodies.append(
                {
                    "module": block.kind,
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                    **fields,
                }
            )
    if not bodies:
        return None
    primary = bodies[0]
    if "maxHealth" not in primary:
        raise PlayableStructureCompilerError(
            f"structure body does not author MaxHealth: {target_id}"
        )
    return {"primary": primary, "evidence": bodies}


def _trained_command_sets(
    lineage: Sequence[SageObject],
    command_sets: Mapping[str, object],
    target_id: str,
) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    direct = [
        value
        for value in (
            _first((row.value,)) for row in _effective_values(lineage, "CommandSet")
        )
        if value
    ]
    upgraded: list[tuple[str, list[str]]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        set_id = _first(block.values("CommandSet"))
        if not set_id or "commandsetupgrade" not in block.kind.casefold():
            continue
        triggers = sorted(
            {
                token
                for value in block.values("TriggeredBy")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            },
            key=str.casefold,
        )
        upgraded.append((set_id, triggers))
    for kind, entries in (
        ("direct", [(value, []) for value in direct]),
        ("upgraded", upgraded),
    ):
        for set_id, triggers in entries:
            key = (kind, set_id.casefold())
            if key in seen:
                continue
            seen.add(key)
            command_set = command_sets.get(set_id.casefold())
            if command_set is None:
                raise PlayableStructureCompilerError(
                    f"structure references a missing CommandSet: {set_id}"
                )
            row: dict[str, object] = {
                "id": command_set.name,
                "kind": kind,
                "slots": [
                    {"slot": slot, "commandId": command_id}
                    for slot, command_id in _command_slots(command_set)
                ],
            }
            if triggers:
                row["triggeredBy"] = triggers
            result.append(row)
    result.sort(key=lambda row: (str(row["kind"]), str(row["id"]).casefold()))
    return result


def _module_evidence(
    lineage: Sequence[SageObject],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        rows.append(
            {
                "module": block.kind,
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(
        key=lambda row: (
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
            str(row["module"]).casefold(),
        )
    )
    return rows


def compile_playable_structure_descriptor(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    prepared: PlayableUnitCompilerInputs | None = None,
    engine_spawned_roots: Iterable[str] = (),
    wall_template_roots: Iterable[str] = (),
) -> dict[str, object]:
    """Compile one source-backed structure descriptor or fail closed."""

    if not target_id or len(target_id) > 256:
        raise PlayableStructureCompilerError("target Object id is invalid")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise PlayableStructureCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    target = prepared.objects.get(target_id.casefold())
    if target is None:
        raise PlayableStructureCompilerError(
            f"effective Object is missing: {target_id}"
        )
    lineage = _ancestry(prepared.objects, target)
    kinds = _kind_of(lineage)
    if not STRUCTURE_KIND_TOKENS & set(kinds):
        raise PlayableStructureCompilerError(
            f"Object {target_id} has no structure KindOf capability"
        )

    production = _construct_routes(
        target_id, prepared.objects, prepared.command_sets, prepared.command_buttons
    )
    spawned_keys = {value.casefold() for value in engine_spawned_roots}
    wall_keys = {value.casefold() for value in wall_template_roots}
    if production:
        production_evidence = "authored-construct-command"
    elif target.name.casefold() in spawned_keys:
        production_evidence = "engine-spawned-composite"
    elif target.name.casefold() in wall_keys:
        production_evidence = "wall-template"
    else:
        raise PlayableStructureCompilerError(
            f"Object {target_id} is not targeted by an authored construct "
            "command and is not a declared engine-spawned or wall-template "
            "composite"
        )

    scalars = _scalar_fields(lineage)
    health = _health_contract(lineage, prepared.numeric_defines, target_id)
    if health is None and "BASE_FOUNDATION" not in kinds:
        raise PlayableStructureCompilerError(
            f"structure has no authored body health: {target_id}"
        )
    trained = _trained_command_sets(
        lineage, prepared.command_sets, target_id
    )
    audio = {
        key: value
        for key, value in sorted(_audio_routes(lineage).items())
    }
    sources = sorted(
        {
            row["sourceIni"]
            for row in _module_evidence(lineage)
            if isinstance(row.get("sourceIni"), str)
        }
        | {item.source_virtual_path for item in lineage},
        key=lambda value: (value.casefold(), value),
    )
    source_documents = []
    for path in sources:
        payload = documents.get(path)
        if payload is None:
            normalized = next(
                (
                    key
                    for key in documents
                    if key.replace("\\", "/").casefold()
                    == path.replace("\\", "/").casefold()
                ),
                None,
            )
            payload = documents.get(normalized) if normalized else None
        if payload is None:
            raise PlayableStructureCompilerError(
                f"structure source document is missing: {path}"
            )
        source_documents.append(
            {"virtualPath": path, "sha256": hashlib.sha256(payload).hexdigest()}
        )

    descriptor: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": target.name,
        "category": "structure",
        "kindOf": list(kinds),
        "production": {
            "evidence": production_evidence,
            "routes": production,
        },
        "gameplay": {
            "health": health,
            "trainedCommandSets": trained,
            "scalarFields": {
                key: value
                for key, value in sorted(scalars.items())
                if key
                in {
                    "BuildCost",
                    "BuildTime",
                    "VisionRange",
                    "ShroudClearingRange",
                    "CommandPoints",
                }
            },
        },
        "presentation": {
            "ui": {
                key: value
                for key, value in sorted(scalars.items())
                if key in {"DisplayName", "SelectPortrait", "ButtonImage"}
            },
            "audioRoutes": audio,
        },
        "runtimeModules": [],
        "runtimeModuleEvidence": _module_evidence(lineage),
        "sourceDocuments": source_documents,
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def validate_playable_structure_descriptor(value: Mapping[str, object]) -> None:
    """Reject any structure descriptor that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableStructureCompilerError(
            "structure descriptor identity is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructureCompilerError(
            "structure descriptor digest is invalid"
        )
    if value.get("category") != "structure":
        raise PlayableStructureCompilerError(
            "structure descriptor category is invalid"
        )
    kinds = value.get("kindOf")
    if not isinstance(kinds, list) or not STRUCTURE_KIND_TOKENS & {
        str(item) for item in kinds
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor KindOf evidence is invalid"
        )
    production = value.get("production")
    if not isinstance(production, Mapping):
        raise PlayableStructureCompilerError(
            "structure descriptor production is invalid"
        )
    routes = production.get("routes")
    evidence = production.get("evidence")
    if not isinstance(routes, list) or evidence not in {
        "authored-construct-command",
        "engine-spawned-composite",
        "wall-template",
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor production evidence is invalid"
        )
    if evidence == "authored-construct-command" and not routes:
        raise PlayableStructureCompilerError(
            "structure descriptor claims construct evidence without routes"
        )
    if evidence != "authored-construct-command" and routes:
        raise PlayableStructureCompilerError(
            "structure descriptor claims non-construct evidence with routes"
        )
    gameplay = value.get("gameplay")
    if not isinstance(gameplay, Mapping):
        raise PlayableStructureCompilerError(
            "structure descriptor gameplay is invalid"
        )
    if gameplay.get("health") is None and "BASE_FOUNDATION" not in {
        str(item) for item in kinds
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor omits health without foundation evidence"
        )


__all__ = [
    "PlayableStructureCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "STRUCTURE_KIND_TOKENS",
    "compile_playable_structure_descriptor",
    "validate_playable_structure_descriptor",
]
