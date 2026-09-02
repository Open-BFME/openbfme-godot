#!/usr/bin/env python3
"""Install the fast gate as .git/hooks/pre-commit and a rebase reminder as pre-push.

Run once per clone: python tools/fleet/install_hooks.py
"""
from __future__ import annotations

import os
import stat
import sys
from pathlib import Path

import fleetlib as fl

PRE_COMMIT = """#!/bin/sh
# OpenBFME fast gate (tools/fleet/precommit.py). Never bypass; fix the gate in its own unit.
exec python "$(git rev-parse --show-toplevel)/tools/fleet/precommit.py"
"""

PRE_PUSH = """#!/bin/sh
# Refuse to push a branch that is behind its upstream; rebase first.
upstream=$(git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null) || exit 0
git fetch -q origin 2>/dev/null
behind=$(git rev-list --count HEAD.."$upstream" 2>/dev/null || echo 0)
if [ "$behind" != "0" ]; then
  echo "pre-push: behind $upstream by $behind commit(s); run: git pull --rebase"
  exit 1
fi
exit 0
"""


def main() -> int:
    hooks = fl.REPO / ".git" / "hooks"
    if not hooks.is_dir():
        print("no .git/hooks directory; is this a checkout?")
        return 1
    # Remove hooks from the retired review gate.
    for stale in ("pre-merge-commit", "pre-applypatch", "post-checkout", "post-commit", "post-merge", "openbfme-ponytail.json"):
        path = hooks / stale
        if path.is_file() and ("ponytail" in path.read_text(encoding="utf-8", errors="ignore") or stale.endswith(".json")):
            path.unlink()
    for name, body in (("pre-commit", PRE_COMMIT), ("pre-push", PRE_PUSH)):
        path = hooks / name
        path.write_text(body, encoding="utf-8", newline="\n")
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print(f"installed pre-commit and pre-push under {hooks}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
