"""The profile that puts 62 retail screens into a content pack.

Queue Q117.  `retail_shell_apt_profile` attests one pinned scene with hardcoded
oracle markers; a screen lane cannot, because there are 62 closures.  So it
attests per screen instead - which virtual paths, and what they hashed to - and
these gates cover the two ways that could go quietly wrong: a screen cooked
into another screen's slot, and the 22 movies that do NOT reconstruct being
dropped instead of named.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.profile import (
    ImportProfile,
    MAX_PATTERNS_PER_RESOURCE,
    MAX_SCREEN_APT_PATTERNS,
)
from openbfme_importer.retail_screen_apt_profile import (
    SCREEN_APT_CONVERTER,
    ScreenAptProfileError,
    build_retail_screen_apt_plan,
    generated_import_profile,
    screen_profile_resource,
    screen_runtime_path,
)

REPO = Path(__file__).resolve().parents[2]

#: TWO EDITIONS SHIP DIFFERENT SCREENS, and conflating them is the exact
#: "which tree did this load" trap this project has been burned by before.
#: RotWK is the owner-ratified content baseline, so it is the SHIPPING pin;
#: BFME2 is kept beside it because the lane must stay edition-agnostic and
#: because a silent swap between them would otherwise look like success.
#: 26 of the 59 screens both editions share differ in draw count - MenuExport
#: is 634 primitives in BFME2 and 20 in RotWK - so these numbers are not
#: interchangeable.
BFME2_ASSETS = REPO / "workspace/retail-work/cache/effective-assets"
ROTWK_ASSETS = (
    REPO / "workspace/retail-work/editions/rotwk/cache/effective-assets"
)
EDITIONS = {
    "rotwk": (
        ROTWK_ASSETS,
        {
            "movieCount": 86,
            "screenCount": 62,
            "refusedCount": 24,
            "drawCount": 11769,
            "sourceCount": 16922,
        },
    ),
    "bfme2": (
        BFME2_ASSETS,
        {
            "movieCount": 84,
            "screenCount": 62,
            "refusedCount": 22,
            "drawCount": 13233,
            "sourceCount": 11664,
        },
    ),
}
#: The edition the game actually ships.
ASSETS = ROTWK_ASSETS

pytestmark = pytest.mark.skipif(
    not ASSETS.is_dir(), reason="private RotWK effective-assets oracle is not present"
)


@pytest.fixture(scope="module")
def plan() -> dict:
    return build_retail_screen_apt_plan(ASSETS)


@pytest.mark.parametrize("edition", sorted(EDITIONS))
def test_each_edition_cooks_its_own_screens(edition: str) -> None:
    """A screen cook is edition-specific; the two must never be conflated."""

    root, expected = EDITIONS[edition]
    if not root.is_dir():
        pytest.skip(f"{edition} effective-assets oracle is not present")
    assert build_retail_screen_apt_plan(root)["summary"] == expected


def test_the_plan_admits_what_reconstructs_and_names_what_does_not(plan) -> None:
    assert plan["summary"] == EDITIONS["rotwk"][1]
    refused = {row["movie"] for row in plan["refused"]}
    # Libraries exist to be imported, not shown; they must be named, not hidden.
    assert {"libInGameUI", "MenuFrameAndBg", "GameWindowGadgets"} <= refused
    assert all(row["reason"] for row in plan["refused"])
    assert refused.isdisjoint({row["movie"] for row in plan["screens"]})


def test_every_resource_names_the_screen_it_cooks(plan) -> None:
    """A screen cooked into another screen's slot is the quiet failure here."""

    resources = plan["profileFragment"]["resources"]
    assert len(resources) == 62
    ids = [row["id"] for row in resources]
    assert len(set(ids)) == len(ids)
    for row in resources:
        movie = row["options"]["movie"]
        assert row["converter"] == SCREEN_APT_CONVERTER
        assert row["output"] == screen_runtime_path(movie)
        assert row["id"] == f"screen-apt-{movie.casefold()}"
        assert row["expected_count"] == row["limit"] == len(row["patterns"])
        assert len(row["options"]["expectedSourceAggregateSha256"]) == 64
        # The movie's own sources are always part of its bundle.
        assert f"{movie}.apt" in row["patterns"]


def test_the_frame_state_is_recorded_per_screen(plan) -> None:
    """Every screen states which authored frame it was cooked at."""

    rules = {str(row["frameRule"]) for row in plan["screens"]}
    assert rules <= {"authored-open-label", "no-authored-label-frame-zero"}
    for row in plan["screens"]:
        if row["frameRule"] == "no-authored-label-frame-zero":
            assert row["frameLabel"] is None and row["frame"] == 0
        else:
            assert row["frameLabel"] in ("_open", "_show", "_init", "_fadeIn")
    spellstore = next(row for row in plan["screens"] if row["movie"] == "SpellStore")
    assert (spellstore["frameLabel"], spellstore["frame"]) == ("_open", 0)
    assert spellstore["drawCount"] == 148


def test_the_generated_profile_is_catalog_loadable(plan, tmp_path: Path) -> None:
    profile = generated_import_profile(plan)
    assert profile["pack"]["dataPolicy"]["redistributable"] is False
    assert len(profile["resources"]) == 62
    path = tmp_path / "profile.json"
    import json

    path.write_text(json.dumps(profile), encoding="utf-8")
    loaded = ImportProfile.load(path)
    assert len(loaded.resources) == 62
    assert {rule.converter for rule in loaded.resources} == {SCREEN_APT_CONVERTER}


def test_a_tampered_plan_digest_is_refused(plan) -> None:
    tampered = dict(plan)
    tampered["aggregateSha256"] = "0" * 64
    with pytest.raises(ScreenAptProfileError, match="aggregate digest mismatch"):
        generated_import_profile(tampered)


def test_colliding_resource_identities_are_refused(plan) -> None:
    resources = plan["profileFragment"]["resources"]
    clashing = dict(plan)
    clashing["profileFragment"] = {"resources": [resources[0], dict(resources[0])]}
    clashing["aggregateSha256"] = "0" * 64
    with pytest.raises(ScreenAptProfileError):
        generated_import_profile(clashing)


def test_a_screen_bundle_may_exceed_the_ordinary_pattern_ceiling(plan) -> None:
    """A screen cannot be split, so it gets a stated ceiling of its own.

    Cooking a screen from partial sources is not a smaller cook, it is a wrong
    one - the same reason the terrain-material table has its own ceiling.  In
    RotWK - the shipping edition - the widest bundle is OnlineStrategic at 760
    paths and 32 of the 62 admitted screens exceed the ordinary 256.  Measuring
    this on BFME2 instead would have understated it badly: there the widest is
    SaveLoad at 491, so a ceiling sized to that edition would have been only
    2x the true worst case rather than comfortably above it.
    """

    assert MAX_PATTERNS_PER_RESOURCE == 256
    assert MAX_SCREEN_APT_PATTERNS == 1024
    widest = max(
        len(row["patterns"]) for row in plan["profileFragment"]["resources"]
    )
    assert widest == 760
    over = [
        row["options"]["movie"]
        for row in plan["profileFragment"]["resources"]
        if len(row["patterns"]) > MAX_PATTERNS_PER_RESOURCE
    ]
    assert len(over) == 32
    assert "OnlineStrategic" in over and "SaveLoad" in over
    # The ceiling still means something.
    assert widest < MAX_SCREEN_APT_PATTERNS


def test_a_screen_with_no_sources_is_refused() -> None:
    with pytest.raises(ScreenAptProfileError, match="declares no sources"):
        screen_profile_resource("SpellStore", [])
