## Lane RELEASE-1: what a public tester needs, as the autoload `Release` (project.godot [autoload]), so every scene has it.
##
##  * the session log: one file per run, user://logs/openbfme-<date>-<time>.log (the user data folder: Linux ~/.local/share/godot/app_userdata/OpenBFME,
##    Windows %APPDATA%\Godot\app_userdata\OpenBFME). Written by the SessionLogger class (GodotDevice/GodotRelease.h): every print, warning and error of
##    the session, the version line first, each line without the home folder or the user name (Common/LogPrivacy.h), flushed at once. Godot's own
##    file log is off in project.godot (it would hold the same text unfiltered). The console (stdout / stderr) goes through the same filter
##    (SessionLogger.filter_console, POSIX). Fields are capped at 16 KiB; at 1 MiB each file retains its initial context (up to 64 KiB)
##    and a rolling tail of recent records. The 20 newest logs are kept.
##  * the version: "OpenBFME <git describe> (<build date>)" printed first, written first into the log, and shown in a corner of the menus
##    (game.gd calls set_in_game(true) while a game is on screen).
##  * a crash: a marker user://logs/<log name>.running (with the process id) exists while the game runs and is removed when it ends normally; a
##    marker left by a process that is no longer running means the last run crashed or was killed: a message box names that run's log and asks the
##    tester to attach it.
##    Lane CRASHACT-1: in a player's build (an export template, not headless, not --auto) that run's report goes to the OpenBFME crash service
##    by itself (scripts/crash/crash_client.gd: the privacy-filtered report, the on-disk queue, the upload); Options > Interface "Send crash
##    reports automatically" (Options.ini OpenBFMECrashReports, default yes) turns that off, and the message then offers "Send report" for that
##    one crash. A fatal error (fatal()) is reported the same way. The queue (user://crash-queue) is sent at each start, in the background.
##    Minidumps stay local: they are never uploaded (the service takes no attachments).
##  * a stop the game cannot continue from: fatal(message) shows a message box with the error and the log file, then quits.
##  * the logs within reach (lane WINCRASH-1): the crash and error messages show the log's real path (Windows: %APPDATA%\...) and an "Open log
##    folder" button (the file manager opens with the log selected); a "Logs" link next to the version in the menus opens the folder; the
##    command line --open-logs opens it and quits without starting the game. The logs stay in the user data folder: not next to the exe (the
##    launcher accepts a version folder only with exactly its signed files, and Program Files is not writable).
##  * --crash-test / --crash-test-worker (debug builds): deliberate native main/worker crashes once the menus are up.
##  * the game folders: mount_retail(fs) mounts the retail archives from ROTWK_INSTALL / BFME2_INSTALL (a developer / test override) or the folders
##    remembered in user://install-paths.cfg; when neither exists, or the remembered folders stopped working, the first-run screen
##    (scripts/release/first_run.gd) lets the player choose them, checks them and remembers them.
##  * updates (lane ONEZIP-1): a game the player starts directly from the combined package asks the OpenBFME Launcher beside it which
##    version to play and for an update check (update_handover(), scripts/release/update_handover.gd); the check's state is one line
##    under the version in the menus' corner ("UPDATE ..." in the log).
extends Node

const UpdateHandover := preload("res://scripts/release/update_handover.gd")
const CrashClient := preload("res://scripts/crash/crash_client.gd")
const GameOptions := preload("res://scripts/game_options.gd")
const LanguageWarning := preload("res://scripts/release/language_warning.gd")
const UPDATE_POLL_SECONDS := 1.0
const UPDATE_NOTE_SECONDS := 15.0   # how long "Up to date" stays in the corner
const UPDATE_WATCH_SECONDS := 3 * 3600.0
const LOG_DIR := "user://logs"
const KEEP_LOGS := 20
const REPORT_HINT := "Please attach this file and the version above to the OpenBFME Discord #bug-reports forum or a GitHub issue."
const NO_LOG_REPORT_HINT := "No session log was created. Report this error in the OpenBFME Discord #bug-reports forum or GitHub issues. Include your OpenBFME version."
const OPEN_LOGS_ACTION := "open_logs"
const SEND_REPORT_ACTION := "send_report"
const CRASH_QUEUE := "user://crash-queue"            # lane CRASHACT-1: reports waiting to be sent (CrashClient's queue)
const CRASH_ID := "user://crash-install-id"          # the random id the crash service limits submissions by
const CRASH_OPTION := "OpenBFMECrashReports"         # Options.ini: yes (default) = send crash reports automatically
const CRASH_SENT_NOTE := "A report of that crash is being sent to the OpenBFME team automatically (no names, folders or addresses; the log stays on your PC). You can turn this off in Options, Interface: Send crash reports automatically."
const CRASH_OFFER_NOTE := "Automatic crash reports are off. Choose Send report to send this crash's report to the OpenBFME team (no names, folders or addresses)."

var info: RefCounted        # ReleaseInfo
var logger: RefCounted      # SessionLogger
var log_path := ""          # user://logs/openbfme-....log of this run
var previous_crash_log := "" # the log of a run that did not end normally ("" = none)
var _marker := ""
var _previous_marker := {}        # lane CRASHACT-1: what the crashed run's marker said (version, profile, context, settings)
var _crash_context := {"map": "unknown", "mode": "menu", "factions": []}
var _crash_settings := {}
var _crash_uploader: Node         # CrashClient while it sends the queue
var crash_report_status := ""     # what became of the previous run's report: queued / offered / not reported: <why> (logged; tests read it)
var _watermark: CanvasLayer
var _in_game := false
var _handover: RefCounted   # UpdateHandover of this start (null: the launcher was not asked)
var _update_note: Label
var _update_timer: Timer
var _update_watched := 0.0


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


func _set_window_title() -> void:
	await get_tree().process_frame
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_title(String(ProjectSettings.get_setting("application/config/name", "OpenBFME")))


func _ready() -> void:
	# lane UI-4: the packages run Godot's DEBUG export template on purpose (tools/release/package.sh: its crash handler writes the native backtrace
	# into the session log), and that template titles the window "<name> (DEBUG)". The title is the project's name, "OpenBFME", in every
	# package. (Retail RotWK titles its window with gi.dat's GameName or the installer's registry DisplayName, RW 0x640ED0 / CreateWindowExW at
	# RW 0x40243D; OpenBFME is not that program, so it keeps its own name.)
	_set_window_title.call_deferred() # after the engine's own start-up titling
	if info == null:
		return
	_add_watermark()
	if OS.get_name() == "Windows":
		# Autoload ready runs after Godot's native initialization.
		var crash_error: String = info.install_crash_handler()
		if not crash_error.is_empty():
			printerr("RELEASE Windows crash diagnostics unavailable: ", info.redact(crash_error))
		print("RELEASE local minidumps: user://logs/minidumps (private process data; never uploaded)")
		# stop S-1923 (what is left of it since win-crash-1's minidumps; DEVPORT-6 keeps it reported): the dump names the extension's frames
		# by offset only, mapped later with the build's private symbols
		print("RELEASE [S-1923] a crash writes a local minidump (user://logs/minidumps, every thread); it names the extension's frames as openbfme.windows.template_debug.x86_64.dll+<offset> (mapped later with the build's private symbols)")
	if _has_arg("--open-logs"):
		# lane WINCRASH-1: open the log folder (the newest earlier log selected) and quit without starting the game
		var logs := _session_logs()
		logs.erase(log_path.get_file())
		print("RELEASE --open-logs: ", log_path_for_user(LOG_DIR + "/" + logs[-1] if not logs.is_empty() else LOG_DIR))
		open_log_folder(LOG_DIR + "/" + logs[-1] if not logs.is_empty() else LOG_DIR)
		get_tree().quit.call_deferred(0)
		return
	if (_has_arg("--crash-test") or _has_arg("--crash-test-worker")) and OS.is_debug_build():
		_crash_test.call_deferred()
	_crash_start.call_deferred()


## lane CRASHACT-1: the previous run's crash (its report queued, or offered when automatic reports are off), then the queue sent in the background.
## lane PLAY-1: a scripted run (--auto) never waits for a modal dialog (a killed test run left the marker and the next windowed run stopped at the alert)
func _crash_start() -> void:
	var eligible := crash_reports_eligible()
	var auto := crash_auto_enabled() if eligible else false
	var note := ""
	var offer := {}
	if not previous_crash_log.is_empty():
		if not eligible or not _previous_marker.get("eligible", false):
			# Sol r1: the crashed run must have been a player's run too (a killed --auto / headless / development run is no crash report)
			crash_report_status = "not reported: not a player's build (an export template, windowed, not --auto)"
		else:
			var report := crash_report(previous_crash_log, _previous_marker)
			if not report.ok:
				crash_report_status = "not reported: " + report.error
			elif auto:
				var queued := crash_enqueue(report.wire)
				crash_report_status = "queued" if queued.is_empty() else "not reported: " + queued
				note = CRASH_SENT_NOTE if queued.is_empty() else ""
			else:
				crash_report_status = "offered"
				offer = report
				note = CRASH_OFFER_NOTE
		print("RELEASE CRASH the previous run's report: ", crash_report_status)
		if not _headless() and not OS.get_cmdline_user_args().has("--auto"):
			_message("OpenBFME did not close normally last time (a crash?).\n\nThe log of that run is:\n%s\n\n%s%s" % [
				_folder_and_file(log_path_for_user(previous_crash_log)), REPORT_HINT, "" if note.is_empty() else "\n\n" + note],
				"OpenBFME", previous_crash_log, false, offer)
	if auto:
		upload_crash_reports()


## lane CRASHACT-1: crash reports leave this machine only from a player's build: an export template, windowed, not a scripted (--auto) run; a test
## that sets CrashClient's loopback endpoint stands in for one (its reports can only go to 127.0.0.1)
func crash_reports_eligible() -> bool:
	if not OS.get_environment(CrashClient.TEST_ENDPOINT_ENV).is_empty():
		return true
	return OS.has_feature("template") and not _headless() and not _has_arg("--auto")


## lane CRASHACT-1: Options.ini OpenBFMECrashReports (Options > Interface, "Send crash reports automatically"): on unless the player turned it off.
## A file that exists but cannot be read counts as off (said in the log): the player's choice is not known.
func crash_auto_enabled() -> bool:
	var path := _crash_options_path()
	if path.is_empty() or not FileAccess.file_exists(path):
		return GameOptions.value_of({}, CRASH_OPTION) == "yes"
	var read: Dictionary = ClassDB.class_call_static("AptMenuPlayer", "read_options_file", path)
	if not read.get("ok", false):
		printerr("RELEASE CRASH automatic reports off for this run: Options.ini could not be read (%s)" % info.redact(str(read.get("error", "?"))))
		return false
	var problems := []
	var value := GameOptions.value_of(read.get("values", {}), CRASH_OPTION, problems)
	for p in problems:
		printerr("RELEASE CRASH Options.ini: ", p)
	return value == "yes"


func _crash_options_path() -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--options-file="):
			return a.trim_prefix("--options-file=")
	if not ClassDB.class_exists("AptMenuPlayer"):
		return ""
	return str(ClassDB.class_call_static("AptMenuPlayer", "default_options_path"))


## lane CRASHACT-1: the report of the run whose session log is `log_file` (marker: what its marker said). -> CrashClient.build's result
func crash_report(log_file: String, marker: Dictionary) -> Dictionary:
	if info == null:
		return {"ok": false, "error": "crash.no_extension (no privacy filter)"}
	var tail := CrashClient.read_tail(log_file)
	if not tail.ok:
		return tail
	var version := str(marker.get("version", "unknown"))
	var facts := {
		"version": version,
		"os_name": OS.get_name(),
		"os_version": OS.get_version(),
		"cpu_name": OS.get_processor_name(),
		"gpu_name": RenderingServer.get_video_adapter_name(),
		"context": marker.get("context", {}),
		"settings": marker.get("settings", {}),
		"header": ["OpenBFME crash report: the game, %s, build %s, profile %s, simulation %s" % [OS.get_name(),
			str(marker.get("version_line", version)), str(marker.get("profile", "unknown")), str(marker.get("simulation", "unknown"))],
			"the last lines of the session log %s follow" % log_file.get_file()],
	}
	return CrashClient.build(facts, tail.lines, Callable(info, "redact"))


## lane CRASHACT-1: puts a report into the queue. "" or why not (logged)
func crash_enqueue(wire: PackedByteArray) -> String:
	var r := CrashClient.enqueue(CRASH_QUEUE, wire)
	for d in r.dropped:
		print("RELEASE CRASH report %s dropped: %s" % d)
	if not r.ok:
		printerr("RELEASE CRASH report not queued: ", r.error)
		return r.error
	print("RELEASE CRASH report queued: ", r.path.get_file())
	return ""


## lane CRASHACT-1: sends the queue in the background (never waits; a quit cancels it, the reports stay). Returns the client (its `finished`
## signal carries the summary), or null while a send already runs.
func upload_crash_reports() -> Node:
	if _crash_uploader != null:
		return null
	_crash_uploader = CrashClient.new()
	_crash_uploader.name = "CrashUpload"
	_crash_uploader.log_line = func(text: String) -> void: print("RELEASE ", text)
	add_child(_crash_uploader)
	_crash_uploader.finished.connect(func(_summary: Dictionary) -> void:
		_crash_uploader.queue_free()
		_crash_uploader = null, CONNECT_DEFERRED)
	_crash_uploader.upload_queue.call_deferred(CRASH_QUEUE, CRASH_ID) # deferred: a caller awaiting `finished` is listening by then
	return _crash_uploader


## lane CRASHACT-1: what game.gd says about the game on screen (map, mode, factions; window size, vsync), kept in the marker for the report
func set_crash_context(context: Dictionary, settings: Dictionary) -> void:
	_crash_context = context.duplicate(true)
	_crash_settings = settings.duplicate(true)
	_write_marker()


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


## lane VERSION-1: the version as players read it, "OpenBFME Alpha 0.4.5 (<date>)" (the log and the error box keep version_line(): the tag)
func display_line() -> String:
	return info.display_line() if info != null else "OpenBFME (no extension)"


## Shows the error and the log file (with the Open log folder button), then quits with exit code 1 when the player closes it (headless:
## printed only, quits at once). The game is paused while the message is up.
func fatal(message: String) -> void:
	printerr("GAME FATAL: ", message)
	if _headless():
		get_tree().quit(1)
		return
	if info == null:
		# lane GLIBC-1: without the extension there is no session log and no message panel; the system's own dialog, then quit
		OS.alert(fatal_text(message, "", ""), "OpenBFME error")
		get_tree().quit(1)
		return
	var where: String = _folder_and_file(log_path_for_user(log_path)) if not log_path.is_empty() else ""
	get_tree().paused = true
	# lane CRASHACT-1: a fatal error is reported like a crash (queued now, sent at the next start; or offered when automatic reports are off)
	var note := ""
	var offer := {}
	if crash_reports_eligible() and not log_path.is_empty():
		var report := crash_report(log_path, _marker_data())
		if not report.ok:
			printerr("RELEASE CRASH fatal error not reported: ", report.error)
		elif crash_auto_enabled():
			note = "\n\nA report of this error will be sent to the OpenBFME team at the next start." if crash_enqueue(report.wire).is_empty() else ""
		else:
			offer = report
			note = "\n\n" + CRASH_OFFER_NOTE
	_message(fatal_text(message, version_line(), where) + note, "OpenBFME error", log_path, true, offer)


static func fatal_text(message: String, version: String, log_file: String) -> String:
	var text := "%s\n\nOpenBFME cannot continue.\n\n" % message
	if not version.is_empty():
		text += "Version: %s\n" % version
	if log_file.is_empty():
		return text + NO_LOG_REPORT_HINT
	return text + "Log file: %s\n\n%s" % [log_file, REPORT_HINT]


## lane GLIBC-1: what to do when the openbfme extension did not load. A package (Godot's export template): update it with the launcher or
## report it; on Linux the usual cause is a system GLIBC older than the build's (v0.3.0-preview.6 on Pop!_OS 22.04: "version `GLIBC_2.38'
## not found"). A developer's tree: build the extension (build.bat is Windows only).
static func extension_hint() -> String:
	if OS.has_feature("template"):
		var hint := "Update OpenBFME with the launcher. If it still will not start, report this error in the OpenBFME Discord #bug-reports forum or GitHub issues."
		if OS.get_name() == "Linux":
			return "This Linux system may be too old for this build. Update Linux and try again. " + hint
		return hint
	if OS.get_name() == "Windows":
		return "run build.bat"
	return "build it first (cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build engine/build --target openbfme, see README.md)"


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


## The game's file-manager policy (the external-open helper owns the OS effect).
## `path` must lie inside the user data folder after normalising (no URL, no UNC path, no ".." escape).
func _show_in_file_manager(path: String) -> Error:
	var root := OS.get_user_data_dir().replace("\\", "/").simplify_path()
	var target := path.replace("\\", "/").simplify_path()
	if root.is_empty() or target.contains("..") or target.contains("://") or not (target == root or target.begins_with(root + "/")):
		printerr("RELEASE refused to show a path outside the user data folder in the file manager")
		return ERR_INVALID_PARAMETER
	return preload("res://scripts/external_open.gd").show_in_file_manager(target)


## Lane ONEZIP-1: asks the launcher beside the game which version this start plays (at most UpdateHandover.WAIT_MS). true when the
## launcher started another installed version: the caller quits. Otherwise this game starts; why the launcher was not asked, or what
## it answered, is printed ("UPDATE ..."), and the update check's later state is shown in the menus' corner.
func update_handover(scripted: bool) -> bool:
	var why := UpdateHandover.why_not(scripted)
	if why != "":
		print("UPDATE the launcher is not asked for updates: ", why)
		return false
	_handover = UpdateHandover.new()
	why = _handover.begin(OS.get_cmdline_user_args())
	if why != "":
		_handover = null
		print("UPDATE not checked: ", why)
		set_update_note("Updates were not checked: " + why)
		return false
	var answer := {"state": "waiting", "text": ""}
	while answer.state == "waiting":
		await get_tree().process_frame
		answer = _handover.poll()
	if answer.state == "switched":
		print("UPDATE ", answer.text, ": this game closes")
		return true
	if answer.text != "":
		print("UPDATE ", answer.text)
		set_update_note(answer.text)
	_update_timer = Timer.new()
	_update_timer.wait_time = UPDATE_POLL_SECONDS
	_update_timer.timeout.connect(_on_update_status)
	add_child(_update_timer)
	_update_timer.start()
	return false


func _on_update_status() -> void:
	_update_watched += UPDATE_POLL_SECONDS
	var s: Dictionary = _handover.status()
	if not s.is_empty():
		print("UPDATE ", s.text)
		set_update_note(s.text)
		if s.final:
			_update_timer.stop()
			if String(s.text).begins_with("Up to date"):
				get_tree().create_timer(UPDATE_NOTE_SECONDS).timeout.connect(set_update_note.bind(""))
	elif _update_watched > UPDATE_WATCH_SECONDS:
		_update_timer.stop()


## the update line under the version in the menus' corner ("" removes it)
func set_update_note(text: String) -> void:
	if _update_note != null:
		_update_note.text = text
		_update_note.visible = not text.is_empty()


## Lane MENUFIX-1: the version corner (the version, the Logs link, the update note) in the menus' look (scripts/menu_text.gd): game.gd calls it once the
## shell's fonts are there and whenever the window's size changes. The lines stack from the window's top right, MARGIN_PX stage pixels in.
func style_corner(shell: Object, size: Vector2, errors: Array) -> void:
	if _watermark == null:
		return
	var menu_text: GDScript = load("res://scripts/menu_text.gd")
	var k: float = menu_text.scale_of(size)
	var margin := roundf(menu_text.MARGIN_PX * k)
	var plate: StyleBoxFlat = _update_note.get_theme_stylebox("normal")
	plate.set_content_margin_all(roundf(4 * k))
	_update_note.custom_minimum_size = Vector2(roundf(270 * k), 0)
	var y := margin
	for line: Control in [_watermark.get_node("Version"), _watermark.get_node("OpenLogs"), _update_note]:
		var h: float = menu_text.apply(line, shell, size, menu_text.CORNER_PX, errors)
		line.offset_right = -margin
		line.offset_top = y
		line.offset_bottom = y
		y += h


## game.gd: true while a game is on screen (the version corner is shown in the menus only).
func set_in_game(in_game: bool) -> void:
	if in_game == _in_game:
		return
	_in_game = in_game
	if _watermark != null:
		_watermark.visible = not in_game
	if not in_game and _crash_context.get("mode") != "menu":
		set_crash_context({"map": "unknown", "mode": "menu", "factions": []}, _crash_settings) # lane CRASHACT-1


## Mounts the retail archives (RetailFileSystem `fs`) from the environment, the remembered folders or the first-run screen. Returns the
## mount report ({ok, errors, ...}); with ok false the caller fails with the errors. PR #77: when the mount took language archives no pin covers,
## the details are logged and the language warning (scripts/release/language_warning.gd) is shown before the game goes on.
func mount_retail(fs: RefCounted) -> Dictionary:
	var mount: Dictionary = await _mount_retail_folders(fs)
	# lane SDK-1: the asset pack models that replace nothing or do not fit the model they replace (left out; the game's model is drawn)
	for note in mount.get("asset_pack_notes", PackedStringArray()):
		print("RELEASE asset packs: ", note)
	if mount.get("ok", false) and mount.has("asset_pack_notes"):
		_write_asset_pack_report(fs) # lane SDK-1b (b)
	if mount.get("ok", false) and await language_warning(mount):
		mount["quit"] = true # --language-warning-shot: the capture is saved, the game quits (game.gd)
	return mount


## PR #77: the warning for an install in a language OpenBFME has not checked: logged always; shown in a window once per install (until the player
## ticks "Don't show this again"), not in a headless or scripted (--auto) run. --language-warning-shot=<png> (a proof capture): the box is shown even
## in a scripted run, the window is saved to that file, and the game quits (returns true).
func language_warning(mount: Dictionary) -> bool:
	var w := LanguageWarning.warning_of(mount)
	if w.is_empty():
		return false
	for line in LanguageWarning.log_lines(mount):
		print(line)
	var shot := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--language-warning-shot="):
			shot = a.trim_prefix("--language-warning-shot=")
	if _headless() or (OS.get_cmdline_user_args().has("--auto") and shot.is_empty()):
		print("LANGUAGE the warning box is not shown in a headless or scripted run")
		return false
	if shot.is_empty() and LanguageWarning.dismissed(mount):
		print("LANGUAGE the player chose not to see the warning again for this install")
		return false
	var box: AcceptDialog = LanguageWarning.make_dialog(mount)
	if not shot.is_empty():
		# the capture renders the box at the menus' size in a viewport of its own (the window may not have its size yet this early)
		var stage := SubViewport.new()
		stage.size = Vector2i(1280, 720)
		stage.gui_embed_subwindows = true
		stage.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var backdrop := ColorRect.new()
		backdrop.color = Color(0.05, 0.05, 0.06)
		backdrop.size = Vector2(1280, 720)
		stage.add_child(backdrop)
		add_child(stage)
		stage.add_child(box)
		box.popup_centered()
		for i in 30:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var image := stage.get_texture().get_image()
		var err := image.save_png(shot)
		print("LANGUAGE --language-warning-shot: ", shot.get_file(), " ", image.get_width(), "x", image.get_height(), " ", error_string(err))
		stage.queue_free()
		return true
	add_child(box)
	box.popup_centered()
	await box.tree_exiting
	return false


func _mount_retail_folders(fs: RefCounted) -> Dictionary:
	if OS.get_cmdline_user_args().has("--game=bfme1"):
		if not mod_args(OS.get_cmdline_user_args(), OS.get_environment("OPENBFME_MOD")).is_empty():
			return {"ok": false, "errors": ["-mod is RotWK's switch (lane MODS-1); BFME1 mode mounts no mods"]}
		var install := OS.get_environment("BFME1_INSTALL")
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--bfme1-install="): install = arg.trim_prefix("--bfme1-install=")
		if install.is_empty():
			# community PR #36: downloaded-bfme1.cfg is the launcher's optional BFME1 download (launcher aio_install.gd BFME1_MARKER)
			for file in ["user://install-paths.cfg", "user://downloaded-installs.cfg", "user://downloaded-bfme1.cfg"]:
				if not FileAccess.file_exists(file): continue
				for line in FileAccess.get_file_as_string(file).split("\n"):
					if line.begins_with("BFME1_INSTALL="):
						install = line.trim_prefix("BFME1_INSTALL=").trim_suffix("\r")
				if not install.is_empty(): break
		return fs.mount_bfme1_path(install)
	var setup: RefCounted = ClassDB.instantiate("InstallSetup")
	var conf: Dictionary = setup.configured()
	var args := OS.get_cmdline_user_args()
	fs.set_mods(mod_args(args, OS.get_environment("OPENBFME_MOD"))) # lane MODS-1: RotWK's -mod, mounted over the verified game files
	fs.set_install_mods(install_mod_args(args, OS.get_environment("OPENBFME_MOD_INSTALL"))) # lane MODS-2: mods installed into the game folder
	var engines := PackedStringArray() # lane MODS-3: the modified game binaries of the mod (Common/ModEngineFeatures.h): their features turn on
	for a in args:
		if a.begins_with("--mod-engine="):
			engines.push_back(a.trim_prefix("--mod-engine="))
	fs.set_mod_engines(engines)
	# lane SDK-0: cosmetic asset packs (drawing only; engine GameClient/AssetPacks.h): every folder in user://asset-packs (name order), then
	# each --asset-pack=FOLDER; a pack with a problem stops the start with its message rather than being skipped
	var packs: Dictionary = fs.set_asset_packs(asset_pack_folders(args))
	if not packs.ok:
		return {"ok": false, "errors": packs.errors}
	if not packs.packs.is_empty():
		print("RELEASE asset packs: ", ", ".join(packs.packs), " (", packs.textures, " textures, ", packs.models, " models)")
		for note in packs.notes:
			print("RELEASE asset packs: ", note)
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


# lane MODS-1: the -mod operands in command-line order: "-mod PATH" (retail's spelling), "--mod=PATH", and OPENBFME_MOD before them. A relative
# path is taken below the user data folder, as RotWK does (engine Common/ModMount.h).
static func mod_args(args: PackedStringArray, env: String) -> PackedStringArray:
	var out := PackedStringArray()
	if not env.is_empty():
		out.push_back(env)
	var i := 0
	while i < args.size():
		var a := args[i]
		if a == "-mod" and i + 1 < args.size():
			out.push_back(args[i + 1])
			i += 1
		elif a.begins_with("--mod="):
			out.push_back(a.trim_prefix("--mod="))
		i += 1
	return out


# lane MODS-2: the install-folder mod folders, highest priority first (an All In One package: its enhancements, then the package, then the
# patches it depends on): each --mod-install=FOLDER in command-line order, else OPENBFME_MOD_INSTALL; empty = none
static func install_mod_args(args: PackedStringArray, env: String) -> PackedStringArray:
	var out := PackedStringArray()
	for a in args:
		if a.begins_with("--mod-install="):
			out.push_back(a.trim_prefix("--mod-install="))
	if out.is_empty() and not env.is_empty():
		out.push_back(env)
	return out


# lane SDK-0: the asset pack folders: user://asset-packs/<pack>/ in name order (drag and drop), then --asset-pack=FOLDER in command-line order.
# Lane SDK-1: a pack named in user://asset-packs/disabled.txt (one folder name per line; the launcher's Mods page writes it) is left out.
static func asset_pack_folders(args: PackedStringArray) -> PackedStringArray:
	var out := PackedStringArray()
	var root := ProjectSettings.globalize_path("user://asset-packs")
	var names := DirAccess.get_directories_at(root) if DirAccess.dir_exists_absolute(root) else PackedStringArray()
	names.sort()
	var off := disabled_asset_packs(root)
	for n in names:
		if off.has(n.to_lower()):
			print("RELEASE asset packs: ", n, " is turned off (asset-packs/disabled.txt)")
			continue
		out.push_back(root.path_join(n))
	for a in args:
		if a.begins_with("--asset-pack="):
			out.push_back(a.trim_prefix("--asset-pack="))
	return out


# lane SDK-1b (b): user://asset-packs/report.json, what the launcher's Asset packs card shows: per pack, the models left out (they do not fit
# the game's), each with its reason; written at every start that mounted packs
static func _write_asset_pack_report(fs: RefCounted) -> void:
	var report: Dictionary = fs.get_asset_pack_report()
	report["format"] = "openbfme.asset-pack-report 1"
	report["checked"] = Time.get_datetime_string_from_system(true)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("user://asset-packs"))
	var f := FileAccess.open("user://asset-packs/report.json", FileAccess.WRITE)
	if f == null:
		printerr("RELEASE asset packs: cannot write user://asset-packs/report.json: ", error_string(FileAccess.get_open_error()))
		return
	f.store_string(JSON.stringify(report, "\t"))
	var counts := PackedStringArray()
	for p in report.packs:
		counts.push_back("%s: %d models not used" % [p.name, p.misfits.size()])
	print("RELEASE asset packs: report written (", "; ".join(counts), ")")


# lane SDK-1: the pack folder names (lower case) listed in <root>/disabled.txt; blank lines and lines starting with # are skipped
static func disabled_asset_packs(root: String) -> Dictionary:
	var off := {}
	var path := root.path_join("disabled.txt")
	if not FileAccess.file_exists(path):
		return off
	for line in FileAccess.get_file_as_string(path).split("\n"):
		var n := line.strip_edges()
		if not n.is_empty() and not n.begins_with("#"):
			off[n.to_lower()] = true
	return off


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


## lane JOINVER-1: a join refused for a different build: the plain words, OK (the game closes, as after any failed online start) and "Copy details",
## which puts both builds' tags and profile ids on the clipboard for a bug report (the box stays)
func join_refused(message: String, details: String) -> void:
	printerr("GAME JOIN refused: ", details.replace("\n", "; "))
	if _headless():
		get_tree().quit(1)
		return
	get_tree().paused = true
	var box := AcceptDialog.new()
	box.name = "JoinRefused"
	box.title = "Can't join this game"
	box.dialog_text = message
	box.dialog_autowrap = true
	box.min_size = Vector2i(720, 0)
	box.exclusive = true
	box.process_mode = Node.PROCESS_MODE_ALWAYS
	box.ok_button_text = "OK"
	box.add_button("Copy details", false, &"copy_details")
	box.custom_action.connect(func(action: StringName) -> void:
		if action == &"copy_details":
			DisplayServer.clipboard_set(details)
			box.dialog_text = message + "\n\nCopied to the clipboard:\n" + details)
	var done := func() -> void:
		box.queue_free()
		get_tree().quit(1)
	box.confirmed.connect(done)
	box.canceled.connect(done)
	add_child(box)
	box.popup_centered()


## A message box with "Open log folder" (opens the folder with `log_file` selected, the box stays) and OK. `quit_after`: OK / closing quits with
## exit code 1 (fatal). It works while the tree is paused.
## lane CRASHACT-1: `offer` (a CrashClient report): a "Send report" button queues and sends it (once), and says how that went.
func _message(text: String, title: String, log_file: String, quit_after: bool, offer := {}) -> void:
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
	var send: Button = box.add_button("Send report", false, SEND_REPORT_ACTION) if offer.get("ok", false) else null
	box.custom_action.connect(func(action: StringName) -> void:
		if action == OPEN_LOGS_ACTION:
			open_log_folder(log_file)
		elif action == SEND_REPORT_ACTION and send != null and not send.disabled:
			send.disabled = true
			var said := await send_offered_report(offer)
			if is_instance_valid(box): # the player may have closed the box while it sent
				box.dialog_text = text + "\n\n" + said)
	var done := func() -> void:
		box.queue_free()
		if quit_after:
			get_tree().quit(1)
	box.confirmed.connect(done)
	box.canceled.connect(done)
	add_child(box)
	box.popup_centered()


## lane CRASHACT-1: the player chose "Send report": the report is queued and the queue sent now. Returns what to tell the player.
func send_offered_report(offer: Dictionary) -> String:
	var queued := crash_enqueue(offer.wire)
	if not queued.is_empty():
		return "The report could not be saved for sending (%s)." % queued
	crash_report_status = "queued (sent by the player)"
	var client := upload_crash_reports()
	if client == null:
		return "The report is queued; reports are being sent now."
	var summary: Dictionary = await client.finished
	if summary.error.is_empty() and summary.kept == 0:
		return "Report sent. Thank you."
	return "The report could not be sent now (%s). It stays queued and is sent at a later start." % summary.error


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
	if _has_arg("--crash-test-worker"):
		info.crash_test_worker()
	else:
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
	_write_marker()
	_prune_logs()


## the marker: the process id (the crash check), and lane CRASHACT-1's facts for the report should this run crash
func _marker_data() -> Dictionary:
	var uber := "unknown"
	var path := _crash_options_path()
	if not path.is_empty() and FileAccess.file_exists(path):
		var read: Dictionary = ClassDB.class_call_static("AptMenuPlayer", "read_options_file", path)
		if read.get("ok", false):
			uber = "uber" if GameOptions.value_of(read.get("values", {}), "OpenBFMEUberPerformance") == "yes" else "classic"
	return {"pid": OS.get_process_id(), "eligible": crash_reports_eligible(), "version": info.version(), "version_line": info.version_line(), "profile": info.engine_id(),
		"simulation": uber, "context": _crash_context, "settings": _crash_settings}


func _write_marker() -> void:
	if _marker.is_empty() or info == null:
		return
	var f := FileAccess.open(_marker, FileAccess.WRITE)
	if f == null:
		printerr("RELEASE crash marker not written: ", error_string(FileAccess.get_open_error()))
		return
	f.store_string(JSON.stringify(_marker_data()))
	f.close()


func _find_previous_crash() -> void:
	var dir := DirAccess.open(LOG_DIR)
	if dir == null:
		return
	var newest := ""
	for file in dir.get_files():
		if not file.ends_with(".log.running") or not _is_session_log_name(file.trim_suffix(".running")):
			continue
		var marker := LOG_DIR + "/" + file
		var f := FileAccess.open(marker, FileAccess.READ)
		var data: Variant = null
		var parser := JSON.new()
		if f != null and f.get_length() <= 4096 and parser.parse(f.get_as_text()) == OK:
			data = parser.data
		var pid := -1
		if data is Dictionary and typeof(data.get("pid")) in [TYPE_INT, TYPE_FLOAT]:
			var value: float = data.pid
			if is_finite(value) and value >= 1 and value <= 2147483647 and value == floor(value):
				pid = int(value)
		if f != null:
			f.close()
		if pid > 0 and pid != OS.get_process_id() and info.process_alive(pid):
			continue # another OpenBFME that is still running (two windows of a LAN test)
		DirAccess.remove_absolute(ProjectSettings.globalize_path(marker))
		var crashed_log := marker.trim_suffix(".running")
		if FileAccess.file_exists(crashed_log) and crashed_log > newest:
			newest = crashed_log
			_previous_marker = _marker_facts(data)
	previous_crash_log = newest


## lane CRASHACT-1: the report's facts from a crashed run's marker, typed (a marker is untrusted text: anything else is left out)
static func _marker_facts(data: Variant) -> Dictionary:
	var out := {}
	if not data is Dictionary:
		return out
	for key in ["version", "version_line", "profile", "simulation"]:
		if typeof(data.get(key)) == TYPE_STRING:
			out[key] = str(data[key]).left(256)
	out["eligible"] = data.get("eligible") == true
	for key in ["context", "settings"]:
		if typeof(data.get(key)) == TYPE_DICTIONARY:
			out[key] = data[key]
	return out


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
	# lane RELNAME-1 (owner, 2026-10-10): an alpha build, no longer "test build"; "unofficial" stays (not made or endorsed by EA).
	# Lane VERSION-1: "OpenBFME Alpha 0.4.5 (<date>)  -  unofficial build" (the line already says Alpha)
	var shown := display_line()
	label.text = shown + ("  -  unofficial build" if shown.begins_with("OpenBFME Alpha ") else "  -  unofficial alpha build")
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
	label.add_to_group("player_message_obstacles")
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
	logs.add_to_group("player_message_obstacles")
	# lane ONEZIP-1: the update check's state (a game started directly), under the Logs link
	_update_note = Label.new()
	# a dark plate behind it: the line is longer than the version text and lies over the menu's art
	var plate := StyleBoxFlat.new()
	plate.bg_color = Color(0, 0, 0, 0.6)
	plate.set_corner_radius_all(4)
	plate.set_content_margin_all(6)
	_update_note.add_theme_stylebox_override("normal", plate)
	_update_note.add_theme_font_size_override("font_size", 13)
	_update_note.add_theme_color_override("font_color", Color(1, 1, 1, 0.95))
	_update_note.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	_update_note.add_theme_constant_override("shadow_offset_x", 1)
	_update_note.add_theme_constant_override("shadow_offset_y", 1)
	_update_note.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	_update_note.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	_update_note.offset_right = -8
	_update_note.offset_top = 44
	_update_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_update_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_update_note.custom_minimum_size = Vector2(380, 0)
	_update_note.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_update_note.name = "UpdateNote"
	_update_note.set_meta("game_text", true)
	_update_note.visible = false
	_watermark.add_child(_update_note)
	_update_note.add_to_group("player_message_obstacles")
	_watermark.name = "ReleaseWatermark"
	add_child(_watermark)
