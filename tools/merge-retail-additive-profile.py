"""Build and publish a proven additive delta over an audited retail pack.

This is intentionally narrow: the target profile must reduce byte-for-byte to
the base pack's recorded profile after the explicitly named resources, runtime
records, and pack file keys are removed.  The normal importer converts only the
delta; this command then preserves the audited base provenance and records the
immutable base/delta bundle identities in the merged manifest.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import replace
import hashlib
import json
import os
from pathlib import Path
import shutil
from typing import Any

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.pipeline import (
    ImportPipeline,
    RETAIL_PROVENANCE_CONTRACT,
    _canonical_pack_inventory,
    audit_pack,
    bundle_digest,
)
from openbfme_importer.profile import ImportProfile, ResolvedProfile, resolve_profile
from openbfme_importer.util import read_json, write_json_atomic


def _canonical_payload(value: Any) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _strip_delta(
    document: dict[str, Any],
    resource_ids: set[str],
    runtime_paths: set[str],
    pack_file_keys: set[str],
) -> dict[str, Any]:
    stripped = deepcopy(document)
    resources = stripped.get("resources")
    runtime_data = stripped.get("runtime_data")
    pack = stripped.get("pack")
    files = pack.get("files") if isinstance(pack, dict) else None
    if not isinstance(resources, list) or not isinstance(runtime_data, dict):
        raise RuntimeError("target profile has an invalid additive structure")
    if not isinstance(files, dict):
        raise RuntimeError("target profile pack.files is missing")

    present_resource_ids = {
        item.get("id") for item in resources if isinstance(item, dict)
    }
    if not resource_ids <= present_resource_ids:
        raise RuntimeError("one or more additive resource ids are absent")
    if not runtime_paths <= set(runtime_data):
        raise RuntimeError("one or more additive runtime paths are absent")
    if not pack_file_keys <= set(files):
        raise RuntimeError("one or more additive pack file keys are absent")

    stripped["resources"] = [
        item
        for item in resources
        if not isinstance(item, dict) or item.get("id") not in resource_ids
    ]
    for path in runtime_paths:
        del runtime_data[path]
    for key in pack_file_keys:
        value = files[key]
        if value not in runtime_paths:
            raise RuntimeError(
                f"pack file key {key!r} does not point at an additive runtime path"
            )
        del files[key]
    return stripped


def _copy_delta_files(delta_root: Path, staging: Path) -> list[str]:
    copied: list[str] = []
    excluded = {
        "pack.json",
        "provenance/manifest.json",
        "provenance/audit.json",
    }
    for source in sorted(path for path in delta_root.rglob("*") if path.is_file()):
        relative = source.relative_to(delta_root).as_posix()
        if relative in excluded:
            continue
        destination = staging / Path(relative)
        if destination.exists():
            raise RuntimeError(f"additive output collides with base pack: {relative}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied.append(relative)
    if not copied:
        raise RuntimeError("additive conversion produced no bundle files")
    return copied


def _pack_manifest(profile: ImportProfile) -> dict[str, Any]:
    manifest = {
        "id": profile.pack_id,
        "version": profile.pack_version,
        "priority": 100,
        "title": profile.title,
        "local_retail_import": True,
        "redistributable": False,
        "profile_build_complete": True,
        "vertical_slice_complete": False,
        "provenance_contract": RETAIL_PROVENANCE_CONTRACT,
    }
    manifest.update(profile.pack_metadata)
    manifest.update(
        {
            "id": profile.pack_id,
            "version": profile.pack_version,
            "local_retail_import": True,
            "redistributable": False,
            "profile_build_complete": True,
            "vertical_slice_complete": bool(
                profile.pack_metadata.get("vertical_slice_complete", False)
            ),
            "provenance_contract": RETAIL_PROVENANCE_CONTRACT,
        }
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--content-root", type=Path, required=True)
    parser.add_argument("--resource-id", action="append", required=True)
    parser.add_argument("--runtime-path", action="append", required=True)
    parser.add_argument("--pack-file-key", action="append", required=True)
    parser.add_argument("--game", default="bfme2")
    args = parser.parse_args()

    state_root = args.state_root.resolve()
    profile_path = args.profile.resolve()
    resource_ids = set(args.resource_id)
    runtime_paths = set(args.runtime_path)
    pack_file_keys = set(args.pack_file_key)
    profile_document = read_json(profile_path)
    if not isinstance(profile_document, dict):
        raise RuntimeError("target profile root must be an object")
    full_profile = ImportProfile.load(profile_path)

    base_root = state_root / "packs" / full_profile.pack_id
    base_audit = audit_pack(base_root)
    if not base_audit["valid"]:
        raise RuntimeError("base pack is not canonically auditable")
    base_manifest_path = base_root / "provenance" / "manifest.json"
    base_manifest = read_json(base_manifest_path)
    stripped = _strip_delta(
        profile_document, resource_ids, runtime_paths, pack_file_keys
    )
    stripped_sha256 = hashlib.sha256(_canonical_payload(stripped)).hexdigest()
    if stripped_sha256 != base_manifest.get("profile_sha256"):
        raise RuntimeError(
            "target profile is not a strict additive extension of the base profile"
        )
    if base_manifest.get("profile") != full_profile.id:
        raise RuntimeError("base and target profile ids differ")

    catalog = InstallCatalog.load(state_root / "catalog" / f"{args.game}.json")
    full_resolved = resolve_profile(full_profile, catalog)
    delta_resources = tuple(
        item for item in full_resolved.resources if item.rule.id in resource_ids
    )
    if len(delta_resources) != len(resource_ids):
        raise RuntimeError("resolved additive resource set is incomplete")
    delta_pack_id = f"{full_profile.pack_id}-additive-delta"
    delta_metadata = deepcopy(full_profile.pack_metadata)
    delta_metadata["id"] = delta_pack_id
    delta_metadata["vertical_slice_complete"] = False
    delta_metadata["files"] = {
        key: value
        for key, value in full_profile.pack_metadata.get("files", {}).items()
        if key in pack_file_keys
    }
    delta_profile = replace(
        full_profile,
        source_sha256=hashlib.sha256(
            _canonical_payload(
                {
                    "resources": sorted(resource_ids),
                    "runtime_paths": sorted(runtime_paths),
                }
            )
        ).hexdigest(),
        id=f"{full_profile.id}-additive-delta",
        title=f"{full_profile.title} additive delta",
        pack_id=delta_pack_id,
        pack_metadata=delta_metadata,
        resources=tuple(item.rule for item in delta_resources),
        runtime_data={path: full_profile.runtime_data[path] for path in runtime_paths},
    )
    delta_resolved = ResolvedProfile(delta_profile, delta_resources)
    pipeline = ImportPipeline(catalog, state_root, game=args.game)
    delta_root = pipeline.packs_root / delta_pack_id
    delta_audit = audit_pack(delta_root) if delta_root.is_dir() else {"valid": False}
    delta_manifest = (
        read_json(delta_root / "provenance" / "manifest.json")
        if delta_audit["valid"]
        else {}
    )
    delta_entry_ids = {
        entry["resource_id"] for entry in delta_manifest.get("entries", [])
    }
    if delta_entry_ids != resource_ids:
        delta_root = pipeline.build(delta_resolved, force=False)
        delta_audit = audit_pack(delta_root)
        if not delta_audit["valid"]:
            raise RuntimeError("delta pack failed canonical audit")
        delta_manifest = read_json(delta_root / "provenance" / "manifest.json")
        delta_entry_ids = {
            entry["resource_id"] for entry in delta_manifest["entries"]
        }
    if delta_entry_ids != resource_ids:
        raise RuntimeError("delta provenance resource set differs from request")

    base_bundle_sha256 = bundle_digest(base_root)
    delta_bundle_sha256 = bundle_digest(delta_root)
    staging = base_root.with_name(base_root.name + ".additive-building")
    if staging.exists():
        shutil.rmtree(staging)
    shutil.copytree(base_root, staging)
    copied = _copy_delta_files(delta_root, staging)
    write_json_atomic(staging / "pack.json", _pack_manifest(full_profile))

    merged_manifest = deepcopy(base_manifest)
    merged_manifest.update(
        {
            "profile": full_profile.id,
            "profile_sha256": full_profile.source_sha256,
            "importer_version": delta_manifest["importer_version"],
            "importer_recipe": delta_manifest["importer_recipe"],
            "tools": delta_manifest["tools"],
            "incomplete": [],
            "entries": base_manifest["entries"] + delta_manifest["entries"],
            "additive_reuse": {
                "format": 1,
                "base_bundle_sha256": base_bundle_sha256,
                "base_manifest_sha256": _sha256(base_manifest_path),
                "base_profile_sha256": stripped_sha256,
                "delta_bundle_sha256": delta_bundle_sha256,
                "delta_resource_ids": sorted(resource_ids),
                "copied_files": copied,
            },
        }
    )
    merged_manifest["bundle_files"] = _canonical_pack_inventory(staging)
    write_json_atomic(staging / "provenance" / "manifest.json", merged_manifest)
    merged_audit = audit_pack(staging)
    write_json_atomic(staging / "provenance" / "audit.json", merged_audit)
    if not merged_audit["valid"]:
        raise RuntimeError(
            "merged additive pack failed canonical audit: "
            + "; ".join(merged_audit["errors"])
        )

    previous = base_root.with_name(base_root.name + ".additive-previous")
    if previous.exists():
        shutil.rmtree(previous)
    os.replace(base_root, previous)
    try:
        os.replace(staging, base_root)
    except BaseException:
        os.replace(previous, base_root)
        raise
    shutil.rmtree(previous)
    published = pipeline.publish_to_godot(base_root, args.content_root)
    print(
        json.dumps(
            {
                "base_profile_sha256": stripped_sha256,
                "profile_sha256": full_profile.source_sha256,
                "provenance_entries": len(merged_manifest["entries"]),
                "copied_files": copied,
                "audit": merged_audit,
                **published,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
