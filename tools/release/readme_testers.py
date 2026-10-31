#!/usr/bin/env python3
"""Lane RELEASE-1: writes README_TESTERS.txt for a test package.

    python3 tools/release/readme_testers.py --version <version> --platform linux|windows [--out FILE] [--game-only]

Lane ONEZIP-1: the package holds the game and the OpenBFME Launcher; the README says how to start either and how updates work.
--game-only: the text for package.sh --no-launcher's trial package (the game alone, which does not update).

The known issues come from tools/release/known_issues.json, each item checked against the docs/STOPS.md rows it names (the row's Area is
printed with its ID); a row that is gone fails the run, so the list cannot silently describe issues that were fixed or renumbered.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

TEMPLATE = """\
{title}
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
- OpenBFME only reads your game files, it never changes them. The game
  talks to the network only for LAN games you start yourself.
{updates}
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
- {find_logs}
{console}
KNOWN ISSUES
{issues}

FILES
{files}- README_TESTERS.txt  this file
- LICENSE, NOTICE     OpenBFME's licence (GPL-3.0) and notices
- VERSION             the exact build
- SHA256SUMS of the archives are published next to them.
"""

PLATFORMS = {
    "linux": dict(
        platform_name="Linux x86-64",
        requirements="A 64-bit Linux with Vulkan graphics (Mesa or the proprietary drivers). The game folders can be anywhere, e.g. in a Wine or Proton prefix.",
        start="Unpack the archive and run ./OpenBFME.x86_64 (the game) or\n  ./OpenBFMELauncher.x86_64 (the launcher). Keep the files together.",
        start_game_only="Unpack the archive and run ./OpenBFME.x86_64 (keep the files together).",
        game="./OpenBFME.x86_64", launcher="./OpenBFMELauncher.x86_64",
        wine=", also inside Wine, Proton (Steam), Lutris, Heroic and Bottles prefixes",
        logs="~/.local/share/godot/app_userdata/OpenBFME/logs/",
        find_logs="To open that folder: \"Logs\" next to the version in the menus, \"Open log\n  folder\" in the error / crash message, or ./OpenBFME.x86_64 --open-logs",
        console="- Please attach the log file rather than a copy of the terminal output.\n",
    ),
    "windows": dict(
        platform_name="Windows x86-64",
        requirements="64-bit Windows 10 or 11 with a graphics card that supports Vulkan or Direct3D 12.",
        start="Unpack the archive and run OpenBFME.exe (the game) or\n  OpenBFMELauncher.exe (the launcher). Keep the files together.",
        start_game_only="Unpack the archive and run OpenBFME.exe (keep the files together).",
        game="OpenBFME.exe", launcher="OpenBFMELauncher.exe",
        wine="",
        logs="%APPDATA%\\Godot\\app_userdata\\OpenBFME\\logs\\",
        find_logs="To open that folder: \"Logs\" next to the version in the menus, \"Open log\n  folder\" in the error / crash message, or OpenBFME.exe --open-logs",
        # the console filter (Common/ConsoleFilter.h) does not exist on Windows: the console wrapper prints unfiltered text (review r2)
        console=("- Attach the log file, NOT a copy of the console window: the console\n"
                 "  (OpenBFME.console.exe) is not filtered and can show your Windows\n"
                 "  user name and folders; the log file never does.\n"),
    ),
    "macos": dict(
        platform_name="macOS Universal",
        requirements="macOS 11 or newer on Intel, or macOS 13 or newer on Apple silicon.",
        start="Open OpenBFMELauncher.app for the launcher, or OpenBFME.app for the game. The apps are ad-hoc signed, not signed by an identified Apple Developer ID or notarized; if Gatekeeper blocks them, use the documented Open Anyway steps in docs/MACOS.md.",
        start_game_only="Open OpenBFME.app.",
        game="OpenBFME.app", launcher="OpenBFMELauncher.app",
        wine="",
        logs="~/Library/Application Support/Godot/app_userdata/OpenBFME/logs/",
        find_logs="Open the log folder shown in the error message, or use the OpenBFME app's user-data folder.",
        console="",
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


# the areas whose issues read better with their area in front in a flat list (the patch notes group by area instead)
AREA_PREFIX = {"Campaign": "Campaign", "Create-a-Hero": "Create-a-Hero", "Launcher and install": "Install"}


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
        text = item["text"]
        if item.get("area") in AREA_PREFIX and not text.startswith(AREA_PREFIX[item["area"]]):
            text = f"{AREA_PREFIX[item['area']]}: {text[:1].lower()}{text[1:]}"
        body = textwrap.fill(text + f" [{refs}]", width=72, initial_indent="- ", subsequent_indent="  ")
        lines.append(body)
    return lines, errors


UPDATES = """
UPDATES
- Start whichever you like: {game} (the game) or {launcher} (the
  launcher: news, settings, older versions). Both keep OpenBFME up to
  date, through the launcher's updater.
- A newer version is downloaded in the background, checked against the
  signed release, and used from your next start. This folder is never
  changed while you play: updates are kept in the launcher's data folder
  (see README_LAUNCHER.txt), the two newest versions.
- Offline, or when GitHub cannot be reached, the game starts at once with
  what you have and says why in the top right corner of the menu.
- The update check contacts GitHub only (README_LAUNCHER.txt, "What it
  contacts"). To start without it: {game} --no-update-check. To be told
  about updates without downloading them: untick "Update automatically
  in the background" in the launcher's Settings.
"""

MACOS_UPDATES = """
UPDATES
- Automatic downloads and self-updates are not available in this macOS
  package. Keep both app bundles together and replace them together when
  installing a newer package; do not replace an executable inside either app.
"""

GAME_ONLY_UPDATES = """
UPDATES
- This package is the game alone: it does not update itself. Get the
  normal download (the game and the launcher together) to stay up to date.
"""

FILES = """\
- {game:<19} the game
- {launcher:<19} the OpenBFME Launcher
- README_LAUNCHER.txt the launcher's own notes (what it contacts, where it stores files)
"""


def title_of(version: str, platform_name: str) -> str:
    """the first line: "OpenBFME Alpha 0.4.5 for Linux" for a plain release (lane VERSION-1: the owner's plain numbers, 2026-10-14),
    "OpenBFME <version> - alpha build for Linux" for anything else (an alpha build, a preview, a development package)"""
    m = re.fullmatch(r"v((?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8}))", version)
    if m:
        return f"OpenBFME Alpha {m.group(1)} for {platform_name}"
    # lane NIGHTLY-1: "OpenBFME Nightly 0.4.12 (2026-10-22) for Linux"; lane NIGHTLY-2: "OpenBFME Nightly 0.4.13 (2026-10-23, evening) for Linux"
    core = r"v((?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8}))-nightly\."
    n = re.fullmatch(core + r"(20\d\d)(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])", version)
    if n:
        return f"OpenBFME Nightly {n.group(1)} ({n.group(2)}-{n.group(3)}-{n.group(4)}) for {platform_name}"
    n = re.fullmatch(core + r"(2[6-9]|[3-9]\d)(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])([12])00", version)
    if n:
        half = {"1": "morning", "2": "evening"}[n.group(5)]
        return f"OpenBFME Nightly {n.group(1)} (20{n.group(2)}-{n.group(3)}-{n.group(4)}, {half}) for {platform_name}"
    return f"OpenBFME {version} - alpha build for {platform_name}"


def render(version: str, platform: str, issues: list[str], game_only: bool = False) -> str:
    p = dict(PLATFORMS[platform])
    names = {"game": p.pop("game"), "launcher": p.pop("launcher")}
    if game_only:
        p["start"] = p.pop("start_game_only")
    else:
        p.pop("start_game_only")
    title = title_of(version, p["platform_name"])
    updates = GAME_ONLY_UPDATES if game_only else (MACOS_UPDATES if platform == "macos" else UPDATES)
    return TEMPLATE.format(version=version, title=title, rule="=" * len(title), issues="\n".join(issues),
                           updates=updates.format(**names),
                           files="" if game_only else FILES.format(**names), **p)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    ap.add_argument("--out")
    ap.add_argument("--game-only", action="store_true", help="the game alone (package.sh --no-launcher): it does not update")
    ap.add_argument("--issues", default=str(REPO / "tools/release/known_issues.json"))
    ap.add_argument("--stops", default=str(REPO / "docs/STOPS.md"))
    a = ap.parse_args()
    issues, errors = known_issues(Path(a.issues), Path(a.stops))
    if errors:
        for e in errors:
            print("README_TESTERS:", e, file=sys.stderr)
        return 1
    text = render(a.version, a.platform, issues, a.game_only)
    if a.platform == "windows":
        text = text.replace("\n", "\r\n")
    if a.out:
        Path(a.out).write_bytes(text.encode("utf-8"))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
