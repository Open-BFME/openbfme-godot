"""Deterministic completeness planning and conversion for a BFME2 faction import."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Callable, Mapping

from .catalog import InstallCatalog
from .faction_census import census_playable_faction
from .faction_policy import implicit_object_roots
from .playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
)
from .playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    compile_structure_visual_recipe,
    compose_structure_runtime_document,
)
from .playable_unit_import import (
    FACTIONS,
    _resolved_media,
    _resolved_strings,
    _source_documents,
)
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
)
from .playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    compile_playable_unit_pack_recipe,
)
from .retail_visual_closure import build_retail_visual_closure


SCHEMA = "openbfme.faction-import-plan"
SCHEMA_VERSION = 0
COVERAGE_SCHEMA = "openbfme.faction-import-coverage"
COVERAGE_SCHEMA_VERSION = 0

_EXCLUDED_FAMILY_REASONS = {
    "banner-member": "banner members convert inside their parent horde recipes",
    "projectile": "projectiles convert inside their firing unit recipes",
    "spellbook": "spell book surfaces are outside the vertical-slice scope",
    "object-inheritance": "inheritance-only base objects are not standalone content",
}


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


def _faction_spec(faction: str) -> tuple[str, str, str]:
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
    return spec


def plan_faction_import(
    catalog: InstallCatalog, effective_root: Path, faction: str
) -> dict[str, object]:
    """Build the source-backed plan for one of the six BFME2 factions."""

    spec = _faction_spec(faction)
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


def _wall_template_roots(faction_graph: Mapping[str, object]) -> tuple[str, ...]:
    definitions = faction_graph.get("definitions")
    if not isinstance(definitions, Mapping):
        return ()
    targets: set[str] = set()
    for row in definitions.get("objects", []):
        if not isinstance(row, Mapping):
            continue
        for edge in row.get("edges", []):
            if not isinstance(edge, Mapping):
                continue
            kind = str(edge.get("targetKind", ""))
            target = str(edge.get("targetId", ""))
            if kind.startswith("wall-") and kind.endswith("-template") and target:
                targets.add(target)
    return tuple(sorted(targets, key=lambda value: (value.casefold(), value)))


def build_faction_conversion(
    faction_graph: Mapping[str, object],
    documents: Mapping[str, bytes],
    effective_root: Path,
    *,
    catalog_identity_sha256: str,
    artifact_writer: Callable[[str, str, Mapping[str, object]], None] | None = None,
    catalog: InstallCatalog | None = None,
) -> dict[str, object]:
    """Convert every supported plan row and account for the rest, fail-closed.

    Per-object conversion failures become coverage rows, never a batch abort;
    ``artifact_writer(object_id, artifact_kind, document)`` receives each
    compiled descriptor, recipe, and runtime document for persistence.
    """

    plan = build_faction_import_plan(
        faction_graph, documents, catalog_identity_sha256=catalog_identity_sha256
    )
    prepared = prepare_playable_unit_compiler(documents)
    target = plan["target"]
    assert isinstance(target, Mapping)
    template = str(target["playerTemplate"])
    spawned = tuple(
        object_id for object_id, _reason in implicit_object_roots(template)
    )
    wall_templates = _wall_template_roots(faction_graph)

    rows: list[dict[str, object]] = []
    for plan_row in plan["objects"]:
        assert isinstance(plan_row, Mapping)
        object_id = str(plan_row["id"])
        family = str(plan_row["family"])
        status = str(plan_row["status"])
        row: dict[str, object] = {"id": object_id, "family": family}
        if status == "descriptor-ready":
            try:
                draft = compile_playable_unit_descriptor(
                    object_id,
                    documents,
                    faction_graph=faction_graph,
                    prepared=prepared,
                )
                images, audio = _resolved_media(faction_graph, draft)
                strings = (
                    _resolved_strings(catalog, draft)
                    if catalog is not None
                    else None
                )
                descriptor = compile_playable_unit_descriptor(
                    object_id,
                    documents,
                    faction_graph=faction_graph,
                    resolved_images=images,
                    resolved_audio=audio,
                    resolved_strings=strings,
                    prepared=prepared,
                )
                composition = descriptor["composition"]
                assert isinstance(composition, Mapping)
                targets = {str(composition["containerObjectId"])}
                targets.update(
                    str(member["objectId"]) for member in composition["members"]
                )
                closure = build_retail_visual_closure(
                    effective_root, sorted(targets, key=str.casefold)
                )
                recipe = compile_playable_unit_pack_recipe(descriptor, closure)
            except (
                PlayableUnitCompilerError,
                PlayableUnitPackCompilerError,
                ValueError,
            ) as exc:
                row.update({"status": "converter-gap", "reason": str(exc)})
            else:
                if artifact_writer is not None:
                    artifact_writer(object_id, "descriptor", descriptor)
                    artifact_writer(object_id, "pack-recipe", recipe)
                row.update(
                    {
                        "status": "converted",
                        "converter": "playable-unit",
                        "category": str(descriptor["category"]),
                        "descriptorSha256": descriptor["descriptorSha256"],
                        "recipeSha256": recipe["recipeSha256"],
                        "resourceCount": len(recipe["resources"]),
                    }
                )
        elif family == "structure":
            descriptor = None
            try:
                descriptor = compile_playable_structure_descriptor(
                    object_id,
                    documents,
                    prepared=prepared,
                    engine_spawned_roots=spawned,
                    wall_template_roots=wall_templates,
                )
                closure = build_retail_visual_closure(effective_root, [object_id])
                recipe = compile_structure_visual_recipe(object_id, closure)
                runtime = compose_structure_runtime_document(descriptor, recipe)
            except (
                PlayableStructureCompilerError,
                PlayableStructurePackCompilerError,
                ValueError,
            ) as exc:
                if (
                    isinstance(descriptor, Mapping)
                    and "no resolved lifecycle model" in str(exc)
                    and "BASE_FOUNDATION" in descriptor.get("kindOf", [])
                ):
                    row.update(
                        {
                            "status": "excluded",
                            "reason": (
                                "foundation composite authors no lifecycle "
                                "visuals"
                            ),
                            "descriptorSha256": descriptor["descriptorSha256"],
                        }
                    )
                else:
                    row.update({"status": "converter-gap", "reason": str(exc)})
            else:
                if artifact_writer is not None:
                    artifact_writer(object_id, "descriptor", descriptor)
                    artifact_writer(object_id, "pack-recipe", recipe)
                    artifact_writer(object_id, "runtime", runtime)
                row.update(
                    {
                        "status": "converted",
                        "converter": "playable-structure",
                        "category": "structure",
                        "productionEvidence": str(
                            descriptor["production"]["evidence"]
                        ),
                        "descriptorSha256": descriptor["descriptorSha256"],
                        "recipeSha256": recipe["recipeSha256"],
                        "runtimeSha256": runtime["runtimeSha256"],
                        "resourceCount": len(recipe["resources"]),
                    }
                )
        elif family in _EXCLUDED_FAMILY_REASONS:
            row.update(
                {
                    "status": "excluded",
                    "reason": _EXCLUDED_FAMILY_REASONS[family],
                }
            )
        else:
            row.update(
                {
                    "status": "converter-gap",
                    "reason": str(plan_row.get("reason", "")),
                }
            )
        rows.append(row)

    counts = {
        key: sum(row["status"] == key for row in rows)
        for key in ("converted", "excluded", "converter-gap")
    }
    plan_summary = plan["summary"]
    assert isinstance(plan_summary, Mapping)
    unresolved = int(plan_summary["unresolvedLeafCount"])
    coverage: dict[str, object] = {
        "schema": COVERAGE_SCHEMA,
        "schemaVersion": COVERAGE_SCHEMA_VERSION,
        "target": dict(target),
        "inputs": dict(plan["inputs"]),
        "planAggregateSha256": plan["aggregateSha256"],
        "objects": rows,
        "summary": {
            # Conversion coverage is not publication: pack build, audit, and
            # runtime receipts belong to the publication stage.
            "publicationReady": False,
            "objectCount": len(rows),
            "convertedCount": counts["converted"],
            "excludedCount": counts["excluded"],
            "converterGapCount": counts["converter-gap"],
            "unresolvedLeafCount": unresolved,
            "conversionComplete": unresolved == 0
            and counts["converter-gap"] == 0,
            "blockingReason": "conversion artifacts lack a pack/runtime receipt",
        },
    }
    coverage["aggregateSha256"] = hashlib.sha256(
        _canonical_bytes(coverage)
    ).hexdigest()
    return coverage


def convert_faction_import(
    catalog: InstallCatalog,
    effective_root: Path,
    faction: str,
    *,
    artifact_writer: Callable[[str, str, Mapping[str, object]], None] | None = None,
) -> dict[str, object]:
    """Convert one faction's supported objects and account for every other row."""

    spec = _faction_spec(faction)
    graph = census_playable_faction(
        catalog,
        player_template=spec[1],
        expected_side=spec[2],
        implicit_object_roots=implicit_object_roots(spec[1]),
    )
    return build_faction_conversion(
        graph,
        _source_documents(effective_root),
        effective_root,
        catalog_identity_sha256=catalog.identity_sha256(),
        artifact_writer=artifact_writer,
        catalog=catalog,
    )


__all__ = [
    "COVERAGE_SCHEMA",
    "COVERAGE_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "build_faction_conversion",
    "build_faction_import_plan",
    "convert_faction_import",
    "plan_faction_import",
]
