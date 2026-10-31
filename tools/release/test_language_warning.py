"""PR #77 (owner decision "load with warning"): the game's warning box for an install in a language OpenBFME has not checked
(godot/scripts/release/language_warning.gd), run by godot/tests/language_warning_test.gd with a simulated mount whose language and file
names are a hostile % sequence. The engine side (the texts, the mount, the strings of such an archive) is engine/tests/test_any_language.cpp."""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def test_the_language_warning_box_copies_logs_and_remembers_per_install(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the warning box")
    env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
    settings = tmp_path / "language-warning.cfg"
    r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/language_warning_test.gd", "--",
                        f"--settings={settings}"], capture_output=True, text=True, timeout=300, env=env)
    out = r.stdout + r.stderr
    assert "LANGUAGE WARNING ALL PASS" in out, out[-4000:]
    assert "LANGUAGE WARNING FAIL" not in out, out[-4000:]
    assert r.returncode == 0, out[-4000:]
    assert settings.is_file()
