[CmdletBinding()]
param([string]$GodotPath = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "AMBIENT_MOD_CLEAN"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$runner = Join-Path $gameRoot "tests\ambient_mod_clean_runner.gd"
$report = Join-Path $repoRoot "workspace\logs\P0-REPO-AMBIENT-MOD-001\ambient-mod-clean.json"
$exampleRoot = Join-Path $repoRoot "examples\mods\example_hard_orcs"
$marker = '(?m)^AMBIENT_MOD_CLEAN PASS tracked_shipping_packs=0 example=examples/mods/example_hard_orcs\s*$'
$forbidden = '(?i)(?:SCRIPT ERROR|Parse Error|WARNING:|watchdog (?:abort|timeout)|ObjectDB instances leaked|RID allocations|AMBIENT_MOD_CLEAN FAIL|ambient pack at res://mods/example_hard_orcs|res://mods/example_hard_orcs is NOT mounted)'

try {
    Assert-ProofTrue (Test-Path -LiteralPath $runner -PathType Leaf) "Ambient-mod runner is missing."
    Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $exampleRoot "pack.json") -PathType Leaf) "Non-shipping example pack.json is missing."
    Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $exampleRoot "units\orc.json") -PathType Leaf) "Non-shipping example unit is missing."
    $expectedPackBlob = ([string](& git -C $repoRoot rev-parse "7178a63b85295f2e4dbc00cd4e83309eb5bf6ba4:game/mods/example_hard_orcs/pack.json")).Trim()
    $expectedUnitBlob = ([string](& git -C $repoRoot rev-parse "7178a63b85295f2e4dbc00cd4e83309eb5bf6ba4:game/mods/example_hard_orcs/units/orc.json")).Trim()
    $currentPackBlob = ([string](& git -C $repoRoot rev-parse ":examples/mods/example_hard_orcs/pack.json")).Trim()
    $currentUnitBlob = ([string](& git -C $repoRoot rev-parse ":examples/mods/example_hard_orcs/units/orc.json")).Trim()
    Assert-ProofTrue ($expectedPackBlob -ceq $currentPackBlob -and $expectedUnitBlob -ceq $currentUnitBlob) "Example Git blobs changed during the move."
    $expectedLoaderBlob = ([string](& git -C $repoRoot rev-parse "7178a63b85295f2e4dbc00cd4e83309eb5bf6ba4:game/src/content/mod_loader.gd")).Trim()
    $currentLoaderBlob = ([string](& git -C $repoRoot hash-object -- "game/src/content/mod_loader.gd")).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and $expectedLoaderBlob -match '^[0-9a-f]{40}$' -and $currentLoaderBlob -ceq $expectedLoaderBlob) "ModLoader bytes changed from the assignment revision."
    $trackedShippingPacks = @(& git -C $repoRoot ls-files -- 'game/mods/**/pack.json' | Where-Object {
        Test-Path -LiteralPath (Join-Path $repoRoot $_) -PathType Leaf
    })
    Assert-ProofTrue ($LASTEXITCODE -eq 0) "Tracked shipping-pack census failed."
    Assert-ProofTrue ($trackedShippingPacks.Count -eq 0) "Tracked pack.json remains under game/mods: $($trackedShippingPacks -join ', ')"
    $commonGitDir = ([string](& git -C $repoRoot rev-parse --path-format=absolute --git-common-dir)).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $commonGitDir -PathType Container)) "Shared Git directory could not be resolved."
    $controlRoot = [IO.Path]::GetFullPath((Split-Path -Parent $commonGitDir))
    $toolRoot = $repoRoot
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot ".tools\godot") -PathType Container)) { $toolRoot = $controlRoot }
    $godot = Resolve-ProofGodot $GodotPath $toolRoot
    $classCache = Join-Path $gameRoot ".godot\global_script_class_cache.cfg"
    if (-not (Test-Path -LiteralPath $classCache -PathType Leaf)) {
        $sharedClassCache = Join-Path $controlRoot "game\.godot\global_script_class_cache.cfg"
        Assert-ProofTrue (Test-Path -LiteralPath $sharedClassCache -PathType Leaf) "Pinned Godot class registry is unavailable."
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $classCache) | Out-Null
        Copy-Item -LiteralPath $sharedClassCache -Destination $classCache
    }
    $initialIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $report) | Out-Null
    $previousReport = $env:OPENBFME_AMBIENT_MOD_REPORT
    $previousExampleRoot = $env:OPENBFME_AMBIENT_MOD_EXAMPLE_ROOT
    $isolatedUserData = Join-Path $repoRoot ("workspace\logs\P0-REPO-AMBIENT-MOD-001\user-data-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $isolatedUserData | Out-Null
    try {
        $env:OPENBFME_AMBIENT_MOD_REPORT = $report
        $env:OPENBFME_AMBIENT_MOD_EXAMPLE_ROOT = $exampleRoot
        $output = Invoke-ProofChecked $gate "shipping_runtime" $godot @(
            "--headless", "--path", $gameRoot, "--user-data-dir", $isolatedUserData,
            "--script", "res://tests/ambient_mod_clean_runner.gd"
        ) $marker $forbidden
    }
    finally {
        $env:OPENBFME_AMBIENT_MOD_REPORT = $previousReport
        $env:OPENBFME_AMBIENT_MOD_EXAMPLE_ROOT = $previousExampleRoot
    }
    Assert-ProofTrue (Test-Path -LiteralPath $report -PathType Leaf) "Runner did not write the declared report."
    $receipt = Read-ProofJson $report
    Assert-ProofTrue ([string]$receipt.schema -eq "openbfme.ambient-mod-clean" -and [int]$receipt.schemaVersion -eq 1) "Report schema changed."
    Assert-ProofTrue ([bool]$receipt.shippingAmbientExampleAbsent -and [bool]$receipt.explicitSourceMounted) "Runtime containment proof failed."
    Assert-ProofTrue ([string]$receipt.explicitPackId -eq "example_hard_orcs" -and [int]$receipt.mountedRootCount -eq 1) "Explicit example mount identity changed."
    Assert-ProofTrue ([int]$receipt.trackedShippingPacks -eq 0 -and [string]$receipt.example -eq "examples/mods/example_hard_orcs") "Report paths/counts changed."
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$initialIdentity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$initialIdentity.dirtyStateDigest
    ) "Working-tree identity changed during the ambient-mod gate."
    Write-Host "$gate PASS tracked_shipping_packs=0 example=examples/mods/example_hard_orcs"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
