from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "tools" / "gate-retail.ps1"
WRAPPER = ROOT / "run_retail_pipeline_tests.bat"


def _gate_text() -> str:
    return GATE.read_text(encoding="utf-8")


def test_batch_wrapper_forwards_only_to_hardened_gate() -> None:
    text = WRAPPER.read_text(encoding="utf-8").casefold()
    assert "tools\\gate-retail.ps1" in text
    assert "%*" in text
    assert "exit /b %errorlevel%" in text


def test_default_profile_is_exact_private_generated_completion_profile() -> None:
    text = _gate_text()
    assert (
        '".private\\retail-work\\profiles\\men-fords-v0-complete.generated.json"'
        in text
    )
    assert '$expectedProfileId = "men-fords-v0-complete-generated"' in text
    assert (
        '$expectedProfileSha256 = '
        '"a67530d230dbbdaefd00c32f58a94fad5fe5b590af18f1193d480c5f93cf7c5c"'
        in text
    )
    assert '$expectedPackId = "bfme2-men-vslice"' in text
    assert "$expectedResourceCount = 378" in text
    assert "$expectedSelectedFileCount = 2538" in text
    assert "$expectedProvenanceEntryCount = 2572" in text
    assert "$expectedSourceArchiveCount = 24" in text
    assert 'Join-Path $repoRoot "importer\\profiles\\men-fords-v0.json"' not in text
    assert '"--profile", "men-fords-v0"' not in text


def test_gate_fails_closed_on_identity_readiness_and_incomplete_marker() -> None:
    text = _gate_text()
    required_fragments = (
        "Test-Path -LiteralPath $path -PathType Leaf",
        "$profileSha256 -eq $expectedProfileSha256",
        "[string]$profileDocument.id -eq $expectedProfileId",
        "[string]$profileDocument.pack.id -eq $expectedPackId",
        "$profileDocument.pack.vertical_slice_complete -is [bool]",
        "-not [bool]$profileDocument.pack.vertical_slice_complete",
        "[bool]$plan.ready",
        "@($plan.missing_required).Count -eq 0",
        "[string]$plan.profile -eq $expectedProfileId",
        "[string]$plan.profile_sha256 -eq $expectedProfileSha256",
        "Measure-Object -Sum).Sum) -eq $expectedProvenanceEntryCount",
        "Generated completion profile changed during planning.",
        "Generated completion profile changed during proof builds.",
        '$env:OPENBFME_CONTENT = $expectedPublishedPack',
        '$env:OPENBFME_CONTENT = $expectedBuildPackPath',
        'runtime pack explicitly selected root=',
    )
    for fragment in required_fragments:
        assert fragment in text

    assert "Ran 998 tests in " in text
    assert r"^OK \(skipped=5\)\s*$" in text
    assert "RETAIL_PACK_RESULT passed=175 failed=0" in text
    assert "RETAIL_SLICE_RESULT passed=208 failed=0" in text
    assert "EXTERNAL_PACK_RESULT passed=64 failed=0" in text


def test_proof_builds_cannot_publish_without_owner_switch() -> None:
    text = _gate_text()
    assert "[switch]$IntegrationOwnerPublish" in text
    assert '$buildArguments += "--no-publish"' in text
    assert "if ($IntegrationOwnerPublish)" in text
    assert '$buildArguments += @("--godot-content-root", $publishRoot)' in text
    assert "} else {" in text
    assert "$first.PSObject.Properties.Name -notcontains 'published_pack'" in text
    assert "$second.PSObject.Properties.Name -notcontains 'published_pack'" in text
    assert 'Invoke-ImporterJson "build_a" $buildArguments' in text
    assert 'Invoke-ImporterJson "build_b" $buildArguments' in text
    direct_build_calls = re.findall(
        r'Invoke-ImporterJson\s+"build_[ab]"\s+@\(', text
    )
    assert direct_build_calls == []


def test_audit_stays_bound_to_frozen_profile_and_resolved_closure() -> None:
    text = _gate_text()
    assert "$builtPackDocument.profile_build_complete -is [bool]" in text
    assert "[bool]$builtPackDocument.profile_build_complete" in text
    assert "@($builtProvenanceDocument.incomplete).Count -eq 0" in text
    assert "$audit.profile -eq $expectedProfileId" in text
    assert "$audit.profile_sha256 -eq $expectedProfileSha256" in text
    assert (
        "[int]$audit.source_archive_count -eq $expectedSourceArchiveCount"
        in text
    )
    assert (
        "[int]$audit.provenance_entry_count -eq $expectedProvenanceEntryCount"
        in text
    )
    assert "[int]$audit.tool_attestation_count -ge 5" in text
    assert "$first.bundle_sha256 -eq $second.bundle_sha256" in text
    assert (
        "[int]$audit.checked_files -eq [int]$first.checked_files" in text
        and "[int]$audit.checked_files -eq [int]$second.checked_files" in text
    )
    assert (
        "[int]$audit.checked_outputs -eq [int]$first.checked_outputs" in text
        and "[int]$audit.checked_outputs -eq [int]$second.checked_outputs" in text
    )


def test_build_and_explicit_release_paths_are_exact() -> None:
    text = _gate_text()
    assert 'Join-Path $stateRoot "packs\\$expectedPackId"' in text
    assert '[IO.Path]::GetFullPath([string]$first.pack) -eq $expectedBuildPackPath' in text
    assert '[IO.Path]::GetFullPath([string]$second.pack) -eq $expectedBuildPackPath' in text
    assert 'Join-Path $repoRoot ".private\\content-packs"' in text
    assert (
        'Join-Path $publishRoot "$expectedPackId\\$($second.bundle_sha256)"'
        in text
    )
    assert 'Join-Path $publishRoot "selection.json"' in text
    assert '$expectedActivePack = "$expectedPackId/$($second.bundle_sha256)"' in text
