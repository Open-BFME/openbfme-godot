## Lane LAUNCH-1: replacing the launcher with a staged update (Updater.stage_self_update), crash-safe (Sol r1: a kill between the old
## two-file renames left a launcher that could not load its project data).
##
## The launcher is ONE file (its pack is embedded, launcher/export_presets.cfg), so "the launcher" is always exactly the executable at
## its name, and every step below either leaves that file untouched or replaces it in one atomic rename. A persistent journal,
## user://launcher-update.json {state, tag, from, old_sha256, new_sha256, attempts}, records the transaction; every start runs on_start
## first (before anything else; the code is in whichever complete executable is at the name) and completes or undoes it:
##
##   1. the running launcher (old) sees <dir>/update/ staged: it checks every staged file against the cached signed manifest of that
##      version (a mismatch removes update/), writes the journal (state "begin", the old executable's SHA-256 and the new one's FROM THE
##      SIGNED MANIFEST),
##      starts the staged executable as the HELPER (<dir>/update/<exe> -- --apply-update=<dir>) and exits at once;
##   2. the helper waits for the old process to end, then: <exe> -> <exe>.old (a verified copy), update/<exe> -> <exe>.new (a verified
##      copy), and <exe>.new renamed over <exe> in one atomic rename (Linux rename(2); Windows MoveFileEx with MOVEFILE_REPLACE_EXISTING,
##      through `cmd /c move /y`, because Godot's DirAccess.rename removes the target first); journal "placed";
##   3. the helper starts the new <exe> with --after-update; the new launcher sets the journal to "confirmed" as the first thing it does;
##      the helper polls for that from its main loop (a windowed launcher must keep handling messages: under Wine a new Godot process
##      did not get past its start otherwise, so on Windows the swap happens only at a start with a window) and exits;
##   4. not confirmed within START_TIMEOUT_MS (or the new process ended first): the helper stops it (its own child, by pid), puts
##      <exe>.old back over <exe> with the same atomic rename, journal "rolled-back", records the version as rejected (never staged
##      again automatically) and starts the previous launcher;
##   5. the next start after "confirmed" removes <exe>.old, update/ and the journal (the previous launcher is kept until the new one
##      has started once).
## Recovery at a start, by journal state and the SHA-256 of the executable at the name: "begin" with the new executable in place (the
## helper died after its rename) counts as placed; "begin" with the old one retries the helper (at most MAX_ATTEMPTS, then the update is
## rejected); "placed" with the new one confirms (it started); "placed" or "rolled-back" with the old one ends the transaction as
## rejected. Each step is killed in turn by tools/release/test_launcher.py (--test-kill-at, test builds only).
extends RefCounted

const Log := preload("res://scripts/core/launcher_log.gd")
const Updater := preload("res://scripts/core/updater.gd")
const Semver := preload("res://scripts/core/semver.gd")

const START_TIMEOUT_MS := 60000
const MAX_ATTEMPTS := 3
const DOCS := ["README.txt", "LICENSE", "NOTICE", "VERSION"]


static func journal_path() -> String:
	return OS.get_user_data_dir().path_join("launcher-update.json")


static func _read_journal() -> Dictionary:
	if not FileAccess.file_exists(journal_path()):
		return {}
	var j = JSON.parse_string(FileAccess.get_file_as_string(journal_path()))
	if typeof(j) != TYPE_DICTIONARY or not j.has("state") or not j.has("tag"):
		# a journal cut short by a crash while it was written: write_journal writes a temporary file and renames it, so this is not
		# expected; treat it as no transaction (the executable at the name is complete either way)
		Log.line("LAUNCHER the update journal is unreadable: ignored")
		DirAccess.remove_absolute(journal_path())
		return {}
	return j


## the journal is written to a temporary file and renamed over the old one (Linux: atomic; Windows: Godot removes the old journal
## first, so a crash in between leaves no journal: the executable at the name is complete either way, and a staged update is retried)
static func _write_journal(j: Dictionary) -> void:
	var tmp := journal_path() + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	f.store_string(JSON.stringify(j))
	f.flush()
	f.close()
	DirAccess.rename_absolute(tmp, journal_path())


static func _end_journal() -> void:
	DirAccess.remove_absolute(journal_path())


static func sha256_of(path: String) -> String:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return ""
	var hc := HashingContext.new()
	hc.start(HashingContext.HASH_SHA256)
	while f.get_position() < f.get_length():
		hc.update(f.get_buffer(1 << 20))
	return hc.finish().hex_encode()


## copy `from` to `to` through `to`.tmp and a rename; true when `to` then has the SHA-256 `sha`
static func _copy_verified(from: String, to: String, sha: String) -> bool:
	var tmp := to + ".tmp"
	DirAccess.remove_absolute(tmp)
	if DirAccess.copy_absolute(from, tmp) != OK or sha256_of(tmp) != sha:
		DirAccess.remove_absolute(tmp)
		return false
	if FileAccess.file_exists(to):
		DirAccess.remove_absolute(to)
	if DirAccess.rename_absolute(tmp, to) != OK:
		return false
	if OS.get_name() != "Windows":
		FileAccess.set_unix_permissions(to, FileAccess.get_unix_permissions(from))
	return true


## rename `from` over `to` in one atomic step (the executable at `to` is never missing)
static func _replace_atomically(from: String, to: String) -> bool:
	if OS.get_name() == "Windows":
		# MoveFileEx(MOVEFILE_REPLACE_EXISTING): cmd's `move /y` (Godot's DirAccess.rename deletes the target first)
		var out := []
		var code := OS.execute("cmd.exe", ["/c", "move", "/y", from.replace("/", "\\"), to.replace("/", "\\")], out, true)
		return code == 0 and not FileAccess.file_exists(from)
	return DirAccess.rename_absolute(from, to) == OK


static func _reject(updater: RefCounted, tag: String) -> void:
	var rejected: Array = updater.state.get_value("launcher", "rejected_launcher", [])
	if not tag in rejected:
		rejected.append(tag)
	updater.state.set_value("launcher", "rejected_launcher", rejected)
	updater._save_state()


static func _arg(user_args: PackedStringArray, prefix: String) -> String:
	for a in user_args:
		if a.begins_with(prefix):
			return a.substr(prefix.length())
	return ""


static func _forwarded(user_args: PackedStringArray) -> PackedStringArray:
	var out := PackedStringArray()
	for a in user_args:
		if not a.begins_with("--apply-update=") and not a.begins_with("--after-update=") and not a.begins_with("--old-pid="):
			out.append(a)
	return out


static func _start(exe: String, extra: Array, user_args: PackedStringArray) -> int:
	var args := PackedStringArray(["--headless"] if DisplayServer.get_name() == "headless" else [])
	args.append("--")
	args.append_array(extra)
	args.append_array(_forwarded(user_args))
	return OS.create_process(exe, args)


## Called first at every start of a packaged launcher (and of the helper). Returns {exit, message}: exit true when this process must
## quit now; with `swap` the caller polls poll(swap) from its main loop, quits on "confirmed" and calls roll_back on a failure.
static func on_start(updater: RefCounted, user_args: PackedStringArray) -> Dictionary:
	var info: RefCounted = updater.info
	var apply_dir := _arg(user_args, "--apply-update=")
	if apply_dir != "":
		return _helper(updater, apply_dir, user_args)
	var dir: String = updater.launcher_dir
	if dir == "":
		return {"exit": false, "message": ""}
	var exe := dir.path_join(updater.launcher_files()[0])
	var j := _read_journal()
	var message := ""
	if not j.is_empty():
		var mine := sha256_of(exe)
		var state: String = j.state
		if state == "begin" and mine == j.new_sha256:
			state = "placed"  # the helper died after its rename
		if state == "placed" and mine == j.new_sha256:
			if Updater.kill_at == "no-confirm" and _arg(user_args, "--after-update=") != "":
				Log.line("LAUNCHER test: the new launcher quits without confirming its start")
				return {"exit": true, "message": ""}
			j.state = "confirmed"
			_write_journal(j)
			Updater.test_point("after-update-confirmed")
			Log.line("LAUNCHER %s started after the update from %s" % [info.version, j.from])
			message = "The launcher was updated to %s." % info.version
			if _arg(user_args, "--after-update=") != "":
				return {"exit": false, "message": message}  # the helper is still waiting: the rest is done at the next start
		if j.state == "confirmed" and mine == j.new_sha256:
			for f in [exe + ".old", exe + ".new", exe + ".old.tmp", exe + ".new.tmp"]:
				DirAccess.remove_absolute(f)
			updater.remove_tree(dir.path_join("update"))
			if not DirAccess.dir_exists_absolute(dir.path_join("update")):
				_end_journal()
				Log.line("LAUNCHER removed the previous launcher (%s)" % j.from)
		elif state == "begin" and mine == j.old_sha256:
			if int(j.get("attempts", 1)) >= MAX_ATTEMPTS:
				Log.line("LAUNCHER the update to %s did not complete after %d attempts: given up" % [j.tag, MAX_ATTEMPTS])
				_reject(updater, j.tag)
				updater.remove_tree(dir.path_join("update"))
				_end_journal()
			else:
				Log.line("LAUNCHER the update to %s was interrupted before the launcher was replaced: it is retried" % j.tag)
		elif mine == j.old_sha256:
			Log.line("LAUNCHER the update to %s was rolled back; staying on %s" % [j.tag, info.version])
			_reject(updater, j.tag)
			updater.remove_tree(dir.path_join("update"))
			DirAccess.remove_absolute(exe + ".new")
			DirAccess.remove_absolute(exe + ".old")
			_end_journal()
		elif mine != j.new_sha256:
			Log.line("LAUNCHER the update journal names other launchers than this one: ended")
			_end_journal()
	var staged := dir.path_join("update")
	var tag := FileAccess.get_file_as_string(staged.path_join("VERSION.launcher")).strip_edges()
	if tag == "":
		return {"exit": false, "message": message}
	if tag in updater.state.get_value("launcher", "rejected_launcher", []):
		updater.remove_tree(staged)
		return {"exit": false, "message": message}
	if OS.get_name() == "Windows" and DisplayServer.get_name() == "headless":
		Log.line("LAUNCHER the launcher update %s is applied at the next start with a window" % tag)
		return {"exit": false, "message": message}
	var helper := staged.path_join(updater.launcher_files()[0])
	# round 3: the staged files are checked against the signed manifest NOW, and the journal takes the executable's SHA-256 from it (a
	# file swapped into update/ after staging used to be run unchecked: the helper only compared the file with itself)
	var signed: Dictionary = updater.check_staged(dir, tag)
	if signed.ok and (not Semver.is_valid(tag) or not Semver.is_valid(info.version) or Semver.compare(tag, info.version) <= 0):
		signed = {"ok": false, "error": "the staged launcher %s is not newer than this one (%s)" % [tag, info.version]}
	if not signed.ok:
		Log.line("LAUNCHER removed the staged launcher update: %s" % signed.error)
		updater.remove_tree(staged)
		return {"exit": false, "message": message}
	var attempts := 1
	j = _read_journal()
	if not j.is_empty() and j.tag == tag:
		attempts = int(j.get("attempts", 1)) + 1
	_write_journal({"state": "begin", "tag": tag, "from": info.version, "old_sha256": sha256_of(exe), "new_sha256": signed.new_sha256,
		"attempts": attempts})
	Updater.test_point("swap-journal")
	var pid := _start(helper, ["--apply-update=" + dir, "--old-pid=%d" % OS.get_process_id()], user_args)
	if pid <= 0:
		Log.line("LAUNCHER the launcher update %s could not be started; staying on %s" % [tag, info.version])
		return {"exit": false, "message": message}
	Log.line("LAUNCHER started the update to %s (pid %d); this launcher (%s) exits" % [tag, pid, info.version])
	Updater.test_point("swap-helper-started")
	return {"exit": true, "message": ""}


## the helper: the staged new launcher, run from <dir>/update/, replaces <dir>/<exe>
static func _helper(updater: RefCounted, dir: String, user_args: PackedStringArray) -> Dictionary:
	var info: RefCounted = updater.info
	var own := OS.get_executable_path()
	var exe := dir.path_join(updater.launcher_files()[0])
	var j := _read_journal()
	if own.get_base_dir().simplify_path() != dir.path_join("update").simplify_path() or j.is_empty() or j.state != "begin":
		Log.line("LAUNCHER --apply-update ignored: not the staged launcher of an update in progress")
		return {"exit": true, "message": ""}
	var old_pid := _arg(user_args, "--old-pid=").to_int()
	var t0 := Time.get_ticks_msec()
	while old_pid > 0 and OS.is_process_running(old_pid) and Time.get_ticks_msec() - t0 < 30000:
		OS.delay_msec(50)
	Updater.test_point("helper-start")
	if sha256_of(own) != j.new_sha256:
		Log.line("LAUNCHER the staged launcher changed after it was verified: the update is abandoned")
		_reject(updater, j.tag)
		_end_journal()
		return {"exit": true, "message": ""}
	var current := sha256_of(exe)
	if current == j.old_sha256:
		if not _copy_verified(exe, exe + ".old", j.old_sha256):
			Log.line("LAUNCHER the previous launcher could not be backed up (is the folder full or read-only?): the update waits")
			return {"exit": true, "message": ""}
		Updater.test_point("helper-backup")
		if not _copy_verified(own, exe + ".new", j.new_sha256):
			Log.line("LAUNCHER the new launcher could not be copied next to the old one: the update waits")
			return {"exit": true, "message": ""}
		Updater.test_point("helper-copied")
		if not _replace_atomically(exe + ".new", exe) or sha256_of(exe) != j.new_sha256:
			Log.line("LAUNCHER the launcher could not be replaced (is it running?): the update waits")
			return {"exit": true, "message": ""}
		Updater.test_point("helper-replaced")
	elif current != j.new_sha256:
		Log.line("LAUNCHER the launcher at %s is neither the previous nor the new one: the update is abandoned" % exe)
		_end_journal()
		return {"exit": true, "message": ""}
	for d in DOCS:
		if FileAccess.file_exists(own.get_base_dir().path_join(d)):
			DirAccess.copy_absolute(own.get_base_dir().path_join(d), dir.path_join(d))
	j.state = "placed"
	_write_journal(j)
	Updater.test_point("helper-placed")
	var pid := _start(exe, ["--after-update=" + j.from], user_args)
	Log.line("LAUNCHER replaced %s by %s; started it (pid %d)" % [j.from, j.tag, pid])
	var swap := {"tag": j.tag, "pid": pid, "start": Time.get_ticks_msec(), "exe": exe, "user_args": user_args}
	if pid <= 0:
		return roll_back(updater, swap, "the new launcher %s could not be started" % j.tag)
	return {"exit": false, "message": "", "swap": swap}


## One look at a started new launcher (polled from the helper's main loop). "confirmed", "waiting", or "failed: <why>".
static func poll(swap: Dictionary) -> String:
	var j := _read_journal()
	if j.is_empty() or j.get("state") == "confirmed":
		return "confirmed"
	if not OS.is_process_running(swap.pid):
		return "failed: the new launcher %s ended without confirming its start" % swap.tag
	if Time.get_ticks_msec() - swap.start > START_TIMEOUT_MS:
		return "failed: the new launcher %s did not confirm its start within %d s" % [swap.tag, START_TIMEOUT_MS / 1000]
	return "waiting"


## the helper puts the previous launcher back, then starts it. {exit: true}
static func roll_back(updater: RefCounted, swap: Dictionary, problem: String) -> Dictionary:
	var j := _read_journal()
	var exe: String = swap.exe
	if swap.pid > 0 and OS.is_process_running(swap.pid):
		OS.kill(swap.pid)
		OS.delay_msec(500)
	j.state = "rolling-back"
	_write_journal(j)
	if FileAccess.file_exists(exe + ".old") and sha256_of(exe + ".old") == j.old_sha256 \
			and _copy_verified(exe + ".old", exe + ".new", j.old_sha256) and _replace_atomically(exe + ".new", exe):
		j.state = "rolled-back"
		_write_journal(j)
		_reject(updater, swap.tag)
		Log.line("LAUNCHER update to %s rolled back: %s; the previous launcher (%s) is back" % [swap.tag, problem, j.from])
		_start(exe, [], swap.user_args)
	else:
		Log.line("LAUNCHER update to %s failed (%s) and the previous launcher could not be put back: restore %s.old by hand" % [swap.tag, problem, exe.get_file()])
	return {"exit": true, "message": ""}
