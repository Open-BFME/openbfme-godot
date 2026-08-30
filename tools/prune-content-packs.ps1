[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$Execute,
    [ValidatePattern('^[a-f0-9]{64}$')]
    [string]$PlanSha256,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
$InformationPreference = "Continue"
Set-StrictMode -Version 2.0

$actionCount = @(@($DryRun, $Execute, $SelfTest) | Where-Object { $_ }).Count
if ($actionCount -ne 1) { throw "Choose exactly one action: -DryRun, -Execute, or -SelfTest." }
if ($Execute -and -not $PlanSha256) { throw "-Execute requires -PlanSha256 <64-lowercase-hex>." }
if (-not $Execute -and $PlanSha256) { throw "-PlanSha256 is valid only with -Execute." }

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:DigestPattern = '^[a-f0-9]{64}$'
$script:AddressPattern = '^[a-z0-9][a-z0-9-]*/[a-f0-9]{64}$'
$script:PackIdPattern = '^[a-z0-9][a-z0-9-]*$'

function Get-Sha256Bytes {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Get-Sha256File {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [IO.File]::Exists($Path)) { throw "Required file is missing: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Write-Utf8JsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Document)
    $parent = Split-Path -Parent $Path
    if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    $json = ($Document | ConvertTo-Json -Depth 30) + "`n"
    $temporary = "$Path.tmp-$PID"
    [IO.File]::WriteAllText($temporary, $json, $script:Utf8NoBom)
    if ([IO.File]::Exists($Path)) {
        $backup = "$Path.replace-backup-$PID"
        if ([IO.File]::Exists($backup)) { throw "Atomic JSON backup path already exists: $backup" }
        [IO.File]::Replace($temporary, $Path, $backup, $true)
        [IO.File]::Delete($backup)
    }
    else { [IO.File]::Move($temporary, $Path) }
}

function Convert-DocumentToUtf8Json {
    param([Parameter(Mandatory = $true)]$Document)
    return ($Document | ConvertTo-Json -Depth 30) + "`n"
}

function Get-NormalizedDirectoryPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-DirectoryNotReparse {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)
    if (-not [IO.Directory]::Exists($Path)) { throw "$Label directory is missing: $Path" }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$Label directory is a reparse point: $Path" }
}

function Assert-ExactChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Child,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $rootFull = Get-NormalizedDirectoryPath $Root
    $childFull = [IO.Path]::GetFullPath($Child)
    $parentFull = Get-NormalizedDirectoryPath (Split-Path -Parent $childFull)
    if (-not [string]::Equals($rootFull, $parentFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is not an exact child of its approved root: root=$rootFull child=$childFull"
    }
}

function Assert-SafeArchiveParent {
    param(
        [Parameter(Mandatory = $true)][string]$Parent,
        [Parameter(Mandatory = $true)][string]$ArchiveRoot,
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )
    $parentFull = Get-NormalizedDirectoryPath $Parent
    $archiveFull = Get-NormalizedDirectoryPath $ArchiveRoot
    $repoFull = Get-NormalizedDirectoryPath $RepoRoot
    $archivePrefix = $archiveFull + [IO.Path]::DirectorySeparatorChar
    $repoPrefix = $repoFull + [IO.Path]::DirectorySeparatorChar
    if (-not [string]::Equals($parentFull, $archiveFull, [StringComparison]::OrdinalIgnoreCase) -and
        -not $parentFull.StartsWith($archivePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive parent escaped the approved archive root: $parentFull"
    }
    if (-not $archiveFull.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive root escaped the repository: $archiveFull"
    }

    # Check every already-existing ancestor before creating a directory. This
    # prevents CreateDirectory from traversing a junction into another tree.
    $cursor = $parentFull
    while (-not [IO.Directory]::Exists($cursor)) {
        $next = Split-Path -Parent $cursor
        if (-not $next -or [string]::Equals($next, $cursor, [StringComparison]::OrdinalIgnoreCase)) {
            throw "No existing safe ancestor for archive parent: $parentFull"
        }
        $cursor = Get-NormalizedDirectoryPath $next
    }
    while ($true) {
        Assert-DirectoryNotReparse -Path $cursor -Label 'archive ancestor'
        if ([string]::Equals($cursor, $repoFull, [StringComparison]::OrdinalIgnoreCase)) { break }
        if (-not $cursor.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Archive ancestor escaped the repository: $cursor"
        }
        $cursor = Get-NormalizedDirectoryPath (Split-Path -Parent $cursor)
    }
}

function Get-SafeFileRows {
    param([Parameter(Mandatory = $true)][string]$Root)
    Assert-DirectoryNotReparse -Path $Root -Label "bundle"
    $rootFull = Get-NormalizedDirectoryPath $Root
    $stack = New-Object 'Collections.Generic.Stack[string]'
    $stack.Push($rootFull)
    $rows = @()
    while ($stack.Count -gt 0) {
        $current = $stack.Pop()
        foreach ($entry in @(Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop)) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Bundle contains a reparse point: $($entry.FullName)"
            }
            if ($entry.PSIsContainer) { $stack.Push($entry.FullName); continue }
            if (-not [IO.File]::Exists($entry.FullName)) {
                throw "Bundle contains a non-regular filesystem entry: $($entry.FullName)"
            }
            $rows += [pscustomobject][ordered]@{
                relativePath = $entry.FullName.Substring($rootFull.Length + 1).Replace('\', '/')
                bytes = [long]$entry.Length
                sha256 = Get-Sha256File -Path $entry.FullName
            }
        }
    }
    $byPath = @{}
    foreach ($row in $rows) {
        if ($byPath.ContainsKey($row.relativePath)) { throw "Duplicate bundle-relative path: $($row.relativePath)" }
        $byPath[$row.relativePath] = $row
    }
    $orderedPaths = @($byPath.Keys)
    [Array]::Sort($orderedPaths, [StringComparer]::Ordinal)
    return @($orderedPaths | ForEach-Object { $byPath[$_] })
}

function Get-BundleTreeIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)
    $rows = @(Get-SafeFileRows -Root $Path)
    $fold = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($row in $rows) {
            $bytes = $script:Utf8NoBom.GetBytes("$($row.relativePath)`0$($row.bytes)`0$($row.sha256)`n")
            [void]$fold.TransformBlock($bytes, 0, $bytes.Length, $bytes, 0)
        }
        [void]$fold.TransformFinalBlock((New-Object byte[] 0), 0, 0)
        $digest = ([BitConverter]::ToString($fold.Hash)).Replace('-', '').ToLowerInvariant()
    }
    finally { $fold.Dispose() }
    return [pscustomobject][ordered]@{
        bundleSha256 = $digest
        fileCount = $rows.Count
        bytes = [long](($rows | Measure-Object -Property bytes -Sum).Sum)
    }
}

function Get-BundleRecord {
    param(
        [Parameter(Mandatory = $true)][string]$RootRole,
        [Parameter(Mandatory = $true)][string]$PackId,
        [Parameter(Mandatory = $true)][string]$Digest,
        [Parameter(Mandatory = $true)][string]$Path
    )
    if ($PackId -cnotmatch $script:PackIdPattern) { throw "Malformed pack id in ${RootRole}: $PackId" }
    if ($Digest -cnotmatch $script:DigestPattern) { throw "Malformed digest directory in ${RootRole}/${PackId}: $Digest" }
    $manifestPath = Join-Path $Path 'pack.json'
    if (-not [IO.File]::Exists($manifestPath)) { throw "Bundle is missing pack.json: $Path" }
    try { $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Bundle has unreadable pack.json at ${Path}: $($_.Exception.Message)" }
    if (-not [string]::Equals([string]$manifest.id, $PackId, [StringComparison]::Ordinal)) {
        throw "pack.json id mismatch at ${Path}: found='$($manifest.id)' expected='$PackId'"
    }
    $identity = Get-BundleTreeIdentity -Path $Path
    if ($identity.bundleSha256 -cne $Digest) {
        throw "Content address mismatch at ${Path}: declared=$Digest actual=$($identity.bundleSha256)"
    }
    return [pscustomobject][ordered]@{
        rootRole = $RootRole; address = "$PackId/$Digest"; packId = $PackId; digest = $Digest
        path = Get-NormalizedDirectoryPath $Path
        fileCount = $identity.fileCount; bytes = $identity.bytes; bundleSha256 = $identity.bundleSha256
    }
}

function Read-Selection {
    param([Parameter(Mandatory = $true)][string]$RootRole, [Parameter(Mandatory = $true)][string]$RootPath)
    $selectionPath = Join-Path $RootPath 'selection.json'
    if (-not [IO.File]::Exists($selectionPath)) { throw "$RootRole selection is missing: $selectionPath" }
    try { $document = Get-Content -LiteralPath $selectionPath -Raw | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "$RootRole selection is unreadable: $($_.Exception.Message)" }
    if ([string]$document.schema -ne 'openbfme.pack-selection' -or [int]$document.schemaVersion -ne 0) {
        throw "$RootRole selection has an unsupported schema."
    }
    $entries = @([string]$document.activePack) + @($document.supplementalPacks | ForEach-Object { [string]$_ })
    if ($entries.Count -eq 0) { throw "$RootRole selection is empty." }
    $seen = @{}
    foreach ($entry in $entries) {
        if ($entry -cnotmatch $script:AddressPattern) { throw "$RootRole selection has malformed address: $entry" }
        if ($seen.ContainsKey($entry)) { throw "$RootRole selection has duplicate address: $entry" }
        $seen[$entry] = $true
    }
    return [pscustomobject][ordered]@{
        path = [IO.Path]::GetFullPath($selectionPath)
        sha256 = Get-Sha256File -Path $selectionPath
        entries = $entries
    }
}

function Get-RootInventory {
    param([Parameter(Mandatory = $true)][string]$RootRole, [Parameter(Mandatory = $true)][string]$RootPath)
    Assert-DirectoryNotReparse -Path $RootPath -Label "$RootRole pack root"
    $rootFull = Get-NormalizedDirectoryPath $RootPath
    $selection = Read-Selection -RootRole $RootRole -RootPath $rootFull
    $rootFiles = @(); $bundleMap = @{}
    foreach ($entry in @(Get-ChildItem -LiteralPath $rootFull -Force -ErrorAction Stop)) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$RootRole pack root contains a reparse point: $($entry.FullName)"
        }
        if (-not $entry.PSIsContainer) {
            $rootFiles += [pscustomobject][ordered]@{ name = $entry.Name; bytes = [long]$entry.Length; sha256 = Get-Sha256File $entry.FullName }
            continue
        }
        $packId = $entry.Name
        if ($packId -cnotmatch $script:PackIdPattern) { throw "$RootRole has malformed pack directory: $packId" }
        foreach ($child in @(Get-ChildItem -LiteralPath $entry.FullName -Force -ErrorAction Stop)) {
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Pack contains reparse point: $($child.FullName)" }
            if (-not $child.PSIsContainer) { throw "Pack directory contains file outside bundle: $($child.FullName)" }
            Assert-ExactChildPath -Root $entry.FullName -Child $child.FullName -Label 'bundle'
            $record = Get-BundleRecord -RootRole $RootRole -PackId $packId -Digest $child.Name -Path $child.FullName
            if ($bundleMap.ContainsKey($record.address)) { throw "$RootRole has duplicate bundle address: $($record.address)" }
            $bundleMap[$record.address] = $record
        }
    }
    foreach ($selected in $selection.entries) {
        if (-not $bundleMap.ContainsKey($selected)) { throw "$RootRole selected bundle is missing: $selected" }
    }
    $names = @($rootFiles | ForEach-Object { $_.name }); [Array]::Sort($names, [StringComparer]::Ordinal)
    $rootFileMap = @{}; foreach ($row in $rootFiles) { $rootFileMap[$row.name] = $row }
    return [pscustomobject][ordered]@{
        role = $RootRole; path = $rootFull; selection = $selection
        rootFiles = @($names | ForEach-Object { $rootFileMap[$_] }); bundles = $bundleMap
    }
}

function Resolve-RepoRelativeFile {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ([IO.Path]::IsPathRooted($RelativePath) -or $RelativePath.Contains('\') -or $RelativePath -match '(^|/)\.\.(/|$)') {
        throw "$Label must be a repository-relative forward-slash path: $RelativePath"
    }
    $full = [IO.Path]::GetFullPath((Join-Path $RepoRoot $RelativePath.Replace('/', '\')))
    $prefix = (Get-NormalizedDirectoryPath $RepoRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "$Label escapes repository: $RelativePath" }
    if (-not [IO.File]::Exists($full)) { throw "$Label is missing: $RelativePath" }
    return $full
}

function Read-ProtectionInput {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ProtectionPath,
        [Parameter(Mandatory = $true)]$InventoryByRole
    )
    if (-not [IO.File]::Exists($ProtectionPath)) { return $null }
    try { $document = Get-Content -LiteralPath $ProtectionPath -Raw | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Protection input is unreadable: $($_.Exception.Message)" }
    if ([string]$document.schema -ne 'openbfme.content-retention-protection' -or [int]$document.schemaVersion -ne 1) {
        throw "Protection input has an unsupported schema."
    }
    $validated = @(); $seen = @{}
    foreach ($entry in @($document.entries)) {
        $role = [string]$entry.rootRole; $address = [string]$entry.bundleAddress
        $reason = [string]$entry.reasonCode; $evidencePath = [string]$entry.evidencePath
        $evidenceSha = [string]$entry.evidenceSha256
        if (-not $InventoryByRole.ContainsKey($role)) { throw "Protection names unknown rootRole: $role" }
        if ($address -cnotmatch $script:AddressPattern) { throw "Protection has malformed bundleAddress: $address" }
        if ($reason -notin @('last-known-good', 'accepted-evidence', 'current-distribution')) { throw "Unsupported protection reasonCode: $reason" }
        if ($evidenceSha -cnotmatch $script:DigestPattern) { throw "Malformed protection evidenceSha256: $evidenceSha" }
        $evidenceFull = Resolve-RepoRelativeFile -RepoRoot $RepoRoot -RelativePath $evidencePath -Label 'protection evidence'
        $observed = Get-Sha256File $evidenceFull
        if ($observed -cne $evidenceSha) { throw "Protection evidence drift: $evidencePath expected=$evidenceSha actual=$observed" }
        if (-not $InventoryByRole[$role].bundles.ContainsKey($address)) { throw "Explicit protected bundle is missing: $role|$address" }
        $key = "$role|$address|$reason|$evidencePath|$evidenceSha"
        if ($seen.ContainsKey($key)) { throw "Duplicate protection entry: $key" }; $seen[$key] = $true
        $validated += [pscustomobject][ordered]@{
            rootRole = $role; bundleAddress = $address; reasonCode = $reason
            evidencePath = $evidencePath; evidenceSha256 = $evidenceSha
        }
    }
    return [pscustomobject][ordered]@{
        path = [IO.Path]::GetFullPath($ProtectionPath); sha256 = Get-Sha256File $ProtectionPath; entries = $validated
    }
}

function Get-GitState {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $revision = (& git -C $RepoRoot rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $revision) { throw "Unable to resolve repository revision." }
    $status = @(& git -C $RepoRoot status --porcelain=v1 --untracked-files=no 2>$null)
    if ($LASTEXITCODE -ne 0) { throw "Unable to resolve repository dirty state." }
    if ($status.Count -gt 0) {
        throw "Retention planning and execution require a clean tracked worktree; commit or remove tracked drift first."
    }
    return [pscustomobject][ordered]@{ revision = ([string]$revision).Trim(); trackedClean = $true }
}

function Get-ProtectedState {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $locationSpecs = @(
        @{ role = 'source-material'; path = 'workspace\retail-extract' },
        @{ role = 'source-and-provenance'; path = 'workspace\retail-work' },
        @{ role = 'oracle'; path = 'workspace\retail-oracle' }
    )
    $locations = @()
    foreach ($spec in $locationSpecs) {
        $full = Join-Path $RepoRoot $spec.path; Assert-DirectoryNotReparse -Path $full -Label $spec.role
        $locations += [pscustomobject][ordered]@{ role = $spec.role; path = Get-NormalizedDirectoryPath $full }
    }
    $specs = @(
        @{ role = 'baseline'; path = 'contracts/rotwk-202-v9.7.7-baseline.json'; required = $true },
        @{ role = 'effective-tree'; path = 'workspace/retail-work/reports/compatibility/rotwk-202-v9.7.7-effective-tree.json'; required = $true },
        @{ role = 'feature-graph'; path = 'workspace/retail-work/reports/compatibility/rotwk-202-v9.7.7-feature-graph.json'; required = $true },
        @{ role = 'current-oracle'; path = 'workspace/retail-oracle/rotwk-201-baseline.local.json'; required = $false }
    )
    $sentinels = @()
    foreach ($spec in $specs) {
        $full = [IO.Path]::GetFullPath((Join-Path $RepoRoot $spec.path.Replace('/', '\')))
        if (-not [IO.File]::Exists($full)) { if ($spec.required) { throw "Required sentinel missing: $($spec.path)" }; continue }
        $item = Get-Item -LiteralPath $full -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Sentinel is reparse point: $full" }
        $sentinels += [pscustomobject][ordered]@{
            role = $spec.role; path = $spec.path; bytes = [long]$item.Length; sha256 = Get-Sha256File $full
        }
    }
    return [pscustomobject][ordered]@{ locations = $locations; sentinels = $sentinels }
}

function New-RetentionPlan {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$WorkspacePacksRoot,
        [Parameter(Mandatory = $true)][string]$DurablePacksRoot,
        [Parameter(Mandatory = $true)][string]$ArchiveRoot,
        [Parameter(Mandatory = $true)][string]$ProtectionPath,
        [Parameter(Mandatory = $true)][string]$ScriptPath
    )
    $workspace = Get-RootInventory -RootRole 'workspace' -RootPath $WorkspacePacksRoot
    $durable = Get-RootInventory -RootRole 'durable' -RootPath $DurablePacksRoot
    $byRole = @{ workspace = $workspace; durable = $durable }
    $input = Read-ProtectionInput -RepoRoot $RepoRoot -ProtectionPath $ProtectionPath -InventoryByRole $byRole
    $reasons = @{}
    foreach ($root in @($workspace, $durable)) {
        foreach ($address in $root.selection.entries) { $reasons["$($root.role)|$address"] = @('current-selection') }
    }
    if ($null -ne $input) {
        foreach ($entry in $input.entries) {
            $key = "$($entry.rootRole)|$($entry.bundleAddress)"
            if (-not $reasons.ContainsKey($key)) { $reasons[$key] = @() }
            $reasons[$key] = @($reasons[$key] + $entry.reasonCode | Sort-Object -Unique)
        }
    }
    $all = @{}
    foreach ($root in @($workspace, $durable)) {
        foreach ($address in $root.bundles.Keys) { $all["$($root.role)|$address"] = $root.bundles[$address] }
    }
    foreach ($key in $reasons.Keys) { if (-not $all.ContainsKey($key)) { throw "Protected bundle missing from inventory: $key" } }
    $keys = @($all.Keys); [Array]::Sort($keys, [StringComparer]::Ordinal)
    $protected = @(); $candidates = @()
    foreach ($key in $keys) {
        $bundle = $all[$key]
        if ($reasons.ContainsKey($key)) {
            $protected += [pscustomobject][ordered]@{
                rootRole = $bundle.rootRole; address = $bundle.address; path = $bundle.path
                fileCount = $bundle.fileCount; bytes = $bundle.bytes; bundleSha256 = $bundle.bundleSha256
                reasons = @($reasons[$key])
            }
        }
        else {
            $destination = Join-Path $ArchiveRoot (Join-Path $bundle.rootRole (Join-Path $bundle.packId $bundle.digest))
            $candidates += [pscustomobject][ordered]@{
                rootRole = $bundle.rootRole; address = $bundle.address; sourcePath = $bundle.path
                archivePath = [IO.Path]::GetFullPath($destination); fileCount = $bundle.fileCount
                bytes = $bundle.bytes; bundleSha256 = $bundle.bundleSha256; reason = 'not-protected'
            }
        }
    }
    $state = Get-ProtectedState -RepoRoot $RepoRoot
    return [pscustomobject][ordered]@{
        schema = 'openbfme.content-retention-plan'; schemaVersion = 1
        git = Get-GitState -RepoRoot $RepoRoot; scriptSha256 = Get-Sha256File $ScriptPath
        approvedRoots = @(
            [pscustomobject][ordered]@{ role = 'workspace'; path = $workspace.path; selectionSha256 = $workspace.selection.sha256; rootFiles = $workspace.rootFiles },
            [pscustomobject][ordered]@{ role = 'durable'; path = $durable.path; selectionSha256 = $durable.selection.sha256; rootFiles = $durable.rootFiles }
        )
        protectedLocations = $state.locations; sentinels = $state.sentinels; protectionInput = $input
        protectedBundles = $protected; candidates = $candidates
        totals = [pscustomobject][ordered]@{
            inventoriedBundles = $keys.Count; protectedBundles = $protected.Count; candidateBundles = $candidates.Count
            candidateFiles = [long](($candidates | Measure-Object fileCount -Sum).Sum)
            candidateBytes = [long](($candidates | Measure-Object bytes -Sum).Sum)
        }
    }
}

function Assert-ProtectedState {
    param([Parameter(Mandatory = $true)]$Plan, [Parameter(Mandatory = $true)][string]$RepoRoot, [Parameter(Mandatory = $true)][string]$ScriptPath)
    if ((Get-Sha256File $ScriptPath) -cne [string]$Plan.scriptSha256) { throw "Retention script changed after review." }
    $git = Get-GitState $RepoRoot
    if ($git.revision -cne [string]$Plan.git.revision -or -not [bool]$Plan.git.trackedClean) { throw "Git state changed after review." }
    foreach ($root in @($Plan.approvedRoots)) {
        Assert-DirectoryNotReparse -Path ([string]$root.path) -Label "$($root.role) pack root"
        if ((Get-Sha256File (Join-Path ([string]$root.path) 'selection.json')) -cne [string]$root.selectionSha256) { throw "$($root.role) selection changed." }
        foreach ($file in @($root.rootFiles)) {
            $path = Join-Path ([string]$root.path) ([string]$file.name)
            if ((Get-Sha256File $path) -cne [string]$file.sha256) { throw "$($root.role) root file changed: $($file.name)" }
        }
    }
    foreach ($location in @($Plan.protectedLocations)) { Assert-DirectoryNotReparse -Path ([string]$location.path) -Label ([string]$location.role) }
    foreach ($sentinel in @($Plan.sentinels)) {
        $path = Resolve-RepoRelativeFile -RepoRoot $RepoRoot -RelativePath ([string]$sentinel.path) -Label 'sentinel'
        $item = Get-Item -LiteralPath $path -Force
        if ([long]$item.Length -ne [long]$sentinel.bytes -or (Get-Sha256File $path) -cne [string]$sentinel.sha256) { throw "Sentinel changed: $($sentinel.path)" }
    }
    if ($null -ne $Plan.protectionInput) {
        if ((Get-Sha256File ([string]$Plan.protectionInput.path)) -cne [string]$Plan.protectionInput.sha256) { throw "Protection input changed." }
        foreach ($entry in @($Plan.protectionInput.entries)) {
            $path = Resolve-RepoRelativeFile -RepoRoot $RepoRoot -RelativePath ([string]$entry.evidencePath) -Label 'protection evidence'
            if ((Get-Sha256File $path) -cne [string]$entry.evidenceSha256) { throw "Protection evidence changed: $($entry.evidencePath)" }
        }
    }
    foreach ($bundle in @($Plan.protectedBundles)) {
        $parts = ([string]$bundle.address).Split('/')
        $observed = Get-BundleRecord -RootRole ([string]$bundle.rootRole) -PackId $parts[0] -Digest $parts[1] -Path ([string]$bundle.path)
        if ($observed.fileCount -ne [long]$bundle.fileCount -or $observed.bytes -ne [long]$bundle.bytes -or $observed.bundleSha256 -cne [string]$bundle.bundleSha256) {
            throw "Protected bundle changed: $($bundle.rootRole)|$($bundle.address)"
        }
    }
}

function Invoke-RetentionExecute {
    param(
        [string]$PlanPath, [string]$ExpectedPlanSha256, [string]$RepoRoot,
        [string]$WorkspacePacksRoot, [string]$DurablePacksRoot, [string]$ArchiveRoot,
        [string]$ProtectionPath, [string]$ScriptPath, [string]$JournalPath, [string]$ReceiptPath
    )
    if (-not [IO.File]::Exists($PlanPath)) { throw "Reviewed plan is missing: $PlanPath" }
    $storedHash = Get-Sha256File $PlanPath
    if ($storedHash -cne $ExpectedPlanSha256) { throw "Plan SHA mismatch: supplied=$ExpectedPlanSha256 stored=$storedHash" }
    try { $stored = Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Reviewed plan is unreadable: $($_.Exception.Message)" }
    if ([string]$stored.schema -ne 'openbfme.content-retention-plan' -or [int]$stored.schemaVersion -ne 1) { throw "Unsupported plan schema." }
    $fresh = New-RetentionPlan -RepoRoot $RepoRoot -WorkspacePacksRoot $WorkspacePacksRoot -DurablePacksRoot $DurablePacksRoot -ArchiveRoot $ArchiveRoot -ProtectionPath $ProtectionPath -ScriptPath $ScriptPath
    $freshHash = Get-Sha256Bytes -Bytes $script:Utf8NoBom.GetBytes((Convert-DocumentToUtf8Json $fresh))
    if ($freshHash -cne $ExpectedPlanSha256) { throw "Live state drifted after review: reviewed=$ExpectedPlanSha256 current=$freshHash" }

    $journal = [pscustomobject][ordered]@{ schema = 'openbfme.content-retention-journal'; schemaVersion = 1; planSha256 = $ExpectedPlanSha256; outcomes = @() }
    Write-Utf8JsonAtomic -Path $JournalPath -Document $journal
    foreach ($candidate in @($stored.candidates)) {
        $source = [IO.Path]::GetFullPath([string]$candidate.sourcePath)
        $destination = [IO.Path]::GetFullPath([string]$candidate.archivePath)
        $root = @($stored.approvedRoots | Where-Object { $_.role -eq $candidate.rootRole })
        if ($root.Count -ne 1) { throw "Candidate has unknown rootRole: $($candidate.rootRole)" }
        $parts = ([string]$candidate.address).Split('/')
        $expectedSource = Join-Path ([string]$root[0].path) (Join-Path $parts[0] $parts[1])
        $expectedArchive = Join-Path $ArchiveRoot (Join-Path ([string]$candidate.rootRole) (Join-Path $parts[0] $parts[1]))
        if (-not [string]::Equals([IO.Path]::GetFullPath($expectedSource), $source, [StringComparison]::OrdinalIgnoreCase)) { throw "Source not exact root-qualified address: $source" }
        if (-not [string]::Equals([IO.Path]::GetFullPath($expectedArchive), $destination, [StringComparison]::OrdinalIgnoreCase)) { throw "Archive path escaped reviewed layout: $destination" }
        if ([IO.Directory]::Exists($destination) -or [IO.File]::Exists($destination)) { throw "Archive destination exists: $destination" }
        if (-not [string]::Equals([IO.Path]::GetPathRoot($source), [IO.Path]::GetPathRoot($destination), [StringComparison]::OrdinalIgnoreCase)) { throw "Atomic move requires one volume." }
        $parent = Split-Path -Parent $destination
        Assert-SafeArchiveParent -Parent $parent -ArchiveRoot $ArchiveRoot -RepoRoot $RepoRoot
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        Assert-DirectoryNotReparse -Path $parent -Label 'archive parent'
        $outcome = [pscustomobject][ordered]@{
            rootRole = [string]$candidate.rootRole; address = [string]$candidate.address
            sourcePath = $source; archivePath = $destination; fileCount = [long]$candidate.fileCount
            bytes = [long]$candidate.bytes; bundleSha256 = [string]$candidate.bundleSha256; status = 'move-started'
        }
        $journal.outcomes = @($journal.outcomes) + @($outcome); Write-Utf8JsonAtomic $JournalPath $journal
        [IO.Directory]::Move($source, $destination)
        $observed = Get-BundleRecord -RootRole ([string]$candidate.rootRole) -PackId $parts[0] -Digest $parts[1] -Path $destination
        if ($observed.fileCount -ne [long]$candidate.fileCount -or $observed.bytes -ne [long]$candidate.bytes -or $observed.bundleSha256 -cne [string]$candidate.bundleSha256) { throw "Archived bundle verification failed: $($candidate.address)" }
        $journal.outcomes[-1].status = 'moved-and-verified'; Write-Utf8JsonAtomic $JournalPath $journal
    }
    Assert-ProtectedState -Plan $stored -RepoRoot $RepoRoot -ScriptPath $ScriptPath
    $receipt = [pscustomobject][ordered]@{
        schema = 'openbfme.content-retention-removal-receipt'; schemaVersion = 1
        planSha256 = $ExpectedPlanSha256; gitRevision = [string]$stored.git.revision
        selectionSha256 = [pscustomobject][ordered]@{
            workspace = [string](@($stored.approvedRoots | Where-Object role -eq 'workspace')[0].selectionSha256)
            durable = [string](@($stored.approvedRoots | Where-Object role -eq 'durable')[0].selectionSha256)
        }
        outcomes = @($journal.outcomes)
        totals = [pscustomobject][ordered]@{
            movedBundles = @($journal.outcomes).Count
            movedFiles = [long](($journal.outcomes | Measure-Object fileCount -Sum).Sum)
            movedBytes = [long](($journal.outcomes | Measure-Object bytes -Sum).Sum)
        }
        archivePurged = $false; protectedStateRevalidated = $true
    }
    Write-Utf8JsonAtomic $ReceiptPath $receipt
    Write-Host "RETENTION EXECUTE PASS plan_sha256=$ExpectedPlanSha256 moved=$(@($journal.outcomes).Count) archive_purged=false"
}

function New-SelfTestBundle {
    param([string]$Root, [string]$PackId, [string]$Payload)
    $staging = Join-Path $Root "$PackId.building"; [IO.Directory]::CreateDirectory($staging) | Out-Null
    [IO.File]::WriteAllText((Join-Path $staging 'pack.json'), "{`"id`":`"$PackId`"}`n", $script:Utf8NoBom)
    [IO.File]::WriteAllText((Join-Path $staging 'payload.txt'), $Payload, $script:Utf8NoBom)
    $identity = Get-BundleTreeIdentity $staging; $packRoot = Join-Path $Root $PackId
    [IO.Directory]::CreateDirectory($packRoot) | Out-Null
    [IO.Directory]::Move($staging, (Join-Path $packRoot $identity.bundleSha256))
    return "$PackId/$($identity.bundleSha256)"
}

function Invoke-SelfTest {
    $selfTestScriptPath = [IO.Path]::GetFullPath($PSCommandPath)
    $tempBase = Get-NormalizedDirectoryPath ([IO.Path]::GetTempPath())
    $testRoot = Join-Path $tempBase ("openbfme-retention-selftest-" + [Guid]::NewGuid().ToString('N'))
    if (-not [string]::Equals((Get-NormalizedDirectoryPath (Split-Path -Parent $testRoot)), $tempBase, [StringComparison]::OrdinalIgnoreCase)) { throw "Self-test root escaped temp." }
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null
    try {
        $fakeRepo = Join-Path $testRoot 'repo'
        $workspaceRoot = Join-Path $fakeRepo 'workspace\content-packs'
        $durableRoot = Join-Path $testRoot 'durable-content-packs'
        $logRoot = Join-Path $fakeRepo 'workspace\logs\P0-RETENTION-001'
        $archiveRoot = Join-Path $fakeRepo 'workspace\archive\P0-RETENTION-001\content-packs'
        $planPath = Join-Path $logRoot 'retention-plan.json'
        $journalPath = Join-Path $logRoot 'removal-journal.json'
        $receiptPath = Join-Path $logRoot 'removal-receipt.json'
        $protectionPath = Join-Path $logRoot 'protection-input.json'
        foreach ($directory in @(
            $workspaceRoot,
            $durableRoot,
            (Join-Path $fakeRepo 'contracts'),
            (Join-Path $fakeRepo 'workspace\retail-extract'),
            (Join-Path $fakeRepo 'workspace\retail-work\reports\compatibility'),
            (Join-Path $fakeRepo 'workspace\retail-oracle'),
            $logRoot
        )) { [IO.Directory]::CreateDirectory($directory) | Out-Null }

        $baseline = Join-Path $fakeRepo 'contracts\rotwk-202-v9.7.7-baseline.json'
        $effective = Join-Path $fakeRepo 'workspace\retail-work\reports\compatibility\rotwk-202-v9.7.7-effective-tree.json'
        $feature = Join-Path $fakeRepo 'workspace\retail-work\reports\compatibility\rotwk-202-v9.7.7-feature-graph.json'
        $oracle = Join-Path $fakeRepo 'workspace\retail-oracle\rotwk-201-baseline.local.json'
        $evidence = Join-Path $fakeRepo 'workspace\logs\lkg-evidence.json'
        [IO.Directory]::CreateDirectory((Split-Path -Parent $evidence)) | Out-Null
        foreach ($file in @($baseline, $effective, $feature, $oracle, $evidence)) {
            [IO.File]::WriteAllText($file, "{}`n", $script:Utf8NoBom)
        }

        & git -C $fakeRepo init --quiet
        if ($LASTEXITCODE -ne 0) { throw "Self-test git init failed." }
        & git -C $fakeRepo config core.autocrlf false
        if ($LASTEXITCODE -ne 0) { throw "Self-test git configuration failed." }
        & git -C $fakeRepo add contracts workspace/retail-work/reports/compatibility workspace/retail-oracle
        if ($LASTEXITCODE -ne 0) { throw "Self-test git add failed." }
        & git -C $fakeRepo -c user.name=OpenBFME-SelfTest -c user.email=selftest.invalid commit --quiet -m sentinel
        if ($LASTEXITCODE -ne 0) { throw "Self-test git commit failed." }

        $workspaceKeep = New-SelfTestBundle $workspaceRoot 'workspace-keep' 'selected'
        $workspaceLkg = New-SelfTestBundle $workspaceRoot 'workspace-lkg' 'protected-by-receipt'
        $workspaceRemove = New-SelfTestBundle $workspaceRoot 'workspace-remove' 'candidate'
        $durableKeep = New-SelfTestBundle $durableRoot 'durable-keep' 'selected'
        Write-Utf8JsonAtomic (Join-Path $workspaceRoot 'selection.json') ([pscustomobject][ordered]@{
            activePack = $workspaceKeep; schema = 'openbfme.pack-selection'; schemaVersion = 0; supplementalPacks = @()
        })
        Write-Utf8JsonAtomic (Join-Path $durableRoot 'selection.json') ([pscustomobject][ordered]@{
            activePack = $durableKeep; schema = 'openbfme.pack-selection'; schemaVersion = 0; supplementalPacks = @()
        })
        Write-Utf8JsonAtomic $protectionPath ([pscustomobject][ordered]@{
            schema = 'openbfme.content-retention-protection'; schemaVersion = 1
            entries = @([pscustomobject][ordered]@{
                rootRole = 'workspace'; bundleAddress = $workspaceLkg; reasonCode = 'last-known-good'
                evidencePath = 'workspace/logs/lkg-evidence.json'; evidenceSha256 = Get-Sha256File $evidence
            })
        })

        $plan = New-RetentionPlan $fakeRepo $workspaceRoot $durableRoot $archiveRoot $protectionPath $selfTestScriptPath
        if ($plan.totals.inventoriedBundles -ne 4 -or $plan.totals.protectedBundles -ne 3 -or $plan.totals.candidateBundles -ne 1) {
            throw "Self-test protect/candidate equation failed."
        }
        Write-Utf8JsonAtomic $planPath $plan
        $planHash = Get-Sha256File $planPath
        Invoke-RetentionExecute $planPath $planHash $fakeRepo $workspaceRoot $durableRoot $archiveRoot $protectionPath $selfTestScriptPath $journalPath $receiptPath

        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $candidateParts = $workspaceRemove.Split('/')
        $archivedCandidate = Join-Path $archiveRoot (Join-Path 'workspace' (Join-Path $candidateParts[0] $candidateParts[1]))
        if (@($receipt.outcomes).Count -ne 1 -or $receipt.outcomes[0].status -ne 'moved-and-verified' -or
            -not [IO.Directory]::Exists($archivedCandidate) -or
            -not [IO.Directory]::Exists((Join-Path $workspaceRoot $workspaceKeep.Replace('/', '\'))) -or
            -not [IO.Directory]::Exists((Join-Path $workspaceRoot $workspaceLkg.Replace('/', '\'))) -or
            -not [IO.Directory]::Exists((Join-Path $durableRoot $durableKeep.Replace('/', '\')))) {
            throw "Self-test execute/receipt/protected-state assertion failed."
        }
        Write-Host "SELF TEST PASS inventory=4 protected=3 moved=1 plan_sha_revalidated=true receipt=true"
    }
    finally {
        if ([IO.Directory]::Exists($testRoot)) {
            $resolved = Get-NormalizedDirectoryPath $testRoot
            if (-not [string]::Equals((Get-NormalizedDirectoryPath (Split-Path -Parent $resolved)), $tempBase, [StringComparison]::OrdinalIgnoreCase) -or -not [IO.Path]::GetFileName($resolved).StartsWith('openbfme-retention-selftest-', [StringComparison]::Ordinal)) { throw "Unsafe self-test cleanup: $resolved" }
            foreach ($entry in @(Get-ChildItem -LiteralPath $resolved -Force -Recurse -ErrorAction Stop)) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Self-test unexpectedly created a reparse point: $($entry.FullName)"
                }
                $entry.Attributes = [IO.FileAttributes]::Normal
            }
            [IO.Directory]::Delete($resolved, $true)
        }
    }
}

if ($SelfTest) { Invoke-SelfTest; exit 0 }
if (-not $env:APPDATA) { throw "APPDATA is unavailable; durable root cannot be bound." }
$repoRoot = Get-NormalizedDirectoryPath (Split-Path -Parent $PSScriptRoot)
$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$workspacePacksRoot = Join-Path $repoRoot 'workspace\content-packs'
$durablePacksRoot = Join-Path $env:APPDATA 'Godot\app_userdata\Open BFME\content-packs'
$logRoot = Join-Path $repoRoot 'workspace\logs\P0-RETENTION-001'
$archiveRoot = Join-Path $repoRoot 'workspace\archive\P0-RETENTION-001\content-packs'
$planPath = Join-Path $logRoot 'retention-plan.json'
$receiptPath = Join-Path $logRoot 'removal-receipt.json'
$journalPath = Join-Path $logRoot 'removal-journal.json'
$protectionPath = Join-Path $logRoot 'protection-input.json'
if (-not [IO.File]::Exists((Join-Path $repoRoot 'AGENTS.md'))) { throw "Repository root is invalid: $repoRoot" }

if ($DryRun) {
    $plan = New-RetentionPlan $repoRoot $workspacePacksRoot $durablePacksRoot $archiveRoot $protectionPath $scriptPath
    Write-Utf8JsonAtomic $planPath $plan
    $planHash = Get-Sha256File $planPath
    Write-Host "DRY RUN PASS plan=$planPath plan_sha256=$planHash inventoried=$($plan.totals.inventoriedBundles) protected=$($plan.totals.protectedBundles) candidates=$($plan.totals.candidateBundles) candidate_files=$($plan.totals.candidateFiles) candidate_bytes=$($plan.totals.candidateBytes)"
    exit 0
}

Invoke-RetentionExecute $planPath $PlanSha256 $repoRoot $workspacePacksRoot $durablePacksRoot $archiveRoot $protectionPath $scriptPath $journalPath $receiptPath
