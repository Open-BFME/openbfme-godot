#!/usr/bin/env python3
"""Lane AUTOREL-1 r2: a stand-in for the `gh` commands autorelease.sh and publish_release.sh run, for tests (never the real GitHub).

Its state is a JSON file ($FAKE_GH_STATE: PRs and releases; asset bytes beside it in <state>.d/), the "GitHub repository" a local bare
repository ($FAKE_GH_REMOTE: PR merges are merge commits there, tags are read there). Every call is appended to $FAKE_GH_LOG. Faults
($FAKE_GH_FAULT, comma separated):
  create-lost     the draft is created but the call fails (a lost answer)
  upload-corrupt  the first asset is stored with another digest
  upload-nodigest assets carry no digest (the reader must download them)
  publish-fail    the publish call fails and changes nothing
  publish-lost    the release is published but the call fails (a lost answer)
  merge-lost      the PR is merged but the call fails
  delete-fail     deleting a release fails (it stays)
  final-readback-fail  reading a published release by its tag fails (autorelease.sh's last read-back)
  upload-fail     uploading assets fails (nothing is stored)
  publish-after-read  someone publishes a draft right after it was read by its id (Sol r3: the race between a read and a delete)
  homepage-fail   setting the repository's homepage fails (it stays)
  tag-read-fail   (RELCAND-1) reading a commit / tag (repos/R/commits/<ref>) fails: publish_release.sh stops before it creates a draft, so
                  a run fails after its push with the tag on the remote and no release
  community-merge-lost     (GHPR-1) a community pull request is merged but the call fails
  community-merge-refused  (GHPR-1) merging a community pull request fails and changes nothing
  pull-read-fail  (GHPR-1) reading a pull request fails
  community-push-before-merge  (GHPR-1) the author pushes to the pull request just before the merge call: the merge, which names the
                  reviewed head (--match-head-commit), is refused
Community pull requests (GHPR-1): $FAKE_GH_STATE's "pulls" ({number: {head, login, state, merged, mergeable, draft, base, repo,
merge_commit_sha}}), their heads refs/pull/<n>/head of the remote; `api repos/R/pulls/<n>` answers GitHub's record of one,
`pr merge <n> --merge --match-head-commit <sha>` merges it into main with a real merge commit (a detached worktree; a conflict fails).
GitHub Actions (MACACT-1): .github/workflows/release-package.yml dispatched on a branch of the remote runs $FAKE_GH_ACTIONS_BUILD
<platform> <checkout of the branch's head> <version> <out dir> at once (the run is completed when the dispatch returns); its files become
the run's one artifact (a ZIP, its SHA-256 the artifact's digest). Faults: actions-workflow-missing (GitHub does not know the workflow),
actions-dispatch-fail, actions-dispatch-lost (the run is made but the answer is lost), actions-no-run-id (the dispatch answers without the run's id), actions-run-fail (the build fails), actions-hang (the
run never finishes), actions-wrong-sha (the run built another commit), actions-wrong-actor, actions-rerun (attempt 2), actions-wrong-title,
actions-two-artifacts, actions-wrong-artifact-name, actions-digest-mismatch (the downloaded bytes are not the recorded digest's),
actions-partial (a file is missing), actions-extra-file (one more file), actions-folder-entry (a sub-folder entry), actions-job-extra (another
job ran too). The runs are in $FAKE_GH_STATE's "runs", the artifacts in "artifacts" (bytes beside it in <state>.d/artifact-<id>.zip).
SIGNKMS-1 (tools/release/check_draft.py): releases carry "author" ({"login": $FAKE_GH_AUTHOR, default tester}) and "created_at"
(a counter-based time stamp); `api repos/R/compare/<base>...<head>` answers GitHub's status / merge_base_commit / base_commit from the
remote's history; `api --method POST https://uploads.github.com/repos/R/releases/<id>/assets?name=N --input FILE` uploads one asset to
exactly that release (an existing name is refused like GitHub's 422; the upload-* faults apply). Fault cosign-upload-lost: the asset is
stored but the call fails.
RELGH-2: a release that is published (created or patched non-draft) gets "published_at" (now, or $FAKE_GH_PUBLISHED_AT).
RELGH-2: `api repos/R/actions/workflows/release.yml` answers {path, state: active}; faults release-workflow-missing (404) and
release-workflow-disabled (state disabled_manually).
SIGNKMS-1 (tools/release/actions_release.py dispatch / wait): `workflow run <file> --repo R --ref <ref> -f k=v ...` records the dispatch
and, with $FAKE_GH_WORKFLOW set, runs that program as the workflow (synchronously; IN_KIND / IN_TAG / IN_COMMIT / IN_REQUEST, GH_REF and
FAKE_GH_STATE in its environment: it adds its run to the state's "release_runs" and may create a draft); `api repos/R/actions/workflows/release.yml/runs`
answers {"workflow_runs": the state's "release_runs", newest first} (RELSIGN-1: apart from MACACT-1's "runs" of release-package.yml).
RELGH-3 (tools/release/stable_environment.py): deployment environments in the state's "environments" ({name: {"record": GitHub's GET
record, "branch_policies": [{id, name, type}]}}): `api repos/R/environments/<name>` (GET; PUT --input - with GitHub's body: reviewers by
id, prevent_self_review, can_admins_bypass, deployment_branch_policy), `.../deployment-branch-policies` (GET, POST -f name -f type,
DELETE .../<id>); `api users/<login>` ({login, id}: ids from the state's "users", else a stable number); `api user` is $FAKE_GH_USER
(default tester); `api repos/R/actions/runs/<id>/approvals` answers the state's "approvals"[<id>] (default []). Fault env-read-fail: reading
an environment fails.
Commands: api (user, repos/R GET / PATCH (homepage), repos/R/commits/<ref>, repos/R/compare/<a>...<b>, repos/R/releases[?..], repos/R/releases/<id> GET / PATCH / DELETE, repos/R/releases/tags/<tag>,
repos/R/releases/assets/<id>), pr create / merge / view, release upload. --jq supports the expressions these scripts use.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

STATE = Path(os.environ["FAKE_GH_STATE"])
REMOTE = os.environ.get("FAKE_GH_REMOTE", "")
FAULTS = set(filter(None, os.environ.get("FAKE_GH_FAULT", "").split(",")))
ENV = {**os.environ, "GIT_AUTHOR_NAME": "github", "GIT_AUTHOR_EMAIL": "noreply@example.invalid", "GIT_COMMITTER_NAME": "github",
       "GIT_COMMITTER_EMAIL": "noreply@example.invalid"}


def load() -> dict:
    return json.loads(STATE.read_text()) if STATE.exists() else {"prs": {}, "releases": [], "next": 100}


def save(s: dict) -> None:
    STATE.write_text(json.dumps(s, indent=1))


def stamp() -> str:
    """RELGH-2: a release's published_at (GitHub's UTC form); $FAKE_GH_PUBLISHED_AT fixes it (tests of the transition rule's age window)"""
    import time
    return os.environ.get("FAKE_GH_PUBLISHED_AT") or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def remote_git(*args: str) -> str:
    return subprocess.run(["git", "-C", REMOTE, *args], capture_output=True, text=True, check=True, env=ENV).stdout.strip()


def jq(value, expr: str | None) -> str:
    if expr is None:
        return json.dumps(value)
    m = re.fullmatch(r'\.\[\] \| select\(\.tag_name == "([^"]*)"\) \| \.id', expr)
    if m:
        return "\n".join(str(r["id"]) for r in value if r["tag_name"] == m.group(1))
    if expr == '"\\(.login) \\(.id)"':
        return f"{value['login']} {value['id']}"
    for key in filter(None, expr.split(".")):
        value = None if value is None else value.get(key)
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    return value if isinstance(value, str) else json.dumps(value)


def out(text: str) -> None:
    if text:
        sys.stdout.write(text + "\n")


def api(args: list[str]) -> int:
    method, fields, expr, endpoint, accept, infile = "GET", {}, None, None, "", None
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--method", "-X"):
            method = args[i + 1]; i += 2
        elif a in ("-f", "--raw-field"):
            k, _, v = args[i + 1].partition("="); fields[k] = v; i += 2
        elif a in ("-F", "--field"):
            k, _, v = args[i + 1].partition("=")
            if v.startswith("@"):
                v = Path(v[1:]).read_text()
            elif v in ("true", "false"):
                v = v == "true"
            elif v.isdigit():
                v = int(v)
            fields[k] = v; i += 2
        elif a in ("--jq", "-q"):
            expr = args[i + 1]; i += 2
        elif a in ("-H", "--header"):
            accept = args[i + 1]; i += 2
        elif a == "--paginate":
            i += 1
        elif a == "--input":
            infile = args[i + 1]; i += 2
        else:
            endpoint = a; i += 1
    s = load()
    path = endpoint.split("?")[0]
    m = re.fullmatch(r"https://uploads\.github\.com/repos/[^/]+/[^/]+/releases/(\d+)/assets", path)
    if m and method == "POST":
        return upload_one(s, int(m.group(1)), dict(kv.partition("=")[::2] for kv in endpoint.partition("?")[2].split("&") if kv).get("name", ""),
                          infile, expr)
    m = re.fullmatch(r"repos/[^/]+/[^/]+/compare/([^.]+)\.\.\.(.+)", path)
    if m:
        def rev(ref):
            r = subprocess.run(["git", "-C", REMOTE, "rev-parse", "-q", "--verify", ref + "^{commit}"], capture_output=True, text=True)
            return r.stdout.strip() if r.returncode == 0 else ""
        base, head = rev(m.group(1)), rev(m.group(2))
        if not base or not head:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        mb = subprocess.run(["git", "-C", REMOTE, "merge-base", base, head], capture_output=True, text=True).stdout.strip()
        status = "identical" if base == head else "behind" if mb == head else "ahead" if mb == base else "diverged"
        out(jq({"status": status, "base_commit": {"sha": base}, "merge_base_commit": {"sha": mb}}, expr)); return 0
    if re.fullmatch(r"repos/[^/]+/[^/]+/actions/workflows/release\.yml/runs", path):
        out(jq({"workflow_runs": s.get("release_runs", [])[::-1]}, expr)); return 0
    if re.fullmatch(r"repos/[^/]+/[^/]+/actions/workflows/release\.yml", path) and method == "GET":
        # RELGH-2: autorelease.sh (signer actions) asks that GitHub knows the release workflow and that it is active
        if "release-workflow-missing" in FAULTS:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        state = "disabled_manually" if "release-workflow-disabled" in FAULTS else "active"
        out(jq({"path": ".github/workflows/release.yml", "state": state}, expr)); return 0
    if path == "user":
        out(jq({"login": os.environ.get("FAKE_GH_USER", "tester"), "id": 42}, expr)); return 0
    m = re.fullmatch(r"users/([A-Za-z0-9-]+)", path)
    if m and method == "GET":
        users = s.get("users", {})
        uid = users.get(m.group(1), 1000 + int(hashlib.sha256(m.group(1).lower().encode()).hexdigest()[:6], 16))
        out(jq({"login": m.group(1), "id": uid, "type": "User"}, expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/environments/([a-z0-9-]+)(/deployment-branch-policies(?:/(\d+))?)?", path)
    if m:
        return environments(s, method, m.group(1), m.group(2), m.group(3), fields, infile, expr)
    m = re.fullmatch(r"repos/[^/]+/[^/]+/actions/runs/(\d+)/approvals", path)
    if m and method == "GET":
        out(jq(s.get("approvals", {}).get(m.group(1), []), expr)); return 0
    if "/actions/" in path:
        return actions(s, method, path, fields, expr)
    m = re.fullmatch(r"(repos/[^/]+/[^/]+)/pulls/(\d+)", path)
    if m and method == "GET":
        p = s.get("pulls", {}).get(m.group(2))
        if p is None or "pull-read-fail" in FAULTS:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        out(jq(pull_record(m.group(1), p), expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/commits/(.+)", path)
    if m:
        if "tag-read-fail" in FAULTS:
            print("gh: HTTP 502", file=sys.stderr); return 1
        r = subprocess.run(["git", "-C", REMOTE, "rev-parse", "-q", "--verify", m.group(1) + "^{commit}"], capture_output=True, text=True)
        if r.returncode:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        out(jq({"sha": r.stdout.strip()}, expr)); return 0
    if re.fullmatch(r"repos/[^/]+/[^/]+", path):  # RELTEST-1: the repository (its homepage, the About link)
        repo = s.setdefault("repo", {"homepage": ""})
        if method == "PATCH":
            if "homepage-fail" in FAULTS:
                print("gh: HTTP 502", file=sys.stderr); return 1
            repo.update(fields); save(s)
        out(jq(repo, expr)); return 0
    if re.fullmatch(r"repos/[^/]+/[^/]+/releases", path):
        if method == "GET":  # RELNAME-1: GitHub's order, the newest first, one page (?per_page=, default 30; ?page=)
            q = dict(kv.partition("=")[::2] for kv in endpoint.partition("?")[2].split("&") if kv)
            n, page = int(q.get("per_page", 30)), int(q.get("page", 1))
            out(jq(s["releases"][::-1][(page - 1) * n:page * n], expr)); return 0
        rel = {"id": s["next"], "tag_name": fields["tag_name"], "name": fields.get("name"), "draft": bool(fields.get("draft")),
               "prerelease": bool(fields.get("prerelease")), "body": fields.get("body", ""), "assets": [],
               "author": {"login": os.environ.get("FAKE_GH_AUTHOR", "tester")}, "created_at": f"2026-10-22T00:00:{s['next'] % 60:02d}Z"}
        s["next"] += 1
        if not rel["draft"]:
            rel["published_at"] = stamp()
        s["releases"].append(rel); save(s)
        if "create-lost" in FAULTS:
            print("gh: connection reset", file=sys.stderr); return 1
        out(jq(rel, expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/releases/tags/(.+)", path)
    if m:
        if "final-readback-fail" in FAULTS:
            print("gh: connection reset", file=sys.stderr); return 1
        rel = [r for r in s["releases"] if r["tag_name"] == m.group(1) and not r["draft"]]
        if not rel:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        out(jq(rel[0], expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/releases/assets/(\d+)", path)
    if m:
        sys.stdout.buffer.write((Path(str(STATE) + ".d") / m.group(1)).read_bytes()); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/releases/(\d+)", path)
    if m:
        rel = [r for r in s["releases"] if r["id"] == int(m.group(1))]
        if not rel:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        rel = rel[0]
        if method == "DELETE":
            if "delete-fail" in FAULTS:
                print("gh: HTTP 502", file=sys.stderr); return 1
            s["releases"].remove(rel); save(s); return 0
        if method == "GET" and rel["draft"] and "publish-after-read" in FAULTS:
            out(jq(rel, expr))
            rel["draft"] = False; rel.setdefault("published_at", stamp()); save(s); return 0
        if method == "PATCH":
            if "publish-fail" in FAULTS:
                print("gh: HTTP 502", file=sys.stderr); return 1
            rel.update({k: v for k, v in fields.items()})
            if not rel["draft"]:
                rel.setdefault("published_at", stamp())
            save(s)
            if "publish-lost" in FAULTS:
                print("gh: connection reset", file=sys.stderr); return 1
        out(jq(rel, expr)); return 0
    print(f"gh stand-in: unknown endpoint {endpoint}", file=sys.stderr)
    return 2


WORKFLOW = ".github/workflows/release-package.yml"
JOBS = {"macos": "macOS arm64 package", "linux": "Linux GDExtension (Godot's Linux SDK)", "windows": "Windows GDExtension (llvm-mingw cross build)"}
STEMS = {"macos": "macos-arm64", "linux": "linux-x64", "windows": "windows-x64"}


def actions(s: dict, method: str, path: str, fields: dict, expr: str | None) -> int:
    """MACACT-1: the GitHub Actions endpoints actions_build.py and autorelease.sh use"""
    m = re.fullmatch(r"repos/([^/]+/[^/]+)/actions/workflows/release-package\.yml", path)
    if m and method == "GET":
        if "actions-workflow-missing" in FAULTS:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        out(jq({"path": WORKFLOW, "state": "active"}, expr)); return 0
    m = re.fullmatch(r"repos/([^/]+/[^/]+)/actions/workflows/release-package\.yml/dispatches", path)
    if m and method == "POST":
        if "actions-dispatch-fail" in FAULTS:
            print("gh: HTTP 502", file=sys.stderr); return 1
        return dispatch_run(s, m.group(1), fields, expr)
    m = re.fullmatch(r"repos/([^/]+/[^/]+)/actions/workflows/release-package\.yml/runs", path)
    if m and method == "GET":
        runs = sorted(s.get("runs", {}).values(), key=lambda r: -r["id"])
        out(jq({"total_count": len(runs), "workflow_runs": runs}, expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/actions/runs/(\d+)(/[a-z]+)?", path)
    if m:
        run = s.get("runs", {}).get(m.group(1))
        if run is None:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        what = m.group(2)
        if what is None:
            out(jq(run, expr)); return 0
        if what == "/cancel" and method == "POST":
            if run["status"] != "completed":
                run.update(status="completed", conclusion="cancelled"); save(s)
            return 0
        if what == "/jobs":
            jobs = [{"name": JOBS[p], "status": "completed",
                     "conclusion": run["conclusion"] if p == run["_platform"] else ("success" if "actions-job-extra" in FAULTS else "skipped")}
                    for p in JOBS]
            out(jq({"total_count": len(jobs), "jobs": jobs}, expr)); return 0
        if what == "/artifacts":
            arts = [a for a in s.get("artifacts", {}).values() if a["workflow_run"]["id"] == run["id"]]
            out(jq({"total_count": len(arts), "artifacts": arts}, expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/actions/artifacts/(\d+)/zip", path)
    if m and m.group(1) in s.get("artifacts", {}):
        sys.stdout.buffer.write((Path(str(STATE) + ".d") / f"artifact-{m.group(1)}.zip").read_bytes()); return 0
    print(f"gh stand-in: unknown endpoint {path}", file=sys.stderr)
    return 2


def environments(s: dict, method: str, name: str, sub: str | None, pid: str | None, fields: dict, infile: str | None, expr) -> int:
    """RELGH-3: a deployment environment, its record and branch policies"""
    envs = s.setdefault("environments", {})
    if "env-read-fail" in FAULTS and method == "GET":
        print("gh: HTTP 502", file=sys.stderr); return 1
    e = envs.get(name)
    if sub is None and method == "PUT":
        body = json.loads(sys.stdin.read() if infile in (None, "-") else Path(infile).read_text())
        ids = {v: k for k, v in s.get("users", {}).items()}
        def login(i):
            return ids.get(i) or next((u for u in ("Ancalgonn", "tester", "release-bot")
                                       if 1000 + int(hashlib.sha256(u.lower().encode()).hexdigest()[:6], 16) == i), f"user{i}")
        rules = []
        if body.get("reviewers"):
            rules.append({"type": "required_reviewers", "prevent_self_review": bool(body.get("prevent_self_review")),
                          "reviewers": [{"type": r["type"], "reviewer": {"login": login(r["id"]), "id": r["id"]}} for r in body["reviewers"]]})
        if body.get("wait_timer"):
            rules.append({"type": "wait_timer", "wait_timer": body["wait_timer"]})
        dbp = body.get("deployment_branch_policy")
        if dbp:
            rules.append({"type": "branch_policy"})
        rec = {"name": name, "can_admins_bypass": body.get("can_admins_bypass", True), "deployment_branch_policy": dbp,
               "protection_rules": rules}
        envs[name] = {"record": rec, "branch_policies": (e or {}).get("branch_policies", [])}
        save(s); out(jq(rec, expr)); return 0
    if e is None:
        print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
    if sub is None:
        out(jq(e["record"], expr)); return 0
    bps = e.setdefault("branch_policies", [])
    if pid is None and method == "GET":
        out(jq({"total_count": len(bps), "branch_policies": bps}, expr)); return 0
    if pid is None and method == "POST":
        bp = {"id": 500 + len(bps) + sum(len(x.get("branch_policies", [])) for x in envs.values()), "name": fields.get("name"),
              "type": fields.get("type", "branch")}
        bps.append(bp); save(s); out(jq(bp, expr)); return 0
    if pid is not None and method == "DELETE":
        e["branch_policies"] = [b for b in bps if str(b.get("id")) != pid]; save(s); return 0
    print(f"gh stand-in: unknown environment call {method} {name}{sub}", file=sys.stderr)
    return 2


def dispatch_run(s: dict, repo: str, fields: dict, expr: str | None) -> int:
    import zipfile
    ref = fields["ref"]
    inputs = {k[7:-1]: v for k, v in fields.items() if k.startswith("inputs[")}
    platform, version, request = inputs["platform"], inputs["version"], inputs["request"]
    head = remote_git("rev-parse", f"refs/heads/{ref}")
    rid = s["next"]; s["next"] += 1
    run = {"id": rid, "event": "workflow_dispatch", "path": WORKFLOW, "head_branch": ref,
           "head_sha": "0" * 40 if "actions-wrong-sha" in FAULTS else head,
           "actor": {"login": "mallory" if "actions-wrong-actor" in FAULTS else os.environ.get("FAKE_GH_USER", "tester")}, "triggering_actor": {"login": os.environ.get("FAKE_GH_USER", "tester")},
           "run_attempt": 2 if "actions-rerun" in FAULTS else 1,
           "display_title": f"Release package {platform} {version} (request {'0' * 32 if 'actions-wrong-title' in FAULTS else request})",
           "status": "in_progress", "conclusion": None, "_platform": platform, "_inputs": inputs,
           "html_url": f"https://github.com/{repo}/actions/runs/{rid}"}
    s.setdefault("runs", {})[str(rid)] = run
    if "actions-hang" not in FAULTS:
        with tempfile.TemporaryDirectory(prefix="gh-actions-") as tmp:
            src, outd = Path(tmp, "src"), Path(tmp, "out")
            subprocess.run(["git", "clone", "-q", "--no-checkout", REMOTE, str(src)], check=True, capture_output=True, env=ENV)
            subprocess.run(["git", "-C", str(src), "checkout", "-q", "--detach", head], check=True, capture_output=True, env=ENV)
            outd.mkdir()
            ok = "actions-run-fail" not in FAULTS and subprocess.run(
                [os.environ["FAKE_GH_ACTIONS_BUILD"], platform, str(src), version, str(outd)]).returncode == 0
            run.update(status="completed", conclusion="success" if ok else "failure")
            if ok:
                files = sorted(outd.iterdir())
                if "actions-partial" in FAULTS:
                    files = files[1:]
                store = Path(str(STATE) + ".d"); store.mkdir(exist_ok=True)
                for extra in ([""] + (["-second"] if "actions-two-artifacts" in FAULTS else [])):
                    aid = s["next"]; s["next"] += 1
                    zpath = store / f"artifact-{aid}.zip"
                    with zipfile.ZipFile(zpath, "w") as z:
                        for f in files:
                            z.write(f, f.name)
                        if "actions-extra-file" in FAULTS:
                            z.writestr("notes.txt", "one more file\n")
                        if "actions-folder-entry" in FAULTS:
                            z.writestr("sub/", "")
                    data = zpath.read_bytes()
                    digest = "sha256:" + hashlib.sha256(data).hexdigest()
                    if "actions-digest-mismatch" in FAULTS:
                        zpath.write_bytes(data + b"substituted")
                    name = f"openbfme-{version}-{STEMS[platform]}-{request}{extra}"
                    if "actions-wrong-artifact-name" in FAULTS:
                        name = f"openbfme-{version}-{STEMS[platform]}"
                    s.setdefault("artifacts", {})[str(aid)] = {
                        "id": aid, "name": name, "size_in_bytes": len(data), "digest": digest, "expired": False,
                        "workflow_run": {"id": rid, "head_sha": run["head_sha"], "head_branch": ref}}
    save(s)
    if "actions-dispatch-lost" in FAULTS:
        print("gh: connection reset", file=sys.stderr); return 1
    if "actions-no-run-id" in FAULTS:
        out(jq({}, expr)); return 0
    out(jq({"workflow_run_id": rid, "run_url": f"https://api.github.com/repos/{repo}/actions/runs/{rid}", "html_url": run["html_url"]}, expr))
    return 0


def upload_one(s: dict, rid: int, name: str, infile: str | None, expr: str | None) -> int:
    """SIGNKMS-1: the upload API: one asset to exactly the release rid"""
    rel = [r for r in s["releases"] if r["id"] == rid]
    if not rel or not name or infile is None:
        print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
    if "upload-fail" in FAULTS:
        print("gh: upload failed", file=sys.stderr); return 1
    if any(a["name"] == name for a in rel[0]["assets"]):
        print("gh: Validation Failed: already_exists (HTTP 422)", file=sys.stderr); return 1
    data = Path(infile).read_bytes()
    store = Path(str(STATE) + ".d")
    store.mkdir(exist_ok=True)
    aid = s["next"]; s["next"] += 1
    digest = "sha256:" + hashlib.sha256(data).hexdigest()
    if "upload-corrupt" in FAULTS:
        digest = "sha256:" + "0" * 64
    if "upload-nodigest" in FAULTS:
        digest = None
    (store / str(aid)).write_bytes(data)
    asset = {"id": aid, "name": name, "size": len(data), "digest": digest, "state": "uploaded"}
    rel[0]["assets"].append(asset)
    save(s)
    if "cosign-upload-lost" in FAULTS:
        print("gh: connection reset", file=sys.stderr); return 1
    out(jq(asset, expr)); return 0


def pull_record(repo_path: str, p: dict) -> dict:
    """GitHub's record of a pull request (the fields community_prs.py reads)"""
    return {"number": p.get("answer_number", p["number"]), "state": p["state"], "merged": p["merged"], "mergeable": p.get("mergeable", True),
            "draft": p.get("draft", False), "merge_commit_sha": p.get("merge_commit_sha"), "user": {"login": p["login"]},
            "head": {"sha": p["head"]}, "base": {"ref": p.get("base", "main"), "repo": {"full_name": p.get("repo", repo_path[6:])}}}


def merge_pull(n: str, args: list[str], s: dict) -> int:
    p = s["pulls"][n]
    if "community-merge-refused" in FAULTS or p["state"] != "open" or p["merged"] or not p.get("mergeable", True):
        print("gh: Pull request is not mergeable", file=sys.stderr); return 1
    if "community-push-before-merge" in FAULTS:
        p["head"] = remote_git("commit-tree", f"{p['head']}^{{tree}}", "-p", p["head"], "-m", "a push after the review")
        remote_git("update-ref", f"refs/pull/{n}/head", p["head"])
        save(s)
    want = opt(args, "--match-head-commit")
    if want is not None and want != p["head"]:
        print("gh: Head branch was modified. Review and try the merge again.", file=sys.stderr); return 1
    if "--merge" not in args:
        print("gh stand-in: community pull requests are merged with --merge only", file=sys.stderr); return 2
    base = remote_git("rev-parse", "refs/heads/main")
    with tempfile.TemporaryDirectory(prefix="gh-merge-") as tmp:
        work = str(Path(tmp, "work"))
        remote_git("worktree", "add", "--quiet", "--detach", work, base)
        try:
            r = subprocess.run(["git", "-C", work, "merge", "--no-commit", "--no-ff", p["head"]],
                               capture_output=True, text=True, env=ENV)
            if r.returncode:
                print(f"gh: merge failed\n{r.stdout}{r.stderr}", file=sys.stderr); return 1
            tree = remote_git("-C", work, "write-tree")
        finally:
            remote_git("worktree", "remove", "--force", work)
    merged = remote_git("commit-tree", tree, "-p", base, "-p", p["head"], "-m", f"Merge pull request #{n} from {p['login']}/patch")
    remote_git("update-ref", "refs/heads/main", merged)
    p.update(state="closed", merged=True, merge_commit_sha=merged)
    save(s)
    return 1 if "community-merge-lost" in FAULTS else 0


def opt(args: list[str], name: str) -> str | None:
    return args[args.index(name) + 1] if name in args else None


def pr(args: list[str]) -> int:
    s = load()
    if args[0] == "create":
        n = str(len(s["prs"]) + 1)
        s["prs"][n] = {"head": opt(args, "--head"), "base": opt(args, "--base"), "title": opt(args, "--title"),
                       "body": Path(opt(args, "--body-file")).read_text(), "merge": None}
        save(s)
        out(f"https://github.com/{opt(args, '--repo')}/pull/{n}"); return 0
    if args[0] == "merge" and args[1] in s.get("pulls", {}):
        return merge_pull(args[1], args, s)
    if args[0] == "merge":
        p = s["prs"][args[1]]
        head = remote_git("rev-parse", f"refs/heads/{p['head']}")
        base = remote_git("rev-parse", f"refs/heads/{p['base']}")
        merged = remote_git("commit-tree", f"{head}^{{tree}}", "-p", base, "-p", head, "-m", opt(args, "--subject") or "merge")
        remote_git("update-ref", f"refs/heads/{p['base']}", merged)
        p["merge"] = merged; save(s)
        return 1 if "merge-lost" in FAULTS else 0
    if args[0] == "view":
        p = s["prs"][args[1]]
        out(jq({"mergeCommit": {"oid": p["merge"]} if p["merge"] else None}, opt(args, "-q") or opt(args, "--jq"))); return 0
    return 2


def release(args: list[str]) -> int:
    if args[0] != "upload":
        return 2
    s = load()
    tag = args[1]
    if "upload-fail" in FAULTS:
        print("gh: upload failed", file=sys.stderr); return 1
    files = [a for a in args[2:] if not a.startswith("--") and a != opt(args, "--repo")]
    rel = [r for r in s["releases"] if r["tag_name"] == tag]
    if len(rel) != 1:
        print(f"gh: release not found: {tag}", file=sys.stderr); return 1
    store = Path(str(STATE) + ".d")
    store.mkdir(exist_ok=True)
    for i, f in enumerate(files):
        data = Path(f).read_bytes()
        aid = s["next"]; s["next"] += 1
        digest = "sha256:" + hashlib.sha256(data).hexdigest()
        if "upload-corrupt" in FAULTS and i == 0:
            digest = "sha256:" + "0" * 64
        if "upload-nodigest" in FAULTS:
            digest = None
        (store / str(aid)).write_bytes(data)
        rel[0]["assets"].append({"id": aid, "name": Path(f).name, "size": len(data), "digest": digest, "state": "uploaded"})
    save(s)
    return 0


def workflow(args: list[str]) -> int:
    """SIGNKMS-1: `workflow run <file> --repo R --ref REF -f k=v ...`"""
    if args[:1] != ["run"] or len(args) < 2:
        return 2
    fields = dict(args[i + 1].partition("=")[::2] for i in range(len(args)) if args[i] == "-f")
    s = load()
    s.setdefault("dispatches", []).append({"workflow": args[1], "ref": opt(args, "--ref"), "inputs": fields})
    save(s)
    if "dispatch-fail" in FAULTS:
        print("gh: HTTP 502", file=sys.stderr); return 1
    hook = os.environ.get("FAKE_GH_WORKFLOW")
    if hook:
        env = {**os.environ, "GH_REF": opt(args, "--ref") or "", **{f"IN_{k.upper()}": v for k, v in fields.items()}}
        r = subprocess.run([hook], env=env)
        if r.returncode:
            print(f"gh stand-in: the workflow hook failed ({r.returncode})", file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    log = os.environ.get("FAKE_GH_LOG")
    if log:
        with open(log, "a") as f:
            f.write(" ".join(argv) + "\n")
    cmd = {"api": api, "pr": pr, "release": release, "workflow": workflow}.get(argv[0])
    return cmd(argv[1:]) if cmd else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
