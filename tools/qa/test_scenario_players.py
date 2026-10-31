"""No retail files or GDExtension: exercise each real player's setup/dispatch
with a temporary synthetic scenario in an isolated Godot project.

GODOT=/path/to/Godot_v4.7.2-stable_linux.x86_64 python3 -m pytest tools/qa/test_scenario_players.py -q
"""
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
SCRIPTS = REPO / "godot/scripts"
# The movie is gameplay, not a harness. The QA survey has a Node-based adapter
# tested separately below; scenario_player.gd is the shared base itself.
NON_SCENARIO_PLAYERS = {"movie_player.gd", "qa_player.gd", "scenario_player.gd"}
PLAYERS = sorted(p.name for p in SCRIPTS.glob("*_player.gd") if p.name not in NON_SCENARIO_PLAYERS)
SPECIAL = {
    "gamemodes1_player.gd": ("_battle", '"battle"', "battle"),
    "rules1_player.gd": ("_heroes", '"GondorBoromir"', "heroes"),
    "uber1_player.gd": ("_battle", "12", "battle 12"),
}
# players whose _scenario_label() is not the file name's (the prefix their own gates read)
LABELS = {"ping_wheel_player.gd": "PINGWHEEL"}
FAKE_GAME = """extends Node
var _hud: Node = self
var _world: Node = self
var _shell: Node = self
var _faction := "synthetic"
var _warp := 0.0
class FakePings extends Node3D:
    var beacon_model := "synthetic"
    var errors := []
var _ping_wheel := CanvasLayer.new()
var _pings := FakePings.new()
func _init() -> void:
    add_child(_ping_wheel)
    add_child(_pings)
func _step(_frames: int) -> void:
    await get_tree().process_frame
"""
ENTRY = """extends SceneTree
func _initialize() -> void:
    root.size = Vector2i(1280, 720)
    _test.call_deferred()
func _test() -> void:
    var game = load("res://fake_game.gd").new()
    root.add_child(game)
    var player = load("res://scripts/%s").new()
    player.scenario_timeout_seconds = %s
    await player.run(game, %s)
    print("COUNTS: %%d ok, %%d failed" %% [player.ok_count, player.fail_count])
    quit(0 if player.fail_count == 0 else 1)
"""


@pytest.fixture(scope="module")
def godot():
    binary = shutil.which(os.environ.get("GODOT", "godot"))
    if binary is None:
        if os.environ.get("GODOT"):
            pytest.fail("GODOT is set but does not name an executable")
        pytest.skip("SKIP LOUDLY: set GODOT to the documented Godot 4.7.2 editor; no retail files needed")
    return binary


def project(tmp_path, player):
    scripts = tmp_path / "scripts"
    scripts.mkdir()
    for name in [*PLAYERS, "scenario_player.gd", "qa_player.gd", "qa_scene.gd"]:
        shutil.copyfile(SCRIPTS / name, scripts / name)
    (tmp_path / "project.godot").write_text('config_version=5\n[application]\nconfig/name="Scenario runner regression"\n')
    (tmp_path / "fake_game.gd").write_text(FAKE_GAME)
    source = (scripts / player).read_text()
    if player in SPECIAL:
        method, request, name = SPECIAL[player]
    else:
        method = re.search(r"^func (_scenario_\w+)\(", source, re.M)[1]
        name = method.removeprefix("_scenario_")
        request = f'PackedStringArray(["{name}"])'
    return scripts / player, method, request, name


def replace_method(path, method, body, untyped=False):
    source = path.read_text()
    pattern = rf"(^func {method}\([^\n]*\))( -> \w+)(:\n)(?:(?:\t[^\n]*|)[\n]|\t[^\n]*$)*"
    source, count = re.subn(pattern, lambda m: m[1] + ("" if untyped else m[2]) + m[3] + body + "\n\n", source, count=1, flags=re.M)
    assert count == 1, method
    path.write_text(source)


def run(godot, tmp_path, player, request, timeout="5.0"):
    (tmp_path / "entry.gd").write_text(ENTRY % (player, timeout, request))
    result = subprocess.run(
        [godot, "--headless", "--path", str(tmp_path), "--script", "res://entry.gd"],
        capture_output=True, text=True, timeout=20,
    )
    return result.returncode, result.stdout + result.stderr


@pytest.mark.parametrize("player", PLAYERS)
@pytest.mark.parametrize("mode", ["pass", "assertion_failure", "script_error", "async_script_error", "helper_error", "no_result", "assert_error"])
def test_each_player_fails_closed(godot, tmp_path, player, mode):
    path, method, request, name = project(tmp_path, player)
    before = "\tvar missing: Variant = null\n\tmissing.deliberate_script_error()\n"
    bodies = {
        "pass": '\tawait game.get_tree().process_frame\n\treturn ""\n',
        "assertion_failure": '\treturn "original assertion failed"\n',
        "script_error": before + '\treturn ""\n',
        "async_script_error": '\tawait game.get_tree().process_frame\n' + before + '\treturn ""\n',
        "helper_error": '\t_broken_helper()\n\treturn ""\n',
        "no_result": '\tawait game.get_tree().process_frame\n',
        "assert_error": '\tawait game.get_tree().process_frame\n\tassert(false, "deliberate assertion error")\n\treturn ""\n',
    }
    replace_method(path, method, bodies[mode], untyped=(mode == "no_result"))
    if mode == "helper_error":
        with path.open("a") as f:
            f.write('\nfunc _broken_helper() -> void:\n' + before)
    code, out = run(godot, tmp_path, player, request)
    prefix = LABELS.get(player, player.removesuffix("_player.gd").upper())
    if mode == "pass":
        assert code == 0, out
        assert f"{prefix} {name}: ok" in out, out
        assert f"{prefix} RESULT: 1 ok, 0 failed" in out, out
        assert "COUNTS: 1 ok, 0 failed" in out, out
        assert "SCRIPT ERROR" not in out, out
    else:
        assert code == 1, out
        assert f"{prefix} {name}: FAIL" in out, out
        assert f"{prefix} RESULT: 0 ok, 1 failed" in out, out
        assert f"{prefix} {name}: ok" not in out, out
        if "error" in mode:
            assert "script error:" in out, out
        elif mode == "no_result":
            assert "returned no String result" in out, out
        else:
            assert "original assertion failed" in out, out


@pytest.mark.parametrize("mode", ["missing", "empty", "duplicate", "incomplete", "timeout", "setup_error", "setup_async_error", "result_mismatch"])
def test_runner_accounts_for_every_requested_scenario(godot, tmp_path, mode):
    player = "play1_player.gd"
    path, method, request, name = project(tmp_path, player)
    replace_method(path, method, '\treturn ""\n')
    if mode == "missing":
        request = 'PackedStringArray(["does_not_exist"])'
        name = "does_not_exist"
    elif mode == "empty":
        request = 'PackedStringArray()'
        name = "<empty scenario list>"
    elif mode == "duplicate":
        request = f'PackedStringArray(["{name}", "{name}"])'
    elif mode == "incomplete":
        request = f'PackedStringArray(["{name}", "unreported"])'
        replace_method(path, "_run", '\treturn\n')
    elif mode == "timeout":
        replace_method(path, method, '\tawait game.get_tree().create_timer(60.0).timeout\n\treturn ""\n')
    elif mode in ("setup_error", "setup_async_error"):
        replace_method(path, "_run", ('\tawait p_game.get_tree().process_frame\n' if mode == "setup_async_error" else '')
                       + '\tvar missing: Variant = null\n\tmissing.deliberate_script_error()\n')
    elif mode == "result_mismatch":
        replace_method(path, "_run", '\tawait _run_scenarios(PackedStringArray(["not_requested"]))\n')
    code, out = run(godot, tmp_path, player, request, timeout="0.1" if mode == "timeout" else "5.0")
    if mode == "duplicate":
        assert code == 0, out
        assert out.count(f"PLAY1 {name}: ok") == 2, out
        assert "PLAY1 RESULT: 2 ok, 0 failed" in out, out
    else:
        assert code == 1, out
        assert f"PLAY1 {name}: FAIL" in out, out
        assert "PLAY1 RESULT: 0 ok," in out, out
        if mode == "incomplete":
            assert "PLAY1 unreported: FAIL" in out, out
            assert "PLAY1 RESULT: 0 ok, 2 failed" in out, out
        if mode == "timeout":
            assert "did not complete within" in out, out


def test_every_scenario_harness_uses_the_shared_runner():
    """A newly added player cannot silently keep the old, permissive run loop."""
    def guarded(name):
        source = (SCRIPTS / name).read_text()
        parent = re.search(r'^extends "res://scripts/([^"/]+)"', source, re.M)
        return parent is not None and (parent[1] == "scenario_player.gd" or guarded(parent[1]))

    for player in PLAYERS:
        assert guarded(player), player
        assert "func run(" not in (SCRIPTS / player).read_text(), player


@pytest.mark.parametrize("mode", ["extra_result", "error_after_result", "timeout_after_result"])
def test_runner_cannot_pass_after_results_if_it_dies_or_never_finishes(godot, tmp_path, mode):
    player = "play1_player.gd"
    path, method, request, name = project(tmp_path, player)
    replace_method(path, method, '\treturn ""\n')
    source = path.read_text()
    after = {
        "extra_result": '\t_record_scenario("")\n',
        "error_after_result": '\tvar missing: Variant = null\n\tmissing.deliberate_script_error()\n',
        "timeout_after_result": '\tawait game.get_tree().create_timer(60.0).timeout\n',
    }[mode]
    source = source.replace('\tawait _run_scenarios(scenarios)\n', '\tawait _run_scenarios(scenarios)\n' + after, 1)
    path.write_text(source)
    code, out = run(godot, tmp_path, player, request, timeout="0.1" if mode == "timeout_after_result" else "5.0")
    assert code == 1, out
    assert f"PLAY1 {name}: FAIL" in out, out
    assert "PLAY1 RESULT: 1 ok, 1 failed" in out, out


def test_runner_preserves_completed_results_and_names_unreported_scenarios(godot, tmp_path):
    player = "play1_player.gd"
    path, method, request, name = project(tmp_path, player)
    replace_method(path, method, '\treturn ""\n')
    with path.open("a") as f:
        f.write('\nfunc _scenario_broken() -> String:\n\tawait game.get_tree().process_frame\n'
                '\tvar missing: Variant = null\n\tmissing.deliberate_script_error()\n\treturn ""\n')
    request = f'PackedStringArray(["{name}", "broken", "unreported"])'
    code, out = run(godot, tmp_path, player, request)
    assert code == 1, out
    assert f"PLAY1 {name}: ok" in out, out
    assert "PLAY1 broken: FAIL script error:" in out, out
    assert "PLAY1 unreported: FAIL script error:" in out, out
    assert "PLAY1 RESULT: 1 ok, 2 failed" in out, out


@pytest.mark.parametrize("timeout", ["0.0", "-1.0", "NAN", "INF"])
def test_invalid_deadline_fails_closed(godot, tmp_path, timeout):
    player = "play1_player.gd"
    project(tmp_path, player)
    code, out = run(godot, tmp_path, player, 'PackedStringArray(["unused"])', timeout=timeout)
    assert code == 1, out
    assert "PLAY1 configuration: FAIL scenario timeout must be finite and positive" in out, out


def test_script_error_in_shared_helper_cannot_be_hidden_by_a_successful_return(godot, tmp_path):
    player = "play1_player.gd"
    path, method, request, name = project(tmp_path, player)
    helper = tmp_path / "fake_game.gd"
    helper.write_text(FAKE_GAME.replace('    await get_tree().process_frame\n',
                      '    await get_tree().process_frame\n    var missing: Variant = null\n    missing.deliberate_script_error()\n'))
    replace_method(path, method, '\tawait game._step(1)\n\treturn ""\n')
    code, out = run(godot, tmp_path, player, request)
    assert code == 1, out
    assert f"PLAY1 {name}: FAIL script error:" in out, out
    assert "fake_game.gd" in out, out
    assert "PLAY1 RESULT: 0 ok, 1 failed" in out, out


@pytest.mark.parametrize("entry", ["play", "watch", "scene"])
@pytest.mark.parametrize("mode", ["pass", "script_error", "async_script_error", "helper_error", "no_summary", "no_result", "timeout"])
def test_qa_entry_point_uses_the_same_guard(godot, tmp_path, entry, mode):
    project(tmp_path, "play1_player.gd")
    qa = tmp_path / ("scripts/qa_scene.gd" if entry == "scene" else "scripts/qa_player.gd")
    error = '\tvar missing: Variant = null\n\tmissing.deliberate_script_error()\n'
    bodies = {
        "pass": '\tawait get_tree().process_frame\n\treturn {"complete": true}\n',
        "script_error": error + '\treturn {"complete": true}\n',
        "async_script_error": '\tawait get_tree().process_frame\n' + error + '\treturn {"complete": true}\n',
        "helper_error": '\t_broken_helper()\n\treturn {"complete": true}\n',
        "no_summary": '\treturn {}\n',
        "no_result": '\tawait get_tree().process_frame\n',
        "timeout": '\tawait get_tree().create_timer(60.0).timeout\n\treturn {"complete": true}\n',
    }
    replace_method(qa, entry, bodies[mode], untyped=(mode == "no_result"))
    if mode == "helper_error":
        with qa.open("a") as f:
            f.write('\nfunc _broken_helper() -> void:\n' + error)
    # Retain the production entry point, setup/dispatch, and no-summary check.
    # Only the retail-dependent score/lobby flow is replaced with a synthetic end.
    game = (SCRIPTS / "game.gd").read_text()
    wrapper = game[game.index("func _run_qa() -> void:"):game.index('func _run_qa_scenario() -> String:')]
    scenario = game[game.index('func _run_qa_scenario() -> String:'):game.index('\tsummary["real_seconds"] =')]
    synthetic = '''extends Node
var _net_state := ""
var _net_host := 0
var _net_join := ""
var _lan_mode := ""
var _replay_file := ""
var _hud: Node = self
var _world: Node = self
var _qa_scene := "%s"
var _local_name := "synthetic"
var _qa_minutes := 1.0
var _screens := ""
var _qa_tag := "synthetic"
var _qa_cash := 0
var _qa_speed := 1.0
var _qa_idle := %s
func is_logic_held() -> bool:
    return false
func set_time_scale(_scale: float) -> void:
    pass
func _fail(_reason: String) -> void:
    get_tree().quit(1)
''' % ("synthetic" if entry == "scene" else "", "true" if entry == "watch" else "false")
    (tmp_path / "qa_driver.gd").write_text(synthetic.replace("    ", "\t") + wrapper + scenario + '\treturn ""\n')
    (tmp_path / "entry.gd").write_text('''extends SceneTree
func _initialize() -> void:
    _test.call_deferred()
func _test() -> void:
    var driver = load("res://qa_driver.gd").new()
    root.add_child(driver)
    await driver._run_qa()
''')
    result = subprocess.run([godot, "--headless", "--path", str(tmp_path), "--script", "res://entry.gd",
                             "--", "--scenario-timeout=0.1" if mode == "timeout" else "--scenario-timeout=5.0"],
                            capture_output=True, text=True, timeout=20)
    out = result.stdout + result.stderr
    if mode == "pass":
        assert result.returncode == 0, out
        assert "QA-RUN synthetic: ok" in out, out
        assert "QA-RUN RESULT: 1 ok, 0 failed" in out, out
    else:
        assert result.returncode == 1, out
        assert "QA-RUN synthetic: FAIL" in out, out
        assert "QA-RUN RESULT: 0 ok, 1 failed" in out, out
        assert "QA-RUN synthetic: ok" not in out, out
