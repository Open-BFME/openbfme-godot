## Upgrade viewer / recorded scenario (lane UPGRADE-1): a retail map as a LIVE game (the HUD viewer's start: no menu), our hordes and an enemy horde, then the
## upgrades researched through the game's own command path (GameWorld.queue_upgrade: MSG_QUEUE_UPGRADE -> the horde's ProductionUpdate -> Object::giveUpgrade,
## which hands the upgrade to every member) and screenshots before / after.
##
##   godot --path godot res://scenes/upgrade_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>          the map (default "map mp fall back 4p")
##   --shots=<dir>         where the screenshots go (upgrade1-*.png; default user://screens)
##   --timescale=<f>       the logic speed while the research runs (default 4.0)
##   --showcase            a slower pace made for a recorded video (use with `--write-movie out.avi --fixed-fps 30`)
##   --quit-after-scenario (default) / --stay               quit when the scenario ends, or keep the window open
extends Node3D

const FIGHTERS := "GondorFighterHorde"
const ARCHERS := "GondorArcherHorde"
const ENEMY := "MordorFighterHorde"
const FIGHTER_UPGRADES := ["Upgrade_GondorForgedBlades", "Upgrade_GondorHeavyArmor"]
const ARCHER_UPGRADES := ["Upgrade_GondorFireArrows"]

var _map_name := "map mp fall back 4p"
var _shots_dir := ""
var _time_scale := 4.0
var _showcase := false
var _stay := false

var _fs: RefCounted
var _builder: RefCounted
var _root: Node3D
var _world: Node3D
var _camera: Camera3D
var _hud: Node2D
var _layer: CanvasLayer
var _label: Label
var _focus := Vector2.ZERO
var _cam_target := Vector2.ZERO
var _cam_height := 300.0
var _local_index := -1
var _enemy_index := -1
var _fighters := -1
var _archers := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg.begins_with("--timescale="):
			_time_scale = float(arg.substr(12))
		elif arg == "--showcase":
			_showcase = true
		elif arg == "--stay":
			_stay = true
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
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	_builder = ClassDB.instantiate("MapTerrainBuilder")
	_root = _builder.build_map(_fs, _map_name, {"markers": false, "fog": false})
	if _root == null:
		_fail("build_map failed: %s" % [_builder.get_report().errors])
		return
	add_child(_root)
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
	_root.add_child(_world)
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and _local_index < 0:
			_local_index = idx
		if "Player_2" in str(pl) and _enemy_index < 0:
			_enemy_index = idx
		idx += 1
	_focus = _find_focus()
	_cam_target = _focus
	_layer = CanvasLayer.new()
	_layer.layer = 10
	add_child(_layer)
	_hud = ClassDB.instantiate("InGameHudNode")
	_layer.add_child(_hud)
	_label = Label.new()
	_label.position = Vector2(10, 6)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	_layer.add_child(_label)
	_world.set_auto_advance(true)
	var f_spot := _free_spot(_focus + Vector2(0, -150))
	_fighters = _world.create_object(FIGHTERS, _local_index, f_spot.x, f_spot.y, 1.5708)
	var a_spot := _free_spot(_focus + Vector2(160, -150))
	_archers = _world.create_object(ARCHERS, _local_index, a_spot.x, a_spot.y, 1.5708)
	# the upgrades' RequiredObjectFilter: the forge (blades, armour) and the archery range (fire arrows) must stand (Object::affectedByUpgrade RW 0x694914)
	for b in ["GondorForge", "GondorArcherRange"]:
		var b_spot := _free_spot(_focus + Vector2(-500, 300))
		var bid: int = _world.create_object(b, _local_index, b_spot.x, b_spot.y, 0.0)
		print("UPGRADEVIEW building %s: %d" % [b, bid])
	print("UPGRADEVIEW spawned fighters %d at %s, archers %d at %s" % [_fighters, f_spot, _archers, a_spot])
	await get_tree().process_frame
	var hs: Dictionary = _hud.setup(_fs, _world, _camera, "Player_1", {"camera_start": Vector3(_focus.x, 0.0, -_focus.y)})
	if not hs.ok:
		_fail("HUD setup failed: %s" % [hs.errors])
		return
	_run_scenario()


func _fail(message: String) -> void:
	push_error(message)
	print("UPGRADEVIEW FAIL: ", message)
	get_tree().quit(1)


func _find_focus() -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false) and o.get("owner", "") == "Player_1":
			sum += Vector2(o.x, o.y)
			n += 1
	if n == 0:
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if o.get("ok", false):
				sum += Vector2(o.x, o.y)
				n += 1
	return sum / maxf(n, 1)


func _free_spot(near: Vector2) -> Vector2:
	var pts: Array = []
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			pts.append(Vector2(o.x, o.y))
	for ring in 40:
		var per := 1 if ring == 0 else 8 * ring
		for k in per:
			var a := TAU * k / per
			var p := near + Vector2(cos(a), sin(a)) * 40.0 * ring
			var free := true
			for q in pts:
				if p.distance_to(q) < 90.0:
					free = false
					break
			if free:
				return p
	return near


func _place_camera() -> void:
	_hud.camera_look_at(_cam_target)
	_hud.camera_set_height(clampf(_cam_height, 120.0, 300.0))


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _wait_logic(n: int) -> void:
	var target: int = _world.get_frame() + n
	while _world.get_frame() < target:
		await get_tree().process_frame


func _save(name: String) -> void:
	var dir := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	var path := dir.path_join(name)
	var image := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("UPGRADEVIEW screenshot %s -> %s (%dx%d)" % [path, error_string(err), image.get_width(), image.get_height()])


func _member_upgrades(id: int) -> String:
	var o: Dictionary = _world.get_object(id)
	var ups: Dictionary = _world.get_upgrades(id)
	var members: Array = o.get("members", [])
	var first := ""
	if members.size() > 0:
		first = str(_world.get_upgrades(members[0]).get("object", []))
	return "%s: object %s, %d members, first member %s" % [o.get("template", "?"), ups.get("object", []), members.size(), first]


func _run_scenario() -> void:
	var pace := 2.0 if _showcase else 1.0
	await _frames(int(40 * pace))
	var fo: Dictionary = _world.get_object(_fighters)
	var ao: Dictionary = _world.get_object(_archers)
	if not fo.get("ok", false) or not ao.get("ok", false):
		_fail("the hordes were not created (%s, %s)" % [fo, ao])
		return
	_cam_target = (Vector2(fo.x, fo.y) + Vector2(ao.x, ao.y)) * 0.5
	_cam_height = 150.0
	_place_camera()
	_label.text = "before: no upgrades"
	await _frames(int(60 * pace))
	print("UPGRADEVIEW before: ", _member_upgrades(_fighters), " | ", _member_upgrades(_archers))
	_save("upgrade1-horde-before.png")
	# the research, through MSG_QUEUE_UPGRADE (the HUD's own button sends the same message)
	var money0: Dictionary = _world.get_economy()
	for u in FIGHTER_UPGRADES:
		_world.queue_upgrade(_local_index, _fighters, u)
	for u in ARCHER_UPGRADES:
		_world.queue_upgrade(_local_index, _archers, u)
	_world.set_time_scale(_time_scale)
	_label.text = "researching forged blades, heavy armour, fire arrows"
	await _wait_logic(4)
	print("UPGRADEVIEW queued: fighters ", JSON.stringify(_world.get_production(_local_index, _fighters)), " archers ",
		JSON.stringify(_world.get_production(_local_index, _archers).get("queue", [])), " local index ", _local_index, " owner ", fo.get("owner", "?"))
	await _wait_logic(110)
	_world.set_time_scale(1.0)
	await _frames(int(30 * pace))
	print("UPGRADEVIEW after: ", _member_upgrades(_fighters), " | ", _member_upgrades(_archers))
	print("UPGRADEVIEW economy before ", JSON.stringify(money0), " after ", JSON.stringify(_world.get_economy()))
	_label.text = "after: forged blades + heavy armour (fighters), fire arrows (archers)"
	await _frames(3)
	_save("upgrade1-horde-after.png")
	# fire arrows in flight: an enemy horde in the archers' range; they acquire it on their own
	var e_spot := _free_spot(Vector2(ao.x, ao.y) + Vector2(0, 260))
	_enemy = _world.create_object(ENEMY, _enemy_index, e_spot.x, e_spot.y, -1.5708)
	_cam_target = (Vector2(ao.x, ao.y) + e_spot) * 0.5
	_cam_height = 180.0
	_place_camera()
	_world.set_time_scale(0.5)
	var saved := false
	_label.text = "fire arrows against a Mordor horde"
	for i in int(600 * pace):
		await get_tree().process_frame
		var proj: Array = _world.get_projectiles()
		var flying := 0
		for p in proj:
			if p.in_flight:
				flying += 1
		if not saved and flying >= 4:
			saved = true
			await _frames(3)
			print("UPGRADEVIEW volley: %d projectiles in flight, first %s" % [flying, proj[0].template])
			_save("upgrade1-firearrows.png")
			if not _showcase:
				break
	if not saved:
		print("UPGRADEVIEW no volley seen: combat ", JSON.stringify(_world.get_combat_report()))
	await _frames(int(30 * pace))
	print("UPGRADEVIEW report stops: ")
	for s in _world.get_report().get("stops", []):
		var line := str(s)
		if line.begins_with("[S-48"):
			print("  ", line.substr(0, 220))
	if not _stay:
		get_tree().quit(0)
