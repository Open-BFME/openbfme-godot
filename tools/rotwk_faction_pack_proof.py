#!/usr/bin/env python3
"""Compose + cook + audit converted RotWK faction coverage into pack receipts.

This is the publication-stage proof that convert coverage deliberately refuses
to claim (``publicationReady=false`` until a pack/runtime receipt exists).

- Reads ``<coverage-root>/<faction>-coverage.json`` and object artifacts
- Runs ``publish-faction-to-slice`` with ``--no-publish`` (no selection rewrite)
  unless ``--publish`` is passed (still never ``--select`` unless requested)
- Writes ``openbfme.faction-publication-receipt`` next to coverage

Does not invent greening: pack audit must report ``valid=true``.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.game import workspace_root  # noqa: E402
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402

RECEIPT_SCHEMA = "openbfme.faction-publication-receipt"
RECEIPT_SCHEMA_VERSION = 0
KNOWN_FACTIONS = (
    "men",
    "elves",
    "dwarves",
    "isengard",
    "mordor",
    "wild",
    "angmar",
)


def _default_coverage_root(state_root: Path, game: str) -> Path:
    return workspace_root(state_root, game) / "reports" / "faction-import"


def _parse_cli_json(text: str) -> dict[str, Any]:
    blob = (text or "").strip()
    if not blob:
        raise ValueError("empty CLI stdout")
    try:
        value = json.loads(blob)
        if isinstance(value, dict):
            return value
    except json.JSONDecodeError:
        pass
    decoder = json.JSONDecoder()
    last: dict[str, Any] | None = None
    for index, char in enumerate(blob):
        if char != "{":
            continue
        try:
            obj, _end = decoder.raw_decode(blob[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            last = obj
    if last is None:
        raise ValueError("no JSON object found in CLI output")
    return last


def _load_coverage(path: Path) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError(f"coverage root is not an object: {path}")
    if raw.get("schema") != "openbfme.faction-import-coverage":
        raise ValueError(f"unsupported coverage schema at {path}")
    summary = raw.get("summary")
    if not isinstance(summary, dict):
        raise ValueError(f"coverage summary missing at {path}")
    return raw


def _artifact_proof(coverage_root: Path, faction: str, coverage: dict[str, Any]) -> dict[str, Any]:
    """Verify converted rows have on-disk pack-recipe / runtime receipts."""
    objects = coverage.get("objects") or []
    converted = [
        row
        for row in objects
        if isinstance(row, dict) and row.get("status") == "converted"
    ]
    missing: list[dict[str, str]] = []
    present = 0
    for row in converted:
        object_id = str(row.get("id") or "")
        family = str(row.get("family") or "")
        root = coverage_root / faction / "objects" / object_id.casefold()
        recipe = root / "pack-recipe.json"
        if not recipe.is_file():
            missing.append(
                {
                    "objectId": object_id,
                    "family": family,
                    "missing": "pack-recipe.json",
                }
            )
            continue
        if family in {"structure", "spellbook"}:
            runtime = root / "runtime.json"
            if not runtime.is_file():
                missing.append(
                    {
                        "objectId": object_id,
                        "family": family,
                        "missing": "runtime.json",
                    }
                )
                continue
        present += 1
    return {
        "convertedCount": len(converted),
        "artifactsPresent": present,
        "missingCount": len(missing),
        "missingSample": missing[:20],
        "ok": len(missing) == 0 and len(converted) > 0,
    }


def _pack_incomplete_args(*, allow_incomplete_pack: bool) -> list[str]:
    """Translate only the explicit pack-build waiver to the downstream CLI."""

    return ["--allow-incomplete"] if allow_incomplete_pack else []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument(
        "--faction",
        action="append",
        choices=KNOWN_FACTIONS,
        default=None,
        help="faction to pack-proof (repeatable; default: all with coverage)",
    )
    parser.add_argument(
        "--coverage-root",
        type=Path,
        default=None,
        help="override faction-import report root",
    )
    parser.add_argument(
        "--base-profile",
        type=Path,
        default=None,
        help="host profile for compose (default: men-fords-v1.generated.json)",
    )
    parser.add_argument(
        "--artifacts-only",
        action="store_true",
        help="only verify on-disk convert artifacts; skip pack cook",
    )
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help=(
            "allow honest residual converter-gap objects in coverage only; "
            "still pack converted objects. Does NOT waive pack-build failures."
        ),
    )
    parser.add_argument(
        "--allow-incomplete-pack",
        action="store_true",
        help=(
            "forward --allow-incomplete to publish-faction-to-slice (waives "
            "missing required pack resources). Prefer fixing converters instead."
        ),
    )
    # The two overrides below were previously unreachable through this batch
    # tool: `--allow-incomplete` waives only THIS tool's own pre-check and
    # forwards nothing, while `--allow-incomplete-pack` forwards the unrelated
    # pack-BUILD waiver. So a run that needed either downstream gate had no
    # option but `--allow-incomplete-pack`, which waives the wrong thing.
    # They are deliberately separate, separately named, and off by default.
    parser.add_argument(
        "--allow-incomplete-coverage",
        action="store_true",
        help=(
            "forward --allow-incomplete-coverage to publish-faction-to-slice: "
            "publish a faction whose coverage records honest residual CONVERTER "
            "gaps. Every waived gap must be named, with its retail cause, in the "
            "change that uses this."
        ),
    )
    parser.add_argument(
        "--allow-fewer-playable-units",
        action="store_true",
        help=(
            "forward --allow-fewer-playable-units to publish-faction-to-slice: "
            "publish a cook whose playable-unit roster drops ids the incumbent "
            "bundle carries. This is a REGRESSION guard, so the only legitimate "
            "use is a baseline change that provably removes non-retail content; "
            "list every dropped id and its citation in the change that uses it."
        ),
    )
    parser.add_argument(
        "--publish",
        action="store_true",
        help="copy pack into content-packs (still no selection rewrite unless --select)",
    )
    parser.add_argument(
        "--select",
        action="store_true",
        help="rewrite selection.json (integration-owner only; requires --publish)",
    )
    parser.add_argument("--python", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args(argv)

    if args.select and not args.publish:
        print("FAIL: --select requires --publish", file=sys.stderr)
        return 2

    operator_install = args.install.expanduser().resolve()
    if not (operator_install / "game.dat").is_file():
        print(f"FAIL: no game.dat at {operator_install}", file=sys.stderr)
        return 2

    state_root = args.state_root
    if state_root is None:
        state_root = ROOT / ".private" / "retail-work"
    state_root = ensure_external_to_repo(
        Path(state_root).expanduser().resolve(), repo_root_from_module()
    )
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)

    # Match convert batch: RotWK pack cook must resolve UI textures / media
    # against the layered install when present (same catalog identity as convert).
    cook_install = operator_install
    if args.game == "rotwk":
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            from rotwk_layered_install import (  # type: ignore
                ensure_layered_rotwk_install,
                layered_rotwk_install,
            )

            layered = layered_rotwk_install(state_root)
            if layered is None:
                layered = ensure_layered_rotwk_install(
                    state_root, rotwk_install=operator_install
                )
            if layered is not None:
                cook_install = layered
                print(f"LAYERED_INSTALL {cook_install}", flush=True)
        except Exception as exc:
            print(
                f"WARN layered install unavailable ({exc}); using operator install",
                flush=True,
            )

    workspace = workspace_root(state_root, args.game)
    coverage_root = (
        Path(args.coverage_root).expanduser().resolve()
        if args.coverage_root
        else _default_coverage_root(state_root, args.game)
    )
    if not coverage_root.is_dir():
        print(f"FAIL: coverage root missing: {coverage_root}", file=sys.stderr)
        return 2

    base_profile = args.base_profile
    if base_profile is None:
        # Host skeleton lives on the BFME2 state profiles path historically.
        candidates = [
            state_root / "profiles" / "men-fords-v1.generated.json",
            workspace / "profiles" / "men-fords-v1.generated.json",
        ]
        base_profile = next((p for p in candidates if p.is_file()), candidates[0])
    else:
        base_profile = Path(base_profile).expanduser().resolve()
    if not args.artifacts_only and not base_profile.is_file():
        print(f"FAIL: base profile missing: {base_profile}", file=sys.stderr)
        return 2

    python = args.python
    if python is None:
        python = state_root / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
        if not python.is_file():
            python = Path(sys.executable)
    else:
        python = Path(python).expanduser().resolve()

    cli = ROOT / "tools" / "openbfme_import.py"
    if not cli.is_file():
        print(f"FAIL: missing CLI {cli}", file=sys.stderr)
        return 2

    if args.faction:
        factions = list(dict.fromkeys(args.faction))
    else:
        factions = [
            name
            for name in KNOWN_FACTIONS
            if (coverage_root / f"{name}-coverage.json").is_file()
        ]
    if not factions:
        print(f"FAIL: no coverage files under {coverage_root}", file=sys.stderr)
        return 2

    run_id = uuid.uuid4().hex
    rows: list[dict[str, Any]] = []
    exit_code = 0

    for faction in factions:
        coverage_path = coverage_root / f"{faction}-coverage.json"
        row: dict[str, Any] = {"faction": faction, "coverage": str(coverage_path)}
        try:
            if not coverage_path.is_file():
                raise FileNotFoundError(f"missing coverage: {coverage_path}")
            coverage = _load_coverage(coverage_path)
            summary = coverage["summary"]
            assert isinstance(summary, dict)
            row["conversionComplete"] = bool(summary.get("conversionComplete"))
            row["converterGapCount"] = int(summary.get("converterGapCount") or 0)
            row["convertedCount"] = int(summary.get("convertedCount") or 0)
            row["coverageAggregateSha256"] = str(coverage.get("aggregateSha256") or "")
            artifacts = _artifact_proof(coverage_root, faction, coverage)
            row["artifactProof"] = artifacts
            if not artifacts["ok"]:
                raise RuntimeError(
                    f"artifact proof failed missing={artifacts['missingCount']} "
                    f"sample={artifacts['missingSample'][:3]}"
                )
            if not row["conversionComplete"] or row["converterGapCount"] > 0:
                if not args.allow_incomplete:
                    raise RuntimeError(
                        "refuse pack proof on incomplete convert "
                        f"(complete={row['conversionComplete']} "
                        f"gaps={row['converterGapCount']}; pass --allow-incomplete)"
                    )
                print(
                    f"WARN {faction}: incomplete convert allowed "
                    f"(complete={row['conversionComplete']} "
                    f"gaps={row['converterGapCount']})",
                    flush=True,
                )

            if args.artifacts_only:
                receipt = {
                    "schema": RECEIPT_SCHEMA,
                    "schemaVersion": RECEIPT_SCHEMA_VERSION,
                    "game": args.game,
                    "faction": faction,
                    "mode": "artifacts-only",
                    "publicationReady": False,
                    "blockingReason": (
                        "artifact proof passed; pack cook/audit not run "
                        "(pass without --artifacts-only)"
                    ),
                    "coveragePath": str(coverage_path),
                    "coverageAggregateSha256": row["coverageAggregateSha256"],
                    "artifactProof": artifacts,
                    "runId": run_id,
                }
                receipt_path = coverage_root / f"{faction}-publication.json"
                write_json_atomic(receipt_path, receipt)
                row["receipt"] = str(receipt_path)
                row["publicationReady"] = False
                row["status"] = "artifacts-ok"
                print(
                    f"ARTIFACTS_OK {faction} converted={artifacts['convertedCount']}",
                    flush=True,
                )
            else:
                profile_output = (
                    workspace
                    / "profiles"
                    / f"faction-slice-{faction}.generated.json"
                )
                # --json is a global flag (before the subcommand).
                cmd = [
                    str(python),
                    str(cli),
                    "--json",
                    "publish-faction-to-slice",
                    "--install",
                    str(cook_install),
                    "--game",
                    args.game,
                    "--faction",
                    faction,
                    "--coverage-root",
                    str(coverage_root),
                    "--base-profile",
                    str(base_profile),
                    "--profile-output",
                    str(profile_output),
                ]
                # Coverage residual gaps (allowed above) must not waive pack-build
                # resource failures. Only --allow-incomplete-pack forwards the
                # pipeline incomplete flag to publish-faction-to-slice.
                cmd.extend(
                    _pack_incomplete_args(
                        allow_incomplete_pack=args.allow_incomplete_pack
                    )
                )
                if args.allow_incomplete_coverage:
                    cmd.append("--allow-incomplete-coverage")
                if args.allow_fewer_playable_units:
                    cmd.append("--allow-fewer-playable-units")
                if not args.publish:
                    cmd.append("--no-publish")
                if args.select:
                    cmd.append("--select")
                print("RUN", " ".join(cmd), flush=True)
                env = os.environ.copy()
                env["OPENBFME_IMPORT_ROOT"] = str(state_root)
                env["PYTHONPATH"] = str(ROOT / "importer")
                proc = subprocess.run(
                    cmd,
                    cwd=str(ROOT),
                    env=env,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                if proc.returncode != 0:
                    tail = (proc.stderr or proc.stdout or "")[-2000:]
                    raise RuntimeError(
                        f"publish-faction-to-slice exit={proc.returncode}: {tail}"
                    )
                cli_result = _parse_cli_json(proc.stdout)
                valid = bool(cli_result.get("valid"))
                conversion_failures = cli_result.get("conversion_failures")
                # None means the pack has no provenance/manifest yet; treat as
                # unknown and fail closed. Zero is the only success path.
                if conversion_failures is None or int(conversion_failures) > 0:
                    raise RuntimeError(
                        "pack cook reported conversion_failures="
                        f"{conversion_failures!r} (coverage residual gaps must "
                        "not publish packs with pack-build resource failures)"
                    )
                publication_ready = valid and bool(
                    cli_result.get("pack") or cli_result.get("bundle_sha256")
                )
                receipt = {
                    "schema": RECEIPT_SCHEMA,
                    "schemaVersion": RECEIPT_SCHEMA_VERSION,
                    "game": args.game,
                    "faction": faction,
                    "mode": "pack-cook-audit",
                    "publicationReady": publication_ready,
                    "blockingReason": (
                        None
                        if publication_ready
                        else "pack audit did not report valid pack receipt"
                    ),
                    "coveragePath": str(coverage_path),
                    "coverageAggregateSha256": row["coverageAggregateSha256"],
                    "artifactProof": artifacts,
                    "pack": cli_result.get("pack"),
                    "profile": cli_result.get("profile"),
                    "composeReceipt": cli_result.get("receipt"),
                    "bundleSha256": cli_result.get("bundle_sha256"),
                    "auditValid": valid,
                    "conversionFailures": conversion_failures,
                    "composedObjects": cli_result.get("composed_objects"),
                    "publishedPack": cli_result.get("published_pack"),
                    "selectionTouched": bool(args.select),
                    "runId": run_id,
                }
                receipt_path = coverage_root / f"{faction}-publication.json"
                write_json_atomic(receipt_path, receipt)
                row["receipt"] = str(receipt_path)
                row["publicationReady"] = publication_ready
                row["pack"] = cli_result.get("pack")
                row["bundleSha256"] = cli_result.get("bundle_sha256")
                row["publishedPack"] = cli_result.get("published_pack")
                row["conversionFailures"] = conversion_failures
                row["status"] = "publication-ready" if publication_ready else "pack-invalid"
                if not publication_ready:
                    raise RuntimeError("pack audit invalid / missing pack receipt")
                print(
                    f"PUBLICATION_READY {faction} pack={cli_result.get('pack')} "
                    f"bundle={cli_result.get('bundle_sha256')} "
                    f"published={cli_result.get('published_pack')}",
                    flush=True,
                )
        except Exception as exc:
            exit_code = 3
            row["status"] = "failed"
            row["publicationReady"] = False
            row["error"] = f"{type(exc).__name__}: {exc}"[:1200]
            print(f"FAIL {faction}: {exc}", file=sys.stderr)

        rows.append(row)

    report = {
        "schema": "openbfme.rotwk-faction-pack-proof",
        "schemaVersion": 1,
        "game": args.game,
        "installRoot": str(cook_install),
        "operatorInstallRoot": str(operator_install),
        "coverageRoot": str(coverage_root),
        "artifactsOnly": bool(args.artifacts_only),
        "runId": run_id,
        "factions": rows,
        "publicationReadyCount": sum(1 for r in rows if r.get("publicationReady")),
        "factionCount": len(rows),
    }
    out = args.output
    if out is None:
        out = state_root / "reports" / f"{args.game}-faction-pack-proof.json"
    else:
        out = Path(out).expanduser().resolve()
    write_json_atomic(out, report)
    print(f"REPORT {out}")
    print(
        f"PACK_PROOF ready={report['publicationReadyCount']}/{report['factionCount']} "
        f"exit={exit_code}"
    )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
