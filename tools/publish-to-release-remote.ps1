<#
.SYNOPSIS
    Prepare and (only when explicitly told to) perform a publish to the release
    repository.

.DESCRIPTION
    Dry-run by default. Nothing is pushed unless -Execute is passed, and even
    then every gate below must pass first:

      1. The release target resolves (tools/release_source.py) - no literal is
         baked in here, so the target can move without editing this script.
      2. The working tree is clean. Publishing a dirty tree publishes whatever
         happened to be lying around.
      3. tools/export-scan.ps1 passes BOTH phases. This is the legal gate: no
         retail-derived bytes and no developer-machine specifics. It is not
         skippable, and it is the reason this script exists rather than a bare
         `git push`.
      4. The target's existing history is fetched and reconciled explicitly.
         The target is NOT empty - it has an auto-init commit - so a naive push
         is rejected as non-fast-forward. This script never force-pushes; it
         reports what reconciliation is required and leaves the choice to a
         human.

    Authentication is delegated entirely to the `gh` CLI. This script never
    reads, logs, stores, or accepts a token.

.PARAMETER Branch
    Local branch to publish. Defaults to the current branch.

.PARAMETER Remote
    Git remote name for the release target. Default: github.

.PARAMETER Execute
    Actually push. Without this the script only reports what it would do.
#>
[CmdletBinding()]
param(
    [string]$Branch = "",
    [string]$Remote = "github",
    [switch]$Execute
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$failures = [Collections.Generic.List[string]]::new()

function Write-Step { param([string]$Text) Write-Host "PUBLISH_CHECK $Text" }

# --- 1. Resolve the target (never hardcoded) --------------------------------

$python = $env:OPENBFME_IMPORTER_PYTHON
if ([string]::IsNullOrWhiteSpace($python)) {
    $candidate = Join-Path $repoRoot "workspace\retail-work\tools\python-3.12-env\Scripts\python.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $python = $candidate }
    else { $python = "python" }
}

$sourceLine = & $python (Join-Path $repoRoot "tools\release_source.py") 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "PUBLISH_ABORT The release target is not configured."
    Write-Host $sourceLine
    exit 1
}
$repository = ([regex]::Match(($sourceLine -join "`n"), 'repository=(\S+)')).Groups[1].Value
if ([string]::IsNullOrWhiteSpace($repository)) {
    Write-Host "PUBLISH_ABORT Could not parse the resolved release target."
    exit 1
}
Write-Step "target=$repository"

# The configured remote must actually point at the resolved target, or we would
# publish somewhere nobody reviewed.
$remoteUrl = (& git -C $repoRoot remote get-url $Remote 2>$null)
if ($LASTEXITCODE -ne 0) {
    $failures.Add("Remote '$Remote' does not exist. Add it pointing at $repository.")
}
elseif ($remoteUrl -notmatch [regex]::Escape($repository)) {
    $failures.Add("Remote '$Remote' is $remoteUrl but the configured target is $repository.")
}
else {
    Write-Step "remote=$Remote url=$remoteUrl"
}

# --- 2. Clean working tree ---------------------------------------------------

if ([string]::IsNullOrWhiteSpace($Branch)) {
    $Branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD).Trim()
}
Write-Step "branch=$Branch"

$dirty = @(& git -C $repoRoot status --porcelain)
if ($dirty.Count -gt 0) {
    $failures.Add("Working tree is not clean ($($dirty.Count) entries). Commit or stash first.")
}
else {
    Write-Step "working_tree=clean"
}

# --- 3. Release firewall (not skippable) -------------------------------------

& (Join-Path $PSScriptRoot "export-scan.ps1")
if ($LASTEXITCODE -ne 0) {
    $failures.Add("export-scan.ps1 FAILED. Retail bytes or developer-machine specifics are still present. This is a legal gate; do not bypass it.")
}
else {
    Write-Step "release_firewall=pass"
}

# --- 3b. Git LFS payload -----------------------------------------------------
#
# Most of this repository's asset weight is in LFS, not in git objects. A push
# that omits the LFS objects produces a repo full of dangling pointers that
# looks fine in the file list and fails on checkout, so surface the payload
# explicitly rather than letting it be a surprise.

$lfsFiles = @(& git -C $repoRoot lfs ls-files 2>$null)
if ($LASTEXITCODE -ne 0) {
    Write-Step "lfs=unavailable (git-lfs not installed; required for this repo)"
    $failures.Add("git-lfs is not available, but this repository stores assets in LFS. Install it before publishing.")
}
elseif ($lfsFiles.Count -gt 0) {
    Write-Step "lfs_objects=$($lfsFiles.Count)"
    Write-Host "PUBLISH_NOTE LFS objects must reach the target too ('git lfs push --all $Remote')."
    Write-Host "PUBLISH_NOTE GitHub bills LFS storage and bandwidth per account; a large payload can exhaust the monthly quota after only a few clones."
}

# --- 4. Reconcile with existing target history -------------------------------

$reconciliation = "unknown"
if ($true) {
    & git -C $repoRoot fetch --quiet $Remote 2>$null
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("Could not fetch '$Remote'. Check 'gh auth status' and network access.")
    }
    else {
        $targetRef = "$Remote/main"
        $targetHead = (& git -C $repoRoot rev-parse --verify --quiet "$targetRef")
        if ([string]::IsNullOrWhiteSpace($targetHead)) {
            $reconciliation = "target-branch-absent"
            Write-Step "target_main=absent (first publish creates it)"
        }
        else {
            $mergeBase = (& git -C $repoRoot merge-base $Branch $targetRef 2>$null)
            if ([string]::IsNullOrWhiteSpace($mergeBase)) {
                $reconciliation = "unrelated-histories"
                $behind = (& git -C $repoRoot rev-list --count $targetRef).Trim()
                Write-Step "target_main=$($targetHead.Substring(0,8)) commits=$behind reconciliation=unrelated-histories"
                $failures.Add(
                    "Local '$Branch' and '$targetRef' have NO common ancestor, so a push is rejected as non-fast-forward. " +
                    "Two options, both for a human to choose: " +
                    "(a) non-destructive - 'git merge --allow-unrelated-histories $targetRef' locally, resolve, then publish; " +
                    "(b) destructive - force-push, which DISCARDS the $behind commit(s) already on the target. " +
                    "This script will never force-push."
                )
            }
            elseif ($mergeBase.Trim() -eq $targetHead.Trim()) {
                $reconciliation = "fast-forward"
                Write-Step "reconciliation=fast-forward"
            }
            else {
                $reconciliation = "diverged"
                $failures.Add("'$Branch' and '$targetRef' have diverged. Merge or rebase locally before publishing.")
            }
        }
    }
}

# --- Verdict -----------------------------------------------------------------

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "PUBLISH_BLOCKED $failure" }
    Write-Host "PUBLISH_RESULT BLOCKED blockers=$($failures.Count) target=$repository branch=$Branch reconciliation=$reconciliation"
    exit 1
}

if (-not $Execute) {
    Write-Host "PUBLISH_RESULT DRY_RUN_OK target=$repository branch=$Branch reconciliation=$reconciliation"
    Write-Host "PUBLISH_NOTE Re-run with -Execute to push. Nothing has been sent."
    exit 0
}

Write-Host "PUBLISH_PUSH pushing $Branch -> ${Remote}:main"
& git -C $repoRoot push $Remote "${Branch}:main"
if ($LASTEXITCODE -ne 0) {
    Write-Host "PUBLISH_RESULT FAILED push rejected"
    exit 1
}
Write-Host "PUBLISH_RESULT PUBLISHED target=$repository branch=$Branch"
exit 0
