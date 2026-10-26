#!/usr/bin/env python3
"""Lane AUTOREL-1: the decisions of tools/release/autorelease.sh that are worth testing on their own (docs/RELEASE.md, "Automatic preview
releases").

    python3 tools/release/autorelease.py next-version [--remote origin] [--bump patch|minor] [--git-dir DIR]
        prints the next plain release number (lane VERSION-1, owner 2026-10-14: "Alpha 0.4.5", then 0.4.6, ...): the highest plain tag
        v<x>.<y>.<z> at or above FIRST_PLAIN on the remote (git ls-remote) with its last number + 1 (--bump minor: the middle number + 1
        and the last 0, for a feature the owner calls huge); FIRST_PLAIN (v0.4.5; v0.5.0 with --bump minor) when there is none. Tags
        outside the launcher's release grammar (make_manifest.TAG: no leading zeros, digits only) are ignored; a version that is not
        above every release tag on the remote is refused (the launchers install only a newer version).
    python3 tools/release/autorelease.py release-version --repo OWNER/NAME --sha SHA [--bump patch|minor] [--remote origin] [--gh gh]
        [--git-dir DIR]
        the version autorelease.sh releases: the next plain release (release_version(); VERSION-1: no v0.3.0-preview.<n> bridge any more,
        the launchers up to v0.3.0-preview.4 read plain tags).
    python3 tools/release/autorelease.py intent-version V [LAST]   exit 0 when an open intent's V is a plain release, an alpha build or
        a bridge above every released or tagged version of its line
    python3 tools/release/autorelease.py title V       the GitHub release's name ("OpenBFME Alpha 0.4.5", release_title())
    python3 tools/release/autorelease.py auto-publish V   exit 0 when V is a plain alpha-stage release (v0.4.5 to below v1.0.0), which
        publish_release.sh --confirm-tag publishes without the typed tag (auto_publish_ok())
    python3 tools/release/autorelease.py newer A B     exit 0 when release version A is above B in the launcher's order, else 1
    python3 tools/release/autorelease.py changed --from A --to B [--git-dir DIR]
        exit 0 when something under RELEASE_PATHS changed between the two commits, 1 when nothing did (the release is skipped).
    python3 tools/release/autorelease.py notes --from A --to B --version V [--known-issues FILE] [--commit SHA] [--git-dir DIR]
        --out FILE [--title-out FILE] [--items-out FILE] [--repo OWNER/NAME] [--titles bridge|studio]
        the release notes for players from the first-parent subjects of A..B (see release_notes()).

Release notes (patch notes in a studio's shape, lane NOTES-1). Every first-parent commit of A..B is one candidate (on the rebuild's
branch a lane lands as one merge commit):
  * a `Release-note: [Kind [Area]:] text` line in its message is used as written. Kind is Added, Changed, Fixed (or `Fix:`) or Removed;
    Area is one of AREAS in brackets, e.g. `Release-note: Fixed [Interface]: A left click on empty ground deselects.`; without a kind
    the line is Added (Fixed when its words say so, FIXED); without an area it goes under "General". `Release-note: none` drops the
    commit. One line is one change: autorelease.sh --plan warns by name about a line that looks like several (note_warnings()).
  * an integration round's lane merge ("Merge lane/x (<sha>) into integ/pN: ...") gives no note: the round's release-notes commit
    carries the player lines;
  * otherwise a commit that changes nothing a player runs (PLAYER_PATHS minus tests) is dropped, and the subject is reduced to plain
    words: the lane id, parenthesised details (binary addresses, stop ids, review verdicts) and everything after the first ';' are cut;
    a long first clause keeps only its head before ':'. Subjects that still read as lane jargon after that are dropped.
The page: title, date and build kind, the install sentence, the download table (download_section()), then the sections Added, Changed,
Bug fixes, Removed, Known issues (empty ones left out), their bullets grouped under a bold area line in AREAS' order, "General" last.
Every line (generated, `Release-note:` text, known issues) is neutralised before it reaches GitHub (Sol r1: injection through subjects):
links and URLs are removed, @mentions and #references are written with full-width signs (GitHub links neither), control characters go;
the Markdown notes and PR body also escape every Markdown / HTML special character (neutral(), markdown()).

    python3 tools/release/autorelease.py audit-tree --tree TREE [--git-dir DIR]
        the public sync's privacy audit, before anything is pushed (audit_tree()): only PUBLIC_TOP entries at the top, no workspace /
        archive / reference folder anywhere, no file a .gitignore rule of the tree ignores (tracked or not), and every file passes the
        commit gate's rules (tools/precommit.py check_file: retail suffixes, retail-format bytes, developer-machine paths), also inside
        containers (r3, Sol r2: a gzip-wrapped BIG passed): every file goes through audit_package.Audit (Godot packs, ZIP, gzip, bzip2,
        xz, tar, opened recursively to MAX_DEPTH and MAX_UNPACKED; encrypted, truncated, too deep, too large or unopenable kinds such as
        7z / RAR are findings) plus raw zlib streams (SyncAudit).
"""
from __future__ import annotations

import argparse
import json
from collections.abc import Iterator
import os
import re
import subprocess
import sys
import tempfile
import hashlib
import io
import struct
import tarfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from make_manifest import KINDS, PLATFORMS, TAG, _N, asset_name, channel_of, version_key  # noqa: E402  (the launcher's grammar)
import ed25519  # noqa: E402
sys.path.insert(0, str(HERE.parent))
from precommit import check_file  # noqa: E402  (the commit gate's per-file rules)
import audit_package  # noqa: E402  (the package audit: containers opened recursively, fail closed)
import rulescan  # noqa: E402  (PRIVSPEED-1: the rules' scans, exact and fast)
import zlib  # noqa: E402
import contextlib  # noqa: E402
from concurrent.futures import FIRST_COMPLETED, Future, ProcessPoolExecutor, wait  # noqa: E402

# what a release is made of: a change anywhere else (docs, roadmap, other tools) does not make a new release
RELEASE_PATHS = ("engine", "godot", "launcher", "tools/release")
# what a player runs: the notes only describe commits that change these (tests excluded)
PLAYER_PATHS = ("engine/", "godot/", "launcher/")
TEST_PATHS = ("engine/tests/", "godot/tests/", "launcher/tests/")

LANE_ID = re.compile(r"^(?:[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+|[A-Z][a-z]+\d+)(?: r\d+)?(?: \([^()]*\))?:?\s+")
JARGON = re.compile(r"\bRW\b|0x[0-9A-Fa-f]{3,}|\bS-\d+|\bFB-\d+|\bSol\b|\b[A-Z]{2,}[0-9]*-[0-9A-Z]+\b|#\d|\br\d+\b|\bstack\d+\b|"
                    r"\bpins?\b|\bgate\b|\.(?:cpp|h|py|sh|gd|md)\b|\b[a-z]+[A-Z][A-Za-z]*(?:'s)?\b")
FIXED = re.compile(r"\b(?:fix(?:es|ed)?|regression|crash(?:es|ed)?|no longer|stops?\b|instead of|bugs?|broken|wrong)\b", re.I)
# an integration round's lane merge, "Merge lane/x (<sha>) into integ/p3: ...": its words are the integrator's summary for
# reviewers; the round's player notes are its release-notes commit's `Release-note:` lines (preview.3 printed "Ce7997fd into integ/p3: ...").
# INTEG-5: any merge into an integ/ branch is one too, also "Merge lane/x (LANE-1: ...) into integ/p5" and git's own "Merge branch 'lane/x' into
# integ/p5" (they would have printed "Max zoom-out, ... into integ/p5.")
INTEG_MERGE = re.compile(r"^Merge \S+ \([0-9a-f]{7,40}\) into \S+|^Merge .* into integ/\S+", re.I)
SKIP_START = re.compile(r"^(?:batch\b|roadmap\b|docs?\b|merge\b)", re.I)
MAX_ITEM = 140
# GHPR-1: the community pull request flow's public files: the contributor guide and GitHub's folder (the PR template and the public PR
# workflow, .github/workflows/pr.yml); every file in them passes the same audit as the rest of the tree
GHPR_TOP = frozenset({".github", "CONTRIBUTING.md"})
# the public main's top level (the sync recipe: the archive sha's tree without archive/, plus the README line; a PRIVACY.md is not carried any more, PR #34); anything else is refused. ONLINE-2: services/
# (the online relay and lobby service source) is public by the owner's decision of 2026-10-11; its secrets come from the environment at deploy time
PUBLIC_TOP = frozenset({".gitattributes", ".gitignore", ".gitmodules", "CLAUDE.md", "LICENSE", "NOTICE", "README.md",
                        "build.bat", "docs", "engine", "godot", "launcher", "pytest.ini", "run_tests.bat", "services", "tools"} | GHPR_TOP)
PRIVATE_DIRS = frozenset({"workspace", "archive", "reference", "references"})
URL = re.compile(r"(?i)\b[a-z][a-z0-9+.-]*://\S*|\bwww\.\S+|\bmailto:\S+")
# characters that could start emphasis, code or a link in Markdown: replaced by plain look-alikes, never escaped with a backslash (the launcher
# shows the notes as text, so "original\." would show its backslash)
MD_PLAIN = str.maketrans({"\\": "/", "`": "'", "*": "\u2217", "[": "(", "]": ")", "|": "/", "~": "\u223c"})


def git(git_dir: str | None, *args: str) -> str:
    cmd = ["git"] + (["-C", git_dir] if git_dir else []) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


# ---- version --------------------------------------------------------------------------------------------------------------------------
# Lane VERSION-1 (owner, 2026-10-14: "instead of alpha 0.3.x preview 1, just alpha 0.3.x <number>; make a major version number when you
# get a huge new feature"): releases are plain numbers, v<x>.<y>.<z>, shown as "OpenBFME Alpha 0.4.5". Each release takes the next last
# number; --bump minor (the owner's call for a huge feature) raises the middle one. The first is v0.4.5: the count goes on from
# v0.4.0-alpha.4, and SemVer orders every v0.4.0-alpha.<n> below it (a pre-release is below its release, v0.4.0 < v0.4.5). A plain tag is
# on the launcher's "stable" channel (semver.gd channel_of), so it is published as a full release, not a GitHub pre-release: the manifest's
# channel must match the release's pre-release flag (manifest.gd), and a launcher on either channel offers a full release (in_channel).
FIRST_PLAIN = "v0.4.5"
BUMPS = ("patch", "minor")
STAGE = "Alpha"   # the word before a plain number in what players see ("OpenBFME Alpha 0.4.5"); also launcher semver.gd and godot release.gd


def _plain_core(version: str) -> tuple[int, int, int] | None:
    m = TAG.fullmatch(version)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m and m.group(4) is None else None


def next_version(tag_names: list[str], bump: str = "patch") -> str:
    """the next plain release (see the comment above): 1 + the last number of the highest plain tag at or above FIRST_PLAIN among
    tag_names (ls-remote names, refs/tags/ and ^{} allowed), or the middle number + 1 with bump="minor"; FIRST_PLAIN (bump="minor": its
    middle number + 1) when there is none. Refused when the result is not above every release tag in tag_names (the launcher installs only
    a newer version automatically)."""
    if bump not in BUMPS:
        raise SystemExit(f"autorelease: --bump {bump!r} is not one of {', '.join(BUMPS)}")
    tags = [n for n in _tag_names(tag_names) if TAG.fullmatch(n) and len(n) <= 40]
    plain = [n for n in tags if _plain_core(n) and version_key(n) >= version_key(FIRST_PLAIN)]
    if plain:
        x, y, z = _plain_core(max(plain, key=version_key))
        version = f"v{x}.{y}.{z + 1}" if bump == "patch" else f"v{x}.{y + 1}.0"
    else:
        x, y, z = _plain_core(FIRST_PLAIN)
        version = FIRST_PLAIN if bump == "patch" else f"v{x}.{y + 1}.0"
    newer = sorted({t for t in tags if version_key(t) >= version_key(version)}, key=version_key)
    if newer:
        raise SystemExit(f"autorelease: {version} would not be above the release tag {newer[-1]} (the launcher's order: a pre-release "
                         f"below its release, alpha below preview): installed launchers would never offer it")
    return version


def remote_tags(git_dir: str | None, remote: str) -> list[str]:
    """every tag on the remote (next_version compares the new version with all of them)"""
    out = git(git_dir, "ls-remote", "--tags", remote, "refs/tags/v*")
    return [line.split("\t", 1)[1] for line in out.splitlines() if "\t" in line]


def auto_publish_ok(version: str) -> bool:
    """VERSION-1: a plain release of the alpha stage (line 'plain', major 0) is what the automatic releases make under the owner's scheme
    (2026-10-14), so publish_release.sh --confirm-tag takes it as it takes a pre-release; v1.0.0 and later wait for the owner's typed tag"""
    m = TAG.fullmatch(version)
    return bool(m) and line_of(version) == "plain" and m.group(1) == "0"


def display_name(version: str) -> str:
    """what players read for a version: "Alpha 0.4.5" for a plain release (VERSION-1), "v0.4.0 Alpha 1" for an alpha build (RELNAME-1);
    a preview keeps its tag, as the earlier previews were named"""
    m = TAG.fullmatch(version)
    if not m or len(version) > 40:
        raise SystemExit(f"autorelease: {version!r} is not a release version")
    if m.group(4) is None:
        return f"{STAGE} {m.group(1)}.{m.group(2)}.{m.group(3)}"
    if m.group(4) == "alpha":
        return f"v{m.group(1)}.{m.group(2)}.{m.group(3)} Alpha {m.group(5)}"
    if m.group(4) == "nightly":
        return f"Nightly {m.group(1)}.{m.group(2)}.{m.group(3)} ({nightly_when(version)})"
    return version


# ---- the nightly builds (lane NIGHTLY-1, owner 2026-10-21: "build the pulled in PRs every night with patch notes and what to test ... stable vs
# nightly ... but the game is still in alpha"). A nightly is a GitHub pre-release v<x>.<y>.<z>-nightly.<n>: <x>.<y>.<z> is the next
# plain release (next_version), so SemVer puts it below that release and above the last one, and the launcher's pre-release channel (id
# "preview", shown as "Nightly") offers it. Launchers older than NIGHTLY-1 do not parse the word and skip it.
# NIGHTLY-1 made one a UTC date, n = YYYYMMDD (v0.4.12-nightly.20261022). Lane NIGHTLY-2 (owner, 2026-10-23: "i would like the nightly to run
# twice a day on actions"): two a UTC day, n = <YYMMDD><s>00, 9 digits (the launcher's limit): s = 1 for the morning build (made 00:00-11:59
# UTC, the 07:00Z run), 2 for the evening one (12:00-23:59, the 19:00Z run): v0.4.12-nightly.261023100, v0.4.12-nightly.261023200. Every
# 9-digit number is above every 8-digit date, so numbers sort in run order across the change. The final "00" keeps the launchers already
# shipped from showing a wrong date: NIGHTLY-1's semver.gd nightly_text() reads the last two digits as the day, and 00 is no day, so they
# show "Nightly 0.4.12 build 261023200" (test_nightly.py checks that for every date). One nightly per half day (slot).
NIGHTLY_DATE = re.compile(r"(20\d{2})(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])")
NIGHTLY_SLOT = re.compile(r"(2[6-9]|[3-9]\d)(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])([12])00")
SLOT_NAMES = {"1": "morning", "2": "evening"}
NIGHTLY_CLOCK = re.compile(r"(20\d{2})(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])([01]\d|2[0-3])")


def nightly_parts(version: str) -> tuple[str, str]:
    """(YYYY-MM-DD, "" for a NIGHTLY-1 nightly of a date or "morning" / "evening" for a NIGHTLY-2 one) of a nightly version; an error for
    anything else"""
    m = TAG.fullmatch(version)
    n = m.group(5) if m and m.group(4) == "nightly" and len(version) <= 40 else ""
    if d := NIGHTLY_DATE.fullmatch(n):
        return f"{d.group(1)}-{d.group(2)}-{d.group(3)}", ""
    if d := NIGHTLY_SLOT.fullmatch(n):
        return f"20{d.group(1)}-{d.group(2)}-{d.group(3)}", SLOT_NAMES[d.group(4)]
    raise SystemExit(f"autorelease: {version!r} is not a nightly version (v<x>.<y>.<z>-nightly.<YYMMDD><1|2>00, or an earlier "
                     f"-nightly.<YYYYMMDD>)")


def nightly_date(version: str) -> str:
    """the date of a nightly version, YYYY-MM-DD (an error for anything else)"""
    return nightly_parts(version)[0]


def nightly_when(version: str) -> str:
    """what players read for a nightly's build: "2026-10-23, evening" (NIGHTLY-2), "2026-10-22" (NIGHTLY-1)"""
    date, slot = nightly_parts(version)
    return f"{date}, {slot}" if slot else date


def nightly_number(clock: str) -> str:
    """the nightly number of a run started at `clock` (UTC YYYYMMDDHH): <YYMMDD><1 before 12:00, else 2>00"""
    c = NIGHTLY_CLOCK.fullmatch(clock)
    if not c or not NIGHTLY_SLOT.fullmatch(n := f"{c.group(1)[2:]}{c.group(2)}{c.group(3)}{1 if int(c.group(4)) < 12 else 2}00"):
        raise SystemExit(f"autorelease: {clock!r} is not a UTC time YYYYMMDDHH from 2026 on")
    return n


def nightly_version(tag_names: list[str], clock: str) -> str:
    """the nightly of a run started at `clock` (UTC YYYYMMDDHH): the next plain release's numbers with -nightly.<nightly_number>; refused
    when a release tag is at or above it (a nightly of this half day or a later one exists: one nightly per half day, never one below an
    earlier)"""
    version = f"{next_version(tag_names)}-nightly.{nightly_number(clock)}"
    tags = [n for n in _tag_names(tag_names) if TAG.fullmatch(n) and len(n) <= 40]
    newer = sorted({t for t in tags if version_key(t) >= version_key(version)}, key=version_key)
    if newer:
        raise SystemExit(f"autorelease: the nightly {version} would not be above the release tag {newer[-1]} (one nightly per half day, "
                         f"morning and evening UTC)")
    return version


def release_title(version: str) -> str:
    """the GitHub release's name: "OpenBFME Alpha 0.4.5" (VERSION-1), "OpenBFME v0.4.0 Alpha 1" for an alpha build (RELNAME-1), the plain
    tag for a preview"""
    return f"OpenBFME {display_name(version)}"


# ---- the bridge for the launchers up to v0.3.0-preview.4 (RELNAME-1 r2) ---------------------------------------------------------------
# Every launcher published before the alpha builds (v0.3.0-preview.1 to .4: two launcher codebases, the archive's 42f8a8d2 for preview.1
# and .2, 2a422ce6 for .3 and .4) reads the release list the same way (updater.gd fetch_releases): ONE request,
# repos/<repo>/releases?per_page=30, no further page; tags outside `v<x>.<y>.<z>[-preview.<n>]` are skipped (so every -alpha release);
# the newest remaining tag of the channel (preview: all of them) is installed and its launcher staged (stage_self_update, applied at
# the next start). Such a launcher reaches the alpha builds only through a release it can parse whose launcher knows -alpha: a bridge,
# v0.3.0-preview.<n>, from a sha whose launcher/scripts/core/semver.gd takes -alpha. A bridge works only while it is on that first page
# (the API's order, whatever GitHub sorts by: this check reads the same list), so every automatic release first looks at the page:
# when the newest tag an old launcher can parse there is not a verified bridge, or it is within BRIDGE_MARGIN entries of the page's end,
# this release IS the next bridge; otherwise it is the next alpha build. A bridge sorts below every alpha build, so an alpha-aware
# launcher never installs it; an old one installs its game and updates itself, then offers the newest alpha build.
BRIDGE_BASE = "v0.3.0"
OLD_TAG = re.compile(rf"v({_N})\.({_N})\.({_N})(?:-preview\.({_N}))?")   # the release grammar of the launchers up to v0.3.0-preview.4
OLD_PAGE = 30
BRIDGE_MARGIN = 5


def _tag_names(tag_names: list[str]) -> list[str]:
    return [n.removeprefix("refs/tags/").removesuffix("^{}") for n in tag_names]


# Sol r2: a word in the source proves nothing ("# alpha" after the old pattern passed); the launcher's semver.gd itself is run in Godot
# on sample tags: it must take the alpha builds, refuse what the grammar refuses, order them after v0.3.0-preview.4 and put them on the
# pre-release channel "preview"
_ALPHA_PROBE = """extends SceneTree
func _init() -> void:
	quit.call_deferred(0)  # also when a check below stops on a script error (no verdict then: NO ALPHA)
	print("SEMVER PROBE START")
	var S = load("res://semver.gd")
	var ok: bool = S != null and S.can_instantiate()  # a script that does not parse loads, but cannot be used
	for t in ["v0.4.0-alpha.1", "v0.4.0-alpha.10", "v0.3.0-preview.5", "v0.3.0-preview.4", "v0.4.0", "v1.2.3"]:
		ok = ok and S.is_valid(t)
	for t in ["v0.4.0-alpha.01", "v0.4.0-beta.1", "v0.4.0-alpha", "v0.4.0-ALPHA.1", "v0.4.0-alpha.1 ", "v0.4.0-alpha.1.2", "v0.4.0-alphaa.1"]:
		ok = ok and not S.is_valid(t)
	ok = ok and S.compare("v0.3.0-preview.4", "v0.4.0-alpha.1") < 0 and S.compare("v0.4.0-alpha.1", "v0.4.0-alpha.2") < 0
	ok = ok and S.compare("v0.4.0-alpha.2", "v0.4.0-alpha.10") < 0 and S.compare("v0.4.0-alpha.9", "v0.4.0") < 0
	ok = ok and S.compare("v0.4.0-alpha.3", "v0.3.0-preview.9") > 0 and S.compare("v0.4.0-alpha.1", "v0.4.0-alpha.1") == 0
	ok = ok and S.channel_of("v0.4.0-alpha.1") == "preview" and S.channel_of("v0.3.0-preview.5") == "preview"
	print("SEMVER PROBE ", "ALPHA" if ok else "NO ALPHA")
"""


def knows_alpha(semver_gd: str) -> bool:
    """the launcher's release grammar (launcher/scripts/core/semver.gd), run in Godot ($GODOT, 4.7.2) on sample tags, takes and orders
    the alpha builds (_ALPHA_PROBE). A Godot that does not run is an error (fail closed), never a "yes"."""
    with tempfile.TemporaryDirectory() as d:
        Path(d, "project.godot").write_text('config_version=5\n\n[application]\n\nconfig/name="semver probe"\n')
        Path(d, "semver.gd").write_text(semver_gd)
        Path(d, "probe.gd").write_text(_ALPHA_PROBE)
        env = dict(os.environ, XDG_DATA_HOME=str(Path(d, "data")), XDG_CONFIG_HOME=str(Path(d, "config")), XDG_CACHE_HOME=str(Path(d, "cache")))
        try:
            r = subprocess.run([os.environ.get("GODOT", "godot"), "--headless", "--path", d, "--script", "res://probe.gd"],
                               capture_output=True, text=True, timeout=180, env=env, stdin=subprocess.DEVNULL)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise SystemExit(f"autorelease: Godot ($GODOT) could not run the launcher's semver.gd ({e}): the alpha check fails closed")
        # a semver.gd that does not parse or stops on an error gives no "ALPHA" verdict; no START at all: Godot did not run the probe
        if "SEMVER PROBE START" not in r.stdout:
            raise SystemExit(f"autorelease: Godot ($GODOT) did not run the semver probe (fail closed): {(r.stdout + r.stderr)[-500:]}")
        return "SEMVER PROBE ALPHA" in r.stdout


def next_bridge(tag_names: list[str]) -> str:
    """v0.3.0-preview.<1 + the highest such tag>; refused unless it is above every tag the old launchers can parse"""
    names = _tag_names(tag_names)
    high = max((int(TAG.fullmatch(n).group(5)) for n in names if TAG.fullmatch(n) and n.startswith(BRIDGE_BASE + "-preview.")), default=0)
    version = f"{BRIDGE_BASE}-preview.{high + 1}"
    above = sorted((n for n in names if OLD_TAG.fullmatch(n) and version_key(n) >= version_key(version)), key=version_key)
    if above:
        raise SystemExit(f"autorelease: the bridge {version} would not be above {above[-1]}: the launchers up to v0.3.0-preview.4 would "
                         f"not install it")
    return version


def bridge_check(releases: list[dict], verify, margin: int = BRIDGE_MARGIN) -> tuple[str, str]:
    """('', tag) when the release list (the API's order, newest first, drafts included as the API gives them to the owner) shows the
    launchers up to v0.3.0-preview.4 a verified bridge with room for BRIDGE_MARGIN more releases; else (why a bridge is needed, tag).
    verify(release) -> '' or why the release is not a working bridge."""
    page = [r for r in releases if not r.get("draft")][:OLD_PAGE]   # what an anonymous ?per_page=30 request gets
    old = [(i, r) for i, r in enumerate(page) if OLD_TAG.fullmatch(str(r.get("tag_name", "")))]
    if not old:
        return f"no release on the first {OLD_PAGE} an old launcher can read", ""
    i, top = max(old, key=lambda x: version_key(x[1]["tag_name"]))
    tag = top["tag_name"]
    why = verify(top)
    if why:
        return f"the newest release an old launcher installs, {tag}, is not a bridge: {why}", tag
    if i >= OLD_PAGE - margin:
        return f"the bridge {tag} is entry {i + 1} of the {OLD_PAGE} an old launcher reads: a new bridge keeps it on the page", tag
    return "", tag


def line_of(version: str) -> str:
    """'plain' (v<x>.<y>.<z> from FIRST_PLAIN on, VERSION-1), 'alpha' (v0.4.0-alpha.<n>: an intent opened before VERSION-1), 'bridge'
    (v0.3.0-preview.<n>) or '' for a version automatic releases never make"""
    m = TAG.fullmatch(version)
    if m and len(version) <= 40 and m.group(4) is None and version_key(version) >= version_key(FIRST_PLAIN):
        return "plain"
    if m and m.group(4) == "alpha" and version.startswith("v0.4.0-alpha."):
        return "alpha"
    if m and m.group(4) == "preview" and version.startswith(BRIDGE_BASE + "-preview."):
        return "bridge"
    if m and len(version) <= 40 and m.group(4) == "nightly" and (NIGHTLY_DATE.fullmatch(m.group(5)) or NIGHTLY_SLOT.fullmatch(m.group(5))):
        return "nightly"
    return ""


def intent_version_ok(version: str, last: str, tag_names: list[str] = ()) -> bool:
    """an open intent's version: of one of the lines (line_of), and above every version of its line already released or tagged (the last
    released one and every tag on the remote but its own: Sol r2, after alpha.1 and a bridge an intent edited to alpha.0 was published;
    an alpha build and a bridge are on separate lines, so alpha.3 then the bridge preview.6 is forward). VERSION-1: once a plain release
    is released or tagged, only a plain version is (the alpha builds and the bridges are over: one sorts below every plain release)."""
    line = line_of(version)
    if not line:
        return False
    known = [n for n in _tag_names(list(tag_names)) if n != version] + ([last] if last else [])
    # NIGHTLY-1: a resumed nightly is above every release tag but its own, whatever line (a plain release tagged meanwhile ends it)
    if line == "nightly":
        return not any(TAG.fullmatch(n) and len(n) <= 40 and version_key(n) >= version_key(version) for n in known)
    if line != "plain" and any(line_of(n) == "plain" for n in known):
        return False
    # Sol (VERSION-1 review): a resumed plain release must still be above EVERY valid release tag but its own reserved one, whatever
    # line they are on (v0.4.5 interrupted, v0.5.0-alpha.1 tagged meanwhile: resuming v0.4.5 would publish below it)
    if line == "plain" and any(TAG.fullmatch(n) and len(n) <= 40 and version_key(n) >= version_key(version) for n in known):
        return False
    older = [n for n in _tag_names(list(tag_names)) if n != version and line_of(n) == line] + ([last] if line_of(last) == line else [])
    return all(version_key(version) > version_key(n) for n in older)


# ---- the formats the published launchers read (VERSION-1, Sol) ---------------------------------------------------------------------------
FORMAT_LINE = re.compile(r"^const (?:FORMATS := \[([0-9, ]+)\]|FORMAT := ([0-9]+))\s*(?:#.*)?$", re.M)


def launcher_formats(manifest_gd: str) -> set[int]:
    """the manifest formats a launcher's launcher/scripts/core/manifest.gd reads (`const FORMATS := [2, 3]` or `const FORMAT := 2`);
    a source with neither is an error (fail closed)"""
    found = FORMAT_LINE.findall(manifest_gd)
    if len(found) != 1:
        raise SystemExit("autorelease: cannot tell which manifest formats a launcher reads (no single FORMATS / FORMAT constant)")
    many, one = found[0]
    return {int(x) for x in many.split(",")} if many else {int(one)}


# legacy-needed's "no published launcher needs the launcher archives": not 1, which every uncaught error exits with too (PUBLISH-2 r4: a
# remote that could not be listed crashed with 1 and autorelease.sh took it for "not needed")
LEGACY_NOT_NEEDED = 10


def format2_only_launchers(git_dir: str | None, tag_names: list[str]) -> list[str]:
    """the release tags whose launcher reads manifest format 2 only: published launchers that cannot take a format 3 release
    (no launcher archives). Derived from each tag's own manifest.gd; a release tag whose source is not in the repository is an error."""
    out = []
    for name in sorted({n for n in _tag_names(tag_names) if TAG.fullmatch(n) and len(n) <= 40}, key=version_key):
        try:
            src = git(git_dir, "show", f"{name}:launcher/scripts/core/manifest.gd")
        except subprocess.CalledProcessError:
            raise SystemExit(f"autorelease: the launcher of the release tag {name} is not in this repository, so the formats it reads are unknown "
                             f"(fail closed): fetch the tags")
        if launcher_formats(src) == {2}:
            out.append(name)
    return out


EXE = {"game": {"linux-x64": "OpenBFME.x86_64", "windows-x64": "OpenBFME.exe"},
       "launcher": {"linux-x64": "OpenBFMELauncher.x86_64", "windows-x64": "OpenBFMELauncher.exe"}}


def manifest_problems(m, tag: str, repo: str, prerelease) -> list[str]:
    """the signed manifest of a bridge, checked as the launchers check it (launcher/scripts/core/manifest.gd _schema and verify): the
    keys, format 2, this tag and repository, the channel of the tag and the release's pre-release flag (Sol r2: a release flagged
    stable is refused by every launcher), every kind and platform with its asset name, size, SHA-256 and file list naming the exe"""
    if not isinstance(m, dict):
        return ["the manifest is not a JSON object"]
    p = []
    if sorted(m) != sorted(["format", "product", "repo", "version", "channel", "date", "commit", "assets"]):
        return [f"the manifest's keys are {sorted(m)}"]
    if m["format"] != 2 or m["product"] != "OpenBFME":
        p.append(f"format {m['format']!r} / product {m['product']!r}")
    if m["version"] != tag or m["repo"] != repo:
        p.append(f"it names {m['version']!r} of {m['repo']!r}, not {tag} of {repo}")
    if not TAG.fullmatch(str(m["version"])) or m["channel"] != channel_of(str(m["version"])):
        p.append(f"channel {m['channel']!r} for {m['version']!r}")
    if prerelease is not (m["channel"] == "preview"):
        p.append(f"the release's pre-release flag {prerelease!r} does not match the channel {m['channel']!r}")
    if not re.fullmatch(r"20\d\d-\d\d-\d\d", str(m["date"])) or not re.fullmatch(r"[0-9a-f]{40}", str(m["commit"])):
        p.append(f"date {m['date']!r} / commit {m['commit']!r}")
    a = m["assets"]
    if not isinstance(a, dict) or sorted(a) != sorted(KINDS):
        return p + ["its assets are not the game and the launcher"]
    for kind in KINDS:
        if not isinstance(a[kind], dict) or sorted(a[kind]) != sorted(PLATFORMS):
            p.append(f"assets.{kind} are not {list(PLATFORMS)}")
            continue
        for platform in PLATFORMS:
            x = a[kind][platform]
            w = f"assets.{kind}.{platform}"
            if not isinstance(x, dict) or sorted(x) != ["files", "name", "sha256", "size"]:
                p.append(f"{w} is not {{name, size, sha256, files}}")
                continue
            if x["name"] != asset_name(kind, tag, platform):
                p.append(f"{w} is named {x['name']!r}")
            if not isinstance(x["size"], int) or isinstance(x["size"], bool) or x["size"] <= 0 or not re.fullmatch(r"[0-9a-f]{64}", str(x["sha256"])):
                p.append(f"{w} has no size and SHA-256")
            f = x["files"]
            if not isinstance(f, dict) or EXE[kind][platform] not in f or not all(
                    isinstance(v, dict) and isinstance(v.get("size"), int) and re.fullmatch(r"[0-9a-f]{64}", str(v.get("sha256", "")))
                    for v in f.values()):
                p.append(f"{w} lists no {EXE[kind][platform]} with its size and SHA-256")
    return p


class Live:
    """the reads the release-line decision makes: GitHub through `gh api` (fail closed), the public main through git"""

    def __init__(self, gh: str, target: str, git_dir: str | None, remote: str, key: bytes):
        self.gh, self.target, self.git_dir, self.remote, self.key = gh, target, git_dir, remote, key

    def api(self, *args: str) -> bytes:
        r = subprocess.run([self.gh, "api", *args], capture_output=True)
        if r.returncode:
            raise SystemExit(f"autorelease: gh api {' '.join(args)} failed (fail closed): {r.stderr.decode(errors='replace').strip()}")
        return r.stdout

    def releases(self) -> list[dict]:
        rel = json.loads(self.api(f"repos/{self.target}/releases?per_page=100"))
        if not isinstance(rel, list) or (len(rel) == 100 and sum(not r.get("draft") for r in rel) < OLD_PAGE):
            raise SystemExit("autorelease: the release list does not show the first page an old launcher reads (fail closed)")
        return rel

    def verify(self, release: dict) -> str:
        assets = {a.get("name"): a for a in release.get("assets") or []}
        if release.get("draft") or not all(n in assets for n in ("manifest.json", "manifest.json.sig")):
            return "it has no signed manifest"
        data = {n: self.api("-H", "Accept: application/octet-stream", f"repos/{self.target}/releases/assets/{assets[n]['id']}")
                for n in ("manifest.json", "manifest.json.sig")}
        if not ed25519.verify(self.key, data["manifest.json"], data["manifest.json.sig"]):
            return "its manifest's signature does not verify under launcher/release_key.pub"
        try:
            m = json.loads(data["manifest.json"])
        except ValueError:
            return "its manifest is not JSON"
        why = manifest_problems(m, release["tag_name"], self.target, release.get("prerelease"))
        if why:
            return "its manifest: " + "; ".join(why)
        # Sol r2: the downloads the old launchers make must be there: both platforms' game and launcher archives, uploaded, with the
        # signed size; the launcher archives (what the bridge is for) also with the signed SHA-256, from GitHub's digest or downloaded
        for kind in KINDS:
            for platform in PLATFORMS:
                x = m["assets"][kind][platform]
                got = assets.get(x["name"])
                if not got or got.get("state", "uploaded") != "uploaded" or got.get("size") != x["size"]:
                    return f"its {x['name']} is missing or not the signed size"
                digest = got.get("digest") or ""
                if digest and digest != "sha256:" + x["sha256"]:
                    return f"its {x['name']} is not the signed one (digest {digest})"
                if kind == "launcher" and not digest and \
                        hashlib.sha256(self.api("-H", "Accept: application/octet-stream",
                                                f"repos/{self.target}/releases/assets/{got['id']}")).hexdigest() != x["sha256"]:
                    return f"its {x['name']} is not the signed one"
        c = m["commit"]
        main = f"refs/remotes/{self.remote}/main"
        if subprocess.run(["git", *(["-C", self.git_dir] if self.git_dir else []), "merge-base", "--is-ancestor", c, main],
                          capture_output=True).returncode:
            return f"its commit {c[:10]} is not on {self.remote}/main"
        if not knows_alpha(git(self.git_dir, "show", f"{c}:launcher/scripts/core/semver.gd")):
            return f"the launcher of its commit {c[:10]} does not take the alpha builds (its semver.gd, run on sample tags)"
        return ""


def release_version(tags: list[str], bump: str = "patch") -> tuple[str, str]:
    """(the version of this automatic release, why): the next plain release (next_version, `bump`). Lane VERSION-1: no bridge any more.
    The launchers up to v0.3.0-preview.4 read plain tags (OLD_TAG) and a plain release is the newest entry of the page they read, so a
    plain release that carries the launcher archives (manifest format 2) is what they install and update themselves from; a
    v0.3.0-preview.<n> bridge would sort below every plain release and never be installed. (The bridge rule, bridge_check(), still guards
    an alpha build resumed from an intent opened before VERSION-1: autorelease.sh bridge-ok.)"""
    return next_version(tags, bump), "a plain release: every published launcher reads it (VERSION-1)"


# ---- skip-when-unchanged --------------------------------------------------------------------------------------------------------------
def release_changed(git_dir: str | None, a: str, b: str) -> list[str]:
    """the files under RELEASE_PATHS that differ between commits a and b"""
    return [p for p in git(git_dir, "diff", "--name-only", a, b, "--", *RELEASE_PATHS).splitlines() if p]


# ---- release notes --------------------------------------------------------------------------------------------------------------------
def _strip_parens(s: str) -> str:
    out, depth = [], 0
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def _top_level_split(s: str, sep: str) -> str:
    depth = 0
    for i, ch in enumerate(s):
        depth += ch == "("
        depth -= ch == ")"
        if ch == sep and depth == 0:
            return s[:i]
    return s


def _unwrap(s: str) -> str:
    """'(text)' -> 'text' when the parentheses enclose all of s"""
    s = s.strip()
    while s.startswith("("):
        depth = 0
        for i, ch in enumerate(s):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0:
                break
        if i != len(s) - 1:
            break
        s = s[1:-1].strip()
    return s


def player_line(subject: str) -> str | None:
    """a commit subject in plain words, or None when nothing a player would understand is left"""
    s = subject.strip()
    if INTEG_MERGE.match(s):
        return None
    if s.startswith("Merge "):
        i = s.find("(")
        if i < 0:
            return None
        s = s[i:]
        if s.endswith(")") and s.count("(") == s.count(")"):
            s = _unwrap(s) if _unwrap(s) != s else s[1:-1]
        else:
            s = s[1:]
    for _ in range(3):
        s = _unwrap(LANE_ID.sub("", _unwrap(s), count=1))
    if SKIP_START.match(s):
        return None
    s = _top_level_split(s, ";")
    s = re.sub(r"\s+([,.:])", r"\1", re.sub(r"\s+", " ", _strip_parens(s))).strip(" ,.:")
    head, colon, tail = s.partition(": ")
    if colon and JARGON.search(head) and tail:
        s, head, colon, tail = tail, *tail.partition(": ")
    if len(s) > MAX_ITEM and colon:
        s = head
    s = s.strip(" ,.:")
    if not s or JARGON.search(s) or len(s) > MAX_ITEM or len(s.split()) < 3:
        return None
    return s[0].upper() + s[1:] + "."


def neutral(text: str) -> str:
    """text that GitHub turns into no link, mention or reference: URLs removed, @ and # full-width, control characters and line breaks
    gone (Sol r1: a subject or Release-note line could ping users, link issues or carry links into the release page)"""
    text = URL.sub("(link removed)", text)
    text = "".join(ch if ch.isprintable() else " " for ch in text)
    text = text.replace("@", "\uff20").replace("#", "\uff03")
    text = re.sub(r"\b(GH)-(?=\d)", "\\1\u2011", text, flags=re.I)        # GH-123 is an issue reference too
    return re.sub(r"\s+", " ", text).strip()


def markdown(text: str) -> str:
    """a neutral() line as literal Markdown text with no backslash escapes: HTML entities for & < >, look-alikes for the characters that
    could start emphasis, code, a link or a table cell (MD_PLAIN)"""
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return text.translate(MD_PLAIN)


# the patch notes' vocabulary (lane NOTES-1, owner 2026-10-13): kinds in page order with their section titles, the areas in page order
NOTE_KINDS = (("added", "Added"), ("changed", "Changed"), ("fixed", "Bug fixes"), ("removed", "Removed"))
AREAS = ("Gameplay", "Units and heroes", "Buildings", "AI", "Campaign", "Create-a-Hero", "Interface", "Audio", "Graphics", "Performance",
         "Online", "Saves", "Replays and LAN", "Launcher and install", "Linux")
GENERAL = "General"                      # a line without an area: printed last
# Installed launchers (up to v0.4.0-alpha.3) find the Play card's lines by the section titles "What's new", then "What's fixed"
# (launcher/scripts/ui/play_screen.gd), and the News screen prints every "#" title as it reads it. The first release with the studio
# shape keeps those two titles ("bridge"): a launcher that has not updated itself yet still shows its Play card. Once a launcher that
# reads both sets (this lane's release_notes.gd) has shipped, switch DEFAULT_TITLES to "studio" (docs/RELEASE.md, "Patch notes").
TITLES = {"studio": {"added": "Added", "changed": "Changed", "fixed": "Bug fixes", "removed": "Removed"},
          "bridge": {"added": "What's new", "changed": "Changed", "fixed": "What's fixed", "removed": "Removed"}}
DEFAULT_TITLES = "bridge"
NOTE_HEAD = re.compile(r"^(Added|Changed|Fixed|Fix|Removed)\s*(?:\[([^\]\n]*)\])?\s*:\s*(.*)$", re.I | re.S)
MAX_NOTE = 200                           # a longer line is probably several changes (note_warnings)
DEFAULT_REPO = "Open-BFME/openbfme-godot"


def _cap(t: str) -> str:
    return t[:1].upper() + t[1:]


def parse_note(text: str) -> tuple[str, str, str]:
    """(kind, area, text) of one `Release-note:` text: `Kind [Area]: text`, `Fix: text` or a bare text (Added, or Fixed when its words say
    so). area is the AREAS entry the brackets name (case-insensitive) or "" for none / an unknown name (note_warnings names it)."""
    m = NOTE_HEAD.match(text.strip())
    if not m:
        t = text.strip()
        return ("fixed" if FIXED.search(t) else "added"), "", _cap(t)
    kind = {"fix": "fixed", "fixed": "fixed"}.get(m.group(1).lower(), m.group(1).lower())
    area = next((x for x in AREAS if x.lower() == (m.group(2) or "").strip().lower()), "")
    return kind, area, _cap(m.group(3).strip())


def note_warnings(text: str) -> list[str]:
    """why a `Release-note:` text should be fixed before the release (never an error): it joins several changes, or names an unknown area.
    PUBLISH-2 r3 (review F2): the reasons quote nothing of the note (a warning of a `Release-note: none` commit, or one printed by --plan,
    has passed no metadata audit)"""
    out, m = [], NOTE_HEAD.match(text.strip())
    if m and m.group(2) is not None and m.group(2).strip().lower() not in {x.lower() for x in AREAS}:
        out.append(f"unknown area in brackets (it goes under {GENERAL}; areas: {', '.join(AREAS)})")
    body = m.group(3) if m else text
    if re.search(r";\s*\S", body):
        out.append("a ';' joins clauses: one note, one change")
    if len(body.strip()) > MAX_NOTE:
        out.append(f"{len(body.strip())} characters (more than {MAX_NOTE}): probably several changes in one line")
    return out


def release_note_texts(body: str) -> list[str]:
    return [m.group(1).strip() for m in (re.match(r"^Release-note:\s*(.*)$", ln.strip(), re.I) for ln in body.splitlines()) if m]


def notes_of(subject: str, body: str, files: list[str]) -> list[tuple[str, str, str]]:
    """Every (kind, area, line) of one first-parent commit. A commit with several `Release-note:` lines gives one note per line (a
    release-notes commit lists a whole build's news this way); `Release-note: none` anywhere drops the commit; without any
    Release-note line it is note_of's single generated line, if any."""
    texts = release_note_texts(body)
    if not texts:
        n = note_of(subject, body, files)
        return [n] if n else []
    if any(not t or t.lower() == "none" for t in texts):
        return []
    return [parse_note(t) for t in texts]


def note_of(subject: str, body: str, files: list[str]) -> tuple[str, str, str] | None:
    """(kind, area, line) of one first-parent commit; None when it is not news for a player. A generated line has no area and no kind
    but added / fixed (the subject has no tags: the words decide)."""
    texts = release_note_texts(body)
    if texts:
        if not texts[0] or texts[0].lower() == "none":
            return None
        return parse_note(texts[0])
    if not any(f.startswith(PLAYER_PATHS) and not f.startswith(TEST_PATHS) for f in files):
        return None
    line = player_line(subject)
    if line is None:
        return None
    return ("fixed" if FIXED.search(line) else "added"), "", line


def release_warnings(commit: dict) -> list[str]:
    """`NOTES WARNING <sha> note <n>: <reason>` for each Release-note line of the commit that note_warnings() questions. PUBLISH-2 r3
    (review F2): the commit and the note's number name it, nothing of its text is printed"""
    return [f"NOTES WARNING {commit['sha'][:10]} note {n}: {w}" for n, t in enumerate(release_note_texts(commit.get("body", "")), 1)
            for w in note_warnings(t)]


def first_parent_commits(git_dir: str | None, a: str, b: str) -> list[dict]:
    out = git(git_dir, "log", "--first-parent", "--reverse", "--format=%H%x1f%s%x1f%b%x1e", f"{a}..{b}")
    commits = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if not rec:
            continue
        sha, subject, body = rec.split("\x1f", 2)
        files = git(git_dir, "diff", "--name-only", f"{sha}^1", sha).splitlines()
        commits.append({"sha": sha, "subject": subject, "body": body, "files": files})
    return commits


REPO_NAME = re.compile(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}")


def download_section(version: str, repo: str = DEFAULT_REPO, legacy_launchers: bool = True, mac: bool = False,
                     mac_manifest: bool = False) -> list[str]:
    """Lane ONEZIP-1: the notes' "Which file to download" part: one archive per platform with a direct link, and one line for the files
    nobody downloads by hand. Its links are built here from the release's own version and repository (both checked against their
    grammar) and never pass neutral(), which removes every URL from commit text; nothing else of the notes may hold a link."""
    if not TAG.fullmatch(version) or len(version) > 40:
        raise SystemExit(f"autorelease: {version!r} is not a release version")
    if not REPO_NAME.fullmatch(repo):
        raise SystemExit(f"autorelease: {repo!r} is not <owner>/<name>")
    base = f"https://github.com/{repo}/releases/download/{version}"
    what = "The game and the launcher in one download. Unpack it and start OpenBFME or the OpenBFME Launcher: both keep it up to date."
    out = ["## Which file to download", "", "| File | What it is |", "|---|---|"]
    for platform, label in (("windows-x64", "Windows"), ("linux-x64", "Linux")):
        name = asset_name("game", version, platform)
        out.append(f"| **[{name}]({base}/{name})** | **OpenBFME for {label}.** {what} |")
    rest = [f"SHA256SUMS-{version}.txt", "manifest.json", "manifest.json.sig"]
    if mac:
        # MACREL-1 (owner, 2026-10-15/16): Apple Silicon only, macOS 13 or later, an unstable preview; the apps are ad-hoc signed, not notarized
        name = f"openbfme-{version}-macos-arm64.zip"
        out.append(f"| **[{name}]({base}/{name})** | **OpenBFME for Mac (Apple Silicon, macOS 13 or later), an unstable preview.** The game "
                   "and the launcher in one download. Unzip it, then open each app the first time with right-click > Open (they are not "
                   "notarized). The launcher keeps it up to date. Intel Macs are not supported. |")
    if mac or mac_manifest:   # a release without the Mac package (OPENBFME_RELEASE_SKIP_MAC=1) still carries its signed no-entry sidecar
        rest += ["manifest-macos.json", "manifest-macos.json.sig"]
    if legacy_launchers:
        rest = [asset_name("launcher", version, p) for p in ("windows-x64", "linux-x64")] + rest
    out.append(f"| {', '.join(rest)} | Used by OpenBFME to update itself and for checking downloads. You don't need these. |")
    return out + ["", "Already have OpenBFME or its launcher? Start it: it updates itself.", ""]


def _date_text(date: str) -> str:
    """2026-10-13 -> "October 13, 2026" ("" when it is not such a date)"""
    m = re.fullmatch(r"(\d{4})-(\d{2})-(\d{2})", date)
    months = ("January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December")
    if not m or not 1 <= int(m.group(2)) <= 12 or not 1 <= int(m.group(3)) <= 31:
        return ""
    return f"{months[int(m.group(2)) - 1]} {int(m.group(3))}, {m.group(1)}"


def _grouped(items: list[tuple[str, str]]) -> list[str]:
    """Markdown lines of (area, neutral line) pairs: a bold area line for each area in AREAS' order, General last, its bullets under it"""
    out = []
    for area in AREAS + (GENERAL,):
        mine = [t for a, t in items if (a if a in AREAS else GENERAL) == area]
        if mine:
            out += [f"**{markdown(neutral(area))}**"] + [f"- {markdown(t)}" for t in mine] + [""]
    return out


def release_notes(version: str, commits: list[dict], known_issues: list, commit: str = "", date: str = "", repo: str = DEFAULT_REPO,
                  titles: str = DEFAULT_TITLES, legacy_launchers: bool = True, mac: bool = False,
                  mac_manifest: bool = False) -> tuple[str, list[str]]:
    """(markdown notes, the neutral() lines in order). known_issues: texts, or {"text", "area"} entries. `repo`: the release's repository,
    for the "Which file to download" part (repo="" leaves it out: tests of the items alone)"""
    if titles not in TITLES:
        raise SystemExit(f"autorelease: --titles {titles!r} is not one of {', '.join(TITLES)}")
    by_kind: dict[str, list[tuple[str, str]]] = {k: [] for k, _ in NOTE_KINDS}
    seen = set()
    for c in commits:
        for kind, area, text in notes_of(c["subject"], c.get("body", ""), c.get("files", [])):
            line = neutral(text)
            if line and line not in seen:
                seen.add(line)
                by_kind[kind].append((area, line))
    m = TAG.fullmatch(version)
    build = {None: "alpha build for testers", "alpha": "alpha build for testers", "preview": "alpha build for testers, numbered as a preview so that launchers of "
             "v0.3.0-preview.4 and older can update themselves: after it, they offer the alpha builds"}.get(m.group(4), "build") if m else "build"
    when = _date_text(date)
    # version (TAG grammar, checked by download_section), date (_date_text: digits and a month name), build words and area names (AREAS)
    # are closed sets; the area and section labels go through markdown(neutral()) all the same (structural, not luck)
    # VERSION-1: "OpenBFME Alpha 0.4.5 patch notes"; the alpha builds and previews keep their tag here
    out = [f"# OpenBFME {display_name(version) if m and m.group(4) is None else version} patch notes", "", f"{when} \u00b7 {build}" if when else build, "",
           "Install or update it with the OpenBFME Launcher. It uses your installed The Rise of the Witch-king 2.01 and "
           "The Battle for Middle-earth II 1.06, or downloads them for you.", ""]   # said once, not preachy (owner, 2026-10-10/11)
    if repo:
        out += download_section(version, repo, legacy_launchers, mac, mac_manifest)
    for kind, _ in NOTE_KINDS:
        if by_kind[kind]:
            out += [f"## {markdown(neutral(TITLES[titles][kind]))}", ""] + _grouped(by_kind[kind])
    if not any(by_kind.values()):
        out += ["No changes you will notice in the game: this build updates the release tooling and internal parts.", ""]
    issues = [("", neutral(i)) if isinstance(i, str) else (i.get("area", ""), neutral(i["text"])) for i in known_issues]
    if issues:
        out += ["## Known issues", ""] + _grouped(issues)
    if commit:
        out += [f"Built from commit {markdown(neutral(commit[:10]))}" + (f" ({markdown(neutral(date))})." if date else ".")]
    return "\n".join(out).rstrip() + "\n", [t for k, _ in NOTE_KINDS for _, t in by_kind[k]]


def title_summary(lines: list[str], limit: int = 90) -> str:
    """a short plain-language summary of the first lines (each one's first clause, up to 8 words), for the sync PR's title"""
    parts = []
    for line in lines:
        short = re.split(r"[:,;]", line.rstrip("."), maxsplit=1)[0].strip()
        if len(short.split()) > 8:
            continue
        if short[:1].isupper() and not short[1:2].isupper():
            short = short[0].lower() + short[1:]
        if len(", ".join(parts + [short])) > limit:
            break
        parts.append(short)
    return ", ".join(parts) if parts else "internal changes"


# ---- the public sync's privacy audit --------------------------------------------------------------------------------------------------
TEMPLATES = HERE / "godot_templates.txt"


def _sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_templates(folder: Path | None, pins: Path | None = None) -> dict[str, tuple[Path, str]]:
    """{sha256: (file, executable name)} of the Godot export templates tools/release/godot_templates.txt pins (`<sha256>  <file name>
    <executable name>  <reason>`) that are in `folder` with that sha256 (a file there with another sha256 is not one). PUBLISH-2 r3: the
    release's game executables are these templates byte for byte and the launcher's are one with its pack added: upstream bytes, which the
    private rules' short words match by chance (3-letter words in Godot's locale tables), so the asset audit does not scan a template's
    own bytes. r4 (Sol r3 P2): only in the executable the template is pinned for (its file name), and only where an export may change it
    (export_region)."""
    out: dict[str, tuple[Path, str]] = {}
    pins = pins or TEMPLATES
    for n, line in enumerate(pins.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.fullmatch(r"([0-9a-f]{64})  ([\w.-]+)  ([\w.-]+)  (\S.*)", line)
        if not m:
            raise SystemExit(f"autorelease: {pins.name}:{n} is not `<sha256>  <file name>  <executable name>  <reason>`")
        f = (folder / m.group(2)) if folder else None
        if f and f.is_file() and _sha256_of(f) == m.group(1):
            out[m.group(1)] = (f, m.group(3))   # read only when its executable has its size (content_hits), and checked again then
    return out


def export_region(t: bytes) -> tuple[int, int] | None:
    """(start, end) of the bytes of a Godot export template that exporting with an embedded pack may change: an ELF's section header
    table (Godot 4.7.2 sets its `pck` section's offset and size there), a PE's headers up to the first section's data (the section table:
    v0.4.6's OpenBFMELauncher.exe differs from windows_release_x86_64.exe in its first 872 bytes only); None for any other file. Measured on
    the v0.4.6 launchers: OpenBFMELauncher.x86_64 differs from linux_release.x86_64 in 7 bytes of the section header table."""
    if t[:6] == b"\x7fELF\x02\x01" and len(t) >= 64:
        shoff, = struct.unpack_from("<Q", t, 0x28)
        shentsize, shnum = struct.unpack_from("<HH", t, 0x3A)
        end = shoff + shentsize * shnum
        return (shoff, end) if 64 <= shoff < end <= len(t) else None
    if t[:2] == b"MZ" and len(t) >= 0x40:
        pe, = struct.unpack_from("<I", t, 0x3C)
        if t[pe:pe + 4] == b"PE\0\0" and pe + 24 + 64 <= len(t):
            size, = struct.unpack_from("<I", t, pe + 24 + 60)   # the optional header's SizeOfHeaders
            return (0, size) if pe < size <= len(t) else None
    return None


def content_hits(name: str, data: bytes, rules, templates: dict[str, tuple[Path, str]] | None = None) -> list[str]:
    """the rules' hits in one file or member's content, which every publishing audit checks (the tree's files, the decoded members, the
    release assets). Text as text (every form); a text file by its name that is not UTF-8 is refused (Sol r2 P2: an undecodable .txt member
    passed); a container layer that the audit decodes member by member by its metadata only (its member names are checked as names, its
    members as content): ZIP local headers and central directory, tar headers, the gzip header's fields; an executable that is a pinned
    Godot template (templates) for its executable is upstream bytes, one with a pack appended is checked in its export_region() and in its
    pack's members; every other binary through binary_texts()."""
    if name.endswith(".import") and data.endswith(b"\0"):
        data = data[:-1]   # Godot 4.7.2's export ends an exported .import file with one NUL byte (v0.4.7's pack)
    if Path(re.split(r":|\[", name)[-1].lower()).suffix in TEXT_SUFFIXES and not _is_text(data):
        return [f"a text file that is not UTF-8 without NUL bytes (an encoding the audit does not read): {sanitize(name, rules)}"]
    if _is_text(data):
        return denylist_hits(name, data.decode("utf-8"), rules, check_path=False)
    meta = None
    if audit_package.stream_kind(data) == "gzip":
        meta = gzip_header_fields(data)
    elif audit_package.stream_kind(data) or data[:4] == audit_package.PCK_MAGIC:
        meta = b""   # bzip2 / xz carry no names; a pack's directory holds only the entry names (checked as names) and zero padding
    elif audit_package.is_tar(data):
        meta = tar_headers(data)
    elif zipfile.is_zipfile(io.BytesIO(data)):
        meta = zip_metadata(data)
    if meta is not None:
        return denylist_hits(name, None, rules, check_path=False, data=meta) if meta else []
    # r4 (Sol r3 P2: a template matched by size alone exempted unchanged blocks of any same-sized file and split a string across changed
    # ones): a template is only used for the executable it is pinned for, with the same image size, unchanged outside export_region(),
    # whose bytes are then scanned as one piece (a changed byte keeps its neighbours); anything else is scanned whole
    leaf = re.split(r":|\[", name)[-1].rsplit("/", 1)[-1]
    base = audit_package.embedded_pck_base(data)
    image = data[:base] if base is not None else data
    for sha, (f, exe) in (templates or {}).items():
        if exe != leaf or f.stat().st_size != len(image):
            continue
        t = f.read_bytes()
        if hashlib.sha256(t).hexdigest() != sha:   # the file is no longer the pinned template
            continue
        if image == t:
            return []
        region = export_region(t)
        if region and image[:region[0]] == t[:region[0]] and image[region[1]:] == t[region[1]:]:
            image = image[region[0]:region[1]]
        break
    return denylist_hits(name, None, rules, check_path=False, data=image)


def gzip_headers(data: bytes) -> list[tuple[int, bytes]]:
    """[(flags, fields)] of every member of a gzip stream (RFC 1952: concatenated members are one stream; PUBLISH-2 r4, Sol r3 P2: a host
    name in the second member's file name passed, only the first header was read). fields: the modification time, XFL and OS bytes, the
    extra field, the file name and the comment. A member that does not end, or bytes after the last one that are no member, end the list
    (the package audit refuses them: decompress_members)."""
    out, pos = [], 0
    while pos + 10 <= len(data) and data[pos:pos + 2] == b"\x1f\x8b":
        flags, head, fields = data[pos + 3], pos + 10, data[pos + 4:pos + 10] + b"\0"
        if flags & 4:
            n = int.from_bytes(data[head:head + 2], "little")
            fields += data[head + 2:head + 2 + n] + b"\0"
            head += 2 + n
        for bit in (8, 16):
            if flags & bit:
                end = data.find(b"\0", head)
                end = len(data) if end < 0 else end
                fields += data[head:end] + b"\0"
                head = end + 1
        out.append((flags, fields))
        d = zlib.decompressobj(31)   # where the member ends: its input fed in pieces, its output dropped
        nxt = None
        for i in range(pos, len(data), 1 << 16):
            d.decompress(data[i:i + (1 << 16)])
            if d.eof:
                nxt = min(len(data), i + (1 << 16)) - len(d.unused_data)
                break
        if nxt is None:
            break
        pos = nxt
    return out


def gzip_header_fields(data: bytes) -> bytes:
    """every gzip member's header fields (gzip_headers), as the private rules see them"""
    return b"".join(f for _, f in gzip_headers(data))


def tar_headers(data: bytes) -> bytes:
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as t:
        return b"\0".join(data[m.offset:m.offset_data] for m in t.getmembers())


def zip_metadata(data: bytes) -> bytes:
    """a ZIP without its members' compressed data: local headers, data descriptors, the central directory, its end record and comment"""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        cut = []
        for i in z.infolist():
            n, m = struct.unpack_from("<HH", data, i.header_offset + 26)
            start = i.header_offset + 30 + n + m
            cut.append((start, start + i.compress_size))
    out, cur = b"", 0
    for a, b in sorted(cut):
        out += data[cur:a] + b"\0"
        cur = max(cur, b)
    return out + data[cur:]


class SyncAudit(audit_package.Audit):
    """audit_package's recursive audit plus raw zlib streams (no gzip header), which the package audit does not open. Every decoded
    layer goes back through this blob() (r4, Sol r3: a twice-zlib-wrapped BIG passed because the decoded layer skipped the zlib check),
    and so do the gzip / bzip2 / xz / zip / tar / pack members the package audit opens (it recurses through self.blob). Bounds:
    MAX_DEPTH layers, MAX_UNPACKED bytes per layer; anything beyond a bound is a finding. A binary file with a valid zlib header must
    decode to its end, or it is a finding (fail closed); text with a zlib-looking start ("x^...") is decoded when it can be, and is
    text otherwise. PUBLISH-2 r2 (Sol r1 P2): every decoded member (its name and its content_hits()) also passes the rules; r3: with
    top=True the file itself too (a release asset), and templates (load_templates) for the Godot executables."""

    def __init__(self, rules=(), top: bool = False, templates: dict[str, tuple[Path, str]] | None = None) -> None:
        super().__init__()
        self.rules = list(rules)
        self.top = top
        self.templates = templates or {}
        self.payload_tar_containers: set[str] = set()

    def _rules(self, name: str, data: bytes | None, depth: int) -> None:
        if (depth > 0 or self.top) and self.rules:
            self.problems += denylist_hits(name, None, self.rules)
            if data is not None:
                self.problems += content_hits(name, data, self.rules, self.templates)

    commit_gate_paths = True   # the public source tree: the commit gate's developer-path rule (audit_package.Audit, DEVPORT-6)

    def blob(self, name: str, data: bytes, depth: int = 0) -> None:
        if len(data) >= 2 and data[0] & 0x0F == 8 and data[0] >> 4 <= 7 and (data[0] << 8 | data[1]) % 31 == 0:
            text = _is_text(data)
            if depth >= audit_package.MAX_DEPTH:
                self.problems.append(f"containers nested deeper than {audit_package.MAX_DEPTH}: {name}")
                return
            d = zlib.decompressobj()
            problem = ""
            try:
                out = d.decompress(data, audit_package.MAX_UNPACKED + 1)
                if len(out) > audit_package.MAX_UNPACKED or d.unconsumed_tail:
                    problem = f"{name}: zlib data expands beyond {audit_package.MAX_UNPACKED} bytes"
                elif not d.eof or d.unused_data:
                    problem = f"{name}: truncated zlib stream or bytes after it"
            except zlib.error as e:
                problem = f"{name}: unreadable zlib data ({e})"
            if not problem:
                self._rules(name, None, depth)   # the compressed bytes are no text: the decoded layer is checked
                self.files += 1
                self.blob(f"{name}[zlib]", out, depth + 1)
                return
            if not text and not is_detached_signature(name, data, depth, name.rsplit(":", 1)[0] in self.payload_tar_containers):
                self.problems.append(problem)
                return
        self._rules(name, data, depth)
        if depth == 1 and re.fullmatch(r"[^:\[\]]+\.tar\.gz\[gzip\]", name) and audit_package.is_tar(data):
            self.payload_tar_containers.add(name)
        super().blob(name, data, depth)


def is_detached_signature(name: str, data: bytes, depth: int, payload_tar: bool = False) -> bool:
    """PUBLICCI-1: a release's detached Ed25519 signature (manifest*.json.sig, sign_manifest.py, and SIGNKMS-1's manifest*.json.sig2,
    kms_sign.py: 64 raw bytes, top level) is random; about
    1 in 992 starts with a valid zlib header (the hosted pull request checks hit one in test_autorelease). Such a file that does NOT decode
    as zlib is a signature, not a finding; one that decodes is still decoded and checked like any other stream.
    The signed payload sidecar at a release package root has the same random bytes (one archive layer, or the Linux gzip stream followed by its tar)."""
    if len(data) != 64:
        return False
    return (depth == 0 and re.fullmatch(r"(?:.*/)?[^/]+\.json\.sig2?", name) is not None
            or depth == 1 and re.fullmatch(r"[^:]+:openbfme-[^/:]+/payload\.json\.sig", name) is not None
            or depth == 2 and payload_tar and re.fullmatch(r"[^:]+:openbfme-[^/:]+/payload\.json\.sig", name) is not None)


def _is_text(data: bytes) -> bool:
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return b"\x00" not in data


ALLOWLIST = HERE / "public_binaries.txt"
CONTAINER_SUFFIXES = (".zip", ".jar", ".apk", ".whl", ".egg", ".nupkg", ".gz", ".tgz", ".tar", ".bz2", ".tbz2", ".xz", ".txz", ".lzma",
                      ".7z", ".rar", ".cab", ".zst", ".lz4", ".lzo", ".z", ".zlib", ".pck", ".big", ".dmg", ".iso", ".cpio")


def load_allowlist(path: Path | None = None) -> dict[str, str]:
    """{path: sha256} of the containers the public tree may hold: lines `<sha256>  <path>  <reason>`, # comments; a malformed line,
    a missing reason or a path listed twice is an error (fail closed)"""
    out: dict[str, str] = {}
    path = path or ALLOWLIST
    if not path.exists():
        raise SystemExit(f"autorelease: the container allowlist {path} is missing")
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.fullmatch(r"([0-9a-f]{64})  (\S+)  (\S.*)", line)
        if not m or m.group(2) in out:
            raise SystemExit(f"autorelease: {path.name}:{n} is not `<sha256>  <path>  <reason>` (or lists a path twice)")
        out[m.group(2)] = m.group(1)
    return out


def container_kind(path: str, data: bytes) -> str:
    """'' or what makes the file a container or a compressed file, by content as well as by name"""
    if path.lower().endswith(CONTAINER_SUFFIXES):
        return f"suffix {Path(path).suffix.lower()}"
    if data[:4] in (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08") or zipfile.is_zipfile(io.BytesIO(data)):
        return "zip"
    kind = audit_package.stream_kind(data)
    if kind:
        return kind
    if audit_package.is_tar(data):
        return "tar"
    if data[:4] == audit_package.PCK_MAGIC or audit_package.embedded_pck_base(data) is not None:
        return "Godot pack"
    for magic, what in audit_package.UNOPENABLE.items():
        if data.startswith(magic):
            return what
    if data[:4] in (b"BIGF", b"BIG4"):
        return "BIG archive"
    if data[:3] == b"\x5d\x00\x00":
        return "lzma"
    if len(data) >= 2 and data[0] & 0x0F == 8 and data[0] >> 4 <= 7 and (data[0] << 8 | data[1]) % 31 == 0:
        try:
            zlib.decompress(data)
            return "zlib"
        except zlib.error:
            if not _is_text(data):
                return "zlib (undecodable)"
    return ""


def container_metadata_problems(name: str, data: bytes, depth: int = 0) -> list[str]:
    """a listed container's metadata, all layers: ZIP without archive / member comments or extra fields, gzip without a file name,
    comment or extra field, tar with regular files and folders only (no links, devices, PAX / GNU headers, owner names); every member
    name passes the path rules; only zip, tar, gzip, bzip2, xz and zlib layers are allowed (anything else is refused even when listed)"""
    if depth > audit_package.MAX_DEPTH:
        return [f"{name}: containers nested deeper than {audit_package.MAX_DEPTH}"]
    probs: list[str] = []
    kind = container_kind(name if depth == 0 else "", data)
    if not kind:
        return probs
    if kind.startswith("suffix") and container_kind("", data) in ("gzip", "bzip2", "xz", "zlib"):
        kind = container_kind("", data)   # r3 (Sol r2): a listed .gz / .bz2 / .xz / .zlib is read by its content
    try:
        if kind == "zip" or (depth == 0 and kind.startswith("suffix") and zipfile.is_zipfile(io.BytesIO(data))):
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                if z.comment:
                    probs.append(f"{name}: a ZIP archive comment")
                for i in z.infolist():
                    if i.comment:
                        probs.append(f"{name}:{i.filename}: a ZIP member comment")
                    if i.extra:
                        probs.append(f"{name}:{i.filename}: a ZIP extra field")
                    if i.flag_bits & 1:
                        probs.append(f"{name}:{i.filename}: an encrypted ZIP member")
                        continue
                    p = path_problem(i.filename)
                    if p:
                        probs.append(f"{name}: {p}")
                    if not i.is_dir():
                        probs += container_metadata_problems(f"{name}:{i.filename}", z.read(i), depth + 1)
        elif kind == "gzip":
            for n, (flags, _) in enumerate(gzip_headers(data), 1):   # r4 (Sol r3 P2): every member's header, not the first only
                if flags & 0x1C:
                    probs.append(f"{name}: gzip member {n}'s header has a file name, comment or extra field")
            probs += container_metadata_problems(f"{name}[gzip]", b"".join(audit_package.decompress_members("gzip", data)), depth + 1)
        elif kind in ("bzip2", "xz"):
            probs += container_metadata_problems(f"{name}[{kind}]", b"".join(audit_package.decompress_members(kind, data)), depth + 1)
        elif kind == "zlib":
            probs += container_metadata_problems(f"{name}[zlib]", zlib.decompress(data), depth + 1)
        elif kind == "tar" or (depth == 0 and kind.startswith("suffix") and audit_package.is_tar(data)):
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as t:
                for m in t.getmembers():
                    if not (m.isfile() or m.isdir()) or m.linkname:
                        probs.append(f"{name}:{m.name}: a tar member that is not a file or a folder (link, device ...)")
                    if m.pax_headers:
                        probs.append(f"{name}:{m.name}: tar PAX headers")
                    if m.uname or m.gname:
                        probs.append(f"{name}:{m.name}: tar owner names")
                    p = path_problem(m.name)
                    if p:
                        probs.append(f"{name}: {p}")
                    if m.isfile():
                        probs += container_metadata_problems(f"{name}:{m.name}", t.extractfile(m).read(), depth + 1)
        else:
            probs.append(f"{name}: a {kind} file is refused even when listed (only zip, tar, gzip, bzip2, xz and zlib can be audited)")
    except (zipfile.BadZipFile, tarfile.TarError, ValueError, OSError, EOFError, zlib.error) as e:
        probs.append(f"{name}: a {kind} that cannot be decoded completely ({e})")
    return probs


def _user_names() -> set[str]:
    names = {Path.home().name}
    try:
        import getpass  # noqa: PLC0415
        names.add(getpass.getuser())
    except (KeyError, OSError):
        pass
    return {n.lower() for n in names if len(n) >= 2}


USER_NAMES = _user_names()


def path_problem(path: str) -> str:
    """a tracked path (or a member name) with a private path or a user name in it: '' when there is none"""
    if audit_package.MACHINE_PATH.search(path) or re.search(r"(?i)(^|[/\\])(home|users)[/\\]", path):
        return f"a private path in a file name: {path}"
    # a whole path component equal to a user name (a folder or file named after the account); not a file's stem: the CI user "claude"
    # would otherwise refuse the repository's CLAUDE.md on that host
    for c in re.split(r"[/\\]", path):
        if c.lower() in USER_NAMES:
            return f"a user name in a file name: {path}"
    return ""


# ---- the public metadata's privacy audit (r6) ------------------------------------------------------------------------------------------
# The sync commit message, the PR title and body, the release title and notes and the tag message go to the public repository: they pass
# these rules or the run stops before anything is pushed (Sol r5: a Release-note with a private path and the key path reached the public
# commit message, the PR and the release notes). Nothing is redacted here: a hit is a failure.
NOREPLY = re.compile(r"(?i)^(?:[0-9]+\+[a-z0-9-]+@users\.noreply\.github\.com|noreply@anthropic\.com)$")
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
# our own fixed attribution lines (their words are not user or host names)
OWN_LINES = ("Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>", "🤖 Generated with [Claude Code](https://claude.com/claude-code)")
ORDINARY_WORDS = frozenset({"mac"})   # ssh aliases that are ordinary words of the public text (private_words)


def key_path() -> Path | None:
    """the release key's path (PUBLISH-2: the operator's OPENBFME_RELEASE_KEY, which autorelease.sh exports; the public tree names no
    private path), or None outside a release run"""
    k = os.environ.get("OPENBFME_RELEASE_KEY", "")
    return Path(k) if k else None


def _ssh_config_values(field: str) -> set[str]:
    out: set[str] = set()
    cfg = Path.home() / ".ssh" / "config"
    try:
        lines = cfg.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return out
    for line in lines:
        parts = line.split()
        if len(parts) >= 2 and parts[0].lower() == field.lower():
            out.update(v.lower() for v in parts[1:] if not any(c in v for c in "*?!"))
    return out


_PRIVATE_WORDS: tuple[set[str], set[str]] | None = None


def private_words() -> tuple[set[str], set[str]]:
    """(user names, host names) of this machine and the hosts it reaches: the local account, the ssh config's users and hosts (no DNS
    lookup: gethostname only; computed once)"""
    global _PRIVATE_WORDS
    if _PRIVATE_WORDS is None:
        import socket  # noqa: PLC0415
        users = set(USER_NAMES) | _ssh_config_values("User")
        hosts = {socket.gethostname().lower(), socket.gethostname().lower().split(".")[0]} | _ssh_config_values("Host")
        # v0.4.7 (coordinator, 2026-10-18): an ssh alias that is also an ordinary word of our public text (the platform name "mac" in
        # the release notes' download table) is no host name there; every other alias still counts
        _PRIVATE_WORDS = ({u for u in users if len(u) >= 2}, {h for h in hosts if len(h) >= 2 and h != "localhost" and h not in ORDINARY_WORDS})
    return _PRIVATE_WORDS


def public_text_problems(name: str, text: str) -> list[str]:
    """the privacy problems of text that goes to the public repository: home / Users paths, the release key's path (or its folder),
    user names in path or user@host form, host names, e-mail addresses other than the noreply ones. Checked as written, without Markdown escapes and NFKC-normalised
    (full-width @ / # back to ASCII), so an escaped or neutralised form is caught too."""
    import unicodedata  # noqa: PLC0415
    for own in OWN_LINES:
        text = text.replace(own, "")
    forms = {text, re.sub(r"\\(.)", r"\1", text)}
    forms |= {unicodedata.normalize("NFKC", f) for f in list(forms)}
    users, hosts = private_words()
    key = key_path()
    secrets = {str(Path.home())} | ({str(key), str(key.parent), key.name} if key else set())
    problems: set[str] = set()
    for f in forms:
        if audit_package.MACHINE_PATH.search(f) or re.search(r"(?i)(^|[^A-Za-z0-9])(home|users)[/\\][A-Za-z0-9._-]", f):
            problems.add("a home or Users path")
        for sct in secrets:
            if len(sct) > 1 and sct in f:
                problems.add("the release key's path or folder (or the home folder)")
        low = f.lower()
        for u in users:
            # r7 (Sol r6): a user name counts only where it names an account: a path component (/deck, \\deck, ~deck) or user@host;
            # an ordinary word ("Steam Deck controls now work") is not one
            e = re.escape(u)
            if re.search(rf"(?:[/\\\\~]{e}(?![a-z0-9_-]))|(?:(?<![a-z0-9_.+-]){e}@[a-z0-9])", low):
                problems.add("a user name of this machine or its ssh config (masked)")
        for h in hosts:
            if re.search(rf"(?<![a-z0-9_.-]){re.escape(h)}(?![a-z0-9_-])", low):
                problems.add("a host name of this machine or its ssh config (masked)")
        for e in EMAIL.findall(f):
            if not NOREPLY.match(e):
                problems.add("an e-mail address that is not the noreply one")
    return [f"{name}: {p}" for p in sorted(problems)]


# ---- PUBLISH-2: the public tree's denylist (after the 2026-10-15 history scrub) --------------------------------------------------------
# Two rule sets, both checked on every path and every text file of the public tree (and on the public metadata), and both FAIL the sync:
#   * BUILTIN_RULES, here: generic shapes only (address ranges, private path forms, account-id shapes, operational file names), so this
#     public file names nothing private;
#   * the operator's private denylist (autorelease.sh: OPENBFME_RELEASE_DENYLIST, required), kept outside the repository: the host, machine
#     and account names, the owner's identity, the infrastructure's ids and file names. The same rule format as the CI mirror's
#     denylist.yml (ci/scripts/denylist-scan.py): `{"rules": [{"id", "kind": regex | word | user, "pattern": str or [str], "allow": [glob]}]}`
#     (JSON; YAML when PyYAML is installed). Unlike the mirror, a `warn` severity fails here too: a release never publishes a known hit.
# A rule's `allow` globs name files where a hit is known-safe test data; `paths: true` makes a regex rule a file-name rule only.
# the audit's own rules and their test fixtures are the only files allowed to spell these shapes
SELF = ["tools/release/autorelease.py", "tools/release/test_autorelease.py"]
LAN_TEST_FILES = ["engine/tests/**", "godot/tests/**", "launcher/tests/**"]
BUILTIN_RULES = [
    # addresses: private LAN and tailnet ranges and names (synthetic addresses in the game's own LAN tests are allowed)
    {"id": "lan-192.168", "kind": "regex", "pattern": r"(?<![\d.])192\.168\.\d{1,3}\.\d{1,3}(?![\d.])", "allow": LAN_TEST_FILES},
    {"id": "lan-10", "kind": "regex", "pattern": r"(?<![\d.])10\.\d{1,3}\.\d{1,3}\.\d{1,3}(?![\d.])", "allow": LAN_TEST_FILES},
    {"id": "lan-172", "kind": "regex", "pattern": r"(?<![\d.])172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}(?![\d.])", "allow": LAN_TEST_FILES},
    {"id": "cgnat-tailnet-ip", "kind": "regex", "pattern": r"(?<![\d.])100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}(?![\d.])"},
    {"id": "tailnet-name", "kind": "regex", "pattern": r"\b[a-z0-9-]+\.ts\.net\b"},
    # private path forms (the home / Users forms are the commit gate's MACHINE_PATH, checked above)
    {"id": "private-path", "kind": "regex", "pattern": r"~/\.ssh/|/mnt/[a-z]/Users/"},
    # account ids and cloud credentials by shape
    # lane ONLINEMENU-1: the mailer's IAM policy and its deploy steps name their resources with the <ACCOUNT_ID> placeholder only
    # (test_autorelease.test_onlinemenu1_the_mailer_arns_hold_no_account pins that)
    {"id": "aws-account", "kind": "regex", "allow": ["services/online/mailer/iam-policy.json", "services/online/mailer/README.md"],
     "pattern": r"arn:aws[a-z-]*:|\b(?:AKIA|ASIA)[A-Z0-9]{16}\b|aws_secret_access_key|aws_session_token|"
                                                     r"\baccount\W{0,12}\d{4}-?\d{4}-?\d{4}\b"},
    {"id": "cloudflare-account", "kind": "regex", "pattern": r"(?:account|zone)[_ -]?id\W{0,6}[0-9a-f]{32}\b"},
    # e-mail addresses: only the noreply forms and the reserved example domains are public (third-party credits and test fixtures allowed);
    # RELFIX-3: nongnu.org's public mailing lists too (FreeType's, in every Godot template)
    # lane ONLINEMENU-1: the project's own domain (the sign-in mail's sender no-reply@ and the privacy contact admin@ are published by
    # design: docs/PRIVACY-SIGNIN.md) and the lobby's / mailer's address-validation test fixtures
    {"id": "e-mail", "kind": "regex", "allow": ["engine/thirdparty/**", "engine/src/GameClient/VP6Decoder.cpp", "engine/src/GameClient/VP6Tables.inc",
                                                  "engine/tests/**", "godot/tests/**", "launcher/tests/**", "services/online/lobby/test/email.test.mjs",
                                                  "services/online/lobby/test/email-flow.test.mjs",
                                                  "services/online/mailer/test_handler.py"],
     "pattern": r"[A-Za-z0-9._%+-]+@(?!users\.noreply\.github\.com\b|anthropic\.com\b|nongnu\.org\b|openbfme\.com(?![\w.-])|example\.(?:com|org|net)\b|[a-z0-9.-]*\.(?:example|invalid|test|localhost)\b)"
                # r3: a file name after the '@' is no address (the game's library holds "...o@multiplayer.ini")
                r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.(?!(?:ini|cfg|txt|json|big|str|dat|map|wnd|apt|w3d|tga|dds|png|gd|tscn|tres)\b)"
                r"[A-Za-z]{2,}\b"},
    # operational files by name: deploy notes, the deploy folder (the sync drops it), logs, agent and orchestration files, CI config
    {"id": "operational-file", "kind": "regex", "paths": True,
     "pattern": r"(?:^|/)DEPLOY[^/]*\.md$|^services/online/deploy/|\.log$|(?:^|/)\.codex|(?:^|/)\.lane-|(?:^|/)orchestration/|"
                r"(?:^|/)briefs/|(?:^|/)AGENTS\.md$|^\.gitea/|"
                r"^(?:services|tools/net)/(?:.*/)?[^/]*(?<![a-z0-9])(?:backup|monitor|watch|infra|ops)(?![a-z0-9])[^/]*$"},
]
RULE_KINDS = ("regex", "word", "user")
RULE_FIELDS = {"id", "kind", "pattern", "severity", "allow", "paths"}
SEVERITIES = ("fail", "warn")


def glob_re(pattern: str) -> re.Pattern:
    """a path glob (`**` any folders, `*` / `?` within one name), as the CI mirror's denylist-scan.py reads its allow lists"""
    out, i = "", 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out += "(?:.*/)?"; i += 3
        elif pattern.startswith("**", i):
            out += ".*"; i += 2
        elif pattern[i] == "*":
            out += "[^/]*"; i += 1
        elif pattern[i] == "?":
            out += "[^/]"; i += 1
        else:
            out += re.escape(pattern[i]); i += 1
    return re.compile(out + r"\Z")


def compile_rules(rules, source: str) -> list[tuple[str, re.Pattern, list[re.Pattern], bool]]:
    """[(id, pattern, allow globs, paths only)]. A strict schema (Sol r1 P2: `"paths": "false"` made a rule path-only): only the fields of
    RULE_FIELDS, each of its type (`paths` a JSON boolean, regex rules only; `severity` fail or warn); anything else, or an empty rule
    list, is an error. Errors name the rule's position and the field, never a pattern (the patterns are private)."""
    if not isinstance(rules, list) or not rules:
        raise SystemExit(f"autorelease: the denylist {source} has no rules (refusing to audit with an empty list)")
    out = []
    for n, r in enumerate(rules, 1):
        bad = lambda what: SystemExit(f"autorelease: the denylist {source}, rule {n}: {what}")  # noqa: E731
        if not isinstance(r, dict):
            raise bad("is not an object")
        extra = sorted(set(r) - RULE_FIELDS)
        if extra:
            # PUBLISH-2 r3 (review F3): only how many: a YAML slip can turn a private value into a key
            raise bad(f"{len(extra)} unknown field(s) (the fields are {', '.join(sorted(RULE_FIELDS))})")
        if not isinstance(r.get("id"), str) or not r["id"].strip():
            raise bad("needs an id")
        kind = r.get("kind")
        if kind not in RULE_KINDS:
            raise bad(f"needs a kind ({', '.join(RULE_KINDS)})")
        pat = r.get("pattern")
        words = pat if isinstance(pat, list) else [pat]
        if not words or not all(isinstance(w, str) and w for w in words) or (kind == "regex" and isinstance(pat, list)):
            raise bad("an empty or malformed pattern")
        if "severity" in r and r["severity"] not in SEVERITIES:
            raise bad("severity is fail or warn")
        allow = r.get("allow", [])
        if not isinstance(allow, list) or not all(isinstance(g, str) and g for g in allow):
            raise bad("allow is a list of path globs")
        paths = r.get("paths", False)
        if not isinstance(paths, bool):
            raise bad("paths is true or false (a JSON boolean)")
        if paths and kind != "regex":
            raise bad("paths applies to regex rules only")
        alt = "|".join(re.escape(w) for w in words)
        try:
            if kind == "regex":
                rx = re.compile(pat, re.I)
            elif kind == "word":
                rx = re.compile(rf"(?<![A-Za-z0-9_.-])(?:{alt})(?![A-Za-z0-9_-])", re.I)
            else:  # an account name: a path component (/name, \name, ~name) or name@host, never an ordinary word
                rx = re.compile(rf"(?:[/\\~](?:{alt})(?![A-Za-z0-9_-]))|(?:(?<![A-Za-z0-9_.+-])(?:{alt})@[A-Za-z0-9])", re.I)
        except re.error:
            raise bad("its pattern is not a valid regular expression") from None
        out.append((r["id"], rx, [glob_re(g) for g in allow], paths))
    return out


BUILTIN = compile_rules([{**r, "allow": sorted(set(r.get("allow", [])) | set(SELF))} for r in BUILTIN_RULES], "built in")


class _Duplicate(Exception):
    pass


def load_denylist(path: Path) -> list[tuple[str, re.Pattern, list[re.Pattern], bool]]:
    """the operator's private denylist (see BUILTIN_RULES): a missing, unreadable, empty or malformed file is an error, never 'no rules'.
    Its own errors never quote it (Sol r1 P1: a YAML parser exception printed a private pattern)."""
    try:
        raw = path.read_bytes()
    except OSError as e:
        raise SystemExit(f"autorelease: cannot read the private denylist ({e.strerror})") from None
    try:
        text = strict_text(raw)
    except ValueError as e:
        raise SystemExit(f"autorelease: the private denylist {e}") from None
    top = {"rules"}
    # PUBLISH-2 r3 (Sol r2 P2): a key given twice is an error in both forms (both parsers keep the last one: a second `pattern` replaced
    # the first, a second `rules` list dropped the whole first one)
    if path.suffix.lower() in (".yml", ".yaml"):
        try:
            import yaml  # noqa: PLC0415
        except ImportError:
            raise SystemExit("autorelease: the private denylist is YAML but PyYAML is not installed (use the JSON form)") from None

        class Strict(yaml.SafeLoader):
            pass

        def mapping(loader, node, deep=False):
            keys = [loader.construct_object(k, deep=deep) for k, _ in node.value]
            if len(keys) != len({repr(k) for k in keys}):
                raise _Duplicate(node.start_mark.line + 1)
            return yaml.SafeLoader.construct_mapping(loader, node, deep)
        Strict.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
        try:
            data = yaml.load(text, Loader=Strict)  # noqa: S506 (a SafeLoader subclass)
        except _Duplicate as e:
            raise SystemExit(f"autorelease: the private denylist gives a key twice (line {e.args[0]})") from None
        except Exception as e:  # noqa: BLE001  (every parser error: only its position is printed)
            mark = getattr(e, "problem_mark", None)
            raise SystemExit(f"autorelease: the private denylist is not valid YAML (line {mark.line + 1 if mark else '?'})") from None
    else:
        def pairs(items):
            keys = [k for k, _ in items]
            if len(keys) != len(set(keys)):
                raise _Duplicate("?")
            return dict(items)
        try:
            data = json.loads(text, object_pairs_hook=pairs)
        except _Duplicate:
            raise SystemExit("autorelease: the private denylist gives a key twice in one object") from None
        except json.JSONDecodeError as e:
            raise SystemExit(f"autorelease: the private denylist is not JSON (line {e.lineno})") from None
    if not isinstance(data, dict) or any(k not in top and not str(k).startswith("_") for k in data):
        raise SystemExit("autorelease: the private denylist is an object with a rules list (and _comment fields) only")
    return compile_rules(data.get("rules"), "(private)")


def _mask(s: str) -> str:
    """a rejected value as printed: nothing of it but its length (Sol r1 P1: two-character matches were printed whole)"""
    return f"<{len(s)} chars>"


def strict_text(data: bytes) -> str:
    """UTF-8 text without NUL bytes (an optional UTF-8 BOM dropped), or ValueError: never a replacement decoding (Sol r1 P2: UTF-16 text
    with a denylisted value passed the metadata audit as replacement characters)"""
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        raise ValueError("is UTF-16 (only UTF-8 text is accepted)")
    if b"\x00" in data:
        raise ValueError("holds NUL bytes (only UTF-8 text is accepted)")
    try:
        return data.decode("utf-8").removeprefix("﻿")
    except UnicodeDecodeError as e:
        raise ValueError(f"is not UTF-8 (byte {e.start})") from None


DENEUTRAL = str.maketrans({"＠": "@", "＃": "#", "‑": "-", "∗": "*", "∼": "~", "(": "[", ")": "]"})


def text_forms(text: str, unescape: bool = True) -> list[str]:
    """the forms a text is checked in: as written, without Markdown backslash escapes (metadata only: in a source file they are string
    escapes, and test fixtures spell paths that way), NFKC-normalised, and with neutral() / markdown()'s look-alikes turned back
    (Sol r1 P1: '@' written as a full-width sign hid an account@host from the private rules)"""
    import unicodedata  # noqa: PLC0415
    forms = [text] + ([re.sub(r"\\(.)", r"\1", text)] if unescape else [])
    forms += [unicodedata.normalize("NFKC", f) for f in list(forms)]
    forms += [f.translate(DENEUTRAL) for f in list(forms)]
    return list(dict.fromkeys(forms))


NORMAL_CHUNK = 1 << 20
SPARSE_MIN = 1 << 16   # _nfkc looks for a sparse text's stretches from this length on
_ASCII_CHAR = re.compile(r"[\x00-\x7f]")
_NON_INERT: tuple[re.Pattern, frozenset[str]] | None = None


def non_inert() -> tuple[re.Pattern, frozenset[str]]:
    """(a pattern of runs of characters that NFKC may change or join to their neighbours, the ones of them beyond U+FFFF): a character
    whose own NFKC form differs, a combining mark, the second character of any canonical composition (Hangul vowel and trailing jamo
    included). The pattern's class has every character beyond U+FFFF (re reads astral ranges one by one), the set the ones that
    count. Every other ("inert") character is a normalisation boundary that normalises to itself: a starter that never composes with
    what comes before it, so NFKC(x + c + y) = NFKC(x) + c + NFKC(y) for one. Read from the running unicodedata."""
    global _NON_INERT
    if _NON_INERT is None:
        import unicodedata  # noqa: PLC0415
        seconds = set(range(0x1161, 0x1176)) | set(range(0x11A8, 0x11C3))
        for cp in range(0x110000):
            if not 0xD800 <= cp < 0xE000:
                d = unicodedata.decomposition(chr(cp))
                if d and not d.startswith("<") and len(d.split()) == 2:
                    seconds.add(int(d.split()[1], 16))
        chars = [chr(cp) for cp in range(0x110000) if not 0xD800 <= cp < 0xE000 and (
            cp in seconds or unicodedata.combining(chr(cp)) or unicodedata.normalize("NFKC", chr(cp)) != chr(cp))]
        _NON_INERT = (re.compile(rulescan.class_re("".join(chars), astral=True).pattern + "+"),
                      frozenset(c for c in chars if c >= "\U00010000"))
    return _NON_INERT


def _nfkc(t: str) -> str:
    """unicodedata.normalize("NFKC", t); when few characters are not inert (non_inert(): the UTF-32 decodings, ~90 % U+FFFD, and the
    UTF-8 one of a binary), only the stretches around them, each from the inert character before it to the next inert one"""
    import unicodedata  # noqa: PLC0415
    if len(t) < SPARSE_MIN:
        return unicodedata.normalize("NFKC", t)   # short: not worth reading non_inert() for
    rx, astral = non_inert()

    def counts(m) -> bool:   # a run that is not all inert characters beyond U+FFFF
        return not all(c >= "\U00010000" and c not in astral for c in m.group(0))
    sample = t[:1 << 14]
    if sum(map(counts, rx.finditer(sample))) > len(sample) >> 6:
        return unicodedata.normalize("NFKC", t)
    runs = []
    for m in rx.finditer(t):
        if counts(m):
            runs.append(m.span())
            if len(runs) > len(t) >> 6:
                return unicodedata.normalize("NFKC", t)
    out, cur = "", 0
    for a, b in runs:
        a = max(a - 1, cur)   # the inert starter its marks may compose with
        out += t[cur:a]
        out += unicodedata.normalize("NFKC", t[a:b])
        cur = b
    return out + t[cur:] if runs else t


def _back(t: str) -> str:
    """the look-alikes turned back (str.translate, but as fast on 20 MB of decoded binary as on a line)"""
    for k, v in DENEUTRAL.items():
        t = t.replace(chr(k), v)
    return t


def _normal_piece(t: str) -> str:
    return _back(_nfkc(_back(t)))


def _normal(text: str) -> str:
    """the look-alikes turned back, NFKC-normalised, the look-alikes turned back again (U+2011 normalises to U+2010, not to '-').
    PRIVSPEED-1: a long text piece by piece (NORMAL_CHUNK characters, each piece up to the next ASCII character), the pieces joined in
    place: the same text (an ASCII character is inert, non_inert(); the look-alikes are per character), without normalize()'s
    whole-text buffers; each piece through _nfkc()."""
    out, a = "", 0
    while True:
        m = _ASCII_CHAR.search(text, a + NORMAL_CHUNK) if a + NORMAL_CHUNK < len(text) else None
        b = m.start() if m else len(text)
        out += _normal_piece(text[a:b])
        a = b
        if a >= len(text):
            return out


# the decodings binary_texts() reads, in order; PRIVSPEED-1: a large content's scan is split into these groups of them (about the same
# work each: the whole data in Latin-1, in UTF-8, half of it in each UTF-16 alignment, a quarter in each UTF-32 one)
BINARY_DECODINGS = ("latin-1", "utf-8") + tuple(f"{enc} +{off}" for enc, step in (("utf-16-le", 2), ("utf-16-be", 2), ("utf-32-le", 4),
                                                                                    ("utf-32-be", 4)) for off in range(step))
BINARY_GROUPS = tuple((d,) for d in BINARY_DECODINGS[:6]) + tuple(BINARY_DECODINGS[i:i + 2] for i in range(6, 14, 2))
# a worker's peak memory per byte of the content for each decoding (measured on v0.4.9's 113 MB OpenBFMELauncher.exe: Latin-1 3.9,
# UTF-8 6.5 (its text is UCS-4), UTF-16 4.8-5.2, a UTF-32 pair 4.8-5.1), rounded up: what parallel_scans() budgets
DECODING_MEMORY = {"latin-1": 5, "utf-8": 8, "utf-16": 6, "utf-32": 6}

_FLAG = {name: bytes(0xFF if test(i) else 0 for i in range(256)) for name, test in (
    ("nonzero", lambda i: i != 0), ("above-10", lambda i: i > 0x10), ("zero", lambda i: i == 0), ("d8-df", lambda i: 0xD8 <= i <= 0xDF))}


def _utf32_replace(data: bytes, enc: str, start: int = 0, end: int | None = None) -> str:
    """data[start:end].decode(enc, "replace") for UTF-32 (a whole number of units): the same text, without the codec's per-unit error
    path (binary data is ~90 % invalid units, which made the UTF-32 decodings a fifth of the whole scan). A unit is invalid when it is
    above U+10FFFF or a surrogate (U+D800..U+DFFF); each becomes one U+FFFD, as the codec does. The bytes of each significance are read
    as one column (data[i::4]), flagged with bytes.translate and combined as integers; the result is decoded strictly, so any unit this
    missed fails the decode, and the codec decodes it instead."""
    end = len(data) if end is None else end
    n = (end - start) // 4
    if (end - start) % 4 or n <= 0:
        return data[start:end].decode(enc, "replace")
    order = (0, 1, 2, 3) if enc.endswith("le") else (3, 2, 1, 0)   # the offsets of the low ... high byte of each unit

    def num(k: int, flag: str | None = None) -> int:   # column k (0: the low byte), flagged
        col = data[start + order[k]:end:4]
        return int.from_bytes(col.translate(_FLAG[flag]) if flag else col, "little")
    ones = (1 << (8 * n)) - 1
    high = num(3, "nonzero")
    bad = high | num(2, "above-10") | (num(1, "d8-df") & num(2, "zero") & (high ^ ones))
    del high
    if not bad:
        try:
            return data[start:end].decode(enc)
        except UnicodeDecodeError:
            return data[start:end].decode(enc, "replace")
    keep = bad ^ ones
    out = bytearray(data[start:end])
    for k, fill in enumerate(b"\xfd\xff\x00\x00"):   # U+FFFD, low byte first
        col = num(k) & keep | (bad & int.from_bytes(bytes([fill]) * n, "little") if fill else 0)
        out[order[k]::4] = col.to_bytes(n, "little")
        del col
    try:
        return out.decode(enc)
    except UnicodeDecodeError:
        return data[start:end].decode(enc, "replace")


def _span(data: bytes, name: str) -> tuple[str, int, int, int]:
    """(codec, code unit size, first byte, end byte) of a BINARY_DECODINGS decoding: the alignment's whole code units"""
    if name in ("latin-1", "utf-8"):
        return name, 1, 0, len(data)
    enc, off = name.split(" +")
    step, off = (2 if "16" in enc else 4), int(off)
    return enc, step, off, len(data) - (len(data) - off) % step


def _decode(data: bytes, enc: str, start: int, end: int) -> str:
    if enc.startswith("utf-32"):
        return _utf32_replace(data, enc, start, end)
    return data[start:end].decode(enc, "replace")


def _decoding(data: bytes, name: str) -> str:
    enc, _, start, end = _span(data, name)
    return _decode(data, enc, start, end)


# a code unit that decodes to an ASCII character, by codec (UTF-8: any byte below 0x80 is one, never part of a sequence)
_ASCII_UNIT = {"latin-1": re.compile(rb"[\x00-\x7f]"), "utf-8": re.compile(rb"[\x00-\x7f]"),
               "utf-16-le": re.compile(rb"[\x00-\x7f]\x00"), "utf-16-be": re.compile(rb"\x00[\x00-\x7f]"),
               "utf-32-le": re.compile(rb"[\x00-\x7f]\x00\x00\x00"), "utf-32-be": re.compile(rb"\x00\x00\x00[\x00-\x7f]")}


def _pieces(data: bytes, name: str) -> list[tuple[int, int]]:
    """byte ranges that split a decoding about every NORMAL_CHUNK characters, each after the first starting at a code unit that
    decodes to an ASCII character: decoding them one by one gives the whole decoding (no codec state or error range crosses such a
    unit: the UTF-8 and UTF-16 'replace' decodings restart there exactly) and normalising them one by one its _normal() (_normal)"""
    enc, step, start, end = _span(data, name)
    rx, bounds = _ASCII_UNIT[enc], [start]
    while bounds[-1] + NORMAL_CHUNK * step < end:
        pos, q = bounds[-1] + NORMAL_CHUNK * step, None
        while q is None:
            m = rx.search(data, pos, end)
            if not m:
                break
            if (m.start() - start) % step == 0:
                q = m.start()
            pos = m.start() + 1
        if q is None:
            break
        bounds.append(q)
    bounds.append(end)
    return list(zip(bounds, bounds[1:]))


def _normal_form(data: bytes, name: str) -> str | None:
    """_normal() of the decoding `name` of data, or None if that is the decoding itself, made piece by piece from the bytes (_pieces):
    the whole decoding is not held while its normal form is built (a 100 MB executable's UTF-8 decoding is UCS-4: both at once took
    gigabytes per worker)"""
    enc, *_ = _span(data, name)
    out, first, pos = "", None, 0
    for a, b in _pieces(data, name):
        t = _decode(data, enc, a, b)
        n = _normal_piece(t)
        if first is None and n != t:
            first = (pos, t)   # the first piece that changes: the text up to it is out's
        out += n
        pos += len(t)
        del t, n
    if first is None:
        return None   # every piece is its own normal form: so is the whole
    at, t = first
    if out[at:at + len(t)] != t:
        return out    # out differs from the decoding there
    pos = 0
    for a, b in _pieces(data, name):   # the same text after all (never seen): compare it whole
        t = _decode(data, enc, a, b)
        if out[pos:pos + len(t)] != t:
            return out
        pos += len(t)
    return None if pos == len(out) else out


def binary_texts(data: bytes, decodings: tuple[str, ...] = BINARY_DECODINGS) -> Iterator[tuple[str, str]]:
    """[(encoding, text)]: what the private rules see of binary data (a pinned binary, a decoded container member, a library). PUBLISH-2 r3
    (Sol r2 P2: only ASCII and UTF-16LE runs of 4+ were read, so a 3-character literal, UTF-16BE, UTF-32 and non-ASCII UTF-8 strings
    passed): the whole data in Latin-1 (every ASCII byte as itself: a literal of any length, between any bytes), UTF-8 and UTF-16 / UTF-32
    in both byte orders at every alignment (BINARY_DECODINGS). r4 (Sol r3 P2: a full-width host name passed in UTF-16 and UTF-32): every
    decoding that is not ASCII is also read _normal()ised, as text is (text_forms). Undecodable code units become U+FFFD, which no rule
    matches and which ends a word. One form at a time (a generator: 28 decodings of a 20 MB library held at once took gigabytes).
    decodings: a part of BINARY_DECODINGS (PRIVSPEED-1: a worker's group), in its order. PRIVSPEED-1: a long decoding is let go (the
    consumer drops it too) before its normal form is made from the bytes (_normal_form)."""
    for enc in decodings:
        text = _decoding(data, enc)
        yield enc, text
        if text.isascii():
            continue
        if len(text) <= NORMAL_CHUNK:
            norm = _normal(text)
            norm = None if norm == text else norm
        else:
            del text
            norm = _normal_form(data, enc)
        if norm is not None:
            yield f"{enc} NFKC", norm
        del norm


# RELFIX-3 (v0.4.9's publish refused 3-letter private words found by chance in 100 MB of executables): in binary bytes a match of this
# many characters or fewer is not a finding; text, names, notes and metadata are still checked for them
SHORT_BINARY = 3
# RELFIX-4 (the v0.4.9 macOS executables: "T@9.xk" in ARM64 machine code): an e-mail-shaped match of 7 characters or fewer in binary
# bytes is chance too (the shortest real address is longer; a private one far longer)
SHORT_BINARY_RULE = {"e-mail": 7}


def short_binary(rid: str) -> int:
    """the longest match of rule rid that is no finding in binary bytes"""
    return max(SHORT_BINARY, SHORT_BINARY_RULE.get(rid, 0))

TEXT_SUFFIXES = (".txt", ".md", ".json", ".gd", ".cfg", ".ini", ".tscn", ".tres", ".import", ".remap", ".gdshader", ".gdshaderinc",
                 ".gdextension", ".csv", ".xml", ".yml", ".yaml", ".html", ".sh", ".py", ".toml", ".pub")
# the file and member names a rule matched (their printed forms are masked wherever they appear: sanitize)
PRIVATE_NAMES: set[str] = set()


def name_forms(path: str) -> list[str]:
    """a file or member name as the rules check it (PUBLISH-2 r3, review F4 and Sol r2 P2): every text_forms() form, and for a container
    member (`archive:member`, `x.tar.gz[gzip]`) also each member part on its own, so an anchored rule (`^docs/...`) sees it from its start"""
    parts = {path}
    for piece in re.split(r":|\[[^\]]*\]", path):
        if piece:
            parts.add(piece.lstrip("/"))
    return list(dict.fromkeys(f for p in sorted(parts) for f in text_forms(p, unescape=False)))


def denylist_hits(path: str, text: str | None, rules, check_path: bool = True, metadata: bool = False, data: bytes | None = None) -> list[str]:
    """the hits of one file (its name, and its text, or with data= its binary_texts(), unless None) as problem lines. Nothing of a matched
    value is printed (_mask), and a file name that a rule matches is printed masked too (and recorded in PRIVATE_NAMES). PRIVSPEED-1:
    every rule's matches through rulescan.finditer (exactly rx.finditer, run only where a match can be); binary data through
    binary_rule_hits(), once per distinct content and in a worker process inside parallel_scans() (then the result is a _Pending
    that resolve() expands, in order)."""
    names = name_forms(path) if check_path else []
    maybe = _name_rules(names, rules)
    named = [rid for i, (rid, rx, allow, _) in enumerate(rules)
             if i in maybe and not any(g.match(path) for g in allow) and any(rx.search(n) for n in names)]
    if named:
        PRIVATE_NAMES.add(path)
    shown_once: list[str] = []

    def shown() -> str:   # the name as printed: made when a line needs it (sanitize is most of an audit of thousands of clean files)
        if not shown_once:
            shown_once.append(sanitize(path, rules))
        return shown_once[0]
    live = [r for r in rules if not any(g.match(path) for g in r[2])]
    found: dict[str, list[str]] = {r[0]: [] for r in live}
    for rid, rx, _, _ in live:
        if rid in named:
            m = next(m for m in (rx.search(n) for n in names) if m)
            found[rid].append(f"denylisted ({rid}) in a file name: {shown()} ({_mask(m.group(0))})")
    order = list(dict.fromkeys(r[0] for r in live))
    if data is not None:
        shown()   # now: PRIVATE_NAMES as of this file (a worker's lines are made later)
        def lines(hits: dict[str, list[tuple[str, int, int]]]) -> list[str]:
            for rid, got in hits.items():
                found[rid] += [f"denylisted ({rid}) in {shown()} ({enc} bytes at {at}) (<{n} chars>)" for enc, at, n in got]
            return [h for rid in order for h in found[rid]]
        hits = _binary_scan(data, live)
        return [_Pending(lambda: lines(hits.result()))] if isinstance(hits, _Joined) else lines(hits)
    if text is not None:
        for own in OWN_LINES:   # our own attribution lines (their URL path reads like a user path to an account rule)
            text = text.replace(own, "")
    seen: set[tuple[str, str]] = set()
    scanned = [rx for _, rx, _, paths_only in live if not paths_only]
    for form in (text_forms(text, unescape=metadata) if text is not None else ()):   # every rule on each form
        index = rulescan.literal_index(form, scanned)   # every rule's literals in one pass
        for rid, rx, _, paths_only in live:
            if paths_only:
                continue
            for m in rulescan.finditer(rx, form, None, index):
                where = f"{shown()}:{form.count(chr(10), 0, m.start()) + 1}"
                if (rid, where) not in seen:
                    seen.add((rid, where))
                    found[rid].append(f"denylisted ({rid}) in {where} ({_mask(m.group(0))})")
    return [h for rid in order for h in found[rid]]


def _name_rules(names: list[str], rules) -> set[int]:
    """the indexes of the rules that may match one of the names: every rule whose cover literal (rulescan) is in one of them, every
    rule rulescan scans whole"""
    if not names:
        return set()
    index = rulescan.literal_index("\n".join(names), [rx for _, rx, _, _ in rules])
    out = set()
    for i, (_, rx, _, _) in enumerate(rules):
        sc = rulescan.scan_of(rx)
        if index is None or sc.mode == "full" or any(index.get(c[0]) for c in sc.cover):
            out.add(i)
    return out


def binary_rule_hits(data: bytes, rules, decodings: tuple[str, ...] = BINARY_DECODINGS) -> dict[str, list[tuple[str, int, int]]]:
    """{rule id: [(encoding, offset, length)]}: the first match of each rule in each of binary_texts(data) (its offset in that decoding,
    its length; never its value), every rule on each form. RELFIX-3: a match of SHORT_BINARY characters or fewer is no hit; a UTF-8 match
    that is ASCII is the Latin-1 form's (the same bytes); an NFKC form's match that its own decoding has as written is that decoding's.
    Each decoding (with its NFKC form) is on its own, so the hits of a split scan (decodings: a group, in order) joined in
    BINARY_DECODINGS order are the whole scan's (_Joined)."""
    found: dict[str, list[tuple[str, int, int]]] = {}
    values: dict[tuple[str, str], set[str]] = {}   # (rule, encoding): the values found in it
    seen: set[tuple[str, str]] = set()
    scanned = [rx for _, rx, _, paths_only in rules if not paths_only]

    def scan(enc: str, form: str) -> None:
        index = rulescan.literal_index(form, scanned)   # every rule's literals in one pass
        for rid, rx, _, paths_only in rules:
            if paths_only:
                continue
            for m in rulescan.finditer(rx, form, None, index):
                v = m.group(0)
                if len(v) <= short_binary(rid):
                    continue   # RELFIX-3: a match this short in binary bytes is chance (machine code, ICU / locale tables), no leak
                if enc == "utf-8" and v.isascii():
                    continue   # the Latin-1 form has it (the same bytes)
                if enc.endswith(" NFKC") and v in values.get((rid, enc[:-5]), ()):
                    continue   # the encoding's own form has it as written
                values.setdefault((rid, enc), set()).add(v)
                if (rid, enc) not in seen:
                    seen.add((rid, enc))
                    found.setdefault(rid, []).append((enc, m.start(), len(v)))
    for enc, form in binary_texts(data, decodings):   # form by form (binary_texts is a generator)
        scan(enc, form)
        del form   # PRIVSPEED-1: not held while the next form is made
    return found


class _Pending:
    """problem lines a worker is still computing (parallel_scans): resolve() expands it in place. Never printable (fail closed: a
    pending scan that reached a report unresolved is an error, not a pass)."""

    def __init__(self, fn) -> None:
        self.fn = fn

    def __str__(self) -> str:
        raise RuntimeError("autorelease: an unresolved privacy scan reached a report")

    __format__ = lambda self, spec: str(self)  # noqa: E731


def prefixed(prefix: str, problems: list) -> list:
    """prefix + every problem line (a _Pending one when it is resolved)"""
    return [_Pending(lambda p=p: [prefix + x for x in p.fn()]) if isinstance(p, _Pending) else prefix + p for p in problems]


def resolve(problems: list) -> list[str]:
    """the problem lines with every _Pending expanded in place (the order of a sequential run); a failed worker is an error that names
    nothing it read (its exception's text could quote what it scanned)"""
    out: list[str] = []
    for p in problems:
        if isinstance(p, _Pending):
            try:
                out += p.fn()
            except Exception as e:  # noqa: BLE001
                raise SystemExit(f"autorelease: a privacy scan worker failed ({type(e).__name__}): nothing is published") from None
        else:
            out.append(p)
    return out


def _rule_spec(rules) -> tuple:
    return tuple((rid, rx.pattern, rx.flags, paths_only) for rid, rx, _, paths_only in rules)


_WORKER_RULES: dict[tuple, list] = {}


def _scan_job(data: bytes, spec: tuple, decodings: tuple[str, ...]) -> dict[str, list[tuple[str, int, int]]]:
    """a worker's binary_rule_hits (the rules rebuilt from their spec: ids, patterns, flags)"""
    rules = _WORKER_RULES.get(spec)
    if rules is None:
        rules = _WORKER_RULES[spec] = [(rid, re.compile(pat, flags), [], po) for rid, pat, flags, po in spec]
    return binary_rule_hits(data, rules, decodings)


class _Part:
    """one worker task: a group of decodings of one content, with the memory it may take (DECODING_MEMORY)"""

    def __init__(self, data: bytes, spec: tuple, group: tuple[str, ...]) -> None:
        self.args: tuple | None = (data, spec, group)
        self.cost = len(data) * max(DECODING_MEMORY[re.sub(r"-[lb]e$", "", d.split(" ")[0])] for d in group) + (64 << 20)
        self.future: Future | None = None


class _Joined:
    """the workers' parts of one content's scan: result() joins them in BINARY_DECODINGS order (the whole scan's hits)"""

    def __init__(self, state: "_Scans", parts: list[_Part]) -> None:
        self.state, self.parts = state, parts

    def result(self) -> dict[str, list[tuple[str, int, int]]]:
        out: dict[str, list[tuple[str, int, int]]] = {}
        for p in self.parts:
            self.state.run(p)
            for rid, got in p.future.result().items():
                out.setdefault(rid, []).extend(got)
        return out


def _worker_failed(e: BaseException) -> SystemExit:
    return SystemExit(f"autorelease: a privacy scan worker failed ({type(e).__name__}): nothing is published")


class _Scans:
    """parallel_scans()'s state: each distinct (content, rules) once (sha256), large ones in a worker pool, very large ones split into
    BINARY_GROUPS (one worker each: a 100 MB executable no longer scans on one core while the others wait). Tasks start while the
    workers' estimated memory (_Part.cost) stays within the budget (scan_memory(); one task at a time always runs)."""
    INLINE = 1 << 20          # smaller contents are scanned in this process
    SPLIT = 8 << 20           # larger ones are split
    INFLIGHT = 512 << 20      # contents waiting for a worker that this process holds (the walk waits beyond it)

    def __init__(self, jobs: int, budget: int | None = None) -> None:
        self.done: dict[tuple, object] = {}
        self.pool = None
        self.jobs = jobs
        self.budget = scan_memory() if budget is None else budget
        self.queue: list[_Part] = []
        self.running: dict[Future, _Part] = {}
        self.used = 0
        if jobs > 1:
            # workers from a clean process (never fork() this one: a thread of the caller's, e.g. a test's server, can hold a lock a
            # forked child then waits on forever): forkserver where there is one (Linux), else spawn
            import multiprocessing  # noqa: PLC0415
            method = "forkserver" if sys.platform.startswith("linux") and "forkserver" in multiprocessing.get_all_start_methods() \
                else "spawn"
            self.pool = ProcessPoolExecutor(max_workers=jobs, mp_context=multiprocessing.get_context(method))

    def _pump(self) -> None:
        """retire finished tasks, start queued ones (first fit) while a worker is free and the budget allows"""
        for f in [f for f in self.running if f.done()]:
            self.used -= self.running.pop(f).cost
        i = 0
        while i < len(self.queue) and len(self.running) < self.jobs:
            p = self.queue[i]
            if self.running and self.used + p.cost > self.budget:
                i += 1
                continue
            del self.queue[i]
            try:
                p.future = self.pool.submit(_scan_job, *p.args)
            except Exception as e:  # noqa: BLE001  (a broken pool)
                raise _worker_failed(e) from None
            p.args = None
            self.running[p.future] = p
            self.used += p.cost

    def _wait(self) -> None:
        wait(list(self.running), return_when=FIRST_COMPLETED)
        self._pump()

    def run(self, part: _Part) -> None:
        """until the part has started (its future then gives its result)"""
        self._pump()
        while part.future is None:
            self._wait()

    def get(self, data: bytes, rules):
        spec = _rule_spec(rules)
        key = (hashlib.sha256(data).digest(), len(data), spec)
        if key in self.done:
            return self.done[key]
        if self.pool is None or len(data) < self.INLINE:
            got = self.done[key] = binary_rule_hits(data, rules)
            return got
        parts = [_Part(data, spec, g) for g in (BINARY_GROUPS if len(data) >= self.SPLIT else (BINARY_DECODINGS,))]
        self.queue += parts
        self._pump()
        while self.running and sum({id(p.args[0]): len(p.args[0]) for p in self.queue}.values()) > self.INFLIGHT:
            self._wait()
        got = self.done[key] = _Joined(self, parts)
        return got


_SCAN_STATE: _Scans | None = None


def _binary_scan(data: bytes, rules):
    """binary_rule_hits(data, rules): at once outside parallel_scans(), else deduplicated and (when large) a _Joined"""
    return binary_rule_hits(data, rules) if _SCAN_STATE is None else _SCAN_STATE.get(data, rules)


def scan_memory() -> int:
    """bytes the privacy gate's workers may take together: OPENBFME_PRIVACY_MEMORY_MB, else half of what this machine (and this
    process's cgroup, when it has a limit) has available, at most 16 GB; 4 GB where neither can be read"""
    v = os.environ.get("OPENBFME_PRIVACY_MEMORY_MB", "")
    if v:
        if not v.isdigit() or int(v) < 256:
            raise SystemExit("autorelease: OPENBFME_PRIVACY_MEMORY_MB is a whole number of at least 256")
        return int(v) << 20
    avail = []
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                avail.append(int(line.split()[1]) << 10)
        rel = next(x.split(":", 2)[2] for x in Path("/proc/self/cgroup").read_text().splitlines() if x.startswith("0::"))
        cg = Path("/sys/fs/cgroup") / rel.strip().lstrip("/")
        limit = (cg / "memory.max").read_text().strip()
        if limit.isdigit():
            avail.append(int(limit) - int((cg / "memory.current").read_text().strip()))
    except (OSError, ValueError, StopIteration):
        pass
    return max(512 << 20, min(min(avail) // 2, 16 << 30)) if avail else 4 << 30


def scan_jobs() -> int:
    """the privacy gate's worker processes: OPENBFME_PRIVACY_JOBS, else the CPU count up to 8 (1: everything in this process)"""
    v = os.environ.get("OPENBFME_PRIVACY_JOBS", "")
    if v:
        if not v.isdigit() or int(v) < 1:
            raise SystemExit("autorelease: OPENBFME_PRIVACY_JOBS is a whole number of at least 1")
        return int(v)
    return max(1, min(8, len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 1)))


@contextlib.contextmanager
def parallel_scans(jobs: int):
    """inside: binary contents are scanned once each (sha256 and rules), large ones in `jobs` worker processes; the problem lists of
    denylist_hits / content_hits / SyncAudit then hold _Pending entries until resolve()"""
    global _SCAN_STATE
    if _SCAN_STATE is not None:
        yield _SCAN_STATE
        return
    state = _SCAN_STATE = _Scans(jobs)
    try:
        yield state
    finally:
        _SCAN_STATE = None
        if state.pool is not None:
            state.pool.shutdown(wait=True, cancel_futures=True)


def _mapped_forms(seg: str) -> list[tuple[str, list[int]]]:
    """(form, index of each of its characters in seg): seg as written, without backslash escapes, and both with each character
    NFKC-normalised and the look-alikes turned back (character by character, so a match maps back to the characters it came from)"""
    import unicodedata  # noqa: PLC0415
    plain, plain_idx, i = [], [], 0
    while i < len(seg):
        if seg[i] == "\\" and i + 1 < len(seg):
            i += 1
        plain.append(seg[i]); plain_idx.append(i); i += 1
    out = [(seg, list(range(len(seg)))), ("".join(plain), plain_idx)]
    for form, idx in list(out):
        chars, cidx = [], []
        for c, j in zip(form, idx):
            n = unicodedata.normalize("NFKC", c).translate(DENEUTRAL)
            chars.append(n); cidx += [j] * len(n)
        out.append(("".join(chars), cidx))
    return out


TOKEN = re.compile(r"[^\s:\"'(),\[\]<>]+")
ANCHORS = re.compile(r"\^|\$|\\A|\\Z|\\z")


def _spans(line: str, rxs) -> list[tuple[int, int]]:
    """(start, end) of every match in the line of any of rxs, in every _mapped_forms() form: in the whole line, in each word (TOKEN) on its
    own, and (PUBLISH-2 r4, Sol r3 P2: `^docs/private folder/` never matched a path with a space inside a message, a word holds only a
    part of it) for a rule with an anchor in any piece of the line that starts where a word starts and ends where a word ends: from each
    word start to the end of the line (`^`), from the start to each word end (`$`) and between the two (both)."""
    import bisect  # noqa: PLC0415
    spans = []
    for start, seg in [(0, line)] + [(m.start(), m.group(0)) for m in TOKEN.finditer(line)]:
        for form, idx in _mapped_forms(seg):
            for rx in rxs:
                for m in rx.finditer(form):
                    if m.end() > m.start():
                        spans.append((start + idx[m.start()], start + idx[m.end() - 1] + 1))
    anchored = [rx for rx in rxs if ANCHORS.search(rx.pattern)]
    if not anchored:
        return spans
    words = list(TOKEN.finditer(line))
    starts = sorted({0} | {m.start() for m in words})
    ends = sorted({len(line)} | {m.end() for m in words})
    for form, idx in _mapped_forms(line):
        at = lambda i: bisect.bisect_left(idx, i)  # noqa: E731  (a line position -> the form's)
        def add(m, off: int) -> None:
            if m and m.end() > m.start():
                spans.append((idx[off + m.start()], idx[off + m.end() - 1] + 1))
        for rx in anchored:
            for i in starts:
                fi = at(i)
                tail = form[fi:]
                for mm in rx.finditer(tail):
                    add(mm, fi)
                for j in ends:
                    if j > i:
                        add(rx.match(tail, 0, at(j) - fi), fi)
            for j in ends:
                for mm in rx.finditer(form, 0, at(j)):
                    add(mm, 0)
    return spans


def sanitize(line: str, rules, words: bool = True) -> str:
    """a diagnostic line with every match of any rule (its paths-only rules too) and (words) of this machine's user and host names masked:
    what a problem line, a warning, an error or the redactor may print. PUBLISH-2 r3: matched in every form of the line (review F2: a
    full-width '@' hid an account@host), in each of its words on its own (Sol r2 P2: an anchored path rule never matched a path inside a
    message) and the file names a rule matched (PRIVATE_NAMES) wherever they appear; r4: an anchored rule in every piece between word
    boundaries (_spans). Fail closed: a line that any form of still matches after the masking is withheld whole."""
    rxs = [rx for _, rx, _, _ in rules]
    if words:
        users, hosts = private_words()
        rxs += [re.compile(rf"(?<![A-Za-z0-9_.-]){re.escape(w)}(?![A-Za-z0-9_-])", re.I) for w in sorted(users | hosts, key=len, reverse=True)]
    for n in sorted(PRIVATE_NAMES, key=len, reverse=True):
        line = line.replace(n, _mask(n))
    out, cur = "", 0
    for a, b in sorted(_spans(line, rxs)):
        if b <= cur:
            continue
        a = max(a, cur)
        out += line[cur:a] + _mask(line[a:b])
        cur = b
    out += line[cur:]
    if _spans(out, rxs) or any(rx.search(f) for seg in [out] + TOKEN.findall(out) for f in text_forms(seg) for rx in rxs):
        return f"<a line of {len(line)} chars withheld: a private rule matches it>"
    return out


SYNC_EXCLUDE = "tools/release/sync_exclude.txt"
ARCHIVE_LINE = "previous codebase is kept in `archive/` as reference only.\n"
PUBLIC_ARCHIVE_LINE = "previous codebase lives on in this repository's history (tag `legacy-final`).\n"


def sync_excludes(git_dir: str | None, sha: str) -> list[str]:
    """the repository-relative paths the public tree leaves out, from the sha's own SYNC_EXCLUDE (in step with the CI mirror's
    mirror-exclude.txt): one path per line (a folder drops everything under it), # comments; the file is required"""
    r = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "blob", f"{sha}:{SYNC_EXCLUDE}"],
                       capture_output=True)
    if r.returncode:
        raise SystemExit(f"autorelease: {sha} has no {SYNC_EXCLUDE} (the sync recipe's exclusions): refusing to build its public tree")
    out = []
    for n, line in enumerate(r.stdout.decode("utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("/") or line.endswith("/") or any(c in ("", ".", "..") for c in line.split("/")) or "\\" in line:
            raise SystemExit(f"autorelease: {SYNC_EXCLUDE}:{n} is not a repository-relative path: {line}")
        out.append(line)
    return out


def _tree_without(git_dir: str | None, tree: str, parts: list[str]) -> str:
    g = ["git"] + (["-C", git_dir] if git_dir else [])
    entries = [e for e in subprocess.run(g + ["ls-tree", "-z", tree], capture_output=True, check=True).stdout.decode().split("\0") if e]
    out = []
    for e in entries:
        meta, name = e.split("\t", 1)
        if name == parts[0]:
            if len(parts) == 1:
                continue
            mode, kind, oid = meta.split()
            if kind == "tree":
                oid = _tree_without(git_dir, oid, parts[1:])
                if not subprocess.run(g + ["ls-tree", oid], capture_output=True, check=True).stdout:
                    continue  # nothing left in that folder
                meta = f"{mode} {kind} {oid}"
        out.append(f"{meta}\t{name}")
    return subprocess.run(g + ["mktree", "-z"], input="".join(f"{e}\0" for e in out).encode(), capture_output=True,
                          check=True).stdout.decode().strip()


def sync_tree(git_dir: str | None, sha: str) -> str:
    """the public tree of an archive sha (the sync recipe): its tree without archive/ and every SYNC_EXCLUDE path, README.md's archive
    line replaced by the public one (exactly once, or an error)"""
    g = ["git"] + (["-C", git_dir] if git_dir else [])
    readme = subprocess.run(g + ["cat-file", "blob", f"{sha}:README.md"], capture_output=True, check=True).stdout.decode("utf-8")
    if readme.count(ARCHIVE_LINE) != 1:
        raise SystemExit("autorelease: README.md of the archive sha does not have the archive line exactly once (the sync recipe replaces it)")
    blob = subprocess.run(g + ["hash-object", "-w", "--stdin"], input=readme.replace(ARCHIVE_LINE, PUBLIC_ARCHIVE_LINE).encode(),
                          capture_output=True, check=True).stdout.decode().strip()
    tree = git(git_dir, "rev-parse", f"{sha}^{{tree}}").strip()
    for p in ["archive", "README.md"] + sync_excludes(git_dir, sha):
        tree = _tree_without(git_dir, tree, p.split("/"))
    entries = [e for e in subprocess.run(g + ["ls-tree", "-z", tree], capture_output=True, check=True).stdout.decode().split("\0") if e]
    entries.append(f"100644 blob {blob}\tREADME.md")
    return subprocess.run(g + ["mktree", "-z"], input="".join(f"{e}\0" for e in entries).encode(), capture_output=True,
                          check=True).stdout.decode().strip()


SUBMODULES = HERE / "public_submodules.txt"
SYMLINKS_ALLOWED: frozenset[str] = frozenset()   # PUBLISH-2 r2: the public tree has no symbolic link; one would be named here, reviewed


def load_submodules(path: Path | None = None) -> dict[str, tuple[str, str]]:
    """{path: (commit, url)} of the submodules the public tree may carry: lines `<40 hex commit>  <path>  <url>`, # comments; a malformed
    line or a path listed twice is an error (fail closed)"""
    out: dict[str, tuple[str, str]] = {}
    path = path or SUBMODULES
    if not path.exists():
        raise SystemExit(f"autorelease: the submodule allowlist {path.name} is missing")
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.fullmatch(r"([0-9a-f]{40})  (\S+)  (https://\S+)", line)
        if not m or m.group(2) in out:
            raise SystemExit(f"autorelease: {path.name}:{n} is not `<commit>  <path>  <https url>` (or lists a path twice)")
        out[m.group(2)] = (m.group(1), m.group(3))
    return out


def link_problems(git_dir: str | None, entries) -> list[str]:
    """PUBLISH-2 r2 (Sol r1 P2): no symbolic link (a checkout or an archive tool may follow it out of the tree), and only the approved
    submodules: each gitlink's (path, commit) and its .gitmodules url must be a line of public_submodules.txt, and .gitmodules names
    no other submodule"""
    probs = []
    approved = load_submodules()
    for mode, kind, oid, path in entries:
        if mode == "120000" and path not in SYMLINKS_ALLOWED:
            probs.append(f"a symbolic link in the public tree: {path}")
        if kind == "commit":
            want = approved.get(path)
            if want is None:
                probs.append(f"a submodule that is not in {SUBMODULES.name}: {path}")
            elif want[0] != oid:
                probs.append(f"the submodule {path} is at another commit than {SUBMODULES.name} approves")
    gm = [oid for mode, kind, oid, path in entries if path == ".gitmodules" and kind == "blob"]
    declared: dict[str, dict] = {}
    if gm:
        raw = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "blob", gm[0]], capture_output=True,
                             check=True).stdout
        with tempfile.NamedTemporaryFile() as f:
            f.write(raw)
            f.flush()
            r = subprocess.run(["git", "config", "--file", f.name, "--null", "--list"], capture_output=True)
        if r.returncode:
            probs.append(".gitmodules cannot be read")
        for item in r.stdout.decode("utf-8", "replace").split("\0"):
            k, _, v = item.partition("\n")
            m = re.fullmatch(r"submodule\.(.+)\.(path|url)", k)
            if m:
                declared.setdefault(m.group(1), {})[m.group(2)] = v
    for name, d in declared.items():
        want = approved.get(d.get("path", ""))
        if want is None or d.get("url") != want[1]:
            probs.append(f".gitmodules declares a submodule that {SUBMODULES.name} does not approve (path and url): {d.get('path', name)}")
    for mode, kind, oid, path in entries:
        if kind == "commit" and path not in {d.get("path") for d in declared.values()}:
            probs.append(f"a submodule without its .gitmodules entry: {path}")
    return probs


def audit_tree(git_dir: str | None, tree: str, denylist=None) -> list[str]:
    """the problems of a tree about to be pushed to the public repository (empty: it may go). Run before the first push (Sol r1: a tracked
    workspace/ file or a retail file outside archive/ went to the public branch). PUBLISH-2: every path and text file also passes
    BUILTIN_RULES and the operator's private denylist (compiled rules, load_denylist())."""
    problems = []
    rules = BUILTIN + list(denylist or [])
    allowed = load_allowlist()
    for line in git(git_dir, "ls-tree", "-z", tree).split("\0"):
        if line and line.split("\t", 1)[1] not in PUBLIC_TOP:
            problems.append(f"not a public top-level entry: {line.split(chr(9), 1)[1]}")
    entries = []  # (mode, type, oid, path)
    for line in git(git_dir, "ls-tree", "-r", "-z", "--full-tree", tree).split("\0"):
        if line:
            meta, path = line.split("\t", 1)
            mode, kind, oid = meta.split()
            entries.append((mode, kind, oid, path))
    for _, _, _, path in entries:
        hit = [c for c in path.split("/")[:-1] if c.lower() in PRIVATE_DIRS]
        if hit:
            problems.append(f"inside a private folder ({hit[0]}/): {path}")
    # .gitignore rules of the tree itself, applied with --no-index: a tracked file that a rule ignores is still refused
    with tempfile.TemporaryDirectory() as tmp:
        env = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
        subprocess.run(["git", "init", "-q", tmp], check=True, env=env)
        for mode, kind, oid, path in entries:
            target = Path(tmp, path)
            target.parent.mkdir(parents=True, exist_ok=True)
            if kind == "blob" and path.split("/")[-1] == ".gitignore":
                target.write_bytes(subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "blob", oid],
                                                  capture_output=True, check=True).stdout)
            elif kind != "tree":
                target.touch()
        r = subprocess.run(["git", "-C", tmp, "-c", f"core.excludesFile={os.devnull}", "check-ignore", "--no-index", "-z", "--stdin"],
                           input="\0".join(p for _, _, _, p in entries).encode(), capture_output=True, env=env)
        if r.returncode not in (0, 1):
            raise SystemExit(f"autorelease: git check-ignore failed: {r.stderr.decode(errors='replace')}")
        problems += [f"a .gitignore rule ignores it (tracked anyway): {p}" for p in r.stdout.decode().split("\0") if p]
    # the commit gate's rules on every file (names and bytes)
    blobs = [(oid, path) for mode, kind, oid, path in entries if kind == "blob"]
    cat = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "--batch"],
                         input="".join(f"{oid}\n" for oid, _ in blobs).encode(), capture_output=True, check=True).stdout
    pos = 0
    for oid, path in blobs:
        header_end = cat.index(b"\n", pos)
        size = int(cat[pos:header_end].split()[2])
        data = cat[header_end + 1:header_end + 1 + size]
        pos = header_end + 1 + size + 1
        problem = check_file(path, verb="in the public tree") or check_file(path, data, verb="in the public tree")
        if problem:
            problems.append(problem)
        # PUBLISH-2 r2 (Sol r1 P2): a binary file's readable strings pass the rules too, pinned or not (a pinned hash never exempts a file)
        problems += denylist_hits(path, None, rules) + content_hits(path, data, rules)
        if problem:
            continue
        binary = not _is_text(data)
        if binary and path not in allowed:
            # r6: the public tree is text only (Sol r5: a prefixed gzip, a shell stub with gzip, PNG + gzip passed the detectors)
            problems.append(f"a binary file in the public tree (not UTF-8 text, or NUL bytes), not in {ALLOWLIST.name}: {path}")
            continue
        if binary and hashlib.sha256(data).hexdigest() != allowed[path]:
            problems.append(f"{path} is listed in {ALLOWLIST.name} with another sha256")
            continue
        kind = container_kind(path, data)
        if kind:
            # r5: no containers in the public tree, except listed ones (tools/release/public_containers.txt: path, sha256, reason), each
            # fully decoded and audited, its metadata included (Sol r4: comments, extra fields, names and tar metadata leaked)
            listed = allowed.get(path)
            if listed is None:
                problems.append(f"a container or compressed file ({kind}) in the public tree, not in {ALLOWLIST.name}: {path}")
                continue
            if hashlib.sha256(data).hexdigest() != listed:
                problems.append(f"{path} is listed in {ALLOWLIST.name} with another sha256")
                continue
            problems += container_metadata_problems(path, data)
        inner = SyncAudit(rules)
        inner.blob(path, data)
        problems += prefixed("inside a container: ", inner.problems)
    for _, _, _, path in entries:
        problem = path_problem(path)
        if problem:
            problems.append(problem)
    for mode, kind, oid, path in entries:
        if kind == "commit":
            problem = check_file(path, verb="in the public tree")
            if problem:
                problems.append(problem)
            problems += denylist_hits(path, None, rules)
    problems += link_problems(git_dir, entries)
    # PUBLISH-2 r3 (review hardening): GitHub's source archives expand `$Format:...$` in files an export-subst attribute names, so what they
    # publish would not be the audited blob: the attribute is refused (the public tree uses none)
    for mode, kind, oid, path in entries:
        if kind == "blob" and path.split("/")[-1] == ".gitattributes":
            text = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "blob", oid], capture_output=True,
                                  check=True).stdout.decode("utf-8", "replace")
            if re.search(r"(?i)(?<![\w-])export-subst\b", text):
                problems.append(f"an export-subst attribute (GitHub's source archives would not hold the audited bytes): {path}")
    return problems


def note_sources(commits: list[dict], known_issues: list) -> list[str]:
    """the original texts the notes are made from (PUBLISH-2 r2, Sol r1 P1: neutral() hides an account@host behind a full-width sign): the
    Release-note lines, or the subject, of every commit that gives a note, and the known issues"""
    out = []
    for c in commits:
        if notes_of(c["subject"], c.get("body", ""), c.get("files", [])):
            out += release_note_texts(c.get("body", "")) or [c["subject"]]
    return out + [i["text"] for i in known_issues]


def asset_problems(assets: list[Path], rules, templates: dict[str, tuple[Path, str]]) -> list[str]:
    """PUBLISH-2 r3 (review F1, Sol r2): the files a release uploads (the archives, the checksum file, the manifest and its signature) pass
    the built-in and the private rules: each file and every member at every depth, by name and content (content_hits; the Godot
    executables in the export region of the template pinned for them)"""
    problems = []
    for f in assets:
        try:
            data = f.read_bytes()
        except OSError as e:
            problems.append(f"a release asset cannot be read ({e.strerror}): {f.name}")
            continue
        a = SyncAudit(rules, top=True, templates=templates)
        a.blob(f.name, data)
        problems += prefixed("in a release asset: ", a.problems)   # the package audit's own findings too (fail closed)
    return problems


def privacy_gate(git_dir: str | None, tree: str | None, files: list[Path], rules, assets: list[Path] = (),
                 templates: dict[str, tuple[Path, str]] | None = None, jobs: int | None = None) -> int:
    """the one privacy gate of every publishing path: the tree (audit_tree), the text files (strict UTF-8, public_text_problems and
    every rule on every form of the text) and the release assets (asset_problems); every line printed is sanitized. 0: it may go.
    PRIVSPEED-1: the binary scans run once per distinct content, in `jobs` worker processes (scan_jobs()); the lines keep the order of a
    sequential run."""
    with parallel_scans(scan_jobs() if jobs is None else jobs):
        problems = _privacy_problems(git_dir, tree, files, rules, assets, templates)
    every = BUILTIN + list(rules)
    label = "SYNC" if tree else "METADATA"
    for p in problems:
        print(f"{label} AUDIT: {sanitize(p, every)}")
    what = f"{len(files)} text files" + (f", {len(assets)} release assets" if assets else "")
    print(f"{label} AUDIT {'FAILED' if problems else 'ok'}: {tree or ''} {what}".replace(":  ", ": "))
    return 1 if problems else 0


def _privacy_problems(git_dir: str | None, tree: str | None, files: list[Path], rules, assets, templates) -> list[str]:
    problems = []
    for f in files:
        try:
            text = strict_text(f.read_bytes())
        except ValueError as e:
            problems.append(f"{f.name} {e}")
            continue
        except OSError as e:
            problems.append(f"{f.name} cannot be read ({e.strerror})")
            continue
        problems += public_text_problems(f.name, text) + denylist_hits(f.name, text, BUILTIN + list(rules), check_path=False, metadata=True)
    if tree:
        problems += audit_tree(git_dir, tree, rules)
    every = BUILTIN + list(rules)
    if assets:
        problems += asset_problems(list(assets), every, templates or {})
    return resolve(problems)


def redact(denylist: Path, check: bool = False) -> int:
    """the release scripts' output redactor (autorelease.sh, publish_release.sh: every line they and their helpers print or log): the
    release key's path and folder and the home folder (as given and resolved, also Markdown-escaped and URL-encoded) become <release key>,
    <release key folder> and ~; then sanitize() masks every rule's match (words=False: this machine's own names stay readable in its
    logs). PUBLISH-2 r3 (review F5): fail closed: a list that cannot be loaded prints one line saying so and nothing of the input."""
    try:
        rules = BUILTIN + load_denylist(denylist)
    except SystemExit as e:
        print(f"REDACTOR FAILED: {e} (output withheld)", flush=True)
        return 3
    if check:
        return 0
    import urllib.parse  # noqa: PLC0415
    pairs = []
    for var, disp in (("AR_KEY", "<release key>"), ("AR_KEYREAL", "<release key>"), ("AR_KEYDIR", "<release key folder>"),
                      ("AR_KEYDIRREAL", "<release key folder>"), ("AR_HOME", "~"), ("AR_HOMEREAL", "~")):
        v = os.environ.get(var, "").rstrip("/")
        if len(v) > 1:
            for form in {v, re.sub(r"([\\`*_{}\[\]()!|~+\-.:#])", r"\\\1", v), urllib.parse.quote(v), urllib.parse.quote(v, safe="")}:
                pairs.append((form, disp))
    pairs.sort(key=lambda p: -len(p[0]))   # the longest first: the key before its folder before the home folder
    out = sys.stdout
    for raw in iter(sys.stdin.buffer.readline, b""):
        line = raw.decode("utf-8", "replace")
        end = "\n" if line.endswith("\n") else ""
        line = line[:len(line) - len(end)]
        for v, disp in pairs:
            line = line.replace(v, disp)
        out.write(sanitize(line, rules, words=False) + end)
        out.flush()
    return 0


def load_known_issues(path: Path) -> list[dict]:
    return [{"text": i["text"], "area": i.get("area", "")} for i in json.loads(path.read_text(encoding="utf-8"))["issues"]]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("next-version")
    v.add_argument("--remote", default="origin")
    v.add_argument("--bump", default="patch", choices=BUMPS, help="minor: the middle number + 1 (a huge feature, the owner's call)")
    rv = sub.add_parser("release-version", help="the version of this automatic release: the next plain release")
    rv.add_argument("--bump", default="patch", choices=BUMPS)
    rv.add_argument("--remote", default="origin")
    rv.add_argument("--repo", required=True)
    rv.add_argument("--sha", required=True)
    rv.add_argument("--gh", default=os.environ.get("GH", "gh"))
    nv = sub.add_parser("nightly-version", help="NIGHTLY-2: the nightly of a run started at --clock (UTC YYYYMMDDHH): the next plain "
                        "release's numbers, -nightly.<YYMMDD><1 morning | 2 evening>00")
    nv.add_argument("--remote", default="origin")
    nv.add_argument("--clock", required=True)
    bo = sub.add_parser("bridge-ok", help="exit 0 when the old launchers' page still holds a verified bridge (before an alpha is published)")
    bo.add_argument("--remote", default="origin")
    bo.add_argument("--repo", required=True)
    bo.add_argument("--sha", required=True)
    bo.add_argument("--gh", default=os.environ.get("GH", "gh"))
    iv = sub.add_parser("intent-version", help="the remote's tags (git ls-remote lines) on stdin")
    iv.add_argument("version")
    iv.add_argument("last", nargs="?", default="")
    ap_ = sub.add_parser("auto-publish", help="exit 0 when a plain release may be published without the typed tag (v0.4.5 to below v1.0.0)")
    ap_.add_argument("version")
    lg = sub.add_parser("legacy-needed", help="exit 0 and name them when a published launcher reads format 2 only (a release must carry the launcher archives), "
                                                     "10 when none does")
    lg.add_argument("--remote", default="origin")
    tt = sub.add_parser("title")
    tt.add_argument("version")
    nw = sub.add_parser("newer")
    nw.add_argument("a")
    nw.add_argument("b")
    c = sub.add_parser("changed")
    c.add_argument("--from", dest="a", required=True)
    c.add_argument("--to", dest="b", required=True)
    n = sub.add_parser("notes")
    n.add_argument("--from", dest="a", required=True)
    n.add_argument("--to", dest="b", required=True)
    n.add_argument("--version", required=True)
    n.add_argument("--known-issues", type=Path, default=HERE / "known_issues.json")
    n.add_argument("--commit", default="")
    n.add_argument("--date", default="")
    n.add_argument("--repo", default=DEFAULT_REPO, help="OWNER/NAME of the release page (the download table's links)")
    n.add_argument("--titles", default=DEFAULT_TITLES, choices=sorted(TITLES), help="bridge: the old launchers' section titles for added / fixed")
    n.add_argument("--no-legacy-launchers", action="store_true", help="the release has no launcher-only archives (format 3)")
    n.add_argument("--mac", action="store_true", help="MACREL-1: the release has the macOS Apple Silicon package (its download row)")
    n.add_argument("--mac-manifest", action="store_true", help="MACREL-1: the release has manifest-macos.json (with or without the package)")
    n.add_argument("--out", type=Path, required=True)
    n.add_argument("--title-out", type=Path)
    n.add_argument("--items-out", type=Path, help="the items as Markdown (PR body)")
    n.add_argument("--plain-items-out", type=Path, help="the items as neutral plain text (the sync commit's message)")
    n.add_argument("--sources-out", type=Path, help="the original texts the items come from, unneutralised (audited too: PUBLISH-2 r2)")
    w = sub.add_parser("warnings", help="NOTES WARNING lines for the Release-note lines of A..B (never an error)")
    w.add_argument("--from", dest="a", required=True)
    w.add_argument("--to", dest="b", required=True)
    w.add_argument("--denylist", type=Path, help="mask what the private rules match in the warnings (autorelease.sh passes it)")
    # PUBLISH-2 r2 (Sol r1): one privacy gate for every publishing path (the release sync, the metadata, the notes, the tag's tree,
    # publish_release.sh): privacy-gate; audit-tree / audit-text are its two halves. The private denylist is required; --builtin-only
    # (the built-in rules alone) is for checks that publish nothing, such as the CI mirror's dry run, and says so.
    t = sub.add_parser("audit-tree")
    t.add_argument("--tree", required=True)
    x = sub.add_parser("audit-text", help="the public metadata files: exit 1 on any privacy hit (no redaction)")
    x.add_argument("files", nargs="+", type=Path)
    g = sub.add_parser("privacy-gate", help="the tree of --tree / --commit, the --text files and the --assets: the release's one privacy gate")
    g.add_argument("--tree")
    g.add_argument("--commit")
    g.add_argument("--no-tree", action="store_true", help="tests' untagged dry runs: no tree (the commit is not in this repository)")
    g.add_argument("--text", nargs="*", type=Path, default=[])
    g.add_argument("--assets", nargs="+", type=Path, default=[], help="the files the release uploads (at least one when given)")
    g.add_argument("--templates", type=Path, default=Path(os.environ.get("OPENBFME_GODOT_TEMPLATES") or
                                                          Path.home() / ".local/share/godot/export_templates/4.7.2.stable"),
                   help="the Godot export templates godot_templates.txt pins (default: Godot 4.7.2's)")
    rd = sub.add_parser("redact", help="stdin to stdout, every line masked: the release scripts' one output redactor")
    rd.add_argument("--denylist", type=Path, required=True)
    rd.add_argument("--check", action="store_true", help="only load the list: exit 0 when the redactor can run")
    for q in (t, x, g):
        q.add_argument("--denylist", type=Path, help="the operator's private denylist (required for every publishing check)")
        q.add_argument("--jobs", type=int, help="worker processes for the binary scans (default: OPENBFME_PRIVACY_JOBS, else the CPUs up to 8)")
    for q in (t, x):
        q.add_argument("--builtin-only", action="store_true", help="the built-in rules alone: NOT a publishing check")
    st = sub.add_parser("sync-tree", help="write the public tree of an archive sha (the sync recipe) and print its id")
    st.add_argument("--sha", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "sync-tree":
        print(sync_tree(a.git_dir, git(a.git_dir, "rev-parse", "--verify", f"{a.sha}^{{commit}}").strip()))
        return 0
    if a.cmd == "redact":
        return redact(a.denylist, a.check)
    if a.cmd in ("audit-text", "audit-tree", "privacy-gate"):
        if a.jobs is not None and a.jobs < 1:
            print("AUDIT FAILED: --jobs is at least 1", file=sys.stderr)
            return 2
        if a.denylist:
            rules = load_denylist(a.denylist)
        elif getattr(a, "builtin_only", False):
            rules = []
            print("AUDIT NOTE: the built-in rules only (--builtin-only): NOT a publishing check")
        else:
            print("AUDIT FAILED: no private denylist (--denylist): every publishing check needs it", file=sys.stderr)
            return 2
        if a.cmd == "audit-tree":
            return privacy_gate(a.git_dir, a.tree, [], rules, jobs=a.jobs)
        if a.cmd == "audit-text":
            return privacy_gate(a.git_dir, None, a.files, rules, jobs=a.jobs)
        if (bool(a.tree) + bool(a.commit) + a.no_tree) != 1:
            print("AUDIT FAILED: privacy-gate needs one of --tree, --commit and --no-tree", file=sys.stderr)
            return 2
        tree = a.tree
        if a.commit:
            r = subprocess.run(["git"] + (["-C", a.git_dir] if a.git_dir else []) + ["rev-parse", "--verify", "-q", f"{a.commit}^{{tree}}"],
                               capture_output=True, text=True)
            if r.returncode:
                print("AUDIT FAILED: the commit to publish is not in this repository: its tree cannot be audited", file=sys.stderr)
                return 1
            tree = r.stdout.strip()
        return privacy_gate(a.git_dir, tree, a.text, rules, a.assets, load_templates(a.templates) if a.assets else {}, a.jobs)
    if a.cmd == "next-version":
        print(next_version(remote_tags(a.git_dir, a.remote), a.bump))
        return 0
    if a.cmd == "release-version":
        version, why = release_version(remote_tags(a.git_dir, a.remote), a.bump)
        print(f"AUTORELEASE line: {version} ({why})", file=sys.stderr)
        print(version)
        return 0
    if a.cmd == "nightly-version":
        print(nightly_version(remote_tags(a.git_dir, a.remote), a.clock))
        return 0
    if a.cmd == "bridge-ok":
        # Sol r2: right before an alpha build is published (also one resumed from an open intent, its version kept): the bridge is
        # still there and verified, and still on the page once this release is added (margin 1: at most this one release came since)
        key = bytes.fromhex(git(a.git_dir, "show", f"{a.sha}:launcher/release_key.pub").strip())
        git(a.git_dir, "fetch", "-q", a.remote, "main")
        live = Live(a.gh, a.repo, a.git_dir, a.remote, key)
        why, tag = bridge_check(live.releases(), live.verify, margin=1)
        print(f"AUTORELEASE bridge: {why or f'{tag} is in place'}")
        return 1 if why else 0
    if a.cmd == "intent-version":
        tags = [line.split("\t", 1)[1] for line in sys.stdin.read().splitlines() if "\t" in line]
        return 0 if intent_version_ok(a.version, a.last, tags) else 1
    if a.cmd == "legacy-needed":
        old = format2_only_launchers(a.git_dir, remote_tags(a.git_dir, a.remote))
        if old:
            print(f"AUTORELEASE: published launchers read manifest format 2 only ({', '.join(old)}): a release without the launcher archives "
                  f"(format 3) would strand them")
        return 0 if old else LEGACY_NOT_NEEDED
    if a.cmd == "auto-publish":
        return 0 if auto_publish_ok(a.version) else 1
    if a.cmd == "title":
        print(release_title(a.version))
        return 0
    if a.cmd == "newer":
        return 0 if version_key(a.a) > version_key(a.b) else 1
    if a.cmd == "changed":
        files = release_changed(a.git_dir, a.a, a.b)
        print(f"{len(files)} release files changed" + (f" (first: {files[0]})" if files else ""))
        return 0 if files else 1
    if a.cmd == "warnings":
        rules = BUILTIN + (load_denylist(a.denylist) if a.denylist else [])
        for c in first_parent_commits(a.git_dir, a.a, a.b):
            for w in release_warnings(c):
                print(sanitize(w, rules))
        return 0
    text, lines = release_notes(a.version, first_parent_commits(a.git_dir, a.a, a.b), load_known_issues(a.known_issues), a.commit, a.date, a.repo, a.titles,
                                not a.no_legacy_launchers, a.mac, a.mac_manifest)
    a.out.write_text(text, encoding="utf-8")
    if a.title_out:
        a.title_out.write_text(title_summary(lines) + "\n", encoding="utf-8")
    if a.items_out:
        a.items_out.write_text("".join(f"- {markdown(x)}\n" for x in lines), encoding="utf-8")
    if a.plain_items_out:
        a.plain_items_out.write_text("".join(f"- {x}\n" for x in lines), encoding="utf-8")
    if a.sources_out:
        a.sources_out.write_text("".join(f"{s}\n" for s in note_sources(first_parent_commits(a.git_dir, a.a, a.b),
                                                                          load_known_issues(a.known_issues))), encoding="utf-8")
    print(f"NOTES {a.out}: {len(lines)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
