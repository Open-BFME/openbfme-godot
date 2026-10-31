## Lane SPELLRC-1 (owner, 2026-10-22: "you can't right click mouse move with spellbook powers out"): with the spell book shown and a power bought,
## a selected unit takes a right click on the ground as its move order (RotWK's alternate mouse setup), as in RotWK 2.01: the right press reaches the
## world unless the Apt has a button under the pointer (RW 0x83EE14 -> 0x83EB7A -> 0x622764: TheAptWindowManager + 0x329, the Apt's current button
## gApt + 0x6C, RW 0x624EE1 -> 0xAE1270). With a spell book power aimed a right drag scrolls the camera and the power keeps waiting; a right click (not
## dragged) cancels it without an order (the GUI command translator RW 0x83D7AD: setGUICommand(0) at RW 0x83DB8D) and the next right click moves.
##
##   godot --headless --path godot --script res://tests/spellbook_rightclick_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failed := false


func _check(cond: bool, what: String) -> void:
	print(("  ok   " if cond else "  FAIL ") + what)
	if not cond:
		_failed = true


func _moves(hud) -> int:
	var n := 0
	for l in hud.get_message_log():
		if String(l).begins_with("MSG_DO_MOVETO"):
			n += 1
	return n


func _initialize() -> void:
	call_deferred("_run")


func _frames(world, n: int) -> void:
	for i in n:
		world.advance(1.0 / 30.0)
		await process_frame


func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SPELLBOOK RIGHTCLICK SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		print("SPELLBOOK RIGHTCLICK FAIL: the retail mount failed")
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not world.setup(fs).ok:
		print("SPELLBOOK RIGHTCLICK FAIL: GameWorld.setup")
		quit(1)
		return
	var loaded: Dictionary = world.load_map("map mp fall back 4p", {"terrain": true, "slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}]})
	_check(loaded.ok, "the map loads")
	var owner := -1
	for i in loaded.players.size():
		if String(loaded.players[i]).contains("'Player_1'"):
			owner = i
	_check(owner >= 0, "the local player is found")
	# the spell book's requirement (a COMMANDCENTER); a load_map game has no starting fortress
	_check(world.create_object("MenFortressCitadel", owner, 1200.0, 1200.0, 0.0) > 0, "a citadel for the spell book")
	var unit: int = world.create_object("GondorFighter", owner, 2000.0, 1800.0, 0.0)
	_check(unit > 0, "a unit to order")
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	_check(hud.setup(fs, world, camera, "Player_1", {"alternate_mouse": true, "camera_start": Vector3(2000, 0, -1800)}).ok, "the HUD is set up")
	world.set_shroud_drawn(false)
	await _frames(world, 40)
	# a power bought through the store: the spell book shows it
	_check(hud.open_spell_store(), "the store opens")
	await _frames(world, 30)
	var store: Dictionary = hud.get_spellbook_state().store
	var bought := false
	for i in store.states.size():
		if store.states[i] == "_active" and not bought:
			bought = hud.spell_store_click(i)
	_check(bought, "a power is bought")
	hud.close_spell_store()
	await _frames(world, 40)
	var book: Dictionary = hud.get_spellbook_state()
	_check(book.shown, "the spell book is shown")
	_check(book.slots[0] == "_up", "the bought power is up on the spell book: " + str(book.slots))
	hud.select_object_with_message(unit)
	await _frames(world, 10)
	_check(hud.get_selection().size() == 1, "the unit is selected")
	# right clicks on the ground across the view (above the Palantir's bar): each is a move order
	var tried := 0
	var moved := 0
	for target in [Vector2(2150, 1800), Vector2(1850, 1850), Vector2(2000, 1950), Vector2(2100, 1700)]:
		var p: Vector2 = hud.world_to_pixel(target)
		if p.y > 520.0 or p.x < 0.0 or p.x > 1280.0:
			continue
		var before := _moves(hud)
		hud.inject_mouse_move(p)
		await _frames(world, 3)
		hud.inject_mouse_button(2, true, p, false)
		hud.inject_mouse_button(2, false, p, false)
		await _frames(world, 4)
		tried += 1
		var ok := _moves(hud) > before
		moved += 1 if ok else 0
		_check(ok, "a right click on the ground at %s (world %s) orders the move" % [str(p), str(target)])
	_check(tried >= 2, "at least two ground points were tried")
	# the store open (the Palantir's PlayerMagic button, the spell book with every power): the ground beside it still takes the move
	_check(hud.open_spell_store(), "the store opens again")
	await _frames(world, 30)
	var store_tried := 0
	var store_moves := 0
	for target in [Vector2(2150, 1800), Vector2(1850, 1850), Vector2(2000, 1950), Vector2(2100, 1700)]:
		var p: Vector2 = hud.world_to_pixel(target)
		if p.y > 520.0 or p.x < 0.0 or p.x > 1280.0:
			continue
		var before := _moves(hud)
		hud.inject_mouse_move(p)
		await _frames(world, 3)
		hud.inject_mouse_button(2, true, p, false)
		hud.inject_mouse_button(2, false, p, false)
		await _frames(world, 4)
		var store_moved := _moves(hud) > before
		print("  store open: right click at %s moved=%s" % [str(p), str(store_moved)])
		store_tried += 1
		store_moves += 1 if store_moved else 0
	# tolerance: a point the store's buttons cover is the store's (RW 0x83EE14); at least one of the ground points lies beside them
	_check(store_moves >= 1, "with the store open a ground right click beside it orders the move (%d of %d)" % [store_moves, store_tried])
	hud.close_spell_store()
	await _frames(world, 10)
	_check(not hud.get_spellbook_state().targeting, "no power is aiming")
	# the bought power aimed (the cast bar's press): a right drag scrolls the camera and keeps it; a right click cancels it and orders nothing; the next moves
	_check(hud.press_spell_slot(0), "the spell book's first slot is pressed")
	await _frames(world, 3)
	_check(hud.get_spellbook_state().targeting, "the power is aiming")
	var g: Vector2 = hud.world_to_pixel(Vector2(2000, 1950))
	var cam0: Vector2 = hud.get_camera().position
	var before := _moves(hud)
	hud.inject_mouse_move(g)
	await _frames(world, 2)
	hud.inject_mouse_button(2, true, g, false)
	for i in 10:
		hud.inject_mouse_move(g + Vector2(18 * (i + 1), 0))
		await _frames(world, 2)
	hud.inject_mouse_button(2, false, g + Vector2(180, 0), false)
	await _frames(world, 4)
	var cam1: Vector2 = hud.get_camera().position
	print("  right drag: camera %s -> %s" % [str(cam0), str(cam1)])
	_check(cam0.distance_to(cam1) > 1.0, "a right drag with the power aimed scrolls the camera")
	_check(hud.get_spellbook_state().targeting, "the power still waits after the drag")
	_check(_moves(hud) == before, "the drag ordered nothing")
	g = hud.world_to_pixel(Vector2(2000, 1950))
	hud.inject_mouse_move(g)
	await _frames(world, 3)
	hud.inject_mouse_button(2, true, g, false)
	hud.inject_mouse_button(2, false, g, false)
	await _frames(world, 4)
	_check(not hud.get_spellbook_state().targeting, "a right click cancels the aimed power")
	_check(_moves(hud) == before, "the cancelling right click ordered nothing")
	hud.inject_mouse_button(2, true, g, false)
	hud.inject_mouse_button(2, false, g, false)
	await _frames(world, 4)
	_check(_moves(hud) == before + 1, "the next right click orders the move")
	print("SPELLBOOK RIGHTCLICK " + ("FAIL" if _failed else "PASS") + " (%d of %d ground right clicks moved)" % [moved, tried])
	quit(1 if _failed else 0)
