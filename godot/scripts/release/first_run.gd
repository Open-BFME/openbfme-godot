## Lane RELEASE-1: the first-run screen. OpenBFME runs on the player's own copies of The Rise of the Witch-king 2.01 and The Battle for
## Middle-earth II 1.06; this screen finds them (InstallSetup.discover: the registry keys retail and the community installers write, Wine / Proton
## prefixes on Linux, the installers' default folders), lets the player confirm or choose the two folders, checks them (every 2.01 / 1.06 archive
## present with its size; a clear message naming what is wrong) and, once the full mount (every archive's md5) succeeded, remembers them in
## user://install-paths.cfg. Nothing found is never silently replaced by a guess: the player always confirms.
##
## Shown by Release.mount_retail (scripts/release/release.gd) before the retail shell exists, so it is plain Godot UI, not an Apt movie
## (UI-2's menus start after it). run() returns the mount report; {ok: false, quit: true} when the player quits.
##
## Test hooks (the fresh-user test, godot/tests/first_run_test.gd), also headless: test_pick {accept: true} takes the first discovered folders
## that pass the check; {rotwk, bfme2} picks those folders. Every step prints a FIRST RUN line; a rejected pick ends the run with ok false.
## --first-run-shot=FILE (windowed): save the screen as it first appears to FILE and quit (the media, the layout check).
extends CanvasLayer

signal finished(result: Dictionary)

var _fs: RefCounted
var _setup: RefCounted
var _found: Array = []
var _rotwk_edit: LineEdit
var _bfme2_edit: LineEdit
var _rotwk_found: OptionButton
var _bfme2_found: OptionButton
var _status: RichTextLabel
var _start: Button
var _dialog: FileDialog
var _dialog_target: LineEdit
var _checked_ok := false


func run(fs: RefCounted, setup: RefCounted, problem: String, test_pick: Dictionary) -> Dictionary:
	_fs = fs
	_setup = setup
	layer = 90
	_found = setup.discover()
	for c in _found:
		print(Release.info.redact("FIRST RUN found %s: %s (%s) %s" % [c.game, c.path, c.source, "ok" if c.ok else "rejected: " + "; ".join(c.errors)]))
	if DisplayServer.get_name() != "headless":
		_build_ui(problem)
	if not test_pick.is_empty():
		return await _run_test(test_pick)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--first-run-shot=") and DisplayServer.get_name() != "headless":
			for i in 20:
				await get_tree().process_frame
			var shot := arg.substr(17)
			get_viewport().get_texture().get_image().save_png(shot)
			print("FIRST RUN screenshot: ", shot)
			return {"ok": false, "quit": true, "errors": ["screenshot taken"]}
	return await finished


func _run_test(pick: Dictionary) -> Dictionary:
	var rotwk: String = pick.get("rotwk", "")
	var bfme2: String = pick.get("bfme2", "")
	if pick.get("accept", false):
		rotwk = _first_ok("rotwk")
		bfme2 = _first_ok("bfme2")
		if rotwk.is_empty() or bfme2.is_empty():
			print("FIRST RUN nothing to accept: rotwk '%s', bfme2 '%s'" % [Release.info.redact(rotwk), Release.info.redact(bfme2)])
			return {"ok": false, "errors": ["the first-run test found no usable game folders to accept"]}
	var checked: Dictionary = _setup.check(rotwk, bfme2)
	print("FIRST RUN check: ok %s" % str(checked.ok))
	for e in checked.errors:
		print("FIRST RUN rejected: ", e)
	if not checked.ok:
		return {"ok": false, "errors": Array(checked.errors)}
	return _mount_and_remember(rotwk, bfme2)


func _first_ok(game: String) -> String:
	for c in _found:
		if c.game == game and c.ok:
			return c.path
	return ""


func _mount_and_remember(rotwk: String, bfme2: String) -> Dictionary:
	var mount: Dictionary = _fs.mount_retail_paths(rotwk, bfme2)
	print("FIRST RUN mount: ok %s" % str(mount.ok))
	if not mount.ok:
		for e in mount.errors:
			print("FIRST RUN mount error: ", e)
		return mount
	var saved: Dictionary = _setup.remember(rotwk, bfme2)
	print("FIRST RUN remembered: %s %s" % [str(saved.ok), saved.error])
	if not saved.ok:
		return {"ok": false, "errors": ["the game folders work but could not be saved: " + saved.error]}
	return mount


# ---- the screen ----------------------------------------------------------------------------------------------------------------------------

func _build_ui(problem: String) -> void:
	var back := ColorRect.new()
	back.color = Color(0.06, 0.06, 0.07)
	back.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(back)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	add_child(margin)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 14)
	margin.add_child(box)

	var title := Label.new()
	title.text = "OpenBFME - choose your game folders"
	title.add_theme_font_size_override("font_size", 28)
	box.add_child(title)
	var intro := Label.new()
	intro.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	intro.text = ("OpenBFME plays on your own copy of the game: The Rise of the Witch-king patched to 2.01 and The Battle for Middle-earth II "
		+ "patched to 1.06 (English). They stay where they are; OpenBFME only reads them and never changes them.\n"
		+ "Choose the two folders that hold the games' .big files. Folders found on this computer are offered below.")
	box.add_child(intro)
	if not problem.is_empty():
		var warn := Label.new()
		warn.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		warn.add_theme_color_override("font_color", Color(1, 0.6, 0.4))
		warn.text = problem
		box.add_child(warn)

	var rows := [["The Rise of the Witch-king 2.01", "rotwk"], ["The Battle for Middle-earth II 1.06", "bfme2"]]
	for row in rows:
		var label := Label.new()
		label.text = row[0]
		label.add_theme_font_size_override("font_size", 18)
		box.add_child(label)
		var found := OptionButton.new()
		found.fit_to_longest_item = false # a long path must not push the screen wider than the window
		found.clip_text = true
		found.add_item("Found on this computer: choose one" if _count(row[1]) > 0 else "Not found automatically: use Browse")
		found.set_item_disabled(0, true)
		for c in _found:
			if c.game == row[1]:
				found.add_item(("OK   " if c.ok else "not usable   ") + c.path)
				found.set_item_metadata(found.item_count - 1, c.path)
				found.set_item_tooltip(found.item_count - 1, c.source + ("" if c.ok else "\n" + "\n".join(c.errors)))
		box.add_child(found)
		var line := HBoxContainer.new()
		var edit := LineEdit.new()
		edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		edit.placeholder_text = "folder of " + row[0]
		var browse := Button.new()
		browse.text = "Browse..."
		line.add_child(edit)
		line.add_child(browse)
		box.add_child(line)
		# checked when the player is done typing (Enter, leaving the field), not per key: a half-typed "/" is not worth a folder walk
		edit.text_changed.connect(func(_t): _start.disabled = true)
		edit.text_submitted.connect(func(_t): _recheck())
		edit.focus_exited.connect(_recheck)
		browse.pressed.connect(_browse.bind(edit, row[0]))
		found.item_selected.connect(func(i): edit.text = str(found.get_item_metadata(i)); _recheck())
		if row[1] == "rotwk":
			_rotwk_edit = edit
			_rotwk_found = found
		else:
			_bfme2_edit = edit
			_bfme2_found = found
		# preselect the first usable folder found
		for i in range(1, found.item_count):
			if str(found.get_item_text(i)).begins_with("OK"):
				found.select(i)
				edit.text = str(found.get_item_metadata(i))
				break

	_status = RichTextLabel.new()
	_status.bbcode_enabled = true
	_status.fit_content = true
	_status.scroll_active = false
	_status.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_status.selection_enabled = true
	box.add_child(_status)

	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	var quit := Button.new()
	quit.text = "Quit"
	quit.pressed.connect(func(): finished.emit({"ok": false, "quit": true, "errors": ["the player quit on the first-run screen"]}))
	_start = Button.new()
	_start.text = "Use these folders"
	_start.pressed.connect(_on_start)
	buttons.add_child(quit)
	buttons.add_child(_start)
	box.add_child(buttons)
	var version := Label.new()
	version.text = Release.version_line()
	version.add_theme_color_override("font_color", Color(1, 1, 1, 0.5))
	box.add_child(version)

	_dialog = FileDialog.new()
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.use_native_dialog = true
	_dialog.dir_selected.connect(func(d): _dialog_target.text = d; _recheck())
	add_child(_dialog)
	_recheck()


func _count(game: String) -> int:
	var n := 0
	for c in _found:
		if c.game == game:
			n += 1
	return n


func _browse(edit: LineEdit, what: String) -> void:
	_dialog_target = edit
	_dialog.title = "Folder of " + what
	if not edit.text.is_empty():
		_dialog.current_dir = edit.text
	_dialog.popup_centered_ratio(0.7)


func _recheck() -> void:
	var r: Dictionary = _setup.check(_rotwk_edit.text.strip_edges(), _bfme2_edit.text.strip_edges())
	_checked_ok = r.ok
	_start.disabled = not r.ok
	var t := ""
	for game in [["rotwk", "The Rise of the Witch-king"], ["bfme2", "The Battle for Middle-earth II"]]:
		var g: Dictionary = r[game[0]]
		if g.ok:
			t += "[color=#8f8]%s: found, %d of %d archives[/color]\n" % [game[1], g.present, g.expected]
		else:
			for e in g.errors:
				t += "[color=#f98]%s: %s[/color]\n" % [game[1], e]
	_status.text = t


func _on_start() -> void:
	if not _checked_ok:
		return
	_start.disabled = true
	_status.text = "Checking every game file (the first time this can take a minute)..."
	await get_tree().process_frame
	await get_tree().process_frame
	var mount := _mount_and_remember(_rotwk_edit.text.strip_edges(), _bfme2_edit.text.strip_edges())
	if mount.ok:
		finished.emit(mount)
		return
	var t := "[color=#f98]The folders could not be used:[/color]\n"
	for e in mount.errors:
		t += "[color=#f98]%s[/color]\n" % e
	_status.text = t
	_start.disabled = false
