"""Behavioural gate for tools/publish-durable-pack.ps1 -Verify.

The durable user:// content-pack cache is what an env-less launch (and the MP
lobby) resolves content from. It is a COPY of the workspace selection, so it
silently lags every republish: at the time this gate was written the durable
selection still named a single bfme2 pack while the workspace selection named
ten, and the MP lobby offered three factions instead of seven. Nothing failed.

-Verify makes that drift loud and cheap to check.
"""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "publish-durable-pack.ps1"


def _powershell(*args: str) -> subprocess.CompletedProcess[str]:
    executable = shutil.which("powershell") or shutil.which("pwsh")
    if executable is None:
        pytest.skip("no PowerShell host available")
    return subprocess.run(
        [
            executable,
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(SCRIPT),
            *args,
        ],
        capture_output=True,
        text=True,
        timeout=180,
    )


def _pack(root: Path, relative: str, marker: str) -> None:
    directory = root / relative.replace("/", "\\")
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "pack.json").write_text(
        json.dumps({"id": relative.split("/")[0], "marker": marker}),
        encoding="utf-8",
    )
    # A real bundle carries its content beside pack.json; pack.json holds no
    # digest of it, so content drift is only visible if the tree is hashed.
    data = directory / "data"
    data.mkdir(parents=True, exist_ok=True)
    (data / "objects.json").write_text(
        json.dumps({"objects": [{"id": marker + "-unit", "health": 100}]}),
        encoding="utf-8",
    )


def _selection(root: Path, active: str, supplemental: list[str]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "selection.json").write_text(
        json.dumps(
            {
                "schema": "openbfme.pack-selection",
                "schemaVersion": 0,
                "activePack": active,
                "supplementalPacks": supplemental,
            }
        ),
        encoding="utf-8",
    )


@pytest.fixture()
def workspace(tmp_path: Path) -> Path:
    root = tmp_path / "workspace"
    _selection(root, "pack-a/" + "a" * 64, ["pack-b/" + "b" * 64])
    _pack(root, "pack-a/" + "a" * 64, "a")
    _pack(root, "pack-b/" + "b" * 64, "b")
    return root


def test_verify_fails_when_the_durable_cache_has_never_been_published(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    assert result.returncode != 0
    assert "DRIFT" in result.stdout + result.stderr


def test_verify_fails_and_names_the_pack_the_durable_cache_lags_on(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    # Workspace republishes pack-b under a new bundle sha; the durable copy
    # still holds the old one. This is exactly the stale-MP-lobby failure.
    new_b = "pack-b/" + "c" * 64
    _pack(workspace, new_b, "c")
    _selection(workspace, "pack-a/" + "a" * 64, [new_b])
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0
    assert "DRIFT" in output
    assert new_b in output


def test_verify_passes_immediately_after_a_publish(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "DRIFT" not in result.stdout


def test_verify_detects_a_pack_whose_bytes_changed_under_the_same_path(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    # Tamper CONTENT, not the manifest. pack.json stays byte-identical, so an
    # identity-only check cannot see this -- yet the durable cache now serves
    # different gameplay data than the workspace it claims to mirror.
    tampered = durable / "pack-a" / ("a" * 64) / "data" / "objects.json"
    tampered.write_text(
        json.dumps({"objects": [{"id": "a-unit", "health": 999999}]}),
        encoding="utf-8",
    )
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert "DRIFT" in output
    assert "pack-a" in output


def test_verify_detects_a_truncated_file_in_the_durable_bundle(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    (durable / "pack-b" / ("b" * 64) / "data" / "objects.json").write_bytes(b"")
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert "DRIFT" in output


def test_verify_detects_supplemental_packs_listed_in_a_different_order(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    first = "pack-b/" + "b" * 64
    second = "pack-c/" + "c" * 64
    _pack(workspace, second, "c")
    _selection(workspace, "pack-a/" + "a" * 64, [first, second])
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    # Same set, different order. Load order decides which bundle's copy of a
    # shared document id wins, so a reordered durable selection is drift.
    durable_selection = json.loads(
        (durable / "selection.json").read_text(encoding="utf-8")
    )
    durable_selection["supplementalPacks"] = [second, first]
    (durable / "selection.json").write_text(
        json.dumps(durable_selection), encoding="utf-8"
    )
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert "DRIFT" in output
    assert "order" in output.lower()


@pytest.mark.parametrize(
    "mutate",
    [
        pytest.param(lambda doc: doc.update({"schema": "openbfme.something-else"}), id="wrong-schema"),
        pytest.param(lambda doc: doc.update({"schemaVersion": 7}), id="wrong-version"),
        pytest.param(lambda doc: doc.pop("schema"), id="missing-schema"),
        pytest.param(lambda doc: doc.update({"activePack": ""}), id="empty-active-pack"),
        pytest.param(lambda doc: doc.update({"supplementalPacks": "pack-b"}), id="supplementals-not-a-list"),
    ],
)
def test_verify_rejects_a_malformed_durable_selection_document(
    workspace: Path, tmp_path: Path, mutate
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    document = json.loads((durable / "selection.json").read_text(encoding="utf-8"))
    mutate(document)
    (durable / "selection.json").write_text(json.dumps(document), encoding="utf-8")
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert "DRIFT" in output
    assert "schema" in output.lower() or "selection" in output.lower()


def test_verify_rejects_a_durable_selection_that_is_not_valid_json(
    workspace: Path, tmp_path: Path
) -> None:
    durable = tmp_path / "durable"
    assert (
        _powershell(
            "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable)
        ).returncode
        == 0
    )
    (durable / "selection.json").write_text("{ not json", encoding="utf-8")
    result = _powershell(
        "-WorkspaceRoot", str(workspace), "-DurableRoot", str(durable), "-Verify"
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert "DRIFT" in output
