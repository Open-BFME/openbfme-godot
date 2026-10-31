"""Inventory every launcher/game script; run only fast, asset-free headless game checks.

Existing CI gates, retail/rendered tests and destructive probes are NOT scheduled
here, even when their prerequisites happen to be installed. See docs/GDSCRIPT_TESTS.md.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
GAME = REPO / "godot"

# These need fixture arguments or synthetic servers supplied by an existing runner.
GAME_OWNERS = {'caster_director_test.gd': 'tools/caster/test_caster_godot.py',
 'caster_end_fixture.gd': 'tools/caster/test_caster_godot.py',
 'caster_overlay_test.gd': 'tools/caster/test_caster_godot.py',
 'caster_vision_test.gd': 'tools/caster/test_caster_godot.py',
 'online_hostile_test.gd': 'tools/net/test_online_hostile.py',
 'online_identity_test.gd': 'tools/net/test_online_identity.py',
 'online_invite_test.gd': 'tools/net/test_online_invite.py',
 'online_lobby_redirect_test.gd': 'tools/net/test_online_lobby_redirect.py',
 'online_reports_test.gd': 'tools/net/test_online_reports.py',
 'online_social_test.gd': 'tools/net/test_online_social_ui.py',
 'online_stats_test.gd': 'tools/net/test_online_stats.py',
 'platformicon1_test.gd': 'tools/platformicon/test_make_icons.py',
 'rejoin_watch_test.gd': 'tools/net/test_rejoin_watchdog.py',
 'save4e_flow_test.gd': 'tools/save/test_save_flow.py',
 'update_handover_test.gd': 'tools/release/test_onezip.py'}
LAUNCHER_OWNERS = {'aio_progress_test.gd': 'tools/release/test_aio.py',
 'background_play_test.gd': 'tools/release/test_launcher.py',
 'bfme1_assets_test.gd': 'tools/release/test_aio.py',
 'bfme1_offer_test.gd': 'tools/release/test_aio.py',
 'bounded_read_test.gd': 'tools/release/test_onezip.py',
 'crypto_test.gd': 'tools/release/test_launcher.py',
 'instance_lock_parser_test.gd': 'tools/release/test_instance_lock.py',
 'instance_lock_test.gd': 'tools/release/test_instance_lock.py',
 'layout_test.gd': 'tools/release/test_launcher.py',
 'lobby_list_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'macos_cache_test.gd': 'tools/release/test_macplay.py',
 'macos_helper_probe_test.gd': 'tools/release/test_macplay.py',
 'macos_platform_test.gd': 'tools/release/test_macos_platform.py',
 'macos_preview_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'macplay_test.gd': 'tools/release/test_macplay.py',
 'oauth_integration_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'oauth_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'social_client_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'social_launch_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'social_ui_test.gd': 'manual: existing legacy scenario; no runner in this branch',
 'unit_test.gd': 'tools/release/test_launcher.py'}
# Fixtures, opt-in rendered walks, and the LAN child launched by start_test.gd.
SUPPORT = {'caster_director_walk.gd': 'godot/scripts/game.gd',
 'caster_live_walk.gd': 'godot/scripts/game.gd',
 'caster_overlay_walk.gd': 'godot/scripts/game.gd',
 'caster_pip_walk.gd': 'godot/scripts/game.gd',
 'caster_transport_walk.gd': 'godot/scripts/game.gd',
 'friends_walk.gd': 'godot/scripts/game.gd',
 'new_game_loading_art_host.gd': 'godot/tests/new_game_loading_art_test.gd',
 'online_social_node.gd': 'godot/tests/online_social_node.tscn',
 'smoke_checks.gd': 'godot/tests/smoke_test.gd',
 'social_privacy_walk.gd': 'godot/tests/social_walk.gd',
 'social_walk.gd': 'godot/scripts/game.gd',
 'uberjoin1_lan_test.gd': 'godot/tests/start_test.gd'}


def scripts(names: str) -> set[str]:
    return {name + "_test.gd" for name in names.split()}


# Allowlist: complete checks with no retail data, renderer readback, window or
# frame-delay assumptions. Each must finish within 20 s on a 6-core/6-GiB host.
ALLOWLIST = {'app_icon_test.gd': 'bundled application icon resources',
 'bfme1_launch_args_test.gd': 'command-line parsing only; no game is started',
 'cah_camera_test.gd': 'camera geometry against synthetic bounds',
 'menustyle_settings_test.gd': 'temporary Options.ini and shell/game spies',
 'net_score_continue_test.gd': 'score transitions against shell/audio stubs',
 'online_list_client_test.gd': 'the native list client against synthetic responses',
 'online_list_test.gd': 'online list parsing and paging against synthetic responses'}
SCRIPT_TIMEOUT = 20

# Denylist part 1: the reviewer's existing 12 CI Godot gates. The external CI
# runner supplies their assets/window/setup; pytest must never duplicate them.
CI_GATES = {'audio_test.gd',
 'end_test.gd',
 'menufix2_test.gd',
 'net_end_test.gd',
 'new_game_loading_art_test.gd',
 'opts1_test.gd',
 'opts2_test.gd',
 'opts6_test.gd',
 'saveui1_test.gd',
 'smoke_test.gd',
 'start_test.gd',
 'stealth_gate_test.gd'}

# Denylist part 2: manual dedicated invocations only, not pytest skips. Having
# retail paths, a GPU or an opt-in environment variable must not schedule these.
DENYLIST = {'apt_canvas_pixel_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'audio_stress_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'backlog3_focus_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'bfme1_audio_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_building_floor_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_castle_path_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_hud_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_live_plot_zoom_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_live_tooltip_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_map_memory_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_music_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_options_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_plot_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_plot_zoom_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_quit_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_resource_farm_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_save_folder_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_score_screen_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_script_ai_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_skirmish_menu_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_starting_armies_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_startup_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_test.gd': 'manual: needs a private BFME1 installation',
 'bfme1_wall_upgrade_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'bfme1_weather_test.gd': 'manual: private BFME1 installation, rebuilt extension and live game/rendering',
 'building_render_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'cah_awards_display_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'camp2_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'campsave1_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'floating_money_text_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'fx_render_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'gamma_composite_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'hero_radius_cursor_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'hud_image_mode_pixel_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'hudredraw_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'menustyle_native_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'menustyle_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'net_uber_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'netlag1_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'object_corpus_sweep.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'online_branding_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'opts4_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'opts5_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'pathperf1_render_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'perf3_cull_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'pingwheel_test.gd': 'manual: mixed UI/timer checks and retail skirmish/hash runs; not wholly asset-free',
 'play3_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'player_house_color_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'player_message_log_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'pro_camera_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'radial_background_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'render4_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'renderwait1_teardown_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'replay_refusal_test.gd': 'manual: mixed stub checks and retail replay subprocess; not wholly asset-free',
 'selperf_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'shader_test.gd': 'manual: GPU/window/render readback required (some also need retail assets)',
 'shellfix1_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'spellbook_reload_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'terrain_lighting_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'uberjoin1_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'ui4_resize_test.gd': 'manual: needs private RotWK/BFME2 assets and game/frame setup',
 'windows_crash_test.gd': 'manual: destructive Windows debug crash/minidump probe; explicit opt-in required'}
ENGINE_ERRORS = re.compile(r"(?m)^\s*(?:SCRIPT ERROR|ERROR):")


def test_every_gdscript_is_inventoried():
    inventory = (REPO / "docs/GDSCRIPT_TESTS.md").read_text(encoding="utf-8")
    for project, owners in (("launcher", LAUNCHER_OWNERS), ("godot", GAME_OWNERS)):
        found = {p.name for p in (REPO / project / "tests").glob("*.gd")}
        groups = [set(owners)] if project == "launcher" else [set(owners), set(SUPPORT), set(ALLOWLIST), CI_GATES, set(DENYLIST)]
        owned = set().union(*groups)
        assert found == owned, f"{project}: uninventoried={sorted(found - owned)}, stale={sorted(owned - found)}"
        assert sum(map(len, groups)) == len(owned), "a script has more than one owner"
        for name in found:
            assert f"| `{project}/tests/{name}` |" in inventory, f"undocumented script: {project}/{name}"
        for name, runner in owners.items():
            if runner.startswith("manual: "):
                assert runner.removeprefix("manual: ").strip(), "manual scenarios need a reason"
                continue
            source = (REPO / runner).read_text(encoding="utf-8")
            # Launcher integration entry points delegate to their argument-aware CLI runners.
            if name in {"oauth_integration_test.gd", "social_client_test.gd"}:
                delegate = "test_launcher_oauth.py" if name.startswith("oauth") else "test_launcher_social.py"
                assert delegate in source
                source = (REPO / "tools/release" / delegate).read_text(encoding="utf-8")
            assert name in source, f"{runner} no longer invokes {name}"
    for name, consumer in SUPPORT.items():
        if consumer.startswith("manual: "):
            continue
        assert name in (REPO / consumer).read_text(encoding="utf-8"), f"unused fixture: {name}"
    assert "res://tests/online_social_node.tscn" in (REPO / "tools/net/test_online_social_node.py").read_text()
    assert all(DENYLIST.values()), "every exclusion needs a reason"
    for name in ALLOWLIST:
        assert re.search(r"^extends SceneTree\s*$", (GAME / "tests" / name).read_text(), re.MULTILINE), name


@pytest.fixture(scope="module")
def godot(tmp_path_factory):
    binary = shutil.which(os.environ.get("GODOT", "godot"))
    if not binary:
        pytest.skip("SKIP LOUDLY: Godot 4.7.2 is required")
    version = subprocess.run([binary, "--version"], capture_output=True, text=True, timeout=15, check=True).stdout
    if not version.startswith("4.7.2."):
        pytest.skip(f"SKIP LOUDLY: Godot 4.7.2 required; found {version.strip()}")
    extension = {"linux": "openbfme.linux.template_debug.x86_64.so", "win32": "openbfme.windows.template_debug.x86_64.dll",
                 "darwin": "openbfme.macos.template_debug.universal.dylib"}.get(sys.platform)
    libraries = [GAME / "bin" / extension] if extension else []
    if sys.platform == "darwin":
        libraries.append(GAME / "bin/openbfme.macos.template_debug.arm64.dylib")
    if not any(path.is_file() for path in libraries):
        pytest.skip("SKIP LOUDLY: build the host OpenBFME GDExtension before running game scripts")
    result = subprocess.run([binary, "--headless", "--path", str(GAME), "--editor", "--import"],
                            env=isolated_environment(tmp_path_factory.mktemp("gdscript-import")),
                            capture_output=True, text=True, timeout=30, check=False)
    output = result.stdout + result.stderr
    assert result.returncode == 0 and not ENGINE_ERRORS.search(output), output
    return binary


def isolated_environment(tmp_path):
    env = dict(os.environ, HOME=str(tmp_path), USERPROFILE=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"),
               XDG_CONFIG_HOME=str(tmp_path / "config"), XDG_CACHE_HOME=str(tmp_path / "cache"),
               APPDATA=str(tmp_path / "appdata"), LOCALAPPDATA=str(tmp_path / "localappdata"),
               OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"))
    for name in ("BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "DISPLAY", "WAYLAND_DISPLAY"):
        env.pop(name, None)
    return env


def check_result(result):
    output = result.stdout + result.stderr
    # A Godot assert/parse error can otherwise leave a process exiting zero.
    assert not ENGINE_ERRORS.search(output), output
    assert result.returncode == 0 and not re.search(r"\bSKIP\b", output), output


@pytest.mark.parametrize("script", sorted(ALLOWLIST))
def test_game_script(script, godot, tmp_path):
    command = [godot, "--headless", "--path", str(GAME), "--script", f"res://tests/{script}"]
    result = subprocess.run(command, env=isolated_environment(tmp_path), capture_output=True, text=True,
                            timeout=SCRIPT_TIMEOUT, check=False)
    check_result(result)


def test_game_environment_excludes_retail_and_window(monkeypatch, tmp_path):
    names = ("BFME1_INSTALL", "BFME2_INSTALL", "ROTWK_INSTALL", "DISPLAY", "WAYLAND_DISPLAY")
    for name in names:
        monkeypatch.setenv(name, "must-not-be-used")
    assert not set(names) & isolated_environment(tmp_path).keys()


@pytest.mark.parametrize(("status", "output"), [(1, "FAIL"), (0, "SCRIPT ERROR: assertion failed"),
                                             (77, "SCRIPT ERROR: parse failed\nSKIP"), (0, "SKIP: incomplete checks"), (77, "SKIP")])
def test_game_failure_is_not_a_skip(status, output):
    with pytest.raises(AssertionError):
        check_result(subprocess.CompletedProcess([], status, output, ""))
