#!/usr/bin/env python3
"""Commit gate for the rebuild. Two checks that keep a public repo legal:

  1. no retail-format bytes staged (the game's files never enter git), judged by suffix AND by content: a RIFF/WAVE, ID3 or
     MPEG Layer III stream, a BIG archive or a DDS texture is refused under any file name (renaming does not bypass the gate;
     synthetic streams count too: generate them at test time instead)
  2. no developer-machine paths in staged text

Pure renames are skipped: their content already passed this gate. The legacy
gate lives at archive/tools/fleet/precommit.py. Exit non-zero blocks the commit.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RETAIL_SUFFIXES = {".big", ".w3d", ".dds", ".tga", ".map", ".apt", ".bse", ".wav", ".mp3", ".vp6", ".csf", ".str", ".wnd", ".dat"}
FIXTURE_ROOTS = ("archive/content/", "archive/importer/tests/fixtures/", "archive/docs/assets/",
                 "archive/launcher/OpenBFME.Launcher/Assets/", "archive/game/data/base/assets/ui/")
MACHINE_PATH = re.compile(r"[A-Za-z]:\\Users\\|/home/[a-z]|/Users/[A-Z]")


def retail_content(data: bytes) -> str:
    """Names the retail format the leading bytes announce, or ''. Independent of the file name."""
    if data[:4] == b"RIFF" and data[8:12] == b"WAVE":
        return "RIFF/WAVE audio"
    if data[:3] == b"ID3":
        return "ID3-tagged MP3 audio"
    if len(data) >= 4 and data[0] == 0xFF and (data[1] & 0xE0) == 0xE0:
        version, layer = (data[1] >> 3) & 3, (data[1] >> 1) & 3
        if version != 1 and layer == 1 and (data[2] >> 4) != 15 and ((data[2] >> 2) & 3) != 3:
            return "MPEG Layer III audio"
    if data[:4] in (b"BIGF", b"BIG4"):
        return "BIG archive"
    if data[:4] == b"DDS ":
        return "DDS texture"
    return ""


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True)


def staged() -> list[str]:
    out = git("diff", "--cached", "--name-status", "-M", "--diff-filter=ACMR").stdout.decode("utf-8", "replace")
    files = []
    for line in out.splitlines():
        parts = line.split("\t")
        # every destination is checked, pure renames (R100) included: a rename can move bytes out of an exemption or behind another suffix
        files.append(parts[-1].replace("\\", "/"))
    return files


def main() -> int:
    problems = []
    files = staged()
    for rel in files:
        if Path(rel).suffix.lower() in RETAIL_SUFFIXES and not rel.startswith(FIXTURE_ROOTS):
            problems.append(f"retail-format file staged: {rel}")
            continue
        data = git("show", f":{rel}").stdout
        kind = "" if rel.startswith(FIXTURE_ROOTS) else retail_content(data)
        if kind:
            problems.append(f"retail-format bytes staged ({kind}): {rel}")
            continue
        if b"\x00" not in data[:4096] and MACHINE_PATH.search(data.decode("utf-8", "replace")):
            problems.append(f"developer-machine path in {rel}")
    if problems:
        print("COMMIT GATE FAILED")
        for problem in problems:
            print(" -", problem)
        return 1
    print(f"commit gate ok ({len(files)} changed files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
