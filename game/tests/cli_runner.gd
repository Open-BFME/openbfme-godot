extends SceneTree
## godot --headless --path game -s res://tests/cli_runner.gd

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var runner_script = load("res://tests/run_stage_tests.gd")
	if runner_script == null:
		printerr("Failed to load run_stage_tests.gd")
		quit(2)
		return
	var runner = runner_script.new()
	var report: String = runner.run_all()
	print(report)
	var code := 0 if int(runner.failed) == 0 else 1
	quit(code)
