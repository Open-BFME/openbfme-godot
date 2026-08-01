[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

function Resolve-Godot {
    param([string]$Requested)
    . (Join-Path $PSScriptRoot "resolve-godot.ps1")
    return Resolve-OpenBfmeGodot -RequestedPath $Requested -RepoRoot $repoRoot -PreferConsole
}

try {
    $godot = Resolve-Godot $GodotPath
    $godotOutput = @(& $godot --version 2>&1 | ForEach-Object { $_.ToString() })
    $godotExitCode = $LASTEXITCODE
    $godotVersion = ($godotOutput | Select-Object -First 1).Trim()
    if ($godotExitCode -ne 0 -or $godotVersion -notmatch '^4\.7\.') {
        throw "Expected Godot 4.7, got '$godotVersion'."
    }
    $dotnet = Get-Command dotnet -ErrorAction Stop | Select-Object -First 1
    $dotnetOutput = @(& $dotnet.Source --version 2>&1 | ForEach-Object { $_.ToString() })
    $dotnetExitCode = $LASTEXITCODE
    $dotnetVersion = ($dotnetOutput | Select-Object -First 1).Trim()
    if ($dotnetExitCode -ne 0 -or $dotnetVersion -ne '10.0.100') {
        throw "Expected .NET SDK 10.0.100, got '$dotnetVersion'."
    }
    $python = Get-Command python -ErrorAction Stop | Select-Object -First 1
    $pythonOutput = @(& $python.Source --version 2>&1 | ForEach-Object { $_.ToString() })
    $pythonExitCode = $LASTEXITCODE
    $pythonVersion = ($pythonOutput | Select-Object -First 1).Trim()
    if ($pythonExitCode -ne 0) { throw "Python version probe failed." }
    foreach ($required in @(
        "game\project.godot",
        "content\openbfme-test\pack.json",
        "content\openbfme-test\provenance\manifest.json",
        "global.json"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $required) -PathType Leaf)) {
            throw "Missing required file: $required"
        }
    }
    Write-Host "OPENBFME_DOCTOR godot=$godotVersion path=$godot"
    Write-Host "OPENBFME_DOCTOR dotnet=$dotnetVersion path=$($dotnet.Source)"
    Write-Host "OPENBFME_DOCTOR python=$pythonVersion path=$($python.Source)"
    Write-Host "OPENBFME_DOCTOR PASS"
    exit 0
}
catch {
    Write-Host "OPENBFME_DOCTOR FAIL $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
