[CmdletBinding()]
param(
    [string]$GodotPath = "",
    [string]$PrivatePackPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($GodotPath)) {
    $GodotPath = $env:OPENBFME_GODOT
}
if ([string]::IsNullOrWhiteSpace($GodotPath)) {
    $GodotPath = "$env:USERPROFILE\Downloads\godot47\Godot_v4.7-stable_win64_console.exe"
}
if (-not (Test-Path -LiteralPath $GodotPath -PathType Leaf)) {
    throw "Godot console executable not found: $GodotPath"
}
if ([string]::IsNullOrWhiteSpace($PrivatePackPath)) {
    $PrivatePackPath = Join-Path $repoRoot ".private/scratch/jobs/m3-hero-selection-surface/private-pack-state/packs/m3-hero-selection-private"
}
$PrivatePackPath = [IO.Path]::GetFullPath($PrivatePackPath)
if (-not (Test-Path -LiteralPath (Join-Path $PrivatePackPath "pack.json") -PathType Leaf)) {
    throw "Converted private hero pack not found: $PrivatePackPath"
}
$savedContent = $env:OPENBFME_CONTENT
$savedHeroPack = $env:OPENBFME_HERO_PACK
try {
    $env:OPENBFME_CONTENT = ""
    $env:OPENBFME_HERO_PACK = $PrivatePackPath
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $GodotPath --headless --quit-after 1000 --path (Join-Path $repoRoot "game") --script res://tests/hero_selection_surface_runner.gd 2>&1 | Out-String
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    Write-Host $output.TrimEnd()
    if ($code -ne 0 -or $output -notmatch '(?m)^HERO_SELECTION_SURFACE_OK passed=\d+ failed=0\s*$') {
        throw "Hero selection surface check failed with exit code $code."
    }
}
finally {
    $env:OPENBFME_CONTENT = $savedContent
    $env:OPENBFME_HERO_PACK = $savedHeroPack
}
