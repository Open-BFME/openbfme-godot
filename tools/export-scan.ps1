<#
.SYNOPSIS
    Publication boundary: reject retail-derived bytes and developer-machine
    specifics before anything is published.

.DESCRIPTION
    Two independent phases.

    Phase 1 (tree scan) walks a directory and rejects retail formats, donor
    references and reparse points. Defaults to game/, which is what the Godot
    export packages.

    Phase 2 (tracked scan) covers what a *source* release actually ships: every
    file git tracks, anywhere in the repository. Phase 1 alone was never enough
    — docs/, importer/, tools/ and the root .bat launchers are all published
    but sit outside game/. Phase 2 additionally rejects developer-machine
    specifics (home directories, private drive paths, LAN addresses) that leak
    the maintainer's setup into a public repo.

    Skip phase 2 with -SkipTracked, or run it alone with -TrackedOnly.

.PARAMETER Root
    Directory for phase 1. Defaults to the repository's game/ folder. Passing
    an explicit -Root runs phase 1 only (fixture mode used by the self-test).
#>
[CmdletBinding()]
param(
    [string]$Root = "",
    [switch]$SkipTracked,
    [switch]$TrackedOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
# An explicit -Root means "scan just this tree" (fixtures, ad-hoc checks).
$explicitRoot = -not [string]::IsNullOrWhiteSpace($Root)
if (-not $explicitRoot) {
    $Root = Join-Path $repoRoot "game"
}
if ($explicitRoot) {
    $SkipTracked = $true
}
$scanRoot = [IO.Path]::GetFullPath($Root)
if (-not (Test-Path -LiteralPath $scanRoot -PathType Container)) {
    throw "Export scan root does not exist: $scanRoot"
}

$forbiddenExtensions = @(
    ".apt", ".big", ".csf", ".dds", ".ini", ".map", ".skudef", ".str", ".tga", ".w3d", ".wnd"
)
$textExtensions = @(
    ".cfg", ".gd", ".godot", ".json", ".md", ".ps1", ".py", ".txt", ".tscn"
)
$forbiddenText = '(?i)(?:[A-Z]:[\\/]BFME2(?:[\\/]|$)|workspace/retail-extract|EA Games|Electronic Arts|OpenSAGE(?:\.|/|\\)|Sage\.Game|Sage\.Ini)'
$ignoredSegments = @(".godot", ".import")
$rootPrefix = $scanRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar

$violations = [Collections.Generic.List[string]]::new()
$files = [Collections.Generic.List[IO.FileInfo]]::new()
$pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
$rootItem = Get-Item -LiteralPath $scanRoot -Force
if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    $violations.Add("directory reparse point is not export-safe: .")
}
else {
    $pending.Push($rootItem)
}
while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($entry in Get-ChildItem -LiteralPath $directory.FullName -Force) {
        $relative = $entry.FullName.Substring($rootPrefix.Length)
        $segments = @($relative -split '[\\/]')
        if ($entry.PSIsContainer) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                $violations.Add("directory reparse point is not export-safe: $($relative.Replace('\', '/'))")
            }
            elseif (-not ($segments | Where-Object { $ignoredSegments -contains $_ })) {
                $pending.Push([IO.DirectoryInfo]$entry)
            }
            continue
        }
        if (-not ($segments | Where-Object { $ignoredSegments -contains $_ })) {
            $files.Add([IO.FileInfo]$entry)
        }
    }
}

$totalBytes = [long]0
foreach ($file in $files) {
    $relative = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
    $totalBytes += [long]$file.Length
    $segments = @($relative -split '/')
    if ($segments -contains "workspace/retail-extract") {
        $violations.Add("forbidden directory: $relative")
    }
    if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant()) {
        $violations.Add("forbidden retail-format extension: $relative")
    }
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $violations.Add("reparse point is not export-safe: $relative")
        continue
    }
    if ($textExtensions -contains $file.Extension.ToLowerInvariant()) {
        $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 -ErrorAction Stop
        if ($text -match $forbiddenText) {
            $violations.Add("forbidden path, metadata, or donor reference: $relative")
        }
    }
}

if ($violations.Count -gt 0) {
    foreach ($violation in $violations) {
        Write-Host "EXPORT_SCAN violation=$violation"
    }
    Write-Host "EXPORT_SCAN FAIL violations=$($violations.Count)"
    exit 1
}

Write-Host "EXPORT_SCAN PASS files=$($files.Count) bytes=$totalBytes root=$scanRoot"

if ($SkipTracked) {
    exit 0
}

# ---------------------------------------------------------------------------
# Phase 2 — every git-tracked file, repository-wide.
# ---------------------------------------------------------------------------

# Developer-machine specifics that must never reach a public repository.
# Each entry is name => regex. Keep these anchored enough to avoid matching
# prose; a violation here blocks a release, so false positives are expensive.
$personalPatterns = [ordered]@{
    # Any Windows home directory other than the documented placeholders.
    "home directory" = '(?i)[A-Z]:[\\/]+Users[\\/]+(?!Example\b|Public\b|Default\b|%|\$|<)[A-Za-z0-9._-]+'
    # The maintainer's private retail install drive.
    "private retail drive" = '(?i)\bF:[\\/]+(BFME2|ROTWK)\b'
    # Private LAN hosts (RFC1918). Documented test literals are allow-listed.
    "private LAN address" = '(?i)\b(?:192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})\b'
    # Credentials that must never be committed in any form.
    "credential-shaped token" = '(?i)\b(?:ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|gh[opsu]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN [A-Z ]*PRIVATE KEY-----)'
    # The publish/update target moves. It must be resolved at runtime from
    # config/release-source.json, never written into code, CI, or the launcher.
    # Retired targets are named explicitly so they cannot creep back as a
    # "temporary" fallback.
    "hardcoded release target" = '(?i)\b(?:Ancalgonn|open-bfme-engine|openbmfe-godot|openbfme-godot)\b'
}

# Allow-list: path => list of pattern names that are reviewed and intentional.
# Add a row only with a reason; this is the audited exception surface.
$allowList = @{
    # Address-validation tests need literal RFC1918 addresses as input data.
    # These are documentation-style example addresses, not real hosts.
    "game/tests/retail_mp_menu_runner.gd"   = @("private LAN address")
    "game/tests/menu_match_cycle_runner.gd" = @("private LAN address")
    # Same rationale: classify_address() must be proven to sort RFC1918 ranges
    # into NETWORK_LAN and the Radmin 26.0.0.0/8 range into NETWORK_RADMIN, so
    # the literals ARE the test inputs. Substituting non-RFC1918 addresses would
    # invert what the assertion proves.
    "game/tests/lan_discovery_runner.gd"    = @("private LAN address")
    # The scanner's own pattern table.
    "tools/export-scan.ps1"                 = @("home directory", "private retail drive", "private LAN address", "credential-shaped token", "hardcoded release target")
    # Diagnostics redaction tests. The whole point of these fixtures is to feed
    # the redactor a path that looks like somebody's home directory or retail
    # install and prove it never reaches a bug report. The literals ARE the test
    # input - substituting a safe string would invert what the assertion proves.
    # None of them name THIS machine; they are foreign-looking by construction.
    "game/src/core/diag_log.gd"                       = @("home directory")
    "game/tests/diagnostics_log_runner.gd"            = @("home directory")
    "launcher/OpenBFME.Launcher.Tests/Program.cs"     = @("home directory")
    "importer/tests/test_diagnostics.py"              = @("home directory", "private retail drive")
    # THE one file allowed to name the publish target. Everything else must
    # resolve it at runtime via tools/release_source.py or release-source.mjs.
    "config/release-source.json"            = @("hardcoded release target")
    # Names the retired targets in order to assert they are never used.
    "importer/tests/test_release_source.py" = @("hardcoded release target")
    # The release-pipeline gate. It must name the forbidden repository literals to
    # assert the workflow and release scripts never contain them, and it plants a
    # fake developer path inside a bundle fixture to prove the artifact scanner
    # rejects one. Both are inputs to negative tests, not real values.
    "tools/release/Test-ReleaseTools.ps1"   = @("hardcoded release target", "home directory")
    # Public clone instructions must name the active repository so strangers can
    # download the tree. Retired names remain forbidden outside this allow-list.
    "README.md"                             = @("hardcoded release target")
    "docs/ONBOARDING.md"                    = @("hardcoded release target")
    "docs/BUILD_AND_RELEASE.md"             = @("hardcoded release target")
}

$trackedViolations = [Collections.Generic.List[string]]::new()
Push-Location -LiteralPath $repoRoot
try {
    $tracked = @(& git ls-files 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "EXPORT_SCAN_TRACKED SKIP reason=not-a-git-checkout"
        exit 0
    }
}
finally {
    Pop-Location
}

$scannedText = 0
foreach ($relative in $tracked) {
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    $full = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }

    $extension = [IO.Path]::GetExtension($relative).ToLowerInvariant()
    if ($forbiddenExtensions -contains $extension) {
        $trackedViolations.Add("tracked retail-format extension: $relative")
        continue
    }
    if (@($relative -split '/') -contains "workspace/retail-extract") {
        $trackedViolations.Add("tracked forbidden directory: $relative")
        continue
    }
    # Text-like files get the content patterns. Anything else is binary and is
    # covered by the extension rule plus the provenance ledger.
    if (-not ($textExtensions -contains $extension -or $extension -in @(".bat", ".cmd", ".mjs", ".js", ".ts", ".yml", ".yaml", ".toml", ".cs", ".csproj", ".sln", ".gitignore", ".gitattributes", ""))) {
        continue
    }

    try {
        $text = Get-Content -LiteralPath $full -Raw -Encoding UTF8 -ErrorAction Stop
    }
    catch {
        continue
    }
    if ($null -eq $text) { continue }
    $scannedText++

    $allowed = @()
    if ($allowList.ContainsKey($relative)) { $allowed = $allowList[$relative] }

    foreach ($name in $personalPatterns.Keys) {
        if ($allowed -contains $name) { continue }
        $match = [regex]::Match($text, $personalPatterns[$name])
        if ($match.Success) {
            $line = ($text.Substring(0, $match.Index) -split "`n").Count
            $sample = $match.Value
            if ($sample.Length -gt 60) { $sample = $sample.Substring(0, 60) + "..." }
            $trackedViolations.Add("$name at ${relative}:${line} -> $sample")
        }
    }
    # NOTE: $forbiddenText is deliberately NOT applied here. It bans naming EA
    # / Electronic Arts, which is correct for the exported game tree but wrong
    # for the source repository — the legal docs must name the rights holder,
    # and retail-install detection must match real "EA Games" directory names.
}

if ($trackedViolations.Count -gt 0) {
    foreach ($violation in $trackedViolations) {
        Write-Host "EXPORT_SCAN_TRACKED violation=$violation"
    }
    Write-Host "EXPORT_SCAN_TRACKED FAIL violations=$($trackedViolations.Count) tracked=$($tracked.Count)"
    exit 1
}

Write-Host "EXPORT_SCAN_TRACKED PASS tracked=$($tracked.Count) text_scanned=$scannedText"
exit 0
