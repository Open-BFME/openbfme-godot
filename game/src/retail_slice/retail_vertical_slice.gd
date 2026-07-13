class_name RetailVerticalSlice
extends Node3D
## Integrated private Men-versus-Men production slice. Authoritative gameplay is
## held by RetailSliceSim; Godot nodes interpolate and present that state only.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const BattalionScript = preload("res://src/retail_slice/retail_battalion.gd")
const StructureScript = preload("res://src/retail_slice/retail_structure.gd")
const OrderIndicatorScript = preload("res://src/retail_slice/retail_order_indicator.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const MapDataScript = preload("res://src/retail_slice/retail_map_data.gd")
const BattlefieldScript = preload("res://src/retail_slice/retail_fords_battlefield.gd")
const HudScript = preload("res://src/retail_slice/retail_hud.gd")
const OBJECT_ID := "bfme2.object.gondor-fighter"
const HORDE_ID := "bfme2.object.gondor-fighter-horde"
const MAP_ID := "bfme2.map.fords-of-isen-ii"
const CAPABILITY_ID := "bfme2.animation.gondor-fighter"
const BUILDING_OBJECT_IDS := {
	"fortress": "bfme2.object.men-fortress",
	"farm": "bfme2.object.men-farm",
	"barracks": "bfme2.object.men-barracks",
	"archery_range": "bfme2.object.men-archery-range",
	"stable": "bfme2.object.men-stable",
}

var simulation: RetailSliceSim
var battalion_nodes: Dictionary = {}
var structure_nodes: Dictionary = {}
var order_indicators: Dictionary = {}
var audio_system: RetailSliceAudio
var source_map_data: RetailMapData
var selected_pack_root := ""
var gameplay_rules: Dictionary = {}
var validated_battalion_capability: Dictionary = {}
var ready_ok := false
var failure_reason := ""
var source_driven_terrain := false
var crossing_count := 0
var map_preview_loaded := false
var map_art_loaded := false
var equipment_proof_loaded := false
var simulation_paused := false
var accumulator := 0.0
var camera: Camera3D
var camera_focus := Vector2.ZERO
var camera_zoom := 0.18
var camera_zoom_target := 0.18
var camera_zoom_response_seconds := 0.095
var hud: RetailHud
var status_label: Label
var selection_label: Label
var objective_label: Label
var feedback_label: Label
var minimap: RetailMinimap
var battlefield: RetailFordsBattlefield
var pause_panel: PanelContainer
var failure_panel: PanelContainer
var source_preview_rect: TextureRect
var source_reference_label: Label
var hud_root: Control
var selected_structure_id := 0
var diagnostics_visible := false
var _preview_texture: Texture2D
var _source_art_texture: Texture2D
var _last_presented_winner := -1
var initialization_metrics_ms: Dictionary = {}
var _initialization_started_ms := 0
var _initialization_last_ms := 0


func _ready() -> void:
	_initialization_started_ms = Time.get_ticks_msec()
	_initialization_last_ms = _initialization_started_ms
	_mark_initialization_phase("ready")
	_build_environment()
	_mark_initialization_phase("environment")
	_build_hud()
	_mark_initialization_phase("hud")
	_initialize_content_and_match()


func _initialize_content_and_match() -> void:
	if not ContentDB.bundle_objects.has(OBJECT_ID):
		ContentDB.reload()
	_mark_initialization_phase("content")
	var member_definition := ContentDB.get_bundle_object(OBJECT_ID)
	var horde_definition := ContentDB.get_bundle_object(HORDE_ID)
	var capability := ContentDB.get_animation_capability(CAPABILITY_ID)
	var map_definition := ContentDB.get_bundle_map(MAP_ID)
	if member_definition.is_empty() or horde_definition.is_empty() or capability.is_empty() or map_definition.is_empty():
		_fail("The private bfme2-men-vslice pack is not selected. Run run_importer.bat to build and select it.")
		return
	selected_pack_root = String(member_definition.get("_pack_root", ""))
	if selected_pack_root == "" or String((ModLoader._read_json(selected_pack_root.path_join("pack.json")) as Dictionary).get("id", "")) != "bfme2-men-vslice":
		_fail("The selected content pack is not bfme2-men-vslice.")
		return
	var hud_binding_error := hud.bind_retail_train_command(ContentDB, selected_pack_root, true)
	if hud_binding_error != "":
		_fail("Private Barracks command UI validation failed: %s" % hud_binding_error)
		return
	_mark_initialization_phase("retail_command_ui")

	var asset_factory = load("res://src/view/asset_factory.gd")
	var preview_path := ContentDB.resolve_asset(String(map_definition.get("preview", "")), selected_pack_root)
	var art_path := ContentDB.resolve_asset(String(map_definition.get("art", "")), selected_pack_root)
	_preview_texture = asset_factory.load_texture_asset(preview_path)
	_source_art_texture = asset_factory.load_texture_asset(art_path)
	map_preview_loaded = _preview_texture != null
	map_art_loaded = _source_art_texture != null
	_mark_initialization_phase("map_art")

	source_map_data = MapDataScript.new()
	if not source_map_data.load_from_pack(selected_pack_root, map_definition):
		_fail("Cooked Fords map data failed validation: %s" % source_map_data.error)
		return
	_mark_initialization_phase("map_data")
	battlefield = BattlefieldScript.new()
	battlefield.name = "CookedSourceFordsBattlefield"
	add_child(battlefield)
	if not battlefield.configure(source_map_data):
		_fail("Cooked Fords data was valid, but source geometry could not be built.")
		return
	source_driven_terrain = battlefield.source_driven
	crossing_count = battlefield.ford_marker_count
	_mark_initialization_phase("battlefield")

	capability = _attach_equipment_proof(capability)
	validated_battalion_capability = capability.duplicate(true)
	gameplay_rules = _gameplay_rules(member_definition, horde_definition)
	simulation = SimScript.new()
	simulation.setup(source_map_data.simulation_configuration(), gameplay_rules)
	var player_fortress_id := simulation.fortress_id(0)
	if player_fortress_id != 0:
		var player_fortress_position := Vector2(simulation.structure(player_fortress_id).get("position", Vector2.ZERO))
		var enemy_fortress_position := Vector2(simulation.structure(simulation.fortress_id(1)).get("position", -player_fortress_position))
		# Show the home base from its playable side. Centering directly on the
		# source-edge Fortress exposes empty world beyond the cooked map.
		camera_focus = player_fortress_position + player_fortress_position.direction_to(enemy_fortress_position) * 18.0
		_clamp_camera_focus()
		_apply_camera_transform()
	_mark_initialization_phase("simulation")
	_spawn_all_presentations(validated_battalion_capability, int(horde_definition.get("memberCount", 15)))
	_mark_initialization_phase("presentations")

	audio_system = AudioScript.new()
	add_child(audio_system)
	audio_system.configure(selected_pack_root, DisplayServer.get_name() != "headless")
	_mark_initialization_phase("audio")
	hud.configure_minimap(simulation, source_map_data, camera)
	hud.apply_audio_values(audio_system.get_music_volume(), audio_system.get_voice_sfx_volume(), audio_system.is_muted())
	audio_system.sync_events(simulation.events)
	ready_ok = (
		battalion_nodes.size() == 4
		and structure_nodes.size() == 10
		and map_preview_loaded
		and map_art_loaded
		and equipment_proof_loaded
		and source_map_data.ready
		and source_driven_terrain
		and simulation.source_map_configured
		and simulation.base_loop_enabled
		and audio_system.has_complete_audio_closure()
		and hud.retail_train_command_bound
	)
	if not ready_ok:
		_fail("Retail pack mounted, but a model, equipment, source-map, base-loop, audio, or command-UI capability failed validation.")
		return
	hud.set_feedback("Select a blue battalion to move, or select your Barracks to train soldiers.")
	_sync_presentation()
	_mark_initialization_phase("ready_complete")


func _mark_initialization_phase(phase: String) -> void:
	var now := Time.get_ticks_msec()
	initialization_metrics_ms[phase] = now - _initialization_started_ms
	if DisplayServer.get_name() == "headless":
		print("RETAIL_INIT_PHASE name=%s delta_ms=%d total_ms=%d" % [phase, now - _initialization_last_ms, now - _initialization_started_ms])
	_initialization_last_ms = now


func _attach_equipment_proof(capability: Dictionary) -> Dictionary:
	var result := capability.duplicate(true)
	var proof_relative := String(capability.get("conversionProof", ""))
	if not proof_relative.begins_with("provenance/conversion/") or not proof_relative.ends_with(".json"):
		return result
	var path := selected_pack_root.path_join(proof_relative)
	if not ModLoader.path_is_within(selected_pack_root, path) or not FileAccess.file_exists(path):
		return result
	var proof_value: Variant = ModLoader._read_json(path)
	if typeof(proof_value) != TYPE_DICTIONARY:
		return result
	var proof: Dictionary = proof_value
	var capabilities: Dictionary = proof.get("capabilities", {}) as Dictionary
	var equipment: Dictionary = proof.get("equipment", {}) as Dictionary
	if (
		String(proof.get("schema", "")) != "openbfme.w3d-presentation-capabilities"
		or int(proof.get("schemaVersion", -1)) != 0
		or not bool(capabilities.get("nonRenderGeometryExcluded", false))
		or not bool(capabilities.get("ambiguousBoxGeometryExcluded", false))
		or not bool(capabilities.get("requiredEquipmentProven", false))
		or not bool(capabilities.get("equipmentAttachmentsCanonicalizedRestoredAndRevalidated", false))
		or not equipment.has("right-hand-weapon")
		or not equipment.has("left-hand-shield")
	):
		return result
	var right_hand: Dictionary = equipment["right-hand-weapon"]
	var left_hand: Dictionary = equipment["left-hand-shield"]
	if String(right_hand.get("attachment", "")) != "right-hand" or String(left_hand.get("attachment", "")) != "left-hand":
		return result
	result["equipment"] = {
		"validated": true,
		"right_hand_weapon": right_hand.duplicate(true),
		"left_hand_shield": left_hand.duplicate(true),
	}
	result["unresolvedAnimationTracks"] = 0
	equipment_proof_loaded = true
	return result


func _gameplay_rules(member_definition: Dictionary, horde_definition: Dictionary) -> Dictionary:
	var member: Dictionary = member_definition.get("simulation", {}) as Dictionary
	var member_count := maxi(1, int(horde_definition.get("memberCount", 15)))
	var tick_ms := SimScript.TICK_SECONDS * 1000.0
	return {
		"enable_base_loop": true,
		"starting_resources": 1200,
		"command_point_cap": 200,
		"member_health": maxi(1, int(member.get("health", 200))),
		"member_count": member_count,
		"battalion_damage": maxi(1, int(member.get("baseDamage", 40))) * member_count,
		"speed_world_per_second": maxf(0.1, float(member.get("speed", 55)) / 10.0),
		"pre_attack_ticks": maxi(0, roundi(float(member.get("preAttackMs", 500)) / tick_ms)),
		"attack_period_ticks": maxi(1, roundi(float(member.get("shotIntervalMs", 1000)) / tick_ms)),
		"soldier_cost": maxi(0, int(member.get("cost", 200))),
		"soldier_build_ticks": maxi(1, roundi(float(member.get("buildTimeSeconds", 20)) / SimScript.TICK_SECONDS)),
		"soldier_command_points": maxi(1, int(horde_definition.get("commandPoints", 60))),
		"farm_income": 25,
		"farm_payout_ticks": 50,
		"maximum_queue": 5,
		"ai_queue_interval_ticks": 60,
		"ai_attack_delay_ticks": 300,
	}


func _spawn_all_presentations(capability: Dictionary, expected_members: int) -> void:
	_clear_presentations()
	for id in simulation.entity_ids():
		_spawn_battalion(id, capability, expected_members)
	for id in simulation.structure_ids():
		_spawn_structure(id)


func _spawn_battalion(id: int, capability: Dictionary, expected_members: int) -> void:
	if battalion_nodes.has(id):
		return
	var entity: Dictionary = simulation.entity(id)
	var battalion: RetailBattalion = BattalionScript.new()
	battalion.configure(id, int(entity["team"]), capability, expected_members)
	var position := Vector2(entity["position"])
	battalion.position = Vector3(position.x, _presentation_height(position), position.y)
	add_child(battalion)
	battalion_nodes[id] = battalion
	var indicator: RetailOrderIndicator = OrderIndicatorScript.new()
	indicator.name = "OrderIndicator_%d" % id
	add_child(indicator)
	order_indicators[id] = indicator


func _spawn_structure(id: int) -> void:
	if structure_nodes.has(id):
		return
	var entity: Dictionary = simulation.structure(id)
	var structure: RetailStructure = StructureScript.new()
	var kind := String(entity.get("structure_kind", ""))
	structure.configure(entity, String(BUILDING_OBJECT_IDS.get(kind, "")))
	var position := Vector2(entity["position"])
	structure.position = Vector3(position.x, _presentation_height(position) - 0.35, position.y)
	add_child(structure)
	structure_nodes[id] = structure


func _process(delta: float) -> void:
	_sync_hud_to_viewport()
	_update_camera(delta)
	if not ready_ok:
		return
	if not simulation_paused and simulation.winner == -1:
		accumulator += minf(delta, 0.25)
		while accumulator >= SimScript.TICK_SECONDS:
			accumulator -= SimScript.TICK_SECONDS
			simulation.tick()
	_sync_presentation()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("pause_menu"):
		toggle_escape_menu()
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey:
		var key := event as InputEventKey
		if key.pressed and not key.echo:
			if key.keycode == KEY_F10:
				diagnostics_visible = not diagnostics_visible
				_refresh_hud()
				get_viewport().set_input_as_handled()
				return
			if key.keycode >= KEY_1 and key.keycode <= KEY_9 and ready_ok:
				var group := int(key.keycode - KEY_0)
				if key.ctrl_pressed:
					_assign_group(group)
				else:
					_recall_group(group)
				get_viewport().set_input_as_handled()
				return
	if not ready_ok or simulation_paused or simulation.winner != -1:
		return
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_WHEEL_UP:
			_nudge_camera_zoom(-1)
			return
		if mouse.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_nudge_camera_zoom(1)
			return
		var world: Variant = _screen_to_world(mouse.position)
		if world == null:
			return
		var point := Vector2((world as Vector3).x, (world as Vector3).z)
		if mouse.button_index == MOUSE_BUTTON_LEFT:
			_handle_left_click(point, mouse.shift_pressed)
		elif mouse.button_index == MOUSE_BUTTON_RIGHT and not simulation.selected_ids.is_empty():
			_handle_right_click(point)


func _handle_left_click(point: Vector2, additive: bool) -> void:
	var player_id := _closest_battalion(point, 0, 6.0)
	if player_id != 0:
		selected_structure_id = 0
		if additive:
			simulation.toggle_selection(player_id)
		else:
			simulation.select_only(player_id)
		hud.set_feedback("Selected %s" % String(simulation.entity(player_id).get("name", "battalion")))
	else:
		var structure_id := _closest_structure(point, 0)
		simulation.clear_selection()
		selected_structure_id = structure_id
		if structure_id != 0:
			hud.set_feedback("Selected %s" % String(simulation.structure(structure_id).get("name", "structure")))
	_sync_presentation()


func _handle_right_click(point: Vector2) -> void:
	var enemy_id := _closest_battalion(point, 1, 6.0)
	if enemy_id == 0:
		enemy_id = _closest_structure(point, 1)
	if enemy_id != 0:
		var accepted := simulation.issue_attack(simulation.selected_ids.duplicate(), enemy_id)
		hud.set_feedback("Attack order accepted." if accepted > 0 else "Attack order rejected.", accepted == 0)
	else:
		var moved := simulation.issue_move(simulation.selected_ids.duplicate(), point)
		hud.set_feedback("Move order plotted on the Palantir." if moved > 0 else "Move rejected: %s." % simulation.last_route_rejection.replace("-", " "), moved == 0)
	_sync_presentation()


func _screen_to_world(screen_position: Vector2) -> Variant:
	if camera == null:
		return null
	var origin := camera.project_ray_origin(screen_position)
	var direction := camera.project_ray_normal(screen_position)
	return Plane(Vector3.UP, 0.35).intersects_ray(origin, direction)


func _closest_battalion(point: Vector2, team: int, maximum_distance: float) -> int:
	var result := 0
	var best_distance := maximum_distance
	for id in simulation.living_ids(team):
		var distance := point.distance_to(Vector2(simulation.entity(id)["position"]))
		if distance <= best_distance:
			best_distance = distance
			result = id
	return result


func _closest_structure(point: Vector2, team: int) -> int:
	var result := 0
	var best_distance := 9.0
	for id in simulation.living_structure_ids(team):
		var row: Dictionary = simulation.structure(id)
		var radius := 8.0 if String(row.get("structure_kind", "")) == "fortress" else 5.5
		var distance := point.distance_to(Vector2(row["position"]))
		if distance <= radius and distance <= best_distance:
			best_distance = distance
			result = id
	return result


func _sync_presentation() -> void:
	if simulation == null:
		return
	for id in simulation.entity_ids():
		if not battalion_nodes.has(id):
			_spawn_battalion(id, validated_battalion_capability, int(gameplay_rules.get("member_count", 15)))
		var entity: Dictionary = simulation.entity(id)
		var battalion: RetailBattalion = battalion_nodes[id]
		var position := Vector2(entity["position"])
		battalion.position = Vector3(position.x, _presentation_height(position), position.y)
		battalion.set_selected(simulation.selected_ids.has(id))
		battalion.set_health(int(entity["health"]), int(entity["maximum_health"]))
		battalion.set_action_state(String(entity["state"]), false, int(entity.get("attack_sequence", -1)))
		var facing := _entity_facing(entity)
		battalion.set_facing_direction(facing)
		var indicator: RetailOrderIndicator = order_indicators[id]
		indicator.sync_from_entity(entity, simulation.selected_ids.has(id), _presentation_height)
	for id in simulation.structure_ids():
		if not structure_nodes.has(id):
			_spawn_structure(id)
		var structure: RetailStructure = structure_nodes[id]
		structure.set_selected(selected_structure_id == id)
		structure.sync_state(simulation.structure(id))
	if audio_system != null:
		audio_system.sync_events(simulation.events)
	_refresh_hud()


func _entity_facing(entity: Dictionary) -> Vector2:
	var position := Vector2(entity.get("position", Vector2.ZERO))
	var target_id := int(entity.get("target_id", 0))
	if target_id != 0:
		var target_kind := String(entity.get("target_kind", "battalion"))
		var target := simulation.structure(target_id) if target_kind == "structure" else simulation.entity(target_id)
		if not target.is_empty():
			return position.direction_to(Vector2(target.get("position", position)))
	var route: Array = entity.get("route", [])
	if not route.is_empty():
		return position.direction_to(Vector2(route[0]))
	return Vector2.RIGHT if int(entity.get("team", 0)) == 0 else Vector2.LEFT


func _refresh_hud() -> void:
	if hud == null or simulation == null:
		return
	hud.set_resources(simulation.resources_for_team(0), simulation.command_points_for_team(0), simulation.command_point_cap)
	hud.set_control_groups(simulation.control_groups)
	if selected_structure_id != 0:
		var structure := simulation.structure(selected_structure_id)
		hud.set_selection("%s  •  %d%%" % [String(structure.get("name", "Structure")), roundi(100.0 * float(structure.get("health", 0)) / float(maxi(1, int(structure.get("maximum_health", 1)))))])
		var can_train := String(structure.get("structure_kind", "")) == "barracks" and int(structure.get("health", 0)) > 0
		var queue_count := Array(structure.get("queue", [])).size()
		hud.set_train_state(can_train, "Train Soldiers  •  %d queued" % queue_count)
	else:
		var names: Array[String] = []
		for id in simulation.selected_ids:
			names.append(String(simulation.entity(id).get("name", str(id))))
		hud.set_selection(", ".join(names) if not names.is_empty() else "No battalion selected")
		hud.set_train_state(false)
	hud.set_objective("DESTROY THE ENEMY FORTRESS" if simulation.winner == -1 else ("VICTORY" if simulation.winner == 0 else "DEFEAT"))
	minimap.camera_center = camera_focus
	var diagnostics := "TICK %05d  HASH %s  MUSIC %s\nBLUE %d  RED %d  ROUTES %d  F10 hides diagnostics" % [
		simulation.tick_index,
		simulation.state_signature(),
		audio_system.current_music_state.to_upper() if audio_system != null else "OFF",
		simulation.living_ids(0).size(),
		simulation.living_ids(1).size(),
		source_map_data.route_query_count if source_map_data != null else 0,
	]
	hud.show_diagnostics(diagnostics, diagnostics_visible)
	if simulation.winner != -1 and simulation.winner != _last_presented_winner:
		_last_presented_winner = simulation.winner
		hud.show_outcome(simulation.winner)


func _assign_group(group: int) -> void:
	var result := simulation.assign_control_group(group, simulation.selected_ids)
	hud.set_feedback("Group %d assigned (%d battalions)." % [group, Array(result.get("entity_ids", [])).size()])
	_refresh_hud()


func _recall_group(group: int) -> void:
	simulation.selected_ids = simulation.recall_control_group(group)
	selected_structure_id = 0
	hud.set_feedback("Group %d recalled." % group)
	_sync_presentation()


func _queue_selected_barracks(unit_id: String) -> void:
	var producer := selected_structure_id
	if producer == 0 or String(simulation.structure(producer).get("structure_kind", "")) != "barracks":
		producer = simulation.producer_id(0, "barracks")
	var result := simulation.queue_unit(0, producer, unit_id)
	hud.set_feedback("Gondor Soldiers added to the queue." if bool(result.get("ok", false)) else "Cannot train: %s." % String(result.get("reason", "rejected")).replace("-", " "), not bool(result.get("ok", false)))
	_refresh_hud()


func toggle_escape_menu() -> void:
	if not ready_ok or simulation.winner != -1:
		return
	simulation_paused = not simulation_paused
	hud.show_pause(simulation_paused)
	hud.set_feedback("Simulation paused." if simulation_paused else "Simulation resumed.")


func reset_match() -> void:
	if simulation == null:
		return
	simulation.setup(source_map_data.simulation_configuration(), gameplay_rules)
	simulation_paused = false
	selected_structure_id = 0
	accumulator = 0.0
	_last_presented_winner = -1
	hud.show_pause(false)
	hud.hide_outcome()
	_spawn_all_presentations(validated_battalion_capability, int(gameplay_rules.get("member_count", 15)))
	if audio_system != null:
		audio_system.intent_log.clear()
		audio_system._next_event_index = 0
		audio_system.current_music_state = ""
		audio_system.sync_events(simulation.events)
	_sync_presentation()
	hud.set_feedback("Battle reset. Select a blue battalion or your Barracks.")


func test_select(id: int) -> bool:
	selected_structure_id = 0
	var accepted := simulation.select_only(id)
	_sync_presentation()
	return accepted


func test_move(destination: Vector2) -> int:
	var accepted := simulation.issue_move(simulation.selected_ids.duplicate(), destination)
	_sync_presentation()
	return accepted


func test_attack(target_id: int) -> int:
	var accepted := simulation.issue_attack(simulation.selected_ids.duplicate(), target_id)
	_sync_presentation()
	return accepted


func step_for_test(ticks: int) -> void:
	simulation.advance(ticks)
	_sync_presentation()


func _presentation_height(position: Vector2) -> float:
	if source_map_data == null or not source_map_data.ready:
		return 0.35
	return source_map_data.local_ground_height(position) + 0.35


func _build_environment() -> void:
	var world_environment := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color("27495b")
	sky_material.sky_horizon_color = Color("789384")
	sky_material.ground_bottom_color = Color("111e22")
	sky_material.ground_horizon_color = Color("1e342b")
	sky_material.sun_angle_max = 18.0
	sky.sky_material = sky_material
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_energy = 0.72
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.adjustment_enabled = true
	environment.adjustment_contrast = 1.08
	environment.adjustment_saturation = 0.92
	environment.fog_enabled = true
	environment.fog_light_color = Color("536f70")
	environment.fog_density = 0.0008
	environment.fog_height = -2.0
	environment.fog_height_density = 0.035
	world_environment.environment = environment
	add_child(world_environment)
	var sunlight := DirectionalLight3D.new()
	sunlight.rotation_degrees = Vector3(-58, -34, 0)
	sunlight.light_color = Color("ffe8bd")
	sunlight.light_energy = 1.28
	sunlight.shadow_enabled = true
	sunlight.directional_shadow_max_distance = 220.0
	add_child(sunlight)
	camera = Camera3D.new()
	camera.fov = 46.0
	add_child(camera)
	_apply_camera_transform()


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	layer.name = "PlayerHudLayer"
	add_child(layer)
	hud = HudScript.new()
	hud_root = hud
	layer.add_child(hud)
	hud.build()
	var viewport := get_viewport()
	if not viewport.size_changed.is_connected(_sync_hud_to_viewport):
		viewport.size_changed.connect(_sync_hud_to_viewport)
	_sync_hud_to_viewport()
	call_deferred("_sync_hud_to_viewport")
	status_label = hud.diagnostics_label
	selection_label = hud.selection_label
	objective_label = hud.objective_label
	feedback_label = hud.feedback_label
	minimap = hud.minimap
	minimap.mouse_filter = Control.MOUSE_FILTER_STOP
	minimap.center_requested.connect(_center_camera_on)
	pause_panel = hud.pause_panel
	failure_panel = hud.failure_panel
	hud.pause_requested.connect(toggle_escape_menu)
	hud.restart_requested.connect(reset_match)
	hud.main_menu_requested.connect(func() -> void: get_tree().change_scene_to_file("res://scenes/boot.tscn"))
	hud.quit_requested.connect(func() -> void: get_tree().quit())
	hud.group_assign_requested.connect(_assign_group)
	hud.group_recall_requested.connect(_recall_group)
	hud.train_requested.connect(_queue_selected_barracks)
	hud.music_volume_changed.connect(func(value: float) -> void:
		if audio_system != null: audio_system.set_music_volume(value, true)
	)
	hud.voice_volume_changed.connect(func(value: float) -> void:
		if audio_system != null: audio_system.set_voice_sfx_volume(value, true)
	)
	hud.mute_changed.connect(func(value: bool) -> void:
		if audio_system != null: audio_system.set_muted(value, true)
	)


func _sync_hud_to_viewport() -> void:
	if hud_root == null or not is_instance_valid(hud_root):
		return
	var viewport_size := get_viewport().get_visible_rect().size
	if viewport_size.x <= 0.0 or viewport_size.y <= 0.0:
		return
	if not hud_root.position.is_equal_approx(Vector2.ZERO) or not hud_root.size.is_equal_approx(viewport_size):
		hud_root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)


func _fail(message: String) -> void:
	failure_reason = message
	ready_ok = false
	hud.show_failure("RETAIL SLICE UNAVAILABLE\n\n%s\n\nNo retail files are stored in the repository." % message)
	hud.set_feedback(message, true)


func _update_camera(delta: float) -> void:
	if camera == null or simulation_paused:
		return
	var movement := Vector2.ZERO
	if Input.is_action_pressed("cam_left"):
		movement.x -= 1.0
	if Input.is_action_pressed("cam_right"):
		movement.x += 1.0
	if Input.is_action_pressed("cam_forward"):
		movement.y -= 1.0
	if Input.is_action_pressed("cam_back"):
		movement.y += 1.0
	if movement.length_squared() > 0.0:
		camera_focus += movement.normalized() * delta * lerpf(22.0, 34.0, camera_zoom)
		_clamp_camera_focus()
	var response := 1.0 - exp(-maxf(delta, 0.0) / camera_zoom_response_seconds)
	camera_zoom = lerpf(camera_zoom, camera_zoom_target, response)
	_apply_camera_transform()


func _nudge_camera_zoom(direction: int) -> void:
	camera_zoom_target = clampf(camera_zoom_target + float(direction) * 0.16, 0.0, 1.0)


func _center_camera_on(world_position: Vector2) -> void:
	camera_focus = world_position
	_clamp_camera_focus()
	_apply_camera_transform()


func _apply_camera_transform() -> void:
	var height := lerpf(24.0, 82.0, camera_zoom)
	var depth := lerpf(20.0, 64.0, camera_zoom)
	camera.position = Vector3(camera_focus.x, height, camera_focus.y + depth)
	camera.look_at(Vector3(camera_focus.x, 0.0, camera_focus.y), Vector3.UP)


func _clamp_camera_focus() -> void:
	if source_map_data == null or not source_map_data.ready:
		camera_focus.x = clampf(camera_focus.x, -50.0, 50.0)
		camera_focus.y = clampf(camera_focus.y, -42.0, 42.0)
		return
	var bounds: Rect2 = source_map_data.local_bounds
	camera_focus.x = clampf(camera_focus.x, bounds.position.x, bounds.end.x)
	camera_focus.y = clampf(camera_focus.y, bounds.position.y, bounds.end.y)


func _clear_presentations() -> void:
	for collection in [battalion_nodes, structure_nodes, order_indicators]:
		for node_value in (collection as Dictionary).values():
			if node_value is Node and is_instance_valid(node_value):
				(node_value as Node).queue_free()
		(collection as Dictionary).clear()


func cleanup_for_test() -> void:
	if audio_system != null:
		audio_system.stop_all()
	_clear_presentations()
	var asset_factory = load("res://src/view/asset_factory.gd")
	asset_factory.clear_mesh_cache()


func _exit_tree() -> void:
	if audio_system != null:
		audio_system.dispose()
	var asset_factory = load("res://src/view/asset_factory.gd")
	asset_factory.clear_mesh_cache()
