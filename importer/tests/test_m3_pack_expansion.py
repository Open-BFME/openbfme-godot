from __future__ import annotations

import json
import inspect
import hashlib
from pathlib import Path

import pytest

from openbfme_importer.m3_pack_expansion import (
    BUILDINGS,
    RUNTIME_PATHS,
    UNITS,
    UPGRADES,
    build_upgrade_manifest,
    build_m3_visual_resources,
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


def test_m3_visual_composer_never_claims_root_rigid_bake_from_a_model_name() -> None:
    assert "provenRootRigidBake" not in inspect.getsource(build_m3_visual_resources)


def test_required_upgrade_definitions_are_hash_bound_and_icon_complete() -> None:
    manifest = build_upgrade_manifest(fixture_report())
    assert manifest["count"] == 4
    assert [row["id"] for row in manifest["upgrades"]] == list(UPGRADES)
    assert all(row["icons"] and len(row["definitionSha256"]) == 64 for row in manifest["upgrades"])


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
