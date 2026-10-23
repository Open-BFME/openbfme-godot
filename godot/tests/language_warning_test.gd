## PR #77 (owner decision "load with warning"): the language warning box (scripts/release/language_warning.gd) with a simulated mount of an install
## in a language OpenBFME has not checked (the mount's "unverified_language", as GodotRetailFileSystem.cpp fills it). The language and file names are
## a hostile % sequence: the box, the copied details and the log lines must carry them as text. "Don't show this again" is kept per install and per
## set of files. Runs without the extension and without game files.
## godot --headless --path godot --script res://tests/language_warning_test.gd -- --settings=<a cfg path in a temp folder>
extends SceneTree

const LanguageWarning := preload("res://scripts/release/language_warning.gd")
const HOSTILE := "%s%s%n%x"

var failures := 0


func check(ok: bool, message: String) -> void:
	print("LANGUAGE WARNING ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1


func mount_of(rotwk: String, signature: String) -> Dictionary:
	var details := "OpenBFME language report\nOpenBFME test\nlanguage not checked: " + HOSTILE + "\n" \
		+ "rotwk lang/" + HOSTILE + ".big 1234 bytes md5 0123456789abcdef0123456789abcdef\n"
	return {
		"ok": true,
		"rotwk_install": rotwk,
		"bfme2_install": "/games/BFME2",
		"unverified_language": {
			"archives": [{"install": "rotwk", "path": "lang/" + HOSTILE + ".big", "language": HOSTILE, "md5": "0123456789abcdef0123456789abcdef", "size": 1234}],
			"message": "OpenBFME recognised your copies ... a language we have not checked yet: " + HOSTILE + ".\n\nin rare cases the game can crash.",
			"details": details,
			"signature": signature,
		},
	}


func _initialize() -> void:
	await process_frame # the root window is in the tree from the first frame on
	var settings := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--settings="):
			settings = a.trim_prefix("--settings=")
	if settings.is_empty():
		print("LANGUAGE WARNING FAIL no --settings=<path>")
		quit(1)
		return
	DirAccess.remove_absolute(settings)
	var mount := mount_of("/games/RotWK", "11111111111111111111111111111111")

	# a pinned install: no warning, nothing logged
	check(LanguageWarning.warning_of({"ok": true}).is_empty() and LanguageWarning.log_lines({"ok": true}).is_empty(), "a pinned install has no warning")

	# the log: the same details, each line as text
	var lines := LanguageWarning.log_lines(mount)
	check(lines.size() == 5, "five log lines: %d" % lines.size())
	check(lines[3] == "LANGUAGE language not checked: " + HOSTILE, "the language reaches the log as text: " + lines[3])
	check(lines[4] == "LANGUAGE rotwk lang/" + HOSTILE + ".big 1234 bytes md5 0123456789abcdef0123456789abcdef", "the file line reaches the log as text: " + lines[4])

	# the box: the engine's message as it is, Continue, Copy details, the check box
	check(not LanguageWarning.dismissed(mount, settings), "not dismissed at first")
	var box: AcceptDialog = LanguageWarning.make_dialog(mount, settings)
	root.add_child(box)
	box.popup_centered()
	await process_frame
	var message: Label = box.get_node("Content/Message")
	check(message.text == String(mount.unverified_language.message), "the box shows the message as text: " + message.text)
	check(message.text.contains("not checked yet: " + HOSTILE + "."), "the hostile language name is in the box unchanged")
	check(box.title == "Game language not checked yet" and box.ok_button_text == "Continue", "title and Continue: " + box.title)
	var copy: Button = null
	for b in box.find_children("*", "Button", true, false):
		if (b as Button).text == "Copy details":
			copy = b
	check(copy != null, "a Copy details button")
	box.custom_action.emit(&"copy_details")
	var copied: Label = box.get_node("Content/Copied")
	check(copied.text.begins_with("The details are on the clipboard"), "Copy details confirms: " + copied.text)
	if DisplayServer.has_feature(DisplayServer.FEATURE_CLIPBOARD):
		check(DisplayServer.clipboard_get() == String(mount.unverified_language.details), "the clipboard holds the details as text")
	else:
		print("LANGUAGE WARNING note: this display server has no clipboard (headless): the copy itself is not read back")
	var again: CheckBox = box.get_node("Content/DontShowAgain")
	check(again.text == "Don't show this again for this install", "the check box: " + again.text)

	# Continue without the check box: shown again next time
	box.confirmed.emit()
	await process_frame
	check(not is_instance_valid(box) or box.is_queued_for_deletion(), "Continue closes the box")
	check(not LanguageWarning.dismissed(mount, settings), "without the check box the warning shows again")

	# Continue with the check box: not again for this install and these files
	box = LanguageWarning.make_dialog(mount, settings)
	root.add_child(box)
	box.popup_centered()
	await process_frame
	(box.get_node("Content/DontShowAgain") as CheckBox).button_pressed = true
	box.confirmed.emit()
	await process_frame
	check(LanguageWarning.dismissed(mount, settings), "after 'Don't show this again' the warning is not shown for this install")
	check(not LanguageWarning.dismissed(mount_of("/games/RotWK", "22222222222222222222222222222222"), settings), "other files in the same folders show it again")
	check(not LanguageWarning.dismissed(mount_of("/games/Other RotWK", "11111111111111111111111111111111"), settings), "another install shows it")
	var cfg := ConfigFile.new()
	check(cfg.load(settings) == OK and cfg.get_section_keys(LanguageWarning.SECTION).size() == 1, "one install remembered in the settings file")

	# the box's script never formats a text with `%` (the names come from the disk)
	var source := FileAccess.get_file_as_string("res://scripts/release/language_warning.gd")
	var code := PackedStringArray()
	for line in source.split("\n"):
		var cut := line.find("#")
		code.push_back(line if cut < 0 else line.substr(0, cut))
	check(not "\n".join(code).contains("%"), "language_warning.gd has no % formatting")

	print("LANGUAGE WARNING ", "ALL PASS" if failures == 0 else "%d FAILED" % failures)
	quit(0 if failures == 0 else 1)
