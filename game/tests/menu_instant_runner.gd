extends SceneTree
## THE INSTANT MENU. Pins the property the player actually complained about,
## twice: choosing SOLO PLAY must produce a usable skirmish list immediately,
## cold, with no cache to lean on.
##
## What it is guarding against. The skirmish list used to be gated on a full
## availability sweep - seven factions deep-validated and every catalog map
## terrain-loaded, ~80 of them, a measured ~90 s cold. Caching it helped exactly
## once per content revision, because the cache key is (correctly) a function of
## the availability algorithm, so every engine change threw the table away and
## the player paid the ~90 s again. The fix is not a bigger cache: it is to stop
## asking the question. The list is drawn from what the packs already declare,
## and the fail-closed validation happens for the ONE map and the FEW factions
## that were actually picked.
##
## So this runner asserts all four halves of that bargain:
##   1. COLD RENDER  - the list exists in under a second with NOTHING validated.
##   2. ON PICK      - pressing the gate validates exactly one map, and says yes.
##   3. FAIL CLOSED  - a map that cannot be played blocks the launch BY NAME.
##   4. NON-BLOCKING - the background warmer is running and nobody waits on it.
## `menu_sweep_runner.gd` owns the fifth: the on-pick verdicts and the full
## sweep's verdicts are byte-identical, because they are the same functions.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
## The whole point. A cold SOLO PLAY list in under a second, every time.
const MAX_INSTANT_LIST_MS := 1000
## A memoized verdict is a dictionary lookup; anything slower means the gate
## re-validated something it had already answered.
const MAX_MEMOIZED_GATE_MS := 250
const MAX_WAIT_FRAMES := 1200
## No frame of the instant build may grow back into a stall the player feels.
const MAX_BUILD_FRAME_MS := 8
## A row id that is deliberately in no catalog, so its refusal is deterministic.
const ABSENT_MAP_ID := "openbfme.test.absent-map"
const ABSENT_MAP_NAME := "ABSENT FIXTURE MAP"

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "MENU_INSTANT_RUNNER", 900_000)
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
	var menu_script := load("res://src/ui/main_menu.gd")

	# ---- 1. COLD RENDER ------------------------------------------------------
	# Start with no cache at all. This is the exact state every engine round put
	# the player in, and the state in which they waited ~90 s twice.
	menu_script.clear_skirmish_availability_cache()
	var menu := packed.instantiate()
	root.add_child(menu)
	var cold_started_ms := Time.get_ticks_msec()
	var frames := 0
	while not bool(menu.get("_skirmish_options_ready")) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	var cold_list_ms := Time.get_ticks_msec() - cold_started_ms
	print("MENU_INSTANT_TIMING cold_list_ms=%d frames=%d" % [cold_list_ms, frames])
	_check("cold_list_became_ready", bool(menu.get("_skirmish_options_ready")), "frames=%d" % frames)
	if not bool(menu.get("_skirmish_options_ready")):
		_finish()
		return
	_check("cold_list_ready_under_%dms" % MAX_INSTANT_LIST_MS, cold_list_ms < MAX_INSTANT_LIST_MS,
		"cold_list_ms=%d" % cold_list_ms)
	_check("cold_list_was_not_a_cache_hit", not bool(menu.skirmish_sweep_cache_hit()))
	# THE LOAD-BEARING ONE: the list is up and NOTHING has been validated. Not a
	# faction manifest, not a terrain load, not a worker result.
	_check("cold_list_validated_nothing",
		int(menu.skirmish_on_pick_validation_ms()) == 0
			and int(menu.skirmish_sweep_worker_compute_ms()) == 0
			and not bool(menu.skirmish_sweep_is_complete()),
		"on_pick_ms=%d worker_ms=%d complete=%s" % [
			int(menu.skirmish_on_pick_validation_ms()),
			int(menu.skirmish_sweep_worker_compute_ms()),
			str(bool(menu.skirmish_sweep_is_complete())),
		])
	_check("cold_list_holds_no_verdicts",
		(menu.get("_skirmish_availability") as Dictionary).is_empty()
			and (menu.get("_skirmish_map_notes") as Dictionary).is_empty())
	_check("cold_list_worst_build_frame_under_%dms" % MAX_BUILD_FRAME_MS,
		int(menu.skirmish_sweep_worst_step_ms()) < MAX_BUILD_FRAME_MS,
		"worst_ms=%d name=%s" % [
			int(menu.skirmish_sweep_worst_step_ms()), String(menu.skirmish_sweep_worst_frame_name()),
		])

	# The list is a LIST: every catalog map, every faction, drawn from the pack
	# manifests. An "instant" menu that is instant because it is empty is not a
	# fix, so the contents are asserted, not just the timing.
	var catalog_rows := (menu.get("_skirmish_map_choice_rows") as Array).size()
	var drawn_rows: int = menu.solo_flyout.map_rows.size()
	_check("cold_list_drew_every_catalog_map", catalog_rows > 0 and drawn_rows == catalog_rows,
		"catalog=%d drawn=%d" % [catalog_rows, drawn_rows])
	var army: OptionButton = menu.solo_flyout.row_army_opts[0]
	_check("cold_list_drew_every_faction", army.item_count == (menu_script.RETAIL_FACTIONS as Array).size(),
		"items=%d" % army.item_count)
	var greyed := 0
	for entry_value in menu.solo_flyout.map_rows:
		if ((entry_value as Dictionary)["button"] as Button).disabled:
			greyed += 1
	_check("cold_list_greys_out_nothing_it_has_not_checked", greyed == 0, "greyed=%d" % greyed)
	var selected_map := String(menu._selected_skirmish_map())
	_check("cold_list_selected_a_map", selected_map != "", selected_map)

	# ---- 4. NON-BLOCKING -----------------------------------------------------
	# Navigation resolves in the calling frame while the warmer is still out.
	var opened: bool = menu.show_page("solo")
	_check("solo_page_opens_without_waiting_for_the_sweep",
		opened and String(menu.get_current_page()) == "solo"
			and not bool(menu.skirmish_sweep_is_complete()),
		"page=%s complete=%s" % [
			String(menu.get_current_page()), str(bool(menu.skirmish_sweep_is_complete())),
		])
	_check("menu_is_interactive_while_nothing_is_validated",
		int(menu.get("_skirmish_worker_task_id")) == -1
			and not bool(menu.skirmish_sweep_is_complete())
			and bool(menu.get("_skirmish_options_ready")),
		"task_id=%d" % int(menu.get("_skirmish_worker_task_id")))

	# ---- 2. VALIDATE ON PICK -------------------------------------------------
	# The launch gate is where the deep work moved to. It must validate the
	# picked map - and, crucially, ONLY the picked map.
	var gate_started_ms := Time.get_ticks_msec()
	var gate_error := String(menu.retail_launch_error())
	var gate_ms := Time.get_ticks_msec() - gate_started_ms
	print("MENU_INSTANT_TIMING on_pick_gate_ms=%d validation_ms=%d" % [
		gate_ms, int(menu.skirmish_on_pick_validation_ms()),
	])
	_check("on_pick_gate_opens_for_the_default_setup", gate_error == "", gate_error)
	_check("on_pick_gate_really_validated", int(menu.skirmish_on_pick_validation_ms()) > 0)
	var notes := menu.get("_skirmish_map_notes") as Dictionary
	_check("on_pick_validated_exactly_one_map",
		notes.size() == 1 and notes.has(selected_map),
		"validated=%d of %d catalog maps" % [notes.size(), catalog_rows])
	_check("on_pick_resolved_every_faction_verdict",
		(menu.get("_skirmish_availability") as Dictionary).size() == (menu_script.RETAIL_FACTIONS as Array).size())

	# ---- memoization ---------------------------------------------------------
	var memo_ms_before := int(menu.skirmish_on_pick_validation_ms())
	var repeat_started_ms := Time.get_ticks_msec()
	var repeat_error := String(menu.retail_launch_error())
	var repeat_ms := Time.get_ticks_msec() - repeat_started_ms
	_check("repeat_pick_is_memoized",
		repeat_error == gate_error and repeat_ms < MAX_MEMOIZED_GATE_MS
			and int(menu.skirmish_on_pick_validation_ms()) == memo_ms_before,
		"repeat_ms=%d" % repeat_ms)
	menu.queue_free()
	await process_frame

	# ---- the memo survives the launch ---------------------------------------
	# A second launch over unchanged content must find the picked map already
	# answered, and must still render its list instantly.
	var warm := packed.instantiate()
	root.add_child(warm)
	var warm_started_ms := Time.get_ticks_msec()
	var warm_frames := 0
	while not bool(warm.get("_skirmish_options_ready")) and warm_frames < MAX_WAIT_FRAMES:
		warm_frames += 1
		await process_frame
	var warm_list_ms := Time.get_ticks_msec() - warm_started_ms
	print("MENU_INSTANT_TIMING warm_list_ms=%d frames=%d" % [warm_list_ms, warm_frames])
	_check("warm_list_ready_under_%dms" % MAX_INSTANT_LIST_MS, warm_list_ms < MAX_INSTANT_LIST_MS,
		"warm_list_ms=%d" % warm_list_ms)
	_check("warm_list_adopted_the_persisted_pick",
		(warm.get("_skirmish_map_notes") as Dictionary).has(selected_map)
			and int(warm.skirmish_on_pick_validation_ms()) == 0,
		"notes=%d" % (warm.get("_skirmish_map_notes") as Dictionary).size())

	# ---- the warmer starts on its own, AFTER the list, and is not waited on ---
	# It cannot even begin until the slice script finishes compiling on the
	# loader thread (~3-4 s), which is precisely why nothing may depend on it:
	# the list above was already up and interactive tens of milliseconds in.
	var warmer_frames := 0
	while int(warm.get("_skirmish_worker_task_id")) < 0 \
			and not bool(warm.skirmish_sweep_is_complete()) \
			and warmer_frames < MAX_WAIT_FRAMES:
		warmer_frames += 1
		await process_frame
	_check("background_warmer_starts_after_the_list_is_already_up",
		(int(warm.get("_skirmish_worker_task_id")) >= 0 or bool(warm.skirmish_sweep_is_complete()))
			and bool(warm.get("_skirmish_options_ready")),
		"frames_after_ready=%d" % warmer_frames)
	_check("background_warmer_did_not_stall_a_frame",
		int(warm.skirmish_sweep_worst_step_ms()) < MAX_BUILD_FRAME_MS,
		"worst_ms=%d" % int(warm.skirmish_sweep_worst_step_ms()))

	# ---- 3. FAIL CLOSED ------------------------------------------------------
	# The fail-closed launch gate moved from "every row was pre-greyed by a 90 s
	# sweep" to "the pick is checked". Prove the refusal is still there, still
	# named, and still on the button - through the REAL row builder and the REAL
	# gate, over an id that is deliberately in no catalog.
	var absent := {"id": ABSENT_MAP_ID, "name": ABSENT_MAP_NAME}
	var choice_rows := warm.get("_skirmish_map_choice_rows") as Array
	choice_rows.append(absent)
	warm._build_skirmish_map_row(choice_rows.size() - 1, absent)
	warm.solo_flyout.set_selected_map_row(warm.solo_flyout.map_rows.size() - 1)
	_check("absent_map_row_is_selectable_until_checked",
		String(warm._selected_skirmish_map()) == ABSENT_MAP_ID,
		String(warm._selected_skirmish_map()))
	var absent_error := String(warm.retail_launch_error())
	_check("absent_map_blocks_the_launch", absent_error != "", absent_error)
	_check("absent_map_refusal_names_the_map_and_the_reason",
		absent_error.contains(ABSENT_MAP_NAME)
			and (absent_error.contains("not registered") or absent_error.contains("OPENBFME_CONTENT")),
		absent_error)
	warm._refresh_skirmish_launch_state()
	_check("absent_map_disables_the_play_button_with_that_reason",
		warm.solo_flyout.play_btn.disabled
			and String(warm.solo_flyout.hint_label.text) == absent_error,
		String(warm.solo_flyout.hint_label.text))

	# And re-picking a real map re-opens the gate: the refusal is about the pick,
	# not a state the menu got stuck in.
	warm.solo_flyout.set_selected_map_row(0)
	var recovered := String(warm.retail_launch_error())
	_check("re_picking_a_real_map_reopens_the_gate", recovered == "", recovered)
	warm._refresh_skirmish_launch_state()
	_check("recovered_play_button_is_enabled", not warm.solo_flyout.play_btn.disabled)

	warm.queue_free()
	await process_frame
	# The synthetic row was memoized alongside the real ones; do not leave a
	# fixture id in a durable table other runs would read.
	menu_script.clear_skirmish_availability_cache()
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("MENU_INSTANT PASS %s" % name)
	else:
		_failed += 1
		print("MENU_INSTANT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("MENU_INSTANT_RESULT passed=%d failed=%d" % [_passed, _failed])
	_runner_watchdog.stop()
	quit(0 if _failed == 0 else 1)
