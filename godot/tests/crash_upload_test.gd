## Lane CRASHACT-1: crash reports (scripts/crash/crash_client.gd, scripts/release/release.gd). Run by tools/release/test_crash_upload.py:
##   --stage=local   the report's privacy filtering, the queue's limits, the Options toggle, the previous run's crash on and off, offline
##   --stage=upload  against the runner's fake crash service (OPENBFME_CRASH_TEST_ENDPOINT / _KEY, a loopback port): sent, kept, dropped
## Prints CRASHUP OK <n> checks, or CRASHUP FAIL lines and exits 1.
extends SceneTree

const CrashClient := preload("res://scripts/crash/crash_client.gd")
const QUEUE := "user://crash-queue"

var checks := 0
var failed := 0
var release: Node


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("CRASHUP FAIL ", label)


func arg(prefix: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with(prefix):
			return a.trim_prefix(prefix)
	return ""


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	release = root.get_node("Release")
	# Release's own start (its deferred _crash_start, and the queue upload it may begin) finishes before the checks touch the same state
	for i in 3:
		await process_frame
	if release._crash_uploader != null:
		await release._crash_uploader.finished
		await process_frame
	var stage := arg("--stage=")
	if stage == "upload":
		await _upload()
	elif stage == "ci":
		await _ci()
	else:
		_report()
		_queue()
		await _toggle()
	print("CRASHUP %s %d checks" % ["OK" if failed == 0 else "FAILED", checks])
	quit(0 if failed == 0 else 1)


## lane CRASHACT-1: a CI host (OPENBFME_CRASH_UPLOAD=0) where a smoke run was killed: the runner planted that run's log and marker; the
## next start (this one, eligible through the runner's loopback service) reports the crash into the queue and sends nothing
func _ci() -> void:
	check(OS.get_environment(CrashClient.UPLOAD_ENV) == "0", "the runner set OPENBFME_CRASH_UPLOAD=0")
	check(release.crash_report_status == "queued", "the killed run's crash was found and queued: %s" % release.crash_report_status)
	# the start's own send already stopped (the runner reads its crash.upload_off line and the service's empty request list); once more
	var again: Dictionary = await _send()
	check(str(again.get("error", "")).begins_with("crash.upload_off") and again.sent.is_empty(),
		"a send on a CI host stops by name, nothing sent: %s" % [again])
	check(CrashClient.queued(QUEUE).size() == 1, "the report stays queued (on a CI host it is never sent)")


func _redact() -> Callable:
	if release.info != null:
		print("CRASHUP redaction through ReleaseInfo.redact")
		return Callable(release.info, "redact")
	# no extension (a local run): the home folder and the user name, as LogPrivacy does
	print("CRASHUP no extension: a stand-in for ReleaseInfo.redact")
	var home := OS.get_environment("HOME")
	var user := OS.get_environment("USER")
	return func(t: String) -> String:
		if home.length() > 1:
			t = t.replace(home, "~")
		return t.replace(user, "<user>") if user.length() > 2 else t


func _facts() -> Dictionary:
	return {"version": "v0.4.12-3-gabcdef0", "os_name": OS.get_name(), "os_version": "6.8 test", "cpu_name": "Test CPU", "gpu_name": "Test GPU",
		"context": {"map": "maps/map mp fords of isen", "mode": "skirmish", "factions": ["Men"]},
		"settings": {"resolution_width": 1920, "resolution_height": 1080, "vsync": true},
		"header": ["OpenBFME crash report: the game, test build"]}


func _strings(v: Variant, out: Array) -> Array:
	if typeof(v) == TYPE_STRING:
		out.append(v)
	elif typeof(v) == TYPE_ARRAY:
		for c in v:
			_strings(c, out)
	elif typeof(v) == TYPE_DICTIONARY:
		for k in v:
			_strings(v[k], out)
	return out


func _sample_lines() -> PackedStringArray:
	var home := OS.get_environment("HOME")
	var user := OS.get_environment("USER")
	return PackedStringArray([
		"OpenBFME v0.4.12 (2026-10-27)",
		"RELEASE session log: %s/.local/share/godot/app_userdata/OpenBFME/logs/openbfme-20261027-101010.log" % home,
		"GAME install C:\\Users\\Alice Smith\\Documents\\RotWK\\game.dat",
		"GAME mounted /" + "home/alice/Games/rotwk/lang/English.big",
		"GAME player %s joined from 203.0.113.20:8088 and fe80::1ff:fe23:4567:890a" % user,
		"GAME ONLINE mail jon.doe@example.com token=abcdef0123456789 password: hunter2",
		"GAME lobby https://api.openbfme.com/v1/rooms?key=s3cr3t",
		"GAME chat: meet me at 198.51.100.7 tonight",
		"GAME CHAT line slot 2: [Friend] meet by the river",
		"GAME BEACON state: {} lines [\"rally at the ford\"]",
		"GAME host desktop.lan answered; Object::kill at res://scripts/game.gd:120 (15:30:12)",
		"ERROR: Condition \"p_index < 0\" is true.",
		"[1] /" + "home/alice/openbfme/engine/src/GameLogic/Object.cpp:120 (Object::kill)",
		"[2] openbfme.windows.template_debug.x86_64.dll+0x1a2b3c",
	])


func _report() -> void:
	var r := CrashClient.build(_facts(), _sample_lines(), _redact())
	check(r.ok, "the report builds: %s" % r.error)
	if not r.ok:
		return
	var data: Dictionary = JSON.parse_string(r.json.get_string_from_utf8())
	check(data.keys().size() == 12 and ["backtrace", "build_kind", "client", "context", "cpu_name", "gpu_name", "log_tail", "os_name", "os_version",
		"schema_version", "settings", "version"].all(func(k: String) -> bool: return data.has(k)), "the schema's twelve fields: %s" % [data.keys()])
	check(data.client.version == data.version and data.client.platform == CrashClient.client_platform(OS.get_name()), "client = platform + version")
	check(data.settings.texture_quality == "unknown" and data.settings.shadows == "unknown" and int(data.settings.resolution_width) == 1920,
		"settings: given ones kept, missing ones 'unknown'")
	var all := _strings(data, [])
	var leaked := []
	var home := OS.get_environment("HOME")
	var user := OS.get_environment("USER")
	for s: String in all:
		for secret in ["alice", "Alice", "203.0.113", "198.51.100.7", "fe80", "example.com", "jon.doe", "hunter2", "abcdef0123456789", "s3cr3t",
				"desktop.lan", "meet me", "river", "rally"]:
			if s.contains(secret):
				leaked.append([secret, s])
		if home.length() > 1 and s.contains(home):
			leaked.append(["HOME", s])
		if user.length() > 2 and s.contains(user):
			leaked.append(["USER", s])
		if CrashClient.service_would_refuse(s):
			leaked.append(["the service would refuse", s])
	check(leaked.is_empty(), "nothing private left in the report: %s" % [leaked])
	var text: String = r.json.get_string_from_utf8()
	check(text.contains("res://scripts/game.gd:120") and text.contains("Object::kill") and text.contains("Object.cpp"),
		"resource paths, symbols and source file names stay")
	check(data.backtrace.size() == 3 and str(data.backtrace[0]).begins_with("ERROR:") and str(data.backtrace[1]).begins_with("[1]")
		and str(data.backtrace[2]).contains("dll+0x1a2b3c"), "the backtrace: the error and the frames: %s" % [data.backtrace])
	check(data.log_tail[0] == "OpenBFME crash report: the game, test build" and data.log_tail.size() == 15, "the header, then the log's lines")
	check(CrashClient.wire_problem(r.wire).is_empty() and r.wire.decompress_dynamic(-1, FileAccess.COMPRESSION_GZIP) == r.json,
		"gzip without name or time, the same JSON inside")
	# no backtrace in the log: the service still needs one entry, an explicit unknown
	var bare := CrashClient.build(_facts(), PackedStringArray(["GAME menus up"]), _redact())
	check(bare.ok and bare.data.backtrace == [CrashClient.NO_BACKTRACE], "no frames: one explicit 'unknown' entry")
	# 1000 lines: the last 200 (header included) are kept
	var many := PackedStringArray()
	for i in 1000:
		many.append("GAME line %d" % i)
	var big := CrashClient.build(_facts(), many, _redact())
	check(big.ok and big.data.log_tail.size() == CrashClient.MAX_LOG_LINES and big.data.log_tail[-1] == "GAME line 999", "the tail is capped at 200 lines")
	check(not CrashClient.build({"os_name": "Haiku"}, many, _redact()).ok, "an unsupported platform is refused by name")
	# 200 long lines gzip cannot shrink below the service's 256 KiB: the oldest lines go until it fits
	var noisy := PackedStringArray()
	for i in 200:
		noisy.append("GAME %d " % i + Marshalls.raw_to_base64(Crypto.new().generate_random_bytes(1500)).replace("/", "_"))
	var trimmed := CrashClient.build(_facts(), noisy, _redact())
	check(trimmed.ok and trimmed.wire.size() <= CrashClient.MAX_WIRE_BYTES and trimmed.data.log_tail.size() < CrashClient.MAX_LOG_LINES
		and str(trimmed.data.log_tail[-1]).begins_with("GAME 199 "), "too large: trimmed from the oldest line, still sent (%d bytes)" % trimmed.wire.size())
	check(CrashClient.service_would_refuse("C:\\Users\\Bob\\x") and CrashClient.service_would_refuse("token = x")
		and CrashClient.service_would_refuse("at 8.8.8.8 ") and not CrashClient.service_would_refuse("15:30:12 Object::kill 999.1.1.1"),
		"the mirrored service rules")


func _wire(bytes: int) -> PackedByteArray:
	var raw := Crypto.new().generate_random_bytes(bytes) # random: gzip cannot shrink it
	return raw.compress(FileAccess.COMPRESSION_GZIP)


func _clear(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(dir)):
		return
	for name in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(dir.path_join(name)))


func _queue() -> void:
	var dir := "user://crashup-queue"
	_clear(dir)
	var t := 1_800_000_000
	var dropped := []
	for i in 10:
		var r := CrashClient.enqueue(dir, _wire(100), t + i)
		check(r.ok, "queued %d: %s" % [i, r.error])
		dropped.append_array(r.dropped)
	var names := CrashClient.queued(dir)
	check(names.size() == CrashClient.QUEUE_MAX_FILES and names[0].begins_with(str(t + 2)), "8 files at most, the oldest dropped: %s" % [names])
	check(dropped.size() == 2 and str(dropped[0][1]).contains("queue is full"), "each drop named with its reason: %s" % [dropped])
	_clear(dir)
	for i in 5:
		CrashClient.enqueue(dir, _wire(240_000), t + i)
	var total := 0
	for name in CrashClient.queued(dir):
		total += FileAccess.get_file_as_bytes(dir.path_join(name)).size()
	check(CrashClient.queued(dir).size() == 4 and total <= CrashClient.QUEUE_MAX_BYTES, "the byte limit: %d files, %d bytes" % [CrashClient.queued(dir).size(), total])
	_clear(dir)
	CrashClient.enqueue(dir, _wire(100), t - 31 * 86400)
	var part := FileAccess.open(dir.path_join("1800000000-000000000000.json.gz.part"), FileAccess.WRITE)
	part.store_string("half")
	part.close()
	var pruned := CrashClient.prune(dir, t)
	check(CrashClient.queued(dir).is_empty() and pruned.dropped.size() == 2 and DirAccess.get_files_at(dir).is_empty(),
		"older than 30 days and an unfinished write go: %s" % [pruned.dropped])
	check(not CrashClient.enqueue(dir, "not gzip".to_utf8_buffer(), t).ok, "bytes that are not a report are refused")
	_clear(dir)


## the Options toggle and the previous run's crash, through Release (the extension's Options.ini reader); offline: the queue stays
func _toggle() -> void:
	if release.info == null:
		print("CRASHUP no extension: the Options toggle part is not run")
		return
	var options := arg("--options-file=")
	check(not options.is_empty(), "the runner passes --options-file")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(options))
	check(release.crash_auto_enabled(), "no Options.ini: on (the default)")
	_write(options, "OpenBFMECrashReports = no\n")
	check(not release.crash_auto_enabled(), "OpenBFMECrashReports = no: off")
	_write(options, "OpenBFMECrashReports = yes\n")
	check(release.crash_auto_enabled(), "OpenBFMECrashReports = yes: on")
	print("CRASHUP the Options toggle checks ran")
	# a previous run that crashed: its log and marker facts
	var log_file := "user://logs/openbfme-20260101-000001.log"
	_write(log_file, "\n".join(_sample_lines()) + "\n")
	OS.set_environment(CrashClient.TEST_ENDPOINT_ENV, "http://127.0.0.1:9/v1/reports") # nothing listens there: offline
	OS.set_environment(CrashClient.TEST_KEY_ENV, "k".repeat(64))
	_clear(QUEUE)
	release.previous_crash_log = log_file
	release._previous_marker = {"eligible": false, "version": "v0.4.12", "version_line": "OpenBFME v0.4.12 (2026-10-27)", "profile": "test-profile",
		"context": {"map": "maps/map mp fords of isen", "mode": "skirmish", "factions": ["Men"]}}
	release._crash_start()
	check(release.crash_report_status.begins_with("not reported: not a player's build") and CrashClient.queued(QUEUE).is_empty(),
		"a crashed run that was no player's run (killed --auto / headless / development) is not reported: %s" % release.crash_report_status)
	release._previous_marker.eligible = true
	_write(options, "OpenBFMECrashReports = no\n")
	release._crash_start()
	check(release.crash_report_status == "offered" and CrashClient.queued(QUEUE).is_empty(), "off: offered, nothing queued (%s)" % release.crash_report_status)
	_write(options, "OpenBFMECrashReports = yes\n")
	release._crash_start()
	check(release.crash_report_status == "queued" and CrashClient.queued(QUEUE).size() == 1, "on: queued at once (%s)" % release.crash_report_status)
	var client: Node = release._crash_uploader
	check(client != null, "on: the queue is being sent in the background")
	if client != null:
		var summary: Dictionary = await client.finished
		check(str(summary.error).begins_with("crash.network") and summary.kept == 1 and CrashClient.queued(QUEUE).size() == 1,
			"offline: the report stays queued, the failure named: %s" % [summary])
	var report: Dictionary = release.crash_report(log_file, release._previous_marker)
	var tail_text: String = report.json.get_string_from_utf8() if report.ok else ""
	check(report.ok and tail_text.contains("test-profile") and tail_text.contains("v0.4.12") and not tail_text.contains("alice"),
		"the game's report: build and profile in the header, filtered")
	OS.set_environment(CrashClient.TEST_ENDPOINT_ENV, "")
	release.previous_crash_log = ""
	_clear(QUEUE)


func _write(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()


## the runner's fake service answers 201, 201, 429, 400, 201, then 201 after 4 s, then never
func _upload() -> void:
	check(not OS.get_environment(CrashClient.TEST_ENDPOINT_ENV).is_empty(), "the runner set the test endpoint")
	_clear(QUEUE)
	for i in 3:
		var r := CrashClient.build(_facts(), _sample_lines(), _redact())
		CrashClient.enqueue(QUEUE, r.wire, int(Time.get_unix_time_from_system()) - 10 + i)
	var first: Dictionary = await _send()
	check(first.sent.size() == 2 and first.kept == 1 and first.error.is_empty() and CrashClient.queued(QUEUE).size() == 1,
		"two a run sent (201), the third kept for the next start: %s" % [first])
	var limited: Dictionary = await _send()
	check(limited.sent.is_empty() and limited.kept == 1 and str(limited.error).contains("429"), "429: kept, named: %s" % [limited])
	var refused: Dictionary = await _send()
	check(refused.dropped.size() == 1 and CrashClient.queued(QUEUE).is_empty() and str(refused.dropped[0][1]).contains("400"),
		"400: dropped by name (the service will never take it): %s" % [refused])
	# the player's Send report (automatic reports off) through Release
	var offer := CrashClient.build(_facts(), _sample_lines(), _redact())
	var said: String = await release.send_offered_report(offer)
	check(said == "Report sent. Thank you." and CrashClient.queued(QUEUE).is_empty(), "Send report: queued and sent: %s" % said)
	# the service unreachable
	var url := OS.get_environment(CrashClient.TEST_ENDPOINT_ENV)
	OS.set_environment(CrashClient.TEST_ENDPOINT_ENV, "http://127.0.0.1:9/v1/reports")
	CrashClient.enqueue(QUEUE, offer.wire)
	var offline: Dictionary = await _send()
	check(str(offline.error).begins_with("crash.network") and CrashClient.queued(QUEUE).size() == 1, "offline: kept, named: %s" % [offline])
	OS.set_environment(CrashClient.TEST_ENDPOINT_ENV, "https://evil.example/v1/reports")
	var elsewhere: Dictionary = await _send()
	check(str(elsewhere.error).begins_with("crash.test_endpoint_refused") and CrashClient.queued(QUEUE).size() == 1,
		"a test endpoint that is not loopback is refused: %s" % [elsewhere])
	OS.set_environment(CrashClient.TEST_ENDPOINT_ENV, url)
	_clear(QUEUE)
	# lane CRASHACT-1 r3: the game's start blocks its main thread for seconds (loading); those frames do not use up the request's timeout
	# (HTTPRequest's own timer counted them: a slow PC timed out at every start). Ten 2 s frames (20 s) while the reply comes after 4 s.
	CrashClient.enqueue(QUEUE, offer.wire)
	var got := {}
	var client := CrashClient.new()
	root.add_child(client)
	client.finished.connect(func(s: Dictionary) -> void: got.merge(s, true))
	client.upload_queue(QUEUE, "user://crash-install-id")
	for i in 10:
		OS.delay_msec(2000)
		await process_frame
	while got.is_empty():
		await process_frame
	check(got.sent.size() == 1 and got.error.is_empty(), "blocking frames (20 s) do not time the request out: %s" % [got])
	client.queue_free()
	# a service that never answers: 15 s of running frames end the request by name; the report stays queued
	CrashClient.enqueue(QUEUE, offer.wire)
	var t0 := Time.get_ticks_msec()
	var silent: Dictionary = await _send()
	var seconds := (Time.get_ticks_msec() - t0) / 1000.0
	# tolerance: the client counts REQUEST_SECONDS of frame time (deltas), the test reads the wall clock: up to a frame or so apart
	check(silent.error == "crash.network (timed out)" and CrashClient.queued(QUEUE).size() == 1 and seconds > CrashClient.REQUEST_SECONDS - 1,
		"no answer: timed out after %.1f s of frames, kept: %s" % [seconds, silent])
	_clear(QUEUE)


func _send() -> Dictionary:
	var client := CrashClient.new()
	root.add_child(client)
	var summary: Dictionary = await client.upload_queue(QUEUE, "user://crash-install-id")
	client.queue_free()
	return summary
