## Stealth abilities viewer (lane STEALTH-2): the elven cloak toggled on a Mirkwood archer horde, Thranduil's Move Unseen, the One Ring worn (viewer-only ring
## times: no RotWK 2.01 object can wear it), the Create-a-Hero Corrupted Man's disguise as a Mordor fighter, and a hero archer volley (Legolas, Lurtz). For the
## lane's screenshots and video; the abilities go through the command path, the scenario set-up through viewer helpers.
##
##   godot --path godot res://scenes/stealth2_viewer.tscn -- [options]
##
## Options (after `--`):
##   --map=<name>      the map (default "map mp fall back 4p")
##   --shots=<dir>     saves <prefix>-<scene>.png there
##   --prefix=<name>   the screenshot prefix (default stealth2)
extends Node3D

var _map_name := "map mp fall back 4p"
var _shots := ""
var _prefix := "stealth2"
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1
var _centre := Vector2.ZERO


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
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
		{"player": "Player_1", "faction": "FactionElves", "human": true, "team": 0, "start_index": 0},
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
	_world.set_time_scale(1.0)
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
		hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	_centre = (lo + hi) * 0.5
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
	print("STEALTH2VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


## the camera looking at ground point `at` from `back` units south and `height` up
func _aim(at: Vector2, back: float, height: float) -> void:
	var g: float = _world.get_ground_height(at.x, at.y)
	_camera.position = _sage_to_godot(Vector3(at.x, at.y - back, g + height))
	_camera.look_at(_sage_to_godot(Vector3(at.x, at.y, g + 5.0)), Vector3.UP)


func _wait(frames: int, text: String) -> void:
	for k in frames:
		await get_tree().process_frame
		_label.text = "%s  (frame %d)" % [text, _world.get_frame()]


## waits until `cond` holds (or `limit` render frames), showing `text`
func _until(limit: int, text: String, cond: Callable) -> bool:
	for k in limit:
		await get_tree().process_frame
		_label.text = "%s  (frame %d)" % [text, _world.get_frame()]
		if cond.call():
			return true
	return false


func _save(name: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("STEALTH2VIEW SAVED ", path)


func _run() -> void:
	await get_tree().process_frame
	await _cloak(_centre + Vector2(-900, 0))
	await _move_unseen(_centre + Vector2(-300, 0))
	await _ring()
	await _disguise(_centre + Vector2(300, 0))
	await _volley(_centre + Vector2(900, 0))
	print("STEALTH2VIEW report %s" % JSON.stringify(_world.get_report()).substr(0, 300))
	print("STEALTH2VIEW DONE")
	get_tree().quit(0)


# 1. the elven cloak (ToggleHiddenSpecialAbilityUpdate): the horde hides (STEALTH), its owner sees it translucent, the enemy does not see it; moving unhides it
func _cloak(at: Vector2) -> void:
	_world.set_local_player(_local)
	var elves: int = _world.create_object("ElvenMirkwoodArcherHorde", _local, at.x, at.y, 0.0)
	_aim(at, 260.0, 120.0)
	await _wait(60, "A Mirkwood archer horde (Elven Cloak in its command set)")
	print("STEALTH2VIEW cloak ", _world.cast_object_power(_local, elves, "SpecialAbilityElfCloak", 0))
	await _until(120, "Elven Cloak toggled: hidden, its countdown paused", func(): return _world.get_invisibility(elves, _enemy).get("type", 2) == 0)
	await _wait(30, "Elven Cloak toggled: the owner sees the horde translucent (STEALTH)")
	print("STEALTH2VIEW cloak state %s %s" % [JSON.stringify(_world.get_stealth_state(elves)), JSON.stringify(_world.get_invisibility(elves, _enemy))])
	await _until(120, "Elven Cloak toggled: the owner sees the horde translucent (STEALTH)", func(): return _world.get_invisibility(elves, _enemy).get("draw_opacity", 1.0) < 0.6)
	_save("cloak-toggled")
	_world.set_local_player(_enemy)
	await _wait(60, "The enemy's view: the cloaked horde is not drawn")
	_save("cloak-enemy-view")
	_world.set_local_player(_local)
	_world.order_move([elves], at.x - 150.0, at.y)
	await _until(90, "Moving breaks the cloak (UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH)", func(): return not _world.get_stealth_state(elves).get("hidden", true))
	await _wait(40, "Moving breaks the cloak (UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH): visible again")
	print("STEALTH2VIEW cloak after the move %s" % JSON.stringify(_world.get_stealth_state(elves)))


# 2. Thranduil's Move Unseen (InvisibilitySpecialPower): the allied hordes within 50 of the target point are camouflaged for 30 s (not Thranduil, not enemies)
func _move_unseen(at: Vector2) -> void:
	_world.set_local_player(_local)
	var thranduil: int = _world.create_object("ElvenThranduil", _local, at.x - 90.0, at.y, 0.0)
	var friends: int = _world.create_object("ElvenLorienWarriorHorde", _local, at.x, at.y, 0.0)
	_aim(at, 260.0, 120.0)
	await _wait(60, "Thranduil and a Lorien warrior horde")
	_world.debug_stealth_scenario(thranduil, "unpause")
	var f: Dictionary = _world.get_object(friends)
	print("STEALTH2VIEW move unseen ", _world.cast_object_power_at(_local, thranduil, "SpecialAbilityMoveUnseen", f.x, f.y))
	await _until(60, "Move Unseen cast at the horde", func(): return _world.get_invisibility(friends, _enemy).get("type", 2) == 1)
	await _until(150, "Move Unseen: the allies within 50 camouflaged for 30 s (seen translucent by their owner; Thranduil is not)", func(): return _world.get_invisibility(friends, _enemy).get("draw_opacity", 1.0) < 0.6)
	print("STEALTH2VIEW move unseen %s / thranduil %s" % [JSON.stringify(_world.get_invisibility(friends, _enemy)), JSON.stringify(_world.get_invisibility(thranduil, _enemy))])
	_save("move-unseen")
	_world.set_local_player(_enemy)
	await _wait(60, "Move Unseen, the enemy's view: the horde is not drawn, Thranduil is")
	_save("move-unseen-enemy-view")
	_world.set_local_player(_local)


# 3. the One Ring (StealthUpdate ring mode) on a Noldor horde among trees: viewer-only ring times (RotWK 2.01 data never reaches the ring)
func _ring() -> void:
	_world.set_local_player(_local)
	var trees: PackedVector3Array = _world.get_terrain_trees(-1)
	if trees.is_empty():
		print("STEALTH2VIEW no terrain tree: the ring scene is skipped")
		return
	var best := trees[0]
	for t in trees:
		if Vector2(t.x, t.y).distance_squared_to(_centre) < Vector2(best.x, best.y).distance_squared_to(_centre):
			best = t
	var at := Vector2(best.x + 30.0, best.y)
	var noldor: int = _world.create_object("NoldorWarriorHorde", _local, at.x, at.y, 0.0)
	_aim(at, 220.0, 110.0)
	await _wait(60, "A Noldor horde (StealthUpdate) among trees")
	print("STEALTH2VIEW ring times ", _world.debug_stealth_scenario(noldor, "ring_times"))
	print("STEALTH2VIEW ring ", _world.order_one_ring(_local, [noldor]))
	await _until(60, "MSG_ONE_RING: the ring goes on (viewer-only ring times)", func(): return _world.get_stealth_state(noldor).get("ring", false))
	await _wait(60, "The One Ring worn: STEALTHED, ONE_RING (viewer-only ring times: no 2.01 object wears it)")
	print("STEALTH2VIEW ring state %s %s" % [JSON.stringify(_world.get_stealth_state(noldor)), JSON.stringify(_world.get_invisibility(noldor, _enemy))])
	_save("ring-worn")
	_world.order_one_ring(_local, [noldor])
	await _until(90, "The ring taken off (TAKING_OFF_RING, off after OneRingDelayOff)", func(): return not _world.get_stealth_state(noldor).get("ring", true))
	await _wait(30, "The ring is off")


# 4. the Create-a-Hero Corrupted Man's disguise as a Mordor fighter: everyone draws the fighter in Mordor's colour; the enemy does not attack it; a skull totem ends it
func _disguise(at: Vector2) -> void:
	_world.set_local_player(_local)
	var cah: int = _world.create_object("CreateAHero", _local, at.x, at.y, 0.0)
	var orcs: int = _world.create_object("MordorFighterHorde", _enemy, at.x + 260.0, at.y, PI)
	_world.set_stance([orcs], 3) # HoldGround: they stay put while the hero is still itself
	_world.debug_stealth_scenario(cah, "object_upgrade", "Upgrade_CreateAHero_ClassCorruptedMan")
	_world.debug_stealth_scenario(cah, "unpause")
	_aim(at + Vector2(120, 0), 230.0, 90.0)
	await _wait(60, "A Corrupted Man (Create-a-Hero) and a Mordor fighter horde")
	_save("disguise-before")
	var members: Array = _world.get_object(orcs).get("members", [])
	if members.is_empty():
		print("STEALTH2VIEW no fighter to disguise as")
		return
	print("STEALTH2VIEW disguise ", _world.cast_object_power(_local, cah, "SpecialAbilityCreateAHeroDisguise", members[0]))
	await _until(60, "Disguise: the transition (2 s)", func(): return _world.get_stealth_state(cah).get("disguise_shown", false))
	await _wait(60, "Disguised as a Mordor fighter: drawn as one, in Mordor's colour, and the fighters do not attack it")
	print("STEALTH2VIEW disguise state %s %s" % [JSON.stringify(_world.get_stealth_state(cah)), JSON.stringify(_world.get_invisibility(cah, _enemy))])
	_save("disguise")
	_world.set_local_player(_enemy)
	await _wait(45, "Disguised, the enemy's view: one of its own fighters")
	_save("disguise-enemy-view")
	_world.set_local_player(_local)
	_world.create_object("WildSkullTotem", _enemy, at.x + 40.0, at.y + 60.0, PI)
	await _until(90, "A skull totem detects it: the disguise ends", func(): return not _world.get_stealth_state(cah).get("disguise_shown", true))
	await _wait(45, "Revealed: the Corrupted Man again")
	_save("disguise-revealed")


# 5. a hero archer volley: Legolas and Lurtz shoot their retail arrows (GoodFactionArrow / EvilFactionArrow, the EXArrowStreak01 streak)
func _volley(at: Vector2) -> void:
	_world.set_local_player(_local)
	var legolas: int = _world.create_object("ElvenLegolas", _local, at.x, at.y, 0.0)
	var lurtz: int = _world.create_object("IsengardLurtz", _enemy, at.x + 200.0, at.y, PI)
	_aim(at + Vector2(100, 0), 170.0, 95.0)
	await _wait(30, "Legolas and Lurtz")
	_world.order_attack([legolas], lurtz)
	_world.order_attack([lurtz], legolas)
	await _wait(20, "Hero archer volley: Legolas (GoodFactionArrow) and Lurtz (EvilFactionArrow)")
	for k in 12: # a burst: the arrows fly for a few frames only
		await _wait(4, "Hero archer volley: Legolas (GoodFactionArrow) and Lurtz (EvilFactionArrow)")
		_save("hero-volley-%02d" % k)
	await _wait(60, "Hero archer volley: Legolas (GoodFactionArrow) and Lurtz (EvilFactionArrow)")
