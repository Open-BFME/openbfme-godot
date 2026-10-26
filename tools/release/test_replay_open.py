"""Asset-free replay entrypoint/packaging checks; GODOT and the native build are required."""
from __future__ import annotations

import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT") or shutil.which("godot")
NATIVE = Path(os.environ.get("OPENBFME_TESTS", ROOT / "engine/build" / ("openbfme_tests.exe" if os.name == "nt" else "openbfme_tests")))


def run(*args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    result = subprocess.run(args, cwd=ROOT, env=env, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    assert "SCRIPT ERROR" not in result.stdout, result.stdout
    return result


def game(*args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    if not GODOT:
        pytest.skip("GODOT (4.7) is required")
    return run(GODOT, "--headless", "--path", str(ROOT / "godot"), *args, env=env)


def test_launcher_arguments_matching_refusals_and_association_commands():
    if not GODOT:
        pytest.skip("GODOT (4.7) is required")
    result = run(GODOT, "--headless", "--path", str(ROOT / "launcher"), "--script", "res://tests/replay_open_test.gd")
    assert result.returncode == 0 and "0 failures" in result.stdout, result.stdout


def test_game_argument_parser():
    result = game("--script", "res://tests/replay_args_test.gd")
    assert result.returncode == 0 and "0 failures" in result.stdout, result.stdout


@pytest.mark.skipif(sys.platform != "linux", reason="Linux XDG association integration")
def test_linux_replay_association(tmp_path: Path):
    if not GODOT:
        pytest.skip("GODOT (4.7) is required")
    for program in ("xdg-mime", "update-mime-database", "update-desktop-database"):
        if not shutil.which(program):
            pytest.skip(f"{program} is needed for a real XDG sandbox test")
    for folder in ("data", "config"):  # a real home has both; xdg-mime silently writes no mimeapps.list without the config folder
        (tmp_path / folder).mkdir(exist_ok=True)
    env = dict(os.environ, OPENBFME_TEST_ASSOCIATION_SANDBOX=str(tmp_path),
               XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"),
               XDG_CURRENT_DESKTOP="XFCE")
    result = run(GODOT, "--headless", "--path", str(ROOT / "launcher"),
                 "--script", "res://tests/replay_association_test.gd", env=env)
    assert result.returncode == 0 and "26 checks, 0 failures" in result.stdout, result.stdout


@pytest.mark.parametrize("kind,extension", [("replay", "replay"), ("save", "obfmesave")])
def test_macos_exported_document_type(kind: str, extension: str):
    preset = (ROOT / "launcher/export_presets.cfg").read_text()
    line = next(line for line in preset.splitlines() if line.startswith("application/additional_plist_content="))
    extra = json.loads(line.split("=", 1)[1])
    plist = plistlib.loads(("<plist version=\"1.0\"><dict>" + extra + "</dict></plist>").encode())
    index = 0 if kind == "replay" else 1
    doc = plist["CFBundleDocumentTypes"][index]
    assert doc["CFBundleTypeRole"] == "Viewer"
    assert doc["LSHandlerRank"] == "Alternate"
    assert doc["LSItemContentTypes"] == [f"org.openbfme.{kind}"]
    tag = plist["UTExportedTypeDeclarations"][index]["UTTypeTagSpecification"]
    assert tag["public.filename-extension"] == [extension]
    assert tag["public.mime-type"] == f"application/x-openbfme-{kind}"


def test_synthetic_native_save_routes_through_shared_launcher(tmp_path: Path):
    if not GODOT or not NATIVE.is_file():
        pytest.skip("GODOT and openbfme_tests are required")
    path = tmp_path / "synthetic λ save.obfmesave"
    env = dict(os.environ, OPENBFME_SAVE_FIXTURE=str(path))
    native = run(str(NATIVE), "--test-case=save route:*", env=env)
    assert native.returncode == 0 and path.is_file(), native.stdout
    result = run(GODOT, "--headless", "--path", str(ROOT / "launcher"),
                 "--script", "res://tests/replay_open_test.gd", env=env)
    assert result.returncode == 0 and "123 checks, 0 failures" in result.stdout, result.stdout


@pytest.mark.parametrize("equals", [False, True])
def test_synthetic_save_game_entrypoints_refuse_by_build(tmp_path: Path, equals: bool):
    if not NATIVE.is_file():
        pytest.skip("build openbfme_tests first")
    path = tmp_path / "synthetic λ save.obfmesave"
    env = dict(os.environ, OPENBFME_SAVE_FIXTURE=str(path))
    native = run(str(NATIVE), "--test-case=save route:*", env=env)
    assert native.returncode == 0 and path.is_file(), native.stdout
    args = [f"--load={path}"] if equals else ["--load", str(path)]
    result = game("--script", "res://tests/save_route_test.gd", "--", *args, env=env)
    assert result.returncode == 0 and "8 checks, 0 failures" in result.stdout, result.stdout


@pytest.mark.parametrize("separator", [[], ["--"]])
def test_game_save_startup_mounts_before_loading(tmp_path: Path, separator: list[str]):
    env = dict(os.environ, ROTWK_INSTALL=str(tmp_path / "missing-rotwk"), BFME2_INSTALL=str(tmp_path / "missing-bfme2"))
    result = game(*separator, "--load", str(tmp_path / "save λ.obfmesave"), env=env)
    assert result.returncode != 0, result.stdout
    assert "GAME save requested; mounting retail files before loading" in result.stdout, result.stdout
    assert "mount failed:" in result.stdout, result.stdout


def test_existing_save_flow_preserves_running_game():
    result = game("--script", "res://tests/save4e_flow_test.gd")
    assert result.returncode == 0 and "FLOW PASS" in result.stdout, result.stdout


@pytest.mark.parametrize("separator", [[], ["--"]])
def test_malformed_game_replay_refused_before_mounting(tmp_path: Path, separator: list[str]):
    path = tmp_path / "truncated λ replay.replay"
    path.write_bytes(b"OBFMEREP\x08\0\0\0")
    result = game(*separator, "--replay", str(path))
    assert result.returncode == 1, result.stdout
    assert "Replay refused:" in result.stdout, result.stdout
    assert "mounting retail files before playback" not in result.stdout, result.stdout


def test_synthetic_native_recording_reaches_game_mount_and_playback_driver(tmp_path: Path):
    if not NATIVE.is_file():
        pytest.skip("build openbfme_tests first")
    path = tmp_path / "synthetic λ match.replay"
    env = os.environ.copy()
    env["OPENBFME_REPLAY_FIXTURE"] = str(path)
    native = run(str(NATIVE), "--test-case=replay open:*", env=env)
    assert native.returncode == 0, native.stdout
    assert path.is_file(), native.stdout
    # Keep this test asset-free even if the developer's real install is configured.
    for key in ["ROTWK_INSTALL", "BFME2_INSTALL", "BFME1_INSTALL"]:
        env[key] = str(tmp_path / "not-installed")
    env["XDG_DATA_HOME"] = str(tmp_path / "data")
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    result = game("--", "--replay", str(path), env=env)
    assert result.returncode == 1, result.stdout
    assert "GAME replay input validated; mounting retail files before playback" in result.stdout, result.stdout
    assert "mount failed" in result.stdout, result.stdout
    assert "Replay refused:" not in result.stdout, result.stdout
