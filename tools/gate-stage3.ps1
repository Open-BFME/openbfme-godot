[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$defensesPath = Join-Path $repoRoot "content\openbfme-test\data\defenses.json"
$stage2Gate = Join-Path $PSScriptRoot "gate-stage2.ps1"
$proofRunner = Join-Path $gameRoot "tests\stage3_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage3_visual_runner.gd"
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
    Write-Host "STAGE3_GATE $Step PASS"
    return $output
}

function Assert-Defenses {
    $root = Read-Json $defensesPath
    Assert-Properties $root @("schema", "schemaVersion", "structures") @("schema", "schemaVersion", "structures") "defenses.json"
    Assert-True ([string]$root.schema -ceq "openbfme.defenses") "defenses.json schema is unsupported."
    Assert-True ((Is-Integer $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "defenses.json schemaVersion must be 0."
    $rows = @($root.structures)
    Assert-True ($rows.Count -eq 4) "defenses.json must define the four Stage 3 proof structures."
    $ids = @{}
    $slots = @{}
    $kinds = @{}
    $baseAllowed = @("id", "displayName", "buildMenuSlot", "kind", "maximumHealth", "cost", "blocksMovement", "visionRadiusCells")
    $fireAllowed = $baseAllowed + @("canFire", "rangeCells", "cooldownTicks", "projectileSpeedCellsPerTick", "damage")
    $attachmentAllowed = @("id", "displayName", "buildMenuSlot", "kind", "maximumHealth", "cost", "attachment", "compatibleBaseKinds", "canFire", "rangeCells", "cooldownTicks", "projectileSpeedCellsPerTick", "damage", "visionRadiusCells")
    foreach ($row in $rows) {
        $context = "defense '$($row.id)'"
        $kind = [string]$row.kind
        $required = @("id", "displayName", "buildMenuSlot", "kind", "maximumHealth", "cost")
        $allowed = if ($kind -eq "wall_tower") { $attachmentAllowed } elseif ($kind -eq "tower") { $fireAllowed } else { $baseAllowed }
        Assert-Properties $row $required $allowed $context
        $id = [string]$row.id
        Assert-True (-not [string]::IsNullOrWhiteSpace($id) -and -not $ids.ContainsKey($id)) "$context has an invalid or duplicate id."
        $ids[$id] = $true
        Assert-True (@("wall", "gate", "tower", "wall_tower") -contains $kind -and -not $kinds.ContainsKey($kind)) "$context has an invalid or duplicate kind."
        $kinds[$kind] = $true
        Assert-True ((Is-Integer $row.buildMenuSlot) -and [int]$row.buildMenuSlot -ge 0 -and -not $slots.ContainsKey([int]$row.buildMenuSlot)) "$context has an invalid or duplicate buildMenuSlot."
        $slots[[int]$row.buildMenuSlot] = $true
        Assert-True ((Is-Integer $row.maximumHealth) -and [int]$row.maximumHealth -gt 0) "$context maximumHealth must be positive."
        Assert-True ((Is-Integer $row.cost) -and [int]$row.cost -ge 0) "$context cost may not be negative."
        if ($kind -eq "wall_tower") {
            Assert-True ($row.attachment -eq $true -and (@($row.compatibleBaseKinds) -join ",") -ceq "wall") "wall_tower must be a wall-only attachment."
        }
        else {
            Assert-True ($row.blocksMovement -eq $true) "$context must block movement."
        }
        if ($kind -eq "tower" -or $kind -eq "wall_tower") {
            foreach ($field in @("rangeCells", "cooldownTicks", "projectileSpeedCellsPerTick", "damage")) {
                Assert-True ((Is-Integer $row.$field) -and [int]$row.$field -gt 0) "$context $field must be positive."
            }
            Assert-True ($row.canFire -eq $true) "$context must opt into projectile fire."
        }
        Assert-True ((Is-Integer $row.visionRadiusCells) -and [int]$row.visionRadiusCells -ge 0) "$context visionRadiusCells is invalid."
    }
}

try {
    $stage2Arguments = @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $stage2Gate)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $stage2Arguments += @("-GodotPath", $GodotPath) }
    [void](Invoke-Checked "stage2_regression" "powershell.exe" $stage2Arguments "(?m)^STAGE2_GATE PASS\s*$")
    Assert-Defenses
    Write-Host "STAGE3_GATE bundle PASS"

    Assert-True (Test-Path -LiteralPath $proofRunner -PathType Leaf) "Missing Stage 3 proof runner."
    Assert-True (Test-Path -LiteralPath $visualRunner -PathType Leaf) "Missing Stage 3 visual runner."
    $godot = Resolve-Godot
    $forbidden = "(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b"
    $proof = Invoke-Checked "godot_stage3_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage3_proof_runner.gd") "(?m)^STAGE3_GODOT_PROOF PASS authority=gdscript-proof assertions=\d+\s*$" $forbidden
    $visual = Invoke-Checked "godot_stage3_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage3_visual_runner.gd") "(?m)^STAGE3_VISUAL_PROOF PASS assertions=\d+\s*$" $forbidden
    $metrics = [regex]::Match($proof, "(?m)^STAGE3_METRICS local_edit_ms=([0-9]+(?:\.[0-9]+)?) repeat_hash=([0-9A-F]{8}) assertions=(\d+)\s*$")
    Assert-True $metrics.Success "Stage 3 proof did not emit metrics."
    Assert-True ([double]$metrics.Groups[1].Value -lt 100.0) "Stage 3 local topology edit exceeded 100 ms."
    Assert-True ([int]$metrics.Groups[3].Value -ge 70) "Stage 3 proof assertion coverage regressed below 70."
    $visualMetrics = [regex]::Match($visual, "(?m)^STAGE3_VISUAL_PROOF PASS assertions=(\d+)\s*$")
    Assert-True ($visualMetrics.Success -and [int]$visualMetrics.Groups[1].Value -ge 40) "Stage 3 visual assertion coverage regressed below 40."
    Write-Host "STAGE3_GATE local_edit_budget PASS ms=$($metrics.Groups[1].Value)"
    Write-Host "STAGE3_GATE PASS"
    exit 0
}
catch {
    Write-Host "STAGE3_GATE FAIL $($_.Exception.Message)" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($script:FailureOutput)) {
        Write-Host "STAGE3_GATE child-output-begin"
        Write-Host (($script:FailureOutput -split "`r?`n" | Select-Object -Last 100) -join [Environment]::NewLine)
        Write-Host "STAGE3_GATE child-output-end"
    }
    exit 1
}
