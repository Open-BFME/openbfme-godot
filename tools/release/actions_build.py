#!/usr/bin/env python3
"""Lane MACACT-1: a release platform built on GitHub Actions (.github/workflows/release-package.yml) for autorelease.sh, from the Deck
(docs/RELEASE.md, "Release builds on GitHub Actions"). The release key never takes part: GitHub builds the public commit, the Deck checks
what comes back exactly as it checks a build of its own hosts, then seals and signs it itself.

    actions_build.py --git-dir G --repo R --remote REMOTE [--gh GH] stage    --commit C --version V
    actions_build.py ...                                             dispatch --commit C --version V --platform P --record FILE
    actions_build.py ...                                             collect  --commit C --version V --platform P --record FILE
                                                                              --out DIR [--timeout SECONDS]
    actions_build.py ...                                             unstage  --commit C --version V
    actions_build.py ...                                             cancel | urls --commit C --version V --record FILE

  stage     pushes C (the candidate's public commit, audited before) to refs/heads/staging/<V> of REMOTE: GitHub builds what is in the
            repository, so the commit must be there first. A leftover staging/<V> (a run killed before its unstage) is replaced, both
            pushes with a lease (the ref must still be what was read, or absent), so a concurrent push is never overwritten;
  dispatch  starts the workflow on staging/<V> for one platform with a one-time request id and records the run GitHub created for THIS
            dispatch (return_run_details: its id; nothing is looked up by name, so no other run can be taken for it);
  collect   waits for that run (polling; --timeout, then the run is cancelled and the build fails), then accepts it only when: it is the
            recorded run, event workflow_dispatch, the workflow file .github/workflows/release-package.yml, on staging/<V> at C (head_sha),
            dispatched by the release account (actor and triggering actor: `gh api user`), its first attempt (a re-run could upload other
            files), the title naming the platform, version and request, concluded success, the platform's job succeeded and every other
            job was skipped; it has exactly one artifact, the expected name with the request id, not expired, made by this run at C; the
            downloaded archive's size and SHA-256 are the artifact's recorded size and digest; the archive holds exactly the platform's
            files (plain names, regular files, no folders) and nothing else. They are written into the empty folder DIR;
  unstage   deletes staging/<V>, with a lease on C (a ref someone moved is left and named);
  cancel    asks GitHub to cancel every recorded run that has not finished (a failed release: best effort, always exits 0);
  urls      prints the recorded runs (`<platform> <url>`, comma separated) for the summary.
A failure exits 1 with an ACTIONS BUILD FAIL line; nothing is retried and there is no fallback (CLAUDE.md): autorelease.sh fails the release.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

WORKFLOW = ".github/workflows/release-package.yml"
VERSION = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-(?:alpha|nightly|preview)\.[0-9]+)?")
SHA = re.compile(r"[0-9a-f]{40}")
REPO_NAME = re.compile(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}")
PLATFORMS = ("macos", "linux", "windows")
POLL = float(os.environ.get("OPENBFME_RELEASE_ACTIONS_POLL", "20"))   # seconds between reads of a run (tests: 0)
FIND = int(os.environ.get("OPENBFME_RELEASE_ACTIONS_FIND", "30"))     # reads a not yet visible run is waited for (GitHub's lag)


def fail(message: str) -> None:
    raise SystemExit(f"ACTIONS BUILD FAIL: {message}")


def stem(platform: str, version: str) -> str:
    return {"macos": f"openbfme-{version}-macos-arm64", "linux": f"openbfme-{version}-linux-x64",
            "windows": f"openbfme-{version}-windows-x64"}[platform]


def expected_files(platform: str, version: str) -> set[str]:
    """exactly what the workflow's job uploads (the macOS set is the one autorelease.sh checks for a host build too)"""
    if platform == "macos":
        s = stem(platform, version)
        return {f"{s}.zip", f"{s}.zip.sha256", f"{s}.inventory.txt", "README-MAC.txt"}
    if platform == "linux":
        return {"openbfme.linux.template_debug.x86_64.so"}
    return {"openbfme.windows.template_debug.x86_64.dll"}


def job_name(platform: str) -> str:
    return {"macos": "macOS arm64 package", "linux": "Linux GDExtension (Godot's Linux SDK)",
            "windows": "Windows GDExtension (llvm-mingw cross build)"}[platform]


class Ctx:
    def __init__(self, a: argparse.Namespace):
        self.git_dir, self.repo, self.remote, self.gh = a.git_dir, a.repo, a.remote, a.gh
        self.commit, self.version = a.commit, a.version
        if not SHA.fullmatch(self.commit):
            fail(f"{self.commit!r} is not a full commit sha")
        if not VERSION.fullmatch(self.version):
            fail(f"{self.version!r} is not a release version")
        if not REPO_NAME.fullmatch(self.repo):
            fail(f"{self.repo!r} is not an <owner>/<name>")
        self.branch = f"staging/{self.version}"
        self.ref = f"refs/heads/{self.branch}"

    def git(self, *args: str, check: bool = True) -> subprocess.CompletedProcess:
        r = subprocess.run(["git", "-C", self.git_dir, *args], capture_output=True, text=True)
        if check and r.returncode:
            fail(f"git {args[0]} failed: {r.stderr.strip()[-600:]}")
        return r

    def api(self, *args: str) -> str:
        r = subprocess.run([self.gh, "api", *args], capture_output=True, text=True)
        if r.returncode:
            fail(f"gh api {args[-1] if args else ''} failed: {r.stderr.strip()[-600:]}")
        return r.stdout

    def json_api(self, *args: str):
        out = self.api(*args)
        try:
            return json.loads(out)
        except ValueError:
            fail(f"gh api {args[-1]} did not answer JSON")

    def remote_value(self) -> str:
        r = self.git("ls-remote", "--exit-code", self.remote, self.ref, check=False)
        if r.returncode == 2:
            return ""
        if r.returncode:
            fail(f"cannot read {self.ref} of {self.remote} (fail closed): {r.stderr.strip()[-300:]}")
        vals = {line.split("\t")[0] for line in r.stdout.splitlines() if line.endswith("\t" + self.ref)}
        if len(vals) != 1 or not SHA.fullmatch(next(iter(vals))):
            fail(f"an unexpected answer for {self.ref} of {self.remote}")
        return vals.pop()

    def push(self, src: str, lease: str) -> None:
        # --no-follow-tags (Sol r1): a push.followTags setting would otherwise carry the candidate's local tag along, before step P
        self.git("-c", "push.followTags=false", "push", "--quiet", "--no-follow-tags", f"--force-with-lease={self.ref}:{lease}", self.remote,
                 f"{src}:{self.ref}")


def stage(c: Ctx) -> int:
    c.git("cat-file", "-e", f"{c.commit}^{{commit}}")
    if c.git("cat-file", "-e", f"{c.commit}:{WORKFLOW}", check=False).returncode:
        fail(f"the public commit {c.commit} has no {WORKFLOW}: GitHub cannot build it")
    old = c.remote_value()
    if old == c.commit:
        print(f"ACTIONS staging: {c.ref} is already {c.commit}")
        return 0
    if old:
        print(f"ACTIONS staging: replacing a leftover {c.ref} ({old}, an earlier run's) with the candidate {c.commit}")
    c.push(c.commit, old)
    if c.remote_value() != c.commit:
        fail(f"{c.ref} of {c.remote} is not {c.commit} after the push")
    print(f"ACTIONS staging: {c.ref} = {c.commit} on {c.remote}")
    return 0


def unstage(c: Ctx) -> int:
    old = c.remote_value()
    if not old:
        print(f"ACTIONS staging: {c.ref} is not on {c.remote} (nothing to delete)")
        return 0
    if old != c.commit:
        fail(f"{c.ref} of {c.remote} is {old}, not the candidate {c.commit}: someone moved it; it is left for the operator")
    c.push("", c.commit)
    if c.remote_value():
        fail(f"{c.ref} is still on {c.remote} after the delete")
    print(f"ACTIONS staging: deleted {c.ref}")
    return 0


def load_record(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        rec = json.loads(path.read_text())
    except ValueError:
        fail(f"{path} is not a JSON record")
    if not isinstance(rec, dict):
        fail(f"{path} is not a JSON record")
    return rec


def write_record(record: Path, rec: dict) -> None:
    tmp = record.with_name(record.name + ".tmp")
    tmp.write_text(json.dumps(rec, indent=1, sort_keys=True) + "\n")
    tmp.replace(record)


def title(platform: str, version: str, request: str) -> str:
    return f"Release package {platform} {version} (request {request})"


def dispatch(c: Ctx, platform: str, record: Path) -> int:
    rec = load_record(record)
    if platform in rec:
        fail(f"{platform} was already dispatched for this release ({rec[platform].get('url')})")
    if c.remote_value() != c.commit:
        fail(f"{c.ref} of {c.remote} is not the candidate {c.commit} (stage first)")
    request = secrets.token_hex(16)
    # recorded before the dispatch (Sol r1): a lost answer or a killed run still leaves the request, which `cancel` finds by its title
    rec[platform] = {"run_id": None, "request": request, "commit": c.commit, "version": c.version, "url": None}
    write_record(record, rec)
    out = c.json_api("--method", "POST", f"repos/{c.repo}/actions/workflows/{os.path.basename(WORKFLOW)}/dispatches",
                     "-f", f"ref={c.branch}", "-f", f"inputs[platform]={platform}", "-f", f"inputs[commit]={c.commit}",
                     "-f", f"inputs[version]={c.version}", "-f", f"inputs[request]={request}", "-F", "return_run_details=true")
    run_id = out.get("workflow_run_id") if isinstance(out, dict) else None
    if not isinstance(run_id, int) or run_id <= 0:
        fail("GitHub did not return the dispatched run's id (return_run_details)")
    url = out.get("html_url") or f"https://github.com/{c.repo}/actions/runs/{run_id}"
    rec[platform] = {"run_id": run_id, "request": request, "commit": c.commit, "version": c.version, "url": url}
    write_record(record, rec)
    print(f"ACTIONS dispatched {platform} {c.version}: {url}")
    return 0


def cancel(c: Ctx, record: Path) -> int:
    for platform, rec in sorted(load_record(record).items()):
        if not rec.get("run_id"):
            # a dispatch whose answer never came: its run, if GitHub made one, is found by the request id in its title
            r = subprocess.run([c.gh, "api", f"repos/{c.repo}/actions/workflows/{os.path.basename(WORKFLOW)}/runs?branch={c.branch}"
                                f"&event=workflow_dispatch&per_page=100"], capture_output=True, text=True)
            try:
                runs = json.loads(r.stdout).get("workflow_runs") or [] if r.returncode == 0 else []
            except ValueError:
                runs = []
            ids = [x.get("id") for x in runs if x.get("display_title") == title(platform, c.version, rec.get("request", ""))]
            if not ids:
                print(f"ACTIONS {platform}: the dispatch was not answered and no run of its request is visible (nothing to cancel)")
            rec = {**rec, "run_id": ids[0] if ids else None, "url": f"run {ids[0]}" if ids else None}
            if not ids:
                continue
        r = subprocess.run([c.gh, "api", f"repos/{c.repo}/actions/runs/{rec.get('run_id')}", "--jq", ".status"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip() == "completed":
            continue
        r = subprocess.run([c.gh, "api", "--method", "POST", f"repos/{c.repo}/actions/runs/{rec.get('run_id')}/cancel"], capture_output=True, text=True)
        print(f"ACTIONS {platform} run {rec.get('url')}: {'cancelled' if r.returncode == 0 else 'could not be cancelled (it ends on its own)'}")
    return 0


def safe_extract(archive: Path, want: set[str], out: Path) -> dict[str, str]:
    """the artifact's files into `out`: exactly `want`, plain regular files; their SHA-256"""
    try:
        z = zipfile.ZipFile(archive)
    except zipfile.BadZipFile:
        fail("the artifact is not a ZIP archive")
    with z:
        infos = z.infolist()
        names = [i.filename for i in infos]
        if len(names) != len(set(names)):
            fail("the artifact names a file twice")
        if set(names) != want:
            missing, extra = sorted(want - set(names)), sorted(set(names) - want)
            fail(f"the artifact is not exactly the package files (missing {missing}, extra {extra})")
        digests = {}
        for i in infos:
            mode = (i.external_attr >> 16) & 0o170000
            if "/" in i.filename or "\\" in i.filename or i.is_dir() or (mode and mode != 0o100000):
                fail(f"the artifact entry {i.filename!r} is not a plain regular file")
            data = z.read(i)
            (out / i.filename).write_bytes(data)
            digests[i.filename] = hashlib.sha256(data).hexdigest()
    return digests


def collect(c: Ctx, platform: str, record: Path, out: Path, timeout: int) -> int:
    rec = load_record(record).get(platform)
    if not rec:
        fail(f"no dispatched {platform} run in {record}")
    if rec.get("commit") != c.commit or rec.get("version") != c.version:
        fail(f"the recorded {platform} run is for {rec.get('version')} at {rec.get('commit')}, not {c.version} at {c.commit}")
    run_id, request, url = rec["run_id"], rec["request"], rec["url"]
    if not isinstance(run_id, int):
        fail(f"the {platform} dispatch was never answered with its run (see the dispatch's failure)")
    if not out.is_dir() or any(out.iterdir()):
        fail(f"{out} must be an empty folder")
    login = c.api("user", "--jq", ".login").strip()
    if not login:
        fail("cannot read the release account (gh api user)")
    print(f"ACTIONS waiting for the {platform} build {url} (at most {timeout} s)")
    deadline = time.monotonic() + timeout
    seen = 0
    while True:
        r = subprocess.run([c.gh, "api", f"repos/{c.repo}/actions/runs/{run_id}"], capture_output=True, text=True)
        if r.returncode == 0:
            try:
                run = json.loads(r.stdout)
            except ValueError:
                fail(f"gh api for the run {run_id} did not answer JSON")
            if run.get("status") == "completed":
                break
        elif seen >= FIND:
            fail(f"the run {run_id} cannot be read: {r.stderr.strip()[-300:]}")
        else:
            seen += 1   # a just-dispatched run can take a moment to become readable
        if time.monotonic() >= deadline:
            subprocess.run([c.gh, "api", "--method", "POST", f"repos/{c.repo}/actions/runs/{run_id}/cancel"], capture_output=True)
            fail(f"the {platform} build {url} did not finish within {timeout} s (cancelled): the release fails, nothing was published")
        time.sleep(POLL)
    want_title = title(platform, c.version, request)
    checks = [
        (run.get("id") == run_id, f"the run is {run.get('id')}, not the dispatched {run_id}"),
        (run.get("event") == "workflow_dispatch", f"the run's event is {run.get('event')}"),
        (run.get("path") == WORKFLOW, f"the run's workflow is {run.get('path')}, not {WORKFLOW}"),
        (run.get("head_sha") == c.commit, f"the run built {run.get('head_sha')}, not the candidate {c.commit}"),
        (run.get("head_branch") == c.branch, f"the run is on {run.get('head_branch')}, not {c.branch}"),
        (((run.get("actor") or {}).get("login")) == login, f"the run's actor is not the release account {login}"),
        (((run.get("triggering_actor") or {}).get("login")) == login, f"the run was triggered by someone else than {login}"),
        (run.get("run_attempt") == 1, f"the run is attempt {run.get('run_attempt')} (a re-run), not the first"),
        (run.get("display_title") == want_title, f"the run's title is {run.get('display_title')!r}, not {want_title!r}"),
    ]
    for ok, why in checks:
        if not ok:
            fail(f"the {platform} run {url} is refused: {why}")
    if run.get("conclusion") != "success":
        fail(f"the {platform} build {url} concluded {run.get('conclusion')}: the release fails, nothing was published")
    jobs = c.json_api(f"repos/{c.repo}/actions/runs/{run_id}/jobs?per_page=100").get("jobs") or []
    want_job = job_name(platform)
    states = {j.get("name"): j.get("conclusion") for j in jobs}
    if len(states) != len(jobs) or states.get(want_job) != "success" or any(
            v != "skipped" for k, v in states.items() if k != want_job):
        fail(f"the {platform} run {url} jobs are {states}: only '{want_job}' may run and it must succeed")
    arts = c.json_api(f"repos/{c.repo}/actions/runs/{run_id}/artifacts?per_page=100")
    arts = arts.get("artifacts") if isinstance(arts, dict) else None
    name = f"{stem(platform, c.version)}-{request}"
    if not isinstance(arts, list) or len(arts) != 1:
        fail(f"the {platform} run {url} has {len(arts) if isinstance(arts, list) else 'no list of'} artifacts, not exactly one")
    art = arts[0]
    wr = art.get("workflow_run") or {}
    digest = str(art.get("digest") or "")
    if art.get("name") != name or art.get("expired") or wr.get("id") != run_id or wr.get("head_sha") != c.commit:
        fail(f"the {platform} run's artifact {art.get('name')!r} is not {name!r} of this run at {c.commit} (or it expired)")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        fail(f"the artifact {name} carries no SHA-256 digest")
    size = art.get("size_in_bytes")
    with tempfile.TemporaryDirectory(prefix=".actions-", dir=out.parent) as t:
        zpath = Path(t) / "artifact.zip"
        with zpath.open("wb") as f:
            r = subprocess.run([c.gh, "api", f"repos/{c.repo}/actions/artifacts/{art.get('id')}/zip"], stdout=f, stderr=subprocess.PIPE)
        if r.returncode:
            fail(f"downloading the artifact {name} failed: {r.stderr.decode(errors='replace').strip()[-300:]}")
        h = hashlib.sha256()
        with zpath.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        got, length = h.hexdigest(), zpath.stat().st_size
        if f"sha256:{got}" != digest:
            fail(f"the downloaded {name} has the SHA-256 {got}, not the artifact's digest {digest[7:]}: refused")
        if size is not None and size != length:
            fail(f"the downloaded {name} has {length} bytes, not the artifact's {size}")
        digests = safe_extract(zpath, expected_files(platform, c.version), out)
    for f, d in sorted(digests.items()):
        print(f"ACTIONS {platform} {f} sha256 {d}")
    print(f"ACTIONS BUILD OK {platform} {c.version} {c.commit} {url} (artifact {name}, {digest})")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir", required=True)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--remote", required=True)
    ap.add_argument("--gh", default=os.environ.get("GH", "gh"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("stage", "dispatch", "collect", "unstage", "cancel", "urls"):
        p = sub.add_parser(name)
        p.add_argument("--commit", required=True)
        p.add_argument("--version", required=True)
        if name in ("dispatch", "collect"):
            p.add_argument("--platform", required=True, choices=PLATFORMS)
        if name in ("dispatch", "collect", "cancel", "urls"):
            p.add_argument("--record", required=True, type=Path)
        if name == "collect":
            p.add_argument("--out", required=True, type=Path)
            p.add_argument("--timeout", type=int, default=int(os.environ.get("OPENBFME_RELEASE_ACTIONS_TIMEOUT", "7200")))
    a = ap.parse_args(argv)
    c = Ctx(a)
    if a.cmd == "stage":
        return stage(c)
    if a.cmd == "unstage":
        return unstage(c)
    if a.cmd == "dispatch":
        return dispatch(c, a.platform, a.record)
    if a.cmd == "cancel":
        return cancel(c, a.record)
    if a.cmd == "urls":
        print(", ".join(f"{p} {r.get('url')}" for p, r in sorted(load_record(a.record).items())))
        return 0
    if a.timeout <= 0:
        fail("--timeout must be a positive number of seconds")
    return collect(c, a.platform, a.record, a.out, a.timeout)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
