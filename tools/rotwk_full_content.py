#!/usr/bin/env python3
"""One-button RotWK convert, faction pack-proof, and bound 10-map publish.

Publishing never changes ``selection.json`` unless ``--select`` is explicit.
Retail outputs remain under the configured private state/content roots.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))
sys.path.insert(0, str(ROOT / "tools"))

from openbfme_importer.game import workspace_root  # noqa: E402
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from rotwk_layered_install import ensure_layered_rotwk_install  # noqa: E402
from rotwk_multimap_skirmish import _default_effective_assets  # noqa: E402

FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar")


def _fingerprint(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def _run(label: str, command: list[str], env: dict[str, str]) -> None:
    print(f"\n=== {label} ===", flush=True)
    print("RUN", " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=ROOT, env=env, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{label} failed with exit {completed.returncode}")


def _load_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"JSON root is not an object: {path}")
    return value


def _fortress_proof(state_root: Path) -> dict[str, list[str]]:
    coverage_root = workspace_root(state_root, "rotwk") / "reports" / "faction-import"
    result: dict[str, list[str]] = {}
    for faction in FACTIONS:
        coverage = _load_object(coverage_root / f"{faction}-coverage.json")
        summary = coverage.get("summary") or {}
        if int(summary.get("converterGapCount") or 0) != 0 or not bool(
            summary.get("conversionComplete")
        ):
            raise RuntimeError(
                f"{faction} conversion is incomplete: "
                f"complete={summary.get('conversionComplete')} "
                f"converterGapCount={summary.get('converterGapCount')}"
            )
        converted = {
            str(row.get("id") or "")
            for row in coverage.get("objects") or []
            if isinstance(row, dict) and row.get("status") == "converted"
        }
        fortress = sorted(
            name
            for name in converted
            if "fortress" in name.casefold() or "citadel" in name.casefold()
        )
        pads = sorted(name for name in converted if "expansionpad" in name.casefold())
        if not fortress or not pads:
            raise RuntimeError(
                f"{faction} fortress closure incomplete: fortress/citadel={fortress} pads={pads}"
            )
        result[faction] = [*fortress, *pads]
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, default=os.environ.get("ROTWK_INSTALL"))
    parser.add_argument("--bfme2-install", type=Path, default=None)
    parser.add_argument("--state-root", type=Path, default=ROOT / ".private" / "retail-work")
    parser.add_argument("--effective-assets", type=Path, default=None)
    parser.add_argument("--python", type=Path, default=None)
    parser.add_argument("--map-limit", type=int, default=10)
    parser.add_argument("--select", action="store_true")
    args = parser.parse_args(argv)

    if args.install is None:
        print("FAIL: pass --install or set ROTWK_INSTALL", file=sys.stderr)
        return 2
    if args.map_limit != 10:
        print("FAIL: the full-content playtest contract requires --map-limit 10", file=sys.stderr)
        return 2
    install = Path(args.install).expanduser().resolve()
    if not (install / "game.dat").is_file():
        print(f"FAIL: RotWK game.dat missing: {install}", file=sys.stderr)
        return 2
    state_root = ensure_external_to_repo(
        Path(args.state_root).expanduser().resolve(), repo_root_from_module()
    )
    try:
        layered = ensure_layered_rotwk_install(
            state_root,
            rotwk_install=install,
            bfme2_install=args.bfme2_install,
        )
    except Exception as exc:
        print(f"FAIL layered install: {exc}", file=sys.stderr)
        return 2
    assets = (
        Path(args.effective_assets).expanduser().resolve()
        if args.effective_assets
        else _default_effective_assets(state_root, "rotwk")
    )
    if assets is None or not assets.is_dir():
        print("FAIL: layered RotWK effective-assets tree is missing", file=sys.stderr)
        return 2

    python = (
        Path(args.python).expanduser().resolve()
        if args.python
        else Path(sys.executable)
    )
    env = os.environ.copy()
    env["OPENBFME_IMPORT_ROOT"] = str(state_root)
    env["PYTHONPATH"] = str(ROOT / "importer") + os.pathsep + env.get("PYTHONPATH", "")
    selection = ROOT / ".private" / "content-packs" / "selection.json"
    selection_before = _fingerprint(selection)
    try:
        _run(
            "effective-assets identity",
            [
                str(python),
                str(ROOT / "tools" / "openbfme_import.py"),
                "verify-effective-assets",
                "--assets-root",
                str(assets),
                "--expect-game",
                "rotwk",
            ],
            env,
        )
        convert_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_faction_convert_batch.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
        ]
        for faction in FACTIONS:
            convert_cmd.extend(["--faction", faction])
        _run(
            "convert all seven factions",
            convert_cmd,
            env,
        )
        fortress = _fortress_proof(state_root)
        proof_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_faction_pack_proof.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
            "--publish",
        ]
        for faction in FACTIONS:
            proof_cmd.extend(["--faction", faction])
        # Never --select here: intermediate faction packs must not clobber selection.
        _run("pack-proof all seven factions (publish only)", proof_cmd, env)
        map_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_multimap_skirmish.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
            "--effective-assets",
            str(assets),
            "--full-profile",
            "--build",
            "--publish",
            "--map-limit",
            "10",
        ]
        # No --select: maps pack published but not activated until composite write below.
        _run("cook and publish bound 10-map pack (publish only)", map_cmd, env)
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 3

    if not args.select and _fingerprint(selection) != selection_before:
        print("FAIL: selection.json changed without --select", file=sys.stderr)
        return 4
    faction_report = _load_object(
        state_root / "reports" / "rotwk-faction-pack-proof.json"
    )
    if faction_report.get("publicationReadyCount") != 7:
        print("FAIL: faction publication proof is not 7/7", file=sys.stderr)
        return 4
    map_report = _load_object(state_root / "reports" / "rotwk-multimap-skirmish.json")
    profile = _load_object(Path(str(map_report["profile"])))
    map_ids = [
        str(row.get("id"))
        for row in profile["runtime_data"]["data/maps.json"]["maps"]
    ]
    unbound = list(map_report.get("unboundObjectTypes") or [])
    notable_unbound = [
        name
        for name in unbound
        if name in {"CaptureFlag", "SignalFire"} or "lair" in name.casefold()
    ]
    unbound_sample = list(dict.fromkeys([*notable_unbound, *unbound[:20]]))[:40]
    published = str((map_report.get("build") or {}).get("publishedPack") or "")
    content_root = ROOT / ".private" / "content-packs"

    # Build supplemental pack paths for all 7 factions from proof report.
    supplemental: list[str] = []
    for row in faction_report.get("factions") or []:
        if not isinstance(row, dict):
            continue
        pack_path = Path(str(row.get("publishedPack") or row.get("pack") or ""))
        # Prefer content-packs mount path if published next to content root.
        rel = None
        if pack_path.is_dir():
            try:
                # published as content-packs/<id>/<bundle>/
                if pack_path.parent.parent.resolve() == content_root.resolve():
                    rel = f"{pack_path.parent.name}/{pack_path.name}".replace("\\", "/")
                elif pack_path.parent.resolve() == content_root.resolve():
                    # content-packs/<id> with single child hash dir
                    children = [p for p in pack_path.iterdir() if p.is_dir()]
                    if len(children) == 1:
                        rel = f"{pack_path.name}/{children[0].name}".replace("\\", "/")
            except OSError:
                rel = None
        if rel is None:
            # Fall back: look for rotwk-<faction>-vslice under content-packs
            faction = str(row.get("faction") or "")
            candidates = sorted(content_root.glob(f"rotwk-{faction}-vslice/*"))
            dirs = [p for p in candidates if p.is_dir()]
            if dirs:
                newest = max(dirs, key=lambda p: p.stat().st_mtime)
                rel = f"{newest.parent.name}/{newest.name}".replace("\\", "/")
        if rel:
            supplemental.append(rel)

    maps_active = None
    if published:
        pub = Path(published)
        try:
            if pub.parent.parent.resolve() == content_root.resolve():
                maps_active = f"{pub.parent.name}/{pub.name}".replace("\\", "/")
        except OSError:
            maps_active = None
    if maps_active is None:
        # newest rotwk-skirmish-maps-private child
        sk = content_root / "rotwk-skirmish-maps-private"
        if sk.is_dir():
            kids = [p for p in sk.iterdir() if p.is_dir()]
            if kids:
                newest = max(kids, key=lambda p: p.stat().st_mtime)
                maps_active = f"rotwk-skirmish-maps-private/{newest.name}"

    if args.select:
        if not maps_active or len(supplemental) < 7:
            print(
                "FAIL: cannot compose selection "
                f"(maps={maps_active!r} supplemental={len(supplemental)}/7)",
                file=sys.stderr,
            )
            return 4
        document = {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": maps_active,
            "supplementalPacks": supplemental,
        }
        selection.parent.mkdir(parents=True, exist_ok=True)
        selection.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(
            f"SELECTION wrote activePack={maps_active} "
            f"supplemental={len(supplemental)}",
            flush=True,
        )

    print("\n========== PLAYTEST ==========")
    print(f"maps pack: {published or maps_active}")
    print(f"selection: {selection} ({'updated composite' if args.select else 'unchanged'})")
    if args.select and maps_active:
        print(f"activePack: {maps_active}")
        print("supplementalPacks: " + ", ".join(supplemental))
    print("factions: " + ", ".join(FACTIONS))
    print("maps (10): " + ", ".join(map_ids))
    print(
        "fortress closure: "
        + "; ".join(f"{key}={len(value)}" for key, value in fortress.items())
    )
    if unbound:
        print(
            f"known unbound types ({len(unbound)} unique; sample): "
            + ", ".join(unbound_sample)
        )
        print(
            f"full unbound list: {state_root / 'reports' / 'rotwk-multimap-skirmish.json'}"
        )
    else:
        print("known unbound types: none reported")
    print("==============================")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
