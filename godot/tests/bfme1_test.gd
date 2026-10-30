extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit()
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	print("BFME1 MOUNT ", mounted)
	if not mounted.ok: quit(1); return
	assert(fs.install_root() == install, "BFME1 loose resources must use the BFME1 install")
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	print("BFME1 SETUP ", setup.get("templates"), " templates, ", setup.get("factions"), " factions")
	for error in setup.errors: print("BFME1 ERROR ", error)
	if not setup.ok: quit(1); return
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	var boot: Dictionary = shell.boot_shell(fs, world, {"skirmish_ini": OS.get_user_data_dir() + "/bfme1-test-skirmish.ini"})
	print("BFME1 SHELL ", boot)
	if not boot.ok: quit(1); return
	var cursor: Dictionary = shell.get_cursor_image()
	assert(cursor.get("name", "") == "Arrow")
	assert(cursor.get("texture") != null, "BFME1 must load its native cursor image")
	assert(not cursor.get("hidden", true))
	assert(shell.get_report().get("cursor_errors", []).is_empty())
	if not shell.shell_push("MainMenu.apt"): print(shell.get_shell_report()); quit(1); return
	for i in 90: shell.tick(0.033)
	print("BFME1 MENU ", shell.get_shell_report())
	if not shell.shell_fscommand("AptMainMenu::Skirmish", ""): quit(1); return
	for i in 90: shell.tick(0.033)
	print("BFME1 PROFILE ", shell.lobby_apply({"profile":"BFME1Test"}))
	for i in 90: shell.tick(0.033)
	print("BFME1 LOBBY REPORT ", shell.get_shell_report())
	var lobby: Dictionary = shell.lobby_state()
	print("BFME1 LOBBY ", lobby)
	if not lobby.get("found", false): quit(1); return
	var keys: PackedStringArray = lobby.get("map_keys", PackedStringArray())
	assert(not keys.is_empty())
	for key in keys:
		assert(key.begins_with("maps/map mp "), key)
		assert(not key.begins_with("maps/map mp test/"), key)
		assert(not key.begins_with("maps/map mp nctest/"), key)
	# Exercise real skirmish startup: load_map alone never unpacks start castles.
	for faction in [3, 4]: # BFME1 store: Rohan, Gondor
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"Test","accepted":true,"color":0,"start_pos":0,"player_template":faction,"team":0}
		var prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":2000,"slots":slots})
		assert(prepared.ok, str(prepared))
		var loaded: Dictionary = world.start_new_game({"logic_thread":false})
		assert(loaded.ok, str(loaded.get("errors")))
		world.set_shroud_drawn(true)
		for i in 10: world.advance(0.2)
		var prefix := "Rohan" if faction == 3 else "Gondor"
		var walls := 0
		var gates := 0
		var keeps := 0
		for id in world.get_object_ids():
			var object: Dictionary = world.get_object(id)
			var name: String = object.get("template", "")
			if object.get("owner", "") != "Player_1": continue
			if name == prefix + "CastleWall": walls += 1
			elif name == prefix + "CastleGateL" or name == prefix + "CastleGateR": gates += 1
			elif name == prefix + "CastleBaseKeep": keeps += 1
			elif name == ("RBCastleFloor" if faction == 3 else "GBCastleFloor"): pass
			else: continue
			assert(object.get("instances", 0) > 0, str(object))  # tolerance: the object was drawn (instance count depends on its LOD and pieces)
		assert(walls >= 8, "%s: only %d walls" % [prefix, walls])  # tolerance: a castle stands: its wall count varies with the faction's layout (port note, community PR #36)
		assert(gates == 2, "%s: only %d gate leaves" % [prefix, gates])
		assert(keeps == 1, "%s: missing keep" % prefix)
		print("BFME1 CASTLE ", prefix, ": ", walls, " walls, ", gates, " gate leaves, keep rendered")
	# Regression: Enedwaith's Gondor outposts load a base with PolyGonTriggers.
	var outpost_slots: Array = []
	for i in 8:
		outpost_slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	outpost_slots[0] = {"state":6,"name":"Test","accepted":true,"color":8,"start_pos":7,"player_template":4,"team":-1}
	outpost_slots[1] = {"state":2,"name":"AI","accepted":true,"color":15,"start_pos":0,"player_template":4,"team":-1}
	var outpost_prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp enedwaith/map mp enedwaith.map","seed":1911,"starting_cash":-1,"slots":outpost_slots})
	assert(outpost_prepared.ok, str(outpost_prepared))
	var outpost_loaded: Dictionary = world.start_new_game({"logic_thread":false})
	assert(outpost_loaded.ok, str(outpost_loaded.get("errors")))
	for i in 10: world.advance(0.2)
	print("BFME1 ENEDWAITH outposts loaded")
	print("BFME1 PASS")
	quit()
