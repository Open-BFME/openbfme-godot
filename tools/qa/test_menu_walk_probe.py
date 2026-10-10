"""Lane CAH-2 r2: game.gd --menu-walk must fail when a menu button soft-locks the main menu.

The probe presses one button (--menu-walk-only=SoloPlayNav.LoadGame) twice, headless: as shipped the host answers the unported Load Game with the
"not available in this build yet" box and the walk passes (exit 0); with the test hook --menu-walk-drop=LoadGame the host ignores the request, the menu
stays in DisableAllButtons and the walk must print FAIL and exit non-zero (Sol r1 found the walk printing PASS after such a drop).

Needs Godot 4.7 (`godot` on the PATH or GODOT), the built extension (godot/bin) and the retail installs (ROTWK_INSTALL / BFME2_INSTALL); without them the
test is SKIPPED with the reason. About a minute per run.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
PROJECT = REPO / "godot"


def _godot() -> str | None:
    return os.environ.get("GODOT") or shutil.which("godot")


def _skip_reason() -> str | None:
    if not _godot():
        return "no godot on the PATH (set GODOT)"
    if not any((PROJECT / "bin").glob("openbfme.*")):
        return "the openbfme extension is not built (godot/bin)"
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        return "ROTWK_INSTALL / BFME2_INSTALL unset"
    return None


def _walk(*extra: str) -> tuple[int, str]:
    with tempfile.TemporaryDirectory(prefix="openbfme-walk-probe-") as shots:
        args = [_godot(), "--headless", "--path", str(PROJECT), "--", f"--menu-walk={shots}", "--menu-walk-only=SoloPlayNav.LoadGame", *extra]
        p = subprocess.run(args, capture_output=True, text=True, errors="replace", timeout=900)
    return p.returncode, p.stdout + p.stderr


@pytest.mark.skipif(_skip_reason() is not None, reason=str(_skip_reason()))
def test_menu_walk_passes_the_unavailable_box_and_fails_a_dropped_request():
    code, out = _walk()
    assert "MENUWALK PASS (0 failures)" in out, out[-3000:]
    assert code == 0
    code, out = _walk("--menu-walk-drop=LoadGame")
    assert "GAME TEST HOOK: --menu-walk-drop drops the request LoadGame" in out
    assert "MENUWALK FAIL" in out and "soft-lock" in out, out[-3000:]
    assert "MENUWALK PASS" not in out
    assert code != 0
