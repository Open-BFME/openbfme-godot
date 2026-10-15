"""Lane AUTOREL-1: tools/release/autorelease.sh, record_gate.sh, publish_release.sh --execute / --confirm-tag, the release notes and the
public sync's privacy audit. Everything runs against throwaway repositories: a fake archive repository holding copies of the release
scripts, a local bare repository as the public remote, gh_stand_in.py as gh, a throwaway key, and OPENBFME_RELEASE_STANDIN for the build
on JonathanPC and the Deck's package checks. Never the real key, the real repository or GitHub.
Round 2 (Sol r1 REJECT): each finding has a test here, real mode included (round 1 tested --plan only, and every real run aborted)."""
from __future__ import annotations

import fcntl
import gzip
import hashlib
import io
import zipfile
import zlib
import json
import re
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import autorelease  # noqa: E402
import ed25519  # noqa: E402
import make_manifest  # noqa: E402
from launcher_test_server import tiny_package  # noqa: E402

GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
TARGET = "Test/repo"
ARCHIVE_LINE = "previous codebase is kept in `archive/` as reference only.\n"


def git(repo: Path, *args: str) -> str:
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, env={**os.environ, **GIT_ENV})
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


def commit(repo: Path, files: dict[str, str | bytes], message: str, force: bool = False) -> str:
    for name, data in files.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_bytes(data if isinstance(data, bytes) else data.encode())
    git(repo, "add", *(["-f"] if force else []), *files)
    git(repo, "commit", "-q", "-m", message)
    return git(repo, "rev-parse", "HEAD")


def run(*cmd, env=None, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=600, env={**os.environ, **GIT_ENV, **(env or {})},
                          stdin=subprocess.DEVNULL, **kw)


def package_dir(d: Path, version: str, extra: bool = False, legacy: bool = True) -> Path:
    """the build's output as package.sh writes it (lane ONEZIP-1): per platform the package, the game with the launcher's executable
    in it, and with `legacy` the launcher-only archive holding the same launcher"""
    d.mkdir(parents=True, exist_ok=True)
    sums = ""
    for kind in ("game", "launcher") if legacy else ("game",):
        for platform in ("linux-x64", "windows-x64"):
            n = make_manifest.asset_name(kind, version, platform)
            ext = ".x86_64" if platform.startswith("linux") else ".exe"
            files = {"OpenBFMELauncher" + ext: f"the launcher of {version} for {platform}".encode()}
            if kind == "game":
                files["OpenBFME" + ext] = f"{n} bytes".encode()
                files["VERSION"] = f"OpenBFME {version}\n".encode()   # seal_package.py checks it names the version
            data = tiny_package(n, files)
            (d / n).write_bytes(data)
            sums += f"{hashlib.sha256(data).hexdigest()}  {n}\n"
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    (d / f"BUILDINFO-{version}.txt").write_text("python 3 zlib 1\n")
    if extra:
        (d / "payload.bin").write_bytes(b"not a package file")
    return d


def zip_of(members: dict[str, bytes]) -> bytes:
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for n, d in members.items():
            z.writestr(n, d)
    return b.getvalue()


class World:
    """the archive repository, the public remote, gh, the key, the state and the stand-in build"""

    def __init__(self, tmp: Path):
        self.tmp = tmp
        self.repo = tmp / "archive"
        self.remote = tmp / "public.git"
        self.state = tmp / "state"
        # r6: autorelease.sh takes only the configured key path, ~/.config/openbfme-release/release-ed25519.pem: the runs get a HOME of
        # their own whose configured key is a throwaway one
        self.home = tmp / "testhome"
        self.key = self.home / ".config" / "openbfme-release" / "release-ed25519.pem"
        self.key.parent.mkdir(parents=True)
        pub = run(sys.executable, HERE / "sign_manifest.py", "--generate-key", self.key)
        assert pub.returncode == 0, pub.stderr
        # the public repository: main with PRIVACY.md
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(self.remote)], check=True)
        seed = tmp / "seed"
        git(tmp, "init", "-q", "-b", "main", str(seed))
        commit(seed, {"PRIVACY.md": "privacy\n", "README.md": "public\n"}, "public start")
        git(seed, "push", "-q", str(self.remote), "main")
        # the archive repository: the release scripts, the launcher key, an engine file, the README line the sync replaces
        git(tmp, "init", "-q", "-b", "archive-legacy-codebase", str(self.repo))
        shutil.copytree(HERE, self.repo / "tools/release", ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"))
        shutil.copy2(REPO / "tools/precommit.py", self.repo / "tools/precommit.py")
        files = {"README.md": "OpenBFME\n" + ARCHIVE_LINE, "launcher/release_key.pub": pub.stdout.strip() + "\n",
                 # RELNAME-1: the launcher's release grammar (a bridge's launcher must know -alpha)
                 "launcher/scripts/core/semver.gd": (REPO / "launcher/scripts/core/semver.gd").read_text(),
                 "engine/a.cpp": "1\n", ".gitignore": "/workspace/\n*.secret\n", "archive/old.txt": "legacy\n",
                 "tools/release/known_issues.json": json.dumps({"issues": [{"stops": ["S-1"], "text": "Create-a-Hero is not available."}]})}
        for name, data in files.items():
            (self.repo / name).parent.mkdir(parents=True, exist_ok=True)
            (self.repo / name).write_text(data)
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "the first rebuild commit")
        self.first = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "remote", "add", "origin", str(self.remote))
        self.standin = tmp / "standin"
        self.standin.mkdir()
        (self.standin / "build").write_text(f"""#!{sys.executable}
import sys; sys.path.insert(0, {str(HERE)!r})
from pathlib import Path
from test_autorelease import package_dir
import os
package_dir(Path(sys.argv[3]), sys.argv[2], extra=os.environ.get("STANDIN_EXTRA") == "1",
            legacy=os.environ.get("OPENBFME_RELEASE_LEGACY_LAUNCHERS", "1") == "1")
""")
        (self.standin / "verify").write_text('#!/bin/bash\necho "stand-in verify $3"; [ "${STANDIN_TAMPERED:-0}" = 0 ]\n')
        for f in ("build", "verify"):
            (self.standin / f).chmod(0o755)
        self.gh = tmp / "bin" / "gh"
        self.gh.parent.mkdir()
        self.gh.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
        self.gh.chmod(0o755)
        self.gh_state = tmp / "gh.json"
        self.gh_log = tmp / "gh.log"

    def gate(self, sha: str) -> None:
        self.state.mkdir(exist_ok=True)
        with open(self.state / "gated.tsv", "a") as f:
            f.write(f"{sha}\ttest-gate\t2026-10-09T00:00:00Z\tALL PASS\n")

    def change(self, files: dict[str, str | bytes], message: str, force: bool = False) -> str:
        return commit(self.repo, files, message, force)

    def env(self, **extra) -> dict:
        return {"GH": str(self.gh), "FAKE_GH_STATE": str(self.gh_state), "FAKE_GH_REMOTE": str(self.remote), "FAKE_GH_LOG": str(self.gh_log),
                "OPENBFME_RELEASE_STATE": str(self.state), "OPENBFME_RELEASE_WORK": str(self.tmp / "work"),
                "OPENBFME_RELEASE_LOGS": str(self.tmp / "logs"), "HOME": str(self.home),
                # the stand-in build imports this module: pytest from the user site of the real home
                "PYTHONPATH": str(Path(pytest.__file__).resolve().parents[1]),
                "OPENBFME_RELEASE_STANDIN": str(self.standin), "BFME_COMMUNITY": str(self.tmp / "no-community"),
                "OPENBFME_RELEASE_MIN_COMMIT": self.first,
                "PATH": f"{self.gh.parent}:{os.environ['PATH']}", **extra}

    def release(self, sha: str, *args: str, **env) -> subprocess.CompletedProcess:
        return run("bash", self.repo / "tools/release/autorelease.sh", sha, "--repo", TARGET, "--remote", "origin", *args, env=self.env(**env))

    def gh_data(self) -> dict:
        return json.loads(self.gh_state.read_text()) if self.gh_state.exists() else {"prs": {}, "releases": []}

    def remote_refs(self, pattern: str) -> str:
        return git(self.repo, "ls-remote", str(self.remote), pattern)


@pytest.fixture
def world(tmp_path):
    return World(tmp_path)


# ---- real mode end to end (Sol r1: real mode always aborted) ------------------------------------------------------------------------------
def test_real_release_end_to_end(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: units walk around trees instead of through them (RW 0x123456))")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "AUTORELEASE OK v0.4.5" in r.stdout and "stand-in verify v0.4.5" in r.stdout
    gh = world.gh_data()
    (rel,) = gh["releases"]
    assert rel["tag_name"] == "v0.4.5" and rel["draft"] is False and rel["prerelease"] is False
    assert rel["name"] == "OpenBFME Alpha 0.4.5", "VERSION-1: the first release is a plain number, a full release (no bridge any more)"
    assert sorted(a["name"] for a in rel["assets"]) == sorted(
        [make_manifest.asset_name(k, "v0.4.5", p) for k in ("game", "launcher") for p in ("linux-x64", "windows-x64")]
        + ["SHA256SUMS-v0.4.5.txt", "manifest.json", "manifest.json.sig"])
    assert "Units walk around trees instead of through them" in rel["body"]
    # lane ONEZIP-1: the notes say which file to get, with a direct link per platform (never "(link removed)"), and name the rest once
    base = f"https://github.com/{TARGET}/releases/download/v0.4.5"
    for name in ("openbfme-v0.4.5-windows-x64.zip", "openbfme-v0.4.5-linux-x64.tar.gz"):
        assert f"| **[{name}]({base}/{name})** | **OpenBFME for " in rel["body"], rel["body"]
    assert "## Which file to download" in rel["body"] and "(link removed)" not in rel["body"]
    assert ("| openbfme-launcher-v0.4.5-windows-x64.zip, openbfme-launcher-v0.4.5-linux-x64.tar.gz, "
            "SHA256SUMS-v0.4.5.txt, manifest.json, manifest.json.sig | Used by OpenBFME to update itself") in rel["body"]
    # the PR merged on the public main, its tree the archive's without archive/ plus PRIVACY.md and the README line; the branch deleted
    (pr,) = gh["prs"].values()
    main_tree = git(world.repo, "ls-tree", "--name-only", f"{pr['merge']}")
    assert "archive" not in main_tree.split() and "PRIVACY.md" in main_tree.split()
    assert "legacy-final" in git(world.remote, "show", f"{pr['merge']}:README.md")
    assert world.remote_refs("refs/heads/sync/*") == ""
    assert world.remote_refs("refs/tags/v0.4.5")
    assert (world.state / "last-released").read_text().split()[:2] == [sha, "v0.4.5"]
    # the publish steps: a draft, the uploads, the read-back, then published; gh never asked to create with assets
    log = world.gh_log.read_text()
    assert "release create" not in log and "-F draft=true" in log and "-F draft=false" in log and "release upload v0.4.5" in log
    # RELTEST-1: the About link points at the new release (read back)
    assert gh["repo"]["homepage"] == f"https://github.com/{TARGET}/releases/tag/v0.4.5", gh.get("repo")
    assert "AUTORELEASE homepage: the About link is" in r.stdout
    # forward only: the same sha, and an older one, are refused whatever --since says
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "is the last released archive sha" in r.stdout
    world.gate(world.first)
    r = world.release(world.first, "--since", world.first)
    assert r.returncode != 0 and "is older than the last released archive sha" in r.stdout
    # a commit that changes nothing a release is made of is skipped
    docs = world.change({"docs/A.md": "a\n"}, "docs only")
    world.gate(docs)
    r = world.release(docs)
    assert r.returncode == 0 and "AUTORELEASE SKIP" in r.stdout
    # the next one takes the next number and its notes start at the last release
    nxt = world.change({"engine/a.cpp": "3\n"}, "Merge lane/y (Y-1: archers no longer shoot through walls)")
    world.gate(nxt)
    r = world.release(nxt)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    rel = world.gh_data()["releases"][1]
    body = rel["body"]
    assert "Archers no longer shoot through walls" in body and "Units walk around trees" not in body
    # VERSION-1: the next plain number, published as a full release (channel stable) and named as players read it
    assert rel["tag_name"] == "v0.4.6" and rel["prerelease"] is False and rel["name"] == "OpenBFME Alpha 0.4.6", rel
    assert body.startswith("# OpenBFME Alpha 0.4.6 patch notes\n") and "-alpha." not in body.split("## Which file")[0]
    assert world.gh_data()["repo"]["homepage"] == f"https://github.com/{TARGET}/releases/tag/v0.4.6"


def test_a_failed_homepage_update_is_a_warning(world):
    """RELTEST-1: the release is out when the About link cannot be set: the run passes and the summary says to set it by hand"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT="homepage-fail")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE WARNING: the repository's About link could not be set" in r.stdout
    gh = world.gh_data()
    assert gh["releases"][0]["draft"] is False and gh.get("repo", {}).get("homepage", "") == ""


def test_gate_record_is_required(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "has no gate record" in r.stdout
    # the gate record of an ancestor does not stand in for it
    world.gate(world.first)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "has no gate record" in r.stdout
    assert not world.gh_state.exists() and world.remote_refs("refs/heads/sync/*") == ""


def test_plan_rules(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--plan")
    assert r.returncode != 0 and "pass --since" in r.stdout
    # VERSION-1: after the previews the next release is the first plain number (RELNAME-1 made a bridge here)
    git(world.repo, "tag", "v0.3.0-preview.4", world.first)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.3.0-preview.4")
    r = world.release(sha, "--plan", "--since", world.first)
    assert r.returncode == 0 and f"AUTORELEASE PLAN v0.4.5 from {sha}" in r.stdout, r.stdout + r.stderr
    assert "a plain release: every published launcher reads it" in r.stdout + r.stderr, r.stdout + r.stderr
    # only commits of the archive branch
    git(world.repo, "checkout", "-q", "-b", "side", world.first)
    side = world.change({"engine/b.cpp": "x\n"}, "not gated")
    git(world.repo, "checkout", "-q", "archive-legacy-codebase")
    world.gate(side)
    r = world.release(side, "--plan", "--since", world.first)
    assert r.returncode != 0 and "is not on archive-legacy-codebase" in r.stdout
    # one release at a time
    with open(world.state / "autorelease.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        r = world.release(sha, "--plan", "--since", world.first)
    assert r.returncode != 0 and "another autorelease is running" in r.stdout


def test_plain_releases_follow_the_alpha_builds_without_a_bridge(world):
    """VERSION-1 (owner, 2026-10-14): after the previews and alpha builds the releases are plain numbers, v0.4.5 first (above every
    v0.4.0-alpha.<n>), each one the next last number, --minor the next middle number; full releases (channel stable) named "OpenBFME
    Alpha 0.4.5". No v0.3.0-preview.<n> bridge is made any more: the launchers up to v0.3.0-preview.4 read plain tags (a bridge would
    sort below every plain release)."""
    for t in ("v0.3.0-preview.4", "v0.3.0-preview.6", "v0.4.0-alpha.4"):
        git(world.repo, "tag", t, world.first)
        git(world.repo, "push", "-q", "origin", f"refs/tags/{t}")
    shas = []
    for n in range(2, 5):
        shas.append(world.change({"engine/a.cpp": f"{n}\n"}, f"X-{n}: change {n}"))
        world.gate(shas[-1])
    a, b, c = shas
    r = world.release(a, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout and "a plain release" in r.stdout + r.stderr, r.stdout + r.stderr
    rel = world.gh_data()["releases"][-1]
    assert (rel["tag_name"], rel["name"], rel["prerelease"]) == ("v0.4.5", "OpenBFME Alpha 0.4.5", False)
    mid = next(x["id"] for x in rel["assets"] if x["name"] == "manifest.json")
    m = json.loads((Path(str(world.gh_state) + ".d") / str(mid)).read_bytes())
    assert m["version"] == "v0.4.5" and m["channel"] == "stable" and m["format"] == 2 and sorted(m["assets"]) == ["game", "launcher"], \
        "the launcher archives stay (the launchers before ONEZIP-1 read format 2 only)"
    assert autorelease.manifest_problems(m, "v0.4.5", TARGET, False) == [], "what every launcher checks: stable channel, no pre-release flag"
    r = world.release(b, "--plan")
    assert r.returncode == 0 and "AUTORELEASE PLAN v0.4.6" in r.stdout, r.stdout + r.stderr
    r = world.release(b, "--plan", "--minor")
    assert r.returncode == 0 and "AUTORELEASE PLAN v0.5.0" in r.stdout, r.stdout + r.stderr
    r = world.release(b, "--minor")
    assert r.returncode == 0 and "AUTORELEASE OK v0.5.0" in r.stdout, r.stdout + r.stderr
    r = world.release(c, "--plan")
    assert r.returncode == 0 and "AUTORELEASE PLAN v0.5.1" in r.stdout, r.stdout + r.stderr
    assert [x["tag_name"] for x in world.gh_data()["releases"]] == ["v0.4.5", "v0.5.0"]
    assert [x["prerelease"] for x in world.gh_data()["releases"]] == [False, False]


def test_bridge_rules():
    ok = lambda r: ""
    rel = lambda t, draft=False: {"tag_name": t, "draft": draft, "prerelease": "-" in t}
    assert autorelease.bridge_check([], ok) == ("no release on the first 30 an old launcher can read", "")
    page = [rel(f"v0.4.0-alpha.{n}") for n in range(30, 0, -1)] + [rel("v0.3.0-preview.5")]
    assert autorelease.bridge_check(page, ok)[0].startswith("no release on the first 30"), "the bridge is entry 31: old launchers miss it"
    page = [rel("v0.4.0-alpha.2", draft=True)] + [rel(f"v0.4.0-alpha.{n}") for n in range(24, 0, -1)] + [rel("v0.3.0-preview.5"),
                                                                                                         rel("v0.3.0-preview.4")]
    assert autorelease.bridge_check(page, ok) == ("", "v0.3.0-preview.5"), "drafts are not on the anonymous page"
    assert autorelease.bridge_check(page, lambda r: "nope")[0] == "the newest release an old launcher installs, v0.3.0-preview.5, is not a bridge: nope"
    # the newest tag an old launcher parses is the one it installs, wherever it is on the page
    assert autorelease.bridge_check([rel("v0.3.0-preview.4"), rel("v0.3.0-preview.6")], lambda r: "" if r["tag_name"].endswith(".6") else "x") \
        == ("", "v0.3.0-preview.6")
    assert autorelease.next_bridge(["refs/tags/v0.3.0-preview.4", "refs/tags/v0.4.0-alpha.7", "refs/tags/v0.3.0-preview.5^{}"]) == "v0.3.0-preview.6"
    with pytest.raises(SystemExit, match="would not be above v0.3.1"):
        autorelease.next_bridge(["refs/tags/v0.3.0-preview.4", "refs/tags/v0.3.1"])
    # Sol r2: the grammar is run (Godot, sample tags), not searched for a word
    assert autorelease.knows_alpha((REPO / "launcher/scripts/core/semver.gd").read_text())
    for old in ("42f8a8d2", "2a422ce6"):   # the launchers of v0.3.0-preview.1/.2 and .3/.4
        src = subprocess.run(["git", "-C", REPO, "show", f"{old}:launcher/scripts/core/semver.gd"], capture_output=True, text=True)
        assert src.returncode == 0, f"the launcher of {old} is not in this clone"
        assert not autorelease.knows_alpha(src.stdout), old
        hacked = src.stdout.replace('(0|[1-9][0-9]{0,8}))?$"', '(0|[1-9][0-9]{0,8}))?$"  # alpha', 1)
        assert hacked != src.stdout and not autorelease.knows_alpha(hacked), "a comment saying alpha is no alpha grammar"
    assert not autorelease.knows_alpha("this is not gdscript"), "a semver.gd that does not parse"
    assert not autorelease.knows_alpha("extends RefCounted\nstatic func is_valid(t): return [][1]\n"), "one that stops on an error"
    lax = (REPO / "launcher/scripts/core/semver.gd").read_text().replace("(?:-(alpha|preview)", "(?:-(alpha|beta|preview)")
    assert lax != (REPO / "launcher/scripts/core/semver.gd").read_text()
    assert not autorelease.knows_alpha(lax), "a grammar that takes more than the launchers' is not the alpha grammar"
    iv = autorelease.intent_version_ok
    assert iv("v0.3.0-preview.5", "v0.3.0-preview.4") and iv("v0.4.0-alpha.1", "v0.3.0-preview.5") and iv("v0.3.0-preview.6", "v0.4.0-alpha.3")
    assert not iv("v0.4.0-alpha.2", "v0.4.0-alpha.2") and not iv("v0.3.0-preview.5", "v0.3.0-preview.6") and not iv("v0.5.0-alpha.1", "")
    assert iv("v0.4.0-alpha.1", "") and not iv("v0.3.1-preview.1", "")
    # Sol r2: every released or tagged version of the line counts, not only the last released one (another line's)
    tags = ["refs/tags/v0.3.0-preview.5", "refs/tags/v0.4.0-alpha.1", "refs/tags/v0.4.0-alpha.1^{}", "refs/tags/v0.3.0-preview.6"]
    assert not iv("v0.4.0-alpha.0", "v0.3.0-preview.6", tags) and not iv("v0.4.0-alpha.0", "v0.3.0-preview.6", tags[:3])
    assert iv("v0.4.0-alpha.2", "v0.3.0-preview.6", tags) and iv("v0.4.0-alpha.2", "v0.3.0-preview.6", tags + ["refs/tags/v0.4.0-alpha.2"])
    assert not iv("v0.3.0-preview.6", "v0.4.0-alpha.1", tags[:3] + ["refs/tags/v0.3.0-preview.7"])
    # VERSION-1: the plain line; once a plain release is tagged or released, an alpha build or a bridge is no intent any more
    assert iv("v0.4.5", "v0.4.0-alpha.4") and iv("v0.4.5", "v0.3.0-preview.6", tags) and iv("v0.5.0", "v0.4.5") and iv("v0.4.6", "v0.4.5")
    assert not iv("v0.4.5", "v0.4.5") and not iv("v0.4.5", "v0.4.6") and not iv("v0.4.4", "") and not iv("v0.4.05", "")
    assert not iv("v0.4.6", "v0.4.0-alpha.4", tags + ["refs/tags/v0.4.7"]) and iv("v0.4.8", "v0.4.5", tags + ["refs/tags/v0.4.7"])
    assert not iv("v0.4.0-alpha.5", "v0.4.5") and not iv("v0.4.0-alpha.5", "v0.4.0-alpha.4", ["refs/tags/v0.4.5"])
    assert not iv("v0.3.0-preview.7", "v0.4.5") and not iv("v0.3.0-preview.7", "", ["refs/tags/v0.4.5^{}"])


def test_bridge_manifest_rules(world, tmp_path):
    """Sol r2: a bridge must be what the launchers accept: the signed manifest's schema, its channel and the release's pre-release flag,
    every kind and platform (VERSION-1: the rule only guards an alpha build resumed from an old intent; the manifest is made directly)"""
    d = publish_dir(tmp_path, "v0.3.0-preview.5", world, git(world.repo, "rev-parse", "HEAD"))
    m = json.loads((d / "manifest.json").read_bytes())
    mp = autorelease.manifest_problems
    assert mp(m, "v0.3.0-preview.5", TARGET, True) == []
    assert "does not match the channel" in " ".join(mp(m, "v0.3.0-preview.5", TARGET, False))
    assert "names" in " ".join(mp(m, "v0.3.0-preview.6", TARGET, True)) and mp(m, "v0.3.0-preview.5", "Other/repo", True)
    for change, why in [(lambda x: x["assets"]["launcher"].pop("windows-x64"), "assets.launcher are not"),
                        (lambda x: x["assets"]["launcher"]["linux-x64"].update(name="x.tar.gz"), "is named"),
                        (lambda x: x["assets"]["launcher"]["linux-x64"]["files"].clear(), "lists no OpenBFMELauncher.x86_64"),
                        (lambda x: x["assets"]["game"]["windows-x64"].update(size=-1), "has no size"),
                        (lambda x: x.update(channel="stable"), "channel 'stable'"),
                        (lambda x: x.update(extra=1), "keys"), (lambda x: x.update(format=1), "format")]:
        y = json.loads(json.dumps(m))
        change(y)
        assert why in " ".join(mp(y, "v0.3.0-preview.5", TARGET, True)), why


def test_an_open_plain_intent_keeps_its_version(world):
    """Sol r2 (RELNAME-1), for the plain releases (VERSION-1): a release interrupted after its tag is resumed with its reserved version;
    an intent's version edited below a version of its line already tagged, or to an alpha build, is refused and changes nothing"""
    git(world.repo, "tag", "v0.4.0-alpha.4", world.first)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.4.0-alpha.4")
    shas = []
    for n in range(2, 4):
        shas.append(world.change({"engine/a.cpp": f"{n}\n"}, f"X-{n}: change {n}"))
        world.gate(shas[-1])
    a, b = shas
    assert "AUTORELEASE OK v0.4.5" in world.release(a, "--since", world.first).stdout
    r = world.release(b, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and world.remote_refs("refs/tags/v0.4.6") and "version=v0.4.6" in (world.state / "intent").read_text(), \
        r.stdout + r.stderr
    intent = world.state / "intent"
    text = intent.read_text()
    before = json.dumps(world.gh_data()["releases"])
    for wrong in ("v0.4.4", "v0.4.0-alpha.5", "v0.3.0-preview.7"):
        intent.write_text(text.replace("version=v0.4.6", f"version={wrong}"))
        r = world.release(b)
        assert r.returncode != 0 and json.dumps(world.gh_data()["releases"]) == before, (wrong, r.stdout + r.stderr)
    intent.write_text(text.replace("version=v0.4.6", "version=v0.4.5"))   # a published one: refused by the reconcile
    r = world.release(b)
    assert r.returncode != 0 and json.dumps(world.gh_data()["releases"]) == before, r.stdout + r.stderr
    intent.write_text(text)
    r = world.release(b, "--minor")   # an open intent keeps its version: --minor does not move it
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    rel = world.gh_data()["releases"][-1]
    assert (rel["tag_name"], rel["prerelease"], rel["name"]) == ("v0.4.6", False, "OpenBFME Alpha 0.4.6")


def test_standin_is_refused_for_the_real_repository(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something")
    world.gate(sha)
    r = run("bash", world.repo / "tools/release/autorelease.sh", sha, "--since", world.first, "--plan", env=world.env())
    assert r.returncode != 0 and "OPENBFME_RELEASE_STANDIN is for tests" in r.stdout


# ---- the public sync's privacy audit (Sol r1: a tracked workspace/ file and a retail file were pushed) ------------------------------------
@pytest.mark.parametrize("files,why", [
    ({"workspace/private.secret": "s\n"}, "inside a private folder (workspace/)"),
    ({"docs/retail.big": b"BIGF\0\0\0\0"}, "retail-format file in the public tree"),
    ({"docs/notes.txt": "see /" + "home/deck/x\n"}, "developer-machine path in docs/notes.txt"),
    ({"engine/keys.secret": "k\n"}, "a .gitignore rule ignores it"),
    ({"engine/tex.bin": b"DDS |\0\0\0"}, "retail-format bytes in the public tree (DDS texture)"),
    ({"private/x.txt": "x\n"}, "not a public top-level entry: private"),
    ({"tools/reference/x.txt": "x\n"}, "inside a private folder (reference/)"),
    # r3 (Sol r2): retail bytes inside containers
    ({"docs/content.bin": gzip.compress(b"BIGF\0\0\0\0retail-payload")}, "a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: docs/content.bin"),
    ({"docs/nested.gz.bin": gzip.compress(zip_of({"a/b.bin": b"BIGF\0\0\0\0retail"}))}, "a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: docs/nested.gz.bin"),
    ({"docs/raw.bin": zlib.compress(b"DDS |\0\0\0 texture")}, "a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: docs/raw.bin"),
    ({"docs/cut.bin": zlib.compress(b"\x01\x02" * 500)[:-4]}, "a binary file in the public tree"),
    ({"docs/seven.bin": b"7z\xbc\xaf\x27\x1c" + b"\0" * 32}, "a binary file in the public tree"),
])
def test_sync_privacy_audit(world, files, why):
    sha = world.change(files, "tracked anyway", force=True)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "failed the privacy audit: nothing pushed" in r.stdout and why in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()


def test_audit_tree_of_the_real_public_tree():
    tree = subprocess.run("git ls-tree HEAD | grep -vP '\\tarchive$' | git mktree", shell=True, cwd=REPO, capture_output=True, text=True,
                          check=True).stdout.strip()
    assert autorelease.audit_tree(str(REPO), tree) == []


# ---- the returned artifacts (Sol r1: whatever arrived was signed) -----------------------------------------------------------------------
def test_returned_artifacts_are_checked_before_signing(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and "at step deck-verify" in r.stdout
    assert not list((world.tmp / "work").rglob("manifest.json.sig")) and world.gh_data()["releases"] == []
    # the tag stays (no release was created for it) and the release intent stays open: the next run resumes that sha and tag
    assert world.remote_refs("refs/tags/v0.4.5") and (world.state / "intent").exists()
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout and "already on origin" in r.stdout, r.stdout + r.stderr
    assert not world.remote_refs("refs/tags/v0.4.6") and not (world.state / "intent").exists()
    # an extra file among the returned ones
    nxt = world.change({"engine/a.cpp": "3\n"}, "X-2: something else new for players")
    world.gate(nxt)
    r = world.release(nxt, "--since", world.first, STANDIN_EXTRA="1")
    assert r.returncode != 0 and "is not exactly the package files" in r.stdout
    assert [x["tag_name"] for x in world.gh_data()["releases"]] == ["v0.4.5"], "nothing of v0.4.6 was published"


def test_trusted_checkout_runs_the_deck_scripts(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    log = (world.tmp / "logs" / "autorelease-v0.4.5.log").read_text()
    assert "MANIFEST " + str(world.tmp / "work/v0.4.5/pkg/manifest.json") in log
    assert not (world.tmp / "work/v0.4.5/trusted").exists(), "the trusted worktree is removed after the run"


# ---- publishing: draft, assets checked, published, read back (Sol r1: gh's failure path could delete a published release) ---------------
@pytest.mark.parametrize("fault,ok,left,why", [
    ("publish-lost", True, 1, ""),
    ("upload-nodigest", True, 1, ""),
    ("publish-fail", False, 1, "is left as it is"),
    ("upload-corrupt", False, 1, "is left as it is"),
    ("upload-fail", False, 1, "is left as it is"),
    ("create-lost", False, 1, "left in place (not provably this run's)"),
])
def test_publish_outcomes_are_read_back(world, fault, ok, left, why):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT=fault)
    assert (r.returncode == 0) == ok, r.stdout + r.stderr
    rels = world.gh_data()["releases"]
    assert len(rels) == left
    assert why in r.stdout + r.stderr
    if ok:
        assert rels[0]["draft"] is False and (world.state / "last-released").exists()
    else:
        assert not (world.state / "last-released").exists()
        assert "--method DELETE" not in world.gh_log.read_text(), "the tool never deletes a release (r4)"


def publish_dir(tmp: Path, version: str, world: World, sha: str) -> Path:
    """a signed package folder for version, at a tagged commit of the archive repository (publish_release.sh run from there)"""
    git(world.repo, "tag", "-a", version, "-m", "t", sha)
    git(world.repo, "push", "-q", "origin", f"refs/tags/{version}")
    d = package_dir(tmp / f"pkg-{version}", version)
    assert run(sys.executable, world.repo / "tools/release/make_manifest.py", d, "--repo", TARGET, "--commit", sha).returncode == 0
    assert run(sys.executable, world.repo / "tools/release/sign_manifest.py", "--key", world.key, d / "manifest.json").returncode == 0
    return d


def test_confirm_tag_is_for_previews_only(world, tmp_path):
    """--confirm-tag: pre-releases and (VERSION-1) the plain alpha-stage releases v0.4.5 .. v0.x.y; every other stable release waits for
    the owner's typed tag"""
    sha = git(world.repo, "rev-parse", "HEAD")
    tool = world.repo / "tools/release/publish_release.sh"
    for version in ("v0.3.0", "v0.4.4", "v1.0.0"):
        stable = publish_dir(tmp_path, version, world, sha)
        r = run("bash", tool, stable, "--repo", TARGET, "--execute", "--confirm-tag", version, env=world.env())
        assert r.returncode != 0 and "--confirm-tag is for pre-releases and the alpha-stage releases" in r.stderr and not world.gh_state.exists(), version
        r = run("bash", tool, stable, "--repo", TARGET, "--execute", env=world.env())
        assert r.returncode != 0 and "not confirmed" in r.stderr and not world.gh_state.exists(), "a stable release waits for the typed tag"
    assert [v for v in ("v0.4.4", "v0.4.5", "v0.4.6", "v0.5.0", "v0.99.0", "v1.0.0", "v0.4.5-alpha.1", "v0.4.05") if autorelease.auto_publish_ok(v)] == \
        ["v0.4.5", "v0.4.6", "v0.5.0", "v0.99.0"]
    plain = publish_dir(tmp_path, "v0.4.5", world, sha)
    r = run("bash", tool, plain, "--repo", TARGET, "--execute", "--confirm-tag", "v0.4.5", env=world.env())
    assert r.returncode == 0 and "PUBLISHED v0.4.5" in r.stdout, r.stdout + r.stderr
    rel = next(x for x in world.gh_data()["releases"] if x["tag_name"] == "v0.4.5")
    assert rel["prerelease"] is False and rel["name"] == "OpenBFME Alpha 0.4.5", "a plain release is a full release, named Alpha 0.4.5"
    world.gh_state.unlink()
    world.gh_log.unlink()
    preview = publish_dir(tmp_path, "v0.3.0-preview.7", world, sha)
    r = run("bash", tool, preview, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.6", env=world.env())
    assert r.returncode != 0 and "is not the manifest's version" in r.stderr and not world.gh_state.exists()
    r = run("bash", tool, preview, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.7", env=world.env())
    assert r.returncode == 0 and "PUBLISHED v0.3.0-preview.7" in r.stdout, r.stdout + r.stderr
    # every check stays: another key's signature is refused before gh is asked anything
    world.gh_log.unlink()
    (preview / "manifest.json.sig").write_bytes(ed25519.sign(os.urandom(32), (preview / "manifest.json").read_bytes()))
    r = run("bash", tool, preview, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.7", env=world.env())
    assert r.returncode != 0 and "does not verify" in r.stderr and not world.gh_log.exists()


def test_publish_refuses_an_existing_release(world, tmp_path):
    sha = git(world.repo, "rev-parse", "HEAD")
    d = publish_dir(tmp_path, "v0.3.0-preview.8", world, sha)
    tool = world.repo / "tools/release/publish_release.sh"
    assert run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.8", env=world.env()).returncode == 0
    r = run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.8", env=world.env())
    assert r.returncode != 0 and "already exists" in r.stderr and len(world.gh_data()["releases"]) == 1


# ---- the Windows start (Sol r1: exit codes 124 and 139 passed) ----------------------------------------------------------------------------
@pytest.mark.parametrize("rc,ok", [(0, True), (124, False), (139, False), (1, False)])
def test_windows_start_needs_exit_0(tmp_path, rc, ok):
    log = tmp_path / "run.log"
    log.write_text("OpenBFME v0.3.0-preview.1 (2026-10-09)\nGAME object world: ok=true, 4657 templates, 7 playable factions, 3.3 s\n"
                   "GAME screen: MainMenu.apt\n")
    r = run("bash", HERE / "windows_start_check.sh", log, rc, 4657)
    assert (r.returncode == 0) == ok, r.stdout
    log.write_text("GAME object world: ok=true, 4656 templates\nGAME screen: MainMenu.apt\n")
    assert run("bash", HERE / "windows_start_check.sh", log, 0, 4657).returncode != 0


# ---- the Windows help box run (lane BUGS-1: Devlog #4's help box over a barracks bubble drew no text) ------------------------------------
HUD6_OK = ('HUD6 barracks (bubble Command_ConstructGondorFighterHorde): {"calls":["Show(69.688889)"],"draws":284,"name":"Gondor Soldiers",'
           '"shown":true,"state":3,"text_ops":5,"width":481}\nHUD6 barracks: ok\nHUD6 RESULT: 2 ok, 0 failed\n')


@pytest.mark.parametrize("rc,text,ok", [
    (0, HUD6_OK, True),
    (124, HUD6_OK, False),                                                     # a timeout
    (0, HUD6_OK.replace('"text_ops":5', '"text_ops":0'), False),               # the frame without text
    (0, HUD6_OK.replace('"name":"Gondor Soldiers"', '"name":""'), False),      # no help
    (0, HUD6_OK.replace("2 ok, 0 failed", "1 ok, 1 failed"), False),
    (0, HUD6_OK.replace("HUD6 barracks: ok", "HUD6 barracks: FAIL the help box drew no text"), False),
    (0, "", False),
])
def test_windows_hud6_check(tmp_path, rc, text, ok):
    log = tmp_path / "hud6.log"
    log.write_text(text)
    r = run("bash", HERE / "windows_hud6_check.sh", log, rc)
    assert (r.returncode == 0) == ok, r.stdout


# ---- record_gate.sh ---------------------------------------------------------------------------------------------------------------------
def gate_log(logs: Path, name: str, lines: list[str]) -> None:
    logs.mkdir(exist_ok=True)
    (logs / f"remote-{name}.log").write_text("\n".join(lines) + "\n")


def test_record_gate(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something")
    other = world.first
    logs = world.tmp / "logs"
    tool = world.repo / "tools/release/record_gate.sh"
    env = {"OPENBFME_RELEASE_STATE": str(world.state), "OPENBFME_RELEASE_LOGS": str(logs),
           "OPENBFME_REMOTE_VERIFY": str(world.tmp / "no-remote-verify")}
    ok = ["RESULT slot 0", f"RESULT commit {sha[:8]} X-1: something", f"RESULT sha {sha}", "RESULT build PASS", "RESULT ALL PASS", "DONE 0"]
    gate_log(logs, "g1", ok)
    r = run("bash", tool, sha, "g1", env=env)
    assert r.returncode == 0 and "recorded" in r.stdout, r.stderr
    assert (world.state / "gated.tsv").read_text().startswith(f"{sha}\tg1\t") and (world.state / "gated.tsv").read_text().endswith("\tALL PASS\n")
    assert "already recorded" in run("bash", tool, sha, "g1", env=env).stdout
    assert len((world.state / "gated.tsv").read_text().splitlines()) == 1
    # another commit's run, a failed run, an unfinished one: nothing recorded
    r = run("bash", tool, other, "g1", env=env)
    assert r.returncode != 0 and "was of" in r.stderr
    gate_log(logs, "g2", [f"RESULT sha {sha}", "RESULT SOME GATE FAILED", "DONE 1"])
    assert run("bash", tool, sha, "g2", env=env).returncode != 0
    gate_log(logs, "g3", [f"RESULT sha {sha}", "RESULT build PASS"])
    r = run("bash", tool, sha, "g3", env=env)
    assert r.returncode != 0 and "did not end with DONE 0" in r.stderr
    # a log from before `RESULT sha`: the abbreviation must name this commit
    gate_log(logs, "g4", [f"RESULT commit {sha[:8]} X-1: something", "RESULT ALL PASS", "DONE 0"])
    assert run("bash", tool, sha, "g4", env=env).returncode == 0
    gate_log(logs, "g5", [f"RESULT commit {other[:8]} first", "RESULT ALL PASS", "DONE 0"])
    assert run("bash", tool, sha, "g5", env=env).returncode != 0
    assert len((world.state / "gated.tsv").read_text().splitlines()) == 2


# ---- versions and the release notes -------------------------------------------------------------------------------------------------------
def test_next_version():
    """VERSION-1 (owner, 2026-10-14): plain numbers. v0.4.5 follows any v0.4.0-alpha.<n> (the count goes on from alpha.4), then the next
    last number; --bump minor the next middle number (a huge feature, the owner's call); tags outside the grammar are ignored"""
    previews = [f"refs/tags/v0.3.0-preview.{n}" for n in (1, 2, 3, 4, 6)] + ["refs/tags/v0.3.0-preview.4^{}", "refs/tags/v0.2.0-playtest.7"]
    alphas = previews + [f"refs/tags/v0.4.0-alpha.{n}" for n in (1, 2, 3, 4)]
    nv = autorelease.next_version
    assert nv([]) == "v0.4.5" and nv(previews) == "v0.4.5" and nv(alphas) == "v0.4.5"
    assert nv(alphas + ["refs/tags/v0.4.0-alpha.9"]) == "v0.4.5", "after any alpha build"
    assert nv(alphas, "minor") == "v0.5.0" and nv([], "minor") == "v0.5.0"
    assert nv(alphas + ["refs/tags/v0.4.5"]) == "v0.4.6" and nv(alphas + ["refs/tags/v0.4.5", "refs/tags/v0.4.5^{}"]) == "v0.4.6"
    assert nv(alphas + ["refs/tags/v0.4.5", "refs/tags/v0.4.9", "refs/tags/v0.4.10"]) == "v0.4.11", "numbers, not text"
    assert nv(alphas + ["refs/tags/v0.4.5", "refs/tags/v0.4.6"], "minor") == "v0.5.0"
    assert nv(alphas + ["refs/tags/v0.4.6", "refs/tags/v0.5.0"]) == "v0.5.1" and nv(["refs/tags/v0.5.3"], "minor") == "v0.6.0"
    # outside the launcher's grammar: ignored (no leading zeros, digits only); a plain tag below v0.4.5 does not start the count
    assert nv(alphas + ["refs/tags/v0.4.07", "refs/tags/v0.4.6x", "refs/tags/v0.4.8-rc.1"]) == "v0.4.5"
    assert nv(previews + ["refs/tags/v0.3.0"]) == "v0.4.5"
    # a version the launchers would order below a published one is refused by name, never tagged
    assert nv(alphas + ["refs/tags/v0.4.5-alpha.1"]) == "v0.4.5", "a pre-release of v0.4.5 sorts below it"
    for tags in (alphas + ["refs/tags/v0.4.6-alpha.1"], alphas + ["refs/tags/v0.5.0-alpha.1"], ["refs/tags/v1.0.0-preview.1"]):
        with pytest.raises(SystemExit, match="would not be above"):
            nv(tags)
    assert nv(alphas + ["refs/tags/v0.5.0-alpha.1"], "minor") == "v0.5.0"
    with pytest.raises(SystemExit, match="would not be above the release tag v0.5.1-alpha.1"):
        nv(alphas + ["refs/tags/v0.5.1-alpha.1"], "minor")
    with pytest.raises(SystemExit, match="--bump 'major' is not one of patch, minor"):
        nv([], "major")
    for args in ([], ["--bump", "minor"]):
        r = subprocess.run([sys.executable, HERE / "autorelease.py", "--git-dir", str(REPO), "next-version", "--remote", "/nonexistent", *args],
                           capture_output=True, text=True)
        assert r.returncode != 0, "a remote that cannot be listed is an error, never a default version"
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "next-version", "--bump", "huge"], capture_output=True, text=True)
    assert r.returncode == 2 and "invalid choice" in r.stderr


def test_release_order_and_title():
    order = ["v0.3.0-preview.3", "v0.3.0-preview.4", "v0.3.0", "v0.4.0-alpha.1", "v0.4.0-alpha.2", "v0.4.0-alpha.4", "v0.4.0-alpha.10",
             "v0.4.0-preview.1", "v0.4.0", "v0.4.5", "v0.4.6", "v0.4.10", "v0.5.0", "v1.0.0"]
    for i, x in enumerate(order):
        for j, y in enumerate(order):
            assert (make_manifest.version_key(x) > make_manifest.version_key(y)) == (i > j), (x, y)
            assert subprocess.run([sys.executable, HERE / "autorelease.py", "newer", x, y]).returncode == (0 if i > j else 1), (x, y)
    assert make_manifest.channel_of("v0.4.0-alpha.1") == "preview", "an alpha build is in the launcher's pre-release channel"
    assert make_manifest.channel_of("v0.4.5") == "stable", "a plain release is on the stable channel (a full GitHub release)"
    assert autorelease.release_title("v0.4.5") == "OpenBFME Alpha 0.4.5" and autorelease.release_title("v0.5.0") == "OpenBFME Alpha 0.5.0"
    assert autorelease.release_title("v0.4.0-alpha.1") == "OpenBFME v0.4.0 Alpha 1", "the alpha builds keep their names"
    assert autorelease.release_title("v0.4.0-alpha.12") == "OpenBFME v0.4.0 Alpha 12"
    assert autorelease.release_title("v0.3.0-preview.4") == "OpenBFME v0.3.0-preview.4", "the previews keep their names"
    for bad in ("v0.4.05", "0.4.5", "v0.4.5-beta.1", "v0.4.5 ", ""):
        with pytest.raises(SystemExit):
            autorelease.release_title(bad)
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "title", "v0.4.5"], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout == "OpenBFME Alpha 0.4.5\n"
    text, _ = autorelease.release_notes("v0.4.5", [], [])
    assert text.startswith("# OpenBFME Alpha 0.4.5 patch notes\n\nalpha build for testers\n\nInstall or update it with the OpenBFME Launcher.")
    head = text.split("## Which file to download")[0]
    assert "-alpha." not in head and "-preview." not in head, "no -alpha.N / -preview.N where a player looks"
    text, _ = autorelease.release_notes("v0.4.0-alpha.1", [], [])
    assert text.startswith("# OpenBFME v0.4.0-alpha.1 patch notes\n\nalpha build for testers\n\n"), "the alpha builds' pages as they were"


def test_player_lines():
    line = autorelease.player_line
    assert line("Merge lane/exit1 (EXIT-1: produced members leave the barracks exit and idle units stop running in place (QA-2 findings 1 "
                "and 2: the hub's busy rule RW 0x87471B); S-1750 / S-1751; Sol ACCEPT)") == \
        "Produced members leave the barracks exit and idle units stop running in place."
    assert line("Merge lane/move2 (MOVE-2: horde movement as RotWK does it: members' slot destinations (RW 0x6F0889), the command hand-off "
                "with the leash and the active-member rule, the melee member pass through the hub, blockedBy's step aside, the wrapped "
                "orientation, the melee nearest member in 3D)") == "Horde movement as RotWK does it."
    assert line("Merge fix/stack8 (batch stack8: COMBAT-4 on EXIT-1 / MOVE-2 with pins re-measured)") is None
    assert line("Roadmap: the community feedback table") is None
    assert line("Merge lane/play1 (ce7997fd) into integ/p3: tribute, move hints, upgrade affordability, tours") is None, \
        "an integration merge: the round's Release-note commit speaks for it"
    assert line("Merge lane/reltest1 (7d1da4d2) into integ/p3") is None
    assert line("Merge lane/zoom1 (ZOOM-1: max zoom-out, shroud border, health bars for all players) into integ/p5") is None, \
        "INTEG-5: an integration merge without the lane's sha"
    assert line("Merge branch 'lane/relname1' into integ/p5") is None
    assert autorelease.notes_of("Notes", "Release-note: Fix: a crash on the sound thread.\n", []) == \
        [("fixed", "", "A crash on the sound thread.")]
    assert line("MOVE-2 r5 (stack6): FB-0001 measured by blockedBy") is None, "jargon only"
    assert autorelease.note_of("X-1: internal", "Body.\nRelease-note: Fix: Trolls no longer one-punch whole hordes.\n", []) == \
        ("fixed", "", "Trolls no longer one-punch whole hordes.")
    assert autorelease.note_of("X-1: units walk around trees now", "Release-note: none\n", ["engine/a.cpp"]) is None
    assert autorelease.note_of("X-1: units walk around trees now", "", ["engine/tests/test_a.cpp", "docs/A.md"]) is None
    assert autorelease.note_of("X-1: units walk around trees now", "", ["godot/scripts/game.gd"]) == ("added", "", "Units walk around trees now.")


INJECTED = ("ping @octocat and @Open-BFME/team, see #12 and Open-BFME/openbfme-godot#3 and GH-7, "
            "visit https://evil.example/x?y=1 or www.evil.example, [click me](https://evil.example) "
            "![img](http://evil.example/a.png) <img src=x onerror=alert(1)> <b>bold</b> **strong** `code` mail me@evil.example\n"
            "## a heading")
REF_FREE = ("@", "#", "http", "www.", "](", "<img", "<b>", "\n## ")


def test_notes_are_neutralised(tmp_path):
    """Sol r1: mentions and Markdown links went through subjects and Release-note: text"""
    lines = [autorelease.neutral(INJECTED)]
    md = autorelease.markdown(lines[0])
    for bad in ("@", "#", "http", "www.", "\n"):     # the plain text (commit message, PR title): no mention, reference or URL
        assert bad not in lines[0], bad
    for bad in REF_FREE:                              # the Markdown (notes, PR body): no link, image, HTML or heading either
        assert bad not in md, bad
    assert "**" not in md and "`" not in md
    # end to end: an injected Release-note line and an injected subject, notes, PR items and the title
    repo = tmp_path / "r"
    git(tmp_path, "init", "-q", str(repo))
    a = commit(repo, {"engine/a.cpp": "1\n"}, "first")
    commit(repo, {"engine/a.cpp": "2\n"}, "X-1: injected", force=False)
    git(repo, "commit", "-q", "--allow-empty", "-m", "X-2: injected", "-m", "Release-note: " + INJECTED.replace("\n", " "))
    commit(repo, {"godot/s.gd": "x\n"}, "Merge lane/y (Y-1: units see @octocat at https://evil.example and [x](https://e) now)")
    issues = tmp_path / "known.json"
    issues.write_text(json.dumps({"issues": [{"stops": ["S-1"], "text": "Ask @octocat about #5 at https://evil.example."}]}))
    out = tmp_path / "notes.md"
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "--git-dir", repo, "notes", "--from", a, "--to", "HEAD", "--version",
                        "v0.3.0-preview.3", "--known-issues", issues, "--out", out, "--title-out", tmp_path / "t.txt", "--items-out",
                        tmp_path / "i.md", "--plain-items-out", tmp_path / "i.txt"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    for f in ("notes.md", "t.txt", "i.md", "i.txt"):
        text = (tmp_path / f).read_text()
        text = re.sub(r"## Which file to download.*?\n\n## ", "## ", text, flags=re.S)   # the download table's links are ours (validated tag and repo)
        markdown = f.endswith(".md")   # the plain files (the commit message, the PR title) are not rendered: no mention, ref or URL
        for bad in ("@", "#1", "#3", "#5", "http", "www.") + (("](", "<img", "<b>") if markdown else ()):
            assert bad not in text, (f, bad, text)
    notes = out.read_text()
    assert [l for l in notes.splitlines() if l.startswith("#")] == ["# OpenBFME v0.3.0-preview.3 patch notes", "## Which file to download",
                                                                     f"## {autorelease.TITLES[autorelease.DEFAULT_TITLES]['added']}", "## Known issues"], "no heading injected"


def test_release_notes_end_to_end(tmp_path):
    repo = tmp_path / "r"
    git(tmp_path, "init", "-q", str(repo))
    a = commit(repo, {"engine/a.cpp": "1\n"}, "first")
    commit(repo, {"docs/ROADMAP.md": "2\n"}, "Roadmap: docs only")
    b = commit(repo, {"engine/a.cpp": "3\n"}, "Merge lane/x (X-1: units walk around trees instead of through them (RW 0x123456))")
    issues = tmp_path / "known_issues.json"
    issues.write_text(json.dumps({"issues": [{"stops": ["S-1"], "text": "Create-a-Hero is not available."}]}))
    out = tmp_path / "notes.md"
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "--git-dir", repo, "notes", "--from", a, "--to", b, "--version",
                        "v0.3.0-preview.3", "--known-issues", issues, "--commit", b, "--date", "2026-10-09", "--out", out, "--title-out",
                        tmp_path / "title.txt"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    notes = out.read_text()
    assert "alpha build for testers, numbered as a preview" in notes  # RELNAME-1: a preview now is a bridge
    assert f"## {autorelease.TITLES[autorelease.DEFAULT_TITLES]['fixed']}\n\n**General**\n- Units walk around trees instead of through them.\n" in notes
    assert "Roadmap" not in notes and "RW" not in notes and "## Known issues\n\n**General**\n- Create-a-Hero is not available.\n" in notes
    assert (tmp_path / "title.txt").read_text() == "units walk around trees instead of through them\n"


def test_a_release_without_the_legacy_launcher_archives(world):
    """lane ONEZIP-1, the target page: OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 publishes one archive per platform, the manifest (format 3),
    its signature and the checksum file, and the notes name no launcher archive; any other value is refused before anything runs"""
    sha = world.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: units walk around trees instead of through them (RW 0x123456))")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, OPENBFME_RELEASE_LEGACY_LAUNCHERS="yes")
    assert r.returncode == 2 and "OPENBFME_RELEASE_LEGACY_LAUNCHERS is 1 or 0" in r.stdout + r.stderr and world.gh_data()["releases"] == []
    r = world.release(sha, "--since", world.first, OPENBFME_RELEASE_LEGACY_LAUNCHERS="0")
    assert r.returncode == 0, r.stdout + r.stderr
    (rel,) = world.gh_data()["releases"]
    v = rel["tag_name"]
    assert sorted(a["name"] for a in rel["assets"]) == sorted(
        [f"openbfme-{v}-linux-x64.tar.gz", f"openbfme-{v}-windows-x64.zip", f"SHA256SUMS-{v}.txt", "manifest.json", "manifest.json.sig"])
    (found,) = (world.tmp / "work").rglob("pkg/manifest.json")
    manifest = json.loads(found.read_bytes())
    assert manifest["format"] == 3 and sorted(manifest["assets"]) == ["game"]
    assert "OpenBFMELauncher.exe" in manifest["assets"]["game"]["windows-x64"]["files"]
    assert "openbfme-launcher-" not in rel["body"] and f"| SHA256SUMS-{v}.txt, manifest.json, manifest.json.sig | Used by OpenBFME" in rel["body"]


# ---- round 3 (Sol r2 REJECT) ----------------------------------------------------------------------------------------------------------------
def test_record_gate_ignores_refs_named_like_the_abbreviation(world):
    """Sol r2: a tag named after the logged abbreviation made record_gate record another commit"""
    wrong = world.change({"engine/a.cpp": "2\n"}, "different commit")
    git(world.repo, "tag", world.first[:8], wrong)
    logs = world.tmp / "logs"
    gate_log(logs, "old", [f"RESULT commit {world.first[:8]} original gated commit", "RESULT ALL PASS", "DONE 0"])
    env = {"OPENBFME_RELEASE_STATE": str(world.state), "OPENBFME_RELEASE_LOGS": str(logs), "OPENBFME_REMOTE_VERIFY": str(world.tmp / "none")}
    r = run("bash", world.repo / "tools/release/record_gate.sh", wrong, "old", env=env)
    assert r.returncode != 0 and "was of " + world.first in r.stderr
    assert not (world.state / "gated.tsv").exists() or wrong not in (world.state / "gated.tsv").read_text()
    assert run("bash", world.repo / "tools/release/record_gate.sh", world.first, "old", env=env).returncode == 0


def test_stale_state_cannot_roll_back(world):
    """Sol r2: the final read-back failed after publishing; the next run published older source under a newer version"""
    a = world.change({"engine/a.cpp": "2\n"}, "A released")
    world.gate(a)
    assert world.release(a, "--since", world.first).returncode == 0
    c = world.change({"engine/a.cpp": "3\n"}, "C older candidate")
    world.gate(c)
    b = world.change({"engine/a.cpp": "4\n"}, "B newer candidate")
    world.gate(b)
    r = world.release(b, FAKE_GH_FAULT="final-readback-fail")
    assert r.returncode != 0 and (world.state / "intent").exists(), r.stdout
    assert (world.state / "last-released").read_text().split()[0] == a
    r = world.release(c)
    assert r.returncode != 0 and "reconciled: v0.4.6" in r.stdout and "is older than the last released archive sha" in r.stdout, r.stdout
    assert (world.state / "last-released").read_text().split()[:2] == [b, "v0.4.6"] and not (world.state / "intent").exists()
    assert [(x["tag_name"], x["draft"]) for x in world.gh_data()["releases"]] == [("v0.4.5", False), ("v0.4.6", False)]


def test_an_open_intent_is_resumed_before_any_other_sha(world):
    a = world.change({"engine/a.cpp": "2\n"}, "X-1: first change")
    world.gate(a)
    r = world.release(a, "--since", world.first, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and (world.state / "intent").exists()
    # RELTEST-1: the pushed tag cannot be deleted (GH013); the stop message says the next run resumes the same version from it
    assert world.remote_refs("refs/tags/v0.4.5")
    assert "run autorelease.sh again (for this sha or a later one) and it resumes v0.4.5" in r.stdout, r.stdout
    assert "next number" not in r.stdout and "--delete refs/tags" not in r.stdout
    b = world.change({"engine/a.cpp": "3\n"}, "X-2: second change")
    world.gate(b)
    for extra in (["--plan"], ["--dry-run"]):
        r = world.release(b, *extra)
        assert r.returncode != 0 and "only a real run reconciles it" in r.stdout
    r = world.release(b)
    assert r.returncode == 3 and "RESUMED ONLY" in r.stdout and f"{b} was not released" in r.stdout, r.stdout + r.stderr
    assert (world.state / "last-released").read_text().split()[:2] == [a, "v0.4.5"]
    r = world.release(b)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr


def test_an_intents_own_draft_is_resumed_never_deleted(world):
    """r4: a draft the intent recorded is finished (the missing assets uploaded, all checked, published), never deleted"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT="upload-fail")
    assert r.returncode != 0 and [x["draft"] for x in world.gh_data()["releases"]] == [True], r.stdout + r.stderr
    r = world.release(sha)
    assert r.returncode == 0 and "the intent's own draft" in r.stdout and "resuming the draft release" in r.stdout, r.stdout + r.stderr
    rels = world.gh_data()["releases"]
    assert [(x["tag_name"], x["draft"], len(x["assets"])) for x in rels] == [("v0.4.5", False, 7)]
    assert "--method DELETE" not in world.gh_log.read_text()


def test_a_draft_with_a_wrong_asset_is_left_for_the_operator(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, FAKE_GH_FAULT="upload-corrupt").returncode != 0
    r = world.release(sha)
    assert r.returncode != 0 and "the draft's assets do not match the package folder" in r.stderr + r.stdout, r.stdout + r.stderr
    assert [x["draft"] for x in world.gh_data()["releases"]] == [True] and "--method DELETE" not in world.gh_log.read_text()
    assert (world.state / "intent").exists()


def test_a_draft_published_meanwhile_survives(world):
    """Sol r3: a draft published between the reconciliation's read and its DELETE was deleted; now it survives and is recorded"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, FAKE_GH_FAULT="upload-fail").returncode != 0
    r = world.release(sha, FAKE_GH_FAULT="publish-after-read")
    assert r.returncode != 0, r.stdout + r.stderr
    rels = world.gh_data()["releases"]
    assert [(x["tag_name"], x["draft"]) for x in rels] == [("v0.4.5", False)], "the release survives"
    assert "--method DELETE" not in world.gh_log.read_text() and (world.state / "intent").exists()


def test_a_draft_the_intent_did_not_create_is_left(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, FAKE_GH_FAULT="create-lost").returncode != 0
    r = world.release(sha)
    assert r.returncode != 0 and "that this intent did not record creating" in r.stdout
    assert "--method DELETE" not in world.gh_log.read_text() and len(world.gh_data()["releases"]) == 1


def test_github_read_failures_fail_closed(world, tmp_path):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, STANDIN_TAMPERED="1").returncode != 0
    broken = tmp_path / "broken-gh"
    broken.write_text("#!/bin/bash\necho 'gh: HTTP 502' >&2; exit 1\n")
    broken.chmod(0o755)
    r = world.release(sha, GH=str(broken))
    assert r.returncode != 0 and "fail closed" in r.stdout and (world.state / "intent").exists()


def git_wrapper(tmp: Path, fail_args: list[str]) -> Path:
    real = shutil.which("git")
    w = tmp / "wbin" / "git"
    w.parent.mkdir(exist_ok=True)
    w.write_text(f"#!{sys.executable}\nimport os, sys\nif sys.argv[-{len(fail_args)}:] == {fail_args!r}:\n"
                 f"    print('simulated failure', file=sys.stderr); sys.exit(1)\nos.execv({real!r}, [{real!r}] + sys.argv[1:])\n")
    w.chmod(0o755)
    return w.parent


def test_dangling_tags(world, tmp_path):
    """Sol r2: a failed remote listing was swallowed; a playtest tag (which the build's version ignores) blocked the release"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: changes")
    world.gate(sha)
    wbin = git_wrapper(tmp_path, ["ls-remote", "--tags", "origin"])
    r = world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode != 0 and "cannot list the tags of origin (fail closed)" in r.stdout
    assert not world.remote_refs("refs/tags/v0.4.5")
    # main already has the tree now (the PR merged); a playtest tag on it does not count, a release tag does
    shutil.rmtree(world.tmp / "work", ignore_errors=True)
    (world.state / "intent").unlink(missing_ok=True)
    public = git(world.remote, "rev-parse", "main")
    git(world.repo, "fetch", "-q", "origin", "main")
    git(world.repo, "tag", "v0.2.0-playtest.99", public)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.2.0-playtest.99")
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr


def test_old_or_modified_control_scripts_are_refused(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: changes")
    world.gate(sha)
    git(world.repo, "checkout", "-q", "-b", "later", world.first)
    later = world.change({"docs/x.md": "x\n"}, "not an ancestor")
    git(world.repo, "checkout", "-q", "archive-legacy-codebase")
    r = world.release(sha, "--since", world.first, OPENBFME_RELEASE_MIN_COMMIT=later)
    assert r.returncode != 0 and "predates the release scripts of AUTOREL-1 r3" in r.stdout
    # the tag's publish_release.sh is not the running copy (an old publisher in the released sha)
    pub = world.repo / "tools/release/publish_release.sh"
    good = pub.read_text()
    old = world.change({"tools/release/publish_release.sh": good + "# an older publisher\n", "engine/a.cpp": "3\n"}, "X-2: old publisher")
    pub.write_text(good)
    world.gate(old)
    r = world.release(old, "--since", world.first)
    assert r.returncode != 0 and "tools/release/publish_release.sh differs from the running autorelease's copy" in r.stdout, r.stdout
    assert world.gh_data()["releases"] == [] and "manifest.json.sig" not in str(list((world.tmp / "work").rglob("*")))


def test_the_key_path_is_never_printed(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: changes")
    world.gate(sha)
    key = str(world.key)
    r = world.release(sha, "--since", world.first, "--dry-run")
    assert r.returncode == 0 and "AUTORELEASE DRY RUN OK" in r.stdout, r.stdout + r.stderr
    assert "sign_manifest.py --key \\<release\\ key\\>" in r.stdout, "the WOULD RUN line names the key only as <release key>"
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists(), "the dry run writes nothing to GitHub"
    assert "WOULD RUN: " in r.stdout and "PATCH repos/" in r.stdout and "homepage=https://github.com/" in r.stdout  # RELTEST-1
    world.key.chmod(0o644)
    r2 = world.release(sha, "--since", world.first)
    assert r2.returncode != 0 and "is not mode 600" in r2.stdout
    world.key.chmod(0o600)
    # r6: only the configured key path is used; another override, also a relative one (Sol r5: ./keyrepo/release.pem leaked its
    # normalised path through the signer), is refused without echoing it
    inside = world.tmp / "keyrepo"
    git(world.tmp, "init", "-q", str(inside))
    shutil.copy2(world.key, inside / "release.pem")
    (inside / "release.pem").chmod(0o600)
    r3 = world.release(sha, "--since", world.first, OPENBFME_RELEASE_KEY=str(inside / "release.pem"))
    assert r3.returncode == 2 and "is not the configured release key" in r3.stderr, r3.stdout + r3.stderr
    r4 = run("bash", world.repo / "tools/release/autorelease.sh", sha, "--since", world.first, "--repo", TARGET, "--remote", "origin",
             env=world.env(OPENBFME_RELEASE_KEY="./keyrepo/release.pem"), cwd=world.tmp)
    assert r4.returncode == 2 and "is not the configured release key" in r4.stderr
    r5 = world.release(sha, "--since", world.first, "--dry-run", OPENBFME_RELEASE_KEY=str(world.key))
    assert "is not the configured release key" not in r5.stdout + r5.stderr, "the configured path itself is accepted"
    # the signer's own diagnostics name the key: the configured key inside a git work tree is refused by sign_manifest.py itself
    git(world.home, "init", "-q", str(world.home / ".config"))
    r6 = world.release(sha, "--since", world.first)
    shutil.rmtree(world.home / ".config" / ".git")
    assert r6.returncode != 0 and "cannot read the release key's public half" in r6.stdout
    assert "<release key> is inside a git work tree" in r6.stdout + r6.stderr, r6.stdout + r6.stderr
    texts = [r.stdout, r.stderr, r2.stdout, r2.stderr, r3.stdout, r3.stderr, r4.stdout, r4.stderr, r6.stdout, r6.stderr]
    texts += [p.read_text() for p in (world.tmp / "logs").glob("*.log")]
    for t in texts:
        for path in (key, str(world.key.resolve()), str(world.key.parent), str(world.home), str(inside / "release.pem"), "keyrepo/release.pem"):
            assert path not in t, (path, t[-1500:])


# ---- round 4 (Sol r3 REJECT) -----------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("data,why", [
    (zlib.compress(zlib.compress(b"BIGF\0\0\0\0retail")), "a binary file in the public tree"),
    (gzip.compress(zip_of({"in.bin": zlib.compress(b"BIGF\0\0\0\0retail")})), "a binary file in the public tree"),
])
def test_every_decoded_layer_is_audited(world, data, why):
    """Sol r3: a twice-zlib-wrapped BIG passed; every decoded layer goes back through SyncAudit.blob"""
    sha = world.change({"docs/twice.bin": data}, "tracked anyway", force=True)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "failed the privacy audit: nothing pushed" in r.stdout and why in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == ""


def test_the_depth_bound_is_a_refusal():
    import audit_package  # noqa: PLC0415
    data = b"BIGF\0\0\0\0retail"
    for _ in range(audit_package.MAX_DEPTH + 2):
        data = zlib.compress(data)
    a = autorelease.SyncAudit()
    a.blob("docs/deep.bin", data)
    assert any("nested deeper than" in p for p in a.problems), a.problems


def test_a_swapped_intent_sha_changes_nothing(world):
    """Sol r3: after a lost final read-back, changing only the intent's sha let older source get the next version"""
    a = world.change({"engine/a.cpp": "2\n"}, "A released")
    world.gate(a)
    assert world.release(a, "--since", world.first).returncode == 0
    c = world.change({"engine/a.cpp": "3\n"}, "C older candidate")
    world.gate(c)
    b = world.change({"engine/a.cpp": "4\n"}, "B newer candidate")
    world.gate(b)
    assert world.release(b, FAKE_GH_FAULT="final-readback-fail").returncode != 0
    intent = world.state / "intent"
    text = intent.read_text()
    assert f"sha={b}" in text
    intent.write_text(text.replace(f"sha={b}", f"sha={c}"))
    before = (intent.read_text(), (world.state / "last-released").read_text(), json.dumps(world.gh_data()["releases"]))
    r = world.release(c)
    assert r.returncode != 0 and "does not hold" in r.stdout, r.stdout + r.stderr
    after = (intent.read_text(), (world.state / "last-released").read_text(), json.dumps(world.gh_data()["releases"]))
    assert before == after, "no state changed"
    # the same for the version
    intent.write_text(text.replace("version=v0.4.6", "version=v0.4.5"))
    r = world.release(b)
    assert r.returncode != 0 and "does not hold" in r.stdout and (world.state / "last-released").read_text() == before[1]
    # the true record reconciles
    intent.write_text(text)
    r = world.release(b)
    assert r.returncode != 0 and "reconciled: v0.4.6" in r.stdout and "is the last released archive sha" in r.stdout, r.stdout
    assert (world.state / "last-released").read_text().split()[:2] == [b, "v0.4.6"]


# ---- round 5 (Sol r4 REJECT): no containers in the public tree, one redaction point -----------------------------------------------------
def tar_of(members: list) -> bytes:
    """members: tarfile.TarInfo with data (bytes or None)"""
    import tarfile  # noqa: PLC0415
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w", format=tarfile.PAX_FORMAT) as t:
        for info, data in members:
            t.addfile(info, io.BytesIO(data) if data is not None else None)
    return b.getvalue()


def sol_r4_cases() -> dict:
    import tarfile  # noqa: PLC0415
    private = "/" + "home/deck/secret"
    zc = io.BytesIO()
    with zipfile.ZipFile(zc, "w") as z:
        z.writestr("a.txt", b"hello")
        z.comment = zlib.compress(zlib.compress(b"BIGF\0\0\0\0retail"))
    ze = io.BytesIO()
    with zipfile.ZipFile(ze, "w") as z:
        i = zipfile.ZipInfo("a.txt")
        i.extra = b"\xca\xfe" + len(b"BIGF\0\0\0\0").to_bytes(2, "little") + b"BIGF\0\0\0\0"
        z.writestr(i, b"hello")
    zn = io.BytesIO()
    with zipfile.ZipFile(zn, "w") as z:
        z.writestr(private.lstrip("/") + "/a.txt", b"hello")
        z.writestr("b.txt", b"x")
        z.getinfo("b.txt").comment = private.encode()
    link = tarfile.TarInfo("a.lnk")
    link.type, link.linkname = tarfile.SYMTYPE, private
    pax = tarfile.TarInfo("a.txt")
    pax.size, pax.pax_headers = 5, {"comment": private}
    return {"zip comment": (zc.getvalue(), "a ZIP archive comment"),
            "zip extra field": (ze.getvalue(), "a ZIP extra field"),
            "zip name": (zn.getvalue(), "a private path in a file name"),
            "tar link": (tar_of([(link, None)]), "not a file or a folder"),
            "tar pax": (tar_of([(pax, b"hello")]), "tar PAX headers")}


@pytest.mark.parametrize("case", ["zip comment", "zip extra field", "zip name", "tar link", "tar pax"])
def test_sol_r4_container_metadata_is_refused(world, case):
    """Sol r4: BIG bytes in ZIP comments and extra fields, private paths in ZIP names / comments and tar links / PAX metadata reached the
    public remote. Unlisted: refused as a container. Listed with its sha256: refused for the metadata."""
    data, why = sol_r4_cases()[case]
    sha = world.change({"docs/case.bin": data}, "tracked anyway", force=True)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "a binary file in the public tree" in r.stdout and "nothing pushed" in r.stdout, r.stdout
    allow = world.repo / "tools/release/public_binaries.txt"
    allow.write_text(allow.read_text() + f"{hashlib.sha256(data).hexdigest()}  docs/case.bin  test: {case}\n")
    sha = world.change({"tools/release/public_binaries.txt": allow.read_text()}, "allowlist it")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and why in r.stdout and "nothing pushed" in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()


def test_a_listed_clean_container_passes_and_a_changed_one_does_not():
    data = gzip.compress(zip_of({"a.txt": b"hello"}), mtime=0)
    assert autorelease.container_kind("x.bin", data) == "gzip"
    assert autorelease.container_metadata_problems("x.bin", data) == []
    assert autorelease.container_kind("notes.txt", b"x^2 + 1\n") == "" and autorelease.container_kind("a.png", b"\x89PNG") == ""
    for name in ("a.zip", "b.tar.gz", "c.7z", "d.big", "e.pck", "f.jar"):
        assert autorelease.container_kind(name, b"plain") != "", name


def test_the_allowlist_hash_must_match(world):
    data = gzip.compress(b"harmless", mtime=0)
    allow = world.repo / "tools/release/public_binaries.txt"
    allow.write_text(allow.read_text() + f"{'0' * 64}  docs/h.gz  test: wrong hash\n")
    sha = world.change({"docs/h.gz": data, "tools/release/public_binaries.txt": allow.read_text()}, "listed with another hash")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "is listed in public_binaries.txt with another sha256" in r.stdout


@pytest.mark.parametrize("path", ["docs/" + "home/deck/notes.txt", "docs/<user>/notes.txt", "docs/Users/x/a.txt"])
def test_private_paths_and_user_names_in_file_names(world, path):
    import getpass  # noqa: PLC0415
    path = path.replace("<user>", getpass.getuser())   # the user running the release (deck on the Deck, another name in CI)
    sha = world.change({path: "x\n"}, "a private name")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and ("a private path in a file name" in r.stdout or "a user name in a file name" in r.stdout), r.stdout


def test_one_redaction_point_covers_intent_fields(world, tmp_path):
    """Sol r4: an intent's step set to the key path was printed by an echo. Every line goes through one redactor: the key path, its folder
    and the home folder never appear in stdout, stderr or any log."""
    home, key = world.home, world.key
    keydir = key.parent
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: changes")
    world.gate(sha)
    world.state.mkdir(exist_ok=True)
    (world.state / "intent").write_text(f"sha={sha}\nversion=v0.4.5\nstep={key}\nwork={keydir}\ntree={home}\npublic={key}\n")
    env = {}
    runs = [world.release(sha, **env)]
    (world.state / "intent").unlink()
    runs.append(world.release(sha, "--since", world.first, "--dry-run", **env))
    assert runs[0].returncode != 0 and "<release key>" in runs[0].stdout, runs[0].stdout
    assert runs[1].returncode == 0, runs[1].stdout + runs[1].stderr
    texts = [t for r in runs for t in (r.stdout, r.stderr)] + [p.read_text() for p in (world.tmp / "logs").glob("*.log")]
    for t in texts:
        for secret in (str(key), str(keydir), str(home)):
            assert secret not in t, (secret, t[-2000:])


# ---- round 6 (Sol r5 REJECT): a text-only public tree, public metadata audited, one fixed key path -----------------------------------
@pytest.mark.parametrize("name,data", [
    ("docs/prefixed.bin", b"header bytes " + gzip.compress(b"BIGF\0\0\0\0retail")),
    ("tools/install.sh", b"#!/bin/sh\ntail -n +3 \"$0\" | gunzip; exit\n" + gzip.compress(b"BIGF\0\0\0\0retail")),
    ("docs/picture.png", b"\x89PNG\r\n\x1a\n" + b"\0" * 16 + gzip.compress(b"BIGF\0\0\0\0retail")),
])
def test_the_public_tree_is_text_only(world, name, data):
    """Sol r5: a prefixed gzip, a self-extracting shell stub and PNG + gzip passed the container detectors"""
    sha = world.change({name: data}, "a binary", force=True)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and f"a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: {name}" \
        in r.stdout and "nothing pushed" in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()


def test_public_metadata_fails_rather_than_redacts(world):
    """Sol r5: a Release-note with a private path and the key path reached the commit message, the PR body and the release notes"""
    for note in ("see /" + "home/deck/secrets for the maps", f"signed with {world.key}", "ask me at someone@example.org",
                 "built on steamdeck"):
        sha = world.change({"engine/a.cpp": note + "\n"}, "X-1: a change", )
        git(world.repo, "commit", "-q", "--amend", "-m", "X-1: a change", "-m", "Release-note: " + note)
        sha = git(world.repo, "rev-parse", "HEAD")
        world.gate(sha)
        r = world.release(sha, "--since", world.first)
        assert r.returncode != 0 and "the public metadata failed the privacy audit: nothing pushed" in r.stdout, r.stdout
        assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists() and not world.remote_refs("refs/tags/*")


def test_public_text_rules():
    t = autorelease.public_text_problems
    assert t("x", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\n1760627+Ancalgonn@users.noreply.github.com") == []
    assert t("x", "Horde movement as RotWK does it\\.") == []
    for bad in ("/" + "home/x/y", "C:" + "\\Users\\x", "~/.config/openbfme-release", "a\\@b\\.example\\.org", "me＠evil.example.org",
                str(autorelease.KEY_PATH).replace(".", "\\.")):
        assert t("x", bad), bad


def test_the_redactor_takes_escaped_and_url_encoded_forms(world):
    import urllib.parse  # noqa: PLC0415
    script = (world.repo / "tools/release/autorelease.sh").read_text()
    code = script.split("REDACT_PY='")[1].split("\n'\n")[0]
    key = str(world.key)
    env = {**os.environ, "AR_KEY": key, "AR_KEYDIR": str(world.key.parent), "AR_HOME": str(world.home)}
    text = "\n".join([key, autorelease.markdown(key), urllib.parse.quote(key), urllib.parse.quote(key, safe=""), str(world.home) + "/x"])
    out = subprocess.run([sys.executable, "-u", "-c", code], input=text.encode(), capture_output=True, env=env).stdout.decode()
    assert out.splitlines() == ["<release key>"] * 4 + ["~/x"], out


def test_no_production_script_writes_to_the_terminal_or_its_own_log():
    for f in sorted(HERE.glob("*.sh")) + [p for p in sorted(HERE.glob("*.py")) if not p.name.startswith("test_")]:
        text = f.read_text()
        assert "/dev/tty" not in text, f.name


# ---- round 7 (Sol r6 ACCEPT WITH FIXES): user names only in path-like or user@host form -------------------------------------------------
@pytest.mark.parametrize("text,ok", [
    ("Steam Deck controls now work", True),
    ("the Deck", True),
    ("Units on the deck of a ship", True),
    ("/" + "home/deck/maps", False),
    ("deck@host", False),
    ("C:" + "\\Users\\jonathan", False),
    ("~deck/x", False),
])
def test_user_names_count_only_as_accounts(text, ok, monkeypatch):
    monkeypatch.setattr(autorelease, "_PRIVATE_WORDS", ({"deck", "jonathan"}, {"steamdeck"}))
    assert (autorelease.public_text_problems("note", text) == []) == ok, autorelease.public_text_problems("note", text)


def test_a_steam_deck_release_note_is_published(world):
    """Sol r6: a legitimate 'Steam Deck controls now work' Release-note stopped the release"""
    world.change({"engine/a.cpp": "2\n"}, "X-1: a change")
    git(world.repo, "commit", "-q", "--amend", "-m", "X-1: a change", "-m", "Release-note: Steam Deck controls now work")
    sha = git(world.repo, "rev-parse", "HEAD")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "Steam Deck controls now work" in world.gh_data()["releases"][0]["body"]


def test_a_release_notes_commit_gives_one_note_per_line():
    body = "Notes.\nRelease-note: Movies play.\nRelease-note: Fix: The Options crash is fixed.\nRelease-note: Health bars show.\n"
    assert autorelease.notes_of("Release notes for v0.3.0-preview.1", body, []) == \
        [("added", "", "Movies play."), ("fixed", "", "The Options crash is fixed."), ("added", "", "Health bars show.")]
    assert autorelease.notes_of("X", "Release-note: A.\nRelease-note: none\n", ["engine/a.cpp"]) == []
    assert autorelease.notes_of("X-1: units walk around trees now", "", ["godot/scripts/game.gd"]) == [("added", "", "Units walk around trees now.")]
    text, lines = autorelease.release_notes("v0.3.0-preview.9", [{"subject": "Release notes", "body": body, "files": []}], [])
    assert {"Movies play.", "Health bars show.", "The Options crash is fixed."} <= set(lines)
    dt = autorelease.TITLES[autorelease.DEFAULT_TITLES]
    assert f"## {dt['added']}\n" in text and f"## {dt['fixed']}\n" in text, "the DEFAULT_TITLES titles"
    studio, _ = autorelease.release_notes("v0.3.0-preview.9", [{"subject": "Release notes", "body": body, "files": []}], [], titles="studio")
    assert "## Added" in studio and "## Bug fixes" in studio and "What's" not in studio


def test_a_user_name_is_a_whole_path_component_not_a_stem(monkeypatch):
    monkeypatch.setattr(autorelease, "USER_NAMES", {"claude"})
    assert autorelease.path_problem("CLAUDE.md") == ""
    assert autorelease.path_problem("docs/claude.md") == ""
    assert autorelease.path_problem("claude/notes.txt") != ""
    assert autorelease.path_problem("tools/Claude") != ""


# ---- NOTES-1: the studio shape of the patch notes ---------------------------------------------------------------------------------------
def _commit(body: str, subject: str = "Release notes", files=()) -> dict:
    return {"subject": subject, "body": body, "files": list(files)}


def test_release_note_syntax():
    p = autorelease.parse_note
    assert p("Added [Online]: Server browser.") == ("added", "Online", "Server browser.")
    assert p("Changed [Performance]: faster paths.") == ("changed", "Performance", "Faster paths.")
    assert p("Fixed [Units and heroes]: Heroes strike hordes.") == ("fixed", "Units and heroes", "Heroes strike hordes.")
    assert p("Fix: A crash on the sound thread.") == ("fixed", "", "A crash on the sound thread.")
    assert p("Fix [interface]: a click deselects.") == ("fixed", "Interface", "A click deselects."), "the area is case-insensitive"
    assert p("Removed [Launcher and install]: The old updater.") == ("removed", "Launcher and install", "The old updater.")
    assert p("Added: no area.") == ("added", "", "No area.")
    assert p("Units walk around trees now.") == ("added", "", "Units walk around trees now.")
    assert p("Units no longer walk through trees.") == ("fixed", "", "Units no longer walk through trees.")
    assert p("Added [Nowhere]: x") == ("added", "", "X"), "an unknown area is General (and a plan warning)"
    assert p("Changelog: not a kind") == ("added", "", "Changelog: not a kind")
    assert autorelease.notes_of("S", "Release-note: Removed [Audio]: A sound.\nRelease-note: Changed: Balance.\n", []) == \
        [("removed", "Audio", "A sound."), ("changed", "", "Balance.")]


def test_note_warnings_name_what_to_fix():
    w = autorelease.note_warnings
    assert w("Added [Online]: A short, single change.") == []
    assert w("Fix: A short fix.") == []
    assert any("unknown area [Nowhere]" in x for x in w("Added [Nowhere]: x"))
    assert any("';'" in x for x in w("Added [Online]: one thing; another thing"))
    assert any("201 characters" in x for x in w("Added: " + "x" * 201)) and w("Added: " + "x" * 200) == []
    assert w("Ends with;") == [], "a ';' joins clauses only when something follows"
    c = {"sha": "0123456789abcdef", "subject": "S", "body": "Body.\nRelease-note: Added [Foo]: a; b\nRelease-note: Fine.\nRelease-note: none\n"}
    ws = autorelease.release_warnings(c)
    assert len(ws) == 2 and all(x.startswith("NOTES WARNING 0123456789: ") for x in ws), ws
    hostile = {"sha": "0123456789abcdef", "body": "Release-note: Added [@evil https://x.example]: a; b see #12"}
    assert not any(bad in "".join(autorelease.release_warnings(hostile)) for bad in ("@", "#1", "https"))


def test_the_studio_page(tmp_path):
    body = "\n".join(["Release-note: Added [Online]: Server browser.", "Release-note: Added [Saves]: Games can be saved.",
                      "Release-note: Added [Gameplay]: First gameplay line.", "Release-note: Added: No area.",
                      "Release-note: Changed [Linux]: Runs on older systems.", "Release-note: Changed [Performance]: Faster paths.",
                      "Release-note: Fixed [Interface]: A click on empty ground deselects.", "Release-note: Fixed [Units and heroes]: Heroes fight.",
                      "Release-note: Removed [Interface]: The old menu.", "Release-note: Added [Nowhere]: Unknown area.",
                      "Release-note: Added [Online]: Server browser."])
    issues = [{"area": "Campaign", "text": "No campaign menu."}, {"area": "Audio", "text": "Some sounds are missing."},
              {"area": "Nowhere", "text": "An unknown area."}, {"text": "No area at all."}]
    text, lines = autorelease.release_notes("v0.4.0-alpha.5", [_commit(body)], issues, "0123456789abcdef", "2026-10-13", titles="studio")
    heads = [l for l in text.splitlines() if l.startswith("#")]
    assert heads == ["# OpenBFME v0.4.0-alpha.5 patch notes", "## Which file to download", "## Added", "## Changed", "## Bug fixes", "## Removed",
                     "## Known issues"], heads
    assert text.splitlines()[2] == "October 13, 2026 \u00b7 alpha build for testers"
    assert text.rstrip().endswith("Built from commit 0123456789 (2026-10-13).")
    section = lambda t: text.split(f"## {t}\n", 1)[1].split("\n## ", 1)[0]
    assert section("Added") == ("\n**Gameplay**\n- First gameplay line.\n\n**Online**\n- Server browser.\n\n**Saves**\n- Games can be saved.\n\n"
                                "**General**\n- No area.\n- Unknown area.\n"), "areas in AREAS' order, General last, a repeated line once"
    assert section("Changed") == "\n**Performance**\n- Faster paths.\n\n**Linux**\n- Runs on older systems.\n"
    assert section("Known issues").startswith("\n**Campaign**\n- No campaign menu.\n\n**Audio**\n")
    assert "**General**\n- An unknown area.\n- No area at all.\n" in section("Known issues")
    assert lines == ["Server browser.", "Games can be saved.", "First gameplay line.", "No area.", "Unknown area.", "Runs on older systems.",
                     "Faster paths.", "A click on empty ground deselects.", "Heroes fight.", "The old menu."], lines
    # the bridge titles: the old launchers' two Play-card titles hold the added and fixed lines
    bridge, _ = autorelease.release_notes("v0.4.0-alpha.5", [_commit(body)], issues, titles="bridge")
    assert [l for l in bridge.splitlines() if l.startswith("## ")] == ["## Which file to download", "## What's new", "## Changed",
                                                                         "## What's fixed", "## Removed", "## Known issues"]
    assert "Built from commit" not in bridge and autorelease.DEFAULT_TITLES in autorelease.TITLES
    with pytest.raises(SystemExit):
        autorelease.release_notes("v0.4.0-alpha.5", [], [], titles="nonsense")


def test_the_download_part_is_one_function():
    d = autorelease.download_section("v0.4.0-alpha.3")
    assert d[0] == "## Which file to download" and d[1] == "" and d[2:4] == ["| File | What it is |", "|---|---|"]
    assert len(d) == 4 + 3 + 3 and all(l.startswith("|") for l in d[2:7]) and d[7] == "" and d[9] == ""   # ONEZIP-1: a game link per platform, the rest in one line
    assert "[openbfme-v0.4.0-alpha.3-windows-x64.zip](https://github.com/Open-BFME/openbfme-godot/releases/download/v0.4.0-alpha.3/" \
           "openbfme-v0.4.0-alpha.3-windows-x64.zip)" in d[4]
    assert "openbfme-launcher-v0.4.0-alpha.3-windows-x64.zip" in d[6] and "SHA256SUMS-v0.4.0-alpha.3.txt" in d[6]
    for bad in (("not a version", "Open-BFME/x"), ("v0.4.0-alpha.3", "evil.example/x y"), ("v0.4.0-alpha.3", "a/b](c")):
        with pytest.raises(SystemExit):
            autorelease.download_section(*bad)
    text, _ = autorelease.release_notes("v0.4.0-alpha.3", [], [], repo="Some-Owner/some.repo")
    assert "https://github.com/Some-Owner/some.repo/releases/download/v0.4.0-alpha.3/" in text


def test_the_hostile_note_cannot_break_the_page(tmp_path):
    """every new field: kind and area words are closed sets (a hostile area is General), the text keeps its escaping, the page keeps its headings"""
    evil = "Added [## Online\n## Fake]: @octocat https://evil.example <img src=x> **bold** #12"
    text, lines = autorelease.release_notes("v0.4.0-alpha.5", [_commit("Release-note: " + evil.replace("\n", " ")),
                                                              _commit("Release-note: Fixed [Interface\u2028]: line break")], [], titles="studio")
    assert [l for l in text.splitlines() if l.startswith("#")] == ["# OpenBFME v0.4.0-alpha.5 patch notes", "## Which file to download",
                                                                    "## Added", "## Bug fixes"]
    assert [l for l in text.splitlines() if l.startswith("**")] == ["**General**", "**General**"]
    for bad in ("@", "#12", "<img", "evil.example", "**bold**"):
        assert bad not in text.replace("(link removed)", ""), bad


def _launcher_view(tmp_path, notes: str) -> str:
    """what the alpha.3 launcher (a frozen copy of its parser) and this tree's launcher read from a release body (fixtures/notes_view.gd)"""
    proj = tmp_path / "view"
    proj.mkdir(exist_ok=True)
    (proj / "project.godot").write_text('config_version=5\n\n[application]\n\nconfig/name="notes view"\n')
    shutil.copy(HERE / "fixtures/release_notes_alpha3.gd", proj / "release_notes_alpha3.gd")
    shutil.copy(HERE.parent.parent / "launcher/scripts/core/release_notes.gd", proj / "release_notes.gd")
    (proj / "scripts/core").mkdir(parents=True, exist_ok=True)   # VERSION-1: release_notes.gd preloads the launcher's semver.gd
    shutil.copy(HERE.parent.parent / "launcher/scripts/core/semver.gd", proj / "scripts/core/semver.gd")
    shutil.copy(HERE / "fixtures/notes_view.gd", proj / "notes_view.gd")
    (tmp_path / "notes.md").write_text(re.sub(r"\\(.)", r"\1", notes), encoding="utf-8")   # Updater.plain_text undoes the Markdown escapes first
    env = dict(os.environ, XDG_DATA_HOME=str(tmp_path / "d"), XDG_CONFIG_HOME=str(tmp_path / "c"), XDG_CACHE_HOME=str(tmp_path / "k"))
    r = subprocess.run([os.environ.get("GODOT", "godot"), "--headless", "--path", str(proj), "--script", "res://notes_view.gd", "--",
                        str(tmp_path / "notes.md")], capture_output=True, text=True, timeout=180, env=env, stdin=subprocess.DEVNULL)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


def test_launchers_read_the_new_notes(tmp_path):
    """NOTES-1: the real page text through the parser of the launcher that shipped in v0.4.0-alpha.3 and through this tree's launcher"""
    if shutil.which(os.environ.get("GODOT", "godot")) is None:
        pytest.skip("no Godot ($GODOT) to run the launcher's parsers")
    body = "\n".join(["Release-note: Added [Online]: Server browser.", "Release-note: Added [Saves]: Games can be saved.",
                      "Release-note: Changed [Performance]: Faster paths.", "Release-note: Fixed [Interface]: A click deselects.",
                      "Release-note: Removed [Audio]: A sound."])
    issues = [{"area": "Audio", "text": "Some sounds are missing."}]
    for titles in ("bridge", "studio"):
        text, _ = autorelease.release_notes("v0.4.0-alpha.5", [_commit(body)], issues, "0123456789abcdef", "2026-10-13", titles=titles)
        view = _launcher_view(tmp_path, text)
        assert "NEW PLAY CARD: 2 lines" in view and "NEW PLAY: Server browser." in view, view
        assert "NEW NEWS SECTION: Known issues (1 lines) areas: Audio" in view, view
        if titles == "bridge":   # an alpha.3 launcher that has not updated itself yet still has its Play card and the sections' titles
            assert "OLD PLAY CARD: 2 lines" in view and "OLD PLAY: Server browser." in view, view
            assert "OLD NEWS SECTION: What's new (2 lines)" in view and "OLD NEWS SECTION: What's fixed (1 lines)" in view, view
        else:                    # the studio titles alone leave the old Play card empty
            assert "OLD PLAY CARD: 0 lines" in view and "NEW NEWS SECTION: Added (2 lines) areas: Online, Saves" in view, view


def test_every_label_goes_through_the_escaping(monkeypatch):
    """NOTES-1 r2: the area and section-title labels are escaped like every other field, whatever the allowlists hold (a boundary test:
    the lists are patched with hostile words, nothing hostile reaches the page unescaped)"""
    evil = "Evil @octocat [x](https://evil.example) <b>\n## Fake"
    monkeypatch.setattr(autorelease, "AREAS", ("Online", evil))
    monkeypatch.setattr(autorelease, "TITLES", {"studio": {"added": evil, "changed": "Changed", "fixed": "Bug fixes", "removed": "Removed"}})
    text, _ = autorelease.release_notes("v0.4.0-alpha.5", [_commit("Release-note: Added [Online]: a thing.")], [{"area": evil, "text": "x"}],
                                        titles="studio")
    body = re.sub(r"## Which file to download.*?\n\n## ", "## ", text, flags=re.S)
    for bad in ("@", "](", "<b>", "https", "\n## Fake"):
        assert bad not in body, (bad, body)
    assert [l for l in body.splitlines() if l.startswith("#")] == ["# OpenBFME v0.4.0-alpha.5 patch notes", "## " + autorelease.markdown(autorelease.neutral(evil)),
                                                                    "## Known issues"]


# ---- VERSION-1 review (Sol): two release guards that failed open -------------------------------------------------------------------------
def test_a_resumed_plain_release_is_above_every_release_tag():
    """Sol's repro 1: v0.4.5 interrupted, v0.5.0-alpha.1 tagged meanwhile, v0.4.5 resumed would publish below it. Every valid release tag
    but the intent's own counts, whatever its line"""
    iv = autorelease.intent_version_ok
    tags = ["refs/tags/v0.4.0-alpha.4", "refs/tags/v0.4.5", "refs/tags/v0.4.5^{}"]
    assert iv("v0.4.5", "v0.4.0-alpha.4", tags), "its own reserved tag is no obstacle"
    for later in ("v0.5.0-alpha.1", "v0.4.6-preview.1", "v0.4.6", "v1.0.0"):
        assert not iv("v0.4.5", "v0.4.0-alpha.4", tags + [f"refs/tags/{later}"]), later
    assert iv("v0.4.5", "v0.4.0-alpha.4", tags + ["refs/tags/v0.4.5-alpha.1", "refs/tags/not-a-release"]), "below it, or no release tag"
    assert not iv("v0.4.5", "v0.5.0-alpha.1", tags), "the last released one counts too"
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "intent-version", "v0.4.5", "v0.4.0-alpha.4"], capture_output=True, text=True,
                       input="x\trefs/tags/v0.4.5\nx\trefs/tags/v0.5.0-alpha.1\n")
    assert r.returncode == 1


def test_a_resumed_release_below_a_later_tag_is_refused_by_the_run(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and "version=v0.4.5" in (world.state / "intent").read_text(), r.stdout + r.stderr
    git(world.repo, "tag", "v0.5.0-alpha.1", sha)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.5.0-alpha.1")
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "above every released or tagged version" in r.stdout + r.stderr and world.gh_data()["releases"] == [], \
        r.stdout + r.stderr


def test_format_3_is_refused_while_a_published_launcher_reads_format_2_only(world):
    """Sol's repro 2: OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 published format 3, which the published preview.4 launcher refuses
    ("this launcher reads format 2"). Derived from each release tag's own manifest.gd"""
    assert autorelease.launcher_formats("const FORMATS := [2, 3]\n") == {2, 3} and autorelease.launcher_formats("const FORMAT := 2\n") == {2}
    for bad in ("", "const FORMAT := 2\nconst FORMAT := 3\n", "# FORMAT := 2\n"):
        with pytest.raises(SystemExit, match="cannot tell which manifest formats"):
            autorelease.launcher_formats(bad)
    old = "extends RefCounted\nconst FORMAT := 2\n"
    new = "extends RefCounted\nconst FORMATS := [2, 3]\n"
    a = world.change({"launcher/scripts/core/manifest.gd": old}, "old launcher")
    git(world.repo, "tag", "v0.3.0-preview.4", a)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.3.0-preview.4")
    b = world.change({"launcher/scripts/core/manifest.gd": new, "engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    git(world.repo, "tag", "v0.4.0-alpha.9", b)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.4.0-alpha.9")
    assert autorelease.format2_only_launchers(str(world.repo), ["refs/tags/v0.3.0-preview.4", "refs/tags/v0.4.0-alpha.9^{}",
                                                                "refs/tags/v0.2.0-playtest.1"]) == ["v0.3.0-preview.4"]
    with pytest.raises(SystemExit, match="not in this repository"):
        autorelease.format2_only_launchers(str(world.repo), ["refs/tags/v0.3.0-preview.7"])
    world.gate(b)
    r = world.release(b, "--since", world.first, OPENBFME_RELEASE_LEGACY_LAUNCHERS="0")
    assert r.returncode == 2 and "read manifest format 2 only (v0.3.0-preview.4)" in r.stdout + r.stderr, r.stdout + r.stderr
    assert "LEGACY_LAUNCHERS=0 is refused" in r.stdout + r.stderr and world.gh_data()["releases"] == [] and not (world.state / "intent").exists()
    r = world.release(b, "--since", world.first)   # the default (archives kept) still releases
    assert r.returncode == 0, r.stdout + r.stderr


def test_the_real_published_launchers_read_format_2_only():
    tags = subprocess.run(["git", "-C", str(REPO), "tag", "-l", "v0.3.0-preview.*", "v0.4.0-alpha.*"], capture_output=True, text=True).stdout.split()
    if not tags:
        pytest.skip("SKIP: this clone has no release tags")
    old = autorelease.format2_only_launchers(str(REPO), tags)
    assert {"v0.3.0-preview.4", "v0.4.0-alpha.4"} <= set(old) or not {"v0.3.0-preview.4", "v0.4.0-alpha.4"} <= set(tags), old


def test_notes_have_no_backslash_escapes():
    """The launcher shows the notes as text: v0.4.0-alpha.5 printed "original\\.", "\\(" and "under\\-attack" (NOTES-1 escaped punctuation)"""
    text = 'The original. Radar (pings), under-attack lines: 1+1 = 2! snake_case {x} a|b `c` *d* [e](f) ~g~ back\\slash <h> & i'
    md = autorelease.markdown(autorelease.neutral(text))
    assert "\\" not in md
    for plain in ("original.", "(pings)", "under-attack", "lines: 1+1 = 2!", "snake_case", "{x}"):
        assert plain in md, plain
    for active in ("`", "*", "[", "]", "|", "~", "<h>"):
        assert active not in md, active
    commits = [{"subject": "X-1: y", "body": "Release-note: Fixed [Gameplay]: the original. Radar (pings) and under-attack lines (see [x](u)) work again", "files": []}]
    notes, _ = autorelease.release_notes("v0.4.0-alpha.6", commits, [{"text": "A known (issue) with under-attack.", "area": "Gameplay"}])
    assert "\\" not in notes
    assert "- The original. Radar (pings) and under-attack lines" in notes or "original. Radar (pings)" in notes
