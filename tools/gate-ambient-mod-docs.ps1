[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$WarningPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "AMBIENT_MOD_DOCS"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$readmeRelative = "game/mods/README.md"
$guideRelative = "docs/MODDING.md"
$exampleRelative = "examples/mods/example_hard_orcs"
$readme = Join-Path $repoRoot $readmeRelative
$guide = Join-Path $repoRoot $guideRelative
$example = Join-Path $repoRoot $exampleRelative
$report = Join-Path $repoRoot "workspace/logs/P0-REPO-AMBIENT-MOD-DOCS-001/ambient-mod-docs.json"
$forbiddenProcessDiagnostics = '(?i)(?:\bWARNING\b|\bSKIP(?:PED)?\b|\bfallback\b|Parse Error)'
$approvedDocuments = [ordered]@{
    "README.md" = "cf9d865c2e28b3d03c7cfd5f08108f1079af84c7"
    "docs/CONTENT_PIPELINE.md" = "d16d44c230f505a0adf372b217588f52d38f29d6"
    "docs/MODDING.md" = "7e4ead6fd473ccba867e3c7cd47d96b8286df728"
    "docs/ONBOARDING.md" = "64d7f7ad83ba1ace90d47d4a908962239ba3c310"
    "docs/VERIFICATION.md" = "65419442297a5e1e3e4bfda16d7d3cdc040adfa5"
    "docs/patch-notes/v0.2.13.md" = "e619cca0b59aea095723c619a0d3f27c7caccf5f"
    "game/mods/README.md" = "ed8d258ac53682b573aaf6eb7dcec703a08d69d9"
}

try {
    $initialIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (Test-Path -LiteralPath $readme -PathType Leaf) "Ambient-mod tombstone is missing."
    Assert-ProofTrue (Test-Path -LiteralPath $guide -PathType Leaf) "Canonical modding guide is missing."
    Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $example "pack.json") -PathType Leaf) "Non-shipping example pack is missing."

    $markdownDiff = @(& git -C $repoRoot diff --quiet -- '*.md' 2>&1)
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and $markdownDiff.Count -eq 0) "Working-tree Markdown differs from the staged identity."
    $gitRows = @(& git -C $repoRoot ls-files --stage -- '*.md' 2>&1)
    Assert-ProofTrue ($LASTEXITCODE -eq 0) "Tracked Markdown census failed."
    $gitDiagnostics = @($gitRows | Where-Object { $_ -is [Management.Automation.ErrorRecord] } | ForEach-Object { $_.ToString() })
    Assert-ProofTrue ($gitDiagnostics.Count -eq 0 -and -not (($gitDiagnostics -join "`n") -match $forbiddenProcessDiagnostics)) "Tracked Markdown census emitted stderr or a forbidden warning, skip, fallback, or parse diagnostic."
    $trackedDocs = [Collections.Generic.List[string]]::new()
    $documents = [ordered]@{}
    foreach ($row in @($gitRows | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] } | ForEach-Object { $_.ToString() } | Sort-Object)) {
        Assert-ProofTrue ($row -match '^100644 ([0-9a-f]{40}) 0\t(.+)$') "Tracked Markdown index row is malformed: $row"
        $blob = [string]$Matches[1]
        $relative = [string]$Matches[2]
        if ($relative.StartsWith('orchestration/', [StringComparison]::OrdinalIgnoreCase)) { continue }
        $text = Get-Content -LiteralPath (Join-Path $repoRoot $relative) -Raw -Encoding UTF8
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace($text)) "Tracked modding guide $relative is empty."
        if ($text -notmatch '(?i)(?:game[\\/]mods|user://mods|OPENBFME_CONTENT|content_root)') { continue }
        $trackedDocs.Add($relative)
        Assert-ProofTrue ($approvedDocuments.Contains($relative)) "Unreviewed tracked mod-loading guide entered the current set: $relative"
        Assert-ProofTrue ($blob -ceq [string]$approvedDocuments[$relative]) "Reviewed mod-loading guide changed: $relative"
        $documents[$relative] = $blob
    }
    foreach ($approved in $approvedDocuments.Keys) {
        Assert-ProofTrue ($trackedDocs.Contains([string]$approved)) "Reviewed mod-loading guide left the dynamic census: $approved"
    }

    $readmeText = Get-Content -LiteralPath $readme -Raw -Encoding UTF8
    Assert-ProofTrue ($readmeText.Contains('`game/mods/`') -and $readmeText.Contains('`user://mods`')) "Tombstone does not name both retired ambient locations."
    Assert-ProofTrue ($readmeText -match '(?is)diagnostic-only.{0,160}\bnot\s+mounted\b') "Tombstone does not state the diagnostic-only, not-mounted policy."
    Assert-ProofTrue ($readmeText.Contains('`OPENBFME_CONTENT`') -and $readmeText.Contains('`selection.json`')) "Tombstone does not require an explicit content source."
    Assert-ProofTrue ($readmeText.Contains('(../../docs/MODDING.md)')) "Tombstone has no correct relative link to the modding guide."
    Assert-ProofTrue ($readmeText.Contains('(../../examples/mods/example_hard_orcs/)')) "Tombstone has no correct relative link to the example mod."

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $report) | Out-Null
    [ordered]@{
        schema = "openbfme.ambient-mod-docs"
        schemaVersion = 2
        gitRevision = [string]$initialIdentity.revision
        dirtyStateDigest = [string]$initialIdentity.dirtyStateDigest
        retiredAmbientDocs = $true
        trackedModdingDocs = @($trackedDocs)
        documentGitBlobs = $documents
        moddingGuide = $guideRelative
        example = $exampleRelative
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding UTF8
    $receipt = Read-ProofJson $report
    Assert-ProofTrue ([string]$receipt.schema -eq "openbfme.ambient-mod-docs" -and [int]$receipt.schemaVersion -eq 2) "Ambient-mod docs receipt is invalid."

    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$initialIdentity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$initialIdentity.dirtyStateDigest
    ) "Working-tree identity changed during the ambient-mod docs gate."

    Write-Host "$gate PASS retired_ambient_docs=true"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
