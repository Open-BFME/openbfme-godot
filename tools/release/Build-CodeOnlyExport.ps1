[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repo = [IO.Path]::GetFullPath($RepositoryRoot)
$destinationRoot = [IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath (Join-Path $repo "game/project.godot"))) {
    throw "RepositoryRoot does not contain game/project.godot."
}
if ($destinationRoot.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The code-only export root must be outside the source checkout."
}
if (Test-Path -LiteralPath $destinationRoot) {
    throw "Destination already exists: $destinationRoot"
}

$gameSource = Join-Path $repo "game"
$gameSourcePrefix = [IO.Path]::GetFullPath($gameSource).TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
$gameDestination = Join-Path $destinationRoot "game"
[void](New-Item -ItemType Directory -Path $gameDestination)

$excludedRoots = @(
    "data\base",
    ".godot",
    "captures",
    "screenshots"
)
$copied = 0
Get-ChildItem -LiteralPath $gameSource -Recurse -File -Force | ForEach-Object {
    $fullName = [IO.Path]::GetFullPath($_.FullName)
    if (-not $fullName.StartsWith($gameSourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source enumeration escaped the game root."
    }
    $relative = $fullName.Substring($gameSourcePrefix.Length)
    foreach ($excluded in $excludedRoots) {
        if ($relative -eq $excluded -or
            $relative.StartsWith($excluded + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            return
        }
    }
    if ($_.Name.EndsWith(".import", [StringComparison]::OrdinalIgnoreCase)) {
        return
    }
    $target = Join-Path $gameDestination $relative
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
    Copy-Item -LiteralPath $_.FullName -Destination $target
    $copied++
}

if (Test-Path -LiteralPath (Join-Path $gameDestination "data/base")) {
    throw "Code-only staging copied a game pack."
}
if ($copied -lt 10) {
    throw "Code-only staging copied too few files."
}

Write-Host "CODE_ONLY_EXPORT_STAGE PASS files=$copied root=$destinationRoot"
