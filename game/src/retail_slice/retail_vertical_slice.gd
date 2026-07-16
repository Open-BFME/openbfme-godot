class_name RetailVerticalSlice
extends Node3D
## Integrated private Men-versus-Men production slice. Authoritative gameplay is
## held by RetailSliceSim; Godot nodes interpolate and present that state only.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const BattalionScript = preload("res://src/retail_slice/retail_battalion.gd")
const StructureScript = preload("res://src/retail_slice/retail_structure.gd")
const OrderIndicatorScript = preload("res://src/retail_slice/retail_order_indicator.gd")
const AttackTargetIndicatorScript = preload("res://src/retail_slice/retail_attack_target_indicator.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const MapDataScript = preload("res://src/retail_slice/retail_map_data.gd")
const BattlefieldScript = preload("res://src/retail_slice/retail_fords_battlefield.gd")
const HudScript = preload("res://src/retail_slice/retail_hud.gd")
const LinearFogScript = preload("res://src/retail_slice/fords_linear_fog.gd")
const MemberHealthOverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"
const BUILDER_OBJECT_ID := "bfme2.object.men-porter"
const MAP_ID := "bfme2.map.fords-of-isen-ii"
const UNIT_OBJECT_IDS: Array[String] = [
	"bfme2.object.gondor-fighter",
	"bfme2.object.gondor-archer",
	"bfme2.object.gondor-tower-guard",
	"bfme2.object.gondor-knight",
]
const UNIT_MODEL_PATHS := {
	"bfme2.object.gondor-fighter": "assets/models/units/gondor-fighter.glb",
	"bfme2.object.gondor-archer": "assets/models/units/gondor-archer.glb",
	"bfme2.object.gondor-tower-guard": "assets/models/units/gondor-tower-guard.glb",
	"bfme2.object.gondor-knight": "assets/models/units/gondor-knight.glb",
	"bfme2.object.men-porter": "assets/models/units/men-porter.glb",
}
const PRESENTATION_UNIT_OBJECT_IDS: Array[String] = [
	"bfme2.object.gondor-fighter",
	"bfme2.object.gondor-archer",
	"bfme2.object.gondor-tower-guard",
	"bfme2.object.gondor-knight",
	BUILDER_OBJECT_ID,
]
const UNIT_QUEUE_NAMES := {
	"bfme2.object.gondor-fighter-horde": "Gondor Soldiers",
	"bfme2.object.gondor-tower-guard": "Gondor Tower Guards",
	"bfme2.object.gondor-archer": "Gondor Archers",
	"bfme2.object.gondor-knight": "Gondor Knights",
}
const BUILDING_OBJECT_IDS := {
	"fortress": "bfme2.object.men-fortress",
	"farm": "bfme2.object.men-farm",
	"barracks": "bfme2.object.men-barracks",
	"archery_range": "bfme2.object.men-archery-range",
	"stable": "bfme2.object.men-stable",
}
const FORDS_ENVIRONMENT_ORACLE_SHA256 := "c1f300fcf6fed6f225d1b04f50b14fab04883641d8b5b36762be6cfcb58e9a59"
const FORDS_ACTIVE_TIME_OF_DAY := "AFTERNOON"
const FORDS_ACTIVE_WEATHER := "NORMAL"
const FORDS_SKYBOX_MODEL_SOURCE := "art/w3d/ne/new_skybox.w3d"
const FORDS_SKYBOX_TEXTURE_SET_SOURCE := "data/ini/environment.ini"
const FORDS_SKYBOX_STATUS := "texture-set-selection-and-current-pack-conversion-unresolved-neutral-black-background"
const FORDS_WATER_REFLECTION_SOURCE_LEAF := "SkyEnv.tga"
const FORDS_WATER_REFLECTION_STATUS := "unresolved-in-effective-tree-and-current-pack-profile"
const FORDS_FOG_COLOR := Color(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0, 1.0)
const FORDS_FOG_START_SOURCE := 350.0
const FORDS_FOG_END_SOURCE := 2000.0
const FORDS_CAMERA_MIN_HEIGHT_SOURCE := 120.0
const FORDS_CAMERA_MAX_HEIGHT_SOURCE := 300.0
const FORDS_CAMERA_PITCH_ABOVE_HORIZONTAL_DEGREES := 37.5
const FORDS_CAMERA_YAW_DEGREES := 0.0
const FORDS_CAMERA_SCROLL_SPEED_SCALAR := 1.0
const FORDS_CAMERA_GROUND_MIN_SOURCE := 260.0
const FORDS_CAMERA_GROUND_MAX_SOURCE := 380.0
const FORDS_CAMERA_NAMED_FOV_RADIANS := 0.8726646304130554
const FORDS_CAMERA_ZOOM_STEP_SOURCE := 10.0
const OPENBFME_KEYBOARD_SCROLL_BASE_LOCAL_PER_SECOND := 28.0
const TERRAIN_LIGHT_LAYER := 1 << 1
const OBJECT_LIGHT_LAYER := 1 << 2
const INFANTRY_LIGHT_LAYER := 1 << 3
const FORDS_AFTERNOON_LIGHT_RIGS := {
	"terrain": [
		{"name": "sun", "ambient": [0.04313725605607033, 0.03529411926865578, 0.04313725605607033], "color": [0.6235294342041016, 0.49803921580314636, 0.45098039507865906], "direction": [0.2649596631526947, 0.4240240752696991, -0.8660253882408142]},
		{"name": "accent1", "ambient": [0.0, 0.0, 0.0], "color": [0.2235294133424759, 0.3333333432674408, 0.29019609093666077], "direction": [0.5065234899520874, -0.6036512851715088, -0.6156614422798157]},
		{"name": "accent2", "ambient": [0.04313725605607033, 0.03529411926865578, 0.04313725605607033], "color": [0.6235294342041016, 0.49803921580314636, 0.45098039507865906], "direction": [0.2649596631526947, 0.4240240752696991, -0.8660253882408142]},
	],
	"object": [
		{"name": "sun", "ambient": [0.04313725605607033, 0.03529411926865578, 0.04313725605607033], "color": [0.6235294342041016, 0.49803921580314636, 0.45098039507865906], "direction": [0.2649596631526947, 0.4240240752696991, -0.8660253882408142]},
		{"name": "accent1", "ambient": [0.0, 0.0, 0.0], "color": [0.1725490242242813, 0.30588236451148987, 0.38823530077934265], "direction": [-0.7745234966278076, -0.5423271059989929, -0.32556822896003723]},
		{"name": "accent2", "ambient": [0.0, 0.0, 0.0], "color": [0.1725490242242813, 0.30588236451148987, 0.38823530077934265], "direction": [-0.7745234966278076, -0.5423271059989929, -0.32556822896003723]},
	],
	"infantry": [
		{"name": "sun", "ambient": [0.0, 0.0, 0.0], "color": [0.1725490242242813, 0.30588236451148987, 0.38823530077934265], "direction": [-0.7745234966278076, -0.5423271059989929, -0.32556822896003723]},
		{"name": "accent1", "ambient": [0.0, 0.0, 0.0], "color": [0.2235294133424759, 0.3333333432674408, 0.29019609093666077], "direction": [0.5065234899520874, -0.6036512851715088, -0.6156614422798157]},
		{"name": "accent2", "ambient": [0.0, 0.0, 0.0], "color": [0.2235294133424759, 0.3333333432674408, 0.29019609093666077], "direction": [0.5065234899520874, -0.6036512851715088, -0.6156614422798157]},
	],
}

var simulation: RetailSliceSim
var battalion_nodes: Dictionary = {}
var structure_nodes: Dictionary = {}
var order_indicators: Dictionary = {}
var attack_target_indicator: RetailAttackTargetIndicator
var audio_system: RetailSliceAudio
var source_map_data: RetailMapData
var selected_pack_root := ""
var gameplay_rules: Dictionary = {}
var validated_battalion_capabilities: Dictionary = {}
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
var camera_zoom := 1.0
var camera_zoom_target := 1.0
var world_environment: WorldEnvironment
var linear_fog: FordsLinearFog
var source_environment_lights: Array[DirectionalLight3D] = []
var environment_runtime_metadata: Dictionary = {}
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
var member_health_overlay: Control
var selected_structure_id := 0
var structure_lifecycle_route_sequence := 0
var diagnostics_visible := false
var _preview_texture: Texture2D
var _source_art_texture: Texture2D
var _last_presented_winner := -1
var attack_move_armed := false
var construction_kind_armed := ""
var construction_ghost: MeshInstance3D = null
var _drag_select_origin := Vector2.INF
var _drag_selecting := false
var _selection_band: Control = null
const DRAG_SELECT_THRESHOLD := 8.0
var camera_user_yaw := 0.0
var _camera_orbiting := false
var power_cast_armed := ""
# Env-gated presentation profiler (OPENBFME_PROFILE_SYNC=1): accumulates
# per-section time so soak runs can attribute frame-cost growth exactly.
var _profile_sync := OS.get_environment("OPENBFME_PROFILE_SYNC") == "1"
var _profile_mark := 0
var presentation_profile: Dictionary = {}
var initialization_metrics_ms: Dictionary = {}
var _initialization_started_ms := 0
var _initialization_last_ms := 0


func _ready() -> void:
	_initialization_started_ms = Time.get_ticks_msec()
	_initialization_last_ms = _initialization_started_ms
	if DisplayServer.get_name() != "headless":
		# Windowed runs load phase-by-phase behind a progress bar; headless
		# runners keep the original single-pass initialization.
		_build_loading_overlay()
	await _mark_initialization_phase("ready")
	_build_environment()
	await _mark_initialization_phase("environment")
	_build_hud()
	await _mark_initialization_phase("hud")
	_initialize_content_and_match()


func _initialize_content_and_match() -> void:
	if not ContentDB.bundle_objects.has(SOLDIER_OBJECT_ID):
		ContentDB.reload()
	await _mark_initialization_phase("content")
	var member_definition := ContentDB.get_bundle_object(SOLDIER_OBJECT_ID)
	var horde_definition := ContentDB.get_bundle_object(SOLDIER_HORDE_ID)
	var soldier_capability_id := String(member_definition.get("animationCapabilityId", ""))
	var soldier_capability := ContentDB.get_animation_capability(soldier_capability_id)
	var map_definition := ContentDB.get_bundle_map(MAP_ID)
	if member_definition.is_empty() or horde_definition.is_empty() or soldier_capability.is_empty() or map_definition.is_empty():
		_fail("The private bfme2-men-vslice pack is not selected. Run run_importer.bat to build and select it.")
		return
	selected_pack_root = String(member_definition.get("_pack_root", ""))
	if selected_pack_root == "" or String((ModLoader._read_json(selected_pack_root.path_join("pack.json")) as Dictionary).get("id", "")) != "bfme2-men-vslice":
		_fail("The selected content pack is not bfme2-men-vslice.")
		return
	var presentation_definition_error := _load_required_presentation_definitions()
	if presentation_definition_error != "":
		_fail("Private Men roster presentation validation failed: %s" % presentation_definition_error)
		return
	var hud_binding_error := hud.bind_retail_train_commands(ContentDB, selected_pack_root, true)
	if hud_binding_error != "":
		_fail("Private Men production UI validation failed: %s" % hud_binding_error)
		return
	await _mark_initialization_phase("retail_command_ui")
	attack_target_indicator = AttackTargetIndicatorScript.new()
	attack_target_indicator.name = "AttackTargetIndicator"
	add_child(attack_target_indicator)
	attack_target_indicator.configure(hud.retail_action_texture("attack_move"))

	var asset_factory = load("res://src/view/asset_factory.gd")
	var preview_path := ContentDB.resolve_asset(String(map_definition.get("preview", "")), selected_pack_root)
	var art_path := ContentDB.resolve_asset(String(map_definition.get("art", "")), selected_pack_root)
	_preview_texture = asset_factory.load_texture_asset(preview_path)
	_source_art_texture = asset_factory.load_texture_asset(art_path)
	map_preview_loaded = _preview_texture != null
	map_art_loaded = _source_art_texture != null
	await _mark_initialization_phase("map_art")

	source_map_data = MapDataScript.new()
	if not source_map_data.load_from_pack(selected_pack_root, map_definition):
		_fail("Cooked Fords map data failed validation: %s" % source_map_data.error)
		return
	var environment_error := _configure_source_environment()
	if environment_error != "":
		_fail("Fords source environment failed validation: %s" % environment_error)
		return
	await _mark_initialization_phase("map_data")
	battlefield = BattlefieldScript.new()
	battlefield.name = "CookedSourceFordsBattlefield"
	add_child(battlefield)
	if not battlefield.configure(source_map_data):
		_fail("Cooked Fords data was valid, but source geometry could not be built: %s" % String(battlefield.error))
		return
	_assign_battlefield_lighting_domains()
	source_driven_terrain = battlefield.source_driven
	crossing_count = battlefield.ford_marker_count
	await _mark_initialization_phase("battlefield")

	gameplay_rules = _gameplay_rules(member_definition, horde_definition)
	if gameplay_rules.has("_error"):
		_fail("Retail unit gameplay rules failed validation: %s" % String(gameplay_rules["_error"]))
		return
	simulation = SimScript.new()
	simulation.setup(source_map_data.simulation_configuration(), gameplay_rules)
	var player_fortress_id := simulation.fortress_id(0)
	if player_fortress_id != 0:
		var player_fortress_position := Vector2(simulation.structure(player_fortress_id).get("position", Vector2.ZERO))
		camera_focus = player_fortress_position
		_clamp_camera_focus()
		_apply_camera_transform()
	await _mark_initialization_phase("simulation")
	_spawn_all_presentations(int(horde_definition.get("memberCount", 15)))
	await _mark_initialization_phase("presentations")

	audio_system = AudioScript.new()
	add_child(audio_system)
	audio_system.configure(selected_pack_root, DisplayServer.get_name() != "headless")
	audio_system.set_declared_structure_lifecycle_audio_active(_all_men_structure_contracts_v1())
	await _mark_initialization_phase("audio")
	hud.configure_minimap(simulation, source_map_data, camera, _preview_texture)
	var command_costs: Dictionary = {}
	for unit_type in SimScript.UNIT_PRODUCTION_RULES.keys():
		command_costs[unit_type] = simulation._production_rule_value(String(unit_type), "cost_rule", "default_cost")
	for structure_kind in SimScript.STRUCTURE_BUILD_RULES.keys():
		command_costs[structure_kind] = int((SimScript.STRUCTURE_BUILD_RULES[structure_kind] as Dictionary).get("cost", 0))
	hud.set_command_costs(command_costs)
	hud.apply_audio_values(audio_system.get_music_volume(), audio_system.get_voice_sfx_volume(), audio_system.is_muted())
	audio_system.sync_events(simulation.events)
	ready_ok = (
		battalion_nodes.size() == simulation.initial_battalion_count()
		and _all_battalion_retail_visuals_loaded()
		and structure_nodes.size() == 10
		and _all_structure_retail_visuals_loaded()
		and map_preview_loaded
		and map_art_loaded
		and equipment_proof_loaded
		and source_map_data.ready
		and source_driven_terrain
		and simulation.source_map_configured
		and simulation.base_loop_enabled
		and audio_system.has_complete_roster_audio_closure()
		and hud.retail_train_commands_bound
	)
	if not ready_ok:
		var failed_capabilities: Array[String] = []
		if battalion_nodes.size() != simulation.initial_battalion_count():
			failed_capabilities.append("battalion_count=%d expected=%d" % [battalion_nodes.size(), simulation.initial_battalion_count()])
		if not _all_battalion_retail_visuals_loaded():
			failed_capabilities.append("battalion_retail_visuals")
		if structure_nodes.size() != 10:
			failed_capabilities.append("structure_count=%d expected=10" % structure_nodes.size())
		if not _all_structure_retail_visuals_loaded():
			failed_capabilities.append("structure_retail_visuals")
		if not map_preview_loaded:
			failed_capabilities.append("map_preview")
		if not map_art_loaded:
			failed_capabilities.append("map_art")
		if not equipment_proof_loaded:
			failed_capabilities.append("equipment_proof")
		if not source_map_data.ready:
			failed_capabilities.append("source_map_data")
		if not source_driven_terrain:
			failed_capabilities.append("source_driven_terrain")
		if not simulation.source_map_configured:
			failed_capabilities.append("simulation_source_map")
		if not simulation.base_loop_enabled:
			failed_capabilities.append("simulation_base_loop")
		if not audio_system.has_complete_roster_audio_closure():
			failed_capabilities.append("roster_audio_closure")
		if not hud.retail_train_commands_bound:
			failed_capabilities.append("hud_train_commands")
		_fail("Retail pack mounted, but capability validation failed: %s" % ", ".join(failed_capabilities))
		return
	hud.set_feedback("Select a blue battalion to move, or select a production building to train units.")
	_sync_presentation()
	await _mark_initialization_phase("ready_complete")
	if OS.get_environment("OPENBFME_UI_PROBE") == "1":
		_run_ui_probe()


# Env-gated live-GUI diagnostic (OPENBFME_UI_PROBE=1): reproduces the reported
# side-bar and cancel-training flows in a real window using synthesized OS-path
# input events, printing what the GUI actually does. Headless --script runs
# cannot hit-test layered GUI, so this is the only automated way to observe it.
func _run_ui_probe() -> void:
	_probe_log(["probe armed, waiting 2s"])
	await get_tree().create_timer(2.0).timeout
	hud.cancel_production_requested.connect(func(index: int) -> void:
		_probe_log(["SIGNAL cancel_production_requested index=", index])
	)
	var builder_id := 0
	for id in simulation.entities:
		var row: Dictionary = simulation.entities[id]
		if bool(row.get("is_builder", false)) and int(row.get("team", -1)) == 0:
			builder_id = id
			break
	_probe_log(["builder_id=", builder_id])
	selected_structure_id = 0
	simulation.select_only(builder_id)
	_sync_presentation()
	for i in 12:
		await get_tree().process_frame
	var bar := hud.retail_side_command_bar
	_probe_log(["bar visible=", bar.visible, " alpha=", bar.modulate.a, " shown=", bar.builder_bar_shown(), " buttons=", bar.side_buttons().size()])
	for button in bar.side_buttons():
		_probe_log(["side btn ", button.name, " icon=", button.icon != null, " rect=", button.get_global_rect(), " disabled=", button.disabled])
	await get_tree().create_timer(1.5).timeout
	_probe_log(["after 1.5s: bar visible=", bar.visible, " alpha=", bar.modulate.a, " shown=", bar.builder_bar_shown()])
	if not bar.side_buttons().is_empty():
		var target: Vector2 = bar.side_buttons()[0].get_global_rect().get_center()
		_probe_log(["clicking side button 0 at ", target, " hovered_before=", await _probe_hovered(target)])
		await _probe_click(target)
	# Cancel-training flow: select the barracks, queue two units via real GUI
	# clicks on the train socket, then click the blue cancel button and a queue chip.
	var barracks := 0
	for id in simulation.structures:
		var structure_row: Dictionary = simulation.structures[id]
		if int(structure_row.get("team", -1)) == 0 and not Array(structure_row.get("production", [])).is_empty():
			barracks = id
			break
	_probe_log(["barracks=", barracks])
	simulation.clear_selection()
	selected_structure_id = barracks
	_sync_presentation()
	for i in 6:
		await get_tree().process_frame
	var train_button: Button = null
	for button_value in hud.train_buttons.values():
		if (button_value as Button).visible and not (button_value as Button).disabled:
			train_button = button_value
			break
	if train_button != null:
		var train_point: Vector2 = train_button.get_global_rect().get_center()
		_probe_controls_at(train_point, "train")
		_probe_log(["clicking train at ", train_point, " hovered=", await _probe_hovered(train_point)])
		await _probe_click(train_point)
		await _probe_click(train_point)
	for i in 6:
		await get_tree().process_frame
	var queue_size := Array((simulation.structures[barracks] as Dictionary).get("queue", [])).size()
	_probe_log(["queue after training clicks=", queue_size])
	if queue_size == 0 and train_button != null:
		# GUI clicks did not land; queue via the signal path so the cancel
		# affordances become visible for the overlap scan below.
		var probe_unit_id := String(hud.train_buttons.find_key(train_button))
		_queue_selected_producer(probe_unit_id)
		_queue_selected_producer(probe_unit_id)
		_refresh_hud()
		for i in 6:
			await get_tree().process_frame
		_probe_log(["queue after direct queue=", Array((simulation.structures[barracks] as Dictionary).get("queue", [])).size()])
	var cancel_button: Button = hud.cancel_production_button
	_probe_log(["cancel btn visible=", cancel_button.visible, " disabled=", cancel_button.disabled, " rect=", cancel_button.get_global_rect()])
	var cancel_point: Vector2 = cancel_button.get_global_rect().get_center()
	_probe_controls_at(cancel_point, "cancel")
	_probe_log(["clicking cancel at ", cancel_point, " hovered=", await _probe_hovered(cancel_point)])
	await _probe_click(cancel_point)
	for i in 6:
		await get_tree().process_frame
	_probe_log(["queue after cancel click=", Array((simulation.structures[barracks] as Dictionary).get("queue", [])).size()])
	for chip in hud.production_queue_buttons:
		if (chip as Button).visible:
			var chip_point: Vector2 = (chip as Button).get_global_rect().get_center()
			_probe_controls_at(chip_point, "chip")
			_probe_log(["clicking queue chip at ", chip_point, " hovered=", await _probe_hovered(chip_point)])
			await _probe_click(chip_point)
			break
	for i in 6:
		await get_tree().process_frame
	_probe_log(["queue after chip click=", Array((simulation.structures[barracks] as Dictionary).get("queue", [])).size()])
	_probe_log(["DONE"])
	await get_tree().process_frame
	get_tree().quit()


## Lists every visible, input-receiving Control whose global rect contains the
## point, in the reverse-tree order the GUI would consider them (later siblings
## first) — the first entry is what a click would hit.
func _probe_controls_at(point: Vector2, tag: String) -> void:
	var hits: Array = []
	_probe_scan_control(get_viewport().gui_get_canvas_root() if get_viewport().has_method("gui_get_canvas_root") else get_tree().root, point, hits)
	_probe_log(["controls under ", tag, " point ", point, ":"])
	for hit in hits:
		_probe_log(["   ", hit])


func _probe_scan_control(node: Node, point: Vector2, hits: Array) -> void:
	# Children in reverse order first: matches GUI input picking priority.
	for index in range(node.get_child_count() - 1, -1, -1):
		_probe_scan_control(node.get_child(index), point, hits)
	var control := node as Control
	if control == null or not control.is_visible_in_tree():
		return
	if control.mouse_filter == Control.MOUSE_FILTER_IGNORE:
		return
	if control.get_global_rect().has_point(point):
		hits.append("%s filter=%d z=%d rect=%s" % [control.get_path(), control.mouse_filter, control.z_index, control.get_global_rect()])


var _probe_log_file: FileAccess = null


func _probe_log(parts: Array) -> void:
	var msg := "[probe] "
	for part in parts:
		msg += str(part)
	print(msg)
	if _probe_log_file == null:
		_probe_log_file = FileAccess.open("user://ui_probe.log", FileAccess.WRITE)
	if _probe_log_file != null:
		_probe_log_file.store_line(msg)
		_probe_log_file.flush()


func _probe_hovered(point: Vector2) -> String:
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	Input.parse_input_event(motion)
	await get_tree().process_frame
	var hovered := get_viewport().gui_get_hovered_control()
	return str(get_path_to(hovered)) if hovered != null else "<none>"


func _probe_click(point: Vector2) -> void:
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = point
		click.global_position = point
		Input.parse_input_event(click)
		await get_tree().process_frame


# Measured phase costs (ms) weight the loading bar so it advances honestly
# rather than in even steps.
const LOADING_PHASE_WEIGHTS := {
	"ready": 1, "environment": 16, "hud": 51, "content": 12,
	"retail_command_ui": 214, "map_art": 13, "map_data": 1293,
	"battlefield": 2507, "simulation": 12, "presentations": 962,
	"audio": 488, "ready_complete": 1,
}
var _loading_overlay: CanvasLayer = null
var _loading_bar: ProgressBar = null
var _loading_phase_label: Label = null
var _loading_weight_done := 0.0


func _build_loading_overlay() -> void:
	_loading_overlay = CanvasLayer.new()
	_loading_overlay.name = "LoadingOverlay"
	_loading_overlay.layer = 50
	add_child(_loading_overlay)
	var backdrop := ColorRect.new()
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.01, 0.015, 0.02)
	_loading_overlay.add_child(backdrop)
	var column := VBoxContainer.new()
	column.set_anchors_preset(Control.PRESET_CENTER)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 18)
	backdrop.add_child(column)
	var title := Label.new()
	title.text = "FORDS OF ISEN II"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 44)
	title.add_theme_color_override("font_color", Color("e9d489"))
	column.add_child(title)
	_loading_bar = ProgressBar.new()
	_loading_bar.custom_minimum_size = Vector2(560, 22)
	_loading_bar.min_value = 0.0
	_loading_bar.max_value = 100.0
	_loading_bar.show_percentage = true
	var bar_bg := StyleBoxFlat.new()
	bar_bg.bg_color = Color(0.05, 0.06, 0.05)
	bar_bg.border_color = Color(0.55, 0.48, 0.28)
	bar_bg.set_border_width_all(1)
	var bar_fill := StyleBoxFlat.new()
	bar_fill.bg_color = Color(0.72, 0.6, 0.28)
	_loading_bar.add_theme_stylebox_override("background", bar_bg)
	_loading_bar.add_theme_stylebox_override("fill", bar_fill)
	column.add_child(_loading_bar)
	_loading_phase_label = Label.new()
	_loading_phase_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_loading_phase_label.add_theme_font_size_override("font_size", 15)
	_loading_phase_label.add_theme_color_override("font_color", Color("9fb2a1"))
	column.add_child(_loading_phase_label)


func _mark_initialization_phase(phase: String) -> void:
	var now := Time.get_ticks_msec()
	initialization_metrics_ms[phase] = now - _initialization_started_ms
	if DisplayServer.get_name() == "headless":
		print("RETAIL_INIT_PHASE name=%s delta_ms=%d total_ms=%d" % [phase, now - _initialization_last_ms, now - _initialization_started_ms])
	_initialization_last_ms = now
	if _loading_overlay == null:
		return
	_loading_weight_done += float(LOADING_PHASE_WEIGHTS.get(phase, 10))
	var total := 0.0
	for weight in LOADING_PHASE_WEIGHTS.values():
		total += float(weight)
	_loading_bar.value = clampf(_loading_weight_done / total * 100.0, 0.0, 100.0)
	_loading_phase_label.text = "Loading %s..." % phase.replace("_", " ")
	if phase == "ready_complete":
		var overlay := _loading_overlay
		_loading_overlay = null
		var fade := create_tween()
		fade.tween_interval(0.2)
		fade.tween_callback(func() -> void: overlay.queue_free())
	else:
		# Yield one frame so the bar actually renders between phases.
		await get_tree().process_frame


func _load_required_presentation_definitions() -> String:
	validated_battalion_capabilities.clear()
	equipment_proof_loaded = false
	for object_id in PRESENTATION_UNIT_OBJECT_IDS:
		var expected_kind := "builder" if object_id == BUILDER_OBJECT_ID else "member"
		var model_error := _validate_retail_object_model(object_id, expected_kind, String(UNIT_MODEL_PATHS[object_id]))
		if model_error != "":
			return model_error
		var definition: Dictionary = ContentDB.get_bundle_object(object_id)
		var capability_id := String(definition.get("animationCapabilityId", ""))
		var capability: Dictionary = ContentDB.get_animation_capability(capability_id)
		if capability_id == "" or capability.is_empty() or String(capability.get("_pack_root", "")) != selected_pack_root:
			return "%s has no selected-pack animation capability" % object_id
		validated_battalion_capabilities[object_id] = _attach_equipment_proof(capability) if object_id == SOLDIER_OBJECT_ID else capability.duplicate(true)
	for kind in ["fortress", "farm", "barracks", "archery_range", "stable"]:
		var structure_object_id := String(BUILDING_OBJECT_IDS[kind])
		var lifecycle_error := _validate_retail_structure_lifecycle(structure_object_id, kind)
		if lifecycle_error != "":
			return lifecycle_error
	return ""


func _validate_retail_object_model(object_id: String, expected_kind: String, expected_model: String) -> String:
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	if definition.is_empty() or String(definition.get("_pack_root", "")) != selected_pack_root:
		return "%s is not registered by the selected pack" % object_id
	if String(definition.get("kind", "")) != expected_kind:
		return "%s has kind %s instead of %s" % [object_id, String(definition.get("kind", "")), expected_kind]
	var presentation: Dictionary = definition.get("presentation", {}) as Dictionary
	var declared_model := String(presentation.get("model", "")).replace("\\", "/")
	if declared_model != expected_model:
		return "%s declares %s instead of %s" % [object_id, declared_model, expected_model]
	var resolved_model := ContentDB.resolve_mesh_path(definition)
	if resolved_model == "" or not FileAccess.file_exists(resolved_model):
		return "%s retail GLB is missing" % object_id
	return ""


func _validate_retail_structure_lifecycle(object_id: String, structure_kind: String) -> String:
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	if definition.is_empty() or String(definition.get("_pack_root", "")) != selected_pack_root:
		return "%s is not registered by the selected pack" % object_id
	if String(definition.get("kind", "")) != "structure":
		return "%s is not a structure object" % object_id
	if typeof(definition.get("presentation")) != TYPE_DICTIONARY:
		return "%s presentation is not an object" % object_id
	var presentation: Dictionary = definition["presentation"]
	if typeof(presentation.get("buildingLifecycle")) != TYPE_DICTIONARY:
		return "%s has no buildingLifecycle presentation" % object_id
	var lifecycle: Dictionary = presentation["buildingLifecycle"]
	var contract_error: String = StructureScript.validate_lifecycle_contract(
		lifecycle,
		structure_kind,
		String(presentation.get("model", "")),
		int(SimScript.STRUCTURE_MAX_HEALTH.get(structure_kind, 0))
	)
	if contract_error != "":
		return "%s lifecycle contract failed: %s" % [object_id, contract_error]
	var asset_error: String = StructureScript.preflight_lifecycle_assets(
		lifecycle,
		structure_kind,
		selected_pack_root
	)
	if asset_error != "":
		return "%s lifecycle assets failed: %s" % [object_id, asset_error]
	return ""


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
	var unit_rules: Dictionary = {}
	for object_id in UNIT_OBJECT_IDS:
		var source_rules := ContentDB.get_retail_unit_rules(object_id)
		var converted := _convert_retail_unit_rule(source_rules, tick_ms)
		if converted.has("_error"):
			return {"_error": "%s: %s" % [object_id, String(converted["_error"])]}
		unit_rules[object_id] = converted
	var builder_definition := ContentDB.get_bundle_object(BUILDER_OBJECT_ID)
	var builder_simulation: Dictionary = builder_definition.get("simulation", {}) as Dictionary
	if builder_definition.is_empty() or builder_simulation.is_empty():
		return {"_error": "missing selected-pack MenPorter simulation contract"}
	unit_rules[BUILDER_OBJECT_ID] = {
		"horde_id": BUILDER_OBJECT_ID,
		"member_count": 1,
		"member_health": maxi(1, int(builder_simulation.get("health", 500))),
		"member_damage": 1,
		"speed": float(builder_simulation.get("speed", 60.0)) * source_map_data.local_transform_scale,
		"speed_source": float(builder_simulation.get("speed", 60.0)),
		"acceleration": 60.0 * source_map_data.local_transform_scale,
		"acceleration_source": 60.0,
		"turn_rate_degrees_per_second": 360.0,
		"braking": 60.0 * source_map_data.local_transform_scale,
		"braking_source": 60.0,
		"attack_range": 0.0,
		"attack_range_source": 0.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": float(builder_simulation.get("vision", 25.0)) * source_map_data.local_transform_scale,
		"vision_range_source": float(builder_simulation.get("vision", 25.0)),
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"formation_positions": [Vector3.ZERO],
		"stances": {"default": "Battle", "cycleOrder": ["HoldGround", "Battle", "Aggressive"], "states": {"HoldGround": {}, "Battle": {}, "Aggressive": {}}},
		"is_builder": true,
		"provenance": {"source": "data/ini/object/goodfaction/units/men/porter.ini", "constants": "data/ini/gamedata.ini"},
	}
	return {
		"enable_base_loop": true,
		"starting_resources": 1200,
		"command_point_cap": 200,
		"member_health": maxi(1, int(member.get("health", 200))),
		"member_count": member_count,
		"unit_rules": unit_rules,
		"soldier_cost": maxi(0, int(member.get("cost", 200))),
		"soldier_build_ticks": maxi(1, roundi(float(member.get("buildTimeSeconds", 20)) / SimScript.TICK_SECONDS)),
		"soldier_command_points": maxi(1, int(horde_definition.get("commandPoints", 60))),
		"farm_income": 25,
		"farm_payout_ticks": 50,
		"maximum_queue": 5,
		"ai_queue_interval_ticks": 60,
		"ai_attack_delay_ticks": 300,
	}


func _convert_retail_unit_rule(source_rules: Dictionary, tick_ms: float) -> Dictionary:
	if source_rules.is_empty() or tick_ms <= 0.0 or source_map_data == null or source_map_data.local_transform_scale <= 0.0:
		return {"_error": "missing selected-pack rule or map scale"}
	var member: Dictionary = source_rules.get("member", {}) as Dictionary
	var horde: Dictionary = source_rules.get("horde", {}) as Dictionary
	var horde_locomotor_set: Dictionary = horde.get("locomotorSet", {}) as Dictionary
	var horde_locomotor: Dictionary = horde.get("locomotor", {}) as Dictionary
	var weapon: Dictionary = member.get("weapon", {}) as Dictionary
	var weapon_sets: Array = member.get("weaponSets", []) as Array
	var formation: Dictionary = horde.get("formation", {}) as Dictionary
	var stances: Dictionary = horde.get("stances", {}) as Dictionary
	var horde_vision := _retail_rule_number(horde.get("visionRange"))
	var speed_raw := _retail_rule_number(horde_locomotor_set.get("speed"))
	var acceleration_raw := _retail_rule_number(horde_locomotor.get("acceleration"))
	var turn_rate_raw := _retail_rule_number(horde_locomotor.get("turnRateDegreesPerSecond"))
	var braking_raw := _retail_rule_number(horde_locomotor.get("braking"))
	var attack_range_raw := _retail_rule_number(weapon.get("attackRange"))
	var delay_ms := _retail_rule_number(weapon.get("delayBetweenShotsMs"))
	var pre_attack_ms := _retail_rule_number(weapon.get("preAttackDelayMs"))
	var firing_duration_ms := _retail_rule_number(weapon.get("firingDurationMs"))
	var damage := _retail_rule_number(weapon.get("damage"))
	for value in [speed_raw, acceleration_raw, turn_rate_raw, braking_raw, attack_range_raw, horde_vision, delay_ms, pre_attack_ms, firing_duration_ms, damage]:
		if not is_finite(float(value)) or float(value) < 0.0:
			return {"_error": "non-finite or negative retail numeric field"}
	var minimum_range_raw := 0.0
	var minimum_value: Variant = weapon.get("minimumAttackRange", {})
	if typeof(minimum_value) == TYPE_DICTIONARY and bool((minimum_value as Dictionary).get("defined", true)):
		minimum_range_raw = _retail_rule_number(minimum_value)
		if not is_finite(minimum_range_raw) or minimum_range_raw < 0.0:
			return {"_error": "invalid minimum attack range"}
	var member_count_value := int(formation.get("memberCount", 0))
	var ranks_value: Variant = formation.get("ranks", [])
	if member_count_value <= 0 or typeof(ranks_value) != TYPE_ARRAY:
		return {"_error": "missing retail formation"}
	var source_positions: Array[Vector2] = []
	for rank_value in ranks_value as Array:
		if typeof(rank_value) != TYPE_DICTIONARY:
			return {"_error": "invalid retail formation rank"}
		var positions_value: Variant = (rank_value as Dictionary).get("positions", [])
		if typeof(positions_value) != TYPE_ARRAY:
			return {"_error": "invalid retail formation positions"}
		for position_value in positions_value as Array:
			if typeof(position_value) != TYPE_DICTIONARY:
				return {"_error": "invalid retail formation position"}
			var source_x := float((position_value as Dictionary).get("x", NAN))
			var source_y := float((position_value as Dictionary).get("y", NAN))
			if not is_finite(source_x) or not is_finite(source_y):
				return {"_error": "non-finite retail formation position"}
			source_positions.append(Vector2(source_x, source_y))
	if source_positions.size() != member_count_value:
		return {"_error": "retail formation member count mismatch"}
	var stance_states: Dictionary = stances.get("states", {}) as Dictionary
	var stance_order: Array = stances.get("cycleOrder", []) as Array
	if stance_states.size() != 3 or stance_order != ["HoldGround", "Battle", "Aggressive"]:
		return {"_error": "missing retail three-stance contract"}
	var center := Vector2.ZERO
	for position in source_positions:
		center += position
	center /= float(source_positions.size())
	var formation_positions: Array[Vector3] = []
	for position in source_positions:
		formation_positions.append(Vector3(
			(position.y - center.y) * source_map_data.local_transform_scale,
			0.0,
			(position.x - center.x) * source_map_data.local_transform_scale
		))
	# OpenSAGE advances PreAttack -> Firing -> BetweenShots as separate fixed
	# durations (WeaponStates/WeaponStateMachine.cs:35-57), so the existing
	# volley cooldown uses their parsed sum while the hit remains on PreAttack.
	var attack_period_ms := pre_attack_ms + firing_duration_ms + delay_ms
	var weapon_modes := {
		"default": _convert_retail_weapon_mode(weapon, tick_ms),
	}
	var default_weapon_mode := "default"
	var close_weapon_mode := ""
	for set_value in weapon_sets:
		if typeof(set_value) != TYPE_DICTIONARY:
			return {"_error": "invalid retail weapon set"}
		var weapon_set := set_value as Dictionary
		var conditions: Array = weapon_set.get("conditions", []) as Array
		var slots: Dictionary = weapon_set.get("slots", {}) as Dictionary
		var condition_names: Array[String] = []
		for condition_value in conditions:
			condition_names.append(String(condition_value).to_upper())
		if condition_names == ["NONE"] and typeof(slots.get("primary")) == TYPE_DICTIONARY:
			weapon_modes["default"] = _convert_retail_weapon_mode(slots["primary"] as Dictionary, tick_ms)
		if condition_names.has("CLOSE_RANGE") and typeof(slots.get("secondary")) == TYPE_DICTIONARY:
			weapon_modes["close"] = _convert_retail_weapon_mode(slots["secondary"] as Dictionary, tick_ms)
			close_weapon_mode = "close"
	for mode_value in weapon_modes.values():
		if typeof(mode_value) != TYPE_DICTIONARY or (mode_value as Dictionary).has("_error"):
			return {"_error": "invalid retail weapon mode"}
	var switch_distance_source := 0.0
	var switch_value: Variant = member.get("dualWeaponSwitchDistance", {})
	if typeof(switch_value) == TYPE_DICTIONARY and bool((switch_value as Dictionary).get("defined", true)):
		switch_distance_source = _retail_rule_number(switch_value)
		if not is_finite(switch_distance_source) or switch_distance_source < 0.0:
			return {"_error": "invalid dual weapon switch distance"}
	return {
		"horde_id": String(source_rules.get("hordeId", "")),
		"speed": speed_raw * source_map_data.local_transform_scale,
		"speed_source": speed_raw,
		"acceleration": acceleration_raw * source_map_data.local_transform_scale,
		"acceleration_source": acceleration_raw,
		"turn_rate_degrees_per_second": turn_rate_raw,
		"braking": braking_raw * source_map_data.local_transform_scale,
		"braking_source": braking_raw,
		"attack_range": attack_range_raw * source_map_data.local_transform_scale,
		"attack_range_source": attack_range_raw,
		"minimum_attack_range": minimum_range_raw * source_map_data.local_transform_scale,
		"minimum_attack_range_source": minimum_range_raw,
		"vision_range": horde_vision * source_map_data.local_transform_scale,
		"vision_range_source": horde_vision,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_duration_ms,
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / tick_ms)),
		"firing_duration_ticks": maxi(0, roundi(firing_duration_ms / tick_ms)),
		"attack_period_ticks": maxi(1, roundi(attack_period_ms / tick_ms)),
		"member_damage": maxi(1, roundi(damage)),
		"weapon_modes": weapon_modes,
		"default_weapon_mode": default_weapon_mode,
		"close_weapon_mode": close_weapon_mode,
		"close_weapon_switch_distance": switch_distance_source * source_map_data.local_transform_scale,
		"close_weapon_switch_distance_source": switch_distance_source,
		"member_count": member_count_value,
		"formation_positions": formation_positions,
		"stances": stances.duplicate(true),
		"provenance": source_rules.duplicate(true),
	}


func _convert_retail_weapon_mode(weapon: Dictionary, tick_ms: float) -> Dictionary:
	var range_source := _retail_rule_number(weapon.get("attackRange"))
	var delay_ms := _retail_rule_number(weapon.get("delayBetweenShotsMs"))
	var pre_attack_ms := _retail_rule_number(weapon.get("preAttackDelayMs"))
	var firing_ms := _retail_rule_number(weapon.get("firingDurationMs"))
	var damage := _retail_rule_number(weapon.get("damage"))
	var minimum_source := 0.0
	var minimum_value: Variant = weapon.get("minimumAttackRange", {})
	if typeof(minimum_value) == TYPE_DICTIONARY and bool((minimum_value as Dictionary).get("defined", true)):
		minimum_source = _retail_rule_number(minimum_value)
	for value in [range_source, minimum_source, delay_ms, pre_attack_ms, firing_ms, damage]:
		if not is_finite(float(value)) or float(value) < 0.0:
			return {"_error": "non-finite or negative retail weapon field"}
	var period_ms := pre_attack_ms + firing_ms + delay_ms
	return {
		"name": String(weapon.get("name", "")),
		"attack_range": range_source * source_map_data.local_transform_scale,
		"attack_range_source": range_source,
		"minimum_attack_range": minimum_source * source_map_data.local_transform_scale,
		"minimum_attack_range_source": minimum_source,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / tick_ms)),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / tick_ms)),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / tick_ms)),
		"member_damage": maxi(1, roundi(damage)),
		"provenance": weapon.duplicate(true),
	}


func _retail_rule_number(value: Variant) -> float:
	if typeof(value) != TYPE_DICTIONARY:
		return NAN
	var number_value: Variant = (value as Dictionary).get("value")
	return float(number_value) if typeof(number_value) in [TYPE_INT, TYPE_FLOAT] else NAN


func _spawn_all_presentations(expected_members: int) -> void:
	_clear_presentations()
	for id in simulation.entity_ids():
		_spawn_battalion(id, expected_members)
	for id in simulation.structure_ids():
		_spawn_structure(id)


func _spawn_battalion(id: int, expected_members: int) -> void:
	if battalion_nodes.has(id):
		return
	var entity: Dictionary = simulation.entity(id)
	var object_id := String(entity.get("object_id", SOLDIER_OBJECT_ID))
	var capability: Dictionary = validated_battalion_capabilities.get(object_id, {}) as Dictionary
	var member_count := maxi(1, int(entity.get("member_count", expected_members)))
	var battalion: RetailBattalion = BattalionScript.new()
	battalion.configure(
		id,
		int(entity["team"]),
		object_id,
		capability,
		member_count,
		source_map_data.local_transform_scale,
		Array(entity.get("formation_positions", []))
	)
	var position := Vector2(entity["position"])
	add_child(battalion)
	battalion.set_authoritative_position(Vector3(position.x, _presentation_height(position), position.y), true)
	# Capture-anchored equivalence: retail infantry read as sunlit like
	# structures (bfme2-ref-120s), so members receive the object-domain rig in
	# addition to the authored infantry rig; infantry-only lighting leaves
	# units near-black.
	_assign_geometry_light_layer(battalion, INFANTRY_LIGHT_LAYER | OBJECT_LIGHT_LAYER)
	battalion_nodes[id] = battalion
	var indicator: RetailOrderIndicator = OrderIndicatorScript.new()
	indicator.name = "OrderIndicator_%d" % id
	indicator.configure(selected_pack_root, source_map_data.local_transform_scale)
	add_child(indicator)
	order_indicators[id] = indicator


func _spawn_structure(id: int) -> void:
	if structure_nodes.has(id):
		return
	var entity: Dictionary = simulation.structure(id)
	var structure: RetailStructure = StructureScript.new()
	var kind := String(entity.get("structure_kind", ""))
	structure.lifecycle_route_requested.connect(
		Callable(self, "_on_structure_lifecycle_route_requested").bind(structure)
	)
	structure.configure(entity, String(BUILDING_OBJECT_IDS.get(kind, "")), source_map_data.local_transform_scale)
	var position := Vector2(entity["position"])
	structure.position = Vector3(position.x, _presentation_height(position) - 0.35, position.y)
	add_child(structure)
	_assign_geometry_light_layer(structure, OBJECT_LIGHT_LAYER)
	structure_nodes[id] = structure


func _on_structure_lifecycle_route_requested(request: Dictionary, structure: RetailStructure) -> void:
	structure_lifecycle_route_sequence += 1
	var audio_event := String(request.get("audioEvent", ""))
	var fx_id := String(request.get("enteringFx", ""))
	var particles_value: Variant = request.get("particleSystemIds", [])
	var result := {
		"ok": true,
		"sequence": structure_lifecycle_route_sequence,
		"entityId": int(request.get("entityId", 0)),
		"phase": String(request.get("phase", "")),
		"audio": {"ok": true, "status": "not-declared"},
		"effects": {"ok": true, "status": "not-declared"},
	}
	if audio_event != "":
		if audio_system == null:
			result.audio = {"ok": false, "reason": "structure-audio-runtime-unavailable", "event_id": audio_event}
		else:
			result.audio = audio_system.play_declared_structure_event(
				audio_event,
				structure_lifecycle_route_sequence,
				int(request.get("entityId", 0)),
				String(request.get("phase", ""))
			)
	var has_particles := typeof(particles_value) == TYPE_ARRAY and not (particles_value as Array).is_empty()
	if fx_id != "" or has_particles:
		if battlefield == null:
			result.effects = {"ok": false, "reason": "structure-effect-runtime-unavailable"}
		else:
			result.effects = battlefield.route_structure_damage_effects(request)
	var audio_ok := bool((result.audio as Dictionary).get("ok", false))
	var effects_ok := bool((result.effects as Dictionary).get("ok", false))
	result.ok = audio_ok and effects_ok
	if not bool(result.ok):
		var reasons: Array[String] = []
		if not audio_ok:
			reasons.append(String((result.audio as Dictionary).get("reason", "structure audio rejected")))
		if not effects_ok:
			reasons.append(String((result.effects as Dictionary).get("reason", "structure effects rejected")))
		result["reason"] = "; ".join(reasons)
	structure.record_route_dispatch(result)


func _all_battalion_retail_visuals_loaded() -> bool:
	for id in simulation.entity_ids():
		var entity: Dictionary = simulation.entity(id)
		var battalion: RetailBattalion = battalion_nodes.get(id)
		var expected_members := maxi(1, int(entity.get("member_count", 15)))
		if (
			battalion == null
			or battalion.object_id != String(entity.get("object_id", SOLDIER_OBJECT_ID))
			or battalion.member_count != expected_members
			or battalion.retail_visual_count != expected_members
		):
			return false
	return true


func _all_structure_retail_visuals_loaded() -> bool:
	for id in simulation.structure_ids():
		var structure: RetailStructure = structure_nodes.get(id)
		if (
			structure == null
			or not structure.retail_visual_loaded
			or structure.presentation_mode != "private-imported-lifecycle"
			or structure.contract_error != ""
		):
			return false
	return true


func _all_men_structure_contracts_v1() -> bool:
	for object_id_value in BUILDING_OBJECT_IDS.values():
		var definition: Dictionary = ContentDB.get_bundle_object(String(object_id_value))
		var presentation: Dictionary = definition.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		if int(lifecycle.get("schemaVersion", -1)) != StructureScript.LIFECYCLE_SCHEMA_VERSION_V1:
			return false
	return true


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
	_update_construction_ghost()
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
			if key.keycode == KEY_F9:
				hud.set_fps_overlay_visible(hud.fps_overlay == null or not hud.fps_overlay.visible)
				get_viewport().set_input_as_handled()
				return
			if key.keycode == KEY_F8:
				hud.set_input_debug_visible(hud.input_debug_label == null or not hud.input_debug_label.visible)
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
	if event is InputEventMouseButton and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_MIDDLE:
		# Retail middle-mouse orbit; release keeps the chosen angle.
		_camera_orbiting = (event as InputEventMouseButton).pressed
		return
	if event is InputEventMouseMotion and _camera_orbiting:
		camera_user_yaw = wrapf(camera_user_yaw + (event as InputEventMouseMotion).relative.x * 0.006, -PI, PI)
		_apply_camera_transform()
		return
	if event is InputEventMouseMotion and _drag_select_origin != Vector2.INF:
		var motion := event as InputEventMouseMotion
		if _drag_selecting or motion.position.distance_to(_drag_select_origin) > DRAG_SELECT_THRESHOLD:
			_drag_selecting = true
			_update_selection_band(motion.position)
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_LEFT and not mouse.pressed:
			# Left release: finish a drag box, otherwise it was a click handled
			# on press.
			if _drag_selecting:
				_finish_box_selection(mouse.position, mouse.shift_pressed)
			_drag_select_origin = Vector2.INF
			_drag_selecting = false
			_hide_selection_band()
			return
		if not mouse.pressed:
			return
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
			if mouse.double_click:
				_select_same_type_on_screen(point)
				_drag_select_origin = Vector2.INF
				return
			_drag_select_origin = mouse.position
			_drag_selecting = false
			_handle_left_click(point, mouse.shift_pressed)
		elif mouse.button_index == MOUSE_BUTTON_RIGHT and not simulation.selected_ids.is_empty():
			_handle_right_click(point)


func _update_selection_band(current: Vector2) -> void:
	if _selection_band == null:
		_selection_band = Panel.new()
		_selection_band.name = "SelectionBand"
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.85, 0.78, 0.45, 0.08)
		style.border_color = Color(0.87, 0.76, 0.42, 0.9)
		style.set_border_width_all(1)
		_selection_band.add_theme_stylebox_override("panel", style)
		_selection_band.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_selection_band.z_index = 20
		hud_root.add_child(_selection_band)
	var rect := Rect2(_drag_select_origin, Vector2.ZERO).expand(current).abs()
	_selection_band.position = rect.position
	_selection_band.size = rect.size
	_selection_band.visible = true


func _hide_selection_band() -> void:
	if _selection_band != null:
		_selection_band.visible = false


func _finish_box_selection(release_position: Vector2, additive: bool) -> void:
	var rect := Rect2(_drag_select_origin, Vector2.ZERO).expand(release_position).abs()
	var picked: Array[int] = []
	if additive:
		for existing in simulation.selected_ids:
			picked.append(int(existing))
	for id_value in battalion_nodes.keys():
		var id := int(id_value)
		var entity: Dictionary = simulation.entity(id)
		if entity.is_empty() or int(entity.get("team", -1)) != 0 or int(entity.get("health", 0)) <= 0:
			continue
		var battalion := battalion_nodes[id] as Node3D
		var screen := camera.unproject_position(battalion.global_position)
		if not camera.is_position_behind(battalion.global_position) and rect.has_point(screen):
			if not picked.has(id):
				picked.append(id)
	if picked.is_empty():
		return
	selected_structure_id = 0
	var count := int(simulation.select_many(picked))
	hud.set_feedback("Selected %d battalion%s." % [count, "" if count == 1 else "s"])
	_sync_presentation()


func _select_same_type_on_screen(point: Vector2) -> void:
	# Retail double-click: select every on-screen battalion of the same type.
	var anchor_id := _closest_battalion(point, 0, 6.0)
	if anchor_id == 0:
		return
	var anchor_type := String(simulation.entity(anchor_id).get("object_id", ""))
	var viewport_rect := Rect2(Vector2.ZERO, get_viewport().get_visible_rect().size)
	var picked: Array[int] = []
	for id_value in battalion_nodes.keys():
		var id := int(id_value)
		var entity: Dictionary = simulation.entity(id)
		if entity.is_empty() or int(entity.get("team", -1)) != 0 or int(entity.get("health", 0)) <= 0:
			continue
		if String(entity.get("object_id", "")) != anchor_type:
			continue
		var battalion := battalion_nodes[id] as Node3D
		var screen := camera.unproject_position(battalion.global_position)
		if not camera.is_position_behind(battalion.global_position) and viewport_rect.has_point(screen):
			picked.append(id)
	if picked.is_empty():
		return
	selected_structure_id = 0
	var count := int(simulation.select_many(picked))
	hud.set_feedback("Selected %d %s battalion%s." % [count, String(simulation.entity(anchor_id).get("name", "")), "" if count == 1 else "s"])
	_sync_presentation()


func _handle_left_click(point: Vector2, additive: bool) -> void:
	if power_cast_armed != "":
		var cast_result: Dictionary = (
			simulation.cast_heal(0, point) if power_cast_armed == "heal" else simulation.cast_rally(0, point)
		)
		if bool(cast_result.get("ok", false)):
			hud.set_feedback("%s affects %d battalion%s." % [
				power_cast_armed.capitalize(), int(cast_result.get("battalions", 0)),
				"" if int(cast_result.get("battalions", 0)) == 1 else "s",
			])
			power_cast_armed = ""
		else:
			hud.set_feedback("Cannot cast: %s." % String(cast_result.get("reason", "rejected")).replace("-", " "), true)
		_sync_presentation()
		return
	# Retail placement: with a construction command armed, left-click places
	# the structure and right-click cancels.
	if construction_kind_armed != "":
		var result := simulation.issue_construct(simulation.selected_ids.duplicate(), construction_kind_armed, point)
		if bool(result.get("ok", false)):
			hud.set_feedback("%s construction started." % construction_kind_armed.replace("_", " ").capitalize())
			construction_kind_armed = ""
			_clear_construction_ghost()
		else:
			hud.set_feedback("Cannot build here: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)
		_sync_presentation()
		return
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
	if power_cast_armed != "":
		power_cast_armed = ""
		hud.set_feedback("Power cast cancelled.")
		return
	if construction_kind_armed != "":
		# Retail cancels an armed construction command on right-click.
		construction_kind_armed = ""
		_clear_construction_ghost()
		hud.set_feedback("Construction placement cancelled.")
		_sync_presentation()
		return
	var enemy_id := _closest_battalion(point, 1, 6.0)
	if enemy_id == 0:
		enemy_id = _closest_structure(point, 1)
	if enemy_id != 0:
		var accepted := simulation.issue_attack(simulation.selected_ids.duplicate(), enemy_id)
		hud.set_feedback("Attack order accepted." if accepted > 0 else "Attack order rejected.", accepted == 0)
		if accepted > 0:
			_sync_attack_target_indicator(enemy_id)
	else:
		var moved := simulation.issue_attack_move(simulation.selected_ids.duplicate(), point) if attack_move_armed else simulation.issue_move(simulation.selected_ids.duplicate(), point)
		var accepted_text := "Attack-move order plotted." if attack_move_armed else "Move order plotted on the Palantir."
		hud.set_feedback(accepted_text if moved > 0 else "Move rejected: %s." % simulation.last_route_rejection.replace("-", " "), moved == 0)
		attack_move_armed = false
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
	if _profile_sync:
		_profile_mark = Time.get_ticks_usec()
	for id in simulation.entity_ids():
		if not battalion_nodes.has(id):
			_spawn_battalion(id, int(gameplay_rules.get("member_count", 15)))
		var entity: Dictionary = simulation.entity(id)
		var battalion: RetailBattalion = battalion_nodes[id]
		var position := Vector2(entity["position"])
		battalion.set_authoritative_position(Vector3(position.x, _presentation_height(position), position.y))
		battalion.set_health(int(entity["health"]), int(entity["maximum_health"]))
		battalion.set_production_exit_progress(float(entity.get("production_exit_progress", 1.0)))
		battalion.set_selected(simulation.selected_ids.has(id))
		var attack_target := _attack_target_node(entity)
		var attack_target_height := 1.0
		if attack_target is RetailBattalion:
			attack_target_height = float(attack_target.attack_presentation_height())
		elif attack_target is RetailStructure:
			attack_target_height = float(attack_target.attack_presentation_height())
		battalion.set_attack_target(
			attack_target,
			attack_target_height,
			maxf(SimScript.TICK_SECONDS, float(entity.get("pre_attack_ticks", 1)) * SimScript.TICK_SECONDS)
		)
		battalion.sync_member_states(
			Array(entity.get("member_health", [])),
			int(entity.get("member_maximum_health", 1)),
			Array(entity.get("member_attack_tokens", [])),
			String(entity["state"]),
			Array(entity.get("member_attack_release_tokens", [])),
			Array(entity.get("member_weapon_modes", [])),
			_member_attack_target_globals(entity),
			Array(entity.get("member_corpse_expire_ticks", [])),
			simulation.tick_index
		)
		var facing := _entity_facing(entity)
		battalion.set_facing_direction(facing, float(entity.get("turn_rate_degrees_per_second", 720.0)))
		battalion.set_locomotor_speed(float(entity.get("speed", 5.5)))
		var indicator: RetailOrderIndicator = order_indicators[id]
		indicator.sync_from_entity(entity, simulation.selected_ids.has(id), _presentation_height)
	var removed_battalions: Array[int] = []
	for id_value in battalion_nodes.keys():
		var id := int(id_value)
		if simulation.entities.has(id):
			continue
		var battalion := battalion_nodes[id] as Node
		var indicator := order_indicators.get(id) as Node
		if battalion != null:
			battalion.queue_free()
		if indicator != null:
			indicator.queue_free()
		removed_battalions.append(id)
	for id in removed_battalions:
		battalion_nodes.erase(id)
		order_indicators.erase(id)
	if _profile_sync:
		presentation_profile["battalions_us"] = presentation_profile.get("battalions_us", 0) + (Time.get_ticks_usec() - _profile_mark)
		_profile_mark = Time.get_ticks_usec()
	for id in simulation.structure_ids():
		if not structure_nodes.has(id):
			_spawn_structure(id)
		var structure: RetailStructure = structure_nodes[id]
		structure.set_selected(selected_structure_id == id)
		structure.sync_state(simulation.structure(id))
	if _profile_sync:
		presentation_profile["structures_us"] = presentation_profile.get("structures_us", 0) + (Time.get_ticks_usec() - _profile_mark)
		_profile_mark = Time.get_ticks_usec()
	if audio_system != null:
		audio_system.sync_events(simulation.events)
	if _profile_sync:
		presentation_profile["audio_us"] = presentation_profile.get("audio_us", 0) + (Time.get_ticks_usec() - _profile_mark)
		_profile_mark = Time.get_ticks_usec()
	_sync_selected_attack_target_indicator()
	_refresh_hud()
	if _profile_sync:
		presentation_profile["hud_us"] = presentation_profile.get("hud_us", 0) + (Time.get_ticks_usec() - _profile_mark)


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
		# Aim at the next waypoint only while it is meaningfully ahead. Inside
		# the deadzone the bearing to a nearly-reached waypoint swings wildly
		# per tick, which made battalions pirouette while stopping; retail
		# units keep their path heading on arrival.
		var next_waypoint := Vector2(route[0])
		if position.distance_to(next_waypoint) > 1.2:
			return position.direction_to(next_waypoint)
		if route.size() > 1:
			return position.direction_to(Vector2(route[1]))
	var retained := entity.get("facing", Vector2.RIGHT if int(entity.get("team", 0)) == 0 else Vector2.LEFT) as Vector2
	return retained.normalized() if retained.length_squared() > 0.000001 else Vector2.RIGHT


func _attack_target_node(entity: Dictionary) -> Node3D:
	var target_id := int(entity.get("target_id", 0))
	if target_id == 0:
		return null
	if String(entity.get("target_kind", "battalion")) == "structure":
		return structure_nodes.get(target_id) as Node3D
	return battalion_nodes.get(target_id) as Node3D


func _member_attack_target_globals(entity: Dictionary) -> Array:
	var result: Array = []
	var member_count := int(entity.get("member_count", 0))
	result.resize(member_count)
	result.fill(null)
	if String(entity.get("target_kind", "battalion")) != "battalion":
		return result
	var target_id := int(entity.get("target_id", 0))
	var target_battalion := battalion_nodes.get(target_id) as RetailBattalion
	if target_battalion == null:
		return result
	var assignments: Array = entity.get("member_target_indices", [])
	for member_index in range(mini(member_count, assignments.size())):
		var target_member := int(assignments[member_index])
		if target_member >= 0:
			result[member_index] = target_battalion.member_attack_global_position(target_member)
	return result


func _refresh_hud() -> void:
	if hud == null or simulation == null:
		return
	hud.set_resources(simulation.resources_for_team(0), simulation.command_points_for_team(0), simulation.command_point_cap)
	var score := _player_score_values()
	hud.set_score_values(int(score.units_trained), int(score.units_lost), int(score.resources_gathered))
	hud.set_control_groups(simulation.control_groups)
	if selected_structure_id != 0:
		var structure := simulation.structure(selected_structure_id)
		hud.set_selection("%s  •  %d%%" % [String(structure.get("name", "Structure")), roundi(100.0 * float(structure.get("health", 0)) / float(maxi(1, int(structure.get("maximum_health", 1)))))])
		var production: Array = structure.get("production", [])
		var can_train := int(structure.get("team", -1)) == 0 and int(structure.get("health", 0)) > 0 and not production.is_empty()
		var queue_count := Array(structure.get("queue", [])).size()
		var queue_state := simulation.production_queue_state(selected_structure_id)
		hud.set_production_state(production, can_train, queue_count, queue_state)
		hud.set_unit_selection_state([], simulation.entities)
	else:
		var names: Array[String] = []
		for id in simulation.selected_ids:
			names.append(String(simulation.entity(id).get("name", str(id))))
		hud.set_selection(", ".join(names) if not names.is_empty() else "No battalion selected")
		hud.set_production_state([], false)
		hud.set_unit_selection_state(simulation.selected_ids, simulation.entities)
		if not simulation.selected_ids.is_empty():
			hud.set_active_stance(String(simulation.entity(simulation.selected_ids[0]).get("stance", "Battle")))
	if not hud.sync_retail_selection_context(
		simulation.selected_ids,
		selected_structure_id,
		simulation.entities,
		simulation.structures,
		simulation.winner
	):
		_fail("Retail Men/Fords side-command FadeIn rejected the live selection context.")
		return
	hud.set_objective("DESTROY THE ENEMY FORTRESS" if simulation.winner == -1 else ("VICTORY" if simulation.winner == 0 else "DEFEAT"))
	minimap.camera_center = camera_focus
	if diagnostics_visible:
		# state_signature() deep-copies and serializes the whole sim snapshot
		# including the ever-growing event log — O(match age). Computing it
		# every frame regardless of visibility was the progressive slowdown
		# (6 fps by minute 7). It is diagnostics-only; pay for it only on F10.
		var diagnostics := "TICK %05d  HASH %s  MUSIC %s\nBLUE %d  RED %d  ROUTES %d  F10 hides diagnostics" % [
			simulation.tick_index,
			simulation.state_signature(),
			audio_system.current_music_state.to_upper() if audio_system != null else "OFF",
			simulation.living_ids(0).size(),
			simulation.living_ids(1).size(),
			source_map_data.route_query_count if source_map_data != null else 0,
		]
		hud.show_diagnostics(diagnostics, true)
	else:
		hud.show_diagnostics("", false)
	if simulation.winner != -1 and simulation.winner != _last_presented_winner:
		_last_presented_winner = simulation.winner
		hud.show_outcome(simulation.winner)


var _score_cache := {"units_trained": 0, "units_lost": 0, "resources_gathered": 0}
var _score_event_index := 0


func _player_score_values() -> Dictionary:
	# Incremental: this runs every presentation frame, and the sim event log
	# only grows. A full scan here degraded linearly with match age (the
	# 3-8-minute progressive slowdown). Consuming only new events also counts
	# units_lost while the defeated entity still exists in the corpse window.
	var events: Array = simulation.events
	while _score_event_index < events.size():
		var event: Dictionary = events[_score_event_index]
		_score_event_index += 1
		var kind := String(event.get("kind", ""))
		if kind == "production.complete" and int(event.get("team", -1)) == 0:
			_score_cache.units_trained += 1
		elif kind == "economy.payout" and int(event.get("team", -1)) == 0:
			_score_cache.resources_gathered += int(event.get("amount", 0))
		elif kind == "battalion.defeated":
			var defeated: Dictionary = simulation.entities.get(int(event.get("target_id", 0)), {})
			if int(defeated.get("team", -1)) == 0:
				_score_cache.units_lost += 1
	return _score_cache


func _assign_group(group: int) -> void:
	var result := simulation.assign_control_group(group, simulation.selected_ids)
	hud.set_feedback("Group %d assigned (%d battalions)." % [group, Array(result.get("entity_ids", [])).size()])
	_refresh_hud()


func _recall_group(group: int) -> void:
	simulation.selected_ids = simulation.recall_control_group(group)
	selected_structure_id = 0
	hud.set_feedback("Group %d recalled." % group)
	_sync_presentation()


func _queue_selected_producer(unit_id: String) -> void:
	var unit_name := String(UNIT_QUEUE_NAMES.get(unit_id, "Unit"))
	if selected_structure_id == 0:
		hud.set_feedback("Cannot train %s: select its production building." % unit_name, true)
		_refresh_hud()
		return
	var producer := selected_structure_id
	var structure: Dictionary = simulation.structure(producer)
	var production: Array = structure.get("production", [])
	if int(structure.get("team", -1)) != 0 or int(structure.get("health", 0)) <= 0 or not production.has(unit_id):
		hud.set_feedback("Cannot train %s from the selected structure." % unit_name, true)
		_refresh_hud()
		return
	var result := simulation.queue_unit(0, producer, unit_id)
	var accepted := bool(result.get("ok", false))
	var reason := String(result.get("reason", "rejected"))
	var feedback := "%s added to the queue." % unit_name if accepted else "Cannot train %s: %s." % [unit_name, reason.replace("-", " ")]
	hud.set_feedback(feedback, not accepted)
	if not accepted and reason == "command-point-cap":
		hud.flash_command_points()
	_refresh_hud()


func _cancel_selected_production(queue_index: int) -> void:
	if selected_structure_id == 0:
		hud.set_feedback("Cannot cancel training: select its production building.", true)
		_refresh_hud()
		return
	var result := simulation.cancel_queued_unit(0, selected_structure_id, queue_index)
	var accepted := bool(result.get("ok", false))
	var feedback := "Training cancelled; %d resources refunded." % int(result.get("refund", 0)) if accepted else "Cannot cancel training: %s." % String(result.get("reason", "rejected")).replace("-", " ")
	hud.set_feedback(feedback, not accepted)
	_refresh_hud()


func _arm_attack_move() -> void:
	if simulation == null or simulation.selected_ids.is_empty():
		return
	attack_move_armed = true
	construction_kind_armed = ""
	hud.set_feedback("Attack Move armed: right-click a destination.")


func _arm_construction(structure_kind: String) -> void:
	if simulation == null or simulation.selected_ids.is_empty() or not SimScript.STRUCTURE_BUILD_RULES.has(structure_kind):
		hud.set_feedback("Builder construction command rejected.", true)
		return
	construction_kind_armed = structure_kind
	attack_move_armed = false
	_spawn_construction_ghost()
	var rule: Dictionary = SimScript.STRUCTURE_BUILD_RULES[structure_kind]
	hud.set_feedback("Place %s: left-click a clear site (right-click cancels). Cost %d." % [structure_kind.replace("_", " ").capitalize(), int(rule["cost"])])


func _spawn_construction_ghost() -> void:
	_clear_construction_ghost()
	# Gameplay placement cursor: a flat ground quad showing the site footprint
	# with validity tint (a Decal projected a visible volume through the fog).
	# The retail translucent building-model ghost is an M3 parity item.
	construction_ghost = MeshInstance3D.new()
	construction_ghost.name = "ConstructionPlacementGhost"
	var quad := PlaneMesh.new()
	quad.size = Vector2(14.0, 14.0)
	construction_ghost.mesh = quad
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_texture = preload("res://src/retail_slice/retail_shadow_decal.gd")._shared_texture()
	material.albedo_color = Color(0.20, 0.85, 0.30, 0.4)
	material.no_depth_test = false
	construction_ghost.material_override = material
	construction_ghost.set_meta("legal_safe_gameplay_overlay", true)
	construction_ghost.visible = false
	add_child(construction_ghost)


func _clear_construction_ghost() -> void:
	if construction_ghost != null and is_instance_valid(construction_ghost):
		construction_ghost.queue_free()
	construction_ghost = null


func _update_construction_ghost() -> void:
	if construction_kind_armed == "":
		if construction_ghost != null:
			_clear_construction_ghost()
		return
	if construction_ghost == null:
		return
	var world: Variant = _screen_to_world(get_viewport().get_mouse_position())
	if world == null:
		construction_ghost.visible = false
		return
	var ground := world as Vector3
	construction_ghost.visible = true
	construction_ghost.global_position = Vector3(ground.x, ground.y + 0.15, ground.z)
	var probe := simulation.validate_construct_site(
		simulation.selected_ids.duplicate(), construction_kind_armed, Vector2(ground.x, ground.z)
	)
	var material := construction_ghost.material_override as StandardMaterial3D
	material.albedo_color = (
		Color(0.20, 0.85, 0.30, 0.4) if bool(probe.get("ok", false)) else Color(0.90, 0.18, 0.14, 0.4)
	)


func _stop_selected_units() -> void:
	if simulation == null:
		return
	attack_move_armed = false
	construction_kind_armed = ""
	_clear_construction_ghost()
	var stopped := simulation.issue_stop(simulation.selected_ids.duplicate())
	hud.set_feedback("Stop order accepted." if stopped > 0 else "Stop order rejected.", stopped == 0)


func _toggle_selected_stance() -> void:
	var accepted := simulation.issue_toggle_stance(simulation.selected_ids.duplicate())
	if accepted > 0:
		var stance := String(simulation.entity(simulation.selected_ids[0]).get("stance", "Battle"))
		hud.set_active_stance(stance)
		hud.set_feedback("Stance changed to %s." % stance)
	else:
		hud.set_feedback("Stance order rejected.", true)
	_sync_presentation()


func _sync_selected_attack_target_indicator() -> void:
	if attack_target_indicator == null:
		return
	var shared_target := 0
	for id in simulation.selected_ids:
		var target_id := int(simulation.entity(id).get("target_id", 0))
		if target_id == 0 or (shared_target != 0 and shared_target != target_id):
			attack_target_indicator.clear_target()
			return
		shared_target = target_id
	if shared_target == 0:
		attack_target_indicator.clear_target()
		return
	_sync_attack_target_indicator(shared_target)


func _sync_attack_target_indicator(target_id: int) -> void:
	if attack_target_indicator == null:
		return
	var position := Vector2.ZERO
	var height := 2.5
	if simulation.entities.has(target_id):
		var target: Dictionary = simulation.entity(target_id)
		if int(target.get("health", 0)) <= 0:
			attack_target_indicator.clear_target()
			return
		position = Vector2(target.get("position", Vector2.ZERO))
		height = 2.8
	elif simulation.structures.has(target_id):
		var target: Dictionary = simulation.structure(target_id)
		if int(target.get("health", 0)) <= 0:
			attack_target_indicator.clear_target()
			return
		position = Vector2(target.get("position", Vector2.ZERO))
		height = 6.5
	else:
		attack_target_indicator.clear_target()
		return
	attack_target_indicator.show_target(Vector3(position.x, _presentation_height(position), position.y), height)


func toggle_escape_menu() -> void:
	if not ready_ok or simulation.winner != -1:
		return
	simulation_paused = not simulation_paused
	# Retail pause freezes the world (animations, particles, projectiles), not
	# just the tick loop. The slice root, HUD, and audio stay live so the menu,
	# music, and the unpause input keep working.
	process_mode = Node.PROCESS_MODE_ALWAYS
	if hud_root != null:
		hud_root.process_mode = Node.PROCESS_MODE_ALWAYS
	if audio_system != null:
		audio_system.process_mode = Node.PROCESS_MODE_ALWAYS
	get_tree().paused = simulation_paused
	hud.show_pause(simulation_paused)
	hud.set_match_clock_seconds(simulation.tick_index * SimScript.TICK_SECONDS)
	hud.set_feedback("Simulation paused." if simulation_paused else "Simulation resumed.")


func reset_match() -> void:
	if simulation == null:
		return
	simulation.setup(source_map_data.simulation_configuration(), gameplay_rules)
	simulation_paused = false
	_score_cache = {"units_trained": 0, "units_lost": 0, "resources_gathered": 0}
	_score_event_index = 0
	if is_inside_tree():
		get_tree().paused = false
	selected_structure_id = 0
	structure_lifecycle_route_sequence = 0
	accumulator = 0.0
	_last_presented_winner = -1
	hud.show_pause(false)
	hud.hide_outcome()
	_spawn_all_presentations(int(gameplay_rules.get("member_count", 15)))
	if audio_system != null:
		audio_system.intent_log.clear()
		audio_system._next_event_index = 0
		audio_system.current_music_state = ""
		audio_system.sync_events(simulation.events)
	_sync_presentation()
	hud.set_feedback("Battle reset. Select a blue battalion or a production building.")


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
	world_environment = WorldEnvironment.new()
	world_environment.name = "FordsSourceEnvironment"
	var environment := Environment.new()
	# The retail world sky is new_skybox.w3d plus a five-face texture set from
	# environment.ini. Fords does not declare which named set the executable
	# selects, and that closure is absent from the current pack. A neutral
	# background keeps the gap visible without inventing a replacement sky.
	environment.background_mode = Environment.BG_COLOR
	# Approved equivalence: with the skybox texture set still oracle-blocked,
	# the horizon clears to the exact retail fog color the linear fog saturates
	# to at distance, instead of an invented black void.
	environment.background_color = FORDS_FOG_COLOR
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	# Approved equivalence: the object-domain material ambient sum applies as
	# scene ambient. The terrain shader evaluates its own exact ambient and
	# ignores scene ambient; infantry's authored ambient is zero, so the small
	# object-domain term reaching infantry is a documented approximation.
	environment.ambient_light_color = Color(11.0 / 255.0, 9.0 / 255.0, 11.0 / 255.0)
	environment.ambient_light_energy = 1.0
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	environment.adjustment_enabled = false
	# Godot's built-in fog is exponential/height based, so it remains disabled.
	# The source map's exact linear start/end curve is attached below through a
	# Forward+ compositor after the validated map scale is available.
	environment.fog_enabled = false
	environment.fog_light_color = FORDS_FOG_COLOR
	world_environment.environment = environment
	environment_runtime_metadata = {
		"schema": "openbfme.fords-environment-runtime",
		"schema_version": 0,
		"oracle_sha256": FORDS_ENVIRONMENT_ORACLE_SHA256,
		"time_of_day": FORDS_ACTIVE_TIME_OF_DAY,
		"weather": FORDS_ACTIVE_WEATHER,
		"sky": {
			"draw_skybox_in_source": true,
			"model_source": FORDS_SKYBOX_MODEL_SOURCE,
			"texture_set_source": FORDS_SKYBOX_TEXTURE_SET_SOURCE,
			"map_texture_set_override_present": false,
			"status": FORDS_SKYBOX_STATUS,
			"runtime_background": "fog-color-horizon-no-sky-material",
		},
		"water_reflection": {
			"source_leaf": FORDS_WATER_REFLECTION_SOURCE_LEAF,
			"standing_water_area_count": 4,
			"status": FORDS_WATER_REFLECTION_STATUS,
		},
		"fog": {
			"enabled_in_source": true,
			"color": FORDS_FOG_COLOR,
			"start_source": FORDS_FOG_START_SOURCE,
			"end_source": FORDS_FOG_END_SOURCE,
			"start_local": 0.0,
			"end_local": 0.0,
			"runtime_enabled": false,
			"renderer_status": "exact-linear-source-values-retained-awaiting-validated-map-scale",
		},
		"lighting": {
			"source_domain_count": 3,
			"lights_per_domain": 3,
			"runtime_directional_domains": ["object", "infantry"],
			"runtime_directional_light_count": 6,
			"diffuse_status": "terrain-exact-opensage-shader-object-and-infantry-source-domain-lights",
			"nonzero_ambient_row_count": 3,
			"ambient_is_domain_specific": true,
			"ambient_runtime_enabled": true,
			"ambient_runtime_domains": ["terrain", "object"],
			"ambient_unresolved_domains": [],
			"ambient_status": "terrain-exact-opensage-light-sum-shader-bound-object-ambient-scene-approved-equivalence",
			"source_shadow_volumes": true,
			"source_shadow_decals": true,
			"source_shadow_mapping": false,
			"source_shadow_color_argb": 1073741824,
			"source_shadow_color": Color(0.0, 0.0, 0.0, 64.0 / 255.0),
			"shadow_runtime_enabled": true,
			"shadow_status": "source-decal-technique-approved-decal-equivalence-runtime-blob-decals-at-source-shadow-color",
		},
		"camera": {
			"minimum_height_source": FORDS_CAMERA_MIN_HEIGHT_SOURCE,
			"maximum_height_source": FORDS_CAMERA_MAX_HEIGHT_SOURCE,
			"pitch_above_horizontal_degrees": FORDS_CAMERA_PITCH_ABOVE_HORIZONTAL_DEGREES,
			"yaw_degrees": FORDS_CAMERA_YAW_DEGREES,
			"scroll_speed_scalar": FORDS_CAMERA_SCROLL_SPEED_SCALAR,
			"ground_minimum_source": FORDS_CAMERA_GROUND_MIN_SOURCE,
			"ground_maximum_source": FORDS_CAMERA_GROUND_MAX_SOURCE,
			"common_named_camera_fov_radians": FORDS_CAMERA_NAMED_FOV_RADIANS,
			"keyboard_base_status": "openbfme-playability-value-source-base-rate-unresolved",
		},
	}
	world_environment.set_meta("retail_environment", environment_runtime_metadata)
	add_child(world_environment)
	camera = Camera3D.new()
	camera.name = "FordsTacticalCamera"
	camera.fov = rad_to_deg(FORDS_CAMERA_NAMED_FOV_RADIANS)
	camera.current = true
	add_child(camera)


func _configure_source_environment() -> String:
	if source_map_data == null or not source_map_data.ready or source_map_data.local_transform_scale <= 0.0:
		return "validated Fords map scale is unavailable"
	var local_scale := source_map_data.local_transform_scale
	linear_fog = LinearFogScript.new()
	var fog_error := linear_fog.configure_fords(local_scale)
	if fog_error != "":
		linear_fog = null
		return fog_error
	var compositor := linear_fog.create_compositor()
	if compositor == null:
		linear_fog = null
		return "exact Fords linear fog compositor could not be created"
	world_environment.compositor = compositor
	var fog_metadata := environment_runtime_metadata.get("fog", {}) as Dictionary
	fog_metadata["start_local"] = FORDS_FOG_START_SOURCE * local_scale
	fog_metadata["end_local"] = FORDS_FOG_END_SOURCE * local_scale
	fog_metadata["runtime_enabled"] = true
	fog_metadata["renderer_status"] = "exact-linear-depth-compositor-configured-forward-plus-dispatch-requires-rendered-gate"
	fog_metadata["runtime_contract"] = linear_fog.runtime_contract()
	environment_runtime_metadata["fog"] = fog_metadata
	var camera_metadata := environment_runtime_metadata.get("camera", {}) as Dictionary
	camera_metadata["minimum_height_local"] = FORDS_CAMERA_MIN_HEIGHT_SOURCE * local_scale
	camera_metadata["maximum_height_local"] = FORDS_CAMERA_MAX_HEIGHT_SOURCE * local_scale
	camera_metadata["ground_minimum_local"] = (FORDS_CAMERA_GROUND_MIN_SOURCE - source_map_data.reference_elevation) * local_scale
	camera_metadata["ground_maximum_local"] = (FORDS_CAMERA_GROUND_MAX_SOURCE - source_map_data.reference_elevation) * local_scale
	camera_metadata["local_transform_scale"] = local_scale
	camera_metadata["coordinate_transform"] = "godot=(sage.x,sage.z,-sage.y),then-player-start-local-basis"
	camera_metadata["local_axis_x"] = source_map_data.local_axis_x
	camera_metadata["local_axis_z"] = source_map_data.local_axis_z
	environment_runtime_metadata["camera"] = camera_metadata
	_build_source_lights()
	world_environment.set_meta("retail_environment", environment_runtime_metadata)
	_apply_camera_transform()
	return ""


func _build_source_lights() -> void:
	for light in source_environment_lights:
		if is_instance_valid(light):
			if light.get_parent() == self:
				remove_child(light)
			light.free()
	source_environment_lights.clear()
	var domain_layers := {
		"terrain": TERRAIN_LIGHT_LAYER,
		"object": OBJECT_LIGHT_LAYER,
		"infantry": INFANTRY_LIGHT_LAYER,
	}
	# Godot renders at most eight DirectionalLight3D nodes. Terrain evaluates its
	# exact three source rows in the material shader, leaving six runtime nodes
	# for the object and infantry domains without exceeding that hard limit.
	for domain in ["object", "infantry"]:
		var rows: Array = FORDS_AFTERNOON_LIGHT_RIGS[domain]
		for row_value in rows:
			var row := row_value as Dictionary
			var source_ambient := _array_vector3(row.get("ambient", []))
			var source_color := _array_vector3(row.get("color", []))
			var source_direction := _array_vector3(row.get("direction", []))
			var local_direction := _sage_direction_to_local(source_direction)
			var light := DirectionalLight3D.new()
			light.name = "FordsAfternoon%s%s" % [domain.capitalize(), String(row.get("name", "light")).capitalize()]
			light.light_color = Color(source_color.x, source_color.y, source_color.z, 1.0)
			light.light_energy = 1.0
			light.light_cull_mask = int(domain_layers[domain])
			# Retail shadows use volumes and decals, while the current Godot
			# renderer exposes shadow maps. Do not imply equivalence.
			light.shadow_enabled = false
			light.basis = Basis.looking_at(local_direction, Vector3.UP)
			light.set_meta("retail_domain", domain)
			light.set_meta("retail_light_name", String(row.get("name", "")))
			light.set_meta("source_ambient", source_ambient)
			light.set_meta("source_color", source_color)
			light.set_meta("source_ray_direction_sage", source_direction)
			light.set_meta("source_ray_direction_local", local_direction)
			light.set_meta("oracle_sha256", FORDS_ENVIRONMENT_ORACLE_SHA256)
			add_child(light)
			source_environment_lights.append(light)


func _assign_battlefield_lighting_domains() -> void:
	if battlefield == null:
		return
	_assign_geometry_light_layer(battlefield, TERRAIN_LIGHT_LAYER)
	if battlefield.terrain_material is ShaderMaterial:
		var terrain_material := battlefield.terrain_material as ShaderMaterial
		var terrain_ambient := _source_ambient_sum("terrain")
		terrain_material.set_shader_parameter("sage_ambient_color", terrain_ambient)
		var terrain_rows: Array = FORDS_AFTERNOON_LIGHT_RIGS["terrain"]
		for index in range(terrain_rows.size()):
			var row := terrain_rows[index] as Dictionary
			terrain_material.set_shader_parameter("sage_light_%d_color" % index, _array_vector3(row.get("color", [])))
			terrain_material.set_shader_parameter("sage_light_%d_direction" % index, _sage_direction_to_local(_array_vector3(row.get("direction", []))))
		terrain_material.set_meta("sage_ambient_color", terrain_ambient)
		terrain_material.set_meta("sage_lighting_model", "opensage-terrain-three-light-lambert")
	var bound_props: Node = battlefield.find_child("SourceBoundRetailProps", true, false)
	if bound_props != null:
		_assign_geometry_light_layer(bound_props, OBJECT_LIGHT_LAYER)


func _source_ambient_sum(domain: String) -> Vector3:
	var result := Vector3.ZERO
	var rows: Array = FORDS_AFTERNOON_LIGHT_RIGS.get(domain, []) as Array
	for row_value in rows:
		result += _array_vector3((row_value as Dictionary).get("ambient", []))
	return result


func _assign_geometry_light_layer(root_node: Node, layer: int) -> void:
	var pending: Array[Node] = [root_node]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current is GeometryInstance3D:
			(current as GeometryInstance3D).layers = layer
		for child_value in current.get_children():
			if child_value is Node:
				pending.append(child_value as Node)


func _array_vector3(value: Variant) -> Vector3:
	var items: Array = value as Array if value is Array else []
	if items.size() != 3:
		return Vector3.ZERO
	return Vector3(float(items[0]), float(items[1]), float(items[2]))


func _sage_vector_to_local(source_vector: Vector3) -> Vector3:
	if source_map_data == null or not source_map_data.ready:
		return Vector3.ZERO
	var godot_horizontal := Vector2(source_vector.x, -source_vector.y)
	return Vector3(
		godot_horizontal.dot(source_map_data.local_axis_x),
		source_vector.z,
		godot_horizontal.dot(source_map_data.local_axis_z)
	)


func _sage_direction_to_local(source_direction: Vector3) -> Vector3:
	var local_direction := _sage_vector_to_local(source_direction)
	return local_direction.normalized() if local_direction.length_squared() > 0.0 else Vector3.DOWN


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	layer.name = "PlayerHudLayer"
	add_child(layer)
	member_health_overlay = MemberHealthOverlayScript.new()
	member_health_overlay.name = "RetailMemberHealthOverlay"
	layer.add_child(member_health_overlay)
	member_health_overlay.configure(self, camera, battalion_nodes)
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
	hud.train_requested.connect(_queue_selected_producer)
	hud.cancel_production_requested.connect(_cancel_selected_production)
	hud.attack_move_requested.connect(_arm_attack_move)
	hud.stop_requested.connect(_stop_selected_units)
	hud.stance_requested.connect(_toggle_selected_stance)
	hud.command_cap_changed.connect(func(value: int) -> void:
		simulation.command_point_cap = maxi(60, value)
		hud.set_feedback("Command point cap set to %d." % simulation.command_point_cap)
	)
	hud.powers_opened.connect(func() -> void:
		hud.refresh_powers(simulation.power_points(0), simulation.purchased_powers[0])
	)
	hud.power_purchase_requested.connect(func(power_id: String, cost: int) -> void:
		var result := simulation.purchase_power(0, power_id, cost)
		if bool(result.get("ok", false)):
			hud.set_feedback("Power acquired: %s." % power_id.trim_prefix("SBGood_").capitalize())
		else:
			hud.set_feedback("Cannot acquire power: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)
		hud.refresh_powers(simulation.power_points(0), simulation.purchased_powers[0])
	)
	hud.power_cast_requested.connect(func(cast_kind: String) -> void:
		power_cast_armed = cast_kind
		hud.set_feedback("Choose a target area for %s (right-click cancels)." % cast_kind.capitalize())
	)
	hud.weak_fortress_toggled.connect(func(value: bool) -> void:
		# Testing convenience: cap both fortresses at 1500 HP so matches
		# conclude quickly; unchecking restores full retail maximums.
		for team in [0, 1]:
			var fortress_id: int = simulation.fortress_id(team)
			if fortress_id == 0:
				continue
			var fortress: Dictionary = simulation.structure(fortress_id)
			var retail_maximum := int(fortress.get("retail_maximum_health", fortress.get("maximum_health", 5000)))
			fortress["retail_maximum_health"] = retail_maximum
			var new_maximum := 1500 if value else retail_maximum
			fortress["maximum_health"] = new_maximum
			fortress["health"] = mini(int(fortress.get("health", new_maximum)), new_maximum)
		hud.set_feedback("Weak fortresses %s." % ("enabled" if value else "disabled"))
		_sync_presentation()
	)
	hud.construct_requested.connect(_arm_construction)
	hud.music_volume_changed.connect(func(value: float) -> void:
		if audio_system != null: audio_system.set_music_volume(value, true)
	)
	hud.voice_volume_changed.connect(func(value: float) -> void:
		if audio_system != null: audio_system.set_voice_sfx_volume(value, true)
	)
	hud.mute_changed.connect(func(value: bool) -> void:
		if audio_system != null: audio_system.set_muted(value, true)
	)
	hud.ui_sound_requested.connect(func(event_id: String) -> void:
		if audio_system != null: audio_system.play_ui_event(event_id)
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
	var input_direction := Vector2.ZERO
	if Input.is_action_pressed("cam_left"):
		input_direction.x -= 1.0
	if Input.is_action_pressed("cam_right"):
		input_direction.x += 1.0
	if Input.is_action_pressed("cam_forward"):
		input_direction.y -= 1.0
	if Input.is_action_pressed("cam_back"):
		input_direction.y += 1.0
	if input_direction.length_squared() > 0.0:
		var forward := _camera_forward_local()
		var right := Vector2(-forward.y, forward.x)
		var movement := right * input_direction.x - forward * input_direction.y
		camera_focus += movement.normalized() * delta * OPENBFME_KEYBOARD_SCROLL_BASE_LOCAL_PER_SECOND * FORDS_CAMERA_SCROLL_SPEED_SCALAR
		_clamp_camera_focus()
	_apply_camera_transform()


func _nudge_camera_zoom(direction: int) -> void:
	var source_range := FORDS_CAMERA_MAX_HEIGHT_SOURCE - FORDS_CAMERA_MIN_HEIGHT_SOURCE
	var normalized_step := FORDS_CAMERA_ZOOM_STEP_SOURCE / source_range
	camera_zoom_target = clampf(camera_zoom_target + float(direction) * normalized_step, 0.0, 1.0)
	camera_zoom = camera_zoom_target
	_clamp_camera_focus()
	_apply_camera_transform()


func _center_camera_on(world_position: Vector2) -> void:
	camera_focus = world_position
	_clamp_camera_focus()
	_apply_camera_transform()


func _apply_camera_transform() -> void:
	if camera == null or source_map_data == null or not source_map_data.ready:
		return
	var source_height := lerpf(FORDS_CAMERA_MIN_HEIGHT_SOURCE, FORDS_CAMERA_MAX_HEIGHT_SOURCE, camera_zoom)
	# OpenSAGE TacticalView.UpdateCameraOffsetBasedOnZ uses
	# offset_y = -(offset_z / tan(CameraPitch)). CameraPitch is therefore the
	# optical-axis elevation above the horizontal plane, not an off-top-down angle.
	var source_depth := source_height / tan(deg_to_rad(FORDS_CAMERA_PITCH_ABOVE_HORIZONTAL_DEGREES))
	# The source yaw stays exact; camera_user_yaw is the player's retail-style
	# middle-mouse orbit on top of it (zero until the player rotates).
	var yaw := deg_to_rad(FORDS_CAMERA_YAW_DEGREES) + camera_user_yaw
	# At SAGE yaw zero the camera is south of its target and looks toward +Y.
	# Convert that exact Z-up offset through the map's established local basis.
	var source_offset := Vector3(sin(yaw) * source_depth, -cos(yaw) * source_depth, source_height)
	var local_offset := _sage_vector_to_local(source_offset) * source_map_data.local_transform_scale
	var target := Vector3(camera_focus.x, _bounded_camera_ground_local(), camera_focus.y)
	camera.look_at_from_position(target + local_offset, target, Vector3.UP)
	var camera_metadata := environment_runtime_metadata.get("camera", {}) as Dictionary
	camera_metadata["current_height_source"] = source_height
	camera_metadata["current_height_local"] = source_height * source_map_data.local_transform_scale
	camera_metadata["current_depth_source"] = source_depth
	camera_metadata["current_target_ground_local"] = target.y
	camera_metadata["current_source_offset"] = source_offset
	camera_metadata["current_local_offset"] = local_offset
	environment_runtime_metadata["camera"] = camera_metadata
	camera.set_meta("retail_camera", camera_metadata)


func _clamp_camera_focus() -> void:
	if source_map_data == null or not source_map_data.ready:
		camera_focus.x = clampf(camera_focus.x, -50.0, 50.0)
		camera_focus.y = clampf(camera_focus.y, -42.0, 42.0)
		return
	var bounds: Rect2 = source_map_data.local_bounds
	var maximum_inset := maxf(0.0, minf(bounds.size.x, bounds.size.y) * 0.5 - 0.001)
	var inset := minf(_camera_ground_constraint_inset(), maximum_inset)
	var minimum := bounds.position + Vector2(inset, inset)
	var maximum := bounds.end - Vector2(inset, inset)
	camera_focus.x = clampf(camera_focus.x, minimum.x, maximum.x)
	camera_focus.y = clampf(camera_focus.y, minimum.y, maximum.y)


func _bounded_camera_ground_local() -> float:
	if source_map_data == null or not source_map_data.ready or source_map_data.local_transform_scale <= 0.0:
		return 0.0
	var local_ground := source_map_data.local_ground_height(camera_focus)
	var source_ground := source_map_data.reference_elevation + local_ground / source_map_data.local_transform_scale
	var bounded_source_ground := clampf(source_ground, FORDS_CAMERA_GROUND_MIN_SOURCE, FORDS_CAMERA_GROUND_MAX_SOURCE)
	return (bounded_source_ground - source_map_data.reference_elevation) * source_map_data.local_transform_scale


func _camera_forward_local() -> Vector2:
	if source_map_data == null or not source_map_data.ready:
		return Vector2.UP
	var yaw := deg_to_rad(FORDS_CAMERA_YAW_DEGREES)
	var source_forward := Vector3(-sin(yaw), cos(yaw), 0.0)
	var local_forward_3d := _sage_direction_to_local(source_forward)
	var local_forward := Vector2(local_forward_3d.x, local_forward_3d.z)
	return local_forward.normalized() if local_forward.length_squared() > 0.0 else Vector2.UP


func _camera_ground_constraint_inset() -> float:
	if source_map_data == null or not source_map_data.ready:
		return 0.0
	var local_height := lerpf(FORDS_CAMERA_MIN_HEIGHT_SOURCE, FORDS_CAMERA_MAX_HEIGHT_SOURCE, camera_zoom) * source_map_data.local_transform_scale
	var optical_axis_elevation := deg_to_rad(FORDS_CAMERA_PITCH_ABOVE_HORIZONTAL_DEGREES)
	# OpenSAGE's source-derived constraint samples 95% of screen height. The
	# point is 90% of the center-to-bottom normalized projection coordinate.
	var bottom_ray_delta := atan(tan(FORDS_CAMERA_NAMED_FOV_RADIANS * 0.5) * 0.9)
	var bottom_ray_elevation := optical_axis_elevation - bottom_ray_delta
	if bottom_ray_elevation <= 0.0:
		return 0.0
	var center_distance := local_height / tan(deg_to_rad(FORDS_CAMERA_PITCH_ABOVE_HORIZONTAL_DEGREES))
	var bottom_distance := local_height / tan(bottom_ray_elevation)
	return maxf(0.0, bottom_distance - center_distance)


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
