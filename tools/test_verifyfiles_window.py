"""Asset-free launcher verification dialog, with an isolated player profile."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess

import pytest


def test_verifyfiles_window(tmp_path: Path) -> None:
    godot = os.environ.get("GODOT", "godot")
    try:
        version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        pytest.skip("SKIP verify-files dialog: Godot 4.7.2 is required (set GODOT)")
    if version.returncode or not version.stdout.startswith("4.7.2."):
        pytest.skip("SKIP verify-files dialog: Godot 4.7.2 is required (set GODOT)")
    env = dict(os.environ)
    for name in ("BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "OPENBFME_GAME_USER_DIR", "OPENBFME_TEST_LAUNCHER_OPTIONS"):
        env.pop(name, None)
    for name in ("HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        folder = tmp_path / name.lower()
        folder.mkdir()
        env[name] = str(folder)
    result = subprocess.run(
        [godot, "--headless", "--path", str(Path(__file__).resolve().parents[1] / "launcher"),
         "--script", "res://tests/verifyfiles_window_test.gd", "--", "--offline"],
        env=env, capture_output=True, text=True, timeout=90,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "SCRIPT ERROR" not in output and "Parse Error" not in output, output
    assert "VERIFYFILES WINDOW OK" in output, output
    print(next(line for line in output.splitlines() if line.startswith("VERIFYFILES WINDOW OK")))
