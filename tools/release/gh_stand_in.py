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
Commands: api (user, repos/R/commits/<ref>, repos/R/releases[?..], repos/R/releases/<id> GET / PATCH / DELETE, repos/R/releases/tags/<tag>,
repos/R/releases/assets/<id>), pr create / merge / view, release upload. --jq supports the expressions these scripts use.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
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
    method, fields, expr, endpoint, accept = "GET", {}, None, None, ""
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
        else:
            endpoint = a; i += 1
    s = load()
    path = endpoint.split("?")[0]
    if path == "user":
        out(jq({"login": "tester", "id": 42}, expr)); return 0
    m = re.fullmatch(r"repos/[^/]+/[^/]+/commits/(.+)", path)
    if m:
        r = subprocess.run(["git", "-C", REMOTE, "rev-parse", "-q", "--verify", m.group(1) + "^{commit}"], capture_output=True, text=True)
        if r.returncode:
            print("gh: Not Found (HTTP 404)", file=sys.stderr); return 1
        out(jq({"sha": r.stdout.strip()}, expr)); return 0
    if re.fullmatch(r"repos/[^/]+/[^/]+/releases", path):
        if method == "GET":
            out(jq(s["releases"], expr)); return 0
        rel = {"id": s["next"], "tag_name": fields["tag_name"], "name": fields.get("name"), "draft": bool(fields.get("draft")),
               "prerelease": bool(fields.get("prerelease")), "body": fields.get("body", ""), "assets": []}
        s["next"] += 1
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
            rel["draft"] = False; save(s); return 0
        if method == "PATCH":
            if "publish-fail" in FAULTS:
                print("gh: HTTP 502", file=sys.stderr); return 1
            rel.update({k: v for k, v in fields.items()}); save(s)
            if "publish-lost" in FAULTS:
                print("gh: connection reset", file=sys.stderr); return 1
        out(jq(rel, expr)); return 0
    print(f"gh stand-in: unknown endpoint {endpoint}", file=sys.stderr)
    return 2


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


def main(argv: list[str]) -> int:
    log = os.environ.get("FAKE_GH_LOG")
    if log:
        with open(log, "a") as f:
            f.write(" ".join(argv) + "\n")
    cmd = {"api": api, "pr": pr, "release": release}.get(argv[0])
    return cmd(argv[1:]) if cmd else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
