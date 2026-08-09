[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$artifact = [IO.Path]::GetFullPath($Path)
if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
    throw "Artifact is missing: $artifact"
}

Add-Type -AssemblyName System.IO.Compression
$forbiddenExtensions = @(
    ".big", ".w3d", ".dds", ".tga", ".map", ".scb", ".apt", ".dat", ".ini", ".wnd",
    ".pem", ".key", ".pfx", ".p12", ".env", ".log", ".pyc", ".pyo", ".pth"
)
# The importer intentionally names its user-local .private state in source and
# documentation. Archive-entry checks below reject actual private payloads.
$keyHeaderPattern = "-----BEGIN " + "[A-Z ]*PRIVATE KEY-----"
$forbiddenText = "(?i)(?:[A-Z]:\\Users\\(?!MyUser\\)[A-Za-z0-9._-]+\\|/home/[A-Za-z0-9._-]+/|gamepacks?[/\\]|AGENTS\.md|BFME All In One Launcher[/\\]settings\.json|$keyHeaderPattern|gh[opsu]_[A-Za-z0-9_]{20,})"
$trustedOpaqueFiles = @{
    # Exact Godot 4.7 stable Windows release template from the SHA-512-pinned
    # export-template archive in release.yml. The upstream binary contains
    # compiler paths and PEM parser constants, so generic text scanning is not
    # meaningful. Any byte change invalidates this exemption.
    "OpenBFME.exe" = "c42eb5d17f683eb8bcd52c19a9f36ebf811b1788623878d5276a7d9ffc09f95c"
}
$violations = [Collections.Generic.List[string]]::new()
$stream = [IO.File]::OpenRead($artifact)
try {
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Read, $false)
    try {
        if ($archive.Entries.Count -gt 100000) { $violations.Add("too many archive entries") }
        $canonicalNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName.Replace("\", "/")
            if ([string]::IsNullOrWhiteSpace($name) -or $name.StartsWith("/") -or
                $name.Split("/") -contains "..") {
                $violations.Add("unsafe archive path: $name")
                continue
            }
            if (-not $canonicalNames.Add($name.TrimEnd("/"))) {
                $violations.Add("duplicate Windows archive path: $name")
            }
            if ($name -match '(?i)(^|/)(?:\.private|gamepacks?)(?:/|$)|(^|/)AGENTS\.md$') {
                $violations.Add("forbidden private or agent path: $name")
            }
            $extension = [IO.Path]::GetExtension($name).ToLowerInvariant()
            if ($forbiddenExtensions -contains $extension) {
                $violations.Add("forbidden retail-format extension: $name")
            }
            if ($entry.Length -gt 128MB) {
                $violations.Add("oversized unscannable archive entry: $name")
                continue
            }
            $entryStream = $entry.Open()
            $memory = [IO.MemoryStream]::new()
            try {
                $entryStream.CopyTo($memory)
                $bytes = $memory.ToArray()
                $trustedOpaque = $false
                if ($trustedOpaqueFiles.ContainsKey($name)) {
                    $sha256 = [Security.Cryptography.SHA256]::Create()
                    try {
                        $digest = ([BitConverter]::ToString($sha256.ComputeHash($bytes))).
                            Replace("-", "").ToLowerInvariant()
                        $trustedOpaque = $digest -eq $trustedOpaqueFiles[$name]
                    }
                    finally { $sha256.Dispose() }
                }
                # Pinned CPython/extension binaries embed upstream build paths and
                # OpenSSL string tables; text scanning them is noise. Trust only when
                # they live under the released python/ runtime tree and are opaque
                # machine code (exe/dll/pyd/pdb). Source under python/ still scanned.
                $pythonBinary =
                    $name -match '(?i)^python/.+\.(exe|dll|pyd|pdb)$'
                if (-not $trustedOpaque -and -not $pythonBinary) {
                    $latin = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
                    $utf16 = [Text.Encoding]::Unicode.GetString($bytes)
                    if ($latin -match $forbiddenText -or $utf16 -match $forbiddenText) {
                        $violations.Add("forbidden private path, key, or credential material: $name")
                    }
                }
            }
            finally {
                $memory.Dispose()
                $entryStream.Dispose()
            }
        }
    }
    finally { $archive.Dispose() }
}
finally { $stream.Dispose() }

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Host "RELEASE_ARTIFACT_SCAN violation=$_" }
    Write-Host "RELEASE_ARTIFACT_SCAN FAIL violations=$($violations.Count)"
    exit 1
}
Write-Host "RELEASE_ARTIFACT_SCAN PASS artifact=$([IO.Path]::GetFileName($artifact))"
