<#
.SYNOPSIS
    Verify a playable bundle: say what it is, then prove it is intact.

.DESCRIPTION
    Answers the two questions that a hand-assembled bundle could not answer:

      "What is this build?"      - commit, branch, build time, Godot version,
                                   and every content pack with its hash.
      "Is it still complete?"    - the exe, the pck, selection.json and every
                                   staged file are re-hashed and compared against
                                   the bundle's own BUILD-INFO.json.

    It also refuses anything unexpected: a pack directory that is not in the
    selection, a stray file in the content root, a pack whose directory name and
    pack.json id disagree. Extra content is not a harmless surplus - a stale pack
    sitting beside the real one overrides it at load time.

    Exit code 0 means intact. Exit code 1 means do not ship or trust it.

.PARAMETER Bundle
    The bundle directory (the one containing OpenBFME.exe).

.PARAMETER Quick
    Compare file layout and sizes only; do not re-hash file contents. Much
    faster on a multi-gigabyte bundle, and strictly weaker: it catches a missing,
    added, renamed or resized file but not a same-size corruption.

.PARAMETER Launch
    Additionally boot the build headless against its own content-packs and
    require it to reach a [ContentDB] census with no errors.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Test-PlayableBundle.ps1 -Bundle dist\bundle\OpenBFME-alpha02

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Test-PlayableBundle.ps1 -Bundle . -Quick -Launch
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Bundle = '',
    [switch]$Quick,
    [switch]$Launch,
    [int]$LaunchTimeoutSeconds = 300,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help -or $Bundle -eq '') {
    Get-Help -Full $PSCommandPath
    exit $(if ($Help) { 0 } else { 1 })
}

. (Join-Path $PSScriptRoot 'playable-bundle-common.ps1')

try {
    $bundleRoot = [IO.Path]::GetFullPath($Bundle)
    if (-not (Test-Path -LiteralPath $bundleRoot -PathType Container)) {
        throw (New-BundleRefusal -Problem "Not a directory: $bundleRoot")
    }

    Write-Host ''
    Write-Host "Verifying $bundleRoot" -ForegroundColor White

    $info = Read-BundleInfo -BundleRoot $bundleRoot

    Write-BundleHeading 'What this bundle is'
    Write-BundleStep "bundle       $($info.bundleName)"
    Write-BundleStep "built (UTC)  $($info.builtAtUtc)"
    Write-BundleStep "commit       $($info.source.commit)"
    Write-BundleStep "branch       $($info.source.branch)"
    Write-BundleStep "subject      $($info.source.commitSubject)"
    if ($info.source.dirty) {
        Write-BundleWarn "the working tree was DIRTY at build time ($($info.source.dirtyFileCount) uncommitted files); this build is not reproducible from the commit alone"
    }
    Write-BundleStep "godot        $($info.godot.version) (preset $($info.godot.preset))"
    foreach ($pack in @($info.content.packs)) {
        Write-BundleStep "$(([string]$pack.role).PadRight(12)) $($pack.id) $($pack.version) [$($pack.files) files, $(Format-BundleBytes ([long]$pack.bytes))]"
    }
    if (-not $info.redistributable) {
        Write-BundleWarn 'content is retail-derived and NOT redistributable'
    }
    $infoNames = @($info.PSObject.Properties.Name)
    if ($infoNames -contains 'warOfTheRing' -and $null -ne $info.warOfTheRing) {
        if ([bool]$info.warOfTheRing.staged) {
            Write-BundleStep "war of ring  data staged in $($info.warOfTheRing.documentRelative) ($($info.warOfTheRing.files) files)"
        } else {
            Write-BundleWarn "War of the Ring is UNAVAILABLE in this bundle: $($info.warOfTheRing.reason)"
        }
    }
    if ($infoNames -contains 'releaseNotes' -and $null -ne $info.releaseNotes) {
        Write-BundleStep "notes        $($info.releaseNotes.patchNotes) - $($info.releaseNotes.commitCount) change(s) over $($info.releaseNotes.rangeBasis)"
    }
    if ($info.launchCheck.performed) {
        Write-BundleStep "launch check $($info.launchCheck.withContentEnv.contentDb)"
    } else {
        Write-BundleWarn 'this bundle was built with -SkipLaunchCheck: it was never booted at build time'
    }

    Write-BundleHeading 'Integrity'
    if ($Quick) {
        Write-BundleWarn '-Quick: comparing layout and sizes only, not file contents.'
    } else {
        Write-BundleStep "re-hashing $($info.content.totalFiles) content files ($(Format-BundleBytes ([long]$info.content.totalBytes))); this takes a while"
    }
    $problems = @(Test-BundleTree -BundleRoot $bundleRoot -Quick:$Quick)

    if ($Launch) {
        Write-BundleHeading 'Launch'
        $logRoot = Join-Path $bundleRoot 'logs'
        if (-not (Test-Path -LiteralPath $logRoot -PathType Container)) { [void](New-Item -ItemType Directory -Path $logRoot -Force) }
        $probe = Invoke-BundleLaunchProbe -Executable (Join-Path $bundleRoot $script:BundleExe) `
            -LogPath (Join-Path $logRoot 'verify-launch.log') `
            -ContentRoot (Join-Path $bundleRoot $script:BundleContentDir) `
            -TimeoutSeconds $LaunchTimeoutSeconds
        if ($probe.timedOut) { $problems += "the build did not exit within $LaunchTimeoutSeconds seconds when booted headless" }
        elseif ($probe.exitCode -ne 0) { $problems += "the build exited with code $($probe.exitCode) when booted headless" }
        elseif ($probe.contentDb -eq '') { $problems += 'the build booted but reported no [ContentDB] census' }
        elseif ($probe.errorLines.Count -gt 0) { $problems += @($probe.errorLines | ForEach-Object { "boot error: $_" }) }
        else { Write-BundleGood "booted in $($probe.seconds)s: $($probe.contentDb)" }
    }

    Write-Host ''
    if ($problems.Count -gt 0) {
        Write-Host '================================================================' -ForegroundColor Red
        Write-Host "REFUSED: this bundle does not match its own BUILD-INFO.json." -ForegroundColor Red
        foreach ($problem in $problems) { Write-Host "  - $problem" -ForegroundColor Red }
        Write-Host '================================================================' -ForegroundColor Red
        Write-Host ''
        Write-Host 'Do not play or share it. Rebuild with tools\Build-PlayableBundle.ps1.' -ForegroundColor Yellow
        Write-Host ''
        exit 1
    }
    Write-Host 'BUNDLE VERIFIED' -ForegroundColor Green
    Write-Host "  $($info.bundleName) - commit $($info.source.shortCommit) on $($info.source.branch), built $($info.builtAtUtc) UTC"
    Write-Host "  $(@($info.content.packs).Count) content pack(s), $($info.content.totalFiles) files, $(Format-BundleBytes ([long]$info.content.totalBytes)) - all hashes match"
    if ($Quick) { Write-Host '  (layout only - file contents were not re-hashed)' -ForegroundColor Yellow }
    Write-Host ''
    exit 0
} catch {
    $message = $_.Exception.Message
    if ($message -cnotlike 'REFUSED:*') { $message = "REFUSED: $message" }
    Write-Host ''
    Write-Host '================================================================' -ForegroundColor Red
    Write-Host $message -ForegroundColor Red
    Write-Host '================================================================' -ForegroundColor Red
    Write-Host ''
    exit 1
}
