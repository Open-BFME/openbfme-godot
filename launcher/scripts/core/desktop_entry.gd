## Lane OWNERFIX-3: the OpenBFME entry of the Linux / Steam Deck desktop menus, with the OpenBFME icon (the ring), written when the player asks for it
## (--install-desktop-entry; nothing is written otherwise):
##   <data home>/icons/hicolor/256x256/apps/openbfme.png   the icon (scripts/ui/app_icon.gd's 256 x 256 PNG, the same bytes as the window icon)
##   <data home>/applications/openbfme.desktop              Name=OpenBFME, Exec=<this launcher>, Icon=openbfme
## <data home> is $XDG_DATA_HOME, else ~/.local/share (the XDG base directory rule the URL handler's entry follows too).
extends RefCounted

const AppIcon := preload("res://scripts/ui/app_icon.gd")
const DESKTOP_FILE := "openbfme.desktop"
const ICON_NAME := "openbfme"


## the entry's text for the launcher at `exe` (the path quoted: the Desktop Entry spec's Exec quoting; a quote or backslash in it is escaped)
static func text(exe: String) -> String:
	var quoted := exe.replace("\\", "\\\\\\\\").replace("\"", "\\\\\"").replace("`", "\\\\`").replace("$", "\\\\$")
	return "[Desktop Entry]\nType=Application\nName=OpenBFME\nComment=The Battle for Middle-earth II: The Rise of the Witch-king, rebuilt (plays your own game files)\nExec=\"%s\"\nIcon=%s\nTerminal=false\nCategories=Game;StrategyGame;\n" % [quoted, ICON_NAME]


static func data_home() -> String:
	var home := OS.get_environment("XDG_DATA_HOME")
	return home if home != "" else OS.get_environment("HOME").path_join(".local/share")


## writes the icon and the entry under `home`; {ok, error, where}
static func install(exe: String, home: String) -> Dictionary:
	if home == "" or exe == "":
		return {"ok": false, "error": "no data folder or no launcher path"}
	var icon_dir := home.path_join("icons/hicolor/256x256/apps")
	var app_dir := home.path_join("applications")
	for d in [icon_dir, app_dir]:
		var e := DirAccess.make_dir_recursive_absolute(d)
		if e != OK and not DirAccess.dir_exists_absolute(d):
			return {"ok": false, "error": "cannot make %s: %s" % [d, error_string(e)]}
	var png := Marshalls.base64_to_raw(AppIcon.PNG_BASE64)
	if AppIcon.image() == null:
		return {"ok": false, "error": "the launcher's icon does not decode"}
	var writes := {icon_dir.path_join(ICON_NAME + ".png"): png, app_dir.path_join(DESKTOP_FILE): text(exe).to_utf8_buffer()}
	for path in writes:
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f == null:
			return {"ok": false, "error": "cannot write %s: %s" % [path, error_string(FileAccess.get_open_error())]}
		var stored: bool = f.store_buffer(writes[path])
		f.flush()
		var err: Error = f.get_error()
		f.close()
		if not stored or err != OK:
			return {"ok": false, "error": "cannot write %s: %s" % [path, error_string(err) if err != OK else "short write"]}
	return {"ok": true, "error": "", "where": app_dir.path_join(DESKTOP_FILE)}
