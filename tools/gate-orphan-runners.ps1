#Requires -Version 5
<#
.SYNOPSIS
    Runs the 99 orphaned Godot runners proven clean by the 2026-08-15 sweep.

.DESCRIPTION
    Every Godot process writes uniquely named stdout and stderr evidence under
    the user's temporary directory. The gate reads both files before recording
    the runner result. Use -Suite to split the slower second-tier gate into
    functional subsets; omit it to run all 99 runners.

.EXAMPLE
    powershell -File tools\gate-orphan-runners.ps1

.EXAMPLE
    powershell -File tools\gate-orphan-runners.ps1 -Suite handlers,retail
#>

[CmdletBinding()]
param(
    [string[]] $Suite,
    [ValidateRange(0, 10000)]
    [int] $InterRunnerDelayMilliseconds = 2000,
    [ValidateRange(0, 5)]
    [int] $NativeCrashRetries = 2
)

$ErrorActionPreference = 'Stop'

$godotExe = 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe'
$repo = Split-Path -Parent $PSScriptRoot
$game = Join-Path $repo 'game'
$env:OPENBFME_CONTENT = 'C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'

if (-not (Test-Path -LiteralPath $godotExe -PathType Leaf)) {
    throw "Godot executable not found: $godotExe"
}
if (-not (Test-Path -LiteralPath $game -PathType Container)) {
    throw "Godot project directory not found: $game"
}
if (-not (Test-Path -LiteralPath $env:OPENBFME_CONTENT -PathType Container)) {
    throw "OPENBFME_CONTENT directory not found: $env:OPENBFME_CONTENT"
}

# Keep these lists explicit: adding a runner to the gate is a reviewed wiring
# decision backed by a clean headless run, not a directory-scan side effect.
$runnerSuites = [ordered]@{
    handlers = @(
        'handlers_wp22_sciences_runner'
        'handlers_wp08_hero_objectives_runner'
        'handlers_wp09_ai_core_runner'
        'handlers_wp18_build_permissions_runner'
        'handlers_wp20_skirmish_conditions_runner'
        'handlers_wp21_threat_queries_runner'
        'handlers_wp23_misc_verbs_runner'
        'handlers_wp24_fog_runner'
    )
    retail = @(
        'ini_compile_remainders_runner'
        'playable_structure_runtime_consumer_runner'
        'retail_ability_fx_binding_runner'
        'retail_ambient_audio_semantics_runner'
        'retail_command_points_script_runner'
        'retail_hud_multipack_runner'
        'retail_map_script_runner'
        'retail_mp_lobby_runner'
        'retail_music_runner'
        'retail_nteam_setup_runner'
        'retail_radial_layout_runner'
        'retail_ring_hero_exclusion_runner'
        'retail_shell_apt_runtime_runner'
        'retail_slice_map_runner'
        'retail_tree_sway_runner'
        'retail_water_surface_runner'
        'retail_weather_fx_runner'
        'selected_neutral_pack_acceptance_runner'
    )
    ui = @(
        'attention_selection_runner'
        'banner_castle_silent_playtest_runner'
        'boot_deferred_options_runner'
        'cah_awards_runner'
        'camera_start_bounds_runner'
        'drag_select_structures_runner'
        'eva_overlay_closure_runner'
        'fortress_plot_presentation_runner'
        'horse_commandset_runner'
        'input_bindings_runner'
        'interface_art_index_runner'
        'menu_backdrop_weather_runner'
        'menu_instant_runner'
        'minimap_geometry_guard_runner'
        'side_command_frame_runner'
    )
    data = @(
        'ai_dispatch_coverage_runner'
        'authored_field_consumption_runner'
        'castle_fixture_spawn_runner'
        'castle_map_admission_runner'
        'castle_siege_contract_runner'
        'goal_content_matrix_runner'
        'scenario_custom_animation_prerequisite_runner'
        'scenario_pickup_runtime_registry_runner'
        'scenario_prop_runtime_registry_runner'
        'scenario_runtime_registry_runner'
        'sol_slice_gate_adversarial_runner'
    )
    simulation = @(
        'animation_authored_properties_runtime_runner'
        'animation_speed_factor_range_runner'
        'attach_update_runtime_runner'
        'auto_deposit_update_runner'
        'capturable_neutral_runner'
        'citadel_slaughter_runtime_runner'
        'command_set_upgrade_runtime_runner'
        'crush_trample_runner'
        'dual_weapon_behavior_runtime_runner'
        'dynamic_portal_runtime_runner'
        'emotion_tracker_runtime_runner'
        'experience_own_guys_die_runtime_runner'
        'flammable_update_runtime_runner'
        'foundation_monitor_runtime_runner'
        'grab_fling_passenger_runtime_runner'
        'grab_tree_live_binding_runner'
        'hero_banner_spell_recharge_upgrade_runtime_runner'
        'horde_collision_markers_runtime_runner'
        'inactive_body_runtime_runner'
        'invisibility_update_runtime_runner'
        'naval_water_route_runner'
        'open_field_route_runner'
        'passive_area_effect_heal_runner'
        'terrain_object_ground_height_runner'
        'well_statue_aura_runner'
    )
    infrastructure = @(
        'diagnostics_log_runner'
        'lan_discovery_runner'
        'leak_assertion_runner'
        'leak_probe_runner'
        'load_probe_runner'
        'load_stopwatch_runner'
        'lua_host_runner'
        'lua_stdlib_runner'
        'net_upnp_runner'
    )
    wotr = @(
        'wotr_ai_runner'
        'wotr_autoresolve_battle_runner'
        'wotr_battle_bridge_runner'
        'wotr_construction_runner'
        'wotr_living_world_ui_runner'
        'wotr_livingworld_pack_runner'
        'wotr_map3d_runner'
        'wotr_markers_runner'
        'wotr_phase_runner'
        'wotr_receipt_runner'
        'wotr_region_geometry_runner'
        'wotr_strategic_runner'
        'wotr_strategic_ui_runner'
    )
}

$availableSuites = @($runnerSuites.Keys)
if ($Suite) {
    $unknownSuites = @($Suite | Where-Object { $_ -notin $availableSuites })
    if ($unknownSuites.Count -gt 0) {
        throw "Unknown suite(s): $($unknownSuites -join ', '). Available suites: $($availableSuites -join ', ')"
    }
    $selectedSuites = @($availableSuites | Where-Object { $_ -in $Suite })
} else {
    $selectedSuites = $availableSuites
}

$selected = foreach ($suiteName in $selectedSuites) {
    foreach ($runnerName in $runnerSuites[$suiteName]) {
        [pscustomobject]@{ Suite = $suiteName; Runner = $runnerName }
    }
}

$allRunnerNames = @($runnerSuites.Values | ForEach-Object { $_ })
if (($allRunnerNames | Sort-Object -Unique).Count -ne $allRunnerNames.Count) {
    throw 'gate-orphan-runners.ps1 contains a runner in more than one suite'
}
if ($allRunnerNames.Count -ne 99) {
    throw "gate-orphan-runners.ps1 must wire exactly 99 runners; found $($allRunnerNames.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$pass = 0
$fail = 0

Write-Host "ORPHAN_RUNNER_GATE_START runners=$($selected.Count) suites=$($selectedSuites -join ',') inter_runner_delay_ms=$InterRunnerDelayMilliseconds native_crash_retries=$NativeCrashRetries"

$runnerIndex = 0
foreach ($entry in $selected) {
    if ($runnerIndex -gt 0 -and $InterRunnerDelayMilliseconds -gt 0) {
        Start-Sleep -Milliseconds $InterRunnerDelayMilliseconds
    }
    $runnerIndex++
    $runner = $entry.Runner
    $runnerFile = Join-Path $game "tests\$runner.gd"

    if (-not (Test-Path -LiteralPath $runnerFile -PathType Leaf)) {
        Write-Host "FAIL suite=$($entry.Suite) runner=$runner reason=missing-runner"
        $fail++
        continue
    }

    $attempt = 0
    do {
        $attempt++
        $token = [guid]::NewGuid().ToString('N')
        $stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) "orphan-runner-gate-$stamp-$runner-attempt$attempt-$token.stdout.txt"
        $stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) "orphan-runner-gate-$stamp-$runner-attempt$attempt-$token.stderr.txt"

        # Windows PowerShell converts native stderr into non-terminating
        # ErrorRecord objects. Capture it without aborting before ExitCode is
        # accounted for.
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $godotExe --headless --path $game --script "res://tests/$runner.gd" `
                1> $stdoutPath 2> $stderrPath
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }

        # Read both evidence files before making any retry or result decision.
        $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
        $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }

        $canRetryNativeCrash = $exitCode -lt 0 -and $attempt -le $NativeCrashRetries
        if ($canRetryNativeCrash) {
            Write-Host "RETRY suite=$($entry.Suite) runner=$runner attempt=$attempt reason=native-exit-$exitCode stdout=$stdoutPath stderr=$stderrPath"
            if ($InterRunnerDelayMilliseconds -gt 0) {
                Start-Sleep -Milliseconds $InterRunnerDelayMilliseconds
            }
        }
    } while ($canRetryNativeCrash)

    if ($exitCode -eq 0) {
        Write-Host "PASS suite=$($entry.Suite) runner=$runner attempts=$attempt exit=0 stdout=$stdoutPath stderr=$stderrPath"
        $pass++
    } else {
        $reason = "exit-$exitCode"
        $firstError = @($stderr, $stdout) -join "`n" -split "`r?`n" |
            Where-Object { $_ -match 'SCRIPT ERROR|Parse Error|ERROR:|Assertion failed' } |
            Select-Object -First 1
        if (-not $firstError) { $firstError = '(no recognized error line)' }
        Write-Host "FAIL suite=$($entry.Suite) runner=$runner reason=$reason error=$firstError stdout=$stdoutPath stderr=$stderrPath"
        $fail++
    }
}

Write-Host "ORPHAN_RUNNER_GATE pass=$pass fail=$fail"
if ($fail -gt 0) { exit 1 }
exit 0
