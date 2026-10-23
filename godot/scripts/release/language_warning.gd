## PR #77 (owner decision "load with warning"): an install whose language archives no pin covers (engine/data/retail-archives/language-archives.json)
## is mounted, and the player is told so once per install: what was recognised, what an unchecked language can mean, what is true for online play
## (ProfileIdentity lists every mounted archive with its md5: only players with the same files can play together) and how to help ("Copy details").
## The texts come from the engine (Common/RetailArchivePolicy.h RetailLanguageWarningText / Details / Signature, through the mount's
## "unverified_language"); this script only shows, copies and logs them. Language and file names come from the disk: they are put into Labels, the
## clipboard and print() as they are and never used as a format string (no `%` with them on the left).
##
## "Don't show this again for this install" is stored in user://language-warning.cfg, section [dont_show_again]: the key is the md5 of the two
## install folders, the value the signature of the archives (their names, sizes and md5s): other files in the same folders show the warning again.
extends RefCounted

const SETTINGS := "user://language-warning.cfg"
const SECTION := "dont_show_again"
const TITLE := "Game language not checked yet"
const COPY_ACTION := "copy_details"


## the mount's warning ({} when every language archive was pinned)
static func warning_of(mount: Dictionary) -> Dictionary:
	var w: Variant = mount.get("unverified_language", {})
	return w if w is Dictionary else {}


## the key of the install the warning belongs to (both folders: the mount checks them together)
static func install_key(mount: Dictionary) -> String:
	return (String(mount.get("rotwk_install", "")) + "\n" + String(mount.get("bfme2_install", ""))).md5_text()


## the lines the log gets (the same details "Copy details" copies)
static func log_lines(mount: Dictionary) -> PackedStringArray:
	var w := warning_of(mount)
	var out := PackedStringArray()
	if w.is_empty():
		return out
	out.push_back("LANGUAGE this install's language has not been checked by OpenBFME: its language archives were mounted unverified")
	for line in String(w.get("details", "")).split("\n", false):
		out.push_back("LANGUAGE " + line)
	return out


## true when the player chose "Don't show this again" for these folders and these files
static func dismissed(mount: Dictionary, settings_path: String = SETTINGS) -> bool:
	var w := warning_of(mount)
	if w.is_empty():
		return true
	var cfg := ConfigFile.new()
	if cfg.load(settings_path) != OK:
		return false
	return String(cfg.get_value(SECTION, install_key(mount), "")) == String(w.get("signature", ""))


## remembers "Don't show this again" for these folders and these files
static func dismiss(mount: Dictionary, settings_path: String = SETTINGS) -> Error:
	var cfg := ConfigFile.new()
	var err := cfg.load(settings_path)
	if err != OK and err != ERR_FILE_NOT_FOUND:
		return err  # an unreadable settings file is not overwritten
	cfg.set_value(SECTION, install_key(mount), String(warning_of(mount).get("signature", "")))
	return cfg.save(settings_path)


## The message box (an AcceptDialog: Continue, Copy details, the "Don't show this again" check box). It emits `tree_exiting` when closed (it
## frees itself); the check box's choice is saved to `settings_path` first.
static func make_dialog(mount: Dictionary, settings_path: String = SETTINGS) -> AcceptDialog:
	var w := warning_of(mount)
	var box := AcceptDialog.new()
	box.name = "LanguageWarning"
	box.title = TITLE
	box.exclusive = true
	box.process_mode = Node.PROCESS_MODE_ALWAYS
	box.min_size = Vector2i(760, 0)
	box.ok_button_text = "Continue"
	var content := VBoxContainer.new()
	content.name = "Content"
	content.add_theme_constant_override("separation", 12)
	var text := Label.new()
	text.name = "Message"
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	text.custom_minimum_size = Vector2(720, 0)
	text.text = String(w.get("message", ""))
	content.add_child(text)
	var again := CheckBox.new()
	again.name = "DontShowAgain"
	again.text = "Don't show this again for this install"
	content.add_child(again)
	var copied := Label.new()
	copied.name = "Copied"
	copied.text = ""
	content.add_child(copied)
	box.add_child(content)
	var copy := box.add_button("Copy details", false, COPY_ACTION)
	copy.name = "CopyDetails"
	var details := String(w.get("details", ""))
	box.custom_action.connect(func(action: StringName) -> void:
		if action == COPY_ACTION:
			DisplayServer.clipboard_set(details)
			copied.text = "The details are on the clipboard: paste them into your post."
			print("LANGUAGE the player copied the language details"))
	var done := func() -> void:
		if again.button_pressed:
			var err := dismiss(mount, settings_path)
			if err == OK:
				print("LANGUAGE the warning will not be shown again for this install (", ProjectSettings.globalize_path(settings_path).get_file(), ")")
			else:
				printerr("LANGUAGE could not save the choice: ", error_string(err))
		box.queue_free()
	box.confirmed.connect(done)
	box.canceled.connect(done)
	return box
