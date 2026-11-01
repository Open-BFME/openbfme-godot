# DEVPORT-5: the caster / replay mode's Godot tests (CASTER-1, godot/tests/caster_*.gd) run in the gate's pytest step, against the retail files.
# Each check fails closed: a non-zero exit, a missing result line, any FAIL line or a missing named check fails the test; only an unset
# ROTWK_INSTALL / BFME2_INSTALL or a missing Godot binary skips (loudly).
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def _require_env():
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) for the caster tests")
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        pytest.skip("SKIP LOUDLY: ROTWK_INSTALL / BFME2_INSTALL are not set (the caster tests need the retail files)")


def _godot(home: Path, *args: str, timeout: int = 1200) -> tuple[int, str]:
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / "data"), XDG_CONFIG_HOME=str(home / "config"))
    r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), *args], capture_output=True, text=True, timeout=timeout, env=env)
    return r.returncode, r.stdout + r.stderr


def _script(home: Path, script: str, prefix: str, result: str, *user_args: str) -> str:
    code, out = _godot(home, "--script", f"res://tests/{script}", *(("--", *user_args) if user_args else ()))
    assert code == 0, f"{script} exited {code}\n{out[-6000:]}"
    assert f"{result} RESULT failures=0" in out, f"{script}: no '{result} RESULT failures=0' line\n{out[-6000:]}"
    fails = [line for line in out.splitlines() if line.startswith(f"{prefix} FAIL")]
    assert not fails, "\n".join(fails)
    return out


def test_vision_pov_transport_and_restart_seek(tmp_path):
    """caster_vision_test.gd: POV hotkeys, all / team vision, active-player refusal, 50 CRCs, restart + seek, exact step."""
    _require_env()
    out = _script(tmp_path, "caster_vision_test.gd", "CASTER", "CASTER")
    for name in ("active player cannot reveal map", "camera-track absence is visible", "frame 50 matches straight recording",
                 "backward seek requires a restart, not a fake snapshot", "seek hash equals uninterrupted recording",
                 "step hash equals uninterrupted recording"):
        assert f"CASTER PASS {name}" in out, f"missing check: {name}\n{out[-4000:]}"


def test_director_picks_the_recorded_fight_and_overlays_match_the_engine(tmp_path):
    """caster_director_test.gd --record-fight records a real hero encounter; caster_overlay_test.gd replays it and compares the statistics."""
    _require_env()
    out = _script(tmp_path, "caster_director_test.gd", "DIRECTOR", "DIRECTOR", "--record-fight", "--fixture-name=caster-overlay", "--until-death")
    assert "DIRECTOR PASS director selects the scripted fight in the recorded replay" in out, out[-4000:]
    assert "DIRECTOR PASS every replay hash matches with director active" in out, out[-4000:]
    _script(tmp_path, "caster_overlay_test.gd", "OVERLAY", "OVERLAY")


def test_a_replay_end_is_not_the_recorders_end_screen_and_is_reported(tmp_path):
    """Stop S-4932: the recorder's end-game requests reach a replay; the caster drops them by name, the replay still checks every CRC."""
    _require_env()
    out = _script(tmp_path, "caster_end_fixture.gd", "CASTER END", "CASTER END")
    m = re.search(r"^CASTER END fixture replay: (.+)$", out, re.M)
    assert m, out[-4000:]
    replay = m.group(1).strip()  # the game's logs print the home directory as ~ (or, through the session-log privacy filter, <user>)
    for home in ("~/", "<user>/"):
        if replay.startswith(home):
            replay = str(tmp_path) + "/" + replay[len(home):]
    assert Path(replay).is_file(), replay
    code, game = _godot(tmp_path, "--", f"--replay={replay}", "--replay-check", "--no-record")
    assert code == 0, f"game.gd --replay-check exited {code}\n{game[-6000:]}"
    assert "GAME REPLAY RESULT: ok" in game, game[-6000:]
    shown = [line for line in game.splitlines() if line.startswith("GAME END request not shown to the caster [S-4932]: ")]
    assert any('"kind":"show_end_game"' in line for line in shown), game[-6000:]
    assert "GAME END request: " not in game, "an end-game request reached the end sequence in a replay"
