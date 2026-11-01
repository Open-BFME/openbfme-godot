## Lane CRASHACT-1: the launcher's own crashes, reported like the game's (scripts/net/crash_client.gd, the game's crash client). While an
## interactive launcher runs (it holds the one-launcher lock) user://launcher.running holds its process id; a normal end removes it. A marker
## found by the next interactive launcher (holding the lock, so no other launcher runs) means the last one crashed or was killed: its report is
## that process's lines of launcher.log (each line carries the writer's process id) through Log.scrub and the crash client's filters.
## The game's Options.ini OpenBFMECrashReports (Options > Interface "Send crash reports automatically", default yes) is the one switch: on, the
## report is queued and the queue (user://crash-queue) is sent in the background at each start; off, the launcher offers "Send report" once.
## Only a packaged launcher reports (a test's loopback endpoint stands in for one); nothing waits on the network.
extends RefCounted

const CrashClient := preload("res://scripts/net/crash_client.gd")
const GameOptions := preload("res://scripts/core/game_options.gd")
const Log := preload("res://scripts/core/launcher_log.gd")

const MARKER := "user://launcher.running"
const QUEUE := "user://crash-queue"
const INSTALL_ID := "user://crash-install-id"
const OPTION := "OpenBFMECrashReports"


static func eligible(packaged: bool) -> bool:
	return packaged or not OS.get_environment(CrashClient.TEST_ENDPOINT_ENV).is_empty()


## the game's Options.ini switch: on unless it says "no"
static func automatic(options_path: String = "") -> bool:
	return GameOptions.get_value(OPTION, options_path).to_lower() != "no"


## The crashed launcher's marker ({pid, version}), removed; {} when the last launcher ended normally. Call only while holding the lock.
static func take_previous() -> Dictionary:
	if not FileAccess.file_exists(MARKER):
		return {}
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(MARKER))
	var removed := DirAccess.remove_absolute(ProjectSettings.globalize_path(MARKER))
	if removed != OK:
		Log.line("LAUNCHER CRASH marker not removed: " + error_string(removed))
	if not data is Dictionary or not typeof(data.get("pid")) in [TYPE_INT, TYPE_FLOAT]:
		Log.line("LAUNCHER CRASH marker unreadable: the last launcher did not end normally, its process unknown")
		return {"pid": 0, "version": "unknown"}
	return {"pid": int(data.pid), "version": str(data.get("version", "unknown")).left(64)}


static func write_marker(version: String) -> void:
	var f := FileAccess.open(MARKER, FileAccess.WRITE)
	if f == null:
		Log.line("LAUNCHER CRASH marker not written: " + error_string(FileAccess.get_open_error()))
		return
	f.store_string(JSON.stringify({"pid": OS.get_process_id(), "version": version}))
	f.close()


static func remove_marker() -> void:
	if FileAccess.file_exists(MARKER):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(MARKER))


## The report of the crashed launcher `previous` (take_previous): its own lines of launcher.log. -> CrashClient.build's result
static func report(previous: Dictionary, log_path: String = "") -> Dictionary:
	var tail := CrashClient.read_tail(log_path if log_path != "" else OS.get_user_data_dir().path_join("launcher.log"))
	if not tail.ok:
		return tail
	var mine := PackedStringArray()
	var tag := " [%d] " % int(previous.get("pid", 0))
	for line: String in tail.lines:
		if int(previous.get("pid", 0)) == 0 or line.contains(tag):
			mine.append(line)
	var version := str(previous.get("version", "unknown"))
	var facts := {
		"version": version,
		"os_name": OS.get_name(),
		"os_version": OS.get_version(),
		"cpu_name": OS.get_processor_name(),
		"gpu_name": RenderingServer.get_video_adapter_name(),
		"context": {"map": "", "mode": "menu", "factions": []},
		"header": ["OpenBFME crash report: the launcher, %s, build %s" % [OS.get_name(), version], "its last lines of launcher.log follow"],
	}
	return CrashClient.build(facts, mine, Callable(Log, "scrub"))


## Queues a report. "" or why not (logged)
static func enqueue(wire: PackedByteArray) -> String:
	var r := CrashClient.enqueue(QUEUE, wire)
	for d in r.dropped:
		Log.line("LAUNCHER CRASH report %s dropped: %s" % d)
	if not r.ok:
		Log.line("LAUNCHER CRASH report not queued: " + r.error)
		return r.error
	Log.line("LAUNCHER CRASH report queued: " + r.path.get_file())
	return ""


## Sends the queue in the background under `parent` (freed with it: a closing launcher drops the request, the reports stay). Returns the client.
static func upload(parent: Node) -> Node:
	var client := CrashClient.new()
	client.name = "CrashUpload"
	client.log_line = func(text: String) -> void: Log.line("LAUNCHER " + text)
	parent.add_child(client)
	client.finished.connect(func(_summary: Dictionary) -> void: client.queue_free(), CONNECT_DEFERRED)
	client.upload_queue.call_deferred(QUEUE, INSTALL_ID)
	return client
