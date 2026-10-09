"""Lane QA-1: the matrix runner's log parser and ranking (always), and the long game-flow runs (skipped by default).

The long runs play real games through godot/scripts/game.gd --qa (the scripted human, then an AI-vs-AI game) and need the built extension, the retail
installs and a few minutes each:  OPENBFME_QA_LONG=1 ROTWK_INSTALL=... BFME2_INSTALL=... python3 -m pytest -q tools/qa  (OPENBFME_QA_MINUTES: game minutes, default 15)
"""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("qa_matrix", HERE / "qa_matrix.py")
qa = importlib.util.module_from_spec(_spec)
sys.modules["qa_matrix"] = qa
_spec.loader.exec_module(qa)

LOG = """# godot --headless ...
GAME playing: frame 1, 120 objects, hash 5
QA ISSUE button_no_effect | MenBarracksButton | an enabled UNIT_BUILD button of MenBarracks (GondorFighterHorde) sent no message (logic frame 400)
QA ISSUE stall | logic | the logic frame stayed at 812 for 20 s of real time (logic frame 812)
ERROR: something failed at 0x1234 in frame 77
   at: some_function (core/foo.cpp:12)
WARNING: a warning
SCRIPT ERROR: Invalid access to property 'x' on a base object of type 'Dictionary'.
          at: _economy (res://scripts/qa_player.gd:250)
QA SUMMARY {"victory": true, "frame": 3000, "flow": {"end_screen": true, "score_screen": true, "continue_to_lobby": false}, "issues": [], "live_errors": ["object 12 lost its body at frame 5"], "stop_hits": {"[S-1234] a stop met at frame 9": 3}, "hud_unported_presses": {}, "unported_modules": {}}
QA RESULT: flow INCOMPLETE, 2 issues
"""


def test_parse_log_reads_every_kind_of_report():
    r = qa.parse_log(LOG)
    assert [i["kind"] for i in r["issues"]] == ["button_no_effect", "stall"]
    assert r["issues"][0]["subject"] == "MenBarracksButton"
    assert r["summary"]["victory"] is True
    assert r["godot_errors"] == ["something failed at 0x1234 in frame 77"]
    assert r["godot_warnings"] == ["a warning"]
    assert r["script_errors"][0].startswith("Invalid access to property 'x'") and "qa_player.gd:250" in r["script_errors"][0]
    assert r["result_line"].startswith("QA RESULT: flow INCOMPLETE")
    assert r["crash"] == [] and r["fails"] == []


def test_findings_and_rank_merge_runs_without_their_numbers():
    a = qa.parse_log(LOG)
    a.update(run={"tag": "a", "faction": "FactionMen", "map": "maps/x/x.map", "ai": 3, "opponents": 1, "minutes": 10}, exit=1)
    b = qa.parse_log(LOG.replace("frame 77", "frame 99").replace("frame 9", "frame 11"))
    b.update(run={"tag": "b", "faction": "FactionMen", "map": "maps/x/x.map", "ai": 3, "opponents": 1, "minutes": 10}, exit=1)
    c = qa.parse_log("QA SUMMARY {\"victory\": true, \"flow\": {\"end_screen\": true, \"score_screen\": true, \"continue_to_lobby\": true}}\n")
    c.update(run={"tag": "c", "faction": "FactionElves", "map": "maps/x/x.map", "ai": 3, "opponents": 1, "minutes": 10}, exit=0)
    kinds = {(k, s) for k, s, _ in qa.findings(a)}
    assert ("flow_incomplete", "continue_to_lobby") in kinds
    assert ("stop_hit", "S-1234") in kinds
    assert ("stall", "logic") in kinds
    assert qa.findings(c) == []
    ranked = qa.rank([a, b, c])
    # the same error in two runs with other numbers is one finding met in 2 of 3 runs
    err = [e for e in ranked if e["kind"] == "godot_error"]
    assert len(err) == 1 and len(err[0]["runs"]) == 2
    # the flow failure outranks a stop hit met as often
    order = [(e["kind"], e["subject"]) for e in ranked]
    assert order.index(("flow_incomplete", "continue_to_lobby")) < order.index(("stop_hit", "S-1234"))
    md = qa.markdown([a, b, c], ranked)
    assert "| a | FactionMen | x | medium x1 | victory | INCOMPLETE |" in md


def test_full_plan_covers_every_faction_map_size_and_ai_level():
    runs = qa.plan_runs("full", 10, 4)
    scripted = [r for r in runs if not r["idle"]]
    assert {r["faction"] for r in scripted} == set(qa.FACTIONS)
    for f in qa.FACTIONS:
        mine = [r for r in scripted if r["faction"] == f]
        assert {len([m for m in qa.MAPS if r["map"] in qa.MAPS[m]]) for r in mine} == {1}
        assert {next(k for k, v in qa.MAPS.items() if r["map"] in v) for r in mine} == {2, 4, 6, 8}
        assert {r["ai"] for r in mine} == {2, 3, 4, 5}
    assert len({r["tag"] for r in runs}) == len(runs)
    assert any(r["idle"] for r in runs)


LONG = os.environ.get("OPENBFME_QA_LONG") == "1"


@pytest.mark.skipif(not LONG, reason="the long game-flow runs: set OPENBFME_QA_LONG=1 (and ROTWK_INSTALL / BFME2_INSTALL, the built extension)")
@pytest.mark.parametrize("tag", ["smoke-men-evendim-medium", "smoke-aivai-udun"])
def test_long_game_flow_run(tmp_path, tag):
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        pytest.skip("ROTWK_INSTALL / BFME2_INSTALL are not set")
    run = next(r for r in qa.plan_runs("smoke", float(os.environ.get("OPENBFME_QA_MINUTES", "15")), 4.0) if r["tag"] == tag)
    res = qa.execute(run, tmp_path, os.environ.get("GODOT", "godot"), False, "", 3600)
    s = res["summary"]
    assert not res["crash"], res["crash"]
    assert res["exit"] == 0, (res["result_line"], res["fails"])
    assert s["flow"]["score_screen"] and s["flow"]["continue_to_lobby"]
    assert s["stalls"] == 0
    # leaving the game must not free the HUD's globe material under the shell player's live canvas items (QA-1 fix)
    assert not [e for e in res["godot_errors"] if 'Parameter "material" is null' in e], res["godot_errors"][:5]
    if not run["idle"]:
        # the scripted human built, trained and attacked through the HUD
        assert s["counts"].get("structures_ordered", 0) >= 3, s["counts"]
        assert s["counts"].get("units_queued", 0) >= 3, s["counts"]
        assert s["counts"].get("attack_orders", 0) >= 1, s["counts"]


def test_audio_unknown_event_names_are_data_facts_not_errors():
    # lane AUDIO-4 (U10): the names the data asks for that no INI defines are listed by name (retail refuses them silently, RW 0x45CEA7)
    run = {"idle": True, "faction": "FactionMen", "minutes": 1}
    result = {"run": run, "summary": {"victory": True, "audio": {"unknown_events": 1, "unknown_event_names": {"Gui_ShellMapSelect1": 1}, "errors": [], "missing_hooks": {}}}}
    found = qa.findings(result)
    assert ("audio_unknown_event_name", "Gui_ShellMapSelect1",
            "1 requests of 'Gui_ShellMapSelect1', which no INI defines (retail plays nothing)") in found
    assert not [f for f in found if f[0] in ("audio_error", "audio_unknown_events")]
    # an older summary without the names still reports the count
    old = qa.findings({"run": run, "summary": {"victory": True, "audio": {"unknown_events": 2}}})
    assert [f[0] for f in old if f[0].startswith("audio")] == ["audio_unknown_events"]


REPLAY_OK = """# godot --headless ...
GAME REPLAY frame 100 of 649, hashes compared 99, mismatches 0
GAME REPLAY finished: {"active":true,"complete":true,"final_frame":649,"finished":true,"first_mismatch_frame":-1,"frame":649,"hashes_compared":648,"lost":0,"mismatches":0}
GAME REPLAY RESULT: ok
"""


def test_replay_check_results_become_findings():
    # lane QA-2: a game's own replay played back must give the recorded frame hashes; a difference is a desync in a network game
    run = {"tag": "r", "idle": True, "faction": "FactionMen", "minutes": 1}
    ok = qa.parse_replay_log(REPLAY_OK, 0, 12.0)
    assert ok["result"] == "ok" and ok["status"]["hashes_compared"] == 648 and ok["first_mismatch"] == ""
    base = {"run": run, "summary": {"victory": True, "flow": {"end_screen": True, "score_screen": True, "continue_to_lobby": True}}, "replay_file": "r.replay"}
    assert qa.findings(dict(base, replay=ok)) == []
    bad_log = REPLAY_OK.replace('"first_mismatch_frame":-1', '"first_mismatch_frame":412').replace('"mismatches":0}', '"mismatches":237}') \
        .replace("RESULT: ok", "RESULT: FAILED") + "LiveGame: replay hash mismatch at frame 412: recorded 0x1 played 0x2\n"
    bad = qa.parse_replay_log(bad_log, 1, 12.0)
    found = qa.findings(dict(base, replay=bad))
    assert [f[:2] for f in found] == [("replay_mismatch", "determinism")]
    assert "237 of 648" in found[0][2] and "frame 412" in found[0][2]
    # a game with the --qa-cash test hook cannot replay (the grant is not a recorded command): reported as such, not as a desync
    cash = dict(base, replay=bad, run=dict(run, cash=20000))
    assert [f[:2] for f in qa.findings(cash)] == [("replay_test_hook", "qa-cash")]
    # a playback that never finished, and a game that left no replay
    stuck = qa.parse_replay_log("GAME REPLAY frame 1 of 0, hashes compared 0, mismatches 0\n", "timeout", 1200.0)
    assert [f[0] for f in qa.findings(dict(base, replay=stuck))] == ["replay_check_failed"]
    assert [f[0] for f in qa.findings(dict(base, replay_file=""))] == ["replay_missing"]
    assert qa.SEVERITY["replay_mismatch"] > qa.SEVERITY["stall"]


def test_qa2_extra_plan_has_idle_humans_alone_and_a_free_for_all():
    runs = qa.plan_runs("qa2-extra", 15, 4)
    assert all(r["idle"] for r in runs) and all(not r["teams"] for r in runs)
    assert {r["opponents"] for r in runs} == {1, 3, 7}
    assert len({r["tag"] for r in runs}) == len(runs)
    assert not {r["tag"] for r in runs} & {r["tag"] for r in qa.plan_runs("full", 15, 4)}


def test_scene_clip_rate_plays_the_recording_at_game_speed():
    spec = importlib.util.spec_from_file_location("qa_scenes", HERE / "qa_scenes.py")
    sc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sc)
    # 41 frames over 2 s of game time: 20 frames a game second
    assert abs(sc.clip_fps([(i, 50.0 * i) for i in range(41)]) - 20.0) < 1e-9
    assert sc.clip_fps([]) == 30.0 and sc.clip_fps([(0, 5.0)]) == 30.0
    assert set(sc.SCENES) == {"melee", "archers", "troll", "rohirrim", "charge", "garrison", "grond", "wall", "produce"}


def test_motion_section_lists_runs_and_the_ai_states():
    run = {"tag": "m", "faction": "FactionMen", "map": "maps/x/x.map", "ai": 3, "opponents": 1, "minutes": 10}
    a = {"run": run, "summary": {"counts": {"motion_probes": 450, "treadmill_units": 12, "stuck_units": 3, "treadmill_ai_state_7": 9,
                                           "treadmill_ai_state_0": 3, "stuck_ai_state_7": 3}}}
    b = {"run": dict(run, tag="n"), "summary": {"counts": {}}}
    md = qa.motion_section([a, b])
    assert "| m | 450 | 12 | 3 |" in md and not [l for l in md if l.startswith("| n |")]
    assert md.index("| treadmill_ai_state_7 | 9 |") < md.index("| treadmill_ai_state_0 | 3 |")
    assert qa.motion_section([b]) == []
