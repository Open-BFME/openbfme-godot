#Requires -Version 5.1
<#
.SYNOPSIS
Build the private, version-pinned three-layer RotWK 2.02 v9.7.7 catalog root.

.DESCRIPTION
Creates directory junctions only; it never copies or alters retail payloads.
An existing non-matching layered root is refused unless -ReplaceExisting is
provided, in which case it is moved to a timestamped sibling backup first.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Bfme2Install,

    [Parameter(Mandatory = $true)]
    [string]$Rotwk201Install,

    [Parameter(Mandatory = $true)]
    [string]$Patch202Overlay,

    [string]$StateRoot = '',
    [string]$Python = '',
    [switch]$ReplaceExisting
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($StateRoot)) {
    $StateRoot = Join-Path $repoRoot 'workspace\retail-work'
}
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'workspace'))
if (-not ($StateRoot -eq $workspaceRoot -or $StateRoot.StartsWith($workspaceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
    throw "StateRoot must stay below the ignored workspace directory: $workspaceRoot"
}

function Resolve-RequiredDirectory {
    param([string]$Value, [string]$Marker, [string]$Label)
    $resolved = (Resolve-Path -LiteralPath $Value -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolved $Marker) -PathType Leaf)) {
        throw "$Label is missing $Marker`: $resolved"
    }
    return [IO.Path]::GetFullPath($resolved)
}

$bfme2 = Resolve-RequiredDirectory $Bfme2Install 'game.dat' 'BFME2 1.06 install'
$rotwk = Resolve-RequiredDirectory $Rotwk201Install 'game.dat' 'RotWK 2.01 install'
$overlay = Resolve-RequiredDirectory $Patch202Overlay '__patch202.big' 'Patch 2.02 v9.7.7 overlay'
foreach ($relative in @('_202music.big', '___hdrotwk.v.0.9.big', 'lang\englishpatch202.big')) {
    if (-not (Test-Path -LiteralPath (Join-Path $overlay $relative) -PathType Leaf)) {
        throw "Patch 2.02 v9.7.7 overlay is missing $relative`: $overlay"
    }
}

$editions = Join-Path $StateRoot 'editions\rotwk'
$layered = Join-Path $editions 'layered-install'
$desired = [ordered]@{
    'layer-0-patch202' = $overlay
    'layer-1-rotwk' = $rotwk
    'layer-2-bfme2' = $bfme2
}

$matches = Test-Path -LiteralPath $layered -PathType Container
if ($matches) {
    foreach ($name in $desired.Keys) {
        $path = Join-Path $layered $name
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            $matches = $false
            break
        }
        $item = Get-Item -LiteralPath $path -Force
        $targets = @($item.Target)
        if ($item.LinkType -ne 'Junction' -or $targets.Count -ne 1 -or [IO.Path]::GetFullPath($targets[0]) -ne $desired[$name]) {
            $matches = $false
            break
        }
    }
}

if ((Test-Path -LiteralPath $layered) -and -not $matches) {
    if (-not $ReplaceExisting) {
        throw "Layered root exists but does not match the v9.7.7 contract: $layered. Re-run with -ReplaceExisting to archive it first."
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $editions "layered-install-backup-$stamp"
    if (Test-Path -LiteralPath $backup) {
        throw "Refusing to overwrite backup: $backup"
    }
    Move-Item -LiteralPath $layered -Destination $backup
    Write-Host "ROTWK_202_BASELINE archived_previous=$backup"
    $matches = $false
}

if (-not $matches) {
    New-Item -ItemType Directory -Force -Path $editions | Out-Null
    New-Item -ItemType Directory -Path $layered | Out-Null
    foreach ($name in $desired.Keys) {
        New-Item -ItemType Junction -Path (Join-Path $layered $name) -Target $desired[$name] | Out-Null
    }
}

if ([string]::IsNullOrWhiteSpace($Python)) {
    $pinned = Join-Path $StateRoot 'tools\python-3.12-env\Scripts\python.exe'
    if (Test-Path -LiteralPath $pinned -PathType Leaf) {
        $Python = $pinned
    } else {
        $Python = 'py'
    }
}
$verifyScript = Join-Path $repoRoot 'tools\verify-rotwk-202-baseline.py'
if ([IO.Path]::GetFileNameWithoutExtension($Python) -eq 'py') {
    & $Python -3 $verifyScript --root $layered
} else {
    & $Python $verifyScript --root $layered
}
if ($LASTEXITCODE -ne 0) {
    throw "Pinned RotWK 2.02 baseline verification failed with exit $LASTEXITCODE"
}

Write-Host "ROTWK_202_BASELINE PASS root=$layered"
