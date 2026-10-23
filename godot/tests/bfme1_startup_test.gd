## Run with BFME1_INSTALL set and -- --game=bfme1.
## Exercise the complete startup path, including optional menu overlays.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("BFME1_INSTALL").is_empty():
		print("SKIP: set BFME1_INSTALL")
		quit(77)
		return
	create_timer(60).timeout.connect(func(): push_error("BFME1 startup timed out"); quit(1))
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	while game._state == game.State.BOOT:
		await process_frame
	assert(game._state == game.State.MENU, "BFME1 must reach the main menu")
	assert(game._fs.is_bfme1())
	assert(game._shell.get_shell_report().errors.is_empty(), str(game._shell.get_shell_report().errors))
	print("BFME1 STARTUP PASS")
	game.queue_free()
	await process_frame
	quit()
