[CmdletBinding()]
param(
    [string]$GodotPath = "",
    [string]$PrivatePackPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot "resolve-godot.ps1")
$GodotPath = Resolve-OpenBfmeGodot -RequestedPath $GodotPath -RepoRoot $repoRoot -PreferConsole
if ([string]::IsNullOrWhiteSpace($PrivatePackPath)) {
    $PrivatePackPath = $env:OPENBFME_HERO_PACK
}
if ([string]::IsNullOrWhiteSpace($PrivatePackPath)) {
    throw "Hero pack path required. Pass -PrivatePackPath or set OPENBFME_HERO_PACK to a converted pack directory containing pack.json."
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
