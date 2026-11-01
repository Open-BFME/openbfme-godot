"""Shared launcher/game display preferences and real launcher signal paths, without retail assets."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from launcher_test_support import godot_engine, isolated_environment

REPO = Path(__file__).resolve().parents[2]


def test_shared_display_settings(tmp_path):
    godot = godot_engine(os.environ.get("GODOT", "godot"))
    try:
        version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        pytest.skip("Godot 4.7.2 required (set GODOT)")
    if not version.stdout.startswith("4.7.2."):
        pytest.skip("Godot 4.7.2 required (set GODOT)")
    project = tmp_path / "launcher"
    shutil.copytree(REPO / "launcher", project, ignore=shutil.ignore_patterns(".godot"))
    shutil.copyfile(REPO / "godot/scripts/display_settings.gd", project / "test_game_display_settings.gd")
    env = isolated_environment(tmp_path / "home")
    env["OPENBFME_GAME_USER_DIR"] = str(tmp_path / "game")
    imported = subprocess.run([godot, "--headless", "--path", str(project), "--import"], env=env,
                              capture_output=True, text=True, timeout=60)
    assert imported.returncode == 0 and "SCRIPT ERROR" not in imported.stderr, imported.stdout + imported.stderr
    result = subprocess.run([godot, "--headless", "--path", str(project), "--script", "res://tests/display_test.gd",
                             "--", "--ui-demo=main"], env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and "DISPLAY TEST PASS" in result.stdout and "SCRIPT ERROR" not in result.stderr, result.stdout + result.stderr
