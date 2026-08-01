# RotWK systems-first golden path (no selection rewrite unless -PublishSelection).
#
# Stages:
#   doctor           - fail-closed install + tools
#   census-maps      - multiplayer map census report
#   census-factions  - playable faction list
#   map-cook-corpus  - strict cook every official MP map (+ connectivity + ledger)
#   binding-factory  - visual-closure / object-binding burn-down + ledger %
#   faction-plans    - import-faction --plan-only (or -ConvertFactions for convert)
#   multimap-skirmish- optional generate-map-profile skirmish + catalog proof
#   product-contract - product scope contract check
#   build            - optional profile build; -PublishSelection rewrites selection
#
# Conversion ledgers land under state-root/reports/*-ledger*.jsonl with % summary.

[CmdletBinding()]
param(
    [string]$RotwkInstall = $env:ROTWK_INSTALL,
    [string]$StateRoot = "",
    [string]$Game = "rotwk",
    [int]$MapLimit = 0,
    [int]$BindingLimit = 0,
    [switch]$SkipMapCook,
    [switch]$SkipBindingFactory,
    [switch]$SkipFactionPlans,
    [switch]$ConvertFactions,
    [switch]$SkipConnectivity,
    [switch]$MultiMapSkirmish,
    [switch]$MultiMapBuild,
    [switch]$MultiMapNoBinder,
    [switch]$MultiMapFullProfile,
    [string]$BuildProfile = "",
    [switch]$PublishSelection,
    [switch]$Json
)

# Fail-closed flag combinations (do not silent-ignore publish/build intent).
if ($MultiMapBuild -and -not $MultiMapSkirmish) {
    throw "ROTWK_SYSTEMS: -MultiMapBuild requires -MultiMapSkirmish"
}
if ($MultiMapNoBinder -and -not $MultiMapSkirmish) {
    throw "ROTWK_SYSTEMS: -MultiMapNoBinder requires -MultiMapSkirmish"
}
if ($MultiMapNoBinder -and -not $MultiMapFullProfile) {
    throw "ROTWK_SYSTEMS: -MultiMapNoBinder only applies with -MultiMapFullProfile (registry path has no binder)"
}
if ($MultiMapFullProfile -and -not $MultiMapSkirmish) {
    throw "ROTWK_SYSTEMS: -MultiMapFullProfile requires -MultiMapSkirmish"
}
if ($PublishSelection) {
    $hasPublishTarget = $MultiMapBuild -or (-not [string]::IsNullOrWhiteSpace($BuildProfile))
    if (-not $hasPublishTarget) {
        throw "ROTWK_SYSTEMS: -PublishSelection requires -MultiMapBuild or -BuildProfile"
    }
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StateRoot)) {
    $StateRoot = Join-Path $repoRoot ".private\retail-work"
}
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$env:OPENBFME_IMPORT_ROOT = $StateRoot
$env:PYTHONPATH = Join-Path $repoRoot "importer"

$pythonBootstrap = Join-Path $PSScriptRoot "bootstrap-importer-python.ps1"
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $pythonBootstrap -StateRoot $StateRoot
if ($LASTEXITCODE -ne 0) { throw "Importer Python bootstrap failed ($LASTEXITCODE)" }

$python = Join-Path $StateRoot "tools\python-3.12-env\Scripts\python.exe"
$cli = Join-Path $repoRoot "tools\openbfme_import.py"
if (-not (Test-Path -LiteralPath $python)) { throw "Missing importer python: $python" }
if (-not (Test-Path -LiteralPath $cli)) { throw "Missing CLI: $cli" }

function Invoke-Importer {
    param([string[]]$Arguments, [string]$Name = "importer")
    Write-Host "ROTWK_SYSTEMS $Name :: $($Arguments -join ' ')"
    & $python $cli @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit $LASTEXITCODE" }
}

# Resolve RotWK install
if ([string]::IsNullOrWhiteSpace($RotwkInstall)) {
    $probe = @(
        "F:\RotWK",
        "D:\RotWK",
        "C:\RotWK",
        "F:\Games\RotWK",
        "${env:ProgramFiles(x86)}\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king",
        "$env:ProgramFiles\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king"
    )
    foreach ($p in $probe) {
        if (Test-Path -LiteralPath (Join-Path $p "game.dat")) {
            $RotwkInstall = $p
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($RotwkInstall) -or -not (Test-Path -LiteralPath (Join-Path $RotwkInstall "game.dat"))) {
    throw "ROTWK_INSTALL not set and no RotWK game.dat found. Set ROTWK_INSTALL or pass -RotwkInstall."
}
$RotwkInstall = [IO.Path]::GetFullPath($RotwkInstall)
$env:ROTWK_INSTALL = $RotwkInstall
Write-Host "ROTWK_SYSTEMS install=$RotwkInstall game=$Game state=$StateRoot"

Invoke-Importer -Name "bootstrap-tools" -Arguments @("bootstrap-tools")
Invoke-Importer -Name "doctor" -Arguments @("doctor", "--game", $Game, "--install", $RotwkInstall)

$reports = Join-Path $StateRoot "reports"
New-Item -ItemType Directory -Force -Path $reports | Out-Null

Invoke-Importer -Name "census-maps" -Arguments @(
    "--json", "census-maps", "--game", $Game, "--install", $RotwkInstall
)

Invoke-Importer -Name "census-factions" -Arguments @(
    "--json", "census-factions", "--game", $Game, "--install", $RotwkInstall
)

if (-not $SkipMapCook) {
    $cookArgs = @(
        (Join-Path $repoRoot "tools\rotwk_map_cook_corpus.py"),
        "--install", $RotwkInstall,
        "--game", $Game,
        "--state-root", $StateRoot
    )
    if ($MapLimit -gt 0) { $cookArgs += @("--limit", "$MapLimit") }
    if ($SkipConnectivity) { $cookArgs += "--skip-connectivity" }
    Write-Host "ROTWK_SYSTEMS map-cook-corpus"
    & $python @cookArgs
    if ($LASTEXITCODE -ne 0) { throw "map-cook-corpus failed ($LASTEXITCODE)" }
}

if (-not $SkipBindingFactory) {
    $bindArgs = @(
        (Join-Path $repoRoot "tools\rotwk_binding_factory.py"),
        "--install", $RotwkInstall,
        "--game", $Game,
        "--state-root", $StateRoot
    )
    if ($BindingLimit -gt 0) { $bindArgs += @("--limit", "$BindingLimit") }
    elseif ($MapLimit -gt 0) { $bindArgs += @("--limit", "$MapLimit") }
    Write-Host "ROTWK_SYSTEMS binding-factory"
    & $python @bindArgs
    if ($LASTEXITCODE -ne 0) { throw "binding-factory failed ($LASTEXITCODE)" }
}

if (-not $SkipFactionPlans) {
    if ($ConvertFactions) {
        Write-Host "ROTWK_SYSTEMS faction-convert-batch"
        & $python @(
            (Join-Path $repoRoot "tools\rotwk_faction_convert_batch.py"),
            "--install", $RotwkInstall,
            "--game", $Game,
            "--state-root", $StateRoot
        )
        if ($LASTEXITCODE -ne 0) { throw "faction-convert-batch failed ($LASTEXITCODE)" }
    } else {
        $factionLines = @(& $python $cli --json census-factions --game $Game --install $RotwkInstall 2>&1 | ForEach-Object { $_.ToString() })
        if ($LASTEXITCODE -ne 0) {
            throw "census-factions (plan stage) failed: $($factionLines -join [Environment]::NewLine)"
        }
        $factionDoc = ($factionLines -join [Environment]::NewLine) | ConvertFrom-Json
        $factions = @()
        if ($factionDoc.factions) {
            $factions = @($factionDoc.factions | ForEach-Object {
                if ($_.side) { "$($_.side)".ToLowerInvariant() }
                elseif ($_.id) { "$($_.id)".ToLowerInvariant() }
                elseif ($_.name -match '^Faction(.+)$') { $Matches[1].ToLowerInvariant() }
                else { $null }
            } | Where-Object { $_ })
        }
        if ($factions.Count -eq 0) {
            throw "census-factions returned no playable factions for $Game at $RotwkInstall (fail closed; no hard-coded fallback)."
        }
        foreach ($id in $factions) {
            Invoke-Importer -Name "import-faction-$id" -Arguments @(
                "import-faction", "--game", $Game, "--install", $RotwkInstall,
                "--faction", $id, "--plan-only"
            )
        }
    }
}

if ($MultiMapSkirmish) {
    $mmArgs = @(
        (Join-Path $repoRoot "tools\rotwk_multimap_skirmish.py"),
        "--install", $RotwkInstall,
        "--game", $Game,
        "--state-root", $StateRoot
    )
    if ($MultiMapFullProfile) { $mmArgs += "--full-profile" }
    if ($MultiMapNoBinder) { $mmArgs += "--no-binder" }
    if ($MultiMapBuild) {
        $mmArgs += "--build"
        if ($PublishSelection) { $mmArgs += "--publish" }
    }
    Write-Host "ROTWK_SYSTEMS multimap-skirmish"
    & $python @mmArgs
    if ($LASTEXITCODE -ne 0) { throw "multimap-skirmish failed ($LASTEXITCODE)" }
}

Write-Host "ROTWK_SYSTEMS product-contract"
& $python (Join-Path $repoRoot "tools\check-product-contracts.py") --check
if ($LASTEXITCODE -ne 0) { throw "product contracts failed" }

if (-not [string]::IsNullOrWhiteSpace($BuildProfile)) {
    $buildArgs = @(
        "build", "--game", $Game, "--install", $RotwkInstall,
        "--profile", $BuildProfile
    )
    if (-not $PublishSelection) { $buildArgs += "--no-publish" }
    Invoke-Importer -Name "build-$BuildProfile" -Arguments $buildArgs
}

Write-Host "ROTWK_SYSTEMS PASS"
exit 0
