class_name RetailVerticalSlice
extends Node3D
## Integrated private Men-versus-Men production slice. Authoritative gameplay is
## held by RetailSliceSim; Godot nodes interpolate and present that state only.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const LockstepSessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")
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
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const FactionManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")
const HouseColorScript = preload("res://src/retail_slice/retail_house_color.gd")
const OptionsScreenScript = preload("res://src/ui/options_screen.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")
const ControlServerScript = preload("res://src/debug/retail_control_server.gd")
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"
const RANGER_OBJECT_ID := "bfme2.object.gondor-ranger"
const RANGER_HORDE_ID := "bfme2.object.gondor-ranger-horde"
const TREBUCHET_OBJECT_ID := "bfme2.object.gondor-trebuchet"
const BUILDER_OBJECT_ID := "bfme2.object.men-porter"
const MAP_ID := "bfme2.map.fords-of-isen-ii"
## The five-maps supplement pack. The slice resolves non-default slice maps
## from this pack's catalog when ContentDB has not registered it yet (the
## integration step registers it in selection.json as a supplemental pack).
const FIVE_MAPS_PACK_ID := "bfme2-five-maps-106-private"
const MAP_CATALOG_MAX_BYTES := 1024 * 1024
const MAP_DOCUMENT_MAX_BYTES := 2 * 1024 * 1024
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
	RANGER_OBJECT_ID: "assets/models/m3/units/gondorranger.glb",
	TREBUCHET_OBJECT_ID: "assets/models/m3/units/gondortrebuchet.glb",
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
	RANGER_HORDE_ID: "Ithilien Rangers",
}
# Structure object ids now live on the faction manifest
# (RetailFactionManifest.DEFAULT_STRUCTURE_OBJECT_IDS carries the Men table).
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
var local_team := 0
var _local_command_seq := 0
var lockstep_session
## Debug-gated Game Control API (OPENBFME_CONTROL_PORT); null when env unset.
var control_server
var _mp_mode := OS.get_environment("OPENBFME_MP").strip_edges().to_lower()
var _mp_address := OS.get_environment("OPENBFME_MP_ADDRESS").strip_edges()
var _mp_port_text := OS.get_environment("OPENBFME_MP_PORT").strip_edges()


## Menu seam: the NETWORK flyout writes GameState.retail_mp_*; environment
## variables always win so headless runners stay authoritative. Called before
## the first _mp_mode consumer during match initialization. The optional
## override lets tests exercise the seam without tree membership.
func _resolve_mp_settings(game_state_override: Node = null) -> void:
	var game_state := game_state_override if game_state_override != null else get_node_or_null("/root/GameState")
	if game_state == null:
		return
	if _mp_mode == "":
		_mp_mode = String(game_state.get("retail_mp_mode")).strip_edges().to_lower()
	if _mp_address == "":
		_mp_address = String(game_state.get("retail_mp_address")).strip_edges()
	if _mp_port_text == "":
		var menu_port := int(game_state.get("retail_mp_port"))
		if menu_port > 0:
			_mp_port_text = str(menu_port)
var _mp_desync_reported := false
var _mp_last_pause_command_tick := -1
var battalion_nodes: Dictionary = {}
var structure_nodes: Dictionary = {}
var order_indicators: Dictionary = {}
var attack_target_indicator: RetailAttackTargetIndicator
var audio_system: RetailSliceAudio
var source_map_data: RetailMapData
var selected_pack_root := ""
var map_id := MAP_ID
var map_pack_root := ""
var faction_manifest: Dictionary = {}
var enemy_faction := ""
var gameplay_rules: Dictionary = {}
var ranger_runtime: Dictionary = {}
var trebuchet_runtime: Dictionary = {}
var playable_unit_runtimes: Dictionary = {}
## Honestly fieldable converted units for the active faction (resolved sim +
## supported category). Populated by _classify_faction_units.
var fieldable_unit_runtimes: Dictionary = {}
## Subset of fieldable units that may train from producers (excludes builders
## that only exist for construct routes without combat evidence).
var producible_unit_runtimes: Dictionary = {}
## Converted unit docs excluded from the roster with an explicit reason.
var unit_roster_exclusions: Array = []
var ranger_hud_configuration_error := ""
var trebuchet_hud_configuration_error := ""
var playable_unit_hud_configuration_error := ""
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
var construction_ghost: Node3D = null
var _drag_select_origin := Vector2.INF
var _drag_selecting := false
var _selection_band: Control = null
const DRAG_SELECT_THRESHOLD := 8.0
var camera_user_yaw := 0.0
var _last_backspace_ms := 0
var _right_drag_origin := Vector2.INF
var _right_dragging := false
## Runtime camera scroll-speed multiplier, driven by the options screen's
## Scroll Speed slider (options seam; the base rate constant stays untouched).
var keyboard_scroll_speed_scale := 1.0
var options_overlay = null
var _loaded_map_definition: Dictionary = {}
var _camera_orbiting := false
var power_cast_armed := ""
## Set when the current faction has no spellbook document in any mounted
## pack (fail closed; the sim locks the whole tree with spellbook-unavailable).
var _spellbook_resolution_note := ""
# Armed hero ability cast: {"hero_id": int, "ability_id": String, "unit_id": String}.
# Empty when no ability button armed a targeted cast.
var ability_cast_armed: Dictionary = {}
var _power_cast_ghost: MeshInstance3D
var _power_cast_glyph: MeshInstance3D
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
	_apply_stored_display_settings()
	faction_manifest = _resolve_faction_manifest()
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
	# Re-resolve after the potential reload so a data-driven faction manifest
	# always reflects the registries the match will actually run against.
	faction_manifest = _resolve_faction_manifest()
	if faction_manifest.has("_error"):
		_fail("Faction manifest failed: %s" % String(faction_manifest.get("_error", "")))
		return
	var player_faction := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION))
	enemy_faction = _resolve_enemy_faction(player_faction)
	if enemy_faction != player_faction:
		_fail("Enemy faction '%s' cannot be seeded yet: the simulation consumes one faction manifest ('%s') for both teams, so cross-faction matches need per-team manifests that are not implemented." % [enemy_faction, player_faction])
		return
	ranger_runtime = ContentDB.get_ranger_runtime()
	trebuchet_runtime = ContentDB.get_trebuchet_runtime()
	playable_unit_runtimes = ContentDB.get_playable_unit_runtimes()
	# Refresh faction chrome after the post-reload manifest resolution so a
	# full pack's buildings/heading reach the side command bar.
	if hud != null:
		if hud.has_method("configure_faction_surface"):
			hud.configure_faction_surface(faction_manifest)
		else:
			hud.configure_manifest_construct_kinds(faction_manifest.get("structure_kinds", []) as Array)
		if ContentDB.has_method("get_spellbook_runtime"):
			var spellbook_doc: Dictionary = _faction_spellbook_document()
			if not spellbook_doc.is_empty() and hud.has_method("configure_spellbook_runtime"):
				hud.configure_spellbook_runtime(spellbook_doc)
	if ranger_hud_configuration_error != "":
		_fail("Ranger HUD configuration failed: %s" % ranger_hud_configuration_error)
		return
	if trebuchet_hud_configuration_error != "":
		_fail("Trebuchet HUD configuration failed: %s" % trebuchet_hud_configuration_error)
		return
	if playable_unit_hud_configuration_error != "":
		_fail("Playable-unit HUD configuration failed: %s" % playable_unit_hud_configuration_error)
		return
	await _mark_initialization_phase("content")
	var member_definition := ContentDB.get_bundle_object(SOLDIER_OBJECT_ID)
	var horde_definition := ContentDB.get_bundle_object(SOLDIER_HORDE_ID)
	var soldier_capability_id := String(member_definition.get("animationCapabilityId", ""))
	var soldier_capability := ContentDB.get_animation_capability(soldier_capability_id)
	if member_definition.is_empty() or horde_definition.is_empty() or soldier_capability.is_empty():
		_fail("The private bfme2-men-vslice pack is not selected. Run run_importer.bat to build and select it.")
		return
	# Resolve the asserted host pack by id, not through the member document's
	# pack root: supplements carry their own copy of the shared base bundle
	# objects, so the document merge can legitimately resolve a shared id from
	# another pack while the host assertion must stay pinned to pack_id.
	var expected_pack_id := String(faction_manifest.get("pack_id", "bfme2-men-vslice"))
	selected_pack_root = _pack_root_for_id(expected_pack_id)
	if selected_pack_root == "":
		_fail("The selected content pack is not %s." % expected_pack_id)
		return
	map_id = _resolve_slice_map_id()
	if map_id == "":
		_fail("Slice map selection is not a well-formed retail map id. Use OPENBFME_SLICE_MAP=bfme2.map.<slug> (for example bfme2.map.fords-of-isen-ii).")
		return
	var map_definition := _resolve_slice_map_definition(map_id)
	if map_definition.is_empty():
		_fail("Slice map '%s' is unavailable: it is neither in the registered content nor in the %s pack catalog." % [map_id, FIVE_MAPS_PACK_ID])
		return
	_loaded_map_definition = map_definition.duplicate(true)
	map_pack_root = String(map_definition.get("_pack_root", ""))
	if map_pack_root == "" or not DirAccess.dir_exists_absolute(map_pack_root):
		_fail("Slice map '%s' has no usable pack root." % map_id)
		return
	# Identity lands as early as the map resolves: title, matchup, and player
	# rows. The map-art phase call below then adds the imported textures.
	_update_loading_overlay_identity(map_definition)
	_project_faction_structure_definitions()
	var profile_boot := OS.get_environment("OPENBFME_PROFILE_BOOT") == "1"
	var boot_mark := Time.get_ticks_msec()
	var presentation_definition_error := _load_required_presentation_definitions()
	if profile_boot:
		print("BOOT_PROFILE slice.presentation_definitions_ms=%d" % (Time.get_ticks_msec() - boot_mark))
		boot_mark = Time.get_ticks_msec()
	if presentation_definition_error != "":
		_fail("Faction roster presentation validation failed: %s" % presentation_definition_error)
		return
	_prewarm_boot_glb_cache()
	if profile_boot:
		print("BOOT_PROFILE slice.glb_prewarm_ms=%d" % (Time.get_ticks_msec() - boot_mark))
		boot_mark = Time.get_ticks_msec()
	# The bind below first-touches every faction icon/portrait; fan those cold
	# reads across the worker pool first so storage latency overlaps instead of
	# serializing into the retail_command_ui phase.
	var prefetched_count := ContentDB.prefetch_retail_ui_assets([selected_pack_root] + faction_manifest.get("faction_pack_roots", []) as Array)
	if profile_boot:
		print("BOOT_PROFILE ui_prefetch_count=%d slice.ui_prefetch_ms=%d" % [prefetched_count, Time.get_ticks_msec() - boot_mark])
		boot_mark = Time.get_ticks_msec()
	var hud_binding_error := hud.bind_retail_train_commands(
		ContentDB, selected_pack_root, true, faction_manifest.get("faction_pack_roots", []) as Array
	)
	if profile_boot:
		print("BOOT_PROFILE slice.hud_bind_ms=%d" % (Time.get_ticks_msec() - boot_mark))
	if hud_binding_error != "":
		_fail("Private Men production UI validation failed: %s" % hud_binding_error)
		return
	await _mark_initialization_phase("retail_command_ui")
	attack_target_indicator = AttackTargetIndicatorScript.new()
	attack_target_indicator.name = "AttackTargetIndicator"
	add_child(attack_target_indicator)
	attack_target_indicator.configure(hud.retail_action_texture("attack_move"))

	var asset_factory = load("res://src/view/asset_factory.gd")
	var preview_path := ContentDB.resolve_asset(String(map_definition.get("preview", "")), map_pack_root)
	var art_path := ContentDB.resolve_asset(String(map_definition.get("art", "")), map_pack_root)
	_preview_texture = asset_factory.load_texture_asset(preview_path)
	_source_art_texture = asset_factory.load_texture_asset(art_path)
	map_preview_loaded = _preview_texture != null
	map_art_loaded = _source_art_texture != null
	_update_loading_overlay_identity(map_definition)
	await _mark_initialization_phase("map_art")

	source_map_data = MapDataScript.new()
	if not source_map_data.load_from_pack(map_pack_root, map_definition):
		_fail("Cooked %s map data failed validation: %s" % [map_id, source_map_data.error])
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
	_apply_menu_match_options()
	_resolve_mp_settings()
	simulation = SimScript.new()
	_configure_simulation_spellbook()
	simulation.setup(_match_configuration(), gameplay_rules)
	if _mp_mode == "host" or _mp_mode == "join":
		local_team = 0 if _mp_mode == "host" else 1
		simulation.ai_enabled = false
		lockstep_session = LockstepSessionScript.new(simulation)
		var mp_port := 26015 if _mp_port_text == "" else int(_mp_port_text)
		var mp_address := "127.0.0.1" if _mp_address == "" else _mp_address
		var mp_error: Error = lockstep_session.host(mp_port) if _mp_mode == "host" else lockstep_session.join(mp_address, mp_port)
		if mp_error != OK:
			_fail("Lockstep %s failed (%s:%d, error %d)." % [_mp_mode, mp_address, mp_port, mp_error])
			return
	_configure_simulation_expansions()
	if OS.get_environment("OPENBFME_CONTROL_PORT").strip_edges() != "":
		if _mp_mode != "":
			# Control-API commands bypass the lockstep session and would desync
			# a networked match; MP support requires routing through
			# session.submit_local and is deliberately not wired yet.
			push_warning("Control API is disabled in multiplayer sessions")
		else:
			# The live frame loop drives this simulation, so external stepping
			# stays disallowed: the control API inspects and issues commands only.
			control_server = ControlServerScript.new(simulation, false)
			if not control_server.start_from_env():
				control_server = null
	var player_fortress_id := simulation.fortress_id(local_team)
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
	audio_system.configure(selected_pack_root, DisplayServer.get_name() != "headless", producible_unit_runtimes, _faction_structure_audio_contract(), _faction_eva_side())
	audio_system.set_declared_structure_lifecycle_audio_active(_all_men_structure_contracts_v1())
	await _mark_initialization_phase("audio")
	hud.configure_minimap(simulation, source_map_data, camera, _preview_texture)
	var command_costs: Dictionary = {}
	for unit_type in simulation.production_rule_ids():
		command_costs[unit_type] = simulation._production_rule_value(String(unit_type), "cost_rule", "default_cost")
	if not ranger_runtime.is_empty() and String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION)) == FactionManifestScript.DEFAULT_FACTION:
		command_costs["Upgrade_GondorArcheryRangeLevel2"] = int((ranger_runtime.get("prerequisite", {}) as Dictionary).get("cost", 0))
	for structure_kind in simulation.structure_build_rule_ids():
		command_costs[structure_kind] = int(simulation.structure_build_rule(structure_kind).get("cost", 0))
	for expansion_kind_value in simulation._expansion_build_rules.keys():
		var expansion_kind := String(expansion_kind_value)
		command_costs[expansion_kind] = int(simulation._expansion_build_rules[expansion_kind].get("cost", 0))
	hud.set_command_costs(command_costs)
	var command_point_costs: Dictionary = {}
	for unit_type in simulation.production_rule_ids():
		command_point_costs[unit_type] = simulation._production_rule_value(String(unit_type), "command_points_rule", "default_command_points")
	hud.set_command_point_costs(command_point_costs)
	var command_build_seconds: Dictionary = {}
	for unit_type in simulation.production_rule_ids():
		command_build_seconds[unit_type] = float(simulation._production_rule_value(String(unit_type), "build_time_rule", "default_build_ticks")) * SimScript.TICK_SECONDS
	for structure_kind in simulation.structure_build_rule_ids():
		command_build_seconds[structure_kind] = float(simulation.structure_build_rule(structure_kind).get("seconds", 0.0))
	for expansion_kind_value in simulation._expansion_build_rules.keys():
		var expansion_kind := String(expansion_kind_value)
		command_build_seconds[expansion_kind] = float(simulation._expansion_build_rules[expansion_kind].get("seconds", 0.0))
	hud.set_command_build_seconds(command_build_seconds)
	hud.apply_audio_values(audio_system.get_music_volume(), audio_system.get_voice_sfx_volume(), audio_system.is_muted())
	audio_system.sync_events(simulation.events)
	# Full-pack manifests seed fortresses only; constructable kinds stay on the
	# builder bar. Tiny Men pack still seeds its full five-building starter.
	var seed_kinds: Array = faction_manifest.get("seed_structure_kinds", faction_manifest.get("structure_kinds", [])) as Array
	var expected_structure_count := 2 * seed_kinds.size()
	var men_faction_slice := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION)) == FactionManifestScript.DEFAULT_FACTION
	ready_ok = (
		battalion_nodes.size() == simulation.initial_battalion_count()
		and _all_battalion_retail_visuals_loaded()
		and structure_nodes.size() == expected_structure_count
		and _all_structure_retail_visuals_loaded()
		and map_preview_loaded
		and map_art_loaded
		# Full Men packs field via playableUnit projections; the legacy soldier
		# equipment proof is host-pack Men tiny-slice only.
		and (equipment_proof_loaded or not men_faction_slice or _men_uses_full_pack_manifest())
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
		if structure_nodes.size() != expected_structure_count:
			failed_capabilities.append("structure_count=%d expected=%d" % [structure_nodes.size(), expected_structure_count])
		if not _all_structure_retail_visuals_loaded():
			var structure_failures: Array[String] = []
			for structure_id in simulation.structure_ids():
				var structure_node: RetailStructure = structure_nodes.get(structure_id)
				if structure_node == null:
					structure_failures.append("%d:missing-node" % structure_id)
				elif structure_node.contract_error != "":
					structure_failures.append("%d:%s" % [structure_id, structure_node.contract_error])
				elif structure_node.presentation_mode != "private-imported-lifecycle":
					structure_failures.append("%d:mode=%s" % [structure_id, structure_node.presentation_mode])
				elif not structure_node.retail_visual_loaded:
					structure_failures.append("%d:visual-not-loaded" % structure_id)
			failed_capabilities.append(
				"structure_retail_visuals[%s]" % ", ".join(structure_failures)
			)
		if not map_preview_loaded:
			failed_capabilities.append("map_preview")
		if not map_art_loaded:
			failed_capabilities.append("map_art")
		if not equipment_proof_loaded and men_faction_slice and not _men_uses_full_pack_manifest():
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
# rather than in even steps. Re-measured after the threaded-content boot wave
# (men, Fords of Isen II, this machine's headless harness; isengard tracks
# within ~10%).
const LOADING_PHASE_WEIGHTS := {
	"ready": 540, "environment": 12, "hud": 120, "content": 600,
	"retail_command_ui": 560, "map_art": 15, "map_data": 1050,
	"battlefield": 1400, "simulation": 70, "presentations": 60,
	"audio": 800, "ready_complete": 1,
}
const LOADING_SCREEN_SCENE := "res://scenes/retail_loading_screen.tscn"
var _loading_screen: CanvasLayer = null
var _loading_weight_done := 0.0
var _loading_weight_total := -1.0


func _build_loading_overlay() -> void:
	## Menu→match transitions arrive through the loading-boot scene, which
	## already shows the retail loading screen; adopt that instance instead of
	## stacking a second one. Direct launches and gate runners build their own.
	_loading_screen = get_tree().get_first_node_in_group("retail_loading_screen") as CanvasLayer
	if _loading_screen != null:
		return
	var packed := load(LOADING_SCREEN_SCENE)
	if packed == null:
		return
	_loading_screen = packed.instantiate()
	add_child(_loading_screen)


func _update_loading_overlay_identity(map_definition: Dictionary) -> void:
	## Once the map resolves, the load screen stops being generic: real map
	## name, the imported map art and preview, and the two player rows.
	if _loading_screen == null:
		return
	var display_name := String(map_definition.get("displayName", map_definition.get("name", "")))
	var player_count := int(map_definition.get("playerCount", 0))
	var map_title := display_name
	if display_name != "" and player_count > 0:
		map_title = "%s (%d)" % [display_name, player_count]
	var description := String(map_definition.get("description", ""))
	var player_faction := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION))
	if description == "" and player_faction != "":
		# Packs ship no authored map blurbs yet; the retail frame shows the
		# matchup instead of an invented lore paragraph.
		description = "%s versus %s" % [_faction_display_name(player_faction), _faction_display_name(enemy_faction)]
	_loading_screen.call("configure_identity", {
		"map_name": map_title,
		"description": description,
		"art": _source_art_texture,
		"preview": _preview_texture,
		"players": [
			{"name": "Player", "army": _faction_display_name(player_faction), "level": 1, "color": Color(0.36, 0.49, 0.79)},
			{"name": "Computer", "army": _faction_display_name(enemy_faction), "level": 1, "color": Color(0.75, 0.22, 0.17)},
		],
	})


func _faction_display_name(faction: String) -> String:
	return faction.capitalize() if faction != "" else ""


func _mark_initialization_phase(phase: String) -> void:
	var now := Time.get_ticks_msec()
	initialization_metrics_ms[phase] = now - _initialization_started_ms
	if DisplayServer.get_name() == "headless":
		print("RETAIL_INIT_PHASE name=%s delta_ms=%d total_ms=%d" % [phase, now - _initialization_last_ms, now - _initialization_started_ms])
	_initialization_last_ms = now
	if _loading_screen == null:
		return
	_loading_weight_done += float(LOADING_PHASE_WEIGHTS.get(phase, 10))
	if _loading_weight_total <= 0.0:
		_loading_weight_total = 0.0
		for weight in LOADING_PHASE_WEIGHTS.values():
			_loading_weight_total += float(weight)
	var ratio := clampf(_loading_weight_done / _loading_weight_total, 0.0, 1.0)
	# The screen's progress is monotonic: a menu-transition floor (scene fetch
	# share) and any later phase can never move the bar backward.
	_loading_screen.call("set_load_progress", ratio, "Loading %s..." % phase.replace("_", " "))
	if phase == "ready_complete":
		var screen := _loading_screen
		_loading_screen = null
		screen.call("fade_out_and_free")
	else:
		# Yield one frame so the bar actually renders between phases.
		await get_tree().process_frame


func _load_required_presentation_definitions() -> String:
	validated_battalion_capabilities.clear()
	equipment_proof_loaded = false
	var faction := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION))
	# The hard-coded Men roster block (core four, porter, ranger, trebuchet)
	# validates only the Men slice; other factions present entirely through
	# their converted playableUnit.* documents below.
	if faction == FactionManifestScript.DEFAULT_FACTION and not _men_uses_full_pack_manifest():
		# Legacy tiny Men pack: hard-coded core four + optional overlay contracts.
		var presentation_ids: Array[String] = PRESENTATION_UNIT_OBJECT_IDS.duplicate()
		if not ranger_runtime.is_empty() and not _playable_has_ranger():
			presentation_ids.append(RANGER_OBJECT_ID)
		if not trebuchet_runtime.is_empty():
			presentation_ids.append(TREBUCHET_OBJECT_ID)
		for object_id in presentation_ids:
			var expected_kind := "builder" if object_id == BUILDER_OBJECT_ID else "member"
			var model_error := _validate_retail_object_model(object_id, expected_kind, String(UNIT_MODEL_PATHS[object_id]))
			if model_error != "":
				return model_error
			if object_id == TREBUCHET_OBJECT_ID:
				validated_battalion_capabilities[object_id] = _trebuchet_animation_capability()
				continue
			var definition: Dictionary = ContentDB.get_bundle_object(object_id)
			var capability_id := String(definition.get("animationCapabilityId", ""))
			var capability: Dictionary = ContentDB.get_animation_capability(capability_id)
			if capability_id == "" or capability.is_empty() or String(capability.get("_pack_root", "")) != String(definition.get("_pack_root", "")):
				return "%s has no selected-pack animation capability" % object_id
			validated_battalion_capabilities[object_id] = _attach_equipment_proof(capability) if object_id == SOLDIER_OBJECT_ID else capability.duplicate(true)
	elif faction == FactionManifestScript.DEFAULT_FACTION and _men_uses_full_pack_manifest():
		# Full Men pack still needs the host pack's soldier equipment proof for
		# the Men host map presentation, when the soldier is among fieldable units.
		var soldier_def: Dictionary = ContentDB.get_bundle_object(SOLDIER_OBJECT_ID)
		if not soldier_def.is_empty():
			var soldier_cap_id := String(soldier_def.get("animationCapabilityId", ""))
			var soldier_cap: Dictionary = ContentDB.get_animation_capability(soldier_cap_id)
			if not soldier_cap.is_empty():
				validated_battalion_capabilities[SOLDIER_OBJECT_ID] = _attach_equipment_proof(soldier_cap)
	for runtime_value in fieldable_unit_runtimes.values():
		if typeof(runtime_value) != TYPE_DICTIONARY:
			return "playable-unit runtime registry contains a non-object"
		var runtime := runtime_value as Dictionary
		var member_id := PlayableUnitAdapter.runtime_member_id(runtime)
		# Shared retail units project the same member/capability ids from every
		# faction pack; resolve the presentation from the runtime's own pack so a
		# cohabiting pack's projection cannot pass or fail this faction's proof.
		var runtime_root := String(runtime.get("_pack_root", ""))
		var definition := ContentDB.get_bundle_object_for_pack(member_id, runtime_root)
		var capability := ContentDB.get_animation_capability_for_pack(String(definition.get("animationCapabilityId", "")), runtime_root)
		if (
			member_id == ""
			or definition.is_empty()
			or capability.is_empty()
			or runtime_root == ""
			or String(definition.get("_pack_root", "")) != runtime_root
			or String(capability.get("_pack_root", "")) != runtime_root
			or ContentDB.resolve_mesh_path(definition) == ""
		):
			return "%s generic playable-unit presentation is incomplete" % String(runtime.get("objectId", ""))
		validated_battalion_capabilities[member_id] = capability.duplicate(true)
	var structure_object_ids: Dictionary = faction_manifest.get("structure_object_ids", {}) as Dictionary
	for kind_value in faction_manifest.get("structure_kinds", []) as Array:
		var kind := String(kind_value)
		var structure_object_id := String(structure_object_ids.get(kind, ""))
		if structure_object_id == "":
			return "faction manifest declares structure kind '%s' without an object id" % kind
		var lifecycle_error := _validate_retail_structure_lifecycle(structure_object_id, kind)
		if lifecycle_error != "":
			return lifecycle_error
	return ""


var _glb_stream_queue: Array[String] = []
var _glb_stream_seen: Dictionary = {}


func _prewarm_boot_glb_cache() -> void:
	## Boot GLB strategy: parse only the boot-critical GLBs up front (the units
	## the simulation seeds at match start and the seed structures' intact + BIB
	## models), then stream the rest of the faction roster into the shared mesh
	## cache over the first post-boot frames (_pump_glb_stream). With 30+ unit
	## rosters and half-gigabyte structure model sets per faction, warming
	## everything would serialize tens of seconds of scene generation into boot.
	## All paths come from the same resolvers the validators just ran, and
	## AssetFactory still fails closed at the real load site for anything the
	## warm-up could not parse.
	var asset_factory = load("res://src/view/asset_factory.gd")
	var boot_paths: Array = []
	var starter_army := OS.get_environment("OPENBFME_STARTER_ARMY") == "1"
	var boot_object_ids: Dictionary = {}
	for entry_value in faction_manifest.get("spawn_roster", []) as Array:
		var entry := entry_value as Dictionary
		if not starter_army and not String(entry.get("anchor", "")).ends_with("builder"):
			continue
		boot_object_ids[String(entry.get("object_id", ""))] = true
		boot_object_ids[String(entry.get("unit_type", ""))] = true
	for runtime_value in fieldable_unit_runtimes.values():
		var runtime := runtime_value as Dictionary
		var member_id := PlayableUnitAdapter.runtime_member_id(runtime)
		var definition := ContentDB.get_bundle_object_for_pack(
			member_id,
			String(runtime.get("_pack_root", ""))
		)
		var mesh_path := ContentDB.resolve_mesh_path(definition)
		if mesh_path == "":
			continue
		if (
			boot_object_ids.has(member_id)
			or boot_object_ids.has(PlayableUnitAdapter.runtime_unit_id(runtime))
			or boot_object_ids.has(String(runtime.get("objectId", "")))
		):
			boot_paths.append(mesh_path)
		else:
			_glb_stream_offer(mesh_path)
	for object_id_value in validated_battalion_capabilities.keys():
		var object_id := String(object_id_value)
		var definition := ContentDB.get_bundle_object(object_id)
		var mesh_path := ContentDB.resolve_mesh_path(definition)
		if mesh_path == "":
			continue
		if boot_object_ids.has(object_id):
			boot_paths.append(mesh_path)
		else:
			_glb_stream_offer(mesh_path)
	var structure_object_ids: Dictionary = faction_manifest.get("structure_object_ids", {}) as Dictionary
	var seed_kinds: Array = faction_manifest.get("seed_structure_kinds", faction_manifest.get("structure_kinds", [])) as Array
	for kind_value in faction_manifest.get("structure_kinds", []) as Array:
		var kind := String(kind_value)
		var structure_object_id := String(structure_object_ids.get(kind, ""))
		if structure_object_id == "":
			continue
		var definition := ContentDB.get_bundle_object(structure_object_id)
		var definition_root := String(definition.get("_pack_root", ""))
		var presentation: Dictionary = definition.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		# Lifecycle aux models vary by faction (bib may be absent or null): the
		# warm-up is best-effort and never validates, so read defensively.
		var bib_visual: Dictionary = {}
		if typeof(lifecycle.get("bib")) == TYPE_DICTIONARY:
			var bib: Dictionary = lifecycle["bib"]
			if typeof(bib.get("visual")) == TYPE_DICTIONARY:
				bib_visual = bib["visual"]
		for relative in [String(presentation.get("model", "")), String(bib_visual.get("glb", ""))]:
			if relative == "":
				continue
			var resolved := ContentDB.resolve_asset(relative, definition_root)
			if resolved == "":
				continue
			if seed_kinds.has(kind):
				boot_paths.append(resolved)
			else:
				_glb_stream_offer(resolved)
	asset_factory.preload_models_threaded(boot_paths)


func _glb_stream_offer(mesh_path: String) -> void:
	if _glb_stream_seen.has(mesh_path):
		return
	_glb_stream_seen[mesh_path] = true
	_glb_stream_queue.append(mesh_path)


func _pump_glb_stream() -> void:
	## Post-boot roster streaming: a small threaded batch per frame keeps the
	## remaining faction units'/structures' scene generation off the boot path
	## while staying ahead of first production, which then loads from the warm
	## cache instead of reparsing on demand.
	if _glb_stream_queue.is_empty() or DisplayServer.get_name() == "headless":
		# Headless gate runners keep the established lazy-load behavior so their
		# per-frame timing evidence is unchanged; the stream is a windowed-play
		# boot optimization.
		return
	var batch: Array = []
	for index in mini(2, _glb_stream_queue.size()):
		batch.append(_glb_stream_queue[index])
	_glb_stream_queue = _glb_stream_queue.slice(mini(2, _glb_stream_queue.size()))
	var asset_factory = load("res://src/view/asset_factory.gd")
	asset_factory.preload_models_threaded(batch)


func _trebuchet_animation_capability() -> Dictionary:
	var models: Dictionary = (trebuchet_runtime.get("unit", {}) as Dictionary).get("models", {}) as Dictionary
	var clips: Dictionary = {}
	for source_value in models.get("animations", []) as Array:
		var source := String(source_value)
		clips[source.get_file().get_basename().to_lower()] = true
	var expected := {
		"idle": "gusiegtreb_idla",
		"move": "gusiegtreb_wlka",
		"attack": "gusiegtreb_atak",
	}
	for state in expected:
		if not clips.has(String(expected[state])):
			return {}
	return {
		"states": {
			"idle": {"clips": [expected["idle"]], "mode": "loop"},
			"move": {"clips": [expected["move"]], "mode": "loop"},
			"attack": {"clips": [expected["attack"]], "mode": "once", "useWeaponTiming": true},
			"attackRangedPre": {"clips": [expected["attack"]], "mode": "once"},
			"attackRangedFire": {"clips": [expected["attack"]], "mode": "once"},
			"death": {"clips": [], "mode": "once"},
		},
		"unresolvedAnimationTracks": 0,
		"source": "typed-trebuchet-runtime-contract",
	}


func _validate_retail_object_model(object_id: String, expected_kind: String, expected_model: String) -> String:
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	var definition_root := String(definition.get("_pack_root", ""))
	if definition.is_empty() or definition_root == "":
		return "%s is not registered by the selected pack" % object_id
	if object_id != RANGER_OBJECT_ID and definition_root != selected_pack_root:
		return "%s escaped the selected Men pack" % object_id
	if object_id == RANGER_OBJECT_ID:
		var overlay_pack: Dictionary = ModLoader._read_json(definition_root.path_join("pack.json")) as Dictionary
		if String(overlay_pack.get("id", "")) != "bfme2-men-ranger-overlay":
			return "%s is not registered by the bounded Ranger overlay" % object_id
		var horde := ContentDB.get_bundle_object(RANGER_HORDE_ID)
		var capability := ContentDB.get_animation_capability(String(definition.get("animationCapabilityId", "")))
		if (
			String(ranger_runtime.get("_pack_root", "")) != definition_root
			or String(horde.get("_pack_root", "")) != definition_root
			or String(capability.get("_pack_root", "")) != definition_root
			or String(ranger_runtime.get("_reviewed_content_sha256", "")) == ""
		):
			return "%s overlay content is not one reviewed coherent pack" % object_id
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
	var definition_root := String(definition.get("_pack_root", ""))
	if definition.is_empty() or definition_root == "":
		return "%s is not registered by the selected pack" % object_id
	# The host pack owns the Men structures; a data-driven faction's structures
	# arrive from the packs its manifest recorded.
	var faction_roots: Array = faction_manifest.get("faction_pack_roots", []) as Array
	if definition_root != selected_pack_root and not faction_roots.has(definition_root):
		return "%s escaped the selected and faction packs" % object_id
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
		int((faction_manifest.get("structure_max_health", {}) as Dictionary).get(structure_kind, 0))
	)
	if contract_error != "":
		return "%s lifecycle contract failed: %s" % [object_id, contract_error]
	var asset_error: String = StructureScript.preflight_lifecycle_assets(
		lifecycle,
		structure_kind,
		definition_root
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
	var faction := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION))
	var men_slice := faction == FactionManifestScript.DEFAULT_FACTION
	var full_men := men_slice and _men_uses_full_pack_manifest()
	var unit_rules: Dictionary = {}
	# The hard-coded Men core-four conversions validate only the legacy tiny
	# Men slice; full Men packs and other factions arrive through playableUnit
	# documents via playable_unit_runtimes on the rules dictionary.
	if men_slice and not full_men:
		var gameplay_unit_ids: Array[String] = UNIT_OBJECT_IDS.duplicate()
		if not ranger_runtime.is_empty() and not _playable_has_ranger():
			gameplay_unit_ids.append(RANGER_OBJECT_ID)
		for object_id in gameplay_unit_ids:
			var source_rules := (
				(ranger_runtime.get("unitRule", {}) as Dictionary)
				if object_id == RANGER_OBJECT_ID
				else ContentDB.get_retail_unit_rules(object_id)
			)
			var converted := _convert_retail_unit_rule(source_rules, tick_ms)
			if converted.has("_error"):
				return {"_error": "%s: %s" % [object_id, String(converted["_error"])]}
			if object_id == RANGER_OBJECT_ID:
				var ranger_member: Dictionary = (source_rules.get("member", {}) as Dictionary)
				converted["member_health"] = int((ranger_member.get("health", {}) as Dictionary).get("value", 0))
				# The converted core bundle proves bow presentation only. Keep the
				# source sword rule in provenance, but never activate it until its
				# authored transition/attack clips are converted and bound.
				converted["unsupported_close_weapon"] = String(converted.get("close_weapon_mode", "")) != ""
				converted["close_weapon_mode"] = ""
				converted["category"] = "ranged-infantry"
			elif object_id == "bfme2.object.gondor-knight":
				converted["category"] = "cavalry"
			elif object_id == "bfme2.object.gondor-archer":
				converted["category"] = "ranged-infantry"
			else:
				converted["category"] = String(converted.get("category", "infantry"))
			unit_rules[object_id] = converted
	# Men host pack may still carry a MenPorter bundle simulation (tiny pack).
	# Full Men packs only ship playableUnit.MenPorter — builder_unit_ids then
	# supply the rule. Never skip the data-driven path when ids match the
	# host constant but the host bundle simulation is absent (that bug
	# silently dropped the starting porter from spawn).
	var builder_definition := ContentDB.get_bundle_object(BUILDER_OBJECT_ID)
	var builder_simulation: Dictionary = builder_definition.get("simulation", {}) as Dictionary
	if not builder_definition.is_empty() and not builder_simulation.is_empty():
		unit_rules[BUILDER_OBJECT_ID] = {
			"horde_id": BUILDER_OBJECT_ID,
			"member_count": 1,
			"member_health": maxi(1, int(builder_simulation.get("health", 500))),
			"member_damage": 1,
			"speed": float(builder_simulation.get("speed", 60.0)) * source_map_data.local_transform_scale,
			"speed_source": float(builder_simulation.get("speed", 60.0)),
			"acceleration": 60.0 * source_map_data.local_transform_scale * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
			"acceleration_source": 60.0 * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
			"turn_rate_degrees_per_second": 360.0,
			"braking": 60.0 * source_map_data.local_transform_scale * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
			"braking_source": 60.0 * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
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
	elif men_slice and not full_men:
		return {"_error": "missing selected-pack MenPorter simulation contract"}
	# Data-driven builders (including MenPorter when only playableUnit exists).
	for builder_value in (faction_manifest.get("builder_unit_ids", []) as Array):
		var builder_id := String(builder_value)
		if builder_id == "" or unit_rules.has(builder_id):
			continue
		var faction_builder_rule := _faction_builder_unit_rule(builder_id)
		if faction_builder_rule.is_empty():
			return {"_error": "faction builder '%s' has no converted playableUnit document" % builder_id}
		unit_rules[builder_id] = faction_builder_rule
	var rules := {
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
	# Menu RULES tab seam: GameState carries the setup's Initial Resources and
	# Command Point Factor. -1 / 1.0 keep the authored values above.
	var game_state := get_node_or_null("/root/GameState")
	if game_state != null:
		var menu_resources := int(game_state.get("retail_initial_resources"))
		if menu_resources > 0:
			rules["starting_resources"] = menu_resources
		var menu_factor := float(game_state.get("retail_command_point_factor"))
		if menu_factor > 0.0 and not is_equal_approx(menu_factor, 1.0):
			rules["command_point_cap"] = maxi(60, int(roundi(float(rules["command_point_cap"]) * menu_factor)))
	rules["source_map_transform_scale"] = source_map_data.local_transform_scale
	var manifest_for_rules: Dictionary = faction_manifest.duplicate(true)
	# Retail skirmish start: fortress + porter only — the base is built, not
	# given. Gate runners set OPENBFME_STARTER_ARMY=1 to keep the legacy
	# pre-spawned battalions their combat checks are written against.
	if OS.get_environment("OPENBFME_STARTER_ARMY") != "1":
		var builder_only: Array = []
		for entry_value in manifest_for_rules.get("spawn_roster", []) as Array:
			var entry := entry_value as Dictionary
			if String(entry.get("anchor", "")).ends_with("builder"):
				builder_only.append(entry)
		if not builder_only.is_empty():
			manifest_for_rules["spawn_roster"] = builder_only
	rules["faction_manifest"] = manifest_for_rules
	# Overlay ranger contract only when the full pack did not already ship
	# GondorRanger as a playableUnit runtime document.
	if men_slice and not ranger_runtime.is_empty() and not _playable_has_ranger():
		rules["ranger_runtime"] = ranger_runtime.duplicate(true)
		if unit_rules.has(RANGER_OBJECT_ID):
			rules["ranger_unit_rule"] = (unit_rules[RANGER_OBJECT_ID] as Dictionary).duplicate(true)
	if men_slice and not trebuchet_runtime.is_empty() and not full_men:
		rules["trebuchet_runtime"] = trebuchet_runtime.duplicate(true)
	elif men_slice and not trebuchet_runtime.is_empty() and full_men:
		# Full pack may still carry the typed trebuchet contract alongside playable runtimes.
		rules["trebuchet_runtime"] = trebuchet_runtime.duplicate(true)
	if not producible_unit_runtimes.is_empty():
		rules["playable_unit_runtimes"] = producible_unit_runtimes.duplicate(true)
		rules["producer_kind_by_source_object"] = _producer_kind_registry()
		var horde_speed_overrides := _horde_speed_overrides()
		if not horde_speed_overrides.is_empty():
			rules["horde_speed_overrides"] = horde_speed_overrides
	return rules


func _horde_speed_overrides() -> Dictionary:
	## Retail hordes move at their horde LocomotorSet speed (menhordes.ini),
	## while playableUnit documents resolve the unit-object locomotor. For horde
	## containers the pack's retail unit rules carry the authoritative horde
	## speed; record it with provenance instead of silently using either.
	## Retail rules key legacy member ids, so pairing rides the horde
	## LocomotorSet's source scopeName (GondorFighterHorde etc.) against each
	## document's container object.
	var overrides: Dictionary = {}
	var scope_speeds: Dictionary = {}
	for entry_value in ContentDB.retail_unit_rules.values():
		var entry := entry_value as Dictionary
		var locomotor_set: Dictionary = ((entry.get("horde", {}) as Dictionary).get("locomotorSet", {}) as Dictionary)
		var speed_field: Dictionary = locomotor_set.get("speed", {}) as Dictionary
		var scope_name := String((speed_field.get("source", {}) as Dictionary).get("scopeName", ""))
		var speed_value: Variant = speed_field.get("value")
		if scope_name == "" or typeof(speed_value) not in [TYPE_INT, TYPE_FLOAT] or float(speed_value) <= 0.0:
			continue
		scope_speeds[scope_name.to_lower()] = {
			"speed": float(speed_value),
			"source": (speed_field.get("source", {}) as Dictionary).duplicate(true),
		}
	for document_value in producible_unit_runtimes.values():
		var document := document_value as Dictionary
		var simulation := PlayableUnitAdapter.simulation_rule(document)
		if simulation.is_empty() or int(simulation.get("member_count", 0)) <= 1:
			continue
		var container := String((document.get("registration", {}) as Dictionary).get("composition", {}).get("containerObjectId", ""))
		var match: Dictionary = scope_speeds.get(container.to_lower(), {}) as Dictionary
		if match.is_empty():
			continue
		overrides[PlayableUnitAdapter.runtime_member_id(document)] = {
			"speed": float(match.get("speed", 0.0)),
			"source": (match.get("source", {}) as Dictionary).duplicate(true),
			"unit_object_speed": float(simulation.get("speed_source", 0.0)),
		}
	return overrides


func _resolve_faction_manifest() -> Dictionary:
	## Faction selection is an explicit input. OPENBFME_SLICE_FACTION always
	## wins when set; otherwise the main menu's skirmish setup selection on the
	## GameState autoload is the fallback. Unset / "men" with empty playable
	## registries keeps the historical Men tiny-pack tables. When Men
	## playableUnit.* / playableStructure.* runtimes are loaded, Men uses the
	## same data-driven from_registries path as other factions. Non-Men values
	## are lowercase source object-id prefixes resolved purely from the loaded
	## registries, failing closed when content is missing.
	var faction := OS.get_environment("OPENBFME_SLICE_FACTION").strip_edges().to_lower()
	if faction == "":
		var game_state := get_node_or_null("/root/GameState")
		if game_state != null:
			faction = String(game_state.get("retail_player_faction")).strip_edges().to_lower()
	if faction == "":
		faction = FactionManifestScript.DEFAULT_FACTION
	_classify_faction_units(faction)
	# Only honestly fieldable units reach the manifest: the manifest gate is
	# deliberately fail-closed for anything it can see, so unfieldable
	# documents stay out here with their recorded exclusion reason instead.
	# Men with empty registries returns default_manifest() inside from_registries.
	return FactionManifestScript.from_registries(
		faction,
		fieldable_unit_runtimes,
		ContentDB.get_playable_structure_runtimes()
	)


func _men_uses_full_pack_manifest() -> bool:
	## True when Men resolved through converted playable runtimes rather than
	## the hardcoded tiny-pack default tables.
	return (
		String(faction_manifest.get("faction", "")) == FactionManifestScript.DEFAULT_FACTION
		and not (faction_manifest.get("faction_pack_roots", []) as Array).is_empty()
	)


func _playable_has_ranger() -> bool:
	## GondorRanger may arrive as a playableUnit document (full pack) rather
	## than the separate ranger overlay contract.
	for object_id_value in fieldable_unit_runtimes.keys():
		var object_id := String(object_id_value)
		if object_id.to_lower() in ["gondorranger", "gondorrangerhorde"]:
			return true
		var document := fieldable_unit_runtimes[object_id_value] as Dictionary
		var runtime_unit := PlayableUnitAdapter.runtime_unit_id(document)
		if runtime_unit == RANGER_HORDE_ID or runtime_unit == RANGER_OBJECT_ID:
			return true
		if PlayableUnitAdapter.runtime_member_id(document) == RANGER_OBJECT_ID:
			return true
	return false


func _classify_faction_units(faction: String) -> void:
	## Roster composition for the selected faction: each converted playableUnit
	## document is either fieldable (resolved simulation evidence, a supported
	## category, a producer this faction slice loads) or excluded with the
	## exact reason recorded. Builder candidates stay available to the
	## manifest's builder discovery even when their own combat numbers are
	## unresolved — a porter needs movement, not a weapon.
	fieldable_unit_runtimes.clear()
	producible_unit_runtimes.clear()
	unit_roster_exclusions.clear()
	var slug := faction if faction != "" else FactionManifestScript.DEFAULT_FACTION
	var prefixes: Array = ((FactionManifestScript.FACTION_OBJECT_PREFIXES as Dictionary).get(slug, [slug]) as Array).duplicate()
	if slug == FactionManifestScript.DEFAULT_FACTION:
		# Mirror the manifest: Rohan allies may ship inside a Men pack, so Men
		# scope includes Rohan documents whenever any are present.
		var runtimes_for_scope: Dictionary = ContentDB.get_playable_unit_runtimes()
		var has_rohan := false
		for scope_id_value in runtimes_for_scope.keys():
			if String(scope_id_value).to_lower().begins_with("rohan"):
				has_rohan = true
				break
		if not has_rohan:
			for scope_id_value in ContentDB.get_playable_structure_runtimes().keys():
				if String(scope_id_value).to_lower().begins_with("rohan"):
					has_rohan = true
					break
		if has_rohan and not prefixes.has("rohan"):
			prefixes.append("rohan")
	var structure_runtimes: Dictionary = ContentDB.get_playable_structure_runtimes()
	var builder_candidates: Dictionary = {}
	for structure_value in structure_runtimes.values():
		var structure := structure_value as Dictionary
		var structure_id := String(structure.get("objectId", ""))
		var structure_in_scope := false
		for prefix_value in prefixes:
			if structure_id.to_lower().begins_with(String(prefix_value)):
				structure_in_scope = true
				break
		if not structure_in_scope:
			continue
		var production: Dictionary = ((structure.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)
		if String(production.get("evidence", "")) != "authored-construct-command":
			continue
		for route_value in production.get("routes", []) as Array:
			builder_candidates[String((route_value as Dictionary).get("builderObjectId", "")).to_lower()] = true
	# Shared retail units (MordorWorker) ship one document per faction pack in
	# the flat registry; scope to this faction's own pack copies exactly as the
	# manifest does so fieldability, production rules, and the HUD all read the
	# same producer evidence.
	var runtimes: Dictionary = FactionManifestScript.faction_scoped_unit_runtimes(
		prefixes,
		ContentDB.get_playable_unit_runtimes(),
		structure_runtimes,
		ContentDB.get_playable_unit_runtime_pack_index()
	)
	var object_ids: Array[String] = []
	for value in runtimes.keys():
		object_ids.append(String(value))
	object_ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for object_id in object_ids:
		var in_scope := false
		for prefix_value in prefixes:
			if object_id.to_lower().begins_with(String(prefix_value)):
				in_scope = true
				break
		if not in_scope:
			unit_roster_exclusions.append({
				"object_id": object_id,
				"category": String((runtimes[object_id] as Dictionary).get("category", "")),
				"reason": "outside-faction-object-scope",
			})
			continue
		var document := runtimes[object_id] as Dictionary
		if slug == FactionManifestScript.DEFAULT_FACTION and object_id.to_lower().begins_with("rohan"):
			# Mirror the manifest: Rohan allies only join a Men roster when
			# produced at Men/Gondor structures; a Rohan object produced at
			# another faction's structure is that faction's content.
			var has_men_producer := false
			for binding in PlayableUnitAdapter.producer_bindings(document):
				var binding_source := String(binding.get("producer_source_object_id", "")).to_lower()
				if binding_source.begins_with("men") or binding_source.begins_with("gondor"):
					has_men_producer = true
					break
			if not has_men_producer:
				unit_roster_exclusions.append({
					"object_id": object_id,
					"category": String(document.get("category", "")),
					"reason": "producer-outside-faction-scope",
				})
				continue
		if builder_candidates.has(object_id.to_lower()):
			fieldable_unit_runtimes[object_id] = document
			# A builder with an authored producer route (retail: the fortress
			# trains porters) is also producible — excluding it here silently
			# removed the train-porter command from the fortress.
			if not PlayableUnitAdapter.producer_bindings(document).is_empty():
				producible_unit_runtimes[object_id] = document
			continue
		var verdict := PlayableUnitAdapter.fieldability(document)
		if not bool(verdict.get("ok", false)):
			unit_roster_exclusions.append({
				"object_id": object_id,
				"category": String(document.get("category", "")),
				"reason": String(verdict.get("reason", "")),
			})
			continue
		fieldable_unit_runtimes[object_id] = document
		producible_unit_runtimes[object_id] = document


func _faction_builder_unit_rule(builder_member_id: String) -> Dictionary:
	## The faction builder's unit rule from its converted playableUnit document:
	## movement and vision come from the document's resolved values; combat is
	## the unarmed MenPorter shape (zero range, nominal damage) because retail
	## porters never fight. Missing document health keeps the retail porter
	## value and says so in provenance.
	for document_value in fieldable_unit_runtimes.values():
		var document := document_value as Dictionary
		if PlayableUnitAdapter.runtime_member_id(document) != builder_member_id:
			continue
		var simulation: Dictionary = ((document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary)
		var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
		var speed_source := _resolved_document_number(resolved.get("speed"), 60.0)
		var vision_source := _resolved_document_number(resolved.get("visionRange"), 25.0)
		var health := int(_resolved_document_number(resolved.get("memberHealth"), 500.0))
		var defaults_used: Array[String] = []
		if resolved.get("memberHealth") == null:
			defaults_used.append("memberHealth=500")
		return {
			"horde_id": builder_member_id,
			"member_count": 1,
			"member_health": maxi(1, health),
			"member_damage": 1,
			"speed": speed_source * source_map_data.local_transform_scale,
			"speed_source": speed_source,
			"acceleration": 60.0 * source_map_data.local_transform_scale,
			"acceleration_source": 60.0,
			"turn_rate_degrees_per_second": 360.0,
			"braking": 60.0 * source_map_data.local_transform_scale,
			"braking_source": 60.0,
			"attack_range": 0.0,
			"attack_range_source": 0.0,
			"minimum_attack_range": 0.0,
			"minimum_attack_range_source": 0.0,
			"vision_range": vision_source * source_map_data.local_transform_scale,
			"vision_range_source": vision_source,
			"delay_between_shots_ms": 1000.0,
			"pre_attack_delay_ms": 0.0,
			"firing_duration_ms": 0.0,
			"attack_period_ticks": 10,
			"pre_attack_ticks": 0,
			"firing_duration_ticks": 0,
			"formation_positions": [Vector3.ZERO],
			"stances": {"default": "Battle", "cycleOrder": ["HoldGround", "Battle", "Aggressive"], "states": {"HoldGround": {}, "Battle": {}, "Aggressive": {}}},
			"is_builder": true,
			"provenance": {
				"source": "playable-unit-runtime:%s" % String(document.get("objectId", "")),
				"document_defaults": defaults_used,
				"combat_shape": "unarmed-porter-shape",
			},
		}
	return {}


func _resolved_document_number(value: Variant, fallback: float) -> float:
	var raw: Variant = (value as Dictionary).get("value") if typeof(value) == TYPE_DICTIONARY else value
	if typeof(raw) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(raw)) and float(raw) > 0.0:
		return float(raw)
	return fallback


func _resolve_enemy_faction(player_faction: String) -> String:
	## Both teams are seeded from the player's faction manifest today: the
	## simulation consumes a single manifest for both teams' structures,
	## rosters, and AI plan. The menu's enemy selection is honored only when
	## it matches the player faction; a differing choice fails closed in
	## _initialize_content_and_match() instead of silently seeding Men.
	## OPENBFME_SLICE_FACTION keeps its historical single-faction behavior.
	if OS.get_environment("OPENBFME_SLICE_FACTION").strip_edges() != "":
		return player_faction
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null:
		return player_faction
	var selected := String(game_state.get("retail_enemy_faction")).strip_edges().to_lower()
	return selected if selected != "" else player_faction


func _pack_root_for_id(pack_id: String) -> String:
	## Deterministic host-pack resolution: the registered root whose pack.json
	## declares the id, or "" when no registered pack carries it.
	for root in ModLoader.list_pack_roots():
		var data := ModLoader._read_json(root.path_join("pack.json")) as Dictionary
		if String(data.get("id", "")) == pack_id:
			return root
	return ""


func _resolve_slice_map_id() -> String:
	## Map selection mirrors faction selection: OPENBFME_SLICE_MAP always wins;
	## otherwise the main menu's GameState.retail_map_id applies; unset keeps
	## the historical Fords of Isen II default. Anything that is not a
	## well-formed bfme2.map.<slug> id fails closed (empty string).
	var resolved := OS.get_environment("OPENBFME_SLICE_MAP").strip_edges().to_lower()
	if resolved == "":
		var game_state := get_node_or_null("/root/GameState")
		if game_state != null:
			var state_value: Variant = game_state.get("retail_map_id")
			if typeof(state_value) == TYPE_STRING:
				resolved = String(state_value).strip_edges().to_lower()
	if resolved == "":
		return MAP_ID
	return resolved if _is_well_formed_slice_map_id(resolved) else ""


func _is_well_formed_slice_map_id(value: String) -> bool:
	if value.length() < len("bfme2.map.a") or value.length() > 128 or not value.begins_with("bfme2.map."):
		return false
	for index in range(value.length()):
		var codepoint := value.unicode_at(index)
		if not (codepoint >= 97 and codepoint <= 122) and not (codepoint >= 48 and codepoint <= 57) and codepoint not in [45, 46]:
			return false
	return not value.contains("..") and not value.ends_with(".") and not value.ends_with("-")


func _resolve_slice_map_definition(resolved_map_id: String) -> Dictionary:
	## The default slice map is the selected faction pack's declared entry map;
	## resolving it directly from that pack keeps the Fords boot byte-exact even
	## when a supplemental map pack also registers the same map id. Other maps
	## come from the registered content first, then from the five-maps pack
	## catalog (the pack is not yet registered in selection.json).
	if resolved_map_id == MAP_ID:
		return _resolve_pack_entry_map_definition(selected_pack_root, resolved_map_id)
	var registered := ContentDB.get_bundle_map(resolved_map_id)
	if not registered.is_empty():
		return registered
	return _resolve_five_maps_catalog_definition(resolved_map_id)


func _resolve_pack_entry_map_definition(pack_root: String, expected_map_id: String) -> Dictionary:
	var pack_meta := ModLoader._read_json(pack_root.path_join("pack.json")) as Dictionary
	var entry_relative := String((pack_meta.get("files", {}) as Dictionary).get("entryMap", ""))
	if entry_relative == "" or not ModLoader.is_safe_relative_path(entry_relative):
		return {}
	var map_doc := _read_bounded_pack_document(pack_root, entry_relative, MAP_DOCUMENT_MAX_BYTES)
	if map_doc.is_empty() or String(map_doc.get("schema", "")) != "openbfme.map" or int(map_doc.get("schemaVersion", -1)) != 0:
		return {}
	if String(map_doc.get("id", "")) != expected_map_id:
		return {}
	map_doc["_source"] = ModLoader.resolve_pack_path(pack_root, entry_relative)
	map_doc["_pack_root"] = pack_root
	return map_doc


func _resolve_five_maps_catalog_definition(resolved_map_id: String) -> Dictionary:
	## Verification path for the not-yet-registered five-maps pack. Mirrors
	## ContentDB._load_map_catalog's row merge exactly: catalog-row discovery
	## metadata is retained while the cooked map document is authoritative for
	## the fields it defines. Once the pack is registered as a supplement,
	## ContentDB.get_bundle_map resolves these ids and this path is unreachable.
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if content_root == "" or not DirAccess.dir_exists_absolute(content_root):
		return {}
	var pack_root := ModLoader.resolve_pack_path(content_root, FIVE_MAPS_PACK_ID)
	if pack_root == "" or not ModLoader.path_is_within(content_root, pack_root):
		return {}
	var catalog := _read_bounded_pack_document(pack_root, "data/maps.json", MAP_CATALOG_MAX_BYTES)
	if String(catalog.get("schema", "")) != "openbfme.map-catalog" or int(catalog.get("schemaVersion", -1)) != 0:
		return {}
	var rows: Variant = catalog.get("maps", null)
	if typeof(rows) != TYPE_ARRAY:
		return {}
	for row_value in rows as Array:
		var row := row_value as Dictionary
		if row == null or String(row.get("id", "")) != resolved_map_id:
			continue
		var map_relative := String(row.get("map", ""))
		if map_relative == "" or not ModLoader.is_safe_relative_path(map_relative):
			return {}
		var map_doc := _read_bounded_pack_document(pack_root, map_relative, MAP_DOCUMENT_MAX_BYTES)
		if map_doc.is_empty() or String(map_doc.get("schema", "")) != "openbfme.map" or int(map_doc.get("schemaVersion", -1)) != 0:
			return {}
		if String(map_doc.get("id", "")) != resolved_map_id:
			return {}
		var merged := row.duplicate(true)
		merged.merge(map_doc, true)
		merged["map"] = map_relative
		merged["_source"] = ModLoader.resolve_pack_path(pack_root, map_relative)
		merged["_pack_root"] = pack_root
		return merged
	return {}


func _read_bounded_pack_document(pack_root: String, relative: String, maximum_bytes: int) -> Dictionary:
	if relative == "" or maximum_bytes <= 0:
		return {}
	var path := ModLoader.resolve_pack_path(pack_root, relative)
	if path == "" or not ModLoader.path_is_within(pack_root, path) or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	file.close()
	var raw: Variant = ModLoader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


func _project_faction_structure_definitions() -> void:
	## Playable-structure documents describe their own lifecycle presentation;
	## ContentDB only projects bundle objects for units. The slice is the
	## composition root, so it projects the faction manifest's structure object
	## ids into bundle-object definitions (the exact shape RetailStructure
	## consumes) from the converted documents. Legacy tiny Men packs keep their
	## pack-authored definitions untouched (empty faction_pack_roots). Full Men
	## packs and every other faction project from playableStructure documents.
	if (
		String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION)) == FactionManifestScript.DEFAULT_FACTION
		and (faction_manifest.get("faction_pack_roots", []) as Array).is_empty()
	):
		return
	var structure_runtimes: Dictionary = ContentDB.get_playable_structure_runtimes()
	var documents_by_runtime_id: Dictionary = {}
	for source_value in structure_runtimes.keys():
		var document := structure_runtimes[source_value] as Dictionary
		documents_by_runtime_id[PlayableUnitAdapter._runtime_id(String(source_value))] = document
	for kind_value in faction_manifest.get("structure_kinds", []) as Array:
		var kind := String(kind_value)
		var object_id := String((faction_manifest.get("structure_object_ids", {}) as Dictionary).get(kind, ""))
		if object_id == "" or not documents_by_runtime_id.has(object_id):
			continue
		var document := documents_by_runtime_id[object_id] as Dictionary
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var presentation: Dictionary = registration.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		if lifecycle.is_empty():
			continue
		# Prefer an already-authored bundle definition when present (tiny Men
		# workshop etc.); only project missing lifecycle presentation.
		if ContentDB.bundle_objects.has(object_id):
			var existing: Dictionary = ContentDB.bundle_objects[object_id] as Dictionary
			var existing_presentation: Dictionary = existing.get("presentation", {}) as Dictionary
			if typeof(existing_presentation.get("buildingLifecycle")) == TYPE_DICTIONARY:
				continue
		ContentDB.bundle_objects[object_id] = {
			"id": object_id,
			"kind": "structure",
			"sourceObjectId": String(document.get("objectId", "")),
			"presentation": {
				"model": StructureScript._intact_visual_path(lifecycle),
				"buildingLifecycle": lifecycle,
			},
			"_source": String(document.get("_source", "")),
			"_pack_root": String(document.get("_pack_root", "")),
		}


func _producer_kind_registry() -> Dictionary:
	# This registry describes the structures actually instantiated by this
	# faction slice. A descriptor whose retail producer is not present fails
	# closed instead of being attached to an unrelated building. The Men table
	# lives on the default faction manifest.
	var registry: Dictionary = faction_manifest.get("producer_kind_registry", {}) as Dictionary
	if registry.is_empty():
		registry = FactionManifestScript.DEFAULT_PRODUCER_KIND_REGISTRY
	return registry.duplicate(true)


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
	var default_mode: Dictionary = weapon_modes["default"] as Dictionary
	var switch_distance_source := 0.0
	var switch_value: Variant = member.get("dualWeaponSwitchDistance", {})
	if typeof(switch_value) == TYPE_DICTIONARY and bool((switch_value as Dictionary).get("defined", true)):
		switch_distance_source = _retail_rule_number(switch_value)
		if not is_finite(switch_distance_source) or switch_distance_source < 0.0:
			return {"_error": "invalid dual weapon switch distance"}
	# Category drives fail-closed features (e.g. cavalry trample). Map the known
	# Men core-four object ids; unknown units stay uncategorized.
	var horde_id := String(source_rules.get("hordeId", ""))
	var category := ""
	if horde_id in ["GondorKnightHorde", "bfme2.object.gondor-knight"] or String(source_rules.get("memberId", "")).to_lower().contains("knight"):
		category = "cavalry"
	elif horde_id in ["GondorArcherHorde", "GondorRangerHorde"] or String(source_rules.get("memberId", "")).to_lower().contains("archer"):
		category = "ranged-infantry"
	elif horde_id != "":
		category = "infantry"
	return {
		"horde_id": horde_id,
		"category": category,
		"speed": speed_raw * source_map_data.local_transform_scale,
		"speed_source": speed_raw,
		# HORDE_LOCOMOTION_RESPONSE_SCALE snappens proven accel/braking slightly
		# (not a retail number claim; see playable_unit_runtime_adapter.gd).
		"acceleration": acceleration_raw * source_map_data.local_transform_scale * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
		"acceleration_source": acceleration_raw * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
		"turn_rate_degrees_per_second": turn_rate_raw,
		"braking": braking_raw * source_map_data.local_transform_scale * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
		"braking_source": braking_raw * PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE,
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
		"attack_period_ticks": int(default_mode.get("attack_period_ticks", 1)),
		"clip_size": int(default_mode.get("clip_size", 0)),
		"clip_reload_time_ms": float(default_mode.get("clip_reload_time_ms", 0.0)),
		"continuous_fire_one": int(default_mode.get("continuous_fire_one", 0)),
		"continuous_fire_coast_ticks": int(default_mode.get("continuous_fire_coast_ticks", 0)),
		"continuous_fire_rate_multiplier": float(default_mode.get("continuous_fire_rate_multiplier", 1.0)),
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
	var clip: Dictionary = weapon.get("clip", {}) as Dictionary
	var clip_size := int(clip.get("size", 0))
	var clip_reload_ms := float(clip.get("reloadTimeMs", 0.0))
	var continuous_fire_one := int(clip.get("continuousFireOne", 0))
	var continuous_fire_coast_ms := float(clip.get("continuousFireCoastMs", 0.0))
	var continuous_fire_rate_percent := float(clip.get("continuousFireRatePercent", 100.0))
	if (
		clip_size < 0 or clip_reload_ms < 0.0 or continuous_fire_one < 0
		or continuous_fire_coast_ms < 0.0 or continuous_fire_rate_percent < 100.0
	):
		return {"_error": "invalid retail weapon clip field"}
	# A one-round clip reloads before the next PreAttack. DelayBetweenShots is
	# only the transition used while rounds remain in the same clip.
	var period_ms := pre_attack_ms + firing_ms + (clip_reload_ms if clip_size == 1 else delay_ms)
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
		"clip_size": clip_size,
		"clip_reload_time_ms": clip_reload_ms,
		"continuous_fire_one": continuous_fire_one,
		"continuous_fire_coast_ticks": maxi(0, roundi(continuous_fire_coast_ms / tick_ms)),
		"continuous_fire_rate_multiplier": continuous_fire_rate_percent / 100.0,
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
	# The lifecycle route registry may resolve from the host pack and every
	# pack the faction manifest recorded (mirrors the HUD's multi-pack seam).
	var allowed_roots: Array = [selected_pack_root]
	allowed_roots.append_array(faction_manifest.get("faction_pack_roots", []) as Array)
	structure.set_allowed_pack_roots(allowed_roots)
	var structure_object_id := String((faction_manifest.get("structure_object_ids", {}) as Dictionary).get(kind, ""))
	if structure_object_id == "":
		# Fortress expansion structures resolve from their expansion documents.
		structure_object_id = String((_expansion_object_ids.get(kind, {}) as Dictionary).get("object_id", ""))
	structure.configure(entity, structure_object_id, source_map_data.local_transform_scale)
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
		if structure == null:
			return false
		if structure.contract_error != "":
			return false
		if structure.presentation_mode != "private-imported-lifecycle":
			return false
		if not structure.retail_visual_loaded:
			return false
	return true


func _all_men_structure_contracts_v1() -> bool:
	for object_id_value in (faction_manifest.get("structure_object_ids", {}) as Dictionary).values():
		var definition: Dictionary = ContentDB.get_bundle_object(String(object_id_value))
		var presentation: Dictionary = definition.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		if int(lifecycle.get("schemaVersion", -1)) != StructureScript.LIFECYCLE_SCHEMA_VERSION_V1:
			return false
	return true


const FACTION_EVA_SIDES: Dictionary = {
	"men": "Men",
	"elves": "Elves",
	"dwarves": "Dwarves",
	"isengard": "Isengard",
	"mordor": "Mordor",
	"wild": "Wild",
}


func _faction_eva_side() -> String:
	return String(FACTION_EVA_SIDES.get(String(faction_manifest.get("faction", "")), ""))


func _faction_structure_audio_contract() -> Dictionary:
	## Project each structure kind's converted audio evidence (select, damage
	## stages, collapse, EVA routes, damaged-band fractions) from its
	## playable-structure document, plus the global eva.ini side map when a
	## mounted pack ships it. Kinds without document evidence keep the audio
	## layer's legacy generic routing.
	var contract := {
		"select": {},
		"damaged": {},
		"really_damaged": {},
		"collapse": {},
		"damaged_fraction": {},
		"really_damaged_fraction": {},
		"eva_damaged": {},
		"eva_die": {},
		"eva_events": {},
	}
	var structure_object_ids: Dictionary = faction_manifest.get("structure_object_ids", {}) as Dictionary
	var docs_by_runtime_id := {}
	for object_id_value in ContentDB.get_playable_structure_runtimes().keys():
		var document: Dictionary = ContentDB.get_playable_structure_runtime(String(object_id_value))
		docs_by_runtime_id[PlayableUnitAdapter._runtime_id(String(document.get("objectId", "")))] = document
	for kind_value in structure_object_ids.keys():
		var kind := String(kind_value)
		var document: Dictionary = docs_by_runtime_id.get(String(structure_object_ids[kind_value]), {})
		if document.is_empty():
			continue
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var presentation: Dictionary = registration.get("presentation", {}) as Dictionary
		var routes: Dictionary = presentation.get("audioRoutes", {}) as Dictionary
		var select_id := _first_audio_route_id(routes.get("VoiceSelect", []))
		if select_id != "":
			(contract["select"] as Dictionary)[kind] = select_id
		var damaged_id := _first_audio_route_id(routes.get("SoundOnDamaged", []))
		if damaged_id != "":
			(contract["damaged"] as Dictionary)[kind] = damaged_id
		var really_id := _first_audio_route_id(routes.get("SoundOnReallyDamaged", []))
		if really_id != "":
			(contract["really_damaged"] as Dictionary)[kind] = really_id
		var eva_damaged := _first_audio_route_id(routes.get("EvaEventDamagedOwner", []))
		if eva_damaged != "":
			(contract["eva_damaged"] as Dictionary)[kind] = eva_damaged
		var eva_die := _first_audio_route_id(routes.get("EvaEventDieOwner", []))
		if eva_die != "":
			(contract["eva_die"] as Dictionary)[kind] = eva_die
		# Collapse stays with the v1 lifecycle lane when it names an id.
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		var collapse_value: Variant = (lifecycle.get("audioEvents", {}) as Dictionary).get("collapse")
		if typeof(collapse_value) == TYPE_STRING and String(collapse_value) != "":
			(contract["collapse"] as Dictionary)[kind] = String(collapse_value)
		var health: Dictionary = ((registration.get("gameplay", {}) as Dictionary).get("health", {}) as Dictionary).get("primary", {}) as Dictionary
		var max_health := float((health.get("maxHealth", {}) as Dictionary).get("value", 0.0))
		if max_health > 0.0:
			var damaged_health := float((health.get("maxHealthDamaged", {}) as Dictionary).get("value", 0.0))
			var really_health := float((health.get("maxHealthReallyDamaged", {}) as Dictionary).get("value", 0.0))
			if damaged_health > 0.0:
				(contract["damaged_fraction"] as Dictionary)[kind] = clampf(damaged_health / max_health, 0.0, 1.0)
			if really_health > 0.0:
				(contract["really_damaged_fraction"] as Dictionary)[kind] = clampf(really_health / max_health, 0.0, 1.0)
	contract["eva_events"] = _load_eva_side_map()
	return contract


func _first_audio_route_id(rows: Variant) -> String:
	if typeof(rows) != TYPE_ARRAY:
		return ""
	for row_value in rows as Array:
		if typeof(row_value) == TYPE_DICTIONARY:
			var event_id := String((row_value as Dictionary).get("id", ""))
			if event_id != "":
				return event_id
	return ""


func _load_eva_side_map() -> Dictionary:
	## eva.ini is global: every cooked pack ships the same side map, so the
	## first mounted pack that carries it wins (the men EVA overlay and every
	## recooked faction pack are all valid sources). When no pack ships it the
	## map stays empty and EVA announcements fail closed to silence.
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root == "":
			continue
		var path := String(ModLoader.resolve_pack_path(root, "data/eva_events.json"))
		if path == "" or not FileAccess.file_exists(path):
			continue
		var document: Variant = ModLoader._read_json(path)
		if typeof(document) != TYPE_DICTIONARY:
			continue
		if String((document as Dictionary).get("schema", "")) != "openbfme.eva-events" or int((document as Dictionary).get("schemaVersion", -1)) != 0:
			continue
		var events: Variant = (document as Dictionary).get("events", {})
		if typeof(events) == TYPE_DICTIONARY:
			return (events as Dictionary).duplicate(true)
	return {}


func _process(delta: float) -> void:
	_sync_hud_to_viewport()
	_update_camera(delta)
	if not ready_ok:
		return
	if control_server != null:
		control_server.poll()
	if lockstep_session != null:
		lockstep_session.poll()
		_sync_multiplayer_pause_state()
		if lockstep_session.desynced:
			if not _mp_desync_reported:
				_mp_desync_reported = true
				hud.set_feedback("MULTIPLAYER DESYNC AT TICK %d" % lockstep_session.desync_tick, true)
		elif simulation.winner == -1:
			accumulator = minf(accumulator + minf(delta, 0.25), 0.25)
			while accumulator >= SimScript.TICK_SECONDS:
				if not lockstep_session.advance_if_ready():
					break
				accumulator -= SimScript.TICK_SECONDS
				lockstep_session.poll()
				_sync_multiplayer_pause_state()
	elif not simulation_paused and simulation.winner == -1:
		accumulator += minf(delta, 0.25)
		while accumulator >= SimScript.TICK_SECONDS:
			accumulator -= SimScript.TICK_SECONDS
			simulation.tick()
	_update_construction_ghost()
	_update_power_cast_ghost()
	_sync_presentation()
	_pump_glb_stream()


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT or what == NOTIFICATION_WM_WINDOW_FOCUS_OUT:
		# A release lost to alt-tab/focus change must not leave a drag armed —
		# the next right-press would silently pan instead of ordering.
		_right_drag_origin = Vector2.INF
		_right_dragging = false
		_drag_select_origin = Vector2.INF
		_drag_selecting = false
		_hide_selection_band()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("pause_menu") or (event is InputEventKey and (event as InputEventKey).pressed and not (event as InputEventKey).echo and (event as InputEventKey).keycode == KEY_ESCAPE):
		# Esc priority: close the options overlay → cancel armed cast/construct →
		# close spellbook → pause.
		if options_overlay != null and options_overlay.visible:
			options_overlay.cancel()
			get_viewport().set_input_as_handled()
			return
		if not ability_cast_armed.is_empty():
			ability_cast_armed = {}
			hud.set_feedback("Ability cast cancelled.")
			get_viewport().set_input_as_handled()
			return
		if power_cast_armed != "":
			power_cast_armed = ""
			hud.set_feedback("Power cast cancelled.")
			get_viewport().set_input_as_handled()
			return
		if construction_kind_armed != "":
			construction_kind_armed = ""
			_clear_construction_ghost()
			hud.set_feedback("Construction placement cancelled.")
			get_viewport().set_input_as_handled()
			return
		if hud != null and hud.has_method("close_powers_palette") and bool(hud.close_powers_palette()):
			hud.set_feedback("Spellbook closed.")
			get_viewport().set_input_as_handled()
			return
		toggle_escape_menu()
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey:
		var key := event as InputEventKey
		if key.pressed and not key.echo:
			if key.keycode == KEY_BACKSPACE:
				# Retail: double-tap Backspace snaps the camera home to the
				# player fortress with default zoom and rotation.
				var now := Time.get_ticks_msec()
				if now - _last_backspace_ms <= 500:
					_reset_camera_home()
				_last_backspace_ms = now
				get_viewport().set_input_as_handled()
				return
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
			# Dev cheats for testing production/spellbook without farming.
			if key.keycode == KEY_F7 and ready_ok and simulation != null and HudScript.dev_hud_enabled():
				_grant_test_resources()
				get_viewport().set_input_as_handled()
				return
			# Retail order hotkeys (rebindable through the project input map):
			# A = attack-move, S = stop, Z = cycle stance.
			if ready_ok and not simulation.selected_ids.is_empty():
				if event.is_action_pressed("attack_move"):
					_arm_attack_move()
					get_viewport().set_input_as_handled()
					return
				if event.is_action_pressed("stop_units"):
					_stop_selected_units()
					get_viewport().set_input_as_handled()
					return
				if event.is_action_pressed("stance_cycle"):
					_toggle_selected_stance()
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
	if event is InputEventMouseMotion and _right_drag_origin != Vector2.INF:
		# Retail: holding right mouse and dragging grabs the map and pans it.
		var pan_motion := event as InputEventMouseMotion
		if _right_dragging or pan_motion.position.distance_to(_right_drag_origin) > DRAG_SELECT_THRESHOLD:
			_right_dragging = true
			_pan_camera_screen(pan_motion.relative)
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
		if mouse.button_index == MOUSE_BUTTON_RIGHT and not mouse.pressed:
			# Right release: a drag was camera panning; a clean click issues the
			# order (move/attack for units, rally point for a building).
			var was_dragging := _right_dragging
			_right_drag_origin = Vector2.INF
			_right_dragging = false
			if was_dragging:
				return
			var order_world: Variant = _screen_to_world(mouse.position)
			if order_world == null:
				return
			_issue_order_at(Vector2((order_world as Vector3).x, (order_world as Vector3).z))
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
		elif mouse.button_index == MOUSE_BUTTON_RIGHT:
			# Orders resolve on release so right-drag can pan the camera.
			_right_drag_origin = mouse.position
			_right_dragging = false


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


func _selected_hero_for_ability(unit_id: String) -> int:
	## The ability buttons address a converted hero unit type; the cast itself
	## acts on the selected living hero entity of that type.
	for id in simulation.selected_ids:
		var row: Dictionary = simulation.entity(id)
		if (
			int(row.get("health", 0)) > 0
			and String(row.get("unit_type", "")) == unit_id
			and String(row.get("category", "")) == "hero"
		):
			return id
	return 0


func _ability_display_name(unit_id: String, ability_id: String) -> String:
	for rule_value in simulation.ability_rules_for_unit(unit_id):
		if String((rule_value as Dictionary).get("ability_id", "")) == ability_id:
			var label := String((rule_value as Dictionary).get("fallback_label", ""))
			if label != "":
				return label
	return ability_id


func _report_ability_cast(unit_id: String, ability_id: String, result: Dictionary) -> void:
	if bool(result.get("ok", false)):
		var affected := int(result.get("affected", 0))
		var summoned: Array = result.get("summoned", [])
		if not summoned.is_empty():
			hud.set_feedback("%s summons %d unit%s." % [_ability_display_name(unit_id, ability_id), summoned.size(), "" if summoned.size() == 1 else "s"])
		else:
			hud.set_feedback("%s affects %d." % [_ability_display_name(unit_id, ability_id), affected])
	else:
		hud.set_feedback("Cannot cast: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)


func _apply_local_command(command_type: String, args: Dictionary = {}) -> Variant:
	if lockstep_session != null:
		if lockstep_session.desynced:
			return {"ok": false, "reason": "desynced"}
		lockstep_session.submit_local(command_type, args)
		return {"ok": true, "scheduled": true}
	var command := {
		"tick": simulation.tick_index,
		"team": local_team,
		"seq": _local_command_seq,
		"type": command_type,
		"args": args,
	}
	_local_command_seq += 1
	simulation.apply_command(command)
	return simulation.last_command_result


func _handle_left_click(point: Vector2, additive: bool) -> void:
	if not ability_cast_armed.is_empty():
		var ability_result: Dictionary = _apply_local_command("cast_ability", {
			"hero_id": int(ability_cast_armed.get("hero_id", 0)),
			"ability_id": String(ability_cast_armed.get("ability_id", "")),
			"target_point": point,
		})
		_report_ability_cast(
			String(ability_cast_armed.get("unit_id", "")),
			String(ability_cast_armed.get("ability_id", "")),
			ability_result
		)
		if bool(ability_result.get("ok", false)):
			ability_cast_armed = {}
		_sync_presentation()
		return
	if power_cast_armed != "":
		var cast_result: Dictionary = _apply_local_command("cast_power", {"power_id": power_cast_armed, "point": point})
		if bool(cast_result.get("ok", false)):
			hud.set_feedback("%s affects %d battalion%s." % [
				hud.power_display_name(power_cast_armed), int(cast_result.get("battalions", 0)),
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
		var result: Dictionary = _apply_local_command("issue_construct", {"ids": simulation.selected_ids.duplicate(), "structure_kind": construction_kind_armed, "position": point})
		if bool(result.get("ok", false)):
			hud.set_feedback("%s construction started." % construction_kind_armed.replace("_", " ").capitalize())
			construction_kind_armed = ""
			_clear_construction_ghost()
		else:
			hud.set_feedback("Cannot build here: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)
			if String(result.get("reason", "")) == "insufficient-resources":
				hud.push_event_feed("Insufficient funds.")
		_sync_presentation()
		return
	_selected_expansion_pad = {}
	var pad_hit := _closest_expansion_pad(point)
	if not pad_hit.is_empty() and not additive:
		# Owner: clicking a fortress PAD (not the fortress) opens the radial
		# with the fortress's expansion options; the choice builds on that pad.
		_selected_expansion_pad = pad_hit
		simulation.clear_selection()
		selected_structure_id = int(pad_hit.get("fortress_id", 0))
		hud.set_feedback("Build plot selected: choose an expansion.")
		_sync_presentation()
		return
	var player_id := _closest_battalion(point, local_team, 6.0)
	# A porter parks on the producer it just raised; whichever of the two is
	# closer to the click wins, so the building is selectable without making
	# the porter unselectable anywhere near friendly structures.
	if player_id != 0 and bool(simulation.entity(player_id).get("is_builder", false)):
		var shadowing_structure := _closest_structure(point, local_team)
		if shadowing_structure != 0:
			var builder_gap := point.distance_to(Vector2(simulation.entity(player_id).get("position", Vector2.ZERO)))
			var structure_gap := point.distance_to(Vector2(simulation.structure(shadowing_structure).get("position", Vector2.ZERO)))
			if structure_gap < builder_gap:
				player_id = 0
	if player_id != 0:
		selected_structure_id = 0
		if additive:
			simulation.toggle_selection(player_id)
		else:
			simulation.select_only(player_id)
		hud.set_feedback("Selected %s" % String(simulation.entity(player_id).get("name", "battalion")))
	else:
		var structure_id := _closest_structure(point, local_team)
		simulation.clear_selection()
		selected_structure_id = structure_id
		if structure_id != 0:
			hud.set_feedback("Selected %s" % String(simulation.structure(structure_id).get("name", "structure")))
			if audio_system != null:
				audio_system.play_structure_select(String(simulation.structure(structure_id).get("structure_kind", "")))
	_sync_presentation()


func _handle_right_click(point: Vector2) -> void:
	if not ability_cast_armed.is_empty():
		ability_cast_armed = {}
		hud.set_feedback("Ability cast cancelled.")
		return
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
		var accepted := int(_apply_local_command("issue_attack", {"ids": simulation.selected_ids.duplicate(), "target_id": enemy_id}))
		hud.set_feedback("Attack order accepted." if accepted > 0 else "Attack order rejected.", accepted == 0)
		if accepted > 0:
			_sync_attack_target_indicator(enemy_id)
	else:
		var command_type := "issue_attack_move" if attack_move_armed else "issue_move"
		var moved := int(_apply_local_command(command_type, {"ids": simulation.selected_ids.duplicate(), "destination": point}))
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


## The player's free fortress build plots, nearest-first within pick range.
## Returns {"fortress_id", "pad_index", "pad_kind", "position"} or {}.
var _selected_expansion_pad: Dictionary = {}
const EXPANSION_PAD_PICK_RADIUS := 2.2


func _closest_expansion_pad(point: Vector2) -> Dictionary:
	var best: Dictionary = {}
	var best_distance := EXPANSION_PAD_PICK_RADIUS
	for fortress_id_value in simulation.expansion_pads.keys():
		var fortress_id := int(fortress_id_value)
		var fortress: Dictionary = simulation.structure(fortress_id)
		if fortress.is_empty() or int(fortress.get("team", -1)) != 0 or int(fortress.get("health", 0)) <= 0:
			continue
		var pads := simulation.expansion_pad_states(fortress_id)
		for pad_index in pads.size():
			var pad: Dictionary = pads[pad_index]
			if int(pad.get("expansion_structure_id", 0)) != 0:
				continue
			var pad_position := Vector2(pad.get("position", Vector2.ZERO))
			var distance := point.distance_to(pad_position)
			if distance <= best_distance:
				best_distance = distance
				best = {
					"fortress_id": fortress_id,
					"pad_index": pad_index,
					"pad_kind": String(pad.get("pad_kind", "")),
					"position": pad_position,
				}
	return best


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
		battalion.set_experience_level(int(entity.get("level", 1)))
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
		var structure_upgrade_queue := simulation.structure_upgrade_queue_state(id)
		if not structure_upgrade_queue.is_empty():
			# The upgrade timer is sim state; the node only presents it.
			structure.set_level(
				int(simulation.structure(id).get("level", 1)),
				true,
				float(structure_upgrade_queue[0].get("progress", 0.0))
			)
	if _profile_sync:
		presentation_profile["structures_us"] = presentation_profile.get("structures_us", 0) + (Time.get_ticks_usec() - _profile_mark)
		_profile_mark = Time.get_ticks_usec()
	if audio_system != null:
		audio_system.sync_events(simulation.events)
	_consume_power_fx_events()
	if _profile_sync:
		presentation_profile["audio_us"] = presentation_profile.get("audio_us", 0) + (Time.get_ticks_usec() - _profile_mark)
		_profile_mark = Time.get_ticks_usec()
	_sync_selected_attack_target_indicator()
	_sync_expansion_pad_markers()
	_refresh_hud()
	var compacted_events := simulation.compact_consumed_events()
	if compacted_events > 0:
		_score_event_index = simulation.events.size()
		_feed_event_index = simulation.events.size()
		_power_fx_event_index = simulation.events.size()
		if audio_system != null:
			audio_system.acknowledge_event_history_compaction(simulation.events.size())
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
	hud.set_resources(simulation.resources_for_team(local_team), simulation.command_points_for_team(local_team), simulation.command_point_cap)
	# Cooldown sweeps/purchase states move every tick, and the star orb's
	# power-point count is always on screen: keep the spellbook surface live.
	hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	_consume_event_feed()
	var score := _player_score_values()
	hud.set_score_values(int(score.units_trained), int(score.units_lost), int(score.resources_gathered))
	hud.set_control_groups(simulation.control_groups)
	if selected_structure_id != 0:
		var structure := simulation.structure(selected_structure_id)
		var structure_progress := float(structure.get("construction_progress", 1.0))
		if structure_progress < 1.0:
			var remaining_ticks := maxi(0, int(structure.get("construction_build_ticks", 0)) - int(structure.get("construction_elapsed_ticks", 0)))
			hud.set_selection("%s  •  Building %d%%  •  %ds left" % [
				String(structure.get("name", "Structure")),
				roundi(structure_progress * 100.0),
				ceili(float(remaining_ticks) * SimScript.TICK_SECONDS),
			])
		else:
			hud.set_selection("%s  •  %d%%" % [String(structure.get("name", "Structure")), roundi(100.0 * float(structure.get("health", 0)) / float(maxi(1, int(structure.get("maximum_health", 1)))))])
		var production: Array = structure.get("production", [])
		var can_train := int(structure.get("team", -1)) == 0 and int(structure.get("health", 0)) > 0 and not production.is_empty()
		var queue_count := Array(structure.get("queue", [])).size()
		var queue_state := simulation.production_queue_state(selected_structure_id)
		for queue_row_value in queue_state:
			var queue_row: Dictionary = queue_row_value
			# Seconds for the queue button's live countdown (retail training timer).
			queue_row["remaining_seconds"] = maxf(0.0, float(int(queue_row.get("duration_ticks", 0)) - int(queue_row.get("elapsed_ticks", 0))) * SimScript.TICK_SECONDS)
		var completed_upgrades: Array = structure.get("completed_upgrades", [])
		var locked_units: Array[String] = []
		for unit_type_value in production:
			var unit_type := String(unit_type_value)
			if simulation.hero_unavailable(local_team, unit_type):
				locked_units.append(unit_type)
				continue
			for required_upgrade_value in simulation.required_upgrades_for_unit(unit_type, String(structure.get("structure_kind", ""))):
				if not completed_upgrades.has(String(required_upgrade_value)):
					locked_units.append(unit_type)
					break
		hud.set_production_state(
			production,
			can_train,
			queue_count,
			queue_state,
			locked_units,
			completed_upgrades,
			simulation.structure_upgrade_queue_state(selected_structure_id),
			String(structure.get("structure_kind", "")),
			simulation.structure_upgrade_commands(selected_structure_id)
		)
		hud.set_unit_selection_state([], simulation.entities, simulation.tick_index)
		_sync_radial_commands(structure, production, locked_units, can_train)
		hud.set_battalion_upgrade_state([], [])
	else:
		var names: Array[String] = []
		for id in simulation.selected_ids:
			names.append(String(simulation.entity(id).get("name", str(id))))
		hud.set_selection(", ".join(names) if not names.is_empty() else "No battalion selected")
		hud.set_production_state([], false)
		hud.set_unit_selection_state(simulation.selected_ids, simulation.entities, simulation.tick_index)
		hud.hide_radial_commands()
		if not simulation.selected_ids.is_empty():
			hud.set_active_stance(String(simulation.entity(simulation.selected_ids[0]).get("stance", "Battle")))
		# Battalion OBJECT_UPGRADE purchase surface (compiled per unit doc): the
		# first selected battalion's authored rows, gate state, and live purchase
		# progress. Presentation-only wiring; the sim owns eligibility/cost.
		var first_selected := int(simulation.selected_ids[0]) if not simulation.selected_ids.is_empty() else 0
		if first_selected == 0:
			hud.set_battalion_upgrade_state([], [])
		else:
			var upgrade_queue_rows := simulation.battalion_upgrade_queue_state(first_selected)
			for upgrade_row_value in upgrade_queue_rows:
				var upgrade_row: Dictionary = upgrade_row_value
				upgrade_row["remaining_seconds"] = maxf(0.0, float(int(upgrade_row.get("duration_ticks", 0)) - int(upgrade_row.get("elapsed_ticks", 0))) * SimScript.TICK_SECONDS)
			hud.set_battalion_upgrade_state(simulation.battalion_upgrade_commands(first_selected), upgrade_queue_rows)
	_sync_dish_level_caption()
	_sync_hero_bar()
	_sync_construction_progress_labels()
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
			simulation.living_ids(local_team).size(),
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
var _feed_event_index := 0


## Retail top-right event feed. Consumes the sim event log incrementally (same
## discipline as the score counter) and mirrors retail's exact message shapes:
## "Construction Complete: X", "Upgrade Complete: X Level N" / tech names,
## "Fortress rally point set.", "Easy has been Defeated".
func _consume_event_feed() -> void:
	if simulation == null or hud == null:
		return
	var events: Array = simulation.events
	while _feed_event_index < events.size():
		var event: Dictionary = events[_feed_event_index]
		_feed_event_index += 1
		var kind := String(event.get("kind", ""))
		if int(event.get("team", -1)) != 0 and kind != "match.victory":
			continue
		match kind:
			"construction.completed":
				var kind_name := String(event.get("structure_kind", "")).replace("_", " ").capitalize()
				hud.push_event_feed("Construction Complete: %s" % kind_name)
			"upgrade.completed":
				var upgrade_id := String(event.get("upgrade_id", ""))
				var contract: Dictionary = simulation._structure_upgrade_contracts.get(upgrade_id, {}) as Dictionary
				if int(contract.get("levels_to_gain", 0)) > 0:
					var building: Dictionary = simulation.structure(int(event.get("entity_id", 0)))
					hud.push_event_feed("Upgrade Complete: %s Level %d" % [
						String(building.get("name", "Structure")),
						int(event.get("level", 1)),
					])
				else:
					hud.push_event_feed("Upgrade Complete: %s" % upgrade_id.trim_prefix("Upgrade_Gondor").trim_prefix("Upgrade_").capitalize())
			"structure.rally_set":
				var rally_structure: Dictionary = simulation.structure(int(event.get("target_id", 0)))
				hud.push_event_feed("%s rally point set." % String(rally_structure.get("name", "Structure")))
			"match.victory":
				# Retail names the defeated AI player; the slice fields the fixed
				# basic AI opponent.
				hud.push_event_feed("Easy has been Defeated")


## Floating radial command buttons above the selected producer (REF-25/33/35):
## train commands, forge research, and hero roster arc over the building,
## re-emitting the exact palantir socket orders.
func _sync_radial_commands(structure: Dictionary, production: Array, locked_units: Array, can_train: bool) -> void:
	if hud == null or camera == null:
		return
	var sim_position := Vector2(structure.get("position", Vector2.ZERO))
	# Owner: a clicked pad floats the radial over the PAD with the expansion
	# options that plot accepts; choosing one builds on that pad.
	var pad_selected := (
		selected_structure_id != 0
		and not _selected_expansion_pad.is_empty()
		and int(_selected_expansion_pad.get("fortress_id", 0)) == selected_structure_id
	)
	var world_position := Vector3(sim_position.x, _presentation_height(sim_position) + 1.0, sim_position.y)
	if pad_selected:
		var pad_position := Vector2(_selected_expansion_pad.get("position", sim_position))
		world_position = Vector3(pad_position.x, _presentation_height(pad_position) + 1.0, pad_position.y)
	if camera.is_position_behind(world_position):
		hud.hide_radial_commands()
		return
	var entries: Array = []
	if pad_selected:
		var pad_kind := String(_selected_expansion_pad.get("pad_kind", ""))
		for kind_value in simulation.expansion_commands_for(selected_structure_id):
			var kind := String(kind_value)
			var rule: Dictionary = simulation._expansion_build_rules.get(kind, {}) as Dictionary
			if not (rule.get("pad_kinds", []) as Array).has(pad_kind):
				continue
			var command := hud.retail_expansion_command(kind)
			if command.is_empty() or command.get("texture") == null:
				continue
			var pad_cost := int(rule.get("cost", 0))
			entries.append({
				"command_kind": "expansion",
				"id": kind,
				"icon": command.get("texture"),
				"enabled": simulation.resources_for_team(local_team) >= pad_cost,
				"label": String(command.get("label", "")),
				"tooltip": String(command.get("tooltip", "")),
			})
		var pad_anchor := camera.unproject_position(world_position)
		hud.sync_radial_commands(pad_anchor, entries)
		return
	if int(structure.get("team", -1)) == 0 and int(structure.get("health", 0)) > 0 and float(structure.get("construction_progress", 1.0)) >= 1.0:
		# Active queue row per unit type: the radial's training icons sweep the
		# same CCW dial + live countdown as the palantir queue chips (owner).
		var radial_queue_by_unit: Dictionary = {}
		for radial_row_value in simulation.production_queue_state(selected_structure_id):
			var radial_row: Dictionary = radial_row_value
			if not bool(radial_row.get("active", false)):
				continue
			radial_row["remaining_seconds"] = maxf(0.0, float(int(radial_row.get("duration_ticks", 0)) - int(radial_row.get("elapsed_ticks", 0))) * SimScript.TICK_SECONDS)
			radial_queue_by_unit[String(radial_row.get("unit_type", ""))] = radial_row
		for unit_id_value in production:
			var unit_id := String(unit_id_value)
			var train_button: Button = hud.train_buttons.get(unit_id) as Button
			if train_button == null:
				train_button = hud.hero_buttons.get(unit_id) as Button
			if train_button == null or train_button.icon == null:
				continue
			entries.append({
				"command_kind": "hero" if hud.hero_buttons.has(unit_id) else "train",
				"id": unit_id,
				"icon": train_button.icon,
				"enabled": can_train and not locked_units.has(unit_id),
				"label": String(train_button.get_meta("retail_label", "")),
				"tooltip": train_button.tooltip_text,
				"queue_row": radial_queue_by_unit.get(unit_id, {}),
			})
		# Universal radial (owner: every selected building carries ALL of its
		# authored commands above it, REF-25/33/35 — train, research, upgrades):
		# the doc-driven purchasable upgrade steps join the same arc, with the
		# validated icon from the palantir socket button when the pack has one.
		var upgrade_queue := simulation.structure_upgrade_queue_state(selected_structure_id)
		for command_value in simulation.structure_upgrade_commands(selected_structure_id):
			var upgrade_command: Dictionary = command_value
			var upgrade_id := String(upgrade_command.get("upgrade_id", ""))
			var doc_button: Button = hud._doc_upgrade_buttons.get(upgrade_id) as Button
			var upgrade_icon: Texture2D = doc_button.icon if doc_button != null else null
			var upgrade_label := ""
			var upgrade_tip := ""
			if doc_button != null:
				upgrade_label = String(doc_button.get_meta("retail_label", ""))
				upgrade_tip = doc_button.tooltip_text
			entries.append({
				"command_kind": "upgrade",
				"id": upgrade_id,
				"icon": upgrade_icon,
				"text": upgrade_label if upgrade_icon == null else "",
				"enabled": upgrade_queue.is_empty(),
				"label": upgrade_label,
				"tooltip": upgrade_tip,
			})
		# Research rides the doc-driven rows only (structure_upgrade_commands
		# carries compiled research with its own pack strings/icons); the
		# hardcoded forge spec surface is retired — no stale provisional ids.
		if String(structure.get("structure_kind", "")) == "fortress":
			# The fortress's authored expansion pad commands (REF-33): one radial
			# button per expansion with a free plot, click builds on the plot.
			for kind_value in simulation.expansion_commands_for(selected_structure_id):
				var kind := String(kind_value)
				var command := hud.retail_expansion_command(kind)
				if command.is_empty() or command.get("texture") == null:
					continue
				var cost := int(simulation._expansion_build_rules.get(kind, {}).get("cost", 0))
				entries.append({
					"command_kind": "expansion",
					"id": kind,
					"icon": command.get("texture"),
					"enabled": simulation.resources_for_team(local_team) >= cost,
					"label": String(command.get("label", "")),
					"tooltip": String(command.get("tooltip", "")),
				})
	var anchor := camera.unproject_position(world_position)
	hud.sync_radial_commands(anchor, entries)


## Floating "Building: N% • Ns left" above every construction site (REF-27/28).
func _sync_construction_progress_labels() -> void:
	if hud == null or simulation == null or camera == null:
		return
	var entries: Array = []
	for id in simulation.structure_ids():
		var structure: Dictionary = simulation.structure(id)
		var progress := float(structure.get("construction_progress", 1.0))
		if progress >= 1.0 or int(structure.get("health", 0)) <= 0:
			continue
		var sim_position := Vector2(structure.get("position", Vector2.ZERO))
		var world_position := Vector3(sim_position.x, _presentation_height(sim_position) + 4.0, sim_position.y)
		if camera.is_position_behind(world_position):
			continue
		var remaining_ticks := maxi(0, int(structure.get("construction_build_ticks", 0)) - int(structure.get("construction_elapsed_ticks", 0)))
		entries.append({
			"position": camera.unproject_position(world_position),
			"percent": roundi(progress * 100.0),
			"seconds_left": ceili(float(remaining_ticks) * SimScript.TICK_SECONDS),
		})
	hud.sync_construction_progress(entries)


## "Level: N" caption + progress bar in the palantir dish (REF-25 building,
## REF-41 hero). The sim has no XP source, so the bar stays honest at 0.
func _sync_dish_level_caption() -> void:
	if hud == null or simulation == null:
		return
	if selected_structure_id != 0:
		var structure: Dictionary = simulation.structure(selected_structure_id)
		if structure.is_empty() or int(structure.get("health", 0)) <= 0:
			hud.set_dish_level("", 0.0)
			return
		var upgrade_queue := simulation.structure_upgrade_queue_state(selected_structure_id)
		var upgrade_progress := 0.0
		if not upgrade_queue.is_empty():
			upgrade_progress = clampf(float(upgrade_queue[0].get("progress", 0.0)), 0.0, 1.0)
		hud.set_dish_level("Level: %d" % int(structure.get("level", 1)), upgrade_progress)
		return
	for id in simulation.selected_ids:
		var entity: Dictionary = simulation.entity(id)
		if String(entity.get("category", "")) == "hero":
			hud.set_dish_level("Level: %d" % int(entity.get("level", 1)), 0.0)
			return
	hud.set_dish_level("", 0.0)


## Recruited-hero strip bottom-center (REF-24 badges, REF-41 health bar).
func _sync_hero_bar() -> void:
	if hud == null or simulation == null:
		return
	var heroes: Array = []
	for id in simulation.entity_ids():
		var entity: Dictionary = simulation.entity(id)
		if int(entity.get("team", -1)) != 0 or String(entity.get("category", "")) != "hero":
			continue
		if int(entity.get("health", 0)) <= 0:
			continue
		heroes.append({
			"id": id,
			"unit_type": String(entity.get("unit_type", "")),
			"name": String(entity.get("name", "Hero")),
			"level": int(entity.get("level", 1)),
			"health": int(entity.get("health", 0)),
			"maximum_health": int(entity.get("maximum_health", 1)),
			"selected": simulation.selected_ids.has(id),
		})
	hud.sync_hero_bar(heroes)


func _on_hero_recall_requested(hero_id: int) -> void:
	if simulation == null or not simulation.entities.has(hero_id):
		return
	selected_structure_id = 0
	simulation.select_only(hero_id)
	_sync_presentation()


func _on_expansion_requested(expansion_kind: String) -> void:
	if simulation == null or selected_structure_id == 0:
		return
	var pad_index := int(_selected_expansion_pad.get("pad_index", -1))
	var result: Dictionary = _apply_local_command("issue_expansion_construct", {"fortress_id": selected_structure_id, "expansion_kind": expansion_kind, "pad_index": pad_index})
	var accepted := bool(result.get("ok", false))
	if accepted:
		_selected_expansion_pad = {}
	elif String(result.get("reason", "")) == "insufficient-resources":
		hud.push_event_feed("Insufficient funds.")
	else:
		hud.set_feedback("Expansion unavailable: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)
	_sync_presentation()


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
	var unit_name := String(UNIT_QUEUE_NAMES.get(unit_id, ""))
	if unit_name == "" and simulation != null:
		unit_name = simulation.production_rule_display_name(unit_id)
	if unit_name == "":
		unit_name = "Unit"
	if selected_structure_id == 0:
		hud.set_feedback("Cannot train %s: select its production building." % unit_name, true)
		_refresh_hud()
		return
	var producer := selected_structure_id
	var structure: Dictionary = simulation.structure(producer)
	var production: Array = structure.get("production", [])
	if int(structure.get("team", -1)) != local_team or int(structure.get("health", 0)) <= 0 or not production.has(unit_id):
		hud.set_feedback("Cannot train %s from the selected structure." % unit_name, true)
		_refresh_hud()
		return
	var result: Dictionary = _apply_local_command("queue_unit", {"producer": producer, "unit_type": unit_id})
	var accepted := bool(result.get("ok", false))
	var reason := String(result.get("reason", "rejected"))
	var feedback := "%s added to the queue." % unit_name if accepted else "Cannot train %s: %s." % [unit_name, reason.replace("-", " ")]
	hud.set_feedback(feedback, not accepted)
	if not accepted and reason == "command-point-cap":
		hud.flash_command_points()
	if not accepted and reason == "insufficient-resources":
		# Retail surfaces rejected purchases in the top-right event feed.
		hud.push_event_feed("Insufficient funds.")
	_refresh_hud()


func _on_battalion_upgrade_requested(upgrade_id: String) -> void:
	## Battalion OBJECT_UPGRADE purchase from the HUD socket surface. Every
	## selected player battalion the sim accepts is charged its authored
	## (possibly Iron-Ore-discounted) cost; ineligible ones are simply skipped.
	if simulation == null or simulation.selected_ids.is_empty():
		return
	var queued := 0
	var last_result: Dictionary = {}
	for entity_id in simulation.selected_ids:
		var result: Dictionary = _apply_local_command("queue_battalion_upgrade", {"entity_id": entity_id, "upgrade_id": upgrade_id})
		if bool(result.get("ok", false)):
			queued += 1
		else:
			last_result = result
	if queued > 0:
		hud.set_feedback("Purchase started (%d battalion%s)." % [queued, "" if queued == 1 else "s"])
	elif String(last_result.get("reason", "")) == "insufficient-resources":
		hud.push_event_feed("Insufficient funds.")
	else:
		hud.set_feedback("Cannot purchase: %s." % String(last_result.get("reason", "rejected")).replace("-", " "), true)
	_sync_presentation()


func _upgrade_selected_structure(upgrade_id: String) -> void:
	if selected_structure_id == 0:
		hud.set_feedback("Cannot research: select the building first.", true)
		_refresh_hud()
		return
	var result: Dictionary = _apply_local_command("queue_structure_upgrade", {"structure_id": selected_structure_id, "upgrade_id": upgrade_id})
	var accepted := bool(result.get("ok", false))
	var upgrade_label := upgrade_id.trim_prefix("Upgrade_Gondor").capitalize()
	hud.set_feedback(
		"%s research started." % upgrade_label
		if accepted
		else "Cannot research %s: %s." % [upgrade_label, String(result.get("reason", "rejected")).replace("-", " ")],
		not accepted
	)
	if not accepted and String(result.get("reason", "")) == "insufficient-resources":
		hud.push_event_feed("Insufficient funds.")
	_refresh_hud()


func _cancel_selected_production(queue_index: int) -> void:
	if selected_structure_id == 0:
		hud.set_feedback("Cannot cancel training: select its production building.", true)
		_refresh_hud()
		return
	var result: Dictionary = _apply_local_command("cancel_queued_unit", {"producer": selected_structure_id, "queue_index": queue_index})
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
	if simulation == null or simulation.selected_ids.is_empty() or simulation.structure_build_rule(structure_kind).is_empty():
		hud.set_feedback("Builder construction command rejected.", true)
		return
	construction_kind_armed = structure_kind
	attack_move_armed = false
	ability_cast_armed = {}
	_spawn_construction_ghost()
	var rule: Dictionary = simulation.structure_build_rule(structure_kind)
	hud.set_feedback("Place %s: left-click a clear site (right-click cancels). Cost %d." % [structure_kind.replace("_", " ").capitalize(), int(rule["cost"])])


func _spawn_construction_ghost() -> void:
	_clear_construction_ghost()
	# Retail placement cursor (REF-29): the translucent intact building model
	# at the cursor, tinted green when the site validates / red when not, plus
	# the placement footprint circle and the resource behavior's effectiveness
	# ring (farm-style) when the structure document ships one.
	construction_ghost = Node3D.new()
	construction_ghost.name = "ConstructionPlacementGhost"
	construction_ghost.set_meta("legal_safe_gameplay_overlay", true)
	construction_ghost.visible = false
	add_child(construction_ghost)
	var kind := construction_kind_armed
	var object_id := String((faction_manifest.get("structure_object_ids", {}) as Dictionary).get(kind, ""))
	var ghost_model: Node3D = null
	if object_id != "":
		var definition := ContentDB.get_bundle_object(object_id)
		var definition_root := String(definition.get("_pack_root", ""))
		var relative := String((definition.get("presentation", {}) as Dictionary).get("model", "")).replace("\\", "/")
		var resolved := ContentDB.resolve_asset(relative, definition_root) if relative != "" else ""
		if resolved != "" and FileAccess.file_exists(resolved):
			var asset_factory = load("res://src/view/asset_factory.gd")
			ghost_model = asset_factory._try_load_model(resolved) as Node3D
	if ghost_model != null:
		ghost_model.name = "GhostModel"
		ghost_model.scale = Vector3.ONE * maxf(0.0001, source_map_data.local_transform_scale)
		construction_ghost.add_child(ghost_model)
		# Retail placement ghost (owner change): the mesh's ACTUAL colors at ~50%
		# opacity, not a flat green tint. Valid/invalid now rides ONLY on the
		# footprint ring color below (green valid / red invalid).
		_apply_ghost_translucency(ghost_model)
	else:
		# Fail-closed fallback: the flat footprint quad (model not converted).
		var quad_instance := MeshInstance3D.new()
		quad_instance.name = "GhostModel"
		var quad := PlaneMesh.new()
		quad.size = Vector2(14.0, 14.0)
		quad_instance.mesh = quad
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_texture = preload("res://src/retail_slice/retail_shadow_decal.gd")._shared_texture()
		material.albedo_color = Color(0.20, 0.85, 0.30, 0.4)
		material.no_depth_test = false
		quad_instance.material_override = material
		quad_instance.set_meta("tint_material", material)
		construction_ghost.add_child(quad_instance)
	# Placement footprint circle (item 6): the radius the sim enforces.
	var footprint := MeshInstance3D.new()
	footprint.name = "FootprintCircle"
	footprint.mesh = _make_ground_ring(maxf(0.2, simulation._structure_placement_radius(kind)), 64, 0.06)
	footprint.material_override = _ghost_ring_material(Color(0.35, 0.9, 0.4, 0.85))
	footprint.position.y = 0.12
	construction_ghost.add_child(footprint)
	# Effectiveness ring (farm-style, REF-29/30): only when the structure
	# document ships a TerrainResourceBehavior radius; hidden otherwise
	# (fail closed — never invented).
	var effectiveness_radius := _structure_effectiveness_radius_local(object_id)
	if effectiveness_radius > 0.0:
		var ring := MeshInstance3D.new()
		ring.name = "EffectivenessRing"
		ring.mesh = _make_ground_ring(effectiveness_radius, 96, 0.08)
		ring.material_override = _ghost_ring_material(Color(0.55, 0.95, 0.45, 0.6))
		ring.position.y = 0.1
		construction_ghost.add_child(ring)


## Dims every mesh surface to ~50% opacity while KEEPING each material's own
## colors/textures (the retail placement ghost look). Duplicates materials so
## the live structures' originals are never mutated.
var _ghost_material_cache: Dictionary = {}


func _apply_ghost_translucency(node: Node) -> void:
	if node is MeshInstance3D:
		var mesh_instance := node as MeshInstance3D
		var surface_count := mesh_instance.mesh.get_surface_count() if mesh_instance.mesh != null else 0
		for surface_index in surface_count:
			var source_material := mesh_instance.get_active_material(surface_index)
			if source_material == null:
				continue
			var cache_key := source_material.get_instance_id()
			var ghost_material: Material = _ghost_material_cache.get(cache_key)
			if ghost_material == null:
				ghost_material = source_material.duplicate()
				if ghost_material is BaseMaterial3D:
					var base := ghost_material as BaseMaterial3D
					base.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
					var color := base.albedo_color
					color.a = 0.5
					base.albedo_color = color
				_ghost_material_cache[cache_key] = ghost_material
			mesh_instance.set_surface_override_material(surface_index, ghost_material)
	for child in node.get_children():
		_apply_ghost_translucency(child)


func _ghost_ring_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = color
	material.no_depth_test = false
	return material


func _make_ground_ring(radius: float, segments: int, width: float) -> ArrayMesh:
	var vertices := PackedVector3Array()
	for index in segments:
		var a0 := float(index) / float(segments) * TAU
		var a1 := float(index + 1) / float(segments) * TAU
		var outer0 := Vector3(cos(a0), 0.0, sin(a0)) * (radius + width)
		var outer1 := Vector3(cos(a1), 0.0, sin(a1)) * (radius + width)
		var inner0 := Vector3(cos(a0), 0.0, sin(a0)) * (radius - width)
		var inner1 := Vector3(cos(a1), 0.0, sin(a1)) * (radius - width)
		vertices.append_array([outer0, outer1, inner0, outer1, inner1, inner0])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


## The structure document's TerrainResourceBehavior radius in local units
## (0 when the doc ships none — farm/mallorn only).
func _structure_effectiveness_radius_local(object_id: String) -> float:
	if object_id == "" or source_map_data == null:
		return 0.0
	var definition := ContentDB.get_bundle_object(object_id)
	var gameplay := ((definition.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var behavior := gameplay.get("resourceBehavior", {}) as Dictionary
	var radius := float((behavior.get("radius", {}) as Dictionary).get("value", 0.0))
	if radius <= 0.0:
		return 0.0
	return radius * source_map_data.local_transform_scale


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
	var valid := bool(probe.get("ok", false))
	# Validity rides the footprint ring color; the 3D ghost keeps the mesh's
	# actual colors (owner change). The quad fallback (unconverted model) still
	# tints because it has no colors of its own.
	var valid_color := Color(0.25, 0.9, 0.35, 0.45) if valid else Color(0.92, 0.2, 0.16, 0.45)
	var ring_color := Color(0.35, 0.9, 0.4, 0.85) if valid else Color(0.92, 0.25, 0.2, 0.85)
	for child in construction_ghost.get_children():
		if String(child.name) == "GhostModel" and child.has_meta("tint_material"):
			var tint := child.get_meta("tint_material", null) as StandardMaterial3D
			if tint != null:
				tint.albedo_color = valid_color
		elif String(child.name) == "FootprintCircle":
			var ring_material := (child as MeshInstance3D).material_override as StandardMaterial3D
			if ring_material != null:
				ring_material.albedo_color = ring_color


func _update_power_cast_ghost() -> void:
	## Spellbook targeting mode: a range ring at the cursor, sized by the
	## document's resolved radiusCursorRadius for the armed power.
	if power_cast_armed == "" or simulation == null:
		if _power_cast_ghost != null:
			_power_cast_ghost.queue_free()
			_power_cast_ghost = null
		if _power_cast_glyph != null:
			_power_cast_glyph.queue_free()
			_power_cast_glyph = null
		return
	var radius: float = simulation.spellbook_power_radius_sim(power_cast_armed)
	if radius <= 0.0:
		radius = 2.0
	if _power_cast_ghost == null:
		_power_cast_ghost = MeshInstance3D.new()
		_power_cast_ghost.name = "PowerCastTargetRing"
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		# REF-49: the targeting ring is a white/pale circle, not green.
		material.albedo_color = Color(0.95, 0.97, 1.0, 0.8)
		material.emission_enabled = true
		material.emission = Color(0.9, 0.93, 1.0)
		_power_cast_ghost.material_override = material
		add_child(_power_cast_ghost)
		# REF-49: the armed power's glyph projects at the ring's center.
		_power_cast_glyph = MeshInstance3D.new()
		_power_cast_glyph.name = "PowerCastTargetGlyph"
		var glyph_material := StandardMaterial3D.new()
		glyph_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		glyph_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		glyph_material.albedo_color = Color(1.0, 1.0, 1.0, 0.85)
		_power_cast_glyph.material_override = glyph_material
		var glyph_quad := QuadMesh.new()
		glyph_quad.size = Vector2(1.4, 1.4)
		_power_cast_glyph.mesh = glyph_quad
		_power_cast_glyph.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
		add_child(_power_cast_glyph)
		var glyph_icon: Texture2D = null
		if hud != null:
			for power_button in hud.power_buttons:
				if String(power_button.get_meta("power_id", "")) == power_cast_armed:
					glyph_icon = power_button.icon
					break
		if glyph_icon != null:
			(glyph_material as StandardMaterial3D).albedo_texture = glyph_icon
	var mesh := _power_cast_ghost.mesh as TorusMesh
	if mesh == null or not is_equal_approx(mesh.outer_radius, radius):
		mesh = TorusMesh.new()
		mesh.outer_radius = radius
		mesh.inner_radius = radius * 0.94
		_power_cast_ghost.mesh = mesh
	var world: Variant = _screen_to_world(get_viewport().get_mouse_position())
	if world == null:
		_power_cast_ghost.visible = false
		if _power_cast_glyph != null:
			_power_cast_glyph.visible = false
		return
	var ground := world as Vector3
	_power_cast_ghost.visible = true
	_power_cast_ghost.global_position = Vector3(ground.x, ground.y + 0.15, ground.z)
	if _power_cast_glyph != null:
		_power_cast_glyph.visible = true
		_power_cast_glyph.global_position = Vector3(ground.x, ground.y + 0.2, ground.z)


func _stop_selected_units() -> void:
	if simulation == null:
		return
	attack_move_armed = false
	construction_kind_armed = ""
	_clear_construction_ghost()
	var stopped := int(_apply_local_command("issue_stop", {"ids": simulation.selected_ids.duplicate()}))
	hud.set_feedback("Stop order accepted." if stopped > 0 else "Stop order rejected.", stopped == 0)


func _toggle_selected_stance() -> void:
	var accepted := int(_apply_local_command("issue_toggle_stance", {"ids": simulation.selected_ids.duplicate()}))
	if accepted > 0:
		var stance := String(simulation.entity(simulation.selected_ids[0]).get("stance", "Battle"))
		hud.set_active_stance(stance)
		hud.set_feedback("Stance: %s" % stance)
	else:
		hud.set_feedback("Stance order rejected.", true)
	_sync_presentation()


func _toggle_selected_formation() -> void:
	if simulation == null:
		return
	var accepted := int(_apply_local_command("issue_toggle_formation", {"ids": simulation.selected_ids.duplicate()}))
	if accepted > 0:
		var formation := String(simulation.entity(simulation.selected_ids[0]).get("formation_mode", "Line"))
		if hud.has_method("set_active_formation"):
			hud.set_active_formation(formation)
		hud.set_feedback("Formation: %s" % formation)
	else:
		hud.set_feedback("Formation order rejected.", true)
	_sync_presentation()


var _expansion_pad_markers: Dictionary = {}
var _expansion_pad_model_cache: Dictionary = {}


## Engine-spawned fortress build plots: the pad plot model at each free pad
## (retail shows the plot circles ringing the fortress, REF-32/52), hidden
## once an expansion rises on the pad or the fortress falls.
func _sync_expansion_pad_markers() -> void:
	if simulation == null:
		return
	for fortress_id_value in simulation.expansion_pads.keys():
		var fortress_id := int(fortress_id_value)
		var pads := simulation.expansion_pad_states(fortress_id)
		var markers: Array = _expansion_pad_markers.get(fortress_id, [])
		while markers.size() < pads.size():
			var marker := _make_pad_marker(String((pads[markers.size()] as Dictionary).get("pad_kind", "")))
			if marker == null:
				break
			add_child(marker)
			_assign_geometry_light_layer(marker, OBJECT_LIGHT_LAYER)
			markers.append(marker)
		var fortress: Dictionary = simulation.structure(fortress_id)
		var fortress_alive := not fortress.is_empty() and int(fortress.get("health", 0)) > 0 and float(fortress.get("construction_progress", 1.0)) >= 1.0
		for index in markers.size():
			var marker: Node3D = markers[index]
			if index >= pads.size():
				marker.visible = false
				continue
			var pad: Dictionary = pads[index]
			var position := Vector2(pad.get("position", Vector2.ZERO))
			marker.position = Vector3(position.x, _presentation_height(position) + 0.02, position.y)
			marker.visible = fortress_alive and int(pad.get("expansion_structure_id", 0)) == 0
			# The clicked plot swells slightly so the player sees which pad the
			# radial is anchored to.
			var marker_selected := (
				marker.visible
				and int(_selected_expansion_pad.get("fortress_id", 0)) == fortress_id
				and int(_selected_expansion_pad.get("pad_index", -1)) == index
			)
			marker.scale = Vector3.ONE * (1.35 if marker_selected else 1.0)
		_expansion_pad_markers[fortress_id] = markers


func _make_pad_marker(pad_kind: String) -> Node3D:
	var source_id := "MenFortressExpansionPadSide" if pad_kind == "side" else "MenFortressExpansionPadCorner"
	var definition := ContentDB.get_playable_structure_runtime(source_id)
	if definition.is_empty():
		return null
	# Cache only the resolved path; every marker is a fresh instance from the
	# asset factory's duplicating loader (never a re-parented node).
	var resolved := String(_expansion_pad_model_cache.get(source_id, ""))
	if resolved == "<failed>":
		return null
	if resolved == "":
		var bib := (((definition.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary).get("bib", {}) as Dictionary
		var relative := String((bib.get("visual", {}) as Dictionary).get("glb", "")).replace("\\", "/")
		resolved = ContentDB.resolve_asset(relative, String(definition.get("_pack_root", ""))) if relative != "" else ""
		if resolved == "" or not FileAccess.file_exists(resolved):
			_expansion_pad_model_cache[source_id] = "<failed>"
			return null
		_expansion_pad_model_cache[source_id] = resolved
	var asset_factory = load("res://src/view/asset_factory.gd")
	var instance: Node3D = asset_factory._try_load_model(resolved) as Node3D
	if instance == null:
		return null
	var marker := Node3D.new()
	marker.name = "ExpansionPadMarker"
	instance.scale = Vector3.ONE * maxf(0.0001, source_map_data.local_transform_scale)
	marker.add_child(instance)
	# Retail build plots read as a light ground circle; the converted floor
	# model alone is near-invisible against terrain, so ring it (styled chrome
	# in the retail plot idiom, never fake art).
	var ring := MeshInstance3D.new()
	ring.name = "PadRing"
	ring.mesh = _make_ground_ring(0.62, 48, 0.055)
	ring.material_override = _ghost_ring_material(Color(0.82, 0.74, 0.5, 0.7))
	ring.position.y = 0.06
	marker.add_child(ring)
	return marker


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
	if lockstep_session != null:
		if lockstep_session.desynced:
			return
		_apply_local_command("resume" if simulation.clock_paused else "pause")
		hud.set_feedback("Resume requested." if simulation.clock_paused else "Pause requested.")
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


func _sync_multiplayer_pause_state() -> void:
	if lockstep_session == null \
		or lockstep_session.pause_command_tick < 0 \
		or lockstep_session.pause_command_tick == _mp_last_pause_command_tick:
		return
	_mp_last_pause_command_tick = lockstep_session.pause_command_tick
	simulation_paused = lockstep_session.pause_command_state
	process_mode = Node.PROCESS_MODE_ALWAYS
	if hud_root != null:
		hud_root.process_mode = Node.PROCESS_MODE_ALWAYS
	if audio_system != null:
		audio_system.process_mode = Node.PROCESS_MODE_ALWAYS
	get_tree().paused = simulation_paused
	hud.show_pause(simulation_paused)
	hud.set_match_clock_seconds(simulation.tick_index * SimScript.TICK_SECONDS)
	hud.set_feedback("Simulation paused." if simulation_paused else "Simulation resumed.")


func _install_pause_settings_button() -> void:
	## Options seam: retail's pause screen leads with SETTINGS. The button is
	## injected into the HUD's pause column from here so retail_hud.gd keeps
	## owning the panel itself.
	if pause_panel == null or pause_panel.get_child_count() == 0:
		return
	var column := pause_panel.get_child(0)
	if column == null:
		return
	var settings_button := Button.new()
	settings_button.name = "PauseSettingsButton"
	settings_button.text = "SETTINGS"
	settings_button.tooltip_text = "Click here to change your audio and video settings"
	settings_button.custom_minimum_size = Vector2(0, 44)
	settings_button.pressed.connect(_open_options_overlay)
	column.add_child(settings_button)
	column.move_child(settings_button, 2)


func _open_options_overlay() -> void:
	if options_overlay == null:
		options_overlay = OptionsScreenScript.new()
		options_overlay.name = "RetailOptionsOverlay"
		options_overlay.process_mode = Node.PROCESS_MODE_ALWAYS
		options_overlay.z_index = 90
		options_overlay.visible = false
		options_overlay.closed.connect(_close_options_overlay)
		add_child(options_overlay)
		var albertus: Font = null
		if hud != null and hud.retail_apt_runtime != null and hud.retail_apt_runtime.has_method("external_albertus_font"):
			albertus = hud.retail_apt_runtime.external_albertus_font()
		options_overlay.configure({
			"font": albertus,
			"audio_system": audio_system,
			"scroll_target": self,
		})
	if pause_panel != null:
		pause_panel.visible = false
	options_overlay.open()


func _close_options_overlay(applied: bool) -> void:
	if simulation_paused and pause_panel != null:
		pause_panel.visible = true
	if hud != null:
		hud.set_feedback("Settings applied." if applied else "Settings closed.")


func _configure_simulation_spellbook() -> void:
	## The spellbook tree (costs, prerequisite groups, purchase slots, reloads)
	## comes from the selected pack's openbfme.spellbook-runtime document; the
	## sim fails closed (empty tree) when the pack carries none.
	if simulation == null:
		return
	simulation.configure_spellbook_runtime(_faction_spellbook_document())


## Fortress expansion pad rules, built from the playable-structure expansion
## documents in the selected/faction packs. Every number is doc-sourced; kinds
## without a complete document drop out fail-closed.
const EXPANSION_DOC_IDS := {
	"arrow_tower_expansion": "MenArrowTowerExpansion",
	"trebuchet_expansion": "MenTrebuchetExpansion",
	"trebuchet_side_expansion": "MenTrebuchetSideExpansion",
	"garrison_dormitory": "MenGarrisonTowerExpansion",
}
var _expansion_object_ids: Dictionary = {}


func _configure_simulation_expansions() -> void:
	if simulation == null:
		return
	var rules: Dictionary = {}
	for kind_value in EXPANSION_DOC_IDS.keys():
		var kind := String(kind_value)
		var source_id := String(EXPANSION_DOC_IDS[kind])
		var runtime_id := PlayableUnitAdapter._runtime_id(source_id)
		var definition := ContentDB.get_playable_structure_runtime(source_id)
		if definition.is_empty():
			continue
		var registration := definition.get("registration", {}) as Dictionary
		var gameplay := registration.get("gameplay", {}) as Dictionary
		var scalar_fields := gameplay.get("scalarFields", {}) as Dictionary
		var cost := int((scalar_fields.get("BuildCost", {}) as Dictionary).get("value", -1))
		var seconds := float((scalar_fields.get("BuildTime", {}) as Dictionary).get("value", 0.0))
		var health := int(((gameplay.get("health", {}) as Dictionary).get("primary", {}) as Dictionary).get("maxHealth", {}).get("value", 0))
		var pad_kinds: Array = []
		for route_value in (registration.get("production", {}) as Dictionary).get("routes", []) as Array:
			var builder := String((route_value as Dictionary).get("builderObjectId", "")).to_lower()
			if builder.contains("padside") and not pad_kinds.has("side"):
				pad_kinds.append("side")
			elif builder.contains("padcorner") and not pad_kinds.has("corner"):
				pad_kinds.append("corner")
		if cost <= 0 or seconds <= 0.0 or health <= 0 or pad_kinds.is_empty():
			# Incomplete doc: the kind is unavailable, never approximated.
			continue
		rules[kind] = {
			"cost": cost,
			"seconds": seconds,
			"health": health,
			"pad_kinds": pad_kinds,
			"name": kind.replace("_", " ").capitalize(),
			"object_id": runtime_id,
		}
	_expansion_object_ids = rules.duplicate(true)
	simulation.configure_expansion_rules(rules)


func _faction_spellbook_document() -> Dictionary:
	## Resolve the CURRENT faction's spellbook from the packs, never by slot
	## position: the content registry's spellbook slot is last-pack-wins across
	## every mounted pack, so without a faction check every faction plays the
	## Men tree. Search the active pack first, then every mounted pack root;
	## a faction whose packs genuinely ship no spellbook fails closed (the sim
	## reports spellbook-unavailable and locks the tree, recorded here).
	var faction := String(faction_manifest.get("faction", ""))
	var document := _spellbook_document_for_faction(selected_pack_root, faction)
	if not document.is_empty():
		return document
	for pack_root in ModLoader.list_pack_roots():
		if selected_pack_root != "" and pack_root == selected_pack_root:
			continue
		document = _spellbook_document_for_faction(pack_root, faction)
		if not document.is_empty():
			return document
	_spellbook_resolution_note = "no spellbook document for faction '%s' in any mounted pack" % (faction if faction != "" else "<unset>")
	return ContentDB.get_spellbook_runtime() if faction == "" and ContentDB.has_method("get_spellbook_runtime") else {}


func _spellbook_document_for_faction(pack_root: String, faction: String) -> Dictionary:
	if pack_root == "" or faction == "":
		return {}
	var pack_document := ModLoader._read_json(pack_root.path_join("pack.json")) as Dictionary
	var files: Dictionary = pack_document.get("files", {}) as Dictionary
	var keys: Array[String] = []
	for key_value in files.keys():
		if String(key_value).begins_with("spellbook."):
			keys.append(String(key_value))
	keys.sort()
	for key in keys:
		var relative := String(files.get(key, ""))
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			continue
		var document := ModLoader._read_json(ModLoader.resolve_pack_path(pack_root, relative)) as Dictionary
		if document.is_empty() or String(document.get("schema", "")) != "openbfme.spellbook-runtime":
			continue
		var target_faction := String((document.get("target", {}) as Dictionary).get("faction", ""))
		if target_faction.to_lower() != faction.to_lower():
			continue
		document["_pack_root"] = pack_root
		document["_pack_file_key"] = key
		return document
	return {}


func _apply_menu_match_options() -> void:
	## Menu setup seam: the chosen house colors ride GameState into the retail
	## mask-recolor application (defaults are the authored blue/red rows).
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null:
		return
	HouseColorScript.team_color_overrides[0] = game_state.get("retail_player_color")
	HouseColorScript.team_color_overrides[1] = game_state.get("retail_enemy_color")


func _match_configuration() -> Dictionary:
	## Simulation configuration with the menu's player-start choice applied
	## (options seam; 0 keeps the authored assignment — the human then takes
	## Player_2_Start and the AI takes Player_1_Start). Unknown starts fail
	## closed back to the authored configuration, never to a wrong position.
	var configuration := source_map_data.simulation_configuration()
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null:
		return configuration
	var choice := int(game_state.get("retail_player_start_index"))
	if choice <= 0:
		return configuration
	var starts := _read_waypoint_player_starts()
	if not starts.has(choice):
		return configuration
	var enemy_index := -1
	for candidate in [1, 2]:
		if candidate != choice and starts.has(candidate):
			enemy_index = candidate
			break
	if enemy_index < 0:
		for candidate_value in starts.keys():
			if int(candidate_value) != choice:
				enemy_index = int(candidate_value)
				break
	if enemy_index < 0:
		return configuration
	var spawn_positions := (configuration.get("spawn_positions", {}) as Dictionary).duplicate(true)
	if spawn_positions.is_empty():
		return configuration
	var player_local: Vector2 = starts[choice]
	var enemy_local: Vector2 = starts[enemy_index]
	spawn_positions[1] = source_map_data._walkable_spawn(Vector2(player_local.x, player_local.y - 4.5))
	spawn_positions[2] = source_map_data._walkable_spawn(Vector2(player_local.x, player_local.y + 4.5))
	spawn_positions[101] = source_map_data._walkable_spawn(Vector2(enemy_local.x, enemy_local.y - 4.5))
	spawn_positions[102] = source_map_data._walkable_spawn(Vector2(enemy_local.x, enemy_local.y + 4.5))
	configuration["spawn_positions"] = spawn_positions
	# The authored home layout is pinned to the default starts; a chosen start
	# makes the sim derive the base around the new anchor instead.
	if configuration.has("home_layout"):
		configuration.erase("home_layout")
	return configuration


func _read_waypoint_player_starts() -> Dictionary:
	## All authored player starts from the map's cooked waypoints document,
	## converted to local map coordinates by the map's own transform. Keyed by
	## the retail playerIndex (1..N).
	var starts := {}
	if map_pack_root == "" or source_map_data == null:
		return starts
	var source_path := String(_loaded_map_definition.get("_source", ""))
	if source_path == "":
		var map_relative := String(_loaded_map_definition.get("map", ""))
		if map_relative != "":
			source_path = map_relative
	if source_path.begins_with(map_pack_root):
		source_path = source_path.substr(map_pack_root.length()).trim_prefix("/")
	if source_path == "" or source_path.contains(":") or source_path.begins_with("/"):
		return starts
	var document := _read_bounded_pack_document(map_pack_root, source_path.get_base_dir().path_join("waypoints.json"), MAP_DOCUMENT_MAX_BYTES)
	for start_name_value in (document.get("playerStarts", {}) as Dictionary).keys():
		var row := (document.get("playerStarts", {}) as Dictionary)[start_name_value] as Dictionary
		var player_index := int(row.get("playerIndex", -1))
		var position := _godot_position(row.get("godotPosition", []))
		if player_index > 0 and position != Vector3.INF:
			var local: Vector3 = source_map_data.source_to_local(position)
			starts[player_index] = Vector2(local.x, local.z)
	return starts


func _godot_position(value: Variant) -> Vector3:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() < 3:
		return Vector3.INF
	var row := value as Array
	return Vector3(float(row[0]), float(row[1]), float(row[2]))


func reset_match() -> void:
	if simulation == null:
		return
	_configure_simulation_spellbook()
	simulation.setup(_match_configuration(), gameplay_rules)
	# A fresh match starts unpaused with the spellbook closed (setup() also
	# clears the orb clock-pause seam).
	if hud != null and hud.has_method("close_powers_palette"):
		hud.close_powers_palette()
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
	var accepted := int(_apply_local_command("issue_move", {"ids": simulation.selected_ids.duplicate(), "destination": destination}))
	_sync_presentation()
	return accepted


func test_attack(target_id: int) -> int:
	var accepted := int(_apply_local_command("issue_attack", {"ids": simulation.selected_ids.duplicate(), "target_id": target_id}))
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
	# Ranger/trebuchet typed contracts are Men-only content; other factions
	# receive every command surface from their own playableUnit documents.
	# Full Men packs with GondorRanger as a playableUnit skip the overlay HUD path.
	var men_hud_faction := String(faction_manifest.get("faction", FactionManifestScript.DEFAULT_FACTION)) == FactionManifestScript.DEFAULT_FACTION
	var use_ranger_overlay := men_hud_faction and not _playable_has_ranger()
	ranger_hud_configuration_error = hud.enable_ranger_content(ContentDB.get_ranger_runtime() if use_ranger_overlay else {})
	trebuchet_hud_configuration_error = hud.enable_trebuchet_content(ContentDB.get_trebuchet_runtime() if men_hud_faction else {})
	playable_unit_hud_configuration_error = hud.enable_playable_unit_content(producible_unit_runtimes, _producer_kind_registry())
	# Construct action specs must be registered before build() creates buttons.
	# The faction must flow with them: without it the Men construct templates
	# bind on every faction's porter (the elves-uses-Men-art/strings bug).
	hud.configure_manifest_construct_kinds(
		faction_manifest.get("structure_kinds", []) as Array,
		String(faction_manifest.get("faction", "")),
		faction_manifest.get("structure_training_summaries", {}) as Dictionary,
		faction_manifest.get("structure_construct_icons", {}) as Dictionary
	)
	var spellbook_runtime: Dictionary = _faction_spellbook_document()
	if not spellbook_runtime.is_empty() and hud.has_method("configure_spellbook_runtime"):
		hud.configure_spellbook_runtime(spellbook_runtime)
	layer.add_child(hud)
	hud.build()
	# Heading label only exists after build.
	if hud.has_method("configure_faction_surface"):
		hud.configure_faction_surface(faction_manifest)
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
	minimap.order_requested.connect(func(world_position: Vector2) -> void:
		if not ready_ok or simulation_paused:
			return
		_issue_order_at(world_position)
	)
	pause_panel = hud.pause_panel
	failure_panel = hud.failure_panel
	_install_pause_settings_button()
	hud.pause_requested.connect(toggle_escape_menu)
	hud.restart_requested.connect(reset_match)
	hud.main_menu_requested.connect(func() -> void:
		# Unpause before leaving: a pause-open exit must not freeze the main
		# menu's scene tree behind us (buttons dead on a paused tree).
		simulation_paused = false
		get_tree().paused = false
		get_tree().change_scene_to_file("res://scenes/boot.tscn"))
	hud.quit_requested.connect(func() -> void: get_tree().quit())
	hud.group_assign_requested.connect(_assign_group)
	hud.group_recall_requested.connect(_recall_group)
	hud.train_requested.connect(_queue_selected_producer)
	hud.structure_upgrade_requested.connect(_upgrade_selected_structure)
	hud.battalion_upgrade_requested.connect(_on_battalion_upgrade_requested)
	hud.cancel_production_requested.connect(_cancel_selected_production)
	hud.attack_move_requested.connect(_arm_attack_move)
	hud.stop_requested.connect(_stop_selected_units)
	hud.stance_requested.connect(_toggle_selected_stance)
	if hud.has_signal("formation_requested"):
		hud.formation_requested.connect(_toggle_selected_formation)
	hud.command_cap_changed.connect(func(value: int) -> void:
		simulation.command_point_cap = maxi(60, value)
		hud.set_feedback("Command point cap set to %d." % simulation.command_point_cap)
	)
	hud.powers_opened.connect(func() -> void:
		# Single-player: the open spellbook pauses the sim clock (existing
		# seam; the escape-menu pause composes independently).
		_apply_local_command("set_spellbook_orb_open", {"open": true})
		hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	)
	hud.power_purchase_requested.connect(func(power_id: String, cost: int) -> void:
		var result: Dictionary = _apply_local_command("purchase_power", {"power_id": power_id, "cost": cost})
		if bool(result.get("ok", false)):
			hud.set_feedback("Power acquired: %s." % hud.power_display_name(power_id))
		else:
			hud.set_feedback("Cannot acquire power: %s." % String(result.get("reason", "rejected")).replace("-", " "), true)
		hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	)
	hud.powers_reset_requested.connect(func() -> void:
		# Retail RESET: refund this session's unspent picks and re-pick.
		var result: Dictionary = _apply_local_command("reset_spellbook_purchases")
		if int(result.get("refunded", 0)) > 0:
			hud.set_feedback("Picks reset: %d power point%s refunded." % [int(result.get("refunded", 0)), "" if int(result.get("refunded", 0)) == 1 else "s"])
		else:
			hud.set_feedback("No unspent picks to reset.")
		hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	)
	hud.powers_closed.connect(func() -> void:
		# Every orb close path is the retail ACCEPT commit — and resumes the
		# sim clock paused on open.
		_apply_local_command("set_spellbook_orb_open", {"open": false})
		_apply_local_command("accept_spellbook_purchases")
		hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	)
	hud.power_cast_requested.connect(func(power_id: String) -> void:
		ability_cast_armed = {}
		power_cast_armed = power_id
		hud.set_feedback("Choose a target area for %s (left-click casts; right-click or Esc cancels)." % hud.power_display_name(power_id))
	)
	hud.ability_cast_requested.connect(func(unit_id: String, ability_id: String) -> void:
		var hero_id := _selected_hero_for_ability(unit_id)
		if hero_id == 0:
			return
		var targeting := "self"
		for rule_value in simulation.ability_rules_for_unit(unit_id):
			if String((rule_value as Dictionary).get("ability_id", "")) == ability_id:
				targeting = String((rule_value as Dictionary).get("targeting", "self"))
		if targeting == "self":
			var result: Dictionary = _apply_local_command("cast_ability", {"hero_id": hero_id, "ability_id": ability_id, "target_point": Vector2(simulation.entity(hero_id).get("position", Vector2.ZERO))})
			_report_ability_cast(unit_id, ability_id, result)
			_sync_presentation()
			return
		power_cast_armed = ""
		ability_cast_armed = {"hero_id": hero_id, "ability_id": ability_id, "unit_id": unit_id}
		hud.set_feedback("Choose a target for %s (left-click casts; right-click or Esc cancels)." % _ability_display_name(unit_id, ability_id))
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
	hud.cheat_resources_requested.connect(_grant_test_resources)
	hud.construct_requested.connect(_arm_construction)
	hud.hero_recall_requested.connect(_on_hero_recall_requested)
	hud.expansion_requested.connect(_on_expansion_requested)
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
	# Retail edge scroll: the cursor resting on a screen border pans the map.
	if DisplayServer.get_name() != "headless" and not _camera_orbiting and _right_drag_origin == Vector2.INF:
		var edge := 12.0
		var viewport_size := get_viewport().get_visible_rect().size
		var mouse_position := get_viewport().get_mouse_position()
		if Rect2(Vector2.ZERO, viewport_size).has_point(mouse_position):
			if mouse_position.x <= edge:
				input_direction.x -= 1.0
			elif mouse_position.x >= viewport_size.x - edge:
				input_direction.x += 1.0
			if mouse_position.y <= edge:
				input_direction.y -= 1.0
			elif mouse_position.y >= viewport_size.y - edge:
				input_direction.y += 1.0
	if input_direction.length_squared() > 0.0:
		var forward := _camera_forward_local()
		var right := Vector2(-forward.y, forward.x)
		var movement := right * input_direction.x - forward * input_direction.y
		camera_focus += movement.normalized() * delta * OPENBFME_KEYBOARD_SCROLL_BASE_LOCAL_PER_SECOND * keyboard_scroll_speed_scale * FORDS_CAMERA_SCROLL_SPEED_SCALAR
		_clamp_camera_focus()
	_apply_camera_transform()
	_update_hover_cursor()


## Retail shows an attack cursor when the cursor rests over an enemy while a
## combat selection is active (stock cross shape — styled chrome, not art).
func _update_hover_cursor() -> void:
	if DisplayServer.get_name() == "headless" or simulation == null:
		return
	var shape := Input.CURSOR_ARROW
	if (
		not simulation.selected_ids.is_empty()
		and construction_kind_armed == ""
		and ability_cast_armed.is_empty()
		and power_cast_armed == ""
	):
		var world: Variant = _screen_to_world(get_viewport().get_mouse_position())
		if world != null:
			var point := Vector2((world as Vector3).x, (world as Vector3).z)
			var enemy_id := _closest_battalion(point, 1, 6.0)
			if enemy_id == 0:
				enemy_id = _closest_structure(point, 1)
			if enemy_id != 0:
				shape = Input.CURSOR_CROSS
	if Input.get_current_cursor_shape() != shape:
		Input.set_default_cursor_shape(shape)


func _grant_test_resources() -> void:
	if simulation == null:
		return
	var grant := 50000
	simulation.team_resources[0] = int(simulation.team_resources.get(0, 0)) + grant
	simulation.team_resources[1] = int(simulation.team_resources.get(1, 0)) + grant
	if simulation.team_power_points is Dictionary:
		simulation.team_power_points[0] = int(simulation.team_power_points.get(0, 0)) + 10
		simulation.team_power_points[1] = int(simulation.team_power_points.get(1, 0)) + 10
	hud.set_resources(simulation.resources_for_team(local_team), simulation.command_points_for_team(local_team), simulation.command_point_cap)
	hud.refresh_powers(simulation.power_points(local_team), simulation.purchased_powers[local_team], simulation.spellbook_ui_state(local_team))
	hud.set_feedback("Cheat: +%d resources and +10 power points (both teams)." % grant)


var _power_fx_event_index := 0


func _consume_power_fx_events() -> void:
	## Spellbook casts get a visible world cue at the target point (retail
	## plays authored FX lists; this is the provisional stand-in).
	var events: Array = simulation.events
	while _power_fx_event_index < events.size():
		var event: Dictionary = events[_power_fx_event_index]
		_power_fx_event_index += 1
		if String(event.get("kind", "")) != "power.cast":
			continue
		var point_value: Variant = event.get("point")
		if typeof(point_value) != TYPE_ARRAY or (point_value as Array).size() < 2:
			continue
		var point := Vector2(float((point_value as Array)[0]), float((point_value as Array)[1]))
		var is_heal := String(event.get("power_id", "")) == "SpellBookHeal"
		_spawn_power_flash(point, Color(0.45, 1.0, 0.55) if is_heal else Color(1.0, 0.85, 0.4))


func _spawn_power_flash(point: Vector2, color: Color) -> void:
	if DisplayServer.get_name() == "headless":
		return
	var ring := MeshInstance3D.new()
	var mesh := TorusMesh.new()
	mesh.inner_radius = 2.3
	mesh.outer_radius = 2.6
	ring.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color
	ring.material_override = material
	ring.position = Vector3(point.x, _presentation_height(point) + 0.15, point.y)
	add_child(ring)
	var tween := create_tween()
	tween.set_parallel(true)
	tween.tween_property(ring, "scale", Vector3(2.4, 1.0, 2.4), 0.9).from(Vector3(0.3, 1.0, 0.3))
	tween.tween_property(material, "albedo_color:a", 0.0, 0.9).from(0.9)
	tween.chain().tween_callback(ring.queue_free)


func _issue_order_at(world_point: Vector2) -> void:
	## Shared right-click order policy for the world viewport and the radar:
	## selected units get move/attack, a selected building gets its rally.
	if not simulation.selected_ids.is_empty():
		_handle_right_click(world_point)
	elif selected_structure_id != 0:
		var rally_result: Dictionary = _apply_local_command("set_structure_rally", {"structure_id": selected_structure_id, "position": world_point})
		if bool(rally_result.get("ok", false)):
			hud.set_feedback("Rally point set.")


func _pan_camera_screen(relative: Vector2) -> void:
	## Grab-the-map panning: the terrain point under the cursor stays under the
	## cursor, so the focus shifts by the world delta of the drag.
	var center := get_viewport().get_visible_rect().size * 0.5
	var from_value: Variant = _screen_to_world(center)
	var to_value: Variant = _screen_to_world(center - relative)
	if from_value == null or to_value == null:
		return
	var from_world := from_value as Vector3
	var to_world := to_value as Vector3
	camera_focus += Vector2(to_world.x - from_world.x, to_world.z - from_world.z)
	_clamp_camera_focus()
	_apply_camera_transform()


func _reset_camera_home() -> void:
	if simulation == null:
		return
	var fortress := simulation.fortress_id(local_team)
	if fortress != 0:
		camera_focus = Vector2(simulation.structure(fortress).get("position", Vector2.ZERO))
	camera_user_yaw = 0.0
	camera_zoom_target = 1.0
	camera_zoom = 1.0
	_clamp_camera_focus()
	_apply_camera_transform()
	if hud != null:
		hud.set_feedback("Camera reset.")


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


func _apply_stored_display_settings() -> void:
	## The shell's stored display/graphics settings apply on every entry path
	## (menu boot, loading boot, direct slice boot), so the user's window
	## mode/resolution/quality choice survives the whole flow in one window.
	var display := UserSettingsScript.load_display()
	OptionsScreenScript.apply_display_settings(String(display["window_mode"]), String(display["resolution"]))
	OptionsScreenScript.apply_graphics_preset(String(UserSettingsScript.load_graphics()["preset"]), get_viewport())


func _exit_tree() -> void:
	if control_server != null:
		control_server.stop()
		control_server = null
	if audio_system != null:
		audio_system.dispose()
	var asset_factory = load("res://src/view/asset_factory.gd")
	asset_factory.clear_mesh_cache()
