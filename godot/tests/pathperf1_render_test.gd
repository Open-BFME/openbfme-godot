## Lane PATHPERF-1: the render side never waits for a long logic frame. 150 Gondor hordes on a large map, the logic on its worker thread (as the game runs
## it); one move order sends them across the map, then the render loop runs as the game's does (GameWorld.advance at 60 Hz, the frame timings --perf reads,
## the non-blocking frame query --perf-log reads) while the worker computes the order's frames. Measured: the wall time of every render-side call, the worker's
## logic frame times and the render's blocking waits on the worker (LiveGame::waitIdle).
##
##   godot --headless --path godot --script res://tests/pathperf1_render_test.gd
##
## PASS: no render-side call blocked on the worker (logic_waits unchanged), and the render calls after the order are as fast as with the same army
## standing before it (p99 within x1.5 + 5 ms, the slowest within x1.5 + 10 ms; the render side's own cost depends on the machine, the order's long
## logic frames must not show in it). Calls that built new models or materials for a first appearance are client work and reported apart.
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const MAP := "map mp brown lands"
const HORDES := 150
const RENDER_FRAMES := 600 # 10 s of 60 Hz render frames: 50 logic frames
const BASE_FRAMES := 300   # 5 s of the army standing, before the order

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("PATHPERF1 RENDER %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("  mount failed ", mount.errors)
		return 1
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not setup.ok:
		print("  setup failed ", setup.errors)
		return 1
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = world.load_map(MAP, {"slots": slots, "seed": 4711, "logic_thread": false})
	if not rep.ok:
		print("  load_map failed ", rep.errors.slice(0, 5))
		return 1
	var men := -1
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and men < 0:
			men = idx
		idx += 1
	world.set_auto_advance(false)
	world.set_local_player(men)
	# the block west of the middle of the map's objects, the order to the east
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in world.get_object_ids():
		var o: Dictionary = world.get_object(id)
		lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
		hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	var c := (lo + hi) * 0.5
	var ids: Array = []
	var cols := 13
	for i in HORDES:
		var x: float = c.x - 900.0 - 70.0 * float(i / cols)
		var y: float = c.y + 70.0 * (float(i % cols) - 6.0)
		var h: int = world.create_object("GondorFighterHorde", men, x, y, 0.0)
		if h > 0:
			ids.append(h)
	_check(ids.size() == HORDES, "%d of %d hordes created" % [ids.size(), HORDES])
	# settle past logic frame 25 (processPathfindQueue's start-up budget) on the main thread, then the worker
	while world.get_frame() < 30:
		world.advance(0.2)
	world.set_logic_thread(true)
	# the baseline: the same army standing, the logic on the worker, no order
	var base: Dictionary = _render_window(world, BASE_FRAMES)
	var ordered: Dictionary = world.order_move(ids, c.x + 900.0, c.y, {})
	_check(ordered.get("ok", false), "the move order is accepted: %s" % [ordered])
	var frame0: int = world.get_presented_frame()
	var after: Dictionary = _render_window(world, RENDER_FRAMES)
	for w in [["standing", base], ["after the order", after]]:
		var r: Dictionary = w[1]
		print("  %s: render calls p50 %.2f ms, p99 %.2f ms, max %.2f ms (%d calls building new models / materials apart, worst %.1f ms); worst worker logic frame %.1f ms; blocking waits %d (%.1f ms)" % [
			w[0], r.p50, r.p99, r.max, r.first_use, r.worst_first_use, r.worst_worker, r.waits, r.wait_ms])
	_check(world.get_presented_frame() > frame0 + 20, "the logic ran on the worker (frames presented %d -> %d)" % [frame0, world.get_presented_frame()])
	_check(base.waits == 0 and after.waits == 0, "no render-side call blocked on the logic worker (%d did)" % [base.waits + after.waits])
	# the render side's own cost is the machine's: the order's long logic frames must not show in it
	_check(after.p99 <= base.p99 * 1.5 + 5.0, "the render calls' p99 after the order stays at the standing army's (%.1f against %.1f ms)" % [after.p99, base.p99])
	_check(after.max <= base.max * 1.5 + 10.0, "the slowest render call after the order stays near the standing army's (%.1f against %.1f ms)" % [after.max, base.max])
	world.set_logic_thread(false)
	return 1 if _failures > 0 else 0


# `frames` render frames at 60 Hz as the game's loop runs them (GameWorld.advance, the --perf timings, the --perf-log frame query): the wall time of each call,
# the worker's slowest logic frame and the render's blocking waits on the worker. Calls that built new models or materials (a first appearance) are counted apart
func _render_window(world: Node3D, frames: int) -> Dictionary:
	var t0: Dictionary = world.get_frame_timings()
	var times: Array = []
	var r := {"first_use": 0, "worst_first_use": 0.0, "worst_worker": 0.0, "max": 0.0}
	for k in frames:
		var a := Time.get_ticks_usec()
		world.advance(1.0 / 60.0)
		var t: Dictionary = world.get_frame_timings()
		var _shown: int = world.get_presented_frame()
		var ms := (Time.get_ticks_usec() - a) / 1000.0
		if t.get("new_models", 0) > 0 or t.get("new_streak_materials", 0) > 0:
			r.first_use += 1
			r.worst_first_use = maxf(r.worst_first_use, ms)
		else:
			times.append(ms)
			r.max = maxf(r.max, ms)
		r.worst_worker = maxf(r.worst_worker, t.get("worker_frame_ms", 0.0))
		OS.delay_usec(maxi(0, 16667 - (Time.get_ticks_usec() - a)))
	var t1: Dictionary = world.get_frame_timings()
	times.sort()
	r["p50"] = times[times.size() / 2] if times.size() > 0 else 0.0
	r["p99"] = times[int(0.99 * (times.size() - 1))] if times.size() > 0 else 0.0
	r["waits"] = t1.get("logic_waits", 0) - t0.get("logic_waits", 0)
	r["wait_ms"] = t1.get("logic_wait_ms", 0.0) - t0.get("logic_wait_ms", 0.0)
	return r
