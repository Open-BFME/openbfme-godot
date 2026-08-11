[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$gate = "M2_FOCUSED_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use|SCRIPT ERROR)\b'
$focusedRunners = [ordered]@{
    'animated_prop_runtime' = @('retail_animated_prop_runtime_runner.gd', '(?m)^RETAIL_ANIMATED_PROP_RUNTIME_RESULT passed=18 failed=0\s*$')
    'archer_projectile_presentation_runtime' = @('retail_archer_projectile_presentation_runner.gd', '(?m)^RETAIL_ARCHER_PROJECTILE_PRESENTATION_RESULT passed=10 failed=0\s*$')
    'bound_props_runtime' = @('retail_bound_props_runner.gd', '(?m)^RETAIL_BOUND_PROPS_RESULT passed=30 failed=0\s*$')
    'builder_construction_runtime' = @('retail_builder_construction_runner.gd', '(?m)^RETAIL_BUILDER_CONSTRUCTION_RESULT passed=12 failed=0\s*$')
    'environment_runtime' = @('retail_environment_runner.gd', '(?m)^RETAIL_ENVIRONMENT_RESULT passed=33 failed=0\s*$')
    'four_unit_audio' = @('retail_four_unit_audio_runner.gd', '(?m)^RETAIL_FOUR_UNIT_AUDIO_RESULT passed=105 failed=0 missing=0\s*$')
    'four_unit_hud' = @('retail_four_unit_hud_runner.gd', '(?m)^RETAIL_FOUR_UNIT_HUD_RESULT passed=134 failed=0\s*$')
    'full_terrain_runtime' = @('retail_full_terrain_runner.gd', '(?m)^RETAIL_FULL_TERRAIN_RESULT passed=29 failed=0\s*$')
    'hud_apt_runtime' = @('retail_hud_apt_runtime_runner.gd', '(?m)^HUD_APT_RUNTIME_SUMMARY passed=67 failed=0\s*$')
    'hud_wnd_runtime' = @('retail_hud_wnd_runtime_runner.gd', '(?m)^RETAIL_HUD_WND_RUNTIME_RESULT passed=62 failed=0\s*$')
    'linear_fog_runtime' = @('retail_linear_fog_runner.gd', '(?m)^RETAIL_LINEAR_FOG_RESULT passed=23 failed=0\s*$')
    'member_combat_runtime' = @('retail_member_combat_runner.gd', '(?m)^RETAIL_MEMBER_COMBAT_RESULT passed=49 failed=0\s*$')
    # Pinned 13 while the runner emitted 15 on clean main - pre-existing gate rot,
    # red before this lane touched anything. Re-pinned to 40 because this lane
    # rewrote the runner and owning a runner means owning its pin.
    'member_health_overlay_runtime' = @('retail_member_health_overlay_runner.gd', '(?m)^RETAIL_MEMBER_HEALTH_OVERLAY_RESULT passed=40 failed=0\s*$')
    'selection_decal_runtime' = @('retail_selection_decal_runner.gd', '(?m)^RETAIL_SELECTION_DECAL_RESULT passed=15 failed=0\s*$')
    'neutral_lifecycle_runtime' = @('retail_neutral_lifecycle_runner.gd', '(?m)^RETAIL_NEUTRAL_LIFECYCLE_RESULT passed=51 failed=0\s*$')
    'particle_runtime' = @('retail_particle_runtime_runner.gd', '(?m)^RETAIL_PARTICLE_RUNTIME_RESULT passed=17 failed=0\s*$')
    'production_queue_runtime' = @('retail_production_queue_runner.gd', '(?m)^RETAIL_PRODUCTION_QUEUE_RESULT passed=16 failed=0\s*$')
    'road_placement_runtime' = @('retail_road_placement_runner.gd', '(?m)^RETAIL_ROAD_RESULT passed=15 failed=0\s*$')
    'road_visual_runtime' = @('retail_road_visual_runner.gd', '(?m)^RETAIL_ROAD_VISUAL_RESULT passed=27 failed=0\s*$')
    'structure_damage_runtime' = @('retail_structure_damage_effects_runner.gd', '(?m)^RETAIL_STRUCTURE_DAMAGE_EFFECTS_RESULT passed=105 failed=0\s*$')
    'structure_lifecycle_runtime' = @('retail_structure_lifecycle_runner.gd', '(?m)^RETAIL_STRUCTURE_LIFECYCLE_RESULT passed=136 failed=0\s*$')
}

try {
    $context = Get-M2OracleContext $repoRoot
    $env:OPENBFME_CONTENT = $context.contentRoot
    $env:OPENBFME_TERRAIN_PACK = $context.packRoot
    $env:OPENBFME_ROAD_PACK = $context.packRoot
    $env:OPENBFME_TEST_PACK = $context.packRoot
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    foreach ($step in $focusedRunners.Keys) {
        $runner = [string]$focusedRunners[$step][0]
        $marker = [string]$focusedRunners[$step][1]
        [void](Invoke-ProofChecked $gate $step $godot @('--headless', '--audio-driver', 'WASAPI', '--path', (Join-Path $repoRoot 'game'), '--script', "res://tests/$runner") $marker $forbiddenDiagnostics)
    }
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Working-tree identity changed during the focused gate."
    Write-Host "$gate PASS runners=$($focusedRunners.Count) profile_sha256=$($context.profileSha256) bundle_sha256=$($context.bundleSha256) dirty_state_digest=$($context.dirtyStateDigest)"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
