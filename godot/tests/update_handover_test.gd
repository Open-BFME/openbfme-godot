## Lane ONEZIP-1: the game's side of the update hand-over (scripts/release/update_handover.gd, launcher_check.gd), protocol 2, against
## folders and a launcher data folder that tools/release/test_onezip.py prepares (-- --cases=<json>):
##   check:     LauncherCheck.check of each prepared folder (the package's launcher, a replaced one, a link, a signed update with its
##              proof, a foreign proof, no / a broken / a foreign inventory): "" or the named refusal the case expects;
##   bootstrap: begin() with a launcher that is not the signed one starts nothing and says why;
##   answers:   "continue"; "start <version>" (this game starts that version itself with ITS arguments after "--", writes `started`
##              and closes the request); a version that is not installed; answers that are not the protocol's, refused by name; the
##              deadline (the request closed, a later answer not acted on); the status lines.
## Prints "HANDOVER OK" when every case passed. Needs no openbfme extension.
extends SceneTree

const Handover := preload("res://scripts/release/update_handover.gd")
const LauncherCheck := preload("res://scripts/release/launcher_check.gd")

var failures := 0
var cases: Dictionary


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		printerr("HANDOVER FAIL: ", what)


func _init() -> void:
	_run.call_deferred()


func fresh(name: String) -> RefCounted:
	var h := Handover.new()
	h.key = String(cases.key).hex_decode()
	h.repo = cases.repo
	h.launcher_dir = cases.launcher_dir
	h.user_args = PackedStringArray(["--script", "res://evil.gd", "--fps"])
	h.dir = String(cases.requests).path_join(name)
	DirAccess.make_dir_recursive_absolute(h.dir)
	for f in DirAccess.get_files_at(h.dir):
		DirAccess.remove_absolute(h.dir.path_join(f))
	FileAccess.open(h.dir.path_join("open"), FileAccess.WRITE).close()
	h._start = Time.get_ticks_msec()
	return h


func put(h: RefCounted, name: String, text: String) -> void:
	var f := FileAccess.open(h.dir.path_join(name), FileAccess.WRITE)
	f.store_string(text)
	f.close()


func answer(action: String, version := "", why := "") -> String:
	return JSON.stringify({"protocol": 2, "action": action, "version": version, "why": why})


func closed(h: RefCounted) -> bool:
	return FileAccess.file_exists(h.dir.path_join("closed")) and not FileAccess.file_exists(h.dir.path_join("open"))


func _run() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--cases="):
			cases = JSON.parse_string(FileAccess.get_file_as_string(a.substr(8)))
	check(Handover.why_not(false) == "a development run", "a development run does not ask: " + Handover.why_not(false))
	var key := String(cases.key).hex_decode()

	# the launcher beside the game: authenticated before it is started
	for c in cases.check:
		var before := OS.get_static_memory_peak_usage()
		var got := LauncherCheck.check(c.dir, key, cases.repo)
		var grew := OS.get_static_memory_peak_usage() - before
		print("CHECK %s: %s (peak memory +%d)" % [c.name, got if got != "" else "ok", grew])
		# tolerance: the 1 MiB hashing buffer and the small signed files; the 32 MiB case's file is never read whole (Sol r2)
		check(grew < (4 << 20), "check %s: peak memory grew %d bytes" % [c.name, grew])
		check(got == "" if c.want == "" else got.contains(c.want), "check %s: '%s', expected '%s'" % [c.name, got, c.want])

	# bootstrap: an unsigned launcher is never started
	var h := fresh("bootstrap")
	var why: String = h.begin(PackedStringArray(), cases.unsigned_dir)
	check(why.begins_with("the launcher beside the game is not started: ") and why.contains("is not the package's launcher"), "bootstrap: " + why)
	check(h.poll().state == "continue", "bootstrap: the game goes on")

	# a real ENOSPC (the temporary file is /dev/full): the write fails, by name in begin(), and leaves nothing behind
	h = fresh("enospc")
	h.dir = cases.enospc_dir
	check(not h._write("request.json", "{}") and not FileAccess.file_exists(h.dir.path_join("request.json")), "a full disk fails the write")

	h = fresh("continue")
	check(h.poll().state == "waiting", "no answer yet: waiting")
	put(h, "answer", answer("continue", "", "This folder is not the signed package (x): download OpenBFME again."))
	var r: Dictionary = h.poll()
	check(r == {"state": "continue", "text": "This folder is not the signed package (x): download OpenBFME again."} and closed(h), "continue: " + str(r))

	# start: this game starts the installed version with its own arguments after "--" (never engine options), reports it, closes
	h = fresh("start")
	put(h, "answer", answer("start", cases.version, "%s is installed: it starts instead of v0.5.0" % cases.version))
	r = h.poll()
	check(r.state == "switched" and closed(h), "start: " + str(r))
	var started: Variant = JSON.parse_string(FileAccess.get_file_as_string(h.dir.path_join("started")))
	check(started is Dictionary and started.version == cases.version and int(started.pid) > 0, "started: " + str(started))

	h = fresh("missing")
	put(h, "answer", answer("start", "v0.7.0"))
	r = h.poll()
	check(r.state == "continue" and r.text == "v0.7.0 is not installed where the launcher keeps it: this version starts" and closed(h), "missing: " + str(r))

	for bad in [["{\"protocol\": 1, \"action\": \"continue\", \"version\": \"\", \"why\": \"\"}", "it speaks protocol 1"],
			["{\"protocol\": 2, \"action\": \"run\", \"version\": \"\", \"why\": \"\"}", "its action is not continue or start"],
			["{\"protocol\": 2, \"action\": \"start\", \"version\": \"../../bin/sh\", \"why\": \"\"}", "it names no release version to start"],
			["{\"protocol\": 2, \"action\": \"start\", \"version\": \"v0.6.0\", \"why\": \"\", \"path\": \"/bin/sh\"}", "its keys are"],
			["{\"protocol\": 2, \"action\": \"continue\", \"version\": 5, \"why\": \"\"}", "its reason or version is not a short text"],
			["[\"continue\"]", "it is not a JSON object"],
			["{\"protocol\": 2, \"action\": \"continue\", \"version\": \"\", \"why\": \"" + "x".repeat(5000) + "\"}", "it is larger than 4096 bytes"]]:
		h = fresh("bad")
		put(h, "answer", bad[0])
		r = h.poll()
		check(r.state == "continue" and String(r.text).begins_with("the launcher's answer was refused: " + bad[1]) and closed(h)
			and not FileAccess.file_exists(h.dir.path_join("started")), "malformed '%s': %s" % [bad[0].left(60), str(r)])

	# the deadline: the request is closed, this game starts and says why; an answer written later is not acted on
	h = fresh("deadline")
	h._start = Time.get_ticks_msec() - Handover.DEADLINE_MS - 1
	r = h.poll()
	check(r.state == "continue" and r.text.begins_with("the launcher beside the game did not answer within 3 s") and closed(h), "deadline: " + str(r))
	put(h, "answer", answer("start", cases.version))
	r = h.poll()
	check(r.state == "continue" and not FileAccess.file_exists(h.dir.path_join("started")), "a late answer is not acted on: " + str(r))

	# Sol r2: an answer that is already there when the deadline has passed (the launcher was slow, the game was busy) is refused by
	# name and never acted on, also before the request was closed
	h = fresh("stale")
	put(h, "answer", answer("start", cases.version, "x"))
	h._start = Time.get_ticks_msec() - Handover.DEADLINE_MS - 4000
	r = h.poll()
	check(r == {"state": "continue", "text": "the launcher's answer was refused: it came after the 3 s deadline"} and closed(h)
		and not FileAccess.file_exists(h.dir.path_join("started")), "a stale answer: " + str(r))

	# the status line: each new text once, the final flag, nothing for a malformed file
	h = fresh("status")
	check(h.status().is_empty(), "no status yet")
	put(h, "status", JSON.stringify({"text": "Checking for updates...", "final": false}))
	check(h.status() == {"text": "Checking for updates...", "final": false}, "the first line")
	check(h.status().is_empty(), "the same line is not reported twice")
	put(h, "status", JSON.stringify({"text": "Updates skipped: offline. v1 starts.", "final": true}))
	check(h.status() == {"text": "Updates skipped: offline. v1 starts.", "final": true}, "the final line")
	put(h, "status", JSON.stringify({"text": 5, "final": true}))
	check(h.status().is_empty(), "a status whose text is not a text is not shown")
	put(h, "status", JSON.stringify({"text": "y".repeat(500), "final": "yes"}))
	var long: Dictionary = h.status()
	check(String(long.text).length() == 300 and long.final == false, "a long line is cut, a final flag that is not true is false")

	print("HANDOVER OK" if failures == 0 else "HANDOVER FAILED: %d" % failures)
	quit(0 if failures == 0 else 1)
