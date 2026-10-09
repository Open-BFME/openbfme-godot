## Lane RELEASE-1: what a public tester needs, as the autoload `Release` (project.godot [autoload]), so every scene has it.
##
##  * the session log: one file per run, user://logs/openbfme-<date>-<time>.log (the user data folder: Linux ~/.local/share/godot/app_userdata/OpenBFME,
##    Windows %APPDATA%\Godot\app_userdata\OpenBFME). Written by the SessionLogger class (GodotDevice/GodotRelease.h): every print, warning and error of
##    the session, the version line first, each line without the home folder or the user name (Common/LogPrivacy.h), flushed at once. Godot's own
##    file log is off in project.godot (it would hold the same text unfiltered). The console (stdout / stderr) goes through the same filter
##    (SessionLogger.filter_console, POSIX). The 20 newest logs are kept.
##  * the version: "OpenBFME <git describe> (<build date>)" printed first, written first into the log, and shown in a corner of the menus
##    (game.gd calls set_in_game(true) while a game is on screen).
##  * a crash: a marker user://logs/<log name>.running (with the process id) exists while the game runs and is removed when it ends normally; a
##    marker left by a process that is no longer running means the last run crashed or was killed: a message box names that run's log and asks the
##    tester to attach it.
##  * a stop the game cannot continue from: fatal(message) shows a message box with the error and the log file, then quits.
##  * the game folders: mount_retail(fs) mounts the retail archives from ROTWK_INSTALL / BFME2_INSTALL (a developer / test override) or the folders
##    remembered in user://install-paths.cfg; when neither exists, or the remembered folders stopped working, the first-run screen
##    (scripts/release/first_run.gd) lets the player choose them, checks them and remembers them.
extends Node

const LOG_DIR := "user://logs"
const KEEP_LOGS := 20
const REPORT_HINT := "Please attach this file to your report in the OpenBFME Discord, channel #feedback, together with the version above."

var info: RefCounted        # ReleaseInfo
var logger: RefCounted      # SessionLogger
var log_path := ""          # user://logs/openbfme-....log of this run
var previous_crash_log := "" # the log of a run that did not end normally ("" = none)
var _marker := ""
var _watermark: CanvasLayer
var _in_game := false


func _init() -> void:
	if not ClassDB.class_exists("SessionLogger") or not ClassDB.class_exists("ReleaseInfo"):
		printerr("RELEASE the openbfme extension is not loaded: no session log, no version")
		return
	info = ClassDB.instantiate("ReleaseInfo")
	_find_previous_crash()
	_open_log()
	print(info.version_line())
	if not log_path.is_empty():
		print("RELEASE session log: ", info.redact(ProjectSettings.globalize_path(log_path)))
	if not previous_crash_log.is_empty():
		print("RELEASE the previous run did not end normally; its log: ", info.redact(ProjectSettings.globalize_path(previous_crash_log)))


func _ready() -> void:
	if info == null:
		return
	_add_watermark()
	if not previous_crash_log.is_empty() and not _headless():
		_alert.call_deferred("OpenBFME did not close normally last time (a crash?).\n\nThe log of that run is:\n%s\n\n%s" % [
			info.redact(ProjectSettings.globalize_path(previous_crash_log)), REPORT_HINT])


func _exit_tree() -> void:
	# a normal end (quit, window closed): the marker goes, the log is closed
	if not _marker.is_empty():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(_marker))
		_marker = ""
	if logger != null:
		OS.remove_logger(logger)
		logger.close()


func version_line() -> String:
	return info.version_line() if info != null else "OpenBFME (no extension)"


## Shows the error and the log file, then quits with exit code 1 (headless: printed only).
func fatal(message: String) -> void:
	printerr("GAME FATAL: ", message)
	if not _headless():
		var where: String = info.redact(ProjectSettings.globalize_path(log_path)) if info != null and not log_path.is_empty() else "(no log file)"
		OS.alert("%s\n\nOpenBFME cannot continue.\n\nVersion: %s\nLog file: %s\n\n%s" % [message, version_line(), where, REPORT_HINT], "OpenBFME error")
	get_tree().quit(1)


## game.gd: true while a game is on screen (the version corner is shown in the menus only).
func set_in_game(in_game: bool) -> void:
	if in_game == _in_game:
		return
	_in_game = in_game
	if _watermark != null:
		_watermark.visible = not in_game


## Mounts the retail archives (RetailFileSystem `fs`) from the environment, the remembered folders or the first-run screen. Returns the
## mount report ({ok, errors, ...}); with ok false the caller fails with the errors.
func mount_retail(fs: RefCounted) -> Dictionary:
	var setup: RefCounted = ClassDB.instantiate("InstallSetup")
	var conf: Dictionary = setup.configured()
	var args := OS.get_cmdline_user_args()
	var test_pick := _first_run_test_args(args)
	print("RELEASE game folders: ", conf.source)
	if conf.source == "env" and test_pick.is_empty():
		return fs.mount_retail()
	var problem := ""
	if conf.source == "config" and test_pick.is_empty():
		var mount: Dictionary = fs.mount_retail()
		if mount.ok:
			return mount
		problem = "The game folders chosen before do not work any more:\n" + "\n".join(mount.errors)
		printerr("RELEASE ", problem)
	if _headless() and test_pick.is_empty():
		var why := problem if not problem.is_empty() else "no game folders chosen yet"
		return {"ok": false, "errors": [why + " (start OpenBFME with a window once to choose them, or set ROTWK_INSTALL and BFME2_INSTALL)"]}
	var screen: CanvasLayer = preload("res://scripts/release/first_run.gd").new()
	add_child(screen)
	var result: Dictionary = await screen.run(fs, setup, problem, test_pick)
	screen.queue_free()
	return result


# --first-run=accept                 take the first discovered folders that pass the check (the "found" case of the fresh-user test)
# --first-run-rotwk=DIR --first-run-bfme2=DIR   pick these folders (the "picked" / "rejected" cases)
func _first_run_test_args(args: PackedStringArray) -> Dictionary:
	var pick := {}
	for arg in args:
		if arg == "--first-run=accept":
			pick["accept"] = true
		elif arg.begins_with("--first-run-rotwk="):
			pick["rotwk"] = arg.substr(18)
		elif arg.begins_with("--first-run-bfme2="):
			pick["bfme2"] = arg.substr(18)
	return pick


func _headless() -> bool:
	return DisplayServer.get_name() == "headless"


func _alert(text: String) -> void:
	OS.alert(text, "OpenBFME")


func _open_log() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(LOG_DIR))
	var stamp := Time.get_datetime_string_from_system(false, true).replace(":", "").replace(" ", "-").replace("-", "")
	var name := "openbfme-%s-%s" % [stamp.substr(0, 8), stamp.substr(8)]
	var path := LOG_DIR + "/" + name + ".log"
	var n := 2
	while FileAccess.file_exists(path):
		path = LOG_DIR + "/%s-%d.log" % [name, n]
		n += 1
	logger = ClassDB.instantiate("SessionLogger")
	if not logger.open(path):
		printerr("RELEASE ", logger.get_last_error())
		logger = null
		return
	log_path = path
	OS.add_logger(logger)
	# the console is a report sink too (a pasted terminal, the crash handler's dump): the same filter (Common/ConsoleFilter.h)
	if not logger.filter_console():
		print("RELEASE console not filtered: ", logger.get_last_error())
	_marker = path + ".running"
	var f := FileAccess.open(_marker, FileAccess.WRITE)
	if f != null:
		f.store_string(JSON.stringify({"pid": OS.get_process_id(), "log": path, "version": info.version()}))
		f.close()
	_prune_logs()


func _find_previous_crash() -> void:
	var dir := DirAccess.open(LOG_DIR)
	if dir == null:
		return
	var newest := ""
	for file in dir.get_files():
		if not file.ends_with(".log.running"):
			continue
		var marker := LOG_DIR + "/" + file
		var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(marker))
		var pid := int(data.get("pid", -1)) if data is Dictionary else -1
		if pid > 0 and pid != OS.get_process_id() and info.process_alive(pid):
			continue # another OpenBFME that is still running (two windows of a LAN test)
		DirAccess.remove_absolute(ProjectSettings.globalize_path(marker))
		var crashed_log := marker.trim_suffix(".running")
		if crashed_log > newest:
			newest = crashed_log
	previous_crash_log = newest


func _prune_logs() -> void:
	var dir := DirAccess.open(LOG_DIR)
	if dir == null:
		return
	var logs: Array = []
	for file in dir.get_files():
		if file.begins_with("openbfme-") and file.ends_with(".log"):
			logs.append(file)
	logs.sort()
	while logs.size() > KEEP_LOGS:
		var old: String = logs.pop_front()
		if LOG_DIR + "/" + old != previous_crash_log:
			DirAccess.remove_absolute(ProjectSettings.globalize_path(LOG_DIR + "/" + old))


func _add_watermark() -> void:
	_watermark = CanvasLayer.new()
	_watermark.layer = 100
	var label := Label.new()
	label.text = version_line() + "  -  unofficial test build"
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color(1, 1, 1, 0.75))
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	# top right: the retail menus keep their buttons along the bottom edge (the lobby's Play) and their titles in the middle
	label.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	label.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	label.offset_right = -8
	label.offset_top = 4
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.name = "Version"
	_watermark.add_child(label)
	_watermark.name = "ReleaseWatermark"
	add_child(_watermark)
