[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('inventory', 'archive', 'remove')]
    [string] $Action = 'inventory',
    [string] $Id,
    [string] $Out
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

function Get-Hash([string] $Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

function Write-Json([string] $Path, [object] $Value) {
    $parent = [IO.Path]::GetDirectoryName($Path)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $text = ConvertTo-Json -InputObject $Value -Depth 12
    [IO.File]::WriteAllText($Path, $text + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

function Get-ArchiveManifest([string] $Root) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $records = [Collections.Generic.List[object]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Where-Object { $_.Name -cne 'complete.json' } | Sort-Object FullName)) {
        Assert-NoReparsePath $Root $file.FullName
        $records.Add([pscustomobject][ordered]@{
            path = $file.FullName.Substring($rootFull.Length).Replace('\', '/')
            length = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    return @($records.ToArray())
}

function Get-SafeRemovalOrder([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Archive retry encountered a reparse point.' }
    $items = [Collections.Generic.List[object]]::new()
    if ($item.PSIsContainer) {
        foreach ($child in @(Get-ChildItem -LiteralPath $Path -Force)) {
            foreach ($nested in @(Get-SafeRemovalOrder $child.FullName)) { $items.Add($nested) }
        }
    }
    $items.Add($item)
    return @($items.ToArray())
}

function Get-MainRoot([string] $Root) {
    $current = $null
    $matches = [Collections.Generic.List[string]]::new()
    foreach ($line in (Invoke-Git $Root @('worktree', 'list', '--porcelain')).Lines) {
        if ($line.StartsWith('worktree ')) { $current = $line.Substring(9) }
        elseif ($line -ceq 'branch refs/heads/main') { $matches.Add($current) }
    }
    if ($matches.Count -ne 1) { throw 'Expected exactly one main worktree.' }
    return [IO.Path]::GetFullPath($matches[0])
}

function Assert-NoReparsePath([string] $Root, [string] $Path) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $cursor = [IO.Path]::GetFullPath($Path)
    if (-not ($cursor -ceq $rootFull) -and -not $cursor.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped its allowed root.' }
    while ($cursor.Length -ge $rootFull.Length) {
        if ([IO.File]::Exists($cursor) -or [IO.Directory]::Exists($cursor)) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Path traverses a reparse point.' }
        }
        if ($cursor -ceq $rootFull) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Get-Inventory([string] $MainRoot) {
    $mainHead = ((Invoke-Git $MainRoot @('rev-parse', '--verify', 'main^{commit}')).Lines -join '').Trim()
    $raw = [Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in (Invoke-Git $MainRoot @('worktree', 'list', '--porcelain')).Lines + '') {
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($null -ne $current) { $raw.Add([pscustomobject]$current); $current = $null }
        }
        elseif ($line.StartsWith('worktree ')) { $current = [ordered]@{ path = $line.Substring(9) } }
        elseif ($line.StartsWith('HEAD ')) { $current.head = $line.Substring(5) }
        elseif ($line.StartsWith('branch ')) { $current.branch = $line.Substring(7) }
        elseif ($line -ceq 'detached') { $current.detached = $true }
        elseif ($line.StartsWith('prunable')) { $current.prunable = $true }
        elseif ($line.StartsWith('locked')) { $current.locked = $true }
    }
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($entry in $raw) {
        $path = [IO.Path]::GetFullPath([string]$entry.path)
        $isMain = [StringComparer]::OrdinalIgnoreCase.Equals($path, $MainRoot)
        $exists = [IO.Directory]::Exists($path)
        $status = @(); $untracked = @(); $ignored = @()
        $mergeResult = Invoke-Git $MainRoot @('merge-base', '--is-ancestor', $entry.head, $mainHead) -AllowFailure
        $merged = $mergeResult.Code -eq 0
        if ($exists) {
            $statusResult = Invoke-Git $path @('status', '--porcelain=v1', '--untracked-files=all')
            $untrackedResult = Invoke-Git $path @('ls-files', '--others', '--exclude-standard')
            $ignoredResult = Invoke-Git $path @('ls-files', '--others', '-i', '--exclude-standard')
            $status = @($statusResult.Lines)
            $untracked = @($untrackedResult.Lines)
            $ignored = @($ignoredResult.Lines)
        }
        $branch = if ($entry.PSObject.Properties.Name -contains 'branch') { [string]$entry.branch } else { $null }
        $locked = $entry.PSObject.Properties.Name -contains 'locked'
        $classification = if ($isMain) { 'main' }
            elseif ($locked) { 'preserve-locked' }
            elseif (-not $exists -and $merged) { 'removable-missing' }
            elseif (-not $exists) { 'preserve-missing' }
            elseif (-not $merged) { 'preserve-unmerged' }
            elseif ($status.Count -gt 0 -or $untracked.Count -gt 0) { 'preserve-dirty' }
            elseif ($ignored.Count -gt 0) { 'preserve-ignored' }
            else { 'removable' }
        $stateText = @('H:' + $entry.head, 'B:' + $branch, 'S:' + ($status -join "`n"), 'U:' + ($untracked -join "`n"), 'I:' + ($ignored -join "`n")) -join "`n"
        $rows.Add([pscustomobject][ordered]@{
            id = 'wt-' + (Get-Hash $path.ToLowerInvariant()).Substring(0, 16)
            path = $path
            head = [string]$entry.head
            branch = $branch
            locked = $locked
            exists = $exists
            mergedToMain = $merged
            dirtyCount = $status.Count
            untrackedCount = $untracked.Count
            ignoredCount = $ignored.Count
            classification = $classification
            stateSha256 = Get-Hash $stateText
        })
    }
    return [pscustomobject][ordered]@{
        schema = 'openbfme.worktree-inventory'
        schemaVersion = 1
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        mainHead = $mainHead
        total = $rows.Count
        worktrees = @($rows.ToArray())
    }
}

function Find-Row([object] $Inventory, [string] $WorktreeId) {
    if ([string]::IsNullOrWhiteSpace($WorktreeId)) { throw "$Action requires -Id." }
    $matches = @($Inventory.worktrees | Where-Object { $_.id -ceq $WorktreeId })
    if ($matches.Count -ne 1) { throw "Unknown or ambiguous worktree id: $WorktreeId" }
    return $matches[0]
}

function Copy-Payload([string] $Root, [string] $Destination) {
    $paths = @((Invoke-Git $Root @('ls-files', '--others', '--exclude-standard')).Lines) +
        @((Invoke-Git $Root @('ls-files', '--others', '-i', '--exclude-standard')).Lines)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    foreach ($relative in @($paths | Sort-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($relative)) { continue }
        $source = [IO.Path]::GetFullPath((Join-Path $Root $relative))
        if (-not $source.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { throw 'Payload path escaped its worktree.' }
        Assert-NoReparsePath $Root $source
        $item = Get-Item -LiteralPath $source -Force
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unsafe payload path: $relative" }
        $target = Join-Path $Destination ('files\' + $relative.Replace('/', '\'))
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
    return @($paths | Sort-Object -Unique)
}

$invocationRoot = ((Invoke-Git (Get-Location).Path @('rev-parse', '--show-toplevel')).Lines -join '').Trim()
$mainRoot = Get-MainRoot $invocationRoot
$inventory = Get-Inventory $mainRoot
if ([string]::IsNullOrWhiteSpace($Out)) { $Out = Join-Path $mainRoot 'workspace\logs\P0-REPO-001\worktrees.json' }
$outFull = [IO.Path]::GetFullPath($Out)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $mainRoot 'workspace')) + '\'
if (-not $outFull.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Out must remain below main workspace.' }
Assert-NoReparsePath $mainRoot $outFull

if ($Action -ceq 'inventory') {
    Write-Json $outFull $inventory
    Write-Output "WORKTREE_INVENTORY PASS total=$($inventory.total) out=$outFull"
    exit 0
}

$row = Find-Row $inventory $Id
$archiveParent = Join-Path $mainRoot 'workspace\archive\worktrees'
[IO.Directory]::CreateDirectory($archiveParent) | Out-Null
Assert-NoReparsePath $mainRoot $archiveParent
$archiveRoot = Join-Path $archiveParent $row.id
if ($Action -ceq 'archive') {
    if ($row.classification -ceq 'main') { throw 'Main worktree cannot be archived by this action.' }
    if ([IO.Directory]::Exists($archiveRoot)) {
        if ([IO.File]::Exists((Join-Path $archiveRoot 'complete.json'))) { throw 'A completed archive already exists.' }
        Assert-NoReparsePath $archiveParent $archiveRoot
        foreach ($item in @(Get-SafeRemovalOrder $archiveRoot)) {
            if ($item.PSIsContainer) { [IO.Directory]::Delete($item.FullName) }
            else { $item.Attributes = [IO.FileAttributes]::Normal; [IO.File]::Delete($item.FullName) }
        }
    }
    [IO.Directory]::CreateDirectory($archiveRoot) | Out-Null
    $patchPath = Join-Path $archiveRoot 'tracked.patch'
    if ($row.exists) {
        Invoke-Git $row.path @('diff', '--binary', '--no-renames', ('--output=' + $patchPath), 'HEAD', '--') | Out-Null
        $payload = @(Copy-Payload $row.path $archiveRoot)
    } else { [IO.File]::Create($patchPath).Dispose(); $payload = @() }
    if (-not $row.mergedToMain) {
        if ($null -ne $row.branch) {
            Invoke-Git $mainRoot @('bundle', 'create', (Join-Path $archiveRoot 'history.bundle'), $row.branch) | Out-Null
        } elseif ($row.exists) {
            Invoke-Git $row.path @('bundle', 'create', (Join-Path $archiveRoot 'history.bundle'), 'HEAD') | Out-Null
        } else {
            $temporaryRef = 'refs/openbfme-archive/' + $row.id
            $existingRef = Invoke-Git $mainRoot @('rev-parse', '--verify', '--quiet', $temporaryRef) -AllowFailure
            if ($existingRef.Code -eq 0 -and (($existingRef.Lines -join '').Trim() -cne $row.head)) { throw 'Detached archive reference collision.' }
            if ($existingRef.Code -ne 0) { Invoke-Git $mainRoot @('update-ref', $temporaryRef, $row.head, ('0' * 40)) | Out-Null }
            try { Invoke-Git $mainRoot @('bundle', 'create', (Join-Path $archiveRoot 'history.bundle'), $temporaryRef) | Out-Null }
            finally { Invoke-Git $mainRoot @('update-ref', '-d', $temporaryRef, $row.head) | Out-Null }
        }
    }
    $metadata = [ordered]@{ inventoryRow = $row; payloadPaths = $payload; archivedAtUtc = [DateTime]::UtcNow.ToString('o') }
    Write-Json (Join-Path $archiveRoot 'metadata.json') $metadata
    $freshRow = Find-Row (Get-Inventory $mainRoot) $row.id
    if ($freshRow.stateSha256 -cne $row.stateSha256 -or $freshRow.classification -cne $row.classification) { throw 'Worktree changed during archive.' }
    Write-Json (Join-Path $archiveRoot 'complete.json') ([ordered]@{
        id = $row.id; stateSha256 = $row.stateSha256; result = 'PASS'; artifacts = @(Get-ArchiveManifest $archiveRoot)
    })
    Write-Output "WORKTREE_ARCHIVE PASS id=$($row.id) state=$($row.stateSha256)"
    exit 0
}

if ($row.classification -ceq 'main') { throw 'Main worktree removal is forbidden.' }
$archiveComplete = Join-Path $archiveRoot 'complete.json'
if ($row.classification -notin @('removable', 'removable-missing')) {
    if (-not [IO.File]::Exists($archiveComplete)) { throw 'Non-removable worktree requires a completed archive.' }
    $complete = Get-Content -Raw -LiteralPath $archiveComplete | ConvertFrom-Json
    if ($complete.id -cne $row.id -or $complete.stateSha256 -cne $row.stateSha256 -or $complete.result -cne 'PASS') { throw 'Archive does not match current worktree state.' }
    $metadataPath = Join-Path $archiveRoot 'metadata.json'
    if (-not [IO.File]::Exists($metadataPath) -or -not [IO.File]::Exists((Join-Path $archiveRoot 'tracked.patch'))) { throw 'Archive is incomplete.' }
    $metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
    if ($metadata.inventoryRow.stateSha256 -cne $row.stateSha256) { throw 'Archive metadata differs from current worktree.' }
    if (-not $row.mergedToMain -and -not [IO.File]::Exists((Join-Path $archiveRoot 'history.bundle'))) { throw 'Unmerged history bundle is missing.' }
    $recordedManifest = ConvertTo-Json -InputObject @($complete.artifacts) -Depth 5 -Compress
    $actualManifest = ConvertTo-Json -InputObject @(Get-ArchiveManifest $archiveRoot) -Depth 5 -Compress
    if ($recordedManifest -cne $actualManifest) { throw 'Archive artifact bytes failed validation.' }
}
$freshRow = Find-Row (Get-Inventory $mainRoot) $row.id
if ($freshRow.stateSha256 -cne $row.stateSha256 -or $freshRow.classification -cne $row.classification) { throw 'Worktree changed before removal.' }
if ($freshRow.exists) { Assert-NoReparsePath $freshRow.path $freshRow.path }
$removeArgs = @('worktree', 'remove', '--force')
if ($row.locked) { $removeArgs += '--force' }
Invoke-Git $mainRoot ($removeArgs + @('--', $row.path)) | Out-Null
if ($null -ne $row.branch -and $row.branch.StartsWith('refs/heads/')) {
    $branchName = $row.branch.Substring(11)
    $deleteFlag = if ($row.mergedToMain) { '-d' } else { '-D' }
    Invoke-Git $mainRoot @('branch', $deleteFlag, '--', $branchName) | Out-Null
}
$remaining = Get-Inventory $mainRoot
if (@($remaining.worktrees | Where-Object { $_.id -ceq $row.id }).Count -ne 0) { throw 'Worktree remained registered after removal.' }
Write-Json $outFull $remaining
Write-Output "WORKTREE_REMOVE PASS id=$($row.id)"
