"""Archive-name resolution of the oracle scripts: names are matched case-insensitively inside the install
directory (as Windows does, so Linux installs with `Maps.big` work), the order asked for is kept, and a missing
required archive is an error, never a skip. Run: python -m pytest tools/maps/oracle -q (no game files needed)."""
import os
import sys

from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# census.py refuses to import without the install variables; placeholders only for the import, restored at once (a process-wide value would make the
# later retail tests of the run fail on "unused" instead of skipping)
with mock.patch.dict(os.environ, {"ROTWK_INSTALL": os.environ.get("ROTWK_INSTALL") or "unused",
                                  "BFME2_INSTALL": os.environ.get("BFME2_INSTALL") or "unused"}):
    import census  # noqa: E402
    import fullparse  # noqa: E402
import pytest  # noqa: E402


def touch(d, *names):
    for n in names:
        (d / n).write_bytes(b"")


def test_mixed_case_names_are_found_and_the_requested_order_is_kept(tmp_path):
    touch(tmp_path, "Maps.big", "LIBRARIES.BIG", "_patch201maps.big", "notes.txt")
    got = census.resolve_archives(str(tmp_path), ["_patch201maps.big", "maps.big", "libraries.big"])
    assert got == [str(tmp_path / "_patch201maps.big"), str(tmp_path / "Maps.big"), str(tmp_path / "LIBRARIES.BIG")]


def test_exact_case_name_resolves_to_itself(tmp_path):
    touch(tmp_path, "maps.big")
    assert census.resolve_archive(str(tmp_path), "maps.big") == str(tmp_path / "maps.big")


def test_a_missing_archive_raises_and_names_it(tmp_path):
    touch(tmp_path, "maps.big")
    with pytest.raises(FileNotFoundError, match="libraries.big"):
        census.resolve_archives(str(tmp_path), ["maps.big", "libraries.big"])


def test_a_missing_install_directory_raises(tmp_path):
    with pytest.raises(FileNotFoundError, match="maps.big"):
        census.resolve_archive(str(tmp_path / "nope"), "maps.big")


def test_two_names_differing_only_in_case_are_ambiguous(tmp_path):
    touch(tmp_path, "Maps.big", "MAPS.BIG")
    if len({p.name for p in tmp_path.iterdir()}) < 2:
        pytest.skip("case-insensitive file system")
    with pytest.raises(ValueError, match="ambiguous"):
        census.resolve_archive(str(tmp_path), "maps.big")


def test_fullparse_pure_archive_order_matches_the_survey_precedence(tmp_path):
    rw, b2 = tmp_path / "rw", tmp_path / "b2"
    rw.mkdir()
    b2.mkdir()
    touch(rw, "_Patch201maps.big", "_patch201.big", "DATA2.big", "Maps.big", "Libraries.big")
    touch(b2, "_patch103.big", "_patch101.big", "maps.big", "libraries.big", "Bases.big")
    got = [os.path.basename(p) for p in fullparse.resolve_pure_bigs(str(rw), str(b2))]
    assert got == ["_Patch201maps.big", "_patch201.big", "DATA2.big", "Maps.big", "Libraries.big",
                   "_patch103.big", "_patch101.big", "maps.big", "libraries.big", "Bases.big"]


def test_fullparse_missing_required_archive_raises(tmp_path):
    rw, b2 = tmp_path / "rw", tmp_path / "b2"
    rw.mkdir()
    b2.mkdir()
    touch(rw, "_patch201maps.big", "_patch201.big", "data2.big", "maps.big")  # libraries.big absent
    touch(b2, "_patch103.big", "_patch101.big", "maps.big", "libraries.big", "bases.big")
    with pytest.raises(FileNotFoundError, match="libraries.big"):
        fullparse.resolve_pure_bigs(str(rw), str(b2))
