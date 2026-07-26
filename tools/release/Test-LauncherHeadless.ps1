[CmdletBinding()]
param([Parameter(Mandatory)][string]$Launcher)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$launcherPath = [IO.Path]::GetFullPath($Launcher)
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
    throw "Launcher executable is missing."
}
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$root = Join-Path $tempBase ("openbfme-launcher-headless-" + [Guid]::NewGuid().ToString("N"))
try {
    $version = Join-Path $root "versions\0.0.0-test"
    New-Item -ItemType Directory -Path $version -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $version "OpenBFME.exe"), [byte[]]::new(70000))
    [IO.File]::WriteAllBytes((Join-Path $version "OpenBFME.pck"), [byte[]](1, 2, 3))
    $files = @("OpenBFME.exe", "OpenBFME.pck") | ForEach-Object {
        $path = Join-Path $version $_
        [ordered]@{
            path = $_
            size = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $identity = [ordered]@{
        schema = "openbfme.installed-version"
        schemaVersion = 1
        version = "0.0.0-test"
        commit = "0123456789abcdef0123456789abcdef01234567"
        packageSha256 = "a" * 64
        files = @($files)
    }
    [IO.File]::WriteAllText(
        (Join-Path $version ".openbfme-version.json"),
        ($identity | ConvertTo-Json -Depth 5) + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $state = [ordered]@{
        schema = "openbfme.install-state"
        currentVersion = "0.0.0-test"
        previousVersion = $null
        commit = "0123456789abcdef0123456789abcdef01234567"
        previousCommit = $null
        highestVersion = "0.0.0-test"
        highestCommit = "0123456789abcdef0123456789abcdef01234567"
    }
    [IO.File]::WriteAllText(
        (Join-Path $root "current.json"),
        ($state | ConvertTo-Json) + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $process = Start-Process -FilePath $launcherPath `
        -ArgumentList @("--headless", "--no-update", "--verify-only", "--install-root", $root) `
        -WindowStyle Hidden `
        -PassThru `
        -Wait
    if ($process.ExitCode -ne 0) { throw "Headless launcher exited $($process.ExitCode)." }
    Write-Host "LAUNCHER_HEADLESS_PASS executable=$launcherPath"
}
finally {
    if ($root.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $root)) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
