[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Stop-Gate([string] $Message) {
    throw "GODOT_UID_CLOSURE FAIL $Message"
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesSha256([byte[]] $Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Get-Utf8Sha256([string] $Value) {
    return Get-BytesSha256 ([Text.UTF8Encoding]::new($false).GetBytes($Value))
}

function Sort-Ordinal([string[]] $Values) {
    $copy = [string[]] @($Values)
    [Array]::Sort($copy, [StringComparer]::Ordinal)
    return $copy
}

function Get-PathDigest([string[]] $Paths) {
    return Get-Utf8Sha256 ((($Paths -join "`n") + "`n"))
}

function Get-ManifestDigest([object[]] $Entries) {
    $text = [Text.StringBuilder]::new()
    foreach ($entry in $Entries) {
        [void] $text.Append([string] $entry.path)
        [void] $text.Append([char] 0)
        [void] $text.Append([string] $entry.sha256)
        [void] $text.Append("`n")
    }
    return Get-Utf8Sha256 $text.ToString()
}

function Invoke-Git([string[]] $Arguments) {
    $output = @(& $script:Git.Source -C $script:Root @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Stop-Gate "git failed: $($Arguments -join ' ') :: $($output -join ' | ')"
    }
    return @($output | ForEach-Object { [string] $_ })
}

function Test-GitIgnored([string] $RelativePath) {
    & $script:Git.Source -C $script:Root check-ignore --no-index --quiet -- $RelativePath 2>$null
    return $LASTEXITCODE -eq 0
}

function Get-FileManifest([string] $BasePath) {
    $byPath = @{}
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $BasePath -Recurse -File -Force)) {
        $relative = $file.FullName.Substring($BasePath.Length + 1).Replace('\', '/')
        if ($relative.StartsWith('game/.godot/', [StringComparison]::Ordinal)) {
            continue
        }
        $paths.Add($relative)
        $byPath[$relative] = Get-Sha256 $file.FullName
    }
    $ordered = @(Sort-Ordinal $paths.ToArray())
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($path in $ordered) {
        $entries.Add([ordered]@{path = $path; sha256 = [string] $byPath[$path]})
    }
    return $entries.ToArray()
}

function Compare-ImportedCopy([object[]] $Before, [object[]] $After) {
    $afterByPath = @{}
    foreach ($entry in $After) {
        $afterByPath[[string] $entry.path] = [string] $entry.sha256
    }
    $beforePaths = @{}
    foreach ($entry in $Before) {
        $path = [string] $entry.path
        $beforePaths[$path] = $true
        if (-not $afterByPath.ContainsKey($path)) {
            Stop-Gate "import deleted committed-tree file $path"
        }
        if (-not [StringComparer]::Ordinal.Equals([string] $entry.sha256, [string] $afterByPath[$path])) {
            Stop-Gate "import modified committed-tree file $path"
        }
    }
    $addedPaths = [Collections.Generic.List[string]]::new()
    foreach ($entry in $After) {
        $path = [string] $entry.path
        if (-not $beforePaths.ContainsKey($path)) {
            $addedPaths.Add($path)
        }
    }
    $addedOrdered = @(Sort-Ordinal $addedPaths.ToArray())
    $added = [Collections.Generic.List[object]]::new()
    foreach ($path in $addedOrdered) {
        if (-not $path.EndsWith('.import', [StringComparison]::Ordinal) -or
            -not (Test-GitIgnored $path)) {
            Stop-Gate "import created Git-visible or non-import source artifact $path"
        }
        $sourcePath = $path.Substring(0, $path.Length - '.import'.Length)
        if (-not $beforePaths.ContainsKey($sourcePath)) {
            Stop-Gate "ignored import artifact is not adjacent to a pre-import source $path"
        }
        $added.Add([ordered]@{path = $path; sha256 = [string] $afterByPath[$path]})
    }
    return $added.ToArray()
}

function Invoke-GodotImport([string] $CopyRoot, [string] $RuntimeRoot) {
    $appData = Join-Path $RuntimeRoot 'appdata'
    $localAppData = Join-Path $RuntimeRoot 'localappdata'
    [void] (New-Item -ItemType Directory -Path $appData, $localAppData -Force)
    $project = Join-Path $CopyRoot 'game'
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $script:GodotConsole
    $start.Arguments = "--headless --path `"$project`" --import"
    $start.WorkingDirectory = $CopyRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['APPDATA'] = $appData
    $start.EnvironmentVariables['LOCALAPPDATA'] = $localAppData
    [void] $start.EnvironmentVariables.Remove('OPENBFME_CONTENT')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) {
        Stop-Gate 'Godot process did not start'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) {
        Stop-Gate "Godot import exited $exitCode"
    }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) {
        Stop-Gate "Godot import wrote stderr: $stderr"
    }
    return [ordered]@{
        exitCode = $exitCode
        stdoutBytes = [Text.Encoding]::UTF8.GetByteCount($stdout)
        stdoutSha256 = Get-Utf8Sha256 $stdout
        stderrBytes = [Text.Encoding]::UTF8.GetByteCount($stderr)
        stderrSha256 = Get-Utf8Sha256 $stderr
    }
}

$script:Git = Get-Command git.exe -CommandType Application -ErrorAction Stop
$script:Root = (& $script:Git.Source rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or -not [IO.Directory]::Exists($script:Root)) {
    Stop-Gate 'cannot resolve worker-lane root'
}
$commonGit = (& $script:Git.Source -C $script:Root rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not [IO.Directory]::Exists($commonGit)) {
    Stop-Gate 'cannot resolve common Git directory'
}
$mainRoot = [IO.Path]::GetDirectoryName($commonGit)
$script:GodotConsole = Join-Path $mainRoot '.tools\godot\Godot_v4.7-stable_win64_console.exe'
$godotEngine = Join-Path $mainRoot '.tools\godot\Godot_v4.7-stable_win64.exe'
if ((Get-Sha256 $script:GodotConsole) -cne 'd8055fb8c7e7f5010d7439ec69be051554055dae55a265f8647bd7301c34161c' -or
    (Get-Sha256 $godotEngine) -cne 'b2ca888d5115a6cedee564764a2ee494a625f2ec2edbabd010fe33c9a88a6bf8') {
    Stop-Gate 'pinned Godot identity mismatch'
}

$itemRoot = Join-Path $script:Root 'workspace\logs\P0-GODOT-UID-CLOSURE-001'
$assignmentPath = Join-Path $itemRoot 'assignment.json'
$assignment = Get-Content -LiteralPath $assignmentPath -Raw | ConvertFrom-Json
if ([string] $assignment.itemId -cne 'P0-GODOT-UID-CLOSURE-001') {
    Stop-Gate 'assignment item mismatch'
}
$owned = @(Sort-Ordinal ([string[]] @($assignment.ownedPaths)))
$uidTargets = @(Sort-Ordinal ([string[]] @($owned | Where-Object { $_.EndsWith('.gd.uid', [StringComparison]::Ordinal) })))
if ($owned.Count -ne 70 -or $uidTargets.Count -ne 69 -or
    -not ($owned -ccontains 'tools/gate-godot-uid-closure.ps1')) {
    Stop-Gate 'assignment ownership is not the closed 69 UID plus gate set'
}

$unstaged = @(Invoke-Git @('diff', '--name-only'))
if ($unstaged.Count -ne 0) {
    Stop-Gate "unstaged changes exist: $($unstaged -join ', ')"
}
$staged = @(Sort-Ordinal ([string[]] @(Invoke-Git @('diff', '--cached', '--name-only'))))
$head = [string] (@(Invoke-Git @('rev-parse', 'HEAD'))[0])
if ($staged.Count -gt 0) {
    if ($staged.Count -ne $owned.Count) {
        Stop-Gate 'staged path count differs from ownership'
    }
    for ($index = 0; $index -lt $owned.Count; $index++) {
        if (-not [StringComparer]::Ordinal.Equals($owned[$index], $staged[$index])) {
            Stop-Gate "staged ownership mismatch at $index"
        }
    }
}
else {
    $committed = @(Sort-Ordinal ([string[]] @(Invoke-Git @('diff', '--name-only', [string] $assignment.assignmentCommit, 'HEAD'))))
    if ($committed.Count -ne $owned.Count) {
        Stop-Gate 'committed implementation path count differs from ownership'
    }
    for ($index = 0; $index -lt $owned.Count; $index++) {
        if (-not [StringComparer]::Ordinal.Equals($owned[$index], $committed[$index])) {
            Stop-Gate "committed ownership mismatch at $index"
        }
    }
}
$candidateTree = [string] (@(Invoke-Git @('write-tree'))[0])

$scriptPaths = @(Sort-Ordinal ([string[]] @(Invoke-Git @('ls-files', '--', 'game/**/*.gd'))))
$uidPaths = @(Sort-Ordinal ([string[]] @(Invoke-Git @('ls-files', '--', 'game/**/*.gd.uid'))))
if ($scriptPaths.Count -ne 639 -or $uidPaths.Count -ne 639) {
    Stop-Gate "coverage count mismatch scripts=$($scriptPaths.Count) uids=$($uidPaths.Count)"
}
$scriptSet = @{}
foreach ($path in $scriptPaths) { $scriptSet[$path] = $true }
$uidValues = @{}
$uidEntries = [Collections.Generic.List[object]]::new()
foreach ($path in $uidPaths) {
    $source = $path.Substring(0, $path.Length - 4)
    if (-not $scriptSet.ContainsKey($source)) {
        Stop-Gate "orphan UID $path"
    }
    $bytes = [IO.File]::ReadAllBytes((Join-Path $script:Root $path))
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    if ($text -notmatch '^uid://[a-z0-9]+\n$') {
        Stop-Gate "malformed UID $path"
    }
    $value = $text.TrimEnd("`n")
    if ($uidValues.ContainsKey($value)) {
        Stop-Gate "duplicate UID value $value"
    }
    $uidValues[$value] = $path
    $uidEntries.Add([ordered]@{path = $path; sha256 = Get-BytesSha256 $bytes})
}
foreach ($path in $scriptPaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $script:Root ($path + '.uid')) -PathType Leaf)) {
        Stop-Gate "missing UID for $path"
    }
}

$baselineScriptDelta = @(Invoke-Git @('diff', '--name-only', 'b4f256c71381e6a01516edcb92d6a4d2aa7e2dce', '--', 'game/**/*.gd'))
if ($baselineScriptDelta.Count -ne 0) {
    Stop-Gate 'assignment-base GDScript set or bytes drifted from discovery baseline'
}

$archiveReceipt = Join-Path $mainRoot 'workspace\archive\worktrees\wt-fcc66fd352b19432\complete.json'
if ((Get-Item -LiteralPath $archiveReceipt).Length -ne 292748 -or
    (Get-Sha256 $archiveReceipt) -cne 'fcf9b7a231eb2706061816c8a3bf2ee357314ac9db8482f0cc2a4ec4c81dead2') {
    Stop-Gate 'v7 archive receipt identity mismatch'
}
$archiveJson = Get-Content -LiteralPath $archiveReceipt -Raw | ConvertFrom-Json
if ([string] $archiveJson.stateSha256 -cne '2cfe08d40dd291ee6a02817cf0586033e4df94e892912a1afc407c937624fe68') {
    Stop-Gate 'v7 archive state identity mismatch'
}
$archiveMetadata = Join-Path $mainRoot 'workspace\archive\worktrees\wt-fcc66fd352b19432\metadata.json'
if ((Get-Sha256 $archiveMetadata) -cne 'f8727e370a0bffda25637158a913a115e5eeea3aa7056672df7946bb8c8555b8') {
    Stop-Gate 'v7 archive metadata identity mismatch'
}
$metadataJson = Get-Content -LiteralPath $archiveMetadata -Raw | ConvertFrom-Json
if ([string] $metadataJson.inventoryRow.head -cne 'e56811e7abb6e62db19452673a66346d175adce9' -or
    [string] $metadataJson.inventoryRow.stateSha256 -cne '2cfe08d40dd291ee6a02817cf0586033e4df94e892912a1afc407c937624fe68') {
    Stop-Gate 'v7 archive metadata state mismatch'
}
$archiveFilesRoot = Join-Path $mainRoot 'workspace\archive\worktrees\wt-fcc66fd352b19432\files'
$archiveUidFiles = @(Get-ChildItem -LiteralPath (Join-Path $archiveFilesRoot 'game') -Recurse -File -Filter '*.gd.uid')
$archiveByPath = @{}
$archivePaths = [Collections.Generic.List[string]]::new()
foreach ($file in $archiveUidFiles) {
    $relative = $file.FullName.Substring($archiveFilesRoot.Length + 1).Replace('\', '/')
    $archivePaths.Add($relative)
    $archiveByPath[$relative] = $file.FullName
}
$archiveOrdered = @(Sort-Ordinal $archivePaths.ToArray())
if ($archiveOrdered.Count -ne 69 -or
    (Get-PathDigest $archiveOrdered) -cne 'd325381282bdc784a98a89cd5276a6ef600ccb1e1cc77d57cda4bb9f3df92749') {
    Stop-Gate 'archive UID path set mismatch'
}
$archiveEntries = [Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $archiveOrdered.Count; $index++) {
    $path = $archiveOrdered[$index]
    if (-not [StringComparer]::Ordinal.Equals($path, $uidTargets[$index])) {
        Stop-Gate "archive target mismatch at $index"
    }
    $expectedBytes = [IO.File]::ReadAllBytes([string] $archiveByPath[$path])
    $actualBytes = [IO.File]::ReadAllBytes((Join-Path $script:Root $path))
    if ($expectedBytes.Length -ne $actualBytes.Length -or
        (Get-BytesSha256 $expectedBytes) -cne (Get-BytesSha256 $actualBytes)) {
        Stop-Gate "UID bytes differ from archive for $path"
    }
    $archiveEntries.Add([ordered]@{path = $path; sha256 = Get-BytesSha256 $expectedBytes})
}
if ((Get-ManifestDigest $archiveEntries.ToArray()) -cne 'acda99ded352ad811730a3961be944e81387993da7f50caca463c0a7a5cc843f') {
    Stop-Gate 'archive UID content manifest mismatch'
}

$scriptEntries = [Collections.Generic.List[object]]::new()
foreach ($path in $scriptPaths) {
    $scriptEntries.Add([ordered]@{path = $path; sha256 = Get-Sha256 (Join-Path $script:Root $path)})
}

$runRoot = Join-Path $itemRoot ('run-' + [Guid]::NewGuid().ToString('N'))
[void] (New-Item -ItemType Directory -Path $runRoot -Force)
$success = $false
try {
    $archiveZip = Join-Path $runRoot 'candidate.zip'
    [void] (Invoke-Git @('archive', '--format=zip', "--output=$archiveZip", $candidateTree))
    $copies = @()
    foreach ($name in @('copy-a', 'copy-b')) {
        $copyRoot = Join-Path $runRoot $name
        Expand-Archive -LiteralPath $archiveZip -DestinationPath $copyRoot
        $before = Get-FileManifest $copyRoot
        $process = Invoke-GodotImport $copyRoot (Join-Path $runRoot ($name + '-runtime'))
        $after = Get-FileManifest $copyRoot
        $ignored = Compare-ImportedCopy $before $after
        $copies += [ordered]@{
            name = $name
            before = [ordered]@{entries = $before; treeSha256 = Get-ManifestDigest $before}
            after = [ordered]@{entries = $after; treeSha256 = Get-ManifestDigest $after}
            ignoredAdjacentImportCache = [ordered]@{
                entries = $ignored
                treeSha256 = Get-ManifestDigest $ignored
            }
            process = $process
        }
    }
    if ([string] $copies[0].ignoredAdjacentImportCache.treeSha256 -cne
        [string] $copies[1].ignoredAdjacentImportCache.treeSha256) {
        Stop-Gate 'independent imports produced different ignored adjacent cache bytes'
    }
    $receipt = [ordered]@{
        schema = 'openbfme.godot-uid-closure'
        schemaVersion = 1
        workItemId = 'P0-GODOT-UID-CLOSURE-001'
        assignmentCommit = [string] $assignment.assignmentCommit
        headAtCheck = $head
        candidateTree = $candidateTree
        godot = [ordered]@{
            consoleSha256 = Get-Sha256 $script:GodotConsole
            engineSha256 = Get-Sha256 $godotEngine
        }
        census = [ordered]@{
            scripts = $scriptPaths.Count
            uids = $uidPaths.Count
            missing = 0
            orphan = 0
            duplicate = 0
            scriptEntries = $scriptEntries.ToArray()
            uidEntries = $uidEntries.ToArray()
        }
        sourceEvidence = [ordered]@{
            archiveReceiptSha256 = Get-Sha256 $archiveReceipt
            uidPathSetSha256 = Get-PathDigest $archiveOrdered
            uidPathContentManifestSha256 = Get-ManifestDigest $archiveEntries.ToArray()
        }
        imports = $copies
        createdAtUtc = [DateTime]::UtcNow.ToString('o')
    }
    $receiptPath = Join-Path $itemRoot 'godot-uid-closure.json'
    $temporaryReceipt = $receiptPath + '.tmp'
    $json = $receipt | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText($temporaryReceipt, $json + "`n", [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporaryReceipt -Destination $receiptPath -Force
    $success = $true
}
finally {
    if ($success -and [IO.Directory]::Exists($runRoot)) {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
}

Write-Output 'GODOT_UID_CLOSURE PASS scripts=639 uids=639 missing=0 orphan=0 duplicate=0 imports=2'
