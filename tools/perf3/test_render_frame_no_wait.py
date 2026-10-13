"""Lane PATHPERF-1: what the game's render loop calls every frame must never wait for the logic worker.

GameWorld's live queries (get_frame, get_state_hash, get_object_count, get_object_ids, get_object) go through LiveGame::logic(), which waits until the
worker has finished its frame (LiveGame::waitIdle): a 400 ms logic frame then freezes the render for 400 ms. game.gd's --perf-log line used get_frame and so
froze the render on every long logic frame. This test reads the real callers: the per-frame functions of godot/scripts/game.gd may only use the
non-blocking accessors (get_presented_frame, get_frame_timings), and those accessors' C++ bodies must not reach the live logic.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME_GD = ROOT / "godot" / "scripts" / "game.gd"
GAME_WORLD_CPP = ROOT / "engine" / "src" / "GodotDevice" / "GodotGameWorld.cpp"

# the functions game.gd runs on every rendered frame while a game is played (_process and what it calls each frame)
PER_FRAME = ["_process", "_perf_frame", "_perf_log_frame", "_update_listener", "_end_game_tick"]
BLOCKING = re.compile(r"_world\.(get_frame|get_state_hash|get_object_count|get_object_ids|get_object)\(")


def gd_function(source: str, name: str) -> str:
    m = re.search(r"^func " + re.escape(name) + r"\(.*?$(.*?)(?=^func |\Z)", source, re.M | re.S)
    assert m, f"game.gd has no func {name}"
    return m.group(1)


def cpp_function(source: str, signature: str) -> str:
    start = source.index(signature)
    body = source.index("{", start)
    depth = 0
    for i in range(body, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[body:i + 1]
    raise AssertionError(f"unterminated body of {signature}")


def test_game_gd_per_frame_functions_use_no_blocking_world_query():
    source = GAME_GD.read_text(encoding="utf-8")
    for name in PER_FRAME:
        body = gd_function(source, name)
        hits = [m.group(0) for m in BLOCKING.finditer(body)]
        assert not hits, f"game.gd {name} (run every render frame) calls {hits}: these wait for the logic worker; use get_presented_frame / get_frame_timings"


def test_perf_log_line_reads_the_presented_frame():
    body = gd_function(GAME_GD.read_text(encoding="utf-8"), "_perf_log_frame")
    assert "_world.get_presented_frame()" in body


def test_the_non_blocking_accessors_do_not_reach_the_live_logic():
    source = GAME_WORLD_CPP.read_text(encoding="utf-8")
    for signature in ("int64_t GameWorld::get_presented_frame() const", "Dictionary GameWorld::get_frame_timings() const"):
        body = cpp_function(source, signature)
        for forbidden in ("->logic()", ".logic()", "waitIdle", "->players()", "->ai()", "->commands()"):
            assert forbidden not in body, f"{signature} uses {forbidden}, which waits for the logic worker"
