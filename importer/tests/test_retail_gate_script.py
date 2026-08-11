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
    profile_match = re.search(
        r'\$expectedProfileSha256 = "([0-9a-f]{64})"', text
    )
    assert profile_match is not None
    assert '$expectedPackId = "bfme2-men-vslice"' in text
    for variable in (
        "expectedResourceCount",
        "expectedSelectedFileCount",
        "expectedProvenanceEntryCount",
        "expectedSourceArchiveCount",
    ):
        assert variable not in text
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
        "Measure-Object -Sum).Sum) -gt 0",
        "Generated completion profile changed during planning.",
        "Generated completion profile changed during proof builds.",
        '$env:OPENBFME_CONTENT = $expectedPublishedPack',
        '$env:OPENBFME_CONTENT = $expectedBuildPackPath',
        'runtime pack explicitly selected root=',
    )
    for fragment in required_fragments:
        assert fragment in text

    assert "$minimumImporterTestCount = 2491" in text
    assert "$maximumImporterSkipCount = 86" in text
    assert "$executedTestCount -ge $minimumImporterTestCount" in text
    assert "$skippedTestCount -le $maximumImporterSkipCount" in text
    assert '"-m", "pytest"' in text
    assert "([1-9][0-9]*) passed[, ]" in text
    assert "[1-9][0-9]* passed, ([0-9]+) skipped" in text
    assert "function Invoke-GodotPassedFloor" in text
    for runner, minimum in (
        ("stage11_12_runner.gd", 26),
        ("stage14_15_sim_runner.gd", 31),
        ("stage15_menu_runner.gd", 22),
        ("retail_pack_runner.gd", 175),
        ("external_pack_runner.gd", 64),
    ):
        assert re.search(
            rf'Invoke-GodotPassedFloor\s+"[^"]+"\s+"{re.escape(runner)}".*\s{minimum}$',
            text,
            re.MULTILINE,
        )


def test_importer_tests_step_runs_the_whole_suite_not_just_unittest() -> None:
    """RE-PINNED 2026-08-04.

    The step ran `python -m unittest discover -s importer/tests`. `unittest`
    cannot collect a module-level `def test_*`, and 73 files in this suite
    define nothing else, so 949 test functions -- including
    `test_playable_unit_compiler` (139), `test_playable_unit_pack_compiler`
    (67), `test_playable_structure_compiler` (64) and
    `test_playable_structure_pack_compiler` (42) -- were outside the gate
    entirely. Reproduce the old blind spot with:

        python -m unittest discover -s importer/tests \
            -p "test_playable_structure_compiler.py" -v    ->  "Ran 0 tests"

    That hole is why two committed fail-open/fail-closed contradictions in the
    structure pack compiler sat red on `main` unnoticed. The step now runs
    pytest, and the floors are the measured pytest numbers (2405 passed +
    86 skipped = 2491 executed) rather than a `unittest`-era baseline.
    """

    text = _gate_text()
    assert '"-m", "pytest"' in text
    # The weaker collector must not come back as an invocation. (The word
    # itself still appears in the step's comment, explaining why it left.)
    assert '"-m", "unittest"' not in text
    assert '"discover"' not in text
    # Run from the repo root: `python -m` puts the working directory on
    # sys.path, which is how `importer.tests.*` fixture imports resolve.
    assert "Push-Location $repoRoot" in text
    assert "Pop-Location" in text
    # The success marker must be anchored so a run that reports failures first
    # ("7 failed, 2396 passed, ...") can never satisfy it.
    assert "'(?m)^[1-9][0-9]* passed(?:, [0-9]+ [a-z ]+)* in '" in text
    # Skips count toward the executed floor, as "Ran N tests" used to.
    assert (
        "$executedTestCount = [int]$testCountMatch.Groups[1].Value "
        "+ $skippedTestCount" in text
    )


def test_playable_retail_slice_step_honours_the_runners_acceptance_contract() -> None:
    """RE-PINNED 2026-08-04 (retail rebase).

    `retail_slice_runner.gd` used to be gated by
    `Invoke-GodotPassedFloor ... "retail_slice_runner.gd" ... 208`, and this
    test pinned that literal. Two things were wrong with it:

      * `RETAIL_SLICE_RESULT ... failed=N` does NOT count the runner's pinned
        known failures, so a row that failed WITHOUT being in
        KNOWN_FAILURE_NAMES left `failed` at 0 and the marker still matched -
        only the child exit code caught it, and only incidentally;
      * the floor 208 sat 155 checks below the runner's own declared
        ACCEPTANCE_MIN_PASSED ratchet (363), so a huge regression could clear
        the gate.

    The step now asserts the runner's own verdict line and reads the ratchet
    FROM the runner, so the gate's floor can no longer drift away from it.
    """

    text = _gate_text()
    assert "RETAIL_SLICE_ACCEPTANCE PASS min_passed=([0-9]+)" in text
    # The old, weaker floor-based wiring must not come back.
    assert not re.search(
        r'Invoke-GodotPassedFloor\s+"[^"]+"\s+"retail_slice_runner\.gd"', text
    )
    # The measured pass count is compared against the runner's OWN ratchet,
    # never against a literal duplicated into this script.
    assert "playable_retail_slice passed fewer checks than its own declared" in text
    assert "playable_retail_slice reported unpinned failures." in text


def test_state_pin_runner_is_wired_into_the_gate() -> None:
    """The behavioural state pin must be a gate step, not CI-only.

    It was CI-only until 2026-08-04, which is why its pinned hash sat red on
    `main` for a week without any local run failing. A pin nothing runs is not
    a pin.
    """

    text = _gate_text()
    assert "retail_state_pin_runner.gd" in text
    assert "RETAIL_STATE_PIN OK hash matches the pinned value" in text


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
    # Build B appends `--no-conversion-cache` to the SAME `$buildArguments` (see
    # test_reproducibility_build_b_runs_cold), so it must still be derived from
    # that variable and never be an inline argument array.
    assert 'Invoke-ImporterJson "build_b" ($buildArguments + ' in text
    direct_build_calls = re.findall(
        r'Invoke-ImporterJson\s+"build_[ab]"\s+@\(', text
    )
    assert direct_build_calls == []


def test_reproducibility_build_b_runs_cold() -> None:
    """A/B must prove the CONVERTER is deterministic, not just the assembler.

    Both proof builds used to share the W3D conversion cache, so build B never
    re-ran the converter — it restored A's artefacts from the cache and
    re-assembled them. Equal bundle hashes therefore could not have detected a
    non-deterministic converter: its output was only ever produced once.
    Build B now passes `--no-conversion-cache`, so a bundle match covers
    conversion as well as assembly.

    This test exists so the flag cannot be quietly dropped to make the gate
    faster; dropping it silently weakens what the gate's own
    "byte-reproducible" assertion means.
    """
    text = _gate_text()
    assert 'Invoke-ImporterJson "build_b" ($buildArguments + "--no-conversion-cache")' in text
    # ...and only on B. If A went cold too, neither build would populate the
    # cache and the comparison would stop saying anything about cache reuse.
    # Checked on the CALL LINES, not the whole file — the rationale comment
    # above the calls names the flag too.
    call_lines = [
        line
        for line in text.splitlines()
        if 'Invoke-ImporterJson "build_' in line
    ]
    assert len(call_lines) == 2
    assert "--no-conversion-cache" not in call_lines[0]
    assert call_lines[0].strip() == (
        '$first = Invoke-ImporterJson "build_a" $buildArguments'
    )
    assert "--no-conversion-cache" in call_lines[1]
    # The failure message has to say what the match now covers.
    assert "build B ran cold" in text


def test_single_build_is_explicit_and_never_claims_attestation() -> None:
    text = _gate_text()
    assert "[switch]$SingleBuild" in text
    assert '$buildArguments += "--single-build"' in text
    assert "$second = $first" in text
    assert "reproducibility NOT ATTESTED" in text
    assert "if (-not $SingleBuild)" in text
    assert "Single build provenance falsely claimed" in text
    assert "Pack provenance must defer the A/B claim" in text


def test_audit_stays_bound_to_frozen_profile_and_resolved_closure() -> None:
    text = _gate_text()
    assert "$builtPackDocument.profile_build_complete -is [bool]" in text
    assert "[bool]$builtPackDocument.profile_build_complete" in text
    assert "@($builtProvenanceDocument.incomplete).Count -eq 0" in text
    assert "$audit.profile -eq $expectedProfileId" in text
    assert "$audit.profile_sha256 -eq $expectedProfileSha256" in text
    assert "[int]$audit.source_archive_count -gt 0" in text
    assert "[int]$audit.provenance_entry_count -gt 0" in text
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
