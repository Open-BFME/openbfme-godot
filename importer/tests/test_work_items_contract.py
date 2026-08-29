from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = ROOT / "tools" / "check-work-items.py"
LEDGER_PATH = ROOT / "orchestration" / "work-items.json"
PRODUCT_PATH = ROOT / "contracts" / "rotwk-202-v9.7.7-product-scope.json"
BASELINE_PATH = ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _load_checker():
    spec = importlib.util.spec_from_file_location("check_work_items", CHECKER_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _item(ledger: dict, item_id: str) -> dict:
    return next(row for row in ledger["workItems"] if row["id"] == item_id)


def _evidence(ledger: dict, evidence_id: str) -> dict:
    return next(row for row in ledger["evidenceSources"] if row["id"] == evidence_id)


def test_tracked_work_items_contract_is_fail_closed(monkeypatch, capsys) -> None:
    checker = _load_checker()
    ledger = _load(LEDGER_PATH)
    product = _load(PRODUCT_PATH)
    baseline = _load(BASELINE_PATH)

    live = subprocess.run(
        [sys.executable, str(CHECKER_PATH), "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    assert live.returncode == 0, live.stderr
    assert live.stderr == ""
    assert re.fullmatch(
        rf"WORK_ITEMS PASS items={len(ledger['workItems'])} "
        rf"evidence={len(ledger['evidenceSources'])} complete=\d+ "
        r"verification=\d+ in_progress=\d+ pending=\d+ blocked=\d+",
        live.stdout.strip(),
    )

    counts = checker.validate_documents(ledger, product, baseline, root=ROOT)
    assert counts["items"] == len(ledger["workItems"])
    assert counts["evidence"] == len(ledger["evidenceSources"])

    planned = copy.deepcopy(ledger)
    planned_path = "planned/not-yet-created/work-item-contract-test.gd"
    assert not (ROOT / planned_path).exists()
    _item(planned, "P0-SELECTION-001")["ownership"]["candidatePaths"].append(
        planned_path
    )
    checker.validate_documents(planned, product, baseline, root=ROOT)

    def reject(fragment: str, mutate) -> None:
        changed_ledger = copy.deepcopy(ledger)
        changed_product = copy.deepcopy(product)
        changed_baseline = copy.deepcopy(baseline)
        mutate(changed_ledger, changed_product, changed_baseline)
        with pytest.raises(ValueError, match=re.escape(fragment)):
            checker.validate_documents(
                changed_ledger,
                changed_product,
                changed_baseline,
                root=ROOT,
            )

    reject(
        "ledger: keys differ",
        lambda document, _product, _baseline: document.__setitem__("extra", True),
    )
    reject(
        "ledger: schema identity changed",
        lambda document, _product, _baseline: document.__setitem__(
            "schemaVersion", 2
        ),
    )
    reject(
        "target: productPolicySha256 differs from current contract",
        lambda document, _product, _baseline: document["target"].__setitem__(
            "productPolicySha256", "0" * 64
        ),
    )
    reject(
        "baseline contract: target identity changed",
        lambda _document, _product, contract: contract.__setitem__(
            "baselineId", "rotwk-201-en"
        ),
    )
    reject(
        "workItems: duplicate id",
        lambda document, _product, _baseline: document["workItems"][1].__setitem__(
            "id", document["workItems"][0]["id"]
        ),
    )
    reject(
        "workItems[0]: unstable id",
        lambda document, _product, _baseline: document["workItems"][0].__setitem__(
            "id", "source-one"
        ),
    )
    reject(
        "evidenceSources: duplicate id",
        lambda document, _product, _baseline: document["evidenceSources"][1].__setitem__(
            "id", document["evidenceSources"][0]["id"]
        ),
    )
    reject(
        "evidenceSources[0]: unstable id",
        lambda document, _product, _baseline: document["evidenceSources"][0].__setitem__(
            "id", "evidence one"
        ),
    )
    reject(
        "unknown domain refs",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["domainIds"].append("not-a-product-domain"),
    )
    reject(
        "unknown root refs",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["rootQueryIds"].append("not-a-root-query"),
    )
    reject(
        "unknown evidence refs",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["sourceEvidence"].append("E-NOT-FOUND"),
    )
    reject(
        "unknown evidence refs",
        lambda document, _product, _baseline: _evidence(
            document, "E-AUDIT-CORPUS-20260829"
        ).__setitem__("inputEvidence", ["E-NOT-FOUND"]),
    )
    reject(
        "unknown dependencies",
        lambda document, _product, _baseline: _item(
            document, "P0-CORPUS-001"
        )["dependsOn"].append("P0-NOT-FOUND-999"),
    )
    reject(
        "workItems: dependency cycle",
        lambda document, _product, _baseline: _item(
            document, "P0-SOURCE-001"
        ).__setitem__("dependsOn", ["P0-SOURCE-002"]),
    )
    reject(
        "has later priority",
        lambda document, _product, _baseline: _item(
            document, "P0-CORPUS-001"
        ).__setitem__("dependsOn", ["P1-DATA-001"]),
    )
    reject(
        "priority order regressed",
        lambda document, _product, _baseline: document["workItems"].insert(
            0, document["workItems"].pop()
        ),
    )

    def assign_candidate_only(document: dict, _product: dict, _baseline: dict) -> None:
        row = _item(document, "P0-SELECTION-001")
        row["status"] = "in-progress"
        row["ownership"]["state"] = "assigned"
        row["ownership"]["assignee"] = "agent-1"

    reject("candidatePaths are not ownership", assign_candidate_only)
    reject(
        "status pending is incoherent with ownership assigned",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["ownership"].__setitem__("state", "assigned"),
    )
    reject(
        "complete status requires completionEvidence",
        lambda document, _product, _baseline: _item(
            document, "P0-SOURCE-001"
        ).pop("completionEvidence"),
    )
    reject(
        "complete status relies on non-acceptance evidence",
        lambda document, _product, _baseline: _item(
            document, "P0-SOURCE-001"
        ).__setitem__("sourceEvidence", ["E-AUDIT-CURRENT-20260829"]),
    )

    def note_does_not_override_failure(
        document: dict, _product: dict, _baseline: dict
    ) -> None:
        completion = _item(document, "P0-SOURCE-002")["completionEvidence"]
        completion["note"] = "A note is context, not an acceptance result."
        completion["result"] = "FAIL"

    reject(
        "completionEvidence.result: expected ACCEPT or PASS",
        note_does_not_override_failure,
    )
    reject(
        "completionEvidence: keys differ",
        lambda document, _product, _baseline: _item(
            document, "P0-SOURCE-002"
        )["completionEvidence"].__setitem__("unlockedField", "not allowed"),
    )
    reject(
        "completionEvidence.supersededPath: machine-absolute path is forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SOURCE-002"
        )["completionEvidence"].__setitem__(
            "supersededPath", r"C:\Users\agent\obsolete.json"
        ),
    )

    def complete_hygiene_without_disposition(
        document: dict, _product: dict, _baseline: dict
    ) -> None:
        row = _item(document, "P0-HISTORY-001")
        row["status"] = "complete"
        row["ownership"] = {
            "state": "accepted",
            "assignee": None,
            "ownedPaths": [],
            "candidatePaths": row["ownership"]["candidatePaths"],
        }
        row["blockers"] = []
        row["completionEvidence"] = copy.deepcopy(
            _item(document, "P0-SOURCE-002")["completionEvidence"]
        )
        row.pop("dispositionEvidence", None)

    reject(
        "complete repository-hygiene status requires dispositionEvidence",
        complete_hygiene_without_disposition,
    )
    reject(
        "dispositionEvidence[0]: keys differ",
        lambda document, _product, _baseline: _item(
            document, "P0-HISTORY-001"
        )["dispositionEvidence"][0].__setitem__("unknown", "not allowed"),
    )
    reject(
        "unknown disposition action",
        lambda document, _product, _baseline: _item(
            document, "P0-HISTORY-001"
        )["dispositionEvidence"][0].__setitem__("action", "maybe"),
    )

    def make_pending_with_incomplete_dependency(
        document: dict, _product: dict, _baseline: dict
    ) -> None:
        row = _item(document, "P0-CORPUS-002")
        row["status"] = "pending"
        row["blockers"] = []

    reject("status pending has incomplete dependencies", make_pending_with_incomplete_dependency)

    def make_blocked_without_incomplete_dependency(
        document: dict, _product: dict, _baseline: dict
    ) -> None:
        row = _item(document, "P0-CORPUS-001")
        row["status"] = "blocked"
        row["blockers"] = ["external blocker"]

    reject("blocked status requires an incomplete dependency", make_blocked_without_incomplete_dependency)
    reject(
        "blocked status requires a named blocker",
        lambda document, _product, _baseline: _item(
            document, "P0-CORPUS-002"
        ).__setitem__("blockers", []),
    )
    reject(
        "scope.deliverable: expected a non-empty string",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["scope"].__setitem__("deliverable", ""),
    )
    reject(
        "acceptance.criteria: expected a non-empty array",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["acceptance"].__setitem__("criteria", []),
    )
    reject(
        "verificationCommands: expected a non-empty array",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        ).__setitem__("verificationCommands", []),
    )
    reject(
        "expectedMarkers: expected a non-empty array",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["verificationCommands"][0].__setitem__("expectedMarkers", []),
    )
    reject(
        "unknown diagnostic policy",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["verificationCommands"][0].__setitem__(
            "diagnosticPolicy", "unknown-policy"
        ),
    )
    reject(
        "required diagnostics are missing",
        lambda document, _product, _baseline: document["verificationPolicies"][
            "strict-default"
        ]["forbiddenDiagnostics"].remove("ERROR:"),
    )
    reject(
        "machine-absolute path is forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["ownership"].__setitem__("candidatePaths", [r"C:\Users\agent\file.py"]),
    )
    reject(
        ".sh paths are forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["ownership"].__setitem__("candidatePaths", ["tools/run.sh"]),
    )
    reject(
        "POSIX or unknown shell is forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["verificationCommands"][0].__setitem__("shell", "bash"),
    )
    reject(
        "machine-absolute command path is forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["verificationCommands"][0].__setitem__(
            "command", r"py -3 C:\Users\agent\check.py"
        ),
    )
    reject(
        ".sh command is forbidden",
        lambda document, _product, _baseline: _item(
            document, "P0-SELECTION-001"
        )["verificationCommands"][0].__setitem__("command", "tools/run.sh"),
    )

    for error, marker in (
        (KeyError("missing-key"), "WORK_ITEMS FAIL 'missing-key'"),
        (TypeError("wrong-shape"), "WORK_ITEMS FAIL wrong-shape"),
    ):
        def named_failure(exception=error) -> dict:
            raise exception

        monkeypatch.setattr(checker, "validate", named_failure)
        assert checker.main(["--check"]) == 1
        captured = capsys.readouterr()
        assert captured.out == ""
        assert captured.err.strip() == marker
