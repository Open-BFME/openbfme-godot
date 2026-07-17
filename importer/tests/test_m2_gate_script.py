import base64
import json
from pathlib import Path
import re
import shutil
import subprocess

import pytest


ROOT = Path(__file__).parents[2]
GATE = ROOT / "tools" / "gate-m2-men-fords.ps1"
WRAPPER = ROOT / "run_m2_acceptance.bat"
DOC = ROOT / "docs" / "MILESTONE_CURRENT.md"
RELIABILITY = ROOT / "tools" / "run-m2-reliability.ps1"
LIVE_SOAK = ROOT / "game" / "tests" / "m2_live_soak_runner.gd"
MATCH_LIFECYCLE = ROOT / "game" / "tests" / "m2_match_lifecycle_runner.gd"
ORACLE_COMMON = ROOT / "tools" / "m2-oracle-common.ps1"
ORACLE_WORKSPACE = ROOT / "tools" / "new-m2-oracle-workspace.ps1"
ORACLE_CAPTURE = ROOT / "tools" / "capture-m2-oracle-frame.ps1"
ORACLE_REVIEW = ROOT / "tools" / "review-m2-oracle-capture.ps1"
ORACLE_FREEZE = ROOT / "tools" / "new-m2-oracle-approval.ps1"
ORACLE_FINALIZE = ROOT / "tools" / "finalize-m2-oracle-approval.ps1"
FOCUSED_GATE = ROOT / "tools" / "gate-m2-focused.ps1"
RELIABILITY_COMMON = ROOT / "tools" / "m2-reliability-evidence-common.ps1"


def test_m2_gate_is_final_identity_bound_retail_gate() -> None:
    text = GATE.read_text(encoding="utf-8")
    assert '"Final M2 acceptance requires -IntegrationOwnerPublish."' in text
    assert '"gate-retail.ps1"' in text
    assert '"-IntegrationOwnerPublish"' in text
    profile_match = re.search(
        r'\$expectedProfileSha256 = "([0-9a-f]{64})"', text
    )
    assert profile_match is not None
    assert "$profileSha256 -eq $expectedProfileSha256" in text
    assert "openbfme.m2-men-fords-oracle-approval" in text
    assert "profileSha256" in text
    assert "bundleSha256" in text
    assert "unresolvedSeverity0" in text
    assert "unresolvedSeverity1" in text
    assert "gitRevision" in text
    assert "dirtyStateDigest" in text
    assert "openbfme.m2-men-fords-reliability" in text
    assert "Assert-M2ReliabilitySoakEvidence" in text
    assert "-MinimumDurationSeconds 1800" in text
    assert "minimumAverageFps" in text
    assert "minimumOnePercentLowFps" in text
    assert "maximumPeakMemoryBytes" in text
    assert "maximumMemoryGrowthBytes" in text
    assert "thresholdsFrozenAtUtc" in text


def test_m2_gate_requires_the_documented_capture_matrix() -> None:
    gate = GATE.read_text(encoding="utf-8")
    common = ORACLE_COMMON.read_text(encoding="utf-8")
    document = DOC.read_text(encoding="utf-8")
    required = {
        line.strip()
        for line in document.splitlines()
        if line.startswith(("map-", "ford-", "player-", "enemy-", "unit-", "structure-", "hud-"))
    }
    assert len(required) == 47
    for capture_id in required:
        assert f'"{capture_id}"' in common
    assert "$script:M2OracleCaptureIds" in gate
    assert "Compare-Object $requiredCaptureIds $actualCaptureIds" in gate


def test_m2_wrapper_only_dispatches_the_final_gate() -> None:
    text = WRAPPER.read_text(encoding="utf-8").replace("\\", "/")
    assert "tools/gate-m2-men-fords.ps1" in text
    assert "%*" in text


def test_m2_gate_runs_the_focused_graphics_ui_audio_and_play_contracts() -> None:
    text = GATE.read_text(encoding="utf-8")
    focused = FOCUSED_GATE.read_text(encoding="utf-8")
    assert '"gate-m2-focused.ps1"' in text
    assert "M2_FOCUSED_GATE PASS runners=[1-9][0-9]*" in text
    assert "M2_FOCUSED_GATE PASS runners=21" not in text
    for runner in (
        "retail_animated_prop_runtime_runner.gd",
        "retail_bound_props_runner.gd",
        "retail_builder_construction_runner.gd",
        "retail_environment_runner.gd",
        "retail_four_unit_audio_runner.gd",
        "retail_four_unit_hud_runner.gd",
        "retail_full_terrain_runner.gd",
        "retail_hud_apt_runtime_runner.gd",
        "retail_hud_wnd_runtime_runner.gd",
        "retail_linear_fog_runner.gd",
        "retail_member_combat_runner.gd",
        "retail_member_health_overlay_runner.gd",
        "retail_selection_decal_runner.gd",
        "retail_archer_projectile_presentation_runner.gd",
        "retail_neutral_lifecycle_runner.gd",
        "retail_particle_runtime_runner.gd",
        "retail_production_queue_runner.gd",
        "retail_road_placement_runner.gd",
        "retail_road_visual_runner.gd",
        "retail_structure_damage_effects_runner.gd",
        "retail_structure_lifecycle_runner.gd",
    ):
        assert runner in focused
    assert "--audio-driver" in focused
    assert "WASAPI" in focused


def test_m2_reliability_is_real_time_rendered_and_identity_bound() -> None:
    script = RELIABILITY.read_text(encoding="utf-8")
    runner = LIVE_SOAK.read_text(encoding="utf-8")
    lifecycle = MATCH_LIFECYCLE.read_text(encoding="utf-8")
    assert "DurationSeconds = 1800" in script
    assert "$minimumRetailSliceChecks = 208" in script
    assert "$passedMatch.Groups[1].Value -ge $minimumRetailSliceChecks" in script
    assert "RETAIL_SLICE_RESULT passed=208 failed=0" not in script
    assert "for ($index = 1; $index -le 3; $index++)" in script
    assert "RETAIL_SLICE_SIGNATURE" in script
    assert "Get-ProofWorkingTreeIdentity" in script
    assert "Get-M2OracleContext" in script
    assert '"Working-tree identity changed during the reliability run."' in script
    assert "freeze performance thresholds before the final soak" in script
    assert "Assert-M2MatchLifecycleEvidence" in script
    assert "m2_match_lifecycle_runner.gd" in script
    assert '"--audio-driver", "WASAPI"' in script
    assert 'DisplayServer.get_name() == "headless"' in runner
    assert "DEFAULT_DURATION_SECONDS := 1800.0" in runner
    assert "averageFps" in runner
    assert "onePercentLowFps" in runner
    assert "peakMemoryBytes" in runner
    assert "memoryGrowthBytes" in runner
    assert "MAXIMUM_DURATION_SECONDS := 3600.0" in runner
    assert "PackedFloat64Array" in runner
    assert "PackedInt64Array" in runner
    assert "frame_sample_storage.resize(frame_sample_capacity)" in runner
    assert "memory_sample_storage.resize(memory_sample_capacity)" in runner
    assert runner.index("frame_sample_storage.resize") < runner.index("soak_started")
    assert runner.index("memory_sample_storage.resize") < runner.index("soak_started")
    assert '"schemaVersion": 1' in runner
    assert '"packed-float64-preallocated"' in runner
    assert '"packed-int64-preallocated"' in runner
    assert "frame_msec.append" not in runner
    assert "EXPECTED_MATCHES := 3" in lifecycle
    assert "slice.step_for_test" in lifecycle
    assert "simulation.winner =" not in lifecycle
    assert 'structure.destroyed' in lifecycle
    assert 'match.defeat' in lifecycle
    for field in (
        "constructionStartedSequence",
        "constructionCompletedSequence",
        "productionCompletedSequence",
        "fortressHitSequence",
        "fortressDestroyedSequence",
        "defeatSequence",
    ):
        assert field in lifecycle
        assert field in RELIABILITY_COMMON.read_text(encoding="utf-8")
    assert "scene_reference.get_ref() == null" in lifecycle
    assert "mesh_cache_size()" in lifecycle


def test_m2_lifecycle_flags_require_real_json_booleans() -> None:
    common = RELIABILITY_COMMON.read_text(encoding="utf-8")
    for field in ("outcomePresented", "sceneFreed", "meshCacheCleared"):
        assert f"$row.{field} -is [bool]" in common
        assert f"$row.{field} -eq $true" in common


@pytest.mark.parametrize("field", ("outcomePresented", "sceneFreed", "meshCacheCleared"))
def test_m2_lifecycle_validator_rejects_string_false(field: str) -> None:
    powershell = shutil.which("powershell.exe") or shutil.which("powershell") or shutil.which("pwsh")
    if powershell is None:
        pytest.skip("PowerShell is required to execute the lifecycle validator")
    profile = "a" * 64
    bundle = "b" * 64
    revision = "c" * 40
    dirty = "d" * 64
    row = {
        "index": 1,
        "bundleSha256": bundle,
        "winner": 1,
        "terminalTick": 21300,
        "stateSignature": "932C2486",
        "playerFortressHealth": 0,
        "constructionStartedSequence": 2,
        "constructionCompletedSequence": 15,
        "productionCompletedSequence": 32,
        "fortressHitSequence": 30601,
        "fortressDestroyedSequence": 100327,
        "defeatSequence": 100328,
        "defeatEventCount": 1,
        "outcomePresented": True,
        "musicState": "defeat",
        "sceneFreed": True,
        "meshCacheCleared": True,
        "readyDurationMsec": 4000,
    }
    rows = []
    for index in range(1, 4):
        candidate = dict(row)
        candidate["index"] = index
        rows.append(candidate)
    rows[0][field] = "false"
    lifecycle = {
        "schema": "openbfme.m2-match-lifecycle",
        "schemaVersion": 0,
        "profileSha256": profile,
        "bundleSha256": bundle,
        "gitRevision": revision,
        "dirtyStateDigest": dirty,
        "expectedMatches": 3,
        "completedMatches": 3,
        "readyStarts": 3,
        "teardownsConfirmed": 3,
        "matches": rows,
        "deterministicSignature": "932C2486",
        "diagnosticCount": 0,
    }
    encoded_json = base64.b64encode(json.dumps(lifecycle).encode("utf-8")).decode("ascii")
    helper = str(RELIABILITY_COMMON).replace("'", "''")
    command = (
        "$ErrorActionPreference='Stop'; "
        f". '{helper}'; "
        f"$json=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{encoded_json}')); "
        "$value=$json|ConvertFrom-Json; "
        "$accepted=$true; "
        f"try {{ Assert-M2MatchLifecycleEvidence -Lifecycle $value -ExpectedProfileSha256 '{profile}' -ExpectedBundleSha256 '{bundle}' -ExpectedGitRevision '{revision}' -ExpectedDirtyStateDigest '{dirty}' }} catch {{ $accepted=$false }}; "
        "if($accepted){exit 42}; Write-Output 'TAMPER_REJECTED'"
    )
    encoded_command = base64.b64encode(command.encode("utf-16-le")).decode("ascii")
    completed = subprocess.run(
        [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded_command],
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "TAMPER_REJECTED" in completed.stdout


def test_m2_oracle_tools_fail_closed_and_revoke_stale_review() -> None:
    common = ORACLE_COMMON.read_text(encoding="utf-8")
    workspace = ORACLE_WORKSPACE.read_text(encoding="utf-8")
    capture = ORACLE_CAPTURE.read_text(encoding="utf-8")
    review = ORACLE_REVIEW.read_text(encoding="utf-8")
    freeze = ORACLE_FREEZE.read_text(encoding="utf-8")
    finalize = ORACLE_FINALIZE.read_text(encoding="utf-8")
    assert "Get-ProofWorkingTreeIdentity" in common
    assert "openbfme.m2-men-fords-oracle-captures" in common
    assert '"Selected immutable bundle provenance targets another profile or remains incomplete."' in common
    assert "[string]$provenance.profile_sha256 -eq $profileSha256" in common
    assert "@($provenance.incomplete).Count -eq 0" in common
    assert 'approved = $false' in workspace
    assert 'ValidateSet("Retail", "Godot")' in capture
    assert '"-f", "gdigrab"' in capture
    assert "Captured viewport is" in capture
    assert '$capture.approved = $false' in capture
    assert '$capture.approved = ($UnresolvedSeverity0 -eq 0' in review
    assert "Performance thresholds are frozen and cannot be overwritten" in freeze
    assert "Assert-M2ReliabilitySoakEvidence" in finalize
    assert "-MinimumDurationSeconds 1800" in finalize
    assert "pre-frozen threshold" in finalize
