## Edain's three Dwarven clan choices through a clicked keep and its real HUD button.
## Only test data names are Edain-specific; the production and upgrade paths are shared with retail.
extends "res://scripts/scenario_player.gd"

const CLANS := {
	"Erebor": "Upgrade_ClanLangbarte",
	"IronHills": "Upgrade_ClanFeuerbarte",
	"EredLuin": "Upgrade_ClanBreitschultern",
}
const CLAN_UNITS := {
	"Erebor": "DwarvenGuardianLangbarteHorde",
	"IronHills": "DwarvenGuardianFeuerbarteHorde",
	"EredLuin": "DwarvenGuardianBreitschulternHorde",
}
const CLAN_HERO_SLOTS := {
	"Erebor": "Command_GenericReviveSlot2",
	"IronHills": "Command_GenericReviveSlot6",
	"EredLuin": "Command_GenericReviveSlot12",
}

var game: Node
var hud: Node
var world: Node
var input_player: RefCounted


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	input_player = load("res://scripts/play1_player.gd").new()
	input_player.game = game
	input_player.hud = hud
	input_player.world = world
	input_player.shell = game._shell
	await input_player._headless_window()
	await _run_scenarios(scenarios)


func _find_keep(upgrade: String) -> Dictionary:
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			continue
		hud.camera_look_at(Vector2(o.x, o.y))
		await game._step(25)
		var center: Vector2 = hud.world_to_pixel(Vector2(o.x, o.y))
		var clicked := false
		for dy in range(-120, 41, 10):
			for dx in [0, -20, 20, -40, 40]:
				var px: Vector2 = center + Vector2(dx, dy)
				if int(hud.pick_probe(px, int(o.id)).get("picked", 0)) == int(o.id):
					await input_player.click(px)
					clicked = hud.get_selection().has(int(o.id))
					if clicked:
						break
			if clicked:
				break
		if not clicked:
			continue
		for b in hud.get_command_buttons():
			if str(b.get("upgrade", "")) == upgrade and str(b.get("command", "")) == "PLAYER_UPGRADE":
				return {"object": o, "button": b}
	return {}


func _choice(clan: String) -> String:
	if game._faction != "FactionDwarves":
		return "the selector needs a Dwarven skirmish"
	var upgrade: String = CLANS[clan]
	var selected: Dictionary = await _find_keep(upgrade)
	if selected.is_empty():
		var observed: Array = world.get_player_objects(game._local_name).filter(func(o): return o.structure).map(func(o): return o.template)
		return "no clickable keep with the clan PLAYER_UPGRADE button: " + JSON.stringify(observed)
	var keep: Dictionary = selected.object
	var button: Dictionary = selected.button
	var before: Dictionary = world.get_upgrades(int(keep.id))
	if not before.get("ok", false):
		return "the keep has no readable upgrade state"
	if Array(before.player).has(upgrade):
		return "the clan upgrade was already completed before the click"
	var book_before: Dictionary = world.get_spellbook(game._local_index)
	if not book_before.get("ok", false) or int(book_before.get("book", 0)) == 0:
		return "the starting spell book is missing"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(15)
	game._save_named("edainfactions1-" + clan + "-before")
	var pixel: Vector2 = input_player.palantir_button(str(button.frame))
	if pixel.x < 0:
		return "the clan button has no visible HUD window: " + JSON.stringify(button)
	if int(button.state) != 1:
		return "the clan button is not enabled: " + JSON.stringify(button)
	var messages_before: int = hud.get_message_log().size()
	print("EDAINFACTIONS1 %s: pressing %s at %s; selection %s" % [
			clan, JSON.stringify(button), str(pixel), JSON.stringify(hud.get_selection())])
	await input_player.click(pixel)
	await game._step(30)
	var after: Dictionary = world.get_upgrades(int(keep.id))
	if not after.get("ok", false) or not Array(after.player).has(upgrade):
		return "the click did not complete the player upgrade: %s, commands %s, production %s, selection %s" % [
				JSON.stringify(after), JSON.stringify(hud.get_message_log().slice(messages_before)),
				JSON.stringify(world.get_production(game._local_index, int(keep.id))), JSON.stringify(hud.get_selection())]
	for other in CLANS.values():
		if other != upgrade and Array(after.player).has(other):
			return "another clan was also purchased: " + JSON.stringify(after)
	var buttons: Array = hud.get_command_buttons()
	for b in buttons:
		if str(b.get("upgrade", "")) in CLANS.values():
			return "the keep still offers a clan choice after selection: " + JSON.stringify(b)
	if not buttons.any(func(b): return str(b.get("name", "")) == "Command_RingMechanikDwarven"):
		return "the keep did not switch to its clan command set: " + JSON.stringify(buttons)
	if clan == "IronHills" and not buttons.any(func(b): return str(b.get("name", "")) == "Command_ConstructIronHillsScout"):
		return "the Iron Hills keep does not offer its scout"
	var book_after: Dictionary = world.get_spellbook(game._local_index)
	if not book_after.get("ok", false) or int(book_after.get("book", 0)) == 0:
		return "the spell book disappeared after choosing the clan"
	if book_after.book != book_before.book or Array(book_after.powers).is_empty():
		return "the clan changed or emptied the Dwarven spell book"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(15)
	game._save_named("edainfactions1-" + clan + "-after")
	print("EDAINFACTIONS1 %s: keep %s, player upgrade %s, command buttons %d, spell powers %d" %
			[clan, str(keep.template), upgrade, buttons.size(), Array(book_after.powers).size()])
	return await _check_roster(clan, keep)


func _check_roster(clan: String, keep: Dictionary) -> String:
	# The barracks is only a test fixture; the choice, command-set swap, and spell book above use the live skirmish.
	var id: int = world.create_object("DwarvenKaserne", game._local_index, keep.x + 280.0, keep.y + 220.0, 0.0)
	if id <= 0:
		return "the roster fixture could not create a DwarvenKaserne"
	print("EDAINFACTIONS1 TEST HOOK: created DwarvenKaserne %d to inspect clan roster" % id)
	await game._step(20)
	if not hud.select_object_with_message(id):
		return "the newly created barracks could not be selected"
	await game._step(8)
	var roster: Array = hud.get_command_buttons()
	var unit: String = CLAN_UNITS[clan]
	if not roster.any(func(b): return str(b.get("template", "")) == unit and str(b.get("command", "")) == "UNIT_BUILD"):
		return "the barracks does not offer the clan's %s: %s" % [unit, JSON.stringify(roster)]
	var heroes: Array = roster.filter(func(b): return str(b.get("name", "")) == "Command_SelectRevivablesDwarves")
	if heroes.is_empty():
		return "the clan barracks has no hero submenu"
	var pixel: Vector2 = input_player.palantir_button(str(heroes[0].frame))
	if pixel.x >= 0:
		await input_player.click(pixel)
	elif not hud.press_command_button("Command_SelectRevivablesDwarves"):
		return "the barracks hero submenu is not available: " + JSON.stringify(heroes[0])
	await game._step(8)
	var hero_slot: String = CLAN_HERO_SLOTS[clan]
	var hero_buttons: Array = hud.get_command_buttons()
	if not hero_buttons.any(func(b): return str(b.get("name", "")) == hero_slot):
		return "the clan barracks does not offer %s: %s" % [hero_slot, JSON.stringify(hero_buttons)]
	print("EDAINFACTIONS1 %s: barracks %s, hero slot %s available" % [clan, unit, hero_slot])
	return ""


func _scenario_Erebor() -> String:
	return await _choice("Erebor")


func _scenario_IronHills() -> String:
	return await _choice("IronHills")


func _scenario_EredLuin() -> String:
	return await _choice("EredLuin")


func _scenario_AI() -> String:
	var dwarf := ""
	for p in world.get_economy().get("players", []):
		if str(p.faction) == "FactionDwarves" and str(p.name) != game._local_name:
			dwarf = str(p.name)
			break
	if dwarf.is_empty():
		return "the game has no Dwarven computer opponent"
	world.set_time_scale(20.0)
	var start: int = world.get_frame()
	while world.get_frame() < start + 3000:
		await game.get_tree().process_frame
	world.set_time_scale(1.0)
	var objects: Array = world.get_player_objects(dwarf)
	if objects.is_empty():
		return "the Dwarven AI has no surviving objects at frame %d" % world.get_frame()
	var upgrades: Dictionary = world.get_upgrades(int(objects[0].id))
	var chosen: Array = CLANS.values().filter(func(u): return Array(upgrades.get("player", [])).has(u))
	var spent := 0
	for p in world.get_economy().get("players", []):
		if str(p.name) == dwarf:
			spent = int(p.spent)
	print("EDAINFACTIONS1 AI: frame %d, %s has %d objects, spent %d, clans %s" % [
			world.get_frame(), dwarf, objects.size(), spent, JSON.stringify(chosen)])
	if chosen.size() != 1:
		return "the Dwarven AI did not choose exactly one clan at frame 3000"
	if spent <= 0 or objects.size() <= 25:
		return "the Dwarven AI chose a clan but did not expand or spend resources"
	return ""
