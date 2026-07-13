[CmdletBinding()]
param(
    [string]$Install = "F:\BFME2",
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "RETAIL_PIPELINE_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$cli = Join-Path $repoRoot "tools\openbfme_import.py"
$pythonBootstrap = Join-Path $PSScriptRoot "bootstrap-importer-python.ps1"
$stateRoot = if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
    [IO.Path]::GetFullPath($env:OPENBFME_IMPORT_ROOT)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work"))
}
$env:OPENBFME_IMPORT_ROOT = $stateRoot
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $pythonBootstrap -StateRoot $stateRoot
if ($LASTEXITCODE -ne 0) { throw "Importer Python bootstrap failed." }
$python = Join-Path $stateRoot "tools\python-3.12-env\Scripts\python.exe"
$env:PYTHONPATH = Join-Path $repoRoot "importer"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Invoke-ImporterJson {
    param([string]$Name, [string[]]$Arguments)
    $output = @(& $python $cli --json @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) { Write-Host "$gate $Name $line" }
    if ($exitCode -ne 0) { throw "$Name failed with exit code $exitCode" }
    return (($output -join "`n") | ConvertFrom-Json)
}

try {
    foreach ($path in @($cli, (Join-Path $repoRoot "importer\profiles\men-fords-v0.json"))) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing retail gate dependency: $path"
    }

    $importerTestOutput = Invoke-ProofChecked $gate "importer_tests" $python @("-m", "unittest", "discover", "-s", (Join-Path $repoRoot "importer\tests"), "-v") '(?m)^OK\s*$'
    Assert-ProofTrue ($importerTestOutput -match '(?m)^Ran 203 tests in ') "Importer suite did not execute exactly 203 tests."

    $bootstrap = Invoke-ImporterJson "bootstrap" @("bootstrap-tools")
    Assert-ProofTrue ([bool]$bootstrap.ready) "Pinned external tools are not ready."
    $doctor = Invoke-ImporterJson "doctor" @("doctor", "--install", $Install, "--deep")
    Assert-ProofTrue ([bool]$doctor.ready) "Retail install doctor is not ready."
    $plan = Invoke-ImporterJson "plan" @("plan", "--install", $Install, "--profile", "men-fords-v0")
    Assert-ProofTrue (
        [bool]$plan.ready -and
        [int]$plan.resource_count -eq 53 -and
        [int]$plan.selected_file_count -eq 264 -and
        $plan.profile_sha256 -match '^[0-9a-f]{64}$' -and
        $plan.importer_recipe_sha256 -match '^[0-9a-f]{64}$'
    ) "Exact retail profile/recipe did not resolve its pinned 53-resource, 264-file closure."

    $first = Invoke-ImporterJson "build_a" @("build", "--install", $Install, "--profile", "men-fords-v0", "--force")
    $second = Invoke-ImporterJson "build_b" @("build", "--install", $Install, "--profile", "men-fords-v0", "--force")
    Assert-ProofTrue ([bool]$first.valid -and [bool]$second.valid -and [bool]$first.semantic_provenance -and [bool]$second.semantic_provenance) "A retail build failed its semantic provenance audit."
    Assert-ProofTrue ($first.bundle_sha256 -match '^[0-9a-f]{64}$' -and $first.bundle_sha256 -eq $second.bundle_sha256) "Repeat builds were not byte-reproducible."
    Write-Host "$gate reproducibility PASS bundle_sha256=$($first.bundle_sha256)"

    $audit = Invoke-ImporterJson "audit" @("audit", [string]$second.pack)
    Assert-ProofTrue (
        [bool]$audit.valid -and
        [bool]$audit.semantic_provenance -and
        $audit.provenance_contract -eq 'openbfme.retail-import-provenance-v1' -and
        $audit.profile -eq 'men-fords-v0' -and
        $audit.profile_sha256 -eq $plan.profile_sha256 -and
        $audit.importer_recipe_sha256 -eq $plan.importer_recipe_sha256 -and
        [int]$audit.source_archive_count -eq 12 -and
        [int]$audit.provenance_entry_count -eq 264 -and
        [int]$audit.tool_attestation_count -ge 5 -and
        [int]$audit.checked_files -eq 162 -and
        [int]$audit.checked_outputs -eq 158
    ) "Pack semantic provenance audit failed."

    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    [void](Invoke-ProofChecked $gate "stage11_12_groups_and_routes" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/stage11_12_runner.gd") '(?m)^STAGE 11/12 TESTS: 26 passed, 0 failed\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "stage14_15_base_loop" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/stage14_15_sim_runner.gd") '(?m)^STAGE 14/15 SIM TESTS: 31 passed, 0 failed\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "stage15_menu_and_audio" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/stage15_menu_runner.gd") '(?m)^STAGE15_MENU_RESULT passed=25 failed=0\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "retail_pack_runtime" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/retail_pack_runner.gd") '(?m)^RETAIL_PACK_RESULT passed=84 failed=0\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "playable_retail_slice" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/retail_slice_runner.gd") '(?m)^RETAIL_SLICE_RESULT passed=142 failed=0\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "external_pack_security" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/external_pack_runner.gd") '(?m)^EXTERNAL_PACK_RESULT passed=63 failed=0\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "legacy_regression" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/cli_runner.gd") '(?m)^STAGE TESTS: 101 passed, 0 failed\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "export_firewall_self_test" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "test-export-scan.ps1")) '(?m)^EXPORT_SCAN_SELF_TEST PASS ')
    [void](Invoke-ProofChecked $gate "export_firewall" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "export-scan.ps1"), "-Root", $gameRoot) '(?m)^EXPORT_SCAN PASS ')

    Write-Host "$gate PASS bundle_sha256=$($second.bundle_sha256)"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
