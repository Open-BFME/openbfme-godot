"""Separate-hierarchy death W3Ds compile to a model swap, not a clip binding.

A death state may also mix primary-skeleton fall clips with condition-keyed
decay corpse models (retail GoblinCaveTroll/WildMountainGiant): clips stay
animation bindings while each swap model becomes a death-model resource.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import PurePosixPath

import pytest

from importer.tests.test_playable_unit_pack_compiler import (
    _closure,
    _descriptor,
    _leaf,
    _rehash_closure,
)
from openbfme_importer.playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    compile_playable_unit_pack_recipe,
    validate_playable_unit_pack_recipe,
)


# Recipes compiled from closures without a separate-hierarchy death W3D must
# stay byte-identical to the pre-feature compiler output.
PINNED_UNAFFECTED_DIGESTS = {
    # InfantryHorde has no separate-hierarchy death; digest tracks the current
    # auxiliary static/hierarchy routing baseline (not death-swap routing).
    # Baseline includes the compiled experience contract (status "unavailable"
    # for these fixtures) added with the experiencelevels.ini pipeline.
    #
    # Re-pinned 2026-08-04. The recipe bodies were diffed field by field
    # against the previous pin: the only deltas are three empty additive
    # contract fields — `armor.conditionalSets` (armor_compiler.py, conditional
    # ArmorSet resolution) on the container and its primary member, and
    # `presentation.sourceNullStringIds` (playable_unit_compiler.py, layered
    # NULL UI-string ids). Both are `[]` for these fixtures. Nothing in the
    # death routing this module guards moved, which is exactly what this pin
    # exists to say.
    ("InfantryHorde", False): (
        "156559fff53600900b31a96224662cd29229979f50af8426948399d69225e9d9"
    ),
    ("SiegeUnit", True): (
        "cf14aedd267050b445746f44e63702d0eed50b40f70921797153c48a35ceda53"
    ),
}


def _rehash_recipe(recipe: dict[str, object]) -> None:
    unsigned = dict(recipe)
    unsigned.pop("recipeSha256")
    recipe["recipeSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def _scanned_row(
    path: str,
    model_ids: list[str],
    hierarchy_ids: list[str],
    animation_ids: list[str],
) -> dict[str, object]:
    return {
        "virtualPath": path,
        "byteLength": 1,
        "sha256": hashlib.sha256(path.encode()).hexdigest(),
        "headerIds": {
            "virtualPath": path,
            "modelIds": model_ids,
            "hierarchyIds": hierarchy_ids,
            "animationIds": animation_ids,
        },
        "modelReferences": [],
        "warnings": [],
    }


def _own_rig_death_closure(
    descriptor: dict[str, object],
) -> tuple[dict[str, object], str]:
    """Make the fixture death W3D carry its own hierarchy, like gusiegtreb_diea."""

    closure = _closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    death_path = f"art/w3d/fi/{member.casefold()}_diea.w3d"
    rig = f"{member}_DIEA".upper()
    for row in closure["scannedW3d"]:
        if row["virtualPath"] == death_path:
            row["headerIds"]["modelIds"] = [rig]
            row["headerIds"]["hierarchyIds"] = [rig]
            row["headerIds"]["animationIds"] = [f"{rig}.{rig}"]
    _rehash_closure(closure)
    return closure, death_path


@pytest.mark.parametrize(
    ("target", "conditional_death"),
    sorted(PINNED_UNAFFECTED_DIGESTS, key=str),
)
def test_unaffected_recipes_stay_byte_identical(
    target: str, conditional_death: bool
) -> None:
    descriptor = _descriptor(target)
    recipe = compile_playable_unit_pack_recipe(
        descriptor, _closure(descriptor, conditional_death=conditional_death)
    )
    assert isinstance(
        recipe["runtimeRegistration"]["visual"]["coreAnimations"]["death"], list
    )
    assert (
        recipe["recipeSha256"]
        == PINNED_UNAFFECTED_DIGESTS[(target, conditional_death)]
    )


def test_own_hierarchy_death_w3d_becomes_model_swap() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, death_path = _own_rig_death_closure(descriptor)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)

    visual = recipe["runtimeRegistration"]["visual"]
    binding = visual["coreAnimations"]["death"]
    assert binding["binding"] == "separate-model"
    assert binding["sourceW3d"] == death_path
    resource = next(
        row for row in recipe["resources"] if row["id"] == binding["resourceId"]
    )
    assert resource["converter"] == "w3d-bundle"
    assert resource["patterns"] == [death_path]
    assert resource["output"] == binding["output"]
    name = PurePosixPath(death_path).name
    assert resource["options"]["model"] == name
    assert resource["options"]["animations"] == [name]

    assert len(visual["components"]) == 1
    for row in recipe["resources"]:
        if row["id"] != binding["resourceId"]:
            assert death_path not in row["patterns"]
    authored = next(
        row
        for row in visual["authoredAnimationStates"]
        if row["sourceW3d"] == death_path
    )
    assert authored["semanticState"] == "death"
    assert authored["runtimeSupport"] == "generic-core"


def test_rig_family_mismatch_death_w3d_becomes_model_swap() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure = _closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    death_path = f"art/w3d/fi/{member.casefold()}_diea.w3d"
    death_rig = f"{member}DEATH_SKL".upper()
    death_skl_path = f"art/w3d/fi/{member.casefold()}death_skl.w3d"
    for row in closure["scannedW3d"]:
        if row["virtualPath"] == death_path:
            row["headerIds"]["animationIds"] = [
                f"{death_rig}.{member.upper()}_DIEA"
            ]
    closure["scannedW3d"].append(
        _scanned_row(death_skl_path, [], [death_rig], [])
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)
    binding = recipe["runtimeRegistration"]["visual"]["coreAnimations"]["death"]
    assert binding["binding"] == "separate-model"
    resource = next(
        row for row in recipe["resources"] if row["id"] == binding["resourceId"]
    )
    assert resource["patterns"] == sorted(
        [death_path, death_skl_path], key=str.casefold
    )
    assert resource["options"]["model"] == PurePosixPath(death_path).name


def test_mixed_clip_and_swap_death_compiles_with_composition_rules() -> None:
    # GoblinCaveTroll/WildMountainGiant shape: fall-down clips on the primary
    # skeleton coexist with a condition-keyed decay corpse model that ships
    # its own hierarchy.  Clips stay animation bindings; the swap becomes a
    # condition-keyed death-model resource.
    descriptor = _descriptor("SiegeUnit")
    closure, death_path = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    clip_path = f"art/w3d/fi/{member.casefold()}_dieb.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEB",
            "animation",
            clip_path,
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            clip_path,
            [],
            [],
            [f"{member}_SKL.{member}_DIEB".upper()],
        )
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)

    visual = recipe["runtimeRegistration"]["visual"]
    death = visual["coreAnimations"]["death"]
    assert isinstance(death, list)
    assert [row["sourceW3d"] for row in death] == [clip_path]
    swaps = visual["deathModelSwaps"]
    assert len(swaps) == 1
    swap = swaps[0]
    assert swap["binding"] == "separate-model"
    assert swap["sourceW3d"] == death_path
    assert swap["identifier"] == f"{member}_DIEA"
    assert swap["conditions"] == ["DYING", "DEATH_1"]
    resource = next(
        row for row in recipe["resources"] if row["id"] == swap["resourceId"]
    )
    assert resource["converter"] == "w3d-bundle"
    assert resource["patterns"] == [death_path]
    assert resource["output"] == swap["output"]
    name = PurePosixPath(death_path).name
    assert resource["options"]["model"] == name
    assert resource["options"]["animations"] == [name]


def test_multi_model_mixed_death_compiles_one_resource_per_swap_model() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, first_path = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    second_path = f"art/w3d/fi/{member.casefold()}_dieb.w3d"
    second_rig = f"{member}_DIEB".upper()
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEB",
            "animation",
            second_path,
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            second_path, [second_rig], [second_rig], [f"{second_rig}.{second_rig}"]
        )
    )
    clip_path = f"art/w3d/fi/{member.casefold()}_diec.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEC",
            "animation",
            clip_path,
            ["DYING"],
            "AnimationState DYING",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            clip_path,
            [],
            [],
            [f"{member}_SKL.{member}_DIEC".upper()],
        )
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)

    visual = recipe["runtimeRegistration"]["visual"]
    death = visual["coreAnimations"]["death"]
    assert isinstance(death, list)
    assert [row["sourceW3d"] for row in death] == [clip_path]
    swaps = visual["deathModelSwaps"]
    assert [row["sourceW3d"] for row in swaps] == [first_path, second_path]
    assert [row["conditions"] for row in swaps] == [
        ["DYING", "DEATH_1"],
        ["DYING", "DEATH_2"],
    ]
    assert len({row["resourceId"] for row in swaps}) == 2
    for swap in swaps:
        resource = next(
            row for row in recipe["resources"] if row["id"] == swap["resourceId"]
        )
        assert resource["patterns"] == [swap["sourceW3d"]]
        assert resource["output"] == swap["output"]


def test_death_swap_model_condition_visual_is_bundle_not_hierarchical() -> None:
    """Battlewagon/catapult shape: DYING ModelCondition reuses the die W3D.

    Separate-hierarchy death is packaged as death-model (w3d-bundle). The same
    source must not also become a hierarchical visual component — hierarchical
    fails closed on the embedded animation actions those files carry.
    """

    descriptor = _descriptor("SiegeUnit")
    closure, death_path = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    # Intentionally omit embeddedAnimationChannelCount: routing must key off
    # the death-swap classification, not only channel-count evidence.
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIE_MODEL",
            "model",
            death_path,
            ["DYING"],
            "ModelConditionState DYING",
        )
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)

    visual = recipe["runtimeRegistration"]["visual"]
    death_component = next(
        row for row in visual["components"] if row["sourceW3d"] == death_path
    )
    assert death_component["conditions"] == ["DYING"]
    visual_resource = next(
        row for row in recipe["resources"] if row["id"] == death_component["resourceId"]
    )
    death_name = PurePosixPath(death_path).name
    assert visual_resource["converter"] == "w3d-bundle"
    assert visual_resource["options"]["model"] == death_name
    assert visual_resource["options"]["animations"] == [death_name]

    death_binding = visual["coreAnimations"]["death"]
    assert death_binding["binding"] == "separate-model"
    assert death_binding["sourceW3d"] == death_path
    death_resource = next(
        row for row in recipe["resources"] if row["id"] == death_binding["resourceId"]
    )
    assert death_resource["converter"] == "w3d-bundle"
    assert death_resource["options"]["animations"] == [death_name]
    assert death_resource["id"] != visual_resource["id"]


def test_mixed_death_swap_model_condition_visuals_are_bundles() -> None:
    """CaveTroll/MountainGiant shape: DECAY corpse models are also dis* anims."""

    descriptor = _descriptor("SiegeUnit")
    closure, first_path = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    second_path = f"art/w3d/fi/{member.casefold()}_dieb.w3d"
    second_rig = f"{member}_DIEB".upper()
    clip_path = f"art/w3d/fi/{member.casefold()}_diec.w3d"
    closure["exactLeaves"].extend(
        [
            _leaf(
                member,
                f"{member}_DIEA_MODEL",
                "model",
                first_path,
                ["DYING", "DECAY", "DEATH_1"],
                "ModelConditionState DYING DECAY DEATH_1",
            ),
            _leaf(
                member,
                f"{member}_DIEB",
                "animation",
                second_path,
                ["DYING", "DECAY", "DEATH_2"],
                "AnimationState DYING DECAY DEATH_2",
            ),
            _leaf(
                member,
                f"{member}_DIEB_MODEL",
                "model",
                second_path,
                ["DYING", "DECAY", "DEATH_2"],
                "ModelConditionState DYING DECAY DEATH_2",
            ),
            _leaf(
                member,
                f"{member}_DIEC",
                "animation",
                clip_path,
                ["DYING"],
                "AnimationState DYING",
            ),
        ]
    )
    # No embeddedAnimationChannelCount: death-swap membership alone must route.
    closure["scannedW3d"].extend(
        [
            _scanned_row(
                second_path, [second_rig], [second_rig], [f"{second_rig}.{second_rig}"]
            ),
            _scanned_row(
                clip_path,
                [],
                [],
                [f"{member}_SKL.{member}_DIEC".upper()],
            ),
        ]
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    validate_playable_unit_pack_recipe(recipe)

    visual = recipe["runtimeRegistration"]["visual"]
    assert isinstance(visual["coreAnimations"]["death"], list)
    swaps = visual["deathModelSwaps"]
    assert {row["sourceW3d"] for row in swaps} == {first_path, second_path}

    for path in (first_path, second_path):
        component = next(row for row in visual["components"] if row["sourceW3d"] == path)
        resource = next(
            row for row in recipe["resources"] if row["id"] == component["resourceId"]
        )
        name = PurePosixPath(path).name
        assert resource["converter"] == "w3d-bundle", path
        assert resource["options"]["animations"] == [name]
        death_resource = next(
            row
            for row in recipe["resources"]
            if row.get("options", {}).get("model") == name
            and "death-model" in row["id"]
        )
        assert death_resource["converter"] == "w3d-bundle"
        assert death_resource["id"] != resource["id"]


def test_duplicate_condition_mixed_death_fails_closed() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, _ = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    second_path = f"art/w3d/fi/{member.casefold()}_dieb.w3d"
    second_rig = f"{member}_DIEB".upper()
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEB",
            "animation",
            second_path,
            ["DYING", "DEATH_1"],
            "AnimationState DYING DEATH_1",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            second_path, [second_rig], [second_rig], [f"{second_rig}.{second_rig}"]
        )
    )
    clip_path = f"art/w3d/fi/{member.casefold()}_diec.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEC",
            "animation",
            clip_path,
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            clip_path,
            [],
            [],
            [f"{member}_SKL.{member}_DIEC".upper()],
        )
    )
    _rehash_closure(closure)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="swap conditions are ambiguous"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_ambiguous_swap_death_fails_closed() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, _ = _own_rig_death_closure(descriptor)
    member = str(descriptor["composition"]["primaryMemberObjectId"])
    second_path = f"art/w3d/fi/{member.casefold()}_dieb.w3d"
    second_rig = f"{member}_DIEB".upper()
    closure["exactLeaves"].append(
        _leaf(
            member,
            f"{member}_DIEB",
            "animation",
            second_path,
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["scannedW3d"].append(
        _scanned_row(
            second_path, [second_rig], [second_rig], [f"{second_rig}.{second_rig}"]
        )
    )
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="ambiguous"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_forged_separate_model_binding_is_rejected() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, _ = _own_rig_death_closure(descriptor)
    original = compile_playable_unit_pack_recipe(descriptor, closure)
    intact_component = original["runtimeRegistration"]["visual"]["components"][0]
    texture_resource = next(
        row for row in original["resources"] if row["converter"] == "hash-only"
    )
    for mutation in ("kind", "resource", "component-resource", "output"):
        recipe = deepcopy(original)
        binding = recipe["runtimeRegistration"]["visual"]["coreAnimations"]["death"]
        if mutation == "kind":
            binding["binding"] = "clip"
        elif mutation == "resource":
            binding["resourceId"] = texture_resource["id"]
        elif mutation == "component-resource":
            binding["resourceId"] = intact_component["resourceId"]
        else:
            binding["output"] = "assets/models/units/forged/death.glb"
        _rehash_recipe(recipe)
        with pytest.raises(
            PlayableUnitPackCompilerError, match="separate-model death binding"
        ):
            validate_playable_unit_pack_recipe(recipe)
