"""Lane RENDERWAIT-1: no per-frame render path may wait for the logic worker.

The render (main) thread reads the live logic only while it is held (LiveGame's client window, or an idle worker); a bound GameWorld / InGameHudNode
method that reaches the live logic waits (LiveGame::holdLogic -> waitIdle) when called outside it. UBER-1's 10k battle spent 110 ms of every 164 ms frame
in such a wait: the spell ring's per-frame get_spellbook_state (InGameHud::enterContext). This test reads the real code:
  1. which bound methods wait: their C++ body reaches the live logic (the waiting accessors, enterContext, waitLogic, waitSpellIdle, createObject);
  2. which scripts run every render frame of a game: game.gd's per-frame functions and the _process of every script game.gd loads;
  3. a per-frame function may call a waiting method only where its control flow has established `is_logic_held()`: after an
     `if not <held>: return / continue` of an enclosing block, inside an `if <held> [and ...]:` body, behind `<held> and` in a condition, or in the
     true arm of `x if <held> else y`. A guard in another branch, or after the call, does not count (review r1 #3). Helpers are followed.
The scripts: game.gd's PLAYING branch and per-frame functions, campaign_flow.gd's tick, and the _process of every script reachable from game.gd by
its "res://scripts/*.gd" names (campaign_cine.gd through campaign_flow.gd).
It fails on the code before RENDERWAIT-1 (spell_target_ring.gd's _process, game_options.gd's clock).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "godot" / "scripts"
DEVICE = ROOT / "engine" / "src" / "GodotDevice"

# game.gd's functions run every rendered frame while a game is played (_process and what it calls each frame)
GAME_PER_FRAME = ["_process", "_perf_frame", "_perf_log_frame", "_update_listener", "_end_game_tick"]
# (script, function, the call in game.gd's PLAYING branch) run every render frame of a game from game.gd
EXTERNAL_PER_FRAME = [("campaign_flow.gd", "tick", "_campaign_flow.tick()")]
# bound methods that end or replace the game: reached from the per-frame functions once (the game's end, a mission's teardown and the next one's load),
# never every frame; they wait for the worker by design (the game goes away under it)
ONE_OFF = {"clear_game_data", "start_campaign_mission"}
# what reaches the live logic in a bound method's body (each waits for the worker outside the client window)
WAITING = re.compile(
    r"(->|\.)(logic|players|ai|shroud|commands|dispatch|aiCommands|buildCommands|spellCommands|launchBones|createObject|waitIdle|holdLogic)\(|"
    r"\benterContext\(|\bwaitLogic\(|\bwaitSpellIdle\(")


def cpp_body(source: str, start: int) -> str:
    body = source.index("{", start)
    depth = 0
    for i in range(body, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[body:i + 1]
    raise AssertionError("unterminated body")


def waiting_methods() -> dict:
    """{bound name: C++ method} of GameWorld / InGameHudNode whose body reaches the live logic."""
    out = {}
    sources = {p: p.read_text(encoding="utf-8") for p in DEVICE.glob("*.cpp")}
    binds = []
    for text in sources.values():
        binds += re.findall(r'bind_method\(D_METHOD\("(\w+)"[^)]*\)\s*,\s*&(GameWorld|InGameHudNode)::(\w+)', text)
    for name, cls, method in binds:
        for text in sources.values():
            m = re.search(r"^[\w:<>,\s\*&]*\b" + cls + r"::" + method + r"\([^;{]*\)\s*(const\s*)?\{", text, re.M)
            if m:
                body = cpp_body(text, m.start())
                # a body that reads the live logic only after asking whether it is held (logicHeld) does not wait
                if WAITING.search(body) and "logicHeld()" not in body:
                    out[name] = f"{cls}::{method}"
                break
    return out


def gd_function(source: str, name: str) -> str:
    m = re.search(r"^func " + re.escape(name) + r"\(.*?$(.*?)(?=^func |\Z)", source, re.M | re.S)
    return m.group(1) if m else ""


HELD_CALL = r"(?:[\w.]+\.)?is_logic_held\(\)"


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip("\t"))


def unguarded_waiting_calls(source: str, body: str, waiting: dict, entry_guarded: bool = False, _depth: int = 0) -> list:
    """the waiting calls of `body` (a function's lines; its helpers in `source` are followed) made where the control flow has not established that the
    logic is held"""
    funcs = set(re.findall(r"^func (\w+)\(", source, re.M))
    lines = [l for l in body.split("\n") if l.strip() and not l.strip().startswith("#")]
    held_names = set(re.findall(r"^\s*var (\w+)(?:: bool)? :?= (?!not )[^#\n]*?" + HELD_CALL, body, re.M))
    held = "(?:" + "|".join([HELD_CALL] + [re.escape(n) + r"\b" for n in sorted(held_names)]) + ")"
    out = []
    after_guard = []   # indents d: an `if not held: return / continue` was passed in the block at indent d
    inside_held = []   # indents d: inside the body of an `if held [and ...]:` at indent d
    for k, line in enumerate(lines):
        d = _indent(line)
        after_guard = [g for g in after_guard if g <= d]
        inside_held = [g for g in inside_held if g < d]
        text = line.strip()
        base = entry_guarded or bool(after_guard) or bool(inside_held)
        # where in this line the held state is established: behind `held and` of an and-only condition, before ` if held else`
        from_col, until_col = None, None
        cond = re.match(r"(?:if|elif|while) (.*):\s*(?:#.*)?$", text)
        positive = False
        if cond and " or " not in cond.group(1):
            parts = cond.group(1).split(" and ")
            col = len(text) - len(text.lstrip()) + text.index(cond.group(1))
            for part in parts:
                if re.fullmatch(held, part.strip()):
                    positive = True
                    from_col = col + len(part)
                    break
                col += len(part) + len(" and ")
        tern = re.search(r" if " + held + r" else ", text)
        if tern:
            until_col = tern.start()
        for m in list(re.finditer(r"\b[\w]+\.(\w+)\(", text)) + list(re.finditer(r"(?<![\w.])(\w+)\(", text)):
            name = m.group(1)
            guarded = base or (from_col is not None and m.start() >= from_col) or (until_col is not None and m.start() < until_col)
            if "." in m.group(0) and name in waiting:
                if not guarded:
                    out.append(name)
            elif "." not in m.group(0) and name in funcs and not text.startswith("func ") and _depth < 4:
                out += unguarded_waiting_calls(source, gd_function(source, name), waiting, guarded, _depth + 1)
        if positive and text.startswith("if "):
            inside_held.append(d)
        if re.fullmatch(r"if not " + held + r":(?:\s*#.*)?", text) and k + 1 < len(lines) and _indent(lines[k + 1]) == d + 1 \
                and lines[k + 1].strip().split("#")[0].strip() in ("return", "continue"):
            after_guard.append(d)
    return out


def reachable_scripts(start: str = "game.gd") -> list:
    seen, todo = [], [start]
    while todo:
        name = todo.pop()
        path = SCRIPTS / name
        if name in seen or not path.exists():
            continue
        seen.append(name)
        todo += re.findall(r'"res://scripts/(\w+\.gd)"', path.read_text(encoding="utf-8"))
    return sorted(seen)


def per_frame_functions() -> list:
    """[(file, function, source, body)] run every render frame of a game"""
    game = (SCRIPTS / "game.gd").read_text(encoding="utf-8")
    # game.gd's _process while a game is played: its PLAYING branch (the menu's and the score screen's run without a game on the worker)
    process = gd_function(game, "_process")
    playing = process[process.index("elif _state == State.PLAYING:"):process.index("elif _state == State.SCORE:")]
    out = [("game.gd", "_process (PLAYING)", game, playing)]
    out += [("game.gd", f, game, gd_function(game, f)) for f in GAME_PER_FRAME if f != "_process"]
    for script, func, call in EXTERNAL_PER_FRAME:
        assert call in playing, f"game.gd's PLAYING branch no longer calls {call}: update EXTERNAL_PER_FRAME"
        text = (SCRIPTS / script).read_text(encoding="utf-8")
        out.append((script, func, text, gd_function(text, func)))
    for script in reachable_scripts():
        if script == "game.gd":
            continue
        text = (SCRIPTS / script).read_text(encoding="utf-8")
        body = gd_function(text, "_process")
        if body:
            out.append((script, "_process", text, body))
    return out


def test_the_waiting_set_is_found():
    waiting = waiting_methods()
    # known members of both classes (their bodies wait for the worker outside the window)
    for name in ("get_frame", "get_object", "get_spellbook_state", "pixel_to_world", "get_ground_height", "create_object", "debug_script_unit"):
        assert name in waiting, f"{name} is not recognised as a waiting bound method: the parser of this test is broken"
    for name in ("get_presented_frame", "get_frame_timings", "get_camera", "get_move_hints", "is_logic_held", "get_logic_wait_sites",
                 "update_script_view", "take_script_requests"):
        assert name not in waiting, f"{name} reaches the live logic ({waiting.get(name)}): it must not wait, scripts call it every frame"


def test_per_frame_scripts_never_call_a_waiting_method_unguarded():
    waiting = waiting_methods()
    found = per_frame_functions()
    for script in ("spell_target_ring.gd", "move_hints.gd", "game_options.gd", "qa_player.gd", "campaign_cine.gd"):
        assert any(f == script for f, _, _, _ in found), f"{script}'s _process is not among the per-frame functions"
    bad = []
    for script, func, source, body in found:
        for name in unguarded_waiting_calls(source, body, waiting):
            if name in ONE_OFF:
                continue
            bad.append(f"{script} {func} calls {name} ({waiting[name]}) where the logic is not known to be held")
    assert not bad, "per-frame render paths that wait for the logic worker:\n" + "\n".join(bad)


def test_the_script_audio_of_a_request_waits_for_a_held_logic():
    """campaign_cine.gd's take_script_requests / update_script_view (every frame): the requests' audio reads the live game (review r1 #2)"""
    text = (DEVICE / "GodotGameWorldScripts.cpp").read_text(encoding="utf-8")
    take = cpp_body(text, text.index("Array GameWorld::take_script_requests()"))
    assert "applyScriptRequest" not in take, "take_script_requests applies the audio itself: it reads the live game outside the client window"
    apply = cpp_body(text, text.index("void GameWorld::applyPendingScriptAudio()"))
    assert apply.index("logicHeld()") < apply.index("applyScriptRequest")
    view = cpp_body(text, text.index("Dictionary GameWorld::update_script_view("))
    assert "presentedObjectPosition" in view and not WAITING.search(view)


def test_the_guard_itself_does_not_wait():
    text = (DEVICE / "GodotGameWorld.cpp").read_text(encoding="utf-8")
    body = cpp_body(text, text.index("bool GameWorld::is_logic_held() const"))
    assert "logicHeld()" in body and not WAITING.search(body)


def test_the_detector_reads_the_control_flow():
    waiting = {"get_x": "GameWorld::get_x"}

    def bad(body, source=""):
        return unguarded_waiting_calls(source or body, body, waiting)
    assert bad("\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tif not world.is_logic_held():\n\t\treturn\n\tvar v = world.get_x()\n") == []
    # a guard after the call, in another branch, or one that does not leave
    assert bad("\tvar v = world.get_x()\n\tif not world.is_logic_held():\n\t\treturn\n") == ["get_x"]
    assert bad("\tif a:\n\t\tif not world.is_logic_held():\n\t\t\treturn\n\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tif not world.is_logic_held():\n\t\tprint(1)\n\tvar v = world.get_x()\n") == ["get_x"]
    assert bad("\tvar n = world.is_logic_held()\n\tvar v = world.get_x()\n") == ["get_x"]
    # a loop's continue covers the rest of the loop body only
    loop = "\tvar held: bool = world.is_logic_held()\n\tfor h in hs:\n\t\tif not held:\n\t\t\tcontinue\n\t\tworld.get_x()\n"
    assert bad(loop) == []
    assert bad(loop + "\tworld.get_x()\n") == ["get_x"]
    # the positive forms: an if body (not its else), a condition's later conjunct, the true arm of a conditional expression
    assert bad("\tif world.is_logic_held():\n\t\tworld.get_x()\n") == []
    assert bad("\tif world.is_logic_held():\n\t\tpass\n\telse:\n\t\tworld.get_x()\n") == ["get_x"]
    assert bad("\tif a and world.is_logic_held() and world.get_x() > 0:\n\t\tpass\n") == []
    assert bad("\tif world.get_x() > 0 and world.is_logic_held():\n\t\tpass\n") == ["get_x"]
    assert bad("\tif a or world.is_logic_held():\n\t\tworld.get_x()\n") == ["get_x"]
    assert bad("\tvar held = world.is_logic_held()\n\tz = world.get_x() if held else z\n") == []
    assert bad("\tvar held = world.is_logic_held()\n\tz = z if held else world.get_x()\n") == ["get_x"]
    # a helper is read at its call: guarded there or not
    source = "func _process(d):\n\t_h()\n\nfunc _h():\n\treturn world.get_x()\n"
    assert unguarded_waiting_calls(source, gd_function(source, "_process"), waiting) == ["get_x"]
    source = "func _process(d):\n\tif world.is_logic_held():\n\t\t_h()\n\nfunc _h():\n\treturn world.get_x()\n"
    assert unguarded_waiting_calls(source, gd_function(source, "_process"), waiting) == []


def test_every_deferred_list_of_renderwait1_is_cleared_when_the_scene_goes():
    """review r2: what the render side defers for a game must not reach the next one (godot/tests/renderwait1_teardown_test.gd runs the real teardown)"""
    text = (DEVICE / "GodotGameWorld.cpp").read_text(encoding="utf-8")
    body = cpp_body(text, text.index("void GameWorld::clear_scene()"))
    for member in ("m_pendingScriptAudio.clear()", "m_pendingScriptRequests.clear()", "m_fxPendingDt = 0.0", "m_netHash = 0"):
        assert member in body, f"clear_scene() leaves {member.split('.')[0].split(' ')[0]} of the old game for the next one"
