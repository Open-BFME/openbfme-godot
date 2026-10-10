"""Lane CAH-2 r2: game.gd --cah never writes the player's real Create-a-Hero folder unless --cah-real-profile asks for it, and removes what it wrote.

Runs the builder part (--auto --cah-builder-only) headless and checks the run's own lines: the isolated temporary save folder, the saved hero file equal
to the save path's record, the cleanup of that file and of the folder, and that the folder is gone. Needs Godot 4.7 (`godot` or GODOT), the built
extension (godot/bin) and the retail installs (ROTWK_INSTALL / BFME2_INSTALL); SKIPPED with the reason otherwise. About a minute.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
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


@pytest.mark.skipif(_skip_reason() is not None, reason=str(_skip_reason()))
def test_cah_run_uses_a_temporary_folder_and_removes_its_hero():
    p = subprocess.run([_godot(), "--headless", "--path", str(PROJECT), "--", "--auto", "--cah-builder-only"],
                       capture_output=True, text=True, errors="replace", timeout=900)
    out = p.stdout + p.stderr
    assert p.returncode == 0, out[-3000:]
    m = re.search(r"^CAH isolated save folder (.+?) \(--cah-real-profile", out, re.M)
    assert m, out[-3000:]
    folder = Path(m.group(1))
    assert re.search(r"^CAH file .*equal true; reloaded \(\d+ heroes\) equal true", out, re.M), out[-3000:]
    assert "CAH OK (builder only)" in out
    assert re.search(r"^CAH cleanup: removed .*MyHero_[0-9A-F]+\.cah: OK", out, re.M), out[-3000:]
    assert "(exists: false)" in out
    assert not folder.exists() and not folder.parent.exists()
