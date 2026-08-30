#!/usr/bin/env python3
"""Build/check the private exact RotWK 2.02 feature and reference graph."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import InstallCatalog, default_rotwk_202_archive_policy  # noqa: E402
from openbfme_importer.effective_tree import source_identity  # noqa: E402
from openbfme_importer.module_census import effective_ini_entries  # noqa: E402
from openbfme_importer.requirement_graph import (  # noqa: E402
    EXPECTED_COUNTS,
    build_feature_graph,
    render_feature_graph,
)
from openbfme_importer.retail_ini_coverage import classify_assignment_value  # noqa: E402
from openbfme_importer.sage_ini import MAX_INI_BYTES  # noqa: E402


BASELINE = ROOT / "contracts/rotwk-202-v9.7.7-baseline.json"
PRODUCT = ROOT / "contracts/rotwk-202-v9.7.7-product-scope.json"
LAYERED_ROOT = ROOT / "workspace/retail-work/editions/rotwk/layered-install"
EFFECTIVE_TREE = ROOT / "workspace/retail-work/reports/compatibility/rotwk-202-v9.7.7-effective-tree.json"
OUTPUT = ROOT / "workspace/retail-work/reports/compatibility/rotwk-202-v9.7.7-feature-graph.json"
EFFECTIVE_TREE_SHA256 = "52fe8f2eb81371804e0b95f205561c0e3e98b58be86d3ee04f74505a93c1b6e6"
SOURCE_PATHS = (
    "importer/openbfme_importer/requirement_graph.py",
    "importer/openbfme_importer/retail_ini_coverage.py",
    "importer/openbfme_importer/sage_ini.py",
    "importer/openbfme_importer/sage_particles.py",
    "tools/build-rotwk-202-compatibility.py",
    "tools/retail-ini-coverage.py",
)


def _json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("JSON root must be an object")
    return value


def _legacy_scanner():
    path = ROOT / "tools/retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("openbfme_retail_ini_scanner", path)
    if spec is None or spec.loader is None:
        raise ValueError("retail INI scanner cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _classifications(catalog: InstallCatalog, scanner, scan) -> dict[tuple[str, int, str], str]:
    asset_counts: Counter[tuple[str, int, str]] = Counter(
        (str(row["sourceIni"]).casefold(), int(row["line"]), str(row["field"]).casefold())
        for row in scan.asset_references
    )
    module_by_site = {
        (str(row["sourceIni"]).casefold(), int(row["line"]), str(row["field"]).casefold()): row.get("moduleKind")
        for row in scan.assignment_sites
    }
    result: dict[tuple[str, int, str], str] = {}
    for entry in effective_ini_entries(catalog.entries):
        archive = catalog.open_archive_for(entry)
        source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_INI_BYTES)
        for line in scanner.coverage_lines(source):
            assignment = scanner._split_assignment(line.text.strip())
            if assignment is None:
                continue
            field, value = assignment
            key = (entry.name.casefold(), line.number, field.casefold())
            result[key] = classify_assignment_value(
                value,
                module_kind=module_by_site.get(key),
                asset_reference_count=asset_counts[key],
            )
    return result


def build_bytes(baseline_path: Path, product_path: Path, layered_root: Path, effective_tree: Path) -> bytes:
    baseline = _json(baseline_path)
    product = _json(product_path)
    authority = baseline["authority"]
    if hashlib.sha256(effective_tree.read_bytes()).hexdigest() != EFFECTIVE_TREE_SHA256:
        raise ValueError("accepted effective-tree artifact is missing or changed")
    policy = default_rotwk_202_archive_policy()
    catalog = InstallCatalog.build(layered_root.resolve(strict=True), source_policy=policy)
    if policy.policy_sha256 != authority["policySha256"] or catalog.identity_sha256() != authority["catalogSha256"]:
        raise ValueError("feature-graph source identity differs from the baseline")
    scanner = _legacy_scanner()
    scan = scanner.scan_game(catalog, "rotwk")
    graph = build_feature_graph(
        scan,
        baseline_id=baseline["baselineId"],
        policy_sha256=authority["policySha256"],
        catalog_sha256=authority["catalogSha256"],
        effective_tree_sha256=EFFECTIVE_TREE_SHA256,
        source_identity=source_identity(ROOT, SOURCE_PATHS),
        root_queries=list(product["root_discovery_queries"]),
        assignment_classifications=_classifications(catalog, scanner, scan),
        expected_counts=EXPECTED_COUNTS,
    )
    return render_feature_graph(graph)


def _write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", choices=("feature-graph",), required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--product", type=Path, default=PRODUCT)
    parser.add_argument("--root", type=Path, default=LAYERED_ROOT)
    parser.add_argument("--effective-tree", type=Path, default=EFFECTIVE_TREE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        output.relative_to((ROOT / "workspace").resolve())
    except ValueError as exc:
        raise ValueError("feature-graph output must remain below workspace") from exc
    payload = build_bytes(
        args.baseline.resolve(strict=True), args.product.resolve(strict=True),
        args.root, args.effective_tree.resolve(strict=True),
    )
    if args.check:
        if not output.is_file() or output.read_bytes() != payload:
            raise ValueError("feature-graph artifact is missing or differs from deterministic output")
    else:
        _write(output, payload)
    print("ROTWK_202_FEATURE_GRAPH PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ROTWK_202_FEATURE_GRAPH FAIL {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
