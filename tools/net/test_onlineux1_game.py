"""Lane ONLINEUX-1 r2 (Sol 2, Sol 3): an online game given up by a failed rejoin goes back to the menus through game.gd's own path with nothing of the
game left over it (the quit menu, its Options, the catch-up line), a second failed attempt in the same run is given up too, and the next game plays
(game.gd --auto --onlineux1-game, scripts/onlineux1_check.gd). Needs Godot with the openbfme extension (GODOT, else `godot`) and the retail installs
(ROTWK_INSTALL, BFME2_INSTALL): SKIP LOUDLY otherwise."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def _skip_reason() -> str | None:
    if not shutil.which(GODOT):
        return "Godot is not installed here (GODOT)"
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        return "ROTWK_INSTALL / BFME2_INSTALL are not set"
    if not list((REPO / "godot" / "bin").glob("openbfme.*")):
        return "the openbfme GDExtension is not built (godot/bin)"
    return None


def test_a_failed_rejoin_leaves_no_game_menu_behind_and_a_second_one_is_given_up_too():
    why = _skip_reason()
    if why:
        pytest.skip("SKIP LOUDLY: " + why + ", so the failed-rejoin teardown test did not run")
    home = tempfile.mkdtemp(prefix="openbfme-ux1-home-")
    try:
        env = {**os.environ, "HOME": home, "XDG_DATA_HOME": os.path.join(home, ".local/share"), "XDG_CONFIG_HOME": os.path.join(home, ".config")}
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--", "--auto", "--advance=2", "--onlineux1-game", "--movies=off"],
                           env=env, capture_output=True, text=True, timeout=900)
        out = r.stdout + r.stderr
        lines = [l for l in out.splitlines() if l.startswith("UX1 ")]
        assert r.returncode == 0, "\n".join(lines) or out[-4000:]
        assert lines and lines[-1] == "UX1 RESULT: 20 ok, 0 failed", "\n".join(lines) or out[-4000:]
        # both failures went through _online_game_failed (back to the screens, not the program's end)
        assert out.count("GAME ONLINE error: rejoin:") == 2, out[-4000:]
    finally:
        shutil.rmtree(home, ignore_errors=True)
