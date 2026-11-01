"""VERIFYFILES-1 backend tests against the local AIO stand-in service."""
from __future__ import annotations

from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
RELEASE = HERE / "release"
sys.path.insert(0, str(RELEASE))
from launcher_test_server import ReleaseServer  # noqa: E402
from test_aio import Service, file_path  # noqa: E402


GODOT = os.environ.get("GODOT", "godot")


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, timeout=30).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


pytestmark = pytest.mark.skipif(not _godot_ok(), reason="SKIP verify-files backend: Godot 4.7.2 not found (set GODOT)")


def _package() -> list[dict]:
    files = [
        {"name": "lang\\English.big", "data": b"english archive", "language": "EN", "pinned": True, "size": 15},
        {"name": "missing.txt", "data": b"missing fixture", "language": "ALL", "pinned": True, "size": 15},
        {"name": "changed.txt", "data": b"changed fixture!", "language": "ALL", "pinned": True, "size": 16},
    ]
    return [{"key": "rotwk", "guid": "fixture-RotWK", "name": "Fixture", "game": 2, "folder": "RotWK", "files": files}]


def _run(tmp_path: Path, service: Service, *extra: str) -> tuple[int, str]:
    env = dict(os.environ)
    for name in ("BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "OPENBFME_GAME_USER_DIR", "OPENBFME_TEST_LAUNCHER_OPTIONS"):
        env.pop(name, None)
    for name in ("HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        folder = tmp_path / name.lower()
        folder.mkdir(exist_ok=True)
        env[name] = str(folder)
    result = subprocess.run(
        [GODOT, "--headless", "--path", str(REPO / "launcher"), "--script", "res://tests/verifyfiles_test.gd", "--",
         f"--origin={service.server.origin}", f"--pins={service.pins_file}", *extra],
        env=env, capture_output=True, text=True, timeout=120,
    )
    output = result.stdout + result.stderr
    assert "SCRIPT ERROR" not in output and "Parse Error" not in output, output
    assert "VERIFYFILES OK" in output, output
    for line in output.splitlines():
        if line.startswith(("VERIFYFILES OK", "VERIFYFILES symlink checks")):
            print(line)
    return result.returncode, output


def test_windows_replacement_contract() -> None:
    source = (REPO / "launcher/scripts/core/game_files.gd").read_text(encoding="utf-8")
    replace_body = source.split("func _replace(", 1)[1].split("\n\nfunc repair(", 1)[0]
    assert re.search(
        r'if OS\.get_name\(\) == "Windows":\s+if not SelfUpdate\._replace_atomically\(stages\[index\], plan\.target\):'
        r'\s+replace_error = L10n\.t\("launcher\.game_files\.helper_failed"\)\s+else:\s+var err := '
        r'DirAccess\.rename_absolute\(stages\[index\], plan\.target\)\s+if err != OK:\s+replace_error = error_string\(err\)',
        replace_body,
    )
    repair_body = source.split("func repair(", 1)[1]
    assert repair_body.index("_windows_repair_paths_safe") < repair_body.index("_repair_downloads")
    assert '"launcher.game_files.windows_shell_chars"' in repair_body
    catalog = json.loads((REPO / "tools/l10n/catalog/en.json").read_text(encoding="utf-8"))
    assert catalog["launcher.game_files.windows_shell_chars"] == (
        "Windows repair refused a root or target path containing shell metacharacters.")
    assert catalog["launcher.game_files.helper_failed"] == "the Windows atomic replacement helper failed"


def test_verifyfiles_backend(tmp_path: Path) -> None:
    server = ReleaseServer()
    try:
        service = Service(server, tmp_path, _package())
        root_files = service.pkgs[0]["files"]
        missing_data = next(f["data"] for f in root_files if f["name"] == "missing.txt")
        changed_data = next(f["data"] for f in root_files if f["name"] == "changed.txt")
        code, output = _run(tmp_path, service)
        assert code == 0, output
        manifests = [path for path, _headers in service.server.requests if path.startswith("/api/workshop/download")]
        assert len(manifests) == 1, manifests
        requested = Counter(path for path, _headers in service.file_requests())
        assert requested == Counter({file_path(missing_data): 1, file_path(changed_data): 1})
    finally:
        server.stop()


def test_verifyfiles_no_consent_does_not_request_files(tmp_path: Path) -> None:
    server = ReleaseServer()
    try:
        service = Service(server, tmp_path, _package())
        code, output = _run(tmp_path, service, "--no-consent-only")
        assert code == 0, output
        assert service.server.requests == []
    finally:
        server.stop()


def test_verifyfiles_corrupt_download_preserves_originals(tmp_path: Path) -> None:
    server = ReleaseServer()
    try:
        service = Service(server, tmp_path, _package())
        changed = next(f for f in service.pkgs[0]["files"] if f["name"] == "changed.txt")
        server.files[file_path(changed["data"])] = b"x" * len(changed["data"])
        code, output = _run(tmp_path, service, "--corrupt")
        assert code == 0, output
    finally:
        server.stop()
