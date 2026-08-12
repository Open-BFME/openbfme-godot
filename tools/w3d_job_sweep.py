"""Convert every W3D job in a profile standalone and account for all of them.

A full pack build takes about 40 minutes and stops at its first W3D failure,
so each newly reachable retail quirk costs a whole build to discover. This
sweep stages and converts every W3D job in a profile directly, in batched
multi-job Blender processes, and reports every failure in one pass (about 13
minutes for the 1625-job maps profile). Run it before a build, not instead of
one: it proves the conversion stage only.

Staging mirrors ``pipeline._prepare_w3d_bundle_job``: the same source
selection (the resource's own patterns plus every declared input resource's
patterns) and the same three preparation passes, so a failure here is the
failure the build would hit.

Usage::

    python tools/w3d_job_sweep.py --profile <profile.json> \
        --assets <effective-assets> --root <scratch> \
        --blender <blender.exe> --plugin <OpenSAGE.BlenderPlugin> [--batch 100]

Exit status is 0 only when every job is accounted for and green.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "importer"))

from openbfme_importer.pipeline import (  # noqa: E402
    _apply_w3d_texture_overrides,
    _prepare_w3d_no_motion_animations,
    _prepare_w3d_secondary_skin_streams,
)

CONVERTER_ASSET_KINDS = {
    "w3d-bundle": "animated",
    "w3d-hierarchical": "hierarchical",
    "w3d-static": "static",
}
OK_MARKER = re.compile(r'OPENBFME_W3D_JOB_OK \{"job_id": "([^"]+)"')
FAIL_MARKER = re.compile(r"OPENBFME_W3D_JOB_FAIL (\{.*?\})\r?\n")


def stage_job(
    resource: dict[str, Any],
    by_id: dict[str, Any],
    assets_root: Path,
    batch_root: Path,
) -> dict[str, Any]:
    """Stage one job's closure and return its multi-job document entry."""

    asset_id = resource["id"]
    options = resource.get("options") or {}
    input_root = batch_root / asset_id / "input"
    if input_root.exists():
        shutil.rmtree(input_root)
    input_root.mkdir(parents=True)
    patterns = list(resource.get("patterns") or [])
    for input_id in options.get("inputResourceIds") or []:
        owner = by_id.get(input_id)
        if owner is None:
            raise RuntimeError(
                f"{asset_id} references unknown input resource {input_id}"
            )
        patterns.extend(owner.get("patterns") or [])
    copied: dict[str, Path] = {}
    for pattern in patterns:
        for match in sorted(assets_root.glob(pattern)):
            if not match.is_file():
                continue
            target = input_root / match.name
            shutil.copyfile(match, target)
            copied[match.name] = target
    model_name = options.get("model")
    model = copied.get(model_name)
    if model is None:
        raise RuntimeError(f"model not staged for {asset_id}: {model_name}")
    _prepare_w3d_no_motion_animations(
        copied, model, options.get("provenNoMotionAnimations")
    )
    _prepare_w3d_secondary_skin_streams(copied, model)
    _apply_w3d_texture_overrides(copied, model, options.get("textureOverrides"))
    animations: list[str] = []
    for name in options.get("animations") or []:
        animation = copied.get(name)
        if animation is None:
            raise RuntimeError(f"animation not staged for {asset_id}: {name}")
        # The pipeline drops retail's zero-byte animation placeholders.
        if animation.stat().st_size == 0:
            continue
        animations.append(str(animation))
    return {
        "job_id": asset_id,
        "model": str(model),
        "asset_kind": CONVERTER_ASSET_KINDS[resource["converter"]],
        "animations": animations,
        "required_equipment": list(options.get("requiredEquipment") or []),
        "excluded_optional_meshes": list(options.get("excludedOptionalMeshes") or []),
        "proven_root_rigid_bake": bool(options.get("provenRootRigidBake", False)),
        "proven_pivot_only_model": bool(options.get("provenPivotOnlyModel", False)),
        "retail_absent_textures": list(options.get("retailAbsentTextures") or []),
        "output": str(batch_root / asset_id / f"{asset_id}.glb"),
    }


def run_batch(
    jobs: list[dict[str, Any]],
    *,
    blender: Path,
    plugin: Path,
    jobs_path: Path,
    log_path: Path,
) -> tuple[set[str], dict[str, Any]]:
    """Run one multi-job Blender process and return its markers."""

    jobs_path.parent.mkdir(parents=True, exist_ok=True)
    jobs_path.write_text(
        json.dumps({"schema": "openbfme.w3d-multi-jobs", "jobs": jobs}, indent=1),
        encoding="utf-8",
    )
    command = [
        str(blender),
        "--factory-startup",
        "-noaudio",
        "--background",
        "--python-use-system-env",
        "--python-exit-code",
        "1",
        "--python",
        str(REPO_ROOT / "importer" / "blender" / "w3d_multi_to_glb.py"),
        "--",
        "--plugin-root",
        str(plugin),
        "--jobs",
        str(jobs_path),
    ]
    # The pipeline sets this for its own Blender children. Without it Python
    # writes .pyc files into the pinned portable tree and the next build fails
    # its tooling attestation with "contains generated Python bytecode".
    environment = dict(os.environ)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("wb") as handle:
        subprocess.run(
            command,
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=False,
            env=environment,
        )
    text = log_path.read_text(encoding="utf-8", errors="replace")
    failures: dict[str, Any] = {}
    for raw in FAIL_MARKER.findall(text):
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            continue
        failures[str(payload.get("job_id", ""))] = payload
    return set(OK_MARKER.findall(text)), failures


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--blender", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--batch", type=int, default=100)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.batch < 1:
        raise SystemExit("--batch must be at least 1")
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    by_id = {item["id"]: item for item in profile["resources"]}
    w3d_jobs = [
        item
        for item in profile["resources"]
        if item["converter"] in CONVERTER_ASSET_KINDS
    ]
    results: dict[str, Any] = {
        "ok": [],
        "failed": {},
        "stage_errors": {},
        "missing": [],
    }
    results_path = args.root / "results.json"
    began = time.time()
    for offset in range(0, len(w3d_jobs), args.batch):
        batch_root = args.root / f"batch-{offset:05d}"
        jobs: list[dict[str, Any]] = []
        for resource in w3d_jobs[offset : offset + args.batch]:
            try:
                jobs.append(stage_job(resource, by_id, args.assets, batch_root))
            except Exception as error:  # noqa: BLE001 - a staging fault is a result
                results["stage_errors"][resource["id"]] = repr(error)
        if jobs:
            ok, failures = run_batch(
                jobs,
                blender=args.blender,
                plugin=args.plugin,
                jobs_path=batch_root / "jobs.json",
                log_path=args.root / "logs" / f"batch-{offset:05d}.log",
            )
            results["ok"].extend(sorted(ok))
            results["failed"].update(failures)
            seen = ok | set(failures)
            results["missing"].extend(
                job["job_id"] for job in jobs if job["job_id"] not in seen
            )
        results_path.parent.mkdir(parents=True, exist_ok=True)
        results_path.write_text(json.dumps(results, indent=1), encoding="utf-8")
        # Reclaim disk between batches: keep only what needs investigating.
        keep = set(results["failed"]) | set(results["missing"])
        if batch_root.is_dir():
            for child in batch_root.iterdir():
                if child.is_dir() and child.name not in keep:
                    shutil.rmtree(child, ignore_errors=True)
        print(
            f"[sweep] offset={offset} ok={len(results['ok'])} "
            f"failed={len(results['failed'])} "
            f"stage_errors={len(results['stage_errors'])} "
            f"missing={len(results['missing'])} "
            f"elapsed={int(time.time() - began)}s",
            flush=True,
        )
    accounted = (
        len(results["ok"])
        + len(results["failed"])
        + len(results["stage_errors"])
        + len(results["missing"])
    )
    print(
        json.dumps(
            {
                "w3d_jobs": len(w3d_jobs),
                "accounted": accounted,
                "ok": len(results["ok"]),
                "failed": sorted(results["failed"]),
                "stage_errors": sorted(results["stage_errors"]),
                "missing": sorted(results["missing"]),
                "results": str(results_path),
            },
            indent=1,
            sort_keys=True,
        )
    )
    complete = accounted == len(w3d_jobs)
    green = not (results["failed"] or results["stage_errors"] or results["missing"])
    return 0 if complete and green else 1


if __name__ == "__main__":
    raise SystemExit(main())
