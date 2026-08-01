<#
.SYNOPSIS
    Prove that a built bundle's War of the Ring data is reachable with NO
    environment variable set, by running the production discovery code against
    the bundle's own pack roots.

.DESCRIPTION
    File accounting cannot answer "will the player see the filled territories".
    Only the loaders can, so this runs them: tools/wotr-bundle-probe.gd calls
    WotrSession.locate_document, WotrMapBundle.locate_and_load,
    WotrRegionGeometry.locate_and_load and the five bundles searched beside the
    region geometry - all preloaded from res://, none of them reimplemented -
    and prints one PROBE-RESULT line per bundle with the numbers it found.

    Every OPENBFME_LIVING_* and OPENBFME_WOTR_* override is REMOVED from the
    child process. The probe refuses if it finds one set, because a pass with an
    override set proves nothing about the bundle. OPENBFME_CONTENT is set to the
    bundle's own content-packs directory, which is what run-with-log.bat does.

    Exit code 0 means every bundle resolved. Anything else means at least one
    did not, and the reason names every path that was tried.

.PARAMETER Bundle
    The built bundle directory (the one holding OpenBFME.exe and content-packs).

.PARAMETER Project
    The Godot project to run the loaders from. Default: <this checkout>/game.
    The build stages a private copy under dist/bundle/.stage/<name>/game; either
    works, because the probe only reads res:// scripts and the bundle's files.

.PARAMETER Godot
    Godot binary. Default: $env:OPENBFME_GODOT (required if not passed).

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Probe-WotrBundles.ps1 -Bundle dist\bundle\OpenBFME-release-...
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Bundle,
    [string]$Project = '',
    [string]$Godot = '',
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'playable-bundle-common.ps1')

# Kept in step with wotr-bundle-probe.gd's own list; the probe re-checks them
# itself and refuses, so this array being short cannot produce a false pass.
$overrides = @(
    'OPENBFME_LIVING_WORLD_DOC', 'OPENBFME_LIVING_MAP', 'OPENBFME_LIVING_MAP_REGIONS',
    'OPENBFME_LIVING_WORLD_MARKERS', 'OPENBFME_LIVING_WORLD_REGION_IMAGES',
    'OPENBFME_LIVING_WORLD_UI', 'OPENBFME_LIVING_WORLD_STRINGS',
    'OPENBFME_LIVING_WORLD_MACROS', 'OPENBFME_WOTR_SETUP_STRINGS'
)

try {
    $bundleRoot = [IO.Path]::GetFullPath($Bundle)
    $contentRoot = Join-Path $bundleRoot 'content-packs'
    if (-not (Test-Path -LiteralPath $contentRoot -PathType Container)) {
        throw (New-BundleRefusal -Problem "That is not a built bundle - it has no content-packs directory: $bundleRoot")
    }

    $repoRoot = Get-BundleRepoRoot -StartPath $PSScriptRoot
    if ($Project -eq '') { $Project = Join-Path $repoRoot 'game' }
    $Project = [IO.Path]::GetFullPath($Project)
    if (-not (Test-Path -LiteralPath (Join-Path $Project 'project.godot') -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "No Godot project at $Project")
    }
    if ($Godot -eq '') {
        if (Test-Path Env:OPENBFME_GODOT) { $Godot = $env:OPENBFME_GODOT }
        else {
            throw (New-BundleRefusal -Problem 'Godot path not set.' -Remedy 'Pass -Godot <path> or set OPENBFME_GODOT to your Godot 4.7 console executable.')
        }
    }
    if (-not (Test-Path -LiteralPath $Godot -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "Godot executable not found: $Godot" -Remedy 'Pass -Godot <path>, or set OPENBFME_GODOT.')
    }

    # The probe script lives in tools/ and is copied into the project for the
    # run, then removed. It is the only thing that enters the project, it reads
    # nothing but res:// scripts and the bundle, and the production loaders it
    # calls are untouched.
    $probeName = 'openbfme-wotr-bundle-probe.gd'
    $probeTarget = Join-Path $Project $probeName
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'wotr-bundle-probe.gd') -Destination $probeTarget -Force
    try {
        $startInfo = New-Object Diagnostics.ProcessStartInfo
        $startInfo.FileName = [IO.Path]::GetFullPath($Godot)
        $startInfo.Arguments = "--headless --path `"$Project`" --script res://$probeName"
        $startInfo.WorkingDirectory = $Project
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        foreach ($name in $overrides) {
            if ($startInfo.EnvironmentVariables.ContainsKey($name)) {
                [void]$startInfo.EnvironmentVariables.Remove($name)
            }
        }
        $startInfo.EnvironmentVariables['OPENBFME_CONTENT'] = $contentRoot

        $process = [Diagnostics.Process]::Start($startInfo)
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill() } catch { }
            $process.WaitForExit()
            throw (New-BundleRefusal -Problem "The probe did not finish within $TimeoutSeconds seconds.")
        }
        $exitCode = $process.ExitCode
        $text = $stdout.Result + "`n" + $stderr.Result
    } finally {
        if (Test-Path -LiteralPath $probeTarget) { Remove-Item -LiteralPath $probeTarget -Force }
        $uid = "$probeTarget.uid"
        if (Test-Path -LiteralPath $uid) { Remove-Item -LiteralPath $uid -Force }
    }

    $reported = @($text -split "`r?`n" | Where-Object { $_ -cmatch '^PROBE-' })
    if ($reported.Count -eq 0) {
        Write-Host $text
        throw (New-BundleRefusal -Problem 'The probe printed no PROBE- lines at all, so nothing was proved.' -Remedy 'The output above is everything it said.')
    }
    foreach ($line in $reported) {
        $colour = 'Gray'
        if ($line -clike 'PROBE-RESULT:*FAILED*' -or $line -clike 'PROBE-RESULT: REFUSED*' -or $line -clike 'PROBE-REASON:*') { $colour = 'Red' }
        elseif ($line -clike 'PROBE-RESULT:*') { $colour = 'Green' }
        Write-Host $line -ForegroundColor $colour
    }
    Write-Host ''
    if ($exitCode -eq 0) {
        Write-Host 'PROBE PASSED - every War of the Ring bundle resolved with no environment variable set.' -ForegroundColor Green
    } else {
        Write-Host "PROBE FAILED (exit $exitCode)." -ForegroundColor Red
    }
    exit $exitCode
} catch {
    $message = $_.Exception.Message
    if ($message -cnotlike 'REFUSED:*') { $message = "REFUSED: $message" }
    Write-Host ''
    Write-Host $message -ForegroundColor Red
    Write-Host ''
    exit 1
}
