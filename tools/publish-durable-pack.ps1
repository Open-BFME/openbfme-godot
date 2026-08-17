# Publish the workspace content selection into the durable user pack cache.
#
# Launches that cannot see the repo workspace (exported builds, installs on
# another machine) resolve content from the durable Godot user cache
# (user://content-packs). This script mirrors the current workspace selection
# (workspace/content-packs/selection.json plus every pack bundle it names) into
# that cache so such launches play the same content as env-driven runs.
#
# Release firewall: this copies retail-derived bytes ONLY into the local user
# cache under %APPDATA%. It never stages them into the repository or any
# distributable artifact.
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$DurableRoot = (Join-Path $env:APPDATA "Godot\app_userdata\Open BFME\content-packs"),
    [switch]$DryRun,
    # Compare only; copy nothing. Exits 1 and prints one DRIFT line per
    # disagreement. The durable cache is a COPY, so it silently lags every
    # republish -- an env-less launch and the MP lobby then serve the old
    # content with nothing failing. Run this wherever the workspace selection
    # is gated.
    #
    # Checks, in order: the durable selection document parses and matches the
    # openbfme.pack-selection schema; it names the same active pack and the same
    # supplements in the same ORDER; and every named bundle hashes identically
    # on both sides, file by file. That last check reads every byte of every
    # named bundle, so expect minutes on a full retail selection.
    [switch]$Verify
)

$ErrorActionPreference = "Stop"

if ($WorkspaceRoot -eq "") {
    # $PSScriptRoot is not available in param defaults under Windows PowerShell 5.1.
    $repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
    $WorkspaceRoot = Join-Path $repoRoot "workspace\content-packs"
}

function Get-BundleContentDigest([string]$Root) {
    # One digest over the WHOLE bundle: a manifest of "<relative-path> <sha256>"
    # lines, sorted, hashed. pack.json carries no digest of the bundle's bytes,
    # so hashing it proves only which bundle was published -- not that the
    # published bytes survived. Corruption, truncation and substitution under
    # data/, assets/, effects/, sources/ and provenance/ are only visible here.
    $rootFull = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd("\")
    $entries = New-Object "System.Collections.Generic.List[string]"
    $keys = New-Object "System.Collections.Generic.List[string]"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force) {
            $relative = $file.FullName.Substring($rootFull.Length + 1).Replace("\", "/")
            $stream = [System.IO.File]::OpenRead($file.FullName)
            try {
                $fileHash = [System.BitConverter]::ToString($sha.ComputeHash($stream)).Replace("-", "")
            }
            finally { $stream.Dispose() }
            $entries.Add("$relative $fileHash")
            # Case-folded primary key with an ordinal tiebreak: Windows cannot
            # hold two paths differing only in case, Linux can, and a bare
            # culture-aware sort reorders punctuation per locale. This ordering
            # is identical on every host, so the digest is comparable anywhere.
            $keys.Add($relative.ToLowerInvariant() + "`0" + $relative)
        }
    }
    finally { $sha.Dispose() }
    $keyArray = [string[]]$keys.ToArray()
    $entryArray = [string[]]$entries.ToArray()
    [System.Array]::Sort($keyArray, $entryArray, [System.StringComparer]::Ordinal)
    $manifest = ($entryArray -join "`n")
    $digest = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($manifest)
        return [System.BitConverter]::ToString($digest.ComputeHash($bytes)).Replace("-", "")
    }
    finally { $digest.Dispose() }
}

function Get-SelectionSchemaFault($Document) {
    # A durable selection the loader would reject (or read differently) is drift
    # even when every byte under the bundles is perfect: game/src/content/
    # mod_loader.gd returns NO supplements at all when the schema or version
    # does not match, so a wrong-schema durable document silently degrades an
    # env-less launch to the active pack alone.
    if ($null -eq $Document) { return "durable selection.json is not a JSON object" }
    $names = @($Document.PSObject.Properties.Name)
    foreach ($required in @("schema", "schemaVersion", "activePack")) {
        if ($names -notcontains $required) { return "durable selection.json has no '$required' field" }
    }
    if ($Document.schema -ne "openbfme.pack-selection") {
        return "durable selection.json schema is '$($Document.schema)', expected 'openbfme.pack-selection'"
    }
    if ($Document.schemaVersion -ne 0) {
        return "durable selection.json schemaVersion is '$($Document.schemaVersion)', expected 0"
    }
    if (($Document.activePack -isnot [string]) -or [string]::IsNullOrWhiteSpace($Document.activePack)) {
        return "durable selection.json activePack is empty or not a string"
    }
    if ($names -contains "supplementalPacks" -and $null -ne $Document.supplementalPacks) {
        if ($Document.supplementalPacks -isnot [System.Array]) {
            return "durable selection.json supplementalPacks is not an array"
        }
        foreach ($entry in $Document.supplementalPacks) {
            if (($entry -isnot [string]) -or [string]::IsNullOrWhiteSpace($entry)) {
                return "durable selection.json supplementalPacks holds a non-string entry"
            }
        }
    }
    return ""
}

function Test-SafeRelativePack([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative)) { return $false }
    $normalized = $Relative.Replace("\", "/")
    if ($normalized.StartsWith("/") -or $normalized.StartsWith("~") -or $normalized.Contains(":")) { return $false }
    foreach ($segment in $normalized.Split("/")) {
        if ($segment -eq "" -or $segment -eq "." -or $segment -eq "..") { return $false }
    }
    return $true
}

$selectionPath = Join-Path $WorkspaceRoot "selection.json"
if (-not (Test-Path $selectionPath)) {
    Write-Error "No workspace selection at $selectionPath - nothing to publish."
}
$selection = Get-Content $selectionPath -Raw | ConvertFrom-Json
if ($selection.schema -ne "openbfme.pack-selection" -or $selection.schemaVersion -ne 0) {
    Write-Error "Unsupported selection schema in $selectionPath"
}

$packs = @($selection.activePack)
if ($null -ne $selection.supplementalPacks) {
    $packs += @($selection.supplementalPacks)
}

foreach ($pack in $packs) {
    if (-not (Test-SafeRelativePack $pack)) {
        Write-Error "Unsafe pack path in selection: $pack"
    }
    $source = Join-Path $WorkspaceRoot ($pack.Replace("/", "\"))
    if (-not (Test-Path (Join-Path $source "pack.json"))) {
        Write-Error "Selection names a pack with no pack.json: $source"
    }
}

if ($Verify) {
    $drift = @()
    $durableSelectionPath = Join-Path $DurableRoot "selection.json"
    if (-not (Test-Path $durableSelectionPath)) {
        $drift += "DRIFT durable cache has no selection.json at $durableSelectionPath"
    }
    else {
        $durableSelection = $null
        try {
            $durableSelection = Get-Content $durableSelectionPath -Raw | ConvertFrom-Json
        }
        catch {
            $durableSelection = $null
            $drift += "DRIFT durable selection.json is not valid JSON: $($_.Exception.Message)"
        }
        $schemaFault = ""
        if ($null -ne $durableSelection) { $schemaFault = Get-SelectionSchemaFault $durableSelection }
        if ($schemaFault -ne "") {
            $drift += "DRIFT $schemaFault"
        }
        elseif ($null -ne $durableSelection) {
            if ($durableSelection.activePack -ne $selection.activePack) {
                $drift += "DRIFT activePack workspace=$($selection.activePack) durable=$($durableSelection.activePack)"
            }
            $workspaceSupplemental = @()
            if ($null -ne $selection.supplementalPacks) { $workspaceSupplemental = @($selection.supplementalPacks) }
            $durableSupplemental = @()
            if ($null -ne $durableSelection.supplementalPacks) { $durableSupplemental = @($durableSelection.supplementalPacks) }
            foreach ($pack in $workspaceSupplemental) {
                if ($durableSupplemental -notcontains $pack) {
                    $drift += "DRIFT supplementalPack missing from durable selection: $pack"
                }
            }
            foreach ($pack in $durableSupplemental) {
                if ($workspaceSupplemental -notcontains $pack) {
                    $drift += "DRIFT durable selection names a pack the workspace does not: $pack"
                }
            }
            # Set equality is not enough. game/src/content/mod_loader.gd:190
            # appends selected_pack_supplements() in DOCUMENT order and :192-198
            # de-duplicates keeping the FIRST occurrence, so document order picks
            # the survivor whenever two entries resolve to the same root. The
            # sort at :205-215 then reorders by pack.json priority, which means a
            # resequenced durable list is not always a different final load --
            # but it is always a document that no longer mirrors the workspace,
            # and this script's whole job is to prove the copy is faithful.
            if (($workspaceSupplemental -join "`n") -ne ($durableSupplemental -join "`n")) {
                $sameSet = $true
                foreach ($pack in $workspaceSupplemental) { if ($durableSupplemental -notcontains $pack) { $sameSet = $false } }
                foreach ($pack in $durableSupplemental) { if ($workspaceSupplemental -notcontains $pack) { $sameSet = $false } }
                if ($sameSet) {
                    $drift += "DRIFT supplementalPacks load order differs: workspace=[$($workspaceSupplemental -join ', ')] durable=[$($durableSupplemental -join ', ')]"
                }
            }
        }
    }
    foreach ($pack in $packs) {
        # Whole-bundle content digests, recomputed on BOTH sides at verify time.
        # No digest is stored at publish time on purpose: a stored manifest is
        # itself a copy that can rot or be rewritten alongside the bytes it
        # vouches for, and it could only ever attest the durable side. Deriving
        # both digests from the trees themselves keeps this check stateless --
        # it detects durable corruption AND a workspace bundle that changed
        # under a path the durable cache already holds. The cost is one full
        # read of each named bundle, which is why -Verify is a gate, not a
        # per-launch check.
        $sourceRoot = Join-Path $WorkspaceRoot ($pack.Replace("/", "\"))
        $targetRoot = Join-Path $DurableRoot ($pack.Replace("/", "\"))
        if (-not (Test-Path (Join-Path $targetRoot "pack.json"))) {
            $drift += "DRIFT durable cache is missing pack bundle: $pack"
            continue
        }
        $sourceHash = Get-BundleContentDigest $sourceRoot
        $targetHash = Get-BundleContentDigest $targetRoot
        if ($sourceHash -ne $targetHash) {
            $drift += "DRIFT bundle content differs for $pack (workspace=$sourceHash durable=$targetHash)"
        }
    }
    if ($drift.Count -gt 0) {
        foreach ($line in $drift) { Write-Host $line }
        Write-Host "Durable cache is STALE: $($drift.Count) drift(s). Run tools\publish-durable-pack.ps1 to refresh it."
        exit 1
    }
    Write-Host "Durable cache matches the workspace selection ($($packs.Count) pack bundle(s))."
    exit 0
}

Write-Host "Publishing $($packs.Count) pack bundle(s) from $WorkspaceRoot"
Write-Host "                                         to $DurableRoot"
foreach ($pack in $packs) {
    $source = Join-Path $WorkspaceRoot ($pack.Replace("/", "\"))
    $target = Join-Path $DurableRoot ($pack.Replace("/", "\"))
    Write-Host "  $pack"
    if ($DryRun) { continue }
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    # /MIR keeps the immutable bundle byte-identical; unchanged files are skipped.
    robocopy $source $target /MIR /NJH /NJS /NDL /NFL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) {
        Write-Error "robocopy failed for $pack (exit $LASTEXITCODE)"
    }
}

if (-not $DryRun) {
    # The selection document is written last so a partially copied publish can
    # never be selected: until this file lands, the old selection stays active.
    $staged = Join-Path $DurableRoot "selection.json.publishing"
    Copy-Item $selectionPath $staged -Force
    Move-Item $staged (Join-Path $DurableRoot "selection.json") -Force
    Write-Host "Durable selection updated: $(Join-Path $DurableRoot 'selection.json')"
} else {
    Write-Host "Dry run - nothing copied."
}
