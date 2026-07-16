[CmdletBinding()]
param(
    [string]$OutputPath = "",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$context = Get-M2OracleContext $repoRoot
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $context.oracleRoot "m2-men-fords-captures.json"
}
$manifestPath = Assert-M2OracleContainedPath $OutputPath $context.oracleRoot
if ((Test-Path -LiteralPath $manifestPath) -and -not $Force) {
    throw "Capture manifest already exists. Use -Force only when intentionally invalidating the entire prior review."
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $manifestPath) | Out-Null

$captures = foreach ($id in $script:M2OracleCaptureIds) {
    [ordered]@{
        id = $id
        profileSha256 = $context.profileSha256
        bundleSha256 = $context.bundleSha256
        gitRevision = $context.gitRevision
        dirtyStateDigest = $context.dirtyStateDigest
        viewport = ""
        cameraState = ""
        retailPath = ""
        retailSha256 = ""
        godotPath = ""
        godotSha256 = ""
        approved = $false
        approvedBy = ""
        approvedAtUtc = ""
        unresolvedSeverity0 = 0
        unresolvedSeverity1 = 0
        notes = ""
    }
}
$manifest = [ordered]@{
    schema = "openbfme.m2-men-fords-oracle-captures"
    schemaVersion = 0
    createdAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    profileSha256 = $context.profileSha256
    bundleSha256 = $context.bundleSha256
    gitRevision = $context.gitRevision
    dirtyStateDigest = $context.dirtyStateDigest
    captures = @($captures)
}
$finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
Assert-M2OracleTrue ([string]$finalIdentity.revision -eq $context.gitRevision -and [string]$finalIdentity.dirtyStateDigest -eq $context.dirtyStateDigest) "Source identity changed while creating the oracle workspace."
Write-M2OracleJson $manifest $manifestPath
Write-Host "M2_ORACLE_WORKSPACE_RESULT rows=$($script:M2OracleCaptureIds.Count) approved=0"
Write-Host "M2_ORACLE_WORKSPACE_PATH $manifestPath"
Write-Host "M2_ORACLE_IDENTITY profile=$($context.profileSha256) bundle=$($context.bundleSha256) git=$($context.gitRevision) dirty=$($context.dirtyStateDigest)"
