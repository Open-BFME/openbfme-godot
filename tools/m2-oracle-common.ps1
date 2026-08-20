Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:M2OracleCaptureIds = @(
    "map-overview", "ford-north", "ford-center", "ford-south",
    "player-base", "enemy-base",
    "unit-soldier-idle", "unit-soldier-move", "unit-soldier-attack", "unit-soldier-death",
    "unit-archer-idle", "unit-archer-move", "unit-archer-attack", "unit-archer-death",
    "unit-tower-guard-idle", "unit-tower-guard-move", "unit-tower-guard-attack", "unit-tower-guard-death",
    "unit-knight-idle", "unit-knight-move", "unit-knight-attack", "unit-knight-death",
    "structure-fortress-construction", "structure-fortress-intact", "structure-fortress-damaged", "structure-fortress-rubble",
    "structure-farm-construction", "structure-farm-intact", "structure-farm-damaged", "structure-farm-rubble",
    "structure-barracks-construction", "structure-barracks-intact", "structure-barracks-damaged", "structure-barracks-rubble",
    "structure-archery-range-construction", "structure-archery-range-intact", "structure-archery-range-damaged", "structure-archery-range-rubble",
    "structure-stable-construction", "structure-stable-intact", "structure-stable-damaged", "structure-stable-rubble",
    "hud-default", "hud-unit-selected", "hud-production", "hud-victory", "hud-defeat"
)

function Assert-M2OracleTrue {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-M2OracleContext {
    param([string]$RepoRoot)
    $root = [IO.Path]::GetFullPath($RepoRoot)
    $contentRootSource = "repo-workspace"
    if (-not [string]::IsNullOrWhiteSpace([string]$env:OPENBFME_CONTENT)) {
        $contentRoot = [IO.Path]::GetFullPath([string]$env:OPENBFME_CONTENT)
        $contentRootSource = "OPENBFME_CONTENT"
    }
    else {
        $contentRoot = [IO.Path]::GetFullPath((Join-Path $root "workspace\content-packs"))
    }
    Assert-M2OracleTrue ((Split-Path -Leaf $contentRoot) -eq "content-packs") "M2 oracle content root must be a content-packs directory: $contentRoot"
    $workspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $contentRoot))
    $profilePath = Join-Path $workspaceRoot "retail-work\profiles\men-fords-v0-complete.generated.json"
    $selectionPath = Join-Path $contentRoot "selection.json"
    Assert-M2OracleTrue (Test-Path -LiteralPath $profilePath -PathType Leaf) "Missing strict completion profile: $profilePath"
    Assert-M2OracleTrue (Test-Path -LiteralPath $selectionPath -PathType Leaf) "Missing private pack selection: $selectionPath"
    $profileSha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    # The RotWK Men pack is an M3 extension of this strict Men/Fords profile,
    # not a replacement for it. Keep the original pin and require pack.json's
    # m3Recipe.baseProfile to bind the selected RotWK pack back to these bytes.
    Assert-M2OracleTrue ($profileSha256 -eq "0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d") "The strict completion profile hash changed."
    $selection = Get-Content -Raw -LiteralPath $selectionPath -Encoding UTF8 | ConvertFrom-Json
    Assert-M2OracleTrue ([string]$selection.schema -eq "openbfme.pack-selection" -and [int]$selection.schemaVersion -eq 0) "Pack selection schema is invalid."
    $activePack = [string]$selection.activePack
    Assert-M2OracleTrue ($activePack -match '^rotwk-men-vslice/[0-9a-f]{64}$') "Selection does not name an immutable RotWK Men bundle."
    $bundleSha256 = $activePack.Split('/')[-1]
    $packRoot = [IO.Path]::GetFullPath((Join-Path $contentRoot $activePack))
    $packPath = Join-Path $packRoot "pack.json"
    $provenancePath = Join-Path $packRoot "provenance\manifest.json"
    $auditPath = Join-Path $packRoot "provenance\audit.json"
    Assert-M2OracleTrue (Test-Path -LiteralPath $packPath -PathType Leaf) "Selected immutable bundle is missing."
    Assert-M2OracleTrue (Test-Path -LiteralPath $provenancePath -PathType Leaf) "Selected immutable bundle has no provenance manifest."
    Assert-M2OracleTrue (Test-Path -LiteralPath $auditPath -PathType Leaf) "Selected immutable bundle has no provenance audit."
    $pack = Get-Content -Raw -LiteralPath $packPath -Encoding UTF8 | ConvertFrom-Json
    $provenance = Get-Content -Raw -LiteralPath $provenancePath -Encoding UTF8 | ConvertFrom-Json
    $audit = Get-Content -Raw -LiteralPath $auditPath -Encoding UTF8 | ConvertFrom-Json
    Assert-M2OracleTrue (
        [string]$pack.schema -eq "openbfme.content-pack" -and
        [string]$pack.id -eq "rotwk-men-vslice" -and
        [bool]$pack.profile_build_complete -and
        -not [bool]$pack.vertical_slice_complete
    ) "Selected immutable bundle is not the strict pending RotWK Men completion pack."
    Assert-M2OracleTrue (
        [string]$pack.m3Recipe.schema -eq "openbfme.m3-men-pack-recipe" -and
        [string]$pack.m3Recipe.baseProfile.id -eq "men-fords-v0-complete-generated" -and
        [string]$pack.m3Recipe.baseProfile.sha256 -eq $profileSha256
    ) "Selected RotWK Men bundle is not based on the pinned strict Men/Fords profile."
    Assert-M2OracleTrue (
        [string]$provenance.contract -eq "openbfme.retail-import-provenance-v1" -and
        [string]$pack.provenance_contract -eq [string]$provenance.contract -and
        [string]$provenance.profile -match '^faction-slice-[0-9a-f]{16}$' -and
        [string]$provenance.profile_sha256 -match '^[0-9a-f]{64}$' -and
        [string]$provenance.source_game -eq "rotwk-retail-user-owned" -and
        @($provenance.incomplete).Count -eq 0
    ) "Selected immutable bundle provenance is not a complete RotWK faction-slice import."
    Assert-M2OracleTrue (
        [string]$audit.provenance_contract -eq [string]$provenance.contract -and
        [string]$audit.profile -eq [string]$provenance.profile -and
        [string]$audit.profile_sha256 -eq [string]$provenance.profile_sha256 -and
        [bool]$audit.semantic_provenance -and
        -not [bool]$audit.light -and
        [bool]$audit.valid -and
        @($audit.errors).Count -eq 0
    ) "Selected immutable bundle provenance audit is missing, stale, light, or invalid."
    $identity = Get-ProofWorkingTreeIdentity $root
    Write-Host "M2_ORACLE_CONTEXT content_source=$contentRootSource content_root=$contentRoot active_pack=$activePack"
    return [pscustomobject]@{
        repoRoot = $root
        oracleRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot "retail-work\oracle"))
        contentRoot = $contentRoot
        profileSha256 = $profileSha256
        bundleSha256 = $bundleSha256
        packRoot = $packRoot
        gitRevision = [string]$identity.revision
        dirtyStateDigest = [string]$identity.dirtyStateDigest
    }
}

function Assert-M2OracleContainedPath {
    param([string]$Path, [string]$OracleRoot)
    $full = [IO.Path]::GetFullPath($Path)
    $rootPrefix = [IO.Path]::GetFullPath($OracleRoot).TrimEnd('\') + '\'
    Assert-M2OracleTrue ($full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) "Oracle evidence must stay below $OracleRoot"
    return $full
}

function Get-M2OracleRelativePath {
    param([string]$BaseDirectory, [string]$Path)
    $base = [IO.Path]::GetFullPath($BaseDirectory).TrimEnd('\') + '\'
    $target = [IO.Path]::GetFullPath($Path)
    $baseUri = [Uri]::new($base)
    $targetUri = [Uri]::new($target)
    Assert-M2OracleTrue ($baseUri.Scheme -eq $targetUri.Scheme) "Cannot make an oracle-relative path across URI schemes."
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString())
}

function Assert-M2OracleManifestIdentity {
    param([object]$Manifest, [object]$Context)
    Assert-M2OracleTrue ([string]$Manifest.schema -eq "openbfme.m2-men-fords-oracle-captures" -and [int]$Manifest.schemaVersion -eq 1) "Capture manifest schema is invalid."
    Assert-M2OracleTrue ([string]$Manifest.profileSha256 -eq [string]$Context.profileSha256) "Capture manifest targets another profile."
    Assert-M2OracleTrue ([string]$Manifest.bundleSha256 -eq [string]$Context.bundleSha256) "Capture manifest targets another bundle."
    Assert-M2OracleTrue ([string]$Manifest.gitRevision -eq [string]$Context.gitRevision -and [string]$Manifest.dirtyStateDigest -eq [string]$Context.dirtyStateDigest) "Capture manifest targets another source identity. Start a new oracle workspace after source changes."
    $rows = @($Manifest.captures)
    $ids = @($rows | ForEach-Object { [string]$_.id })
    Assert-M2OracleTrue ($rows.Count -eq $script:M2OracleCaptureIds.Count) "Capture manifest does not have exactly 47 rows."
    Assert-M2OracleTrue (@($ids | Select-Object -Unique).Count -eq $ids.Count) "Capture manifest has duplicate IDs."
    Assert-M2OracleTrue (@(Compare-Object $script:M2OracleCaptureIds $ids).Count -eq 0) "Capture manifest has the wrong capture IDs."
}

function Write-M2OracleJson {
    param([object]$Value, [string]$Path)
    $json = $Value | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
