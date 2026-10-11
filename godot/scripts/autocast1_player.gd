## Lane AUTOCAST-1: the owner's ask (2026-10-10): right-clicking a hero's ability button toggles its autocast; the button glows and the hero then uses
## the ability by itself. game.gd runs it after the start with --autocast1=<scenario>[,...] (with --auto); every click is an InputEvent pushed into the
## viewport (the path a player's mouse takes). Each scenario prints "AUTOCAST1 <scenario>: ok" or "AUTOCAST1 <scenario>: FAIL <why>", screenshots go to
## --screens as autocast1-<name>.png (and a crop of the Palantir arc as autocast1-<name>-crop.png), and "AUTOCAST1 RESULT: <n> ok, <m> failed" ends the run.
##   witchking  (with --faction=FactionAngmar) the Witch-king beside the citadel (a test hook creates him and grants his levels, so the Morgul Blade is
##              unlocked), a left click selects him; the Morgul Blade button before; a right click on it: MSG_DO_AUTO_ABILITY, the button glows; an enemy
##              hero walks up and the Witch-king casts the blade by himself (the auto cast count grows, the enemy's health drops); a second right
##              click: off, the glow goes
extends RefCounted

var game: Node
var hud: Node
var world: Node
var ok_count := 0
var fail_count := 0
var _p1 = null # scripts/play1_player.gd: its input helpers (move_to / press / click, select_object, palantir_button)


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	_p1 = load("res://scripts/play1_player.gd").new()
	_p1.game = game
	_p1.hud = hud
	_p1.world = world
	_p1.shell = game._shell
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("AUTOCAST1 %s: ok" % s)
		else:
			fail_count += 1
			print("AUTOCAST1 %s: FAIL %s" % [s, r])
	print("AUTOCAST1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


func shot(name: String, around: Vector2 = Vector2(-1, -1)) -> void:
	game._save_named("autocast1-" + name)
	if around.x < 0 or game._screens.is_empty():
		return
	# a crop around the button (the glow is a ring of a few pixels on a 1280 x 720 window)
	var img: Image = game.get_viewport().get_texture().get_image()
	var r := Rect2i(Vector2i(around) - Vector2i(110, 90), Vector2i(220, 180)).intersection(Rect2i(Vector2i(), img.get_size()))
	var crop := img.get_region(r)
	crop.resize(r.size.x * 2, r.size.y * 2, Image.INTERPOLATE_NEAREST)
	var path := "%s/autocast1-%s-crop.png" % [game._screens, name]
	print("GAME screenshot %s -> %s" % [path, error_string(crop.save_png(path))])


func blade_button() -> Dictionary:
	for b in hud.get_command_buttons():
		if b.name == "Command_WitchkingMorgulBlade":
			return b
	return {}


func player_index(name: String) -> int:
	for p in world.get_economy().players:
		if p.name == name:
			return int(p.index)
	return -1


func _scenario_witchking() -> String:
	if game._local_index < 0:
		return "no local player index"
	var base := Vector2()
	for o in _p1.own_objects():
		if o.structure:
			base = Vector2(o.x, o.y)
			break
	if base == Vector2():
		return "the local player has no structure"
	var at := base + Vector2(220, -160)
	var wk: int = world.create_object("AngmarWitchking", game._local_index, at.x, at.y, 0.0)
	if wk <= 0:
		return "the world did not create AngmarWitchking"
	world.gain_hero_levels(wk, 9) # the level-up grants Upgrade_WitchkingMorgulBlade (UnpauseSpecialPowerUpgrade)
	await game._wait_seconds(1.0)
	world.ready_object_powers(wk)
	hud.camera_look_at(at)
	await game._wait_seconds(1.0)
	if not await _p1.select_object(world.get_object(wk)):
		return "the Witch-king could not be selected by a click"
	await game._step(10)
	var b := blade_button()
	if b.is_empty():
		return "no Command_WitchkingMorgulBlade on the Palantir (%s)" % _p1.button_list()
	var px: Vector2 = _p1.palantir_button(b.frame)
	if px.x < 0:
		return "the Morgul Blade's frame %s has no button window" % b.frame
	print("AUTOCAST1 witchking: Morgul Blade at %s (%s), state %d, auto %s" % [px, b.frame, int(b.state), str(b.auto_ability)])
	await _p1.move_to(px + Vector2(0, 90)) # the pointer off the button (no hover highlight in the picture)
	await game._step(6)
	shot("1-before", px)
	# the right click
	var toggles: int = int(hud.get_state().get("auto_ability_toggles", 0))
	var n: int = hud.get_message_log().size()
	await _p1.click(px, MOUSE_BUTTON_RIGHT)
	await game._wait_seconds(1.0)
	var sent: Array = _p1.messages_since(n)
	print("AUTOCAST1 witchking: the right click sent %s" % JSON.stringify(sent))
	b = blade_button()
	if not b.get("auto_ability", false):
		return "the right click did not turn the autocast on (sent %s, toggles %d -> %d)" % [JSON.stringify(sent), toggles, int(hud.get_state().get("auto_ability_toggles", 0))]
	if not b.get("glow", false):
		return "the autocast is on but the button does not glow"
	if int(hud.get_state().get("auto_ability_overlay_errors", -1)) != 0:
		return "the movie refused SetAutoAbilityOverlayState (%d errors)" % int(hud.get_state().get("auto_ability_overlay_errors", -1))
	await _p1.move_to(px + Vector2(0, 90))
	await game._step(6)
	shot("2-glow", px)
	# an enemy hero walks up: the Witch-king casts the blade by himself
	var enemy := player_index(_p1.enemy_name())
	var casts: int = int(hud.get_state().get("auto_ability_casts", 0))
	var foe_at := at + Vector2(90, 0)
	if enemy < 0:
		return "no enemy player"
	var foe: int = world.create_object("GondorBoromir", enemy, foe_at.x, foe_at.y, 3.14)
	if foe <= 0:
		foe = world.create_object("RohanEowyn", enemy, foe_at.x, foe_at.y, 3.14)
	if foe <= 0:
		return "no enemy hero could be created"
	var hp0: float = float(world.get_object(foe).get("health", 0.0))
	var cast_at := -1.0
	var t0 := Time.get_ticks_msec()
	for i in 120:
		await game._wait_seconds(0.25)
		if int(blade_button().get("auto_casts", 0)) > 0:
			cast_at = (Time.get_ticks_msec() - t0) / 1000.0
			break
	if cast_at < 0:
		return "the Witch-king did not cast the Morgul Blade by himself in 30 s (casts in the game %d -> %d)" % [casts, int(hud.get_state().get("auto_ability_casts", 0))]
	# the cast is the Witch-king's blade module's, at the enemy hero, and the blade's own power recharges
	b = blade_button()
	if str(b.get("auto_cast_power", "")) != "SpecialAbilityWitchkingMorgulBlade" or int(b.get("auto_cast_target", 0)) != foe:
		return "the auto cast was not the Morgul Blade at the enemy hero (%s, target %d, foe %d)" % [str(b.get("auto_cast_power", "")), int(b.get("auto_cast_target", 0)), foe]
	# the blade's own power recharges once the ability fires (WeaponFireSpecialAbilityUpdate: UnpackTime 250 ms, then the special weapon)
	var recharging := false
	for i in 12:
		for p in world.get_object_powers(wk):
			if p.name == "SpecialAbilityWitchkingMorgulBlade":
				recharging = float(p.percent) < 1.0
		if recharging:
			break
		await game._wait_seconds(0.25)
	if not recharging:
		return "the Morgul Blade's power is not recharging after the auto cast"
	print("AUTOCAST1 witchking: auto cast %.1f s after the enemy hero came: %s at %d, the blade recharging" % [cast_at, str(b.auto_cast_power), foe])
	for k in 4:
		await game._wait_seconds(0.4)
		shot("3-autocasting-%d" % k)
	await game._wait_seconds(2.0)
	var hp1: float = float(world.get_object(foe).get("health", 0.0))
	print("AUTOCAST1 witchking: the enemy hero's health %.0f -> %.0f" % [hp0, hp1])
	if not (hp1 < hp0):
		return "the enemy hero took no damage (%.0f -> %.0f)" % [hp0, hp1]
	shot("4-after-cast")
	# off again: a second right click
	n = hud.get_message_log().size()
	await _p1.click(px, MOUSE_BUTTON_RIGHT)
	await game._wait_seconds(1.0)
	b = blade_button()
	if b.get("auto_ability", true) or b.get("glow", true):
		return "the second right click did not turn the autocast off (%s)" % JSON.stringify(b)
	await _p1.move_to(px + Vector2(0, 90))
	await game._step(6)
	shot("5-off", px)
	return ""
