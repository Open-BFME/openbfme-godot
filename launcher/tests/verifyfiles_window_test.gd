extends SceneTree

const MainScene := preload("res://scenes/main.tscn")

var _failures := PackedStringArray()
var _checks := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures.append(what)
		print("VERIFYFILES WINDOW fail: " + what)


func _write(path: String, value: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	_check(file != null, "fixture file opens")
	if file != null:
		file.store_string(value)
		file.close()


func _done(main) -> bool:
	var deadline := Time.get_ticks_msec() + 10000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if not main._busy() and main._job_kind == "":
			return true
	return false


func _run() -> void:
	if not "--offline" in OS.get_cmdline_user_args():
		_check(false, "requires --offline and an isolated user directory")
		quit(1)
		return
	var main = MainScene.instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	var dir := OS.get_user_data_dir().path_join("verify-window-fixture")
	_check(DirAccess.make_dir_recursive_absolute(dir.path_join("lang")) == OK, "fixture folder created")
	var rows := []
	for path in ["lang/English.big", "missing.txt", "changed.txt"]:
		_write(dir.path_join(path), "synthetic fixture only")
		rows.append([path, "synthetic fixture only".md5_text(), 22])
	main.aio.packages = [{"key": "rotwk", "guid": "original-RotWK", "label": "The Rise of the Witch-king 2.01", "language": "EN", "files": rows}]
	main._show_screen("games")
	main._games_screen.set_folders({"found": true, "rotwk": dir, "bfme2": "", "source": "env"})
	_check(main._games_screen._verify_buttons.size() == 1, "one detected install has a Verify game files button")
	main._games_screen._verify_buttons[0].pressed.emit()
	_check(main._verify_panel.visible, "verification opens the progress dialog")
	_check(await _done(main), "clean verification finishes")
	_check(main._verify_panel._summary.text.contains("3 files OK / 0 missing / 0 changed / 0 not checked"), "clean counts")
	_check(not main._verify_panel._repair.visible, "clean install has no repair action")
	main._verify_panel._on_close()
	_check(not main._verify_panel.visible, "close dismisses report")
	_check(DirAccess.remove_absolute(dir.path_join("missing.txt")) == OK, "missing fixture prepared")
	_write(dir.path_join("changed.txt"), "changed synthetic data")
	_write(dir.path_join("my-mod.txt"), "leave my mod alone")
	main._games_screen._verify_buttons[0].pressed.emit()
	_check(await _done(main), "damaged verification finishes")
	_check(main._verify_panel._summary.text.contains("1 files OK / 1 missing / 1 changed / 1 not checked"), "damaged and extra counts")
	_check(main._verify_panel._files.text.contains("my-mod.txt"), "extra file named in report")
	_check(main._verify_panel._repair.visible, "repair offered for known English install")
	main._verify_panel._repair.pressed.emit()
	_check(main._verify_panel._repair.text == "Confirm repair", "repair requires a second explicit confirmation")
	_check(not main._busy(), "first Repair click does not start a download")
	_check(FileAccess.get_file_as_string(dir.path_join("changed.txt")) == "changed synthetic data", "unconfirmed original untouched")
	_check(not main._verify_panel._files.text.contains("my-mod.txt"), "extra file excluded from confirmation")
	main._verify_panel._close.pressed.emit()
	_check(main._verify_panel._repair.text == "Repair…", "Back returns to full report")
	_check(FileAccess.get_file_as_string(dir.path_join("my-mod.txt")) == "leave my mod alone", "mod untouched")
	print("VERIFYFILES WINDOW %s (%d checks)" % ["OK" if _failures.is_empty() else "FAILED", _checks])
	if "--hold" in OS.get_cmdline_user_args() and _failures.is_empty():
		return
	main.queue_free()
	await process_frame
	quit(0 if _failures.is_empty() else 1)
