[CmdletBinding()]
param([string]$GodotPath = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($GodotPath)) {
    $GodotPath = $env:OPENBFME_GODOT
}
if ([string]::IsNullOrWhiteSpace($GodotPath)) {
    $GodotPath = "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe"
}
if (-not (Test-Path -LiteralPath $GodotPath -PathType Leaf)) {
    throw "Godot console executable not found: $GodotPath"
}
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
