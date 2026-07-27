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
    $contentRoot = Join-Path $BundleRoot $script:BundleContentDir
    if (-not (Test-Path -LiteralPath $contentRoot -PathType Container)) {
        $problems.Add("required directory is missing: $($script:BundleContentDir)/ (it must sit beside the exe)")
        return $problems.ToArray()
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

function Format-BundleBytes {
    param([Parameter(Mandatory)][long]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N1} KB' -f ($Bytes / 1KB)) }
    return "$Bytes bytes"
}
