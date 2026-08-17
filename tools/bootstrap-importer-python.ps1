[CmdletBinding()]
param(
    [string]$StateRoot = "",
    [switch]$PrintStateRoot,
    [string]$OfflineArchive = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StateRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
        $StateRoot = $env:OPENBFME_IMPORT_ROOT
    } else {
        $StateRoot = Join-Path $repoRoot "workspace\retail-work"
    }
}
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
if ($PrintStateRoot) {
    Write-Output "OPENBFME_IMPORT_ROOT=$StateRoot"
    return
}

$toolsRoot = [IO.Path]::GetFullPath((Join-Path $StateRoot "tools"))
$environmentRoot = [IO.Path]::GetFullPath((Join-Path $toolsRoot "python-3.12-env"))
$python = Join-Path $environmentRoot "Scripts\python.exe"
$configPath = Join-Path $environmentRoot "pyvenv.cfg"
$explicitBasePython = -not [string]::IsNullOrWhiteSpace($env:OPENBFME_PYTHON)

if ($explicitBasePython) {
    $basePython = $env:OPENBFME_PYTHON.Trim()
    if (-not (Test-Path -LiteralPath $basePython -PathType Leaf)) {
        throw "OPENBFME_PYTHON points to a missing interpreter: '$basePython'. Set it to a working Python 3.12 python.exe or clear it to use the repository-pinned interpreter."
    }
    $basePython = (Resolve-Path -LiteralPath $basePython).Path
} else {
    $installer = Join-Path $PSScriptRoot "Install-PinnedPython.ps1"
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        throw "Pinned Python installer is missing: '$installer'. Restore tools\Install-PinnedPython.ps1 and rerun."
    }
    $installArguments = @{ StateRoot = $StateRoot }
    if (-not [string]::IsNullOrWhiteSpace($OfflineArchive)) {
        $installArguments["OfflineArchive"] = $OfflineArchive
    }
    & $installer @installArguments
    $basePython = Join-Path $toolsRoot "cpython-3.12.13\python.exe"
    if (-not (Test-Path -LiteralPath $basePython -PathType Leaf)) {
        throw "Pinned Python installer returned without creating the expected interpreter: '$basePython'. Review the installer output and rerun."
    }
    $basePython = (Resolve-Path -LiteralPath $basePython).Path
}

$versionOutput = @(& $basePython -c "import sys; print('.'.join(map(str, sys.version_info[:3])))" 2>&1)
$versionExit = $LASTEXITCODE
$version = [string]($versionOutput | Select-Object -First 1)
$version = $version.Trim()
if ($versionExit -ne 0 -or $version -notmatch '^3\.12\.') {
    throw "The importer bootstrap requires Python 3.12; found '$version' at '$basePython' (exit code $versionExit). Set OPENBFME_PYTHON to a working Python 3.12 interpreter or clear it to reinstall the repository pin."
}

function Get-PyVenvConfig {
    param([Parameter(Mandatory = $true)][string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*([^=]+?)\s*=\s*(.*?)\s*$') {
            $values[$matches[1].Trim().ToLowerInvariant()] = $matches[2].Trim()
        }
    }
    return $values
}

function Test-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root
    )
    if (-not [IO.Path]::IsPathRooted($Candidate)) {
        return $false
    }
    $candidateFull = [IO.Path]::GetFullPath($Candidate).TrimEnd('\')
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    return $candidateFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $candidateFull.StartsWith($rootFull + "\", [StringComparison]::OrdinalIgnoreCase)
}

function Remove-PartialImporterEnvironment {
    param([Parameter(Mandatory = $true)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath((Split-Path $fullPath -Parent))
    if ($parent -ne $toolsRoot -or (Split-Path $fullPath -Leaf) -ne "python-3.12-env") {
        throw "Refusing unsafe cleanup of unexpected importer environment path '$fullPath'."
    }
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

$recreateReason = ""
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    $recreateReason = "the importer venv python.exe is missing"
} elseif (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    $recreateReason = "the importer venv pyvenv.cfg is missing"
} else {
    $config = Get-PyVenvConfig -Path $configPath
    $configuredHome = if ($config.ContainsKey("home")) { [string]$config["home"] } else { "" }
    $configuredExecutable = if ($config.ContainsKey("executable")) { [string]$config["executable"] } else { "" }
    if ([string]::IsNullOrWhiteSpace($configuredExecutable) -and -not [string]::IsNullOrWhiteSpace($configuredHome)) {
        $configuredExecutable = Join-Path $configuredHome "python.exe"
    }

    if ([string]::IsNullOrWhiteSpace($configuredExecutable)) {
        $recreateReason = "pyvenv.cfg does not name its base interpreter"
    } elseif (-not (Test-Path -LiteralPath $configuredExecutable -PathType Leaf)) {
        $recreateReason = "pyvenv.cfg points at a base interpreter that no longer exists: '$configuredExecutable'"
    } elseif (-not $explicitBasePython -and (
        -not (Test-PathInside -Candidate $configuredExecutable -Root $toolsRoot) -or
        ([string]::IsNullOrWhiteSpace($configuredHome)) -or
        -not (Test-PathInside -Candidate $configuredHome -Root $toolsRoot)
    )) {
        $recreateReason = "pyvenv.cfg points outside the OpenBFME project-managed tools tree: home='$configuredHome' executable='$configuredExecutable'"
    } elseif (-not ([IO.Path]::GetFullPath($configuredExecutable).Equals([IO.Path]::GetFullPath($basePython), [StringComparison]::OrdinalIgnoreCase))) {
        $recreateReason = "pyvenv.cfg uses a different base interpreter: '$configuredExecutable' (selected '$basePython')"
    }
}

$previousEnvironment = ""
$recreating = -not [string]::IsNullOrWhiteSpace($recreateReason)
if ($recreating) {
    Write-Host "OPENBFME_IMPORTER_PYTHON_RECREATE reason=$recreateReason"
    New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
    if (Test-Path -LiteralPath $environmentRoot) {
        $backupName = "python-3.12-env.pre-pinned-" + (Get-Date -Format "yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N").Substring(0, 8)
        $previousEnvironment = Join-Path $toolsRoot $backupName
        Move-Item -LiteralPath $environmentRoot -Destination $previousEnvironment
        Write-Host "OPENBFME_IMPORTER_PYTHON_PREVIOUS=$previousEnvironment"
    }
}

$expectedDependencies = '12.2.0|4.61.1|0.7.1|9.1.1|0.4.6|2.3.0|26.2|1.6.0|2.20.0'
$dependencyProbe = "import PIL,fontTools,defusedxml,pytest,colorama,iniconfig,packaging,pluggy,pygments; from importlib.metadata import version; print('|'.join(version(name) for name in ('Pillow','fonttools','defusedxml','pytest','colorama','iniconfig','packaging','pluggy','Pygments')))"

function Get-DependencyProbe {
    param([Parameter(Mandatory = $true)][string]$PythonPath)
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $probeOutput = @(& $PythonPath -c $dependencyProbe 2>$null)
        $probeExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    return @{
        ExitCode = $probeExit
        Versions = [string]($probeOutput | Select-Object -First 1)
    }
}

try {
    if ($recreating) {
        if (-not [string]::IsNullOrWhiteSpace($previousEnvironment)) {
            Copy-Item -LiteralPath $previousEnvironment -Destination $environmentRoot -Recurse
            & $basePython -m venv --upgrade $environmentRoot
        } else {
            & $basePython -m venv $environmentRoot
        }
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $python -PathType Leaf)) {
            throw "Could not create importer Python environment at '$environmentRoot' from '$basePython' (exit code $LASTEXITCODE)."
        }
    }

    $dependencyResult = Get-DependencyProbe -PythonPath $python
    if ($dependencyResult.ExitCode -ne 0 -or $dependencyResult.Versions -ne $expectedDependencies) {
        & $python -m pip install --disable-pip-version-check --only-binary=:all: --require-hashes -r (Join-Path $repoRoot "importer\requirements-win.txt")
        if ($LASTEXITCODE -ne 0) {
            throw "Could not install the hash-pinned importer Python requirements from 'importer\requirements-win.txt' (exit code $LASTEXITCODE). Check package access or supply the required wheels through pip's configured offline cache."
        }
    }

    $verifiedOutput = @(& $python -c "import sys; assert sys.version_info[:2]==(3,12); $dependencyProbe" 2>&1)
    $verifiedExit = $LASTEXITCODE
    $verified = [string]($verifiedOutput | Select-Object -First 1)
    $verified = $verified.Trim()
    if ($verifiedExit -ne 0 -or $verified -ne $expectedDependencies) {
        throw "Importer Python environment verification failed at '$python': expected dependency versions '$expectedDependencies', got '$verified' (exit code $verifiedExit)."
    }
} catch {
    $failure = $_
    if ($recreating) {
        Remove-PartialImporterEnvironment -Path $environmentRoot
        if (-not [string]::IsNullOrWhiteSpace($previousEnvironment) -and (Test-Path -LiteralPath $previousEnvironment)) {
            Move-Item -LiteralPath $previousEnvironment -Destination $environmentRoot
            Write-Host "OPENBFME_IMPORTER_PYTHON_ROLLBACK restored=$environmentRoot"
        }
    }
    throw $failure
}

$verifiedParts = $verified.Split('|')
Write-Host "OPENBFME_IMPORTER_PYTHON=$python"
Write-Host "OPENBFME_IMPORTER_PYTHON_READY version=$version pillow=$($verifiedParts[0]) fonttools=$($verifiedParts[1]) defusedxml=$($verifiedParts[2]) pytest=$($verifiedParts[3])"
