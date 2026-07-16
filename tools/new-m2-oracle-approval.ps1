[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1.0, 1000.0)]
    [double]$MinimumAverageFps,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1.0, 1000.0)]
    [double]$MinimumOnePercentLowFps,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$MaximumPeakMemoryBytes,
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [long]::MaxValue)]
    [long]$MaximumMemoryGrowthBytes,
    [string]$ApprovalPath = "",
    [string]$ManifestPath = "",
    [string]$ReliabilityPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
if ([string]::IsNullOrWhiteSpace($ApprovalPath)) { $ApprovalPath = Join-Path $context.oracleRoot "m2-men-fords-approval.json" }
if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $context.oracleRoot "m2-men-fords-captures.json" }
if ([string]::IsNullOrWhiteSpace($ReliabilityPath)) { $ReliabilityPath = Join-Path $context.oracleRoot "m2-men-fords-reliability.json" }
$approvalPath = Assert-M2OracleContainedPath $ApprovalPath $context.oracleRoot
$manifestPath = Assert-M2OracleContainedPath $ManifestPath $context.oracleRoot
$reliabilityPath = Assert-M2OracleContainedPath $ReliabilityPath $context.oracleRoot
Assert-M2OracleTrue (-not (Test-Path -LiteralPath $approvalPath)) "Approval already exists. Performance thresholds are frozen and cannot be overwritten; delete it only when intentionally starting the entire identity-bound oracle again."
Assert-M2OracleTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Capture manifest is missing."
$manifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleManifestIdentity $manifest $context
Assert-M2OracleTrue ($MinimumOnePercentLowFps -le $MinimumAverageFps) "One-percent-low threshold cannot exceed average-FPS threshold."

$approval = [ordered]@{
    schema = "openbfme.m2-men-fords-oracle-approval"
    schemaVersion = 0
    createdAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    approved = $false
    approvedBy = ""
    approvedAtUtc = ""
    profileSha256 = $context.profileSha256
    bundleSha256 = $context.bundleSha256
    gitRevision = $context.gitRevision
    dirtyStateDigest = $context.dirtyStateDigest
    captureManifest = Get-M2OracleRelativePath (Split-Path -Parent $approvalPath) $manifestPath
    captureManifestSha256 = ""
    reliabilityEvidence = Get-M2OracleRelativePath (Split-Path -Parent $approvalPath) $reliabilityPath
    reliabilityEvidenceSha256 = ""
    unresolvedSeverity0 = 0
    unresolvedSeverity1 = 0
    performanceThresholds = [ordered]@{
        minimumAverageFps = $MinimumAverageFps
        minimumOnePercentLowFps = $MinimumOnePercentLowFps
        maximumPeakMemoryBytes = $MaximumPeakMemoryBytes
        maximumMemoryGrowthBytes = $MaximumMemoryGrowthBytes
    }
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $approvalPath) | Out-Null
$finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while freezing oracle thresholds."
Write-M2OracleJson $approval $approvalPath
Write-Host "M2_ORACLE_APPROVAL_CREATED approved=false thresholds_frozen=true"
Write-Host "M2_ORACLE_APPROVAL_PATH $approvalPath"
