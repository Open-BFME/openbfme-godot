extends SceneTree
## Headless gate for the main menu's BFME2 skirmish setup: faction
## availability must come from the same fail-closed signals the retail slice
## uses, unavailable factions must be disabled in the dropdowns, and the
## selection must reach GameState only through a validated launch handoff.
##
## The slice scripts are loaded at runtime (not preloaded): a --script main
## loop compiles its top-level preloads before the autoload globals the slice
## references (ContentDB, ModLoader) are registered.

const SettingsScript = preload("res://src/ui/user_settings.gd")

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

	var player_opt := menu.find_child("PlayerArmy", true, false) as OptionButton
	var enemy_opt := menu.find_child("EnemyArmy", true, false) as OptionButton
	var retail_btn := menu.find_child("Retail", true, false) as Button
	var setup := menu.get_node("Center/SoloFlyout")
	var map_rows: Array = setup.get("map_rows")
	_check("skirmish_controls_present", player_opt != null and enemy_opt != null and retail_btn != null and map_rows.size() == 5)
	if player_opt == null or enemy_opt == null or retail_btn == null or map_rows.is_empty():
		menu.queue_free()
		await process_frame
		_finish()
		return

	# The skirmish setup offers exactly the six BFME2 factions plus Angmar, in
	# order, with the slice's internal ids as metadata on both sides.
	var expected := [
		["men", "Men"], ["elves", "Elves"], ["dwarves", "Dwarves"],
		["isengard", "Isengard"], ["mordor", "Mordor"], ["wild", "Goblins"],
		["angmar", "Angmar"],
	]
	_check("seven_factions_offered_both_sides", player_opt.item_count == 7 and enemy_opt.item_count == 7)
	var ids_match := true
	for index in range(mini(7, player_opt.item_count)):
		if String(player_opt.get_item_metadata(index)) != String(expected[index][0]) or String(enemy_opt.get_item_metadata(index)) != String(expected[index][0]):
			ids_match = false
		if player_opt.get_item_text(index) != String(expected[index][1]) and player_opt.get_item_text(index) != String(expected[index][1]) + " (not converted)":
			ids_match = false
	_check("faction_ids_and_display_names", ids_match)

	# The map list carries every cooked map with its authored player count;
	# Fords of Isen II remains the default selection.
	_check("map_list_offers_five_choices", map_rows.size() == 5, "got %d maps" % map_rows.size())
	var expected_players := [2, 3, 4, 6, 8]
	var players_match := map_rows.size() == expected_players.size()
	for index in range(mini(expected_players.size(), map_rows.size())):
		var row_button := map_rows[index]["button"] as Button
		if int(map_rows[index].get("players", -1)) != expected_players[index]:
			players_match = false
		if row_button != null and row_button.get_meta("player_count", -1) != expected_players[index]:
			players_match = false
	_check("map_list_player_counts_from_pack", players_match, str(expected_players))
	var first_row := map_rows[0]["button"] as Button
	_check(
		"map_default_is_fords",
		int(setup.get("selected_map_row")) == 0
			and String(map_rows[0].get("map_id", "")) == String(slice_script.MAP_ID)
			and first_row != null
			and first_row.text.begins_with("Fords of Isen II"),
		"selected=%s text=%s" % [str(setup.get("selected_map_row")), first_row.text if first_row != null else ""]
	)
	# MAP/RULES tabs switch their content; RULES persists to GameState live.
	var map_tab := menu.find_child("MapTab", true, false) as Button
	var rules_tab := menu.find_child("RulesTab", true, false) as Button
	var map_content := menu.find_child("MapContent", true, false)
	var rules_content := menu.find_child("RulesContent", true, false)
	_check("tabs_present", map_tab != null and rules_tab != null and map_content != null and rules_content != null)
	if rules_tab != null:
		rules_tab.emit_signal("pressed")
		_check("rules_tab_switches", rules_content.visible and not map_content.visible)
		map_tab.emit_signal("pressed")
		_check("map_tab_switches_back", map_content.visible and not rules_content.visible)
	var resources_opt := menu.find_child("InitialResources", true, false) as OptionButton
	var factor_opt := menu.find_child("CpFactor", true, false) as OptionButton
	var rules_reset := menu.find_child("RulesReset", true, false) as Button
	_check("rules_controls_present", resources_opt != null and factor_opt != null and rules_reset != null)
	_check("rules_defaults_match_slice_authored", int(game_state.get("retail_initial_resources")) == -1 and is_equal_approx(float(game_state.get("retail_command_point_factor")), 1.0))
	_select_option_by_metadata_value(resources_opt, 2000)
	_select_option_by_metadata_value(factor_opt, 2.0)
	_check(
		"rules_changes_reach_game_state",
		int(game_state.get("retail_initial_resources")) == 2000
			and is_equal_approx(float(game_state.get("retail_command_point_factor")), 2.0),
		"resources=%d factor=%s" % [int(game_state.get("retail_initial_resources")), str(game_state.get("retail_command_point_factor"))]
	)
	# BFME1 build-plots-only toggle: defaults to freeform (false), and flipping the
	# RULES tab control to "BFME1 Plots" reaches GameState live through the same
	# validated handoff as resources/factor.
	var build_mode_opt := menu.find_child("BuildMode", true, false) as OptionButton
	_check("build_mode_control_present", build_mode_opt != null and build_mode_opt.item_count == 2)
	_check("build_mode_defaults_freeform", not bool(game_state.get("retail_build_plots_only")))
	if build_mode_opt != null:
		_select_option_by_metadata_value(build_mode_opt, true)
		_check("build_mode_toggle_reaches_game_state", bool(game_state.get("retail_build_plots_only")))
	rules_reset.emit_signal("pressed")
	_check(
		"rules_reset_restores_defaults",
		int(game_state.get("retail_initial_resources")) == 1200
			and is_equal_approx(float(game_state.get("retail_command_point_factor")), 1.0)
			and not bool(game_state.get("retail_build_plots_only"))
	)
	# Honestly disabled columns and toggles carry their reasons as tooltips. Team
	# is no longer in this set: it is the enabled alliance control for the N-team
	# setup (asserted enabled just below).
	var disabled_reasons_ok := true
	for node_name in ["Hero0", "Hero1", "Handicap0", "Handicap1", "CustomHeroesToggle", "RingHeroesToggle", "ProfileButton"]:
		var control := menu.find_child(node_name, true, false) as Control
		if control == null or not control.get("disabled") or String(control.tooltip_text).strip_edges() == "":
			disabled_reasons_ok = false
	_check("unsupported_columns_disabled_with_reasons", disabled_reasons_ok)
	# The Team column is now an enabled alliance dropdown (rows sharing a number are
	# allied); default assigns each of the two rows its own number (free-for-all).
	var team0_opt := menu.find_child("Team0", true, false) as OptionButton
	var team1_opt := menu.find_child("Team1", true, false) as OptionButton
	_check(
		"team_alliance_dropdowns_enabled",
		team0_opt != null and team1_opt != null and not team0_opt.disabled and not team1_opt.disabled
			and team0_opt.item_count == 8 and int(team0_opt.get_item_metadata(team0_opt.selected)) != int(team1_opt.get_item_metadata(team1_opt.selected))
	)
	# The description panel stays honest: no converted description text exists.
	var description_label := menu.find_child("SetupDescription", true, false) as Label
	_check(
		"description_panel_fails_closed_without_lore",
		description_label != null and description_label.text.contains("No authored description"),
		description_label.text if description_label != null else "missing"
	)


	# Map availability is the slice's own resolution: registered maps enable,
	# unregistered ids disable with the honest reason, and the five-maps
	# catalog path is env-required and schema-validated.
	_check(
		"registered_maps_available",
		String(menu.retail_map_availability("bfme2.map.fords-of-isen-ii")) == ""
			and String(menu.retail_map_availability("bfme2.map.rivendell")) == ""
	)
	var unknown_map_note := String(menu.retail_map_availability("bfme2.map.notreal"))
	_check(
		"unregistered_map_disabled_with_reason",
		unknown_map_note != ""
			and (unknown_map_note.contains("not registered") or unknown_map_note.contains("OPENBFME_CONTENT") or unknown_map_note.contains("absent")),
		unknown_map_note
	)
	var fixture_root := "user://menu-map-fixture"
	var fixture_pack := fixture_root.path_join("bfme2-five-maps-106-private")
	DirAccess.make_dir_recursive_absolute(fixture_pack.path_join("data"))
	DirAccess.make_dir_recursive_absolute(fixture_pack.path_join("maps/fixturemap"))
	var catalog_file := FileAccess.open(fixture_pack.path_join("data/maps.json"), FileAccess.WRITE)
	catalog_file.store_string(JSON.stringify({
		"schema": "openbfme.map-catalog", "schemaVersion": 0,
		"maps": [{"id": "bfme2.map.fixturemap", "map": "maps/fixturemap/map.json", "preview": ""}],
	}))
	catalog_file.close()
	var saved_content_root := OS.get_environment("OPENBFME_CONTENT")
	OS.set_environment("OPENBFME_CONTENT", ProjectSettings.globalize_path(fixture_root))
	var map_doc_file := FileAccess.open(fixture_pack.path_join("maps/fixturemap/map.json"), FileAccess.WRITE)
	map_doc_file.store_string(JSON.stringify({"schema": "openbfme.map", "schemaVersion": 0, "id": "bfme2.map.fixturemap"}))
	map_doc_file.close()
	var catalog_valid_note := String(menu.retail_map_availability("bfme2.map.fixturemap"))
	map_doc_file = FileAccess.open(fixture_pack.path_join("maps/fixturemap/map.json"), FileAccess.WRITE)
	map_doc_file.store_string(JSON.stringify({"schema": "not-a-map", "schemaVersion": 0, "id": "bfme2.map.fixturemap"}))
	map_doc_file.close()
	var catalog_invalid_note := String(menu.retail_map_availability("bfme2.map.fixturemap"))
	OS.set_environment("OPENBFME_CONTENT", saved_content_root)
	_check("catalog_valid_map_enabled", catalog_valid_note == "", catalog_valid_note)
	_check("catalog_invalid_map_disabled_with_reason", catalog_invalid_note.contains("schema"), catalog_invalid_note)

	# Availability comes from the slice's own fail-closed signals: the men
	# pack gate for the default faction, RetailFactionManifest.from_registries
	# over the slice's fieldable-unit classification for every faction.
	var slice_probe = slice_script.new()
	var availability: Dictionary = menu.get_retail_faction_availability()
	_check("availability_covers_seven_factions", availability.size() == 7)
	var availability_matches_slice_signals := true
	for entry in expected:
		var faction_id := String(entry[0])
		if not availability.has(faction_id):
			availability_matches_slice_signals = false
			continue
		if faction_id == String(faction_manifest_script.DEFAULT_FACTION):
			continue
		slice_probe._classify_faction_units(faction_id)
		var expected_note := String(faction_manifest_script.from_registries(
			faction_id,
			slice_probe.get("fieldable_unit_runtimes"),
			content_db.call("get_playable_structure_runtimes")
		).get("_error", ""))
		if String(availability.get(faction_id, "")) != expected_note:
			availability_matches_slice_signals = false
	_check("availability_matches_slice_fail_closed_signals", availability_matches_slice_signals)
	_check("missing_content_signal_is_specific", String(faction_manifest_script.from_registries("mordor", {}, {}).get("_error", "")).contains("playableStructure"))

	# A faction with fieldable converted units shows available; a faction whose
	# documents are all unfieldable shows not-converted and cannot launch.
	_check("fieldable_faction_available", String(availability.get("elves", "missing")) == "", str(availability.get("elves", "missing")))
	content_db.playable_unit_runtimes["ZzztestSoldier"] = {"objectId": "ZzztestSoldier", "category": "infantry"}
	slice_probe._classify_faction_units("zztest")
	var zz_excluded := false
	for exclusion in slice_probe.get("unit_roster_exclusions") as Array:
		if String((exclusion as Dictionary).get("object_id", "")) == "ZzztestSoldier":
			zz_excluded = true
	_check("unfieldable_units_excluded_by_classification", zz_excluded)
	_check("unfieldable_faction_not_converted", String(menu._retail_faction_availability("zztest")) != "")
	content_db.playable_unit_runtimes.erase("ZzztestSoldier")

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
	# Faction packs convert continuously; men is the anchor today, others follow.
	_check("at_least_men_converted", converted_count >= 1, "converted=%d" % converted_count)

	# Men never depends on the elves supplement: with every elves-pack document
	# removed from the registries, Men still resolves and Elves is honestly
	# disabled with a reason.
	var unit_runtimes: Dictionary = content_db.get("playable_unit_runtimes") as Dictionary
	var structure_runtimes: Dictionary = content_db.get("playable_structure_runtimes") as Dictionary
	var saved_units := unit_runtimes.duplicate()
	var saved_structures := structure_runtimes.duplicate()
	for key in unit_runtimes.keys():
		if String((unit_runtimes[key] as Dictionary).get("_pack_root", "")).to_lower().contains("elves"):
			unit_runtimes.erase(key)
	for key in structure_runtimes.keys():
		if String((structure_runtimes[key] as Dictionary).get("_pack_root", "")).to_lower().contains("elves"):
			structure_runtimes.erase(key)
	slice_probe._classify_faction_units("men")
	var men_without_elves := String(faction_manifest_script.from_registries(
		"men", slice_probe.get("fieldable_unit_runtimes"), structure_runtimes
	).get("_error", ""))
	slice_probe._classify_faction_units("elves")
	var elves_without_elves := String(faction_manifest_script.from_registries(
		"elves", slice_probe.get("fieldable_unit_runtimes"), structure_runtimes
	).get("_error", ""))
	for key in saved_units:
		unit_runtimes[key] = saved_units[key]
	for key in saved_structures:
		structure_runtimes[key] = saved_structures[key]
	_check("men_boots_without_elves_supplement", men_without_elves == "", men_without_elves)
	_check("elves_disabled_with_reason_without_supplement", elves_without_elves != "")

	# Default selection is the converted faction on both sides and may launch.
	_check("default_selection_is_men_vs_men", String(player_opt.get_item_metadata(player_opt.selected)) == "men" and String(enemy_opt.get_item_metadata(enemy_opt.selected)) == "men")
	_check("launch_ready_by_default", String(menu.retail_launch_error()) == "" and not retail_btn.disabled, String(menu.retail_launch_error()))

	# Selection -> GameState handoff happens only through the validated path.
	game_state.set("retail_player_faction", "sentinel-player")
	game_state.set("retail_enemy_faction", "sentinel-enemy")
	game_state.set("retail_map_id", "sentinel-map")
	_check(
		"valid_selection_reaches_game_state",
		bool(menu.apply_skirmish_selection())
		and String(game_state.get("retail_player_faction")) == "men"
		and String(game_state.get("retail_enemy_faction")) == "men"
		and String(game_state.get("retail_map_id")).begins_with("bfme2.map."),
		"map=%s" % String(game_state.get("retail_map_id"))
	)

	# Solo page must fully own the surface: main bar hidden + non-interactive
	# so it cannot thrash under the flyout (user-reported "spazz" on Solo Play).
	var solo_btn := menu.get_node("Center/Solo") as Button
	var flyout := menu.get_node("Center/SoloFlyout") as Control
	menu.show_page("solo")
	await process_frame
	await process_frame
	_check("solo_page_hides_main_bar", solo_btn != null and not solo_btn.visible and solo_btn.mouse_filter == Control.MOUSE_FILTER_IGNORE)
	_check("solo_page_shows_flyout", flyout != null and flyout.visible)
	_check("solo_page_shows_launch", retail_btn.visible)
	# Rapid page flips must not leave both layers interactive.
	for _i in range(6):
		menu.show_page("main")
		menu.show_page("solo")
	await process_frame
	await process_frame
	_check(
		"solo_after_flip_storm_stable",
		not solo_btn.visible
		and solo_btn.mouse_filter == Control.MOUSE_FILTER_IGNORE
		and flyout.visible
		and retail_btn.visible
		and String(menu.get_current_page()) == "solo"
	)
	menu.show_page("main")
	await process_frame
	_check("main_page_restores_bar", solo_btn.visible and not flyout.visible)

	# STATS stub: six faction crest tiles, streak values recorded as untracked
	# ("—"), BACK returns to the setup page.
	_check("stats_page_accepts_navigation", bool(menu.show_page("stats")))
	var crest_row := menu.find_child("CrestRow", true, false)
	_check("stats_crest_row_has_six_factions", crest_row != null and crest_row.get_child_count() == 6)
	var streak_grid := menu.find_child("StreakGrid", true, false)
	var dash_cells := 0
	if streak_grid != null:
		for child in streak_grid.get_children():
			if (child as Label).text == "—":
				dash_cells += 1
	_check("stats_values_recorded_as_untracked", dash_cells >= 10 and menu.find_child("TrackedNote", true, false) != null, "dash=%d" % dash_cells)
	var stats_back := menu.find_child("StatsBack", true, false) as Button
	stats_back.emit_signal("pressed")
	_check("stats_back_returns_to_solo", String(menu.get_current_page()) == "solo" and flyout.visible)

	# Issue coverage: toggles, spacing, colors, starts, scroll, settings flow.
	_check("checkbutton_icons_present", menu.theme.has_icon("checked", "CheckButton") and menu.theme.has_icon("unchecked", "CheckButton") and menu.theme.get_icon("checked", "CheckButton") != menu.theme.get_icon("unchecked", "CheckButton"))
	var rows_panel := menu.find_child("PlayerRows", true, false) as Control
	var bottom_button := menu.find_child("MainMenuButton", true, false) as Button
	_check(
		"player_rows_clear_of_bottom_bar",
		rows_panel != null and bottom_button != null and (rows_panel.position.y + rows_panel.size.y) <= bottom_button.position.y - 8.0,
		"rows_bottom=%s bar_top=%s" % [rows_panel.position.y + rows_panel.size.y if rows_panel != null else -1.0, bottom_button.position.y if bottom_button != null else -1.0]
	)
	var map_scroll := menu.find_child("MapListScroll", true, false) as ScrollContainer
	_check("map_list_scrolls", map_scroll != null and map_scroll == (menu.find_child("MapRows", true, false) as Control).get_parent())
	var player_color_opt := menu.find_child("Color0", true, false) as OptionButton
	var enemy_color_opt := menu.find_child("Color1", true, false) as OptionButton
	_check("color_dropdowns_offer_eight_colors", player_color_opt != null and enemy_color_opt != null and player_color_opt.item_count == 8 and enemy_color_opt.item_count == 8)
	_check(
		"color_defaults_match_authored_teams",
		player_color_opt.selected == 0 and enemy_color_opt.selected == 1
			and (game_state.get("retail_player_color") as Color).is_equal_approx(Color8(45, 77, 172))
			and (game_state.get("retail_enemy_color") as Color).is_equal_approx(Color8(166, 32, 28))
	)
	_select_option_by_metadata_value(player_color_opt, Color8(46, 125, 50))
	_check(
		"color_choice_reaches_game_state_and_swatch",
		(game_state.get("retail_player_color") as Color).is_equal_approx(Color8(46, 125, 50))
			and (menu.find_child("ColorSwatch0", true, false) as ColorRect).color.is_equal_approx(Color8(46, 125, 50))
	)
	_select_option_by_metadata_value(player_color_opt, Color8(45, 77, 172))
	var start_buttons := menu.find_child("Start1", true, false) as Button
	_check("start_buttons_from_map_waypoints", start_buttons != null and (menu.find_child("Start2", true, false) as Button) != null)
	_check("start_default_is_authored", int(game_state.get("retail_player_start_index")) == 0)
	if start_buttons != null:
		start_buttons.emit_signal("pressed")
		_check("start_choice_reaches_game_state", int(game_state.get("retail_player_start_index")) == 1)
		start_buttons.emit_signal("pressed")
		_check("start_choice_toggles_back_to_authored", int(game_state.get("retail_player_start_index")) == 0)

	# Retail options screen: columns, pending edits, ACCEPT applies + persists,
	# CANCEL discards, RESET restores defaults.
	var options = menu.get_node("Center/OptionsScreen")
	_check("options_screen_present", options != null)
	var original_audio: Dictionary = SettingsScript.load_audio()
	var original_display: Dictionary = SettingsScript.load_display()
	var original_graphics: Dictionary = SettingsScript.load_graphics()
	var original_controls: Dictionary = SettingsScript.load_controls()
	_check("options_page_shows_retail_columns", bool(menu.show_page("options"))
		and options.find_child("DisplayColumn", true, false) != null
		and options.find_child("AudioColumn", true, false) != null
		and options.find_child("ControlsColumn", true, false) != null)
	var window_mode_opt := options.get("window_mode_opt") as OptionButton
	var resolution_opt := options.get("resolution_opt") as OptionButton
	var preset_opt := options.get("preset_opt") as OptionButton
	var music_slider := options.get("music_slider") as HSlider
	var sfx_slider := options.get("sfx_slider") as HSlider
	var scroll_slider := options.get("scroll_slider") as HSlider
	var health_toggle := options.get("health_toggle") as CheckButton
	_check("options_controls_present", window_mode_opt != null and resolution_opt != null and preset_opt != null and music_slider != null and sfx_slider != null and scroll_slider != null and health_toggle != null)
	var resolution_ids: Dictionary = {}
	var resolutions_valid := resolution_opt.item_count >= 5
	var last_area := 0
	for index in range(resolution_opt.item_count):
		var meta := String(resolution_opt.get_item_metadata(index))
		var parts := meta.split("x", false)
		if parts.size() != 2 or not parts[0].is_valid_int() or not parts[1].is_valid_int() or resolution_ids.has(meta):
			resolutions_valid = false
			continue
		resolution_ids[meta] = true
		var area := int(parts[0]) * int(parts[1])
		if int(parts[0]) < 1280 or int(parts[1]) < 720 or area <= last_area:
			resolutions_valid = false
		last_area = area
	_check("resolution_list_valid", resolutions_valid, "%d entries" % resolution_opt.item_count)
	for mode_index in range(window_mode_opt.item_count):
		window_mode_opt.select(mode_index)
		options.call("apply_and_persist")
		var stored_mode := String(SettingsScript.load_display()["window_mode"])
		if stored_mode != String(window_mode_opt.get_item_metadata(mode_index)):
			break
	_check("window_mode_persists_each_mode", String(SettingsScript.load_display()["window_mode"]) == String(window_mode_opt.get_item_metadata(window_mode_opt.selected)))
	preset_opt.select(0)
	options.call("apply_and_persist")
	_check("graphics_low_applies_msaa_off", root.msaa_3d == Viewport.MSAA_DISABLED and String(SettingsScript.load_graphics()["preset"]) == "low")
	preset_opt.select(3)
	options.call("apply_and_persist")
	_check("graphics_ultra_applies_msaa_4x", root.msaa_3d == Viewport.MSAA_4X and String(SettingsScript.load_graphics()["preset"]) == "ultra_high")
	music_slider.value = 0.33
	sfx_slider.value = 0.66
	scroll_slider.value = 1.5
	health_toggle.button_pressed = true
	options.call("apply_and_persist")
	var persisted_audio: Dictionary = SettingsScript.load_audio()
	var persisted_controls: Dictionary = SettingsScript.load_controls()
	_check(
		"sliders_persist_on_accept",
		is_equal_approx(float(persisted_audio["music_volume"]), 0.33)
			and is_equal_approx(float(persisted_audio["voice_sfx_volume"]), 0.66)
			and is_equal_approx(float(persisted_controls["scroll_speed"]), 1.5)
			and bool(persisted_controls["show_all_health_bars"]),
		"audio=%s controls=%s" % [persisted_audio, persisted_controls]
	)
	options.call("reset_to_defaults")
	var defaults_audio: Dictionary = SettingsScript.load_audio()
	var defaults_display: Dictionary = SettingsScript.load_display()
	var defaults_controls: Dictionary = SettingsScript.load_controls()
	_check(
		"reset_to_defaults_restores_store_and_controls",
		is_equal_approx(float(defaults_audio["music_volume"]), 0.80)
			and is_equal_approx(float(defaults_audio["voice_sfx_volume"]), 0.85)
			and not bool(defaults_audio["muted"])
			and String(defaults_display["window_mode"]) == "windowed"
			and String(defaults_display["resolution"]) == "1920x1080"
			and is_equal_approx(float(defaults_controls["scroll_speed"]), 1.0)
			and not bool(defaults_controls["show_all_health_bars"])
			and is_equal_approx(float(music_slider.value), 0.80)
			and is_equal_approx(float(scroll_slider.value), 1.0)
	)
	options.call("open")
	music_slider.value = 0.11
	options.call("cancel")
	_check("options_cancel_discards_pending_edits", not is_equal_approx(float(SettingsScript.load_audio()["music_volume"]), 0.11))
	SettingsScript.save_audio(float(original_audio["music_volume"]), float(original_audio["voice_sfx_volume"]), bool(original_audio["muted"]))
	SettingsScript.save_display(String(original_display["window_mode"]), String(original_display["resolution"]))
	SettingsScript.save_graphics(String(original_graphics["preset"]))
	SettingsScript.save_controls(float(original_controls["scroll_speed"]), bool(original_controls["show_all_health_bars"]))
	options.call("apply_stored_settings")
	menu.show_page("main")
	await process_frame

	# Retail button treatment: procedurally generated pointed-cap nine-slice
	# textures on every button family, a distinct brighter hover with dark
	# text, and the solo flyout's neon green frame (REF-01/02/05/06).
	var nav_normal: StyleBox = menu.theme.get_stylebox("normal", "NavButton")
	_check(
		"button_style_uses_cap_textures",
		nav_normal is StyleBoxTexture
			and (nav_normal as StyleBoxTexture).texture != null
			and (nav_normal as StyleBoxTexture).texture_margin_left >= 18.0
			and (nav_normal as StyleBoxTexture).texture_margin_right >= 18.0
	)
	var nav_hover: StyleBox = menu.theme.get_stylebox("hover", "NavButton")
	_check(
		"button_hover_state_present_and_distinct",
		nav_hover is StyleBoxTexture
			and (nav_hover as StyleBoxTexture).texture != null
			and (nav_hover as StyleBoxTexture).texture != (nav_normal as StyleBoxTexture).texture
			and menu.theme.get_color("font_hover_color", "NavButton").get_luminance() < 0.4
	)
	var nav_pressed: StyleBox = menu.theme.get_stylebox("pressed", "NavButton")
	var nav_disabled: StyleBox = menu.theme.get_stylebox("disabled", "NavButton")
	_check(
		"button_pressed_disabled_states_present",
		nav_pressed is StyleBoxTexture
			and (nav_pressed as StyleBoxTexture).texture != null
			and nav_disabled is StyleBoxTexture
			and (nav_disabled as StyleBoxTexture).texture != null
	)
	var flyout_box := menu.theme.get_stylebox("panel", "FlyoutPanel") as StyleBoxFlat
	var flyout_border: Color = flyout_box.border_color if flyout_box != null else Color.BLACK
	_check(
		"flyout_panel_has_neon_green_border",
		flyout_box != null
			and flyout_border.g > flyout_border.r + 0.2
			and flyout_border.g > flyout_border.b + 0.2
			and flyout_box.get_border_width_min() == 2
			and (flyout as Panel).theme_type_variation == "FlyoutPanel"
	)
	var accept_button := options.find_child("AcceptButton", true, false) as Button
	var primary_box: StyleBox = menu.theme.get_stylebox("normal", "PrimaryButton")
	_check(
		"options_accept_button_cap_styled",
		accept_button != null
			and accept_button.theme_type_variation == "PrimaryButton"
			and primary_box is StyleBoxTexture
			and (primary_box as StyleBoxTexture).texture != null
	)

	# A faction whose pack content is missing can never be handed off, even if
	# its disabled dropdown entry is forced programmatically. When every
	# faction is eventually converted there is no disabled entry to force; the
	# fail-closed path is exercised as long as one exists.
	var disabled_index := -1
	for index in range(enemy_opt.item_count):
		if enemy_opt.is_item_disabled(index):
			disabled_index = index
			break
	_check("unconverted_option_exists", disabled_index >= 0 or converted_count == 7)
	if disabled_index >= 0:
		game_state.set("retail_player_faction", "sentinel-player")
		game_state.set("retail_enemy_faction", "sentinel-enemy")
		enemy_opt.select(disabled_index)
		var blocked_launch := not bool(menu.apply_skirmish_selection())
		_check("unconverted_enemy_launch_blocked", blocked_launch and String(menu.retail_launch_error()) != "")
		_check("blocked_launch_leaves_game_state_untouched", String(game_state.get("retail_player_faction")) == "sentinel-player" and String(game_state.get("retail_enemy_faction")) == "sentinel-enemy")
		_check("blocked_launch_disables_button", retail_btn.disabled)
		var hint := menu.find_child("RetailHint", true, false) as Label
		_check("blocked_launch_explains_itself", hint != null and hint.text.contains("not converted"), hint.text if hint != null else "missing")

	# Restoring the converted selection re-arms the launch.
	for index in range(enemy_opt.item_count):
		if not enemy_opt.is_item_disabled(index):
			enemy_opt.select(index)
			break
	menu.apply_skirmish_selection()
	_check("reselection_recovers_launch", String(menu.retail_launch_error()) == "" and String(game_state.get("retail_enemy_faction")) == "men")

	# The legacy prototype skirmish entry point is gone for good.
	_check("legacy_grid_removed", menu.get_node_or_null("Center/LegacyGrid") == null and menu.get_node_or_null("Center/Start") == null)

	game_state.set("retail_player_faction", "men")
	game_state.set("retail_enemy_faction", "men")
	slice_probe.free()
	menu.queue_free()
	await process_frame
	_finish()


func _select_option_by_metadata_value(option: OptionButton, metadata_value: Variant) -> void:
	for index in range(option.item_count):
		if option.get_item_metadata(index) == metadata_value:
			option.select(index)
			option.item_selected.emit(index)
			return


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
