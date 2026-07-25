extends SceneTree
## godot --headless --path game -s res://tests/cli_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "CLI_RUNNER")
	call_deferred("_run")

func _run() -> void:
	var runner_script = load("res://tests/run_stage_tests.gd")
	if runner_script == null:
		printerr("Failed to load run_stage_tests.gd")
		quit(2)
		return
	# Headless simulation assertions verify the audio hook counter, not playback.
	# Repeated play() calls cannot be mixed by the dummy audio driver before exit.
	var game_audio = root.get_node_or_null("GameAudio")
	if game_audio != null:
		game_audio.enabled = false
		game_audio.music_enabled = false
	var runner = runner_script.new()
	var report: String = runner.run_all()
	print(report)
	var code := 0 if int(runner.failed) == 0 else 1
	_cleanup_test_resources()
	runner = null
	await process_frame
	quit(code)

func _cleanup_test_resources() -> void:
	# AssetFactory caches detached Node3D templates. Production keeps them for the
	# process lifetime; this short-lived test process must release them explicitly.
	# Load dynamically because -s entry scripts are parsed before autoload names.
	var asset_factory = load("res://src/view/asset_factory.gd")
	if asset_factory != null:
		for cached in asset_factory._mesh_cache.values():
			if cached is Node and is_instance_valid(cached):
				(cached as Node).free()
		asset_factory._mesh_cache.clear()
	var game_state = root.get_node_or_null("GameState")
	if game_state != null:
		game_state.reset_match()
	var game_audio = root.get_node_or_null("GameAudio")
	if game_audio == null:
		return
	if is_instance_valid(game_audio.sfx_player):
		game_audio.sfx_player.stop()
		game_audio.sfx_player.stream = null
	if is_instance_valid(game_audio.music_player):
		game_audio.music_player.stop()
		game_audio.music_player.stream = null
	game_audio._streams.clear()
