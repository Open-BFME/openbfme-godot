[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ApprovedBy,
    [string]$ApprovalPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")
. (Join-Path $PSScriptRoot "m2-reliability-evidence-common.ps1")

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
if ([string]::IsNullOrWhiteSpace($ApprovalPath)) { $ApprovalPath = Join-Path $context.oracleRoot "m2-men-fords-approval.json" }
$approvalPath = Assert-M2OracleContainedPath $ApprovalPath $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $approvalPath -PathType Leaf) "Frozen pending approval is missing. Run tools/new-m2-oracle-approval.ps1 before observing final soak results."
$approval = Get-Content -Raw -LiteralPath $approvalPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleTrue ([string]$approval.schema -eq "openbfme.m2-men-fords-oracle-approval" -and [int]$approval.schemaVersion -eq 0) "Approval schema is invalid."
Assert-M2OracleTrue (-not [bool]$approval.approved) "Approval is already final."
Assert-M2OracleTrue (-not [string]::IsNullOrWhiteSpace([string]$approval.createdAtUtc)) "Approval has no threshold-freeze timestamp."
Assert-M2OracleTrue ([string]$approval.profileSha256 -eq $context.profileSha256 -and [string]$approval.bundleSha256 -eq $context.bundleSha256) "Approval targets another pack identity."
Assert-M2OracleTrue ([string]$approval.gitRevision -eq $context.gitRevision -and [string]$approval.dirtyStateDigest -eq $context.dirtyStateDigest) "Approval targets another source identity."
$thresholds = $approval.performanceThresholds
Assert-M2OracleTrue ($null -ne $thresholds -and [double]$thresholds.minimumAverageFps -gt 0 -and [double]$thresholds.minimumOnePercentLowFps -gt 0 -and [long]$thresholds.maximumPeakMemoryBytes -gt 0 -and [long]$thresholds.maximumMemoryGrowthBytes -ge 0 -and [long]$thresholds.maximumLateWindowMemoryGrowthBytes -ge 0 -and [long]$thresholds.maximumLateWindowMemoryGrowthBytes -le [long]$thresholds.maximumMemoryGrowthBytes) "Frozen performance thresholds are invalid."

$manifestPath = Assert-M2OracleContainedPath (Join-Path (Split-Path -Parent $approvalPath) ([string]$approval.captureManifest)) $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Capture manifest is missing."
$manifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleManifestIdentity $manifest $context
foreach ($capture in @($manifest.captures)) {
    Assert-M2OracleTrue ([bool]$capture.approved -and [int]$capture.unresolvedSeverity0 -eq 0 -and [int]$capture.unresolvedSeverity1 -eq 0) "Capture '$([string]$capture.id)' is not approved with zero severity-0/1 differences."
    Assert-M2OracleTrue (-not [string]::IsNullOrWhiteSpace([string]$capture.approvedBy) -and -not [string]::IsNullOrWhiteSpace([string]$capture.approvedAtUtc)) "Capture '$([string]$capture.id)' lacks reviewer evidence."
    foreach ($side in @("retail", "godot")) {
        $path = Assert-M2OracleContainedPath (Join-Path (Split-Path -Parent $manifestPath) ([string]$capture.("${side}Path"))) $context.oracleRoot
        Assert-M2OracleTrue (Test-Path -LiteralPath $path -PathType Leaf) "Capture '$([string]$capture.id)' lacks $side image."
        $actualSha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-M2OracleTrue ($actualSha -eq [string]$capture.("${side}Sha256")) "Capture '$([string]$capture.id)' $side hash changed."
    }
}

$reliabilityPath = Assert-M2OracleContainedPath (Join-Path (Split-Path -Parent $approvalPath) ([string]$approval.reliabilityEvidence)) $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $reliabilityPath -PathType Leaf) "Final reliability evidence is missing."
$reliability = Get-Content -Raw -LiteralPath $reliabilityPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleTrue ([string]$reliability.schema -eq "openbfme.m2-men-fords-reliability" -and [int]$reliability.schemaVersion -eq 0) "Reliability schema is invalid."
Assert-M2OracleTrue ([string]$reliability.profileSha256 -eq $context.profileSha256 -and [string]$reliability.bundleSha256 -eq $context.bundleSha256) "Reliability evidence targets another pack identity."
Assert-M2OracleTrue ([string]$reliability.gitRevision -eq $context.gitRevision -and [string]$reliability.dirtyStateDigest -eq $context.dirtyStateDigest) "Reliability evidence targets another source identity."
Assert-M2OracleTrue ([int]$reliability.diagnosticCount -eq 0) "Reliability evidence contains forbidden diagnostics."
Assert-M2OracleTrue ([string]$reliability.thresholdsFrozenAtUtc -eq [string]$approval.createdAtUtc) "Reliability evidence was not bound to this pre-frozen approval."
$reliabilityThresholds = $reliability.performanceThresholds
Assert-M2OracleTrue (
    [double]$reliabilityThresholds.minimumAverageFps -eq [double]$thresholds.minimumAverageFps -and
    [double]$reliabilityThresholds.minimumOnePercentLowFps -eq [double]$thresholds.minimumOnePercentLowFps -and
    [long]$reliabilityThresholds.maximumPeakMemoryBytes -eq [long]$thresholds.maximumPeakMemoryBytes -and
    [long]$reliabilityThresholds.maximumMemoryGrowthBytes -eq [long]$thresholds.maximumMemoryGrowthBytes -and
    [long]$reliabilityThresholds.maximumLateWindowMemoryGrowthBytes -eq [long]$thresholds.maximumLateWindowMemoryGrowthBytes
) "Reliability evidence threshold snapshot changed."
$soak = $reliability.liveSoak
Assert-M2ReliabilitySoakEvidence -Soak $soak -MinimumDurationSeconds 1800 -MaximumMemoryGrowthBytes ([long]$thresholds.maximumMemoryGrowthBytes) -MaximumLateWindowMemoryGrowthBytes ([long]$thresholds.maximumLateWindowMemoryGrowthBytes)
$restarts = @($reliability.restarts)
Assert-M2OracleTrue ($restarts.Count -eq 3 -and @($restarts | ForEach-Object { [string]$_.signature } | Select-Object -Unique).Count -eq 1) "Three clean full-match restarts do not share one deterministic signature."
Assert-M2OracleTrue (@($restarts | Where-Object { [string]$_.bundleSha256 -ne $context.bundleSha256 }).Count -eq 0) "A clean full-match restart mounted another bundle."
Assert-M2OracleTrue ([double]$soak.averageFps -ge [double]$thresholds.minimumAverageFps) "Average FPS missed the pre-frozen threshold."
Assert-M2OracleTrue ([double]$soak.onePercentLowFps -ge [double]$thresholds.minimumOnePercentLowFps) "One-percent-low FPS missed the pre-frozen threshold."
Assert-M2OracleTrue ([long]$soak.peakMemoryBytes -le [long]$thresholds.maximumPeakMemoryBytes) "Peak memory exceeded the pre-frozen threshold."

$approval.captureManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$approval.reliabilityEvidenceSha256 = (Get-FileHash -LiteralPath $reliabilityPath -Algorithm SHA256).Hash.ToLowerInvariant()
$approval.unresolvedSeverity0 = 0
$approval.unresolvedSeverity1 = 0
$approval.approvedBy = $ApprovedBy
$approval.approvedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
$approval.approved = $true
$finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while finalizing oracle approval."
Write-M2OracleJson $approval $approvalPath
Write-Host "M2_ORACLE_APPROVAL_RESULT approved=true reviewer=$ApprovedBy captures=$(@($manifest.captures).Count) soak_seconds=$([double]$soak.actualDurationSeconds)"
