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
ASSETS = REPO / "workspace/retail-work/cache/effective-assets"

pytestmark = pytest.mark.skipif(
    not ASSETS.is_dir(), reason="private effective-assets oracle is not present"
)


@pytest.fixture(scope="module")
def plan() -> dict:
    return build_retail_screen_apt_plan(ASSETS)


def test_the_plan_admits_what_reconstructs_and_names_what_does_not(plan) -> None:
    assert plan["summary"] == {
        "movieCount": 84,
        "screenCount": 62,
        "refusedCount": 22,
        "drawCount": 13233,
        "sourceCount": 11664,
    }
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
    assert spellstore["drawCount"] == 150


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
    one - the same reason the terrain-material table has its own ceiling. The
    widest bundle in the tree is SaveLoad at 491 paths, well past the ordinary
    256; 14 of the 62 ADMITTED screens exceed it (18 across all 84 movies).
    """

    assert MAX_PATTERNS_PER_RESOURCE == 256
    assert MAX_SCREEN_APT_PATTERNS == 1024
    widest = max(
        len(row["patterns"]) for row in plan["profileFragment"]["resources"]
    )
    assert widest == 491
    over = [
        row["options"]["movie"]
        for row in plan["profileFragment"]["resources"]
        if len(row["patterns"]) > MAX_PATTERNS_PER_RESOURCE
    ]
    assert len(over) == 14
    assert "SaveLoad" in over
    # The ceiling still means something.
    assert widest < MAX_SCREEN_APT_PATTERNS


def test_a_screen_with_no_sources_is_refused() -> None:
    with pytest.raises(ScreenAptProfileError, match="declares no sources"):
        screen_profile_resource("SpellStore", [])
