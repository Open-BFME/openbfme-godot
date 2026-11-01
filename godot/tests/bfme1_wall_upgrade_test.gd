## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_wall_upgrade_test.gd
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	print("BFME1 MOUNT ", mounted)
	if not mounted.ok: quit(1); return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	print("BFME1 SETUP ", setup.get("templates"), " templates, ", setup.get("factions"), " factions")
	for error in setup.errors: print("BFME1 ERROR ", error)
	if not setup.ok: quit(1); return
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"ScoreTest","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	slots[1] = {"state":6,"name":"Opponent","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":100000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	for i in 30: world.advance(0.2)
	var plots: Array = []
	for id in world.get_object_ids():
		var o: Dictionary = world.get_object(id)
		if o.get("owner", "") == "Player_1" and o.template == "GondorCastleUpgrade": plots.append(id)
	print("PLOTS ", plots.size())
	# tolerance: two distinct wall plots are needed to research the mutually exclusive upgrades.
	assert(plots.size() >= 2)
	for i in 2:
		var upgrade: String = ["Upgrade_TrebuchetTurret", "Upgrade_OpenGarrison"][i]
		world.queue_upgrade(3, plots[i], upgrade)
	for i in 400: world.advance(0.2)
	for i in 2:
		var object: Dictionary = world.get_object(plots[i])
		var upgrade: String = ["Upgrade_TrebuchetTurret", "Upgrade_OpenGarrison"][i]
		var condition: String = ["UPGRADE_TREBUCHET", "UPGRADE_GARRISON"][i]
		var model: String = ["#gbcasttreb#", "#gbcasttowr#"][i]
		assert(world.get_upgrades(plots[i]).object.has(upgrade))
		assert(object.conditions.has(condition), str(object))
		assert(object.shown_models.size() == 1, str(object))
		assert(str(object.shown_models[0]).begins_with(model), str(object))
		print("BFME1 WALL UPGRADE PASS ", upgrade, ": supporting structure visible")
	var trebuchets := 0
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.template == "GondorTrebuchetWall" and object.owner == "Player_1": trebuchets += 1
	assert(trebuchets == 1)
	assert(world.clear_game_data(false).ok)
	quit()
