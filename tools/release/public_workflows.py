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
Lane SIGNKMS-1, RELGH-2: ONE exception, the release workflow (.github/workflows/release.yml, RELEASE_WORKFLOW): the job that signs a
nightly with the nightly key creates the draft release (`contents: write`) and reads the key, the environment secret
OPENBFME_NIGHTLY_SIGNING_KEY. Those two lines are accepted in that file alone and only when the parsed YAML has exactly this shape
(release_workflow_problems): the one trigger workflow_dispatch; the top-level permissions `contents: read`; exactly one job with a
`permissions:` block, which is exactly {contents: write}, whose `environment:` is exactly nightly-signing (deployment branches main only:
docs/RELEASE.md, "Nightly signing on GitHub Actions"); no job-level `env:` naming a secret; exactly ONE step of that job whose `env:` maps
NIGHTLY_SIGNING_KEY to the secret, by the exact line RELEASE_SECRET_LINE; no other secrets reference, and no `id-token` anywhere (the
SIGNKMS-1 OIDC permission is gone with KMS). Every other rule still applies to the file.
Lane RELGH-3: a SECOND signing job in the same file, the one that signs a stable release with the stable Actions key: its `environment:` is
exactly stable-signing (the owner's review required: tools/release/stable_environment.json), its permissions exactly {contents: write,
actions: read} (actions read: it reads the environment's rules and the run's approval), and exactly one step's `env:` maps
STABLE_SIGNING_KEY to the environment secret OPENBFME_STABLE_SIGNING_KEY by the exact line STABLE_SECRET_LINE. So the file has exactly two
signing jobs, one per environment, each environment named once, each secret once and in its own job (the nightly key is never in the
stable job, nor the stable key in the nightly one). Any other job may have its own permissions only when they are read-only and at most
{contents: read, actions: read} (the plan job reads the environment), and enters no environment.
Lane RELSIGN-1: the release workflow builds with MACACT-1's jobs by CALLING .github/workflows/release-package.yml (REUSABLE_WORKFLOW), so
the build jobs have one definition. That file alone may add the trigger `workflow_call` (beside workflow_dispatch, nothing else), and it
declares no `secrets`; a job that calls a workflow (`jobs.<id>.uses`) is allowed in release.yml alone, must call exactly
./.github/workflows/release-package.yml and has no `permissions` or `secrets` of its own (it runs with the top-level contents: read). A
`secrets:` key is refused in every workflow (`secrets: inherit` would hand a called workflow every secret without a `secrets.` reference).
Lane CRASHACT-1: ONE more secret, CRASH_CLIENT_KEY (the crash service's client key, which every package carries anyway: it is the signing
key of an extractable client, tools/release/crash_key.py), and only by these exact lines (crash_key_problems): in release.yml the step
"Build the packages (package.sh)" of job `package` (its step env exactly {CRASH_CLIENT_KEY}) and the macOS call (job `macos`: `secrets:`
exactly {CRASH_CLIENT_KEY}); in release-package.yml `on.workflow_call.secrets` exactly {CRASH_CLIENT_KEY: {required: false}} and the step
"Build the package (package_macos.py)" of job `macos` (step env exactly {CRASH_CLIENT_KEY}). No other job, step, call or workflow.

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
REUSABLE_WORKFLOW = ".github/workflows/release-package.yml"
SECRETS_KEY = re.compile(r"^\s*(?:-\s+)?[\"']?secrets[\"']?\s*:")
RELEASE_SECRET = "${{ secrets.OPENBFME_NIGHTLY_SIGNING_KEY }}"
RELEASE_SECRET_LINE = "          NIGHTLY_SIGNING_KEY: " + RELEASE_SECRET
STABLE_SECRET = "${{ secrets.OPENBFME_STABLE_SIGNING_KEY }}"   # RELGH-3 (tools/release/stable_environment.json "secret")
STABLE_SECRET_LINE = "          STABLE_SIGNING_KEY: " + STABLE_SECRET
# the signing jobs, by environment: (the step's variable, the secret expression, the job's exact permissions)
SIGN_JOBS = {"nightly-signing": ("NIGHTLY_SIGNING_KEY", RELEASE_SECRET, {"contents": "write"}),
             "stable-signing": ("STABLE_SIGNING_KEY", STABLE_SECRET, {"contents": "write", "actions": "read"})}
SIGN_ENVIRONMENTS = set(SIGN_JOBS)
SIGN_RUN = 'python3 tools/release/actions_release.py sign --in "$RUNNER_TEMP/in" --pkg "$RUNNER_TEMP/pkg"'
APPROVAL_RUN = "python3 tools/release/stable_environment.py approval"
READ_PERMISSIONS = {"contents": "read", "actions": "read"}   # the most any other job of the release workflow may ask for
CRASH_SECRET = "${{ secrets.CRASH_CLIENT_KEY }}"   # lane CRASHACT-1
CRASH_ENV = {"CRASH_CLIENT_KEY": CRASH_SECRET}
# path -> (job, step name) whose step env is exactly CRASH_ENV
CRASH_STEPS = {RELEASE_WORKFLOW: ("package", "Build the packages (package.sh)"),
               REUSABLE_WORKFLOW: ("macos", "Build the package (package_macos.py)")}
CRASH_CALL_JOB = "macos"   # release.yml's job that calls REUSABLE_WORKFLOW with `secrets:` exactly CRASH_ENV
CRASH_CALL_SECRETS = {"CRASH_CLIENT_KEY": {"required": False}}   # REUSABLE_WORKFLOW's on.workflow_call.secrets


def _job_block(lines: list[str], job: str) -> tuple[int, int] | None:
    """the line range (0-based, end exclusive) of `  <job>:` under jobs, written as a block at two spaces"""
    start = next((i for i, line in enumerate(lines) if line == f"  {job}:"), None)
    if start is None:
        return None
    end = start + 1
    while end < len(lines) and not (lines[end].strip() and not lines[end].startswith("    ")):
        end += 1
    return start, end


def crash_key_problems(path: str, text: str) -> tuple[list[str], set[int]]:
    """CRASHACT-1: the crash key's allowed shapes in the release workflow and its reusable workflow (see the module's doc); the 1-based line
    numbers of its allowed secret and `secrets:` lines. Every other file gets no exception."""
    if path not in CRASH_STEPS:
        return [], set()
    try:
        import yaml
        doc = yaml.safe_load(text)
    except Exception:  # noqa: BLE001 (yaml_problems names the parse failure)
        return [], set()
    if not isinstance(doc, dict):
        return [], set()
    probs: list[str] = []
    lines = text.splitlines()
    # Sol r3: every place the PARSED workflow holds the secret, YAML aliases expanded (an anchored env reused by another step reads the
    # secret there while its text line appears once): exactly the approved recipients (release.yml: the step and the call; the reusable
    # workflow: its step)
    def holders(node, seen=0):
        if isinstance(node, dict):
            return sum(holders(v) for v in node.values())
        if isinstance(node, list):
            return sum(holders(v) for v in node)
        return 1 if isinstance(node, str) and "secrets.crash_client_key" in node.lower().replace(" ", "") else 0
    want_count = 2 if path == RELEASE_WORKFLOW else 1
    if holders(doc) != want_count:
        probs.append(f"{path}: the crash key is read {holders(doc)} times in the parsed workflow, not exactly {want_count}")
    jobs = doc.get("jobs") if isinstance(doc.get("jobs"), dict) else {}
    job_name, step_name = CRASH_STEPS[path]
    job = jobs.get(job_name) if isinstance(jobs.get(job_name), dict) else {}
    steps = job.get("steps") if isinstance(job.get("steps"), list) else []
    named = [st for st in steps if isinstance(st, dict) and st.get("name") == step_name]
    if len(named) != 1 or named[0].get("env") != CRASH_ENV:
        probs.append(f"{path}: job {job_name}'s step {step_name!r} must hand exactly {{CRASH_CLIENT_KEY}} to its env, once")
    allowed: set[int] = set()
    want = [(job_name, "          CRASH_CLIENT_KEY: " + CRASH_SECRET)]
    if path == RELEASE_WORKFLOW:
        call = jobs.get(CRASH_CALL_JOB) if isinstance(jobs.get(CRASH_CALL_JOB), dict) else {}
        if call.get("secrets") != CRASH_ENV or (call.get("with") or {}).get("platform") != "macos":
            probs.append(f"{path}: job {CRASH_CALL_JOB} must call the macOS build with `secrets:` exactly {{CRASH_CLIENT_KEY}}")
        want += [(CRASH_CALL_JOB, "    secrets:"), (CRASH_CALL_JOB, "      CRASH_CLIENT_KEY: " + CRASH_SECRET)]
    else:
        on = doc.get("on", doc.get(True))
        call = on.get("workflow_call") if isinstance(on, dict) else None
        if not isinstance(call, dict) or call.get("secrets") != CRASH_CALL_SECRETS:
            probs.append(f"{path}: workflow_call must declare exactly the secret CRASH_CLIENT_KEY (required: false)")
        on_line = next((i for i, line in enumerate(lines) if re.match(r"^(?:on|\"on\"|'on'):", line)), None)
        hits = [i for i in range(on_line + 1 if on_line is not None else 0, len(lines)) if lines[i] == "    secrets:"
                and i > 0 and lines[i - 1].rstrip() == "  workflow_call:"]
        if len(hits) != 1:
            probs.append(f"{path}: `secrets:` is not written right under `  workflow_call:`")
        allowed.update(i + 1 for i in hits)
    for job_id, line_text in want:
        block = _job_block(lines, job_id)
        found = [i for i in range(*block) if lines[i].split("#", 1)[0].rstrip() == line_text] if block else []
        if len(found) != 1:
            probs.append(f"{path}: job {job_id} does not hold the line {line_text.strip()!r} exactly once")
        allowed.update(i + 1 for i in found)
    return probs, (set() if probs else allowed)


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
    """SIGNKMS-1, RELGH-2, RELGH-3: the release workflow's shape (see the module's doc); the line numbers of its allowed permission, secret
    and environment lines"""
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
    jobs = doc.get("jobs") if isinstance(doc.get("jobs"), dict) else {}
    signing: dict[str, str] = {}   # job -> environment
    for name, job in jobs.items():
        if not isinstance(job, dict):
            continue
        env = job.get("environment")
        if isinstance(env, dict):
            env = env.get("name")
        if env is None:
            perms = job.get("permissions")
            if perms is not None and (not isinstance(perms, dict) or any(READ_PERMISSIONS.get(k) != v for k, v in perms.items())):
                probs.append(f"{path}: job {name} asks for {perms!r}: only a signing job writes (others read contents / actions at most)")
            keyed = [st for st in job.get("steps") or [] if isinstance(st, dict) and "secrets" in str(st.get("env") or "").lower()]
            crash = (name, keyed[0].get("name") if len(keyed) == 1 else None) == CRASH_STEPS[RELEASE_WORKFLOW] and keyed[0].get("env") == CRASH_ENV
            if "secrets" in str(job.get("env") or "").lower() or (keyed and not crash):
                probs.append(f"{path}: job {name} reads a secret outside a signing job (CRASHACT-1: only the package step's crash key)")
            continue
        if env not in SIGN_JOBS:
            probs.append(f"{path}: job {name} signs outside the signing environments (environment: {env!r})")
            continue
        if env in signing.values():
            probs.append(f"{path}: the environment {env} is named by two jobs")
        signing[name] = env
    if sorted(signing.values()) != sorted(SIGN_JOBS):
        probs.append(f"{path}: the release workflow has the signing jobs {sorted(signing.values())}, not exactly one per "
                     f"environment {sorted(SIGN_JOBS)}")
    for name, env in signing.items():
        job = jobs[name]
        var, secret, perms = SIGN_JOBS[env]
        if job.get("permissions") != perms:
            probs.append(f"{path}: job {name}'s permissions are not exactly {perms}")
        if "secrets" in str(job.get("env") or "").lower():
            probs.append(f"{path}: job {name} hands a secret to every step (job-level env); one step's env only")
        steps = job.get("steps") if isinstance(job.get("steps"), list) else []
        keyed = [st for st in steps if isinstance(st, dict) and "secrets" in str(st.get("env") or "").lower()]
        if len(keyed) != 1 or keyed[0].get("env") != {var: secret}:
            probs.append(f"{path}: job {name} must hand its key to exactly one step, as env {var} alone ({env}'s own secret)")
        # Sol r1 (RELGH-3): the job's guard, its keyed command and (stable) the approval check before the key are fixed too
        kind = "nightly" if env == "nightly-signing" else "stable"
        if job.get("if") != f"needs.plan.outputs.kind == '{kind}'":
            probs.append(f"{path}: job {name} does not run exactly when the plan's kind is {kind}")
        if not isinstance(job.get("needs"), list) or not {"plan", "macos", "package"} <= set(job["needs"]):
            probs.append(f"{path}: job {name} does not wait for the plan and the builds")
        if len(keyed) == 1 and keyed[0].get("run") != SIGN_RUN:
            probs.append(f"{path}: job {name}'s keyed step does not run exactly `{SIGN_RUN}`")
        if kind == "stable":
            runs = [st.get("run") if isinstance(st, dict) else None for st in steps]
            k = steps.index(keyed[0]) if len(keyed) == 1 else -1
            if APPROVAL_RUN not in runs[:max(k, 0)]:
                probs.append(f"{path}: job {name} has no `{APPROVAL_RUN}` step before its keyed step")
    allowed: set[int] = set()   # line numbers: +n a permission line, -n a secret line, -(10**6 + n) an environment line, all inside a
    #                             signing job's block
    if not probs:
        lines = text.splitlines()
        for name, env in signing.items():
            var, secret, _ = SIGN_JOBS[env]
            start = next((i for i, line in enumerate(lines) if line == f"  {name}:"), None)
            if start is None:
                probs.append(f"{path}: job {name} is not written as a block at two spaces")
                continue
            got = {"perm": 0, "secret": 0, "env": 0}
            for i in range(start + 1, len(lines)):
                if lines[i].strip() and not lines[i].startswith("    "):
                    break
                code = lines[i].split("#", 1)[0].rstrip()
                if code == "      contents: write":
                    allowed.add(i + 1)
                    got["perm"] += 1
                elif code.strip() == f"environment: {env}" and code.startswith("    environment:"):
                    allowed.add(-(10**6 + i + 1))
                    got["env"] += 1
                elif code == f"          {var}: {secret}":
                    allowed.add(-(i + 1))
                    got["secret"] += 1
            if got != {"perm": 1, "secret": 1, "env": 1}:
                probs.append(f"{path}: job {name}'s permission line, secret line and environment line are not where they are expected")
        if probs:
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
        if call is not None and (not isinstance(call, dict) or set(call) - {"inputs", "secrets"}
                                 or call.get("secrets", CRASH_CALL_SECRETS) != CRASH_CALL_SECRETS):
            probs.append(f"{path}: workflow_call declares more than inputs and the crash key (no other secret, no outputs)")
    jobs = doc.get("jobs") if isinstance(doc.get("jobs"), dict) else {}
    for name, job in jobs.items():
        if not isinstance(job, dict) or "uses" not in job:
            continue
        if path != RELEASE_WORKFLOW:
            probs.append(f"{path}: job {name} calls a workflow (only the release workflow may)")
        if job.get("uses") != f"./{REUSABLE_WORKFLOW}":
            probs.append(f"{path}: job {name} calls {job.get('uses')!r}, not ./{REUSABLE_WORKFLOW}")
        crash = name == CRASH_CALL_JOB and job.get("secrets") == CRASH_ENV and (job.get("with") or {}).get("platform") == "macos"
        if "permissions" in job or ("secrets" in job and not crash):
            probs.append(f"{path}: job {name} calls a workflow with its own permissions or secrets (CRASHACT-1: the macOS call's crash key alone)")
    return probs


def workflow_problems(path: str, text: str) -> list[str]:
    probs = yaml_problems(path, text) + reusable_problems(path, text)
    lines = text.splitlines()
    allowed: set[int] = set()
    if path == RELEASE_WORKFLOW:
        more, allowed = release_workflow_problems(path, text)
        probs += more
    crash_probs, crash_lines = crash_key_problems(path, text)   # CRASHACT-1
    probs += crash_probs
    for n, line in enumerate(lines, 1):
        code = line   # comments included: a refused word in a comment fails too (stricter than YAML, never looser)
        low = code.lower()
        for t in REFUSED_TRIGGERS:
            if re.search(rf"(?<![a-z_]){t}(?![a-z_])", low):
                probs.append(f"{path}:{n}: the trigger {t} (it runs with the base repository's token and secrets)")
        # RELGH-2 (Sol r1): any use of the secrets context (secrets.X, secrets[...], toJSON(secrets), ...), comments included
        exprs = " ".join(re.findall(r"\$\{\{(.*?)\}\}", low)) + (" " + low if re.match(r"^\s*(?:-\s+)?if\s*:", low) else "")
        if ("secrets." in low or "secrets[" in low or re.search(r"(?<![a-z0-9_-])secrets(?![a-z0-9_-])", exprs)) and -n not in allowed \
                and n not in crash_lines:
            probs.append(f"{path}:{n}: a secrets reference")
        # RELGH-2 (Sol r1): a job may enter a deployment environment only as the release workflow's signing job
        if re.match(r"^\s*(?:-\s+)?[\"']?environment[\"']?\s*:", code) and -(10**6 + n) not in allowed:
            probs.append(f"{path}:{n}: a deployment environment outside the release workflow's signing job")
        if SECRETS_KEY.match(code) and n not in crash_lines:
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
