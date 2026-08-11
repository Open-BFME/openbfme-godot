<#
.SYNOPSIS
    Publish a numbered beta build to dist\v<version>\ - the folder you copy to
    another machine and run.

.DESCRIPTION
    ONE COMMAND per milestone. It does not reimplement the build: it wraps
    tools/Build-PlayableBundle.ps1, which already exports the game, stages the
    selected content packs beside the exe, proves the staged bytes hash to the
    source packs, boots the result headless twice (with and without
    OPENBFME_CONTENT) and refuses at every step it cannot prove. What this adds
    is the part that was missing - a NUMBER, and the notes that go with it.

        dist\v0.2.1\
          OpenBFME.exe / OpenBFME.pck      the game
          content-packs\                   the selected packs, staged beside it
          PATCH-NOTES.md                   docs/patch-notes/v0.2.1.md, verbatim
          VERSION.json                     version + build + commit, one file
          PATCH-NOTES.txt / CHANGES.txt    generated from git history
          BUILD-INFO.txt / BUILD-INFO.json exactly what this is
          run-with-log.bat                 play it, with a log
          launcher\                        -Rc only
          launcher-install-root\           -Rc only
          Play-With-Launcher.bat           -Rc only

    SELF-SUFFICIENT OR REFUSED. The point of this folder is that it runs on a
    machine that has never seen this project: no environment variables, no
    Godot, no retail install. The wrapped build already probes exactly that -
    it boots the exported exe with OPENBFME_CONTENT unset and compares the
    content census against the run that had it set. This script reads that
    verdict out of BUILD-INFO.json and REFUSES to finish a publish that failed
    it, because "works on the machine that built it" is the failure mode, not
    the test.

    THE VERSION COMES FROM ONE PLACE. `VERSION` at the repository root. Four
    files repeat it and all four are compared before anything is built; see
    tools/dist-pipeline-common.ps1. Bumping the number is editing one file and
    running tools/Write-BuildInfo.ps1.

    THE RELEASE FIREWALL IS CHECKED, NOT ASSUMED. dist\ carries converted retail
    bytes. Before the build and again after it, git is asked whether the dist
    root is ignored and whether it tracks anything there. Either answer being
    wrong stops the publish.

.PARAMETER Rc
    Also build the WPF launcher and the install root it reads, so the folder is
    the release-candidate shape rather than just a playable build. The launcher
    has no "browse to a game" control - it follows
    <install-root>/current.json to <install-root>/versions/<version>/ and
    verifies those files by hash - so the layout is built here rather than
    left to the tester to guess. Requires the .NET SDK.

.PARAMETER Version
    Publish under this version instead of VERSION. Refuses unless it matches
    VERSION, unless -AllowVersionOverride is also given; the override exists for
    a re-publish of an already-tagged number, not for skipping the bump.

.PARAMETER Godot
    Godot 4.7 executable. Default: OPENBFME_GODOT, then tools/resolve-godot.ps1.

.PARAMETER ContentRoot
    Pack cache to publish from. Default: whatever Build-PlayableBundle resolves,
    which is OPENBFME_CONTENT then .private/content-packs in this worktree then
    in the main one. The resolved path and the packs selection.json names are
    printed and recorded, never assumed.

.PARAMETER DistRoot
    Where dist\v<version>\ is created. Default: <main checkout>\dist. A worktree
    is temporary; a build published into one disappears with it.

.PARAMETER AllowDirty
    Publish from a working copy with uncommitted changes. Off by default: the
    generated change log is made from committed history, so uncommitted work
    would be in the build and absent from every record of it.

.PARAMETER AllowEnvDependentContent
    Finish a publish whose build does NOT resolve its content without
    OPENBFME_CONTENT. The folder is then not self-sufficient and both this
    script and BUILD-INFO say so. Never use it for a build you are handing to
    another machine.

.PARAMETER AllowMissingWotrData
    Passed through: publish although one or more War of the Ring bundles are
    absent. The build names each one and what the player loses.

.PARAMETER Force
    Replace an existing dist\v<version>\.

.PARAMETER Zip
    Also produce dist\v<version>.zip with SHA256SUMS.txt, for handing over a
    shared folder.

.PARAMETER SkipLaunchCheck
    Passed through. Also disables the self-sufficiency refusal, because there is
    then no probe to judge - and says so rather than passing quietly.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Publish-DistBuild.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Publish-DistBuild.ps1 -Rc
#>
[CmdletBinding()]
param(
    [switch]$Rc,
    [string]$Version = '',
    [switch]$AllowVersionOverride,
    [string]$Godot = '',
    [string]$ContentRoot = '',
    [string]$DistRoot = '',
    [switch]$AllowDirty,
    [switch]$AllowEnvDependentContent,
    [switch]$AllowMissingWotrData,
    [switch]$Force,
    [switch]$Zip,
    [switch]$SkipLaunchCheck,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Get-Help -Full $PSCommandPath
    exit 0
}

. (Join-Path $PSScriptRoot 'dist-pipeline-common.ps1')

function Write-DistHeading { param([string]$Text) Write-Host ''; Write-Host $Text -ForegroundColor White; Write-Host ('-' * $Text.Length) }
function Write-DistStep { param([string]$Text) Write-Host "  $Text" }
function Write-DistGood { param([string]$Text) Write-Host "  OK  $Text" -ForegroundColor Green }
function Write-DistWarn { param([string]$Text) Write-Host "  !!  $Text" -ForegroundColor Yellow }

$started = Get-Date

try {
    Write-Host ''
    Write-Host 'OpenBFME dist publisher' -ForegroundColor White
    Write-Host '======================='

    # ------------------------------------------------------------- preflight
    Write-DistHeading 'Preflight'

    $repoRoot = Get-DistRepoRoot -StartPath (Split-Path -Parent $PSScriptRoot)
    Write-DistStep "checkout            $repoRoot"

    $commonDir = (& git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null | Out-String).Trim()
    if ($commonDir -eq '') { throw (New-DistRefusal -Problem "git could not name the common directory for $repoRoot.") }
    $mainWorktree = [IO.Path]::GetDirectoryName(([IO.Path]::GetFullPath($commonDir)).TrimEnd('\', '/'))
    Write-DistStep "main checkout       $mainWorktree"

    # -- the version, and every copy of it -----------------------------------
    $sourceVersion = Read-DistVersionFile -RepoRoot $repoRoot
    if ($Version -eq '') {
        $Version = $sourceVersion
    } elseif ($Version -cne $sourceVersion -and -not $AllowVersionOverride) {
        throw (New-DistRefusal `
            -Problem "-Version '$Version' disagrees with VERSION ('$sourceVersion'), which is the source of truth." `
            -Remedy 'Edit VERSION, run tools/Write-BuildInfo.ps1, commit - or pass -AllowVersionOverride to republish an existing number deliberately.')
    }
    Write-DistStep "version             $Version (VERSION says $sourceVersion)"

    $versionProblems = @(Test-DistVersionConsistency -RepoRoot $repoRoot)
    if ($versionProblems.Count -gt 0) {
        throw (New-DistRefusal `
            -Problem ("This build would state its version inconsistently:`n           " + ($versionProblems -join "`n           ")) `
            -Remedy 'Bring every copy in line with VERSION, then re-run. tools/Test-DistPipeline.ps1 checks this in a second.')
    }
    Write-DistGood 'every file that states a version agrees with VERSION'

    $notesSource = Get-DistPatchNotesPath -RepoRoot $repoRoot -Version $Version
    Write-DistStep "patch notes         $notesSource"

    # -- the release firewall, before anything is written --------------------
    if ($DistRoot -eq '') { $DistRoot = Join-Path $mainWorktree 'dist' }
    $DistRoot = [IO.Path]::GetFullPath($DistRoot)
    [void](Assert-DistReleaseFirewall -RepoRoot $repoRoot -DistRoot $DistRoot)
    Write-DistGood "dist root is git-ignored and tracks nothing: $DistRoot"

    $bundleName = "v$Version"
    $bundleRoot = Join-Path $DistRoot $bundleName
    if ((Test-Path -LiteralPath $bundleRoot) -and -not $Force) {
        throw (New-DistRefusal -Problem "This version is already published: $bundleRoot" -Remedy 'Bump VERSION for a new milestone, or pass -Force to replace it.')
    }

    # -- the tree this is made from ------------------------------------------
    $dirty = @(& git -C $repoRoot status --porcelain | Where-Object { $_ -ne '' })
    if ($dirty.Count -gt 0) {
        $preview = ($dirty | Select-Object -First 10) -join "`n           "
        if (-not $AllowDirty) {
            throw (New-DistRefusal `
                -Problem ("The working tree has $($dirty.Count) uncommitted path(s), so this build's contents and its change log would describe different trees:`n           $preview") `
                -Remedy 'Commit the work, or pass -AllowDirty to publish notes that are knowingly incomplete (the build says so in its own output).')
        }
        Write-DistWarn "-AllowDirty: publishing with $($dirty.Count) uncommitted path(s); the change log cannot include them."
    }
    $commit = (& git -C $repoRoot rev-parse HEAD | Out-String).Trim()
    $shortCommit = (& git -C $repoRoot rev-parse --short HEAD | Out-String).Trim()
    Write-DistStep "commit              $shortCommit"

    # -- godot ---------------------------------------------------------------
    if ($Godot -eq '') {
        if ((Test-Path Env:OPENBFME_GODOT) -and $env:OPENBFME_GODOT -ne '') {
            $Godot = $env:OPENBFME_GODOT
        } else {
            $resolver = Join-Path $PSScriptRoot 'resolve-godot.ps1'
            if (Test-Path -LiteralPath $resolver -PathType Leaf) {
                $previous = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                try { $resolved = @(& $resolver -RepoRoot $repoRoot -Quiet 2>$null | Where-Object { $_ -ne '' }) }
                finally { $ErrorActionPreference = $previous }
                foreach ($line in $resolved) {
                    $candidate = ([string]$line).Trim().Trim('"')
                    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Godot = $candidate }
                }
            }
        }
        if ($Godot -eq '') {
            throw (New-DistRefusal -Problem 'No Godot 4.7 executable was found.' -Remedy 'Pass -Godot <path>, set OPENBFME_GODOT, or drop the binary in .tools\godot\.')
        }
    }
    Write-DistStep "godot               $Godot"

    if ($Rc) {
        $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $dotnet) {
            throw (New-DistRefusal -Problem '-Rc needs the .NET SDK to build the launcher, and `dotnet` is not on PATH.' -Remedy 'Install the .NET SDK pinned by global.json, or publish without -Rc.')
        }
        Write-DistStep "dotnet              $($dotnet.Source)"
    }

    # ------------------------------------------------------------ the build
    Write-DistHeading "Build dist\$bundleName"
    Write-DistStep 'handing off to tools/Build-PlayableBundle.ps1 (export, stage, hash, boot) ...'

    $buildArguments = @(
        '-RepositoryRoot', $repoRoot
        '-Release'
        '-ReleaseRoot', $DistRoot
        '-Name', $bundleName
        '-Godot', $Godot
    )
    if ($ContentRoot -ne '') { $buildArguments += @('-ContentRoot', $ContentRoot) }
    if ($AllowDirty) { $buildArguments += '-AllowDirtyRelease' }
    if ($AllowMissingWotrData) { $buildArguments += '-AllowMissingWotrData' }
    if ($Force) { $buildArguments += '-Force' }
    if ($Zip) { $buildArguments += '-Zip' }
    if ($SkipLaunchCheck) { $buildArguments += '-SkipLaunchCheck' }

    & (Join-Path $PSScriptRoot 'Build-PlayableBundle.ps1') @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw (New-DistRefusal -Problem "Build-PlayableBundle.ps1 refused (exit $LASTEXITCODE). Nothing was published." -Remedy 'Its own output above names the reason and the remedy.')
    }
    if (-not (Test-Path -LiteralPath $bundleRoot -PathType Container)) {
        throw (New-DistRefusal -Problem "Build-PlayableBundle.ps1 reported success but $bundleRoot does not exist." -Remedy 'Do not ship anything. This is a defect in the build script or the disk.')
    }

    # ------------------------------------------------- read back the verdict
    # From the artefact, not from the fact that the previous command exited 0.
    Write-DistHeading 'Self-sufficiency'
    $buildInfoPath = Join-Path $bundleRoot 'BUILD-INFO.json'
    if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) {
        throw (New-DistRefusal -Problem "The published folder has no BUILD-INFO.json, so it cannot say what it is: $buildInfoPath")
    }
    $bundleInfo = (([IO.File]::ReadAllText($buildInfoPath)).TrimStart([char]0xFEFF) | ConvertFrom-Json)
    $selfLocating = [bool]$bundleInfo.launchCheck.selfLocatesContent
    if ($SkipLaunchCheck) {
        Write-DistWarn '-SkipLaunchCheck: this folder was never booted, so nothing here has proved it runs on another machine.'
    } elseif ($selfLocating) {
        Write-DistGood 'booted with OPENBFME_CONTENT unset and reached the same content census - this folder is self-sufficient'
        Write-DistStep "census              $($bundleInfo.launchCheck.withoutContentEnv.contentDb)"
    } else {
        $detail = 'the env-less boot did not reach the same content database'
        if ($null -ne $bundleInfo.launchCheck.withoutContentEnv) {
            $detail = "env-less boot: exit $($bundleInfo.launchCheck.withoutContentEnv.exitCode), census '$($bundleInfo.launchCheck.withoutContentEnv.contentDb)'"
        }
        if (-not $AllowEnvDependentContent) {
            throw (New-DistRefusal `
                -Problem ("This build does NOT resolve its own content without OPENBFME_CONTENT, so copying dist\$bundleName to another machine and double-clicking it would load the wrong content or none: $detail") `
                -Remedy 'Fix content resolution beside the exe, or pass -AllowEnvDependentContent to publish a folder that must be started with run-with-log.bat and nothing else.')
        }
        Write-DistWarn "-AllowEnvDependentContent: this folder is NOT self-sufficient. $detail"
        Write-DistWarn 'It must be started with run-with-log.bat, which sets OPENBFME_CONTENT for it.'
    }

    # -------------------------------------------------- notes and the stamp
    Write-DistHeading 'Version stamp and patch notes'
    Copy-Item -LiteralPath $notesSource -Destination (Join-Path $bundleRoot 'PATCH-NOTES.md') -Force
    Write-DistGood "PATCH-NOTES.md <- $notesSource"

    $stamp = New-DistVersionStamp -RepoRoot $repoRoot -Version $Version -Destination $bundleRoot `
        -IsReleaseCandidate ([bool]$Rc) -SourceCommit $shortCommit
    Write-DistGood "VERSION.json: v$Version, build $($stamp.build), built from $($stamp.commit)"
    # Not a refusal: build_info.json is generated and then committed, so it can
    # never carry the hash of the commit that carries it. One commit behind is
    # correct. Further behind than that is a stale stamp, and worth saying.
    if ($stamp.buildInfoCommit -ne '' -and -not $shortCommit.StartsWith($stamp.buildInfoCommit)) {
        $behind = (& git -C $repoRoot rev-list --count "$($stamp.buildInfoCommit)..HEAD" 2>$null | Out-String).Trim()
        if ($behind -eq '') { $behind = '?' }
        $line = "the game's own menu will print build $($stamp.build) ($($stamp.buildInfoCommit)), which is $behind commit(s) behind this build ($shortCommit)."
        if ($behind -eq '1') { Write-DistStep "note: $line That is structural - build_info.json is committed after it is generated." }
        else { Write-DistWarn "$line Re-run tools/Write-BuildInfo.ps1 and commit it so the menu matches the folder." }
    }

    # ------------------------------------------------------------ launcher
    $launcherRecord = $null
    if ($Rc) {
        Write-DistHeading 'Release candidate: launcher'
        $launcherRoot = Join-Path $bundleRoot 'launcher'
        if (Test-Path -LiteralPath $launcherRoot) { Remove-Item -LiteralPath $launcherRoot -Recurse -Force }
        $project = Join-Path $repoRoot 'launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj'
        if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
            throw (New-DistRefusal -Problem "The launcher project is missing: $project")
        }
        Write-DistStep 'dotnet publish (win-x64, self-contained, single file) ...'
        $publishLog = Join-Path $bundleRoot 'logs\launcher-publish.log'
        [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($publishLog)) -Force)
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $publishOutput = @(& dotnet publish $project --configuration Release --runtime win-x64 --self-contained true `
            -p:PublishSingleFile=true -p:DebugType=None --output $launcherRoot --nologo 2>&1)
        $publishExit = $LASTEXITCODE
        $ErrorActionPreference = $previous
        [IO.File]::WriteAllText($publishLog, (($publishOutput | ForEach-Object { $_.ToString() }) -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
        if ($publishExit -ne 0) {
            throw (New-DistRefusal -Problem "dotnet publish failed with exit code $publishExit." -Remedy "Read $publishLog")
        }
        $launcherExe = Join-Path $launcherRoot $script:DistLauncherExe
        if (-not (Test-Path -LiteralPath $launcherExe -PathType Leaf)) {
            throw (New-DistRefusal -Problem "dotnet publish produced no $($script:DistLauncherExe)." -Remedy "Read $publishLog")
        }
        Write-DistGood "launcher\$($script:DistLauncherExe) ($([int]((Get-Item -LiteralPath $launcherExe).Length / 1MB)) MB)"

        # The layout the launcher reads. Built here because the launcher has no
        # control that would let a tester point it at a game folder.
        Write-DistStep 'building the install root the launcher reads ...'
        $installRoot = Join-Path $bundleRoot 'launcher-install-root'
        if (Test-Path -LiteralPath $installRoot) { Remove-Item -LiteralPath $installRoot -Recurse -Force }
        [void](New-Item -ItemType Directory -Path $installRoot -Force)
        $launcherRecord = New-DistLauncherInstallRoot -BundleRoot $bundleRoot -InstallRoot $installRoot -Version $Version -Commit $commit
        Write-DistGood "launcher-install-root\versions\$Version\ ($(($launcherRecord.linkModes | Select-Object -Unique) -join ', '))"

        $playBat = @"
@echo off
setlocal
REM Starts the LAUNCHER against the build in this folder. Generated by
REM tools/Publish-DistBuild.ps1 - do not hand-edit; republish.
cd /d "%~dp0"

REM The launcher hands this to the game it starts. Setting it explicitly is the
REM difference between playing this build and quietly playing a months-old pack
REM from your user profile.
set "OPENBFME_CONTENT=%~dp0content-packs"

"%~dp0launcher\$($script:DistLauncherExe)" --install-root "%~dp0launcher-install-root" --no-update %*
"@
        [IO.File]::WriteAllText((Join-Path $bundleRoot 'Play-With-Launcher.bat'), $playBat + "`n", [Text.UTF8Encoding]::new($false))

        # Prove the exe runs and reads an install root, headless, with no
        # network - the sanctioned check, against the exe just published.
        Write-DistStep 'verifying the launcher headless (no network) ...'
        & (Join-Path $PSScriptRoot 'release\Test-LauncherHeadless.ps1') -Launcher $launcherExe
        if ($LASTEXITCODE -ne 0) {
            throw (New-DistRefusal -Problem "Test-LauncherHeadless.ps1 failed (exit $LASTEXITCODE); the published launcher does not start." -Remedy 'Do not hand this folder over. Read the output above.')
        }
        Write-DistGood 'launcher starts headless, reads an install root and exits 0'
    }

    # ---------------------------------------------------- firewall, again
    # After writing, not only before: the answer that matters is about the
    # bytes that now exist, not about an empty directory.
    Write-DistHeading 'Release firewall'
    [void](Assert-DistReleaseFirewall -RepoRoot $repoRoot -DistRoot $DistRoot)
    [void](Assert-DistReleaseFirewall -RepoRoot $repoRoot -DistRoot $bundleRoot)
    Write-DistGood 'the published folder is git-ignored and git tracks nothing in it'

    $elapsed = (Get-Date) - $started
    Write-Host ''
    Write-Host "PUBLISHED  v$Version$(if ($Rc) { '  (release candidate)' } else { '' })" -ForegroundColor Green
    Write-Host "  $bundleRoot"
    Write-Host "  build   $($stamp.build) ($($stamp.commit))"
    Write-Host "  content $($bundleInfo.content.packs.Count) pack(s), active $($bundleInfo.content.selection.activePack)"
    Write-Host "  took    $([int]$elapsed.TotalMinutes)m $($elapsed.Seconds)s"
    Write-Host ''
    Write-Host "  Play it:      $bundleRoot\run-with-log.bat"
    if ($Rc) { Write-Host "  Or launcher:  $bundleRoot\Play-With-Launcher.bat" }
    Write-Host "  What changed: $bundleRoot\PATCH-NOTES.md"
    Write-Host "  What it is:   $bundleRoot\BUILD-INFO.txt"
    Write-Host ''
    Write-Host '  NOT FOR REDISTRIBUTION - content-packs\ is converted from a retail install.' -ForegroundColor Yellow
    Write-Host ''
    exit 0
} catch {
    $message = $_.Exception.Message
    if ($message -cnotlike 'REFUSED:*') { $message = "REFUSED: $message" }
    Write-Host ''
    Write-Host '================================================================' -ForegroundColor Red
    Write-Host $message -ForegroundColor Red
    Write-Host '================================================================' -ForegroundColor Red
    if ($message -cnotlike '*Fix:*') {
        Write-Host ''
        Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    }
    Write-Host ''
    exit 1
}
