[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WindowTitle,

    [ValidateRange(1, 3600)]
    [int]$DurationSeconds = 30,

    [ValidateRange(1, 120)]
    [int]$FrameRate = 60,

    [string]$AudioDevice = "",

    [string]$OutputPath = "",

    [switch]$UseNvenc,
    [switch]$ListAudioDevices,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$privateCaptureRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot ".private\retail-work\oracle\captures")
)

function Find-Ffmpeg {
    $command = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    $candidates = @(
        "C:\Program Files\ShareX\ffmpeg.exe",
        "C:\Program Files\Kdenlive\bin\ffmpeg.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "FFmpeg was not found. Install it or add ffmpeg.exe to PATH."
}

function Assert-ContainedCapturePath([string]$Candidate) {
    $full = [System.IO.Path]::GetFullPath($Candidate)
    $rootWithSeparator = $privateCaptureRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Retail/original-game recordings must stay under $privateCaptureRoot"
    }
    $extension = [System.IO.Path]::GetExtension($full).ToLowerInvariant()
    if ($extension -notin @(".mkv", ".mp4")) {
        throw "Capture output must use .mkv or .mp4."
    }
    return $full
}

$ffmpeg = Find-Ffmpeg
if ($ListAudioDevices) {
    & $ffmpeg -hide_banner -list_devices true -f dshow -i dummy
    exit $LASTEXITCODE
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $safeTitle = ($WindowTitle -replace '[^A-Za-z0-9._-]+', '-').Trim('-')
    if ([string]::IsNullOrWhiteSpace($safeTitle)) {
        $safeTitle = "retail-window"
    }
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputPath = Join-Path $privateCaptureRoot "$safeTitle-$stamp.mkv"
}
$resolvedOutput = Assert-ContainedCapturePath $OutputPath
$outputDirectory = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @(
    "-hide_banner",
    "-y",
    "-f", "gdigrab",
    "-draw_mouse", "1",
    "-framerate", $FrameRate.ToString(),
    "-i", "title=$WindowTitle"
)
if (-not [string]::IsNullOrWhiteSpace($AudioDevice)) {
    $arguments += @("-f", "dshow", "-i", "audio=$AudioDevice")
}
$arguments += @(
    "-t", $DurationSeconds.ToString(),
    "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2",
    "-pix_fmt", "yuv420p"
)
if ($UseNvenc) {
    $arguments += @("-c:v", "h264_nvenc", "-preset", "p4", "-cq", "20")
} else {
    $arguments += @("-c:v", "libx264", "-preset", "veryfast", "-crf", "18")
}
if (-not [string]::IsNullOrWhiteSpace($AudioDevice)) {
    $arguments += @("-c:a", "aac", "-b:a", "192k")
}
$arguments += $resolvedOutput

Write-Host "RETAIL_CAPTURE window=$WindowTitle seconds=$DurationSeconds fps=$FrameRate"
Write-Host "RETAIL_CAPTURE output=$resolvedOutput"
if ($DryRun) {
    Write-Host "RETAIL_CAPTURE dry_run=true"
    Write-Host ("RETAIL_CAPTURE command={0} {1}" -f $ffmpeg, ($arguments -join " "))
    exit 0
}

& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg capture failed with exit code $LASTEXITCODE. Verify the exact window title."
}
if (-not (Test-Path -LiteralPath $resolvedOutput -PathType Leaf)) {
    throw "FFmpeg exited successfully but did not create the capture."
}

$capture = Get-Item -LiteralPath $resolvedOutput
$sha256 = (Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256).Hash.ToLowerInvariant()
$metadata = [ordered]@{
    schema = "openbfme.private-retail-window-capture"
    schemaVersion = 0
    capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    windowTitle = $WindowTitle
    durationSeconds = $DurationSeconds
    frameRate = $FrameRate
    audioDevice = $AudioDevice
    encoder = if ($UseNvenc) { "h264_nvenc" } else { "libx264" }
    fileName = $capture.Name
    byteLength = $capture.Length
    sha256 = $sha256
}
$metadataPath = "$resolvedOutput.json"
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath -Encoding UTF8
Write-Host "RETAIL_CAPTURE_RESULT ok=true bytes=$($capture.Length) sha256=$sha256"
Write-Host "RETAIL_CAPTURE_METADATA $metadataPath"
