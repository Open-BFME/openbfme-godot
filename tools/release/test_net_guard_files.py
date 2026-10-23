"""Lane WINCRASH-1: the only file-manager call the game may make (tools/release/net_guard.py): release.gd's _show_in_file_manager, by tokens."""
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import net_guard  # noqa: E402


def test_scanners_use_portable_owner_paths_without_widening_permissions(tmp_path, monkeypatch):
    from pathlib import PureWindowsPath

    owner = 'func _show_in_file_manager(path):\n\tOS.shell_show_in_file_manager(path)\n'
    launcher_owner = 'static func show_in_file_manager(path):\n\tOS.shell_show_in_file_manager(path)\n'
    sources = {
        "engine/src/GameNetwork/Transport.cpp": "void f() { socket(); }",
        "engine/src/Other.cpp": "void f() { socket(); }",
        "godot/scripts/release/release.gd": owner,
        "godot/scripts/other.gd": owner,
        "godot/scripts/online_lobby.gd": "var request = HTTPRequest.new()",
        "godot/tests/ignored.gd": owner,
        "launcher/scripts/ui/paths.gd": launcher_owner,
        "launcher/scripts/other.gd": launcher_owner,
        "launcher/scripts/core/instance_lock.gd": "var server = TCPServer.new()\nvar request = HTTPRequest.new()",
        "launcher/tests/ignored.gd": launcher_owner,
    }
    for relative, source in sources.items():
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
    path_type = type(tmp_path)
    relative_to = path_type.relative_to
    # Exercise Windows separators even on POSIX, without changing real file access.
    with monkeypatch.context() as patch:
        patch.setattr(net_guard, "REPO", tmp_path)
        patch.setattr(path_type, "relative_to", lambda self, *args, **kwargs:
                      PureWindowsPath(*relative_to(self, *args, **kwargs).parts))
        game = net_guard.scan()
        launcher = net_guard.scan_launcher()
    assert set(game) == {
        "engine/src/GameNetwork/Transport.cpp", "engine/src/Other.cpp",
        "godot/scripts/other.gd", "godot/scripts/online_lobby.gd",
    }
    assert set(launcher) == {"launcher/scripts/other.gd", "launcher/scripts/core/instance_lock.gd"}
    allowed = lambda path, finding: path in net_guard.ALLOWED_NATIVE or net_guard.game_finding_allowed(path, finding)
    assert all(allowed("engine/src/GameNetwork/Transport.cpp", f) for f in game["engine/src/GameNetwork/Transport.cpp"])
    assert not any(allowed("engine/src/Other.cpp", f) for f in game["engine/src/Other.cpp"])
    assert not any(allowed("godot/scripts/other.gd", f) for f in game["godot/scripts/other.gd"])
    assert all(allowed("godot/scripts/online_lobby.gd", f) for f in game["godot/scripts/online_lobby.gd"])
    lock = "launcher/scripts/core/instance_lock.gd"
    assert {f.split(": ", 1)[1] for f in launcher[lock]} == {"TCPServer", "HTTPRequest"}
    assert net_guard.launcher_finding_allowed(lock, "line 1: TCPServer")
    assert not net_guard.launcher_finding_allowed(lock, "line 2: HTTPRequest")
    assert not any(net_guard.launcher_finding_allowed("launcher/scripts/other.gd", f)
                   for f in launcher["launcher/scripts/other.gd"])


def test_launcher_excluded_ports_query_is_exact_and_read_only():
    path = "launcher/scripts/core/instance_lock.gd"
    query = ('OS.execute(OS.get_environment("SystemRoot").path_join("System32/netsh.exe"), '
             '["int", "ipv4", "show", "excludedportrange", "protocol=tcp"], output, true)')
    source = "func _windows_exclusions():\n\tvar code := " + query + "\n"
    assert net_guard.gdscript_findings(source, path) == []
    assert net_guard.gdscript_findings(source.replace("OS.execute(", "OS . execute ("), path) == []
    assert net_guard.gdscript_findings(source, "launcher/scripts/main.gd")
    assert net_guard.gdscript_findings(source.replace("func _windows_exclusions", "func other"), path)
    for old, new in [('"show"', '"add"'), ('"protocol=tcp"', '"protocol=udp"'),
                     ('"System32/netsh.exe"', '"netsh.exe"'), ('"SystemRoot"', '"CUSTOM_ROOT"'),
                     ('"excludedportrange"', 'command'), ('output, true)', 'output, true, true)'),
                     ('OS.execute(', 'OS.create_process(')]:
        changed = source.replace(old, new)
        assert changed != source and net_guard.gdscript_findings(changed, path)
    assert net_guard.gdscript_findings(source + 'func other():\n\tOS.execute("netsh", args)\n', path)
    assert net_guard.gdscript_findings('func other():\n\tOS.execute("netsh", args) # ' + query, path)
    assert net_guard.gdscript_findings('var fake = """' + source + '"""\nfunc f():\n\tOS.execute("netsh", args)', path)
    findings = net_guard.gdscript_findings((REPO / path).read_text(encoding="utf-8"), path)
    assert not [f for f in findings if "OS.execute" in f]

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
    # lane ONLINE-2: the online lobby client's HTTPRequest / HTTPClient are allowed finding by finding (net_guard.ALLOWED_GAME)
    found = {p: [x for x in f if not net_guard.game_finding_allowed(p, x)] for p, f in net_guard.scan().items() if p.endswith(".gd")}
    assert {p: f for p, f in found.items() if f} == {}
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
    # lane ONLINEMENU-1: the account page opens the system browser on a checked sign-in URL (test_launcher_tools pins both calls): no file manager
    oauth = "launcher/scripts/core/oauth_account.gd"
    assert [f.split(": ", 1)[-1] for f in found.get(oauth, []) if "shell_" in f] == ["OS.shell_open", "OS.shell_open"]
    assert [f for f in found.get(paths, []) if "shell_" in f] == [] and not [p for p, fs in found.items() if p != oauth for f in fs if "shell_" in f]


def test_the_online_lobby_client_requests_only_the_named_lobby():
    """lane ONLINE-2: godot/scripts/online_lobby.gd is the one game file allowed HTTP, and its one request goes to lobby_url"""
    text = (REPO / net_guard.ONLINE_LOBBY).read_text()
    assert net_guard.online_lobby_problems(text) == []
    assert net_guard.online_lobby_problems(text + '\nfunc leak():\n\t_http.request("https://example.com/collect")\n') != []
    swapped = text.replace("_http.request(lobby_url", "_http.request(other_url")
    assert swapped != text and net_guard.online_lobby_problems(swapped) != []
    # ONLINE-2 r2 (Sol r1): redirects must stay off (Godot would take the token to the redirect's origin)
    following = text.replace("_http.max_redirects = 0", "_http.max_redirects = 8")
    assert following != text and any("max_redirects" in p for p in net_guard.online_lobby_problems(following))
    # a networking class elsewhere in the game is still a finding
    assert not net_guard.game_finding_allowed("godot/scripts/game.gd", "line 1: HTTPRequest")
    assert not net_guard.game_finding_allowed(net_guard.ONLINE_LOBBY, "line 1: TCPServer")


def test_online4_the_online_screens_transport_requests_only_the_named_lobby():
    """lane ONLINE-4: godot/scripts/online_http.gd (the online screens' HTTP) is allowed HTTP finding by finding; its one request goes to lobby_url
    (the lobby client hands it a path), with no redirects"""
    path = "godot/scripts/online_http.gd"
    assert path in net_guard.ONLINE_LOBBY_FILES
    text = (REPO / path).read_text()
    assert net_guard.online_lobby_problems(text) == []
    # lane ONLINE-5: the ordinary requests and the held poll share the one request call (`node` is one of the file's two HTTPRequest nodes)
    swapped = text.replace("node.request(lobby_url", "node.request(q.url")
    assert swapped != text and net_guard.online_lobby_problems(swapped) != []
    assert net_guard.online_lobby_problems(text + '\nfunc leak():\n\t_held.request("https://example.com/collect")\n') != []
    assert net_guard.online_lobby_problems(text.replace("max_redirects = 0", "max_redirects = 8")) != []
    assert net_guard.game_finding_allowed(path, "line 3: HTTPRequest")
    assert not net_guard.game_finding_allowed(path, "line 3: TCPServer")


def test_onlinecount1_the_player_count_requests_only_the_named_lobby():
    """lane ONLINECOUNT-1: godot/scripts/online_stats.gd (the menus' "N players online") may use HTTPRequest alone, its one request goes to lobby_url,
    and a second request, another URL or a followed redirect is a finding"""
    path = "godot/scripts/online_stats.gd"
    assert path in net_guard.ONLINE_LOBBY_FILES
    assert net_guard.ALLOWED_GAME[path] == {"HTTPRequest"}
    text = (REPO / path).read_text()
    assert net_guard.online_lobby_problems(text) == []
    assert net_guard.online_lobby_problems(text.replace("_http.request(lobby_url", '_http.request("https://example.com" + lobby_url')) != []
    assert net_guard.online_lobby_problems(text + '\nfunc leak():\n\t_http.request("https://example.com/collect")\n') != []
    assert net_guard.online_lobby_problems(text.replace("max_redirects = 0", "max_redirects = 8")) != []
    assert not net_guard.game_finding_allowed(path, "line 1: HTTPClient")
    assert not net_guard.game_finding_allowed(path, "line 1: TCPServer")


def test_netguard1_player_reports_make_no_network_call_of_their_own():
    """lane NETGUARD-1: godot/scripts/online_reports.gd (DEVINPR-1's player reports) opens no connection: its POST goes through the lobby client's
    post_oneshot, so the one request path of the game stays online_lobby.gd's `_http.request(lobby_url...)`, and the file is not allow-listed"""
    path = "godot/scripts/online_reports.gd"
    assert path not in net_guard.ALLOWED_GAME and path not in net_guard.ONLINE_LOBBY_FILES
    text = (REPO / path).read_text()
    assert net_guard.gdscript_findings(text, path) == []
    assert "post_oneshot(" in text and ".request(" not in text
    # a request of its own is a finding again, even with an allowed-looking class
    assert net_guard.gdscript_findings(text + "\nfunc leak():\n\tvar h := HTTPRequest.new()\n", path) != []
    assert not net_guard.game_finding_allowed(path, "line 1: HTTPRequest")
    lobby = (REPO / net_guard.ONLINE_LOBBY).read_text()
    assert "func post_oneshot(" in lobby and net_guard.online_lobby_problems(lobby) == []


def test_onlinemenu1_the_machine_guid_query_is_exact_and_read_only():
    """lane ONLINEMENU-1 (owner 2026-10-20: a hardware id for bans only): the two identity files read Windows' MachineGuid with the system's
    reg.exe and one literal read-only argument list, in machine_id(); the files are on no allow list, and any other process call is a finding"""
    query = ('OS.execute(OS.get_environment("SystemRoot").path_join("System32/reg.exe"), '
             '["query", "HKLM\\\\SOFTWARE\\\\Microsoft\\\\Cryptography", "/v", "MachineGuid"], out, true)')
    for path in ("godot/scripts/online_identity.gd", "launcher/scripts/core/social_identity.gd"):
        assert path not in net_guard.ALLOWED_GAME and path not in net_guard.ALLOWED_LAUNCHER
        text = (REPO / path).read_text(encoding="utf-8")
        assert text.count("OS.execute(") == 1 and query in text
        assert net_guard.gdscript_findings(text, path) == []
        source = "static func machine_id() -> String:\n\tvar out: Array = []\n\tif " + query + " != 0:\n\t\treturn \"\"\n"
        assert net_guard.gdscript_findings(source, path) == []
        assert net_guard.gdscript_findings(source, "godot/scripts/game.gd")
        assert net_guard.gdscript_findings(source.replace("func machine_id", "func other"), path)
        for old, new in [('"query"', '"add"'), ('"MachineGuid"', '"Other"'), ('"System32/reg.exe"', '"reg.exe"'), ('"SystemRoot"', '"CUSTOM_ROOT"'),
                         ('Cryptography', 'Run'), ('out, true)', 'out, true, true)'), ('OS.execute(', 'OS.create_process(')]:
            changed = source.replace(old, new)
            assert changed != source and net_guard.gdscript_findings(changed, path), (old, new)
        assert net_guard.gdscript_findings(source + 'func other():\n\tOS.execute("reg", args)\n', path)
