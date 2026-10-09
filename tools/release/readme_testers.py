#!/usr/bin/env python3
"""Lane RELEASE-1: writes README_TESTERS.txt for a test package.

    python3 tools/release/readme_testers.py --version <version> --platform linux|windows [--out FILE]

The known issues come from tools/release/known_issues.json, each item checked against the docs/STOPS.md rows it names (the row's Area is
printed with its ID); a row that is gone fails the run, so the list cannot silently describe issues that were fixed or renumbered.
"""
from __future__ import annotations

import argparse
import json
import sys
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

TEMPLATE = """\
OpenBFME {version} - closed test build for {platform_name}
{rule}

Thank you for testing! OpenBFME is an unofficial, fan-made rebuild of
"The Lord of the Rings: The Battle for Middle-earth II - The Rise of the
Witch-king" (RotWK) 2.01. It is not made or endorsed by EA. This package
contains NO game files: it plays on your own copy of the game.

WHAT YOU NEED
- Your own installs of The Rise of the Witch-king patched to 2.01 AND
  The Battle for Middle-earth II patched to 1.06 (the English version).
  RotWK always needs BFME2 next to it, as in the original.
- {requirements}

STARTING
- {start}
- On the first start OpenBFME looks for your game folders (the registry
  keys the original installers and the All-in-One launcher write{wine}) and
  shows them; confirm them or choose them with "Browse...". It checks
  every game file once (this can take a minute) and remembers the folders.
  To choose again, delete install-paths.cfg in the data folder below.
- OpenBFME only reads your game files, it never changes them. It talks to
  the network only for LAN games you start yourself.

WHAT TO TEST
- Skirmish against the computer: pick a map, a faction and opponents in
  Solo Play > Skirmish and play a whole game.
- LAN games between two or more computers on the same network (the LAN
  entry of the main menu). Everyone needs the same OpenBFME version.
- Tell us what looks, sounds or plays differently from the original game.

HOW TO REPORT
- Post in the OpenBFME Discord, channel #feedback. Include the version
  ({version}) and what you did when it happened.
- Attach the log of that session: every run writes one file to
    {logs}
  named openbfme-<date>-<time>.log. If the game shows an error or crashed
  last time, it tells you which file to attach. Logs do not contain your
  user name or your home folder.
{console}
KNOWN ISSUES
{issues}

FILES
- README_TESTERS.txt  this file
- LICENSE, NOTICE     OpenBFME's licence (GPL-3.0) and notices
- VERSION             the exact build
- SHA256SUMS of the archives are published next to them.
"""

PLATFORMS = {
    "linux": dict(
        platform_name="Linux x86-64",
        requirements="A 64-bit Linux with Vulkan graphics (Mesa or the proprietary drivers). The game folders can be anywhere, e.g. in a Wine or Proton prefix.",
        start="Unpack the archive and run ./OpenBFME.x86_64 (keep the files together).",
        wine=", also inside Wine, Proton (Steam), Lutris, Heroic and Bottles prefixes",
        logs="~/.local/share/godot/app_userdata/OpenBFME/logs/",
        console="- Please attach the log file rather than a copy of the terminal output.\n",
    ),
    "windows": dict(
        platform_name="Windows x86-64",
        requirements="64-bit Windows 10 or 11 with a graphics card that supports Vulkan or Direct3D 12.",
        start="Unpack the archive and run OpenBFME.exe (keep the files together).",
        wine="",
        logs="%APPDATA%\\Godot\\app_userdata\\OpenBFME\\logs\\",
        # the console filter (Common/ConsoleFilter.h) does not exist on Windows: the console wrapper prints unfiltered text (review r2)
        console=("- Attach the log file, NOT a copy of the console window: the console\n"
                 "  (OpenBFME.console.exe) is not filtered and can show your Windows\n"
                 "  user name and folders; the log file never does.\n"),
    ),
}


def stop_rows(stops_md: Path) -> dict[str, str]:
    """ID -> Area of every row of docs/STOPS.md."""
    rows = {}
    for line in stops_md.read_text(encoding="utf-8").splitlines():
        if line.startswith("| S-"):
            cells = [c.strip() for c in line.split(" | ")]
            rows[cells[0][2:]] = cells[2]
    return rows


def known_issues(issues_json: Path, stops_md: Path) -> tuple[list[str], list[str]]:
    """(the formatted issue lines, the errors)."""
    rows = stop_rows(stops_md)
    data = json.loads(issues_json.read_text(encoding="utf-8"))
    lines, errors = [], []
    for item in data["issues"]:
        missing = [s for s in item["stops"] if s not in rows]
        if missing:
            errors.append(f"known issue names stops that are not in {stops_md.name}: {', '.join(missing)} ({item['text'][:60]}...)")
            continue
        refs = "; ".join(f"{s} {rows[s]}" for s in item["stops"])
        body = textwrap.fill(item["text"] + f" [{refs}]", width=72, initial_indent="- ", subsequent_indent="  ")
        lines.append(body)
    return lines, errors


def render(version: str, platform: str, issues: list[str]) -> str:
    p = PLATFORMS[platform]
    title = TEMPLATE.split("\n", 1)[0].format(version=version, platform_name=p["platform_name"])
    return TEMPLATE.format(version=version, rule="=" * len(title), issues="\n".join(issues), **p)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    ap.add_argument("--out")
    ap.add_argument("--issues", default=str(REPO / "tools/release/known_issues.json"))
    ap.add_argument("--stops", default=str(REPO / "docs/STOPS.md"))
    a = ap.parse_args()
    issues, errors = known_issues(Path(a.issues), Path(a.stops))
    if errors:
        for e in errors:
            print("README_TESTERS:", e, file=sys.stderr)
        return 1
    text = render(a.version, a.platform, issues)
    if a.platform == "windows":
        text = text.replace("\n", "\r\n")
    if a.out:
        Path(a.out).write_bytes(text.encode("utf-8"))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
