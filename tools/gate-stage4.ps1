[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$championsPath = Join-Path $repoRoot "content\openbfme-test\data\champions.json"
$stage3Gate = Join-Path $PSScriptRoot "gate-stage3.ps1"
$proofRunner = Join-Path $gameRoot "tests\stage4_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage4_visual_runner.gd"
$script:FailureOutput = ""

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Properties {
    param([object]$Object, [string[]]$Required, [string[]]$Allowed, [string]$Context)
    Assert-True ($null -ne $Object) "$Context must be an object."
    foreach ($name in $Required) {
        Assert-True ($null -ne $Object.PSObject.Properties[$name]) "$Context is missing '$name'."
    }
    foreach ($property in $Object.PSObject.Properties) {
        Assert-True ($Allowed -contains $property.Name) "$Context contains unknown property '$($property.Name)'."
    }
}

function Is-Integer {
    param([object]$Value)
    return $Value -is [int] -or $Value -is [long]
}

function Read-Json {
    param([string]$Path)
    try { return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "Invalid JSON in '$Path': $($_.Exception.Message)" }
}

function Resolve-Godot {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $candidates += $GodotPath }
    foreach ($name in @("OPENBFME_GODOT", "GODOT_CONSOLE", "GODOT_EXE", "GODOT")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($value)) { $candidates += $value }
    }
    $candidates += @(
        "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe",
        "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"
    )
    foreach ($value in $candidates) {
        $candidate = ([string]$value).Trim().Trim('"')
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    throw "Godot 4.7 was not found. Set OPENBFME_GODOT or pass -GodotPath."
}

function Invoke-Checked {
    param([string]$Step, [string]$FilePath, [string[]]$Arguments, [string]$Marker, [string]$Forbidden = "")
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $lines = @(& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $oldPreference }
    $output = $lines -join [Environment]::NewLine
    if ($exitCode -ne 0 -or $output -cnotmatch $Marker -or (-not [string]::IsNullOrWhiteSpace($Forbidden) -and $output -match $Forbidden)) {
        $script:FailureOutput = $output
        throw "$Step failed."
    }
    Write-Host "STAGE4_GATE $Step PASS"
    return $output
}

function Assert-Champions {
    $root = Read-Json $championsPath
    $rootFields = @("schema", "schemaVersion", "rulesVersion", "abilities", "progression", "status", "revival")
    Assert-Properties $root $rootFields $rootFields "champions.json"
    Assert-True ([string]$root.schema -ceq "openbfme.champions") "champions.json schema is unsupported."
    Assert-True ((Is-Integer $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "champions.json schemaVersion must be 0."
    Assert-True ((Is-Integer $root.rulesVersion) -and [int]$root.rulesVersion -eq 2) "champions.json rulesVersion must be 2."

    $abilityFields = @("code", "name", "target_mode", "activation_mode", "rank_required", "cooldown_ticks", "effect", "magnitude", "secondary_magnitude", "range_cells")
    $expectedEffects = @{
        self = @("self_heal", "guard_toggle")
        position = @("position_dash")
        friendly_entity = @("replenish")
        hostile_entity = @("damage_knockback")
    }
    $codes = @{}
    $modes = @{}
    $toggleCount = 0
    foreach ($ability in @($root.abilities)) {
        Assert-Properties $ability $abilityFields $abilityFields "ability"
        $code = if (Is-Integer $ability.code) { [int]$ability.code } else { -1 }
        Assert-True ($code -gt 0 -and -not $codes.ContainsKey($code)) "Ability codes must be positive and unique."
        $codes[$code] = $true
        Assert-True (-not [string]::IsNullOrWhiteSpace([string]$ability.name)) "Ability names must not be empty."
        $mode = [string]$ability.target_mode
        Assert-True ($expectedEffects.ContainsKey($mode)) "Ability '$code' has an invalid target mode."
        $modes[$mode] = $true
        Assert-True ($expectedEffects[$mode] -contains [string]$ability.effect) "Ability '$code' effect does not match its target mode."
        $activationMode = [string]$ability.activation_mode
        Assert-True (@("instant", "toggle") -contains $activationMode) "Ability '$code' activation_mode is invalid."
        if ($activationMode -eq "toggle") {
            $toggleCount += 1
            Assert-True ($mode -eq "self" -and [string]$ability.effect -ceq "guard_toggle") "Toggle abilities must use the self guard_toggle contract."
        }
        else {
            Assert-True ([string]$ability.effect -cne "guard_toggle") "guard_toggle must use toggle activation."
        }
        foreach ($field in @("rank_required", "cooldown_ticks", "magnitude", "secondary_magnitude", "range_cells")) {
            Assert-True (Is-Integer $ability.$field) "Ability '$code' $field must be an integer."
        }
        Assert-True ([int]$ability.rank_required -ge 1) "Ability '$code' rank gate is invalid."
        Assert-True ([int]$ability.cooldown_ticks -ge 0 -and [int]$ability.magnitude -ge 0 -and [int]$ability.secondary_magnitude -ge 0 -and [int]$ability.range_cells -ge 0) "Ability '$code' has a negative value."
    }
    Assert-True ($modes.Count -eq 4) "Abilities must cover all four target modes."
    Assert-True ($toggleCount -eq 1) "Abilities must define exactly one persistent toggle proof."

    $progressionFields = @("rank_thresholds", "replenish_health_permille", "xp_damage_permille", "xp_per_member_defeat", "xp_per_champion_defeat", "replenish_delay_ticks", "replenish_interval_ticks", "replenish_members_per_interval", "stances")
    Assert-Properties $root.progression $progressionFields $progressionFields "progression"
    $thresholds = @($root.progression.rank_thresholds)
    Assert-True ($thresholds.Count -ge 2 -and (Is-Integer $thresholds[0]) -and [int]$thresholds[0] -eq 0) "Rank thresholds must start at zero."
    $previous = -1
    foreach ($threshold in $thresholds) {
        Assert-True ((Is-Integer $threshold) -and [int]$threshold -gt $previous) "Rank thresholds must be strictly increasing integers."
        $previous = [int]$threshold
    }
    Assert-True ((Is-Integer $root.progression.replenish_health_permille) -and [int]$root.progression.replenish_health_permille -ge 1 -and [int]$root.progression.replenish_health_permille -le 1000) "Replenishment health permille is invalid."
    Assert-True ((Is-Integer $root.progression.xp_damage_permille) -and [int]$root.progression.xp_damage_permille -ge 0 -and [int]$root.progression.xp_damage_permille -le 1000) "Progression xp_damage_permille is invalid."
    foreach ($field in @("xp_per_member_defeat", "xp_per_champion_defeat", "replenish_interval_ticks", "replenish_members_per_interval")) {
        Assert-True ((Is-Integer $root.progression.$field) -and [int]$root.progression.$field -gt 0) "Progression $field must be positive."
    }
    Assert-True ((Is-Integer $root.progression.replenish_delay_ticks) -and [int]$root.progression.replenish_delay_ticks -ge 0) "Progression replenish_delay_ticks may not be negative."
    $stanceFields = @("code", "damage_permille", "armor_permille", "speed_permille")
    $stances = @{}
    foreach ($stance in @($root.progression.stances)) {
        Assert-Properties $stance $stanceFields $stanceFields "stance"
        $code = [string]$stance.code
        Assert-True (-not [string]::IsNullOrWhiteSpace($code) -and -not $stances.ContainsKey($code)) "Stance codes must be nonempty and unique."
        $stances[$code] = $true
        foreach ($field in @("damage_permille", "armor_permille", "speed_permille")) {
            Assert-True ((Is-Integer $stance.$field) -and [int]$stance.$field -ge 100 -and [int]$stance.$field -le 3000) "Stance '$code' $field is invalid."
        }
    }
    foreach ($requiredStance in @("balanced", "aggressive", "defensive", "hold")) {
        Assert-True $stances.ContainsKey($requiredStance) "Missing '$requiredStance' stance."
    }

    $statusFields = @("fear_power_floor", "flee_steps_per_tick", "terror_power_bonus", "champion_fear_immune", "rank_immunity_threshold")
    Assert-Properties $root.status $statusFields $statusFields "status"
    Assert-True ((Is-Integer $root.status.fear_power_floor) -and [int]$root.status.fear_power_floor -gt 0) "fear_power_floor must be positive."
    Assert-True ((Is-Integer $root.status.flee_steps_per_tick) -and [int]$root.status.flee_steps_per_tick -ge 1 -and [int]$root.status.flee_steps_per_tick -le 8) "flee_steps_per_tick is invalid."
    Assert-True ((Is-Integer $root.status.terror_power_bonus) -and [int]$root.status.terror_power_bonus -ge 0) "terror_power_bonus is invalid."
    Assert-True ($root.status.champion_fear_immune -is [bool] -and $root.status.champion_fear_immune -eq $true) "champion_fear_immune must be true for the proof."
    Assert-True ((Is-Integer $root.status.rank_immunity_threshold) -and [int]$root.status.rank_immunity_threshold -ge 1) "rank_immunity_threshold must be positive."

    $revivalFields = @("base_cost", "rank_cost", "death_cost", "revive_health_permille")
    Assert-Properties $root.revival $revivalFields $revivalFields "revival"
    foreach ($field in @("base_cost", "rank_cost", "death_cost")) {
        Assert-True ((Is-Integer $root.revival.$field) -and [int]$root.revival.$field -ge 0) "Revival $field may not be negative."
    }
    Assert-True ((Is-Integer $root.revival.revive_health_permille) -and [int]$root.revival.revive_health_permille -ge 1 -and [int]$root.revival.revive_health_permille -le 1000) "revive_health_permille is invalid."
}

try {
    $stage3Arguments = @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $stage3Gate)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $stage3Arguments += @("-GodotPath", $GodotPath) }
    [void](Invoke-Checked "stage3_regression" "powershell.exe" $stage3Arguments "(?m)^STAGE3_GATE PASS\s*$")
    Assert-Champions
    Write-Host "STAGE4_GATE bundle PASS"

    Assert-True (Test-Path -LiteralPath $proofRunner -PathType Leaf) "Missing Stage 4 proof runner."
    Assert-True (Test-Path -LiteralPath $visualRunner -PathType Leaf) "Missing Stage 4 visual runner."
    $godot = Resolve-Godot
    $forbidden = "(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b"
    $proof = Invoke-Checked "godot_stage4_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage4_proof_runner.gd") "(?m)^STAGE4_GODOT_PROOF PASS authority=gdscript-proof assertions=\d+\s*$" $forbidden
    $visual = Invoke-Checked "godot_stage4_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage4_visual_runner.gd") "(?m)^STAGE4_VISUAL_PROOF PASS assertions=\d+\s*$" $forbidden
    $metrics = [regex]::Match($proof, "(?m)^STAGE4_METRICS repeat_hash=([0-9A-F]{8}) assertions=(\d+)\s*$")
    Assert-True ($metrics.Success -and [int]$metrics.Groups[2].Value -ge 95) "Stage 4 proof assertion coverage regressed below 95."
    $visualMetrics = [regex]::Match($visual, "(?m)^STAGE4_VISUAL_PROOF PASS assertions=(\d+)\s*$")
    Assert-True ($visualMetrics.Success -and [int]$visualMetrics.Groups[1].Value -ge 60) "Stage 4 visual assertion coverage regressed below 60."
    Write-Host "STAGE4_GATE deterministic_replay PASS hash=$($metrics.Groups[1].Value)"
    Write-Host "STAGE4_GATE PASS"
    exit 0
}
catch {
    Write-Host "STAGE4_GATE FAIL $($_.Exception.Message)" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($script:FailureOutput)) {
        Write-Host "STAGE4_GATE child-output-begin"
        Write-Host (($script:FailureOutput -split "`r?`n" | Select-Object -Last 120) -join [Environment]::NewLine)
        Write-Host "STAGE4_GATE child-output-end"
    }
    exit 1
}
