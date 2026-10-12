"""Lane BUGS-1: README.md's open-stop count is docs/STOPS.md's (tools/stops_count.py), so it cannot go stale."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import stops_count  # noqa: E402


def test_readme_open_stop_count_matches_stops_md():
    n = stops_count.open_count(stops_count.STOPS.read_text(encoding="utf-8"))
    claim = stops_count.readme_claim(stops_count.README.read_text(encoding="utf-8"))
    assert claim == n, f"README.md says {claim} open stops, docs/STOPS.md has {n}: run python3 tools/stops_count.py --write"


def test_open_count_skips_closed_rows_only():
    text = ("| ID | Kind |\n|---|---|\n"
            "| S-1 | code | a |\n| S-2 | project | b |\n| S-3 | closed | c |\n| S-40 | code | d |\n")
    assert stops_count.open_count(text) == 3


@pytest.mark.parametrize("text", ["| S-1 | cod | a |\n", "| S-1 | code | a |\n| S-1 | code | b |\n", "| S-1 |code| a |\n"])
def test_malformed_rows_are_errors(text):
    with pytest.raises(ValueError):
        stops_count.open_count(text)


def test_readme_claim_must_be_unique():
    with pytest.raises(ValueError):
        stops_count.readme_claim("**3 open stops** and **4 open stops**")
    assert stops_count.readme_claim("There are **1,203 open stops** today.") == 1203
