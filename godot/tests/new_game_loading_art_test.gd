## The new-game player loading screen keeps the ring backdrop until gameplay.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _fail(message: String) -> void:
	printerr("NEW GAME LOADING ART FAIL: " + message)
	quit(1)

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty():
		print("NEW GAME LOADING ART SKIP: ROTWK_INSTALL is not set")
		quit(77)
		return
	create_timer(180).timeout.connect(func(): _fail("timeout"))
	var folder := OS.get_environment("RESUME_TEST_DIR")
	if folder.is_empty():
		folder = OS.get_temp_dir().path_join("openbfme-loading-art-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()])
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		_fail("cannot create test directory")
		return
	var host = load("res://tests/new_game_loading_art_host.gd")
	if host == null or not host.can_instantiate():
		_fail("host script could not load")
		return
	var game = host.new()
	game.replay_folder = folder
	game._intro = "off"
	game._options_file = folder.path_join("Options.ini")
	game._save_dir_arg = folder.path_join("Save")
	root.add_child(game)
	while game._state != game.State.FAILED and (game._state != game.State.MENU or game._campaign_flow == null):
		await process_frame
	if game._state != game.State.MENU:
		_fail("game did not reach menu")
		return
	var slots := []
	for i in 8: slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	for i in 2: slots[i] = {"state": 6, "name": "Loading test %d" % i, "accepted": true, "color": i, "start_pos": i, "player_template": 4, "team": i}
	await game._begin_game({"mode": "skirmish", "map_crc": 0, "map_size": 0, "map": "maps/map wor buckland/map wor buckland.map", "seed": 42, "starting_cash": 2000, "slots": slots})
	if not game.loading_art_error.is_empty():
		_fail(game.loading_art_error)
		return
	if game._state != game.State.PLAYING:
		_fail("game did not reach gameplay")
		return
	if not game.checked_loading_art:
		_fail("ring never produced two visible advancing samples")
		return
	game._shell.tick(0.033)
	game._shell.render(true)
	var art: Dictionary = game._shell.get_backdrop_state()
	if art.backdrop_image != "" or art.background_mode != 0:
		_fail("loading art remains during gameplay")
		return
	game.free()
	await process_frame
	print("NEW GAME LOADING ART PASS")
	quit()
