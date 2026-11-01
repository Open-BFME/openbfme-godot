## Lane ONEZIP-1: the launcher's side of a game that was started directly (the combined package holds the game and the launcher in one
## folder; whichever is started keeps the install up to date). There is ONE updater, the launcher's: the game downloads and checks
## nothing itself. Started without the launcher, it runs the launcher beside it as
##     OpenBFMELauncher --headless -- --game-start=<request folder>
## (godot/scripts/release/update_handover.gd) and waits a few seconds for the answer. This run, in order:
##   1. decides which version that start plays, from what is on the disk alone (no network): the version Play would start
##      (Updater.play_version: the player's pick, else the newest installed, else the game beside the launcher). When that is the game
##      that asked, the answer is "continue" (with a warning when that folder is not exactly the signed package); when another version
##      is installed, it is checked file by file against its signed manifest and held against removal (the versions lock), and the
##      answer is "start <version>": THE GAME starts that version itself (only the game starts a game, so no late answer can leave two
##      running) and writes its process id into `started`, which this run records as a running game before it lets the lock go;
##   2. then looks for an update like the launcher's window does (the release list, the signed manifest, the verified install into the
##      data folder, the launcher's own update staged), in the background while the game runs: a newer version is used from the next
##      start on. Nothing is ever changed underneath the running game: the game beside the launcher is never written, an update is
##      another folder. With "Update automatically" off it only says that a newer version is there.
##
## The request folder (made by the game in its own user data folder, named on the command line), protocol 2:
##   request.json   {"protocol": 2, "pid": <the game's process id>}                                    written by the game
##   open           the game still waits; it renames it to `closed` when it stops waiting (an answer after that is not read)
##   answer         {"protocol": 2, "action": "continue" | "start", "version": <to start, or "">, "why": <text>}   this run's answer
##   started        {"protocol": 2, "pid": <process id>, "version": <started>}                          the game, after it started one
##   status         {"text": <one line for the player>, "final": <bool>}   the update check's state, rewritten as it goes; the game
##                  shows it in a corner of its menus
## Every file is written to a temporary name and renamed; a failed write is named in the log and ends the hand-over (the game then
## starts its own version at its deadline). Every refusal is named in the launcher's log.
##
## Two launchers never update at once: the update part runs under the launcher lock (InstanceLock), marked as a background run in the
## lock's hello line. With a launcher window open, that window looks after updates and this run only answers the game. A window opened
## while this run downloads asks it to stop (STOP_FILE) and takes the lock over (take_over); the download resumes there.
extends RefCounted

const Semver := preload("res://scripts/core/semver.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const InstanceLock := preload("res://scripts/core/instance_lock.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const PROTOCOL := 2
const MAX_STARTED := 4096
const WAIT_STARTED_MS := 6000   # how long the chosen version is held for the game's `started` (its deadline is 3 s)
const MAX_REQUEST := 65536
const BACKGROUND := " background"
const STOP_FILE := "game-start.stop"
const TAKE_OVER_MS := 10000

var updater: RefCounted
var dir := ""
var _lock: InstanceLock
var _stopped := false
var _last_progress := 0
var _warn := ""


func _init(u: RefCounted, request_dir: String) -> void:
	updater = u
	dir = request_dir


static func request_of(args: PackedStringArray) -> String:
	for a in args:
		if a.begins_with("--game-start="):
			return a.substr(13)
	return ""


## the launcher window's side: the lock's holder is a background run (its hello ends in BACKGROUND): ask it to stop and take the lock.
## true when `lock` is held now.
static func take_over(lock: InstanceLock) -> bool:
	if not lock.other_launcher.ends_with(BACKGROUND):
		return false
	var stop := OS.get_user_data_dir().path_join(STOP_FILE)
	var f := FileAccess.open(stop, FileAccess.WRITE)
	if f != null:
		f.close()
	Log.line("LAUNCHER a background update (started with the game) is asked to stop: this launcher goes on with it")
	var start := Time.get_ticks_msec()
	var held := false
	while not held and Time.get_ticks_msec() - start < TAKE_OVER_MS:
		OS.delay_msec(100)
		held = lock.acquire()
	DirAccess.remove_absolute(stop)
	return held


## {ok, error, args}: the game's request, checked before anything is written into its folder
func _read_request() -> Dictionary:
	var bad := func(why: String) -> Dictionary: return {"ok": false, "error": why, "args": PackedStringArray()}
	if not dir.is_absolute_path() or dir.simplify_path() != dir.replace("\\", "/").trim_suffix("/") or not DirAccess.dir_exists_absolute(dir):
		return bad.call("--game-start does not name an existing folder by its full path")
	var f := FileAccess.open(dir.path_join("request.json"), FileAccess.READ)
	if f == null or f.get_length() > MAX_REQUEST:
		return bad.call("the request folder has no request.json (or one above %d bytes)" % MAX_REQUEST)
	var json := JSON.new()
	if json.parse(f.get_buffer(f.get_length()).get_string_from_utf8()) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return bad.call("request.json is not a JSON object")
	var r: Dictionary = json.data
	var keys := r.keys()
	keys.sort()
	if keys != ["pid", "protocol"]:
		return bad.call("request.json has the keys %s, expected pid, protocol" % str(keys).left(160))
	if typeof(r.protocol) != TYPE_FLOAT or int(r.protocol) != PROTOCOL or float(int(r.protocol)) != r.protocol:
		return bad.call("request.json speaks protocol %s (this launcher speaks %d)" % [str(r.protocol).left(20), PROTOCOL])
	if typeof(r.pid) != TYPE_FLOAT or r.pid < 1 or r.pid != floor(r.pid) or r.pid > 2147483647.0:
		return bad.call("request.json's pid is not a process id")
	if not FileAccess.file_exists(dir.path_join("open")) and not FileAccess.file_exists(dir.path_join("closed")):
		return bad.call("the request folder holds neither open nor closed: not a game's request")
	return {"ok": true, "error": "", "args": PackedStringArray()}


## writes one file of the request folder through a temporary name and a rename; false (named in the log) when that failed
func _write(name: String, value: Dictionary) -> bool:
	var tmp := dir.path_join(name + ".tmp")
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	var ok := f != null
	if ok:
		ok = f.store_string(JSON.stringify(value))
		f.flush()
		ok = ok and f.get_error() == OK
		f.close()
	if ok and FileAccess.file_exists(dir.path_join(name)):
		ok = DirAccess.remove_absolute(dir.path_join(name)) == OK
	ok = ok and DirAccess.rename_absolute(tmp, dir.path_join(name)) == OK
	if not ok:
		DirAccess.remove_absolute(tmp)
		Log.line("LAUNCHER game start: %s could not be written into the game's request folder (is the disk full?)" % name)
	return ok


func _answer(action: String, why: String, version := "") -> bool:
	Log.line("LAUNCHER game start: %s%s%s" % [action, " " + version if version != "" else "", " (%s)" % why if why != "" else ""])
	return _write("answer", {"protocol": PROTOCOL, "action": action, "why": why.left(300), "version": version})


## {pid} the game wrote into `started` after it started `version`, or {} when it did not within WAIT_STARTED_MS (or closed the request
## without it); a reply that is not exactly the protocol's is refused by name
func _wait_started(version: String) -> Dictionary:
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < WAIT_STARTED_MS:
		var f := FileAccess.open(dir.path_join("started"), FileAccess.READ)
		if f != null:
			# the whole reply, refused as a whole when it is larger than the limit (Sol r2: a valid 4096-byte prefix was taken)
			var length := f.get_length()
			var d: Variant = JSON.parse_string(f.get_buffer(length).get_string_from_utf8()) if length <= MAX_STARTED else null
			f.close()
			if length > MAX_STARTED:
				Log.line("LAUNCHER game start: the game's `started` reply is refused (%d bytes, at most %d)" % [length, MAX_STARTED])
				return {}
			# every value's type is checked before it is compared (GDScript refuses == between a text and a number)
			if d is Dictionary and d.keys().size() == 3 and typeof(d.get("protocol")) == TYPE_FLOAT and d.protocol == float(PROTOCOL) \
					and typeof(d.get("version")) == TYPE_STRING and d.version == version \
					and typeof(d.get("pid")) == TYPE_FLOAT and d.pid >= 1 and d.pid == floor(d.pid) and d.pid <= 2147483647.0:
				return {"pid": int(d.pid)}
			Log.line("LAUNCHER game start: the game's `started` reply is refused (not the protocol's)")
			return {}
		if FileAccess.file_exists(dir.path_join("closed")) and not FileAccess.file_exists(dir.path_join("started")):
			OS.delay_msec(100)   # the game closes the request right after it wrote `started`
			if not FileAccess.file_exists(dir.path_join("started")):
				return {}
		OS.delay_msec(20)
	return {}


func _status(text: String, final := false) -> void:
	if _warn != "":
		text = L10n.t("launcher.game_start.warned", {"warning": _warn, "text": text})   # a folder that is not the signed package is said with every line
	Log.line("LAUNCHER game start: " + text)
	_write("status", {"text": text, "final": final})


## the request is still open: take it (the game waits for this run's outcome now); false when the game already went on by itself
func _stop_asked() -> bool:
	if not _stopped and FileAccess.file_exists(updater.data_dir.path_join(STOP_FILE)):
		_stopped = true
		updater.http.cancelled = true
	return _stopped


func run(problem: String) -> int:
	var req := _read_request()
	if not req.ok:
		Log.line("LAUNCHER game start refused, nothing done: " + req.error)
		return 1
	var b: Dictionary = updater.bundled()
	if b.is_empty():
		var why := L10n.t("launcher.game_start.no_signed_game", {"problem": updater.bundled_problem()})
		_answer("continue", why)
		_status(L10n.t("launcher.game_start.not_checked", {"why": why}), true)
		return 1
	var playing: String = b.version
	var pv: String = updater.play_version()
	var warn := ""
	var c: Dictionary = updater.check_bundled()
	if not c.ok:
		warn = L10n.t("launcher.game_start.not_signed", {"error": c.error})
	if pv == b.version:
		if not _answer("continue", warn):
			return 1
	else:
		var held: Dictionary = updater.hold_for_start(pv)
		if not held.ok:
			if not _answer("continue", L10n.t("launcher.game_start.cannot_start", {"version": pv, "error": held.error, "bundled": b.version})):
				return 1
		elif not _answer("start", L10n.t("launcher.game_start.starts_instead", {"version": pv, "bundled": b.version}), pv):
			updater.unlock_versions()
			return 1
		else:
			var started := _wait_started(pv)
			if started.is_empty():
				Log.line("LAUNCHER game start: the game did not report starting %s (it went on with %s, or it could not start it)" % [pv, b.version])
			elif updater.record_started(pv, started.pid):
				playing = pv
			updater.unlock_versions()
		if warn != "" and pv != b.version:
			warn = ""
	_warn = warn
	var code := _update(problem, playing)
	if _lock != null:
		_lock.release()
	return code


func _update(problem: String, playing: String) -> int:
	if updater.macos_updates_unsupported():
		Log.line("LAUNCHER macOS automated updates are unsupported; continuing with the bundled game")
		_status(L10n.t("launcher.game_start.macos_unavailable"), true)
		return 0
	if problem != "":
		_status(L10n.t("launcher.game_start.updates_off", {"problem": problem}), true)
		return 2
	_lock = InstanceLock.new(updater.data_dir, updater.info.version + BACKGROUND)
	if not _lock.acquire():
		var open: bool = _lock.other_launcher != ""
		var text := L10n.t("launcher.game_start.not_checked_lock", {"error": _lock.error})
		if _lock.other_launcher.ends_with(BACKGROUND):
			text = L10n.t("launcher.game_start.other_start")
		elif open:
			text = L10n.t("launcher.game_start.launcher_open")
		_status(text, true)
		_lock = null
		return 0 if open else 2
	DirAccess.remove_absolute(updater.data_dir.path_join(STOP_FILE))
	updater.progress = func(text: String, done: int, total: int) -> void:
		_stop_asked()
		if total > 0 and Time.get_ticks_msec() - _last_progress > 1000:
			_last_progress = Time.get_ticks_msec()
			_write("status", {"text": L10n.t("launcher.game_start.progress", {"text": text, "percent": int(100.0 * done / total)}), "final": false})
	_status(L10n.t("launcher.game_start.checking"))
	var r: Dictionary = updater.fetch_releases()
	if not r.ok:
		_status(L10n.t("launcher.game_start.skipped", {"error": r.error, "version": playing}), true)
		return 2
	var channel: String = updater.channel()
	var chan: Array = updater.in_channel(r.releases, channel)
	if chan.is_empty():
		_status(L10n.t("launcher.game_start.no_release", {"channel": Semver.channel_name(channel).to_lower()}), true)
		return 0
	var mm: Dictionary = updater.release_manifest(chan[0])
	if not mm.ok:
		_status(L10n.t("launcher.game_start.rejected", {"error": mm.error, "version": playing}), true)
		return 3
	var freeze: String = updater.freeze_warning(mm.manifest)
	if freeze != "":
		Log.line("LAUNCHER warning: " + freeze)
	var tag: String = chan[0].tag
	var newest: String = updater.newest_installed()
	if newest != "" and Semver.compare(tag, newest) <= 0:
		_status(L10n.t("launcher.game_start.up_to_date", {"version": playing}), true)
		return 0
	if not updater.auto_update():
		_status(L10n.t("launcher.game_start.available", {"version": tag}), true)
		return 0
	if _stop_asked():
		_status(L10n.t("launcher.game_start.goes_on"), true)
		return 0
	_status(L10n.t("launcher.game_start.downloading", {"version": tag}))
	var code := 0
	var i: Dictionary = updater.install(chan[0], false)
	var msg := L10n.t("launcher.game_start.installed", {"version": tag})
	if not i.ok and _stop_asked():
		_status(L10n.t("launcher.game_start.goes_on"), true)
		return 0
	if not i.ok:
		msg = L10n.t("launcher.game_start.not_installed", {"error": i.error, "version": playing})
		code = 3
	var s: Dictionary = updater.stage_self_update(chan[0])
	if not s.ok and not _stop_asked():
		msg = L10n.t("launcher.game_start.launcher_rejected", {"message": msg, "error": s.error})
		code = 3
	elif s.ok and s.staged != "":
		Log.line("LAUNCHER launcher update %s staged" % s.staged)
	if code != 0:
		# shown by the launcher's window at its next start (main.gd: "An update failed while you played")
		updater.set_state("launcher", "unseen_error", msg)
	_status(msg, true)
	return code
