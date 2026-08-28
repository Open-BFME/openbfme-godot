"""The screen plan builder refuses rather than guessing.

Queue Q117: `build_retail_hud_apt_plan` is a Men-HUD instrument, so cooking the
other retail screens needs a movie-agnostic plan. These cover the refusals this
module owns; the decoding underneath is `sage_apt`'s and is tested there.
"""

from __future__ import annotations

import re
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
def test_the_flagged_null_guard_is_frozen_not_self_admitting() -> None:
    """The walk is a VERIFIER, not a rubber stamp on whatever the file holds.

    An earlier revision enumerated these identities from the same file the
    parser was about to read and registered whatever it found, which made the
    fail-closed guard vacuous - the records it exists to refuse were
    pre-admitted by construction.  Counting them agreeing with a hand
    measurement proved the WALK was right, not that the records were authored.

    So it now diffs against a frozen table, and this pins that BOTH failure
    directions bite: an identity that is not frozen, and a frozen identity
    whose 64 bytes no longer hash the same.
    """

    from openbfme_importer import retail_screen_apt_plan as plan_module
    from openbfme_importer.retail_hud_apt_convert import (
        _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS,
    )
    from openbfme_importer.retail_strategic_apt_convert import (
        EXPECTED_FLAGGED_NULL_CLIP_ACTIONS,
    )

    root = REPO / "workspace/retail-work/cache/effective-assets"
    frozen = plan_module.FROZEN_FLAGGED_NULL_CLIP_ACTIONS
    assert len(frozen) == 54
    assert len({(row[0], row[1], row[2]) for row in frozen}) == 54
    assert all(len(row[3]) == 64 for row in frozen)

    # Two INDEPENDENT hand measurements land inside the freeze, which is what
    # makes the walk trustworthy in the first place.
    assert ("ingameheroselect.apt", 166756, 0xB6) in {
        (row[0], row[1], row[2]) for row in frozen
    }
    assert ("ingameheroselect.apt", 166756, 0xB6) in _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS
    # The strategic lane hand-froze RotWK's eight TimeLine records; every one of
    # them is in this freeze at the same offset and flags.
    strategic = {(path, offset, flags) for path, offset, flags in
                 EXPECTED_FLAGGED_NULL_CLIP_ACTIONS}
    assert len(strategic) == 8
    assert strategic <= {(row[0], row[1], row[2]) for row in frozen}

    # Direction 1: an identity that is not frozen is refused, by name.
    key = ("ingameheroselect.apt", 166756, 0xB6)
    saved = plan_module._FROZEN_FLAGGED_NULL_INDEX.pop(key)
    try:
        with pytest.raises(ScreenAptPlanError, match="unfrozen flagged-null"):
            build_screen_apt_plan(root, "InGameHeroSelect")
        # Direction 2: a frozen identity whose bytes changed is refused too.
        plan_module._FROZEN_FLAGGED_NULL_INDEX[key] = "0" * 64
        with pytest.raises(ScreenAptPlanError, match="record changed"):
            build_screen_apt_plan(root, "InGameHeroSelect")
    finally:
        plan_module._FROZEN_FLAGGED_NULL_INDEX[key] = saved

    # And it still verifies cleanly once restored.
    rows = build_screen_apt_plan(root, "InGameHeroSelect")["flaggedNullClipActions"]
    assert [(row["virtualPath"], row["recordOffset"], row["flags"]) for row in rows] == [
        ("ingameheroselect.apt", 166756, 0xB6)
    ]

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

    # Membership alone would pass if every name were wrong, so pin the NAMES
    # against the SAGE enum this repo already keeps in tree and already ports
    # into `game/src/apt/apt_vm.gd`.  That file is the oracle for this table.
    expected = {
        0x13: "string-equals",
        0x18: "to-integer",
        0x24: "clone-sprite",
        0x25: "remove-sprite",
        0x37: "mb-ascii-to-char",
        0x60: "bitwise-and",
        0x64: "bitwise-right-shift",
    }
    enum_source = (
        REPO
        / "workspace/scratch/opensage-source/src/OpenSage.Game/Gui/Apt"
        / "ActionScript/Opcodes/Instruction.cs"
    )
    if enum_source.is_file():
        text = enum_source.read_text(encoding="utf-8", errors="replace")
        enum_names = {
            int(value, 16): name
            for name, value in re.findall(
                r"(\w+)\s*=\s*(0x[0-9A-Fa-f]{2})\s*,", text
            )
        }
        for opcode in expected:
            assert opcode in enum_names, f"0x{opcode:02x} is not SAGE-attested"
        # The names we chose are the enum's names, in this project's spelling.
        assert enum_names[0x13] == "StringEquals"
        assert enum_names[0x18] == "ToInteger"
        assert enum_names[0x24] == "CloneSprite"
        assert enum_names[0x25] == "RemoveSprite"
        assert enum_names[0x37] == "MbChr"
        assert enum_names[0x60] == "BitwiseAnd"
        assert enum_names[0x64] == "ShiftRight"
        # And 0x1A - the one GuiTest still refuses on - is genuinely absent,
        # which is why that refusal is correct rather than a missing entry.
        assert 0x1A not in enum_names

    for opcode, name in expected.items():
        assert convert._ACTION_NAMES[opcode] == name
        # Decodable is not executable: none of them joins the timeline subset.
        assert name not in convert._ACTION_TIMELINE_OPS

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


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_an_empty_function_body_is_authored_data_not_corruption() -> None:
    """`function f() {}` compiles to a DefineFunction with body size zero.

    Eleven retail screens author one, and refusing them cost the game its MAIN
    MENU.  The bound is still enforced - a bounded range that ends anywhere
    other than its declared end is still a refusal - so this only stops an
    EMPTY body from being read as a truncated one.  MainMenu is the proof: it
    reconstructs its authored `_show`@1 state with real buttons and text.
    """

    from openbfme_importer import retail_hud_apt_convert as convert

    root = REPO / "workspace/retail-work/cache/effective-assets"
    plan = build_screen_apt_plan(root, "MainMenu")
    movie = convert._movie_from_plan(plan, asset_root=root)

    # The empty body MainMenu actually authors, decoded directly.
    assert convert._decode_action_sequence(movie, 144280, 144280) == ([], 144280)
    # A bounded range that stops short of its declared end is still refused.
    with pytest.raises(convert.HudAptConvertError):
        convert._decode_action_sequence(movie, 142340, 142341)

    assert convert._timeline_labels(movie.frames)["_show"] == 1
    flattener = convert._Flattener({movie.name.casefold(): movie}, {}, ())
    flattener.flatten_screen([("MainMenu", 1)])
    assert len(flattener.draws) == 20
    assert len(flattener.text_instances) == 10
    assert len(flattener.button_instances) == 5


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_the_import_closure_is_what_makes_a_screen_a_scene() -> None:
    """A screen alone is not a scene; its imports carry most of the pixels.

    ScoreScreen draws 184 primitives by itself and 770 with MenuExport and
    GameWindowGadgets loaded.  The converter already resolves imported
    characters through `movie.imports` - it only needs those movies present in
    the same dict, which is the service `MOVIE_CLOSURE` hardcodes for the
    Palantir and this computes.
    """

    from openbfme_importer import retail_hud_apt_convert as convert
    from openbfme_importer.retail_screen_apt_plan import build_screen_closure_plans

    root = REPO / "workspace/retail-work/cache/effective-assets"
    closure = build_screen_closure_plans(root, "ScoreScreen")
    assert sorted(closure["plans"]) == [
        "GameWindowGadgets",
        "MenuExport",
        "ScoreScreen",
    ]
    assert closure["unplannable"] == []

    movies = {}
    for plan in closure["plans"].values():
        convert.register_expected_flagged_null_clip_actions(
            (row["virtualPath"], row["recordOffset"], row["flags"])
            for row in plan["flaggedNullClipActions"]
        )
        loaded = convert._movie_from_plan(plan, asset_root=root)
        movies[loaded.name.casefold()] = loaded

    flattener = convert._Flattener(movies, {}, ())
    flattener.flatten_screen([("ScoreScreen", 9)])
    assert len(flattener.draws) == 770
    codes = {str(row["code"]) for row in flattener.blockers}
    assert "unresolved-import-movie" not in codes


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_an_unplannable_import_is_named_never_dropped_silently(tmp_path) -> None:
    """A movie the closure cannot plan is carried with its reason.

    Every import in the retail tree now plans, so this stages the failure: a
    tree holding MainMenu but not the two movies it imports.  What must never
    happen is those movies vanishing quietly, leaving a scene that looks whole
    and is missing its frame - the closure names both, and the converter's own
    `unresolved-import-movie` blocker still fires for them downstream.
    """

    from openbfme_importer.retail_screen_apt_plan import build_screen_closure_plans

    root = REPO / "workspace/retail-work/cache/effective-assets"
    for suffix in (".apt", ".const", ".dat"):
        (tmp_path / f"MainMenu{suffix}").write_bytes(
            (root / f"MainMenu{suffix}").read_bytes()
        )
    textures = tmp_path / "art" / "Textures"
    textures.mkdir(parents=True)
    (textures / "apt_MainMenu_1.tga").write_bytes(
        (root / "art" / "Textures" / "apt_MainMenu_1.tga").read_bytes()
    )
    geometry = tmp_path / "MainMenu_geometry"
    geometry.mkdir()
    for entry in (root / "MainMenu_geometry").iterdir():
        (geometry / entry.name).write_bytes(entry.read_bytes())

    closure = build_screen_closure_plans(tmp_path, "MainMenu")
    assert sorted(closure["plans"]) == ["MainMenu"]
    assert [row["movie"] for row in closure["unplannable"]] == [
        "GameWindowGadgets",
        "MenuExport",
    ]
    assert all("is missing" in row["reason"] for row in closure["unplannable"])

    # A screen whose OWN plan fails raises rather than reporting itself absent.
    with pytest.raises(ScreenAptPlanError, match="screen source is missing"):
        build_screen_closure_plans(tmp_path, "MenuExport")


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_an_authored_null_clip_action_pointer_is_named_not_fatal() -> None:
    """Admitting the flagged-null records exposed the next honest question.

    Once a PlaceObject with a null clip-action pointer PARSES, the flatten
    reaches it and finds no event list to bind - by construction, not by
    failure.  Raising there took down CahAppearance, CahManager, InGameChat and
    TimeLine entirely.  The record is already carried verbatim into the
    contract's `source-flagged-null-clip-action-pointer` evidence, so the
    flatten names it at the exact display path and keeps going.
    """

    from openbfme_importer import retail_hud_apt_convert as convert
    from openbfme_importer.retail_screen_apt_plan import build_screen_closure_plans

    root = REPO / "workspace/retail-work/cache/effective-assets"
    closure = build_screen_closure_plans(root, "CahAppearance")
    movies = {}
    for plan in closure["plans"].values():
        convert.register_expected_flagged_null_clip_actions(
            (row["virtualPath"], row["recordOffset"], row["flags"])
            for row in plan["flaggedNullClipActions"]
        )
        loaded = convert._movie_from_plan(plan, asset_root=root)
        movies[loaded.name.casefold()] = loaded

    flattener = convert._Flattener(movies, {}, ())
    flattener.flatten_screen([("CahAppearance", 0)])
    named = [
        row
        for row in flattener.blockers
        if str(row["code"]) == "clip-action-pointer-authored-null"
    ]
    assert named, "the authored-null records must stay visible, not vanish"
    for row in named:
        assert int(row["sourceOffset"]) > 0
        assert len(str(row["recordSha256"])) == 64
    assert flattener.draws


@pytest.mark.skipif(
    not (REPO / "workspace/retail-work/cache/effective-assets").is_dir(),
    reason="private effective-assets oracle is not present",
)
def test_importing_one_symbol_into_two_slots_is_authored_not_corrupt() -> None:
    """MpGameSetup binds `GameWindowGadgets::ComboBox` to slots 21 and 210.

    The parser rejected duplicate (movie, symbol) pairs, which took the
    multiplayer game-setup screen - one of the seven parity-gate reds - out of
    the lane over a legal authoring pattern.  The invariant that actually
    matters is that every import binds a DISTINCT local character slot, since
    that id is what resolution looks up.  Measured across all 84 movies in the
    tree: exactly one duplicates a symbol, and ZERO duplicate a character id.
    """

    from openbfme_importer import retail_hud_apt_convert as convert

    root = REPO / "workspace/retail-work/cache/effective-assets"
    plan = build_screen_apt_plan(root, "MpGameSetup")
    imports = plan["apt"]["imports"]
    slots = [int(row["characterId"]) for row in imports]
    assert len(slots) == len(set(slots)) == 24
    combo = [
        int(row["characterId"])
        for row in imports
        if str(row["symbol"]) == "ComboBox"
    ]
    assert sorted(combo) == [21, 210]

    movie = convert._movie_from_plan(plan, asset_root=root)
    flattener = convert._Flattener({movie.name.casefold(): movie}, {}, ())
    flattener.flatten_screen([("MpGameSetup", 0)])
    assert flattener.draws
