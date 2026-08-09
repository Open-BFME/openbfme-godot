# Shared logic for the local playable-bundle tools.
#
#   tools/Build-PlayableBundle.ps1   produces a bundle
#   tools/Test-PlayableBundle.ps1    verifies one that already exists
#
# Both scripts dot-source this file so that "what the builder promised" and
# "what the verifier checks" can never drift apart: the staged-content check the
# builder runs after copying is literally the same function the verifier runs
# against a finished bundle.
#
# WHY THIS EXISTS AT ALL: OpenBFME-alpha01 was assembled by hand and carried no
# statement of what was inside it. When it turned out to have features a dev
# checkout did not, there was no way to tell whether the checkout had regressed
# or the bundle was simply built from somewhere else. A bundle that cannot
# identify itself manufactures exactly that confusion. Everything here exists to
# make a bundle self-describing and to refuse loudly rather than emit a
# plausible-looking half build.

Set-StrictMode -Version Latest

$script:BundleSchema = 'openbfme.playable-bundle'
$script:BundleSchemaVersion = 1
$script:SelectionSchema = 'openbfme.pack-selection'
$script:SelectionSchemaVersion = 0
$script:BundleInfoJson = 'BUILD-INFO.json'
$script:BundleInfoText = 'BUILD-INFO.txt'
$script:BundleLauncher = 'run-with-log.bat'
$script:BundleReadme = 'README.txt'
$script:BundleExe = 'OpenBFME.exe'
$script:BundlePck = 'OpenBFME.pck'
$script:BundleContentDir = 'content-packs'
$script:BundlePatchNotes = 'PATCH-NOTES.txt'
$script:BundleChangeLog = 'CHANGES.txt'
$script:BundleChecksums = 'SHA256SUMS.txt'
$script:BundleVerifyLauncher = 'verify-then-play.bat'

# WAR OF THE RING DATA, staged INTO the bundle.
#
# The runtime finds both of these by walking the pack roots it actually mounted
# and looking for these exact relative paths - game/src/wotr/wotr_session.gd
# (PACK_DOCUMENT_RELATIVE) and game/src/wotr/wotr_map_bundle.gd
# (PACK_BUNDLE_RELATIVE). That is why they are staged inside the active pack and
# not beside the exe: a directory beside the exe is reachable only through
# OPENBFME_LIVING_WORLD_DOC / OPENBFME_LIVING_MAP, and a bundle that needs
# environment variables to show its own content is the bug this staging fixes.
#
# THESE TWO CONSTANTS ARE NO LONGER THE LIST. They are the two paths this file's
# own verifier still checks by name, kept because BUILD-INFO.json schema 1
# records them by name. What a build must STAGE is computed in
# tools/wotr-data-staging.ps1 from the loaders themselves - see the comment at
# the top of that file for why a hardcoded list was the bug rather than the fix.
$script:WotrDocumentRelative = 'data/living-world.json'
$script:WotrMapRelative = 'data/living-map'
$script:WotrDocumentMaxBytes = 32 * 1024 * 1024   # wotr_session.gd DOCUMENT_MAX_BYTES
$script:WotrMapSchema = 'openbfme.living-map'

# <pack-id>/<bundle-hash>. Deliberately strict: a selection entry is an identity,
# not a path expression. Anything with a drive letter, a backslash, a "..", or a
# third segment is refused rather than normalized.
$script:PackRelativePattern = '^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$'


function Write-BundleStep {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "  $Message"
}

function Write-BundleHeading {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Write-BundleGood {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "  OK   $Message" -ForegroundColor Green
}

function Write-BundleWarn {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "  WARN $Message" -ForegroundColor Yellow
}

function New-BundleRefusal {
    # Every refusal reads the same way: what is wrong, then what to do about it.
    param(
        [Parameter(Mandatory)][string]$Problem,
        [string]$Remedy = ''
    )
    $text = "REFUSED: $Problem"
    if ($Remedy -ne '') { $text += "`n         Fix: $Remedy" }
    return $text
}

function Get-BundleRepoRoot {
    param([Parameter(Mandatory)][string]$StartPath)
    $current = [IO.Path]::GetFullPath($StartPath)
    while ($true) {
        if (Test-Path -LiteralPath (Join-Path $current 'game/project.godot') -PathType Leaf) { return $current }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $current) { break }
        $current = $parent
    }
    throw (New-BundleRefusal -Problem "No OpenBFME checkout found at or above $StartPath (game/project.godot is missing)." -Remedy 'Run this from inside the repository.')
}

function Get-BundleFileSha256 {
    param([Parameter(Mandatory)][string]$Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
        } finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
}

function Get-BundleTreeManifest {
    <#
    .SYNOPSIS
        Content identity for a directory tree.
    .DESCRIPTION
        Returns two independent identities:
          layoutSha256 - over sorted "size<TAB>relative-path" lines. Cheap; catches
                         a missing, added, renamed or resized file.
          sha256       - over sorted "filehash<TAB>size<TAB>relative-path" lines.
                         Catches a file whose bytes changed without changing size.
        Sorting is ordinal, and paths are normalized to forward slashes, so the
        identity is stable regardless of enumeration order or path separator.
    #>
    param(
        [Parameter(Mandatory)][string]$Root,
        [switch]$Quick,
        [string]$Label = ''
    )
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not [IO.Directory]::Exists($rootFull)) {
        throw (New-BundleRefusal -Problem "Directory does not exist: $rootFull")
    }
    $prefixLength = $rootFull.Length + 1

    $relatives = New-Object 'System.Collections.Generic.List[string]'
    $sizes = @{}
    foreach ($file in [IO.Directory]::EnumerateFiles($rootFull, '*', [IO.SearchOption]::AllDirectories)) {
        $relative = $file.Substring($prefixLength).Replace('\', '/')
        $relatives.Add($relative)
        $sizes[$relative] = (New-Object IO.FileInfo $file).Length
    }
    $ordered = $relatives.ToArray()
    [Array]::Sort($ordered, [StringComparer]::Ordinal)

    $layout = New-Object Text.StringBuilder
    $deep = New-Object Text.StringBuilder
    $entries = [ordered]@{}
    $totalBytes = [long]0
    $index = 0
    foreach ($relative in $ordered) {
        $size = [long]$sizes[$relative]
        $totalBytes += $size
        [void]$layout.Append("$size`t$relative`n")
        $hash = '-'
        if (-not $Quick) {
            $hash = Get-BundleFileSha256 -Path (Join-Path $rootFull ($relative -replace '/', '\'))
            [void]$deep.Append("$hash`t$size`t$relative`n")
        }
        $entries[$relative] = [pscustomobject]@{ bytes = $size; sha256 = $hash }
        $index++
        if ($Label -ne '' -and ($index % 4000) -eq 0) {
            Write-BundleStep "$Label ... $index / $($ordered.Length) files"
        }
    }

    $encoding = [Text.UTF8Encoding]::new($false)
    $layoutHash = [BitConverter]::ToString(
        [Security.Cryptography.SHA256]::Create().ComputeHash($encoding.GetBytes($layout.ToString()))
    ).Replace('-', '').ToLowerInvariant()
    $deepHash = ''
    if (-not $Quick) {
        $deepHash = [BitConverter]::ToString(
            [Security.Cryptography.SHA256]::Create().ComputeHash($encoding.GetBytes($deep.ToString()))
        ).Replace('-', '').ToLowerInvariant()
    }

    return [pscustomobject]@{
        root         = $rootFull
        files        = $ordered.Length
        bytes        = $totalBytes
        layoutSha256 = $layoutHash
        sha256       = $deepHash
        quick        = [bool]$Quick
        entries      = $entries
    }
}

function Compare-BundleTreeManifest {
    <#
    .SYNOPSIS
        Names the individual files that differ between two trees.
    .DESCRIPTION
        A hash mismatch alone ("expected abc, got def") tells the owner nothing
        actionable. This reports the actual added / missing / changed paths, which
        is the difference between "the bundle is broken somehow" and "assets/foo.res
        is missing".
    #>
    param(
        [Parameter(Mandatory)]$Expected,
        [Parameter(Mandatory)]$Actual,
        [int]$MaxReported = 12
    )
    $problems = New-Object 'System.Collections.Generic.List[string]'
    $expectedKeys = @($Expected.entries.Keys)
    $actualKeys = @($Actual.entries.Keys)
    $missing = @($expectedKeys | Where-Object { -not $Actual.entries.Contains($_) })
    $added = @($actualKeys | Where-Object { -not $Expected.entries.Contains($_) })
    foreach ($path in ($missing | Select-Object -First $MaxReported)) {
        $problems.Add("missing file: $path")
    }
    if ($missing.Count -gt $MaxReported) { $problems.Add("... and $($missing.Count - $MaxReported) more missing files") }
    foreach ($path in ($added | Select-Object -First $MaxReported)) {
        $problems.Add("unexpected extra file: $path")
    }
    if ($added.Count -gt $MaxReported) { $problems.Add("... and $($added.Count - $MaxReported) more unexpected files") }

    $changed = New-Object 'System.Collections.Generic.List[string]'
    foreach ($path in $expectedKeys) {
        if (-not $Actual.entries.Contains($path)) { continue }
        $left = $Expected.entries[$path]
        $right = $Actual.entries[$path]
        if ($left.bytes -ne $right.bytes) {
            $changed.Add("size changed: $path ($($left.bytes) -> $($right.bytes) bytes)")
            continue
        }
        if ($left.sha256 -ne '-' -and $right.sha256 -ne '-' -and $left.sha256 -ne $right.sha256) {
            $changed.Add("content changed: $path")
        }
    }
    foreach ($item in ($changed | Select-Object -First $MaxReported)) { $problems.Add($item) }
    if ($changed.Count -gt $MaxReported) { $problems.Add("... and $($changed.Count - $MaxReported) more changed files") }
    return $problems.ToArray()
}

function Read-BundlePackSelection {
    <#
    .SYNOPSIS
        Parse and validate a content-packs/selection.json.
    .DESCRIPTION
        Fails closed on every ambiguity. The runtime's own loader silently skips
        an invalid supplemental entry ("diagnosed and skipped, never searched
        for"); that is the right call for a running game and the wrong call for a
        bundler, because a skipped supplement becomes a faction that quietly is
        not in the build. Here every named pack must resolve or nothing ships.
    #>
    param([Parameter(Mandatory)][string]$ContentRoot)

    $selectionPath = Join-Path $ContentRoot 'selection.json'
    if (-not (Test-Path -LiteralPath $selectionPath -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "No selection.json in the content root: $ContentRoot" -Remedy 'Point -ContentRoot at a pack cache that has selection.json, or publish a selection first.')
    }
    $raw = [IO.File]::ReadAllText($selectionPath)
    try { $config = $raw | ConvertFrom-Json } catch {
        throw (New-BundleRefusal -Problem "selection.json is not valid JSON: $selectionPath" -Remedy 'Repair the file; do not hand-edit it while the game is running.')
    }
    if ($null -eq $config -or $config -isnot [psobject]) {
        throw (New-BundleRefusal -Problem "selection.json is not a JSON object: $selectionPath")
    }
    $names = @($config.PSObject.Properties.Name)
    foreach ($required in @('schema', 'schemaVersion', 'activePack')) {
        if ($names -notcontains $required) {
            throw (New-BundleRefusal -Problem "selection.json has no '$required' field: $selectionPath")
        }
    }
    if ([string]$config.schema -cne $script:SelectionSchema) {
        throw (New-BundleRefusal -Problem "selection.json declares schema '$($config.schema)', expected '$($script:SelectionSchema)'.")
    }
    if ([int]$config.schemaVersion -ne $script:SelectionSchemaVersion) {
        throw (New-BundleRefusal -Problem "selection.json declares schemaVersion $($config.schemaVersion), expected $($script:SelectionSchemaVersion).")
    }
    $active = [string]$config.activePack
    if ($active -cnotmatch $script:PackRelativePattern) {
        throw (New-BundleRefusal -Problem "selection.json activePack is not a safe '<pack-id>/<bundle-hash>' identity: '$active'." -Remedy 'A selection entry must be exactly two path segments with no drive, no backslash and no "..".')
    }
    $supplements = New-Object 'System.Collections.Generic.List[string]'
    if ($names -contains 'supplementalPacks' -and $null -ne $config.supplementalPacks) {
        foreach ($entry in @($config.supplementalPacks)) {
            $relative = [string]$entry
            if ($relative -cnotmatch $script:PackRelativePattern) {
                throw (New-BundleRefusal -Problem "selection.json supplementalPacks contains an unsafe entry: '$relative'." -Remedy 'A selection entry must be exactly two path segments with no drive, no backslash and no "..".')
            }
            if ($relative -ceq $active) {
                throw (New-BundleRefusal -Problem "selection.json lists '$relative' as both the active pack and a supplement.")
            }
            if ($supplements.Contains($relative)) {
                throw (New-BundleRefusal -Problem "selection.json lists supplemental pack '$relative' more than once.")
            }
            $supplements.Add($relative)
        }
    }
    return [pscustomobject]@{
        path              = [IO.Path]::GetFullPath($selectionPath)
        rawText           = $raw
        sha256            = Get-BundleFileSha256 -Path $selectionPath
        activePack        = $active
        supplementalPacks = $supplements.ToArray()
        allPacks          = @(@($active) + $supplements.ToArray())
    }
}

function Read-BundlePackMetadata {
    <#
    .SYNOPSIS
        Validate that <root>/<pack-id>/<bundle-hash> really is that pack.
    #>
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Relative,
        [string]$Origin = 'selection.json'
    )
    $packRoot = Join-Path $Root ($Relative -replace '/', '\')
    if (-not (Test-Path -LiteralPath $packRoot -PathType Container)) {
        throw (New-BundleRefusal -Problem "$Origin names a content pack that is not present: $Relative" -Remedy "Expected directory: $packRoot")
    }
    $packJson = Join-Path $packRoot 'pack.json'
    if (-not (Test-Path -LiteralPath $packJson -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "Content pack '$Relative' has no pack.json, so it is not a valid pack root." -Remedy "Expected file: $packJson")
    }
    try { $meta = [IO.File]::ReadAllText($packJson) | ConvertFrom-Json } catch {
        throw (New-BundleRefusal -Problem "Content pack '$Relative' has an unreadable pack.json.")
    }
    $metaNames = @($meta.PSObject.Properties.Name)
    if ($metaNames -notcontains 'id' -or [string]::IsNullOrWhiteSpace([string]$meta.id)) {
        throw (New-BundleRefusal -Problem "Content pack '$Relative' has a pack.json with no id.")
    }
    $declaredId = ([string]$meta.id).Trim()
    $directoryId = $Relative.Split('/')[0]
    if ($declaredId -cne $directoryId) {
        # A pack tree copied under the wrong id would load, then resolve assets
        # from the wrong bundle. Refuse rather than ship a mislabelled pack.
        throw (New-BundleRefusal -Problem "Content pack directory '$directoryId' contains a pack that declares id '$declaredId'." -Remedy 'The directory name and pack.json id must agree.')
    }
    $version = ''
    if ($metaNames -contains 'version') { $version = [string]$meta.version }
    $title = ''
    if ($metaNames -contains 'title') { $title = [string]$meta.title }
    $redistributable = $false
    if ($metaNames -contains 'redistributable') { $redistributable = [bool]$meta.redistributable }
    if ($metaNames -contains 'dataPolicy' -and $null -ne $meta.dataPolicy) {
        $policyNames = @($meta.dataPolicy.PSObject.Properties.Name)
        if ($policyNames -contains 'redistributable') { $redistributable = [bool]$meta.dataPolicy.redistributable }
    }
    return [pscustomobject]@{
        relative        = $Relative
        path            = [IO.Path]::GetFullPath($packRoot)
        id              = $declaredId
        bundleHash      = $Relative.Split('/')[1]
        version         = $version
        title           = $title
        redistributable = $redistributable
    }
}

function Test-BundlePathIsGitIgnored {
    <#
    .SYNOPSIS
        Prove (not assume) that a destination is outside git's reach.
    .DESCRIPTION
        The bundle carries retail-derived content. .gitignore currently ignores
        /dist/, but this asks git rather than trusting that, because a future
        edit to .gitignore that quietly un-ignores dist/ would otherwise turn
        every subsequent build into a redistribution incident.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$Path
    )
    # Ask about a representative FILE inside the destination, not the directory
    # itself. A pattern like "/dist/" only matches a path git can see is a
    # directory, and the destination usually does not exist yet on a first build;
    # asking about "<dest>/<bundle>/BUILD-INFO.json" answers the question that
    # actually matters - will the files we are about to write be ignored.
    $full = [IO.Path]::GetFullPath($Path)
    $probe = Join-Path (Join-Path $full 'openbfme-bundle-probe') 'BUILD-INFO.json'

    # Which repository's rules apply is decided by where the destination is, not
    # by which checkout we happen to be building. A destination outside this
    # checkout can still sit inside ANOTHER git repository, and "not mine" is not
    # the same as "safe" when the payload is retail-derived content.
    $owner = $RepoRoot
    $repoPrefix = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $existing = $full
        while ($existing -ne '' -and -not [IO.Directory]::Exists($existing)) {
            $existing = [IO.Path]::GetDirectoryName($existing)
        }
        if ($existing -eq '' -or $null -eq $existing) { return $true }
        Push-Location $existing
        try {
            $top = (& git rev-parse --show-toplevel 2>$null | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $top -eq '') { return $true }  # not inside any repository
            $owner = $top
        } finally { Pop-Location }
    }

    Push-Location $owner
    try {
        & git check-ignore --quiet -- $probe 2>$null
        return ($LASTEXITCODE -eq 0)
    } finally { Pop-Location }
}

function Get-BundleSourceIdentity {
    param([Parameter(Mandatory)][string]$RepoRoot)
    Push-Location $RepoRoot
    try {
        $commit = (& git rev-parse HEAD 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $commit -eq '') {
            throw (New-BundleRefusal -Problem "Cannot read the git commit for $RepoRoot." -Remedy 'A bundle that cannot state which commit it came from is the exact failure this tool exists to prevent.')
        }
        $branch = (& git rev-parse --abbrev-ref HEAD 2>$null | Out-String).Trim()
        $containingBranches = @()
        if ($branch -ceq 'HEAD') {
            # A detached checkout would otherwise stamp the bundle "branch: HEAD",
            # which answers nothing. "Which branch is this build from" is the
            # question that started this whole exercise, so name the branches this
            # commit actually sits on.
            $containingBranches = @(
                & git branch --all --contains HEAD --format='%(refname:short)' 2>$null |
                    ForEach-Object { $_.Trim() } |
                    Where-Object { $_ -ne '' -and $_ -notlike '*HEAD detached*' } |
                    Select-Object -First 8
            )
            if ($containingBranches.Count -gt 0) {
                $branch = "detached at $($containingBranches[0])"
            } else {
                $branch = 'detached (no branch contains this commit)'
            }
        }
        $commitDate = (& git log -1 --format=%cI 2>$null | Out-String).Trim()
        $subject = (& git log -1 --format=%s 2>$null | Out-String).Trim()
        $status = @(& git status --porcelain 2>$null | Where-Object { $_ -ne '' })
        $describe = (& git describe --tags --always --dirty 2>$null | Out-String).Trim()
        return [pscustomobject]@{
            commit       = $commit
            shortCommit  = $commit.Substring(0, 10)
            branch       = $branch
            branchesContaining = $containingBranches
            commitDate   = $commitDate
            commitSubject = $subject
            describe     = $describe
            worktree     = $RepoRoot
            dirty        = ($status.Count -gt 0)
            dirtyFileCount = $status.Count
            dirtyFiles   = @($status | Select-Object -First 50)
        }
    } finally { Pop-Location }
}

function Get-BundleGodotIdentity {
    param(
        [Parameter(Mandatory)][string]$GodotExe,
        [Parameter(Mandatory)][string]$Preset
    )
    if (-not (Test-Path -LiteralPath $GodotExe -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "Godot executable not found: $GodotExe" -Remedy 'Pass -Godot <path>, or set OPENBFME_GODOT.')
    }
    $version = (& $GodotExe --version 2>&1 | Out-String).Trim() -split "`n" | Select-Object -Last 1
    $version = $version.Trim()
    if ($version -eq '') {
        throw (New-BundleRefusal -Problem "'$GodotExe --version' printed nothing; this is not a usable Godot binary.")
    }
    # "4.7.stable.official.5b4e0cb0f" -> template directory "4.7.stable"
    if ($version -cnotmatch '^(?<line>[0-9]+\.[0-9]+(?:\.[0-9]+)?)\.(?<status>[A-Za-z0-9]+)') {
        throw (New-BundleRefusal -Problem "Cannot parse the Godot version string: '$version'.")
    }
    $templateDirName = "$($Matches['line']).$($Matches['status'])"
    $templateRoot = Join-Path $env:APPDATA "Godot\export_templates\$templateDirName"
    $templateFile = Join-Path $templateRoot 'windows_release_x86_64.exe'
    if (-not (Test-Path -LiteralPath $templateFile -PathType Leaf)) {
        $downloadName = "Godot_v$($Matches['line'])-$($Matches['status'])_export_templates.tpz"
        throw (New-BundleRefusal -Problem "Godot $version export templates are not installed (missing $templateFile)." -Remedy "Open Godot -> Editor -> Manage Export Templates -> Download and Install, or unpack $downloadName into $templateRoot.")
    }
    $templateVersion = ''
    $versionTxt = Join-Path $templateRoot 'version.txt'
    if (Test-Path -LiteralPath $versionTxt -PathType Leaf) {
        $templateVersion = ([IO.File]::ReadAllText($versionTxt)).Trim()
    }
    if ($templateVersion -ne '' -and $templateVersion -cne $templateDirName) {
        throw (New-BundleRefusal -Problem "Export templates in $templateRoot declare version '$templateVersion' but Godot is '$version'." -Remedy 'Reinstall export templates matching this Godot build.')
    }
    return [pscustomobject]@{
        executable              = [IO.Path]::GetFullPath($GodotExe)
        version                 = $version
        preset                  = $Preset
        exportTemplatesRoot     = $templateRoot
        exportTemplatesVersion  = $templateVersion
        windowsReleaseTemplate  = $templateFile
        windowsReleaseTemplateSha256 = ''
    }
}

function Invoke-BundleLaunchProbe {
    <#
    .SYNOPSIS
        Actually start the exported game, headless, and see what content it loads.
    .DESCRIPTION
        This is the only check that answers the question that matters: does this
        bundle boot, and does it boot on ITS OWN content? Everything else is file
        accounting. The probe reads the '[ContentDB] packs=... factions=...'
        census the boot path prints, because a build that loads the wrong pack
        still boots happily and looks fine.
    #>
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string]$LogPath,
        [string]$ContentRoot = '',
        [int]$TimeoutSeconds = 240,
        [int]$QuitAfterFrames = 600
    )
    $workingDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Executable))
    $errorLog = "$LogPath.err"
    foreach ($stale in @($LogPath, $errorLog)) {
        if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force }
    }
    # Start-Process -PassThru does not reliably surface ExitCode with redirected
    # streams, and a blank exit code in a refusal ("exited with code ") is exactly
    # the sort of vague failure this tool exists to eliminate. Drive the process
    # directly, and set OPENBFME_CONTENT on the CHILD only, so a probe can never
    # leak content selection into the rest of the build.
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.Arguments = "--headless --quit-after $QuitAfterFrames"
    $startInfo.WorkingDirectory = $workingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($startInfo.EnvironmentVariables.ContainsKey('OPENBFME_CONTENT')) {
        [void]$startInfo.EnvironmentVariables.Remove('OPENBFME_CONTENT')
    }
    if ($ContentRoot -ne '') { $startInfo.EnvironmentVariables['OPENBFME_CONTENT'] = $ContentRoot }

    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($startInfo)
    # Read both streams asynchronously before waiting: a full pipe buffer on
    # either one deadlocks the child, which would look exactly like a hang.
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $exited = $process.WaitForExit($TimeoutSeconds * 1000)
    $timedOut = (-not $exited)
    if ($timedOut) { try { $process.Kill() } catch { } }
    $process.WaitForExit()
    $timer.Stop()
    $exitCode = $process.ExitCode

    $stdoutText = $stdoutTask.Result
    $stderrText = $stderrTask.Result
    Write-BundleTextFile -Path $LogPath -Content $stdoutText
    Write-BundleTextFile -Path $errorLog -Content $stderrText

    if ($timedOut) {
        return [pscustomobject]@{
            performed = $true; timedOut = $true; exitCode = $exitCode
            seconds = [math]::Round($timer.Elapsed.TotalSeconds, 1)
            contentDb = ''; contentRoot = $ContentRoot; logPath = $LogPath
            errorLines = @("the process did not exit within $TimeoutSeconds seconds and was killed")
            warningLines = @()
        }
    }

    $lines = @(($stdoutText + "`n" + $stderrText) -split "`r?`n")
    $contentDb = @($lines | Where-Object { $_ -like '*[ContentDB]*packs=*' } | Select-Object -First 1)
    $censusLine = ''
    if ($contentDb.Count -gt 0) { $censusLine = $contentDb[0].Trim() }
    $errorLines = @($lines | Where-Object { $_ -cmatch '^(ERROR|SCRIPT ERROR|USER ERROR|FATAL):' } | Select-Object -First 20)
    $warningLines = @($lines | Where-Object { $_ -cmatch '^(WARNING|USER WARNING):' } | Select-Object -First 20)
    return [pscustomobject]@{
        performed = $true; timedOut = $false; exitCode = $exitCode
        seconds = [math]::Round($timer.Elapsed.TotalSeconds, 1)
        contentDb = $censusLine; contentRoot = $ContentRoot; logPath = $LogPath
        errorLines = $errorLines; warningLines = $warningLines
    }
}

function ConvertTo-BundleJsonText {
    param([Parameter(Mandatory)]$Value)
    return ($Value | ConvertTo-Json -Depth 12)
}

function Write-BundleTextFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [AllowEmptyString()][string]$Content = ''
    )
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function Read-BundleInfo {
    param([Parameter(Mandatory)][string]$BundleRoot)
    $path = Join-Path $BundleRoot $script:BundleInfoJson
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "This directory has no $($script:BundleInfoJson), so it cannot state what it contains: $BundleRoot" -Remedy 'Only bundles produced by tools/Build-PlayableBundle.ps1 can be verified. OpenBFME-alpha01 was hand-assembled and cannot.')
    }
    try { $info = [IO.File]::ReadAllText($path) | ConvertFrom-Json } catch {
        throw (New-BundleRefusal -Problem "$($script:BundleInfoJson) is not valid JSON: $path")
    }
    if ([string]$info.schema -cne $script:BundleSchema) {
        throw (New-BundleRefusal -Problem "$($script:BundleInfoJson) declares schema '$($info.schema)', expected '$($script:BundleSchema)'.")
    }
    if ([int]$info.schemaVersion -ne $script:BundleSchemaVersion) {
        throw (New-BundleRefusal -Problem "$($script:BundleInfoJson) declares schemaVersion $($info.schemaVersion), expected $($script:BundleSchemaVersion).")
    }
    return $info
}

function Test-BundleTree {
    <#
    .SYNOPSIS
        Check a finished bundle against its own BUILD-INFO.json.
    .OUTPUTS
        An array of human-readable problems. Empty means the bundle is intact.
    #>
    param(
        [Parameter(Mandatory)][string]$BundleRoot,
        [switch]$Quick
    )
    $problems = New-Object 'System.Collections.Generic.List[string]'
    $info = Read-BundleInfo -BundleRoot $BundleRoot

    # 1. Layout: the files the alpha01 README says a working bundle must have.
    foreach ($required in @($script:BundleExe, $script:BundlePck, $script:BundleReadme, $script:BundleLauncher, $script:BundleInfoJson, $script:BundleInfoText)) {
        if (-not (Test-Path -LiteralPath (Join-Path $BundleRoot $required) -PathType Leaf)) {
            $problems.Add("required file is missing: $required")
        }
    }
    # Release notes are required only of bundles that claim to have them, so a
    # bundle built before the notes existed still verifies as what it is.
    $infoNames = @($info.PSObject.Properties.Name)
    if ($infoNames -contains 'releaseNotes' -and $null -ne $info.releaseNotes) {
        foreach ($required in @($script:BundlePatchNotes, $script:BundleChangeLog)) {
            if (-not (Test-Path -LiteralPath (Join-Path $BundleRoot $required) -PathType Leaf)) {
                $problems.Add("BUILD-INFO.json says this bundle carries release notes, but $required is missing")
            }
        }
    }
    $contentRoot = Join-Path $BundleRoot $script:BundleContentDir
    if (-not (Test-Path -LiteralPath $contentRoot -PathType Container)) {
        $problems.Add("required directory is missing: $($script:BundleContentDir)/ (it must sit beside the exe)")
        return $problems.ToArray()
    }

    # War of the Ring data, if this bundle claims to carry it. A bundle that says
    # War of the Ring works and does not carry the files would ship a button that
    # reads (UNAVAILABLE) while its own provenance says otherwise - which is the
    # defect this staging exists to fix, wearing a disguise.
    if ($infoNames -contains 'warOfTheRing' -and $null -ne $info.warOfTheRing -and [bool]$info.warOfTheRing.staged) {
        $wotrPackRoot = Join-Path $contentRoot (([string]$info.warOfTheRing.packRelative) -replace '/', '\')
        $wotrDocument = Join-Path $wotrPackRoot ($script:WotrDocumentRelative -replace '/', '\')
        $wotrMapManifest = Join-Path (Join-Path $wotrPackRoot ($script:WotrMapRelative -replace '/', '\')) 'manifest.json'
        if (-not (Test-Path -LiteralPath $wotrDocument -PathType Leaf)) {
            $problems.Add("BUILD-INFO.json says War of the Ring is playable, but its living-world document is missing: content-packs/$($info.warOfTheRing.packRelative)/$($script:WotrDocumentRelative)")
        }
        if (-not (Test-Path -LiteralPath $wotrMapManifest -PathType Leaf)) {
            $problems.Add("BUILD-INFO.json says War of the Ring is playable, but its 3D map bundle has no manifest: content-packs/$($info.warOfTheRing.packRelative)/$($script:WotrMapRelative)/manifest.json")
        }
    }
    # EVERY converted bundle this build claims to have staged, re-checked at the
    # path the loader that needs it actually opens. The two lines above cover the
    # two artefacts BUILD-INFO schema 1 names directly; this covers the rest, and
    # it is the check that would have caught four bundles going missing at once.
    if ($infoNames -contains 'warOfTheRing' -and $null -ne $info.warOfTheRing) {
        $wotrNames = @($info.warOfTheRing.PSObject.Properties.Name)
        if ($wotrNames -contains 'bundles' -and $null -ne $info.warOfTheRing.bundles) {
            $wotrPackRoot = Join-Path $contentRoot (([string]$info.warOfTheRing.packRelative) -replace '/', '\')
            foreach ($declared in @($info.warOfTheRing.bundles)) {
                if (-not [bool]$declared.staged) { continue }
                $relative = [string]$declared.packRelative
                if ($relative -eq '') {
                    $problems.Add("BUILD-INFO.json says $([string]$declared.env) was staged but records no path for it")
                    continue
                }
                $landed = Join-Path $wotrPackRoot ($relative -replace '/', '\')
                if (-not (Test-Path -LiteralPath $landed -PathType Leaf)) {
                    $problems.Add("BUILD-INFO.json says $([string]$declared.env) was staged, and the file the loader opens is not there: content-packs/$($info.warOfTheRing.packRelative)/$relative (without it the player loses $([string]$declared.losesWithout))")
                }
            }
        }
    }

    # 2. The exe and pck are the ones this bundle claims.
    foreach ($artifact in @(
        @{ name = $script:BundleExe; declared = $info.artifacts.exe },
        @{ name = $script:BundlePck; declared = $info.artifacts.pck }
    )) {
        $path = Join-Path $BundleRoot $artifact.name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $size = (New-Object IO.FileInfo $path).Length
        if ($size -ne [long]$artifact.declared.bytes) {
            $problems.Add("$($artifact.name) is $size bytes, BUILD-INFO.json says $($artifact.declared.bytes)")
            continue
        }
        if (-not $Quick) {
            $hash = Get-BundleFileSha256 -Path $path
            if ($hash -cne [string]$artifact.declared.sha256) {
                $problems.Add("$($artifact.name) sha256 is $hash, BUILD-INFO.json says $($artifact.declared.sha256)")
            }
        }
    }

    # 3. selection.json is byte-identical to the one that was staged, and the
    #    packs it names are exactly the packs BUILD-INFO.json says were staged.
    $selectionPath = Join-Path $contentRoot 'selection.json'
    if (-not (Test-Path -LiteralPath $selectionPath -PathType Leaf)) {
        $problems.Add("content-packs/selection.json is missing")
    } else {
        $hash = Get-BundleFileSha256 -Path $selectionPath
        if ($hash -cne [string]$info.content.selection.sha256) {
            $problems.Add("content-packs/selection.json has been modified since the build (sha256 $hash, expected $($info.content.selection.sha256))")
        }
        try {
            $selection = Read-BundlePackSelection -ContentRoot $contentRoot
            $declared = @($info.content.packs | ForEach-Object { [string]$_.relative })
            foreach ($relative in $selection.allPacks) {
                if ($declared -cnotcontains $relative) {
                    $problems.Add("selection.json names '$relative', which BUILD-INFO.json does not list as staged")
                }
            }
            foreach ($relative in $declared) {
                if ($selection.allPacks -cnotcontains $relative) {
                    $problems.Add("BUILD-INFO.json lists staged pack '$relative', which selection.json does not name")
                }
            }
        } catch {
            $problems.Add("content-packs/selection.json is unusable: $($_.Exception.Message -replace "`n", ' ')")
        }
    }

    # 4. Every staged pack is present, correctly labelled, and byte-intact.
    foreach ($declared in @($info.content.packs)) {
        $relative = [string]$declared.relative
        try {
            [void](Read-BundlePackMetadata -Root $contentRoot -Relative $relative -Origin 'BUILD-INFO.json')
        } catch {
            $problems.Add(($_.Exception.Message -replace "`n", ' '))
            continue
        }
        $packRoot = Join-Path $contentRoot ($relative -replace '/', '\')
        $label = ''
        if (-not $Quick) { $label = "hashing $relative" }
        $manifest = Get-BundleTreeManifest -Root $packRoot -Quick:$Quick -Label $label
        if ($manifest.files -ne [int]$declared.files) {
            $problems.Add("pack '$relative' holds $($manifest.files) files, BUILD-INFO.json says $($declared.files)")
        }
        if ($manifest.bytes -ne [long]$declared.bytes) {
            $problems.Add("pack '$relative' holds $($manifest.bytes) bytes, BUILD-INFO.json says $($declared.bytes)")
        }
        if ($manifest.layoutSha256 -cne [string]$declared.layoutSha256) {
            $problems.Add("pack '$relative' layout does not match BUILD-INFO.json (a file was added, removed, renamed or resized)")
        }
        if (-not $Quick -and $manifest.sha256 -cne [string]$declared.sha256) {
            $problems.Add("pack '$relative' content hash does not match BUILD-INFO.json (a file's bytes changed)")
        }
    }

    # 5. Nothing extra. An unexpected pack directory beside the staged ones is
    #    how a stale faction sneaks into a build and overrides the real one.
    $declaredIds = @(@($info.content.packs) | ForEach-Object { ([string]$_.relative).Split('/')[0] })
    foreach ($directory in @(Get-ChildItem -LiteralPath $contentRoot -Directory)) {
        if ($declaredIds -cnotcontains $directory.Name) {
            $problems.Add("unexpected pack directory in the bundle: content-packs/$($directory.Name)")
        }
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $contentRoot -File)) {
        if ($file.Name -cne 'selection.json') {
            $problems.Add("unexpected file in the bundle content root: content-packs/$($file.Name)")
        }
    }

    return $problems.ToArray()
}

function New-BundleChecksumManifest {
    <#
    .SYNOPSIS
        Write SHA256SUMS.txt at the root of a bundle, covering every file in it.
    .DESCRIPTION
        Standard sha256sum format - '<hash><two spaces><relative path>' with
        forward slashes and ordinal-sorted paths - so `sha256sum -c SHA256SUMS.txt`
        on any machine, and verify-then-play.bat on the tester's, both agree with
        it. The manifest cannot cover itself, so it is the one file excluded.
    .OUTPUTS
        The number of files listed.
    #>
    param(
        [Parameter(Mandatory)][string]$Root,
        [string]$Label = ''
    )
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not [IO.Directory]::Exists($rootFull)) {
        throw (New-BundleRefusal -Problem "Directory does not exist: $rootFull")
    }
    $prefixLength = $rootFull.Length + 1
    $relatives = New-Object 'System.Collections.Generic.List[string]'
    foreach ($file in [IO.Directory]::EnumerateFiles($rootFull, '*', [IO.SearchOption]::AllDirectories)) {
        $relative = $file.Substring($prefixLength).Replace('\', '/')
        if ($relative -ceq $script:BundleChecksums) { continue }   # it cannot cover itself
        $relatives.Add($relative)
    }
    if ($relatives.Count -eq 0) {
        throw (New-BundleRefusal -Problem "There is nothing to list: $rootFull holds no files.")
    }
    $ordered = $relatives.ToArray()
    [Array]::Sort($ordered, [StringComparer]::Ordinal)

    $builder = New-Object Text.StringBuilder
    $index = 0
    foreach ($relative in $ordered) {
        $hash = Get-BundleFileSha256 -Path (Join-Path $rootFull ($relative -replace '/', '\'))
        [void]$builder.Append("$hash  $relative`n")
        $index++
        if ($Label -ne '' -and ($index % 4000) -eq 0) {
            Write-BundleStep "$Label ... $index / $($ordered.Length) files"
        }
    }
    Write-BundleTextFile -Path (Join-Path $rootFull $script:BundleChecksums) -Content $builder.ToString()
    return $ordered.Length
}

function Test-BundleChecksumManifest {
    <#
    .SYNOPSIS
        Verify a bundle against its own SHA256SUMS.txt.
    .DESCRIPTION
        The same check verify-then-play.bat performs on the tester's machine,
        callable from this repository: every listed file must exist and hash to
        what the manifest says. Extra files are NOT flagged here - the manifest
        proves the shipped files survived the copy; run.log and other files a
        bundle legitimately grows at runtime are not corruption.
    .OUTPUTS
        An array of human-readable problems. Empty means the copy is intact.
    #>
    param([Parameter(Mandatory)][string]$Root)
    $problems = New-Object 'System.Collections.Generic.List[string]'
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $manifestPath = Join-Path $rootFull $script:BundleChecksums
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        $problems.Add("$($script:BundleChecksums) is missing, so this copy cannot be checked")
        return $problems.ToArray()
    }
    $listed = 0
    $lineNumber = 0
    foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
        $lineNumber++
        if ($line.Trim() -eq '') { continue }
        # Accept both sha256sum spellings: 'hash  path' (text) and 'hash *path'
        # (binary), so a manifest regenerated by coreutils still verifies.
        if ($line -cnotmatch '^(?<hash>[0-9a-fA-F]{64}) [ \*](?<path>.+)$') {
            $problems.Add("$($script:BundleChecksums) line ${lineNumber} is not a sha256sum entry: $line")
            continue
        }
        $expected = $Matches['hash'].ToLowerInvariant()
        $relative = $Matches['path'].Trim()
        # The manifest is data from outside; a hostile or mangled line must not
        # make this read anything above the bundle root.
        if ($relative -match '(^|[/\\])\.\.([/\\]|$)' -or $relative -match '^[A-Za-z]:' -or $relative -match '^[/\\]') {
            $problems.Add("$($script:BundleChecksums) line ${lineNumber} names an unsafe path: $relative")
            continue
        }
        $listed++
        $full = Join-Path $rootFull ($relative -replace '/', '\')
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            $problems.Add("missing file: $relative")
            continue
        }
        $actual = Get-BundleFileSha256 -Path $full
        if ($actual -cne $expected) {
            $problems.Add("content changed: $relative")
        }
    }
    if ($listed -eq 0 -and $problems.Count -eq 0) {
        $problems.Add("$($script:BundleChecksums) lists no files, so it proves nothing")
    }
    return $problems.ToArray()
}

function New-BundleVerifyLauncher {
    <#
    .SYNOPSIS
        Write verify-then-play.bat into a bundle: double-clickable, checks every
        file against SHA256SUMS.txt, and only then hands off to run-with-log.bat.
    .DESCRIPTION
        For the playtester who got this over a shared folder: a truncated or
        bit-rotted copy fails HERE, with the damaged files named in plain
        English and 'copy the bundle again' as the remedy, instead of failing
        inside the game as a missing faction or a crash nobody can diagnose by
        phone.

        Deliberately self-contained: the bundle does not carry tools/, so the
        check is an embedded PowerShell command, not a call back into this
        repository. Every path out of the window pauses (the success path via
        run-with-log.bat's own pause), so a double-clicked console never
        vanishes with its message.
    #>
    param(
        [Parameter(Mandatory)][string]$BundleRoot,
        [Parameter(Mandatory)][string]$BundleName
    )
    # Single-quoted here-string ON PURPOSE: everything in it is PowerShell for
    # the tester's machine, not for now. One line, because cmd hands -Command
    # exactly one argument. %% is cmd's escape for the literal modulo percent.
    $verify = @'
powershell -NoProfile -ExecutionPolicy Bypass -Command "$failed = @(); $checked = 0; foreach ($line in (Get-Content -LiteralPath 'SHA256SUMS.txt')) { if ($line -notmatch '^([0-9a-fA-F]{64}) [ *](.+)$') { continue }; $expected = $Matches[1].ToLowerInvariant(); $shown = $Matches[2].Trim(); $checked++; if (($checked %% 400) -eq 0) { Write-Host ('  ... ' + $checked + ' files checked') }; $file = $shown -replace '/', '\'; if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { $failed += ($shown + '  - MISSING'); continue }; $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant(); if ($actual -ne $expected) { $failed += ($shown + '  - DAMAGED, contents differ') } }; if ($checked -eq 0) { Write-Host 'SHA256SUMS.txt lists no files, so this copy cannot be checked.'; exit 1 }; if ($failed.Count -gt 0) { Write-Host ''; Write-Host ('BAD COPY - ' + $failed.Count + ' of ' + $checked + ' files failed:'); foreach ($f in $failed) { Write-Host ('  ' + $f) }; exit 1 }; Write-Host ('All ' + $checked + ' files check out.'); exit 0"
'@
    $text = @"
@echo off
setlocal
REM Checks every file in this bundle against SHA256SUMS.txt, then starts the
REM game with run-with-log.bat.
REM Generated by tools/Build-PlayableBundle.ps1 - do not hand-edit; rebuild.
cd /d "%~dp0"

echo Checking this copy of $BundleName.
echo This reads every file once - allow a few minutes on a slow drive...
echo.

if not exist "%~dp0SHA256SUMS.txt" (
    echo This copy has no SHA256SUMS.txt, so it cannot be checked.
    echo Copy the whole bundle folder again from the shared folder. If you accept
    echo an unchecked copy, run-with-log.bat starts the game directly.
    echo.
    pause
    exit /b 2
)

$verify

if errorlevel 1 (
    echo.
    echo ---------------------------------------------------------------
    echo This copy of the game is DAMAGED or INCOMPLETE.
    echo The files listed above did not survive the copy.
    echo.
    echo What to do: delete this folder, copy the whole bundle again from
    echo the shared folder, then double-click verify-then-play.bat again.
    echo If it fails a second time, send us the file names printed above.
    echo ---------------------------------------------------------------
    echo.
    pause
    exit /b 1
)

echo.
echo Everything checks out. Starting the game...
echo.
call "%~dp0run-with-log.bat"
exit /b 0
"@
    # cmd.exe is the one consumer in this codebase that genuinely cares about
    # CRLF, so the launcher is normalized rather than inheriting whatever line
    # endings this source file happens to carry.
    $normalized = (($text -replace "`r`n", "`n") -replace "`n", "`r`n")
    Write-BundleTextFile -Path (Join-Path $BundleRoot $script:BundleVerifyLauncher) -Content ($normalized + "`r`n")
}

function Get-BundleMainWorktree {
    <#
    .SYNOPSIS
        The main checkout of this repository, whatever worktree we are in.
    .DESCRIPTION
        Releases land in the MAIN checkout's dist/, not in whichever worktree
        happened to build them. A worktree is temporary and gets deleted; a
        release that lived only inside one would vanish with it.
    #>
    param([Parameter(Mandatory)][string]$RepoRoot)
    Push-Location $RepoRoot
    try {
        $commonDir = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Out-String).Trim()
    } finally { Pop-Location }
    if ($commonDir -eq '') {
        throw (New-BundleRefusal -Problem "Cannot locate the main checkout from $RepoRoot (git rev-parse --git-common-dir said nothing)." -Remedy 'Pass -ReleaseRoot <path> explicitly.')
    }
    $main = [IO.Path]::GetDirectoryName(([IO.Path]::GetFullPath($commonDir)).TrimEnd('\', '/'))
    if ($main -eq '' -or $null -eq $main -or -not [IO.Directory]::Exists($main)) {
        throw (New-BundleRefusal -Problem "Resolved a main checkout that does not exist: '$main'." -Remedy 'Pass -ReleaseRoot <path> explicitly.')
    }
    return $main
}

function Set-BundleReleaseDirectoryIgnored {
    <#
    .SYNOPSIS
        Make the release directory git-invisible on EVERY branch, then let the
        existing refusal check the result.
    .DESCRIPTION
        THE HAZARD THIS CLOSES, measured rather than assumed: `/dist/` is in
        .gitignore on some branches of this repository and not on others. The
        root .gitignore is a TRACKED file, so which rules apply depends on which
        branch the checkout is standing on - and a release directory holding
        gigabytes of retail-derived output was one `git add -A` from entering
        history whenever a branch without the rule was checked out.

        A .gitignore INSIDE the release directory containing `*` does not have
        that property. It is untracked, it ignores itself, and it applies on
        every branch because it is not part of any branch. That is the whole
        fix: the guarantee stops depending on which branch is checked out.

        This ADDS a guard; it does not replace one. Test-BundlePathIsGitIgnored
        still asks git afterwards and still refuses if git disagrees, so a
        release can never be written on the strength of this function having
        been called.
    #>
    param([Parameter(Mandatory)][string]$ReleaseDirectory)
    $full = [IO.Path]::GetFullPath($ReleaseDirectory).TrimEnd('\', '/')

    # NEVER blanket-ignore a directory that has tracked files in it. Writing `*`
    # into, say, the repository root would hide the whole project from git and
    # clobber the real .gitignore on the way. A release directory is output; if
    # git is tracking anything here, this is not one, and the caller has pointed
    # a release somewhere it must not go - which the git-ignore refusal below
    # will then say out loud.
    if ([IO.Directory]::Exists($full)) {
        Push-Location $full
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { $tracked = @(& git ls-files -- . 2>$null | Where-Object { $_ -ne '' }) }
        finally { $ErrorActionPreference = $previousPreference; Pop-Location }
        if ($tracked.Count -gt 0) {
            throw (New-BundleRefusal -Problem "Refusing to write a blanket .gitignore into $full - git is tracking $($tracked.Count) file(s) there, so it is source, not release output." -Remedy 'Point the release at a directory that holds output only (dist/ by default).')
        }
    }

    [void](New-Item -ItemType Directory -Path $full -Force)
    $ignoreFile = Join-Path $full '.gitignore'
    $wanted = @(
        '# Release staging: packaged builds and the retail-derived content they carry.'
        '#'
        '# Self-ignoring ON PURPOSE. The root .gitignore is tracked, so a /dist/ rule'
        '# only applies on the branches that happen to carry it; this file is untracked'
        '# and therefore applies on every branch, including a checkout that has just'
        '# switched to one without the rule.'
        '#'
        '# Written by tools/playable-bundle-common.ps1. Do not delete: without it a'
        '# `git add -A` on the wrong branch would commit gigabytes of retail-derived'
        '# content, which is permanent and pushed, unlike the bundle itself.'
        '*'
    ) -join "`n"
    $existing = ''
    if (Test-Path -LiteralPath $ignoreFile -PathType Leaf) { $existing = [IO.File]::ReadAllText($ignoreFile) }
    if ($existing.TrimEnd() -cne $wanted) {
        Write-BundleTextFile -Path $ignoreFile -Content ($wanted + "`n")
        return $true
    }
    return $false
}

function New-BundleMergedManifest {
    <#
    .SYNOPSIS
        A tree manifest for "this pack plus these extra files", without walking
        and re-hashing the whole multi-gigabyte pack a third time.
    .DESCRIPTION
        Produces exactly what Get-BundleTreeManifest would produce for the
        merged tree: the roll-up hashes are recomputed from the merged entry set
        using the same sorted, tab-separated, forward-slash encoding, so a
        verifier that re-walks the finished bundle agrees with it byte for byte.
        Refuses an overlay that would land on a path the pack already has -
        silently replacing a pack's own file is content substitution, not
        staging.
    #>
    param(
        [Parameter(Mandatory)]$Base,
        [Parameter(Mandatory)][hashtable]$Added
    )
    $entries = [ordered]@{}
    foreach ($key in $Base.entries.Keys) { $entries[$key] = $Base.entries[$key] }
    foreach ($key in $Added.Keys) {
        if ($entries.Contains($key)) {
            throw (New-BundleRefusal -Problem "Staging would overwrite a file the content pack already ships: $key" -Remedy 'Nothing may replace a pack file. Remove the collision at the source, or stage under a path the pack does not use.')
        }
        $entries[$key] = $Added[$key]
    }
    $ordered = @($entries.Keys)
    [Array]::Sort($ordered, [StringComparer]::Ordinal)

    $layout = New-Object Text.StringBuilder
    $deep = New-Object Text.StringBuilder
    $totalBytes = [long]0
    $quick = [bool]$Base.quick
    foreach ($relative in $ordered) {
        $entry = $entries[$relative]
        $totalBytes += [long]$entry.bytes
        [void]$layout.Append("$($entry.bytes)`t$relative`n")
        if (-not $quick) { [void]$deep.Append("$($entry.sha256)`t$($entry.bytes)`t$relative`n") }
    }
    $encoding = [Text.UTF8Encoding]::new($false)
    $layoutHash = [BitConverter]::ToString(
        [Security.Cryptography.SHA256]::Create().ComputeHash($encoding.GetBytes($layout.ToString()))
    ).Replace('-', '').ToLowerInvariant()
    $deepHash = ''
    if (-not $quick) {
        $deepHash = [BitConverter]::ToString(
            [Security.Cryptography.SHA256]::Create().ComputeHash($encoding.GetBytes($deep.ToString()))
        ).Replace('-', '').ToLowerInvariant()
    }
    $merged = [ordered]@{}
    foreach ($relative in $ordered) { $merged[$relative] = $entries[$relative] }
    return [pscustomobject]@{
        root         = $Base.root
        files        = $ordered.Length
        bytes        = $totalBytes
        layoutSha256 = $layoutHash
        sha256       = $deepHash
        quick        = $quick
        entries      = $merged
    }
}

function Test-BundleWotrDocument {
    <#
    .SYNOPSIS
        Refuse a living-world document the shipped game would reject anyway.
    #>
    param([Parameter(Mandatory)][string]$Path)
    $info = New-Object IO.FileInfo $Path
    if ($info.Length -eq 0) {
        throw (New-BundleRefusal -Problem "The living-world document is empty: $Path")
    }
    if ($info.Length -gt $script:WotrDocumentMaxBytes) {
        throw (New-BundleRefusal -Problem "The living-world document is $($info.Length) bytes, over the $($script:WotrDocumentMaxBytes)-byte limit the game enforces (wotr_session.gd DOCUMENT_MAX_BYTES)." -Remedy 'The shipped build would refuse to read it, so shipping it would produce a War of the Ring button that is present and broken.')
    }
    try { $parsed = [IO.File]::ReadAllText($Path) | ConvertFrom-Json } catch {
        throw (New-BundleRefusal -Problem "The living-world document is not valid JSON: $Path" -Remedy 'Re-run the living-world report; a document that does not parse disables War of the Ring at runtime.')
    }
    if ($null -eq $parsed -or $parsed -isnot [psobject]) {
        throw (New-BundleRefusal -Problem "The living-world document is not a JSON object: $Path")
    }
    return $info.Length
}

function Test-BundleWotrMap {
    <#
    .SYNOPSIS
        Refuse a living-map bundle the shipped game would reject anyway.
    #>
    param([Parameter(Mandatory)][string]$Path)
    $manifest = Join-Path $Path 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "The living-map directory has no manifest.json, so it is not a living-map bundle: $Path" -Remedy 'Produce one with `python -m openbfme_importer.livingmap_bundle <catalog>/rotwk.json <bundle-dir>`.')
    }
    try { $parsed = [IO.File]::ReadAllText($manifest) | ConvertFrom-Json } catch {
        throw (New-BundleRefusal -Problem "The living-map manifest is not valid JSON: $manifest")
    }
    if ([string]$parsed.schema -cne $script:WotrMapSchema) {
        throw (New-BundleRefusal -Problem "The living-map manifest declares schema '$($parsed.schema)', expected '$($script:WotrMapSchema)': $manifest" -Remedy 'The shipped build checks this and would fall back to the flat 2D map.')
    }
    return $true
}

$script:PatchNoteSections = @('NEW', 'FIX', 'KNOWN', 'REMOVED')

function Get-BundlePreviousRelease {
    <#
    .SYNOPSIS
        The most recent release in this release root that can identify itself.
    .DESCRIPTION
        "What changed since last time" is only answerable if last time said what
        commit it was. Hand-assembled bundles (alpha01) carry no BUILD-INFO.json
        and are deliberately NOT counted: guessing a range from a directory name
        would produce a confident, wrong changelog, which is the exact failure
        class the patch notes exist to avoid. Returns $null when there is none,
        and the caller must then say so in the notes rather than invent a range.
    #>
    param(
        [Parameter(Mandatory)][string]$ReleaseRoot,
        [Parameter(Mandatory)][string]$RepoRoot,
        [string]$ExcludeName = ''
    )
    if (-not (Test-Path -LiteralPath $ReleaseRoot -PathType Container)) { return $null }
    $best = $null
    foreach ($directory in @(Get-ChildItem -LiteralPath $ReleaseRoot -Directory -ErrorAction SilentlyContinue)) {
        if ($ExcludeName -ne '' -and $directory.Name -ceq $ExcludeName) { continue }
        $infoPath = Join-Path $directory.FullName $script:BundleInfoJson
        if (-not (Test-Path -LiteralPath $infoPath -PathType Leaf)) { continue }
        try { $info = [IO.File]::ReadAllText($infoPath) | ConvertFrom-Json } catch { continue }
        $names = @($info.PSObject.Properties.Name)
        if ($names -notcontains 'source' -or $names -notcontains 'builtAtUtc') { continue }
        $commit = [string]$info.source.commit
        if ($commit -cnotmatch '^[0-9a-f]{40}$') { continue }
        Push-Location $RepoRoot
        # $ErrorActionPreference is 'Stop' in the callers, and git writes to
        # stderr when the object is unknown - which Windows PowerShell turns into
        # a terminating error before the exit code can be read. Asking about a
        # commit that is legitimately absent must not kill the build.
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & git cat-file -e "$commit^{commit}" 2>$null | Out-Null; $known = ($LASTEXITCODE -eq 0) }
        finally { $ErrorActionPreference = $previousPreference; Pop-Location }
        if (-not $known) { continue }
        $builtAt = [string]$info.builtAtUtc
        if ($null -eq $best -or $builtAt -gt $best.builtAtUtc) {
            $best = [pscustomobject]@{
                name = [string]$info.bundleName; commit = $commit
                shortCommit = $commit.Substring(0, 10); builtAtUtc = $builtAt
                path = $directory.FullName
            }
        }
    }
    return $best
}

function Read-BundleReleaseHighlights {
    <#
    .SYNOPSIS
        Player-facing lines, each of which must name the change that introduced it.
    .DESCRIPTION
        THE CITATION IS THE POINT. A patch note here cannot be written without a
        commit id, and a note whose commit is not in this release's range is
        dropped rather than printed. So the notes cannot claim a feature this
        build does not contain: the worst case is a note that is missing, never
        one that is invented. Lines are `SECTION|commit|text`; `#` comments and
        blank lines are ignored.

        Sections, and why there are exactly three:
          NEW       something a player can do in this build that they could not before
          FIX       something that was broken for a player and now is not
          UNCERTAIN real work that landed, which this tool CANNOT confirm the
                    player reaches. It is printed under its own heading rather
                    than promoted or dropped, because both of those are lies of a
                    different kind.
    #>
    param([Parameter(Mandatory)][string]$Path)
    $rows = New-Object 'System.Collections.Generic.List[object]'
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $rows.ToArray() }
    $lineNumber = 0
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $lineNumber++
        $trimmed = $line.Trim()
        if ($trimmed -eq '' -or $trimmed.StartsWith('#')) { continue }
        $parts = $trimmed.Split('|', 3)
        if ($parts.Length -ne 3) {
            throw (New-BundleRefusal -Problem "$Path line ${lineNumber}: expected 'SECTION|commit|text'." -Remedy "Got: $trimmed")
        }
        $section = $parts[0].Trim().ToUpperInvariant()
        if ($script:PatchNoteSections -cnotcontains $section) {
            throw (New-BundleRefusal -Problem "$Path line ${lineNumber}: unknown section '$section'." -Remedy "Use one of: $($script:PatchNoteSections -join ', ')")
        }
        $commit = $parts[1].Trim()
        if ($commit -cnotmatch '^[0-9a-f]{7,40}$') {
            throw (New-BundleRefusal -Problem "$Path line ${lineNumber}: '$commit' is not a commit id." -Remedy 'Every patch note must cite the commit that introduced it; that citation is what stops the notes overstating.')
        }
        $text = $parts[2].Trim()
        if ($text -eq '') {
            throw (New-BundleRefusal -Problem "$Path line ${lineNumber}: the note text is empty.")
        }
        $rows.Add([pscustomobject]@{ section = $section; commit = $commit; text = $text; line = $lineNumber })
    }
    return $rows.ToArray()
}

function New-BundleReleaseNotes {
    <#
    .SYNOPSIS
        Build the playtester-facing PATCH-NOTES.txt and the raw CHANGES.txt.
    .DESCRIPTION
        Everything printed is derived from `git log` over a stated range plus a
        citation file whose every line names a commit inside that range. Nothing
        is inferred from a feature name, a directory listing, or this tool's
        opinion of what a commit probably did.

        The range is chosen in this order, and the notes SAY which was used:
          1. -Since, if given.
          2. The newest previous release in the release root that carries a
             BUILD-INFO.json naming a commit this repository knows.
          3. Nothing - in which case the notes state that no previous release
             could be identified and fall back to the branch point against the
             stated base branch.
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$BundleName,
        [Parameter(Mandatory)]$Source,
        [Parameter(Mandatory)][string]$BuiltAtUtc,
        [Parameter(Mandatory)]$PackRecords,
        [string]$Since = '',
        [string]$HighlightsPath = '',
        [string]$BaseBranch = 'main',
        $PreviousRelease = $null,
        $WotrData = $null
    )
    Push-Location $RepoRoot
    try {
        # THE RANGE ENDS AT THE COMMIT THIS BUNDLE WAS BUILT FROM, never at HEAD.
        # Other lanes commit to this repository continuously; a first version of
        # this used HEAD and, when re-run minutes later, produced notes claiming
        # two changes that were not in the build. Notes that describe a different
        # build than the one they sit next to are worse than no notes.
        $to = [string]$Source.commit
        if ($to -cnotmatch '^[0-9a-f]{40}$') {
            throw (New-BundleRefusal -Problem "The source identity carries no commit, so the release notes have no endpoint to stop at: '$to'.")
        }
        $rangeBasis = ''
        $from = ''
        if ($Since -ne '') {
            # Same stderr trap as above: an unknown -Since must produce this
            # refusal, which names the parameter, not a bare "fatal:" from git.
            $previousPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try { $resolved = (& git rev-parse --verify "$Since^{commit}" 2>$null | Out-String).Trim() }
            finally { $ErrorActionPreference = $previousPreference }
            if ($LASTEXITCODE -ne 0 -or $resolved -eq '') {
                throw (New-BundleRefusal -Problem "-Since '$Since' is not a commit this repository knows." -Remedy 'Pass a commit, tag or branch that exists here.')
            }
            $from = $resolved
            $rangeBasis = "explicit -Since $Since"
        } elseif ($null -ne $PreviousRelease) {
            $from = $PreviousRelease.commit
            $rangeBasis = "the previous release in this folder, $($PreviousRelease.name) (built $($PreviousRelease.builtAtUtc))"
        } else {
            $mergeBase = (& git merge-base $BaseBranch $to 2>$null | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $mergeBase -eq '') {
                $from = ''
                $rangeBasis = "NOTHING - not even a branch point against '$BaseBranch' could be resolved, so this covers the whole history"
            } else {
                $from = $mergeBase
                $rangeBasis = "the branch point against '$BaseBranch' (no previous release could be identified)"
            }
        }

        $range = if ($from -eq '') { $to } else { "$from..$to" }
        # A separator that cannot occur in a commit subject. Written as [char],
        # not as a "`u{...}" escape: Windows PowerShell 5.1 does not understand
        # that escape and would split on the literal text, silently producing an
        # EMPTY changelog for a release that has 250 changes in it.
        $unitSeparator = [string][char]0x1f
        $log = @(& git log --reverse --format='%H%x1f%h%x1f%cI%x1f%s' $range 2>$null | Where-Object { $_ -ne '' })
        $commits = New-Object 'System.Collections.Generic.List[object]'
        foreach ($row in $log) {
            $fields = $row -split $unitSeparator
            if ($fields.Length -lt 4) { continue }
            $subject = $fields[3]
            $area = 'other'
            if ($subject -cmatch '^(?<a>[a-z][a-z0-9-]{1,14}):\s') { $area = $Matches['a'] }
            $commits.Add([pscustomobject]@{
                commit = $fields[0]; short = $fields[1]; date = $fields[2]
                subject = $subject; area = $area
            })
        }

        # "git printed N lines and we understood none of them" is not an empty
        # release, it is a broken parser - and it would ship a release whose
        # notes say nothing changed. That mistake has already been made here
        # once, so it is now a refusal rather than a comment.
        if ($log.Count -gt 0 -and $commits.Count -eq 0) {
            throw (New-BundleRefusal -Problem "git log returned $($log.Count) lines for $range and none of them parsed, so the release notes would claim nothing changed." -Remedy 'This is a defect in New-BundleReleaseNotes, not in the repository. Do not ship notes produced by it.')
        }

        $highlights = @()
        $dropped = New-Object 'System.Collections.Generic.List[string]'
        if ($HighlightsPath -ne '') {
            $inRange = @{}
            foreach ($entry in $commits) {
                $inRange[$entry.commit] = $entry
                $inRange[$entry.short] = $entry
            }
            foreach ($row in @(Read-BundleReleaseHighlights -Path $HighlightsPath)) {
                $matched = $null
                if ($inRange.ContainsKey($row.commit)) { $matched = $inRange[$row.commit] }
                else {
                    foreach ($entry in $commits) {
                        if ($entry.commit.StartsWith($row.commit, [StringComparison]::Ordinal)) { $matched = $entry; break }
                    }
                }
                if ($null -eq $matched) {
                    # Not an error: a highlights file accumulates across releases,
                    # so a note for an older release legitimately falls out of
                    # range. Dropping it is the honest outcome; printing it would
                    # credit this build with someone else's work.
                    $dropped.Add("$($row.section) $($row.commit): $($row.text)")
                    continue
                }
                $highlights += [pscustomobject]@{
                    section = $row.section; commit = $matched.short
                    date = $matched.date.Substring(0, 10); text = $row.text
                }
            }
        }
        $cited = @{}
        foreach ($highlight in $highlights) { $cited[$highlight.commit] = $true }

        $byArea = @{}
        foreach ($entry in $commits) {
            if (-not $byArea.ContainsKey($entry.area)) { $byArea[$entry.area] = 0 }
            $byArea[$entry.area] = $byArea[$entry.area] + 1
        }
        $areaLines = @($byArea.Keys | Sort-Object { -$byArea[$_] }, { $_ } | ForEach-Object {
            "    {0,-16} {1,4}" -f $_, $byArea[$_]
        })

        function Format-Section {
            param([string]$Key)
            $rows = @($highlights | Where-Object { $_.section -ceq $Key })
            if ($rows.Count -eq 0) { return @('-none') }
            # One line per note, no commit citation in the OUTPUT. The citation
            # still governs: a note whose commit is outside the range was already
            # dropped upstream, so this file still cannot claim work the build
            # does not contain - it just no longer makes the reader step over
            # the proof to read the sentence.
            return @($rows | ForEach-Object { "-$($_.text)" })
        }

        $first = ''
        $last = ''
        if ($commits.Count -gt 0) {
            $first = "$($commits[0].short) $($commits[0].date.Substring(0,10))"
            $last = "$($commits[$commits.Count - 1].short) $($commits[$commits.Count - 1].date.Substring(0,10))"
        }

        $previousLine = 'NONE COULD BE IDENTIFIED. No earlier build in this folder states which'
        if ($null -ne $PreviousRelease) {
            $previousLine = "$($PreviousRelease.name), built $($PreviousRelease.builtAtUtc) from $($PreviousRelease.shortCommit)"
        }
        $previousExtra = ''
        if ($null -eq $PreviousRelease) {
            $previousExtra = @"

  commit it was built from, so "what changed since then" is not answerable for
  it. Rather than guess a range, this list covers $rangeBasis.
  Everything below is still taken from the real history of this build; only the
  starting point is a fallback.
"@
        }

        $wotrLine = 'War of the Ring data: not recorded by this build.'
        if ($null -ne $WotrData) {
            $wotrNames = @($WotrData.PSObject.Properties.Name)
            $wotrDegraded = ($wotrNames -contains 'degraded' -and [bool]$WotrData.degraded)
            if ([bool]$WotrData.staged -and -not $wotrDegraded) {
                $wotrLine = 'War of the Ring data ships inside this build - no setup needed.'
            } elseif ([bool]$WotrData.staged) {
                # A degraded build must not read like a complete one. The player
                # is told which parts are absent and what they will see instead,
                # in the same file that tells them what is new.
                $lost = @()
                if ($wotrNames -contains 'bundles') {
                    $lost = @(@($WotrData.bundles) | Where-Object { -not [bool]$_.staged } | ForEach-Object { "  -no $([string]$_.losesWithout)" })
                }
                $wotrLine = ("War of the Ring opens, but this build is MISSING some of its converted art:`n" + ($lost -join "`n"))
            } else {
                $wotrLine = "War of the Ring is NOT in this build (menu entry reads UNAVAILABLE): $([string]$WotrData.reason)"
            }
        }

        $dirtyLine = ''
        if ($Source.dirty) {
            $dirtyLine = "`nBuilt from a working copy with $($Source.dirtyFileCount) uncommitted file(s). Those changes are in`nthe build but not in the lists below, which come from committed history.`n"
        }

        $notes = @"
OPEN BFME - $($Source.shortCommit)  ($BuiltAtUtc UTC)
$dirtyLine
Bugfixes:

$((Format-Section -Key 'FIX') -join "`n")

Known Issues:

$((Format-Section -Key 'KNOWN') -join "`n")

New

$((Format-Section -Key 'NEW') -join "`n")

Removed

$((Format-Section -Key 'REMOVED') -join "`n")


---
$wotrLine
Covers $($commits.Count) change(s), $rangeBasis.
Full list: $($script:BundleChangeLog). Problems: send run.log and BUILD-INFO.txt.
"@

        $changeLines = @($commits | ForEach-Object { "$($_.date.Substring(0,10))  $($_.short)  $($_.subject)" })
        $changeLog = @"
OPEN BFME - $BundleName
Every committed change in this release, oldest first. Raw history, no editing.
Range: $rangeBasis
$(if ($from -eq '') { 'From: (the beginning of history)' } else { "From: $from" })
To:   $($Source.commit)
Count: $($commits.Count)

$($changeLines -join "`n")
"@
        return [pscustomobject]@{
            notesText   = $notes
            changeText  = $changeLog
            rangeBasis  = $rangeBasis
            fromCommit  = $from
            commitCount = $commits.Count
            highlights  = $highlights
            dropped     = $dropped.ToArray()
        }
    } finally { Pop-Location }
}

# The derived War of the Ring staging plan. Dot-sourced LAST so it can use the
# refusal helper and the size limits above; both scripts get it for free, which
# is what keeps "what the builder staged" and "what the verifier looks for" from
# drifting apart.
. (Join-Path $PSScriptRoot 'wotr-data-staging.ps1')

function Format-BundleBytes {
    param([Parameter(Mandatory)][long]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N1} KB' -f ($Bytes / 1KB)) }
    return "$Bytes bytes"
}
