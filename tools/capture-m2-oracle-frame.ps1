[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureId,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Retail", "Godot")]
    [string]$Side,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WindowTitle,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[1-9][0-9]*x[1-9][0-9]*$')]
    [string]$Viewport,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CameraState,
    [string]$Notes = "",
    [string]$ManifestPath = "",
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

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
Assert-M2OracleTrue ($script:M2OracleCaptureIds -ccontains $CaptureId) "Unknown capture ID '$CaptureId'."
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $context.oracleRoot "m2-men-fords-captures.json"
}
$manifestPath = Assert-M2OracleContainedPath $ManifestPath $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Capture manifest is missing. Run tools/new-m2-oracle-workspace.ps1 after source is frozen."
$manifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleManifestIdentity $manifest $context
$row = @($manifest.captures | Where-Object { [string]$_.id -ceq $CaptureId })
Assert-M2OracleTrue ($row.Count -eq 1) "Capture row '$CaptureId' is not unique."
$capture = $row[0]
if (-not [string]::IsNullOrWhiteSpace([string]$capture.viewport)) {
    Assert-M2OracleTrue ([string]$capture.viewport -eq $Viewport) "The pair already uses viewport '$($capture.viewport)'. Reinitialize the pair deliberately instead of mixing viewports."
}
if (-not [string]::IsNullOrWhiteSpace([string]$capture.cameraState)) {
    Assert-M2OracleTrue ([string]$capture.cameraState -eq $CameraState) "The pair already uses another camera state. Reinitialize the pair deliberately instead of mixing states."
}

$sideName = $Side.ToLowerInvariant()
$outputPath = Assert-M2OracleContainedPath (Join-Path $context.oracleRoot "captures\$sideName\$CaptureId.png") $context.oracleRoot
if ((Test-Path -LiteralPath $outputPath) -and -not $Replace) {
    throw "Capture already exists: $outputPath. Use -Replace to recapture it and revoke review."
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputPath) | Out-Null
if ($DelaySeconds -gt 0) {
    Write-Host "M2_ORACLE_CAPTURE waiting_seconds=$DelaySeconds window=$WindowTitle"
    Start-Sleep -Seconds $DelaySeconds
}
$ffmpeg = Resolve-M2Ffmpeg
$arguments = @(
    "-hide_banner", "-loglevel", "error", "-y",
    "-f", "gdigrab", "-draw_mouse", "0", "-framerate", "1",
    "-i", "title=$WindowTitle", "-frames:v", "1", "-compression_level", "4", $outputPath
)
& $ffmpeg @arguments
Assert-M2OracleTrue ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $outputPath -PathType Leaf)) "FFmpeg could not capture the exact window title '$WindowTitle'."

Add-Type -AssemblyName System.Drawing
$bitmap = [Drawing.Image]::FromFile($outputPath)
try { $actualViewport = "$($bitmap.Width)x$($bitmap.Height)" }
finally { $bitmap.Dispose() }
if ($actualViewport -ne $Viewport) {
    Remove-Item -LiteralPath $outputPath -Force
    throw "Captured viewport is $actualViewport, expected $Viewport. The rejected frame was deleted."
}
$sha256 = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant()
$relativePath = Get-M2OracleRelativePath (Split-Path -Parent $manifestPath) $outputPath
$capture.viewport = $Viewport
$capture.cameraState = $CameraState
$capture.PSObject.Properties["${sideName}Path"].Value = $relativePath
$capture.PSObject.Properties["${sideName}Sha256"].Value = $sha256
if (-not [string]::IsNullOrWhiteSpace($Notes)) { $capture.notes = $Notes }
$capture.approved = $false
$capture.approvedBy = ""
$capture.approvedAtUtc = ""
$capture.unresolvedSeverity0 = 0
$capture.unresolvedSeverity1 = 0
$finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while capturing; the manifest is no longer valid."
Write-M2OracleJson $manifest $manifestPath
Write-Host "M2_ORACLE_CAPTURE_RESULT id=$CaptureId side=$sideName viewport=$Viewport sha256=$sha256 approved=false"
Write-Host "M2_ORACLE_CAPTURE_PATH $outputPath"
