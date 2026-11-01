## Parse the clan-choice harness on Boat; with OPENBFME_EDAIN_INSTALL and OPENBFME_EDAIN_ASSETS_INSTALL,
## run all three choices and the Dwarven AI through real skirmishes (one child process at a time).
## godot --headless --path godot --script res://tests/edainfactions1_test.gd
extends SceneTree

const CHOICES := ["Erebor", "IronHills", "EredLuin", "AI"]


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var harness: GDScript = load("res://scripts/edainfactions1_player.gd")
	if harness == null or not harness.can_instantiate():
		printerr("EDAINFACTIONS1 FAIL: selector harness does not compile")
		quit(1)
		return
	var player: RefCounted = harness.new()
	for choice in CHOICES:
		if not player.has_method("_scenario_" + choice):
			printerr("EDAINFACTIONS1 FAIL: missing ", choice)
			quit(1)
			return
	var install := OS.get_environment("OPENBFME_EDAIN_INSTALL")
	var assets := OS.get_environment("OPENBFME_EDAIN_ASSETS_INSTALL")
	if install.is_empty() and assets.is_empty():
		print("EDAINFACTIONS1 harness parses; SKIP: set OPENBFME_EDAIN_INSTALL and OPENBFME_EDAIN_ASSETS_INSTALL for mod gameplay")
		quit(0)
		return
	if install.is_empty() or assets.is_empty() or not DirAccess.dir_exists_absolute(install) or not DirAccess.dir_exists_absolute(assets):
		printerr("EDAINFACTIONS1 FAIL: both Edain install layers must exist (no fallback)")
		quit(1)
		return
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		printerr("EDAINFACTIONS1 FAIL: ROTWK_INSTALL and BFME2_INSTALL must be set")
		quit(1)
		return
	var failures := 0
	for choice in CHOICES:
		var output: Array = []
		var code: int = OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--",
				"--auto", "--no-record", "--opponents=2", "--ai=4", "--faction=FactionDwarves", "--seed=7",
				"--edain-choice=" + choice, "--mod-install=" + assets, "--mod-install=" + install], output, true)
		var text: String = "".join(output)
		var result := "EDAINFACTIONS1 %s: ok" % choice
		if code != 0 or not text.contains(result):
			printerr("EDAINFACTIONS1 FAIL %s: exit %d; first error: %s" % [choice, code, _first_error(text)])
			failures += 1
		else:
			for line in text.split("\n"):
				if line.begins_with("EDAINFACTIONS1 %s:" % choice) and not line.contains(": ok"):
					print(line.strip_edges())
			print("EDAINFACTIONS1 PASS ", choice)
	print("EDAINFACTIONS1 RESULT: %d passed, %d failed" % [CHOICES.size() - failures, failures])
	quit(0 if failures == 0 else 1)


func _first_error(text: String) -> String:
	for line in text.split("\n"):
		if (line.begins_with("EDAINFACTIONS1 ") and line.contains(": FAIL")) or line.begins_with("GAME FAIL") or line.begins_with("SCRIPT ERROR"):
			return line.strip_edges()
	for line in text.split("\n"):
		if line.contains("FAIL") or line.contains("ERROR") or line.contains("Parse Error"):
			return line.strip_edges()
	return text.right(350).strip_edges()
