[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE5_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$powersPath = Join-Path $repoRoot "content\openbfme-test\data\powers.json"
$priorGate = Join-Path $PSScriptRoot "gate-stage4.ps1"
$proofRunner = Join-Path $gameRoot "tests\stage5_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage5_visual_runner.gd"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Assert-PowersDocument {
    $root = Read-ProofJson $powersPath
    $rootFields = @("schema", "schemaVersion", "rulesVersion", "rules", "tiers", "powers")
    Assert-ProofProperties $root $rootFields $rootFields "powers.json"
    Assert-ProofTrue ([string]$root.schema -ceq "openbfme.powers") "powers.json schema is unsupported."
    Assert-ProofTrue ((Test-ProofInteger $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "powers.json schemaVersion must be 0."
    Assert-ProofTrue ((Test-ProofInteger $root.rulesVersion) -and [int]$root.rulesVersion -eq 1) "powers.json rulesVersion must be 1."

    $ruleFields = @("teamCount", "startingPowerPoints", "experiencePerPoint", "unitDefeatPowerPoints", "buildingDefeatPowerPoints", "maximumQueuedCommands", "weatherPolicy")
    Assert-ProofProperties $root.rules $ruleFields $ruleFields "powers rules"
    foreach ($field in @("teamCount", "startingPowerPoints", "experiencePerPoint", "unitDefeatPowerPoints", "buildingDefeatPowerPoints", "maximumQueuedCommands")) {
        Assert-ProofTrue ((Test-ProofInteger $root.rules.$field) -and [int]$root.rules.$field -gt 0) "powers rules $field must be a positive integer."
    }
    Assert-ProofTrue ([int]$root.rules.teamCount -eq 2) "The proof must define exactly two teams."
    Assert-ProofTrue ([string]$root.rules.weatherPolicy -ceq "replace") "The weather replacement policy must be explicit."

    $tierFields = @("tier", "minimumSpent")
    $tiers = @($root.tiers)
    Assert-ProofTrue ($tiers.Count -eq 4) "The Stage 5 proof must define four tiers."
    $priorSpent = -1
    for ($index = 0; $index -lt $tiers.Count; $index++) {
        $tier = $tiers[$index]
        Assert-ProofProperties $tier $tierFields $tierFields "power tier"
        Assert-ProofTrue ((Test-ProofInteger $tier.tier) -and [int]$tier.tier -eq $index + 1) "Power tiers must be contiguous and one-based."
        Assert-ProofTrue ((Test-ProofInteger $tier.minimumSpent) -and [int]$tier.minimumSpent -ge 0 -and [int]$tier.minimumSpent -ge $priorSpent) "Tier minimumSpent values must be nonnegative and monotonic."
        $priorSpent = [int]$tier.minimumSpent
    }
    Assert-ProofTrue ([int]$tiers[0].minimumSpent -eq 0) "Tier one must be reachable without prior spending."

    $powerFields = @("code", "id", "displayName", "tier", "pointCost", "cooldownTicks", "targetMode", "prerequisites", "effects")
    $effectFields = @("type", "amount", "buildingAmount", "radiusCells", "weatherCode", "durationTicks", "unitDamagePerTick", "buildingDamagePerTick")
    $effectModes = @{
        heal_entity = "friendly_entity"
        damage_entity = "hostile_entity"
        area_damage = "position"
        area_heal = "position"
        weather = "global"
        damage_building = "hostile_building"
        global_heal = "global"
    }
    $powers = @($root.powers)
    Assert-ProofTrue ($powers.Count -ge 7) "Stage 5 must define at least seven powers."
    $codes = @{}
    $modes = @{}
    $tierOneCost = 0
    foreach ($power in $powers) {
        Assert-ProofProperties $power $powerFields $powerFields "power"
        Assert-ProofTrue ((Test-ProofInteger $power.code) -and [int]$power.code -gt 0 -and -not $codes.ContainsKey([int]$power.code)) "Power codes must be unique positive integers."
        $codes[[int]$power.code] = $power
        Assert-ProofTrue ([string]$power.id -cmatch '^[a-z0-9][a-z0-9.-]{2,63}$') "Power ids must be stable lowercase identifiers."
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$power.displayName)) "Power displayName must not be empty."
        Assert-ProofTrue ((Test-ProofInteger $power.tier) -and [int]$power.tier -ge 1 -and [int]$power.tier -le 4) "Power tier is invalid."
        Assert-ProofTrue ((Test-ProofInteger $power.pointCost) -and [int]$power.pointCost -gt 0) "Power pointCost must be positive."
        Assert-ProofTrue ((Test-ProofInteger $power.cooldownTicks) -and [int]$power.cooldownTicks -ge 0) "Power cooldownTicks may not be negative."
        $mode = [string]$power.targetMode
        Assert-ProofTrue (@("friendly_entity", "hostile_entity", "hostile_building", "position", "global") -contains $mode) "Power targetMode is unsupported."
        $modes[$mode] = $true
        if ([int]$power.tier -eq 1) { $tierOneCost += [int]$power.pointCost }
        Assert-ProofTrue (@($power.effects).Count -gt 0) "Every power must define an effect."
        foreach ($effect in @($power.effects)) {
            Assert-ProofProperties $effect @("type") $effectFields "power effect"
            $type = [string]$effect.type
            Assert-ProofTrue ($effectModes.ContainsKey($type) -and [string]$effectModes[$type] -ceq $mode) "Power effect '$type' does not match target mode '$mode'."
            foreach ($property in $effect.PSObject.Properties) {
                if ($property.Name -ne "type" -and $property.Name -ne "weatherCode") {
                    Assert-ProofTrue ((Test-ProofInteger $property.Value) -and [int]$property.Value -ge 0) "Effect '$type' contains an invalid numeric value."
                }
            }
        }
    }
    foreach ($power in $powers) {
        foreach ($prerequisite in @($power.prerequisites)) {
            Assert-ProofTrue ((Test-ProofInteger $prerequisite) -and $codes.ContainsKey([int]$prerequisite) -and [int]$prerequisite -lt [int]$power.code) "Power prerequisites must reference an earlier power and remain acyclic."
        }
    }
    foreach ($requiredMode in @("friendly_entity", "hostile_entity", "hostile_building", "position", "global")) {
        Assert-ProofTrue $modes.ContainsKey($requiredMode) "Stage 5 is missing '$requiredMode' targeting coverage."
    }
    Assert-ProofTrue ([int]$root.rules.startingPowerPoints -ge $tierOneCost) "Starting power points must allow the complete first tier and may not soft-lock the tree."
}

try {
    [void](Invoke-ProofPriorGate $gate "stage4_regression" $priorGate $GodotPath '(?m)^STAGE4_GATE PASS\s*$')
    Assert-PowersDocument
    Write-Host "$gate bundle PASS"
    Assert-ProofTrue (Test-Path -LiteralPath $proofRunner -PathType Leaf) "Missing Stage 5 proof runner."
    Assert-ProofTrue (Test-Path -LiteralPath $visualRunner -PathType Leaf) "Missing Stage 5 visual runner."
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $proofOutput = Invoke-ProofChecked $gate "godot_stage5_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage5_proof_runner.gd") '(?m)^STAGE5_GODOT_PROOF PASS authority=gdscript-proof assertions=\d+ hash=[0-9A-F]{8}\s*$' $forbiddenDiagnostics
    $visualOutput = Invoke-ProofChecked $gate "godot_stage5_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage5_visual_runner.gd") '(?m)^STAGE5_VISUAL_PROOF PASS assertions=\d+\s*$' $forbiddenDiagnostics
    $proofMatch = [regex]::Match($proofOutput, '(?m)^STAGE5_GODOT_PROOF PASS authority=gdscript-proof assertions=(\d+) hash=([0-9A-F]{8})\s*$')
    $visualMatch = [regex]::Match($visualOutput, '(?m)^STAGE5_VISUAL_PROOF PASS assertions=(\d+)\s*$')
    Assert-ProofTrue ($proofMatch.Success -and [int]$proofMatch.Groups[1].Value -ge 50) "Stage 5 proof assertion coverage regressed below 50."
    Assert-ProofTrue ($visualMatch.Success -and [int]$visualMatch.Groups[1].Value -ge 24) "Stage 5 visual assertion coverage regressed below 24."
    Write-Host "$gate deterministic_replay PASS hash=$($proofMatch.Groups[2].Value)"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
