"""Fail-closed validation for the tracked OpenBFME work-item ledger."""

from __future__ import annotations

import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
LEDGER_PATH = ROOT / "orchestration" / "work-items.json"
PRODUCT_PATH = ROOT / "contracts" / "rotwk-202-v9.7.7-product-scope.json"
BASELINE_PATH = ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"

TOP_LEVEL_KEYS = {
    "schema",
    "schemaVersion",
    "asOfDate",
    "authority",
    "target",
    "assignmentPolicy",
    "verificationPolicies",
    "evidenceSources",
    "workItems",
}
AUTHORITY_KEYS = {
    "writeOwner",
    "workerRule",
    "statusValues",
    "ownershipStates",
    "completionRule",
    "selectionRule",
}
TARGET_KEYS = {
    "contractId",
    "productPolicySha256",
    "baselineId",
    "baselineReceiptSha256",
    "archivePolicySha256",
    "catalogSha256",
    "archiveCount",
    "recordCount",
    "claimProfiles",
}
ASSIGNMENT_POLICY_KEYS = {
    "candidatePathsAreNotOwnership",
    "requirementsBeforeInProgress",
    "handoffRequirements",
}
POLICY_KEYS = {
    "terminalStatuses",
    "passRule",
    "forbiddenDiagnostics",
    "skipIsSuccess",
    "artifactRootRule",
}
ITEM_KEYS = {
    "id",
    "title",
    "kind",
    "priority",
    "status",
    "domainIds",
    "rootQueryIds",
    "dependsOn",
    "ownership",
    "scope",
    "sourceEvidence",
    "artifacts",
    "acceptance",
    "verificationCommands",
    "blockers",
}
OWNERSHIP_KEYS = {"state", "assignee", "ownedPaths", "candidatePaths"}
SCOPE_KEYS = {"deliverable", "notIncluded"}
ACCEPTANCE_KEYS = {"requiredLevels", "criteria"}
COMMAND_KEYS = {
    "shell",
    "command",
    "availability",
    "timeoutSeconds",
    "expectedMarkers",
    "diagnosticPolicy",
}
DISPOSITION_EVIDENCE_KEYS = {"path", "action", "reason", "retention"}
DISPOSITION_ACTIONS = {"archived", "removed", "retained"}
COMPLETION_EVIDENCE_REQUIRED_KEYS = {
    "acceptedAtAuditDate",
    "verifier",
    "result",
    "marker",
    "claimBoundary",
}
COMPLETION_EVIDENCE_OPTIONAL_KEYS = {
    "note",
    "supersededPath",
    "replacementPath",
    "retention",
}

STATUS_VALUES = ["pending", "blocked", "in-progress", "verification", "complete"]
OWNERSHIP_STATES = [
    "unassigned",
    "assigned",
    "handoff",
    "independent-verification",
    "accepted",
]
STATUS_OWNERSHIP = {
    "pending": {"unassigned"},
    "blocked": {"unassigned"},
    "in-progress": {"assigned"},
    "verification": {"handoff", "independent-verification"},
    "complete": {"accepted"},
}
PRIORITY_RANK = {"P0": 0, "P1": 1, "P2": 2}
EVIDENCE_LEVEL_RANK = {f"L{level}": level for level in range(7)}
ITEM_ID_RE = re.compile(r"^(P[0-2])-[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-\d{3}$")
EVIDENCE_ID_RE = re.compile(r"^E-[A-Z0-9]+(?:-[A-Z0-9]+)+$")
POLICY_ID_RE = re.compile(r"^[a-z][a-z0-9-]*$")
HEX64_RE = re.compile(r"^[0-9a-f]{64}$")
DRIVE_ABSOLUTE_RE = re.compile(r"(?i)^[a-z]:[\\/]")
COMMAND_DRIVE_RE = re.compile(r"(?i)(?:^|[\s\"'=])[a-z]:[\\/]")
COMMAND_UNC_RE = re.compile(r"(?:^|[\s\"'=])\\\\[^\\\s]+")
COMMAND_POSIX_ABSOLUTE_RE = re.compile(
    r"(?:^|[\s\"'=])/(?:bin|etc|home|mnt|opt|tmp|usr|var)(?:/|\b)", re.I
)
COMMAND_POSIX_SHELL_RE = re.compile(
    r"(?:^|[\s;&|])(?:bash|zsh|sh|wsl)(?:\.exe)?(?=$|[\s;&|])", re.I
)
COMMAND_SH_FILE_RE = re.compile(r"(?:^|[\\/\s\"'])[^\s\"']+\.sh(?=$|[\s\"'])", re.I)


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name}: root must be an object")
    return value


def _policy_digest(document: dict[str, Any]) -> str:
    envelope = dict(document)
    digest = dict(envelope.get("policy_digest", {}))
    digest.pop("value", None)
    envelope["policy_digest"] = digest
    canonical = json.dumps(
        envelope, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def _exact_keys(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label}: expected an object")
    actual = set(value)
    if actual != expected:
        raise ValueError(
            f"{label}: keys differ; missing={sorted(expected - actual)} "
            f"unknown={sorted(actual - expected)}"
        )
    return value


def _nonempty_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label}: expected a non-empty string")
    return value


def _string_list(
    value: Any, label: str, *, allow_empty: bool = False
) -> list[str]:
    if not isinstance(value, list) or (not value and not allow_empty):
        qualifier = "an array" if allow_empty else "a non-empty array"
        raise ValueError(f"{label}: expected {qualifier} of strings")
    if any(not isinstance(row, str) or not row.strip() for row in value):
        raise ValueError(f"{label}: every value must be a non-empty string")
    if len(set(value)) != len(value):
        raise ValueError(f"{label}: duplicate value")
    return value


def _iso_date(value: Any, label: str) -> date:
    text = _nonempty_string(value, label)
    try:
        parsed = date.fromisoformat(text)
    except ValueError as exc:
        raise ValueError(f"{label}: expected YYYY-MM-DD") from exc
    if parsed.isoformat() != text:
        raise ValueError(f"{label}: expected canonical YYYY-MM-DD")
    return parsed


def _portable_path(value: Any, label: str) -> str:
    path = _nonempty_string(value, label)
    filesystem_part = path.split("#", 1)[0]
    if (
        DRIVE_ABSOLUTE_RE.match(filesystem_part)
        or filesystem_part.startswith(("/", "\\", "~"))
        or "file://" in filesystem_part.casefold()
    ):
        raise ValueError(f"{label}: machine-absolute path is forbidden: {path}")
    parts = [part for part in re.split(r"[\\/]", filesystem_part) if part]
    if ".." in parts:
        raise ValueError(f"{label}: parent traversal is forbidden: {path}")
    if filesystem_part.casefold().endswith(".sh"):
        raise ValueError(f"{label}: .sh paths are forbidden: {path}")
    return path


def _portable_command(shell: Any, command: Any, label: str) -> tuple[str, str]:
    shell_text = _nonempty_string(shell, f"{label}.shell")
    command_text = _nonempty_string(command, f"{label}.command")
    if shell_text.casefold() not in {"powershell.exe", "cmd.exe"}:
        raise ValueError(f"{label}: POSIX or unknown shell is forbidden: {shell_text}")
    if (
        COMMAND_DRIVE_RE.search(command_text)
        or COMMAND_UNC_RE.search(command_text)
        or COMMAND_POSIX_ABSOLUTE_RE.search(command_text)
        or "$HOME" in command_text
        or "${HOME}" in command_text
        or "file://" in command_text.casefold()
    ):
        raise ValueError(f"{label}: machine-absolute command path is forbidden")
    if COMMAND_POSIX_SHELL_RE.search(command_text):
        raise ValueError(f"{label}: POSIX shell invocation is forbidden")
    if COMMAND_SH_FILE_RE.search(command_text):
        raise ValueError(f"{label}: .sh command is forbidden")
    return shell_text, command_text


def _stable_ids(rows: Any, pattern: re.Pattern[str], label: str) -> list[str]:
    if not isinstance(rows, list) or not rows:
        raise ValueError(f"{label}: expected a non-empty array")
    ids: list[str] = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"{label}[{index}]: expected an object")
        item_id = row.get("id")
        if not isinstance(item_id, str) or pattern.fullmatch(item_id) is None:
            raise ValueError(f"{label}[{index}]: unstable id {item_id!r}")
        ids.append(item_id)
    if len({item_id.casefold() for item_id in ids}) != len(ids):
        raise ValueError(f"{label}: duplicate id")
    return ids


def _validate_target(
    ledger: dict[str, Any], product: dict[str, Any], baseline: dict[str, Any]
) -> tuple[set[str], set[str]]:
    if ledger.get("schema") != "openbfme.work-items" or ledger.get("schemaVersion") != 1:
        raise ValueError("ledger: schema identity changed")

    stored_product_digest = product.get("policy_digest", {}).get("value")
    actual_product_digest = _policy_digest(product)
    if stored_product_digest != actual_product_digest:
        raise ValueError("product contract: policy digest mismatch")
    if (
        product.get("contract_id") != "openbfme.rotwk-202-v9.7.7.product-scope"
        or product.get("patch") != "2.02 v9.7.7"
    ):
        raise ValueError("product contract: target identity changed")

    if (
        baseline.get("schema") != "openbfme.retail-baseline"
        or baseline.get("schemaVersion") != 1
        or baseline.get("baselineId") != "rotwk-202-v9.7.7-en"
        or baseline.get("product", {}).get("patch") != "2.02"
        or baseline.get("product", {}).get("communityBuild") != "9.7.7"
    ):
        raise ValueError("baseline contract: target identity changed")

    authority = baseline.get("authority")
    if not isinstance(authority, dict):
        raise ValueError("baseline contract: authority must be an object")
    target = _exact_keys(ledger.get("target"), TARGET_KEYS, "target")
    expected_claims = [
        row["id"]
        for row in product.get("claim_profiles", [])
        if isinstance(row, dict)
        and isinstance(row.get("id"), str)
        and row["id"].startswith("rotwk-202-v9.7.7-")
    ]
    expected = {
        "contractId": product["contract_id"],
        "productPolicySha256": actual_product_digest,
        "baselineId": baseline["baselineId"],
        "baselineReceiptSha256": authority.get("receiptSha256"),
        "archivePolicySha256": authority.get("policySha256"),
        "catalogSha256": authority.get("catalogSha256"),
        "archiveCount": authority.get("archiveCount"),
        "recordCount": authority.get("recordCount"),
        "claimProfiles": expected_claims,
    }
    for field, expected_value in expected.items():
        if target.get(field) != expected_value:
            raise ValueError(
                f"target: {field} differs from current contract; "
                f"expected={expected_value!r} actual={target.get(field)!r}"
            )
    for field in (
        "productPolicySha256",
        "baselineReceiptSha256",
        "archivePolicySha256",
        "catalogSha256",
    ):
        if not isinstance(target[field], str) or HEX64_RE.fullmatch(target[field]) is None:
            raise ValueError(f"target: {field} is not a lowercase SHA-256")

    domain_rows = product.get("product_domains")
    root_rows = product.get("root_discovery_queries")
    if not isinstance(domain_rows, list) or not isinstance(root_rows, list):
        raise ValueError("product contract: domain/root arrays are invalid")
    domain_ids = {
        row.get("id") for row in domain_rows if isinstance(row, dict)
    }
    root_ids = {row.get("id") for row in root_rows if isinstance(row, dict)}
    if None in domain_ids or None in root_ids:
        raise ValueError("product contract: domain/root id is missing")
    return domain_ids, root_ids


def _validate_authority(ledger: dict[str, Any]) -> None:
    authority = _exact_keys(ledger.get("authority"), AUTHORITY_KEYS, "authority")
    if authority.get("writeOwner") != "integration-owner":
        raise ValueError("authority: writeOwner must be integration-owner")
    if authority.get("statusValues") != STATUS_VALUES:
        raise ValueError("authority: statusValues changed")
    if authority.get("ownershipStates") != OWNERSHIP_STATES:
        raise ValueError("authority: ownershipStates changed")
    for field in ("workerRule", "completionRule", "selectionRule"):
        _nonempty_string(authority.get(field), f"authority.{field}")

    assignment = _exact_keys(
        ledger.get("assignmentPolicy"),
        ASSIGNMENT_POLICY_KEYS,
        "assignmentPolicy",
    )
    if assignment.get("candidatePathsAreNotOwnership") is not True:
        raise ValueError("assignmentPolicy: candidatePaths must not grant ownership")
    _string_list(
        assignment.get("requirementsBeforeInProgress"),
        "assignmentPolicy.requirementsBeforeInProgress",
    )
    _string_list(
        assignment.get("handoffRequirements"),
        "assignmentPolicy.handoffRequirements",
    )


def _validate_policies(ledger: dict[str, Any]) -> set[str]:
    policies = ledger.get("verificationPolicies")
    if not isinstance(policies, dict) or not policies:
        raise ValueError("verificationPolicies: expected a non-empty object")
    if "strict-default" not in policies:
        raise ValueError("verificationPolicies: strict-default is required")
    required_diagnostics = {
        "ERROR:",
        "SCRIPT ERROR",
        "Parse Error",
        "timeout",
        "crash",
        "fallback",
        "placeholder",
        "source/recipe/selection/state-pin drift",
    }
    for policy_id, raw_policy in policies.items():
        if not isinstance(policy_id, str) or POLICY_ID_RE.fullmatch(policy_id) is None:
            raise ValueError(f"verificationPolicies: unstable id {policy_id!r}")
        policy = _exact_keys(
            raw_policy, POLICY_KEYS, f"verification policy {policy_id}"
        )
        if policy.get("terminalStatuses") != ["PASS", "FAIL", "SKIP"]:
            raise ValueError(
                f"verification policy {policy_id}: terminal statuses changed"
            )
        if policy.get("skipIsSuccess") is not False:
            raise ValueError(f"verification policy {policy_id}: SKIP cannot succeed")
        _nonempty_string(policy.get("passRule"), f"verification policy {policy_id}.passRule")
        diagnostics = set(
            _string_list(
                policy.get("forbiddenDiagnostics"),
                f"verification policy {policy_id}.forbiddenDiagnostics",
            )
        )
        if policy_id == "strict-default" and not required_diagnostics <= diagnostics:
            raise ValueError(
                "verification policy strict-default: required diagnostics are missing"
            )
        artifact_rule = _nonempty_string(
            policy.get("artifactRootRule"),
            f"verification policy {policy_id}.artifactRootRule",
        )
        if not artifact_rule.startswith("workspace/logs/"):
            raise ValueError(
                f"verification policy {policy_id}: artifact root must stay in workspace/logs"
            )
    return set(policies)


def _detect_cycles(graph: dict[str, list[str]], label: str) -> None:
    state: dict[str, int] = {}
    stack: list[str] = []

    def visit(node: str) -> None:
        marker = state.get(node, 0)
        if marker == 2:
            return
        if marker == 1:
            start = stack.index(node)
            cycle = stack[start:] + [node]
            raise ValueError(f"{label}: dependency cycle {' -> '.join(cycle)}")
        state[node] = 1
        stack.append(node)
        for dependency in graph.get(node, []):
            visit(dependency)
        stack.pop()
        state[node] = 2

    for node in graph:
        visit(node)


def _validate_evidence(
    ledger: dict[str, Any],
    product: dict[str, Any],
    baseline: dict[str, Any],
    domain_ids: set[str],
    root: Path,
) -> tuple[set[str], dict[str, dict[str, Any]]]:
    rows = ledger.get("evidenceSources")
    evidence_ids = _stable_ids(rows, EVIDENCE_ID_RE, "evidenceSources")
    evidence_by_id = dict(zip(evidence_ids, rows, strict=True))
    graph: dict[str, list[str]] = {}

    for evidence_id, row in evidence_by_id.items():
        _nonempty_string(row.get("type"), f"evidence {evidence_id}.type")
        _nonempty_string(row.get("claimLimit"), f"evidence {evidence_id}.claimLimit")
        if "asOfDate" in row:
            _iso_date(row["asOfDate"], f"evidence {evidence_id}.asOfDate")
        if "path" in row:
            relative = _portable_path(row["path"], f"evidence {evidence_id}.path")
            if row.get("type") == "tracked-contract" and not (root / relative).is_file():
                raise ValueError(
                    f"evidence {evidence_id}: tracked contract does not exist: {relative}"
                )
        if "sourcePointers" in row:
            for index, pointer in enumerate(
                _string_list(row["sourcePointers"], f"evidence {evidence_id}.sourcePointers")
            ):
                _portable_path(pointer, f"evidence {evidence_id}.sourcePointers[{index}]")
        if "staleArtifacts" in row:
            artifacts = row["staleArtifacts"]
            if not isinstance(artifacts, list) or not artifacts:
                raise ValueError(f"evidence {evidence_id}.staleArtifacts: expected rows")
            for index, artifact in enumerate(artifacts):
                if not isinstance(artifact, dict):
                    raise ValueError(
                        f"evidence {evidence_id}.staleArtifacts[{index}]: expected object"
                    )
                _portable_path(
                    artifact.get("path"),
                    f"evidence {evidence_id}.staleArtifacts[{index}].path",
                )
                if HEX64_RE.fullmatch(str(artifact.get("sha256", ""))) is None:
                    raise ValueError(
                        f"evidence {evidence_id}.staleArtifacts[{index}]: invalid sha256"
                    )
                _nonempty_string(
                    artifact.get("reason"),
                    f"evidence {evidence_id}.staleArtifacts[{index}].reason",
                )
        inputs = _string_list(
            row.get("inputEvidence", []),
            f"evidence {evidence_id}.inputEvidence",
            allow_empty=True,
        )
        graph[evidence_id] = inputs

    evidence_id_set = set(evidence_ids)
    for evidence_id, dependencies in graph.items():
        unknown = set(dependencies) - evidence_id_set
        if unknown:
            raise ValueError(
                f"evidence {evidence_id}: unknown evidence refs {sorted(unknown)}"
            )
        if evidence_id in dependencies:
            raise ValueError(f"evidence {evidence_id}: self dependency")
    _detect_cycles(graph, "evidenceSources")

    baseline_path = "contracts/rotwk-202-v9.7.7-baseline.json"
    product_path = "contracts/rotwk-202-v9.7.7-product-scope.json"
    baseline_rows = [row for row in rows if row.get("path") == baseline_path]
    product_rows = [row for row in rows if row.get("path") == product_path]
    if len(baseline_rows) != 1 or len(product_rows) != 1:
        raise ValueError("evidenceSources: exact baseline and product contract refs required")

    baseline_row = baseline_rows[0]
    baseline_identity = baseline_row.get("identity")
    if not isinstance(baseline_identity, dict):
        raise ValueError("baseline evidence: identity must be an object")
    baseline_authority = baseline["authority"]
    expected_identity = {
        "baselineId": baseline["baselineId"],
        "receiptSha256": baseline_authority["receiptSha256"],
        "policySha256": baseline_authority["policySha256"],
        "catalogSha256": baseline_authority["catalogSha256"],
    }
    if baseline_identity != expected_identity:
        raise ValueError("baseline evidence: identity differs from baseline contract")
    measurements = baseline_row.get("measurements")
    if not isinstance(measurements, dict):
        raise ValueError("baseline evidence: measurements must be an object")
    expected_layers = [row.get("archiveCount") for row in baseline["layers"]]
    for field, expected_value in {
        "layerArchiveCounts": expected_layers,
        "archiveCount": baseline_authority["archiveCount"],
        "recordCount": baseline_authority["recordCount"],
    }.items():
        if measurements.get(field) != expected_value:
            raise ValueError(f"baseline evidence: {field} differs from baseline contract")

    product_row = product_rows[0]
    if product_row.get("productPolicySha256") != product["policy_digest"]["value"]:
        raise ValueError("product evidence: policy digest differs from product contract")
    retail_domains = {
        row["id"] for row in product["product_domains"] if row.get("authority") == "retail"
    }
    openbfme_domains = domain_ids - retail_domains
    if set(product_row.get("retailDomains", [])) != retail_domains:
        raise ValueError("product evidence: retail domain refs differ from product contract")
    if set(product_row.get("openbfmeDomains", [])) != openbfme_domains:
        raise ValueError("product evidence: OpenBFME domain refs differ from product contract")

    return evidence_id_set, evidence_by_id


def _validate_item_shape(
    item: dict[str, Any],
    index: int,
    domain_ids: set[str],
    root_ids: set[str],
    evidence_ids: set[str],
    policy_ids: set[str],
    ledger_date: date,
    evidence_by_id: dict[str, dict[str, Any]],
) -> None:
    item_id = item["id"]
    label = f"work item {item_id}"
    allowed = ITEM_KEYS | {"completionEvidence", "dispositionEvidence"}
    actual = set(item)
    if not ITEM_KEYS <= actual or not actual <= allowed:
        raise ValueError(
            f"{label}: keys differ; missing={sorted(ITEM_KEYS - actual)} "
            f"unknown={sorted(actual - allowed)}"
        )

    _nonempty_string(item.get("title"), f"{label}.title")
    _nonempty_string(item.get("kind"), f"{label}.kind")
    priority = item.get("priority")
    match = ITEM_ID_RE.fullmatch(item_id)
    if priority not in PRIORITY_RANK or match is None or match.group(1) != priority:
        raise ValueError(f"{label}: priority/id prefix mismatch")

    status = item.get("status")
    if status not in STATUS_VALUES:
        raise ValueError(f"{label}: unknown status {status!r}")

    domains = _string_list(item.get("domainIds"), f"{label}.domainIds", allow_empty=True)
    roots = _string_list(
        item.get("rootQueryIds"), f"{label}.rootQueryIds", allow_empty=True
    )
    source_evidence = _string_list(
        item.get("sourceEvidence"), f"{label}.sourceEvidence"
    )
    unknown_domains = set(domains) - domain_ids
    unknown_roots = set(roots) - root_ids
    unknown_evidence = set(source_evidence) - evidence_ids
    if unknown_domains:
        raise ValueError(f"{label}: unknown domain refs {sorted(unknown_domains)}")
    if unknown_roots:
        raise ValueError(f"{label}: unknown root refs {sorted(unknown_roots)}")
    if unknown_evidence:
        raise ValueError(f"{label}: unknown evidence refs {sorted(unknown_evidence)}")

    _string_list(item.get("dependsOn"), f"{label}.dependsOn", allow_empty=True)
    blockers = _string_list(item.get("blockers"), f"{label}.blockers", allow_empty=True)
    if status == "blocked" and not blockers:
        raise ValueError(f"{label}: blocked status requires a named blocker")
    if status in {"in-progress", "complete"} and blockers:
        raise ValueError(f"{label}: {status} status cannot carry blockers")

    ownership = _exact_keys(item.get("ownership"), OWNERSHIP_KEYS, f"{label}.ownership")
    ownership_state = ownership.get("state")
    if ownership_state not in STATUS_OWNERSHIP[status]:
        raise ValueError(
            f"{label}: status {status} is incoherent with ownership {ownership_state}"
        )
    candidate_paths = _string_list(
        ownership.get("candidatePaths"),
        f"{label}.ownership.candidatePaths",
        allow_empty=True,
    )
    owned_paths = _string_list(
        ownership.get("ownedPaths"),
        f"{label}.ownership.ownedPaths",
        allow_empty=True,
    )
    for path_index, path in enumerate(candidate_paths):
        _portable_path(path, f"{label}.ownership.candidatePaths[{path_index}]")
    for path_index, path in enumerate(owned_paths):
        _portable_path(path, f"{label}.ownership.ownedPaths[{path_index}]")

    assignee = ownership.get("assignee")
    if ownership_state in {"assigned", "handoff"}:
        _nonempty_string(assignee, f"{label}.ownership.assignee")
        if not owned_paths:
            raise ValueError(
                f"{label}: candidatePaths are not ownership; active work needs ownedPaths"
            )
    else:
        if assignee is not None and (not isinstance(assignee, str) or not assignee.strip()):
            raise ValueError(f"{label}.ownership.assignee: invalid assignee")
        if owned_paths:
            raise ValueError(
                f"{label}: ownership state {ownership_state} cannot retain ownedPaths"
            )
        if ownership_state in {"unassigned", "accepted"} and assignee is not None:
            raise ValueError(
                f"{label}: ownership state {ownership_state} must not name an assignee"
            )

    scope = _exact_keys(item.get("scope"), SCOPE_KEYS, f"{label}.scope")
    for field in SCOPE_KEYS:
        _nonempty_string(scope.get(field), f"{label}.scope.{field}")

    artifacts = _string_list(item.get("artifacts"), f"{label}.artifacts")
    for artifact_index, artifact in enumerate(artifacts):
        _portable_path(artifact, f"{label}.artifacts[{artifact_index}]")

    if "dispositionEvidence" in item:
        dispositions = item["dispositionEvidence"]
        if not isinstance(dispositions, list) or not dispositions:
            raise ValueError(
                f"{label}.dispositionEvidence: expected a non-empty array"
            )
        disposition_paths: set[str] = set()
        for disposition_index, raw_disposition in enumerate(dispositions):
            disposition_label = (
                f"{label}.dispositionEvidence[{disposition_index}]"
            )
            disposition = _exact_keys(
                raw_disposition, DISPOSITION_EVIDENCE_KEYS, disposition_label
            )
            disposition_path = _portable_path(
                disposition.get("path"), f"{disposition_label}.path"
            )
            normalized_path = _normalized_owned_path(disposition_path)
            if normalized_path in disposition_paths:
                raise ValueError(
                    f"{label}.dispositionEvidence: duplicate disposition path"
                )
            disposition_paths.add(normalized_path)
            action = _nonempty_string(
                disposition.get("action"), f"{disposition_label}.action"
            )
            if action not in DISPOSITION_ACTIONS:
                raise ValueError(
                    f"{disposition_label}.action: unknown disposition action {action!r}"
                )
            for field in ("reason", "retention"):
                _nonempty_string(
                    disposition.get(field), f"{disposition_label}.{field}"
                )
    elif status == "complete" and item.get("kind") == "repository-hygiene":
        raise ValueError(
            f"{label}: complete repository-hygiene status requires dispositionEvidence"
        )

    acceptance = _exact_keys(
        item.get("acceptance"), ACCEPTANCE_KEYS, f"{label}.acceptance"
    )
    levels = _string_list(
        acceptance.get("requiredLevels"), f"{label}.acceptance.requiredLevels"
    )
    if any(level not in EVIDENCE_LEVEL_RANK for level in levels):
        raise ValueError(f"{label}: unknown required evidence level")
    if levels != sorted(levels, key=EVIDENCE_LEVEL_RANK.get):
        raise ValueError(f"{label}: required evidence levels are out of order")
    _string_list(acceptance.get("criteria"), f"{label}.acceptance.criteria")

    commands = item.get("verificationCommands")
    if not isinstance(commands, list) or not commands:
        raise ValueError(f"{label}.verificationCommands: expected a non-empty array")
    for command_index, raw_command in enumerate(commands):
        command_label = f"{label}.verificationCommands[{command_index}]"
        command = _exact_keys(raw_command, COMMAND_KEYS, command_label)
        _portable_command(command.get("shell"), command.get("command"), command_label)
        _nonempty_string(command.get("availability"), f"{command_label}.availability")
        timeout = command.get("timeoutSeconds")
        if not isinstance(timeout, int) or isinstance(timeout, bool) or timeout <= 0:
            raise ValueError(f"{command_label}.timeoutSeconds: expected a positive integer")
        _string_list(command.get("expectedMarkers"), f"{command_label}.expectedMarkers")
        diagnostic_policy = command.get("diagnosticPolicy")
        if diagnostic_policy not in policy_ids:
            raise ValueError(
                f"{command_label}: unknown diagnostic policy {diagnostic_policy!r}"
            )

    completion = item.get("completionEvidence")
    if status == "complete":
        if not isinstance(completion, dict) or not completion:
            raise ValueError(f"{label}: complete status requires completionEvidence")
        completion_keys = set(completion)
        completion_allowed = (
            COMPLETION_EVIDENCE_REQUIRED_KEYS | COMPLETION_EVIDENCE_OPTIONAL_KEYS
        )
        if (
            not COMPLETION_EVIDENCE_REQUIRED_KEYS <= completion_keys
            or not completion_keys <= completion_allowed
        ):
            raise ValueError(
                f"{label}.completionEvidence: keys differ; "
                f"missing={sorted(COMPLETION_EVIDENCE_REQUIRED_KEYS - completion_keys)} "
                f"unknown={sorted(completion_keys - completion_allowed)}"
            )
        accepted_date = _iso_date(
            completion.get("acceptedAtAuditDate"),
            f"{label}.completionEvidence.acceptedAtAuditDate",
        )
        if accepted_date > ledger_date:
            raise ValueError(f"{label}: completion evidence postdates the ledger")
        for field in ("verifier", "marker", "claimBoundary"):
            _nonempty_string(
                completion.get(field), f"{label}.completionEvidence.{field}"
            )
        result = _nonempty_string(
            completion.get("result"), f"{label}.completionEvidence.result"
        )
        if result not in {"ACCEPT", "PASS"}:
            raise ValueError(
                f"{label}.completionEvidence.result: expected ACCEPT or PASS"
            )
        for field in ("note", "retention"):
            if field in completion:
                _nonempty_string(
                    completion.get(field), f"{label}.completionEvidence.{field}"
                )
        for field in ("supersededPath", "replacementPath"):
            if field in completion:
                _portable_path(
                    completion.get(field), f"{label}.completionEvidence.{field}"
                )
        for evidence_id in source_evidence:
            claim_limit = str(evidence_by_id[evidence_id].get("claimLimit", ""))
            if "no row may satisfy" in claim_limit.casefold() or (
                "historical diagnostic only" in claim_limit.casefold()
            ):
                raise ValueError(
                    f"{label}: complete status relies on non-acceptance evidence {evidence_id}"
                )
    elif completion is not None:
        raise ValueError(f"{label}: only complete items may carry completionEvidence")


def _normalized_owned_path(path: str) -> str:
    return path.replace("\\", "/").rstrip("/").casefold()


def _validate_items(
    ledger: dict[str, Any],
    domain_ids: set[str],
    root_ids: set[str],
    evidence_ids: set[str],
    evidence_by_id: dict[str, dict[str, Any]],
    policy_ids: set[str],
    ledger_date: date,
) -> dict[str, int]:
    rows = ledger.get("workItems")
    item_ids = _stable_ids(rows, ITEM_ID_RE, "workItems")
    items = dict(zip(item_ids, rows, strict=True))

    last_priority = -1
    for index, item in enumerate(rows):
        _validate_item_shape(
            item,
            index,
            domain_ids,
            root_ids,
            evidence_ids,
            policy_ids,
            ledger_date,
            evidence_by_id,
        )
        priority = PRIORITY_RANK[item["priority"]]
        if priority < last_priority:
            raise ValueError(
                f"workItems: priority order regressed at {item['id']}"
            )
        last_priority = priority

    graph: dict[str, list[str]] = {}
    for item_id, item in items.items():
        dependencies = item["dependsOn"]
        unknown = set(dependencies) - set(items)
        if unknown:
            raise ValueError(f"work item {item_id}: unknown dependencies {sorted(unknown)}")
        if item_id in dependencies:
            raise ValueError(f"work item {item_id}: self dependency")
        for dependency in dependencies:
            if PRIORITY_RANK[items[dependency]["priority"]] > PRIORITY_RANK[item["priority"]]:
                raise ValueError(
                    f"work item {item_id}: dependency {dependency} has later priority"
                )
        graph[item_id] = dependencies
    _detect_cycles(graph, "workItems")

    for item_id, item in items.items():
        incomplete = [
            dependency
            for dependency in item["dependsOn"]
            if items[dependency]["status"] != "complete"
        ]
        if item["status"] == "blocked" and not incomplete:
            raise ValueError(
                f"work item {item_id}: blocked status requires an incomplete dependency"
            )
        if item["status"] != "blocked" and incomplete:
            raise ValueError(
                f"work item {item_id}: status {item['status']} has incomplete "
                f"dependencies {incomplete}"
            )

    active_paths: list[tuple[str, str]] = []
    for item_id, item in items.items():
        if item["ownership"]["state"] not in {"assigned", "handoff"}:
            continue
        for raw_path in item["ownership"]["ownedPaths"]:
            normalized = _normalized_owned_path(raw_path)
            for other_item, other_path in active_paths:
                wildcard = any(character in normalized + other_path for character in "*?[")
                overlap = normalized == other_path
                if not wildcard:
                    overlap = overlap or normalized.startswith(other_path + "/") or other_path.startswith(normalized + "/")
                if overlap:
                    raise ValueError(
                        f"work item {item_id}: owned path overlaps {other_item}: {raw_path}"
                    )
            active_paths.append((item_id, normalized))

    counts = {status: 0 for status in STATUS_VALUES}
    for item in rows:
        counts[item["status"]] += 1
    return counts


def validate_documents(
    ledger: dict[str, Any],
    product: dict[str, Any],
    baseline: dict[str, Any],
    *,
    root: Path = ROOT,
) -> dict[str, int]:
    _exact_keys(ledger, TOP_LEVEL_KEYS, "ledger")
    ledger_date = _iso_date(ledger.get("asOfDate"), "ledger.asOfDate")
    domain_ids, root_ids = _validate_target(ledger, product, baseline)
    _validate_authority(ledger)
    policy_ids = _validate_policies(ledger)
    evidence_ids, evidence_by_id = _validate_evidence(
        ledger, product, baseline, domain_ids, root
    )
    counts = _validate_items(
        ledger,
        domain_ids,
        root_ids,
        evidence_ids,
        evidence_by_id,
        policy_ids,
        ledger_date,
    )
    counts["items"] = len(ledger["workItems"])
    counts["evidence"] = len(ledger["evidenceSources"])
    return counts


def validate() -> dict[str, int]:
    return validate_documents(
        _load(LEDGER_PATH), _load(PRODUCT_PATH), _load(BASELINE_PATH), root=ROOT
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail-closed validation for orchestration/work-items.json"
    )
    parser.add_argument("--check", action="store_true", help="validate without writing")
    parser.parse_args(argv)
    try:
        counts = validate()
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
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
