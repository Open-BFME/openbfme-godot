## BFME2 selection mounts verified BFME2 data without the RotWK overlay.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	create_timer(90).timeout.connect(func(): push_error("BFME2 gameplay test timed out"); quit(1))
	var release = root.get_node("Release")
	if not (release.selected_game() == "bfme2-106"):
		push_error('release.selected_game() == "bfme2-106"')
		quit(1)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = await release.mount_retail(fs)
	if not (mounted.ok):
		push_error(str(mounted))
		quit(1)
		return
	if not (not fs.is_bfme1()):
		push_error('not fs.is_bfme1()')
		quit(1)
		return
	if not (not fs.is_shell_source()):
		push_error('not fs.is_shell_source()')
		quit(1)
		return
	if not (mounted.stops == PackedStringArray(["[S-5911] BFME2-only gameplay uses the shared simulation; BFME2 versus RotWK logic differences remain unverified, including the stored-only PreLandingEmotionAffectsAllies filter"])):
		push_error('mounted.stops == PackedStringArray(["[S-5911] BFME2-only gameplay uses the shared simulation; BFME2 versus RotWK logic differences remain unverified, including the stored-only PreLandingEmotionAffectsAllies filter"])')
		quit(1)
		return
	if not (not mounted.mounted.is_empty()):
		push_error('not mounted.mounted.is_empty()')
		quit(1)
		return
	for archive in mounted.mounted:
		if not (archive.install == "bfme2"):
			push_error(str(archive))
			quit(1)
			return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not (setup.ok):
		push_error(str(setup))
		quit(1)
		return
	if not (setup.factions == 6):
		push_error('setup.factions == 6')
		quit(1)
		return
	var loaded: Dictionary = world.load_map("map mp fords of isen ii", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]})
	if not (loaded.ok):
		push_error(str(loaded))
		quit(1)
		return
	var first_frame: int = world.get_frame()
	world.set_time_scale(1.0)
	world.set_paused(false)
	for i in 10: world.advance(0.2)
	if not (world.get_frame() == first_frame + 10):
		push_error(str(world.get_frame(), " from ", first_frame))
		quit(1)
		return
	print("BFME2 GAMEPLAY PASS: BFME2-only archives, six factions and live map simulation")
	world.free()
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	while game._state == game.State.BOOT: await process_frame
	if not (game._state == game.State.MENU):
		push_error('game._state == game.State.MENU')
		quit(1)
		return
	if not (game._menu_style == "bfme2"):
		push_error('game._menu_style == "bfme2"')
		quit(1)
		return
	if not (game._shell.get_shell_report().errors.is_empty()):
		push_error(str(game._shell.get_shell_report()))
		quit(1)
		return
	print("BFME2 MENU PASS")
	game.queue_free()
	await process_frame
	quit()
