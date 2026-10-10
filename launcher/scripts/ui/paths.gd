## Lane UI-3: what the launcher's window may show of this machine. The owner's devlog footage showed the Deck's home folder, so every text
## the window shows goes through scrub(): the home folder becomes ~ (Linux) or %USERPROFILE% (Windows), and a path component equal to
## the user's name (a removable drive under /run/media/<name>, another user folder) becomes "…". The full path stays behind a
## "Show folder" button (the file manager opens it; it is never drawn as text).
##
## It also answers "are the games set up?" for the main screen (the Download the games card shows only when they are not), from the files
## the game itself reads (engine/src/Common/System/InstallLocator.cpp): <game user dir>/install-paths.cfg (InstallLocator::remember, the
## folders the player confirmed), <game user dir>/downloaded-installs.cfg (kDownloadedMarkerName, written by scripts/core/aio_install.gd)
## and the ROTWK_INSTALL / BFME2_INSTALL overrides (InstallLocator::configured). The game's own discovery (registry, Wine prefixes) runs
## only in the game: when none of these name two existing folders, the launcher offers the download and says the game will look itself.
extends RefCounted

const GameOptions := preload("res://scripts/core/game_options.gd")

const CONFIG := "install-paths.cfg"           # InstallLocator::remember (GodotRelease.cpp InstallSetup::config_path)
const MARKER := "downloaded-installs.cfg"     # InstallLocator::kDownloadedMarkerName


static func is_windows() -> bool:
	return OS.get_name() == "Windows"


## the user's home folder ("" when the environment has none)
static func home() -> String:
	return OS.get_environment("USERPROFILE" if is_windows() else "HOME")


## the user's name: the login name, else the home folder's last component ("" when neither is known)
static func user_name() -> String:
	for k in (["USERNAME", "USER"] if is_windows() else ["USER", "LOGNAME", "USERNAME"]):
		var v := OS.get_environment(k)
		if v != "":
			return v
	var h := home().replace("\\", "/").trim_suffix("/")
	return h.get_file()


## `text` with the home folder and the user's name taken out (see the file's comment). The home prefix is replaced in both slash forms
## on Windows; the name only as a whole path component, so a word in a sentence that happens to equal it stays.
static func scrub(text: String, home_dir: String = home(), name: String = user_name()) -> String:
	const END := "(?=[/\\\\]|$|\\.(?:\\s|$)|[^A-Za-z0-9._~-])"  # a whole path component ends here (a home folder .../ann is not .../anna)
	if home_dir.length() > 1:
		var h := home_dir.trim_suffix("/").trim_suffix("\\")
		var tilde := "%USERPROFILE%" if is_windows() else "~"
		var forms := [h]
		if h.contains("\\"):
			forms.append(h.replace("\\", "/"))
		elif is_windows() and h.contains("/"):
			forms.append(h.replace("/", "\\"))
		for f in forms:
			text = RegEx.create_from_string(_regex_escape(f) + END).sub(text, tilde.replace("$", "$$"), true)
	if name.length() > 0:
		text = RegEx.create_from_string("([/\\\\])" + _regex_escape(name) + END).sub(text, "$1…", true)
	return text


## a folder for the window: scrubbed, with the platform's separators
static func short(path: String) -> String:
	var p := path.replace("/", "\\") if is_windows() else path
	return scrub(p)


static func _regex_escape(s: String) -> String:
	var out := ""
	for c in s:
		out += ("\\" + c) if "\\^$.|?*+()[]{}".contains(c) else c
	return out


## The game folders the game will use or offer: {found: bool, rotwk, bfme2, source: "env" | "config" | "downloaded" | ""}. found only
## when both folders exist. The first complete source wins, in the game's order (InstallLocator::configured reads the environment, then
## the config; discover() offers the downloaded folders first).
static func game_folders(user_dir: String = GameOptions.game_user_dir()) -> Dictionary:
	var sources := [["env", {"ROTWK_INSTALL": OS.get_environment("ROTWK_INSTALL"), "BFME2_INSTALL": OS.get_environment("BFME2_INSTALL")}],
		["config", read_cfg(user_dir.path_join(CONFIG))], ["downloaded", read_cfg(user_dir.path_join(MARKER))]]
	for s in sources:
		var d: Dictionary = s[1]
		var r: String = d.get("ROTWK_INSTALL", "")
		var b: String = d.get("BFME2_INSTALL", "")
		if r != "" and b != "":
			return {"found": DirAccess.dir_exists_absolute(r) and DirAccess.dir_exists_absolute(b), "rotwk": r, "bfme2": b, "source": s[0]}
	return {"found": false, "rotwk": "", "bfme2": "", "source": ""}


## KEY=VALUE lines of the game's folder files (comments and blank lines skipped, as InstallLocator reads them); {} when missing
static func read_cfg(path: String) -> Dictionary:
	var out := {}
	if not FileAccess.file_exists(path):
		return out
	for line in FileAccess.get_file_as_string(path).split("\n"):
		line = line.trim_suffix("\r")
		if line == "" or line.begins_with("#"):
			continue
		var eq := line.find("=")
		if eq > 0:
			out[line.substr(0, eq)] = line.substr(eq + 1)
	return out


## "Choose again": removes the game's install-paths.cfg (its header says "Delete this file to choose again"), so the game asks on its
## next start. "" or why not.
static func forget_game_folders(user_dir: String = GameOptions.game_user_dir()) -> String:
	var p := user_dir.path_join(CONFIG)
	if not FileAccess.file_exists(p):
		return ""
	var err := DirAccess.remove_absolute(p)
	return "" if err == OK else "cannot remove %s (%s)" % [p, error_string(err)]


## "Show folder" / "Open log folder": the launcher's only file-manager call (tools/release/net_guard.py allows OS.shell_show_in_file_manager
## in this function alone). It opens a folder only when it is an existing local folder that is the launcher's data folder or inside it,
## or one of the game folders game_folders() names now; a URL, a UNC path, a relative path or any other folder is refused. "" or why not.
static func show_in_file_manager(path: String) -> String:
	if path == "" or path.contains("://") or path.begins_with("//") or path.begins_with("\\\\") or not path.is_absolute_path():
		return "not a local folder"
	var p := path.replace("\\", "/").simplify_path()
	if p.split("/").has(".."):
		return "not a plain folder path"
	var data := OS.get_user_data_dir().replace("\\", "/").simplify_path()
	var g := game_folders()
	var games := [String(g.rotwk).replace("\\", "/").simplify_path(), String(g.bfme2).replace("\\", "/").simplify_path()] if g.found else []
	if not (p == data or p.begins_with(data + "/") or p in games):
		return "not a folder the launcher shows"
	if not DirAccess.dir_exists_absolute(p):
		return "the folder does not exist"
	var err := OS.shell_show_in_file_manager(p, true)
	return "" if err == OK else "the file manager did not open (%s)" % error_string(err)


## where the Download the games screen puts the games by default: <home>/Games/BFME (the AIO packages go into its RotWK and BFME2)
static func default_games_dir() -> String:
	var h := home()
	return h.path_join("Games").path_join("BFME") if h != "" else ""
