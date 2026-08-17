# Orphaned Test Runner Gate: Completion Report

## Scope and accounting

The orphan cohort is frozen to the pre-sweep snapshot at commit d642b2f. That
snapshot contained 312 *_runner.gd files: 101 were executable from existing
gates and 211 were orphaned. Concurrent lanes have since raised the live on-disk
count to 345; those later files are outside this 211-runner audit.

The completed manifest has 211 rows:

| Manifest status | Count | Meaning |
|---|---:|---|
| pass | 99 | Independently observed process exit 0 and admitted to gate-orphan-runners.ps1 |
| fail | 20 | 11 false-green rows corrected from Sweep A plus 9 Sweep B exit-1 results |
| timeout | 2 | Sweep B exceeded the 180-second watchdog |
| error | 90 | Legacy Sweep A diagnostic-signature rows; process exit was not captured |

The new gate changes the frozen baseline from 101 executable runners to 200
(101 existing + 99 added). It does not claim that all 345 runners now on disk
are covered.

## Gate implementation and result

tools/gate-orphan-runners.ps1 contains seven explicit functional suites:
handlers, retail, ui, data, simulation, infrastructure, and wotr. The -Suite
parameter accepts one or more names. Every runner uses the pinned Godot console
executable with --headless --path game --script res://tests/<runner>.gd.

The gate fixes OPENBFME_CONTENT to
C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs, redirects each
process to unique files under %TEMP%, reads both files, prints one PASS or FAIL
record per runner, and exits nonzero if any process does.

The default 2000 ms inter-runner cooldown and two-attempt native-crash retry are
evidence-driven. Across repeated full gates, otherwise passing runners returned
Windows native crash codes 0xC0000005 and 0xC0000374; one had already printed
passed=35 failed=0. The failures moved between runners, while focused reruns
returned exit 0. Only negative native crash exits are retried, every attempt has
separate logs and a visible RETRY line, semantic exit 1 is never retried, and an
exhausted native crash still fails the gate.

Final unfiltered result:

    ORPHAN_RUNNER_GATE pass=99 fail=0
    GATE_EXIT=0

The definitive run emitted one RETRY record for
handlers_wp22_sciences_runner after native exit -1073741819; attempt 2 returned
exit 0. The other 98 runners passed on attempt 1. The complete summary log is
workspace/scratch/orphan-runners-20260815/orphan-runner-gate-definitive-20260816-024217-701.txt.

Rerun from the repository root:

    powershell -NoProfile -File tools\gate-orphan-runners.ps1
    powershell -NoProfile -File tools\gate-orphan-runners.ps1 -Suite handlers,retail

## Sweep B: 22 previously untested runners

Sweep B used System.Diagnostics.Process because the original Start-Process
harness did not retain Godot's exit code. Every runner received unique stdout
and stderr files named orphan-runner-sweepB-<runner>-<timestamp>-<guid>.*.txt.

| Result | Count | Runners |
|---|---:|---|
| Pass | 11 | wotr_battle_bridge_runner, wotr_construction_runner, wotr_living_world_ui_runner, wotr_livingworld_pack_runner, wotr_map3d_runner, wotr_markers_runner, wotr_phase_runner, wotr_receipt_runner, wotr_region_geometry_runner, wotr_strategic_runner, wotr_strategic_ui_runner |
| Fail | 9 | wotr_autoresolve_runner, wotr_frame_budget_runner, wotr_map_stage_runner, wotr_perf_runner, wotr_play_construction_runner, wotr_playability_runner, wotr_region_card_runner, wotr_round_trip_runner, wotr_setup_runner |
| Timeout | 2 | wotr_capture_runner, wotr_setup_capture_runner |

The machine-readable result is persisted as
workspace/scratch/orphan-runners-20260815/orphan-runner-sweepB-results-20260816-013405-141.csv.
The 22 rows are indices 190-211 in tools/orphan-runners-manifest.csv.

## Sweep A false-green correction

The handoff's 99-pass list was not executable as written. Its harness lost
native exit codes, and 11 rows marked pass contain explicit failed-result lines
in the archived logs. Those rows were changed to fail and excluded from the
gate:

- angmar_hud_binding_sweep_runner
- castle_member_behavior_runtime_runner
- eva_fidelity_runner
- goal_prop_binding_closure_runner
- men_vslice_gate_runner
- retail_archery_range_level2_runner
- retail_map_data_runner
- retail_non_fords_boot_runner
- retail_shroud_render_runner
- script_pack_startup_runner
- sol_deeper_roads_tiles_runner

Examples include retail_shroud_render_runner explicitly reporting that it
cannot run headless, men_vslice_gate_runner reporting 34 passed / 18 failed,
and goal_prop_binding_closure_runner reporting 2 passed / 6 failed.
open_field_route_runner was a transient native-process crash in the first gate
run; a focused rerun passed 10/10 and it remains wired.

The corrected 99 are the 87 that passed the first complete gate, the recovered
open_field_route_runner, and the 11 Sweep B passes.

## Failure Classification

This table classifies the 90 legacy error rows by their first captured
diagnostic, as requested. It must not be read as 90 proven nonzero exits: the
legacy harness did not capture exit codes, and manifest_dead_route_runner ends
with passed=24 failed=0. Several handler tests intentionally emit error
diagnostics while exercising refusal paths. The buckets are evidence-signature
triage for post-settle reruns, not a substitute for process exit evidence.
Because the requested schema permits exactly three buckets, expected
negative-path diagnostics with no captured exit code remain in bucket (b) by
runtime-diagnostic signature; they are not promoted to confirmed regressions
without a reliable rerun.

| Bucket | Count | Example runners |
|---|---:|---|
| (a) Parse/Load Errors (Mid-Flight Noise) | 12 | ai_library_composition_runner, hud_command_feedback_runner, retail_mp_menu_runner, structure_radial_command_set_runner |
| (b) Content/Assertion Failures (Real Regressions) | 72 | archer_cadence_runner, drawable_script_runtime_runner, object_status_runner, wotr_ai_play_runner |
| (c) Stale Expectations (No Longer Applicable) | 6 | boot_startup_runner, cah_capture_runner, handlers_wp16_ai_units_runner, minimap_parchment_capture_runner, options_pause_runner, retail_ai_ladder_runner |

Bucket (a), the post-settle rerun list:

- ai_library_composition_runner
- castle_fixtures_loader_runner
- content_validation_guarantee_runner
- hud_command_feedback_runner
- mp_eight_peer_runner
- repair_special_power_runtime_runner
- retail_input_probe_runner
- retail_mp_menu_runner
- ship_scenario_admission_runner
- structure_radial_command_set_runner
- trebuchet_vision_runner
- workspace_content_runner

The corresponding evidence is addressed by the original three-digit index, for
example:

    workspace/scratch/orphan-runners-20260815/002-ai_library_composition_runner-*.stdout.txt
    workspace/scratch/orphan-runners-20260815/002-ai_library_composition_runner-*.stderr.txt

## Phantom References (Not Actual Wiring)

All five phantom names are real runner files. None was a missing-file reference.
The original audit counted comments and export-boundary scan configuration as
lexical references even though neither executes Godot.

| Runner | File status | Pre-gate reference | Classification / final state |
|---|---|---|---|
| banner_castle_silent_playtest_runner | Exists | tools/gate-retail.ps1:611 says NOT WIRED | Intentionally unwired there; now executable through the orphan gate |
| diagnostics_log_runner | Exists | tools/export-scan.ps1:170 privacy-pattern entry | Scan-only phantom before; now executable through the orphan gate |
| lan_discovery_runner | Exists | tools/export-scan.ps1:161 privacy-pattern entry | Scan-only phantom before; now executable through the orphan gate |
| menu_match_cycle_runner | Exists | tools/export-scan.ps1:156 privacy-pattern entry | Intentionally unwired; still not in the orphan gate |
| retail_mp_menu_runner | Exists | tools/export-scan.ps1:155 privacy-pattern entry | Intentionally unwired; remains a failing parse/load triage row |

Rerun the phantom audit:

    $names = 'banner_castle_silent_playtest_runner','diagnostics_log_runner','lan_discovery_runner','menu_match_cycle_runner','retail_mp_menu_runner'
    $names | ForEach-Object { Test-Path "game/tests/$_.gd"; rg -n $_ tools }

## Persistent evidence

The original temporary directory continued filling after the 376-file handoff
and completed at 423 files: 211 stdout/stderr pairs plus results.csv. All 423
were copied to workspace/scratch/orphan-runners-20260815/. Sweep B, focused
retests, and post-settle reruns were also copied there. The definitive gate
added 200 per-attempt stdout/stderr files and its summary log, bringing the
persistent directory to 679 readable files.

Git ignore proof:

    .gitignore:19:/workspace/  workspace/scratch/orphan-runners-20260815

Rerunnable checks:

    (Import-Csv tools\orphan-runners-manifest.csv).Count
    Import-Csv tools\orphan-runners-manifest.csv | Group-Object Status
    Import-Csv tools\orphan-runners-manifest.csv | Where-Object FailureBucket | Group-Object FailureBucket
    (Get-ChildItem workspace\scratch\orphan-runners-20260815 -File).Count
    git check-ignore -v workspace/scratch/orphan-runners-20260815
    git diff --check -- tools/gate-orphan-runners.ps1 tools/orphan-runners-manifest.csv ORPHAN_RUNNERS_REPORT.md

No runner was deleted or weakened. No pack was built, published, modified, or
selected during this work.
