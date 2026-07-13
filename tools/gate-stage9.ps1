[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE9_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$stage8Gate = Join-Path $PSScriptRoot "gate-stage8.ps1"
$rulesPath = Join-Path $repoRoot "content\openbfme-test\data\stage9_ring_rules.json"
$proofRunner = Join-Path $gameRoot "tests\stage9_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage9_visual_runner.gd"
$scenePath = Join-Path $gameRoot "scenes\stage9_lab.tscn"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Assert-Stage9Rules {
    Assert-ProofTrue (Test-Path -LiteralPath $rulesPath -PathType Leaf) "Missing Stage 9 objective rules."
    $root = Read-ProofJson $rulesPath
    $topFields = @("schema", "schemaVersion", "rulesVersion", "objective", "audioEvents")
    Assert-ProofProperties $root $topFields $topFields "stage9 rule root"
    Assert-ProofTrue ([string]$root.schema -eq "openbfme.relic-ring-objective") "Unexpected Stage 9 schema."
    Assert-ProofTrue ((Test-ProofInteger $root.schemaVersion) -and [int]$root.schemaVersion -eq 0 -and (Test-ProofInteger $root.rulesVersion) -and [int]$root.rulesVersion -eq 1) "Stage 9 schema/rules versions are invalid."

    $objectiveFields = @("displayName", "enabledByDefault", "spawnCell", "claimRadiusCells", "reclaimDelayTicks", "victoryHoldTicks", "holderSpeedPermille", "holderDamagePermille", "dropOnHolderDefeat", "revealHolder")
    Assert-ProofProperties $root.objective $objectiveFields $objectiveFields "stage9 objective"
    $display = [string]$root.objective.displayName
    Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace($display) -and $display -eq "Auric Loop" -and $display -notmatch '(?i)one ring|gollum|sauron|tolkien|middle.?earth') "Stage 9 objective display name must remain original and legal-safe."
    Assert-ProofTrue ($root.objective.enabledByDefault -is [bool]) "Stage 9 classic-mode default must be boolean."
    Assert-ProofTrue ($root.objective.spawnCell.Count -eq 2 -and (Test-ProofInteger $root.objective.spawnCell[0]) -and (Test-ProofInteger $root.objective.spawnCell[1])) "Stage 9 spawnCell must be an integer pair."
    foreach ($field in @("claimRadiusCells", "reclaimDelayTicks", "victoryHoldTicks", "holderSpeedPermille", "holderDamagePermille")) {
        Assert-ProofTrue (Test-ProofInteger $root.objective.$field) "Stage 9 objective $field must be an integer."
    }
    Assert-ProofTrue ([int]$root.objective.claimRadiusCells -ge 0 -and [int]$root.objective.reclaimDelayTicks -ge 0 -and [int]$root.objective.victoryHoldTicks -ge 1) "Stage 9 claim/reclaim/victory timing is invalid."
    Assert-ProofTrue ([int]$root.objective.holderSpeedPermille -ge 1 -and [int]$root.objective.holderSpeedPermille -le 1000 -and [int]$root.objective.holderDamagePermille -ge 1000) "Stage 9 holder consequences are invalid."
    Assert-ProofTrue ($root.objective.dropOnHolderDefeat -is [bool] -and $root.objective.revealHolder -is [bool]) "Stage 9 holder flags must be booleans."

    $audioFields = @("spawn", "claim", "drop", "reclaim", "victory", "loss", "hit", "music_explore", "music_contest", "music_victory", "music_defeat")
    Assert-ProofProperties $root.audioEvents $audioFields $audioFields "stage9 audio routes"
    $eventIds = @{}
    foreach ($field in $audioFields) {
        $eventId = [string]$root.audioEvents.$field
        Assert-ProofTrue ($eventId -match '^[a-z]+(?:[._-][a-z]+)+$' -and -not $eventIds.ContainsKey($eventId)) "Stage 9 audio event '$field' is invalid or duplicated."
        $eventIds[$eventId] = $true
    }
    Assert-ProofTrue ([string]$root.audioEvents.hit -eq "combat.clean-impact") "Stage 9 hit event must remain explicitly blood-free."
    foreach ($musicField in @("music_explore", "music_contest", "music_victory", "music_defeat")) {
        Assert-ProofTrue ([string]$root.audioEvents.$musicField -match '^music\.') "Stage 9 $musicField must route through the music category."
    }
}

try {
    Assert-ProofTrue (Test-Path -LiteralPath $stage8Gate -PathType Leaf) "Missing Stage 8 gate dependency."
    [void](Invoke-ProofPriorGate $gate "stage8_regression" $stage8Gate $GodotPath '(?m)^STAGE8_GATE PASS\s*$')
    Assert-Stage9Rules
    Write-Host "$gate bundle PASS"

    foreach ($path in @($proofRunner, $visualRunner, $scenePath)) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing Stage 9 dependency: $path"
    }
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $proof = Invoke-ProofChecked $gate "godot_stage9_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage9_proof_runner.gd") '(?m)^STAGE9_GODOT_PROOF PASS authority=gdscript-proof assertions=\d+\s*$' $forbiddenDiagnostics
    $visual = Invoke-ProofChecked $gate "godot_stage9_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage9_visual_runner.gd") '(?m)^STAGE9_VISUAL_PROOF PASS assertions=\d+\s*$' $forbiddenDiagnostics
    $metrics = [regex]::Match($proof, '(?m)^STAGE9_METRICS repeat_hash=([0-9A-F]{8}) audio_events=(\d+) assertions=(\d+)\s*$')
    Assert-ProofTrue ($metrics.Success -and [int]$metrics.Groups[2].Value -ge 8 -and [int]$metrics.Groups[3].Value -ge 45) "Stage 9 proof metrics or assertion coverage regressed."
    $visualMetrics = [regex]::Match($visual, '(?m)^STAGE9_VISUAL_PROOF PASS assertions=(\d+)\s*$')
    Assert-ProofTrue ($visualMetrics.Success -and [int]$visualMetrics.Groups[1].Value -ge 28) "Stage 9 visual coverage regressed below 28 assertions."
    Write-Host "$gate deterministic_objective_audio PASS hash=$($metrics.Groups[1].Value) events=$($metrics.Groups[2].Value)"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
