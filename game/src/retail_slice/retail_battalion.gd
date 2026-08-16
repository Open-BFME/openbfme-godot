class_name RetailBattalion
extends Node3D
## Imported retail member GLBs presented as one selectable battalion.

const ShadowDecalScript = preload("res://src/retail_slice/retail_shadow_decal.gd")
const FormationScript = preload("res://src/retail_slice/retail_formation.gd")
const AnimationTimingScript = preload("res://src/retail_slice/retail_animation_timing.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const AnimationStateSelectScript = preload("res://src/retail_slice/animation_state_select.gd")
const ParticleSysBoneScript = preload("res://src/retail_slice/particle_sys_bone.gd")
const DEFAULT_OBJECT_ID := "bfme2.object.gondor-fighter"
const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const RANGER_OBJECT_ID := "bfme2.object.gondor-ranger"
## Pack file keys. `projectileArt` is the per-projectile art document the
## importer compiles for every faction pack (keyed by compiled
## projectileObjectId); `gondorArcherProjectile` is the older BFME2 Men host
## sidecar that only ever carried the Gondor closure.
const PROJECTILE_ART_PACK_KEY := "projectileArt"
const ARCHER_PROJECTILE_PACK_KEY := "gondorArcherProjectile"
## The only projectile the sidecar declares (its weapon.projectileTemplateId);
## every other projectile it renders is a borrow.
const SIDECAR_PROJECTILE_OBJECT_ID := "GondorArcherArrow"
## Arrow-art diagnostic severity. "info" rows are recorded and logged but do not
## raise an engine warning; see _record_combat_visual_diagnostic.
## Values are DiagLog levels; "warn" additionally raises an engine warning.
const DIAGNOSTIC_SEVERITY := {
	# TODO(2026-08-11): "info" is a point-in-time judgment — the shared-Good borrow is
	# the universal expected state while no pack ships projectileArt. The day a
	# republished pack carries it, a borrow means id-keyed resolution FAILED and this
	# entry must flip to "warn" (rot condition ratified by the arrow-art round-2 review).
	"arrow-art-shared-good-fallback": "info",
	"arrow-art-unresolved": "warn",
	"projectile-art-binding-rejected": "warn",
}
const VISIBLE_ARROW_PROJECTILE_IDS := {
	"GondorArcherArrow": true,
	"GoodFactionArrow": true,
	"EvilFactionArrow": true,
}
const PRESENTATION_TICK_SECONDS := 0.1
const DEFAULT_TURN_RATE_DEGREES_PER_SECOND := 720.0
const ARCHER_LAUNCH_BONE := "ARROWNOCK"
const RANGER_LAUNCH_BONE := "ARROW"
const ARCHER_CURVE_HEIGHT_SOURCE := 9.0
const MELEE_ADVANCE_LIMIT := 3.8
const MELEE_LANE_SPACING := 1.05
const MELEE_RANK_SPACING := 0.72
const SOURCE_HEALTH_GEOMETRY_HEIGHT := {
	"bfme2.object.gondor-fighter": 19.2,
	"bfme2.object.gondor-archer": 19.2,
	"bfme2.object.gondor-tower-guard": 19.2,
	"bfme2.object.gondor-knight": 20.0,
	"bfme2.object.gondor-ranger": 19.2,
}
const SOURCE_HEALTH_HEIGHT_OFFSET := 10.0
## World-unit clearance between a member's measured visual top and its health
## bar. Matches the structure presenter's roof gap (retail_structure.gd
## HEALTH_BAR_ROOF_GAP) so units and buildings float their bars alike.
const HEALTH_BAR_HEAD_GAP := 0.12
## Proportions of the legal-safe world-space member bar, preserved exactly from
## the authored 0.58 x 0.065 back and 0.54 x 0.042 fill so only its size and
## anchor become member-relative.
const BAR_BACK_ASPECT := 0.065 / 0.58
const BAR_FILL_WIDTH_RATIO := 0.54 / 0.58
const BAR_FILL_ASPECT := 0.042 / 0.58
## THE BAR IS SIZED BY THE UNIT'S AUTHORED BODY, NOT BY WHAT IT IS HOLDING.
##
## The measured mesh AABB was used for both the bar's width and (through
## BAR_BACK_ASPECT) its thickness. That AABB contains the WEAPON: a Tower Guard's
## spear and an infantry banner stick metres out of the man, so a spear horde
## drew bars several times wider AND several times thicker than a sword horde
## standing next to it -- the owner's "a lot of the health bars have inconsistent
## sizes", where one horde renders as overlapping green slabs and its neighbour
## as thin ticks.
##
## Retail's own oracle for a unit's body is the authored `Geometry` block, and it
## is near-uniform by design. From the mounted RotWK men pack
## (data/playable-units/*.json, `registration.gameplay.geometry`):
##   GondorFighter          majorRadius 8.0   (object/.../gondor.ini:940)
##   GondorTowerShieldGuard majorRadius 8.0
##   GondorArcher           majorRadius 8.0
##   GondorCavalry          majorRadius 8.0
##   RohanSpearmen          majorRadius 8.0
##   RohanTheoden           majorRadius 8.0
## Sizing the bar off that reproduces retail's near-uniform bar and still leaves
## room for the genuinely larger classes (trolls, siege) as a MINOR width class,
## bounded by the clamp below instead of running away with a mesh extent.
##
## Calibration: the pre-existing legal-safe bar was 0.58 world units wide at the
## slice's validated source scale 0.1, and retail's infantry radius is 8.0, so
## 0.58 / (8.0 * 0.1) = 0.725 world units of bar per source radius per unit of
## scale. Infantry therefore keeps EXACTLY the bar width it shipped with; only
## the runaway weapon-driven widths change.
const MEMBER_BAR_WIDTH_PER_SOURCE_RADIUS := 0.725
## Width classes are bounded, not free. Below/above this band a class stops
## scaling, so no authored footprint can ever reproduce the slab.
const MEMBER_BAR_MINIMUM_SOURCE_RADIUS := 6.0
const MEMBER_BAR_MAXIMUM_SOURCE_RADIUS := 16.0
## Used only when the map supplies no source scale at all.
const MEMBER_BAR_FALLBACK_WIDTH := 0.58
const ArcherProjectileControllerScript = preload("res://src/retail_slice/retail_archer_projectile_controller.gd")
const SelectionDecalScript = preload("res://src/retail_slice/retail_selection_decal.gd")
const PackCapability = preload("res://src/content/pack_capability.gd")
const DiagLogScript = preload("res://src/core/diag_log.gd")
const SelectionPick = preload("res://src/retail_slice/retail_selection_pick.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")
const HealthOverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")

var entity_id := 0
var team := 0
var object_id := DEFAULT_OBJECT_ID
## World-unit footprint radius used for mouse picking: the horde's own
## formation spread plus one member's retail Geometry body. Never a flat
## constant -- see RetailSelectionPick.
var pick_radius := SelectionPick.MINIMUM_SELECTION_RADIUS
var selection_radius_source := "unresolved"
var retail_model_filename := ""
var current_state := "idle"
var current_clip := ""
var member_count := 0
var retail_visual_count := 0
var rigged_member_count := 0
var animation_player_count := 0
var team_tinted_surface_count := 0
var house_color_surface_count := 0
var team_color_status := ""
var member_overlay_status := ""
var clip_map: Dictionary = {}
var clip_sets: Dictionary = {}
var clip_modes: Dictionary = {}
var attack_uses_weapon_timing := false
var authored_animation_speed_ranges: Dictionary = {}
## Per routed state/clip receipts copied from the sealed capability.
## AnimationBlendTime is frames at 30 FPS. AnimationPriority zero/selection
## law is still not applied to playback.
var authored_animation_clip_properties: Dictionary = {}
## StateName is exact only where the compiler linked the authored state label
## to a concrete animation identifier. Keep both sides of the last transition
## so drawable predicates can query STATE_X and PREV_STATE_X without guessing
## from our semantic state names.
var authored_state_label_links: Dictionary = {}
var member_current_authored_state_labels: Dictionary = {}
var member_previous_authored_state_labels: Dictionary = {}
## Typed W3DScriptedModelDraw BeginScript rows from the pack. Applied when a
## member's authored animation state changes so CurDrawableSetTransitionAnimState
## and CurDrawableAllowToContinue can read Prev.
var _drawable_scripts: Array = []
## Last BeginScript receipt from the live `_play_member_state` path. Empty until
## a member state change actually executes authored drawable scripts.
var last_drawable_script_receipt: Dictionary = {}
## Typed SubObjectsUpgrade rows (TriggeredBy + Show/HideSubObjects). Empty means
## the Fire Stones path still uses the named hardcoded FirePlane fallback.
var sub_object_upgrade_contracts: Array = []
var _last_sub_object_show: Array = []
var _last_sub_object_hide: Array = []
## Typed ModelConditionUpgrade flags granted by completed upgrades.
var model_condition_upgrade_contracts: Array = []
var model_condition_flags: Array = []
var _permanent_model_condition_flags: Dictionary = {}
## Typed AnimationState rows (moduleContracts or pack authoredAnimationStates).
var animation_state_contracts: Array = []
var last_animation_state_receipt: Dictionary = {}
var particle_sys_bone_contracts: Array = []
var last_particle_sys_bone_receipt: Dictionary = {}
var equipment_contract: Dictionary = {}
var equipment_contract_ready := false
var unresolved_animation_track_count := 0
var private_parity_mode_active := false
var combat_visual_source_closure_present := false
var exact_projectile_node_count := 0
var exact_impact_effect_node_count := 0
var combat_visual_contract_error := ""
## Named runtime diagnostics for arrow art resolution ({"code": ..., ...}).
## Replaces the silent contract-error-only path: a battalion that falls back to
## the shared Good arrow, or resolves no art at all, says which and why.
var combat_visual_diagnostics: Array[Dictionary] = []
## Human-readable one-liners for the same rows, for surfaces that carry text
## rather than structured rows (the slice's arrow-art aggregate).
var combat_visual_diagnostic_summaries: Array[String] = []
## Test-only. A runner that DELIBERATELY provokes the unresolved-art path (to
## prove the diagnostic fires) would otherwise print an engine warning that the
## proof gates read as a defect. Production never sets this.
static var suppress_engine_warnings := false
var archer_projectile_controller: RetailArcherProjectileController
var source_selection_decal: RetailSelectionDecal
var animation_players: Array[AnimationPlayer] = []
var member_animation_players: Dictionary = {}
var member_current_clips: Dictionary = {}
var member_visuals: Dictionary = {}
## Mesh AABB centre offset in the member visual's local XZ. The selection
## ring / decal sits here so a hero whose body is authored off the node
## origin is not ringed "just on the side".
var member_visual_centers: Dictionary = {}
var member_selection_rings: Dictionary = {}
var member_health_backs: Dictionary = {}
var member_health_fills: Dictionary = {}
var member_health_ratios: Dictionary = {}
var member_health_anchor_heights: Dictionary = {}
## Half the measured horizontal extent of each member's visual, in world units.
## The screen-space health overlay bounds its bar to the member it belongs to
## instead of drawing one fixed-pixel slab per soldier.
var member_health_half_widths: Dictionary = {}
## World-space member bar geometry for the legal-safe (non-parity) presenter:
## the quad's width and its anchor height above the member's own origin.
var member_health_bar_widths: Dictionary = {}
var member_health_bar_heights: Dictionary = {}
## How the members' anchor heights were obtained, as a single battalion-scalar
## summary: "measured-visual-bounds" only when EVERY member was measured. One
## member falling back reports "partial-...", so a scalar can no longer hide a
## per-member measurement failure behind its luckier neighbours.
var member_health_anchor_source := "unmeasured"
## Where the bar's WIDTH came from: the unit's compiled retail `Geometry` block,
## or retail's infantry default when the mounted pack's bundle object carries no
## `sourceObjectId` to reach its playable-unit document with.
var member_health_bar_source := "unsized"
var member_health_measured_count := 0
var member_health_fallback_count := 0
## The player's "Show All Health Bars" opt-in, read once when this battalion is
## configured. A gate may set it directly to exercise the opted-in surface.
var show_all_health_bars := UserSettingsScript.DEFAULT_SHOW_ALL_HEALTH_BARS
var experience_level := 1
var banner_carrier_visual: Node3D
var banner_carrier_object_id := ""
var fire_upgrade_active := false
var fire_upgrade_id := ""
var ring_carrier_visual: Node3D
var ring_carrier_object_id := ""
## Presentation detail level published by the view-side distance LOD
## (src/view/member_render_batcher.gd). 0 = full detail. Above 0 the per-member
## selection rings and health bars stop drawing, because this battalion rewrites
## their visibility every frame and would otherwise immediately undo the LOD.
## Presentation-only: nothing here feeds the simulation.
var presentation_detail_level := 0
var member_attack_tokens: Dictionary = {}
var member_attack_release_tokens: Dictionary = {}
var member_attack_target_globals: Dictionary = {}
var member_action_states: Dictionary = {}
# Members whose death clip is playing; once it finishes the corpse subtree is
# frozen (processing disabled, final pose kept) until the 60s expiry frees it.
var _corpse_settle_pending: Dictionary = {}
var formation: RefCounted = FormationScript.new()
var member_formation_slots: Dictionary = {}
var selected := false
var health_ratio := 1.0
var production_exit_progress := 1.0
var _selection_ring: MeshInstance3D
var _team_ring: MeshInstance3D
var _status_label: Label3D
var _team_marker: MeshInstance3D
var _last_action_token := -1
var _facing_direction := Vector2.RIGHT
var _target_facing_yaw := 0.0
var _facing_sample_ready := false
var _turn_rate_radians_per_second := deg_to_rad(DEFAULT_TURN_RATE_DEGREES_PER_SECOND)
var _source_unit_scale := 0.0
var _attack_target_ref: WeakRef
var _attack_target_height := 0.15
var _attack_travel_seconds := 0.1
var _authoritative_position_from := Vector3.ZERO
var _authoritative_position_to := Vector3.ZERO
var _authoritative_position_elapsed := PRESENTATION_TICK_SECONDS
var _authoritative_position_ready := false
var _selection_layout_state := ""
var _next_archer_presentation_token := 1
var archer_projectiles_presented := 0
var archer_impacts_presented := 0
var weapon_launch_bone := ARCHER_LAUNCH_BONE
## Retail Weapon.ProjectileTemplateName projected by the playable-unit
## descriptor. Empty on packs built before the generic binding landed.
var projectile_object_id := ""
## Last applied mount presentation. Foot model is kept when the pack has no
## mounted-container-payload recipe (named gap, not a silent mesh swap).
var _mount_presentation_applied := false
var mount_visual_gap := ""


func configure(
	id: int,
	battalion_team: int,
	configured_object_id: String,
	capability: Dictionary,
	expected_members: int = 15,
	source_unit_scale: float = 0.0,
	formation_positions: Array = []
) -> void:
	entity_id = id
	if not is_in_group("retail_battalion"):
		add_to_group("retail_battalion")
	team = battalion_team
	object_id = configured_object_id if configured_object_id != "" else DEFAULT_OBJECT_ID
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	var presentation: Dictionary = definition.get("presentation", {}) as Dictionary
	retail_model_filename = String(presentation.get("model", "")).get_file()
	weapon_launch_bone = String(presentation.get("weaponLaunchBone", ARCHER_LAUNCH_BONE))
	if object_id == RANGER_OBJECT_ID and weapon_launch_bone != RANGER_LAUNCH_BONE:
		# The bounded Ranger overlay borrows Men's projectile sidecar and does
		# not repeat the member attachment metadata. Retail's Ranger launch
		# bone is the already-pinned ARROW contract, not Archer's ARROWNOCK.
		weapon_launch_bone = RANGER_LAUNCH_BONE
	private_parity_mode_active = _is_private_retail_pack(definition)
	projectile_object_id = _compiled_projectile_object_id(object_id)
	_source_unit_scale = source_unit_scale if is_finite(source_unit_scale) and source_unit_scale > 0.0 else 0.0
	show_all_health_bars = bool(
		UserSettingsScript.load_controls().get(
			"show_all_health_bars", UserSettingsScript.DEFAULT_SHOW_ALL_HEALTH_BARS
		)
	)
	_configure_combat_visual_contract(definition)
	name = "RetailBattalion_%d" % id
	_build_clip_map(capability)
	_drawable_scripts = (definition.get("drawableScripts", []) as Array).duplicate(true)
	if _drawable_scripts.is_empty():
		var visual_row: Variant = definition.get("visual", {})
		if typeof(visual_row) == TYPE_DICTIONARY:
			_drawable_scripts = ((visual_row as Dictionary).get("drawableScripts", []) as Array).duplicate(true)
	_bind_sub_object_upgrade_contracts_from_content(definition)
	_build_members(expected_members, formation_positions)
	_resolve_selection_radius()
	_configure_source_selection_decal(definition)
	_build_markers()
	set_action_state("idle", true)


func sync_mount_presentation(mounted: bool) -> void:
	## Tiny hook: when the sim marks the hero mounted, prefer the pack's
	## mounted-container-payload visual. If that recipe is absent, keep the
	## foot model and record the gap.
	if _mount_presentation_applied == mounted:
		return
	_mount_presentation_applied = mounted
	if not mounted:
		mount_visual_gap = ""
		return
	var playable: Dictionary = PlayableUnitAdapter.resolve_playable_document(
		ContentDB,
		{"object_id": object_id, "unit_type": object_id}
	)
	var mesh: Dictionary = PlayableUnitAdapter.authored_mounted_mesh(playable)
	if playable.is_empty():
		var definition: Dictionary = ContentDB.get_bundle_object(object_id)
		if not definition.is_empty():
			mesh = PlayableUnitAdapter.authored_mounted_mesh(definition)
			if mesh.is_empty():
				mesh = PlayableUnitAdapter.authored_mounted_mesh({"registration": definition})
	var gap := String(mesh.get("gap", "mounted-model-missing"))
	if String(mesh.get("path", "")).to_lower().ends_with(".glb"):
		mount_visual_gap = ""
		return
	if String(mesh.get("id", "")) != "" and gap == "":
		gap = "mounted-model-missing:%s" % String(mesh.get("id"))
	mount_visual_gap = gap if gap != "" else "mounted-model-missing"


func sync_banner_carrier(spawned: bool, banner_object_id: String, offset_source: Vector2) -> void:
	## Presentation-only projection of the authoritative horde contract. Missing
	## converted retail art stays invisible; never substitute the kit fallback.
	if not spawned or banner_object_id == "":
		if banner_carrier_visual != null and is_instance_valid(banner_carrier_visual):
			banner_carrier_visual.queue_free()
		banner_carrier_visual = null
		banner_carrier_object_id = ""
		return
	if (
		banner_carrier_visual != null
		and is_instance_valid(banner_carrier_visual)
		and banner_carrier_object_id == banner_object_id
	):
		return
	if banner_carrier_visual != null and is_instance_valid(banner_carrier_visual):
		banner_carrier_visual.queue_free()
	var asset_factory = load("res://src/view/asset_factory.gd")
	var visual: Node3D = asset_factory.make_bundle_object_visual(
		banner_object_id, team, _source_unit_scale
	)
	if visual == null or not bool(visual.get_meta("authored", false)):
		if visual != null:
			visual.queue_free()
		banner_carrier_visual = null
		banner_carrier_object_id = ""
		return
	visual.name = "BannerCarrier"
	var scale_factor := _source_unit_scale if _source_unit_scale > 0.0 else 1.0
	visual.position = Vector3(offset_source.x * scale_factor, 0.0, offset_source.y * scale_factor)
	visual.rotation.y = PI * 0.5
	add_child(visual)
	banner_carrier_visual = visual
	banner_carrier_object_id = banner_object_id
	_play_banner_clip(current_state if current_state != "" else "idle")


func sync_upgrade_visuals(applied_upgrades: Dictionary) -> void:
	## WeaponSetUpgrade still binds the flaming projectile. Sub-object
	## visibility prefers typed SubObjectsUpgrade rows; the FirePlane name
	## guess is only the loud fallback when no typed contract is attached.
	var visibility: Dictionary = apply_sub_object_upgrades(applied_upgrades)
	var conditions: Dictionary = apply_model_condition_upgrades(applied_upgrades)
	set_meta("model_condition_upgrade_source", String(conditions.get("source", "")))
	var upgrade_id := _fire_stones_upgrade_id(applied_upgrades)
	var wanted := upgrade_id != ""
	if wanted == fire_upgrade_active and fire_upgrade_id == upgrade_id:
		set_meta("fire_plane_nodes_shown", int(visibility.get("shown", 0)))
		set_meta("sub_object_upgrade_source", String(visibility.get("source", "")))
		return
	fire_upgrade_active = wanted
	fire_upgrade_id = upgrade_id
	if wanted:
		var flaming := _compiled_flaming_projectile_object_id()
		if flaming != "":
			projectile_object_id = flaming
	else:
		projectile_object_id = _compiled_projectile_object_id(object_id)
	set_meta("fire_upgrade_active", fire_upgrade_active)
	set_meta("fire_upgrade_id", fire_upgrade_id)
	set_meta("fire_plane_nodes_shown", int(visibility.get("shown", 0)))
	set_meta("fire_projectile_object_id", projectile_object_id)
	set_meta("sub_object_upgrade_source", String(visibility.get("source", "")))


func bind_sub_object_upgrade_contracts(source: Dictionary) -> int:
	## Attach compile-shaped SubObjectsUpgrade rows from a pack document.
	sub_object_upgrade_contracts = collect_sub_object_upgrade_contracts(source)
	return sub_object_upgrade_contracts.size()


func collect_sub_object_upgrade_contracts(source: Dictionary) -> Array:
	var out: Array = []
	for row_value in _module_contract_arrays(source):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		if String(row.get("module", "")) != "SubObjectsUpgrade":
			continue
		if not row.has("runtimeStatus") and row.has("runtime_status"):
			row["runtimeStatus"] = String(row.get("runtime_status", ""))
		out.append(row)
	return out


func bind_model_condition_upgrade_contracts(source: Dictionary) -> int:
	## Attach compile-shaped ModelConditionUpgrade rows from a pack document.
	model_condition_upgrade_contracts = collect_model_condition_upgrade_contracts(source)
	return model_condition_upgrade_contracts.size()


func collect_model_condition_upgrade_contracts(source: Dictionary) -> Array:
	var out: Array = []
	for row_value in _module_contract_arrays(source):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		if String(row.get("module", "")) != "ModelConditionUpgrade":
			continue
		if not row.has("runtimeStatus") and row.has("runtime_status"):
			row["runtimeStatus"] = String(row.get("runtime_status", ""))
		out.append(row)
	return out


func bind_animation_state_contracts(source: Dictionary) -> int:
	## Prefer typed AnimationState moduleContracts; current packs still ship
	## the same rows as visual.authoredAnimationStates until recook.
	animation_state_contracts = collect_animation_state_contracts(source)
	if animation_state_contracts.is_empty():
		animation_state_contracts = collect_authored_animation_states(source)
	return animation_state_contracts.size()


func bind_particle_sys_bone_contracts(source: Dictionary) -> int:
	particle_sys_bone_contracts = collect_particle_sys_bone_contracts(source)
	if particle_sys_bone_contracts.is_empty():
		particle_sys_bone_contracts = collect_pack_particle_attachments(source)
	return particle_sys_bone_contracts.size()


func collect_particle_sys_bone_contracts(source: Dictionary) -> Array:
	var out: Array = []
	for row_value in _module_contract_arrays(source):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		if String(row.get("module", "")) != "ParticleSysBone":
			continue
		if not row.has("runtimeStatus") and row.has("runtime_status"):
			row["runtimeStatus"] = String(row.get("runtime_status", ""))
		out.append(row)
	return out


func collect_pack_particle_attachments(source: Dictionary) -> Array:
	var out: Array = []
	for visual_value in _visual_documents(source):
		for row_value in (visual_value.get("particleAttachments", []) as Array):
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := (row_value as Dictionary).duplicate(true)
			if String(row.get("field", "ParticleSysBone")) != "ParticleSysBone":
				continue
			out.append(row)
	return out


func collect_animation_state_contracts(source: Dictionary) -> Array:
	var out: Array = []
	for row_value in _module_contract_arrays(source):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		if String(row.get("module", "")) != "AnimationState":
			continue
		if not row.has("runtimeStatus") and row.has("runtime_status"):
			row["runtimeStatus"] = String(row.get("runtime_status", ""))
		out.append(row)
	return out


func collect_authored_animation_states(source: Dictionary) -> Array:
	var out: Array = []
	for visual_value in _visual_documents(source):
		for row_value in (visual_value.get("authoredAnimationStates", []) as Array):
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := (row_value as Dictionary).duplicate(true)
			if String(row.get("runtimeSupport", "")).begins_with("excluded"):
				continue
			if String(row.get("identifier", "")) == "":
				continue
			out.append(row)
	return out


func _visual_documents(source: Dictionary) -> Array:
	var found: Array = []
	var registration: Dictionary = source.get("registration", {}) as Dictionary
	for raw_value in [
		source.get("visual", {}),
		registration.get("visual", {}),
		((registration.get("gameplay", {}) as Dictionary).get("visual", {})),
	]:
		if typeof(raw_value) == TYPE_DICTIONARY and not (raw_value as Dictionary).is_empty():
			found.append(raw_value)
	return found


func apply_model_condition_upgrades(applied_upgrades: Dictionary) -> Dictionary:
	## Grant/clear typed ModelConditionUpgrade flags. Temp-duration and
	## range-remove rows stay inert until a duration clock exists.
	var active: Dictionary = {}
	for key_value in applied_upgrades.keys():
		active[String(key_value).to_lower()] = true
	var granted: Dictionary = _permanent_model_condition_flags.duplicate()
	var source := "absent-typed-contract"
	if not model_condition_upgrade_contracts.is_empty():
		source = "typed-model-condition-upgrade"
	for contract_value in model_condition_upgrade_contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "ModelConditionUpgrade":
			continue
		if String(contract.get("extraction", "")) != "typed":
			continue
		if String(contract.get("runtimeStatus", contract.get("runtime_status", ""))) != "executable":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		if fields.has("deferredFields"):
			continue
		if not _sub_object_upgrade_triggers_match(fields, active):
			continue
		var permanent := bool((fields.get("Permanent", {}) as Dictionary).get("value", false))
		for flag_value in ((fields.get("AddConditionFlags", {}) as Dictionary).get("value", []) as Array):
			var flag := String(flag_value).to_upper()
			if flag == "":
				continue
			granted[flag] = true
			if permanent:
				_permanent_model_condition_flags[flag] = true
		for flag_value in ((fields.get("RemoveConditionFlags", {}) as Dictionary).get("value", []) as Array):
			var flag := String(flag_value).to_upper()
			granted.erase(flag)
			if permanent:
				_permanent_model_condition_flags.erase(flag)
	model_condition_flags = granted.keys()
	model_condition_flags.sort()
	set_meta("model_condition_flags", model_condition_flags.duplicate())
	set_meta("model_condition_upgrade_source", source)
	return {
		"source": source,
		"flags": model_condition_flags.duplicate(),
	}


func apply_sub_object_upgrades(applied_upgrades: Dictionary) -> Dictionary:
	## Execute typed SubObjectsUpgrade Show/Hide tokens. Deferred texture-swap
	## / HideOnRemove rows stay inert. Absent contracts fall back to FirePlane
	## and name that fallback on the receipt.
	var asset_factory = load("res://src/view/asset_factory.gd")
	var active: Dictionary = {}
	for key_value in applied_upgrades.keys():
		active[String(key_value).to_lower()] = true
	var executable: Array = []
	for contract_value in sub_object_upgrade_contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "SubObjectsUpgrade":
			continue
		if String(contract.get("extraction", "")) != "typed":
			continue
		if String(contract.get("runtimeStatus", contract.get("runtime_status", ""))) != "executable":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		if fields.has("deferredFields"):
			continue
		if _sub_object_upgrade_triggers_match(fields, active):
			executable.append(fields)
	if executable.is_empty() and not sub_object_upgrade_contracts.is_empty():
		var restored := _restore_last_sub_object_upgrades(asset_factory)
		return {"source": "typed-sub-objects-upgrade", "applied": 0, "show": [], "hide": [], "shown": restored}
	if executable.is_empty():
		var upgrade_id := _fire_stones_upgrade_id(applied_upgrades)
		var wanted := upgrade_id != ""
		var shown := 0
		for visual_value in member_visuals.values():
			if visual_value is Node:
				shown += int(asset_factory.set_named_subobject_visible(visual_value as Node, "FirePlane", wanted))
		return {
			"source": "hardcoded-fireplane-absent-typed-contract",
			"applied": 1 if wanted or shown > 0 else 0,
			"show": ["FirePlane"] if wanted else [],
			"hide": [],
			"shown": shown,
		}
	_restore_last_sub_object_upgrades(asset_factory)
	var show_tokens: Array = []
	var hide_tokens: Array = []
	for fields in executable:
		for token_value in ((fields.get("ShowSubObjects", {}) as Dictionary).get("value", []) as Array):
			var token := String(token_value)
			if token != "" and not show_tokens.has(token):
				show_tokens.append(token)
		for token_value in ((fields.get("HideSubObjects", {}) as Dictionary).get("value", []) as Array):
			var token := String(token_value)
			if token != "" and not hide_tokens.has(token):
				hide_tokens.append(token)
	var flipped := 0
	for visual_value in member_visuals.values():
		if not (visual_value is Node):
			continue
		for token in hide_tokens:
			flipped += int(asset_factory.set_named_subobject_visible(visual_value as Node, String(token), false))
		for token in show_tokens:
			flipped += int(asset_factory.set_named_subobject_visible(visual_value as Node, String(token), true))
	_last_sub_object_show = show_tokens.duplicate()
	_last_sub_object_hide = hide_tokens.duplicate()
	return {
		"source": "typed-sub-objects-upgrade",
		"applied": executable.size(),
		"show": show_tokens,
		"hide": hide_tokens,
		"shown": flipped,
	}


func _bind_sub_object_upgrade_contracts_from_content(definition: Dictionary) -> void:
	var playable: Dictionary = PlayableUnitAdapter.resolve_playable_document(
		ContentDB, {"object_id": object_id, "unit_type": object_id}
	)
	if not playable.is_empty():
		bind_sub_object_upgrade_contracts(playable)
		bind_model_condition_upgrade_contracts(playable)
		bind_animation_state_contracts(playable)
		bind_particle_sys_bone_contracts(playable)
	if sub_object_upgrade_contracts.is_empty():
		bind_sub_object_upgrade_contracts(definition)
	if model_condition_upgrade_contracts.is_empty():
		bind_model_condition_upgrade_contracts(definition)
	if animation_state_contracts.is_empty():
		bind_animation_state_contracts(definition)
	if particle_sys_bone_contracts.is_empty():
		bind_particle_sys_bone_contracts(definition)


func _module_contract_arrays(source: Dictionary) -> Array:
	var candidates: Array = []
	var registration: Dictionary = source.get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	if gameplay.is_empty() and typeof(source.get("gameplay")) == TYPE_DICTIONARY:
		gameplay = source.get("gameplay", {}) as Dictionary
	var simulation: Dictionary = gameplay.get("simulation", {}) as Dictionary
	if simulation.is_empty():
		simulation = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	for raw_value in [
		resolved.get("moduleContracts", []),
		gameplay.get("moduleContracts", []),
		registration.get("moduleContracts", []),
		source.get("moduleContracts", []),
	]:
		if typeof(raw_value) != TYPE_ARRAY:
			continue
		for row_value in raw_value as Array:
			candidates.append(row_value)
	return candidates


func _sub_object_upgrade_triggers_match(fields: Dictionary, active: Dictionary) -> bool:
	for conflict_value in ((fields.get("ConflictsWith", {}) as Dictionary).get("value", []) as Array):
		if active.has(String(conflict_value).to_lower()):
			return false
	var triggers: Array = ((fields.get("TriggeredBy", {}) as Dictionary).get("value", []) as Array)
	var requires_all := bool((fields.get("RequiresAllTriggers", {}) as Dictionary).get("value", false))
	if requires_all:
		if triggers.is_empty():
			return false
		for trigger_value in triggers:
			if not active.has(String(trigger_value).to_lower()):
				return false
		return true
	for trigger_value in triggers:
		if active.has(String(trigger_value).to_lower()):
			return true
	return false


func _restore_last_sub_object_upgrades(asset_factory: GDScript) -> int:
	var flipped := 0
	for visual_value in member_visuals.values():
		if not (visual_value is Node):
			continue
		for token in _last_sub_object_show:
			flipped += int(asset_factory.set_named_subobject_visible(visual_value as Node, String(token), false))
		for token in _last_sub_object_hide:
			flipped += int(asset_factory.set_named_subobject_visible(visual_value as Node, String(token), true))
	_last_sub_object_show.clear()
	_last_sub_object_hide.clear()
	return flipped


func fire_plane_visible_count() -> int:
	var count := 0
	for visual_value in member_visuals.values():
		if visual_value is Node:
			count += _count_visible_named(visual_value as Node, "FirePlane")
	return count


func _fire_stones_upgrade_id(applied_upgrades: Dictionary) -> String:
	for key_value in applied_upgrades.keys():
		var upgrade_id := String(key_value)
		if upgrade_id == "":
			continue
		var folded := upgrade_id.to_lower()
		if folded.contains("firestones") or folded.contains("fire_stones") or folded.contains("flamingmunitions"):
			return upgrade_id
	return ""


func _compiled_flaming_projectile_object_id() -> String:
	## weapon.ini:3257 GondorTrebuchetRockFlaming -> GondorTrebuchetRockProjectileFlaming
	for document_value in ContentDB.get_playable_unit_runtimes().values():
		var document := document_value as Dictionary
		if (
			PlayableUnitRuntimeAdapter.runtime_unit_id(document) != object_id
			and PlayableUnitRuntimeAdapter.runtime_member_id(document) != object_id
		):
			continue
		var gameplay := (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
		var resolved := (gameplay.get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary
		var combat: Dictionary = resolved.get("combat", {}) as Dictionary
		for upgrade_value in combat.get("upgrades", []) as Array:
			if typeof(upgrade_value) != TYPE_DICTIONARY:
				continue
			var upgrade := upgrade_value as Dictionary
			if String(upgrade.get("upgradeId", "")).to_lower().contains("firestone"):
				var weapon_id := String(upgrade.get("weaponId", ""))
				if weapon_id.to_lower().ends_with("flaming"):
					return weapon_id.replace("RockFlaming", "RockProjectileFlaming")
		return "GondorTrebuchetRockProjectileFlaming"
	return ""


func _count_visible_named(root: Node, token: String) -> int:
	var count := 0
	var want := token.to_lower().replace(" ", "")
	if root is Node3D:
		var folded := root.name.to_lower().replace(" ", "")
		if (folded == want or folded.begins_with(want)) and (root as Node3D).visible:
			count += 1
	for child in root.get_children():
		count += _count_visible_named(child, token)
	return count


func _play_banner_clip(state: String) -> void:
	## Banner carriers are a visual child, not a second battalion, so they never
	## went through set_action_state. Without this they stay in the GLB bind
	## pose — the T-pose the owner reported.
	if banner_carrier_visual == null or not is_instance_valid(banner_carrier_visual):
		return
	var semantic := "move" if state in ["run", "move"] else "idle"
	if state == "attack":
		semantic = "attack"
	elif state == "death":
		semantic = "death"
	var requested := _banner_clip_for_state(semantic)
	if requested == "":
		requested = "GUBanner_IDLB" if semantic != "move" else "GUBanner_RUNA"
	for player in _animation_players_under(banner_carrier_visual):
		var playable := _resolve_animation_name(player, requested)
		if playable == "":
			continue
		if player.current_animation == playable and player.is_playing():
			continue
		player.play(playable, 0.12)
		if semantic in ["idle", "move"] and player.current_animation_length > 0.0:
			player.seek(player.current_animation_length * 0.15, true)


func _banner_clip_for_state(semantic: String) -> String:
	var capability_id := String(banner_carrier_visual.get_meta("animation_capability_id", ""))
	if capability_id == "":
		capability_id = "playable-unit:gondorinfantrybanner"
	var capability: Dictionary = ContentDB.get_animation_capability(capability_id)
	var states: Dictionary = capability.get("states", {}) as Dictionary
	var row: Dictionary = states.get(semantic, states.get("idle", {})) as Dictionary
	var clips: Array = row.get("clips", []) as Array
	if clips.is_empty():
		return ""
	return String(clips[0])


func _animation_players_under(root: Node) -> Array[AnimationPlayer]:
	var players: Array[AnimationPlayer] = []
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		if node is AnimationPlayer:
			players.append(node as AnimationPlayer)
		for child in node.get_children():
			stack.append(child)
	return players


func sync_ring_carrier(holding: bool, ring_object_id: String, offset_source: Vector2) -> void:
	## Presentation-only attachment. Like the banner carrier, it consumes only
	## authored pack art and follows the authoritative battalion node.
	if not holding or ring_object_id == "":
		if ring_carrier_visual != null and is_instance_valid(ring_carrier_visual):
			ring_carrier_visual.queue_free()
		ring_carrier_visual = null
		ring_carrier_object_id = ""
		return
	if ring_carrier_visual != null and is_instance_valid(ring_carrier_visual) \
			and ring_carrier_object_id == ring_object_id:
		return
	if ring_carrier_visual != null and is_instance_valid(ring_carrier_visual):
		ring_carrier_visual.queue_free()
	var asset_factory = load("res://src/view/asset_factory.gd")
	var visual: Node3D = asset_factory.make_bundle_object_visual(ring_object_id, team, _source_unit_scale)
	if visual == null or not bool(visual.get_meta("authored", false)):
		if visual != null:
			visual.queue_free()
		return
	visual.name = "RingCarrier"
	var scale_factor := _source_unit_scale if _source_unit_scale > 0.0 else 1.0
	visual.position = Vector3(offset_source.x * scale_factor, 0.0, offset_source.y * scale_factor)
	add_child(visual)
	ring_carrier_visual = visual
	ring_carrier_object_id = ring_object_id


func _build_clip_map(capability: Dictionary) -> void:
	var states: Dictionary = capability.get("states", {}) as Dictionary
	clip_sets = {
		"idle": _clips(states.get("idle", {})),
		"run": _clips(states.get("move", {})),
		"attack": _clips(states.get("attack", {})),
		"attack_melee": _clips(states.get("attackMelee", states.get("attack", {}))),
		"attack_ranged_pre": _clips(states.get("attackRangedPre", states.get("attack", {}))),
		"attack_ranged_fire": _clips(states.get("attackRangedFire", states.get("attack", {}))),
		"death": _clips(states.get("death", {})),
		"construct": _clips(states.get("construct", {})),
		"victory": _clips(states.get("idle", {})),
		"selectionTransition": _clips(states.get("selectionTransition", {})),
		"selected": _clips(states.get("selected", {})),
	}
	clip_modes.clear()
	authored_animation_speed_ranges.clear()
	authored_animation_clip_properties.clear()
	authored_state_label_links.clear()
	member_current_authored_state_labels.clear()
	member_previous_authored_state_labels.clear()
	for state_name in ["idle", "move", "attack", "death", "selectionTransition", "selected"]:
		var state_value: Variant = states.get(state_name, {})
		if typeof(state_value) == TYPE_DICTIONARY:
			clip_modes[state_name] = String((state_value as Dictionary).get("mode", "loop"))
	for route in {
		"idle": "idle", "run": "move", "attack": "attack",
		"attack_melee": "attackMelee", "attack_ranged_pre": "attackRangedPre",
		"attack_ranged_fire": "attackRangedFire", "death": "death",
		"selectionTransition": "selectionTransition", "selected": "selected",
	}.keys():
		var capability_state := String({
			"idle": "idle", "run": "move", "attack": "attack",
			"attack_melee": "attackMelee", "attack_ranged_pre": "attackRangedPre",
			"attack_ranged_fire": "attackRangedFire", "death": "death",
			"selectionTransition": "selectionTransition", "selected": "selected",
		}[route])
		var row: Dictionary = states.get(capability_state, states.get("attack", {}) if String(route).begins_with("attack") else {}) as Dictionary
		var ranges: Dictionary = row.get("AnimationSpeedFactorRange", {}) as Dictionary
		for clip_value in ranges.keys():
			authored_animation_speed_ranges["%s|%s" % [String(route), String(clip_value)]] = (ranges[clip_value] as Array).duplicate()
		var properties: Variant = row.get("clipProperties", {})
		if typeof(properties) == TYPE_DICTIONARY:
			for clip_value in (properties as Dictionary).keys():
				var property_value: Variant = (properties as Dictionary)[clip_value]
				if typeof(property_value) == TYPE_DICTIONARY:
					authored_animation_clip_properties["%s|%s" % [String(route), String(clip_value)]] = (property_value as Dictionary).duplicate(true)
	var labels_value: Variant = capability.get("authoredStateLabels", [])
	if typeof(labels_value) == TYPE_ARRAY:
		for label_value in labels_value as Array:
			if typeof(label_value) != TYPE_DICTIONARY:
				continue
			var label_row := label_value as Dictionary
			var label := String(label_row.get("StateName", ""))
			var linked_value: Variant = label_row.get("linkedAnimations", [])
			if label == "" or typeof(linked_value) != TYPE_ARRAY:
				continue
			for linked_row_value in linked_value as Array:
				if typeof(linked_row_value) != TYPE_DICTIONARY:
					continue
				var identifier := String((linked_row_value as Dictionary).get("identifier", ""))
				if identifier == "":
					continue
				if not authored_state_label_links.has(identifier):
					authored_state_label_links[identifier] = []
				var linked_labels := authored_state_label_links[identifier] as Array
				if not linked_labels.has(label):
					linked_labels.append(label)
	for identifier_value in authored_state_label_links.keys():
		(authored_state_label_links[identifier_value] as Array).sort_custom(
			func(a: Variant, b: Variant) -> bool: return String(a).naturalnocasecmp_to(String(b)) < 0
		)
	attack_uses_weapon_timing = bool((states.get("attack", {}) as Dictionary).get("useWeaponTiming", false))
	equipment_contract = (capability.get("equipment", {}) as Dictionary).duplicate(true)
	equipment_contract_ready = bool(equipment_contract.get("validated", false))
	unresolved_animation_track_count = int(capability.get("unresolvedAnimationTracks", 0))
	clip_map.clear()
	for state in clip_sets.keys():
		var values: Array = clip_sets[state]
		clip_map[state] = String(values[0]) if not values.is_empty() else ""


func _clips(value: Variant) -> Array[String]:
	var result: Array[String] = []
	if typeof(value) != TYPE_DICTIONARY:
		return result
	var clips: Variant = (value as Dictionary).get("clips", [])
	if typeof(clips) != TYPE_ARRAY:
		return result
	for clip in clips as Array:
		var name := String(clip)
		if name != "" and not result.has(name):
			result.append(name)
	return result


func _build_members(expected_members: int, formation_positions: Array) -> void:
	var asset_factory = load("res://src/view/asset_factory.gd")
	# One authored body radius for the whole battalion: every member of a horde is
	# the same retail Object, so their bars are identical by construction and a
	# horde can never again render as a ragged row of different-sized slabs.
	var bar_width := member_bar_width(_member_source_geometry_radius(), _source_unit_scale)
	member_health_bar_source = (
		"compiled-geometry" if _member_geometry_is_compiled else "retail-infantry-default"
	)
	var source_geometry_height := float(SOURCE_HEALTH_GEOMETRY_HEIGHT.get(object_id, 19.2))
	var shared_health_anchor_height := (
		(source_geometry_height + SOURCE_HEALTH_HEIGHT_OFFSET) * _source_unit_scale
		if _source_unit_scale > 0.0
		else 1.8
	)
	for index in range(expected_members):
		var visual: Node3D = asset_factory.make_bundle_object_visual(object_id, team, _source_unit_scale)
		if visual == null:
			continue
		var member_index := member_count
		var authored_slot: Variant = formation_positions[member_index] if member_index < formation_positions.size() else Vector3.ZERO
		visual.position = authored_slot if typeof(authored_slot) == TYPE_VECTOR3 else Vector3.ZERO
		member_formation_slots[member_index] = visual.position
		# Every imported member keeps the same model-space forward axis. Battalion
		# root yaw is updated from the authoritative route/target direction.
		# Retail infantry meshes are authored facing the opposite X axis from the
		# mount convention used by the Godot battalion root. The old -90 degree
		# correction left every imported member 180 degrees behind its authoritative
		# travel direction; +90 degrees keeps the mesh nose aligned with root -Z.
		visual.rotation.y = PI * 0.5
		add_child(visual)
		member_count += 1
		member_visuals[member_index] = visual
		member_health_ratios[member_index] = 1.0
		# THE BAR RIDES THE MODEL, NOT A TABLE.
		#
		# The old anchor was `(SOURCE_HEALTH_GEOMETRY_HEIGHT + 10) * scale` above
		# the member's ORIGIN: for a Gondor fighter that is 2.92 world units over
		# a soldier the same table says is 1.92 tall, so every bar floated half a
		# body height over its owner's head (owner-visible "slabs above units").
		# The structure presenter had the same class of bug and was fixed by
		# measuring the transformed model instead (retail_structure.gd
		# `_sync_health_bar_geometry`, `_visual_top_y + HEALTH_BAR_ROOF_GAP`).
		# Members are measured the same way, and the table survives only as the
		# fallback for geometry that cannot be measured.
		var measured := _measure_member_bounds(visual)
		# The ANCHOR is measured (it must ride this model); the WIDTH is authored
		# and shared, so both presenters draw the same bar for the same unit.
		member_health_half_widths[member_index] = bar_width * 0.5
		if measured.size.y > 0.0:
			member_health_anchor_heights[member_index] = measured.end.y + HEALTH_BAR_HEAD_GAP
			member_visual_centers[member_index] = Vector3(measured.get_center().x, 0.0, measured.get_center().z)
			member_health_measured_count += 1
		else:
			member_health_anchor_heights[member_index] = shared_health_anchor_height
			member_visual_centers[member_index] = Vector3.ZERO
			member_health_fallback_count += 1
		_publish_anchor_source()
		# Retail contact shadow: SAGE draws infantry shadows as dark decals at
		# shadow color ARGB(64,0,0,0); the blob decal is the approved
		# equivalence and travels/frees with the member visual.
		var member_height := maxf(source_geometry_height * _source_unit_scale, 0.2)
		var member_shadow := ShadowDecalScript.create(Vector2(member_height * 0.75, member_height * 0.75), member_height * 2.0)
		visual.add_child(member_shadow)
		member_attack_tokens[member_index] = 0
		member_attack_release_tokens[member_index] = 0
		member_action_states[member_index] = "idle"
		if private_parity_mode_active:
			member_overlay_status = "source-health-canvas-bound-selection-decal-contract-pending"
		else:
			_build_member_overlay(member_index, visual.position, measured, bar_width)
			member_overlay_status = "legal-safe-gameplay-overlay"
		if (
			bool(visual.get_meta("authored", false))
			and String(visual.get_meta("content_object_id", "")) == object_id
			and retail_model_filename != ""
			and String(visual.get_meta("mesh_path", "")).replace("\\", "/").get_file() == retail_model_filename
		):
			retail_visual_count += 1
		if bool(visual.get_meta("has_skeleton", false)):
			rigged_member_count += 1
		team_tinted_surface_count += int(visual.get_meta("team_tinted_surfaces", 0))
		house_color_surface_count += int(visual.get_meta("house_color_surfaces", 0))
		if team_color_status == "":
			team_color_status = String(visual.get_meta("team_color_status", ""))
		member_animation_players[member_index] = []
		_collect_animation_players(visual, member_index)


func _publish_anchor_source() -> void:
	var fallback_name := "source-geometry-table" if _source_unit_scale > 0.0 else "unscaled-default"
	if member_health_fallback_count == 0:
		member_health_anchor_source = "measured-visual-bounds"
	elif member_health_measured_count == 0:
		member_health_anchor_source = fallback_name
	else:
		member_health_anchor_source = "partial-measured-visual-bounds-%d-of-%d-fell-back-to-%s" % [
			member_health_fallback_count,
			member_health_measured_count + member_health_fallback_count,
			fallback_name,
		]


func _measure_member_bounds(visual: Node3D) -> AABB:
	## The member's drawn extent in ITS OWN space, walked with local transforms so
	## the measurement is valid before the battalion enters the scene tree (the
	## slice configures battalions off-tree). Returns a zero AABB when the member
	## carries no mesh yet, which is the caller's signal to fall back.
	var bounds := AABB()
	var measured := false
	var pending: Array = [[visual, Transform3D.IDENTITY]]
	while not pending.is_empty():
		var entry: Array = pending.pop_back()
		var node := entry[0] as Node
		var to_visual := entry[1] as Transform3D
		if node is MeshInstance3D and (node as MeshInstance3D).mesh != null:
			var mesh_bounds := to_visual * (node as MeshInstance3D).get_aabb()
			bounds = bounds.merge(mesh_bounds) if measured else mesh_bounds
			measured = true
		for child_value in node.get_children():
			if child_value is Node3D:
				pending.append([child_value, to_visual * (child_value as Node3D).transform])
			elif child_value is Node:
				pending.append([child_value, to_visual])
	return bounds if measured else AABB()


func _resolve_selection_radius() -> void:
	## Retail selects a horde by clicking one of its members, so the pickable
	## footprint is the formation spread plus a single member's authored
	## Geometry body -- not a fixed world-unit circle.
	var member_source_radius := _member_source_geometry_radius()
	var member_radius := SelectionPick.world_radius_from_source(member_source_radius, _source_unit_scale)
	if member_radius <= 0.0:
		member_radius = SelectionPick.MINIMUM_SELECTION_RADIUS
		selection_radius_source = "unscaled-member-default"
	_member_selection_radius = member_radius
	pick_radius = maxf(SelectionPick.MINIMUM_SELECTION_RADIUS, _member_spread() + member_radius)
	if selection_radius_source == "unresolved":
		selection_radius_source = (
			"formation-spread-and-compiled-geometry"
			if _member_geometry_is_compiled
			else "formation-spread-and-source-default"
		)


var _member_geometry_is_compiled := false
var _member_selection_radius := SelectionPick.MINIMUM_SELECTION_RADIUS


func _member_spread() -> float:
	## Furthest live member from the battalion origin, in world units. Members
	## are re-laid-out every frame by the formation controller, so the authored
	## slots alone would under-report a marching or engaged horde.
	var spread := 0.0
	for visual_value in member_visuals.values():
		if visual_value == null or not is_instance_valid(visual_value):
			continue
		var visual := visual_value as Node3D
		spread = maxf(spread, Vector2(visual.position.x, visual.position.z).length())
	if spread > 0.0:
		return spread
	for slot_value in member_formation_slots.values():
		if typeof(slot_value) != TYPE_VECTOR3:
			continue
		var slot := slot_value as Vector3
		spread = maxf(spread, Vector2(slot.x, slot.z).length())
	return spread


func selection_radius() -> float:
	## Live picking footprint. Recomputed on demand (a click, not a frame) so a
	## spread-out horde stays clickable across its whole silhouette.
	pick_radius = maxf(
		SelectionPick.MINIMUM_SELECTION_RADIUS, _member_spread() + _member_selection_radius
	)
	return pick_radius


func member_pick_candidates(entity_id: int) -> Array:
	## One pick candidate per LIVING member, at the member's own position with
	## its own authored Geometry radius.
	##
	## Retail hit-tests the click against each object member's Geometry cylinder
	## (gondorarcher.ini:796-799 authors MajorRadius 8.0 for one archer), so the
	## pickable silhouette of a horde is the union of its members' bodies. The
	## battalion-level `selection_radius()` (formation spread + one member body,
	## centred on the horde root) is the bounding DISC of that union: on a
	## 15-member archer line it lights the attack cursor over the gaps between
	## ranks and well past the outermost silhouette - the owner's "hovering far
	## from an enemy still lights the attack cursor". This is the union itself.
	##
	## Returns an empty array when no member visuals exist yet (spawn frame,
	## headless seams); callers keep the battalion-level fallback for that case.
	##
	## The radius is the authored Geometry body (`_member_selection_radius`),
	## not `MINIMUM_SELECTION_RADIUS`. That floor exists so a horde-level disc
	## stays clickable; applying it per member would inflate a thin unit
	## (archer MajorRadius 8.0 -> 0.21 world at Fords) up to 0.32 and miss
	## the owner's "just around their hitbox". `_resolve_selection_radius`
	## already substitutes the floor when authored geometry is missing.
	var radius := _member_selection_radius
	var candidates: Array = []
	for member_index in member_visuals.keys():
		if float(member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		var visual_value = member_visuals[member_index]
		if visual_value == null or not is_instance_valid(visual_value):
			continue
		var world := (visual_value as Node3D).global_position
		candidates.append({
			"id": entity_id,
			"position": Vector2(world.x, world.z),
			"radius": radius,
		})
	return candidates


func _member_source_geometry_radius() -> float:
	## Compiled retail Geometry from the selected pack's playable-unit document
	## when present; otherwise the retail infantry default (MajorRadius 8.0).
	_member_geometry_is_compiled = false
	if not ContentDB.has_method("get_playable_unit_runtime"):
		return SelectionPick.DEFAULT_MEMBER_SOURCE_RADIUS
	# THE MEMBER IS NOT THE HORDE.
	#
	# A playable-unit document registers under its HORDE id
	# (`GondorFighterHorde`) while the battalion carries a MEMBER bundle slug
	# whose definition records the member's retail source (`GondorFighter`). So
	# `get_playable_unit_runtime(source_object_id)` missed on EVERY unit in the
	# game and every one of them silently took the infantry default 8.0 -
	# discarding, among others, Knights of Dol Amroth's authored majorRadius 10.
	# ContentDB now indexes each document by the member it describes; the
	# horde-id lookups stay as fallbacks for callers that hold a horde slug.
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	var source_object_id := String(definition.get("sourceObjectId", ""))
	var documents: Array = []
	if ContentDB.has_method("get_playable_unit_runtime_for_member") and source_object_id != "":
		documents.append(ContentDB.get_playable_unit_runtime_for_member(source_object_id))
	for candidate in [source_object_id, object_id]:
		var id := String(candidate)
		if id == "":
			continue
		documents.append(ContentDB.get_playable_unit_runtime(id))
	for document_value in documents:
		var document: Variant = document_value
		if typeof(document) != TYPE_DICTIONARY:
			continue
		var registration: Dictionary = (document as Dictionary).get("registration", {}) as Dictionary
		var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
		var geometry: Variant = gameplay.get("geometry", {})
		if typeof(geometry) != TYPE_DICTIONARY:
			continue
		var resolved := SelectionPick.source_footprint_radius(geometry as Dictionary)
		if resolved > 0.0:
			_member_geometry_is_compiled = true
			return resolved
	return SelectionPick.DEFAULT_MEMBER_SOURCE_RADIUS


static func member_bar_width(source_radius: float, source_unit_scale: float) -> float:
	## Drawn width of one member's health bar, in world units, from the unit's
	## AUTHORED source footprint radius. Weapon-free by construction: the value
	## comes from the compiled `Geometry` block, never from a mesh AABB.
	if not is_finite(source_unit_scale) or source_unit_scale <= 0.0:
		return MEMBER_BAR_FALLBACK_WIDTH
	var radius := (
		source_radius
		if is_finite(source_radius) and source_radius > 0.0
		else SelectionPick.DEFAULT_MEMBER_SOURCE_RADIUS
	)
	radius = clampf(radius, MEMBER_BAR_MINIMUM_SOURCE_RADIUS, MEMBER_BAR_MAXIMUM_SOURCE_RADIUS)
	return MEMBER_BAR_WIDTH_PER_SOURCE_RADIUS * radius * source_unit_scale


static func member_bar_thickness(source_unit_scale: float, aspect: float) -> float:
	## Bar THICKNESS is the same for every member on the map. It used to be
	## `width * aspect`, so a wide bar was also a fat bar and the two errors
	## multiplied. Deriving it from the DEFAULT infantry radius alone means a
	## troll's slightly wider bar is no thicker than a soldier's.
	if not is_finite(source_unit_scale) or source_unit_scale <= 0.0:
		return MEMBER_BAR_FALLBACK_WIDTH * aspect
	return (
		MEMBER_BAR_WIDTH_PER_SOURCE_RADIUS
		* SelectionPick.DEFAULT_MEMBER_SOURCE_RADIUS
		* source_unit_scale
		* aspect
	)


func _member_selection_anchor(member_index: int) -> Vector3:
	var visual := member_visuals.get(member_index) as Node3D
	var offset: Vector3 = member_visual_centers.get(member_index, Vector3.ZERO)
	if visual == null or not is_instance_valid(visual):
		return offset
	## member_visual_centers is the mesh AABB centre in the member visual's
	## local space. Every imported member is yawed PI/2 in _build_members
	## (and again when the formation heading is applied), so adding the local
	## offset to visual.position parks the ring a body-width to the side.
	## Rotate the offset through the live basis first.
	return visual.position + visual.basis * offset


func _ensure_synthetic_member_selection_rings() -> void:
	## Fallback ring under every living member when the SHADOW_MERGE_DECAL
	## contract is missing or failed to bind. Sized to the authored Geometry
	## body, centred on the live visual — never a giant battalion torus.
	for member_index in range(member_count):
		if member_selection_rings.has(member_index):
			continue
		var visual := member_visuals.get(member_index) as Node3D
		if visual == null or not is_instance_valid(visual):
			continue
		var selection_ring := MeshInstance3D.new()
		selection_ring.name = "MemberSelection_%02d" % member_index
		var ring_mesh := TorusMesh.new()
		var radius := maxf(0.12, _member_selection_radius)
		ring_mesh.inner_radius = maxf(0.08, radius * 0.85)
		ring_mesh.outer_radius = radius
		ring_mesh.rings = 24
		ring_mesh.ring_segments = 6
		selection_ring.mesh = ring_mesh
		var anchor := _member_selection_anchor(member_index)
		selection_ring.position = Vector3(anchor.x, 0.045, anchor.z)
		var ring_material := StandardMaterial3D.new()
		ring_material.albedo_color = Color(0.22, 0.82, 0.30, 0.72)
		ring_material.emission_enabled = true
		ring_material.emission = Color(0.035, 0.16, 0.05)
		ring_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		ring_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		selection_ring.material_override = ring_material
		selection_ring.visible = false
		add_child(selection_ring)
		member_selection_rings[member_index] = selection_ring


func _build_member_overlay(member_index: int, member_position: Vector3, measured_bounds: AABB = AABB(), bar_width: float = MEMBER_BAR_FALLBACK_WIDTH) -> void:
	# These are gameplay overlays rather than claimed retail art. Their scale is
	# matched to the source member radius; final texture/color parity remains an
	# oracle styling gate.
	var selection_ring := MeshInstance3D.new()
	selection_ring.name = "MemberSelection_%02d" % member_index
	var ring_mesh := TorusMesh.new()
	ring_mesh.inner_radius = 0.26
	ring_mesh.outer_radius = 0.32
	ring_mesh.rings = 24
	ring_mesh.ring_segments = 6
	selection_ring.mesh = ring_mesh
	selection_ring.position = member_position + Vector3(0.0, 0.045, 0.0)
	var ring_material := StandardMaterial3D.new()
	ring_material.albedo_color = Color(0.22, 0.82, 0.30, 0.72)
	ring_material.emission_enabled = true
	ring_material.emission = Color(0.035, 0.16, 0.05)
	ring_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	selection_ring.material_override = ring_material
	selection_ring.visible = false
	add_child(selection_ring)
	member_selection_rings[member_index] = selection_ring

	# THE BAR SITS JUST OVER HIS HEAD, SIZED BY HIS AUTHORED BODY.
	#
	# It used to be a fixed 0.58x0.065 quad parked at a fixed y=1.45 above the
	# member's authored formation slot - the owner-visible "slabs floating above
	# units". The anchor was then moved onto the member's MEASURED visual top,
	# which is right, and the WIDTH was moved there too, which was not: the mesh
	# AABB contains the man's spear, so a Tower Guard's bar came out several
	# times wider and (through the aspect) several times thicker than the Gondor
	# Fighter's beside him. Width and thickness now come from the authored
	# `Geometry` radius (see MEMBER_BAR_WIDTH_PER_SOURCE_RADIUS); only the anchor
	# is measured.
	var bar_anchor_height := 1.45
	if measured_bounds.size.y > 0.0:
		bar_anchor_height = measured_bounds.end.y + HEALTH_BAR_HEAD_GAP
	member_health_bar_widths[member_index] = bar_width
	member_health_bar_heights[member_index] = bar_anchor_height
	var back_thickness := member_bar_thickness(_source_unit_scale, BAR_BACK_ASPECT)
	var fill_thickness := member_bar_thickness(_source_unit_scale, BAR_FILL_ASPECT)
	var health_back := MeshInstance3D.new()
	health_back.name = "MemberHealthBack_%02d" % member_index
	var back_quad := QuadMesh.new()
	back_quad.size = Vector2(bar_width, back_thickness)
	health_back.mesh = back_quad
	health_back.position = member_position + Vector3(0.0, bar_anchor_height, 0.0)
	var back_material := StandardMaterial3D.new()
	back_material.albedo_color = Color(0.025, 0.035, 0.04, 0.94)
	back_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	back_material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	back_material.no_depth_test = true
	back_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	health_back.material_override = back_material
	health_back.visible = false
	add_child(health_back)
	member_health_backs[member_index] = health_back

	var health_fill := MeshInstance3D.new()
	health_fill.name = "MemberHealthFill_%02d" % member_index
	var fill_quad := QuadMesh.new()
	fill_quad.size = Vector2(bar_width * BAR_FILL_WIDTH_RATIO, fill_thickness)
	health_fill.mesh = fill_quad
	health_fill.position = member_position + Vector3(0.0, bar_anchor_height, -0.01)
	var fill_material := StandardMaterial3D.new()
	fill_material.albedo_color = Color(0.18, 0.86, 0.24, 0.98)
	fill_material.emission_enabled = true
	fill_material.emission = Color(0.025, 0.12, 0.035)
	fill_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	fill_material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	fill_material.no_depth_test = true
	fill_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	health_fill.material_override = fill_material
	health_fill.visible = false
	add_child(health_fill)
	member_health_fills[member_index] = health_fill


func _collect_animation_players(node: Node, member_index: int) -> void:
	if node is AnimationPlayer:
		var player := node as AnimationPlayer
		animation_players.append(player)
		var players: Array = member_animation_players.get(member_index, [])
		players.append(player)
		member_animation_players[member_index] = players
		animation_player_count += 1
	for child in node.get_children():
		_collect_animation_players(child, member_index)


func _build_markers() -> void:
	# The old rings and health bar are repository-authored debug presentation,
	# not BFME2 retail art. The selected private parity pack therefore fails
	# closed and renders none of them — except a selection ring, which every
	# selectable battalion must show. Heroes / porter often ship neither the
	# Icon02 mesh nor a bound SHADOW_MERGE_DECAL; failing into "nothing" is
	# the owner's missing circle.
	if source_selection_decal == null or not source_selection_decal.contract_ready:
		_ensure_synthetic_member_selection_rings()
	if private_parity_mode_active:
		return
	if source_selection_decal != null and source_selection_decal.contract_ready:
		# The retail squiggly decal bound from this faction's own pack: the big
		# synthetic team/selection tori are the men-less fallback and must not
		# double-draw over it (the elven porter's giant blue/green ellipses).
		return
	_team_ring = MeshInstance3D.new()
	_team_ring.name = "SyntheticTeamRing"
	var team_ring_mesh := TorusMesh.new()
	team_ring_mesh.inner_radius = 3.95
	team_ring_mesh.outer_radius = 4.08
	team_ring_mesh.rings = 28
	team_ring_mesh.ring_segments = 6
	_team_ring.mesh = team_ring_mesh
	_team_ring.position.y = 0.05
	var team_ring_material := StandardMaterial3D.new()
	team_ring_material.albedo_color = Color(0.20, 0.58, 1.0, 0.88) if team == 0 else Color(1.0, 0.24, 0.20, 0.88)
	team_ring_material.emission_enabled = true
	team_ring_material.emission = team_ring_material.albedo_color * 0.42
	team_ring_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	team_ring_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_team_ring.material_override = team_ring_material
	add_child(_team_ring)

	_selection_ring = MeshInstance3D.new()
	_selection_ring.name = "SyntheticSelectionRing"
	var ring := TorusMesh.new()
	ring.inner_radius = 4.18
	ring.outer_radius = 4.42
	ring.rings = 24
	ring.ring_segments = 8
	_selection_ring.mesh = ring
	_selection_ring.position.y = 0.09
	var ring_material := StandardMaterial3D.new()
	ring_material.albedo_color = Color(0.25, 1.0, 0.45, 0.92)
	ring_material.emission_enabled = true
	ring_material.emission = Color(0.12, 0.8, 0.25)
	ring_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_selection_ring.material_override = ring_material
	_selection_ring.visible = false
	add_child(_selection_ring)

	var health_back := MeshInstance3D.new()
	health_back.name = "SyntheticHealthBack"
	var back_mesh := BoxMesh.new()
	back_mesh.size = Vector3(2.6, 0.16, 0.10)
	health_back.mesh = back_mesh
	health_back.position = Vector3(0, 3.05, 0)
	var back_material := StandardMaterial3D.new()
	back_material.albedo_color = Color("10161a")
	back_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	health_back.material_override = back_material
	add_child(health_back)

	_team_marker = MeshInstance3D.new()
	_team_marker.name = "SyntheticHealthMarker"
	var marker := BoxMesh.new()
	marker.size = Vector3(2.5, 0.12, 0.08)
	_team_marker.mesh = marker
	_team_marker.position = Vector3(0, 3.06, -0.02)
	var marker_material := StandardMaterial3D.new()
	marker_material.albedo_color = Color("4da3ff") if team == 0 else Color("ee554d")
	marker_material.emission_enabled = true
	marker_material.emission = marker_material.albedo_color * 0.45
	_team_marker.material_override = marker_material
	add_child(_team_marker)

	_status_label = Label3D.new()
	_status_label.name = "SyntheticStatusLabel"
	_status_label.position = Vector3(0, 3.5, 0)
	_status_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_status_label.font_size = 26
	_status_label.outline_size = 6
	_status_label.modulate = Color("b9dcff") if team == 0 else Color("ffb4af")
	_status_label.visible = false
	add_child(_status_label)
	_refresh_label()


func set_selected(value: bool) -> void:
	var was_selected := selected
	selected = value and health_ratio > 0.0
	if _selection_ring != null:
		_selection_ring.visible = selected and production_exit_progress >= 1.0
	if source_selection_decal != null:
		source_selection_decal.set_selected(selected and production_exit_progress >= 1.0)
	if selected and not was_selected and current_state == "idle":
		# Retail (gondorfighter.ini:519-531 / :664-670): SELECTED BeginScript
		# decides whether Prev == STATE_Idle plays TRANS_IdleToSelected.
		# `_play_member_state` runs those scripts; the compiled ATNA map is
		# only the authored-script-absent fallback.
		for member_index in range(member_count):
			if float(member_health_ratios.get(member_index, 0.0)) <= 0.0:
				continue
			if String(member_action_states.get(member_index, "idle")) != "idle":
				continue
			if String(clip_map.get("selected", "")) != "" or String(clip_map.get("selectionTransition", "")) != "":
				_play_member_state(member_index, "selected", -1, true)
	elif not selected and was_selected:
		for member_index in range(member_count):
			var action := String(member_action_states.get(member_index, ""))
			if action == "selectionTransition" or action == "selected":
				_play_member_state(member_index, "idle", -1, true)
	_refresh_member_overlays()


func set_health(current: int, maximum: int) -> void:
	health_ratio = clampf(float(current) / float(maxi(1, maximum)), 0.0, 1.0)
	var living := health_ratio > 0.0
	if not living:
		selected = false
	if _team_ring != null:
		_team_ring.visible = living
	if _selection_ring != null:
		_selection_ring.visible = living and selected and production_exit_progress >= 1.0
	if source_selection_decal != null:
		source_selection_decal.set_selected(living and selected and production_exit_progress >= 1.0)
	if _team_marker != null:
		_team_marker.visible = living
		_team_marker.scale.x = maxf(0.001, health_ratio)
		_team_marker.position.x = (health_ratio - 1.0) * 1.25
	_refresh_label()
	_refresh_member_overlays()


func set_production_exit_progress(value: float) -> void:
	var normalized := clampf(value, 0.0, 1.0) if is_finite(value) else 1.0
	var restarting := normalized + 0.0001 < production_exit_progress
	production_exit_progress = normalized
	for member_index in range(member_count):
		var visual := member_visuals.get(member_index) as Node3D
		if visual == null:
			continue
		var reveal_threshold := float(member_index + 1) / float(maxi(1, member_count))
		var should_reveal := normalized >= reveal_threshold or normalized >= 0.9999
		if restarting:
			visual.position = Vector3.ZERO
			visual.visible = false
		if should_reveal and not visual.visible:
			# Newly revealed retail members begin at the doorway/root and spread into
			# their battalion slots instead of materializing in a finished formation.
			visual.position = Vector3.ZERO
		visual.visible = should_reveal
	if source_selection_decal != null:
		source_selection_decal.set_selected(selected and normalized >= 1.0)
	_refresh_member_overlays()


func emerged_member_count() -> int:
	var result := 0
	for member_index in range(member_count):
		var visual := member_visuals.get(member_index) as Node3D
		if visual != null and visual.visible:
			result += 1
	return result


func sync_member_states(
	health_values: Array,
	member_maximum_health: int,
	attack_tokens: Array,
	battalion_state: String,
	release_tokens: Array = [],
	weapon_modes: Array = [],
	target_globals: Array = [],
	corpse_expire_ticks: Array = [],
	authoritative_tick: int = 0
) -> void:
	var maximum := maxi(1, member_maximum_health)
	var normalized_state := battalion_state if clip_map.has(battalion_state) else "idle"
	var battalion_state_changed := normalized_state != current_state
	current_state = normalized_state
	current_clip = String(clip_map.get(normalized_state, ""))
	if battalion_state_changed:
		_sync_selection_layout(normalized_state)
	for member_index in range(member_count):
		var prior_ratio := float(member_health_ratios.get(member_index, 1.0))
		var member_health := int(health_values[member_index]) if member_index < health_values.size() else maximum
		var ratio := clampf(float(member_health) / float(maximum), 0.0, 1.0)
		member_health_ratios[member_index] = ratio
		var corpse_expire_tick := int(corpse_expire_ticks[member_index]) if member_index < corpse_expire_ticks.size() else -1
		var member_visual := member_visuals.get(member_index) as Node3D
		if ratio <= 0.0 and corpse_expire_tick >= 0 and authoritative_tick >= corpse_expire_tick:
			# Retail removes the corpse at the 60s boundary; free the skinned
			# subtree instead of hiding it so long battles do not accumulate
			# render/processing cost.
			if member_visual != null:
				member_visuals.erase(member_index)
				member_animation_players.erase(member_index)
				_corpse_settle_pending.erase(member_index)
				member_visual.queue_free()
			continue
		var prior_token := int(member_attack_tokens.get(member_index, 0))
		var token := int(attack_tokens[member_index]) if member_index < attack_tokens.size() else prior_token
		member_attack_tokens[member_index] = token
		var prior_release := int(member_attack_release_tokens.get(member_index, 0))
		var release_token := int(release_tokens[member_index]) if member_index < release_tokens.size() else prior_release
		member_attack_release_tokens[member_index] = release_token
		if member_index < target_globals.size() and typeof(target_globals[member_index]) == TYPE_VECTOR3:
			member_attack_target_globals[member_index] = target_globals[member_index]
		else:
			member_attack_target_globals.erase(member_index)
		if prior_ratio > 0.0 and ratio <= 0.0:
			_play_member_state(member_index, "death", token, true)
			_corpse_settle_pending[member_index] = true
			continue
		if ratio <= 0.0:
			continue
		if normalized_state == "attack":
			if battalion_state_changed:
				_play_member_state(member_index, "idle", token, false)
			if token != prior_token:
				var weapon_mode := String(weapon_modes[member_index]) if member_index < weapon_modes.size() else "default"
				_play_member_state(
					member_index,
					"attack_melee" if weapon_mode == "close" else "attack_ranged_pre",
					token,
					true
				)
			if release_token != prior_release:
				_play_member_state(member_index, "attack_ranged_fire", release_token, true)
				_present_archer_member_attack(member_index)
		elif battalion_state_changed or String(member_action_states.get(member_index, "")) != normalized_state:
			# Restart on state change so walk clips cannot keep looping after
			# the sim has already settled into idle (archer stop-anim bug).
			_play_member_state(member_index, normalized_state, token, battalion_state_changed)
	_sync_source_selection_living_mask()
	_refresh_member_overlays()


func _play_member_state(member_index: int, state: String, action_token: int, restart: bool) -> void:
	var play_state := state
	var conditions := _drawable_conditions_for_state(state)
	var receipt: Dictionary = apply_member_drawable_scripts(member_index, conditions)
	last_drawable_script_receipt = receipt.duplicate(true)
	if bool(receipt.get("allow_to_continue", false)):
		restart = false
	var transition := String(receipt.get("transition_anim_state", ""))
	if transition != "":
		var mapped := _clip_state_for_transition_name(transition)
		if mapped != "" and member_clip_for_state(member_index, mapped) != "":
			play_state = mapped
	elif (
		state == "selected"
		and String(member_action_states.get(member_index, "idle")) == "idle"
		and not _has_authored_transition_script(conditions)
		and member_clip_for_state(member_index, "selectionTransition") != ""
	):
		play_state = "selectionTransition"
	var requested := member_clip_for_state(member_index, play_state)
	var authored: Dictionary = AnimationStateSelectScript.select(animation_state_contracts, conditions)
	last_animation_state_receipt = authored.duplicate(true)
	if int(authored.get("specificity", -1)) > 0 and String(authored.get("clip", "")) != "":
		requested = String(authored.get("clip", ""))
	if requested == "":
		return
	_sync_member_authored_state_labels(member_index, requested)
	member_action_states[member_index] = play_state
	member_current_clips[member_index] = requested
	var apply_phase := not restart or bool(authored.get("randomStart", false))
	var blend := _authored_blend_seconds(play_state, requested, authored)
	for player_value in member_animation_players.get(member_index, []):
		_play_member_clip(player_value as AnimationPlayer, requested, play_state, member_index, blend, apply_phase, action_token)
	if play_state.begins_with("attack") and action_token >= 0:
		_last_action_token = maxi(_last_action_token, action_token)
	_sync_member_particle_sys_bones(member_index, authored, conditions)


func _drawable_conditions_for_state(state: String) -> Array:
	var conditions: Array = []
	if state == "selected" or state == "selectionTransition":
		conditions.append("SELECTED")
	elif state == "run":
		conditions.append("MOVING")
	elif state.begins_with("attack"):
		conditions.append("ATTACKING")
	elif state == "death":
		conditions.append("DYING")
	for flag_value in model_condition_flags:
		var flag := String(flag_value).to_upper()
		if flag != "" and not conditions.has(flag):
			conditions.append(flag)
	return conditions


func _clip_state_for_transition_name(transition: String) -> String:
	var folded := transition.to_lower().replace("_", "")
	if folded == "transidletoselected":
		return "selectionTransition"
	return ""


func _has_authored_transition_script(active_conditions: Array) -> bool:
	var active: Dictionary = {}
	for value in active_conditions:
		active[String(value).to_upper()] = true
	for script_value in _drawable_scripts:
		if typeof(script_value) != TYPE_DICTIONARY:
			continue
		var script := script_value as Dictionary
		var matches := true
		for condition_value in script.get("conditions", []) as Array:
			if not active.has(String(condition_value).to_upper()):
				matches = false
				break
		if not matches:
			continue
		for action_value in script.get("actions", []) as Array:
			if typeof(action_value) != TYPE_DICTIONARY:
				continue
			var action := action_value as Dictionary
			var operation := String(action.get("operation", ""))
			var raw := String(action.get("raw", "")).to_lower()
			if operation == "set-transition-animation-state" or raw.contains("curdrawablesettransitionanimstate"):
				return true
	return false


func _authored_blend_seconds(state: String, clip: String, authored: Dictionary) -> float:
	var seconds := float(authored.get("blendSeconds", -1.0))
	if seconds >= 0.0:
		return seconds
	var receipt: Dictionary = authored_animation_property_receipt(state, clip)
	seconds = float(receipt.get("animationBlendSeconds", -1.0))
	if seconds >= 0.0:
		return seconds
	return 0.10


func authored_animation_property_receipt(state: String, clip: String) -> Dictionary:
	## Consume the authored values without silently assigning made-up runtime
	## semantics. Animated props have a separately evidenced relative-weight
	## law; playable-unit AnimationPriority (especially zero) does not.
	var properties: Variant = authored_animation_clip_properties.get("%s|%s" % [state, clip], {})
	if typeof(properties) != TYPE_DICTIONARY or (properties as Dictionary).is_empty():
		return {}
	var receipt := (properties as Dictionary).duplicate(true)
	if receipt.has("animationBlendTimeRaw"):
		var seconds := AnimationStateSelectScript.blend_seconds(receipt.get("animationBlendTimeRaw", null))
		receipt["animationBlendRuntimeSupport"] = "frames-at-30fps"
		receipt["animationBlendSeconds"] = seconds
		receipt["animationBlendApplied"] = seconds >= 0.0
	if receipt.has("animationPriority"):
		receipt["animationPriorityRuntimeSupport"] = "higher-wins-equal-specificity"
		receipt["animationPriorityApplied"] = true
	return receipt


func current_authored_state_labels(member_index: int) -> Array:
	return (member_current_authored_state_labels.get(member_index, []) as Array).duplicate()


func previous_authored_state_labels(member_index: int) -> Array:
	return (member_previous_authored_state_labels.get(member_index, []) as Array).duplicate()


func member_has_authored_state_label(member_index: int, label: String, previous: bool = false) -> bool:
	var labels := previous_authored_state_labels(member_index) if previous else current_authored_state_labels(member_index)
	return labels.has(label)


func _sync_member_particle_sys_bones(member_index: int, authored: Dictionary, active_conditions: Array) -> void:
	var selected: Array = authored.get("conditions", []) as Array
	var picked: Dictionary = ParticleSysBoneScript.select(
		particle_sys_bone_contracts, selected, active_conditions
	)
	var visual := member_visuals.get(member_index) as Node
	var applied: Dictionary = ParticleSysBoneScript.apply(
		visual, picked.get("attachments", []) as Array, {}, entity_id + member_index
	)
	last_particle_sys_bone_receipt = applied.duplicate(true)
	set_meta("particle_sys_bone_source", String(applied.get("source", "")))
	set_meta("particle_sys_bone_applied", int(applied.get("applied", 0)))


func apply_member_drawable_scripts(member_index: int, active_conditions: Array) -> Dictionary:
	## Execute authored BeginScript rows against one member mesh. Prev is the
	## member's current authored StateName set — call this before swapping labels
	## to the destination clip.
	var visual := member_visuals.get(member_index) as Node
	if visual == null:
		return {}
	var asset_factory = load("res://src/view/asset_factory.gd")
	return asset_factory.apply_drawable_scripts(visual, {
		"sourceObjectId": object_id,
		"previousStateLabels": current_authored_state_labels(member_index),
		"drawableScripts": _drawable_scripts,
	}, active_conditions)


func _sync_member_authored_state_labels(member_index: int, clip: String) -> void:
	var next_labels := (authored_state_label_links.get(clip, []) as Array).duplicate()
	var current_labels := current_authored_state_labels(member_index)
	if next_labels == current_labels:
		return
	member_previous_authored_state_labels[member_index] = current_labels
	member_current_authored_state_labels[member_index] = next_labels
	var visual := member_visuals.get(member_index) as Node3D
	if visual != null:
		visual.set_meta("authored_state_labels", next_labels.duplicate())
		visual.set_meta("previous_authored_state_labels", current_labels.duplicate())


func set_attack_target(target: Node3D, target_height: float, travel_seconds: float) -> void:
	var prior_target := _attack_target_ref.get_ref() as Node3D if _attack_target_ref != null else null
	_attack_target_ref = weakref(target) if target != null else null
	_attack_target_height = maxf(0.0, target_height) if is_finite(target_height) else 0.15
	_attack_travel_seconds = maxf(0.05, travel_seconds) if is_finite(travel_seconds) else 0.1
	if current_state == "attack" and prior_target != target:
		_sync_selection_layout("attack", true)


func attack_presentation_height() -> float:
	var source_geometry_height := float(SOURCE_HEALTH_GEOMETRY_HEIGHT.get(object_id, 19.2))
	return maxf(0.05, source_geometry_height * maxf(_source_unit_scale, 0.01) * 0.6)


func member_attack_global_position(member_index: int) -> Vector3:
	var member := member_visuals.get(member_index) as Node3D
	if member == null or not is_instance_valid(member):
		return global_position + Vector3(0.0, attack_presentation_height(), 0.0)
	return member.global_position + Vector3(0.0, attack_presentation_height(), 0.0)


func _present_archer_member_attack(member_index: int) -> void:
	if not VISIBLE_ARROW_PROJECTILE_IDS.has(projectile_object_id) or archer_projectile_controller == null or not archer_projectile_controller.contract_ready:
		return
	if _attack_target_ref == null:
		return
	var target := _attack_target_ref.get_ref() as Node3D
	var member := member_visuals.get(member_index) as Node3D
	if target == null or not is_instance_valid(target) or member == null or not is_instance_valid(member):
		return
	var event_token := _next_archer_presentation_token
	_next_archer_presentation_token += 1
	var start_global := _archer_launch_global(member)
	var target_global: Vector3 = (
		member_attack_target_globals[member_index]
		if member_attack_target_globals.has(member_index)
		else target.global_position + Vector3(0.0, _attack_target_height, 0.0)
	)
	var start_local := archer_projectile_controller.to_local(start_global)
	var target_local := archer_projectile_controller.to_local(target_global)
	var direction := target_local - start_local
	if direction.length_squared() <= 0.000001:
		return
	var pose := Transform3D(Basis.looking_at(direction.normalized(), Vector3.UP), start_local)
	# Compiled per-projectile art carries no audio leaves; only the Gondor
	# sidecar closure does. Never index an empty leaf pool.
	var fire_leaf_count := int(archer_projectile_controller.validated_fire_audio_leaf_count)
	var play_fire_audio := fire_leaf_count > 0 and object_id == ARCHER_OBJECT_ID
	var fire_leaf := posmod(event_token, fire_leaf_count) if fire_leaf_count > 0 else 0
	var projectile := archer_projectile_controller.present_authoritative_projectile(
		event_token,
		pose,
		false,
		fire_leaf,
		play_fire_audio
	)
	if projectile == null:
		return
	projectile.set_meta("authoritative_member_index", member_index)
	projectile.set_meta("presentation_authority", "simulation-member-attack-token")
	projectile.set_meta("projectile_object_id", projectile_object_id)
	# Honest provenance: the compiled per-projectile art when the mounted packs
	# answered for this id, otherwise the borrowed Gondor sidecar arrow.
	projectile.set_meta(
		"art_binding",
		archer_projectile_controller.art_binding
		if archer_projectile_controller.art_binding == ArcherProjectileControllerScript.ART_BINDING_COMPILED
		else (
			"gondor-arrow-closure"
			if projectile_object_id == "GondorArcherArrow"
			else "shared-good-arrow-visible-fallback"
		)
	)
	projectile.set_meta("launch_bone", weapon_launch_bone)
	projectile.set_meta("launch_global", start_global)
	archer_projectiles_presented += 1
	var target_ref: WeakRef = weakref(target)
	var tween := create_tween()
	tween.tween_method(
		Callable(self, "_update_archer_projectile").bind(event_token, start_local, target_local),
		0.0,
		1.0,
		_attack_travel_seconds
	)
	tween.tween_callback(Callable(self, "_finish_archer_projectile").bind(event_token, target_ref, target_global, direction.normalized()))


func _update_archer_projectile(weight: float, event_token: int, start_local: Vector3, target_local: Vector3) -> void:
	if archer_projectile_controller == null:
		return
	var normalized_weight := clampf(weight, 0.0, 1.0)
	var height := ARCHER_CURVE_HEIGHT_SOURCE * maxf(_source_unit_scale, 0.01)
	var delta := target_local - start_local
	var control_a := start_local + delta * 0.20 + Vector3.UP * height
	var control_b := start_local + delta * 0.90 + Vector3.UP * height
	var position := _cubic_bezier(start_local, control_a, control_b, target_local, normalized_weight)
	var direction := _cubic_bezier_tangent(start_local, control_a, control_b, target_local, normalized_weight)
	if direction.length_squared() <= 0.000001:
		direction = target_local - start_local
	var pose := Transform3D(
		Basis.looking_at(direction.normalized(), Vector3.UP),
		position
	)
	archer_projectile_controller.update_projectile_pose(event_token, pose)


func _archer_launch_global(member: Node3D) -> Vector3:
	var skeleton := _first_skeleton(member)
	if skeleton != null:
		var bone_index := skeleton.find_bone(weapon_launch_bone)
		if bone_index >= 0:
			return skeleton.to_global(skeleton.get_bone_global_pose(bone_index).origin)
	return to_global(member.position + Vector3(0.0, attack_presentation_height(), 0.0))


func _first_skeleton(node: Node) -> Skeleton3D:
	if node is Skeleton3D:
		return node as Skeleton3D
	for child in node.get_children():
		var found := _first_skeleton(child)
		if found != null:
			return found
	return null


static func _cubic_bezier(a: Vector3, b: Vector3, c: Vector3, d: Vector3, weight: float) -> Vector3:
	var inverse := 1.0 - weight
	return inverse * inverse * inverse * a + 3.0 * inverse * inverse * weight * b + 3.0 * inverse * weight * weight * c + weight * weight * weight * d


static func _cubic_bezier_tangent(a: Vector3, b: Vector3, c: Vector3, d: Vector3, weight: float) -> Vector3:
	var inverse := 1.0 - weight
	return 3.0 * inverse * inverse * (b - a) + 6.0 * inverse * weight * (c - b) + 3.0 * weight * weight * (d - c)


func _finish_archer_projectile(event_token: int, target_ref: WeakRef, target_global: Vector3, direction: Vector3) -> void:
	if archer_projectile_controller == null:
		return
	archer_projectile_controller.remove_projectile(event_token)
	var target := target_ref.get_ref() as Node3D if target_ref != null else null
	if target == null or not is_instance_valid(target):
		return
	var global_pose := Transform3D(Basis.looking_at(direction, Vector3.UP), target_global)
	var local_pose := target.global_transform.affine_inverse() * global_pose
	var impact_leaf_count := int(archer_projectile_controller.validated_impact_audio_leaf_count)
	var impact_leaf := posmod(event_token, impact_leaf_count) if impact_leaf_count > 0 else 0
	var impact := archer_projectile_controller.present_authoritative_target_impact(
		event_token,
		target,
		local_pose,
		"NormalDamageFX",
		impact_leaf,
		impact_leaf_count > 0 and object_id == ARCHER_OBJECT_ID
	)
	if impact == null:
		return
	impact.set_meta("presentation_authority", "simulation-member-attack-token")
	archer_impacts_presented += 1
	var cleanup := create_tween()
	cleanup.tween_interval(1.0)
	cleanup.tween_callback(Callable(self, "_remove_archer_impact").bind(event_token))


func _remove_archer_impact(event_token: int) -> void:
	if archer_projectile_controller != null:
		archer_projectile_controller.remove_target_impact(event_token)


func _refresh_member_overlays() -> void:
	var overlay_detail_allowed := presentation_detail_level == 0
	for member_index in range(member_count):
		var ratio := float(member_health_ratios.get(member_index, 1.0))
		var living := ratio > 0.0
		var ring: MeshInstance3D = member_selection_rings.get(member_index)
		if ring != null:
			ring.visible = overlay_detail_allowed and visual_is_emerged(member_index) and living and selected
		var emerged := visual_is_emerged(member_index)
		# Retail's rule, one line, shared with the screen-space presenter: a
		# SELECTED horde bars every member including full-health ones (REF-45),
		# and damage alone summons nothing (REF-40). `show_all_health_bars` is the
		# player's authored opt-in (lotr.str APT:EnableHealthBars).
		var show_health := (
			overlay_detail_allowed
			and emerged
			and living
			and HealthOverlayScript.should_show_battalion(selected, show_all_health_bars)
		)
		var member_visual: Node3D = member_visuals.get(member_index)
		# The bar TRAVELS WITH ITS SOLDIER. Only `fill.position.x` used to track
		# the member, so a battalion that had walked or wheeled left every bar
		# hanging over the authored formation slot the member had long left.
		var anchor := Vector3.ZERO
		if member_visual != null:
			anchor = member_visual.position + Vector3(
				0.0, float(member_health_bar_heights.get(member_index, 1.45)), 0.0
			)
		var back: MeshInstance3D = member_health_backs.get(member_index)
		if back != null:
			back.visible = show_health
			if member_visual != null:
				back.position = anchor
		var fill: MeshInstance3D = member_health_fills.get(member_index)
		if fill != null:
			fill.visible = show_health
			fill.scale.x = maxf(0.001, ratio)
			if member_visual != null:
				# A left-anchored drain: the quad shrinks around its centre, so it
				# slides left by half of what it lost.
				var fill_half_width := float(member_health_bar_widths.get(member_index, 0.58)) * BAR_FILL_WIDTH_RATIO * 0.5
				fill.position = anchor + Vector3((ratio - 1.0) * fill_half_width, 0.0, -0.01)


func member_overlay_node_count() -> int:
	return member_selection_rings.size() + member_health_backs.size() + member_health_fills.size()


func source_selection_decal_count() -> int:
	return source_selection_decal.projected_decal_count() if source_selection_decal != null else 0


func member_health_overlay_rows() -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if not private_parity_mode_active:
		return rows
	for member_index in range(member_count):
		var ratio := float(member_health_ratios.get(member_index, 0.0))
		var visual: Node3D = member_visuals.get(member_index)
		if ratio <= 0.0 or visual == null or not is_instance_valid(visual) or not visual.visible:
			continue
		rows.append({
			"member_index": member_index,
			"health_ratio": ratio,
			"world_position": visual.global_position + Vector3.UP * float(member_health_anchor_heights.get(member_index, 1.8)),
			# Half the unit's AUTHORED bar width, shared by every member of the
			# horde. It no longer depends on whether this member's mesh could be
			# measured, so the old "zero means fall back to the source width"
			# contract is gone; a zero here now means the battalion was never
			# built, and the overlay's own floor handles it.
			"half_width": float(member_health_half_widths.get(member_index, 0.0)),
			"experience_level": experience_level,
		})
	return rows




func set_experience_level(level: int) -> void:
	experience_level = maxi(1, level)


func visual_is_emerged(member_index: int) -> bool:
	var visual := member_visuals.get(member_index) as Node3D
	return visual != null and visual.visible


func set_action_state(state: String, force: bool = false, action_token: int = -1) -> void:
	var normalized := state if clip_map.has(state) else "idle"
	var token_changed := normalized == "attack" and action_token >= 0 and action_token != _last_action_token
	if normalized == current_state and not force and not token_changed:
		return
	if normalized == "attack" and current_state != "attack":
		# Entering combat: members claim the nearest slot of the attack spread
		# immediately instead of finishing a formation walk first.
		formation.request_reassign()
	current_state = normalized
	if action_token >= 0:
		_last_action_token = action_token
	current_clip = String(clip_map.get(normalized, ""))
	member_current_clips.clear()
	for member_index in range(member_count):
		if float(member_health_ratios.get(member_index, 1.0)) <= 0.0 and normalized != "death":
			continue
		var requested := member_clip_for_state(member_index, normalized)
		member_current_clips[member_index] = requested
		member_action_states[member_index] = normalized
		_sync_member_authored_state_labels(member_index, requested)
		for player_value in member_animation_players.get(member_index, []):
			_play_member_clip(player_value as AnimationPlayer, requested, normalized, member_index, 0.12, true)
	_play_banner_clip(normalized)
	_refresh_label()


func set_facing_direction(direction: Vector2, turn_rate_degrees_per_second: float = DEFAULT_TURN_RATE_DEGREES_PER_SECOND) -> void:
	if direction.length_squared() <= 0.000001:
		return
	_facing_direction = direction.normalized()
	_target_facing_yaw = atan2(-_facing_direction.x, -_facing_direction.y)
	_turn_rate_radians_per_second = deg_to_rad(maxf(1.0, turn_rate_degrees_per_second))
	formation.notify_heading(_target_facing_yaw)
	if not _facing_sample_ready:
		rotation.y = _target_facing_yaw
		_facing_sample_ready = true


func set_locomotor_speed(value: float) -> void:
	formation.set_locomotor_speed(value)


func set_authoritative_position(world_position: Vector3, snap: bool = false) -> void:
	if not _authoritative_position_ready or snap:
		position = world_position
		_authoritative_position_from = world_position
		_authoritative_position_to = world_position
		_authoritative_position_elapsed = PRESENTATION_TICK_SECONDS
		_authoritative_position_ready = true
		return
	if world_position.is_equal_approx(_authoritative_position_to):
		return
	_authoritative_position_from = position
	_authoritative_position_to = world_position
	_authoritative_position_elapsed = 0.0


func facing_direction() -> Vector2:
	return _facing_direction


func member_formation_slot(member_index: int) -> Vector3:
	var value: Variant = member_formation_slots.get(member_index, Vector3.ZERO)
	return value if typeof(value) == TYPE_VECTOR3 else Vector3.ZERO


func member_presentation_target(member_index: int, state: String = "") -> Vector3:
	var normalized_state := state if state != "" else current_state
	var base_slot := member_formation_slot(member_index)
	if normalized_state != "attack" or object_id in [ARCHER_OBJECT_ID, RANGER_OBJECT_ID] or _attack_target_ref == null:
		return base_slot
	var target := _attack_target_ref.get_ref() as Node3D
	if target == null or not is_inside_tree() or not target.is_inside_tree():
		return base_slot
	var target_local_3d := to_local(target.global_position)
	var target_local := Vector2(target_local_3d.x, target_local_3d.z)
	var target_distance := target_local.length()
	if target_distance <= 0.001:
		return base_slot
	var forward := target_local / target_distance
	var tangent := Vector2(-forward.y, forward.x)
	if target is RetailStructure:
		# Melee vs buildings: members surround the footprint along the facing
		# arc instead of stacking into one face, deterministically by member
		# index. Swing timing is already staggered by the sim's attack ticks.
		var ring_radius := maxf(1.2, float(target.pick_radius) * 0.8)
		var span := PI * 1.3
		var fraction := (float(member_index) + 0.5) / float(maxi(1, member_count)) - 0.5
		var around := forward.rotated(fraction * span)
		var surround := target_local - around * ring_radius
		return Vector3(surround.x, base_slot.y, surround.y)
	var lane := base_slot.x
	# Alternating the back two ranks prevents the engagement target from
	# collapsing into three perfectly overlapping files while remaining fully
	# deterministic for replay and capture.
	if absf(base_slot.z) > 0.001:
		lane += (-0.18 if posmod(member_index + entity_id, 2) == 0 else 0.18)
	var advance := minf(MELEE_ADVANCE_LIMIT, maxf(0.0, target_distance - 0.85))
	advance = maxf(0.0, advance - absf(base_slot.z) * MELEE_RANK_SPACING)
	var target_position := forward * advance + tangent * lane
	return Vector3(target_position.x, base_slot.y, target_position.y)


func reflowing_member_count(tolerance: float = 0.08) -> int:
	var result := 0
	for member_index in range(member_count):
		var visual := member_visuals.get(member_index) as Node3D
		if visual == null or float(member_health_ratios.get(member_index, 0.0)) <= 0.0:
			continue
		if visual.position.distance_to(member_presentation_target(member_index)) > tolerance:
			result += 1
	return result


func clip_for_state(state: String) -> String:
	return String(clip_map.get(state, ""))


func death_animation_pick(member_index: int = -1) -> Dictionary:
	## Clip the presenter is already playing (or will play) for this death.
	## Frame stays -1 at event time: the sim death event is not the authored
	## AnimationSound frame. Clip match is enough to beat lowest-id fallback.
	var clip := ""
	if member_index >= 0:
		clip = String(member_current_clips.get(member_index, ""))
		if clip == "":
			clip = member_clip_for_state(member_index, "death")
	if clip == "":
		clip = String(clip_map.get("death", ""))
	return {"clip": clip, "frame": -1, "source": "live-battalion-clip"}


func member_clip_for_state(member_index: int, state: String) -> String:
	var values: Array = clip_sets.get(state, [])
	if values.is_empty():
		return ""
	return String(values[posmod(member_index, values.size())])


func variant_clips_for_state(state: String) -> Array[String]:
	var result: Array[String] = []
	for member_index in range(member_count):
		var clip := member_clip_for_state(member_index, state)
		if clip != "" and not result.has(clip):
			result.append(clip)
	result.sort()
	return result


func active_clip_variants() -> Array[String]:
	var result: Array[String] = []
	for clip_value in member_current_clips.values():
		var clip := String(clip_value)
		if clip != "" and not result.has(clip):
			result.append(clip)
	result.sort()
	return result


func phase_for_member(member_index: int, state: String) -> float:
	var state_seed := int({"idle": 11, "run": 29, "attack": 47, "death": 61, "victory": 73}.get(state, 3))
	return float(posmod(entity_id * 13 + member_index * 37 + state_seed, 97)) / 97.0


func phase_variation_count(state: String) -> int:
	var phases: Dictionary = {}
	for member_index in range(member_count):
		phases[snappedf(phase_for_member(member_index, state), 0.0001)] = true
	return phases.size()


func markers_visible() -> bool:
	return _team_ring != null and _team_ring.visible and _team_marker != null and _team_marker.visible


func synthetic_overlay_node_count() -> int:
	var result := 0
	for node in [_team_ring, _selection_ring, _team_marker, _status_label]:
		if node != null and is_instance_valid(node):
			result += 1
	var health_back := get_node_or_null("SyntheticHealthBack")
	if health_back != null:
		result += 1
	return result


func _is_private_retail_pack(definition: Dictionary) -> bool:
	## Parity mode suppresses the repository-authored legal-safe overlays
	## (synthetic team/selection tori, health bars) in favour of the pack's own
	## converted retail surfaces. What decides that is whether those retail
	## surfaces EXIST for this unit, not what the pack is called: an id
	## allowlist ("bfme2-men-vslice", "bfme2-men-ranger-overlay") refused every
	## newer converted pack and would have admitted an empty same-named one.
	return PackCapability.provides_retail_presentation(_retail_presentation_pack_root(definition))


func _retail_presentation_pack_root(definition: Dictionary) -> String:
	## The pack that owns this unit's retail presentation surfaces. Normally the
	## unit's own pack; the bounded Ranger overlay ships rules + model only and
	## explicitly borrows the host pack's shared contracts, which is the same
	## borrow _configure_source_selection_decal already performs below.
	if object_id == RANGER_OBJECT_ID:
		return String(ContentDB.get_bundle_object(ARCHER_OBJECT_ID).get("_pack_root", ""))
	return String(definition.get("_pack_root", ""))


## True when this pack root ships the retail SHADOW_MERGE_DECAL selection
## contract (every converted faction pack carries the identical one). The
## decal's configure still validates fail-closed; this only decides whether the
## attempt replaces the synthetic fallback rings.
func _pack_ships_selection_decal(pack_root: String) -> bool:
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	var contract_path := ProjectSettings.globalize_path(pack_root).replace("\\", "/").simplify_path().path_join("effects/men-selection-decal.json").simplify_path()
	var pack_base := ProjectSettings.globalize_path(pack_root).replace("\\", "/").simplify_path().trim_suffix("/")
	return contract_path.to_lower().begins_with(pack_base.to_lower() + "/") and FileAccess.file_exists(contract_path)


## The pack root whose decal contract this unit binds: its own pack when the
## contract shipped there, otherwise the first mounted pack that carries the
## universal contract (a faction converted before the decal emitter landed —
## rotwk-angmar — borrows it exactly like the Ranger overlay borrows Men's).
func _selection_decal_source_root(definition: Dictionary) -> String:
	var own := String(definition.get("_pack_root", ""))
	if _pack_ships_selection_decal(own):
		return own
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if _pack_ships_selection_decal(root):
			return root
	return ""


func _configure_combat_visual_contract(definition: Dictionary) -> void:
	combat_visual_source_closure_present = false
	exact_projectile_node_count = 0
	exact_impact_effect_node_count = 0
	combat_visual_contract_error = ""
	combat_visual_diagnostics.clear()
	combat_visual_diagnostic_summaries.clear()
	if not VISIBLE_ARROW_PROJECTILE_IDS.has(projectile_object_id):
		return
	if object_id == RANGER_OBJECT_ID and weapon_launch_bone != RANGER_LAUNCH_BONE:
		combat_visual_contract_error = "Ranger primary launch bone is not ARROW"
		return
	archer_projectile_controller = ArcherProjectileControllerScript.new()
	archer_projectile_controller.name = "RetailArcherProjectileController"
	add_child(archer_projectile_controller)
	var sidecar_root := String(definition.get("_pack_root", ""))
	if not _pack_ships_archer_projectile(sidecar_root):
		sidecar_root = _mounted_archer_projectile_root()
	# 1. The Gondor sidecar is this projectile's OWN full closure (streak plus
	#    the FX_GoodArrowHit g_arrow impact and 100 exact audio leaves), so for
	#    GondorArcherArrow it outranks the art-only document. That is
	#    resolution by identity, not a special case: the sidecar declares
	#    weapon.projectileTemplateId = GondorArcherArrow and nothing else.
	#
	#    TODO (2026-08-11, opus-arrow-art): this comparison ROTS the day the
	#    importer compiles an impact/audio closure into projectileArt. From then
	#    on the richer compiled document is silently ignored for the one
	#    projectile it would improve, because this branch short-circuits before
	#    step 2 -- and the runner check
	#    gondor_sidecar_full_closure_outranks_art_only_row pins the old
	#    behaviour in place, so nothing will fail to tell you. Replace the id
	#    literal with a comparison of what each candidate actually carries
	#    (impact model + audio leaf counts) when that lane lands.
	if (
		sidecar_root != ""
		and projectile_object_id == SIDECAR_PROJECTILE_OBJECT_ID
		and archer_projectile_controller.configure_from_pack(sidecar_root, _source_unit_scale)
		and archer_projectile_controller.contract_ready
	):
		combat_visual_source_closure_present = true
		exact_projectile_node_count = int(archer_projectile_controller.validated_streak_texture_count > 0)
		exact_impact_effect_node_count = int(archer_projectile_controller.validated_impact_model_count > 0)
		return
	# 2. Art compiled for THIS projectile object id, from whichever mounted
	#    pack ships it (standard resolution: the pack's own first).
	var art := _compiled_projectile_art(definition, projectile_object_id)
	if not art.is_empty() and archer_projectile_controller.configure_from_projectile_art(
		projectile_object_id,
		String(art.get("_pack_root", "")),
		art.get("entry", {}) as Dictionary,
		_source_unit_scale
	) and archer_projectile_controller.contract_ready:
		combat_visual_source_closure_present = true
		exact_projectile_node_count = int(archer_projectile_controller.validated_streak_texture_count > 0)
		exact_impact_effect_node_count = 0
		return
	if not art.is_empty():
		# The document existed but could not bind: that is a defect, not a gap.
		_record_combat_visual_diagnostic({
			"code": "projectile-art-binding-rejected",
			"projectileObjectId": projectile_object_id,
			"packRoot": String(art.get("_pack_root", "")),
			"reason": String(archer_projectile_controller.error),
		})
	# 3. Fall back to the Gondor sidecar for a projectile it does NOT declare.
	#    It is the only shipped arrow art until the next republish carries the
	#    compiled per-projectile documents, and for a non-Gondor projectile it
	#    is a borrowed Good arrow -- say so out loud rather than rendering
	#    silently.
	var pack_root := sidecar_root
	if archer_projectile_controller.configure_from_pack(pack_root, _source_unit_scale) and archer_projectile_controller.contract_ready:
		# Recorded only once the borrow actually binds. Appending before the
		# configure would leave a failed bind claiming both "presenting the
		# shared Good arrow" and "presents no visible arrows".
		if projectile_object_id != SIDECAR_PROJECTILE_OBJECT_ID:
			_record_combat_visual_diagnostic({
				"code": "arrow-art-shared-good-fallback",
				"projectileObjectId": projectile_object_id,
				"packRoot": pack_root,
				"packFileKey": ARCHER_PROJECTILE_PACK_KEY,
				"impact": "no mounted pack ships compiled art for this projectile; presenting the Gondor sidecar's shared Good arrow",
			})
		combat_visual_source_closure_present = true
		# These are validated presentation-resource counts. No visible projectile
		# or impact node is emitted until an owner supplies the authoritative
		# weapon event, pose, DamageFX set, attachment policy, and audio leaf.
		exact_projectile_node_count = int(archer_projectile_controller.validated_streak_texture_count > 0)
		exact_impact_effect_node_count = int(archer_projectile_controller.validated_impact_model_count > 0)
		return
	if archer_projectile_controller.contract_declared:
		combat_visual_contract_error = "invalid selected-pack arrow projectile contract: %s" % archer_projectile_controller.error
		_record_combat_visual_diagnostic({
			"code": "arrow-art-unresolved",
			"projectileObjectId": projectile_object_id,
			"objectId": object_id,
			"reason": String(archer_projectile_controller.error),
			"impact": "this battalion presents no visible arrows",
		})
		return
	# Retail evidence closes this chain as GondorArcherBow ->
	# GondorArcherArrow/GoodFactionArrow -> EXArrowStreak01, then
	# GOOD_ARROW_PIERCE -> FX_GoodArrowHit -> g_arrow + ImpactArrow.
	combat_visual_contract_error = (
		"missing selected-pack arrow combat closure for %s -> %s: convert " % [object_id, projectile_object_id]
		+ "GoodFactionArrow/GondorArcherArrow with EXArrowStreak01, and "
		+ "GOOD_ARROW_PIERCE/FX_GoodArrowHit with g_arrow plus ImpactArrow"
	)
	# The silent path this replaces: no mounted pack answers for this
	# projectile at all, so a RotWK-only mount fired invisible arrows with
	# nothing but an unread error string to show for it.
	_record_combat_visual_diagnostic({
		"code": "arrow-art-unresolved",
		"projectileObjectId": projectile_object_id,
		"objectId": object_id,
		"reason": "no mounted pack ships %s art or the %s sidecar" % [
			PROJECTILE_ART_PACK_KEY, ARCHER_PROJECTILE_PACK_KEY
		],
		"impact": "this battalion presents no visible arrows",
	})


func _compiled_projectile_object_id(runtime_object_id: String) -> String:
	for document_value in ContentDB.get_playable_unit_runtimes().values():
		var document := document_value as Dictionary
		if (
			PlayableUnitRuntimeAdapter.runtime_unit_id(document) != runtime_object_id
			and PlayableUnitRuntimeAdapter.runtime_member_id(document) != runtime_object_id
		):
			continue
		var gameplay := (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
		var resolved := (gameplay.get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary
		return String((resolved.get("combat", {}) as Dictionary).get("projectileObjectId", ""))
	return ""


## Record one arrow-art diagnostic AND surface it. A diagnostic nobody reads is
## the same defect as the silent contract-error string this replaced, so every
## row goes three places: the aggregated array (which the parent slice pulls up
## exactly like retail_fords_battlefield pulls up its animated-prop rows), a
## node meta for probes, the engine warning log, and the diagnostics run record
## when one is open.
func _record_combat_visual_diagnostic(row: Dictionary) -> void:
	combat_visual_diagnostics.append(row)
	set_meta("combat_visual_diagnostics", combat_visual_diagnostics.duplicate(true))
	var code := String(row.get("code", "unnamed"))
	var summary := "%s (%s %s -> %s): %s" % [
		code,
		object_id,
		"entity %d" % entity_id,
		String(row.get("projectileObjectId", "")),
		String(row.get("impact", row.get("reason", ""))),
	]
	# Severity is deliberate. The shared-Good borrow is the EXPECTED state until
	# the next republish ships compiled art, and an engine warning for a known
	# universal condition would either redden every gate or train readers to
	# ignore warnings. Only the genuine defects -- no art at all, or a shipped
	# document that will not bind -- earn a warning.
	var level := String(DIAGNOSTIC_SEVERITY.get(code, "warn"))
	if level == "warn" and not suppress_engine_warnings:
		push_warning(summary)
	DiagLogScript.emit(level, "presentation.arrow_art", {
		"code": code,
		"objectId": object_id,
		"entityId": entity_id,
		"projectileObjectId": String(row.get("projectileObjectId", "")),
		"packRoot": String(row.get("packRoot", "")),
		"reason": String(row.get("reason", "")),
		"impact": String(row.get("impact", "")),
	})
	combat_visual_diagnostic_summaries.append(summary)


## Standard mounted-pack resolution for compiled per-projectile art: the
## battalion's own pack answers first, then any other mounted pack that ships
## a row for this projectile object id. No pack-id or faction guessing, and no
## borrowing a foreign projectile's art under a different id.
func _compiled_projectile_art(definition: Dictionary, wanted_projectile_object_id: String) -> Dictionary:
	if wanted_projectile_object_id == "":
		return {}
	var roots: Array[String] = []
	var own := String(definition.get("_pack_root", ""))
	if own != "":
		roots.append(own)
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root != "" and not roots.has(root):
			roots.append(root)
	for pack_root in roots:
		var entry := _projectile_art_entry(pack_root, wanted_projectile_object_id)
		if not entry.is_empty():
			return {"_pack_root": pack_root, "entry": entry}
	return {}


func _projectile_art_entry(pack_root: String, wanted_projectile_object_id: String) -> Dictionary:
	var relative := _pack_declared_file(pack_root, PROJECTILE_ART_PACK_KEY)
	if relative == "":
		return {}
	var path := ModLoader.resolve_pack_path(pack_root, relative)
	if path == "" or not FileAccess.file_exists(path) or not ModLoader.path_is_within(pack_root, path):
		return {}
	var value: Variant = ModLoader._read_json(path)
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var document := value as Dictionary
	if String(document.get("schema", "")) != RetailArcherProjectileController.PROJECTILE_ART_SCHEMA:
		return {}
	var rows: Variant = document.get("projectiles", [])
	if typeof(rows) != TYPE_ARRAY:
		return {}
	for row_value in rows as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		if String(row.get("projectileObjectId", "")) == wanted_projectile_object_id:
			return row
	return {}


func _pack_declared_file(pack_root: String, key: String) -> String:
	if pack_root == "":
		return ""
	var pack_path := ModLoader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return ""
	var value: Variant = ModLoader._read_json(pack_path)
	if typeof(value) != TYPE_DICTIONARY:
		return ""
	var files: Variant = (value as Dictionary).get("files", {})
	if typeof(files) != TYPE_DICTIONARY:
		return ""
	return String((files as Dictionary).get(key, ""))


func _pack_ships_archer_projectile(pack_root: String) -> bool:
	return _pack_declared_file(pack_root, ARCHER_PROJECTILE_PACK_KEY) != ""


func _mounted_archer_projectile_root() -> String:
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if _pack_ships_archer_projectile(root):
			return root
	return ""


func _configure_source_selection_decal(definition: Dictionary) -> void:
	# Retail selection presentation is universal: any mounted pack that ships
	# the SHADOW_MERGE_DECAL contract binds the leafy squiggly ring (the elven
	# porter's giant blue/green synthetic ellipses were the men-less fallback,
	# and the Angmar porter's until it learned to borrow a shipped contract).
	# When no mounted pack has the contract, keep the legal-safe synthetic
	# overlay.
	var decal_root := _selection_decal_source_root(definition)
	if not private_parity_mode_active and decal_root == "":
		return
	var positions: Array[Vector3] = []
	for member_index in range(member_count):
		var visual: Node3D = member_visuals.get(member_index)
		if visual != null:
			positions.append(_member_selection_anchor(member_index))
	source_selection_decal = SelectionDecalScript.new()
	source_selection_decal.name = "RetailMenSelectionDecal"
	add_child(source_selection_decal)
	# Private parity (Men) stays strict: only its OWN pack's contract counts,
	# fail-closed. Non-parity factions may borrow a shipped copy.
	var pack_root := String(definition.get("_pack_root", "")) if private_parity_mode_active else decal_root
	if object_id == RANGER_OBJECT_ID:
		# The bounded Ranger overlay owns the unit model and rules only. Reuse the
		# selected retail Men pack's shared selection-decal contract explicitly.
		pack_root = String(ContentDB.get_bundle_object(ARCHER_OBJECT_ID).get("_pack_root", ""))
	var selection_error := source_selection_decal.configure(pack_root, _source_unit_scale, positions)
	if selection_error == "":
		member_overlay_status = "source-health-canvas-and-source-selection-merge-decal-bound-oracle-color-throb-pending"
	else:
		member_overlay_status = "source-health-canvas-bound-selection-decal-fail-closed: " + selection_error


func _sync_source_selection_living_mask() -> void:
	if source_selection_decal == null or not source_selection_decal.contract_ready:
		return
	var living: Array[bool] = []
	for member_index in range(member_count):
		living.append(float(member_health_ratios.get(member_index, 0.0)) > 0.0)
	source_selection_decal.set_living_mask(living)
	source_selection_decal.set_selected(selected and production_exit_progress >= 1.0)


func _resolve_animation_name(player: AnimationPlayer, requested: String) -> String:
	## Cooked playable clips often use W3D-style ids like GUAragorn_SKL.GUAragorn_IDLE
	## while GLB AnimationPlayer tracks are basename-only or slash-separated.
	var want := requested.strip_edges()
	if want == "" or player == null:
		return ""
	var want_lower := want.to_lower()
	var want_base := want_lower.get_file().replace("\\", "/")
	if want_base.contains("."):
		want_base = want_base.get_slice(".", want_base.get_slice_count(".") - 1)
	if want_base.contains("/"):
		want_base = want_base.get_file()
	for animation_name in player.get_animation_list():
		var candidate := String(animation_name)
		var cand_lower := candidate.to_lower()
		if cand_lower == want_lower:
			return candidate
		if cand_lower.ends_with("/" + want_lower) or cand_lower.ends_with("." + want_lower):
			return candidate
		var cand_base := cand_lower.get_file().replace("\\", "/")
		if cand_base.contains("."):
			cand_base = cand_base.get_slice(".", cand_base.get_slice_count(".") - 1)
		if cand_base == want_base or cand_base.ends_with("_" + want_base) or want_base.ends_with("_" + cand_base):
			return candidate
		if cand_lower.ends_with("/" + want_base) or cand_lower.ends_with("." + want_base):
			return candidate
	return ""


## Authored span of the fire-and-reload animation state, in seconds. Set by
## the slice from the sim's unit rule (firing duration + the reload the sim
## actually waits out); 0 leaves fire clips at their authored speed.
var attack_reload_seconds := 0.0


func set_attack_reload_seconds(value: float) -> void:
	attack_reload_seconds = value if is_finite(value) and value > 0.0 else 0.0


func _play_member_clip(player: AnimationPlayer, requested: String, state: String, member_index: int, blend: float, apply_phase: bool, action_token: int = 0) -> void:
	var playable := _resolve_animation_name(player, requested)
	if playable == "":
		return
	player.speed_scale = _authored_animation_speed_factor(state, requested, member_index, action_token)
	if state == "attack_ranged_fire":
		# Retail comments UseWeaponTiming OUT (gondorarcher.ini:251/261) and
		# authors AnimationSpeedFactorRange = 1.2 1.3 (:262) so the fire clip
		# always finishes before the randomized reload. Play at that authored
		# factor and idle until the next shot — do not stretch across reload.
		player.speed_scale = _authored_animation_speed_factor(state, requested, member_index, action_token)
	player.play(playable, blend)
	if apply_phase and state in ["idle", "run", "victory", "selected"] and player.current_animation_length > 0.0:
		player.seek(player.current_animation_length * phase_for_member(member_index, state), true)


func _authored_animation_speed_factor(state: String, clip: String, member_index: int, action_token: int) -> float:
	var values: Variant = authored_animation_speed_ranges.get("%s|%s" % [state, clip], [])
	# SAGE samples uniformly when entering the animation state. Keep that
	# authored behavior deterministic for lockstep/replays using stable runtime
	# identities; exact retail global-RNG stream alignment remains an oracle gap.
	return AnimationTimingScript.speed_factor(values, entity_id, member_index, action_token, clip)


func _process(_delta: float) -> void:
	_update_root_presentation(_delta)
	_update_member_positions(_delta)
	_settle_finished_corpses()
	if current_state == "death" or current_clip == "":
		return
	# The source GLB clips intentionally preserve their one-shot metadata. The
	# presentation controller replays persistent RTS states after each clip.
	# Source attack clips are one-shot and are restarted only by a new
	# authoritative attack-cycle token, never merely because playback ended.
	if current_state == "attack":
		for member_index in range(member_count):
			if float(member_health_ratios.get(member_index, 1.0)) <= 0.0:
				continue
			if not String(member_action_states.get(member_index, "idle")).begins_with("attack"):
				continue
			var any_playing := false
			for player_value in member_animation_players.get(member_index, []):
				if (player_value as AnimationPlayer).is_playing():
					any_playing = true
					break
			if not any_playing:
				_play_member_state(member_index, "idle", int(member_attack_tokens.get(member_index, 0)), false)
		return
	for member_index in range(member_count):
		if float(member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		if String(member_action_states.get(member_index, "")) == "selectionTransition":
			# The at-attention acknowledgement is one-shot; then loop ATNB for
			# as long as the battalion stays selected and idle. Snapping back
			# to idle the moment the one-shot ended was the owner's "clicking
			# Gondor soldiers does not play the at-attention animation".
			var attention_playing := false
			for player_value in member_animation_players.get(member_index, []):
				if (player_value as AnimationPlayer).is_playing():
					attention_playing = true
					break
			if not attention_playing:
				if selected and current_state == "idle" and String(clip_map.get("selected", "")) != "":
					_play_member_state(member_index, "selected", -1, false)
				else:
					_play_member_state(member_index, "idle", -1, false)
			continue
		var requested := String(member_current_clips.get(member_index, ""))
		for player_value in member_animation_players.get(member_index, []):
			var player := player_value as AnimationPlayer
			if not player.is_playing():
				_play_member_clip(player, requested, current_state, member_index, 0.08, false)


func _settle_finished_corpses() -> void:
	if _corpse_settle_pending.is_empty():
		return
	for member_index in _corpse_settle_pending.keys():
		var still_playing := false
		for player_value in member_animation_players.get(member_index, []):
			var player := player_value as AnimationPlayer
			if player != null and player.is_playing():
				still_playing = true
				break
		if still_playing:
			continue
		var visual := member_visuals.get(member_index) as Node3D
		if visual != null:
			# Death clip ended: keep the final corpse pose but stop all node
			# processing so retained corpses cost render time only.
			visual.process_mode = Node.PROCESS_MODE_DISABLED
		_corpse_settle_pending.erase(member_index)


func _update_root_presentation(delta: float) -> void:
	if not is_finite(delta) or delta <= 0.0:
		return
	if _authoritative_position_ready and _authoritative_position_elapsed < PRESENTATION_TICK_SECONDS:
		_authoritative_position_elapsed = minf(PRESENTATION_TICK_SECONDS, _authoritative_position_elapsed + delta)
		var weight := _authoritative_position_elapsed / PRESENTATION_TICK_SECONDS
		position = _authoritative_position_from.lerp(_authoritative_position_to, weight)
	if _facing_sample_ready:
		var difference := wrapf(_target_facing_yaw - rotation.y, -PI, PI)
		var step := _turn_rate_radians_per_second * delta
		rotation.y += clampf(difference, -step, step)


func _update_member_positions(delta: float) -> void:
	if not is_inside_tree() or not is_finite(delta) or delta <= 0.0:
		return
	if formation.battalion == null:
		formation.configure(self)
	formation.update(delta)
	_update_legal_safe_member_overlays()


func _update_legal_safe_member_overlays() -> void:
	if presentation_detail_level > 0:
		# Overlays are hidden by distance LOD; do not pay to track their
		# positions until they are drawn again.
		return
	for member_index in range(member_count):
		var visual := member_visuals.get(member_index) as Node3D
		if visual == null:
			continue
		var ring := member_selection_rings.get(member_index) as MeshInstance3D
		if ring != null:
			var anchor := _member_selection_anchor(member_index)
			ring.position.x = anchor.x
			ring.position.z = anchor.z
		if private_parity_mode_active:
			continue
		var health_back := member_health_backs.get(member_index) as MeshInstance3D
		if health_back != null:
			health_back.position.x = visual.position.x
			health_back.position.z = visual.position.z
		var health_fill := member_health_fills.get(member_index) as MeshInstance3D
		if health_fill != null:
			var ratio := float(member_health_ratios.get(member_index, 1.0))
			health_fill.position.x = visual.position.x + (ratio - 1.0) * 0.27
			health_fill.position.z = visual.position.z - 0.01


func _sync_selection_layout(state: String, force: bool = false) -> void:
	if source_selection_decal == null or not source_selection_decal.contract_ready:
		return
	var layout_state := "attack" if state == "attack" and object_id not in [ARCHER_OBJECT_ID, RANGER_OBJECT_ID] else "formation"
	if not force and layout_state == _selection_layout_state:
		return
	var positions: Array[Vector3] = []
	for member_index in range(member_count):
		positions.append(_member_selection_anchor(member_index))
	source_selection_decal.set_member_positions(positions)
	_selection_layout_state = layout_state


func _refresh_label() -> void:
	if _status_label == null:
		return
	_status_label.text = "%d%%" % roundi(health_ratio * 100.0)
	_status_label.visible = false
