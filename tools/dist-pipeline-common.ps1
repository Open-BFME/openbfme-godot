<#
.SYNOPSIS
    Shared logic for the dist publishing pipeline: one version, one firewall.

.DESCRIPTION
    Dot-sourced by tools/Publish-DistBuild.ps1 (the operator entry point) and by
    tools/Test-DistPipeline.ps1 (the fast gate). Everything here is a pure
    function over a repository path so the gate can drive it against a scratch
    repository in a second, instead of only ever being exercised by a build that
    takes half an hour.

    TWO THINGS LIVE HERE AND NOTHING ELSE:

    1. THE VERSION. `VERSION` at the repository root is the single source of
       truth. Four other files repeat it - project.godot (what the menu prints),
       boot.tscn (what the boot screen prints), game/data/build_info.json (what
       an export carries, since an export ships no .git) and the patch-notes
       filename. Repetition is fine; DRIFT is not, so every copy is compared and
       a mismatch refuses the publish rather than shipping a build that calls
       itself two different things in two different screens.

    2. THE PUBLICATION BOUNDARY. A dist build contains converted retail bytes. It
       must be unreachable from git on every branch, and "unreachable" is asked
       of git, never assumed: the directory must be ignored AND must have no
       tracked files under it. Both are checked, because .gitignore does not
       apply to a path git already tracks - a single `git add -f dist` in
       history would make the ignore rule decorative.
#>

Set-StrictMode -Version Latest

$script:DistVersionPattern = '^(?<v>[0-9]+\.[0-9]+\.[0-9]+)(?<pre>-[0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*)?$'
$script:DistPatchNotesDirectory = 'docs/patch-notes'
$script:DistVersionStampName = 'VERSION.json'
$script:DistPatchNotesName = 'PATCH-NOTES.md'
$script:DistLauncherExe = 'OpenBFME.Launcher.exe'


function New-DistRefusal {
    param(
        [Parameter(Mandatory)][string]$Problem,
        [string]$Remedy = ''
    )
    $text = "REFUSED: $Problem"
    if ($Remedy -ne '') { $text += "`n           Fix: $Remedy" }
    return (New-Object System.Management.Automation.RuntimeException $text)
}


function Get-DistRepoRoot {
    param([Parameter(Mandatory)][string]$StartPath)
    $full = [IO.Path]::GetFullPath($StartPath)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $top = (& git -C $full rev-parse --show-toplevel 2>$null | Out-String).Trim()
    } finally { $ErrorActionPreference = $previous }
    if ($top -eq '') {
        throw (New-DistRefusal -Problem "Not inside a git checkout: $full")
    }
    return [IO.Path]::GetFullPath($top)
}


function Read-DistVersionFile {
    <#
    .SYNOPSIS
        The single source of truth, validated. Anything unparseable is a refusal,
        never a silent default - a publish named by a fallback string is exactly
        the "which build is this?" question this pipeline exists to answer.
    #>
    param([Parameter(Mandatory)][string]$RepoRoot)
    $path = Join-Path $RepoRoot 'VERSION'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw (New-DistRefusal -Problem "The version source of truth is missing: $path" -Remedy 'Create VERSION at the repository root holding one line, e.g. 0.2.1')
    }
    $raw = ([IO.File]::ReadAllText($path)).Trim()
    if ($raw -cnotmatch $script:DistVersionPattern) {
        throw (New-DistRefusal -Problem "VERSION does not hold a version: '$raw'" -Remedy 'Use major.minor.patch with an optional prerelease suffix, e.g. 0.2.1 or 0.2.1-beta.2')
    }
    return $raw
}


function Get-DistProjectSetting {
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Key
    )
    $path = Join-Path $RepoRoot 'game\project.godot'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        $match = [regex]::Match($line, '^\s*' + [regex]::Escape($Key) + '\s*=\s*"(?<value>[^"]*)"\s*$')
        if ($match.Success) { return $match.Groups['value'].Value }
    }
    return $null
}


function Get-DistBootScreenVersion {
    <#
    .SYNOPSIS
        The literal the boot screen prints. A scene file, so it is read as text.
    .DESCRIPTION
        Returns $null when the scene has no v-prefixed label at all, which is a
        different fact from "it disagrees" and is reported differently.
    #>
    param([Parameter(Mandatory)][string]$RepoRoot)
    $path = Join-Path $RepoRoot 'game\scenes\boot.tscn'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    $matches = [regex]::Matches([IO.File]::ReadAllText($path), '(?m)^text\s*=\s*"v(?<value>[0-9][^"]*)"\s*$')
    if ($matches.Count -eq 0) { return $null }
    return $matches[0].Groups['value'].Value
}


function Get-DistBuildInfo {
    param([Parameter(Mandatory)][string]$RepoRoot)
    $path = Join-Path $RepoRoot 'game\data\build_info.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    $text = [IO.File]::ReadAllText($path)
    # Windows PowerShell writes UTF-8 with a BOM; ConvertFrom-Json chokes on it.
    $text = $text.TrimStart([char]0xFEFF)
    try { return ($text | ConvertFrom-Json) } catch { return $null }
}


function Get-DistBuildInfoAtCommit {
    <#
    .SYNOPSIS
        The build_info.json that was EXPORTED into a given build, read out of
        git rather than off the working tree.
    .DESCRIPTION
        The same mistake as stamping HEAD, one field over. `build` and
        `buildInfoCommit` describe what the GAME prints in its own menu, and the
        game prints whatever was baked into the .pck - which is the file as it
        stood at the bundle's commit, not as it stands in the checkout now.
        Finishing an older bundle from a moved-on checkout reported build 171
        for a .pck containing build 166.

        The .pck cannot be opened here, but git has the exact bytes that went
        into it. Returns $null when git cannot produce them, so the caller can
        fall back out loud instead of quietly using the wrong file.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Commit
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& git -C $RepoRoot show "${Commit}:game/data/build_info.json" 2>$null | Out-String)
    } finally { $ErrorActionPreference = $previous }
    if ($text.Trim() -eq '') { return $null }
    try { return ($text.TrimStart([char]0xFEFF) | ConvertFrom-Json) } catch { return $null }
}


function Get-DistPatchNotesPath {
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Version
    )
    return (Join-Path $RepoRoot ("$($script:DistPatchNotesDirectory)/v$Version.md" -replace '/', '\'))
}


function Test-DistVersionConsistency {
    <#
    .SYNOPSIS
        Every place this product states its own version, compared against VERSION.
    .DESCRIPTION
        Returns a list of problem strings; empty means consistent. It returns
        rather than throws so the gate can report all of them at once - fixing
        four drifted strings one refusal at a time is four builds.
    #>
    param([Parameter(Mandatory)][string]$RepoRoot)
    $problems = New-Object 'System.Collections.Generic.List[string]'
    $version = Read-DistVersionFile -RepoRoot $RepoRoot

    $projectVersion = Get-DistProjectSetting -RepoRoot $RepoRoot -Key 'config/version'
    if ($null -eq $projectVersion) {
        $problems.Add('game/project.godot has no config/version, so the main menu can print no version at all.')
    } elseif ($projectVersion -cne $version) {
        $problems.Add("game/project.godot config/version is '$projectVersion' but VERSION says '$version'. The main menu would print the wrong number.")
    }

    $bootVersion = Get-DistBootScreenVersion -RepoRoot $RepoRoot
    if ($null -eq $bootVersion) {
        $problems.Add('game/scenes/boot.tscn has no version label, so the boot screen states nothing.')
    } elseif ($bootVersion -cne $version) {
        $problems.Add("game/scenes/boot.tscn prints 'v$bootVersion' but VERSION says '$version'. The boot screen and the menu would disagree.")
    }

    $buildInfo = Get-DistBuildInfo -RepoRoot $RepoRoot
    if ($null -eq $buildInfo) {
        $problems.Add('game/data/build_info.json is missing or unreadable. Run tools/Write-BuildInfo.ps1.')
    } else {
        $buildInfoVersion = ''
        if ($buildInfo.PSObject.Properties.Name -ccontains 'version') {
            $buildInfoVersion = [string]$buildInfo.version
        }
        if ($buildInfoVersion -eq '') {
            $problems.Add('game/data/build_info.json carries no version field, so an export cannot state one. Run tools/Write-BuildInfo.ps1.')
        } elseif ($buildInfoVersion -cne $version) {
            $problems.Add("game/data/build_info.json says version '$buildInfoVersion' but VERSION says '$version'. Re-run tools/Write-BuildInfo.ps1 and commit it.")
        }
    }

    $notes = Get-DistPatchNotesPath -RepoRoot $RepoRoot -Version $version
    if (-not (Test-Path -LiteralPath $notes -PathType Leaf)) {
        $problems.Add("There are no patch notes for this version: $notes. A published build with no notes is a build nobody can tell apart from the last one.")
    }

    return $problems.ToArray()
}


function Get-DistOwningCheckout {
    <#
    .SYNOPSIS
        Which working tree does this path belong to? Not "which one am I in".
    .DESCRIPTION
        A second real hole this gate found, this one on the first live run.

        Publishing happens FROM a worktree and INTO the main checkout, because a
        worktree is temporary and a build published into one disappears with it.
        Ask `git -C <worktree> check-ignore <main>\dist` and git answers with an
        error, because the path is outside the working tree it was asked in -
        and an error read as "not ignored" reports the firewall as DOWN when it
        is up. That is a false alarm rather than a false pass, so it refused
        rather than leaked, but it stopped every publish from a worktree.

        The question is therefore asked of the checkout that OWNS the path.
        Walks up to the nearest ancestor that exists, since the directory may
        not have been created yet.
    #>
    param([Parameter(Mandatory)][string]$Path)
    $current = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    while ($current -ne '' -and $null -ne $current -and -not [IO.Directory]::Exists($current)) {
        $current = [IO.Path]::GetDirectoryName($current)
    }
    if ($current -eq '' -or $null -eq $current) { return '' }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $top = (& git -C $current rev-parse --show-toplevel 2>$null | Out-String).Trim()
    } finally { $ErrorActionPreference = $previous }
    if ($top -eq '') { return '' }
    return [IO.Path]::GetFullPath($top)
}


function Get-DistTrackedFiles {
    <#
    .SYNOPSIS
        What git is tracking under a path. The question .gitignore cannot answer.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Path
    )
    $full = [IO.Path]::GetFullPath($Path)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $RepoRoot ls-files -- $full 2>$null | Where-Object { $_ -ne '' })
    } finally { $ErrorActionPreference = $previous }
    return $output
}


function Test-DistPathIsIgnored {
    <#
    .SYNOPSIS
        Would anything written at this path be invisible to git?
    .DESCRIPTION
        Not the same question as "is this path ignored", and the difference is a
        real hole that this gate found: the rules are `/dist/` and `build/`, both
        DIRECTORY patterns, and git matches a directory pattern only against a
        path that is a directory ON DISK. Ask about `dist` before the first
        publish has created it and git answers "not ignored" - which would have
        been read as the firewall being down when it was not.

        So a path that does not exist is asked about via a would-be child:
        `/dist/` matches `dist/<anything>`, existing or not. That is the question
        the caller actually has, and it has one answer whether or not somebody
        has published yet.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Path
    )
    $full = [IO.Path]::GetFullPath($Path)
    $probe = $full
    if (-not (Test-Path -LiteralPath $full)) {
        $probe = Join-Path $full '.would-be-written-here'
    }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git -C $RepoRoot check-ignore --quiet -- $probe 2>$null | Out-Null
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    return ($code -eq 0)
}


function Assert-DistReleaseFirewall {
    <#
    .SYNOPSIS
        The hard rule: converted retail bytes never become reachable from git.
    .DESCRIPTION
        Both halves are checked because either alone is a hole:
          - ignored but tracked  -> .gitignore is decorative; git already has it
          - untracked but not ignored -> one `git add -A` away from permanent
        A refusal here costs seconds. The failure it prevents is a force-push
        away from being someone else's clone.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$DistRoot
    )
    # Ask the checkout that OWNS the dist root, not the one this script is
    # running from - publishing happens FROM a worktree INTO the main checkout,
    # and git answers questions about paths outside its own working tree with an
    # error that reads exactly like "not ignored".
    $owner = Get-DistOwningCheckout -Path $DistRoot
    if ($owner -eq '') {
        throw (New-DistRefusal `
            -Problem "No git checkout owns the dist root, so nothing can prove it is unreachable from git: $DistRoot" `
            -Remedy 'Publish into a directory inside the repository (dist/ by default), or one whose own checkout ignores it.')
    }
    if ($owner -cne [IO.Path]::GetFullPath($RepoRoot)) {
        Write-Verbose "dist root belongs to $owner, not $RepoRoot; asking git there."
    }
    $RepoRoot = $owner
    $tracked = @(Get-DistTrackedFiles -RepoRoot $RepoRoot -Path $DistRoot)
    if ($tracked.Count -gt 0) {
        $preview = ($tracked | Select-Object -First 10) -join "`n           "
        throw (New-DistRefusal `
            -Problem ("git is TRACKING $($tracked.Count) file(s) under the dist root, so publishing there would stage retail-derived bytes for commit:`n           $preview") `
            -Remedy 'Run: git rm -r --cached dist   then commit the removal. .gitignore does not apply to paths git already tracks.')
    }
    if (-not (Test-DistPathIsIgnored -RepoRoot $RepoRoot -Path $DistRoot)) {
        throw (New-DistRefusal `
            -Problem "The dist root is NOT git-ignored: $DistRoot" `
            -Remedy 'Add /dist/ to .gitignore. A dist build carries converted retail content and must never be commit-reachable.')
    }
    return $true
}


function Resolve-DistSourceCommit {
    <#
    .SYNOPSIS
        Which commit did the BYTES being published come from?
    .DESCRIPTION
        Not the same question as "what is HEAD", and answering it with HEAD
        shipped a wrong stamp: -FinishOnly finishes a bundle that was built
        earlier, so by the time the notes, VERSION.json and the launcher install
        root were written, HEAD had moved two commits on and all three claimed a
        commit whose bytes were never in the folder. BUILD-INFO.json - written by
        the build, from the tree the build actually exported - said something
        else, and it was right.

        So on the finishing path the answer comes from the artefact, and on the
        building path from git. `origin` says which, so a caller can refuse a
        mismatch rather than average two numbers.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][bool]$FromBundle,
        $BuildInfo = $null
    )
    if ($FromBundle) {
        if ($null -eq $BuildInfo -or $null -eq $BuildInfo.source) {
            throw (New-DistRefusal `
                -Problem 'The existing bundle carries no BUILD-INFO.json source record, so nothing can say which commit its bytes came from.' `
                -Remedy 'Rebuild it: run without -FinishOnly and with -Force.')
        }
        $full = [string]$BuildInfo.source.commit
        $short = [string]$BuildInfo.source.shortCommit
        if ($full -cnotmatch '^[0-9a-f]{40}$') {
            throw (New-DistRefusal -Problem "The existing bundle records an unusable commit: '$full'" -Remedy 'Rebuild it.')
        }
        if ($short -eq '') { $short = $full.Substring(0, 7) }
        return [pscustomobject]@{ commit = $full; shortCommit = $short; origin = 'bundle' }
    }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $full = (& git -C $RepoRoot rev-parse HEAD 2>$null | Out-String).Trim()
        $short = (& git -C $RepoRoot rev-parse --short HEAD 2>$null | Out-String).Trim()
    } finally { $ErrorActionPreference = $previous }
    if ($full -cnotmatch '^[0-9a-f]{40}$') {
        throw (New-DistRefusal -Problem "git could not name HEAD in $RepoRoot.")
    }
    return [pscustomobject]@{ commit = $full; shortCommit = $short; origin = 'head' }
}


function Get-DistCommitDistance {
    <#
    .SYNOPSIS
        How many commits separate two revisions, or '?' when git cannot say.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$From,
        [Parameter(Mandatory)][string]$To
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $count = (& git -C $RepoRoot rev-list --count "$From..$To" 2>$null | Out-String).Trim()
    } finally { $ErrorActionPreference = $previous }
    if ($count -cnotmatch '^[0-9]+$') { return '?' }
    return $count
}


function New-DistVersionStamp {
    <#
    .SYNOPSIS
        What this folder is, in one small file, cross-checked against build_info.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][bool]$IsReleaseCandidate,
        [Parameter(Mandatory)][string]$SourceCommit,
        # The build_info.json that was EXPORTED into this build. Supplied by the
        # caller because only the caller knows whether that is the checkout's
        # copy (building) or the one at the bundle's commit (finishing).
        $ExportedBuildInfo = $null
    )
    $buildInfo = $ExportedBuildInfo
    if ($null -eq $buildInfo) { $buildInfo = Get-DistBuildInfo -RepoRoot $RepoRoot }
    $build = ''
    $buildInfoCommit = ''
    if ($null -ne $buildInfo) {
        if ($buildInfo.PSObject.Properties.Name -ccontains 'build') { $build = [string]$buildInfo.build }
        if ($buildInfo.PSObject.Properties.Name -ccontains 'commit') { $buildInfoCommit = [string]$buildInfo.commit }
    }
    # TWO commits, deliberately, because they are two different facts and
    # collapsing them would be a lie in whichever direction it went:
    #   commit          - what this folder was actually built from. Authoritative.
    #   buildInfoCommit - what the game prints in its own menu.
    # They differ by one commit as a matter of structure: build_info.json is
    # generated, then committed, so it can never contain the hash of the commit
    # that contains it. Recording both makes a support report legible instead of
    # making one of them wrong.
    $stamp = [ordered]@{
        schema             = 'openbfme.dist-version'
        schemaVersion      = 1
        version            = $Version
        channel            = 'beta'
        releaseCandidate   = $IsReleaseCandidate
        build              = $build
        commit             = $SourceCommit
        buildInfoCommit    = $buildInfoCommit
        publishedUtc       = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        publishedBy        = 'tools/Publish-DistBuild.ps1'
        patchNotes         = $script:DistPatchNotesName
        buildIdentity      = 'BUILD-INFO.json'
        redistributable    = $false
    }
    $text = ($stamp | ConvertTo-Json -Depth 5) + "`n"
    [IO.File]::WriteAllText((Join-Path $Destination $script:DistVersionStampName), $text, [Text.UTF8Encoding]::new($false))
    return $stamp
}


function New-DistFileLink {
    <#
    .SYNOPSIS
        Hardlink if the volume allows it, copy if it does not. Never silently
        neither.
    .DESCRIPTION
        The launcher needs its own copy of the exe and pck under
        versions/<version>/. On one volume a hardlink costs nothing and copies
        into a real file when the folder is moved to another machine, which is
        the whole point of a portable dist folder. Cross-volume or on a
        filesystem without links, it copies and says so.
    #>
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Destination)) -Force)
    try {
        [void](New-Item -ItemType HardLink -Path $Destination -Target $Source -ErrorAction Stop)
        return 'hardlink'
    } catch {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        return 'copy'
    }
}


function New-DistLauncherInstallRoot {
    <#
    .SYNOPSIS
        The install root the launcher actually reads, built from a finished
        bundle so the RC works with no network and no prior install.
    .DESCRIPTION
        The launcher has NO "browse to a game exe" control. It reads
        <root>/current.json, follows it to <root>/versions/<version>/, and
        verifies every file named in that folder's .openbfme-version.json by
        size and sha256 before it will enable Play. Handing a tester a launcher
        exe next to a game folder therefore does nothing at all - the layout IS
        the interface, and it is reproduced here exactly as
        tools/release/Test-LauncherHeadless.ps1 fabricates it.

        The two verified files are linked, not re-exported, so the RC is proved
        to be the same bytes as the bundle beside it rather than a second build
        that happens to have been made at the same time.
    #>
    param(
        [Parameter(Mandatory)][string]$BundleRoot,
        [Parameter(Mandatory)][string]$InstallRoot,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$Commit
    )
    if ($Commit -cnotmatch '^[0-9a-f]{40}$') {
        throw (New-DistRefusal -Problem "The launcher install state needs a full 40-character commit sha, got '$Commit'." -Remedy 'Pass the full commit, not the short one; the launcher validates the shape.')
    }
    $versionRoot = Join-Path $InstallRoot ("versions\$Version")
    if (Test-Path -LiteralPath $versionRoot) { Remove-Item -LiteralPath $versionRoot -Recurse -Force }
    [void](New-Item -ItemType Directory -Path $versionRoot -Force)

    $linkModes = New-Object 'System.Collections.Generic.List[string]'
    $files = New-Object 'System.Collections.Generic.List[object]'
    foreach ($name in @('OpenBFME.exe', 'OpenBFME.pck')) {
        $source = Join-Path $BundleRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw (New-DistRefusal -Problem "The bundle has no $name, so no launcher install root can be built from it." -Remedy 'Build the bundle first; this runs after it, not instead of it.')
        }
        $target = Join-Path $versionRoot $name
        $linkModes.Add((New-DistFileLink -Source $source -Destination $target))
        $files.Add([ordered]@{
            path   = $name
            size   = (Get-Item -LiteralPath $target).Length
            sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }

    $identity = [ordered]@{
        schema        = 'openbfme.installed-version'
        schemaVersion = 1
        version       = $Version
        commit        = $Commit
        # Not verified locally - it identifies the downloaded package, and this
        # install was never downloaded. Zeroed rather than faked with a hash of
        # something else, so nothing can mistake it for a real package digest.
        packageSha256 = ('0' * 64)
        files         = @($files.ToArray())
    }
    [IO.File]::WriteAllText(
        (Join-Path $versionRoot '.openbfme-version.json'),
        ($identity | ConvertTo-Json -Depth 5) + "`n",
        [Text.UTF8Encoding]::new($false))

    $state = [ordered]@{
        schema          = 'openbfme.install-state'
        currentVersion  = $Version
        previousVersion = $null
        commit          = $Commit
        previousCommit  = $null
        highestVersion  = $Version
        highestCommit   = $Commit
    }
    [IO.File]::WriteAllText(
        (Join-Path $InstallRoot 'current.json'),
        ($state | ConvertTo-Json) + "`n",
        [Text.UTF8Encoding]::new($false))

    return [pscustomobject]@{
        installRoot = $InstallRoot
        versionRoot = $versionRoot
        linkModes   = $linkModes.ToArray()
        files       = $files.ToArray()
    }
}
