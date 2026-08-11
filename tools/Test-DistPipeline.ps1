<#
.SYNOPSIS
    The cheap gate in front of the expensive one. Ten to fifteen seconds,
    measured, against ninety minutes for the thing it guards.

.DESCRIPTION
    tools/Publish-DistBuild.ps1 exports Godot, stages several gigabytes of
    content and boots the result twice. Discovering a drifted version string or
    a hole in the release firewall at the END of that is paying thirty minutes
    to learn something knowable in fifteen seconds, so it is knowable here.

    Two kinds of check, and the second kind is the one that matters:

    STATE  - this checkout is publishable: every file that states a version
             agrees with VERSION, the notes for it exist, dist/ is ignored and
             untracked.

    BEHAVIOUR - the guards actually refuse. Each is driven against a scratch
             repository built to be wrong in exactly one way. A firewall that
             has never been seen to refuse anything is a firewall nobody has
             tested; these cases fail if the guard is removed, which is the only
             property that makes them worth running.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Test-DistPipeline.ps1
#>
[CmdletBinding()]
param([string]$RepoRoot = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'dist-pipeline-common.ps1')
. (Join-Path $PSScriptRoot 'playable-bundle-common.ps1')

$script:Passed = 0
$script:Failed = New-Object 'System.Collections.Generic.List[string]'

function Test-Case {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][scriptblock]$Body
    )
    try {
        & $Body
        $script:Passed++
        Write-Host "  PASS  $Name" -ForegroundColor Green
    } catch {
        $script:Failed.Add("$Name : $($_.Exception.Message)")
        Write-Host "  FAIL  $Name" -ForegroundColor Red
        Write-Host "        $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Assert-True {
    param([Parameter(Mandatory)][bool]$Condition, [Parameter(Mandatory)][string]$Because)
    if (-not $Condition) { throw $Because }
}

function Assert-Throws {
    <#
    .SYNOPSIS
        The guard must refuse, and refuse for the stated reason - not merely
        blow up. A test that accepts any exception passes when the function is
        deleted and replaced with a typo.
    #>
    param(
        [Parameter(Mandatory)][scriptblock]$Body,
        [Parameter(Mandatory)][string]$Matching
    )
    $threw = $false
    $message = ''
    try { & $Body } catch { $threw = $true; $message = $_.Exception.Message }
    if (-not $threw) { throw "expected a refusal, got none" }
    if ($message -cnotmatch $Matching) { throw "refused, but not for the expected reason (wanted /$Matching/): $message" }
}

function New-ScratchRepository {
    <#
    .SYNOPSIS
        A throwaway git checkout shaped like this one, for driving the guards.
    #>
    param([string[]]$IgnoreLines = @('/dist/'))
    $root = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-dist-gate-" + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $root -Force)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git -C $root init --quiet 2>$null | Out-Null
        & git -C $root config user.email gate@example.invalid 2>$null | Out-Null
        & git -C $root config user.name 'dist gate' 2>$null | Out-Null
    } finally { $ErrorActionPreference = $previous }
    [IO.File]::WriteAllText((Join-Path $root '.gitignore'), (($IgnoreLines -join "`n") + "`n"))
    [IO.File]::WriteAllText((Join-Path $root 'VERSION'), "0.2.1`n")
    return $root
}

function Remove-ScratchRepository {
    param([Parameter(Mandatory)][string]$Root)
    if ($Root.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $Root)) {
        # git leaves read-only pack files behind; clear them or the delete fails.
        Get-ChildItem -LiteralPath $Root -Recurse -Force -File | ForEach-Object { $_.IsReadOnly = $false }
        Remove-Item -LiteralPath $Root -Recurse -Force
    }
}

if ($RepoRoot -eq '') { $RepoRoot = Get-DistRepoRoot -StartPath (Split-Path -Parent $PSScriptRoot) }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)

Write-Host ''
Write-Host "dist pipeline gate  ($RepoRoot)" -ForegroundColor White
Write-Host '---------------------------------------------------------------'

# ------------------------------------------------------------------- state
Write-Host ''
Write-Host 'This checkout is publishable'

Test-Case 'VERSION exists and parses' {
    $version = Read-DistVersionFile -RepoRoot $RepoRoot
    Assert-True ($version -ne '') 'VERSION produced an empty string'
}

Test-Case 'every file that states a version agrees with VERSION' {
    $problems = @(Test-DistVersionConsistency -RepoRoot $RepoRoot)
    Assert-True ($problems.Count -eq 0) ("version drift:`n        " + ($problems -join "`n        "))
}

Test-Case 'this version has patch notes' {
    $version = Read-DistVersionFile -RepoRoot $RepoRoot
    $notes = Get-DistPatchNotesPath -RepoRoot $RepoRoot -Version $version
    Assert-True (Test-Path -LiteralPath $notes -PathType Leaf) "missing $notes"
    $text = [IO.File]::ReadAllText($notes)
    Assert-True ($text.Length -gt 400) "patch notes are $($text.Length) bytes; that is a placeholder, not notes"
    Assert-True ($text -cmatch '(?im)^##\s+Known gaps') 'patch notes have no "Known gaps" section, so they only say what went right'
}

Test-Case 'dist is git-ignored in this checkout' {
    Assert-True (Test-DistPathIsIgnored -RepoRoot $RepoRoot -Path (Join-Path $RepoRoot 'dist')) 'git check-ignore says dist/ is NOT ignored'
}

Test-Case 'build is git-ignored in this checkout' {
    Assert-True (Test-DistPathIsIgnored -RepoRoot $RepoRoot -Path (Join-Path $RepoRoot 'build')) 'git check-ignore says build/ is NOT ignored'
}

Test-Case 'git tracks nothing under dist' {
    $tracked = @(Get-DistTrackedFiles -RepoRoot $RepoRoot -Path (Join-Path $RepoRoot 'dist'))
    Assert-True ($tracked.Count -eq 0) "git tracks $($tracked.Count) file(s) under dist/: $($tracked -join ', ')"
}

Test-Case 'the real firewall passes on this checkout' {
    [void](Assert-DistReleaseFirewall -RepoRoot $RepoRoot -DistRoot (Join-Path $RepoRoot 'dist'))
}

# --------------------------------------------------------------- behaviour
Test-Case 'the publisher preflights and binds against the build it wraps' {
    # Every argument the wrapper passes is checked against
    # Build-PlayableBundle.ps1's own declared parameters. The first live run
    # refused with "Cannot convert value 'v0.2.1' to type System.Int32" AFTER
    # preflight, because array splatting binds positionally; this is the second
    # in fifteen seconds rather than the same discovery at the end of an export.
    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot 'Publish-DistBuild.ps1') `
        -PreflightOnly -Rc -Godot 'C:\does-not-need-to-exist\godot.exe' `
        -AllowDirty -Force 2>&1 |
        ForEach-Object { $_.ToString() })
    $text = $output -join "`n"
    Assert-True ($LASTEXITCODE -eq 0) "preflight exited $LASTEXITCODE`n        $text"
    Assert-True ($text -cmatch 'bind against Build-PlayableBundle') "preflight did not report the binding check:`n        $text"
    Assert-True ($text -cmatch 'PREFLIGHT ONLY') "preflight did not stop before building:`n        $text"
    Assert-True ($text -cnotmatch 'REFUSED') "preflight refused:`n        $text"
}

Write-Host ''
Write-Host 'The guards refuse when they should'

Test-Case 'firewall refuses when git TRACKS a file under dist' {
    $scratch = New-ScratchRepository
    try {
        $dist = Join-Path $scratch 'dist'
        [void](New-Item -ItemType Directory -Path $dist -Force)
        [IO.File]::WriteAllText((Join-Path $dist 'leaked-pack.bin'), 'retail-derived bytes')
        # -f, exactly as the accident would: .gitignore does not protect a path
        # somebody has already forced into the index.
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & git -C $scratch add -f dist/leaked-pack.bin 2>$null | Out-Null } finally { $ErrorActionPreference = $previous }
        Assert-True (@(Get-DistTrackedFiles -RepoRoot $scratch -Path $dist).Count -gt 0) 'scratch setup failed: git did not track the file'
        Assert-Throws -Matching 'TRACKING' -Body {
            [void](Assert-DistReleaseFirewall -RepoRoot $scratch -DistRoot $dist)
        }
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'firewall refuses when dist is NOT ignored' {
    $scratch = New-ScratchRepository -IgnoreLines @('# nothing is ignored here')
    try {
        $dist = Join-Path $scratch 'dist'
        [void](New-Item -ItemType Directory -Path $dist -Force)
        Assert-Throws -Matching 'NOT git-ignored' -Body {
            [void](Assert-DistReleaseFirewall -RepoRoot $scratch -DistRoot $dist)
        }
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'a dist directory that does not exist YET is still known to be ignored' {
    # `/dist/` is a directory pattern, and git matches those only against paths
    # that are directories on disk. Asking about `dist` in a checkout that has
    # never published would answer "not ignored" - the firewall reading as down
    # while it is up. Found by this gate; the probe below is the fix.
    $scratch = New-ScratchRepository
    try {
        $dist = Join-Path $scratch 'dist'
        Assert-True (-not (Test-Path -LiteralPath $dist)) 'scratch setup failed: dist already exists'
        Assert-True (Test-DistPathIsIgnored -RepoRoot $scratch -Path $dist) 'an unpublished dist/ was reported as NOT ignored'
        [void](Assert-DistReleaseFirewall -RepoRoot $scratch -DistRoot $dist)
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'the firewall works when publishing FROM a worktree INTO the main checkout' {
    # How every publish actually runs, and it failed on the first live attempt:
    # git answers a question about a path outside its own working tree with an
    # error, which read as "dist is NOT ignored" and refused a publish that was
    # perfectly safe. The firewall must ask the checkout that owns the path.
    $scratch = New-ScratchRepository
    $worktree = ''
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & git -C $scratch add .gitignore VERSION 2>$null | Out-Null
            & git -C $scratch commit -q -m 'scratch base' 2>$null | Out-Null
            $worktree = Join-Path $scratch '.worktrees\lane'
            & git -C $scratch worktree add -q -b lane $worktree 2>$null | Out-Null
        } finally { $ErrorActionPreference = $previous }
        Assert-True (Test-Path -LiteralPath $worktree -PathType Container) 'scratch setup failed: no worktree was created'

        $dist = Join-Path $scratch 'dist'
        [void](New-Item -ItemType Directory -Path $dist -Force)
        Assert-True ((Get-DistOwningCheckout -Path $dist) -ceq ([IO.Path]::GetFullPath($scratch))) 'the dist root was attributed to the wrong checkout'
        # -RepoRoot is the worktree; -DistRoot is in the main checkout.
        [void](Assert-DistReleaseFirewall -RepoRoot $worktree -DistRoot $dist)
    } finally {
        if ($worktree -ne '' -and (Test-Path -LiteralPath $worktree)) {
            $previous = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try { & git -C $scratch worktree remove --force $worktree 2>$null | Out-Null } finally { $ErrorActionPreference = $previous }
        }
        Remove-ScratchRepository -Root $scratch
    }
}

Test-Case 'version consistency reports drift rather than tolerating it' {
    $scratch = New-ScratchRepository
    try {
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'game\data') -Force)
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'game\scenes') -Force)
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'docs\patch-notes') -Force)
        [IO.File]::WriteAllText((Join-Path $scratch 'game\project.godot'), "[application]`nconfig/version=`"0.9.9`"`n")
        [IO.File]::WriteAllText((Join-Path $scratch 'game\scenes\boot.tscn'), "text = `"v0.9.9`"`n")
        [IO.File]::WriteAllText((Join-Path $scratch 'game\data\build_info.json'), '{"version":"0.9.9","build":"1","commit":"abcdef1"}')
        $problems = @(Test-DistVersionConsistency -RepoRoot $scratch)
        Assert-True ($problems.Count -ge 4) "expected drift in project.godot, boot.tscn, build_info.json and the notes; got $($problems.Count): $($problems -join ' | ')"
        Assert-True (($problems -join ' ') -cmatch 'project\.godot') 'project.godot drift was not reported'
        Assert-True (($problems -join ' ') -cmatch 'boot\.tscn') 'boot.tscn drift was not reported'
        Assert-True (($problems -join ' ') -cmatch 'build_info\.json') 'build_info.json drift was not reported'
        Assert-True (($problems -join ' ') -cmatch 'patch notes') 'missing patch notes were not reported'
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'version consistency reports a build_info.json with no version at all' {
    $scratch = New-ScratchRepository
    try {
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'game\data') -Force)
        [IO.File]::WriteAllText((Join-Path $scratch 'game\data\build_info.json'), '{"build":"1","commit":"abcdef1"}')
        $problems = @(Test-DistVersionConsistency -RepoRoot $scratch)
        Assert-True (($problems -join ' ') -cmatch 'no version field') 'a build_info.json without a version was accepted'
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'VERSION refuses a value that is not a version' {
    $scratch = New-ScratchRepository
    try {
        [IO.File]::WriteAllText((Join-Path $scratch 'VERSION'), "beta-ish`n")
        Assert-Throws -Matching 'does not hold a version' -Body { [void](Read-DistVersionFile -RepoRoot $scratch) }
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'the release guard never blanket-ignores tracked source' {
    # The hazard: -ReleaseRoot <repo>\dist makes the PARENT the repository root.
    # Writing `*` there would hide the whole project from git and clobber the
    # real .gitignore doing it. The owner must fall back to dist itself.
    $scratch = New-ScratchRepository
    try {
        [IO.File]::WriteAllText((Join-Path $scratch 'tracked.txt'), 'source')
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & git -C $scratch add tracked.txt 2>$null | Out-Null } finally { $ErrorActionPreference = $previous }

        $dist = Join-Path $scratch 'dist'
        [void](New-Item -ItemType Directory -Path $dist -Force)
        Push-Location $scratch
        try {
            $owner = Get-BundleReleaseIgnoreOwner -OutputRoot $dist
            Assert-True ($owner -ceq ([IO.Path]::GetFullPath($dist).TrimEnd('\', '/'))) "owner should be dist itself, got '$owner'"

            $nested = Join-Path $dist 'bundle'
            [void](New-Item -ItemType Directory -Path $nested -Force)
            $nestedOwner = Get-BundleReleaseIgnoreOwner -OutputRoot $nested
            Assert-True ($nestedOwner -ceq ([IO.Path]::GetFullPath($dist).TrimEnd('\', '/'))) "owner for dist\bundle should be dist, got '$nestedOwner'"
        } finally { Pop-Location }

        Assert-Throws -Matching 'tracking' -Body { [void](Set-BundleReleaseDirectoryIgnored -ReleaseDirectory $scratch) }
        Assert-True (([IO.File]::ReadAllText((Join-Path $scratch '.gitignore'))) -cnotmatch '(?m)^\*$') 'the repository .gitignore was clobbered with a blanket rule'
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'Write-BuildInfo copies VERSION into build_info.json' {
    $out = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-buildinfo-" + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        & (Join-Path $PSScriptRoot 'Write-BuildInfo.ps1') -RepoRoot $RepoRoot -OutFile $out | Out-Null
        $written = (([IO.File]::ReadAllText($out)).TrimStart([char]0xFEFF) | ConvertFrom-Json)
        $expected = Read-DistVersionFile -RepoRoot $RepoRoot
        Assert-True ([string]$written.version -ceq $expected) "build_info.json says '$($written.version)', VERSION says '$expected'"
        Assert-True ([string]$written.build -match '^[0-9]+$') "build number is not a number: '$($written.build)'"
    } finally { if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force } }
}

Test-Case 'the version stamp records the build commit AND the commit the menu prints' {
    # These differ by one as a matter of structure: build_info.json is generated
    # and then committed, so it cannot contain the hash of the commit containing
    # it. A stamp that recorded only one of them would be wrong about the other.
    $scratch = New-ScratchRepository
    try {
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'game\data') -Force)
        [IO.File]::WriteAllText((Join-Path $scratch 'game\data\build_info.json'), '{"version":"0.2.1","build":"166","commit":"d5bc2d1"}')
        $destination = Join-Path $scratch 'out'
        [void](New-Item -ItemType Directory -Path $destination -Force)
        $stamp = New-DistVersionStamp -RepoRoot $scratch -Version '0.2.1' -Destination $destination `
            -IsReleaseCandidate $false -SourceCommit '0af8a80'
        Assert-True ($stamp.commit -ceq '0af8a80') "stamp commit is '$($stamp.commit)', not what the build came from"
        Assert-True ($stamp.buildInfoCommit -ceq 'd5bc2d1') "stamp buildInfoCommit is '$($stamp.buildInfoCommit)'"
        Assert-True ($stamp.build -ceq '166') "stamp build is '$($stamp.build)'"
        $written = (([IO.File]::ReadAllText((Join-Path $destination 'VERSION.json'))) | ConvertFrom-Json)
        Assert-True ($written.schema -ceq 'openbfme.dist-version') "VERSION.json schema is '$($written.schema)'"
        Assert-True ($written.redistributable -eq $false) 'VERSION.json must state that this folder is not redistributable'
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case '-FinishOnly stamps the BUNDLE commit, not HEAD' {
    # The shipped defect: -FinishOnly took the commit from `git rev-parse HEAD`
    # at preflight, so VERSION.json, current.json and .openbfme-version.json all
    # named a commit two later than the bytes in the folder, while
    # BUILD-INFO.json - written by the build - named the right one.
    # The existing install-root case cannot catch this: it is HANDED the commit.
    $scratch = New-ScratchRepository
    try {
        [IO.File]::WriteAllText((Join-Path $scratch 'a.txt'), 'one')
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & git -C $scratch add -A 2>$null | Out-Null
            & git -C $scratch commit -q -m 'first' 2>$null | Out-Null
            $bundleCommit = (& git -C $scratch rev-parse HEAD 2>$null | Out-String).Trim()
            [IO.File]::WriteAllText((Join-Path $scratch 'b.txt'), 'two')
            & git -C $scratch add -A 2>$null | Out-Null
            & git -C $scratch commit -q -m 'second' 2>$null | Out-Null
            $head = (& git -C $scratch rev-parse HEAD 2>$null | Out-String).Trim()
        } finally { $ErrorActionPreference = $previous }
        Assert-True ($bundleCommit -cne $head) 'scratch setup failed: HEAD did not move'

        $buildInfo = [pscustomobject]@{
            source = [pscustomobject]@{ commit = $bundleCommit; shortCommit = $bundleCommit.Substring(0, 7) }
        }
        $fromBundle = Resolve-DistSourceCommit -RepoRoot $scratch -FromBundle $true -BuildInfo $buildInfo
        Assert-True ($fromBundle.commit -ceq $bundleCommit) "-FinishOnly resolved '$($fromBundle.commit)', not the bundle's own commit"
        Assert-True ($fromBundle.origin -ceq 'bundle') "origin is '$($fromBundle.origin)'"

        $fromHead = Resolve-DistSourceCommit -RepoRoot $scratch -FromBundle $false
        Assert-True ($fromHead.commit -ceq $head) "the building path resolved '$($fromHead.commit)', not HEAD"
        Assert-True ($fromHead.origin -ceq 'head') "origin is '$($fromHead.origin)'"

        # And the stamp must carry through what was resolved, not re-ask git.
        $destination = Join-Path $scratch 'out'
        [void](New-Item -ItemType Directory -Path $destination -Force)
        [void](New-Item -ItemType Directory -Path (Join-Path $scratch 'game\data') -Force)
        [IO.File]::WriteAllText((Join-Path $scratch 'game\data\build_info.json'), '{"version":"0.2.1","build":"9","commit":"deadbee"}')
        $stamp = New-DistVersionStamp -RepoRoot $scratch -Version '0.2.1' -Destination $destination `
            -IsReleaseCandidate $false -SourceCommit $fromBundle.shortCommit
        Assert-True ($stamp.commit -ceq $bundleCommit.Substring(0, 7)) "VERSION.json would say '$($stamp.commit)'"

        # The drift distance must be measured from the BUNDLE, not from HEAD.
        Assert-True ((Get-DistCommitDistance -RepoRoot $scratch -From $bundleCommit -To $bundleCommit) -ceq '0') 'distance to itself is not 0'
        Assert-True ((Get-DistCommitDistance -RepoRoot $scratch -From $bundleCommit -To $head) -ceq '1') 'distance from the bundle to HEAD is not 1'
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case 'Resolve-DistSourceCommit refuses a bundle that cannot say what it is' {
    $scratch = New-ScratchRepository
    try {
        Assert-Throws -Matching 'no BUILD-INFO.json source record' -Body {
            [void](Resolve-DistSourceCommit -RepoRoot $scratch -FromBundle $true -BuildInfo $null)
        }
        Assert-Throws -Matching 'unusable commit' -Body {
            [void](Resolve-DistSourceCommit -RepoRoot $scratch -FromBundle $true `
                -BuildInfo ([pscustomobject]@{ source = [pscustomobject]@{ commit = 'abc'; shortCommit = 'abc' } }))
        }
    } finally { Remove-ScratchRepository -Root $scratch }
}

Test-Case '-PreflightOnly with -FinishOnly is refused, not silently ignored' {
    # It used to be ignored: the exit-0 block lives on the building path, so the
    # combination did the finishing work for real - a flag whose whole promise is
    # "changes nothing" writing the notes, the stamp and the launcher.
    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot 'Publish-DistBuild.ps1') `
        -PreflightOnly -FinishOnly 2>&1 | ForEach-Object { $_.ToString() })
    $text = $output -join "`n"
    Assert-True ($LASTEXITCODE -ne 0) "the combination exited 0:`n        $text"
    Assert-True ($text -cmatch 'contradict each other') "it did not refuse for the stated reason:`n        $text"
    Assert-True ($text -cnotmatch 'PREFLIGHT ONLY') 'it claimed to have preflighted'
}

Test-Case '-AllowFinishFromOtherCommit is refused without -FinishOnly' {
    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot 'Publish-DistBuild.ps1') `
        -AllowFinishFromOtherCommit 2>&1 | ForEach-Object { $_.ToString() })
    $text = $output -join "`n"
    Assert-True ($LASTEXITCODE -ne 0) "it exited 0:`n        $text"
    Assert-True ($text -cmatch 'no meaning without -FinishOnly') "wrong refusal:`n        $text"
}

Test-Case 'the launcher install root is the shape the launcher reads' {
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-installroot-" + [Guid]::NewGuid().ToString('N'))
    try {
        $bundle = Join-Path $scratch 'bundle'
        [void](New-Item -ItemType Directory -Path $bundle -Force)
        [IO.File]::WriteAllBytes((Join-Path $bundle 'OpenBFME.exe'), [byte[]]::new(70000))
        [IO.File]::WriteAllBytes((Join-Path $bundle 'OpenBFME.pck'), [byte[]](1, 2, 3))
        $installRoot = Join-Path $scratch 'install'
        $record = New-DistLauncherInstallRoot -BundleRoot $bundle -InstallRoot $installRoot `
            -Version '0.2.1' -Commit ('a' * 40)

        $state = (([IO.File]::ReadAllText((Join-Path $installRoot 'current.json'))) | ConvertFrom-Json)
        Assert-True ($state.schema -ceq 'openbfme.install-state') "current.json schema is '$($state.schema)'"
        Assert-True ($state.currentVersion -ceq '0.2.1') 'current.json does not point at this version'

        $identityPath = Join-Path $record.versionRoot '.openbfme-version.json'
        $identity = (([IO.File]::ReadAllText($identityPath)) | ConvertFrom-Json)
        Assert-True ($identity.schema -ceq 'openbfme.installed-version') "identity schema is '$($identity.schema)'"
        Assert-True (@($identity.files).Count -eq 2) 'the identity must name both OpenBFME.exe and OpenBFME.pck'
        foreach ($file in @($identity.files)) {
            $onDisk = Join-Path $record.versionRoot $file.path
            Assert-True (Test-Path -LiteralPath $onDisk -PathType Leaf) "identity names a file that is not there: $($file.path)"
            Assert-True ((Get-Item -LiteralPath $onDisk).Length -eq $file.size) "size recorded for $($file.path) does not match the file"
            $hash = (Get-FileHash -LiteralPath $onDisk -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-True ($hash -ceq $file.sha256) "sha256 recorded for $($file.path) does not match the file"
        }
    } finally {
        if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
    }
}

Test-Case 'the launcher install root refuses a short commit' {
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-installroot-" + [Guid]::NewGuid().ToString('N'))
    try {
        $bundle = Join-Path $scratch 'bundle'
        [void](New-Item -ItemType Directory -Path $bundle -Force)
        [IO.File]::WriteAllBytes((Join-Path $bundle 'OpenBFME.exe'), [byte[]]::new(16))
        [IO.File]::WriteAllBytes((Join-Path $bundle 'OpenBFME.pck'), [byte[]](1))
        Assert-Throws -Matching '40-character commit' -Body {
            [void](New-DistLauncherInstallRoot -BundleRoot $bundle -InstallRoot (Join-Path $scratch 'install') `
                -Version '0.2.1' -Commit 'abcdef1')
        }
    } finally {
        if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
    }
}

Write-Host ''
if ($script:Failed.Count -gt 0) {
    Write-Host "DIST PIPELINE GATE FAILED - $($script:Failed.Count) of $($script:Passed + $script:Failed.Count)" -ForegroundColor Red
    foreach ($failure in $script:Failed) { Write-Host "  $failure" -ForegroundColor Red }
    Write-Host ''
    exit 1
}
Write-Host "DIST PIPELINE GATE PASSED - $($script:Passed) checks" -ForegroundColor Green
Write-Host ''
exit 0
