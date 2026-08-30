"""Validate the human-readable OpenBFME work-item ledger.

The checker deliberately enforces a small contract. Git, literal owned paths,
one closed command vector, and independent review are the security boundary;
the ledger is not a generated policy language.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
LEDGER = ROOT / "orchestration" / "work-items.json"
PRODUCT = ROOT / "contracts" / "rotwk-202-v9.7.7-product-scope.json"
BASELINE = ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"

TOP_KEYS = {
    "schema", "schemaVersion", "asOfDate", "authority", "target",
    "assignmentPolicy", "verificationPolicies", "evidenceSources", "workItems",
}
ITEM_REQUIRED = {
    "id", "title", "kind", "priority", "allocationClass", "status",
    "domainIds", "rootQueryIds", "dependsOn", "ownership", "scope",
    "sourceEvidence", "artifacts", "acceptance", "verificationCommands", "blockers",
}
ITEM_OPTIONAL = {
    "ownershipPlanning", "dispositionEvidence", "envelopeCompletion",
}
OWNERSHIP_KEYS = {"state", "assignee", "ownedPaths", "candidatePaths"}
COMMAND_KEYS = {
    "steps", "availability", "timeoutSeconds", "expectedMarkers", "diagnosticPolicy",
}
STEP_KEYS = {"toolRole", "invocation", "target", "args", "toolchainProfile"}
DIMENSIONS = ["SOURCE", "CONVERT", "LOAD", "BEHAVIOR", "VISUAL", "AUDIO"]
STATUS_TO_OWNERSHIP = {
    "pending": "unassigned",
    "blocked": "unassigned",
    "in-progress": "assigned",
    "verification": "independent-verification",
    "complete": "accepted",
}
ID_RE = re.compile(r"^P[0-2]-[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-[0-9]{3}(?:-C-[0-9A-F]{16})?$")
MARKER_RE = re.compile(r"^[A-Z][A-Za-z0-9_.:-]*(?: [A-Za-z0-9_.:=/-]+)*$")
FORBIDDEN_ARGUMENT_RE = re.compile(
    r"(?i)(?:^[a-z]:|^[\\/]|^~|\.\.(?:[\\/]|$)|%[^%]+%|"
    r"\$(?:env:|\{?[A-Za-z_])|^[a-z][a-z0-9+.-]*://|^@|"
    r"^(?:--?eval|--?execute|/c)$)"
)

EXPECTED_POLICY = {
    "schema": "openbfme.agent-workflow",
    "schemaVersion": 1,
    "candidatePathsAreNotOwnership": True,
    "lane": {
        "allocationClasses": ["worker-lane", "closure-envelope", "rollup"],
        "workerAllocationClass": "worker-lane",
        "baseBranch": "main",
        "siblingRoot": "../open-bfme-lanes",
        "branchPattern": "work/{item-id-lower}",
        "oneItemPerLane": True,
        "maxImplementationCommits": 1,
        "nestedWorktrees": False,
    },
    "ownerOnly": {
        "roots": ["contracts", "orchestration"],
        "files": ["DIRECTION.md", "PLAN.md", "config/repository-boundaries.json"],
        "selectionFiles": True,
    },
    "paths": {
        "portableRelative": True,
        "insideRepository": True,
        "literalOwnedPaths": True,
        "explicitStage": True,
        "workspaceTracked": False,
    },
    "command": {
        "format": "structured-argv-v1",
        "requiredCommandKeys": [
            "steps", "availability", "timeoutSeconds", "expectedMarkers",
            "diagnosticPolicy",
        ],
        "requiredStepKeys": [
            "toolRole", "invocation", "target", "args", "toolchainProfile",
        ],
        "roles": {
            "python": ["script", "module"],
            "retail-python": ["script", "module"],
            "powershell": ["script"],
        },
        "profileRoles": {
            "python-hermetic-v1": "python",
            "retail-python-hermetic-v1": "retail-python",
            "planner-materialization-v1": "python",
            "powershell-git-readonly-v1": "powershell",
            "powershell-content-retention-v1": "powershell",
            "onboarding-contract-v1": "powershell",
            "rotwk-gate-v1": "powershell",
        },
        "maxCommands": 4,
        "maxStepsPerCommand": 8,
        "shell": False,
        "workingDirectory": "lane-root",
        "exactExpectedMarkers": True,
        "skipIsFailure": True,
    },
    "evidence": {
        "privateRoot": "workspace",
        "logRoot": "workspace/logs/{item-id}",
        "retailTracked": False,
        "workerStatus": "provisional",
        "requiredReceiptFiles": [
            "check.json", "handoff.json", "independent-review.json",
        ],
    },
    "dimensions": DIMENSIONS,
    "merge": {
        "workerMayAccept": False,
        "independentReviewRequired": True,
        "ownerMergeRequired": True,
        "requiredReviewIdentity": "different-from-assignee",
    },
}


def strict_json_loads(payload: str, *, label: str) -> Any:
    def pairs(rows: list[tuple[str, Any]]) -> dict[str, Any]:
        value: dict[str, Any] = {}
        for key, item in rows:
            if key in value:
                raise ValueError(f"{label}: duplicate JSON key {key!r}")
            value[key] = item
        return value

    try:
        return json.loads(payload, object_pairs_hook=pairs)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{label}: invalid JSON: {exc}") from exc


def _load(path: Path) -> dict[str, Any]:
    value = strict_json_loads(path.read_text(encoding="utf-8"), label=path.name)
    if not isinstance(value, dict):
        raise ValueError(f"{path.name}: expected object")
    return value


def _exact(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label}: expected object")
    actual = set(value)
    if actual != keys:
        raise ValueError(
            f"{label}: keys differ; missing={sorted(keys - actual)} unknown={sorted(actual - keys)}"
        )
    return value


def _text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label}: expected non-empty string")
    return value


def _strings(value: Any, label: str, *, unique: bool = True) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(row, str) for row in value):
        raise ValueError(f"{label}: expected string array")
    if unique and len(value) != len(set(value)):
        raise ValueError(f"{label}: duplicate value")
    return value


def _portable_path(value: Any, label: str) -> str:
    path = _text(value, label)
    pure = PurePosixPath(path)
    if (
        "\\" in path or path.startswith("/") or re.match(r"^[A-Za-z]:", path)
        or any(part in {"", ".", ".."} for part in pure.parts)
    ):
        raise ValueError(f"{label}: expected portable repository-relative path")
    return path.rstrip("/")


def _validate_target(target: Any, product: dict[str, Any], baseline: dict[str, Any]) -> None:
    row = _exact(target, {
        "contractId", "productPolicySha256", "baselineId", "baselineReceiptSha256",
        "archivePolicySha256", "catalogSha256", "archiveCount", "recordCount",
        "claimProfiles",
    }, "target")
    authority = baseline["authority"]
    expected = {
        "contractId": product["contract_id"],
        "productPolicySha256": product["policy_digest"]["value"],
        "baselineId": baseline["baselineId"],
        "baselineReceiptSha256": authority["receiptSha256"],
        "archivePolicySha256": authority["policySha256"],
        "catalogSha256": authority["catalogSha256"],
        "archiveCount": authority["archiveCount"],
        "recordCount": authority["recordCount"],
        "claimProfiles": [
            "rotwk-202-v9.7.7-skirmish-complete", "rotwk-202-v9.7.7-complete",
        ],
    }
    if row != expected:
        raise ValueError("target: product or retail baseline identity drifted")


def _validate_command(command: Any, label: str, policies: set[str]) -> None:
    row = _exact(command, COMMAND_KEYS, label)
    steps = row["steps"]
    if not isinstance(steps, list) or not 1 <= len(steps) <= 8:
        raise ValueError(f"{label}.steps: expected 1..8 steps")
    profiles = EXPECTED_POLICY["command"]["profileRoles"]
    roles = EXPECTED_POLICY["command"]["roles"]
    for index, raw in enumerate(steps):
        step_label = f"{label}.steps[{index}]"
        step = _exact(raw, STEP_KEYS, step_label)
        role = _text(step["toolRole"], f"{step_label}.toolRole")
        invocation = _text(step["invocation"], f"{step_label}.invocation")
        profile = _text(step["toolchainProfile"], f"{step_label}.toolchainProfile")
        if role not in roles or invocation not in roles[role]:
            raise ValueError(f"{step_label}: unsupported role/invocation")
        if profiles.get(profile) != role:
            raise ValueError(f"{step_label}: profile does not match role")
        target = _text(step["target"], f"{step_label}.target")
        if invocation == "script":
            _portable_path(target, f"{step_label}.target")
            extension = {"python": ".py", "retail-python": ".py", "powershell": ".ps1"}[role]
            if not target.casefold().endswith(extension):
                raise ValueError(f"{step_label}: script extension differs from role")
        elif target not in {"pytest", "unittest"}:
            raise ValueError(f"{step_label}: module is not allowlisted")
        args = _strings(step["args"], f"{step_label}.args", unique=False)
        if len(args) > 128:
            raise ValueError(f"{step_label}.args: too many arguments")
        for argument in args:
            if not argument or len(argument) > 4096 or any(c in argument for c in "\0\r\n"):
                raise ValueError(f"{step_label}.args: invalid argument")
            if FORBIDDEN_ARGUMENT_RE.search(argument):
                raise ValueError(f"{step_label}.args: forbidden argument {argument!r}")
    timeout = row["timeoutSeconds"]
    if isinstance(timeout, bool) or not isinstance(timeout, int) or not 1 <= timeout <= 43200:
        raise ValueError(f"{label}.timeoutSeconds: expected 1..43200")
    markers = _strings(row["expectedMarkers"], f"{label}.expectedMarkers")
    if not markers or any(
        MARKER_RE.fullmatch(marker) is None
        or (" PASS" not in marker and marker != "DRY RUN")
        or "FAIL" in marker
        or "SKIP" in marker
        for marker in markers
    ):
        raise ValueError(f"{label}.expectedMarkers: invalid terminal marker")
    if row["diagnosticPolicy"] not in policies:
        raise ValueError(f"{label}.diagnosticPolicy: unknown policy")
    _text(row["availability"], f"{label}.availability")


def _validate_item(item: Any, evidence: set[str], policies: set[str]) -> None:
    if not isinstance(item, dict):
        raise ValueError("workItems: expected object rows")
    keys = set(item)
    if not ITEM_REQUIRED <= keys or not keys <= ITEM_REQUIRED | ITEM_OPTIONAL:
        raise ValueError(f"work item: keys differ for {item.get('id', '?')}")
    item_id = _text(item["id"], "work item id")
    if ID_RE.fullmatch(item_id) is None:
        raise ValueError(f"work item {item_id}: invalid id")
    for field in ("title", "kind"):
        _text(item[field], f"work item {item_id}.{field}")
    if item["priority"] not in {"P0", "P1", "P2"}:
        raise ValueError(f"work item {item_id}: invalid priority")
    if item["allocationClass"] not in EXPECTED_POLICY["lane"]["allocationClasses"]:
        raise ValueError(f"work item {item_id}: invalid allocationClass")
    status = item["status"]
    if status not in STATUS_TO_OWNERSHIP:
        raise ValueError(f"work item {item_id}: invalid status")
    ownership = _exact(item["ownership"], OWNERSHIP_KEYS, f"work item {item_id}.ownership")
    if ownership["state"] != STATUS_TO_OWNERSHIP[status]:
        raise ValueError(f"work item {item_id}: status/ownership state differ")
    assignee = ownership["assignee"]
    if status == "in-progress":
        _text(assignee, f"work item {item_id}.assignee")
    elif assignee is not None:
        raise ValueError(f"work item {item_id}: assignee is legal only in-progress")
    owned = [_portable_path(path, f"work item {item_id}.ownedPath") for path in _strings(ownership["ownedPaths"], f"work item {item_id}.ownedPaths")]
    candidates = [_portable_path(path, f"work item {item_id}.candidatePath") for path in _strings(ownership["candidatePaths"], f"work item {item_id}.candidatePaths")]
    if status == "in-progress" and item["allocationClass"] == "worker-lane" and not owned:
        raise ValueError(f"work item {item_id}: assigned lane has no ownedPaths")
    if status != "in-progress" and owned:
        raise ValueError(f"work item {item_id}: unassigned row owns paths")
    if any(path.startswith("workspace/") for path in owned + candidates):
        raise ValueError(f"work item {item_id}: workspace is never a tracked ownership path")
    _strings(item["domainIds"], f"work item {item_id}.domainIds")
    _strings(item["rootQueryIds"], f"work item {item_id}.rootQueryIds")
    _strings(item["dependsOn"], f"work item {item_id}.dependsOn")
    for source in _strings(item["sourceEvidence"], f"work item {item_id}.sourceEvidence"):
        if source not in evidence:
            raise ValueError(f"work item {item_id}: unknown evidence {source}")
    for artifact in _strings(item["artifacts"], f"work item {item_id}.artifacts"):
        _portable_path(artifact, f"work item {item_id}.artifact")
    acceptance = _exact(item["acceptance"], {"requiredLevels", "requiredOutputDimensions", "criteria"}, f"work item {item_id}.acceptance")
    levels = _strings(acceptance["requiredLevels"], f"work item {item_id}.requiredLevels")
    if any(level not in {f"L{index}" for index in range(7)} for level in levels):
        raise ValueError(f"work item {item_id}: invalid evidence level")
    dimensions = _exact(acceptance["requiredOutputDimensions"], set(DIMENSIONS), f"work item {item_id}.dimensions")
    if any(dimensions[name] not in {"REQUIRED", "NOT_REQUIRED"} for name in DIMENSIONS):
        raise ValueError(f"work item {item_id}: invalid dimension disposition")
    if not _strings(acceptance["criteria"], f"work item {item_id}.criteria"):
        raise ValueError(f"work item {item_id}: acceptance criteria are empty")
    commands = item["verificationCommands"]
    if not isinstance(commands, list) or not 1 <= len(commands) <= 4:
        raise ValueError(f"work item {item_id}: expected 1..4 verification commands")
    for index, command in enumerate(commands):
        _validate_command(command, f"work item {item_id}.verificationCommands[{index}]", policies)
    _strings(item["blockers"], f"work item {item_id}.blockers", unique=False)


def validate_documents(
    ledger: dict[str, Any], product: dict[str, Any], baseline: dict[str, Any],
    *, root: Path = ROOT,
) -> dict[str, int]:
    del root
    _exact(ledger, TOP_KEYS, "ledger")
    if ledger["schema"] != "openbfme.work-items" or ledger["schemaVersion"] != 2:
        raise ValueError("ledger: expected openbfme.work-items schemaVersion 2")
    _text(ledger["asOfDate"], "ledger.asOfDate")
    if ledger["assignmentPolicy"] != EXPECTED_POLICY:
        raise ValueError("assignmentPolicy: compact workflow policy drifted")
    authority = _exact(ledger["authority"], {
        "writeOwner", "workerRule", "statusValues", "ownershipStates",
        "completionRule", "selectionRule",
    }, "authority")
    if (
        authority["writeOwner"] != "integration-owner"
        or authority["statusValues"] != list(STATUS_TO_OWNERSHIP)
        or authority["ownershipStates"] != [
            "unassigned", "assigned", "independent-verification", "accepted",
        ]
    ):
        raise ValueError("authority: owner or status values drifted")
    _validate_target(ledger["target"], product, baseline)
    verification = ledger["verificationPolicies"]
    if not isinstance(verification, dict) or not verification:
        raise ValueError("verificationPolicies: expected non-empty object")
    for name, raw in verification.items():
        row = _exact(raw, {"terminalStatuses", "passRule", "forbiddenDiagnostics", "skipIsSuccess", "artifactRootRule"}, f"verificationPolicies.{name}")
        if row["skipIsSuccess"] is not False:
            raise ValueError(f"verificationPolicies.{name}: SKIP cannot pass")
        _strings(row["forbiddenDiagnostics"], f"verificationPolicies.{name}.forbiddenDiagnostics")
    evidence_rows = ledger["evidenceSources"]
    if not isinstance(evidence_rows, list) or not evidence_rows:
        raise ValueError("evidenceSources: expected non-empty array")
    evidence: set[str] = set()
    for index, row in enumerate(evidence_rows):
        if not isinstance(row, dict):
            raise ValueError(f"evidenceSources[{index}]: expected object")
        evidence_id = _text(row.get("id"), f"evidenceSources[{index}].id")
        if evidence_id in evidence:
            raise ValueError(f"evidenceSources: duplicate {evidence_id}")
        evidence.add(evidence_id)
        _text(row.get("type"), f"evidenceSources[{index}].type")
        _text(row.get("claimLimit"), f"evidenceSources[{index}].claimLimit")
        if "path" in row:
            _portable_path(row["path"], f"evidenceSources[{index}].path")
    items = ledger["workItems"]
    if not isinstance(items, list) or not items:
        raise ValueError("workItems: expected non-empty array")
    by_id: dict[str, dict[str, Any]] = {}
    for item in items:
        _validate_item(item, evidence, set(verification))
        if item["id"] in by_id:
            raise ValueError(f"workItems: duplicate {item['id']}")
        by_id[item["id"]] = item
    for item_id, item in by_id.items():
        for dependency in item["dependsOn"]:
            if dependency not in by_id or dependency == item_id:
                raise ValueError(f"work item {item_id}: invalid dependency {dependency}")
    active = [
        item for item in items
        if item["status"] == "in-progress" and item["allocationClass"] == "worker-lane"
    ]
    for index, left in enumerate(active):
        for right in active[index + 1:]:
            for left_path in left["ownership"]["ownedPaths"]:
                for right_path in right["ownership"]["ownedPaths"]:
                    if (
                        left_path == right_path
                        or left_path.startswith(right_path.rstrip("/") + "/")
                        or right_path.startswith(left_path.rstrip("/") + "/")
                    ):
                        raise ValueError(
                            f"active ownership overlaps: {left['id']} and {right['id']}"
                        )
    visiting: set[str] = set()
    visited: set[str] = set()
    def visit(item_id: str) -> None:
        if item_id in visiting:
            raise ValueError(f"work item dependency cycle at {item_id}")
        if item_id in visited:
            return
        visiting.add(item_id)
        for dependency in by_id[item_id]["dependsOn"]:
            visit(dependency)
        visiting.remove(item_id)
        visited.add(item_id)
    for item_id in by_id:
        visit(item_id)
    counts = {status: 0 for status in STATUS_TO_OWNERSHIP}
    for item in items:
        counts[item["status"]] += 1
    counts.update(items=len(items), evidence=len(evidence))
    return counts


def validate() -> dict[str, int]:
    return validate_documents(_load(LEDGER), _load(PRODUCT), _load(BASELINE))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate without writing")
    parser.parse_args(argv)
    try:
        counts = validate()
    except (OSError, KeyError, TypeError, ValueError) as exc:
        print(f"WORK_ITEMS FAIL {exc}", file=sys.stderr)
        return 1
    print(
        "WORK_ITEMS PASS "
        f"items={counts['items']} evidence={counts['evidence']} "
        f"complete={counts['complete']} verification={counts['verification']} "
        f"in_progress={counts['in-progress']} pending={counts['pending']} "
        f"blocked={counts['blocked']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
