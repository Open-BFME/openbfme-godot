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
##  * the logs within reach (lane WINCRASH-1): the crash and error messages show the log's real path (Windows: %APPDATA%\...) and an "Open log
##    folder" button (the file manager opens with the log selected); a "Logs" link next to the version in the menus opens the folder; the
##    command line --open-logs opens it and quits without starting the game. The logs stay in the user data folder: not next to the exe (the
##    launcher accepts a version folder only with exactly its signed files, and Program Files is not writable).
##  * --crash-test (debug builds): a deliberate native crash once the menus are up (the release gate checks the symbolized crash dump).
##  * the game folders: mount_retail(fs) mounts the retail archives from ROTWK_INSTALL / BFME2_INSTALL (a developer / test override) or the folders
##    remembered in user://install-paths.cfg; when neither exists, or the remembered folders stopped working, the first-run screen
##    (scripts/release/first_run.gd) lets the player choose them, checks them and remembers them.
extends Node

const LOG_DIR := "user://logs"
const KEEP_LOGS := 20
const REPORT_HINT := "Please attach this file to your report in the OpenBFME Discord, channel #feedback, together with the version above."
const OPEN_LOGS_ACTION := "open_logs"

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
	if OS.get_name() == "Windows":
		# stop S-1923: Godot's crash dump symbolizes its own executable only, on the main thread only
		print("RELEASE [S-1923] a crash dump names the extension's frames as openbfme.windows.template_debug.x86_64.dll+<offset> (mapped later with the build's private symbols), and a crash on a thread other than the main thread writes no dump at all (Godot's handler covers the main thread only)")
	if _has_arg("--open-logs"):
		# lane WINCRASH-1: open the log folder (the newest earlier log selected) and quit without starting the game
		var logs := _session_logs()
		logs.erase(log_path.get_file())
		print("RELEASE --open-logs: ", log_path_for_user(LOG_DIR + "/" + logs[-1] if not logs.is_empty() else LOG_DIR))
		open_log_folder(LOG_DIR + "/" + logs[-1] if not logs.is_empty() else LOG_DIR)
		get_tree().quit.call_deferred(0)
		return
	if _has_arg("--crash-test") and OS.is_debug_build():
		_crash_test.call_deferred()
	# lane PLAY-1: a scripted run (--auto) never waits for a modal dialog (a killed test run left the marker and the next windowed run stopped at the alert)
	if not previous_crash_log.is_empty() and not _headless() and not OS.get_cmdline_user_args().has("--auto"):
		_message.call_deferred("OpenBFME did not close normally last time (a crash?).\n\nThe log of that run is:\n%s\n\n%s" % [
			_folder_and_file(log_path_for_user(previous_crash_log)), REPORT_HINT], "OpenBFME", previous_crash_log, false)


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


## Shows the error and the log file (with the Open log folder button), then quits with exit code 1 when the player closes it (headless:
## printed only, quits at once). The game is paused while the message is up.
func fatal(message: String) -> void:
	printerr("GAME FATAL: ", message)
	if _headless() or info == null:
		get_tree().quit(1)
		return
	var where: String = _folder_and_file(log_path_for_user(log_path)) if not log_path.is_empty() else "(no log file)"
	get_tree().paused = true
	_message("%s\n\nOpenBFME cannot continue.\n\nVersion: %s\nLog file: %s\n\n%s" % [message, version_line(), where, REPORT_HINT], "OpenBFME error", log_path,
		true)


## lane WINCRASH-1: a log path as the player finds it in the file manager: Windows separators, the %APPDATA% folder by its name (the dialog may
## show the player's own path, it is not written to the log; the console print of it goes through the privacy filter); elsewhere ~/...
func log_path_for_user(path: String) -> String:
	var full := ProjectSettings.globalize_path(path)
	if OS.get_name() == "Windows":
		var appdata := OS.get_environment("APPDATA").replace("\\", "/").trim_suffix("/")
		if not appdata.is_empty() and full.to_lower().begins_with(appdata.to_lower() + "/"):
			full = "%APPDATA%" + full.substr(appdata.length())
		return full.replace("/", "\\")
	return info.redact(full) if info != null else full


## lane WINCRASH-1: opens the file manager on the log folder, with a session log selected in it when `path` names one (by its file name;
## "" = this session's log); otherwise the folder itself. Explorer /select, the FileManager1 D-Bus ShowItems or xdg-open of the folder.
func open_log_folder(path: String = "") -> void:
	var name := (path if not path.is_empty() else log_path).get_file()
	var err: Error
	if _is_session_log_name(name) and FileAccess.file_exists(LOG_DIR + "/" + name):
		err = open_log_file(name)
	else:
		err = open_user_folder(UserFolder.LOGS)
	if err != OK:
		printerr("RELEASE the log folder did not open: ", error_string(err), " (", log_path_for_user(LOG_DIR), ")")


## The user data folders the game may show in the file manager (lane WINCRASH-1 r4: a fixed set, their paths built here).
enum UserFolder { LOGS }


func open_user_folder(kind: UserFolder) -> Error:
	match kind:
		UserFolder.LOGS:
			DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("user://logs"))
			return _show_in_file_manager(ProjectSettings.globalize_path("user://logs"))
	return ERR_INVALID_PARAMETER


## A session log, selected in its folder: `name` must be a session log's file name (openbfme-<...>.log: letters, digits, "-", "." only).
func open_log_file(name: String) -> Error:
	if not _is_session_log_name(name):
		return ERR_INVALID_PARAMETER
	return _show_in_file_manager(ProjectSettings.globalize_path("user://logs/" + name))


## The game's only call of the file manager (tools/release/net_guard.py allows OS.shell_show_in_file_manager / OS.shell_open in this function
## and nowhere else): `path` must lie inside the user data folder after normalising (no URL, no UNC path, no ".." escape).
func _show_in_file_manager(path: String) -> Error:
	var root := OS.get_user_data_dir().replace("\\", "/").simplify_path()
	var target := path.replace("\\", "/").simplify_path()
	if root.is_empty() or target.contains("..") or target.contains("://") or not (target == root or target.begins_with(root + "/")):
		printerr("RELEASE refused to show a path outside the user data folder in the file manager")
		return ERR_INVALID_PARAMETER
	return OS.shell_show_in_file_manager(target, true)


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


## A message box with "Open log folder" (opens the folder with `log_file` selected, the box stays) and OK. `quit_after`: OK / closing quits with
## exit code 1 (fatal). It works while the tree is paused.
func _message(text: String, title: String, log_file: String, quit_after: bool) -> void:
	var box := AcceptDialog.new()
	box.name = "LogMessage"
	box.title = title
	box.dialog_text = text
	box.dialog_autowrap = true
	box.min_size = Vector2i(720, 0)
	box.exclusive = true
	box.process_mode = Node.PROCESS_MODE_ALWAYS
	box.ok_button_text = "OK"
	box.add_button("Open log folder", false, OPEN_LOGS_ACTION)
	box.custom_action.connect(func(action: StringName) -> void:
		if action == OPEN_LOGS_ACTION:
			open_log_folder(log_file))
	var done := func() -> void:
		box.queue_free()
		if quit_after:
			get_tree().quit(1)
	box.confirmed.connect(done)
	box.canceled.connect(done)
	add_child(box)
	box.popup_centered()


# a path without spaces cannot wrap in the message box: the folder on one line, the file name on the next
func _folder_and_file(path: String) -> String:
	var cut := maxi(path.rfind("\\"), path.rfind("/"))
	return path if cut < 0 else path.substr(0, cut + 1) + "\n" + path.substr(cut + 1)


# openbfme-<date>-<time>[-<n>].log: letters, digits, "-" and "." only (no separator, no "..")
func _is_session_log_name(name: String) -> bool:
	if not name.begins_with("openbfme-") or not name.ends_with(".log") or name.contains(".."):
		return false
	for c in name:
		if not (c == "-" or c == "." or (c >= "0" and c <= "9") or (c >= "a" and c <= "z") or (c >= "A" and c <= "Z")):
			return false
	return true


func _has_arg(flag: String) -> bool:
	return OS.get_cmdline_user_args().has(flag) or OS.get_cmdline_args().has(flag)


func _crash_test() -> void:
	# the menus first, so the crash happens in a running game as a tester's would (about two seconds)
	await get_tree().create_timer(2.0).timeout
	print("RELEASE --crash-test: a deliberate native crash now")
	info.crash_test()


## user://logs/openbfme-*.log file names, oldest first.
func _session_logs() -> Array:
	var logs: Array = []
	var dir := DirAccess.open(LOG_DIR)
	if dir == null:
		return logs
	for file in dir.get_files():
		if file.begins_with("openbfme-") and file.ends_with(".log"):
			logs.append(file)
	logs.sort()
	return logs


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
	var logs := _session_logs()
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
	label.set_meta("game_text", true) # lane CAMP-2: the release's own corner text, not a developer overlay (game.gd _dev_text_on_screen)
	_watermark.add_child(label)
	# lane WINCRASH-1: the log folder one click away, a small link under the version (the retail menus keep that corner free)
	var logs := LinkButton.new()
	logs.text = "Logs"
	logs.tooltip_text = "Open the folder of OpenBFME's log files (attach the log to a report)"
	logs.underline = LinkButton.UNDERLINE_MODE_ON_HOVER
	logs.add_theme_font_size_override("font_size", 13)
	logs.add_theme_color_override("font_color", Color(1, 1, 1, 0.75))
	logs.add_theme_color_override("font_hover_color", Color(1, 1, 1, 1))
	logs.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	logs.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	logs.offset_right = -8
	logs.offset_top = 22
	logs.focus_mode = Control.FOCUS_NONE
	logs.name = "OpenLogs"
	logs.pressed.connect(func() -> void: open_log_folder())
	_watermark.add_child(logs)
	_watermark.name = "ReleaseWatermark"
	add_child(_watermark)
