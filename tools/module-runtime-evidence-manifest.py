"""Emit the Godot runners that make executable module evidence reproducible.

The importer registry is the authority.  This tool deliberately derives the
runner list from that registry instead of maintaining a second hand-written
list which could silently drift.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.module_contracts import (  # noqa: E402
    EXECUTABLE_TYPED_MODULE_EVIDENCE,
    ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE,
)


def build_manifest() -> dict[str, object]:
    claims: dict[str, dict[str, set[str]]] = {}
    for scope, registry in (
        ("field", EXECUTABLE_TYPED_MODULE_EVIDENCE),
        ("row", ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE),
    ):
        for module, evidence in registry.items():
            if not isinstance(evidence, tuple) or len(evidence) != 2:
                raise ValueError(f"{scope} evidence for {module!r} is malformed")
            source, runner = evidence
            claim = claims.setdefault(
                str(runner), {"modules": set(), "scopes": set(), "sources": set()}
            )
            claim["modules"].add(str(module))
            claim["scopes"].add(scope)
            claim["sources"].add(str(source))

    rows: list[dict[str, object]] = []
    for runner in sorted(claims):
        runner_path = ROOT / runner
        if runner_path.parent != ROOT / "game" / "tests":
            raise ValueError(f"evidence runner escapes game/tests: {runner}")
        if not runner_path.is_file():
            raise FileNotFoundError(f"evidence runner is missing: {runner}")
        sources = sorted(claims[runner]["sources"])
        for source in sources:
            source_path = ROOT / source
            if not source_path.is_file():
                raise FileNotFoundError(f"evidence source is missing: {source}")
        rows.append(
            {
                "runner": runner,
                "runnerSha256": hashlib.sha256(runner_path.read_bytes()).hexdigest(),
                "modules": sorted(claims[runner]["modules"]),
                "scopes": sorted(claims[runner]["scopes"]),
                "sources": sources,
            }
        )
    return {
        "schema": "openbfme.module-runtime-evidence-gate",
        "version": 1,
        "runnerCount": len(rows),
        "runners": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--require-count", type=int)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()
    document = build_manifest()
    if args.require_count is not None and document["runnerCount"] != args.require_count:
        raise SystemExit(
            f"runner count changed: expected {args.require_count}, "
            f"observed {document['runnerCount']}"
        )
    print(json.dumps(document, indent=2 if args.pretty else None, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
