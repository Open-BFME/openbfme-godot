"""All external opens use the record-or-open boundary, including native website requests."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import pytest

REPO = Path(__file__).resolve().parents[2]
GAME = REPO / "godot/scripts/external_open.gd"
LAUNCHER = REPO / "launcher/scripts/core/external_open.gd"
GODOT = shutil.which(os.environ.get("GODOT", "godot"))


def test_projects_ship_the_same_external_open_boundary():
    assert GAME.read_bytes() == LAUNCHER.read_bytes()
    for root, owner in ((REPO / "godot/scripts", GAME), (REPO / "launcher/scripts", LAUNCHER)):
        for script in root.rglob("*.gd"):
            if script != owner:
                assert not re.search(r"OS\s*\.\s*shell_(?:open|show_in_file_manager)", script.read_text()), script
    for function, method in (("open_url", "shell_open"), ("show_in_file_manager", "shell_show_in_file_manager")):
        body = GAME.read_text().split(f"static func {function}(")[1]
        body = body.split("\nstatic func ")[0]
        assert body.index("if testing():") < body.index("requests.append(") < body.index("return OK") < body.index(f"OS.{method}(")


def test_native_website_bridge_delegates_and_fails_closed():
    native = (REPO / "engine/src/GodotDevice/GodotAptPlayer.cpp").read_text()
    body = native.split("if (r.action == ShellAction::OpenProjectWebsite)")[1].split("return;")[0]
    assert 'load("res://scripts/external_open.gd")' in body
    assert 'opener->call("open_url", "https://openbfme.com")' in body
    assert "ERR_UNAVAILABLE" in body
    assert "shell_open(" not in native
    test = (REPO / "godot/tests/online_branding_test.gd").read_text()
    assert 'ExternalOpen.requests == [{"kind": "url", "target": "https://openbfme.com"}]' in test


@pytest.mark.skipif(os.name == "nt", reason="the PATH canary instruments POSIX external opener executables")
def test_suite_guard_rejects_a_bypassed_opener(tmp_path):
    shutil.copy(REPO / "tools/conftest.py", tmp_path / "conftest.py")
    (tmp_path / "test_bypass.py").write_text('import subprocess\ndef test_bypass():\n    subprocess.run(["xdg-open", "about:blank"])\n')
    # The nested session must not touch the shared /tmp/pytest-of-<user> root: every pytest start there deletes the garbage-*
    # trees earlier runs left (gigabytes on a gate host), which took over a minute under load. A private basetemp skips that.
    # No inherited xdist worker identity, no plugin autoload (the guard conftest needs only pytest), no cache dir, no stdin.
    env = {k: v for k, v in os.environ.items() if not k.startswith(("PYTEST_XDIST", "PYTEST_CURRENT_TEST"))}
    env["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    result = subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", f"--basetemp={tmp_path / 'nested-tmp'}",
                             str(tmp_path / "test_bypass.py")], cwd=tmp_path, env=env, stdin=subprocess.DEVNULL,
                            capture_output=True, text=True,
                            timeout=60)  # tolerance: second guard only; a real opener would block here, the cleanup stall is fixed above
    assert result.returncode == 1 and "a test subprocess attempted a real external open" in result.stdout, result.stdout + result.stderr


@pytest.mark.skipif(not GODOT, reason="Godot required for opener canaries")
@pytest.mark.parametrize("project", ["godot", "launcher"])
@pytest.mark.parametrize("headless", [True, False])
def test_no_real_opener_is_called(tmp_path, project, headless):
    if not headless and sys.platform.startswith("linux") and not os.environ.get("DISPLAY"):
        pytest.skip("windowed opener canary requires a display")
    home = tmp_path / "home"
    home.mkdir()
    canaries = tmp_path / "bin"
    canaries.mkdir()
    marker = tmp_path / "real-opener-called"
    if os.name != "nt":
        for name in ("xdg-open", "gio", "open", "explorer", "rundll32"):
            script = canaries / name
            script.write_text('#!/bin/sh\nprintf attempted >> "$OPENER_CANARY"\nexit 99\n')
            script.chmod(0o755)
    env = {**os.environ, "HOME": str(home), "XDG_DATA_HOME": str(home / "data"),
           "XAUTHORITY": os.environ.get("XAUTHORITY", str(Path.home() / ".Xauthority")),
           "OPENBFME_TEST": "0", "OPENER_CANARY": str(marker), "PATH": str(canaries) + os.pathsep + os.environ["PATH"]}
    display = ["--headless"] if headless else ["--rendering-method", "gl_compatibility"]
    result = subprocess.run([GODOT, *display, "--path", str(REPO / project), "--script", "res://tests/external_open_test.gd"],
                            env=env, capture_output=True, text=True, timeout=60)
    assert not marker.exists(), "a test attempted to run the real external opener"
    assert result.returncode == 0 and "EXTERNAL OPEN PASS (2 recorded requests)" in result.stdout, result.stdout + result.stderr
    assert "SCRIPT ERROR" not in result.stderr and "Parse Error" not in result.stderr


@pytest.mark.skipif(not GODOT, reason="Godot required for opts6 wait unit tests")
def test_opts6_condition_waits():
    result = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/opts6_wait_test.gd"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and "WAIT RESULT: 15 checks, 0 failed" in result.stdout, result.stdout + result.stderr
    assert "SCRIPT ERROR" not in result.stderr and "Parse Error" not in result.stderr
