extends SceneTree
## Focused private Men production-command gate. Retail payloads stay mounted
## under the external pack; this runner only inspects their registered sources.

var HudScript: Script
const SOLDIER_ID := "bfme2.object.gondor-fighter-horde"
const TOWER_GUARD_ID := "bfme2.object.gondor-tower-guard"
const ARCHER_ID := "bfme2.object.gondor-archer"
const KNIGHT_ID := "bfme2.object.gondor-knight"

var passed := 0
var failed := 0
var _fixture_root := ""


class CompleteContentStub:
	extends RefCounted

	var pack_root := ""
	var images: Dictionary = {}
	var strings: Dictionary = {}


	func _init(root_path: String, image_rows: Dictionary, string_rows: Dictionary) -> void:
		pack_root = root_path
		images = image_rows
		strings = string_rows


	func get_retail_ui_image(image_id: String) -> Dictionary:
		var row: Dictionary = images.get(image_id, {})
		if row.is_empty():
			return {}
		var result := row.duplicate(true)
		result["id"] = image_id
		result["_pack_root"] = pack_root
		return result


	func get_retail_string(string_id: String, fallback: String = "") -> String:
		return String(strings.get(string_id, fallback))


	func resolve_retail_ui_image_path(image_id: String) -> String:
		var row: Dictionary = images.get(image_id, {})
		return pack_root.path_join(String(row.get("path", ""))) if not row.is_empty() else ""


	func is_resolved_asset_path(path: String) -> bool:
		return path.begins_with(pack_root.path_join("assets"))


class CorruptContentStub:
	extends RefCounted

	var pack_root := ""


	func _init(root_path: String) -> void:
		pack_root = root_path


	func get_retail_ui_image(image_id: String) -> Dictionary:
		if image_id != "BGBarracks_Soldiers":
			return {}
		return {
			"id": image_id,
			"path": "corrupt.png",
			"width": 64,
			"height": 64,
			"_pack_root": pack_root,
		}


	func get_retail_string(string_id: String, fallback: String = "") -> String:
		if string_id == "CONTROLBAR:ConstructGondorFighterHorde":
			return "Fixture Soldiers"
		if string_id == "CONTROLBAR:ToolTipBuildGondorFighterHorde":
			return "Fixture tooltip"
		return fallback


	func resolve_retail_ui_image_path(image_id: String) -> String:
		return pack_root.path_join("corrupt.png") if image_id == "BGBarracks_Soldiers" else ""


	func is_resolved_asset_path(path: String) -> bool:
		return path == pack_root.path_join("corrupt.png")


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_FOUR_UNIT_HUD_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	_check("content_db_available", content_db != null)
	if content_db == null:
		_finish()
		return
	# Load the HUD after autoload startup so AssetFactory cannot form a compile-
	# time ContentDB cycle when this focused runner is the main script.
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	_check("hud_script_available", HudScript != null)
	if HudScript == null:
		_finish()
		return
	var soldier_definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var selected_pack_root := String(soldier_definition.get("_pack_root", ""))
	# The host pack must be an external converted pack that provides Men; it is
	# NOT required to be the single-faction `bfme2-men-vslice`. A composed pack
	# is id'd `bfme2-<a>-<b>-…-vslice` and lists its factions in pack.json
	# `factionImportCoverage`.
	var host_mod_loader = root.get_node("/root/ModLoader")
	_check(
		"private_men_pack_selected",
		selected_pack_root != ""
			and bool(host_mod_loader.call("pack_is_retail_import", selected_pack_root))
			and bool(host_mod_loader.call("pack_provides_faction", selected_pack_root, "men")),
		selected_pack_root
	)
	if selected_pack_root == "":
		_finish()
		return

	var selected_hud = HudScript.new()
	root.add_child(selected_hud)
	selected_hud.build()
	var selected_bind_error: String = selected_hud.bind_retail_train_commands(content_db, selected_pack_root, true)
	if selected_bind_error == "":
		_check("selected_pack_complete_retail_hud", selected_hud.retail_presentation_bound)
		_check_complete_binding("selected_pack", selected_hud, content_db, selected_pack_root)
		_check("selected_pack_retail_parchment_radar", selected_hud.minimap.private_parity_mode and selected_hud.minimap.retail_parchment != null)
		_check("selected_pack_three_retail_orb_buttons", selected_hud.orb_buttons.size() == 3 and (selected_hud.orb_buttons["options"] as Button).icon != null and (selected_hud.orb_buttons["powers"] as Button).icon != null and (selected_hud.orb_buttons["score"] as Button).icon != null)
		_check("selected_pack_six_retail_empty_sockets", selected_hud.command_grid.find_children("RetailEmptySocket*", "TextureRect", false, false).size() == 6)
		(selected_hud.orb_buttons["powers"] as Button).pressed.emit()
		_check("selected_pack_powers_palette_toggles", selected_hud.powers_palette.visible and selected_hud.power_buttons.size() == 12)
		(selected_hud.orb_buttons["powers"] as Button).pressed.emit()
		(selected_hud.orb_buttons["score"] as Button).pressed.emit()
		_check("selected_pack_score_overlay_toggles", selected_hud.score_overlay.visible and not selected_hud.powers_palette.visible)
		selected_hud.set_unit_selection_state([77] as Array[int], {77: {"object_id": "bfme2.object.men-porter", "is_builder": true}})
		var visible_builder_commands := 0
		for action_id in selected_hud.unit_action_buttons:
			if String(action_id).begins_with("construct_") and (selected_hud.unit_action_buttons[action_id] as Button).visible:
				visible_builder_commands += 1
		# Retail (REF-32): the porter's palantir sockets carry only his orders;
		# every construct command lives on the right-edge side command bar.
		_check("selected_pack_builder_constructs_live_on_side_bar", visible_builder_commands == 0, str(visible_builder_commands))
	else:
		# Treat any selected-pack UI closure gap as an explicit blocker, never as
		# permission to paint the fake shell.
		print("RETAIL_FOUR_UNIT_HUD SELECTED_PACK_BLOCKER %s" % selected_bind_error)
		_check(
			"selected_pack_blocker_is_explicit",
			selected_bind_error.contains("SGCommandBar") or selected_bind_error.contains("HUD APT"),
			selected_bind_error
		)
		_check("selected_pack_blocker_fails_closed", _all_commands_failed_closed(selected_hud))
		_check(
			"selected_pack_blocker_hides_synthetic_shell",
			not selected_hud.synthetic_palantir_frame.uses_synthetic_shell()
				and not selected_hud.retail_control_bar_frame.has_retail_shell()
				and not selected_hud.retail_apt_runtime.visible
		)
	selected_hud.free()

	_fixture_root = "user://retail-four-unit-hud-%d" % Time.get_ticks_usec()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_fixture_root.path_join("assets")))
	var pack_file := FileAccess.open(_fixture_root.path_join("pack.json"), FileAccess.WRITE)
	_check("fixture_pack_document_created", pack_file != null)
	if pack_file != null:
		pack_file.store_string(JSON.stringify({"files": {}}, "  ") + "\n")
		pack_file.close()
	var fixture_rows := _write_complete_fixture_images()
	var fixture_strings := _fixture_strings()
	var fixture_content = CompleteContentStub.new(_fixture_root, fixture_rows, fixture_strings)
	var hud = HudScript.new()
	root.add_child(hud)
	hud.build()
	hud.set_resources(1200, 60, 200)
	_check(
		"retail_live_text_values_forward_exactly",
		String(hud.retail_apt_runtime.live_text_value("$PalantirResources")) == "1200"
			and String(hud.retail_apt_runtime.live_text_value("$PalantirResourceMultiplier")) == " "
			and String(hud.retail_apt_runtime.live_text_value("$PalantirCommandPoints")) == "60/200"
	)
	var bind_error: String = hud.bind_retail_train_commands(fixture_content, _fixture_root, true)
	_check("complete_fixture_binding_succeeds", bind_error == "", bind_error)
	_check_complete_binding("complete_fixture", hud, fixture_content, _fixture_root)
	var public_hud = HudScript.new()
	root.add_child(public_hud)
	public_hud.build()
	var public_error: String = public_hud.bind_retail_train_commands(fixture_content, "", false)
	_check("public_fallback_binding_succeeds", public_error == "", public_error)
	_check(
		"public_fallback_retains_synthetic_shell",
		public_hud.synthetic_palantir_frame.uses_synthetic_shell()
			and public_hud.get_node("CommandPanel").get_theme_stylebox("panel") is StyleBoxFlat
	)
	public_hud.free()
	_check("four_command_binding_atomic_flag", hud.retail_train_commands_bound and hud.retail_train_command_bound)
	_check("legacy_train_button_aliases_soldier", hud.train_button == hud.train_buttons.get(SOLDIER_ID))
	_check("exactly_four_deterministic_buttons", hud.train_buttons.size() == 4)
	# attack_move, stop, stance, formation, five construct kinds.
	_check("nine_source_unit_action_buttons", hud.unit_action_buttons.size() == 9)
	hud.set_production_state([], false)
	var selected_member_ids: Array[int] = [7]
	hud.set_unit_selection_state(selected_member_ids, {7: {"object_id": "bfme2.object.gondor-fighter"}})
	_check(
		"selected_unit_shows_retail_portrait_and_actions",
		hud.selection_portrait.visible
			and String(hud.selection_portrait.get_meta("retail_active_portrait_unit_id", "")) == SOLDIER_ID
			and (hud.unit_action_buttons["attack_move"] as Button).visible
			and not (hud.unit_action_buttons["attack_move"] as Button).disabled
			and (hud.unit_action_buttons["stop"] as Button).visible
	)
	var action_events: Array[String] = []
	hud.attack_move_requested.connect(func() -> void: action_events.append("attack_move"))
	hud.stop_requested.connect(func() -> void: action_events.append("stop"))
	(hud.unit_action_buttons["attack_move"] as Button).pressed.emit()
	(hud.unit_action_buttons["stop"] as Button).pressed.emit()
	_check("unit_action_buttons_emit_orders", action_events == ["attack_move", "stop"], str(action_events))

	_check_state("barracks_commands", hud, [SOLDIER_ID, TOWER_GUARD_ID], true)
	_check_state("archery_range_commands", hud, [ARCHER_ID], true)
	_check_state("stable_commands", hud, [KNIGHT_ID], true)
	_check_state("damaged_producer_disables_command", hud, [KNIGHT_ID], false)
	_check_state("no_selected_producer_hides_commands", hud, [], false)

	var emitted: Array[String] = []
	hud.train_requested.connect(func(unit_id: String) -> void: emitted.append(unit_id))
	for spec_value in HudScript.RETAIL_COMMAND_SPECS:
		var unit_id := String((spec_value as Dictionary)["unit_id"])
		hud.set_production_state([unit_id], true)
		(hud.train_buttons[unit_id] as Button).pressed.emit()
	_check("buttons_emit_exact_unit_ids", emitted == [SOLDIER_ID, TOWER_GUARD_ID, ARCHER_ID, KNIGHT_ID], str(emitted))

	# Remove the final manifest entry after the first three validate. The bind is
	# still atomic: no earlier texture or string may leak onto the failed HUD.
	var saved_knight_image: Dictionary = fixture_rows[String(HudScript.RETAIL_COMMAND_SPECS[3]["image_id"])]
	fixture_rows.erase(String(HudScript.RETAIL_COMMAND_SPECS[3]["image_id"]))
	var missing_hud = HudScript.new()
	root.add_child(missing_hud)
	missing_hud.build()
	var missing_error: String = missing_hud.bind_retail_train_commands(fixture_content, _fixture_root, true)
	fixture_rows[String(HudScript.RETAIL_COMMAND_SPECS[3]["image_id"])] = saved_knight_image
	_check("missing_fourth_art_rejected", missing_error.contains("BGStables_Knights"), missing_error)
	_check("missing_art_keeps_surface_unbound", not missing_hud.retail_train_commands_bound and not missing_hud.retail_train_command_bound)
	_check("missing_art_is_atomic_and_hidden", _all_commands_failed_closed(missing_hud))
	missing_hud.free()

	# Invalid signature is rejected before invoking Godot's PNG decoder, keeping
	# the expected failure free of engine ERROR/WARNING diagnostics.
	var corrupt_file := FileAccess.open(_fixture_root.path_join("corrupt.png"), FileAccess.WRITE)
	if corrupt_file != null:
		corrupt_file.store_string("not a png\n")
		corrupt_file.close()
	var corrupt_hud = HudScript.new()
	root.add_child(corrupt_hud)
	corrupt_hud.build()
	var corrupt_error: String = corrupt_hud.bind_retail_train_commands(CorruptContentStub.new(_fixture_root), _fixture_root, true)
	_check("corrupt_png_rejected_before_decode", corrupt_error.contains("invalid signature"), corrupt_error)
	_check("corrupt_art_keeps_surface_unbound", not corrupt_hud.retail_train_commands_bound and not corrupt_hud.retail_train_command_bound)
	_check("corrupt_art_is_atomic_and_hidden", _all_commands_failed_closed(corrupt_hud))
	corrupt_hud.free()

	_check_retail_tooltips(hud, fixture_content)
	_check_side_command_bar(hud)

	hud.free()
	_cleanup_fixture()
	await process_frame
	_finish()


func _check_retail_tooltips(hud, content) -> void:
	# Tooltip title/description must come from the pack strings the button was
	# bound with, and the cost row must reflect the HUD's supplied cost source.
	var soldier_spec: Dictionary = HudScript.RETAIL_COMMAND_SPECS[0]
	var label_id := String(soldier_spec["label_id"])
	var tooltip_id := String(soldier_spec["tooltip_id"])
	var expected_title := String(content.get_retail_string(label_id, ""))
	var expected_desc := String(content.get_retail_string(tooltip_id, ""))
	hud.set_command_costs({SOLDIER_ID: 200, "farm": 300})
	var button: Button = hud.train_buttons[SOLDIER_ID]
	hud.show_retail_tooltip(button)
	_check(
		"tooltip_shows_pack_string_title",
		hud.retail_tooltip.visible and hud.retail_tooltip.title_text() == expected_title,
		"%s|%s" % [expected_title, hud.retail_tooltip.title_text()]
	)
	_check(
		"tooltip_description_from_pack_string",
		hud.retail_tooltip.description_text() == expected_desc,
		hud.retail_tooltip.description_text()
	)
	_check(
		"tooltip_cost_row_matches_supplied_cost",
		hud.retail_tooltip.cost_row_visible() and hud.retail_tooltip.cost_text() == "Cost: 200",
		hud.retail_tooltip.cost_text()
	)
	# A button without a supplied cost shows title + description only.
	var stance_button: Button = hud.unit_action_buttons["stance"]
	hud.show_retail_tooltip(stance_button)
	_check(
		"tooltip_omits_cost_when_source_absent",
		hud.retail_tooltip.visible and not hud.retail_tooltip.cost_row_visible()
	)
	# Tooltip hides on exit.
	hud._tooltip_hover_button = button
	hud._end_tooltip_hover(button)
	_check("tooltip_hides_on_exit", not hud.retail_tooltip.visible)


func _check_side_command_bar(hud) -> void:
	# Selecting the builder reveals the right-edge build column; deselecting hides
	# it. Side buttons re-emit the shared construct order with the correct kind.
	hud.set_command_costs({SOLDIER_ID: 200, "farm": 300})
	hud.set_unit_selection_state(
		[91] as Array[int],
		{91: {"object_id": "bfme2.object.men-porter", "is_builder": true}}
	)
	var bar = hud.retail_side_command_bar
	# The porter strip mirrors retail (REF-29): the authored build set minus
	# the fortress/expansion commands that ride other command sets.
	var construct_specs := 0
	for spec_value in HudScript.RETAIL_UNIT_ACTION_SPECS:
		var action_id := String((spec_value as Dictionary)["action_id"])
		if action_id.begins_with("construct_") and not HudScript.PORTER_STRIP_EXCLUDED_KINDS.has(action_id.trim_prefix("construct_")):
			construct_specs += 1
	_check(
		"side_bar_shows_for_builder",
		bar.builder_bar_shown() and bar.visible and bar.side_buttons().size() == construct_specs,
		str(bar.side_buttons().size())
	)
	var side_events: Array[String] = []
	hud.construct_requested.connect(func(kind: String) -> void: side_events.append(kind))
	var first_side: Button = bar.side_buttons()[0]
	var expected_kind := String(first_side.get_meta("construct_kind", ""))
	first_side.pressed.emit()
	_check(
		"side_button_emits_construct_requested",
		side_events == [expected_kind] and expected_kind == "farm",
		"%s|%s" % [expected_kind, str(side_events)]
	)
	# Side-bar buttons carry retail tooltips too.
	hud.show_retail_tooltip(first_side)
	_check(
		"side_button_has_retail_tooltip",
		hud.retail_tooltip.visible and hud.retail_tooltip.cost_text() == "Cost: 300",
		hud.retail_tooltip.cost_text()
	)
	hud.set_unit_selection_state([] as Array[int], {})
	_check("side_bar_hides_without_builder", not bar.builder_bar_shown())


func _check_complete_binding(prefix: String, hud, content, pack_root: String) -> void:
	var exact_control_bar_bound: bool = (
		hud.retail_presentation_bound
		and hud.retail_control_bar_bound
		and (
			(
				hud.retail_apt_bound
				and hud.retail_apt_runtime.visible
				and hud.retail_apt_runtime.contract_ready
				and hud.retail_apt_runtime.presentation_ready
				and hud.retail_control_bar_frame.visible
				and hud.retail_control_bar_frame.has_retail_shell()
				and hud.retail_control_bar_frame.retail_image_id == "PalantirFrame_GoodDouble"
				and hud.retail_control_bar_frame.retail_source_size == HudScript.RETAIL_PALANTIR_FRAME_SOURCE_SIZE
				and hud.retail_control_bar_frame.retail_image_path == HudScript.RETAIL_PALANTIR_FRAME_ATLAS
			)
			or (
				not hud.retail_apt_bound
				and hud.retail_control_bar_frame.visible
				and hud.retail_control_bar_frame.has_retail_shell()
				and hud.retail_control_bar_frame.retail_image_id == HudScript.RETAIL_COMMAND_BAR_IMAGE_ID
				and hud.retail_control_bar_frame.retail_source_size == HudScript.RETAIL_COMMAND_BAR_SOURCE_SIZE
				and hud.retail_control_bar_frame.retail_image_path == String(content.resolve_retail_ui_image_path(HudScript.RETAIL_COMMAND_BAR_IMAGE_ID))
			)
		)
		and not hud.synthetic_palantir_frame.uses_synthetic_shell()
	)
	_check(
		"%s_exact_retail_control_bar" % prefix,
		exact_control_bar_bound
	)
	_check(
		"%s_no_visible_synthetic_panel_shells" % prefix,
		hud.get_node("CommandPanel").get_theme_stylebox("panel") is StyleBoxEmpty
			and hud.get_node("PalantirDock/ResourceStrip").get_theme_stylebox("panel") is StyleBoxEmpty
			# Playable text surfaces stay visible, but shell-less: no synthetic
			# grey panel art may cover the retail control bar.
			and hud.get_node("ObjectiveBanner").get_theme_stylebox("panel") is StyleBoxEmpty
			and hud.get_node("FeedbackPanel").get_theme_stylebox("panel") is StyleBoxEmpty
	)
	hud.show_diagnostics("must remain suppressed", true)
	_check("%s_diagnostics_suppressed" % prefix, not hud.diagnostics_panel.visible)
	for spec_value in HudScript.RETAIL_COMMAND_SPECS:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		var button: Button = hud.train_buttons.get(unit_id)
		var image_id := String(spec["image_id"])
		var label_id := String(spec["label_id"])
		var tooltip_id := String(spec["tooltip_id"])
		var expected_path := String(content.resolve_retail_ui_image_path(image_id))
		_check("%s_%s_button_exists" % [prefix, button.name if button != null else unit_id], button != null)
		if button == null:
			continue
		_check(
			"%s_%s_retail_icon_source" % [prefix, button.name],
			button.icon != null
				and button.icon.get_width() == 64
				and button.icon.get_height() == 64
				and String(button.get_meta("retail_icon_id", "")) == image_id
				and String(button.get_meta("retail_icon_path", "")) == expected_path
				and expected_path.begins_with(pack_root.path_join("assets"))
				and bool(content.is_resolved_asset_path(expected_path)),
			expected_path
		)
		_check(
			"%s_%s_retail_strings" % [prefix, button.name],
			String(button.get_meta("retail_label_id", "")) == label_id
				and String(button.get_meta("retail_tooltip_id", "")) == tooltip_id
				and button.text == ""
				and button.tooltip_text == String(content.get_retail_string(tooltip_id, ""))
		)
		# The frame must be either empty or the retail empty-socket art (the
		# unified socket-button system uses the atlas region as the button
		# background); any other stylebox is a synthetic frame.
		var frame_box := button.get_theme_stylebox("normal")
		var retail_socket_frame := frame_box is StyleBoxTexture and (frame_box as StyleBoxTexture).texture is AtlasTexture
		_check(
			"%s_%s_no_synthetic_button_frame" % [prefix, button.name],
			frame_box is StyleBoxEmpty or retail_socket_frame
		)
	for spec_value in HudScript.RETAIL_UNIT_ACTION_SPECS:
		var spec: Dictionary = spec_value
		var action_id := String(spec["action_id"])
		var button: Button = hud.unit_action_buttons.get(action_id)
		var expected_path := String(content.resolve_retail_ui_image_path(String(spec["image_id"])))
		var action_frame := button.get_theme_stylebox("normal") if button != null else null
		var action_retail_frame := action_frame is StyleBoxTexture and (action_frame as StyleBoxTexture).texture is AtlasTexture
		_check(
			"%s_%s_exact_retail_action" % [prefix, action_id],
			button != null
				and button.icon != null
				and button.text == ""
				and String(button.get_meta("retail_icon_path", "")) == expected_path
				and String(button.get_meta("retail_label", "")) == String(content.get_retail_string(String(spec["label_id"]), ""))
				and (action_frame is StyleBoxEmpty or action_retail_frame)
		)
	for portrait_value in HudScript.RETAIL_PORTRAIT_SPECS:
		var portrait_spec: Dictionary = portrait_value
		var unit_id := String(portrait_spec["unit_id"])
		var portrait_bindings: Dictionary = hud.selection_portrait.get_meta("retail_portrait_bindings", {})
		var metadata: Dictionary = portrait_bindings.get(unit_id, {})
		var expected_path := String(content.resolve_retail_ui_image_path(String(portrait_spec["image_id"])))
		_check(
			"%s_%s_exact_retail_portrait" % [prefix, unit_id],
			hud.retail_portraits_bound
				and String(metadata.get("image_id", "")) == String(portrait_spec["image_id"])
				and String(metadata.get("path", "")) == expected_path
				and Vector2i(metadata.get("source_size", Vector2i.ZERO)) == HudScript.RETAIL_PORTRAIT_SOURCE_SIZE
		)
		hud.set_production_state([unit_id], true)
		_check(
			"%s_%s_portrait_selects" % [prefix, unit_id],
			hud.selection_portrait.visible
				and hud.selection_portrait.texture != null
				and hud.selection_portrait.texture.get_size() == Vector2(HudScript.RETAIL_PORTRAIT_SOURCE_SIZE)
				and String(hud.selection_portrait.get_meta("retail_active_portrait_unit_id", "")) == unit_id
		)


func _write_complete_fixture_images() -> Dictionary:
	var rows: Dictionary = {}
	for spec_value in HudScript.RETAIL_COMMAND_SPECS:
		var image_id := String((spec_value as Dictionary)["image_id"])
		rows[image_id] = _write_fixture_png(image_id, Vector2i(64, 64), Color("476a87"))
	for spec_value in HudScript.RETAIL_PORTRAIT_SPECS:
		var image_id := String((spec_value as Dictionary)["image_id"])
		rows[image_id] = _write_fixture_png(image_id, HudScript.RETAIL_PORTRAIT_SOURCE_SIZE, Color("786647"))
	for spec_value in HudScript.RETAIL_UNIT_ACTION_SPECS:
		var spec := spec_value as Dictionary
		var image_id := String(spec["image_id"])
		var source_size := Vector2i(64, 64) if String(spec["action_id"]).begins_with("construct_") else Vector2i(63, 63)
		rows[image_id] = _write_fixture_png(image_id, source_size, Color("876a47"))
	for kind_value in HudScript.EXPANSION_COMMAND_SPECS.keys():
		var expansion_image_id := String((HudScript.EXPANSION_COMMAND_SPECS[kind_value] as Dictionary)["image_id"])
		rows[expansion_image_id] = _write_fixture_png(expansion_image_id, Vector2i(64, 64), Color("6a8747"))
	rows[HudScript.RETAIL_COMMAND_BAR_IMAGE_ID] = _write_fixture_png(
		HudScript.RETAIL_COMMAND_BAR_IMAGE_ID,
		HudScript.RETAIL_COMMAND_BAR_SOURCE_SIZE,
		Color("39412f")
	)
	return rows


func _write_fixture_png(image_id: String, image_size: Vector2i, color: Color) -> Dictionary:
	var relative := "assets/%s.png" % image_id.to_lower()
	var image := Image.create(image_size.x, image_size.y, false, Image.FORMAT_RGBA8)
	image.fill(color)
	var error := image.save_png(_fixture_root.path_join(relative))
	_check("fixture_%s_written" % image_id, error == OK, error_string(error))
	return {"path": relative, "width": image_size.x, "height": image_size.y}


func _fixture_strings() -> Dictionary:
	var result: Dictionary = {}
	for spec_value in HudScript.RETAIL_COMMAND_SPECS:
		var spec: Dictionary = spec_value
		result[String(spec["label_id"])] = "Fixture %s" % String(spec["button_name"])
		result[String(spec["tooltip_id"])] = "Fixture tooltip %s" % String(spec["button_name"])
	for spec_value in HudScript.RETAIL_UNIT_ACTION_SPECS:
		var spec: Dictionary = spec_value
		result[String(spec["label_id"])] = "Fixture %s" % String(spec["button_name"])
		result[String(spec["tooltip_id"])] = "Fixture tooltip %s" % String(spec["button_name"])
	for kind_value in HudScript.EXPANSION_COMMAND_SPECS.keys():
		var expansion_spec: Dictionary = HudScript.EXPANSION_COMMAND_SPECS[kind_value]
		result[String(expansion_spec["label_id"])] = "Fixture %s" % String((expansion_spec as Dictionary)["button_name"])
		result[String(expansion_spec["tooltip_id"])] = "Fixture tooltip %s" % String((expansion_spec as Dictionary)["button_name"])
	return result


func _check_state(name: String, hud, supported: Array, enabled: bool) -> void:
	var queue_count := 2 if not supported.is_empty() else 0
	hud.set_production_state(supported, enabled, queue_count)
	var exact := true
	for spec_value in HudScript.RETAIL_COMMAND_SPECS:
		var unit_id := String((spec_value as Dictionary)["unit_id"])
		var button: Button = hud.train_buttons[unit_id]
		var expected_visible := supported.has(unit_id)
		exact = exact and button.visible == expected_visible
		exact = exact and button.disabled == (not enabled or not expected_visible)
		exact = exact and int(button.get_meta("producer_queue_count", -1)) == queue_count
	_check(name, exact)


func _all_commands_failed_closed(hud) -> bool:
	for button_value in hud.train_buttons.values():
		var button: Button = button_value
		if button.icon != null or button.visible or not button.disabled:
			return false
	for button_value in hud.unit_action_buttons.values():
		var button: Button = button_value
		if button.icon != null or button.visible or not button.disabled:
			return false
	return true


func _cleanup_fixture() -> void:
	if _fixture_root == "":
		return
	var global_root := ProjectSettings.globalize_path(_fixture_root)
	var fixture_dir := DirAccess.open(_fixture_root)
	if fixture_dir != null:
		for file_name in fixture_dir.get_files():
			DirAccess.remove_absolute(ProjectSettings.globalize_path(_fixture_root.path_join(file_name)))
	var assets_dir := DirAccess.open(_fixture_root.path_join("assets"))
	if assets_dir != null:
		for file_name in assets_dir.get_files():
			DirAccess.remove_absolute(ProjectSettings.globalize_path(_fixture_root.path_join("assets").path_join(file_name)))
		DirAccess.remove_absolute(ProjectSettings.globalize_path(_fixture_root.path_join("assets")))
	DirAccess.remove_absolute(global_root)
	_fixture_root = ""


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_FOUR_UNIT_HUD PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_FOUR_UNIT_HUD FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	_cleanup_fixture()
	print("RETAIL_FOUR_UNIT_HUD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
