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
  SemVer without leading v (default 0.2.0-playtest.1)
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*)?$')]
    [string]$Version = '0.2.0-playtest.1',

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

Write-Host ('== [1] push main ==')
git push origin main
if ($LASTEXITCODE -ne 0) { throw 'git push origin main failed' }

Write-Host ('== [2] environments ==')
gh api -X PUT "repos/$Repo/environments/release-signing" | Out-Null
gh api -X PUT "repos/$Repo/environments/production-release" | Out-Null

Write-Host ('== [3] signing secret ==')
Get-Content -LiteralPath $priv -Raw | gh secret set OPENBFME_RELEASE_SIGNING_KEY -R $Repo --env release-signing
if ($LASTEXITCODE -ne 0) { throw 'gh secret set failed' }

Write-Host ('== [4] rulesets (best-effort; non-fatal) ==')
function Ensure-Ruleset {
    param(
        [string]$Name,
        [string]$Target,
        [string[]]$Includes
    )
    try {
        $ids = gh api "repos/$Repo/rulesets" --jq ".[] | select(.name==`"$Name`") | .id" 2>$null
        if ($ids) {
            Write-Host ("  ruleset already exists: {0}" -f $Name)
            return
        }
    } catch {
        # listing can fail on some plans; try create anyway
    }
    $bodyObj = [ordered]@{
        name        = $Name
        target      = $Target
        enforcement = 'active'
        conditions  = @{
            ref_name = @{
                include = @($Includes)
                exclude = @()
            }
        }
        rules = @(
            @{ type = 'deletion' },
            @{ type = 'non_fast_forward' }
        )
    }
    $body = $bodyObj | ConvertTo-Json -Depth 8 -Compress
    $tmp = Join-Path $env:TEMP ('openbfme-ruleset-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        [IO.File]::WriteAllText($tmp, $body, [Text.UTF8Encoding]::new($false))
        $createOut = gh api -X POST "repos/$Repo/rulesets" --input $tmp 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            Write-Host ("  WARN ruleset {0} not created: {1}" -f $Name, $createOut.Trim())
            return
        }
        Write-Host ("  created ruleset {0}" -f $Name)
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
    }
}
Ensure-Ruleset -Name 'main-protection' -Target 'branch' -Includes @('refs/heads/main')
Ensure-Ruleset -Name 'v-tag-protection' -Target 'tag' -Includes @('refs/tags/v*')

Write-Host ('== [5] dispatch windows release build ==')
gh workflow run 'windows release' -R $Repo `
    -f ("version={0}" -f $Version) `
    -f 'channel=playtest' `
    -f 'run_acceptance=false'
if ($LASTEXITCODE -ne 0) { throw 'workflow dispatch failed' }

Start-Sleep -Seconds 10
$runId = gh run list -R $Repo --workflow=release.yml --limit 1 --json databaseId --jq '.[0].databaseId'
if ([string]::IsNullOrWhiteSpace([string]$runId)) { throw 'Could not resolve workflow run id' }
Write-Host ("  run id={0}; waiting up to {1} min" -f $runId, $WaitMinutes)
gh run watch $runId -R $Repo --exit-status
if ($LASTEXITCODE -ne 0) { throw ("Workflow run {0} failed" -f $runId) }

Write-Host ('== [6] download unsigned artifact ==')
$work = Join-Path $env:TEMP ('openbfme-rc-' + $Version)
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null
$artifactName = 'openbfme-{0}-windows-unsigned' -f $Version
gh run download $runId -R $Repo -n $artifactName -D $work
if ($LASTEXITCODE -ne 0) { throw 'artifact download failed' }

$dist = Get-ChildItem -LiteralPath $work -Directory | Select-Object -First 1
if ($null -eq $dist) {
    $distPath = $work
} else {
    $distPath = $dist.FullName
}
$manifest = Join-Path $distPath 'release-manifest.json'
$gameZip = Join-Path $distPath ('OpenBFME-{0}-windows-x64.zip' -f $Version)
$launcherZip = Join-Path $distPath ('OpenBFME-Launcher-{0}-windows-x64.zip' -f $Version)
foreach ($p in @($manifest, $gameZip, $launcherZip)) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) {
        Write-Host 'Artifact tree:'
        Get-ChildItem -LiteralPath $work -Recurse -File | ForEach-Object { Write-Host ('  ' + $_.FullName) }
        throw ("Missing package after download: {0}" -f $p)
    }
}

Write-Host ('== [7] sign manifest ==')
$env:OPENBFME_RELEASE_SIGNING_KEY = Get-Content -LiteralPath $priv -Raw
$sig = Join-Path $distPath 'release-manifest.json.sig'
try {
    $signScript = Join-Path $repoRoot 'tools\release\Sign-ReleaseManifest.ps1'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $signScript -Manifest $manifest -Output $sig
    if ($LASTEXITCODE -ne 0) { throw 'Sign-ReleaseManifest failed' }
} finally {
    Remove-Item Env:OPENBFME_RELEASE_SIGNING_KEY -ErrorAction SilentlyContinue
}

$sums = Join-Path $distPath 'SHA256SUMS.txt'
Get-ChildItem -LiteralPath $distPath -File |
    Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    Sort-Object Name |
    ForEach-Object {
        $hash = ($_ | Get-FileHash -Algorithm SHA256).Hash.ToLowerInvariant()
        '{0}  {1}' -f $hash, $_.Name
    } |
    Set-Content -LiteralPath $sums -Encoding ascii

Write-Host ('== [8] create GitHub prerelease {0} ==' -f $tag)
$sha = (git rev-parse HEAD).Trim()
# Target the commit the workflow built from when possible (dispatched from main tip after push).
gh release create $tag `
    $gameZip `
    $launcherZip `
    $manifest `
    $sig `
    $sums `
    -R $Repo `
    --title ("OpenBFME {0}" -f $Version) `
    --notes 'Playtest RC for launcher Update feed (signed release-manifest). Channel: playtest.' `
    --prerelease `
    --target $sha
if ($LASTEXITCODE -ne 0) { throw 'gh release create failed' }

Write-Host ''
Write-Host ('RELEASE_CANDIDATE_READY {0}' -f $tag)
Write-Host ('  Private key remains at: {0}' -f $priv)
Write-Host '  Launcher: --channel playtest then Check for update'
Write-Host ('  Verify: gh release view {0} -R {1}' -f $tag, $Repo)
