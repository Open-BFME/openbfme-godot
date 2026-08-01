[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$LogRoot,
    [int]$QuitAfter = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$exe = [IO.Path]::GetFullPath($Executable)
$logs = [IO.Path]::GetFullPath($LogRoot)
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Exported executable is missing." }
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$stdout = Join-Path $logs "launch.stdout.log"
$stderr = Join-Path $logs "launch.stderr.log"
$process = Start-Process -FilePath $exe `
    -ArgumentList @("--headless", "--quit-after", $QuitAfter.ToString()) `
    -WorkingDirectory ([IO.Path]::GetDirectoryName($exe)) `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr `
    -WindowStyle Hidden `
    -PassThru `
    -Wait
if ($process.ExitCode -ne 0) { throw "Export smoke test exited $($process.ExitCode)." }
$text = ((Get-Content -LiteralPath $stdout, $stderr -Raw -ErrorAction SilentlyContinue) -join "`n")
if ($text -match '(?im)^(ERROR|WARNING):') {
    throw "Export smoke test reported $([regex]::Match($text, '(?im)^(ERROR|WARNING):[^\r\n]*').Value)"
}
if ($text -notmatch 'Godot Engine v4\.7') { throw "Export smoke test did not start Godot 4.7." }
Write-Host "WINDOWS_EXPORT_SMOKE_PASS executable=$exe"
