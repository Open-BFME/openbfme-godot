"""Lane SCRIPT-1: the generated script template registry and the script census stay what the retail files say.

    RW_GAME_DAT=<RotWK game.dat>                       the registry (engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc) is re-extracted
    ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir>            the census (engine/tests/data/script-census.json) is recomputed (about a minute)

Each part SKIPs when its input is not configured."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent


@pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")
def test_template_tables_match_the_binary():
    r = subprocess.run([sys.executable, str(HERE / "extract_script_templates.py"), "--check"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


@pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")), reason="ROTWK_INSTALL / BFME2_INSTALL not set")
def test_census_matches_the_mount():
    r = subprocess.run([sys.executable, str(HERE / "script_census.py"), "--check"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_resolution_rules():
    sys.path.insert(0, str(HERE))
    import script_census as sc

    table = {0: ("CONDITION_FALSE", 0, False), 1: ("COUNTER", 3, False), 7: ("TEAM_INSIDE_AREA_PARTIALLY", 3, True)}
    # the stored ordinal's name matches
    assert sc.resolve(table, "condition", 1, "COUNTER", 3, 5) == 1
    # a renumbered record is found by name
    assert sc.resolve(table, "condition", 9, "TEAM_INSIDE_AREA_PARTIALLY", 3, 5) == 7
    # an unknown name, a wrong count and a version 3 record become CONDITION_FALSE
    assert sc.resolve(table, "condition", 9, "NOPE", 0, 5) == 0
    assert sc.resolve(table, "condition", 1, "COUNTER", 2, 5) == 0
    assert sc.resolve(table, "condition", 1, "COUNTER", 3, 3) == 0
    actions = {5: ("NO_OP", 0, True), 8: ("ENABLE_SCRIPT", 1, False)}
    assert sc.resolve(actions, "action", 3, "ENABLE_SCRIPT", 1, 3) == 8
    assert sc.resolve(actions, "action", 3, "GONE", 1, 3) == 5
    assert sc.resolve(actions, "action", 8, "ENABLE_SCRIPT", 2, 3) == 5
