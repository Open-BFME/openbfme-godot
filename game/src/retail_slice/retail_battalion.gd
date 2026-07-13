class_name RetailBattalion
extends Node3D
## Fifteen imported Gondor Fighter GLBs presented as one selectable battalion.

const OBJECT_ID := "bfme2.object.gondor-fighter"
const FORMATION_COLUMNS := 5
const MEMBER_SPACING := Vector2(1.4, 1.25)

var entity_id := 0
var team := 0
var current_state := "idle"
var current_clip := ""
var member_count := 0
var retail_visual_count := 0
var rigged_member_count := 0
var animation_player_count := 0
var team_tinted_surface_count := 0
var clip_map: Dictionary = {}
var clip_sets: Dictionary = {}
var clip_modes: Dictionary = {}
var attack_uses_weapon_timing := false
var equipment_contract: Dictionary = {}
var equipment_contract_ready := false
var unresolved_animation_track_count := 0
var animation_players: Array[AnimationPlayer] = []
var member_animation_players: Dictionary = {}
var member_current_clips: Dictionary = {}
var selected := false
var health_ratio := 1.0
var _selection_ring: MeshInstance3D
var _team_ring: MeshInstance3D
var _status_label: Label3D
var _team_marker: MeshInstance3D
var _last_action_token := -1
var _facing_direction := Vector2.RIGHT


func configure(id: int, battalion_team: int, capability: Dictionary, expected_members: int = 15) -> void:
	entity_id = id
	team = battalion_team
	name = "RetailBattalion_%d" % id
	_build_clip_map(capability)
	_build_members(expected_members)
	_build_markers()
	set_action_state("idle", true)


func _build_clip_map(capability: Dictionary) -> void:
	var states: Dictionary = capability.get("states", {}) as Dictionary
	clip_sets = {
		"idle": _clips(states.get("idle", {})),
		"run": _clips(states.get("move", {})),
		"attack": _clips(states.get("attack", {})),
		"death": _clips(states.get("death", {})),
		"victory": _clips(states.get("idle", {})),
	}
	clip_modes.clear()
	for state_name in ["idle", "move", "attack", "death"]:
		var state_value: Variant = states.get(state_name, {})
		if typeof(state_value) == TYPE_DICTIONARY:
			clip_modes[state_name] = String((state_value as Dictionary).get("mode", "loop"))
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


func _build_members(expected_members: int) -> void:
	var asset_factory = load("res://src/view/asset_factory.gd")
	for index in range(expected_members):
		var visual: Node3D = asset_factory.make_bundle_object_visual(OBJECT_ID, team)
		if visual == null:
			continue
		var member_index := member_count
		var column := member_index % FORMATION_COLUMNS
		var row := member_index / FORMATION_COLUMNS
		visual.position = Vector3(
			(float(column) - 2.0) * MEMBER_SPACING.x,
			0.0,
			(float(row) - 1.0) * MEMBER_SPACING.y
		)
		# Every imported member keeps the same model-space forward axis. Battalion
		# root yaw is updated from the authoritative route/target direction.
		visual.rotation.y = -PI * 0.5
		add_child(visual)
		member_count += 1
		if bool(visual.get_meta("authored", false)) and String(visual.get_meta("mesh_path", "")).ends_with("gondor-fighter.glb"):
			retail_visual_count += 1
		if bool(visual.get_meta("has_skeleton", false)):
			rigged_member_count += 1
		team_tinted_surface_count += int(visual.get_meta("team_tinted_surfaces", 0))
		member_animation_players[member_index] = []
		_collect_animation_players(visual, member_index)


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
	_team_ring = MeshInstance3D.new()
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
	_status_label.position = Vector3(0, 3.5, 0)
	_status_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_status_label.font_size = 26
	_status_label.outline_size = 6
	_status_label.modulate = Color("b9dcff") if team == 0 else Color("ffb4af")
	_status_label.visible = false
	add_child(_status_label)
	_refresh_label()


func set_selected(value: bool) -> void:
	selected = value and health_ratio > 0.0
	if _selection_ring != null:
		_selection_ring.visible = selected


func set_health(current: int, maximum: int) -> void:
	health_ratio = clampf(float(current) / float(maxi(1, maximum)), 0.0, 1.0)
	var living := health_ratio > 0.0
	if not living:
		selected = false
	if _team_ring != null:
		_team_ring.visible = living
	if _selection_ring != null:
		_selection_ring.visible = living and selected
	if _team_marker != null:
		_team_marker.visible = living
		_team_marker.scale.x = maxf(0.001, health_ratio)
		_team_marker.position.x = (health_ratio - 1.0) * 1.25
	_refresh_label()


func set_action_state(state: String, force: bool = false, action_token: int = -1) -> void:
	var normalized := state if clip_map.has(state) else "idle"
	var token_changed := normalized == "attack" and action_token >= 0 and action_token != _last_action_token
	if normalized == current_state and not force and not token_changed:
		return
	current_state = normalized
	if action_token >= 0:
		_last_action_token = action_token
	current_clip = String(clip_map.get(normalized, ""))
	member_current_clips.clear()
	for member_index in range(member_count):
		var requested := member_clip_for_state(member_index, normalized)
		member_current_clips[member_index] = requested
		for player_value in member_animation_players.get(member_index, []):
			_play_member_clip(player_value as AnimationPlayer, requested, normalized, member_index, 0.12, true)
	_refresh_label()


func set_facing_direction(direction: Vector2) -> void:
	if direction.length_squared() <= 0.000001:
		return
	_facing_direction = direction.normalized()
	rotation.y = atan2(-_facing_direction.x, -_facing_direction.y)


func facing_direction() -> Vector2:
	return _facing_direction


func clip_for_state(state: String) -> String:
	return String(clip_map.get(state, ""))


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


func _resolve_animation_name(player: AnimationPlayer, requested: String) -> String:
	for animation_name in player.get_animation_list():
		var candidate := String(animation_name)
		if candidate.to_lower() == requested.to_lower() or candidate.to_lower().ends_with("/" + requested.to_lower()):
			return candidate
	return ""


func _play_member_clip(player: AnimationPlayer, requested: String, state: String, member_index: int, blend: float, apply_phase: bool) -> void:
	var playable := _resolve_animation_name(player, requested)
	if playable == "":
		return
	player.speed_scale = 0.96 + float(posmod(entity_id * 5 + member_index * 7, 7)) * (0.08 / 6.0)
	player.play(playable, blend)
	if apply_phase and state in ["idle", "run", "victory"] and player.current_animation_length > 0.0:
		player.seek(player.current_animation_length * phase_for_member(member_index, state), true)


func _process(_delta: float) -> void:
	if current_state == "death" or current_clip == "":
		return
	# The source GLB clips intentionally preserve their one-shot metadata. The
	# presentation controller replays persistent RTS states after each clip.
	# Source attack clips are one-shot and are restarted only by a new
	# authoritative attack-cycle token, never merely because playback ended.
	if current_state == "attack":
		return
	for member_index in range(member_count):
		var requested := String(member_current_clips.get(member_index, ""))
		for player_value in member_animation_players.get(member_index, []):
			var player := player_value as AnimationPlayer
			if not player.is_playing():
				_play_member_clip(player, requested, current_state, member_index, 0.08, false)


func _refresh_label() -> void:
	if _status_label == null:
		return
	_status_label.text = "%d%%" % roundi(health_ratio * 100.0)
	_status_label.visible = false
