[CmdletBinding()]
param([string]$GodotPath = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$WarningPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "MINAS_GATE_202"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$assignmentPath = Join-Path $repoRoot "workspace\logs\P1-MINAS-GATE-004\assignment.json"
$expectedSourceSha = "f5e06cc70ac92b2e505c2f6430c50ded012a37ae4ad80200b280335ab4f14b07"
$virtualPath = "data/ini/object/civilian/ministirithbuildings.ini"
$yawProof = "exact-v9.7.7-minas-fixture/non-cardinal-placement-yaw-mutation"
$forbiddenDiagnostics = '(?i)(?:\bWARNING\b|\bSKIP(?:PED)?\b|\bfallback\b|SCRIPT ERROR|Parse Error|CHECK_FAIL|watchdog (?:abort|timeout)|ObjectDB instances leaked|RID allocations|resources still in use)'

try {
    $assignment = Read-ProofJson $assignmentPath
    Assert-ProofTrue ([string]$assignment.itemId -eq "P1-MINAS-GATE-004") "Assignment identity is not P1-MINAS-GATE-004."
    Assert-ProofTrue ([string]$assignment.assignee -eq "codex-minas-gate-v4") "Assignment assignee drifted."
    $mainRoot = [IO.Path]::GetFullPath([string]$assignment.mainPath)
    $effectiveRoot = Join-Path $mainRoot "workspace\retail-work\editions\rotwk\cache\layered-effective-assets"
    $sourcePath = Join-Path $effectiveRoot ($virtualPath.Replace('/', '\'))
    $manifestPath = Join-Path $effectiveRoot ".openbfme\manifest.json"
    Assert-ProofTrue (Test-Path -LiteralPath $sourcePath -PathType Leaf) "Exact v9.7.7 Minas source winner is missing."
    Assert-ProofTrue (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Layered effective-tree manifest is missing."
    $sourceSha = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-ProofTrue ($sourceSha -eq $expectedSourceSha) "Minas source winner hash mismatch: $sourceSha"
    $manifest = Read-ProofJson $manifestPath
    $winner = @($manifest.files | Where-Object { [string]$_.path -eq $virtualPath })
    Assert-ProofTrue ($winner.Count -eq 1) "Manifest must contain exactly one Minas source winner."
    Assert-ProofTrue ([string]$winner[0].archive -eq "layer-expansion/__patch202.big") "Minas winner is not precedence-zero __patch202.big."
    Assert-ProofTrue ([int]$winner[0].precedence -eq 0 -and [string]$winner[0].sha256 -eq $expectedSourceSha) "Minas winner provenance does not match the accepted source row."

    $python = Join-Path $mainRoot "workspace\retail-work\tools\python-3.12-env\Scripts\python.exe"
    Assert-ProofTrue (Test-Path -LiteralPath $python -PathType Leaf) "Pinned importer Python environment is missing."
    $previousSource = $env:OPENBFME_MINAS_GATE_SOURCE
    try {
        $env:OPENBFME_MINAS_GATE_SOURCE = $sourcePath
        [void](Invoke-ProofChecked $gate "convert" $python @(
            '-m', 'pytest', '-q',
            'importer/tests/test_castle_fixtures.py',
            '-k', 'v202_minas_gate_exact_source_projection or admission_compiles_gate_health_armor_geometry_and_modules or gate_fixture_carries_module_block_and_named_geometries'
        ) '(?m)^3 passed(?:, \d+ deselected)? in ' $forbiddenDiagnostics)
    }
    finally {
        $env:OPENBFME_MINAS_GATE_SOURCE = $previousSource
    }

    $runnerPath = Join-Path $repoRoot "game\tests\minas_gate_nav_v202_runner.gd"
    Assert-ProofTrue (Test-Path -LiteralPath $runnerPath -PathType Leaf) "Focused Minas runner is missing."
    $runnerText = Get-Content -LiteralPath $runnerPath -Raw -Encoding UTF8
    Assert-ProofTrue ($runnerText.Contains("const MUTATION_PROOF_LABEL := `"$yawProof`"")) "Non-cardinal mutation proof is not source-labelled."
    Assert-ProofTrue ($runnerText -match 'NON_CARDINAL_MUTATION_YAW_RADIANS\s*:=\s*PI\s*/\s*6\.0') "Focused yaw must remain non-cardinal and non-zero."
    Assert-ProofTrue ($runnerText -match 'RESET_MILLISECONDS\s*:=\s*6000') "Focused reset proof must bind the converted 6000 ms value."
    Assert-ProofTrue ($runnerText -cnotmatch '\bRESET_TICKS\b' -and $runnerText -notmatch '["'']reset_ticks["'']\s*:') "Focused runner must not inject an independent reset_ticks policy oracle."
    Assert-ProofTrue ($runnerText -match '_seed_castle_fixtures\s*\(' -and $runnerText -notmatch '_ship_contract_delay_ticks\s*\(') "Focused runner must obtain reset ticks through shipping castle-fixture seeding only."
    Assert-ProofTrue ($runnerText -match 'rotated_anchor_origin_is_analytic' -and $runnerText -match 'yaw_sign_flip_cannot_satisfy_origin') "Focused runner lost analytic rotation/sign proof."
    Assert-ProofTrue ($runnerText -match 'production_equals_exact_rotated_footprint' -and $runnerText -match 'sign_flipped_footprint_is_unequal' -and $runnerText -match 'axis_aligned_footprint_is_unequal') "Focused runner lost exact footprint equality or negative controls."
    Assert-ProofTrue ($runnerText -match 'gate_navigation_channel_cells\s*\(' -and $runnerText -match '_independent_channel_footprint\s*\(') "Focused runner must compare the production footprint to independent enumeration."
    Assert-ProofTrue ($runnerText -match 'sign_flipped_control_cannot_open_seam' -and $runnerText -match 'axis_aligned_control_cannot_open_seam') "Wrong-yaw controls must fail the route seam."
    Assert-ProofTrue ($runnerText -match '_apply_structure_damage\s*\(') "Focused runner must breach through production structure damage."
    Assert-ProofTrue ($runnerText -notmatch 'set_castle_gate_pathing\s*\([^\r\n]*,\s*true\s*\)') "Focused runner must not stand in a direct breached=true map call."

    $classCacheSource = Join-Path $mainRoot "game\.godot\global_script_class_cache.cfg"
    $classCacheTarget = Join-Path $repoRoot "game\.godot\global_script_class_cache.cfg"
    Assert-ProofTrue (Test-Path -LiteralPath $classCacheSource -PathType Leaf) "Pinned Godot script-class cache is missing."
    New-Item -ItemType Directory -Force (Split-Path -Parent $classCacheTarget) | Out-Null
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
        [IO.Path]::GetFullPath($classCacheSource),
        [IO.Path]::GetFullPath($classCacheTarget)
    )) {
        Copy-Item -LiteralPath $classCacheSource -Destination $classCacheTarget -Force
    }
    $initialIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    $godot = Resolve-ProofGodot $GodotPath $mainRoot
    $behaviorOutput = Invoke-ProofChecked $gate "behavior" $godot @(
        '--headless', '--path', (Join-Path $repoRoot 'game'),
        '--script', 'res://tests/minas_gate_nav_v202_runner.gd'
    ) '(?m)^MINAS_GATE_202 PASS checks=25\s*$' $forbiddenDiagnostics
    Assert-ProofTrue ($behaviorOutput -match '(?m)^MINAS_GATE_202 FOOTPRINT production=([1-9][0-9]*) rotated=([1-9][0-9]*) sign_flipped=([0-9]+) axis_aligned=([0-9]+)\s*$') "Focused runner did not report exact footprint cardinalities."
    $productionCellCount = [int]$Matches[1]
    $rotatedCellCount = [int]$Matches[2]
    $signFlippedCellCount = [int]$Matches[3]
    $axisAlignedCellCount = [int]$Matches[4]
    Assert-ProofTrue ($productionCellCount -eq $rotatedCellCount) "Production and independent rotated footprint counts differ."

    $receiptPath = Join-Path $repoRoot "workspace\logs\P1-MINAS-GATE-004\minas-gate-check.json"
    $receipt = [ordered]@{
        schema = "openbfme.minas-gate-v202-check"
        schemaVersion = 3
        gitRevision = [string]$initialIdentity.revision
        dirtyStateDigest = [string]$initialIdentity.dirtyStateDigest
        sourceEvidence = @("E-BL-202", "E-EFFECTIVE-TREE-20260830")
        sourceVirtualPath = $virtualPath
        sourceArchive = [string]$winner[0].archive
        sourcePrecedence = [int]$winner[0].precedence
        sourceSha256 = $sourceSha
        resetMilliseconds = 6000
        tickMilliseconds = 100
        resetTicks = 60
        placementYawProof = $yawProof
        placementYawRadians = [Math]::PI / 6.0
        productionChannelCellCount = $productionCellCount
        independentRotatedCellCount = $rotatedCellCount
        signFlippedControlCellCount = $signFlippedCellCount
        axisAlignedControlCellCount = $axisAlignedCellCount
        exactChannelFootprint = "PASS"
        wrongYawRouteControls = "PASS"
        resetProjectionPath = "castle-fixture-seed-to-ship-contract-delay-ticks"
        conversion = "PASS"
        behavior = "PASS"
        synchronousNavigation = "PASS"
        liveCombatDestruction = "PASS"
        stickyBreach = "PASS"
        load = "NOT_REQUIRED"
        visual = "NOT_REQUIRED"
        audio = "NOT_REQUIRED"
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $receiptPath) | Out-Null
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    $boundReceipt = Read-ProofJson $receiptPath
    Assert-ProofTrue ([int]$boundReceipt.resetMilliseconds -eq 6000 -and [int]$boundReceipt.resetTicks -eq 60) "Written receipt lost exact reset timing."
    Assert-ProofTrue ([string]$boundReceipt.placementYawProof -eq $yawProof -and [double]$boundReceipt.placementYawRadians -ne 0.0) "Written receipt lost non-cardinal yaw proof."
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$initialIdentity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$initialIdentity.dirtyStateDigest
    ) "Working-tree identity changed during the Minas gate check."

    Write-Host "$gate PASS source_sha256=$sourceSha reset_ms=6000 reset_ticks=60 yaw_proof=$yawProof"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
