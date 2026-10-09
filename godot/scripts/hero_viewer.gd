## Hero viewer (lane HERO-1): starts a retail map as a LIVE game for one faction (the in-game HUD on top, as hud_viewer does), puts the faction's fortress down and
## runs the hero scenario through the command path: recruit a hero from the fortress by its hero list index (MSG_QUEUE_UNIT_CREATE with the build-index flag),
## let it level, cast its ready abilities (MSG_DO_SPECIAL_POWER), kill it and revive it from the fortress. Saves <prefix>-recruit / -ability / -death /
## -revive screenshots and prints the hero list at every step.
##
##   godot --path godot res://scenes/hero_viewer.tscn -- --faction=FactionMen --fortress=MenFortressCitadel [options]
##
## Options (after `--`):
##   --map=<name>        the map (default "map mp fall back 4p")
##   --faction=<Name>    the local player's faction (default FactionMen); the opponent is FactionMordor (FactionMen when the local player is Mordor)
##   --fortress=<T>      the fortress template to place (it must have REVIVE buttons)
##   --hero=<T>          the hero to recruit (default: the first record that is not CreateAHero)
##   --enemy=<T>         enemy objects placed in front of the hero for its abilities (repeatable)
##   --shots=<dir>       where the screenshots go (default user://screens); --prefix=<name> their name prefix (default hero1)
##   --speed=<f>         the logic speed (default 2.0: the recruit and revive timers are minutes long)
##   --create-a-hero=<Name> lane HERO-2: the local player's slot Create-a-Hero is that system hero or .cah file (load_map's slot create_a_hero; use with --hero=CreateAHero)
##   --abilities=<T,T..> abilities mode (lane HERO-1 part 2): no fortress; each named hero is made next to the --enemy units, reaches its last level and casts
##                       every ability a SpecialAbilityUpdate drives (at the first live enemy when the ability takes a target); one screenshot per ability
extends Node3D

var _map_name := "map mp fall back 4p"
var _faction := "FactionMen"
var _fortress := ""
var _hero_name := ""
var _enemy_args: Array = []
var _shots_dir := ""
var _prefix := "hero1"
var _speed := 2.0
var _ability_heroes: PackedStringArray = []
var _create_a_hero := ""
var _at := Vector2.ZERO

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
var _text := ""
var _cam_shift := Vector2(0, 40)


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--faction="):
			_faction = arg.substr(10)
		elif arg.begins_with("--fortress="):
			_fortress = arg.substr(11)
		elif arg.begins_with("--hero="):
			_hero_name = arg.substr(7)
		elif arg.begins_with("--enemy="):
			_enemy_args.append(arg.substr(8))
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--cam-shift="):
			var c: PackedStringArray = arg.substr(12).split(",")
			_cam_shift = Vector2(float(c[0]), float(c[1]))
		elif arg.begins_with("--at="):
			var a: PackedStringArray = arg.substr(5).split(",")
			_at = Vector2(float(a[0]), float(a[1]))
		elif arg.begins_with("--create-a-hero="):
			_create_a_hero = arg.substr(16)
		elif arg.begins_with("--abilities="):
			_ability_heroes = arg.substr(12).split(",")
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
	var enemy_faction := "FactionMen" if _faction == "FactionMordor" else "FactionMordor"
	var slots := [
		{"player": "Player_1", "faction": _faction, "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": enemy_faction, "human": false, "team": 1, "start_index": 1},
	]
	if not _create_a_hero.is_empty():
		# the slot's Create-a-Hero comes with the game setup (read here, before the game; nothing reads a .cah file during play)
		print("HERO system heroes ", JSON.stringify(_world.get_system_heroes()))
		var rec: Dictionary = _world.get_create_a_hero_record(_create_a_hero)
		if not rec.ok:
			_fail("create-a-hero %s: %s" % [_create_a_hero, rec.errors])
			return
		slots[0]["create_a_hero"] = rec.record
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
	if not _create_a_hero.is_empty():
		var cah: Dictionary = _world.get_create_a_hero(_local_index)
		print("HERO create-a-hero ", JSON.stringify(cah))
		if cah.is_empty() or not cah.can_build:
			_fail("create-a-hero %s: not installed by the game start" % [_create_a_hero])
			return
	if _ability_heroes.size() > 0:
		_run_abilities()
	else:
		_run()


func _run_abilities() -> void:
	var shots := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	var spot := _focus + _at
	for hero_name in _ability_heroes:
		var hero: int = _world.create_object(hero_name, _local_index, spot.x, spot.y, 0.0)
		if hero <= 0:
			_fail("no hero %s" % hero_name)
			return
		_text = hero_name
		_look(spot)
		_hud.camera_set_height(220.0)
		await _frames(20)
		_world.gain_hero_levels(hero, 10)
		await _frames(30)
		# select the hero by a click (the Palantir shows its ability buttons and their recharge clocks)
		var hpos: Dictionary = _world.get_object(hero)
		var px: Vector2 = _hud.world_to_pixel(Vector2(hpos.x, hpos.y))
		_hud.inject_mouse_move(px)
		_hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, px)
		await _frames(2)
		_hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, px)
		await _frames(10)
		var powers: Array = _world.get_object_powers(hero)
		print("HERO powers ", JSON.stringify(powers))
		for p in powers:
			if String(p.update).is_empty() or p.paused or String(p.name) == "SpecialAbilityCaptureBuilding":
				continue
			_world.ready_object_powers(hero)
			var ho0: Dictionary = _world.get_object(hero)
			var enemies: Array = []
			# a mount toggle needs its hero idle (RW 0x8B1690): its enemies stand away
			# lane HERO-2: the disguise as well (RW 0x8B4760 needs the hero idle; a detector next to her cancels it, RW 0x8A65F1)
			var away := 300.0 if String(p.update) == "ToggleMounted" or String(p.name).find("Disguise") >= 0 else 55.0
			for a in _enemy_args:
				var eid: int = _world.create_object(a, _enemy_index, ho0.x + away, ho0.y + 20.0 * enemies.size() - 20.0, 3.14)
				if eid > 0:
					enemies.append(eid)
			# a heal ability (Athelas, Elven Grace) needs a wound to show: 40% of the hero's health
			if String(p.name).find("Athelas") >= 0 or String(p.name).find("Grace") >= 0:
				_world.damage_object(hero, 0.4 * float(_world.get_object(hero).get("max_health", 0.0)))
			await _frames(2)
			var hp0 := _enemy_health(enemies)
			var hero_hp0: float = _world.get_object(hero).get("health", 0.0)
			var st0: Dictionary = _world.get_ability_state(hero)
			var target := 0
			if p.targeted:
				for e in enemies:
					var eo: Dictionary = _world.get_object(e)
					if eo.get("ok", false) and eo.get("health", 0.0) > 0.0:
						target = e
						break
			var ok: bool = _world.cast_object_power(_local_index, hero, p.name, target)
			var title := "%s: %s (%s)" % [hero_name.replace("Gondor", "").replace("Angmar", "").replace("Isengard", "").replace("Elven", "").replace("Dwarven", "").replace("Rohan", ""), String(p.name).replace("SpecialAbility", "").replace("SpecialPower", ""), p.update]
			print("HERO cast %s %s (%s) at %d: %s" % [hero_name, p.name, p.update, target, ok])
			_text = title + "\ncasting..."
			# close camera between the hero and its targets; wait for the trigger, then a little for the effect to land
			var triggered_at := -1
			for i in 360:
				var ho: Dictionary = _world.get_object(hero)
				_look(Vector2(ho.x + 28.0, ho.y))
				_hud.camera_set_height(130.0)
				await _frames(1)
				var st: Dictionary = _world.get_ability_state(hero)
				var pw: Dictionary = st.get("powers", {}).get(p.name, {})
				if triggered_at < 0 and int(pw.get("triggered", 0)) > int(st0.get("powers", {}).get(p.name, {}).get("triggered", 0)):
					triggered_at = i
				if triggered_at >= 0 and i - triggered_at >= 18:
					break
			var measured := _effect_text(p.name, st0, _world.get_ability_state(hero), hp0, _enemy_health(enemies), triggered_at >= 0)
			var hero_hp1: float = _world.get_object(hero).get("health", 0.0)
			if hero_hp1 > hero_hp0 + 0.5:
				var healed := "hero HP %d -> %d (healed)" % [int(hero_hp0), int(hero_hp1)]
				measured = healed if measured == "triggered, no visible change" else healed + ", " + measured
			_text = title + "\nMEASURED: " + measured
			print("HERO effect %s %s: %s" % [hero_name, p.name, measured])
			await _frames(4)
			_save(shots.path_join("%s-%s-%s.png" % [_prefix, hero_name, p.name]))
			await _frames(45)
			for e in enemies:
				_world.kill_hero(e)
			await _frames(20)
		_world.kill_hero(hero)
		await _frames(30)
		spot += Vector2(260, 0)
	print("HERO report errors ", (_world.get_report().get("errors", []) as Array).slice(0, 6))
	get_tree().quit(0)


func _run() -> void:
	var shots := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	var spot := _focus
	print("HERO focus ", _focus)
	var fortress_id: int = _world.create_object(_fortress, _local_index, spot.x, spot.y, 0.0)
	print("HERO fortress %s: object %d" % [_fortress, fortress_id])
	_world.give_money(_local_index, 10000) # the start money of a slot game does not pay for a hero and its revive
	if fortress_id <= 0:
		_fail("no fortress %s" % _fortress)
		return
	_look(spot + Vector2(0, -160))
	_hud.camera_set_height(260.0)
	await _frames(20)
	var heroes: Array = _world.get_heroes(_local_index, fortress_id)
	print("HERO list ", JSON.stringify(heroes))
	var index := -1
	var template := ""
	for h in heroes:
		if (_hero_name.is_empty() and h.template != "CreateAHero") or h.template == _hero_name:
			index = h.index
			template = h.template
			break
	if index < 0:
		_fail("no hero record to recruit")
		return
	_text = "recruiting %s (index %d): $%d, %d frames" % [template, index, heroes[index].cost, heroes[index].frames]
	_world.queue_hero(_local_index, fortress_id, index)
	await _frames(20)
	print("HERO production ", JSON.stringify(_world.get_production(_local_index, fortress_id)))
	print("HERO fortress ", JSON.stringify(_world.get_object(fortress_id)))
	var hero := 0
	for i in 6000:
		await _frames(1)
		hero = _find_live(template, 0)
		if hero > 0:
			break
		if i % 30 == 0:
			var hl: Array = _world.get_heroes(_local_index, fortress_id)
			for h in hl:
				if h.template == template:
					_text = "recruiting %s: %d%%" % [template, int(h.progress * 100.0)]
	if hero <= 0:
		_fail("the hero never appeared")
		return
	var ho: Dictionary = _world.get_object(hero)
	print("HERO recruited ", JSON.stringify(ho))
	_text = "%s recruited from %s" % [template, _fortress]
	await _frames(90)
	ho = _world.get_object(hero)
	_look(Vector2(ho.x, ho.y))
	_hud.camera_set_height(260.0)
	await _frames(10)
	_save(shots.path_join(_prefix + "-recruit.png"))
	# the levels, then the abilities
	_world.gain_hero_levels(hero, 10)
	await _frames(40)
	ho = _world.get_object(hero)
	var enemies: Array = []
	for a in _enemy_args:
		var eid: int = _world.create_object(a, _enemy_index, ho.x + 60.0, ho.y + 30.0 * enemies.size(), 3.14)
		enemies.append(eid)
	var powers: Array = _world.get_object_powers(hero)
	print("HERO powers ", JSON.stringify(powers))
	var cast := []
	for p in powers:
		if p.ready and not p.paused:
			var target: int = enemies[0] if enemies.size() > 0 else 0
			if _world.cast_object_power(_local_index, hero, p.name, 0 if p.attribute_modifier != "" else target):
				cast.append(p.name)
				_text = "%s uses %s" % [template, p.name]
				await _frames(25)
	print("HERO cast ", cast)
	ho = _world.get_object(hero)
	_look(Vector2(ho.x, ho.y))
	await _frames(10)
	_save(shots.path_join(_prefix + "-ability.png"))
	# death
	ho = _world.get_object(hero)
	_look(Vector2(ho.x, ho.y))
	_kill(hero)
	_text = "%s has fallen" % template
	await _frames(40)
	_save(shots.path_join(_prefix + "-death.png"))
	heroes = _world.get_heroes(_local_index, fortress_id)
	print("HERO list after death ", JSON.stringify(heroes))
	var dead := -1
	for h in heroes:
		if h.template == template and h.dead:
			dead = h.index
	if dead < 0:
		_fail("no revive record")
		return
	_world.queue_hero(_local_index, fortress_id, dead)
	_look(spot + Vector2(0, -160))
	_hud.camera_set_height(260.0)
	var back := 0
	for i in 8000:
		await _frames(1)
		back = _find_live(template, hero)
		if back > 0:
			break
		if i % 30 == 0:
			var hl2: Array = _world.get_heroes(_local_index, fortress_id)
			for h in hl2:
				if h.template == template:
					_text = "reviving %s: %d%% ($%d)" % [template, int(h.progress * 100.0), h.cost]
	if back <= 0:
		_fail("the hero was not revived")
		return
	var bo: Dictionary = _world.get_object(back)
	print("HERO revived ", JSON.stringify(bo))
	_text = "%s revived" % template
	await _frames(150)
	bo = _world.get_object(back)
	print("HERO revived later ", JSON.stringify(bo).substr(0, 300))
	_look(Vector2(bo.x, bo.y))
	await _frames(10)
	_save(shots.path_join(_prefix + "-revive.png"))
	await _frames(40)
	print("HERO report errors ", (_world.get_report().get("errors", []) as Array).slice(0, 6))
	get_tree().quit(0)


## the HUD camera shows its look-at point above the screen centre: look a little north of the subject (--cam-shift=dx,dy, SAGE units)
## lane HERO-1 review: the sum of the live enemies' health and the number dead
func _enemy_health(ids: Array) -> Vector2:
	var hp := 0.0
	var dead := 0
	for e in ids:
		var eo: Dictionary = _world.get_object(e)
		var h: float = eo.get("health", 0.0) if eo.get("ok", false) else 0.0
		hp += h
		if h <= 0.0:
			dead += 1
	return Vector2(hp, dead)


## the measured effect of one cast, from the ability's counters, the hero's state and the enemies' health before / after
func _effect_text(power: String, st0: Dictionary, st1: Dictionary, hp0: Vector2, hp1: Vector2, triggered: bool) -> String:
	var parts: Array = []
	if not triggered:
		parts.append("not triggered")
	if hp1.x < hp0.x:
		parts.append("enemy HP %d -> %d" % [int(hp0.x), int(hp1.x)])
	if hp1.y > hp0.y:
		parts.append("%d killed" % int(hp1.y - hp0.y))
	if st1.get("mounted", false) != st0.get("mounted", false):
		parts.append("MOUNTED " + ("on" if st1.get("mounted", false) else "off"))
	if st1.get("disguised", false) != st0.get("disguised", false):
		parts.append("DISGUISED " + ("on" if st1.get("disguised", false) else "off"))
	var m0: Array = st0.get("modifiers", [])
	for m in st1.get("modifiers", []):
		if not m0.has(m):
			parts.append("modifier " + String(m))
	var p0: Dictionary = st0.get("powers", {}).get(power, {})
	var p1: Dictionary = st1.get("powers", {}).get(power, {})
	if int(p1.get("shots", 0)) > int(p0.get("shots", 0)):
		parts.append("%d shot" % (int(p1.get("shots", 0)) - int(p0.get("shots", 0))))
	if int(p1.get("emotions", 0)) > int(p0.get("emotions", 0)):
		parts.append("%d terror requests" % (int(p1.get("emotions", 0)) - int(p0.get("emotions", 0))))
	if parts.is_empty():
		parts.append("triggered, no visible change")
	return ", ".join(parts)


func _look(at: Vector2) -> void:
	_hud.camera_look_at(at + _cam_shift)


func _kill(id: int) -> void:
	# a killing UNRESISTABLE hit (GameWorld.kill_hero): the RespawnBody / RespawnUpdate death, the revive record
	_world.kill_hero(id)


func _find_live(template: String, not_this: int) -> int:
	for id in _world.get_object_ids():
		if id == not_this:
			continue
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false) and o.template == template and o.get("health", 1.0) > 0.0:
			return id
	return 0


func _find_focus() -> Vector2:
	var sum := Vector2.ZERO
	var n := 0
	if n == 0:
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if o.get("ok", false):
				sum += Vector2(o.x, o.y)
				n += 1
	return sum / maxf(n, 1)


func _process(_delta: float) -> void:
	if _label != null:
		_label.text = _text


func _save(path: String) -> void:
	var image := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("HERO screenshot %s -> %s" % [path, error_string(err)])


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _fail(message: String) -> void:
	push_error("HERO " + message)
	print("HERO FAIL ", message)
	get_tree().quit(1)
