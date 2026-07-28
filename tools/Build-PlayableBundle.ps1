<#
.SYNOPSIS
    Build a complete, runnable OpenBFME bundle from the current checkout.

.DESCRIPTION
    Produces the thing that used to be assembled by hand: a Godot Windows export
    with the selected content packs staged beside the exe, exactly the layout the
    OpenBFME-alpha01 README describes.

        dist/bundle/<name>/
          OpenBFME.exe
          OpenBFME.pck
          content-packs/            <- must stay beside the exe
            selection.json
            <pack-id>/<bundle-hash>/...
          run-with-log.bat
          README.txt
          BUILD-INFO.json           <- what this bundle is
          BUILD-INFO.txt
          logs/

    The bundle states its own identity: commit, branch, whether the working tree
    was dirty, build time, Godot version, and the id + content hash of every pack
    staged. That is the whole point. alpha01 could not say which branch it came
    from, so when it had features a dev checkout lacked, the only available
    conclusion was "the project lost progress".

    It fails closed. A missing pack, an unreadable selection, a failed export, a
    staged tree whose hashes do not match the source, or a build that will not
    boot headless all stop the build with a REFUSED message. Nothing is promoted
    to its final name until every check has passed, so a half bundle never
    appears under a name that looks finished.

.PARAMETER Name
    Bundle directory name. Default: OpenBFME-<branch>-<shortcommit>[-dirty].

.PARAMETER RepositoryRoot
    Checkout to build. Default: the checkout this script lives in. Useful when
    the branch you want to hand a playtester is in another worktree - the bundle
    records that branch and commit, not this one.

.PARAMETER OutputRoot
    Where the bundle directory is created. Default: <repo>/dist/bundle.
    Must be git-ignored; this is checked with `git check-ignore`, not assumed.

.PARAMETER Release
    Build a RELEASE: it lands in the MAIN checkout's dist/bundle rather than in
    whichever worktree built it, it is named for the date and commit rather than
    the branch, and it carries PATCH-NOTES.txt written from real history. Use
    this for anything handed to a playtester; leave it off for a dev build,
    which keeps working exactly as before.

    A worktree is temporary. A release built into one disappears when the
    worktree is removed, which is why this exists at all.

.PARAMETER ReleaseRoot
    Override where releases land. Default: <main checkout>/dist/bundle.

.PARAMETER AllowDirtyRelease
    Permit a release from a working copy with uncommitted changes. Off by
    default: the patch notes are made from committed history, so uncommitted
    work is in the build and cannot be in the notes. The notes say so loudly
    when this is used.

.PARAMETER Since
    Write the patch notes against this commit instead of the previous release.

.PARAMETER Highlights
    The player-facing note file. Default: tools/release-highlights.txt. Every
    line must cite a commit, and a line whose commit is not in this release's
    range is dropped rather than printed.

.PARAMETER LivingWorldDocument
.PARAMETER LivingMapBundle
    War of the Ring's data, staged INTO the bundle so the menu entry works
    without any environment variable. Defaults point at the private retail
    workspace in the main checkout.

.PARAMETER AllowMissingWotrData
    Build a release even though one or more of War of the Ring's converted
    bundles is absent. The bundle then says so in its own output, in BUILD-INFO
    and in the patch notes, naming each missing bundle and what the player loses
    without it; it does not ship a dead button or a degraded map quietly.

    WHICH bundles a build must carry is not written down anywhere. It is derived
    in tools/wotr-data-staging.ps1 from the loaders in game/src/wotr - see the
    comment at the top of that file. A new loader with no staging rule stops the
    build; that is deliberate, and it is the fix for having shipped this class of
    omission three times.

.PARAMETER AllowMismatchedWotrDocument
    Ship a living-world document other than the one the converted region
    geometry and region portraits record being built against. Off by default:
    those meshes are keyed to the region ids in a specific document, and pairing
    them with a different one degrades the map silently.

.PARAMETER ContentRoot
    Pack cache to stage from. Default search order:
      1. -ContentRoot
      2. $env:OPENBFME_CONTENT
      3. <this worktree>/.private/content-packs
      4. <main worktree>/.private/content-packs
    Nothing under .private/ is ever copied anywhere git can see.

.PARAMETER Godot
    Godot editor binary. Default: $env:OPENBFME_GODOT, else the known local
    install. Export templates for the matching version must be installed; if
    they are not, this refuses in preflight with the exact remedy rather than
    failing halfway through a build.

.PARAMETER Preset
    export_presets.cfg preset name. Default: windows.

.PARAMETER Force
    Replace an existing bundle directory of the same name.

.PARAMETER SkipLaunchCheck
    Do not boot the exported build. The bundle then records that no launch
    verification was performed, rather than implying one.

.PARAMETER FastFinalVerify
    Final verification compares file layout and sizes but does not re-hash every
    staged file a third time. Faster; strictly weaker. The staged-vs-source hash
    comparison still runs in full either way.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PlayableBundle.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PlayableBundle.ps1 -Name OpenBFME-alpha02 -Force

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PlayableBundle.ps1 -Release
#>
[CmdletBinding()]
param(
    [string]$Name = '',
    [string]$RepositoryRoot = '',
    [string]$OutputRoot = '',
    [string]$ContentRoot = '',
    [string]$Godot = '',
    [string]$Preset = 'windows',
    [int]$LaunchTimeoutSeconds = 300,
    [switch]$Release,
    [string]$ReleaseRoot = '',
    [switch]$AllowDirtyRelease,
    [string]$Since = '',
    [string]$Highlights = '',
    [string]$BaseBranch = 'main',
    [string]$LivingWorldDocument = '',
    [string]$LivingMapBundle = '',
    [switch]$AllowMissingWotrData,
    [switch]$AllowMismatchedWotrDocument,
    [switch]$Force,
    [switch]$SkipLaunchCheck,
    [switch]$FastFinalVerify,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Get-Help -Full $PSCommandPath
    exit 0
}

. (Join-Path $PSScriptRoot 'playable-bundle-common.ps1')

$script:StartedAt = Get-Date
$script:PartialRoot = ''

function Invoke-Robocopy {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [string[]]$ExcludeDirectories = @(),
        # /E instead of /MIR: adds files without deleting what is already there.
        # Used only where two converted bundles deliberately share one staged
        # directory because that is where the loader searches for both.
        [switch]$Merge
    )
    $mode = $(if ($Merge) { '/E' } else { '/MIR' })
    $arguments = @($Source, $Destination, $mode, '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:2', '/W:2', '/MT:16')
    foreach ($excluded in $ExcludeDirectories) { $arguments += @('/XD', (Join-Path $Source $excluded)) }
    & robocopy.exe @arguments | Out-Null
    $code = $LASTEXITCODE
    # robocopy: 0-7 are success//informational, 8+ are real failures.
    if ($code -ge 8) {
        throw (New-BundleRefusal -Problem "Copy failed (robocopy exit $code): $Source -> $Destination" -Remedy 'Check for locked files, a full disk, or a concurrent build using the same staging directory.')
    }
}

try {
    Write-Host ''
    Write-Host 'OpenBFME playable bundle builder' -ForegroundColor White
    Write-Host '--------------------------------'

    # ---------------------------------------------------------------- preflight
    # Everything that can be known before doing expensive work is checked here,
    # so a missing pack or missing export template costs seconds, not a failed
    # ten-minute build.
    Write-BundleHeading 'Preflight'

    $repoStart = $PSScriptRoot
    if ($RepositoryRoot -ne '') { $repoStart = [IO.Path]::GetFullPath($RepositoryRoot) }
    $repoRoot = Get-BundleRepoRoot -StartPath $repoStart
    Write-BundleStep "checkout            $repoRoot"

    $source = Get-BundleSourceIdentity -RepoRoot $repoRoot
    $dirtyLabel = 'clean'
    if ($source.dirty) { $dirtyLabel = "DIRTY ($($source.dirtyFileCount) uncommitted files)" }
    Write-BundleStep "commit              $($source.shortCommit) on $($source.branch) - $dirtyLabel"
    Write-BundleStep "commit subject      $($source.commitSubject)"
    if ($source.dirty) {
        Write-BundleWarn 'The working tree has uncommitted changes. They WILL be in this build; BUILD-INFO.json records that, and lists the files.'
    }
    # A release states what changed since the last one, and it derives that from
    # committed history. Uncommitted work is therefore IN the build and CANNOT be
    # in the notes - a release whose notes are structurally incomplete is the
    # overstating-by-omission version of the problem the notes exist to solve.
    if ($Release -and $source.dirty -and -not $AllowDirtyRelease) {
        throw (New-BundleRefusal -Problem "This would be a RELEASE built from a working copy with $($source.dirtyFileCount) uncommitted files. Its patch notes are made from committed history, so that work would be in the build and absent from the notes." -Remedy 'Commit the work first, or pass -AllowDirtyRelease to accept notes that are knowingly incomplete (the bundle and its notes then say so).')
    }

    if ($Godot -eq '') {
        if (Test-Path Env:OPENBFME_GODOT) { $Godot = $env:OPENBFME_GODOT }
        else { $Godot = 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' }
    }
    $godotIdentity = Get-BundleGodotIdentity -GodotExe $Godot -Preset $Preset
    Write-BundleStep "godot               $($godotIdentity.version)"
    Write-BundleStep "export templates    $($godotIdentity.exportTemplatesRoot)"

    $presetsFile = Join-Path $repoRoot 'game\export_presets.cfg'
    if (-not (Test-Path -LiteralPath $presetsFile -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "game/export_presets.cfg is missing, so there is nothing to export." )
    }
    $presetNames = @([IO.File]::ReadAllLines($presetsFile) |
        Where-Object { $_ -cmatch '^name="(?<n>[^"]+)"$' } |
        ForEach-Object { ([regex]::Match($_, '^name="(?<n>[^"]+)"$')).Groups['n'].Value })
    if ($presetNames -cnotcontains $Preset) {
        throw (New-BundleRefusal -Problem "game/export_presets.cfg has no preset named '$Preset'." -Remedy "Available presets: $($presetNames -join ', ')")
    }

    # Content root resolution, announced out loud. Loading the wrong pack set
    # silently is a recorded failure mode in this project.
    if ($ContentRoot -eq '') {
        $candidates = New-Object 'System.Collections.Generic.List[string]'
        if ((Test-Path Env:OPENBFME_CONTENT) -and $env:OPENBFME_CONTENT -ne '') { $candidates.Add($env:OPENBFME_CONTENT) }
        $candidates.Add((Join-Path $repoRoot '.private\content-packs'))
        Push-Location $repoRoot
        try {
            $commonDir = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Out-String).Trim()
        } finally { Pop-Location }
        if ($commonDir -ne '') {
            $mainWorktree = [IO.Path]::GetDirectoryName(([IO.Path]::GetFullPath($commonDir)).TrimEnd('\', '/'))
            if ($mainWorktree -ne '') { $candidates.Add((Join-Path $mainWorktree '.private\content-packs')) }
        }
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'selection.json') -PathType Leaf) {
                $ContentRoot = [IO.Path]::GetFullPath($candidate)
                break
            }
        }
        if ($ContentRoot -eq '') {
            throw (New-BundleRefusal -Problem ("No content pack cache with a selection.json was found. Looked in:`n           " + (($candidates | ForEach-Object { $_ }) -join "`n           ")) -Remedy 'Pass -ContentRoot <path to content-packs>.')
        }
    } else {
        $ContentRoot = [IO.Path]::GetFullPath($ContentRoot)
    }
    Write-BundleStep "content source      $ContentRoot"

    $selection = Read-BundlePackSelection -ContentRoot $ContentRoot
    Write-BundleStep "active pack         $($selection.activePack)"
    foreach ($supplement in $selection.supplementalPacks) {
        Write-BundleStep "supplemental pack   $supplement"
    }

    $packs = New-Object 'System.Collections.Generic.List[object]'
    foreach ($relative in $selection.allPacks) {
        $packs.Add((Read-BundlePackMetadata -Root $ContentRoot -Relative $relative))
    }
    $redistributable = $true
    foreach ($pack in $packs) { if (-not $pack.redistributable) { $redistributable = $false } }
    Write-BundleGood "$($packs.Count) content pack(s) named by selection.json are present and correctly labelled"
    if (-not $redistributable) {
        Write-BundleWarn 'These packs are retail-derived and marked NOT redistributable. The bundle stays inside dist/ (git-ignored) and must not be published.'
    }

    # ------------------------------------------------------- where this lands
    $mainWorktree = Get-BundleMainWorktree -RepoRoot $repoRoot
    if ($Release) {
        if ($OutputRoot -ne '' -and $ReleaseRoot -ne '') {
            throw (New-BundleRefusal -Problem 'Both -OutputRoot and -ReleaseRoot were given, and they mean the same thing for a release.' -Remedy 'Pass one.')
        }
        if ($ReleaseRoot -eq '' -and $OutputRoot -eq '') {
            $ReleaseRoot = Join-Path $mainWorktree 'dist\bundle'
        } elseif ($ReleaseRoot -eq '') {
            $ReleaseRoot = $OutputRoot
        }
        $OutputRoot = $ReleaseRoot
        Write-BundleStep 'RELEASE build'
    } elseif ($ReleaseRoot -ne '') {
        throw (New-BundleRefusal -Problem '-ReleaseRoot was given without -Release.' -Remedy 'Add -Release, or use -OutputRoot for a development build.')
    }
    if ($OutputRoot -eq '') { $OutputRoot = Join-Path $repoRoot 'dist\bundle' }
    $OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

    # Close the git hazard BEFORE asking git about it. `/dist/` lives in a
    # TRACKED .gitignore, so whether it applies depends on which branch the
    # destination checkout is standing on - and this destination is deliberately
    # in another checkout, whose branch this build does not control. Writing a
    # self-ignoring .gitignore into the release directory makes the guarantee
    # branch-independent. The refusal below still runs and is still what decides.
    # Only for -Release, and only for the directory this tool owns. A dev build
    # with a hand-picked -OutputRoot is not this tool's directory to blanket-
    # ignore, and the refusal below is the right answer for it.
    $ignoreOwner = ''
    if ($Release) { $ignoreOwner = [IO.Path]::GetDirectoryName($OutputRoot.TrimEnd('\', '/')) }
    if ($ignoreOwner -ne '' -and $null -ne $ignoreOwner) {
        if (Set-BundleReleaseDirectoryIgnored -ReleaseDirectory $ignoreOwner) {
            Write-BundleStep "wrote the self-ignoring guard $ignoreOwner\.gitignore (keeps this directory out of git on every branch)"
        }
    }
    if (-not (Test-BundlePathIsGitIgnored -RepoRoot $repoRoot -Path $OutputRoot)) {
        throw (New-BundleRefusal -Problem "The output directory is NOT git-ignored: $OutputRoot" -Remedy 'The bundle carries retail-derived content. Build into a git-ignored path (dist/ is ignored by default) and never into a tracked one.')
    }
    Write-BundleGood "output root is git-ignored: $OutputRoot"

    if ($Name -eq '') {
        if ($Release) {
            # A release is named for WHEN and WHAT, not for the branch it came
            # from: the branch is an implementation detail that stops existing,
            # and a playtester sorting a folder wants the date first.
            $Name = "OpenBFME-release-$((Get-Date).ToString('yyyyMMdd'))-$($source.shortCommit)"
            if ($source.dirty) { $Name += '-dirty' }
        } else {
            $branchPart = ($source.branch -replace '[^A-Za-z0-9._-]', '-')
            if ($branchPart -eq '' -or $branchPart -eq 'HEAD') { $branchPart = 'detached' }
            $Name = "OpenBFME-$branchPart-$($source.shortCommit)"
            if ($source.dirty) { $Name += '-dirty' }
        }
    }
    if ($Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
        throw (New-BundleRefusal -Problem "Unsafe bundle name: '$Name'." -Remedy 'Use letters, digits, dot, dash and underscore only.')
    }
    $bundleRoot = Join-Path $OutputRoot $Name
    if (Test-Path -LiteralPath $bundleRoot) {
        if (-not $Force) {
            throw (New-BundleRefusal -Problem "A bundle already exists: $bundleRoot" -Remedy 'Pass -Force to replace it, or -Name <other> to build alongside it.')
        }
    }
    Write-BundleStep "bundle name         $Name"

    # ------------------------------------------- War of the Ring data sources
    # The bundle used to ship a WAR OF THE RING button and none of its data, so
    # it read (UNAVAILABLE) in every build and the owner hit it. Then it shipped
    # the button and the document with no 3D map. Then it shipped both with none
    # of the four bundles that had landed since. Each fix added a name to a list
    # that was correct until the next converter ran.
    #
    # So the list is DERIVED now: tools/wotr-data-staging.ps1 reads every loader
    # in game/src/wotr, learns from each one the environment variable it reads,
    # the schema it accepts and the pack-relative path it searches, and then
    # finds the artefacts in the private workspace BY SCHEMA. A loader with no
    # staging rule refuses this build; so does a rule with no loader. Resolved
    # and validated HERE, in preflight, so a missing bundle costs seconds rather
    # than a finished build with a degraded map in it.
    $wotrWorkspaceRoots = @(
        (Join-Path $repoRoot ($script:WotrWorkspaceRelative -replace '/', '\')),
        (Join-Path $mainWorktree ($script:WotrWorkspaceRelative -replace '/', '\'))
    )
    $wotrPlan = New-WotrStagingPlan -RepoRoot $repoRoot -WorkspaceRoots $wotrWorkspaceRoots `
        -DocumentOverride $LivingWorldDocument -MapOverride $LivingMapBundle `
        -AllowMismatchedDocument:$AllowMismatchedWotrDocument
    Write-BundleStep "wotr loaders        $($wotrPlan.rules.Count) declared by $($script:WotrLoaderDirectory), $($wotrPlan.scanned) workspace documents scanned by schema"

    # Validate every present artefact against the schema ITS OWN LOADER declares,
    # before the build spends six minutes exporting.
    $wotrDocumentBytes = [long]0
    foreach ($rule in $wotrPlan.rules) {
        if (-not $rule.present) { continue }
        $bytes = Test-BundleWotrArtifact -Path $rule.source -Schema $rule.schema `
            -Label "the $($rule.schema) artefact" -Strict:($rule.kind -ceq 'document')
        if ($rule.kind -ceq 'document') {
            $wotrDocumentBytes = $bytes
            # The refusal that already existed for the document, unchanged.
            [void](Test-BundleWotrDocument -Path $rule.source)
        }
        Write-BundleStep ("  {0,-36} {1}" -f $rule.env, $rule.source)
        Write-BundleStep ("  {0,-36} -> content-packs/<active pack>/{1}" -f '', $rule.landedRelative)
    }

    # BUILD-INFO schema 1 records these two by name, so they keep their meaning:
    # the living-world document and the 3D map are what decide whether the menu
    # entry opens at all. Everything else degrades the screen without closing it.
    $LivingWorldDocument = ''
    $LivingMapBundle = ''
    foreach ($rule in $wotrPlan.rules) {
        if (-not $rule.present) { continue }
        if ($rule.env -ceq 'OPENBFME_LIVING_WORLD_DOC') { $LivingWorldDocument = $rule.source }
        if ($rule.env -ceq 'OPENBFME_LIVING_MAP') { $LivingMapBundle = $rule.sourceDirectory }
    }

    $wotrMissing = New-Object 'System.Collections.Generic.List[string]'
    foreach ($rule in $wotrPlan.missing) {
        # A missing bundle must never read like a bundle that was never needed.
        # Name it, name the setting that would have found it, and name what the
        # player gets instead.
        $wotrMissing.Add("$($rule.env) ($($rule.schema)) - the player loses $($rule.loses)")
    }

    # "The menu entry opens" and "the screen is complete" are different claims,
    # and collapsing them is how four bundles went missing without a word.
    $wotrStaged = ($LivingWorldDocument -ne '' -and $LivingMapBundle -ne '')
    $wotrDegraded = ($wotrMissing.Count -gt 0)
    $wotrReason = ''
    if ($wotrDegraded) {
        $wotrReason = "not present at build time: $($wotrMissing -join '; ')"
        if (-not $wotrStaged) {
            Write-BundleWarn 'WAR OF THE RING WILL BE UNAVAILABLE in this bundle - its document or its 3D map is absent.'
            Write-BundleWarn 'The menu entry will read WAR OF THE RING (UNAVAILABLE). BUILD-INFO and the release notes say so; this build is not pretending otherwise.'
        } else {
            Write-BundleWarn "WAR OF THE RING WILL BE DEGRADED in this bundle - $($wotrMissing.Count) converted bundle(s) are absent from the workspace:"
        }
        foreach ($line in $wotrMissing) { Write-BundleWarn "  $line" }
        # A dev build without the private workspace is a normal, honest state. A
        # RELEASE handed to a playtester with a feature whose art sits on the
        # author's disk is the exact defect this staging was added to fix - three
        # times now - so that one is refused, for ANY absent bundle and not only
        # for the two the menu entry needs.
        if ($Release -and -not $AllowMissingWotrData) {
            throw (New-BundleRefusal -Problem ("A release would ship War of the Ring with $($wotrMissing.Count) of its converted bundle(s) missing:`n           " + ($wotrMissing -join "`n           ")) -Remedy 'Produce the artefacts into the private workspace (they are found by schema, so any path under .private/retail-work works), or pass -AllowMissingWotrData to ship a release that states in its own output, in BUILD-INFO and in the notes exactly what the player is losing.')
        }
    } else {
        Write-BundleGood "every War of the Ring bundle the loaders declare is present ($($wotrPlan.rules.Count) of $($wotrPlan.rules.Count))"
        Write-BundleStep "living-world doc    $([IO.Path]::GetFileName($LivingWorldDocument)) - $($wotrPlan.document.basis)"
    }

    if ($Highlights -eq '') { $Highlights = Join-Path $PSScriptRoot 'release-highlights.txt' }
    elseif (-not (Test-Path -LiteralPath $Highlights -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "-Highlights does not exist: $Highlights")
    }
    # Parse now, not after the export: a typo in a note should not cost a build.
    [void](Read-BundleReleaseHighlights -Path $Highlights)

    # Build into <name>.partial and rename only at the very end. If anything
    # below refuses, what is left on disk is named .partial and can never be
    # mistaken for a finished build.
    $script:PartialRoot = "$bundleRoot.partial"

    # Content is measured in gigabytes, so a retry after a failed build should
    # not re-copy all of it. Carry the previous attempt's content-packs across
    # the wipe; every pack is then re-mirrored, re-hashed and pruned below, so
    # reuse can only save time, never smuggle stale files into the result.
    # Only a .partial is ever cannibalised - never a finished bundle, which must
    # stay intact and playable if this build fails.
    $carriedContent = "$bundleRoot.carried-content"
    if (Test-Path -LiteralPath $carriedContent) { Remove-Item -LiteralPath $carriedContent -Recurse -Force }
    $previousContent = Join-Path $script:PartialRoot $script:BundleContentDir
    if (Test-Path -LiteralPath $previousContent -PathType Container) {
        Move-Item -LiteralPath $previousContent -Destination $carriedContent
        Write-BundleStep 'reusing content staged by a previous failed attempt (it is re-mirrored and re-hashed below)'
    }
    if (Test-Path -LiteralPath $script:PartialRoot) { Remove-Item -LiteralPath $script:PartialRoot -Recurse -Force }
    [void](New-Item -ItemType Directory -Path $script:PartialRoot -Force)
    if (Test-Path -LiteralPath $carriedContent -PathType Container) {
        Move-Item -LiteralPath $carriedContent -Destination (Join-Path $script:PartialRoot $script:BundleContentDir)
    }
    $logRoot = Join-Path $script:PartialRoot 'logs'
    [void](New-Item -ItemType Directory -Path $logRoot -Force)

    # ------------------------------------------------------------------ export
    Write-BundleHeading 'Godot export'

    # Export from a private copy of game/ rather than the live checkout. Other
    # lanes work in this repository continuously; running --import against the
    # shared game/.godot would fight whatever else is running, and a torn import
    # cache is a confusing way to fail.
    $stageRoot = Join-Path $OutputRoot ".stage\$Name"
    $stageProject = Join-Path $stageRoot 'game'
    Write-BundleStep "staging project     $stageProject"
    Invoke-Robocopy -Source (Join-Path $repoRoot 'game') -Destination $stageProject -ExcludeDirectories @('captures', 'screenshots')

    $exePath = Join-Path $script:PartialRoot $script:BundleExe
    foreach ($phase in @(
        @{ name = 'import'; arguments = @('--headless', '--path', $stageProject, '--import') },
        @{ name = 'export'; arguments = @('--headless', '--path', $stageProject, '--export-release', $Preset, $exePath) }
    )) {
        Write-BundleStep "godot $($phase.name) ..."
        $log = Join-Path $logRoot "godot-$($phase.name).log"
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $output = @(& $godotIdentity.executable @($phase.arguments) 2>&1)
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousPreference
        Write-BundleTextFile -Path $log -Content ((($output | ForEach-Object { $_.ToString() }) -join "`n") + "`n")
        if ($exitCode -ne 0) {
            throw (New-BundleRefusal -Problem "Godot $($phase.name) failed with exit code $exitCode." -Remedy "Read $log")
        }
        $errorCount = @($output | Where-Object { $_.ToString() -cmatch '^(ERROR|SCRIPT ERROR|FATAL):' }).Count
        if ($errorCount -gt 0) { Write-BundleWarn "godot $($phase.name) printed $errorCount ERROR line(s); see $log" }
    }

    $pckPath = [IO.Path]::ChangeExtension($exePath, '.pck')
    if (-not (Test-Path -LiteralPath $exePath -PathType Leaf) -or (New-Object IO.FileInfo $exePath).Length -lt 64KB) {
        throw (New-BundleRefusal -Problem 'The export produced no usable executable.' -Remedy "Read $(Join-Path $logRoot 'godot-export.log')")
    }
    if (-not (Test-Path -LiteralPath $pckPath -PathType Leaf) -or (New-Object IO.FileInfo $pckPath).Length -eq 0) {
        throw (New-BundleRefusal -Problem 'The export produced no usable PCK, so the build has no game data.' -Remedy "Read $(Join-Path $logRoot 'godot-export.log')")
    }
    $exeInfo = New-Object IO.FileInfo $exePath
    $pckInfo = New-Object IO.FileInfo $pckPath
    Write-BundleGood "exported $($script:BundleExe) ($(Format-BundleBytes $exeInfo.Length)) and $($script:BundlePck) ($(Format-BundleBytes $pckInfo.Length))"

    # ----------------------------------------------------------- stage content
    Write-BundleHeading 'Stage content packs'
    $bundleContentRoot = Join-Path $script:PartialRoot $script:BundleContentDir
    [void](New-Item -ItemType Directory -Path $bundleContentRoot -Force)
    Write-BundleTextFile -Path (Join-Path $bundleContentRoot 'selection.json') -Content $selection.rawText

    $packRecords = New-Object 'System.Collections.Generic.List[object]'
    $totalFiles = 0
    $totalBytes = [long]0
    $wotrRecord = [ordered]@{
        staged = $false
        degraded = $true
        reason = $(if ($wotrReason -ne '') { $wotrReason } else { 'the active pack was never staged, so nothing could be added to it' })
        packRelative = ''
        documentSource = $LivingWorldDocument
        documentBytes = $wotrDocumentBytes
        documentRelative = ''
        documentChoiceBasis = [string]$wotrPlan.document.basis
        documentProvenance = [string]$wotrPlan.document.provenanceFileName
        mapSource = $LivingMapBundle
        mapRelative = ''
        files = 0
        bytes = [long]0
        reachableWithoutEnvironment = $false
        loaderDirectory = $script:WotrLoaderDirectory
        bundlesDeclared = @($wotrPlan.rules).Count
        bundlesStaged = 0
        bundlesMissing = @($wotrMissing)
        bundles = @($wotrPlan.rules | ForEach-Object {
            [ordered]@{
                env = $_.env; schema = $_.schema; loader = $_.loader
                staged = $false; source = $_.source; packRelative = ''
                basis = $_.basis; losesWithout = $_.loses
            }
        })
    }
    foreach ($pack in $packs) {
        Write-BundleStep "copying $($pack.relative) ..."
        $destination = Join-Path $bundleContentRoot ($pack.relative -replace '/', '\')
        Invoke-Robocopy -Source $pack.path -Destination $destination

        Write-BundleStep "hashing source   $($pack.relative) ..."
        $sourceManifest = Get-BundleTreeManifest -Root $pack.path -Label "  source $($pack.id)"
        Write-BundleStep "hashing staged   $($pack.relative) ..."
        $stagedManifest = Get-BundleTreeManifest -Root $destination -Label "  staged $($pack.id)"

        if ($stagedManifest.sha256 -cne $sourceManifest.sha256 -or $stagedManifest.layoutSha256 -cne $sourceManifest.layoutSha256) {
            $differences = Compare-BundleTreeManifest -Expected $sourceManifest -Actual $stagedManifest
            throw (New-BundleRefusal -Problem ("The staged copy of '$($pack.relative)' does not match the source pack:`n           " + ($differences -join "`n           ")) -Remedy 'Do not ship this bundle. Re-run the build; if it repeats, the source pack or the disk is at fault.')
        }
        Write-BundleGood "$($pack.relative): $($stagedManifest.files) files, $(Format-BundleBytes $stagedManifest.bytes), hash matches source"

        # --------------------------------------- War of the Ring data overlay
        # AFTER the source-vs-staged proof, never before: that check exists to
        # prove the copy is faithful, and adding files first would make it
        # unprovable. The overlay is then folded into the recorded manifest, so
        # the verifier still re-hashes the tree it will actually find.
        #
        # It goes INSIDE the active pack because that is where the running game
        # looks: wotr_session.gd walks the pack roots ContentDB mounted and
        # opens <root>/data/living-world.json, and wotr_map_bundle.gd does the
        # same for <root>/data/living-map. Beside the exe is reachable only via
        # OPENBFME_LIVING_WORLD_DOC / OPENBFME_LIVING_MAP, and needing an
        # environment variable to see your own bundled content is the bug.
        if ($wotrPlan.groups.Count -gt 0 -and $pack.relative -ceq $selection.activePack) {
            Write-BundleStep "staging $($wotrPlan.groups.Count) War of the Ring bundle(s) into the active pack ..."

            # Mirrors first, merges after - New-WotrStagingPlan already ordered
            # the groups that way. A merge group shares a staged directory with
            # its host because that is literally where the game searches for it:
            # wotr_screen.gd hands the marker, region-image, string, UI and macro
            # loaders the directory the region geometry was found in, plus
            # <pack>/data/living-map-regions. Staging them anywhere else would
            # produce files no shipped build can reach.
            $stagedRoots = New-Object 'System.Collections.Generic.List[string]'
            $createdTargets = New-Object 'System.Collections.Generic.List[string]'
            foreach ($group in $wotrPlan.groups) {
                $target = Join-Path $destination ($group.destination -replace '/', '\')
                $alreadyMine = ($createdTargets -ccontains $target)
                if ($group.kind -ceq 'document') {
                    if (Test-Path -LiteralPath $target) {
                        throw (New-BundleRefusal -Problem "The content pack already ships $($group.destination); staging would replace it." -Remedy 'A pack that carries its own living-world document is already complete. Rebuild without staging, or remove the collision at the source.')
                    }
                    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
                    Copy-Item -LiteralPath $group.source -Destination $target -Force
                    $stagedRoots.Add($target)
                    $createdTargets.Add($target)
                } elseif ($group.primary) {
                    if (Test-Path -LiteralPath $target) {
                        throw (New-BundleRefusal -Problem "The content pack already ships $($group.destination); staging would replace it." -Remedy 'Rebuild without staging, or remove the collision at the source.')
                    }
                    Invoke-Robocopy -Source $group.source -Destination $target
                    $stagedRoots.Add($target)
                    $createdTargets.Add($target)
                } else {
                    # A merge is only ever allowed into a directory THIS BUILD
                    # created. Merging into one the pack itself ships would put
                    # files inside a pack tree whose hash was just proved, which
                    # is content substitution wearing staging's clothes.
                    if (-not $alreadyMine -and (Test-Path -LiteralPath $target)) {
                        throw (New-BundleRefusal -Problem "$($group.envs -join ', ') must be staged beside $($group.destination), and the content pack already ships that directory." -Remedy 'Rebuild the pack without it, or remove the collision at the source. Nothing may be merged into a pack-owned directory.')
                    }
                    Invoke-Robocopy -Source $group.source -Destination $target -Merge
                }
            }

            # Verify what LANDED, not what was asked for, against the schema each
            # loader itself declares. A copy that silently dropped a manifest
            # would otherwise produce a bundle that falls back with nothing
            # saying why - flat 2D map, flat marker plates, raw string keys.
            foreach ($rule in $wotrPlan.rules) {
                if (-not $rule.present) { continue }
                $landed = Join-Path $destination ($rule.landedRelative -replace '/', '\')
                [void](Test-BundleWotrArtifact -Path $landed -Schema $rule.schema `
                    -Label "the staged $($rule.schema) artefact ($($rule.env))" -Strict:($rule.kind -ceq 'document'))
                if ($rule.kind -ceq 'document') { [void](Test-BundleWotrDocument -Path $landed) }
                if ($rule.env -ceq 'OPENBFME_LIVING_MAP') {
                    # The 3D map keeps its own landed check as well, unchanged.
                    [void](Test-BundleWotrMap -Path (Join-Path $destination ($rule.destination -replace '/', '\')))
                }
            }

            $overlay = @{}
            $overlayBytes = [long]0
            $destinationFull = [IO.Path]::GetFullPath($destination).TrimEnd('\', '/')
            foreach ($root in $stagedRoots) {
                $files = @()
                if (Test-Path -LiteralPath $root -PathType Container) {
                    $files = @([IO.Directory]::EnumerateFiles($root, '*', [IO.SearchOption]::AllDirectories) | ForEach-Object { [IO.FileInfo]$_ })
                } else {
                    $files = @([IO.FileInfo]$root)
                }
                foreach ($file in $files) {
                    $relativeKey = $file.FullName.Substring($destinationFull.Length + 1).Replace('\', '/')
                    $hash = '-'
                    if (-not $stagedManifest.quick) { $hash = Get-BundleFileSha256 -Path $file.FullName }
                    $overlay[$relativeKey] = [pscustomobject]@{ bytes = $file.Length; sha256 = $hash }
                    $overlayBytes += $file.Length
                }
            }
            $stagedManifest = New-BundleMergedManifest -Base $stagedManifest -Added $overlay

            $bundleRecords = @($wotrPlan.rules | ForEach-Object {
                [ordered]@{
                    env         = $_.env
                    schema      = $_.schema
                    loader      = $_.loader
                    staged      = [bool]$_.present
                    source      = $_.source
                    packRelative = $(if ($_.present) { $_.landedRelative } else { '' })
                    basis       = $_.basis
                    losesWithout = $_.loses
                }
            })
            $wotrRecord = [ordered]@{
                staged        = $wotrStaged
                degraded      = $wotrDegraded
                reason        = $wotrReason
                packRelative  = $pack.relative
                documentSource = $LivingWorldDocument
                documentBytes  = $wotrDocumentBytes
                documentRelative = $(if ($LivingWorldDocument -ne '') { "$($script:BundleContentDir)/$($pack.relative)/$($script:WotrDocumentRelative)" } else { '' })
                documentChoiceBasis = [string]$wotrPlan.document.basis
                documentProvenance  = [string]$wotrPlan.document.provenanceFileName
                mapSource      = $LivingMapBundle
                mapRelative    = $(if ($LivingMapBundle -ne '') { "$($script:BundleContentDir)/$($pack.relative)/$($script:WotrMapRelative)" } else { '' })
                files          = $overlay.Count
                bytes          = $overlayBytes
                reachableWithoutEnvironment = $true
                loaderDirectory = $script:WotrLoaderDirectory
                bundlesDeclared = @($wotrPlan.rules).Count
                bundlesStaged   = @($wotrPlan.rules | Where-Object { $_.present }).Count
                bundlesMissing  = @($wotrMissing)
                bundles         = $bundleRecords
            }
            Write-BundleGood "War of the Ring: $($wotrRecord.bundlesStaged) of $($wotrRecord.bundlesDeclared) bundle(s), $($overlay.Count) files, $(Format-BundleBytes $overlayBytes) staged inside content-packs/$($pack.relative)/ - no environment variable needed"
            foreach ($rule in $wotrPlan.rules) {
                if ($rule.present) { Write-BundleGood ("  {0,-36} {1}" -f $rule.env, $rule.landedRelative) }
                else { Write-BundleWarn ("  {0,-36} NOT STAGED - {1}" -f $rule.env, $rule.loses) }
            }
        }

        $totalFiles += $stagedManifest.files
        $totalBytes += $stagedManifest.bytes
        $packRecords.Add([pscustomobject]@{
            relative        = $pack.relative
            id              = $pack.id
            bundleHash      = $pack.bundleHash
            version         = $pack.version
            title           = $pack.title
            role            = $(if ($pack.relative -ceq $selection.activePack) { 'active' } else { 'supplemental' })
            redistributable = $pack.redistributable
            sourcePath      = $pack.path
            files           = $stagedManifest.files
            bytes           = $stagedManifest.bytes
            layoutSha256    = $stagedManifest.layoutSha256
            sha256          = $stagedManifest.sha256
        })
    }

    # Nothing beyond what selection.json names may remain. A leftover pack from
    # an earlier attempt sitting beside the real one overrides it at load time -
    # this is the documented way a stale faction wins over the current build.
    $stagedIds = @($packs | ForEach-Object { $_.id })
    foreach ($directory in @(Get-ChildItem -LiteralPath $bundleContentRoot -Directory)) {
        if ($stagedIds -cnotcontains $directory.Name) {
            Write-BundleWarn "removing pack directory that selection.json does not name: $($directory.Name)"
            Remove-Item -LiteralPath $directory.FullName -Recurse -Force
        }
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $bundleContentRoot -File)) {
        if ($file.Name -cne 'selection.json') { Remove-Item -LiteralPath $file.FullName -Force }
    }
    foreach ($pack in $packs) {
        $idRoot = Join-Path $bundleContentRoot $pack.id
        foreach ($directory in @(Get-ChildItem -LiteralPath $idRoot -Directory)) {
            if ($directory.Name -cne $pack.bundleHash) {
                Write-BundleWarn "removing superseded bundle of $($pack.id): $($directory.Name)"
                Remove-Item -LiteralPath $directory.FullName -Recurse -Force
            }
        }
    }

    # ------------------------------------------------------------ launch check
    Write-BundleHeading 'Launch check'
    $withEnv = $null
    $withoutEnv = $null
    if ($SkipLaunchCheck) {
        Write-BundleWarn '-SkipLaunchCheck was passed: this bundle has NOT been booted. BUILD-INFO.json records that no launch verification was performed.'
    } else {
        Write-BundleStep "booting headless with OPENBFME_CONTENT set ..."
        $withEnv = Invoke-BundleLaunchProbe -Executable $exePath `
            -LogPath (Join-Path $logRoot 'launch-check.log') `
            -ContentRoot $bundleContentRoot -TimeoutSeconds $LaunchTimeoutSeconds
        if ($withEnv.timedOut) {
            throw (New-BundleRefusal -Problem "The exported build did not exit within $LaunchTimeoutSeconds seconds when booted headless." -Remedy "Read $($withEnv.logPath). Raise -LaunchTimeoutSeconds if the machine is simply slow.")
        }
        if ($withEnv.exitCode -ne 0) {
            throw (New-BundleRefusal -Problem "The exported build exited with code $($withEnv.exitCode) when booted headless against its own content." -Remedy "Read $($withEnv.logPath)")
        }
        if ($withEnv.contentDb -eq '') {
            throw (New-BundleRefusal -Problem 'The exported build booted but never reported a [ContentDB] census, so it loaded no content database.' -Remedy "Read $($withEnv.logPath)")
        }
        if ($withEnv.errorLines.Count -gt 0) {
            throw (New-BundleRefusal -Problem ("The exported build printed errors while booting:`n           " + ($withEnv.errorLines -join "`n           ")) -Remedy "Read $($withEnv.logPath)")
        }
        # A census of zeros is the failure this project keeps paying for: the
        # build boots, the menu appears, and every roster is empty. It looks like
        # "the game lost its factions" and it is indistinguishable from a stale
        # or unreadable pack unless something checks the numbers.
        if ($withEnv.contentDb -cmatch 'units=(?<units>[0-9]+)') {
            $unitCount = [int]$Matches['units']
            if ($unitCount -le 0) {
                throw (New-BundleRefusal -Problem ("The exported build booted, mounted its packs and resolved NOTHING from them:`n           $($withEnv.contentDb)`n           The content is staged and hash-verified, so the packs are intact - this build cannot read them.") -Remedy "This is a content-resolution defect in the code, not in the bundle. Compare against a build that does resolve this pack set before shipping. Full log: $($withEnv.logPath)")
            }
        } else {
            throw (New-BundleRefusal -Problem "The [ContentDB] census did not report a unit count, so the build's content cannot be judged: '$($withEnv.contentDb)'" -Remedy "Read $($withEnv.logPath)")
        }
        Write-BundleGood "booted in $($withEnv.seconds)s: $($withEnv.contentDb)"

        # Second, informational probe: what does this build do when someone just
        # double-clicks the exe with no environment set? Not every branch of this
        # project resolves content beside the executable, and a build that
        # silently falls back to a months-old pack in user:// is the failure this
        # whole exercise is about. Record the answer instead of guessing it.
        Write-BundleStep 'booting headless with NO OPENBFME_CONTENT (double-click behaviour) ...'
        $withoutEnv = Invoke-BundleLaunchProbe -Executable $exePath `
            -LogPath (Join-Path $logRoot 'launch-check-envless.log') `
            -ContentRoot '' -TimeoutSeconds $LaunchTimeoutSeconds
        if ($withoutEnv.timedOut -or $withoutEnv.exitCode -ne 0 -or $withoutEnv.contentDb -eq '') {
            Write-BundleWarn 'Without OPENBFME_CONTENT this build does not reach a content database. run-with-log.bat sets it; the README says so.'
        } elseif ($withoutEnv.contentDb -cne $withEnv.contentDb) {
            Write-BundleWarn "Without OPENBFME_CONTENT this build loads DIFFERENT content: $($withoutEnv.contentDb). Launch it with run-with-log.bat."
        } else {
            Write-BundleGood 'Without OPENBFME_CONTENT the build reaches the same census (it resolves content beside the exe).'
        }
    }

    $selfLocating = $false
    if ($null -ne $withoutEnv -and -not $withoutEnv.timedOut -and $withoutEnv.exitCode -eq 0 -and
        $null -ne $withEnv -and $withoutEnv.contentDb -ceq $withEnv.contentDb -and $withEnv.contentDb -ne '') {
        $selfLocating = $true
    }

    # --------------------------------------------------------- write provenance
    Write-BundleHeading 'Provenance and launcher'

    $artifacts = [ordered]@{
        exe = [ordered]@{ name = $script:BundleExe; bytes = $exeInfo.Length; sha256 = (Get-BundleFileSha256 -Path $exePath) }
        pck = [ordered]@{ name = $script:BundlePck; bytes = $pckInfo.Length; sha256 = (Get-BundleFileSha256 -Path $pckPath) }
    }
    $consoleExe = Join-Path $script:PartialRoot 'OpenBFME.console.exe'
    $hasConsoleExe = Test-Path -LiteralPath $consoleExe -PathType Leaf

    function ConvertTo-ProbeRecord {
        param($Probe)
        if ($null -eq $Probe) { return $null }
        return [ordered]@{
            contentRoot = $Probe.contentRoot
            exitCode    = $Probe.exitCode
            timedOut    = $Probe.timedOut
            seconds     = $Probe.seconds
            contentDb   = $Probe.contentDb
            errorLines  = @($Probe.errorLines)
            warningLines = @($Probe.warningLines)
        }
    }

    # ------------------------------------------------------------ patch notes
    # Written from `git log` over a stated range plus a citation file whose every
    # line names a commit inside it. Nothing here is inferred from what a feature
    # is called. This project's recurring failure is confident wrong reporting,
    # and a changelog is the easiest place in the whole build to commit it.
    $builtAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $previousRelease = Get-BundlePreviousRelease -ReleaseRoot $OutputRoot -RepoRoot $repoRoot -ExcludeName $Name
    if ($null -ne $previousRelease) {
        Write-BundleStep "previous release    $($previousRelease.name) ($($previousRelease.shortCommit), built $($previousRelease.builtAtUtc))"
    } else {
        Write-BundleWarn 'No previous release in this folder states which commit it was built from, so the notes fall back to a stated range and say so.'
    }
    $notes = New-BundleReleaseNotes -RepoRoot $repoRoot -BundleName $Name -Source $source `
        -BuiltAtUtc $builtAtUtc -PackRecords $packRecords.ToArray() -Since $Since `
        -HighlightsPath $Highlights -BaseBranch $BaseBranch -PreviousRelease $previousRelease `
        -WotrData ([pscustomobject]$wotrRecord)
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundlePatchNotes) -Content ($notes.notesText + "`n")
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundleChangeLog) -Content ($notes.changeText + "`n")
    Write-BundleGood "$($script:BundlePatchNotes): $($notes.commitCount) change(s) over $($notes.rangeBasis); $(@($notes.highlights).Count) player-facing note(s) cited"
    foreach ($droppedNote in @($notes.dropped)) {
        Write-BundleWarn "release note left out - the change it cites is not in this range: $droppedNote"
    }

    $buildInfo = [ordered]@{
        schema        = $script:BundleSchema
        schemaVersion = $script:BundleSchemaVersion
        bundleName    = $Name
        isRelease     = [bool]$Release
        builtAtUtc    = $builtAtUtc
        builtBy       = 'tools/Build-PlayableBundle.ps1'
        builtOnMachine = $env:COMPUTERNAME
        source        = [ordered]@{
            commit        = $source.commit
            shortCommit   = $source.shortCommit
            branch        = $source.branch
            branchesContaining = @($source.branchesContaining)
            commitDateIso = $source.commitDate
            commitSubject = $source.commitSubject
            describe      = $source.describe
            worktree      = $source.worktree
            dirty         = $source.dirty
            dirtyFileCount = $source.dirtyFileCount
            dirtyFiles    = @($source.dirtyFiles)
        }
        godot         = [ordered]@{
            executable             = $godotIdentity.executable
            version                = $godotIdentity.version
            preset                 = $Preset
            exportTemplatesRoot    = $godotIdentity.exportTemplatesRoot
            exportTemplatesVersion = $godotIdentity.exportTemplatesVersion
        }
        artifacts     = $artifacts
        hasConsoleExe = $hasConsoleExe
        content       = [ordered]@{
            sourceRoot = $ContentRoot
            selection  = [ordered]@{
                sha256            = $selection.sha256
                activePack        = $selection.activePack
                supplementalPacks = @($selection.supplementalPacks)
            }
            # .ToArray(), not @(...): Windows PowerShell 5.1 throws "Argument
            # types do not match" when a generic List is splatted into an
            # ordered-hashtable literal.
            packs      = $packRecords.ToArray()
            totalFiles = $totalFiles
            totalBytes = $totalBytes
        }
        redistributable = $redistributable
        warOfTheRing  = $wotrRecord
        releaseNotes  = [ordered]@{
            patchNotes  = $script:BundlePatchNotes
            changeLog   = $script:BundleChangeLog
            rangeBasis  = $notes.rangeBasis
            fromCommit  = $notes.fromCommit
            commitCount = $notes.commitCount
            citedNotes  = @($notes.highlights).Count
            highlightsSource = $Highlights
            previousRelease = $(if ($null -eq $previousRelease) { $null } else { [ordered]@{
                name = $previousRelease.name; commit = $previousRelease.commit; builtAtUtc = $previousRelease.builtAtUtc } })
        }
        launchCheck   = [ordered]@{
            performed         = (-not $SkipLaunchCheck)
            selfLocatesContent = $selfLocating
            withContentEnv    = (ConvertTo-ProbeRecord -Probe $withEnv)
            withoutContentEnv = (ConvertTo-ProbeRecord -Probe $withoutEnv)
        }
    }
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundleInfoJson) -Content ((ConvertTo-BundleJsonText -Value $buildInfo) + "`n")

    $packLines = @($packRecords | ForEach-Object {
        "  $($_.role.PadRight(12)) $($_.id)`n               version   $($_.version)`n               bundle    $($_.bundleHash)`n               content   $($_.files) files, $(Format-BundleBytes $_.bytes)`n               sha256    $($_.sha256)"
    })
    $launchSummary = 'NOT PERFORMED (-SkipLaunchCheck)'
    if (-not $SkipLaunchCheck) {
        $launchSummary = "booted headless in $($withEnv.seconds)s, exit 0`n  census       $($withEnv.contentDb)"
    }
    $buildInfoText = @"
OpenBFME playable bundle
========================
  bundle       $Name
  built (UTC)  $($buildInfo.builtAtUtc)
  built on     $($buildInfo.builtOnMachine)

SOURCE
  commit       $($source.commit)
  branch       $($source.branch)$(if (@($source.branchesContaining).Count -gt 0) { "`n  also on      " + (@($source.branchesContaining) -join ', ') } else { '' })
  committed    $($source.commitDate)
  subject      $($source.commitSubject)
  describe     $($source.describe)
  worktree     $($source.worktree)
  working tree $(if ($source.dirty) { "DIRTY - $($source.dirtyFileCount) uncommitted files were built into this bundle" } else { 'clean' })

  If a feature you expect is missing, compare this commit against the branch you
  expect it on before concluding the project regressed. That comparison is the
  reason this file exists.

ENGINE
  godot        $($godotIdentity.version)
  preset       $Preset
  templates    $($godotIdentity.exportTemplatesVersion)

ARTIFACTS
  OpenBFME.exe $(Format-BundleBytes $exeInfo.Length)
               $($artifacts.exe.sha256)
  OpenBFME.pck $(Format-BundleBytes $pckInfo.Length)
               $($artifacts.pck.sha256)

CONTENT  ($totalFiles files, $(Format-BundleBytes $totalBytes))
  staged from  $ContentRoot
  selection    sha256 $($selection.sha256)
$($packLines -join "`n")

  Redistributable: $(if ($redistributable) { 'yes' } else { 'NO - retail-derived. Keep this bundle inside the dev group.' })

WAR OF THE RING  ($($wotrRecord.bundlesStaged) of $($wotrRecord.bundlesDeclared) converted bundles, $($wotrRecord.files) files, $(Format-BundleBytes ([long]$wotrRecord.bytes)))
  What must be here is not a list somebody maintains. It is derived from the
  loaders in $($wotrRecord.loaderDirectory): each one names the data it needs, and a
  loader with nothing behind it appears below as NOT STAGED rather than silently.

$(@($wotrRecord.bundles | ForEach-Object { if ($_.staged) {
"  STAGED       $($_.env)`n               $($_.packRelative)"
} else {
"  NOT STAGED   $($_.env)`n               the player loses $($_.losesWithout)"
} }) -join "`n")

  menu entry   $(if ($wotrRecord.staged) { 'OPENS - the document and the 3D map are both here' } else { 'READS (UNAVAILABLE) - the document or the 3D map is absent' })
  document     $(if ([string]$wotrRecord.documentSource -ne '') { [IO.Path]::GetFileName([string]$wotrRecord.documentSource) } else { 'none' })
               chosen by: $($wotrRecord.documentChoiceBasis)
  reachable    without any environment variable: the running game finds all of
               this by walking the pack roots it mounted. None of the
               OPENBFME_LIVING_* settings are needed, and the launcher sets none.
$(if ($wotrRecord.degraded) { @"

  THIS BUILD IS INCOMPLETE and says so rather than hiding it. A build that ships
  the feature without the data behind it is a defect, not a feature that happens
  to be off. Reason: $($wotrRecord.reason)
"@ } else { '' })
RELEASE NOTES
  $($script:BundlePatchNotes) - $($notes.commitCount) change(s)
  range        $($notes.rangeBasis)
  cited notes  $(@($notes.highlights).Count) player-facing line(s), each naming the change it came from
  full history $($script:BundleChangeLog)

LAUNCH CHECK
  $launchSummary
  content resolution without OPENBFME_CONTENT: $(if ($SkipLaunchCheck) { 'not tested' } elseif ($selfLocating) { 'works - the build finds content-packs beside the exe' } else { 'DOES NOT match - always launch with run-with-log.bat' })

VERIFY THIS BUNDLE AT ANY TIME
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\Test-PlayableBundle.ps1 -Bundle "<this folder>"
"@
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundleInfoText) -Content ($buildInfoText + "`n")

    $launchNote = 'Run run-with-log.bat. It sets the content path, keeps the console open and writes run.log.'
    $exeNote = 'Double-clicking OpenBFME.exe directly is NOT supported by this build: it does not resolve content-packs on its own.'
    if ($selfLocating) {
        $exeNote = 'Double-clicking OpenBFME.exe works too - this build finds content-packs beside itself - but the exe gives you no console, so a startup error vanishes with the window.'
    }

    $launcher = @"
@echo off
setlocal
REM Launches the bundle with its own content and keeps the console open.
REM Generated by tools/Build-PlayableBundle.ps1 - do not hand-edit; rebuild.
cd /d "%~dp0"
if exist run.log del run.log

REM The content path is set explicitly. Leaving it to chance is how a build ends
REM up quietly loading a months-old pack from your user profile and looking like
REM it lost features.
set "OPENBFME_CONTENT=%~dp0content-packs"

echo Launching OpenBFME...
echo   bundle : $Name
echo   commit : $($source.shortCommit) on $($source.branch)
echo   content: %OPENBFME_CONTENT%
echo   log    : %~dp0run.log
echo.

"%~dp0$($script:BundleExe)" > "%~dp0run.log" 2>&1
set "CODE=%ERRORLEVEL%"

echo.
echo ---------------------------------------------------------------
echo Exit code: %CODE%
echo ---------------------------------------------------------------
echo.
echo Content it loaded:
findstr /C:"[ContentDB]" /C:"workspace content root" /C:"DURABLE" "%~dp0run.log"
echo.
echo Errors (if any):
findstr /C:"ERROR" /C:"SCRIPT ERROR" /C:"Parse Error" "%~dp0run.log" | findstr /V "Unreferenced static string"
echo.
echo Full log: %~dp0run.log
echo Build identity: %~dp0BUILD-INFO.txt
echo.
pause
"@
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundleLauncher) -Content $launcher

    $readme = @"
OpenBFME - playable bundle
==========================
  $Name
  commit $($source.shortCommit) on $($source.branch), built $($buildInfo.builtAtUtc) UTC

WHAT THIS IS
  A Godot rebuild of BFME2 skirmish, bundled with pre-converted content so it
  runs without owning or converting anything yourself.

  NOT FOR REDISTRIBUTION. content-packs/ is derived from a retail BFME2/RotWK
  installation. Keep this bundle inside the dev group.

HOW TO RUN
  $launchNote
  $exeNote

LAYOUT - the game finds content in content-packs NEXT TO the exe.
  OpenBFME.exe
  OpenBFME.pck
  content-packs/          <- must stay beside the exe
    selection.json
$(@($packRecords | ForEach-Object { "    $($_.id)/$($_.bundleHash)/..." }) -join "`n")
  run-with-log.bat
  PATCH-NOTES.txt         <- what changed since the last release, in plain words
  CHANGES.txt             <- every change in this release, raw
  BUILD-INFO.txt          <- exactly what is in this build
  BUILD-INFO.json         <- the same, machine-readable
  logs/                   <- export and launch-check logs from the build

WAR OF THE RING
$(if ($wotrRecord.staged) { @"
  Its data ships inside this bundle, so the menu entry opens with no setup - the
  3D Middle-earth map, the filled territories, the 3D army and building models,
  the region portraits and retail's own names for all of it. You do not need any
  OPENBFME_LIVING_* environment variable.
"@ } else { @"
  NOT IN THIS BUILD. The menu entry reads WAR OF THE RING (UNAVAILABLE) and will
  not open: $($wotrRecord.reason)
"@ })$(if ($wotrRecord.staged -and $wotrRecord.degraded) { @"

  It is INCOMPLETE, and BUILD-INFO.txt names exactly which parts are missing and
  what you will see instead. That list is generated, not written by hand.
"@ } else { '' })

  To point at a different pack root instead:
    set "OPENBFME_CONTENT=D:\some\other\content-packs"

WHAT IS IN THIS BUILD
  Read BUILD-INFO.txt. It names the commit, the branch, whether the working tree
  was dirty, and every content pack with its hash. If something you expect is
  missing, check that commit against the branch you expect the feature on -
  a bundle is only ever as new as the commit it was built from.

REPORTING A PROBLEM
  Send run.log and BUILD-INFO.txt. Between them they say exactly which build,
  which commit and which content produced the behaviour.
"@
    Write-BundleTextFile -Path (Join-Path $script:PartialRoot $script:BundleReadme) -Content ($readme + "`n")
    Write-BundleGood 'wrote BUILD-INFO.json, BUILD-INFO.txt, README.txt, PATCH-NOTES.txt, CHANGES.txt, run-with-log.bat'

    # ------------------------------------------------------------- verification
    Write-BundleHeading 'Verify the produced bundle'
    if ($FastFinalVerify) {
        Write-BundleWarn '-FastFinalVerify: checking layout and sizes only, not re-hashing every staged file.'
    }
    $problems = @(Test-BundleTree -BundleRoot $script:PartialRoot -Quick:$FastFinalVerify)
    if ($problems.Count -gt 0) {
        throw (New-BundleRefusal -Problem ("The bundle this build just produced does not verify:`n           " + ($problems -join "`n           ")) -Remedy 'Do not ship it. This means the staged tree changed after it was written, or the disk is unreliable.')
    }
    Write-BundleGood 'bundle verifies against its own BUILD-INFO.json'

    # --------------------------------------------------------------- promote
    if (Test-Path -LiteralPath $bundleRoot) { Remove-Item -LiteralPath $bundleRoot -Recurse -Force }
    Rename-Item -LiteralPath $script:PartialRoot -NewName $Name
    $script:PartialRoot = ''

    # A copy of the notes beside the bundle, so the release folder can be read
    # without opening a 4 GB directory to find out what is in it.
    $notesBeside = Join-Path $OutputRoot "$Name-PATCH-NOTES.txt"
    Write-BundleTextFile -Path $notesBeside -Content ($notes.notesText + "`n")

    $elapsed = (Get-Date) - $script:StartedAt
    Write-Host ''
    Write-Host 'BUNDLE READY' -ForegroundColor Green
    Write-Host "  $bundleRoot"
    Write-Host "  commit  $($source.shortCommit) on $($source.branch)$(if ($source.dirty) { ' (working tree was dirty)' } else { '' })"
    Write-Host "  content $($packRecords.Count) pack(s), $totalFiles files, $(Format-BundleBytes $totalBytes)"
    if (-not $SkipLaunchCheck) { Write-Host "  booted  $($withEnv.contentDb)" }
    Write-Host "  took    $([int]$elapsed.TotalMinutes)m $($elapsed.Seconds)s"
    Write-Host ''
    if ($wotrRecord.staged -and -not $wotrRecord.degraded) {
        Write-Host "  wotr    all $($wotrRecord.bundlesDeclared) converted bundles staged ($($wotrRecord.files) files) - the menu entry opens with no environment set"
    } elseif ($wotrRecord.staged) {
        Write-Host "  wotr    DEGRADED - $($wotrRecord.bundlesStaged) of $($wotrRecord.bundlesDeclared) bundles staged; BUILD-INFO.txt names what the player loses" -ForegroundColor Yellow
    } else {
        Write-Host "  wotr    NOT STAGED - the menu entry will read WAR OF THE RING (UNAVAILABLE): $($wotrRecord.reason)" -ForegroundColor Yellow
    }
    Write-Host ''
    Write-Host "  Play it:      $bundleRoot\$($script:BundleLauncher)"
    Write-Host "  What changed: $bundleRoot\$($script:BundlePatchNotes)"
    Write-Host "                $notesBeside"
    Write-Host "  What it is:   $bundleRoot\$($script:BundleInfoText)"
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
        # An unanticipated failure still has to be actionable, so show where it
        # happened rather than a bare one-line exception.
        Write-Host ''
        Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    }
    if ($script:PartialRoot -ne '' -and (Test-Path -LiteralPath $script:PartialRoot)) {
        Write-Host ''
        Write-Host "No bundle was produced. The incomplete build is left at:" -ForegroundColor Yellow
        Write-Host "  $($script:PartialRoot)" -ForegroundColor Yellow
        Write-Host "It is deliberately named .partial so it cannot be mistaken for a finished bundle." -ForegroundColor Yellow
    }
    Write-Host ''
    exit 1
}
