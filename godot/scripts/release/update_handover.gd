## Lane ONEZIP-1: a game started directly keeps itself up to date through the launcher beside it.
##
## The combined package holds the game and the OpenBFME Launcher in one folder. The launcher is the ONE updater (the release list, the
## signed manifest, the downloads, the checks: launcher/scripts/core/updater.gd); the game downloads nothing. Started by the player
## without the launcher, the game:
##   1. checks that the launcher beside it is a signed one (scripts/release/launcher_check.gd: the package's signed payload inventory,
##      or the signed proof of a launcher update) and refuses to start any other program, by name;
##   2. runs it once, without a window and detached from this game's console:
##          OpenBFMELauncher --headless -- --game-start=<request folder>
##      (launcher/scripts/core/game_start.gd describes the folder's files);
##   3. waits for its answer until DEADLINE_MS after step 1 began, at most: "continue" (this game starts) or "start <version>" (a newer
##      or picked version is installed and verified: THIS game starts that version's executable from the launcher's data folder with
##      its own arguments after "--", tells the launcher its process id, and closes). Only this game starts a game, so a late or lost
##      answer can never leave two running: at the deadline the request is closed (the `open` file renamed to `closed`) and this game
##      starts; anything the launcher writes later is not read. An answer that is not exactly the protocol's is refused by name.
## The launcher then looks for an update in the background and writes its state as one line (`status`), shown in a corner of the menus
## by scripts/release/release.gd. The game is never changed while it runs: an update is another folder, used from the next start on.
##
## Not asked (why_not names the reason in the log): a game the launcher started or installed (it already looked), a run without a
## window or with scripted options, a development run, --no-update-check / OPENBFME_NO_UPDATE_CHECK=1, a folder without the launcher.
## tools/release/net_guard.py allows OS.create_process in this file alone: the launcher, and the installed version the launcher named
## (Windows: the program itself; elsewhere through /bin/sh, which only detaches it from this game's console).
extends RefCounted

const LauncherCheck := preload("res://scripts/release/launcher_check.gd")
const Trust := preload("res://scripts/release/release_trust.gd")

const PROTOCOL := 2
const DEADLINE_MS := 3000
const STARTED_BY_LAUNCHER := "OPENBFME_STARTED_BY_LAUNCHER"
const OFF_ENV := "OPENBFME_NO_UPDATE_CHECK"
const OFF_ARG := "--no-update-check"
const REQUESTS := "user://update-check"
const KEEP_REQUEST_SECONDS := 86400
const MAX_ANSWER := 4096
const VERSION_RE := "^v(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})(-(alpha|preview)\\.(0|[1-9][0-9]{0,8}))?$"

var dir := ""
var key := PackedByteArray()   # tests give their own key and repository (the packaged game: release_trust.gd)
var repo := ""
var launcher_dir := ""         # the launcher's data folder (tests: their own)
var user_args := PackedStringArray()
var _start := 0
var _done := false
var _status_seen := ""


func _init() -> void:
	key = Trust.RELEASE_KEY.hex_decode()
	repo = Trust.REPO
	launcher_dir = OS.get_data_dir().path_join("OpenBFMELauncher")


static func launcher_path() -> String:
	return OS.get_executable_path().get_base_dir().path_join(LauncherCheck.launcher_name())


static func game_exe() -> String:
	return "OpenBFME.exe" if OS.get_name() == "Windows" else "OpenBFME.x86_64"


## "" when the launcher beside the game is asked; else why this start does not ask
static func why_not(scripted: bool) -> String:
	if not OS.has_feature("template"):
		return "a development run"
	if DisplayServer.get_name() == "headless":
		return "a run without a window"
	if scripted:
		return "a scripted run"
	if OS.get_cmdline_user_args().has(OFF_ARG) or OS.get_environment(OFF_ENV) == "1":
		return "turned off (%s)" % OFF_ARG
	if OS.get_environment(STARTED_BY_LAUNCHER) != "":
		return "started by the launcher"
	var here := OS.get_executable_path().get_base_dir().replace("\\", "/").simplify_path()
	if RegEx.create_from_string("/versions/v[^/]+/game$").search(here) != null:
		return "a version the launcher installed"
	if not FileAccess.file_exists(launcher_path()):
		return "no OpenBFME Launcher beside the game (a game-only folder does not update)"
	return ""


## Checks the launcher beside the game, makes the request folder and starts the launcher on it. "" when it runs; else why not (this
## game then starts as it is). `folder`: the game's folder (the executable's).
func begin(args: PackedStringArray, folder: String = "") -> String:
	_start = Time.get_ticks_msec()
	user_args = args
	var why := LauncherCheck.check(folder if folder != "" else OS.get_executable_path().get_base_dir(), key, repo)
	if why != "":
		_done = true
		return "the launcher beside the game is not started: " + why
	var root := ProjectSettings.globalize_path(REQUESTS)
	DirAccess.make_dir_recursive_absolute(root)
	_remove_old(root)
	dir = root.path_join("%d-%d-%08x" % [OS.get_process_id(), Time.get_ticks_usec(), randi()])
	if DirAccess.make_dir_absolute(dir) != OK:
		_done = true
		return "the request folder could not be made in the user data folder (is the disk full?)"
	if not _write("request.json", JSON.stringify({"protocol": PROTOCOL, "pid": OS.get_process_id()})) or not _write("open", ""):
		_done = true
		return "the request could not be written into the user data folder (is the disk full?)"
	var pid := _start_detached(launcher_path(), PackedStringArray(["--headless", "--", "--game-start=" + dir]))
	if pid <= 0:
		_close()
		return "the OpenBFME Launcher beside the game could not be started"
	return ""


## {state: "waiting" | "continue" | "switched", text}. Never waits past DEADLINE_MS after begin().
func poll() -> Dictionary:
	if _done:
		return {"state": "continue", "text": "the update hand-over is over"}
	# the deadline first (Sol r2): an answer that is there only after it is not acted on, whatever it says (the launcher holds the
	# version it names only for a while after its answer)
	var late := Time.get_ticks_msec() - _start > DEADLINE_MS
	var f := FileAccess.open(dir.path_join("answer"), FileAccess.READ)
	if late:
		if f != null:
			f.close()
		_close()
		if f != null:
			return {"state": "continue", "text": "the launcher's answer was refused: it came after the %d s deadline" % (DEADLINE_MS / 1000)}
		return {"state": "continue", "text": "the launcher beside the game did not answer within %d s; the update check goes on in the background" % (DEADLINE_MS / 1000)}
	if f != null:
		var length := f.get_length()
		var answer := _parse_answer(f.get_buffer(MAX_ANSWER + 1) if length <= MAX_ANSWER else PackedByteArray(), length)
		f.close()
		return _act(answer)
	return {"state": "waiting", "text": ""}


## {action: continue | start, version, why} of the launcher's answer, or {error} naming what is wrong with it
func _parse_answer(bytes: PackedByteArray, length: int) -> Dictionary:
	if length > MAX_ANSWER:
		return {"error": "it is larger than %d bytes" % MAX_ANSWER}
	var json := JSON.new()
	if json.parse(bytes.get_string_from_utf8()) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return {"error": "it is not a JSON object"}
	var a: Dictionary = json.data
	var keys := a.keys()
	keys.sort()
	if keys != ["action", "protocol", "version", "why"]:
		return {"error": "its keys are %s" % str(keys).left(120)}
	if not typeof(a.protocol) in [TYPE_INT, TYPE_FLOAT] or a.protocol != PROTOCOL:
		return {"error": "it speaks protocol %s (this game speaks %d)" % [str(a.protocol).left(20), PROTOCOL]}
	if typeof(a.action) != TYPE_STRING or not a.action in ["continue", "start"]:
		return {"error": "its action is not continue or start"}
	if typeof(a.why) != TYPE_STRING or a.why.length() > 300 or typeof(a.version) != TYPE_STRING:
		return {"error": "its reason or version is not a short text"}
	if a.action == "start" and RegEx.create_from_string(VERSION_RE).search(a.version) == null:
		return {"error": "it names no release version to start ('%s')" % a.version.left(40).c_escape()}
	return a


func _act(answer: Dictionary) -> Dictionary:
	if answer.has("error"):
		_close()
		return {"state": "continue", "text": "the launcher's answer was refused: " + answer.error}
	if answer.action == "continue":
		_close()
		return {"state": "continue", "text": answer.why}
	# start the installed version the launcher verified (it holds that version against removal until it hears from this game)
	var exe := launcher_dir.path_join("versions").path_join(answer.version).path_join("game").path_join(game_exe())
	var folder := DirAccess.open(exe.get_base_dir())
	if folder == null or not FileAccess.file_exists(exe) or folder.is_link(game_exe()):
		_close()
		return {"state": "continue", "text": "%s is not installed where the launcher keeps it: this version starts" % answer.version}
	OS.set_environment(STARTED_BY_LAUNCHER, "1")
	var run := PackedStringArray(["--"])
	run.append_array(user_args)
	var pid := _start_detached(exe, run)
	OS.set_environment(STARTED_BY_LAUNCHER, "")
	if pid <= 0:
		_close()
		return {"state": "continue", "text": "%s could not be started: this version starts" % answer.version}
	if not _write("started", JSON.stringify({"protocol": PROTOCOL, "pid": pid, "version": answer.version})):
		push_warning("UPDATE the launcher could not be told the process id of %s (it does not protect that version from removal this time)" % answer.version)
	_close()
	return {"state": "switched", "text": answer.why if answer.why != "" else "%s is installed: it starts instead" % answer.version}


## the update check's newest line for the player: {text, final}, or {} when there is no new one
func status() -> Dictionary:
	var f := FileAccess.open(dir.path_join("status"), FileAccess.READ)
	if f == null or f.get_length() > MAX_ANSWER:
		return {}
	var s: Variant = JSON.parse_string(f.get_buffer(f.get_length()).get_string_from_utf8())
	if not s is Dictionary or typeof(s.get("text")) != TYPE_STRING or String(s.text) == _status_seen:
		return {}
	_status_seen = String(s.text)
	return {"text": _status_seen.left(300), "final": typeof(s.get("final")) == TYPE_BOOL and s.final}


## the hand-over is over: the request is closed (the launcher starts nothing for it and reads no answer from it from now on)
func _close() -> void:
	_done = true
	if DirAccess.rename_absolute(dir.path_join("open"), dir.path_join("closed")) != OK and not FileAccess.file_exists(dir.path_join("closed")):
		push_warning("UPDATE the update request could not be closed")


## Starts a program detached from this game's console. The game's stdout and stderr are the session logger's filter pipe (POSIX,
## Common/ConsoleFilter); a child that inherited it would keep it open after this game ended (found with the packaged game,
## tools/release/onezip_play_test.py), so elsewhere than on Windows the program starts through the shell with its console on /dev/null.
static func _start_detached(path: String, args: PackedStringArray) -> int:
	if OS.get_name() == "Windows":
		return OS.create_process(path, args)
	return OS.create_process("/bin/sh", PackedStringArray(["-c", "exec \"$0\" \"$@\" </dev/null >/dev/null 2>&1", path]) + args)


func _write(name: String, text: String) -> bool:
	var tmp := dir.path_join(name + ".tmp")
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return false
	var ok := (f.store_string(text) or text.is_empty())
	f.flush()
	ok = ok and f.get_error() == OK
	f.close()
	if not ok or DirAccess.rename_absolute(tmp, dir.path_join(name)) != OK:
		DirAccess.remove_absolute(tmp)
		return false
	return true


# request folders of earlier starts (their launchers are long done)
func _remove_old(root: String) -> void:
	var now := int(Time.get_unix_time_from_system())
	for name in DirAccess.get_directories_at(root):
		var d := root.path_join(name)
		if now - int(FileAccess.get_modified_time(d.path_join("request.json"))) < KEEP_REQUEST_SECONDS:
			continue
		for f in DirAccess.get_files_at(d):
			DirAccess.remove_absolute(d.path_join(f))
		DirAccess.remove_absolute(d)
