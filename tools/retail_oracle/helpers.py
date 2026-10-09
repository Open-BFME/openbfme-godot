"""Staleness of the helper executables the tests run (manifest: helpers.json).

The engine and retail_oracle tests drive prebuilt helpers (retail_oracle.exe, x87_oracle.exe and drivers that
link engine sources). A helper older than its inputs gives answers for code that no longer exists and fails
with cryptic protocol errors ("hardware ERR"). `require_fresh` turns that into one clear failure.

    python tools/retail_oracle/helpers.py [name ...]     prints every stale helper, exit 1 if any

helper_stale.ps1 applies the same rule inside build.bat so fresh helpers are not rebuilt.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "helpers.json"
NOT_BUILT = "not built"


def _inputs(base: Path, rel: str):
    p = (base / rel).resolve()
    if p.is_dir():
        files = [f for f in p.rglob("*") if f.is_file()]
        if not files:
            raise FileNotFoundError(f"helper input directory '{rel}' holds no files (nothing to compare against)")
        return files
    if p.is_file():
        return [p]
    raise FileNotFoundError(f"helper input '{rel}' does not exist (manifest typo or deleted source)")


def stale_reason(name: str, manifest: Path = MANIFEST) -> str | None:
    """Why helper `name` must be rebuilt, or None if it is fresh. Raises on a bad manifest or input path."""
    doc = json.loads(manifest.read_text(encoding="utf-8"))
    if name not in doc["helpers"]:
        raise KeyError(f"unknown helper '{name}' in {manifest}")
    h = doc["helpers"][name]
    base = manifest.parent
    exe = (base / h["exe"]).resolve()
    # Resolve every declared input first, so a typo or a deleted source is an error whether or not the exe
    # exists and whatever HELPERS_FORCE says.
    inputs = [f for rel in h["inputs"] for f in _inputs(base, rel)]
    if not inputs:
        raise FileNotFoundError(f"helper '{name}' declares no inputs in {manifest}")
    if os.environ.get("HELPERS_FORCE"):
        return "HELPERS_FORCE is set"
    if not exe.is_file():
        return NOT_BUILT
    t = exe.stat().st_mtime_ns
    for f in inputs:
        if f.stat().st_mtime_ns > t:
            return f"{f.name} is newer than the exe"
    return None


def stale_message(name: str, manifest: Path = MANIFEST) -> str | None:
    reason = stale_reason(name, manifest)
    if reason is None:
        return None
    rebuild = json.loads(manifest.read_text(encoding="utf-8"))["rebuild"]
    return f"stale helper: {name} ({reason}); rebuild with {rebuild}"


def require_fresh(name: str, manifest: Path = MANIFEST) -> None:
    """Fail the calling test (or collection) with the rebuild command when the helper exists but is out of
    date. A missing exe is the caller's own loud skip, not handled here."""
    import pytest

    if stale_reason(name, manifest) == NOT_BUILT:
        return
    msg = stale_message(name, manifest)
    if msg is not None:
        pytest.fail(msg, pytrace=False)


def main(argv: list[str]) -> int:
    names = argv or list(json.loads(MANIFEST.read_text(encoding="utf-8"))["helpers"])
    bad = [m for m in (stale_message(n) for n in names) if m]
    for m in bad:
        print(m)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
