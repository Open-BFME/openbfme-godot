[CmdletBinding(DefaultParameterSetName = "Window")]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureId,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Retail", "Godot")]
    [string]$Side,
    [Parameter(Mandatory = $true, ParameterSetName = "Window")]
    [ValidateNotNullOrEmpty()]
    [string]$WindowTitle,
    [Parameter(Mandatory = $true, ParameterSetName = "Existing")]
    [ValidateNotNullOrEmpty()]
    [string]$SourcePath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[1-9][0-9]*x[1-9][0-9]*$')]
    [string]$Viewport,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CameraState,
    [string]$Notes = "",
    [string]$ManifestPath = "",
    [Parameter(ParameterSetName = "Window")]
    [ValidateRange(0, 30)]
    [int]$DelaySeconds = 2,
    [switch]$Replace
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

function Resolve-M2Ffmpeg {
    $command = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    foreach ($candidate in @("C:\Program Files\ShareX\ffmpeg.exe", "C:\Program Files\Kdenlive\bin\ffmpeg.exe")) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "FFmpeg was not found. Install it or add ffmpeg.exe to PATH."
}

function Assert-M2PrivatePhysicalFile {
    param([string]$Path, [string]$RepoRoot)
    $privateRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot "workspace")).TrimEnd('\')
    $candidate = [IO.Path]::GetFullPath($Path)
    $prefix = $privateRoot + '\'
    Assert-M2OracleTrue ($candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) "Existing frame must remain below the repository workspace root."
    Assert-M2OracleTrue (Test-Path -LiteralPath $candidate -PathType Leaf) "Existing private frame is missing."
    Assert-M2OracleTrue ([IO.Path]::GetExtension($candidate).Equals(".png", [StringComparison]::OrdinalIgnoreCase)) "Existing private frame must be a PNG."

    $relative = $candidate.Substring($prefix.Length)
    $cursor = $privateRoot
    foreach ($segment in @($relative.Split([IO.Path]::DirectorySeparatorChar, [StringSplitOptions]::RemoveEmptyEntries))) {
        $item = Get-Item -Force -LiteralPath $cursor
        Assert-M2OracleTrue (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Existing frame crosses a link or junction: $cursor"
        $cursor = Join-Path $cursor $segment
    }
    $leaf = Get-Item -Force -LiteralPath $cursor
    Assert-M2OracleTrue (($leaf.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Existing frame may not be a link or reparse point."
    Assert-M2OracleTrue ($leaf.LinkType -ne "HardLink") "Existing frame may not be a hard link."
    return $candidate
}

function Assert-M2PhysicalOutputPath {
    param([string]$Path, [string]$OracleRoot)
    $root = [IO.Path]::GetFullPath($OracleRoot).TrimEnd('\')
    $candidate = [IO.Path]::GetFullPath($Path)
    $prefix = $root + '\'
    Assert-M2OracleTrue ($candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) "Oracle output escapes its private root."
    $relative = $candidate.Substring($prefix.Length)
    $cursor = $root
    foreach ($segment in @($relative.Split([IO.Path]::DirectorySeparatorChar, [StringSplitOptions]::RemoveEmptyEntries))) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -Force -LiteralPath $cursor
            Assert-M2OracleTrue (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Oracle output crosses a link or junction: $cursor"
            Assert-M2OracleTrue ($item.LinkType -ne "HardLink") "Oracle output crosses a hard-linked file: $cursor"
        }
        $cursor = Join-Path $cursor $segment
    }
    if (Test-Path -LiteralPath $cursor) {
        $leaf = Get-Item -Force -LiteralPath $cursor
        Assert-M2OracleTrue (($leaf.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Oracle output may not be a link or reparse point."
        Assert-M2OracleTrue ($leaf.LinkType -ne "HardLink") "Oracle output may not be a hard link."
    }
    return $candidate
}

function Get-M2StreamSha256 {
    param([IO.Stream]$Stream)
    $Stream.Position = 0
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hasher.ComputeHash($Stream))).Replace("-", "").ToLowerInvariant() }
    finally { $hasher.Dispose() }
}

function Write-M2OracleJsonCreateNew {
    param([object]$Value, [string]$Path)
    $json = ($Value | ConvertTo-Json -Depth 12) + [Environment]::NewLine
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    }
    finally { $stream.Dispose() }
}

function Remove-M2FileIfPresent {
    param([string]$Path)
    if (-not [string]::IsNullOrWhiteSpace($Path) -and [IO.File]::Exists($Path)) {
        [IO.File]::Delete($Path)
    }
}

function ConvertTo-M2NativeArgument {
    param([AllowEmptyString()][string]$Argument)
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (($slashes * 2) + 1)))
            [void]$builder.Append('"')
        }
        else {
            if ($slashes -gt 0) { [void]$builder.Append(('\' * $slashes)) }
            [void]$builder.Append($character)
        }
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-M2FfmpegPngToStream {
    param([string]$FfmpegPath, [string[]]$Arguments, [IO.Stream]$OutputStream)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FfmpegPath
    $startInfo.Arguments = (($Arguments | ForEach-Object { ConvertTo-M2NativeArgument ([string]$_) }) -join ' ')
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        Assert-M2OracleTrue ($process.Start()) "FFmpeg could not be started."
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.StandardOutput.BaseStream.CopyTo($OutputStream)
        $OutputStream.Flush($true)
        $process.WaitForExit()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        Assert-M2OracleTrue ($process.ExitCode -eq 0) "FFmpeg capture failed: $stderr"
    }
    finally { $process.Dispose() }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
Assert-M2OracleTrue ($script:M2OracleCaptureIds -ccontains $CaptureId) "Unknown capture ID '$CaptureId'."
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $context.oracleRoot "m2-men-fords-captures.json"
}
$manifestPath = Assert-M2OracleContainedPath $ManifestPath $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Capture manifest is missing. Run tools/new-m2-oracle-workspace.ps1 after source is frozen."
$manifestPath = Assert-M2PhysicalOutputPath $manifestPath $context.oracleRoot
$manifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleManifestIdentity $manifest $context
$row = @($manifest.captures | Where-Object { [string]$_.id -ceq $CaptureId })
Assert-M2OracleTrue ($row.Count -eq 1) "Capture row '$CaptureId' is not unique."
$capture = $row[0]
$sideName = $Side.ToLowerInvariant()
$cameraStateProperty = "${sideName}CameraState"
Assert-M2OracleTrue ($null -ne $capture.PSObject.Properties[$cameraStateProperty]) "Capture manifest lacks the $sideName camera-state field."
if (-not [string]::IsNullOrWhiteSpace([string]$capture.viewport)) {
    Assert-M2OracleTrue ([string]$capture.viewport -eq $Viewport) "The pair already uses viewport '$($capture.viewport)'. Reinitialize the pair deliberately instead of mixing viewports."
}
if (-not [string]::IsNullOrWhiteSpace([string]$capture.PSObject.Properties[$cameraStateProperty].Value)) {
    Assert-M2OracleTrue ([string]$capture.PSObject.Properties[$cameraStateProperty].Value -eq $CameraState -or $Replace) "The $sideName capture already uses another camera state. Use -Replace or reinitialize that side deliberately instead of mixing states."
}

$manifestRoot = Split-Path -Parent $manifestPath
$outputPath = Assert-M2OracleContainedPath (Join-Path $manifestRoot "captures\$sideName\$CaptureId.png") $context.oracleRoot
$outputPath = Assert-M2PhysicalOutputPath $outputPath $context.oracleRoot
$failureInjectionEnabled = [Environment]::GetEnvironmentVariable("OPENBFME_ORACLE_TEST_FAIL_AFTER_OUTPUT", "Process") -eq "1"
if ($failureInjectionEnabled) {
    $oracleScratchRoot = [IO.Path]::GetFullPath((Join-Path $context.oracleRoot "scratch")).TrimEnd('\') + '\'
    Assert-M2OracleTrue ($manifestPath.StartsWith($oracleScratchRoot, [StringComparison]::OrdinalIgnoreCase)) "Failure injection is allowed only for an isolated oracle scratch manifest."
}
$outputExisted = Test-Path -LiteralPath $outputPath -PathType Leaf
if ($outputExisted -and -not $Replace) {
    throw "Capture already exists: $outputPath. Use -Replace to recapture it and revoke review."
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null
Add-Type -AssemblyName System.Drawing
$captureSource = "live-window"
$transactionId = [guid]::NewGuid().ToString("N")
$stagedOutput = Join-Path (Split-Path -Parent $outputPath) ".$CaptureId.$transactionId.pending.png"
$stagedManifest = Join-Path $manifestRoot ".$([IO.Path]::GetFileName($manifestPath)).$transactionId.pending"
$revokedManifestStage = Join-Path $manifestRoot ".$([IO.Path]::GetFileName($manifestPath)).$transactionId.revoked.pending"
$manifestOriginalBackup = Join-Path $manifestRoot ".$([IO.Path]::GetFileName($manifestPath)).$transactionId.original.bak"
$manifestRevokedBackup = Join-Path $manifestRoot ".$([IO.Path]::GetFileName($manifestPath)).$transactionId.revoked.bak"
$outputOriginalBackup = Join-Path (Split-Path -Parent $outputPath) ".$CaptureId.$transactionId.original.bak"
$rollbackTrash = Join-Path $manifestRoot ".$([IO.Path]::GetFileName($manifestPath)).$transactionId.rollback.trash"
$outputRollbackTrash = Join-Path (Split-Path -Parent $outputPath) ".$CaptureId.$transactionId.rollback.trash"
$sourcePathFull = ""
$sourceSha256 = ""
$manifestRevoked = $false
$outputPublished = $false
$finalManifestPublished = $false
$preserveRecoveryArtifacts = $false
try {
    $stagedOutput = Assert-M2PhysicalOutputPath $stagedOutput $context.oracleRoot
    if ($PSCmdlet.ParameterSetName -eq "Existing") {
        $sourcePathFull = Assert-M2PrivatePhysicalFile $SourcePath $repoRoot
        Assert-M2OracleTrue (-not $sourcePathFull.Equals($outputPath, [StringComparison]::OrdinalIgnoreCase)) "Existing frame source and oracle destination must differ."
        $sourceStream = [IO.FileStream]::new($sourcePathFull, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $sourceBitmap = [Drawing.Image]::FromStream($sourceStream, $true, $true)
            try { $sourceViewport = "$($sourceBitmap.Width)x$($sourceBitmap.Height)" }
            finally { $sourceBitmap.Dispose() }
            Assert-M2OracleTrue ($sourceViewport -eq $Viewport) "Existing frame is $sourceViewport, expected $Viewport."
            $sourceSha256 = Get-M2StreamSha256 $sourceStream
            $sourceStream.Position = 0
            $stageStream = [IO.FileStream]::new($stagedOutput, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $sourceStream.CopyTo($stageStream)
                $stageStream.Flush($true)
            }
            finally { $stageStream.Dispose() }
            $sourceHashAfterCopy = Get-M2StreamSha256 $sourceStream
            Assert-M2OracleTrue ($sourceHashAfterCopy -eq $sourceSha256) "Existing frame changed while its locked handle was copied."
        }
        finally { $sourceStream.Dispose() }
        $captureSource = "existing-private-frame"
    }
    else {
        $stageStream = [IO.FileStream]::new($stagedOutput, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            if ($DelaySeconds -gt 0) {
                Write-Host "M2_ORACLE_CAPTURE waiting_seconds=$DelaySeconds window=$WindowTitle"
                Start-Sleep -Seconds $DelaySeconds
            }
            $ffmpeg = Resolve-M2Ffmpeg
            $arguments = @(
                "-hide_banner", "-loglevel", "error",
                "-f", "gdigrab", "-draw_mouse", "0", "-framerate", "1",
                "-i", "title=$WindowTitle", "-frames:v", "1", "-compression_level", "4",
                "-f", "image2pipe", "-vcodec", "png", "pipe:1"
            )
            Invoke-M2FfmpegPngToStream $ffmpeg $arguments $stageStream
        }
        finally { $stageStream.Dispose() }
    }

    $stagedOutput = Assert-M2PhysicalOutputPath $stagedOutput $context.oracleRoot
    $bitmap = [Drawing.Image]::FromFile($stagedOutput)
    try { $actualViewport = "$($bitmap.Width)x$($bitmap.Height)" }
    finally { $bitmap.Dispose() }
    Assert-M2OracleTrue ($actualViewport -eq $Viewport) "Captured viewport is $actualViewport, expected $Viewport."
    $sha256 = (Get-FileHash -LiteralPath $stagedOutput -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($captureSource -eq "existing-private-frame") {
        Assert-M2OracleTrue ($sha256 -eq $sourceSha256) "Staged destination bytes differ from the locked private source."
        $sourcePathFull = Assert-M2PrivatePhysicalFile $sourcePathFull $repoRoot
        Assert-M2OracleTrue ((Get-FileHash -LiteralPath $sourcePathFull -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceSha256) "Existing frame path changed after locked copy."
    }

    $relativePath = Get-M2OracleRelativePath $manifestRoot $outputPath
    $capture.viewport = $Viewport
    $capture.PSObject.Properties[$cameraStateProperty].Value = $CameraState
    $capture.PSObject.Properties["${sideName}Path"].Value = $relativePath
    $capture.PSObject.Properties["${sideName}Sha256"].Value = $sha256
    if (-not [string]::IsNullOrWhiteSpace($Notes)) { $capture.notes = $Notes }
    $capture.approved = $false
    $capture.approvedBy = ""
    $capture.approvedAtUtc = ""
    $capture.unresolvedSeverity0 = 0
    $capture.unresolvedSeverity1 = 0
    $stagedManifest = Assert-M2PhysicalOutputPath $stagedManifest $context.oracleRoot
    Write-M2OracleJsonCreateNew $manifest $stagedManifest

    if ($outputExisted) {
        $finalPath = [string]$capture.PSObject.Properties["${sideName}Path"].Value
        $finalHash = [string]$capture.PSObject.Properties["${sideName}Sha256"].Value
        $capture.PSObject.Properties["${sideName}Path"].Value = ""
        $capture.PSObject.Properties["${sideName}Sha256"].Value = ""
        $capture.approved = $false
        $capture.approvedBy = ""
        $capture.approvedAtUtc = ""
        $revokedManifestStage = Assert-M2PhysicalOutputPath $revokedManifestStage $context.oracleRoot
        Write-M2OracleJsonCreateNew $manifest $revokedManifestStage
        $capture.PSObject.Properties["${sideName}Path"].Value = $finalPath
        $capture.PSObject.Properties["${sideName}Sha256"].Value = $finalHash
    }

    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while staging capture evidence."
    $manifestPath = Assert-M2PhysicalOutputPath $manifestPath $context.oracleRoot
    $outputPath = Assert-M2PhysicalOutputPath $outputPath $context.oracleRoot
    $stagedManifest = Assert-M2PhysicalOutputPath $stagedManifest $context.oracleRoot
    Assert-M2OracleTrue ((Get-FileHash -LiteralPath $stagedOutput -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sha256) "Staged capture changed before publication."
    $stagedCheck = Get-Content -Raw -LiteralPath $stagedManifest -Encoding UTF8 | ConvertFrom-Json
    Assert-M2OracleManifestIdentity $stagedCheck $context

    if ($outputExisted) {
        $revokedManifestStage = Assert-M2PhysicalOutputPath $revokedManifestStage $context.oracleRoot
        [IO.File]::Replace($revokedManifestStage, $manifestPath, $manifestOriginalBackup, $true)
        $manifestRevoked = $true
        $outputPath = Assert-M2PhysicalOutputPath $outputPath $context.oracleRoot
        [IO.File]::Replace($stagedOutput, $outputPath, $outputOriginalBackup, $true)
    }
    else {
        [IO.File]::Move($stagedOutput, $outputPath)
    }
    $outputPublished = $true

    if ($failureInjectionEnabled) {
        throw "Injected isolated failure after atomic output publication."
    }

    $manifestPath = Assert-M2PhysicalOutputPath $manifestPath $context.oracleRoot
    [IO.File]::Replace($stagedManifest, $manifestPath, $manifestRevokedBackup, $true)
    $finalManifestPublished = $true

    $outputPath = Assert-M2PhysicalOutputPath $outputPath $context.oracleRoot
    $manifestPath = Assert-M2PhysicalOutputPath $manifestPath $context.oracleRoot
    Assert-M2OracleTrue ((Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sha256) "Published capture hash differs from staged evidence."
    $publishedManifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
    Assert-M2OracleManifestIdentity $publishedManifest $context
    $publishedRow = @($publishedManifest.captures | Where-Object { [string]$_.id -ceq $CaptureId })[0]
    Assert-M2OracleTrue ([string]$publishedRow.PSObject.Properties[$cameraStateProperty].Value -eq $CameraState -and [string]$publishedRow.PSObject.Properties["${sideName}Path"].Value -eq $relativePath -and [string]$publishedRow.PSObject.Properties["${sideName}Sha256"].Value -eq $sha256 -and -not [bool]$publishedRow.approved) "Published manifest row does not match staged capture evidence."
    $publishedIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-M2OracleTrue ([string]$publishedIdentity.revision -eq $context.gitRevision -and [string]$publishedIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while publishing capture evidence."
}
catch {
    $originalError = $_
    try {
        if ($finalManifestPublished -and [IO.File]::Exists($manifestRevokedBackup)) {
            [IO.File]::Replace($manifestRevokedBackup, $manifestPath, $rollbackTrash, $true)
            Remove-M2FileIfPresent $rollbackTrash
            $finalManifestPublished = $false
        }
        if ($outputPublished) {
            if ($outputExisted -and [IO.File]::Exists($outputOriginalBackup)) {
                [IO.File]::Replace($outputOriginalBackup, $outputPath, $outputRollbackTrash, $true)
                Remove-M2FileIfPresent $outputRollbackTrash
            }
            elseif (-not $outputExisted) {
                Remove-M2FileIfPresent $outputPath
            }
            $outputPublished = $false
        }
        if ($manifestRevoked -and [IO.File]::Exists($manifestOriginalBackup)) {
            [IO.File]::Replace($manifestOriginalBackup, $manifestPath, $rollbackTrash, $true)
            Remove-M2FileIfPresent $rollbackTrash
            $manifestRevoked = $false
        }
    }
    catch {
        $preserveRecoveryArtifacts = $true
        throw "Oracle capture publication failed and rollback also failed. Original: $($originalError.Exception.Message) Rollback: $($_.Exception.Message)"
    }
    throw $originalError
}
finally {
    $cleanupPaths = @($stagedOutput, $stagedManifest, $revokedManifestStage)
    if (-not $preserveRecoveryArtifacts) {
        $cleanupPaths += @($manifestOriginalBackup, $manifestRevokedBackup, $outputOriginalBackup, $rollbackTrash, $outputRollbackTrash)
    }
    foreach ($path in $cleanupPaths) {
        Remove-M2FileIfPresent $path
    }
}
Write-Host "M2_ORACLE_CAPTURE_RESULT id=$CaptureId side=$sideName viewport=$Viewport sha256=$sha256 approved=false source=$captureSource"
Write-Host "M2_ORACLE_CAPTURE_PATH $outputPath"
