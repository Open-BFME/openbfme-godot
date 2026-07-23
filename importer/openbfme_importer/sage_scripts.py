"""Deterministic script-list extraction from SAGE ``.map``/``.scb`` sources.

Emits the pack-facing ``openbfme.map-scripts`` document consumed by the
runtime script interpreter (``retail_map_scripts.gd``): every WorldBuilder
Script payload in source order, verbatim in the byte-exact ``sage_scb``
native shape, plus a fail-closed opcode census.  Wire parsing is delegated
entirely to the proven ``sage_map``/``sage_scb`` decoders; any structural
surprise raises instead of degrading, and the emitted JSON is byte-stable
(sorted keys, no timestamps, no absolute paths).
"""

from __future__ import annotations

from collections import Counter
import hashlib
from pathlib import Path
from typing import Any

from .sage_map import (
    MAX_TOP_LEVEL_RECORDS,
    SageMapError,
    _Cursor,
    _parse_name_table,
    _records,
    decode_sage_map_blob,
)
from .sage_scb import (
    SageScbError,
    _map_error_code,
    _parse_player_scripts,
    convert_sage_scb_bytes,
)
from .util import write_json_atomic


MAP_SCRIPTS_SCHEMA = "openbfme.map-scripts"
MAP_SCRIPTS_SCHEMA_VERSION = 0

# Containers this converter admits, keyed by source suffix (casefolded).
_CONTAINERS = {".map": "map", ".scb": "scb"}

# Retail .map files carry PlayerScriptsList v1/5/6 with identical ScriptList
# v1 children (the versions independently observed and admitted by
# sage_map).  Standalone .scb containers are v1 only, enforced by sage_scb.
_MAP_PLAYER_SCRIPTS_VERSIONS = frozenset({1, 5, 6})

_ACTION_RECORD_NAMES = frozenset({"ScriptAction", "ScriptActionFalse"})


def _map_player_scripts_chunks(source: bytes) -> list[dict[str, Any]]:
    """Decode a .map blob and parse exactly its PlayerScriptsList chunk."""

    try:
        body, _envelope = decode_sage_map_blob(source)
        cursor = _Cursor(body, label="map script extraction")
        if cursor.bytes(4) != b"CkMp":
            raise SageScbError("invalid-magic", "decoded map lacks CkMp magic")
        names = _parse_name_table(cursor)
        budget = {"nodes": 0}
        chunks: list[dict[str, Any]] = []
        for record in _records(
            cursor, names, cap=MAX_TOP_LEVEL_RECORDS, label="MapFile"
        ):
            if record.name == "PlayerScriptsList":
                if record.version not in _MAP_PLAYER_SCRIPTS_VERSIONS:
                    raise SageScbError(
                        "unsupported-top-level-version",
                        f"unsupported PlayerScriptsList version: {record.version}",
                    )
                chunks.append(
                    {
                        "name": record.name,
                        "version": record.version,
                        "value": _parse_player_scripts(
                            record, names, budget=budget
                        ),
                    }
                )
            else:
                record.payload.skip(record.payload.remaining)
        cursor.finish()
    except SageScbError:
        raise
    except SageMapError as exc:
        raise SageScbError(
            _map_error_code(exc), "map wire parsing failed"
        ) from exc
    if len(chunks) != 1:
        raise SageScbError(
            "missing-player-scripts",
            f"map contains {len(chunks)} PlayerScriptsList chunk(s); expected 1",
        )
    return chunks


def _collect_scripts(chunks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Flatten every Script node with its group path, payloads verbatim."""

    scripts: list[dict[str, Any]] = []

    def visit(node: Any, path: list[str]) -> None:
        if isinstance(node, dict):
            name = node.get("name")
            value = node.get("value")
            if name == "Script" and isinstance(value, dict):
                scripts.append({"groupPath": "/".join(path), "script": value})
                return
            if name == "ScriptGroup" and isinstance(value, dict):
                for child in value.get("records", []):
                    visit(child, path + [str(value.get("name", "?"))])
                return
            for child in node.values():
                visit(child, path)
        elif isinstance(node, (list, tuple)):
            for child in node:
                visit(child, path)

    visit(chunks, [])
    return scripts


def _script_census(script: dict[str, Any]) -> tuple[Counter, Counter, int, int]:
    """Count action/condition opcode occurrences and slots in one script."""

    actions: Counter = Counter()
    conditions: Counter = Counter()
    action_slots = 0
    condition_slots = 0

    def visit(node: Any) -> None:
        nonlocal action_slots, condition_slots
        if isinstance(node, dict):
            name = node.get("name")
            value = node.get("value")
            if name in _ACTION_RECORD_NAMES and isinstance(value, dict):
                internal = value.get("internalName")
                opcode = (
                    internal.get("name") if isinstance(internal, dict) else None
                )
                if not isinstance(opcode, str) or not opcode:
                    raise SageScbError(
                        "missing-opcode-name",
                        f"{name} record lacks an internal opcode name",
                    )
                actions[opcode] += 1
                action_slots += 1
            elif name == "Condition" and isinstance(value, dict):
                internal = value.get("internalName")
                opcode = (
                    internal.get("name") if isinstance(internal, dict) else None
                )
                if not isinstance(opcode, str) or not opcode:
                    raise SageScbError(
                        "missing-opcode-name",
                        "Condition record lacks an internal opcode name",
                    )
                conditions[opcode] += 1
                condition_slots += 1
            for child in node.values():
                visit(child)
        elif isinstance(node, (list, tuple)):
            for child in node:
                visit(child)

    visit(script)
    return actions, conditions, action_slots, condition_slots


def map_scripts_document(source: bytes, *, container: str) -> dict[str, Any]:
    """Build the deterministic ``openbfme.map-scripts`` document."""

    if container == "scb":
        chunks = convert_sage_scb_bytes(source)["chunks"]
    elif container == "map":
        chunks = _map_player_scripts_chunks(source)
    else:
        raise ValueError(f"unsupported sage-scripts container: {container!r}")

    rows: list[dict[str, Any]] = []
    total_actions: Counter = Counter()
    total_conditions: Counter = Counter()
    total_action_slots = 0
    total_condition_slots = 0
    for item in _collect_scripts(chunks):
        script = item["script"]
        actions, conditions, action_slots, condition_slots = _script_census(
            script
        )
        total_actions.update(actions)
        total_conditions.update(conditions)
        total_action_slots += action_slots
        total_condition_slots += condition_slots
        rows.append(
            {
                "name": str(script.get("name", "")),
                "groupPath": item["groupPath"],
                "isActive": bool(script.get("isActive", False)),
                "isSubroutine": bool(script.get("isSubroutine", False)),
                "deactivateUponSuccess": bool(
                    script.get("deactivateUponSuccess", False)
                ),
                "actionSlots": action_slots,
                "conditionSlots": condition_slots,
                "actionOpcodes": dict(sorted(actions.items())),
                "conditionOpcodes": dict(sorted(conditions.items())),
                "payload": script,
            }
        )

    return {
        "schema": MAP_SCRIPTS_SCHEMA,
        "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
        "source": {
            "container": container,
            "sourceBytes": len(source),
            "sourceSha256": hashlib.sha256(source).hexdigest(),
        },
        "counts": {
            "scripts": len(rows),
            "actionSlots": total_action_slots,
            "conditionSlots": total_condition_slots,
            "distinctActionOpcodes": len(total_actions),
            "distinctConditionOpcodes": len(total_conditions),
        },
        "actionOpcodes": dict(sorted(total_actions.items())),
        "conditionOpcodes": dict(sorted(total_conditions.items())),
        "scripts": rows,
    }


def convert_map_scripts(source: Path, target: Path) -> list[Path]:
    """Convert one .map/.scb source into ``<name>.scripts.json``."""

    container = _CONTAINERS.get(source.suffix.casefold())
    if container is None:
        raise ValueError(
            f"sage-scripts source must be a .map or .scb file: {source.name}"
        )
    document = map_scripts_document(source.read_bytes(), container=container)
    write_json_atomic(target, document)
    return [target]
