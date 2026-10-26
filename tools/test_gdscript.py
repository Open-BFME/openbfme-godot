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
GAME_OWNERS = {
    **dict.fromkeys(("caster_director_test.gd", "caster_end_fixture.gd", "caster_overlay_test.gd", "caster_vision_test.gd"),
                    "tools/caster/test_caster_godot.py"),
    "online_hostile_test.gd": "tools/net/test_online_hostile.py",
    "crash_log_test.gd": "tools/release/test_launcher.py",
    "external_open_test.gd": "tools/release/test_external_open.py",
    "install_paths_test.gd": "tools/release/test_install_paths.py",
    "opts6_wait_test.gd": "tools/release/test_external_open.py",
    "release_message_test.gd": "tools/release/test_release_tools.py",
    "replay_args_test.gd": "tools/release/test_replay_open.py",
    "save_route_test.gd": "tools/release/test_replay_open.py",
    "online_identity_test.gd": "tools/net/test_online_identity.py",
    "online_invite_test.gd": "tools/net/test_online_invite.py",
    "online_lobby_redirect_test.gd": "tools/net/test_online_lobby_redirect.py",
    "online_reports_test.gd": "tools/net/test_online_reports.py",
    "online_social_test.gd": "tools/net/test_online_social_ui.py",
    "online_stats_test.gd": "tools/net/test_online_stats.py",
    "rejoin_watch_test.gd": "tools/net/test_rejoin_watchdog.py",
    "platformicon1_test.gd": "tools/platformicon/test_make_icons.py",
    "update_handover_test.gd": "tools/release/test_onezip.py",
    "save4e_flow_test.gd": "tools/save/test_save_flow.py",
}
LAUNCHER_OWNERS = {
    **dict.fromkeys(("background_play_test.gd", "crypto_test.gd", "layout_test.gd", "macos_preview_test.gd", "oauth_test.gd",
                    "social_launch_test.gd", "social_ui_test.gd", "unit_test.gd", "write_failure_test.gd",
                    "oauth_integration_test.gd", "social_client_test.gd"), "tools/release/test_launcher.py"),
    "aio_progress_test.gd": "tools/release/test_aio.py",
    "bfme1_assets_test.gd": "tools/release/test_aio.py",
    "bfme1_offer_test.gd": "tools/release/test_aio.py",
    "bounded_read_test.gd": "tools/release/test_onezip.py",
    "instance_lock_test.gd": "tools/release/test_instance_lock.py",
    "instance_lock_parser_test.gd": "tools/release/test_instance_lock.py",
    "macos_cache_test.gd": "tools/release/test_macplay.py",
    "macos_helper_probe_test.gd": "tools/release/test_macplay.py",
    "macos_platform_test.gd": "tools/release/test_macos_platform.py",
    "macplay_test.gd": "tools/release/test_macplay.py",
    "external_open_test.gd": "tools/release/test_external_open.py",
    "first_run_test.gd": "launcher/tests/test_first_run.py",
    "install_paths_test.gd": "tools/release/test_install_paths.py",
    "lobby_list_test.gd": "tools/release/test_launcher.py",
    "log_privacy_test.gd": "tools/release/test_launcher.py",
    "mod_catalog_test.gd": "tools/release/test_mods.py",
    "mods_window_test.gd": "tools/release/test_mods.py",
    "replay_association_test.gd": "tools/release/test_replay_open.py",
    "replay_open_test.gd": "tools/release/test_replay_open.py",
    "update_fault_test.gd": "tools/release/test_launcher_updates.py",
    "update_status_test.gd": "tools/release/test_launcher.py",
}
# Fixtures, opt-in rendered walks, and the LAN child launched by start_test.gd.
SUPPORT = {
    **dict.fromkeys(("caster_transport_walk.gd", "caster_director_walk.gd", "caster_overlay_walk.gd",
                    "caster_pip_walk.gd", "caster_live_walk.gd", "friends_walk.gd", "social_walk.gd"),
                   "godot/scripts/game.gd"),
    "social_privacy_walk.gd": "godot/tests/social_walk.gd",
    "new_game_loading_art_host.gd": "godot/tests/new_game_loading_art_test.gd",
    "online_social_node.gd": "godot/tests/online_social_node.tscn",
    "smoke_checks.gd": "godot/tests/smoke_test.gd",
    "uberjoin1_lan_test.gd": "godot/tests/start_test.gd",
    "save_route_game.gd": "godot/tests/save_route_test.gd",
}


def scripts(names: str) -> set[str]:
    return {name + "_test.gd" for name in names.split()}


# Allowlist: complete checks with no retail data, renderer readback, window or
# frame-delay assumptions. Each must finish within 20 s on a 6-core/6-GiB host.
ALLOWLIST = {
    "app_icon_test.gd": "bundled application icon resources",
    "bfme1_launch_args_test.gd": "command-line parsing only; no game is started",
    "cah_camera_test.gd": "camera geometry against synthetic bounds",
    "menustyle_settings_test.gd": "temporary Options.ini and shell/game spies",
    "net_score_continue_test.gd": "score transitions against shell/audio stubs",
    "controller_menu_test.gd": "controller menu mapping against a stub shell",
    "controller_routing_test.gd": "controller input routing rules only",
    "online_list_test.gd": "online list parsing and paging against synthetic responses",
    "online_list_client_test.gd": "the native list client against synthetic responses",
}
SCRIPT_TIMEOUT = 20

# Denylist part 1: the reviewer's existing 12 CI Godot gates. The external CI
# runner supplies their assets/window/setup; pytest must never duplicate them.
CI_GATES = scripts("""
    smoke start end net_end stealth_gate audio opts1 opts2 saveui1 opts6
    new_game_loading_art menufix2
""")

# Denylist part 2: manual dedicated invocations only, not pytest skips. Having
# retail paths, a GPU or an opt-in environment variable must not schedule these.
DENYLIST = {
    **dict.fromkeys(scripts("bfme1_hud bfme1_map_memory bfme1_plot bfme1_quit bfme1"),
                    "manual: needs a private BFME1 installation"),
    **dict.fromkeys(scripts("""
        audio_stress backlog3_focus cah_awards_display camp2 campsave1 floating_money_text
        hero_radius_cursor hudredraw menustyle_native menustyle net_uber netlag1 opts4 opts5
        pathperf1_render perf3_cull play3 player_house_color player_message_log
        renderwait1_teardown selperf shellfix1 spellbook_reload terrain_lighting uberjoin1 ui4_resize
        controller_main_menu formation_placement mixed_formation_placement online16_fix_bridge p16_ui_dictionary
        p16_ui_legacy_shape spellbook_rightclick
    """) | {"object_corpus_sweep.gd"}, "manual: needs private RotWK/BFME2 assets and game/frame setup"),
    **dict.fromkeys(scripts("""
        apt_canvas_pixel building_render fx_render gamma_composite hud_image_mode_pixel
        online_branding pro_camera radial_background render4 shader
    """), "manual: GPU/window/render readback required (some also need retail assets)"),
    "pingwheel_test.gd": "manual: mixed UI/timer checks and retail skirmish/hash runs; not wholly asset-free",
    "replay_refusal_test.gd": "manual: mixed stub checks and retail replay subprocess; not wholly asset-free",
    "windows_crash_test.gd": "manual: destructive Windows debug crash/minidump probe; explicit opt-in required",
}
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
            source = (REPO / runner).read_text(encoding="utf-8")
            # Launcher integration entry points delegate to their argument-aware CLI runners.
            if name in {"oauth_integration_test.gd", "social_client_test.gd"}:
                delegate = "test_launcher_oauth.py" if name.startswith("oauth") else "test_launcher_social.py"
                assert delegate in source
                source = (REPO / "tools/release" / delegate).read_text(encoding="utf-8")
            assert name in source, f"{runner} no longer invokes {name}"
    for name, consumer in SUPPORT.items():
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
