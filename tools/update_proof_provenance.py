#!/usr/bin/env python3
"""Refresh deterministic hashes for the declared legal-safe proof bundle."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath


REPO_ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ROOT = REPO_ROOT / "content" / "openbfme-test"
PACK_PATH = BUNDLE_ROOT / "pack.json"


def canonical_bytes(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8-sig")
    return text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def checked_path(relative: str) -> Path:
    pure = PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts or pure.suffix.lower() != ".json":
        raise ValueError(f"unsafe proof-bundle path: {relative}")
    resolved = (BUNDLE_ROOT / Path(*pure.parts)).resolve()
    resolved.relative_to(BUNDLE_ROOT.resolve())
    if not resolved.is_file():
        raise FileNotFoundError(relative)
    return resolved


def main() -> None:
    pack = json.loads(PACK_PATH.read_text(encoding="utf-8"))
    provenance_relative = str(pack["files"]["provenance"])
    declared = ["pack.json", *map(str, pack["files"].values())]
    if len(declared) != len(set(declared)):
        raise ValueError("pack.json declares duplicate bundle paths")

    entries: list[dict[str, object]] = []
    for relative in declared:
        if relative == provenance_relative:
            continue
        payload = canonical_bytes(checked_path(relative))
        entries.append(
            {
                "path": relative,
                "canonicalBytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )

    provenance_path = checked_path(provenance_relative)
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    provenance["files"] = entries
    provenance_path.write_text(
        json.dumps(provenance, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"PROOF_PROVENANCE_UPDATED files={len(entries)} path={provenance_path}")


if __name__ == "__main__":
    main()
