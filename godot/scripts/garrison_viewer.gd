## Garrison viewer (lane GARRISON-1): a Gondor archer horde ordered into a Gondor battle tower (MSG_ENTER), the members vanishing into it, the archers shooting a
## Mordor horde from inside, the evacuate message bringing them out, then a second garrison ejected when the tower is destroyed. For the lane's screenshots and video.
##
##   godot --path godot res://scenes/garrison_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-walking.png, -inside.png, -firing.png, -evacuated.png, -ejected.png there
##   --prefix=<name>   the screenshot prefix (default garrison1)
##   --speed=<f>       the logic speed (default 1.0)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "garrison1"
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
	print("GARRISONVIEW FAIL: ", message)
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


func _garrison_text(tower: int) -> String:
	var g: Dictionary = _world.get_garrison(tower)
	return "inside: %d horde(s), %d of %d members hidden, %d on the way, %d garrison points in use" % [g.get("count", 0), g.get("members_hidden", 0), g.get("members", 0), g.get("entering", 0), g.get("points_in_use", 0)]


func _health(horde: int) -> float:
	var h := 0.0
	var o: Dictionary = _world.get_object(horde)
	for m in o.get("members", []):
		var mo: Dictionary = _world.get_object(m)
		h += mo.get("health", 0.0)
	return h


func _run() -> void:
	await get_tree().process_frame
	var spot := _pick_spot()
	var g0: float = _world.get_ground_height(spot.x, spot.y)
	print("GARRISONVIEW spot (%.0f, %.0f, %.0f)" % [spot.x, spot.y, g0])
	var tower: int = _world.create_object("GondorKeep", _local, spot.x, spot.y, 0.0)
	var archers: int = _world.create_object("GondorArcherHorde", _local, spot.x - 220.0, spot.y - 40.0, 0.0)
	if tower <= 0 or archers <= 0:
		_fail("could not create the tower / the archers")
		return
	var focus := Vector3(spot.x - 60.0, spot.y, g0 + 30.0)
	_aim(focus, Vector2(spot.x - 120.0, spot.y - 360.0), 190.0)
	_label.text = "A Gondor battle tower and a Gondor archer horde"
	await _wait(60)
	# 1. the order: MSG_ENTER through the command path
	print("GARRISONVIEW order ", JSON.stringify(_world.order_garrison([archers], tower)))
	var inside_at := -1
	for k in 900:
		await get_tree().process_frame
		var g: Dictionary = _world.get_garrison(tower)
		_label.text = "MSG_ENTER: the archers walk to the tower and go in  (%s, frame %d)" % [_garrison_text(tower), _world.get_frame()]
		if k == 40:
			_save("walking")
		if g.get("count", 0) == 1 and g.get("entering", 1) == 0 and g.get("members", 0) > 0 and g.get("members_hidden", 0) == g.get("members", 0):
			inside_at = _world.get_frame()
			break
	print("GARRISONVIEW inside at frame %d: %s" % [inside_at, JSON.stringify(_world.get_garrison(tower))])
	await _wait(30)
	_label.text = "The archers are inside the tower (GARRISONED)  (%s)" % _garrison_text(tower)
	_save("inside")
	await _wait(60)
	# 2. a Mordor horde comes; the archers shoot it from the tower
	var orcs: int = _world.create_object("MordorFighterHorde", _enemy, spot.x - 430.0, spot.y + 40.0, 0.0)
	await _wait(10)
	print("GARRISONVIEW orcs attack ", JSON.stringify(_world.order_attack([orcs], tower)))
	_aim(Vector3(spot.x - 190.0, spot.y, g0 + 30.0), Vector2(spot.x - 210.0, spot.y - 430.0), 220.0)
	var before := _health(orcs)
	var shot := false
	for k in 900:
		await get_tree().process_frame
		var hnow := _health(orcs)
		_label.text = "Mordor fighters approach; the garrisoned archers shoot from the tower  (orc health %.0f / %.0f)" % [hnow, before]
		if not shot and hnow < before * 0.8:
			_save("firing")
			shot = true
		if shot and hnow < before * 0.45:
			_save("firing2")
			break
	print("GARRISONVIEW orcs %.0f -> %.0f; combat %s" % [before, _health(orcs), JSON.stringify(_world.get_combat_report()).substr(0, 400)])
	# 3. the evacuate message
	_aim(focus, Vector2(spot.x - 120.0, spot.y - 360.0), 190.0)
	print("GARRISONVIEW evacuate ", JSON.stringify(_world.order_evacuate(tower)))
	for k in 150:
		await get_tree().process_frame
		_label.text = "MSG_EVACUATE: the archers come out  (%s)" % _garrison_text(tower)
		if k == 60:
			_save("evacuated")
	print("GARRISONVIEW after evacuate: %s" % JSON.stringify(_world.get_garrison(tower)))
	# 4. in again; the tower is destroyed and ejects its garrison
	if not _world.get_object(orcs).get("destroyed", true):
		_world.destroy_object(orcs)
	await _wait(30)
	print("GARRISONVIEW in again ", JSON.stringify(_world.order_garrison([archers], tower)))
	for k in 600:
		await get_tree().process_frame
		_label.text = "In again  (%s)" % _garrison_text(tower)
		var g: Dictionary = _world.get_garrison(tower)
		if g.get("count", 0) == 1 and g.get("entering", 1) == 0 and g.get("members", 0) > 0 and g.get("members_hidden", 0) == g.get("members", 0):
			break
	print("GARRISONVIEW before the destruction: %s" % JSON.stringify(_world.get_garrison(tower)))
	_label.text = "In again  (%s)" % _garrison_text(tower)
	_save("inside-again")
	await _wait(30)
	_world.damage_object(tower, 1.0e7)
	for k in 200:
		await get_tree().process_frame
		_label.text = "The tower is destroyed: EjectPassengersOnDeath puts the archers out  (archers: %s)" % JSON.stringify(_world.get_object(archers)).substr(0, 60)
		if k == 40:
			_save("ejected")
	print("GARRISONVIEW archers at the end: %s" % JSON.stringify(_world.get_object(archers)).substr(0, 300))
	print("GARRISONVIEW report stops: %s" % JSON.stringify(_world.get_report()).substr(0, 300))
	print("GARRISONVIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("GARRISONVIEW SAVED ", path)
