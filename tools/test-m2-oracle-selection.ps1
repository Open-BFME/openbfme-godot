[CmdletBinding()]
param(
    [string]$ContentRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$previousContentRoot = [Environment]::GetEnvironmentVariable("OPENBFME_CONTENT", "Process")

try {
    if (-not [string]::IsNullOrWhiteSpace($ContentRoot)) {
        $env:OPENBFME_CONTENT = [IO.Path]::GetFullPath($ContentRoot)
    }
    $context = Get-M2OracleContext $repoRoot
    $selectionPath = Join-Path $context.contentRoot "selection.json"
    $selection = Get-Content -Raw -LiteralPath $selectionPath -Encoding UTF8 | ConvertFrom-Json
    Assert-M2OracleTrue ([string]$selection.activePack -match '^rotwk-men-vslice/[0-9a-f]{64}$') "Current selection drifted from the immutable RotWK Men shape."
    Assert-M2OracleTrue ([IO.Path]::GetFullPath((Join-Path $context.contentRoot ([string]$selection.activePack))) -eq $context.packRoot) "M2 oracle context did not resolve the current active pack."
    Write-Host "M2_ORACLE_SELECTION_TEST PASS active_pack=$([string]$selection.activePack) profile_sha256=$($context.profileSha256)"
}
finally {
    [Environment]::SetEnvironmentVariable("OPENBFME_CONTENT", $previousContentRoot, "Process")
}
