#Requires -Version 5.1
# Preflight for launcher branding + GitHub playtest release cycle.
# Does not create tags, push, or access signing secret values.
#
# Exit codes:
#   0 = LOCAL_BUILD_READY (and PUBLIC_RELEASE_READY when -CheckGitHub and controls exist)
#   1 = local machinery missing/broken
#   2 = local OK but public release control plane incomplete (only with -CheckGitHub)
[CmdletBinding()]
param(
    [switch]$CheckGitHub
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

$script:localFailed = 0
$script:publicFailed = 0
function Ok([string]$msg) { Write-Host ("  OK  " + $msg) -ForegroundColor Green }
function BadLocal([string]$msg) {
    Write-Host ("  FAIL " + $msg) -ForegroundColor Red
    $script:localFailed++
}
function BadPublic([string]$msg) {
    Write-Host ("  NEED " + $msg) -ForegroundColor Yellow
    $script:publicFailed++
}
function Info([string]$msg) { Write-Host ("  --  " + $msg) -ForegroundColor DarkGray }

Write-Host "OpenBFME launch / release readiness"
Write-Host ("Root: " + $repoRoot)
Write-Host ""

# --- Brand / launcher chrome ---
Write-Host "Brand / launcher chrome"
# Physical files. The WPF banner is linked from docs/assets (see csproj Link=),
# not duplicated under launcher/.../Assets/openbfme-banner.png.
$assets = @(
    "launcher\OpenBFME.Launcher\Assets\openbfme-mark.png",
    "launcher\OpenBFME.Launcher\Assets\launcher-hero-bg.png",
    "docs\assets\openbfme-readme-banner.png"
)
foreach ($rel in $assets) {
    $path = Join-Path $repoRoot $rel
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $len = (Get-Item -LiteralPath $path).Length
        if ($len -lt 1024) { BadLocal ($rel + " is too small (" + $len + " bytes)") }
        else { Ok ($rel + " (" + $len + " bytes)") }
    } else {
        BadLocal ("missing " + $rel)
    }
}
$linkedBanner = "docs\assets\openbfme-readme-banner.png"
$bannerLinkOk = $false
$csprojProbe = Join-Path $repoRoot "launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj"
if (Test-Path -LiteralPath $csprojProbe) {
    $csprojProbeText = Get-Content -LiteralPath $csprojProbe -Raw
    if ($csprojProbeText -match 'openbfme-readme-banner\.png' -and
        $csprojProbeText -match 'Link="Assets\\openbfme-banner\.png"') {
        $bannerLinkOk = $true
    }
}
if ($bannerLinkOk -and (Test-Path -LiteralPath (Join-Path $repoRoot $linkedBanner))) {
    Ok "launcher banner linked from $linkedBanner (pack URI Assets/openbfme-banner.png)"
} else {
    BadLocal "launcher banner link missing (csproj must Link docs/assets/openbfme-readme-banner.png as Assets\openbfme-banner.png)"
}

# --- Release machinery (tracked) ---
Write-Host ""
Write-Host "Release machinery (tracked)"
$required = @(
    "config\release-source.json",
    "docs\RELEASE_CYCLE.md",
    "docs\BUILD_AND_RELEASE.md",
    "docs\LAUNCHER_UX_AND_RELEASE_READINESS.md",
    ".github\workflows\release.yml",
    "tools\release\Start-PlaytestRelease.ps1",
    "tools\release\release-manifest-public.pem",
    "tools\release\New-ReleaseManifest.ps1",
    "tools\release\Sign-ReleaseManifest.ps1",
    "tools\release\Test-ReleaseArtifact.ps1",
    "tools\release_source.py",
    "launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj"
)
foreach ($rel in $required) {
    $path = Join-Path $repoRoot $rel
    if (Test-Path -LiteralPath $path) { Ok $rel }
    else { BadLocal ("missing " + $rel) }
}

# --- Release source config ---
Write-Host ""
Write-Host "Release source config"
$sourcePath = Join-Path $repoRoot "config\release-source.json"
$configuredRepo = $null
try {
    $source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace([string]$source.repository)) {
        BadLocal "config/release-source.json repository is empty"
    } else {
        $configuredRepo = [string]$source.repository
        Ok ("repository = " + $configuredRepo)
    }
    if ($source.retiredRepositories) {
        Ok ("retiredRepositories = " + ($source.retiredRepositories -join ", "))
    }
} catch {
    BadLocal ("config/release-source.json unreadable: " + $_.Exception.Message)
}

# Python resolver CLI (workflow must not use fragile python -c dict quotes)
Write-Host ""
Write-Host "Release source resolver (PowerShell-safe)"
$py = $null
foreach ($cand in @("python", "py")) {
    $cmd = Get-Command $cand -ErrorAction SilentlyContinue
    if ($cmd) { $py = $cmd.Source; break }
}
if (-not $py) {
    BadLocal "python not on PATH - cannot verify tools.release_source --json"
} else {
    try {
        $jsonOut = & $py -m tools.release_source --json 2>&1
        if ($LASTEXITCODE -ne 0) {
            BadLocal ("tools.release_source --json failed: " + ($jsonOut -join " "))
        } else {
            $resolved = ($jsonOut -join "`n") | ConvertFrom-Json
            if ([string]::IsNullOrWhiteSpace([string]$resolved.repository)) {
                BadLocal "tools.release_source --json returned empty repository"
            } else {
                Ok ("resolve --json repository=" + $resolved.repository + " host=" + $resolved.host)
            }
        }
    } catch {
        BadLocal ("tools.release_source --json error: " + $_.Exception.Message)
    }
}

# Workflow must not use the broken inline -c pattern
Write-Host ""
Write-Host "Workflow release-source invocation"
$wfPath = Join-Path $repoRoot ".github\workflows\release.yml"
$wfText = Get-Content -LiteralPath $wfPath -Raw
if ($wfText -match 'python -c .*json\.dumps') {
    BadLocal "release.yml still uses python -c with json.dumps (PowerShell quote bug)"
} elseif ($wfText -match 'tools\.release_source --json' -or $wfText -match 'tools/release_source\.py') {
    Ok "release.yml resolves source via checked-in CLI (not fragile -c quotes)"
} else {
    BadLocal "release.yml does not call tools.release_source --json"
}

# --- Launcher project ---
Write-Host ""
Write-Host "Launcher project"
$csproj = Join-Path $repoRoot "launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj"
$projText = Get-Content -LiteralPath $csproj -Raw
if ($projText -match "openbfme-banner\.png") { Ok "csproj embeds openbfme-banner.png" }
else { BadLocal "csproj missing openbfme-banner Resource" }
if ($projText -match "launcher-hero-bg\.png") { Ok "csproj embeds launcher-hero-bg.png" }
else { BadLocal "csproj missing launcher-hero-bg Resource" }
if ($projText -match "release-source\.json") { Ok "csproj embeds release-source.json" }
else { BadLocal "csproj missing release-source embed" }

# --- Git tags (local) ---
Write-Host ""
Write-Host "Git tags (local)"
$tags = @(git tag -l "v*" 2>$null)
if ($tags.Count -eq 0) {
    Info "no local v* tags yet - first playtest cut still pending"
} else {
    $tail = ($tags | Select-Object -Last 5) -join ", "
    Ok ("local v* tags: " + $tail)
}

# --- GitHub public control plane ---
if ($CheckGitHub) {
    Write-Host ""
    Write-Host "GitHub public release control plane"
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        BadPublic "gh CLI not on PATH"
    } else {
        # Prefer GH_REPO so older gh builds that lack `gh -R` / `--repo` still work.
        if ($configuredRepo) {
            $env:GH_REPO = $configuredRepo
        }

        try {
            $repoView = & gh repo view --json nameWithOwner -q .nameWithOwner 2>$null
            if ($LASTEXITCODE -eq 0 -and $repoView) {
                Ok ("gh repo = " + $repoView)
                if ($configuredRepo -and ($repoView -ne $configuredRepo)) {
                    BadPublic ("gh repo $repoView != config repository $configuredRepo")
                }
            } else {
                BadPublic "gh repo view failed (auth or network)"
            }
        } catch {
            BadPublic ("gh repo view: " + $_.Exception.Message)
        }

        # Environments
        try {
            $envJson = & gh api "repos/$configuredRepo/environments" 2>$null
            if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace([string]$envJson)) {
                BadPublic "could not list environments"
            } else {
                $envObj = $envJson | ConvertFrom-Json
                $names = @()
                if ($envObj.environments) {
                    $names = @($envObj.environments | ForEach-Object { $_.name })
                }
                if ($names -contains "release-signing") {
                    Ok "environment release-signing exists"
                } else {
                    BadPublic "missing environment release-signing (needs OPENBFME_RELEASE_SIGNING_KEY)"
                }
                if ($names -contains "production-release") {
                    Ok "environment production-release exists"
                } else {
                    BadPublic "missing environment production-release (publish gate)"
                }
                if ($names.Count -eq 0) {
                    Info "environments total_count=0"
                }
            }
        } catch {
            BadPublic ("environments API: " + $_.Exception.Message)
        }

        # Rulesets / branch protection
        try {
            $rulesJson = & gh api "repos/$configuredRepo/rulesets" 2>$null
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace([string]$rulesJson)) {
                $rules = $rulesJson | ConvertFrom-Json
                if (@($rules).Count -eq 0) {
                    BadPublic "no repository rulesets (need main PR ruleset + v* tag ruleset)"
                } else {
                    Ok ("rulesets present: " + (($rules | ForEach-Object { $_.name }) -join ", "))
                }
            } else {
                # Fallback: classic branch protection
                $bp = & gh api "repos/$configuredRepo/branches/main/protection" 2>$null
                if ($LASTEXITCODE -eq 0) {
                    Ok "classic branch protection on main"
                } else {
                    BadPublic "main is not protected (no rulesets / classic protection)"
                }
            }
        } catch {
            BadPublic ("rulesets/protection: " + $_.Exception.Message)
        }

        # Self-hosted acceptance runner (labels are opaque; only count/labels snapshot)
        try {
            $runnersJson = & gh api "repos/$configuredRepo/actions/runners" 2>$null
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace([string]$runnersJson)) {
                $runners = ($runnersJson | ConvertFrom-Json).runners
                if (-not $runners -or @($runners).Count -eq 0) {
                    BadPublic "no self-hosted runners (need windows openbfme-release-vm for acceptance)"
                } else {
                    $labels = @($runners | ForEach-Object { $_.labels.name }) -join ", "
                    Ok ("self-hosted runners: " + @($runners).Count + " (labels snapshot: $labels)")
                    $flat = (@($runners | ForEach-Object { $_.labels.name }) -join " ").ToLowerInvariant()
                    if ($flat -notmatch "openbfme-release-vm") {
                        BadPublic "no runner labeled openbfme-release-vm"
                    }
                }
            } else {
                Info "could not list runners (permission may be limited)"
            }
        } catch {
            Info ("runners API skipped: " + $_.Exception.Message)
        }

        # Releases / workflow
        try {
            $rels = & gh release list -L 3 2>$null
            if ($LASTEXITCODE -eq 0) {
                if ([string]::IsNullOrWhiteSpace([string]$rels)) {
                    BadPublic "no GitHub Releases yet - Update feed empty until first publish"
                } else {
                    Ok "recent releases present"
                    $rels | ForEach-Object { Info $_ }
                }
            }
        } catch {
            Info ("release list skipped: " + $_.Exception.Message)
        }

        try {
            $runs = & gh run list --workflow=release.yml -L 1 --json conclusion,status,displayTitle,databaseId 2>$null
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace([string]$runs)) {
                $run = ($runs | ConvertFrom-Json) | Select-Object -First 1
                if ($run) {
                    if ($run.conclusion -eq "success") {
                        Ok ("latest windows release run: success (#" + $run.databaseId + ")")
                    } elseif ($run.status -eq "in_progress" -or $run.status -eq "queued") {
                        Info ("latest windows release run still " + $run.status)
                    } else {
                        BadPublic ("latest windows release run conclusion=" + $run.conclusion + " (#" + $run.databaseId + " " + $run.displayTitle + ")")
                    }
                }
            }
        } catch {
            Info ("workflow run list skipped: " + $_.Exception.Message)
        }
    }
}

# --- Summary ---
Write-Host ""
if ($script:localFailed -gt 0) {
    Write-Host ("NOT READY (local): " + $script:localFailed + " check(s) failed.") -ForegroundColor Red
    exit 1
}

Write-Host "LOCAL_BUILD_READY: brand assets, release scripts, and resolver CLI are present." -ForegroundColor Green

if ($CheckGitHub) {
    if ($script:publicFailed -gt 0) {
        Write-Host ("PUBLIC_RELEASE_NOT_READY: " + $script:publicFailed + " control-plane item(s) missing.") -ForegroundColor Yellow
        Write-Host "Admin: create release-signing + production-release envs, main/v* rulesets, signing secret, acceptance runner."
        Write-Host "Then fix any failing workflow_dispatch and cut v0.1.0-playtest.1 with Start-PlaytestRelease.ps1 -Signed -Push"
        exit 2
    }
    Write-Host "PUBLIC_RELEASE_READY: GitHub control plane looks complete enough to attempt a signed tag." -ForegroundColor Green
    Write-Host "Next: ./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -WhatIf"
    exit 0
}

Write-Host "Next (public gate): ./tools/release/Test-LaunchReleaseReadiness.ps1 -CheckGitHub"
Write-Host "Next (tag plan):    ./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -WhatIf"
exit 0
