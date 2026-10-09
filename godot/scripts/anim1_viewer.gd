## Combat animation viewer (lane ANIM-1, FEEDBACK-2 G1 / G3 / G5 / G8): a close side view of one attacker fighting one target in a live game, scene after
## scene - a melee horde at a barracks and at a horde, an archer horde's firing cycle, the mountain troll's swing, the Rohirrim shooting in bow mode
## standing and riding. For the lane's before / after clips; it changes nothing in the game.
##
##   godot --path godot res://scenes/anim1_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>          the map (default "map mp fall back 4p")
##   --scenes=<a,b,...>    each Attacker@Target[@distance][@bow][@ride]: bow sets WEAPONSET_TOGGLE_1 on the attacker and its members, ride orders the
##                         attacker away past the target half way through the scene (default: the five FEEDBACK-2 scenes)
##   --frames=<n>          render frames per scene (default 420)
##   --speed=<f>           the logic speed (default 0.5: slow motion)
##   --title=<text>        a caption prefix (e.g. "before" / "after")
extends Node3D

var _map_name := "map mp fall back 4p"
var _scenes: Array = ["GondorFighterHorde@MordorBarracks@260", "GondorFighterHorde@MordorFighterHorde@220", "GondorArcherHorde@MordorFighterHorde@260",
	"MordorMountainTroll@GondorFighterHorde@110", "RohanRohirrimHorde@MordorFighterHorde@260@bow@ride"]
var _frames := 420
var _speed := 0.5
var _title := ""
var _fs: RefCounted
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1
var _centre := Vector2.INF


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--scenes="):
			_scenes = Array(arg.substr(9).split(","))
		elif arg.begins_with("--frames="):
			_frames = int(arg.substr(9))
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--title="):
			_title = arg.substr(8)
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
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
	# the attacker is the computer player's: a human player's units do not attack what its shroud fogs
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
	_label.add_theme_font_size_override("font_size", 22)
	layer.add_child(_label)
	_run()


func _hide_static_scenery() -> void:
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


func _fail(message: String) -> void:
	push_error(message)
	print("ANIM1VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _base(dist: float) -> Vector2:
	if _centre == Vector2.INF:
		var lo := Vector2(1e9, 1e9)
		var hi := Vector2(-1e9, -1e9)
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if o.get("ok", false):
				lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
				hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
		_centre = (lo + hi) * 0.5
	return Vector2(_centre.x - dist * 0.5, _centre.y)


func _run() -> void:
	await get_tree().process_frame
	for spec in _scenes:
		await _film(String(spec))
	print("ANIM1VIEW DONE")
	get_tree().quit(0)


func _film(spec: String) -> void:
	var parts: PackedStringArray = spec.split("@")
	var attacker: String = parts[0]
	var target: String = parts[1] if parts.size() > 1 else "MordorBarracks"
	var dist: float = float(parts[2]) if parts.size() > 2 else 240.0
	var bow := parts.has("bow")
	var ride := parts.has("ride")
	var base := _base(dist)
	var aid: int = _world.create_object(attacker, _enemy, base.x, base.y, 0.0)
	var tid: int = _world.create_object(target, _local, base.x + dist, base.y, PI)
	print("ANIM1VIEW %s object %d -> %s object %d" % [attacker, aid, target, tid])
	if aid <= 0 or tid <= 0:
		_fail("could not create %s / %s" % [attacker, target])
		return
	var ground: float = _world.get_object(aid).get("z", 0.0)
	for k in 10:
		await get_tree().process_frame
	_world.order_stop([tid])
	if bow:
		print("ANIM1VIEW bow mode ", _world.debug_set_weapon_set_flag(aid, "WEAPONSET_TOGGLE_1", true))
		for k in 30:
			await get_tree().process_frame
	print("ANIM1VIEW ORDER ", _world.order_attack([aid], tid))
	var caption := "%s%s -> %s%s%s" % [("" if _title.is_empty() else _title + ": "), attacker, target, (" (bow mode)" if bow else ""), ("" if not ride else ", riding away at half time")]
	for f in _frames:
		await get_tree().process_frame
		if ride and f == _frames / 2:
			print("ANIM1VIEW RIDE ", _world.order_move([aid], base.x + dist * 0.5, base.y + 700.0, {}))
		var ao: Dictionary = _world.get_object(aid)
		var to: Dictionary = _world.get_object(tid)
		var focus := Vector2(base.x + dist * 0.5, base.y)
		if ao.get("ok", false) and to.get("ok", false):
			focus = (Vector2(ao.x, ao.y) + Vector2(to.x, to.y)) * 0.5
		elif ao.get("ok", false):
			focus = Vector2(ao.x, ao.y)
		var span := 160.0
		if ao.get("ok", false) and to.get("ok", false):
			span = maxf(160.0, Vector2(ao.x, ao.y).distance_to(Vector2(to.x, to.y)) * 0.9 + 80.0)
		_aim(Vector3(focus.x, focus.y, ground + 20.0), Vector2(focus.x, focus.y - span * 1.3), ground + span * 0.55, 45.0)
		_label.text = "%s   frame %d" % [caption, _world.get_frame()]
	print("ANIM1VIEW %s done at logic frame %d, combat %s" % [attacker, _world.get_frame(), JSON.stringify(_world.get_combat_report()).substr(0, 300)])
	_world.destroy_object(aid)
	_world.destroy_object(tid)
	for k in 5:
		await get_tree().process_frame


func _aim(focus: Vector3, pos: Vector2, height: float, fov: float) -> void:
	var g: float = _world.get_ground_height(pos.x, pos.y)
	_camera.fov = fov
	_camera.position = _sage_to_godot(Vector3(pos.x, pos.y, maxf(height, g + 30.0)))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)
