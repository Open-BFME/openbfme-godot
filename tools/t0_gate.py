#!/usr/bin/env python3
"""T0 machine gate for OpenBFME loop packets.

Mechanically rejects a packet before any model or human review, so that
reviewer attention is spent on parity and design rather than on contract
and containment violations.

Every check here exists because the recorded incident history says it
happened: .private/orchestration/metrics.json reports pathLockViolations 2,
canonicalPublicationIncidents 1, testWeakeningIncidents 1, and
firstPassAccepted 0 of 13.

Contract source: AGENTS.md (packet schema, lock rules,
reviewer counts, stop conditions).

Usage:
    python tools/t0_gate.py --task-id <id> [--repo <path>] [--worktree <path>]
                            [--json <out>] [--strict-acceptance]

Exit codes: 0 all checks pass, 1 one or more FAIL, 2 usage/setup error.

Pure standard library. Runs on Windows, Linux, and macOS — no PyYAML, so it
works in the pinned runtimes and in CI without provisioning.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------- constants

REQUIRED_FIELDS = [
    "id", "state", "objective", "source_of_truth", "oracle", "base_commit",
    "lane", "allowed_paths", "forbidden_paths", "private_job_root",
    "non_goals", "acceptance", "expected_marker", "reviewers", "stop_if",
    "deliverables", "workflow_version",
]

VALID_LANES = {
    "importer", "runtime", "simulation", "networking", "oracle", "tooling",
    "docs", "importer-runtime", "importer-tooling", "performance", "cleanup",
    "platform", "gameplay", "content",
}

# AGENTS.md: objectives containing open-ended language stay draft.
OPEN_ENDED = [
    "improve", "polish", "finish", "cleanup", "clean up", "better", "various",
    "etc", "and more", "as needed", "where appropriate", "general", "tidy",
    "refactor",
]

# Two reviewers required for these boundaries (AGENT_WORKFLOW.md).
TWO_REVIEWER_LANES = {
    "importer", "importer-runtime", "simulation", "networking", "performance",
    "platform",
}
TWO_REVIEWER_RISK = {"high"}

# Owner-only state. A worker touching these is a canonical-publication
# incident, which has happened once.
OWNER_ONLY = [
    ".private/orchestration/",
    ".private/content-packs/selection.json",
    "AGENTS.md",
    "DIRECTION.md",
    "contracts/",
]

# Never commit-reachable. The release firewall.
CONTAINMENT_PREFIXES = [
    ".private/", "dist/", "reference/", "_bfme2_extract/", "build/",
]

# Retail-derived payload extensions. Outside .private these are a leak.
RETAIL_EXTENSIONS = {
    ".big", ".w3d", ".dds", ".tga", ".bik", ".vp6", ".wnd", ".apt", ".ru",
    ".ini.big", ".manifest", ".relo", ".imp",
}

MAX_BLOB_BYTES = 5 * 1024 * 1024

# An acceptance entry should be runnable, not prose.
RUNNABLE_PREFIXES = (
    "python", "py ", "pytest", "dotnet", "godot", "powershell", "pwsh",
    "cmd", "bash", "sh ", "npm", "node", "git", "./", ".\\",
)
RUNNABLE_SUFFIXES = (".bat", ".ps1", ".sh", ".py")

# A broad or final gate on a worker packet. AGENTS.md: workers do not run
# broad/final gates, because a broad worker gate can mutate shared selection.
BROAD_GATE = re.compile(
    r"\bfull\b[^.\n]*\b(suite|pytest|test)\b|\bentire\b[^.\n]*\bsuite\b|"
    r"\ball tests\b|run_m2_acceptance|IntegrationOwnerPublish",
    re.IGNORECASE,
)

EVIDENCE_KEYS = [
    "commit", "os", "command", "exit_status", "expected_marker_observed",
    "content_pack_id", "artifact_hashes", "unresolved_risks",
]


# ------------------------------------------------------------------ results

PASS, FAIL, WARN, SKIP = "PASS", "FAIL", "WARN", "SKIP"


@dataclass
class Report:
    rows: list = field(default_factory=list)

    def add(self, status, check, detail=""):
        self.rows.append({"status": status, "check": check, "detail": detail})
        return status

    def ok(self, check, detail=""):
        return self.add(PASS, check, detail)

    def fail(self, check, detail=""):
        return self.add(FAIL, check, detail)

    def warn(self, check, detail=""):
        return self.add(WARN, check, detail)

    def skip(self, check, detail=""):
        return self.add(SKIP, check, detail)

    @property
    def failed(self):
        return [r for r in self.rows if r["status"] == FAIL]

    @property
    def warned(self):
        return [r for r in self.rows if r["status"] == WARN]


# ------------------------------------------------------------- yaml (subset)

def parse_task_yaml(text):
    """Parse the flat scalar + block-list subset used by task.yaml.

    Deliberately not a general YAML parser. It handles exactly the schema in
    AGENTS.md and raises on anything it does not understand,
    rather than silently mis-reading a packet contract.
    """
    data, current_key = {}, None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith(("  - ", "- ")):
            item = line.split("- ", 1)[1].strip()
            if current_key is None:
                raise ValueError(f"line {lineno}: list item before any key")
            if not isinstance(data.get(current_key), list):
                data[current_key] = []
            data[current_key].append(_unquote(item))
            continue
        if line.startswith(" "):
            raise ValueError(f"line {lineno}: unsupported nesting: {line!r}")
        if ":" not in line:
            raise ValueError(f"line {lineno}: not a key: {line!r}")
        key, _, value = line.partition(":")
        key, value = key.strip(), value.strip()
        current_key = key
        data[key] = _unquote(value) if value else []
    return data


def _unquote(value):
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


# --------------------------------------------------------------------- git

def git(repo, *args, check=True):
    proc = subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if check and proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout


def changed_paths(repo, base):
    """Committed changes since base, plus anything currently uncommitted."""
    out = set()
    committed = git(repo, "diff", "--name-only", f"{base}..HEAD", check=False)
    out.update(p for p in committed.splitlines() if p)
    working = git(repo, "status", "--porcelain")
    for line in working.splitlines():
        if not line:
            continue
        path = line[3:].strip()
        if " -> " in path:            # rename
            path = path.split(" -> ", 1)[1]
        out.add(path.strip('"'))
    return sorted(out)


def under_any(path, prefixes):
    norm = path.replace("\\", "/").lstrip("./")
    for prefix in prefixes:
        pre = prefix.replace("\\", "/").lstrip("./")
        if not pre:
            continue
        if norm == pre or norm.startswith(pre.rstrip("/") + "/") or norm.startswith(pre):
            return prefix
    return None


# ------------------------------------------------------------------ checks

def check_contract(task, rep, strict_acceptance):
    missing = [f for f in REQUIRED_FIELDS if f not in task or task[f] in ("", [], None)]
    if missing:
        rep.fail("C1 required fields", f"missing/empty: {', '.join(missing)}")
    else:
        rep.ok("C1 required fields", f"{len(REQUIRED_FIELDS)} present")

    base = task.get("base_commit", "")
    if re.fullmatch(r"[0-9a-f]{40}", str(base)):
        rep.ok("C2 base_commit", "full 40-hex identity")
    else:
        rep.fail("C2 base_commit", f"must be full 40-hex, got {base!r}")

    lane = task.get("lane", "")
    if lane in VALID_LANES:
        rep.ok("C3 lane", lane)
    else:
        rep.fail("C3 lane", f"{lane!r} not in {sorted(VALID_LANES)}")

    objective = str(task.get("objective", "")).lower()
    hits = [w for w in OPEN_ENDED if re.search(rf"\b{re.escape(w)}\b", objective)]
    if hits:
        rep.fail("C4 objective bounded",
                 f"open-ended language: {', '.join(hits)} — AGENT_WORKFLOW.md "
                 f"keeps such objectives in draft")
    else:
        rep.ok("C4 objective bounded")

    allowed = task.get("allowed_paths") or []
    if not isinstance(allowed, list) or not allowed:
        rep.fail("C5 allowed_paths", "must be a non-empty list")
    elif any(p.strip() in (".", "/", "*", "**") for p in allowed):
        rep.fail("C5 allowed_paths", "whole-repo scope is not a bounded packet")
    else:
        rep.ok("C5 allowed_paths", f"{len(allowed)} entries")

    non_goals = task.get("non_goals") or []
    if non_goals:
        rep.ok("C6 non_goals", f"{len(non_goals)} declared")
    else:
        rep.fail("C6 non_goals", "must declare at least one explicit non-goal")

    acceptance = task.get("acceptance") or []
    if not acceptance:
        rep.fail("C7 acceptance runnable", "no acceptance command")
    else:
        prose = [
            a for a in acceptance
            if not (str(a).strip().lower().startswith(RUNNABLE_PREFIXES)
                    or str(a).strip().lower().endswith(RUNNABLE_SUFFIXES))
        ]
        if not prose:
            rep.ok("C7 acceptance runnable", f"{len(acceptance)} command(s)")
        elif strict_acceptance:
            rep.fail("C7 acceptance runnable",
                     f"prose, not a command: {prose!r} — a gate that cannot be "
                     f"executed cannot be failed, so acceptance becomes a "
                     f"judgment call")
        else:
            rep.warn("C7 acceptance runnable",
                     f"prose, not a command: {prose!r} (use --strict-acceptance "
                     f"to make this a hard failure)")

    marker = str(task.get("expected_marker", "")).strip()
    if len(marker) >= 12:
        rep.ok("C8 expected_marker")
    else:
        rep.fail("C8 expected_marker", f"too vague to check: {marker!r}")

    try:
        reviewers = int(task.get("reviewers", 0))
    except (TypeError, ValueError):
        reviewers = 0
    needs_two = lane in TWO_REVIEWER_LANES or str(task.get("risk", "")) in TWO_REVIEWER_RISK
    if needs_two and reviewers < 2:
        rep.fail("C9 reviewer count",
                 f"lane={lane} risk={task.get('risk')} requires 2, got {reviewers}")
    elif reviewers < 1:
        rep.fail("C9 reviewer count", "at least one reviewer required")
    else:
        rep.ok("C9 reviewer count", str(reviewers))


def check_authority(task, repo, rep):
    """Packet-level authority checks.

    These enforce rules already written in AGENTS.md and AGENTS.md.
    Measured against the 101 packets on disk: 38 declare an owner-only path in
    allowed_paths (53 correctly place them in forbidden_paths instead), and 5
    require a broad suite from a worker.
    """
    allowed = task.get("allowed_paths") or []
    owner_grants = [p for p in allowed if under_any(p, OWNER_ONLY)]
    if owner_grants:
        rep.fail("A4 owner-only grant",
                 f"allowed_paths claims owner-only state {owner_grants[:6]} - "
                 f"AGENTS.md reserves canonical publication and queue state to the "
                 f"integration owner. Move these to forbidden_paths")
    else:
        rep.ok("A4 owner-only grant", "no owner-only path claimed")

    acceptance = task.get("acceptance") or []
    broad = [a for a in acceptance if BROAD_GATE.search(str(a))]
    if broad:
        rep.fail("A5 broad-gate",
                 f"worker packet requires a broad or final gate: {broad[:3]} - "
                 f"AGENTS.md: workers do not run broad/final gates. A broad worker "
                 f"gate can mutate shared selection state")
    else:
        rep.ok("A5 broad-gate", "focused checks only")

    unresolved = []
    for entry in task.get("source_of_truth") or []:
        match = re.search(r"((?:docs|contracts)/[A-Za-z0-9_./-]+\.(?:md|json))", str(entry))
        if match and not (Path(repo) / match.group(1)).exists():
            unresolved.append(match.group(1))
    if unresolved:
        rep.fail("A7 source_of_truth", f"cites path that does not exist: {unresolved}")
    else:
        rep.ok("A7 source_of_truth", "all cited paths resolve")


def check_repo_state(repo, task, rep):
    base = task.get("base_commit", "")
    try:
        # git discovers its repository by walking up, so a --worktree that is
        # not itself a repository root would silently report the HEAD (and
        # diff, and status) of whatever checkout encloses it. A gate that
        # inherits another repository's identity is a gate that lies.
        toplevel = git(repo, "rev-parse", "--show-toplevel").strip()
        head = git(repo, "rev-parse", "HEAD").strip()
    except RuntimeError as exc:
        rep.fail("R1 base identity", str(exc))
        return
    if Path(toplevel).resolve() != Path(repo).resolve():
        rep.fail("R1 base identity",
                 f"{repo} is not itself a repository root (git top-level: "
                 f"{toplevel}) - identity would be inherited from an "
                 f"enclosing checkout")
        return
    if not re.fullmatch(r"[0-9a-f]{40}", str(base)):
        rep.skip("R1 base identity", "base_commit invalid; see C2")
    elif head == base:
        rep.ok("R1 base identity", f"HEAD == base_commit {base[:12]}")
    else:
        try:
            merge_base = git(repo, "merge-base", "HEAD", base).strip()
            if merge_base == base:
                ahead = git(repo, "rev-list", "--count", f"{base}..HEAD").strip()
                rep.ok("R1 base identity", f"HEAD is {ahead} commit(s) ahead of base")
            else:
                rep.fail("R1 base identity",
                         f"HEAD {head[:12]} does not descend from base {base[:12]} "
                         f"— base changed under the packet")
        except RuntimeError as exc:
            rep.fail("R1 base identity", str(exc))

    allowed = task.get("allowed_paths") or []
    dirty = [
        line[3:].strip().strip('"')
        for line in git(repo, "status", "--porcelain").splitlines() if line
    ]
    stray = [p for p in dirty if not under_any(p, allowed)]
    if not dirty:
        rep.ok("R2 working tree", "clean")
    elif stray:
        rep.fail("R2 working tree",
                 f"{len(stray)} path(s) dirty outside allowed_paths: {stray[:8]}")
    else:
        rep.ok("R2 working tree", f"{len(dirty)} dirty path(s), all within allowed_paths")


def check_locks(repo, task, rep):
    locks_file = Path(repo) / ".private" / "orchestration" / "locks.json"
    if not locks_file.exists():
        rep.skip("R3 lock overlap", "no locks.json")
        return
    try:
        locks = json.loads(locks_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        rep.fail("R3 lock overlap", f"unreadable locks.json: {exc}")
        return

    mine = task.get("id")
    allowed = [p.replace("\\", "/").rstrip("/") for p in (task.get("allowed_paths") or [])]
    conflicts = []
    for lock in locks.get("locks", []):
        if lock.get("taskId") == mine or lock.get("task_id") == mine:
            continue
        held = lock.get("paths") or ([lock["path"]] if lock.get("path") else [])
        for h in held:
            hn = str(h).replace("\\", "/").rstrip("/")
            for a in allowed:
                if a == hn or a.startswith(hn + "/") or hn.startswith(a + "/"):
                    conflicts.append((lock.get("taskId") or lock.get("task_id"), hn, a))
    if conflicts:
        rep.fail("R3 lock overlap",
                 "; ".join(f"{t} holds {h} vs our {a}" for t, h, a in conflicts))
    else:
        rep.ok("R3 lock overlap", f"{len(locks.get('locks', []))} active lock(s), disjoint")

    protected = locks.get("protectedUserChanges") or []
    if protected:
        touched = [p for p in protected if under_any(p, allowed)]
        if touched:
            rep.fail("R4 protected user changes", f"packet claims {touched}")
        else:
            rep.ok("R4 protected user changes", f"{len(protected)} preserved")
    else:
        rep.ok("R4 protected user changes", "none registered")


def check_diff(repo, task, rep):
    base = task.get("base_commit", "")
    if not re.fullmatch(r"[0-9a-f]{40}", str(base)):
        rep.skip("D* diff checks", "base_commit invalid; see C2")
        return []

    paths = changed_paths(repo, base)
    if not paths:
        rep.warn("D0 diff non-empty", "no changes detected against base")
        return paths
    rep.ok("D0 diff non-empty", f"{len(paths)} path(s) changed")

    allowed = task.get("allowed_paths") or []
    escaped = [p for p in paths if not under_any(p, allowed)]
    if escaped:
        rep.fail("D1 allowed paths",
                 f"{len(escaped)} path(s) outside contract: {escaped[:10]}")
    else:
        rep.ok("D1 allowed paths", "all changes within contract")

    forbidden = task.get("forbidden_paths") or []
    hit = [(p, under_any(p, forbidden)) for p in paths if under_any(p, forbidden)]
    if hit:
        rep.fail("D2 forbidden paths", "; ".join(f"{p} under {f}" for p, f in hit[:10]))
    else:
        rep.ok("D2 forbidden paths", "none touched")

    owner = [(p, under_any(p, OWNER_ONLY)) for p in paths if under_any(p, OWNER_ONLY)]
    if owner:
        rep.fail("D3 owner-only state",
                 "canonical publication is owner-only: "
                 + "; ".join(f"{p} under {o}" for p, o in owner[:10]))
    else:
        rep.ok("D3 owner-only state", "untouched")

    contained = [(p, under_any(p, CONTAINMENT_PREFIXES)) for p in paths
                 if under_any(p, CONTAINMENT_PREFIXES)]
    if contained:
        rep.fail("D4 containment",
                 "release-firewall path in diff: "
                 + "; ".join(f"{p} under {c}" for p, c in contained[:10]))
    else:
        rep.ok("D4 containment", "no private/dist/reference/build path in diff")

    oversized, retail = [], []
    for p in paths:
        f = Path(repo) / p
        if not f.is_file():
            continue
        try:
            size = f.stat().st_size
        except OSError:
            continue
        if size > MAX_BLOB_BYTES:
            oversized.append((p, size))
        if f.suffix.lower() in RETAIL_EXTENSIONS:
            retail.append(p)
    if oversized:
        rep.fail("D5 blob size",
                 "; ".join(f"{p} {s / 1048576:.1f} MB" for p, s in oversized[:6]))
    else:
        rep.ok("D5 blob size", f"all under {MAX_BLOB_BYTES // 1048576} MB")
    if retail:
        rep.fail("D6 retail extensions", f"retail-derived payload in diff: {retail[:10]}")
    else:
        rep.ok("D6 retail extensions", "none")

    return paths


def check_test_weakening(repo, task, rep, paths):
    """Assertion count in a changed test file must not decrease.

    metrics.json records one testWeakeningIncident. AGENTS.md forbids
    weakening or deleting an assertion to make a gate pass.
    """
    base = task.get("base_commit", "")
    patterns = re.compile(
        r"\bassert\b|\bassert_[a-z_]+\(|\bself\.assert[A-Za-z]+\(|"
        r"Assert\.[A-Za-z]+\(|\bexpect\(|\bpytest\.raises\("
    )
    test_paths = [
        p for p in paths
        if ("test" in p.lower() or "runner" in p.lower())
        and Path(p).suffix.lower() in {".py", ".gd", ".cs"}
    ]
    if not test_paths:
        rep.skip("T1 test weakening", "no test files changed")
        return

    regressions = []
    for p in test_paths:
        before_text = git(repo, "show", f"{base}:{p}", check=False)
        after_file = Path(repo) / p
        after_text = after_file.read_text(encoding="utf-8", errors="replace") \
            if after_file.is_file() else ""
        before = len(patterns.findall(before_text))
        after = len(patterns.findall(after_text))
        if before and after < before:
            regressions.append((p, before, after))
    if regressions:
        rep.fail("T1 test weakening",
                 "; ".join(f"{p}: {b} -> {a} assertions" for p, b, a in regressions))
    else:
        rep.ok("T1 test weakening", f"{len(test_paths)} test file(s), no decrease")


def check_evidence(repo, task, rep):
    root = task.get("private_job_root")
    if not root:
        rep.fail("E1 evidence", "no private_job_root declared")
        return
    job = Path(repo) / root
    if not job.is_dir():
        rep.fail("E1 evidence", f"job root missing: {root}")
        return
    candidates = sorted(job.glob("evidence*.json"))
    if not candidates:
        rep.fail("E1 evidence", f"no evidence*.json under {root}")
        return
    latest = candidates[-1]
    try:
        ev = json.loads(latest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        rep.fail("E1 evidence", f"{latest.name} unreadable: {exc}")
        return

    missing = [k for k in EVIDENCE_KEYS if k not in ev or ev[k] in ("", None)]
    if missing:
        rep.fail("E1 evidence", f"{latest.name} missing: {', '.join(missing)}")
    else:
        rep.ok("E1 evidence", f"{latest.name} complete")

    # A run that cannot name the pack it loaded cannot be trusted. A stale
    # durable pack has previously produced confidently wrong numbers.
    pack = str(ev.get("content_pack_id", "")).strip()
    if not pack:
        rep.fail("E2 content pack identity", "run did not record which pack it loaded")
    elif pack.lower() in {"unknown", "default", "none", "n/a"}:
        rep.fail("E2 content pack identity", f"unusable pack identity: {pack!r}")
    else:
        rep.ok("E2 content pack identity", pack)

    marker = str(task.get("expected_marker", "")).strip()
    observed = str(ev.get("expected_marker_observed", "")).strip()
    if marker and observed:
        rep.ok("E3 marker observed", observed[:80])
    else:
        rep.fail("E3 marker observed", "packet marker not confirmed in evidence")

    status = ev.get("exit_status")
    if status in (0, "0"):
        rep.ok("E4 acceptance exit status", "0")
    else:
        rep.fail("E4 acceptance exit status", f"non-zero: {status!r}")


# -------------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description="T0 machine gate for OpenBFME packets")
    ap.add_argument("--task-id", required=True)
    ap.add_argument("--repo", default=".", help="repository root (default: cwd)")
    ap.add_argument("--worktree", help="worktree to inspect (default: --repo)")
    ap.add_argument("--json", help="write the machine-readable report here")
    ap.add_argument("--strict-acceptance", action="store_true",
                    help="treat non-runnable acceptance entries as FAIL")
    args = ap.parse_args(argv)

    repo = Path(args.repo).resolve()
    worktree = Path(args.worktree).resolve() if args.worktree else repo

    task_file = repo / ".private" / "scratch" / "jobs" / args.task_id / "task.yaml"
    if not task_file.is_file():
        print(f"T0 SETUP ERROR: no packet at {task_file}", file=sys.stderr)
        return 2
    try:
        task = parse_task_yaml(task_file.read_text(encoding="utf-8"))
    except ValueError as exc:
        print(f"T0 SETUP ERROR: {task_file}: {exc}", file=sys.stderr)
        return 2

    rep = Report()
    check_contract(task, rep, args.strict_acceptance)
    check_authority(task, repo, rep)
    check_repo_state(worktree, task, rep)
    check_locks(repo, task, rep)
    paths = check_diff(worktree, task, rep)
    check_test_weakening(worktree, task, rep, paths)
    check_evidence(repo, task, rep)

    width = max(len(r["check"]) for r in rep.rows)
    print(f"\nT0 GATE - packet {args.task_id}")
    print(f"  repo     {repo}")
    print(f"  worktree {worktree}\n")
    for r in rep.rows:
        print(f"  [{r['status']:4}] {r['check']:<{width}}  {r['detail']}")

    digest = hashlib.sha256(
        json.dumps(rep.rows, sort_keys=True).encode("utf-8")
    ).hexdigest()[:16]
    verdict = "REJECT" if rep.failed else "ADMIT"
    print(f"\n  {verdict}: {len(rep.failed)} failed, {len(rep.warned)} warned, "
          f"{len(rep.rows)} checks  [{digest}]")
    if rep.failed:
        print("  Packet does not reach review. Fix the above and re-run.")

    if args.json:
        out = Path(args.json)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps({
            "schema": "openbfme.t0-gate",
            "schemaVersion": 1,
            "taskId": args.task_id,
            "verdict": verdict,
            "digest": digest,
            "checks": rep.rows,
        }, indent=2), encoding="utf-8")
        print(f"  report: {out}")

    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
