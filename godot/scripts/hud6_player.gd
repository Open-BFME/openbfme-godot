## Lane HUD-6: RotWK's in-game UI pieces the owner's retail shots show (owner-shots/hud6), scripted for proof screenshots: every scenario prints
## "HUD6 <name>: ok" or "HUD6 <name>: FAIL <why>", saves hud6-<name>*.png in --screens and "HUD6 RESULT: <n> ok, <m> failed" ends the run. game.gd runs it with
## --auto --faction=FactionMordor --hud6=radial,help,heroes,sidebar
##   radial   the player's fortress selected by a click: its Radial command buttons ring over it as gold-rimmed bubbles (retail-1.png)
##   help     the pointer on a bubble, then on a Palantir arc button: the help box shows the button's name, shortcut, cost and description (retail-5.png)
##   heroes   the fortress's hero bubble pressed (PUSH_VISIBLE_COMMAND_RANGE): the ring of hero portraits (retail-2.png)
##   sidebar  a builder selected: the build list in the side bar's carved frame (retail-3.png)
extends RefCounted

var game: Node
var hud: Node
var world: Node
var ok_count := 0
var fail_count := 0


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("HUD6 %s: ok" % s)
		else:
			fail_count += 1
			print("HUD6 %s: FAIL %s" % [s, r])
	print("HUD6 RESULT: %d ok, %d failed" % [ok_count, fail_count])


# ---- helpers ----------------------------------------------------------------------------------------------------------------------------------------------

func shot(name: String) -> void:
	game._save_named("hud6-" + name)


func own() -> Array:
	return world.get_player_objects(game._local_name)


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


func find_own(pred: Callable) -> Dictionary:
	for o in own():
		if pred.call(o):
			return o
	return {}


# a click where the object shows (the real pick); the id hook only as a logged fallback
func select_object(o: Dictionary) -> bool:
	await look(o.x, o.y)
	var centre: Vector2 = hud.world_to_pixel(Vector2(o.x, o.y))
	for dy in range(-120, 41, 10):
		for dx in [0, -20, 20, -40, 40]:
			var px := centre + Vector2(dx, dy)
			var probe: Dictionary = hud.pick_probe(px, int(o.id))
			if int(probe.get("picked", 0)) == int(o.id):
				await click_pixel(px)
				await game._step(6)
				if hud.get_selection().has(int(o.id)):
					print("HUD6 select: %s %d selected by a click at (%d, %d)" % [o.template, int(o.id), int(px.x), int(px.y)])
					return true
	print("HUD6 select: no click selected %s %d; selecting it by id (GAME TEST HOOK)" % [o.template, int(o.id)])
	return hud.select_object(int(o.id))


func radial() -> Dictionary:
	return hud.get_hud6_state().get("radial", {})


func help() -> Dictionary:
	return hud.get_hud6_state().get("help", {})


func bubble_centre(b: Dictionary) -> Vector2:
	var r: Rect2 = b.rect
	return r.position + r.size * 0.5


func fortress() -> Dictionary:
	return find_own(func(o): return str(o.template).find("Fortress") >= 0 and str(o.template).find("Citadel") >= 0)


func wait_help_shown() -> bool:
	for i in 60:
		if bool(help().get("shown", false)) and int(help().get("draws", 0)) > 0:
			return true
		await game._step(2)
	return false


# ---- scenarios ------------------------------------------------------------------------------------------------------------------------------------------

func _scenario_radial() -> String:
	var f := fortress()
	if f.is_empty():
		var names := []
		for o in own():
			names.append(o.template)
		return "no own fortress citadel among " + str(names)
	if not await select_object(f):
		return "the fortress could not be selected"
	hud.camera_set_height(330.0)
	await look(f.x, f.y)
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(30)
	var r := radial()
	print("HUD6 radial: %s" % JSON.stringify(r))
	shot("radial")
	if int(r.get("count", 0)) < 1:
		return "no bubbles over the fortress"
	if (r.get("errors", []) as Array).size() > 0:
		return "radial errors: " + str(r.errors)
	return ""


func _scenario_help() -> String:
	var r := radial()
	var buttons: Array = r.get("buttons", [])
	if buttons.is_empty():
		return "no bubbles (run radial first)"
	# the bubble the owner's shot hovers: a unit's build button if there is one
	var target: Dictionary = buttons[0]
	for b in buttons:
		if str(b.command) == "UNIT_BUILD":
			target = b
			break
	hud.inject_mouse_move(bubble_centre(target))
	await game._step(4)
	var ok := await wait_help_shown()
	await game._step(20)
	print("HUD6 help (bubble %s): %s" % [str(target.name), JSON.stringify(help())])
	if OS.get_environment("HUD6_DUMP") != "":
		var tree: String = hud.dump_tree(40)
		var keep := false
		for line in tree.split("\n"):
			if line.find("helpBox") >= 0:
				keep = true
			if keep:
				print("HUD6 TREE ", line)
	shot("help-bubble")
	if not ok:
		return "the help box did not show over bubble " + str(target.name)
	# the pointer off the buttons: the box hides
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(40)
	if bool(help().get("shown", false)):
		return "the help box stayed after the pointer left"
	# an arc button of the Palantir
	for b in hud.get_command_buttons():
		if str(b.frame).begins_with("CommandButtons."):
			var w: Dictionary = hud.find_button_window(b.frame)
			if w.get("found", false):
				hud.inject_mouse_move(Vector2(w.x, w.y))
				await game._step(4)
				var arc_ok := await wait_help_shown()
				await game._step(20)
				print("HUD6 help (arc %s): %s" % [str(b.name), JSON.stringify(help())])
				shot("help-arc")
				hud.inject_mouse_move(Vector2(60, 60))
				await game._step(20)
				if not arc_ok:
					return "the help box did not show over arc button " + str(b.name)
				return ""
	return "no arc button found"


func _scenario_heroes() -> String:
	var r := radial()
	for b in r.get("buttons", []):
		if str(b.command) == "PUSH_VISIBLE_COMMAND_RANGE":
			await click_pixel(bubble_centre(b))
			hud.inject_mouse_move(Vector2(60, 60))
			await game._step(30)
			var after := radial()
			print("HUD6 heroes: pressed %s -> %s" % [str(b.name), JSON.stringify(after)])
			shot("heroes")
			if int(after.get("count", 0)) < 2:
				return "the hero ring did not open"
			# back to the first ring (POP_VISIBLE_COMMAND_RANGE)
			for c in after.get("buttons", []):
				if str(c.command) == "POP_VISIBLE_COMMAND_RANGE":
					await click_pixel(bubble_centre(c))
					await game._step(10)
					break
			return ""
	return "the fortress has no PUSH_VISIBLE_COMMAND_RANGE bubble: " + JSON.stringify(r.get("buttons", []))


func _scenario_sidebar() -> String:
	var b := find_own(func(o): return str(o.template).find("Worker") >= 0 or str(o.template).find("Porter") >= 0 or str(o.template).find("Builder") >= 0)
	if b.is_empty():
		return "no own builder"
	if not await select_object(b):
		return "the builder could not be selected"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(60)
	var side: Dictionary = hud.get_hud6_state().get("side_bar", {})
	print("HUD6 sidebar: %s" % JSON.stringify(side))
	shot("sidebar")
	if not bool(side.get("shown", false)) or int(side.get("count", 0)) < 1:
		return "the side bar shows no framed buttons"
	return ""
