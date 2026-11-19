## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_score_screen_test.gd
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
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {}).ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"ScoreTest","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	slots[1] = {"state":6,"name":"Opponent","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":2000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	for i in 30: world.advance(0.2)
	var cleared: Dictionary = world.clear_game_data(true)
	assert(cleared.ok, str(cleared))
	assert(shell.set_score_screen_from_world(world))
	assert(shell.shell_push("ScoreScreen.apt"))
	for i in 180: shell.tick(0.033)
	for i in 3: await process_frame
	assert(shell.get_shell_report().errors.is_empty(), str(shell.get_shell_report()))
	var rows: Array = cleared.score_screen.entries.duplicate()
	assert(rows.size() == 2)
	rows.sort_custom(func(a, b): return a.score > b.score)
	var texts := {}
	for op in shell.describe_ops():
		if not op.begins_with("text "): continue
		texts[op.split(" ")[1]] = op.split("'")[1]
	for row in rows.size():
		var columns := ["name", "units_built", "units_lost", "units_destroyed", "structures_built", "structures_lost", "structures_destroyed", "money_earned"]
		for column in columns.size():
			var path := "_level2.Table.%d.ColoredItems.instance%d" % [row, column + 1]
			assert(texts.get(path) == str(rows[row][columns[column]]), path + ": " + str(texts.get(path)))
	var requests: Array = []
	shell.shell_request.connect(func(action, _argument): requests.append(action))
	assert(shell.shell_fscommand("AptScoreScreen::Continue", ""))
	shell.tick(0.033)
	assert(requests == ["ScoreScreenContinue"], str(requests))
	print("BFME1 SCORE PASS")
	quit()
