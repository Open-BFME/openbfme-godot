[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE7_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$strategiesPath = Join-Path $repoRoot "content\openbfme-test\data\ai_strategies.json"
$priorGate = Join-Path $PSScriptRoot "gate-stage6.ps1"
$proofRunner = Join-Path $gameRoot "tests\stage7_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage7_visual_runner.gd"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Assert-AiStrategyDocument {
    $root = Read-ProofJson $strategiesPath
    $rootFields = @("schema", "schemaVersion", "rulesVersion", "scenario", "difficulties", "plans")
    Assert-ProofProperties $root $rootFields $rootFields "ai_strategies.json"
    Assert-ProofTrue ([string]$root.schema -ceq "openbfme.ai-strategies") "ai_strategies.json schema is unsupported."
    Assert-ProofTrue ((Test-ProofInteger $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "ai_strategies.json schemaVersion must be 0."
    Assert-ProofTrue ((Test-ProofInteger $root.rulesVersion) -and [int]$root.rulesVersion -eq 1) "ai_strategies.json rulesVersion must be 1."

    $scenarioFields = @("startingResources", "finiteResourceAmount", "harvestAmount", "harvestIntervalTicks", "startingArmy", "enemyFortressHealth", "attackIntervalTicks", "baseAttackDamage", "maximumProofTicks")
    Assert-ProofProperties $root.scenario $scenarioFields $scenarioFields "AI scenario"
    foreach ($field in $scenarioFields) {
        Assert-ProofTrue ((Test-ProofInteger $root.scenario.$field) -and [int]$root.scenario.$field -gt 0) "AI scenario $field must be a positive integer."
    }
    Assert-ProofTrue ([int]$root.scenario.harvestAmount -le [int]$root.scenario.finiteResourceAmount) "Harvest amount may not exceed the finite deposit."
    Assert-ProofTrue ([int]$root.scenario.maximumProofTicks -eq 200) "Stage 7 proof budget must remain exactly 200 ticks."

    $difficultyFields = @("id", "displayName", "thinkIntervalTicks", "incomePermille", "attackPermille")
    $difficultyIds = @{}
    $thinkValues = @()
    $incomeValues = @()
    $attackValues = @()
    $difficulties = @($root.difficulties)
    Assert-ProofTrue ($difficulties.Count -eq 3) "Stage 7 must define exactly three difficulties."
    foreach ($difficulty in $difficulties) {
        Assert-ProofProperties $difficulty $difficultyFields $difficultyFields "AI difficulty"
        $difficultyId = [string]$difficulty.id
        Assert-ProofTrue (@("easy", "normal", "hard") -contains $difficultyId -and -not $difficultyIds.ContainsKey($difficultyId)) "Difficulty ids must be unique easy/normal/hard values."
        $difficultyIds[$difficultyId] = $true
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$difficulty.displayName)) "Difficulty displayName must not be empty."
        foreach ($field in @("thinkIntervalTicks", "incomePermille", "attackPermille")) {
            Assert-ProofTrue ((Test-ProofInteger $difficulty.$field) -and [int]$difficulty.$field -gt 0) "Difficulty $field must be positive."
        }
        $thinkValues += [int]$difficulty.thinkIntervalTicks
        $incomeValues += [int]$difficulty.incomePermille
        $attackValues += [int]$difficulty.attackPermille
    }
    Assert-ProofTrue (@($thinkValues | Select-Object -Unique).Count -eq 3) "Difficulty think intervals must be distinguishable."
    Assert-ProofTrue (@($incomeValues | Select-Object -Unique).Count -eq 3) "Difficulty income rates must be distinguishable."
    Assert-ProofTrue (@($attackValues | Select-Object -Unique).Count -eq 3) "Difficulty attack rates must be distinguishable."

    $planFields = @("id", "displayName", "steps")
    $jobFields = @("action", "objectId", "cost", "durationTicks")
    $attackFields = @("action", "minimumArmy")
    $plans = @($root.plans)
    Assert-ProofTrue ($plans.Count -eq 1) "Stage 7 must define exactly one bounded proof plan."
    $plan = $plans[0]
    Assert-ProofProperties $plan $planFields $planFields "AI plan"
    Assert-ProofTrue ([string]$plan.id -ceq "measured_opening" -and -not [string]::IsNullOrWhiteSpace([string]$plan.displayName)) "AI plan identity is invalid."
    $steps = @($plan.steps)
    Assert-ProofTrue ($steps.Count -eq 4) "AI plan must contain build, train, train, and attack."
    $expectedActions = @("build", "train", "train", "attack")
    for ($index = 0; $index -lt $steps.Count; $index++) {
        $step = $steps[$index]
        Assert-ProofTrue ([string]$step.action -ceq $expectedActions[$index]) "AI plan action order changed."
        if ($index -lt 3) {
            Assert-ProofProperties $step $jobFields $jobFields "AI job step"
            Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$step.objectId)) "AI job objectId must not be empty."
            Assert-ProofTrue ((Test-ProofInteger $step.cost) -and [int]$step.cost -gt 0) "AI job cost must be positive."
            Assert-ProofTrue ((Test-ProofInteger $step.durationTicks) -and [int]$step.durationTicks -gt 0) "AI job duration must be positive."
        }
        else {
            Assert-ProofProperties $step $attackFields $attackFields "AI attack step"
            Assert-ProofTrue ((Test-ProofInteger $step.minimumArmy) -and [int]$step.minimumArmy -gt 0) "AI minimumArmy must be positive."
        }
    }
}

try {
    [void](Invoke-ProofPriorGate $gate "stage6_regression" $priorGate $GodotPath '(?m)^STAGE6_GATE PASS\s*$')
    Assert-AiStrategyDocument
    Write-Host "$gate bundle PASS"
    Assert-ProofTrue (Test-Path -LiteralPath $proofRunner -PathType Leaf) "Missing Stage 7 proof runner."
    Assert-ProofTrue (Test-Path -LiteralPath $visualRunner -PathType Leaf) "Missing Stage 7 visual runner."
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $proofOutput = Invoke-ProofChecked $gate "godot_stage7_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage7_proof_runner.gd") '(?m)^STAGE7_GODOT_PROOF PASS authority=gdscript-proof assertions=85\s*$' $forbiddenDiagnostics
    $visualOutput = Invoke-ProofChecked $gate "godot_stage7_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage7_visual_runner.gd") '(?m)^STAGE7_VISUAL_PROOF PASS assertions=32\s*$' $forbiddenDiagnostics
    $metrics = [regex]::Match($proofOutput, '(?m)^STAGE7_METRICS repeat_hash=([0-9A-F]{8}) assertions=(\d+) easy_tick=(\d+) normal_tick=(\d+) hard_tick=(\d+) victories=(\d+) starvation_victories=(\d+) no_softlocks=(\d+)\s*$')
    Assert-ProofTrue ($metrics.Success -and $metrics.Groups[1].Value -ceq "8D53B31F") "Stage 7 deterministic replay hash changed."
    Assert-ProofTrue ([int]$metrics.Groups[2].Value -eq 85) "Stage 7 proof assertion count changed."
    Assert-ProofTrue ([int]$metrics.Groups[3].Value -eq 24 -and [int]$metrics.Groups[4].Value -eq 17 -and [int]$metrics.Groups[5].Value -eq 15) "Stage 7 difficulty timing metrics changed."
    Assert-ProofTrue ([int]$metrics.Groups[6].Value -eq 3) "Stage 7 did not prove victory on all three difficulties."
    Assert-ProofTrue ([int]$metrics.Groups[7].Value -eq 3 -and [int]$metrics.Groups[8].Value -eq 3) "Stage 7 finite-resource no-softlock metrics regressed."
    Write-Host "$gate deterministic_replay PASS hash=$($metrics.Groups[1].Value)"
    Write-Host "$gate difficulty_matrix PASS easy_tick=$($metrics.Groups[3].Value) normal_tick=$($metrics.Groups[4].Value) hard_tick=$($metrics.Groups[5].Value)"
    Write-Host "$gate finite_resource_victory PASS victories=3 starvation_victories=3 no_softlocks=3"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
