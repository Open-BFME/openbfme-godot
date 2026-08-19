extends SceneTree
## THE LOBBY HALF of "castle maps are playable" (owner 2026-08-19).
##
## The battlefield/slice runners prove a castle map boots; this one proves the
## menu OFFERS it. Two traps it pins, both found by the fcd0cbd review:
##   1. STALE VERDICT CACHE - player machines hold a persisted
##      skirmish_availability_cache.json whose v1 entries read
##      "castle gameplay unsupported: ...". The key is a function of
##      SKIRMISH_AVAILABILITY_ALGORITHM_VERSION; a version that admits castle
##      maps must not replay v1's refusals. We plant exactly that stale file
##      under the v1 key and assert it is NOT adopted.
##   2. ON-PICK VALIDATION - every castle map validates "" (playable) and its
##      row is enabled with the partial-siege tooltip, not greyed out.
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/castle_lobby_admission_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const ProfileSandboxScript := preload("res://tests/cah_profile_sandbox.gd")
const MAX_WAIT_FRAMES := 1200
const CASTLE_MAPS: Array[String] = [
	"rotwk.map.wor-erebor", "rotwk.map.wor-helms-deep", "rotwk.map.wor-minas-tirith",
	"rotwk.map.wor-black-gate", "rotwk.map.wor-dol-guldur", "rotwk.map.wor-grey-havens",
	"rotwk.map.wor-isengard", "rotwk.map.wor-minas-morgul", "rotwk.map.wor-ang-carn-dum",
	"rotwk.map.wor-ang-fornost",
]
const V1_REFUSAL := "castle gameplay unsupported: walkable-walls, defendable-gates, wall-garrisons, wall-mounted-defenses, skirmish-ai-libraries"

var _profiles := ProfileSandboxScript.new()
var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _initialize() -> void:
	_profiles.open("castle-lobby")
	_runner_watchdog.start(self, "CASTLE_LOBBY_RUNNER", 900_000)
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/boot.tscn")
	var menu_script := load("res://src/ui/main_menu.gd")
	_check("boot_scene_parses", packed != null and menu_script != null)
	if packed == null or menu_script == null:
		_finish()
		return
	# The algorithm version MUST have moved past the one that refused castles.
	_check("availability_algorithm_version_moved_past_v1", int(menu_script.SKIRMISH_AVAILABILITY_ALGORITHM_VERSION) >= 2,
		"version=%d" % int(menu_script.SKIRMISH_AVAILABILITY_ALGORITHM_VERSION))
	menu_script.clear_skirmish_availability_cache()
	# ---- 1. plant the stale v1 cache the owner's machine really carries ----
	var probe := packed.instantiate()
	root.add_child(probe)
	var frames := 0
	while not bool(probe.get("_skirmish_options_ready")) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	var facts: Dictionary = probe.call("_skirmish_content_identity_facts")
	_check("content_identity_facts_available", not facts.is_empty())
	var v1_facts := facts.duplicate(true)
	v1_facts["algorithm_version"] = 1
	var v1_key := String(menu_script.compute_skirmish_content_identity(v1_facts))
	var v2_key := String(menu_script.compute_skirmish_content_identity(facts))
	_check("v1_and_current_keys_differ", v1_key != "" and v2_key != "" and v1_key != v2_key, "v1=%s v2=%s" % [v1_key.left(12), v2_key.left(12)])
	root.remove_child(probe)
	probe.free()
	await process_frame
	var stale_notes := {}
	for map_id in CASTLE_MAPS:
		stale_notes[map_id] = V1_REFUSAL
	var file := FileAccess.open(menu_script.SKIRMISH_CACHE_PATH, FileAccess.WRITE)
	_check("stale_v1_cache_planted", file != null)
	if file != null:
		file.store_string(JSON.stringify({
			"schema": menu_script.SKIRMISH_CACHE_SCHEMA,
			"schemaVersion": menu_script.SKIRMISH_CACHE_SCHEMA_VERSION,
			"contentIdentity": v1_key,
			"availability": {},
			"map_notes": stale_notes,
		}, "\t"))
		file.close()
	# ---- 2. boot the menu against that file ----
	var menu := packed.instantiate()
	root.add_child(menu)
	frames = 0
	while not bool(menu.get("_skirmish_options_ready")) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	_check("menu_list_ready", bool(menu.get("_skirmish_options_ready")), "frames=%d" % frames)
	menu.call("_load_known_skirmish_verdicts")
	var notes: Dictionary = menu.get("_skirmish_map_notes")
	var replayed := 0
	for map_id in CASTLE_MAPS:
		if String(notes.get(map_id, "")).begins_with("castle gameplay unsupported"):
			replayed += 1
	_check("stale_v1_castle_refusals_not_replayed", replayed == 0, "replayed=%d of %d" % [replayed, CASTLE_MAPS.size()])
	# ---- 3. every castle map validates playable on pick, row enabled + tooltip ----
	var offered := 0
	var playable := 0
	var tooltips := 0
	for map_id in CASTLE_MAPS:
		var row_button: Button = null
		for entry_value in menu.solo_flyout.map_rows:
			if String((entry_value as Dictionary)["map_id"]) == map_id:
				row_button = (entry_value as Dictionary)["button"] as Button
				break
		if row_button == null:
			continue
		offered += 1
		var verdict := String(menu.validate_skirmish_map(map_id))
		if verdict == "":
			playable += 1
		else:
			print("CASTLE_LOBBY NOTE %s verdict=%s" % [map_id, verdict])
	menu.call("_refresh_map_row_states")
	var enabled := 0
	for map_id in CASTLE_MAPS:
		for entry_value in menu.solo_flyout.map_rows:
			if String((entry_value as Dictionary)["map_id"]) == map_id:
				var b := (entry_value as Dictionary)["button"] as Button
				if not b.disabled:
					enabled += 1
				if b.tooltip_text.begins_with("Playable") and b.tooltip_text.contains("defendable-gates"):
					tooltips += 1
	_check("all_castle_maps_offered", offered == CASTLE_MAPS.size(), "offered=%d" % offered)
	_check("all_castle_maps_validate_playable", playable == CASTLE_MAPS.size(), "playable=%d/%d" % [playable, CASTLE_MAPS.size()])
	_check("all_castle_rows_enabled", enabled == CASTLE_MAPS.size(), "enabled=%d/%d" % [enabled, CASTLE_MAPS.size()])
	_check("all_castle_rows_carry_partial_siege_tooltip", tooltips == CASTLE_MAPS.size(), "tooltips=%d/%d" % [tooltips, CASTLE_MAPS.size()])
	root.remove_child(menu)
	menu.free()
	await process_frame
	menu_script.clear_skirmish_availability_cache()
	_finish()


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		_passed += 1
		print("CASTLE_LOBBY PASS %s" % name)
	else:
		_failed += 1
		print("CASTLE_LOBBY FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])


func _finish() -> void:
	_profiles.close()
	_runner_watchdog.stop()
	print("CASTLE_LOBBY_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
