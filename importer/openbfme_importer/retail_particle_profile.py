"""Plan the exact Fords particle-definition and render-asset closure.

The planner consumes the sealed unresolved-object census, the authoritative
effective-assets manifest, and the matching private effective tree.  It emits
only exact particle definition conversions, one conversion per physical render
texture, the source-authored ripple anchor, and a normalized binding document.

Seven Fords systems exist in both SAGE particle-definition families.  A sealed
retail-binary oracle proves one unqualified runtime manager namespace and
last-definition-wins for repeated FX syntax, but not cross-family precedence.
Both candidates remain preserved; only WaterRipplesSmall receives the oracle's
explicit, non-general provisional FX runtime selection.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Mapping

from .paths import safe_relative_parts
from .profile import ImportProfile
from .retail_visual_profile import (
    _canonical_sha256,
    _case_unique,
    _is_int,
    _list,
    _mapping,
    _safe_object_id,
    _safe_virtual_path,
    _sha256,
    _source_record,
    _stable_slug,
    _text,
    _validate_declared_digest,
    _validate_effective_manifest,
)
from .sage_particles import (
    ParticleDefinition,
    parse_particle_definitions,
    select_particle_definition,
)
from .util import write_json_atomic


FORDS_PARTICLE_PLAN_SCHEMA = "openbfme.retail-fords-particle-plan"
FORDS_PARTICLE_PLAN_SCHEMA_VERSION = 0
FORDS_PARTICLE_BINDINGS_SCHEMA = "openbfme.fords-particle-bindings"
FORDS_PARTICLE_BINDINGS_SCHEMA_VERSION = 0
CENSUS_SCHEMA = "openbfme.fords-unresolved-object-census"
CENSUS_SCHEMA_VERSION = 1
FAMILY_ORACLE_SCHEMA_VERSION = 1
RUNTIME_DATA_PATH = "effects/fords-particle-bindings.json"

_PARTICLE_SOURCE_PATH = "data/ini/particlesystem.ini"
_FX_PARTICLE_SOURCE_PATH = "data/ini/fxparticlesystem.ini"
_FX_LIST_SOURCE_PATH = "data/ini/fxlist.ini"
_SUBSYSTEM_LEGEND_PATH = "data/ini/default/subsystemlegend.ini"
_RIPPLE_OBJECT_SOURCE_PATH = "data/ini/object/civilian/civilianprop.ini"
_RIPPLE_ANCHOR_PATH = "art/w3d/p_/p_wtrriplssmall.w3d"
_RIPPLE_ANCHOR_BONE = "waterRippleBone"
_RIPPLE_PROVISIONAL_FAMILY = "FXParticleSystem"
_MAX_INPUT_BYTES = 64 * 1024 * 1024
_MAX_SOURCE_BYTES = 32 * 1024 * 1024

_TARGET_PLACEMENTS = {
    "CaveTrollLair": 2,
    "Inn": 2,
    "WargLair": 4,
    "WtrRiplsSmall": 7,
}

_TARGET_SYSTEMS = {
    "CaveTrollLair": (
        "BuildingDamaged",
        "PCTMediumDust",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
        "SmokeBuildingMediumRubble",
        "UntamedAllegiance",
        "UntamedAllegiance2",
    ),
    "Inn": (
        "BuildingContructDust",
        "BuildingDamaged",
        "PCTMediumDust",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
        "SmokeBuildingLarge",
    ),
    "WargLair": (
        "BuildingDamaged",
        "PCTMediumDust",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
        "SmokeBuildingMediumRubble",
        "UntamedAllegiance",
        "UntamedAllegiance2",
    ),
    "WtrRiplsSmall": ("WaterRipplesSmall",),
}

_SYSTEM_FAMILIES = {
    "BuildingContructDust": ("ParticleSystem", "FXParticleSystem"),
    "BuildingDamaged": ("FXParticleSystem",),
    "PCTMediumDust": ("ParticleSystem", "FXParticleSystem"),
    "RDTMediumExplosion": ("ParticleSystem", "FXParticleSystem"),
    "RDTMediumExplosionLight": ("ParticleSystem", "FXParticleSystem"),
    "SmokeBuildingLarge": ("ParticleSystem", "FXParticleSystem"),
    "SmokeBuildingMediumRubble": ("ParticleSystem", "FXParticleSystem"),
    "UntamedAllegiance": ("FXParticleSystem",),
    "UntamedAllegiance2": ("FXParticleSystem",),
    "WaterRipplesSmall": ("ParticleSystem", "FXParticleSystem"),
}

_FX_LIST_SYSTEMS = {
    "FX_BuildingDamaged": ("BuildingDamaged",),
    "FX_BuildingReallyDamaged": (
        "BuildingDamaged",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
    ),
    "FX_StructureAlmostCollapse": (
        "PCTMediumDust",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
    ),
    "FX_StructureMediumCollapse": (
        "PCTMediumDust",
        "RDTMediumExplosion",
        "RDTMediumExplosionLight",
    ),
}

_ATTACHMENT_SIGNATURES = {
    "CaveTrollLair": (
        (
            "NONE",
            "SmokeBuildingMediumRubble",
            (),
            "ModelConditionState",
            ("POST_RUBBLE",),
        ),
        (
            "NONE",
            "SmokeBuildingMediumRubble",
            (),
            "ModelConditionState",
            ("POST_COLLAPSE",),
        ),
        (
            "None",
            "UntamedAllegiance",
            ("HouseColor:Yes",),
            "AnimationState",
            ("USER_2",),
        ),
        (
            "None",
            "UntamedAllegiance2",
            ("HouseColor:Yes",),
            "AnimationState",
            ("USER_2",),
        ),
    ),
    "Inn": (
        (
            "DUSTBONE",
            "BuildingContructDust",
            (),
            "ModelConditionState",
            ("ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"),
        ),
        ("SmokeLarge01", "SmokeBuildingLarge", (), "ModelConditionState", ("RUBBLE",)),
    ),
    "WargLair": (
        (
            "NONE",
            "SmokeBuildingMediumRubble",
            (),
            "ModelConditionState",
            ("POST_RUBBLE",),
        ),
        (
            "NONE",
            "SmokeBuildingMediumRubble",
            (),
            "ModelConditionState",
            ("POST_COLLAPSE",),
        ),
        (
            "None",
            "UntamedAllegiance",
            ("HouseColor:Yes",),
            "AnimationState",
            ("USER_2",),
        ),
        (
            "None",
            "UntamedAllegiance2",
            ("HouseColor:Yes",),
            "AnimationState",
            ("USER_2",),
        ),
    ),
    "WtrRiplsSmall": (
        (_RIPPLE_ANCHOR_BONE, "WaterRipplesSmall", (), "IdleAnimationState", ()),
    ),
}

_FX_ROOT_SIGNATURES = {
    "CaveTrollLair": (
        ("EnteringStateFX", "FX_BuildingDamaged", "AnimationState", ("DAMAGED",), None),
        (
            "EnteringStateFX",
            "FX_BuildingReallyDamaged",
            "AnimationState",
            ("REALLYDAMAGED",),
            None,
        ),
        (
            "EnteringStateFX",
            "FX_StructureMediumCollapse",
            "AnimationState",
            ("COLLAPSING",),
            None,
        ),
        (
            "FXList",
            "FX_StructureMediumCollapse",
            "StructureCollapseUpdate",
            (),
            "INITIAL",
        ),
        (
            "FXList",
            "FX_StructureAlmostCollapse",
            "StructureCollapseUpdate",
            (),
            "ALMOST_FINAL",
        ),
    ),
    "Inn": (
        ("EnteringStateFX", "FX_BuildingDamaged", "AnimationState", ("DAMAGED",), None),
        (
            "EnteringStateFX",
            "FX_BuildingReallyDamaged",
            "AnimationState",
            ("REALLYDAMAGED",),
            None,
        ),
        (
            "EnteringStateFX",
            "FX_StructureMediumCollapse",
            "AnimationState",
            ("COLLAPSING",),
            None,
        ),
    ),
    "WargLair": (
        ("EnteringStateFX", "FX_BuildingDamaged", "AnimationState", ("DAMAGED",), None),
        (
            "EnteringStateFX",
            "FX_BuildingReallyDamaged",
            "AnimationState",
            ("REALLYDAMAGED",),
            None,
        ),
        (
            "EnteringStateFX",
            "FX_StructureMediumCollapse",
            "AnimationState",
            ("COLLAPSING",),
            None,
        ),
        (
            "FXList",
            "FX_StructureMediumCollapse",
            "StructureCollapseUpdate",
            (),
            "INITIAL",
        ),
        (
            "FXList",
            "FX_StructureAlmostCollapse",
            "StructureCollapseUpdate",
            (),
            "ALMOST_FINAL",
        ),
    ),
    "WtrRiplsSmall": (),
}


def _positive_int(value: object, label: str) -> int:
    if not _is_int(value) or int(value) <= 0:
        raise ValueError(f"{label} must be a positive integer")
    return int(value)


def _nonnegative_int(value: object, label: str) -> int:
    if not _is_int(value) or int(value) < 0:
        raise ValueError(f"{label} must be a nonnegative integer")
    return int(value)


def _string_array(value: object, label: str) -> list[str]:
    rows = _list(value, label)
    result = [_text(item, f"{label} entry") for item in rows]
    return result


def _source_file_record(
    raw: object,
    sources: Mapping[str, Mapping[str, Any]],
    label: str,
    *,
    expected_role: str | None = None,
) -> dict[str, Any]:
    item = _mapping(raw, label)
    expected_fields = {
        "archive",
        "byteLength",
        "precedence",
        "role",
        "sha256",
        "virtualPath",
    }
    if set(item) != expected_fields:
        raise ValueError(f"{label} has unsupported fields")
    path = _safe_virtual_path(item.get("virtualPath"), f"{label}.virtualPath")
    archive = _safe_virtual_path(item.get("archive"), f"{label}.archive")
    digest = _sha256(item.get("sha256"), f"{label}.sha256")
    byte_length = _nonnegative_int(item.get("byteLength"), f"{label}.byteLength")
    precedence = _nonnegative_int(item.get("precedence"), f"{label}.precedence")
    role = _text(item.get("role"), f"{label}.role")
    if expected_role is not None and role != expected_role:
        raise ValueError(f"{label} has unexpected role {role!r}")
    result = _source_record(
        path,
        sources,
        expected_sha256=digest,
        expected_size=byte_length,
    )
    if (
        result["source"]["archive"] != archive
        or result["source"]["precedence"] != precedence
    ):
        raise ValueError(f"{label} archive precedence does not match manifest")
    result["role"] = role
    return result


class _PrivateSourceReader:
    def __init__(
        self,
        root: Path | str,
        sources: Mapping[str, Mapping[str, Any]],
    ) -> None:
        self.root = Path(root).expanduser().resolve()
        if not self.root.is_dir():
            raise ValueError("effective-assets root is not a directory")
        self.sources = sources
        self._payloads: dict[str, bytes] = {}

    def read(self, path: str) -> bytes:
        canonical = _safe_virtual_path(path, "private source virtual path")
        cached = self._payloads.get(canonical)
        if cached is not None:
            return cached
        manifest = self.sources.get(canonical)
        if manifest is None:
            raise ValueError(f"private source is absent from manifest: {canonical}")
        expected_size = int(manifest["size"])
        if expected_size > _MAX_SOURCE_BYTES:
            raise ValueError(f"private source exceeds byte limit: {canonical}")
        candidate = self.root.joinpath(*safe_relative_parts(canonical)).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError as exc:
            raise ValueError("private source escaped effective-assets root") from exc
        if not candidate.is_file() or candidate.stat().st_size != expected_size:
            raise ValueError(f"private source size mismatch: {canonical}")
        payload = candidate.read_bytes()
        if hashlib.sha256(payload).hexdigest() != manifest["sha256"]:
            raise ValueError(f"private source SHA-256 mismatch: {canonical}")
        self._payloads[canonical] = payload
        return payload

    def evidence(self) -> dict[str, Any]:
        rows = [
            {
                "virtualPath": path,
                "byteLength": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            for path, payload in sorted(
                self._payloads.items(), key=lambda item: (item[0].casefold(), item[0])
            )
        ]
        return {
            "policy": "manifest-bound-files-below-private-effective-assets-root",
            "uniqueReadCount": len(rows),
            "byteLength": sum(int(row["byteLength"]) for row in rows),
            "sources": rows,
            "aggregateSha256": _canonical_sha256(rows),
        }


def _raw_span(payload: bytes, start_line: int, end_line: int) -> bytes:
    lines = payload.splitlines(keepends=True)
    if start_line <= 0 or end_line < start_line or end_line > len(lines):
        raise ValueError("source span line range is invalid")
    return b"".join(lines[start_line - 1 : end_line])


def _fx_list_body_counts(block: bytes) -> tuple[int, int]:
    try:
        text = block.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("FX list block is not CP1252") from exc
    assignments = 0
    nested_blocks = 0
    for position, raw in enumerate(text.splitlines()):
        content = raw.split(";", 1)[0].strip()
        if not content or position == 0 or content.casefold() == "end":
            continue
        if "=" in content:
            assignments += 1
        else:
            nested_blocks += 1
    return assignments, nested_blocks


def _scope_rows(value: object, source_path: str, label: str) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for position, raw in enumerate(_list(value, label)):
        item = _mapping(raw, f"{label} {position}")
        allowed = {"kind", "line", "sourceVirtualPath", "headerTokens", "instanceTag"}
        if not set(item) <= allowed or not {"kind", "line", "sourceVirtualPath"} <= set(
            item
        ):
            raise ValueError(f"{label} {position} has unsupported fields")
        path = _safe_virtual_path(
            item.get("sourceVirtualPath"), f"{label} {position} sourceVirtualPath"
        )
        if path != source_path:
            raise ValueError(f"{label} {position} source path mismatch")
        row: dict[str, Any] = {
            "kind": _text(item.get("kind"), f"{label} {position} kind"),
            "line": _positive_int(item.get("line"), f"{label} {position} line"),
            "sourceVirtualPath": path,
        }
        if "headerTokens" in item:
            row["headerTokens"] = _string_array(
                item.get("headerTokens"), f"{label} {position} headerTokens"
            )
        if "instanceTag" in item:
            row["instanceTag"] = _text(
                item.get("instanceTag"), f"{label} {position} instanceTag"
            )
        result.append(row)
    if not result:
        raise ValueError(f"{label} must not be empty")
    return result


def _trigger(
    scope: list[dict[str, Any]], value: str, *, fx_list: bool = False
) -> dict[str, Any]:
    state = scope[-1]
    tokens = (
        list(state.get("headerTokens", []))
        if state["kind"]
        in {"AnimationState", "ModelConditionState", "IdleAnimationState"}
        else []
    )
    return {
        "stateFamily": state["kind"],
        "conditionTokens": tokens,
        "stage": value.split()[0] if fx_list else None,
        "scope": scope,
    }


def _normalize_attachment(
    raw: object,
    target: str,
    source_path: str,
) -> tuple[dict[str, Any], tuple[Any, ...]]:
    item = _mapping(raw, f"{target} particle attachment")
    expected = {
        "bone",
        "definingObject",
        "field",
        "inheritanceDistance",
        "line",
        "options",
        "particleSystemId",
        "scope",
        "sourceVirtualPath",
        "value",
    }
    if set(item) != expected:
        raise ValueError(f"{target} particle attachment has unsupported fields")
    if item.get("definingObject") != target or item.get("field") != "ParticleSysBone":
        raise ValueError(f"{target} particle attachment provenance mismatch")
    if item.get("inheritanceDistance") != 0:
        raise ValueError(f"{target} particle attachment must be directly authored")
    if item.get("sourceVirtualPath") != source_path:
        raise ValueError(f"{target} particle attachment source mismatch")
    bone = _text(item.get("bone"), f"{target} particle bone")
    system = _safe_object_id(item.get("particleSystemId"), f"{target} particle system")
    options = _string_array(item.get("options"), f"{target} attachment options")
    scope = _scope_rows(item.get("scope"), source_path, f"{target} attachment scope")
    value = _text(item.get("value"), f"{target} particle attachment value")
    trigger = _trigger(scope, value)
    row = {
        "field": "ParticleSysBone",
        "anchorBone": bone,
        "particleSystemId": system,
        "options": options,
        "trigger": trigger,
        "source": {
            "virtualPath": source_path,
            "line": _positive_int(item.get("line"), f"{target} attachment line"),
            "value": value,
        },
    }
    signature = (
        bone,
        system,
        tuple(options),
        trigger["stateFamily"],
        tuple(trigger["conditionTokens"]),
    )
    return row, signature


def _normalize_fx_root(
    raw: object,
    target: str,
    source_path: str,
) -> tuple[dict[str, Any], tuple[Any, ...]]:
    item = _mapping(raw, f"{target} FX root")
    expected = {
        "definingObject",
        "field",
        "fxListId",
        "inheritanceDistance",
        "line",
        "scope",
        "sourceVirtualPath",
        "value",
    }
    if set(item) != expected:
        raise ValueError(f"{target} FX root has unsupported fields")
    field = _text(item.get("field"), f"{target} FX root field")
    if field not in {"EnteringStateFX", "FXList"}:
        raise ValueError(f"{target} FX root field is unsupported")
    if item.get("definingObject") != target or item.get("inheritanceDistance") != 0:
        raise ValueError(f"{target} FX root provenance mismatch")
    if item.get("sourceVirtualPath") != source_path:
        raise ValueError(f"{target} FX root source mismatch")
    fx_list_id = _safe_object_id(item.get("fxListId"), f"{target} FX list id")
    value = _text(item.get("value"), f"{target} FX root value")
    scope = _scope_rows(item.get("scope"), source_path, f"{target} FX root scope")
    trigger = _trigger(scope, value, fx_list=field == "FXList")
    row = {
        "field": field,
        "fxListId": fx_list_id,
        "trigger": trigger,
        "source": {
            "virtualPath": source_path,
            "line": _positive_int(item.get("line"), f"{target} FX root line"),
            "value": value,
        },
    }
    signature = (
        field,
        fx_list_id,
        trigger["stateFamily"],
        tuple(trigger["conditionTokens"]),
        trigger["stage"],
    )
    return row, signature


def _validate_subsystem_legend(payload: bytes) -> dict[str, Any]:
    try:
        text = payload.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("subsystem legend is not CP1252") from exc
    declarations: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for line_number, raw in enumerate(text.splitlines(), start=1):
        content = raw.split(";", 1)[0].strip()
        if not content:
            continue
        words = content.split()
        if words[0].casefold() == "loadsubsystem":
            if len(words) != 2 or current is not None:
                raise ValueError("malformed particle subsystem declaration")
            current = {"name": words[1], "line": line_number, "initFiles": []}
            continue
        if current is None:
            continue
        if content.casefold() == "end":
            declarations.append(current)
            current = None
            continue
        if "=" in content:
            field, value = (part.strip() for part in content.split("=", 1))
            if field.casefold() == "initfile":
                current["initFiles"].append(value.replace("\\", "/").casefold())
    if current is not None:
        raise ValueError("unterminated particle subsystem declaration")

    wanted = {
        "TheParticleSystemManager": _PARTICLE_SOURCE_PATH,
        "TheFXParticleSystemManager": _FX_PARTICLE_SOURCE_PATH,
    }
    selected: list[dict[str, Any]] = []
    for name, expected_path in wanted.items():
        matches = [row for row in declarations if row["name"] == name]
        if len(matches) != 1:
            raise ValueError(f"subsystem legend must define exactly one {name}")
        row = matches[0]
        if row["initFiles"] != [expected_path.casefold()]:
            raise ValueError(f"subsystem legend {name} InitFile mismatch")
        selected.append(
            {
                "name": name,
                "line": row["line"],
                "initFile": expected_path,
            }
        )
    selected.sort(key=lambda row: int(row["line"]))
    return {
        "declarationsInAuthoredFileOrder": selected,
        "provesLoaderExecutionOrder": False,
        "provesDuplicateIdentifierCollisionRule": False,
    }


def _validate_summary(
    census: Mapping[str, Any], targets: list[Mapping[str, Any]]
) -> None:
    summary = _mapping(census.get("summary"), "Fords census summary")
    all_systems: set[str] = set()
    all_definitions: set[tuple[str, str, str]] = set()
    all_fx_lists: set[tuple[str, str]] = set()
    placement_count = 0
    runtime_target_count = 0
    for target in targets:
        placements = _mapping(target.get("mapPlacements"), "target mapPlacements")
        placement_count += _nonnegative_int(placements.get("count"), "placement count")
        closure = _mapping(target.get("particleAndFxClosure"), "particleAndFxClosure")
        systems = _string_array(closure.get("particleSystemIds"), "particle system ids")
        all_systems.update(systems)
        conversion = _mapping(target.get("conversion"), "target conversion")
        if conversion.get("classification") == "particle-system-runtime-work":
            runtime_target_count += 1
        for raw in _list(closure.get("definitions"), "particle definitions"):
            item = _mapping(raw, "particle definition")
            all_definitions.add(
                (
                    _text(item.get("kind"), "particle definition kind"),
                    _text(item.get("id"), "particle definition id"),
                    _sha256(item.get("sha256"), "particle definition sha256"),
                )
            )
        for raw in _list(closure.get("fxLists"), "FX lists"):
            item = _mapping(raw, "FX list")
            all_fx_lists.add(
                (
                    _text(item.get("id"), "FX list id"),
                    _sha256(item.get("sha256"), "FX list sha256"),
                )
            )
    expected = {
        "targetCount": len(targets),
        "placementCount": placement_count,
        "uniqueParticleSystemCount": len(all_systems),
        "particleDefinitionBlockCount": len(all_definitions),
        "fxListCount": len(all_fx_lists),
        "particleRuntimeTargetCount": runtime_target_count,
    }
    for field, value in expected.items():
        if summary.get(field) != value:
            raise ValueError(f"Fords census summary {field} mismatch")


def _validate_family_oracle(
    raw: Mapping[str, Any], sources: Mapping[str, Mapping[str, Any]]
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    """Validate the bounded retail-binary family oracle and derive policy."""

    oracle = _mapping(raw, "particle-family oracle")
    if oracle.get("schema_version") != FAMILY_ORACLE_SCHEMA_VERSION:
        raise ValueError("unsupported particle-family oracle schema version")
    if oracle.get("scope") != (
        "BFME2 1.06 particle declaration family lookup and duplicate precedence"
    ):
        raise ValueError("particle-family oracle scope mismatch")

    expected_paths = (
        _PARTICLE_SOURCE_PATH,
        _FX_PARTICLE_SOURCE_PATH,
        _SUBSYSTEM_LEGEND_PATH,
        _FX_LIST_SOURCE_PATH,
        _RIPPLE_OBJECT_SOURCE_PATH,
    )
    retail_sources = [
        _mapping(item, "particle-family oracle retail source")
        for item in _list(oracle.get("retail_sources"), "oracle retail_sources")
    ]
    matched_sources: list[dict[str, Any]] = []
    for virtual_path in expected_paths:
        suffix = "/" + virtual_path.casefold()
        matches = [
            item
            for item in retail_sources
            if str(item.get("path", "")).replace("\\", "/").casefold().endswith(suffix)
        ]
        if len(matches) != 1:
            raise ValueError(
                f"particle-family oracle must identify exactly one {virtual_path}"
            )
        item = matches[0]
        record = _source_record(
            virtual_path,
            sources,
            expected_sha256=_sha256(
                item.get("sha256"), f"oracle {virtual_path} sha256"
            ),
            expected_size=_positive_int(
                item.get("bytes"), f"oracle {virtual_path} bytes"
            ),
        )
        matched_sources.append(record)

    game_rows = [
        item
        for item in retail_sources
        if str(item.get("path", "")).replace("\\", "/").casefold().endswith(
            "/game.dat"
        )
    ]
    if len(game_rows) != 1:
        raise ValueError("particle-family oracle must identify exactly one game.dat")
    game = game_rows[0]
    game_evidence = {
        "byteLength": _positive_int(game.get("bytes"), "oracle game.dat bytes"),
        "sha256": _sha256(game.get("sha256"), "oracle game.dat sha256"),
        "fileVersion": _text(game.get("file_version"), "oracle game.dat file version"),
        "productVersion": _text(
            game.get("product_version"), "oracle game.dat product version"
        ),
    }
    if {
        game_evidence["fileVersion"],
        game_evidence["productVersion"],
    } != {"1.6.2429.30210"}:
        raise ValueError("particle-family oracle game.dat version mismatch")

    expected_claim_grades = {
        "C1": "PROVEN",
        "C2": "PROVEN",
        "C3": "PROVEN",
        "C4": "UNRESOLVED",
        "C5": "UNRESOLVED",
        "C6": "CORROBORATION_ONLY",
    }
    claims = [
        _mapping(item, "particle-family oracle claim")
        for item in _list(oracle.get("claims"), "oracle claims")
    ]
    claim_grades = {
        _text(item.get("id"), "oracle claim id"): _text(
            item.get("grade"), "oracle claim grade"
        )
        for item in claims
    }
    if claim_grades != expected_claim_grades:
        raise ValueError("particle-family oracle claim grades mismatch")

    binary = _mapping(
        oracle.get("retail_binary_evidence"), "oracle retail_binary_evidence"
    )
    registration = _mapping(
        binary.get("manager_registration"), "oracle manager registration"
    )
    object_consumer = _mapping(
        binary.get("object_draw_consumer"), "oracle object draw consumer"
    )
    fx_consumer = _mapping(binary.get("fxlist_consumer"), "oracle FXList consumer")
    declaration = _mapping(
        binary.get("fx_declaration_parser"), "oracle FX declaration parser"
    )
    proven_binary_values = {
        "managerSubsystem": registration.get("subsystem_literal"),
        "managerGlobal": registration.get("manager_global"),
        "objectManagerLoad": object_consumer.get("manager_global_load_va"),
        "objectNameLookup": object_consumer.get("name_lookup_target_va"),
        "fxListManagerLoad": fx_consumer.get("runtime_manager_load_va"),
        "fxListNameLookup": fx_consumer.get("name_lookup_target_va"),
        "declarationManagerLoad": declaration.get("manager_global_load_va"),
        "declarationFindTarget": declaration.get("find_target_va"),
        "repeatedFxSyntax": declaration.get("duplicate_semantics"),
    }
    if proven_binary_values != {
        "managerSubsystem": "TheFXParticleSystemManager",
        "managerGlobal": "0xDFDD04",
        "objectManagerLoad": "0x7395DD",
        "objectNameLookup": "0x5F90DA",
        "fxListManagerLoad": "0x5E1A34",
        "fxListNameLookup": "0x5F90DA",
        "declarationManagerLoad": "0x5FD0DF",
        "declarationFindTarget": "0x5F90DA",
        "repeatedFxSyntax": "last_definition_wins",
    }:
        raise ValueError("particle-family oracle binary contract mismatch")

    probe = _mapping(oracle.get("probe_name"), "oracle probe_name")
    legacy_probe = _mapping(probe.get("legacy"), "oracle legacy probe")
    fx_probe = _mapping(probe.get("fx"), "oracle FX probe")
    probe_consumer = _mapping(probe.get("consumer"), "oracle probe consumer")
    if (
        probe.get("name") != "WaterRipplesSmall"
        or legacy_probe.get("declaration") != "ParticleSystem"
        or legacy_probe.get("priority") != "CRITICAL"
        or fx_probe.get("declaration") != "FXParticleSystem"
        or fx_probe.get("priority") != "VERY_LOW_OR_ABOVE"
        or probe_consumer.get("object") != "WtrRiplsSmall"
        or probe_consumer.get("reference_is_family_qualified") is not False
        or probe.get("visible_fields_materially_equivalent") is not True
        or probe.get("material_discriminator") != "priority/culling"
    ):
        raise ValueError("particle-family oracle WaterRipplesSmall probe mismatch")

    guidance = _mapping(
        oracle.get("converter_guidance"), "oracle converter_guidance"
    )
    if (
        guidance.get("preserve_both_source_declarations") is not True
        or guidance.get("preserve_family_and_source_provenance") is not True
        or guidance.get("emit_single_runtime_binding") is not True
        or guidance.get("current_provisional_choice_for_WaterRipplesSmall")
        != _RIPPLE_PROVISIONAL_FAMILY
        or guidance.get("choice_is_retail_precedence_proof") is not False
    ):
        raise ValueError("particle-family oracle converter guidance mismatch")

    oracle_digest = _canonical_sha256(oracle)
    evidence = {
        "aggregateSha256": oracle_digest,
        "retailSources": matched_sources,
        "retailBinary": game_evidence,
        "claimGrades": claim_grades,
        "runtimeNamespace": {
            "status": "proven-single-unqualified-manager-namespace",
            "managerSubsystem": "TheFXParticleSystemManager",
            "managerGlobal": "0xDFDD04",
            "nameLookupTarget": "0x5F90DA",
            "objectDrawAndFxListShareNamespace": True,
            "consumerReferencesAreFamilyQualified": False,
        },
        "duplicateSemantics": {
            "repeatedFxParticleSystemSyntax": "proven-last-definition-wins",
            "crossFamilyPrecedence": "unresolved",
            "legacySubsystemActive": "unresolved",
        },
    }
    selection = {
        "status": "provisional-explicit-runtime-selection",
        "selectedKind": _RIPPLE_PROVISIONAL_FAMILY,
        "crossFamilyPrecedenceProven": False,
        "generalizesToOtherDuplicateIdentifiers": False,
        "visibleFieldsMateriallyEquivalent": True,
        "materialDiscriminator": "priority/culling",
        "reason": _text(guidance.get("reason"), "oracle provisional reason"),
        "oracleAggregateSha256": oracle_digest,
    }
    return evidence, {"WaterRipplesSmall": selection}


def _validate_definition_row(
    raw: object,
    sources: Mapping[str, Mapping[str, Any]],
    reader: _PrivateSourceReader,
    parsed_cache: dict[str, tuple[ParticleDefinition, ...]],
) -> tuple[dict[str, Any], str, list[dict[str, Any]]]:
    item = _mapping(raw, "particle definition")
    expected_fields = {
        "assignmentCount",
        "byteLength",
        "endLine",
        "id",
        "kind",
        "nestedBlockCount",
        "particleNameIds",
        "renderAssets",
        "sha256",
        "source",
        "startLine",
    }
    if set(item) != expected_fields:
        raise ValueError("particle definition has unsupported fields")
    kind = _text(item.get("kind"), "particle definition kind")
    name = _safe_object_id(item.get("id"), "particle definition id")
    if kind not in {"ParticleSystem", "FXParticleSystem"}:
        raise ValueError("particle definition kind is unsupported")
    expected_path = (
        _PARTICLE_SOURCE_PATH if kind == "ParticleSystem" else _FX_PARTICLE_SOURCE_PATH
    )
    source = _source_file_record(
        item.get("source"),
        sources,
        "particle definition source",
        expected_role="particle-definition-document",
    )
    if source["virtualPath"] != expected_path:
        raise ValueError("particle definition family source path mismatch")
    payload = reader.read(expected_path)
    definitions = parsed_cache.get(expected_path)
    if definitions is None:
        definitions = parse_particle_definitions(payload)
        parsed_cache[expected_path] = definitions
    definition = select_particle_definition(definitions, name, kind=kind)
    if (
        definition.source.sha256
        != _sha256(item.get("sha256"), "definition block sha256")
        or definition.source.byte_length
        != _positive_int(item.get("byteLength"), "definition block byteLength")
        or definition.source.start_line
        != _positive_int(item.get("startLine"), "definition startLine")
        or definition.source.end_line
        != _positive_int(item.get("endLine"), "definition endLine")
    ):
        raise ValueError(f"particle definition span mismatch: {kind} {name}")
    blocks = definition.blocks(recursive=True)
    assignment_count = len(definition.assignments(recursive=True)) + sum(
        block.selector is not None for block in blocks
    )
    if assignment_count != _nonnegative_int(
        item.get("assignmentCount"), "definition assignmentCount"
    ):
        raise ValueError(
            f"particle definition assignment count mismatch: {kind} {name}"
        )
    if len(blocks) != _nonnegative_int(
        item.get("nestedBlockCount"), "definition nestedBlockCount"
    ):
        raise ValueError(f"particle definition block count mismatch: {kind} {name}")
    authored_names = [
        assignment.value
        for assignment in definition.assignments(recursive=True)
        if assignment.field.casefold() == "particlename"
    ]
    if authored_names != _string_array(
        item.get("particleNameIds"), "definition particleNameIds"
    ):
        raise ValueError(f"particle render identifier mismatch: {kind} {name}")
    if not authored_names:
        raise ValueError(f"particle definition has no render identifier: {kind} {name}")

    texture_records: list[dict[str, Any]] = []
    render_rows = _list(item.get("renderAssets"), "definition renderAssets")
    if len(render_rows) != len(authored_names):
        raise ValueError(f"particle render asset cardinality mismatch: {kind} {name}")
    for position, raw_render in enumerate(render_rows):
        render = _mapping(raw_render, f"particle render asset {position}")
        if set(render) != {"evidence", "file", "identifier", "status"}:
            raise ValueError("particle render asset has unsupported fields")
        identifier = _text(render.get("identifier"), "particle render identifier")
        if identifier != authored_names[position]:
            raise ValueError("particle render asset identifier is out of order")
        if (
            render.get("status") != "resolved"
            or render.get("evidence") != "exact-tga-stem-to-compiled-dds"
        ):
            raise ValueError("particle render asset is not an exact resolution")
        texture = _source_file_record(
            render.get("file"),
            sources,
            "particle render source",
            expected_role="particle-render-asset",
        )
        if (
            PurePosixPath(texture["virtualPath"]).stem.casefold()
            != PurePosixPath(identifier).stem.casefold()
        ):
            raise ValueError("particle render asset stem does not match ParticleName")
        reader.read(texture["virtualPath"])
        texture_records.append(texture)

    resource_id = _stable_slug("fords-particle-def", f"{kind}/{name}")
    output = f"effects/particles/definitions/{resource_id}.json"
    evidence = {
        "kind": kind,
        "definitionId": name,
        "resourceId": resource_id,
        "outputJson": output,
        "source": source,
        "sourceSpan": {
            "startLine": definition.source.start_line,
            "endLine": definition.source.end_line,
            "byteLength": definition.source.byte_length,
            "sha256": definition.source.sha256,
        },
        "assignmentCount": assignment_count,
        "nestedBlockCount": len(blocks),
        "particleNameIds": authored_names,
        "textureVirtualPaths": [row["virtualPath"] for row in texture_records],
    }
    return evidence, expected_path, texture_records


def _validate_fx_list_row(
    raw: object,
    sources: Mapping[str, Mapping[str, Any]],
    reader: _PrivateSourceReader,
) -> dict[str, Any]:
    item = _mapping(raw, "FX list")
    expected_fields = {
        "assignmentCount",
        "audioEventIds",
        "byteLength",
        "endLine",
        "hasViewShake",
        "id",
        "kind",
        "nestedBlockCount",
        "particleNameIds",
        "particleSystemIds",
        "sha256",
        "source",
        "startLine",
    }
    if set(item) != expected_fields or item.get("kind") != "FXList":
        raise ValueError("FX list has unsupported fields")
    name = _safe_object_id(item.get("id"), "FX list id")
    if name not in _FX_LIST_SYSTEMS:
        raise ValueError(f"unexpected Fords FX list: {name}")
    source = _source_file_record(
        item.get("source"),
        sources,
        "FX list source",
        expected_role="fx-list-definition-document",
    )
    if source["virtualPath"] != _FX_LIST_SOURCE_PATH:
        raise ValueError("FX list source path mismatch")
    payload = reader.read(_FX_LIST_SOURCE_PATH)
    start_line = _positive_int(item.get("startLine"), "FX list startLine")
    end_line = _positive_int(item.get("endLine"), "FX list endLine")
    block = _raw_span(payload, start_line, end_line)
    if len(block) != _positive_int(
        item.get("byteLength"), "FX list byteLength"
    ) or hashlib.sha256(block).hexdigest() != _sha256(
        item.get("sha256"), "FX list sha256"
    ):
        raise ValueError(f"FX list source span mismatch: {name}")
    assignment_count, nested_block_count = _fx_list_body_counts(block)
    if assignment_count != _nonnegative_int(
        item.get("assignmentCount"), "FX list assignmentCount"
    ):
        raise ValueError(f"FX list assignment count mismatch: {name}")
    if nested_block_count != _nonnegative_int(
        item.get("nestedBlockCount"), "FX list nestedBlockCount"
    ):
        raise ValueError(f"FX list nested-block count mismatch: {name}")
    systems = _string_array(item.get("particleSystemIds"), "FX list particle systems")
    if tuple(systems) != _FX_LIST_SYSTEMS[name]:
        raise ValueError(f"FX list particle edge mismatch: {name}")
    particle_names = _string_array(
        item.get("particleNameIds"), "FX list particle names"
    )
    if particle_names:
        raise ValueError(f"FX list has unsupported direct render leaves: {name}")
    audio = _string_array(item.get("audioEventIds"), "FX list audio events")
    has_view_shake = item.get("hasViewShake")
    if type(has_view_shake) is not bool:
        raise ValueError("FX list hasViewShake must be a boolean")
    if name == "FX_StructureMediumCollapse":
        if audio != ["BuildingSink"] or not has_view_shake:
            raise ValueError("medium collapse FX audio/view-shake closure mismatch")
    elif audio or has_view_shake:
        raise ValueError(f"unexpected FX audio/view shake: {name}")
    return {
        "fxListId": name,
        "particleSystemIds": systems,
        "audioEventIds": audio,
        "hasViewShake": has_view_shake,
        "assignmentCount": assignment_count,
        "nestedBlockCount": nested_block_count,
        "source": source,
        "sourceSpan": {
            "startLine": start_line,
            "endLine": end_line,
            "byteLength": len(block),
            "sha256": hashlib.sha256(block).hexdigest(),
        },
    }


def _resource_candidate(
    definition: Mapping[str, Any], texture_ids: Mapping[str, str]
) -> dict[str, Any]:
    paths = [str(path) for path in definition["textureVirtualPaths"]]
    return {
        "kind": definition["kind"],
        "definitionId": definition["definitionId"],
        "definitionResourceId": definition["resourceId"],
        "definitionOutputJson": definition["outputJson"],
        "sourceBlockSha256": definition["sourceSpan"]["sha256"],
        "textureResourceIds": [texture_ids[path] for path in paths],
    }


def _system_reference(
    system: str,
    definitions: Mapping[tuple[str, str], Mapping[str, Any]],
    texture_ids: Mapping[str, str],
    runtime_selections: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    families = _SYSTEM_FAMILIES[system]
    candidates = [
        _resource_candidate(definitions[(kind, system)], texture_ids)
        for kind in families
    ]
    if len(candidates) == 1:
        resolution = {
            "status": "exact-single-authored-family",
            "selectedKind": candidates[0]["kind"],
        }
    elif system in runtime_selections:
        resolution = deepcopy(dict(runtime_selections[system]))
    else:
        resolution = {
            "status": "unresolved-cross-family-precedence",
            "selectedKind": None,
            "crossFamilyPrecedenceProven": False,
        }
    return {
        "particleSystemId": system,
        "definitionCandidates": candidates,
        "familyResolution": resolution,
    }


def _validate_generated_profile(
    resources: list[dict[str, Any]], runtime: Mapping[str, Any]
) -> bool:
    payload = {
        "format": 1,
        "id": "fords-particle-fragment-validation",
        "pack": {"id": "fords-particle-fragment-validation-pack"},
        "resources": resources,
        "runtime_data": {RUNTIME_DATA_PATH: runtime},
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-fords-particle-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(
            json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=False,
            ),
            encoding="utf-8",
        )
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != len(resources):
        raise ValueError("generated particle ImportProfile changed resource count")
    return True


def build_retail_fords_particle_plan(
    census_document: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
    family_oracle_document: Mapping[str, Any],
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    """Build a deterministic exact Fords particle conversion and binding plan."""

    census = _mapping(census_document, "Fords unresolved-object census")
    if census.get("schema") != CENSUS_SCHEMA:
        raise ValueError("unsupported Fords unresolved-object census schema")
    if census.get("schemaVersion") != CENSUS_SCHEMA_VERSION:
        raise ValueError("unsupported Fords unresolved-object census schema version")
    census_digest = _validate_declared_digest(
        census, "aggregateSha256", "Fords unresolved-object census"
    )
    manifest = _mapping(effective_assets_manifest, "effective-assets manifest")
    sources, manifest_evidence = _validate_effective_manifest(manifest)
    oracle_evidence, runtime_selections = _validate_family_oracle(
        _mapping(family_oracle_document, "particle-family oracle"), sources
    )
    reader = _PrivateSourceReader(effective_assets_root, sources)

    source_evidence = _mapping(census.get("sourceEvidence"), "census sourceEvidence")
    census_manifest = _mapping(
        source_evidence.get("effectiveAssetsManifest"),
        "census effectiveAssetsManifest",
    )
    expected_manifest_evidence = {
        "aggregateSha256": manifest_evidence["aggregateSha256"],
        "byteLength": manifest_evidence["byteLength"],
        "catalogIdentitySha256": manifest_evidence["catalogIdentitySha256"],
        "fileCount": manifest_evidence["fileCount"],
    }
    if dict(census_manifest) != expected_manifest_evidence:
        raise ValueError("census effective-assets identity does not match manifest")

    targets = [
        _mapping(raw, f"Fords census target {position}")
        for position, raw in enumerate(_list(census.get("targets"), "census targets"))
    ]
    _validate_summary(census, targets)
    by_name: dict[str, Mapping[str, Any]] = {}
    for target in targets:
        definition = _mapping(target.get("objectDefinition"), "target objectDefinition")
        name = _safe_object_id(definition.get("name"), "target Object name")
        if name.casefold() in {item.casefold() for item in by_name}:
            raise ValueError(f"duplicate or case-ambiguous census target: {name}")
        by_name[name] = target
    for required in _TARGET_PLACEMENTS:
        if required not in by_name:
            folded = [
                name for name in by_name if name.casefold() == required.casefold()
            ]
            if folded:
                raise ValueError(f"Fords target case mismatch: {folded[0]!r}")
            raise ValueError(f"Fords particle target is absent: {required}")

    particle_source_rows = [
        _source_file_record(
            raw,
            sources,
            f"particle definition document {position}",
            expected_role="particle-definition-document",
        )
        for position, raw in enumerate(
            _list(
                source_evidence.get("particleDefinitionDocuments"),
                "particleDefinitionDocuments",
            )
        )
    ]
    if [row["virtualPath"] for row in particle_source_rows] != [
        _PARTICLE_SOURCE_PATH,
        _FX_PARTICLE_SOURCE_PATH,
    ]:
        raise ValueError("particle definition document source set/order mismatch")
    fx_list_source = _source_file_record(
        source_evidence.get("fxListDefinitionDocument"),
        sources,
        "FX list definition document",
        expected_role="fx-list-definition-document",
    )
    if fx_list_source["virtualPath"] != _FX_LIST_SOURCE_PATH:
        raise ValueError("FX list definition source path mismatch")

    parsed_cache: dict[str, tuple[ParticleDefinition, ...]] = {}
    definitions: dict[tuple[str, str], dict[str, Any]] = {}
    texture_sources: dict[str, dict[str, Any]] = {}
    fx_lists: dict[str, dict[str, Any]] = {}
    target_rows: list[dict[str, Any]] = []
    object_sources: list[dict[str, Any]] = []

    for target_name in _TARGET_PLACEMENTS:
        target = by_name[target_name]
        object_definition = _mapping(
            target.get("objectDefinition"), f"{target_name} objectDefinition"
        )
        if (
            object_definition.get("kind") != "Object"
            or object_definition.get("inheritanceComplete") is not True
            or object_definition.get("parent") is not None
            or object_definition.get("ancestry") != [target_name]
        ):
            raise ValueError(f"{target_name} Object definition is not exact/direct")
        object_source = _source_file_record(
            object_definition.get("sourceFile"),
            sources,
            f"{target_name} object source",
            expected_role="object-definition-document",
        )
        if object_definition.get("sourceVirtualPath") != object_source["virtualPath"]:
            raise ValueError(f"{target_name} object source path mismatch")
        reader.read(object_source["virtualPath"])
        object_sources.append(object_source)

        placements = _mapping(
            target.get("mapPlacements"), f"{target_name} mapPlacements"
        )
        placement_count = _nonnegative_int(
            placements.get("count"), f"{target_name} placement count"
        )
        if placement_count != _TARGET_PLACEMENTS[target_name]:
            raise ValueError(f"{target_name} Fords placement count mismatch")
        placement_records = _list(
            placements.get("records"), f"{target_name} placement records"
        )
        if len(placement_records) != placement_count:
            raise ValueError(f"{target_name} placement record count mismatch")
        seen_record_indices: set[int] = set()
        for position, raw_record in enumerate(placement_records):
            record = _mapping(raw_record, f"{target_name} placement {position}")
            if set(record) != {"recordIndex", "uniqueId"}:
                raise ValueError(f"{target_name} placement has unsupported fields")
            record_index = _nonnegative_int(
                record.get("recordIndex"), f"{target_name} placement recordIndex"
            )
            if record_index in seen_record_indices:
                raise ValueError(f"{target_name} duplicate placement recordIndex")
            seen_record_indices.add(record_index)
            _text(record.get("uniqueId"), f"{target_name} placement uniqueId")

        closure = _mapping(
            target.get("particleAndFxClosure"),
            f"{target_name} particleAndFxClosure",
        )
        expected_closure_fields = {
            "attachments",
            "definitions",
            "fxLists",
            "fxRoots",
            "particleSystemIds",
        }
        if set(closure) != expected_closure_fields:
            raise ValueError(f"{target_name} particle closure has unsupported fields")
        systems = _string_array(
            closure.get("particleSystemIds"), f"{target_name} particleSystemIds"
        )
        if tuple(systems) != _TARGET_SYSTEMS[target_name]:
            raise ValueError(f"{target_name} particle system closure mismatch")

        for raw_definition in _list(
            closure.get("definitions"), f"{target_name} definitions"
        ):
            evidence, _, textures = _validate_definition_row(
                raw_definition, sources, reader, parsed_cache
            )
            key = (str(evidence["kind"]), str(evidence["definitionId"]))
            previous = definitions.get(key)
            if previous is not None and previous != evidence:
                raise ValueError(f"inconsistent repeated particle definition: {key}")
            definitions[key] = evidence
            for texture in textures:
                path = str(texture["virtualPath"])
                old = texture_sources.get(path)
                if old is not None and old != texture:
                    raise ValueError(f"inconsistent repeated particle texture: {path}")
                texture_sources[path] = texture

        for raw_fx_list in _list(closure.get("fxLists"), f"{target_name} FX lists"):
            evidence = _validate_fx_list_row(raw_fx_list, sources, reader)
            name = str(evidence["fxListId"])
            previous = fx_lists.get(name)
            if previous is not None and previous != evidence:
                raise ValueError(f"inconsistent repeated FX list: {name}")
            fx_lists[name] = evidence

        attachments: list[dict[str, Any]] = []
        attachment_signatures: list[tuple[Any, ...]] = []
        for raw_attachment in _list(
            closure.get("attachments"), f"{target_name} attachments"
        ):
            attachment, signature = _normalize_attachment(
                raw_attachment, target_name, object_source["virtualPath"]
            )
            attachments.append(attachment)
            attachment_signatures.append(signature)
        if tuple(attachment_signatures) != _ATTACHMENT_SIGNATURES[target_name]:
            raise ValueError(f"{target_name} particle attachment contract mismatch")

        fx_roots: list[dict[str, Any]] = []
        fx_root_signatures: list[tuple[Any, ...]] = []
        for raw_fx_root in _list(closure.get("fxRoots"), f"{target_name} FX roots"):
            fx_root, signature = _normalize_fx_root(
                raw_fx_root, target_name, object_source["virtualPath"]
            )
            fx_roots.append(fx_root)
            fx_root_signatures.append(signature)
        if tuple(fx_root_signatures) != _FX_ROOT_SIGNATURES[target_name]:
            raise ValueError(f"{target_name} FX-root contract mismatch")

        target_rows.append(
            {
                "typeName": target_name,
                "matchMethod": "exact-type-name",
                "placementCount": placement_count,
                "objectSource": object_source,
                "particleSystemIds": systems,
                "attachments": attachments,
                "fxRoots": fx_roots,
            }
        )

    expected_definition_keys = {
        (kind, system)
        for system, families in _SYSTEM_FAMILIES.items()
        for kind in families
    }
    if set(definitions) != expected_definition_keys:
        missing = sorted(expected_definition_keys - set(definitions))
        extra = sorted(set(definitions) - expected_definition_keys)
        raise ValueError(
            f"Fords particle definition family closure mismatch: missing={missing}, extra={extra}"
        )
    if set(fx_lists) != set(_FX_LIST_SYSTEMS):
        raise ValueError("Fords FX-list closure is incomplete")

    texture_resources: list[dict[str, Any]] = []
    texture_ids: dict[str, str] = {}
    for path in sorted(texture_sources, key=lambda value: (value.casefold(), value)):
        resource_id = _stable_slug("fords-particle-texture", path)
        texture_ids[path] = resource_id
        texture_resources.append(
            {
                "id": resource_id,
                "kind": "texture",
                "patterns": [path],
                "required": True,
                "converter": "texture",
                "output": f"assets/textures/effects/{resource_id}.png",
                "limit": 1,
                "expected_count": 1,
            }
        )

    definition_resources = [
        {
            "id": evidence["resourceId"],
            "kind": "data",
            "patterns": [evidence["source"]["virtualPath"]],
            "required": True,
            "converter": "sage-particle-definition",
            "output": evidence["outputJson"],
            "limit": 1,
            "expected_count": 1,
            "options": {
                "kind": evidence["kind"],
                "name": evidence["definitionId"],
            },
        }
        for _, evidence in sorted(
            definitions.items(), key=lambda item: (item[0][1].casefold(), item[0][0])
        )
    ]

    ripple_target = by_name["WtrRiplsSmall"]
    visual = _mapping(
        ripple_target.get("visualReferences"), "WtrRiplsSmall visualReferences"
    )
    references = _list(visual.get("references"), "WtrRiplsSmall visual references")
    if visual.get("count") != 1 or len(references) != 1:
        raise ValueError("WtrRiplsSmall must have exactly one visual anchor")
    reference = _mapping(references[0], "WtrRiplsSmall visual reference")
    if (
        reference.get("kind") != "model"
        or reference.get("status") != "resolved"
        or reference.get("targetObject") != "WtrRiplsSmall"
        or reference.get("identifier") != "P_WtrRiplsSmall"
    ):
        raise ValueError("WtrRiplsSmall visual anchor reference mismatch")
    physical_files = _list(
        reference.get("physicalFiles"), "WtrRiplsSmall anchor physicalFiles"
    )
    if len(physical_files) != 1:
        raise ValueError("WtrRiplsSmall visual anchor must resolve to one W3D")
    anchor_source = _source_file_record(
        physical_files[0],
        sources,
        "WtrRiplsSmall anchor source",
        expected_role="visual-model",
    )
    if anchor_source["virtualPath"] != _RIPPLE_ANCHOR_PATH:
        raise ValueError("WtrRiplsSmall anchor source path mismatch")
    reader.read(_RIPPLE_ANCHOR_PATH)
    anchor_resource = {
        "id": "fords-particle-anchor-wtrripls-small",
        "kind": "data",
        "patterns": [_RIPPLE_ANCHOR_PATH],
        "required": True,
        "converter": "hash-only",
        "limit": 1,
        "expected_count": 1,
    }

    for target in target_rows:
        target["systems"] = [
            _system_reference(system, definitions, texture_ids, runtime_selections)
            for system in target.pop("particleSystemIds")
        ]
        if target["typeName"] == "WtrRiplsSmall":
            target["anchor"] = {
                "sourceVirtualModel": _RIPPLE_ANCHOR_PATH,
                "sourceResourceId": anchor_resource["id"],
                "bone": _RIPPLE_ANCHOR_BONE,
            }

    for fx_list in fx_lists.values():
        fx_list["systems"] = [
            _system_reference(system, definitions, texture_ids, runtime_selections)
            for system in fx_list.pop("particleSystemIds")
        ]

    subsystem_source = _source_record(_SUBSYSTEM_LEGEND_PATH, sources)
    subsystem_payload = reader.read(_SUBSYSTEM_LEGEND_PATH)
    subsystem_evidence = _validate_subsystem_legend(subsystem_payload)
    subsystem_evidence["source"] = subsystem_source
    fx_list_payload = reader.read(_FX_LIST_SOURCE_PATH)
    authored_fx_comment = (
        b"Name = {particle name in ParticleSystem.ini}" in fx_list_payload
    )

    duplicate_family_systems = sorted(
        [system for system, families in _SYSTEM_FAMILIES.items() if len(families) > 1],
        key=lambda value: (value.casefold(), value),
    )
    provisional_systems = sorted(
        runtime_selections, key=lambda value: (value.casefold(), value)
    )
    unresolved_duplicate_systems = [
        system
        for system in duplicate_family_systems
        if system not in runtime_selections
    ]
    family_resolution = {
        "status": "provisional-selection-with-cross-family-precedence-unresolved",
        "runtimeNamespace": deepcopy(oracle_evidence["runtimeNamespace"]),
        "duplicateSemantics": deepcopy(oracle_evidence["duplicateSemantics"]),
        "duplicateIdentifierSystemIds": duplicate_family_systems,
        "provisionalRuntimeSelections": [
            {
                "particleSystemId": system,
                **deepcopy(dict(runtime_selections[system])),
            }
            for system in provisional_systems
        ],
        "unresolvedDuplicateIdentifierSystemIds": unresolved_duplicate_systems,
        "noGeneralPrecedenceRule": True,
        "retailAuthoredEvidence": {
            "subsystemLegend": subsystem_evidence,
            "fxListCommentNamesParticleSystemIni": authored_fx_comment,
        },
        "blocker": (
            "The retail oracle proves one unqualified runtime namespace and "
            "last-definition-wins for repeated FX syntax, but cross-family precedence "
            "remains unresolved. Preserve both authored candidates. The explicit "
            "WaterRipplesSmall FX selection is provisional and does not generalize."
        ),
    }

    runtime: dict[str, Any] = {
        "schema": FORDS_PARTICLE_BINDINGS_SCHEMA,
        "schemaVersion": FORDS_PARTICLE_BINDINGS_SCHEMA_VERSION,
        "sourceCensusAggregateSha256": census_digest,
        "familyResolution": deepcopy(family_resolution),
        "definitionRegistry": [
            {
                **_resource_candidate(evidence, texture_ids),
                "particleNameIds": evidence["particleNameIds"],
            }
            for _, evidence in sorted(
                definitions.items(),
                key=lambda item: (item[0][1].casefold(), item[0][0]),
            )
        ],
        "fxLists": [
            fx_lists[name]
            for name in sorted(fx_lists, key=lambda value: (value.casefold(), value))
        ],
        "objectBindings": target_rows,
    }
    resources = [*texture_resources, anchor_resource, *definition_resources]
    _case_unique(
        [str(resource["id"]) for resource in resources], "particle resource id"
    )
    output_paths = [
        str(resource["output"]) for resource in resources if "output" in resource
    ]
    _case_unique(output_paths, "particle resource output")
    profile_validated = _validate_generated_profile(resources, runtime)

    plan: dict[str, Any] = {
        "schema": FORDS_PARTICLE_PLAN_SCHEMA,
        "schemaVersion": FORDS_PARTICLE_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "censusAggregateSha256": census_digest,
            "effectiveAssets": manifest_evidence,
            "particleFamilyOracle": oracle_evidence,
            "particleDefinitionDocuments": particle_source_rows,
            "fxListDefinitionDocument": fx_list_source,
            "objectDefinitionDocuments": object_sources,
            "rippleAnchor": anchor_source,
            "textureSources": [
                texture_sources[path]
                for path in sorted(
                    texture_sources, key=lambda value: (value.casefold(), value)
                )
            ],
            "privateReadBoundary": reader.evidence(),
        },
        "policy": {
            "selection": "exact-active-fords-particle-closure-only",
            "substitutesAllowed": False,
            "genericFallbackAllowed": False,
            "textureGrouping": "one-resource-per-exact-physical-source",
            "duplicateFamilySelection": (
                "oracle-bounded-provisional-water-only-no-general-precedence-rule"
            ),
            "profileFragmentValidatedByImportProfile": profile_validated,
        },
        "familyResolution": family_resolution,
        "definitions": [
            evidence
            for _, evidence in sorted(
                definitions.items(),
                key=lambda item: (item[0][1].casefold(), item[0][0]),
            )
        ],
        "profileFragment": {
            "resources": resources,
            "runtimeDataPath": RUNTIME_DATA_PATH,
            "runtimeData": runtime,
        },
        "summary": {
            "targetTypeCount": len(target_rows),
            "placementCount": sum(int(row["placementCount"]) for row in target_rows),
            "particleSystemIdCount": len(_SYSTEM_FAMILIES),
            "definitionResourceCount": len(definition_resources),
            "legacyDefinitionCount": sum(
                key[0] == "ParticleSystem" for key in definitions
            ),
            "fxDefinitionCount": sum(
                key[0] == "FXParticleSystem" for key in definitions
            ),
            "duplicateFamilySystemCount": len(duplicate_family_systems),
            "textureResourceCount": len(texture_resources),
            "anchorResourceCount": 1,
            "profileResourceCount": len(resources),
            "directAttachmentCount": sum(
                len(row["attachments"]) for row in target_rows
            ),
            "fxRootCount": sum(len(row["fxRoots"]) for row in target_rows),
            "fxListCount": len(fx_lists),
            "provisionalRuntimeSelectionCount": len(provisional_systems),
            "unresolvedFamilySelectionCount": len(unresolved_duplicate_systems),
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "men-fords-v0-particles-generated",
    pack_id: str = "bfme2-men-vslice-particles-private",
) -> dict[str, Any]:
    """Return a standalone validated private ImportProfile for the plan."""

    document = _mapping(plan, "Fords particle plan")
    if document.get("schema") != FORDS_PARTICLE_PLAN_SCHEMA:
        raise ValueError("unsupported Fords particle plan schema")
    _validate_declared_digest(document, "aggregateSha256", "Fords particle plan")
    fragment = _mapping(document.get("profileFragment"), "particle profileFragment")
    resources = _list(fragment.get("resources"), "particle profile resources")
    runtime = _mapping(fragment.get("runtimeData"), "particle runtimeData")
    if fragment.get("runtimeDataPath") != RUNTIME_DATA_PATH:
        raise ValueError("particle runtime-data path mismatch")
    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II Fords exact particle closure",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
        "runtime_data": {RUNTIME_DATA_PATH: deepcopy(dict(runtime))},
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-fords-particle-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile


def load_retail_fords_particle_plan_inputs(
    census_path: Path | str,
    effective_assets_manifest_path: Path | str,
    family_oracle_path: Path | str,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Load the three bounded JSON inputs; build performs semantic validation."""

    def load(path: Path | str, label: str) -> dict[str, Any]:
        source = Path(path).expanduser().resolve()
        if not source.is_file() or source.stat().st_size > _MAX_INPUT_BYTES:
            raise ValueError(f"{label} is missing or exceeds {_MAX_INPUT_BYTES} bytes")
        try:
            value = json.loads(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"invalid {label}: {exc}") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{label} root must be an object")
        return value

    return (
        load(census_path, "Fords unresolved-object census"),
        load(effective_assets_manifest_path, "effective-assets manifest"),
        load(family_oracle_path, "particle-family oracle"),
    )


def write_retail_fords_particle_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    """Atomically write one sealed private Fords particle plan."""

    document = _mapping(plan, "Fords particle plan")
    if document.get("schema") != FORDS_PARTICLE_PLAN_SCHEMA:
        raise ValueError("cannot write an unsupported Fords particle plan schema")
    _validate_declared_digest(document, "aggregateSha256", "Fords particle plan")
    write_json_atomic(Path(path), deepcopy(dict(document)))


def write_generated_import_profile(
    path: Path | str, profile: Mapping[str, Any]
) -> None:
    """Validate and atomically write a standalone generated ImportProfile."""

    document = _mapping(profile, "generated Fords particle ImportProfile")
    with tempfile.TemporaryDirectory(prefix="openbfme-fords-particle-write-") as raw:
        check = Path(raw) / "profile.json"
        check.write_text(json.dumps(document), encoding="utf-8")
        ImportProfile.load(check)
    write_json_atomic(Path(path), deepcopy(dict(document)))


__all__ = [
    "FORDS_PARTICLE_BINDINGS_SCHEMA",
    "FORDS_PARTICLE_BINDINGS_SCHEMA_VERSION",
    "FORDS_PARTICLE_PLAN_SCHEMA",
    "FORDS_PARTICLE_PLAN_SCHEMA_VERSION",
    "FAMILY_ORACLE_SCHEMA_VERSION",
    "RUNTIME_DATA_PATH",
    "build_retail_fords_particle_plan",
    "generated_import_profile",
    "load_retail_fords_particle_plan_inputs",
    "write_generated_import_profile",
    "write_retail_fords_particle_plan",
]
