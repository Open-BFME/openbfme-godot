[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE10_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$stage9Gate = Join-Path $PSScriptRoot "gate-stage9.ps1"
$doctor = Join-Path $PSScriptRoot "doctor.ps1"
$exportScan = Join-Path $PSScriptRoot "export-scan.ps1"
$exportScanSelfTest = Join-Path $PSScriptRoot "test-export-scan.ps1"
$legacyRunner = Join-Path $gameRoot "tests\cli_runner.gd"
$matchBootRunner = Join-Path $gameRoot "tests\boot_match.gd"
$stageBootRunner = Join-Path $gameRoot "tests\stage10_boot_runner.gd"
$soakRunner = Join-Path $gameRoot "tests\stage10_soak_runner.gd"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

try {
    [void](Invoke-ProofPriorGate $gate "stage9_regression" $stage9Gate $GodotPath '(?m)^STAGE9_GATE PASS\s*$')

    foreach ($path in @($doctor, $exportScan, $exportScanSelfTest, $legacyRunner, $matchBootRunner, $stageBootRunner, $soakRunner)) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing Stage 10 dependency: $path"
    }

    $doctorArguments = @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $doctor)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $doctorArguments += @("-GodotPath", $GodotPath) }
    [void](Invoke-ProofChecked $gate "doctor" "powershell.exe" $doctorArguments '(?m)^OPENBFME_DOCTOR PASS\s*$')
    [void](Invoke-ProofChecked $gate "export_scan" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $exportScan, "-Root", $gameRoot) '(?m)^EXPORT_SCAN PASS ')
    [void](Invoke-ProofChecked $gate "export_scan_self_test" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $exportScanSelfTest) '(?m)^EXPORT_SCAN_SELF_TEST PASS ')

    foreach ($documentation in @("docs\BUILD_AND_RELEASE.md", "docs\KNOWN_ISSUES.md", "docs\legal_assets.md", "docs\THIRD_PARTY.md")) {
        Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $repoRoot $documentation) -PathType Leaf) "Missing release documentation: $documentation"
    }
    $buildGuide = Get-Content -LiteralPath (Join-Path $repoRoot "docs\BUILD_AND_RELEASE.md") -Raw -Encoding UTF8
    $knownIssues = Get-Content -LiteralPath (Join-Path $repoRoot "docs\KNOWN_ISSUES.md") -Raw -Encoding UTF8
    Assert-ProofTrue ($buildGuide -match 'run_stage10_tests\.bat' -and $buildGuide -match 'build_base_content\.py') "Build guide must document the gold gate and content rebuild."
    Assert-ProofTrue ($knownIssues -match '30-minute' -and $knownIssues -match 'not\s+a\s+declaration\s+of\s+complete\s+BFME2') "Known issues must state the soak and compatibility boundaries."
    foreach ($generator in @("tools\build_base_content.py", "tools\port_mert_config.py")) {
        $generatorText = Get-Content -LiteralPath (Join-Path $repoRoot $generator) -Raw -Encoding UTF8
        Assert-ProofTrue ($generatorText -notmatch 'C:\\Users\\Jonathan\\Desktop\\open-bfme') "$generator still contains a checkout-specific path."
    }
    Write-Host "$gate release_docs PASS"

    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    [void](Invoke-ProofChecked $gate "legacy_stage_suite" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/cli_runner.gd") '(?m)^STAGE TESTS: \d+ passed, 0 failed\s*$' $forbiddenDiagnostics)
    [void](Invoke-ProofChecked $gate "legacy_match_boot" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/boot_match.gd") '(?m)^BOOT_OK zero script load failures for match path\s*$' $forbiddenDiagnostics)
    $bootOutput = Invoke-ProofChecked $gate "stage_menu_boot" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage10_boot_runner.gd") '(?m)^STAGE10_BOOT_PROOF PASS assertions=\d+\s*$' $forbiddenDiagnostics
    $bootMatch = [regex]::Match($bootOutput, '(?m)^STAGE10_BOOT_PROOF PASS assertions=(\d+)\s*$')
    Assert-ProofTrue ($bootMatch.Success -and [int]$bootMatch.Groups[1].Value -ge 40) "Stage menu boot coverage regressed below 40 assertions."

    $soakOutput = Invoke-ProofChecked $gate "thirty_minute_soak" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage10_soak_runner.gd") '(?m)^STAGE10_SOAK_PROOF PASS assertions=\d+\s*$' $forbiddenDiagnostics
    $soakMatch = [regex]::Match($soakOutput, '(?m)^STAGE10_SOAK_METRICS hash=([0-9A-F]{8}) ticks=(\d+) simulated_seconds=(\d+) matches=(\d+) actions=(\d+) peak_entities=(\d+) peak_projectiles=(\d+) elapsed_ms=(\d+)\s*$')
    Assert-ProofTrue $soakMatch.Success "Stage 10 soak metrics were not emitted."
    Assert-ProofTrue ([int]$soakMatch.Groups[2].Value -eq 18000 -and [int]$soakMatch.Groups[3].Value -eq 1800) "Stage 10 soak must cover 30 simulated minutes."
    Assert-ProofTrue ([int]$soakMatch.Groups[5].Value -ge 20 -and [int]$soakMatch.Groups[6].Value -le 512 -and [int]$soakMatch.Groups[7].Value -le 256) "Stage 10 soak activity or bounds regressed."
    Write-Host "$gate deterministic_soak PASS hash=$($soakMatch.Groups[1].Value)"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
