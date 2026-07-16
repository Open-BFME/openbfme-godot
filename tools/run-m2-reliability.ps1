[CmdletBinding()]
param(
    [string]$GodotPath = "",
    [ValidateRange(5, 86400)][int]$DurationSeconds = 1800,
    [string]$Output = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$gate = "M2_RELIABILITY"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$contentRoot = Join-Path $repoRoot ".private\content-packs"
$selectionPath = Join-Path $contentRoot "selection.json"
$profilePath = Join-Path $repoRoot ".private\retail-work\profiles\men-fords-v0-complete.generated.json"
$expectedProfileSha256 = "0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use|SCRIPT ERROR)\b'

try {
    foreach ($path in @($selectionPath, $profilePath, (Join-Path $gameRoot "tests\m2_live_soak_runner.gd"))) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing reliability dependency: $path"
    }
    $context = Get-M2OracleContext $repoRoot
    $profileSha256 = [string]$context.profileSha256
    $bundleSha256 = [string]$context.bundleSha256
    $packRoot = [string]$context.packRoot
    Assert-ProofTrue ($profileSha256 -eq $expectedProfileSha256) "Completion profile hash changed."
    $identity = Get-ProofWorkingTreeIdentity $repoRoot
    $approvalPath = Join-Path $context.oracleRoot "m2-men-fords-approval.json"
    Assert-ProofTrue (Test-Path -LiteralPath $approvalPath -PathType Leaf) "Pending oracle approval is missing; freeze performance thresholds before the final soak."
    $pendingApproval = Read-ProofJson $approvalPath
    Assert-ProofTrue (
        [string]$pendingApproval.schema -eq 'openbfme.m2-men-fords-oracle-approval' -and
        [int]$pendingApproval.schemaVersion -eq 0 -and
        -not [bool]$pendingApproval.approved -and
        -not [string]::IsNullOrWhiteSpace([string]$pendingApproval.createdAtUtc)
    ) "Reliability requires one timestamped pending oracle approval."
    Assert-ProofTrue (
        [string]$pendingApproval.profileSha256 -eq $profileSha256 -and
        [string]$pendingApproval.bundleSha256 -eq $bundleSha256 -and
        [string]$pendingApproval.gitRevision -eq [string]$identity.revision -and
        [string]$pendingApproval.dirtyStateDigest -eq [string]$identity.dirtyStateDigest
    ) "Pending oracle approval targets another identity."
    $thresholds = $pendingApproval.performanceThresholds
    Assert-ProofTrue (
        $null -ne $thresholds -and
        [double]$thresholds.minimumAverageFps -gt 0.0 -and
        [double]$thresholds.minimumOnePercentLowFps -gt 0.0 -and
        [long]$thresholds.maximumPeakMemoryBytes -gt 0 -and
        [long]$thresholds.maximumMemoryGrowthBytes -ge 0
    ) "Pending oracle approval has invalid frozen performance thresholds."
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $env:OPENBFME_CONTENT = $packRoot

    $restartRows = [Collections.Generic.List[object]]::new()
    for ($index = 1; $index -le 3; $index++) {
        $restartOutput = Invoke-ProofChecked $gate "restart_$index" $godot @("--headless", "--audio-driver", "WASAPI", "--path", $gameRoot, "--script", "res://tests/retail_slice_runner.gd") '(?m)^RETAIL_SLICE_RESULT passed=208 failed=0\s*$' $forbiddenDiagnostics
        $signatureMatch = [regex]::Match($restartOutput, '(?m)^RETAIL_SLICE_SIGNATURE ([0-9A-F]{8})\s*$')
        Assert-ProofTrue $signatureMatch.Success "Restart $index did not emit a deterministic signature."
        $restartRows.Add([ordered]@{ index = $index; bundleSha256 = $bundleSha256; signature = $signatureMatch.Groups[1].Value })
    }
    $signatures = @($restartRows | ForEach-Object { [string]$_.signature } | Select-Object -Unique)
    Assert-ProofTrue ($signatures.Count -eq 1) "Three clean restarts did not reproduce one signature."

    $outputPath = if ([string]::IsNullOrWhiteSpace($Output)) {
        Join-Path $repoRoot ".private\retail-work\oracle\m2-men-fords-reliability.json"
    } else { [IO.Path]::GetFullPath($Output) }
    $privateRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private"))
    Assert-ProofTrue ($outputPath.StartsWith($privateRoot, [StringComparison]::OrdinalIgnoreCase)) "Reliability evidence escaped .private."
    $rawOutput = Join-Path $repoRoot ".private\scratch\m2-live-soak-raw.json"
    $env:OPENBFME_M2_SOAK_OUTPUT = $rawOutput
    $env:OPENBFME_M2_SOAK_SECONDS = [string]$DurationSeconds
    $env:OPENBFME_M2_PROFILE_SHA256 = $profileSha256
    $env:OPENBFME_M2_GIT_REVISION = [string]$identity.revision
    $env:OPENBFME_M2_DIRTY_STATE_DIGEST = [string]$identity.dirtyStateDigest
    $soakOutput = Invoke-ProofChecked $gate "live_soak" $godot @("--audio-driver", "WASAPI", "--path", $gameRoot, "--script", "res://tests/m2_live_soak_runner.gd") '(?m)^M2_LIVE_SOAK_RESULT ' $forbiddenDiagnostics
    $raw = Read-ProofJson $rawOutput
    Assert-ProofTrue ([string]$raw.bundleSha256 -eq $bundleSha256) "Live soak mounted another bundle."
    Assert-ProofTrue ([string]$raw.profileSha256 -eq $profileSha256) "Live soak used another profile."
    Assert-ProofTrue ([string]$raw.gitRevision -eq [string]$identity.revision -and [string]$raw.dirtyStateDigest -eq [string]$identity.dirtyStateDigest) "Live soak identity changed."
    Assert-ProofTrue ([double]$raw.actualDurationSeconds -ge [double]$DurationSeconds) "Live soak ended before the requested active duration."
    Assert-ProofTrue ([int]$raw.completedMatches -ge 3 -and [int]$raw.readyStarts -ge 3) "Live soak did not complete three matches/restarts."
    Assert-ProofTrue (
        @($raw.restartLoadDurationsMsec).Count -eq [int]$raw.completedMatches -and
        [int]$raw.maximumRestartLoadMsec -gt 0 -and
        [int]$raw.maximumRestartLoadMsec -le 5000
    ) "Live soak restart loading exceeded the unchanged five-second initialization budget or lacks exact evidence."
    Assert-ProofTrue ([string]$raw.viewport -eq '1920x1080' -and -not [string]::IsNullOrWhiteSpace([string]$raw.renderingDriver) -and -not [string]::IsNullOrWhiteSpace([string]$raw.videoAdapter)) "Live soak renderer evidence is incomplete."
    Assert-ProofTrue ([double]$raw.averageFps -ge [double]$thresholds.minimumAverageFps) "Live soak average FPS missed the pre-frozen threshold."
    Assert-ProofTrue ([double]$raw.onePercentLowFps -ge [double]$thresholds.minimumOnePercentLowFps) "Live soak one-percent-low FPS missed the pre-frozen threshold."
    Assert-ProofTrue ([long]$raw.peakMemoryBytes -le [long]$thresholds.maximumPeakMemoryBytes) "Live soak peak memory exceeded the pre-frozen threshold."
    Assert-ProofTrue ([long]$raw.memoryGrowthBytes -le [long]$thresholds.maximumMemoryGrowthBytes) "Live soak memory growth exceeded the pre-frozen threshold."

    $evidence = [ordered]@{
        schema = 'openbfme.m2-men-fords-reliability'
        schemaVersion = 0
        profileSha256 = $profileSha256
        bundleSha256 = $bundleSha256
        gitRevision = [string]$identity.revision
        dirtyStateDigest = [string]$identity.dirtyStateDigest
        liveSoak = $raw
        restarts = @($restartRows)
        deterministicRestartSignature = $signatures[0]
        thresholdsFrozenAtUtc = [string]$pendingApproval.createdAtUtc
        performanceThresholds = $thresholds
        diagnosticCount = 0
    }
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$identity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$identity.dirtyStateDigest
    ) "Working-tree identity changed during the reliability run."
    New-Item -ItemType Directory -Force (Split-Path -Parent $outputPath) | Out-Null
    $evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding UTF8
    $evidenceSha256 = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "$gate PASS evidence=$outputPath sha256=$evidenceSha256 duration=$([double]$raw.actualDurationSeconds) average_fps=$([double]$raw.averageFps) one_percent_low_fps=$([double]$raw.onePercentLowFps)"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
