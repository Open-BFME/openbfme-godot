## Emotion viewer (lanes MODULES-2 / MODULES-3): a retail map as a LIVE game for FactionMen against FactionMordor, the in-game HUD on top (as hero_viewer does), and two
## emotion scenes through the logic:
##   1. terror: Aragorn (levelled) blows the horn of Elendil (SpecialAbilityAragornElendil, ModelConditionSpecialAbilityUpdate GenerateTerror) next to two
##      Mordor orc hordes: each victim's EmotionTrackerUpdate runs Terror_Base (EMOTION_TERROR + EMOTION_AFRAID, AIState RUN_AWAY_PANIC: MODULES-3 runs the
##      run-away panic state, RW 0x74E3E7: the hordes take the safe path away from Aragorn, their orders locked), then the terror ends and they stand again;
##   2. fear: a Mordor attack troll (RadiateFearUpdate, always on, 300) walks up to an idle Gondor horde: FearIdle_Base (AIState BACK_AWAY: the horde's
##      members back away from the troll, facing it, RW 0x878905 / 0x873FE8); an order to move ends the back-up and the horde re-forms.
## Saves <prefix>-terror-before / -terror / -terror-flee / -terror-after / -fear-approach / -fear / -fear-cower / -fear-recover screenshots; with Godot's
## --write-movie the run is a video.
##
##   DISPLAY=:1 godot --path godot --write-movie out.avi --fixed-fps 30 res://scenes/emotion_viewer.tscn -- --shots=<dir> [--prefix=mod3] [--map=<name>]
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots_dir := ""
var _prefix := "mod3"
var _speed := 1.0

var _fs: Object
var _builder: Object
var _root: Node3D
var _world: Object
var _hud: Object
var _layer: CanvasLayer
var _label: Label
var _camera: Camera3D
var _local_index := -1
var _enemy_index := -1
var _focus := Vector2.ZERO
var _title := ""
var _status := ""


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
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
	_layer = CanvasLayer.new()
	_layer.layer = 10
	add_child(_layer)
	_hud = ClassDB.instantiate("InGameHudNode")
	_layer.add_child(_hud)
	_label = Label.new()
	_label.position = Vector2(10, 6)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 6)
	_label.add_theme_font_size_override("font_size", 26)
	_layer.add_child(_label)
	_world.set_auto_advance(true)
	_world.set_time_scale(_speed)
	await get_tree().process_frame
	var hs: Dictionary = _hud.setup(_fs, _world, _camera, "Player_1", {"camera_start": Vector3(_focus.x, 0.0, -_focus.y)})
	if not hs.ok:
		_fail("HUD setup failed: %s" % [hs.errors])
		return
	await _terror_scene()
	await _fear_scene()
	print("EMOTION report stops ", (_world.get_report().get("stops", []) as Array).filter(func(s): return String(s).begins_with("[S-102")))
	get_tree().quit(0)


# the members of a horde that show a model condition
func _members_with(horde: int, condition: String) -> Vector2i:
	var h: Dictionary = _world.get_object(horde)
	var n := 0
	var total := 0
	for m in h.get("members", []):
		var mo: Dictionary = _world.get_object(int(m))
		if not mo.get("ok", false):
			continue
		total += 1
		if condition in (mo.get("conditions", []) as Array):
			n += 1
	return Vector2i(n, total)


func _terror_scene() -> void:
	var spot := _focus
	var hero: int = _world.create_object("GondorAragorn", _local_index, spot.x, spot.y, 0.0)
	if hero <= 0:
		_fail("no GondorAragorn")
		return
	var orcs: Array = []
	for i in 2:
		var o: int = _world.create_object("MordorFighterHorde", _enemy_index, spot.x + 70.0, spot.y - 45.0 + 90.0 * i, 3.14)
		if o > 0:
			orcs.append(o)
	_title = "Aragorn: the horn of Elendil (GenerateTerror) next to two Mordor orc hordes"
	_look(Vector2(spot.x + 60.0, spot.y))
	_hud.camera_set_height(460.0) # high enough to keep the run away in view
	await _frames(25)
	_world.gain_hero_levels(hero, 10)
	await _frames(20)
	_status = "before the horn: orcs idle"
	_save("terror-before")
	var power := ""
	for p in _world.get_object_powers(hero):
		if String(p.name).find("Elendil") >= 0:
			power = String(p.name)
	if power.is_empty():
		_fail("Aragorn has no Elendil power: %s" % [JSON.stringify(_world.get_object_powers(hero))])
		return
	_world.ready_object_powers(hero)
	var ok: bool = _world.cast_object_power(_local_index, hero, power, 0)
	print("EMOTION cast %s: %s" % [power, ok])
	var shot := false
	var flee_shot := false
	var peak := 0
	var start := _mean_distance(orcs, hero)
	var farthest := start
	var terror_frames := 0
	for i in 600:
		await _frames(1)
		var t := Vector2i.ZERO
		for o in orcs:
			t += _members_with(o, "EMOTION_TERROR")
		peak = max(peak, t.x)
		var d := _mean_distance(orcs, hero)
		farthest = maxf(farthest, d)
		if t.x > 0:
			terror_frames += 1
		_status = "orc members with EMOTION_TERROR: %d / %d; mean distance from Aragorn %d (was %d): RUN_AWAY_PANIC, the safe path out of vision + RepulsedDistance (MODULES-3)" % [t.x, t.y, int(d), int(start)]
		if t.x > 0 and not shot:
			await _frames(10)
			_save("terror")
			shot = true
		if shot and not flee_shot and d > start + 120.0:
			_save("terror-flee")
			flee_shot = true
		if shot and t.x == 0:
			_status = "terror over: the orcs stand %d from Aragorn (peak %d terrified, ran from %d to %d)" % [int(d), peak, int(start), int(farthest)]
			await _frames(40)
			_save("terror-after")
			break
		if i % 60 == 0:
			print("EMOTION terror frame %d terrified %d / %d distance %d" % [i, t.x, t.y, int(d)])
	print("EMOTION terror ran from %d to %d" % [int(start), int(farthest)])
	print("EMOTION terror peak %d members" % peak)
	for o in orcs:
		for m in _world.get_object(o).get("members", []):
			_world.kill_hero(int(m)) # the members too: a horde's death alone leaves them standing
		_world.kill_hero(o)
	_world.kill_hero(hero)
	await _frames(30)


func _fear_scene() -> void:
	var spot := _focus + Vector2(900.0, 0.0)
	var gondor: int = _world.create_object("GondorFighterHorde", _local_index, spot.x, spot.y, 0.0)
	var troll: int = _world.create_object("MordorAttackTroll", _enemy_index, spot.x - 480.0, spot.y, 0.0)
	if gondor <= 0 or troll <= 0:
		_fail("fear scene objects missing")
		return
	_title = "a Mordor attack troll (RadiateFearUpdate, always on, radius 300) walks up to an idle Gondor horde: FearIdle_Base, BACK_AWAY"
	_look(Vector2(spot.x - 150.0, spot.y))
	_hud.camera_set_height(340.0)
	await _frames(30)
	_world.order_move([troll], spot.x - 120.0, spot.y, {})
	var approach_shot := false
	var afraid_shot := false
	var frames_afraid := 0
	for i in 400:
		await _frames(1)
		var t := _members_with(gondor, "EMOTION_AFRAID")
		if t.x > 0:
			frames_afraid += 1
		var tr: Dictionary = _world.get_object(troll)
		var dist := Vector2(tr.get("x", 0.0), tr.get("y", 0.0)).distance_to(spot)
		_status = "troll %d away; Gondor members with EMOTION_AFRAID: %d / %d; mean member distance from the troll %d (BACK_AWAY: the members back up facing it)" % [int(dist), t.x, t.y, int(_member_distance(gondor, troll))]
		if not approach_shot and dist < 360.0:
			_save("fear-approach")
			approach_shot = true
		if t.x > 0 and not afraid_shot:
			await _frames(2)
			_save("fear")
			afraid_shot = true
		if i % 60 == 0:
			print("EMOTION fear frame %d troll at %d, %d; afraid %d / %d" % [i, int(tr.get("x", 0.0)), int(tr.get("y", 0.0)), t.x, t.y])
		if afraid_shot and i > 300:
			break
	print("EMOTION fear frames with afraid members %d" % frames_afraid)
	var before_order := _member_distance(gondor, troll)
	_status = "the Gondor members stand backed away from the troll (mean member distance %d), facing it, until the horde moves" % int(before_order)
	await _frames(20)
	_save("fear-cower")
	_world.order_move([troll], spot.x - 600.0, spot.y, {})
	_world.order_move([gondor], spot.x + 120.0, spot.y + 60.0, {})
	_status = "an order to move: the back-up records end (RW 0x873155), the horde re-forms and walks"
	await _frames(150)
	_save("fear-recover")
	print("EMOTION fear member distance before the order %d" % int(before_order))
	await _frames(30)


# the mean distance of the hordes' members from an object
func _mean_distance(hordes: Array, from: int) -> float:
	var sum := 0.0
	var n := 0
	for h in hordes:
		sum += _member_distance(h, from)
		n += 1
	return sum / maxf(n, 1)


func _member_distance(horde: int, from: int) -> float:
	var f: Dictionary = _world.get_object(from)
	var at := Vector2(f.get("x", 0.0), f.get("y", 0.0))
	var sum := 0.0
	var n := 0
	for m in _world.get_object(horde).get("members", []):
		var mo: Dictionary = _world.get_object(int(m))
		if mo.get("ok", false):
			sum += Vector2(mo.x, mo.y).distance_to(at)
			n += 1
	return sum / maxf(n, 1)


func _look(at: Vector2) -> void:
	_hud.camera_look_at(at)


# the mean position of the map's objects (hero_viewer's focus)
func _find_focus() -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false):
			sum += Vector2(o.x, o.y)
			n += 1
	return sum / maxf(n, 1)


func _process(_delta: float) -> void:
	if _label != null:
		_label.text = _title + "\n" + _status


func _save(name: String) -> void:
	var shots := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	var path := shots.path_join("%s-%s.png" % [_prefix, name])
	var image := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("EMOTION screenshot %s -> %s" % [path, error_string(err)])


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _fail(message: String) -> void:
	push_error(message)
	print("EMOTION FAIL ", message)
	get_tree().quit(1)
