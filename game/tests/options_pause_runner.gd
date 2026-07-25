extends SceneTree
## Headless gate for the retail pause menu's SETTINGS entry: the slice's pause
## panel must lead to the shared options screen, and edits accepted there must
## reach the live slice (camera scroll scale, audio) and the settings store.

const SettingsScript = preload("res://src/ui/user_settings.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "OPTIONS_PAUSE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var original_audio: Dictionary = SettingsScript.load_audio()
	var original_controls: Dictionary = SettingsScript.load_controls()
	var original_resources := int((root.get_node_or_null("GameState") as Node).get("retail_initial_resources"))
	var original_factor := float((root.get_node_or_null("GameState") as Node).get("retail_command_point_factor"))
	# The menu's RULES tab must reach the simulation: seed the setup values
	# before the slice builds its gameplay rules.
	(root.get_node_or_null("GameState") as Node).set("retail_initial_resources", 2000)
	(root.get_node_or_null("GameState") as Node).set("retail_command_point_factor", 2.0)
	# Setup surface values that must also reach the live match: start spot,
	# house colors, and the stored graphics preset applied at slice boot.
	var original_start := int((root.get_node_or_null("GameState") as Node).get("retail_player_start_index"))
	var original_player_color: Color = (root.get_node_or_null("GameState") as Node).get("retail_player_color")
	var original_graphics: Dictionary = SettingsScript.load_graphics()
	(root.get_node_or_null("GameState") as Node).set("retail_player_start_index", 1)
	(root.get_node_or_null("GameState") as Node).set("retail_player_color", Color8(46, 125, 50))
	SettingsScript.save_graphics("low")
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("slice_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	for index in 600:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.queue_free()
		await process_frame
		_finish()
		return
	_check(
		"rules_reach_slice_simulation",
		int(slice.simulation.team_resources[0]) == 2000
			and slice.simulation.command_point_cap == 400,
		"resources=%d cap=%d" % [int(slice.simulation.team_resources[0]), slice.simulation.command_point_cap]
	)
	(root.get_node_or_null("GameState") as Node).set("retail_initial_resources", original_resources)
	(root.get_node_or_null("GameState") as Node).set("retail_command_point_factor", original_factor)

	# Slice boot applies the stored graphics preset (Low → MSAA disabled).
	_check("slice_boot_applies_graphics_preset", root.msaa_3d == Viewport.MSAA_DISABLED, "msaa=%d" % root.msaa_3d)
	SettingsScript.save_graphics(String(original_graphics["preset"]))
	(load("res://src/ui/options_screen.gd") as GDScript).apply_graphics_preset(String(original_graphics["preset"]), root)

	# Start-spot choice seeds the player side at Player_1_Start (the authored
	# default takes Player_2_Start); the AI keeps the other start.
	var waypoints_doc := {}
	var source_path := String(slice._loaded_map_definition.get("_source", ""))
	if source_path == "":
		var map_relative := String(slice._loaded_map_definition.get("map", ""))
		if map_relative != "":
			source_path = String(slice.map_pack_root).path_join(map_relative)
	if source_path != "":
		var wp_text := FileAccess.get_file_as_string(source_path.get_base_dir().path_join("waypoints.json"))
		var parsed: Variant = JSON.parse_string(wp_text)
		if typeof(parsed) == TYPE_DICTIONARY:
			waypoints_doc = parsed
	var start_one := Vector3.INF
	if not waypoints_doc.is_empty():
		var row := ((waypoints_doc.get("playerStarts", {}) as Dictionary).get("Player_1_Start", {}) as Dictionary)
		var pos: Variant = row.get("godotPosition", null)
		if typeof(pos) == TYPE_ARRAY and (pos as Array).size() >= 3:
			start_one = Vector3(float(pos[0]), float(pos[1]), float(pos[2]))
	var expected_local := Vector2.INF
	if start_one != Vector3.INF and slice.source_map_data != null:
		var local: Vector3 = slice.source_map_data.source_to_local(start_one)
		expected_local = Vector2(local.x, local.z)
	var player_fortress_id := int(slice.simulation.fortress_id(0))
	var fortress_position := Vector2(slice.simulation.structure(player_fortress_id).get("position", Vector2.INF)) if player_fortress_id != 0 else Vector2.INF
	_check(
		"start_choice_seeds_player_start",
		expected_local != Vector2.INF and fortress_position != Vector2.INF and fortress_position.distance_to(expected_local) < 35.0,
		"fortress=%s expected≈%s" % [fortress_position, expected_local]
	)
	(root.get_node_or_null("GameState") as Node).set("retail_player_start_index", original_start)

	# House-color override reaches the retail mask-recolor application.
	var house_color: Dictionary = (load("res://src/retail_slice/retail_house_color.gd") as GDScript).team_color_overrides
	_check(
		"house_color_override_reaches_recolor",
		house_color.has(0) and (house_color[0] as Color).is_equal_approx(Color8(46, 125, 50)),
		str(house_color.get(0, "missing"))
	)
	(root.get_node_or_null("GameState") as Node).set("retail_player_color", original_player_color)

	var pause_panel: PanelContainer = slice.hud.pause_panel
	var settings_button := pause_panel.find_child("PauseSettingsButton", true, false) as Button
	_check(
		"pause_menu_includes_settings",
		settings_button != null and settings_button.tooltip_text.to_lower().contains("settings")
	)
	if settings_button == null:
		slice.queue_free()
		await process_frame
		_finish()
		return

	slice.toggle_escape_menu()
	await process_frame
	_check("pause_panel_shows_when_paused", bool(slice.simulation_paused) and pause_panel.visible)
	settings_button.emit_signal("pressed")
	await process_frame
	await process_frame
	var overlay = slice.get("options_overlay")
	_check(
		"settings_opens_options_overlay",
		overlay != null and overlay.visible and not pause_panel.visible
	)
	_check("overlay_has_scroll_speed_slider", overlay.get("scroll_slider") != null and overlay.get("music_slider") != null)
	overlay.get("scroll_slider").value = 1.75
	overlay.get("music_slider").value = 0.5
	overlay.call("accept")
	await process_frame
	_check(
		"accept_applies_scroll_speed_live",
		is_equal_approx(float(slice.get("keyboard_scroll_speed_scale")), 1.75),
		"scale=%s" % str(slice.get("keyboard_scroll_speed_scale"))
	)
	_check(
		"accept_persists_controls_and_audio",
		is_equal_approx(float(SettingsScript.load_controls()["scroll_speed"]), 1.75)
			and is_equal_approx(float(SettingsScript.load_audio()["music_volume"]), 0.5)
	)
	_check(
		"accept_applies_audio_live",
		is_equal_approx(slice.audio_system.get_music_volume(), 0.5),
		"music=%s" % slice.audio_system.get_music_volume()
	)
	_check("overlay_closes_back_to_pause", not overlay.visible and pause_panel.visible)

	overlay.call("open")
	overlay.call("reset_to_defaults")
	await process_frame
	_check(
		"reset_restores_scroll_default",
		is_equal_approx(float(slice.get("keyboard_scroll_speed_scale")), 1.0)
			and is_equal_approx(float(SettingsScript.load_controls()["scroll_speed"]), 1.0)
	)
	slice.toggle_escape_menu()
	await process_frame

	SettingsScript.save_audio(float(original_audio["music_volume"]), float(original_audio["voice_sfx_volume"]), bool(original_audio["muted"]))
	SettingsScript.save_controls(float(original_controls["scroll_speed"]), bool(original_controls["show_all_health_bars"]))
	slice.queue_free()
	await process_frame
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("OPTIONS_PAUSE PASS %s" % name)
	else:
		failed += 1
		printerr("OPTIONS_PAUSE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("OPTIONS_PAUSE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
