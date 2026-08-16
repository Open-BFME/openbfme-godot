from __future__ import annotations

import importlib.util
from pathlib import Path

from openbfme_importer.module_contracts import (
    EXECUTABLE_TYPED_MODULE_EVIDENCE,
    ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE,
)


ROOT = Path(__file__).resolve().parents[2]
MANIFEST_TOOL = ROOT / "tools" / "module-runtime-evidence-manifest.py"
GATE = ROOT / "tools" / "gate-module-runtime-evidence.ps1"
RETAIL_GATE = ROOT / "tools" / "gate-retail.ps1"


def _load_tool():
    spec = importlib.util.spec_from_file_location("module_runtime_evidence_manifest", MANIFEST_TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_manifest_is_exactly_the_executable_evidence_registry() -> None:
    document = _load_tool().build_manifest()
    expected = {
        evidence[1]
        for evidence in (
            *EXECUTABLE_TYPED_MODULE_EVIDENCE.values(),
            *ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE.values(),
        )
    }
    actual = {row["runner"] for row in document["runners"]}
    assert document["schema"] == "openbfme.module-runtime-evidence-gate"
    assert document["runnerCount"] == 44
    assert actual == expected
    assert all((ROOT / path).is_file() for path in actual)


def test_gate_runs_every_dynamic_runner_and_fails_closed() -> None:
    text = GATE.read_text(encoding="utf-8")
    assert "module-runtime-evidence-manifest.py" in text
    assert "--require-count 44" in text
    assert "foreach ($row in $runners)" in text
    assert "Invoke-EvidenceRunner" in text
    assert "failed=0" in text
    assert "WaitForExit($TimeoutSeconds * 1000)" in text
    assert "Working-tree identity changed while executable evidence ran." in text
    assert 'Write-Host "$gate PASS runners=' in text


def test_retail_gate_invokes_the_module_runtime_evidence_gate() -> None:
    text = RETAIL_GATE.read_text(encoding="utf-8")
    assert '"gate-module-runtime-evidence.ps1"' in text
    assert "MODULE_RUNTIME_EVIDENCE_GATE PASS runners=44" in text
