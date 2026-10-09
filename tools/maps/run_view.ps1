# Runs the Godot map viewer once and prints its output. Needs GODOT (the 4.7 *_console.exe),
# ROTWK_INSTALL and BFME2_INSTALL in the environment.
#   .\tools\maps\run_view.ps1 -Map "map mp evendim" -Shot map1-evendim.png [-Cam overview|low|top] [-Measure 200] [-Opt debug_mode=1,cliff_uv=atlas] [-Report]
param(
    [string]$Map = "map mp evendim",
    [string]$Shot = "",
    [string]$Cam = "overview",
    [int]$Measure = 0,
    [string[]]$Opt = @(),
    [switch]$Report
)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $env:GODOT) { throw "set GODOT to Godot_v4.7-stable_win64_console.exe" }
$args = @("--path", (Join-Path $root "godot"), "res://scenes/map_viewer.tscn", "--", "--map=$Map", "--cam=$Cam")
if ($Shot) {
    $screens = if ($env:MAP1_SCREENS) { $env:MAP1_SCREENS } else { Join-Path $root "workspace\rebuild\screens" }
    $args += "--screenshot=$(Join-Path $screens $Shot)"
}
if ($Measure -gt 0) { $args += "--measure=$Measure" }
foreach ($o in $Opt) { $args += "--opt=$o" }
if ($Report) { $args += "--report" }
& $env:GODOT @args
