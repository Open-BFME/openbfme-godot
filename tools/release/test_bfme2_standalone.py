"""BFME2-only selection uses pinned packages without changing the RotWK default."""
import os
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]

@pytest.fixture(scope="module", autouse=True)
def import_launcher():
    godot = shutil.which(os.environ.get("GODOT", "godot"))
    if not godot:
        pytest.skip("Godot is required for the launcher checks")
    result = subprocess.run([godot, "--headless", "--editor", "--path", str(ROOT / "launcher"),
                             "--import", "--quit"], capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr


def test_bfme2_only_offer_and_launch(tmp_path):
    godot = shutil.which(os.environ.get("GODOT", "godot"))
    if not godot:
        pytest.skip("Godot is required for the asset-free launcher check")
    env = dict(os.environ, OPENBFME_TEST_ROOT=str(tmp_path))
    result = subprocess.run([godot, "--headless", "--path", str(ROOT / "launcher"),
                             "--script", "res://tests/bfme2_offer_test.gd"],
                            env=env, capture_output=True, text=True, timeout=60)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "SCRIPT ERROR:" not in output and "ERROR:" not in output, output
    assert "BFME2 OFFER PASS" in output


def test_bfme2_only_download_never_requests_rotwk(tmp_path):
    from tools.release.test_aio import Service, ReleaseServer, manifest_path
    godot = shutil.which(os.environ.get("GODOT", "godot"))
    if not godot:
        pytest.skip("Godot is required for the synthetic installer check")
    server = ReleaseServer()
    try:
        service = Service(server, tmp_path)
        env = dict(os.environ, OPENBFME_TEST_ROOT=str(tmp_path),
                   OPENBFME_AIO_ORIGIN=server.origin, OPENBFME_AIO_PINS=str(service.pins_file))
        result = subprocess.run([godot, "--headless", "--path", str(ROOT / "launcher"),
                                 "--script", "res://tests/bfme2_offer_test.gd"],
                                env=env, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert "ERROR:" not in output and "BFME2 OFFER PASS" in output, output
        requested = [path for path, _headers in server.requests]
        assert manifest_path("original-BFME2") in requested
        assert manifest_path("original-RotWK") not in requested
        assert not (tmp_path / "games" / "RotWK").exists()
        package = next(p for p in service.pkgs if p["key"] == "bfme2")
        for entry in package["files"]:
            path = tmp_path / "games" / "BFME2" / entry["name"].replace("\\", "/")
            expected = entry["language"] == "ALL" or "EN" in entry["language"].split()
            assert path.exists() == expected
            if expected:
                assert path.read_bytes() == entry["data"]
        marker = (tmp_path / "game-user" / "downloaded-installs.cfg").read_text()
        assert "BFME2_INSTALL=" in marker and "ROTWK_INSTALL=" not in marker
        assert "original-RotWK" not in marker
    finally:
        server.stop()
