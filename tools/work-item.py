"""Small Windows-native work-item workflow for OpenBFME.

This tool coordinates Git; it is not a sandbox. The integration owner assigns
one ledger row and creates one sibling worktree. A worker may then run only the
row's structured checks, change only owned paths, and hand back one commit.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import Any, Sequence


LEDGER_RELATIVE = Path("orchestration/work-items.json")
DIMENSIONS = ["SOURCE", "CONVERT", "LOAD", "BEHAVIOR", "VISUAL", "AUDIO"]
RECEIPT_SCHEMA = "openbfme.work-item-receipt"
ASSIGNMENT_SCHEMA = "openbfme.work-item-assignment"
HEX40_RE = re.compile(r"^[0-9a-f]{40}$")
FORBIDDEN_ARGUMENT_RE = re.compile(
    r"(?i)(?:^[a-z]:|^[\\/]|^~|\.\.(?:[\\/]|$)|%[^%]+%|"
    r"\$(?:env:|\{?[A-Za-z_])|^[a-z][a-z0-9+.-]*://|^@|"
    r"^(?:--?eval|--?execute|/c)$)"
)


class WorkflowError(RuntimeError):
    pass


def _run(
    argv: Sequence[str], *, cwd: Path, timeout: int = 120,
    env: dict[str, str] | None = None, check: bool = False,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(argv), cwd=cwd, env=env, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="strict", timeout=timeout, shell=False,
    )
    if check and result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        raise WorkflowError(f"command failed ({result.returncode}): {message}")
    return result


def _git(root: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return _run(["git", "-c", "core.hooksPath=NUL", *args], cwd=root, check=check)


def _git_bytes(root: Path, *args: str) -> bytes:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=NUL", *args], cwd=root,
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        shell=False,
    )
    if result.returncode != 0:
        raise WorkflowError(
            f"Git command failed ({result.returncode}): "
            + result.stderr.decode("utf-8", errors="replace").strip()
        )
    return result.stdout


def _json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise WorkflowError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise WorkflowError(f"{path} must contain one JSON object")
    return value


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    try:
        with temporary.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical_digest(value: Any) -> str:
    payload = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8") + b"\n"
    return _sha256_bytes(payload)


def _head(root: Path) -> str:
    value = _git(root, "rev-parse", "--verify", "HEAD^{commit}").stdout.strip()
    if HEX40_RE.fullmatch(value) is None:
        raise WorkflowError("HEAD is not one commit")
    return value


def _branch(root: Path) -> str:
    value = _git(root, "symbolic-ref", "--quiet", "--short", "HEAD").stdout.strip()
    if not value:
        raise WorkflowError("worktree is detached")
    return value


def _status(root: Path, *, include_untracked: bool = True) -> list[str]:
    mode = "all" if include_untracked else "no"
    output = _git(root, "status", "--porcelain=v1", f"--untracked-files={mode}").stdout
    return [line for line in output.splitlines() if line]


def _worktrees(root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    for line in _git(root, "worktree", "list", "--porcelain").stdout.splitlines():
        if line.startswith("worktree "):
            if current is not None:
                rows.append(current)
            current = {"path": line[9:]}
        elif current is not None and " " in line:
            key, value = line.split(" ", 1)
            current[key] = value
    if current is not None:
        rows.append(current)
    return rows


def _main_root(root: Path) -> Path:
    rows = [row for row in _worktrees(root) if row.get("branch") == "refs/heads/main"]
    if len(rows) != 1:
        raise WorkflowError("repository must have exactly one registered main worktree")
    return Path(rows[0]["path"]).resolve(strict=True)


def _assert_main(root: Path) -> None:
    if root.resolve() != _main_root(root) or _branch(root) != "main":
        raise WorkflowError("owner command must run from the main worktree")
    if _status(root):
        raise WorkflowError("main worktree must be clean")


def _ledger(root: Path) -> dict[str, Any]:
    return _json(root / LEDGER_RELATIVE)


def _item(ledger: dict[str, Any], item_id: str) -> dict[str, Any]:
    rows = [row for row in ledger["workItems"] if row.get("id") == item_id]
    if len(rows) != 1:
        raise WorkflowError(f"work item {item_id} is missing or duplicated")
    return rows[0]


def _portable(path: str) -> str:
    pure = PurePosixPath(path)
    if (
        not path or "\\" in path or path.startswith("/")
        or re.match(r"^[A-Za-z]:", path)
        or any(part in {"", ".", ".."} for part in pure.parts)
    ):
        raise WorkflowError(f"non-portable path: {path!r}")
    return path.rstrip("/")


def _inside(path: str, root: str) -> bool:
    return path == root or path.startswith(root.rstrip("/") + "/")


def _paths_overlap(left: str, right: str) -> bool:
    return _inside(left, right) or _inside(right, left)


def _owner_only(path: str, policy: dict[str, Any]) -> bool:
    owner = policy["ownerOnly"]
    normalized = path.rstrip("/")
    if any(_inside(normalized, root) for root in owner["roots"]):
        return True
    if any(_inside(normalized, file) for file in owner["files"]):
        return True
    return bool(owner["selectionFiles"] and PurePosixPath(normalized).name == "selection.json")


def _changed_paths(root: Path, base: str) -> list[str]:
    tracked = _git(
        root, "diff", "--no-renames", "--name-only", "--diff-filter=ACDMRTUXB",
        base, "--",
    ).stdout
    untracked = _git(root, "ls-files", "--others", "--exclude-standard").stdout
    paths = sorted({line.strip().replace("\\", "/") for line in (tracked + untracked).splitlines() if line.strip()})
    return [_portable(path) for path in paths]


def _worktree_state_digest(root: Path, base: str) -> str:
    """Bind the exact tracked diff and every non-ignored untracked file byte."""

    digest = hashlib.sha256()
    diff = _git_bytes(
        root, "diff", "--binary", "--no-ext-diff", "--no-renames", base, "--",
    )
    digest.update(diff)
    for path in sorted(_git(root, "ls-files", "--others", "--exclude-standard").stdout.splitlines()):
        portable = _portable(path.replace("\\", "/"))
        candidate = root.joinpath(*PurePosixPath(portable).parts)
        if not candidate.is_file() or candidate.is_symlink():
            raise WorkflowError(f"untracked path must be one regular file: {portable}")
        digest.update(portable.encode("utf-8") + b"\0")
        digest.update(candidate.read_bytes())
    return digest.hexdigest()


def _check_scope(
    root: Path, base: str, owned_paths: list[str], policy: dict[str, Any],
) -> list[str]:
    owned = [_portable(path) for path in owned_paths]
    changed = _changed_paths(root, base)
    outside = [path for path in changed if not any(_inside(path, allowed) for allowed in owned)]
    canonical = [path for path in changed if _owner_only(path, policy)]
    workspace = [path for path in changed if _inside(path, "workspace")]
    if outside:
        raise WorkflowError("changed path is outside ownership: " + ", ".join(outside))
    if canonical:
        raise WorkflowError("worker changed owner-only state: " + ", ".join(canonical))
    if workspace:
        raise WorkflowError("workspace material became trackable: " + ", ".join(workspace))
    return changed


def _assignment_path(root: Path, item_id: str) -> Path:
    return root / "workspace" / "logs" / item_id / "assignment.json"


def _receipt_path(root: Path, item_id: str, name: str) -> Path:
    return root / "workspace" / "logs" / item_id / name


def _load_assignment(root: Path) -> dict[str, Any]:
    candidates = list((root / "workspace" / "logs").glob("*/assignment.json"))
    if len(candidates) != 1:
        raise WorkflowError("lane must contain exactly one private assignment.json")
    assignment = _json(candidates[0])
    if assignment.get("schema") != ASSIGNMENT_SCHEMA or assignment.get("schemaVersion") != 1:
        raise WorkflowError("assignment receipt schema differs")
    main_root = _main_root(root)
    owner_copy = _json(_assignment_path(main_root, assignment.get("itemId", "invalid")))
    if owner_copy != assignment:
        raise WorkflowError("lane assignment differs from the owner copy")
    if Path(assignment["mainPath"]).resolve() != main_root:
        raise WorkflowError("assignment main path differs from registered main")
    if Path(assignment["lanePath"]).resolve() != root.resolve():
        raise WorkflowError("assignment lane path differs from this worktree")
    expected_branch = "work/" + assignment["itemId"].casefold()
    if assignment["branch"] != expected_branch or _branch(root) != expected_branch:
        raise WorkflowError("assignment branch does not match the item ID")
    subject = _git(
        root, "log", "-1", "--format=%s", assignment["assignmentCommit"],
    ).stdout.strip()
    if subject != f"orchestration: assign {assignment['itemId']} to {assignment['assignee']}":
        raise WorkflowError("assignment commit subject differs from assignment identity")
    ancestry = _git(
        root, "merge-base", "--is-ancestor", assignment["assignmentCommit"], "HEAD",
        check=False,
    )
    if ancestry.returncode != 0:
        raise WorkflowError("assignment commit is not an ancestor of the lane")
    return assignment


def _ready_rows(ledger: dict[str, Any]) -> list[dict[str, Any]]:
    by_id = {row["id"]: row for row in ledger["workItems"]}
    policy = ledger["assignmentPolicy"]
    active_paths = [
        path
        for active in ledger["workItems"]
        if active["status"] == "in-progress"
        for path in active["ownership"]["ownedPaths"]
    ]
    rows: list[dict[str, Any]] = []
    for row in ledger["workItems"]:
        ownership = row["ownership"]
        candidates = ownership["candidatePaths"]
        if (
            row["allocationClass"] != "worker-lane"
            or row["status"] not in {"pending", "blocked"}
            or ownership["state"] != "unassigned"
            or ownership["assignee"] is not None
            or ownership["ownedPaths"]
            or not candidates
            or row["blockers"]
            or any(by_id[dep]["status"] != "complete" for dep in row["dependsOn"])
            or any(_owner_only(_portable(path), policy) for path in candidates)
            or any(
                _paths_overlap(_portable(candidate), _portable(active))
                for candidate in candidates for active in active_paths
            )
        ):
            continue
        rows.append(row)
    return sorted(rows, key=lambda row: ({"P0": 0, "P1": 1, "P2": 2}[row["priority"]], row["id"]))


def ready(main_root: Path, *, json_output: bool) -> None:
    ledger = _ledger(main_root)
    rows = _ready_rows(ledger)
    payload = {
        "next": rows[0]["id"] if rows else None,
        "count": len(rows),
        "items": [row["id"] for row in rows],
    }
    if json_output:
        print(json.dumps(payload, sort_keys=True, separators=(",", ":")))
    else:
        print(f"WORK_ITEM_READY PASS next={payload['next'] or 'none'} count={payload['count']}")


def create(main_root: Path, *, item_id: str, assignee: str) -> None:
    _assert_main(main_root)
    if not assignee.strip() or any(c in assignee for c in "\r\n\0"):
        raise WorkflowError("assignee must be one non-empty line")
    ledger = _ledger(main_root)
    row = _item(ledger, item_id)
    branch = "work/" + item_id.casefold()
    lane_root = main_root.parent / "open-bfme-lanes" / item_id
    registered = _worktrees(main_root)
    path_rows = [
        entry for entry in registered
        if Path(entry["path"]).resolve() == lane_root.resolve()
    ]
    branch_rows = [
        entry for entry in registered
        if entry.get("branch") == f"refs/heads/{branch}"
    ]
    branch_exists = _git(
        main_root, "show-ref", "--verify", "--quiet", f"refs/heads/{branch}",
        check=False,
    ).returncode == 0
    resuming = (
        row["status"] == "in-progress"
        and row["ownership"]["state"] == "assigned"
        and row["allocationClass"] == "worker-lane"
        and row["ownership"]["assignee"] == assignee
        and bool(row["ownership"]["ownedPaths"])
    )
    if row in _ready_rows(ledger):
        if lane_root.exists() or branch_exists or path_rows or branch_rows:
            raise WorkflowError("lane path or branch already exists before assignment")
        owned = [_portable(path) for path in row["ownership"]["candidatePaths"]]
        for active in ledger["workItems"]:
            if active["id"] == item_id or active["status"] != "in-progress":
                continue
            for candidate in owned:
                for active_path in active["ownership"]["ownedPaths"]:
                    if _paths_overlap(candidate, _portable(active_path)):
                        raise WorkflowError(
                            f"owned path overlaps active {active['id']}: {candidate}"
                        )
        row["status"] = "in-progress"
        row["ownership"]["state"] = "assigned"
        row["ownership"]["assignee"] = assignee
        row["ownership"]["ownedPaths"] = owned
        row["blockers"] = []
        ledger_path = main_root / LEDGER_RELATIVE
        original_ledger = ledger_path.read_bytes()
        index_entry = _git(
            main_root, "ls-files", "-s", "--", LEDGER_RELATIVE.as_posix(),
        ).stdout.strip().split()
        if len(index_entry) < 4 or index_entry[2] != "0":
            raise WorkflowError("canonical ledger index entry is not ordinary")
        _validate_ledger_documents(main_root, ledger)
        committed = False
        _write_json(ledger_path, ledger)
        try:
            checker = main_root / "tools" / "check-work-items.py"
            python = _python_executable(main_root)
            result = _run([str(python), "-I", "-S", "-B", str(checker), "--check"], cwd=main_root)
            if result.returncode != 0:
                raise WorkflowError((result.stderr or result.stdout).strip())
            _git(main_root, "add", "--", LEDGER_RELATIVE.as_posix())
            _git(main_root, "commit", "-m", f"orchestration: assign {item_id} to {assignee}")
            committed = True
        except Exception as exc:
            if not committed:
                temporary = ledger_path.with_name(ledger_path.name + f".rollback-{os.getpid()}")
                temporary.write_bytes(original_ledger)
                os.replace(temporary, ledger_path)
                _git(
                    main_root, "update-index", "--cacheinfo",
                    f"{index_entry[0]},{index_entry[1]},{LEDGER_RELATIVE.as_posix()}",
                )
            raise WorkflowError(
                "assignment failed; owner must inspect the canonical ledger/index"
            ) from exc
        assignment_commit = _head(main_root)
    elif resuming:
        owned = [_portable(path) for path in row["ownership"]["ownedPaths"]]
        assignment_commit = _head(main_root)
        subject = _git(main_root, "log", "-1", "--format=%s").stdout.strip()
        if subject != f"orchestration: assign {item_id} to {assignee}":
            raise WorkflowError("resumable assignment is not the current main commit")
    else:
        raise WorkflowError(f"work item {item_id} is not dependency-ready or resumable")
    lane_root.parent.mkdir(parents=True, exist_ok=True)
    if resuming:
        if len(path_rows) > 1 or len(branch_rows) > 1:
            raise WorkflowError("resumable lane path or branch is registered more than once")
        if path_rows or branch_rows:
            if (
                len(path_rows) != 1 or len(branch_rows) != 1
                or path_rows[0] != branch_rows[0]
                or not lane_root.is_dir()
            ):
                raise WorkflowError("resumable lane path/branch is bound to a foreign worktree")
        elif branch_exists:
            if lane_root.exists():
                raise WorkflowError("unregistered resumable lane path already exists")
            branch_head = _git(main_root, "rev-parse", "--verify", branch).stdout.strip()
            if branch_head != assignment_commit:
                raise WorkflowError("resumable branch does not point at the assignment")
            _git(main_root, "worktree", "add", str(lane_root), branch)
        else:
            if lane_root.exists():
                raise WorkflowError("unregistered resumable lane path already exists")
            _git(main_root, "worktree", "add", "-b", branch, str(lane_root), assignment_commit)
    else:
        _git(main_root, "worktree", "add", "-b", branch, str(lane_root), assignment_commit)
    assignment = {
        "schema": ASSIGNMENT_SCHEMA,
        "schemaVersion": 1,
        "itemId": item_id,
        "assignee": assignee,
        "assignmentCommit": assignment_commit,
        "branch": branch,
        "mainPath": str(main_root),
        "lanePath": str(lane_root.resolve()),
        "ownedPaths": owned,
        "workItemSha256": _canonical_digest(row),
        "commandsSha256": _canonical_digest(row["verificationCommands"]),
    }
    _write_json(_assignment_path(main_root, item_id), assignment)
    _write_json(_assignment_path(lane_root, item_id), assignment)
    brief = (
        f"# {item_id}\n\nAssignee: `{assignee}`\n\n"
        "Owned paths:\n" + "\n".join(f"- `{path}`" for path in owned) + "\n\n"
        f"Acceptance: {json.dumps(row['acceptance'], ensure_ascii=False, indent=2)}\n"
    )
    brief_path = lane_root / "workspace" / "logs" / item_id / "brief.md"
    brief_path.parent.mkdir(parents=True, exist_ok=True)
    brief_path.write_text(brief, encoding="utf-8", newline="\n")
    print(f"WORK_ITEM_CREATE PASS id={item_id} branch={branch} lane={lane_root}")


def _python_executable(main_root: Path) -> Path:
    candidates = [
        main_root / "workspace/retail-work/tools/python-3.12-env/Scripts/python.exe",
        main_root / "workspace/retail-work/tools/cpython-3.12.13/python.exe",
    ]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise WorkflowError("pinned private Python runtime is missing")


def _validate_ledger_documents(main_root: Path, ledger: dict[str, Any]) -> None:
    import importlib.util

    checker_path = main_root / "tools/check-work-items.py"
    spec = importlib.util.spec_from_file_location("openbfme_assignment_checker", checker_path)
    if spec is None or spec.loader is None:
        raise WorkflowError("cannot load the canonical ledger checker")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    checker.validate_documents(
        ledger,
        _json(main_root / "contracts/rotwk-202-v9.7.7-product-scope.json"),
        _json(main_root / "contracts/rotwk-202-v9.7.7-baseline.json"),
    )


def _powershell_executable() -> Path:
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    path = system_root / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if not path.is_file():
        raise WorkflowError("System32 Windows PowerShell is missing")
    return path.resolve()


def _step_argv(
    step: dict[str, Any], *, main_root: Path, lane_root: Path,
) -> list[str]:
    role = step["toolRole"]
    invocation = step["invocation"]
    target = step["target"]
    arguments = list(step["args"])
    for argument in arguments:
        if FORBIDDEN_ARGUMENT_RE.search(argument):
            raise WorkflowError(f"ledger command contains forbidden argument: {argument!r}")
    if invocation == "script":
        target_path = lane_root.joinpath(*PurePosixPath(_portable(target)).parts)
        if not target_path.is_file():
            raise WorkflowError(f"structured command target is missing: {target}")
    if role in {"python", "retail-python"}:
        python = _python_executable(main_root)
        if invocation == "script":
            return [str(python), "-I", "-B", str(target_path), *arguments]
        if invocation == "module" and target in {"pytest", "unittest"}:
            return [str(python), "-I", "-B", "-m", target, *arguments]
    if role == "powershell" and invocation == "script":
        return [
            str(_powershell_executable()), "-NoLogo", "-NoProfile", "-NonInteractive",
            "-ExecutionPolicy", "Bypass", "-File", str(target_path), *arguments,
        ]
    raise WorkflowError(f"unsupported structured command: {role}/{invocation}/{target}")


def _run_checks(
    lane_root: Path, *, assignment: dict[str, Any], role: str, reviewer: str | None,
) -> dict[str, Any]:
    main_root = Path(assignment["mainPath"]).resolve(strict=True)
    ledger = _ledger(main_root)
    row = _item(ledger, assignment["itemId"])
    if _canonical_digest(row) != assignment["workItemSha256"]:
        raise WorkflowError("canonical work-item row changed; assignment is revoked")
    if _canonical_digest(row["verificationCommands"]) != assignment["commandsSha256"]:
        raise WorkflowError("canonical verification commands changed")
    if (
        row["ownership"]["assignee"] != assignment["assignee"]
        or row["ownership"]["ownedPaths"] != assignment["ownedPaths"]
        or row["ownership"]["state"] != "assigned"
    ):
        raise WorkflowError("canonical assignment fields changed")
    for protected in (
        LEDGER_RELATIVE.as_posix(), "tools/work-item.py", "tools/work-item.ps1",
        "tools/check-work-items.py",
    ):
        tracked = _git(
            main_root, "ls-files", "--error-unmatch", "--", protected,
            check=False,
        )
        dirty = _git(
            main_root, "status", "--porcelain=v1", "--untracked-files=all", "--",
            protected,
        ).stdout
        if tracked.returncode != 0 or dirty:
            raise WorkflowError(f"main control-plane path is dirty: {protected}")
    if _branch(lane_root) != assignment["branch"]:
        raise WorkflowError("lane branch differs from assignment")
    base = assignment["assignmentCommit"]
    changed_before = _check_scope(lane_root, base, assignment["ownedPaths"], ledger["assignmentPolicy"])
    state_before = _worktree_state_digest(lane_root, base)
    environment = dict(os.environ)
    environment.update({
        "PYTHONDONTWRITEBYTECODE": "1",
        "PYTHONNOUSERSITE": "1",
        "PYTEST_DISABLE_PLUGIN_AUTOLOAD": "1",
    })
    runs: list[dict[str, Any]] = []
    for command_index, command in enumerate(row["verificationCommands"], start=1):
        stdout_parts: list[str] = []
        stderr_parts: list[str] = []
        step_rows: list[dict[str, Any]] = []
        command_started = datetime.now(timezone.utc)
        for step_index, step in enumerate(command["steps"], start=1):
            argv = _step_argv(step, main_root=main_root, lane_root=lane_root)
            try:
                result = _run(
                    argv, cwd=lane_root, timeout=int(command["timeoutSeconds"]),
                    env=environment,
                )
            except subprocess.TimeoutExpired as exc:
                raise WorkflowError(f"verification command {command_index} timed out") from exc
            stdout_parts.append(result.stdout)
            stderr_parts.append(result.stderr)
            step_rows.append({
                "index": step_index,
                "stepSha256": _canonical_digest(step),
                "executableSha256": _sha256_bytes(Path(argv[0]).read_bytes()),
                "exitCode": result.returncode,
            })
            if result.returncode != 0:
                raise WorkflowError(
                    f"verification command {command_index} step {step_index} failed: "
                    + (result.stderr or result.stdout).strip()
                )
        stdout = "".join(stdout_parts)
        stderr = "".join(stderr_parts)
        cursor = 0
        for marker in command["expectedMarkers"]:
            position = stdout.find(marker, cursor)
            if position < 0:
                raise WorkflowError(f"verification command {command_index} missing marker {marker!r}")
            cursor = position + len(marker)
        combined = (stdout + "\n" + stderr).casefold()
        forbidden = ledger["verificationPolicies"][command["diagnosticPolicy"]]["forbiddenDiagnostics"]
        hits = [diagnostic for diagnostic in forbidden if diagnostic.casefold() in combined]
        if hits:
            raise WorkflowError(
                f"verification command {command_index} emitted forbidden diagnostics: "
                + ", ".join(hits)
            )
        log_root = lane_root / "workspace" / "logs" / assignment["itemId"]
        stdout_path = log_root / f"command-{command_index:02d}.stdout.txt"
        stderr_path = log_root / f"command-{command_index:02d}.stderr.txt"
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stdout_path.write_text(stdout, encoding="utf-8", newline="\n")
        stderr_path.write_text(stderr, encoding="utf-8", newline="\n")
        runs.append({
            "index": command_index,
            "commandSha256": _canonical_digest(command),
            "steps": step_rows,
            "stdoutSha256": _sha256_bytes(stdout.encode("utf-8")),
            "stderrSha256": _sha256_bytes(stderr.encode("utf-8")),
            "startedAtUtc": command_started.isoformat().replace("+00:00", "Z"),
            "result": "PASS",
        })
    changed_after = _check_scope(lane_root, base, assignment["ownedPaths"], ledger["assignmentPolicy"])
    if changed_after != changed_before or _worktree_state_digest(lane_root, base) != state_before:
        raise WorkflowError("worktree bytes changed during verification")
    dimensions = {
        name: ("UNPROVED" if row["acceptance"]["requiredOutputDimensions"][name] == "REQUIRED" else "NOT_REQUIRED")
        for name in DIMENSIONS
    }
    artifact_digests: list[dict[str, str]] = []
    for artifact in row["artifacts"]:
        portable = _portable(artifact)
        lane_artifact = lane_root.joinpath(*PurePosixPath(portable).parts)
        main_artifact = main_root.joinpath(*PurePosixPath(portable).parts)
        candidate = lane_artifact if lane_artifact.is_file() else main_artifact
        if candidate.is_file() and not candidate.is_symlink():
            artifact_digests.append({
                "path": portable,
                "sha256": _sha256_bytes(candidate.read_bytes()),
            })
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "schemaVersion": 1,
        "role": role,
        "itemId": assignment["itemId"],
        "assignee": assignment["assignee"],
        "reviewer": reviewer,
        "baseCommit": base,
        "commit": _head(lane_root),
        "branch": assignment["branch"],
        "ownedPaths": assignment["ownedPaths"],
        "changedPaths": changed_after,
        "workItemSha256": assignment["workItemSha256"],
        "commandsSha256": assignment["commandsSha256"],
        "commandRuns": runs,
        "declaredArtifacts": row["artifacts"],
        "artifactDigests": artifact_digests,
        "missingArtifacts": [
            path for path in row["artifacts"]
            if path not in {artifact["path"] for artifact in artifact_digests}
        ],
        "dimensions": dimensions,
        "status": "provisional",
        "result": "PASS",
    }
    name = "independent-review.json" if role == "independent" else "check.json"
    _write_json(_receipt_path(lane_root, assignment["itemId"], name), receipt)
    return receipt


def check(lane_root: Path) -> None:
    assignment = _load_assignment(lane_root)
    receipt = _run_checks(lane_root, assignment=assignment, role="worker", reviewer=None)
    print(f"WORK_ITEM_CHECK PASS id={receipt['itemId']} commit={receipt['commit']}")


def handoff(lane_root: Path) -> None:
    assignment = _load_assignment(lane_root)
    base = assignment["assignmentCommit"]
    count = int(_git(lane_root, "rev-list", "--count", f"{base}..HEAD").stdout.strip())
    merges = _git(lane_root, "rev-list", "--merges", f"{base}..HEAD").stdout.strip()
    if count != 1 or merges:
        raise WorkflowError("handoff requires exactly one non-merge implementation commit")
    if _status(lane_root):
        raise WorkflowError("handoff requires a clean worktree and index")
    receipt = _run_checks(lane_root, assignment=assignment, role="worker", reviewer=None)
    if not receipt["changedPaths"]:
        raise WorkflowError("handoff implementation commit has no changed paths")
    handoff_receipt = {**receipt, "status": "handoff"}
    _write_json(_receipt_path(lane_root, assignment["itemId"], "handoff.json"), handoff_receipt)
    print(f"WORK_ITEM_HANDOFF PASS id={receipt['itemId']} commit={receipt['commit']}")


def review(lane_root: Path, *, reviewer: str) -> None:
    assignment = _load_assignment(lane_root)
    if not reviewer.strip() or reviewer == assignment["assignee"]:
        raise WorkflowError("independent reviewer must differ from the assignee")
    handoff_receipt = _json(_receipt_path(lane_root, assignment["itemId"], "handoff.json"))
    if handoff_receipt.get("result") != "PASS" or handoff_receipt.get("commit") != _head(lane_root):
        raise WorkflowError("current lane commit lacks a matching PASS handoff")
    receipt = _run_checks(
        lane_root, assignment=assignment, role="independent", reviewer=reviewer,
    )
    if receipt["commit"] != handoff_receipt["commit"]:
        raise WorkflowError("reviewed commit differs from handoff")
    print(f"WORK_ITEM_REVIEW PASS id={receipt['itemId']} commit={receipt['commit']} reviewer={reviewer}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-root", type=Path, required=True)
    parser.add_argument("--lane-root", type=Path)
    commands = parser.add_subparsers(dest="command", required=True)
    ready_parser = commands.add_parser("ready")
    ready_parser.add_argument("--json", action="store_true")
    create_parser = commands.add_parser("create")
    create_parser.add_argument("--id", required=True)
    create_parser.add_argument("--assignee", required=True)
    commands.add_parser("check")
    commands.add_parser("handoff")
    review_parser = commands.add_parser("review")
    review_parser.add_argument("--reviewer", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    main_root = args.main_root.resolve(strict=True)
    lane_root = args.lane_root.resolve(strict=True) if args.lane_root else None
    try:
        if args.command == "ready":
            ready(main_root, json_output=args.json)
        elif args.command == "create":
            create(main_root, item_id=args.id, assignee=args.assignee)
        elif args.command == "check":
            if lane_root is None:
                raise WorkflowError("check requires one explicit lane root")
            check(lane_root)
        elif args.command == "handoff":
            if lane_root is None:
                raise WorkflowError("handoff requires one explicit lane root")
            handoff(lane_root)
        elif args.command == "review":
            if lane_root is None:
                raise WorkflowError("review requires one explicit lane root")
            review(lane_root, reviewer=args.reviewer)
        return 0
    except (WorkflowError, OSError, ValueError, KeyError, TypeError) as exc:
        print(f"WORK_ITEM FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
