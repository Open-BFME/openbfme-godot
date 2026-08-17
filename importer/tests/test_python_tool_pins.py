from __future__ import annotations

from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys
from urllib.parse import unquote, urlparse

import pytest

from openbfme_importer import pipeline as pipeline_module
from openbfme_importer.bootstrap import (
    PYTHON_ARCHIVE_SHA256,
    PYTHON_BUILD_TAG,
    PYTHON_EXE_SHA256,
    PYTHON_URL,
    PYTHON_VERSION,
    python_runtime_attestation,
)
from openbfme_importer.big import sha256_file
from openbfme_importer.pipeline import ImportPipeline


def test_pinned_python_artifact_contract_is_well_formed() -> None:
    sha256 = re.compile(r"[0-9a-f]{64}")
    assert sha256.fullmatch(PYTHON_ARCHIVE_SHA256)
    assert sha256.fullmatch(PYTHON_EXE_SHA256)

    parsed = urlparse(PYTHON_URL)
    assert parsed.scheme == "https"
    assert parsed.netloc == "github.com"
    assert PYTHON_VERSION in parsed.path
    assert PYTHON_BUILD_TAG in parsed.path

    asset_name = unquote(PurePosixPath(parsed.path).name)
    asset = re.fullmatch(
        r"cpython-(?P<version>\d+\.\d+\.\d+)\+(?P<build>\d+)"
        r"-x86_64-pc-windows-msvc-install_only\.tar\.gz",
        asset_name,
    )
    assert asset is not None
    assert asset.group("version") == PYTHON_VERSION
    assert asset.group("build") == PYTHON_BUILD_TAG


def test_runtime_attestation_pins_executables_without_host_paths() -> None:
    base_executable = Path(sys._base_executable).resolve(strict=True)
    venv_launcher = Path(sys.executable).resolve(strict=True)
    if base_executable == venv_launcher:
        pytest.skip("focused regression requires the pinned importer venv")

    receipt = python_runtime_attestation()

    assert receipt["launcher_sha256"] == sha256_file(base_executable)
    assert receipt["venv_launcher_sha256"] == sha256_file(venv_launcher)
    assert "base_executable" not in receipt
    assert "venv_launcher" not in receipt

    def assert_portable(value: object) -> None:
        if isinstance(value, dict):
            for child in value.values():
                assert_portable(child)
        elif isinstance(value, list):
            for child in value:
                assert_portable(child)
        elif isinstance(value, str):
            assert not PureWindowsPath(value).is_absolute()
            assert not PurePosixPath(value).is_absolute()
            components = {
                component.casefold()
                for component in value.replace("\\", "/").split("/")
                if component
            }
            assert "workspace" not in components
            assert "retail-work" not in components

    assert_portable(receipt)
    for private_path in (
        "workspace/retail-work/tools/python.exe",
        r"workspace\retail-work\tools\python.exe",
        "retail-work/tools/python.exe",
    ):
        with pytest.raises(AssertionError):
            assert_portable(private_path)


def test_pipeline_provenance_projects_runtime_attestation_unchanged(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    receipt = python_runtime_attestation()
    pipeline = object.__new__(ImportPipeline)
    pipeline._python_runtime_report = receipt
    pipeline._w3d_final_attestation = None
    pipeline.state_root = tmp_path / "missing-state-root"
    monkeypatch.setattr(pipeline_module, "discover_executable", lambda *_args: None)

    projected = pipeline._canonical_tool_report()["python"]

    assert projected == receipt
    assert projected is not receipt
