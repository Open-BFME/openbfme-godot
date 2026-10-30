"""Lane CRASHACT-1: the crash service's client key goes into a package at package time, never into the source.

    python3 tools/release/crash_key.py <staged Godot project folder>...

With CRASH_CLIENT_KEY set (the operator's release.env for the Deck's packages and --verify rebuilds; the GitHub Actions secret
CRASH_CLIENT_KEY for the Actions builds) each project gets res://scripts/crash_client_key.gd (`const KEY := "<key>"`, git-ignored), which
godot/scripts/crash/crash_client.gd (and its launcher copy) signs reports with. Without it nothing is written: that build queues its crash
reports and logs crash.no_client_key, as before; there is no fallback key. A key that is not 64 letters / digits fails the packaging.
Only freshly staged projects (git archive) are written to, so a key never lingers in a working tree. The key is never printed.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ENV = "CRASH_CLIENT_KEY"
FILE = "scripts/crash_client_key.gd"   # under scripts/: a game or launcher pack may hold scripts there (audit_package.py)
KEY = re.compile(r"[A-Za-z0-9]{64}")


def text(key: str) -> str:
    return ("## Generated at package time by tools/release/crash_key.py from CRASH_CLIENT_KEY (lane CRASHACT-1); never committed.\n"
            "extends RefCounted\n\n"
            f'const KEY := "{key}"\n')


def write(project: Path, key: str | None = None) -> str:
    """writes (or, without a key, leaves out) the key script of one staged project; returns the line to log"""
    key = os.environ.get(ENV, "") if key is None else key
    if not (project / "project.godot").is_file():
        raise SystemExit(f"crash_key: {project} is not a Godot project")
    target = project / FILE
    if target.exists():
        raise SystemExit(f"crash_key: {target} already exists (only a freshly staged project is written to)")
    if not key:
        return f"PACKAGE crash reports: no {ENV}: {project.name} queues its crash reports and sends none (crash.no_client_key)"
    if not KEY.fullmatch(key):
        raise SystemExit(f"crash_key: {ENV} is not 64 letters / digits (the crash service's client key)")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text(key), encoding="utf-8")
    return f"PACKAGE crash reports: {project.name} signs them with the crash service key ({ENV})"


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    for folder in argv:
        print(write(Path(folder)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
