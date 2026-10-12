#!/usr/bin/env python3
"""Lane AUTOREL-1: the decisions of tools/release/autorelease.sh that are worth testing on their own (docs/RELEASE.md, "Automatic preview
releases").

    python3 tools/release/autorelease.py next-version [--remote origin] [--base v0.4.0] [--kind alpha] [--git-dir DIR]
        prints v0.4.0-alpha.<n>, n = 1 + the highest v0.4.0-alpha.<n> tag on the remote (git ls-remote); 1 when there is none. Tags
        outside the launcher's release grammar (make_manifest.TAG: no leading zeros, digits only) are ignored. Lane RELNAME-1 (owner,
        2026-10-10): the builds after v0.3.0-preview.4 are alpha builds; the line is v0.4.0 because SemVer orders "alpha" before
        "preview" (v0.3.0-alpha.1 would be older than v0.3.0-preview.4 and no launcher would offer it), so a version that is not above
        every release tag on the remote is refused.
    python3 tools/release/autorelease.py release-version --repo OWNER/NAME --sha SHA [--remote origin] [--gh gh] [--git-dir DIR]
        the version autorelease.sh releases: the next alpha build, or the next bridge v0.3.0-preview.<n> while the launchers up to
        v0.3.0-preview.4 have no verified bridge on the page they read (RELNAME-1 r2, release_version()).
    python3 tools/release/autorelease.py intent-version V [LAST]   exit 0 when an open intent's V is an alpha build or a bridge above LAST
    python3 tools/release/autorelease.py title V       the GitHub release's name ("OpenBFME v0.4.0 Alpha 1", release_title())
    python3 tools/release/autorelease.py newer A B     exit 0 when release version A is above B in the launcher's order, else 1
    python3 tools/release/autorelease.py changed --from A --to B [--git-dir DIR]
        exit 0 when something under RELEASE_PATHS changed between the two commits, 1 when nothing did (the release is skipped).
    python3 tools/release/autorelease.py notes --from A --to B --version V [--known-issues FILE] [--commit SHA] [--git-dir DIR]
        --out FILE [--title-out FILE] [--items-out FILE]
        the release notes for players from the first-parent subjects of A..B (see release_notes()).

Release notes. Every first-parent commit of A..B is one candidate (on the rebuild's branch a lane lands as one merge commit):
  * a `Release-note: <text>` line in its message is used as written (`Release-note: none` drops the commit; a text starting with
    `Fix:` goes under "What's fixed");
  * an integration round's lane merge ("Merge lane/x (<sha>) into integ/pN: ...") gives no note: the round's release-notes commit
    carries the player lines;
  * otherwise a commit that changes nothing a player runs (PLAYER_PATHS minus tests) is dropped, and the subject is reduced to plain
    words: the lane id, parenthesised details (binary addresses, stop ids, review verdicts) and everything after the first ';' are cut;
    a long first clause keeps only its head before ':'. Subjects that still read as lane jargon after that are dropped.
  * a line that says something got fixed (fix, regression, crash, no longer, stop ..., instead of) goes under "What's fixed".
Known issues come from known_issues.json (the README_TESTERS list, already in a player's words).
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
import os
import re
import subprocess
import sys
import tempfile
import hashlib
import io
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
import zlib  # noqa: E402

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
# the public main's top level (the sync recipe: the archive sha's tree without archive/, plus PRIVACY.md); anything else is refused
PUBLIC_TOP = frozenset({".gitattributes", ".gitignore", ".gitmodules", "CLAUDE.md", "LICENSE", "NOTICE", "README.md", "PRIVACY.md",
                        "build.bat", "docs", "engine", "godot", "launcher", "pytest.ini", "run_tests.bat", "tools"})
PRIVATE_DIRS = frozenset({"workspace", "archive", "reference", "references"})
URL = re.compile(r"(?i)\b[a-z][a-z0-9+.-]*://\S*|\bwww\.\S+|\bmailto:\S+")
MD_SPECIAL = re.compile(r"([\\`*_{}\[\]()!|~+\-.:#])")


def git(git_dir: str | None, *args: str) -> str:
    cmd = ["git"] + (["-C", git_dir] if git_dir else []) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


# ---- version --------------------------------------------------------------------------------------------------------------------------
BASE_VERSION = "v0.4.0"
KIND = "alpha"


def next_version(tag_names: list[str], base: str = BASE_VERSION, kind: str = KIND) -> str:
    """1 + the highest <base>-<kind>.<n> among tag_names (ls-remote names, refs/tags/ and ^{} allowed); <base>-<kind>.1 when none.
    Refused when the result is not above every release tag in tag_names (the launcher installs only a newer version automatically)."""
    if not TAG.fullmatch(base) or "-" in base:
        raise SystemExit(f"autorelease: base {base!r} is not v<major>.<minor>.<patch>")
    if kind not in ("alpha", "preview"):
        raise SystemExit(f"autorelease: kind {kind!r} is not alpha or preview")
    high, tags = 0, []
    for name in tag_names:
        name = name.removeprefix("refs/tags/").removesuffix("^{}")
        m = TAG.fullmatch(name)
        if not m:
            continue
        tags.append(name)
        if m.group(4) == kind and name.startswith(f"{base}-{kind}."):
            high = max(high, int(m.group(5)))
    version = f"{base}-{kind}.{high + 1}"
    newer = sorted({t for t in tags if version_key(t) >= version_key(version)}, key=version_key)
    if newer:
        raise SystemExit(f"autorelease: {version} would not be above the release tag {newer[-1]} (the launcher's order: a pre-release "
                         f"below its release, alpha below preview): installed launchers would never offer it; raise the base version")
    return version


def remote_tags(git_dir: str | None, remote: str) -> list[str]:
    """every tag on the remote (next_version compares the new version with all of them)"""
    out = git(git_dir, "ls-remote", "--tags", remote, "refs/tags/v*")
    return [line.split("\t", 1)[1] for line in out.splitlines() if "\t" in line]


def release_title(version: str) -> str:
    """the GitHub release's name: "OpenBFME v0.4.0 Alpha 1" for an alpha build (the owner, RELNAME-1); a preview or stable release
    keeps the plain tag, as the earlier previews were named"""
    m = TAG.fullmatch(version)
    if not m:
        raise SystemExit(f"autorelease: {version!r} is not a release version")
    if m.group(4) == "alpha":
        return f"OpenBFME v{m.group(1)}.{m.group(2)}.{m.group(3)} Alpha {m.group(5)}"
    return f"OpenBFME {version}"


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
    """'alpha' (v0.4.0-alpha.<n>), 'bridge' (v0.3.0-preview.<n>) or '' for a version automatic releases never make"""
    m = TAG.fullmatch(version)
    if m and m.group(4) == "alpha" and version.startswith(BASE_VERSION + "-alpha."):
        return "alpha"
    if m and m.group(4) == "preview" and version.startswith(BRIDGE_BASE + "-preview."):
        return "bridge"
    return ""


def intent_version_ok(version: str, last: str, tag_names: list[str] = ()) -> bool:
    """an open intent's version: of one of the two lines, and above every version of its line already released or tagged (the last
    released one and every tag on the remote but its own: Sol r2, after alpha.1 and a bridge an intent edited to alpha.0 was published;
    an alpha build and a bridge are on separate lines, so alpha.3 then the bridge preview.6 is forward)"""
    line = line_of(version)
    if not line:
        return False
    older = [n for n in _tag_names(list(tag_names)) if n != version and line_of(n) == line] + ([last] if line_of(last) == line else [])
    return all(version_key(version) > version_key(n) for n in older)


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


def release_version(tags: list[str], releases: list[dict], verify, sha_semver: str) -> tuple[str, str]:
    """(the version of this automatic release, why): the next alpha build when the old launchers have their bridge, else the next
    bridge, which needs this sha's launcher to know -alpha"""
    why, tag = bridge_check(releases, verify)
    if not why:
        return next_version(tags), f"the bridge {tag} is in place"
    if not knows_alpha(sha_semver):
        raise SystemExit(f"autorelease: a bridge is needed ({why}) but this sha's launcher does not know -alpha tags")
    return next_bridge(tags), why


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
    """a neutral() line as literal Markdown text: HTML entities for & < >, a backslash before every other special character"""
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return MD_SPECIAL.sub(r"\\\1", text)


def notes_of(subject: str, body: str, files: list[str]) -> list[tuple[str, str]]:
    """Every (section, line) of one first-parent commit. A commit with several `Release-note:` lines gives one note per line (a
    release-notes commit lists a whole preview's news this way); `Release-note: none` anywhere drops the commit; without any
    Release-note line it is note_of's single generated line, if any."""
    texts = [m.group(1).strip() for m in (re.match(r"^Release-note:\s*(.*)$", ln.strip(), re.I) for ln in body.splitlines()) if m]
    if not texts:
        n = note_of(subject, body, files)
        return [n] if n else []
    if any(not t or t.lower() == "none" for t in texts):
        return []
    cap = lambda t: t[:1].upper() + t[1:]              # "Fix: a crash ..." printed "a crash ..." (preview.4)
    return [("fixed", cap(t[4:].strip())) if t.lower().startswith("fix:") else (("fixed" if FIXED.search(t) else "new"), cap(t))
            for t in texts]


def note_of(subject: str, body: str, files: list[str]) -> tuple[str, str] | None:
    """(section, line) of one first-parent commit: section 'new' or 'fixed'; None when it is not news for a player"""
    for line in body.splitlines():
        m = re.match(r"^Release-note:\s*(.*)$", line.strip(), re.I)
        if m:
            text = m.group(1).strip()
            if not text or text.lower() == "none":
                return None
            if text.lower().startswith("fix:"):
                return "fixed", text[4:].strip()
            return ("fixed" if FIXED.search(text) else "new"), text
    if not any(f.startswith(PLAYER_PATHS) and not f.startswith(TEST_PATHS) for f in files):
        return None
    line = player_line(subject)
    if line is None:
        return None
    return ("fixed" if FIXED.search(line) else "new"), line


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


def release_notes(version: str, commits: list[dict], known_issues: list[str], commit: str = "", date: str = "") -> tuple[str, list[str]]:
    """(markdown notes, the neutral() lines in order)"""
    new, fixed, seen = [], [], set()
    for c in commits:
        for n in notes_of(c["subject"], c.get("body", ""), c.get("files", [])):
            line = neutral(n[1])
            if line and line not in seen:
                seen.add(line)
                (fixed if n[0] == "fixed" else new).append(line)
    m = TAG.fullmatch(version)
    kind = m.group(4) if m else None
    # RELNAME-1 r2: an automatic preview is a bridge for the launchers up to v0.3.0-preview.4 (release_version())
    out = [f"OpenBFME {version}" + {"alpha": " is an alpha build for testers.", "preview": " is an alpha build for testers, numbered "
           "as a preview so that launchers of v0.3.0-preview.4 and older can update themselves: after it, they offer the alpha builds."
           }.get(kind, "."),
           "Install or update it with the OpenBFME Launcher. It uses your installed The Rise of the Witch-king 2.01 and "
           "The Battle for Middle-earth II 1.06, or downloads them for you.", ""]   # said once, not preachy (owner, 2026-10-10/11)
    if new:
        out += ["## What's new", ""] + [f"- {markdown(x)}" for x in new] + [""]
    if fixed:
        out += ["## What's fixed", ""] + [f"- {markdown(x)}" for x in fixed] + [""]
    if not new and not fixed:
        out += ["No changes you will notice in the game: this build updates the release tooling and internal parts.", ""]
    if known_issues:
        out += ["## Known issues", ""] + [f"- {markdown(neutral(x))}" for x in known_issues] + [""]
    if commit:
        out += [f"Built from commit {markdown(neutral(commit[:10]))}" + (f" ({markdown(neutral(date))})." if date else ".")]
    return "\n".join(out).rstrip() + "\n", new + fixed


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
class SyncAudit(audit_package.Audit):
    """audit_package's recursive audit plus raw zlib streams (no gzip header), which the package audit does not open. Every decoded
    layer goes back through this blob() (r4, Sol r3: a twice-zlib-wrapped BIG passed because the decoded layer skipped the zlib check),
    and so do the gzip / bzip2 / xz / zip / tar / pack members the package audit opens (it recurses through self.blob). Bounds:
    MAX_DEPTH layers, MAX_UNPACKED bytes per layer; anything beyond a bound is a finding. A binary file with a valid zlib header must
    decode to its end, or it is a finding (fail closed); text with a zlib-looking start ("x^...") is decoded when it can be, and is
    text otherwise."""

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
                self.files += 1
                self.blob(f"{name}[zlib]", out, depth + 1)
                return
            if not text:
                self.problems.append(problem)
                return
        super().blob(name, data, depth)


def _is_text(data: bytes) -> bool:
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return b"\x00" not in data


ALLOWLIST = HERE / "public_binaries.txt"
CONTAINER_SUFFIXES = (".zip", ".jar", ".apk", ".whl", ".egg", ".nupkg", ".gz", ".tgz", ".tar", ".bz2", ".tbz2", ".xz", ".txz", ".lzma",
                      ".7z", ".rar", ".cab", ".zst", ".lz4", ".lzo", ".z", ".zlib", ".pck", ".big", ".dmg", ".iso", ".cpio")


def load_allowlist(path: Path = ALLOWLIST) -> dict[str, str]:
    """{path: sha256} of the containers the public tree may hold: lines `<sha256>  <path>  <reason>`, # comments; a malformed line,
    a missing reason or a path listed twice is an error (fail closed)"""
    out: dict[str, str] = {}
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
            if data[3] & 0x1C:
                probs.append(f"{name}: a gzip header with a file name, comment or extra field")
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
KEY_PATH = Path.home() / ".config" / "openbfme-release" / "release-ed25519.pem"


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
        _PRIVATE_WORDS = ({u for u in users if len(u) >= 2}, {h for h in hosts if len(h) >= 2 and h != "localhost"})
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
    key = str(KEY_PATH)
    secrets = {key, str(KEY_PATH.parent), "~/.config/openbfme-release", KEY_PATH.name, str(Path.home())}
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
                problems.add(f"a user name ({u})")
        for h in hosts:
            if re.search(rf"(?<![a-z0-9_.-]){re.escape(h)}(?![a-z0-9_-])", low):
                problems.add(f"a host name ({h})")
        for e in EMAIL.findall(f):
            if not NOREPLY.match(e):
                problems.add("an e-mail address that is not the noreply one")
    return [f"{name}: {p}" for p in sorted(problems)]


def audit_tree(git_dir: str | None, tree: str) -> list[str]:
    """the problems of a tree about to be pushed to the public repository (empty: it may go). Run before the first push (Sol r1: a tracked
    workspace/ file or a retail file outside archive/ went to the public branch)."""
    problems = []
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
        inner = SyncAudit()
        inner.blob(path, data)
        problems += [f"inside a container: {p}" for p in inner.problems]
    for _, _, _, path in entries:
        problem = path_problem(path)
        if problem:
            problems.append(problem)
    for mode, kind, oid, path in entries:
        if kind == "commit":
            problem = check_file(path, verb="in the public tree")
            if problem:
                problems.append(problem)
    return problems


def load_known_issues(path: Path) -> list[str]:
    return [i["text"] for i in json.loads(path.read_text(encoding="utf-8"))["issues"]]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("next-version")
    v.add_argument("--remote", default="origin")
    v.add_argument("--base", default=BASE_VERSION)
    v.add_argument("--kind", default=KIND, choices=("alpha", "preview"))
    rv = sub.add_parser("release-version", help="the version of this automatic release: the next alpha build or the next bridge")
    rv.add_argument("--remote", default="origin")
    rv.add_argument("--repo", required=True)
    rv.add_argument("--sha", required=True)
    rv.add_argument("--gh", default=os.environ.get("GH", "gh"))
    bo = sub.add_parser("bridge-ok", help="exit 0 when the old launchers' page still holds a verified bridge (before an alpha is published)")
    bo.add_argument("--remote", default="origin")
    bo.add_argument("--repo", required=True)
    bo.add_argument("--sha", required=True)
    bo.add_argument("--gh", default=os.environ.get("GH", "gh"))
    iv = sub.add_parser("intent-version", help="the remote's tags (git ls-remote lines) on stdin")
    iv.add_argument("version")
    iv.add_argument("last", nargs="?", default="")
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
    n.add_argument("--out", type=Path, required=True)
    n.add_argument("--title-out", type=Path)
    n.add_argument("--items-out", type=Path, help="the items as Markdown (PR body)")
    n.add_argument("--plain-items-out", type=Path, help="the items as neutral plain text (the sync commit's message)")
    t = sub.add_parser("audit-tree")
    t.add_argument("--tree", required=True)
    x = sub.add_parser("audit-text", help="the public metadata files: exit 1 on any privacy hit (no redaction)")
    x.add_argument("files", nargs="+", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "audit-text":
        problems = [p for f in a.files for p in public_text_problems(f.name, f.read_text(encoding="utf-8", errors="replace"))]
        for p in problems:
            print(f"METADATA AUDIT: {p}")
        print(f"METADATA AUDIT {'FAILED' if problems else 'ok'}: {len(a.files)} files")
        return 1 if problems else 0
    if a.cmd == "audit-tree":
        problems = audit_tree(a.git_dir, a.tree)
        for p in problems:
            print(f"SYNC AUDIT: {p}")
        print(f"SYNC AUDIT {'FAILED' if problems else 'ok'}: {a.tree}")
        return 1 if problems else 0
    if a.cmd == "next-version":
        print(next_version(remote_tags(a.git_dir, a.remote), a.base, a.kind))
        return 0
    if a.cmd == "release-version":
        key = bytes.fromhex(git(a.git_dir, "show", f"{a.sha}:launcher/release_key.pub").strip())
        git(a.git_dir, "fetch", "-q", a.remote, "main")
        live = Live(a.gh, a.repo, a.git_dir, a.remote, key)
        version, why = release_version(remote_tags(a.git_dir, a.remote), live.releases(), live.verify,
                                       git(a.git_dir, "show", f"{a.sha}:launcher/scripts/core/semver.gd"))
        print(f"AUTORELEASE line: {version} ({'an alpha build' if line_of(version) == 'alpha' else 'a bridge'}: {why})", file=sys.stderr)
        print(version)
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
    if a.cmd == "title":
        print(release_title(a.version))
        return 0
    if a.cmd == "newer":
        return 0 if version_key(a.a) > version_key(a.b) else 1
    if a.cmd == "changed":
        files = release_changed(a.git_dir, a.a, a.b)
        print(f"{len(files)} release files changed" + (f" (first: {files[0]})" if files else ""))
        return 0 if files else 1
    text, lines = release_notes(a.version, first_parent_commits(a.git_dir, a.a, a.b), load_known_issues(a.known_issues), a.commit, a.date)
    a.out.write_text(text, encoding="utf-8")
    if a.title_out:
        a.title_out.write_text(title_summary(lines) + "\n", encoding="utf-8")
    if a.items_out:
        a.items_out.write_text("".join(f"- {markdown(x)}\n" for x in lines), encoding="utf-8")
    if a.plain_items_out:
        a.plain_items_out.write_text("".join(f"- {x}\n" for x in lines), encoding="utf-8")
    print(f"NOTES {a.out}: {len(lines)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
