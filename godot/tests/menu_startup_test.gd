## Regression: intro playback advances the game clock before deferred MainMenu.apt loading.
## Run with retail installs configured:
## godot --headless --path godot --script res://tests/menu_startup_test.gd -- --intro --movie-skip-after=200
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	# Make the reveal deadline expire before the shell's first movie update.
	game.set("_frames", 100)
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if game.get("_menu_revealed"):
			var shell = game.get("_shell")
			var level: int = shell.shell_top_level()
			if not shell.has_level(level):
				printerr("MENU STARTUP FAIL: marked revealed before the movie loaded")
				quit(1)
				return
			var shown: Dictionary = shell.get_member(level, "", "MainMenuShown")
			if shown.get("value", "") not in ["true", "1"]:
				printerr("MENU STARTUP FAIL: ShowMainMenu did not reveal the loaded movie: ", shown)
				quit(1)
				return
			print("MENU STARTUP PASS")
			game.queue_free()
			await process_frame
			quit(0)
			return
	printerr("MENU STARTUP FAIL: main menu did not reveal before the deadline")
	quit(1)
