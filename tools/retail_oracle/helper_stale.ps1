# Reports helper executables that are missing or older than their inputs (manifest: helpers.json).
#   helper_stale.ps1 [-Name a,b] [-Manifest path] [-Quiet]
# Exit 0: every named helper (default: all) is fresh. Exit 1: at least one is stale or missing.
# Exit 2: the manifest or an input path is wrong (a loud error, never "fresh").
# HELPERS_FORCE=1 in the environment reports every helper stale (forces a rebuild).
# helpers.py implements the same rule for the tests.
param(
    [string[]]$Name = @(),
    [string]$Manifest = (Join-Path $PSScriptRoot 'helpers.json'),
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
try {
    $manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
    $base = Split-Path -Parent $manifestPath
    $m = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $names = if ($Name.Count -gt 0) { $Name } else { @($m.helpers.PSObject.Properties.Name) }
    $anyStale = $false
    foreach ($n in $names) {
        $h = $m.helpers.$n
        if ($null -eq $h) { throw "unknown helper '$n' in $manifestPath" }
        $exe = Join-Path $base $h.exe
        $reason = $null
        if ($env:HELPERS_FORCE) {
            $reason = 'HELPERS_FORCE is set'
        } elseif (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            $reason = 'not built'
        } else {
            $exeTime = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
            foreach ($rel in $h.inputs) {
                $p = Join-Path $base $rel
                if (-not (Test-Path -LiteralPath $p)) { throw "helper '$n': input '$rel' does not exist" }
                $files = if (Test-Path -LiteralPath $p -PathType Container) { Get-ChildItem -LiteralPath $p -Recurse -File } else { Get-Item -LiteralPath $p }
                foreach ($f in $files) {
                    if ($f.LastWriteTimeUtc -gt $exeTime) { $reason = "$($f.Name) is newer than the exe"; break }
                }
                if ($reason) { break }
            }
        }
        if ($reason) {
            $anyStale = $true
            if (-not $Quiet) { Write-Host "stale helper: $n ($reason); rebuild with $($m.rebuild)" }
        }
    }
    if ($anyStale) { exit 1 }
    exit 0
} catch {
    Write-Host "helper_stale.ps1: $($_.Exception.Message)"
    exit 2
}
