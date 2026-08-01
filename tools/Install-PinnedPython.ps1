[CmdletBinding()]
param(
    [string]$StateRoot = "",
    [string]$OfflineArchive = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$pythonVersion = "3.12.13"
$pythonBuildTag = "20260718"
$pythonUrl = "https://github.com/astral-sh/python-build-standalone/releases/download/20260718/cpython-3.12.13%2B20260718-x86_64-pc-windows-msvc-install_only.tar.gz"
$pythonArchiveSha256 = "56c9dd9681c4810cb8bfdec277ee2606d8ab17e678e5bc2bd138eb8098e330b6"
$pythonExeSha256 = "32783151cd5dcf5196ff2fa342c11fc0909436531d4deec7824cbc29fd8c1a0c"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StateRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
        $StateRoot = $env:OPENBFME_IMPORT_ROOT
    } else {
        $StateRoot = Join-Path $repoRoot ".private\retail-work"
    }
}
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
$toolsRoot = [IO.Path]::GetFullPath((Join-Path $StateRoot "tools"))
$destination = [IO.Path]::GetFullPath((Join-Path $toolsRoot "cpython-$pythonVersion"))
$python = Join-Path $destination "python.exe"

function Get-Sha256Lower {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Write-PinnedPythonSuccess {
    Write-Host "OPENBFME_PINNED_PYTHON_READY path=$python version=$pythonVersion sha256=$($pythonExeSha256.Substring(0, 12))"
}

if (Test-Path -LiteralPath $destination) {
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        throw "Pinned Python destination is partial: '$destination' exists but '$python' does not. Move the partial directory aside and rerun this installer."
    }
    $existingHash = Get-Sha256Lower -Path $python
    if ($existingHash -ne $pythonExeSha256) {
        throw "Existing pinned Python hash mismatch at '$python': expected $pythonExeSha256, got $existingHash. The directory was not modified; move it aside and rerun this installer."
    }
    Write-PinnedPythonSuccess
    return
}

$systemTar = Join-Path $env:SystemRoot "System32\tar.exe"
if (Test-Path -LiteralPath $systemTar -PathType Leaf) {
    $tar = $systemTar
} else {
    $tarCommand = Get-Command tar.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $tarCommand) {
        throw "tar.exe is required to extract the pinned Python archive but was not found. Install the Windows tar feature or place tar.exe on PATH, then rerun."
    }
    $tar = $tarCommand.Source
}

New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
$stagingRoot = Join-Path $toolsRoot (".cpython-$pythonVersion.install-" + [Guid]::NewGuid().ToString("N"))
$downloadedArchive = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-cpython-$pythonVersion-$pythonBuildTag-" + [Guid]::NewGuid().ToString("N") + ".tar.gz")
$removeDownloadedArchive = $false

try {
    if ([string]::IsNullOrWhiteSpace($OfflineArchive)) {
        try {
            Invoke-WebRequest -Uri $pythonUrl -OutFile $downloadedArchive -UseBasicParsing
        } catch {
            throw "Failed to download pinned Python $pythonVersion from '$pythonUrl': $($_.Exception.Message) Use -OfflineArchive <path> with the exact pinned archive if direct downloads are blocked."
        }
        $archive = $downloadedArchive
        $removeDownloadedArchive = $true
    } else {
        $archive = [IO.Path]::GetFullPath($OfflineArchive)
        if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
            throw "Offline Python archive does not exist or is not a file: '$archive'. Supply the downloaded .tar.gz path to -OfflineArchive."
        }
    }

    $archiveHash = Get-Sha256Lower -Path $archive
    if ($archiveHash -ne $pythonArchiveSha256) {
        throw "Pinned Python archive hash mismatch for '$archive': expected $pythonArchiveSha256, got $archiveHash. Nothing was extracted; obtain the exact $pythonVersion+$pythonBuildTag artifact."
    }

    New-Item -ItemType Directory -Path $stagingRoot | Out-Null
    & $tar -xzf $archive -C $stagingRoot
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe failed to extract the verified Python archive '$archive' (exit code $LASTEXITCODE). Check available disk space and write access to '$toolsRoot'."
    }

    $extractedRoot = Join-Path $stagingRoot "python"
    $extractedPython = Join-Path $extractedRoot "python.exe"
    if (-not (Test-Path -LiteralPath $extractedPython -PathType Leaf)) {
        throw "Verified Python archive extracted without the expected 'python\python.exe' beneath '$stagingRoot'. The partial extraction will be removed."
    }
    $extractedHash = Get-Sha256Lower -Path $extractedPython
    if ($extractedHash -ne $pythonExeSha256) {
        throw "Extracted Python executable hash mismatch at '$extractedPython': expected $pythonExeSha256, got $extractedHash. The partial extraction will be removed."
    }

    $versionOutput = @(& $extractedPython -c "import sys; print('.'.join(map(str, sys.version_info[:3])))" 2>&1)
    $versionExit = $LASTEXITCODE
    $actualVersion = [string]($versionOutput | Select-Object -First 1)
    $actualVersion = $actualVersion.Trim()
    if ($versionExit -ne 0 -or $actualVersion -ne $pythonVersion) {
        throw "Extracted Python version probe failed at '$extractedPython': expected $pythonVersion, got '$actualVersion' (exit code $versionExit). The partial extraction will be removed."
    }

    if (Test-Path -LiteralPath $destination) {
        throw "Pinned Python destination appeared during installation: '$destination'. It was not overwritten; inspect the competing installation and rerun."
    }
    Move-Item -LiteralPath $extractedRoot -Destination $destination

    $installedHash = Get-Sha256Lower -Path $python
    if ($installedHash -ne $pythonExeSha256) {
        throw "Installed Python executable hash mismatch at '$python': expected $pythonExeSha256, got $installedHash. Do not use this installation; move '$destination' aside and rerun."
    }
    Write-PinnedPythonSuccess
} finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        $resolvedStagingParent = [IO.Path]::GetFullPath((Split-Path $stagingRoot -Parent))
        if ($resolvedStagingParent -ne $toolsRoot -or -not (Split-Path $stagingRoot -Leaf).StartsWith(".cpython-$pythonVersion.install-", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing unsafe cleanup of unexpected Python staging path '$stagingRoot'. Remove it manually after inspecting the path."
        }
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    if ($removeDownloadedArchive -and (Test-Path -LiteralPath $downloadedArchive -PathType Leaf)) {
        Remove-Item -LiteralPath $downloadedArchive -Force
    }
}
