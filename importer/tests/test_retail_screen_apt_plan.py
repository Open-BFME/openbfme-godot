"""The screen plan builder refuses rather than guessing.

Queue Q117: `build_retail_hud_apt_plan` is a Men-HUD instrument, so cooking the
other retail screens needs a movie-agnostic plan. These cover the refusals this
module owns; the decoding underneath is `sage_apt`'s and is tested there.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_screen_apt_plan import (
    MAX_SCREEN_APT_BYTES,
    ScreenAptPlanError,
    build_screen_apt_plan,
    screen_movie_names,
)

REPO = Path(__file__).resolve().parents[2]


def test_movie_name_must_be_a_bare_identifier(tmp_path: Path) -> None:
    for bad in ("", "../Palantir", "Quit Menu", "Quit/Menu", "x" * 65):
        with pytest.raises(ScreenAptPlanError, match="bare identifier"):
            build_screen_apt_plan(tmp_path, bad)


def test_a_missing_source_is_a_refusal_not_a_partial_plan(tmp_path: Path) -> None:
    with pytest.raises(ScreenAptPlanError, match="screen source is missing"):
        build_screen_apt_plan(tmp_path, "QuitMenu")
    # The constants file is read first, so its absence is what surfaces even
    # when the .apt happens to be present.
    (tmp_path / "QuitMenu.apt").write_bytes(b"stub")
    with pytest.raises(ScreenAptPlanError, match=r"QuitMenu\.const"):
        build_screen_apt_plan(tmp_path, "QuitMenu")


def test_an_oversize_source_is_refused_against_its_stated_bound(tmp_path: Path) -> None:
    (tmp_path / "Big.const").write_bytes(b"\0" * (MAX_SCREEN_APT_BYTES + 1))
    with pytest.raises(ScreenAptPlanError, match="stated byte bound"):
        build_screen_apt_plan(tmp_path, "Big")


def test_screen_movie_names_needs_the_whole_trio(tmp_path: Path) -> None:
    (tmp_path / "Lonely.apt").write_bytes(b"stub")
    assert screen_movie_names(tmp_path) == ()
    (tmp_path / "Lonely.const").write_bytes(b"stub")
    assert screen_movie_names(tmp_path) == ()
    (tmp_path / "Lonely.dat").write_bytes(b"stub")
    assert screen_movie_names(tmp_path) == ("Lonely",)


def test_a_missing_atlas_directory_is_a_refusal(tmp_path: Path) -> None:
    """The atlas directory is retail structure, so its absence is not a gap."""

    source = REPO / "workspace/retail-work/cache/effective-assets"
    if not source.is_dir():
        pytest.skip("private effective-assets oracle is not present")
    for suffix in (".apt", ".const", ".dat"):
        (tmp_path / f"SpellStore{suffix}").write_bytes(
            (source / f"SpellStore{suffix}").read_bytes()
        )
    with pytest.raises(ScreenAptPlanError, match="atlas directory is missing"):
        build_screen_apt_plan(tmp_path, "SpellStore")


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_spellstore_flattens_at_its_authored_open_frame() -> None:
    """The first retail SCREEN reconstructed end to end, with real pixels.

    SpellStore labels `_open` on root frame 0, so its authored open state needs
    no accumulation - which is exactly why it is the honest first cook.  What
    this pins is that the plan resolves `apt_SpellStore_1.tga` through the .dat
    image map: before that wiring every one of its textured triangles was a
    `texture-assignment-unresolved` receipt and the screen drew nothing.
    """

    from openbfme_importer import retail_hud_apt_convert as convert

    root = REPO / "workspace/retail-work/cache/effective-assets"
    plan = build_screen_apt_plan(root, "SpellStore")
    assert [atlas["virtualPath"] for atlas in plan["atlases"]] == [
        "art/Textures/apt_SpellStore_1.tga"
    ]
    movie = convert._movie_from_plan(plan, asset_root=root)
    assert movie.atlases[1]["width"] == 1024

    flattener = convert._Flattener({movie.name.casefold(): movie}, {}, ())
    flattener.flatten_screen([("SpellStore", 0)])
    codes = {str(row["code"]) for row in flattener.blockers}
    assert "texture-assignment-unresolved" not in codes
    assert "unresolved-import-movie" not in codes
    assert "move-without-local-placement" not in codes
    textured = [row for row in flattener.draws if row["kind"] == "textured-triangle"]
    assert len(textured) == 150
    assert {row["atlas"] for row in textured} == {
        str(plan["atlases"][0]["cookedPng"])
    }


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_flatten_screen_refuses_a_frame_the_movie_does_not_have() -> None:
    from openbfme_importer import retail_hud_apt_convert as convert

    root = REPO / "workspace/retail-work/cache/effective-assets"
    movie = convert._movie_from_plan(
        build_screen_apt_plan(root, "SpellStore"), asset_root=root
    )
    flattener = convert._Flattener({movie.name.casefold(): movie}, {}, ())
    flattener.flatten_screen([("SpellStore", len(movie.frames))])
    assert [str(row["code"]) for row in flattener.blockers] == [
        "root-target-frame-out-of-range"
    ]
    assert flattener.draws == []


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_flagged_null_clip_actions_match_the_hand_measured_identities() -> None:
    """The enumerator agrees with two independently hand-measured closures.

    `retail_hud_apt_convert` fails closed on a PlaceObject whose clip-action
    flag is set while its pointer is null, admitting only exact
    (path, offset, flags) identities.  The Men-HUD closure hand-measured ONE -
    `InGameHeroSelect.apt` at offset 166756, flags 0xB6 - and the RotWK
    strategic closure hand-measured EIGHT in `TimeLine.apt`.  Walking the frame
    tables generically reproduces both numbers exactly, which is what makes the
    enumeration evidence rather than a rubber stamp on whatever the file holds.
    """

    from openbfme_importer.retail_hud_apt_convert import (
        _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS,
    )

    root = REPO / "workspace/retail-work/cache/effective-assets"
    hero = build_screen_apt_plan(root, "InGameHeroSelect")["flaggedNullClipActions"]
    assert [
        (row["virtualPath"], row["recordOffset"], row["flags"]) for row in hero
    ] == [("ingameheroselect.apt", 166756, 0xB6)]
    assert ("ingameheroselect.apt", 166756, 0xB6) in _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS
    timeline = build_screen_apt_plan(root, "TimeLine")["flaggedNullClipActions"]
    assert len(timeline) == 8
    assert {row["virtualPath"] for row in timeline} == {"timeline.apt"}

    # A screen that authors none reports none rather than an absent key.
    assert build_screen_apt_plan(root, "SpellStore")["flaggedNullClipActions"] == []


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_the_swf_base_block_opcodes_let_the_options_screen_reconstruct() -> None:
    """Five zero-operand SWF opcodes were the whole difference for 11 screens.

    The converter's opcode table follows the published SWF AVM1 assignment -
    which the table demonstrates about itself, since 0x30 random, 0x17 pop and
    0x62 bitwise-xor all sit where that spec puts them - and SAGE extends it
    only in the 0xAE-0xB9 range.  The five the retail screens needed are all
    zero-operand stack ops, so a mis-assignment could not decode silently: it
    would derail into the existing bounded-end refusal.  Options is the proof
    that it does not - it decodes and reconstructs its authored `_open` state.
    """

    from openbfme_importer import retail_hud_apt_convert as convert

    for opcode in (0x13, 0x18, 0x37, 0x60, 0x64):
        assert opcode in convert._ACTION_NAMES
        # Decodable is not executable: none of them joins the timeline subset.
        assert convert._ACTION_NAMES[opcode] not in convert._ACTION_TIMELINE_OPS

    root = REPO / "workspace/retail-work/cache/effective-assets"
    movie = convert._movie_from_plan(
        build_screen_apt_plan(root, "Options"), asset_root=root
    )
    assert convert._timeline_labels(movie.frames)["_open"] == 5
    flattener = convert._Flattener({movie.name.casefold(): movie}, {}, ())
    flattener.flatten_screen([("Options", 5)])
    assert len(flattener.draws) == 104
    assert {str(row["code"]) for row in flattener.blockers}.isdisjoint(
        {"texture-assignment-unresolved", "move-without-local-placement"}
    )
