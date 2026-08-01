[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][string]$Output,
    [string]$OpenSsl = "C:\Program Files\Git\usr\bin\openssl.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$manifestPath = [IO.Path]::GetFullPath($Manifest)
$outputPath = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Release manifest is missing."
}
if (-not (Test-Path -LiteralPath $OpenSsl -PathType Leaf)) {
    throw "OpenSSL is missing."
}
$privateKey = $env:OPENBFME_RELEASE_SIGNING_KEY
$privateKeyMarker = "BEGIN " + "PRIVATE KEY"
if ([string]::IsNullOrWhiteSpace($privateKey) -or $privateKey -notmatch $privateKeyMarker) {
    throw "OPENBFME_RELEASE_SIGNING_KEY is missing or invalid."
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempRoot = [IO.Path]::GetFullPath((Join-Path $tempBase ("openbfme-sign-" + [Guid]::NewGuid().ToString("N"))))
if (-not $tempRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe signing scratch root."
}
[void](New-Item -ItemType Directory -Path $tempRoot)
$keyPath = Join-Path $tempRoot "release-key.pem"
$rawSignature = Join-Path $tempRoot "manifest.sig"
try {
    [IO.File]::WriteAllText($keyPath, $privateKey, [Text.UTF8Encoding]::new($false))
    & $OpenSsl dgst -sha256 -sign $keyPath -out $rawSignature $manifestPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $rawSignature -PathType Leaf)) {
        throw "Release manifest signing failed."
    }
    $signature = [Convert]::ToBase64String([IO.File]::ReadAllBytes($rawSignature))
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($outputPath)) -Force)
    [IO.File]::WriteAllText($outputPath, $signature + "`n", [Text.ASCIIEncoding]::new())
}
finally {
    if (Test-Path -LiteralPath $keyPath) {
        $length = (Get-Item -LiteralPath $keyPath).Length
        [IO.File]::WriteAllBytes($keyPath, [byte[]]::new($length))
    }
    if ($tempRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $tempRoot)) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host "RELEASE_MANIFEST_SIGNATURE PASS output=$outputPath"
