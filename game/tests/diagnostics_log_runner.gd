extends SceneTree
## Proves the sim half of the shared diagnostic run contract.
##
## WHAT IS ACTUALLY UNDER TEST, and why each of these is here rather than
## "it looked right when I ran the game":
##
##  * OFF BY DEFAULT. The contract's first promise is no regression in normal
##    play. A diagnostics feature that quietly starts writing files on every
##    launch has already broken the thing it was meant to help.
##  * THE LAYOUT. Directory name, the four documents, and the fact that error.txt
##    appears only on failure. A support bundle whose shape drifts between the
##    four components is a bundle nobody can read.
##  * IDENTITY CAPTURE. The recorded failure class in this half is "a stale pack
##    silently served the wrong content", so the artifact has to name the pack id,
##    the bundle digest, the sha256 of the selection it came from, and WHY that
##    root was mounted. Fed here from a synthetic ModLoader.runtime_pack_report,
##    because the runner must not depend on a machine having a converted pack.
##  * RETENTION. Including the exemption that matters: the run that failed is the
##    one the bug report is about, so twenty later uneventful runs must not evict
##    it.
##  * REDACTION. Home paths and credentials, asserted on the exact strings a run
##    record carries.
##  * RATE LIMITING. Per-frame data must not be able to bury the run.
##
## Run it:
##   <godot> --headless --path game --script res://tests/diagnostics_log_runner.gd

const DiagLog = preload("res://src/core/diag_log.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")

## Liveness guard, same convention as handlers_wp20_skirmish_conditions_runner:
## a function that aborts before its assertions would otherwise report a clean
## pass over a shrunken suite.
const EXPECTED_CHECKS := 30

var passed := 0
var failed := 0

var _watchdog := Watchdog.new()
## Every recorder built here. DiagLog extends Node, so an instance that is never
## added to the tree is NOT reference counted - dropping the variable leaks it
## and leaves its file handles open, which on Windows makes the temp-tree cleanup
## below fail in a way that looks like a test failure. Same class of teardown
## bug the script-world runners document with _release_facets.
var _recorders: Array = []
var _temp_root := ""


func _initialize() -> void:
	_watchdog.start(self, "DIAG_LOG", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_temp_root = OS.get_user_data_dir().replace("\\", "/").trim_suffix("/").path_join(
		"diag-log-runner-%d" % OS.get_process_id()
	)
	_remove_tree(_temp_root)
	DirAccess.make_dir_recursive_absolute(_temp_root)
	# The runner must decide the enable state itself, never inherit it: a
	# developer with OPENBFME_DIAGNOSTICS=1 exported would otherwise silently
	# skip the most important assertion in the file.
	OS.set_environment("OPENBFME_DIAGNOSTICS", "0")

	_test_off_by_default()
	_test_run_directory_layout()
	_test_jsonl_event_shape()
	_test_identity_capture()
	_test_retention_keeps_newest_and_spares_failures()
	_test_error_document_on_failure()
	_test_redaction()
	_test_rate_limited_sampling()

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("DIAG_LOG FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, EXPECTED_CHECKS
		])
	print("DIAG_LOG_RESULT passed=%d failed=%d" % [passed, failed])
	_teardown()
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

func _test_off_by_default() -> void:
	_check("requested_is_false_without_the_opt_in", not DiagLog.requested())
	var recorder := _recorder()
	# No begin(): this is the shape production takes when nobody asked.
	_check("recorder_starts_disabled", recorder.enabled == false)
	recorder.event("info", "should.not.appear", {"a": 1})
	recorder.failure("should.not.appear", "nor this")
	var root := _temp_root.path_join("untouched")
	_check(
		"disabled_recorder_writes_nothing",
		not DirAccess.dir_exists_absolute(root) and _run_directories(_temp_root).is_empty()
	)


func _test_run_directory_layout() -> void:
	var root := _temp_root.path_join("layout")
	var recorder := _recorder()
	var started: bool = recorder.begin(root)
	_check("begin_creates_a_run", started and recorder.run_dir != "")
	var name := String(recorder.run_dir).get_file()
	var parts := name.split("-")
	_check(
		"run_directory_is_stamp_component_pid",
		parts.size() == 3
			and String(parts[0]).length() == 16
			and String(parts[0])[8] == "T"
			and String(parts[0]).ends_with("Z")
			and String(parts[1]) == "sim"
			and String(parts[2]) == str(OS.get_process_id()),
		name
	)
	_check("run_log_exists", FileAccess.file_exists(recorder.run_dir.path_join("run.log")))
	_check("run_jsonl_exists", FileAccess.file_exists(recorder.run_dir.path_join("run.jsonl")))
	var env_document: Variant = _read_json(recorder.run_dir.path_join("env.json"))
	_check(
		"env_json_describes_the_process",
		env_document is Dictionary
			and String((env_document as Dictionary).get("component", "")) == "sim"
			and (env_document as Dictionary).has("os")
			and (env_document as Dictionary).has("renderer")
			and (env_document as Dictionary).has("environment")
	)
	_check(
		"no_error_document_on_a_clean_run",
		not FileAccess.file_exists(recorder.run_dir.path_join("error.txt"))
	)


func _test_jsonl_event_shape() -> void:
	var recorder := _recorder()
	recorder.begin(_temp_root.path_join("jsonl"))
	recorder.event("warn", "content.durable_fallback", {"source": "durable"})
	var rows := _read_jsonl(recorder.run_dir.path_join("run.jsonl"))
	_check("jsonl_has_one_object_per_event", rows.size() >= 2, "rows=%d" % rows.size())
	var complete := true
	for row in rows:
		var entry: Dictionary = row
		for key in ["ts", "level", "component", "event", "fields"]:
			if not entry.has(key):
				complete = false
		if String(entry.get("component", "")) != "sim":
			complete = false
	_check("every_jsonl_row_carries_the_contract_keys", complete)
	var found := false
	for row in rows:
		if String((row as Dictionary).get("event", "")) == "content.durable_fallback":
			found = true
	_check("recorded_events_are_readable_back", found)


func _test_identity_capture() -> void:
	## The synthetic report is exactly the shape ModLoader.runtime_pack_report
	## returns. A real pack is deliberately NOT required: this runner has to pass
	## on a fresh checkout with no converted content, and the contract under test
	## is the ARTIFACT, not the loader.
	var selection_path := _temp_root.path_join("selection.json")
	var file := FileAccess.open(selection_path, FileAccess.WRITE)
	file.store_string('{"schema":"openbfme.pack-selection","schemaVersion":0,"activePack":"rotwk-angmar/' + "a".repeat(64) + '"}')
	file = null
	var expected_sha := FileAccess.get_sha256(selection_path)

	var digest := "b".repeat(64)
	var report := {
		"activeContentSource": "durable",
		"activeSelectionPath": selection_path,
		"activePackRoot": "/packs/rotwk-angmar/%s" % digest,
		"strictParityProfile": false,
		"strictParityErrors": [],
		"suppressedAmbientRoots": [],
		"refusedForeignPacks": [],
		"diagnostics": ["Falling back to the DURABLE pack in user data"],
		"orderedPacks": [
			{
				"packId": "", "packRoot": "res://data", "bundleDigest": "",
				"contentAddressed": false, "embeddedResource": true,
				"ambientMod": false, "active": false,
			},
			{
				"packId": "rotwk-angmar", "packRoot": "/packs/rotwk-angmar/%s" % digest,
				"bundleDigest": digest, "contentAddressed": true,
				"embeddedResource": false, "ambientMod": false, "active": true,
			},
		],
		"orderedPackIds": ["res://data", "rotwk-angmar/%s" % digest],
	}

	var recorder := _recorder()
	recorder.begin(_temp_root.path_join("identity"))
	recorder.capture_content_identity(report, ["/packs/rotwk-base/%s" % "c".repeat(64)])
	var written: Variant = _read_json(recorder.run_dir.path_join("identity.json"))
	_check("identity_json_is_written", written is Dictionary)
	var document: Dictionary = written as Dictionary if written is Dictionary else {}
	_check(
		"identity_names_the_active_pack_and_digest",
		String(document.get("activePackId", "")) == "rotwk-angmar"
			and String(document.get("activePackDigest", "")) == digest
			and bool(document.get("activePackContentAddressed", false)),
		String(document.get("activePackId", ""))
	)
	_check(
		"identity_hashes_the_mounted_selection",
		String(document.get("activeSelectionSha256", "")) == expected_sha and expected_sha != "",
		String(document.get("activeSelectionSha256", ""))
	)
	_check(
		"identity_lists_supplemental_packs",
		(document.get("supplementalPacks", []) as Array).size() == 1
	)
	# The whole point of the identity document: not just WHAT mounted, but WHY,
	# and the durable source has to admit it may be stale in the artifact itself.
	_check(
		"durable_source_reason_says_it_may_be_stale",
		String(document.get("activeContentSourceReason", "")).contains("STALE"),
		String(document.get("activeContentSourceReason", ""))
	)
	var reasons: Array = []
	for entry in (document.get("mountedRoots", []) as Array):
		reasons.append(String((entry as Dictionary).get("reason", "")))
	_check(
		"every_mounted_root_records_why_it_mounted",
		reasons.size() == 2
			and String(reasons[0]).begins_with("ambient")
			and String(reasons[1]).contains("activePack"),
		", ".join(PackedStringArray(reasons))
	)


func _test_retention_keeps_newest_and_spares_failures() -> void:
	var root := _temp_root.path_join("retention")
	DirAccess.make_dir_recursive_absolute(root)
	# 30 runs, oldest first. Run 0 failed; runs 1..29 did not.
	for index in 30:
		var name := "20260801T%06dZ-sim-4242" % index
		DirAccess.make_dir_recursive_absolute(root.path_join(name))
		var marker := FileAccess.open(root.path_join(name).path_join("run.log"), FileAccess.WRITE)
		marker.store_line("run %d" % index)
		marker = null
		if index == 0:
			var error_file := FileAccess.open(root.path_join(name).path_join("error.txt"), FileAccess.WRITE)
			error_file.store_line("it broke")
			error_file = null
	# A human-created directory in the log root must survive retention untouched.
	DirAccess.make_dir_recursive_absolute(root.path_join("notes-from-the-owner"))

	DiagLog.prune(root)
	var remaining := _run_directories(root)
	_check(
		"retention_keeps_the_newest_twenty_plus_the_failure",
		remaining.size() == 21, "remaining=%d" % remaining.size()
	)
	_check(
		"retention_never_evicts_a_run_that_recorded_a_failure",
		remaining.has("20260801T000000Z-sim-4242"),
		", ".join(PackedStringArray(remaining))
	)
	_check(
		"retention_leaves_unrecognised_directories_alone",
		DirAccess.dir_exists_absolute(root.path_join("notes-from-the-owner"))
	)


func _test_error_document_on_failure() -> void:
	var recorder := _recorder()
	recorder.begin(_temp_root.path_join("failure"))
	recorder.event("info", "map.install_begin", {"map": "mp-harlindon"})
	recorder.failure(
		"script.install_refused",
		"map scripts: scripts.json: unknown condition; installing NO scripts",
		"mp-harlindon"
	)
	var run_dir: String = recorder.run_dir
	recorder.close()
	var text := FileAccess.get_file_as_string(run_dir.path_join("error.txt"))
	_check("error_txt_is_written_when_a_run_fails", text != "")
	_check(
		"error_txt_names_the_failure",
		text.contains("installing NO scripts") and text.contains("script.install_refused")
	)
	_check(
		"error_txt_carries_the_tail_of_the_run",
		text.contains("map.install_begin") and text.contains("LAST ")
	)


func _test_redaction() -> void:
	var home := OS.get_environment("USERPROFILE")
	if home == "":
		home = OS.get_environment("HOME")
	var redacted_home := DiagLog.redact("%s/Desktop/open-bfme" % home.replace("\\", "/"))
	_check(
		"the_users_home_directory_never_reaches_the_artifact",
		home == "" or (not redacted_home.contains(home.replace("\\", "/")) and redacted_home.contains("%")),
		redacted_home
	)
	_check(
		"another_machines_user_directory_is_redacted_too",
		DiagLog.redact("D:\\Users\\someone\\packs") == "D:\\Users\\<user>\\packs",
		DiagLog.redact("D:\\Users\\someone\\packs")
	)
	var secret := DiagLog.redact('{"signing_key": "MIIEvQIBADANBg", "packId": "rotwk-angmar"}')
	_check(
		"credentials_are_replaced_not_truncated",
		not secret.contains("MIIEvQIBADANBg") and secret.contains("rotwk-angmar"),
		secret
	)
	# Field NAMES are checked as well as values: a run record that logs
	# {"token": ...} must not leak it just because the value looked innocent.
	var recorder := _recorder()
	recorder.begin(_temp_root.path_join("redaction"))
	recorder.event("info", "auth.probe", {"token": "abc123", "packId": "rotwk-men"})
	var rows := _read_jsonl(recorder.run_dir.path_join("run.jsonl"))
	var leaked := false
	for row in rows:
		if JSON.stringify(row).contains("abc123"):
			leaked = true
	_check("secret_named_fields_are_redacted_by_name", not leaked)


func _test_rate_limited_sampling() -> void:
	var recorder := _recorder()
	recorder.begin(_temp_root.path_join("sampling"))
	for _index in 200:
		recorder.sampled("sim.heartbeat", 60_000, "debug", "sim.heartbeat", {"tick": 1})
	var samples := 0
	for row in _read_jsonl(recorder.run_dir.path_join("run.jsonl")):
		if String((row as Dictionary).get("event", "")) == "sim.heartbeat":
			samples += 1
	_check("per_frame_events_are_rate_limited", samples == 1, "samples=%d" % samples)
	recorder.close()
	var end_row := {}
	for row in _read_jsonl(recorder.run_dir.path_join("run.jsonl")):
		if String((row as Dictionary).get("event", "")) == "run.end":
			end_row = (row as Dictionary).get("fields", {}) as Dictionary
	_check(
		"dropped_samples_are_counted_so_the_rate_is_not_mistaken_for_the_truth",
		int(end_row.get("droppedSamples", 0)) == 199,
		"dropped=%d" % int(end_row.get("droppedSamples", 0))
	)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

func _recorder() -> Node:
	var recorder: Node = DiagLog.new()
	_recorders.append(recorder)
	return recorder


func _teardown() -> void:
	## The _release_facets equivalent for this runner: close every recorder so its
	## file handles are released, then free the Nodes (they were never added to
	## the tree, so nothing else will), then remove the temp tree. Closing before
	## freeing matters on Windows, where an open handle makes the directory
	## removal fail.
	for recorder in _recorders:
		if is_instance_valid(recorder):
			recorder.call("close")
			recorder.free()
	_recorders.clear()
	if _temp_root != "":
		_remove_tree(_temp_root)


func _run_directories(root: String) -> Array[String]:
	var names: Array[String] = []
	var dir := DirAccess.open(root)
	if dir == null:
		return names
	for name in dir.get_directories():
		if String(name).contains("-sim-") or String(name).contains("-launcher-"):
			names.append(String(name))
	names.sort()
	return names


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var json := JSON.new()
	if json.parse(FileAccess.get_file_as_string(path)) != OK:
		return null
	return json.data


func _read_jsonl(path: String) -> Array:
	var rows: Array = []
	if not FileAccess.file_exists(path):
		return rows
	for line in FileAccess.get_file_as_string(path).split("\n"):
		var text := String(line).strip_edges()
		if text == "":
			continue
		var json := JSON.new()
		if json.parse(text) == OK and json.data is Dictionary:
			rows.append(json.data)
	return rows


func _remove_tree(path: String) -> void:
	var dir := DirAccess.open(path)
	if dir == null:
		return
	for name in dir.get_files():
		dir.remove(name)
	for name in dir.get_directories():
		_remove_tree(path.path_join(name))
	DirAccess.remove_absolute(path)


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("DIAG_LOG PASS %s" % name)
	else:
		failed += 1
		printerr("DIAG_LOG FAIL %s (%s)" % [name, detail])
