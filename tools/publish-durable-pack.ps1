# Publish the workspace content selection into the durable user pack cache.
#
# Launches that cannot see the repo workspace (exported builds, installs on
# another machine) resolve content from the durable Godot user cache
# (user://content-packs). This script mirrors the current workspace selection
# (.private/content-packs/selection.json plus every pack bundle it names) into
# that cache so such launches play the same content as env-driven runs.
#
# Release firewall: this copies retail-derived bytes ONLY into the local user
# cache under %APPDATA%. It never stages them into the repository or any
# distributable artifact.
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$DurableRoot = (Join-Path $env:APPDATA "Godot\app_userdata\Open BFME\content-packs"),
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ($WorkspaceRoot -eq "") {
    # $PSScriptRoot is not available in param defaults under Windows PowerShell 5.1.
    $repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
    $WorkspaceRoot = Join-Path $repoRoot ".private\content-packs"
}

function Test-SafeRelativePack([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative)) { return $false }
    $normalized = $Relative.Replace("\", "/")
    if ($normalized.StartsWith("/") -or $normalized.StartsWith("~") -or $normalized.Contains(":")) { return $false }
    foreach ($segment in $normalized.Split("/")) {
        if ($segment -eq "" -or $segment -eq "." -or $segment -eq "..") { return $false }
    }
    return $true
}

$selectionPath = Join-Path $WorkspaceRoot "selection.json"
if (-not (Test-Path $selectionPath)) {
    Write-Error "No workspace selection at $selectionPath - nothing to publish."
}
$selection = Get-Content $selectionPath -Raw | ConvertFrom-Json
if ($selection.schema -ne "openbfme.pack-selection" -or $selection.schemaVersion -ne 0) {
    Write-Error "Unsupported selection schema in $selectionPath"
}

$packs = @($selection.activePack)
if ($null -ne $selection.supplementalPacks) {
    $packs += @($selection.supplementalPacks)
}

foreach ($pack in $packs) {
    if (-not (Test-SafeRelativePack $pack)) {
        Write-Error "Unsafe pack path in selection: $pack"
    }
    $source = Join-Path $WorkspaceRoot ($pack.Replace("/", "\"))
    if (-not (Test-Path (Join-Path $source "pack.json"))) {
        Write-Error "Selection names a pack with no pack.json: $source"
    }
}

Write-Host "Publishing $($packs.Count) pack bundle(s) from $WorkspaceRoot"
Write-Host "                                         to $DurableRoot"
foreach ($pack in $packs) {
    $source = Join-Path $WorkspaceRoot ($pack.Replace("/", "\"))
    $target = Join-Path $DurableRoot ($pack.Replace("/", "\"))
    Write-Host "  $pack"
    if ($DryRun) { continue }
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    # /MIR keeps the immutable bundle byte-identical; unchanged files are skipped.
    robocopy $source $target /MIR /NJH /NJS /NDL /NFL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) {
        Write-Error "robocopy failed for $pack (exit $LASTEXITCODE)"
    }
}

if (-not $DryRun) {
    # The selection document is written last so a partially copied publish can
    # never be selected: until this file lands, the old selection stays active.
    $staged = Join-Path $DurableRoot "selection.json.publishing"
    Copy-Item $selectionPath $staged -Force
    Move-Item $staged (Join-Path $DurableRoot "selection.json") -Force
    Write-Host "Durable selection updated: $(Join-Path $DurableRoot 'selection.json')"
} else {
    Write-Host "Dry run - nothing copied."
}
