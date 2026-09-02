extends SceneTree
## Headless retail setup -> native match-launch -> loading transition proof.

const SetupScript := preload("res://src/ui/skirmish_setup.gd")
const NativeMatchLaunchScript := preload("res://src/present/native_match_launch.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var setup = SetupScript.new()
	setup.name = "NativeShellSetupProbe"
	setup.size = Vector2(1920, 1080)
	root.add_child(setup)
	await process_frame
	_populate_setup(setup)
	_check("native_choice_available", setup.native_core_available())
	setup.engine_opt.select(1)
	_check("native_choice_selected", setup.native_core_selected())
	var game_state := root.get_node_or_null("GameState")
	_check("game_state_available", game_state != null)
	if game_state != null:
		game_state.set("retail_player_start_index", 2)

	var retail_map_id := "bfme2.map.fords-of-isen-ii"
	var document: Dictionary = setup.build_native_match_launch(retail_map_id, "Fords of Isen II")
	var expected_map := NativeMatchLaunchScript.choose_map(
		NativeMatchLaunchScript.maps(), retail_map_id, "Fords of Isen II"
	)
	var expected_map_artifact := String(expected_map.get("path", ""))
	if not FileAccess.file_exists(expected_map_artifact):
		expected_map_artifact = String(NativeMatchLaunchScript.native_context().get("content_root", "")).path_join(expected_map_artifact)
	var expected_map_document := JSON.parse_string(FileAccess.get_file_as_string(expected_map_artifact)) as Dictionary
	var expected_map_source := String((expected_map_document.get("source", {}) as Dictionary).get("path", ""))
	_check("launch_schema", String(document.get("schema", "")) == "openbfme.match-launch.v1")
	_check("launch_validates", NativeMatchLaunchScript.validate(document))
	_check("map_from_native_index", String((document.get("map", {}) as Dictionary).get("path", "")) == expected_map_source and not expected_map_source.is_empty())
	var bundle_path := String(NativeMatchLaunchScript.native_context().get("bundle_path", ""))
	_check("pack_digest_matches_resolved_bundle", String((document.get("pack", {}) as Dictionary).get("sha256", "")) == FileAccess.get_sha256(bundle_path).to_lower())
	var players := document.get("players", []) as Array
	_check("two_setup_players", players.size() == 2)
	if players.size() == 2:
		_check("human_faction_and_seat", int((players[0] as Dictionary).get("seat", -1)) == 0 and String((players[0] as Dictionary).get("faction", "")) == "FactionMen" and String((players[0] as Dictionary).get("controller", "")) == "human")
		_check("ai_difficulty_and_faction", String((players[1] as Dictionary).get("faction", "")) == "FactionMordor" and String((players[1] as Dictionary).get("controller", "")) == "ai" and String((players[1] as Dictionary).get("ai_difficulty", "")) == "hard")
		_check("colors_and_authored_starts", int((players[0] as Dictionary).get("color", -1)) == 2 and int((players[1] as Dictionary).get("color", -1)) == 4 and int((players[0] as Dictionary).get("start_position", -1)) == 1 and int((players[1] as Dictionary).get("start_position", -1)) == 0)
	current_scene = setup
	setup.play_btn.emit_signal("pressed")
	await process_frame
	await process_frame
	var boot = current_scene
	_check("setup_entered_shared_retail_loading_boot", boot != null and boot.scene_file_path == "res://scenes/retail_loading_boot.tscn")
	_check("shared_boot_targets_native_match", boot != null and String(boot.get("_match_scene_path")) == "res://scenes/sim_host_match.tscn")
	_check("launch_reached_game_state", game_state != null and (game_state.get_meta("native_match_launch", {}) as Dictionary) == document)
	_check("retail_loading_screen_created", boot.get("_screen") != null)
	for _frame in 600:
		if current_scene != boot:
			break
		await process_frame
	var transitioned = current_scene
	_check("transitioned_to_native_match", transitioned != null and transitioned.scene_file_path == "res://scenes/sim_host_match.tscn")
	_check("launch_survives_transition", game_state != null and ((game_state.get_meta("native_match_launch", {}) as Dictionary) == document or transitioned.active_match_launch() == document))
	for _frame in 1800:
		if transitioned.is_running() or transitioned.startup_failed():
			break
		await process_frame
	_check("transitioned_match_running", transitioned.is_running())
	var consumed: Dictionary = transitioned.active_match_launch()
	var consumed_players := consumed.get("players", []) as Array
	_check("runtime_preserves_authored_start", consumed_players.size() == 2 and int((consumed_players[0] as Dictionary).get("start_position", -1)) == 1)
	_check("runtime_preserves_ai_difficulty", consumed_players.size() == 2 and String((consumed_players[1] as Dictionary).get("ai_difficulty", "")) == "hard")
	if transitioned != null:
		transitioned.shutdown()
		root.remove_child(transitioned)
		transitioned.free()
	if game_state != null:
		game_state.set("retail_player_start_index", 0)
	for child in root.get_children():
		if child is CanvasLayer and child.is_in_group("retail_loading_screen"):
			root.remove_child(child)
			child.free()
	await process_frame
	_finish()


func _populate_setup(setup) -> void:
	var map_button := Button.new()
	map_button.text = "Fords of Isen II"
	map_button.toggle_mode = true
	setup.map_rows_host.add_child(map_button)
	setup.map_rows.append({
		"button": map_button,
		"map_id": "bfme2.map.fords-of-isen-ii",
		"players": 2,
	})
	setup.set_selected_map_row(0)
	for row in 2:
		var army := setup.row_army_opts[row] as OptionButton
		army.add_item("Men" if row == 0 else "Mordor")
		army.set_item_metadata(0, "men" if row == 0 else "mordor")
		army.select(0)
		var difficulty := setup.row_difficulty_opts[row] as OptionButton
		difficulty.add_item("Hard")
		difficulty.set_item_metadata(0, "hard")
		difficulty.select(0)
		var team := setup.team_dropdowns[row] as OptionButton
		team.add_item("Team %d" % (row + 1))
		team.set_item_metadata(0, row)
		team.select(0)
		var color := setup.color_dropdowns[row] as OptionButton
		for index in 6:
			color.add_item("Color %d" % index)
		color.select(2 if row == 0 else 4)
	setup.initial_resources_opt.add_item("1200")
	setup.initial_resources_opt.set_item_metadata(0, 1200)
	setup.initial_resources_opt.select(0)
	setup.cp_factor_opt.add_item("1X")
	setup.cp_factor_opt.set_item_metadata(0, 1.0)
	setup.cp_factor_opt.select(0)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_SHELL FAIL %s" % name)


func _finish() -> void:
	print("NATIVE_SHELL_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
