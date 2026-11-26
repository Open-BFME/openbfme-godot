## Lane HOVER-1: the in-game hover texts, scripted for proof screenshots: every scenario prints "HOVER1 <name>: ok" or "HOVER1 <name>: FAIL <why>", saves
## hover1-<name>*.png in --screens and "HOVER1 RESULT: <n> ok, <m> failed" ends the run. game.gd runs it with --auto --faction=FactionAngmar
## --hover1=ability,spellbook,herobar
##   ability    a Necromancer horde of the player's (create_object test hook) selected: the pointer on its arc buttons until the help box shows a cost line
##              (RotWK's SPECIAL_POWER "TOOLTIP:UnitCost", RW 0x808675)
##   spellbook  power points given (test hook) and every store power bought through the store's own clicks: the pointer on the spell book's first slot
##              shows the power's help box (RW 0x930DC4)
##   cutscene   a spell book slot's help is shown, then the scripts' DISABLE_INPUT (game._script_input(false)): the box hides and the next frames do not
##              re-compose it while the pointer stays on the slot; ENABLE_INPUT lets a fresh hover show it again (lane HOVER-1 guards vslot 0x1BC / 0x17C)
##   herobar    a hero of the faction (create_object test hook): the pointer on its hero bar portrait shows the unit help (name, level, health, damage,
##              description: UnitHelpSource RW 0x8EBC61)
##   portrait   an own hero selected: the pointer on the Palantir's portrait shows its unit help (RW 0x93099D)
##   quitmenu   Esc opens the quit menu; the pointer on Resume shows its tool tip (GuiFX.apt's ToolTip, the shell's window manager)
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
	game._save_named("hover1-" + name)


func help() -> Dictionary:
	return hud.get_hud6_state().get("help", {})


func away() -> void:
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(20)


# the pointer at `p` until the help box shows and has been drawn; the help's dictionary, or {} when it never showed
func hover(p: Vector2) -> Dictionary:
	await away()
	hud.inject_mouse_move(p)
	for i in 60:
		await game._step(2)
		var h := help()
		if bool(h.get("shown", false)) and int(h.get("draws", 0)) > 0:
			await game._step(10)
			return help()
	return {}


func base_position() -> Vector2:
	for o in world.get_player_objects(game._local_name):
		if str(o.template).find("Fortress") >= 0:
			return Vector2(o.x, o.y)
	var objs: Array = world.get_player_objects(game._local_name)
	return Vector2(objs[0].x, objs[0].y) if not objs.is_empty() else Vector2.ZERO


func make(template: String, offset: Vector2) -> int:
	var at := base_position() + offset
	var id: int = world.create_object(template, game._local_index, at.x, at.y, 0.0)
	print("HOVER1 TEST HOOK: create_object %s at %s -> %d" % [template, str(at), id])
	await game._step(10)
	return id


# ---- scenarios ------------------------------------------------------------------------------------------------------------------------------------------

func _scenario_ability() -> String:
	var id := await make("AngmarNecromancerHorde", Vector2(350, 0))
	if id < 0:
		return "create_object AngmarNecromancerHorde failed"
	hud.select_object(id)
	hud.camera_look_at(base_position() + Vector2(350, 0))
	await game._step(30)
	for b in hud.get_command_buttons():
		if str(b.frame).begins_with("CommandButtons."):
			var w: Dictionary = hud.find_button_window(b.frame)
			if not w.get("found", false):
				continue
			var h := await hover(Vector2(w.x, w.y))
			print("HOVER1 ability (%s): %s" % [str(b.name), JSON.stringify(h)])
			if str(h.get("title", "")) != "":
				shot("ability-unit-cost")
				await away()
				return ""
	shot("ability-unit-cost") # what the last arc button shows (a build without the cost line: the "before" shot)
	return "no arc button of the Necromancer showed a cost line"


func _scenario_spellbook() -> String:
	var me: int = game._local_index
	var pts: int = world.debug_add_science_points(me, 200)
	print("HOVER1 TEST HOOK: player %d given 200 power points -> %d" % [me, pts])
	if not hud.open_spell_store():
		return "the spell store did not open"
	await game._step(60)
	for pass_index in 5:
		for i in 20:
			hud.spell_store_click(i)
		await game._step(4)
	hud.close_spell_store()
	await game._step(90)
	var sb: Dictionary = hud.get_spellbook_state()
	print("HOVER1 spellbook state: %s" % JSON.stringify(sb))
	var path: String = str(sb.get("path", ""))
	for slot in range(1, 25):
		var w: Dictionary = hud.find_button_window("%s.Spell%d" % [path, slot])
		if not w.get("found", false):
			continue
		var h := await hover(Vector2(w.x, w.y))
		print("HOVER1 spellbook slot %d: %s" % [slot, JSON.stringify(h)])
		if int(h.get("bar", -1)) == 3 and str(h.get("name", "")) != "":
			shot("spellbook-power")
			await away()
			return ""
	var first: Dictionary = hud.find_button_window("%s.Spell1" % path)
	if first.get("found", false):
		await hover(Vector2(first.x, first.y))
		shot("spellbook-power") # the "before" shot: nothing over the slot
	return "no spell book slot showed a help box"


func _scenario_cutscene() -> String:
	# the spell book is a provider that does not depend on the selection: its slot stays under the pointer while DISABLE_INPUT clears the selection.
	# This is the button/Apt branch the review found re-composing the help after the input went off.
	var me: int = game._local_index
	world.debug_add_science_points(me, 200)
	if not hud.open_spell_store():
		return "the spell store did not open"
	await game._step(60)
	for pass_index in 5:
		for i in 20:
			hud.spell_store_click(i)
		await game._step(4)
	hud.close_spell_store()
	await game._step(90)
	var sb: Dictionary = hud.get_spellbook_state()
	var path: String = str(sb.get("path", ""))
	var target := {}
	for slot in range(1, 25):
		var w: Dictionary = hud.find_button_window("%s.Spell%d" % [path, slot])
		if w.get("found", false):
			target = w
			break
	if target.is_empty():
		return "no spell book slot window"
	var p := Vector2(target.x, target.y)
	var h := await hover(p)
	print("HOVER1 cutscene before: %s" % JSON.stringify(h))
	if not bool(h.get("shown", false)) or int(h.get("draws", 0)) == 0:
		return "the spell book slot showed no help before DISABLE_INPUT"
	shot("cutscene-before")
	game._script_input(false) # the same call a campaign cutscene makes (RW 0x7BC586)
	hud.inject_mouse_move(p) # the pointer does not move off the slot
	await game._step(30)
	var during := help()
	print("HOVER1 cutscene off: %s" % JSON.stringify(during))
	shot("cutscene-off")
	if bool(during.get("shown", false)) or not str(during.get("name", "")).is_empty() or not str(during.get("text", "")).is_empty():
		return "the help box re-composed with the input off (RW 0x807861 / 0x807978 guards missing)"
	game._script_input(true)
	await game._step(10)
	var after := await hover(p)
	print("HOVER1 cutscene after: %s" % JSON.stringify(after))
	shot("cutscene-after")
	if not bool(after.get("shown", false)) or int(after.get("draws", 0)) == 0:
		return "the help box did not show after ENABLE_INPUT"
	await away()
	return ""


func _scenario_herobar() -> String:
	var hero := ""
	match str(game._faction):
		"FactionAngmar":
			hero = "AngmarRogash"
		"FactionMen":
			hero = "GondorAragornMP"
		_:
			hero = ""
	if hero == "":
		return "no hero for faction " + str(game._faction)
	var id := await make(hero, Vector2(-300, 120))
	if id < 0:
		return "create_object %s failed" % hero
	await game._step(60)
	var sel: Dictionary = hud.get_hero_select_state() if hud.has_method("get_hero_select_state") else {}
	for n in range(1, 17):
		var w: Dictionary = hud.find_button_window("HeroSelectUI.Hero%d" % n)
		if not w.get("found", false):
			continue
		var h := await hover(Vector2(w.x, w.y))
		print("HOVER1 herobar Hero%d: %s" % [n, JSON.stringify(h)])
		if bool(h.get("simple", false)):
			shot("herobar-portrait")
			await away()
			return ""
	var portrait: Dictionary = hud.find_button_window("HeroSelectUI.Hero2")
	if portrait.get("found", false):
		await hover(Vector2(portrait.x, portrait.y))
		shot("herobar-portrait") # the "before" shot
	return "no hero bar portrait showed the unit help " + JSON.stringify(sel)


## quitmenu: Esc opens QuitMenu.apt over the game; the pointer rests on Resume: its tool tip (TOOLTIP:QuitMenu/Resume/TheButton) shows after the delay
func _scenario_quitmenu() -> String:
	await game._press_escape()
	await game._wait_seconds(3.0)
	var b: Dictionary = game._quit_button("Resume")
	if b.is_empty():
		return "the quit menu's Resume button was not found"
	# the system pointer too (the display's own cursor sends motion events of its own): where the scripted pointer is
	game.get_viewport().warp_mouse(game._shell.stage_to_window(Vector2(b.x, b.y)))
	game._mouse_event(Vector2(b.x, b.y), false, false)
	await game._wait_seconds(2.0)
	if game._shell.has_method("get_cursor_tooltip"):
		print("HOVER1 quitmenu tool tip: %s" % JSON.stringify(game._shell.get_cursor_tooltip()))
	shot("quitmenu-resume")
	game._mouse_event(Vector2(4, 4), false, false)
	await game._step(10)
	await game._click_quit("Resume")
	return ""


## portrait: an own hero selected; the pointer on the Palantir's portrait shows its unit help (RW 0x93099D)
func _scenario_portrait() -> String:
	var id := await make("AngmarRogash", Vector2(-200, -150))
	if id < 0:
		return "create_object AngmarRogash failed"
	hud.select_object(id)
	await game._step(30)
	var w: Dictionary = hud.find_button_window("CommandUI.PortraitBackground")
	if not w.get("found", false):
		return "no portrait button"
	var h := await hover(Vector2(w.x, w.y))
	print("HOVER1 portrait: %s" % JSON.stringify(h))
	shot("palantir-portrait")
	await away()
	if not bool(h.get("simple", false)):
		return "the portrait showed no unit help"
	return ""
