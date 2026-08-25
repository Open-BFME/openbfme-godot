[CmdletBinding()]
param(
    [switch]$Execute
)

$ErrorActionPreference = "Stop"
$InformationPreference = "Continue"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$workspacePacksRoot = Join-Path $repoRoot "workspace\content-packs"
$durablePacksRoot = "$env:APPDATA\Godot\app_userdata\Open BFME\content-packs"
$workspaceCacheConverted = Join-Path $repoRoot "workspace\retail-work\cache\converted"
$logsRoot = Join-Path $repoRoot "workspace\logs\q88-prune"

if (-not (Test-Path $logsRoot)) {
    New-Item -ItemType Directory -Path $logsRoot -Force | Out-Null
}

# Load selection.json files
function Get-SelectedBundles {
    $selected = @{}

    # Workspace selection
    $wsSelection = "$workspacePacksRoot\selection.json"
    if (Test-Path $wsSelection) {
        $wsJson = Get-Content $wsSelection -Raw | ConvertFrom-Json
        $selected["workspace"] = @{
            active = $wsJson.activePack
            supplemental = @($wsJson.supplementalPacks)
        }
        Write-Information "Workspace selection.json loaded: 1 active + $($wsJson.supplementalPacks.Count) supplemental"
    }

    # Durable selection (if it exists)
    if (Test-Path $durablePacksRoot) {
        $durSelection = "$durablePacksRoot\selection.json"
        if (Test-Path $durSelection) {
            $durJson = Get-Content $durSelection -Raw | ConvertFrom-Json
            $selected["durable"] = @{
                active = $durJson.activePack
                supplemental = @($durJson.supplementalPacks)
            }
            Write-Information "Durable selection.json loaded: 1 active + $($durJson.supplementalPacks.Count) supplemental"
        }
    }

    return $selected
}

# Get pinned digests from gate-retail.ps1 Section B
function Get-GatePinnedDigests {
    $gatePath = Join-Path $repoRoot "tools\gate-retail.ps1"
    $pinned = @()

    if (Test-Path $gatePath) {
        $content = Get-Content $gatePath -Raw
        # Extract the pinned digests from the expectedSelectionSupplementalPacks array
        $match = [regex]::Matches($content, '"([a-z0-9\-]+/[a-f0-9]{64})"')
        foreach ($m in $match) {
            $pinned += $m.Groups[1].Value
        }
        Write-Information "Found $($pinned.Count) pinned digests in gate-retail.ps1"
    }

    return @($pinned | Select-Object -Unique)
}

# Get pinned digests from Godot runner files
function Get-RunnerPinnedDigests {
    $pinned = @()
    $runnersPath = Join-Path $repoRoot "game\tests"

    if (Test-Path $runnersPath) {
        $runners = Get-ChildItem $runnersPath -Filter "*runner.gd"
        foreach ($runner in $runners) {
            $content = Get-Content $runner.FullName -Raw
            # Look for bundle references like rotwk-men-vslice/hash
            $matches = [regex]::Matches($content, '"([a-z0-9\-]+/[a-f0-9]{64})"')
            foreach ($m in $matches) {
                $pinned += $m.Groups[1].Value
            }
        }
    }

    if ($pinned.Count -gt 0) {
        Write-Information "Found $($pinned.Count) pinned digests in runner files"
    }

    return @($pinned | Select-Object -Unique)
}

# Get all bundles currently on disk
function Get-AllBundles {
    param($root)

    if (-not (Test-Path $root)) {
        return @()
    }

    $bundles = @()
    $packDirs = Get-ChildItem $root -Directory

    foreach ($packDir in $packDirs) {
        $bundleDirs = Get-ChildItem $packDir.FullName -Directory
        foreach ($bundleDir in $bundleDirs) {
            $bundles += @{
                PackId = $packDir.Name
                Digest = $bundleDir.Name
                FullPath = $bundleDir.FullName
                Root = $root
                SizeBytes = (Get-ChildItem $bundleDir.FullName -Recurse -File | Measure-Object -Property Length -Sum).Sum
            }
        }
    }

    return $bundles
}

# Main prune logic
function Build-PruneList {
    param($bundleMap)

    $keepSet = @{}
    $pruneList = @()

    # Add all selected bundles to keep set
    foreach ($selection in $bundleMap.Values) {
        if ($selection.active) {
            $keepSet[$selection.active] = "selected-active"
        }
        foreach ($bundle in $selection.supplemental) {
            $keepSet[$bundle] = "selected-supplemental"
        }
    }

    # Add pinned digests from gates and runners
    $gatePins = Get-GatePinnedDigests
    $runnerPins = Get-RunnerPinnedDigests

    foreach ($pin in ($gatePins + $runnerPins)) {
        if ($pin) {
            $keepSet[$pin] = "gate-pinned"
        }
    }

    # Build prune list
    $allBundles = @()
    $allBundles += Get-AllBundles $workspacePacksRoot
    if (Test-Path $durablePacksRoot) {
        $allBundles += Get-AllBundles $durablePacksRoot
    }

    foreach ($bundle in $allBundles) {
        $bundleId = "$($bundle.PackId)/$($bundle.Digest)"
        if ($keepSet.ContainsKey($bundleId)) {
            $keepSet[$bundleId] = "$($keepSet[$bundleId])|found"
        } else {
            $pruneList += @{
                BundleId = $bundleId
                Root = $bundle.Root
                Path = $bundle.FullPath
                SizeBytes = $bundle.SizeBytes
                Reason = "unselected-and-unpinned"
            }
        }
    }

    # Scan for .tmp orphans
    $tmpOrphans = @()

    # workspace/content-packs *.json.tmp
    if (Test-Path $workspacePacksRoot) {
        $tmpOrphans += Get-ChildItem $workspacePacksRoot -Recurse -Filter "*.json.tmp" -File | ForEach-Object {
            @{
                Path = $_.FullName
                SizeBytes = $_.Length
                Reason = "json.tmp-orphan"
            }
        }
    }

    # workspace/retail-work/cache/converted *.json.tmp
    if (Test-Path $workspaceCacheConverted) {
        $tmpOrphans += Get-ChildItem $workspaceCacheConverted -Recurse -Filter "*.json.tmp" -File | ForEach-Object {
            @{
                Path = $_.FullName
                SizeBytes = $_.Length
                Reason = "cache-json.tmp-orphan"
            }
        }
    }

    return @{
        KeepSet = $keepSet
        PruneList = $pruneList
        TmpOrphans = $tmpOrphans
    }
}

# Output dry-run table
function Write-DryRunTable {
    param($result)

    $output = @()
    $output += "=== DRY-RUN REPORT $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="
    $output += ""
    $output += "## KEEP SET (will NOT be deleted)"
    $output += ""
    $output += "BundleId | Reason"
    $output += "---------|--------"

    foreach ($item in $result.KeepSet.GetEnumerator() | Sort-Object Name) {
        $output += "$($item.Name) | $($item.Value)"
    }

    $output += ""
    $output += "## PRUNE LIST (WILL BE DELETED if -Execute is used)"
    $output += ""

    if ($result.PruneList.Count -eq 0) {
        $output += "(no packs to prune)"
    } else {
        $output += "BundleId | Bytes | Root | Reason"
        $output += "---------|-------|------|--------"
        $totalBytes = 0
        foreach ($item in $result.PruneList | Sort-Object BundleId) {
            $sizeGB = [math]::Round($item.SizeBytes / 1GB, 3)
            $output += "$($item.BundleId) | $($item.SizeBytes) ($($sizeGB)GB) | $($item.Root) | $($item.Reason)"
            $totalBytes += $item.SizeBytes
        }
        $output += ""
        $output += "Total prune size: $('{0:N0}' -f $totalBytes) bytes ($([math]::Round($totalBytes / 1GB, 2)) GB)"
    }

    $output += ""
    $output += "## TEMPORARY ORPHANS (*.json.tmp, WILL BE DELETED if -Execute is used)"
    $output += ""

    if ($result.TmpOrphans.Count -eq 0) {
        $output += "(no .json.tmp files found)"
    } else {
        $output += "Path | Bytes | Reason"
        $output += "-----|-------|--------"
        $totalOrphanBytes = 0
        foreach ($item in $result.TmpOrphans | Sort-Object Path) {
            $output += "$($item.Path) | $($item.SizeBytes) | $($item.Reason)"
            $totalOrphanBytes += $item.SizeBytes
        }
        $output += ""
        $output += "Total orphan size: $('{0:N0}' -f $totalOrphanBytes) bytes ($([math]::Round($totalOrphanBytes / 1MB, 2)) MB)"
    }

    $output += ""
    $output += "=== END REPORT ==="

    return $output -join "`n"
}

# Main execution
Write-Information "Starting prune script (Repo: $repoRoot)"

$selectedBundles = Get-SelectedBundles
$pruneResult = Build-PruneList $selectedBundles

$dryRunTable = Write-DryRunTable $pruneResult
$dryRunLog = Join-Path $logsRoot "dry-run.txt"

if (-not $Execute) {
    Write-Information "=== DRY RUN MODE (use -Execute to delete) ==="
    Write-Information ""
    Write-Information $dryRunTable
    Write-Information ""
    Write-Information "Dry-run results saved to: $dryRunLog"
    $dryRunTable | Out-File -FilePath $dryRunLog -Encoding UTF8
} else {
    Write-Information "=== EXECUTE MODE - DELETING FILES ==="
    Write-Information ""
    Write-Information $dryRunTable
    Write-Information ""
    Write-Information "Executing deletions..."
    Write-Information ""

    # Delete prune list items
    $deletedCount = 0
    $failedCount = 0
    foreach ($item in $pruneResult.PruneList) {
        try {
            Write-Information "Deleting: $($item.Path)"
            Remove-Item -Path $item.Path -Recurse -Force -ErrorAction Stop
            Write-Information "  ✓ Deleted"
            $deletedCount++
        } catch {
            Write-Error "Failed to delete $($item.Path): $_"
            $failedCount++
        }
    }

    # Delete .tmp orphans
    $orphanDeletedCount = 0
    foreach ($item in $pruneResult.TmpOrphans) {
        try {
            Write-Information "Deleting orphan: $($item.Path)"
            Remove-Item -Path $item.Path -Force -ErrorAction Stop
            Write-Information "  ✓ Deleted"
            $orphanDeletedCount++
        } catch {
            Write-Error "Failed to delete $($item.Path): $_"
            $failedCount++
        }
    }

    Write-Information ""
    Write-Information "Deletion summary:"
    Write-Information "  Bundles deleted: $deletedCount"
    Write-Information "  Orphans deleted: $orphanDeletedCount"
    Write-Information "  Failed deletions: $failedCount"
    Write-Information ""
    Write-Information "Execution complete. Dry-run log saved for reference."
    $dryRunTable | Out-File -FilePath $dryRunLog -Encoding UTF8
    Write-Information "Log: $dryRunLog"
}
