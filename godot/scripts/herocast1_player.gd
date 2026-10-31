## Lane HEROCAST-1: a power cast with several heroes selected comes from the senior hero alone (AIGroup::groupDoSpecialPowerAtLocation, RW 0x76F774).
## game.gd runs it after the start with --herocast1=<scenario>[,...] (with --auto); every click is an InputEvent pushed into the viewport (the path a
## player's mouse takes). Each scenario prints "HEROCAST1 <scenario>: ok" or "HEROCAST1 <scenario>: FAIL <why>", screenshots go to --screens as
## herocast1-<name>.png, and "HEROCAST1 RESULT: <n> ok, <m> failed" ends the run.
##   gandalfs  (with --faction=FactionMen) two Gandalfs beside the citadel (a test hook creates them and grants 9 and 8 levels, so Lightning Sword is
##             unlocked and their ranks differ), a drag box selects both, the Lightning Sword button and a left click on an enemy group cast it at that
##             spot (Lightning Sword is a CONTEXTMODE_COMMAND power: MSG_DO_SPECIAL_POWER_AT_LOCATION from the whole selection). The result line names
##             who cast: retail and this lane only the senior Gandalf (rank 10). Before the lane the HUD sent the first selected Gandalf as the
##             source, so that one cast, whichever it was. HEROCAST1_EXPECT=senior (default) or first (the old rule) picks the check
extends "res://scripts/scenario_player.gd"

const POWER := "SpecialAbilityLightningSword"

var game: Node
var hud: Node
var world: Node
var _p1 = null # scripts/play1_player.gd: its input helpers (move_to / press / click, palantir_button)


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	_p1 = load("res://scripts/play1_player.gd").new()
	_p1.game = game
	_p1.hud = hud
	_p1.world = world
	_p1.shell = game._shell
	await _run_scenarios(scenarios)


func shot(name: String) -> void:
	game._save_named("herocast1-" + name)


func player_index(name: String) -> int:
	for p in world.get_economy().players:
		if p.name == name:
			return int(p.index)
	return -1


func sword_button() -> Dictionary:
	for b in hud.get_command_buttons():
		if b.name == "Command_GondorGandalfLightningSword":
			return b
	return {}


func recharging(id: int) -> bool:
	for p in world.get_object_powers(id):
		if p.name == POWER:
			return float(p.percent) < 1.0
	return false


func _scenario_gandalfs() -> String:
	if game._local_index < 0:
		return "no local player index"
	var base := Vector2()
	for o in _p1.own_objects():
		if o.structure:
			base = Vector2(o.x, o.y)
			break
	if base == Vector2():
		return "the local player has no structure"
	var at := base + Vector2(240, -160)
	var senior: int = world.create_object("GondorGandalf", game._local_index, at.x, at.y, 0.0)
	var junior: int = world.create_object("GondorGandalf", game._local_index, at.x + 40.0, at.y + 10.0, 0.0)
	if senior <= 0 or junior <= 0:
		return "the world did not create two GondorGandalf"
	world.gain_hero_levels(senior, 9)
	world.gain_hero_levels(junior, 8)
	await game._wait_seconds(1.0)
	world.ready_object_powers(senior)
	world.ready_object_powers(junior)
	# the enemy: a group of soldiers a little ahead
	var enemy := player_index(_p1.enemy_name())
	if enemy < 0:
		return "no enemy player"
	var foe_at := at + Vector2(220, 40)
	var foes: Array = []
	for k in 4:
		var f: int = world.create_object("MordorFighter", enemy, foe_at.x + 14.0 * k, foe_at.y + 8.0 * (k % 2), 3.14)
		if f > 0:
			foes.append(f)
	hud.camera_look_at(at + Vector2(110, 0))
	await game._wait_seconds(1.5)
	# a drag box around both Gandalfs
	var pa: Vector2 = _p1.to_pixel(at.x, at.y)
	var pb: Vector2 = _p1.to_pixel(at.x + 40.0, at.y + 10.0)
	print("HEROCAST1 gandalfs: the Gandalfs at pixels %s and %s" % [pa, pb])
	var lo := Vector2(minf(pa.x, pb.x), minf(pa.y, pb.y)) - Vector2(80, 130)
	var hi := Vector2(maxf(pa.x, pb.x), maxf(pa.y, pb.y)) + Vector2(80, 14)
	await _p1.move_to(lo.floor())
	_p1.press(lo.floor(), MOUSE_BUTTON_LEFT, true)
	await game._step(2)
	for t in 6:
		await _p1.move_to(lo.lerp(hi, float(t + 1) / 6.0).floor())
	_p1.press(hi.floor(), MOUSE_BUTTON_LEFT, false)
	await game._step(8)
	var sel: Array = hud.get_selection()
	if not (sel.has(senior) and sel.has(junior)):
		print("HEROCAST1 gandalfs: the drag box selected %s; selecting both by id (GAME TEST HOOK)" % str(sel))
		hud.select_objects_with_message([senior, junior])
		await game._step(6)
		sel = hud.get_selection()
	if not (sel.has(senior) and sel.has(junior)):
		return "both Gandalfs are not selected (%s)" % str(sel)
	shot("1-selected")
	var b := sword_button()
	if b.is_empty():
		return "no Command_GondorGandalfLightningSword on the Palantir (%s)" % _p1.button_list()
	var px: Vector2 = _p1.palantir_button(b.frame)
	if px.x < 0:
		return "the Lightning Sword's frame %s has no button window" % b.frame
	var n: int = hud.get_message_log().size()
	await _p1.click(px)
	await game._wait_seconds(0.6)
	var target: Vector2 = _p1.to_pixel(foe_at.x + 20.0, foe_at.y + 4.0)
	await _p1.move_to(target)
	await game._step(6)
	shot("2-aiming")
	await _p1.click(target)
	var sent: Array = _p1.messages_since(n)
	print("HEROCAST1 gandalfs: the clicks sent %s" % JSON.stringify(sent))
	var cast_s := false
	var cast_j := false
	for i in 40:
		await game._wait_seconds(0.25)
		cast_s = cast_s or recharging(senior)
		cast_j = cast_j or recharging(junior)
	await game._wait_seconds(1.0)
	shot("3-after")
	print("HEROCAST1 gandalfs: senior (rank 10) cast %s, junior (rank 9) cast %s" % [str(cast_s), str(cast_j)])
	var expect := OS.get_environment("HEROCAST1_EXPECT")
	if expect == "first":
		var first := int(sel[0])
		var ok := (cast_s and not cast_j and first == senior) or (cast_j and not cast_s and first == junior)
		return "" if ok else "expected the first selected Gandalf %d alone to cast (senior %s, junior %s)" % [first, str(cast_s), str(cast_j)]
	if not cast_s or cast_j:
		return "expected the senior Gandalf alone to cast (senior %s, junior %s)" % [str(cast_s), str(cast_j)]
	return ""
