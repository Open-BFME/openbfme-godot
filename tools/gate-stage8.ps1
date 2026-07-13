[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE8_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$stage7Gate = Join-Path $PSScriptRoot "gate-stage7.ps1"
$mapPath = Join-Path $repoRoot "content\openbfme-test\data\stage8_maps.json"
$proofRunner = Join-Path $gameRoot "tests\stage8_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage8_visual_runner.gd"
$scenePath = Join-Path $gameRoot "scenes\stage8_lab.tscn"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Assert-CellArray {
    param([object[]]$Cells, [int]$ExpectedCount, [int]$Width, [int]$Height, [string]$Context)
    if ($ExpectedCount -ge 0) {
        Assert-ProofTrue ($Cells.Count -eq $ExpectedCount) "$Context must contain exactly $ExpectedCount cells."
    } else {
        Assert-ProofTrue ($Cells.Count -gt 0) "$Context must not be empty."
    }
    $seen = @{}
    foreach ($cell in $Cells) {
        Assert-ProofTrue ($null -ne $cell -and $cell.Count -eq 2) "$Context entries must be [x,y] pairs."
        Assert-ProofTrue ((Test-ProofInteger $cell[0]) -and (Test-ProofInteger $cell[1])) "$Context coordinates must be integers."
        $x = [int]$cell[0]
        $y = [int]$cell[1]
        Assert-ProofTrue ($x -ge 0 -and $x -lt $Width -and $y -ge 0 -and $y -lt $Height) "$Context cell [$x,$y] is outside the map."
        $key = "$x,$y"
        Assert-ProofTrue (-not $seen.ContainsKey($key)) "$Context contains duplicate cell [$x,$y]."
        $seen[$key] = $true
    }
    return $seen
}

function Assert-Stage8Maps {
    Assert-ProofTrue (Test-Path -LiteralPath $mapPath -PathType Leaf) "Missing Stage 8 map definitions."
    $root = Read-ProofJson $mapPath
    $topFields = @("schema", "schemaVersion", "rulesVersion", "maps")
    Assert-ProofProperties $root $topFields $topFields "stage8 map root"
    Assert-ProofTrue ([string]$root.schema -eq "openbfme.skirmish-maps") "Unexpected Stage 8 map schema."
    Assert-ProofTrue ((Test-ProofInteger $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "Stage 8 schemaVersion must be 0."
    Assert-ProofTrue ((Test-ProofInteger $root.rulesVersion) -and [int]$root.rulesVersion -eq 1) "Stage 8 rulesVersion must be 1."
    Assert-ProofTrue (@($root.maps).Count -eq 4) "Stage 8 requires exactly four maps."

    $requiredIds = @("verdant-crossing", "slate-foothills", "emberwood-edge", "sable-barrens")
    $mapFields = @("id", "displayName", "widthCells", "heightCells", "startCells", "resourceCells", "blockerCells", "lighting")
    $ids = @{}
    foreach ($map in @($root.maps)) {
        Assert-ProofProperties $map $mapFields $mapFields "stage8 map"
        $id = [string]$map.id
        Assert-ProofTrue ($requiredIds -contains $id -and $id -match '^[a-z0-9]+(?:-[a-z0-9]+)*$' -and -not $ids.ContainsKey($id)) "Stage 8 map id '$id' is invalid or duplicated."
        $ids[$id] = $true
        $display = [string]$map.displayName
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace($display) -and $display -notmatch '(?i)anduin|fangorn|mordor|gondor|tolkien|middle.?earth') "Stage 8 map '$id' must use an original legal-safe display name."
        Assert-ProofTrue ((Test-ProofInteger $map.widthCells) -and (Test-ProofInteger $map.heightCells)) "Stage 8 map dimensions must be integers."
        $width = [int]$map.widthCells
        $height = [int]$map.heightCells
        Assert-ProofTrue ($width -ge 12 -and $width -le 64 -and $height -ge 10 -and $height -le 64) "Stage 8 map '$id' dimensions are outside proof bounds."
        Assert-ProofTrue ([string]$map.lighting -match '^[a-z]+(?:-[a-z]+)*$') "Stage 8 map '$id' lighting id is invalid."
        $starts = Assert-CellArray @($map.startCells) 2 $width $height "$id startCells"
        [void](Assert-CellArray @($map.resourceCells) 4 $width $height "$id resourceCells")
        $blockers = Assert-CellArray @($map.blockerCells) -1 $width $height "$id blockerCells"
        foreach ($key in $starts.Keys) {
            Assert-ProofTrue (-not $blockers.ContainsKey($key)) "Stage 8 map '$id' blocks start cell $key."
        }
    }
    foreach ($required in $requiredIds) {
        Assert-ProofTrue $ids.ContainsKey($required) "Stage 8 is missing required original map '$required'."
    }
}

try {
    Assert-ProofTrue (Test-Path -LiteralPath $stage7Gate -PathType Leaf) "Missing Stage 7 gate dependency."
    [void](Invoke-ProofPriorGate $gate "stage7_regression" $stage7Gate $GodotPath '(?m)^STAGE7_GATE PASS\s*$')
    Assert-Stage8Maps
    Write-Host "$gate bundle PASS"

    foreach ($path in @($proofRunner, $visualRunner, $scenePath)) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing Stage 8 dependency: $path"
    }
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $proof = Invoke-ProofChecked $gate "godot_stage8_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage8_proof_runner.gd") '(?m)^STAGE8_GODOT_PROOF PASS authority=gdscript-proof assertions=\d+\s*$' $forbiddenDiagnostics
    $visual = Invoke-ProofChecked $gate "godot_stage8_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage8_visual_runner.gd") '(?m)^STAGE8_VISUAL_PROOF PASS assertions=\d+\s*$' $forbiddenDiagnostics
    $metrics = [regex]::Match($proof, '(?m)^STAGE8_METRICS repeat_hash=([0-9A-F]{8}) save_bytes=(\d+) assertions=(\d+)\s*$')
    Assert-ProofTrue ($metrics.Success -and [int]$metrics.Groups[2].Value -ge 400 -and [int]$metrics.Groups[3].Value -ge 45) "Stage 8 proof metrics or assertion coverage regressed."
    $visualMetrics = [regex]::Match($visual, '(?m)^STAGE8_VISUAL_PROOF PASS assertions=(\d+)\s*$')
    Assert-ProofTrue ($visualMetrics.Success -and [int]$visualMetrics.Groups[1].Value -ge 24) "Stage 8 visual coverage regressed below 24 assertions."
    Write-Host "$gate deterministic_save PASS hash=$($metrics.Groups[1].Value) bytes=$($metrics.Groups[2].Value)"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
