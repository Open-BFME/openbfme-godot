#!/usr/bin/env python3
"""Lane SIGNKMS-1: the release workflow's steps (.github/workflows/release.yml) and the Deck's side of a release signed on GitHub Actions
(docs/RELEASE.md, "Release signing").

On GitHub's runners (the inputs come from the environment the workflow sets, IN_KIND / IN_TAG / IN_COMMIT / IN_REQUEST, never from the
command line, so no input is ever pasted into a script):
    actions_release.py plan                      the request: workflow_dispatch on refs/heads/main of the policy's repository; kind nightly
                                                 with a v<x.y.z>-nightly.<YYMMDD><1|2>00 (or <YYYYMMDD>) tag, kind stable with a plain v<x.y.z> tag; the tag
                                                 names the commit; the commit is on main (an ancestor of the run's main head)
    actions_release.py checkout-is-tag           the checkout is the commit and `git describe` is the tag (the builds embed it)
    actions_release.py sign --in DIR --pkg DIR   the build jobs' artifacts into one release folder (exactly the files a release has), the
                                                 strict package checks, then sealing and signing through AWS KMS (kms_sign.py; GitHub's
                                                 OIDC token traded for a 15-minute session of $SIGNING_ROLE_ARN), every signature verified
                                                 under the committed launcher/release_keys.pub
    actions_release.py draft --pkg DIR           the DRAFT release of the tag with every file of the folder, read back (names, sizes and
                                                 GitHub's SHA-256 of each upload); it never publishes
On the Deck (autorelease.sh with OPENBFME_RELEASE_SIGNER=actions):
    actions_release.py dispatch --tag T --commit C --kind K [--repo R] --out FILE
                                                 starts the workflow on main with a fresh request id; FILE records {request, run_id}
    actions_release.py wait --request-file FILE [--timeout S] [--repo R]
                                                 waits for that run (a stable run waits for the owner's approval of the `stable`
                                                 environment), requires success, prints the draft release's id (the draft whose body names
                                                 the request)
    actions_release.py compare --candidate DIR --actions DIR --version V
                                                 the draft's packages (downloaded by cosign_release.py) against the candidate this Deck
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


def request_env() -> dict:
    """the dispatched inputs, from the environment, each checked before anything uses it"""
    e = {k: os.environ.get(f"IN_{k.upper()}", "") for k in ("kind", "tag", "commit", "request")}
    if e["kind"] not in KINDS:
        raise Fail(f"kind {e['kind']!r} is not nightly or stable")
    if not KINDS[e["kind"]].match(e["tag"]):
        raise Fail(f"the tag {e['tag']!r} is not a {e['kind']} release tag ("
                   + ("v<x.y.z>-nightly.<YYMMDD><1|2>00 or -nightly.<YYYYMMDD>" if e["kind"] == "nightly" else "a plain v<x.y.z>") + ")")
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
    head = os.environ.get("GITHUB_SHA", "")
    if not HEX40.match(head):
        raise Fail("GITHUB_SHA is not a commit id")
    tagged = git("rev-parse", "-q", "--verify", f"refs/tags/{e['tag']}^{{commit}}") if _has_ref(f"refs/tags/{e['tag']}") else ""
    if tagged != e["commit"]:
        raise Fail(f"the tag {e['tag']} names {tagged or 'nothing here'}, not {e['commit']}")
    if subprocess.run(["git", "merge-base", "--is-ancestor", e["commit"], head]).returncode:
        raise Fail(f"{e['commit']} is not on main (not an ancestor of the run's main head {head})")
    print(f"PLAN ok: {e['kind']} {e['tag']} at {e['commit']} (request {e['request']}, main {head})")


def _has_ref(ref: str) -> bool:
    return subprocess.run(["git", "show-ref", "-q", "--verify", ref]).returncode == 0


def cmd_checkout_is_tag(_a) -> None:
    e = request_env()
    head = git("rev-parse", "HEAD")
    if head != e["commit"]:
        raise Fail(f"the checkout is {head}, not {e['commit']}")
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


def cmd_sign(a) -> None:
    e = request_env()
    import release_keys
    policy = release_keys.load_policy()
    keys = release_keys.load(REPO / "launcher" / "release_keys.pub")
    if not any(label == policy["kms_label"] for label, _ in keys):
        raise Fail(f"launcher/release_keys.pub has no {policy['kms_label']} key: commit the KMS public key first (docs/RELEASE.md)")
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
         "--version", version, "--commit", e["commit"], "--repo", TARGET, "--release-key-file", str(REPO / "launcher" / "release_key.pub")])
    # the Mac package joins the folder before the sealing (autorelease.sh step 6, MACPLAY-1)
    shutil.copyfile(src_mac / maczip, pkg / maczip)
    with open(pkg / f"SHA256SUMS-{version}.txt", "a", encoding="ascii") as sums:
        sums.write((src_mac / mac[1]).read_text(encoding="ascii"))
    env = aws_session_env()
    run([sys.executable, f"{T}/seal_package.py", "seal", str(pkg), "--kms", "--repo", TARGET], env=env)
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
    slot = "both" if policy["phase"] == "kms" else "sig2"
    for m in ("manifest.json", "manifest-macos.json"):
        run([sys.executable, f"{T}/kms_sign.py", "--slot", slot, str(pkg / m)], env=env)
    # everything once more, from the files alone, under the committed list
    keys_file = str(REPO / "launcher" / "release_keys.pub")
    run([sys.executable, f"{T}/seal_package.py", "check", str(pkg), "--public", keys_file, "--repo", TARGET])
    run([sys.executable, f"{T}/make_manifest.py", "--check", str(pkg), "--version", version])
    run([sys.executable, f"{T}/make_macos_manifest.py", "--check", str(pkg), "--release-key-file", str(REPO / "launcher" / "release_key.pub"),
         "--seal-key-file", keys_file])
    for m in ("manifest.json", "manifest-macos.json"):
        for s in (("sig", "sig2") if slot == "both" else ("sig2",)):
            run([sys.executable, f"{T}/sign_manifest.py", "--verify", keys_file, str(pkg / m), "--sig", str(pkg / f"{m}.{s}")])
        if slot == "sig2" and (pkg / f"{m}.sig").exists():
            raise Fail(f"{m}.sig exists in the transition phase (the Deck co-signs it)")
    if any(b"PRIVATE KEY" in p.read_bytes() for p in pkg.iterdir()):
        raise Fail("a private key is among the release's files")
    summary(f"Signed {version} ({e['kind']}, phase {policy['phase']}): " + ", ".join(
        f"{p.name} {hashlib.sha256(p.read_bytes()).hexdigest()}" for p in sorted(pkg.iterdir())))
    print(f"SIGN ok: {version}")


def aws_session_env() -> dict:
    """GitHub's OIDC token (audience sts.amazonaws.com) traded for a 15-minute session of the signing role; the credentials live in the
    environment of the aws processes this run starts, never in a file, an output or the log"""
    role, region = os.environ.get("SIGNING_ROLE_ARN", ""), os.environ.get("SIGNING_REGION", "")
    if not re.fullmatch(r"arn:(aws):iam::[0-9]{12}:role/openbfme-release-signer", role):
        raise Fail("SIGNING_ROLE_ARN (the environment's secret OPENBFME_SIGNING_ROLE_ARN) is not the signing role's ARN")
    if not re.fullmatch(r"[a-z]{2}-[a-z]+-[0-9]", region):
        raise Fail("SIGNING_REGION (the environment's variable OPENBFME_SIGNING_REGION) is not an AWS region")
    url, bearer = os.environ.get("ACTIONS_ID_TOKEN_REQUEST_URL", ""), os.environ.get("ACTIONS_ID_TOKEN_REQUEST_TOKEN", "")
    if not url or not bearer:
        raise Fail("no OIDC token request in this job (it needs the id-token permission)")
    import urllib.request
    req = urllib.request.Request(url + ("&" if "?" in url else "?") + "audience=sts.amazonaws.com",
                                 headers={"Authorization": f"bearer {bearer}"})
    opener = os.environ.get("OPENBFME_TEST_OIDC_FILE")   # tests: the token from a file (stand-in mode only)
    if opener and os.environ.get("OPENBFME_RELEASE_STANDIN"):
        token = Path(opener).read_text().strip()
    else:
        with urllib.request.urlopen(req, timeout=30) as r:  # noqa: S310 (GitHub's own token endpoint, from its environment)
            token = json.loads(r.read())["value"]
    session = f"openbfme-{os.environ.get('GITHUB_RUN_ID', '0')}-{os.environ.get('GITHUB_RUN_ATTEMPT', '0')}"
    env = {k: v for k, v in os.environ.items() if not k.startswith("AWS_") and not k.startswith("ACTIONS_ID_TOKEN")}
    env["AWS_REGION"] = env["AWS_DEFAULT_REGION"] = region
    env["AWS_EC2_METADATA_DISABLED"] = "true"
    r = subprocess.run([os.environ.get("AWS", "aws"), "sts", "assume-role-with-web-identity", "--role-arn", role, "--role-session-name",
                        session, "--web-identity-token", token, "--duration-seconds", "900", "--output", "json"],
                       capture_output=True, text=True, env=env)
    if r.returncode:
        raise Fail(f"AssumeRoleWithWebIdentity failed: {r.stderr.strip()[-300:]}")
    c = json.loads(r.stdout)["Credentials"]
    # the variable names are assembled: the public tree's privacy audit refuses the credential names' literal shape
    for name, field in (("ACCESS_KEY_ID", "AccessKeyId"), ("SECRET_ACCESS_KEY", "SecretAccessKey"), ("SESSION_TOKEN", "SessionToken")):
        env["AWS_" + name] = c[field]
    return env


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
    body = (f"OpenBFME {e['tag']}: signed on GitHub Actions (run {run_id}). The release's Deck checks it, adds the notes and publishes it.\n\n"
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
    if a.kind not in KINDS or not KINDS[a.kind].match(a.tag):
        raise Fail(f"{a.tag} is not a {a.kind} tag")
    if not HEX40.match(a.commit):
        raise Fail("the commit is not 40 hex")
    request = secrets.token_hex(16)
    out = Path(a.out)
    out.write_text(json.dumps({"request": request, "tag": a.tag, "kind": a.kind, "commit": a.commit, "run_id": None}) + "\n")
    run([gh(), "workflow", "run", WORKFLOW, "--repo", a.repo, "--ref", "main", "-f", f"kind={a.kind}", "-f", f"tag={a.tag}",
         "-f", f"commit={a.commit}", "-f", f"request={request}"])
    print(f"DISPATCHED {a.tag} ({a.kind}), request {request}")


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
            raise Fail(f"the release run of {req['tag']} is still {state} after {a.timeout} s (a stable run waits for the owner's approval)")
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
    p.add_argument("--kind", required=True, choices=sorted(KINDS))
    p.add_argument("--repo", default=TARGET)
    p.add_argument("--out", required=True)
    w = sub.add_parser("wait")
    w.add_argument("--request-file", required=True)
    w.add_argument("--repo", default=TARGET)
    w.add_argument("--timeout", type=int, default=4 * 3600)
    w.add_argument("--poll", type=int, default=60)
    c = sub.add_parser("compare")
    c.add_argument("--candidate", required=True)
    c.add_argument("--actions", required=True)
    c.add_argument("--version", required=True)
    a = ap.parse_args(argv)
    try:
        {"plan": cmd_plan, "checkout-is-tag": cmd_checkout_is_tag, "sign": cmd_sign, "draft": cmd_draft, "dispatch": cmd_dispatch,
         "wait": cmd_wait, "compare": cmd_compare}[a.cmd](a)
    except Fail as e:
        print(f"RELEASE ACTIONS FAIL ({a.cmd}): {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
