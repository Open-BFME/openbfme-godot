[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ReleaseRoot,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$Commit,
    [ValidateSet("stable", "playtest", "nightly")][string]$Channel = "playtest",
    [Parameter(Mandatory)][string]$ReleaseHost,
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$Output
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
if ($Version -cnotmatch '^[0-9A-Za-z][0-9A-Za-z._-]{0,63}$') { throw "Unsafe version." }
# Mirrors ReleaseManifest.IsOrderableVersion in the launcher. The launcher orders
# releases by version to refuse downgrades, so a version it cannot order is refused at
# parse time — publishing one would produce a signed manifest that every launcher
# rejects, discovered only after the release went out.
if ($Version -cnotmatch '^[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}(-[0-9A-Za-z.-]+)?$') {
    throw "Version '$Version' is not SemVer (major.minor.patch[-prerelease]); the launcher cannot order it."
}
if ($Commit -cnotmatch '^[0-9a-f]{40}$') { throw "Commit must be a full lowercase SHA-1." }
if ($ReleaseHost -cnotmatch '^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$') {
    throw "Unsafe release host."
}
if ($Repository -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Unsafe release repository."
}

$root = [IO.Path]::GetFullPath($ReleaseRoot)
$packageDefinitions = @(
    @{ name = "OpenBFME-$Version-windows-x64.zip"; kind = "game-windows-x64" },
    @{ name = "OpenBFME-Launcher-$Version-windows-x64.zip"; kind = "launcher-windows-x64" }
)
$packages = foreach ($definition in $packageDefinitions) {
    $path = Join-Path $root $definition.name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release package: $($definition.name)" }
    $item = Get-Item -LiteralPath $path
    $stream = [IO.File]::OpenRead($path)
    try {
        $archive = [IO.Compression.ZipArchive]::new(
            $stream, [IO.Compression.ZipArchiveMode]::Read, $false
        )
        try { $expandedSize = ($archive.Entries | Measure-Object Length -Sum).Sum }
        finally { $archive.Dispose() }
    }
    finally { $stream.Dispose() }
    @{
        name = $definition.name
        url = "https://$ReleaseHost/$Repository/releases/download/v$Version/$($definition.name)"
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        size = $item.Length
        expandedSize = $expandedSize
        kind = $definition.kind
    }
}
$manifest = [ordered]@{
    schema = "openbfme.release-manifest"
    schemaVersion = 1
    repository = $Repository
    version = $Version
    channel = $Channel
    commit = $Commit
    packages = @($packages)
}
$outputPath = [IO.Path]::GetFullPath($Output)
[void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($outputPath)) -Force)
$temporary = "$outputPath.$PID.tmp"
[IO.File]::WriteAllText($temporary, ($manifest | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporary -Destination $outputPath
Write-Host "RELEASE_MANIFEST PASS packages=$($packages.Count) output=$outputPath"
