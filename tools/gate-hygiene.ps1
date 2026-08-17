#Requires -Version 5
<#
.SYNOPSIS
    Fails if the repo root sprawls, a tracked path is lane debris, or a
    tracked text file carries a machine-absolute user path or a retired
    workspace directory name.

.DESCRIPTION
    Three checks, all fail-closed:

    (a) Every file at the repository root must be on the allowlist below.
        Directories are ignored. The allowlist is what is legitimate today,
        not a wish list.

    (b) No tracked path may match \.lane- (root .lane-* is git-ignored and
        forbidden).

    (c) No tracked text file may contain a machine-absolute user profile
        path or the retired workspace directory name, except historical
        notes under docs/patch-notes/, the orchestration audit trail, and
        the frozen orphan-runner evidence CSV.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
} else {
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
}

# Legitimate root files. Enumerated from the tree after the stage-3 cleanup.
# Add a name here only when a new root file is an intended, lasting entry point.
$RootAllowlist = @(
    '.gitattributes'
    '.gitignore'
    'AGENTS.md'
    'CONTRIBUTING.md'
    'DIRECTION.md'
    'LICENSE'
    'README.md'
    'SECURITY.md'
    'VERSION'
    'global.json'
    'import_faction.bat'
    'import_gui.bat'
    'import_unit.bat'
    'run_doctor.bat'
    'run_game.bat'
    'run_importer.bat'
    'run_importer_tests.bat'
    'run_launcher.bat'
    'run_m2_acceptance.bat'
    'run_retail_pack_tests.bat'
    'run_retail_pipeline_tests.bat'
    'run_retail_slice.bat'
    'run_rotwk_full_content.bat'
    'run_rotwk_one_button.bat'
    'run_rotwk_systems.bat'
)

function Test-HygieneExcludedPath {
    param([string]$RelativePath)
    $norm = $RelativePath.Replace('\', '/')
    if ($norm.StartsWith('docs/patch-notes/', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    if ($norm.StartsWith('orchestration/', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    if ($norm.Equals('tools/orphan-runners-manifest.csv', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    return $false
}

function Add-GitGrepHits {
    param(
        [string]$Label,
        [string[]]$GitArgs
    )
    $raw = & git -C $RepoRoot @GitArgs 2>$null
    if ($LASTEXITCODE -eq 1) { return }
    if ($LASTEXITCODE -ne 0) {
        throw "git grep failed ($Label) exit=$LASTEXITCODE"
    }
    foreach ($line in @($raw)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $rel = ($line -split ':', 2)[0]
        if (Test-HygieneExcludedPath -RelativePath $rel) { continue }
        $script:offenders.Add("${Label}: $line")
    }
}

$script:offenders = New-Object 'System.Collections.Generic.List[string]'

# (a) root file allowlist
$rootFiles = @(Get-ChildItem -LiteralPath $RepoRoot -Force -File)
foreach ($file in $rootFiles) {
    if ($RootAllowlist -notcontains $file.Name) {
        $script:offenders.Add("root-file-not-allowlisted: $($file.Name)")
    }
}

# (b) tracked .lane- paths
$tracked = @(& git -C $RepoRoot ls-files -- . 2>$null)
if ($LASTEXITCODE -ne 0) {
    Write-Host "HYGIENE_GATE FAIL reason=not-a-git-checkout"
    exit 1
}
foreach ($path in $tracked) {
    if ($path -match '\.lane-') {
        $script:offenders.Add("tracked-lane-debris: $path")
    }
}

# (c) Needles built at runtime so this file does not contain the forbidden literals.
$absUserRe = '[A-Za-z]:' + [regex]::Escape(('\' + 'Users' + '\'))
$retiredFwd = '.' + 'private' + '/'
$retiredBack = '.' + 'private' + '\'

Add-GitGrepHits -Label 'machine-absolute-path' -GitArgs @('grep', '-n', '-E', $absUserRe, '--', '.')
Add-GitGrepHits -Label 'retired-workspace-dirname' -GitArgs @('grep', '-n', '-F', $retiredFwd, '--', '.')
Add-GitGrepHits -Label 'retired-workspace-dirname' -GitArgs @('grep', '-n', '-F', $retiredBack, '--', '.')

if ($script:offenders.Count -gt 0) {
    Write-Host "HYGIENE_GATE FAIL count=$($script:offenders.Count)"
    foreach ($item in $script:offenders) {
        Write-Host "  $item"
    }
    exit 1
}

Write-Host "HYGIENE_GATE PASS root-files=$($rootFiles.Count) tracked=$($tracked.Count)"
exit 0
