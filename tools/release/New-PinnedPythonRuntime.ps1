[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourcePython,
    [Parameter(Mandatory)][string]$Destination,
    [Parameter(Mandatory)][string]$Requirements,
    [Parameter(Mandatory)][string]$ImporterRoot,
    [Parameter(Mandatory)][string]$ImporterEntry,
    [Parameter(Mandatory)][string]$BundleRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$python = [IO.Path]::GetFullPath($SourcePython)
$destinationRoot = [IO.Path]::GetFullPath($Destination)
$requirementsPath = [IO.Path]::GetFullPath($Requirements)
$importerRootPath = [IO.Path]::GetFullPath($ImporterRoot)
$importerEntryPath = [IO.Path]::GetFullPath($ImporterEntry)
$bundleRootPath = [IO.Path]::GetFullPath($BundleRoot)
foreach ($path in @($python, $requirementsPath, $importerEntryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
if (-not (Test-Path -LiteralPath $importerRootPath -PathType Container)) {
    throw "ImporterRoot is missing."
}
if (-not (Test-Path -LiteralPath $bundleRootPath -PathType Container)) {
    throw "BundleRoot is missing."
}
if (-not $destinationRoot.StartsWith(
    $bundleRootPath.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase
)) {
    throw "Python destination must be contained by BundleRoot."
}
if (Test-Path -LiteralPath $destinationRoot) {
    throw "Destination already exists: $destinationRoot"
}

$sourceRoot = (& $python -c "import sys; print(sys.base_prefix)").Trim()
$sourceRoot = [IO.Path]::GetFullPath($sourceRoot)
$version = (& $python -c "import sys; print(sys.version.split()[0])").Trim()
if ($version -ne "3.12.13") {
    throw "Python 3.12.13 is required; found $version."
}

[void](New-Item -ItemType Directory -Path $destinationRoot)
$rootFiles = @(
    "python.exe",
    "python3.dll",
    "python312.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll"
)
foreach ($name in $rootFiles) {
    $source = if ($name -eq "python.exe") {
        $python
    } else {
        Join-Path $sourceRoot $name
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Pinned Python runtime file is missing: $name"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $destinationRoot $name)
}

$excludedLibRoots = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
foreach ($name in @(
    "ensurepip",
    "idlelib",
    "lib2to3",
    "pydoc_data",
    "site-packages",
    "test",
    "tkinter",
    "turtledemo"
)) {
    [void]$excludedLibRoots.Add($name)
}

foreach ($tree in @("DLLs", "Lib")) {
    $sourceTree = Join-Path $sourceRoot $tree
    $destinationTree = Join-Path $destinationRoot $tree
    if (-not (Test-Path -LiteralPath $sourceTree -PathType Container)) {
        throw "Pinned Python runtime directory is missing: $tree"
    }
    [void](New-Item -ItemType Directory -Path $destinationTree)
    $prefix = $sourceTree.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
    Get-ChildItem -LiteralPath $sourceTree -Recurse -File -Force | ForEach-Object {
        if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Python runtime source contains a link: $($_.FullName)"
        }
        $relative = $_.FullName.Substring($prefix.Length)
        $parts = $relative -split '[\\/]'
        if ($tree -eq "Lib" -and $parts.Count -gt 0 -and $excludedLibRoots.Contains($parts[0])) {
            return
        }
        if ($parts -contains "__pycache__" -or $_.Extension -in @(".pyc", ".pyo")) {
            return
        }
        $target = Join-Path $destinationTree $relative
        [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
        Copy-Item -LiteralPath $_.FullName -Destination $target
    }
}

$sitePackages = Join-Path $destinationRoot "Lib\site-packages"
[void](New-Item -ItemType Directory -Path $sitePackages)
$priorPythonPath = $env:PYTHONPATH
$priorUtf8 = $env:PYTHONUTF8
$priorBytecode = $env:PYTHONDONTWRITEBYTECODE
try {
    $env:PYTHONDONTWRITEBYTECODE = "1"
    & $python -m pip install `
        --disable-pip-version-check `
        --only-binary=:all: `
        --require-hashes `
        --ignore-installed `
        --no-deps `
        --no-compile `
        --target $sitePackages `
        -r $requirementsPath
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled importer dependencies failed to install."
    }

    $generatedConsoleScripts = Join-Path $sitePackages "bin"
    if (Test-Path -LiteralPath $generatedConsoleScripts -PathType Container) {
        $sitePrefix = $sitePackages.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
        $resolvedScripts = [IO.Path]::GetFullPath($generatedConsoleScripts)
        if (-not $resolvedScripts.StartsWith($sitePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Generated console-script root escaped site-packages."
        }
        Remove-Item -LiteralPath $resolvedScripts -Recurse -Force
    }

    $unsafePythonFiles = Get-ChildItem -LiteralPath $destinationRoot -Recurse -Force |
        Where-Object {
            $_.Name -eq "__pycache__" -or
            $_.Extension -in @(".pyc", ".pyo", ".pth") -or
            $_.Name -in @("sitecustomize.py", "usercustomize.py")
        }
    if ($unsafePythonFiles) {
        throw "Bundled Python contains generated bytecode or startup customization."
    }

    $env:PYTHONPATH = $importerRootPath
    $env:PYTHONUTF8 = "1"
    $encodedImporter = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($importerRootPath))
    $encodedSite = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($sitePackages))
    $attestationCommand = "import base64,json,sys; sys.path[:0]=[base64.b64decode('$encodedImporter').decode(),base64.b64decode('$encodedSite').decode()]; from openbfme_importer.bootstrap import python_runtime_attestation; print(json.dumps(python_runtime_attestation()))"
    $runtimeJson = & (Join-Path $destinationRoot "python.exe") -X utf8 -B -I -S -c $attestationCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled Python runtime failed its pinned attestation."
    }
    $runtime = $runtimeJson | ConvertFrom-Json
    if (
        $runtime.version -ne "3.12.13" -or
        $runtime.launcher_sha256 -ne "32783151cd5dcf5196ff2fa342c11fc0909436531d4deec7824cbc29fd8c1a0c" -or
        $runtime.base_dll_sha256 -ne "60a12f6f0bdc0363544fcb3c824decf97d843ea7c3a9732f4ba02fa8b33cd6df" -or
        $runtime.tree_sha256 -ne "af162c36194d692391e2a972537bfa57d6576d8ffd701b731aee1ee282b6b013"
    ) {
        throw "Bundled Python runtime does not match the pinned 3.12.13 identity."
    }
    & (Join-Path $destinationRoot "python.exe") -X utf8 -B -I -S $importerEntryPath --help | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled importer failed its startup check."
    }
}
finally {
    $env:PYTHONPATH = $priorPythonPath
    $env:PYTHONUTF8 = $priorUtf8
    $env:PYTHONDONTWRITEBYTECODE = $priorBytecode
}

$unsafeBundleEntries = Get-ChildItem -LiteralPath $bundleRootPath -Recurse -Force |
    Where-Object {
        ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $_.Name -eq "__pycache__" -or
        $_.Extension -in @(".pyc", ".pyo", ".pth") -or
        $_.Name -in @("sitecustomize.py", "usercustomize.py")
    }
if ($unsafeBundleEntries) {
    throw "Release bundle contains a reparse point, bytecode, or Python startup customization."
}

$inventoryName = "openbfme-bundle-inventory.json"
$inventoryPath = Join-Path $bundleRootPath $inventoryName
$bundlePrefix = $bundleRootPath.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
$rows = [Collections.Generic.List[object]]::new()
$canonical = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $bundleRootPath -Recurse -File -Force |
    Sort-Object FullName |
    ForEach-Object {
        if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Bundle contains a file reparse point."
        }
        $relative = $_.FullName.Substring($bundlePrefix.Length).Replace("\", "/")
        if ($relative -eq $inventoryName) { return }
        if (-not $canonical.Add($relative)) { throw "Bundle contains duplicate Windows paths." }
        $rows.Add([ordered]@{
            path = $relative
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
$inventory = [ordered]@{
    schema = "openbfme.bundle-inventory"
    schemaVersion = 1
    files = @($rows)
}
[IO.File]::WriteAllText(
    $inventoryPath,
    ($inventory | ConvertTo-Json -Depth 5) + "`n",
    [Text.UTF8Encoding]::new($false)
)

Write-Host "PINNED_PYTHON_RUNTIME_PASS root=$destinationRoot"
