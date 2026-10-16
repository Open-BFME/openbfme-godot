extends SceneTree
func _initialize() -> void:
	var game = load("res://scripts/game.gd").new()
	assert(game._scripted_run() == OS.get_cmdline_user_args().has("--auto"))
	game.free()
	print("BFME1 LAUNCH ARGS PASS")
	quit()
