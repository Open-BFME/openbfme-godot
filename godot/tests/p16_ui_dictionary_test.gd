## P16 device bridge: legacy eight-slot dictionaries and explicit extended capacity.
## No generated or retail bytes are written. Requires the two retail installations.
extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	print("PASS " if ok else "FAIL ", what)
	if not ok:
		failures += 1

func message(capacity: int) -> Dictionary:
	var slots: Array = []
	for i in (8 if capacity == 8 else 16):
		slots.append({"state": 6 if i < capacity else 1, "name": "Player %d" % (i + 1), "accepted": true,
			"color": i, "start_pos": -1, "player_template": 3, "team": i / 2})
	var result := {"mode": "skirmish", "map": "maps/map mp evendim/map mp evendim.map", "map_crc": 0,
		"map_size": 0, "seed": 9001, "starting_cash": 2000, "slots": slots}
	if capacity > 8:
		result.map_start_count = capacity
	return result

func state(game: Dictionary) -> Dictionary:
	var slots: Array = []
	for slot in game.slots:
		slots.append({"has_player": slot.state == 6})
	return {"ok": true, "game": game, "local_slot": 0, "slots": slots,
		"show_random_template": false, "show_random_color": false}

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("P16_UI_DICTIONARY SKIP: retail installations required")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	check(mounted.get("ok", false), "mount retail")
	if not mounted.get("ok", false):
		quit(1)
		return
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	check(setup.get("ok", false), "world setup")
	var menu: Node = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(menu)
	var boot: Dictionary = menu.boot_shell(fs, world, {})
	check(boot.get("ok", false), "shell setup")
	if failures:
		quit(1)
		return
	var legacy := message(8)
	for i in range(2, 8):
		legacy.slots[i].state = 1
	var prepared: Dictionary = world.prepare_new_game(legacy)
	check(prepared.get("ok", false), "unchanged eight-slot message accepted: %s" % str(prepared.get("errors", [])))
	if prepared.get("ok", false):
		check(prepared.resolved.slots.size() == 8, "legacy output stays eight-wide")
		check(not prepared.resolved.has("map_start_count"), "legacy output shape unchanged")
	for capacity in [9, 16]:
		var output: Dictionary = menu.set_player_status(state(message(capacity)))
		check(output.get("ok", false), "explicit %d-slot dictionary accepted" % capacity)
		if output.get("ok", false):
			check(output.rows.size() == capacity, "all %d player status rows retained" % capacity)
			if capacity == 16:
				check(output.rows[15][4] == -10274016, "slot sixteen retains extended Brown ARGB")
				check(output.rows[15][2] == "8", "extended team eight label")
	var bad := message(16)
	bad.erase("map_start_count")
	check(not menu.set_player_status(state(bad)).ok, "wide dictionary without capacity refused")
	for capacity in [7, 17, "16", 16.5]:
		bad = message(16)
		bad.map_start_count = capacity
		check(not menu.set_player_status(state(bad)).ok, "invalid capacity %s refused" % str(capacity))
	bad = message(16)
	bad.slots.pop_back()
	check(not menu.set_player_status(state(bad)).ok, "truncated extended dictionary refused")
	bad = message(9)
	bad.slots[9].state = 0
	check(not menu.set_player_status(state(bad)).ok, "open slot beyond map capacity refused")
	var load_legacy: Dictionary = menu.set_load_screen_from_game(message(8))
	check(load_legacy.get("cards", []).size() == 8, "legacy loading cards remain eight-wide")
	check(not load_legacy.has("map_start_count"), "legacy loading report shape unchanged")
	var load_extended: Dictionary = menu.set_load_screen_from_game(message(16))
	check(load_extended.get("cards", []).size() == 16, "sixteen loading cards retained")
	check(load_extended.get("map_start_count", 0) == 16, "extended loading capacity retained")
	if load_extended.get("cards", []).size() == 16:
		check(load_extended.cards[15].color == 0x633B20, "last loading card retains Brown")
		check(load_extended.cards[15].team == 7, "last loading card retains team eight")
	check(menu.set_load_screen(load_extended).get("ok", false), "explicit sixteen-card presentation accepted")
	var load_bad: Dictionary = load_extended.duplicate(true)
	load_bad.erase("map_start_count")
	check(not menu.set_load_screen(load_bad).get("ok", true), "wide loading cards without capacity refused, not truncated")
	load_bad = load_extended.duplicate(true)
	load_bad.map_start_count = "16"
	check(not menu.set_load_screen(load_bad).get("ok", true), "non-integer loading capacity refused")
	load_bad = load_extended.duplicate(true)
	load_bad.cards[15] = "invalid"
	check(not menu.set_load_screen(load_bad).get("ok", true), "malformed loading card refused")
	load_bad = load_extended.duplicate(true)
	load_bad.local_card = 16
	check(not menu.set_load_screen(load_bad).get("ok", true), "unknown local loading card refused")
	var disconnect := {"visible": true, "rows": []}
	for i in 7:
		disconnect.rows.append({"used": true, "name": "Player %d" % (i + 2), "bar": 50, "votes": 0, "kick": true})
	var dc: Dictionary = menu.disconnect_screen(disconnect)
	check(dc.ok and not dc.has("first_row"), "legacy disconnect keeps seven-row presentation")
	for i in range(7, 15):
		disconnect.rows.append({"used": true, "name": "Player %d" % (i + 2), "bar": 50, "votes": 0, "kick": true})
	check(not menu.disconnect_screen(disconnect).ok, "wide disconnect without map capacity refused")
	disconnect.map_start_count = 16
	dc = menu.disconnect_screen(disconnect)
	check(dc.ok and dc.row_count == 15, "explicit sixteen-slot disconnect retains all fifteen remote rows")
	var dc_bad: Dictionary = disconnect.duplicate(true)
	dc_bad.rows[14] = "invalid"
	check(not menu.disconnect_screen(dc_bad).ok, "malformed disconnect row refused")
	dc_bad = disconnect.duplicate(true)
	dc_bad.map_start_count = "16"
	check(not menu.disconnect_screen(dc_bad).ok, "noninteger disconnect capacity refused")
	dc_bad = disconnect.duplicate(true)
	dc_bad.rows[14].bar = 101
	check(not menu.disconnect_screen(dc_bad).ok, "invalid disconnect progress refused")
	menu.disconnect_screen({"visible": false})
	print("P16_UI_DICTIONARY ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
