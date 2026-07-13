[CmdletBinding()]
param(
    [string]$Root = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Join-Path $repoRoot "game"
}
$scanRoot = [IO.Path]::GetFullPath($Root)
if (-not (Test-Path -LiteralPath $scanRoot -PathType Container)) {
    throw "Export scan root does not exist: $scanRoot"
}

$forbiddenExtensions = @(
    ".apt", ".big", ".csf", ".dds", ".ini", ".map", ".skudef", ".str", ".tga", ".w3d", ".wnd"
)
$textExtensions = @(
    ".cfg", ".gd", ".godot", ".json", ".md", ".ps1", ".py", ".txt", ".tscn"
)
$forbiddenText = '(?i)(?:[A-Z]:[\\/]BFME2(?:[\\/]|$)|_bfme2_extract|EA Games|Electronic Arts|OpenSAGE(?:\.|/|\\)|Sage\.Game|Sage\.Ini)'
$ignoredSegments = @(".godot", ".import")
$rootPrefix = $scanRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar

$violations = [Collections.Generic.List[string]]::new()
$files = [Collections.Generic.List[IO.FileInfo]]::new()
$pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
$rootItem = Get-Item -LiteralPath $scanRoot -Force
if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    $violations.Add("directory reparse point is not export-safe: .")
}
else {
    $pending.Push($rootItem)
}
while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($entry in Get-ChildItem -LiteralPath $directory.FullName -Force) {
        $relative = $entry.FullName.Substring($rootPrefix.Length)
        $segments = @($relative -split '[\\/]')
        if ($entry.PSIsContainer) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                $violations.Add("directory reparse point is not export-safe: $($relative.Replace('\', '/'))")
            }
            elseif (-not ($segments | Where-Object { $ignoredSegments -contains $_ })) {
                $pending.Push([IO.DirectoryInfo]$entry)
            }
            continue
        }
        if (-not ($segments | Where-Object { $ignoredSegments -contains $_ })) {
            $files.Add([IO.FileInfo]$entry)
        }
    }
}

$totalBytes = [long]0
foreach ($file in $files) {
    $relative = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
    $totalBytes += [long]$file.Length
    $segments = @($relative -split '/')
    if ($segments -contains "_bfme2_extract") {
        $violations.Add("forbidden directory: $relative")
    }
    if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant()) {
        $violations.Add("forbidden retail-format extension: $relative")
    }
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $violations.Add("reparse point is not export-safe: $relative")
        continue
    }
    if ($textExtensions -contains $file.Extension.ToLowerInvariant()) {
        $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 -ErrorAction Stop
        if ($text -match $forbiddenText) {
            $violations.Add("forbidden path, metadata, or donor reference: $relative")
        }
    }
}

if ($violations.Count -gt 0) {
    foreach ($violation in $violations) {
        Write-Host "EXPORT_SCAN violation=$violation"
    }
    Write-Host "EXPORT_SCAN FAIL violations=$($violations.Count)"
    exit 1
}

Write-Host "EXPORT_SCAN PASS files=$($files.Count) bytes=$totalBytes root=$scanRoot"
exit 0
