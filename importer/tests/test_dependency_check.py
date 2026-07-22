"""Tests for unified dependency preflight."""

from __future__ import annotations

from pathlib import Path
import sys
from unittest import mock

from openbfme_importer.dependency_check import (
    blocking_message,
    check_dependencies,
    format_dependency_report,
)


def test_check_dependencies_missing_install(tmp_path: Path) -> None:
    report = check_dependencies(
        tmp_path / "no-install",
        tmp_path / "state",
        mode="faction-convert",
        deep=False,
    )
    assert report["ready"] is False
    assert report["error_count"] >= 1
    assert blocking_message(report)
    text = format_dependency_report(report)
    assert "BLOCKED" in text or "ERR" in text


def test_check_dependencies_soft_w3d_for_convert_mode(tmp_path: Path) -> None:
    """Plan/convert mode should not hard-require Blender when missing."""

    install = tmp_path / "install"
    install.mkdir()
    (install / "lotrbfme2.exe").write_bytes(b"x")
    (install / "game.dat").write_bytes(b"x")
    for name in ("asset.big", "base.big", "INI.big"):
        (install / name).write_bytes(b"x" * 16)
    (install / "patch.doc").write_text("1.06", encoding="latin-1")

    fake_doctor = {
        "ready": True,
        "install_root": str(install),
        "declared_patch": "1.06",
        "missing_required": [],
        "executable_present": True,
        "executable_attestation": {},
    }
    with mock.patch(
        "openbfme_importer.dependency_check.doctor_install",
        return_value=fake_doctor,
    ):
        report = check_dependencies(
            install,
            tmp_path / "state",
            mode="faction-convert",
            deep=False,
        )
    blender = next(i for i in report["items"] if i["id"] == "blender")
    # Missing blender is warn for convert, not required error.
    assert blender["required"] is False
    assert blender["severity"] in {"warn", "ok"}


def test_check_dependencies_men_build_requires_blender(tmp_path: Path) -> None:
    install = tmp_path / "install"
    install.mkdir()
    fake_doctor = {
        "ready": True,
        "install_root": str(install),
        "declared_patch": "1.06",
        "missing_required": [],
        "executable_present": True,
        "executable_attestation": {},
    }
    with mock.patch(
        "openbfme_importer.dependency_check.doctor_install",
        return_value=fake_doctor,
    ):
        report = check_dependencies(
            install,
            tmp_path / "state",
            mode="men-build",
            deep=False,
        )
    blender = next(i for i in report["items"] if i["id"] == "blender")
    assert blender["required"] is True
    if not blender["ok"]:
        assert report["ready"] is False
        assert any(e["id"] == "blender" for e in report["errors"])


def test_python_version_item_present(tmp_path: Path) -> None:
    with mock.patch(
        "openbfme_importer.dependency_check.doctor_install",
        return_value={
            "ready": False,
            "install_root": str(tmp_path),
            "declared_patch": "unknown",
            "missing_required": ["asset.big"],
            "executable_attestation": {},
        },
    ):
        report = check_dependencies(tmp_path, tmp_path / "s", mode="faction-plan")
    py = next(i for i in report["items"] if i["id"] == "python")
    assert py["found"] == sys.version.split()[0]
