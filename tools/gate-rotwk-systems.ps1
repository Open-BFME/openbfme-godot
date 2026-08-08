# Focused systems-first gate for RotWK baseline.
# Always: product contracts, CLI default game, script presence, resolve helper.
# When ROTWK_INSTALL is set (or a probe finds game.dat): doctor + optional map corpus smoke.
# Never rewrites selection.json.

[CmdletBinding()]
param(
    [string]$RotwkInstall = $env:ROTWK_INSTALL,
    [int]$MapSmokeLimit = 3,
    [switch]$SkipLiveRetail
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "ROTWK_SYSTEMS_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$stateRoot = if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
    [IO.Path]::GetFullPath($env:OPENBFME_IMPORT_ROOT)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work"))
}
$env:OPENBFME_IMPORT_ROOT = $stateRoot
$env:PYTHONPATH = Join-Path $repoRoot "importer"
$python = Join-Path $stateRoot "tools\python-3.12-env\Scripts\python.exe"
$cli = Join-Path $repoRoot "tools\openbfme_import.py"

function Assert-File([string]$Path) {
    Assert-ProofTrue (Test-Path -LiteralPath $Path -PathType Leaf) "Missing required file: $Path"
}

try {
    Assert-File (Join-Path $repoRoot "contracts\rotwk-201-product-scope.json")
    Assert-File (Join-Path $repoRoot "DIRECTION.md")
    Assert-File (Join-Path $repoRoot "docs\MILESTONE_CURRENT.md")
    Assert-File (Join-Path $repoRoot "tools\rotwk-systems.ps1")
    Assert-File (Join-Path $repoRoot "tools\rotwk_map_cook_corpus.py")
    Assert-File (Join-Path $repoRoot "tools\rotwk_binding_factory.py")
    Assert-File (Join-Path $repoRoot "tools\rotwk_faction_convert_batch.py")
    Assert-File (Join-Path $repoRoot "tools\rotwk_faction_pack_proof.py")
    Assert-File (Join-Path $repoRoot "tools\rotwk_multimap_skirmish.py")
    Assert-File (Join-Path $repoRoot "docs\OPENSAGE_GAP_MATRIX.md")
    Assert-File (Join-Path $repoRoot "importer\openbfme_importer\conversion_ledger.py")
    Assert-File (Join-Path $repoRoot "tools\resolve-rotwk-install.bat")
    Assert-File (Join-Path $repoRoot "run_rotwk_systems.bat")
    Assert-File (Join-Path $repoRoot "run_rotwk_one_button.bat")
    Assert-File $cli

    # CLI default game must be rotwk
    $cliSource = Get-Content -LiteralPath (Join-Path $repoRoot "importer\openbfme_importer\cli.py") -Raw
    Assert-ProofTrue ($cliSource -match 'DEFAULT_GAME\s*=\s*"rotwk"') "importer CLI DEFAULT_GAME is not rotwk"

    $direction = Get-Content -LiteralPath (Join-Path $repoRoot "DIRECTION.md") -Raw
    Assert-ProofTrue ($direction -match 'systems-first') "DIRECTION.md missing systems-first model"
    Assert-ProofTrue ($direction -match 'RotWK|Rise of the Witch-king') "DIRECTION.md missing RotWK target"

    if (-not (Test-Path -LiteralPath $python)) {
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "bootstrap-importer-python.ps1") -StateRoot $stateRoot
        if ($LASTEXITCODE -ne 0) { throw "python bootstrap failed" }
    }
    Assert-File $python

    $null = Invoke-ProofChecked $gate "product_contracts" $python @(
        (Join-Path $repoRoot "tools\check-product-contracts.py"), "--check"
    ) 'PRODUCT_CONTRACTS PASS'

    # Unit tests for map cook helper + conversion ledger (offline)
    $null = Invoke-ProofChecked $gate "map_cook_helper_tests" $python @(
        "-m", "pytest",
        (Join-Path $repoRoot "importer\tests\test_rotwk_map_cook_corpus.py"),
        (Join-Path $repoRoot "importer\tests\test_conversion_ledger.py"),
        "-q", "--tb=line"
    ) '(?m)\d+ passed'

    if ($SkipLiveRetail) {
        Write-Host "$gate SKIPPED reason=-SkipLiveRetail live_retail_stages=not_run"
        exit 0
    }

    if ([string]::IsNullOrWhiteSpace($RotwkInstall)) {
        foreach ($candidate in @(
                "D:\RotWK",
                "C:\RotWK",
                "D:\Games\RotWK",
                "${env:ProgramFiles(x86)}\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king",
                "$env:ProgramFiles\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king"
            )) {
            if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath (Join-Path $candidate "game.dat"))) {
                $RotwkInstall = $candidate
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($RotwkInstall) -or -not (Test-Path -LiteralPath (Join-Path $RotwkInstall "game.dat"))) {
        Write-Host "$gate SKIPPED reason=no_ROTWK_INSTALL live_retail_stages=not_run"
        exit 0
    }

    $RotwkInstall = [IO.Path]::GetFullPath($RotwkInstall)
    $null = Invoke-ProofChecked $gate "doctor_rotwk" $python @(
        $cli, "doctor", "--game", "rotwk", "--install", $RotwkInstall
    ) 'READY'

    if ($MapSmokeLimit -gt 0) {
        $cookOut = Join-Path $stateRoot "reports\rotwk-map-cook-corpus-gate-smoke.json"
        $null = Invoke-ProofChecked $gate "map_cook_smoke" $python @(
            (Join-Path $repoRoot "tools\rotwk_map_cook_corpus.py"),
            "--install", $RotwkInstall,
            "--game", "rotwk",
            "--state-root", $stateRoot,
            "--limit", "$MapSmokeLimit",
            "--output", $cookOut
        ) 'REPORT'
        Assert-ProofTrue (Test-Path -LiteralPath $cookOut) "map cook smoke report missing"
        $doc = Get-Content -LiteralPath $cookOut -Raw | ConvertFrom-Json
        Assert-ProofTrue ([int]$doc.mapCount -eq $MapSmokeLimit) "map cook smoke mapCount=$($doc.mapCount) expected $MapSmokeLimit"
        Assert-ProofTrue ($doc.schema -eq "openbfme.rotwk-map-cook-corpus") "unexpected map cook schema"
        $allowed = @(
            "cooked",
            "cooked-and-connected",
            "cooked-but-starts-disconnected",
            "under-two-player-starts",
            "registry-stale-missing-payload"
        )
        foreach ($row in @($doc.maps)) {
            Assert-ProofTrue ($allowed -contains [string]$row.verdict) "disallowed map cook verdict '$($row.verdict)' for $($row.path)"
        }
        Assert-ProofTrue ([int]$doc.fatalVerdictCount -eq 0) "fatal map cook verdicts present"
        Assert-ProofTrue ($null -ne $doc.conversionLedger) "map cook missing conversionLedger"
        Assert-ProofTrue ([int]$doc.conversionLedger.totalUnits -ge $MapSmokeLimit) "map cook ledger under-counted units"

        $bindOut = Join-Path $stateRoot "reports\rotwk-binding-factory-gate-smoke.json"
        $null = Invoke-ProofChecked $gate "binding_factory_smoke" $python @(
            (Join-Path $repoRoot "tools\rotwk_binding_factory.py"),
            "--install", $RotwkInstall,
            "--game", "rotwk",
            "--state-root", $stateRoot,
            "--limit", "$MapSmokeLimit",
            "--output", $bindOut
        ) 'REPORT'
        $bind = Get-Content -LiteralPath $bindOut -Raw | ConvertFrom-Json
        Assert-ProofTrue ([int]$bind.mapCount -eq $MapSmokeLimit) "binding factory mapCount mismatch"
        Assert-ProofTrue ($bind.schema -eq "openbfme.rotwk-binding-factory") "binding factory schema"
        Assert-ProofTrue ($null -ne $bind.percentBoundCorpus) "binding factory missing percentBoundCorpus"
        # Arithmetic: ledger object-type events should equal typeTotal
        $typeEvents = 0
        if ($bind.ledger.kindCounts.'object-type') {
            $typeEvents = [int]$bind.ledger.kindCounts.'object-type'
        }
        Assert-ProofTrue ($typeEvents -eq [int]$bind.typeTotal) "binding ledger object-type count $typeEvents != typeTotal $($bind.typeTotal)"

        # Faction discovery smoke (full plan/convert is multi-minute; gate stays focused).
        $null = Invoke-ProofChecked $gate "faction_discovery_smoke" $python @(
            "-c",
            @"
import os, sys
from pathlib import Path
sys.path.insert(0, r'$repoRoot\importer')
os.environ['OPENBFME_IMPORT_ROOT'] = r'$stateRoot'
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import discover_playable_factions
cat = InstallCatalog.load(Path(r'$stateRoot') / 'catalog' / 'rotwk.json')
factions = discover_playable_factions(cat)
names = sorted(f.short_name for f in factions)
assert 'men' in names and 'angmar' in names and len(names) >= 7, names
print('FACTION_DISCOVERY_OK', ','.join(names))
"@
        ) 'FACTION_DISCOVERY_OK'
        # Batch tool is present and importable (convert path exercised by operators).
        Assert-ProofTrue (Test-Path -LiteralPath (Join-Path $repoRoot "tools\rotwk_faction_convert_batch.py")) "faction convert batch tool missing"

        # Multimap registry-catalog smoke: unique slugs/ids for official MP corpus.
        $mmOut = Join-Path $stateRoot "reports\rotwk-multimap-skirmish-gate-smoke.json"
        $null = Invoke-ProofChecked $gate "multimap_registry_catalog_smoke" $python @(
            (Join-Path $repoRoot "tools\rotwk_multimap_skirmish.py"),
            "--install", $RotwkInstall,
            "--game", "rotwk",
            "--state-root", $stateRoot,
            "--output", $mmOut
        ) 'proof_ok=True'
        Assert-ProofTrue (Test-Path -LiteralPath $mmOut) "multimap smoke report missing"
        $mm = Get-Content -LiteralPath $mmOut -Raw | ConvertFrom-Json
        Assert-ProofTrue ([int]$mm.mapCount -eq 72) "multimap mapCount=$($mm.mapCount) expected full official corpus 72"
        Assert-ProofTrue ([int]$mm.rejectedMapCount -eq 0) "multimap rejectedMapCount must be 0"
        Assert-ProofTrue ($mm.catalogProof.ok -eq $true) "multimap catalogProof not ok"
        Assert-ProofTrue ($mm.catalogProof.zeroRejections -eq $true) "multimap catalogProof.zeroRejections missing"
        Assert-ProofTrue ($mm.mode -eq "registry-catalog") "multimap mode not registry-catalog"
    }

    # Optional: note selection when present (not a mount/boot claim).
    $selection = Join-Path $repoRoot ".private\content-packs\selection.json"
    if (Test-Path -LiteralPath $selection) {
        $sel = Get-Content -LiteralPath $selection -Raw | ConvertFrom-Json
        if (-not [string]::IsNullOrWhiteSpace([string]$sel.activePack)) {
            Write-Host "$gate selection present activePack=$($sel.activePack) (not a mount proof)"
        }
    }

    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-Host "$gate FAIL $_"
    exit 1
}
