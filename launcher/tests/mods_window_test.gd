## Lane MODS-3: the Mods screen in the real window (scripts/main.gd, headless, offline), driven the way a player does it: Load the list,
## Install (the question before the first mod download, then none), Pause / Resume, Play with, Remove. Run by tools/release/test_mods.py
## (test_mods_window) against its stand-in service with the made-up catalogue:
##
##   godot --headless --path launcher --script res://tests/mods_window_test.gd -- --offline --aio-base=<stand-in>
##
## Prints "MODS WINDOW fail: ..." lines and "MODS WINDOW OK" when every check passed; exit 0 only then.
extends SceneTree

const MainScene := preload("res://scenes/main.tscn")
const ModCatalog := preload("res://scripts/core/mod_catalog.gd")

var _failures := PackedStringArray()
var _checks := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures.append(what)
		print("MODS WINDOW fail: " + what)


func _frames(n: int) -> void:
	for i in n:
		await process_frame


## waits for the running job to end (at most 120 s); false when it did not
func _job_done(main) -> bool:
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 120000:
		await process_frame
		if not main._busy() and main._job_kind == "":
			await _frames(2)
			return true
	return false


## the texts of every Label and Button below `node`
static func _texts(node: Node) -> PackedStringArray:
	var out := PackedStringArray()
	for c in node.find_children("*", "", true, false):
		if c is Label or c is Button:
			out.append(c.text)
	return out


func _run() -> void:
	if not "--offline" in OS.get_cmdline_user_args():
		_check(false, "start it with -- --offline --aio-base=<stand-in>")
		_end()
		return
	var main = MainScene.instantiate()
	root.add_child(main)
	await _frames(3)
	main._show_screen("mods")
	await _frames(2)
	var screen = main._mods
	_check(screen._view.is_empty() and screen._refresh.text == "Load the list", "nothing is listed before Load the list")
	_check("Load the list to see the mods you can install." in _texts(screen._list), "the list says how to load it")

	# 1. Load the list
	screen._refresh.pressed.emit()
	_check(await _job_done(main), "the list was read")
	_check(screen._view.get("packages", []).size() == 5 and screen._refresh.text == "Refresh", "five packages are listed: " + str(screen._view.keys()))
	var shown := _texts(screen._list)
	for t in ["Example Realms", "Made-Up Patch 9.9", "Sample Siege", "Tiny Tales", "Broken Bridge", "Zoom For The Original",
			"Realms No Intro 1.0.0 (ready)", "Realms Big Font 1.0.1 (ready)", "Siege Timer 1.0.0 (needs engine features)"]:
		_check(t in shown, "the list shows '%s'" % t)
	_check(Array(shown).any(func(t): return t.begins_with("Needs engine features:") and "Made-Up Patch 9.9: lotrbfme2ep1.exe" in t), "the patch is labelled with its engine edits")
	_check(Array(shown).any(func(t): return t.begins_with("Cannot be installed: The service's package test-broken")), "the refused package says why")

	# 2. Install with an enhancement: the question first (nothing downloads), then the download; Play starts it
	screen._chosen["test-realms"] = {"test-realms-nointro": true}
	screen.install.emit("test-realms", screen.chosen("test-realms"))
	await _frames(2)
	_check(main._message.visible and main._message._title.text == "Download mods from bfmeladder.com?", "the question before the first mod download")
	_check("Example Realms 1.2.0 is about" in main._message._text.text and "not by EA or OpenBFME" in main._message._text.text,
		"the question names the package, its size and the source: " + main._message._text.text)
	_check(not main._busy() and not DirAccess.dir_exists_absolute(main.mods.dir.path_join("test-realms")), "nothing downloads before the answer")
	main._message._ok.pressed.emit()
	await _frames(1)
	_check(main._job_kind == "mod" and main._play_blocked(), "Play waits while the mod downloads")
	_check(await _job_done(main), "the mod download ended")
	_check(main._status.text == "Example Realms 1.2.0 is installed. Play starts it.", "status: " + main._status.text)
	_check(main.mods.active() == {"guid": "test-realms", "enhancements": ["test-realms-nointro"]}, "the installed mod is picked: %s" % main.mods.active())
	var launch: Dictionary = main.updater.game_arguments()
	_check(launch.ok and "--mod-install=" + main.mods.dir.path_join("test-realms/install") in launch.args, "Play passes the mod: %s" % launch)
	_check(screen._pick.get_item_text(screen._pick.selected) == "Example Realms 1.2.0", "Play with shows it")
	_check(screen._pick_note.text.begins_with("Ready: Play starts The Rise of the Witch-king with Example Realms 1.2.0 and Realms No Intro"), "its note: " + screen._pick_note.text)
	_check("Installed" in _texts(screen._list), "its row says Installed")
	# ticking another enhancement offers Apply and says it is not installed yet
	screen._chosen["test-realms"]["test-realms-font"] = true
	screen._show()
	_check("Apply" in _texts(screen._list) and "press Apply" in screen._pick_note.text, "a ticked enhancement offers Apply")

	# 3. a mod that needs engine features: no question any more; labelled plainly; paused and resumed
	main.mods.test_stop_after = "install/__siege.big:pause"
	screen.install.emit("test-siege", [])
	await _frames(1)
	_check(not main._message.visible or main._message._title.text != "Download mods from bfmeladder.com?", "the question is asked only once")
	_check(await _job_done(main), "the paused download ended")
	main.mods.test_stop_after = ""
	_check(main._paused == "mod" and screen._pause.text == "Resume", "the mod download is paused (%s)" % main._paused)
	_check(main.mods.partials().has("test-siege"), "the unfinished download is listed")
	_check(main.mods.active().guid == "test-realms", "a paused download is not picked")
	main._on_pause()
	_check(await _job_done(main), "the resumed download ended")
	_check(main._status.text == "Sample Siege 0.3 is installed. Play starts it. It needs engine features that are not in OpenBFME yet: the game may differ or fail.", "status: " + main._status.text)
	_check(screen._pick_note.text.begins_with("Needs engine features:") and "Sample Siege: d3d9.dll" in screen._pick_note.text, "its note: " + screen._pick_note.text)

	# 4. Play with: the original game
	screen.pick.emit("")
	await _frames(1)
	_check(main.mods.active().guid == "" and not Array(main.updater.game_arguments().args).any(func(a): return a.begins_with("--mod")), "none: no mod arguments")
	_check(screen._pick.selected == 0 and screen._pick_note.text == "Play starts The Rise of the Witch-king 2.01 as it is.", "Play with shows the original game")

	# 5. Remove: a question, then only that folder
	main._on_mod_remove("test-siege")
	_check(main._message.visible and main._message._title.text == "Remove Sample Siege?", "Remove asks first")
	main._message._ok.pressed.emit()
	await _frames(1)
	_check(not DirAccess.dir_exists_absolute(main.mods.dir.path_join("test-siege")), "its folder is gone")
	_check(DirAccess.dir_exists_absolute(main.mods.dir.path_join("test-realms/install")), "the other mod stays")
	_check(main._status.text == "Sample Siege was removed.", "status: " + main._status.text)
	main.queue_free()
	await _frames(2)
	_end()


func _end() -> void:
	if _checks == 0:
		_failures.append("no check ran")
	print("MODS WINDOW OK (%d checks)" % _checks if _failures.is_empty() else "MODS WINDOW FAILED: %d of %d checks" % [_failures.size(), _checks])
	quit(0 if _failures.is_empty() else 1)
