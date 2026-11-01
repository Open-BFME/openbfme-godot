#!/usr/bin/env python3
"""Lane SIGNKMS-1, RELGH-2, RELGH-3: the release workflow's steps (.github/workflows/release.yml) and the Deck's side of a release signed on
GitHub Actions: a NIGHTLY with the nightly key of the environment nightly-signing (docs/RELEASE.md, "Nightly signing on GitHub Actions"),
or (RELGH-3) a STABLE release with the stable Actions key of the environment stable-signing, which the owner's review must approve first
(docs/RELEASE.md, "Stable signing on GitHub Actions"; tools/release/stable_environment.json). The Deck key (deck-2026, the first entry of
launcher/release_keys.pub) never enters CI or GitHub: it is refused here.

On GitHub's runners (the inputs come from the environment the workflow sets, IN_TAG / IN_COMMIT / IN_REQUEST, never from the
command line, so no input is ever pasted into a script):
    actions_release.py plan                      the request: workflow_dispatch on refs/heads/main of this repository; a
                                                 v<x.y.z>-nightly.<YYMMDD><1|2>00 (or <YYYYMMDD>) tag or (RELGH-3) a stable v<x.y.z>; the
                                                 tag names the commit; the commit is on main (an ancestor of the run's main head); the newest
                                                 published stable release (stable-key signed, 14+ days old) trusts the commit's nightly keys,
                                                 or for a stable release the commit's stable Actions key (trusted_by_stable); for a stable
                                                 release also the environment stable-signing exactly as configured and the run's triggering
                                                 actor no reviewer of it (stable_environment.py check); writes kind=nightly|stable to
                                                 $GITHUB_OUTPUT
    actions_release.py checkout-is-tag           the checkout is the commit and `git describe` is the tag (the builds embed it)
    actions_release.py sign --in DIR --pkg DIR   the build jobs' artifacts into one release folder (exactly the files a release has), the
                                                 strict package checks, then sealing and signing with the key of the run's kind: the
                                                 environment's secret NIGHTLY_SIGNING_KEY or (RELGH-3) STABLE_SIGNING_KEY (removed from this
                                                 process's environment at once, so no child sees it) written to a mode 0600 file on tmpfs
                                                 (/dev/shm) and shredded when the step ends, whatever happened; its public half must be in the
                                                 commit's launcher/nightly_keys.pub (a nightly) or be the commit's stable Actions key (the
                                                 label of stable_environment.json in launcher/release_keys.pub, never its first entry) AND be
                                                 trusted by the newest stable release (the transition rule); a stable run also checks the
                                                 environment and the owner's approval of this run again before the key is read; every
                                                 signature verified under the committed lists by the scope rule (release_keys.verify_scoped)
    actions_release.py draft --pkg DIR           the DRAFT release of the tag with every file of the folder, read back (names, sizes and
                                                 GitHub's SHA-256 of each upload); it never publishes
On the Deck (autorelease.sh with OPENBFME_RELEASE_SIGNER=actions, --nightly or, RELGH-3, a stable release):
    actions_release.py trust-check --commit C [--kind nightly|stable] [--git-dir D] [--remote R]
                                                 before anything is pushed: C's launcher/nightly_keys.pub lists a key and every key of it is
                                                 trusted by the newest PUBLISHED stable release, authenticated by the stable key and
                                                 published TRUST_DAYS (14) days ago or more (trusted_by_stable) (else the launchers players have would
                                                 refuse the nightly: ship the transition launcher first); --kind stable: the same for C's
                                                 stable Actions key, and the environment stable-signing is exactly its configuration with
                                                 this machine's gh account no reviewer (stable_environment.py check --dispatcher-is-gh-user)
    actions_release.py dispatch --tag T --commit C [--repo R] --out FILE
                                                 starts the workflow on main with a fresh request id; FILE records {request, run_id}
    actions_release.py wait --request-file FILE [--timeout S] [--repo R]
                                                 waits for that run, requires success, prints the draft release's id (the draft whose body
                                                 names the request); a stable run waits for the owner's review (status waiting) meanwhile
    actions_release.py compare --candidate DIR --actions DIR --version V
                                                 the draft's packages (downloaded by check_draft.py) against the candidate this Deck
                                                 built and tested: each archive without its seal (seal_package.py unseal) byte for byte
                                                 (RELSIGN-1: the game's Windows ZIP may differ only in its DLL's CodeView GUID,
                                                 member by member: same_build_archive);
                                                 OPENBFME_RELEASE_ACTIONS_COMPARE=require (default: a difference fails the release) or
                                                 report (differences are printed, the release goes on: only while MACACT-1's host-vs-Actions
                                                 comparison explains every difference)
Every check fails closed: a read that fails, an answer that is not exactly the expected one, or a file too many or too few is an error.
GH (default gh) is the GitHub CLI; the tests use tools/release/gh_stand_in.py.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

TARGET = "Open-BFME/openbfme-godot"
WORKFLOW = "release.yml"
# a nightly's number: NIGHTLY-2's <YYMMDD><1 morning | 2 evening>00 (two a day), or NIGHTLY-1's <YYYYMMDD> (autorelease.py nightly_parts)
KINDS = {"nightly": re.compile(r"^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})-nightly\."
                               r"(?:(?:2[6-9]|[3-9][0-9])(0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01])[12]00|20[0-9]{2}(0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01]))$"),
         "stable": re.compile(r"^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$")}
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX32 = re.compile(r"^[0-9a-f]{32}$")
REQUEST_LINE = "openbfme-release-request: "


class Fail(Exception):
    pass


def gh() -> str:
    return os.environ.get("GH", "gh")


def run(cmd: list[str], **kw) -> str:
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        raise Fail(f"{' '.join(cmd[:3])} ... failed: {(r.stderr or r.stdout).strip()[-400:]}")
    return r.stdout


def git(*args: str, cwd: Path | None = None) -> str:
    return run(["git", "-C", str(cwd or Path.cwd()), *args]).strip()


def kind_of(tag: str) -> str | None:
    """RELGH-3: nightly or stable (the two kinds signed on GitHub Actions); None for anything else (an alpha build, a typo)"""
    return next((k for k, rx in KINDS.items() if rx.match(tag)), None)


def request_env() -> dict:
    """the dispatched inputs, from the environment, each checked before anything uses it; the kind follows from the tag (RELGH-3)"""
    e = {k: os.environ.get(f"IN_{k.upper()}", "") for k in ("tag", "commit", "request")}
    kind = kind_of(e["tag"])
    if kind is None:
        raise Fail(f"the tag {e['tag']!r} is not a nightly release tag (v<x.y.z>-nightly.<YYMMDD><1|2>00 or -nightly.<YYYYMMDD>) "
                   "and not a stable one (v<x.y.z>): nothing else is signed on GitHub Actions")
    e["kind"] = kind
    if not HEX40.match(e["commit"]):
        raise Fail("the commit is not 40 lower-case hex digits")
    if not HEX32.match(e["request"]):
        raise Fail("the request id is not 32 lower-case hex digits")
    return e


def cmd_plan(_a) -> None:
    e = request_env()
    if os.environ.get("GITHUB_EVENT_NAME") != "workflow_dispatch":
        raise Fail(f"the event is {os.environ.get('GITHUB_EVENT_NAME')!r}, not workflow_dispatch")
    if os.environ.get("GITHUB_REF") != "refs/heads/main":
        raise Fail(f"the workflow runs on {os.environ.get('GITHUB_REF')!r}, not refs/heads/main: only main's release workflow signs")
    if os.environ.get("GITHUB_REPOSITORY") != TARGET:
        raise Fail(f"the repository is {os.environ.get('GITHUB_REPOSITORY')!r}, not {TARGET}")
    # RELGH-3: a GitHub environment cannot be limited to one workflow file; this check (and public_workflows.py: no other workflow names
    # an environment) does it: only release.yml of main signs
    wref = f"{TARGET}/.github/workflows/{WORKFLOW}@refs/heads/main"
    if os.environ.get("GITHUB_WORKFLOW_REF") != wref:
        raise Fail(f"the workflow is {os.environ.get('GITHUB_WORKFLOW_REF')!r}, not {wref}")
    head = os.environ.get("GITHUB_SHA", "")
    if not HEX40.match(head):
        raise Fail("GITHUB_SHA is not a commit id")
    tagged = git("rev-parse", "-q", "--verify", f"refs/tags/{e['tag']}^{{commit}}") if _has_ref(f"refs/tags/{e['tag']}") else ""
    if tagged != e["commit"]:
        raise Fail(f"the tag {e['tag']} names {tagged or 'nothing here'}, not {e['commit']}")
    if subprocess.run(["git", "merge-base", "--is-ancestor", e["commit"], head]).returncode:
        raise Fail(f"{e['commit']} is not on main (not an ancestor of the run's main head {head})")
    if e["kind"] == "nightly":
        stable = trusted_by_stable(e["commit"])
        what = "the nightly keys are"
    else:
        import stable_environment
        cfg = commit_config(e["commit"])
        label, key = actions_stable_key(e["commit"], cfg)
        stable = trusted_by_stable(e["commit"], [key], kind="stable")
        trig = os.environ.get("GITHUB_TRIGGERING_ACTOR", "")
        try:
            stable_environment.check(TARGET, cfg, trig)
        except stable_environment.Fail as ex:
            raise Fail(f"the environment {cfg['environment']}: {ex}") from None
        what = f"the stable Actions key {label} is"
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a", encoding="ascii") as f:
            f.write(f"kind={e['kind']}\n")
    print(f"PLAN ok: {e['tag']} at {e['commit']} ({e['kind']}, request {e['request']}, main {head}; {what} trusted since {stable})")


def commit_config(commit: str, cwd: Path | None = None) -> dict:
    """RELGH-3: the commit's tools/release/stable_environment.json, checked (stable_environment.load_config)"""
    import stable_environment
    try:
        return stable_environment.load_config(git("show", f"{commit}:tools/release/stable_environment.json", cwd=cwd))
    except Fail:
        raise Fail(f"{commit[:12]} has no tools/release/stable_environment.json") from None
    except stable_environment.Fail as ex:
        raise Fail(f"{commit[:12]}'s tools/release/stable_environment.json: {ex}") from None


def actions_stable_key(commit: str, cfg: dict, cwd: Path | None = None) -> tuple[str, str]:
    """RELGH-3: (label, hex) of the key the environment stable-signing holds: the entry of the commit's launcher/release_keys.pub
    labelled cfg["key_label"], never the list's first entry (the Deck key, the offline recovery key, never enters GitHub)"""
    import release_keys
    try:
        keys = release_keys.parse(git("show", f"{commit}:launcher/release_keys.pub", cwd=cwd), "release_keys.pub")
    except (Fail, release_keys.KeyListError) as ex:
        raise Fail(f"{commit[:12]} has no valid launcher/release_keys.pub ({ex})") from None
    hit = [i for i, (lb, _) in enumerate(keys) if lb == cfg["key_label"]]
    if not hit:
        raise Fail(f"{commit[:12]}'s launcher/release_keys.pub has no key {cfg['key_label']} (stable_environment.json key_label)")
    if hit[0] == 0:
        raise Fail(f"{cfg['key_label']} is the first entry of launcher/release_keys.pub, the Deck key: it never signs on GitHub Actions")
    return cfg["key_label"], keys[hit[0]][1].hex()


def _version(tag: str) -> tuple[int, int, int]:
    return tuple(int(x) for x in tag[1:].split("."))


def _nightly_hexes(text: str, where: str) -> list[str]:
    import release_keys
    try:
        return [k.hex() for _, k in release_keys.parse(text, where, allow_empty=True, max_keys=release_keys.MAX_NIGHTLY_KEYS)]
    except release_keys.KeyListError as ex:
        raise Fail(str(ex))


# RELGH-2 (Sol r1): how long the transition release must have been published before a nightly may be signed on Actions: a launcher that
# updates itself at a start in this window follows the switch; one not started in it stays behind (docs/RELEASE.md, the transition)
TRUST_DAYS = 14


def _trust_days() -> int:
    test = os.environ.get("OPENBFME_TEST_TRUST_DAYS")   # tests only (stand-in mode)
    if test is not None and os.environ.get("OPENBFME_RELEASE_STANDIN"):
        return int(test)
    return TRUST_DAYS


def trusted_by_stable(commit: str, keys_hex: list[str] | None = None, cwd: Path | None = None, remote: str | None = None,
                      repo: str = TARGET, kind: str = "nightly") -> str:
    """RELGH-2, the transition rule (Sol r1: a tag alone is no evidence): the newest PUBLISHED stable release of `repo` on GitHub (not a
    draft, not a pre-release, a plain v<x.y.z>) must
      * carry manifest.json and its .sig, the signature valid under the STABLE list (the commit's launcher/release_keys.pub: every
        launcher already installed can authenticate that release), the manifest naming the release's tag, the stable channel, `repo`
        and its commit;
      * list, in that commit's launcher/nightly_keys.pub, every nightly key of `commit`'s list (keys_hex: these keys instead, e.g. the one
        the sign job holds): its launchers and games trust them; RELGH-3, kind "stable": list keys_hex (the stable Actions key) in that
        commit's launcher/release_keys.pub instead;
      * have been published TRUST_DAYS days ago or more.
    The release's commit is read from this repository (fetched by its id from `remote` first, when given: the Deck's archive commit is
    not on the public history). Returns the release's tag; Fail on anything else."""
    import release_keys
    want = keys_hex
    if kind not in KINDS or (kind == "stable" and not want):
        raise Fail("the transition rule of a stable release needs the stable Actions key")
    if want is None:
        try:
            text = git("show", f"{commit}:launcher/nightly_keys.pub", cwd=cwd)
        except Fail:
            raise Fail(f"{commit[:12]} has no launcher/nightly_keys.pub") from None
        want = _nightly_hexes(text, f"nightly_keys.pub of {commit[:12]}")
    if not want:
        raise Fail(f"{commit[:12]}'s launcher/nightly_keys.pub lists no key: nothing can sign a nightly on GitHub Actions")
    try:
        stable_keys = release_keys.parse(git("show", f"{commit}:launcher/release_keys.pub", cwd=cwd), "release_keys.pub")
    except (Fail, release_keys.KeyListError) as ex:
        raise Fail(f"{commit[:12]} has no valid launcher/release_keys.pub ({ex})") from None
    releases = gh_json(f"repos/{repo}/releases?per_page=100") or []
    published = [r for r in releases if isinstance(r, dict) and r.get("draft") is False and r.get("prerelease") is False
                 and isinstance(r.get("tag_name"), str) and KINDS["stable"].match(r["tag_name"])]
    if not published:
        raise Fail(f"{repo} has no published stable release")
    rel = max(published, key=lambda r: _version(r["tag_name"]))
    tag = rel["tag_name"]
    assets = {x.get("name"): x for x in rel.get("assets") or [] if isinstance(x, dict)}
    if "manifest.json" not in assets or "manifest.json.sig" not in assets:
        raise Fail(f"the newest published stable release {tag} has no manifest.json / manifest.json.sig")
    data, sig = (_download(repo, assets[n]) for n in ("manifest.json", "manifest.json.sig"))
    if release_keys.verify_detail(stable_keys, "manifest.json", data, sig) is None:
        raise Fail(f"the manifest of the newest published stable release {tag} is not signed by a stable key")
    try:
        m = json.loads(data)
    except ValueError:
        raise Fail(f"the manifest of {tag} is not JSON") from None
    if not isinstance(m, dict) or (m.get("version"), m.get("channel"), m.get("repo")) != (tag, "stable", repo) \
            or not isinstance(m.get("commit"), str) or not HEX40.match(m["commit"]):
        raise Fail(f"the manifest of {tag} does not name {tag}, the stable channel, {repo} and a commit")
    rev = m["commit"]
    if remote is not None:
        git("fetch", "-q", "--no-tags", remote, rev, cwd=cwd)
    lst = "nightly_keys.pub" if kind == "nightly" else "release_keys.pub"
    try:
        text = git("show", f"{rev}:launcher/{lst}", cwd=cwd)
        if kind == "nightly":
            have = set(_nightly_hexes(text, f"nightly_keys.pub of {tag}"))
        else:
            have = {k.hex() for _, k in release_keys.parse(text, f"release_keys.pub of {tag}")}
    except (Fail, release_keys.KeyListError):
        have = set()
    missing = [k[:16] for k in want if k not in have]
    if missing:
        raise Fail(f"the newest published stable release {tag} does not trust the {kind} key(s) {', '.join(missing)}...: its launchers "
                   f"would refuse the {kind} release. Ship a stable release (signed with a key they trust) whose launcher/{lst} lists "
                   f"them first, then sign {'nightlies' if kind == 'nightly' else 'stable releases'} here (docs/RELEASE.md, the transition)")
    when = str(rel.get("published_at") or "")
    try:
        import calendar
        age = time.time() - calendar.timegm(time.strptime(when, "%Y-%m-%dT%H:%M:%SZ"))
    except ValueError:
        raise Fail(f"the release {tag} has no publication time ({when!r})") from None
    days = _trust_days()
    if age < days * 86400:
        raise Fail(f"the transition release {tag} was published {age / 86400:.1f} days ago, under {days}: launchers that have not "
                   f"started since would refuse the {kind} release. Sign on the Deck until then")
    return tag


def _download(repo: str, asset: dict) -> bytes:
    aid = asset.get("id")
    if not isinstance(aid, int) or not isinstance(asset.get("size"), int) or asset["size"] > (1 << 20):
        raise Fail(f"{asset.get('name')}: not a small uploaded asset")
    r = subprocess.run([gh(), "api", "-H", "Accept: application/octet-stream", f"repos/{repo}/releases/assets/{aid}"], capture_output=True)
    if r.returncode or len(r.stdout) != asset["size"]:
        raise Fail(f"{asset.get('name')}: the download failed or has another size")
    return r.stdout


def cmd_trust_check(a) -> None:
    if not HEX40.match(a.commit):
        raise Fail("the commit is not 40 hex")
    if a.kind == "nightly":
        got = trusted_by_stable(a.commit, cwd=Path(a.git_dir), remote=a.remote, repo=a.repo)
        print(f"TRUST ok: the nightly keys of {a.commit[:12]} are trusted by {got}")
        return
    import stable_environment
    cfg = commit_config(a.commit, cwd=Path(a.git_dir))
    label, key = actions_stable_key(a.commit, cfg, cwd=Path(a.git_dir))
    got = trusted_by_stable(a.commit, [key], cwd=Path(a.git_dir), remote=a.remote, repo=a.repo, kind="stable")
    try:
        u = stable_environment.gh_json("user")
        stable_environment.check(a.repo, cfg, u.get("login") if isinstance(u, dict) else None)
    except stable_environment.Fail as ex:
        raise Fail(f"the environment {cfg['environment']}: {ex}") from None
    print(f"TRUST ok: the stable Actions key {label} of {a.commit[:12]} is trusted by {got}; the environment {cfg['environment']} is "
          f"its configuration and this machine's gh account is no reviewer")


def _has_ref(ref: str) -> bool:
    return subprocess.run(["git", "show-ref", "-q", "--verify", ref]).returncode == 0


def cmd_checkout_is_tag(_a) -> None:
    e = request_env()
    head = git("rev-parse", "HEAD")
    if head != e["commit"]:
        raise Fail(f"the checkout is {head}, not {e['commit']}")
    # RELGH-3 (dry run): .gitattributes marks *.png as Git LFS while the public tree holds them as plain blobs; the runner's git-lfs clean
    # filter turns them into pointers on a status read (racily) and the checkout looks changed, here and in package.sh's clean-tree check
    # later in the job. The build hosts have no git-lfs filter; this checkout (the job's own, thrown away) gets none either, as in
    # release-package.yml
    if os.environ.get("GITHUB_ACTIONS") == "true":
        for k, v in (("filter.lfs.process", ""), ("filter.lfs.clean", ""), ("filter.lfs.smudge", ""), ("filter.lfs.required", "false")):
            git("config", "--local", k, v)
    if git("status", "--porcelain", "--untracked-files=no"):
        raise Fail("the checkout has changes")
    d = git("describe", "--tags", "--match", "v[0-9]*", "--exclude", "*-playtest.*", "--always", "--dirty", "--abbrev=10")
    if d != e["tag"]:
        raise Fail(f"the checkout describes as {d}, not {e['tag']} (the builds would name {d})")
    print(f"CHECKOUT ok: {head} is {e['tag']}")


# ---- the release folder ------------------------------------------------------------------------------------------------------------------

def expected_build_files(version: str, legacy: bool) -> tuple[list[str], list[str]]:
    """(package.sh's folder, package_macos.py's folder): exactly the files a release build returns (autorelease.sh step 5)"""
    linux = [f"openbfme-{version}-linux-x64.tar.gz", f"openbfme-{version}-windows-x64.zip"]
    if legacy:
        linux += [f"openbfme-launcher-{version}-linux-x64.tar.gz", f"openbfme-launcher-{version}-windows-x64.zip"]
    linux += [f"SHA256SUMS-{version}.txt", f"BUILDINFO-{version}.txt"]
    stem = f"openbfme-{version}-macos-arm64"
    mac = [f"{stem}.zip", f"{stem}.zip.sha256", f"{stem}.inventory.txt", "README-MAC.txt"]
    return linux, mac


def _exactly(d: Path, want: list[str]) -> None:
    if not d.is_dir():
        raise Fail(f"{d} is missing")
    got = sorted(p.name + ("/" if p.is_dir() else "") for p in d.iterdir())
    if got != sorted(want) or any(p.is_symlink() or not p.is_file() for p in d.iterdir()):
        raise Fail(f"{d.name} holds {got}, not exactly {sorted(want)}")


# the one step's environment variable that holds the key, by kind (release.yml: one per signing job, public_workflows.py)
SECRET_VARS = {"nightly": "NIGHTLY_SIGNING_KEY", "stable": "STABLE_SIGNING_KEY"}


def cmd_sign(a) -> None:
    # the secrets leave the environment first, so that no child process (the build tools below) ever inherits one
    pems = {k: os.environ.pop(v, "") for k, v in SECRET_VARS.items()}
    e = request_env()
    kind = e["kind"]
    var = SECRET_VARS[kind]
    pem = pems.pop(kind)
    if any(x.strip() for x in pems.values()):
        raise Fail(f"the step holds the key of another kind ({', '.join(SECRET_VARS[k] for k, x in pems.items() if x.strip())}): "
                   f"a {kind} release is signed with {var} alone")
    del pems
    import ed25519
    import release_keys
    keys_file, nightly_file = REPO / "launcher" / "release_keys.pub", REPO / "launcher" / "nightly_keys.pub"
    nightly = release_keys.load_nightly(nightly_file, keys_file)
    env_name = "nightly-signing" if kind == "nightly" else commit_config(e["commit"])["environment"]
    if not pem.strip():
        raise Fail(f"{var} (the environment {env_name}'s secret) is empty")
    try:
        public = ed25519.public_key(ed25519.secret_from_pem(pem)).hex()
    except Exception:  # noqa: BLE001 (never echo anything of the secret)
        raise Fail(f"{var} is not an Ed25519 private key (PKCS#8 PEM, sign_manifest.py --generate-key)") from None
    if kind == "nightly":
        label = next((lb for lb, k in nightly if k.hex() == public), None)
        if label is None:
            raise Fail(f"the environment's nightly key ({public[:16]}...) is not in launcher/nightly_keys.pub of {e['commit'][:12]}")
        trusted_by_stable(e["commit"], [public])
    else:
        # RELGH-3: exactly the commit's stable Actions key (never the Deck key), trusted by the newest stable release; the environment
        # and this run's approval checked once more, from this job, before the key touches a file
        import stable_environment
        cfg = commit_config(e["commit"])
        label, want = actions_stable_key(e["commit"], cfg)
        if public != want:
            first = release_keys.load(keys_file)[0]
            what = "the Deck key (the offline recovery key: it never enters GitHub)" if public == first[1].hex() else f"{public[:16]}..."
            raise Fail(f"the environment's stable key is {what}, not the stable Actions key {label} of {e['commit'][:12]}")
        trusted_by_stable(e["commit"], [public], kind="stable")
        trig = os.environ.get("GITHUB_TRIGGERING_ACTOR", "")
        try:
            stable_environment.check(TARGET, cfg, trig)
            probs = stable_environment.approval_problems(
                cfg, stable_environment.gh_json(f"repos/{TARGET}/actions/runs/{os.environ.get('GITHUB_RUN_ID', '0')}/approvals"), trig)
        except stable_environment.Fail as ex:
            raise Fail(f"the environment {cfg['environment']}: {ex}") from None
        if probs:
            raise Fail("; ".join(probs))
    keyfile = write_secret(pem, kind)
    del pem
    try:
        sign_folder(a, e, keyfile, label, keys_file, nightly_file)
    finally:
        shred(keyfile)


SHM = Path(os.environ.get("OPENBFME_TEST_SHM", "/dev/shm"))


def write_secret(pem: str, kind: str = "nightly") -> Path:
    """the key into a new mode 0700 folder on tmpfs (memory only, never the runner's disk), file mode 0600 (release.yml's always-step
    removes openbfme-<kind>-key-* leftovers)"""
    mounts = Path("/proc/mounts").read_text(encoding="utf-8", errors="replace").split("\n") if Path("/proc/mounts").is_file() else []
    if not any(len(f) > 2 and f[1] == str(SHM) and f[2] == "tmpfs" for f in (m.split() for m in mounts)) \
            and not (os.environ.get("OPENBFME_RELEASE_STANDIN") and "OPENBFME_TEST_SHM" in os.environ):
        raise Fail(f"{SHM} is not a tmpfs: the {kind} key is never written to a disk")
    if kind not in SECRET_VARS:
        raise Fail(f"no key of the kind {kind!r}")
    d = SHM / f"openbfme-{kind}-key-{os.environ.get('GITHUB_RUN_ID', '0')}-{os.getpid()}"
    os.mkdir(d, 0o700)
    f = d / f"{kind}.pem"
    fd = os.open(f, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as out:
        out.write(pem)
    return f


def shred(f: Path) -> None:
    """overwrite, unlink, remove the folder (the workflow's last step removes any leftover too)"""
    try:
        if f.exists():
            n = f.stat().st_size
            with open(f, "r+b") as out:
                out.write(os.urandom(n))
                out.flush()
                os.fsync(out.fileno())
            f.unlink()
        f.parent.rmdir()
    except OSError as ex:
        print(f"RELEASE ACTIONS WARNING: could not remove the key folder ({ex.__class__.__name__})", file=sys.stderr)


def sign_folder(a, e: dict, keyfile: Path, label: str, keys_file: Path, nightly_file: Path) -> None:
    version = e["tag"]
    legacy = os.environ.get("OPENBFME_RELEASE_LEGACY_LAUNCHERS", "1") != "0"
    lin, mac = expected_build_files(version, legacy)
    src_pkg, src_mac = Path(a.inp) / "packages", Path(a.inp) / "macos"
    _exactly(src_pkg, lin)
    _exactly(src_mac, mac)
    pkg = Path(a.pkg)
    if pkg.exists():
        raise Fail(f"{pkg} exists already")
    pkg.mkdir(parents=True)
    for f in lin:
        shutil.copyfile(src_pkg / f, pkg / f)
    T = str(HERE)
    archives = [f for f in lin if f.startswith("openbfme-")]
    run([sys.executable, f"{T}/audit_package.py", "--sums", f"SHA256SUMS-{version}.txt", *archives], cwd=pkg)
    for f in archives:
        ap = ("launcher-linux" if f.endswith("linux-x64.tar.gz") else "launcher-windows") if f.startswith("openbfme-launcher-") \
            else ("linux" if f.endswith("linux-x64.tar.gz") else "windows")
        run([sys.executable, f"{T}/audit_package.py", "--release", ap, f], cwd=pkg)
    maczip = mac[0]
    run([sys.executable, f"{T}/make_macos_manifest.py", "--verify-package", str(src_mac / maczip), "--checksum", str(src_mac / mac[1]),
         "--version", version, "--commit", e["commit"], "--repo", TARGET, "--release-key-file", str(REPO / "launcher" / "release_key.pub"),
         "--nightly-key-file", str(nightly_file)])
    # the Mac package joins the folder before the sealing (autorelease.sh step 6, MACPLAY-1)
    shutil.copyfile(src_mac / maczip, pkg / maczip)
    with open(pkg / f"SHA256SUMS-{version}.txt", "a", encoding="ascii") as sums:
        sums.write((src_mac / mac[1]).read_text(encoding="ascii"))
    run([sys.executable, f"{T}/seal_package.py", "seal", str(pkg), "--key", str(keyfile), "--repo", TARGET])
    fmt = "2" if legacy else "3"
    run([sys.executable, f"{T}/make_manifest.py", str(pkg), "--repo", TARGET, "--format", fmt])
    mdate = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["date"]
    sealed_sum = [ln for ln in (pkg / f"SHA256SUMS-{version}.txt").read_text(encoding="ascii").splitlines() if ln.endswith("  " + maczip)]
    if len(sealed_sum) != 1:
        raise Fail(f"SHA256SUMS-{version}.txt has {len(sealed_sum)} lines for {maczip}")
    sealed = Path(a.pkg + ".mac.sha256")
    sealed.write_text(sealed_sum[0] + "\n", encoding="ascii")
    run([sys.executable, f"{T}/make_macos_manifest.py", "--version", version, "--repo", TARGET, "--commit", e["commit"], "--date", mdate,
         "--archive", str(pkg / maczip), "--checksum", str(sealed), "--out", str(pkg / "manifest-macos.json")])
    for m in ("manifest.json", "manifest-macos.json"):
        run([sys.executable, f"{T}/sign_manifest.py", "--key", str(keyfile), str(pkg / m)])
    # everything once more, from the files alone, under the committed lists (the scope rule: a nightly key signs a nightly's files only)
    kf, nf = str(keys_file), str(nightly_file)
    run([sys.executable, f"{T}/seal_package.py", "check", str(pkg), "--public", kf, "--nightly", nf, "--repo", TARGET])
    run([sys.executable, f"{T}/make_manifest.py", "--check", str(pkg), "--version", version])
    run([sys.executable, f"{T}/make_macos_manifest.py", "--check", str(pkg), "--release-key-file", str(REPO / "launcher" / "release_key.pub"),
         "--seal-key-file", kf, "--nightly-key-file", nf])
    for m in ("manifest.json", "manifest-macos.json"):
        out = run([sys.executable, f"{T}/sign_manifest.py", "--verify", kf, "--nightly", nf, str(pkg / m)])
        if f"({label}, raw)" not in out:
            raise Fail(f"{m}.sig does not verify as the {label} key's signature")
    if any(b"PRIVATE KEY" in p.read_bytes() for p in pkg.iterdir()):
        raise Fail("a private key is among the release's files")
    summary(f"Signed {version} with the {e['kind']} key {label}: " + ", ".join(
        f"{p.name} {hashlib.sha256(p.read_bytes()).hexdigest()}" for p in sorted(pkg.iterdir())))
    print(f"SIGN ok: {version}")


def summary(text: str) -> None:
    p = os.environ.get("GITHUB_STEP_SUMMARY")
    if p:
        with open(p, "a", encoding="utf-8") as f:
            f.write(text + "\n")


# ---- the draft -------------------------------------------------------------------------------------------------------------------------

def gh_json(*args: str):
    out = run([gh(), "api", *args])
    return json.loads(out) if out.strip() else None


def cmd_draft(a) -> None:
    e = request_env()
    pkg = Path(a.pkg)
    names = run([sys.executable, str(HERE / "release_assets.py"), "--names", "--dir", str(pkg), "--version", e["tag"]]).split()
    if sorted(names) != sorted(p.name for p in pkg.iterdir()):
        raise Fail(f"the folder holds {sorted(p.name for p in pkg.iterdir())}, the release's assets are {sorted(names)}")
    releases = gh_json(f"repos/{TARGET}/releases?per_page=100") or []
    same = [r for r in releases if r.get("tag_name") == e["tag"]]
    if same:
        raise Fail(f"a release of {e['tag']} exists already (ids {[r.get('id') for r in same]}, drafts {[r.get('draft') for r in same]}): "
                   "nothing is created (the Deck removes a stale draft)")
    title = run([sys.executable, str(HERE / "autorelease.py"), "title", e["tag"]]).strip()
    run_id = os.environ.get("GITHUB_RUN_ID", "0")
    body = (f"OpenBFME {e['tag']}: signed on GitHub Actions (run {run_id}). The release's host checks it, adds the notes and publishes it.\n\n"
            f"{REQUEST_LINE}{e['request']}\n")
    body_file = pkg.parent / f"{pkg.name}.body.md"
    body_file.write_text(body, encoding="utf-8")
    rel = gh_json("--method", "POST", f"repos/{TARGET}/releases", "-f", f"tag_name={e['tag']}", "-f", f"name={title}",
                  "-F", "draft=true", "-F", f"prerelease={'true' if e['kind'] == 'nightly' else 'false'}", "-F", f"body=@{body_file}")
    rid = rel.get("id") if isinstance(rel, dict) else None
    if not isinstance(rid, int) or rel.get("draft") is not True:
        raise Fail("the draft release was not created as a draft")
    run([gh(), "release", "upload", e["tag"], "--repo", TARGET, *[str(pkg / n) for n in names]])
    back = gh_json(f"repos/{TARGET}/releases/{rid}")
    if back.get("draft") is not True or back.get("tag_name") != e["tag"]:
        raise Fail(f"release {rid} does not read back as the draft of {e['tag']}")
    have = {x["name"]: x for x in back.get("assets", [])}
    if sorted(have) != sorted(names):
        raise Fail(f"release {rid} holds {sorted(have)}, not {sorted(names)}")
    for n in names:
        data = (pkg / n).read_bytes()
        if have[n].get("size") != len(data) or have[n].get("digest") != "sha256:" + hashlib.sha256(data).hexdigest():
            raise Fail(f"{n} reads back with another size or digest")
    summary(f"Draft release {rid} of {e['tag']} (request {e['request']}): {len(names)} assets, read back")
    print(f"DRAFT ok: release {rid} of {e['tag']}")


# ---- the Deck's side ---------------------------------------------------------------------------------------------------------------------

def cmd_dispatch(a) -> None:
    kind = kind_of(a.tag)
    if kind is None:
        raise Fail(f"{a.tag} is not a nightly or a stable tag: nothing else is signed on GitHub Actions")
    if not HEX40.match(a.commit):
        raise Fail("the commit is not 40 hex")
    request = secrets.token_hex(16)
    out = Path(a.out)
    out.write_text(json.dumps({"request": request, "tag": a.tag, "kind": kind, "commit": a.commit, "run_id": None}) + "\n")
    run([gh(), "workflow", "run", WORKFLOW, "--repo", a.repo, "--ref", "main", "-f", f"tag={a.tag}",
         "-f", f"commit={a.commit}", "-f", f"request={request}"])
    print(f"DISPATCHED {a.tag}, request {request}")


def _find_run(repo: str, request: str):
    runs = gh_json(f"repos/{repo}/actions/workflows/{WORKFLOW}/runs?event=workflow_dispatch&branch=main&per_page=50") or {}
    mine = [r for r in runs.get("workflow_runs", []) if f"request {request})" in str(r.get("display_title", ""))]
    if len(mine) > 1:
        raise Fail(f"{len(mine)} runs name the request {request}")
    return mine[0] if mine else None


def cmd_wait(a) -> None:
    rf = Path(a.request_file)
    req = json.loads(rf.read_text())
    deadline = time.monotonic() + a.timeout
    run_ = None
    while True:
        run_ = _find_run(a.repo, req["request"])
        if run_ and run_.get("status") == "completed":
            break
        if time.monotonic() > deadline:
            state = run_.get("status") if run_ else "not started"
            if state == "waiting":   # RELGH-3: a stable run waits for the review of its environment
                state = "waiting for the owner's review (Approve in the run's page)"
            raise Fail(f"the release run of {req['tag']} is still {state} after {a.timeout} s")
        time.sleep(a.poll)
    if run_.get("head_branch") != "main" or run_.get("event") != "workflow_dispatch" or run_.get("path", f".github/workflows/{WORKFLOW}") \
            != f".github/workflows/{WORKFLOW}":
        raise Fail(f"run {run_.get('id')} is not the release workflow dispatched on main")
    if run_.get("conclusion") != "success":
        raise Fail(f"the release run {run_.get('html_url', run_.get('id'))} ended {run_.get('conclusion')}")
    req["run_id"] = run_["id"]
    rf.write_text(json.dumps(req) + "\n")
    releases = gh_json(f"repos/{a.repo}/releases?per_page=100") or []
    drafts = [r for r in releases if r.get("tag_name") == req["tag"] and r.get("draft") and f"{REQUEST_LINE}{req['request']}" in str(r.get("body", ""))]
    if len(drafts) != 1:
        raise Fail(f"{len(drafts)} draft releases of {req['tag']} name the request {req['request']} (exactly one)")
    print(drafts[0]["id"])


def _zip_members(data: bytes) -> list[tuple[str, int, bytes]]:
    import io
    import zipfile
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        return [(i.filename, (i.external_attr, i.date_time, i.compress_type), z.read(i)) for i in z.infolist()]


def same_build_archive(name: str, cand: bytes, act: bytes) -> str | None:
    """RELSIGN-1: a Windows ZIP whose bytes differ only through its DLL's CodeView GUID (MACACT-1: lld derives it from the private PDB, which
    holds the build folder's absolute path; pdb_tools.same_build) is the same build: None. Anything else: why not. Only the game's Windows
    ZIP (openbfme-<v>-windows-x64.zip) qualifies; every other archive must be the candidate's byte for byte."""
    if not (name.startswith("openbfme-") and not name.startswith("openbfme-launcher-") and name.endswith("-windows-x64.zip")):
        return "not the same bytes"
    import pdb_tools
    try:
        a, b = _zip_members(cand), _zip_members(act)
    except Exception as e:  # noqa: BLE001 (a ZIP that does not read is a difference)
        return f"unreadable ZIP: {e}"
    if [(n, m) for n, m, _ in a] != [(n, m) for n, m, _ in b]:
        return "the member lists differ"
    dlls = 0
    for (n, _, x), (_, _, y) in zip(a, b):
        if x == y:
            continue
        if not n.lower().endswith(".dll"):
            return f"{n} differs"
        bad = pdb_tools.same_build(x, y)
        if bad:
            return f"{n}: {bad[0]}"
        dlls += 1
    return None if dlls else "the members are the same but the ZIP bytes are not"


def cmd_compare(a) -> None:
    import seal_package
    mode = os.environ.get("OPENBFME_RELEASE_ACTIONS_COMPARE", "require")
    if mode not in ("require", "report"):
        raise Fail("OPENBFME_RELEASE_ACTIONS_COMPARE is require or report")
    cand, act = Path(a.candidate), Path(a.actions)
    names = sorted(p.name for p in cand.iterdir() if p.name.startswith("openbfme-"))
    other = sorted(p.name for p in act.iterdir() if p.name.startswith("openbfme-"))
    if names != other or not names:
        raise Fail(f"the candidate's archives are {names}, the draft's {other}")
    import tempfile
    diffs = []
    with tempfile.TemporaryDirectory(prefix="compare-") as tmp:
        for n in names:
            pair = []
            for side, d in (("candidate", cand), ("actions", act)):
                src = d / n
                if seal_package.NAME.match(n):
                    out = Path(tmp) / f"{side}-{n}"
                    seal_package.unseal(src, out)
                    src = out
                pair.append(src.read_bytes())
            same = pair[0] == pair[1]
            why = "" if same else same_build_archive(n, pair[0], pair[1])
            if not same and why is None:
                print(f"COMPARE same build: {n} (its members are the candidate's; the Windows DLL but for its CodeView GUID)")
                continue
            print(f"COMPARE {'identical' if same else 'DIFFERS'}: {n}" + ("" if same else
                  f" (candidate {hashlib.sha256(pair[0]).hexdigest()[:16]}, actions {hashlib.sha256(pair[1]).hexdigest()[:16]}: {why})"))
            if not same:
                diffs.append(n)
    if diffs and mode == "require":
        raise Fail(f"{len(diffs)} archive(s) of the draft are not the tested candidate's: {', '.join(diffs)}")
    if diffs:
        print(f"COMPARE WARNING (report mode): {len(diffs)} archive(s) differ from the tested candidate")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("plan")
    sub.add_parser("checkout-is-tag")
    s = sub.add_parser("sign")
    s.add_argument("--in", dest="inp", required=True)
    s.add_argument("--pkg", required=True)
    d = sub.add_parser("draft")
    d.add_argument("--pkg", required=True)
    p = sub.add_parser("dispatch")
    p.add_argument("--tag", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--repo", default=TARGET)
    p.add_argument("--out", required=True)
    w = sub.add_parser("wait")
    w.add_argument("--request-file", required=True)
    w.add_argument("--repo", default=TARGET)
    w.add_argument("--timeout", type=int, default=4 * 3600)
    w.add_argument("--poll", type=int, default=60)
    t = sub.add_parser("trust-check")
    t.add_argument("--commit", required=True)
    t.add_argument("--kind", choices=sorted(KINDS), default="nightly")
    t.add_argument("--git-dir", default=str(REPO))
    t.add_argument("--remote", default=None, help="fetch the stable release's commit from this remote first (the Deck)")
    t.add_argument("--repo", default=TARGET)
    c = sub.add_parser("compare")
    c.add_argument("--candidate", required=True)
    c.add_argument("--actions", required=True)
    c.add_argument("--version", required=True)
    a = ap.parse_args(argv)
    try:
        {"plan": cmd_plan, "checkout-is-tag": cmd_checkout_is_tag, "sign": cmd_sign, "draft": cmd_draft, "dispatch": cmd_dispatch,
         "wait": cmd_wait, "compare": cmd_compare, "trust-check": cmd_trust_check}[a.cmd](a)
    except Fail as e:
        print(f"RELEASE ACTIONS FAIL ({a.cmd}): {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
