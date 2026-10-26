## Runs in an isolated project with a Release stub; exercises the actual first-run UI callbacks without retail policies or bytes.
extends SceneTree

var checks := 0
var failed := 0

class Setup extends RefCounted:
	var checked: Array = []
	var saved: Array = []
	var config := ""
	func check(r: String, b: String) -> Dictionary:
		checked = [r, b]
		var good := {"ok": true, "present": 1, "expected": 1, "errors": []}
		return {"ok": true, "rotwk": good, "bfme2": good, "errors": []}
	func remember(r: String, b: String) -> Dictionary:
		saved = [r, b]
		var f := FileAccess.open(config, FileAccess.WRITE)
		if f == null:
			return {"ok": false, "error": "cannot write " + config}
		f.store_string("ROTWK_INSTALL=" + r + "\nBFME2_INSTALL=" + b + "\n")
		f.close()
		return {"ok": true, "error": ""}

class Mount extends RefCounted:
	var mounted: Array = []
	func mount_retail_paths(r: String, b: String) -> Dictionary:
		mounted = [r, b]
		return {"ok": true, "errors": []}

func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("PATH FAIL ", label)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var first_run: Script = load("res://first_run.gd")
	var cases: Array = JSON.parse_string(FileAccess.get_file_as_string(OS.get_environment("OPENBFME_PATH_CASES")))
	for item in cases:
		var folder: String = item.folder
		var second: String = item.second
		var ui = first_run.new()
		root.add_child(ui)
		ui._rotwk_edit = LineEdit.new()
		ui._bfme2_edit = LineEdit.new()
		ui._status = RichTextLabel.new()
		ui._start = Button.new()
		for node in [ui._rotwk_edit, ui._bfme2_edit, ui._status, ui._start]:
			ui.add_child(node)
		ui._rotwk_edit.text = folder
		ui._bfme2_edit.text = second
		var setup := Setup.new()
		setup.config = item.user_dir.path_join("install-paths.cfg")
		var mount := Mount.new()
		ui._setup = setup
		ui._fs = mount
		ui._recheck()
		check(setup.checked == [folder, second], "typed or selected paths reach check unchanged")
		check(not ui._start.disabled, "a usable synthetic folder enables Start")
		ui._on_start()
		var result: Dictionary = await ui.finished
		check(result.ok, "Start accepts the folder")
		check(mount.mounted == [folder, second], "Start mounts exactly the checked folders")
		check(setup.saved == [folder, second], "Start remembers exactly the mounted folders")
		check(FileAccess.get_file_as_string(setup.config) == "ROTWK_INSTALL=" + folder + "\nBFME2_INSTALL=" + second + "\n", "remembered text preserves all bytes")
		ui.queue_free()
		await process_frame
	print("PATH FIRST-RUN checks=", checks, " failed=", failed, " cases=", cases.size())
	quit(0 if failed == 0 else 1)
