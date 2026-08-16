class_name RetailStructure
extends Node3D
## Fail-closed authored lifecycle presentation shared by private Men and neutral
## structures. Every declared retail GLB is contained/preflighted up front;
## phase scenes are instantiated lazily and share the intact body's transform.

const ShadowDecalScript = preload("res://src/retail_slice/retail_shadow_decal.gd")
const SelectionPick = preload("res://src/retail_slice/retail_selection_pick.gd")
const UnitAdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const TransitionDamageFXScript = preload("res://src/retail_slice/transition_damage_fx.gd")
const LIFECYCLE_SCHEMA := "openbfme.building-lifecycle-presentation"
const LIFECYCLE_SCHEMA_VERSION := 0
const LIFECYCLE_SCHEMA_VERSION_V1 := 1
const COMPOSED_EVIDENCE_PROFILE := "composed-structure-runtime"
const MEN_LIFECYCLE_OBJECT_IDS := {
	"bfme2.object.men-fortress": true,
	"bfme2.object.men-farm": true,
	"bfme2.object.men-barracks": true,
	"bfme2.object.men-archery-range": true,
	"bfme2.object.men-stable": true,
}
const BODY_PHASES: Array[String] = ["construction", "intact", "damaged", "reallyDamaged", "rubble"]
const BODY_PATH_KEYS: Array[String] = ["construction", "intact", "damaged", "reallyDamaged", "rubble", "bib"]
const DOOR_PATH_KEYS: Array[String] = ["construction", "closed", "rubble"]
const V1_PHASES: Array[String] = [
	"construction",
	"intact",
	"damaged",
	"really-damaged",
	"collapsing",
	"rubble",
	"post-rubble",
	"post-collapse",
]
const V1_NEXT_PHASES := {
	"construction": "intact",
	"intact": "damaged",
	"damaged": "really-damaged",
	"really-damaged": "collapsing",
	"collapsing": "rubble",
	"rubble": "post-rubble",
	"post-rubble": null,
	"post-collapse": null,
}
const DEFAULT_TARGET_HEIGHT := 7.0
const MAX_ROUTE_DOCUMENT_BYTES := 2 * 1024 * 1024
const MAX_DRAWABLE_AUDIO_RECEIPTS := 128
signal lifecycle_route_requested(request: Dictionary)

var entity_id := 0
var team := 0
var structure_kind := ""
var selected := false
var health_ratio := 1.0
var construction_ratio := 1.0
var presentation_mode := "unconfigured"
var retail_visual_loaded := false
var retail_mesh_path := ""
## World-unit footprint radius used for mouse picking and for the selection
## ring. Resolved from the compiled retail Geometry block when the selected pack
## carries one, otherwise from the intact body's own horizontal bounds. Never a
## flat constant: see RetailSelectionPick for why that broke picking.
var pick_radius := 4.0
var selection_radius_source := "unresolved"
## Compiled Geometry document (primary + AdditionalGeometry pieces). Empty when
## the selected pack predates the projection.
var compiled_geometry: Dictionary = {}
## Intact-body AABB centre in local XZ. The selection ring and pick floor sit
## here so an offset keep / well / statue is not picked at a stale origin.
var visual_pick_center := Vector2.ZERO

# Deterministic public lifecycle state used by the focused gate and diagnostics.
var current_lifecycle_phase := ""
var active_body_path := ""
var active_bib_path := ""
var active_door_path := ""
var active_animation_clip := ""
var active_animation_mode := ""
var active_visual_mode := ""
var active_entering_fx := ""
var active_audio_event := ""
var active_particle_system_ids: Array[String] = []
var drawable_actions_applied := 0
var drawable_action_gaps: Array[Dictionary] = []
var drawable_audio_requests: Array[Dictionary] = []
var transition_error := ""
var route_dispatch_error := ""
var route_blockers: Array[Dictionary] = []
var last_route_request: Dictionary = {}
var last_route_dispatch: Dictionary = {}
var rebuild_hole_present := false
var rebuild_hole_phase := ""
var rebuild_hole_body_path := ""
var rebuild_hole_maximum_health := 0
var rebuild_hole_health_regen_percent_per_second := 0.0
var rebuild_hole_fade_in_seconds := 0.0
var rebuild_hole_terminal_duration := ""
var rebuild_hole_original_removal_frame: Variant = null
var rebuild_hole_original_removal_frame_status := ""
var contract_error := ""
var shared_uniform_scale := 1.0
var shared_vertical_offset := 0.0
## Authored retail Body module for this object, read verbatim from the compiled
## structure document (`ActiveBody` / `StructureBody` / `ImmortalBody` / ...).
## Empty when the document carries no health block (fixture and bounded-workshop
## seams), which reads as damageable — the pre-existing behaviour.
var body_module := ""

## --- Purchased structure levels (doc-driven L2/L3 presentation) ---
## The building's level rides the simulation row; the per-level model variant
## is the authored SubObjectsUpgrade visibility compiled into the structure
## document's upgradeChain (named sub-object nodes of one GLB, never separate
## invented meshes).
var level := 1
var upgrade_in_progress := false
var upgrade_progress := 0.0
var level_visible_sub_objects: Array[String] = []
var level_hidden_sub_objects: Array[String] = []
var level_unmatched_tokens: Array[String] = []
var level_applied_nodes: Dictionary = {}
var _level_presentations: Dictionary = {}
## Authored additive HEALTH modifiers per chain level (upgradeChain effects);
## keys are toLevel ints, values the health added when that level is reached.
var _level_health_additions: Dictionary = {}


var _bundle_object_id := ""
var _pack_root := ""
## Host/faction pack roots the lifecycle route registry may resolve from
## (structure pack first, then the selected host pack and faction pack roots).
var _allowed_pack_roots: Array[String] = []
var scenario_descriptor_maximum_health: Variant = null
var scenario_authoritative_maximum_health := 0
var _lifecycle: Dictionary = {}
var _drawable_scripts: Array = []
## Typed TransitionDamageFX rows. Empty means no authored crossing FX.
var transition_damage_fx_contracts: Array = []
var last_transition_damage_fx: Dictionary = {}
var _resolved_paths: Dictionary = {}
var _fixture_visuals: Dictionary = {}
var _loaded_visuals: Dictionary = {}
var _fixture_mode := false
var _target_height := DEFAULT_TARGET_HEIGHT
var _selection_ring: MeshInstance3D
var _health_back: Sprite3D
var _health_fill: Sprite3D
var _build_back: Sprite3D
var _build_fill: Sprite3D
var _visual_root: Node3D
var _model_host: Node3D
var _active_body: Node3D
var _geometry_upgrade_baseline: Dictionary = {}
var _active_bib: Node3D
var _active_door: Node3D
var _rebuild_hole_visual: Node3D
var _runtime_route_registry: Dictionary = {}
var _source_unit_scale := 0.0
var _pending_route_phase := ""
var _pending_drawable_audio_requests: Array[Dictionary] = []
var _drawable_audio_activation_key := ""
var _bounded_phase_paths: Dictionary = {}
var _health_bar_width := 3.6
var _visual_top_y := DEFAULT_TARGET_HEIGHT
var _aura_ring: MeshInstance3D
var _water_fx: Node3D
var aura_radius_source := 0.0
var aura_radius_local := 0.0
var aura_effect_kind := ""
var aura_upgrade_required := ""
var water_fx_present := false
var _structure_upgrade_ids: Dictionary = {}
var _well_water_authored := false
var _well_water_resolved := false
var _scenario_game := ""
const HEALTH_BAR_SCREEN_WIDTH_PX := 64
const HEALTH_BAR_SCREEN_HEIGHT_PX := 5
const HEALTH_BAR_ROOF_GAP := 0.12
## Retail gamedata.ini:2321 / 2337. PassiveAreaEffectBehavior on GondorWell and
## GondorStatue authors these macros; the current pack records the token but
## not the resolved number. The table is the cited retail value, not a guess.
const AREA_EFFECT_RADIUS_SOURCE := {
	"GONDOR_WELL_AOE_RADIUS": 200.0,
	"GONDOR_STATUE_AOE_RADIUS": 200.0,
}
const WELL_WATER_FX_ID := "WellHealFX"


func _enter_tree() -> void:
	if not _pending_drawable_audio_requests.is_empty():
		var pending := _pending_drawable_audio_requests.duplicate(true)
		_pending_drawable_audio_requests.clear()
		_emit_drawable_audio_requests(pending)
	if _pending_route_phase != "":
		_publish_v1_route_request(_pending_route_phase)


func configure(entity: Dictionary, bundle_object_id: String = "", source_unit_scale: float = 0.0, scenario_game: String = "") -> void:
	_scenario_game = scenario_game.to_lower() if scenario_game.to_lower() in ["bfme2", "rotwk"] else ""
	scenario_authoritative_maximum_health = int(entity.get("maximum_health", 0))
	_prepare_identity(entity, bundle_object_id)
	_source_unit_scale = source_unit_scale if is_finite(source_unit_scale) and source_unit_scale > 0.0 else 0.0
	_seed_selection_radius()
	_build_visual_root()
	_configure_selected_pack_contract(bundle_object_id)
	_build_markers()
	_ingest_structure_upgrades(entity)
	_configure_area_effect_presentation()
	sync_state(entity)


func configure_fixture(
	entity: Dictionary,
	lifecycle: Dictionary,
	fixture_visuals: Dictionary,
	height_millimeters: float = DEFAULT_TARGET_HEIGHT * 1000.0
) -> void:
	## Legal-safe focused-test seam. It exercises the exact contract, state
	## selection, lazy swapping, shared transform, and animation rules without
	## pretending fixture geometry is retail content or bypassing production
	## containment checks.
	_prepare_identity(entity, "fixture.%s" % String(entity.get("structure_kind", "unknown")))
	_seed_selection_radius()
	_fixture_mode = true
	_fixture_visuals = fixture_visuals.duplicate()
	_build_visual_root()
	var presentation := {
		"model": _intact_visual_path(lifecycle),
		"heightMillimeters": height_millimeters,
		"buildingLifecycle": lifecycle,
	}
	_configure_contract(presentation, lifecycle)
	_build_markers()
	sync_state(entity)


func sync_state(entity: Dictionary) -> void:
	# Remove the previous GeometryUpgrade overlay before level/phase presenters
	# establish this frame's selected-model baseline.
	_restore_geometry_upgrade_baseline()
	var entity_maximum := int(entity.get("maximum_health", 0))
	var bounded_workshop := presentation_mode == "bounded-workshop-model-state-evidence"
	var lifecycle_maximum := entity_maximum if bounded_workshop else maximum_health_for_lifecycle(_lifecycle)
	if not bounded_workshop:
		# Authored level chains add health per purchased level (the compiled
		# upgradeChain's HEALTH modifiers); the expected pool is the lifecycle
		# base plus every authored addition at or below the entity's level.
		lifecycle_maximum += _health_addition_for_level(int(entity.get("level", 1)))
	if not bounded_workshop and contract_error == "" and entity_maximum != lifecycle_maximum:
		_set_contract_error(
			"entity maximum_health %d does not equal lifecycle maximum health %d"
			% [entity_maximum, lifecycle_maximum]
		)
	var maximum := maxi(1, entity_maximum)
	var health := int(entity.get("health", maximum))
	health_ratio = clampf(float(health) / float(maximum), 0.0, 1.0)
	construction_ratio = clampf(float(entity.get("construction_progress", 1.0)), 0.0, 1.0)
	# Purchased levels: the simulation row's level drives the authored per-level
	# sub-object visibility; a queued upgrade marks the in-progress state (the
	# slice refines the live progress below after each sync).
	var entity_upgrade_queue: Array = entity.get("upgrade_queue", [])
	set_level(
		int(entity.get("level", 1)),
		not entity_upgrade_queue.is_empty(),
		0.0 if entity_upgrade_queue.is_empty() else upgrade_progress
	)
	if contract_error == "" and bounded_workshop:
		_sync_bounded_workshop_phase(health, construction_ratio)
	elif contract_error == "":
		if int(_lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
			if current_lifecycle_phase == "":
				_activate_phase(String(_lifecycle.get("initialPhase", "")))
			# Every v1 lifecycle drives its phase from live health/construction
			# facts. This was once gated to the five men structure ids, which
			# left manifest-resolved structures stuck in their initial phase —
			# player-built structures popped in fully built with no buildup.
			_sync_men_damage_phase(health, construction_ratio)
		else:
			var next_phase := phase_for_state(health, construction_ratio, _lifecycle)
			if next_phase != current_lifecycle_phase:
				_activate_phase(next_phase)
			elif next_phase == "construction":
				_apply_declared_phase_animation(_active_body, next_phase, construction_ratio)
	_ensure_shadow_decal()
	if _visual_root != null:
		# Rubble is an authored retained phase. Construction progress is animation
		# time, never a non-uniform Y scale and never a visibility shortcut.
		_visual_root.visible = contract_error == ""
	if _selection_ring != null:
		_selection_ring.visible = selected and health > 0 and contract_error == ""
	var show_health_bar := (
		contract_error == ""
		and health > 0
		and active_visual_mode != "no-render"
		and draws_health_bar()
		and (selected or health < maximum)
	)
	if _health_fill != null:
		_health_fill.visible = show_health_bar
		_health_fill.scale.x = maxf(0.001, health_ratio)
		_health_fill.offset.x = (health_ratio - 1.0) * HEALTH_BAR_SCREEN_WIDTH_PX * 0.5
	if _health_back != null:
		_health_back.visible = show_health_bar
	var under_construction := contract_error == "" and construction_ratio < 1.0 and health > 0
	if _build_back != null:
		_build_back.visible = under_construction
	if _build_fill != null:
		_build_fill.visible = under_construction
		_build_fill.scale.x = maxf(0.001, construction_ratio)
		_build_fill.offset.x = (construction_ratio - 1.0) * HEALTH_BAR_SCREEN_WIDTH_PX * 0.5
	_ingest_structure_upgrades(entity)
	apply_geometry_upgrade_visibility(entity.get("geometry_visibility", {}) as Dictionary)
	_sync_aura_visibility()
	_sync_water_fx()
	_update_lifecycle_metadata()


func _sync_men_damage_phase(health: int, construction_progress: float) -> void:
	var target := "intact"
	if construction_progress < 1.0:
		target = "construction"
	elif health <= 0:
		# Zero health proves entry into authored collapse. Completion timing is not
		# proven, so repeated simulation syncs retain collapsing/rubble/terminal
		# states and never manufacture a timer-driven advance.
		if current_lifecycle_phase in ["collapsing", "rubble", "post-rubble", "post-collapse"]:
			return
		target = "collapsing"
	else:
		var facts: Dictionary = _lifecycle.get("simulationFacts", {}) as Dictionary
		var rule: Dictionary = facts.get("damageStateRule", {}) as Dictionary
		if health <= int(rule.get("reallyDamagedThreshold", -1)):
			target = "really-damaged"
		elif health <= int(rule.get("damagedThreshold", -1)):
			target = "damaged"
	if target != current_lifecycle_phase:
		# Lifecycles are free to omit optional phases (e.g. no construction on
		# preplaced-only structures); staying in the current phase is correct
		# there, while activating an undeclared phase would fail the contract.
		if _v1_phase_row(_lifecycle, target).is_empty():
			return
		_activate_phase(target)
		_sync_water_fx()
	elif target == "construction":
		_apply_declared_phase_animation(_active_body, target, construction_progress)


func set_selected(value: bool) -> void:
	selected = value
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0 and contract_error == "" and active_visual_mode != "no-render"
	_sync_aura_visibility()


func lifecycle_state() -> Dictionary:
	return {
		"phase": current_lifecycle_phase,
		"activeBodyPath": active_body_path,
		"activeBibPath": active_bib_path,
		"activeDoorPath": active_door_path,
		"activeAnimationClip": active_animation_clip,
		"activeAnimationMode": active_animation_mode,
		"activeVisualMode": active_visual_mode,
		"activeEnteringFx": active_entering_fx,
		"activeAudioEvent": active_audio_event,
		"activeParticleSystemIds": active_particle_system_ids.duplicate(),
		"transitionError": transition_error,
		"routeDispatchError": route_dispatch_error,
		"routeBlockers": route_blockers.duplicate(true),
		"lastRouteRequest": last_route_request.duplicate(true),
		"lastRouteDispatch": last_route_dispatch.duplicate(true),
		"terminalNoRender": (
			current_lifecycle_phase in ["post-rubble", "post-collapse"]
			and active_visual_mode == "no-render"
			and _active_body == null
		),
		"rebuildHolePresent": rebuild_hole_present,
		"rebuildHolePhase": rebuild_hole_phase,
		"rebuildHoleBodyPath": rebuild_hole_body_path,
		"rebuildHoleMaximumHealth": rebuild_hole_maximum_health,
		"rebuildHoleHealthRegenPercentPerSecond": rebuild_hole_health_regen_percent_per_second,
		"rebuildHoleFadeInSeconds": rebuild_hole_fade_in_seconds,
		"rebuildHoleTerminalDuration": rebuild_hole_terminal_duration,
		"rebuildHoleOriginalRemovalFrame": rebuild_hole_original_removal_frame,
		"rebuildHoleOriginalRemovalFrameStatus": rebuild_hole_original_removal_frame_status,
		"contractError": contract_error,
		"sharedUniformScale": shared_uniform_scale,
		"sharedVerticalOffset": shared_vertical_offset,
		"retainedRubble": current_lifecycle_phase == "rubble" and _active_body != null and _active_body.visible,
	}


func set_authoritative_lifecycle_phase(phase: String, progress: float = 1.0) -> bool:
	## Version 1 presentation is driven by deterministic simulation state. Clip
	## completion is never allowed to advance the lifecycle on its own.
	if contract_error != "" or int(_lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return false
	if _v1_phase_row(_lifecycle, phase).is_empty():
		_set_contract_error("authoritative lifecycle phase is not declared: %s" % phase)
		return false
	transition_error = ""
	construction_ratio = clampf(progress, 0.0, 1.0)
	_activate_phase(phase)
	if contract_error == "" and phase in ["collapsing", "rubble", "post-rubble", "post-collapse"]:
		if not _ensure_rebuild_hole_visible():
			return false
	return contract_error == ""


func attack_presentation_height() -> float:
	## Source presentation height is already normalized into Godot world units.
	## Aim into the visible body instead of attaching arrows at the structure's
	## ground pivot; exact DamageFX attachment policy remains oracle-gated.
	return maxf(0.25, _target_height * 0.55)


func request_declared_lifecycle_transition() -> bool:
	## This is the guarded automatic-transition seam. Authoritative simulation
	## may set a proven phase directly, but the presenter never advances across
	## an unresolved original-game timing/order boundary by itself.
	if contract_error != "" or int(_lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return false
	var next_value: Variant = declared_next_phase()
	if next_value == null:
		transition_error = "current lifecycle phase has no declared next phase"
		_update_lifecycle_metadata()
		return false
	var next_phase := String(next_value)
	var blocker := _declared_transition_blocker(current_lifecycle_phase, next_phase)
	if blocker != "":
		transition_error = blocker
		_update_lifecycle_metadata()
		return false
	return set_authoritative_lifecycle_phase(next_phase, construction_ratio)


func set_authoritative_rebuild_hole_present(present: bool) -> bool:
	if contract_error != "" or int(_lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return false
	if present:
		return _ensure_rebuild_hole_visible()
	rebuild_hole_present = false
	rebuild_hole_phase = ""
	rebuild_hole_body_path = ""
	if _rebuild_hole_visual != null:
		_rebuild_hole_visual.visible = false
	_update_lifecycle_metadata()
	return true


func declared_next_phase() -> Variant:
	if int(_lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return null
	var row := _v1_phase_row(_lifecycle, current_lifecycle_phase)
	return row.get("nextPhase") if not row.is_empty() else null


func audio_event_id(role: String) -> String:
	if int(_lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return ""
	var events: Dictionary = _lifecycle.get("audioEvents", {}) as Dictionary
	var value: Variant = events.get(role)
	return String(value) if typeof(value) == TYPE_STRING else ""


func record_route_dispatch(result: Dictionary) -> void:
	## The presenter owns the declared identifiers; the slice controller owns
	## their audio/FX/particle consumers. A rejected dispatch remains explicit
	## state and never falls back to a guessed generic effect.
	last_route_dispatch = result.duplicate(true)
	route_dispatch_error = "" if bool(result.get("ok", false)) else String(result.get("reason", "route dispatch rejected"))
	_update_lifecycle_metadata()


static func validate_declared_route_registry(lifecycle: Dictionary, registry: Dictionary) -> String:
	## Testable fail-closed boundary used after selected-pack route discovery.
	## Registry values are evidence statuses, not permission to synthesize an
	## implementation. Unknown identifiers are rejected before presentation.
	if int(lifecycle.get("schemaVersion", -1)) != LIFECYCLE_SCHEMA_VERSION_V1:
		return ""
	for field in ["audioEvents", "effects"]:
		if typeof(lifecycle.get(field)) != TYPE_DICTIONARY:
			return "buildingLifecycle.%s is not an object" % field
	var audio_registry: Dictionary = registry.get("audioEvents", {}) as Dictionary
	for role_value in (lifecycle.get("audioEvents", {}) as Dictionary).keys():
		var event_value: Variant = (lifecycle.get("audioEvents", {}) as Dictionary)[role_value]
		if event_value == null:
			continue
		var event_id := String(event_value)
		if not audio_registry.has(event_id.to_lower()):
			return "buildingLifecycle audio identifier is absent from selected-pack runtime contracts: %s" % event_id
	var effects: Dictionary = lifecycle.get("effects", {}) as Dictionary
	# FX lists and particle systems are optional presentation. Full-faction
	# packs often ship lifecycle metadata before the effects cook lands;
	# missing identifiers no-op at runtime rather than blocking the slice.
	var _fx_registry: Dictionary = registry.get("fxLists", {}) as Dictionary
	var _particle_registry: Dictionary = registry.get("particleSystems", {}) as Dictionary
	for attachment_value in effects.get("particleAttachments", []) as Array:
		if typeof(attachment_value) != TYPE_DICTIONARY:
			return "buildingLifecycle particle attachment is not an object"
		var attachment: Dictionary = attachment_value
		var particle_id := String(attachment.get("particleSystemId", ""))
		if particle_id != "" and not _safe_runtime_identifier(particle_id):
			return "buildingLifecycle particle attachment has an invalid system ID"
	return ""


static func phase_for_state(health: int, construction_progress: float, lifecycle: Dictionary) -> String:
	if construction_progress < 1.0:
		return "construction"
	if health <= 0:
		return "rubble"
	if health <= int(lifecycle.get("reallyDamagedHealth", -1)):
		return "reallyDamaged"
	if health <= int(lifecycle.get("damagedHealth", -1)):
		return "damaged"
	return "intact"


static func validate_lifecycle_contract(
	lifecycle: Dictionary,
	expected_structure_kind: String,
	declared_model: String = "",
	expected_maximum_health: int = 0,
	expected_object_id: String = ""
) -> String:
	var version := int(lifecycle.get("schemaVersion", -1))
	if version == LIFECYCLE_SCHEMA_VERSION:
		return _validate_v0_lifecycle_contract(
			lifecycle,
			expected_structure_kind,
			declared_model,
			expected_maximum_health
		)
	if version == LIFECYCLE_SCHEMA_VERSION_V1:
		return _validate_v1_lifecycle_contract(
			lifecycle,
			declared_model,
			expected_maximum_health,
			expected_object_id
		)
	return "buildingLifecycle schemaVersion is neither 0 nor 1"


static func _validate_v0_lifecycle_contract(
	lifecycle: Dictionary,
	expected_structure_kind: String,
	declared_model: String,
	expected_maximum_health: int
) -> String:
	if String(lifecycle.get("schema", "")) != LIFECYCLE_SCHEMA:
		return "buildingLifecycle schema is not %s" % LIFECYCLE_SCHEMA
	for field in ["maxHealth", "damagedHealth", "reallyDamagedHealth"]:
		var value: Variant = lifecycle.get(field)
		if not _is_exact_integer(value):
			return "buildingLifecycle.%s is not an exact integer" % field
	var maximum := int(lifecycle.get("maxHealth", 0))
	var damaged := int(lifecycle.get("damagedHealth", -1))
	var really_damaged := int(lifecycle.get("reallyDamagedHealth", -1))
	if maximum <= 0:
		return "buildingLifecycle.maxHealth must be positive"
	if really_damaged <= 0 or damaged <= really_damaged or damaged >= maximum:
		return "buildingLifecycle health thresholds are not strictly ordered"
	if expected_maximum_health > 0 and maximum != expected_maximum_health:
		return "buildingLifecycle.maxHealth %d does not equal expected %d" % [maximum, expected_maximum_health]
	if typeof(lifecycle.get("paths")) != TYPE_DICTIONARY:
		return "buildingLifecycle.paths is not an object"
	var paths: Dictionary = lifecycle["paths"]
	for key in BODY_PATH_KEYS:
		var path_error := _validate_relative_glb_path(paths.get(key), "buildingLifecycle.paths.%s" % key)
		if path_error != "":
			return path_error
	var normalized_model := declared_model.replace("\\", "/")
	if normalized_model != "" and normalized_model != String(paths.get("intact", "")).replace("\\", "/"):
		return "presentation.model is not the lifecycle intact path"
	if typeof(lifecycle.get("bibDuringConstruction")) != TYPE_BOOL:
		return "buildingLifecycle.bibDuringConstruction is not a bool"
	if typeof(lifecycle.get("unresolved")) != TYPE_ARRAY:
		return "buildingLifecycle.unresolved is not an explicit list"
	if lifecycle.has("clips"):
		var clips_error := _validate_clip_contract(lifecycle.get("clips"))
		if clips_error != "":
			return clips_error
	if expected_structure_kind == "fortress":
		if typeof(lifecycle.get("components")) != TYPE_DICTIONARY:
			return "fortress buildingLifecycle.components is not an object"
		var components: Dictionary = lifecycle["components"]
		if typeof(components.get("door")) != TYPE_DICTIONARY:
			return "fortress buildingLifecycle.components.door is not an object"
		var door: Dictionary = components["door"]
		for key in DOOR_PATH_KEYS:
			if typeof(door.get(key)) != TYPE_DICTIONARY:
				return "buildingLifecycle.components.door.%s is not an object" % key
			var door_row: Dictionary = door[key]
			var door_error := _validate_relative_glb_path(
				door_row.get("path"),
				"buildingLifecycle.components.door.%s.path" % key
			)
			if door_error != "":
				return door_error
	elif lifecycle.has("components"):
		var ordinary_components: Variant = lifecycle.get("components")
		if typeof(ordinary_components) != TYPE_DICTIONARY or not (ordinary_components as Dictionary).is_empty():
			return "ordinary structure buildingLifecycle.components must be absent or empty"
	return ""


static func _validate_v1_lifecycle_contract(
	lifecycle: Dictionary,
	declared_model: String,
	expected_maximum_health: int,
	expected_object_id: String
) -> String:
	if lifecycle.has("schema") and String(lifecycle.get("schema", "")) != LIFECYCLE_SCHEMA:
		return "buildingLifecycle schema is not %s" % LIFECYCLE_SCHEMA
	var object_id := String(lifecycle.get("objectId", ""))
	if not _safe_content_object_id(object_id):
		return "buildingLifecycle.objectId is unsafe or missing"
	if expected_object_id != "" and object_id != expected_object_id:
		return "buildingLifecycle.objectId does not match the selected bundle object"
	if String(lifecycle.get("initialPhase", "")) != "intact":
		return "buildingLifecycle.initialPhase is not intact"
	if typeof(lifecycle.get("simulationFacts")) != TYPE_DICTIONARY:
		return "buildingLifecycle.simulationFacts is not an object"
	var facts: Dictionary = lifecycle["simulationFacts"]
	if not _is_exact_integer(facts.get("maximumHealth")):
		return "buildingLifecycle.simulationFacts.maximumHealth is not an exact integer"
	var composed := String(lifecycle.get("evidenceProfile", "")) == COMPOSED_EVIDENCE_PROFILE
	var rule_value: Variant = facts.get("damageStateRule")
	var has_damage_rule := typeof(rule_value) == TYPE_DICTIONARY
	if not has_damage_rule:
		# Only composed evidence may omit the damage rule, and only with the
		# explicit no-authored-thresholds marker; the phase chain must then
		# omit the damaged phases too (checked below).
		if not composed or rule_value != null:
			return "buildingLifecycle.simulationFacts.damageStateRule is not an object"
		if String(facts.get("damageStateRuleStatus", "")) != "no-authored-damage-thresholds":
			return "buildingLifecycle damage-rule omission is not explicit"
	var maximum := int(facts.get("maximumHealth", 0))
	if maximum <= 0:
		return "buildingLifecycle simulation health thresholds are not strictly ordered"
	if has_damage_rule:
		var damage_rule: Dictionary = rule_value
		for field in ["damagedThreshold", "reallyDamagedThreshold"]:
			if not _is_exact_integer(damage_rule.get(field)):
				return "buildingLifecycle.simulationFacts.damageStateRule.%s is not an exact integer" % field
		var damaged := int(damage_rule.get("damagedThreshold", -1))
		var really_damaged := int(damage_rule.get("reallyDamagedThreshold", -1))
		if really_damaged <= 0 or damaged <= really_damaged or damaged >= maximum:
			return "buildingLifecycle simulation health thresholds are not strictly ordered"
	if expected_maximum_health > 0 and maximum != expected_maximum_health:
		return "buildingLifecycle maximum health %d does not equal expected %d" % [maximum, expected_maximum_health]
	var facts_error := ""
	if MEN_LIFECYCLE_OBJECT_IDS.has(object_id):
		facts_error = _validate_v1_men_simulation_facts(facts)
	elif composed:
		facts_error = _validate_v1_composed_simulation_facts(facts)
	else:
		facts_error = _validate_v1_simulation_facts(facts, maximum)
	if facts_error != "":
		return facts_error
	var construction_omitted := false
	if composed:
		var construction_value: Variant = facts.get("construction")
		construction_omitted = (
			typeof(construction_value) == TYPE_DICTIONARY
			and (construction_value as Dictionary).has("status")
		)

	if typeof(lifecycle.get("phases")) != TYPE_ARRAY:
		return "buildingLifecycle.phases is not a list"
	var phases: Array = lifecycle["phases"]
	var expected_phase_list: Array[String] = []
	for candidate_phase in V1_PHASES:
		if composed and construction_omitted and candidate_phase == "construction":
			continue
		if composed and not has_damage_rule and candidate_phase in ["damaged", "really-damaged"]:
			continue
		expected_phase_list.append(candidate_phase)
	if phases.size() != expected_phase_list.size():
		return "buildingLifecycle.phases does not contain the declared version-1 phase set"
	for index in range(phases.size()):
		if typeof(phases[index]) != TYPE_DICTIONARY:
			return "buildingLifecycle.phases[%d] is not an object" % index
		var phase_row: Dictionary = phases[index]
		var expected_phase := expected_phase_list[index]
		if String(phase_row.get("phase", "")) != expected_phase:
			return "buildingLifecycle phase order or identity changed at %s" % expected_phase
		var expected_next: Variant = null
		if expected_phase not in ["post-rubble", "post-collapse"]:
			expected_next = expected_phase_list[index + 1]
		var phase_error := _validate_v1_phase_row(phase_row, expected_phase, expected_next)
		if phase_error != "":
			return phase_error

	if typeof(lifecycle.get("bib")) != TYPE_DICTIONARY:
		# A composed structure without an authored W3DFloorDraw has a proven
		# null bib; every other evidence lane requires the bib closure.
		if not composed or lifecycle.get("bib") != null:
			return "buildingLifecycle.bib is not an object"
	else:
		var bib: Dictionary = lifecycle["bib"]
		if typeof(bib.get("duringConstruction")) != TYPE_BOOL:
			return "buildingLifecycle.bib.duringConstruction is not a bool"
		if typeof(bib.get("sourceConditions")) != TYPE_ARRAY:
			return "buildingLifecycle.bib.sourceConditions is not a list"
		if typeof(bib.get("visual")) != TYPE_DICTIONARY:
			return "buildingLifecycle.bib.visual is not an object"
		var bib_visual: Dictionary = bib["visual"]
		if String(bib_visual.get("mode", "")) != "glb":
			return "buildingLifecycle.bib.visual is not a GLB phase"
		var bib_error := _validate_relative_glb_path(bib_visual.get("glb"), "buildingLifecycle.bib.visual.glb")
		if bib_error != "":
			return bib_error

	var intact_path := _intact_visual_path(lifecycle)
	var normalized_model := declared_model.replace("\\", "/")
	if normalized_model != "" and normalized_model != intact_path:
		return "presentation.model is not the lifecycle intact path"
	var routes_error := _validate_v1_routes(lifecycle, object_id)
	if routes_error != "":
		return routes_error
	var rebuild_error := _validate_v1_rebuild_hole(lifecycle.get("rebuildHole"))
	if rebuild_error != "":
		return rebuild_error
	return ""


static func _validate_v1_simulation_facts(facts: Dictionary, maximum: int) -> String:
	for field in ["initialHealth", "construction", "collapse", "postRubble", "captureInitialState"]:
		if typeof(facts.get(field)) != TYPE_DICTIONARY:
			return "buildingLifecycle.simulationFacts.%s is not an object" % field
	var initial_health: Dictionary = facts["initialHealth"]
	if (
		not _is_exact_integer(initial_health.get("derivedHitPoints"))
		or int(initial_health.get("derivedHitPoints", -1)) != maximum
		or not _is_exact_integer(initial_health.get("mapAuthoredPercent"))
		or int(initial_health.get("mapAuthoredPercent", -1)) != 100
		or String(initial_health.get("status", "")) != "proven-full-health-map-placement"
	):
		return "buildingLifecycle initial health is not proven full-health placement data"
	var construction: Dictionary = facts["construction"]
	if (
		not _finite_positive_variant(construction.get("buildTimeSeconds"))
		or String(construction.get("animationMode", "")) != "MANUAL"
		or String(construction.get("animation", "")).strip_edges() == ""
	):
		return "buildingLifecycle construction facts are incomplete"
	var capture: Dictionary = facts["captureInitialState"]
	if (
		not _is_exact_integer(capture.get("initialHealthPercent"))
		or int(capture.get("initialHealthPercent", -1)) != 100
		or String(capture.get("initialPhase", "")) != "intact"
		or not _is_exact_integer(capture.get("mapPlacementCount"))
		or int(capture.get("mapPlacementCount", 0)) <= 0
		or String(capture.get("owner", "")).strip_edges() == ""
	):
		return "buildingLifecycle capture initial state is incomplete"
	var collapse: Dictionary = facts["collapse"]
	if (
		not _is_exact_integer(collapse.get("animationFrameCount"))
		or int(collapse.get("animationFrameCount", 0)) <= 0
		or not _is_exact_integer(collapse.get("animationFrameRate"))
		or int(collapse.get("animationFrameRate", 0)) <= 0
		or String(collapse.get("animationVirtualPath", "")).get_extension().to_lower() != "w3d"
		or String(collapse.get("exactTotalTimingStatus", "")).strip_edges() == ""
	):
		return "buildingLifecycle collapse evidence is incomplete"
	var post_rubble: Dictionary = facts["postRubble"]
	if String(post_rubble.get("terminalDuration", "")).strip_edges() == "":
		return "buildingLifecycle post-rubble terminal duration is missing"
	return ""


static func _validate_v1_men_simulation_facts(facts: Dictionary) -> String:
	if typeof(facts.get("collapse")) != TYPE_DICTIONARY:
		return "Men buildingLifecycle.simulationFacts.collapse is not an object"
	var collapse: Dictionary = facts["collapse"]
	if (
		String(collapse.get("module", "")) != "StructureCollapseUpdate"
		or typeof(collapse.get("destroyObjectWhenDone")) != TYPE_BOOL
		or not bool(collapse.get("destroyObjectWhenDone", false))
		or String(collapse.get("sourceObject", "")).strip_edges() == ""
		or String(collapse.get("exactTotalTimingStatus", "")) != "blocked-on-bfme2-runtime-oracle"
	):
		return "Men buildingLifecycle collapse identity is incomplete"
	for field in [
		"minCollapseDelayMilliseconds",
		"maxCollapseDelayMilliseconds",
		"minBurstDelayMilliseconds",
		"maxBurstDelayMilliseconds",
		"bigBurstFrequency",
		"collapseHeight",
	]:
		var value: Variant = collapse.get(field)
		if not _is_exact_integer(value) or int(value) < 0:
			return "Men buildingLifecycle collapse %s is not a nonnegative exact integer" % field
	for field in ["collapseDamping", "maxShudder"]:
		var value: Variant = collapse.get(field)
		if typeof(value) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(value)) or float(value) < 0.0:
			return "Men buildingLifecycle collapse %s is not finite and nonnegative" % field
	var fx_lists: Variant = collapse.get("fxLists")
	if typeof(fx_lists) != TYPE_DICTIONARY or (fx_lists as Dictionary).is_empty():
		return "Men buildingLifecycle collapse FX lists are absent"
	for fx_value in (fx_lists as Dictionary).values():
		if not _safe_runtime_identifier(String(fx_value)):
			return "Men buildingLifecycle collapse FX identifier is unsafe"
	return ""


static func _validate_v1_composed_simulation_facts(facts: Dictionary) -> String:
	## Composed structure runtimes are porter-built, never map-placed: the
	## importer proves construction scrub, collapse-module, and terminal facts
	## from the source Object instead of map placement evidence.
	for field in ["construction", "collapse", "postRubble"]:
		if typeof(facts.get(field)) != TYPE_DICTIONARY:
			return "buildingLifecycle.simulationFacts.%s is not an object" % field
	var construction: Dictionary = facts["construction"]
	if construction.has("status"):
		# Never-constructed structures record why the construction phase is
		# absent instead of inventing a build scrub.
		if String(construction.get("status", "")) not in [
			"never-constructed-engine-spawned-composite",
			"never-constructed-authored-neutral-map",
			"never-constructed-wall-template",
			"no-authored-construction-states",
		]:
			return "buildingLifecycle construction omission is not explicit"
	elif (
		not _finite_positive_variant(construction.get("buildTimeSeconds"))
		or String(construction.get("animationMode", "")) != "MANUAL"
		or String(construction.get("animation", "")).strip_edges() == ""
	):
		return "buildingLifecycle construction facts are incomplete"
	var collapse: Dictionary = facts["collapse"]
	if collapse.get("module") == null:
		if String(collapse.get("status", "")) != "no-authored-structure-collapse-update":
			return "buildingLifecycle collapse absence is not explicit"
	else:
		if (
			String(collapse.get("module", "")) != "StructureCollapseUpdate"
			or String(collapse.get("sourceObject", "")).strip_edges() == ""
			or String(collapse.get("exactTotalTimingStatus", "")).strip_edges() == ""
		):
			return "buildingLifecycle collapse identity is incomplete"
		# Only authored fields are present: an unauthored field is honest
		# absence (engine default), never an invented number.
		if collapse.has("destroyObjectWhenDone") and typeof(collapse.get("destroyObjectWhenDone")) != TYPE_BOOL:
			return "buildingLifecycle collapse destroyObjectWhenDone is not a bool"
		for field in [
			"minCollapseDelayMilliseconds",
			"maxCollapseDelayMilliseconds",
			"minBurstDelayMilliseconds",
			"maxBurstDelayMilliseconds",
			"bigBurstFrequency",
			"collapseHeight",
		]:
			if not collapse.has(field):
				continue
			var value: Variant = collapse.get(field)
			if not _is_exact_integer(value) or int(value) < 0:
				return "buildingLifecycle collapse %s is not a nonnegative exact integer" % field
		for field in ["collapseDamping", "maxShudder"]:
			if not collapse.has(field):
				continue
			var value: Variant = collapse.get(field)
			if typeof(value) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(value)) or float(value) < 0.0:
				return "buildingLifecycle collapse %s is not finite and nonnegative" % field
		if typeof(collapse.get("fxLists")) != TYPE_DICTIONARY:
			return "buildingLifecycle collapse FX lists are absent"
		for fx_value in (collapse.get("fxLists") as Dictionary).values():
			if not _safe_runtime_identifier(String(fx_value)):
				return "buildingLifecycle collapse FX identifier is unsafe"
	var post_rubble: Dictionary = facts["postRubble"]
	if String(post_rubble.get("terminalDuration", "")).strip_edges() == "":
		return "buildingLifecycle post-rubble terminal duration is missing"
	return ""


static func _validate_v1_rebuild_hole(value: Variant) -> String:
	if value == null:
		return ""
	if typeof(value) != TYPE_DICTIONARY:
		return "buildingLifecycle.rebuildHole is neither null nor an object"
	var rebuild: Dictionary = value
	if (
		String(rebuild.get("sourceTypeName", "")) not in ["CaveTrollLairHole", "WargLairHole"]
		or String(rebuild.get("sourceDefinitionVirtualPath", "")) != "data/ini/object/neutral/holes.ini"
		or String(rebuild.get("spawnTiming", "")) != "death-behavior-immediate"
		or typeof(rebuild.get("originalObjectDestroyWhenCollapseDone")) != TYPE_BOOL
		or not bool(rebuild.get("originalObjectDestroyWhenCollapseDone", false))
		or rebuild.get("exactOriginalRemovalFrame") != null
		or String(rebuild.get("exactOriginalRemovalFrameStatus", "")) != "blocked-on-bfme2-runtime-oracle"
		or not _is_exact_integer(rebuild.get("maximumHealth"))
		or int(rebuild.get("maximumHealth", -1)) != 500
		or not _finite_zero_variant(rebuild.get("healthRegenPercentPerSecond"))
		or String(rebuild.get("terminalDuration", "")) != "unbounded-until-rebuild-or-explicit-destruction"
	):
		return "buildingLifecycle.rebuildHole source state is incomplete or altered"
	if typeof(rebuild.get("states")) != TYPE_ARRAY or (rebuild.get("states") as Array).size() != 1:
		return "buildingLifecycle.rebuildHole does not contain one exact state"
	var state_value: Variant = (rebuild.get("states") as Array)[0]
	if typeof(state_value) != TYPE_DICTIONARY:
		return "buildingLifecycle.rebuildHole state is not an object"
	var state: Dictionary = state_value
	if (
		String(state.get("phase", "")) != "visible-rubble"
		or String(state.get("transitionAuthority", "")) != "deterministic-simulation"
		or not _finite_positive_variant(state.get("fadeInSeconds"))
		or not is_equal_approx(float(state.get("fadeInSeconds", -1.0)), 2.0)
		or typeof(state.get("sourceConditionSets")) != TYPE_ARRAY
		or typeof(state.get("visual")) != TYPE_DICTIONARY
	):
		return "buildingLifecycle.rebuildHole visible-rubble state is incomplete"
	var visual: Dictionary = state["visual"]
	if String(visual.get("mode", "")) != "glb":
		return "buildingLifecycle.rebuildHole visible-rubble state is not a GLB"
	return _validate_relative_glb_path(visual.get("glb"), "buildingLifecycle.rebuildHole.states[0].visual.glb")


static func _validate_v1_phase_row(row: Dictionary, phase: String, expected_next: Variant = null) -> String:
	if typeof(row.get("sourceConditionSets")) != TYPE_ARRAY:
		return "buildingLifecycle phase %s sourceConditionSets is not a list" % phase
	for condition_set_value in row.get("sourceConditionSets") as Array:
		if typeof(condition_set_value) != TYPE_ARRAY:
			return "buildingLifecycle phase %s contains a malformed source condition set" % phase
		for condition_value in condition_set_value as Array:
			if typeof(condition_value) != TYPE_STRING or String(condition_value).strip_edges() == "":
				return "buildingLifecycle phase %s contains an invalid source condition" % phase
	if typeof(row.get("visual")) != TYPE_DICTIONARY:
		return "buildingLifecycle phase %s visual is not an object" % phase
	var visual: Dictionary = row["visual"]
	var visual_mode := String(visual.get("mode", ""))
	if visual_mode == "glb":
		var path_error := _validate_relative_glb_path(visual.get("glb"), "buildingLifecycle phase %s visual.glb" % phase)
		if path_error != "":
			return path_error
	elif visual_mode == "no-render":
		var identifier := String(visual.get("sourceIdentifier", ""))
		if identifier not in ["None", "NONE", "DestroyObjectWhenDone"] or String(visual.get("glb", "")) != "":
			return "buildingLifecycle phase %s no-render source is not explicit" % phase
	else:
		return "buildingLifecycle phase %s has an unsupported visual mode" % phase

	if typeof(row.get("animation")) != TYPE_DICTIONARY:
		return "buildingLifecycle phase %s animation is not an object" % phase
	var animation: Dictionary = row["animation"]
	var mode := String(animation.get("mode", ""))
	var clip: Variant = animation.get("clip")
	if mode not in ["none", "manual-progress", "loop", "loop-random", "once"]:
		return "buildingLifecycle phase %s has an unsupported animation mode" % phase
	if mode == "none":
		if clip != null:
			return "buildingLifecycle phase %s none animation declares a clip" % phase
	elif typeof(clip) != TYPE_STRING or String(clip).strip_edges() == "" or visual_mode != "glb":
		return "buildingLifecycle phase %s active animation lacks a rendered clip" % phase
	if phase == "construction" and mode != "manual-progress":
		return "buildingLifecycle construction animation is not manual-progress"
	if String(row.get("transitionAuthority", "")) != "deterministic-simulation":
		return "buildingLifecycle phase %s transition authority is not deterministic simulation" % phase
	var actual_next: Variant = row.get("nextPhase")
	if expected_next == null:
		if actual_next != null:
			return "buildingLifecycle phase %s must be terminal" % phase
	elif typeof(actual_next) != TYPE_STRING or String(actual_next) != String(expected_next):
		return "buildingLifecycle phase %s has the wrong explicit next phase" % phase
	return ""


static func _validate_v1_routes(lifecycle: Dictionary, object_id: String = "") -> String:
	if typeof(lifecycle.get("audioEvents")) != TYPE_DICTIONARY:
		return "buildingLifecycle.audioEvents is not an object"
	var audio: Dictionary = lifecycle["audioEvents"]
	var men_contract := MEN_LIFECYCLE_OBJECT_IDS.has(object_id)
	var composed := String(lifecycle.get("evidenceProfile", "")) == COMPOSED_EVIDENCE_PROFILE
	var audio_roles := ["collapse", "construction"] if men_contract or composed else ["ambient", "collapse", "constructionSelect", "damaged", "reallyDamaged", "select"]
	if audio.size() != audio_roles.size():
		return "buildingLifecycle.audioEvents does not contain the exact route set"
	for role in audio_roles:
		if not audio.has(role):
			return "buildingLifecycle.audioEvents is missing %s" % role
		var event_value: Variant = audio[role]
		if event_value != null and (typeof(event_value) != TYPE_STRING or not _safe_runtime_identifier(String(event_value))):
			return "buildingLifecycle.audioEvents.%s is invalid" % role
	if typeof(lifecycle.get("effects")) != TYPE_DICTIONARY:
		return "buildingLifecycle.effects is not an object"
	var effects: Dictionary = lifecycle["effects"]
	for field in ["enteringStateFx", "collapseUpdateFx"]:
		if typeof(effects.get(field)) != TYPE_DICTIONARY:
			return "buildingLifecycle.effects.%s is not an object" % field
		for key_value in (effects[field] as Dictionary).keys():
			var value: Variant = (effects[field] as Dictionary)[key_value]
			if typeof(key_value) != TYPE_STRING or typeof(value) != TYPE_STRING or not _safe_runtime_identifier(String(value)):
				return "buildingLifecycle.effects.%s contains an invalid route" % field
	var entering: Dictionary = effects["enteringStateFx"]
	var entering_keys: Array = entering.keys()
	entering_keys.sort()
	if composed:
		# Composed evidence declares only source-proven EnteringStateFX routes;
		# an unauthored route is honest absence, never an invented default.
		for key_value in entering_keys:
			if String(key_value) not in ["collapsing", "damaged", "really-damaged"]:
				return "buildingLifecycle.effects.enteringStateFx has an unsupported phase route"
	elif entering_keys != ["collapsing", "damaged", "really-damaged"]:
		return "buildingLifecycle.effects.enteringStateFx does not contain the exact phase routes"
	var collapse_update: Dictionary = effects["collapseUpdateFx"]
	var collapse_keys: Array = collapse_update.keys()
	collapse_keys.sort()
	var expected_men_collapse_keys := ["initial"] if object_id == "bfme2.object.men-fortress" else ["almost-final", "initial"]
	if composed:
		for key_value in collapse_keys:
			if String(key_value) not in ["almost-final", "initial"]:
				return "buildingLifecycle.effects.collapseUpdateFx is incomplete"
	elif (
		men_contract and collapse_keys != expected_men_collapse_keys
		or not men_contract and not collapse_keys.is_empty() and collapse_keys != ["almost-final", "initial"]
	):
		return "buildingLifecycle.effects.collapseUpdateFx is incomplete"
	if typeof(effects.get("particleAttachments")) != TYPE_ARRAY:
		return "buildingLifecycle.effects.particleAttachments is not a list"
	for attachment_value in effects.get("particleAttachments") as Array:
		if typeof(attachment_value) != TYPE_DICTIONARY:
			return "buildingLifecycle particle attachment is not an object"
		var attachment: Dictionary = attachment_value
		# sourceConditions is required as an array. Empty means always-on ambient
		# FX (forge chimney smoke / embers) — exact retail authoring, not a gap.
		if typeof(attachment.get("sourceConditions")) != TYPE_ARRAY:
			return "buildingLifecycle particle attachment sourceConditions is not a list"
		for condition_value in attachment.get("sourceConditions") as Array:
			if typeof(condition_value) != TYPE_STRING or String(condition_value).strip_edges() == "":
				return "buildingLifecycle particle attachment has an invalid source condition"
		if typeof(attachment.get("bone")) != TYPE_STRING or String(attachment.get("bone", "")).strip_edges() == "":
			return "buildingLifecycle particle attachment has an invalid bone"
		if typeof(attachment.get("particleSystemId")) != TYPE_STRING or not _safe_runtime_identifier(String(attachment.get("particleSystemId", ""))):
			return "buildingLifecycle particle attachment has an invalid system ID"
		if typeof(attachment.get("options")) != TYPE_ARRAY:
			return "buildingLifecycle particle attachment options are not a list"
		for option_value in attachment.get("options") as Array:
			if typeof(option_value) != TYPE_STRING or String(option_value).strip_edges() == "":
				return "buildingLifecycle particle attachment has an invalid option"
	var translation_status := String(effects.get("definitionTranslationStatus", ""))
	if translation_status == "":
		return "buildingLifecycle effect-definition selection status is missing"
	return ""


static func preflight_lifecycle_assets(
	lifecycle: Dictionary,
	structure_kind_value: String,
	pack_root: String
) -> String:
	var entries := _required_path_entries(lifecycle, structure_kind_value)
	var asset_factory = load("res://src/view/asset_factory.gd")
	for role_value in entries.keys():
		var role := String(role_value)
		var relative := String(entries[role]).replace("\\", "/")
		var resolved := ContentDB.resolve_asset(relative, pack_root)
		if resolved == "":
			return "%s does not resolve in the selected pack" % role
		if not ModLoader.path_is_within(pack_root, resolved):
			return "%s resolves outside the selected pack" % role
		var asset_error: String = asset_factory.preflight_explicit_model_path(resolved, pack_root)
		if asset_error != "":
			return "%s failed GLB preflight: %s" % [role, asset_error]
	return ""


static func _validate_clip_contract(value: Variant) -> String:
	if typeof(value) != TYPE_DICTIONARY:
		return "buildingLifecycle.clips is not an object"
	var clips: Dictionary = value
	for phase_value in clips.keys():
		var phase := String(phase_value)
		if not BODY_PHASES.has(phase):
			return "buildingLifecycle.clips has unsupported phase %s" % phase
		if typeof(clips[phase]) != TYPE_DICTIONARY:
			return "buildingLifecycle.clips.%s is not an object" % phase
		var row: Dictionary = clips[phase]
		if typeof(row.get("names")) != TYPE_ARRAY:
			return "buildingLifecycle.clips.%s.names is not a list" % phase
		var names: Array = row.get("names") as Array
		for name_value in row.get("names") as Array:
			if typeof(name_value) != TYPE_STRING or String(name_value).strip_edges() == "":
				return "buildingLifecycle.clips.%s.names contains an invalid name" % phase
		var mode := String(row.get("mode", ""))
		if mode not in ["none", "manual-progress", "loop", "loop-random", "once"]:
			return "buildingLifecycle.clips.%s.mode is unsupported" % phase
		if mode == "none" and not names.is_empty():
			return "buildingLifecycle.clips.%s none mode declares names" % phase
		if mode != "none" and names.is_empty():
			return "buildingLifecycle.clips.%s active mode has no names" % phase
		if phase == "construction" and mode != "manual-progress":
			return "buildingLifecycle construction clip is not manual-progress"
	return ""


static func _validate_relative_glb_path(value: Variant, label: String) -> String:
	if typeof(value) != TYPE_STRING:
		return "%s is not a path string" % label
	var path := String(value).replace("\\", "/")
	if path == "" or not path.begins_with("assets/") or not ModLoader.is_safe_relative_path(path):
		return "%s is not a safe pack-relative asset path" % label
	if path.get_extension().to_lower() != "glb":
		return "%s is not a GLB path" % label
	return ""


static func _is_exact_integer(value: Variant) -> bool:
	if typeof(value) == TYPE_INT:
		return true
	return typeof(value) == TYPE_FLOAT and is_finite(float(value)) and float(value) == floorf(float(value))


static func _finite_positive_variant(value: Variant) -> bool:
	return typeof(value) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(value)) and float(value) > 0.0


static func _finite_zero_variant(value: Variant) -> bool:
	return typeof(value) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(value)) and is_zero_approx(float(value))


static func maximum_health_for_lifecycle(lifecycle: Dictionary) -> int:
	if int(lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
		var facts: Dictionary = lifecycle.get("simulationFacts", {}) as Dictionary
		var v1_value: Variant = facts.get("maximumHealth")
		return int(v1_value) if _is_exact_integer(v1_value) else 0
	var v0_value: Variant = lifecycle.get("maxHealth")
	return int(v0_value) if _is_exact_integer(v0_value) else 0


static func _intact_visual_path(lifecycle: Dictionary) -> String:
	if int(lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
		var row := _v1_phase_row(lifecycle, "intact")
		var visual: Dictionary = row.get("visual", {}) as Dictionary
		return String(visual.get("glb", "")).replace("\\", "/")
	var paths: Dictionary = lifecycle.get("paths", {}) as Dictionary
	return String(paths.get("intact", "")).replace("\\", "/")


static func _v1_phase_row(lifecycle: Dictionary, phase: String) -> Dictionary:
	var phases_value: Variant = lifecycle.get("phases")
	if typeof(phases_value) != TYPE_ARRAY:
		return {}
	for value in phases_value as Array:
		if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("phase", "")) == phase:
			return value as Dictionary
	return {}


static func _safe_content_object_id(value: String) -> bool:
	if value.length() < 3 or value.length() > 128 or not value.begins_with("bfme2.object."):
		return false
	for index in range(value.length()):
		var codepoint := value.unicode_at(index)
		if not (codepoint >= 97 and codepoint <= 122) and not (codepoint >= 48 and codepoint <= 57) and codepoint not in [45, 46]:
			return false
	return not value.contains("..") and not value.ends_with(".") and not value.ends_with("-")


static func _safe_runtime_identifier(value: String) -> bool:
	if value.is_empty() or value.length() > 128:
		return false
	for index in range(value.length()):
		var codepoint := value.unicode_at(index)
		var alpha_numeric := (codepoint >= 65 and codepoint <= 90) or (codepoint >= 97 and codepoint <= 122) or (codepoint >= 48 and codepoint <= 57)
		if not alpha_numeric and codepoint not in [45, 46, 95]:
			return false
	return true


static func _required_path_entries(lifecycle: Dictionary, structure_kind_value: String) -> Dictionary:
	var entries: Dictionary = {}
	if int(lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
		for phase_value in lifecycle.get("phases", []) as Array:
			if typeof(phase_value) != TYPE_DICTIONARY:
				continue
			var row: Dictionary = phase_value
			var visual: Dictionary = row.get("visual", {}) as Dictionary
			if String(visual.get("mode", "")) == "glb":
				entries["phases.%s.visual.glb" % String(row.get("phase", ""))] = String(visual.get("glb", ""))
		var bib_value: Variant = lifecycle.get("bib", {})
		if typeof(bib_value) == TYPE_DICTIONARY:
			var bib_visual: Dictionary = (bib_value as Dictionary).get("visual", {}) as Dictionary
			entries["bib.visual.glb"] = String(bib_visual.get("glb", ""))
		if structure_kind_value == "fortress":
			var components: Dictionary = lifecycle.get("components", {}) as Dictionary
			var door: Dictionary = components.get("door", {}) as Dictionary
			for key in DOOR_PATH_KEYS:
				var door_row: Dictionary = door.get(key, {}) as Dictionary
				# A fortress lifecycle without a declared door component (the
				# elven fortress keeps its gates on wall structures) has no
				# door assets to preflight; declared door paths stay required.
				if String(door_row.get("path", "")) != "":
					entries["components.door.%s.path" % key] = String(door_row.get("path", ""))
		if typeof(lifecycle.get("rebuildHole")) == TYPE_DICTIONARY:
			var rebuild: Dictionary = lifecycle["rebuildHole"]
			var states: Array = rebuild.get("states", []) as Array
			if states.size() == 1 and typeof(states[0]) == TYPE_DICTIONARY:
				var state: Dictionary = states[0]
				var rebuild_visual: Dictionary = state.get("visual", {}) as Dictionary
				entries["rebuildHole.states[0].visual.glb"] = String(rebuild_visual.get("glb", ""))
		return entries
	var paths: Dictionary = lifecycle.get("paths", {}) as Dictionary
	for key in BODY_PATH_KEYS:
		entries["paths.%s" % key] = String(paths.get(key, ""))
	if structure_kind_value == "fortress":
		var components: Dictionary = lifecycle.get("components", {}) as Dictionary
		var door: Dictionary = components.get("door", {}) as Dictionary
		for key in DOOR_PATH_KEYS:
			var door_row: Dictionary = door.get(key, {}) as Dictionary
			if String(door_row.get("path", "")) != "":
				entries["components.door.%s.path" % key] = String(door_row.get("path", ""))
	return entries


func _prepare_identity(entity: Dictionary, bundle_object_id: String) -> void:
	entity_id = int(entity.get("id", 0))
	team = int(entity.get("team", 0))
	structure_kind = String(entity.get("structure_kind", entity.get("kind", "fortress")))
	_bundle_object_id = bundle_object_id
	name = "RetailStructure_%d_%s" % [entity_id, structure_kind]


func _seed_selection_radius() -> void:
	## Conservative starting footprint before the body's bounds are known. It is
	## expressed in retail SOURCE units and projected through the map transform
	## scale, so it shrinks with the battlefield instead of staying a flat
	## world-unit circle that swallowed most of the screen.
	var source_radius := (
		SelectionPick.DEFAULT_FORTRESS_SOURCE_RADIUS
		if structure_kind == "fortress"
		else SelectionPick.DEFAULT_STRUCTURE_SOURCE_RADIUS
	)
	var compiled := _compiled_geometry_source_radius()
	if compiled > 0.0:
		source_radius = compiled
		selection_radius_source = "compiled-retail-geometry"
	else:
		selection_radius_source = "source-unit-default"
	var projected := SelectionPick.world_radius_from_source(source_radius, _source_unit_scale)
	if projected > 0.0:
		pick_radius = projected
	else:
		# No map transform scale (fixture seams): keep a bounded circle rather
		# than the legacy 4.5/7.0 world-unit constants.
		pick_radius = 1.6 if structure_kind == "fortress" else 1.1
		selection_radius_source = "unscaled-default"


func _compiled_geometry_source_radius() -> float:
	## Retail Geometry projected by the importer into the selected pack's
	## playable-structure document, when that pack carries it. Packs published
	## before the projection landed simply have no row and fall through to the
	## body-bounds measurement below.
	compiled_geometry = {}
	if _bundle_object_id == "" or not ContentDB.has_method("get_playable_structure_runtime"):
		return 0.0
	var document: Variant = ContentDB.get_playable_structure_runtime(_bundle_object_id)
	if typeof(document) != TYPE_DICTIONARY:
		return 0.0
	var registration: Dictionary = (document as Dictionary).get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var geometry: Variant = gameplay.get("geometry", {})
	if typeof(geometry) != TYPE_DICTIONARY:
		return 0.0
	compiled_geometry = (geometry as Dictionary).duplicate(true)
	return SelectionPick.source_footprint_radius(compiled_geometry)


func _apply_visual_bounds_selection_radius(bounds: AABB, uniform_scale: float) -> void:
	## The intact body's measured top owns marker placement even when compiled
	## retail Geometry owns picking. Constants made a billboard jump far above
	## some rooflines and bury itself in others.
	_visual_top_y = (bounds.position.y + bounds.size.y) * uniform_scale + shared_vertical_offset
	var scale := uniform_scale if is_finite(uniform_scale) and uniform_scale > 0.0 else 0.0
	if scale > 0.0:
		visual_pick_center = Vector2(
			(bounds.position.x + bounds.size.x * 0.5) * scale,
			(bounds.position.z + bounds.size.z * 0.5) * scale
		)
	## Retail Geometry is a floor, never a ceiling against the body the player
	## can see. A fortress keep whose intact AABB outruns the 64-unit box
	## (fortress.ini:1254-1306) must pick — and draw its ring — at the visual
	## edge. Compiled-only used to return early here and leave the walls
	## unclickable.
	var measured := SelectionPick.world_radius_from_visual_bounds(bounds, uniform_scale)
	var combined := SelectionPick.effective_world_pick_radius(pick_radius, measured)
	if combined > 0.0:
		if measured > pick_radius and selection_radius_source == "compiled-retail-geometry":
			selection_radius_source = "compiled-and-intact-visual-floor"
		elif selection_radius_source != "compiled-retail-geometry" and measured > 0.0:
			selection_radius_source = "intact-body-bounds"
		pick_radius = combined
	_sync_health_bar_geometry()
	_sync_selection_ring_geometry()


func structure_pick_candidates(entity_id: int) -> Array:
	## Piece-accurate hit tests when AdditionalGeometry exists, plus a visual
	## AABB floor circle so the keep's walls stay clickable. Empty when this
	## node has neither compiled pieces nor a measured pick radius; callers
	## keep the origin-circle fallback for that seam.
	if entity_id == 0:
		return []
	var origin := Vector2(global_position.x, global_position.z)
	var candidates: Array = SelectionPick.geometry_piece_candidates(
		entity_id, origin, compiled_geometry, _source_unit_scale
	)
	if pick_radius > 0.0:
		candidates.append({
			"id": entity_id,
			"position": origin + visual_pick_center,
			"radius": pick_radius,
		})
	return candidates


func _build_visual_root() -> void:
	_visual_root = Node3D.new()
	_visual_root.name = "StructureVisual"
	add_child(_visual_root)
	_model_host = Node3D.new()
	_model_host.name = "SharedLifecycleTransform"
	_visual_root.add_child(_model_host)


var _shadow_decal: Decal


func _ensure_shadow_decal() -> void:
	# Retail contact shadow: SAGE structure shadows use the decal technique at
	# shadow color ARGB(64,0,0,0); the footprint blob is the approved
	# equivalence, sized once from the intact body's merged mesh bounds.
	if _shadow_decal != null or contract_error != "" or _visual_root == null or _active_body == null or not is_inside_tree():
		return
	var merged := AABB()
	var has_mesh := false
	var pending: Array[Node] = [_active_body]
	var root_inverse := global_transform.affine_inverse()
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		var mesh := node as MeshInstance3D
		if mesh != null and mesh.mesh != null:
			var local_box: AABB = (root_inverse * mesh.global_transform) * mesh.get_aabb()
			merged = local_box if not has_mesh else merged.merge(local_box)
			has_mesh = true
		pending.append_array(node.get_children())
	if not has_mesh or merged.size.x <= 0.0 or merged.size.z <= 0.0:
		return
	_shadow_decal = ShadowDecalScript.create(
		Vector2(merged.size.x * 1.05, merged.size.z * 1.05),
		maxf(merged.size.y, 1.0)
	)
	_shadow_decal.position = Vector3(merged.get_center().x, merged.position.y + 0.05, merged.get_center().z)
	_visual_root.add_child(_shadow_decal)


func _configure_selected_pack_contract(bundle_object_id: String) -> void:
	var definition: Dictionary = {}
	if bundle_object_id != "" and ContentDB.bundle_objects.has(bundle_object_id):
		definition = ContentDB.get_bundle_object(bundle_object_id)
	else:
		# objects.json does not project fortress expansion structures yet; bind
		# the same content from the playable-structure runtime document whose
		# runtime id matches (fail closed when neither registry carries it).
		for source_id_value in ContentDB.get_playable_structure_runtimes().keys():
			var source_id := String(source_id_value)
			if UnitAdapterScript._runtime_id(source_id) != bundle_object_id:
				continue
			var document: Dictionary = ContentDB.get_playable_structure_runtime(source_id)
			definition = {
				"_pack_root": String(document.get("_pack_root", "")),
				"kind": "structure",
				"presentation": (document.get("registration", {}) as Dictionary).get("presentation", {}),
			}
			break
	if definition.is_empty():
		var scenario_document := _scenario_structure_document_for_bundle_id(bundle_object_id)
		if not scenario_document.is_empty():
			var scenario_presentation: Dictionary = ((scenario_document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).duplicate(true)
			var scenario_lifecycle: Dictionary = scenario_presentation.get("buildingLifecycle", {}) as Dictionary
			var scenario_facts: Dictionary = scenario_lifecycle.get("simulationFacts", {}) as Dictionary
			scenario_descriptor_maximum_health = scenario_facts.get("maximumHealth")
			if scenario_authoritative_maximum_health > 0:
				scenario_facts["maximumHealth"] = scenario_authoritative_maximum_health
				scenario_lifecycle["simulationFacts"] = scenario_facts
				scenario_presentation["buildingLifecycle"] = scenario_lifecycle
				set_meta("scenario_descriptor_maximum_health", scenario_descriptor_maximum_health)
				set_meta("scenario_authoritative_maximum_health", scenario_authoritative_maximum_health)
			definition = {
				"_pack_root": String(scenario_document.get("_pack_root", "")),
				"kind": "structure",
				"presentation": scenario_presentation,
			}
	if definition.is_empty():
		_set_contract_error("structure bundle object is not registered")
		return
	_pack_root = String(definition.get("_pack_root", ""))
	if _pack_root == "":
		_set_contract_error("structure bundle object has no pack root")
		return
	if String(definition.get("kind", "")) != "structure":
		_set_contract_error("structure bundle object kind is not structure")
		return
	if typeof(definition.get("presentation")) != TYPE_DICTIONARY:
		_set_contract_error("structure presentation is not an object")
		return
	var presentation: Dictionary = definition["presentation"]
	if typeof(presentation.get("buildingLifecycle")) != TYPE_DICTIONARY:
		if (
			bundle_object_id == "bfme2.object.gondor-workshop"
			and String(presentation.get("lifecycleStatus", "")) == "deferred-composite-role-and-runtime-binding"
			and typeof(presentation.get("modelStateEvidence")) == TYPE_ARRAY
		):
			_configure_bounded_workshop_evidence(presentation.get("modelStateEvidence", []) as Array)
			return
		_set_contract_error("structure presentation has no buildingLifecycle object")
		return
	_bind_structure_document_facts(bundle_object_id)
	_configure_contract(presentation, presentation["buildingLifecycle"] as Dictionary)


func _bind_structure_document_facts(bundle_object_id: String) -> void:
	## Bind the structure document's authored per-level model variants (the
	## upgradeChain's SubObjectsUpgrade rows) and its authored Body module.
	## Documents without a chain simply keep every sub-object visible — nothing
	## is invented.
	_level_presentations.clear()
	_level_health_additions.clear()
	body_module = ""
	transition_damage_fx_contracts.clear()
	var scenario_document := _scenario_structure_document_for_bundle_id(bundle_object_id)
	if not scenario_document.is_empty():
		var scenario_gameplay: Dictionary = (scenario_document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
		var scenario_health: Variant = (scenario_gameplay.get("health", {}) as Dictionary).get("primary", {})
		if typeof(scenario_health) == TYPE_DICTIONARY:
			body_module = String((scenario_health as Dictionary).get("module", ""))
		bind_transition_damage_fx_contracts(scenario_document)
		return
	for source_id_value in ContentDB.get_playable_structure_runtimes().keys():
		var source_id := String(source_id_value)
		if UnitAdapterScript._runtime_id(source_id) != bundle_object_id:
			continue
		var document: Dictionary = ContentDB.get_playable_structure_runtime(source_id)
		var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
		bind_transition_damage_fx_contracts(document)
		# Retail Body module, verbatim. `ImmortalBody` is the fortress build
		# plot's body: it can never lose health, which is why retail floats no
		# health bar over an empty plot (user bug #9).
		var health_primary: Variant = (gameplay.get("health", {}) as Dictionary).get("primary", {})
		if typeof(health_primary) == TYPE_DICTIONARY:
			body_module = String((health_primary as Dictionary).get("module", ""))
		var chain: Dictionary = gameplay.get("upgradeChain", {}) as Dictionary
		if not chain.is_empty():
			var level_one: Dictionary = chain.get("levelOne", {}) as Dictionary
			_level_presentations[1] = {
				"visible": _string_array(level_one.get("visibleSubObjects", [])),
				"hidden": _string_array(level_one.get("hiddenSubObjects", [])),
			}
			for step_value in Array(chain.get("steps", [])):
				if typeof(step_value) != TYPE_DICTIONARY:
					continue
				var step := step_value as Dictionary
				var step_level := int(step.get("toLevel", 0))
				var presentation: Dictionary = step.get("presentation", {}) as Dictionary
				if not presentation.is_empty():
					_level_presentations[step_level] = {
						"visible": _string_array(presentation.get("visibleSubObjects", [])),
						"hidden": _string_array(presentation.get("hiddenSubObjects", [])),
					}
				# Authored per-level health additions (the sim raises the
				# building's pool by exactly these on purchase): sync_state's
				# health contract must expect them, never reject them.
				var health_add := 0
				for leaf_value in Array(step.get("effects", [])):
					if typeof(leaf_value) != TYPE_DICTIONARY:
						continue
					for modifier_value in Array((leaf_value as Dictionary).get("modifiers", [])):
						if typeof(modifier_value) != TYPE_DICTIONARY:
							continue
						var modifier := modifier_value as Dictionary
						if String(modifier.get("kind", "")) == "HEALTH":
							health_add += roundi(float(modifier.get("value", 0.0)))
				if health_add != 0 and step_level > 0:
					_level_health_additions[step_level] = health_add
		# Presentation-only staged levels (StructureLevel1/2/3-bound rows for
		# buildings with no purchase chain, e.g. economy auto-levelers): the
		## converter emits them separately; a purchase chain always wins.
		var staged: Dictionary = (gameplay.get("structureLevelPresentation", {}) as Dictionary).get("levels", {}) as Dictionary
		for level_key in staged.keys():
			var staged_level := int(level_key)
			if staged_level <= 0 or _level_presentations.has(staged_level):
				continue
			var row: Dictionary = staged[level_key] as Dictionary
			if row.is_empty():
				continue
			_level_presentations[staged_level] = {
				"visible": _string_array(row.get("visibleSubObjects", [])),
				"hidden": _string_array(row.get("hiddenSubObjects", [])),
			}
		return


func _health_addition_for_level(value: int) -> int:
	## Cumulative authored health additions every level at or below this one
	## grants; zero without a compiled chain (the contract then expects base).
	var total := 0
	for level_value in _level_health_additions.keys():
		if int(level_value) <= value:
			total += int(_level_health_additions[level_value])
	return total


func _string_array(value: Variant) -> Array[String]:
	var output: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return output
	for item in value as Array:
		output.append(String(item))
	return output


func set_level(value: int, upgrading: bool = false, progress: float = 0.0) -> void:
	## Authoritative level from the simulation row; the upgrade timer rides
	## along so the in-progress bar reflects the real purchase state.
	level = maxi(1, value)
	upgrade_in_progress = upgrading
	upgrade_progress = clampf(progress, 0.0, 1.0)
	_apply_level_sub_objects()
	if _build_back != null and _build_fill != null:
		var show_upgrade_bar := upgrade_in_progress and construction_ratio >= 1.0 and contract_error == ""
		if show_upgrade_bar:
			_build_back.visible = true
			_build_fill.visible = true
			_build_fill.scale.x = maxf(0.001, upgrade_progress)
			_build_fill.offset.x = (upgrade_progress - 1.0) * HEALTH_BAR_SCREEN_WIDTH_PX * 0.5


func _level_targets_for(value: int) -> Dictionary:
	## The compiled visibility at this level: the closest authored level at or
	## below it (levels are contiguous in the chain; the fallback is the base).
	var best := 0
	for level_value in _level_presentations.keys():
		var candidate := int(level_value)
		if candidate <= value and candidate > best:
			best = candidate
	if best == 0:
		return {"visible": [], "hidden": []}
	return _level_presentations[best]


func _apply_level_sub_objects() -> void:
	if _active_body == null or not is_instance_valid(_active_body):
		return
	var targets := _level_targets_for(level)
	var visible_tokens: Array = targets.get("visible", [])
	var hidden_tokens: Array = targets.get("hidden", [])
	level_visible_sub_objects.assign(visible_tokens)
	level_hidden_sub_objects.assign(hidden_tokens)
	var matched: Dictionary = {}
	_apply_subobject_tokens(_active_body, visible_tokens, true, matched)
	_apply_subobject_tokens(_active_body, hidden_tokens, false, matched)
	level_applied_nodes = matched
	var unmatched: Array[String] = []
	for token in visible_tokens + hidden_tokens:
		var has_match := false
		for node_name in matched.keys():
			if _subobject_token_matches(String(token), String(node_name)):
				has_match = true
				break
		if not has_match:
			unmatched.append(String(token))
	unmatched.sort()
	level_unmatched_tokens = unmatched


func _apply_subobject_tokens(root: Node, tokens: Array, shown: bool, matched: Dictionary) -> void:
	for token_value in tokens:
		var token := String(token_value)
		_set_named_visibility(root, token, shown, matched)


func apply_geometry_upgrade_visibility(state: Dictionary) -> Dictionary:
	## Real structure-model seam for the simulation's authoritative
	## GeometryUpgrade fold. Tokens retain SAGE's exact-or-trailing-* matching;
	## unresolved names are reported, never approximated to a nearby mesh.
	if _active_body == null or not is_instance_valid(_active_body):
		return {"matched_count": 0, "unmatched_tokens": [], "status": "model-body-unavailable"}
	# Restore the state observed immediately before the previous geometry fold.
	# sync_state applies level/phase visibility first, so each new baseline is the
	# exact selected-model state this upgrade layer must overlay, not a guess.
	_restore_geometry_upgrade_baseline()
	var matched: Dictionary = {}
	var shown := _string_array(state.get("show", []))
	var hidden := _string_array(state.get("hide", []))
	for token in shown + hidden:
		_capture_geometry_upgrade_baseline(_active_body, token)
	_apply_subobject_tokens(_active_body, shown, true, matched)
	_apply_subobject_tokens(_active_body, hidden, false, matched)
	var unmatched: Array[String] = []
	for token in shown + hidden:
		var found := false
		for node_name in matched.keys():
			if _subobject_token_matches(token, String(node_name)):
				found = true
				break
		if not found:
			unmatched.append(token)
	unmatched.sort()
	set_meta("geometry_upgrade_matched_nodes", matched.duplicate(true))
	set_meta("geometry_upgrade_unmatched_tokens", unmatched.duplicate())
	return {"matched_count": matched.size(), "matched_nodes": matched, "unmatched_tokens": unmatched, "status": "applied"}


func _restore_geometry_upgrade_baseline() -> void:
	for baseline_value in _geometry_upgrade_baseline.values():
		var baseline := baseline_value as Dictionary
		var node: Variant = baseline.get("node")
		if node is Node3D and is_instance_valid(node):
			(node as Node3D).visible = bool(baseline.get("visible", true))
	_geometry_upgrade_baseline.clear()


func _capture_geometry_upgrade_baseline(root: Node, token: String) -> void:
	if root is Node3D and _subobject_token_matches(token, root.name):
		var identity := root.get_instance_id()
		if not _geometry_upgrade_baseline.has(identity):
			_geometry_upgrade_baseline[identity] = {"node": root, "visible": (root as Node3D).visible}
	for child in root.get_children():
		_capture_geometry_upgrade_baseline(child, token)


func _subobject_token_matches(token: String, node_name: String) -> bool:
	## Exact authored names first; the SAGE prefix wildcard (V1_PIECE*) matches
	## by prefix, never by substring.
	if token.ends_with("*"):
		return node_name.to_lower().begins_with(token.trim_suffix("*").to_lower())
	return node_name.to_lower() == token.to_lower()


func _set_named_visibility(root: Node, token: String, shown: bool, matched: Dictionary) -> void:
	if _subobject_token_matches(token, root.name):
		if root is Node3D:
			(root as Node3D).visible = shown
			matched[root.name] = shown
	for child in root.get_children():
		_set_named_visibility(child, token, shown, matched)


func level_state() -> Dictionary:
	return {
		"level": level,
		"visibleSubObjects": level_visible_sub_objects.duplicate(),
		"hiddenSubObjects": level_hidden_sub_objects.duplicate(),
		"unmatchedTokens": level_unmatched_tokens.duplicate(),
		"appliedNodes": level_applied_nodes.duplicate(),
		"upgradeInProgress": upgrade_in_progress,
		"upgradeProgress": upgrade_progress,
	}


func _configure_bounded_workshop_evidence(evidence: Array) -> void:
	var expected := {
		"construction": "art/w3d/gb/gbworkshop_a.w3d",
		"intact": "art/w3d/gb/gbworkshop.w3d",
		"rubble": "art/w3d/gb/gbworkshop_d3.w3d",
	}
	_bounded_phase_paths.clear()
	for phase in expected:
		var matches: Array[Dictionary] = []
		for value in evidence:
			if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("sourceW3d", "")).to_lower() == String(expected[phase]):
				matches.append(value as Dictionary)
		if matches.size() != 1:
			_set_contract_error("Workshop %s model-state evidence is not exact" % phase)
			return
		var relative := String(matches[0].get("output", "")).replace("\\", "/")
		var resolved := ContentDB.resolve_asset(relative, _pack_root)
		if resolved == "":
			_set_contract_error("Workshop %s model-state GLB is missing" % phase)
			return
		_bounded_phase_paths[phase] = relative
		_resolved_paths[relative] = resolved
	var intact := _load_visual(String(_bounded_phase_paths["intact"]), "body")
	if intact == null:
		_set_contract_error("Workshop intact model-state GLB could not be instantiated")
		return
	var asset_factory = load("res://src/view/asset_factory.gd")
	var intact_aabb: AABB = asset_factory.model_aabb(intact)
	if intact_aabb.size.y <= 0.000001:
		_set_contract_error("Workshop intact model-state GLB has no body AABB")
		return
	shared_uniform_scale = _source_unit_scale if _source_unit_scale > 0.0 else DEFAULT_TARGET_HEIGHT / intact_aabb.size.y
	shared_vertical_offset = -intact_aabb.position.y * shared_uniform_scale
	_model_host.scale = Vector3.ONE * shared_uniform_scale
	_model_host.position.y = shared_vertical_offset
	intact.visible = false
	_target_height = maxf(DEFAULT_TARGET_HEIGHT, intact_aabb.size.y * shared_uniform_scale)
	_apply_visual_bounds_selection_radius(intact_aabb, shared_uniform_scale)
	retail_visual_loaded = true
	presentation_mode = "bounded-workshop-model-state-evidence"
	_update_lifecycle_metadata()


func _sync_bounded_workshop_phase(health: int, construction_progress: float) -> void:
	var phase := "rubble" if health <= 0 else ("construction" if construction_progress < 1.0 else "intact")
	if phase == current_lifecycle_phase:
		return
	var body_path := String(_bounded_phase_paths.get(phase, ""))
	var body := _load_visual(body_path, "body")
	if body == null:
		_set_contract_error("Workshop %s model-state body could not be instantiated" % phase)
		return
	_hide_loaded_visuals()
	body.visible = true
	_active_body = body
	_active_bib = null
	_active_door = null
	current_lifecycle_phase = phase
	active_body_path = body_path
	active_bib_path = ""
	active_door_path = ""
	active_animation_clip = ""
	active_animation_mode = ""
	active_visual_mode = "glb"


func _configure_contract(presentation: Dictionary, lifecycle: Dictionary) -> void:
	var declared_model := String(presentation.get("model", ""))
	var expected_maximum := maximum_health_for_lifecycle(lifecycle)
	var contract_validation_error := validate_lifecycle_contract(
		lifecycle,
		structure_kind,
		declared_model,
		expected_maximum,
		_bundle_object_id if not _fixture_mode else String(lifecycle.get("objectId", ""))
	)
	if contract_validation_error != "":
		_set_contract_error(contract_validation_error)
		return
	_lifecycle = lifecycle.duplicate(true)
	_drawable_scripts = (presentation.get("drawableScripts", []) as Array).duplicate(true)
	var millimeters := float(presentation.get("heightMillimeters", 0.0))
	_target_height = millimeters / 1000.0 if millimeters > 0.0 else DEFAULT_TARGET_HEIGHT
	var preflight_error := _preflight_required_paths()
	if preflight_error != "":
		_set_contract_error(preflight_error)
		return
	if int(_lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1 and not _fixture_mode:
		var selected_manifest := ModLoader.resolve_pack_path(_pack_root, "pack.json")
		# ContentDB normally registers only manifested packs. The missing-manifest
		# branch is retained solely for repository-authored direct fixture seams;
		# the actual vertical-slice and map loaders validate pack.json first.
		if selected_manifest != "" and FileAccess.file_exists(selected_manifest):
			_runtime_route_registry = _selected_pack_route_registry()
			if _runtime_route_registry.is_empty():
				_set_contract_error("selected-pack lifecycle route registry is missing or invalid")
				return
			var route_error := validate_declared_route_registry(_lifecycle, _runtime_route_registry)
			if route_error != "":
				_set_contract_error(route_error)
				return
			_collect_route_blockers()
	var intact_path := _intact_visual_path(_lifecycle)
	var intact_visual := _load_visual(intact_path, "body")
	if intact_visual == null:
		_set_contract_error("intact lifecycle GLB could not be instantiated")
		return
	var asset_factory = load("res://src/view/asset_factory.gd")
	var intact_aabb: AABB = asset_factory.model_aabb(intact_visual)
	if intact_aabb.size.y <= 0.000001:
		_set_contract_error("intact lifecycle GLB has no measurable body AABB")
		return
	shared_uniform_scale = _source_unit_scale if _source_unit_scale > 0.0 else _target_height / intact_aabb.size.y
	shared_vertical_offset = -intact_aabb.position.y * shared_uniform_scale
	_model_host.scale = Vector3.ONE * shared_uniform_scale
	_model_host.position.y = shared_vertical_offset
	intact_visual.visible = false
	_apply_visual_bounds_selection_radius(intact_aabb, shared_uniform_scale)
	retail_visual_loaded = true
	presentation_mode = "private-imported-lifecycle" if not _fixture_mode else "legal-safe-lifecycle-fixture"
	_update_lifecycle_metadata()


func _preflight_required_paths() -> String:
	var entries := _required_path_entries(_lifecycle, structure_kind)
	if _fixture_mode:
		for role_value in entries.keys():
			var role := String(role_value)
			var relative := String(entries[role])
			if not _fixture_visuals.has(relative) or not (_fixture_visuals[relative] is Node3D):
				return "%s has no valid fixture model" % role
			_resolved_paths[relative] = relative
		return ""
	var asset_factory = load("res://src/view/asset_factory.gd")
	for role_value in entries.keys():
		var role := String(role_value)
		var relative := String(entries[role]).replace("\\", "/")
		var resolved := ContentDB.resolve_asset(relative, _pack_root)
		if resolved == "" or not ModLoader.path_is_within(_pack_root, resolved):
			return "%s is missing or escapes the selected pack" % role
		var asset_error: String = asset_factory.preflight_explicit_model_path(resolved, _pack_root)
		if asset_error != "":
			return "%s failed GLB preflight: %s" % [role, asset_error]
		_resolved_paths[relative] = resolved
	return ""


func _selected_pack_route_registry() -> Dictionary:
	var registry := {
		"audioEvents": {},
		"fxLists": {},
		"particleSystems": {},
	}
	var tree := Engine.get_main_loop() as SceneTree
	var content_db := tree.root.get_node_or_null("ContentDB") if tree != null else null
	if content_db == null or _pack_root == "":
		return {}
	var audio_events: Dictionary = _lifecycle.get("audioEvents", {}) as Dictionary
	for event_value in audio_events.values():
		if event_value == null:
			continue
		var event_id := String(event_value)
		var definition: Variant = content_db.call("get_retail_audio_event", event_id)
		if typeof(definition) == TYPE_DICTIONARY and not (definition as Dictionary).is_empty():
			(registry.audioEvents as Dictionary)[event_id.to_lower()] = "selected-pack-event"

	# The particle-bindings document lives in whichever allowed pack declares it
	# (the host Fords pack for structures arriving from faction supplement
	# packs). A candidate with no declared bindings is skipped, not fatal; only
	# a declared-but-invalid document or no declaration anywhere fails closed.
	for candidate_root in _route_registry_pack_roots():
		var pack_path := ModLoader.resolve_pack_path(candidate_root, "pack.json")
		var pack_document := _read_bounded_json_for_root(pack_path, MAX_ROUTE_DOCUMENT_BYTES, candidate_root)
		if pack_document.is_empty() or typeof(pack_document.get("files")) != TYPE_DICTIONARY:
			continue
		var files: Dictionary = pack_document.get("files", {}) as Dictionary
		var bindings_relative := String(files.get("fordsParticleBindings", ""))
		if bindings_relative == "":
			continue
		var bindings_path := ModLoader.resolve_pack_path(candidate_root, bindings_relative)
		var bindings := _read_bounded_json_for_root(bindings_path, MAX_ROUTE_DOCUMENT_BYTES, candidate_root)
		if (
			String(bindings.get("schema", "")) != "openbfme.fords-particle-bindings"
			or int(bindings.get("schemaVersion", -1)) != 0
		):
			return {}

		var unresolved: Dictionary = {}
		var family: Dictionary = bindings.get("familyResolution", {}) as Dictionary
		for value in family.get("unresolvedDuplicateIdentifierSystemIds", []) as Array:
			unresolved[String(value)] = true
		var selected: Dictionary = {}
		for value in family.get("provisionalRuntimeSelections", []) as Array:
			if typeof(value) != TYPE_DICTIONARY:
				var row: Dictionary = value
				selected[String(row.get("particleSystemId", ""))] = String(row.get("status", ""))
		for value in bindings.get("definitionRegistry", []) as Array:
			if typeof(value) != TYPE_DICTIONARY:
				return {}
			var row: Dictionary = value
			var particle_id := String(row.get("definitionId", ""))
			if particle_id == "":
				return {}
			var status := "exact-single-authored-family"
			if unresolved.has(particle_id):
				status = "blocked-unresolved-cross-family-precedence"
			elif selected.has(particle_id):
				status = String(selected[particle_id])
			(registry.particleSystems as Dictionary)[particle_id] = status
		for value in bindings.get("fxLists", []) as Array:
			if typeof(value) != TYPE_DICTIONARY:
				return {}
			var row: Dictionary = value
			var fx_id := String(row.get("fxListId", ""))
			if fx_id == "":
				return {}
			var status := "source-identified-runtime-emitter-unimplemented"
			for system_value in row.get("systems", []) as Array:
				if typeof(system_value) != TYPE_DICTIONARY:
					return {}
				var system: Dictionary = system_value
				var resolution: Dictionary = system.get("familyResolution", {}) as Dictionary
				if String(resolution.get("status", "")) == "unresolved-cross-family-precedence":
					status = "blocked-unresolved-cross-family-precedence"
			(registry.fxLists as Dictionary)[fx_id] = status
		return registry
	return {}


func _route_registry_pack_roots() -> Array[String]:
	## Candidate packs for the lifecycle route registry, in priority order: the
	## structure's own pack first, then every recorded host/faction pack root.
	var roots: Array[String] = []
	if _pack_root != "":
		roots.append(_pack_root)
	for allowed in _allowed_pack_roots:
		var duplicate := false
		for existing in roots:
			if _same_pack_root(existing, allowed):
				duplicate = true
				break
		if not duplicate:
			roots.append(allowed)
	return roots


func _read_bounded_json_for_root(path: String, maximum_bytes: int, pack_root: String) -> Dictionary:
	if path == "" or not FileAccess.file_exists(path) or not ModLoader.path_is_within(pack_root, path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	var payload := file.get_buffer(file.get_length())
	file.close()
	var parsed: Variant = JSON.parse_string(payload.get_string_from_utf8())
	return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}


func set_allowed_pack_roots(roots: Array) -> void:
	## Records every pack root the lifecycle route registry may resolve from.
	## Mirrors the HUD's multi-pack acceptance: fail closed for content with no
	## pack backing, accept any recorded host/faction pack root otherwise.
	_allowed_pack_roots.clear()
	for root_value in roots:
		var root := String(root_value).strip_edges()
		if root == "":
			continue
		var duplicate := false
		for existing in _allowed_pack_roots:
			if _same_pack_root(existing, root):
				duplicate = true
				break
		if not duplicate:
			_allowed_pack_roots.append(root)


func _same_pack_root(left: String, right: String) -> bool:
	if left == right:
		return true
	var a := left.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()
	var b := right.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()
	return a != "" and a == b


func _collect_route_blockers() -> void:
	route_blockers.clear()
	var effects: Dictionary = _lifecycle.get("effects", {}) as Dictionary
	var fx_registry: Dictionary = _runtime_route_registry.get("fxLists", {}) as Dictionary
	for table_name in ["enteringStateFx", "collapseUpdateFx"]:
		var table: Dictionary = effects.get(table_name, {}) as Dictionary
		for phase_value in table.keys():
			var fx_id := String(table[phase_value])
			var status := String(fx_registry.get(fx_id, ""))
			if status != "runtime-ready":
				_append_route_blocker("fx", fx_id, status)
	var particle_registry: Dictionary = _runtime_route_registry.get("particleSystems", {}) as Dictionary
	for attachment_value in effects.get("particleAttachments", []) as Array:
		var attachment: Dictionary = attachment_value
		var particle_id := String(attachment.get("particleSystemId", ""))
		var status := String(particle_registry.get(particle_id, ""))
		if status != "runtime-ready":
			_append_route_blocker("particle", particle_id, status)


func _append_route_blocker(kind: String, identifier: String, status: String) -> void:
	var row := {"kind": kind, "identifier": identifier, "status": status}
	if not route_blockers.has(row):
		route_blockers.append(row)


func _load_visual(relative_path: String, role: String) -> Node3D:
	var cache_key := "%s|%s" % [role, relative_path]
	if _loaded_visuals.has(cache_key):
		return _loaded_visuals[cache_key] as Node3D
	var visual: Node3D
	if _fixture_mode:
		var fixture: Node3D = _fixture_visuals.get(relative_path) as Node3D
		visual = fixture.duplicate() as Node3D if fixture != null else null
	else:
		var asset_factory = load("res://src/view/asset_factory.gd")
		visual = asset_factory.make_explicit_model_visual(
			String(_resolved_paths.get(relative_path, "")),
			team,
			_bundle_object_id
		)
	if visual == null:
		return null
	visual.name = "%s_%s" % [role.capitalize(), relative_path.get_file().get_basename()]
	visual.visible = false
	visual.scale = Vector3.ONE
	visual.position = Vector3.ZERO
	_model_host.add_child(visual)
	_loaded_visuals[cache_key] = visual
	return visual


func _activate_phase(phase: String) -> void:
	var previous := current_lifecycle_phase
	if int(_lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
		_activate_v1_phase(phase)
	else:
		_activate_v0_phase(phase)
	# A phase swap re-instantiates the body: the live level's authored
	# sub-object visibility re-applies on the fresh visual.
	_apply_level_sub_objects()
	last_transition_damage_fx = TransitionDamageFXScript.select_crossing(
		transition_damage_fx_contracts, previous, phase
	)
	set_meta("transition_damage_fx_source", String(last_transition_damage_fx.get("source", "")))
	set_meta("transition_damage_fx_applied", int(last_transition_damage_fx.get("applied", 0)))


func _activate_v0_phase(phase: String) -> void:
	var paths: Dictionary = _lifecycle.get("paths", {}) as Dictionary
	var body_path := String(paths.get(phase, ""))
	var body := _load_visual(body_path, "body")
	if body == null:
		_set_contract_error("%s lifecycle body could not be instantiated" % phase)
		return
	var show_bib := phase != "rubble" and (
		phase != "construction" or bool(_lifecycle.get("bibDuringConstruction", false))
	)
	var bib_path := String(paths.get("bib", "")) if show_bib else ""
	var bib: Node3D
	if show_bib:
		bib = _load_visual(bib_path, "bib")
		if bib == null:
			_set_contract_error("lifecycle bib could not be instantiated")
			return
	var door_path := _door_path_for_phase(phase)
	var door: Node3D
	if door_path != "":
		door = _load_visual(door_path, "door")
		if door == null:
			_set_contract_error("%s fortress door could not be instantiated" % phase)
			return
	_hide_loaded_visuals()
	body.visible = true
	if bib != null:
		bib.visible = true
	if door != null:
		door.visible = true
	_active_body = body
	_active_bib = bib
	_active_door = door
	current_lifecycle_phase = phase
	active_body_path = body_path
	active_bib_path = bib_path
	active_door_path = door_path
	active_visual_mode = "glb"
	active_entering_fx = ""
	active_audio_event = ""
	active_particle_system_ids.clear()
	transition_error = ""
	route_dispatch_error = ""
	route_blockers.clear()
	last_route_request.clear()
	last_route_dispatch.clear()
	_runtime_route_registry.clear()
	rebuild_hole_present = false
	rebuild_hole_phase = ""
	rebuild_hole_body_path = ""
	rebuild_hole_maximum_health = 0
	rebuild_hole_health_regen_percent_per_second = 0.0
	rebuild_hole_fade_in_seconds = 0.0
	rebuild_hole_terminal_duration = ""
	rebuild_hole_original_removal_frame = null
	rebuild_hole_original_removal_frame_status = ""
	retail_mesh_path = String(_resolved_paths.get(body_path, body_path))
	_apply_declared_phase_animation(body, phase, construction_ratio)
	_update_lifecycle_metadata()


func _activate_v1_phase(phase: String) -> void:
	var row := _v1_phase_row(_lifecycle, phase)
	if row.is_empty():
		_set_contract_error("version-1 lifecycle phase is not declared: %s" % phase)
		return
	var visual: Dictionary = row.get("visual", {}) as Dictionary
	var visual_mode := String(visual.get("mode", ""))
	var body_path := String(visual.get("glb", "")) if visual_mode == "glb" else ""
	var body: Node3D
	if visual_mode == "glb":
		body = _load_visual(body_path, "body")
		if body == null:
			_set_contract_error("%s lifecycle body could not be instantiated" % phase)
			return
	elif visual_mode != "no-render":
		_set_contract_error("%s lifecycle visual mode is unsupported" % phase)
		return

	var bib_value: Variant = _lifecycle.get("bib", {})
	var bib_contract: Dictionary = bib_value if typeof(bib_value) == TYPE_DICTIONARY else {}
	var show_bib := not bib_contract.is_empty() and (
		phase in ["intact", "damaged", "really-damaged"] or (
			phase == "construction" and bool(bib_contract.get("duringConstruction", false))
		)
	)
	var bib_path := ""
	var bib: Node3D
	if show_bib:
		var bib_visual: Dictionary = bib_contract.get("visual", {}) as Dictionary
		bib_path = String(bib_visual.get("glb", ""))
		bib = _load_visual(bib_path, "bib")
		if bib == null:
			_set_contract_error("lifecycle bib could not be instantiated")
			return
	var door_path := _door_path_for_phase(phase) if visual_mode == "glb" else ""
	var door: Node3D
	if door_path != "":
		door = _load_visual(door_path, "door")
		if door == null:
			_set_contract_error("%s fortress door could not be instantiated" % phase)
			return

	_hide_loaded_visuals()
	if body != null:
		body.visible = true
	if bib != null:
		bib.visible = true
	if door != null:
		door.visible = true
	_active_body = body
	_active_bib = bib
	_active_door = door
	current_lifecycle_phase = phase
	active_body_path = body_path
	active_bib_path = bib_path
	active_door_path = door_path
	active_visual_mode = visual_mode
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0 and visual_mode != "no-render"
	var show_health_bar := health_ratio > 0.0 and visual_mode != "no-render" and draws_health_bar() and (selected or health_ratio < 1.0)
	if _health_fill != null:
		_health_fill.visible = show_health_bar
	if _health_back != null:
		_health_back.visible = show_health_bar
	retail_mesh_path = String(_resolved_paths.get(body_path, body_path)) if body_path != "" else ""
	if body != null:
		_apply_drawable_scripts_for_phase(body, row, phase)
		_apply_declared_phase_animation(body, phase, construction_ratio)
	else:
		active_animation_clip = ""
		active_animation_mode = "none"
	_bind_v1_phase_routes(row)
	if rebuild_hole_present and _rebuild_hole_visual != null:
		_rebuild_hole_visual.visible = true
	_update_lifecycle_metadata()
	_publish_v1_route_request(phase)


func _apply_drawable_scripts_for_phase(body: Node3D, row: Dictionary, phase: String) -> void:
	drawable_actions_applied = 0
	drawable_action_gaps.clear()
	if body == null or _drawable_scripts.is_empty():
		_drawable_audio_activation_key = ""
		return

	var conditions: Array[String] = []
	for set_value in row.get("sourceConditionSets", []) as Array:
		for condition_value in set_value as Array:
			var condition := String(condition_value)
			if not conditions.has(condition):
				conditions.append(condition)
	if phase == "intact" and not conditions.has("NONE"):
		conditions.append("NONE")
	var asset_factory = load("res://src/view/asset_factory.gd")
	var result: Dictionary = asset_factory.apply_drawable_scripts(
		body, {"sourceObjectId": _bundle_object_id, "drawableScripts": _drawable_scripts}, conditions
	)
	drawable_actions_applied = int(result.get("applied", 0))
	drawable_action_gaps = (result.get("unhandled", []) as Array).duplicate(true)
	# Drawable scripts are re-applied while synchronizing the same visible
	# phase. Audio is an activation edge, not a per-frame side effect: the body
	# instance plus phase/condition ordering gives that edge a stable view-local
	# identity. This presentation sequence is deliberately outside sim hashes.
	var activation_key := "%d|%s|%s" % [body.get_instance_id(), phase, "\u001f".join(conditions)]
	if activation_key == _drawable_audio_activation_key:
		return
	_drawable_audio_activation_key = activation_key
	var requests: Array[Dictionary] = []
	for intent_value in result.get("audio_intents", []) as Array:
		if typeof(intent_value) != TYPE_DICTIONARY:
			continue
		var intent := (intent_value as Dictionary).duplicate(true)
		var event_id := String(intent.get("event_id", ""))
		if event_id == "":
			continue
		intent["entityId"] = entity_id
		intent["objectId"] = _bundle_object_id
		intent["phase"] = phase
		intent["audioEvent"] = event_id
		intent["routeKind"] = "drawable-script.play-sound"
		requests.append(intent)
	if requests.is_empty():
		return
	drawable_audio_requests.append_array(requests)
	if drawable_audio_requests.size() > MAX_DRAWABLE_AUDIO_RECEIPTS:
		drawable_audio_requests = drawable_audio_requests.slice(
			drawable_audio_requests.size() - MAX_DRAWABLE_AUDIO_RECEIPTS
		)
	if is_inside_tree():
		_emit_drawable_audio_requests(requests)
	else:
		_pending_drawable_audio_requests.append_array(requests)


func _scenario_structure_document_for_bundle_id(bundle_object_id: String) -> Dictionary:
	if bundle_object_id == "" or _scenario_game == "" or not ContentDB.has_method("get_scenario_structure_runtimes"):
		return {}
	for document_value in ContentDB.get_scenario_structure_runtimes(_scenario_game).values():
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document := document_value as Dictionary
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var presentation: Dictionary = registration.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		if String(lifecycle.get("objectId", "")) == bundle_object_id:
			return document
	return {}


func _emit_drawable_audio_requests(requests: Array[Dictionary]) -> void:
	# Reuse the authoritative lifecycle route consumed by RetailSliceAudio.
	# Signal emission is synchronous and preserves source script/action order.
	for request in requests:
		last_route_request = request.duplicate(true)
		lifecycle_route_requested.emit(request.duplicate(true))


func _door_path_for_phase(phase: String) -> String:
	if structure_kind != "fortress":
		return ""
	var components: Dictionary = _lifecycle.get("components", {}) as Dictionary
	var door: Dictionary = components.get("door", {}) as Dictionary
	if phase == "construction":
		return String((door.get("construction", {}) as Dictionary).get("path", ""))
	if phase in ["collapsing", "rubble"]:
		return String((door.get("rubble", {}) as Dictionary).get("path", ""))
	return String((door.get("closed", {}) as Dictionary).get("path", ""))


func _hide_loaded_visuals() -> void:
	for visual_value in _loaded_visuals.values():
		var visual := visual_value as Node3D
		if visual != null:
			visual.visible = false
			_stop_animations(visual)


func _apply_declared_phase_animation(node: Node3D, phase: String, progress: float) -> void:
	active_animation_clip = ""
	active_animation_mode = ""
	if node == null:
		return
	var declared_names: Array = []
	var mode := ""
	if int(_lifecycle.get("schemaVersion", -1)) == LIFECYCLE_SCHEMA_VERSION_V1:
		var phase_row := _v1_phase_row(_lifecycle, phase)
		var animation: Dictionary = phase_row.get("animation", {}) as Dictionary
		mode = String(animation.get("mode", ""))
		var clip_value: Variant = animation.get("clip")
		if typeof(clip_value) == TYPE_STRING:
			declared_names.append(String(clip_value))
	else:
		var clips: Dictionary = _lifecycle.get("clips", {}) as Dictionary
		if typeof(clips.get(phase)) != TYPE_DICTIONARY:
			return
		var row: Dictionary = clips[phase]
		declared_names = row.get("names", []) as Array
		mode = String(row.get("mode", ""))
	active_animation_mode = mode
	if mode == "none":
		return
	for player in _animation_players(node):
		var clip := _first_declared_available_clip(player, declared_names)
		if clip == "":
			continue
		if mode == "loop" or mode == "loop-random":
			# Source GLB clips preserve their one-shot metadata; the declared
			# loop mode must be enforced or idle animations (banners, doors,
			# farm/barracks idles) play once at activation and freeze — which is
			# why long-standing preplaced structures looked static while fresh
			# builds animated. loop-random loops the authored clip(s); random
			# variant selection is deferred until a document declares more than
			# one idle clip for a phase.
			var looped := player.get_animation(clip)
			if looped != null:
				looped.loop_mode = Animation.LOOP_LINEAR
		player.play(clip)
		if mode == "manual-progress":
			player.pause()
			var animation := player.get_animation(clip)
			if animation != null:
				player.seek(clampf(progress, 0.0, 1.0) * animation.length, true)
		active_animation_clip = clip
		return
	_set_contract_error("%s lifecycle GLB has none of its declared animation clips" % phase)


func _bind_v1_phase_routes(row: Dictionary) -> void:
	active_entering_fx = ""
	active_audio_event = ""
	active_particle_system_ids.clear()
	var phase := String(row.get("phase", ""))
	var effects: Dictionary = _lifecycle.get("effects", {}) as Dictionary
	var entering: Dictionary = effects.get("enteringStateFx", {}) as Dictionary
	active_entering_fx = String(entering.get(phase, ""))
	var audio_role: String = String({
		"damaged": "damaged",
		"really-damaged": "reallyDamaged",
		"collapsing": "collapse",
	}.get(phase, ""))
	if audio_role != "":
		active_audio_event = audio_event_id(String(audio_role))
	var active_conditions: Dictionary = {}
	for condition_set_value in row.get("sourceConditionSets", []) as Array:
		for condition_value in condition_set_value as Array:
			active_conditions[String(condition_value)] = true
	for attachment_value in effects.get("particleAttachments", []) as Array:
		var attachment: Dictionary = attachment_value
		var matches := false
		for condition_value in attachment.get("sourceConditions", []) as Array:
			var condition := String(condition_value)
			# Retail ModelConditionState = NONE on the well's second Draw module
			# (well.ini:122-126 WellHealFX) means "no special condition" — the
			# intact building. Matching only exact phase flags dropped it.
			if condition == "NONE":
				if phase == "intact":
					matches = true
					break
				continue
			if active_conditions.has(condition):
				matches = true
				break
		if matches:
			var particle_id := String(attachment.get("particleSystemId", ""))
			if not active_particle_system_ids.has(particle_id):
				active_particle_system_ids.append(particle_id)


func _publish_v1_route_request(phase: String) -> void:
	if active_entering_fx == "" and active_audio_event == "" and active_particle_system_ids.is_empty():
		_pending_route_phase = ""
		last_route_request.clear()
		route_dispatch_error = ""
		last_route_dispatch.clear()
		return
	if not is_inside_tree():
		_pending_route_phase = phase
		return
	_pending_route_phase = ""
	var active_blockers: Array[Dictionary] = []
	for blocker in route_blockers:
		var identifier := String(blocker.get("identifier", ""))
		if identifier == active_entering_fx or active_particle_system_ids.has(identifier):
			active_blockers.append(blocker.duplicate(true))
	last_route_request = {
		"entityId": entity_id,
		"objectId": _bundle_object_id,
		"phase": phase,
		"enteringFx": active_entering_fx,
		"audioEvent": active_audio_event,
		"particleSystemIds": active_particle_system_ids.duplicate(),
		"blockers": active_blockers,
		"worldPosition": global_position,
	}
	route_dispatch_error = ""
	last_route_dispatch.clear()
	lifecycle_route_requested.emit(last_route_request.duplicate(true))


func _declared_transition_blocker(from_phase: String, to_phase: String) -> String:
	var facts: Dictionary = _lifecycle.get("simulationFacts", {}) as Dictionary
	var collapse: Dictionary = facts.get("collapse", {}) as Dictionary
	var timing_status := String(collapse.get("exactTotalTimingStatus", ""))
	if from_phase == "really-damaged" and to_phase == "collapsing":
		if collapse.get("module") == null and timing_status.contains("not-proven"):
			return "collapse reachability/order is unresolved: %s" % timing_status
	if from_phase == "collapsing" and to_phase == "rubble":
		if timing_status.contains("blocked") or timing_status.contains("not-proven"):
			return "collapse completion timing is unresolved: %s" % timing_status
	if from_phase == "rubble" and to_phase == "post-rubble":
		if typeof(_lifecycle.get("rebuildHole")) == TYPE_DICTIONARY:
			var rebuild: Dictionary = _lifecycle["rebuildHole"]
			var removal_status := String(rebuild.get("exactOriginalRemovalFrameStatus", ""))
			if rebuild.get("exactOriginalRemovalFrame") == null:
				return "original-object removal timing is unresolved: %s" % removal_status
		var post_rubble: Dictionary = facts.get("postRubble", {}) as Dictionary
		var post_status := String(post_rubble.get("exactPostRubbleTransitionTimingStatus", ""))
		if post_status.contains("blocked") or post_status.contains("not-proven"):
			return "post-rubble transition timing is unresolved: %s" % post_status
	return ""


func _ensure_rebuild_hole_visible() -> bool:
	var value: Variant = _lifecycle.get("rebuildHole")
	if value == null:
		return true
	if typeof(value) != TYPE_DICTIONARY:
		_set_contract_error("rebuild-hole lifecycle contract is malformed")
		return false
	var rebuild: Dictionary = value
	var states: Array = rebuild.get("states", []) as Array
	if states.size() != 1 or typeof(states[0]) != TYPE_DICTIONARY:
		_set_contract_error("rebuild-hole visible-rubble state is missing")
		return false
	var state: Dictionary = states[0]
	var visual: Dictionary = state.get("visual", {}) as Dictionary
	var body_path := String(visual.get("glb", ""))
	if _rebuild_hole_visual == null:
		_rebuild_hole_visual = _load_visual(body_path, "rebuild-hole")
	if _rebuild_hole_visual == null:
		_set_contract_error("rebuild-hole visible-rubble GLB could not be instantiated")
		return false
	_rebuild_hole_visual.visible = true
	rebuild_hole_present = true
	rebuild_hole_phase = String(state.get("phase", ""))
	rebuild_hole_body_path = body_path
	rebuild_hole_maximum_health = int(rebuild.get("maximumHealth", 0))
	rebuild_hole_health_regen_percent_per_second = float(rebuild.get("healthRegenPercentPerSecond", 0.0))
	rebuild_hole_fade_in_seconds = float(state.get("fadeInSeconds", 0.0))
	rebuild_hole_terminal_duration = String(rebuild.get("terminalDuration", ""))
	rebuild_hole_original_removal_frame = rebuild.get("exactOriginalRemovalFrame")
	rebuild_hole_original_removal_frame_status = String(rebuild.get("exactOriginalRemovalFrameStatus", ""))
	_rebuild_hole_visual.set_meta("presentation", "retail-rebuild-hole-visible-rubble")
	_rebuild_hole_visual.set_meta("source_type_name", String(rebuild.get("sourceTypeName", "")))
	_rebuild_hole_visual.set_meta("spawn_timing", String(rebuild.get("spawnTiming", "")))
	_rebuild_hole_visual.set_meta("maximum_health", rebuild_hole_maximum_health)
	_rebuild_hole_visual.set_meta("health_regen_percent_per_second", rebuild_hole_health_regen_percent_per_second)
	_rebuild_hole_visual.set_meta("fade_in_seconds", rebuild_hole_fade_in_seconds)
	_rebuild_hole_visual.set_meta("terminal_duration", rebuild_hole_terminal_duration)
	_rebuild_hole_visual.set_meta("exact_original_removal_frame", rebuild_hole_original_removal_frame)
	_rebuild_hole_visual.set_meta("exact_original_removal_frame_status", rebuild_hole_original_removal_frame_status)
	_update_lifecycle_metadata()
	return true


func _first_declared_available_clip(player: AnimationPlayer, declared_names: Array) -> String:
	var actual_by_canonical: Dictionary = {}
	for animation_name in player.get_animation_list():
		var actual := String(animation_name)
		if actual != "RESET":
			actual_by_canonical[actual.to_lower()] = actual
	for declared_value in declared_names:
		var declared := String(declared_value)
		if actual_by_canonical.has(declared.to_lower()):
			return String(actual_by_canonical[declared.to_lower()])
	return ""


func _animation_players(node: Node3D) -> Array[AnimationPlayer]:
	var result: Array[AnimationPlayer] = []
	var pending: Array[Node] = [node]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current is AnimationPlayer:
			result.append(current as AnimationPlayer)
		for child_value in current.get_children():
			if child_value is Node:
				pending.append(child_value as Node)
	return result


func _stop_animations(node: Node3D) -> void:
	for player in _animation_players(node):
		player.stop()


func _configure_area_effect_presentation() -> void:
	## Retail well/statue SelectionDecal (experiencelevels.ini:10091-10140) and
	## PassiveAreaEffectBehavior (well.ini:228, statue.ini:168). The ring is
	## the authored EffectRadius; WellHealFX is the intact well's water cue.
	## fortress.ini:897-904 House of Healing is the same module with
	## UpgradeRequired — no ring until that upgrade is applied.
	var effect := _passive_area_effect_from_document()
	aura_upgrade_required = String(effect.get("upgrade_required", ""))
	if not effect.is_empty() and _aura_upgrade_satisfied():
		aura_radius_source = float(effect.get("radius_source", 0.0))
		aura_effect_kind = String(effect.get("kind", ""))
		var scale := _source_unit_scale if _source_unit_scale > 0.0 else 1.0
		aura_radius_local = aura_radius_source * scale
		_build_aura_ring()
	else:
		aura_radius_source = 0.0
		aura_radius_local = 0.0
		aura_effect_kind = ""
		_clear_aura_ring()
	_sync_water_fx()


func _ingest_structure_upgrades(entity: Dictionary) -> void:
	var next_ids: Dictionary = {}
	for item in entity.get("completed_upgrades", []) as Array:
		var upgrade_id := String(item)
		if upgrade_id != "":
			next_ids[upgrade_id] = true
	var applied: Variant = entity.get("applied_upgrades", {})
	if typeof(applied) == TYPE_DICTIONARY:
		for key_value in (applied as Dictionary).keys():
			var upgrade_id := String(key_value)
			if upgrade_id != "":
				next_ids[upgrade_id] = true
	var changed := next_ids.size() != _structure_upgrade_ids.size()
	if not changed:
		for key_value in next_ids.keys():
			if not _structure_upgrade_ids.has(key_value):
				changed = true
				break
	_structure_upgrade_ids = next_ids
	if changed and aura_upgrade_required != "":
		_configure_area_effect_presentation()


func _aura_upgrade_satisfied() -> bool:
	return aura_upgrade_required == "" or _structure_upgrade_ids.has(aura_upgrade_required)


func _passive_area_effect_from_document() -> Dictionary:
	var document := _playable_structure_document()
	if document.is_empty():
		return {}
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	var compiled: Variant = gameplay.get("passiveAreaEffect")
	if typeof(compiled) == TYPE_DICTIONARY and float((compiled as Dictionary).get("radius", 0.0)) > 0.0:
		var row := compiled as Dictionary
		return {
			"radius_source": float(row.get("radius", 0.0)),
			"kind": "heal" if String(row.get("healFx", "")) != "" or float(row.get("healPercentPerSecond", 0.0)) > 0.0 else "leadership",
			"heal_fx": String(row.get("healFx", "")),
			"upgrade_required": String(row.get("upgradeRequired", "")),
		}
	for contract_value in gameplay.get("moduleContracts", []) as Array:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "PassiveAreaEffectBehavior":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var authored := String((fields.get("EffectRadius", {}) as Dictionary).get("authored", ""))
		var radius := float(AREA_EFFECT_RADIUS_SOURCE.get(authored, 0.0))
		if radius <= 0.0 and authored.is_valid_float():
			radius = float(authored)
		if radius <= 0.0:
			continue
		var heal_fx := String((fields.get("HealFX", {}) as Dictionary).get("authored", ""))
		var modifier := String((fields.get("ModifierName", {}) as Dictionary).get("authored", ""))
		return {
			"radius_source": radius,
			"kind": "heal" if heal_fx != "" else ("leadership" if modifier != "" else "area"),
			"heal_fx": heal_fx,
			"upgrade_required": String((fields.get("UpgradeRequired", {}) as Dictionary).get("authored", "")),
		}
	return {}


func _playable_structure_document() -> Dictionary:
	if not ContentDB.has_method("get_playable_structure_runtime"):
		return {}
	var direct: Variant = ContentDB.get_playable_structure_runtime(_bundle_object_id)
	if typeof(direct) == TYPE_DICTIONARY and not (direct as Dictionary).is_empty():
		return direct as Dictionary
	if ContentDB.has_method("get_playable_structure_runtimes"):
		for source_id_value in ContentDB.get_playable_structure_runtimes().keys():
			var document: Dictionary = ContentDB.get_playable_structure_runtime(String(source_id_value))
			var object_id := String(document.get("objectId", ""))
			if object_id == _bundle_object_id or String(document.get("id", "")) == _bundle_object_id:
				return document
			var registration: Dictionary = document.get("registration", {}) as Dictionary
			var life: Dictionary = registration.get("buildingLifecycle", {}) as Dictionary
			if life.is_empty():
				life = (registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
			if String(life.get("objectId", "")) == _bundle_object_id:
				return document
			var folded_source := object_id.to_lower()
			var folded_bundle := _bundle_object_id.to_lower().replace("bfme2.object.", "").replace("-", "")
			if folded_source == folded_bundle:
				return document
	var scenario_document := _scenario_structure_document_for_bundle_id(_bundle_object_id)
	if not scenario_document.is_empty():
		return scenario_document
	return {}


func _clear_aura_ring() -> void:
	if _aura_ring != null and is_instance_valid(_aura_ring):
		_aura_ring.queue_free()
	_aura_ring = null


func _build_aura_ring() -> void:
	if aura_radius_local <= 0.0:
		return
	_clear_aura_ring()
	_aura_ring = MeshInstance3D.new()
	_aura_ring.name = "AreaEffectRadius"
	var mesh := TorusMesh.new()
	mesh.inner_radius = maxf(0.01, aura_radius_local * 0.94)
	mesh.outer_radius = aura_radius_local
	_aura_ring.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = (
		Color(0.35, 0.72, 0.95, 0.42) if aura_effect_kind == "heal" else Color(0.86, 0.78, 0.32, 0.42)
	)
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	_aura_ring.material_override = material
	_aura_ring.position = Vector3(0.0, 0.08, 0.0)
	_aura_ring.visible = false
	add_child(_aura_ring)
	_sync_aura_visibility()


func _sync_aura_visibility() -> void:
	if _aura_ring == null or not is_instance_valid(_aura_ring):
		return
	_aura_ring.visible = (
		selected
		and health_ratio > 0.0
		and construction_ratio >= 1.0
		and active_visual_mode != "no-render"
		and aura_radius_local > 0.0
	)


func _authors_well_heal_fx() -> bool:
	## well.ini:121-126 second Draw TheHealEffect ParticleSysBone NONE WellHealFX.
	## House of Healing (fortress.ini:904) authors FX_SpellHealUnitHealBuff —
	## that is a unit heal ping, not the well fountain.
	if _well_water_resolved:
		return _well_water_authored
	if structure_kind == "well" or _bundle_object_id.to_lower().contains("well"):
		_well_water_authored = true
		_well_water_resolved = true
		return true
	var document := _playable_structure_document()
	if not document.is_empty():
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var life: Dictionary = registration.get("buildingLifecycle", {}) as Dictionary
		if life.is_empty():
			life = (registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
		var effects: Dictionary = life.get("effects", {}) as Dictionary
		for attachment_value in effects.get("particleAttachments", []) as Array:
			if typeof(attachment_value) != TYPE_DICTIONARY:
				continue
			if String((attachment_value as Dictionary).get("particleSystemId", "")) == WELL_WATER_FX_ID:
				_well_water_authored = true
				_well_water_resolved = true
				return true
	_well_water_authored = false
	_well_water_resolved = true
	return false


func _sync_water_fx() -> void:
	var wants_water := (
		_authors_well_heal_fx()
		and current_lifecycle_phase in ["", "intact"]
		and construction_ratio >= 1.0
		and health_ratio > 0.0
	)
	if not wants_water:
		if _water_fx != null and is_instance_valid(_water_fx):
			_water_fx.visible = false
		water_fx_present = false
		return
	if _water_fx != null and is_instance_valid(_water_fx):
		_water_fx.visible = true
		water_fx_present = true
		return
	_water_fx = Node3D.new()
	_water_fx.name = "WellHealFX"
	var column := MeshInstance3D.new()
	column.name = "WellWaterColumn"
	var mesh := CylinderMesh.new()
	var scale := _source_unit_scale if _source_unit_scale > 0.0 else 1.0
	mesh.top_radius = 4.0 * scale
	mesh.bottom_radius = 6.0 * scale
	mesh.height = 10.0 * scale
	column.mesh = mesh
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = Color(0.45, 0.72, 0.92, 0.38)
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	column.material_override = material
	column.position = Vector3(0.0, mesh.height * 0.45, 0.0)
	_water_fx.add_child(column)
	add_child(_water_fx)
	water_fx_present = true
	if not active_particle_system_ids.has(WELL_WATER_FX_ID):
		active_particle_system_ids.append(WELL_WATER_FX_ID)


func bind_transition_damage_fx_contracts(source: Dictionary) -> int:
	transition_damage_fx_contracts.clear()
	for row_value in UnitAdapterScript.module_contracts(source):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		if String(row.get("module", "")) != "TransitionDamageFX":
			continue
		if not row.has("runtimeStatus") and row.has("runtime_status"):
			row["runtimeStatus"] = String(row.get("runtime_status", ""))
		transition_damage_fx_contracts.append(row)
	if transition_damage_fx_contracts.is_empty():
		for row_value in source.get("moduleContracts", []) as Array:
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var raw := row_value as Dictionary
			if String(raw.get("module", "")) != "TransitionDamageFX":
				continue
			transition_damage_fx_contracts.append(raw.duplicate(true))
	return transition_damage_fx_contracts.size()


func _set_contract_error(message: String) -> void:
	if contract_error == "":
		contract_error = message
	presentation_mode = "contract-error"
	retail_visual_loaded = false
	current_lifecycle_phase = ""
	active_body_path = ""
	active_bib_path = ""
	active_door_path = ""
	active_animation_clip = ""
	active_animation_mode = ""
	active_visual_mode = ""
	active_entering_fx = ""
	active_audio_event = ""
	active_particle_system_ids.clear()
	transition_error = ""
	route_dispatch_error = ""
	route_blockers.clear()
	last_route_request.clear()
	last_route_dispatch.clear()
	_runtime_route_registry.clear()
	rebuild_hole_present = false
	rebuild_hole_phase = ""
	rebuild_hole_body_path = ""
	rebuild_hole_maximum_health = 0
	rebuild_hole_health_regen_percent_per_second = 0.0
	rebuild_hole_fade_in_seconds = 0.0
	rebuild_hole_terminal_duration = ""
	rebuild_hole_original_removal_frame = null
	rebuild_hole_original_removal_frame_status = ""
	retail_mesh_path = ""
	if _visual_root != null:
		_visual_root.visible = false
	_hide_loaded_visuals()
	_update_lifecycle_metadata()


func _update_lifecycle_metadata() -> void:
	set_meta("building_lifecycle_state", lifecycle_state())
	set_meta("building_lifecycle_contract", _lifecycle.duplicate(true))


func _build_markers() -> void:
	_selection_ring = MeshInstance3D.new()
	_selection_ring.name = "SelectionRing"
	var ring := TorusMesh.new()
	ring.inner_radius = pick_radius
	ring.outer_radius = pick_radius + 0.22
	ring.rings = 36
	ring.ring_segments = 8
	_selection_ring.mesh = ring
	_selection_ring.position = Vector3(visual_pick_center.x, 0.08, visual_pick_center.y)
	_selection_ring.material_override = _emissive(Color("67e48b"))
	_selection_ring.visible = false
	add_child(_selection_ring)

	_health_back = Sprite3D.new()
	_health_back.name = "HealthBack"
	_configure_fixed_pixel_sprite(_health_back, _solid_texture(Color("131a1e"), HEALTH_BAR_SCREEN_WIDTH_PX + 4, HEALTH_BAR_SCREEN_HEIGHT_PX + 2), 8)
	_health_back.visible = false
	add_child(_health_back)
	_health_fill = Sprite3D.new()
	_health_fill.name = "HealthFill"
	_configure_fixed_pixel_sprite(_health_fill, _solid_texture(Color("5bd765") if team == 0 else Color("df5a4f"), HEALTH_BAR_SCREEN_WIDTH_PX, HEALTH_BAR_SCREEN_HEIGHT_PX), 9)
	_health_fill.visible = false
	add_child(_health_fill)
	# Construction progress rides just under the health bar in retail gold.
	_build_back = Sprite3D.new()
	_build_back.name = "BuildBack"
	_configure_fixed_pixel_sprite(_build_back, _solid_texture(Color("1e1710"), HEALTH_BAR_SCREEN_WIDTH_PX + 4, HEALTH_BAR_SCREEN_HEIGHT_PX + 2), 8)
	_build_back.visible = false
	add_child(_build_back)
	_build_fill = Sprite3D.new()
	_build_fill.name = "BuildFill"
	_configure_fixed_pixel_sprite(_build_fill, _solid_texture(Color("e0b64f"), HEALTH_BAR_SCREEN_WIDTH_PX, HEALTH_BAR_SCREEN_HEIGHT_PX), 9)
	_build_fill.visible = false
	add_child(_build_fill)
	_sync_health_bar_geometry()


func _configure_fixed_pixel_sprite(sprite: Sprite3D, texture: Texture2D, priority: int) -> void:
	sprite.texture = texture
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.fixed_size = true
	# At the canonical 1080p retail capture viewport this is a 64px command-
	# scale strip. fixed_size keeps that apparent size stable as the camera zooms.
	sprite.pixel_size = 0.00085
	sprite.no_depth_test = true
	sprite.shaded = false
	sprite.render_priority = priority


func _solid_texture(color: Color, width: int, height: int) -> ImageTexture:
	var image := Image.create(width, height, false, Image.FORMAT_RGBA8)
	image.fill(color)
	return ImageTexture.create_from_image(image)


func _sync_selection_ring_geometry() -> void:
	if _selection_ring == null:
		return
	var ring := _selection_ring.mesh as TorusMesh
	if ring == null:
		ring = TorusMesh.new()
		ring.rings = 36
		ring.ring_segments = 8
		_selection_ring.mesh = ring
	ring.inner_radius = pick_radius
	ring.outer_radius = pick_radius + 0.22
	_selection_ring.position = Vector3(visual_pick_center.x, 0.08, visual_pick_center.y)


func _sync_health_bar_geometry() -> void:
	# This is intentionally an exact positive relationship, not merely a cap.
	# A later intact-body measurement must update the value markers were seeded
	# with; the previous min/max expression silently stayed at the legacy 3.6.
	_health_bar_width = maxf(0.001, pick_radius * 2.0)
	if _health_back == null:
		return
	var anchor_y := _visual_top_y + HEALTH_BAR_ROOF_GAP
	_health_back.position = Vector3(0, anchor_y, 0)
	_health_fill.position = Vector3(0, anchor_y, -0.01)
	_build_back.position = Vector3(0, anchor_y - 0.22, 0)
	_build_fill.position = Vector3(0, anchor_y - 0.22, -0.01)


## Retail draws a health bar for objects that can lose health. A fortress build
## plot cannot: its authored Body is `ImmortalBody` (the only structures in every
## published faction pack that carry it are the `*FortressExpansionPad*` objects,
## MaxHealth 15000), so an EMPTY plot floats no bar (user bug #9). The structure
## a player raises on that plot is an ordinary damageable building and keeps its
## bar. Documents without a compiled health block keep the old behaviour.
func draws_health_bar() -> bool:
	return body_module != "ImmortalBody"


func _emissive(color: Color, billboard: bool = false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if billboard:
		material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color * 0.35
	return material
