#Requires -Version 5.1
<#
.SYNOPSIS
  Operator one-shot: land a launcher-ready playtest RC on GitHub.

.DESCRIPTION
  Assumes:
  - main contains the rotated public signing pins (commit after key rotation)
  - Private key exists at %LOCALAPPDATA%\OpenBFME-release-key\openbfme-release-signing-private.pem
  - gh is authenticated with repo + workflow scopes

  Steps:
  1. Push main
  2. Create release-signing + production-release environments
  3. Upload OPENBFME_RELEASE_SIGNING_KEY to release-signing
  4. Create lightweight main + v* tag rulesets (no force-push/delete)
  5. workflow_dispatch windows release (version, channel=playtest, no acceptance)
  6. Wait for build, download unsigned artifact
  7. Sign release-manifest.json with the private key
  8. Create prerelease tag + assets the launcher Update feed can consume

.PARAMETER Version
  SemVer without leading v (default 0.1.0-playtest.1)
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*)?$')]
    [string]$Version = '0.1.0-playtest.1',

    [string]$Repo = 'Open-BFME/openbfme-godot',

    [int]$WaitMinutes = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $repoRoot

$tag = "v$Version"
$priv = Join-Path $env:LOCALAPPDATA 'OpenBFME-release-key\openbfme-release-signing-private.pem'
if (-not (Test-Path -LiteralPath $priv -PathType Leaf)) {
    throw "Missing private key at $priv. Generate with openssl genpkey (see docs/BUILD_AND_RELEASE.md)."
}

Write-Host "== 1) push main =="
git push origin main

Write-Host "== 2) environments =="
gh api -X PUT "repos/$Repo/environments/release-signing" | Out-Null
gh api -X PUT "repos/$Repo/environments/production-release" | Out-Null

Write-Host "== 3) signing secret =="
Get-Content -LiteralPath $priv -Raw | gh secret set OPENBFME_RELEASE_SIGNING_KEY -R $Repo --env release-signing

Write-Host "== 4) rulesets (idempotent-ish; ignore already_exists) =="
function Ensure-Ruleset([string]$name, [string]$target, [string[]]$includes) {
    $existing = gh api "repos/$Repo/rulesets" --jq ".[] | select(.name==`"$name`") | .id" 2>$null
    if ($existing) {
        Write-Host "  ruleset $name already id=$existing"
        return
    }
    $body = @{
        name = $name
        target = $target
        enforcement = 'active'
        conditions = @{ ref_name = @{ include = $includes; exclude = @() } }
        rules = @(
            @{ type = 'deletion' },
            @{ type = 'non_fast_forward' }
        )
    } | ConvertTo-Json -Depth 8 -Compress
    $body | gh api -X POST "repos/$Repo/rulesets" --input - | Out-Null
    Write-Host "  created ruleset $name"
}
Ensure-Ruleset 'main-protection' 'branch' @('refs/heads/main')
Ensure-Ruleset 'v-tag-protection' 'tag' @('refs/tags/v*')

Write-Host "== 5) dispatch windows release build =="
gh workflow run 'windows release' -R $Repo `
    -f "version=$Version" `
    -f 'channel=playtest' `
    -f 'run_acceptance=false'

Start-Sleep -Seconds 8
$runId = gh run list -R $Repo --workflow=release.yml --limit 1 --json databaseId,status,url --jq '.[0].databaseId'
if (-not $runId) { throw 'Could not resolve workflow run id' }
Write-Host "  run id=$runId — waiting up to $WaitMinutes min"
gh run watch $runId -R $Repo --exit-status
if ($LASTEXITCODE -ne 0) { throw "Workflow run $runId failed" }

Write-Host "== 6) download unsigned artifact =="
$work = Join-Path $env:TEMP ("openbfme-rc-" + $Version)
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null
gh run download $runId -R $Repo -n "openbfme-$Version-windows-unsigned" -D $work
$dist = Get-ChildItem $work -Directory | Select-Object -First 1
if (-not $dist) {
    # artifact may unpack flat
    $distPath = $work
} else {
    $distPath = $dist.FullName
}
$manifest = Join-Path $distPath 'release-manifest.json'
$gameZip = Join-Path $distPath "OpenBFME-$Version-windows-x64.zip"
$launcherZip = Join-Path $distPath "OpenBFME-Launcher-$Version-windows-x64.zip"
foreach ($p in @($manifest, $gameZip, $launcherZip)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Missing package after download: $p" }
}

Write-Host "== 7) sign manifest =="
$env:OPENBFME_RELEASE_SIGNING_KEY = Get-Content -LiteralPath $priv -Raw
$sig = Join-Path $distPath 'release-manifest.json.sig'
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools\release\Sign-ReleaseManifest.ps1') `
        -Manifest $manifest -Output $sig
    if ($LASTEXITCODE -ne 0) { throw 'Sign-ReleaseManifest failed' }
} finally {
    Remove-Item Env:OPENBFME_RELEASE_SIGNING_KEY -ErrorAction SilentlyContinue
}

# Refresh SHA256SUMS to include .sig
$sums = Join-Path $distPath 'SHA256SUMS.txt'
Get-ChildItem $distPath -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    "$(($_ | Get-FileHash -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath $sums -Encoding ascii

Write-Host "== 8) create GitHub prerelease $tag =="
$sha = (git rev-parse HEAD).Trim()
# Point release at current main tip (must match packages if built from that SHA — workflow uses the commit of the dispatch)
gh release create $tag `
    $gameZip `
    $launcherZip `
    $manifest `
    $sig `
    $sums `
    -R $Repo `
    --title "OpenBFME $Version" `
    --notes "First launcher-ready playtest RC. Channel: playtest. Signed release-manifest for Update feed." `
    --prerelease `
    --target $sha

Write-Host ""
Write-Host "RELEASE_CANDIDATE_READY $tag"
Write-Host "  Private key remains at: $priv"
Write-Host "  Launcher: --channel playtest then Check for update"
Write-Host "  Verify: gh release view $tag -R $Repo"
