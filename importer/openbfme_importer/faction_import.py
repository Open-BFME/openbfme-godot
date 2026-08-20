"""Deterministic completeness planning and conversion for a BFME2 faction import."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import threading
import time
from typing import Callable, Mapping, Sequence

from .catalog import InstallCatalog
from .castle_behavior import (
    CastleBehaviorCompilerError,
    compile_castle_behavior_contract,
)
from .faction_census import census_playable_faction, resolve_playable_faction
from .faction_object_cache import (
    FactionObjectCache,
    compiler_identity_token,
    default_cache_root,
    durable_non_ini_assets_fingerprint,
    object_cache_key,
    policy_roots_fingerprint,
)
from .faction_plan_cache import (
    FactionPlanRowCache,
    default_plan_cache_root,
    plan_cache_disabled,
    plan_row_cache_key,
)
from .incremental_rebuild import (
    compiler_dependency_identity,
    document_closure_identity,
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
    _block_values,
    _banner_field_assignments,
    _command_slots,
    _effective_values,
    _first,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
    _ancestry,
    _tokens,
)
from .playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    compile_playable_unit_pack_recipe,
)
from .pack_recipe_catalog_identity import (
    PackRecipeCatalogIdentityError,
    assert_pack_recipe_catalog_identity,
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
from .retail_ability_fx_ingress import (
    build_ability_fx_closure,
    harvest_fx_ids,
    texture_index_for,
)
from .special_disguise_prerequisite import (
    build_special_disguise_prerequisite,
    special_disguise_visual_targets,
)
from .spellbook_pack_compiler import (
    SpellbookPackCompilerError,
    compile_spellbook_pack_recipe,
    compose_spellbook_runtime_document,
)
from .spellbook_visual_ingress import (
    SpellbookVisualIngressError,
    build_spellbook_visual_closures,
)


SCHEMA = "openbfme.faction-import-plan"
SCHEMA_VERSION = 0
COVERAGE_SCHEMA = "openbfme.faction-import-coverage"
COVERAGE_SCHEMA_VERSION = 0

# Run-only fields excluded from coverage aggregateSha256 (cold/warm/jobs stable).
_COVERAGE_EPHEMERAL_OBJECT_KEYS = frozenset({"cacheHit", "convertElapsedMs"})
_COVERAGE_EPHEMERAL_SUMMARY_KEYS = frozenset(
    {
        "cacheHits",
        "convertWorkers",
        "convertLoopMs",
        "objectElapsedMsTotal",
        "objectElapsedMsP50",
        "objectElapsedMsP95",
        "objectsPerSecond",
        "slowestObjects",
    }
)


def _descriptor_source_paths(descriptor: Mapping[str, object]) -> list[str] | None:
    """Extract the compiler's own source closure, or signal broad fallback."""

    sources = descriptor.get("sourceDocuments")
    if not isinstance(sources, list) or not sources:
        return None
    paths: list[str] = []
    for row in sources:
        if not isinstance(row, Mapping) or not isinstance(row.get("virtualPath"), str):
            return None
        paths.append(str(row["virtualPath"]))
    return sorted(set(paths), key=lambda value: (value.casefold(), value))


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
    "projectile": "projectiles convert inside their firing unit recipes",
    "object-inheritance": "inheritance-only base objects are not standalone content",
    "create-a-hero": "create-a-hero slots are engine-managed by the CAH editor flow",
}

# Plan-time exclusions are the families whose content is accounted for inside
# another row's descriptor.  Banner carriers are no longer excluded: they
# convert as playable-unit descriptors for sim spawn at BannerCarrierMinLevel.
# Spell book rows compile through the spellbook lane; retail-object-parser rows
# stay explicit converter gaps until their owning lane lands.
_PLAN_EXCLUDED_FAMILY_REASONS = {
    key: _EXCLUDED_FAMILY_REASONS[key]
    for key in ("projectile", "object-inheritance", "create-a-hero")
}


def _expand_layered_command_objects(
    object_ids: list[str],
    prepared: PlayableUnitCompilerInputs,
    census_command_button_ids: frozenset[str],
) -> None:
    """Reconcile a sealed census root set with layered CommandSet overrides.

    The census is an attested input, but an expansion layer can replace an
    inherited CommandSet slot (for example RotWK enabling MordorTavern).  Walk
    only from census-reachable objects and add exact Object targets from their
    effective layered command buttons.  This preserves the bounded faction
    closure while preventing a newly active producer from being omitted from
    the published slice.
    """

    pending = list(object_ids)
    seen = {value.casefold() for value in object_ids}
    while pending:
        source_id = pending.pop()
        source = prepared.objects.get(source_id.casefold())
        if source is None:
            continue
        try:
            lineage = _ancestry(prepared.objects, source)
        except PlayableUnitCompilerError:
            continue
        command_set_ids = {
            value.casefold(): value
            for value in (
                _first((row.value,))
                for row in _effective_values(lineage, "CommandSet")
            )
            if value
        }
        for command_set_id in command_set_ids.values():
            command_set = prepared.command_sets.get(command_set_id.casefold())
            if command_set is None:
                continue
            for _, command_id in _command_slots(command_set):
                # Census already accounted for commands active in the sealed
                # base graph. Only commands newly activated by the layered
                # CommandSet can extend this closure.
                if command_id.casefold() in census_command_button_ids:
                    continue
                button = prepared.command_buttons.get(command_id.casefold())
                if button is None:
                    continue
                for raw_target in _block_values(button, "Object"):
                    target_id = _first((raw_target,))
                    if not target_id:
                        continue
                    folded = target_id.casefold()
                    target = prepared.objects.get(folded)
                    if target is None or folded in seen:
                        continue
                    object_ids.append(target.name)
                    pending.append(target.name)
                    seen.add(folded)


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


def resolve_convert_worker_count(jobs: int | None) -> int:
    """Worker count for the plan and convert loops.

    Convert is CPU-bound (visual closure + compilers). Cap at 16 so a
    32-thread host does not thrash the object-cache disk and GIL-heavy JSON
    paths; override with ``OPENBFME_FACTION_CONVERT_JOBS``.
    """

    try:
        workers = int(
            jobs
            if jobs is not None
            else os.environ.get("OPENBFME_FACTION_CONVERT_JOBS", "0")
        )
    except ValueError:
        workers = 0
    if workers <= 0:
        cpu = os.cpu_count() or 4
        workers = max(1, min(16, cpu))
    return workers


def resolve_plan_worker_count(jobs: int | None) -> int:
    """Worker count for the plan loop. Serial by default — see the call site.

    Explicit ``jobs`` wins; otherwise ``OPENBFME_FACTION_PLAN_JOBS``; otherwise
    1. This is deliberately *not* the convert loop's default: planning is
    GIL-bound and measured slower with 16 threads than with one.
    """

    if jobs is not None:
        return max(1, int(jobs))
    try:
        workers = int(os.environ.get("OPENBFME_FACTION_PLAN_JOBS", "1"))
    except ValueError:
        workers = 1
    return max(1, workers)


def build_faction_import_plan(
    faction_graph: Mapping[str, object],
    documents: Mapping[str, bytes],
    *,
    catalog_identity_sha256: str,
    game: str = "bfme2",
    plan_jobs: int | None = None,
    row_cache: FactionPlanRowCache | None = None,
    row_cache_assets_fp: str = "",
    row_cache_graph_identity: str = "",
    document_hashes: Mapping[str, str] | None = None,
    full_corpus_closure: Mapping[str, object] | None = None,
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
    # The catalog census can name a base banner while the sealed layered
    # oracle overrides BannerCarriersAllowed to a layered ChildObject. Expand
    # that exact dependency edge before classifying rows so the carrier is
    # converted and packaged with its horde instead of quarantining the horde.
    layered_banner_targets: set[str] = set()
    if prepared is not None:
        raw_buttons = definitions.get("commandButtons", [])
        census_command_button_ids = frozenset(
            str(row["id"]).casefold()
            for row in raw_buttons
            if isinstance(row, Mapping)
            and isinstance(row.get("id"), str)
            and row["id"]
        ) if isinstance(raw_buttons, list) else frozenset()
        # Legacy/unit-test graphs without a button ledger cannot distinguish
        # a layered activation from unrelated authored commands, so preserve
        # their explicit object boundary.
        if census_command_button_ids:
            _expand_layered_command_objects(
                object_ids, prepared, census_command_button_ids
            )
        pending_ids = list(object_ids)
        seen_ids = {value.casefold() for value in object_ids}
        while pending_ids:
            source_id = pending_ids.pop()
            target = prepared.objects.get(source_id.casefold())
            if target is None:
                continue
            for assignment in _banner_field_assignments(
                _ancestry(prepared.objects, target), "BannerCarriersAllowed"
            ):
                for token in _tokens(assignment.value):
                    folded = token.casefold()
                    layered_banner_targets.add(folded)
                    if folded not in seen_ids and folded in prepared.objects:
                        canonical = prepared.objects[folded].name
                        object_ids.append(canonical)
                        pending_ids.append(canonical)
                        seen_ids.add(folded)
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
    engine_spawned_roles: dict[str, str] = {}
    if engine_spawned_roots:
        policy_roles = dict(implicit_object_roots(player_template, game=game))
        engine_spawned_roles = {
            object_id.casefold(): policy_roles[object_id]
            for object_id in engine_spawned_roots
            if object_id in policy_roles
        }
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
    horde_banner_targets.update(layered_banner_targets)

    # Key material for the durable plan-row cache. Every non-document input the
    # per-object body reads is folded in here: the structure policy roots and
    # the banner-carrier set (both graph- and compiler-derived), plus the
    # process-wide compiler identity token so any converter source edit misses.
    plan_policy_fp = policy_roots_fingerprint(
        spawned=engine_spawned_roots,
        spawned_roles=engine_spawned_roles,
        wall_templates=wall_template_roots,
        source_null_sets=(
            *source_null_sets,
            *(f"horde-banner:{value}" for value in sorted(horde_banner_targets)),
        ),
    )
    plan_compiler_token = compiler_identity_token() if row_cache is not None else ""

    def _plan_one(object_id: str) -> list[dict[str, object]]:
        objects: list[dict[str, object]] = []
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
            return objects
        kinds: tuple[str, ...] = ()
        source_path = ""
        parse_error: str | None = None
        try:
            kinds = playable_object_kind_of(prepared, object_id)
        except PlayableUnitCompilerError as exc:
            if object_id.casefold() in prepared.objects:
                family = "object-inheritance"
            else:
                graph_row = graph_rows.get(object_id.casefold(), {})
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
            return objects
        family = _family(kinds)
        if family == "structure":
            try:
                structure_descriptor = compile_playable_structure_descriptor(
                    object_id,
                    documents,
                    prepared=prepared,
                    engine_spawned_roots=engine_spawned_roots,
                    engine_spawned_roles=engine_spawned_roles,
                    wall_template_roots=wall_template_roots,
                    source_null_command_sets=source_null_sets,
                    game=game,
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
                        "sourceDocumentPaths": _descriptor_source_paths(
                            structure_descriptor
                        ),
                    }
                )
            return objects
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
                        "sourceDocumentPaths": _descriptor_source_paths(
                            spellbook_descriptor
                        ),
                    }
                )
            return objects
        try:
            descriptor = compile_playable_unit_descriptor(
                object_id,
                documents,
                faction_graph=faction_graph,
                prepared=prepared,
                game=game,
                engine_spawned_banner_carrier=(
                    object_id.casefold() in horde_banner_targets
                ),
            )
        except PlayableUnitCompilerError as exc:
            reason = str(exc)
            if family == "banner-member" or object_id.casefold() in horde_banner_targets:
                # Banner carriers (BANNER KindOf or ObjectReskin banner targets)
                # must convert as standalone units for level-gated horde spawn.
                # Fail as converter-gap when the descriptor cannot be built —
                # never silently exclude them from the convert set.
                family = "banner-member"
            if (
                family in {"unit-extension", "horde-extension", "hero-extension"}
                and "UNIT_BUILD" in reason
            ):
                objects.append(
                    {
                        "id": object_id,
                        "family": family,
                        "kindOf": list(kinds),
                        "status": "excluded",
                        "reason": (
                            "engine-managed reachable extension has no independent "
                            "UNIT_BUILD production surface"
                        ),
                    }
                )
            elif family in _PLAN_EXCLUDED_FAMILY_REASONS:
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
                        "reason": reason,
                    }
                )
        else:
            out_family = "playable-unit"
            if family == "banner-member" or object_id.casefold() in horde_banner_targets:
                out_family = "banner-carrier"
            objects.append(
                {
                    "id": object_id,
                    "family": out_family,
                    "category": descriptor["category"],
                    "kindOf": list(kinds),
                    "status": "descriptor-ready",
                    "descriptorSha256": descriptor["descriptorSha256"],
                    "sourceDocumentPaths": _descriptor_source_paths(descriptor),
                }
            )
        return objects

    # Planning compiles one descriptor per object — 167 s for the 61 Men
    # objects, the single largest stage of a faction convert. It can be run
    # across threads (results are collected in submission order, so the plan
    # document is unchanged, and these are the same compilers the convert loop
    # already runs concurrently against this shared ``prepared``), but MEASURED
    # ON THIS HOST IT IS A LOSS: the stage is pure-Python and GIL-bound, and 16
    # workers took ~240 s against ~167-215 s serial. So it stays serial unless
    # a caller or OPENBFME_FACTION_PLAN_JOBS asks otherwise. The real batch win
    # is elsewhere — one shared parsed corpus across all seven factions.
    ordered_ids = sorted(object_ids, key=lambda value: (value.casefold(), value))
    workers = resolve_plan_worker_count(plan_jobs)

    # Durable plan-row cache. Planning is a full second descriptor compile per
    # object, so a warm faction used to pay it in full; a cached row is only
    # admitted when every input byte-identity still matches (see
    # faction_plan_cache). Any doubt recomputes — a cache fault costs time,
    # never correctness.
    def _closure_identity(source_paths: list[str] | None) -> Mapping[str, object]:
        if not source_paths and full_corpus_closure is not None:
            return full_corpus_closure
        return document_closure_identity(
            documents, source_paths, document_hashes=document_hashes
        )

    def _row_source_paths(rows: Sequence[Mapping[str, object]]) -> list[str] | None:
        for row in rows:
            declared = row.get("sourceDocumentPaths")
            if isinstance(declared, list):
                return [str(item) for item in declared]
        return None

    def _plan_one_cached(object_id: str) -> list[dict[str, object]]:
        if row_cache is None:
            return _plan_one(object_id)
        key = plan_row_cache_key(
            object_id=object_id,
            documents_fp=str(_closure_identity(None)["sha256"]),
            catalog_identity_sha256=catalog_identity,
            effective_root_fp=row_cache_assets_fp,
            graph_identity_sha256=row_cache_graph_identity or graph_identity,
            policy_fp=plan_policy_fp,
            compiler_token=plan_compiler_token,
            game=game,
        )
        hit = row_cache.get(
            key,
            verify_compiler_identity=lambda family: str(
                compiler_dependency_identity(family)["sha256"]
            ),
            verify_document_closure=lambda paths: str(
                _closure_identity(paths)["sha256"]
            ),
        )
        if hit is not None:
            return hit
        rows_out = _plan_one(object_id)
        try:
            family = str(rows_out[0]["family"]) if rows_out else ""
            declared = _row_source_paths(rows_out)
            row_cache.put(
                key,
                rows=rows_out,
                family=family,
                compiler_identity=str(
                    compiler_dependency_identity(family)["sha256"]
                ),
                document_closure_sha256=str(_closure_identity(declared)["sha256"]),
                source_paths=declared,
            )
        except (KeyError, IndexError, TypeError, ValueError):
            # Never let a caching problem change what planning returned.
            pass
        return rows_out

    objects: list[dict[str, object]] = []
    if workers <= 1 or len(ordered_ids) <= 1:
        planned = [_plan_one_cached(object_id) for object_id in ordered_ids]
    else:
        with ThreadPoolExecutor(
            max_workers=min(workers, len(ordered_ids))
        ) as pool:
            planned = list(pool.map(_plan_one_cached, ordered_ids))
    for chunk in planned:
        objects.extend(chunk)

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
        spellbook_source_documents(effective_root, catalog=catalog),
        catalog_identity_sha256=catalog.identity_sha256(),
        game=game,
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


def _ability_fx_closure(
    descriptor: Mapping[str, object],
    documents: Mapping[str, bytes],
    effective_root: Path,
    assets_fp: str,
    namespace: str,
) -> dict[str, object] | None:
    """Seal the authored ability/power FX closure for one compiled descriptor.

    Returns ``None`` only when the descriptor authored no FXList at all, so a
    unit without abilities keeps exactly its previous recipe bytes.  Ids that
    do not resolve are recorded inside the closure as unresolved rows, never
    substituted; a corpus-level failure raises and turns the object into an
    explicit converter-gap row rather than shipping invented art.
    """

    fx_ids = harvest_fx_ids(descriptor)
    particle_ids = _draw_particle_system_ids(descriptor)
    if not fx_ids and not particle_ids:
        return None
    return build_ability_fx_closure(
        documents,
        fx_ids,
        namespace=namespace,
        texture_index=texture_index_for(effective_root, assets_fp),
        particle_ids=particle_ids,
    )


def _spellbook_visual_row(recipe: Mapping[str, object]) -> dict[str, object]:
    """Surface the effect-geometry outcome on the coverage row.

    Operators need to see, without opening the recipe, how many effect objects
    got real converted geometry, how many retail authors invisible, and how many
    still have no converted model — the last number is the honest gap list.
    """

    registration = recipe.get("runtimeRegistration")
    if not isinstance(registration, Mapping):
        return {}
    bindings = registration.get("visualBindings")
    if not isinstance(bindings, Mapping):
        return {}
    summary = bindings.get("summary")
    if not isinstance(summary, Mapping):
        return {}
    return {"effectVisuals": dict(summary)}


def _draw_particle_system_ids(descriptor: Mapping[str, object]) -> list[str]:
    """Return every ParticleSysBone system the descriptor's leaves author.

    Some effect objects have no model at all: their whole appearance is a
    Draw-module bone particle system (CloudBreakSunbeam -> ``CloudBreakRays``,
    ElvenGrove -> ``TaintHCPing``).  Those ids never appear in an FXList, so
    they have to be seeded into the FX closure explicitly or the object
    converts with nothing to draw.
    """

    found: list[str] = []

    def walk(value: object) -> None:
        if isinstance(value, Mapping):
            for key, item in value.items():
                folded = str(key).casefold()
                if folded == "unresolvedparticlesysbones":
                    # Authored references with no definition behind them. They
                    # are kept as evidence on the leaf, never seeded into the
                    # closure — seeding one would ask the FX lane to convert a
                    # system that does not exist.
                    continue
                if folded == "particlesystem" and isinstance(item, str):
                    if item.strip():
                        found.append(item.strip())
                else:
                    walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)

    walk(descriptor.get("leaves"))
    return sorted(set(found), key=lambda value: (value.casefold(), value))


def _convert_one_plan_object(
    plan_row: Mapping[str, object],
    *,
    documents: Mapping[str, bytes],
    prepared: PlayableUnitCompilerInputs,
    faction_graph: Mapping[str, object],
    faction: str = "",
    effective_root: Path,
    catalog: InstallCatalog | None,
    spawned: tuple[str, ...],
    spawned_roles: Mapping[str, str] | None = None,
    wall_templates: tuple[str, ...],
    source_null_sets: tuple[str, ...],
    object_cache: FactionObjectCache | None,
    catalog_identity_sha256: str,
    assets_fp: str,
    policy_fp: str,
    graph_identity_sha256: str = "",
    numeric_defines_sha256: str = "",
    document_hashes: Mapping[str, str] | None = None,
    full_corpus_closure: Mapping[str, object] | None = None,
    game: str = "bfme2",
) -> tuple[dict[str, object], dict[str, Mapping[str, object]]]:
    """Convert one plan row; returns (coverage_row, artifacts)."""

    import time as _time

    started = _time.perf_counter()
    object_id = str(plan_row["id"])
    family = str(plan_row["family"])
    status = str(plan_row["status"])
    row: dict[str, object] = {"id": object_id, "family": family}
    artifacts: dict[str, Mapping[str, object]] = {}

    source_paths = plan_row.get("sourceDocumentPaths")
    declared_sources = source_paths if isinstance(source_paths, list) else None
    if not declared_sources and full_corpus_closure is not None:
        # A row without a declared source closure hashes the whole corpus, and
        # that answer is identical for every such row in the batch. Reuse the
        # one computed up front instead of rebuilding it per object.
        closure_identity: Mapping[str, object] = full_corpus_closure
    else:
        closure_identity = document_closure_identity(
            documents,
            declared_sources,
            document_hashes=document_hashes,
        )
    compiler_identity = compiler_dependency_identity(family)
    documents_fp = str(closure_identity["sha256"])
    compiler_token = str(compiler_identity["sha256"])
    row["incremental"] = {
        "dependencyMode": closure_identity["mode"],
        "sourceDocuments": closure_identity["sourceDocuments"],
        "compilerMode": compiler_identity["mode"],
        "compilerIdentity": compiler_token,
    }

    plan_reason = str(plan_row.get("reason", ""))
    if (
        status == "converter-gap"
        and family in {"unit-extension", "horde-extension", "hero-extension"}
        and "UNIT_BUILD" in plan_reason
    ):
        row.update(
            {
                "status": "excluded",
                "reason": (
                    "engine-managed reachable extension has no independent "
                    "UNIT_BUILD production surface"
                ),
                "convertElapsedMs": int((_time.perf_counter() - started) * 1000),
            }
        )
        return row, artifacts

    plan_descriptor = plan_row.get("descriptorSha256")
    cache_key = object_cache_key(
        family=family,
        object_id=object_id,
        documents_fp=documents_fp,
        catalog_identity_sha256=catalog_identity_sha256,
        effective_root_fp=assets_fp,
        # The graph directly supplies mapped-image rows to structure recipes
        # and runtimes. Keep the whole graph as a fail-closed component until
        # every consumed row has a proven per-object projection.
        graph_identity_sha256=graph_identity_sha256,
        plan_aggregate_sha256="",
        policy_fp=policy_fp,
        compiler_token=compiler_token,
        # Structures consume prepared.numeric_defines even though their
        # descriptor sourceDocuments do not currently include every defining
        # INI (notably gamedata.ini).
        numeric_defines_sha256=numeric_defines_sha256,
        plan_descriptor_sha256=(
            str(plan_descriptor) if isinstance(plan_descriptor, str) else ""
        ),
        extra={"plan_status": status, "game": game.casefold().strip()},
    )
    row["incremental"]["cacheKey"] = cache_key  # type: ignore[index]
    if object_cache is not None and (
        family in {"structure", "spellbook"} or status == "descriptor-ready"
    ):
        hit = object_cache.get(cache_key)
        if hit is not None:
            cached_row = dict(hit["row"])
            cached_row["cacheHit"] = True
            cached_row["convertElapsedMs"] = int(
                (_time.perf_counter() - started) * 1000
            )
            return cached_row, dict(hit["artifacts"])

    if family == "structure":
        descriptor = None
        try:
            descriptor = compile_playable_structure_descriptor(
                object_id,
                documents,
                prepared=prepared,
                engine_spawned_roots=spawned,
                engine_spawned_roles=spawned_roles,
                wall_template_roots=wall_templates,
                source_null_command_sets=source_null_sets,
                game=game,
            )
            kinds = {
                str(item)
                for item in (descriptor.get("kindOf") or [])
                if isinstance(item, str)
            }
            # Only the fortress center-generic plot composite is a pure
            # BASE_FOUNDATION pad with no lifecycle presentation. Other
            # BASE_FOUNDATION-tagged objects (fortress, expansion pads) still
            # convert as real structures — do not over-exclude on KindOf alone.
            is_center_generic_foundation = (
                "BASE_FOUNDATION" in kinds
                and object_id.casefold().endswith("fortresscentergeneric")
            )
            if is_center_generic_foundation:
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
                closure_kwargs = (
                    {
                        "catalog": catalog,
                        "catalog_identity_sha256": catalog_identity_sha256,
                    }
                    if catalog is not None
                    else {}
                )
                closure = build_retail_visual_closure(
                    effective_root, [object_id], **closure_kwargs
                )
                images, image_gaps = _resolved_structure_images(
                    faction_graph, descriptor
                )
                recipe = compile_structure_visual_recipe(
                    object_id,
                    closure,
                    resolved_images=images,
                    image_binding_gaps=image_gaps,
                )
                assert_pack_recipe_catalog_identity(
                    recipe, catalog, object_id=object_id
                )
                evidence = compile_structure_lifecycle_evidence(
                    object_id, documents, prepared=prepared
                )
                runtime = compose_structure_runtime_document(
                    descriptor, recipe, evidence
                )
                castle_contract = (
                    compile_castle_behavior_contract(evidence, faction, effective_root)
                    if evidence.get("schema")
                    == "openbfme.playable-structure-lifecycle-evidence"
                    else None
                )
                if castle_contract is not None:
                    registration = runtime.get("registration")
                    gameplay = (
                        registration.get("gameplay")
                        if isinstance(registration, dict)
                        else None
                    )
                    if not isinstance(gameplay, dict):
                        raise CastleBehaviorCompilerError(
                            "structure runtime gameplay registration is malformed"
                        )
                    gameplay["castleBehavior"] = castle_contract
                    runtime.pop("runtimeSha256", None)
                    runtime["runtimeSha256"] = hashlib.sha256(
                        json.dumps(
                            runtime,
                            sort_keys=True,
                            separators=(",", ":"),
                            ensure_ascii=False,
                            allow_nan=False,
                        ).encode("utf-8")
                    ).hexdigest()
        except (
            CastleBehaviorCompilerError,
            PlayableStructureCompilerError,
            PlayableStructurePackCompilerError,
            PackRecipeCatalogIdentityError,
            ValueError,
        ) as exc:
            if (
                isinstance(descriptor, Mapping)
                and "no resolved lifecycle model" in str(exc)
                and "BASE_FOUNDATION" in {
                    str(item)
                    for item in (descriptor.get("kindOf") or [])
                    if isinstance(item, str)
                }
                # Only the fortress center-generic plot composite may
                # take this exclusion (the primary path above already
                # handles it before the visual closure runs). Any other
                # BASE_FOUNDATION object (fortress, expansion pads)
                # converts as a real structure, so a "no resolved
                # lifecycle model" failure there is a cook/converter
                # failure and must stay a loud converter-gap with the
                # real reason - never be re-marked as a content
                # exclusion, which once masked a broken toolchain's pad
                # cook failures as "excluded" and failed the fortress
                # closure with the wrong diagnosis.
                and object_id.casefold().endswith("fortresscentergeneric")
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
            if row.get("status") != "excluded":
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
                        "productionEvidence": str(
                            descriptor["production"]["evidence"]
                        ),
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
            layered_media_sources: tuple[bytes, ...] = ()
            layered_virtual_paths: tuple[str, ...] = ()
            layered_audio_source: bytes | None = None
            if (
                faction_graph.get("spellbookDefinitionAuthority")
                == "layered-effective-assets"
                and catalog is not None
            ):
                mapped_root = effective_root / "data" / "ini" / "mappedimages"
                layered_media_sources = tuple(
                    path.read_bytes()
                    for path in sorted(mapped_root.rglob("*.ini"))
                    if path.is_file()
                )
                layered_virtual_paths = tuple(entry.name for entry in catalog.entries)
                # Spellbook Initiate/FX audio roots live in soundeffects.ini.
                # Do not concatenate unrelated voice/music namespaces here:
                # the layered tree retains duplicate voice definitions which
                # are irrelevant to this exact closure and correctly rejected
                # by the shared parser.
                layered_audio_source = (
                    effective_root / "data/ini/soundeffects.ini"
                ).read_bytes()
            images, audio = _resolved_spellbook_media(
                faction_graph,
                draft,
                fallback_mapped_image_sources=layered_media_sources,
                fallback_virtual_paths=layered_virtual_paths,
                fallback_audio_source=layered_audio_source,
            )
            strings = (
                _resolved_spellbook_strings(catalog, draft, graph=faction_graph)
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
            # Effect geometry: every model the spellbook's leaf objects author
            # (summoned units, groves, trees, dragons) converts through the same
            # generic W3D stack the unit lane uses.  Without this the runtime
            # has no art binding for a summoned object at all and falls back to
            # the synthetic kit mesh — the "blue units" symptom.
            visual_closures, visual_failures = build_spellbook_visual_closures(
                descriptor,
                effective_root,
                catalog=catalog,
                catalog_identity_sha256=catalog_identity_sha256,
            )
            recipe = compile_spellbook_pack_recipe(
                descriptor,
                _ability_fx_closure(
                    descriptor,
                    documents,
                    effective_root,
                    assets_fp,
                    str(descriptor["spellBook"]["objectId"]),  # type: ignore[index]
                ),
                visual_closures=visual_closures,
                visual_closure_failures=visual_failures,
            )
            assert_pack_recipe_catalog_identity(
                recipe, catalog, object_id=object_id
            )
            runtime = compose_spellbook_runtime_document(descriptor, recipe)
        except (
            SpellbookCompilerError,
            SpellbookPackCompilerError,
            SpellbookVisualIngressError,
            PackRecipeCatalogIdentityError,
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
                    **_spellbook_visual_row(recipe),
                }
            )
    elif status == "descriptor-ready":
        engine_spawned_banner_carrier = str(row.get("family", "")) == "banner-carrier"
        try:
            draft = compile_playable_unit_descriptor(
                object_id,
                documents,
                faction_graph=faction_graph,
                prepared=prepared,
                game=game,
                engine_spawned_banner_carrier=engine_spawned_banner_carrier,
            )
            gameplay = draft.get("gameplay")
            banner = (
                gameplay.get("bannerCarrier")
                if isinstance(gameplay, Mapping)
                else None
            )
            if isinstance(banner, Mapping):
                known_objects = set(prepared.objects)
                missing_banners = [
                    str(target)
                    for target in banner.get("allowedObjectIds", [])
                    if str(target).casefold() not in known_objects
                ]
                if missing_banners:
                    raise PlayableUnitCompilerError(
                        f"unit {object_id} banner carrier target is outside the "
                        f"faction census: {missing_banners[0]}"
                    )
            images, audio = _resolved_media(
                faction_graph,
                draft,
                effective_root=effective_root,
                catalog=catalog,
            )
            strings = (
                _resolved_strings(catalog, draft, graph=faction_graph)
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
                game=game,
                engine_spawned_banner_carrier=engine_spawned_banner_carrier,
            )
            composition = descriptor["composition"]
            assert isinstance(composition, Mapping)
            targets = {str(composition["containerObjectId"])}
            targets.update(
                str(member["objectId"]) for member in composition["members"]
            )
            disguise_targets = special_disguise_visual_targets(
                descriptor, documents
            )
            targets.update(disguise_targets)
            closure_kwargs = (
                {
                    "catalog": catalog,
                    "catalog_identity_sha256": catalog_identity_sha256,
                }
                if catalog is not None
                else {}
            )
            closure = build_retail_visual_closure(
                effective_root,
                sorted(targets, key=str.casefold),
                **closure_kwargs,
            )
            special_disguise = build_special_disguise_prerequisite(
                descriptor,
                documents,
                closure,
                game=game,
                texture_index=(
                    texture_index_for(effective_root, assets_fp)
                    if disguise_targets
                    else {}
                ),
                effective_root=effective_root,
            )
            recipe = compile_playable_unit_pack_recipe(
                descriptor,
                closure,
                _ability_fx_closure(
                    descriptor,
                    documents,
                    effective_root,
                    assets_fp,
                    str(descriptor["objectId"]),
                ),
                special_disguise,
            )
            assert_pack_recipe_catalog_identity(
                recipe, catalog, object_id=object_id
            )
        except (
            PlayableUnitCompilerError,
            PlayableUnitPackCompilerError,
            PackRecipeCatalogIdentityError,
            ValueError,
        ) as exc:
            reason = str(exc)
            if (
                str(row.get("family", ""))
                in {"unit-extension", "horde-extension", "hero-extension"}
                and "UNIT_BUILD" in reason
            ):
                row.update(
                    {
                        "status": "excluded",
                        "reason": (
                            "engine-managed reachable extension has no independent "
                            "UNIT_BUILD production surface"
                        ),
                    }
                )
            else:
                row.update({"status": "converter-gap", "reason": reason})
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
    row["convertElapsedMs"] = int((_time.perf_counter() - started) * 1000)
    return row, artifacts


def _resolve_plan_row_cache(
    state_root: Path | None, effective_root: Path
) -> FactionPlanRowCache | None:
    """Durable plan-row cache root, or ``None`` when no durable root is known.

    Mirrors the object cache's rule exactly: never fall back to the working
    directory, and never let the ambient ``OPENBFME_IMPORT_ROOT`` of the pytest
    gate point a synthetic conversion at durable retail entries.
    """

    if plan_cache_disabled():
        return None
    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    import_root = os.environ.get("OPENBFME_IMPORT_ROOT", "").strip()
    try:
        if state_root is not None:
            return FactionPlanRowCache(default_plan_cache_root(Path(state_root)))
        if shared:
            return FactionPlanRowCache(default_plan_cache_root(Path(shared)))
        if import_root:
            resolved_import_root = Path(import_root).expanduser().resolve()
            resolved_effective_root = Path(effective_root).expanduser().resolve()
            if resolved_effective_root.is_relative_to(resolved_import_root):
                return FactionPlanRowCache(
                    default_plan_cache_root(resolved_import_root)
                )
    except OSError:
        return None
    return None


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

    # Identity material the plan-row cache keys on. Computed before the plan so
    # the plan stage can consult its durable cache; the convert loop below
    # reuses the very same values, so this is a move, not a new cost.
    document_hashes = {
        path: hashlib.sha256(payload).hexdigest() for path, payload in documents.items()
    }
    # Rows with no declared source closure all hash the same full corpus.
    full_corpus_closure = document_closure_identity(
        documents, None, document_hashes=document_hashes
    )
    # Keep every manifest row outside data/ini broad. Rows inside data/ini also
    # have compiler-authored closures, while the catalog identity remains the
    # safety backstop until those hand-curated closures are complete.
    assets_fp = durable_non_ini_assets_fingerprint(effective_root)
    graph_identity_sha256 = hashlib.sha256(
        json.dumps(
            faction_graph,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    plan_row_cache = _resolve_plan_row_cache(state_root, effective_root)

    progress_emit("faction-plan", "building faction import plan")
    plan = build_faction_import_plan(
        faction_graph,
        documents,
        catalog_identity_sha256=catalog_identity_sha256,
        game=game,
        row_cache=plan_row_cache,
        row_cache_assets_fp=assets_fp,
        row_cache_graph_identity=graph_identity_sha256,
        document_hashes=document_hashes,
        full_corpus_closure=full_corpus_closure,
    )
    if plan_row_cache is not None:
        # A refused entry is recompiled exactly like an absent one, so report
        # the recompiled total rather than letting "refused" read as "skipped".
        recompiled = plan_row_cache.misses + plan_row_cache.refusals
        progress_emit(
            "faction-plan",
            f"plan rows: {plan_row_cache.hits} cached, "
            f"{recompiled} recompiled "
            f"({plan_row_cache.misses} absent, "
            f"{plan_row_cache.refusals} refused on identity)",
            extra={
                "planRowCacheHits": plan_row_cache.hits,
                "planRowCacheMisses": plan_row_cache.misses,
                "planRowCacheRefusals": plan_row_cache.refusals,
                "planRowCacheRecompiled": recompiled,
            },
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
    faction = str(target["faction"])
    spawned_policy = implicit_object_roots(template, game=game)
    spawned = tuple(object_id for object_id, _reason in spawned_policy)
    spawned_roles = {
        object_id.casefold(): reason for object_id, reason in spawned_policy
    }
    wall_templates = _wall_template_roots(faction_graph)
    source_null_sets = _source_null_command_set_ids(faction_graph)

    plan_objects = list(plan["objects"])
    numeric_defines_sha256 = (
        hashlib.sha256(
            json.dumps(
                prepared.numeric_defines,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=False,
                allow_nan=False,
            ).encode("utf-8")
        ).hexdigest()
        if prepared is not None
        else hashlib.sha256(str(preparation_error).encode("utf-8")).hexdigest()
    )
    policy_fp = policy_roots_fingerprint(
        spawned=spawned,
        spawned_roles=spawned_roles,
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
        elif shared:
            object_cache = FactionObjectCache(
                default_cache_root(Path(shared))
            )
        elif import_root:
            # OPENBFME_IMPORT_ROOT is also exported by the repository-wide
            # pytest gate.  Do not let that ambient setting make synthetic or
            # mocked conversions consume durable retail cache entries.  The
            # implicit cache is valid only for an effective-assets tree owned
            # by that import root; CLI production calls pass state_root
            # explicitly and therefore take the branch above.
            resolved_import_root = Path(import_root).expanduser().resolve()
            resolved_effective_root = effective_root.expanduser().resolve()
            if resolved_effective_root.is_relative_to(resolved_import_root):
                object_cache = FactionObjectCache(
                    default_cache_root(resolved_import_root)
                )

    workers = resolve_convert_worker_count(convert_jobs)

    progress_emit(
        "faction-convert",
        f"converting {len(plan_objects)} objects ({workers} workers"
        f"{', cache on' if object_cache else ', cache off'})",
        total_units=len(plan_objects),
        extra={
            "objectCount": len(plan_objects),
            "workers": workers,
            "objectCache": bool(object_cache),
            "catalogFilter": catalog is not None,
        },
    )

    writer_lock = threading.Lock()
    results: list[dict[str, object] | None] = [None] * len(plan_objects)

    def _work(index: int, plan_row: Mapping[str, object]) -> tuple[int, dict[str, object]]:
        assert isinstance(plan_row, Mapping)
        work_started = time.perf_counter()
        try:
            row, artifacts = _convert_one_plan_object(
                plan_row,
                documents=documents,
                prepared=prepared,
                faction_graph=faction_graph,
                faction=faction,
                effective_root=effective_root,
                catalog=catalog,
                spawned=spawned,
                spawned_roles=spawned_roles,
                wall_templates=wall_templates,
                source_null_sets=source_null_sets,
                object_cache=object_cache,
                catalog_identity_sha256=catalog_identity_sha256,
                assets_fp=assets_fp,
                policy_fp=policy_fp,
                graph_identity_sha256=graph_identity_sha256,
                numeric_defines_sha256=numeric_defines_sha256,
                document_hashes=document_hashes,
                full_corpus_closure=full_corpus_closure,
                game=game,
            )
        except Exception as exc:  # noqa: BLE001 — fail-closed per object, not batch
            object_id = str(plan_row.get("id", f"index-{index}"))
            family = str(plan_row.get("family", "unknown"))
            row = {
                "id": object_id,
                "family": family,
                "status": "converter-gap",
                "reason": f"unexpected convert error: {type(exc).__name__}: {exc}",
                "convertElapsedMs": int((time.perf_counter() - work_started) * 1000),
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
    convert_loop_started = time.perf_counter()

    def _emit_object_done(row: Mapping[str, object]) -> None:
        elapsed_ms = int(row.get("convertElapsedMs") or 0)
        progress_emit(
            "",
            f"done {row.get('family')}: {row.get('id')} ({row.get('status')}"
            f"{', cache' if row.get('cacheHit') else ''}"
            f", {elapsed_ms}ms)",
            unit_delta=1,
            extra={
                "objectId": row.get("id"),
                "family": row.get("family"),
                "status": row.get("status"),
                "cacheHit": bool(row.get("cacheHit")),
                "convertElapsedMs": elapsed_ms,
            },
        )

    if workers == 1 or len(plan_objects) <= 1:
        for index, plan_row in enumerate(plan_objects):
            assert isinstance(plan_row, Mapping)
            idx, row = _work(index, plan_row)
            results[idx] = row
            _emit_object_done(row)
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
                _emit_object_done(row)

    convert_loop_ms = int((time.perf_counter() - convert_loop_started) * 1000)
    rows = [row for row in results if isinstance(row, dict)]

    counts = {
        key: sum(row["status"] == key for row in rows)
        for key in ("converted", "excluded", "converter-gap")
    }
    plan_summary = plan["summary"]
    assert isinstance(plan_summary, Mapping)
    unresolved = int(plan_summary["unresolvedLeafCount"])
    cache_hits = sum(1 for row in rows if row.get("cacheHit"))
    object_ms = [
        int(row["convertElapsedMs"])
        for row in rows
        if isinstance(row.get("convertElapsedMs"), int)
    ]
    object_ms_sorted = sorted(object_ms)
    p50 = object_ms_sorted[len(object_ms_sorted) // 2] if object_ms_sorted else 0
    p95 = (
        object_ms_sorted[max(0, int(len(object_ms_sorted) * 0.95) - 1)]
        if object_ms_sorted
        else 0
    )
    slowest = sorted(
        (
            {
                "id": row.get("id"),
                "family": row.get("family"),
                "status": row.get("status"),
                "convertElapsedMs": int(row.get("convertElapsedMs") or 0),
                "cacheHit": bool(row.get("cacheHit")),
            }
            for row in rows
        ),
        key=lambda item: int(item["convertElapsedMs"]),
        reverse=True,
    )[:10]
    coverage: dict[str, object] = {
        "schema": COVERAGE_SCHEMA,
        "schemaVersion": COVERAGE_SCHEMA_VERSION,
        "target": dict(target),
        # The compiler token is recorded on the *coverage* report and not on
        # the plan: the plan describes what retail authors, which no compiler
        # change can move, while coverage describes descriptors this compiler
        # emitted. Publication binds against it so a clean-but-stale report
        # cannot authorise a cook - six faction packs shipped 20 dead unit
        # buttons behind exactly such a report (see publish_gate).
        "inputs": {**dict(plan["inputs"]), "compilerIdentityToken": compiler_token},
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
            # Object conversion is complete when every planned object is
            # converted or excluded. Census leaves that retail authors but does
            # not ship (placeholder UI textures with source-null policy, voice
            # samples missing from englishaudio.big, dead wall-hub object
            # names) remain visible in unresolvedLeafCount without blocking
            # the converter-gap bar.
            "conversionComplete": counts["converter-gap"] == 0,
            "censusLeafCoverageComplete": unresolved == 0,
            "blockingReason": "conversion artifacts lack a pack/runtime receipt",
            "cacheHits": cache_hits,
            "convertWorkers": workers,
            "convertLoopMs": convert_loop_ms,
            "objectElapsedMsTotal": sum(object_ms),
            "objectElapsedMsP50": p50,
            "objectElapsedMsP95": p95,
            "objectsPerSecond": (
                round(len(rows) / (convert_loop_ms / 1000.0), 2)
                if convert_loop_ms > 0
                else None
            ),
            "slowestObjects": slowest,
        },
    }
    # Ephemeral run metadata stays in the document for operators but is not
    # content identity (compose_faction_profile / profile receipts bind this).
    coverage["aggregateSha256"] = hashlib.sha256(
        _canonical_bytes(coverage_digest_payload(coverage))
    ).hexdigest()
    return coverage


def load_retail_string_catalog(catalog: InstallCatalog):
    """Parse the ``data/lotr.str`` this catalog resolves, or ``None``.

    This is the ONE retail string loader on the import path: unit conversion
    (``convert_faction_import`` above) and the faction-slice compose strings
    lane (``faction_slice_profile``) both consume it, so the tier the compiler
    records ``sourceNullStringIds`` evidence against and the tier the published
    strings document resolves against can never drift apart. The catalog's own
    layering decides which table wins (a RotWK layered install replaces the
    BFME2 table wholesale), which is exactly the tier policy
    ``game/tests/hud_string_completeness_runner.gd`` pins.

    Parsed with ``first-wins`` / ``strict=False``: RotWK 2.01 retail ships
    lotr.str with a bounded lexical typo (a label containing a space), and the
    census parse records malformed rows as evidence instead of failing the
    whole catalog.
    """

    from .sage_string import MAX_STRING_BYTES, parse_string_catalog

    string_entry = catalog.resolve_exact("data/lotr.str")
    if string_entry is None:
        return None
    string_source = catalog.open_archive_for(string_entry).read_entry(
        catalog.as_entry(string_entry), max_bytes=MAX_STRING_BYTES
    )
    return parse_string_catalog(
        string_source, duplicate_policy="first-wins", strict=False
    )


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
    # The owner-selected RotWK oracle is the layered effective tree itself.
    # Passing the synthetic install catalog here used to replace those bytes
    # with catalog winners, so an explicit layered assets root still emitted
    # non-layered leaves (AngmarOrcWarriors_Summoned source line 1796). Keep
    # the catalog for census identity/visual archive access, but do not let it
    # override the canonical INI document view.
    layered_authority = (
        game_id == "rotwk"
        and effective_root.name.casefold() == "layered-effective-assets"
    )
    documents = spellbook_source_documents(effective_root, catalog=catalog)
    if layered_authority:
        # The sealed layered tree contains the owner-selected gameplay oracle.
        # Its command sets, command buttons, player templates and system
        # objects are one coherent definition graph; mixing any catalog copy
        # back in leaves ordinary retail references unresolved (for example
        # MenSentryTowerCommandSet and SpellBookArrowVolleyDamagerEgg). Census
        # identity still comes from the installed catalog, while compilation
        # consumes the sealed layered document view in full.
        layered_documents = spellbook_source_documents(
            effective_root, catalog=None
        )
        documents = layered_documents
        graph["layeredDocumentAuthority"] = "layered-effective-assets"

        # Census discovers reachability through the installed catalog, but a
        # fully layered command surface may reference additional MappedImage
        # definitions. Bind those definitions from the same layered oracle to
        # the installed texture archive instead of misclassifying the images
        # as absent (the HeroUI atlases are present in retail).
        from .faction_census import _effective_entries
        from .mapped_image import (
            MappedImageRecord,
            _parse_mapped_images,
            resolve_mapped_image_texture_paths_partial,
        )

        layered_mapped_images: dict[str, list[MappedImageRecord]] = {}
        for path, source in sorted(documents.items(), key=lambda item: item[0].casefold()):
            normalized = path.replace("\\", "/").casefold()
            if not normalized.startswith("data/ini/mappedimages/"):
                continue
            for record in _parse_mapped_images(source, reject_duplicate_ids=False):
                key = record.id.casefold()
                layered_mapped_images.setdefault(key, []).append(record)
        if layered_mapped_images:
            resolved_leaves = graph.get("resolvedLeaves")
            existing_rows = (
                resolved_leaves.get("mappedImages", [])
                if isinstance(resolved_leaves, dict)
                else []
            )
            existing_resolved_keys = {
                str(row.get("id", "")).casefold()
                for row in existing_rows
                if isinstance(row, dict)
                and isinstance(row.get("compiledTextureVirtualPath"), str)
            }
            records: tuple[MappedImageRecord, ...] = tuple(
                candidates[0]
                for key, candidates in sorted(layered_mapped_images.items())
                if key not in existing_resolved_keys
                and len(
                    {
                        repr(candidate.neutral())
                        for candidate in candidates
                    }
                )
                == 1
            )
            rebound_keys = {record.id.casefold() for record in records}
            texture_paths, _missing_textures = (
                resolve_mapped_image_texture_paths_partial(
                    records,
                    [entry.name for entry in _effective_entries(catalog).values()],
                )
            )
            paths_by_texture = {
                key.casefold(): value for key, value in texture_paths.items()
            }
            rows: list[dict[str, object]] = [
                dict(row)
                for row in existing_rows
                if isinstance(row, dict)
                and str(row.get("id", "")).casefold() not in rebound_keys
            ]
            for record in sorted(records, key=lambda item: item.id.casefold()):
                row = record.neutral()
                texture_path = paths_by_texture.get(record.texture.casefold())
                if texture_path is not None:
                    row["compiledTextureVirtualPath"] = texture_path
                else:
                    row["compiledTextureResolution"] = "missing"
                rows.append(row)
            if isinstance(resolved_leaves, dict):
                resolved_leaves["mappedImages"] = sorted(
                    rows, key=lambda row: str(row.get("id", "")).casefold()
                )
                graph["mappedImageDefinitionAuthority"] = (
                    "layered-effective-assets"
                )

        # The layered command surface also contains expansion/community rows
        # whose label ids are absent from retail lotr.str. Record the exact ids
        # as source-null presentation leaves so unit conversion can preserve
        # the authored command without inventing replacement text.
        from .sage_cst import parse_sage_document

        string_catalog = load_retail_string_catalog(catalog)
        if string_catalog is not None:
            missing_layered_text_ids: set[str] = set()
            for path, source in documents.items():
                if path.replace("\\", "/").casefold() != "data/ini/commandbutton.ini":
                    continue
                for block in parse_sage_document(source, path).objects:
                    if block.kind.casefold() != "commandbutton":
                        continue
                    for assignment in block.assignments:
                        if assignment.key.casefold() not in {
                            "textlabel",
                            "descriptlabel",
                        }:
                            continue
                        identifier = assignment.value.split(None, 1)[0].strip()
                        if identifier and string_catalog.record(identifier) is None:
                            missing_layered_text_ids.add(identifier)
            if missing_layered_text_ids:
                graph["layeredSourceNullTextIds"] = sorted(
                    missing_layered_text_ids, key=str.casefold
                )

        # The census still supplies retail reachability/identity from the
        # installed faction, but definition digests for those reachable rows
        # must bind to the same layered gameplay documents the compiler reads.
        from .spellbook_compiler import _gameplay_digest, _unique_blocks

        definitions = graph.get("definitions", {})
        if isinstance(definitions, dict):
            for family, kind, path in (
                ("sciences", "Science", "data/ini/science.ini"),
                ("specialPowers", "SpecialPower", "data/ini/specialpower.ini"),
                ("upgrades", "Upgrade", "data/ini/upgrade.ini"),
            ):
                source = documents.get(path)
                if source is None:
                    continue
                blocks = _unique_blocks(source, kind, path)
                # Layered prerequisite chains can reach shared definitions
                # which the non-layered faction census did not enumerate.
                # Bind the definition ledger to the complete layered source;
                # command/store reachability remains the installed surface.
                definitions[family] = [
                    {
                        "id": block.name,
                        "definitionSha256": _gameplay_digest(block),
                    }
                    for block in sorted(
                        blocks.values(), key=lambda item: item.name.casefold()
                    )
                ]
            graph["spellbookDefinitionAuthority"] = "layered-effective-assets"
    return build_faction_conversion(
        graph,
        documents,
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
    "load_retail_string_catalog",
    "plan_faction_import",
]
