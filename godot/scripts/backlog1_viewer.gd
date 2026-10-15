## BACKLOG-1 proof viewer: canEnterObject's DOZER + HARVESTER test (RW 0x82CC09). A goblin laborer (WildLaborer, DOZER + HARVESTER) is ordered into a
## goblin mine shaft (WildMineShaft, MSG_ENTER as a right click sends it): into a sound shaft it goes; a shaft below 99 percent health refuses it and it
## stays outside (before lane BACKLOG-1 the port let it into the damaged shaft).
##
##   godot --path godot res://scenes/backlog1_viewer.tscn -- --shots=<dir> [--prefix=<name>] [--map=<name>] [--speed=<f>]
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "backlog1"
var _speed := 2.0
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
	env.background_color = Color(0.45, 0.55, 0.68)
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
		{"player": "Player_1", "faction": "FactionWild", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": true, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(_map_name, {"slots": slots, "seed": 4711, "test_hooks": true})
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
	print("BACKLOG1 FAIL: ", message)
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


func _scene(spot: Vector2, damaged: bool, tag: String) -> void:
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	var shaft: int = _world.create_object("WildMineShaft", _local, spot.x, spot.y, 0.0)
	var laborer: int = _world.create_object("WildLaborer", _local, spot.x + 140.0, spot.y - 40.0, 0.0)
	if shaft <= 0 or laborer <= 0:
		_fail("could not create the shaft / the laborer")
		return
	_aim(Vector3(spot.x + 50.0, spot.y, g0 + 10.0), spot + Vector2(60.0, -260.0), 200.0)
	await _wait(30)
	var o: Dictionary = _world.get_object(shaft)
	var health := "100"
	if damaged:
		var d: Dictionary = _world.debug_damage_object(shaft, float(o.max_health) * 0.05, 0)
		if not d.get("ok", false):
			_fail("debug_damage_object: %s" % [d])
			return
		health = "95"
	_label.text = "Mine shaft at %s%% health; the laborer is ordered in (MSG_ENTER)" % health
	await _wait(20)
	_save("%s-ordered" % tag)
	print("BACKLOG1 order ", JSON.stringify(_world.order_garrison([laborer], shaft)))
	var f0: int = _world.get_frame()
	var inside := false
	while _world.get_frame() - f0 < 150:
		await get_tree().process_frame
		var l: Dictionary = _world.get_object(laborer)
		if l.get("ok", false) and int(l.get("contained_by", 0)) == shaft:
			inside = true
			break
	var l2: Dictionary = _world.get_object(laborer)
	_label.text = "Mine shaft at %s%% health, %d logic frames after the order: the laborer is %s" % [health, _world.get_frame() - f0, "inside" if inside else "outside (refused)"]
	print("BACKLOG1 %s shaft health %s%%: laborer inside=%s at (%.0f, %.0f)" % [tag, health, inside, l2.get("x", 0.0), l2.get("y", 0.0)])
	await _wait(30)
	_save("%s-result" % tag)


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	await _scene(spot, false, "sound-shaft")
	await _scene(spot + Vector2(0.0, 400.0), true, "damaged-shaft")
	print("BACKLOG1 DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("BACKLOG1 SAVED ", path)
