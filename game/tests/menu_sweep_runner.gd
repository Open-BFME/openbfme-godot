extends SceneTree
## Regression gate for the main-menu availability sweep. It first records the
## old serial result as an oracle, then boots a fresh menu and requires the
## background sweep to publish byte-identical faction/map verdict tables while
## keeping every sweep-attributable `_process` slice below 8 ms.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const MAX_SWEEP_FRAME_MS := 8
const MAX_WAIT_FRAMES := 36000
## A cache hit must not re-run ANY validation. Cold sweeps measure ~94 s here;
## this ceiling is generous enough for the menu's own chunked Control
## construction and nothing else.
const MAX_CACHED_SWEEP_MS := 5000

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "MENU_SWEEP_RUNNER", 900_000)
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var content_root := OS.get_environment("OPENBFME_CONTENT")
	_check("content_root_selected", content_root != "" and DirAccess.dir_exists_absolute(content_root), content_root)
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	# Start cold on purpose: the worker path below must be the real validation,
	# not a hit on a cache some earlier run left behind.
	var menu_script := load("res://src/ui/main_menu.gd")
	menu_script.clear_skirmish_availability_cache()

	# Serial oracle: this is the exact pre-change validation ordering and values,
	# intentionally allowed to block because it is a headless measurement only.
	var baseline_menu := packed.instantiate()
	root.add_child(baseline_menu)
	baseline_menu.set_process(false)
	var baseline: Dictionary = baseline_menu.compute_skirmish_synchronous_baseline_for_test()
	var baseline_bytes := var_to_bytes({
		"availability": baseline["availability"],
		"map_notes": baseline["map_notes"],
	})
	print("MENU_SWEEP_TIMING baseline_wall_ms=%d baseline_worst_step_ms=%d" % [
		int(baseline["wall_ms"]), int(baseline["worst_step_ms"]),
	])
	baseline_menu.queue_free()
	await process_frame

	# ---- NO DUAL IMPLEMENTATION. -------------------------------------------
	# The menu no longer waits for this sweep: it renders instantly and validates
	# the map and factions the player PICKED, on demand. That is only safe while
	# the on-pick answer and the sweep's answer are the same answer, because the
	# on-pick answer is what opens the launch gate and the sweep's answer is what
	# greys the row. Two implementations of a fail-closed rule is how a menu comes
	# to say yes to a match the slice then refuses - so there is only one, and
	# this block proves it byte-for-byte against the serial oracle above rather
	# than trusting the comment that says so.
	var pick_menu := packed.instantiate()
	root.add_child(pick_menu)
	pick_menu.set_process(false)
	var oracle_availability := baseline["availability"] as Dictionary
	var oracle_notes := baseline["map_notes"] as Dictionary
	var picked_availability: Dictionary = {}
	for faction_value in menu_script.RETAIL_FACTIONS:
		var faction_id := String((faction_value as Dictionary)["id"])
		picked_availability[faction_id] = String(pick_menu.validate_skirmish_faction(faction_id))
	var picked_notes: Dictionary = {}
	var oracle_map_ids := oracle_notes.keys()
	for index in range(mini(3, oracle_map_ids.size())):
		var map_id := String(oracle_map_ids[index])
		picked_notes[map_id] = String(pick_menu.validate_skirmish_map(map_id))
	var oracle_subset: Dictionary = {}
	for map_id in picked_notes.keys():
		oracle_subset[map_id] = String(oracle_notes[map_id])
	_check("on_pick_faction_verdicts_are_the_sweeps_verdicts",
		var_to_bytes(picked_availability) == var_to_bytes(oracle_availability),
		"picked=%d oracle=%d" % [picked_availability.size(), oracle_availability.size()])
	_check("on_pick_map_verdicts_are_the_sweeps_verdicts",
		not picked_notes.is_empty() and var_to_bytes(picked_notes) == var_to_bytes(oracle_subset),
		"picked=%s" % str(picked_notes.keys()))
	_check("on_pick_validated_only_what_was_picked",
		(pick_menu.get("_skirmish_map_notes") as Dictionary).size() == picked_notes.size(),
		"notes=%d of %d catalog maps" % [
			(pick_menu.get("_skirmish_map_notes") as Dictionary).size(), oracle_notes.size(),
		])
	pick_menu.queue_free()
	await process_frame

	# Fresh menu, real automatic worker path. `_skirmish_options_ready` is now the
	# INSTANT list's promise, so the sweep's own completion is its own flag.
	var menu := packed.instantiate()
	root.add_child(menu)
	var frames := 0
	while not bool(menu.skirmish_sweep_is_complete()) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	_check("background_sweep_completed", bool(menu.skirmish_sweep_is_complete()), "frames=%d" % frames)
	_check("menu_list_was_ready_long_before_the_sweep", bool(menu.get("_skirmish_options_ready")))
	if not bool(menu.skirmish_sweep_is_complete()):
		menu.queue_free()
		await process_frame
		_finish()
		return

	var result_bytes := var_to_bytes({
		"availability": (menu.get("_skirmish_availability") as Dictionary),
		"map_notes": (menu.get("_skirmish_map_notes") as Dictionary),
	})
	var worst_frame_ms := int(menu.skirmish_sweep_worst_step_ms())
	var worst_frame_name := String(menu.skirmish_sweep_worst_frame_name())
	var wall_ms := int(menu.skirmish_sweep_total_wall_ms())
	var worker_ms := int(menu.skirmish_sweep_worker_compute_ms())
	print("MENU_SWEEP_TIMING worker_compute_ms=%d total_wall_ms=%d worst_process_frame_ms=%d worst_frame_name=%s frames=%d" % [
		worker_ms, wall_ms, worst_frame_ms, worst_frame_name, frames,
	])
	_check("availability_tables_byte_identical", baseline_bytes == result_bytes,
		"baseline_bytes=%d worker_bytes=%d" % [baseline_bytes.size(), result_bytes.size()])
	_check("all_factions_published", (menu.get("_skirmish_availability") as Dictionary).size() == 7)
	_check("all_map_notes_published",
		(menu.get("_skirmish_map_notes") as Dictionary).size() == (menu.get("_skirmish_map_choice_rows") as Array).size())
	_check("worst_sweep_process_frame_under_8ms", worst_frame_ms < MAX_SWEEP_FRAME_MS,
		"worst_process_frame_ms=%d" % worst_frame_ms)

	_check("cold_sweep_was_not_a_cache_hit", not bool(menu.skirmish_sweep_cache_hit()))
	_check("cold_sweep_reported_progress",
		int(menu.skirmish_sweep_completed_units()) == int(menu.get("_skirmish_sweep_total_units")),
		"completed=%d total=%d" % [
			int(menu.skirmish_sweep_completed_units()),
			int(menu.get("_skirmish_sweep_total_units")),
		])
	var cache_key := String(menu.skirmish_sweep_cache_key())
	_check("cold_sweep_established_a_content_identity", cache_key != "")

	# THE PROPERTY THE CACHE RESTS ON: a CONTENT CHANGE implies a KEY CHANGE.
	# `facts` are the real facts this launch keyed on; mutating any single one
	# must move the key, or two different contents share one stored table.
	var facts := menu.skirmish_content_identity_facts_for_test() as Dictionary
	var facts_usable := not facts.is_empty() and not (facts.get("bundles", []) as Array).is_empty()
	_check("content_identity_facts_available", facts_usable)
	if facts_usable:
		_check("content_identity_key_is_a_function_of_the_facts",
			String(menu_script.compute_skirmish_content_identity(facts)) == cache_key)

		var moved_tree := facts.duplicate(true)
		((moved_tree["bundles"] as Array)[0] as Dictionary)["prefix"] = "res://not-the-mounted-tree"
		_check_key_moves(menu_script, cache_key, moved_tree, "mounted_prefix")

		var repacked := facts.duplicate(true)
		((repacked["bundles"] as Array)[0] as Dictionary)["pack_json_sha"] = "a-different-bundle-content-digest"
		_check_key_moves(menu_script, cache_key, repacked, "bundle_content_digest")

		var other_source := facts.duplicate(true)
		other_source["selection_source"] = (
			"workspace" if String(facts.get("selection_source", "")) != "workspace" else "durable"
		)
		_check_key_moves(menu_script, cache_key, other_source, "resolved_selection_source")

		var other_selection_path := facts.duplicate(true)
		other_selection_path["selection_path"] = "res://not-the-resolved-selection.json"
		_check_key_moves(menu_script, cache_key, other_selection_path, "resolved_selection_path")

		var other_selection_bytes := facts.duplicate(true)
		other_selection_bytes["selection_sha"] = "different-selection-bytes"
		_check_key_moves(menu_script, cache_key, other_selection_bytes, "selection_bytes")

		var next_algorithm := facts.duplicate(true)
		next_algorithm["algorithm_version"] = int(facts.get("algorithm_version", 0)) + 1
		_check_key_moves(menu_script, cache_key, next_algorithm, "availability_algorithm_version")

	menu.queue_free()
	await process_frame

	# Second launch over unchanged content: the persisted table must be reused
	# verbatim, and reused WITHOUT re-running any faction or map validation.
	# This is the ~94 s SOLO PLAY wait the cache exists to remove.
	var cached_menu := packed.instantiate()
	root.add_child(cached_menu)
	var cached_start_ms := Time.get_ticks_msec()
	var cached_frames := 0
	while not bool(cached_menu.skirmish_sweep_is_complete()) and cached_frames < MAX_WAIT_FRAMES:
		cached_frames += 1
		await process_frame
	var cached_wall_ms := Time.get_ticks_msec() - cached_start_ms
	_check("cached_sweep_completed", bool(cached_menu.skirmish_sweep_is_complete()),
		"frames=%d" % cached_frames)
	_check("cached_sweep_reported_cache_hit", bool(cached_menu.skirmish_sweep_cache_hit()))
	_check("cached_sweep_same_content_identity",
		String(cached_menu.skirmish_sweep_cache_key()) == cache_key)
	var cached_bytes := var_to_bytes({
		"availability": (cached_menu.get("_skirmish_availability") as Dictionary),
		"map_notes": (cached_menu.get("_skirmish_map_notes") as Dictionary),
	})
	_check("cached_tables_byte_identical", cached_bytes == baseline_bytes,
		"cached_bytes=%d baseline_bytes=%d" % [cached_bytes.size(), baseline_bytes.size()])
	_check("cached_sweep_ran_no_validation_worker",
		int(cached_menu.skirmish_sweep_worker_compute_ms()) == 0,
		"worker_compute_ms=%d" % int(cached_menu.skirmish_sweep_worker_compute_ms()))
	_check("cached_sweep_under_%dms" % MAX_CACHED_SWEEP_MS, cached_wall_ms < MAX_CACHED_SWEEP_MS,
		"cached_wall_ms=%d cold_wall_ms=%d" % [cached_wall_ms, wall_ms])
	print("MENU_SWEEP_TIMING cached_wall_ms=%d cached_frames=%d" % [cached_wall_ms, cached_frames])
	cached_menu.queue_free()
	await process_frame

	# A cache written for different content must never be honoured.
	var poisoned := FileAccess.open("user://skirmish_availability_cache.json", FileAccess.WRITE)
	_check("cache_file_writable", poisoned != null)
	if poisoned != null:
		poisoned.store_string(JSON.stringify({
			"schema": "openbfme.skirmish-availability-cache",
			"schemaVersion": 0,
			"contentIdentity": "not-the-mounted-content",
			"availability": {},
			"map_notes": {},
		}))
		poisoned.close()
	var stale_menu := packed.instantiate()
	root.add_child(stale_menu)
	var stale_frames := 0
	while not bool(stale_menu.skirmish_sweep_is_complete()) and stale_frames < MAX_WAIT_FRAMES:
		stale_frames += 1
		await process_frame
	_check("stale_cache_key_forces_revalidation",
		bool(stale_menu.skirmish_sweep_is_complete()) and not bool(stale_menu.skirmish_sweep_cache_hit()),
		"frames=%d" % stale_frames)
	var stale_bytes := var_to_bytes({
		"availability": (stale_menu.get("_skirmish_availability") as Dictionary),
		"map_notes": (stale_menu.get("_skirmish_map_notes") as Dictionary),
	})
	_check("stale_cache_revalidation_matches_oracle", stale_bytes == baseline_bytes)
	stale_menu.queue_free()
	await process_frame

	# A SEMANTICALLY FAILED sweep is not a finding about the content. When every
	# faction verdict is the manifest-unavailable verdict nothing could be
	# resolved at all — an environmental failure of this process — and persisting
	# it would serve a durable "nothing is playable" answer that no content change
	# can invalidate.
	var manifest_verdict := String(menu_script.SKIRMISH_MANIFEST_UNAVAILABLE_VERDICT)
	var failed_availability: Dictionary = {}
	for faction_value in menu_script.RETAIL_FACTIONS:
		failed_availability[String((faction_value as Dictionary)["id"])] = manifest_verdict
	var failed_notes: Dictionary = {}
	for map_id in (baseline["map_notes"] as Dictionary).keys():
		failed_notes[String(map_id)] = manifest_verdict

	var store_menu := packed.instantiate()
	root.add_child(store_menu)
	store_menu.set_process(false)
	# Positive control first: at this exact moment a REAL table does persist, so
	# a missing file below is caused by the verdicts and not by a dead key.
	menu_script.clear_skirmish_availability_cache()
	store_menu.store_skirmish_sweep_cache_for_test({
		"availability": baseline["availability"],
		"map_notes": baseline["map_notes"],
	})
	_check("healthy_sweep_is_persisted",
		FileAccess.file_exists("user://skirmish_availability_cache.json"))
	menu_script.clear_skirmish_availability_cache()
	store_menu.store_skirmish_sweep_cache_for_test({
		"availability": failed_availability,
		"map_notes": failed_notes,
	})
	_check("failed_sweep_leaves_no_durable_negative_entry",
		not FileAccess.file_exists("user://skirmish_availability_cache.json"))
	store_menu.queue_free()
	await process_frame

	# An entry an older build already poisoned must be rejected on read, under
	# the CORRECT content key, and force a revalidation.
	var poison := FileAccess.open("user://skirmish_availability_cache.json", FileAccess.WRITE)
	_check("poisoned_cache_file_writable", poison != null)
	if poison != null:
		poison.store_string(JSON.stringify({
			"schema": "openbfme.skirmish-availability-cache",
			"schemaVersion": 0,
			"contentIdentity": cache_key,
			"availability": failed_availability,
			"map_notes": failed_notes,
		}))
		poison.close()
	var poisoned_menu := packed.instantiate()
	root.add_child(poisoned_menu)
	var poison_frames := 0
	while not bool(poisoned_menu.skirmish_sweep_rejected_poisoned_cache()) and poison_frames < 1800:
		poison_frames += 1
		await process_frame
	_check("poisoned_cache_entry_rejected_on_read",
		bool(poisoned_menu.skirmish_sweep_rejected_poisoned_cache()), "frames=%d" % poison_frames)
	_check("poisoned_cache_entry_forces_revalidation",
		not bool(poisoned_menu.skirmish_sweep_cache_hit()))
	_check("poisoned_cache_entry_is_deleted",
		not FileAccess.file_exists("user://skirmish_availability_cache.json"))
	poisoned_menu.queue_free()
	await process_frame

	# A completed task that returns no table must be reaped and published as a
	# complete fail-closed result. Navigation is re-enabled and a second
	# SKIRMISH choice can invoke retry_skirmish_sweep().
	# Cleared first so nothing is already known: the fail-closed fill is only
	# about verdicts NOBODY has computed, which is exactly the state a launch
	# gate must never read as "available".
	menu_script.clear_skirmish_availability_cache()
	var dead_menu := packed.instantiate()
	root.add_child(dead_menu)
	dead_menu.start_dead_skirmish_worker_for_test()
	var dead_frames := 0
	while not bool(dead_menu.skirmish_sweep_is_complete()) and dead_frames < 300:
		dead_frames += 1
		await process_frame
	var dead_reason := "availability worker completed without a result; choose SKIRMISH again to retry"
	var dead_availability := dead_menu.get("_skirmish_availability") as Dictionary
	_check("dead_worker_fail_closed_completed", bool(dead_menu.skirmish_sweep_is_complete()), "frames=%d" % dead_frames)
	_check("dead_worker_left_the_list_standing", bool(dead_menu.get("_skirmish_options_ready")))
	_check("dead_worker_all_factions_unavailable",
		dead_availability.size() == 7 and dead_availability.values().all(
			func(value: Variant) -> bool: return String(value) == dead_reason
		))
	_check("dead_worker_retry_affordance", dead_menu.has_method("retry_skirmish_sweep") and bool(dead_menu.get("_skirmish_sweep_failed")))
	dead_menu.queue_free()
	await process_frame
	_finish()


func _check_key_moves(menu_script, base_key: String, mutated_facts: Dictionary, label: String) -> void:
	var mutated_key := String(menu_script.compute_skirmish_content_identity(mutated_facts))
	_check("content_change_changes_cache_key_%s" % label, mutated_key != base_key,
		"mutated_key=%s base_key=%s" % [mutated_key.substr(0, 12), base_key.substr(0, 12)])


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("MENU_SWEEP PASS %s" % name)
	else:
		_failed += 1
		print("MENU_SWEEP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("MENU_SWEEP_RESULT passed=%d failed=%d" % [_passed, _failed])
	_runner_watchdog.stop()
	quit(0 if _failed == 0 else 1)
