[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("hud-default", "hud-unit-selected", "unit-soldier-idle", "unit-soldier-move", "unit-soldier-attack")]
    [string]$CaptureId,
    [ValidatePattern('^[1-9][0-9]*x[1-9][0-9]*$')]
    [string]$Viewport = "2560x1440",
    [string]$OutputRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

function Assert-PhysicalScratchPath {
    param([string]$Root, [string]$Candidate)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $candidateFull = [IO.Path]::GetFullPath($Candidate)
    $prefix = $rootFull + '\'
    if ($candidateFull -ne $rootFull -and -not $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escapes .private/scratch."
    }
    $relative = if ($candidateFull -eq $rootFull) { "" } else { $candidateFull.Substring($prefix.Length) }
    $cursor = $rootFull
    foreach ($segment in @($relative.Split([IO.Path]::DirectorySeparatorChar, [StringSplitOptions]::RemoveEmptyEntries))) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -Force -LiteralPath $cursor
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Capture path crosses a link or junction: $cursor"
            }
        }
        $cursor = Join-Path $cursor $segment
    }
    if (Test-Path -LiteralPath $cursor) {
        $item = Get-Item -Force -LiteralPath $cursor
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Capture path crosses a link or junction: $cursor"
        }
    }
}

function Get-PackTreeDigest {
    param([string]$Root)
    $rows = [Collections.Generic.List[string]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName)) {
        $relative = Get-M2OracleRelativePath $Root $file.FullName
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $rows.Add("$relative`0$($file.Length)`0$hash")
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hasher.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant() }
    finally { $hasher.Dispose() }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$privateScratch = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\scratch"))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $privateScratch "jobs\m2-oracle-first-capture-tranche"
}
$outputRootFull = [IO.Path]::GetFullPath($OutputRoot)
$scratchPrefix = $privateScratch.TrimEnd('\') + '\'
if (-not $outputRootFull.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must remain below .private/scratch."
}
Assert-PhysicalScratchPath $privateScratch $outputRootFull

$recipes = @{
    "hud-default" = @{
        OPENBFME_CAPTURE_CAMERA_FOCUS = "-43.77434,0"
        OPENBFME_CAPTURE_CAMERA_ZOOM = "0.18"
    }
    "hud-unit-selected" = @{
        OPENBFME_CAPTURE_FOCUS_BATTALION = "1"
        OPENBFME_CAPTURE_CAMERA_ZOOM = "0.18"
        OPENBFME_CAPTURE_SELECT_BATTALION = "1"
    }
    "unit-soldier-idle" = @{
        OPENBFME_CAPTURE_FOCUS_BATTALION = "1"
        OPENBFME_CAPTURE_CAMERA_ZOOM = "0.18"
    }
    "unit-soldier-move" = @{
        OPENBFME_CAPTURE_FOCUS_BATTALION = "1"
        OPENBFME_CAPTURE_CAMERA_ZOOM = "0.18"
        OPENBFME_CAPTURE_SELECT_BATTALION = "1"
        OPENBFME_CAPTURE_MOVE_OFFSET = "8,0"
        OPENBFME_CAPTURE_ADVANCE_TICKS = "6"
    }
    "unit-soldier-attack" = @{
        OPENBFME_CAPTURE_CAMERA_ZOOM = "0.18"
        OPENBFME_CAPTURE_ATTACK_PAIR = "1,101"
        OPENBFME_CAPTURE_ADVANCE_TICKS = "120"
        OPENBFME_CAPTURE_SETTLE_FRAMES = "60"
    }
}

$runId = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssfffZ")
$captureDirectory = Join-Path $outputRootFull "captures\$CaptureId"
$logDirectory = Join-Path $outputRootFull "logs\$CaptureId"
$receiptDirectory = Join-Path $outputRootFull "receipts\$CaptureId"
New-Item -ItemType Directory -Force -Path $captureDirectory, $logDirectory, $receiptDirectory | Out-Null
Assert-PhysicalScratchPath $privateScratch $captureDirectory
Assert-PhysicalScratchPath $privateScratch $logDirectory
Assert-PhysicalScratchPath $privateScratch $receiptDirectory
$outputPath = Join-Path $captureDirectory "$runId.png"
$logPath = Join-Path $logDirectory "$runId.log"
$receiptPath = Join-Path $receiptDirectory "$runId.json"
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to overwrite an existing capture output."
}
Assert-PhysicalScratchPath $privateScratch $outputPath
$godot = Resolve-ProofGodot "" $repoRoot
$contextBefore = Get-M2OracleContext $repoRoot
$contentRoot = [string]$contextBefore.contentRoot
$expectedPack = [string]$contextBefore.packRoot
$packTreeDigestBefore = Get-PackTreeDigest $expectedPack

$captureEnvironmentNames = @(
    "OPENBFME_CONTENT",
	"OPENBFME_CAPTURE_SCENARIO_MODE",
	"OPENBFME_CAPTURE_SCENARIO_ID",
    "OPENBFME_CAPTURE_PATH",
    "OPENBFME_CAPTURE_VIEWPORT",
    "OPENBFME_CAPTURE_CAMERA_FOCUS",
    "OPENBFME_CAPTURE_FOCUS_BATTALION",
    "OPENBFME_CAPTURE_CAMERA_ZOOM",
    "OPENBFME_CAPTURE_SELECT_BATTALION",
    "OPENBFME_CAPTURE_MOVE_OFFSET",
    "OPENBFME_CAPTURE_ATTACK_PAIR",
    "OPENBFME_CAPTURE_ADVANCE_TICKS",
	"OPENBFME_CAPTURE_UNCLAMPED",
	"OPENBFME_CAPTURE_DISABLE_FOG",
	"OPENBFME_CAPTURE_DISABLE_ROADS",
	"OPENBFME_CAPTURE_DISABLE_PROPS",
	"OPENBFME_CAPTURE_HIDE_PROP_TYPE",
	"OPENBFME_CAPTURE_UNSHADED_TERRAIN",
	"OPENBFME_CAPTURE_FORCE_TERRAIN_LIGHT_LAYER_ONE",
	"OPENBFME_CAPTURE_ADD_TEST_TERRAIN_LIGHT",
	"OPENBFME_CAPTURE_INVERT_DOMAIN_LIGHTS",
	"OPENBFME_CAPTURE_SETTLE_FRAMES",
	"OPENBFME_UI_PROBE",
	"OPENBFME_PROFILE_INIT",
	"OPENBFME_PROFILE_SYNC"
)
$savedEnvironment = @{}
foreach ($name in $captureEnvironmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
    [Environment]::SetEnvironmentVariable($name, $null, "Process")
}

try {
	[Environment]::SetEnvironmentVariable("OPENBFME_CONTENT", $contentRoot, "Process")
    [Environment]::SetEnvironmentVariable("OPENBFME_CAPTURE_PATH", $outputPath, "Process")
    [Environment]::SetEnvironmentVariable("OPENBFME_CAPTURE_VIEWPORT", $Viewport, "Process")
    [Environment]::SetEnvironmentVariable("OPENBFME_CAPTURE_SCENARIO_MODE", "1", "Process")
    [Environment]::SetEnvironmentVariable("OPENBFME_CAPTURE_SCENARIO_ID", $CaptureId, "Process")
    foreach ($entry in $recipes[$CaptureId].GetEnumerator()) {
        [Environment]::SetEnvironmentVariable([string]$entry.Key, [string]$entry.Value, "Process")
    }
    $priorErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $processOutput = & $godot --audio-driver WASAPI --path (Join-Path $repoRoot "game") --script res://tests/retail_render_capture_runner.gd 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $priorErrorActionPreference
    }
    $processText = ($processOutput | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    [IO.File]::WriteAllText($logPath, $processText + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
finally {
    foreach ($name in $captureEnvironmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
}

if ($exitCode -ne 0) { throw "Godot capture failed with exit code $exitCode. See $logPath" }
if ($processText -notmatch '(?m)^RETAIL_RENDER_CAPTURE_OK ') { throw "Godot capture emitted no success marker. See $logPath" }
if ($CaptureId -eq "unit-soldier-attack" -and $processText -notmatch '(?m)^RETAIL_RENDER_SCENARIO_STATE id=unit-soldier-attack state=attack paused=true\r?$') {
    throw "Attack scenario did not reach the authoritative attack state. See $logPath"
}
if ($CaptureId -eq "unit-soldier-move" -and $processText -notmatch '(?m)^RETAIL_RENDER_SCENARIO_STATE id=unit-soldier-move state=run paused=true\r?$') {
    throw "Move scenario did not reach the authoritative run state. See $logPath"
}
if ($processText -match '(?im)\b(error|warning|orphan|leak|remaining resources|rid allocations)\b') {
    throw "Godot capture emitted a forbidden diagnostic. See $logPath"
}
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) { throw "Godot did not write the capture PNG." }
$successLine = [regex]::Match($processText, '(?m)^RETAIL_RENDER_CAPTURE_OK .+$').Value
$normalizedExpectedPack = $expectedPack.Replace('\', '/')
if ($successLine -notmatch (' pack=' + [regex]::Escape($normalizedExpectedPack) + ' ')) {
    throw "Godot mounted a pack other than the selected private bundle. See $logPath"
}

Add-Type -AssemblyName System.Drawing
$bitmap = [Drawing.Image]::FromFile($outputPath)
try { $actualViewport = "$($bitmap.Width)x$($bitmap.Height)" }
finally { $bitmap.Dispose() }
if ($actualViewport -ne $Viewport) { throw "Capture dimensions are $actualViewport instead of $Viewport." }

$contextAfter = Get-M2OracleContext $repoRoot
$packTreeDigestAfter = Get-PackTreeDigest ([string]$contextAfter.packRoot)
if (
    $contextAfter.gitRevision -ne $contextBefore.gitRevision -or
    $contextAfter.dirtyStateDigest -ne $contextBefore.dirtyStateDigest -or
    $contextAfter.profileSha256 -ne $contextBefore.profileSha256 -or
    $contextAfter.bundleSha256 -ne $contextBefore.bundleSha256 -or
    $packTreeDigestAfter -ne $packTreeDigestBefore
) { throw "Source, profile, bundle, or selected-pack contents changed during capture." }
Assert-PhysicalScratchPath $privateScratch $outputPath
$sha256 = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant()
$cameraLine = [regex]::Match($processText, '(?m)^RETAIL_RENDER_CAMERA .+$').Value.TrimEnd("`r")
$semanticLine = [regex]::Match($processText, '(?m)^RETAIL_RENDER_SCENARIO_STATE .+$').Value.TrimEnd("`r")
$receipt = [ordered]@{
    schema = "openbfme.m2-godot-scenario-capture-receipt"
    schemaVersion = 0
    createdAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    id = $CaptureId
    viewport = $Viewport
    approved = $false
    gitRevision = [string]$contextAfter.gitRevision
    dirtyStateDigest = [string]$contextAfter.dirtyStateDigest
    profileSha256 = [string]$contextAfter.profileSha256
    bundleSha256 = [string]$contextAfter.bundleSha256
    packTreeSha256 = $packTreeDigestAfter
    pngPath = Get-M2OracleRelativePath $outputRootFull $outputPath
    pngSha256 = $sha256
    logPath = Get-M2OracleRelativePath $outputRootFull $logPath
    camera = $cameraLine
    semanticState = $semanticLine
}
[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 6) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Host "M2_GODOT_SCENARIO_CAPTURE_RESULT id=$CaptureId viewport=$Viewport sha256=$sha256 approved=false"
Write-Host "M2_GODOT_SCENARIO_CAPTURE_IDENTITY git=$($contextAfter.gitRevision) dirty=$($contextAfter.dirtyStateDigest) profile=$($contextAfter.profileSha256) bundle=$($contextAfter.bundleSha256) pack_tree=$packTreeDigestAfter"
Write-Host "M2_GODOT_SCENARIO_CAPTURE_PATH $outputPath"
Write-Host "M2_GODOT_SCENARIO_CAPTURE_LOG $logPath"
Write-Host "M2_GODOT_SCENARIO_CAPTURE_RECEIPT $receiptPath"
