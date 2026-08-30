#!/usr/bin/env python3
"""Build or check the private exact RotWK 2.02 v9.7.7 effective tree."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import (  # noqa: E402
    InstallCatalog,
    default_rotwk_202_archive_policy,
)
from openbfme_importer.effective_tree import (  # noqa: E402
    build_effective_tree,
    render_effective_tree,
    require_exact_bytes,
    source_identity,
)


BASELINE = ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"
LAYERED_ROOT = ROOT / "workspace" / "retail-work" / "editions" / "rotwk" / "layered-install"
OUTPUT = ROOT / "workspace" / "retail-work" / "reports" / "compatibility" / "rotwk-202-v9.7.7-effective-tree.json"
SOURCE_PATHS = (
    "importer/openbfme_importer/big.py",
    "importer/openbfme_importer/catalog.py",
    "importer/openbfme_importer/effective_tree.py",
    "importer/openbfme_importer/paths.py",
    "tools/build-rotwk-202-effective-tree.py",
)


def _load_object(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("JSON root must be an object")
    return value


def _write_atomic(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def build_bytes(baseline_path: Path, layered_root: Path) -> bytes:
    baseline = _load_object(baseline_path)
    authority = baseline.get("authority")
    if baseline.get("baselineId") != "rotwk-202-v9.7.7-en" or not isinstance(authority, dict):
        raise ValueError("baseline identity is invalid")
    layered_root = layered_root.resolve(strict=True)
    policy = default_rotwk_202_archive_policy()
    if policy.policy_sha256 != authority.get("policySha256"):
        raise ValueError("composed archive policy differs from the baseline")
    catalog = InstallCatalog.build(layered_root, source_policy=policy)
    manifest = build_effective_tree(
        catalog,
        baseline_id=baseline["baselineId"],
        policy_sha256=authority["policySha256"],
        catalog_sha256=authority["catalogSha256"],
        parser_sources=source_identity(ROOT, SOURCE_PATHS),
        expected_records=authority["recordCount"],
        expected_winners=48_566,
        expected_shadows=4_867,
    )
    if manifest["counts"]["archives"] != authority["archiveCount"]:
        raise ValueError("effective-tree archive count differs from the baseline")
    return render_effective_tree(manifest)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--root", type=Path, default=LAYERED_ROOT)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    workspace = (ROOT / "workspace").resolve()
    output = args.output.resolve()
    try:
        output.relative_to(workspace)
    except ValueError as exc:
        raise ValueError("effective-tree output must remain below workspace") from exc
    payload = build_bytes(args.baseline.resolve(strict=True), args.root)
    if args.check:
        require_exact_bytes(output, payload)
    else:
        _write_atomic(output, payload)
    print(
        "ROTWK_202_EFFECTIVE_TREE PASS winners=48566 records=53433 shadows=4867 "
        "policy_sha256=aaf30a92eacc76a8b11c0534235e569653f06fecb47400e28260981b5a04cf31 "
        "catalog_sha256=e1485aa8af794e0d154d2f5ccb65fa24af937c4ac9731f27d62e0eef9753c748"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ROTWK_202_EFFECTIVE_TREE FAIL {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
