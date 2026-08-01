# Portable Godot 4.7 resolution for OpenBFME.
# Order: explicit arg -> env (OPENBFME_GODOT, GODOT_CONSOLE, GODOT_EXE, GODOT)
#      -> repo .tools/godot/ -> PATH (godot) -> fail closed.
# No maintainer-machine paths (Downloads\godot47, absolute home layouts).

[CmdletBinding()]
param(
    [string]$RequestedPath = "",
    [string]$RepoRoot = "",
    [switch]$PreferConsole,
    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-OpenBfmeGodot {
    param(
        [string]$RequestedPath = "",
        [string]$RepoRoot = "",
        [switch]$PreferConsole
    )
    if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
        $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
    } else {
        $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $candidates.Add($RequestedPath.Trim().Trim('"'))
    }
    foreach ($name in @("OPENBFME_GODOT", "GODOT_CONSOLE", "GODOT_EXE", "GODOT")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            $candidates.Add($value.Trim().Trim('"'))
        }
    }

    $toolsGodot = Join-Path $RepoRoot ".tools\godot"
    if ($PreferConsole) {
        $candidates.Add((Join-Path $toolsGodot "Godot_v4.7-stable_win64_console.exe"))
        $candidates.Add((Join-Path $toolsGodot "Godot_v4.7-stable_win64.exe"))
    } else {
        $candidates.Add((Join-Path $toolsGodot "Godot_v4.7-stable_win64.exe"))
        $candidates.Add((Join-Path $toolsGodot "Godot_v4.7-stable_win64_console.exe"))
    }

    foreach ($cmd in @("godot", "Godot_v4.7-stable_win64_console", "Godot_v4.7-stable_win64")) {
        $found = Get-Command $cmd -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $found -and -not [string]::IsNullOrWhiteSpace($found.Source)) {
            $candidates.Add($found.Source)
        }
    }

    foreach ($value in $candidates) {
        if ([string]::IsNullOrWhiteSpace($value)) { continue }
        if (Test-Path -LiteralPath $value -PathType Leaf) {
            return [IO.Path]::GetFullPath($value)
        }
    }

    throw @"
Godot 4.7 was not found.

Set one of:
  OPENBFME_GODOT   (preferred)
  GODOT_CONSOLE / GODOT_EXE / GODOT
Or place a Godot 4.7 binary under:
  .tools\godot\Godot_v4.7-stable_win64_console.exe
  .tools\godot\Godot_v4.7-stable_win64.exe
Or put ``godot`` on PATH.
"@
}

# When dotted (`` . .\resolve-godot.ps1 ``), only define the function.
# When executed directly, resolve and print path (and set env for child processes).
$isDotSourced = $MyInvocation.InvocationName -eq '.' -or $MyInvocation.Line -match '^\s*\.'
if (-not $isDotSourced) {
    try {
        $godot = Resolve-OpenBfmeGodot -RequestedPath $RequestedPath -RepoRoot $RepoRoot -PreferConsole:$PreferConsole
        $env:OPENBFME_GODOT = $godot
        if (-not $Quiet) { Write-Output $godot }
        exit 0
    } catch {
        if (-not $Quiet) { Write-Error $_.Exception.Message }
        exit 1
    }
}
