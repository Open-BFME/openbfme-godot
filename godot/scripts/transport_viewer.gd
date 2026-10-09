## Transport viewer (lane GARRISON-2): the transports, siege engines and AI turrets of RotWK 2.01 running from the binary's logic: a mumak carrying its Haradrim
## archer horde on its cargo bones, moving and shooting, then dying and throwing its riders off; Grond moving with its troll crew; Rohirrim in bow mode turning in
## the saddle to shoot warg riders while their horses run. For the lane's screenshots and video.
##
##   godot --path godot res://scenes/transport_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-mumak.png, -mumak-firing.png, -mumak-death.png, -grond.png, -rohirrim.png, -rohirrim-moving.png there
##   --prefix=<name>   the screenshot prefix (default garrison2)
##   --speed=<f>       the logic speed (default 1.0)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "garrison2"
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
	print("TRANSPORTVIEW FAIL: ", message)
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


func _riders_text(mumak: int) -> String:
	var g: Dictionary = _world.get_garrison(mumak)
	return "%d horde aboard, %d members on the cargo bones" % [g.get("count", 0), g.get("members", 0)]


func _turret_text(horde: int) -> String:
	var shots := 0
	var turning := 0
	var angles := []
	for m in _world.get_object(horde).get("members", []):
		var t: Dictionary = _world.get_turret(m)
		if t.get("ok", false):
			shots += int(t.get("shots", 0))
			turning += int(t.get("turn_frames", 0))
			if angles.size() < 3:
				angles.append("%.2f" % float(t.get("angle", 0.0)))
	return "turret shots %d, turning %d, angles %s" % [shots, turning, ", ".join(angles)]


func _shots_of(horde: int) -> int:
	var shots := 0
	for m in _world.get_object(horde).get("members", []):
		shots += int(_world.get_turret(m).get("shots", 0))
	return shots


func _clear(ids: Array) -> void:
	for id in ids:
		if not _world.get_object(id).get("destroyed", true):
			_world.destroy_object(id)
	await _wait(10)


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	print("TRANSPORTVIEW spot (%.0f, %.0f, %.0f) local %d enemy %d" % [spot.x, spot.y, g0, _local, _enemy])
	# 1. the mumak and its archers
	var mumak: int = _world.create_object("MordorMumakil", _enemy, spot.x - 200.0, spot.y, 0.0)
	await _wait(5)
	_follow(mumak, Vector2(-80.0, -420.0), 230.0)
	_label.text = "MordorMumakil: its Haradrim archers on the cargo bones  (%s)" % _riders_text(mumak)
	await _wait(45)
	_save("mumak")
	print("TRANSPORTVIEW mumak ", JSON.stringify(_world.get_garrison(mumak)))
	_world.order_move([mumak], spot.x + 150.0, spot.y + 60.0)
	for k in 120:
		await get_tree().process_frame
		_follow(mumak, Vector2(-80.0, -420.0), 230.0)
		_label.text = "The mumak walks; the riders stay on their bones  (%s)" % _riders_text(mumak)
	var gondor: int = _world.create_object("GondorFighterHorde", _local, spot.x + 220.0, spot.y + 60.0, 0.0)
	await _wait(5)
	var before := _health(gondor)
	var shot := false
	for k in 600:
		await get_tree().process_frame
		_follow(mumak, Vector2(60.0, -460.0), 240.0)
		var hnow := _health(gondor)
		_label.text = "The archers shoot from the mumak's back  (Gondor health %.0f / %.0f)" % [hnow, before]
		if not shot and hnow < before * 0.85:
			_save("mumak-firing")
			shot = true
		if shot and k > 450:
			break
	print("TRANSPORTVIEW soldiers %.0f -> %.0f" % [before, _health(gondor)])
	var riders: Array = _world.get_garrison(mumak).get("riders", [])
	var archers: int = riders[0] if riders.size() > 0 else -1
	_world.debug_damage_object(mumak, 1.0e7, 0)
	for k in 120:
		await get_tree().process_frame
		var alive := 0
		if archers > 0:
			for m in _world.get_object(archers).get("members", []):
				if _world.get_object(m).get("health", 0.0) > 0.0:
					alive += 1
		_label.text = "The mumak dies: its riders are thrown off and die  (alive %d)" % alive
		if k == 25:
			_save("mumak-death")
	await _clear([gondor, mumak])
	# 2. Grond and its troll crew
	var grond: int = _world.create_object("MordorGrond", _enemy, spot.x - 300.0, spot.y - 100.0, 0.0)
	await _wait(5)
	_follow(grond, Vector2(-120.0, -520.0), 280.0)
	for k in 40:
		await get_tree().process_frame
		var g: Dictionary = _world.get_garrison(grond)
		_label.text = "Grond: its troll crew on the crew bones  (crew %d, speed x%.2f)" % [g.get("crew", 0), g.get("crew_power", 0.0)]
	_save("grond")
	_world.order_move([grond], spot.x + 300.0, spot.y - 100.0)
	for k in 200:
		await get_tree().process_frame
		_follow(grond, Vector2(-120.0, -520.0), 280.0)
		var g: Dictionary = _world.get_garrison(grond)
		_label.text = "Grond rolls, pushed by its crew  (crew %d, speed x%.2f)" % [g.get("crew", 0), g.get("crew_power", 0.0)]
		if k == 120:
			_save("grond-moving")
	print("TRANSPORTVIEW grond ", JSON.stringify(_world.get_garrison(grond)))
	await _clear([grond])
	# 3. the Rohirrim's turrets
	var rohirrim: int = _world.create_object("RohanRohirrimHorde", _enemy, spot.x - 150.0, spot.y, 0.0)
	var wargs: int = _world.create_object("IsengardWargRiderHorde", _local, spot.x + 110.0, spot.y, 0.0)
	await _wait(5)
	print("TRANSPORTVIEW bow mode ", JSON.stringify(_world.debug_set_weapon_set_flag(rohirrim, "WEAPONSET_TOGGLE_1", true)))
	_aim(Vector3(spot.x - 20.0, spot.y, g0 + 30.0), Vector2(spot.x - 60.0, spot.y - 460.0), 230.0)
	print("TRANSPORTVIEW rohirrim attack ", JSON.stringify(_world.debug_ai_attack(rohirrim, wargs)))
	for k in 900:
		await get_tree().process_frame
		_label.text = "Rohirrim, bow mode: the bow is on the AI's turret  (%s)" % _turret_text(rohirrim)
		if _shots_of(rohirrim) >= 12:
			break
	_save("rohirrim")
	print("TRANSPORTVIEW ride on ", JSON.stringify(_world.debug_ai_move(rohirrim, spot.x + 150.0, spot.y + 600.0)))
	for k in 600:
		await get_tree().process_frame
		_follow(rohirrim, Vector2(260.0, -380.0), 230.0)
		_label.text = "Riding on: the riders turn their bows and shoot  (%s)" % _turret_text(rohirrim)
		if k == 150:
			_save("rohirrim-moving")
	print("TRANSPORTVIEW turrets: %s; rohirrim at %s" % [_turret_text(rohirrim), _pos(rohirrim)])
	print("TRANSPORTVIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("TRANSPORTVIEW SAVED ", path)
