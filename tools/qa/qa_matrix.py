#!/usr/bin/env python3
"""Lane QA-1: plays the skirmish matrix through the real game flow and ranks what breaks.

Each run is one process of the game scene (godot/scripts/game.gd) with --auto --qa: main menu -> Skirmish lobby (mouse events) -> load -> the scripted
human (godot/scripts/qa_player.gd) plays the local side through the HUD's input, or watches an AI game (--qa-idle) -> the end of the game -> the score
screen -> Continue back to the lobby. The run's output is kept; this script extracts

  - the scripted player's findings (QA ISSUE lines, the QA SUMMARY json: buttons pressed without effect, units never produced, live errors, stop hits),
  - the engine's and Godot's own reports (ERROR / WARNING / SCRIPT ERROR lines, GAME FAIL, crashes and their backtraces),
  - the flow (end screen, score screen, Continue), the exit code and the time,

and aggregates them over the runs into a ranked list: how many runs meet the problem x how bad it is.

Lane QA-2: --userdata DIR gives the games their own Godot user data folder (XDG_DATA_HOME), so parallel streams and other test runs on the same
machine never share the recorded replay (every skirmish is recorded to Replays/Last Replay.replay); the run's replay is kept as <tag>.replay.
--replay-check then plays each game's replay back (game.gd --replay=FILE --replay-check at --replay-speed times speed) and reports a replay whose
frame hashes differ from the recording (the game is not deterministic: a desync in a network game) as replay_mismatch.

  python3 tools/qa/qa_matrix.py run --out DIR [--plan smoke|full|<json file>] [--only TAG ...] [--windowed] [--screens DIR]
                                   [--userdata DIR [--replay-check [--replay-speed N]]]
  python3 tools/qa/qa_matrix.py rank --out DIR [--markdown FILE]

Needs ROTWK_INSTALL / BFME2_INSTALL (the runs SKIP without them) and GODOT (default: godot on the PATH) with the openbfme extension built into godot/bin.
Nothing retail is written: the logs and the screenshots go to --out / --screens (outside the repository).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PROJECT = Path(os.environ["QA_PROJECT"]) if os.environ.get("QA_PROJECT") else ROOT / "godot"

FACTIONS = ["FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar"]
MAPS = {
    2: ["maps/map mp fords of isen ii/map mp fords of isen ii.map", "maps/map mp umbar/map mp umbar.map", "maps/map mp tournament mp1/map mp tournament mp1.map"],
    4: ["maps/map mp tournament udun/map mp tournament udun.map", "maps/map mp weathertop/map mp weathertop.map", "maps/map mp fall back 4p/map mp fall back 4p.map"],
    6: ["maps/map mp anfalas/map mp anfalas.map", "maps/map mp brown lands/map mp brown lands.map"],
    8: ["maps/map mp evendim/map mp evendim.map", "maps/map mp adorn river/map mp adorn river.map", "maps/map mp tournament hills/map mp tournament hills.map"],
}
AI_LEVELS = {2: "easy", 3: "medium", 4: "hard", 5: "brutal"}

# how bad a problem is when a player meets it (the ranking multiplies it with the share of runs that meet it)
SEVERITY = {
    "crash": 10, "flow_incomplete": 8, "stall": 8, "game_fail": 8, "timeout": 8,
    "unit_never_produced": 5, "structure_not_started": 5, "no_legal_site": 4, "build_button_no_placement": 5, "placement_no_message": 5,
    "attack_no_message": 6, "select_all_empty": 5, "select_failed": 3, "select_occluded": 0, "select_missed": 2, "button_no_effect": 4, "button_unported": 4, "target_click_no_message": 3,
    "spell_no_message": 3, "button_window_missing": 3, "no_command_set": 3, "no_command_center": 6, "placement_no_ghost": 2,
    "live_error": 3, "hud_error": 2, "shell_error": 2, "load_error": 3, "godot_error": 3, "script_error": 6, "godot_warning": 1,
    "stop_hit": 1, "unported_module": 1, "never_won": 2, "construction_stalled": 5, "site_vanished": 4, "resume_not_sent": 5, "attack_move_not_sent": 4,
    "ability_no_effect": 3, "audio_error": 3, "audio_unknown_events": 2, "audio_unknown_event_name": 1, "audio_play_failures": 3, "audio_missing_hook": 2, "audio_calls_without_audio": 2,
    "audio_calls_without_eva": 2, "audio_unit_voices_without_handler": 2, "audio_ambient_unknown_event": 1, "audio_music_track_failures": 3, "audio_music_error": 3,
    "audio_music_unported": 1, "fx_error": 3, "fx_missing_list": 2,
    # lane QA-2
    "replay_mismatch": 9, "replay_missing": 4, "replay_check_failed": 6, "treadmill": 5, "stuck_moving": 4, "unit_producer_gone": 1, "unit_waits_command_points": 1, "replay_test_hook": 0,
}

REPLAY_NAME = "Last Replay.replay"  # GUI:LastReplay of the retail strings (game.gd _last_replay_path)


def plan_runs(plan: str, minutes: float, speed: float) -> list[dict]:
    """The runs of a plan: a list of {tag, faction, map, ai, opponents, idle, teams, minutes, speed, seed}."""
    runs: list[dict] = []
    if plan == "smoke":
        runs.append(dict(tag="smoke-men-evendim-medium", faction="FactionMen", map=MAPS[8][0], ai=3, opponents=1, idle=False, minutes=minutes))
        runs.append(dict(tag="smoke-aivai-udun", faction="FactionMordor", map=MAPS[4][0], ai=4, opponents=3, idle=True, teams="0,0,1,1", minutes=minutes))
    elif plan == "victory":
        # the end of a won game through the real flow: every faction against one Easy computer on a 2-player map, with a starting grant (--qa-cash)
        for i, faction in enumerate(FACTIONS):
            runs.append(dict(tag=f"win-{faction[7:].lower()}", faction=faction, map=MAPS[2][i % len(MAPS[2])], ai=2, opponents=1, idle=False,
                             minutes=minutes, cash=20000, seed=3000 + i))
    elif plan == "full":
        # every faction as the human: the map size and the AI level rotate so each faction meets 2/4/6/8-player maps and every difficulty over the plan
        sizes = [2, 4, 6, 8]
        n = 0
        for fi, faction in enumerate(FACTIONS):
            for k in range(4):
                size = sizes[(fi + k) % 4]
                maps = MAPS[size]
                level = 2 + (fi + k) % 4
                opponents = 1 if size == 2 else min(size - 1, 3)
                runs.append(dict(tag=f"{faction[7:].lower()}-{size}p-{AI_LEVELS[level]}", faction=faction, map=maps[(fi + k) % len(maps)], ai=level,
                                 opponents=opponents, idle=False, minutes=minutes, seed=1000 + n))
                n += 1
        # long AI-vs-AI games (the local side idle on a team with an AI ally)
        for i, (size, level, teams) in enumerate([(4, 4, "0,0,1,1"), (8, 5, "0,0,1,1"), (6, 3, "0,0,1,1"), (4, 5, "0,1,0,1")]):
            opponents = 3
            runs.append(dict(tag=f"aivai-{size}p-{AI_LEVELS[level]}", faction=FACTIONS[(i * 3) % 7], map=MAPS[size][i % len(MAPS[size])], ai=level,
                             opponents=opponents, idle=True, teams=teams, minutes=minutes * 2, seed=2000 + i))
    elif plan == "qa2-extra":
        # lane QA-2: the idle human alone against computers (must be attacked and beaten), and an 8-player free-for-all of computers (a big, long game)
        runs.append(dict(tag="idle-2p-hard", faction="FactionElves", map=MAPS[2][0], ai=4, opponents=1, idle=True, minutes=minutes * 2, seed=4000))
        runs.append(dict(tag="idle-4p-brutal-ffa", faction="FactionDwarves", map=MAPS[4][1], ai=5, opponents=3, idle=True, minutes=minutes * 2, seed=4001))
        runs.append(dict(tag="idle-2p-easy", faction="FactionWild", map=MAPS[2][2], ai=2, opponents=1, idle=True, minutes=minutes * 2, seed=4002))
        runs.append(dict(tag="aivai-8p-ffa-hard", faction="FactionMen", map=MAPS[8][2], ai=4, opponents=7, idle=True, minutes=minutes * 3, seed=4003))
    else:
        runs = json.loads(Path(plan).read_text())
    for r in runs:
        r.setdefault("speed", speed)
        r.setdefault("seed", 1234)
        r.setdefault("teams", "")
        r.setdefault("minutes", minutes)
    return runs


def command(run: dict, godot: str, windowed: bool, screens: str) -> list[str]:
    cmd = [godot]
    if not windowed:
        cmd.append("--headless")
    cmd += ["--path", str(PROJECT), "--", "--auto", "--qa-idle" if run.get("idle") else "--qa", f"--faction={run['faction']}", f"--map={run['map']}",
            f"--ai={run['ai']}", f"--opponents={run['opponents']}", f"--seed={run['seed']}", f"--qa-minutes={run['minutes']}", f"--qa-speed={run['speed']}",
            f"--qa-tag={run['tag']}"]
    if run.get("teams"):
        cmd.append(f"--teams={run['teams']}")
    if run.get("cash"):
        cmd.append(f"--qa-cash={run['cash']}")
    cmd += ["--res=1280x720"]  # also headless: the HUD's picks and the Palantir's buttons need a real screen size (the headless window is 64 x 64)
    if screens:
        cmd.append(f"--screens={screens}")
    return cmd


def _replay_folder(userdata: str) -> Path:
    """The Replays folder of the games run with XDG_DATA_HOME = userdata (Godot: <data home>/godot/app_userdata/<project name>)."""
    return Path(userdata) / "godot" / "app_userdata" / "OpenBFME" / "Replays"


def check_replay(replay: Path, out: Path, tag: str, godot: str, userdata: str, speed: float, timeout: float) -> dict:
    """Plays a recorded game back headless and compares every frame's hash with the recording (game.gd --replay-check)."""
    log = out / f"{tag}.replay.log"
    cmd = [godot, "--headless", "--path", str(PROJECT), "--", f"--replay={replay}", "--replay-check", f"--replay-speed={speed}", "--no-record",
           "--res=1280x720"]
    env = dict(os.environ)
    env.setdefault("MALLOC_ARENA_MAX", "2")
    env["XDG_DATA_HOME"] = userdata
    t0 = time.time()
    with log.open("w", encoding="utf-8", errors="replace") as fh:
        fh.write("# " + " ".join(cmd) + "\n")
        fh.flush()
        try:
            code = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, env=env, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    return parse_replay_log(log.read_text(encoding="utf-8", errors="replace"), code, round(time.time() - t0, 1))


def parse_replay_log(text: str, code, seconds: float) -> dict:
    """The result of a --replay-check run: ok / FAILED / none, and the playback's final status (frames, hashes compared, mismatches)."""
    res = {"exit": code, "seconds": seconds, "result": "none", "status": {}, "first_mismatch": ""}
    for line in text.splitlines():
        if line.startswith("GAME REPLAY RESULT:"):
            res["result"] = line.split(":", 1)[1].strip()
        elif line.startswith("GAME REPLAY finished:"):
            try:
                res["status"] = json.loads(line.split(":", 1)[1])
            except json.JSONDecodeError:
                pass
        elif "mismatch" in line.lower() and not res["first_mismatch"] and not line.startswith("GAME REPLAY frame"):
            res["first_mismatch"] = line.strip()[:300]
    return res


def execute(run: dict, out: Path, godot: str, windowed: bool, screens: str, timeout: float, userdata: str = "") -> dict:
    log = out / f"{run['tag']}.log"
    cmd = command(run, godot, windowed, screens)
    env = dict(os.environ)
    env.setdefault("MALLOC_ARENA_MAX", "2")
    if userdata:
        env["XDG_DATA_HOME"] = userdata
        (_replay_folder(userdata) / REPLAY_NAME).unlink(missing_ok=True)
    t0 = time.time()
    with log.open("w", encoding="utf-8", errors="replace") as fh:
        fh.write("# " + " ".join(cmd) + "\n")
        fh.flush()
        try:
            proc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, env=env, timeout=timeout)
            code = proc.returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    result = parse_log(log.read_text(encoding="utf-8", errors="replace"))
    result.update(run=run, exit=code, seconds=round(time.time() - t0, 1), log=log.name)
    if userdata:
        rec = _replay_folder(userdata) / REPLAY_NAME
        result["replay_file"] = ""
        if rec.exists():
            kept = out / f"{run['tag']}.replay"
            rec.replace(kept)
            result["replay_file"] = kept.name
    (out / f"{run['tag']}.json").write_text(json.dumps(result, indent=1, sort_keys=True))
    return result


_NOISE = (
    "ERROR: Condition \"!is_inside_tree()\"",  # (none known yet; kept for explicit filtering later)
)


def parse_log(text: str) -> dict:
    """Everything a run reports, from its output."""
    issues, errors, warnings, script_errors, crash, fails = [], [], [], [], [], []
    summary: dict = {}
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("QA ISSUE "):
            parts = [p.strip() for p in line[len("QA ISSUE "):].split("|", 2)]
            while len(parts) < 3:
                parts.append("")
            issues.append({"kind": parts[0], "subject": parts[1], "detail": parts[2]})
        elif line.startswith("QA SUMMARY "):
            try:
                summary = json.loads(line[len("QA SUMMARY "):])
            except json.JSONDecodeError as e:
                fails.append(f"QA SUMMARY is not json: {e}")
        elif line.startswith("SCRIPT ERROR:"):
            where = lines[i + 1].strip() if i + 1 < len(lines) else ""
            script_errors.append(line[len("SCRIPT ERROR:"):].strip() + (" @ " + where if where else ""))
        elif line.startswith("ERROR:") and not any(line.startswith(n) for n in _NOISE):
            errors.append(line[len("ERROR:"):].strip())
        elif line.startswith("WARNING:"):
            warnings.append(line[len("WARNING:"):].strip())
        elif "handle_crash" in line or "Program crashed" in line or line.startswith("[") and "] " in line and "crash" in line.lower():
            crash.append(line.strip())
        elif line.startswith("GAME FAIL"):
            fails.append(line.strip())
    return {"issues": issues, "summary": summary, "godot_errors": errors, "godot_warnings": warnings, "script_errors": script_errors, "crash": crash,
            "fails": fails, "result_line": next((l for l in lines if l.startswith("QA RESULT")), "")}


_NUM = re.compile(r"\d+(\.\d+)?")


def _norm(text: str) -> str:
    """A problem's identity across runs: the numbers (ids, frames, positions, counts) are not part of it."""
    return _NUM.sub("#", text)[:240]


def _stop_id(text: str) -> str:
    m = re.search(r"\[?(S-\d+)\]?", text)
    return m.group(1) if m else _norm(text)[:80]


def findings(result: dict) -> list[tuple[str, str, str]]:
    """(kind, subject, detail) of one run."""
    out: list[tuple[str, str, str]] = []
    s = result.get("summary", {})
    if result.get("crash"):
        out.append(("crash", "process", result["crash"][0]))
    if result.get("exit") == "timeout":
        out.append(("timeout", "process", "the run did not finish in time"))
    for f in result.get("fails", []):
        out.append(("game_fail", "flow", f))
    flow = s.get("flow", {})
    if not s:
        if not result.get("crash") and result.get("exit") != "timeout":
            out.append(("flow_incomplete", "summary", "no QA SUMMARY (exit %s)" % result.get("exit")))
    else:
        for k in ("score_screen", "continue_to_lobby"):
            if not flow.get(k, False):
                out.append(("flow_incomplete", k, f"the {k} step of the end flow failed: {json.dumps(flow)}"))
        if (s.get("victory") or s.get("defeat")) and not flow.get("end_screen"):
            out.append(("flow_incomplete", "end_screen", "the game ended but the end screen was not shown"))
        if not s.get("victory") and not result["run"].get("idle"):
            out.append(("never_won", result["run"]["faction"], "the scripted player did not win in %s game minutes" % result["run"]["minutes"]))
    seen = set()
    for i in result.get("issues", []) + list(s.get("issues", [])):
        key = (i["kind"], i["subject"])
        if key in seen:
            continue
        seen.add(key)
        out.append((i["kind"], i["subject"], i["detail"]))
    for e in s.get("live_errors", []):
        out.append(("live_error", _norm(e)[:80], e))
    for e in s.get("hud_errors", []):
        out.append(("hud_error", _norm(e)[:80], e))
    for e in s.get("shell_errors", []):
        out.append(("shell_error", _norm(str(e))[:80], str(e)))
    for e in s.get("load_errors", []):
        out.append(("load_error", _norm(e)[:80], e))
    au = s.get("audio", {})
    for e in au.get("errors", []):
        out.append(("audio_error", _norm(str(e))[:80], str(e)))
    names = au.get("unknown_event_names", {})
    for name, n in names.items():
        # lane AUDIO-4 (U10): a name the data asks for that no INI defines; retail refuses it silently (RW 0x45CEA7), so this is a data fact to
        # check against the INI, not a port failure (QuitMenu.apt's "Gui_ShellMapSelect1" is one)
        out.append(("audio_unknown_event_name", str(name)[:80], f"{n} requests of '{name}', which no INI defines (retail plays nothing)"))
    if au.get("unknown_events", 0) and not names:
        out.append(("audio_unknown_events", "audio", f"{au['unknown_events']} sound requests named no known event"))
    if au.get("play_failures", 0):
        out.append(("audio_play_failures", "audio", f"{au['play_failures']} sounds failed to play"))
    for h in au.get("missing_hooks", []):
        out.append(("audio_missing_hook", str(h)[:80], str(h)))
    ga = s.get("game_audio", {})
    for k in ("calls_without_audio", "calls_without_eva", "unit_voices_without_handler", "ambient_unknown_event", "music_track_failures"):
        if ga.get(k, 0):
            out.append(("audio_" + k, "audio", f"{ga[k]} {k.replace('_', ' ')}"))
    if ga.get("music_error"):
        out.append(("audio_music_error", "music", str(ga["music_error"])))
    for k, v in ga.get("music_unported", {}).items():
        out.append(("audio_music_unported", k, f"{v} music script actions not ported"))
    fx = s.get("fx", {})
    for e in fx.get("setup_errors", []):
        out.append(("fx_error", _norm(str(e))[:80], str(e)))
    for name in fx.get("missing_fx_lists", []):
        out.append(("fx_missing_list", str(name), "an FXList the game asked for is unknown"))
    # lane FX-3: fx.unresolved_particle_systems (names RW 0x73AECB also resolves to NULL: retail plays nothing) and fx.missing_bones (bones the retail
    # models lack: RW 0x4C6514 plays the system at the drawable's origin) are retail data played the retail way, not problems of the port
    for k, v in s.get("hud_unported_presses", {}).items():
        out.append(("button_unported", k, f"pressed {v} times"))
    for k, v in s.get("unported_modules", {}).items():
        out.append(("unported_module", k, f"{v} objects carry it"))
    for k, v in s.get("stop_hits", {}).items():
        out.append(("stop_hit", _stop_id(k), f"{v} hits: {k}"))
    for e in result.get("script_errors", []):
        out.append(("script_error", _norm(e)[:100], e))
    for e in result.get("godot_errors", []):
        out.append(("godot_error", _norm(e)[:100], e))
    for e in result.get("godot_warnings", []):
        out.append(("godot_warning", _norm(e)[:100], e))
    # lane QA-2: the game played back from its own recording must give the same frame hashes
    if "replay_file" in result and not result["replay_file"] and s:
        out.append(("replay_missing", "replay", "the game left no recorded replay"))
    rp = result.get("replay")
    if rp:
        st = rp.get("status", {})
        if rp.get("result") == "ok":
            pass
        elif st.get("mismatches", 0) and result["run"].get("cash"):
            # the --qa-cash grant (GameWorld.give_money) changes the logic outside the command stream, so the recording cannot hold it: the
            # playback differs from the grant's frame on. Not a determinism finding.
            out.append(("replay_test_hook", "qa-cash", f"the --qa-cash grant is not a recorded command: the playback differs from frame "
                        f"{st.get('first_mismatch_frame')} (not comparable)"))
        elif st.get("mismatches", 0):
            out.append(("replay_mismatch", "determinism", f"{st.get('mismatches')} of {st.get('hashes_compared')} frame hashes differ "
                        f"(played to frame {st.get('frame')} of {st.get('final_frame')}): {rp.get('first_mismatch', '')}"))
        else:
            out.append(("replay_check_failed", "replay", f"the playback did not finish (exit {rp.get('exit')}, result {rp.get('result')}): {json.dumps(st)[:200]}"))
    return out


def rank(results: list[dict]) -> list[dict]:
    table: dict[tuple[str, str], dict] = {}
    n = len(results)
    for r in results:
        for kind, subject, detail in findings(r):
            e = table.setdefault((kind, subject), {"kind": kind, "subject": subject, "detail": detail, "runs": [], "severity": SEVERITY.get(kind, 2)})
            if r["run"]["tag"] not in e["runs"]:
                e["runs"].append(r["run"]["tag"])
    out = []
    for e in table.values():
        e["share"] = len(e["runs"]) / max(n, 1)
        e["score"] = round(e["severity"] * e["share"], 3)
        out.append(e)
    out.sort(key=lambda e: (-e["score"], -e["severity"], e["kind"], e["subject"]))
    return out


def markdown(results: list[dict], ranked: list[dict]) -> str:
    lines = ["# QA-1 matrix results (generated by tools/qa/qa_matrix.py rank)", ""]
    lines.append("| run | faction | map | AI | result | flow | frame | real s | issues |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for r in results:
        s = r.get("summary", {})
        res = "victory" if s.get("victory") else ("defeat" if s.get("defeat") else ("crash" if r.get("crash") else ("timeout" if r.get("exit") == "timeout" else "time up")))
        flow = s.get("flow", {})
        fl = "complete" if flow.get("score_screen") and flow.get("continue_to_lobby") else "INCOMPLETE"
        m = r["run"]["map"].split("/")[-1].replace(".map", "")
        lines.append(f"| {r['run']['tag']} | {r['run']['faction']} | {m} | {AI_LEVELS.get(r['run']['ai'], r['run']['ai'])} x{r['run']['opponents']} | {res} | {fl} | "
                     f"{s.get('frame', '-')} | {r.get('seconds', '-')} | {len(findings(r))} |")
    lines += motion_section(results)
    lines += ["", "## Ranked findings (score = severity x share of runs)", "", "| # | score | kind | subject | runs | example |", "|---|---|---|---|---|---|"]
    for i, e in enumerate(ranked, 1):
        detail = e["detail"].replace("|", "/")[:200]
        lines.append(f"| {i} | {e['score']} | {e['kind']} | {e['subject'].replace('|', '/')} | {len(e['runs'])}/{len(results)} | {detail} |")
    return "\n".join(lines) + "\n"


def motion_section(results: list[dict]) -> list[str]:
    """Lane QA-2: the motion invariants of each run (qa_player.gd _probe_motion) and the AI states that leave a standing unit running."""
    rows = []
    states: dict[str, int] = {}
    for r in results:
        c = r.get("summary", {}).get("counts", {})
        if "motion_probes" not in c:
            continue
        rows.append(f"| {r['run']['tag']} | {c.get('motion_probes', 0)} | {c.get('treadmill_units', 0)} | {c.get('treadmill_melee_units', 0)} | {c.get('stuck_units', 0)} |")
        for k, v in c.items():
            if k.startswith("treadmill_ai_state_") or k.startswith("stuck_ai_state_"):
                states[k] = states.get(k, 0) + v
    if not rows:
        return []
    out = ["", "## Motion invariants (lane QA-2)", "",
           "treadmill = a unit standing still with the MOVING model condition for 3 probes (30 logic frames) in a row; stuck = the AI says moving, the "
           "unit stood still for 150 logic frames; melee treadmill = the treadmill units that were soldiers of a horde in a melee (included in the "
           "treadmill count).", "", "| run | probes | treadmill units | melee treadmill | stuck units |", "|---|---|---|---|---|"] + rows
    if states:
        out += ["", "| AI state of the unit when met (all runs) | units |", "|---|---|"]
        out += [f"| {k} | {v} |" for k, v in sorted(states.items(), key=lambda kv: -kv[1])]
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--out", required=True)
    r.add_argument("--plan", default="smoke")
    r.add_argument("--only", nargs="*", default=[])
    r.add_argument("--minutes", type=float, default=15.0)
    r.add_argument("--speed", type=float, default=4.0)
    r.add_argument("--windowed", action="store_true")
    r.add_argument("--screens", default="")
    r.add_argument("--timeout", type=float, default=3600.0)
    r.add_argument("--skip-done", action="store_true", help="skip runs whose json exists in --out")
    r.add_argument("--userdata", default="", help="the games' own user data folder (XDG_DATA_HOME); keeps each game's replay as <tag>.replay")
    r.add_argument("--replay-check", action="store_true", help="(with --userdata) play each game's replay back and compare the frame hashes")
    r.add_argument("--replay-speed", type=float, default=16.0)
    k = sub.add_parser("rank")
    k.add_argument("--out", required=True)
    k.add_argument("--markdown", default="")
    a = ap.parse_args(argv)
    out = Path(a.out)
    if a.cmd == "run":
        if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
            print("QA SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
            return 77
        out.mkdir(parents=True, exist_ok=True)
        godot = os.environ.get("GODOT", "godot")
        runs = plan_runs(a.plan, a.minutes, a.speed)
        if a.only:
            runs = [x for x in runs if any(o in x["tag"] for o in a.only)]
        bad = 0
        for run in runs:
            if a.skip_done and (out / f"{run['tag']}.json").exists():
                continue
            print(f"QA run {run['tag']}: {run['faction']} on {run['map']} vs {run['opponents']} x AI {run['ai']} ({'idle' if run.get('idle') else 'scripted'})", flush=True)
            for attempt in range(3):
                res = execute(run, out, godot, a.windowed, a.screens, a.timeout, a.userdata)
                # killed from outside (the machine's memory pressure: SIGKILL / SIGTERM) before the summary: retry later, never in parallel
                if res["summary"] or res["exit"] not in (-9, -15, 137, 143):
                    break
                print(f"  killed from outside (exit {res['exit']}); retrying in 180 s", flush=True)
                time.sleep(180)
            if a.replay_check and res.get("replay_file"):
                res["replay"] = check_replay(out / res["replay_file"], out, run["tag"], godot, a.userdata, a.replay_speed, a.timeout)
                (out / f"{run['tag']}.json").write_text(json.dumps(res, indent=1, sort_keys=True))
                print(f"  replay check: {res['replay']['result']} {json.dumps(res['replay']['status'])[:160]} in {res['replay']['seconds']} s", flush=True)
            s = res.get("summary", {})
            print(f"  exit {res['exit']} in {res['seconds']} s: {res['result_line'] or 'no result'}; frame {s.get('frame', '-')}, victory {s.get('victory')}, "
                  f"{len(findings(res))} findings", flush=True)
            bad += 0 if res["exit"] == 0 else 1
        return 1 if bad else 0
    results = [json.loads(p.read_text()) for p in sorted(out.glob("*.json"))]
    ranked = rank(results)
    md = markdown(results, ranked)
    if a.markdown:
        Path(a.markdown).write_text(md)
    else:
        print(md)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
