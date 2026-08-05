#Requires -Version 5.1
<#
.SYNOPSIS
  Start an OpenBFME playtest release cycle (tag + optional push).

.DESCRIPTION
  Operator helper for the cadence documented in docs/RELEASE_CYCLE.md.
  Does not build packages itself — the windows release GitHub Actions workflow
  does that when it sees a v* tag (or a manual workflow_dispatch).

  The hosted workflow requires a GitHub-verified signed annotated tag for push
  events. Therefore -Push requires -Signed (or an explicit -AllowUnsignedLocal
  for local-only experimentation — never for production publish).

.PARAMETER Version
  SemVer without leading v, e.g. 0.1.0-playtest.1

.PARAMETER Message
  Annotated tag message. Defaults to "Playtest <version>".

.PARAMETER Push
  Push the tag to origin (triggers CI when origin is GitHub). Requires -Signed.

.PARAMETER Signed
  Use git tag -s (GPG/SSH signing). Required when -Push is set.

.PARAMETER AllowUnsignedLocal
  Permit an unsigned annotated tag for local-only use. Incompatible with -Push.

.PARAMETER SkipCleanCheck
  Skip the clean working tree / on-main / up-to-date checks (not recommended).

.PARAMETER WhatIf
  Print the plan without creating a tag.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$')]
    [string]$Version,

    [string]$Message = "",

    [switch]$Push,

    [switch]$Signed,

    [switch]$AllowUnsignedLocal,

    [switch]$SkipCleanCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

$tag = "v$Version"
if ([string]::IsNullOrWhiteSpace($Message)) {
    $Message = "Playtest $Version"
}

if ($Push -and -not $Signed) {
    throw "Refusing -Push without -Signed. The windows release workflow requires a GitHub-verified signed annotated tag. For local-only unsigned tags use neither -Push nor -Signed, and pass -AllowUnsignedLocal."
}

if ($AllowUnsignedLocal -and ($Push -or $Signed)) {
    throw "-AllowUnsignedLocal is only for local unsigned tags (no -Push, no -Signed)."
}

if (-not $Signed -and -not $AllowUnsignedLocal -and -not $WhatIfPreference) {
    throw "Pass -Signed for a real release tag, or -AllowUnsignedLocal for a local-only unsigned tag, or -WhatIf to preview."
}

$branch = (git rev-parse --abbrev-ref HEAD).Trim()
$head = (git rev-parse HEAD).Trim()
$sourcePath = Join-Path $repoRoot "config\release-source.json"
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "config/release-source.json is missing."
}
$source = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
$configuredRepo = [string]$source.repository

Write-Host "Repository target: $configuredRepo"
Write-Host "Current branch:    $branch"
Write-Host "HEAD:              $head"
Write-Host "Tag to create:     $tag"
Write-Host "Message:           $Message"
Write-Host "Push:              $Push"
Write-Host "Signed tag:        $Signed"

# origin must match release-source.json
$originUrl = (git remote get-url origin 2>$null)
if (-not $originUrl) {
    throw "No 'origin' remote configured."
}
$originNormalized = $originUrl.Trim() -replace '\.git$', '' -replace 'https://github.com/', '' -replace 'git@github.com:', ''
if ($originNormalized -ne $configuredRepo) {
    throw "origin remote resolves to '$originNormalized' but config/release-source.json targets '$configuredRepo'."
}
Write-Host "origin:            $originUrl"

if (-not $SkipCleanCheck) {
    $cleanProblems = @()
    if ($branch -ne "main") {
        $cleanProblems += "Not on main (current branch: $branch)."
    }
    $status = git status --porcelain
    if ($status) {
        $cleanProblems += "Working tree is dirty. Uncommitted work is not in the tag."
    }
    git fetch origin main --quiet 2>$null
    $originMain = (git rev-parse origin/main 2>$null)
    if ($originMain -and ($originMain.Trim() -ne $head)) {
        $cleanProblems += "HEAD ($head) != origin/main ($($originMain.Trim()))."
    }
    if ($cleanProblems.Count -gt 0) {
        if ($WhatIfPreference) {
            foreach ($p in $cleanProblems) {
                Write-Warning ("[WhatIf] clean-check would fail: " + $p)
            }
        } else {
            throw (($cleanProblems -join " ") + " Checkout/pull/stash first, or pass -SkipCleanCheck.")
        }
    } else {
        Write-Host "Clean checks:      main, clean tree, HEAD == origin/main"
    }
}

$existing = git tag -l $tag
if ($existing) {
    throw "Tag $tag already exists locally. Choose a new SemVer playtest number."
}

# Remote tag collision
$remoteTag = git ls-remote --tags origin "refs/tags/$tag" 2>$null
if ($remoteTag) {
    throw "Tag $tag already exists on origin. Choose a new SemVer playtest number."
}

if ($WhatIfPreference) {
    Write-Host "[WhatIf] Would create $(if ($Signed) { 'signed' } else { 'unsigned' }) annotated tag $tag and" $(if ($Push) { "push to origin." } else { "leave it local." })
    Write-Host "[WhatIf] After push, watch Actions → windows release."
    return
}

if ($PSCmdlet.ShouldProcess($tag, "Create annotated git tag")) {
    if ($Signed) {
        git tag -s -a $tag -m $Message
    } else {
        git tag -a $tag -m $Message
        Write-Warning "Created an unsigned annotated tag (local only). Production publish requires -Signed -Push."
    }
    if ($LASTEXITCODE -ne 0) { throw "git tag failed." }
    Write-Host "Created $tag"
}

if ($Push) {
    if ($PSCmdlet.ShouldProcess("origin", "Push tag $tag")) {
        git push origin $tag
        if ($LASTEXITCODE -ne 0) { throw "git push origin $tag failed." }
        Write-Host "Pushed $tag → origin"
        Write-Host "Open: https://github.com/$configuredRepo/actions"
    }
} else {
    Write-Host "Tag is local only. Push when ready:"
    Write-Host "  git push origin $tag"
}

Write-Host ""
Write-Host "Next: wait for windows release → sign → publish (tag builds)."
Write-Host "Then: OpenBFME.Launcher.exe --channel playtest  →  Check for update"
