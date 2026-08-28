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
