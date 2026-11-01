## Local crash markers are untrusted metadata, not report attachments or arbitrary paths.
extends SceneTree

var checks := 0
var failed := 0

class ProcessInfo extends RefCounted:
	func process_alive(pid: int) -> bool:
		return pid == 42


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("CRASH LOG FAIL ", label)


func put(name: String, contents: String) -> void:
	var f := FileAccess.open("user://logs/" + name, FileAccess.WRITE)
	if f == null:
		check(false, "fixture opened")
		return
	f.store_string(contents)
	f.close()


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("user://logs"))
	var release: Node = root.get_node("Release")
	release.info = ProcessInfo.new()
	# with the extension loaded this process has its own session marker (made at startup, after the real crash check); it is no fixture
	if not release._marker.is_empty():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(release._marker))
	put("openbfme-20260101-000000.log", "fixture log")
	for contents in ['{"pid":42}', '{"pid":-1}', '{"pid":"arbitrary text"}', '{"pid":{}}', '{"pid":1e100}', '{"pid":1.5}', '[]', '{broken', "x".repeat(4097)]:
		put("openbfme-20260101-000000.log.running", contents)
		release._find_previous_crash()
		var alive: bool = contents == '{"pid":42}'
		check(FileAccess.file_exists("user://logs/openbfme-20260101-000000.log.running") == alive, "only a live numeric PID keeps its marker")
		check(release.previous_crash_log == ("" if alive else "user://logs/openbfme-20260101-000000.log"), "stale marker resolves only to its own local log name")
	put("unrelated.log.running", '{"pid":-1,"log":"arbitrary-file"}')
	release._find_previous_crash()
	check(FileAccess.file_exists("user://logs/unrelated.log.running"), "unrelated markers ignored")
	put("openbfme-20260102-000000.log.running", '{"pid":-1,"log":"arbitrary-file","version":"arbitrary text"}')
	put("openbfme-20260102-000000.log", "fixture log")
	release._find_previous_crash()
	check(release.previous_crash_log == "user://logs/openbfme-20260102-000000.log", "legacy metadata cannot supply a path or report contents")
	for contents in ['{broken', '{"pid":-1}']:
		put("openbfme-20260103-000000.log.running", contents)
		release._find_previous_crash()
		check(release.previous_crash_log.is_empty(), "missing log makes malformed or stale marker inconclusive")
		check(not FileAccess.file_exists("user://logs/openbfme-20260103-000000.log.running"), "inconclusive marker cleaned up")
	check(release.REPORT_HINT.contains("#bug-reports") and release.REPORT_HINT.contains("GitHub issue") and not release.REPORT_HINT.contains("#feedback"), "attachment guidance names current reporting channels")
	print("CRASH LOG ", "OK" if failed == 0 else "FAIL", " ", checks, " checks; ", failed, " failed")
	quit(0 if failed == 0 else 1)
