extends SceneTree

const GameFiles := preload("res://scripts/core/game_files.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")
const Updater := preload("res://scripts/core/updater.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")

class FakeHttp:
	extends RefCounted
	var cancelled := false

class FakeUpdater:
	extends RefCounted
	var http := FakeHttp.new()
	var messages := []

	func _report(text: String, done := -1, total := -1) -> void:
		messages.append({"text": text, "done": done, "total": total})

class FakeAio:
	extends RefCounted
	var packages := []
	var updater := FakeUpdater.new()


var _failures: Array[String] = []
var _checks := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	_checks += 1
	if not condition:
		_failures.append(message)
		print("VERIFYFILES fail: " + message)


func _write(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	_check(file != null, "file opens: " + path)
	if file != null:
		file.store_string(text)
		file.close()


func _remove_tree(path: String) -> void:
	var dir := DirAccess.open(path)
	if dir == null:
		return
	dir.include_hidden = true
	dir.include_navigational = false
	dir.list_dir_begin()
	while true:
		var name := dir.get_next()
		if name.is_empty():
			break
		var child := path.path_join(name)
		if dir.current_is_dir() and not dir.is_link(name):
			_remove_tree(child)
		else:
			DirAccess.remove_absolute(child)
	dir.list_dir_end()
	DirAccess.remove_absolute(path)


func _run() -> void:
	var root := OS.get_user_data_dir().path_join("verifyfiles-backend-" + str(Time.get_ticks_usec()))
	var origin := ""
	var pins_path := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--origin="):
			origin = arg.substr(9)
		elif arg.begins_with("--pins="):
			pins_path = arg.substr(7)
	_check(DirAccess.make_dir_recursive_absolute(root.path_join("LaNg")) == OK, "fixture created")
	_write(root.path_join("LaNg/English.big"), "english archive")
	_write(root.path_join("missing.txt"), "missing fixture")
	var changed_clean := "changed fixture!" if origin != "" else "changed fixture"
	_write(root.path_join("changed.txt"), changed_clean)
	var rows := [
		["lang/English.big", "english archive".md5_text(), 15],
		["missing.txt", "missing fixture".md5_text(), 15],
		["changed.txt", "changed fixture".md5_text(), 15],
	]
	var aio
	if origin != "":
		var updater := Updater.new(BuildInfo.load_info())
		aio = AioInstall.new(updater)
		_check(aio.set_test_origin(origin) == "", "test service origin accepted")
		_check(aio.set_test_pins(pins_path) == "", "test pins accepted")
	else:
		aio = FakeAio.new()
		aio.packages = [{"key": "rotwk", "guid": "fixture", "label": "Fixture", "language": "EN", "files": rows}]
	var files := GameFiles.new(aio)
	var corrupt := "--corrupt" in OS.get_cmdline_user_args()
	for character in ["%", "!", "^", "&", "|", "<", ">", "\"", "\r", "\n"]:
		_check(files._has_windows_shell_metacharacter(character), "Windows shell path character is rejected")
	_check(not files._has_windows_shell_metacharacter("/games/Rise of the Witch-king"),
		"ordinary Windows install path characters are allowed")
	_check(not files._has_windows_shell_metacharacter("C:/Program Files (x86)/Electronic Arts/RotWK"),
		"Program Files (x86) paths quoted by Godot are allowed")
	var clean := files.verify("rotwk", root)
	_check(clean.ok, "clean verification succeeds")
	_check(clean.good.size() == 3 and clean.missing.size() == 0 and clean.changed.size() == 0 and clean.unchecked.size() == 0,
		"clean counts are exact")
	_check("LaNg/English.big" in clean.good, "known file casing is preserved")
	_check(clean.repairable, "matching English archive proves repairability")
	_check(files.verify("rotwk", root + "/").ok, "trailing separator install roots are accepted")
	_write(root.path_join("changed.txt"), "changed fixture" if origin != "" else "changed fixture!")
	_check(DirAccess.remove_absolute(root.path_join("missing.txt")) == OK, "missing file removed")
	_write(root.path_join("my-mod.txt"), "player mod")
	_write(root.path_join(".hidden-mod"), "hidden player mod")
	var damaged := files.verify("rotwk", root)
	_check(damaged.ok, "damaged verification succeeds")
	_check(damaged.good.size() == 1 and damaged.missing.size() == 1 and damaged.changed.size() == 1 and damaged.unchecked.size() == 2,
		"damaged counts are exact")
	_check(damaged.missing[0] == "missing.txt" and damaged.changed[0] == "changed.txt" and
		damaged.unchecked == [".hidden-mod", "my-mod.txt"],
		"damaged paths are exact")
	var before := FileAccess.get_file_as_string(root.path_join("changed.txt"))
	var no_consent := files.repair(damaged, false)
	_check(not no_consent.ok and FileAccess.get_file_as_string(root.path_join("changed.txt")) == before,
		"repair without consent makes no mutation")
	if "--no-consent-only" in OS.get_cmdline_user_args():
		_check(not FileAccess.file_exists(root.path_join("missing.txt")), "no-consent repair leaves the missing file absent")
		_check(FileAccess.get_file_as_string(root.path_join("my-mod.txt")) == "player mod", "no-consent repair leaves extras untouched")
		_check(FileAccess.get_file_as_string(root.path_join(".hidden-mod")) == "hidden player mod",
			"no-consent repair leaves hidden extras untouched")
		_remove_tree(root)
		print("VERIFYFILES %s (%d checks)" % ["OK" if _failures.is_empty() else "FAILED", _checks])
		quit(1 if not _failures.is_empty() else 0)
		return
	_write(root.path_join("changed.txt"), "stale modification")
	var stale := files.repair(damaged, true)
	_check(not stale.ok and "changed since it was checked" in stale.error, "stale report is refused before repair")
	_write(root.path_join("changed.txt"), before)
	var refreshed := files.verify("rotwk", root)
	_check(refreshed.ok and refreshed.missing.size() == 1 and refreshed.changed.size() == 1,
		"restored files can be verified again")
	damaged = refreshed
	if origin != "":
		# Sol review: the repair takes the English pins the scan used, not the launcher's chosen download language (here one whose
		# pins drop an English file the install is missing)
		for p in aio.packages:
			if p.key == "rotwk":
				p["languages"] = {"DE": {"drop": ["missing.txt"], "add": []}}
		aio.text_language = "DE"
		aio.voice_language = "DE"
		var altered_report := damaged.duplicate(true)
		altered_report.missing = ["my-mod.txt"]
		altered_report.changed = []
		var repair := files.repair(altered_report, true)
		_check(repair.ok != corrupt, "service repair result")
		if corrupt:
			_check(not repair.ok and repair.error != "", "corrupt download returns an error")
			_check(FileAccess.get_file_as_string(root.path_join("changed.txt")) == before and not FileAccess.file_exists(root.path_join("missing.txt")),
				"corrupt download preserves originals")
		else:
			_check(FileAccess.get_file_as_string(root.path_join("changed.txt")) == "changed fixture!" and
				FileAccess.get_file_as_string(root.path_join("missing.txt")) == "missing fixture", "repair restores pinned files")
	_check(FileAccess.get_file_as_string(root.path_join("my-mod.txt")) == "player mod", "extra file stays untouched")
	_check(FileAccess.get_file_as_string(root.path_join(".hidden-mod")) == "hidden player mod", "hidden extra stays untouched")
	_check(DirAccess.remove_absolute(root.path_join("LaNg/English.big")) == OK, "language proof removed")
	var language := files.verify("rotwk", root)
	_check(language.ok and language.warning == "Only English game files can be repaired. This install may use another language; no files will be replaced.",
		"language warning is exact")
	_check(not language.repairable and language.good.size() == (2 if origin != "" and not corrupt else 0),
		"language warning prevents repair but keeps counts")
	var warning_repair := files.repair(language, true)
	_check(not warning_repair.ok and warning_repair.error == language.warning, "language warning blocks repair")
	var altered_language := language.duplicate(true)
	altered_language.repairable = true
	var altered_warning_repair := files.repair(altered_language, true)
	_check(not altered_warning_repair.ok and altered_warning_repair.error == language.warning,
		"caller changes cannot bypass language repair safety")
	var outside := root.get_base_dir().path_join("verifyfiles-link-target-" + str(Time.get_ticks_usec()))
	_check(DirAccess.make_dir_recursive_absolute(outside) == OK, "link target created")
	_write(outside.path_join("hidden.txt"), "outside file")
	var link_skip_reasons := PackedStringArray()
	var extra_link := DirAccess.open(root).create_link(outside, root.path_join("linked-mod"))
	if extra_link == OK:
		var linked := files.verify("rotwk", root)
		_check(linked.ok and "linked-mod" in linked.unchecked and not "linked-mod/hidden.txt" in linked.unchecked,
			"extra link is listed without following it")
		DirAccess.remove_absolute(root.path_join("linked-mod"))
	else:
		link_skip_reasons.append("extra link: " + error_string(extra_link))
	var known_link := DirAccess.open(root.path_join("LaNg")).create_link(outside.path_join("hidden.txt"),
		root.path_join("LaNg/English.big"))
	if known_link == OK:
		var refused_link := files.verify("rotwk", root)
		_check(not refused_link.ok and "link" in refused_link.error, "pinned file link is refused")
		DirAccess.remove_absolute(root.path_join("LaNg/English.big"))
	else:
		link_skip_reasons.append("pinned link: " + error_string(known_link))
	_remove_tree(outside)
	_remove_tree(root)
	if link_skip_reasons.is_empty():
		print("VERIFYFILES symlink checks: both executed")
	else:
		print("VERIFYFILES symlink checks skipped: " + "; ".join(link_skip_reasons))
	print("VERIFYFILES %s (%d checks)" % ["OK" if _failures.is_empty() else "FAILED", _checks])
	quit(1 if not _failures.is_empty() else 0)
