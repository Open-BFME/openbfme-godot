extends SceneTree

## N-team skirmish SETUP proof: the menu's variable player rows build a valid
## N-team roster descriptor list, write it to GameState, and validate fail-closed
## (per-row faction availability, at least two hostile alliances, distinct
## authored starts). The default two-row setup stays legacy (empty descriptor
## list). Finally the real slice seam is exercised with the detached-node
## technique: an injected N-team GameState makes the slice call
## simulation.configure_team_roster with one descriptor per team.
##
## Slice scripts are loaded lazily inside _run (never top-level preloaded) to
## dodge the --script autoload-compile-order trap, exactly like the sibling
## menu runners.

var passed := 0
var failed := 0


func _initialize() -> void:
	# The menu is the source of truth here: force the env overrides off so the
	# slice seam reads the injected GameState, not a headless faction pin.
	OS.set_environment("OPENBFME_SLICE_FACTION", "")
	OS.set_environment("OPENBFME_SLICE_MAP", "")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null)
	if packed == null:
		return _finish()
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame

	var game_state := root.get_node_or_null("GameState")
	var setup = menu.get_node_or_null("Center/SoloFlyout")
	_check("autoloads_and_setup_present", game_state != null and setup != null)
	if game_state == null or setup == null:
		menu.queue_free()
		await process_frame
		return _finish()

	# ------------------------------------------------------------------
	# (1) Default two-row setup: valid, launch-ready, and legacy (no N-team
	# descriptor list — the slice keeps its byte-identical two-team path).
	# ------------------------------------------------------------------
	_check("default_is_two_rows", int(setup.player_row_count) == 2 and setup.row_army_opts.size() == 2)
	game_state.set("retail_team_setup", [{"sentinel": true}])
	var default_applied: bool = menu.apply_skirmish_selection()
	_check("default_launch_valid", default_applied and String(menu.retail_launch_error()) == "")
	_check(
		"default_setup_writes_empty_descriptor_list",
		(game_state.get("retail_team_setup") as Array).is_empty(),
		"size=%d" % (game_state.get("retail_team_setup") as Array).size()
	)
	_check(
		"default_legacy_fields_preserved",
		String(game_state.get("retail_player_faction")) == "men"
			and String(game_state.get("retail_enemy_faction")) == "men",
		"%s/%s" % [String(game_state.get("retail_player_faction")), String(game_state.get("retail_enemy_faction"))]
	)

	# ------------------------------------------------------------------
	# (2) Three-row Men / Elves / Men with mixed difficulties on a >=3-start map.
	# ------------------------------------------------------------------
	var three_map: String = _select_map_with_capacity(menu, setup, 3)
	_check("three_player_map_selected", three_map != "", "no available map with >=3 starts")
	if three_map != "":
		setup.add_player_row()
		await process_frame
		_check("added_third_row", int(setup.player_row_count) == 3 and setup.row_army_opts.size() == 3)
		_select_faction(setup.row_army_opts[0], "men")
		_select_faction(setup.row_army_opts[1], "elves")
		_select_faction(setup.row_army_opts[2], "men")
		# Mixed AI difficulties (row 0 is the human).
		_select_meta(setup.row_difficulty_opts[1], "hard")
		_select_meta(setup.row_difficulty_opts[2], "brutal")
		var three_error: String = menu.retail_launch_error()
		_check("three_row_launch_valid", three_error == "", three_error)
		var three_applied: bool = menu.apply_skirmish_selection()
		var roster: Array = game_state.get("retail_team_setup")
		_check("three_row_reaches_game_state", three_applied and roster.size() == 3, "size=%d" % roster.size())
		if roster.size() == 3:
			var factions := PackedStringArray()
			var teams := {}
			var starts := {}
			var difficulties := PackedStringArray()
			for descriptor_value in roster:
				var descriptor := descriptor_value as Dictionary
				factions.append(String(descriptor.get("faction", "")))
				teams[int(descriptor.get("team", -1))] = true
				starts[int(descriptor.get("start_index", -1))] = true
				difficulties.append(String(descriptor.get("difficulty", "")))
			_check("three_row_factions_are_men_elves_men", Array(factions) == ["men", "elves", "men"], str(factions))
			_check("three_row_team_ids_distinct_no_neutral", teams.size() == 3 and not teams.has(2), str(teams.keys()))
			_check("three_row_start_indices_distinct", starts.size() == 3, str(starts.keys()))
			_check(
				"three_row_mixed_difficulties",
				String(roster[0].get("difficulty")) == "medium"
					and String(roster[1].get("difficulty")) == "hard"
					and String(roster[2].get("difficulty")) == "brutal",
				str(difficulties)
			)
			_check(
				"three_row_controllers",
				String(roster[0].get("controller")) == "human"
					and String(roster[1].get("controller")) == "ai"
					and String(roster[2].get("controller")) == "ai"
			)

	# ------------------------------------------------------------------
	# (3) All-allied rejection: every row shares one team number -> no enemy.
	# ------------------------------------------------------------------
	if setup.player_row_count >= 3:
		for row in range(setup.team_dropdowns.size()):
			_select_meta(setup.team_dropdowns[row], 1)
		var all_allied_error: String = menu.retail_launch_error()
		game_state.set("retail_team_setup", [{"sentinel": true}])
		var blocked: bool = not menu.apply_skirmish_selection()
		_check("all_allied_rejected", blocked and all_allied_error.to_lower().contains("team"), all_allied_error)
		_check(
			"all_allied_leaves_descriptor_list_untouched",
			(game_state.get("retail_team_setup") as Array).size() == 1
		)
		# Restore distinct alliances.
		for row in range(setup.team_dropdowns.size()):
			_select_meta(setup.team_dropdowns[row], row + 1)

	# ------------------------------------------------------------------
	# (4) Per-row faction-availability rejection: force an unconvertible faction
	# on one row (skipped only if every faction is already converted).
	# ------------------------------------------------------------------
	var disabled_index := -1
	for index in range(setup.row_army_opts[1].item_count):
		if setup.row_army_opts[1].is_item_disabled(index):
			disabled_index = index
			break
	if disabled_index >= 0:
		setup.row_army_opts[1].select(disabled_index)
		setup.row_army_opts[1].item_selected.emit(disabled_index)
		var faction_error: String = menu.retail_launch_error()
		var faction_blocked: bool = not menu.apply_skirmish_selection()
		_check(
			"unconvertible_row_faction_rejected",
			faction_blocked and faction_error.contains("not converted"),
			faction_error
		)
		_select_faction(setup.row_army_opts[1], "elves")
	else:
		_check("unconvertible_row_faction_rejected", true, "all factions converted; rejection path vacuously satisfied")

	# ------------------------------------------------------------------
	# (5) 1v3 setup on a >=4-start map: one human vs three allied AI (two
	# alliances, four teams, four distinct starts).
	# ------------------------------------------------------------------
	var four_map: String = _select_map_with_capacity(menu, setup, 4)
	_check("four_player_map_selected", four_map != "", "no available map with >=4 starts")
	if four_map != "":
		while setup.player_row_count < 4:
			setup.add_player_row()
			await process_frame
		_check("grew_to_four_rows", int(setup.player_row_count) == 4)
		for row in range(setup.row_army_opts.size()):
			_select_faction(setup.row_army_opts[row], "men")
		# Human = team 1; the three AI share team 2 (1v3).
		_select_meta(setup.team_dropdowns[0], 1)
		for row in range(1, setup.team_dropdowns.size()):
			_select_meta(setup.team_dropdowns[row], 2)
		var onevthree_error: String = menu.retail_launch_error()
		_check("one_v_three_launch_valid", onevthree_error == "", onevthree_error)
		var onevthree_applied: bool = menu.apply_skirmish_selection()
		var four_roster: Array = game_state.get("retail_team_setup")
		var alliances := {}
		var four_teams := {}
		var four_starts := {}
		for descriptor_value in four_roster:
			var descriptor := descriptor_value as Dictionary
			alliances[int(descriptor.get("alliance", -1))] = true
			four_teams[int(descriptor.get("team", -1))] = true
			four_starts[int(descriptor.get("start_index", -1))] = true
		_check(
			"one_v_three_roster",
			onevthree_applied and four_roster.size() == 4 and alliances.size() == 2
				and four_teams.size() == 4 and four_starts.size() == 4,
			"rows=%d alliances=%s teams=%s starts=%s" % [four_roster.size(), str(alliances.keys()), str(four_teams.keys()), str(four_starts.keys())]
		)

	# ------------------------------------------------------------------
	# (6) Real slice seam (detached node): an injected N-team GameState makes the
	# slice call configure_team_roster with one descriptor per team.
	# ------------------------------------------------------------------
	var VerticalSliceScript = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = VerticalSliceScript.new()
	game_state.set("retail_team_setup", [
		{"team": 0, "faction": "men", "controller": "human", "difficulty": "medium", "alliance": 1},
		{"team": 1, "faction": "men", "controller": "ai", "difficulty": "hard", "alliance": 2},
		{"team": 3, "faction": "men", "controller": "ai", "difficulty": "brutal", "alliance": 3},
	])
	var normalized: Array = slice._menu_team_setup(game_state)
	_check("slice_reads_injected_setup", normalized.size() == 3, "size=%d" % normalized.size())
	var stub := RosterStub.new()
	var applied_roster: Array = slice._configure_simulation_team_roster(stub, game_state)
	_check(
		"slice_configures_three_team_roster",
		stub.received.size() == 3 and applied_roster.size() == 3,
		"received=%d applied=%d" % [stub.received.size(), applied_roster.size()]
	)
	if stub.received.size() == 3:
		_check(
			"slice_roster_maps_controller_and_difficulty",
			bool(stub.received[0].get("is_ai")) == false
				and bool(stub.received[1].get("is_ai")) == true
				and String(stub.received[1].get("difficulty")) == "hard"
				and int(stub.received[2].get("team")) == 3
				and stub.received[2].get("alliance") == 3
		)
	# An empty injected setup restores the legacy path: no roster is applied.
	game_state.set("retail_team_setup", [])
	var empty_stub := RosterStub.new()
	var empty_applied: Array = slice._configure_simulation_team_roster(empty_stub, game_state)
	_check(
		"slice_empty_setup_is_legacy_default",
		empty_applied.is_empty() and empty_stub.received.is_empty()
	)
	slice.free()

	game_state.set("retail_team_setup", [])
	menu.queue_free()
	await process_frame
	_finish()


# A stand-in for the simulation that records the injected roster.
class RosterStub:
	var received: Array = []
	func configure_team_roster(descriptors: Array) -> void:
		received = descriptors.duplicate(true)


func _select_map_with_capacity(menu, setup, minimum_capacity: int) -> String:
	## Press the first available map row whose authored player count meets the
	## capacity, returning its map id (or "" when none qualifies).
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


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_NTEAM_SETUP PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_NTEAM_SETUP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_NTEAM_SETUP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
