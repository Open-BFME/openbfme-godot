# lint-complexity.ps1 - cyclomatic-complexity gate for GDScript (gdradon / gdtoolkit).
#
# Runs gdradon over game/src/**, ranks every function (radon-style A-F), and
# FAILS (exit 1) if any function in a file listed in tools/lint-complexity-gated.txt
# ranks worse than C (D/E/F). The gated list is a RATCHET: new/touched files get
# added; legacy files join as they are refactored.
#
# Usage: powershell -File tools\lint-complexity.ps1 [-Top N]
#   -Top N   also print the worst N functions repo-wide (default 30)
param([int]$Top = 30)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$srcRoot = Join-Path $repo "game\src"
$gatedFile = Join-Path $repo "tools\lint-complexity-gated.txt"

$gdradon = Get-Command gdradon -ErrorAction SilentlyContinue
if ($null -eq $gdradon) {
    Write-Host "ERROR: gdradon not found. Install with: pip install gdtoolkit"
    exit 2
}

# Gated list: repo-relative paths, '#' comments and blanks ignored.
$gated = @()
if (Test-Path $gatedFile) {
    $gated = Get-Content $gatedFile | ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne "" -and -not $_.StartsWith("#") } |
        ForEach-Object { ($_ -replace "/", "\").ToLower() }
}

# Run gdradon over all of game/src. Parse errors go to stderr; route through cmd
# so PS 5.1 does not wrap stderr lines in NativeCommandError.
$errTmp = Join-Path $env:TEMP "gdradon-stderr.txt"
$raw = cmd /c "`"$($gdradon.Source)`" cc `"$srcRoot`" 2>`"$errTmp`""
if (Test-Path $errTmp) { $raw = @($raw) + @(Get-Content $errTmp) }

$functions = @()
$parseFailures = @()
$currentFile = $null
$fileCount = 0
foreach ($line in $raw) {
    if ($line -match "^Cannot process file '(.+?)'") {
        $parseFailures += $Matches[1]
        continue
    }
    if ($line -notmatch "^\s") {
        if ($line.Trim() -ne "") { $currentFile = $line.Trim(); $fileCount++ }
        continue
    }
    # "    F 24:0 _init - A (1)"  (also C for class, M for method)
    if ($line -match "^\s+[A-Z]+\s+(\d+):\d+\s+(\S+)\s+-\s+([A-F])\s+\((\d+)\)") {
        $rel = $currentFile
        if ($rel.ToLower().StartsWith($repo.ToLower())) {
            $rel = $rel.Substring($repo.Length).TrimStart("\")
        }
        $functions += [pscustomobject]@{
            File = $rel; Line = [int]$Matches[1]; Function = $Matches[2]
            Rank = $Matches[3]; Score = [int]$Matches[4]
        }
    }
}

# Summary per rank.
Write-Host "gdradon: $fileCount files analyzed, $($parseFailures.Count) parse failures, $($functions.Count) functions"
$summary = $functions | Group-Object Rank | Sort-Object Name
Write-Host ("rank summary: " + (($summary | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join "  "))
if ($parseFailures.Count -gt 0) {
    Write-Host "parse failures (not gated, gdtoolkit parser gaps):"
    $parseFailures | ForEach-Object { Write-Host "  $_" }
}

# Worst offenders repo-wide (informational).
$badRanks = @("D", "E", "F")
$worst = $functions | Sort-Object Score -Descending | Select-Object -First $Top
Write-Host ""
Write-Host "worst $Top functions repo-wide:"
$worst | ForEach-Object { Write-Host ("  {0} ({1})  {2}:{3}  {4}" -f $_.Rank, $_.Score, $_.File, $_.Line, $_.Function) }

# The gate: D/E/F in gated files fails.
$offenders = @($functions | Where-Object {
    $badRanks -contains $_.Rank -and $gated -contains $_.File.ToLower()
} | Sort-Object Score -Descending)

Write-Host ""
Write-Host "gated files ($($gated.Count)): max allowed rank C"
if ($offenders.Count -gt 0) {
    Write-Host "FAIL: $($offenders.Count) function(s) exceed rank C in gated files:"
    $offenders | ForEach-Object { Write-Host ("  {0} ({1})  {2}:{3}  {4}" -f $_.Rank, $_.Score, $_.File, $_.Line, $_.Function) }
    exit 1
}
Write-Host "PASS: no function exceeds rank C in gated files"
exit 0
