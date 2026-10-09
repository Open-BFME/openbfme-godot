## Arrow viewer (lane PROJ-2 round 3): a close side view of an archer horde shooting at an enemy horde in a live game - first with plain arrows, then, after the
## player's arrow upgrade (Upgrade_GondorFireArrows and its kin) is granted, with the fire / ice arrows retail swaps in. For the lane's video and screenshots;
## it changes nothing in the game.
##
##   godot --path godot res://scenes/arrow_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>            the map (default "map mp fall back 4p")
##   --archers=<Horde>       the archer horde (default GondorArcherHorde)
##   --victims=<Horde>       the enemy horde (default MordorFighterHorde)
##   --upgrade=<Upgrade>     the player upgrade granted for the second half (default Upgrade_GondorFireArrows)
##   --offset-y=<d>          the volleys stand this far north of the map's centre (default 420)
##   --dist=<d>              the distance between the hordes (default 260)
##   --frames=<n>            render frames per half (default 420)
##   --speed=<f>             the logic speed (default 0.75); --slow=<f> a slow-motion repeat of each half at this speed (default 0: none)
##   --shots=<dir> / --prefix=<name>   saves <prefix>-plain-flight / -plain-impact / -fire-flight / -fire-impact .png
extends Node3D

var _map_name := "map mp fall back 4p"
var _archers := "GondorArcherHorde"
var _victims := "MordorFighterHorde"
var _upgrade := "Upgrade_GondorFireArrows"
var _dist := 260.0
var _frames := 420
var _speed := 0.75
var _slow := 0.0
var _shots := ""
var _prefix := "proj2-arrows"
var _fs: RefCounted
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1
var _centre := Vector2.ZERO
var _offset_y := 420.0 # an open stretch north of the map's centre (no landmark behind the volleys on the default map)


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--archers="):
			_archers = arg.substr(10)
		elif arg.begins_with("--victims="):
			_victims = arg.substr(10)
		elif arg.begins_with("--upgrade="):
			_upgrade = arg.substr(10)
		elif arg.begins_with("--dist="):
			_dist = float(arg.substr(7))
		elif arg.begins_with("--frames="):
			_frames = int(arg.substr(9))
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--slow="):
			_slow = float(arg.substr(7))
		elif arg.begins_with("--offset-y="):
			_offset_y = float(arg.substr(11))
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
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
	_camera.near = 1.0
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
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_label)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
			hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	_centre = (lo + hi) * 0.5
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("ARROWVIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


## the map's client-only trees and props (the static instancer, the one with the most instances) are hidden: nothing stands in front of the volleys
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


func _run() -> void:
	await get_tree().process_frame
	await _volley("plain", _speed, false)
	if _slow > 0.0:
		await _volley("plain-slow", _slow, false)
	await _volley("fire", _speed, true)
	if _slow > 0.0:
		await _volley("fire-slow", _slow, true)
	print("ARROWVIEW DONE")
	get_tree().quit(0)


## one half: a fresh pair of hordes, the order, the volleys followed; `upgraded` grants the player's arrow upgrade before the order
func _volley(tag: String, speed: float, upgraded: bool) -> void:
	_world.set_time_scale(speed)
	if upgraded:
		print("ARROWVIEW grant %s: %s" % [_upgrade, _world.debug_grant_upgrade(_local, _upgrade)])
	var a := Vector2(_centre.x - _dist * 0.5, _centre.y + _offset_y)
	var aid: int = _world.create_object(_archers, _local, a.x, a.y, 0.0)
	var vid: int = _world.create_object(_victims, _enemy, a.x + _dist, a.y, PI)
	if aid <= 0 or vid <= 0:
		_fail("could not create %s / %s" % [_archers, _victims])
		return
	var ground: float = _world.get_ground_height(a.x + _dist * 0.5, a.y)
	# a close side view from the south: the archers on the left, the victims on the right, the arrows' arc in the middle
	var focus := Vector3(a.x + _dist * 0.5, a.y, ground + 22.0)
	var cam := Vector2(focus.x, focus.y - 190.0)
	var cg: float = _world.get_ground_height(cam.x, cam.y)
	_camera.fov = 42.0
	_camera.position = _sage_to_godot(Vector3(cam.x, cam.y, maxf(ground + 28.0, cg + 20.0)))
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)
	for k in 10:
		await get_tree().process_frame
	print("ARROWVIEW %s ORDER %s" % [tag, _world.order_attack([aid], vid)])
	var seen := {}
	var flight_saved := false
	var flight_at := -1
	var impact_saved := false
	var first_landed := -1
	var frames := int(_frames * maxf(1.0, _speed / speed)) # a slow-motion half lasts as many logic frames as a normal one
	for f in frames:
		await get_tree().process_frame
		_label.text = "%s - %s arrows%s  (logic speed %.2f)" % [_archers, "fire / upgraded" if upgraded else "plain", " (slow motion)" if speed < 0.5 else "", speed]
		var flying := 0
		for p in _world.get_projectiles():
			var id: int = p.get("id", 0)
			if not seen.has(id):
				seen[id] = p.get("template", "")
				print("ARROWVIEW %s projectile %s" % [tag, seen[id]])
			flying += 1
		if flight_at < 0 and flying >= 3:
			flight_at = f + int(40.0 * _speed / speed) # about half a flight later: the arrows in the air between the hordes
		if not flight_saved and flight_at >= 0 and f >= flight_at and not _shots.is_empty():
			flight_saved = true
			_save("%s-%s-flight.png" % [_prefix, tag])
		if seen.size() > 0 and flying < seen.size() and first_landed < 0:
			first_landed = f
		if first_landed >= 0 and not impact_saved and f == first_landed + 4 and not _shots.is_empty():
			impact_saved = true
			_save("%s-%s-impact.png" % [_prefix, tag])
	# the archers' drawn tip sprites (FireArowTip / ARROWFIRE: hidden by the OnCreated script, shown by the arrow upgrade's SubObjectsUpgrade)
	var ao: Dictionary = _world.get_object(aid)
	var tips := {}
	for m in ao.get("members", []):
		var mo: Dictionary = _world.get_object(int(m))
		var hid: PackedStringArray = mo.get("hidden_subobjects", PackedStringArray())
		var key := "%s hides %s" % [str(mo.get("shown_models", [])), str(hid)]
		tips[key] = tips.get(key, 0) + 1
	print("ARROWVIEW %s archers: %s" % [tag, tips])
	var kinds := {}
	for id in seen:
		kinds[seen[id]] = kinds.get(seen[id], 0) + 1
	var fx: Dictionary = _world.get_fx_report()
	print("ARROWVIEW %s done: %s fx played %s" % [tag, kinds, JSON.stringify(fx.get("played", {}))])
	_world.destroy_object(aid)
	_world.destroy_object(vid)
	for k in 30:
		await get_tree().process_frame


func _save(name: String) -> void:
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join(name)
	img.save_png(path)
	print("ARROWVIEW SAVED ", path)
