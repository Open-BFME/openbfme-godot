## FLY-1 viewer: a fell beast flies and fights in a live retail game (GiantBirdAIUpdate, the aerial path and the GIANT_BIRD movers ported from the binary). For
## the lane's proof screenshots and clip.
##
##   godot --path godot res://scenes/fly1_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-takeoff.png, -swoop.png, -strike.png, -climb.png, -grab.png, -toss.png there
##   --prefix=<name>   the screenshot prefix (default fly1)
##   --speed=<f>       the logic speed (default 1.0)
##   --only=<part>     strike (Khamul's fell beast on a Gondor barracks) or grab (Khamul's on a Gondor soldier horde); default both
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "fly1"
var _speed := 1.0
var _only := ""
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
	env.background_color = Color(0.45, 0.55, 0.7)
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
	print("FLY1VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _aim(focus: Vector3, eye: Vector3) -> void:
	_camera.position = _sage_to_godot(eye)
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


## an open spot near the middle of the map's trees: no tree within 300 of it
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
	for gx in range(-20, 21):
		for gy in range(-20, 21):
			var p := centre + Vector2(gx, gy) * 100.0
			var ok := true
			for t in trees:
				if Vector2(t.x, t.y).distance_to(p) < 300.0:
					ok = false
					break
			if ok and p.distance_squared_to(centre) < best_d:
				best = p
				best_d = p.distance_squared_to(centre)
	return best


func _wait(frames: int) -> void:
	for k in frames:
		await get_tree().process_frame


func _pos(id: int) -> Vector3:
	var o: Dictionary = _world.get_object(id)
	return Vector3(o.get("x", 0.0), o.get("y", 0.0), o.get("z", 0.0))


## the camera beside the course, looking at the point between the flyer and its target
func _frame_pair(a: int, b: int) -> void:
	var pa := _pos(a)
	var pb := _pos(b)
	var mid := (pa + pb) * 0.5
	var span := maxf(Vector2(pa.x - pb.x, pa.y - pb.y).length(), 150.0)
	var g: float = _world.get_ground_height(mid.x, mid.y)
	_aim(Vector3(mid.x, mid.y, g + 40.0), Vector3(mid.x - 40.0, mid.y - span * 1.1 - 120.0, g + 90.0 + span * 0.25))


func _height(id: int) -> float:
	var p := _pos(id)
	return p.z - _world.get_ground_height(p.x, p.y)


func _strike(spot: Vector2) -> void:
	var barracks: int = _world.create_object("GondorBarracks", _local, spot.x + 250.0, spot.y, PI)
	var beast: int = _world.create_object("KhamulFellBeast", _enemy, spot.x - 450.0, spot.y, 0.0)
	await _wait(40) # the weapon set loads (ReadyStatusSharedWithinSet)
	var h0: float = _world.get_object(barracks).get("health", 0.0)
	print("FLY1VIEW strike ", JSON.stringify(_world.debug_ai_attack(beast, barracks)))
	var f0: int = _world.get_frame()
	var saved := {}
	var struck_frame := -1
	while _world.get_frame() < f0 + 160:
		await get_tree().process_frame
		_frame_pair(beast, barracks)
		var k: int = _world.get_frame() - f0
		var h: float = _world.get_object(barracks).get("health", 0.0)
		var conds: Array = _world.get_object(beast).get("conditions", [])
		_label.text = "Khamul's fell beast attacks a Gondor barracks: take-off, swoop, plow strike, follow-through\nheight %.0f, barracks health %.0f / %.0f" % [_height(beast), h, h0]
		if k >= 6 and not saved.has("takeoff"):
			_save("takeoff")
			saved["takeoff"] = true
		if "ABOUT_TO_HIT" in conds and not saved.has("swoop"):
			_save("swoop")
			saved["swoop"] = true
		if h < h0 and struck_frame < 0:
			struck_frame = k
		if struck_frame >= 0 and k == struck_frame + 1:
			_save("strike") # the frame drawn after the hit
		if struck_frame >= 0 and k == struck_frame + 8 and not saved.has("climb"):
			_save("climb")
			saved["climb"] = true
	print("FLY1VIEW strike: barracks %.0f -> %.0f" % [h0, float(_world.get_object(barracks).get("health", 0.0))])
	await _clear([beast, barracks])


func _grab(spot: Vector2) -> void:
	var horde: int = _world.create_object("GondorFighterHorde", _local, spot.x + 250.0, spot.y, PI)
	var beast: int = _world.create_object("KhamulFellBeast", _enemy, spot.x - 350.0, spot.y, 0.0)
	await _wait(150) # the claw reloads its clip (ClipReloadTime 2 s) so the swoop can pick the grab
	var members: Array = _world.get_object(horde).get("members", []).duplicate()
	print("FLY1VIEW grab ", JSON.stringify(_world.debug_ai_attack(beast, horde)))
	var f0: int = _world.get_frame()
	var saved := {}
	var carried := -1
	var tossed := -1
	while _world.get_frame() < f0 + 400 and (tossed < 0 or _world.get_frame() < tossed + 40):
		await get_tree().process_frame
		_frame_pair(beast, horde)
		for m in members:
			var mo: Dictionary = _world.get_object(m)
			if int(mo.get("contained_by", 0)) == beast:
				carried = m
		var text := "Khamul's fell beast grabs a Gondor soldier, carries him up and drops him"
		if carried >= 0:
			var co: Dictionary = _world.get_object(carried)
			var lifted := _height(carried)
			text += "\nthe soldier: %.0f above the ground%s" % [lifted, (", falling" if "FREEFALL" in co.get("conditions", []) else "")]
			if int(co.get("contained_by", 0)) == beast and lifted > 40.0 and not saved.has("grab"):
				_save("grab")
				saved["grab"] = true
			if "FREEFALL" in co.get("conditions", []) and int(co.get("contained_by", 0)) == 0 and not saved.has("toss"):
				_save("toss")
				saved["toss"] = true
				tossed = _world.get_frame()
		_label.text = text
	print("FLY1VIEW grab: carried %d, tossed at %d" % [carried, tossed - f0 if tossed >= 0 else -1])
	await _clear([beast, horde])


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	print("FLY1VIEW spot (%.0f, %.0f) local %d enemy %d" % [spot.x, spot.y, _local, _enemy])
	if _only.is_empty() or _only == "strike":
		await _strike(spot)
	if _only.is_empty() or _only == "grab":
		await _grab(spot)
	print("FLY1VIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("FLY1VIEW SAVED ", path)


func _clear(ids: Array) -> void:
	for id in ids:
		if not _world.get_object(id).get("destroyed", true):
			_world.destroy_object(id)
	await _wait(10)
