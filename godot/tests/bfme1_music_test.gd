## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_music_test.gd
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
	var audio = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	assert(audio.boot(fs, {"seed":7}).ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"ScoreTest","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	slots[1] = {"state":6,"name":"Opponent","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":2000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	assert(world.attach_audio().ok)
	for i in 300:
		world.advance(0.2)
		await process_frame
	var music: Dictionary = world.get_audio_report()
	assert(music.music_error == "", str(music))
	assert(music.music_unported.is_empty(), str(music.music_unported))
	# tolerance: randomized playlist choices and durations vary; at least one track must start within a minute of game time.
	assert(music.music_tracks_started > 0, str(music))
	assert(music.music_track_failures == 0, str(music))
	assert(not music.music_track.is_empty())
	assert(audio.is_music_playing(), str(audio.get_report()))
	assert(audio.get_report().play_failures == 0, str(audio.get_report()))
	world.clear_game_data(false)
	audio.shutdown()
	print("BFME1 MUSIC PASS: in-game playlist starts and plays ", music.music_track)
	quit()
