[CmdletBinding()]
param([string]$GodotPath = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot "resolve-godot.ps1")
$GodotPath = Resolve-OpenBfmeGodot -RequestedPath $GodotPath -RepoRoot $repoRoot -PreferConsole
$savedContent = $env:OPENBFME_CONTENT
try {
    $env:OPENBFME_CONTENT = ""
    $output = & $GodotPath --headless --path (Join-Path $repoRoot "game") --script res://tests/playable_unit_runtime_consumer_runner.gd 2>&1 | Out-String
    $code = $LASTEXITCODE
    Write-Host $output.TrimEnd()
    if ($code -ne 0 -or $output -notmatch '(?m)^PLAYABLE_UNIT_RUNTIME_CONSUMER_OK passed=\d+ failed=0\s*$') {
        throw "Playable-unit runtime consumer check failed with exit code $code."
    }
}
finally {
    $env:OPENBFME_CONTENT = $savedContent
}
