[CmdletBinding()]
param(
    [switch]$DryRun = $true,
    [switch]$Execute
)

$ErrorActionPreference = "Stop"
$InformationPreference = "Continue"

# FIX: $PSScriptRoot is <repo>\tools, ONE Split-Path -Parent = repo root
$repoRoot = Split-Path -Parent $PSScriptRoot

# GUARD: Verify repo structure
if (-not (Test-Path (Join-Path $repoRoot "AGENTS.md"))) {
    Write-Error "FATAL: $repoRoot\AGENTS.md not found. Repo root is miscomputed."
    exit 1
}
if (-not (Test-Path (Join-Path $repoRoot "workspace\content-packs"))) {
    Write-Error "FATAL: $repoRoot\workspace\content-packs not found. Invalid repo structure."
    exit 1
}

$workspacePacksRoot = Join-Path $repoRoot "workspace\content-packs"
$durablePacksRoot = "$env:APPDATA\Godot\app_userdata\Open BFME\content-packs"
$workspaceCacheConverted = Join-Path $repoRoot "workspace\retail-work\cache\converted"
$logsRoot = Join-Path $repoRoot "workspace\logs\q88-prune"

if (-not (Test-Path $logsRoot)) { New-Item -ItemType Directory -Path $logsRoot -Force | Out-Null }

Write-Information "Repo root verified: $repoRoot"

function Get-SelectedBundles {
    $selected = @{}
    $wsSelection = "$workspacePacksRoot\selection.json"
    if (-not (Test-Path $wsSelection)) { Write-Error "FATAL: selection.json not found"; exit 1 }

    try {
        $wsJson = Get-Content $wsSelection -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Write-Error "FATAL: selection.json parse failed: $_"; exit 1
    }
    $selected["workspace"] = @{ active = $wsJson.activePack; supplemental = @($wsJson.supplementalPacks) }
    Write-Information "Workspace selection: 1 active + $($wsJson.supplementalPacks.Count) supplemental"

    if (Test-Path $durablePacksRoot) {
        $durSelection = "$durablePacksRoot\selection.json"
        if (Test-Path $durSelection) {
            try {
                $durJson = Get-Content $durSelection -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
                $selected["durable"] = @{ active = $durJson.activePack; supplemental = @($durJson.supplementalPacks) }
                Write-Information "Durable selection: 1 active + $($durJson.supplementalPacks.Count) supplemental"
            } catch {
                Write-Error "FATAL: Durable selection.json parse failed: $_"; exit 1
            }
        }
    }
    return $selected
}

function Get-GatePinnedDigests {
    $pinned = @()
    $gateFiles = Get-ChildItem (Join-Path $repoRoot "tools") -Filter "gate-*.ps1" -File

    foreach ($gateFile in $gateFiles) {
        $content = Get-Content $gateFile.FullName -Raw
        $matches = [regex]::Matches($content, '"([a-z0-9\-]+/[a-f0-9]{64})"')
        foreach ($m in $matches) { $pinned += $m.Groups[1].Value }
    }

    $unique = @($pinned | Select-Object -Unique)
    Write-Information "Gate pins (tools/gate-*.ps1): $($unique.Count) unique"

    if ($unique.Count -eq 0) { Write-Error "FATAL: Gate pin scan produced zero results"; exit 1 }
    return $unique
}

function Get-RunnerPinnedDigests {
    $pinned = @()
    $runnersPath = Join-Path $repoRoot "game\tests"

    if (Test-Path $runnersPath) {
        $runners = Get-ChildItem $runnersPath -Filter "*.gd" -File -Recurse
        foreach ($runner in $runners) {
            $content = Get-Content $runner.FullName -Raw
            $matches = [regex]::Matches($content, '"([a-z0-9\-]+/[a-f0-9]{64})"')
            foreach ($m in $matches) { $pinned += $m.Groups[1].Value }
        }
    }

    $unique = @($pinned | Select-Object -Unique)
    Write-Information "Runner pins (game/tests/**/*.gd): $($unique.Count) unique"
    return $unique
}

function Get-DistPinnedDigests {
    $pinned = @()
    $distRoot = Join-Path $repoRoot "dist"

    if (Test-Path $distRoot) {
        $distVersions = Get-ChildItem $distRoot -Directory -Filter "v0.2.*"
        foreach ($versionDir in $distVersions) {
            $distSelection = Join-Path $versionDir.FullName "content-packs\selection.json"
            if (Test-Path $distSelection) {
                try {
                    $distJson = Get-Content $distSelection -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
                    if ($distJson.activePack) { $pinned += $distJson.activePack }
                    $pinned += @($distJson.supplementalPacks)
                    Write-Information "  Loaded dist/$($versionDir.Name)/content-packs/selection.json"
                } catch {
                    Write-Warning "  Parse failed: $_"
                }
            }
        }
    }

    $unique = @($pinned | Select-Object -Unique)
    Write-Information "Dist pins (dist/**/content-packs/selection.json): $($unique.Count) unique"
    return $unique
}

function Get-AllBundles {
    param($root, [string]$rootName)

    if (-not (Test-Path $root)) { return @() }

    $bundles = @()
    $packDirs = Get-ChildItem $root -Directory

    foreach ($packDir in $packDirs) {
        $bundleDirs = Get-ChildItem $packDir.FullName -Directory
        foreach ($bundleDir in $bundleDirs) {
            $sizeInfo = Get-ChildItem $bundleDir.FullName -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum
            $bundles += @{
                PackId = $packDir.Name
                Digest = $bundleDir.Name
                FullPath = $bundleDir.FullName
                Root = $root
                RootName = $rootName
                SizeBytes = if ($sizeInfo.Sum) { $sizeInfo.Sum } else { 0 }
            }
        }
    }

    Write-Information "Scanned ${rootName}: $($bundles.Count) bundles"
    return $bundles
}

function Build-PruneList {
    param($bundleMap, $allBundles)

    $keepSet = @{}
    $selectedPackIds = @()

    # Rule 1: Selected bundles
    foreach ($selection in $bundleMap.Values) {
        if ($selection.active) {
            $keepSet[$selection.active] = "selected-active"
            $selectedPackIds += (($selection.active -split '/')[0])
        }
        foreach ($bundle in $selection.supplemental) {
            $keepSet[$bundle] = "selected-supplemental"
            $selectedPackIds += (($bundle -split '/')[0])
        }
    }
    $selectedPackIds = @($selectedPackIds | Select-Object -Unique)
    Write-Information "Rule 1 (selected): $($keepSet.Count) bundles"

    # Rule 2: Previous digest per selected pack id
    foreach ($packId in $selectedPackIds) {
        $versions = @($allBundles | Where-Object { $_.PackId -eq $packId } | Sort-Object FullPath)
        foreach ($ver in $versions) {
            $bundleId = "$($ver.PackId)/$($ver.Digest)"
            if (-not $keepSet.ContainsKey($bundleId)) {
                $keepSet[$bundleId] = "previous-digest"
            }
        }
    }
    Write-Information "Rule 2 (previous): $($keepSet.Count) total in keep-set"

    # Rule 3: Pinned digests (gates, runners, dist)
    $gatePins = Get-GatePinnedDigests
    $runnerPins = Get-RunnerPinnedDigests
    $distPins = Get-DistPinnedDigests

    foreach ($pin in ($gatePins + $runnerPins + $distPins)) {
        if ($pin) {
            if ($keepSet.ContainsKey($pin)) {
                $keepSet[$pin] = "$($keepSet[$pin])|pinned"
            } else {
                $keepSet[$pin] = "pinned"
            }
        }
    }
    Write-Information "Rule 3 (pins): $($keepSet.Count) total in keep-set"

    # Build prune list
    $pruneList = @()
    foreach ($bundle in $allBundles) {
        $bundleId = "$($bundle.PackId)/$($bundle.Digest)"
        if ($keepSet.ContainsKey($bundleId)) {
            $keepSet[$bundleId] = "$($keepSet[$bundleId])|found"
        } else {
            # GUARD: Path must be under one of the pack roots
            $safe = ([System.IO.Path]::GetFullPath($bundle.FullPath).StartsWith($workspacePacksRoot)) -or ($bundle.FullPath.StartsWith($durablePacksRoot))
            if (-not $safe) { Write-Error "FATAL: Path not under pack root: $($bundle.FullPath)"; exit 1 }

            $pruneList += @{
                BundleId = $bundleId
                Root = $bundle.Root
                RootName = $bundle.RootName
                Path = $bundle.FullPath
                SizeBytes = $bundle.SizeBytes
                Reason = "unselected-and-unpinned"
            }
        }
    }

    # Orphans
    $tmpOrphans = @()
    if (Test-Path $workspacePacksRoot) {
        $tmpOrphans += Get-ChildItem $workspacePacksRoot -Recurse -Filter "*.json.tmp" -File -ErrorAction SilentlyContinue |
            ForEach-Object { @{ Path = $_.FullName; SizeBytes = $_.Length; Reason = "json.tmp-orphan" } }
    }
    if (Test-Path $workspaceCacheConverted) {
        $tmpOrphans += Get-ChildItem $workspaceCacheConverted -Recurse -Filter "*.json.tmp" -File -ErrorAction SilentlyContinue |
            ForEach-Object { @{ Path = $_.FullName; SizeBytes = $_.Length; Reason = "cache-tmp-orphan" } }
    }

    Write-Information "Prune list: $($pruneList.Count) bundles + $($tmpOrphans.Count) orphans"

    return @{ KeepSet = $keepSet; PruneList = $pruneList; TmpOrphans = $tmpOrphans }
}

function Write-DryRunTable {
    param($result)

    $output = @()
    $output += "=== DRY-RUN REPORT $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="
    $output += ""
    $output += "## KEEP SET (will NOT be deleted)"
    $output += ""

    foreach ($item in $result.KeepSet.GetEnumerator() | Sort-Object Name) {
        $output += "$($item.Name) | $($item.Value)"
    }

    $output += ""
    $output += "## PRUNE LIST"
    $output += ""

    if ($result.PruneList.Count -eq 0) {
        $output += "(no packs to prune)"
    } else {
        $totalBytes = 0
        foreach ($item in $result.PruneList | Sort-Object BundleId) {
            $sizeGB = [math]::Round($item.SizeBytes / 1GB, 3)
            $output += "$($item.BundleId) | $($item.SizeBytes) ($($sizeGB)GB) | $($item.RootName)"
            $totalBytes += $item.SizeBytes
        }
        $output += ""
        $output += "Total prune size: $($totalBytes) bytes ($([math]::Round($totalBytes / 1GB, 2)) GB)"
    }

    $output += ""
    $output += "## TEMPORARY ORPHANS"
    if ($result.TmpOrphans.Count -gt 0) {
        foreach ($item in $result.TmpOrphans) { $output += "$($item.Path) | $($item.SizeBytes) bytes" }
    } else {
        $output += "(none)"
    }

    $output += ""
    $output += "=== END REPORT ==="
    return $output -join "`n"
}

Write-Information "Starting prune script"

$selectedBundles = Get-SelectedBundles
$allBundles = @()
$allBundles += Get-AllBundles $workspacePacksRoot "workspace"
$allBundles += Get-AllBundles $durablePacksRoot "durable"

Write-Information "Total bundles on disk: $($allBundles.Count)"

$pruneResult = Build-PruneList $selectedBundles $allBundles
$dryRunTable = Write-DryRunTable $pruneResult
$dryRunLog = Join-Path $logsRoot "dry-run.txt"

if ($Execute) {
    Write-Information "=== EXECUTING DELETIONS ==="
    Write-Information $dryRunTable

    $deletedCount = 0
    $failedCount = 0
    foreach ($item in $pruneResult.PruneList) {
        try {
            Remove-Item -Path $item.Path -Recurse -Force -ErrorAction Stop
            $deletedCount++
        } catch {
            Write-Error "Failed: $($item.Path)"
            $failedCount++
        }
    }

    foreach ($item in $pruneResult.TmpOrphans) {
        try {
            Remove-Item -Path $item.Path -Force -ErrorAction Stop
        } catch {
            Write-Error "Failed: $($item.Path)"
            $failedCount++
        }
    }

    Write-Information "Deleted: $deletedCount bundles, $failedCount failed"
    $dryRunTable | Out-File -FilePath $dryRunLog -Encoding UTF8
} else {
    Write-Information "=== DRY RUN (use -Execute to delete) ==="
    Write-Information $dryRunTable
    $dryRunTable | Out-File -FilePath $dryRunLog -Encoding UTF8
    Write-Information "Saved to: $dryRunLog"
}
