from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from functools import lru_cache
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.neutral_dependency_pack_compiler import (
    EXPECTED_DEPENDENCIES,
    NeutralDependencyPackCompilerError,
    compile_neutral_dependency_pack_artifact,
    discover_neutral_dependencies,
    validate_neutral_dependency_pack_artifact,
)
from openbfme_importer.neutral_mob_catalog import _structure_admission
from openbfme_importer.neutral_pack_profile import _resource_ownership as _profile_resource_ownership
from openbfme_importer.playable_structure_compiler import (
    compile_playable_structure_descriptor,
)
from openbfme_importer.playable_structure_pack_compiler import _digest
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.retail_visual_closure import build_retail_visual_closure


ROOT = Path(__file__).resolve().parents[2]
PRIVATE = ROOT / "workspace" / "retail-work"
EDITION_PATHS = {
    "bfme2": {
        "catalog": PRIVATE / "catalog" / "bfme2.json",
        "neutral": PRIVATE / "reports" / "bfme2-neutral-mob-catalog.json",
        "assets": PRIVATE / "cache" / "effective-assets",
    },
    "rotwk": {
        "catalog": PRIVATE / "editions" / "rotwk" / "catalog" / "rotwk.json",
        "neutral": PRIVATE
        / "editions"
        / "rotwk"
        / "reports"
        / "rotwk-neutral-mob-catalog.json",
        "assets": PRIVATE
        / "editions"
        / "rotwk"
        / "cache"
        / "layered-effective-assets",
    },
}


@lru_cache(maxsize=2)
def _retail_dependency_artifact(game: str) -> dict[str, object]:
    paths = EDITION_PATHS[game]
    if not all(path.exists() for path in paths.values()):
        pytest.skip(f"{game} private retail dependency fixture is unavailable")
    documents = dict(
        read_catalog_documents(InstallCatalog.load(paths["catalog"]))
    )
    prepared = prepare_playable_unit_compiler(documents)
    catalog = json.loads(paths["neutral"].read_text(encoding="utf-8"))
    # Generated reports may predate a newly typed module contract. Recompile
    # only the canonical structure descriptors that own the dependency graph;
    # the 69/83 catalog membership and denominator remain unchanged.
    for row in catalog["neutralMobs"]:
        if row["runtimeDomain"] == "structure":
            row["descriptor"] = compile_playable_structure_descriptor(
                row["objectId"],
                documents,
                prepared=prepared,
                game=game,
                scenario_admission=_structure_admission(row["role"]),
            )
    catalog["catalogSha256"] = _digest(
        {key: value for key, value in catalog.items() if key != "catalogSha256"}
    )
    artifacts = [
        {
            "objectId": row["objectId"],
            "role": row["role"],
            "runtimeDomain": row["runtimeDomain"],
            "descriptor": row["descriptor"],
            "artifactSha256": hashlib.sha256(row["objectId"].encode()).hexdigest(),
        }
        for row in catalog["neutralMobs"]
    ]
    plan = discover_neutral_dependencies(
        catalog, artifacts, documents, game=game
    )
    closure = build_retail_visual_closure(
        paths["assets"], plan["pickupObjectIds"]
    )
    return compile_neutral_dependency_pack_artifact(
        plan,
        documents,
        closure,
        game=game,
        prepared=prepared,
    )


@pytest.mark.parametrize(
    ("game", "pickup_ids", "runtime_summary"),
    (
        (
            "bfme2",
            ["TreasureChest1", "TreasureChest2"],
            {
                "contractCount": 22,
                "executableCount": 22,
                "deferredCount": 0,
                "ready": True,
            },
        ),
        (
            "rotwk",
            ["TreasureChest1"],
            {
                "contractCount": 32,
                "executableCount": 32,
                "deferredCount": 0,
                "ready": True,
            },
        ),
    ),
)
def test_retail_lair_helper_and_treasure_graph_is_exact_and_sealed(
    game: str,
    pickup_ids: list[str],
    runtime_summary: dict[str, object],
) -> None:
    artifact = _retail_dependency_artifact(game)
    validate_neutral_dependency_pack_artifact(artifact)
    assert artifact["summary"] == EXPECTED_DEPENDENCIES[game]
    assert artifact["plan"]["canonicalObjectCount"] == (
        (69 if game == "bfme2" else 83)
        + artifact["plan"].get("mapPlacementAddedCount", 0)
    )
    assert artifact["plan"]["pickupObjectIds"] == pickup_ids
    assert artifact["runtimeSummary"] == runtime_summary
    assert [row["objectId"] for row in artifact["pickupArtifacts"]] == pickup_ids
    assert all(
        row["runtimeDomain"] == "active-pickup"
        and row["runtimeStatus"] == "executable"
        and row["descriptor"]["production"] == []
        and row["descriptor"]["scenarioAdmission"]["surfaces"]
        == ["object-creation-list"]
        for row in artifact["pickupArtifacts"]
    )
    assert all(
        str(resource["id"]).startswith("neutral-pickup-")
        and str(resource.get("output", "")).startswith(
            "assets/models/neutral-pickups/"
        )
        for row in artifact["pickupArtifacts"]
        for resource in row["resourceOwnership"]["resources"]
        if resource["kind"] == "model"
    )
    for pickup in artifact["pickupArtifacts"]:
        assert pickup["resourceOwnership"] == _profile_resource_ownership(
            pickup["visualRecipe"], f"neutral pickup {pickup['objectId']}"
        )


def test_dependency_envelope_rejects_tampered_ocl_provenance() -> None:
    artifact = deepcopy(_retail_dependency_artifact("bfme2"))
    ocl = artifact["plan"]["objectCreationLists"][0]
    ocl["createObjects"][0]["line"] = 0
    ocl["semanticSha256"] = _digest(
        {key: value for key, value in ocl.items() if key != "semanticSha256"}
    )
    artifact["plan"]["planSha256"] = _digest(
        {
            key: value
            for key, value in artifact["plan"].items()
            if key != "planSha256"
        }
    )
    artifact["planSha256"] = artifact["plan"]["planSha256"]
    artifact["artifactSha256"] = _digest(
        {
            key: value
            for key, value in artifact.items()
            if key != "artifactSha256"
        }
    )
    with pytest.raises(
        NeutralDependencyPackCompilerError, match="leaf provenance is invalid"
    ):
        validate_neutral_dependency_pack_artifact(artifact)
