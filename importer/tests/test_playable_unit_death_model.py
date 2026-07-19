"""Separate-hierarchy death W3Ds compile to a model swap, not a clip binding."""

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
    ("InfantryHorde", False): (
        "1e982a5104584ae1f0d91ebaa638da2f8cc86482ea41dd7a874f022d3270649f"
    ),
    ("SiegeUnit", True): (
        "b6bd20445a64152888fcf3ff97470eb43510c2c2eb095d3007bf22bbe7365171"
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


def test_mixed_clip_and_swap_death_fails_closed() -> None:
    descriptor = _descriptor("SiegeUnit")
    closure, _ = _own_rig_death_closure(descriptor)
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
    with pytest.raises(
        PlayableUnitPackCompilerError, match="mixes skeleton clips"
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
