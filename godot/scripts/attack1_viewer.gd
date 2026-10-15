## ATTACK-1 viewer: a mounted hero ordered onto an enemy horde in a live retail game (the proof of S-3807's fix: blockedBy RW 0x66D16E lets a unit pass an
## enemy it may crush, RW 0x69519A with its live crusher level). For the lane's proof screenshots.
##
##   godot --path godot res://scenes/attack1_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --hero=<name>     the hero (default AngmarWitchking; MordorMouthOfSauron)
##   --shots=<dir>     saves <prefix>-order.png, -ride.png, -strike.png, -after.png there (auto-acquire is switched off: only the order acts)
##   --prefix=<name>   the screenshot prefix (default attack1)
##   --gap=<f>         how far from the horde the hero starts (default 200)
extends Node3D

var _map_name := "map mp fall back 4p"
var _hero := "AngmarWitchking"
var _shots := ""
var _prefix := "attack1"
var _gap := 200.0
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--hero="):
			_hero = arg.substr(7)
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--gap="):
			_gap = float(arg.substr(6))
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
		{"player": "Player_1", "faction": "FactionAngmar" if _hero.begins_with("Angmar") else "FactionMordor", "human": true, "team": 0, "start_index": 0},
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
	_world.set_local_player(_local)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 34)
	_label.add_theme_font_size_override("font_size", 22)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 5)
	layer.add_child(_label)
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("ATTACK1VIEW FAIL: ", message)
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


func _alive(members: Array) -> int:
	var n := 0
	for m in members:
		var mo: Dictionary = _world.get_object(int(m))
		n += 1 if mo.get("ok", false) and float(mo.get("health", 0.0)) > 0.0 else 0
	return n


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	print("ATTACK1VIEW auto-acquire ", JSON.stringify(_world.debug_set_auto_acquire(false)))
	# a produced hero mounts when its production completes (ToggleMountedSpecialAbilityUpdate TriggerInstantlyOnCreate at onBuildComplete, RW 0x8B11FE); a
	# created one gets the same call through the viewer hook. Then wait until it stands MOUNTED
	var hero: int = _world.create_object(_hero, _local, spot.x + 50.0 - _gap, spot.y, 0.0)
	var horde: int = _world.create_object("MordorFighterHorde", _enemy, spot.x + 50.0, spot.y, PI)
	await _wait(5)
	print("ATTACK1VIEW build complete ", JSON.stringify(_world.debug_build_complete(hero)))
	var f_made: int = _world.get_frame()
	while _world.get_frame() < f_made + 150 and not ("MOUNTED" in _world.get_object(hero).get("conditions", [])):
		await get_tree().process_frame
	if not ("MOUNTED" in _world.get_object(hero).get("conditions", [])):
		_fail("%s did not mount" % _hero)
		return
	await _wait(40)
	var members: Array = _world.get_object(horde).get("members", []).duplicate()
	var conds: Array = _world.get_object(hero).get("conditions", [])
	print("ATTACK1VIEW %s %d (mounted %s), horde %d with %d members" % [_hero, hero, "MOUNTED" in conds, horde, members.size()])
	var g: float = _world.get_ground_height(spot.x, spot.y)
	_aim(Vector3(spot.x - 40.0, spot.y, g + 20.0), Vector3(spot.x - 55.0, spot.y - 230.0, g + 150.0))
	print("ATTACK1VIEW order ", JSON.stringify(_world.order_attack([hero], horde)))
	var f0: int = _world.get_frame()
	var saved := {}
	while _world.get_frame() < f0 + 150:
		await get_tree().process_frame
		var k: int = _world.get_frame() - f0
		# the camera follows the point between the hero and the horde (the horde object goes with its last member: then the hero alone)
		var hp := _pos(hero)
		var ho: Dictionary = _world.get_object(horde)
		var mid := Vector2(hp.x, hp.y)
		if ho.get("ok", false):
			mid = (mid + Vector2(ho.x, ho.y)) * 0.5
		var mg: float = _world.get_ground_height(mid.x, mid.y)
		_aim(Vector3(mid.x, mid.y, mg + 20.0), Vector3(mid.x - 15.0, mid.y - 230.0, mg + 150.0))
		var alive := _alive(members)
		_label.text = "%s, mounted, ordered onto an enemy horde (a player's attack order; auto-acquire off)\nframe %d after the order: %d of the horde's %d members alive" % [_hero, k, alive, members.size()]
		for pair in [["order", 1], ["ride", 9], ["strike", 30], ["after", 149]]:
			if k >= int(pair[1]) and not saved.has(pair[0]):
				_save(pair[0])
				saved[pair[0]] = true
				print("ATTACK1VIEW %s at frame %d: %d of %d alive" % [pair[0], k, alive, members.size()])
	print("ATTACK1VIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("ATTACK1VIEW SAVED ", path)
