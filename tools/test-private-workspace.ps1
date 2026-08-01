[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$privateRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot ".private"))
$runImporter = Join-Path $repoRoot "run_importer.bat"
$runRetail = Join-Path $repoRoot "run_retail_slice.bat"
$bootstrap = Join-Path $PSScriptRoot "bootstrap-importer-python.ps1"

function Assert-Equal {
    param([string]$Expected, [string]$Actual, [string]$Label)
    if ($Expected -cne $Actual) {
        throw "$Label expected '$Expected' but got '$Actual'."
    }
}

function Assert-UnderPrivate {
    param([string]$Path, [string]$Label)
    $resolved = [IO.Path]::GetFullPath($Path)
    $prefix = $privateRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escaped the repository .private root: '$resolved'."
    }
}

function Parse-KeyValueOutput {
    param([string[]]$Lines)
    $values = @{}
    foreach ($line in $Lines) {
        if ($line -match '^([A-Z0-9_]+)=(.*)$') {
            $values[$Matches[1]] = $Matches[2]
        }
    }
    return $values
}

function Invoke-BatchPathProbe {
    param([string]$ScriptPath)
    $output = @(& $env:ComSpec /d /c "`"$ScriptPath`" --print-paths")
    if ($LASTEXITCODE -ne 0) {
        throw "Path probe failed for '$ScriptPath' with exit code $LASTEXITCODE."
    }
    return Parse-KeyValueOutput $output
}

function Invoke-BootstrapPathProbe {
    $output = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $bootstrap -PrintStateRoot)
    if ($LASTEXITCODE -ne 0) {
        throw "Bootstrap path probe failed with exit code $LASTEXITCODE."
    }
    return Parse-KeyValueOutput $output
}

$savedImportRoot = $env:OPENBFME_IMPORT_ROOT
$savedContentRoot = $env:OPENBFME_CONTENT
try {
    Remove-Item Env:OPENBFME_IMPORT_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:OPENBFME_CONTENT -ErrorAction SilentlyContinue

    $expectedState = [IO.Path]::GetFullPath((Join-Path $privateRoot "retail-work"))
    $expectedPython = [IO.Path]::GetFullPath((Join-Path $expectedState "tools\python-3.12-env\Scripts\python.exe"))
    $expectedContent = [IO.Path]::GetFullPath((Join-Path $privateRoot "content-packs"))

    $importDefaults = Invoke-BatchPathProbe $runImporter
    Assert-Equal $expectedState ([IO.Path]::GetFullPath($importDefaults.OPENBFME_IMPORT_ROOT)) "Importer default state root"
    Assert-Equal $expectedPython ([IO.Path]::GetFullPath($importDefaults.OPENBFME_IMPORTER_PYTHON)) "Importer default Python"
    Assert-UnderPrivate $importDefaults.OPENBFME_IMPORT_ROOT "Importer default state root"
    Assert-UnderPrivate $importDefaults.OPENBFME_IMPORTER_PYTHON "Importer default Python"

    $bootstrapDefaults = Invoke-BootstrapPathProbe
    Assert-Equal $expectedState ([IO.Path]::GetFullPath($bootstrapDefaults.OPENBFME_IMPORT_ROOT)) "Bootstrap default state root"
    Assert-UnderPrivate $bootstrapDefaults.OPENBFME_IMPORT_ROOT "Bootstrap default state root"

    $retailDefaults = Invoke-BatchPathProbe $runRetail
    Assert-Equal $expectedContent ([IO.Path]::GetFullPath($retailDefaults.OPENBFME_CONTENT)) "Retail default content root"
    Assert-UnderPrivate $retailDefaults.OPENBFME_CONTENT "Retail default content root"

    $importOverride = [IO.Path]::GetFullPath((Join-Path $privateRoot "scratch\override-retail-work"))
    $env:OPENBFME_IMPORT_ROOT = $importOverride
    $importConfigured = Invoke-BatchPathProbe $runImporter
    Assert-Equal $importOverride ([IO.Path]::GetFullPath($importConfigured.OPENBFME_IMPORT_ROOT)) "Importer environment override"
    $bootstrapConfigured = Invoke-BootstrapPathProbe
    Assert-Equal $importOverride ([IO.Path]::GetFullPath($bootstrapConfigured.OPENBFME_IMPORT_ROOT)) "Bootstrap environment override"

    $contentOverride = [IO.Path]::GetFullPath((Join-Path $privateRoot "scratch\override-content-packs"))
    $env:OPENBFME_CONTENT = $contentOverride
    $retailConfigured = Invoke-BatchPathProbe $runRetail
    Assert-Equal $contentOverride ([IO.Path]::GetFullPath($retailConfigured.OPENBFME_CONTENT)) "Retail environment override"

    & git -C $repoRoot check-ignore -q -- ".private/retail-work/private-workspace-test.probe"
    if ($LASTEXITCODE -ne 0) {
        throw "The repository .private workspace is not ignored by git."
    }

    $legacyEnvironmentToken = "LOCAL" + "APPDATA"
    foreach ($script in @($runImporter, $runRetail, $bootstrap)) {
        $text = Get-Content -Raw -LiteralPath $script
        if ($text.IndexOf($legacyEnvironmentToken, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Legacy AppData default remains in '$script'."
        }
    }
} finally {
    if ($null -eq $savedImportRoot) {
        Remove-Item Env:OPENBFME_IMPORT_ROOT -ErrorAction SilentlyContinue
    } else {
        $env:OPENBFME_IMPORT_ROOT = $savedImportRoot
    }
    if ($null -eq $savedContentRoot) {
        Remove-Item Env:OPENBFME_CONTENT -ErrorAction SilentlyContinue
    } else {
        $env:OPENBFME_CONTENT = $savedContentRoot
    }
}

Write-Host "PRIVATE_WORKSPACE_TEST PASS"
