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

# Exclude retail-derived / private pack trees under data\base, but KEEP first-party
# menu chrome the shipped UI loads (splash, backdrop cycle, app icon art). Those live
# under data\base\assets\ui by design and are not EA retail bytes.
$excludedRoots = @(
    "data\base",
    ".godot",
    "captures",
    "screenshots"
)
$allowedUnderExcluded = @(
    "data\base\assets\ui"
)
$copied = 0
Get-ChildItem -LiteralPath $gameSource -Recurse -File -Force | ForEach-Object {
    $fullName = [IO.Path]::GetFullPath($_.FullName)
    if (-not $fullName.StartsWith($gameSourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source enumeration escaped the game root."
    }
    $relative = $fullName.Substring($gameSourcePrefix.Length)
    $excluded = $false
    foreach ($root in $excludedRoots) {
        if ($relative -eq $root -or
            $relative.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            $excluded = $true
            break
        }
    }
    if ($excluded) {
        $allow = $false
        foreach ($root in $allowedUnderExcluded) {
            if ($relative -eq $root -or
                $relative.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                $allow = $true
                break
            }
        }
        if (-not $allow) { return }
    }
    if ($_.Name.EndsWith(".import", [StringComparison]::OrdinalIgnoreCase)) {
        return
    }
    $target = Join-Path $gameDestination $relative
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
    Copy-Item -LiteralPath $_.FullName -Destination $target
    $copied++
}

# Fail closed if we accidentally copied pack.json / selection-shaped retail trees.
$illegalPackMarkers = @(
    "data\base\pack.json",
    "data\base\selection.json"
)
foreach ($marker in $illegalPackMarkers) {
    if (Test-Path -LiteralPath (Join-Path $gameDestination $marker)) {
        throw "Code-only staging copied a game pack marker: $marker"
    }
}
if ($copied -lt 10) {
    throw "Code-only staging copied too few files."
}

Write-Host "CODE_ONLY_EXPORT_STAGE PASS files=$copied root=$destinationRoot"
