"""Lane AI-2: the logic random draws the RotWK offensive AI tactics can make (RW 0x90BE31's prototypes), read from the binary, against what the port makes.
SKIPPED LOUDLY without RW_GAME_DAT."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

# TARGET (RotWK game.dat, caveat S-001): the draw sites reachable from each tactic's setup / launch / started update (tactic_draws.py)
END_MERGE = ["real", "0x8F1F08"]          # RW 0x8F1D88, AITactic.cpp:0x3FC (the end's merge into a nearby AI team, only when candidates exist)
MIN_SIZE = ["int", "0x8F123A"]            # RW 0x8F121F, AITactic.cpp:0x1C7 (GetGameLogicRandomValue(3, 5))
FORMATION_MOVE = ["int", "0x774CEB"]      # AIGroup RW 0x774897 (the formation move)
EXPECTED = {
    "SimpleAttack": {"setup": [MIN_SIZE], "launch": [], "update": [END_MERGE]},
    "FormationAttack": {"setup": [], "launch": [END_MERGE], "update": [FORMATION_MOVE, END_MERGE]},
    "FlankAttack": {"setup": [MIN_SIZE], "launch": [["real", "0x9B4642"], ["int", "0x9B4720"], END_MERGE], "update": [END_MERGE]},
    "PincerAttack": {"setup": [], "launch": [["real", "0x9B4067"], END_MERGE], "update": [END_MERGE]},
    "FeintAttack": {"setup": [], "launch": [FORMATION_MOVE, ["real", "0x9B398D"], ["real", "0x9B3A5A"], ["int", "0x9B3AE4"]],
                    "update": [FORMATION_MOVE, ["real", "0x9B398D"], ["real", "0x9B3A5A"], ["int", "0x9B3AE4"], END_MERGE]},
    "AIBasePenetrationTroopsTactic": {"setup": [], "launch": [FORMATION_MOVE], "update": [END_MERGE]},
    "SimpleSiege": {"setup": [], "launch": [], "update": [END_MERGE]},
    "SiegeGates": {"setup": [], "launch": [END_MERGE], "update": [END_MERGE]},
}


def _sorted(table):
    return {n: {s: sorted(v, key=lambda x: x[1]) for s, v in slots.items()} for n, slots in table.items()}


def test_tactic_draw_sites_in_the_binary():
    path = os.environ.get("RW_GAME_DAT")
    if not path:
        pytest.skip("SKIPPED LOUDLY: RW_GAME_DAT is not set (the tactic draw sites cannot be read from the binary)")
    pytest.importorskip("capstone")
    from tactic_draws import draw_table

    assert _sorted(draw_table(Path(path).read_bytes())) == _sorted(EXPECTED)


def test_the_port_makes_the_ported_draws_and_reports_the_others():
    # the setup draw is ported (AITacticalAI.cpp setupTeam); every other site is named in the S-417 stop the manager reports
    src = (ROOT / "engine/src/GameLogic/SkirmishAI/AITacticalAI.cpp").read_text()
    assert '"AITactic.cpp", 0x1C7' in src
    # lane AI-2 r5: the bodies' own draws (binary site -> the port's file constant and line)
    for name, line in (("kFlankFile", "0x4E"), ("kFlankFile", "0x5B"), ("kPincerFile", "0x87"), ("kFeintFile", "0x43"), ("kFeintFile", "0x66"),
                       ("kFeintFile", "0x7A")):
        assert f"{name}, {line})" in src, (name, line)
    stops = (ROOT / "engine/src/GameLogic/SkirmishAI/SkirmishAIManager.cpp").read_text()
    s417 = stops[stops.index('"[S-417]'):]
    s417 = s417[:s417.index('",\n\t\t"[S-')]
    for slots in EXPECTED.values():
        for sites in slots.values():
            for _kind, site in sites:
                if site != MIN_SIZE[1]:
                    assert site in s417, site
