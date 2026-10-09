## Siege viewer (lane PROJ-2): a close side view of siege engines shelling buildings in a live game, one engine after the other - the arm's throw, the stone's
## arc, the impact. For the lane's video and screenshots; it changes nothing in the game.
##
##   godot --path godot res://scenes/siege_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>            the map (default "map mp fall back 4p")
##   --engines=<a,b,...>     the siege engines, each Template[@EnemyBuilding[@distance]] (default: the Gondor trebuchet, the Mordor catapult, the Dwarven catapult,
##                           the Isengard ballista, the Angmar troll sling, each against an orc pit or a Gondor barracks)
##   --dist=<d>              the distance from the engine to its building (default 430)
##   --frames=<n>            render frames per engine (default 600)
##   --speed=<f>             the logic speed (default 0.5: slow motion)
##   --shots=<dir>           saves <prefix>-<engine>-apex.png when a stone of that engine is at its apex, and <prefix>-<engine>-throw.png at the release
##   --prefix=<name>         the screenshot prefix (default proj2)
##   --keep-trees            do not hide the map's client-only trees and props
##   --north                 film from the north of the line of fire (default: the south)
extends Node3D

var _map_name := "map mp fall back 4p"
var _engines: Array = ["GondorTrebuchet@MordorOrcPit", "MordorCatapult@GondorBarracks", "DwarvenCatapult@MordorOrcPit", "IsengardBallista@GondorBarracks@370",
	"AngmarTrollSling@GondorBarracks"]
var _dist := 430.0
var _default_dist := 430.0
var _frames := 600
var _speed := 0.5
var _shots := ""
var _prefix := "proj2"
var _keep_trees := false
var _close_after := 10.0  # render frames after the stone leaves the arm that the close view stays
var _impact_frames := 70  # render frames after the stone lands that the wide view stays
var _side := -1.0 # the camera stands south (-1) or north (+1) of the line of fire
var _fs: RefCounted
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--engines="):
			_engines = Array(arg.substr(10).split(","))
		elif arg.begins_with("--dist="):
			_default_dist = float(arg.substr(7))
		elif arg.begins_with("--frames="):
			_frames = int(arg.substr(9))
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg == "--keep-trees":
			_keep_trees = true
		elif arg == "--north":
			_side = 1.0
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
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
	_camera.fov = 50
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(_fs, _map_name, {"markers": false, "fog": false})
	if root == null:
		_fail("build_map failed: %s" % [builder.get_report().errors])
		return
	add_child(root)
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(_fs)
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
	_hide_static_scenery()
	_world.set_auto_advance(true)
	_world.set_time_scale(_speed)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_label)
	_run()


## the map's client-only trees and props (the static instancer, the one with the most instances) hide nothing of the shot: a clear view of the engine, the arc and the building
func _hide_static_scenery() -> void:
	if _keep_trees:
		return
	var best: Node = null
	var most := -1
	for c in _world.get_children():
		if c.get_class() == "W3DInstancer":
			var n: int = c.get_instance_count()
			if n > most:
				most = n
				best = c
	if best != null:
		best.visible = false
		print("SIEGEVIEW static scenery hidden (%d instances)" % most)


func _fail(message: String) -> void:
	push_error(message)
	print("SIEGEVIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


## a stretch of ground in the middle of the map (the centre of the bounding box of the map's objects); the engines are filmed there one after the other
var _centre := Vector2.INF


func _base() -> Vector2:
	if _centre != Vector2.INF:
		return Vector2(_centre.x - _dist * 0.5, _centre.y)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
			hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	_centre = (lo + hi) * 0.5
	return Vector2(_centre.x - _dist * 0.5, _centre.y)


func _run() -> void:
	await get_tree().process_frame
	var i := 0
	for spec in _engines:
		var parts: PackedStringArray = String(spec).split("@")
		var engine: String = parts[0]
		var target: String = parts[1] if parts.size() > 1 else "GondorBarracks"
		_dist = float(parts[2]) if parts.size() > 2 else _default_dist
		await _film(i, engine, target)
		i += 1
	print("SIEGEVIEW DONE")
	get_tree().quit(0)


func _film(i: int, engine: String, target: String) -> void:
	var base := _base()
	var eid: int = _world.create_object(engine, _local, base.x, base.y, 0.0)
	var tid: int = _world.create_object(target, _enemy, base.x + _dist, base.y, PI)
	print("SIEGEVIEW %s object %d at (%.0f, %.0f) -> %s object %d" % [engine, eid, base.x, base.y, target, tid])
	if eid <= 0 or tid <= 0:
		_fail("could not create %s / %s" % [engine, target])
		return
	var eo: Dictionary = _world.get_object(eid)
	var ground: float = eo.get("z", 0.0)
	# two views: CLOSE on the engine for the wind-up and the arm's throw, then a WIDE side view from the south for the arc and the impact on the building
	var close_focus := Vector3(base.x + 25.0, base.y, ground + 38.0)
	_aim(close_focus, Vector2(base.x + 20.0, base.y + _side * 230.0), ground + 45.0, 38.0)
	for k in 20:
		await get_tree().process_frame
	print("SIEGEVIEW ORDER ", _world.order_attack([eid], tid))
	var known := {}
	var apex_saved := false
	var throw_saved := false
	var wide := false
	var wide_from := 0
	var over_at := -1
	for f in _frames:
		await get_tree().process_frame
		_label.text = "%s  (logic speed %.2f)  frame %d" % [engine, _speed, _world.get_frame()]
		var live := 0
		for id in _world.get_object_ids():
			if id == eid or id == tid:
				continue
			var o: Dictionary = _world.get_object(id)
			if not o.get("ok", false):
				continue
			var tmpl: String = o.get("template", "")
			if not known.has(id):
				# a stone of this engine: a projectile or a thrown rock first seen near the engine (the launch already took its second path point)
				if tmpl.findn("Projectile") < 0 and tmpl.findn("Rock") < 0:
					continue
				if Vector2(o.x, o.y).distance_to(base) > 220.0:
					continue
				known[id] = {"last_z": o.z, "rising": true, "start": Vector3(o.x, o.y, o.z)}
				print("SIEGEVIEW %s stone %d (%s) at (%.1f, %.1f, %.1f)" % [engine, id, tmpl, o.x, o.y, o.z])
				if not throw_saved and not _shots.is_empty():
					throw_saved = true
					_save("%s-%s-throw.png" % [_prefix, engine])
				if not wide:
					wide_from = f + int(_close_after)
				continue
			live += 1
			var k: Dictionary = known[id]
			if k.rising and o.z < k.last_z:
				k.rising = false
				print("SIEGEVIEW %s stone %d apex near (%.1f, %.1f) z %.1f (%.1f above its start)" % [engine, id, o.x, o.y, k.last_z, k.last_z - k.start.z])
				if not apex_saved and not _shots.is_empty():
					apex_saved = true
					_save("%s-%s-apex.png" % [_prefix, engine])
			k.last_z = o.z
		if not wide and wide_from > 0 and f >= wide_from:
			wide = true
			var mid := Vector3(base.x + _dist * 0.5, base.y, ground + 70.0)
			_aim(mid, Vector2(mid.x, mid.y + _side * 440.0), ground + 75.0, 50.0)
		# the end: the first stone is down (no tracked stone left in the air) and the impact has had its moment
		if known.size() > 0 and wide and live == 0:
			if over_at < 0:
				over_at = f
			elif f - over_at > _impact_frames:
				break
		elif live > 0:
			over_at = -1
	print("SIEGEVIEW engine state ", JSON.stringify(_world.get_object(eid)).substr(0, 600))
	print("SIEGEVIEW target state ", JSON.stringify(_world.get_object(tid)).substr(0, 400))
	var cr: Dictionary = _world.get_combat_report()
	print("SIEGEVIEW %s done at logic frame %d: %d stones seen, combat %s" % [engine, _world.get_frame(), known.size(), JSON.stringify(cr).substr(0, 300)])
	_world.destroy_object(eid)
	_world.destroy_object(tid)


## the camera at `pos` (ground x / y, height at least `min_height` above the ground there) looking at `focus`
func _aim(focus: Vector3, pos: Vector2, height: float, fov: float) -> void:
	var g: float = _world.get_ground_height(pos.x, pos.y)
	_camera.fov = fov
	_camera.position = _sage_to_godot(Vector3(pos.x, pos.y, maxf(height, g + 30.0)))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


func _save(name: String) -> void:
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join(name)
	img.save_png(path)
	print("SIEGEVIEW SAVED ", path)
