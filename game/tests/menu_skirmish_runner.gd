extends SceneTree
## Headless gate for the main menu's BFME2 skirmish setup: faction
## availability must come from the same fail-closed signals the retail slice
## uses, unavailable factions must be disabled in the dropdowns, and the
## selection must reach GameState only through a validated launch handoff.
##
## The slice scripts are loaded at runtime (not preloaded): a --script main
## loop compiles its top-level preloads before the autoload globals the slice
## references (ContentDB, ModLoader) are registered.

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var faction_manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	_check("slice_scripts_load", faction_manifest_script != null and slice_script != null)
	if faction_manifest_script == null or slice_script == null:
		_finish()
		return
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	_check("menu_ready_with_theme", menu.theme != null)
	var content_db := root.get_node_or_null("ContentDB")
	var game_state := root.get_node_or_null("GameState")
	_check("autoloads_present", content_db != null and game_state != null)
	if content_db == null or game_state == null:
		menu.queue_free()
		await process_frame
		_finish()
		return

	var player_opt := menu.get_node("Center/SkirmishGrid/Player") as OptionButton
	var enemy_opt := menu.get_node("Center/SkirmishGrid/Enemy") as OptionButton
	var map_opt := menu.get_node("Center/SkirmishGrid/Map") as OptionButton
	var retail_btn := menu.get_node("Center/Retail") as Button
	_check("skirmish_controls_present", player_opt != null and enemy_opt != null and map_opt != null and retail_btn != null)
	if player_opt == null or enemy_opt == null or map_opt == null or retail_btn == null:
		menu.queue_free()
		await process_frame
		_finish()
		return

	# The skirmish setup offers exactly the six BFME2 factions, in order, with
	# the slice's internal ids as metadata on both sides.
	var expected := [
		["men", "Men"], ["elves", "Elves"], ["dwarves", "Dwarves"],
		["isengard", "Isengard"], ["mordor", "Mordor"], ["wild", "Goblins"],
	]
	_check("six_factions_offered_both_sides", player_opt.item_count == 6 and enemy_opt.item_count == 6)
	var ids_match := true
	for index in range(mini(6, player_opt.item_count)):
		if String(player_opt.get_item_metadata(index)) != String(expected[index][0]) or String(enemy_opt.get_item_metadata(index)) != String(expected[index][0]):
			ids_match = false
		if player_opt.get_item_text(index) != String(expected[index][1]) and player_opt.get_item_text(index) != String(expected[index][1]) + " (not converted)":
			ids_match = false
	_check("faction_ids_and_display_names", ids_match)

	# Fords of Isen II is the only map choice and is shown selected.
	_check("map_is_fords_only_selected", map_opt.item_count == 1 and map_opt.get_item_text(0) == "Fords of Isen II" and map_opt.selected == 0 and String(map_opt.get_item_metadata(0)) == String(slice_script.MAP_ID))

	# Availability comes from the slice's own fail-closed signals: the men
	# pack gate for the default faction, RetailFactionManifest.from_registries
	# for every other faction.
	var availability: Dictionary = menu.get_retail_faction_availability()
	_check("availability_covers_six_factions", availability.size() == 6)
	var availability_matches_slice_signals := true
	for entry in expected:
		var faction_id := String(entry[0])
		if not availability.has(faction_id):
			availability_matches_slice_signals = false
			continue
		if faction_id == String(faction_manifest_script.DEFAULT_FACTION):
			continue
		var expected_note := String(faction_manifest_script.from_registries(
			faction_id,
			content_db.call("get_playable_unit_runtimes"),
			content_db.call("get_playable_structure_runtimes")
		).get("_error", ""))
		if String(availability.get(faction_id, "")) != expected_note:
			availability_matches_slice_signals = false
	_check("availability_matches_slice_fail_closed_signals", availability_matches_slice_signals)
	_check("missing_content_signal_is_specific", String(faction_manifest_script.from_registries("mordor", {}, {}).get("_error", "")).contains("playableStructure"))

	# Men is the converted faction today: the men pack gate passes, every other
	# faction is flagged and disabled in both dropdowns.
	_check("men_faction_available", String(availability.get("men", "missing")) == "", str(availability.get("men", "missing")))
	var unavailable_flagged_and_disabled := true
	var converted_count := 0
	for index in range(player_opt.item_count):
		var note := String(availability.get(String(player_opt.get_item_metadata(index)), ""))
		if note == "":
			converted_count += 1
		if player_opt.is_item_disabled(index) != (note != "") or enemy_opt.is_item_disabled(index) != (note != ""):
			unavailable_flagged_and_disabled = false
		if (note != "") != player_opt.get_item_text(index).ends_with(" (not converted)"):
			unavailable_flagged_and_disabled = false
		if note != "" and not player_opt.get_item_tooltip(index).contains(note):
			unavailable_flagged_and_disabled = false
	_check("unconverted_factions_disabled_with_note", unavailable_flagged_and_disabled)
	_check("only_men_converted_today", converted_count == 1, "converted=%d" % converted_count)

	# Default selection is the converted faction on both sides and may launch.
	_check("default_selection_is_men_vs_men", String(player_opt.get_item_metadata(player_opt.selected)) == "men" and String(enemy_opt.get_item_metadata(enemy_opt.selected)) == "men")
	_check("launch_ready_by_default", String(menu.retail_launch_error()) == "" and not retail_btn.disabled, String(menu.retail_launch_error()))

	# Selection -> GameState handoff happens only through the validated path.
	game_state.set("retail_player_faction", "sentinel-player")
	game_state.set("retail_enemy_faction", "sentinel-enemy")
	_check("valid_selection_reaches_game_state", bool(menu.apply_skirmish_selection()) and String(game_state.get("retail_player_faction")) == "men" and String(game_state.get("retail_enemy_faction")) == "men")

	# A faction whose pack content is missing can never be handed off, even if
	# its disabled dropdown entry is forced programmatically.
	var disabled_index := -1
	for index in range(enemy_opt.item_count):
		if enemy_opt.is_item_disabled(index):
			disabled_index = index
			break
	_check("unconverted_option_exists", disabled_index >= 0)
	game_state.set("retail_player_faction", "sentinel-player")
	game_state.set("retail_enemy_faction", "sentinel-enemy")
	enemy_opt.select(disabled_index)
	var blocked_launch := not bool(menu.apply_skirmish_selection())
	_check("unconverted_enemy_launch_blocked", blocked_launch and String(menu.retail_launch_error()) != "")
	_check("blocked_launch_leaves_game_state_untouched", String(game_state.get("retail_player_faction")) == "sentinel-player" and String(game_state.get("retail_enemy_faction")) == "sentinel-enemy")
	_check("blocked_launch_disables_button", retail_btn.disabled)
	var hint := menu.get_node("Center/RetailHint") as Label
	_check("blocked_launch_explains_itself", hint != null and hint.text.contains("not converted"), hint.text if hint != null else "missing")

	# Restoring the converted selection re-arms the launch.
	for index in range(enemy_opt.item_count):
		if not enemy_opt.is_item_disabled(index):
			enemy_opt.select(index)
			break
	menu.apply_skirmish_selection()
	_check("reselection_recovers_launch", String(menu.retail_launch_error()) == "" and String(game_state.get("retail_enemy_faction")) == "men")

	# The legacy prototype entry point keeps its synthetic roster untouched.
	var legacy_player := menu.get_node("Center/LegacyGrid/Player") as OptionButton
	var legacy_map := menu.get_node("Center/LegacyGrid/Map") as OptionButton
	_check("legacy_grid_intact", legacy_player != null and legacy_player.item_count == 4 and String(legacy_player.get_item_metadata(0)) == "gondor" and legacy_map != null and legacy_map.item_count > 0)

	game_state.set("retail_player_faction", "men")
	game_state.set("retail_enemy_faction", "men")
	menu.queue_free()
	await process_frame
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MENU_SKIRMISH PASS %s" % name)
	else:
		failed += 1
		printerr("MENU_SKIRMISH FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("MENU_SKIRMISH_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
