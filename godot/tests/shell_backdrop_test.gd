## Retail BFME2 shell scene animates without loading a gameplay world.
extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	await create_timer(3.0).timeout
	var shell = game.get("_shell")
	assert(shell != null)
	var first: Dictionary = shell.get_backdrop_state()
	assert(first.backdrop_image == "Shell02_01", str(first))
	await create_timer(0.5).timeout
	var second: Dictionary = shell.get_backdrop_state()
	assert(second.animation_frame > first.animation_frame, "shell animation did not advance")
	assert(shell.get_report().view3d_errors.is_empty(), str(shell.get_report().view3d_errors))
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("/tmp/openbfme-shell-backdrop.png")
	shell.shell_push("Options.apt")
	await create_timer(0.5).timeout
	assert(shell.get_backdrop_state().backdrop_image == "ShellMapLowLOD", "settings backdrop changed")
	shell.shell_hide_background()
	shell.shell_show_shell_map(false)
	await create_timer(0.2).timeout
	assert(shell.get_backdrop_state().backdrop_image == "", "shell backdrop survived game start")
	assert(shell.get_backdrop_state().background_commands == 0, "front-end overlay survived game start")
	shell.shell_fscommand("SetBackground", "fadein")
	await create_timer(0.5).timeout
	assert(shell.get_backdrop_state().background_mode == 1, "settings background did not restore")
	print("SHELL BACKDROP PASS frames ",first.animation_frame," -> ",second.animation_frame)
	game.queue_free()
	await process_frame
	quit(0)
