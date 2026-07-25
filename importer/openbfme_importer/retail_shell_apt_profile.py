"""Seal the exact BFME2 main-menu shell APT source and conversion contract.

The shell ingress lane mirrors :mod:`retail_hud_apt_profile`: it proves the
retail source closure against the effective-assets manifest and the install
catalog, then emits the catalog-resolvable ``ImportProfile`` fragment whose
single ``sage-apt-shell-runtime`` resource cooks
``data/ui/shell/scene-contract.json`` plus the hash-addressed shell atlases.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
from typing import Any, Mapping

from .catalog import InstallCatalog
from .profile import ImportProfile, resolve_profile
from .retail_shell_apt_convert import (
    ATLAS_DIRECTORY,
    MOVIE_CLOSURE,
    RUNTIME_OUTPUT_PATH,
    SCENE_ID,
    shell_source_virtual_paths,
)
from .sage_apt import canonical_sha256
from .util import write_json_atomic


SHELL_APT_PLAN_SCHEMA = "openbfme.retail-shell-apt-plan"
SHELL_APT_PLAN_SCHEMA_VERSION = 0

SHELL_APT_RESOURCE_ID = "shell-apt-runtime-bundle"
SHELL_APT_CONVERTER = "sage-apt-shell-runtime"
SHELL_APT_PACK_KEY = "shellScene"
SHELL_APT_RUNTIME_PATH = RUNTIME_OUTPUT_PATH

#: The retail BFME2 1.06 shell closure, proved once against the effective
#: assets view.  These are bounds, not guesses: a source set that no longer
#: matches fails closed rather than silently cooking a different shell.
EXPECTED_SOURCE_COUNT = 253
EXPECTED_PAYLOAD_BYTES = 10_991_938
EXPECTED_SOURCE_AGGREGATE_SHA256 = (
    "a3f4bd9028c3620ce63c9315402d0fbe4f8918b490cdcc421c4a3cd30e89271a"
)
#: One BIG archive per closure movie.
EXPECTED_ARCHIVES = {
    "apt/aptlevel0.big": 3,
    "apt/background.big": 4,
    "apt/gamewindowgadgets.big": 45,
    "apt/mainmenu.big": 36,
    "apt/menuexport.big": 162,
    "apt/menuframeandbg.big": 3,
}

_MAX_JSON_BYTES = 64 * 1024 * 1024


def _mapping(value: object, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return dict(value)


def _load_json(path: Path | str, context: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{context} is missing or too large")
    try:
        return _mapping(json.loads(source.read_text(encoding="utf-8")), context)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {context}: {exc}") from exc


def _private_root(path: Path | str) -> Path:
    root = Path(path).expanduser().resolve()
    if ".private" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets root must be an existing private directory")
    return root


def shell_profile_resource(
    *,
    expected_source_aggregate_sha256: str | None = EXPECTED_SOURCE_AGGREGATE_SHA256,
    patterns: list[str] | None = None,
) -> dict[str, Any]:
    """Return the single shell bundle resource for an ``ImportProfile``."""

    rows = sorted(patterns or [], key=str.casefold)
    resource: dict[str, Any] = {
        "id": SHELL_APT_RESOURCE_ID,
        "kind": "ui",
        "patterns": rows,
        "required": True,
        "converter": SHELL_APT_CONVERTER,
        "output": SHELL_APT_RUNTIME_PATH,
        "options": {},
        "limit": len(rows),
        "expected_count": len(rows),
    }
    if expected_source_aggregate_sha256 is not None:
        resource["options"]["expectedSourceAggregateSha256"] = (
            expected_source_aggregate_sha256
        )
    return resource


def _validate_profile(
    resources: list[dict[str, Any]], catalog: InstallCatalog, expected_selected: int
) -> bool:
    profile = {
        "format": 1,
        "id": "shell-apt-fragment-validation",
        "pack": {"id": "shell-apt-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-shell-apt-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        loaded = ImportProfile.load(path)
        resolved = resolve_profile(loaded, catalog)
    if resolved.missing_required:
        raise ValueError(
            f"generated shell profile has missing resources: {resolved.missing_required}"
        )
    if len(resolved.selected_entries) != expected_selected:
        raise ValueError("generated shell profile selected-entry total changed")
    return True


def build_retail_shell_apt_plan(
    effective_assets_root: Path | str,
    effective_assets_manifest: Mapping[str, Any],
    catalog: InstallCatalog,
) -> dict[str, Any]:
    """Prove the exact shell source closure and emit the profile fragment."""

    root = _private_root(effective_assets_root)
    rows_raw = effective_assets_manifest.get("files")
    if not isinstance(rows_raw, list):
        raise ValueError("effective-assets manifest files must be an array")
    by_path: dict[str, dict[str, Any]] = {}
    for raw in rows_raw:
        row = _mapping(raw, "effective-assets manifest row")
        path = row.get("path")
        if not isinstance(path, str) or not path or path.casefold() in by_path:
            raise ValueError("effective-assets manifest contains an invalid path")
        if not isinstance(row.get("size"), int) or not isinstance(
            row.get("sha256"), str
        ):
            raise ValueError(
                "effective-assets manifest contains an invalid source identity"
            )
        by_path[path.casefold()] = row

    virtual_paths = shell_source_virtual_paths(root)
    if len(virtual_paths) != EXPECTED_SOURCE_COUNT:
        raise ValueError("shell APT source closure count changed")

    inventory: list[dict[str, Any]] = []
    archives: dict[str, int] = {}
    for virtual_path in virtual_paths:
        row = by_path.get(virtual_path.casefold())
        if row is None:
            raise ValueError(f"shell source missing from manifest: {virtual_path}")
        source = root / Path(*virtual_path.split("/"))
        if not source.is_file() or source.stat().st_size != row["size"]:
            raise ValueError(f"shell source missing or size changed: {virtual_path}")
        payload = source.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        if digest != row["sha256"]:
            raise ValueError(f"shell source digest changed: {virtual_path}")
        entry = catalog.resolve_exact(virtual_path)
        if (
            entry is None
            or entry.size != row["size"]
            or entry.archive.casefold() != str(row["archive"]).casefold()
        ):
            raise ValueError(f"catalog winner disagrees with manifest: {virtual_path}")
        archives[entry.archive.casefold()] = archives.get(entry.archive.casefold(), 0) + 1
        inventory.append(
            {
                "virtualPath": virtual_path,
                "byteLength": row["size"],
                "sha256": digest,
            }
        )
    if archives != EXPECTED_ARCHIVES:
        raise ValueError("shell APT archive closure changed")
    payload_bytes = sum(int(row["byteLength"]) for row in inventory)
    if payload_bytes != EXPECTED_PAYLOAD_BYTES:
        raise ValueError("shell APT payload byte total changed")
    source_aggregate = canonical_sha256(inventory)
    if source_aggregate != EXPECTED_SOURCE_AGGREGATE_SHA256:
        raise ValueError("shell APT source aggregate changed")

    resources = [
        shell_profile_resource(
            expected_source_aggregate_sha256=source_aggregate,
            patterns=virtual_paths,
        )
    ]
    profile_validated = _validate_profile(resources, catalog, len(virtual_paths))

    plan: dict[str, Any] = {
        "schema": SHELL_APT_PLAN_SCHEMA,
        "schemaVersion": SHELL_APT_PLAN_SCHEMA_VERSION,
        "sceneId": SCENE_ID,
        "sourceEvidence": {
            "aggregateSha256": effective_assets_manifest.get("aggregate_sha256"),
            "catalog": deepcopy(effective_assets_manifest.get("catalog")),
            "sourceAggregateSha256": source_aggregate,
            "sourceCount": len(inventory),
            "sourcePayloadBytes": payload_bytes,
            "archives": {key: archives[key] for key in sorted(archives)},
            "files": inventory,
            "privateReadBoundary": {
                "policy": "exact-manifest-sources-below-private-root",
            },
        },
        "policy": {
            "scope": "bfme2-shell-six-movie-apt-closure",
            "universalFlashRuntime": False,
            "executesActionScript": False,
            "substitutesAllowed": False,
            "genericArtAllowed": False,
            "retailPayloadInPlan": False,
            "profileFragmentValidatedByImportProfileAndCatalog": profile_validated,
        },
        "movies": [{"movie": name, "role": role} for name, role in MOVIE_CLOSURE],
        "compositeOutputs": [
            {
                "id": "bfme2.ui.shell.scene-contract",
                "path": SHELL_APT_RUNTIME_PATH,
                "semantics": "shell-static-draw-contract",
                "status": (
                    "runtime-contract-static-frame-zero-subset-ready-"
                    "timeline-and-native-backdrop-fail-closed"
                ),
            },
            {
                "id": "bfme2.ui.shell.atlases",
                "path": ATLAS_DIRECTORY,
                "semantics": "lossless-hash-addressed-png-directory",
                "status": "runtime-bundle-atlases-ready",
            },
        ],
        "packBinding": {
            "packFileKey": SHELL_APT_PACK_KEY,
            "runtimePath": SHELL_APT_RUNTIME_PATH,
        },
        "profileFragment": {"resources": resources},
        "summary": {
            "movieCount": len(MOVIE_CLOSURE),
            "sourceCount": len(inventory),
            "sourcePayloadBytes": payload_bytes,
            "archiveCount": len(archives),
            "profileResourceCount": len(resources),
        },
    }
    plan["aggregateSha256"] = canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "bfme2-shell-apt-generated",
    pack_id: str = "bfme2-shell-apt-private",
) -> dict[str, Any]:
    """Return the catalog-resolvable source profile for the shell contract."""

    document = _mapping(plan, "shell APT plan")
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if declared != canonical_sha256(basis):
        raise ValueError("shell APT plan aggregate digest mismatch")
    fragment = _mapping(document.get("profileFragment"), "shell profileFragment")
    resources = fragment.get("resources")
    if not isinstance(resources, list) or len(resources) != 1:
        raise ValueError("shell APT profile fragment must contain one bundle resource")
    if resources[0].get("id") != SHELL_APT_RESOURCE_ID:
        raise ValueError("shell APT profile fragment resource identity changed")
    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II main-menu shell exact APT source closure",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-shell-apt-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile


def write_retail_shell_apt_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    document = _mapping(plan, "shell APT plan")
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if (
        document.get("schema") != SHELL_APT_PLAN_SCHEMA
        or declared != canonical_sha256(basis)
    ):
        raise ValueError("cannot write invalid shell APT plan")
    write_json_atomic(Path(path), deepcopy(document))


def write_generated_import_profile(
    path: Path | str, profile: Mapping[str, Any]
) -> None:
    document = _mapping(profile, "generated shell ImportProfile")
    with tempfile.TemporaryDirectory(prefix="openbfme-shell-apt-write-") as raw:
        check = Path(raw) / "profile.json"
        check.write_text(json.dumps(document), encoding="utf-8")
        ImportProfile.load(check)
    write_json_atomic(Path(path), deepcopy(document))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--profile", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    plan = build_retail_shell_apt_plan(
        args.effective_assets,
        _load_json(args.manifest, "effective-assets manifest"),
        InstallCatalog.load(args.catalog),
    )
    profile = generated_import_profile(plan)
    write_retail_shell_apt_plan(args.output, plan)
    write_generated_import_profile(args.profile, profile)
    print(json.dumps({"aggregateSha256": plan["aggregateSha256"], **plan["summary"]}, sort_keys=True))
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())


__all__ = [
    "EXPECTED_ARCHIVES",
    "EXPECTED_PAYLOAD_BYTES",
    "EXPECTED_SOURCE_AGGREGATE_SHA256",
    "EXPECTED_SOURCE_COUNT",
    "SHELL_APT_CONVERTER",
    "SHELL_APT_PACK_KEY",
    "SHELL_APT_PLAN_SCHEMA",
    "SHELL_APT_PLAN_SCHEMA_VERSION",
    "SHELL_APT_RESOURCE_ID",
    "SHELL_APT_RUNTIME_PATH",
    "build_retail_shell_apt_plan",
    "generated_import_profile",
    "shell_profile_resource",
    "write_generated_import_profile",
    "write_retail_shell_apt_plan",
]
