#!/usr/bin/env python3
"""The fast gate. Runs on every commit in a few seconds, no model, no network.

Checks, in order:
  1. no retail-format bytes staged outside content/ fixtures
  2. no developer-machine paths in staged text
  3. no file over 1,000 lines grows (.gd .py .cs .ps1)
  4. hub files are locked by this agent when FLEET_AGENT is set
  5. GDScript parse of touched scripts when OPENBFME_GODOT points at a binary
  6. pytest for touched importer test modules

Everything else runs in CI or nightly. Exit non-zero blocks the commit.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import fleetlib as fl

RETAIL_SUFFIXES = {".big", ".w3d", ".dds", ".tga", ".map", ".apt", ".bse", ".wav", ".mp3", ".vp6", ".csf", ".str", ".wnd", ".dat"}
FIXTURE_ROOTS = ("content/", "importer/tests/fixtures/", "docs/assets/", "launcher/OpenBFME.Launcher/Assets/", "game/data/base/assets/ui/")
GROWTH_SUFFIXES = {".gd", ".py", ".cs", ".ps1"}
GROWTH_CAP = 1000
MACHINE_PATH = re.compile(r"[A-Za-z]:\\Users\\|/home/[a-z]|/Users/[A-Z]")


def staged() -> list[str]:
    out = fl.git("diff", "--cached", "--name-only", "--diff-filter=ACMR")
    return [line.replace("\\", "/") for line in out.splitlines() if line]


def staged_text(rel: str) -> str | None:
    result = subprocess.run(["git", "show", f":{rel}"], cwd=fl.REPO, capture_output=True)
    if result.returncode != 0:
        return None
    data = result.stdout
    if b"\x00" in data[:4096]:
        return None
    return data.decode("utf-8", errors="replace")


def head_lines(rel: str) -> int | None:
    result = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=fl.REPO, capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout.count(b"\n")


def check_retail(files: list[str], problems: list[str]) -> None:
    for rel in files:
        if Path(rel).suffix.lower() in RETAIL_SUFFIXES and not rel.startswith(FIXTURE_ROOTS):
            problems.append(f"retail-format file staged: {rel}")


def check_machine_paths(files: list[str], problems: list[str]) -> None:
    for rel in files:
        if rel.startswith(("docs/lessons.md",)):
            continue
        text = staged_text(rel)
        if text and MACHINE_PATH.search(text):
            problems.append(f"developer-machine path in {rel}")


def check_growth(files: list[str], problems: list[str]) -> None:
    for rel in files:
        if Path(rel).suffix.lower() not in GROWTH_SUFFIXES:
            continue
        text = staged_text(rel)
        if text is None:
            continue
        now = text.count("\n")
        before = head_lines(rel)
        if now > GROWTH_CAP and (before is None or now > before):
            problems.append(f"{rel} is {now} lines and grew; files over {GROWTH_CAP} lines may not grow (split it or put the code in a new file)")


def check_locks(files: list[str], problems: list[str]) -> None:
    result = subprocess.run([sys.executable, str(Path(__file__).with_name("lock.py")), "check", *files], cwd=fl.REPO, capture_output=True, text=True)
    if result.returncode != 0:
        problems.append(result.stdout.strip() or "hub lock check failed")


def check_gdscript(files: list[str], problems: list[str]) -> None:
    godot = os.environ.get("OPENBFME_GODOT")
    scripts = [rel for rel in files if rel.endswith(".gd")]
    if not godot or not scripts or not Path(godot).is_file():
        return
    for rel in scripts:
        result = subprocess.run([godot, "--headless", "--path", str(fl.REPO / "game"), "--check-only", "--script", "res://" + rel.removeprefix("game/")],
                                capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            problems.append(f"GDScript parse failed: {rel}\n{result.stderr[-800:]}")


def check_pytest(files: list[str], problems: list[str]) -> None:
    # FLEET_GATE_NO_PYTEST stops recursion: the fleet tests run this gate,
    # and this gate runs the fleet tests when they are staged.
    if os.environ.get("FLEET_GATE_NO_PYTEST"):
        return
    tests = [rel for rel in files if rel.startswith("importer/tests/test_") and rel.endswith(".py")]
    if not tests:
        return
    env = dict(os.environ, PYTHONPATH=str(fl.REPO / "importer"), FLEET_GATE_NO_PYTEST="1")
    pinned = fl.WORKSPACE / "retail-work" / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
    interpreter = str(pinned) if pinned.is_file() else sys.executable
    result = subprocess.run([interpreter, "-m", "pytest", "-q", "-x", "-p", "no:cacheprovider", *tests], cwd=fl.REPO, capture_output=True, text=True, env=env, timeout=600)
    if result.returncode != 0:
        if "No module named pytest" in (result.stderr or ""):
            print(f"warning: pytest not installed for {interpreter}; touched tests not run here, CI runs them")
            return
        problems.append("pytest failed for touched tests:\n" + result.stdout[-1500:])


def main() -> int:
    files = staged()
    if not files:
        return 0
    problems: list[str] = []
    check_retail(files, problems)
    check_machine_paths(files, problems)
    check_growth(files, problems)
    check_locks(files, problems)
    check_gdscript(files, problems)
    check_pytest(files, problems)
    if problems:
        print("FAST GATE FAILED")
        for problem in problems:
            print(" -", problem)
        return 1
    print(f"fast gate ok ({len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
