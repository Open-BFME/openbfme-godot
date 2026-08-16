#Requires -Version 5
<#
.SYNOPSIS
    Invokes orphaned Godot test runners under game/tests/ as a tracked gate.

.DESCRIPTION
    Wires in orphaned test runners that have been validated to pass clean headless
    execution (exit code 0, no SCRIPT ERROR). Runners are grouped into suites.
#>

[CmdletBinding()]
param([switch] $SkipLiveRetail)

$ErrorActionPreference = 'Stop'

# Resolve Godot (hard-coded path for now)
$godotExe = 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe'
if (-not (Test-Path -LiteralPath $godotExe -PathType Leaf)) {
    throw "Godot executable not found: $godotExe"
}

$repo = (Get-Location).Path
$env:OPENBFME_CONTENT = Join-Path $repo '.private' 'content-packs'
$env:BFME2_INSTALL = Join-Path $repo '.private' 'retail-work' 'editions' 'rotwk' 'layered-install' 'layer-1-bfme2'
$env:PYTHONPATH = "$(Join-Path $repo 'importer');$(Join-Path $repo '.private' 'retail-work' 'tools' 'python-3.12-env' 'Lib' 'site-packages')"

# Passing orphaned runners (from executable headless validation)
$orphans = @(
    'ai_dispatch_coverage_runner'
    'angmar_hud_binding_sweep_runner'
    'animation_authored_properties_runtime_runner'
    'animation_speed_factor_range_runner'
    'attach_update_runtime_runner'
    'attention_selection_runner'
    'authored_field_consumption_runner'
    'auto_deposit_update_runner'
    'boot_deferred_options_runner'
    'cah_awards_runner'
    'camera_start_bounds_runner'
    'capturable_neutral_runner'
    'castle_fixture_spawn_runner'
    'castle_map_admission_runner'
    'castle_member_behavior_runtime_runner'
    'castle_siege_contract_runner'
    'citadel_slaughter_runtime_runner'
    'command_set_upgrade_runtime_runner'
    'crush_trample_runner'
    'diagnostics_log_runner'
    'drag_select_structures_runner'
    'dual_weapon_behavior_runtime_runner'
    'dynamic_portal_runtime_runner'
    'emotion_tracker_runtime_runner'
    'eva_fidelity_runner'
    'eva_overlay_closure_runner'
    'experience_own_guys_die_runtime_runner'
    'flammable_update_runtime_runner'
    'fortress_plot_presentation_runner'
    'foundation_monitor_runtime_runner'
    'goal_content_matrix_runner'
    'goal_prop_binding_closure_runner'
    'grab_fling_passenger_runtime_runner'
    'grab_tree_live_binding_runner'
    'handlers_wp08_hero_objectives_runner'
    'handlers_wp09_ai_core_runner'
    'handlers_wp18_build_permissions_runner'
    'handlers_wp20_skirmish_conditions_runner'
    'handlers_wp21_threat_queries_runner'
    'handlers_wp22_sciences_runner'
    'handlers_wp23_misc_verbs_runner'
    'handlers_wp24_fog_runner'
    'hero_banner_spell_recharge_upgrade_runtime_runner'
    'horde_collision_markers_runtime_runner'
    'horse_commandset_runner'
    'inactive_body_runtime_runner'
    'ini_compile_remainders_runner'
    'input_bindings_runner'
    'interface_art_index_runner'
    'invisibility_update_runtime_runner'
    'lan_discovery_runner'
    'leak_assertion_runner'
    'leak_probe_runner'
    'load_probe_runner'
    'load_stopwatch_runner'
    'lua_host_runner'
    'lua_stdlib_runner'
    'men_vslice_gate_runner'
    'menu_backdrop_weather_runner'
    'menu_instant_runner'
    'minimap_geometry_guard_runner'
    'naval_water_route_runner'
    'net_upnp_runner'
    'open_field_route_runner'
    'passive_area_effect_heal_runner'
    'playable_structure_runtime_consumer_runner'
    'retail_ability_fx_binding_runner'
    'retail_ambient_audio_semantics_runner'
    'retail_archery_range_level2_runner'
    'retail_command_points_script_runner'
    'retail_hud_multipack_runner'
    'retail_map_data_runner'
    'retail_map_script_runner'
    'retail_mp_lobby_runner'
    'retail_music_runner'
    'retail_non_fords_boot_runner'
    'retail_nteam_setup_runner'
    'retail_radial_layout_runner'
    'retail_ring_hero_exclusion_runner'
    'retail_shell_apt_runtime_runner'
    'retail_shroud_render_runner'
    'retail_slice_map_runner'
    'retail_tree_sway_runner'
    'retail_water_surface_runner'
    'retail_weather_fx_runner'
    'scenario_custom_animation_prerequisite_runner'
    'scenario_pickup_runtime_registry_runner'
    'scenario_prop_runtime_registry_runner'
    'scenario_runtime_registry_runner'
    'script_pack_startup_runner'
    'selected_neutral_pack_acceptance_runner'
    'side_command_frame_runner'
    'sol_deeper_roads_tiles_runner'
    'sol_slice_gate_adversarial_runner'
    'terrain_object_ground_height_runner'
    'well_statue_aura_runner'
    'wotr_ai_runner'
    'wotr_autoresolve_battle_runner'
    'banner_castle_silent_playtest_runner'
)

$pass = 0
$fail = 0

Write-Host "Running $($orphans.Count) orphaned test runners..."
Write-Host ""

foreach ($runner in $orphans) {
    $runnerFile = Join-Path $repo 'game' 'tests' "$runner.gd"
    if (-not (Test-Path -LiteralPath $runnerFile)) {
        Write-Host "  ? $runner (not found)"
        $fail++
        continue
    }
    
    $scriptPath = "res://tests/$runner.gd"
    $p = Start-Process -FilePath $godotExe `
        -ArgumentList @('--headless', '--path', (Join-Path $repo 'game'), '--script', $scriptPath) `
        -PassThru -NoNewWindow -RedirectStandardOutput $null -RedirectStandardError $null
    
    $p.WaitForExit()
    
    if ($p.ExitCode -eq 0) {
        Write-Host "  + $runner"
        $pass++
    } else {
        Write-Host "  - $runner (exit: $($p.ExitCode))"
        $fail++
    }
}

Write-Host ""
Write-Host "ORPHAN_GATE_RESULT pass=$pass fail=$fail"

if ($fail -gt 0) {
    exit 1
} else {
    exit 0
}
