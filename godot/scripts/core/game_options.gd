## Lane INPUT-1: the game's Options.ini as the launcher's settings see it. OpenBFME keeps the player's Options.ini in its own user data folder
## (Godot's app_userdata/OpenBFME, see the testers' README); the game reads it at start (GameClient/OptionPreferences: "Key = Value" lines; a key
## the game does not know is kept when the in-game Options screen saves). The launcher only edits the OpenBFME keys it offers, every other line is
## left as it is.
##   OpenBFMEFreeCamera = yes   the free camera: the zoom-out limit is the map's extent instead of retail's (lane PLAY-1, game.gd _free_camera_setting)
extends RefCounted

const FREE_CAMERA_KEY := "OpenBFMEFreeCamera"


## The game's user data folder. Godot puts a project without a custom folder under <data dir>/godot/app_userdata/<name> on Linux and
## <data dir>/Godot/app_userdata/<name> on Windows and macOS. OPENBFME_GAME_USER_DIR overrides it (tests, portable setups).
static func game_user_dir() -> String:
	var forced := OS.get_environment("OPENBFME_GAME_USER_DIR")
	if forced != "":
		return forced
	var godot_dir := "godot" if OS.get_name() in ["Linux", "FreeBSD", "NetBSD", "OpenBSD", "BSD"] else "Godot"
	return OS.get_data_dir().path_join(godot_dir).path_join("app_userdata").path_join("OpenBFME")


static func options_path() -> String:
	return game_user_dir().path_join("Options.ini")


## the value of `key` in the game's Options.ini ("" when the file or the key is missing); keys compare as the game's parser does (trimmed, exact)
static func get_value(key: String, path: String = "") -> String:
	var p := path if path != "" else options_path()
	if not FileAccess.file_exists(p):
		return ""
	var value := ""
	for line in FileAccess.get_file_as_string(p).split("\n"):
		var eq := line.find("=")
		if eq < 0:
			continue
		if line.substr(0, eq).strip_edges() == key:
			value = line.substr(eq + 1).strip_edges() # the last line of a key wins, as in OptionPreferences::parse
	return value


## sets `key` to `value` in the game's Options.ini: the key's lines are replaced by one (or it is appended), every other line stays; the folder
## and the file are created when the game has not run yet. "" when written, else why not.
static func set_value(key: String, value: String, path: String = "") -> String:
	var p := path if path != "" else options_path()
	var lines: PackedStringArray = []
	if FileAccess.file_exists(p):
		lines = FileAccess.get_file_as_string(p).split("\n")
		if lines.size() > 0 and lines[lines.size() - 1] == "":
			lines.remove_at(lines.size() - 1)
	var out: PackedStringArray = []
	var written := false
	for line in lines:
		var eq := line.find("=")
		if eq >= 0 and line.substr(0, eq).strip_edges() == key:
			if not written:
				out.append("%s = %s" % [key, value])
				written = true
			continue
		out.append(line)
	if not written:
		out.append("%s = %s" % [key, value])
	var err := DirAccess.make_dir_recursive_absolute(p.get_base_dir())
	if err != OK and not DirAccess.dir_exists_absolute(p.get_base_dir()):
		return "cannot create %s (%s)" % [p.get_base_dir(), error_string(err)]
	var f := FileAccess.open(p, FileAccess.WRITE)
	if f == null:
		return "cannot write %s (%s)" % [p, error_string(FileAccess.get_open_error())]
	f.store_string("\n".join(out) + "\n")
	f.close()
	return ""


static func free_camera(path: String = "") -> bool:
	return get_value(FREE_CAMERA_KEY, path).to_lower() == "yes"


static func set_free_camera(on: bool, path: String = "") -> String:
	return set_value(FREE_CAMERA_KEY, "yes" if on else "no", path)
