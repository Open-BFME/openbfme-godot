"""Lane AUTOREL-1: tools/release/autorelease.sh, record_gate.sh, publish_release.sh --execute / --confirm-tag, the release notes and the
public sync's privacy audit. Everything runs against throwaway repositories: a fake archive repository holding copies of the release
scripts, a local bare repository as the public remote, gh_stand_in.py as gh, a throwaway key, a fictional private denylist, and
OPENBFME_RELEASE_STANDIN for the build and the signing machine's package checks. Never the real key, the real repository or GitHub.
Round 2 (Sol r1 REJECT): each finding has a test here, real mode included (round 1 tested --plan only, and every real run aborted)."""
from __future__ import annotations

import fcntl
import gzip
import hashlib
import io
import struct
import zipfile
import zlib
import json
import re
import os
import shutil
import shlex
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
import release_assets  # noqa: E402
from launcher_test_server import tiny_package  # noqa: E402

GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
TARGET = "Test/repo"
ARCHIVE_LINE = "previous codebase is kept in `archive/` as reference only.\n"
def ip(*parts: int) -> str:
    """a synthetic address, built at run time: the CI mirror's own denylist scans this file's text and refuses literal LAN addresses"""
    return ".".join(map(str, parts))


# PUBLISH-2: a fictional private denylist in the operator's format (the CI mirror's rule kinds)
# (this file, copied into the test archive with the release scripts, spells the fictional names: allowed like test data)
OWN = ["tools/release/test_autorelease.py"]
TEST_DENYLIST = {"rules": [
    {"id": "hosts", "kind": "word", "pattern": ["buildbox-7", "lab-vm-q"], "severity": "warn", "allow": OWN},
    {"id": "accounts", "kind": "user", "pattern": ["opsuser"], "allow": OWN},
    {"id": "account-ids", "kind": "regex", "pattern": r"\b998877665544\b", "allow": OWN},
    {"id": "ops-files", "kind": "regex", "pattern": r"(?:^|/)runbook-[^/]*$", "paths": True, "allow": OWN},
]}


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


# lane MACGATE-1: the release pipeline (autorelease.sh, publish_release.sh) runs on the Deck and the Linux release hosts only: its scripts are
# #!/bin/bash and use bash 4+ (mapfile), and macOS's /bin/bash is 3.2. A run of it on macOS is skipped by name, never passed.
DECK_HOSTED = "the release pipeline (autorelease.sh / publish_release.sh) runs on the Deck and Linux release hosts: #!/bin/bash needs bash 4+, macOS has 3.2"


def skip_on_macos() -> None:
    if sys.platform == "darwin":
        pytest.skip(DECK_HOSTED)


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
                # MACACT-1: the GDExtension, which autorelease.sh compares with a GitHub Actions build of it (actions_lib)
                files[LIBS[platform]] = actions_lib(platform, version)
                files["VERSION"] = f"OpenBFME {version}\n".encode()   # seal_package.py checks it names the version
            data = tiny_package(n, files)
            (d / n).write_bytes(data)
            sums += f"{hashlib.sha256(data).hexdigest()}  {n}\n"
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    (d / f"BUILDINFO-{version}.txt").write_text("python 3 zlib 1\n")
    if extra:
        (d / "payload.bin").write_bytes(b"not a package file")
    return d


LIBS = {"linux-x64": "openbfme.linux.template_debug.x86_64.so", "windows-x64": "openbfme.windows.template_debug.x86_64.dll"}


def actions_lib(platform: str, version: str, github: bool = False, differs: bool = False) -> bytes:
    """the stand-in GDExtension of a platform: the build host's and GitHub Actions' builds (`github`) are the same bytes, the Windows DLL
    but for its CodeView GUID (lld's hash of the PDB, which holds the build folder's path: a small PE image, test_pdb_tools.pe); `differs`
    (STANDIN_ACTIONS_FAULT=lib-differs): GitHub's is another build"""
    body = f"the {platform} GDExtension of {version}{' (another build)' if differs else ''}\n".encode()
    if platform == "linux-x64":
        return body
    import test_pdb_tools
    return test_pdb_tools.pe(guid=bytes([0x6A if github else 0x11]) * 16, extra=body)


def standin_build_hooks(checkout: Path, version: str) -> None:
    """RELCAND-1, the stand-in build's side: it records what it was given (the checkout's commit, `git describe`, the tag object, and
    whether the public remote had the tag or the commit yet) in $STANDIN_BUILD_LOG, then fails (STANDIN_BUILD_FAIL=1), moves the public
    main meanwhile (STANDIN_MOVE_MAIN=1: someone else's push) or kills the release run like a power cut (STANDIN_KILL=1)"""
    def g(*args: str, cwd: Path = checkout) -> str:
        return subprocess.run(["git", "-C", str(cwd), *args], capture_output=True, text=True, env={**os.environ, **GIT_ENV}).stdout.strip()
    remote = Path(os.environ["FAKE_GH_REMOTE"])
    head = g("rev-parse", "HEAD")
    rec = {"version": version, "head": head, "describe": g("describe", "--tags", "--match", "v[0-9]*", "--exclude", "*-playtest.*", "--always",
           "--dirty", "--abbrev=10"), "tagobj": g("rev-parse", f"refs/tags/{version}"),
           "remote_tag": g("ls-remote", str(remote), f"refs/tags/{version}"),
           "remote_has_head": subprocess.run(["git", "-C", str(remote), "merge-base", "--is-ancestor", head, "refs/heads/main"],
                                             capture_output=True).returncode == 0}
    with open(os.environ["STANDIN_BUILD_LOG"], "a") as f:
        f.write(json.dumps(rec) + "\n")
    if os.environ.get("STANDIN_MOVE_MAIN") == "1":
        main = g("rev-parse", "refs/heads/main", cwd=remote)
        moved = g("commit-tree", f"{main}^{{tree}}", "-p", main, "-m", "someone else's push", cwd=remote)
        g("update-ref", "refs/heads/main", moved, cwd=remote)
    if os.environ.get("STANDIN_KILL") == "1":
        os.kill(os.getppid(), 9)
        sys.exit(1)
    if os.environ.get("STANDIN_BUILD_FAIL") == "1":
        print("AUTOREL BUILD FAIL at step build: a stand-in failure (the Windows start)")
        sys.exit(1)


def builds(world) -> list[dict]:
    """what the stand-in build saw, one record per build"""
    log = world.tmp / "builds.jsonl"
    return [json.loads(x) for x in log.read_text().splitlines()] if log.exists() else []


def mac_package_dir(checkout: Path, version: str, out: Path, fault: str = "", commit: str = "") -> Path:
    """MACREL-1 / RELGH-2: GitHub Actions' macOS output as package_macos.py writes it (<stem>.zip, .zip.sha256, .inventory.txt, README-MAC.txt), a
    package that passes the strict macos-arm64 audit (test_macos_release.generic_package) with VERSION naming the checkout's commit and a
    launcher whose build_info.json trusts the checkout's launcher/release_key.pub. `fault`: what a broken build host returns instead"""
    import tempfile
    import make_archive
    import test_macos_preview
    import test_macos_release
    commit = commit or git(checkout, "rev-parse", "HEAD")   # `commit`: the tagged public commit the host built (mac_publish_dir)
    key = (checkout / "launcher/release_key.pub").read_text().strip()
    keys = [ln.split()[0] for ln in (checkout / "launcher/release_keys.pub").read_text().splitlines() if ln.strip() and not ln.startswith("#")]
    nightly = [ln.split()[0] for ln in (checkout / "launcher/nightly_keys.pub").read_text().splitlines() if ln.strip() and not ln.startswith("#")]
    info = {"version": version, "display_version": f"{version} Mac Unstable Preview (Apple Silicon)", "commit": commit, "date": "2026-10-16",
            "repo": TARGET, "release_key": key, "release_keys": keys, "nightly_keys": nightly, "test_build": False, "local_trial": False, "macos_preview": False,
            "macos_updates": True}
    if fault == "wrong-key":
        info["release_key"] = "ab" * 32
    if fault == "wrong-repo":
        info["repo"] = "Other/repo"
    with tempfile.TemporaryDirectory() as t:
        root, files = test_macos_release.generic_package(Path(t), version)
        files["VERSION"] = f"OpenBFME {version} Mac Unstable Preview (Apple Silicon)\ncommit {'0' * 40 if fault == 'wrong-commit' else commit}\n" \
            "committed 2026-10-16\n".encode()
        files["OpenBFME Launcher.app/Contents/Resources/OpenBFME Launcher.pck"] = test_macos_preview.game_pck(
            "build_info.json", (json.dumps(info, indent=2, sort_keys=True) + "\n").encode())
        if fault == "extra-file":
            files["OpenBFME.app/Contents/Resources/extra.txt"] = b"not a file of the package\n"
        for name, data in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(data)
        out.mkdir(parents=True, exist_ok=True)
        archive = out / f"{root.name}.zip"
        make_archive.zip_file(root, archive, 1_700_000_000)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out / f"{archive.name}.sha256").write_text(f"{'0' * 64 if fault == 'bad-checksum' else digest}  {archive.name}\n", encoding="ascii")
    (out / f"{root.name}.inventory.txt").write_text(f"Archive: {archive.name}\nSHA-256: {digest}\n")
    (out / "README-MAC.txt").write_text("OpenBFME for Mac\n")
    return archive


def zip_of(members: dict[str, bytes]) -> bytes:
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for n, d in members.items():
            # fixed timestamp: the bytes end up in parametrize ids, which must be identical on every xdist worker
            info = zipfile.ZipInfo(n, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o600 << 16  # what writestr(name, ...) sets
            z.writestr(info, d)
    return b.getvalue()


class World:
    """the archive repository, the public remote, gh, the key, the state and the stand-in build"""

    def __init__(self, tmp: Path):
        self.tmp = tmp
        self.repo = tmp / "archive"
        self.remote = tmp / "public.git"
        self.state = tmp / "state"
        # r6 / PUBLISH-2: autorelease.sh takes only the configured key path (OPENBFME_RELEASE_KEY, absolute and normalised): the runs get
        # a HOME of their own and a throwaway key in it
        self.home = tmp / "testhome"
        self.key = self.home / "release-keys" / "release-ed25519.pem"
        self.key.parent.mkdir(parents=True)
        # PUBLISH-2: the operator's private denylist (fictional names; the real list is kept outside the repository)
        self.denylist = tmp / "denylist.json"
        self.denylist.write_text(json.dumps(TEST_DENYLIST))
        pub = run(sys.executable, HERE / "sign_manifest.py", "--generate-key", self.key)
        assert pub.returncode == 0, pub.stderr
        # the public repository: main without PRIVACY.md (the owner removed it, PR #34)
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(self.remote)], check=True)
        seed = tmp / "seed"
        git(tmp, "init", "-q", "-b", "main", str(seed))
        commit(seed, {"README.md": "public\n"}, "public start")
        git(seed, "push", "-q", str(self.remote), "main")
        self.seed_main = git(seed, "rev-parse", "HEAD")
        # the archive repository: the release scripts, the launcher key, an engine file, the README line the sync replaces
        git(tmp, "init", "-q", "-b", "archive-legacy-codebase", str(self.repo))
        shutil.copytree(HERE, self.repo / "tools/release", ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"))
        shutil.copy2(REPO / "tools/precommit.py", self.repo / "tools/precommit.py")
        files = {"README.md": "OpenBFME\n" + ARCHIVE_LINE, "launcher/release_key.pub": pub.stdout.strip() + "\n",
                 # SIGNKMS-1: the trusted list, the Deck key first (the Mac launcher's build_info.json carries it)
                 "launcher/release_keys.pub": pub.stdout.strip() + "  deck-2026\n",
                 # RELGH-2: the nightly-only key list (none: the Mac launcher's build_info.json carries "nightly_keys": [])
                 "launcher/nightly_keys.pub": "# the nightly-only keys: none\n",
                 # the workflow GitHub builds the packages with (actions_build.py stage refuses a commit without it)
                 ".github/workflows/release-package.yml": (REPO / ".github/workflows/release-package.yml").read_text(),
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
from test_autorelease import package_dir, standin_build_hooks
import os
standin_build_hooks(Path(sys.argv[1]), sys.argv[2])
package_dir(Path(sys.argv[3]), sys.argv[2], extra=os.environ.get("STANDIN_EXTRA") == "1",
            legacy=os.environ.get("OPENBFME_RELEASE_LEGACY_LAUNCHERS", "1") == "1")
""")
        (self.standin / "verify").write_text('#!/bin/bash\necho "stand-in verify $3"; [ "${STANDIN_TAMPERED:-0}" = 0 ]\n')
        # MACACT-1: what the stand-in GitHub Actions run builds (gh_stand_in.py runs it at the dispatch): the Mac package as the Mac host
        # would, or a platform's GDExtension; it logs what it built and what the public remote had then (the tag must not be there yet)
        (self.standin / "actions-build").write_text(f"""#!{sys.executable}
import sys; sys.path.insert(0, {str(HERE)!r})
from pathlib import Path
from test_autorelease import mac_package_dir, actions_lib, LIBS
import json, os, subprocess
platform, checkout, version, out = sys.argv[1], Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4])
remote = os.environ["FAKE_GH_REMOTE"]
tag = subprocess.run(["git", "ls-remote", remote, "refs/tags/" + version], capture_output=True, text=True).stdout.strip()
head = subprocess.run(["git", "-C", str(checkout), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
open(os.environ["FAKE_ACTIONS_LOG"], "a").write(json.dumps({{"platform": platform, "version": version, "head": head, "remote_tag": tag}}) + "\\n")
if platform == "macos":
    if os.environ.get("STANDIN_MAC_FAIL") == "1":
        print("MAC BUILD FAIL: a stand-in failure"); sys.exit(1)
    mac_package_dir(checkout, version, out, os.environ.get("STANDIN_MAC_FAULT", ""))
else:
    p = "linux-x64" if platform == "linux" else "windows-x64"
    data = actions_lib(p, version, github=True, differs=os.environ.get("STANDIN_ACTIONS_FAULT") == "lib-differs")
    (out / LIBS[p]).write_bytes(data)
""")
        for f in ("build", "verify", "actions-build"):
            (self.standin / f).chmod(0o755)
        self.actions_log = tmp / "actions.jsonl"
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

    def copy(self, tmp: Path) -> World:
        shutil.copytree(self.tmp, tmp, dirs_exist_ok=True)
        world = object.__new__(type(self))
        world.__dict__ = {name: tmp / value.relative_to(self.tmp) if isinstance(value, Path) else value
                          for name, value in self.__dict__.items()}
        git(world.repo, "remote", "set-url", "origin", str(world.remote))
        return world

    def change(self, files: dict[str, str | bytes], message: str, force: bool = False) -> str:
        return commit(self.repo, files, message, force)

    def env(self, **extra) -> dict:
        return {"GH": str(self.gh), "FAKE_GH_STATE": str(self.gh_state), "FAKE_GH_REMOTE": str(self.remote), "FAKE_GH_LOG": str(self.gh_log),
                "OPENBFME_RELEASE_STATE": str(self.state), "OPENBFME_RELEASE_WORK": str(self.tmp / "work"),
                "OPENBFME_RELEASE_LOGS": str(self.tmp / "logs"), "HOME": str(self.home),
                "OPENBFME_RELEASE_KEY": str(self.key), "OPENBFME_RELEASE_DENYLIST": str(self.denylist),
                # the stand-in build imports this module: pytest from the user site of the real home
                "PYTHONPATH": str(Path(pytest.__file__).resolve().parents[1]),
                "OPENBFME_RELEASE_STANDIN": str(self.standin), "BFME_COMMUNITY": str(self.tmp / "no-community"),
                                "OPENBFME_RELEASE_MIN_COMMIT": self.first,
                "STANDIN_BUILD_LOG": str(self.tmp / "builds.jsonl"),
                "FAKE_GH_ACTIONS_BUILD": str(self.standin / "actions-build"), "FAKE_ACTIONS_LOG": str(self.actions_log),
                "OPENBFME_RELEASE_ACTIONS_POLL": "0",
                "PATH": f"{self.gh.parent}:{os.environ['PATH']}", **extra}

    def release(self, sha: str, *args: str, **env) -> subprocess.CompletedProcess:
        skip_on_macos()
        return run("bash", self.repo / "tools/release/autorelease.sh", sha, "--repo", TARGET, "--remote", "origin", *args, env=self.env(**env))

    def gh_data(self) -> dict:
        return json.loads(self.gh_state.read_text()) if self.gh_state.exists() else {"prs": {}, "releases": []}

    def remote_refs(self, pattern: str) -> str:
        return git(self.repo, "ls-remote", str(self.remote), pattern)


@pytest.fixture(scope="session")
def world_template(tmp_path_factory):
    return World(tmp_path_factory.mktemp("release-world"))


@pytest.fixture
def world(tmp_path, world_template):
    return world_template.copy(tmp_path)


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
        + ["SHA256SUMS-v0.4.5.txt", "manifest.json", "manifest.json.sig"]
        + ["openbfme-v0.4.5-macos-arm64.zip", "manifest-macos.json", "manifest-macos.json.sig"])   # MACREL-1
    check_mac_release(world, world.tmp / "work/v0.4.5/pkg", "v0.4.5", pr_merge(world))
    assert "Units walk around trees instead of through them" in rel["body"]
    # lane ONEZIP-1: the notes say which file to get, with a direct link per platform (never "(link removed)"), and name the rest once
    base = f"https://github.com/{TARGET}/releases/download/v0.4.5"
    for name in ("openbfme-v0.4.5-windows-x64.zip", "openbfme-v0.4.5-linux-x64.tar.gz"):
        assert f"| **[{name}]({base}/{name})** | **OpenBFME for " in rel["body"], rel["body"]
    assert "## Which file to download" in rel["body"] and "(link removed)" not in rel["body"]
    assert ("| openbfme-launcher-v0.4.5-windows-x64.zip, openbfme-launcher-v0.4.5-linux-x64.tar.gz, "
            "SHA256SUMS-v0.4.5.txt, manifest.json, manifest.json.sig, manifest-macos.json, manifest-macos.json.sig | Used by OpenBFME to update "
            "itself") in rel["body"]
    mac = "openbfme-v0.4.5-macos-arm64.zip"
    assert f"| **[{mac}]({base}/{mac})** | **OpenBFME for Mac (Apple Silicon, macOS 13 or later), an unstable preview.**" in rel["body"]
    # RELGH-2: the Mac package came from GitHub Actions (the stand-in run), no Mac host was asked
    assert [b["platform"] for b in actions_builds(world)] == ["macos"] and "ACTIONS BUILD OK macos v0.4.5" in r.stdout
    # RELCAND-1: no sync PR; the public main is the candidate's own commit, one commit on the old main, its tree the archive's without
    # archive/ plus the README line, no PRIVACY.md; the tag is the candidate's tag on it
    assert gh["prs"] == {} and world.remote_refs("refs/heads/sync/*") == ""
    public = pr_merge(world)
    main_tree = git(world.repo, "ls-tree", "--name-only", public)
    assert "archive" not in main_tree.split() and "PRIVACY.md" not in main_tree.split()
    assert "legacy-final" in git(world.remote, "show", f"{public}:README.md")
    assert git(world.remote, "rev-parse", "v0.4.5^{commit}") == public and git(world.remote, "rev-parse", f"{public}^") == world.seed_main
    assert "OpenBFME v0.4.5." in git(world.remote, "log", "-1", "--format=%B", public)
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


def pr_merge(world) -> str:
    """the released public commit (RELCAND-1: the candidate's commit, pushed as the public main; there is no sync PR any more)"""
    return git(world.remote, "rev-parse", "refs/heads/main")


def check_mac_release(world, pkg: Path, version: str, public: str) -> None:
    """MACREL-1: the release's Mac package and its separately signed manifest; the Windows / Linux manifest is byte for byte the one of
    the Windows / Linux packages alone (what a release without the Mac package has), so the released launchers read what they always read"""
    import tempfile
    import make_macos_manifest
    pub = bytes.fromhex(git(world.repo, "show", "HEAD:launcher/release_key.pub"))
    zip_name = f"openbfme-{version}-macos-arm64.zip"
    mac_bytes = (pkg / "manifest-macos.json").read_bytes()
    assert ed25519.verify(pub, mac_bytes, (pkg / "manifest-macos.json.sig").read_bytes()), "manifest-macos.json is signed with the release key"
    mac = json.loads(mac_bytes)
    main = json.loads((pkg / "manifest.json").read_bytes())
    assert {k: mac[k] for k in ("version", "repo", "commit", "date", "format")} == \
        {"version": version, "repo": TARGET, "commit": public, "date": main["date"], "format": 3}
    (entry,) = mac["assets"]["game"].values()
    data = (pkg / zip_name).read_bytes()
    assert list(mac["assets"]["game"]) == ["macos-arm64"] and entry["name"] == zip_name
    assert (entry["size"], entry["sha256"]) == (len(data), hashlib.sha256(data).hexdigest())
    assert f"{hashlib.sha256(data).hexdigest()}  {zip_name}" in (pkg / f"SHA256SUMS-{version}.txt").read_text().splitlines()
    assert "macos" not in (pkg / "manifest.json").read_text()
    with tempfile.TemporaryDirectory() as t:
        alone = Path(t)
        for f in pkg.iterdir():
            if f.name.startswith(("openbfme-", "SHA256SUMS-")) and "macos" not in f.name:
                shutil.copy2(f, alone / f.name)
        sums = alone / f"SHA256SUMS-{version}.txt"
        sums.write_text("".join(ln + "\n" for ln in sums.read_text().splitlines() if zip_name not in ln))
        rebuilt = make_manifest.build(alone, version, TARGET, main["commit"], main["date"], main["format"])
        assert make_manifest.dumps(rebuilt) == (pkg / "manifest.json").read_bytes(), "the Windows / Linux manifest is unchanged by the Mac package"
    assert make_macos_manifest.check_dir(pkg, git(world.repo, "show", "HEAD:launcher/release_key.pub")) == []


def test_a_failed_homepage_update_is_a_warning(world):
    """RELTEST-1: the release is out when the About link cannot be set: the run passes and the summary says to set it by hand"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT="homepage-fail")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE WARNING: the repository's About link could not be set" in r.stdout
    gh = world.gh_data()
    assert gh["releases"][0]["draft"] is False and gh.get("repo", {}).get("homepage", "") == ""


# ---- MACREL-1: the macOS package -------------------------------------------------------------------------------------------------------
def nothing_published(world, main: str = "") -> bool:
    """no PR, no sync branch, no tag, no release, no open intent, the public main where it was: nothing left the Deck"""
    gh = world.gh_data()   # RELGH-2: the macOS build runs on the Actions stand-in: its runs / artifacts are not publications, the staging branch is
    return (gh["prs"] == {} and gh["releases"] == [] and world.remote_refs("refs/heads/staging/*") == "" and world.remote_refs("refs/heads/sync/*") == ""
            and world.remote_refs("refs/tags/*") == "" and not (world.state / "intent").exists()
            and git(world.remote, "rev-parse", "refs/heads/main") == (main or world.seed_main))


def actions_builds(world) -> list[dict]:
    """what the stand-in GitHub Actions runs built (RELGH-2: the macOS package always)"""
    return [json.loads(x) for x in world.actions_log.read_text().splitlines()] if world.actions_log.exists() else []


def test_the_mac_build_host_is_refused_and_actions_is_the_default(world):
    """RELGH-2: OPENBFME_RELEASE_MAC_BUILDER=host is an error (exit 2, nothing pushed); unset builds macOS on the Actions stand-in"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    for extra in ((), ("--dry-run",)):
        r = world.release(sha, "--since", world.first, *extra, OPENBFME_RELEASE_MAC_BUILDER="host")
        assert r.returncode == 2, r.stdout + r.stderr
        assert "OPENBFME_RELEASE_MAC_BUILDER is actions" in r.stderr
        assert nothing_published(world), extra
        assert not actions_builds(world)
    assert world.remote_refs("refs/heads/*").split()[0] == world.seed_main and world.remote_refs("refs/tags/*") == ""
    r = world.release(sha, "--since", world.first)   # the variable unset
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert [b["platform"] for b in actions_builds(world)] == ["macos"]
    check_mac_release(world, world.tmp / "work/v0.4.5/pkg", "v0.4.5", pr_merge(world))


def test_skip_mac_is_explicit_and_said(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    for bad in ("yes", "true", "2", " 1"):
        r = world.release(sha, "--since", world.first, OPENBFME_RELEASE_SKIP_MAC=bad)
        assert r.returncode == 2 and "OPENBFME_RELEASE_SKIP_MAC is 1" in r.stderr and nothing_published(world), bad
    r = world.release(sha, "--since", world.first, OPENBFME_RELEASE_SKIP_MAC="1")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    warning = "AUTORELEASE WARNING: OPENBFME_RELEASE_SKIP_MAC=1: v0.4.5 is released WITHOUT the macOS package"
    assert warning in r.stdout and "AUTORELEASE WARNING: v0.4.5 has no macOS package (OPENBFME_RELEASE_SKIP_MAC=1)" in r.stdout
    summary = (world.tmp / "logs/autorelease-summary.log").read_text()
    assert warning in summary and "has no macOS package" in summary
    assert not actions_builds(world), "no macOS build is started"
    (rel,) = world.gh_data()["releases"]
    names = sorted(a["name"] for a in rel["assets"])
    assert "openbfme-v0.4.5-macos-arm64.zip" not in names and {"manifest-macos.json", "manifest-macos.json.sig"} <= set(names)
    assert "macos-arm64.zip" not in rel["body"] and "manifest-macos.json" in rel["body"]
    pkg = world.tmp / "work/v0.4.5/pkg"
    mac = json.loads((pkg / "manifest-macos.json").read_bytes())
    assert mac["assets"] == {"game": {}}, "a signed manifest naming no Mac package: the Mac launchers see no update, nothing to guess"
    pub = bytes.fromhex(git(world.repo, "show", "HEAD:launcher/release_key.pub"))
    assert ed25519.verify(pub, (pkg / "manifest-macos.json").read_bytes(), (pkg / "manifest-macos.json.sig").read_bytes())


@pytest.mark.parametrize("fault,why", [
    ("wrong-key", "the Mac launcher's build_info.json has release_key"),
    ("wrong-repo", "the Mac launcher's build_info.json has repo 'Other/repo'"),
    ("wrong-commit", "VERSION says"),
    ("extra-file", "extra.txt is not a file of the release"),
    ("bad-checksum", "does not exactly match"),
])
def test_a_bad_mac_package_is_refused_before_signing(world, fault, why):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, STANDIN_MAC_FAULT=fault)
    assert r.returncode == 1 and why in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE FAIL at step deck-verify (v0.4.5): openbfme-v0.4.5-macos-arm64.zip failed the Deck's checks" in r.stdout
    assert world.gh_data()["releases"] == [] and not (world.tmp / "work/v0.4.5/pkg/manifest-macos.json").exists()


def mac_publish_dir(tmp: Path, version: str, world: World, sha: str) -> Path:
    """publish_dir plus the Mac package, its checksum line and its signed manifest-macos.json (what autorelease.sh hands publish_release.sh)"""
    d = publish_dir(tmp, version, world, sha)
    m = json.loads((d / "manifest.json").read_bytes())
    date, public = m["date"], m["commit"]   # PUBLISH-2: publish_dir's manifest names the tagged public commit (the sync tree of sha)
    out = tmp / f"mac-{version}"
    archive = mac_package_dir(world.repo, version, out, commit=public)
    shutil.copy2(archive, d / archive.name)
    # MACPLAY-1: sealed like autorelease.sh seals it (payload.json inside the ZIP under the release key), then its own checksum
    import seal_package
    from sign_manifest import load_secret
    seal_package.seal_archive(d / archive.name, load_secret(Path(world.key)), TARGET)
    sealed = tmp / f"{archive.name}.sealed.sha256"
    sealed.write_text(f"{hashlib.sha256((d / archive.name).read_bytes()).hexdigest()}  {archive.name}\n")
    with open(d / f"SHA256SUMS-{version}.txt", "a") as f:
        f.write(sealed.read_text())
    r = run(sys.executable, world.repo / "tools/release/make_macos_manifest.py", "--version", version, "--repo", TARGET, "--commit", public,
            "--date", date, "--archive", d / archive.name, "--checksum", sealed, "--out", d / "manifest-macos.json")
    assert r.returncode == 0, r.stdout + r.stderr
    assert run(sys.executable, world.repo / "tools/release/sign_manifest.py", "--key", world.key, d / "manifest-macos.json").returncode == 0
    return d


def resign(world, path: Path) -> None:
    assert run(sys.executable, world.repo / "tools/release/sign_manifest.py", "--key", world.key, path).returncode == 0


def test_publish_checks_the_mac_manifest(world, tmp_path):
    """publish_release.sh refuses a Mac manifest that does not verify, does not match the folder's Mac package, or names another release,
    each before gh is asked anything; the untouched folder publishes the Mac package with the others"""
    sha = git(world.repo, "rev-parse", "HEAD")
    tool = world.repo / "tools/release/publish_release.sh"
    good = mac_publish_dir(tmp_path, "v0.4.5", world, sha)

    def edit_manifest(d, change):
        m = json.loads((d / "manifest-macos.json").read_bytes())
        change(m)
        (d / "manifest-macos.json").write_text(json.dumps(m, indent=2, sort_keys=True) + "\n")
        resign(world, d / "manifest-macos.json")

    def other_zip(d):
        version_dir = tmp_path / "other"
        world.change({"engine/a.cpp": "9\n"}, "another build")
        a = mac_package_dir(world.repo, "v0.4.5", version_dir)
        git(world.repo, "reset", "-q", "--hard", sha)
        shutil.copy2(a, d / a.name)

    cases = {
        "a changed byte, the old signature": (lambda d: (d / "manifest-macos.json").write_bytes((d / "manifest-macos.json").read_bytes() + b" "),
                                              "the signature of manifest-macos.json does not verify"),
        "another key's signature": (lambda d: (d / "manifest-macos.json.sig").write_bytes(
            ed25519.sign(os.urandom(32), (d / "manifest-macos.json").read_bytes())), "the signature of manifest-macos.json does not verify"),
        "no signature": (lambda d: (d / "manifest-macos.json.sig").unlink(), "manifest-macos.json and manifest-macos.json.sig come together"),
        "no Mac manifest, a Mac package": (lambda d: [(d / n).unlink() for n in ("manifest-macos.json", "manifest-macos.json.sig")],
                                            "a Mac package is in"),
        "re-signed with a wrong digest": (lambda d: edit_manifest(d, lambda m: m["assets"]["game"]["macos-arm64"].update(sha256="0" * 64)),
                                          "is not the manifest of openbfme-v0.4.5-macos-arm64.zip"),
        "re-signed for another commit": (lambda d: edit_manifest(d, lambda m: m.update(commit="1" * 40)), "manifest-macos.json has commit"),
        "re-signed with no entry": (lambda d: edit_manifest(d, lambda m: m["assets"].update(game={})),
                                    "manifest-macos.json names no Mac package, but openbfme-v0.4.5-macos-arm64.zip is in"),
        "another Mac package under the name": (other_zip, "does not list openbfme-v0.4.5-macos-arm64.zip with its digest"),
    }
    for label, (tamper, why) in cases.items():
        d = tmp_path / "tampered"
        shutil.rmtree(d, ignore_errors=True)
        shutil.copytree(good, d)
        tamper(d)
        r = run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.4.5", env=world.env())
        assert r.returncode != 0 and why in r.stdout + r.stderr, (label, r.stdout + r.stderr)
        assert not world.gh_log.exists(), label
    r = run("bash", tool, good, "--repo", TARGET, "--execute", "--confirm-tag", "v0.4.5", env=world.env())
    assert r.returncode == 0 and "PUBLISHED v0.4.5" in r.stdout, r.stdout + r.stderr
    (rel,) = world.gh_data()["releases"]
    assert {"openbfme-v0.4.5-macos-arm64.zip", "manifest-macos.json", "manifest-macos.json.sig"} <= {a["name"] for a in rel["assets"]}


def test_the_dry_run_signs_the_mac_manifest_with_the_throwaway_key(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, "--dry-run")
    assert r.returncode == 0 and "AUTORELEASE DRY RUN OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "WOULD RUN: python3" in r.stdout and "sign_manifest.py --key \\<release\\ key\\> " in r.stdout
    assert r.stdout.count("manifest-macos.json") >= 2 and nothing_published(world)
    pkg = world.tmp / "work/v0.4.5-dryrun/pkg"
    throwaway = bytes.fromhex((world.tmp / "work/v0.4.5-dryrun/dry-key/throwaway.pub").read_text().strip())
    for m in ("manifest.json", "manifest-macos.json"):
        assert ed25519.verify(throwaway, (pkg / m).read_bytes(), (pkg / f"{m}.sig").read_bytes()), m
    assert json.loads((pkg / "manifest-macos.json").read_bytes())["assets"]["game"]["macos-arm64"]["name"] == "openbfme-v0.4.5-macos-arm64.zip"


def test_the_public_tree_keeps_the_commit_gate_path_rule_and_packages_the_strict_one():
    """DEVPORT-6: audit_package's LOCAL_PATH (any /tmp/<dir>/, /home/<user>/, /Users/<user>/) is for release packages; the public source tree
    keeps tools/precommit.py's rule, which the commit gate already holds every file to (LOCAL_PATH refused the tree's own tests)"""
    import audit_package
    for text, package_flags, tree_flags in [
        (b"see /tmp/build/out for the log\n", True, False),
        (b"fixture /Users/u/.cargo/registry\n", True, False),
        (b"a path /" + b"home/deck/notes.txt\n", True, True),   # built here: this file itself passes the commit gate
        (b"C:" + b"\\Users\\someone\\x\n", True, True),
        (b"nothing private here\n", False, False),
    ]:
        pkg, tree = audit_package.Audit(), autorelease.SyncAudit()
        pkg.blob("a.txt", text)
        tree.blob("a.txt", text)
        assert bool(pkg.problems) is package_flags and bool(tree.problems) is tree_flags, (text, pkg.problems, tree.problems)


def test_the_download_table_has_the_mac_row_only_with_the_mac_package():
    base = "https://github.com/Open-BFME/openbfme-godot/releases/download/v0.4.7"
    without = "\n".join(autorelease.download_section("v0.4.7"))
    with_mac = "\n".join(autorelease.download_section("v0.4.7", mac=True))
    assert "macos" not in without
    name = "openbfme-v0.4.7-macos-arm64.zip"
    assert f"| **[{name}]({base}/{name})** | **OpenBFME for Mac (Apple Silicon, macOS 13 or later), an unstable preview.**" in with_mac
    assert "Intel Macs are not supported." in with_mac and "manifest.json.sig, manifest-macos.json, manifest-macos.json.sig |" in with_mac
    assert with_mac.replace(with_mac.split("\n")[6] + "\n", "", 1).replace(", manifest-macos.json, manifest-macos.json.sig", "") == without


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


def test_bridge_version_rules():
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


@pytest.fixture
def bridge_godot():
    if shutil.which(os.environ.get("GODOT", "godot")) is None:
        pytest.skip("no Godot executable (set GODOT) to run the bridge launcher's semver.gd grammar probe")


def test_bridge_rules(bridge_godot):
    # Sol r2: the grammar is run (Godot, sample tags), not searched for a word
    assert autorelease.knows_alpha((REPO / "launcher/scripts/core/semver.gd").read_text())
    assert not autorelease.knows_alpha("this is not gdscript"), "a semver.gd that does not parse"
    assert not autorelease.knows_alpha("extends RefCounted\nstatic func is_valid(t): return [][1]\n"), "one that stops on an error"
    lax = (REPO / "launcher/scripts/core/semver.gd").read_text().replace("(?:-(alpha|nightly|preview)", "(?:-(alpha|beta|nightly|preview)")
    assert lax != (REPO / "launcher/scripts/core/semver.gd").read_text()
    assert not autorelease.knows_alpha(lax), "a grammar that takes more than the launchers' is not the alpha grammar"


@pytest.mark.parametrize("tag,tree", [
    ("v0.3.0-preview.2", "4bc620603c6824421b967bb174f9ed42bea91b9e"),
    ("v0.3.0-preview.4", "b10f453de143a794ce9dd4e02baacc9fa9fd57ce"),
])
def test_bridge_historical_rules(bridge_godot, tag, tree):
    # docs/WINDOWS.md: these published trees are identical to the unpublished development-history pins.
    available = subprocess.run(["git", "-C", REPO, "cat-file", "-e", f"{tree}^{{tree}}"], capture_output=True, text=True)
    if available.returncode:
        pytest.skip(f"the pinned launcher tree {tree} of {tag} is absent; fetch refs/tags/{tag} from the repository")
    src = git(REPO, "show", f"{tree}:scripts/core/semver.gd")
    assert not autorelease.knows_alpha(src), tag
    hacked = src.replace('(0|[1-9][0-9]{0,8}))?$"', '(0|[1-9][0-9]{0,8}))?$"  # alpha', 1)
    assert hacked != src and not autorelease.knows_alpha(hacked), "a comment saying alpha is no alpha grammar"


def test_the_alpha_probe_fails_closed_without_godot(tmp_path, monkeypatch):
    monkeypatch.setenv("GODOT", str(tmp_path / "missing-godot"))
    with pytest.raises(SystemExit, match=r"Godot .* could not run .*the alpha check fails closed"):
        autorelease.knows_alpha((REPO / "launcher/scripts/core/semver.gd").read_text())


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
    r = world.release(b, FAKE_GH_FAULT="tag-read-fail")   # RELCAND-1: a failure after the push (a build failure leaves no tag)
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
    ({"PRIVACY.md": "p\n"}, "not a public top-level entry: PRIVACY.md"),
    ({"tools/reference/x.txt": "x\n"}, "inside a private folder (reference/)"),
    # r3 (Sol r2): retail bytes inside containers
    ({"docs/content.bin": gzip.compress(b"BIGF\0\0\0\0retail-payload", mtime=0)}, "a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: docs/content.bin"),
    ({"docs/nested.gz.bin": gzip.compress(zip_of({"a/b.bin": b"BIGF\0\0\0\0retail"}), mtime=0)}, "a binary file in the public tree (not UTF-8 text, or NUL bytes), not in public_binaries.txt: docs/nested.gz.bin"),
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


def test_sync_drops_a_stray_privacy_md_of_the_public_main(world):
    """a PRIVACY.md that comes back on the public main is not carried: the sync's tree is the archive sha's, so the PR deletes it"""
    seed = world.home.parent / "seed2"
    git(world.home.parent, "clone", "-q", str(world.remote), str(seed))
    git(seed, "config", "user.email", "t@example.com")
    git(seed, "config", "user.name", "t")
    commit(seed, {"PRIVACY.md": "stray\n"}, "a stray PRIVACY.md")
    git(seed, "push", "-q", "origin", "main")
    sha = world.change({"engine/b.cpp": "2\n"}, "engine change")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "PRIVACY.md" not in git(world.repo, "ls-tree", "--name-only", pr_merge(world)).split()


def test_audit_tree_of_the_real_public_tree():
    """the public tree of this checkout's HEAD (the sync recipe) passes the audit: the built-in rules, plus the operator's private
    denylist when OPENBFME_RELEASE_DENYLIST names it on this machine"""
    # PUBLICCI-1: in the public repository (the hosted pull request checks) HEAD already is a public tree: no archive/, no archive line
    has_archive = "archive" in subprocess.run(["git", "-C", str(REPO), "ls-tree", "--name-only", "HEAD"], capture_output=True, text=True,
                                              check=True).stdout.split()
    tree = autorelease.sync_tree(str(REPO), "HEAD") if has_archive else autorelease.git(str(REPO), "rev-parse", "HEAD^{tree}").strip()
    private = os.environ.get("OPENBFME_RELEASE_DENYLIST")
    rules = autorelease.load_denylist(Path(private)) if private else []
    assert autorelease.audit_tree(str(REPO), tree, rules) == []


# ---- the returned artifacts (Sol r1: whatever arrived was signed) -----------------------------------------------------------------------
def test_returned_artifacts_are_checked_before_signing(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and "at step deck-verify" in r.stdout
    assert not list((world.tmp / "work").rglob("manifest.json.sig")) and world.gh_data()["releases"] == []
    # RELCAND-1: the candidate failed before the push: no tag, no public commit, no intent, the local tag gone; the next run releases
    # the same number
    assert nothing_published(world) and not git(world.repo, "tag", "-l", "v0.4.5"), r.stdout
    assert "nothing was pushed: no tag and no public commit of v0.4.5 left the Deck" in r.stdout
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout and "already on origin" not in r.stdout, r.stdout + r.stderr
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
    """a signed package folder for version, at a tagged public commit (the sync tree of sha, as autorelease.sh tags it: publish_release.sh's
    privacy gate audits the tag's tree) of the archive repository (publish_release.sh run from there)"""
    sha = git(world.repo, "commit-tree", autorelease.sync_tree(str(world.repo), sha), "-p", sha, "-m", f"public {version}")
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
        [f"openbfme-{v}-linux-x64.tar.gz", f"openbfme-{v}-windows-x64.zip", f"SHA256SUMS-{v}.txt", "manifest.json", "manifest.json.sig",
         f"openbfme-{v}-macos-arm64.zip", "manifest-macos.json", "manifest-macos.json.sig"])   # MACREL-1: the Mac package and its sidecar
    (found,) = (world.tmp / "work").rglob("pkg/manifest.json")
    manifest = json.loads(found.read_bytes())
    assert manifest["format"] == 3 and sorted(manifest["assets"]) == ["game"]
    assert "OpenBFMELauncher.exe" in manifest["assets"]["game"]["windows-x64"]["files"]
    assert "openbfme-launcher-" not in rel["body"] and \
        f"| SHA256SUMS-{v}.txt, manifest.json, manifest.json.sig, manifest-macos.json, manifest-macos.json.sig | Used by OpenBFME" in rel["body"]


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
    r = world.release(a, "--since", world.first, FAKE_GH_FAULT="tag-read-fail")
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
    assert [(x["tag_name"], x["draft"], len(x["assets"])) for x in rels] == [("v0.4.5", False, 10)]   # 7 + MACREL-1's Mac package and sidecar
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
    assert world.release(sha, "--since", world.first, FAKE_GH_FAULT="tag-read-fail").returncode != 0
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
    assert world.remote_refs("refs/heads/sync/*") == "" and nothing_published(world), "the dry run publishes nothing on GitHub (RELGH-2: its macOS build run on the staging branch, deleted again, is all)"
    assert "WOULD RUN: " in r.stdout and "PATCH repos/" in r.stdout and "homepage=https://github.com/" in r.stdout  # RELTEST-1
    world.key.chmod(0o644)
    r2 = world.release(sha, "--since", world.first)
    assert r2.returncode != 0 and "is not mode 600" in r2.stdout
    world.key.chmod(0o600)
    # r6 / PUBLISH-2: the key is named by one exact path: a relative one (Sol r5: ./keyrepo/release.pem leaked its normalised path through
    # the signer), one with a '..' part or a symbolic link, or none at all, is refused without echoing it
    inside = world.tmp / "keyrepo"
    git(world.tmp, "init", "-q", str(inside))
    shutil.copy2(world.key, inside / "release.pem")
    (inside / "release.pem").chmod(0o600)
    r3 = world.release(sha, "--since", world.first, OPENBFME_RELEASE_KEY=str(inside / ".." / "keyrepo" / "release.pem"))
    assert r3.returncode == 2 and "is not the configured release key" in r3.stderr, r3.stdout + r3.stderr
    (world.tmp / "keylink").symlink_to(world.key.parent)
    r3b = world.release(sha, "--since", world.first, OPENBFME_RELEASE_KEY=str(world.tmp / "keylink" / world.key.name))
    assert r3b.returncode == 2 and "is not the configured release key" in r3b.stderr, r3b.stdout + r3b.stderr
    r3c = world.release(sha, "--since", world.first, OPENBFME_RELEASE_KEY="")
    assert r3c.returncode == 2 and "OPENBFME_RELEASE_KEY is not set" in r3c.stderr, r3c.stdout + r3c.stderr
    # the configured key inside a git work tree: the signer refuses it (below, r6) without naming it
    r3d = world.release(sha, "--since", world.first, OPENBFME_RELEASE_KEY=str(inside / "release.pem"))
    assert r3d.returncode != 0 and "<release key> is inside a git work tree" in r3d.stdout + r3d.stderr, r3d.stdout + r3d.stderr
    r4 = run("bash", world.repo / "tools/release/autorelease.sh", sha, "--since", world.first, "--repo", TARGET, "--remote", "origin",
             env=world.env(OPENBFME_RELEASE_KEY="./keyrepo/release.pem"), cwd=world.tmp)
    assert r4.returncode == 2 and "is not the configured release key" in r4.stderr
    r5 = world.release(sha, "--since", world.first, "--dry-run", OPENBFME_RELEASE_KEY=str(world.key))
    assert "is not the configured release key" not in r5.stdout + r5.stderr, "the configured path itself is accepted"
    # the signer's own diagnostics name the key: the configured key inside a git work tree is refused by sign_manifest.py itself
    git(world.home, "init", "-q", str(world.key.parent))
    r6 = world.release(sha, "--since", world.first)
    shutil.rmtree(world.key.parent / ".git")
    assert r6.returncode != 0 and "cannot read the release key's public half" in r6.stdout
    assert "<release key> is inside a git work tree" in r6.stdout + r6.stderr, r6.stdout + r6.stderr
    texts = [r.stdout, r.stderr, r2.stdout, r2.stderr, r3.stdout, r3.stderr, r4.stdout, r4.stderr, r6.stdout, r6.stderr]
    texts += [x for rr in (r3b, r3c, r3d) for x in (rr.stdout, rr.stderr)]
    texts += [p.read_text() for p in (world.tmp / "logs").glob("*.log")]
    for t in texts:
        for path in (key, str(world.key.resolve()), str(world.key.parent), str(world.home), str(inside / "release.pem"), "keyrepo/release.pem"):
            assert path not in t, (path, t[-1500:])


# ---- round 4 (Sol r3 REJECT) -----------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("data,why", [
    (zlib.compress(zlib.compress(b"BIGF\0\0\0\0retail")), "a binary file in the public tree"),
    (gzip.compress(zip_of({"in.bin": zlib.compress(b"BIGF\0\0\0\0retail")}), mtime=0), "a binary file in the public tree"),
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


def test_a_signature_that_looks_like_zlib_is_a_signature():
    """PUBLICCI-1: the hosted checks' first run failed test_returned_artifacts_are_checked_before_signing on a manifest.json.sig whose
    random bytes began with a valid zlib header ("unreadable zlib data"): 1 in 992 Ed25519 signatures do"""
    sig = b"\x78\x9c" + bytes(range(7, 69))   # a zlib header, then bytes no inflater accepts
    assert len(sig) == 64
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("manifest.json.sig", sig)
    a.blob("out/manifest-macos.json.sig", sig)
    assert a.problems == []
    # the exemption is that shape only: another name, another size or a member inside a container stays a finding
    for name, data in (("manifest.json.bin", sig), ("notes.sig", sig), ("manifest.json.sig", sig + b"\x00"), ("manifest.json.sig", sig[:63])):
        a = autorelease.SyncAudit(PRIVATE, top=True)
        a.blob(name, data)
        assert any("unreadable zlib data" in p for p in a.problems), (name, len(data), a.problems)
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("p.zip", zip_of({"manifest.json.sig": sig}))
    assert any("unreadable zlib data" in p for p in a.problems), a.problems
    # a .sig that IS a zlib stream is still decoded and its content checked
    hidden = zlib.compress(b"built on lab-vm-q".ljust(53, b" "), 0)
    assert len(hidden) == 64
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("manifest.json.sig", hidden)
    assert any(p.startswith("denylisted (hosts)") for p in a.problems), a.problems


def test_a_packed_payload_signature_that_looks_like_zlib_is_a_signature():
    """The package's random Ed25519 sidecar can also begin with a zlib header."""
    sig = b"\x78\x9c" + bytes(range(7, 69))
    path = "openbfme-v0.4.5-macos-arm64/payload.json.sig"
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("package.zip", zip_of({path: sig}))
    assert a.problems == []
    # Keep other members, wrong lengths, and deeper wrappers fail-closed.
    for members in ({path: sig[:-1]}, {path: sig + b"\x00"},
                    {path.replace("payload.json.sig", "notes.sig"): sig},
                    {"nested.zip": zip_of({path: sig})}):
        a = autorelease.SyncAudit(PRIVATE, top=True)
        a.blob("package.zip", zip_of(members))
        assert any("unreadable zlib data" in p for p in a.problems), a.problems
    # A valid compressed stream remains subject to content inspection.
    hidden = zlib.compress(b"built on lab-vm-q".ljust(53, b" "), 0)
    assert len(hidden) == 64
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("package.zip", zip_of({path: hidden}))
    assert any(p.startswith("denylisted (hosts)") for p in a.problems), a.problems


def test_linux_packed_payload_signature_that_looks_like_zlib():
    import tarfile
    sig = b"\x78\x9c" + bytes(range(7, 69))
    path = "openbfme-v0.4.5-linux-x64/payload.json.sig"
    def package(data):
        member = tarfile.TarInfo(path)
        member.size = len(data)
        return gzip.compress(tar_of([(member, data)]), mtime=0)
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("package.tar.gz", package(sig))
    assert a.problems == []
    for name, data in (("package.tar.gz", package(sig[:-1])),
                       ("package.tar.gz", package(sig + b"\x00")),
                       ("package.tar.gz", gzip.compress(zip_of({path: sig}), mtime=0)),
                       ("outer.zip", zip_of({"inner.tar.gz": package(sig)})),
                       ("package.tar.gz", gzip.compress(package(sig), mtime=0))):
        a = autorelease.SyncAudit(PRIVATE, top=True)
        a.blob(name, data)
        assert any("unreadable zlib data" in p for p in a.problems), a.problems
    hidden = zlib.compress(b"built on lab-vm-q".ljust(53, b" "), 0)
    assert len(hidden) == 64
    a = autorelease.SyncAudit(PRIVATE, top=True)
    a.blob("package.tar.gz", package(hidden))
    assert any(p.startswith("denylisted (hosts)") for p in a.problems), a.problems


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
    ("docs/prefixed.bin", b"header bytes " + gzip.compress(b"BIGF\0\0\0\0retail", mtime=0)),
    ("tools/install.sh", b"#!/bin/sh\ntail -n +3 \"$0\" | gunzip; exit\n" + gzip.compress(b"BIGF\0\0\0\0retail", mtime=0)),
    ("docs/picture.png", b"\x89PNG\r\n\x1a\n" + b"\0" * 16 + gzip.compress(b"BIGF\0\0\0\0retail", mtime=0)),
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
                 "built on buildbox-7", "account 998877665544", "on " + ip(192, 168, 4, 4)):
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
    for bad in ("/" + "home/x/y", "C:" + "\\Users\\x", "a\\@b\\.example\\.org", "me＠evil.example.org"):
        assert t("x", bad), bad
    # PUBLISH-2: the key's path and folder come from the operator's OPENBFME_RELEASE_KEY
    os.environ["OPENBFME_RELEASE_KEY"] = "/srv/keys-q/release.pem"
    try:
        for bad in ("/srv/keys-q/release.pem".replace(".", "\\."), "/srv/keys-q", "release.pem"):
            assert t("x", bad), bad
    finally:
        del os.environ["OPENBFME_RELEASE_KEY"]
    assert t("x", "/srv/keys-q") == [], "without OPENBFME_RELEASE_KEY no key path is known"


def test_the_redactor_takes_escaped_and_url_encoded_forms(world):
    import urllib.parse  # noqa: PLC0415
    key = str(world.key)
    env = {**os.environ, "AR_KEY": key, "AR_KEYDIR": str(world.key.parent), "AR_HOME": str(world.home)}
    text = "\n".join([key, autorelease.markdown(key), urllib.parse.quote(key), urllib.parse.quote(key, safe=""), str(world.home) + "/x"])
    out = subprocess.run([sys.executable, "-u", HERE / "autorelease.py", "redact", "--denylist", world.denylist], input=text.encode(),
                         capture_output=True, env=env).stdout.decode()
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
    monkeypatch.setattr(autorelease, "_PRIVATE_WORDS", ({"deck", "jonathan"}, {"buildbox-7"}))
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
    assert any("unknown area in brackets" in x for x in w("Added [Nowhere]: x"))
    assert any("';'" in x for x in w("Added [Online]: one thing; another thing"))
    assert any("201 characters" in x for x in w("Added: " + "x" * 201)) and w("Added: " + "x" * 200) == []
    assert w("Ends with;") == [], "a ';' joins clauses only when something follows"
    c = {"sha": "0123456789abcdef", "subject": "S", "body": "Body.\nRelease-note: Added [Foo]: a; b\nRelease-note: Fine.\nRelease-note: none\n"}
    ws = autorelease.release_warnings(c)
    assert ws == ["NOTES WARNING 0123456789 note 1: unknown area in brackets (it goes under General; areas: " + ", ".join(autorelease.AREAS) + ")",
                  "NOTES WARNING 0123456789 note 1: a ';' joins clauses: one note, one change"], ws
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
    # L10N-1: release_notes.gd and semver.gd preload the launcher's text runtime and its catalog
    shutil.copytree(HERE.parent.parent / "launcher/scripts/l10n", proj / "scripts/l10n", dirs_exist_ok=True)
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
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT="tag-read-fail")
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


# ---- PUBLISH-2: the sync recipe's exclusions and the denylist (after the 2026-10-15 history scrub) ------------------------------------------
def tree_paths(repo: Path, tree: str) -> list[str]:
    return git(repo, "ls-tree", "-r", "--name-only", tree).split("\n")


def test_sync_tree_drops_the_excluded_paths(world):
    """the CI configuration and the online deploy folder never reach the public tree (in step with the CI mirror); the sha's own list"""
    sha = world.change({".gitea/workflows/ci.yml": "on: push\n", "services/online/deploy/setup.sh": "echo\n", "services/online/DEPLOY.md": "d\n",
                        "services/online/relay/relay.cpp": "1\n", "engine/b.cpp": "2\n"}, "X-1: deploy and CI files")
    tree = autorelease.sync_tree(str(world.repo), sha)
    paths = tree_paths(world.repo, tree)
    assert "services/online/relay/relay.cpp" in paths and "engine/b.cpp" in paths and "README.md" in paths
    for gone in (".gitea/workflows/ci.yml", "services/online/deploy/setup.sh", "services/online/DEPLOY.md", "archive/old.txt"):
        assert gone not in paths, gone
    assert git(world.repo, "cat-file", "blob", f"{tree}:README.md").endswith("(tag `legacy-final`).")
    # the whole run: the merged public commit has the same tree (a release of a tree with the excluded folders would be refused by the audit)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    assert git(world.repo, "rev-parse", f"{pr_merge(world)}^{{tree}}") == tree


def test_sync_tree_without_its_exclusion_list_is_refused(world):
    sha = world.change({"engine/b.cpp": "2\n"}, "X-1: a change")
    git(world.repo, "rm", "-q", "tools/release/sync_exclude.txt")
    git(world.repo, "commit", "-q", "-m", "X-2: no list")
    sha = git(world.repo, "rev-parse", "HEAD")
    with pytest.raises(SystemExit, match="has no tools/release/sync_exclude.txt"):
        autorelease.sync_tree(str(world.repo), sha)
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "could not build the public tree" in r.stdout and world.remote_refs("refs/heads/sync/*") == "", r.stdout


@pytest.mark.parametrize("line", ["/services", "services/", "a/../b", "a\\\\b", "./x"])
def test_sync_exclusion_lines_must_be_repository_paths(world, line):
    sha = world.change({"tools/release/sync_exclude.txt": f"# x\n{line}\n"}, "X-1: a bad list")
    with pytest.raises(SystemExit, match="is not a repository-relative path"):
        autorelease.sync_tree(str(world.repo), sha)


# one case per built-in rule and per kind of private rule: (file, content, rule id that must catch it); every case is also audited
# without that rule and must then pass (so the rule, not something else, is what refuses it)
PRIVATE = autorelease.compile_rules(TEST_DENYLIST["rules"], "test")
DENY_CASES = [
    ("docs/net.md", f"the hub at {ip(192, 168, 7, 20)} serves it\n", "lan-192.168"),
    ("docs/net.md", f"reach {ip(10, 20, 30, 40)} first\n", "lan-10"),
    ("docs/net.md", f"the VM is {ip(172, 20, 1, 9)}\n", "lan-172"),
    ("docs/net.md", f"over {ip(100, 101, 102, 103)}\n", "cgnat-tailnet-ip"),
    ("docs/net.md", "ssh to box.tail1234." + "ts.net\n", "tailnet-name"),
    ("tools/x.sh", "cp key ~/.ssh/id_x\n", "private-path"),
    ("tools/x.sh", "cd /mnt/c/Users/someone/x\n", "private-path"),
    ("tools/x.py", 'ACCOUNT = "arn:aws:iam::1:role/x"\n', "aws-account"),
    ("tools/x.py", "aws account: 1234-5678-9012\n", "aws-account"),
    ("tools/x.py", "account_id = " + "ab" * 16 + "\n", "cloudflare-account"),
    ("docs/who.md", "mail owner.private@mailhost.net\n", "e-mail"),
    ("services/online/DEPLOY-notes.md", "x\n", "operational-file"),
    ("tools/net/backup_lobby.py", "x\n", "operational-file"),
    ("tools/run.log", "x\n", "operational-file"),
    ("docs/STATUS.md", "built on buildbox-7 today\n", "hosts"),
    ("docs/STATUS.md", "see ~opsuser/notes\n", "accounts"),
    ("docs/STATUS.md", "billing 998877665544\n", "account-ids"),
    ("docs/runbook-relay.md", "x\n", "ops-files"),
]


@pytest.mark.parametrize("path,text,rule", DENY_CASES)
def test_denylist_rule_catches_its_case(world, path, text, rule, monkeypatch):
    sha = world.change({path: text}, "X-1: a private detail")
    tree = autorelease.sync_tree(str(world.repo), sha)
    problems = autorelease.audit_tree(str(world.repo), tree, PRIVATE)
    hits = [p for p in problems if p.startswith(f"denylisted ({rule})")]
    assert hits, problems
    assert all(p.startswith("denylisted") for p in problems), problems
    # without the rule: the same tree passes (the other rules do not see it)
    monkeypatch.setattr(autorelease, "BUILTIN", [r for r in autorelease.BUILTIN if r[0] != rule])
    assert autorelease.audit_tree(str(world.repo), tree, [r for r in PRIVATE if r[0] != rule]) == []


@pytest.mark.parametrize("path,text", [
    ("engine/tests/test_lan.cpp", f'addr = "{ip(192, 168, 1, 5)}";\n'),          # synthetic LAN addresses in the game's own LAN tests
    ("docs/a.md", "Steam Deck and the lab VM\n"),                       # ordinary words
    ("docs/a.md", "1760627+Ancalgonn@users.noreply.github.com, noreply@anthropic.com, me@example.org\n"),
    ("docs/a.md", "version 1.10.20.3 and 3.192.168.1.40 are not addresses\n"),
    ("docs/a.md", "the opsuser group\n"),                                 # an account name only counts as /name, ~name or name@host
    ("tools/net/online_client.py", "x\n"), ("services/online/relay/upload_logs.py", "x\n"),
    ("docs/a.md", "buildbox-70 and xbuildbox-7\n"),                      # word boundaries
])
def test_denylist_leaves_ordinary_text_alone(world, path, text):
    sha = world.change({path: text}, "X-1: ordinary text")
    tree = autorelease.sync_tree(str(world.repo), sha)
    assert [p for p in autorelease.audit_tree(str(world.repo), tree, PRIVATE) if p.startswith("denylisted")] == []


def test_a_denylisted_tree_is_not_released(world):
    """the whole run: autorelease.sh hands its private denylist to the audit (a fictional host name in a doc stops the sync); --plan too"""
    sha = world.change({"docs/notes.md": "measured on lab-vm-q\n"}, "X-1: notes")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "denylisted (hosts) in docs/notes.md:1 (<8 chars>)" in r.stdout and "nothing pushed" in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()
    assert "lab-vm-q" not in r.stdout + r.stderr, "the audit does not print what it found"
    r = world.release(sha, "--since", world.first, "--plan")
    assert r.returncode != 0 and "failed the privacy audit" in r.stdout, r.stdout
    sha = world.change({"docs/notes.md": "measured on a lab VM\n"}, "X-2: notes")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, "--plan")
    tree = autorelease.sync_tree(str(world.repo), sha)
    assert r.returncode == 0 and f"public tree {tree}" in r.stdout and "SYNC AUDIT ok" in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()


@pytest.mark.parametrize("var", ["OPENBFME_RELEASE_DENYLIST", "OPENBFME_RELEASE_LOGS", "OPENBFME_RELEASE_KEY"])
def test_the_private_release_environment_is_required(world, var):
    sha = world.change({"engine/b.cpp": "2\n"}, "X-1: a change")
    world.gate(sha)
    for mode in ([], ["--plan"]):
        r = world.release(sha, "--since", world.first, *mode, **{var: ""})
        assert r.returncode == 2 and f"{var} is not set" in r.stderr, r.stdout + r.stderr
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()


@pytest.mark.parametrize("content,why", [
    (None, "cannot read the private denylist"),
    ("", "is not JSON"),
    ('{"rules": []}', "has no rules"),
    ('{"other": 1}', "an object with a rules list"),
    ('{"rules": [{"id": "x", "kind": "glob", "pattern": "a"}]}', "needs a kind"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": []}]}', "an empty or malformed pattern"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": [""]}]}', "an empty or malformed pattern"),
    ('{"rules": [{"id": "x", "kind": "regex", "pattern": "(unclosed"}]}', "is not a valid regular expression"),
    # PUBLISH-2 r2 (Sol r1 P2): a strict schema
    ('{"rules": [{"id": "x", "kind": "regex", "pattern": "a", "paths": "false"}]}', "paths is true or false"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["a"], "paths": true}]}', "paths applies to regex rules only"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["a"], "path": false}]}', "unknown field"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["a"], "severity": "info"}]}', "severity is fail or warn"),
    ('{"rules": [{"id": "", "kind": "word", "pattern": ["a"]}]}', "needs an id"),
    ('{"rules": ["x"]}', "is not an object"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["a"]}], "extra": 1}', "an object with a rules list"),
    ("\ufeff".encode("utf-16"), "is UTF-16"),
    ('{"rules": [{"id": "x", "kind": "regex", "pattern": "a", "allow": "docs/**"}]}', "allow is a list"),
])
def test_a_broken_private_denylist_is_an_error(tmp_path, content, why):
    f = tmp_path / "deny.json"
    if isinstance(content, bytes):
        f.write_bytes(content)
    elif content is not None:
        f.write_text(content)
    with pytest.raises(SystemExit, match=re.escape(why)):
        autorelease.load_denylist(f)


def test_a_broken_private_denylist_stops_the_release(world):
    sha = world.change({"engine/b.cpp": "2\n"}, "X-1: a change")
    world.gate(sha)
    world.denylist.write_text('{"rules": []}')
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "has no rules" in r.stdout + r.stderr and world.remote_refs("refs/heads/sync/*") == "", r.stdout


def test_audit_cli_reports_and_masks(world, tmp_path):
    sha = world.change({"docs/a.md": "on buildbox-7\n"}, "X-1: a")
    tree = autorelease.sync_tree(str(world.repo), sha)
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree, "--denylist", world.denylist)
    assert r.returncode == 1 and "SYNC AUDIT: denylisted (hosts) in docs/a.md:1 (<10 chars>)" in r.stdout and "buildbox-7" not in r.stdout
    # PUBLISH-2 r2 (Sol r1 P2): no denylist is a refusal, never a pass; the built-in rules alone only when asked, and labelled
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree)
    assert r.returncode == 2 and "no private denylist" in r.stderr, r.stdout + r.stderr
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree, "--builtin-only")
    assert r.returncode == 0 and "NOT a publishing check" in r.stdout, r.stdout
    note = tmp_path / "notes.md"
    note.write_text(f"Fixed on buildbox-7 at {ip(10, 1, 2, 3)}\n")
    r = run(sys.executable, HERE / "autorelease.py", "audit-text", note, "--denylist", world.denylist)
    assert r.returncode == 1 and "denylisted (hosts)" in r.stdout and "denylisted (lan-10)" in r.stdout, r.stdout


# ---- PUBLISH-2 r2 (Sol r1 REJECT): one privacy gate for every publishing path, nothing private printed, no fail-open mode -------------
def test_the_manual_publisher_runs_the_privacy_gate(world, tmp_path):
    """P1: publish_release.sh audits the tag's tree (what GitHub's source archives hold), the notes and the name with the private list"""
    tool = world.repo / "tools/release/publish_release.sh"
    leak = world.change({"docs/notes.md": "measured on lab-vm-q\n"}, "X-1: a private doc")
    d = publish_dir(tmp_path, "v0.3.0-preview.20", world, leak)
    r = run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.20", env=world.env())
    assert r.returncode != 0 and "privacy gate refused the tag's tree" in r.stderr and not world.gh_state.exists(), r.stdout + r.stderr
    assert "lab-vm-q" not in r.stdout + r.stderr
    r = run("bash", tool, d, "--repo", TARGET, env=world.env())   # the dry run too
    assert r.returncode != 0 and "privacy gate refused" in r.stderr
    clean = world.change({"docs/notes.md": "measured on a lab VM\n"}, "X-2: fixed")
    d = publish_dir(tmp_path, "v0.3.0-preview.21", world, clean)
    notes = tmp_path / "notes.md"
    notes.write_text("Fixed for opsuser@buildbox-7\n")
    r = run("bash", tool, d, "--repo", TARGET, "--notes-file", notes, "--execute", "--confirm-tag", "v0.3.0-preview.21", env=world.env())
    assert r.returncode != 0 and "privacy gate refused" in r.stderr and not world.gh_state.exists(), r.stdout + r.stderr
    r = run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.21", env=world.env(OPENBFME_RELEASE_DENYLIST=""))
    assert r.returncode != 0 and "no private denylist" in r.stderr and not world.gh_state.exists()
    # a tag on an archive commit (archive/ and the excluded paths in its tree) is refused: only a sync tree is a public tree
    git(world.repo, "tag", "-a", "v0.3.0-preview.22", "-m", "t", clean)
    d2 = package_dir(tmp_path / "pkg-raw", "v0.3.0-preview.22")
    assert run(sys.executable, world.repo / "tools/release/make_manifest.py", d2, "--repo", TARGET, "--commit", clean).returncode == 0
    assert run(sys.executable, world.repo / "tools/release/sign_manifest.py", "--key", world.key, d2 / "manifest.json").returncode == 0
    r = run("bash", tool, d2, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.22", env=world.env())
    assert r.returncode != 0 and "not a public top-level entry: archive" in r.stdout and not world.gh_state.exists(), r.stdout + r.stderr
    r = run("bash", tool, d, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.21", env=world.env())
    assert r.returncode == 0 and "PUBLISHED v0.3.0-preview.21" in r.stdout, r.stdout + r.stderr


def test_every_publishing_audit_call_passes_the_private_list():
    """P2: no audit call of the release scripts runs without the private denylist (the final notes check did)"""
    calls = 0
    for name in ("autorelease.sh", "publish_release.sh"):
        for line in (HERE / name).read_text().replace("\\\n", " ").splitlines():   # continuation lines joined
            if re.search(r"python3 .*autorelease\.py\"? .*\b(audit-tree|audit-text|privacy-gate)\b", line.split(" #")[0]):
                calls += 1
                assert "--denylist" in line, (name, line)
    assert calls >= 6, calls


@pytest.mark.parametrize("note", ["Fixed login for opsuser@localhost.", "Fixed [Online]: works on buildbox-7#2 now",
                                  "Fixed: see ~opsuser/notes"])
def test_private_rules_see_through_note_neutralisation(world, note):
    """P1: neutral() writes '@' / '#' full-width and markdown() '~' as a look-alike: the private rules check the original and every form"""
    assert autorelease.denylist_hits("n", autorelease.markdown(autorelease.neutral(note)), PRIVATE, check_path=False, metadata=True)
    sha = world.change({"engine/a.cpp": "5\n"}, "X-1: a change")
    git(world.repo, "commit", "-q", "--amend", "-m", "X-1: a change", "-m", "Release-note: " + note)
    sha = git(world.repo, "rev-parse", "HEAD")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "the public metadata failed the privacy audit: nothing pushed" in r.stdout, r.stdout
    assert world.remote_refs("refs/heads/sync/*") == "" and not world.gh_state.exists()
    logs = "".join(p.read_text() for p in (world.tmp / "logs").glob("*.log"))
    for secret in ("opsuser", "buildbox-7"):
        assert secret not in r.stdout + r.stderr + logs, secret


def test_note_sources_are_the_unneutralised_inputs(world):
    sha = world.change({"engine/a.cpp": "6\n"}, "X-1: a change")
    git(world.repo, "commit", "-q", "--amend", "-m", "X-1: a change", "-m", "Release-note: Fixed for a@b; and more")
    sha = git(world.repo, "rev-parse", "HEAD")
    src = autorelease.note_sources(autorelease.first_parent_commits(str(world.repo), world.first, sha), [{"text": "known", "area": ""}])
    assert src == ["Fixed for a@b; and more", "known"]


def test_nothing_rejected_is_printed(world, tmp_path, monkeypatch):
    """P1: file names, host / account diagnostics, note warnings and parser errors never print a rejected value, short ones included"""
    assert autorelease._mask("ab") == "<2 chars>" and "ab" not in autorelease._mask("ab")
    sha = world.change({"docs/buildbox-7/notes.md": "x\n", "docs/b.md": "on lab-vm-q\n"}, "X-1: names")
    tree = autorelease.sync_tree(str(world.repo), sha)
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree, "--denylist", world.denylist)
    assert r.returncode == 1 and "denylisted (hosts) in a file name" in r.stdout, r.stdout
    assert "buildbox-7" not in r.stdout + r.stderr and "lab-vm-q" not in r.stdout + r.stderr, r.stdout
    monkeypatch.setattr(autorelease, "_PRIVATE_WORDS", ({"zq"}, {"hostq7"}))
    probs = autorelease.public_text_problems("x", "built on hostq7 by ~zq")
    assert len(probs) == 2 and not any("hostq7" in p or "zq" in p.split(":", 1)[1] for p in probs), probs
    # the note warnings: masked, and printed only after the metadata audit passed (here it fails first: never printed)
    sha = world.change({"engine/a.cpp": "7\n"}, "X-2: a change")
    git(world.repo, "commit", "-q", "--amend", "-m", "X-2: a change", "-m", "Release-note: Fixed on buildbox-7; and on lab-vm-q too")
    sha = git(world.repo, "rev-parse", "HEAD")
    w = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "warnings", "--from", world.first, "--to", sha,
            "--denylist", world.denylist)
    assert "NOTES WARNING" in w.stdout and "buildbox-7" not in w.stdout and "lab-vm-q" not in w.stdout, w.stdout
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode != 0 and "NOTES WARNING" not in r.stdout and "buildbox-7" not in r.stdout + r.stderr, r.stdout
    # a YAML denylist that does not parse: its position, never its text
    pytest.importorskip("yaml")
    y = tmp_path / "deny.yml"
    y.write_text("rules:\n  - id: h\n    kind: word\n    pattern: [secret-host-q, \n")
    with pytest.raises(SystemExit) as e:
        autorelease.load_denylist(y)
    assert "secret-host-q" not in str(e.value) and "not valid YAML" in str(e.value)


def test_the_redactor_masks_private_rule_matches(world):
    out = subprocess.run([sys.executable, "-u", HERE / "autorelease.py", "redact", "--denylist", world.denylist],
                         input=b"first: docs/x on buildbox-7\nplain\n", capture_output=True).stdout.decode()
    assert out == "first: docs/x on <10 chars>\nplain\n", out


def test_pinned_binaries_and_container_members_pass_the_private_rules(world, tmp_path, monkeypatch):
    """P2: a hash-pinned binary's strings and a pinned container's members are scanned (a pinned hash never exempts a file)"""
    blob = b"\x00\x01ELF" + b"built on buildbox-7\x00\x01\x02" + "lab-vm-q".encode("utf-16-le") + b"\x00\x00"
    zipped = zip_of({"docs/inner.txt": b"from lab-vm-q\n"})
    sha = world.change({"docs/tool.bin": blob, "docs/pack.zip": zipped}, "X-1: binaries", force=True)
    allow = tmp_path / "public_binaries.txt"
    allow.write_text(f"{hashlib.sha256(blob).hexdigest()}  docs/tool.bin  test binary\n{hashlib.sha256(zipped).hexdigest()}  docs/pack.zip  test zip\n")
    monkeypatch.setattr(autorelease, "ALLOWLIST", allow)
    probs = autorelease.audit_tree(str(world.repo), autorelease.sync_tree(str(world.repo), sha), PRIVATE)
    assert any(p.startswith("denylisted (hosts) in docs/tool.bin") for p in probs), probs
    assert sum(p.startswith("denylisted (hosts) in docs/tool.bin") for p in probs) == 2, "the ASCII and the UTF-16 string"
    assert any("denylisted (hosts) in docs/pack.zip:docs/inner.txt" in p for p in probs), probs


def test_symlinks_and_unapproved_submodules_are_refused(world, tmp_path, monkeypatch):
    """P2: no symbolic link; submodules only as approved (path, commit, url)"""
    (world.repo / "docs").mkdir(exist_ok=True)
    os.symlink("../../etc/passwd", world.repo / "docs/link")
    git(world.repo, "add", "docs/link")
    git(world.repo, "commit", "-q", "-m", "X-1: a link")
    probs = autorelease.audit_tree(str(world.repo), autorelease.sync_tree(str(world.repo), git(world.repo, "rev-parse", "HEAD")), PRIVATE)
    assert "a symbolic link in the public tree: docs/link" in probs, probs
    git(world.repo, "rm", "-q", "docs/link")
    sub = world.first
    (world.repo / ".gitmodules").write_text('[submodule "engine/dep"]\n\tpath = engine/dep\n\turl = https://example.org/dep\n')
    git(world.repo, "add", ".gitmodules")
    git(world.repo, "update-index", "--add", "--cacheinfo", f"160000,{sub},engine/dep")
    git(world.repo, "commit", "-q", "-m", "X-2: a submodule")
    tree = autorelease.sync_tree(str(world.repo), git(world.repo, "rev-parse", "HEAD"))
    lst = tmp_path / "subs.txt"
    monkeypatch.setattr(autorelease, "SUBMODULES", lst)
    lst.write_text("# none\n")
    probs = autorelease.audit_tree(str(world.repo), tree, PRIVATE)
    assert "a submodule that is not in subs.txt: engine/dep" in probs, probs
    lst.write_text(f"{'0' * 40}  engine/dep  https://example.org/dep\n")
    assert "the submodule engine/dep is at another commit than subs.txt approves" in autorelease.audit_tree(str(world.repo), tree, PRIVATE)
    lst.write_text(f"{sub}  engine/dep  https://example.org/other\n")
    assert any(".gitmodules declares a submodule" in p for p in autorelease.audit_tree(str(world.repo), tree, PRIVATE))
    lst.write_text(f"{sub}  engine/dep  https://example.org/dep\n")
    assert autorelease.audit_tree(str(world.repo), tree, PRIVATE) == []


@pytest.mark.parametrize("data,why", [
    ("lab-vm-q here\n".encode("utf-16"), "is UTF-16"),
    (b"fine\x00lab-vm-q\n", "holds NUL bytes"),
    (b"\xff\xfeno", "is UTF-16"),
    (b"caf\xe9 lab-vm-q\n", "is not UTF-8"),
])
def test_metadata_is_strict_utf8(world, tmp_path, data, why):
    """P2: no replacement decoding: UTF-16, NUL bytes and other encodings are refused, not read around"""
    f = tmp_path / "notes.md"
    f.write_bytes(data)
    r = run(sys.executable, HERE / "autorelease.py", "audit-text", f, "--denylist", world.denylist)
    assert r.returncode == 1 and why in r.stdout, r.stdout


# ---- PUBLISH-2 r3 (review F1-F5 ACCEPT WITH FIXES, Sol r2 REJECT): the uploaded assets, masking in every form, fail-closed parsing ------
def every_form(text: str) -> str:
    return "\n".join(autorelease.text_forms(text))


def _leaks(text: str, secrets=("buildbox-7", "lab-vm-q", "opsuser")) -> list[str]:
    return [s for s in secrets if s in every_form(text).lower()]


def leaky_package(d: Path, version: str, member: bytes, name: str = "README_TESTERS.txt") -> Path:
    """package_dir with one member of the Linux game archive replaced (checksum file updated)"""
    package_dir(d, version)
    n = make_manifest.asset_name("game", version, "linux-x64")
    (d / n).write_bytes(tiny_package(n, {"OpenBFMELauncher.x86_64": f"the launcher of {version} for linux-x64".encode(),
                                         "OpenBFME.x86_64": f"{n} bytes".encode(), "VERSION": f"OpenBFME {version}\n".encode(), name: member}))
    sums = "".join(f"{hashlib.sha256((d / a).read_bytes()).hexdigest()}  {a}\n" for a in sorted(p.name for p in d.glob("openbfme-*")))
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    return d


def test_f1_the_uploaded_assets_pass_the_private_rules(world, tmp_path):
    """F1 (both reviews): an archive member that names a private host stops publish_release.sh, the dry run too; nothing is created"""
    tool = world.repo / "tools/release/publish_release.sh"
    sha = git(world.repo, "rev-parse", "HEAD")
    for i, member in enumerate([b"built on buildbox-7\n", "lab-vm-q".encode("utf-16-be"), b"\x00\x01" + "lab-vm-q".encode("utf-32-le")]):
        v = f"v0.3.0-preview.{40 + i}"
        d = publish_dir(tmp_path, v, world, sha)
        leaky_package(d, v, member, "README_TESTERS.txt" if i == 0 else "data.bin")
        r = run(sys.executable, world.repo / "tools/release/make_manifest.py", d, "--repo", TARGET, "--commit",
                git(world.repo, "rev-parse", f"{v}^{{commit}}"))
        assert r.returncode == 0, r.stdout + r.stderr
        assert run(sys.executable, world.repo / "tools/release/sign_manifest.py", "--key", world.key, d / "manifest.json").returncode == 0
        for args in ([], ["--execute", "--confirm-tag", v]):
            r = run("bash", tool, d, "--repo", TARGET, *args, env=world.env())
            assert r.returncode != 0 and "or an asset: nothing published" in r.stderr and not world.gh_state.exists(), (i, r.stdout + r.stderr)
            assert "in a release asset: denylisted (hosts) in openbfme-" in r.stdout, r.stdout
            assert not _leaks(r.stdout + r.stderr), r.stdout + r.stderr
    clean = publish_dir(tmp_path, "v0.3.0-preview.43", world, sha)
    r = run("bash", tool, clean, "--repo", TARGET, "--execute", "--confirm-tag", "v0.3.0-preview.43", env=world.env())
    assert r.returncode == 0 and "PUBLISHED v0.3.0-preview.43" in r.stdout and "7 release assets" in r.stdout, r.stdout + r.stderr


def elf_image(body: bytes, shnum: int = 4) -> bytes:
    """a 64-bit ELF: its header, body, then the section header table (where Godot's export sets the pck section's offset and size)"""
    head = bytearray(64)
    head[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<Q", head, 0x28, 64 + len(body))
    struct.pack_into("<HH", head, 0x3A, 64, shnum)
    return bytes(head) + body + bytes(64 * shnum)


def pe_image(body: bytes) -> bytes:
    """a PE: MZ header, PE signature, COFF and optional header, its headers 0x400 bytes long (SizeOfHeaders), then the body"""
    head = bytearray(0x400)
    head[:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, 0x40)
    head[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<I", head, 0x40 + 24 + 60, 0x400)
    return bytes(head) + body


# upstream bytes that match the private rules by chance (as Godot's locale tables do), and a "host-3" that starts at file offset 8192 (a
# block boundary of the r3 diff) in both images, which a changed byte before it may extend
def upstream(head: int) -> bytes:
    body = bytes(range(256)) * 16 + b"\x00NIQ lab-vm-q tlc\x00"
    return body + bytes(8192 - head - len(body)) + b"host-3 tail\x00" + bytes(4096)


UPSTREAM = upstream(64)
PLANTED = autorelease.compile_rules([{"id": "planted", "kind": "word", "pattern": ["secrethost-3"]}], "t")


def _templates(tmp_path: Path, files: dict[str, tuple[bytes, str]]) -> dict:
    pins = tmp_path / "pins.txt"
    pins.write_text("".join(f"{hashlib.sha256(t).hexdigest()}  {n}  {exe}  test template\n" for n, (t, exe) in files.items()))
    for n, (t, _) in files.items():
        (tmp_path / n).write_bytes(t)
    return autorelease.load_templates(tmp_path, pins)


def _with_pack(image: bytes) -> bytes:
    from test_release_tools import pck  # noqa: PLC0415
    pack = pck([("res://a.gd", b"extends Node\n")])
    return image + pack + struct.pack("<Q", len(pack)) + b"GDPC"


def _hosts(hits: list[str]) -> list[str]:
    return [h for h in hits if "denylisted (hosts)" in h]


@pytest.mark.parametrize("kind", ["elf", "pe"])
def test_f1_godot_templates_are_upstream_but_what_differs_is_scanned(tmp_path, kind):
    """the pinned templates' own bytes are not scanned (Godot's tables match short words by chance), but only for the executable each is
    pinned for; a copy with its pack added may differ from the template in the export region only, which is scanned whole"""
    image, region = (elf_image(UPSTREAM), lambda t: (len(t) - 256, len(t))) if kind == "elf" else (pe_image(UPSTREAM), lambda t: (0, 0x400))
    exe = "OpenBFMELauncher.x86_64" if kind == "elf" else "OpenBFMELauncher.exe"
    templates = _templates(tmp_path, {"tpl": (image, exe)})
    assert [v[1] for v in templates.values()] == [exe]
    assert autorelease.export_region(image) == region(image)
    rules = autorelease.BUILTIN + PRIVATE
    assert autorelease.content_hits(f"x/{exe}", image, rules, templates) == []
    assert _hosts(autorelease.content_hits(f"x/{exe}", image, rules, {})), "without its template it is scanned whole"
    a, b = region(image)
    benign = image[:b - 16] + bytes(range(1, 9)) + image[b - 8:]   # Godot sets the pck section's offset and size
    assert autorelease.content_hits(f"x/{exe}", _with_pack(benign), rules, templates) == []
    private = image[:a + 8] + b"from buildbox-7" + image[a + 23:]
    hits = autorelease.content_hits(f"x/{exe}", _with_pack(private), rules, templates)
    assert len(hits) == 1 and "denylisted (hosts)" in hits[0] and "buildbox-7" not in hits[0], hits
    (tmp_path / "tpl").write_bytes(image + b"x")
    assert autorelease.load_templates(tmp_path, tmp_path / "pins.txt") == {}, "a file with another sha256 is not the template"


@pytest.mark.parametrize("kind", ["elf", "pe"])
def test_sol_r3_1_a_template_exempts_nothing_it_does_not_explain(tmp_path, kind):
    """Sol r3 P2 (1): a denylisted string across two adjacent changed blocks and across a changed / unchanged boundary was found without
    the exemption and missed with it (the r3 diff joined changed 4096-byte blocks with a NUL and dropped unchanged ones); a template's
    bytes were exempt under any name (a same-sized project library). Now each is found, and the privacy gate's asset audit refuses it."""
    image = elf_image(upstream(64)) if kind == "elf" else pe_image(upstream(0x400))
    exe = "OpenBFMELauncher.x86_64" if kind == "elf" else "OpenBFMELauncher.exe"
    templates = _templates(tmp_path, {"tpl": (image, exe)})
    rules = autorelease.BUILTIN + PRIVATE + PLANTED
    assert image.index(b"host-3") == 8192
    adjacent = image[:4096 - 6] + b"secrethost-3" + image[4096 + 6:]
    boundary = image[:8192 - 6] + b"secret" + image[8192:]
    for case, data, name, rid in (("adjacent changed blocks", adjacent, exe, "planted"),
                                  ("changed / unchanged boundary", boundary, exe, "planted"),
                                  ("the template as a same-sized library", image, "openbfme.linux.template_debug.x86_64.so", "hosts"),
                                  ("the template under another executable's name", image, "OpenBFME.x86_64", "hosts")):
        found = lambda hits: [h for h in hits if f"denylisted ({rid})" in h]  # noqa: E731
        assert found(autorelease.content_hits(f"x/{name}", data, rules, {})), case
        assert found(autorelease.content_hits(f"x/{name}", _with_pack(data), rules, templates)), case
        asset = tmp_path / "openbfme-v0.4.7-linux-x64.zip"
        asset.write_bytes(zip_of({f"openbfme-v0.4.7-linux-x64/{name}": _with_pack(data)}))
        probs = autorelease.asset_problems([asset], rules, templates)
        assert found(probs) and not _leaks("\n".join(probs)) and "secrethost" not in "\n".join(probs), (case, probs)


def test_sol_r3_1_the_real_launchers_differ_from_their_templates_in_the_export_region_only():
    """the measured fact export_region() rests on (v0.4.6's launchers, Godot 4.7.2's templates): SKIPS LOUDLY without them"""
    if not os.environ.get("OPENBFME_TEST_RELEASE_DIR"):
        pytest.skip("SKIP: OPENBFME_TEST_RELEASE_DIR (a folder with the v0.4.6 packages unpacked) is not set")
    rel = Path(os.environ["OPENBFME_TEST_RELEASE_DIR"])
    tpl = Path.home() / ".local/share/godot/export_templates/4.7.2.stable"
    lin = rel / "openbfme-v0.4.6-linux-x64/OpenBFMELauncher.x86_64"
    if not (lin.is_file() and (tpl / "linux_release.x86_64").is_file()):
        pytest.skip("SKIP: the v0.4.6 Linux package or the Godot 4.7.2 templates are not here")
    templates = autorelease.load_templates(tpl)
    import audit_package  # noqa: PLC0415
    for data, exe in [(lin.read_bytes(), "OpenBFMELauncher.x86_64")] + \
            [(zipfile.ZipFile(z).read("openbfme-v0.4.6-windows-x64/OpenBFMELauncher.exe"), "OpenBFMELauncher.exe")
             for z in [rel / "openbfme-v0.4.6-windows-x64.zip"] if z.is_file()]:
        image = data[:audit_package.embedded_pck_base(data)]
        (f, _), = [v for v in templates.values() if v[1] == exe]
        t = f.read_bytes()
        a, b = autorelease.export_region(t)
        assert len(image) == len(t) and image[:a] == t[:a] and image[b:] == t[b:] and image != t, exe
        assert autorelease.content_hits(f"x/{exe}", data, autorelease.BUILTIN, templates) == [], exe


def _pack_mutations():
    """(case, a format-4 pack holding one permitted script with a private host name in one header or directory field)"""
    from test_release_tools import pck  # noqa: PLC0415
    good = pck([("res://a.gd", b"extends Node\n")])
    d = 4 + 5 * 4 + 16 + 64 + len(b"extends Node\n")   # the directory's offset
    name_len = 12   # "res://a.gd" + NUL, padded to 4
    def put(at: int, value: bytes) -> bytes:
        return good[:at] + value + good[at + len(value):]
    return good, [
        ("reserved header words", put(40 + 8, b"buildbox-7")),
        ("version numbers", put(8, b"lab-vm-q")),
        ("pack flags", put(20, b"lab\x00")),
        ("files offset", put(24, b"lab-vm-q")),
        ("entry MD5", put(d + 8 + name_len + 16, b"buildbox-7")),
        ("entry flags", put(d + 8 + name_len + 32, b"lab\x00")),
        ("bytes after the entry name", put(d + 8 + 11, b"x")),
    ]


def test_sol_r3_2_every_pack_header_and_directory_byte_is_checked(tmp_path):
    """Sol r3 P2 (2): a host name in a pack's reserved header words gave zero privacy and zero allowlist findings; so did every other
    free field (pattern 7: each sibling). Each is refused by the package audit, so the privacy gate's asset audit fails."""
    import audit_package  # noqa: PLC0415
    good, cases = _pack_mutations()
    assert audit_package.pck_entries(good) == [("res://a.gd", b"extends Node\n")]
    rules = autorelease.BUILTIN + PRIVATE
    asset = tmp_path / "openbfme-v0.4.7-linux-x64.zip"
    asset.write_bytes(zip_of({"openbfme-v0.4.7-linux-x64/OpenBFME.pck": good}))
    assert autorelease.asset_problems([asset], rules, {}) == []
    for case, data in cases:
        with pytest.raises(ValueError):
            audit_package.pck_entries(data)
        asset.write_bytes(zip_of({"openbfme-v0.4.7-linux-x64/OpenBFME.pck": data}))
        probs = autorelease.asset_problems([asset], rules, {})
        assert probs and not _leaks("\n".join(probs)), (case, probs)
        a = audit_package.Audit()
        a.blob("OpenBFME.pck", data)
        assert a.problems, case


def _gzip_member(payload: bytes, name: bytes = b"", comment: bytes = b"", extra: bytes = b"", mtime: bytes = bytes(4)) -> bytes:
    flags = (8 if name else 0) | (16 if comment else 0) | (4 if extra else 0)
    head = b"\x1f\x8b\x08" + bytes([flags]) + mtime + b"\x00\xff"
    if extra:
        head += len(extra).to_bytes(2, "little") + extra
    head += (name + b"\0" if name else b"") + (comment + b"\0" if comment else b"")
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return head + c.compress(payload) + c.flush() + struct.pack("<II", zlib.crc32(payload), len(payload))


@pytest.mark.parametrize("field", ["name", "comment", "extra", "mtime"])
def test_sol_r3_3_every_gzip_members_header_is_checked(tmp_path, field):
    """Sol r3 P2 (3): two valid gzip members with clean payloads, a host name only in the second member's file name: zero findings, and
    the tree's metadata check accepted it; the comment, extra field and time of a later member too"""
    rules = autorelease.BUILTIN + PRIVATE + autorelease.compile_rules([{"id": "short", "kind": "word", "pattern": ["qvzx"]}], "t")
    value = b"qvzx" if field == "mtime" else b"lab-vm-q"   # RELFIX-3: 4 letters (3 and fewer in binary bytes are chance)
    data = _gzip_member(b"clean one\n") + _gzip_member(b"clean two\n", **{field: value})
    assert gzip.decompress(data) == b"clean one\nclean two\n"
    assert [f for f, _ in autorelease.gzip_headers(data)] == [0, {"name": 8, "comment": 16, "extra": 4, "mtime": 0}[field]]
    hits = autorelease.content_hits("x.gz", data, rules)
    assert hits and not _leaks("\n".join(hits)), hits
    asset = tmp_path / "BUILDINFO.gz"
    asset.write_bytes(data)
    assert autorelease.asset_problems([asset], rules, {}), "the privacy gate's asset audit"
    if field != "mtime":
        probs = autorelease.container_metadata_problems("docs/a.gz", data)
        assert probs == [f"docs/a.gz: gzip member 2's header has a file name, comment or extra field"], probs
    clean = _gzip_member(b"clean one\n") + _gzip_member(b"clean two\n")
    assert autorelease.content_hits("x.gz", clean, rules) == [] and autorelease.container_metadata_problems("docs/a.gz", clean) == []


FULL_WIDTH = "".join(chr(ord(c) + 0xFEE0) if c.isalnum() else c for c in "buildbox-7")   # ｂｕｉｌｄｂｏｘ-７


@pytest.mark.parametrize("enc", ["utf-8", "utf-16-le", "utf-16-be", "utf-32-le", "utf-32-be"])
@pytest.mark.parametrize("text", [FULL_WIDTH, "buildbox\u20117", "ｌａｂ\u2011ｖｍ\u2011ｑ"])
def test_sol_r3_4_normalised_spellings_are_found_in_every_encoding(tmp_path, enc, text):
    """Sol r3 P2 (4): a full-width host name was found in UTF-8 but not in UTF-16 / UTF-32 (the privacy gate passed the UTF-16 case); the
    non-breaking hyphen (U+2011 normalises to U+2010, not '-') was missed in UTF-8 binaries too; every alignment"""
    rules = autorelease.BUILTIN + PRIVATE
    for pad in range(4):
        data = b"\x00\xfe" + bytes(pad) + text.encode(enc) + b"\x00\xfe\x01"
        assert _hosts(autorelease.content_hits("x/data.bin", data, rules)), (enc, pad)
    asset = tmp_path / "openbfme-v0.4.7-linux-x64.zip"
    asset.write_bytes(zip_of({"openbfme-v0.4.7-linux-x64/data.bin": b"\x00\x01" + text.encode(enc) + b"\x00"}))
    probs = autorelease.asset_problems([asset], rules, {})
    assert any("denylisted (hosts)" in p for p in probs) and not _leaks("\n".join(probs)), probs


def test_f2_warnings_quote_nothing_and_masking_sees_every_form(world):
    """F2: a warning prints nothing of the note (a `Release-note: none` commit's, and --plan's, have passed no audit); sanitize masks a
    full-width '@' form too"""
    c = {"sha": "0123456789abcdef", "body": "Release-note: none\nRelease-note: Added [opsuser＠buildbox-7]: one; two\n"}
    out = "\n".join(autorelease.release_warnings(c))
    assert out and not _leaks(out), out
    masked = autorelease.sanitize("NOTES: opsuser＠buildbox-7 and \\b\\u\\i\\l\\d\\b\\o\\x-7", PRIVATE)
    assert not _leaks(masked) and "<" in masked, masked
    sha = world.change({"engine/a.cpp": "7\n"}, "X-1: a change")
    git(world.repo, "commit", "-q", "--amend", "-m", "X-1: a change", "-m", "Release-note: none\nRelease-note: Fixed for opsuser＠buildbox-7; more")
    sha = git(world.repo, "rev-parse", "HEAD")
    world.gate(sha)
    git(world.repo, "tag", "v0.3.0-preview.4", world.first)
    git(world.repo, "push", "-q", "origin", "refs/tags/v0.3.0-preview.4")
    r = world.release(sha, "--plan", "--since", world.first)
    assert r.returncode == 0 and "NOTES WARNING" in r.stdout and not _leaks(r.stdout + r.stderr), r.stdout + r.stderr


def test_f3_schema_errors_quote_no_field_name(tmp_path):
    f = tmp_path / "d.json"
    f.write_text(json.dumps({"rules": [{"id": "x", "kind": "word", "pattern": ["a"], "lab-vm-q.internal": 1}]}))
    with pytest.raises(SystemExit) as e:
        autorelease.load_denylist(f)
    assert "1 unknown field(s)" in str(e.value) and "lab-vm" not in str(e.value), e.value


def test_f4_file_names_are_checked_and_masked_in_every_form(world):
    """F4: a full-width file name passes the rules as written; the NFKC form is checked, and printed masked"""
    wide = "docs/ｌａｂ-vm-q.txt"
    hits = autorelease.denylist_hits(wide, None, PRIVATE)
    assert hits and "denylisted (hosts) in a file name" in hits[0] and not _leaks(hits[0]), hits
    sha = world.change({wide: "x\n"}, "X-1: a wide name")
    tree = autorelease.sync_tree(str(world.repo), sha)
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree, "--denylist", world.denylist)
    assert r.returncode == 1 and "denylisted (hosts) in a file name" in r.stdout and not _leaks(r.stdout), r.stdout


def test_f5_an_unloadable_list_stops_before_anything_is_printed(world, tmp_path):
    bad = tmp_path / "bad.json"
    bad.write_text('{"rules": [ {"id": "x", "kind": "word", "pattern": ["buildbox-7"] ')
    r = world.release(git(world.repo, "rev-parse", "HEAD"), "--plan", "--since", world.first, OPENBFME_RELEASE_DENYLIST=str(bad))
    assert r.returncode == 2 and "OPENBFME_RELEASE_DENYLIST cannot be loaded: autorelease: the private denylist is not JSON" in r.stderr, r.stderr
    assert "AUTORELEASE " not in r.stdout and not _leaks(r.stdout + r.stderr), r.stdout + r.stderr
    # the redactor alone: fail closed, nothing of its input passes
    r = subprocess.run([sys.executable, HERE / "autorelease.py", "redact", "--denylist", bad], input=b"on buildbox-7\nplain\n",
                       capture_output=True)
    assert r.returncode == 3 and b"REDACTOR FAILED" in r.stdout and b"plain" not in r.stdout and b"buildbox" not in r.stdout, r.stdout


def test_sol1_a_resumed_draft_publishes_the_audited_name_and_body(world, tmp_path):
    """Sol r2 P2: the draft an earlier run left with private text in its name and body is published with the audited ones (replaced and
    checked), never with its own"""
    tool = world.repo / "tools/release/publish_release.sh"
    v = "v0.3.0-preview.44"
    d = publish_dir(tmp_path, v, world, git(world.repo, "rev-parse", "HEAD"))
    world.gh_state.write_text(json.dumps({"prs": {}, "next": 8, "releases": [
        {"id": 7, "tag_name": v, "name": "OpenBFME opsuser@buildbox-7", "draft": True, "prerelease": True, "body": "built on lab-vm-q",
         "assets": []}]}))
    notes = tmp_path / "notes.md"
    notes.write_text("The audited notes.\n")
    r = run("bash", tool, d, "--repo", TARGET, "--notes-file", notes, "--execute", "--confirm-tag", v, "--resume-id", "7", env=world.env())
    assert r.returncode == 0 and "PUBLISHED" in r.stdout and "replacing them" in r.stdout, r.stdout + r.stderr
    rel = world.gh_data()["releases"][0]
    assert rel["draft"] is False and rel["name"] == autorelease.release_title(v) and rel["body"] == "The audited notes.\n", rel
    # the read-back check refuses a release whose name or body is not the audited one
    want = release_assets.expected(d, v)
    assert "the release's body is not the audited notes" in release_assets.check({**rel, "body": "x"}, want, v, False, True, TARGET,
                                                                                 name=rel["name"], body="The audited notes.\n")


@pytest.mark.parametrize("text,suffix", [
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["buildbox-7"], "pattern": ["harmless"]}]}', ".json"),
    ('{"rules": [{"id": "x", "kind": "word", "pattern": ["buildbox-7"]}], "rules": [{"id": "y", "kind": "word", "pattern": ["z"]}]}', ".json"),
    ("rules:\n  - id: x\n    kind: word\n    pattern: [buildbox-7]\n    pattern: [harmless]\n", ".yml"),
    ("rules:\n  - {id: x, kind: word, pattern: [a]}\nrules:\n  - {id: y, kind: word, pattern: [b]}\n", ".yml"),
])
def test_sol2_a_key_given_twice_is_an_error(tmp_path, text, suffix):
    if suffix == ".yml":
        pytest.importorskip("yaml", reason="SKIP: no PyYAML here (a YAML list is then refused by name: test_a_broken_private_denylist_is_an_error)")
    f = tmp_path / f"d{suffix}"
    f.write_text(text)
    with pytest.raises(SystemExit) as e:
        autorelease.load_denylist(f)
    assert "gives a key twice" in str(e.value) and "buildbox" not in str(e.value), e.value


@pytest.mark.parametrize("member", [
    b"\x01\x02qvzx\x00\x03",                        # a 4-character literal alone between binary bytes (RELFIX-3: 3 and fewer are chance)
    "lab-vm-q".encode("utf-16-be"), b"\x00" + "lab-vm-q".encode("utf-16-le"), "lab-vm-q".encode("utf-32-le"),
    b"\x00\x00" + "lab-vm-q".encode("utf-32-be"), b"\xff\x00" + "gästhost".encode("utf-8"),
])
def test_sol3_binary_members_are_read_in_every_encoding(member):
    rules = PRIVATE + autorelease.compile_rules([{"id": "short", "kind": "word", "pattern": ["qvzx", "gästhost"]}], "t")
    a = autorelease.SyncAudit(rules)
    a.blob("p.zip", zip_of({"data.bin": b"\x00\x01" + member + b"\x00\xfe"}))
    assert any(p.startswith("denylisted (") and "data.bin" in p for p in a.problems), a.problems


def test_relfix3_three_letter_words_count_in_text_but_not_in_binary_bytes():
    """RELFIX-3: v0.4.9's publish refused 3-letter private words that 100 MB of executables hold by chance (machine code, ICU tables);
    in binary bytes they are no finding, in a text member, a member name and the notes they still are"""
    rules = PRIVATE + autorelease.compile_rules([{"id": "short", "kind": "word", "pattern": ["qvz"]}], "t")
    a = autorelease.SyncAudit(rules)
    a.blob("p.zip", zip_of({"data.bin": b"\x00\x01\x02qvz\x00\x03\xfe" + "qvz".encode("utf-16-le") + b"\xfe"}))
    assert not [p for p in a.problems if "denylisted (short)" in p], a.problems
    for member in ({"notes.txt": b"built on qvz\n"}, {"qvz/data.bin": b"\x00\x01"}):
        a = autorelease.SyncAudit(rules)
        a.blob("p.zip", zip_of(member))
        assert [p for p in a.problems if "denylisted (short)" in p], (member, a.problems)
    assert autorelease.denylist_hits("notes.md", "built on qvz", rules, check_path=False, metadata=True)


def test_relfix4_short_email_shapes_in_binary_bytes_are_chance():
    """RELFIX-4: the v0.4.9 macOS executables hold "T@9.xk" in ARM64 machine code; 7 characters or fewer in binary bytes is no hit,
    a real address in the same bytes still is, and in text even a short one is"""
    rules = autorelease.BUILTIN
    code = b"\x00K\x00\x00\x00\x00T@9.xk\x00\x00`\x00"
    assert autorelease.binary_rule_hits(code, rules).get("e-mail") is None
    assert autorelease.binary_rule_hits(code + b"\x00someone@privatehost.org\x00", rules).get("e-mail")
    assert autorelease.denylist_hits("notes.md", "write to ab@cd.ef", rules, check_path=False, metadata=True)


def test_relfix3_freetype_mailing_lists_are_public():
    rules = autorelease.BUILTIN
    assert autorelease.denylist_hits("x.bin", None, rules, data=b"\x00o freetype-devel@nongnu.org\x00") == []
    assert autorelease.denylist_hits("x.bin", None, rules, data=b"\x00mail someone@privatehost.org\x00")


def test_sol3_a_text_member_in_another_encoding_is_refused():
    a = autorelease.SyncAudit(PRIVATE)
    a.blob("p.zip", zip_of({"notes.txt": "fine".encode("utf-16"), "ok.txt": b"fine\n"}))
    assert [p for p in a.problems if "not UTF-8" in p] == ["a text file that is not UTF-8 without NUL bytes (an encoding the audit does not "
                                                           "read): p.zip:notes.txt"], a.problems


def test_sol4_the_standalone_publisher_redacts_from_its_first_line(world):
    """Sol r2 P2: a dry run printed the package folder's absolute path; a private word in it, or the home folder, is masked"""
    tool = world.repo / "tools/release/publish_release.sh"
    sha = git(world.repo, "rev-parse", "HEAD")
    d = publish_dir(world.home / "buildbox-7", "v0.3.0-preview.45", world, sha)
    r = run("bash", tool, d, "--repo", TARGET, env=world.env())
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr
    assert str(world.home) not in r.stdout + r.stderr and "~/<10 chars>/pkg-v0.3.0-preview.45" in r.stdout, r.stdout
    assert not _leaks(r.stdout + r.stderr)
    # a helper's error that names a private member is masked too
    (d / "manifest.json").write_text((d / "manifest.json").read_text().replace("openbfme-v0.3.0-preview.45-linux-x64", "openbfme-buildbox-7-linux-x64"))
    r = run("bash", tool, d, "--repo", TARGET, env=world.env())
    assert r.returncode != 0 and not _leaks(r.stdout + r.stderr), r.stdout + r.stderr


def test_sol5_anchored_path_rules_are_masked_inside_messages(world, tmp_path):
    """Sol r2 P2: a rule anchored at the path's start (`^docs/<private folder>/`) matched the file name, never a message that quotes it"""
    rule = {"id": "private-folder", "kind": "regex", "pattern": "^docs/runbook/"}
    rules = PRIVATE + autorelease.compile_rules([rule], "t")
    line = autorelease.sanitize("retail-format file name in the public tree: docs/runbook/a.dds", rules)
    assert "runbook" not in line and "<" in line, line
    deny = tmp_path / "deny.json"
    deny.write_text(json.dumps({"rules": TEST_DENYLIST["rules"] + [rule]}))
    sha = world.change({"docs/runbook/a.dds": "x\n"}, "X-1: a retail-named file")
    tree = autorelease.sync_tree(str(world.repo), sha)
    r = run(sys.executable, HERE / "autorelease.py", "--git-dir", world.repo, "audit-tree", "--tree", tree, "--denylist", deny)
    assert r.returncode == 1 and "denylisted (private-folder) in a file name" in r.stdout and "retail" in r.stdout, r.stdout
    assert "runbook" not in r.stdout, r.stdout


SPACED = [
    ({"id": "private-folder", "kind": "regex", "pattern": "^docs/private folder/"}, "docs/private folder/"),
    ({"id": "private-file", "kind": "regex", "pattern": "/secret plan\\.md$"}, "/secret plan.md"),
    ({"id": "private-path", "kind": "regex", "pattern": "^docs/private folder/a b\\.md$"}, "private folder/a b.md"),
]


@pytest.mark.parametrize("rule,secret", SPACED)
def test_sol_r3_5_anchored_rules_mask_paths_with_spaces_inside_messages(rule, secret):
    """Sol r3 P2 (5): `^docs/private folder/` found the file name but could not mask it inside a diagnostic (words split at spaces); a
    rule anchored at the end, or at both ends, likewise. In every quoting a message gives a path, and in a full-width form."""
    rules = PRIVATE + autorelease.compile_rules([rule], "t")
    path = "docs/private folder/a b.md" if rule["id"] == "private-path" else \
        ("docs/private folder/notes.md" if rule["id"] == "private-folder" else "docs/secret plan.md")
    assert autorelease.denylist_hits(path, None, rules), "the rule finds the file name"
    for line in (f"make_manifest: x.tar.gz: member {path!r} is not a file of the package folder top",
                 f"PUBLISH FAIL: {path} is missing", f'error at "{path}": unreadable', f"({path})", f"a: {path}: b",
                 f"x {path.replace('/', '／')} y"):
        out = autorelease.sanitize(line, rules)
        assert secret not in out and secret.replace("/", "／") not in out and "<" in out, (line, out)


def test_sol_r3_5_the_publisher_masks_a_helpers_diagnostic_with_a_spaced_path(world, tmp_path):
    """the reviewer's example through the real entry point: make_manifest.py (run before the privacy gate, so no file name of it is
    recorded yet) quotes a package member under `docs/private folder/`; publish_release.sh's redactor masks it"""
    tool = world.repo / "tools/release/publish_release.sh"
    sha = git(world.repo, "rev-parse", "HEAD")
    d = publish_dir(tmp_path, "v0.3.0-preview.46", world, sha)
    n = make_manifest.asset_name("game", "v0.3.0-preview.46", "linux-x64")
    (d / n).write_bytes(_tar_gz({"docs/private folder/notes.md": b"x\n"}))
    sums = "".join(f"{hashlib.sha256((d / a).read_bytes()).hexdigest()}  {a}\n" for a in sorted(p.name for p in d.glob("openbfme-*")))
    (d / "SHA256SUMS-v0.3.0-preview.46.txt").write_text(sums)
    deny = tmp_path / "deny.json"
    deny.write_text(json.dumps({"rules": TEST_DENYLIST["rules"] + [SPACED[0][0]]}))
    r = run("bash", tool, d, "--repo", TARGET, env=world.env(OPENBFME_RELEASE_DENYLIST=str(deny)))
    assert r.returncode != 0 and "manifest.json is not the manifest of these archives" in r.stderr, r.stdout + r.stderr
    assert "member '<" in r.stdout + r.stderr, r.stdout + r.stderr   # the helper's line arrived, masked
    assert "private folder" not in r.stdout + r.stderr, r.stdout + r.stderr


def _tar_gz(members: dict[str, bytes]) -> bytes:
    import tarfile  # noqa: PLC0415
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def test_sol_r3_6_rejected_arguments_are_not_printed_before_the_redactor_runs(world):
    """Sol r3 P3 (6): an unknown option was printed before the denylist was loaded and the redactor started (publish_release.sh,
    autorelease.sh alike); the sibling: autorelease.sh's legacy-launcher check printed its answer (git's error quoting the remote) before
    the redactor too"""
    tool = world.repo / "tools/release/publish_release.sh"
    for cmd in (["bash", tool, "--from-buildbox-7", "x"],
                ["bash", world.repo / "tools/release/autorelease.sh", git(world.repo, "rev-parse", "HEAD"), "--lab-vm-q=1"]):
        r = run(*cmd, env=world.env())
        assert r.returncode == 2 and "an unknown option" in r.stderr and not _leaks(r.stdout + r.stderr), r.stdout + r.stderr
    r = world.release(git(world.repo, "rev-parse", "HEAD"), "--plan", "--remote", "lab-vm-q", OPENBFME_RELEASE_LEGACY_LAUNCHERS="0")
    assert r.returncode == 2 and "OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 is refused" in r.stderr, r.stdout + r.stderr
    assert not _leaks(r.stdout + r.stderr), r.stdout + r.stderr


def test_export_subst_is_refused(world):
    sha = world.change({".gitattributes": "VERSION export-subst\n"}, "X-1: attributes")
    probs = autorelease.audit_tree(str(world.repo), autorelease.sync_tree(str(world.repo), sha), PRIVATE)
    assert any(p.startswith("an export-subst attribute") for p in probs), probs


def test_a_listed_gzip_is_read_by_its_content():
    import gzip  # noqa: PLC0415
    data = gzip.compress(b"plain text\n", mtime=0)
    assert autorelease.container_metadata_problems("docs/a.gz", data) == []
    assert autorelease.container_metadata_problems("docs/a.gz", data[:-4]) != []


def test_the_note_sources_reach_the_metadata_audit():
    """the review's weak spot: nothing checked that autorelease.sh audits note-sources.txt"""
    text = (HERE / "autorelease.sh").read_text().replace("\\\n", " ")
    audit = next(line for line in text.splitlines() if "autorelease.py\" audit-text" in line and "commit-msg.txt" in line)
    assert '"$WORK/note-sources.txt"' in audit and "--denylist" in audit, audit


def test_the_asset_audit_refuses_nothing_and_missing_files(tmp_path, world):
    """a checker that passes on nothing: --assets without a file is a usage error, a missing file a finding"""
    r = run(sys.executable, HERE / "autorelease.py", "privacy-gate", "--no-tree", "--assets", "--denylist", world.denylist)
    assert r.returncode == 2, r.stdout + r.stderr
    r = run(sys.executable, HERE / "autorelease.py", "privacy-gate", "--no-tree", "--assets", tmp_path / "gone.zip", "--denylist", world.denylist)
    assert r.returncode == 1 and "a release asset cannot be read" in r.stdout, r.stdout + r.stderr


# ---- RELCAND-1: candidate first, tag last ---------------------------------------------------------------------------------------------
def released(world, version: str) -> dict:
    (rel,) = [x for x in world.gh_data()["releases"] if x["tag_name"] == version]
    return rel


def published_asset(world, rel: dict, name: str) -> bytes:
    (a,) = [x for x in rel["assets"] if x["name"] == name]
    return (Path(str(world.gh_state) + ".d") / str(a["id"])).read_bytes()


def candidate_files(world, version: str) -> dict[str, str]:
    lines = (world.tmp / "work" / version / "candidate.ok").read_text().splitlines()
    return {ln[66:]: ln[:64] for ln in lines if re.fullmatch(r"[0-9a-f]{64}  .+", ln)}


def test_the_candidate_is_built_and_tested_before_anything_is_pushed_and_published_as_tested(world):
    """the build sees the final tag and commit (git describe is the version) while the remote has neither; the pushed tag is the very
    tag object the build was given, main is the very commit; every published asset is byte for byte the candidate's recorded file"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: units walk around trees instead of through them")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    (b,) = builds(world)
    public = git(world.remote, "rev-parse", "refs/heads/main")
    assert b["describe"] == "v0.4.5" and b["head"] == public, b
    assert b["remote_tag"] == "" and b["remote_has_head"] is False, "tag last: nothing was public while the candidate was built"
    assert git(world.repo, "ls-remote", str(world.remote), "refs/tags/v0.4.5").split()[0] == b["tagobj"], "the tested tag object is pushed"
    out = r.stdout
    assert out.index("AUTORELEASE candidate: v0.4.5 passed every check") < out.index("AUTORELEASE push:") < out.index("PUBLISH draft release")
    pkg = world.tmp / "work/v0.4.5/pkg"
    assert json.loads((pkg / "manifest.json").read_text())["commit"] == public
    rel = released(world, "v0.4.5")
    files = candidate_files(world, "v0.4.5")
    assert len(rel["assets"]) == 10 and all(f"pkg/{a['name']}" in files for a in rel["assets"]), "the candidate is what is published"
    for a in rel["assets"]:
        assert hashlib.sha256(published_asset(world, rel, a["name"])).hexdigest() == files[f"pkg/{a['name']}"], a["name"]
    assert rel["body"] == (world.tmp / "work/v0.4.5/notes.md").read_text() and "notes.md" in files
    assert not git(world.repo, "tag", "-l", "v0.4.5") or git(world.repo, "rev-parse", "v0.4.5") == b["tagobj"]


@pytest.mark.parametrize("env,step", [
    ({"STANDIN_BUILD_FAIL": "1"}, "build"),            # the Linux / Windows build, package.sh, test_export, the Windows start + HUD6
    ({"STANDIN_MAC_FAIL": "1"}, "actions-dispatch"),   # RELGH-2: the macOS build on GitHub failed (the stand-in builds at the dispatch)
    ({"STANDIN_TAMPERED": "1"}, "deck-verify"),        # step 5's trusted-checkout checks (package.sh --verify in a real run)
    ({"STANDIN_MAC_FAULT": "wrong-commit"}, "deck-verify"),
])
def test_a_failed_candidate_burns_no_number_and_the_next_run_releases_it(world, env, step):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, **env)
    assert r.returncode == 1 and f"AUTORELEASE FAIL at step {step} (v0.4.5)" in r.stdout, r.stdout + r.stderr
    assert nothing_published(world), "no tag, no public commit, no release, no intent"
    assert not git(world.repo, "tag", "-l", "v0.4.5"), "the candidate's local tag is gone"
    assert "v0.4.5 is not used and the next run releases it" in r.stdout
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert [x["tag_name"] for x in world.gh_data()["releases"]] == ["v0.4.5"]
    assert sorted(t.split("refs/tags/")[1] for t in world.remote_refs("refs/tags/*").splitlines() if not t.endswith("^{}")) == ["v0.4.5"]
    assert builds(world)[-1]["head"] == git(world.remote, "rev-parse", "refs/heads/main") and builds(world)[-1]["describe"] == "v0.4.5"


def test_main_moving_while_the_candidate_is_built_makes_it_stale(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, STANDIN_MOVE_MAIN="1")
    assert r.returncode == 1 and "AUTORELEASE FAIL at step push (v0.4.5): STALE CANDIDATE: origin/main moved from" in r.stdout, r.stdout
    moved = git(world.remote, "rev-parse", "refs/heads/main")
    assert moved != world.seed_main and git(world.remote, "log", "-1", "--format=%s", moved) == "someone else's push"
    assert world.remote_refs("refs/tags/*") == "" and world.gh_data()["releases"] == [] and not (world.state / "intent").exists()
    assert not git(world.repo, "tag", "-l", "v0.4.5")
    # the next run: a new candidate on the moved main, the same number
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    public = git(world.remote, "rev-parse", "refs/heads/main")
    assert git(world.remote, "rev-parse", f"{public}^") == moved and builds(world)[-1]["head"] == public
    assert git(world.remote, "rev-parse", "v0.4.5^{commit}") == public


def git_racer(tmp: Path, remote: Path) -> Path:
    """a git whose atomic push is preceded by someone else's push to the public main (the race between the last check and the push)"""
    real = shutil.which("git")
    w = tmp / "rbin" / "git"
    w.parent.mkdir(exist_ok=True)
    w.write_text(f"""#!{sys.executable}
import os, subprocess, sys
if "push" in sys.argv and "--atomic" in sys.argv:
    g = lambda *a: subprocess.run([{real!r}, "-C", {str(remote)!r}, *a], capture_output=True, text=True, check=True).stdout.strip()
    main = g("rev-parse", "refs/heads/main")
    g("update-ref", "refs/heads/main", g("commit-tree", main + "^{{tree}}", "-p", main, "-m", "a push just before ours"))
os.execv({real!r}, [{real!r}] + sys.argv[1:])
""")
    w.chmod(0o755)
    return w.parent


def test_a_push_that_loses_the_race_pushes_nothing(world, tmp_path):
    """the push is atomic: when main moved between the last check and the push, git refuses main and the tag goes with it"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    wbin = git_racer(tmp_path, world.remote)
    r = world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode == 1 and "pushing v0.4.5 failed: origin/main moved to" in r.stdout and "nothing is public" in r.stdout, r.stdout + r.stderr
    assert world.remote_refs("refs/tags/*") == "" and world.gh_data()["releases"] == [] and not (world.state / "intent").exists()
    assert git(world.remote, "log", "-1", "--format=%s", "refs/heads/main") == "a push just before ours"
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr


def test_a_run_killed_during_its_candidate_frees_the_number(world):
    """a power cut during the build: the intent (step candidate) and the local tag survive the run; the next run sees the tag is not on
    the remote, deletes the candidate's local tag, discards the intent and releases the same number, for another sha too"""
    a = world.change({"engine/a.cpp": "2\n"}, "X-1: first change")
    world.gate(a)
    r = world.release(a, "--since", world.first, STANDIN_KILL="1")
    assert r.returncode != 0
    intent = (world.state / "intent").read_text()
    assert "step=candidate" in intent and "version=v0.4.5" in intent and git(world.repo, "tag", "-l", "v0.4.5") == "v0.4.5"
    assert world.remote_refs("refs/tags/*") == "" and git(world.remote, "rev-parse", "refs/heads/main") == world.seed_main
    b = world.change({"engine/a.cpp": "3\n"}, "X-2: second change")
    world.gate(b)
    for extra in (["--plan"], ["--dry-run"]):
        r = world.release(b, "--since", world.first, *extra)
        assert r.returncode != 0 and "only a real run reconciles it" in r.stdout
    r = world.release(b, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "never reached origin: discarded; nothing of it was public and v0.4.5 is not used" in r.stdout
    assert "deleted the candidate's local tag v0.4.5" in r.stdout and "AUTORELEASE OK v0.4.5" in r.stdout
    assert (world.state / "last-released").read_text().split()[:2] == [b, "v0.4.5"]


def test_a_candidate_intent_does_not_delete_a_tag_it_did_not_make(world):
    a = world.change({"engine/a.cpp": "2\n"}, "X-1: first change")
    world.gate(a)
    assert world.release(a, "--since", world.first, STANDIN_KILL="1").returncode != 0
    intent = world.state / "intent"
    text = intent.read_text()
    git(world.repo, "tag", "-d", "v0.4.5")
    git(world.repo, "tag", "v0.4.5", world.first)   # someone's own local tag of that name
    r = world.release(a, "--since", world.first)
    assert r.returncode != 0 and "a local tag v0.4.5 exists that is not the open candidate's" in r.stdout, r.stdout
    assert git(world.repo, "rev-parse", "v0.4.5") == world.first and intent.read_text() == text


def git_killer(tmp: Path, lost: bool) -> Path:
    """a git whose atomic push goes through, then the run dies (a power cut) or the answer is lost (exit 1)"""
    real = shutil.which("git")
    w = tmp / "kbin" / "git"
    w.parent.mkdir(exist_ok=True)
    after = "sys.exit(1)" if lost else "os.kill(os.getppid(), 9); sys.exit(1)"
    w.write_text(f"""#!{sys.executable}
import os, subprocess, sys
if "push" in sys.argv and "--atomic" in sys.argv:
    subprocess.run([{real!r}] + sys.argv[1:], check=True)
    {after}
os.execv({real!r}, [{real!r}] + sys.argv[1:])
""")
    w.chmod(0o755)
    return w.parent


def test_a_lost_push_answer_is_read_back(world, tmp_path):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    wbin = git_killer(tmp_path, lost=True)
    r = world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode == 0 and "the push reported a failure, but origin has the candidate's tag: continuing" in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE OK v0.4.5" in r.stdout and len(builds(world)) == 1


def test_a_run_killed_after_its_push_resumes_and_publishes_the_candidate_unrebuilt(world, tmp_path):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    wbin = git_killer(tmp_path, lost=False)
    r = world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode != 0 and "step=pushing" in (world.state / "intent").read_text()
    assert world.remote_refs("refs/tags/v0.4.5") and world.gh_data()["releases"] == []
    r = world.release(sha)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "the candidate's push of v0.4.5 reached origin (its answer was lost): it goes on as tagged" in r.stdout
    assert "it is published as it is, not rebuilt" in r.stdout and len(builds(world)) == 1, "published without a rebuild"
    rel = released(world, "v0.4.5")
    files = candidate_files(world, "v0.4.5")
    for a in rel["assets"]:
        assert hashlib.sha256(published_asset(world, rel, a["name"])).hexdigest() == files[f"pkg/{a['name']}"], a["name"]


def test_a_failure_after_the_push_resumes_the_candidate_and_a_changed_one_is_rebuilt(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, FAKE_GH_FAULT="tag-read-fail")
    assert r.returncode != 0 and world.remote_refs("refs/tags/v0.4.5") and "step=publishing" in (world.state / "intent").read_text()
    assert "the tag v0.4.5 stays on origin with no release yet" in r.stdout
    # the candidate's folder changed after it passed (a file edited): not published; built and tested again at the pushed tag
    pkg = world.tmp / "work/v0.4.5/pkg"
    sums = pkg / "SHA256SUMS-v0.4.5.txt"
    sums.write_text(sums.read_text() + "\n")
    r = world.release(sha)
    assert r.returncode == 0 and "candidate.ok does not verify" in r.stdout and "a file differs from its record" in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE OK v0.4.5" in r.stdout and len(builds(world)) == 2
    assert builds(world)[1]["remote_tag"] and builds(world)[1]["tagobj"] == builds(world)[0]["tagobj"], "the rebuild is of the pushed tag"
    rel = released(world, "v0.4.5")
    files = candidate_files(world, "v0.4.5")
    for a in rel["assets"]:
        assert hashlib.sha256(published_asset(world, rel, a["name"])).hexdigest() == files[f"pkg/{a['name']}"], a["name"]


@pytest.mark.parametrize("edit", ["extra", "missing", "public"])
def test_a_candidate_record_that_does_not_hold_is_not_published(world, edit):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, FAKE_GH_FAULT="tag-read-fail").returncode != 0
    work = world.tmp / "work/v0.4.5"
    if edit == "extra":
        (work / "pkg/extra.bin").write_bytes(b"x")
    elif edit == "missing":
        (work / "pkg/BUILDINFO-v0.4.5.txt").unlink()
    else:
        ok = work / "candidate.ok"
        ok.write_text(re.sub(r"^public=.*$", "public=" + "1" * 40, ok.read_text(), flags=re.M))
    r = world.release(sha)
    why = {"extra": "does not hold exactly the recorded files", "missing": "does not hold exactly the recorded files",
           "public": "candidate.ok names another public"}[edit]
    assert r.returncode == 0 and why in r.stdout and len(builds(world)) == 2, r.stdout + r.stderr
    assert "extra.bin" not in json.dumps(released(world, "v0.4.5")["assets"])


def test_an_intent_of_the_earlier_order_at_step_tag_is_released_with_its_public_commit(world):
    """the order before RELCAND-1 merged the sync PR, then failed to push the tag (intent step=tag, public on main, no tag): the new
    order builds and tests that public commit as the candidate and pushes only the tag"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    tree = autorelease.sync_tree(str(world.repo), sha)
    git(world.repo, "fetch", "-q", "origin", "main")
    public = git(world.repo, "commit-tree", tree, "-p", world.seed_main, "-m", "Sync the rebuild (the old order's merge)")
    git(world.repo, "push", "-q", "origin", f"{public}:refs/heads/main")
    world.state.mkdir(exist_ok=True)
    work = world.tmp / "work/v0.4.5"
    (world.state / "intent").write_text(f"sha={sha}\nversion=v0.4.5\ntree={tree}\nbase={world.first}\nwork={work}\nbranch=sync/x\n"
                                        f"public={public}\nstep=tag\n")
    git(world.repo, "fetch", "-q", "origin", "main")
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "resumed, the public commit" in r.stdout and git(world.remote, "rev-parse", "refs/heads/main") == public
    assert git(world.remote, "rev-parse", "v0.4.5^{commit}") == public and builds(world)[-1]["head"] == public


def test_the_dry_run_prints_the_one_atomic_push_and_leaves_no_tag(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, "--dry-run")
    assert r.returncode == 0 and "AUTORELEASE DRY RUN OK v0.4.5" in r.stdout, r.stdout + r.stderr
    (b,) = builds(world)
    assert f"WOULD RUN: git push --atomic origin {b['head']}:refs/heads/main refs/tags/v0.4.5" in r.stdout
    assert nothing_published(world) and not git(world.repo, "tag", "-l", "v0.4.5") and b["describe"] == "v0.4.5"


@pytest.mark.parametrize("field,value,why", [
    ("tagobj", "2" * 40, "is not the candidate's tag object"),
    ("tagobj", "", "is not the candidate's tag object (none recorded)"),
])
def test_a_pushing_intent_whose_remote_tag_is_not_its_own_is_refused(world, tmp_path, field, value, why):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    wbin = git_killer(tmp_path, lost=False)
    assert world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}").returncode != 0
    intent = world.state / "intent"
    intent.write_text(re.sub(rf"^{field}=.*$", f"{field}={value}", intent.read_text(), flags=re.M))
    before = intent.read_text()
    r = world.release(sha)
    assert r.returncode != 0 and why in r.stdout and intent.read_text() == before and world.gh_data()["releases"] == [], r.stdout



# ---- Sol r1 (RELCAND-1): a push the server is still applying when the client dies -------------------------------------------------------
def paused_remote(world, tmp: Path) -> dict:
    """a pre-receive hook on the public remote that, while <pause> exists, says it received the push and waits for <go>, then accepts or
    refuses it (<rc>): a server still applying a push whose client is gone"""
    f = {k: tmp / k for k in ("pause", "received", "go", "rc", "done")}
    hook = world.remote / "hooks/pre-receive"
    hook.write_text(f"""#!{sys.executable}
import pathlib, sys, time
if " refs/tags/" not in sys.stdin.read():   # RELGH-2: only the release push (it carries the tag) is held, not the macOS staging push
    sys.exit(0)
if not pathlib.Path({str(f['pause'])!r}).exists():
    sys.exit(0)
pathlib.Path({str(f['received'])!r}).touch()
deadline = time.monotonic() + 60
while not pathlib.Path({str(f['go'])!r}).exists():
    if time.monotonic() >= deadline:
        sys.exit("test server timed out waiting for go")
    time.sleep(0.01)
sys.exit(int(pathlib.Path({str(f['rc'])!r}).read_text()))
""")
    hook.chmod(0o755)
    receiver = tmp / "receive-pack"
    receiver.write_text(f"""#!{sys.executable}
import pathlib, subprocess, sys
result = subprocess.run([{shutil.which('git')!r}, "receive-pack", *sys.argv[1:]])
if pathlib.Path({str(f['received'])!r}).exists():   # RELGH-2: the held release push, not the macOS staging push before it
    pathlib.Path({str(f['done'])!r}).touch()
sys.exit(result.returncode)
""")
    receiver.chmod(0o755)
    git(world.repo, "config", "remote.origin.receivepack", shlex.quote(str(receiver)))
    f["pause"].touch()
    return f


def git_dropper(tmp: Path, received: Path) -> Path:
    """a git whose atomic push loses its connection once the server has the push: the client is killed, the server goes on"""
    real = shutil.which("git")
    w = tmp / "dbin" / "git"
    w.parent.mkdir(exist_ok=True)
    w.write_text(f"""#!{sys.executable}
import os, subprocess, sys, time
if "push" in sys.argv and "--atomic" in sys.argv:
    # the local "server" (receive-pack, a child of this push) must not hold the run's output pipes, as a remote one would not
    p = subprocess.Popen([{real!r}] + sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end = time.monotonic() + 60
    while not os.path.exists({str(received)!r}) and time.monotonic() < end:
        time.sleep(0.05)
    p.kill(); p.wait()
    print("fatal: the remote end hung up unexpectedly", file=sys.stderr)
    sys.exit(128)
os.execv({real!r}, [{real!r}] + sys.argv[1:])
""")
    w.chmod(0o755)
    return w.parent


def wait_for(path: Path, what: str) -> None:
    import time  # noqa: PLC0415
    end = time.monotonic() + 60
    while not path.exists() and time.monotonic() < end:
        time.sleep(0.05)
    assert path.exists(), what


@pytest.mark.parametrize("server", ["lands", "refuses"])
def test_a_push_whose_client_dies_while_the_server_applies_it_is_never_discarded(world, tmp_path, server):
    """Sol's repro: the client dies after the server has the push; the run reads no tag yet. The intent must stay (step pushing) with
    the local tag: when the server then lands the push, the next run goes on from the tag; when the server refuses it, the next run pushes
    the very same candidate again. Either way the same version is published from the candidate's folder, never rebuilt, and no second
    number is taken"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    f = paused_remote(world, tmp_path)
    f["rc"].write_text("0\n" if server == "lands" else "1\n")
    wbin = git_dropper(tmp_path, f["received"])
    r = world.release(sha, "--since", world.first, PATH=f"{wbin}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode == 1 and "but the server may still be applying the push" in r.stdout, r.stdout + r.stderr
    intent = (world.state / "intent").read_text()
    assert "step=pushing" in intent and git(world.repo, "tag", "-l", "v0.4.5") == "v0.4.5", "the intent and the local tag stay"
    assert "the next real run reads origin and goes on from the tag v0.4.5, or pushes this same candidate again" in r.stdout
    # the server finishes what it received
    f["go"].touch()
    wait_for(f["done"], "the server's hook finished")
    f["pause"].unlink()
    landed = world.remote_refs("refs/tags/v0.4.5")
    assert bool(landed) == (server == "lands"), landed
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    if server == "lands":
        assert "the candidate's push of v0.4.5 reached origin (its answer was lost)" in r.stdout
    else:
        assert "pushing the very same candidate again" in r.stdout
    assert "it is published as it is, not rebuilt" in r.stdout and len(builds(world)) == 1
    assert git(world.repo, "ls-remote", str(world.remote), "refs/tags/v0.4.5").split()[0] == builds(world)[0]["tagobj"]
    assert [x["tag_name"] for x in world.gh_data()["releases"]] == ["v0.4.5"] and not world.remote_refs("refs/tags/v0.4.6")


def git_unreachable(tmp: Path) -> Path:
    """a git whose atomic push never reaches the remote (the network is down): nothing is sent"""
    real = shutil.which("git")
    w = tmp / "ubin" / "git"
    w.parent.mkdir(exist_ok=True)
    w.write_text(f"""#!{sys.executable}
import os, sys
if "push" in sys.argv and "--atomic" in sys.argv:
    print("fatal: unable to access the remote: Could not resolve host", file=sys.stderr); sys.exit(128)
os.execv({real!r}, [{real!r}] + sys.argv[1:])
""")
    w.chmod(0o755)
    return w.parent


def test_a_push_that_never_left_is_pushed_again_by_the_next_run(world, tmp_path):
    """the push fails and main has not moved: the outcome is not provable from here, so the intent stays (step pushing); the next run
    pushes the same candidate (its record verified) and publishes it unrebuilt; another sha asked for meanwhile waits (exit 3)"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, PATH=f"{git_unreachable(tmp_path)}:{world.gh.parent}:{os.environ['PATH']}")
    assert r.returncode == 1 and "step=pushing" in (world.state / "intent").read_text(), r.stdout + r.stderr
    assert world.remote_refs("refs/tags/*") == "" and git(world.remote, "rev-parse", "refs/heads/main") == world.seed_main
    other = world.change({"engine/a.cpp": "3\n"}, "X-2: a later change")
    world.gate(other)
    r = world.release(other)
    assert r.returncode == 3 and "pushing the very same candidate again" in r.stdout and "RESUMED ONLY" in r.stdout, r.stdout + r.stderr
    assert (world.state / "last-released").read_text().split()[:2] == [sha, "v0.4.5"] and len(builds(world)) == 1
    r = world.release(other)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr


@pytest.mark.parametrize("edit,why", [
    ("candidate.ok", "its candidate record does not verify"),
    ("local tag", "its local tag v0.4.5 is not its tag object"),
    ("public", "does not hold"),
])
def test_a_pushing_intent_is_pushed_again_only_when_it_verifies(world, tmp_path, edit, why):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    assert world.release(sha, "--since", world.first, PATH=f"{git_unreachable(tmp_path)}:{world.gh.parent}:{os.environ['PATH']}").returncode == 1
    intent = world.state / "intent"
    if edit == "candidate.ok":
        notes = world.tmp / "work/v0.4.5/notes.md"
        notes.write_text(notes.read_text() + "edited\n")
    elif edit == "local tag":
        git(world.repo, "tag", "-f", "-a", "v0.4.5", "-m", "another tag object", git(world.repo, "rev-parse", "v0.4.5^{commit}"))
    else:
        intent.write_text(re.sub(r"^public=.*$", f"public={world.first}", intent.read_text(), flags=re.M))
    before = intent.read_text()
    r = world.release(sha)
    assert r.returncode != 0 and why in r.stdout, r.stdout + r.stderr
    assert world.remote_refs("refs/tags/*") == "" and git(world.remote, "rev-parse", "refs/heads/main") == world.seed_main
    assert intent.read_text() == before, "nothing was pushed and the intent is unchanged"


def test_the_push_guard_runs_before_the_push(world, tmp_path):
    """the pre-push hook's check runs first for a remote whose URL is the public repository (a refusal is then a failure before the push)"""
    alias = tmp_path / "gh-host/open-bfme/openbfme-godot.git"
    alias.parent.mkdir(parents=True)
    alias.symlink_to(world.remote)
    git(world.repo, "remote", "set-url", "origin", str(alias))
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert r.stdout.index("PUBLIC PUSH GUARD ok: 1 new commit(s), 1 tree(s) audited for origin") < r.stdout.index("AUTORELEASE push:")


def test_onlinemenu1_the_mailer_arns_hold_no_account():
    """lane ONLINEMENU-1: the aws-account rule allows the mailer's IAM policy and deploy steps because every ARN there names the account with the
    <ACCOUNT_ID> placeholder; no 12-digit account, access key or bucket name may appear in them"""
    for name in ("services/online/mailer/iam-policy.json", "services/online/mailer/README.md"):
        text = (REPO / name).read_text(encoding="utf-8")
        arns = re.findall(r"arn:aws[a-z-]*:[^\s\"]*", text)
        assert arns, name
        for arn in arns:
            parts = arn.split(":")
            assert len(parts) >= 6 and parts[4] == "<ACCOUNT_ID>" and parts[2] in ("iam", "logs", "ses"), arn
        assert not re.search(r"(?<!\d)\d{12}(?!\d)|\b(?:AKIA|ASIA)[A-Z0-9]{16}\b|aws_secret_access_key|aws_session_token", text), name


def test_onlinemenu1_only_the_project_domain_itself_is_public():
    """Sol (onlinemenu1-guards): @openbfme.com is public, a longer domain that starts with it is not"""
    rx = next(r[1] for r in autorelease.BUILTIN if r[0] == "e-mail")
    assert not rx.search("write to admin@openbfme.com with your code") and not rx.search("from `no-reply@openbfme.com`;")
    assert rx.search("person@openbfme.com.evil.net") and rx.search("person@openbfme.community")
