extends SceneTree
const PackCapability = preload("res://src/content/pack_capability.gd")
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
const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
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
			and PackCapability.is_retail_import(selected_pack_root)
			and PackCapability.provides_faction(selected_pack_root, "men"),
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
		# THE RADAR IS TWO SEPARATE BINDINGS AND THEY GO WRONG DIFFERENTLY.
		#
		# PAPER, from the art pass: retail's authored parchment sheet, cropped out
		# of `apt-palantir-1` at `RETAIL_PARCHMENT_REGION`. This used to assert
		# `private_parity_mode and retail_parchment != null` while the art pass
		# stretched that crop across the WHOLE radar disc as the map - the
		# "palantir icon over the radar" the owner reported. The crop was right;
		# the scale was not. It is now bound as the paper only, at the bezel
		# opening, with the map drawn over it - so the binding is asserted again,
		# with the geometry pinned by minimap_parchment_runner.
		#
		# DRAWING, from `configure_minimap`: the map's own ink art, and never the
		# `-preview.png` landmark painting - see retail_minimap.gd's header for
		# why that distinction is the second bug. Nothing binds it until a match
		# configures the radar, so straight after the art pass it must be null.
		_check(
			"selected_pack_radar_paper_is_the_retail_parchment",
			selected_hud.minimap.radar_paper != null
				and bool(selected_hud.minimap.uses_retail_parchment)
				and String(selected_hud.minimap.parchment_source) == "retail-atlas"
				and selected_hud.minimap.radar_paper.get_size()
					== Vector2(MinimapScript.RETAIL_PARCHMENT_REGION.size)
		)
		_check("selected_pack_radar_not_bound_to_spell_atlas", selected_hud.minimap.map_ink_art == null)
		var radar_probe := PlaceholderTexture2D.new()
		radar_probe.size = Vector2(64, 64)
		selected_hud.configure_minimap(null, null, null, radar_probe)
		_check(
			"selected_pack_radar_backdrop_is_the_map_ink_art",
			selected_hud.minimap.uses_map_ink_art
				and selected_hud.minimap.map_ink_art == radar_probe
				and not selected_hud.minimap.uses_source_preview_as_background
		)
		selected_hud.configure_minimap(null, null, null, null)
		_check("selected_pack_radar_without_ink_art_stays_schematic", not selected_hud.minimap.uses_map_ink_art)
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
	# UI PARITY lane (2026-08-19, owner playtest 2026-08-18). These three pins
	# need no pack art — they must run in BOTH the bound and the blocker branch:
	#
	# "Missing gaps in the radar": retail's [hud] captures show a COMPLETE
	# parchment disc behind the frame at match start (reference/in game ui.jpg,
	# REF-52) — the opaque-black shroud darkening over the map is gone; the
	# shroud now gates blips only.
	_check(
		"radar_draws_no_shroud_darkening",
		not selected_hud.minimap.has_method("_draw_shroud"),
		"the radar must be a complete disc behind the frame (REF-52)"
	)
	# The world ring uses the AUTHORED radial button (InGameRadialMenuStage.apt
	# bttnFrame, 48 stage units) on the measured retail ring (39 stage units,
	# REF-25) — never oversized portraits floating around the fortress. Expected
	# values come through the HUD's own viewport and the apt runtime script, the
	# same inputs production uses.
	var apt_runtime_script := load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	var world_stage_scale: Vector2 = HudScript.StageScript.scale_for(selected_hud._hud_viewport_size())
	var world_expected_size: float = apt_runtime_script.RADIAL_BUTTON_STAGE_SIZE * minf(world_stage_scale.x, world_stage_scale.y)
	_check(
		"world_radial_button_size_is_authored",
		is_equal_approx(selected_hud._world_radial_button_size(), world_expected_size),
		"%.2f vs authored %.2f" % [selected_hud._world_radial_button_size(), world_expected_size]
	)
	# Every command button shows retail's hover tooltip (REF-25) — the
	# world-ring buttons mirror their palantir twin's tooltip metadata and
	# register the hover path.
	var parity_entries: Array = []
	for index in 4:
		parity_entries.append({
			"command_kind": "train", "id": "bfme2.object.parity-%d" % index,
			"icon": null, "text": "P%d" % index, "enabled": true,
			"label": "Parity %d" % index, "tooltip": "", "slot": index + 1,
		})
	selected_hud.sync_radial_commands(Vector2(640, 400), parity_entries)
	var parity_tooltip_gaps: Array[String] = []
	for world_button in selected_hud.world_radial_buttons():
		if String((world_button as Button).get_meta("tooltip_group", "")) == "" \
				or not (world_button as Button).has_meta("tooltip_registered"):
			parity_tooltip_gaps.append(String((world_button as Button).name))
	_check(
		"world_ring_buttons_carry_retail_tooltips",
		parity_tooltip_gaps.is_empty(),
		str(parity_tooltip_gaps)
	)
	selected_hud.hide_radial_commands()
	selected_hud.free()
	# The live game binds the HUD against the host root PLUS every mounted pack
	# root (retail_vertical_slice.gd `ui_pack_roots`), which is how the APT
	# contract and interface-art index are found on a composed selection. Bind
	# one exactly that way for the checks that need a bound retail shell.
	var game_hud = HudScript.new()
	root.add_child(game_hud)
	game_hud.build()
	var game_bind_error: String = game_hud.bind_retail_train_commands(
		content_db, selected_pack_root, true, Array(content_db.pack_roots)
	)
	_check("game_style_binding_succeeds", game_bind_error == "", game_bind_error)
	if game_bind_error == "":
		_check_structure_portrait(game_hud)
		_check_control_bar_click_shield(game_hud)
		_check_command_hotkeys(game_hud)
	game_hud.free()

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
	_check_authored_stage_layout(hud)
	_check_authored_hero_bar(hud)
	_check_paged_command_page_layout(hud)
	_check_measured_seat_parity_and_containment(hud)

	hud.free()
	_cleanup_fixture()
	await process_frame
	_finish()


## Q37: every bottom-bar coordinate must be the AUTHORED Palantir.apt stage
## translation put through the one stage->viewport transform, not a number
## measured off a screenshot.
func _check_authored_stage_layout(hud) -> void:
	var stage: Script = load("res://src/retail_slice/retail_hud_stage.gd")
	var apt: Script = load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	_check(
		"stage_is_the_authored_1024x768_apt_stage",
		stage.STAGE_SIZE == Vector2(1024.0, 768.0)
			and stage.STAGE_SIZE == apt.PALANTIR_STAGE_SIZE
	)
	# The dock is the bottom band of the stage, spanning the FULL width.
	var dock: Control = hud.get_node("PalantirDock")
	var expected_dock: Rect2 = stage.dock_rect(stage.DESIGN_VIEWPORT)
	_check(
		"dock_spans_the_authored_full_width_stage_band",
		is_equal_approx(dock.size.x, expected_dock.size.x)
			and is_equal_approx(dock.size.y, expected_dock.size.y),
		"dock=%s expected=%s" % [str(dock.size), str(expected_dock.size)]
	)
	# THE CONTROL-BAR ART, and what it is NOT.
	#
	# controlbarscheme.ini:156-165 authors the in-game bar as one ImagePart at
	# X:0 Y:520, 1024x248, image `SGCommandBar` (`SMCommandBar` for Men, one per
	# side). That texture SHIPS IN NO ARCHIVE: `SGCommandBar.tga` and its five
	# siblings are absent from every .big in both layers of the RotWK layered
	# install (all archives enumerated, zero hits on "commandbar"). BFME2 moved
	# the in-game HUD to APT, and controlbarscheme.ini is carried-forward BFME1
	# text. So there is no full-width bar bitmap to render, and a lane that
	# claims to have rendered one is claiming something retail does not ship.
	#
	# What IS authored is `PalantirFrame`, placed at stage [0, 512] with an
	# identity matrix, whose sheet (`apt_PalantirExport_17`, 512x256) therefore
	# covers stage [0,512]..[512,768]. Assert exactly that.
	# The composition may carry solid "disc" backing pieces (the dish-glass
	# hole cover; retail's own contract authors solid backing quads the same
	# way), but exactly ONE textured piece - the whole authored sheet - and any
	# backing must be drawn BEFORE it so authored pixels land on top.
	var textured_pieces: Array = []
	var backing_after_sheet := false
	for piece_index in (hud.RETAIL_FRAME_PIECES as Array).size():
		var candidate: Dictionary = hud.RETAIL_FRAME_PIECES[piece_index]
		if candidate.has("region"):
			textured_pieces.append(candidate)
		elif not textured_pieces.is_empty():
			backing_after_sheet = true
	_check(
		"control_bar_composition_is_one_sheet_over_solid_backing",
		textured_pieces.size() == 1 and not backing_after_sheet,
		"textured=%d backing_after_sheet=%s" % [textured_pieces.size(), str(backing_after_sheet)]
	)
	if textured_pieces.size() != 1:
		return
	var frame_piece: Dictionary = textured_pieces[0]
	var authored_sheet := Rect2(
		Vector2.ZERO, Vector2(hud.RETAIL_PALANTIR_FRAME_SOURCE_SIZE)
	)
	var authored_dest := Rect2(
		stage.to_dock(apt.PALANTIR_STAGE_PLACEMENTS["PalantirFrame"]),
		stage.scale_size(Vector2(hud.RETAIL_PALANTIR_FRAME_SOURCE_SIZE))
	)
	_check(
		"control_bar_art_is_the_whole_authored_frame_sheet",
		(frame_piece["region"] as Rect2).is_equal_approx(authored_sheet),
		"region=%s sheet=%s" % [str(frame_piece["region"]), str(authored_sheet)]
	)
	_check(
		"control_bar_art_sits_at_its_authored_stage_rect",
		(frame_piece["dest"] as Rect2).is_equal_approx(authored_dest),
		"dest=%s authored=%s" % [str(frame_piece["dest"]), str(authored_dest)]
	)
	_check(
		"control_bar_frame_node_is_wide_enough_for_the_authored_art",
		float(hud.RETAIL_PALANTIR_DISPLAY_SIZE.x) >= authored_dest.size.x,
		"node=%s art=%s" % [str(hud.RETAIL_PALANTIR_DISPLAY_SIZE.x), str(authored_dest.size.x)]
	)
	# Dish centre: `EmptyGlobe` stage [280.2, 660.9].
	_check(
		"dish_centre_is_the_authored_empty_globe_placement",
		(hud.RETAIL_DISH_CENTER as Vector2).is_equal_approx(
			stage.to_dock(apt.PALANTIR_STAGE_PLACEMENTS["EmptyGlobe"])
		),
		str(hud.RETAIL_DISH_CENTER)
	)
	_check(
		"radar_centre_is_the_authored_radar_background_placement",
		(hud.RETAIL_RADAR_CENTER as Vector2).is_equal_approx(
			stage.to_dock(apt.PALANTIR_STAGE_PLACEMENTS["RadarBackground"])
		),
		str(hud.RETAIL_RADAR_CENTER)
	)
	# The six command sockets: `CommandButtons` stage [289.55, 659.85] plus the
	# authored per-slot local offsets from sprite character 114.
	var slots_match: bool = (
		int(hud.RETAIL_COMMAND_SLOT_SOURCE.size())
		== int(apt.PALANTIR_COMMAND_SLOT_LOCAL.size())
	)
	var slot_detail := ""
	for index in apt.PALANTIR_COMMAND_SLOT_LOCAL.size():
		var expected: Vector2 = stage.command_slot_dock(
			index, hud.RETAIL_COMMAND_SLOT_SIZE
		) - Vector2(360.0, 0.0)
		var actual: Vector2 = hud.RETAIL_COMMAND_SLOT_SOURCE[index]
		if not actual.is_equal_approx(expected):
			slots_match = false
			slot_detail += "slot%d actual=%s authored=%s; " % [index, str(actual), str(expected)]
	_check("command_slot_arc_equals_the_authored_apt_table", slots_match, slot_detail)
	# Money and command points are TWO independently anchored fields, not one
	# glued row. Authored order is money LEFT of command points (Palantir.apt
	# ResourceBar locals 0.8 and 108.65) and both of the owner's retail RotWK
	# captures agree.
	var money: Label = hud.resource_label
	var command_points: Label = hud.command_points_label
	_check(
		"money_and_command_points_are_independently_anchored",
		money.get_parent() == dock and command_points.get_parent() == dock
			and money.get_parent() == command_points.get_parent()
			and not (money.get_parent() is BoxContainer)
	)
	_check(
		"money_sits_at_its_authored_stage_anchor",
		money.position.is_equal_approx(stage.resource_text_rect_dock("Resources").position),
		str(money.position)
	)
	_check(
		"command_points_sit_at_their_authored_stage_anchor",
		command_points.position.is_equal_approx(
			stage.resource_text_rect_dock("CommandPoints").position
		),
		str(command_points.position)
	)
	_check(
		"authored_order_is_money_left_of_command_points",
		money.position.x < command_points.position.x,
		"money=%s cp=%s" % [str(money.position.x), str(command_points.position.x)]
	)
	var band := Rect2(Vector2.ZERO, expected_dock.size)
	_check(
		"both_resource_fields_stay_inside_the_bar",
		band.encloses(Rect2(money.position, money.size))
			and band.encloses(Rect2(command_points.position, command_points.size)),
		"bar=%s money=%s cp=%s" % [
			str(band), str(Rect2(money.position, money.size)),
			str(Rect2(command_points.position, command_points.size)),
		]
	)


## Q38: the hero roster bar rides the authored `InGameHeroSelect` stage anchor,
## draws health as an ARC (no ProgressBar anywhere under a hero button), badges
## the level bottom-left, rings the selection, and carries the select-all
## button.
func _check_authored_hero_bar(hud) -> void:
	var stage: Script = load("res://src/retail_slice/retail_hud_stage.gd")
	var apt: Script = load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	hud.sync_hero_bar([
		{"id": 11, "unit_type": "hero.a", "name": "Hero A", "level": 3,
			"health": 50, "maximum_health": 100, "selected": true},
		{"id": 12, "unit_type": "hero.b", "name": "Hero B", "level": 1,
			"health": 100, "maximum_health": 100, "selected": false},
	])
	var bar: Control = hud.hero_bar
	_check(
		"hero_bar_anchors_at_the_authored_hero_select_stage_point",
		bar.position.is_equal_approx(stage.hero_cell_viewport(0))
			and (apt.PALANTIR_STAGE_PLACEMENTS["HeroSelectUI"] as Vector2)
				== Vector2(375.0, 700.0),
		str(bar.position)
	)
	var first: Button = hud._hero_bar_buttons[11]
	var second: Button = hud._hero_bar_buttons[12]
	_check(
		"hero_cells_use_the_authored_70_unit_pitch",
		second.position.is_equal_approx(
			stage.hero_cell_viewport(1) - bar.position
		) and first.position.is_equal_approx(Vector2.ZERO),
		"first=%s second=%s" % [str(first.position), str(second.position)]
	)
	var progress_bars := 0
	for cell_value in hud._hero_bar_buttons.values():
		for child in (cell_value as Button).get_children():
			if child is ProgressBar:
				progress_bars += 1
	_check("hero_health_is_not_a_progress_bar", progress_bars == 0, str(progress_bars))
	var arc := first.get_node_or_null("HealthArc")
	_check(
		"hero_health_is_drawn_as_an_arc",
		arc != null and arc is RetailHudArcGauge and arc.arc_half_angle > 0.0
			and is_equal_approx(arc.arc_value, 0.5),
		str(arc)
	)
	var badge: Label = first.get_node_or_null("LevelBadge")
	var portrait_centre: Vector2 = stage.scale_size(apt.HERO_CELL_PORTRAIT_CENTER_LOCAL)
	_check(
		"level_badge_is_bottom_left_of_the_portrait",
		badge != null and badge.text == "3"
			and badge.get_rect().get_center().x < portrait_centre.x
			and badge.get_rect().get_center().y > portrait_centre.y,
		"badge=%s centre=%s" % [str(badge.get_rect()), str(portrait_centre)]
	)
	_check(
		"selection_is_a_circle_highlight",
		(first.get_node("SelectedHighlight") as Control).visible
			and not (second.get_node("SelectedHighlight") as Control).visible
	)
	_check(
		"select_all_heroes_button_is_present_at_its_authored_anchor",
		hud._hero_select_all_button != null
			and hud._hero_select_all_button.visible
			and String(hud._hero_select_all_button.get_meta("command_button", ""))
				== "NonCommand_SelectAllHeroes"
			and hud._hero_select_all_button.position.is_equal_approx(
				stage.hero_select_all_viewport() - bar.position
			),
		str(hud._hero_select_all_button.position) if hud._hero_select_all_button != null else "<null>"
	)
	_check(
		"select_all_heroes_uses_named_authored_image",
		hud._hero_select_all_button != null
			and hud._hero_select_all_button.icon != null
			and String(hud._hero_select_all_button.get_meta("retail_image_id", ""))
				== "UCCommon_GoodHeroes",
		String(hud._hero_select_all_button.get_meta("retail_image_id", "<missing>"))
			if hud._hero_select_all_button != null else "<null>"
	)
	# Retail authors the glyph per SIDE: evil factions bind UCCommon_EvilHeroes
	# (commandbutton.ini :4097/:8068/:13546/:13788+), good ones UCCommon_GoodHeroes.
	var surface_before := String(hud._faction_surface)
	hud._faction_surface = "mordor"
	var evil_id := String(hud._hero_select_all_image_id())
	hud._faction_surface = "men"
	var good_id := String(hud._hero_select_all_image_id())
	hud._faction_surface = surface_before
	_check(
		"select_all_heroes_glyph_follows_faction_side",
		evil_id == "UCCommon_EvilHeroes" and good_id == "UCCommon_GoodHeroes",
		"evil=%s good=%s" % [evil_id, good_id]
	)
	_check(
		"faction_icon_sits_left_of_the_select_all_button",
		hud._hero_faction_icon != null
			and hud._hero_faction_icon.position.x < hud._hero_select_all_button.position.x
	)
	# The palantir big-portrait level gauge is the same arc, not a rectangle.
	_check(
		"palantir_level_bar_is_an_arc",
		hud._dish_level_bar is RetailHudArcGauge
	)
	hud.sync_hero_bar([])


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
		if bool(spec.get("authored_fallback", false)):
			# Q41: the formation button's icon is PER-SELECTION (the selected
			# horde's authored two-image ButtonImage, commandbutton.ini:196/:651)
			# and the button only shows for hordes whose command set carries
			# HORDE_TOGGLE_FORMATION. With this fixture's selection it must be
			# hidden rather than wear a global icon.
			_check(
				"%s_%s_is_per_selection_authored" % [prefix, action_id],
				button != null and (not button.visible or button.icon != null)
			)
			continue
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
	rows["UCCommon_GoodHeroes"] = _write_fixture_png(
		"UCCommon_GoodHeroes", Vector2i(64, 64), Color("d8d1b5")
	)
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


## The PAGED command page must fit the palantir graphics.
##
## Retail's fortress command sets are one paged set: `MenFortressCommandSet`
## (commandset.ini:1876-1899) declares `InitialVisible = 6`, hero slots 14-23,
## and `Command_SelectRevivablesMenFortress` (commandbutton.ini:11003-11004)
## reveals them with `CommandRangeStart 14 / CommandRangeCount 10`. TEN buttons
## have to land somewhere authored, and `Palantir.apt` says exactly where: the
## `CommandButtons` sprite (character 114, frame 9) carries the six `glass0..5`
## sockets AND FOUR `subMenu0..subMenu3` placements on an outer ring. Six plus
## four is the ten the hero range asks for.
##
## The owner's 1920x1080 v0.2.8 capture shows neither: eleven portraits on an
## invented ~163px ellipse that swallows the palantir and overlaps the radar
## globe. These checks read the authored table, not a screenshot.
func _check_paged_command_page_layout(hud) -> void:
	var stage: Script = load("res://src/retail_slice/retail_hud_stage.gd")
	var apt: Script = load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	_check(
		"submenu_ring_table_is_the_authored_apt_placements",
		apt.PALANTIR_SUBMENU_SLOT_LOCAL.size() == 4
			and (apt.PALANTIR_SUBMENU_SLOT_LOCAL[0] as Vector2).is_equal_approx(
				Vector2(-0.55, -128.55)
			)
			and (apt.PALANTIR_SUBMENU_SLOT_LOCAL[3] as Vector2).is_equal_approx(
				Vector2(116.45, 41.9)
			),
		str(apt.PALANTIR_SUBMENU_SLOT_LOCAL)
	)
	# A ten-entry hero page, exactly as MenFortressCommandSet reveals it.
	var entries: Array = []
	for index in 10:
		entries.append({
			"command_kind": "hero", "id": "hero.%d" % index, "icon": null,
			"text": "H%d" % index, "enabled": true, "label": "Hero %d" % index,
			"tooltip": "", "slot": 14 + index,
		})
	hud.sync_radial_commands(Vector2(900.0, 400.0), entries)
	var buttons: Array = hud._radial_buttons
	_check(
		"paged_page_builds_one_button_per_authored_slot",
		buttons.size() == entries.size(),
		"%d vs %d" % [buttons.size(), entries.size()]
	)
	var panel_origin: Vector2 = hud.command_panel.position
	var glass_ok := true
	var glass_detail := ""
	for index in mini(buttons.size(), hud.RETAIL_COMMAND_SLOT_SOURCE.size()):
		var expected: Vector2 = panel_origin + (hud.RETAIL_COMMAND_SLOT_SOURCE[index] as Vector2)
		var actual: Vector2 = (buttons[index] as Button).position
		if not actual.is_equal_approx(expected):
			glass_ok = false
			glass_detail += "b%d actual=%s authored=%s; " % [index, str(actual), str(expected)]
	_check("paged_page_keeps_the_six_authored_glass_sockets", glass_ok, glass_detail)
	# OWNER 2026-08-26: the palantir never spills past its six glass sockets —
	# in our composition the subMenu ring rendered over bare grass ("the icons
	# are outside of the circle"). Overflow entries hide on the palantir and
	# stay reachable on the WORLD ring, which always carries the full page.
	var hidden_ok := true
	var hidden_detail := ""
	for index in range(hud.RETAIL_COMMAND_SLOT_SOURCE.size(), buttons.size()):
		if (buttons[index] as Button).visible:
			hidden_ok = false
			hidden_detail += "b%d still visible on the palantir; " % index
	_check("paged_overflow_stays_hidden_on_the_palantir", hidden_ok, hidden_detail)
	var world_live := 0
	for world_button_value in hud._world_radial_buttons:
		if (world_button_value as Button).visible:
			world_live += 1
	_check(
		"world_ring_carries_the_full_overflow_page",
		world_live == entries.size(),
		"world=%d entries=%d" % [world_live, entries.size()]
	)
	# The first four ring seats ARE the authored subMenu placements plus the
	# same parent-registration correction the six glass sockets carry.
	var verbatim_ok := true
	var verbatim_detail := ""
	for ring_index in apt.PALANTIR_SUBMENU_SLOT_LOCAL.size():
		var authored: Vector2 = stage.to_dock(
			(apt.PALANTIR_STAGE_PLACEMENTS["CommandButtons"] as Vector2)
			+ stage.COMMAND_SEAT_REGISTRATION_STAGE
			+ (apt.PALANTIR_SUBMENU_SLOT_LOCAL[ring_index] as Vector2)
		)
		var derived: Vector2 = stage.submenu_slot_center_dock(ring_index)
		if not derived.is_equal_approx(authored):
			verbatim_ok = false
			verbatim_detail += "ring%d derived=%s authored=%s; " % [
				ring_index, str(derived), str(authored)
			]
	_check(
		"first_four_ring_seats_are_the_authored_placements_verbatim",
		verbatim_ok,
		verbatim_detail
	)
	# The owner's symptom, stated as geometry: no command button may sit on the
	# radar globe (`RadarBackground` centre, RETAIL_RADAR_RADIUS).
	var radar_centre: Vector2 = (
		panel_origin - Vector2(360.0, 0.0) + (hud.RETAIL_RADAR_CENTER as Vector2)
	)
	var clear_of_radar := true
	var radar_detail := ""
	for index in buttons.size():
		var button := buttons[index] as Button
		if not button.visible:
			continue  # palantir-hidden overflow renders nothing to overlap
		var centre: Vector2 = button.position + button.size * 0.5
		var distance := centre.distance_to(radar_centre)
		if distance < float(hud.RETAIL_RADAR_RADIUS):
			clear_of_radar = false
			radar_detail += "b%d centre=%s d=%.1f<%.1f; " % [
				index, str(centre), distance, float(hud.RETAIL_RADAR_RADIUS)
			]
	_check("no_paged_command_button_sits_on_the_radar_globe", clear_of_radar, radar_detail)
	# ... nor inside the palantir command dish, where the portrait, the level
	# text and the health arc live.
	var dish_centre: Vector2 = (
		panel_origin - Vector2(360.0, 0.0) + (hud.RETAIL_DISH_CENTER as Vector2)
	)
	var dish_extents: Vector2 = hud.RETAIL_DISH_HALF_EXTENTS
	var clear_of_dish := true
	var dish_detail := ""
	for index in buttons.size():
		var button := buttons[index] as Button
		if not button.visible:
			continue  # palantir-hidden overflow renders nothing to overlap
		var centre: Vector2 = button.position + button.size * 0.5
		var offset: Vector2 = (centre - dish_centre) / dish_extents
		if offset.length() < 1.0:
			clear_of_dish = false
			dish_detail += "b%d centre=%s norm=%.2f; " % [index, str(centre), offset.length()]
	_check("no_paged_command_button_sits_inside_the_dish", clear_of_dish, dish_detail)
	hud.hide_radial_commands()


## Owner playtest 2026-08-26 ("the icons are outside of the circle for the
## palantir"): the movie's seat translations register the imported socket art's
## corner, so every seat drew half a socket up-left of retail — the ring's top
## seats poked above the frame silhouette. The fix is the MEASURED registration
## correction `RetailHudStage.COMMAND_SEAT_REGISTRATION_STAGE`; this gate pins
## the corrected seats to the centroids measured off the retail captures and
## sweeps every visible command-surface button for containment.
func _check_measured_seat_parity_and_containment(hud) -> void:
	var stage: Script = load("res://src/retail_slice/retail_hud_stage.gd")
	# MEASURED (reference/in game ui.jpg AND reference/game.dat_5VsCUnKZ04.jpg,
	# 2560x1440, scaled 0.75 into the 1080p dock): centroids of the six
	# near-black empty-socket blobs; the two captures agree within 0.2 px.
	var measured_socket_centers_dock: Array[Vector2] = [
		Vector2(539.9, 94.1), Vector2(629.5, 117.8), Vector2(678.4, 176.1),
		Vector2(674.5, 246.4), Vector2(615.1, 300.5), Vector2(526.5, 316.5),
	]
	var measured_ok := true
	var measured_detail := ""
	for index in measured_socket_centers_dock.size():
		var derived: Vector2 = stage.command_slot_center_dock(index)
		var distance: float = derived.distance_to(measured_socket_centers_dock[index])
		if distance > 1.5:
			measured_ok = false
			measured_detail += "socket%d derived=%s measured=%s d=%.2f; " % [
				index, str(derived), str(measured_socket_centers_dock[index]), distance
			]
	_check(
		"six_socket_centres_match_the_measured_retail_captures",
		measured_ok, measured_detail
	)
	# The three orb buttons ride the radar's top arc INSIDE the gold frame:
	# their centres stay within the radar disc.
	var orbs_ok := true
	var orbs_detail := ""
	for orb_id in hud.orb_buttons.keys():
		var orb := hud.orb_buttons[orb_id] as Button
		var orb_centre: Vector2 = orb.position + orb.size * 0.5
		var orb_distance: float = orb_centre.distance_to(hud.RETAIL_RADAR_CENTER as Vector2)
		if orb_distance > float(hud.RETAIL_RADAR_RADIUS):
			orbs_ok = false
			orbs_detail += "%s centre=%s d=%.1f>%.1f; " % [
				str(orb_id), str(orb_centre), orb_distance, float(hud.RETAIL_RADAR_RADIUS)
			]
	_check("orb_buttons_sit_inside_the_radar_disc", orbs_ok, orbs_detail)
	# The dish portrait fills the authored SUBGLASS rect: the movie itself
	# places palantirsubglass over the dish (stage (216.9,567.9)-(355.9,732.9))
	# and palantirmainglass over the RADAR. The subglass tucks ~8px under the
	# radar ring's art on its left, exactly as retail composites it — the
	# 2026-08-26 "globe clears the radar" pin was wrong and is retired.
	# The dish art is INSET inside the authored subglass rect so the six
	# authored socket cups stay visible around it, exactly as retail's own
	# fortress capture shows (owner round 10). Pinned: centred on the
	# authored glass (minus the measured leftward nudge off the socket arc)
	# and strictly SMALLER than that glass.
	var glass_centre: Vector2 = (
		(hud.RETAIL_DISH_SUBGLASS_CENTER as Vector2) - Vector2(360, 0)
	)
	var art_centre: Vector2 = (
		hud.selection_portrait.position + hud.selection_portrait.size * 0.5
	)
	var glass_size: Vector2 = (hud.RETAIL_DISH_SUBGLASS_HALF_EXTENTS as Vector2) * 2.0
	_check(
		"dish_art_is_inset_inside_the_authored_subglass",
		hud.selection_portrait.size.x < glass_size.x
			and hud.selection_portrait.size.y < glass_size.y
			and absf(art_centre.y - glass_centre.y) < 1.0
			and art_centre.x < glass_centre.x
			and absf(art_centre.x - glass_centre.x) < 24.0,
		"pos=%s size=%s glass=%s" % [
			str(hud.selection_portrait.position), str(hud.selection_portrait.size),
			str(glass_size)
		]
	)
	_check(
		"radar_paper_and_glass_are_the_authored_placements",
		(hud.RETAIL_RADAR_PAPER_HALF_EXTENTS as Vector2).is_equal_approx(
			Vector2((239.4 - 20.4) * 0.5 * 1.875, (749.7 - 530.7) * 0.5 * 1.40625)
		) and (hud.RETAIL_RADAR_GLASS_HALF_EXTENTS as Vector2).is_equal_approx(
			Vector2((236.8 - 17.8) * 0.5 * 1.875, (714.9 - 529.9) * 0.5 * 1.40625)
		),
		"paper=%s glass=%s" % [
			str(hud.RETAIL_RADAR_PAPER_HALF_EXTENTS), str(hud.RETAIL_RADAR_GLASS_HALF_EXTENTS)
		]
	)
	# Containment sweep: with the fortress hero page open (the widest live
	# range), every palantir command button rides an authored seat centre.
	# The radial layer is a HUD-root sibling of the command panel, so this
	# inspects `_radial_buttons` rather than walking panel descendants.
	var entries: Array = []
	for index in 10:
		entries.append({
			"command_kind": "hero", "id": "sweep.%d" % index, "icon": null,
			"text": "S%d" % index, "enabled": true, "label": "Sweep %d" % index,
			"tooltip": "", "slot": 14 + index,
		})
	hud.sync_radial_commands(Vector2(900.0, 400.0), entries)
	# OWNER 2026-08-26: the palantir seats at most its SIX glass sockets; a
	# back-less ten-entry page shows exactly six, and each visible button
	# rides an authored socket centre — never the subMenu ring over the grass.
	var seat_centers: Array = stage.command_seat_centers_dock(
		hud.RETAIL_COMMAND_SLOT_SOURCE.size()
	)
	var panel_origin: Vector2 = hud.command_panel.position
	var sweep_ok := true
	var sweep_detail := ""
	var swept := 0
	for button_value in hud._radial_buttons:
		var button := button_value as Button
		if button == null or not button.visible:
			continue
		var centre: Vector2 = button.position + button.size * 0.5
		var nearest := INF
		for seat_value in seat_centers:
			var seat_panel: Vector2 = panel_origin + (seat_value as Vector2) - Vector2(360.0, 0.0)
			nearest = minf(nearest, centre.distance_to(seat_panel))
		swept += 1
		if nearest > 1.0:
			sweep_ok = false
			sweep_detail += "%s centre=%s nearest_seat=%.1f; " % [button.name, str(centre), nearest]
	_check(
		"every_visible_command_button_rides_an_authored_seat",
		sweep_ok and swept == hud.RETAIL_COMMAND_SLOT_SOURCE.size(),
		"swept=%d %s" % [swept, sweep_detail]
	)
	hud.hide_radial_commands()
	# The queue chip column dodges every authored seat AND stays inside the
	# panel; the seats moved +32.42/+24.38 dock px with the measured
	# registration, so the old x=445 column would now sit under subMenu2.
	var chip_clear := true
	var chip_detail := ""
	for chip_index in 5:
		var chip_rect := Rect2(
			(hud.RETAIL_QUEUE_CHIP_ORIGIN as Vector2)
				+ Vector2(0.0, float(chip_index) * float(hud.RETAIL_QUEUE_CHIP_PITCH)),
			hud.RETAIL_QUEUE_CHIP_SIZE as Vector2
		)
		if chip_rect.end.x > 520.0 or chip_rect.end.y > 360.0:
			chip_clear = false
			chip_detail += "chip%d %s leaves the panel; " % [chip_index, str(chip_rect)]
		for seat_index in seat_centers.size():
			var seat_rect := Rect2(
				(seat_centers[seat_index] as Vector2) - Vector2(360.0, 0.0)
					- (hud.RETAIL_COMMAND_SLOT_SIZE as Vector2) * 0.5,
				hud.RETAIL_COMMAND_SLOT_SIZE as Vector2
			)
			if chip_rect.intersects(seat_rect):
				chip_clear = false
				chip_detail += "chip%d %s x seat%d %s; " % [
					chip_index, str(chip_rect), seat_index, str(seat_rect)
				]
	_check("queue_chips_dodge_every_authored_seat", chip_clear, chip_detail)


func _check_structure_portrait(hud) -> void:
	# Owner 2026-08-22 HUD audit: a selected fortress showed "Level: 1" in the
	# dish and no picture. Retail shows the building's authored SelectPortrait
	# (fortress.ini:1051 `BPGFortress`); the pack carries the image.
	var image_id := String(hud.structure_portrait_image_id("MenFortress"))
	_check("structure_portrait_reads_authored_select_portrait", image_id == "BPGFortress", image_id)
	hud.set_structure_portrait("MenFortress")
	_check(
		"structure_portrait_shows_in_the_dish",
		hud.selection_portrait.visible and hud.selection_portrait.texture != null
			and String(hud.selection_portrait.get_meta("retail_active_portrait_unit_id", "")) == "MenFortress",
		"visible=%s texture=%s" % [hud.selection_portrait.visible, hud.selection_portrait.texture != null]
	)
	# A unit selection afterwards replaces it (no stale building picture).
	hud.set_production_state([], false)
	hud.set_unit_selection_state([] as Array[int], {})
	_check("structure_portrait_clears_on_deselect", not hud.selection_portrait.visible)
	# An unknown structure is a named miss, never a substitute picture.
	hud.set_structure_portrait("NoSuchStructure")
	_check("structure_portrait_unknown_is_a_named_miss", not hud.selection_portrait.visible)


func _check_control_bar_click_shield(hud) -> void:
	# Owner 2026-08-22 HUD audit: clicks on the gold bezel fell through to the
	# world (deselect / move order under the bar). Retail's bar swallows them.
	var frame = hud.retail_control_bar_frame
	_check(
		"control_bar_frame_stops_mouse_events",
		frame != null and frame.mouse_filter == Control.MOUSE_FILTER_STOP and frame.visible,
		"filter=%s visible=%s" % [frame.mouse_filter if frame != null else -1, frame != null and frame.visible]
	)
	if frame == null or not frame.has_retail_shell():
		_check("control_bar_frame_has_retail_shell", false)
		return
	# Opaque art shields; the transparent stage corner does not. Sample a grid
	# over the authored art rectangle: the two rings and the resource scroll
	# cover well over a quarter of it.
	var hits := 0
	var samples := 0
	for piece_value in frame.retail_pieces:
		var piece := piece_value as Dictionary
		if not piece.has("dest"):
			continue
		var dest := piece["dest"] as Rect2
		for gy in range(1, 20):
			for gx in range(1, 20):
				samples += 1
				if bool(frame.shields_point(dest.position + dest.size * Vector2(gx / 20.0, gy / 20.0))):
					hits += 1
	_check("control_bar_opaque_art_shields_world_clicks", samples > 0 and hits * 4 >= samples, "%d/%d" % [hits, samples])
	var corner := Vector2(frame.size.x - 2.0, 2.0)
	_check("control_bar_transparent_corner_reaches_the_world", not bool(frame.shields_point(corner)), str(corner))
	# Owner 2026-08-26 playtest: the WORLD showed through the palantir dish.
	# The authored frame sheet's dish opening is transparent by design - retail
	# composites the EmptyGlobe glass there, an imported APT character the
	# importer does not convert - and the Q37 one-piece refactor (85e7f776)
	# deleted the backing disc that used to cover it. The dish interior (the
	# frame sheet's dish opening, sheet circle center (286,148) r=74 mapped
	# through the 1.875 x 1.40625 dock transform) must be covered by opaque HUD
	# art everywhere; grass between the command sockets is never retail.
	var dish_center := Vector2(536.25, 208.125)
	var dish_half_extents := Vector2(138.75, 104.0625)
	var interior_holes := 0
	var interior_samples := 0
	for gy in range(-9, 10):
		for gx in range(-9, 10):
			var offset := Vector2(gx / 10.0, gy / 10.0)
			if offset.length() > 0.95:
				continue
			interior_samples += 1
			if not bool(frame.shields_point(dish_center + offset * dish_half_extents)):
				interior_holes += 1
	_check(
		"palantir_dish_interior_is_covered_by_opaque_art",
		interior_samples > 0 and interior_holes == 0,
		"%d/%d holes" % [interior_holes, interior_samples]
	)
	# ...but only INSIDE the authored ring: retail's own silhouette leaves the
	# world visible below the dish (sheet (300,248) is alpha 0 in the authored
	# art). The backing must not spill past the frame silhouette.
	var outside := Vector2(562.5, 348.75)
	_check("dish_backing_stays_inside_the_authored_silhouette", not bool(frame.shields_point(outside)), str(outside))


func _check_command_hotkeys(hud) -> void:
	# Owner 2026-08-22 HUD audit: tooltips showed "Shortcut: <letter>" from the
	# authored `&` markers but no key dispatched anything. With the barracks
	# page up, the soldier button's authored letter must fire train_requested.
	var page: Array = []
	for unit_id_value in hud.train_buttons.keys():
		page.append(String(unit_id_value))
	hud.set_production_state(page, true)
	hud.set_unit_selection_state([] as Array[int], {})
	var soldier_button: Button = null
	var letter := ""
	for unit_id_value in page:
		var candidate_button: Button = hud.train_buttons.get(unit_id_value)
		if candidate_button == null or not candidate_button.visible or candidate_button.disabled:
			continue
		var candidate_letter := String(hud.hotkey_letter_for_button(candidate_button))
		if candidate_letter.length() == 1:
			soldier_button = candidate_button
			letter = candidate_letter
			break
	var soldier_id := String(soldier_button.get_meta("tooltip_unit_id", "")) if soldier_button != null else ""
	_check("soldier_command_authors_a_hotkey_letter", letter.length() == 1 and soldier_id != "", "letter='%s' page=%d" % [letter, page.size()])
	if letter.length() != 1:
		return
	var fired: Array[String] = []
	var handler := func(unit_id: String) -> void: fired.append(unit_id)
	hud.train_requested.connect(handler)
	var dispatched: bool = hud.dispatch_command_hotkey(letter)
	_check("hotkey_letter_fires_the_visible_command", dispatched and fired == [soldier_id], "dispatched=%s fired=%s" % [dispatched, str(fired)])
	# A letter no visible command authors does nothing.
	fired.clear()
	var unused := ""
	for candidate in ["X", "J", "Q", "V", "Y"]:
		var taken := false
		for button in hud._hotkey_candidates():
			if String(hud.hotkey_letter_for_button(button)) == candidate:
				taken = true
		if not taken:
			unused = candidate
			break
	_check("unauthored_hotkey_letter_is_inert", unused != "" and not bool(hud.dispatch_command_hotkey(unused)) and fired.is_empty(), unused)
	# Hidden commands never fire from a key.
	hud.set_production_state([], false)
	fired.clear()
	_check("hidden_command_ignores_its_hotkey", not bool(hud.dispatch_command_hotkey(letter)) and fired.is_empty())
	hud.train_requested.disconnect(handler)
	# Reserved action keys (camera WASD, F/H/Z) are never taken by a command.
	var reserved := InputEventKey.new()
	reserved.physical_keycode = KEY_W
	_check("camera_letters_stay_reserved", bool(hud.hotkey_is_reserved(reserved)))
