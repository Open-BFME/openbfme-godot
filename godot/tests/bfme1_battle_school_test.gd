## BFME1's original Battle School assets; no retail bytes are committed.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 BATTLE SCHOOL SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {}).ok)
	assert(shell.shell_push("MainMenu.apt"))
	for i in 90:
		shell.tick(0.033)
	assert(shell.get_shell_report().note_lines.has("unverified: [S-5017] BFME1 MainMenu _root.InGame mirrors the host flag; native writer unverified"))
	shell.set_auto_process(true)
	await _click(shell, "MainMenu.BattleSchool")
	await _movie(shell, "TutorialOpenBook")
	# The authored preview ends early without _root.InGame; native playback
	# must stay alive until its last-frame callback reveals the foreground.
	await create_timer(1.0).timeout
	assert(_has_movie(shell, "TutorialOpenBook"))
	await _movie_gone(shell, "TutorialOpenBook")
	await create_timer(1.2).timeout
	await _click(shell, "movieContainer.toggles.MovesandAttacks")
	await _movie(shell, "TutorialMovesAndAttacks")
	await create_timer(0.25).timeout
	var movies: Array = shell.get_movies()
	assert(movies.size() == 1)
	assert(movies[0].completion == "TurtorialMovieDone")
	assert(movies[0].error == "")
	await _click(shell, "movieContainer.toggles.Heroes")
	await _movie(shell, "TutorialHeroes")
	await _click(shell, "ExitMenu.ExitTutorial")
	await _movie(shell, "TutorialCloseBook")
	await _movie_gone(shell, "TutorialCloseBook")
	await create_timer(1.2).timeout
	assert(shell.find_button(shell.shell_top_level(), "MainMenu.BattleSchool").self_hit)
	assert(shell.get_report().movie_errors.is_empty())
	shell.free()
	world.free()
	print("BFME1 BATTLE SCHOOL PASS")
	quit()

func _has_movie(shell: Node, title: String) -> bool:
	for movie in shell.get_movies():
		if movie.title == title:
			assert(movie.error == "")
			return true
	return false

func _movie(shell: Node, title: String) -> void:
	for attempt in 100:
		if _has_movie(shell, title):
			return
		await create_timer(0.1).timeout
	assert(false, "Movie never started: " + title)

func _movie_gone(shell: Node, title: String) -> void:
	for attempt in 100:
		if not _has_movie(shell, title):
			return
		await create_timer(0.1).timeout
	assert(false, "Movie never completed: " + title)

func _click(shell: Node, path: String) -> void:
	var button: Dictionary = shell.find_button(shell.shell_top_level(), path)
	assert(button.found and button.self_hit, "Button is not reachable: " + path)
	shell.post_mouse_move_stage(Vector2(button.x, button.y))
	await create_timer(0.1).timeout
	shell.post_mouse_button(true)
	await create_timer(0.1).timeout
	shell.post_mouse_button(false)
	await create_timer(0.1).timeout
