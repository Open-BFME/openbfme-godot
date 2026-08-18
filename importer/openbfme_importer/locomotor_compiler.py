"""Canonical, provenance-preserving compiler for retail locomotion data.

``Locomotor`` owns response/shape semantics.  ``LocomotorSet`` owns the
per-object condition binding and speed.  Keeping those two sources separate is
important: retail commonly binds one template at several object-specific
speeds.

This reader deliberately retains fields it does not understand in
``unsupported``.  Adding runtime meaning is a separate, reviewable change;
silently dropping a retail assignment is never allowed.
"""

from __future__ import annotations

from collections.abc import Mapping
import re
from typing import Any


LOCOMOTOR_PATH = "data/ini/locomotor.ini"
SCHEMA = "openbfme.locomotor-table"
SCHEMA_VERSION = 1


class LocomotorCompilerError(ValueError):
    """Retail locomotion input cannot be represented without guessing."""


# Fields with Phase-A data meaning.  Everything else remains source-addressed
# in ``unsupported`` until a later phase assigns semantics.
_FIELD_SPECS: dict[str, tuple[str, str]] = {
    "surfaces": ("surfaces", "tokens"),
    "zaxisbehavior": ("zAxisBehavior", "token"),
    "minturnspeed": ("minTurnSpeed", "percent"),
    "appearance": ("appearance", "token"),
    "braking": ("braking", "number"),
    "acceleration": ("acceleration", "number"),
    "turntime": ("turnTime", "number"),
    "turntimedamaged": ("turnTimeDamaged", "number"),
    "sticktoground": ("stickToGround", "yes-no"),
    "canmovebackwards": ("canMoveBackwards", "yes-no"),
    "backingupspeed": ("backingUpSpeed", "percent"),
    "fastturnradius": ("fastTurnRadius", "number"),
    "slowturnradius": ("slowTurnRadius", "number"),
    "turnpivotoffset": ("turnPivotOffset", "number"),
    "formationpriority": ("formationPriority", "token"),
    "hassuspension": ("hasSuspension", "yes-no"),
    "closeenoughdist": ("closeEnoughDist", "number"),
    "preferredheight": ("preferredHeight", "number"),
    "preferredattackheight": ("preferredAttackHeight", "number"),
    "lift": ("lift", "percent"),
    "maxturnwithoutreform": ("maxTurnWithoutReform", "number"),
    "waitforformation": ("waitForFormation", "yes-no"),
    "scaleswalls": ("scalesWalls", "yes-no"),
    "chargespeed": ("chargeSpeed", "percent"),
    "slideintoplacetime": ("slideIntoPlaceTime", "number"),
    "maxthrustangle": ("maxThrustAngle", "number"),
}

_HEADER_RE = re.compile(r"^\s*Locomotor\s+(\S+)\s*$", re.IGNORECASE)
_ASSIGNMENT_RE = re.compile(r"^\s*([^=]+?)\s*=\s*(.*?)\s*$")
_OBJECT_HEADER_RE = re.compile(
    r"^\s*(Object|ChildObject|ObjectReskin)\s+(\S+)(?:\s+\S+)?\s*$",
    re.IGNORECASE,
)


def _document(documents: Mapping[str, bytes], wanted: str) -> tuple[str, bytes]:
    folded = wanted.casefold()
    matches = [
        (path.replace("\\", "/"), payload)
        for path, payload in documents.items()
        if path.replace("\\", "/").casefold() == folded
    ]
    if len(matches) != 1:
        raise LocomotorCompilerError(
            f"expected one {wanted} document, found {len(matches)}"
        )
    return matches[0]


def _without_comment(raw: str) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if character == '"':
            quoted = not quoted
        elif not quoted and character == ";":
            return raw[:index].strip()
        elif not quoted and raw[index : index + 2] == "//":
            return raw[:index].strip()
        index += 1
    return raw.strip()


def _json_number(value: float) -> int | float:
    return int(value) if value.is_integer() else value


def _numeric(token: str, constants: Mapping[str, int | float], label: str) -> int | float:
    clean = token.strip()
    defined = constants.get(clean.casefold())
    if defined is not None:
        return defined
    try:
        return _json_number(float(clean))
    except ValueError as exc:
        raise LocomotorCompilerError(f"{label} has unresolved value {token!r}") from exc


def _field_value(
    token: str,
    kind: str,
    constants: Mapping[str, int | float],
    label: str,
) -> object:
    if kind == "number":
        return _numeric(token, constants, label)
    if kind == "percent":
        if not token.strip().endswith("%"):
            raise LocomotorCompilerError(f"{label} must be an authored percentage")
        return _json_number(_numeric(token.strip()[:-1], constants, label) / 100.0)
    if kind == "yes-no":
        folded = token.strip().casefold()
        if folded in {"yes", "1", "true"}:
            return True
        if folded in {"no", "0", "false"}:
            return False
        raise LocomotorCompilerError(f"{label} must be Yes/No or 0/1, got {token!r}")
    if kind == "tokens":
        values = token.split()
        if not values:
            raise LocomotorCompilerError(f"{label} has no tokens")
        return values
    if kind == "token":
        values = token.split()
        if len(values) != 1:
            raise LocomotorCompilerError(f"{label} must have one token")
        return values[0]
    raise AssertionError(kind)


def compile_locomotor_templates(
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float] | None = None,
) -> dict[str, object]:
    """Compile every retail ``Locomotor`` with field-level provenance."""

    constants = {str(key).casefold(): value for key, value in (constants or {}).items()}
    path, payload = _document(documents, LOCOMOTOR_PATH)
    try:
        lines = payload.decode("cp1252").splitlines()
    except UnicodeDecodeError as exc:
        raise LocomotorCompilerError(f"{path} is not cp1252") from exc

    templates: dict[str, dict[str, object]] = {}
    current: dict[str, object] | None = None
    current_name = ""
    for line_number, raw in enumerate(lines, start=1):
        line = _without_comment(raw)
        if current is None:
            match = _HEADER_RE.fullmatch(line)
            if match is None:
                continue
            current_name = match.group(1)
            folded = current_name.casefold()
            if folded in templates:
                raise LocomotorCompilerError(
                    f"{path}:{line_number}: duplicate Locomotor {current_name}"
                )
            current = {
                "id": f"locomotors/{current_name}",
                "name": current_name,
                "sourceIni": path,
                "line": line_number,
                "fields": {},
                "unsupported": [],
            }
            templates[folded] = current
            continue
        if not line:
            continue
        if line.casefold() == "end":
            current = None
            current_name = ""
            continue
        match = _ASSIGNMENT_RE.fullmatch(line)
        if match is None:
            unsupported = current["unsupported"]
            assert isinstance(unsupported, list)
            unsupported.append({
                "key": "<statement>", "raw": line, "sourceIni": path, "line": line_number
            })
            continue
        source_key, raw_value = match.group(1).strip(), match.group(2).strip()
        spec = _FIELD_SPECS.get(source_key.casefold())
        source = {
            "raw": raw_value,
            "sourceField": source_key,
            "sourceIni": path,
            "line": line_number,
        }
        if spec is None:
            unsupported = current["unsupported"]
            assert isinstance(unsupported, list)
            unsupported.append({"key": source_key, **source})
            continue
        output_key, kind = spec
        fields = current["fields"]
        assert isinstance(fields, dict)
        # Retail authors some fields multiple times; later value wins
        fields[output_key] = {
            "value": _field_value(
                raw_value, kind, constants, f"{path}:{line_number} {current_name}.{source_key}"
            ),
            **source,
        }
    if current is not None:
        raise LocomotorCompilerError(f"{path}: unterminated Locomotor {current_name}")
    if not templates:
        raise LocomotorCompilerError(f"{path}: no Locomotor templates")
    return {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sourceIni": path,
        "templates": {row["name"]: row for row in templates.values()},
    }


def _binding(
    assignments: list[dict[str, object]], path: str, line: int,
    constants: Mapping[str, int | float]
) -> dict[str, object]:
    fields: dict[str, dict[str, object]] = {}
    unsupported: list[dict[str, object]] = []
    for assignment in assignments:
        folded = str(assignment["key"]).casefold()
        if folded in fields:
            raise LocomotorCompilerError(
                f"{path}:{assignment['line']}: repeated LocomotorSet {assignment['key']}"
            )
        if folded in {"condition", "locomotor", "speed"}:
            fields[folded] = assignment
        else:
            unsupported.append(dict(assignment))
    missing = {"condition", "locomotor", "speed"} - set(fields)
    if missing:
        raise LocomotorCompilerError(
            f"{path}:{line}: LocomotorSet missing "
            + ", ".join(sorted(missing))
        )
    condition = fields["condition"]
    locomotor = fields["locomotor"]
    speed = fields["speed"]
    condition_tokens = str(condition["raw"]).split()
    locomotor_tokens = str(locomotor["raw"]).split()
    if len(condition_tokens) != 1 or len(locomotor_tokens) != 1:
        raise LocomotorCompilerError(
            f"{path}:{line}: ambiguous LocomotorSet binding"
        )
    return {
        "condition": condition_tokens[0],
        "locomotorId": f"locomotors/{locomotor_tokens[0]}",
        "locomotor": locomotor_tokens[0],
        "speed": _numeric(
            str(speed["raw"]),
            constants,
            f"{path}:{speed['line']} LocomotorSet.Speed",
        ),
        "sourceIni": path,
        "line": line,
        "speedLine": int(speed["line"]),
        "unsupported": unsupported,
    }


def compile_object_locomotor_sets(
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float] | None = None,
) -> dict[str, dict[str, dict[str, object]]]:
    """Compile direct per-object condition bindings from every object document."""

    constants = {str(key).casefold(): value for key, value in (constants or {}).items()}
    result: dict[str, dict[str, dict[str, object]]] = {}
    identities: dict[str, str] = {}
    for raw_path, payload in sorted(documents.items(), key=lambda item: item[0].casefold()):
        path = raw_path.replace("\\", "/")
        if "/object/" not in f"/{path.casefold()}":
            continue
        try:
            lines = payload.decode("cp1252").splitlines()
        except UnicodeDecodeError as exc:
            raise LocomotorCompilerError(f"{path}: object source is not cp1252") from exc
        current_object = ""
        active_line = 0
        assignments: list[dict[str, object]] | None = None
        rows: list[tuple[str, int, list[dict[str, object]]]] = []
        for line_number, raw in enumerate(lines, start=1):
            line = _without_comment(raw)
            object_header = _OBJECT_HEADER_RE.fullmatch(line)
            if object_header is not None and assignments is None:
                current_object = object_header.group(2)
                continue
            if assignments is None:
                if current_object and line.casefold() == "locomotorset":
                    active_line = line_number
                    assignments = []
                continue
            if line.casefold() == "end":
                rows.append((current_object, active_line, assignments))
                assignments = None
                active_line = 0
                continue
            assignment = _ASSIGNMENT_RE.fullmatch(line)
            if assignment is not None:
                assignments.append({
                    "key": assignment.group(1).strip(),
                    "raw": assignment.group(2).strip(),
                    "sourceIni": path,
                    "line": line_number,
                })
        if assignments is not None:
            raise LocomotorCompilerError(f"{path}:{active_line}: unterminated LocomotorSet")
        for object_name, block_line, block_assignments in rows:
            folded_object = object_name.casefold()
            prior = identities.get(folded_object)
            if prior is not None and prior != object_name:
                raise LocomotorCompilerError(
                    f"case-conflicting Object locomotor bindings: {prior}, {object_name}"
                )
            identities[folded_object] = object_name
            object_rows = result.setdefault(object_name, {})
            row = _binding(block_assignments, path, block_line, constants)
            condition = str(row["condition"])
            # Retail authors some conditions multiple times for the same object; later wins
            object_rows[condition] = row
    return result


def referenced_locomotor_ids(
    bindings: Mapping[str, Mapping[str, Mapping[str, object]]]
) -> frozenset[str]:
    return frozenset(
        str(row["locomotorId"])
        for object_rows in bindings.values()
        for row in object_rows.values()
    )


__all__ = [
    "LOCOMOTOR_PATH",
    "LocomotorCompilerError",
    "compile_locomotor_templates",
    "compile_object_locomotor_sets",
    "referenced_locomotor_ids",
]
