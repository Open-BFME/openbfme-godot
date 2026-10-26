#!/usr/bin/env python3
"""Lane GHPR-1: the policy for the public repository's GitHub folder (.github/), checked on the tree a release syncs (autorelease.sh,
before anything is pushed; --plan too) and on every pull request (.github/workflows/pr.yml).

A community pull request can change .github/ like any other file, and a sync carries what the archive holds, so a workflow that could reach
a secret, write to the repository or run on a private machine must never get there. The rules (a hit is a failure; nothing is skipped):
  * .github/ holds only APPROVED files: the pull request template, workflows (.github/workflows/<name>.yml) and the workflow linter's
    zizmor.yml; anything else there
    (other templates, dependabot, funding, CODEOWNERS ...) is refused until it is reviewed and added to APPROVED;
  * a workflow parses as YAML (PyYAML's safe loader) into a mapping: GitHub refuses a file it cannot parse and runs none of its jobs
    (lane PUBLICCI-1: a plain `run:` value holding ": " made every run of pr.yml fail as a "workflow file issue" while the text rules
    below passed it); without PyYAML the check fails, it is never skipped;
  * a workflow runs on `pull_request` / `merge_group` / `push` / `workflow_dispatch` only: never `pull_request_target`, `workflow_run` or
    `issue_comment` (they run with the base repository's token and secrets on a contributor's code; `merge_group` runs the merge queue's
    commit of base-repository code, like `push`);
  * no `secrets.` reference at all (GITHUB_TOKEN included), no `id-token`, no `: write` permission anywhere, and a top-level
    `permissions:` block that is exactly `contents: read`;
  * every `uses:` names an action by its full 40-hex commit sha (a tag can be moved) or a local `./` path;
  * GitHub-hosted runners only (`runs-on: ubuntu-*`, `windows-*`, `macos-*`): a `self-hosted` label or any other runner name is refused (it
    would also name private machines).
The checks read the YAML as text, line by line, and are deliberately stricter than YAML: a construct they cannot read (a flow-style
`on: [..]` with a refused trigger, a `uses:` in a form they do not know) fails.
Lane SIGNKMS-1: ONE exception, the release workflow (.github/workflows/release.yml, RELEASE_WORKFLOW): the job that signs through AWS KMS
needs GitHub's OIDC token (`id-token: write`) and creates the draft release (`contents: write`). Those two lines are accepted in that file
alone and only when the parsed YAML has exactly this shape (release_workflow_problems): the one trigger workflow_dispatch; the top-level
permissions `contents: read`; exactly one job with a `permissions:` block, which is exactly {contents: write, id-token: write}, whose
`environment:` is nightly or stable, or `${{ inputs.kind }}` where the input kind is a choice of nightly / stable only (the environments'
protection rules and the AWS role's trust policy do the rest: docs/RELEASE.md, "Release signing"). The same job may name ONE secret, by
the exact line RELEASE_SECRET_LINE (the signing role's ARN: it names the AWS account, and GitHub masks a secret in the logs while a variable
is printed with the job's environment); no other secrets reference anywhere. Every other rule still applies to the file.
Lane RELSIGN-1: the release workflow builds with MACACT-1's jobs by CALLING .github/workflows/release-package.yml (REUSABLE_WORKFLOW), so
the build jobs have one definition. That file alone may add the trigger `workflow_call` (beside workflow_dispatch, nothing else), and it
declares no `secrets`; a job that calls a workflow (`jobs.<id>.uses`) is allowed in release.yml alone, must call exactly
./.github/workflows/release-package.yml and has no `permissions` or `secrets` of its own (it runs with the top-level contents: read). A
`secrets:` key is refused in every workflow (`secrets: inherit` would hand a called workflow every secret without a `secrets.` reference).

    python3 tools/release/public_workflows.py [--git-dir DIR] --tree TREE    (exit 1 and PUBLIC WORKFLOWS lines on a problem)
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys

# .github/zizmor.yml: the workflow linter's configuration (pr.yml runs zizmor on .github/workflows), reviewed 2026-10-24: lint rules only
APPROVED = (re.compile(r"\.github/pull_request_template\.md"), re.compile(r"\.github/workflows/[a-z0-9][a-z0-9_-]*\.yml"),
            re.compile(r"\.github/zizmor\.yml"))
REFUSED_TRIGGERS = ("pull_request_target", "workflow_run", "issue_comment")
ALLOWED_TRIGGERS = ("pull_request", "merge_group", "push", "workflow_dispatch")
USES = re.compile(r"^\s*(?:-\s+)?uses:\s*(\S+)\s*(?:#.*)?$")
PINNED = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+@[0-9a-f]{40}")
RUNS_ON = re.compile(r"^\s*runs-on:\s*(.*?)\s*(?:#.*)?$")
HOSTED = re.compile(r"(?:ubuntu|windows|macos)-[a-z0-9.-]+")
RELEASE_WORKFLOW = ".github/workflows/release.yml"
SIGN_ENVIRONMENTS = {"nightly", "stable"}
SIGN_PERMISSIONS = {"contents": "write", "id-token": "write"}
REUSABLE_WORKFLOW = ".github/workflows/release-package.yml"
SECRETS_KEY = re.compile(r"^\s*(?:-\s+)?[\"']?secrets[\"']?\s*:")
RELEASE_SECRET_LINE = "      SIGNING_ROLE_ARN: ${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}"


def yaml_problems(path: str, text: str) -> list[str]:
    """the file must load as a YAML mapping; PyYAML missing is a failure (fail closed), not a skip"""
    try:
        import yaml
    except ImportError:
        return [f"{path}: PyYAML is not installed, so the file cannot be checked to parse as YAML (pip install PyYAML)"]
    try:
        doc = yaml.safe_load(text)
    except yaml.YAMLError as e:
        mark = getattr(e, "problem_mark", None)
        where = f":{mark.line + 1}:{mark.column + 1}" if mark else ""
        what = getattr(e, "problem", None) or str(e).splitlines()[0]
        return [f"{path}{where}: not valid YAML ({what}): GitHub would refuse the whole workflow"]
    if not isinstance(doc, dict):
        return [f"{path}: the YAML is not a mapping (a workflow is one)"]
    return []


def release_workflow_problems(path: str, text: str) -> tuple[list[str], set[int]]:
    """SIGNKMS-1: the release workflow's shape (see the module's doc); the line numbers of its two allowed permission lines"""
    try:
        import yaml
        doc = yaml.safe_load(text)
    except Exception:  # noqa: BLE001 (yaml_problems names the parse failure)
        return [], set()
    if not isinstance(doc, dict):
        return [], set()
    probs: list[str] = []
    on = doc.get("on", doc.get(True))   # PyYAML reads a bare `on` key as True
    if not isinstance(on, dict) or set(on) != {"workflow_dispatch"}:
        probs.append(f"{path}: the release workflow runs on workflow_dispatch alone")
        on = {}
    if doc.get("permissions") != {"contents": "read"}:
        probs.append(f"{path}: the release workflow's top-level permissions are not exactly contents: read")
    inputs = ((on.get("workflow_dispatch") or {}).get("inputs") or {}) if isinstance(on.get("workflow_dispatch"), dict) else {}
    kind = inputs.get("kind") if isinstance(inputs, dict) else None
    kind_ok = isinstance(kind, dict) and kind.get("type") == "choice" and isinstance(kind.get("options"), list) \
        and kind["options"] and set(kind["options"]) <= SIGN_ENVIRONMENTS
    jobs = doc.get("jobs") if isinstance(doc.get("jobs"), dict) else {}
    signing = [name for name, job in jobs.items() if isinstance(job, dict) and "permissions" in job]
    if len(signing) != 1:
        probs.append(f"{path}: the release workflow has {len(signing)} jobs with their own permissions (exactly one: the signing job)")
    for name in signing:
        job = jobs[name]
        if job.get("permissions") != SIGN_PERMISSIONS:
            probs.append(f"{path}: job {name}'s permissions are not exactly contents: write and id-token: write")
        env = job.get("environment")
        if isinstance(env, dict):
            env = env.get("name")
        if not (env in SIGN_ENVIRONMENTS or (env == "${{ inputs.kind }}" and kind_ok)):
            probs.append(f"{path}: job {name} signs outside the nightly / stable environments (environment: {env!r})")
    allowed: set[int] = set()   # line numbers: +n a permission line, -n the secret line, all inside the signing job's block
    if not probs:
        lines = text.splitlines()
        for name in signing:
            env = jobs[name].get("env")
            if isinstance(env, dict) and "SIGNING_ROLE_ARN" in env and env["SIGNING_ROLE_ARN"] != "${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}":
                probs.append(f"{path}: SIGNING_ROLE_ARN is not the secret OPENBFME_SIGNING_ROLE_ARN")
            start = next((i for i, line in enumerate(lines) if line == f"  {name}:"), None)
            if start is None:
                probs.append(f"{path}: job {name} is not written as a block at two spaces")
                continue
            for i in range(start + 1, len(lines)):
                if lines[i].strip() and not lines[i].startswith("    "):
                    break
                code = lines[i].split("#", 1)[0].rstrip()
                if code in ("      contents: write", "      id-token: write"):
                    allowed.add(i + 1)
                elif code == RELEASE_SECRET_LINE:
                    allowed.add(-(i + 1))
        if len([n for n in allowed if n > 0]) != 2:
            probs.append(f"{path}: the signing job's two permission lines are not where they are expected")
            allowed = set()
    return probs, allowed


def reusable_problems(path: str, text: str) -> list[str]:
    """RELSIGN-1: workflow_call only in REUSABLE_WORKFLOW (beside workflow_dispatch, no secrets declared); job-level `uses:` only in the
    release workflow, calling exactly REUSABLE_WORKFLOW, with no permissions or secrets of its own"""
    try:
        import yaml
        doc = yaml.safe_load(text)
    except Exception:  # noqa: BLE001 (yaml_problems names the parse failure)
        return []
    if not isinstance(doc, dict):
        return []
    probs: list[str] = []
    on = doc.get("on", doc.get(True))
    if isinstance(on, dict) and "workflow_call" in on:
        if path != REUSABLE_WORKFLOW:
            probs.append(f"{path}: the trigger workflow_call (only {REUSABLE_WORKFLOW} may be called)")
        elif set(on) != {"workflow_dispatch", "workflow_call"}:
            probs.append(f"{path}: {REUSABLE_WORKFLOW} runs on workflow_dispatch and workflow_call alone")
        call = on.get("workflow_call")
        if call is not None and (not isinstance(call, dict) or set(call) - {"inputs"}):
            probs.append(f"{path}: workflow_call declares more than inputs (no secrets, no outputs)")
    jobs = doc.get("jobs") if isinstance(doc.get("jobs"), dict) else {}
    for name, job in jobs.items():
        if not isinstance(job, dict) or "uses" not in job:
            continue
        if path != RELEASE_WORKFLOW:
            probs.append(f"{path}: job {name} calls a workflow (only the release workflow may)")
        if job.get("uses") != f"./{REUSABLE_WORKFLOW}":
            probs.append(f"{path}: job {name} calls {job.get('uses')!r}, not ./{REUSABLE_WORKFLOW}")
        if "permissions" in job or "secrets" in job:
            probs.append(f"{path}: job {name} calls a workflow with its own permissions or secrets")
    return probs


def workflow_problems(path: str, text: str) -> list[str]:
    probs = yaml_problems(path, text) + reusable_problems(path, text)
    lines = text.splitlines()
    allowed: set[int] = set()
    if path == RELEASE_WORKFLOW:
        more, allowed = release_workflow_problems(path, text)
        probs += more
    for n, line in enumerate(lines, 1):
        code = line   # comments included: a refused word in a comment fails too (stricter than YAML, never looser)
        low = code.lower()
        for t in REFUSED_TRIGGERS:
            if re.search(rf"(?<![a-z_]){t}(?![a-z_])", low):
                probs.append(f"{path}:{n}: the trigger {t} (it runs with the base repository's token and secrets)")
        if ("secrets." in low or "secrets[" in low) and -n not in allowed:
            probs.append(f"{path}:{n}: a secrets reference")
        if SECRETS_KEY.match(code):
            probs.append(f"{path}:{n}: a secrets key (secrets: inherit or a declared secret)")
        if "id-token" in low and n not in allowed:
            probs.append(f"{path}:{n}: an id-token permission")
        if (re.search(r":\s*write\b", low) and n not in allowed) or re.search(r"\bwrite-all\b", low):
            probs.append(f"{path}:{n}: a write permission")
        if "self-hosted" in low:
            probs.append(f"{path}:{n}: a self-hosted runner")
        if re.match(r"^\s*(?:-\s+)?uses:", code):
            m = USES.match(code)
            if not m or not (m.group(1).startswith("./") or PINNED.fullmatch(m.group(1))):
                probs.append(f"{path}:{n}: an action not pinned by its full commit sha")
        m = RUNS_ON.match(code)
        if m and not HOSTED.fullmatch(m.group(1).strip("'\"")):
            probs.append(f"{path}:{n}: runs-on is not one GitHub-hosted runner (ubuntu-*, windows-*, macos-*)")
    # the triggers: the top-level `on:` block's keys (block style), or a refusal
    on = [i for i, line in enumerate(lines) if re.match(r"^(?:on|\"on\"|'on'):", line)]
    if len(on) != 1:
        probs.append(f"{path}: no single top-level `on:` block")
    else:
        rest = lines[on[0]].split(":", 1)[1].split("#", 1)[0].strip()
        if rest:
            probs.append(f"{path}:{on[0] + 1}: `on:` is not written as a block (one trigger per line)")
        for line in lines[on[0] + 1:]:
            if line and not line.startswith(" "):
                break
            m = re.match(r"^  ([A-Za-z_]+):", line)
            if m and m.group(1) not in ALLOWED_TRIGGERS and not (m.group(1) == "workflow_call" and path == REUSABLE_WORKFLOW):
                probs.append(f"{path}: the trigger {m.group(1)} is not one of {', '.join(ALLOWED_TRIGGERS)}")
    # the top-level permissions: exactly `contents: read`
    perm = [i for i, line in enumerate(lines) if line.startswith("permissions:")]
    block = []
    if len(perm) == 1:
        for line in lines[perm[0] + 1:]:
            if line and not line.startswith(" "):
                break
            if line.split("#", 1)[0].strip():
                block.append(line.split("#", 1)[0].strip())
    if len(perm) != 1 or lines[perm[0]].split("#", 1)[0].strip() != "permissions:" or block != ["contents: read"]:
        probs.append(f"{path}: the top-level permissions are not exactly `contents: read`")
    return probs


def tree_problems(git_dir: str | None, tree: str) -> list[str]:
    g = ["git"] + (["-C", git_dir] if git_dir else [])
    r = subprocess.run(g + ["ls-tree", "-r", "-z", "--full-tree", tree, "--", ".github"], capture_output=True)
    if r.returncode:
        return [f"cannot list the tree {tree}: {r.stderr.decode(errors='replace').strip()}"]
    probs = []
    for e in filter(None, r.stdout.decode().split("\0")):
        meta, path = e.split("\t", 1)
        mode, kind, oid = meta.split()
        if kind != "blob" or mode not in ("100644", "100755") or not any(a.fullmatch(path) for a in APPROVED):
            probs.append(f"{path}: not an approved file of the public GitHub folder (public_workflows.py APPROVED)")
            continue
        if path.startswith(".github/workflows/"):
            data = subprocess.run(g + ["cat-file", "blob", oid], capture_output=True, check=True).stdout
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                probs.append(f"{path}: not UTF-8 text")
                continue
            probs += workflow_problems(path, text)
    return probs


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    ap.add_argument("--tree", required=True)
    a = ap.parse_args(argv)
    probs = tree_problems(a.git_dir, a.tree)
    for p in probs:
        print(f"PUBLIC WORKFLOWS: {p}")
    print(f"PUBLIC WORKFLOWS {'FAILED' if probs else 'ok'}: {a.tree}")
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
