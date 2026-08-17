<#
.SYNOPSIS
    Mutation tests for the derived War of the Ring staging plan.

.DESCRIPTION
    Every refusal added to the bundler is only worth having if it actually
    fires, so each one is broken here on purpose and the result asserted. The
    tests run against SYNTHETIC fixtures - a throwaway checkout-shaped directory
    with copies of the real loaders, and a throwaway workspace with copies of
    the real bundle manifests - so nothing here touches the repository, the
    private workspace or any built bundle.

    The important one is "a new loader with no staging rule". That is the
    sixth-bundle case: the whole point of deriving the plan is that adding a
    loader stops the build instead of silently shipping without its data.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test-wotr-data-staging.ps1
#>
[CmdletBinding()]
param([string]$Scratch = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'playable-bundle-common.ps1')

$script:Passed = 0
$script:Failed = New-Object 'System.Collections.Generic.List[string]'

function Assert-Refusal {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Expect,
        [Parameter(Mandatory)][scriptblock]$Action
    )
    $message = ''
    try { & $Action; $message = '' } catch { $message = $_.Exception.Message }
    if ($message -eq '') {
        $script:Failed.Add("$Name - NOTHING WAS REFUSED (the mutation shipped)")
        Write-Host "  FAIL $Name - nothing was refused" -ForegroundColor Red
        return
    }
    if ($message -cnotlike "*$Expect*") {
        $script:Failed.Add("$Name - refused, but not for the expected reason")
        Write-Host "  FAIL $Name - refused with: $($message -replace "`n", ' ')" -ForegroundColor Red
        return
    }
    $script:Passed++
    Write-Host "  OK   $Name" -ForegroundColor Green
    Write-Host "       $((($message -split "`n")[0]).Trim())" -ForegroundColor DarkGray
}

function Assert-True {
    param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][bool]$Condition, [string]$Detail = '')
    if ($Condition) {
        $script:Passed++
        Write-Host "  OK   $Name" -ForegroundColor Green
        if ($Detail -ne '') { Write-Host "       $Detail" -ForegroundColor DarkGray }
    } else {
        $script:Failed.Add($Name)
        Write-Host "  FAIL $Name $Detail" -ForegroundColor Red
    }
}

$repoRoot = Get-BundleRepoRoot -StartPath $PSScriptRoot
if ($Scratch -eq '') { $Scratch = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-wotr-mutation-" + [Guid]::NewGuid().ToString('N').Substring(0, 8)) }
$Scratch = [IO.Path]::GetFullPath($Scratch)

function New-Fixture {
    <#
        A checkout-shaped directory: game/project.godot plus copies of the real
        loaders. Mutating a copy is how a "someone added a loader" case is
        tested without editing the repository.
    #>
    param([string]$Name)
    $root = Join-Path $Scratch $Name
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    $wotr = Join-Path $root 'game\src\wotr'
    [void](New-Item -ItemType Directory -Path $wotr -Force)
    Write-BundleTextFile -Path (Join-Path $root 'game\project.godot') -Content "; fixture`n"
    Copy-Item -Path (Join-Path $repoRoot 'game\src\wotr\*.gd') -Destination $wotr -Force
    return $root
}

function New-WorkspaceFixture {
    <#
        A workspace-shaped directory carrying only the MANIFESTS of the real
        bundles - not their payloads - which is all the plan reads. Cheap enough
        to build one per test.
    #>
    param([string]$Name, [string[]]$Exclude = @())
    $root = Join-Path $Scratch "$Name-workspace"
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    [void](New-Item -ItemType Directory -Path $root -Force)
    $source = Join-Path (Get-BundleMainWorktree -RepoRoot $repoRoot) 'workspace\retail-work'
    # ENUMERATED, NOT LISTED. This was a hardcoded list of five directory names
    # and it went stale the moment a sixth bundle landed - the auto-resolve
    # converter - which is EXACTLY the failure mode the guard under test exists
    # to prevent. A fixture that has to be edited whenever the thing it tests
    # grows will silently stop covering it. So the fixture mirrors whatever the
    # workspace actually holds, and the assertions below compare that against
    # the loader census rather than against a number somebody typed.
    $directories = @(
        Get-ChildItem -LiteralPath $source -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Name }
    )
    foreach ($directory in $directories) {
        if ($Exclude -ccontains $directory) { continue }
        $from = Join-Path $source $directory
        if (-not (Test-Path -LiteralPath $from -PathType Container)) { continue }
        $to = Join-Path $root $directory
        [void](New-Item -ItemType Directory -Path $to -Force)
        foreach ($file in @(Get-ChildItem -LiteralPath $from -Filter '*.json' -File)) {
            if ($Exclude -ccontains $file.Name) { continue }
            if ($file.Length -gt 8MB) { continue }
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $to $file.Name) -Force
        }
    }
    return $root
}

try {
    Write-Host ''
    Write-Host 'War of the Ring staging - mutation tests' -ForegroundColor White
    Write-Host "  scratch $Scratch"
    Write-Host ''

    # ---------------------------------------------------------------- control
    Write-BundleHeading 'Control: the unmutated checkout and workspace'
    $baseRepo = New-Fixture -Name 'base'
    $baseWorkspace = New-WorkspaceFixture -Name 'base'
    $plan = New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($baseWorkspace)
    Assert-True -Name 'the plan is produced and covers every loader' `
        -Condition ($plan.rules.Count -ge 9 -and $plan.missing.Count -eq 0) `
        -Detail "$($plan.rules.Count) loaders, $($plan.missing.Count) missing"
    Assert-True -Name 'the document is chosen by the bundles own provenance' `
        -Condition ([IO.Path]::GetFileName($plan.document.chosen) -ceq $plan.document.provenanceFileName) `
        -Detail "$([IO.Path]::GetFileName($plan.document.chosen)) - $($plan.document.basis)"

    # ------------------------------------------------ THE SIXTH-BUNDLE CASE
    Write-BundleHeading 'Mutation: a new loader lands with no staging rule'
    $newLoader = New-Fixture -Name 'new-loader'
    Write-BundleTextFile -Path (Join-Path $newLoader 'game\src\wotr\wotr_weather.gd') -Content @'
extends RefCounted
const SCHEMA := "openbfme.living-world-weather"
const SCHEMA_VERSION := 1
const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_WEATHER"
const FILE_NAME := "living-world-weather.json"
'@
    Assert-Refusal -Name 'a sixth bundle cannot be silently missed' `
        -Expect 'has no staging rule for it' `
        -Action { New-WotrStagingPlan -RepoRoot $newLoader -WorkspaceRoots @($baseWorkspace) }

    Write-BundleHeading 'Mutation: a loader is removed and its rule is left behind'
    $goneLoader = New-Fixture -Name 'gone-loader'
    Remove-Item -LiteralPath (Join-Path $goneLoader 'game\src\wotr\wotr_macros.gd') -Force
    Assert-Refusal -Name 'a staging rule with no reader is refused' `
        -Expect 'no loader in game/src/wotr' `
        -Action { New-WotrStagingPlan -RepoRoot $goneLoader -WorkspaceRoots @($baseWorkspace) }

    Write-BundleHeading 'Mutation: the loader directory is gone entirely'
    $noLoaders = New-Fixture -Name 'no-loaders'
    Remove-Item -LiteralPath (Join-Path $noLoaders 'game\src\wotr') -Recurse -Force
    Assert-Refusal -Name 'an empty staging plan is refused rather than shipped' `
        -Expect 'loader directory is missing' `
        -Action { New-WotrStagingPlan -RepoRoot $noLoaders -WorkspaceRoots @($baseWorkspace) }

    # -------------------------------------------------------- absent bundles
    Write-BundleHeading 'Mutation: a converted bundle is absent from the workspace'
    $withoutMarkers = New-WorkspaceFixture -Name 'no-markers' -Exclude @('livingworld-markers')
    $planNoMarkers = New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($withoutMarkers)
    $markerRow = @($planNoMarkers.missing | Where-Object { $_.env -ceq 'OPENBFME_LIVING_WORLD_MARKERS' })
    Assert-True -Name 'the absent bundle is named, with what the player loses' `
        -Condition ($markerRow.Count -eq 1 -and [string]$markerRow[0].loses -ne '') `
        -Detail $(if ($markerRow.Count -eq 1) { "OPENBFME_LIVING_WORLD_MARKERS - $($markerRow[0].loses)" } else { 'NOT REPORTED' })
    Assert-True -Name 'the bundles that are present are still staged' `
        -Condition (@($planNoMarkers.rules | Where-Object { $_.present }).Count -eq ($planNoMarkers.rules.Count - 1)) `
        -Detail "$(@($planNoMarkers.rules | Where-Object { $_.present }).Count) of $($planNoMarkers.rules.Count) present"

    # ------------------------------------------------------------ ambiguity
    Write-BundleHeading 'Mutation: two copies of one bundle in the workspace'
    $twoCopies = New-WorkspaceFixture -Name 'two-copies'
    Copy-Item -LiteralPath (Join-Path $twoCopies 'livingworld-markers') -Destination (Join-Path $twoCopies 'livingworld-markers-old') -Recurse -Force
    Assert-Refusal -Name 'an ambiguous bundle is refused, not guessed' `
        -Expect 'cannot know which one the release should carry' `
        -Action { New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($twoCopies) }

    # ------------------------------------------------------ document choice
    Write-BundleHeading 'Mutation: ship a document the region bundles were not built against'
    $otherDocument = @(Get-ChildItem -LiteralPath (Join-Path $baseWorkspace 'reports') -Filter '*living-world.json' -File |
        Where-Object { $_.Name -cne $plan.document.provenanceFileName })
    if ($otherDocument.Count -eq 0) {
        Write-BundleWarn 'skipped: the workspace holds only one living-world document, so there is nothing to mismatch'
    } else {
        Assert-Refusal -Name 'a mismatched living-world document is refused' `
            -Expect 'were built against' `
            -Action { New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($baseWorkspace) -DocumentOverride $otherDocument[0].FullName }
        $forced = New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($baseWorkspace) `
            -DocumentOverride $otherDocument[0].FullName -AllowMismatchedDocument
        Assert-True -Name '-AllowMismatchedWotrDocument lets it through and records the basis' `
            -Condition ([IO.Path]::GetFileName($forced.document.chosen) -ceq $otherDocument[0].Name -and $forced.document.provenanceFileName -ne '') `
            -Detail "ships $([IO.Path]::GetFileName($forced.document.chosen)), provenance says $($forced.document.provenanceFileName)"
    }

    Write-BundleHeading 'Mutation: the document the bundles cite is not in the workspace'
    $noDocument = New-WorkspaceFixture -Name 'no-document'
    Remove-Item -LiteralPath (Join-Path $noDocument "reports\$($plan.document.provenanceFileName)") -Force
    Assert-Refusal -Name 'a cited-but-absent document is refused' `
        -Expect 'no living-world document with that name is in the workspace' `
        -Action { New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($noDocument) }

    # ------------------------------------------------------------ collision
    Write-BundleHeading 'Mutation: two bundles would land on the same staged file'
    $collide = New-WorkspaceFixture -Name 'collide'
    # Give the markers bundle a PAYLOAD file the region bundle also has - not a
    # second manifest, which the ambiguity guard above would catch first. Both
    # are staged into data/living-map-regions, so one would silently overwrite
    # the other.
    Write-BundleTextFile -Path (Join-Path $collide 'livingmap-regions\shared-payload.dat') -Content 'from the region bundle'
    Write-BundleTextFile -Path (Join-Path $collide 'livingworld-markers\shared-payload.dat') -Content 'from the marker bundle'
    Assert-Refusal -Name 'a staging collision is refused before anything is copied' `
        -Expect 'would land on the same file inside the content pack' `
        -Action { New-WotrStagingPlan -RepoRoot $baseRepo -WorkspaceRoots @($collide) }

    # ----------------------------------------------------- artefact validity
    Write-BundleHeading 'Mutation: a converted bundle declares the wrong schema'
    $wrongSchema = Join-Path $Scratch 'wrong-schema.json'
    Write-BundleTextFile -Path $wrongSchema -Content '{"schema":"openbfme.something-else","schemaVersion":1}'
    Assert-Refusal -Name 'a bundle the loader would reject is refused here first' `
        -Expect 'the loader accepts only' `
        -Action { Test-BundleWotrArtifact -Path $wrongSchema -Schema 'openbfme.living-world-markers' -Label 'the test artefact' }

    $noSchema = Join-Path $Scratch 'no-schema.json'
    Write-BundleTextFile -Path $noSchema -Content '{"totals":{}}'
    Assert-Refusal -Name 'a bundle with no schema at all is refused' `
        -Expect 'declares no schema at all' `
        -Action { Test-BundleWotrArtifact -Path $noSchema -Schema 'openbfme.living-world-markers' -Label 'the test artefact' }

    $notJson = Join-Path $Scratch 'not-json.json'
    Write-BundleTextFile -Path $notJson -Content 'this is not json'
    Assert-Refusal -Name 'a non-object artefact is refused' `
        -Expect 'is not a JSON object' `
        -Action { Test-BundleWotrArtifact -Path $notJson -Schema 'openbfme.living-world-markers' -Label 'the test artefact' }

    # A real retail string table carries keys differing only in case. Godot keeps
    # both; Windows PowerShell 5.1 ConvertFrom-Json throws. Non-strict validation
    # must NOT refuse it - a false refusal is its own defect.
    $caseKeys = Join-Path $Scratch 'case-keys.json'
    Write-BundleTextFile -Path $caseKeys -Content '{"schema":"openbfme.wotr-setup-strings","strings":{"APT:Name":"a","Apt:Name":"b"}}'
    $accepted = $true
    try { [void](Test-BundleWotrArtifact -Path $caseKeys -Schema 'openbfme.wotr-setup-strings' -Label 'the test artefact') } catch { $accepted = $false }
    Assert-True -Name 'a retail string table with case-differing keys is NOT falsely refused' -Condition $accepted

    Write-Host ''
    if ($script:Failed.Count -eq 0) {
        Write-Host "ALL $($script:Passed) MUTATION TESTS PASSED - every refusal fires when its guard is broken." -ForegroundColor Green
        exit 0
    }
    Write-Host "$($script:Failed.Count) MUTATION TEST(S) FAILED:" -ForegroundColor Red
    foreach ($failure in $script:Failed) { Write-Host "  $failure" -ForegroundColor Red }
    exit 1
} finally {
    if (Test-Path -LiteralPath $Scratch) { Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue }
}
