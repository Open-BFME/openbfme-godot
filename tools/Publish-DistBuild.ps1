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

.PARAMETER AllowPackOwnedWotrData
    Passed through: where the content pack already ships a War of the Ring
    DOCUMENT, use the pack's copy instead of refusing the collision. Nothing is
    replaced and the pack's copy is still verified against its loader's schema.
    Required today, because the active pack builds living-world.json into itself
    and without this no release can be built from the current pack set at all.

.PARAMETER PreflightOnly
    Run every check, prove the hand-off to Build-PlayableBundle binds, and stop
    before building. Refused together with -FinishOnly.

.PARAMETER FinishOnly
    The bundle at dist\v<version>\ is already built. Verify it against its own
    BUILD-INFO.json and redo only the finishing work: notes, stamp, launcher,
    firewall. The stamps then name the commit the BUNDLE records - NOT HEAD,
    which is the whole point: the bytes are older than HEAD by definition.

.PARAMETER AllowFinishFromOtherCommit
    Finish a bundle whose commit is not HEAD. Refused by default because the
    patch notes come from this checkout and would describe a later tree. The
    stamps name the bundle's commit either way.

.EXAMPLE
    # What actually works on this machine today. Every switch is a defect
    # elsewhere; AGENTS.md lists what retires each one.
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Publish-DistBuild.ps1 `
        -Godot <godot.exe> -AllowEnvDependentContent -AllowPackOwnedWotrData

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Publish-DistBuild.ps1 -Rc `
        -Godot <godot.exe> -AllowEnvDependentContent -AllowPackOwnedWotrData
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
    # Passed through: where the content pack already ships a War of the Ring
    # artefact, use the pack's copy instead of refusing the collision. Nothing is
    # replaced and the pack's copy is still schema-verified.
    [switch]$AllowPackOwnedWotrData,
    [switch]$Force,
    [switch]$Zip,
    [switch]$SkipLaunchCheck,
    # Run every check, prove the hand-off to Build-PlayableBundle binds, and
    # stop before building anything. What tools/Test-DistPipeline.ps1 drives, and
    # what to run yourself before committing half an hour to an export.
    [switch]$PreflightOnly,
    # The bundle at dist\v<version>\ is already built. Verify it against its own
    # BUILD-INFO.json and do only the finishing work: notes, stamp, launcher,
    # firewall. For when a finishing step failed - the export and several
    # gigabytes of hashing are not the thing that went wrong, and re-paying an
    # hour and a half for a file copy is how a pipeline stops getting used.
    [switch]$FinishOnly,
    # Finish a bundle that was built from a commit other than HEAD. The stamps
    # name the BUNDLE's commit either way; what this accepts is that the patch
    # notes and anything else read from this checkout describe a later tree.
    [switch]$AllowFinishFromOtherCommit,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Get-Help -Full $PSCommandPath
    exit 0
}

. (Join-Path $PSScriptRoot 'dist-pipeline-common.ps1')

# -PreflightOnly's exit lives on the building path, so combining it with
# -FinishOnly used to IGNORE it silently and do the finishing work for real -
# a "change nothing" flag writing files. Refused rather than reinterpreted:
# guessing which one the operator meant is how a dry run becomes a publish.
if ($PreflightOnly -and $FinishOnly) {
    Write-Host ''
    Write-Host 'REFUSED: -PreflightOnly and -FinishOnly contradict each other. -PreflightOnly changes nothing; -FinishOnly writes the notes, the stamp and the launcher.' -ForegroundColor Red
    Write-Host '           Fix: pass one. -PreflightOnly to check without building, -FinishOnly to complete a bundle that is already built.' -ForegroundColor Red
    Write-Host ''
    exit 1
}
if ($AllowFinishFromOtherCommit -and -not $FinishOnly) {
    Write-Host ''
    Write-Host 'REFUSED: -AllowFinishFromOtherCommit has no meaning without -FinishOnly.' -ForegroundColor Red
    Write-Host '           Fix: drop it, or add -FinishOnly.' -ForegroundColor Red
    Write-Host ''
    exit 1
}

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
            -Remedy 'Bring every copy in line with VERSION, then re-run. tools/Test-DistPipeline.ps1 checks this in about fifteen seconds.')
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
    if ($FinishOnly -and -not (Test-Path -LiteralPath $bundleRoot -PathType Container)) {
        throw (New-DistRefusal -Problem "-FinishOnly needs a bundle that is already built, and there is nothing at $bundleRoot" -Remedy 'Run without -FinishOnly to build it.')
    }
    if ((Test-Path -LiteralPath $bundleRoot) -and -not $Force -and -not $FinishOnly) {
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
    # HEAD, for now. On -FinishOnly this is REPLACED below by the commit the
    # bundle records, because the bytes are older than HEAD and the stamp must
    # name the bytes. Nothing may use these two before that point.
    $headCommit = (& git -C $repoRoot rev-parse HEAD | Out-String).Trim()
    $headShortCommit = (& git -C $repoRoot rev-parse --short HEAD | Out-String).Trim()
    $commit = $headCommit
    $shortCommit = $headShortCommit
    Write-DistStep "HEAD                $shortCommit"

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
    if ($FinishOnly) {
        # Verify the bundle against its OWN manifest before finishing it. The
        # claim being made is "this folder is a real, complete build", and it is
        # made by the existing verifier rather than by the folder existing.
        Write-DistHeading "Verify the existing dist\$bundleName"
        Write-DistStep 'tools/Test-PlayableBundle.ps1 -Quick (layout and sizes against BUILD-INFO.json) ...'
        & (Join-Path $PSScriptRoot 'Test-PlayableBundle.ps1') -Bundle $bundleRoot -Quick
        if ($LASTEXITCODE -ne 0) {
            throw (New-DistRefusal -Problem "The existing bundle does not verify (exit $LASTEXITCODE), so there is nothing safe to finish." -Remedy 'Rebuild it: run without -FinishOnly and with -Force.')
        }
        Write-DistGood 'the existing bundle verifies against its own BUILD-INFO.json'
    } else {

    Write-DistHeading "Build dist\$bundleName"
    Write-DistStep 'handing off to tools/Build-PlayableBundle.ps1 (export, stage, hash, boot) ...'

    # A HASH TABLE, not an array. Array splatting binds POSITIONALLY - the
    # `-Name` strings are passed as values, not read as parameter names - so
    # `@('-RepositoryRoot', $repoRoot, '-Release', ...)` quietly filled seven
    # positional parameters in declaration order and refused with
    # "Cannot convert value 'v0.2.1' to type System.Int32" on the seventh.
    # Named binding is the only form that means what it reads like.
    $buildArguments = @{
        RepositoryRoot = $repoRoot
        Release        = $true
        ReleaseRoot    = $DistRoot
        Name           = $bundleName
        Godot          = $Godot
    }
    if ($ContentRoot -ne '') { $buildArguments['ContentRoot'] = $ContentRoot }
    if ($AllowDirty) { $buildArguments['AllowDirtyRelease'] = $true }
    if ($AllowMissingWotrData) { $buildArguments['AllowMissingWotrData'] = $true }
    if ($AllowPackOwnedWotrData) { $buildArguments['AllowPackOwnedWotrData'] = $true }
    if ($Force) { $buildArguments['Force'] = $true }
    if ($Zip) { $buildArguments['Zip'] = $true }
    if ($SkipLaunchCheck) { $buildArguments['SkipLaunchCheck'] = $true }

    # Prove the hand-off binds BEFORE spending the export on it. The binder is
    # asked, using Build-PlayableBundle's own declared parameters, so a renamed
    # or mistyped parameter refuses in milliseconds instead of after a build.
    $buildScript = Join-Path $PSScriptRoot 'Build-PlayableBundle.ps1'
    $buildCommand = Get-Command -Name $buildScript -CommandType ExternalScript
    foreach ($key in @($buildArguments.Keys)) {
        if (-not $buildCommand.Parameters.ContainsKey($key)) {
            throw (New-DistRefusal `
                -Problem "This wrapper would pass -$key, which Build-PlayableBundle.ps1 does not accept." `
                -Remedy "Its parameters are: $(($buildCommand.Parameters.Keys | Sort-Object) -join ', ')")
        }
        $wanted = $buildCommand.Parameters[$key].ParameterType
        try { [void][System.Management.Automation.LanguagePrimitives]::ConvertTo($buildArguments[$key], $wanted) }
        catch {
            throw (New-DistRefusal -Problem "-$key would be passed '$($buildArguments[$key])', which is not a $($wanted.Name).")
        }
    }
    if ($PreflightOnly) {
        Write-DistGood "$($buildArguments.Count) argument(s) bind against Build-PlayableBundle.ps1"
        Write-Host ''
        Write-Host "PREFLIGHT ONLY - nothing was built. v$Version would be published to $bundleRoot" -ForegroundColor Green
        Write-Host ''
        exit 0
    }

    & $buildScript @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw (New-DistRefusal -Problem "Build-PlayableBundle.ps1 refused (exit $LASTEXITCODE). Nothing was published." -Remedy 'Its own output above names the reason and the remedy.')
    }
    if (-not (Test-Path -LiteralPath $bundleRoot -PathType Container)) {
        throw (New-DistRefusal -Problem "Build-PlayableBundle.ps1 reported success but $bundleRoot does not exist." -Remedy 'Do not ship anything. This is a defect in the build script or the disk.')
    }
    }

    # ------------------------------------------------- read back the verdict
    # From the artefact, not from the fact that the previous command exited 0.
    $buildInfoPath = Join-Path $bundleRoot 'BUILD-INFO.json'
    if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) {
        throw (New-DistRefusal -Problem "The published folder has no BUILD-INFO.json, so it cannot say what it is: $buildInfoPath")
    }
    $bundleInfo = (([IO.File]::ReadAllText($buildInfoPath)).TrimStart([char]0xFEFF) | ConvertFrom-Json)

    # ------------------------------------------------- which commit shipped
    # EVERY stamp below names this, so it has to be the commit whose bytes are
    # in the folder. On -FinishOnly that is NOT HEAD: the bundle was built
    # earlier, HEAD has moved, and stamping HEAD put a commit into VERSION.json,
    # current.json and .openbfme-version.json whose bytes were never in the
    # build - while BUILD-INFO.json, written by the build itself, said otherwise.
    $sourceCommit = Resolve-DistSourceCommit -RepoRoot $repoRoot -FromBundle ([bool]$FinishOnly) -BuildInfo $bundleInfo
    $commit = $sourceCommit.commit
    $shortCommit = $sourceCommit.shortCommit
    if ($FinishOnly) {
        Write-DistStep "bundle commit       $shortCommit (from BUILD-INFO.json, not HEAD)"
        if ($commit -cne $headCommit) {
            $distance = Get-DistCommitDistance -RepoRoot $repoRoot -From $commit -To $headCommit
            if (-not $AllowFinishFromOtherCommit) {
                throw (New-DistRefusal `
                    -Problem ("The bundle was built from $shortCommit but HEAD is $headShortCommit ($distance commit(s) later), so the patch notes and any other file taken from this checkout would describe a tree the bundle does not contain.") `
                    -Remedy 'Pass -AllowFinishFromOtherCommit to finish it anyway (the stamps name the BUNDLE commit either way), or rebuild with -Force so the bytes and the checkout agree.')
            }
            Write-DistWarn "-AllowFinishFromOtherCommit: bundle is $shortCommit, HEAD is $headShortCommit ($distance commit(s) later). The stamps name the bundle; the notes come from this checkout."
        }
    }

    Write-DistHeading 'Self-sufficiency'
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
    # Measured against the BUNDLE's commit, not HEAD. Against HEAD it reported
    # "5 commits behind" for a bundle that was one behind, which is the same
    # class of mistake as stamping HEAD in the first place.
    if ($stamp.buildInfoCommit -ne '' -and -not $shortCommit.StartsWith($stamp.buildInfoCommit)) {
        $behind = Get-DistCommitDistance -RepoRoot $repoRoot -From $stamp.buildInfoCommit -To $commit
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
