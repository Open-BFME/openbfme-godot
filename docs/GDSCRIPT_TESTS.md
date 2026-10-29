# GDScript test inventory

Use Python 3.13, `tools/requirements-test.txt` and Godot 4.7.2 (`GODOT` names
the editor executable). The full tools suite additionally needs Capstone and
pefile (see `tools/README.md`).

```sh
python -m pytest -n 6 --dist worksteal -q -p no:cacheprovider tools --durations=20 -ra
python -m pytest -q -rs tools/test_gdscript.py
```

## Scheduling policy

The launcher coverage gap is closed by the existing release pytest runners,
including the resource-limited write-failure regression and the argument-aware
social/OAuth fixtures. Every launcher entry point is listed below.

For the game, **inventory is not permission to execute**. `ALLOWLIST`,
`CI_GATES` and `DENYLIST` in `tools/test_gdscript.py` are explicit and disjoint.
Only the five allowlisted game scripts are parametrized by that runner.
They need no private retail assets, window, GPU, pixel readback or guessed
frame delays. Bundled source resources (e.g. application icons) and the built
host GDExtension are still required. Missing Godot/native-build prerequisites
are loud skips. Each subprocess has a **20-second timeout**, with a separate
30-second editor-import budget. An allowlisted script printing SKIP, returning
77, reporting an engine/script error or exiting nonzero **fails**, not skips.

The reviewer's 12 existing CI Godot gates are explicitly denied here: smoke,
start, end, net_end, stealth_gate, audio, opts1, opts2, saveui1, opts6,
new_game_loading_art and menufix2. Their existing CI runner supplies their
own prerequisites; do not duplicate them in plain pytest. `run_tests.bat`
also invokes the smoke gate locally.

Other retail, rendered, mixed-phase and destructive scripts are inventoried as
**manual only**, with a reason. These are not automated coverage and do not
inflate pytest's skip count. Installing retail files, setting DISPLAY or
`OPENBFME_CRASH_TESTS`, or passing a window option cannot enable them in this
runner. The old `--gdscript-window` option and generic crash subprocess
collection have been removed. For an intentional manual run, use the command
in the individual script's header (normally `godot --path godot --script
res://tests/<file>`), on a host with its stated prerequisites. In particular,
do not invoke the Windows crash probe except in a dedicated Windows debug
crash/minidump validation session with explicit opt-in.

The game runner strips BFME1_INSTALL, BFME2_INSTALL, ROTWK_INSTALL, DISPLAY and
WAYLAND_DISPLAY from its isolated subprocess environment. Existing specialized
fixture runners are retained, not wrapped or duplicated. Support scripts,
scene helpers and rendered walks remain owned by their listed consumer.

`test_every_gdscript_is_inventoried` runs without Godot. It rejects missing,
stale or duplicate classifications, missing documentation rows, broken fixture
references and exclusions without reasons. Adding a file requires an explicit
scheduling/ownership decision; it is never automatically promoted to execution.

## Launcher inventory

| Script | Runner | Coverage/prerequisites |
| --- | --- | --- |
| `launcher/tests/aio_progress_test.gd` | `tools/release/test_aio.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/background_play_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/bfme1_assets_test.gd` | `tools/release/test_aio.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/bfme1_offer_test.gd` | `tools/release/test_aio.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/bounded_read_test.gd` | `tools/release/test_onezip.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/crypto_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/external_open_test.gd` | `tools/release/test_external_open.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/first_run_test.gd` | `launcher/tests/test_first_run.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/install_paths_test.gd` | `tools/release/test_install_paths.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/instance_lock_parser_test.gd` | `tools/release/test_instance_lock.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/instance_lock_test.gd` | `tools/release/test_instance_lock.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/layout_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/l10n_test.gd` | `tools/l10n/test_l10n.py` | The text catalog's runtime on the generated catalog and synthetic patterns; no game files. |
| `launcher/tests/lobby_list_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/log_privacy_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/macos_cache_test.gd` | `tools/release/test_macplay.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/macos_helper_probe_test.gd` | `tools/release/test_macplay.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/macos_platform_test.gd` | `tools/release/test_macos_platform.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/macos_preview_test.gd` | `tools/release/test_launcher.py` | Portable checks run; native macOS-only phase is an explicit skip elsewhere. |
| `launcher/tests/macplay_test.gd` | `tools/release/test_macplay.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/mod_catalog_test.gd` | `tools/release/test_mods.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/mods_window_test.gd` | `tools/release/test_mods.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/oauth_integration_test.gd` | `tools/release/test_launcher.py` | Delegates to test_launcher_oauth.py; loopback provider/SQLite fixtures; loud skip without Node. |
| `launcher/tests/oauth_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/replay_association_test.gd` | `tools/release/test_replay_open.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/replay_open_test.gd` | `tools/release/test_replay_open.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/social_client_test.gd` | `tools/release/test_launcher.py` | Delegates to test_launcher_social.py; loopback social fixture. |
| `launcher/tests/social_launch_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/social_ui_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/unit_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/update_fault_test.gd` | `tools/release/test_launcher_updates.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/update_status_test.gd` | `tools/release/test_launcher.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/verifyfiles_test.gd` | `tools/test_verifyfiles.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/verifyfiles_window_test.gd` | `tools/test_verifyfiles_window.py` | Existing launcher fixture/arguments and platform prerequisites retained. |
| `launcher/tests/write_failure_test.gd` | `tools/release/test_launcher.py` | Linux RLIMIT_FSIZE=4096 subprocess, ignored SIGXFSZ, --write-limited; both assertions must pass. |

The OAuth integration's missing-Node reason starts with **SKIP LOUDLY: launcher
OAuth integration needs Node.js with node:sqlite support**, and explains how to
set NODE/PATH. Run pytest with `-ra` or `-rs` to display it. The regression
`test_oauth_without_node_skips_loudly` empties PATH and removes NODE before
calling the actual collection wrapper; it must skip before starting fixtures.
When Node is available, this remains the real integration, not a mock pass.

## Game inventory

“Existing CI Godot gate” below means the already-scheduled gate identified by
the reviewer, not a new pytest invocation. “Manual only” means no automated
runner is claimed. All paths below are relative to the repository root.

| Script | Runner/consumer | Scheduling decision |
| --- | --- | --- |
| `godot/tests/app_icon_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: bundled application icon resources; headless, 20 s timeout. |
| `godot/tests/apt_canvas_pixel_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/audio_stress_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/audio_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/backlog3_focus_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/bfme1_castle_path_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/bfme1_hud_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/bfme1_launch_args_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: command-line parsing only; no game is started; headless, 20 s timeout. |
| `godot/tests/bfme1_map_memory_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/bfme1_plot_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/bfme1_quit_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/bfme1_test.gd` | Manual only; no generic pytest runner | Denied here: needs a private BFME1 installation. |
| `godot/tests/building_render_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/buildperf_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/cah_awards_display_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/cah_camera_retail_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/cah_camera_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: camera geometry against synthetic bounds; headless, 20 s timeout. |
| `godot/tests/camp2_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/campsave1_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/caster_director_test.gd` | `tools/caster/test_caster_godot.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/caster_director_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/caster_end_fixture.gd` | `tools/caster/test_caster_godot.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/caster_live_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/caster_overlay_test.gd` | `tools/caster/test_caster_godot.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/caster_overlay_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/caster_pip_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/caster_transport_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/caster_vision_test.gd` | `tools/caster/test_caster_godot.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/controller_main_menu_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/controller_menu_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: controller menu mapping against a stub shell; headless, 20 s timeout. |
| `godot/tests/controller_routing_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: controller input routing rules only; headless, 20 s timeout. |
| `godot/tests/crash_log_test.gd` | `tools/release/test_launcher.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/end_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/external_open_test.gd` | `tools/release/test_external_open.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/floating_money_text_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/formation_placement_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/friends_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/fx_render_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/gamma_composite_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/glow_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/hero_radius_cursor_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/hud_image_mode_pixel_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/hudredraw_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/install_paths_test.gd` | `tools/release/test_install_paths.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/language_warning_test.gd` | `tools/release/test_language_warning.py` | The unchecked-language warning box (#77): text, Copy details, the per-install dismissal and hostile `%` sequences; headless. |
| `godot/tests/menu_backdrop_resize_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and GPU/window/render readback; run both `BACKDROP_TEST_STYLE=rotwk` and `BACKDROP_TEST_STYLE=bfme2` explicitly. |
| `godot/tests/menufix2_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/menustyle_native_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/menustyle_settings_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: temporary Options.ini and shell/game spies; headless, 20 s timeout. |
| `godot/tests/menustyle_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/mixed_formation_placement_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/net_end_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/net_score_continue_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: score transitions against shell/audio stubs; headless, 20 s timeout. |
| `godot/tests/net_uber_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/netlag1_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/new_game_loading_art_host.gd` | `godot/tests/new_game_loading_art_test.gd` | CI gate's helper/child; not scheduled separately by pytest. |
| `godot/tests/new_game_loading_art_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/object_corpus_sweep.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/occlude1_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets (a retail model in W3DInstancer); `godot --headless --path godot --script res://tests/occlude1_test.gd`. |
| `godot/tests/online16_fix_bridge_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/online_branding_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/online_hostile_test.gd` | `tools/net/test_online_hostile.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_identity_test.gd` | `tools/net/test_online_identity.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_invite_test.gd` | `tools/net/test_online_invite.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_list_client_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: the native list client against synthetic responses; headless, 20 s timeout. |
| `godot/tests/online_list_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: online list parsing and paging against synthetic responses; headless, 20 s timeout. |
| `godot/tests/online_lobby_redirect_test.gd` | `tools/net/test_online_lobby_redirect.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_reports_test.gd` | `tools/net/test_online_reports.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_social_node.gd` | `godot/tests/online_social_node.tscn` | Scene helper; tools/net/test_online_social_node.py drives this scene. |
| `godot/tests/online_social_test.gd` | `tools/net/test_online_social_ui.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/online_stats_test.gd` | `tools/net/test_online_stats.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/opts1_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/opts2_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/opts4_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/opts5_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/opts6_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/opts6_wait_test.gd` | `tools/release/test_external_open.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/p16_ui_dictionary_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/p16_ui_legacy_shape_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/packed_model_texture_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/pathperf1_render_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/perf3_cull_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/pose_buffer_cull_query_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and an extension build; run by `godot/tests/pose_buffer_regression.py`. |
| `godot/tests/pose_buffer_query_trigger_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and an extension build; run by `godot/tests/pose_buffer_regression.py`. |
| `godot/tests/pose_buffer_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and an extension build; run by `godot/tests/pose_buffer_regression.py`. |
| `godot/tests/pingwheel_test.gd` | Manual only; no generic pytest runner | Denied here: mixed UI/timer checks and retail skirmish/hash runs; not wholly asset-free. |
| `godot/tests/platformicon1_test.gd` | `tools/platformicon/test_make_icons.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/play3_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/player_house_color_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/player_message_log_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/pro_camera_bokeh_test.gd` | Manual only; no generic pytest runner | Denied here: generated bokeh scene requires a GPU, window and render readback. |
| `godot/tests/pro_camera_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/radial_background_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/rejoin_watch_test.gd` | `tools/net/test_rejoin_watchdog.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/release_message_test.gd` | `tools/release/test_release_tools.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/render4_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/renderwait1_teardown_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/replay_args_test.gd` | `tools/release/test_replay_open.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/replay_refusal_test.gd` | Manual only; no generic pytest runner | Denied here: mixed stub checks and retail replay subprocess; not wholly asset-free. |
| `godot/tests/road_shader_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/road_shading_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/save4e_flow_test.gd` | `tools/save/test_save_flow.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/save_route_game.gd` | `godot/tests/save_route_test.gd` | Fixture of its runner; not scheduled separately by pytest. |
| `godot/tests/save_route_test.gd` | `tools/release/test_replay_open.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/saveui1_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/selperf_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/shader_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/shellfb1_cursor_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/shellfb1_spell_store_escape_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/shellfix1_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/smoke_checks.gd` | `godot/tests/smoke_test.gd` | CI gate's helper/child; not scheduled separately by pytest. |
| `godot/tests/smoke_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/social_privacy_walk.gd` | `godot/tests/social_walk.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/social_walk.gd` | `godot/scripts/game.gd` | Support/opt-in walk, not an independently scheduled test. |
| `godot/tests/spellbook_reload_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/spellbook_rightclick_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/start_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/stealth_gate_test.gd` | Existing CI Godot gate | Denied here: already scheduled by CI; use its existing asset/window setup. |
| `godot/tests/tangent_convention_test.gd` | Manual only; no generic pytest runner | Denied here: GPU/window/render readback required (some also need retail assets). |
| `godot/tests/terrain_lighting_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/timeline_markers_test.gd` | `tools/test_gdscript.py::test_game_script` | Allowlisted: score timeline markers against a stub shell; headless, 20 s timeout. |
| `godot/tests/uberjoin1_lan_test.gd` | `godot/tests/start_test.gd` | CI gate's helper/child; not scheduled separately by pytest. |
| `godot/tests/uberjoin1_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/ui4_resize_test.gd` | Manual only; no generic pytest runner | Denied here: needs private RotWK/BFME2 assets and game/frame setup. |
| `godot/tests/update_handover_test.gd` | `tools/release/test_onezip.py` | Existing argument-aware fixture runner; its prerequisites still apply. |
| `godot/tests/windows_crash_test.gd` | Manual only; no generic pytest runner | Denied here: destructive Windows debug crash/minidump probe; explicit opt-in required. |
| `godot/tests/water_normal_test.gd` | Manual only; no generic pytest runner | GPU rendering; synthetic textures, no retail assets. |
| `godot/tests/standing_water_test.gd` | Manual only; no generic pytest runner | Private retail assets and GPU water rendering. |
| `godot/tests/map_water_ini_error_test.gd` | Manual only; no generic pytest runner | Private retail assets; a synthetic mod `water.ini` over a retail map. |
| `godot/tests/ini_error_in_extension_test.gd` | Manual only; no generic pytest runner | Private retail assets; a synthetic mod object INI over a retail map. |

## OAuth readiness regression

Friend seeding can trigger `SocialClient`'s inbox-driven friends refresh. Creating a recovery code while that action owns the client emits `action_done("recovery", false)` and does not emit `recovery_ready`. The old integration ignored the rejection and waited for a code until timeout; no OAuth callback had run yet.

The test now waits for a signed-in, populated, idle social client, connects both terminal signals before issuing the request, and checks successful completion and a nonempty code. A controlled busy-client probe holds readiness until explicitly released, proves no request was sent while busy, and then proves exactly one successful request. There is no fixed sleep or larger timeout. The real Node/SQLite integration still exercises both Discord rounds and all callback, restore, unlink and offline-provider assertions.
