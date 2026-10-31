#!/usr/bin/env python3
"""Checks the Luau 0.744 vendoring of lane SDK-2 (engine/cmake/Luau.cmake).

  1. every file of engine/thirdparty/luau-0.744/ matches its md5 in engine/thirdparty/luau-0.744.manifest (the pristine tree is never
     edited), and nothing is missing or extra;
  2. every source of the patch layer engine/src/Libraries/Luau/ that has a pristine counterpart (same relative path) is marked
     ALTERED SOURCE VERSION, and engine/src/Libraries/Luau/openbfme.patch is exactly the unified diff of those files against the pristine
     ones; a source without a counterpart is a new file and must say so ("New file of the Luau patch layer").

    python tools/luau_patch_check.py            check, exit 1 on any difference
    python tools/luau_patch_check.py --update   rewrite openbfme.patch
"""
from __future__ import annotations

import difflib
import hashlib
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRISTINE = REPO / "engine" / "thirdparty" / "luau-0.744"
MANIFEST = REPO / "engine" / "thirdparty" / "luau-0.744.manifest"
PATCH_DIR = REPO / "engine" / "src" / "Libraries" / "Luau"
PATCH_FILE = PATCH_DIR / "openbfme.patch"
SOURCES = (".h", ".cpp")


def read(p: Path) -> str:
    """the file's text with its line endings as they are (Path.read_text(newline=) needs Python 3.13)"""
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


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


def layer() -> list[Path]:
    return sorted(p for p in PATCH_DIR.rglob("*") if p.is_file() and p.suffix in SOURCES)


def build_patch(problems: list[str]) -> str:
    chunks: list[str] = []
    for p in layer():
        rel = p.relative_to(PATCH_DIR).as_posix()
        base = PRISTINE / rel
        text = read(p)
        if not base.is_file():
            if "New file of the Luau patch layer" not in text:
                problems.append(f"{rel}: no pristine counterpart and not marked as a new file")
            continue
        if "ALTERED SOURCE VERSION" not in text.splitlines()[0]:
            problems.append(f"{rel}: altered file without the ALTERED SOURCE VERSION line")
        a = read(base).splitlines(keepends=True)
        b = text.splitlines(keepends=True)
        chunks.append("".join(difflib.unified_diff(a, b, f"a/luau-0.744/{rel}", f"b/luau-0.744/{rel}", n=3)))
    return "".join(chunks)


def main() -> int:
    problems = check_manifest()
    patch = build_patch(problems)
    if "--update" in sys.argv:
        with open(PATCH_FILE, "w", encoding="utf-8", newline="") as f:
            f.write(patch)
        print(f"wrote {PATCH_FILE.relative_to(REPO)} ({len(patch.splitlines())} lines)")
    elif not PATCH_FILE.exists() or read(PATCH_FILE) != patch:
        problems.append("openbfme.patch is not the diff of the patch layer against the pristine tree (run with --update)")
    for p in problems:
        print("PROBLEM:", p)
    if not problems:
        print("luau vendoring ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
