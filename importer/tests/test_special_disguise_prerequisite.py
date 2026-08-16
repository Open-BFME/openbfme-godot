from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.playable_unit_import import (
    _source_documents,
    compile_unit_recipe,
    extend_profile_with_unit,
)
from openbfme_importer.playable_unit_pack_compiler import (
    compile_playable_unit_pack_recipe,
    validate_playable_unit_pack_recipe,
)
from openbfme_importer.module_contracts import (
    compile_attribute_modifier_aura_updates,
    compile_respawn_updates,
    compile_special_enemy_sense_updates,
    compile_special_disguise_updates,
)
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.sage_cst import parse_sage_document
from openbfme_importer.retail_ability_fx_ingress import texture_index_for
from openbfme_importer.retail_visual_closure import build_retail_visual_closure
from openbfme_importer.special_disguise_prerequisite import (
    RUNTIME_STATUS,
    SpecialDisguisePrerequisiteError,
    build_special_disguise_prerequisite,
    special_disguise_visual_targets,
    validate_special_disguise_prerequisite,
)
from importer.tests.test_playable_unit_import import _base_profile


REPO = Path(__file__).resolve().parents[2]
PRIVATE = REPO / ".private" / "retail-work"
CASES = {
    "bfme2": (
        PRIVATE / "cache" / "effective-assets",
        PRIVATE / "reports/faction-import/men/objects/rohaneowyn/descriptor.json",
        32,
        ["FX_DisguiseExit", "FX_EowynDisguiseToggle"],
        [
            "EowynVoiceDisguiseOff",
            "EowynVoiceDisguiseOn",
            "HorseWhinnyForMountButton",
        ],
    ),
    "rotwk": (
        PRIVATE / "editions/rotwk/cache/layered-effective-assets",
        PRIVATE / "editions/rotwk/reports/faction-import/men/objects/rohaneowyn/descriptor.json",
        35,
        [
            "FX_DisguiseExit",
            "FX_EowynCriticalStrike",
            "FX_EowynDisguiseToggle",
        ],
        [
            "EowynCriticalStrikeVoice",
            "EowynVoiceDisguiseOff",
            "EowynVoiceDisguiseOn",
            "HorseWhinnyForMountButton",
        ],
    ),
}


def _canonical_digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _available(game: str) -> bool:
    root, descriptor, *_ = CASES[game]
    return root.is_dir() and descriptor.is_file()


@lru_cache(maxsize=2)
def _retail_artifact(game: str) -> dict[str, object]:
    root, descriptor_path, *_ = CASES[game]
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
    documents = _source_documents(root)
    targets = [
        "RohanEowyn",
        *special_disguise_visual_targets(descriptor, documents),
    ]
    visual = build_retail_visual_closure(root, targets)
    artifact = build_special_disguise_prerequisite(
        descriptor,
        documents,
        visual,
        game=game,
        texture_index=texture_index_for(root, f"special-disguise-test-{game}"),
        effective_root=root,
    )
    assert artifact is not None
    return artifact


@pytest.mark.parametrize("game", ["bfme2", "rotwk"])
def test_retail_eowyn_disguise_prerequisite_is_exact_and_deferred(game: str) -> None:
    if not _available(game):
        pytest.skip(f"private {game} Eowyn oracle is unavailable")
    artifact = _retail_artifact(game)
    _, _, expected_resources, expected_fx, expected_audio = CASES[game]

    assert artifact["objectId"] == "RohanEowyn"
    assert artifact["runtimeStatus"] == RUNTIME_STATUS
    assert artifact["presentationOnly"] is True
    assert artifact["authoritativeEntityRegistration"] is False
    assert artifact["presentationIdentities"] == {
        "ownerObjectId": "RohanEowyn",
        "nonOwnerDisguiseTemplateId": "RohanEowynDisguised",
        "hostilePerspectiveTemplateId": "RohanRohirrimHorde",
    }
    assert len(artifact["visualLeafBindings"]) == 102
    assert len(artifact["resources"]) == expected_resources
    assert artifact["fxListIds"] == expected_fx
    assert artifact["audioClosure"]["rootIds"] == expected_audio
    assert {
        row["role"] for row in artifact["visualLeafBindings"]
    } == {
        "owner-base-presentation",
        "owner-mounted-presentation",
        "owner-disguised-presentation",
        "non-owner-disguised-presentation",
    }
    fields = artifact["moduleReceipt"]["fields"]
    assert fields["DisguiseAsTemplate"]["authored"] == "RohanEowynDisguised"
    assert fields["DisguisedAsTemplate_EnemyPerspective"]["authored"] == (
        "RohanRohirrimHorde"
    )
    if game == "bfme2":
        assert fields["OpacityTarget"]["authored"] == ".3"
        assert "TriggerAttributeModifier" not in fields
    else:
        assert fields["OpacityTarget"]["authored"] == ".9"
        assert fields["TriggerAttributeModifier"]["authored"] == "Rider2Tracker"
        assert fields["AttributeModifierDuration"]["authored"] == "2000"
    validate_special_disguise_prerequisite(artifact)


def test_canonical_rotwk_201_disguise_is_not_fanpatch_rider2_shape() -> None:
    root = PRIVATE / "editions/rotwk/cache/effective-assets"
    source_path = "data/ini/object/goodfaction/units/men/eowyn.ini"
    source = root / source_path
    if not source.is_file():
        pytest.skip("canonical RotWK 2.01 Eowyn oracle is unavailable")
    objects = {
        row.name.casefold(): row
        for row in parse_sage_document(source.read_bytes(), source_path).objects
    }
    row = compile_special_disguise_updates(
        (objects["rohaneowyn"],), "RohanEowyn"
    )[0]

    assert row["runtimeStatus"] == "executable"
    assert row["fields"]["OpacityTarget"]["value"] == pytest.approx(0.3)
    assert "TriggerAttributeModifier" not in row["fields"]
    assert "AttributeModifierDuration" not in row["fields"]


def test_disguise_prerequisite_rejects_digest_and_identity_tampering() -> None:
    if not _available("rotwk"):
        pytest.skip("private RotWK Eowyn oracle is unavailable")
    artifact = _retail_artifact("rotwk")
    broken = deepcopy(artifact)
    broken["audioSampleOutputs"].pop(next(iter(broken["audioSampleOutputs"])))
    with pytest.raises(SpecialDisguisePrerequisiteError, match="digest"):
        validate_special_disguise_prerequisite(broken)

    rebound = deepcopy(artifact)
    rebound["presentationIdentities"]["nonOwnerDisguiseTemplateId"] = "FakeChild"
    rebound.pop("aggregateSha256")
    rebound["aggregateSha256"] = _canonical_digest(rebound)
    with pytest.raises(SpecialDisguisePrerequisiteError, match="module identity"):
        validate_special_disguise_prerequisite(rebound)


@pytest.mark.parametrize("game", ["bfme2", "rotwk"])
def test_retail_eowyn_upgrade_aura_preserves_edition_exact_default(game: str) -> None:
    root, *_ = CASES[game]
    if not root.is_dir():
        pytest.skip(f"private {game} Eowyn oracle is unavailable")
    source_path = "data/ini/object/goodfaction/units/men/eowyn.ini"
    source = _source_documents(root)[source_path]
    objects = {
        row.name.casefold(): row
        for row in parse_sage_document(source, source_path).objects
    }
    rows = compile_attribute_modifier_aura_updates(
        (objects["rohaneowyn"],), "RohanEowyn"
    )

    if game == "bfme2":
        assert rows == []
        return
    assert len(rows) == 1
    row = rows[0]
    assert row["tag"] == "ModuleTag_EowynDisguiseCriticalStrikeBonus"
    assert row["fields"]["StartsActive"] == {
        "authored": None,
        "value": False,
        "defaulted": True,
        "defaultSource": "AttributeModifierAuraUpdate-module-data-bool-zero",
        "sourceIni": source_path,
        "line": 1125,
    }
    assert row["fields"]["TriggeredBy"]["value"] == [
        "Upgrade_EowynConditionDisguised"
    ]
    assert row["fields"]["BonusName"]["value"] == "EowynCriticalStrikeModifier"
    assert row["fields"]["RefreshDelay"]["milliseconds"] == 0
    assert row["fields"]["AllowSelf"]["value"] is True
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize("game", ["bfme2", "rotwk"])
def test_retail_eowyn_respawn_rules_preserve_exact_edition_expressions(game: str) -> None:
    root, *_ = CASES[game]
    if not root.is_dir():
        pytest.skip(f"private {game} Eowyn oracle is unavailable")
    documents = _source_documents(root)
    source_path = "data/ini/object/goodfaction/units/men/eowyn.ini"
    objects = {
        row.name.casefold(): row
        for row in parse_sage_document(documents[source_path], source_path).objects
    }
    prepared = prepare_playable_unit_compiler(documents)
    row = compile_respawn_updates(
        (objects["rohaneowyn"],),
        "RohanEowyn",
        numeric_defines=prepared.numeric_defines,
        numeric_define_provenance=prepared.numeric_define_provenance,
    )[0]
    rules = row["fields"]["RespawnRules"]

    if game == "bfme2":
        assert rules["line"] == 1020
        assert rules["cost"] == 1000
        assert rules["timeMilliseconds"] == 35000
        assert "costExpression" not in rules
        assert "timeExpression" not in rules
        assert compile_special_enemy_sense_updates(
            (objects["rohaneowyn"],),
            "RohanEowyn",
            numeric_defines=prepared.numeric_defines,
            numeric_define_provenance=prepared.numeric_define_provenance,
        ) == []
    else:
        assert rules["line"] == 1082
        assert rules["cost"] == 600
        assert rules["timeMilliseconds"] == 40000
        assert len(rules["costDefineProvenance"]) == 3
        assert len(rules["timeDefineProvenance"]) == 2
        sense = compile_special_enemy_sense_updates(
            (objects["rohaneowyn"],),
            "RohanEowyn",
            numeric_defines=prepared.numeric_defines,
            numeric_define_provenance=prepared.numeric_define_provenance,
        )[0]
        assert sense["line"] == 1370
        assert sense["fields"]["ScanRange"]["value"] == 175
        assert sense["fields"]["ScanRange"]["defineProvenance"] == {
            "defineId": "VISION_HERO_STANDARD",
            "sourceIni": "data/ini/gamedata.ini",
            "line": 122,
            "authoredValue": "175",
            "value": 175,
        }


def test_bfme2_recipe_and_profile_bind_the_prerequisite_digest_and_resources() -> None:
    root = PRIVATE / "cache/effective-assets"
    catalog_path = PRIVATE / "catalog/bfme2.json"
    if not root.is_dir() or not catalog_path.is_file():
        pytest.skip("private BFME2 unit recipe oracle is unavailable")
    catalog = InstallCatalog.load(catalog_path)
    _, descriptor, _, _ = compile_unit_recipe(
        catalog, root, "RohanEowyn", game="bfme2", faction="men"
    )
    documents = _source_documents(root)
    visual = build_retail_visual_closure(
        root,
        ["RohanEowyn", *special_disguise_visual_targets(descriptor, documents)],
    )
    prerequisite = build_special_disguise_prerequisite(
        descriptor,
        documents,
        visual,
        game="bfme2",
        texture_index=texture_index_for(root, "special-disguise-profile-bfme2"),
        effective_root=root,
    )
    recipe = compile_playable_unit_pack_recipe(
        descriptor, visual, None, prerequisite
    )
    validate_playable_unit_pack_recipe(recipe)
    profile, delta = extend_profile_with_unit(_base_profile(), recipe)
    runtime = profile["runtime_data"][delta["runtimePath"]]
    envelope = runtime["registration"]["specialDisguisePresentationPrerequisite"]

    assert recipe["specialDisguisePrerequisiteSha256"] == (
        prerequisite["aggregateSha256"]
    )
    assert envelope["closure"]["aggregateSha256"] == (
        recipe["specialDisguisePrerequisiteSha256"]
    )
    assert set(envelope["resourceIds"]) <= set(runtime["resourceIds"])
    assert len(envelope["visualResourceIds"]) == 4
    assert runtime["descriptorSha256"] == prerequisite["descriptorSha256"]


def test_canonical_rotwk_recipe_does_not_invent_fanpatch_cancel_fx() -> None:
    """The shipping RotWK cook reads the canonical 2.01 effective tree.

    Rider2Tracker and SpecialAbilityDisguiseCancel occur only in the optional
    layered fan-patch view.  Their absence cannot make canonical Eowyn a
    converter gap, and the prerequisite must contain only FX authored by the
    descriptor's own source lineage.
    """
    root = PRIVATE / "editions/rotwk/cache/effective-assets"
    catalog_path = PRIVATE / "catalog/rotwk.json"
    if not root.is_dir() or not catalog_path.is_file():
        pytest.skip("canonical RotWK unit recipe oracle is unavailable")
    catalog = InstallCatalog.load(catalog_path)
    _, descriptor, _, _ = compile_unit_recipe(
        catalog, root, "RohanEowyn", game="rotwk", faction="men"
    )
    documents = _source_documents(root)
    visual = build_retail_visual_closure(
        root,
        ["RohanEowyn", *special_disguise_visual_targets(descriptor, documents)],
    )
    prerequisite = build_special_disguise_prerequisite(
        descriptor,
        documents,
        visual,
        game="rotwk",
        texture_index=texture_index_for(root, "special-disguise-profile-rotwk"),
        effective_root=root,
    )
    assert prerequisite is not None
    assert prerequisite["fxListIds"] == [
        "FX_DisguiseExit",
        "FX_EowynDisguiseToggle",
    ]
    assert "FX_EowynCriticalStrike" not in prerequisite["fxListIds"]
    recipe = compile_playable_unit_pack_recipe(
        descriptor, visual, None, prerequisite
    )
    validate_playable_unit_pack_recipe(recipe)


def test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure() -> None:
    selection_path = REPO / ".private/content-packs/selection.json"
    if not selection_path.is_file():
        pytest.skip("selected private packs are unavailable")
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    active = str(selection.get("activePack", ""))
    if not active.startswith("rotwk-men-vslice/"):
        pytest.skip("RotWK Men is not the selected active pack")
    runtime_path = (
        REPO / ".private/content-packs" / active
        / "data/playable-units/rohaneowyn.json"
    )
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    registration = runtime["registration"]

    assert "specialDisguisePresentationPrerequisite" not in registration
    assert any(
        row.get("id")
        == "module:container:SpecialDisguiseUpdate:ModuleTag_SpecialDisguiseUpdateUpdate"
        for row in registration["unsupportedCapabilities"]
    )
    # This is an expected-red acceptance receipt: publication/selection must
    # not be called green until a new layered recipe carries the closure.
    assert active == (
        "rotwk-men-vslice/"
        "7f2765762bc3d679044d864c2f1ae3c7a15a712e2dd0dc481994ec17edba7a6f"
    )
