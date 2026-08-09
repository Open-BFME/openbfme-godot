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

$payload = [ordered]@{
    schema        = 'openbfme.build-info'
    schemaVersion = 0
    build         = $count
    commit        = $commit
    generatedUtc  = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}

$directory = Split-Path -Parent $OutFile
if (-not (Test-Path $directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
($payload | ConvertTo-Json) | Out-File -FilePath $OutFile -Encoding utf8
Write-Output "build $count ($commit) -> $OutFile"
