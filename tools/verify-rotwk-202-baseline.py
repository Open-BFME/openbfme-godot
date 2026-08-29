#!/usr/bin/env python3
"""Read-only verification for the pinned RotWK 2.02 v9.7.7 source baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import (  # noqa: E402
    InstallCatalog,
    default_rotwk_202_archive_policy,
)


DEFAULT_BASELINE = ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"
DEFAULT_LAYERED_ROOT = (
    ROOT / "workspace" / "retail-work" / "editions" / "rotwk" / "layered-install"
)


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def _package_path(layer: Path, value: str) -> Path:
    normalized = value.replace("\\", "/")
    relative = PurePosixPath(normalized)
    if (
        not normalized
        or normalized.startswith(("/", "~"))
        or relative.is_absolute()
        or any(part in {"", ".", ".."} or ":" in part for part in relative.parts)
    ):
        raise ValueError(f"unsafe required package path: {value!r}")
    return layer.joinpath(*relative.parts)


def _digest(path: Path, algorithm: str) -> str:
    if algorithm == "md5":
        digest = hashlib.md5(usedforsecurity=False)
    else:
        digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(root: Path, baseline_path: Path) -> tuple[int, int, str, str]:
    baseline = _load_json(baseline_path)
    if baseline.get("baselineId") != "rotwk-202-v9.7.7-en":
        raise ValueError("baseline identity is not rotwk-202-v9.7.7-en")
    authority = baseline.get("authority")
    boundary = baseline.get("claimBoundary")
    layers = baseline.get("layers")
    package_files = baseline.get("requiredPackageFiles")
    if not isinstance(authority, dict) or not isinstance(boundary, dict):
        raise ValueError("baseline authority or claim boundary is invalid")
    if boundary.get("sourcePinned") is not True:
        raise ValueError("baseline source is not pinned")
    if not isinstance(layers, list) or not isinstance(package_files, list):
        raise ValueError("baseline layers or required package files are invalid")

    root = root.resolve(strict=True)
    expected_directories = [str(row.get("directory", "")) for row in layers]
    if expected_directories != [
        "layer-0-patch202",
        "layer-1-rotwk",
        "layer-2-bfme2",
    ]:
        raise ValueError("baseline layer order changed")
    actual_directories = sorted(item.name for item in root.iterdir() if item.is_dir())
    if sorted(expected_directories) != actual_directories:
        raise ValueError(
            "layered root must contain exactly the three contracted directories"
        )

    overlay = root / "layer-0-patch202"
    if len(package_files) != 7:
        raise ValueError("baseline must identify exactly seven required package files")
    for row in package_files:
        if not isinstance(row, dict):
            raise ValueError("required package row is invalid")
        path = _package_path(overlay, str(row.get("path", "")))
        if not path.is_file():
            raise ValueError(f"required package file is missing: {row.get('path')}")
        expected_size = row.get("bytes")
        if path.stat().st_size != expected_size:
            raise ValueError(f"required package size mismatch: {row.get('path')}")
        if _digest(path, "md5") != row.get("md5"):
            raise ValueError(f"required package MD5 mismatch: {row.get('path')}")
        if _digest(path, "sha256") != row.get("sha256"):
            raise ValueError(f"required package SHA-256 mismatch: {row.get('path')}")

    exceptions = authority.get("manifestExceptions")
    if not isinstance(exceptions, list) or len(exceptions) != 1:
        raise ValueError("baseline manifest exception set is invalid")
    exception = exceptions[0]
    if (
        not isinstance(exception, dict)
        or exception.get("packageGuid") != "original-RotWK"
    ):
        raise ValueError("baseline manifest exception identity is invalid")
    exception_path = _package_path(
        root / "layer-1-rotwk", str(exception.get("path", ""))
    )
    if not exception_path.is_file():
        raise ValueError("manifest-exception archive is missing")
    if exception_path.stat().st_size != exception.get("resolvedBytes"):
        raise ValueError("manifest-exception archive size mismatch")
    if _digest(exception_path, "md5") != exception.get("md5"):
        raise ValueError("manifest-exception archive MD5 mismatch")
    if _digest(exception_path, "sha256") != exception.get("sha256"):
        raise ValueError("manifest-exception archive SHA-256 mismatch")

    policy = default_rotwk_202_archive_policy()
    if policy.policy_sha256 != authority.get("policySha256"):
        raise ValueError("composed archive policy identity mismatch")
    if len(policy.archives) != authority.get("archiveCount"):
        raise ValueError("composed archive count mismatch")
    catalog = InstallCatalog.build(root, source_policy=policy)
    catalog_sha256 = catalog.identity_sha256()
    if len(catalog.archives) != authority.get("archiveCount"):
        raise ValueError("catalog archive count mismatch")
    if len(catalog.entries) != authority.get("recordCount"):
        raise ValueError("catalog record count mismatch")
    if catalog_sha256 != authority.get("catalogSha256"):
        raise ValueError("catalog identity mismatch")
    return (
        len(catalog.archives),
        len(catalog.entries),
        policy.policy_sha256,
        catalog_sha256,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=DEFAULT_LAYERED_ROOT)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    args = parser.parse_args()
    archives, records, policy_sha256, catalog_sha256 = verify(
        args.root, args.baseline.resolve(strict=True)
    )
    print(
        "ROTWK_202_BASELINE_VERIFY PASS "
        f"archives={archives} records={records} "
        f"policy_sha256={policy_sha256} catalog_sha256={catalog_sha256}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ROTWK_202_BASELINE_VERIFY FAIL {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
