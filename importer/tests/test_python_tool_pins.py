from __future__ import annotations

from pathlib import Path, PurePosixPath
import re
import sys
from urllib.parse import unquote, urlparse

import pytest

from openbfme_importer.bootstrap import (
    PYTHON_ARCHIVE_SHA256,
    PYTHON_BUILD_TAG,
    PYTHON_EXE_SHA256,
    PYTHON_URL,
    PYTHON_VERSION,
    python_runtime_attestation,
)
from openbfme_importer.big import sha256_file


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


def test_runtime_attestation_pins_base_executable_and_records_venv_launcher() -> None:
    base_executable = Path(sys._base_executable).resolve(strict=True)
    venv_launcher = Path(sys.executable).resolve(strict=True)
    if base_executable == venv_launcher:
        pytest.skip("focused regression requires the pinned importer venv")

    receipt = python_runtime_attestation()

    assert receipt["launcher_sha256"] == sha256_file(base_executable)
    assert receipt["base_executable"] == str(base_executable)
    assert receipt["venv_launcher_sha256"] == sha256_file(venv_launcher)
    assert receipt["venv_launcher"] == str(venv_launcher)
