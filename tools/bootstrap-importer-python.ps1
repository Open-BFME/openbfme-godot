[CmdletBinding()]
param(
    [string]$StateRoot = "",
    [switch]$PrintStateRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($StateRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
        $StateRoot = $env:OPENBFME_IMPORT_ROOT
    } else {
        $StateRoot = Join-Path $repoRoot ".private\retail-work"
    }
}
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
if ($PrintStateRoot) {
    Write-Output "OPENBFME_IMPORT_ROOT=$StateRoot"
    return
}
$basePython = $env:OPENBFME_PYTHON
if ([string]::IsNullOrWhiteSpace($basePython)) {
    $basePython = (Get-Command python -ErrorAction Stop | Select-Object -First 1).Source
}
$version = (& $basePython -c "import sys; print('.'.join(map(str, sys.version_info[:3])))").Trim()
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^3\.12\.') {
    throw "The importer bootstrap requires Python 3.12; found '$version' at '$basePython'. Set OPENBFME_PYTHON."
}

$environmentRoot = Join-Path $StateRoot "tools\python-3.12-env"
$python = Join-Path $environmentRoot "Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $environmentRoot -Parent) | Out-Null
    & $basePython -m venv $environmentRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not create importer Python environment." }
}

$oldPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$pillow = @(& $python -c "import PIL; print(PIL.__version__)" 2>$null)
$pillowExit = $LASTEXITCODE
$ErrorActionPreference = $oldPreference
if ($pillowExit -ne 0 -or ($pillow | Select-Object -First 1) -ne '12.2.0') {
    & $python -m pip install --disable-pip-version-check --only-binary=:all: --require-hashes -r (Join-Path $repoRoot "importer\requirements-win.txt")
    if ($LASTEXITCODE -ne 0) { throw "Could not install the hash-pinned importer Python requirements." }
}

$verified = (& $python -c "import sys,PIL; assert sys.version_info[:2]==(3,12); assert PIL.__version__=='12.2.0'; print(PIL.__version__)").Trim()
if ($LASTEXITCODE -ne 0 -or $verified -ne '12.2.0') { throw "Importer Python environment verification failed." }
Write-Host "OPENBFME_IMPORTER_PYTHON=$python"
Write-Host "OPENBFME_IMPORTER_PYTHON_READY version=$version pillow=$verified"
