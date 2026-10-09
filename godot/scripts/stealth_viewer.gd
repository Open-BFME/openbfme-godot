## Stealth viewer (lane STEALTH-1): a Gondor Ranger horde camouflaged among a map's trees, seen by its owner (translucent), by the enemy (not drawn while Mordor
## archers stand in sight), then revealed by an enemy skull totem (drawn, attacked). For the lane's screenshots and video; scenario helpers only, no command.
##
##   godot --path godot res://scenes/stealth_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-own-translucent.png, <prefix>-enemy-invisible.png, <prefix>-enemy-detected.png and <prefix>-attacked.png there
##   --prefix=<name>   the screenshot prefix (default stealth1)
##   --speed=<f>       the logic speed (default 1.0)
##   --gap=<d>         the archers' distance from the rangers (default 250)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "stealth1"
var _speed := 1.0
var _gap := 250.0
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
		elif arg.begins_with("--gap="):
			_gap = float(arg.substr(6))
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
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
	print("STEALTHVIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


## the camera at ground (x, y) + `height`, looking at `focus`
func _aim(focus: Vector3, pos: Vector2, height: float) -> void:
	var g: float = _world.get_ground_height(pos.x, pos.y)
	_camera.position = _sage_to_godot(Vector3(pos.x, pos.y, g + height))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


## a terrain tree near the middle of the map's objects with open ground to its east (no other tree within 60 of the rangers' and the archers' spots)
func _pick_tree() -> Vector3:
	var trees: PackedVector3Array = _world.get_terrain_trees(-1)
	print("STEALTHVIEW %d terrain trees" % trees.size())
	if trees.is_empty():
		return Vector3.INF
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for t in trees:
		lo = Vector2(minf(lo.x, t.x), minf(lo.y, t.y))
		hi = Vector2(maxf(hi.x, t.x), maxf(hi.y, t.y))
	var centre := (lo + hi) * 0.5
	var best := Vector3.INF
	var best_d := 1e18
	for t in trees:
		var d := Vector2(t.x, t.y).distance_squared_to(centre)
		if d >= best_d:
			continue
		var open := true
		for u in trees:
			var p := Vector2(u.x, u.y)
			if p.distance_to(Vector2(t.x + 35.0 + _gap, t.y - 10.0)) < 90.0 or (p != Vector2(t.x, t.y) and p.distance_to(Vector2(t.x + 35.0, t.y - 10.0)) < 45.0):
				open = false
				break
		if open:
			best = t
			best_d = d
	return best


func _wait(frames: int) -> void:
	for k in frames:
		await get_tree().process_frame


func _run() -> void:
	await get_tree().process_frame
	var tree := _pick_tree()
	if tree == Vector3.INF:
		_fail("the map has no terrain tree")
		return
	var at := Vector2(tree.x + 35.0, tree.y - 10.0) # on the open side of the tree, within its 50 (RW 0xBD88C4)
	print("STEALTHVIEW tree at (%.0f, %.0f, %.0f); the rangers at (%.0f, %.0f)" % [tree.x, tree.y, tree.z, at.x, at.y])
	var rangers: int = _world.create_object("GondorRangerHorde", _local, at.x, at.y, 0.0)
	if rangers <= 0:
		_fail("could not create the rangers")
		return
	print("STEALTHVIEW stance ", _world.set_stance([rangers], 3)) # HoldGround: a camouflaged horde in a hold stance does not acquire
	var ground: float = _world.get_ground_height(at.x, at.y)
	var focus := Vector3(at.x + _gap * 0.45, at.y, ground + 10.0)
	_aim(Vector3(at.x, at.y, ground + 6.0), Vector2(at.x + 70.0, at.y - 120.0), 55.0)
	# 1. the owner's view: the camouflaged rangers pulse translucent
	_world.set_local_player(_local)
	var inv: Dictionary = {}
	for k in 400:
		await get_tree().process_frame
		inv = _world.get_invisibility(rangers, _enemy)
		_label.text = "Gondor Rangers among the trees, seen by their owner  (frame %d)" % _world.get_frame()
		if inv.get("type", 2) == 1:
			break
	print("STEALTHVIEW camouflaged at frame %d: %s" % [_world.get_frame(), JSON.stringify(inv)])
	await _wait(20)
	# the friend's pulse (InvisibilityOpacityMin 0.4 .. Max 3.0, clamped to 1): the shot at a low point of it
	for k in 240:
		await get_tree().process_frame
		inv = _world.get_invisibility(rangers, _enemy)
		_label.text = "Gondor Rangers camouflaged among the trees, seen by their owner: translucent  (opacity %.2f)" % inv.get("draw_opacity", 1.0)
		if inv.get("draw_opacity", 1.0) < 0.5:
			break
	print("STEALTHVIEW own view opacity %.2f" % inv.get("draw_opacity", 1.0))
	_save("own-translucent")
	await _wait(60)
	# 2. the archers arrive; the enemy's view: the rangers are not drawn
	var archers: int = _world.create_object("MordorArcherHorde", _enemy, at.x + _gap, at.y, PI)
	print("STEALTHVIEW archers %d at %.0f" % [archers, _gap])
	_aim(focus, Vector2(focus.x - 30.0, focus.y - 300.0), 130.0)
	await _wait(30)
	_label.text = "Rangers as the enemy sees them (camouflaged)"
	await _wait(60)
	_world.set_local_player(_enemy)
	for k in 150:
		await get_tree().process_frame
		inv = _world.get_invisibility(rangers, _enemy)
		_label.text = "The enemy's view: the camouflaged rangers are not drawn  (frame %d)" % _world.get_frame()
	print("STEALTHVIEW enemy view at frame %d: %s; archers %s" % [_world.get_frame(), JSON.stringify(inv), JSON.stringify(_world.get_object(archers)).substr(0, 300)])
	_save("enemy-invisible")
	# 3. a skull totem (StealthDetectorUpdate, range 400) of the enemy reveals them; the archers attack
	var totem: int = _world.create_object("WildSkullTotem", _enemy, at.x + _gap + 40.0, at.y + 60.0, PI)
	print("STEALTHVIEW totem %d" % totem)
	var revealed_at := -1
	for k in 400:
		await get_tree().process_frame
		inv = _world.get_invisibility(rangers, _enemy)
		if revealed_at < 0 and inv.get("type", 2) == 2:
			revealed_at = _world.get_frame()
			print("STEALTHVIEW revealed at frame %d: %s" % [revealed_at, JSON.stringify(inv)])
		_label.text = ("A skull totem detects them: revealed, the archers attack  (frame %d)" % _world.get_frame()) if revealed_at >= 0 else ("A skull totem of the enemy is placed  (frame %d)" % _world.get_frame())
		if revealed_at >= 0 and k == 60:
			_save("enemy-detected")
		if revealed_at >= 0 and k == 200:
			_save("attacked")
			break
	var r: Dictionary = _world.get_object(rangers)
	print("STEALTHVIEW rangers at the end: %s" % JSON.stringify(r).substr(0, 300))
	var health := 0.0
	for m in r.get("members", []):
		var mo: Dictionary = _world.get_object(m)
		health += mo.get("health", 0.0)
	print("STEALTHVIEW rangers' members' health %.0f; report %s" % [health, JSON.stringify(_world.get_report()).substr(0, 200)])
	print("STEALTHVIEW DONE")
	get_tree().quit(0)


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("STEALTHVIEW SAVED ", path)
