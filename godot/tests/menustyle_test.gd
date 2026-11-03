## godot --headless --path godot --script res://tests/menustyle_test.gd
## Uses real retail archives, an isolated settings directory and the game-level switch checker.
extends SceneTree


func _initialize() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		printerr("MENUSTYLE SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
		quit(77)
		return
	var directory := OS.get_user_data_dir().path_join("menustyle-test-%d" % OS.get_process_id())
	DirAccess.make_dir_recursive_absolute(directory)
	var options := directory.path_join("Options.ini")
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), PackedStringArray([
		"--headless", "--path", ProjectSettings.globalize_path("res://"), "--",
		"--menustyle-check", "--options-file=" + options,
	]), output, true)
	var text: String = "".join(output)
	var failed := code != 0 or not text.contains("MENUSTYLE RESULT: 0 failed") or text.contains("SCRIPT ERROR") or text.contains("Parse Error")
	# lane DEVPORT-8: the restart: a new process on the same Options.ini boots in the style the first run chose last (bfme2)
	var output2: Array = []
	var code2 := OS.execute(OS.get_executable_path(), PackedStringArray([
		"--headless", "--path", ProjectSettings.globalize_path("res://"), "--",
		"--menustyle-check", "--menustyle-expect=bfme2", "--options-file=" + options,
	]), output2, true)
	var text2: String = "".join(output2)
	var failed2 := code2 != 0 or not text2.contains("MENUSTYLE RESULT: 0 failed") or not text2.contains("after a restart the menu is bfme2") or text2.contains("SCRIPT ERROR") or text2.contains("Parse Error")
	for line in text2.split("\n"):
		if line.begins_with("MENUSTYLE ") or line.contains("SCRIPT ERROR") or line.contains("Parse Error") or line.begins_with("GAME FAILED"):
			print("restart: ", line)
	print("MENUSTYLE restart ", "FAIL" if failed2 else "PASS", " exit=", code2)
	failed = failed or failed2
	# lane STABLEQA-1: --stable-channel (a Stable build's menus): both styles without War of the Ring (bfme2: the Options.ini above; rotwk: a new one)
	for style in ["bfme2", "rotwk"]:
		var stable_options := options if style == "bfme2" else directory.path_join("Options-stable.ini")
		var output3: Array = []
		var code3 := OS.execute(OS.get_executable_path(), PackedStringArray([
			"--headless", "--path", ProjectSettings.globalize_path("res://"), "--",
			"--menustyle-check", "--menustyle-expect=" + style, "--stable-channel", "--options-file=" + stable_options,
		]), output3, true)
		var text3: String = "".join(output3)
		var failed3 := code3 != 0 or not text3.contains("MENUSTYLE RESULT: 0 failed") or not text3.contains("War of the Ring offered: false") \
			or text3.contains("SCRIPT ERROR") or text3.contains("Parse Error")
		for line in text3.split("\n"):
			if line.begins_with("MENUSTYLE ") or line.contains("SCRIPT ERROR") or line.contains("Parse Error") or line.begins_with("GAME FAILED"):
				print("stable ", style, ": ", line)
		print("MENUSTYLE stable ", style, " ", "FAIL" if failed3 else "PASS", " exit=", code3)
		failed = failed or failed3
	for line in text.split("\n"):
		if line.begins_with("MENUSTYLE ") or line.contains("SCRIPT ERROR") or line.contains("Parse Error") or line.begins_with("GAME FAILED"):
			print(line)
	for file in DirAccess.get_files_at(directory):
		DirAccess.remove_absolute(directory.path_join(file))
	DirAccess.remove_absolute(directory)
	print("MENUSTYLE integration ", "FAIL" if failed else "PASS", " exit=", code)
	quit(1 if failed else 0)
