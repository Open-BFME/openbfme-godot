extends SceneTree
## Menu-to-match CYCLE gate (verification survey: "no menu -> skirmish setup ->
## launch -> slice -> back-to-menu -> relaunch transition check").
##
## Three sections:
## 1. GameState.reset_match must return every retail menu-to-match selection
##    field (the newer retail_* seam: retail_team_setup, retail_mp_*,
##    retail_build_plots_only, RULES values, colors, map, start index) to its
##    declared default. Historic bug: reset_match predated these fields and
##    silently kept a previous session's roster/mode alive.
## 2. Two full solo cycles through the REAL surfaces: boot.tscn menu ->
##    configure skirmish rows -> validated apply_skirmish_selection handoff
##    (the exact path _on_retail runs before its scene change; the runner owns
##    scene lifecycle like the sibling MP smoke) -> retail_vertical_slice boots
##    ready_ok -> sim ticks to 60 -> teardown back to menu. Cycle 2 uses a
##    DIFFERENT config (Angmar row when converted, legacy two-row path vs the
##    3-row N-team path of cycle 1) and must be unaffected by cycle 1's stale
##    GameState roster. Between cycles: Performance counts return to the
##    post-cycle-1 baseline, root child count returns to the autoload set, no
##    orphan nodes.
## 3. Publish-gate negatives the sibling runners lack: corrupt / wrong-schema
##    selection fixtures, missing-on-disk supplement bundles and unsafe
##    supplement paths (synthetic fixtures in the OS cache dir only — the real
##    user:// selection is never touched: every ModLoader call passes explicit
##    fixture roots), and the menu's mounted-set launch gate staying honest
##    when the pack that can host the match vanishes from the mounted set.
##
## Slice scripts are loaded lazily inside _run (never top-level preloaded) to
## dodge the --script autoload-compile-order trap, like the sibling runners.

const BOOT_DEADLINE_MS := 300000
const TICK_DEADLINE_MS := 120000
const TARGET_TICK := 60
const SETTLE_FRAMES := 8

var passed := 0
var failed := 0
var _faction_manifest_script = null


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "MENU_MATCH_CYCLE_RUNNER")
	# The menu is the source of truth: env overrides off so the slice reads the
	# GameState selection, exactly like a real launch.
	for env_name in [
		"OPENBFME_SLICE_FACTION", "OPENBFME_SLICE_MAP", "OPENBFME_MP",
		"OPENBFME_MP_ADDRESS", "OPENBFME_MP_PORT", "OPENBFME_STARTER_ARMY",
		"OPENBFME_CONTROL_PORT",
	]:
		OS.set_environment(env_name, "")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	_faction_manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var menu_scene: PackedScene = load("res://scenes/boot.tscn")
	_check("scenes_parse", _faction_manifest_script != null and slice_scene != null and menu_scene != null)
	if _faction_manifest_script == null or slice_scene == null or menu_scene == null:
		return _finish()
	var game_state := root.get_node_or_null("GameState")
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_present", game_state != null and content_db != null and mod_loader != null)
	if game_state == null or content_db == null or mod_loader == null:
		return _finish()

	_section_reset_match_defaults(game_state)
	await _section_cycles(game_state, content_db, menu_scene, slice_scene)
	_section_publish_gate_negatives(mod_loader)
	_finish()


# ---------------------------------------------------------------------------
# Section 1: reset_match returns the retail selection seam to its defaults.
# ---------------------------------------------------------------------------
func _section_reset_match_defaults(game_state: Node) -> void:
	game_state.set("retail_mp_mode", "host")
	game_state.set("retail_mp_address", "10.0.0.9")
	game_state.set("retail_mp_port", 31999)
	game_state.set("retail_player_faction", "elves")
	game_state.set("retail_enemy_faction", "mordor")
	game_state.set("retail_initial_resources", 5000)
	game_state.set("retail_command_point_factor", 2.0)
	game_state.set("retail_build_plots_only", true)
	game_state.set("retail_player_start_index", 3)
	game_state.set("retail_player_color", Color(1, 0, 1))
	game_state.set("retail_enemy_color", Color(0, 1, 1))
	game_state.set("retail_map_id", "bfme2.map.sentinel")
	game_state.set("retail_team_setup", [{"team": 0, "faction": "elves"}])
	game_state.set("retail_mp_player_name", "Sentinel")
	game_state.set("retail_mp_peer_name", "Ghost")
	game_state.call("reset_match")
	_check_reset_defaults(game_state, "unit")


func _check_reset_defaults(game_state: Node, label: String) -> void:
	_check("%s_reset_clears_retail_mp_fields" % label,
		String(game_state.get("retail_mp_mode")) == ""
			and String(game_state.get("retail_mp_address")) == "127.0.0.1"
			and int(game_state.get("retail_mp_port")) == 26015
			and String(game_state.get("retail_mp_player_name")) == "Player"
			and String(game_state.get("retail_mp_peer_name")) == "Challenger",
		"mode=%s addr=%s port=%d" % [
			String(game_state.get("retail_mp_mode")),
			String(game_state.get("retail_mp_address")),
			int(game_state.get("retail_mp_port")),
		])
	_check("%s_reset_clears_retail_team_setup" % label,
		(game_state.get("retail_team_setup") as Array).is_empty(),
		"size=%d" % (game_state.get("retail_team_setup") as Array).size())
	_check("%s_reset_restores_retail_rules_defaults" % label,
		int(game_state.get("retail_initial_resources")) == -1
			and is_equal_approx(float(game_state.get("retail_command_point_factor")), 1.0)
			and not bool(game_state.get("retail_build_plots_only")),
		"resources=%d factor=%s plots=%s" % [
			int(game_state.get("retail_initial_resources")),
			str(game_state.get("retail_command_point_factor")),
			str(game_state.get("retail_build_plots_only")),
		])
	_check("%s_reset_restores_retail_selection_defaults" % label,
		String(game_state.get("retail_player_faction")) == "men"
			and String(game_state.get("retail_enemy_faction")) == "men"
			and String(game_state.get("retail_map_id")) == ""
			and int(game_state.get("retail_player_start_index")) == 0
			and (game_state.get("retail_player_color") as Color).is_equal_approx(Color(0.176, 0.302, 0.675))
			and (game_state.get("retail_enemy_color") as Color).is_equal_approx(Color(0.651, 0.125, 0.110)),
		"player=%s enemy=%s map=%s" % [
			String(game_state.get("retail_player_faction")),
			String(game_state.get("retail_enemy_faction")),
			String(game_state.get("retail_map_id")),
		])


# ---------------------------------------------------------------------------
# Section 2: two full menu -> match -> menu cycles with teardown assertions.
# ---------------------------------------------------------------------------
func _section_cycles(game_state: Node, content_db: Node, menu_scene: PackedScene, slice_scene: PackedScene) -> void:
	# ---- Cycle 1: 3-row cross-faction N-team config -----------------------
	var menu = menu_scene.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	_check("cycle1_menu_ready", menu.theme != null)

	# Mounted-set launch gate negative: with the host pack gone from the mounted
	# set, the launch gate must block with the honest reason and recover as soon
	# as the pack returns. (menu_skirmish_runner covers the registry-level
	# signals; this covers the shared host resolution the gate runs.)
	#
	# The row to pull is the one the gate itself resolves as host, found the same
	# way the gate finds it — by capability, in reverse load order. Looking it up
	# by the literal bfme2-men-vslice id asserted a NAME the runtime no longer
	# has any reason to care about, and would fail against the owner's own
	# six-faction selection, where the host pack is mounted under a different id.
	var pack_capability_script = load("res://src/content/pack_capability.gd")
	var pack_meta: Array = content_db.get("pack_meta")
	var removed_index := -1
	var removed_entry = null
	for index in range(pack_meta.size() - 1, -1, -1):
		var root_path := String((pack_meta[index] as Dictionary).get("root", ""))
		if root_path != "" and pack_capability_script.missing_host_slice_surfaces(root_path).is_empty():
			removed_index = index
			removed_entry = pack_meta[index]
			break
	_check("host_pack_present_in_mounted_set", removed_index >= 0)
	if removed_index >= 0:
		pack_meta.remove_at(removed_index)
		var gate_error := String(menu.retail_launch_error())
		_check("unmounted_host_pack_blocks_launch_with_reason",
			gate_error.contains("No mounted content pack provides the host slice surfaces")
				and gate_error.contains("(missing:"),
			gate_error)
		pack_meta.insert(removed_index, removed_entry)
		_check("remounted_host_pack_recovers_launch", String(menu.retail_launch_error()) == "",
			String(menu.retail_launch_error()))

	var availability: Dictionary = menu.get_retail_faction_availability()
	var converted: Array[String] = []
	for faction_id in ["men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar"]:
		if String(availability.get(faction_id, "missing")) == "":
			converted.append(faction_id)
	_check("at_least_one_converted_faction", not converted.is_empty(), str(availability))
	if converted.is_empty():
		menu.queue_free()
		await process_frame
		return

	var setup = menu.get_node("Center/SoloFlyout")
	var cycle1_map := _select_map_with_capacity(menu, setup, 3)
	_check("cycle1_three_start_map_selected", cycle1_map != "", "no available map with >=3 starts")
	var cycle1_rows := 2
	# Angmar leads the row-1 preference so cycle 2's Angmar relaunch reuses a
	# faction cycle 1 already warmed: the strict between-cycle object baseline
	# must only ever see true leaks, never first-touch static caches.
	var cycle1_row1 := _pick_faction(converted, ["angmar", "elves", "dwarves", "isengard", "mordor", "wild"])
	var cycle1_row2 := _pick_faction(converted, ["elves", "dwarves", "mordor", "isengard", "wild", "angmar"])
	if cycle1_map != "":
		setup.add_player_row()
		await process_frame
		cycle1_rows = int(setup.player_row_count)
		_check("cycle1_added_third_row", cycle1_rows == 3)
		_select_faction(setup.row_army_opts[0], "men")
		_select_faction(setup.row_army_opts[1], cycle1_row1)
		_select_faction(setup.row_army_opts[2], cycle1_row2)
		_select_meta(setup.row_difficulty_opts[1], "hard")
	else:
		# No 3-start map available: fall back to a legacy two-row cross-faction
		# cycle so the runner still exercises a full launch.
		_select_faction(setup.row_army_opts[0], "men")
		_select_faction(setup.row_army_opts[1], cycle1_row1)
	var cycle1_error := String(menu.retail_launch_error())
	_check("cycle1_launch_gate_open", cycle1_error == "" and not setup.play_btn.disabled, cycle1_error)
	_check("cycle1_selection_applies", bool(menu.apply_skirmish_selection()))
	var cycle1_setup: Array = game_state.get("retail_team_setup") as Array
	_check("cycle1_writes_advanced_roster", (cycle1_setup.size() == cycle1_rows) if cycle1_rows > 2 else cycle1_setup.is_empty(),
		"rows=%d descriptors=%d" % [cycle1_rows, cycle1_setup.size()])
	menu.queue_free()
	await process_frame
	await process_frame

	var cycle1_ok := await _boot_tick_teardown_slice(slice_scene, "cycle1", cycle1_rows, cycle1_row1)
	if not cycle1_ok:
		return

	# Post-cycle-1 baseline: static caches (ContentDB tables, theme/shader and
	# mesh caches) are warm; cycle 2 must return to these counts.
	var baseline := await _settled_counts()
	var baseline_children := root.get_child_count()
	print("MENU_MATCH_CYCLE baseline objects=%d resources=%d nodes=%d orphans=%d children=%d" % [
		baseline["objects"], baseline["resources"], baseline["nodes"], baseline["orphans"], baseline_children,
	])
	_check("cycle1_teardown_leaves_no_orphans", int(baseline["orphans"]) == 0, "orphans=%d" % int(baseline["orphans"]))

	# ---- Cycle 2: fresh menu, stale GameState, different (legacy) config ---
	# Production never resets GameState between matches; the menu's validated
	# rewrite is what protects the second launch. Prove the hazard is real,
	# then prove the rewrite covers it.
	_check("cycle2_sees_cycle1_stale_roster", (game_state.get("retail_team_setup") as Array).size() == cycle1_setup.size(),
		"stale=%d" % (game_state.get("retail_team_setup") as Array).size())
	var menu2 = menu_scene.instantiate()
	root.add_child(menu2)
	await process_frame
	await process_frame
	_check("cycle2_menu_ready", menu2.theme != null)
	var setup2 = menu2.get_node("Center/SoloFlyout")
	_check("cycle2_fresh_menu_defaults_two_rows", int(setup2.player_row_count) == 2)
	# Angmar when converted, else cycle 1's row-1 faction: either way the
	# faction set is a subset of cycle 1's, so the object baseline stays strict.
	var cycle2_enemy := "angmar" if converted.has("angmar") else cycle1_row1
	_select_faction(setup2.row_army_opts[0], "men")
	_select_faction(setup2.row_army_opts[1], cycle2_enemy)
	var cycle2_error := String(menu2.retail_launch_error())
	_check("cycle2_launch_gate_open", cycle2_error == "", cycle2_error)
	_check("cycle2_selection_applies", bool(menu2.apply_skirmish_selection()))
	_check("cycle2_rewrite_clears_stale_roster", (game_state.get("retail_team_setup") as Array).is_empty(),
		"size=%d" % (game_state.get("retail_team_setup") as Array).size())
	_check("cycle2_is_single_player", String(game_state.get("retail_mp_mode")) == "")
	_check("cycle2_enemy_faction_recorded", String(game_state.get("retail_enemy_faction")) == cycle2_enemy,
		String(game_state.get("retail_enemy_faction")))
	menu2.queue_free()
	await process_frame
	await process_frame

	var cycle2_ok := await _boot_tick_teardown_slice(slice_scene, "cycle2", 2, cycle2_enemy)
	if not cycle2_ok:
		return

	var counts := await _settled_counts()
	_check("cycle2_objects_return_to_baseline", int(counts["objects"]) <= int(baseline["objects"]),
		"objects %d > baseline %d" % [counts["objects"], baseline["objects"]])
	_check("cycle2_resources_return_to_baseline", int(counts["resources"]) <= int(baseline["resources"]),
		"resources %d > baseline %d" % [counts["resources"], baseline["resources"]])
	_check("cycle2_nodes_return_to_baseline", int(counts["nodes"]) <= int(baseline["nodes"]),
		"nodes %d > baseline %d" % [counts["nodes"], baseline["nodes"]])
	_check("cycle2_no_orphan_nodes", int(counts["orphans"]) <= int(baseline["orphans"]),
		"orphans %d > baseline %d" % [counts["orphans"], baseline["orphans"]])
	_check("cycle2_root_children_return_to_autoload_set", root.get_child_count() == baseline_children,
		"children %d != baseline %d" % [root.get_child_count(), baseline_children])

	# Integrated reset seam: after the full double cycle, one reset_match call
	# returns the whole retail selection to its defaults.
	game_state.call("reset_match")
	_check_reset_defaults(game_state, "post_cycle")


func _boot_tick_teardown_slice(slice_scene: PackedScene, label: String, expected_teams: int, expected_enemy: String) -> bool:
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var booted: bool = await _pump_until(func() -> bool:
		return bool(slice.ready_ok) or String(slice.failure_reason) != "", BOOT_DEADLINE_MS)
	_check("%s_slice_boots_ready" % label, booted and bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.queue_free()
		await process_frame
		return false
	var sim = slice.get("simulation")
	var roster: Array = sim.get("_team_roster") as Array
	_check("%s_sim_seats_expected_team_count" % label, roster.size() == expected_teams,
		"teams=%s expected=%d" % [str(roster), expected_teams])
	_check("%s_slice_enemy_faction_matches_selection" % label,
		String(slice.get("enemy_faction")) == expected_enemy,
		"enemy=%s expected=%s" % [String(slice.get("enemy_faction")), expected_enemy])
	var ticked: bool = await _pump_until(func() -> bool:
		return int(sim.tick_index) >= TARGET_TICK, TICK_DEADLINE_MS)
	_check("%s_sim_ticks_to_60" % label, ticked and int(sim.tick_index) >= TARGET_TICK,
		"tick=%d" % int(sim.tick_index))
	# Back to menu: the HUD's main-menu exit runs cleanup + scene free; the
	# runner owns scene lifecycle, so it runs the same teardown surface.
	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	await process_frame
	return true


# ---------------------------------------------------------------------------
# Section 3: publish-gate negatives against synthetic OS-cache fixtures.
# The real user:// selection and .private content are never touched: every
# ModLoader call below passes explicit fixture cache/selection paths.
# ---------------------------------------------------------------------------
func _section_publish_gate_negatives(mod_loader: Node) -> void:
	var fixture_root := OS.get_cache_dir().replace("\\", "/").path_join("openbfme-menu-cycle-fixtures")
	DirAccess.make_dir_recursive_absolute(fixture_root)

	# (a) Corrupt selection document: not JSON at all.
	var corrupt_path := fixture_root.path_join("corrupt-selection.json")
	_write_text(corrupt_path, "{ this is not json !!!")
	var before := (mod_loader.get("diagnostics") as Array).size()
	var corrupt_selected := String(mod_loader.call("selected_user_pack_root", fixture_root, corrupt_path))
	_check("corrupt_selection_yields_no_pack", corrupt_selected == "", corrupt_selected)
	_check("corrupt_selection_diagnosed_honestly",
		_new_diagnostics(mod_loader, before).contains("Content selection is not a JSON object"),
		_new_diagnostics(mod_loader, before))

	# (b) Wrong selection schema fails closed with the schema reason.
	var wrong_schema_path := fixture_root.path_join("wrong-schema-selection.json")
	_write_text(wrong_schema_path, JSON.stringify({"schema": "bogus.schema", "schemaVersion": 99, "activePack": "x/y"}))
	before = (mod_loader.get("diagnostics") as Array).size()
	var wrong_selected := String(mod_loader.call("selected_user_pack_root", fixture_root, wrong_schema_path))
	_check("wrong_schema_selection_yields_no_pack", wrong_selected == "")
	_check("wrong_schema_selection_diagnosed_honestly",
		_new_diagnostics(mod_loader, before).contains("Unsupported content selection schema"),
		_new_diagnostics(mod_loader, before))

	# (c) Selection whose activePack bundle directory exists but carries no
	# pack.json (the bundle bytes are missing on disk). A wholly absent
	# directory instead trips the link-containment guard ("unsafe activePack
	# path") — also fail-closed, but this case pins the honest missing-bundle
	# reason.
	DirAccess.make_dir_recursive_absolute(fixture_root.path_join("ghost-pack/deadbeef"))
	var missing_active_path := fixture_root.path_join("missing-active-selection.json")
	_write_text(missing_active_path, JSON.stringify({
		"schema": "openbfme.pack-selection", "schemaVersion": 0, "activePack": "ghost-pack/deadbeef",
	}))
	before = (mod_loader.get("diagnostics") as Array).size()
	var missing_selected := String(mod_loader.call("selected_user_pack_root", fixture_root, missing_active_path))
	_check("missing_active_pack_yields_no_pack", missing_selected == "")
	_check("missing_active_pack_diagnosed_honestly",
		_new_diagnostics(mod_loader, before).contains("Selected content pack is invalid or missing"),
		_new_diagnostics(mod_loader, before))

	# (d) Valid active pack, but a named supplement bundle is missing on disk:
	# the mounted set must stay honest (active pack loads, supplement is
	# diagnosed and skipped, never searched for).
	var pack_dir := fixture_root.path_join("fixture-pack/cafef00d")
	DirAccess.make_dir_recursive_absolute(pack_dir)
	_write_text(pack_dir.path_join("pack.json"), JSON.stringify({"id": "fixture-pack"}))
	DirAccess.make_dir_recursive_absolute(fixture_root.path_join("ghost-supplement/deadbeef"))
	var supplement_path := fixture_root.path_join("missing-supplement-selection.json")
	_write_text(supplement_path, JSON.stringify({
		"schema": "openbfme.pack-selection", "schemaVersion": 0,
		"activePack": "fixture-pack/cafef00d",
		"supplementalPacks": ["ghost-supplement/deadbeef"],
	}))
	var active_ok := String(mod_loader.call("selected_user_pack_root", fixture_root, supplement_path))
	_check("fixture_active_pack_resolves", active_ok != "" and active_ok.ends_with("fixture-pack/cafef00d"), active_ok)
	before = (mod_loader.get("diagnostics") as Array).size()
	var supplements: Array = mod_loader.call("selected_pack_supplements", fixture_root, supplement_path)
	_check("missing_supplement_bundle_skipped", supplements.is_empty(), str(supplements))
	_check("missing_supplement_bundle_diagnosed_honestly",
		_new_diagnostics(mod_loader, before).contains("Supplemental content pack is invalid or missing"),
		_new_diagnostics(mod_loader, before))

	# (e) Unsafe (escaping) supplement path fails closed with its own reason.
	var unsafe_path := fixture_root.path_join("unsafe-supplement-selection.json")
	_write_text(unsafe_path, JSON.stringify({
		"schema": "openbfme.pack-selection", "schemaVersion": 0,
		"activePack": "fixture-pack/cafef00d",
		"supplementalPacks": ["../escape-attempt"],
	}))
	before = (mod_loader.get("diagnostics") as Array).size()
	var unsafe_supplements: Array = mod_loader.call("selected_pack_supplements", fixture_root, unsafe_path)
	_check("unsafe_supplement_path_skipped", unsafe_supplements.is_empty(), str(unsafe_supplements))
	_check("unsafe_supplement_path_diagnosed_honestly",
		_new_diagnostics(mod_loader, before).contains("unsafe supplementalPacks path"),
		_new_diagnostics(mod_loader, before))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
func _pick_faction(converted: Array[String], preferences: Array) -> String:
	for preference in preferences:
		if converted.has(String(preference)):
			return String(preference)
	return "men"


func _select_map_with_capacity(menu, setup, minimum_capacity: int) -> String:
	var map_rows: Array = setup.map_rows
	for index in range(map_rows.size()):
		var row: Dictionary = map_rows[index]
		var button := row["button"] as Button
		if button == null or button.disabled:
			continue
		if int(row.get("players", 0)) < minimum_capacity:
			continue
		menu._on_map_row_pressed(index)
		if int(setup.max_player_count) >= minimum_capacity:
			return String(row.get("map_id", ""))
	return ""


func _select_faction(option: OptionButton, faction_id: String) -> void:
	for index in range(option.item_count):
		if String(option.get_item_metadata(index)) == faction_id:
			option.select(index)
			option.item_selected.emit(index)
			return


func _select_meta(option: OptionButton, value: Variant) -> void:
	for index in range(option.item_count):
		if option.get_item_metadata(index) == value:
			option.select(index)
			option.item_selected.emit(index)
			return


func _settled_counts() -> Dictionary:
	for _index in range(SETTLE_FRAMES):
		await process_frame
	return {
		"objects": int(Performance.get_monitor(Performance.OBJECT_COUNT)),
		"resources": int(Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT)),
		"nodes": int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)),
		"orphans": int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)),
	}


func _pump_until(predicate: Callable, deadline_ms: int) -> bool:
	var deadline := Time.get_ticks_msec() + deadline_ms
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(predicate.call()):
			return true
	return false


func _write_text(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(text)
		file.close()


func _new_diagnostics(mod_loader: Node, since_index: int) -> String:
	var diagnostics: Array = mod_loader.get("diagnostics") as Array
	var recent: Array = []
	for index in range(since_index, diagnostics.size()):
		recent.append(String(diagnostics[index]))
	return " | ".join(PackedStringArray(recent))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MENU_MATCH_CYCLE PASS %s" % name)
	else:
		failed += 1
		printerr("MENU_MATCH_CYCLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("MENU_MATCH_CYCLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
