[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

# Create-a-Hero slice gate: profile round-trip, the attribute arithmetic, the
# fortress hero roster, and the purchase that spawns the created hero.
#
# NEEDS NO CONTENT PACK. The runner builds its own Create-a-Hero class table
# from numbers transcribed out of PURE RETAIL INI, so this runs anywhere,
# including on a machine with no retail install. That is the point: the gate
# asserts the arithmetic and the wiring, and neither depends on a cooked pack.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot "resolve-godot.ps1")
$GodotPath = Resolve-OpenBfmeGodot -RequestedPath $GodotPath -RepoRoot $repoRoot -PreferConsole

$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    $output = & $GodotPath --headless --quit-after 1000 --path (Join-Path $repoRoot "game") --script res://tests/cah_create_a_hero_runner.gd 2>&1 | Out-String
    $code = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorActionPreference
}

Write-Host $output.TrimEnd()
if ($code -ne 0 -or $output -notmatch '(?m)^CAH_CREATE_A_HERO_OK passed=\d+ failed=0\s*$') {
    throw "Create-a-Hero slice check failed with exit code $code."
}
