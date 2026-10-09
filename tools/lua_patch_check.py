#!/usr/bin/env python3
"""Checks the Lua 4.0.1 vendoring of lane LUA-1.

  1. every file of engine/thirdparty/lua-4.0.1/ matches its md5 in engine/thirdparty/lua-4.0.1.manifest (the pristine tree
     is never edited), and nothing is missing or extra;
  2. engine/src/Libraries/Lua/ea-fork.patch is exactly the unified diff of every altered file (a file of the patch layer
     with a pristine counterpart of the same name) against that pristine file.

    python tools/lua_patch_check.py            check, exit 1 on any difference
    python tools/lua_patch_check.py --update   rewrite ea-fork.patch
"""
from __future__ import annotations

import difflib
import hashlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRISTINE = REPO / "engine" / "thirdparty" / "lua-4.0.1"
MANIFEST = REPO / "engine" / "thirdparty" / "lua-4.0.1.manifest"
PATCH_DIR = REPO / "engine" / "src" / "Libraries" / "Lua"
PATCH_FILE = PATCH_DIR / "ea-fork.patch"


def pristine_files() -> dict[str, Path]:
    """name -> path for the sources the build stages (src/, src/lib/, include/)."""
    out: dict[str, Path] = {}
    for sub in ("src", "src/lib", "include"):
        for p in sorted((PRISTINE / sub).iterdir()):
            if p.is_file():
                out[p.name] = p
    return out


def check_manifest() -> list[str]:
    problems: list[str] = []
    expected: dict[str, str] = {}
    for line in MANIFEST.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        digest, name = line.split("  ", 1)
        expected[name] = digest
    actual = {p.relative_to(PRISTINE).as_posix(): p for p in PRISTINE.rglob("*") if p.is_file()}
    for name in sorted(set(expected) - set(actual)):
        problems.append(f"pristine file missing: {name}")
    for name in sorted(set(actual) - set(expected)):
        problems.append(f"file not in the manifest: {name}")
    for name in sorted(set(expected) & set(actual)):
        if hashlib.md5(actual[name].read_bytes()).hexdigest() != expected[name]:
            problems.append(f"pristine file modified: {name}")
    return problems


def build_patch() -> str:
    pristine = pristine_files()
    chunks: list[str] = []
    for p in sorted(PATCH_DIR.iterdir()):
        if not p.is_file() or p.name not in pristine:
            continue
        base = pristine[p.name]
        rel = base.relative_to(PRISTINE).as_posix()
        a = base.read_text(newline="").splitlines(keepends=True)
        b = p.read_text(newline="").splitlines(keepends=True)
        diff = list(difflib.unified_diff(a, b, f"a/lua-4.0.1/{rel}", f"b/lua-4.0.1/{rel}", n=3))
        chunks.append("".join(diff))
    return "".join(chunks)


def main() -> int:
    problems = check_manifest()
    patch = build_patch()
    if "--update" in sys.argv:
        PATCH_FILE.write_text(patch, newline="")
        print(f"wrote {PATCH_FILE.relative_to(REPO)} ({len(patch.splitlines())} lines)")
    elif not PATCH_FILE.exists() or PATCH_FILE.read_text(newline="") != patch:
        problems.append("ea-fork.patch is not the diff of the patch layer against the pristine tree (run with --update)")
    for p in problems:
        print("PROBLEM:", p)
    if not problems:
        print("lua vendoring ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
