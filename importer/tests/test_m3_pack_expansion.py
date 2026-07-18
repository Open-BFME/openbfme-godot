from __future__ import annotations

import json
import inspect
import hashlib
from pathlib import Path

import pytest
import openbfme_importer.m3_pack_expansion as m3_module

from openbfme_importer.m3_pack_expansion import (
    BUILDINGS,
    BUILDING_RUNTIME_PATH,
    BUILDING_RUNTIME_REQUESTED_IDS,
    RANGER_RUNTIME_PATH,
    RANGER_RUNTIME_SCHEMA,
    RUNTIME_PATHS,
    UNITS,
    UPGRADES,
    attach_building_runtime_gap_contract,
    attach_ranger_playable_bindings,
    build_upgrade_manifest,
    build_building_runtime_gap_contract,
    build_m3_visual_resources,
    build_ranger_runtime_contract,
    candidate_pack_state,
    declarative_visual_resources,
    extract_building_stats,
    extend_selection_transitions,
    SELECTION_TRANSITIONS,
    validate_candidate_pack_state,
    validate_recipe,
    validated_private_output_path,
)
from openbfme_importer.profile import ImportProfile
from openbfme_importer.util import write_json_atomic


ROOT = Path(__file__).resolve().parents[2]
PROFILE_PATH = ROOT / "importer" / "profiles" / "men-fords-v1.json"


def load_recipe() -> dict:
    return json.loads(PROFILE_PATH.read_text(encoding="utf-8"))


def fixture_report() -> dict:
    objects, command_sets, command_buttons, images = [], [], [], []
    for index, building in enumerate(BUILDINGS):
        set_id, button_id, construct_id, image_id = f"Set_{index}", f"Command_{index}", f"Construct_{index}", f"Image_{index}"
        objects.append({"id": building, "edges": [
            {"field": "CommandSet", "targetId": set_id},
            {"field": "SelectPortrait", "targetId": image_id},
        ]})
        command_sets.append({"id": set_id, "buttons": [button_id]})
        command_buttons.append({"id": button_id, "fields": {"ButtonImage": [image_id]}})
        command_buttons.append({"id": construct_id, "fields": {"ButtonImage": [image_id], "Command": ["DOZER_CONSTRUCT"], "Object": [building]}})
        images.append({"id": image_id})
    upgrades = []
    for index, upgrade in enumerate(UPGRADES):
        image_id = f"UpgradeImage_{index}"
        images.append({"id": image_id})
        upgrades.append({"id": upgrade, "definitionSha256": f"{index + 1:064x}", "references": {"mappedImages": [image_id], "localizedStrings": [f"UPGRADE:Fixture{index}"]}})
    return {
        "definitions": {"objects": objects, "commandSets": command_sets, "commandButtons": command_buttons, "upgrades": upgrades},
        "resolvedLeaves": {"mappedImages": images},
        "dependencies": {"spellbookSciences": [], "spellbookSpecialPowers": []},
    }


def test_tracked_v1_is_payload_free_bounded_and_source_gap_honest() -> None:
    recipe = load_recipe()
    loaded = ImportProfile.load(PROFILE_PATH)
    metadata = validate_recipe(recipe)
    assert loaded.id == "men-fords-v1"
    assert tuple(metadata["targets"]["buildings"]) == BUILDINGS
    assert tuple(metadata["targets"]["units"]) == UNITS
    assert tuple(metadata["targets"]["upgrades"]) == UPGRADES
    assert recipe["runtime_data"] == {}
    assert recipe["pack"]["full_faction_complete"] is False
    assert recipe["pack"]["m3ScopeKind"] == "bounded-explicit-targets"
    assert recipe["pack"]["m3CoverageDenominatorComplete"] is False
    gaps = {row["id"] for row in metadata["sourceNull"]}
    assert gaps == {"MenBatteringRam", "GBWallrampart.GBWallrampart", "GUCavalry_ATRA", "BFME2Logo"}
    serialized = PROFILE_PATH.read_text(encoding="utf-8")
    assert "GoodCommandPointLimit\"" not in serialized
    assert "SciencePurchasePointCost" not in serialized
    assert "CONTROLBAR:" not in serialized
    semantic = next(
        row for row in recipe["resources"] if row["id"] == "m3-men-semantic-sources"
    )
    assert semantic["patterns"] == [
        "data/ini/housecolor.ini",
    ]


def test_candidate_pack_contract_never_promotes_known_gaps_to_completion() -> None:
    assert candidate_pack_state() == {
        "vertical_slice_complete": False,
        "m3SourceClosureComplete": False,
        "full_faction_complete": False,
        "asset_conversion_complete": False,
        "oracle_parity_complete": False,
        "capability_maturity": "m3-bounded-men-census-candidate",
        "m3KnownGapsReported": True,
        "m3ScopeKind": "bounded-explicit-targets",
        "m3CoverageDenominatorComplete": False,
    }
    validate_candidate_pack_state(candidate_pack_state())
    invalid = candidate_pack_state()
    invalid.pop("oracle_parity_complete")
    with pytest.raises(ValueError, match="oracle_parity_complete"):
        validate_candidate_pack_state(invalid)


def test_selection_transition_evidence_uses_scoped_status_not_completion_claims() -> None:
    outputs = {
        "GondorFighter": "assets/models/units/gondor-fighter.glb",
        "GondorArcher": "assets/models/units/gondor-archer.glb",
    }
    profile = {
        "resources": [
            {"id": target, "output": output, "patterns": [], "options": {"animations": []}}
            for target, output in outputs.items()
        ]
    }
    closure = {
        "exactLeaves": [
            {
                "targetObject": target,
                "kind": "animation",
                "identifier": identifier,
                "physicalVirtualPaths": [f"art/w3d/{identifier.casefold()}.w3d"],
            }
            for target, identifiers in SELECTION_TRANSITIONS.items()
            for identifier in identifiers
        ]
    }
    result = extend_selection_transitions(profile, closure)
    assert result["explicitTargetStatus"] == "resolved"
    assert "complete" not in result
    assert all(row["explicitTargetStatus"] == "resolved" and "complete" not in row for row in result["units"])


def test_generated_candidate_outputs_are_contained_below_private_root(tmp_path: Path) -> None:
    private_root = tmp_path / ".private"
    job_root = private_root / "scratch" / "jobs" / "fixture"
    public_root = tmp_path / "importer"
    job_root.mkdir(parents=True)
    public_root.mkdir()
    output = job_root / "candidate.json"
    assert validated_private_output_path(output, private_root) == output.resolve()
    with pytest.raises(ValueError, match="below the private root"):
        validated_private_output_path(public_root / "escaped.json", private_root)

    link = job_root / "escaped-link.json"
    try:
        link.symlink_to(public_root / "escaped.json")
    except OSError:
        return
    with pytest.raises(ValueError, match="symbolic link"):
        validated_private_output_path(link, private_root)


def test_declarative_visual_rules_are_deterministic_and_runtime_contract_is_complete() -> None:
    closure = {"scannedW3d": [{"virtualPath": f"art/w3d/m3/source-{i:03d}.w3d"} for i in range(334)]}
    rules = declarative_visual_resources(closure)
    assert [len(row["patterns"]) for row in rules] == [256, 78]
    assert all(row["converter"] == "hash-only" and row["required"] for row in rules)

    assert RUNTIME_PATHS["models"] == "data/m3/model-census.json"
    assert set(load_recipe()["pack"]["m3Recipe"]["runtimeOutputs"].values()) == set(RUNTIME_PATHS.values())


def test_trebuchet_recipe_preserves_retail_embedded_death_drawable() -> None:
    recipe = m3_module.UNIT_MODEL_RECIPES["GondorTrebuchet"]
    assert recipe["model"] == "art/w3d/gu/gusiegtreb_skn.w3d"
    assert recipe["animations"] == (
        "art/w3d/gu/gusiegtreb_idla.w3d",
        "art/w3d/gu/gusiegtreb_wlka.w3d",
        "art/w3d/gu/gusiegtreb_atak.w3d",
    )
    assert recipe["embedded_models"] == {
        "death": "art/w3d/gu/gusiegtreb_diea.w3d",
    }
    assert "m3-gondortrebuchet-rig-and-core-clips" not in (
        m3_module.CONVERSION_SOURCE_GAPS
    )


def test_initial_building_runtime_contract_is_hash_sealed_and_gap_only() -> None:
    recipe = load_recipe()
    base_profile_sha = hashlib.sha256(b"fixture base profile\n").hexdigest()
    recipe_sha = hashlib.sha256(PROFILE_PATH.read_bytes()).hexdigest()
    building_stats = {"schema": "openbfme.building-stats", "buildings": []}
    model_census = {"schema": "openbfme.m3-model-census", "models": []}

    document, descriptor = build_building_runtime_gap_contract(
        recipe, base_profile_sha, recipe_sha, building_stats, model_census
    )
    repeated, repeated_descriptor = build_building_runtime_gap_contract(
        recipe, base_profile_sha, recipe_sha, building_stats, model_census
    )

    assert document == repeated
    assert descriptor == repeated_descriptor
    assert descriptor == {
        "path": BUILDING_RUNTIME_PATH,
        "schema": "openbfme.building-runtime-capabilities",
        "schemaVersion": 0,
        "sha256": hashlib.sha256(
            (json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
                "utf-8"
            )
        ).hexdigest(),
    }
    assert document["scope"] == {
        "id": "bfme2-106-men-ordinary-buildings-v0",
        "requestedIds": list(BUILDING_RUNTIME_REQUESTED_IDS),
    }
    assert document["capabilities"] == []
    assert [row["sourceObjectId"] for row in document["gaps"]] == list(
        BUILDING_RUNTIME_REQUESTED_IDS
    )
    assert all(row["evidenceIds"] == [] and row["reasons"] for row in document["gaps"])
    assert set(document["provenance"]) == {
        "baseProfileInputSha256",
        "recipeSha256",
        "buildingStatsSha256",
        "modelCensusSha256",
    }
    assert document["provenance"]["baseProfileInputSha256"] == base_profile_sha
    assert document["provenance"]["recipeSha256"] == recipe_sha
    assert "complete" not in json.dumps(document).casefold()


def test_building_runtime_contract_attaches_to_composed_profile_once(tmp_path: Path) -> None:
    recipe = load_recipe()
    base_profile_sha = hashlib.sha256(b"fixture base profile\n").hexdigest()
    recipe_sha = hashlib.sha256(PROFILE_PATH.read_bytes()).hexdigest()
    profile = load_recipe()
    profile["pack"]["files"] = {"objects": "data/objects.json"}
    profile["runtime_data"] = {
        RUNTIME_PATHS["buildingStats"]: {
            "schema": "openbfme.building-stats",
            "buildings": [],
        },
        RUNTIME_PATHS["models"]: {
            "schema": "openbfme.m3-model-census",
            "models": [],
        },
    }

    document, descriptor = attach_building_runtime_gap_contract(
        profile, recipe, base_profile_sha, recipe_sha
    )

    assert profile["runtime_data"][BUILDING_RUNTIME_PATH] is document
    assert profile["pack"]["files"] == {
        "objects": "data/objects.json",
        "buildingRuntime": descriptor,
    }
    assert descriptor["sha256"] == hashlib.sha256(
        (json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
            "utf-8"
        )
    ).hexdigest()
    assert document["capabilities"] == []
    assert len(document["gaps"]) == len(BUILDING_RUNTIME_REQUESTED_IDS)

    profile_path = tmp_path / "composed-profile.json"
    write_json_atomic(profile_path, profile)
    loaded = ImportProfile.load(profile_path)
    assert loaded.pack_metadata["files"]["buildingRuntime"] == descriptor
    runtime_path = tmp_path / BUILDING_RUNTIME_PATH
    write_json_atomic(runtime_path, loaded.runtime_data[BUILDING_RUNTIME_PATH])
    assert hashlib.sha256(runtime_path.read_bytes()).hexdigest() == descriptor["sha256"]
    for runtime_key, provenance_key in (
        (RUNTIME_PATHS["buildingStats"], "buildingStatsSha256"),
        (RUNTIME_PATHS["models"], "modelCensusSha256"),
    ):
        artifact_path = tmp_path / runtime_key
        write_json_atomic(artifact_path, loaded.runtime_data[runtime_key])
        assert hashlib.sha256(artifact_path.read_bytes()).hexdigest() == document[
            "provenance"
        ][provenance_key]

    with pytest.raises(ValueError, match="already attached"):
        attach_building_runtime_gap_contract(
            profile, recipe, base_profile_sha, recipe_sha
        )


def test_path_composer_binds_parsed_inputs_to_the_same_raw_bytes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    private_root = tmp_path / ".private"
    private_root.mkdir()
    recipe_path = tmp_path / "recipe.json"
    base_path = tmp_path / "base.json"
    recipe_bytes = b'{\n  "pack": {"id": "fixture", "version": "1"}\n}\n'
    base_bytes = b'{"id":"base", "spacing": "is significant to the digest"}\n'
    recipe_path.write_bytes(recipe_bytes)
    base_path.write_bytes(base_bytes)
    other_paths = []
    expected_catalog_identity = "a" * 64
    for name in ("census.json", "visual.json"):
        path = tmp_path / name
        path.write_text("{}\n", encoding="utf-8", newline="\n")
        other_paths.append(path)
    manifest_path = tmp_path / "manifest.json"
    write_json_atomic(
        manifest_path,
        {"catalog": {"identity_sha256": expected_catalog_identity}},
    )
    other_paths.append(manifest_path)
    assets_root = tmp_path / "assets"
    assets_root.mkdir()
    output_path = private_root / "scratch" / "candidate.json"
    output_path.parent.mkdir()
    captured: dict[str, object] = {}

    def fake_compose(*args: object) -> dict[str, object]:
        captured["recipe"] = args[0]
        captured["base"] = args[1]
        captured["provenance"] = args[6]
        return {"fixture": True}

    monkeypatch.setattr(m3_module, "compose_private_profile", fake_compose)
    m3_module.compose_profile_from_paths(
        recipe_path,
        base_path,
        other_paths[0],
        other_paths[1],
        assets_root,
        other_paths[2],
        expected_catalog_identity,
        output_path,
        private_root,
    )

    assert captured["recipe"] == json.loads(recipe_bytes)
    assert captured["base"] == json.loads(base_bytes)
    assert captured["provenance"] == {
        "baseProfileInputSha256": hashlib.sha256(base_bytes).hexdigest(),
        "expectedCatalogIdentitySha256": expected_catalog_identity,
        "recipeSha256": hashlib.sha256(recipe_bytes).hexdigest(),
    }

    with pytest.raises(ValueError, match="does not match the current catalog"):
        m3_module.compose_profile_from_paths(
            recipe_path,
            base_path,
            other_paths[0],
            other_paths[1],
            assets_root,
            other_paths[2],
            "b" * 64,
            output_path,
            private_root,
        )


def test_json_digest_loader_never_reads_more_than_limit_plus_one() -> None:
    requests: list[int] = []

    class Stream:
        def __enter__(self) -> "Stream":
            return self

        def __exit__(self, *_args: object) -> None:
            return None

        def read(self, size: int) -> bytes:
            requests.append(size)
            return b"x" * size

    class FakePath:
        def is_file(self) -> bool:
            return True

        def stat(self) -> object:
            return type("Stat", (), {"st_size": 0})()

        def open(self, mode: str) -> Stream:
            assert mode == "rb"
            return Stream()

    with pytest.raises(ValueError, match="exceeds 7 byte limit"):
        m3_module._load_json_with_sha256(FakePath(), "fixture", maximum=7)
    assert requests == [8]


def test_m3_visual_composer_never_claims_root_rigid_bake_from_a_model_name() -> None:
    assert "provenRootRigidBake" not in inspect.getsource(build_m3_visual_resources)


def test_required_upgrade_definitions_are_hash_bound_and_icon_complete() -> None:
    manifest = build_upgrade_manifest(fixture_report())
    assert manifest["count"] == 4
    assert [row["id"] for row in manifest["upgrades"]] == list(UPGRADES)
    assert all(row["icons"] and len(row["definitionSha256"]) == 64 for row in manifest["upgrades"])


def test_effective_ranger_runtime_contract_is_exact_and_incomplete() -> None:
    effective = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
    generated_path = (
        ROOT / ".private" / "retail-work" / "profiles" / "men-fords-v1.generated.json"
    )
    manifest_path = effective / ".openbfme" / "manifest.json"
    if not generated_path.is_file() or not manifest_path.is_file():
        pytest.skip("private M3 generated profile/effective manifest is not present")
    profile = json.loads(generated_path.read_text(encoding="utf-8"))
    profile["resources"].append(
        {
            "id": "ranger-contract-test-sources",
            "patterns": [
                "data/ini/attributemodifier.ini",
                "data/ini/commandbutton.ini",
                "data/ini/commandset.ini",
                "data/ini/gamedata.ini",
                "data/ini/locomotor.ini",
                "data/ini/object/goodfaction/hordes/men/menhordes.ini",
                "data/ini/object/goodfaction/structures/men/archerrange.ini",
                "data/ini/object/goodfaction/units/men/gondorranger.ini",
                "data/ini/object/goodfaction/units/men/gondorrangeranims.inc",
                "data/ini/upgrade.ini",
                "data/ini/weapon.ini",
            ],
        }
    )
    building_stats = profile["runtime_data"]["data/building-stats.json"]
    result = build_ranger_runtime_contract(
        effective,
        json.loads(manifest_path.read_text(encoding="utf-8")),
        profile,
        building_stats,
    )
    assert RANGER_RUNTIME_PATH == "data/m3/ranger-runtime.json"
    assert result["schema"] == RANGER_RUNTIME_SCHEMA
    assert result["schemaVersion"] == 0
    assert result["capabilityStatus"] == "rules-and-prerequisite-ready"
    assert result["unitRule"]["member"]["health"]["value"] == 300
    assert result["unitRule"]["horde"]["formation"]["memberCount"] == 10
    assert result["production"]["buildCost"] == 600
    assert result["production"]["buildTime"] == 30
    assert result["production"]["commandPoints"] == 70
    assert result["prerequisite"] == {
        "upgradeId": "Upgrade_GondorArcheryRangeLevel2",
        "type": "OBJECT",
        "cost": 500,
        "buildTimeSeconds": 30,
        "commandId": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
        "options": ["CANCELABLE"],
        "fromCommandSet": "GondorArcheryCommandSet",
        "toCommandSet": "GondorArcheryCommandSetLevel2",
        "conflictsWith": ["Upgrade_GondorArcheryRangeLevel3"],
        "levelsToGain": 1,
        "levelCap": 3,
        "purchaseCommandSlot": 4,
        "trainCommandId": "Command_ConstructGondorRangerHorde",
        "trainCommandOptions": ["NEED_UPGRADE", "CANCELABLE"],
    }
    assert result["audioRoutes"]["purchase"]["id"] == "GondorArcherVoiceBuy"
    assert result["audioRoutes"]["created"]["id"] == "RangerVoiceSalute"
    assert result["audioRoutes"]["select"]["id"] == "RangerVoiceSelectMS"
    assert result["audioRouteKinds"]["select"] == "multisound"
    assert result["presentation"]["trainButtonImage"]["id"] == "BGArcheryRange_Rangers"
    assert result["presentation"]["portraitImage"]["id"] == "UPGondor_Ranger"
    assert result["presentation"]["primaryLaunchBone"]["slot"] == "PRIMARY"
    assert result["presentation"]["primaryLaunchBone"]["bone"] == "ARROW"
    assert result["unitRule"]["member"]["weapon"]["clip"] == {
        "size": 1,
        "reloadTimeMs": 1366,
        "autoReloads": True,
        "continuousFireOne": 2,
        "continuousFireCoastMs": 2000,
        "continuousFireRatePercent": 225,
        "source": {
            "ini": "data/ini/weapon.ini",
            "kind": "Weapon",
            "id": "GondorRangerBow",
        },
    }
    assert all(
        binding["source"]["ini"].startswith("data/ini/")
        for binding in result["audioRoutes"].values()
    )
    assert "modelStatus" not in result["presentation"]
    assert "output" not in result["presentation"]
    assert "coverage" not in result["presentation"]
    assert set(result["deferredCapabilities"]) == {
        "model-and-animation-conversion",
        "close-range-sword-and-transition-presentation",
        "camouflage",
        "fire-arrows",
        "long-shot",
        "bombard",
        "veterancy-and-banner",
        "full-animation-and-audiovisual-oracle",
    }
    assert "complete" not in json.dumps(result).casefold()

    model_census = profile["runtime_data"]["data/m3/model-census.json"]
    attach_ranger_playable_bindings(profile, result, model_census)
    objects = {
        row["id"]: row
        for row in profile["runtime_data"]["data/objects.json"]["objects"]
    }
    capabilities = {
        row["id"]: row
        for row in profile["runtime_data"]["data/animation_capabilities.json"]["capabilities"]
    }
    assert objects["bfme2.object.gondor-ranger"]["presentation"]["model"] == (
        "assets/models/m3/units/gondorranger.glb"
    )
    assert objects["bfme2.object.gondor-ranger"]["presentation"]["weaponLaunchBone"] == "ARROW"
    assert objects["bfme2.object.gondor-ranger-horde"]["memberCount"] == 10
    assert objects["bfme2.object.gondor-ranger-horde"]["commandPoints"] == 70
    assert capabilities["bfme2.animation.gondor-ranger"]["states"] == {
        "idle": {"clips": ["guranger_idla"], "mode": "loop", "required": True},
        "move": {"clips": ["guranger_runa"], "mode": "loop", "required": True},
        "attack": {
            "clips": ["guranger_atkd1"],
            "mode": "once",
            "required": True,
            "useWeaponTiming": True,
        },
        "death": {"clips": ["guranger_diea"], "mode": "once", "required": True},
    }
    assert result["presentation"]["coreClipStatus"] == "source-converted"
    assert "model-and-animation-conversion" not in result["deferredCapabilities"]


def test_building_stats_schema_has_every_m3_building_and_source_attested_trainables(
    tmp_path: Path,
) -> None:
    object_path = "data/ini/object/goodfaction/structures/men/buildings.ini"
    trainable_path = "data/ini/object/goodfaction/hordes/men/trainables.ini"
    command_set_path = "data/ini/commandset.ini"
    command_button_path = "data/ini/commandbutton.ini"
    gamedata_path = "data/ini/gamedata.ini"
    object_lines: list[str] = []
    trainable_lines: list[str] = []
    command_set_lines: list[str] = []
    command_button_lines: list[str] = []
    definitions: list[dict] = []

    for index, building in enumerate(BUILDINGS, start=1):
        owner = "MenFortressCitadel" if building == "MenFortress" else building
        set_id = f"FixtureBuildingSet{index}"
        trainable_id = f"FixtureTrainable{index}"
        train_command = f"Command_TrainFixture{index}"
        porter_target = "MenWallHubSmall" if building == "GondorCastleWallHub" else building
        porter_command = (
            "Command_PorterConstructMenWallHub"
            if building == "GondorCastleWallHub"
            else f"Command_PorterConstructFixture{index}"
        )
        object_lines.extend(
            [
                f"Object {building}",
                f"  BuildCost = {100 + index}",
                f"  BuildTime = {20 + index}",
                *( [f"  CommandSet = {set_id}"] if owner == building else [] ),
                *( [f"  CommandSet = {set_id}"] if building == "GondorStatue" else [] ),
                f"  Body = StructureBody ModuleTag_Body{index}",
                f"    MaxHealth = {1000 + index}",
                "  End",
                "End",
                "",
            ]
        )
        if owner != building:
            object_lines.extend(
                [
                    f"Object {owner}",
                    f"  CommandSet = {set_id}",
                    "End",
                    "",
                ]
            )
        trainable_lines.extend(
            [
                f"Object {trainable_id}",
                f"  BuildCost = {200 + index}",
                f"  BuildTime = {30 + index}",
                f"  CommandPoints = {index}",
                "End",
                "",
            ]
        )
        command_set_lines.extend(
            [f"CommandSet {set_id}", f"  1 = {train_command}", "End", ""]
        )
        command_button_lines.extend(
            [
                f"CommandButton {train_command}",
                "  Command = UNIT_BUILD",
                f"  Object = {trainable_id}",
                "End",
                "",
                f"CommandButton {porter_command}",
                "  Command = DOZER_CONSTRUCT",
                f"  Object = {porter_target}",
                "End",
                "",
                *(
                    [
                        "CommandButton Command_PorterConstructMenWallHubOuter",
                        "  Command = DOZER_CONSTRUCT",
                        "  Object = MenWallHubSmallOuter",
                        "End",
                        "",
                        "CommandButton Command_PorterConstructRohanWallHub",
                        "  Command = DOZER_CONSTRUCT",
                        "  Object = GondorCastleWallHub",
                        "End",
                        "",
                        "CommandButton Command_PorterConstructRohanWallHubOuter",
                        "  Command = DOZER_CONSTRUCT",
                        "  Object = MenWallHubSmallOuter",
                        "End",
                        "",
                    ]
                    if building == "GondorCastleWallHub"
                    else []
                ),
            ]
        )
        definitions.append(
            {
                "id": building,
                "source": {"virtualPath": object_path, "sha256": "0" * 64},
                "inheritanceSources": [],
            }
        )
        if owner != building:
            definitions.append(
                {
                    "id": owner,
                    "source": {"virtualPath": object_path, "sha256": "0" * 64},
                    "inheritanceSources": [],
                }
            )
        definitions.append(
            {
                "id": trainable_id,
                "source": {"virtualPath": trainable_path, "sha256": "0" * 64},
                "inheritanceSources": [],
            }
        )

    payloads = {
        object_path: ("\n".join(object_lines) + "\n").encode("cp1252"),
        trainable_path: ("\n".join(trainable_lines) + "\n").encode("cp1252"),
        command_set_path: ("\n".join(command_set_lines) + "\n").encode("cp1252"),
        command_button_path: ("\n".join(command_button_lines) + "\n").encode("cp1252"),
        gamedata_path: b"#define FIXTURE_UNUSED 1\n",
    }
    for definition in definitions:
        definition["source"]["sha256"] = hashlib.sha256(
            payloads[definition["source"]["virtualPath"]]
        ).hexdigest()
    for path, payload in payloads.items():
        destination = tmp_path.joinpath(*path.split("/"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)

    effective_manifest = {
        "files": [
            {
                "path": path,
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            for path, payload in sorted(payloads.items())
        ]
    }
    profile = {
        "resources": [
            {
                "id": "fixture-building-stat-sources",
                "patterns": sorted(payloads),
            }
        ]
    }
    result = extract_building_stats(
        {"definitions": {"objects": definitions}},
        tmp_path,
        effective_manifest,
        profile,
    )

    assert result["schema"] == "openbfme.building-stats"
    assert result["schemaVersion"] == 0
    assert result["complete"] is True
    assert result["missing"] == []
    assert [row["id"] for row in result["buildings"]] == list(BUILDINGS)
    assert len(result["buildings"]) == 12
    assert len(result["trainables"]) == 12
    assert all(row["sourceIni"] == object_path for row in result["buildings"])
    assert all(row["commandSet"]["commands"] for row in result["buildings"])
    assert all(row["porterConstructCommand"]["id"] for row in result["buildings"])
    wall_hub = next(row for row in result["buildings"] if row["id"] == "GondorCastleWallHub")
    assert wall_hub["porterConstructCommand"]["id"] == "Command_PorterConstructMenWallHubOuter"
    assert wall_hub["porterConstructCommand"]["targetId"] == "MenWallHubSmallOuter"
    assert all(row["sourceIni"] == trainable_path for row in result["trainables"])
