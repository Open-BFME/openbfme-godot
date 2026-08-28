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
