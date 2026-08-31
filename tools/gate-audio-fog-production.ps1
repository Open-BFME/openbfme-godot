[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "AUDIO_FOG_PRODUCTION"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$runner = Join-Path $gameRoot "tests\retail_sfx_shroud_production_runner.gd"
$report = Join-Path $repoRoot "workspace\logs\P1-AUDIO-FOG-002\audio-fog-production.json"
$forbiddenDiagnostics = '(?i)(?:SCRIPT ERROR|Parse Error|watchdog (?:abort|timeout)|ObjectDB instances leaked|RID allocations|Parameter [^\r\n]+ is null)'

try {
    Assert-ProofTrue (Test-Path -LiteralPath $runner -PathType Leaf) "Production fog-audio runner is missing."
    $commonGitDir = ([string](& git -C $repoRoot rev-parse --path-format=absolute --git-common-dir)).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $commonGitDir -PathType Container)) "Shared Git directory could not be resolved."
    $controlRoot = [IO.Path]::GetFullPath((Split-Path -Parent $commonGitDir))
    $toolRoot = $repoRoot
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot ".tools\godot") -PathType Container)) {
        $toolRoot = $controlRoot
    }
    $godot = Resolve-ProofGodot $GodotPath $toolRoot
    $contentRoot = Join-Path $controlRoot "workspace\content-packs"
    Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $contentRoot "selection.json") -PathType Leaf) "Canonical private selection is unavailable."
    # Sibling worktrees intentionally do not duplicate ignored Godot state.
    # The generated class registry contains project-relative script names only;
    # copy the canonical registry so this isolated lane compiles the same named
    # global classes without importing or writing any tracked source file.
    $classCache = Join-Path $gameRoot ".godot\global_script_class_cache.cfg"
    if (-not (Test-Path -LiteralPath $classCache -PathType Leaf)) {
        $sharedClassCache = Join-Path $controlRoot "game\.godot\global_script_class_cache.cfg"
        Assert-ProofTrue (Test-Path -LiteralPath $sharedClassCache -PathType Leaf) "Pinned Godot class registry is unavailable."
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $classCache) | Out-Null
        Copy-Item -LiteralPath $sharedClassCache -Destination $classCache
    }
    $initialIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $report) | Out-Null
    $previousReport = $env:OPENBFME_AUDIO_FOG_REPORT
    $previousContent = $env:OPENBFME_CONTENT
    try {
        $env:OPENBFME_AUDIO_FOG_REPORT = $report
        $env:OPENBFME_CONTENT = $contentRoot
        $output = Invoke-ProofChecked $gate "production_runtime" $godot @(
            "--headless", "--path", $gameRoot,
            "--script", "res://tests/retail_sfx_shroud_production_runner.gd"
        ) '(?m)^AUDIO_FOG_PRODUCTION_RUNNER_RESULT passed=([1-9][0-9]*) failed=0 hidden_players=0\s*$' $forbiddenDiagnostics
    }
    finally {
        $env:OPENBFME_AUDIO_FOG_REPORT = $previousReport
        $env:OPENBFME_CONTENT = $previousContent
    }
    Assert-ProofTrue (Test-Path -LiteralPath $report -PathType Leaf) "Runner did not write the declared report."
    $receipt = Read-ProofJson $report
    Assert-ProofTrue ([string]$receipt.schema -eq "openbfme.audio-fog-production") "Report schema changed."
    Assert-ProofTrue ([int]$receipt.schemaVersion -eq 1) "Report schema version changed."
    Assert-ProofTrue ([string]$receipt.sourceEvidence -eq "E-BL-202") "Report lost the exact source-evidence identity."
    Assert-ProofTrue ([int]$receipt.failed -eq 0 -and [int]$receipt.hiddenPlayers -eq 0) "Hidden combat reached an audio player."
    Assert-ProofTrue ([string]$receipt.provenance.rotwkRuntimePackId -eq "rotwk-men-vslice") "RotWK runtime pack identity is missing."
    Assert-ProofTrue (-not [bool]$receipt.provenance.rotwkAudioEventsDeclared) "Gate expected the currently named RotWK audio-definition gap."
    Assert-ProofTrue ([string]$receipt.provenance.definitionPackId -eq "bfme2-men-vslice") "Supplemental definition ownership changed or was not named."
    Assert-ProofTrue ([string]$receipt.provenance.parityClaim -match '^BFME2 supplemental attribution only') "BFME2 supplemental data was presented as RotWK parity."
    Assert-ProofTrue ([int]$receipt.runtime.unpositionedWorldBlocks.TrebuchetLaunchVoice -ge 1) "Missing-position world route was not blocked and enumerated."
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$initialIdentity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$initialIdentity.dirtyStateDigest
    ) "Working-tree identity changed during the production fog-audio gate."
    Write-Host "$gate PASS hidden_players=0"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
