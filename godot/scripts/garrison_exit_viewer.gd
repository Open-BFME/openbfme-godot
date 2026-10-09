## Garrison exit viewer (lane GARRISON-3, the owner's feedback G6): a Gondor archer horde garrisons a Gondor battle tower and is evacuated; the members come out
## at the contain's EntryPosition (the tower's base, GarrisonContain's exit RW 0x87CA2B), all from one point, and walk to their formation slots once the horde
## object, which leaves last, takes them back (RW 0x8759FF). For the lane's screenshots and video.
##
##   godot --path godot res://scenes/garrison_exit_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-<tower>-<frame>.png there
##   --prefix=<name>   the screenshot prefix (default garrison3)
##   --speed=<f>       the logic speed (default 0.5: the exit is short)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "garrison3"
var _speed := 0.5
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
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": true, "team": 1, "start_index": 1},
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
	print("GARRISONEXIT FAIL: ", message)
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


func _members_at(ids: Array, p: Vector2, r: float) -> int:
	var n := 0
	for m in ids:
		var o: Dictionary = _world.get_object(m)
		if o.get("ok", false) and Vector2(o.x, o.y).distance_to(p) < r:
			n += 1
	return n


func _positions(ids: Array) -> String:
	var parts := []
	for m in ids:
		var o: Dictionary = _world.get_object(m)
		if o.get("ok", false):
			parts.append("(%.0f,%.0f)" % [o.x, o.y])
	return " ".join(parts)


func _scene(tower_name: String, spot: Vector2, angle: float, entry: float, tag: String) -> void:
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	var fwd := Vector2(cos(angle), sin(angle))
	var tower: int = _world.create_object(tower_name, _local, spot.x, spot.y, angle)
	var archers: int = _world.create_object("GondorArcherHorde", _local, spot.x + fwd.x * 260.0, spot.y + fwd.y * 260.0 - 60.0, angle)
	if tower <= 0 or archers <= 0:
		_fail("could not create %s / the archers" % tower_name)
		return
	var exit_spot := spot + fwd * entry
	var side := Vector2(-fwd.y, fwd.x)
	var cam := exit_spot + fwd * 200.0 - side * 300.0
	_aim(Vector3(exit_spot.x + fwd.x * 70.0, exit_spot.y + fwd.y * 70.0, g0 + 10.0), cam, 170.0)
	_label.text = "%s and a Gondor archer horde" % tower_name
	await _wait(40)
	var all_ids: Array = _world.get_object(archers).get("members", [])
	print("GARRISONEXIT %s horde members before the order: %d" % [tag, all_ids.size()])
	print("GARRISONEXIT order ", JSON.stringify(_world.order_garrison([archers], tower)))
	_world.set_time_scale(2.0) # the walk in, faster
	for k in 6000:
		await get_tree().process_frame
		var g: Dictionary = _world.get_garrison(tower)
		_label.text = "The archers go into the %s  (frame %d)" % [tower_name, _world.get_frame()]
		if g.get("count", 0) == 1 and g.get("entering", 1) == 0 and g.get("members", 0) > 0 and g.get("members_hidden", 0) == g.get("members", 0):
			break
	var ids: Array = _world.get_object(archers).get("members", [])
	print("GARRISONEXIT   garrison %s" % JSON.stringify(_world.get_garrison(tower)))
	print("GARRISONEXIT %s inside at frame %d, %d members" % [tag, _world.get_frame(), ids.size()])
	_label.text = "Inside the %s" % tower_name
	await _wait(40)
	_save("%s-inside" % tag)
	_world.set_time_scale(_speed) # the exit, slowed down
	var f0: int = _world.get_frame()
	print("GARRISONEXIT evacuate ", JSON.stringify(_world.order_evacuate(tower)))
	var saved := {}
	for k in 2400:
		await get_tree().process_frame
		var f: int = _world.get_frame() - f0
		var at_exit := _members_at(ids, exit_spot, 3.0)
		_label.text = "MSG_EVACUATE, logic frame +%d: the members leave through the tower's exit (%d of %d at the exit point), then walk to their slots" % [f, at_exit, ids.size()]
		for want in [1, 3, 6, 12, 25, 50]:
			if f >= want and not saved.has(want):
				saved[want] = true
				_save("%s-exit-%02d" % [tag, want])
				print("GARRISONEXIT %s frame +%d: %d at the exit; %s" % [tag, f, at_exit, _positions(ids)])
		if f >= 70:
			break
	await _wait(30)


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	print("GARRISONEXIT spot (%.0f, %.0f)" % [spot.x, spot.y])
	await _scene("GondorKeep", spot, 0.0, 0.0, "tower")
	# the garrison tower expansion (EntryPosition X:35) is the arena test's (test_hud_garrison_exit.cpp): standing alone on this map its archers met the map's
	# creeps on the way in
	print("GARRISONEXIT DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("GARRISONEXIT SAVED ", path)
