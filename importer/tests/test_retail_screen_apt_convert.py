"""The first retail SCREENS cooked end to end - contract plus atlas pixels.

Queue Q117.  `convert_hud_apt_bundle` is the Men-HUD emitter and cannot take a
screen; this is the screen equivalent.  What these pin is the two things that
would otherwise be easy to fake: that the frame chosen is the one the movie's
own author NAMED, and that a screen which reconstructs nothing is REFUSED
rather than written out as an empty scene.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_screen_apt_convert import (
    OPEN_LABEL_PRIORITY,
    ScreenAptConvertError,
    build_screen_scene,
    convert_screen_apt,
    convert_screen_apt_tree,
    select_open_frame,
)

REPO = Path(__file__).resolve().parents[2]
ASSETS = REPO / "workspace/retail-work/cache/effective-assets"

pytestmark = pytest.mark.skipif(
    not ASSETS.is_dir(), reason="private effective-assets oracle is not present"
)


def test_the_frame_rule_prefers_the_authored_open_label() -> None:
    """The selection policy is the priority list and nothing else."""

    assert OPEN_LABEL_PRIORITY == ("_open", "_show", "_init", "_fadeIn")
    chosen = select_open_frame({"_init": 0, "_open": 9, "_popUp": 24})
    assert chosen["frame"] == 9
    assert chosen["label"] == "_open"
    assert chosen["rule"] == "authored-open-label"
    # Every alternative stays visible so the choice can be argued with.
    assert chosen["availableLabels"] == {"_init": 0, "_open": 9, "_popUp": 24}

    bare = select_open_frame({})
    assert (bare["frame"], bare["label"]) == (0, None)
    assert bare["rule"] == "no-authored-label-frame-zero"


def test_spellstore_cooks_to_a_contract_and_real_pixels(tmp_path: Path) -> None:
    contract = convert_screen_apt(ASSETS, "SpellStore", tmp_path / "out")

    assert contract["movie"] == "SpellStore"
    assert contract["closure"] == ["SpellStore"]
    assert contract["frameSelection"]["label"] == "_open"
    assert contract["frameSelection"]["frame"] == 0
    assert contract["stage"] == {
        "width": 1024,
        "height": 768,
        "frameCount": 32,
        "millisecondsPerFrame": 33,
    }
    assert contract["totals"]["draws"] == 150
    assert contract["totals"]["textInstances"] == 3
    # Blockers are carried verbatim, counted, never summarised away.
    assert sum(contract["blockerCounts"].values()) == contract["totals"]["blockers"]
    assert len(contract["blockers"]) == contract["totals"]["blockers"]

    written = sorted(
        path.relative_to(tmp_path / "out").as_posix()
        for path in (tmp_path / "out").rglob("*")
        if path.is_file()
    )
    assert written == [
        "assets/ui/screens/spellstore/apt-spellstore-1-6f967901b486.png",
        "data/ui/screens/spellstore/scene-contract.json",
    ]
    atlas = contract["atlases"][0]
    assert len(str(atlas["cookedPngSha256"])) == 64
    png = (tmp_path / "out" / Path(*str(atlas["cookedPng"]).split("/"))).read_bytes()
    assert png.startswith(b"\x89PNG\r\n\x1a\n")


def test_a_screen_that_reconstructs_nothing_is_refused(tmp_path: Path) -> None:
    """`libInGameUI` is a library, not a scene, and must not cook to an empty one.

    This is the failure mode a screen cooker most easily hides: emitting a
    contract with zero draws looks like success on every count except the one
    that matters.
    """

    with pytest.raises(ScreenAptConvertError, match="reconstructed nothing"):
        convert_screen_apt(ASSETS, "libInGameUI", tmp_path / "empty")
    # And nothing partial is left behind for a later run to mistake for a cook.
    assert not (tmp_path / "empty" / "data").exists()


def test_the_output_directory_must_be_empty(tmp_path: Path) -> None:
    target = tmp_path / "out"
    target.mkdir()
    (target / "stale.json").write_text("{}", encoding="utf-8")
    with pytest.raises(ScreenAptConvertError, match="must be empty"):
        convert_screen_apt(ASSETS, "SpellStore", target)


def test_the_import_closure_is_carried_into_the_contract() -> None:
    """ScoreScreen is a scene only WITH its imports; the contract says so."""

    contract = build_screen_scene(ASSETS, "ScoreScreen")
    assert contract["closure"] == ["GameWindowGadgets", "MenuExport", "ScoreScreen"]
    assert contract["frameSelection"]["label"] == "_open"
    assert contract["totals"]["draws"] == 770
    # One source row per movie in the closure, each hashed.
    assert [row["virtualPath"] for row in contract["sources"]] == [
        "GameWindowGadgets.apt",
        "MenuExport.apt",
        "ScoreScreen.apt",
    ]
    assert all(len(str(row["sha256"])) == 64 for row in contract["sources"])
    assert "unresolved-import-movie" not in contract["blockerCounts"]


def test_a_tree_cook_names_every_refusal(tmp_path: Path) -> None:
    result = convert_screen_apt_tree(
        ASSETS, ["SpellStore", "ScoreScreen", "libInGameUI"], tmp_path / "tree"
    )
    assert sorted(result["cooked"]) == ["ScoreScreen", "SpellStore"]
    assert result["totals"] == {"cooked": 2, "refused": 1, "draws": 920}
    assert [row["movie"] for row in result["refused"]] == ["libInGameUI"]
    assert "reconstructed nothing" in result["refused"][0]["reason"]


def test_the_source_bundle_is_stated_before_anything_is_read(tmp_path: Path) -> None:
    """The pipeline resolves converter inputs by virtual path, so a screen
    lane has to declare its whole source set up front - and a screen is not a
    scene without its imports, so the bundle spans the closure.
    """

    from openbfme_importer.retail_screen_apt_convert import (
        screen_bundle_virtual_paths,
    )
    from openbfme_importer.retail_screen_apt_plan import (
        ScreenAptPlanError,
        screen_source_virtual_paths,
    )

    own = screen_source_virtual_paths(ASSETS, "SpellStore")
    assert own[:3] == ("SpellStore.apt", "SpellStore.const", "SpellStore.dat")
    assert own[-1] == "art/Textures/apt_SpellStore_1.tga"
    assert all(path.startswith("SpellStore_geometry/") for path in own[3:-1])

    # SpellStore imports nothing, so its bundle is its own sources.
    assert screen_bundle_virtual_paths(ASSETS, "SpellStore") == own
    # ScoreScreen does, so its bundle is strictly larger and covers them.
    bundle = screen_bundle_virtual_paths(ASSETS, "ScoreScreen")
    assert len(bundle) > len(screen_source_virtual_paths(ASSETS, "ScoreScreen"))
    for name in ("GameWindowGadgets", "MenuExport", "ScoreScreen"):
        assert f"{name}.apt" in bundle
    assert len(bundle) == len(set(bundle))

    with pytest.raises(ScreenAptPlanError, match="screen source is missing"):
        screen_source_virtual_paths(tmp_path, "SpellStore")


def test_cooking_from_a_source_mapping_matches_cooking_from_the_tree(
    tmp_path: Path,
) -> None:
    """The pipeline hands over extracted archive entries, not a tree.

    Staging that mapping into the retail layout and running the SAME code path
    keeps one honest reader instead of two, and this pins that the two entry
    points genuinely agree - identical source aggregate, identical draws.
    """

    from openbfme_importer.retail_screen_apt_convert import (
        convert_screen_apt_bundle,
        screen_bundle_virtual_paths,
    )

    from_tree = convert_screen_apt(ASSETS, "SpellStore", tmp_path / "tree")
    sources = {
        path: ASSETS.joinpath(*path.split("/"))
        for path in screen_bundle_virtual_paths(ASSETS, "SpellStore")
    }
    from_bundle = convert_screen_apt_bundle(sources, "SpellStore", tmp_path / "bundle")

    assert (
        from_bundle["sourceAggregateSha256"] == from_tree["sourceAggregateSha256"]
    )
    assert from_bundle["totals"] == from_tree["totals"]
    assert from_bundle["draws"] == from_tree["draws"]
    assert (tmp_path / "bundle" / "data/ui/screens/spellstore/scene-contract.json").is_file()

    with pytest.raises(ScreenAptConvertError, match="unsafe"):
        convert_screen_apt_bundle(
            {"../escape.apt": b"stub"}, "SpellStore", tmp_path / "unsafe"
        )
