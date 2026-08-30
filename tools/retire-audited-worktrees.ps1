[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('Plan', 'Archive', 'Retire', 'SelfTest')]
    [string] $Action = 'Plan',
    [string] $Id,
    [string] $RepositoryRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Git = (Get-Command git.exe -CommandType Application -ErrorAction Stop).Source

function Invoke-Git([string] $Root, [string[]] $Arguments, [switch] $AllowFailure) {
    $stderrPath = [IO.Path]::GetTempFileName()
    try {
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Git -c core.hooksPath=NUL -c core.quotepath=false -c core.longpaths=true -C $Root @Arguments 2> $stderrPath | ForEach-Object { [string]$_ })
        $code = $LASTEXITCODE
        $errors = @([IO.File]::ReadAllLines($stderrPath))
    }
    finally {
        $ErrorActionPreference = $oldPreference
        [IO.File]::Delete($stderrPath)
    }
    if ($code -ne 0 -and -not $AllowFailure) {
        throw "Git failed ($code): $((@($errors) + @($lines)) -join [Environment]::NewLine)"
    }
    return ,([pscustomobject]@{ Code = $code; Lines = $lines })
}

function Get-TextHash([string] $Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

function Write-Json([string] $Path, [object] $Value) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    $text = ConvertTo-Json -InputObject $Value -Depth 16
    [IO.File]::WriteAllText($Path, $text + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

function Get-MainRoot([string] $Root) {
    $matches = [Collections.Generic.List[string]]::new()
    $current = $null
    foreach ($line in (Invoke-Git $Root @('worktree', 'list', '--porcelain')).Lines) {
        if ($line.StartsWith('worktree ')) { $current = $line.Substring(9) }
        elseif ($line -ceq 'branch refs/heads/main') { $matches.Add($current) }
    }
    if ($matches.Count -ne 1) { throw 'Expected exactly one registered main worktree.' }
    return [IO.Path]::GetFullPath($matches[0])
}

function Get-WorktreeId([string] $Path) {
    return 'wt-' + (Get-TextHash ([IO.Path]::GetFullPath($Path).ToLowerInvariant())).Substring(0, 16)
}

function Get-WorktreeRow([string] $MainRoot, [string] $WorktreeId) {
    if ([string]::IsNullOrWhiteSpace($WorktreeId)) { throw "$Action requires -Id." }
    $entries = [Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in (Invoke-Git $MainRoot @('worktree', 'list', '--porcelain')).Lines + '') {
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($null -ne $current) { $entries.Add([pscustomobject]$current); $current = $null }
        }
        elseif ($line.StartsWith('worktree ')) { $current = [ordered]@{ path = $line.Substring(9) } }
        elseif ($line.StartsWith('HEAD ')) { $current.head = $line.Substring(5) }
        elseif ($line.StartsWith('branch ')) { $current.branch = $line.Substring(7) }
        elseif ($line -ceq 'locked' -or $line.StartsWith('locked ')) { $current.locked = $true }
    }
    $matches = @($entries | Where-Object { (Get-WorktreeId $_.path) -ceq $WorktreeId })
    if ($matches.Count -ne 1) { throw "Expected one live registered worktree for id $WorktreeId." }
    $entry = $matches[0]
    $path = [IO.Path]::GetFullPath([string]$entry.path)
    if ([StringComparer]::OrdinalIgnoreCase.Equals($path, $MainRoot)) { throw 'Main worktree retirement is forbidden.' }
    if (-not [IO.Directory]::Exists($path)) { throw 'Worktree path is missing.' }
    if ($entry.PSObject.Properties.Name -contains 'locked') { throw 'Locked worktree retirement is forbidden.' }
    $branch = if ($entry.PSObject.Properties.Name -contains 'branch') { [string]$entry.branch } else { $null }
    $mainHead = ((Invoke-Git $MainRoot @('rev-parse', '--verify', 'main^{commit}')).Lines -join '').Trim()
    $merged = (Invoke-Git $MainRoot @('merge-base', '--is-ancestor', [string]$entry.head, $mainHead) -AllowFailure).Code -eq 0
    $trackedStatus = @((Invoke-Git $path @('status', '--porcelain=v1', '--untracked-files=no')).Lines)
    if (-not $merged) { throw 'Only worktrees already merged to main may use this retirement tool.' }
    if ($trackedStatus.Count -ne 0) { throw 'Tracked dirt is outside this retention work item.' }
    return [pscustomobject][ordered]@{
        id = $WorktreeId
        path = $path
        head = [string]$entry.head
        branch = $branch
        mainHead = $mainHead
    }
}

function Get-RelativePath([string] $Root, [string] $Path) {
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped its worktree root.' }
    return $full.Substring($prefix.Length).Replace('\', '/')
}

function Resolve-ReparseTarget([IO.FileSystemInfo] $Item) {
    $targets = @($Item.Target | ForEach-Object { [string]$_ })
    if ($targets.Count -ne 1 -or [string]::IsNullOrWhiteSpace($targets[0])) { throw "Reparse target is missing or ambiguous: $($Item.FullName)" }
    $target = $targets[0]
    if ($target.StartsWith('\??\')) { $target = $target.Substring(4) }
    if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path ([IO.Path]::GetDirectoryName($Item.FullName)) $target }
    return [IO.Path]::GetFullPath($target)
}

function Get-TargetFingerprint([string] $Target) {
    $full = [IO.Path]::GetFullPath($Target)
    if ([IO.File]::Exists($full)) {
        $file = Get-Item -LiteralPath $full -Force
        return [pscustomobject][ordered]@{
            path = $full
            kind = 'file'
            length = $file.Length
            sha256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    if (-not [IO.Directory]::Exists($full)) { throw "Reparse target is not readable: $full" }
    $children = [Collections.Generic.List[object]]::new()
    foreach ($child in @(Get-ChildItem -LiteralPath $full -Force | Sort-Object Name)) {
        $children.Add([pscustomobject][ordered]@{
            name = $child.Name
            directory = [bool]$child.PSIsContainer
            reparse = [bool]($child.Attributes -band [IO.FileAttributes]::ReparsePoint)
            length = if ($child.PSIsContainer) { $null } else { $child.Length }
        })
    }
    $payload = ConvertTo-Json -InputObject @($children.ToArray()) -Depth 4 -Compress
    return [pscustomobject][ordered]@{
        path = $full
        kind = 'directory'
        childCount = $children.Count
        childrenSha256 = Get-TextHash $payload
    }
}

function Get-TrackedSet([string] $Root) {
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in (Invoke-Git $Root @('ls-files', '--cached')).Lines) {
        if (-not [string]::IsNullOrWhiteSpace($relative)) { [void]$set.Add($relative.Replace('\', '/')) }
    }
    return $set
}

function Get-PhysicalSnapshot([string] $Root, [switch] $HashFiles, [string] $CopyRoot) {
    $rootItem = Get-Item -LiteralPath $Root -Force
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Worktree root may not be a reparse point.' }
    $tracked = Get-TrackedSet $Root
    $directories = [Collections.Generic.Stack[string]]::new()
    $directories.Push([IO.Path]::GetFullPath($Root))
    $files = [Collections.Generic.List[object]]::new()
    $reparses = [Collections.Generic.List[object]]::new()
    while ($directories.Count -gt 0) {
        $directory = $directories.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force | Sort-Object FullName)) {
            $relative = Get-RelativePath $Root $item.FullName
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                if ($relative -ceq '.git') { throw 'The worktree .git entry may not be a reparse point.' }
                if ($tracked.Contains($relative)) { throw "Tracked reparse point is forbidden: $relative" }
                $resolvedTarget = Resolve-ReparseTarget $item
                $fingerprint = Get-TargetFingerprint $resolvedTarget
                $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
                $targetScope = if ($resolvedTarget.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { 'internal' } else { 'external' }
                $reparses.Add([pscustomobject][ordered]@{
                    path = $relative
                    directory = [bool]$item.PSIsContainer
                    linkType = [string]$item.LinkType
                    target = @($item.Target | ForEach-Object { [string]$_ })
                    resolvedTarget = $resolvedTarget
                    targetScope = $targetScope
                    targetFingerprint = $fingerprint
                })
                continue
            }
            if ($relative -ceq '.git') { continue }
            if ($item.PSIsContainer) { $directories.Push($item.FullName); continue }
            if ($tracked.Contains($relative)) { continue }
            $sha256 = $null
            if ($HashFiles) { $sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
            if (-not [string]::IsNullOrWhiteSpace($CopyRoot)) {
                $target = Join-Path $CopyRoot $relative.Replace('/', '\')
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
                Copy-Item -LiteralPath $item.FullName -Destination $target -Force
                $copiedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
                $freshSourceHash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($copiedHash -cne $freshSourceHash -or ($null -ne $sha256 -and $sha256 -cne $freshSourceHash)) {
                    throw "Payload changed during archive: $relative"
                }
                $sha256 = $copiedHash
            }
            $files.Add([pscustomobject][ordered]@{
                path = $relative
                length = $item.Length
                sha256 = $sha256
            })
        }
    }
    $orderedFiles = @($files.ToArray() | Sort-Object path)
    $orderedReparses = @($reparses.ToArray() | Sort-Object path)
    $stateJson = ConvertTo-Json -InputObject ([ordered]@{ files = $orderedFiles; reparses = $orderedReparses }) -Depth 12 -Compress
    return [pscustomobject][ordered]@{
        files = $orderedFiles
        reparses = $orderedReparses
        fileCount = $orderedFiles.Count
        bytes = [long](($orderedFiles | Measure-Object -Property length -Sum).Sum)
        stateSha256 = Get-TextHash $stateJson
    }
}

function Get-ArchiveManifest([string] $Root) {
    $records = [Collections.Generic.List[object]]::new()
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Where-Object { $_.Name -cne 'complete.json' } | Sort-Object FullName)) {
        $cursor = $file.FullName
        while ($cursor.Length -ge $Root.Length) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Archive contains a reparse point.' }
            if ([StringComparer]::OrdinalIgnoreCase.Equals($cursor, $Root)) { break }
            $cursor = [IO.Path]::GetDirectoryName($cursor)
        }
        $records.Add([pscustomobject][ordered]@{
            path = $file.FullName.Substring($prefix.Length).Replace('\', '/')
            length = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    return @($records.ToArray())
}

function Remove-TreeWithoutReparse([string] $Root) {
    if (-not [IO.Directory]::Exists($Root)) { return }
    $item = Get-Item -LiteralPath $Root -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to recurse into a reparse point.' }
    foreach ($child in @(Get-ChildItem -LiteralPath $Root -Force)) {
        if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to remove a tree containing a reparse point.' }
        if ($child.PSIsContainer) { Remove-TreeWithoutReparse $child.FullName }
        else { $child.Attributes = [IO.FileAttributes]::Normal; [IO.File]::Delete($child.FullName) }
    }
    [IO.Directory]::Delete($Root, $false)
}

function Assert-Fingerprints([object[]] $Reparses, [switch] $ExternalOnly) {
    foreach ($entry in $Reparses) {
        if ($ExternalOnly -and $entry.targetScope -cne 'external') { continue }
        $fresh = Get-TargetFingerprint ([string]$entry.resolvedTarget)
        $expected = ConvertTo-Json -InputObject $entry.targetFingerprint -Depth 6 -Compress
        $actual = ConvertTo-Json -InputObject $fresh -Depth 6 -Compress
        if ($expected -cne $actual) { throw "External reparse target changed: $($entry.path)" }
    }
}

function Unlink-Reparses([string] $Root, [object[]] $Reparses) {
    foreach ($entry in @($Reparses | Sort-Object { $_.path.Length } -Descending)) {
        $path = [IO.Path]::GetFullPath((Join-Path $Root ([string]$entry.path).Replace('/', '\')))
        $item = Get-Item -LiteralPath $path -Force
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Expected reparse point disappeared: $($entry.path)" }
        $resolved = Resolve-ReparseTarget $item
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals($resolved, [string]$entry.resolvedTarget)) { throw "Reparse target drifted: $($entry.path)" }
        if ($item.PSIsContainer) { [IO.Directory]::Delete($path, $false) }
        else { [IO.File]::Delete($path) }
    }
}

function Invoke-SelfTest {
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $testRoot = Join-Path $tempBase ('openbfme-safe-retire-' + [Guid]::NewGuid().ToString('N'))
    $repo = Join-Path $testRoot 'repo'
    $worktree = Join-Path $testRoot 'worktree'
    $external = Join-Path $testRoot 'external'
    $junction = Join-Path $worktree 'linked'
    $internalJunction = Join-Path $worktree 'internal-link'
    try {
        [IO.Directory]::CreateDirectory($repo) | Out-Null
        [IO.Directory]::CreateDirectory($external) | Out-Null
        [IO.File]::WriteAllText((Join-Path $external 'sentinel.txt'), 'external-canary', [Text.UTF8Encoding]::new($false))
        Invoke-Git $repo @('init', '-b', 'main') | Out-Null
        Invoke-Git $repo @('config', 'user.name', 'OpenBFME Retention Test') | Out-Null
        Invoke-Git $repo @('config', 'user.email', 'retention-test@invalid.local') | Out-Null
        [IO.File]::WriteAllText((Join-Path $repo '.gitignore'), "workspace/`nprivate/`nlinked/`ninternal-link/`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $repo 'tracked.txt'), 'tracked', [Text.UTF8Encoding]::new($false))
        Invoke-Git $repo @('add', '.gitignore', 'tracked.txt') | Out-Null
        Invoke-Git $repo @('commit', '-m', 'fixture') | Out-Null
        Invoke-Git $repo @('branch', 'canary') | Out-Null
        Invoke-Git $repo @('worktree', 'add', '--', $worktree, 'canary') | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $worktree 'private')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $worktree 'private\unique.bin'), 'unique-private-payload', [Text.UTF8Encoding]::new($false))
        New-Item -ItemType Junction -Path $junction -Target $external -ErrorAction Stop | Out-Null
        New-Item -ItemType Junction -Path $internalJunction -Target (Join-Path $worktree 'private') -ErrorAction Stop | Out-Null
        $worktreeId = Get-WorktreeId $worktree
        foreach ($step in @('Archive', 'Retire')) {
            $stderrPath = [IO.Path]::GetTempFileName()
            try {
                $oldPreference = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                $output = @(& powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PSCommandPath -Action $step -Id $worktreeId -RepositoryRoot $repo 2> $stderrPath | ForEach-Object { $_.ToString() })
                $code = $LASTEXITCODE
                $errors = @([IO.File]::ReadAllLines($stderrPath))
            }
            finally {
                $ErrorActionPreference = $oldPreference
                [IO.File]::Delete($stderrPath)
            }
            if ($code -ne 0) { throw "Self-test $step failed: $((@($errors) + @($output)) -join [Environment]::NewLine)" }
        }
        if (-not [IO.File]::Exists((Join-Path $external 'sentinel.txt'))) { throw 'External canary was deleted.' }
        if ([IO.Directory]::Exists($worktree)) { throw 'Fixture worktree remained after retirement.' }
        $archiveFile = Join-Path $repo ('workspace\archive\safe-worktrees\' + $worktreeId + '\files\private\unique.bin')
        if (-not [IO.File]::Exists($archiveFile)) { throw 'Unique physical payload was not archived.' }
        Write-Output 'SAFE_WORKTREE_RETIRE_SELF_TEST PASS external_canary=true internal_alias=true physical_payload=true'
    }
    finally {
        if ([IO.Directory]::Exists($junction)) { [IO.Directory]::Delete($junction, $false) }
        if ([IO.Directory]::Exists($internalJunction)) { [IO.Directory]::Delete($internalJunction, $false) }
        if ([IO.Directory]::Exists($testRoot)) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    }
}

if ($Action -ceq 'SelfTest') {
    Invoke-SelfTest
    exit 0
}

$invocationRoot = if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    ((Invoke-Git (Get-Location).Path @('rev-parse', '--show-toplevel')).Lines -join '').Trim()
} else { [IO.Path]::GetFullPath($RepositoryRoot) }
$mainRoot = Get-MainRoot $invocationRoot
$row = Get-WorktreeRow $mainRoot $Id
$archiveParent = Join-Path $mainRoot 'workspace\archive\safe-worktrees'
[IO.Directory]::CreateDirectory($archiveParent) | Out-Null
$archiveRoot = Join-Path $archiveParent $row.id

if ($Action -ceq 'Plan') {
    $snapshot = Get-PhysicalSnapshot $row.path
    $planPath = Join-Path $mainRoot ('workspace\logs\P0-RETENTION-001\' + $row.id + '-safe-plan.json')
    Write-Json $planPath ([ordered]@{
        schema = 'openbfme.safe-worktree-retirement-plan'
        schemaVersion = 1
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        worktree = $row
        physicalPayloadFiles = $snapshot.fileCount
        physicalPayloadBytes = $snapshot.bytes
        files = $snapshot.files
        reparses = $snapshot.reparses
    })
    Write-Output "SAFE_WORKTREE_RETIRE_PLAN PASS id=$($row.id) files=$($snapshot.fileCount) bytes=$($snapshot.bytes) reparses=$($snapshot.reparses.Count) out=$planPath"
    exit 0
}

if ($Action -ceq 'Archive') {
    if ([IO.Directory]::Exists($archiveRoot)) {
        if ([IO.File]::Exists((Join-Path $archiveRoot 'complete.json'))) { throw 'Completed safe archive already exists.' }
        Remove-TreeWithoutReparse $archiveRoot
    }
    [IO.Directory]::CreateDirectory((Join-Path $archiveRoot 'files')) | Out-Null
    $snapshot = Get-PhysicalSnapshot $row.path -HashFiles -CopyRoot (Join-Path $archiveRoot 'files')
    $metadata = [ordered]@{
        schema = 'openbfme.safe-worktree-archive'
        schemaVersion = 1
        archivedAtUtc = [DateTime]::UtcNow.ToString('o')
        worktree = $row
        physicalPayloadFiles = $snapshot.fileCount
        physicalPayloadBytes = $snapshot.bytes
        stateSha256 = $snapshot.stateSha256
        files = $snapshot.files
        reparses = $snapshot.reparses
    }
    Write-Json (Join-Path $archiveRoot 'metadata.json') $metadata
    $fresh = Get-PhysicalSnapshot $row.path -HashFiles
    if ($fresh.stateSha256 -cne $snapshot.stateSha256) { throw 'Physical worktree state changed during archive.' }
    Write-Json (Join-Path $archiveRoot 'complete.json') ([ordered]@{
        id = $row.id
        head = $row.head
        stateSha256 = $snapshot.stateSha256
        result = 'PASS'
        artifacts = @(Get-ArchiveManifest $archiveRoot)
    })
    Write-Output "SAFE_WORKTREE_ARCHIVE PASS id=$($row.id) state=$($snapshot.stateSha256) files=$($snapshot.fileCount) bytes=$($snapshot.bytes) reparses=$($snapshot.reparses.Count)"
    exit 0
}

$completePath = Join-Path $archiveRoot 'complete.json'
$metadataPath = Join-Path $archiveRoot 'metadata.json'
if (-not [IO.File]::Exists($completePath) -or -not [IO.File]::Exists($metadataPath)) { throw 'Safe archive is incomplete.' }
$complete = Get-Content -Raw -LiteralPath $completePath | ConvertFrom-Json
$metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
if ($complete.result -cne 'PASS' -or $complete.id -cne $row.id -or $complete.head -cne $row.head) { throw 'Safe archive identity does not match the live worktree.' }
$recordedManifest = ConvertTo-Json -InputObject @($complete.artifacts) -Depth 6 -Compress
$actualManifest = ConvertTo-Json -InputObject @(Get-ArchiveManifest $archiveRoot) -Depth 6 -Compress
if ($recordedManifest -cne $actualManifest) { throw 'Safe archive artifact bytes failed validation.' }
$fresh = Get-PhysicalSnapshot $row.path -HashFiles
if ($fresh.stateSha256 -cne $complete.stateSha256 -or $fresh.stateSha256 -cne $metadata.stateSha256) { throw 'Physical worktree state changed after archive.' }
$recordedReparses = @($metadata.reparses)
Assert-Fingerprints $recordedReparses
Unlink-Reparses $row.path $recordedReparses
Assert-Fingerprints $recordedReparses
$remainingReparses = @((Get-PhysicalSnapshot $row.path).reparses)
if ($remainingReparses.Count -ne 0) { throw 'A descendant reparse point remains after unlink.' }
Remove-TreeWithoutReparse $row.path
Assert-Fingerprints $recordedReparses -ExternalOnly
Invoke-Git $mainRoot @('worktree', 'prune', '--expire=now') | Out-Null
if ($null -ne $row.branch -and $row.branch.StartsWith('refs/heads/')) {
    Invoke-Git $mainRoot @('branch', '-d', '--', $row.branch.Substring(11)) | Out-Null
}
$registered = @((Invoke-Git $mainRoot @('worktree', 'list', '--porcelain')).Lines | Where-Object { $_ -ceq ('worktree ' + $row.path) })
if ($registered.Count -ne 0 -or [IO.Directory]::Exists($row.path)) { throw 'Worktree remained after safe retirement.' }
Write-Output "SAFE_WORKTREE_RETIRE PASS id=$($row.id) external_targets=$($recordedReparses.Count)"
