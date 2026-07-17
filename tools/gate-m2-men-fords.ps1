[CmdletBinding()]
param(
    [string]$Install = "F:\BFME2",
    [string]$GodotPath = "",
    [switch]$IntegrationOwnerPublish,
    [string]$OracleApproval = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")
. (Join-Path $PSScriptRoot "m2-oracle-common.ps1")
. (Join-Path $PSScriptRoot "m2-reliability-evidence-common.ps1")

$gate = "M2_MEN_FORDS_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$profilePath = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work\profiles\men-fords-v0-complete.generated.json"))
$contentRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\content-packs"))
$selectionPath = Join-Path $contentRoot "selection.json"
$retailGate = Join-Path $PSScriptRoot "gate-retail.ps1"
$focusedGate = Join-Path $PSScriptRoot "gate-m2-focused.ps1"
$expectedProfileSha256 = "0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d"
$expectedPackId = "bfme2-men-vslice"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use|SCRIPT ERROR)\b'
$requiredCaptureIds = $script:M2OracleCaptureIds
$approvalPath = if ([string]::IsNullOrWhiteSpace($OracleApproval)) {
    Join-Path $repoRoot ".private\retail-work\oracle\m2-men-fords-approval.json"
} else {
    [IO.Path]::GetFullPath($OracleApproval)
}

function Assert-M2 {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-Json {
    param([string]$Path)
    Assert-M2 (Test-Path -LiteralPath $Path -PathType Leaf) "Missing JSON dependency: $Path"
    return (Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json)
}

function Assert-Sha256 {
    param([string]$Value, [string]$Label)
    Assert-M2 ($Value -match '^[0-9a-f]{64}$') "$Label is not a lowercase SHA-256."
}

try {
    Assert-M2 $IntegrationOwnerPublish.IsPresent "Final M2 acceptance requires -IntegrationOwnerPublish."
    foreach ($path in @($retailGate, $focusedGate, $profilePath)) {
        Assert-M2 (Test-Path -LiteralPath $path -PathType Leaf) "Missing M2 dependency: $path"
    }
    $profileSha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-M2 ($profileSha256 -eq $expectedProfileSha256) "Completion profile hash changed."

    # Fail fast before the 80-minute reproducibility build when the human
    # oracle/reliability evidence is absent or targets an older identity.
    $preSelection = Get-Json $selectionPath
    $preActivePack = [string]$preSelection.activePack
    Assert-M2 ($preActivePack -match '^bfme2-men-vslice/[0-9a-f]{64}$') "Selection does not name an immutable Men/Fords bundle."
    $preBundleSha256 = $preActivePack.Split('/')[-1]
    $preApproval = Get-Json $approvalPath
    $workingTreeIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-M2 ([string]$preApproval.schema -eq 'openbfme.m2-men-fords-oracle-approval' -and [int]$preApproval.schemaVersion -eq 0) "Oracle approval schema is invalid."
    Assert-M2 ([bool]$preApproval.approved) "Oracle approval is not final."
    Assert-M2 (-not [string]::IsNullOrWhiteSpace([string]$preApproval.createdAtUtc)) "Oracle approval has no threshold-freeze timestamp."
    Assert-M2 ([string]$preApproval.profileSha256 -eq $profileSha256 -and [string]$preApproval.bundleSha256 -eq $preBundleSha256) "Oracle approval targets another profile or bundle."
    Assert-M2 ([string]$preApproval.gitRevision -eq [string]$workingTreeIdentity.revision -and [string]$preApproval.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Oracle approval targets another Git or dirty-state identity."

    $retailArguments = @(
        "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", $retailGate,
        "-Install", $Install,
        "-IntegrationOwnerPublish"
    )
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) {
        $retailArguments += @("-GodotPath", $GodotPath)
    }
    & powershell.exe @retailArguments
    Assert-M2 ($LASTEXITCODE -eq 0) "The authoritative retail pipeline gate failed."

    $selection = Get-Json $selectionPath
    $activePack = [string]$selection.activePack
    Assert-M2 ($activePack -match '^bfme2-men-vslice/[0-9a-f]{64}$') "Selection does not name an immutable Men/Fords bundle."
    $bundleSha256 = $activePack.Split('/')[-1]
    Assert-Sha256 $bundleSha256 "Selected bundle"
    $packRoot = [IO.Path]::GetFullPath((Join-Path $contentRoot $activePack))
    Assert-M2 ($packRoot.StartsWith($contentRoot, [StringComparison]::OrdinalIgnoreCase)) "Selected bundle escaped the private content root."
    $pack = Get-Json (Join-Path $packRoot "pack.json")
    Assert-M2 ([string]$pack.id -eq $expectedPackId) "Selected bundle has the wrong pack ID."
    Assert-M2 ([bool]$pack.profile_build_complete) "Selected bundle is not a strict completion build."
    Assert-M2 (-not [bool]$pack.vertical_slice_complete) "Readiness marker changed before final oracle approval."
    Assert-M2 (-not (Test-Path -LiteralPath (Join-Path $repoRoot ".private\retail-work\packs\bfme2-men-vslice.building"))) "A failed completion-pack transaction remains."

    $focusedArguments = @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $focusedGate)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $focusedArguments += @("-GodotPath", $GodotPath) }
    [void](Invoke-ProofChecked $gate "focused_contracts" "powershell.exe" $focusedArguments '(?m)^M2_FOCUSED_GATE PASS runners=[1-9][0-9]* .*$' $forbiddenDiagnostics)

    $approval = Get-Json $approvalPath
    Assert-M2 ([string]$approval.schema -eq 'openbfme.m2-men-fords-oracle-approval') "Oracle approval schema is invalid."
    Assert-M2 ([int]$approval.schemaVersion -eq 0) "Oracle approval schema version is invalid."
    Assert-M2 ([bool]$approval.approved) "Oracle approval is not final."
    Assert-M2 ([string]$approval.profileSha256 -eq $profileSha256) "Oracle approval targets another profile."
    Assert-M2 ([string]$approval.bundleSha256 -eq $bundleSha256) "Oracle approval targets another bundle."
    Assert-M2 ([string]$approval.gitRevision -eq [string]$workingTreeIdentity.revision) "Oracle approval targets another Git revision."
    Assert-M2 ([string]$approval.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Oracle approval targets another dirty-state digest."
    Assert-M2 ([int]$approval.unresolvedSeverity0 -eq 0 -and [int]$approval.unresolvedSeverity1 -eq 0) "Oracle approval retains high-severity differences."
    Assert-M2 (-not [string]::IsNullOrWhiteSpace([string]$approval.approvedBy)) "Oracle approval has no integration owner."
    $captureManifestPath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $approvalPath) ([string]$approval.captureManifest)))
    $oracleRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private\retail-work\oracle"))
    Assert-M2 ($captureManifestPath.StartsWith($oracleRoot, [StringComparison]::OrdinalIgnoreCase)) "Capture manifest escaped the private oracle root."
    $captureManifestSha256 = (Get-FileHash -LiteralPath $captureManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-M2 ($captureManifestSha256 -eq [string]$approval.captureManifestSha256) "Capture manifest hash disagrees with approval."
    $captureManifest = Get-Json $captureManifestPath
    Assert-M2 ([string]$captureManifest.schema -eq 'openbfme.m2-men-fords-oracle-captures' -and [int]$captureManifest.schemaVersion -eq 1) "Capture manifest schema is invalid."
    Assert-M2 ([string]$captureManifest.profileSha256 -eq $profileSha256) "Capture manifest targets another profile."
    Assert-M2 ([string]$captureManifest.bundleSha256 -eq $bundleSha256) "Capture manifest targets another bundle."
    Assert-M2 ([string]$captureManifest.gitRevision -eq [string]$workingTreeIdentity.revision -and [string]$captureManifest.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Capture manifest targets another source identity."
    $captures = @($captureManifest.captures)
    $actualCaptureIds = @($captures | ForEach-Object { [string]$_.id })
    Assert-M2 ($captures.Count -eq $requiredCaptureIds.Count) "Capture manifest does not contain the exact required capture count."
    Assert-M2 (@($actualCaptureIds | Select-Object -Unique).Count -eq $actualCaptureIds.Count) "Capture manifest contains duplicate IDs."
    Assert-M2 (@(Compare-Object $requiredCaptureIds $actualCaptureIds).Count -eq 0) "Capture manifest does not contain the exact required capture IDs."
    Assert-M2 (@($captures | Where-Object { -not [bool]$_.approved }).Count -eq 0) "Capture manifest contains an unapproved row."
    foreach ($capture in $captures) {
        Assert-Sha256 ([string]$capture.retailSha256) "Retail capture $([string]$capture.id)"
        Assert-Sha256 ([string]$capture.godotSha256) "Godot capture $([string]$capture.id)"
        Assert-M2 ([string]$capture.profileSha256 -eq $profileSha256 -and [string]$capture.bundleSha256 -eq $bundleSha256) "Capture $([string]$capture.id) targets another pack identity."
        Assert-M2 ([string]$capture.gitRevision -eq [string]$workingTreeIdentity.revision -and [string]$capture.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Capture $([string]$capture.id) targets another source identity."
        Assert-M2 (-not [string]::IsNullOrWhiteSpace([string]$capture.viewport)) "Capture $([string]$capture.id) has no viewport."
        Assert-M2 ($null -ne $capture.notes) "Capture $([string]$capture.id) has no notes field."
        Assert-M2 ([int]$capture.unresolvedSeverity0 -eq 0 -and [int]$capture.unresolvedSeverity1 -eq 0) "Capture $([string]$capture.id) retains a severity-0/1 difference."
        Assert-M2 (-not [string]::IsNullOrWhiteSpace([string]$capture.approvedBy) -and -not [string]::IsNullOrWhiteSpace([string]$capture.approvedAtUtc)) "Capture $([string]$capture.id) lacks reviewer evidence."
        foreach ($side in @('retail', 'godot')) {
            Assert-M2 (-not [string]::IsNullOrWhiteSpace([string]$capture.("${side}CameraState"))) "Capture $([string]$capture.id) has no $side camera state."
            $property = "${side}Path"
            Assert-M2 ($null -ne $capture.PSObject.Properties[$property] -and -not [string]::IsNullOrWhiteSpace([string]$capture.$property)) "Capture $([string]$capture.id) has no $property."
            $imagePath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $captureManifestPath) ([string]$capture.$property)))
            Assert-M2 ($imagePath.StartsWith($oracleRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $imagePath -PathType Leaf)) "Capture $([string]$capture.id) $side image escaped the oracle root or is missing."
            $imageSha256 = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-M2 ($imageSha256 -eq [string]$capture.("${side}Sha256")) "Capture $([string]$capture.id) $side image hash changed."
        }
    }

    $reliabilityPath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $approvalPath) ([string]$approval.reliabilityEvidence)))
    Assert-M2 ($reliabilityPath.StartsWith($oracleRoot, [StringComparison]::OrdinalIgnoreCase)) "Reliability evidence escaped the private oracle root."
    $reliabilitySha256 = (Get-FileHash -LiteralPath $reliabilityPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-M2 ($reliabilitySha256 -eq [string]$approval.reliabilityEvidenceSha256) "Reliability evidence hash disagrees with approval."
    $reliability = Get-Json $reliabilityPath
    Assert-M2 ([string]$reliability.schema -eq 'openbfme.m2-men-fords-reliability' -and [int]$reliability.schemaVersion -eq 1) "Reliability evidence schema is invalid."
    Assert-M2 ([string]$reliability.profileSha256 -eq $profileSha256 -and [string]$reliability.bundleSha256 -eq $bundleSha256) "Reliability evidence targets another pack identity."
    Assert-M2 ([string]$reliability.gitRevision -eq [string]$workingTreeIdentity.revision -and [string]$reliability.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Reliability evidence targets another source identity."
    Assert-M2 ([int]$reliability.diagnosticCount -eq 0) "Reliability run recorded a forbidden diagnostic."
    Assert-M2 ([string]$reliability.thresholdsFrozenAtUtc -eq [string]$approval.createdAtUtc) "Reliability evidence was not bound to the pre-frozen approval."
    $thresholds = $approval.performanceThresholds
    Assert-M2 ($null -ne $thresholds) "Oracle approval has no frozen performance thresholds."
    Assert-M2 ([double]$thresholds.minimumAverageFps -gt 0.0 -and [double]$thresholds.minimumOnePercentLowFps -gt 0.0 -and [long]$thresholds.maximumPeakMemoryBytes -gt 0 -and [long]$thresholds.maximumMemoryGrowthBytes -ge 0 -and [long]$thresholds.maximumLateWindowMemoryGrowthBytes -ge 0 -and [long]$thresholds.maximumLateWindowMemoryGrowthBytes -le [long]$thresholds.maximumMemoryGrowthBytes) "Oracle approval performance thresholds are invalid."
    $soak = $reliability.liveSoak
    Assert-M2MatchLifecycleEvidence -Lifecycle $reliability.matchLifecycle -ExpectedProfileSha256 $profileSha256 -ExpectedBundleSha256 $bundleSha256 -ExpectedGitRevision ([string]$workingTreeIdentity.revision) -ExpectedDirtyStateDigest ([string]$workingTreeIdentity.dirtyStateDigest)
    Assert-M2ReliabilitySoakEvidence -Soak $soak -MinimumDurationSeconds 1800 -MaximumMemoryGrowthBytes ([long]$thresholds.maximumMemoryGrowthBytes) -MaximumLateWindowMemoryGrowthBytes ([long]$thresholds.maximumLateWindowMemoryGrowthBytes)
    $restarts = @($reliability.restarts)
    Assert-M2 ($restarts.Count -eq 3 -and @($restarts | ForEach-Object { [string]$_.signature } | Select-Object -Unique).Count -eq 1) "Three clean restarts do not share one deterministic signature."
    Assert-M2 (@($restarts | Where-Object { [string]$_.bundleSha256 -ne $bundleSha256 }).Count -eq 0) "A clean restart mounted another bundle."
    $reliabilityThresholds = $reliability.performanceThresholds
    Assert-M2 ([double]$reliabilityThresholds.minimumAverageFps -eq [double]$thresholds.minimumAverageFps -and [double]$reliabilityThresholds.minimumOnePercentLowFps -eq [double]$thresholds.minimumOnePercentLowFps -and [long]$reliabilityThresholds.maximumPeakMemoryBytes -eq [long]$thresholds.maximumPeakMemoryBytes -and [long]$reliabilityThresholds.maximumMemoryGrowthBytes -eq [long]$thresholds.maximumMemoryGrowthBytes -and [long]$reliabilityThresholds.maximumLateWindowMemoryGrowthBytes -eq [long]$thresholds.maximumLateWindowMemoryGrowthBytes) "Reliability threshold snapshot changed."
    Assert-M2 ([double]$soak.averageFps -ge [double]$thresholds.minimumAverageFps) "Live soak average FPS missed the frozen threshold."
    Assert-M2 ([double]$soak.onePercentLowFps -ge [double]$thresholds.minimumOnePercentLowFps) "Live soak one-percent-low FPS missed the frozen threshold."
    Assert-M2 ([long]$soak.peakMemoryBytes -le [long]$thresholds.maximumPeakMemoryBytes) "Live soak peak memory exceeded the frozen threshold."

    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-M2 ([string]$finalIdentity.revision -eq [string]$workingTreeIdentity.revision -and [string]$finalIdentity.dirtyStateDigest -eq [string]$workingTreeIdentity.dirtyStateDigest) "Working-tree identity changed during final acceptance."

    Write-Host "$gate PASS profile_sha256=$profileSha256 bundle_sha256=$bundleSha256 dirty_state_digest=$([string]$workingTreeIdentity.dirtyStateDigest) captures=$($captures.Count) soak_seconds=$([double]$soak.actualDurationSeconds)"
    exit 0
}
catch {
    Write-Error "$gate FAIL $($_.Exception.Message)"
    exit 1
}
