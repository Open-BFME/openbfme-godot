"""One focused admission check for the compact autonomous worker workflow."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]


def _module(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


CHECKER = _module("openbfme_check_work_items", "tools/check-work-items.py")
WORKFLOW = _module("openbfme_work_item", "tools/work-item.py")
PONYTAIL_GATE = _module("openbfme_ponytail_gate", "tools/ponytail-git-gate.py")


def _read(relative: str) -> dict:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


class AgentWorkflowContract(unittest.TestCase):
    def test_live_ledger_and_exact_target_validate(self) -> None:
        counts = CHECKER.validate()
        self.assertEqual(counts["items"], 69)
        self.assertGreater(counts["evidence"], 0)

    def test_policy_cannot_self_describe_weaker_validation(self) -> None:
        ledger = _read("orchestration/work-items.json")
        ledger["assignmentPolicy"]["command"]["shell"] = True
        with self.assertRaisesRegex(ValueError, "policy drifted"):
            CHECKER.validate_documents(
                ledger,
                _read("contracts/rotwk-202-v9.7.7-product-scope.json"),
                _read("contracts/rotwk-202-v9.7.7-baseline.json"),
            )

    def test_unknown_work_item_key_is_rejected(self) -> None:
        ledger = _read("orchestration/work-items.json")
        mutated = copy.deepcopy(ledger)
        mutated["workItems"][0]["freeFormCommand"] = "do anything"
        with self.assertRaisesRegex(ValueError, "keys differ"):
            CHECKER.validate_documents(
                mutated,
                _read("contracts/rotwk-202-v9.7.7-product-scope.json"),
                _read("contracts/rotwk-202-v9.7.7-baseline.json"),
            )

    def test_literal_path_boundary(self) -> None:
        self.assertTrue(WORKFLOW._inside("game/foo/bar.gd", "game/foo"))
        self.assertFalse(WORKFLOW._inside("game/foobar.gd", "game/foo"))
        for invalid in ("../escape", r"C:\private", "/absolute", r"a\b"):
            with self.assertRaises(WORKFLOW.WorkflowError):
                WORKFLOW._portable(invalid)

    def test_owner_only_boundary(self) -> None:
        policy = _read("orchestration/work-items.json")["assignmentPolicy"]
        for path in (
            "contracts/example.json",
            "orchestration/work-items.json",
            "DIRECTION.md",
            "game/data/selection.json",
        ):
            self.assertTrue(WORKFLOW._owner_only(path, policy), path)
        self.assertFalse(WORKFLOW._owner_only("game/sim/example.gd", policy))

    def test_workflow_and_launcher_keep_the_small_boundary(self) -> None:
        python_source = (ROOT / "tools/work-item.py").read_text(encoding="utf-8")
        launcher_source = (ROOT / "tools/work-item.ps1").read_text(encoding="utf-8")
        lifecycle_lines = len(python_source.splitlines()) + len(launcher_source.splitlines())
        self.assertLessEqual(lifecycle_lines, 1200)
        self.assertNotIn("shell=True", python_source)
        self.assertIn("owner_copy != assignment", python_source)
        self.assertIn("_worktree_state_digest", python_source)
        self.assertIn("artifactDigests", python_source)
        self.assertIn("reviewer == assignment", python_source)
        self.assertIn("_git_with_hooks", python_source)
        self.assertIn("--verify-installation", python_source)
        for forbidden in ("Invoke-Expression", "Start-Process", "cmd /c", "py -3"):
            self.assertNotIn(forbidden.casefold(), launcher_source.casefold())

    def test_ponytail_approval_is_exact(self) -> None:
        PONYTAIL_GATE._self_test()

    def test_governance_has_no_discarded_hypervisor_contract(self) -> None:
        paths = (
            "AGENTS.md",
            "docs/ONBOARDING.md",
            "docs/VERIFICATION.md",
            "orchestration/README.md",
        )
        combined = "\n".join((ROOT / path).read_text(encoding="utf-8") for path in paths)
        for stale in (
            "PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
            "bootstrapFoundationPolicy",
            "toolchain attestation pipe",
        ):
            self.assertNotIn(stale, combined)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(AgentWorkflowContract)
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=1).run(suite)
    if result.wasSuccessful():
        print("AGENT_WORKFLOW PASS")
        return 0
    print("AGENT_WORKFLOW FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
