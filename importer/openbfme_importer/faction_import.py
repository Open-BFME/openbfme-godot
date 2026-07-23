"""Deterministic completeness planning and conversion for a BFME2 faction import."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import threading
from typing import Callable, Mapping

from .catalog import InstallCatalog
from .faction_census import census_playable_faction, resolve_playable_faction
from .faction_object_cache import (
    FactionObjectCache,
    compiler_identity_token,
    default_cache_root,
    documents_fingerprint,
    durable_effective_assets_fingerprint,
    object_cache_key,
    policy_roots_fingerprint,
)
from .faction_policy import (
    implicit_object_roots,
    music_roots,
    source_null_command_sets,
    source_null_mapped_image_textures,
)
from .playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
)
from .playable_structure_lifecycle_evidence import (
    compile_structure_lifecycle_evidence,
)
from .playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    compile_structure_visual_recipe,
    compose_structure_runtime_document,
)
from .playable_unit_import import _resolved_media, _resolved_strings
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
)
from .playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    compile_playable_unit_pack_recipe,
)
from .retail_visual_closure import build_retail_visual_closure
from .spellbook_compiler import (
    SpellbookCompilerError,
    compile_spellbook_descriptor,
)
from .spellbook_import import (
    _resolved_spellbook_media,
    _resolved_spellbook_strings,
    spellbook_source_documents,
)
from .spellbook_pack_compiler import (
    SpellbookPackCompilerError,
    compile_spellbook_pack_recipe,
    compose_spellbook_runtime_document,
)


SCHEMA = "openbfme.faction-import-plan"
SCHEMA_VERSION = 0
COVERAGE_SCHEMA = "openbfme.faction-import-coverage"
COVERAGE_SCHEMA_VERSION = 0

# Run-only fields excluded from coverage aggregateSha256 (cold/warm/jobs stable).
_COVERAGE_EPHEMERAL_OBJECT_KEYS = frozenset({"cacheHit"})
_COVERAGE_EPHEMERAL_SUMMARY_KEYS = frozenset({"cacheHits", "convertWorkers"})


def coverage_digest_payload(coverage: Mapping[str, object]) -> dict[str, object]:
    """Body used for aggregateSha256 — strips run metadata that is not content."""

    body: dict[str, object] = {
        key: value for key, value in coverage.items() if key != "aggregateSha256"
    }
    objects = body.get("objects")
    if isinstance(objects, list):
        body["objects"] = [
            (
                {k: v for k, v in row.items() if k not in _COVERAGE_EPHEMERAL_OBJECT_KEYS}
                if isinstance(row, Mapping)
                else row
            )
            for row in objects
        ]
    summary = body.get("summary")
    if isinstance(summary, Mapping):
        body["summary"] = {
            k: v
            for k, v in summary.items()
            if k not in _COVERAGE_EPHEMERAL_SUMMARY_KEYS
        }
    return body

def _structure_required_images(
    descriptor: Mapping[str, object],
) -> list[tuple[str, str, str]]:
    """(usage, imageId, gapReason) rows for one structure descriptor.

    ``imageId`` is empty exactly when the evidence itself is absent; then
    ``gapReason`` names why (no authored construct command, a construct
    button without a ButtonImage, no authored SelectPortrait). Non-empty ids
    carry an empty reason and resolve against the faction census below.
    """

    rows: list[tuple[str, str, str]] = []
    production = descriptor.get("production")
    routes = (
        production.get("routes", []) if isinstance(production, Mapping) else []
    )
    construct_routes = [
        row
        for row in routes
        if isinstance(row, Mapping) and row.get("surface") == "construct"
    ]
    if not construct_routes:
        rows.append(("construct-button", "", "no-authored-construct-command"))
    else:
        image_ids = sorted(
            {
                str(row["buttonImageId"])
                for row in construct_routes
                if isinstance(row.get("buttonImageId"), str)
                and row.get("buttonImageId")
            },
            key=str.casefold,
        )
        if not image_ids:
            rows.append(
                ("construct-button", "", "construct-button-authors-no-image")
            )
        rows.extend(("construct-button", image_id, "") for image_id in image_ids)
    presentation = descriptor.get("presentation")
    ui = presentation.get("ui") if isinstance(presentation, Mapping) else None
    portrait_row = ui.get("SelectPortrait") if isinstance(ui, Mapping) else None
    portrait = (
        str(portrait_row.get("expression", ""))
        if isinstance(portrait_row, Mapping)
        else ""
    )
    if portrait.casefold() in {"", "none"}:
        rows.append(("select-portrait", "", "no-authored-select-portrait"))
    else:
        rows.append(("select-portrait", portrait, ""))
    return rows


def _resolved_structure_images(
    faction_graph: Mapping[str, object], descriptor: Mapping[str, object]
) -> tuple[dict[str, Mapping[str, object]], list[dict[str, object]]]:
    """Resolve a structure's construct-button and selection-portrait images
    through the faction census MappedImage closure.

    Fail-closed per image: every id that cannot be resolved to a converted
    atlas crop becomes an explicit gap row (kept in the recipe and runtime
    document) — never a silent omission and never borrowed art.
    """

    required = _structure_required_images(descriptor)
    leaves = faction_graph.get("resolvedLeaves")
    mapped_rows = (
        leaves.get("mappedImages") if isinstance(leaves, Mapping) else None
    )
    if not isinstance(mapped_rows, list):
        # A graph without a MappedImage closure cannot bind any art; every
        # required image becomes an explicit recorded gap, never a guess.
        return {}, [
            {
                "usage": usage,
                "imageId": image_id,
                "reason": gap_reason or "faction-graph-has-no-mapped-image-closure",
            }
            for usage, image_id, gap_reason in required
        ]
    images_by_id = {
        str(row["id"]).casefold(): row
        for row in mapped_rows
        if isinstance(row, Mapping) and isinstance(row.get("id"), str)
    }
    images: dict[str, Mapping[str, object]] = {}
    gaps: list[dict[str, object]] = []
    for usage, image_id, gap_reason in required:
        if gap_reason:
            gaps.append({"usage": usage, "imageId": image_id, "reason": gap_reason})
            continue
        row = images_by_id.get(image_id.casefold())
        if row is None:
            gaps.append(
                {
                    "usage": usage,
                    "imageId": image_id,
                    "reason": "unresolved-mapped-image",
                }
            )
        elif not isinstance(row.get("compiledTextureVirtualPath"), str):
            gaps.append(
                {
                    "usage": usage,
                    "imageId": image_id,
                    "reason": "unresolved-mapped-image-texture",
                }
            )
        elif image_id not in images:
            images[image_id] = row
    return images, gaps


_EXCLUDED_FAMILY_REASONS = {
    "banner-member": "banner members convert inside their parent horde recipes",
    "projectile": "projectiles convert inside their firing unit recipes",
    "object-inheritance": "inheritance-only base objects are not standalone content",
    "create-a-hero": "create-a-hero slots are engine-managed by the CAH editor flow",
}

# Plan-time exclusions are the families whose content is accounted for inside
# another row's descriptor.  Spell book rows compile through the spellbook
# lane; retail-object-parser rows stay explicit converter gaps until their
# owning lane lands.
_PLAN_EXCLUDED_FAMILY_REASONS = {
    key: _EXCLUDED_FAMILY_REASONS[key]
    for key in ("banner-member", "projectile", "object-inheritance", "create-a-hero")
}


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")


def _family(kinds: tuple[str, ...]) -> str:
    values = set(kinds)
    if "CREATE_A_HERO" in values:
        return "create-a-hero"
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
    if re.fullmatch(r"[A-Za-z0-9_+.-]+", player_template) is None:
        raise ValueError("faction graph playerTemplate identity is invalid")
    if re.fullmatch(r"[A-Za-z0-9_+.-]+", faction) is None:
        raise ValueError("faction graph faction identity is invalid")
    catalog_identity = _sha256(catalog_identity_sha256, "catalogIdentitySha256")
    graph_identity = _sha256(
        faction_graph.get("inputSetSha256"), "factionGraphInputSetSha256"
    )

    prepared = None
    preparation_error: str | None = None
    try:
        prepared = prepare_playable_unit_compiler(documents)
    except PlayableUnitCompilerError as exc:
        preparation_error = str(exc)
    roots = faction_graph.get("roots", [])
    if not isinstance(roots, list):
        raise ValueError("faction graph roots are invalid")
    engine_spawned_roots = tuple(
        str(row["id"])
        for row in roots
        if isinstance(row, Mapping)
        and row.get("edgeKind") == "engine-implicit-object"
        and isinstance(row.get("id"), str)
        and row["id"]
    )
    wall_template_roots = _wall_template_roots(faction_graph)
    source_null_sets = _source_null_command_set_ids(faction_graph)
    horde_banner_targets = {
        str(edge["targetId"]).casefold()
        for row in rows
        if isinstance(row, Mapping)
        for edge in row.get("edges", [])
        if isinstance(edge, Mapping)
        and edge.get("targetKind") == "horde-banner"
        and isinstance(edge.get("targetId"), str)
        and edge["targetId"]
    }
    objects: list[dict[str, object]] = []
    for object_id in sorted(object_ids, key=lambda value: (value.casefold(), value)):
        if prepared is None:
            objects.append(
                {
                    "id": object_id,
                    "family": "retail-object-parser",
                    "kindOf": [],
                    "status": "converter-gap",
                    "reason": f"compiler initialization failed: {preparation_error}",
                }
            )
            continue
        kinds: tuple[str, ...] = ()
        source_path = ""
        parse_error: str | None = None
        try:
            kinds = playable_object_kind_of(prepared, object_id)
        except PlayableUnitCompilerError as exc:
            if object_id.casefold() in prepared.objects:
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
            if family in _PLAN_EXCLUDED_FAMILY_REASONS:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": [],
                        "status": "excluded",
                        "reason": _PLAN_EXCLUDED_FAMILY_REASONS[family],
                    }
                )
            else:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": [],
                        "status": "converter-gap",
                        "reason": str(exc),
                        **(
                            {
                                "sourceVirtualPath": source_path,
                                "parserError": parse_error,
                            }
                            if parse_error
                            else {}
                        ),
                    }
                )
            continue
        family = _family(kinds)
        if family == "structure":
            try:
                structure_descriptor = compile_playable_structure_descriptor(
                    object_id,
                    documents,
                    prepared=prepared,
                    engine_spawned_roots=engine_spawned_roots,
                    wall_template_roots=wall_template_roots,
                    source_null_command_sets=source_null_sets,
                )
            except PlayableStructureCompilerError as exc:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": list(kinds),
                        "status": "converter-gap",
                        "reason": str(exc),
                    }
                )
            else:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "category": "structure",
                        "kindOf": list(kinds),
                        "status": "descriptor-ready",
                        "descriptorSha256": structure_descriptor["descriptorSha256"],
                    }
                )
            continue
        if family == "spellbook":
            try:
                spellbook_descriptor = compile_spellbook_descriptor(
                    faction_graph, documents, prepared=prepared
                )
                spellbook_row = spellbook_descriptor.get("spellBook")
                spellbook_object_id = (
                    str(spellbook_row.get("objectId", ""))
                    if isinstance(spellbook_row, Mapping)
                    else ""
                )
                if spellbook_object_id.casefold() != object_id.casefold():
                    raise SpellbookCompilerError(
                        "spell book descriptor identity disagrees with "
                        f"{object_id}"
                    )
            except SpellbookCompilerError as exc:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": list(kinds),
                        "status": "converter-gap",
                        "reason": str(exc),
                    }
                )
            else:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "category": "spellbook",
                        "kindOf": list(kinds),
                        "status": "descriptor-ready",
                        "descriptorSha256": spellbook_descriptor["descriptorSha256"],
                    }
                )
            continue
        try:
            descriptor = compile_playable_unit_descriptor(
                object_id, documents, faction_graph=faction_graph, prepared=prepared
            )
        except PlayableUnitCompilerError as exc:
            if family == "banner-member" or object_id.casefold() in horde_banner_targets:
                # Banner carriers — including ObjectReskin banners without a
                # BANNER KindOf — are accounted for inside their parent horde
                # recipes, never as standalone converter gaps.
                family = "banner-member"
            if family in _PLAN_EXCLUDED_FAMILY_REASONS:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": list(kinds),
                        "status": "excluded",
                        "reason": _PLAN_EXCLUDED_FAMILY_REASONS[family],
                    }
                )
            else:
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": list(kinds),
                        "status": "converter-gap",
                        "reason": str(exc),
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
    excluded_count = sum(row["status"] == "excluded" for row in objects)
    gaps = len(objects) - ready_count - excluded_count
    descriptor_coverage_complete = unresolved == 0 and gaps == 0
    families = sorted(
        {str(row["family"]) for row in objects if row["status"] == "converter-gap"}
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
            "excludedCount": excluded_count,
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


def _faction_spec(
    catalog: InstallCatalog, faction: str
) -> tuple[str, str, str]:
    discovered = resolve_playable_faction(catalog, faction)
    return discovered.short_name, discovered.name, discovered.side


def plan_faction_import(
    catalog: InstallCatalog,
    effective_root: Path,
    faction: str,
    *,
    game: str = "bfme2",
) -> dict[str, object]:
    """Build a source-backed plan for one discovered playable faction."""

    from .progress import emit as progress_emit

    progress_emit("census", f"census playable faction: {faction}")
    spec = _faction_spec(catalog, faction)
    graph = census_playable_faction(
        catalog,
        player_template=spec[1],
        game=game,
        expected_side=spec[2],
        implicit_object_roots=implicit_object_roots(spec[1], game=game),
        source_null_mapped_image_textures=source_null_mapped_image_textures(
            spec[1], game=game
        ),
        source_null_command_sets=source_null_command_sets(spec[1], game=game),
        music_roots=music_roots(spec[1], game=game),
    )
    progress_emit("faction-plan", f"building import plan: {faction}")
    return build_faction_import_plan(
        graph,
        spellbook_source_documents(effective_root),
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


def _source_null_command_set_ids(faction_graph: Mapping[str, object]) -> tuple[str, ...]:
    """Return census-recorded retail-absent CommandSet ids, if the graph has them."""

    dependencies = faction_graph.get("dependencies")
    if not isinstance(dependencies, Mapping):
        return ()
    rows = dependencies.get("sourceNullCommandSets")
    if not isinstance(rows, list):
        return ()
    return tuple(
        str(row["id"])
        for row in rows
        if isinstance(row, Mapping) and isinstance(row.get("id"), str) and row["id"]
    )


def _convert_one_plan_object(
    plan_row: Mapping[str, object],
    *,
    documents: Mapping[str, bytes],
    prepared: PlayableUnitCompilerInputs,
    faction_graph: Mapping[str, object],
    effective_root: Path,
    catalog: InstallCatalog | None,
    spawned: tuple[str, ...],
    wall_templates: tuple[str, ...],
    source_null_sets: tuple[str, ...],
    object_cache: FactionObjectCache | None,
    documents_fp: str,
    catalog_identity_sha256: str,
    assets_fp: str,
    graph_input_set_sha256: str,
    plan_aggregate_sha256: str,
    policy_fp: str,
    compiler_token: str,
) -> tuple[dict[str, object], dict[str, Mapping[str, object]]]:
    """Convert one plan row; returns (coverage_row, artifacts)."""

    object_id = str(plan_row["id"])
    family = str(plan_row["family"])
    status = str(plan_row["status"])
    row: dict[str, object] = {"id": object_id, "family": family}
    artifacts: dict[str, Mapping[str, object]] = {}

    plan_descriptor = plan_row.get("descriptorSha256")
    cache_key = object_cache_key(
        family=family,
        object_id=object_id,
        documents_fp=documents_fp,
        catalog_identity_sha256=catalog_identity_sha256,
        effective_root_fp=assets_fp,
        graph_input_set_sha256=graph_input_set_sha256,
        plan_aggregate_sha256=plan_aggregate_sha256,
        policy_fp=policy_fp,
        compiler_token=compiler_token,
        plan_descriptor_sha256=(
            str(plan_descriptor) if isinstance(plan_descriptor, str) else ""
        ),
        extra={"plan_status": status},
    )
    if object_cache is not None and (
        family in {"structure", "spellbook"} or status == "descriptor-ready"
    ):
        hit = object_cache.get(cache_key)
        if hit is not None:
            cached_row = dict(hit["row"])
            cached_row["cacheHit"] = True
            return cached_row, dict(hit["artifacts"])

    if family == "structure":
        descriptor = None
        try:
            descriptor = compile_playable_structure_descriptor(
                object_id,
                documents,
                prepared=prepared,
                engine_spawned_roots=spawned,
                wall_template_roots=wall_templates,
                source_null_command_sets=source_null_sets,
            )
            closure = build_retail_visual_closure(effective_root, [object_id])
            images, image_gaps = _resolved_structure_images(
                faction_graph, descriptor
            )
            recipe = compile_structure_visual_recipe(
                object_id,
                closure,
                resolved_images=images,
                image_binding_gaps=image_gaps,
            )
            evidence = compile_structure_lifecycle_evidence(
                object_id, documents, prepared=prepared
            )
            runtime = compose_structure_runtime_document(
                descriptor, recipe, evidence
            )
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
                            "foundation composite authors no lifecycle visuals"
                        ),
                        "descriptorSha256": descriptor["descriptorSha256"],
                    }
                )
            else:
                row.update({"status": "converter-gap", "reason": str(exc)})
        else:
            artifacts = {
                "descriptor": descriptor,
                "pack-recipe": recipe,
                "lifecycle-evidence": evidence,
                "runtime": runtime,
            }
            row.update(
                {
                    "status": "converted",
                    "converter": "playable-structure",
                    "category": "structure",
                    "productionEvidence": str(descriptor["production"]["evidence"]),
                    "descriptorSha256": descriptor["descriptorSha256"],
                    "recipeSha256": recipe["recipeSha256"],
                    "runtimeSha256": runtime["runtimeSha256"],
                    "resourceCount": len(recipe["resources"]),
                }
            )
    elif family == "spellbook":
        try:
            draft = compile_spellbook_descriptor(
                faction_graph, documents, prepared=prepared
            )
            draft_row = draft.get("spellBook")
            draft_object_id = (
                str(draft_row.get("objectId", ""))
                if isinstance(draft_row, Mapping)
                else ""
            )
            if draft_object_id.casefold() != object_id.casefold():
                raise SpellbookCompilerError(
                    "spell book descriptor identity disagrees with "
                    f"{object_id}"
                )
            images, audio = _resolved_spellbook_media(faction_graph, draft)
            strings = (
                _resolved_spellbook_strings(catalog, draft)
                if catalog is not None
                else None
            )
            descriptor = compile_spellbook_descriptor(
                faction_graph,
                documents,
                resolved_images=images,
                resolved_audio=audio,
                resolved_strings=strings,
                prepared=prepared,
            )
            recipe = compile_spellbook_pack_recipe(descriptor)
            runtime = compose_spellbook_runtime_document(descriptor, recipe)
        except (
            SpellbookCompilerError,
            SpellbookPackCompilerError,
            ValueError,
        ) as exc:
            row.update({"status": "converter-gap", "reason": str(exc)})
        else:
            artifacts = {
                "descriptor": descriptor,
                "pack-recipe": recipe,
                "runtime": runtime,
            }
            row.update(
                {
                    "status": "converted",
                    "converter": "spellbook",
                    "category": "spellbook",
                    "descriptorSha256": descriptor["descriptorSha256"],
                    "recipeSha256": recipe["recipeSha256"],
                    "runtimeSha256": runtime["runtimeSha256"],
                    "resourceCount": len(recipe["resources"]),
                }
            )
    elif status == "descriptor-ready":
        try:
            draft = compile_playable_unit_descriptor(
                object_id,
                documents,
                faction_graph=faction_graph,
                prepared=prepared,
            )
            images, audio = _resolved_media(faction_graph, draft)
            strings = (
                _resolved_strings(catalog, draft) if catalog is not None else None
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
            artifacts = {
                "descriptor": descriptor,
                "pack-recipe": recipe,
                "visual-closure": closure,
            }
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

    if (
        object_cache is not None
        and row.get("status") == "converted"
        and artifacts
    ):
        object_cache.put(cache_key, row=row, artifacts=artifacts)
    return row, artifacts


def build_faction_conversion(
    faction_graph: Mapping[str, object],
    documents: Mapping[str, bytes],
    effective_root: Path,
    *,
    catalog_identity_sha256: str,
    artifact_writer: Callable[[str, str, Mapping[str, object]], None] | None = None,
    catalog: InstallCatalog | None = None,
    state_root: Path | None = None,
    convert_jobs: int | None = None,
    game: str = "bfme2",
) -> dict[str, object]:
    """Convert every supported plan row and account for the rest, fail-closed.

    Per-object conversion failures (including unexpected exceptions) become
    coverage rows, never a batch abort;
    ``artifact_writer(object_id, artifact_kind, document)`` receives each
    compiled descriptor, recipe, and runtime document for persistence.
    Visual-closure documents are computed for recipes but are not written or
    cached (re-derivable from effective assets + descriptor).
    """

    from .progress import emit as progress_emit

    progress_emit("faction-plan", "building faction import plan")
    plan = build_faction_import_plan(
        faction_graph, documents, catalog_identity_sha256=catalog_identity_sha256
    )
    # Fail-closed on compiler-init failure (mirrors build_faction_import_plan):
    # a corpus the shared compiler cannot index — e.g. RotWK's repeated
    # ChildObject declarations that trip the strict global object index — must
    # gap every object, never abort the whole convert batch.
    try:
        prepared = prepare_playable_unit_compiler(documents)
        preparation_error: str | None = None
    except PlayableUnitCompilerError as exc:
        prepared = None
        preparation_error = str(exc)
    target = plan["target"]
    assert isinstance(target, Mapping)
    template = str(target["playerTemplate"])
    spawned = tuple(
        object_id for object_id, _reason in implicit_object_roots(template, game=game)
    )
    wall_templates = _wall_template_roots(faction_graph)
    source_null_sets = _source_null_command_set_ids(faction_graph)

    plan_objects = list(plan["objects"])
    docs_fp = documents_fingerprint(documents)
    assets_fp = durable_effective_assets_fingerprint(effective_root)
    graph_input = str(faction_graph.get("inputSetSha256") or "")
    plan_aggregate = str(plan.get("aggregateSha256") or "")
    policy_fp = policy_roots_fingerprint(
        spawned=spawned,
        wall_templates=wall_templates,
        source_null_sets=source_null_sets,
    )
    compiler_token = compiler_identity_token()
    cache_disabled = os.environ.get("OPENBFME_NO_OBJECT_CACHE", "").strip().casefold() in {
        "1",
        "true",
        "yes",
    }
    object_cache: FactionObjectCache | None = None
    if not cache_disabled:
        # Only enable when a durable root is known (state_root / shared / import
        # root). Never fall back to cwd — that pollutes the repo during unit tests.
        import_root = os.environ.get("OPENBFME_IMPORT_ROOT", "").strip()
        shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
        if state_root is not None:
            object_cache = FactionObjectCache(default_cache_root(Path(state_root)))
        elif shared or import_root:
            object_cache = FactionObjectCache(
                default_cache_root(Path(import_root or "."))
            )

    try:
        workers = int(
            convert_jobs
            if convert_jobs is not None
            else os.environ.get("OPENBFME_FACTION_CONVERT_JOBS", "0")
        )
    except ValueError:
        workers = 0
    if workers <= 0:
        workers = max(1, min(8, (os.cpu_count() or 4) - 1))

    progress_emit(
        "faction-convert",
        f"converting {len(plan_objects)} objects ({workers} workers"
        f"{', cache on' if object_cache else ', cache off'})",
        total_units=len(plan_objects),
    )

    writer_lock = threading.Lock()
    results: list[dict[str, object] | None] = [None] * len(plan_objects)

    def _work(index: int, plan_row: Mapping[str, object]) -> tuple[int, dict[str, object]]:
        assert isinstance(plan_row, Mapping)
        try:
            row, artifacts = _convert_one_plan_object(
                plan_row,
                documents=documents,
                prepared=prepared,
                faction_graph=faction_graph,
                effective_root=effective_root,
                catalog=catalog,
                spawned=spawned,
                wall_templates=wall_templates,
                source_null_sets=source_null_sets,
                object_cache=object_cache,
                documents_fp=docs_fp,
                catalog_identity_sha256=catalog_identity_sha256,
                assets_fp=assets_fp,
                graph_input_set_sha256=graph_input,
                plan_aggregate_sha256=plan_aggregate,
                policy_fp=policy_fp,
                compiler_token=compiler_token,
            )
        except Exception as exc:  # noqa: BLE001 — fail-closed per object, not batch
            object_id = str(plan_row.get("id", f"index-{index}"))
            family = str(plan_row.get("family", "unknown"))
            row = {
                "id": object_id,
                "family": family,
                "status": "converter-gap",
                "reason": f"unexpected convert error: {type(exc).__name__}: {exc}",
            }
            artifacts = {}
        if artifact_writer is not None and artifacts:
            object_id = str(plan_row["id"])
            with writer_lock:
                for kind, document in artifacts.items():
                    if kind == "visual-closure":
                        continue
                    artifact_writer(object_id, kind, document)
        return index, row

    if prepared is None:
        # Compiler initialization failed for the whole corpus: account for every
        # planned object as a converter-gap without attempting conversion. The
        # plan already carries the retail-object-parser gap rows.
        progress_emit(
            "faction-convert",
            f"compiler initialization failed; gapping {len(plan_objects)} objects: "
            f"{preparation_error}",
        )
        for index, plan_row in enumerate(plan_objects):
            assert isinstance(plan_row, Mapping)
            results[index] = dict(plan_row)
    elif workers == 1 or len(plan_objects) <= 1:
        for index, plan_row in enumerate(plan_objects):
            assert isinstance(plan_row, Mapping)
            idx, row = _work(index, plan_row)
            results[idx] = row
            progress_emit(
                "faction-convert",
                f"done {row.get('family')}: {row.get('id')} ({row.get('status')}"
                f"{', cache' if row.get('cacheHit') else ''})",
                unit_delta=1,
            )
    else:
        with ThreadPoolExecutor(max_workers=min(workers, len(plan_objects))) as pool:
            futures = {
                pool.submit(_work, index, plan_row): index
                for index, plan_row in enumerate(plan_objects)
                if isinstance(plan_row, Mapping)
            }
            for future in as_completed(futures):
                idx, row = future.result()
                results[idx] = row
                progress_emit(
                    "",
                    f"done {row.get('family')}: {row.get('id')} ({row.get('status')}"
                    f"{', cache' if row.get('cacheHit') else ''})",
                    unit_delta=1,
                )

    rows = [row for row in results if isinstance(row, dict)]

    counts = {
        key: sum(row["status"] == key for row in rows)
        for key in ("converted", "excluded", "converter-gap")
    }
    plan_summary = plan["summary"]
    assert isinstance(plan_summary, Mapping)
    unresolved = int(plan_summary["unresolvedLeafCount"])
    cache_hits = sum(1 for row in rows if row.get("cacheHit"))
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
            "cacheHits": cache_hits,
            "convertWorkers": workers,
        },
    }
    # Ephemeral run metadata stays in the document for operators but is not
    # content identity (compose_faction_profile / profile receipts bind this).
    coverage["aggregateSha256"] = hashlib.sha256(
        _canonical_bytes(coverage_digest_payload(coverage))
    ).hexdigest()
    return coverage


def convert_faction_import(
    catalog: InstallCatalog,
    effective_root: Path,
    faction: str,
    *,
    artifact_writer: Callable[[str, str, Mapping[str, object]], None] | None = None,
    state_root: Path | None = None,
    convert_jobs: int | None = None,
    game: str = "bfme2",
) -> dict[str, object]:
    """Convert one faction's supported objects and account for every other row."""

    from .progress import emit as progress_emit

    source_policy = catalog.source_policy
    game_id = game.casefold().strip()
    if game_id == "bfme2":
        if (
            source_policy is None
            or source_policy.game.casefold() != "bfme2"
            or source_policy.patch != "1.06"
        ):
            raise ValueError(
                "import-faction conversion requires a BFME2 1.06 policy-bound catalog"
            )
    elif game_id == "rotwk":
        # RotWK is admitted through data-driven faction discovery; the catalog
        # is built without a fixed source policy (source_policy is None).
        pass
    else:
        raise ValueError(
            f"import-faction conversion does not support game: {game!r}"
        )
    progress_emit("census", f"census playable faction: {faction}")
    spec = _faction_spec(catalog, faction)
    graph = census_playable_faction(
        catalog,
        player_template=spec[1],
        game=game,
        expected_side=spec[2],
        implicit_object_roots=implicit_object_roots(spec[1], game=game),
        source_null_mapped_image_textures=source_null_mapped_image_textures(
            spec[1], game=game
        ),
        source_null_command_sets=source_null_command_sets(spec[1], game=game),
        music_roots=music_roots(spec[1], game=game),
    )
    progress_emit("faction-convert", f"convert faction objects: {faction}")
    return build_faction_conversion(
        graph,
        spellbook_source_documents(effective_root),
        effective_root,
        catalog_identity_sha256=catalog.identity_sha256(),
        artifact_writer=artifact_writer,
        catalog=catalog,
        state_root=state_root,
        convert_jobs=convert_jobs,
        game=game,
    )


__all__ = [
    "COVERAGE_SCHEMA",
    "COVERAGE_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "build_faction_conversion",
    "build_faction_import_plan",
    "convert_faction_import",
    "coverage_digest_payload",
    "plan_faction_import",
]
