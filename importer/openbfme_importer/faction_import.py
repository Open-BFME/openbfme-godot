"""Deterministic completeness planning for a BFME2 faction import."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Mapping

from .catalog import InstallCatalog
from .faction_census import census_playable_faction
from .faction_policy import implicit_object_roots
from .playable_unit_import import FACTIONS, _source_documents
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
)


SCHEMA = "openbfme.faction-import-plan"
SCHEMA_VERSION = 0


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")


def _family(kinds: tuple[str, ...]) -> str:
    values = set(kinds)
    if values & {"STRUCTURE", "BASE_FOUNDATION", "FS_BASE_DEFENSE"}:
        return "structure"
    if values & {"SPELL_BOOK", "SIDEBAR_DISPLAY"}:
        return "spellbook"
    if "PROJECTILE" in values:
        return "projectile"
    if "BANNER" in values:
        return "banner-member"
    if values & {"PORTER", "DOZER"}:
        return "builder"
    if "HERO" in values:
        return "hero-extension"
    if "HORDE" in values:
        return "horde-extension"
    if values & {"INFANTRY", "CAVALRY", "MONSTER", "SIEGEENGINE", "SHIP"}:
        return "unit-extension"
    return "unclassified"


def _sha256(value: object, field: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise ValueError(f"{field} must be a lowercase SHA-256 identity")
    return value


def build_faction_import_plan(
    faction_graph: Mapping[str, object],
    documents: Mapping[str, bytes],
    *,
    catalog_identity_sha256: str,
) -> dict[str, object]:
    """Account for every command-reachable Object without claiming unsupported work."""

    definitions = faction_graph.get("definitions")
    if not isinstance(definitions, Mapping) or not isinstance(
        definitions.get("objects"), list
    ):
        raise ValueError("faction graph Object definitions are invalid")
    rows = definitions["objects"]
    object_ids = [
        str(row["id"])
        for row in rows
        if isinstance(row, Mapping) and isinstance(row.get("id"), str) and row["id"]
    ]
    if len(object_ids) != len(rows):
        raise ValueError("faction graph has an invalid Object row")
    if len({value.casefold() for value in object_ids}) != len(object_ids):
        raise ValueError("faction graph has duplicate Object identities")
    graph_rows = {str(row["id"]).casefold(): row for row in rows}
    summary_source = faction_graph.get("summary")
    if not isinstance(summary_source, Mapping):
        raise ValueError("faction graph summary is invalid")
    raw_unresolved = summary_source.get("unresolvedCount")
    if (
        not isinstance(raw_unresolved, int)
        or isinstance(raw_unresolved, bool)
        or raw_unresolved < 0
    ):
        raise ValueError("faction graph unresolvedCount is invalid")
    unresolved = raw_unresolved
    target = faction_graph.get("target")
    if not isinstance(target, Mapping):
        raise ValueError("faction graph target is invalid")
    player_template = target.get("playerTemplate")
    faction = target.get("faction")
    if not isinstance(player_template, str) or not player_template:
        raise ValueError("faction graph playerTemplate is invalid")
    if not isinstance(faction, str) or not faction:
        raise ValueError("faction graph faction is invalid")
    expected_faction = next(
        (
            spec[2]
            for spec in FACTIONS
            if spec[1].casefold() == player_template.casefold()
        ),
        None,
    )
    if expected_faction is None or faction != expected_faction:
        raise ValueError(
            "faction graph playerTemplate/faction identity pair is invalid"
        )
    catalog_identity = _sha256(catalog_identity_sha256, "catalogIdentitySha256")
    graph_identity = _sha256(
        faction_graph.get("inputSetSha256"), "factionGraphInputSetSha256"
    )

    prepared = prepare_playable_unit_compiler(documents)
    objects: list[dict[str, object]] = []
    for object_id in sorted(object_ids, key=lambda value: (value.casefold(), value)):
        kinds: tuple[str, ...] = ()
        source_path = ""
        parse_error: str | None = None
        try:
            kinds = playable_object_kind_of(prepared, object_id)
            descriptor = compile_playable_unit_descriptor(
                object_id, documents, faction_graph=faction_graph, prepared=prepared
            )
        except PlayableUnitCompilerError as exc:
            if kinds:
                family = _family(kinds)
            elif object_id.casefold() in prepared.objects:
                family = "object-inheritance"
            else:
                graph_row = graph_rows[object_id.casefold()]
                source = graph_row.get("source")
                source_path = (
                    str(source.get("virtualPath", ""))
                    if isinstance(source, Mapping)
                    else ""
                )
                parse_error = prepared.object_parse_errors.get(source_path.casefold())
                family = "retail-object-parser" if parse_error else "missing-object"
            objects.append(
                {
                    "id": object_id,
                    "family": family,
                    "kindOf": list(kinds),
                    "status": "converter-gap",
                    "reason": str(exc),
                    **(
                        {
                            "sourceVirtualPath": source_path,
                            "parserError": parse_error,
                        }
                        if not kinds
                        and object_id.casefold() not in prepared.objects
                        and parse_error
                        else {}
                    ),
                }
            )
        else:
            objects.append(
                {
                    "id": object_id,
                    "family": "playable-unit",
                    "category": descriptor["category"],
                    "kindOf": list(kinds),
                    "status": "descriptor-ready",
                    "descriptorSha256": descriptor["descriptorSha256"],
                }
            )

    ready_count = sum(row["status"] == "descriptor-ready" for row in objects)
    gaps = len(objects) - ready_count
    descriptor_coverage_complete = unresolved == 0 and gaps == 0
    families = sorted(
        {str(row["family"]) for row in objects if row["status"] != "descriptor-ready"}
    )
    plan: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "target": {
            "playerTemplate": player_template,
            "faction": faction,
        },
        "inputs": {
            "catalogIdentitySha256": catalog_identity,
            "factionGraphInputSetSha256": graph_identity,
        },
        "objects": objects,
        "summary": {
            # Version 0 is deliberately a planning boundary. Publication can
            # become ready only after conversion, pack audit, and runtime
            # receipts are added to a later schema.
            "ready": False,
            "publicationReady": False,
            "descriptorCoverageComplete": descriptor_coverage_complete,
            "objectCount": len(objects),
            "descriptorReadyCount": ready_count,
            "converterGapCount": gaps,
            "unresolvedLeafCount": unresolved,
            "unsupportedFamilies": families,
            "blockingReason": "plan-only schema has no audited pack/runtime receipt",
        },
        "requiredClosure": [
            "playable-unit-runtime",
            "structure-runtime",
            "weapon-projectile",
            "visual-animation-material",
            "fx-particle-audio-ui",
            "construction-damage-destruction",
        ],
    }
    plan["aggregateSha256"] = hashlib.sha256(_canonical_bytes(plan)).hexdigest()
    return plan


def plan_faction_import(
    catalog: InstallCatalog, effective_root: Path, faction: str
) -> dict[str, object]:
    """Build the source-backed plan for one of the six BFME2 factions."""

    key = faction.casefold().strip()
    spec = next(
        (
            item
            for item in FACTIONS
            if key in {item[0], item[1].casefold(), item[2].casefold()}
        ),
        None,
    )
    if spec is None:
        raise ValueError(f"unsupported playable faction: {faction!r}")
    graph = census_playable_faction(
        catalog,
        player_template=spec[1],
        expected_side=spec[2],
        implicit_object_roots=implicit_object_roots(spec[1]),
    )
    return build_faction_import_plan(
        graph,
        _source_documents(effective_root),
        catalog_identity_sha256=catalog.identity_sha256(),
    )


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "build_faction_import_plan",
    "plan_faction_import",
]
