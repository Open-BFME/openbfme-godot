extends SceneTree
## FULL HUD + PRESENTATION DIAGNOSTIC (owner round 12: "debug this fully with
## the right tools before proceeding"). One boot, one JSON dump of every number
## the open defects need: node geometry/z-order/visibility for the whole dock,
## shroud state per live entity (fog leak), structure aim heights (archer aim),
## authored particle attachments (fortress smoke), builder roster (cycle seat).
## Writes workspace/logs/hud-diagnostic.json and prints a summary.

## Loaded AFTER the tree is up: these scripts depend on the ContentDB autoload,
## which does not exist while a SceneTree script is being compiled.
var StageScript_: GDScript = null
var AptRuntime_: GDScript = null

var _out := {}


func _initialize() -> void:
	call_deferred("_run")


func _control_row(node: Node) -> Dictionary:
	var control := node as Control
	if control == null:
		return {}
	return {
		"name": String(control.name),
		"pos": [control.global_position.x, control.global_position.y],
		"size": [control.size.x, control.size.y],
		"z": control.z_index,
		"visible": control.visible,
		"in_tree_visible": control.is_visible_in_tree(),
		"mouse_filter": int(control.mouse_filter),
	}


func _run() -> void:
	await process_frame
	await process_frame
	StageScript_ = load("res://src/retail_slice/retail_hud_stage.gd")
	AptRuntime_ = load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	var slice = (load("res://scenes/retail_vertical_slice.tscn") as PackedScene).instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + 300000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_out["ready"] = bool(slice.ready_ok)
	_out["failure"] = String(slice.failure_reason)
	var hud = slice.get("hud")
	var simulation = slice.simulation

	# --- 1. dock geometry / z-order ------------------------------------
	var dock_rows: Array = []
	for path in ["PalantirDock", "CommandPanel", "RetailControlBarFrame",
			"RetailPalantirAptRuntime", "RetailHeroBar", "RetailSideCommandBar"]:
		var node: Node = hud.get_node_or_null(path)
		if node != null:
			dock_rows.append(_control_row(node))
	for orb_id in (hud.get("orb_buttons") as Dictionary).keys():
		var row := _control_row((hud.get("orb_buttons") as Dictionary)[orb_id])
		row["id"] = String(orb_id)
		dock_rows.append(row)
	var seat: Node = hud.get_node_or_null("RetailHeroBar/CycleBuilders")
	if seat != null:
		dock_rows.append(_control_row(seat))
	var select_all: Node = hud.get_node_or_null("RetailHeroBar/SelectAllHeroes")
	if select_all != null:
		dock_rows.append(_control_row(select_all))
	_out["dock"] = dock_rows

	# --- 2. authored stage-piece inventory -----------------------------
	var runtime = hud.get("retail_apt_runtime")
	if runtime != null:
		_out["stage_pieces"] = {
			"below_rows": int(runtime.stage_piece_draws_bound()),
			"top_rows": int(runtime.stage_piece_top_draws_bound()),
			"underlays": (runtime.get("stage_underlays") as Array).size(),
		}

	# --- 3. structure aim heights (archer aim) -------------------------
	var aim_rows: Array = []
	for id_value in simulation.structure_ids():
		var node = slice.structure_nodes.get(int(id_value)) if slice.get("structure_nodes") != null else null
		var row: Dictionary = simulation.structure(int(id_value))
		var entry := {
			"id": int(id_value),
			"kind": String(row.get("structure_kind", "")),
			"team": int(row.get("team", -1)),
		}
		if node != null and node.has_method("attack_presentation_height"):
			entry["aim_height"] = float(node.attack_presentation_height())
			entry["target_height"] = float(node.get("_target_height"))
		aim_rows.append(entry)
	_out["structures"] = aim_rows

	# --- 4. shroud state per live entity (fog leak) --------------------
	var shroud = slice.get("shroud_overlay")
	var fog_rows: Array = []
	_out["shroud_enabled"] = shroud != null and bool(shroud.get("enabled"))
	for id_value in simulation.entity_ids():
		var id := int(id_value)
		var entity: Dictionary = simulation.entity(id)
		if int(entity.get("health", 0)) <= 0:
			continue
		var at := Vector2(entity.get("position", Vector2.ZERO))
		var visual = slice.battalion_nodes.get(id) if slice.get("battalion_nodes") != null else null
		var scenario = slice.scenario_unit_nodes.get(id) if slice.get("scenario_unit_nodes") != null else null
		var node_visible := false
		if visual != null and is_instance_valid(visual):
			node_visible = bool(visual.visible)
		elif scenario != null and is_instance_valid(scenario):
			node_visible = bool(scenario.visible)
		fog_rows.append({
			"id": id,
			"team": int(entity.get("team", -1)),
			"unit": String(entity.get("unit_type", "")),
			"shroud_says_visible": shroud == null or bool(shroud.unit_visible(at)),
			"node_visible": node_visible,
			"has_battalion": visual != null,
			"has_scenario_visual": scenario != null,
		})
	_out["entities"] = fog_rows

	# --- 5. builder roster (cycle seat) --------------------------------
	_out["builders"] = {
		"manifest_ids": (slice.faction_manifest.get("builder_unit_ids", []) as Array),
		"live_ids": slice._local_builder_ids(),
	}

	# --- 6. authored particle attachments (smoke) ----------------------
	var particle_rows: Array = []
	for id_value in simulation.structure_ids():
		var node = slice.structure_nodes.get(int(id_value)) if slice.get("structure_nodes") != null else null
		if node == null or node.get("active_particle_system_ids") == null:
			continue
		particle_rows.append({
			"id": int(id_value),
			"kind": String((simulation.structure(int(id_value)) as Dictionary).get("structure_kind", "")),
			"active_particles": (node.get("active_particle_system_ids") as Array).duplicate(),
		})
	_out["particles"] = particle_rows

	# --- 7. HERO UI geometry (owner 2026-08-27: "this ui for heros is broken")
	# Select every hero the roster carries, then measure the cell the movie
	# authors as a CIRCLE (radius 29.5 about local [28, 28]) against what we
	# actually lay out, plus every command seat the selection produced.
	var hero_rows: Array = []
	for id_value in simulation.entity_ids():
		var id := int(id_value)
		var entity: Dictionary = simulation.entity(id)
		if int(entity.get("health", 0)) <= 0 or not bool(entity.get("is_hero", false)):
			continue
		hero_rows.append({"id": id, "unit": String(entity.get("unit_type", ""))})
	_out["heroes"] = hero_rows
	var cell_rows: Array = []
	for hero_id_value in (hud.get("_hero_bar_buttons") as Dictionary).keys():
		var cell: Button = (hud.get("_hero_bar_buttons") as Dictionary)[hero_id_value]
		var row := _control_row(cell)
		row["hero_id"] = int(hero_id_value)
		row["children"] = []
		for child in cell.get_children():
			var child_row := _control_row(child)
			if not child_row.is_empty():
				(row["children"] as Array).append(child_row)
		cell_rows.append(row)
	_out["hero_cells"] = cell_rows
	_out["hero_cell_geometry"] = {
		"cell_size": [StageScript_.hero_cell_size().x, StageScript_.hero_cell_size().y],
		"cell_origin_0": [StageScript_.hero_cell_viewport(0).x, StageScript_.hero_cell_viewport(0).y],
		"portrait_center": [
			StageScript_.hero_scale_size(AptRuntime_.HERO_CELL_PORTRAIT_CENTER_LOCAL).x,
			StageScript_.hero_scale_size(AptRuntime_.HERO_CELL_PORTRAIT_CENTER_LOCAL).y,
		],
		"portrait_radius": [
			StageScript_.hero_scale_size(Vector2.ONE * AptRuntime_.HERO_CELL_PORTRAIT_RADIUS).x,
			StageScript_.hero_scale_size(Vector2.ONE * AptRuntime_.HERO_CELL_PORTRAIT_RADIUS).y,
		],
		"hero_cell_scale": StageScript_.hero_cell_scale(),
		"command_seats": [
			[StageScript_.command_slot_dock(0, Vector2(64, 64)).x, StageScript_.command_slot_dock(0, Vector2(64, 64)).y],
			[StageScript_.command_slot_dock(1, Vector2(64, 64)).x, StageScript_.command_slot_dock(1, Vector2(64, 64)).y],
			[StageScript_.command_slot_dock(2, Vector2(64, 64)).x, StageScript_.command_slot_dock(2, Vector2(64, 64)).y],
			[StageScript_.command_slot_dock(3, Vector2(64, 64)).x, StageScript_.command_slot_dock(3, Vector2(64, 64)).y],
			[StageScript_.command_slot_dock(4, Vector2(64, 64)).x, StageScript_.command_slot_dock(4, Vector2(64, 64)).y],
			[StageScript_.command_slot_dock(5, Vector2(64, 64)).x, StageScript_.command_slot_dock(5, Vector2(64, 64)).y],
		],
		"submenu_seats": [
			[StageScript_.submenu_slot_dock(0, Vector2(64, 64)).x, StageScript_.submenu_slot_dock(0, Vector2(64, 64)).y],
			[StageScript_.submenu_slot_dock(1, Vector2(64, 64)).x, StageScript_.submenu_slot_dock(1, Vector2(64, 64)).y],
			[StageScript_.submenu_slot_dock(2, Vector2(64, 64)).x, StageScript_.submenu_slot_dock(2, Vector2(64, 64)).y],
			[StageScript_.submenu_slot_dock(3, Vector2(64, 64)).x, StageScript_.submenu_slot_dock(3, Vector2(64, 64)).y],
		],
		"stage_scale": [StageScript_.scale_for(Vector2(1920, 1080)).x, StageScript_.scale_for(Vector2(1920, 1080)).y],
	}
	var radial_rows: Array = []
	for button in (hud.get("_radial_buttons") as Array):
		var row := _control_row(button)
		row["icon"] = str((button as Button).icon)
		row["has_cup"] = (button as Button).get_theme_stylebox("normal") != null
		radial_rows.append(row)
	_out["radial_buttons"] = radial_rows
	var file := FileAccess.open("user://hud-diagnostic.json", FileAccess.WRITE)
	file.store_string(JSON.stringify(_out, "  ", true))
	file.close()
	print("DIAG ready=%s entities=%d structures=%d builders=%d shroud=%s" % [
		str(_out["ready"]), fog_rows.size(), aim_rows.size(),
		(_out["builders"]["live_ids"] as Array).size(), str(_out["shroud_enabled"])
	])
	print("DIAG file=%s" % ProjectSettings.globalize_path("user://hud-diagnostic.json"))
	quit(0)
