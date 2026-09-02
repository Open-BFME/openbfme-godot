"""Composition and CLI plumbing for the complete generic bundle cook."""

from __future__ import annotations

import argparse
from collections import Counter, OrderedDict
from collections.abc import Iterable, Mapping, Sequence
import os
from pathlib import Path
import sys
from typing import Any

from . import objects
from ._blocks import normalize_documents
from .combat import add_combat_tables
from .movement import add_movement_tables
from .tech import add_tech_tables


REPORT_SCHEMA = "openbfme.cook.bundle.report.v1"


def _combined_report(
    bundle: Mapping[str, Any],
    object_report: Mapping[str, Any],
    failures: Sequence[Mapping[str, str]],
) -> dict[str, object]:
    nuggets = Counter(
        str(nugget["kind"])
        for weapon in bundle.get("weapons", [])
        for nugget in weapon.get("nuggets", [])
    )
    return {
        "schema": REPORT_SCHEMA,
        "weapons": len(bundle.get("weapons", [])),
        "armors": len(bundle.get("armors", [])),
        "damage_fx": len(bundle.get("damage_fx", [])),
        "locomotors": len(bundle.get("locomotors", [])),
        "locomotor_sets": len(bundle.get("locomotor_sets", [])),
        "hordes": len(bundle.get("hordes", [])),
        "upgrades": len(bundle.get("upgrades", [])),
        "sciences": len(bundle.get("sciences", [])),
        "special_powers": len(bundle.get("special_powers", [])),
        "command_buttons": len(bundle.get("command_buttons", [])),
        "command_sets": len(bundle.get("command_sets", [])),
        "nuggets_by_kind": dict(sorted(nuggets.items())),
        "parse_failures": [dict(row) for row in failures],
        "objects": dict(object_report),
    }


def cook_documents(
    documents: Iterable[tuple[str, bytes]],
) -> objects.CookResult:
    normalized = normalize_documents(documents)
    object_result = objects.cook_documents(normalized)
    bundle = object_result.bundle
    failures = [
        {
            "file": str(row["file"]),
            "block": str(row.get("block", row.get("template", ""))),
            "message": str(row["message"]),
        }
        for row in object_result.report["parse_failures"]
    ]
    failures.extend(add_combat_tables(bundle, normalized))
    failures.extend(add_movement_tables(bundle, normalized))
    failures.extend(add_tech_tables(bundle, normalized))
    return objects.CookResult(
        bundle=bundle,
        report=_combined_report(bundle, object_result.report, failures),
    )


def _root_paths(root: Path) -> list[Path]:
    def is_nested_package(path: Path) -> bool:
        parts = [part.casefold() for part in path.relative_to(root).parts]
        return any(
            parts[index : index + 2] == ["data", "ini"]
            for index in range(len(parts) - 1)
        )

    return sorted(
        (
            path
            for path in root.rglob("*")
            if path.is_file()
            and path.suffix.casefold() in {".ini", ".inc"}
            and not is_nested_package(path)
        ),
        key=lambda path: (path.relative_to(root).as_posix().casefold(), path.as_posix()),
    )


def _layer_rows(root: Path, label: str) -> list[tuple[str, bytes, str]]:
    rows: list[tuple[str, bytes, str]] = []
    for path in _root_paths(root):
        relative = path.relative_to(root).as_posix()
        virtual_path = f"data/ini/{relative}"
        source = objects._normalize_newlines(path.read_bytes())
        rows.append((virtual_path, source, f"{label}/{virtual_path}"))
    return rows


def layered_documents(
    ini_root: Path | str, mods: Sequence[Path | str]
) -> tuple[list[tuple[str, bytes]], list[tuple[str, bytes]]]:
    """Return effective documents plus provenance rows for loose mod layers."""

    base = Path(ini_root).resolve()
    if not base.is_dir():
        raise ValueError(f"INI root is not a directory: {base}")
    layers: list[tuple[Path, str]] = [(base, "layer-000-base")]
    for index, raw_mod in enumerate(mods, start=1):
        mod = Path(raw_mod).resolve()
        ini = mod / "data" / "ini"
        if not ini.is_dir():
            raise ValueError(f"mod has no data/ini directory: {mod}")
        layers.append((ini, f"layer-{index:03d}-mod"))

    effective: OrderedDict[str, tuple[str, bytes]] = OrderedDict()
    identity: list[tuple[str, bytes]] = []
    for root, label in layers:
        rows = _layer_rows(root, label)
        if not rows:
            raise ValueError(f"INI layer contains no .ini or .inc files: {root}")
        for virtual_path, source, identity_path in rows:
            effective[virtual_path.casefold()] = (virtual_path, source)
            identity.append((identity_path, source))
    documents = sorted(
        effective.values(), key=lambda item: (item[0].casefold(), item[0])
    )
    return documents, identity


def cook_layered_ini(
    ini_root: Path | str, mods: Sequence[Path | str]
) -> objects.CookResult:
    if not mods:
        return cook_ini_root(ini_root)
    documents, identity = layered_documents(ini_root, mods)
    result = cook_documents(documents)
    result.bundle["source"] = objects._source_identity(identity)
    return result


def cook_ini_root(ini_root: Path | str) -> objects.CookResult:
    root = Path(ini_root).resolve()
    if not root.is_dir():
        raise ValueError(f"INI root is not a directory: {root}")
    return cook_documents(objects._path_documents(_root_paths(root), root))


def cook_files(files: Sequence[Path | str]) -> objects.CookResult:
    paths = [Path(path).resolve() for path in files]
    if not paths:
        raise ValueError("--files requires at least one INI file")
    common = Path(os.path.commonpath([str(path.parent) for path in paths])).resolve()
    return cook_documents(objects._path_documents(paths, common))


def write_bundle(bundle: Mapping[str, object], path: Path | str) -> None:
    objects.write_bundle(bundle, path)


def write_report(report: Mapping[str, object], path: Path | str) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(objects._canonical_json_bytes(report))


def run_cli(label: str, description: str | None, argv: Sequence[str] | None) -> int:
    parser = argparse.ArgumentParser(description=description)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--ini-root", type=Path)
    inputs.add_argument("--files", type=Path, nargs="+")
    parser.add_argument("--mod", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    if args.mod and args.ini_root is None:
        parser.error("--mod requires --ini-root")
    try:
        result = (
            cook_layered_ini(args.ini_root, args.mod)
            if args.ini_root is not None
            else cook_files(args.files)
        )
        write_bundle(result.bundle, args.out)
        if args.report is not None:
            write_report(result.report, args.report)
    except (OSError, TypeError, ValueError) as exc:
        parser.exit(1, f"{label} cook failed: {exc}\n")
    failures = result.report["parse_failures"]
    if failures:
        print(
            f"{label} cook retained a partial bundle with "
            f"{len(failures)} named parse failure(s)",
            file=sys.stderr,
        )
        return 2
    return 0
