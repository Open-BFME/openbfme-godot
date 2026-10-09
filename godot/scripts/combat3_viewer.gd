## COMBAT-3 viewer (the owner's feedback G4 / G7): RotWK 2.01's cavalry charge running from the binary's logic. Rohirrim charge Gondor soldiers: the trampled
## men are knocked back flailing (Object::doKnockback through PhysicsBehavior's fling), land, lie stunned and stand up, and the riders lose speed at every crush; the
## same charge into tower guards in their porcupine formation stops at the pikes (CRUSHED_DECELERATE 1000%); Grond rolls with its six trolls on their crew bones.
## For the lane's screenshots and video.
##
##   godot --path godot res://scenes/combat3_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-charge.png, -thrown.png, -stunned.png, -porcupine.png, -grond.png, -grond-moving.png there
##   --prefix=<name>   the screenshot prefix (default combat3)
##   --speed=<f>       the logic speed (default 1.0)
##   --only=<part>     charge, pikes or grond (default: all three)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "combat3"
var _speed := 1.0
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--only="):
			_only = arg.substr(7)
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(fs, _map_name, {"markers": false, "fog": false})
	if root == null:
		_fail("build_map failed: %s" % [builder.get_report().errors])
		return
	add_child(root)
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(_map_name, {"slots": slots, "seed": 4711})
	if not rep.ok:
		_fail("load_map failed: %s" % [rep.errors.slice(0, 5)])
		return
	root.add_child(_world)
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and _local < 0:
			_local = idx
		if "Player_2" in str(pl) and _enemy < 0:
			_enemy = idx
		idx += 1
	_world.set_logic_thread(false)
	_world.set_shroud_drawn(false)
	_world.set_auto_advance(true)
	_world.set_time_scale(_speed)
	_world.set_local_player(_local)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_font_size_override("font_size", 22)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 5)
	layer.add_child(_label)
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("COMBAT3VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _aim(focus: Vector3, pos: Vector2, height: float) -> void:
	var g: float = _world.get_ground_height(pos.x, pos.y)
	_camera.position = _sage_to_godot(Vector3(pos.x, pos.y, g + height))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


## an open spot near the middle of the map's trees: no tree within 260 of it
func _pick_spot() -> Vector2:
	var trees: PackedVector3Array = _world.get_terrain_trees(-1)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for t in trees:
		lo = Vector2(minf(lo.x, t.x), minf(lo.y, t.y))
		hi = Vector2(maxf(hi.x, t.x), maxf(hi.y, t.y))
	if trees.is_empty():
		return Vector2(2000, 2000)
	var centre := (lo + hi) * 0.5
	var best := centre
	var best_d := 1e18
	var step := 100.0
	for gx in range(-20, 21):
		for gy in range(-20, 21):
			var p := centre + Vector2(gx, gy) * step
			var ok := true
			for t in trees:
				if Vector2(t.x, t.y).distance_to(p) < 260.0:
					ok = false
					break
			if ok and p.distance_squared_to(centre) < best_d:
				best = p
				best_d = p.distance_squared_to(centre)
	return best


func _wait(frames: int) -> void:
	for k in frames:
		await get_tree().process_frame


func _health(horde: int) -> float:
	var h := 0.0
	var o: Dictionary = _world.get_object(horde)
	for m in o.get("members", []):
		var mo: Dictionary = _world.get_object(m)
		h += mo.get("health", 0.0)
	return h


func _pos(id: int) -> Vector3:
	var o: Dictionary = _world.get_object(id)
	return Vector3(o.get("x", 0.0), o.get("y", 0.0), o.get("z", 0.0))


func _follow(id: int, back: Vector2, height: float) -> void:
	var p := _pos(id)
	_aim(Vector3(p.x, p.y, p.z + 30.0), Vector2(p.x + back.x, p.y + back.y), height)




var _only := ""


var _thrown_total := 0
var _seen_thrown := {}


## the riders' horde speed (its locomotor's, per logic frame) and the thrown men so far
func _report_line(riders: int, victims: int) -> String:
	for m in _members:
		var conds: Array = _world.get_object(m).get("conditions", [])
		if "STUNNED_FLAILING" in conds and not _seen_thrown.has(m):
			_seen_thrown[m] = true
			_thrown_total += 1
	return "thrown so far %d, riders' speed %.1f" % [_thrown_total, float(_world.get_object(riders).get("speed", 0.0))]


var _members: Array = []


## the victims' stun states, over the members the horde had at the start (a crushed man leaves his horde when he dies)
func _flailing(horde: int) -> Array:
	var counts := [0, 0, 0]
	for m in _members:
		var conds: Array = _world.get_object(m).get("conditions", [])
		if "STUNNED_FLAILING" in conds:
			counts[0] += 1
		if "STUNNED" in conds:
			counts[1] += 1
		if "STUNNED_STANDING_UP" in conds:
			counts[2] += 1
	return counts


func _charge(victim_template: String, porcupine: bool, spot: Vector2, tag: String) -> void:
	var victims: int = _world.create_object(victim_template, _local, spot.x + 150.0, spot.y, PI)
	_thrown_total = 0
	await _wait(5)
	if porcupine:
		print("COMBAT3VIEW toggle ", JSON.stringify(_world.order_toggle_formation(victims)))
		await _wait(60)
	var riders: int = _world.create_object("RohanRohirrimHorde", _enemy, spot.x - 450.0, spot.y, 0.0)
	await _wait(5)
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	_aim(Vector3(spot.x + 100.0, spot.y, g0 + 15.0), Vector2(spot.x + 40.0, spot.y - 300.0), 120.0)
	print("COMBAT3VIEW charge ", JSON.stringify(_world.debug_ai_attack(riders, victims)))
	var saved_thrown := false
	var saved_lying := false
	var saved_first := false
	_members = _world.get_object(victims).get("members", []).duplicate()
	var f0: int = _world.get_frame()
	var k := 0
	while _world.get_frame() < f0 + 200:
		await get_tree().process_frame
		k = _world.get_frame() - f0
		var rp := _pos(riders)
		var focus_x := clampf(rp.x + 60.0, spot.x - 300.0, spot.x + 260.0)
		_aim(Vector3(focus_x, spot.y, g0 + 15.0), Vector2(focus_x - 60.0, spot.y - 300.0), 120.0)
		var c := _flailing(victims)
		_label.text = "%s\nflailing %d, lying %d, standing up %d   %s" % [tag, c[0], c[1], c[2], _report_line(riders, victims)]
		if k >= 40 and not saved_first:
			_save("%s" % ("porcupine" if porcupine else "charge"))
			saved_first = true
		if not porcupine and not saved_thrown and c[0] >= 3:
			await get_tree().process_frame
			_save("thrown")
			saved_thrown = true
		if not porcupine and not saved_lying and c[1] + c[2] >= 1:
			await get_tree().process_frame
			_save("stunned")
			saved_lying = true
	print("COMBAT3VIEW %s: %s" % [tag, _report_line(riders, victims)])
	_thrown_total = 0
	_seen_thrown = {}
	await _clear([riders, victims])


func _grond(spot: Vector2) -> void:
	var grond: int = _world.create_object("MordorGrond", _enemy, spot.x - 300.0, spot.y - 100.0, 0.0)
	await _wait(5)
	# the crew is JUST_BUILT (Model None) for BuildFadeInOnCreateTime = 16 s (80 logic frames), as Grond fades in
	var f0: int = _world.get_frame()
	while _world.get_frame() < f0 + 95:
		await get_tree().process_frame
		_follow(grond, Vector2(-70.0, -250.0), 130.0)
		var g: Dictionary = _world.get_garrison(grond)
		_label.text = "Grond and its six trolls on the crew bones (crew %d, speed x%.2f)" % [g.get("crew", 0), g.get("crew_power", 0.0)]
	_save("grond")
	_world.order_move([grond], spot.x + 300.0, spot.y - 100.0)
	f0 = _world.get_frame()
	var saved := false
	while _world.get_frame() < f0 + 120:
		await get_tree().process_frame
		_follow(grond, Vector2(-70.0, -250.0), 130.0)
		var g: Dictionary = _world.get_garrison(grond)
		_label.text = "Grond rolls with its crew (crew %d, speed x%.2f)" % [g.get("crew", 0), g.get("crew_power", 0.0)]
		if not saved and _world.get_frame() >= f0 + 60:
			_save("grond-moving")
			saved = true
	print("COMBAT3VIEW grond ", JSON.stringify(_world.get_garrison(grond)))
	await _clear([grond])


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	print("COMBAT3VIEW spot (%.0f, %.0f) local %d enemy %d" % [spot.x, spot.y, _local, _enemy])
	if _only.is_empty() or _only == "charge":
		await _charge("GondorFighterHorde", false, spot, "Rohirrim charge Gondor soldiers: CrushKnockback throws the trampled men; the riders slow at each crush")
	if _only.is_empty() or _only == "pikes":
		await _charge("GondorTowerShieldGuardHorde", true, spot, "The same charge into tower guards in porcupine formation: CRUSHED_DECELERATE 1000% stops it at the pikes")
	if _only.is_empty() or _only == "grond":
		await _grond(spot)
	print("COMBAT3VIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("COMBAT3VIEW SAVED ", path)


func _clear(ids: Array) -> void:
	for id in ids:
		if not _world.get_object(id).get("destroyed", true):
			_world.destroy_object(id)
	await _wait(10)
