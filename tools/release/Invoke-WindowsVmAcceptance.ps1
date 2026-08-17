[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [Parameter(Mandatory)][string]$RetailPath,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$ExpectedCommit,
    [Parameter(Mandatory)][string]$Receipt
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param([string]$Path, [string]$Label, [switch]$Container)
    $resolved = [IO.Path]::GetFullPath($Path)
    $pathType = if ($Container) { "Container" } else { "Leaf" }
    if (-not (Test-Path -LiteralPath $resolved -PathType $pathType)) {
        throw "$Label is missing."
    }
    return $resolved
}

function Invoke-HiddenProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [string]$LogRoot
    )
    New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
    $stdout = Join-Path $LogRoot "stdout.log"
    $stderr = Join-Path $LogRoot "stderr.log"
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments 1> $stdout 2> $stderr
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        $message = ((Get-Content -LiteralPath $stdout, $stderr -Raw -ErrorAction SilentlyContinue) -join "`n")
        $message = $message -replace '(?i)[A-Z]:\\[^\r\n"]+', '<private-path>'
        throw "Packaged launcher exited ${exitCode}: $message"
    }
}

function Resolve-SelectedPack {
    param([string]$InstallRoot)
    $contentRoot = Join-Path $InstallRoot "workspace\content-packs"
    $selectionPath = Join-Path $contentRoot "selection.json"
    if (-not (Test-Path -LiteralPath $selectionPath -PathType Leaf)) {
        throw "Packaged launcher did not publish a pack selection."
    }
    $selection = Get-Content -LiteralPath $selectionPath -Raw | ConvertFrom-Json
    $active = [string]$selection.activePack
    if ($active -cnotmatch '^[0-9a-z][0-9a-z._-]{0,63}/[0-9a-f]{64}$') {
        throw "Packaged launcher selected an unsafe pack identity."
    }
    $root = [IO.Path]::GetFullPath((Join-Path $contentRoot $active))
    $prefix = [IO.Path]::GetFullPath($contentRoot).TrimEnd('\') + '\'
    if (-not $root.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Selected pack escaped the private content root."
    }
    return $root
}

$release = Resolve-RequiredPath $ReleaseDirectory "Release artifact directory" -Container
$retail = Resolve-RequiredPath $RetailPath "BFME II retail installation" -Container
$repo = Resolve-RequiredPath $RepositoryRoot "Repository root" -Container
$receiptPath = [IO.Path]::GetFullPath($Receipt)
if ($ExpectedCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw "Expected release commit must be a full lowercase SHA-1."
}

$manifestPath = Resolve-RequiredPath (Join-Path $release "release-manifest.json") "Release manifest"
$signaturePath = Resolve-RequiredPath (Join-Path $release "release-manifest.json.sig") "Release manifest signature"
$publicKeyPath = Resolve-RequiredPath (Join-Path $repo "tools\release\release-manifest-public.pem") "Release public key"
$openSsl = Resolve-RequiredPath "C:\Program Files\Git\usr\bin\openssl.exe" "Git OpenSSL"
$signatureBinary = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-manifest-signature-" + [Guid]::NewGuid().ToString("N"))
try {
    [IO.File]::WriteAllBytes(
        $signatureBinary,
        [Convert]::FromBase64String((Get-Content -LiteralPath $signaturePath -Raw).Trim()))
    & $openSsl dgst -sha256 -verify $publicKeyPath -signature $signatureBinary $manifestPath
    if ($LASTEXITCODE -ne 0) { throw "Release manifest signature is invalid." }
}
finally {
    if (Test-Path -LiteralPath $signatureBinary) { Remove-Item -LiteralPath $signatureBinary -Force }
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne "openbfme.release-manifest" -or
    $manifest.schemaVersion -ne 1 -or
    $manifest.commit -cne $ExpectedCommit) {
    throw "Release manifest identity does not match the tested commit."
}

$sumsPath = Resolve-RequiredPath (Join-Path $release "SHA256SUMS.txt") "SHA256 sums"
$expectedSums = @{}
foreach ($line in Get-Content -LiteralPath $sumsPath) {
    if ($line -cnotmatch '^([0-9a-f]{64})  ([0-9A-Za-z._-]+\.zip)$') {
        throw "SHA256SUMS.txt contains an unsafe row."
    }
    if ($expectedSums.ContainsKey($Matches[2])) { throw "SHA256SUMS.txt contains a duplicate row." }
    $expectedSums[$Matches[2]] = $Matches[1]
}

$gamePackage = @($manifest.packages | Where-Object kind -eq "game-windows-x64")
$launcherPackage = @($manifest.packages | Where-Object kind -eq "launcher-windows-x64")
if ($gamePackage.Count -ne 1 -or $launcherPackage.Count -ne 1) {
    throw "Release manifest package set is incomplete."
}
foreach ($package in @($gamePackage[0], $launcherPackage[0])) {
    if ([string]$package.name -cnotmatch '^[0-9A-Za-z][0-9A-Za-z._-]{0,127}\.zip$' -or
        -not $expectedSums.ContainsKey([string]$package.name)) {
        throw "Release manifest contains an unsafe or unlisted ZIP name."
    }
    $archive = Resolve-RequiredPath (Join-Path $release $package.name) "Release ZIP"
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne [string]$package.sha256 -or
        $expectedSums[[string]$package.name] -cne $actual) {
        throw "Release ZIP hash does not match both manifests."
    }
    & (Join-Path $repo "tools\release\Test-ReleaseArtifact.ps1") -Path $archive
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ("openbfme-vm-acceptance-" + [Guid]::NewGuid().ToString("N"))
$launcherRoot = Join-Path $scratch "launcher"
$gameRoot = Join-Path $scratch "game"
$installA = Join-Path $scratch "install-a"
$installB = Join-Path $scratch "install-b"
$logs = Join-Path $scratch "logs"
try {
    New-Item -ItemType Directory -Path $launcherRoot, $gameRoot | Out-Null
    Expand-Archive -LiteralPath (Join-Path $release $launcherPackage[0].name) -DestinationPath $launcherRoot
    Expand-Archive -LiteralPath (Join-Path $release $gamePackage[0].name) -DestinationPath $gameRoot

    $launcher = Resolve-RequiredPath (Join-Path $launcherRoot "OpenBFME.Launcher.exe") "Packaged launcher"
    $game = Resolve-RequiredPath (Join-Path $gameRoot "OpenBFME.exe") "Packaged game"
    $importArguments = {
        param([string]$InstallRoot)
        return @(
            "--headless", "--no-update", "--import-bfme2",
            "--bfme2-path", $retail, "--install-root", $InstallRoot
        )
    }
    Invoke-HiddenProcess $launcher (& $importArguments $installA) $launcherRoot (Join-Path $logs "import-a")
    Invoke-HiddenProcess $launcher (& $importArguments $installB) $launcherRoot (Join-Path $logs "import-b")

    $packA = Resolve-SelectedPack $installA
    $packB = Resolve-SelectedPack $installB
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($receiptPath)) -Force | Out-Null
    & python (Join-Path $repo "tools\release\compare_import_bundles.py") `
        $packA $packB `
        --game bfme2 `
        --profile men-fords-v0 `
        --release-commit $ExpectedCommit `
        --require-family textures=80 `
        --require-family models=39 `
        --require-family animations=26 `
        --require-family skeletons=36 `
        --require-family audio=41 `
        --require-family maps=87 `
        --require-family rules=57 `
        --require-family ui=14 `
        --receipt $receiptPath
    if ($LASTEXITCODE -ne 0) { throw "Whole-pack reproducibility comparison failed." }

    $oldContent = $env:OPENBFME_CONTENT
    try {
        $env:OPENBFME_CONTENT = Join-Path $installA "workspace\content-packs"
        & (Join-Path $repo "tools\release\Test-WindowsExport.ps1") `
            -Executable $game `
            -LogRoot (Join-Path $logs "game")
    }
    finally {
        if ($null -eq $oldContent) { Remove-Item Env:OPENBFME_CONTENT -ErrorAction SilentlyContinue }
        else { $env:OPENBFME_CONTENT = $oldContent }
    }
    Write-Host "WINDOWS_VM_ACCEPTANCE_PASS"
}
finally {
    if (Test-Path -LiteralPath $scratch) {
        $resolvedScratch = [IO.Path]::GetFullPath($scratch)
        $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $resolvedScratch.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a VM acceptance directory outside the system temp root."
        }
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}
