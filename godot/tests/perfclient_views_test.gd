## Lane PERF-CLIENT: refresh_views' job pool pass leaves the live objects' instancer exactly as the serial pass does.
##
##   godot --headless --path godot --script res://tests/perfclient_views_test.gd
##
## Two GameWorlds load the same map with the same seed, build the same battle (opposing hordes ordered onto each other: walking, fighting and dying
## members; a barracks placed at STRUCTURE_FRAME, a new model and an occluder (a structure) among occludees; DESTROY_COUNT hordes destroyed at
## DESTROY_FRAME, so views are removed) and advance with the same deltas, the logic on the calling thread. One refreshes its views on the client job
## pool (set_parallel_views(true): the drawables view_is_local accepts on the pool, the rest serially in slot order, the occlusion lists after both), the
## other serially from the map load on. Pose culling and horde pose sharing are off (set_perf3_client(false)), so every instance's palette row is its
## own pose, written by each update_now. After every frame both instancers hold the same instances, bit for bit: the instance count, every MultiMesh's
## visible count and buffer (transforms and the custom data with the opacity, the fogged light, the occlusion bits and the palette offset) and every
## instance's palette (its pose); both worlds count the same animated drawables and have the same state hash. The parallel world must have run mixed
## frames (drawables refreshed both on the pool and serially), the serial world none on the pool; both must have built a model after the first frame,
## made occlusion pass materials and lost the destroyed hordes' views. One run samples one thread schedule: a race may need repeated runs.
## Needs ROTWK_INSTALL and BFME2_INSTALL, and a client job pool of more than one thread (get_frame_timings' client_threads; else SKIP, exit 77).
## Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
const MAP := "map mp fall back 4p"
const SEED := 4711
const HORDES_PER_SIDE := 24
const FRAMES := 450
const DELTA := 1.0 / 30.0
const STRUCTURE := "GondorBarracks"
const STRUCTURE_FRAME := 60
const DESTROY_FRAME := 300
const DESTROY_COUNT := 4
const DESTROY_WINDOW := 8 # frames after DESTROY_FRAME in which the destroyed hordes' views must be gone (they went 5 frames after the order, 53 at once)

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("PERFCLIENT VIEWS %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _world(fs: RefCounted, parallel: bool) -> Node:
	var w: Node = ClassDB.instantiate("GameWorld")
	root.add_child(w)
	var setup: Dictionary = w.setup(fs)
	if not setup.ok:
		print("  setup failed ", setup.errors)
		return null
	w.set_parallel_views(parallel) # before the map: load_map refreshes the views once
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = w.load_map(MAP, {"slots": slots, "seed": SEED})
	if not rep.ok:
		print("  load_map failed ", rep.errors.slice(0, 5))
		return null
	w.set_logic_thread(false)
	w.set_auto_advance(false)
	w.set_perf3_client(false) # pose culling and horde pose sharing off: every instance's palette row is its own pose, written by each update_now
	return w


func _player(w: Node, name: String) -> int:
	for p in w.get_economy().get("players", []):
		if p.name == name:
			return int(p.index)
	return -1


# the same battle in both worlds: two lines of hordes facing each other in the middle of the map, each ordered onto the one across. Returns the
# pairs of horde ids, empty when the battle could not be built
func _battle(w: Node) -> Array:
	var me := _player(w, "Player_1")
	var foe := _player(w, "Player_2")
	var census: Dictionary = w.get_census()
	if me < 0 or foe < 0 or not census.has("extent_x"):
		return []
	var cx: float = float(census.extent_x) * 0.5
	var cy: float = float(census.extent_y) * 0.5
	var pairs: Array = []
	for i in HORDES_PER_SIDE:
		var y := cy + 90.0 * float(i - HORDES_PER_SIDE / 2)
		var a: int = w.create_object("GondorFighterHorde", me, cx - 160.0, y, 0.0)
		var b: int = w.create_object("MordorFighterHorde", foe, cx + 160.0, y, PI)
		if a <= 0 or b <= 0:
			return []
		pairs.append([a, b])
	for p in pairs:
		w.order_attack([p[0]], p[1])
		w.order_attack([p[1]], p[0])
	return pairs


# the barracks behind the Gondor line: an occluder (STRUCTURE) among the battle's occludees
func _structure(w: Node) -> bool:
	var census: Dictionary = w.get_census()
	return w.create_object(STRUCTURE, _player(w, "Player_1"), float(census.extent_x) * 0.5 - 420.0, float(census.extent_y) * 0.5, 0.0) > 0


func _instances(w: Node) -> Node:
	return w.get_node_or_null("LiveObjects")


# the instancer's per-instance state as bytes: the count, then each MultiMesh's name, visible count and buffer
func _snapshot(inst: Node) -> Array:
	var out: Array = [inst.get_instance_count()]
	for child in inst.get_children():
		if child is MultiMeshInstance3D and child.multimesh != null:
			out.append(String(child.name))
			out.append(child.multimesh.visible_instance_count)
			out.append(child.multimesh.buffer.to_byte_array())
	return out


func _palettes_equal(a: Node, b: Node) -> bool:
	var n: int = a.get_instance_count()
	if n != b.get_instance_count():
		return false
	for i in n:
		if a.get_instance_palette(i).to_byte_array() != b.get_instance_palette(i).to_byte_array():
			return false
	return true


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return EXIT_SKIP
	if not ClassDB.class_exists("RetailFileSystem"):
		print("FAIL: the openbfme extension is not loaded")
		return EXIT_FAIL
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", mount.errors)
		return EXIT_FAIL
	var par: Node = _world(fs, true)
	var ser: Node = _world(fs, false)
	if par == null or ser == null:
		return EXIT_FAIL
	var client_threads: int = int(par.get_frame_timings().get("client_threads", 0))
	if client_threads < 2:
		print("SKIP: the client job pool has %d thread(s): the pool pass would run inline" % client_threads)
		return EXIT_SKIP
	var par_pairs: Array = _battle(par)
	var ser_pairs: Array = _battle(ser)
	_check(not par_pairs.is_empty() and par_pairs == ser_pairs, "the same battle in both worlds")
	var pi: Node = _instances(par)
	var si: Node = _instances(ser)
	_check(pi != null and si != null, "both worlds have their live objects' instancer")
	if _failures != 0:
		return EXIT_FAIL
	var troops0: int = int(par.get_census().troops)
	var mixed := 0
	var most_views := 0
	var serial_on_pool := 0
	var pose_shared := 0
	var first_bad := -1
	var new_models := [0, 0] # built after the first frame: parallel, serial
	var views_before_destroy := 0
	var views_after_destroy := 0
	for f in FRAMES:
		if f == STRUCTURE_FRAME:
			_check(_structure(par) and _structure(ser), "the %s placed in both worlds at frame %d" % [STRUCTURE, f])
		if f == DESTROY_FRAME:
			for i in DESTROY_COUNT:
				_check(par.destroy_object(par_pairs[i][1]) and ser.destroy_object(ser_pairs[i][1]), "horde %d destroyed in both worlds" % i)
		par.advance(DELTA)
		ser.advance(DELTA)
		await process_frame
		pi.update_now()
		si.update_now()
		var t: Dictionary = par.get_frame_timings()
		var local: int = int(t.get("views_local", 0))
		var serial: int = int(t.get("views_serial", 0))
		most_views = maxi(most_views, local + serial)
		if local > 0 and serial > 0:
			mixed += 1
		var st: Dictionary = ser.get_frame_timings()
		serial_on_pool += int(st.get("views_local", 0))
		pose_shared += int(pi.get_stats().pose_shared) + int(si.get_stats().pose_shared)
		if f > 0:
			new_models[0] += int(t.get("new_models", 0))
			new_models[1] += int(st.get("new_models", 0))
		if f == DESTROY_FRAME - 1:
			views_before_destroy = local + serial
		elif f == DESTROY_FRAME + DESTROY_WINDOW:
			views_after_destroy = local + serial
		var ps: Dictionary = par.get_stats()
		var ss: Dictionary = ser.get_stats()
		var same: bool = ps.state_hash == ss.state_hash and ps.animated_drawables == ss.animated_drawables
		if first_bad < 0 and not (same and _snapshot(pi) == _snapshot(si) and _palettes_equal(pi, si)):
			first_bad = f
	var troops1: int = int(par.get_census().troops)
	var po: Dictionary = pi.get_occlusion_pass_counts()
	var so: Dictionary = si.get_occlusion_pass_counts()
	print("  info: %d frames, %d views at most, %d mixed frames, %d instances, troops %d -> %d, views %d -> %d across the destruction, " % [
		FRAMES, most_views, mixed, pi.get_instance_count(), troops0, troops1, views_before_destroy, views_after_destroy]
		+ "models built after frame 0 %s, occlusion pass materials %s / %s, %d client job pool threads" % [new_models, po, so, client_threads])
	_check(first_bad == -1, "the parallel and the serial world's instancers, animated drawables and state hashes are identical after every frame (first difference: %d)" % first_bad)
	_check(mixed > 0, "frames refreshed drawables both on the job pool and serially (%d)" % mixed) # tolerance: any mixed frame exercises both passes
	_check(serial_on_pool == 0, "the serial world refreshed nothing on the job pool")
	_check(pose_shared == 0, "no instance drew another's pose (%d)" % pose_shared)
	_check(troops1 < troops0, "members died in the battle (%d -> %d)" % [troops0, troops1]) # tolerance: the battle's deaths make and remove instances
	_check(new_models[0] > 0 and new_models[0] == new_models[1], "both worlds built the same models after the first frame (%s)" % [new_models]) # tolerance: the barracks' model is new
	_check(po == so and int(po.get("marks", 0)) > 0 and int(po.get("colors", 0)) > 0, "both worlds made occluder and occludee pass materials (%s / %s)" % [po, so]) # tolerance: any material shows the roles were set
	_check(views_before_destroy - views_after_destroy >= DESTROY_COUNT, "the destroyed hordes' views were removed (%d -> %d)" % [views_before_destroy, views_after_destroy]) # tolerance: each destroyed horde removes at least its own view
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
