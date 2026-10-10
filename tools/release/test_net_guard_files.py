"""Lane WINCRASH-1: the only file-manager call the game may make (tools/release/net_guard.py): release.gd's _show_in_file_manager, by tokens."""
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import net_guard  # noqa: E402

RELEASE = "godot/scripts/release/release.gd"
OWNER = "func _show_in_file_manager(path: String) -> Error:\n\treturn OS.shell_show_in_file_manager(path, true)\n"


def calls(text: str, path: str | None = None) -> list[str]:
    return [f for f in net_guard.gdscript_findings(text, path) if "shell_" in f]


def test_only_the_owner_function_of_release_gd_may_call_the_file_manager():
    assert calls(OWNER, RELEASE) == []
    assert calls(OWNER.replace("shell_show_in_file_manager", "shell_open"), RELEASE) == []
    # the same function in another file, another function of release.gd, a top-level statement
    assert calls(OWNER, "godot/scripts/game.gd") == ["line 2: OS.shell_show_in_file_manager"]
    assert calls(OWNER.replace("func _show_in_file_manager(", "func open_log_folder("), RELEASE) == ["line 2: OS.shell_show_in_file_manager"]
    assert calls(OWNER + "var x = OS.shell_open\n", RELEASE) == ["line 3: OS.shell_open"]
    # an inner class's method of the same name is not the owner (its func is indented: the class at column 0 ends the owner)
    inner = OWNER + "class Inner:\n\tfunc _show_in_file_manager(p):\n\t\tOS.shell_open(p)\n"
    assert calls(inner, RELEASE) == ["line 5: OS.shell_open"]


def test_sol_r3_bypasses_fail():
    """wincrash1-review/r3: a URL reassigned to the allowed variable, a parameter shadowing the allowed const, a fake const inside a multiline
    string, a safe-looking comment after a spaced call; and every argument shape r2 / r3 listed."""
    review = Path.home() / ".cache/openbfme-recover/wincrash1-review/r3"  # Sol r3 probe files, when this machine has them
    for name in ("comment.gd", "fake.gd", "shadow.gd"):
        if (review / name).is_file():
            assert calls((review / name).read_text()), name
    comment = 'func _init():\n\tOS .shell_open("https://example.com") # OS.shell_open(ProjectSettings.globalize_path("user://logs"))\n'
    fake = 'var doc := """\nfunc _show_in_file_manager(p):\n"""\nfunc _init():\n\tOS.shell_open("https://example.com")\n'
    shadow = 'const LOG_DIR := "user://logs"\nfunc candidate(LOG_DIR: String):\n\tOS.shell_open(ProjectSettings.globalize_path(LOG_DIR))\n'
    reassigned = 'func open_log_file(name):\n\tvar log_file_path := "https://example.com"\n\tOS.shell_show_in_file_manager(log_file_path, true)\n'
    for text in (comment, fake, shadow, reassigned):
        assert calls(text, RELEASE), text
        assert calls(text), text
    for arg in ('ProjectSettings.globalize_path("user://logs" if false else "https://example.com")',
                'ProjectSettings.globalize_path("user://logs/../../tmp")', 'ProjectSettings.globalize_path("https://example.com")',
                'ProjectSettings.globalize_path("\\\\\\\\server\\\\share")', 'ProjectSettings.globalize_path("user://" + target)',
                'ProjectSettings.globalize_path("user://%s" % target)', 'ProjectSettings.globalize_path(str(target))', "target",
                'ProjectSettings.globalize_path("user://logs")'):
        assert calls("func f():\n\tOS.shell_open(" + arg + ")\n", RELEASE) == ["line 2: OS.shell_open"], arg


def test_strings_naming_the_methods_are_findings():
    """OS.call("shell_open", ...), Callable(OS, "shell_show_in_file_manager"): a string naming a file-manager or process method."""
    for text in ('func f():\n\tOS.call("shell_open", "https://x")\n', 'func f():\n\tvar c := Callable(OS, "shell_show_in_file_manager")\n'):
        assert any("names an OS process / file-manager method" in f for f in net_guard.gdscript_findings(text, RELEASE)), text


def test_the_profiler_exception_is_by_tokens():
    assert net_guard.gdscript_findings('func f():\n\tOS.create_process("perf", ["stat"])\n') == []
    assert net_guard.gdscript_findings('func f():\n\tOS.execute("kill", ["-INT", "1"])\n') == []
    # a call with a space before its parenthesis and a profiler call in a comment: the real call is what counts
    spaced = 'func f():\n\tOS.execute ("rm", ["-rf", "/"]) # OS.execute("kill", [])\n'
    assert net_guard.gdscript_findings(spaced) == ["line 2: OS.execute"]
    assert net_guard.gdscript_findings('func f():\n\tOS.execute("rm", [])\n') == ["line 2: OS.execute"]


def test_the_game_tree_passes_and_release_gd_keeps_its_guard():
    assert {p: f for p, f in net_guard.scan().items() if p.endswith(".gd")} == {}
    release = (REPO / RELEASE).read_text()
    owner = release[release.index("func _show_in_file_manager(path: String) -> Error:"):]
    owner = owner[:owner.index("\nfunc ") if "\nfunc " in owner else len(owner)]
    assert "OS.get_user_data_dir()" in owner and "simplify_path()" in owner and 'begins_with(root + "/")' in owner and '"://"' in owner
    assert "enum UserFolder { LOGS }" in release and "if not _is_session_log_name(name):" in release


def test_ui3_the_launcher_has_one_file_manager_owner_too():
    """lane UI-3: the launcher's "Show folder" / "Open log folder" go through paths.gd's static show_in_file_manager alone"""
    paths = "launcher/scripts/ui/paths.gd"
    owner = "static func show_in_file_manager(path: String) -> String:\n\tOS.shell_show_in_file_manager(path, true)\n\treturn \"\"\n"
    assert calls(owner, paths) == []
    assert calls(owner.replace("static func", "func"), paths) == []
    assert calls(owner, "launcher/scripts/main.gd") == ["line 2: OS.shell_show_in_file_manager"]
    assert calls(owner.replace("show_in_file_manager(path: String)", "open_logs(path: String)"), paths) == ["line 2: OS.shell_show_in_file_manager"]
    assert calls(owner + "static func other():\n\tOS.shell_open(\"https://example.com\")\n", paths) == ["line 5: OS.shell_open"]
    assert calls("static var x := 1\n" + "func f():\n\tOS.shell_open(\"x\")\n", paths) == ["line 3: OS.shell_open"]
    found = net_guard.scan_launcher()
    assert [f for f in found.get(paths, []) if "shell_" in f] == [] and not [p for p, fs in found.items() for f in fs if "shell_" in f]
