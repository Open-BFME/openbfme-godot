"""First-run UX gate. Run with GODOT=... python -m pytest launcher/tests/test_first_run.py.

Set LAUNCHER_UX_SCREENSHOTS to an output directory and DISPLAY to a virtual display
to capture the same walk. Without that option the gate runs entirely headlessly.
LAUNCHER_UX_BASELINE=1 records findings instead of accepting them, for before shots.
"""
from __future__ import annotations

import json
import os
import socket
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "release"))

from launcher_test_server import TEST_REPO, ReleaseServer
from launcher_test_support import asset_name, isolated_environment
from test_launcher import GODOT, _godot_ok, ed25519, release_files

ROOT = Path(__file__).resolve().parents[2]
PROJECT = Path(os.environ.get("LAUNCHER_UX_PROJECT", ROOT / "launcher"))

SCENARIOS = (
    "no-install", "found", "unsupported-version", "wrong-language", "no-network", "download-http", "download-unknown",
    "lobby-down", "update-down", "disk-full", "corrupt-download", "channels", "signin-unavailable",
)
pytestmark = pytest.mark.skipif(not _godot_ok(), reason="Godot 4.7.2 required (set GODOT)")


@pytest.fixture(scope="session", autouse=True)
def import_launcher(tmp_path_factory):
    env = isolated_environment(tmp_path_factory.mktemp("launcher-import"))
    result = subprocess.run([GODOT, "--headless", "--path", str(PROJECT), "--import"],
                            check=False, env=env, capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr


def test_import_environment(tmp_path, tmp_path_factory, monkeypatch):
    for name in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA", "USERPROFILE",
                 "BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "OPENBFME_GAME_USER_DIR"):
        monkeypatch.setenv(name, str(tmp_path / "inherited" / name))
    calls = []

    def run(command, **kwargs):
        calls.append((command, kwargs["env"]))
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr(subprocess, "run", run)
    import_launcher.__wrapped__(tmp_path_factory)
    assert len(calls) == 1 and "--import" in calls[0][0]
    env = calls[0][1]
    for name in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA"):
        path = Path(env[name])
        assert path.is_relative_to(tmp_path_factory.getbasetemp())
        assert "inherited" not in path.parts
    for name in ("BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "OPENBFME_GAME_USER_DIR"):
        assert name not in env


@pytest.mark.parametrize("scenario", SCENARIOS)
def test_first_run(tmp_path, scenario):
    server = ReleaseServer()
    key = os.urandom(32)
    out = os.environ.get("LAUNCHER_UX_SCREENSHOTS", "")
    env = isolated_environment(tmp_path, OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"),
                               HOME=str(tmp_path / "home"), USER="player", LOGNAME="player",
                               LAUNCHER_UX_SCENARIO=scenario, LAUNCHER_UX_SCREENSHOTS=out)
    # The live OS stays connected. A bound, non-listening local socket deterministically
    # refuses connections without relying on DNS or a public service going down.
    with socket.socket() as unavailable:
        unavailable.bind(("127.0.0.1", 0))
        dead = f"http://127.0.0.1:{unavailable.getsockname()[1]}"
        try:
            for version in ("v0.2.0", "v0.3.0-preview.1"):
                server.publish(version, release_files(tmp_path, version, key), notes="## What's new\n\n- Fixture build.")
            server.files["/v1/health"] = b'{"v":1,"ok":true,"games":0,"features":[]}'
            if scenario == "lobby-down":
                server.truncate["/v1/health"] = 0
            if scenario == "corrupt-download":
                name = asset_name("game", "v0.2.0")
                path = f"/dl/v0.2.0/{name}"
                data = server.files[path]
                server.files[path] = data[:-1] + bytes([data[-1] ^ 1])
            if scenario == "update-down":
                server.status["/dl/v0.2.0/manifest.json"] = 503
            if scenario in {"found", "unsupported-version", "wrong-language"}:
                for name in ("RotWK", "BFME2"):
                    folder = tmp_path / "home" / "Games" / name
                    folder.mkdir(parents=True)
                    (folder / "fixture.txt").write_text(json.dumps({"scenario": scenario, "synthetic": True}))
                    env["ROTWK_INSTALL" if name == "RotWK" else "BFME2_INSTALL"] = str(folder)
            lobby = server.origin
            options = [f"--api-base={server.origin}", f"--repo={TEST_REPO}",
                       f"--trust-key={ed25519.public_key(key).hex()}"]
            # Persist settings through the launcher's own CLI, then open the real UI.
            setup = subprocess.run([GODOT, "--headless", "--path", str(PROJECT), "--", *options,
                                    "--auto-update=off", "--online-count=off", f"--lobby={lobby}"],
                                   check=False, env=env, capture_output=True, text=True, timeout=90)
            assert setup.returncode == 0, setup.stdout + setup.stderr
            if scenario == "no-network":
                options[0] = f"--api-base={dead}"
            display = [] if out else ["--headless"]
            if out:
                Path(out).mkdir(parents=True, exist_ok=True)
            result = subprocess.run([GODOT, *display, "--audio-driver", "Dummy", "--resolution", "1280x720", "--path", str(PROJECT),
                                     "--script", "res://tests/first_run_test.gd", "--", *options],
                                    check=False, env=env, capture_output=True, text=True, timeout=600)
            log = result.stdout + result.stderr
            if out:
                (Path(out) / f"{scenario}.log").write_text(log)
            assert result.returncode == 0, log
            assert "SCRIPT ERROR" not in log and "Parse Error" not in log, log
            assert log.count("FIRST_RUN case ") == 6, log
            if out:
                for width, height in ((1280, 720), (2560, 1440)):
                    images = [(Path(out) / f"{scenario}-{width}x{height}-{scale}.png").read_bytes()
                              for scale in (100, 125, 150)]
                    assert all(struct.unpack(">II", data[16:24]) == (width, height) for data in images)
                    assert len(set(images)) == 3, "scale captures must differ"
            verdict = "BASELINE" if os.environ.get("LAUNCHER_UX_BASELINE") == "1" else "OK"
            assert f"FIRST_RUN {verdict} {scenario}: 6 cases" in log, log
            if scenario == "corrupt-download":
                assert any(p.endswith(asset_name("game", "v0.2.0")) for p, _ in server.requests)
            if scenario in {"signin-unavailable", "lobby-down"}:
                assert sum(p == "/v1/health" for p, _ in server.requests) == 1
                assert not any(p.startswith("/v1/auth") for p, _ in server.requests)
        finally:
            server.stop()
