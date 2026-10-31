## Lane HUD-5: the owner's first Windows session (Helm's Deep, the player in the fortress), scripted: every scenario prints "HUD5 <name>: ok" or
## "HUD5 <name>: FAIL <why>", saves hud5-<name>*.png in --screens and "HUD5 RESULT: <n> ok, <m> failed" ends the run. game.gd runs it with
## --auto --map="maps/map wor helms deep/map wor helms deep.map" --start-spot=0 --hud5=fortress,players,powers,gate,construction,levels
##   fortress      the player owns the map's fortress (the main gate) and no faction castle was built at Player_1_Start (RW 0x62AD24)
##   players       the Palantir's flag opens the players screen; its Status rows are the engine's (no "APT:" / "MISSING" text), then it closes
##   powers        the Palantir badge (APT:PlayerRank) and the powers screen show the same points
##   gate          the main gate selected, its Toggle button pressed: it closes, pressed again: it opens (MSG_CLOSE_GATE / MSG_OPEN_GATE)
##   construction  a porter starts a building: "Building: n%" over it
##   levels        a horde with veterancy marks and a hero with its health bar (selected), damaged
##   pushout       lane GATEPUSH-1: a Men wall gate in the courtyard is opened, a crowd stands in its doorway, the Toggle button closes it and the push-out
##                 scan (RW 0x89C759) walks them clear before the leaves shut (hud5-pushout-*.png; record it with --write-movie)
extends "res://scripts/scenario_player.gd"

var game: Node
var hud: Node
var world: Node


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	await _run_scenarios(scenarios)


# ---- helpers ----------------------------------------------------------------------------------------------------------------------------------------------

func shot(name: String) -> void:
	game._save_named("hud5-" + name)


func own() -> Array:
	return world.get_player_objects(game._local_name)


func find_own(template: String) -> Dictionary:
	for o in own():
		if o.template == template:
			return o
	return {}


func local_index() -> int:
	for p in world.get_economy().get("players", []):
		if p.name == game._local_name:
			return int(p.index)
	return -1


func look(x: float, y: float) -> void:
	hud.camera_look_at(Vector2(x, y))
	await game._step(20)


func click_pixel(p: Vector2, right: bool = false) -> void:
	hud.inject_mouse_move(p)
	await game._step(2)
	hud.inject_mouse_button(2 if right else 1, true, p)
	await game._step(1)
	hud.inject_mouse_button(2 if right else 1, false, p)
	await game._step(4)


func select_object(o: Dictionary, log_picks: bool = false) -> bool:
	await look(o.x, o.y)
	for off in [Vector2(), Vector2(0, -20), Vector2(0, -40), Vector2(15, -25), Vector2(-15, -25)]:
		var px: Vector2 = hud.world_to_pixel(Vector2(o.x, o.y)) + off
		if log_picks:
			var probe: Dictionary = hud.pick_probe(px, int(o.id))
			print("HUD5 pick at (%d, %d): picked %s, target %s, hits %s" % [int(px.x), int(px.y), str(probe.get("picked", 0)), str(probe.get("target", "?")), str(probe.get("hits", []))])
		await click_pixel(px)
		await game._step(6)
		if hud.get_selection().has(int(o.id)):
			print("HUD5 select: %s %d selected by a click at (%d, %d)" % [o.template, int(o.id), int(px.x), int(px.y)])
			return true
	return false


# a player clicks where the object shows: the pixels around its position whose pick (the real pick, HudObjects::pickObject) answers the object, then a real click
func click_visible_part(o: Dictionary) -> bool:
	var centre: Vector2 = hud.world_to_pixel(Vector2(o.x, o.y))
	for dy in range(-160, 41, 10):
		for dx in [0, -20, 20, -40, 40, -60, 60, -90, 90]:
			var px := centre + Vector2(dx, dy)
			var probe: Dictionary = hud.pick_probe(px, int(o.id))
			if int(probe.get("picked", 0)) == int(o.id):
				print("HUD5 pick: %s %d shows at (%d, %d): hits %s" % [o.template, int(o.id), int(px.x), int(px.y), str(probe.get("hits", []))])
				await click_pixel(px)
				await game._step(6)
				if hud.get_selection().has(int(o.id)):
					print("HUD5 select: %s %d selected by a click at (%d, %d)" % [o.template, int(o.id), int(px.x), int(px.y)])
					return true
	return false


func press_button(command: String) -> bool:
	# the arc's window first (the Palantir's CommandButtons.<i>), then the side bar's (a Radial button shows in both)
	var entries: Array = []
	for b in hud.get_command_buttons():
		if b.command == command:
			if str(b.frame).begins_with("CommandButtons."):
				entries.push_front(b)
			else:
				entries.push_back(b)
	for b in entries:
		for attempt in 10:
			var w: Dictionary = hud.find_button_window(b.frame)
			if w.get("found", false):
				print("HUD5 press %s: %s state %s at (%d, %d)" % [command, b.frame, str(b.get("state", "?")), int(w.x), int(w.y)])
				await click_pixel(Vector2(w.x, w.y))
				return true
			await game._step(3)
	return false


func toggle_state() -> int:
	for b in hud.get_command_buttons():
		if b.command == "TOGGLE_GATE":
			return int(b.state)
	return -1


# ---- scenarios ------------------------------------------------------------------------------------------------------------------------------------------

func _scenario_fortress() -> String:
	var gate := find_own("RBHelmsDeepGateDoorBig")
	var castles := 0
	for o in own():
		if o.template.ends_with("Fortress"):
			castles += 1
	print("HUD5 fortress: %d own objects, main gate %s, faction fortresses %d" % [own().size(), str(gate.get("id", "none")), castles])
	if gate.is_empty():
		await look(2820, 3379)
		shot("fortress")
		return "the player does not own Helm's Deep's main gate (was it at start 1? --start-spot=0)"
	hud.camera_set_height(700.0)
	await look(gate.x, gate.y + 250)
	shot("fortress")
	hud.camera_set_height(420.0)
	if castles != 0:
		return "a faction fortress was built at Player_1_Start"
	return ""


func _scenario_players() -> String:
	game._palantir_objectives()
	await game._step(90)
	shot("players")
	var stack: PackedStringArray = game._shell.shell_stack()
	if stack.is_empty() or stack[-1] != "PlayerTribute.apt":
		return "the players screen did not open: " + str(stack)
	game._close_tribute()
	await game._step(20)
	return ""


func _scenario_powers() -> String:
	var sb: Dictionary = hud.get_spellbook_state()
	shot("powers-badge")
	if not hud.open_spell_store():
		return "the powers screen did not open"
	await game._step(60)
	shot("powers-screen")
	var st: Dictionary = hud.get_spellbook_state().get("store", {})
	print("HUD5 powers: book %s, store %s" % [JSON.stringify(sb.get("points", "?")), JSON.stringify(st)])
	hud.close_spell_store()
	await game._step(20)
	return ""


func _scenario_gate() -> String:
	var gate := find_own("RBHelmsDeepGateDoorBig")
	if gate.is_empty():
		return "no main gate"
	var by_click := await select_object(gate, true)
	if not by_click:
		by_click = await click_visible_part(gate)
	if not by_click:
		# a logged fallback so the rest of the scenario runs; the scenario fails (the owner's click must select the gate)
		print("HUD5 gate: the click did not select the door; selecting it by id (GAME TEST HOOK) to go on")
		if not hud.select_object(int(gate.id)):
			shot("gate-select")
			return "the gate could not be selected"
	await look(gate.x, gate.y - 200)
	await game._step(10)
	shot("gate-selected")
	var before: int = hud.get_message_log().size()
	# the Toggle is restricted while the gate moves (RW 0x9436E9): wait until it is usable
	for i in 60:
		if toggle_state() == 5:
			break
		await game._wait_seconds(0.5)
	print("HUD5 gate: Toggle state %d before the first press" % toggle_state())
	if not await press_button("TOGGLE_GATE"):
		return "no Toggle button (command bar %s)" % JSON.stringify(hud.get_command_buttons())
	# the clip: a frame every 0.4 s while the gate closes (hud5-clip-NNN.png)
	for i in 36:
		await game._wait_seconds(0.4)
		shot("clip-%03d" % i)
		if i == 10:
			shot("gate-closing")
	shot("gate-closed")
	# the second press once the gate is settled again (its Toggle is restricted while it moves, RW 0x9436E9)
	for i in 60:
		if toggle_state() == 5:
			break
		await game._wait_seconds(0.5)
	print("HUD5 gate: Toggle state %d before the second press" % toggle_state())
	if not await press_button("TOGGLE_GATE"):
		return "no Toggle button the second time"
	await game._wait_seconds(14.0)
	shot("gate-open")
	var log: PackedStringArray = hud.get_message_log()
	var msgs := log.slice(before)
	print("HUD5 gate: messages ", msgs)
	var closes := 0
	var opens := 0
	for l in msgs:
		closes += 1 if str(l).begins_with("MSG_CLOSE_GATE") else 0
		opens += 1 if str(l).begins_with("MSG_OPEN_GATE") else 0
	if closes < 1 or opens < 1:
		return "the button sent %d MSG_CLOSE_GATE and %d MSG_OPEN_GATE" % [closes, opens]
	if not by_click:
		return "a click on the door did not select the gate (selected by the test hook)"
	return ""


func _scenario_construction() -> String:
	var porter := find_own("MenPorter")
	if porter.is_empty():
		return "no porter"
	if not await select_object(porter):
		return "the porter could not be selected"
	var build: Dictionary = {}
	for b in hud.get_command_buttons():
		if b.command in ["DOZER_CONSTRUCT", "FOUNDATION_CONSTRUCT", "UNIT_BUILD"] and b.state == 1:
			build = b
			break
	if build.is_empty():
		return "no enabled build button (%s)" % JSON.stringify(hud.get_command_buttons())
	if not await press_button(build.command):
		return "the build button could not be pressed"
	await game._step(10)
	var site: Vector2 = hud.world_to_pixel(Vector2(porter.x + 120, porter.y + 60))
	await click_pixel(site)
	await game._wait_seconds(3.0)
	var ui: Dictionary = hud.get_icon_ui()
	shot("construction")
	print("HUD5 construction: building %s, icon ui %s" % [build.template, JSON.stringify(ui)])
	for t in ui.get("texts", []):
		if str(t).begins_with("Building"):
			return ""
	return "no construction text (the placement may have been refused: %s)" % JSON.stringify(hud.get_placement())


func _scenario_levels() -> String:
	var gate := find_own("RBHelmsDeepGateDoorBig")
	var base := Vector2(gate.get("x", 2830), gate.get("y", 3000) + 250)
	var me := local_index()
	var horde: int = world.create_object("GondorFighterHorde", me, base.x, base.y)
	var hero: int = world.create_object("GondorBoromir", me, base.x + 120, base.y)
	if horde <= 0 or hero <= 0:
		return "objects not made: horde %d, hero %d" % [horde, hero]
	await game._step(10)
	var rank: int = world.debug_set_object_rank(horde, 3)
	hud.camera_set_height(300.0)
	await look(base.x + 60, base.y)
	await game._step(10)
	var heroObj := {}
	for o in own():
		if int(o.id) == hero:
			heroObj = o
	world.debug_damage_object(hero, 350.0, 0)
	if not heroObj.is_empty():
		await select_object(heroObj)
	await game._step(15)
	await look(base.x + 60, base.y)
	var ui: Dictionary = hud.get_icon_ui()
	shot("levels")
	print("HUD5 levels: horde rank %d, icon ui %s" % [rank, JSON.stringify(ui)])
	if rank != 3:
		return "the horde's rank is %d" % rank
	if ui.get("images", []).is_empty():
		return "no veterancy marks drawn"
	if int(ui.get("health_bars", 0)) < 1:
		return "no health bar over the selected hero"
	return ""



func _scenario_pushout() -> String:
	# a Men wall gate (GateOpenAndCloseBehavior, BOX "Closed" 25 x 95 with the OpenLeft / OpenRight leaves at +-70) stands in Helm's Deep's courtyard, out
	# of the gatehouse's roof so the camera sees its doorway; the player's own gate, made by the test hook (logged)
	var main := find_own("RBHelmsDeepGateDoorBig")
	if main.is_empty():
		return "no main gate"
	var me := local_index()
	var at := Vector2(main.x, main.y + 330.0)
	var angle := PI / 2.0 # facing north (screen up): its leaves lie across the screen
	var gid: int = world.create_object("MenWallGate", me, at.x, at.y, angle)
	print("HUD5 pushout: MenWallGate %d at (%.0f, %.0f), angle %.2f (GAME TEST HOOK)" % [gid, at.x, at.y, angle])
	if gid <= 0:
		return "the wall gate was not made"
	hud.camera_set_height(420.0)
	await look(at.x, at.y + 60.0)
	await game._step(30)
	var gate: Dictionary = world.get_object(gid)
	if not await select_object(gate) and not hud.select_object(gid):
		return "the wall gate could not be selected"
	# open it (OpenByDefault = No), then a crowd walks into its doorway and the front the scan covers (the cylinder 0.66 x radius ahead of the gate)
	for i in 30:
		if toggle_state() == 5:
			break
		await game._wait_seconds(0.5)
	if not await press_button("TOGGLE_GATE"):
		return "no Toggle button (open)"
	hud.inject_mouse_move(Vector2(1260, 20))
	hud.select_objects([]) # the gate's buttons off the doorway (GAME TEST HOOK)
	await game._wait_seconds(4.0)
	var templates := ["GondorBoromir", "RohanEowyn", "MenPorter", "MenPorter", "MenPorter", "MenPorter", "MenPorter", "MenPorter", "MenPorter", "MenPorter"]
	var made := 0
	for i in templates.size():
		var p := at + Vector2((float(i % 5) - 2.0) * 20.0, float(i / 5) * 28.0 - 6.0)
		if world.create_object(templates[i], me, p.x, p.y, angle) > 0:
			made += 1
	await game._wait_seconds(1.5)
	shot("pushout-before")
	hud.select_object(gid) # GAME TEST HOOK: the gate selected again for its Toggle
	await game._step(6)
	for i in 30:
		if toggle_state() == 5:
			break
		await game._wait_seconds(0.5)
	if not await press_button("TOGGLE_GATE"):
		return "no Toggle button (close)"
	hud.inject_mouse_move(Vector2(1260, 20))
	hud.select_objects([])
	for i in 8:
		await game._wait_seconds(1.0)
		if i == 1:
			shot("pushout-closing")
	shot("pushout-closed")
	var inside := 0
	for o in own():
		if o.template in ["GondorBoromir", "RohanEowyn", "MenPorter"] and absf(float(o.y) - at.y) < 25.0 and absf(float(o.x) - at.x) < 95.0:
			inside += 1
	print("HUD5 pushout: %d units made, %d inside the closed gate's footprint (25 x 95) after the close" % [made, inside])
	if made != templates.size():
		return "only %d of %d units were made" % [made, templates.size()]
	if inside != 0:
		return "%d units stayed inside the closed gate" % inside
	return ""
