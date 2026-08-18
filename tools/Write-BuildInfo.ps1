<#
.SYNOPSIS
    Writes game/data/build_info.json from the current git checkout.

.DESCRIPTION
    The shell's build identity (main_menu.gd -> src/core/build_info.gd) reads this
    file, because an export ships no .git and a menu that shelled out to git would
    have no answer in the only build that matters. Run this before packaging - and
    after the commit you are packaging, since the count includes it.

    Two git calls and one file write; nothing here is allowed to be slow enough to
    want skipping.
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'

if (-not $OutFile) {
    $OutFile = Join-Path $RepoRoot 'game/data/build_info.json'
}

$count = (& git -C $RepoRoot rev-list --count HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $count) {
    throw "git rev-list failed in $RepoRoot; no build number to write."
}
$commit = (& git -C $RepoRoot rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commit) {
    throw "git rev-parse failed in $RepoRoot; no commit to write."
}

# The product version comes from VERSION at the repository root and from nowhere
# else. It is carried in here because an export ships no VERSION file either, and
# a build that cannot state its own beta number is the "which build is this?"
# problem wearing a different hat. Absent VERSION is a refusal, not a blank: a
# published folder named v0.2.1 whose game says nothing would be worse than no
# number at all.
$versionFile = Join-Path $RepoRoot 'VERSION'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw "VERSION is missing at $versionFile; there is no product version to write."
}
$version = ([IO.File]::ReadAllText($versionFile)).Trim()
if ($version -cnotmatch '^[0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?$') {
    throw "VERSION does not hold a version: '$version'"
}

$payload = [ordered]@{
    schema        = 'openbfme.build-info'
    schemaVersion = 1
    version       = $version
    build         = $count
    commit        = $commit
    generatedUtc  = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}

$directory = Split-Path -Parent $OutFile
if (-not (Test-Path $directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
($payload | ConvertTo-Json) | Out-File -FilePath $OutFile -Encoding utf8
Write-Output "v$version build $count ($commit) -> $OutFile"
