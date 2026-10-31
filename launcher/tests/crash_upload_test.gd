## Lane CRASHACT-1: the launcher's crash reports (scripts/core/launcher_crash.gd with scripts/net/crash_client.gd). Run by
## tools/release/test_crash_upload.py with OPENBFME_GAME_USER_DIR (the game's Options.ini) and, for --stage=upload, the fake crash service
## (OPENBFME_CRASH_TEST_ENDPOINT / _KEY, a loopback port that answers 201). Prints CRASHUP LAUNCHER OK <n> checks.
extends SceneTree

const LauncherCrash := preload("res://scripts/core/launcher_crash.gd")
const CrashClient := preload("res://scripts/net/crash_client.gd")
const Log := preload("res://scripts/core/launcher_log.gd")

var checks := 0
var failed := 0


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("CRASHUP LAUNCHER FAIL ", label)


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	Log.open()
	var upload := OS.get_cmdline_user_args().has("--stage=upload")
	_marker()
	var r := _report()
	_toggle()
	if upload and r.get("ok", false):
		await _upload(r)
	print("CRASHUP LAUNCHER %s %d checks" % ["OK" if failed == 0 else "FAILED", checks])
	quit(0 if failed == 0 else 1)


func _marker() -> void:
	LauncherCrash.remove_marker()
	check(LauncherCrash.take_previous().is_empty(), "no marker: the last launcher ended normally")
	LauncherCrash.write_marker("v9.9.9")
	var taken := LauncherCrash.take_previous()
	check(taken.get("pid") == OS.get_process_id() and taken.get("version") == "v9.9.9" and not FileAccess.file_exists(LauncherCrash.MARKER),
		"a marker left behind: its process and version, removed once read: %s" % [taken])
	var f := FileAccess.open(LauncherCrash.MARKER, FileAccess.WRITE)
	f.store_string("{broken")
	f.close()
	check(LauncherCrash.take_previous().get("pid") == 0, "an unreadable marker still counts as a crash (its process unknown)")
	check(not LauncherCrash.eligible(false) or not OS.get_environment(CrashClient.TEST_ENDPOINT_ENV).is_empty(), "a development launcher does not report")
	check(LauncherCrash.eligible(true), "a packaged launcher reports")


func _report() -> Dictionary:
	var path := OS.get_user_data_dir().path_join("crashup-launcher.log")
	var home := OS.get_environment("HOME")
	var f := FileAccess.open(path, FileAccess.WRITE)
	for line in [
		"2026-10-27T10:00:00 [111] LAUNCHER another run: alice@example.com",
		"2026-10-27T10:00:01 [4242] LAUNCHER OpenBFME Launcher v9.9.9 started",
		"2026-10-27T10:00:02 [4242] LAUNCHER game folder %s/Games/rotwk, lobby 203.0.113.9" % home,
		"2026-10-27T10:00:03 [4242] LAUNCHER download C:\\Users\\Alice\\AppData\\Roaming\\OpenBFMELauncher\\downloads\\x.part token=abc123",
		"2026-10-27T10:00:04 [4242] ERROR: something went wrong",
	]:
		f.store_line(line)
	f.close()
	var r := LauncherCrash.report({"pid": 4242, "version": "v9.9.9"}, path)
	check(r.ok, "the launcher's report builds: %s" % r.get("error", ""))
	if not r.ok:
		return r
	var text: String = r.json.get_string_from_utf8()
	var data: Dictionary = r.data
	check(data.log_tail.size() == 6 and str(data.log_tail[0]).contains("the launcher") and not text.contains("another run"),
		"the header, then only the crashed launcher's own lines: %s" % [data.log_tail])
	for secret in ["alice", "Alice", "203.0.113.9", "abc123", "example.com"]:
		check(not text.contains(secret), "no %s in the report" % secret)
	check(home.length() < 2 or not text.contains(home), "no home folder in the report")
	check(data.backtrace == ["2026-10-27T10:00:04 [4242] ERROR: something went wrong"] or str(data.backtrace[0]) == CrashClient.NO_BACKTRACE,
		"backtrace from the error lines (or an explicit unknown): %s" % [data.backtrace])
	check(data.version == "v9.9.9" and data.client.version == "v9.9.9" and data.context.mode == "menu", "version and context")
	return r


func _toggle() -> void:
	var dir := OS.get_environment("OPENBFME_GAME_USER_DIR")
	check(not dir.is_empty(), "the runner sets OPENBFME_GAME_USER_DIR")
	DirAccess.make_dir_recursive_absolute(dir)
	var options := dir.path_join("Options.ini")
	DirAccess.remove_absolute(options)
	check(LauncherCrash.automatic(), "no Options.ini: on")
	_write(options, "OpenBFMECrashReports = no\n")
	check(not LauncherCrash.automatic(), "the game's switch off: off for the launcher too")
	_write(options, "OpenBFMECrashReports = yes\n")
	check(LauncherCrash.automatic(), "the game's switch on: on")


func _write(path: String, text: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()


func _upload(r: Dictionary) -> void:
	for name in CrashClient.queued(LauncherCrash.QUEUE):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(LauncherCrash.QUEUE.path_join(name)))
	check(LauncherCrash.enqueue(r.wire).is_empty(), "queued")
	var client := LauncherCrash.upload(root)
	var summary: Dictionary = await client.finished
	check(summary.sent.size() == 1 and summary.error.is_empty() and CrashClient.queued(LauncherCrash.QUEUE).is_empty(), "sent: %s" % [summary])
