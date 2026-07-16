from pathlib import Path
import re


ROOT = Path(__file__).parents[2]
GATE = ROOT / "tools" / "gate-m2-men-fords.ps1"
WRAPPER = ROOT / "run_m2_acceptance.bat"
DOC = ROOT / "docs" / "MILESTONE_CURRENT.md"
RELIABILITY = ROOT / "tools" / "run-m2-reliability.ps1"
LIVE_SOAK = ROOT / "game" / "tests" / "m2_live_soak_runner.gd"
ORACLE_COMMON = ROOT / "tools" / "m2-oracle-common.ps1"
ORACLE_WORKSPACE = ROOT / "tools" / "new-m2-oracle-workspace.ps1"
ORACLE_CAPTURE = ROOT / "tools" / "capture-m2-oracle-frame.ps1"
ORACLE_REVIEW = ROOT / "tools" / "review-m2-oracle-capture.ps1"
ORACLE_FREEZE = ROOT / "tools" / "new-m2-oracle-approval.ps1"
ORACLE_FINALIZE = ROOT / "tools" / "finalize-m2-oracle-approval.ps1"
FOCUSED_GATE = ROOT / "tools" / "gate-m2-focused.ps1"


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
    assert "actualDurationSeconds -ge 1800.0" in text
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
    assert "DurationSeconds = 1800" in script
    assert "for ($index = 1; $index -le 3; $index++)" in script
    assert "RETAIL_SLICE_SIGNATURE" in script
    assert "Get-ProofWorkingTreeIdentity" in script
    assert "Get-M2OracleContext" in script
    assert '"Working-tree identity changed during the reliability run."' in script
    assert "freeze performance thresholds before the final soak" in script
    assert '"Live soak did not complete three matches/restarts."' in script
    assert '"--audio-driver", "WASAPI"' in script
    assert 'DisplayServer.get_name() == "headless"' in runner
    assert "DEFAULT_DURATION_SECONDS := 1800.0" in runner
    assert "averageFps" in runner
    assert "onePercentLowFps" in runner
    assert "peakMemoryBytes" in runner
    assert "memoryGrowthBytes" in runner


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
    assert "actualDurationSeconds -ge 1800" in finalize
    assert "pre-frozen threshold" in finalize
