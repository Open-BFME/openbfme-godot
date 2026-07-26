[CmdletBinding()]
param(
    [string]$Install = "$env:BFME2_INSTALL",
    [string]$GodotPath = "",
    [switch]$IntegrationOwnerPublish
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "RETAIL_PIPELINE_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$cli = Join-Path $repoRoot "tools\openbfme_import.py"
$pythonBootstrap = Join-Path $PSScriptRoot "bootstrap-importer-python.ps1"
$profilePath = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work\profiles\men-fords-v0-complete.generated.json"))
$expectedProfileId = "men-fords-v0-complete-generated"
$expectedProfileSha256 = "0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d"
$expectedPackId = "bfme2-men-vslice"
$minimumImporterTestCount = 999
$maximumImporterSkipCount = 5
$publishRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\content-packs"))
$stateRoot = if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
    [IO.Path]::GetFullPath($env:OPENBFME_IMPORT_ROOT)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work"))
}
$env:OPENBFME_IMPORT_ROOT = $stateRoot
$expectedBuildPackPath = [IO.Path]::GetFullPath((Join-Path $stateRoot "packs\$expectedPackId"))
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

function Invoke-GodotPassedFloor {
    param(
        [string]$Name,
        [string]$Runner,
        [string]$CountPattern,
        [int]$MinimumPassed
    )
    $output = Invoke-ProofChecked $gate $Name $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/$Runner") $CountPattern $forbiddenDiagnostics
    $countMatch = [regex]::Match($output, $CountPattern)
    Assert-ProofTrue ($countMatch.Success -and [int]$countMatch.Groups[1].Value -ge $MinimumPassed) "$Name passed fewer than the protected baseline of $MinimumPassed checks."
}

try {
    foreach ($path in @($cli, $profilePath)) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing retail gate dependency: $path"
    }
    $profileBytes = [IO.File]::ReadAllBytes($profilePath)
    $profileSha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-ProofTrue ($profileSha256 -eq $expectedProfileSha256) "Generated completion profile hash changed; integration-owner review is required."
    $profileDocument = ([Text.Encoding]::UTF8.GetString($profileBytes) | ConvertFrom-Json)
    Assert-ProofTrue (
        [int]$profileDocument.format -eq 1 -and
        [string]$profileDocument.id -eq $expectedProfileId -and
        [string]$profileDocument.pack.id -eq $expectedPackId -and
        [string]$profileDocument.pack.schema -eq 'openbfme.content-pack' -and
        $profileDocument.pack.vertical_slice_complete -is [bool] -and
        -not [bool]$profileDocument.pack.vertical_slice_complete
    ) "Generated completion profile identity/readiness marker changed; vertical_slice_complete must remain false until the final rendered checklist passes."

    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $pythonBootstrap -StateRoot $stateRoot
    if ($LASTEXITCODE -ne 0) { throw "Importer Python bootstrap failed." }
    $importerTestOutput = Invoke-ProofChecked $gate "importer_tests" $python @("-m", "unittest", "discover", "-s", (Join-Path $repoRoot "importer\tests"), "-v") '(?m)^OK(?: \(skipped=\d+\))?\s*$'
    $testCountMatch = [regex]::Match($importerTestOutput, '(?m)^Ran ([1-9][0-9]*) tests? in ')
    Assert-ProofTrue ($testCountMatch.Success) "Importer suite did not report an executed test count."
    $executedTestCount = [int]$testCountMatch.Groups[1].Value
    Assert-ProofTrue ($executedTestCount -ge $minimumImporterTestCount) "Importer suite executed fewer than the protected baseline of $minimumImporterTestCount tests."
    $skipCountMatch = [regex]::Match($importerTestOutput, '(?m)^OK \(skipped=([0-9]+)\)\s*$')
    $skippedTestCount = if ($skipCountMatch.Success) { [int]$skipCountMatch.Groups[1].Value } else { 0 }
    Assert-ProofTrue ($skippedTestCount -le $maximumImporterSkipCount) "Importer suite skipped more than the approved maximum of $maximumImporterSkipCount tests."

    $bootstrap = Invoke-ImporterJson "bootstrap" @("bootstrap-tools")
    Assert-ProofTrue ([bool]$bootstrap.ready) "Pinned external tools are not ready."
    $doctor = Invoke-ImporterJson "doctor" @("doctor", "--install", $Install, "--deep")
    Assert-ProofTrue ([bool]$doctor.ready) "Retail install doctor is not ready."
    $plan = Invoke-ImporterJson "plan" @("plan", "--install", $Install, "--profile", $profilePath)
    Assert-ProofTrue (
        [bool]$plan.ready -and
        @($plan.missing_required).Count -eq 0 -and
        [string]$plan.profile -eq $expectedProfileId -and
        [string]$plan.pack -eq $expectedPackId -and
        [int]$plan.resource_count -gt 0 -and
        [int]$plan.selected_file_count -gt 0 -and
        [int](($plan.resources | ForEach-Object { @($_.matches).Count } | Measure-Object -Sum).Sum) -gt 0 -and
        [string]$plan.profile_sha256 -eq $expectedProfileSha256 -and
        $plan.importer_recipe_sha256 -match '^[0-9a-f]{64}$'
    ) "Exact generated completion profile did not resolve its pinned ready closure."
    Assert-ProofTrue (((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()) -eq $expectedProfileSha256) "Generated completion profile changed during planning."

    $buildArguments = @("build", "--install", $Install, "--profile", $profilePath, "--force")
    if ($IntegrationOwnerPublish) {
        $buildArguments += @("--godot-content-root", $publishRoot)
        Write-Host "$gate integration-owner publish explicitly enabled target=$publishRoot"
    } else {
        $buildArguments += "--no-publish"
        Write-Host "$gate proof builds are non-publishing"
    }
    $first = Invoke-ImporterJson "build_a" $buildArguments
    $second = Invoke-ImporterJson "build_b" $buildArguments
    Assert-ProofTrue ([bool]$first.valid -and [bool]$second.valid -and [bool]$first.semantic_provenance -and [bool]$second.semantic_provenance) "A retail build failed its semantic provenance audit."
    Assert-ProofTrue ($first.bundle_sha256 -match '^[0-9a-f]{64}$' -and $first.bundle_sha256 -eq $second.bundle_sha256) "Repeat builds were not byte-reproducible."
    Assert-ProofTrue (
        [string]$first.profile -eq $expectedProfileId -and
        [string]$second.profile -eq $expectedProfileId -and
        [string]$first.profile_sha256 -eq $expectedProfileSha256 -and
        [string]$second.profile_sha256 -eq $expectedProfileSha256 -and
        [IO.Path]::GetFullPath([string]$first.pack) -eq $expectedBuildPackPath -and
        [IO.Path]::GetFullPath([string]$second.pack) -eq $expectedBuildPackPath
    ) "A proof build changed the selected completion profile."
    if ($IntegrationOwnerPublish) {
        $expectedPublishedPack = [IO.Path]::GetFullPath((Join-Path $publishRoot "$expectedPackId\$($second.bundle_sha256)"))
        $expectedSelection = [IO.Path]::GetFullPath((Join-Path $publishRoot "selection.json"))
        $expectedActivePack = "$expectedPackId/$($second.bundle_sha256)"
        Assert-ProofTrue (
            [IO.Path]::GetFullPath([string]$first.published_pack) -eq $expectedPublishedPack -and
            [IO.Path]::GetFullPath([string]$second.published_pack) -eq $expectedPublishedPack -and
            [IO.Path]::GetFullPath([string]$first.selection) -eq $expectedSelection -and
            [IO.Path]::GetFullPath([string]$second.selection) -eq $expectedSelection -and
            [string]$first.active_pack -eq $expectedActivePack -and
            [string]$second.active_pack -eq $expectedActivePack
        ) "Integration-owner publication changed the expected release or selection path."
    } else {
        Assert-ProofTrue (
            $first.PSObject.Properties.Name -notcontains 'published_pack' -and
            $second.PSObject.Properties.Name -notcontains 'published_pack'
        ) "A proof-only build unexpectedly published or selected a pack."
    }
    Write-Host "$gate reproducibility PASS bundle_sha256=$($first.bundle_sha256)"

    $builtPackDocument = (Get-Content -Raw -LiteralPath (Join-Path $expectedBuildPackPath "pack.json") | ConvertFrom-Json)
    $builtProvenanceDocument = (Get-Content -Raw -LiteralPath (Join-Path $expectedBuildPackPath "provenance\manifest.json") | ConvertFrom-Json)
    Assert-ProofTrue (
        $builtPackDocument.profile_build_complete -is [bool] -and
        [bool]$builtPackDocument.profile_build_complete -and
        @($builtProvenanceDocument.incomplete).Count -eq 0
    ) "Strict completion build retained incomplete conversion reasons."

    $audit = Invoke-ImporterJson "audit" @("audit", [string]$second.pack)
    Assert-ProofTrue (
        [bool]$audit.valid -and
        [bool]$audit.semantic_provenance -and
        $audit.provenance_contract -eq 'openbfme.retail-import-provenance-v1' -and
        $audit.profile -eq $expectedProfileId -and
        $audit.profile_sha256 -eq $expectedProfileSha256 -and
        $audit.importer_recipe_sha256 -eq $plan.importer_recipe_sha256 -and
        [int]$audit.source_archive_count -gt 0 -and
        [int]$audit.provenance_entry_count -gt 0 -and
        [int]$audit.tool_attestation_count -ge 5 -and
        [int]$audit.checked_files -ge 162 -and
        [int]$audit.checked_outputs -ge 158 -and
        [int]$audit.checked_files -ge [int]$audit.checked_outputs -and
        [int]$audit.source_archive_count -eq [int]$first.source_archive_count -and
        [int]$audit.source_archive_count -eq [int]$second.source_archive_count -and
        [int]$audit.provenance_entry_count -eq [int]$first.provenance_entry_count -and
        [int]$audit.provenance_entry_count -eq [int]$second.provenance_entry_count -and
        [int]$audit.checked_files -eq [int]$first.checked_files -and
        [int]$audit.checked_files -eq [int]$second.checked_files -and
        [int]$audit.checked_outputs -eq [int]$first.checked_outputs -and
        [int]$audit.checked_outputs -eq [int]$second.checked_outputs
    ) "Pack semantic provenance audit failed."
    Assert-ProofTrue (((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()) -eq $expectedProfileSha256) "Generated completion profile changed during proof builds."

    # Runtime acceptance must exercise the pack produced by this exact proof
    # run. Falling through to the durable user:// selection can silently test
    # an older bundle after Build A/B have succeeded. Publishing mode validates
    # the immutable published copy; proof-only mode validates the private build
    # root without mutating selection.json.
    if ($IntegrationOwnerPublish) {
        $env:OPENBFME_CONTENT = $expectedPublishedPack
    } else {
        $env:OPENBFME_CONTENT = $expectedBuildPackPath
    }
    Write-Host "$gate runtime pack explicitly selected root=$($env:OPENBFME_CONTENT)"
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    Invoke-GodotPassedFloor "stage11_12_groups_and_routes" "stage11_12_runner.gd" '(?m)^STAGE 11/12 TESTS: ([0-9]+) passed, 0 failed\s*$' 26
    Invoke-GodotPassedFloor "stage14_15_base_loop" "stage14_15_sim_runner.gd" '(?m)^STAGE 14/15 SIM TESTS: ([0-9]+) passed, 0 failed\s*$' 31
    Invoke-GodotPassedFloor "stage15_menu_and_audio" "stage15_menu_runner.gd" '(?m)^STAGE15_MENU_RESULT passed=([0-9]+) failed=0\s*$' 25
    Invoke-GodotPassedFloor "retail_pack_runtime" "retail_pack_runner.gd" '(?m)^RETAIL_PACK_RESULT passed=([0-9]+) failed=0\s*$' 175
    Invoke-GodotPassedFloor "playable_retail_slice" "retail_slice_runner.gd" '(?m)^RETAIL_SLICE_RESULT passed=([0-9]+) failed=0\s*$' 208
    Invoke-GodotPassedFloor "external_pack_security" "external_pack_runner.gd" '(?m)^EXTERNAL_PACK_RESULT passed=([0-9]+) failed=0\s*$' 64
    Invoke-GodotPassedFloor "legacy_regression" "cli_runner.gd" '(?m)^STAGE TESTS: ([0-9]+) passed, 0 failed\s*$' 101
    [void](Invoke-ProofChecked $gate "export_firewall_self_test" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "test-export-scan.ps1")) '(?m)^EXPORT_SCAN_SELF_TEST PASS ')
    [void](Invoke-ProofChecked $gate "export_firewall" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "export-scan.ps1"), "-Root", $gameRoot) '(?m)^EXPORT_SCAN PASS ')

    Write-Host "$gate PASS bundle_sha256=$($second.bundle_sha256)"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
