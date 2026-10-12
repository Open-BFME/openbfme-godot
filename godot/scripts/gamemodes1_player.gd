## Lane GAMEMODES-1: the proof run of RotWK's two skirmish game modes (game.gd --auto --gamemodes=<mode> --screens=<dir>; docs/GAMEMODES.md).
##   battle   the enemy's structures fall (a test hook) while its builders live: rule 3 (RW 0x808A93) takes the enemy out at the next judgement and the
##            victory screen follows (gamemodes1-battle-base.png before, gamemodes1-battle-victory.png after)
##   sandbox  (with --teams=0,0: one team; game.gd's gamemodes1-sandbox-lobby.png is the lobby as Start is pressed: a skirmish shows no line for
##            "Entering sandbox mode...", RW 0x928482) the game's HUD (gamemodes1-sandbox-game.png), then the ally's structures fall: it is defeated
##            (gamemodes1-sandbox-ally-defeated.png), but a sandbox is never won (RW 0x808C64): no victory screen (gamemodes1-sandbox-no-victory.png)
## Prints "GAMEMODES1 <what>: ok / FAIL <why>" and "GAMEMODES1 RESULT: <n> ok, <m> failed".
extends RefCounted

var game: Node
var hud: Node
var world: Node
var helper # scripts/hud5_player.gd: the camera helpers
var scout_template := "GondorBoromir" # the default --faction FactionMen
var ok_count := 0
var fail_count := 0


func run(p_game: Node, mode: String) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	helper = load("res://scripts/hud5_player.gd").new()
	helper.game = game
	helper.hud = hud
	helper.world = world
	var r := ""
	if mode == "battle":
		r = await _battle()
	elif mode == "sandbox":
		r = await _sandbox()
	else:
		r = "unknown mode '%s'" % mode
	if r == "":
		ok_count += 1
		print("GAMEMODES1 %s: ok" % mode)
	else:
		fail_count += 1
		print("GAMEMODES1 %s: FAIL %s" % [mode, r])
	print("GAMEMODES1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


func _other() -> String:
	for p in world.get_economy().players:
		if p.playable and p.name != game._local_name:
			return p.name
	return ""


# every structure of `player` takes a lethal hit; returns {structures, builders, centre}
func _fell_structures(player: String) -> Dictionary:
	var structures := 0
	var builders := 0
	var centre := Vector2()
	for o in world.get_player_objects(player):
		if o.commandcenter:
			centre = Vector2(o.x, o.y)
		if o.get("dozer", false):
			builders += 1
	for o in world.get_player_objects(player):
		if o.structure:
			world.debug_damage_object(o.id, 1.0e7, 0)
			structures += 1
	return {"structures": structures, "builders": builders, "centre": centre}


func _base_centre(player: String) -> Vector2:
	for o in world.get_player_objects(player):
		if o.commandcenter:
			return Vector2(o.x, o.y)
	var objs: Array = world.get_player_objects(player)
	return Vector2(objs[0].x, objs[0].y) if not objs.is_empty() else Vector2()


func _battle() -> String:
	var enemy := _other()
	if enemy.is_empty():
		return "no enemy"
	await game._wait_seconds(6.0) # past the first judgement (frame 25)
	var c := _base_centre(enemy)
	# a scout of the local player's beside the enemy's base lifts the shroud there (a test hook), so the base and its builders show before the fall
	var scout: int = world.create_object(scout_template, helper.local_index(), c.x - 260.0, c.y)
	print("GAMEMODES1 battle: scout %d" % scout)
	await helper.look(c.x, c.y)
	await game._wait_seconds(2.0)
	game._save_named("gamemodes1-battle-base")
	var fell := _fell_structures(enemy)
	print("GAMEMODES1 battle: %s's %d structures fall, %d builders alive" % [enemy, fell.structures, fell.builders])
	if fell.builders <= 0:
		return "the enemy has no builder: nothing to show"
	var waited := 0.0
	while waited < 20.0 and not world.get_end_game_state().get("shown", false):
		await game._wait_seconds(0.25)
		waited += 0.25
	print("GAMEMODES1 battle victory report: ", JSON.stringify(world.get_victory_report()))
	if not world.get_end_game_state().get("shown", false):
		return "no end screen after the last structure fell"
	await game._wait_seconds(2.5)
	game._save_named("gamemodes1-battle-victory")
	return ""


func _sandbox() -> String:
	var ally := _other()
	if ally.is_empty():
		return "no second player"
	var ctx: Dictionary = world.get_quit_menu_context()
	print("GAMEMODES1 sandbox: quit menu context ", JSON.stringify(ctx))
	if not ctx.get("sandbox", false):
		return "the game is not a sandbox"
	await game._wait_seconds(8.0) # one alliance from the first judgement on, nobody defeated: no victory
	game._save_named("gamemodes1-sandbox-game")
	if world.get_end_game_state().get("shown", false):
		return "an end screen in a sandbox before anyone was defeated"
	var c := _base_centre(ally)
	await helper.look(c.x, c.y)
	var fell := _fell_structures(ally)
	print("GAMEMODES1 sandbox: the ally %s's %d structures fall" % [ally, fell.structures])
	await game._wait_seconds(2.0)
	game._save_named("gamemodes1-sandbox-ally-defeated") # "<ally> has been Defeated", and no victory screen
	await game._wait_seconds(8.0)
	var rep: Dictionary = world.get_victory_report()
	print("GAMEMODES1 sandbox victory report: ", JSON.stringify(rep))
	game._save_named("gamemodes1-sandbox-no-victory")
	if world.get_end_game_state().get("shown", false):
		return "a sandbox showed an end screen"
	if rep.get("defeated", []).is_empty():
		return "the ally was not defeated"
	return ""
