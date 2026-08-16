"""Compose one edition's complete scenario-neutral catalog into a cook profile.

This is deliberately a separate pack lane.  Neutral objects use the ordinary
unit/structure conversion recipes, but their authored scenario admission must
never make them faction-buildable or publish them through HUD registries.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy
import re

from .neutral_mob_catalog import (
    neutral_unit_passive_runtime_ready,
    validate_neutral_mob_catalog,
)
from .neutral_dependency_pack_compiler import (
    validate_neutral_dependency_pack_artifact,
)
from .neutral_prop_pack_compiler import validate_neutral_prop_pack_artifact
from .neutral_structure_pack_compiler import validate_neutral_structure_pack_artifact
from .neutral_custom_animation_presentation import (
    validate_neutral_custom_animation_presentation,
)
from .playable_structure_pack_compiler import _digest
from .playable_unit_compiler import validate_playable_unit_descriptor
from .playable_unit_pack_compiler import validate_playable_unit_pack_recipe
from .profile import assert_input_resource_references_resolve


SCHEMA = "openbfme.neutral-pack-profile-receipt"
SCHEMA_VERSION = 0
UNIT_ARTIFACT_SCHEMA = "openbfme.neutral-unit-pack-artifact"
UNIT_ARTIFACT_SCHEMA_VERSION = 1
EXPECTED_COUNTS = {"bfme2": 69, "rotwk": 83}
_SHA256 = re.compile(r"[0-9a-f]{64}")


class NeutralPackProfileError(ValueError):
    """The neutral corpus is not complete and safe to cook as its own pack."""


def _slug(value: str) -> str:
    result = "".join(character.lower() for character in value if character.isalnum())
    if not result:
        raise NeutralPackProfileError("neutral Object id has no safe slug")
    return result


def _digest_field(value: object, label: str) -> str:
    if not isinstance(value, str) or _SHA256.fullmatch(value) is None:
        raise NeutralPackProfileError(f"{label} is not a sha256 digest")
    return value


def _scenario_admission(descriptor: Mapping[str, object], label: str) -> dict[str, object]:
    production = descriptor.get("production")
    admission = descriptor.get("scenarioAdmission")
    if production not in ([], {"evidence": "authored-neutral-map", "routes": []}):
        raise NeutralPackProfileError(f"{label} is faction-producible")
    if not isinstance(admission, Mapping):
        raise NeutralPackProfileError(f"{label} has no scenario admission")
    role = admission.get("role")
    surfaces = admission.get("surfaces")
    if (
        (role is not None and (not isinstance(role, str) or not role))
        or not isinstance(surfaces, list)
        or not surfaces
        or any(not isinstance(item, str) or not item for item in surfaces)
        or len({item.casefold() for item in surfaces}) != len(surfaces)
    ):
        raise NeutralPackProfileError(f"{label} scenario admission is invalid")
    return deepcopy(dict(admission))


def _resource_ownership(recipe: Mapping[str, object], label: str) -> dict[str, object]:
    raw = recipe.get("resources")
    if not isinstance(raw, list) or not raw:
        raise NeutralPackProfileError(f"{label} has no conversion resources")
    rows: list[dict[str, object]] = []
    ids: set[str] = set()
    outputs: set[str] = set()
    for value in raw:
        if not isinstance(value, Mapping) or not isinstance(value.get("id"), str):
            raise NeutralPackProfileError(f"{label} has an invalid resource")
        row = deepcopy(dict(value))
        identifier = str(row["id"])
        folded = identifier.casefold()
        if folded in ids:
            raise NeutralPackProfileError(f"{label} duplicates resource {identifier}")
        ids.add(folded)
        output = row.get("output")
        if output is not None:
            if not isinstance(output, str) or not output:
                raise NeutralPackProfileError(f"{label} has an invalid resource output")
            if output.casefold() in outputs:
                raise NeutralPackProfileError(f"{label} duplicates output {output}")
            outputs.add(output.casefold())
        rows.append(row)
    return {
        "resourceIds": sorted((str(row["id"]) for row in rows), key=str.casefold),
        "outputs": sorted(
            (str(row["output"]) for row in rows if row.get("output")),
            key=str.casefold,
        ),
        "resources": sorted(rows, key=lambda row: str(row["id"]).casefold()),
    }


def compile_neutral_unit_pack_artifact(
    descriptor: Mapping[str, object],
    visual_closure: Mapping[str, object],
    recipe: Mapping[str, object],
    *,
    game: str,
    catalog_descriptor: Mapping[str, object],
) -> dict[str, object]:
    """Seal source-catalog and media-integrated identities for one unit."""

    if game not in EXPECTED_COUNTS:
        raise NeutralPackProfileError(f"unsupported neutral pack game {game!r}")
    try:
        validate_playable_unit_descriptor(descriptor)
        validate_playable_unit_descriptor(catalog_descriptor)
        validate_playable_unit_pack_recipe(recipe)
    except ValueError as exc:
        raise NeutralPackProfileError(f"neutral unit artifact is invalid: {exc}") from exc
    object_id = descriptor.get("objectId")
    if not isinstance(object_id, str) or not object_id:
        raise NeutralPackProfileError("neutral unit descriptor identity is invalid")
    if recipe.get("objectId") != object_id:
        raise NeutralPackProfileError("neutral unit recipe identity drifted")
    descriptor_sha = _digest_field(
        descriptor.get("descriptorSha256"), "neutral unit descriptor digest"
    )
    catalog_object_id = catalog_descriptor.get("objectId")
    catalog_descriptor_sha = _digest_field(
        catalog_descriptor.get("descriptorSha256"),
        "neutral unit catalog descriptor digest",
    )
    if catalog_object_id != object_id:
        raise NeutralPackProfileError("neutral unit catalog object identity drifted")
    if recipe.get("descriptorSha256") != descriptor_sha:
        raise NeutralPackProfileError("neutral unit recipe descriptor binding drifted")
    closure_unsigned = dict(visual_closure)
    closure_sha = closure_unsigned.pop("aggregateSha256", None)
    if not isinstance(closure_sha, str) or closure_sha != _digest(closure_unsigned):
        raise NeutralPackProfileError("neutral unit visual closure digest is invalid")
    if recipe.get("visualClosureSha256") != closure_sha:
        raise NeutralPackProfileError("neutral unit recipe closure binding drifted")
    admission = _scenario_admission(descriptor, f"neutral unit {object_id}")
    catalog_admission = _scenario_admission(
        catalog_descriptor, f"neutral unit catalog {object_id}"
    )
    if descriptor.get("production") != [] or catalog_descriptor.get("production") != []:
        raise NeutralPackProfileError(
            f"neutral unit {object_id} catalog/integrated production drifted"
        )
    if catalog_admission != admission:
        raise NeutralPackProfileError(
            f"neutral unit {object_id} catalog/integrated admission drifted"
        )
    if not isinstance(admission.get("role"), str) or any(
        field not in admission for field in ("declarationKind", "sourceIni", "line")
    ):
        raise NeutralPackProfileError(
            f"neutral unit {object_id} admission identity is incomplete"
        )
    registration = recipe.get("runtimeRegistration")
    if not isinstance(registration, Mapping) or registration.get(
        "scenarioAdmission"
    ) != admission:
        raise NeutralPackProfileError("neutral unit runtime admission drifted")
    ownership = _resource_ownership(recipe, f"neutral unit {object_id}")
    artifact: dict[str, object] = {
        "schema": UNIT_ARTIFACT_SCHEMA,
        "schemaVersion": UNIT_ARTIFACT_SCHEMA_VERSION,
        "game": game,
        "objectId": object_id,
        "role": admission["role"],
        "runtimeDomain": "unit",
        "catalogDescriptorSha256": catalog_descriptor_sha,
        "integratedDescriptorSha256": descriptor_sha,
        "catalogDescriptor": deepcopy(dict(catalog_descriptor)),
        "descriptor": deepcopy(dict(descriptor)),
        "visualClosure": deepcopy(dict(visual_closure)),
        "visualRecipe": deepcopy(dict(recipe)),
        "resourceOwnership": ownership,
        "sourceIdentity": {
            field: admission[field]
            for field in ("declarationKind", "sourceIni", "line")
        },
    }
    artifact["artifactSha256"] = _digest(artifact)
    return artifact


def validate_neutral_unit_pack_artifact(value: Mapping[str, object]) -> None:
    if value.get("schema") != UNIT_ARTIFACT_SCHEMA or value.get(
        "schemaVersion"
    ) != UNIT_ARTIFACT_SCHEMA_VERSION:
        raise NeutralPackProfileError("neutral unit artifact schema is invalid")
    unsigned = dict(value)
    artifact_sha = unsigned.pop("artifactSha256", None)
    if not isinstance(artifact_sha, str) or artifact_sha != _digest(unsigned):
        raise NeutralPackProfileError("neutral unit artifact digest is invalid")
    descriptor = value.get("descriptor")
    catalog_descriptor = value.get("catalogDescriptor")
    closure = value.get("visualClosure")
    recipe = value.get("visualRecipe")
    if not all(
        isinstance(item, Mapping)
        for item in (catalog_descriptor, descriptor, closure, recipe)
    ):
        raise NeutralPackProfileError("neutral unit artifact documents are invalid")
    assert isinstance(descriptor, Mapping)
    assert isinstance(catalog_descriptor, Mapping)
    assert isinstance(closure, Mapping)
    assert isinstance(recipe, Mapping)
    rebuilt = compile_neutral_unit_pack_artifact(
        descriptor,
        closure,
        recipe,
        game=str(value.get("game", "")),
        catalog_descriptor=catalog_descriptor,
    )
    # ``compile`` intentionally does not call back into this validator: the
    # exact deterministic rebuild is the validator's final ownership check.
    if rebuilt != value:
        raise NeutralPackProfileError("neutral unit artifact ownership drifted")


def _validate_catalog(catalog: Mapping[str, object]) -> tuple[str, list[Mapping[str, object]]]:
    try:
        validate_neutral_mob_catalog(catalog)
    except ValueError as exc:
        raise NeutralPackProfileError(f"neutral catalog is invalid: {exc}") from exc
    game = str(catalog.get("game", ""))
    rows = catalog.get("neutralMobs")
    assert isinstance(rows, list)
    baseline = EXPECTED_COUNTS.get(game)
    summary = catalog.get("summary")
    if not isinstance(summary, Mapping):
        raise NeutralPackProfileError("neutral catalog summary is invalid")
    map_added = summary.get("mapPlacementAddedCount", 0)
    if not isinstance(map_added, int) or isinstance(map_added, bool) or map_added < 0:
        raise NeutralPackProfileError(
            "neutral catalog map-placement addition count is invalid"
        )
    expected = None if baseline is None else baseline + map_added
    if expected is None or len(rows) != expected:
        raise NeutralPackProfileError(
            f"{game or 'unknown'} neutral catalog has {len(rows)} rows; expected {expected}"
        )
    if any(row.get("runtimeStatus") != "descriptor-ready" for row in rows):
        raise NeutralPackProfileError("neutral catalog contains a deferred descriptor")
    for row in rows:
        if row.get("runtimeDomain") == "unit" and not neutral_unit_passive_runtime_ready(
            row.get("descriptor", {})
        ):
            raise NeutralPackProfileError(
                f"neutral {row.get('objectId', '')} passive simulation is not runtime-ready"
            )
    return game, rows


def _unit_runtime(artifact: Mapping[str, object]) -> dict[str, object]:
    descriptor = artifact["descriptor"]
    recipe = artifact["visualRecipe"]
    ownership = artifact["resourceOwnership"]
    assert isinstance(descriptor, Mapping)
    assert isinstance(recipe, Mapping)
    assert isinstance(ownership, Mapping)
    return {
        "schema": "openbfme.playable-unit-runtime",
        "schemaVersion": 0,
        "objectId": artifact["objectId"],
        "category": recipe["category"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": recipe["recipeSha256"],
        "resourceIds": ownership["resourceIds"],
        "registration": deepcopy(recipe["runtimeRegistration"]),
    }


def compose_neutral_pack_profile(
    catalog: Mapping[str, object],
    artifacts: Sequence[Mapping[str, object]],
    *,
    dependency_artifact: Mapping[str, object],
    version: str,
) -> dict[str, object]:
    """Emit deterministic profile inputs only when the edition is 100% ready."""

    game, rows = _validate_catalog(catalog)
    if not isinstance(version, str) or not version or len(version) > 64:
        raise NeutralPackProfileError("neutral pack version is invalid")
    by_id: dict[str, Mapping[str, object]] = {}
    for artifact in artifacts:
        if not isinstance(artifact, Mapping) or not isinstance(
            artifact.get("objectId"), str
        ):
            raise NeutralPackProfileError("neutral recipe artifact is invalid")
        folded = str(artifact["objectId"]).casefold()
        if folded in by_id:
            raise NeutralPackProfileError(
                f"duplicate neutral artifact identity: {artifact['objectId']}"
            )
        by_id[folded] = artifact
    expected_ids = {str(row["objectId"]).casefold() for row in rows}
    if set(by_id) != expected_ids:
        missing = sorted(expected_ids - set(by_id))
        extra = sorted(set(by_id) - expected_ids)
        raise NeutralPackProfileError(
            f"neutral artifact set is not exact; missing={missing[:5]} extra={extra[:5]}"
        )
    try:
        validate_neutral_dependency_pack_artifact(dependency_artifact)
    except ValueError as exc:
        raise NeutralPackProfileError(
            f"neutral dependency artifact is invalid: {exc}"
        ) from exc
    if (
        dependency_artifact.get("game") != game
        or dependency_artifact.get("catalogSha256") != catalog.get("catalogSha256")
    ):
        raise NeutralPackProfileError("neutral dependency artifact catalog binding drifted")
    runtime_summary = dependency_artifact.get("runtimeSummary")
    if not isinstance(runtime_summary, Mapping) or runtime_summary.get("ready") is not True:
        deferred = (
            runtime_summary.get("deferredCount")
            if isinstance(runtime_summary, Mapping)
            else "unknown"
        )
        raise NeutralPackProfileError(
            f"neutral dependency gameplay contracts are deferred: {deferred}"
        )

    resources: list[dict[str, object]] = []
    runtime_data: dict[str, object] = {}
    files: dict[str, str] = {}
    receipt_rows: list[dict[str, object]] = []
    owned_ids: set[str] = set()
    owned_outputs: set[str] = set()
    owned_resource_rows: dict[str, dict[str, object]] = {}
    owned_output_rows: dict[str, dict[str, object]] = {}
    for row in sorted(rows, key=lambda item: str(item["objectId"]).casefold()):
        object_id = str(row["objectId"])
        domain = str(row["runtimeDomain"])
        artifact = by_id[object_id.casefold()]
        if artifact.get("game") != game or artifact.get("objectId") != object_id:
            raise NeutralPackProfileError(f"neutral artifact identity drifted: {object_id}")
        if domain == "unit":
            validate_neutral_unit_pack_artifact(artifact)
            catalog_descriptor = artifact.get("catalogDescriptor")
            assert isinstance(catalog_descriptor, Mapping)
            catalog_sha = row["descriptor"].get("descriptorSha256")
            if artifact.get("catalogDescriptorSha256") != catalog_sha:
                raise NeutralPackProfileError(
                    f"neutral {object_id} catalog binding drifted"
                )
            if catalog_descriptor != row["descriptor"]:
                raise NeutralPackProfileError(
                    f"neutral {object_id} catalog descriptor drifted"
                )
            runtime = _unit_runtime(artifact)
            recipe = artifact["visualRecipe"]
            runtime_path = f"data/playable-units/{_slug(object_id)}.json"
            file_key = f"playableUnit.{_slug(object_id)}"
        elif domain == "structure":
            try:
                validate_neutral_structure_pack_artifact(artifact)
            except ValueError as exc:
                raise NeutralPackProfileError(
                    f"neutral structure {object_id} is invalid: {exc}"
                ) from exc
            runtime = artifact["runtime"]
            recipe = artifact["visualRecipe"]
            runtime_path = f"data/playable-structures/{_slug(object_id)}.json"
            file_key = f"playableStructure.{_slug(object_id)}"
        elif domain == "prop":
            try:
                validate_neutral_prop_pack_artifact(artifact)
            except ValueError as exc:
                raise NeutralPackProfileError(
                    f"neutral prop {object_id} is invalid: {exc}"
                ) from exc
            runtime = artifact["runtime"]
            recipe = artifact["visualRecipe"]
            runtime_path = f"data/neutral-props/{_slug(object_id)}.json"
            file_key = f"neutralProp.{_slug(object_id)}"
        else:
            raise NeutralPackProfileError(f"unknown neutral domain {domain!r}")
        descriptor = artifact.get("descriptor")
        if not isinstance(descriptor, Mapping):
            raise NeutralPackProfileError(f"neutral {object_id} has no descriptor")
        if domain == "unit":
            if artifact.get("integratedDescriptorSha256") != descriptor.get(
                "descriptorSha256"
            ) or recipe.get("descriptorSha256") != descriptor.get("descriptorSha256"):
                raise NeutralPackProfileError(
                    f"neutral {object_id} integrated descriptor binding drifted"
                )
        elif descriptor.get("descriptorSha256") != row["descriptor"].get(
            "descriptorSha256"
        ):
            raise NeutralPackProfileError(f"neutral {object_id} catalog binding drifted")
        admission = _scenario_admission(descriptor, f"neutral {object_id}")
        if admission != row["descriptor"].get("scenarioAdmission"):
            raise NeutralPackProfileError(f"neutral {object_id} admission drifted")
        if artifact.get("role") not in (None, row.get("role")):
            # Structure artifacts normalize ordinary structures to this role.
            if not (
                domain == "structure"
                and row.get("role") != "lair"
                and artifact.get("role") == "neutral-structure"
            ):
                raise NeutralPackProfileError(f"neutral {object_id} role drifted")
        ownership = _resource_ownership(recipe, f"neutral {object_id}")
        if artifact.get("resourceOwnership") not in (None, ownership):
            artifact_ownership = artifact.get("resourceOwnership")
            if (
                not isinstance(artifact_ownership, Mapping)
                or artifact_ownership.get("resourceIds")
                != ownership["resourceIds"]
            ):
                raise NeutralPackProfileError(
                    f"neutral {object_id} resource ownership drifted"
                )
        for resource in ownership["resources"]:
            identifier = str(resource["id"])
            if identifier.casefold() in owned_ids:
                raise NeutralPackProfileError(f"resource has multiple owners: {identifier}")
            owned_ids.add(identifier.casefold())
            output = resource.get("output")
            if output is not None:
                if str(output).casefold() in owned_outputs:
                    raise NeutralPackProfileError(f"output has multiple owners: {output}")
                owned_outputs.add(str(output).casefold())
            copied_resource = deepcopy(resource)
            resources.append(copied_resource)
            owned_resource_rows[identifier.casefold()] = copied_resource
            if output is not None:
                owned_output_rows[str(output).casefold()] = copied_resource
        custom_presentation = artifact.get("customAnimationPresentation")
        if custom_presentation is not None:
            if domain != "structure" or not isinstance(custom_presentation, Mapping):
                raise NeutralPackProfileError(
                    f"neutral {object_id} custom animation prerequisite is invalid"
                )
            try:
                validate_neutral_custom_animation_presentation(custom_presentation)
            except ValueError as exc:
                raise NeutralPackProfileError(
                    f"neutral {object_id} custom animation prerequisite is invalid: {exc}"
                ) from exc
            closure = custom_presentation["particleClosure"]
            assert isinstance(closure, Mapping)
            closure_resources = closure.get("resources")
            if not isinstance(closure_resources, list) or not closure_resources:
                raise NeutralPackProfileError(
                    f"neutral {object_id} custom animation has no sealed resources"
                )
            for resource_value in closure_resources:
                if not isinstance(resource_value, Mapping):
                    raise NeutralPackProfileError(
                        f"neutral {object_id} custom animation resource is invalid"
                    )
                resource = deepcopy(dict(resource_value))
                identifier = str(resource.get("id", ""))
                output = str(resource.get("output", ""))
                prior = owned_resource_rows.get(identifier.casefold())
                prior_output = owned_output_rows.get(output.casefold())
                if prior is not None or prior_output is not None:
                    # These two edition-scoped particle definitions are shared
                    # by every affected lair.  Deduplication is legal only for
                    # byte-identical conversion contracts.
                    if prior != resource or (prior_output is not None and prior_output != resource):
                        raise NeutralPackProfileError(
                            f"neutral {object_id} custom animation resource ownership drifted"
                        )
                    continue
                resources.append(resource)
                owned_ids.add(identifier.casefold())
                owned_outputs.add(output.casefold())
                owned_resource_rows[identifier.casefold()] = resource
                owned_output_rows[output.casefold()] = resource
        runtime_data[runtime_path] = deepcopy(runtime)
        files[file_key] = runtime_path
        receipt_row = {
                "objectId": object_id,
                "runtimeDomain": domain,
                "role": row["role"],
                "artifactSha256": _digest_field(
                    artifact.get("artifactSha256"), f"neutral {object_id} artifact digest"
                ),
                "descriptorSha256": descriptor["descriptorSha256"],
                "recipeSha256": recipe["recipeSha256"],
                "runtimePath": runtime_path,
                "packFileKey": file_key,
                "resourceIds": ownership["resourceIds"],
            }
        if domain == "unit":
            receipt_row["catalogDescriptorSha256"] = artifact[
                "catalogDescriptorSha256"
            ]
            receipt_row["integratedDescriptorSha256"] = artifact[
                "integratedDescriptorSha256"
            ]
        elif domain == "prop" and isinstance(artifact.get("deathFxClosure"), Mapping):
            receipt_row["deathFxClosureSha256"] = _digest_field(
                artifact["deathFxClosure"].get("aggregateSha256"),
                f"neutral prop {object_id} death FX closure digest",
            )
        if domain == "prop":
            module_evidence = runtime.get("runtimeModuleEvidence")
            runtime_capabilities = runtime.get("runtimeCapabilities")
            if not isinstance(module_evidence, list) or not isinstance(
                runtime_capabilities, list
            ):
                raise NeutralPackProfileError(
                    f"neutral prop {object_id} runtime evidence is invalid"
                )
            receipt_row.update(
                {
                    "runtimeDescriptorSha256": _digest_field(
                        runtime.get("descriptorSha256"),
                        f"neutral prop {object_id} runtime descriptor digest",
                    ),
                    "runtimeModuleEvidenceCount": len(module_evidence),
                    "runtimeModuleEvidenceSha256": _digest(module_evidence),
                    "runtimeCapabilityCount": len(runtime_capabilities),
                    "runtimeCapabilitiesSha256": _digest(runtime_capabilities),
                }
            )
        elif domain == "structure" and isinstance(custom_presentation, Mapping):
            receipt_row["customAnimationPresentationSha256"] = _digest_field(
                custom_presentation.get("requestSha256"),
                f"neutral structure {object_id} custom animation prerequisite digest",
            )
            receipt_row["customAnimationEdgeCount"] = len(
                custom_presentation.get("edgeIds", [])
            )
        receipt_rows.append(receipt_row)

    pickup_receipt_rows: list[dict[str, object]] = []
    pickup_artifacts = dependency_artifact.get("pickupArtifacts")
    assert isinstance(pickup_artifacts, list)
    for pickup in sorted(
        pickup_artifacts, key=lambda item: str(item["objectId"]).casefold()
    ):
        object_id = str(pickup["objectId"])
        if object_id.casefold() in expected_ids:
            raise NeutralPackProfileError(
                f"neutral dependency duplicates canonical object: {object_id}"
            )
        descriptor = pickup["descriptor"]
        recipe = pickup["visualRecipe"]
        runtime = pickup["runtime"]
        assert isinstance(descriptor, Mapping)
        assert isinstance(recipe, Mapping)
        assert isinstance(runtime, Mapping)
        ownership = _resource_ownership(recipe, f"neutral pickup {object_id}")
        if pickup.get("resourceOwnership") != ownership:
            raise NeutralPackProfileError(
                f"neutral pickup {object_id} resource ownership drifted"
            )
        for resource in ownership["resources"]:
            identifier = str(resource["id"])
            if identifier.casefold() in owned_ids:
                raise NeutralPackProfileError(
                    f"resource has multiple owners: {identifier}"
                )
            owned_ids.add(identifier.casefold())
            output = resource.get("output")
            if output is not None:
                if str(output).casefold() in owned_outputs:
                    raise NeutralPackProfileError(
                        f"output has multiple owners: {output}"
                    )
                owned_outputs.add(str(output).casefold())
            resources.append(deepcopy(resource))
        runtime_path = f"data/neutral-pickups/{_slug(object_id)}.json"
        file_key = f"neutralPickup.{_slug(object_id)}"
        runtime_data[runtime_path] = deepcopy(dict(runtime))
        files[file_key] = runtime_path
        pickup_receipt_rows.append(
            {
                "objectId": object_id,
                "runtimeDomain": "active-pickup",
                "artifactSha256": _digest_field(
                    pickup.get("artifactSha256"),
                    f"neutral pickup {object_id} artifact digest",
                ),
                "descriptorSha256": descriptor["descriptorSha256"],
                "recipeSha256": recipe["recipeSha256"],
                "runtimeSha256": runtime["runtimeSha256"],
                "runtimePath": runtime_path,
                "packFileKey": file_key,
                "resourceIds": ownership["resourceIds"],
            }
        )

    dependency_graph_path = "data/neutral/dependency-graph.json"
    runtime_data[dependency_graph_path] = deepcopy(dependency_artifact["plan"])
    files["neutralDependencyGraph"] = dependency_graph_path
    try:
        assert_input_resource_references_resolve(resources, label="neutral pack profile")
    except ValueError as exc:
        raise NeutralPackProfileError(str(exc)) from exc
    receipt: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "catalogSha256": catalog["catalogSha256"],
        "objectCount": len(receipt_rows),
        "rows": receipt_rows,
        "dependencyArtifactSha256": dependency_artifact["artifactSha256"],
        "dependencyObjectCount": len(pickup_receipt_rows),
        "dependencyRows": pickup_receipt_rows,
        "dependencySummary": deepcopy(dependency_artifact["summary"]),
    }
    receipt["receiptSha256"] = _digest(receipt)
    receipt_path = "data/neutral/pack-profile-receipt.json"
    runtime_data[receipt_path] = receipt
    files["neutralPackProfileReceipt"] = receipt_path
    pack_id = f"{game}-neutral-vslice"
    return {
        "format": 1,
        "id": pack_id,
        "title": f"Open-BFME {game.upper()} Scenario-Neutral Pack",
        "pack": {
            "id": pack_id,
            "version": version,
            "files": dict(sorted(files.items(), key=lambda item: item[0].casefold())),
            "neutralCatalogSha256": catalog["catalogSha256"],
            "neutralProfileReceiptSha256": receipt["receiptSha256"],
            "neutralDependencyArtifactSha256": dependency_artifact[
                "artifactSha256"
            ],
        },
        "resources": sorted(resources, key=lambda item: str(item["id"]).casefold()),
        "runtime_data": dict(
            sorted(runtime_data.items(), key=lambda item: item[0].casefold())
        ),
    }


__all__ = [
    "EXPECTED_COUNTS",
    "NeutralPackProfileError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "UNIT_ARTIFACT_SCHEMA",
    "UNIT_ARTIFACT_SCHEMA_VERSION",
    "compile_neutral_unit_pack_artifact",
    "compose_neutral_pack_profile",
    "validate_neutral_unit_pack_artifact",
]
