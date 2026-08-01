[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureId,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ReviewedBy,
    [Parameter(Mandatory = $true)]
    [ValidateNotNull()]
    [string]$Notes,
    [ValidateRange(0, 1000)]
    [int]$UnresolvedSeverity0 = 0,
    [ValidateRange(0, 1000)]
    [int]$UnresolvedSeverity1 = 0,
    [string]$ManifestPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
Assert-M2OracleTrue ($script:M2OracleCaptureIds -ccontains $CaptureId) "Unknown capture ID '$CaptureId'."
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $context.oracleRoot "m2-men-fords-captures.json"
}
$manifestPath = Assert-M2OracleContainedPath $ManifestPath $context.oracleRoot
Assert-M2OracleTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Capture manifest is missing."
$manifest = Get-Content -Raw -LiteralPath $manifestPath -Encoding UTF8 | ConvertFrom-Json
Assert-M2OracleManifestIdentity $manifest $context
$row = @($manifest.captures | Where-Object { [string]$_.id -ceq $CaptureId })
Assert-M2OracleTrue ($row.Count -eq 1) "Capture row '$CaptureId' is not unique."
$capture = $row[0]
Assert-M2OracleTrue (-not [string]::IsNullOrWhiteSpace([string]$capture.viewport)) "Capture pair lacks a viewport."
foreach ($side in @("retail", "godot")) {
    $cameraState = [string]$capture.("${side}CameraState")
    $relative = [string]$capture.("${side}Path")
    $expectedSha = [string]$capture.("${side}Sha256")
    Assert-M2OracleTrue (-not [string]::IsNullOrWhiteSpace($cameraState)) "Capture pair lacks $side camera state."
    Assert-M2OracleTrue (-not [string]::IsNullOrWhiteSpace($relative) -and $expectedSha -match '^[0-9a-f]{64}$') "Capture pair lacks $side evidence."
    $path = Assert-M2OracleContainedPath (Join-Path (Split-Path -Parent $manifestPath) $relative) $context.oracleRoot
    Assert-M2OracleTrue (Test-Path -LiteralPath $path -PathType Leaf) "$side image is missing."
    $actualSha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-M2OracleTrue ($actualSha -eq $expectedSha) "$side image hash changed."
}
$capture.unresolvedSeverity0 = $UnresolvedSeverity0
$capture.unresolvedSeverity1 = $UnresolvedSeverity1
$capture.notes = $Notes
$capture.approved = ($UnresolvedSeverity0 -eq 0 -and $UnresolvedSeverity1 -eq 0)
$capture.approvedBy = $ReviewedBy
$capture.approvedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
$finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while reviewing the oracle pair."
Write-M2OracleJson $manifest $manifestPath
Write-Host "M2_ORACLE_REVIEW_RESULT id=$CaptureId approved=$(([bool]$capture.approved).ToString().ToLowerInvariant()) severity0=$UnresolvedSeverity0 severity1=$UnresolvedSeverity1 reviewer=$ReviewedBy"
