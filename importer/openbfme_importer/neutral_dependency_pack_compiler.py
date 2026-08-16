"""Seal helper objects reachable from the canonical neutral pack graph.

The neutral catalog denominator stays the authored map/scenario family.  This
module closes runtime dependencies reached *from* those objects: lair rebuild
holes (which must already be canonical artifacts), treasure ObjectCreationLists,
and the active pickup crates those lists create.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Mapping, Sequence
from copy import deepcopy
from dataclasses import replace
import re

from .module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    validate_module_contracts,
)
from .playable_structure_pack_compiler import (
    _digest,
    _slug,
    compile_structure_visual_recipe,
    validate_structure_visual_recipe,
)
from .playable_unit_compiler import (
    OBJECT_CREATION_LIST_PATH,
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    _ancestry,
    _effective_top_blocks,
    _geometry_contact_points,
    _geometry_contract,
    _nested_references,
    _object_semantic,
    _ocl_create_object_entries,
    playable_object_kind_of,
    playable_object_kind_of_provenance,
    prepare_playable_unit_compiler,
)
from .sage_cst import SageCstError, parse_sage_document


PLAN_SCHEMA = "openbfme.neutral-dependency-plan"
PLAN_SCHEMA_VERSION = 0
ARTIFACT_SCHEMA = "openbfme.neutral-dependency-pack-artifact"
ARTIFACT_SCHEMA_VERSION = 0
PICKUP_DESCRIPTOR_SCHEMA = "openbfme.neutral-pickup-descriptor"
PICKUP_RUNTIME_SCHEMA = "openbfme.neutral-pickup-runtime"
EXPECTED_DEPENDENCIES = {
    "bfme2": {
        "holeObjectCount": 6,
        "holeOwnerEdgeCount": 8,
        "objectCreationListCount": 4,
        "pickupObjectCount": 2,
        "objectCreationEdgeCount": 5,
    },
    "rotwk": {
        "holeObjectCount": 9,
        "holeOwnerEdgeCount": 13,
        "objectCreationListCount": 3,
        "pickupObjectCount": 1,
        "objectCreationEdgeCount": 3,
    },
}
EXPECTED_CANONICAL_COUNTS = {"bfme2": 69, "rotwk": 83}
_SHA256 = re.compile(r"[0-9a-f]{64}")
CRATE_OBJECT_PATH = "data/ini/crate.ini"
SALVAGE_CRATE_BINARY_SEMANTICS = {
    "domain": "active-collision-pickup",
    "activeWhenAuthored": [
        "AllowAIPickup",
        "LevelUpChance",
        "MaxResource",
        "MinResource",
        "Upgrade",
    ],
    "deadBranchWhenAuthored": ["LevelUpRadius"],
    "parsedIgnoredWhenAuthored": [
        "BannerChance",
        "PorterChance",
        "ResourceChance",
    ],
}


class NeutralDependencyPackCompilerError(ValueError):
    """A reachable neutral helper is missing, ambiguous, or unsealed."""


def _sha(value: object, label: str) -> str:
    if not isinstance(value, str) or _SHA256.fullmatch(value) is None:
        raise NeutralDependencyPackCompilerError(f"{label} is invalid")
    return value


def _contracts(descriptor: Mapping[str, object]) -> list[Mapping[str, object]]:
    gameplay = descriptor.get("gameplay")
    if isinstance(gameplay, Mapping):
        rows = gameplay.get("moduleContracts")
    else:
        rows = descriptor.get("moduleContracts")
    if not isinstance(rows, list) or any(not isinstance(row, Mapping) for row in rows):
        raise NeutralDependencyPackCompilerError("neutral descriptor module contracts are invalid")
    return rows


def _typed_contract(
    descriptor: Mapping[str, object],
    module: str,
    *,
    required: bool = True,
    runtime_status: str | tuple[str, ...] = "executable",
) -> Mapping[str, object] | None:
    rows = [row for row in _contracts(descriptor) if row.get("module") == module]
    if not rows and not required:
        return None
    allowed_statuses = (
        (runtime_status,) if isinstance(runtime_status, str) else runtime_status
    )
    if (
        len(rows) != 1
        or rows[0].get("extraction") != "typed"
        or rows[0].get("runtimeStatus") not in allowed_statuses
    ):
        raise NeutralDependencyPackCompilerError(
            f"neutral dependency requires one typed {module} contract with runtimeStatus in {allowed_statuses}"
        )
    return rows[0]


def _field_value(contract: Mapping[str, object], field: str) -> object:
    fields = contract.get("fields")
    row = fields.get(field) if isinstance(fields, Mapping) else None
    if not isinstance(row, Mapping) or "value" not in row:
        raise NeutralDependencyPackCompilerError(
            f"{contract.get('module')} field {field} has no typed value"
        )
    return row["value"]


def _artifact_index(
    catalog: Mapping[str, object], artifacts: Sequence[Mapping[str, object]]
) -> dict[str, Mapping[str, object]]:
    rows = catalog.get("neutralMobs")
    if not isinstance(rows, list):
        raise NeutralDependencyPackCompilerError("neutral catalog rows are invalid")
    expected = {
        str(row.get("objectId", "")).casefold()
        for row in rows
        if isinstance(row, Mapping) and isinstance(row.get("objectId"), str)
    }
    by_id: dict[str, Mapping[str, object]] = {}
    for artifact in artifacts:
        object_id = artifact.get("objectId") if isinstance(artifact, Mapping) else None
        if not isinstance(object_id, str) or not object_id:
            raise NeutralDependencyPackCompilerError("canonical neutral artifact identity is invalid")
        folded = object_id.casefold()
        if folded in by_id:
            raise NeutralDependencyPackCompilerError(f"duplicate canonical artifact: {object_id}")
        _sha(artifact.get("artifactSha256"), f"canonical {object_id} artifact digest")
        by_id[folded] = artifact
    if set(by_id) != expected:
        raise NeutralDependencyPackCompilerError("canonical artifact set is not exact")
    return by_id


def _ocl_definition(
    source: bytes, identifier: str
) -> tuple[int, list[dict[str, object]]]:
    entries = _ocl_create_object_entries(source, identifier)
    if entries is None or not entries:
        raise NeutralDependencyPackCompilerError(
            f"ObjectCreationList {identifier} is missing or creates no objects"
        )
    text = source.decode("cp1252")
    header = re.compile(r"^\s*ObjectCreationList\s+" + re.escape(identifier) + r"\s*(?:;.*)?$", re.I)
    header_lines = [
        line_number
        for line_number, raw in enumerate(text.splitlines(), 1)
        if header.fullmatch(raw)
    ]
    if len(header_lines) != 1:
        raise NeutralDependencyPackCompilerError(
            f"ObjectCreationList {identifier} definition is ambiguous"
        )
    created: list[dict[str, object]] = []
    for entry in entries:
        raw_fields = entry.get("fields")
        if not isinstance(raw_fields, list):
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} fields are invalid")
        grouped: dict[str, list[Mapping[str, object]]] = defaultdict(list)
        for field in raw_fields:
            if not isinstance(field, Mapping):
                raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} field is invalid")
            grouped[str(field.get("key", "")).casefold()].append(field)
        names = grouped.get("objectnames", [])
        if len(names) != 1:
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} ObjectNames is invalid")
        name_tokens = str(names[0].get("value", "")).split()
        if not name_tokens or any(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None for value in name_tokens):
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} ObjectNames is malformed")
        count_rows = grouped.get("count", [])
        if len(count_rows) > 1:
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} Count is duplicated")
        try:
            count = int(str(count_rows[0]["value"])) if count_rows else 1
        except (KeyError, TypeError, ValueError) as exc:
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} Count is invalid") from exc
        if count <= 0 or count > 256:
            raise NeutralDependencyPackCompilerError(f"ObjectCreationList {identifier} Count is out of bounds")
        for object_id in name_tokens:
            created.append(
                {
                    "objectId": object_id,
                    "count": count,
                    "sourceIni": OBJECT_CREATION_LIST_PATH,
                    "line": int(names[0]["line"]),
                    "createObjectLine": int(entry["line"]),
                    "fields": deepcopy(raw_fields),
                }
            )
    return header_lines[0], created


def discover_neutral_dependencies(
    catalog: Mapping[str, object],
    artifacts: Sequence[Mapping[str, object]],
    documents: Mapping[str, bytes],
    *,
    game: str,
) -> dict[str, object]:
    """Discover the exact reachable helper graph without changing catalog rows."""

    expected = EXPECTED_DEPENDENCIES.get(game)
    if expected is None or catalog.get("game") != game:
        raise NeutralDependencyPackCompilerError("neutral dependency game is invalid")
    catalog_sha = _sha(catalog.get("catalogSha256"), "neutral catalog digest")
    summary = catalog.get("summary")
    if not isinstance(summary, Mapping):
        raise NeutralDependencyPackCompilerError("neutral catalog summary is invalid")
    map_added = summary.get("mapPlacementAddedCount", 0)
    if not isinstance(map_added, int) or isinstance(map_added, bool) or map_added < 0:
        raise NeutralDependencyPackCompilerError(
            "neutral map-placement addition count is invalid"
        )
    by_id = _artifact_index(catalog, artifacts)
    hole_edges: list[dict[str, object]] = []
    for artifact in sorted(by_id.values(), key=lambda row: str(row["objectId"]).casefold()):
        if artifact.get("role") != "lair":
            continue
        descriptor = artifact.get("descriptor")
        if not isinstance(descriptor, Mapping):
            raise NeutralDependencyPackCompilerError("lair descriptor is invalid")
        contract = _typed_contract(
            descriptor,
            "RebuildHoleExposeDie",
            required=False,
            runtime_status=("deferred", "executable"),
        )
        if contract is None:
            continue
        hole_id = _field_value(contract, "HoleName")
        if not isinstance(hole_id, str) or not hole_id:
            raise NeutralDependencyPackCompilerError("RebuildHoleExposeDie HoleName is invalid")
        hole_artifact = by_id.get(hole_id.casefold())
        if hole_artifact is None or hole_artifact.get("runtimeDomain") not in (None, "structure"):
            raise NeutralDependencyPackCompilerError(f"lair hole helper is not a canonical structure: {hole_id}")
        hole_descriptor = hole_artifact.get("descriptor")
        if not isinstance(hole_descriptor, Mapping):
            raise NeutralDependencyPackCompilerError(f"lair hole descriptor is invalid: {hole_id}")
        hole_edges.append(
            {
                "ownerObjectId": artifact["objectId"],
                "ownerArtifactSha256": artifact["artifactSha256"],
                "ownerDescriptorSha256": descriptor["descriptorSha256"],
                "moduleTag": contract.get("tag"),
                "sourceIni": contract["sourceIni"],
                "line": contract["line"],
                "runtimeStatus": contract["runtimeStatus"],
                "holeObjectId": hole_artifact["objectId"],
                "holeArtifactSha256": hole_artifact["artifactSha256"],
                "holeDescriptorSha256": hole_descriptor["descriptorSha256"],
            }
        )
    unique_holes = sorted({str(row["holeObjectId"]) for row in hole_edges}, key=str.casefold)
    ocl_source = documents.get(OBJECT_CREATION_LIST_PATH)
    if not isinstance(ocl_source, bytes):
        raise NeutralDependencyPackCompilerError(f"missing {OBJECT_CREATION_LIST_PATH}")
    hole_ocl_edges: list[dict[str, object]] = []
    hole_lifecycle_bindings: list[dict[str, object]] = []
    ocl_ids: set[str] = set()
    for hole_id in unique_holes:
        artifact = by_id[hole_id.casefold()]
        descriptor = artifact.get("descriptor")
        assert isinstance(descriptor, Mapping)
        rebuild = _typed_contract(
            descriptor,
            "RebuildHoleBehavior",
            runtime_status=("deferred", "executable"),
        )
        hole_lifecycle_bindings.append(
            {
                "holeObjectId": hole_id,
                "holeArtifactSha256": artifact["artifactSha256"],
                "holeDescriptorSha256": descriptor["descriptorSha256"],
                "moduleTag": rebuild.get("tag"),
                "sourceIni": rebuild["sourceIni"],
                "line": rebuild["line"],
                "runtimeStatus": rebuild["runtimeStatus"],
                "lifecycleGraph": deepcopy(rebuild.get("lifecycleGraph")),
            }
        )
        contract = _typed_contract(descriptor, "CreateObjectDie")
        ocl_id = _field_value(contract, "CreationList")
        if not isinstance(ocl_id, str) or not ocl_id:
            raise NeutralDependencyPackCompilerError("CreateObjectDie CreationList is invalid")
        ocl_ids.add(ocl_id)
        hole_ocl_edges.append(
            {
                "holeObjectId": hole_id,
                "holeArtifactSha256": artifact["artifactSha256"],
                "moduleTag": contract.get("tag"),
                "sourceIni": contract["sourceIni"],
                "line": contract["line"],
                "runtimeStatus": contract["runtimeStatus"],
                "objectCreationListId": ocl_id,
            }
        )
    ocls: list[dict[str, object]] = []
    pickup_ids: set[str] = set()
    creation_edge_count = 0
    for ocl_id in sorted(ocl_ids, key=str.casefold):
        line, created = _ocl_definition(ocl_source, ocl_id)
        creation_edge_count += len(created)
        pickup_ids.update(str(row["objectId"]) for row in created)
        ocl = {
            "objectCreationListId": ocl_id,
            "sourceIni": OBJECT_CREATION_LIST_PATH,
            "line": line,
            "createObjects": created,
        }
        ocl["semanticSha256"] = _digest(ocl)
        ocls.append(ocl)
    summary = {
        "holeObjectCount": len(unique_holes),
        "holeOwnerEdgeCount": len(hole_edges),
        "objectCreationListCount": len(ocls),
        "pickupObjectCount": len(pickup_ids),
        "objectCreationEdgeCount": creation_edge_count,
    }
    if summary != expected:
        raise NeutralDependencyPackCompilerError(
            f"{game} neutral dependency counts drifted: {summary} expected {expected}"
        )
    plan: dict[str, object] = {
        "schema": PLAN_SCHEMA,
        "schemaVersion": PLAN_SCHEMA_VERSION,
        "game": game,
        "catalogSha256": catalog_sha,
        "canonicalObjectCount": len(by_id),
        "mapPlacementAddedCount": map_added,
        "holeBindings": hole_edges,
        "holeLifecycleBindings": hole_lifecycle_bindings,
        "objectCreationBindings": hole_ocl_edges,
        "objectCreationLists": ocls,
        "pickupObjectIds": sorted(pickup_ids, key=str.casefold),
        "summary": summary,
    }
    plan["planSha256"] = _digest(plan)
    validate_neutral_dependency_plan(plan)
    return plan


def validate_neutral_dependency_plan(value: Mapping[str, object]) -> None:
    if value.get("schema") != PLAN_SCHEMA or value.get("schemaVersion") != PLAN_SCHEMA_VERSION:
        raise NeutralDependencyPackCompilerError("neutral dependency plan schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("planSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralDependencyPackCompilerError("neutral dependency plan digest is invalid")
    expected = EXPECTED_DEPENDENCIES.get(str(value.get("game", "")))
    if expected is None or value.get("summary") != expected:
        raise NeutralDependencyPackCompilerError("neutral dependency plan counts are invalid")
    _sha(value.get("catalogSha256"), "neutral dependency catalog digest")
    map_added = value.get("mapPlacementAddedCount", 0)
    if not isinstance(map_added, int) or isinstance(map_added, bool) or map_added < 0:
        raise NeutralDependencyPackCompilerError(
            "neutral dependency map-placement addition count is invalid"
        )
    expected_canonical = EXPECTED_CANONICAL_COUNTS[str(value["game"])] + map_added
    if value.get("canonicalObjectCount") != expected_canonical:
        raise NeutralDependencyPackCompilerError("neutral dependency canonical count drifted")
    pickups = value.get("pickupObjectIds")
    if not isinstance(pickups, list) or len(pickups) != expected["pickupObjectCount"]:
        raise NeutralDependencyPackCompilerError("neutral dependency pickup identities are invalid")
    if len({str(item).casefold() for item in pickups}) != len(pickups):
        raise NeutralDependencyPackCompilerError("neutral dependency pickup identities are duplicated")
    hole_bindings = value.get("holeBindings")
    creation_bindings = value.get("objectCreationBindings")
    lifecycle_bindings = value.get("holeLifecycleBindings")
    ocls = value.get("objectCreationLists")
    if (
        not isinstance(hole_bindings, list)
        or len(hole_bindings) != expected["holeOwnerEdgeCount"]
        or not isinstance(creation_bindings, list)
        or len(creation_bindings) != expected["holeObjectCount"]
        or not isinstance(lifecycle_bindings, list)
        or len(lifecycle_bindings) != expected["holeObjectCount"]
        or not isinstance(ocls, list)
        or len(ocls) != expected["objectCreationListCount"]
    ):
        raise NeutralDependencyPackCompilerError("neutral dependency edge set is incomplete")
    hole_ids: set[str] = set()
    for edge in hole_bindings:
        if not isinstance(edge, Mapping):
            raise NeutralDependencyPackCompilerError("neutral hole binding is invalid")
        for field in ("ownerObjectId", "holeObjectId", "sourceIni"):
            if not isinstance(edge.get(field), str) or not edge[field]:
                raise NeutralDependencyPackCompilerError("neutral hole binding provenance is invalid")
        for field in (
            "ownerArtifactSha256",
            "ownerDescriptorSha256",
            "holeArtifactSha256",
            "holeDescriptorSha256",
        ):
            _sha(edge.get(field), f"neutral hole binding {field}")
        if not isinstance(edge.get("line"), int) or int(edge["line"]) <= 0:
            raise NeutralDependencyPackCompilerError("neutral hole binding line is invalid")
        if edge.get("runtimeStatus") not in ("deferred", "executable"):
            raise NeutralDependencyPackCompilerError("neutral hole binding runtime status is invalid")
        hole_ids.add(str(edge["holeObjectId"]).casefold())
    if len(hole_ids) != expected["holeObjectCount"]:
        raise NeutralDependencyPackCompilerError("neutral hole identities drifted")
    bound_holes: set[str] = set()
    bound_ocls: set[str] = set()
    for edge in creation_bindings:
        if not isinstance(edge, Mapping):
            raise NeutralDependencyPackCompilerError("neutral creation binding is invalid")
        for field in ("holeObjectId", "objectCreationListId", "sourceIni"):
            if not isinstance(edge.get(field), str) or not edge[field]:
                raise NeutralDependencyPackCompilerError("neutral creation binding provenance is invalid")
        _sha(edge.get("holeArtifactSha256"), "neutral creation hole artifact digest")
        if (
            not isinstance(edge.get("line"), int)
            or int(edge["line"]) <= 0
            or edge.get("runtimeStatus") != "executable"
        ):
            raise NeutralDependencyPackCompilerError("neutral creation binding line is invalid")
        bound_holes.add(str(edge["holeObjectId"]).casefold())
        bound_ocls.add(str(edge["objectCreationListId"]).casefold())
    if bound_holes != hole_ids or len(bound_ocls) != expected["objectCreationListCount"]:
        raise NeutralDependencyPackCompilerError("neutral creation bindings drifted")
    lifecycle_holes: set[str] = set()
    for edge in lifecycle_bindings:
        if not isinstance(edge, Mapping):
            raise NeutralDependencyPackCompilerError("neutral hole lifecycle binding is invalid")
        for field in ("holeObjectId", "sourceIni"):
            if not isinstance(edge.get(field), str) or not edge[field]:
                raise NeutralDependencyPackCompilerError("neutral hole lifecycle provenance is invalid")
        for field in ("holeArtifactSha256", "holeDescriptorSha256"):
            _sha(edge.get(field), f"neutral hole lifecycle {field}")
        if (
            edge.get("runtimeStatus") not in ("deferred", "executable")
            or not isinstance(edge.get("line"), int)
            or not isinstance(edge.get("lifecycleGraph"), Mapping)
        ):
            raise NeutralDependencyPackCompilerError("neutral hole lifecycle contract is invalid")
        lifecycle_holes.add(str(edge["holeObjectId"]).casefold())
    if lifecycle_holes != hole_ids:
        raise NeutralDependencyPackCompilerError("neutral hole lifecycle identities drifted")
    created_ids: set[str] = set()
    creation_edges = 0
    observed_ocls: set[str] = set()
    for ocl in ocls:
        if not isinstance(ocl, Mapping):
            raise NeutralDependencyPackCompilerError("neutral ObjectCreationList is invalid")
        unsigned_ocl = dict(ocl)
        ocl_sha = unsigned_ocl.pop("semanticSha256", None)
        if not isinstance(ocl_sha, str) or ocl_sha != _digest(unsigned_ocl):
            raise NeutralDependencyPackCompilerError("neutral ObjectCreationList digest is invalid")
        identifier = ocl.get("objectCreationListId")
        if not isinstance(identifier, str) or not identifier:
            raise NeutralDependencyPackCompilerError("neutral ObjectCreationList identity is invalid")
        if (
            ocl.get("sourceIni") != OBJECT_CREATION_LIST_PATH
            or not isinstance(ocl.get("line"), int)
            or int(ocl["line"]) <= 0
        ):
            raise NeutralDependencyPackCompilerError("neutral ObjectCreationList provenance is invalid")
        created = ocl.get("createObjects")
        if not isinstance(created, list) or not created:
            raise NeutralDependencyPackCompilerError("neutral ObjectCreationList leaves are invalid")
        observed_ocls.add(identifier.casefold())
        creation_edges += len(created)
        for leaf in created:
            if (
                not isinstance(leaf, Mapping)
                or not isinstance(leaf.get("objectId"), str)
                or not leaf["objectId"]
                or not isinstance(leaf.get("count"), int)
                or int(leaf["count"]) <= 0
                or leaf.get("sourceIni") != OBJECT_CREATION_LIST_PATH
                or not isinstance(leaf.get("line"), int)
                or int(leaf["line"]) <= 0
                or not isinstance(leaf.get("createObjectLine"), int)
                or int(leaf["createObjectLine"]) <= 0
                or not isinstance(leaf.get("fields"), list)
            ):
                raise NeutralDependencyPackCompilerError("neutral ObjectCreationList leaf provenance is invalid")
            created_ids.add(str(leaf["objectId"]).casefold())
    if (
        observed_ocls != bound_ocls
        or creation_edges != expected["objectCreationEdgeCount"]
        or created_ids != {str(item).casefold() for item in pickups}
    ):
        raise NeutralDependencyPackCompilerError("neutral ObjectCreationList graph drifted")


def _pickup_descriptor(
    object_id: str,
    documents: Mapping[str, bytes],
    prepared: PlayableUnitCompilerInputs,
    *,
    game: str,
) -> dict[str, object]:
    crate_sources = [
        (path, source)
        for path, source in documents.items()
        if path.replace("\\", "/").casefold() == CRATE_OBJECT_PATH
    ]
    if len(crate_sources) != 1:
        raise NeutralDependencyPackCompilerError(
            f"effective pickup Object source is not exact: {CRATE_OBJECT_PATH}"
        )
    crate_path, crate_source = crate_sources[0]
    try:
        crate_objects = parse_sage_document(
            crate_source, crate_path.replace("\\", "/")
        ).objects
    except SageCstError as exc:
        raise NeutralDependencyPackCompilerError(
            f"pickup Object source is invalid: {exc}"
        ) from exc
    effective_objects = dict(prepared.objects)
    for crate_object in crate_objects:
        effective_objects[crate_object.name.casefold()] = crate_object
    pickup_prepared = replace(prepared, objects=effective_objects)
    target = pickup_prepared.objects.get(object_id.casefold())
    if target is None or target.name != object_id:
        raise NeutralDependencyPackCompilerError(f"pickup Object is missing: {object_id}")
    try:
        lineage = _ancestry(pickup_prepared.objects, target)
        kind_of = playable_object_kind_of(pickup_prepared, target.name)
        contracts = compile_all_module_contracts(lineage, target.name)
        validate_module_contracts(contracts, label=f"neutral pickup {object_id}")
        geometry = _geometry_contract(lineage, pickup_prepared.numeric_defines)
        contact_points = _geometry_contact_points(lineage)
        references = _nested_references(lineage)
    except (PlayableUnitCompilerError, ModuleContractError) as exc:
        raise NeutralDependencyPackCompilerError(f"pickup {object_id} evidence is invalid: {exc}") from exc
    kinds = {value.upper() for value in kind_of}
    if not {"CRATE", "IMMOBILE", "UNATTACKABLE"}.issubset(kinds) or "STRUCTURE" in kinds:
        raise NeutralDependencyPackCompilerError(f"pickup {object_id} KindOf is invalid")
    pickup = _typed_contract(
        {"moduleContracts": contracts},
        "SalvageCrateCollide",
        runtime_status=("deferred", "executable"),
    )
    deletion = _typed_contract({"moduleContracts": contracts}, "DeletionUpdate", required=False)
    if not references.get("model"):
        raise NeutralDependencyPackCompilerError(f"pickup {object_id} has no model")
    inheritance: list[dict[str, object]] = []
    semantics_by_path: dict[str, list[dict[str, object]]] = defaultdict(list)
    for item in lineage:
        semantic = _object_semantic(item)
        semantics_by_path[item.source_virtual_path].append(semantic)
        inheritance.append(
            {
                "objectId": item.name,
                "declarationKind": item.kind,
                "parentObjectId": item.parent,
                "sourceIni": item.source_virtual_path,
                "line": item.line,
                "semanticSha256": _digest(semantic),
            }
        )
    draw_modules = [
        {
            "kind": block.kind,
            "instanceTag": block.instance_tag,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for block in _effective_top_blocks(lineage)
        if (block.header_key or "").casefold() == "draw"
    ]
    descriptor: dict[str, object] = {
        "schema": PICKUP_DESCRIPTOR_SCHEMA,
        "schemaVersion": 0,
        "game": game,
        "objectId": object_id,
        "runtimeDomain": "active-pickup",
        "runtimeStatus": pickup["runtimeStatus"],
        "declarationKind": target.kind,
        "parentObjectId": target.parent,
        "inheritance": inheritance,
        "kindOf": {
            "effective": list(kind_of),
            "defineProvenance": playable_object_kind_of_provenance(
                pickup_prepared, object_id
            ),
        },
        "moduleContracts": contracts,
        "pickupContract": deepcopy(pickup),
        "binaryOracleReceipt": {
            **deepcopy(SALVAGE_CRATE_BINARY_SEMANTICS),
            "authoredFields": sorted(pickup["fields"], key=str.casefold),
        },
        **({"deletionContract": deepcopy(deletion)} if deletion is not None else {}),
        "geometry": geometry,
        "geometryContactPoints": contact_points,
        "presentation": {"drawModules": draw_modules, "sourceReferences": references},
        "production": [],
        "scenarioAdmission": {
            "kind": "authored-ocl-pickup-leaf",
            "surfaces": ["object-creation-list"],
            "buildCommandExposed": False,
            "evidence": "reachable-neutral-lair-treasure-ocl",
        },
        "sourceDocuments": [
            {"virtualPath": path, "semanticSha256": _digest(rows)}
            for path, rows in sorted(semantics_by_path.items(), key=lambda row: row[0].casefold())
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    validate_neutral_pickup_descriptor(descriptor)
    return descriptor


def validate_neutral_pickup_descriptor(value: Mapping[str, object]) -> None:
    if value.get("schema") != PICKUP_DESCRIPTOR_SCHEMA or value.get("schemaVersion") != 0:
        raise NeutralDependencyPackCompilerError("neutral pickup descriptor schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralDependencyPackCompilerError("neutral pickup descriptor digest is invalid")
    if (
        value.get("runtimeDomain") != "active-pickup"
        or value.get("runtimeStatus") not in ("deferred", "executable")
        or value.get("production") != []
    ):
        raise NeutralDependencyPackCompilerError("neutral pickup runtime domain is invalid")
    admission = value.get("scenarioAdmission")
    if not isinstance(admission, Mapping) or admission.get("surfaces") != ["object-creation-list"]:
        raise NeutralDependencyPackCompilerError("neutral pickup admission is invalid")
    validate_module_contracts(value.get("moduleContracts"), label="neutral pickup")
    pickup = _typed_contract(
        value, "SalvageCrateCollide", runtime_status=("deferred", "executable")
    )
    if pickup.get("runtimeStatus") != value.get("runtimeStatus"):
        raise NeutralDependencyPackCompilerError("neutral pickup runtime status drifted")
    expected_oracle = {
        **SALVAGE_CRATE_BINARY_SEMANTICS,
        "authoredFields": sorted(pickup["fields"], key=str.casefold),
    }
    if value.get("binaryOracleReceipt") != expected_oracle:
        raise NeutralDependencyPackCompilerError("neutral pickup binary oracle receipt drifted")


def _resource_ownership(recipe: Mapping[str, object]) -> dict[str, object]:
    resources = recipe.get("resources")
    if not isinstance(resources, list) or not resources:
        raise NeutralDependencyPackCompilerError("neutral pickup has no visual resources")
    ids: list[str] = []
    outputs: list[str] = []
    for row in resources:
        if not isinstance(row, Mapping) or not isinstance(row.get("id"), str):
            raise NeutralDependencyPackCompilerError("neutral pickup resource is invalid")
        ids.append(str(row["id"]))
        if row.get("output") is not None:
            outputs.append(str(row["output"]))
    if len({item.casefold() for item in ids}) != len(ids) or len({item.casefold() for item in outputs}) != len(outputs):
        raise NeutralDependencyPackCompilerError("neutral pickup resource ownership is duplicated")
    return {
        "resourceIds": sorted(ids, key=str.casefold),
        "outputs": sorted(outputs, key=str.casefold),
        "resources": sorted(
            (deepcopy(dict(row)) for row in resources),
            key=lambda row: str(row["id"]).casefold(),
        ),
    }


def _pickup_visual_recipe(
    object_id: str, visual_closure: Mapping[str, object]
) -> dict[str, object]:
    """Reuse the general W3D lifecycle compiler but own pickup-namespaced output."""

    recipe = compile_structure_visual_recipe(object_id, visual_closure)
    slug = _slug(object_id)
    old_id_prefix = f"structure-{slug}-"
    new_id_prefix = f"neutral-pickup-{slug}-"
    old_output_prefix = f"assets/models/structures/{slug}/"
    new_output_prefix = f"assets/models/neutral-pickups/{slug}/"

    def remap(value: object) -> object:
        if isinstance(value, str):
            if value.startswith(old_id_prefix):
                return new_id_prefix + value[len(old_id_prefix) :]
            if value.startswith(old_output_prefix):
                return new_output_prefix + value[len(old_output_prefix) :]
            return value
        if isinstance(value, list):
            return [remap(item) for item in value]
        if isinstance(value, Mapping):
            return {key: remap(item) for key, item in value.items()}
        return value

    remapped = remap(recipe)
    assert isinstance(remapped, dict)
    remapped.pop("recipeSha256", None)
    remapped["recipeSha256"] = _digest(remapped)
    validate_structure_visual_recipe(remapped)
    return remapped


def compile_neutral_dependency_pack_artifact(
    plan: Mapping[str, object],
    documents: Mapping[str, bytes],
    visual_closure: Mapping[str, object],
    *,
    game: str,
    prepared: PlayableUnitCompilerInputs | None = None,
) -> dict[str, object]:
    validate_neutral_dependency_plan(plan)
    if plan.get("game") != game:
        raise NeutralDependencyPackCompilerError("neutral dependency plan game drifted")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise NeutralDependencyPackCompilerError("prepared inputs belong to different documents")
    pickup_artifacts: list[dict[str, object]] = []
    for object_id in plan["pickupObjectIds"]:
        descriptor = _pickup_descriptor(str(object_id), documents, prepared, game=game)
        try:
            recipe = _pickup_visual_recipe(str(object_id), visual_closure)
            validate_structure_visual_recipe(recipe)
        except ValueError as exc:
            raise NeutralDependencyPackCompilerError(f"pickup {object_id} visual recipe is invalid: {exc}") from exc
        ownership = _resource_ownership(recipe)
        runtime: dict[str, object] = {
            "schema": PICKUP_RUNTIME_SCHEMA,
            "schemaVersion": 0,
            "game": game,
            "objectId": object_id,
            "runtimeDomain": "active-pickup",
            "runtimeStatus": descriptor["runtimeStatus"],
            "descriptorSha256": descriptor["descriptorSha256"],
            "recipeSha256": recipe["recipeSha256"],
            "resourceIds": ownership["resourceIds"],
            "production": [],
            "scenarioAdmission": deepcopy(descriptor["scenarioAdmission"]),
            "kindOf": deepcopy(descriptor["kindOf"]),
            "geometry": deepcopy(descriptor["geometry"]),
            "pickupContract": deepcopy(descriptor["pickupContract"]),
            "binaryOracleReceipt": deepcopy(descriptor["binaryOracleReceipt"]),
            **({"deletionContract": deepcopy(descriptor["deletionContract"])} if "deletionContract" in descriptor else {}),
            "presentation": {
                "lifecycleStates": deepcopy(recipe.get("lifecycleStates", [])),
                "bibStates": deepcopy(recipe.get("bibStates", [])),
            },
        }
        runtime["runtimeSha256"] = _digest(runtime)
        pickup_artifact: dict[str, object] = {
            "schema": "openbfme.neutral-pickup-pack-artifact",
            "schemaVersion": 0,
            "game": game,
            "objectId": object_id,
            "runtimeDomain": "active-pickup",
            "runtimeStatus": descriptor["runtimeStatus"],
            "descriptor": descriptor,
            "visualRecipe": recipe,
            "runtime": runtime,
            "resourceOwnership": ownership,
        }
        pickup_artifact["artifactSha256"] = _digest(pickup_artifact)
        pickup_artifacts.append(pickup_artifact)
    unsigned_closure = dict(visual_closure)
    closure_sha = unsigned_closure.pop("aggregateSha256", None)
    if not isinstance(closure_sha, str) or closure_sha != _digest(unsigned_closure):
        raise NeutralDependencyPackCompilerError("pickup visual closure digest is invalid")
    dependency_statuses = [
        str(row["runtimeStatus"])
        for field in ("holeBindings", "holeLifecycleBindings", "objectCreationBindings")
        for row in plan[field]
    ] + [str(row["runtimeStatus"]) for row in pickup_artifacts]
    runtime_summary = {
        "contractCount": len(dependency_statuses),
        "executableCount": dependency_statuses.count("executable"),
        "deferredCount": dependency_statuses.count("deferred"),
        "ready": all(status == "executable" for status in dependency_statuses),
    }
    artifact: dict[str, object] = {
        "schema": ARTIFACT_SCHEMA,
        "schemaVersion": ARTIFACT_SCHEMA_VERSION,
        "game": game,
        "catalogSha256": plan["catalogSha256"],
        "planSha256": plan["planSha256"],
        "plan": deepcopy(dict(plan)),
        "visualClosureSha256": closure_sha,
        "pickupArtifacts": pickup_artifacts,
        "summary": deepcopy(plan["summary"]),
        "runtimeSummary": runtime_summary,
    }
    artifact["artifactSha256"] = _digest(artifact)
    validate_neutral_dependency_pack_artifact(artifact)
    return artifact


def validate_neutral_dependency_pack_artifact(value: Mapping[str, object]) -> None:
    if value.get("schema") != ARTIFACT_SCHEMA or value.get("schemaVersion") != ARTIFACT_SCHEMA_VERSION:
        raise NeutralDependencyPackCompilerError("neutral dependency artifact schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("artifactSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralDependencyPackCompilerError("neutral dependency artifact digest is invalid")
    plan = value.get("plan")
    if not isinstance(plan, Mapping):
        raise NeutralDependencyPackCompilerError("neutral dependency artifact plan is invalid")
    validate_neutral_dependency_plan(plan)
    if (
        value.get("game") != plan.get("game")
        or value.get("catalogSha256") != plan.get("catalogSha256")
        or value.get("planSha256") != plan.get("planSha256")
        or value.get("summary") != plan.get("summary")
    ):
        raise NeutralDependencyPackCompilerError("neutral dependency artifact plan binding drifted")
    _sha(value.get("visualClosureSha256"), "neutral dependency visual closure digest")
    summary = value.get("summary")
    if not isinstance(summary, Mapping):
        raise NeutralDependencyPackCompilerError("neutral dependency artifact summary is invalid")
    runtime_summary = value.get("runtimeSummary")
    if not isinstance(runtime_summary, Mapping):
        raise NeutralDependencyPackCompilerError("neutral dependency runtime summary is invalid")
    pickups = value.get("pickupArtifacts")
    if not isinstance(pickups, list) or len(pickups) != summary["pickupObjectCount"]:
        raise NeutralDependencyPackCompilerError("neutral dependency pickup artifacts are incomplete")
    expected_ids = {str(item).casefold() for item in plan["pickupObjectIds"]}
    observed_ids: set[str] = set()
    for pickup in pickups:
        if not isinstance(pickup, Mapping):
            raise NeutralDependencyPackCompilerError("neutral pickup artifact is invalid")
        pickup_unsigned = dict(pickup)
        pickup_sha = pickup_unsigned.pop("artifactSha256", None)
        if not isinstance(pickup_sha, str) or pickup_sha != _digest(pickup_unsigned):
            raise NeutralDependencyPackCompilerError("neutral pickup artifact digest is invalid")
        descriptor = pickup.get("descriptor")
        recipe = pickup.get("visualRecipe")
        runtime = pickup.get("runtime")
        if not all(isinstance(item, Mapping) for item in (descriptor, recipe, runtime)):
            raise NeutralDependencyPackCompilerError("neutral pickup artifact documents are invalid")
        validate_neutral_pickup_descriptor(descriptor)
        validate_structure_visual_recipe(recipe)
        runtime_unsigned = dict(runtime)
        runtime_sha = runtime_unsigned.pop("runtimeSha256", None)
        if not isinstance(runtime_sha, str) or runtime_sha != _digest(runtime_unsigned):
            raise NeutralDependencyPackCompilerError("neutral pickup runtime digest is invalid")
        ownership = _resource_ownership(recipe)
        if (
            pickup.get("game") != value.get("game")
            or descriptor.get("game") != value.get("game")
            or pickup.get("objectId") != descriptor.get("objectId")
            or pickup.get("objectId") != recipe.get("objectId")
            or pickup.get("objectId") != runtime.get("objectId")
            or pickup.get("runtimeDomain") != "active-pickup"
            or pickup.get("runtimeStatus") not in ("deferred", "executable")
            or pickup.get("resourceOwnership") != ownership
            or recipe.get("visualClosureSha256")
            != value.get("visualClosureSha256")
            or runtime.get("game") != value.get("game")
            or runtime.get("runtimeDomain") != "active-pickup"
            or runtime.get("runtimeStatus") != pickup.get("runtimeStatus")
            or runtime.get("production") != []
            or runtime.get("scenarioAdmission")
            != descriptor.get("scenarioAdmission")
            or runtime.get("descriptorSha256") != descriptor.get("descriptorSha256")
            or runtime.get("recipeSha256") != recipe.get("recipeSha256")
            or runtime.get("resourceIds") != ownership["resourceIds"]
            or runtime.get("binaryOracleReceipt")
            != descriptor.get("binaryOracleReceipt")
            or not isinstance(runtime.get("presentation"), Mapping)
        ):
            raise NeutralDependencyPackCompilerError("neutral pickup runtime binding drifted")
        observed_ids.add(str(pickup.get("objectId", "")).casefold())
    if observed_ids != expected_ids:
        raise NeutralDependencyPackCompilerError("neutral pickup artifact identities drifted")
    statuses = [
        str(row["runtimeStatus"])
        for field in ("holeBindings", "holeLifecycleBindings", "objectCreationBindings")
        for row in plan[field]
    ] + [str(row["runtimeStatus"]) for row in pickups]
    expected_runtime_summary = {
        "contractCount": len(statuses),
        "executableCount": statuses.count("executable"),
        "deferredCount": statuses.count("deferred"),
        "ready": all(status == "executable" for status in statuses),
    }
    if runtime_summary != expected_runtime_summary:
        raise NeutralDependencyPackCompilerError("neutral dependency runtime readiness drifted")


__all__ = [
    "EXPECTED_DEPENDENCIES",
    "EXPECTED_CANONICAL_COUNTS",
    "NeutralDependencyPackCompilerError",
    "compile_neutral_dependency_pack_artifact",
    "discover_neutral_dependencies",
    "validate_neutral_dependency_pack_artifact",
    "validate_neutral_dependency_plan",
]
