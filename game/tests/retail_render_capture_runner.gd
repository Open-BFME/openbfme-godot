extends SceneTree
## Non-headless private capture gate for the composed Men/Fords retail slice.

const SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const PRIVATE_SCRATCH_PATH := "res://../.private/scratch"
const DEFAULT_VIEWPORT := Vector2i(1920, 1080)
const MAX_READY_FRAMES := 2400
const SETTLE_FRAMES := 12


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_RENDER_CAPTURE_RUNNER")
	create_timer(300.0, true, false, true).timeout.connect(_fail.bind("render capture watchdog timeout"))
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		_fail("render capture requires a non-headless Forward+ window")
		return
	var output := ProjectSettings.globalize_path(OS.get_environment("OPENBFME_CAPTURE_PATH")).simplify_path().replace("\\", "/")
	var private_scratch_root := ProjectSettings.globalize_path(PRIVATE_SCRATCH_PATH).simplify_path().replace("\\", "/")
	if output == "" or output.get_extension().to_lower() != "png" or not _path_is_within(private_scratch_root, output):
		_fail("OPENBFME_CAPTURE_PATH must be a contained PNG below the repository .private/scratch root")
		return
	if FileAccess.file_exists(output) or DirAccess.dir_exists_absolute(output):
		_fail("OPENBFME_CAPTURE_PATH must not already exist")
		return
	if _path_has_link_component(private_scratch_root, output):
		_fail("OPENBFME_CAPTURE_PATH crosses a link, junction, or unreadable directory")
		return
	var viewport_size := _capture_viewport()
	if viewport_size == Vector2i.ZERO:
		return
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(viewport_size)
	root.size = viewport_size
	await process_frame
	await process_frame
	var packed := load(SCENE_PATH) as PackedScene
	if packed == null:
		_fail("retail vertical-slice scene did not load")
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	for _index in MAX_READY_FRAMES:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not bool(slice.ready_ok):
		_fail("retail slice did not become ready: %s" % String(slice.failure_reason))
		return
	var scenario_mode := OS.get_environment("OPENBFME_CAPTURE_SCENARIO_MODE") == "1"
	if scenario_mode:
		slice.simulation_paused = true
		slice.simulation.ai_enabled = false
	var focus_text := OS.get_environment("OPENBFME_CAPTURE_CAMERA_FOCUS")
	var focus_battalion_text := OS.get_environment("OPENBFME_CAPTURE_FOCUS_BATTALION")
	if focus_battalion_text != "":
		if not focus_battalion_text.is_valid_int() or not slice.battalion_nodes.has(int(focus_battalion_text)):
			_fail("OPENBFME_CAPTURE_FOCUS_BATTALION must name a live battalion")
			return
		var focus_entity: Dictionary = slice.simulation.entity(int(focus_battalion_text))
		slice.camera_focus = Vector2(focus_entity.get("position", Vector2.ZERO))
	if focus_text != "":
		var focus_parts := focus_text.split(",", false)
		if focus_parts.size() != 2 or not String(focus_parts[0]).is_valid_float() or not String(focus_parts[1]).is_valid_float():
			_fail("OPENBFME_CAPTURE_CAMERA_FOCUS must be x,z")
			return
		slice.camera_focus = Vector2(float(focus_parts[0]), float(focus_parts[1]))
	var zoom_text := OS.get_environment("OPENBFME_CAPTURE_CAMERA_ZOOM")
	if zoom_text != "":
		if not zoom_text.is_valid_float() or float(zoom_text) < 0.0 or float(zoom_text) > 1.0:
			_fail("OPENBFME_CAPTURE_CAMERA_ZOOM must be in 0..1")
			return
		slice.camera_zoom = float(zoom_text)
		slice.camera_zoom_target = float(zoom_text)
	if focus_text != "" or focus_battalion_text != "" or zoom_text != "":
		if OS.get_environment("OPENBFME_CAPTURE_UNCLAMPED") != "1":
			slice._clamp_camera_focus()
		slice._apply_camera_transform()
	var selected_text := OS.get_environment("OPENBFME_CAPTURE_SELECT_BATTALION")
	if OS.get_environment("OPENBFME_CAPTURE_SELECT_BUILDER") == "1":
		for entity_id in slice.simulation.entity_ids():
			var entity_row: Dictionary = slice.simulation.entity(entity_id)
			if int(entity_row.get("team", -1)) == 0 and bool(entity_row.get("is_builder", false)):
				selected_text = str(entity_id)
				break
	if OS.get_environment("OPENBFME_CAPTURE_SELECT_FORTRESS") == "1":
		# The fortress specifically (SELECT_STRUCTURE deliberately prefers a
		# non-fortress producer), plus an optional radial page, so the paged
		# fortress command wheel and its Fortress Upgrades / Heroes sub-menus can
		# be captured as they are actually rendered.
		var fortress_id := int(slice.simulation.fortress_id(0))
		if fortress_id == 0:
			_fail("OPENBFME_CAPTURE_SELECT_FORTRESS found no player fortress")
			return
		slice.selected_structure_id = fortress_id
		var fortress_row: Dictionary = slice.simulation.structure(fortress_id)
		slice.camera_focus = Vector2(fortress_row.get("position", Vector2.ZERO))
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		slice._sync_presentation()
		var radial_page := OS.get_environment("OPENBFME_CAPTURE_RADIAL_PAGE").strip_edges()
		if radial_page != "":
			slice.hud.set_radial_page(radial_page)
			slice._sync_presentation()
			slice._refresh_hud()
	if OS.get_environment("OPENBFME_CAPTURE_SELECT_STRUCTURE") == "1":
		# Selects the player team's first completed non-fortress structure when
		# present, otherwise the player fortress.
		var chosen_structure := 0
		var fallback_structure := 0
		for structure_id in slice.simulation.structure_ids():
			var structure_row: Dictionary = slice.simulation.structure(structure_id)
			if int(structure_row.get("team", -1)) != 0 or int(structure_row.get("health", 0)) <= 0:
				continue
			if fallback_structure == 0:
				fallback_structure = structure_id
			if String(structure_row.get("structure_kind", "")) != "fortress" and float(structure_row.get("construction_progress", 1.0)) >= 1.0:
				if not Array(structure_row.get("production", [])).is_empty():
					chosen_structure = structure_id
					break
				if chosen_structure == 0:
					chosen_structure = structure_id
		if chosen_structure == 0:
			chosen_structure = fallback_structure
		if chosen_structure == 0:
			_fail("OPENBFME_CAPTURE_SELECT_STRUCTURE found no player structure")
			return
		slice.selected_structure_id = chosen_structure
		var chosen_row: Dictionary = slice.simulation.structure(chosen_structure)
		slice.camera_focus = Vector2(chosen_row.get("position", Vector2.ZERO))
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		slice._sync_presentation()
		if OS.get_environment("OPENBFME_CAPTURE_DEBUG_RADIAL") == "1":
			var layer: Control = slice.hud._radial_layer
			print("RADIAL_DEBUG layer_visible=%s in_tree_visible=%s buttons=%d layer_pos=%s layer_size=%s" % [
				layer.visible, layer.is_visible_in_tree(), slice.hud._radial_buttons.size(), layer.position, layer.size,
			])
			for button_value in slice.hud._radial_buttons:
				var b := button_value as Button
				print("RADIAL_DEBUG_BTN %s pos=%s visible=%s alpha=%s icon=%s in_tree=%s" % [b.name, b.position, b.visible, b.modulate.a, b.icon != null, b.is_visible_in_tree()])
	if selected_text != "":
		if not selected_text.is_valid_int() or not slice.battalion_nodes.has(int(selected_text)):
			_fail("OPENBFME_CAPTURE_SELECT_BATTALION must name a live battalion")
			return
		if not bool(slice.simulation.select_only(int(selected_text))):
			_fail("OPENBFME_CAPTURE_SELECT_BATTALION could not select the battalion")
			return
		slice._sync_presentation()
	var move_offset_text := OS.get_environment("OPENBFME_CAPTURE_MOVE_OFFSET").strip_edges()
	if move_offset_text != "":
		if selected_text == "":
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET requires OPENBFME_CAPTURE_SELECT_BATTALION")
			return
		var move_parts := move_offset_text.split(",", false)
		if move_parts.size() != 2 or not String(move_parts[0]).is_valid_float() or not String(move_parts[1]).is_valid_float():
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET must be x,z")
			return
		var selected_entity: Dictionary = slice.simulation.entity(int(selected_text))
		var selected_position := selected_entity.get("position", Vector2.ZERO) as Vector2
		var destination := selected_position + Vector2(float(move_parts[0]), float(move_parts[1]))
		if int(slice.test_move(destination)) != 1:
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET could not issue the authoritative move")
			return
		slice.camera_focus = selected_position.lerp(destination, 0.5)
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		var move_advance_ticks := 0
		if scenario_mode:
			move_advance_ticks = _capture_advance_ticks(1)
			if move_advance_ticks < 0:
				return
			slice.simulation.advance(move_advance_ticks)
		slice._sync_presentation()
		var moved_entity: Dictionary = slice.simulation.entity(int(selected_text))
		print("RETAIL_RENDER_MOVE_HINT battalion=%s destination=%s advance_ticks=%d state=%s" % [
			selected_text,
			destination,
			move_advance_ticks,
			String(moved_entity.get("state", "")),
		])
	var attack_pair_text := OS.get_environment("OPENBFME_CAPTURE_ATTACK_PAIR").strip_edges()
	if attack_pair_text != "" and not _pose_attack_pair(slice, attack_pair_text):
		return
	if OS.get_environment("OPENBFME_CAPTURE_ARM_CONSTRUCT") != "":
		# Arms a porter construct command and parks the cursor mid-battlefield so
		# the placement ghost + footprint ring render (REF-29).
		var construct_kind := OS.get_environment("OPENBFME_CAPTURE_ARM_CONSTRUCT")
		for entity_id in slice.simulation.entity_ids():
			var entity_row: Dictionary = slice.simulation.entity(entity_id)
			if int(entity_row.get("team", -1)) == 0 and bool(entity_row.get("is_builder", false)):
				slice.simulation.select_only(entity_id)
				break
		slice._arm_construction(construct_kind)
		Input.warp_mouse(root.size * 0.5)
		slice._sync_presentation()
		if OS.get_environment("OPENBFME_CAPTURE_DEBUG_GHOST") == "1" and slice.construction_ghost != null:
			var ghost: Node3D = slice.construction_ghost
			print("GHOST_DEBUG visible=%s pos=%s children=%s" % [str(ghost.visible), str(ghost.global_position), str(ghost.get_children().map(func(c: Node) -> String: return c.name))])
			var ghost_model := ghost.get_node_or_null("GhostModel")
			if ghost_model != null:
				var mesh_count := [0]
				var override_count := [0]
				_count_meshes(ghost_model, mesh_count, override_count)
				print("GHOST_DEBUG meshes=%d overrides=%d" % [mesh_count[0], override_count[0]])
	if OS.get_environment("OPENBFME_CAPTURE_CONSTRUCT_DEMO") != "":
		# Places a real construction site near the player fortress through the
		# sim API and advances until the build is visibly underway, so the
		# floating "Building: N% • Ns left" label renders (REF-27/28).
		var demo_kind := OS.get_environment("OPENBFME_CAPTURE_CONSTRUCT_DEMO")
		slice._grant_test_resources()
		var porter_id := 0
		for entity_id in slice.simulation.entity_ids():
			var entity_row: Dictionary = slice.simulation.entity(entity_id)
			if int(entity_row.get("team", -1)) == 0 and bool(entity_row.get("is_builder", false)):
				porter_id = entity_id
				break
		if porter_id == 0:
			_fail("OPENBFME_CAPTURE_CONSTRUCT_DEMO found no player porter")
			return
		var fortress_row: Dictionary = {}
		for structure_id in slice.simulation.structure_ids():
			var structure_row: Dictionary = slice.simulation.structure(structure_id)
			if int(structure_row.get("team", -1)) == 0 and String(structure_row.get("structure_kind", "")) == "fortress":
				fortress_row = structure_row
				break
		if fortress_row.is_empty():
			_fail("OPENBFME_CAPTURE_CONSTRUCT_DEMO found no player fortress")
			return
		var fortress_position := Vector2(fortress_row.get("position", Vector2.ZERO))
		var porter_ids: Array[int] = [porter_id]
		var placed := false
		var site := fortress_position
		for offset_value in [Vector2(11, 5), Vector2(-11, 5), Vector2(11, -5), Vector2(-11, -5), Vector2(0, 12), Vector2(0, -12), Vector2(14, 0), Vector2(-14, 0)]:
			var candidate: Vector2 = fortress_position + offset_value
			var probe: Dictionary = slice.simulation.validate_construct_site(porter_ids, demo_kind, candidate)
			if bool(probe.get("ok", false)):
				site = candidate
				placed = true
				break
		if not placed:
			_fail("OPENBFME_CAPTURE_CONSTRUCT_DEMO found no valid site near the fortress")
			return
		var issued: Dictionary = slice.simulation.issue_construct(porter_ids, demo_kind, site)
		if not bool(issued.get("ok", false)):
			_fail("OPENBFME_CAPTURE_CONSTRUCT_DEMO construct rejected: %s" % String(issued.get("reason", "")))
			return
		# Advance until the site shows real progress (porter walks over first).
		var progress := 0.0
		var demo_structure_id := 0
		for _demo_tick in 600:
			slice.simulation.advance(1)
			progress = 0.0
			for structure_id in slice.simulation.structure_ids():
				var structure_row: Dictionary = slice.simulation.structure(structure_id)
				if String(structure_row.get("structure_kind", "")) == demo_kind:
					progress = float(structure_row.get("construction_progress", 0.0))
					if demo_structure_id == 0:
						demo_structure_id = structure_id
			if progress >= 0.10:
				break
		if OS.get_environment("OPENBFME_CAPTURE_CONSTRUCT_DEMO_COMPLETE") == "1":
			# Complete the build through the sim and select the finished
			# structure, so its full command surface (research radial) renders.
			for _demo_tick in 2500:
				slice.simulation.advance(1)
				progress = 1.0
				if demo_structure_id != 0:
					progress = float(slice.simulation.structure(demo_structure_id).get("construction_progress", 1.0))
				if progress >= 1.0:
					break
			if demo_structure_id != 0:
				slice.selected_structure_id = demo_structure_id
		slice.camera_focus = site
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		slice._sync_presentation()
		print("RETAIL_RENDER_CONSTRUCT_DEMO kind=%s progress=%.2f site=%s" % [demo_kind, progress, str(site)])
	if OS.get_environment("OPENBFME_CAPTURE_TRAIN_DEMO") == "1":
		# Queues two jobs at the player producer and advances a few ticks so the
		# queue chips show the CCW training dial + live countdown (item 8).
		slice._grant_test_resources()
		var producer_id := 0
		for structure_id in slice.simulation.structure_ids():
			var structure_row: Dictionary = slice.simulation.structure(structure_id)
			if int(structure_row.get("team", -1)) == 0 and not Array(structure_row.get("production", [])).is_empty():
				producer_id = structure_id
				break
		if producer_id == 0:
			_fail("OPENBFME_CAPTURE_TRAIN_DEMO found no player producer")
			return
		var producer_row: Dictionary = slice.simulation.structure(producer_id)
		var queued_types: Array[String] = []
		for production_value in Array(producer_row.get("production", [])):
			var candidate := String(production_value)
			var probe: Dictionary = slice.simulation.queue_unit(0, producer_id, candidate)
			if bool(probe.get("ok", false)):
				queued_types.append(candidate)
			if queued_types.size() >= 2:
				break
		if queued_types.is_empty():
			_fail("OPENBFME_CAPTURE_TRAIN_DEMO could not queue any of %s" % str(producer_row.get("production", [])))
			return
		var unit_type := String(queued_types[0])
		slice.simulation.advance(25)
		slice.selected_structure_id = producer_id
		slice.camera_focus = Vector2(producer_row.get("position", Vector2.ZERO))
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		slice._sync_presentation()
		var chip_state := "none"
		if slice.hud.production_queue_buttons.size() > 0:
			var chip: Button = slice.hud.production_queue_buttons[0]
			var chip_dial := chip.get_node_or_null("TrainingDial") as TextureProgressBar
			var chip_countdown := chip.get_node_or_null("TrainingCountdown") as Label
			chip_state = "visible=%s icon=%s pos=%s dial=%s dial_value=%.2f countdown=%s" % [
				str(chip.visible), str(chip.icon != null), str(chip.global_position),
				str(chip_dial != null and chip_dial.visible), float(chip_dial.value) if chip_dial != null else -1.0,
				chip_countdown.text if chip_countdown != null else "<none>",
			]
		print("RETAIL_RENDER_TRAIN_DEMO producer=%d unit=%s queue=%s chip=%s" % [producer_id, unit_type, str(slice.simulation.production_queue_state(producer_id).size()), chip_state])
	if OS.get_environment("OPENBFME_CAPTURE_SELECT_PAD") != "":
		# Selects a free fortress expansion pad through the same state the pad
		# click handler produces, so the pad-anchored expansion radial renders
		# (owner: clicking a PAD opens the radial with the fortress options).
		var wanted_pad_kind := OS.get_environment("OPENBFME_CAPTURE_SELECT_PAD")
		var pad_fortress := 0
		for structure_id in slice.simulation.structure_ids():
			var structure_row: Dictionary = slice.simulation.structure(structure_id)
			if int(structure_row.get("team", -1)) == 0 and String(structure_row.get("structure_kind", "")) == "fortress":
				pad_fortress = structure_id
				break
		if pad_fortress == 0:
			_fail("OPENBFME_CAPTURE_SELECT_PAD found no player fortress")
			return
		var pad_found := false
		var pad_states: Array = slice.simulation.expansion_pad_states(pad_fortress)
		for pad_index in pad_states.size():
			var pad: Dictionary = pad_states[pad_index]
			if int(pad.get("expansion_structure_id", 0)) != 0:
				continue
			if String(pad.get("pad_kind", "")) != wanted_pad_kind:
				continue
			slice._selected_expansion_pad = {
				"fortress_id": pad_fortress,
				"pad_index": pad_index,
				"pad_kind": wanted_pad_kind,
				"position": Vector2(pad.get("position", Vector2.ZERO)),
			}
			slice.selected_structure_id = pad_fortress
			var fortress_row: Dictionary = slice.simulation.structure(pad_fortress)
			slice.camera_focus = Vector2(fortress_row.get("position", Vector2.ZERO))
			slice._clamp_camera_focus()
			slice._apply_camera_transform()
			pad_found = true
			break
		if not pad_found:
			_fail("OPENBFME_CAPTURE_SELECT_PAD found no free '%s' pad" % wanted_pad_kind)
			return
		slice._sync_presentation()
	if OS.get_environment("OPENBFME_CAPTURE_EVENT_FEED_DEMO") == "1":
		# Presentation check of the retail top-right event feed with the exact
		# retail message shapes (REF-25/30/34/46/48).
		slice.hud.push_event_feed("Insufficient funds.")
		slice.hud.push_event_feed("Construction Complete: Barracks")
		slice.hud.push_event_feed("Upgrade Complete: Fire Arrows")
		slice.hud.push_event_feed("Fortress rally point set.")
	if OS.get_environment("OPENBFME_CAPTURE_ARM_POWER") == "1":
		# Arms the first castable purchased power and parks the cursor
		# mid-battlefield so the AOE range ring renders (REF-49).
		slice._grant_test_resources()
		for row_value in slice.hud._spellbook_power_rows:
			var row: Dictionary = row_value
			var result: Dictionary = slice.simulation.purchase_power(0, String(row.get("power_id", "")), int(row.get("cost", -1)))
			if bool(result.get("ok", false)):
				slice.simulation.accept_spellbook_purchases(0)
				slice.hud.refresh_powers(slice.simulation.power_points(0), slice.simulation.purchased_powers[0], slice.simulation.spellbook_ui_state(0))
				slice.power_cast_armed = String(row.get("power_id", ""))
				break
		Input.warp_mouse(root.size * 0.5)
	if OS.get_environment("OPENBFME_CAPTURE_OPEN_SPELLBOOK") == "1":
		slice.hud._toggle_powers_palette()
	if OS.get_environment("OPENBFME_CAPTURE_POWERS_DEMO") == "1":
		# Buys the first two authored tier powers through the sim API so the
		# left-rim palantir dock renders like REF-24/49.
		slice._grant_test_resources()
		var bought := 0
		for row_value in slice.hud._spellbook_power_rows:
			var row: Dictionary = row_value
			var result: Dictionary = slice.simulation.purchase_power(0, String(row.get("power_id", "")), int(row.get("cost", -1)))
			if bool(result.get("ok", false)):
				bought += 1
			if bought >= 2:
				break
		slice.simulation.accept_spellbook_purchases(0)
		slice.hud.refresh_powers(slice.simulation.power_points(0), slice.simulation.purchased_powers[0], slice.simulation.spellbook_ui_state(0))
		slice._sync_presentation()
	if OS.get_environment("OPENBFME_CAPTURE_POWERS_DEMO") == "all":
		# Full dock: every authored power purchased (overflow-fit check).
		for _grant in 12:
			slice._grant_test_resources()
		for row_value in slice.hud._spellbook_power_rows:
			var row: Dictionary = row_value
			slice.simulation.purchase_power(0, String(row.get("power_id", "")), int(row.get("cost", -1)))
		slice.simulation.accept_spellbook_purchases(0)
		slice.hud.refresh_powers(slice.simulation.power_points(0), slice.simulation.purchased_powers[0], slice.simulation.spellbook_ui_state(0))
		slice._sync_presentation()
	if OS.get_environment("OPENBFME_CAPTURE_TOOLTIP_DEMO") == "1":
		# Shows the retail tooltip for the first side-bar build command through
		# the real hover path (pack strings + sim costs), like REF-32.
		slice.hud._end_tooltip_hover(null)
		var side_buttons: Array = slice.hud.retail_side_command_bar.side_buttons()
		if side_buttons.is_empty():
			_fail("OPENBFME_CAPTURE_TOOLTIP_DEMO requires the builder selected (side bar empty)")
			return
		slice.hud.show_retail_tooltip(side_buttons[0])
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_FOG") == "1":
		slice.world_environment.compositor = null
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_ROADS") == "1" and slice.battlefield.road_container != null:
		slice.battlefield.road_container.visible = false
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_PROPS") == "1" and slice.battlefield.retail_prop_container != null:
		slice.battlefield.retail_prop_container.visible = false
	var hidden_prop_types := OS.get_environment("OPENBFME_CAPTURE_HIDE_PROP_TYPE").split(",", false)
	if not hidden_prop_types.is_empty() and slice.battlefield.retail_prop_container != null:
		for placement_value in slice.battlefield.retail_prop_container.get_children():
			var placement := placement_value as Node3D
			if hidden_prop_types.has(String(placement.get_meta("source_type", ""))):
				placement.visible = false
	if OS.get_environment("OPENBFME_CAPTURE_UNSHADED_TERRAIN") == "1":
		_make_terrain_unshaded(slice.battlefield)
	if OS.get_environment("OPENBFME_CAPTURE_FORCE_TERRAIN_LIGHT_LAYER_ONE") == "1":
		slice.battlefield.terrain_mesh_instance.layers = 1
		for light_value in slice.source_environment_lights:
			var light := light_value as DirectionalLight3D
			if String(light.get_meta("retail_domain", "")) == "terrain":
				light.light_cull_mask = 1
	if OS.get_environment("OPENBFME_CAPTURE_ADD_TEST_TERRAIN_LIGHT") == "1":
		var test_light := DirectionalLight3D.new()
		test_light.name = "CaptureDiagnosticTerrainLight"
		test_light.light_color = Color.WHITE
		test_light.light_energy = 1.0
		test_light.light_cull_mask = slice.battlefield.terrain_mesh_instance.layers
		test_light.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
		slice.add_child(test_light)
	if OS.get_environment("OPENBFME_CAPTURE_INVERT_DOMAIN_LIGHTS") == "1":
		for light_value in slice.source_environment_lights:
			var light := light_value as DirectionalLight3D
			var source_ray := Vector3(light.get_meta("source_ray_direction_local", Vector3.DOWN))
			light.basis = Basis.looking_at(-source_ray, Vector3.UP)
	var settle_frames := SETTLE_FRAMES
	var settle_text := OS.get_environment("OPENBFME_CAPTURE_SETTLE_FRAMES").strip_edges()
	if settle_text != "":
		if not settle_text.is_valid_int() or int(settle_text) < 1 or int(settle_text) > 120:
			_fail("OPENBFME_CAPTURE_SETTLE_FRAMES must be in 1..120")
			return
		settle_frames = int(settle_text)
	for _index in settle_frames:
		await process_frame
	await RenderingServer.frame_post_draw
	if scenario_mode:
		var scenario_id := OS.get_environment("OPENBFME_CAPTURE_SCENARIO_ID")
		var scenario_state := ""
		if scenario_id in ["hud-unit-selected", "unit-soldier-idle", "unit-soldier-move", "unit-soldier-attack"]:
			scenario_state = String(slice.simulation.entity(1).get("state", ""))
		print("RETAIL_RENDER_SCENARIO_STATE id=%s state=%s paused=%s" % [
			scenario_id,
			scenario_state,
			"true" if bool(slice.simulation_paused) else "false",
		])
		if scenario_id == "unit-soldier-attack":
			for participant_id in [1, 101]:
				var participant_node: Node3D = slice.battalion_nodes.get(participant_id) as Node3D
				print("RETAIL_RENDER_SCENARIO_ACTOR id=%d world=%s screen=%s behind=%s visible=%s" % [
					participant_id,
					participant_node.global_position,
					slice.camera.unproject_position(participant_node.global_position),
					"true" if slice.camera.is_position_behind(participant_node.global_position) else "false",
					"true" if participant_node.is_visible_in_tree() else "false",
				])
				var member_visuals: Dictionary = participant_node.get("member_visuals") as Dictionary
				var member_zero: Node3D = member_visuals.get(0) as Node3D
				print("RETAIL_RENDER_SCENARIO_MEMBER battalion=%d member=0 world=%s screen=%s visible=%s" % [
					participant_id,
					member_zero.global_position,
					slice.camera.unproject_position(member_zero.global_position),
					"true" if member_zero.is_visible_in_tree() else "false",
				])
	var image := root.get_texture().get_image()
	if image == null or image.is_empty() or image.get_width() != viewport_size.x or image.get_height() != viewport_size.y:
		_fail("rendered viewport capture has dimensions %dx%d instead of %dx%d" % [image.get_width() if image != null else -1, image.get_height() if image != null else -1, viewport_size.x, viewport_size.y])
		return
	var parent := output.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(parent) != OK:
		_fail("private render-capture directory could not be created")
		return
	var save_error := image.save_png(output)
	if save_error != OK or not FileAccess.file_exists(output):
		_fail("rendered PNG could not be written")
		return
	var camera_metadata: Dictionary = slice.camera.get_meta("retail_camera", {}) as Dictionary
	var structure_diagnostics := _structure_diagnostics(slice)
	var geometry_diagnostics := _geometry_diagnostics(slice)
	var prop_type_diagnostics := _prop_type_diagnostics(slice.battlefield.retail_prop_container)
	print("RETAIL_RENDER_CAMERA position=%s focus=%s local_ground=%s metadata=%s" % [
		slice.camera.global_position,
		slice.camera_focus,
		slice.source_map_data.local_ground_height(slice.camera_focus),
		camera_metadata,
	])
	print("RETAIL_RENDER_STRUCTURES %s" % JSON.stringify(structure_diagnostics))
	print("RETAIL_RENDER_UNIT_MATERIALS %s" % JSON.stringify(_unit_material_diagnostics(slice)))
	print("RETAIL_RENDER_LIGHTING terrain_layers=%d ambient=%s lights=%s" % [
		int(slice.battlefield.terrain_mesh_instance.layers),
		slice.battlefield.terrain_material.get_shader_parameter("sage_ambient_color"),
		JSON.stringify(_light_diagnostics(slice)),
	])
	print("RETAIL_RENDER_CAMERA_GEOMETRY %s" % JSON.stringify(geometry_diagnostics))
	print("RETAIL_RENDER_FLAT_PROP_TYPES %s" % JSON.stringify(prop_type_diagnostics))
	print("RETAIL_RENDER_CAPTURE_OK path=%s width=%d height=%d viewport=%dx%d pack=%s fog_dispatches=%d" % [
		output,
		image.get_width(),
		image.get_height(),
		viewport_size.x,
		viewport_size.y,
		String(slice.selected_pack_root),
		int(slice.linear_fog.execution_metrics().get("dispatch_count", 0)) if slice.linear_fog != null else 0,
	])
	slice.queue_free()
	await process_frame
	quit(0)


func _pose_attack_pair(slice: Node, pair_text: String) -> bool:
	var parts := pair_text.split(",", false)
	if parts.size() != 2 or not String(parts[0]).is_valid_int() or not String(parts[1]).is_valid_int():
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR must be attacker_id,target_id")
		return false
	var attacker_id := int(parts[0])
	var target_id := int(parts[1])
	if not slice.battalion_nodes.has(attacker_id) or not slice.battalion_nodes.has(target_id):
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR must name two live battalions")
		return false
	var attacker_row := slice.simulation.entities.get(attacker_id) as Dictionary
	var target_row := slice.simulation.entities.get(target_id) as Dictionary
	if attacker_row == null or target_row == null:
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR entities are unavailable")
		return false
	var scenario_mode := OS.get_environment("OPENBFME_CAPTURE_SCENARIO_MODE") == "1"
	var attacker_position := attacker_row.get("position", Vector2.ZERO) as Vector2
	if scenario_mode:
		# Stage the pair at the central ford, outside either starting army's
		# acquisition radius. The authoritative attack command below still owns
		# approach, range entry, attack timing, and member presentation.
		attacker_position = Vector2(-1.5, 0.0)
		attacker_row["position"] = attacker_position
		attacker_row["destination"] = attacker_position
		attacker_row["route"] = []
		attacker_row["route_cells"] = []
		attacker_row["route_cursor"] = 0
	var target_position := attacker_position + Vector2(3.0, 0.0)
	target_row["position"] = target_position
	target_row["destination"] = target_position
	if scenario_mode:
		# A Hold Ground defender remains at the retail-authored staging point
		# until the approaching attacker enters weapon range.
		target_row["stance"] = "HoldGround"
	target_row["route"] = []
	target_row["route_cells"] = []
	target_row["route_cursor"] = 0
	var attacker_ids: Array[int] = [attacker_id]
	if not bool(slice.simulation.select_only(attacker_id)) or int(slice.simulation.issue_attack(attacker_ids, target_id)) != 1:
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR could not issue the authoritative attack")
		return false
	var requested_ticks := _capture_advance_ticks(1)
	if requested_ticks < 0:
		return false
	var advance_ticks := 0
	if scenario_mode:
		for _index in requested_ticks:
			slice.simulation.advance(1)
			advance_ticks += 1
			if String(slice.simulation.entity(attacker_id).get("state", "")) == "attack":
				break
		if String(slice.simulation.entity(attacker_id).get("state", "")) != "attack":
			var attacker_after: Dictionary = slice.simulation.entity(attacker_id)
			var target_after: Dictionary = slice.simulation.entity(target_id)
			_fail("OPENBFME_CAPTURE_ATTACK_PAIR did not reach attack within %d ticks: attacker_state=%s attacker_position=%s target_state=%s target_position=%s distance=%s attack_range=%s route_points=%d" % [
				requested_ticks,
				String(attacker_after.get("state", "")),
				Vector2(attacker_after.get("position", Vector2.ZERO)),
				String(target_after.get("state", "")),
				Vector2(target_after.get("position", Vector2.ZERO)),
				Vector2(attacker_after.get("position", Vector2.ZERO)).distance_to(Vector2(target_after.get("position", Vector2.ZERO))),
				float(attacker_after.get("attack_range", 0.0)),
				Array(attacker_after.get("route", [])).size(),
			])
			return false
	else:
		slice.simulation.advance(requested_ticks)
		advance_ticks = requested_ticks
	slice.camera_focus = attacker_position.lerp(target_position, 0.5)
	slice._clamp_camera_focus()
	slice._apply_camera_transform()
	var attacker_after: Dictionary = slice.simulation.entity(attacker_id)
	var target_after: Dictionary = slice.simulation.entity(target_id)
	if scenario_mode:
		# The recipe deliberately relocates the pair before issuing commands.
		# Snap only that setup discontinuity; subsequent member motion and attack
		# presentation still settle through the normal runtime path.
		for participant_id in [attacker_id, target_id]:
			var participant: Dictionary = slice.simulation.entity(participant_id)
			var participant_position := Vector2(participant.get("position", Vector2.ZERO))
			var battalion: Node3D = slice.battalion_nodes.get(participant_id) as Node3D
			_snap_staged_battalion(battalion, Vector3(
				participant_position.x,
				slice._presentation_height(participant_position),
				participant_position.y,
			))
	slice._sync_presentation()
	print("RETAIL_RENDER_ATTACK_POSE attacker=%d target=%d advance_ticks=%d state=%s distance=%s" % [
		attacker_id,
		target_id,
		advance_ticks,
		String(attacker_after.get("state", "")),
		Vector2(attacker_after.get("position", Vector2.ZERO)).distance_to(Vector2(target_after.get("position", Vector2.ZERO))),
	])
	return true


func _snap_staged_battalion(battalion: Node3D, world_position: Vector3) -> void:
	# The formation engine normally compensates root motion to keep individual
	# soldiers world-stable. A capture recipe's one-time staging teleport is not
	# gameplay motion, so reset members to their authored slots and restart the
	# tracker before normal presentation frames resume.
	battalion.call("set_authoritative_position", world_position, true)
	var member_visuals: Dictionary = battalion.get("member_visuals") as Dictionary
	for member_index_value in member_visuals.keys():
		var member_index := int(member_index_value)
		var member: Node3D = member_visuals.get(member_index) as Node3D
		member.position = battalion.call("member_formation_slot", member_index) as Vector3
	var formation: RefCounted = battalion.get("formation") as RefCounted
	formation.set("_root_tracking_ready", false)


func _count_meshes(node: Node, mesh_count: Array, override_count: Array) -> void:
	if node is MeshInstance3D:
		mesh_count[0] = int(mesh_count[0]) + 1
		var mi := node as MeshInstance3D
		var surfaces := mi.mesh.get_surface_count() if mi.mesh != null else 0
		for surface_index in surfaces:
			if mi.get_surface_override_material(surface_index) != null:
				override_count[0] = int(override_count[0]) + 1
	for child in node.get_children():
		_count_meshes(child, mesh_count, override_count)


func _capture_viewport() -> Vector2i:
	var text := OS.get_environment("OPENBFME_CAPTURE_VIEWPORT").strip_edges().to_lower()
	if text == "":
		return DEFAULT_VIEWPORT
	var parts := text.split("x", false)
	if parts.size() != 2 or not String(parts[0]).is_valid_int() or not String(parts[1]).is_valid_int():
		_fail("OPENBFME_CAPTURE_VIEWPORT must be WIDTHxHEIGHT")
		return Vector2i.ZERO
	var result := Vector2i(int(parts[0]), int(parts[1]))
	if result.x < 640 or result.y < 480 or result.x > 3840 or result.y > 2160:
		_fail("OPENBFME_CAPTURE_VIEWPORT must be within 640x480..3840x2160")
		return Vector2i.ZERO
	return result


func _capture_advance_ticks(default_value: int) -> int:
	var text := OS.get_environment("OPENBFME_CAPTURE_ADVANCE_TICKS").strip_edges()
	if text == "":
		return default_value
	if not text.is_valid_int() or int(text) < 1 or int(text) > 120:
		_fail("OPENBFME_CAPTURE_ADVANCE_TICKS must be in 1..120")
		return -1
	return int(text)


func _path_is_within(root_path: String, candidate_path: String) -> bool:
	var root_path_normalized := _comparison_path(root_path).trim_suffix("/")
	var candidate_normalized := _comparison_path(candidate_path)
	return candidate_normalized.begins_with(root_path_normalized + "/")


func _path_has_link_component(root_path: String, candidate_path: String) -> bool:
	var root_path_normalized := _comparison_path(root_path).trim_suffix("/")
	var candidate_normalized := _comparison_path(candidate_path)
	if not _path_is_within(root_path_normalized, candidate_normalized) and candidate_normalized != root_path_normalized:
		return true
	var root_name := root_path_normalized.get_file()
	if root_name != "" and _link_status(root_path_normalized.get_base_dir(), root_name) != 0:
		return true
	var relative := candidate_normalized.substr(root_path_normalized.length()).trim_prefix("/")
	var current := root_path_normalized
	for segment in relative.split("/", false):
		if _link_status(current, segment) != 0:
			return true
		current = current.path_join(segment)
	return false


func _link_status(parent_path: String, child_name: String) -> int:
	var parent := DirAccess.open(parent_path)
	if parent == null:
		return -1
	return 1 if parent.is_link(child_name) else 0


func _comparison_path(path: String) -> String:
	var normalized := path.replace("\\", "/").simplify_path()
	return normalized.to_lower() if OS.get_name() == "Windows" else normalized


func _light_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for light_value in slice.source_environment_lights:
		var light := light_value as DirectionalLight3D
		result.append({
			"domain": String(light.get_meta("retail_domain", "")),
			"name": String(light.get_meta("retail_light_name", "")),
			"mask": light.light_cull_mask,
			"visible": light.is_visible_in_tree(),
			"ray": -light.global_basis.z,
		})
	return result


func _unit_material_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var ids: Array = slice.battalion_nodes.keys()
	ids.sort()
	for id_value in ids.slice(0, mini(2, ids.size())):
		var battalion := slice.battalion_nodes[id_value] as Node3D
		var pending: Array[Node] = [battalion]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null and not String(current.name).begins_with("Member") and not String(current.name).begins_with("Battalion"):
				var mesh_instance := current as MeshInstance3D
				for surface in range(mesh_instance.mesh.get_surface_count()):
					var material := mesh_instance.get_active_material(surface)
					if material is StandardMaterial3D:
						var standard := material as StandardMaterial3D
						result.append({
							"battalion": int(id_value),
							"mesh": String(mesh_instance.name),
							"layers": mesh_instance.layers,
							"surface": surface,
							"albedo": standard.albedo_color,
							"has_texture": standard.albedo_texture != null,
							"shading_mode": standard.shading_mode,
							"emission_enabled": standard.emission_enabled,
							"transparency": standard.transparency,
							"cull_mode": standard.cull_mode,
						})
						if result.size() >= 12:
							return result
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
	return result


func _structure_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for id_value in slice.structure_nodes.keys():
		var structure: Node3D = slice.structure_nodes[id_value] as Node3D
		var bounds := AABB()
		var has_bounds := false
		var pending: Array[Node] = [structure]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
				var mesh_instance := current as MeshInstance3D
				var mesh_bounds := mesh_instance.global_transform * mesh_instance.get_aabb()
				bounds = bounds.merge(mesh_bounds) if has_bounds else mesh_bounds
				has_bounds = true
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
		result.append({
			"id": int(id_value),
			"kind": String(structure.structure_kind),
			"position": structure.global_position,
			"shared_scale": float(structure.shared_uniform_scale),
			"bounds": bounds if has_bounds else AABB(),
			"camera_inside_bounds": has_bounds and bounds.has_point(slice.camera.global_position),
		})
	return result


func _geometry_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var pending: Array[Node] = [slice]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
			var mesh_instance := current as MeshInstance3D
			var bounds := mesh_instance.global_transform * mesh_instance.get_aabb()
			var camera_inside := bounds.has_point(slice.camera.global_position)
			var very_large := maxf(bounds.size.x, maxf(bounds.size.y, bounds.size.z)) >= 100.0
			if camera_inside or very_large:
				result.append({
					"path": String(slice.get_path_to(mesh_instance)),
					"bounds": bounds,
					"visible": mesh_instance.is_visible_in_tree(),
					"camera_inside_bounds": camera_inside,
				})
		for child_value in current.get_children():
			if child_value is Node:
				pending.append(child_value as Node)
	return result


func _make_terrain_unshaded(battlefield: Node) -> void:
	var source: ShaderMaterial = battlefield.terrain_material as ShaderMaterial
	if source == null or source.shader == null:
		return
	source.set_shader_parameter("sage_lighting_enabled", false)


func _prop_type_diagnostics(container: Node3D) -> Array[Dictionary]:
	var by_type: Dictionary = {}
	if container == null:
		return []
	for placement_value in container.get_children():
		var placement := placement_value as Node3D
		var type_name := String(placement.get_meta("source_type", ""))
		var bounds := AABB()
		var has_bounds := false
		var pending: Array[Node] = [placement]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
				var mesh := current as MeshInstance3D
				var mesh_bounds := mesh.global_transform * mesh.get_aabb()
				bounds = bounds.merge(mesh_bounds) if has_bounds else mesh_bounds
				has_bounds = true
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
		if not has_bounds or bounds.size.y > 0.2:
			continue
		var row: Dictionary = by_type.get(type_name, {
			"type": type_name,
			"count": 0,
			"maximum_horizontal_span": 0.0,
			"maximum_height": 0.0,
		})
		row.count = int(row.count) + 1
		row.maximum_horizontal_span = maxf(float(row.maximum_horizontal_span), maxf(bounds.size.x, bounds.size.z))
		row.maximum_height = maxf(float(row.maximum_height), bounds.size.y)
		by_type[type_name] = row
	var result: Array[Dictionary] = []
	for row_value in by_type.values():
		result.append(row_value as Dictionary)
	result.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return String(left.type) < String(right.type))
	return result


func _fail(message: String) -> void:
	printerr("RETAIL_RENDER_CAPTURE_FAIL %s" % message)
	quit(1)
