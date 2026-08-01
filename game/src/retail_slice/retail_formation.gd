extends RefCounted
## Formation movement engine for battalion members.
##
## Members hold assigned formation slots. The battalion root moves and turns
## along the authoritative route, and members steer individually toward their
## (moving) slot targets with arrive behavior, per-member speed caps, and
## neighbor separation — so a turning battalion reads as soldiers reforming
## ranks rather than a rigid block rotating. Large heading changes trigger a
## greedy nearest-slot reassignment, which is what produces the retail
## "members cross in front of each other and reform" look.
##
## The engine works in battalion-local space but compensates the root's own
## motion every update, so members are world-stable while steering: existing
## local-position semantics (overlays, attack origins, runners) stay valid.
##
## Update cost is budgeted from the start: steering runs at full rate near the
## camera, reduced cadence at distance, and not at all off-screen.

const CATCH_UP_FACTOR := 1.35
const COMBAT_CATCH_UP_FACTOR := 1.9
const ARRIVE_RADIUS := 1.1
const SETTLE_DISTANCE := 0.14
const SEPARATION_RADIUS := 0.42
const SEPARATION_PUSH := 2.2
const REASSIGN_HEADING_RADIANS := 2.0943951  # 120 degrees
const MEMBER_TURN_RATE := 7.0  # radians/second; infantry pivot briskly
const RUN_ANIM_MIN_SCALE := 0.55
const RUN_ANIM_MAX_SCALE := 1.5
# Members are authored facing +90 degrees from the battalion root convention.
const MEMBER_MODEL_YAW_OFFSET := PI * 0.5

var battalion: Node3D
var locomotor_speed := 5.5
var slot_of: Dictionary = {}
var member_velocities: Dictionary = {}
var _prev_root_transform := Transform3D.IDENTITY
var _root_tracking_ready := false
var _last_heading_yaw := INF
var _reassign_pending := false
var _lod_frame := 0
var _lod_accumulated_delta := 0.0


func configure(owner: Node3D) -> void:
	battalion = owner


func set_locomotor_speed(value: float) -> void:
	locomotor_speed = maxf(0.5, value)


func notify_heading(target_yaw: float) -> void:
	if _last_heading_yaw == INF:
		_last_heading_yaw = target_yaw
		return
	if absf(wrapf(target_yaw - _last_heading_yaw, -PI, PI)) >= REASSIGN_HEADING_RADIANS:
		_reassign_pending = true
	_last_heading_yaw = target_yaw


func request_reassign() -> void:
	_reassign_pending = true


func slot_index(member_index: int) -> int:
	return int(slot_of.get(member_index, member_index))


func update(delta: float) -> void:
	if battalion == null or not battalion.is_inside_tree():
		return
	_lod_accumulated_delta += delta
	_lod_frame += 1
	if _lod_frame % _lod_interval() != 0:
		# Root-motion compensation cannot be skipped or distant members drift
		# with the root; only the steering pass is rate-reduced.
		_compensate_root_motion()
		return
	var step := _lod_accumulated_delta
	_lod_accumulated_delta = 0.0
	_compensate_root_motion()
	if _reassign_pending:
		_reassign_slots()
		_reassign_pending = false
	_steer_members(step)


func _lod_interval() -> int:
	if not battalion.visible:
		return 8
	var camera := battalion.get_viewport().get_camera_3d() if battalion.is_inside_tree() else null
	if camera == null:
		return 1
	var distance := camera.global_position.distance_to(battalion.global_position)
	if distance < 70.0:
		return 1
	if distance < 150.0:
		return 3
	return 6


func _compensate_root_motion() -> void:
	var current := battalion.global_transform
	if not _root_tracking_ready:
		_prev_root_transform = current
		_root_tracking_ready = true
		return
	if current.is_equal_approx(_prev_root_transform):
		return
	# new_local = inv(current_root) * prev_root * old_local keeps each member's
	# world transform fixed while the root moves/turns underneath it.
	var correction := current.affine_inverse() * _prev_root_transform
	var correction_yaw := correction.basis.get_euler().y
	for member_index in battalion.member_visuals.keys():
		var visual := battalion.member_visuals.get(member_index) as Node3D
		if visual == null:
			continue
		visual.position = correction * visual.position
		visual.rotation.y += correction_yaw
	_prev_root_transform = current


func _steer_members(delta: float) -> void:
	if delta <= 0.0:
		return
	var members: Dictionary = battalion.member_visuals
	var state := String(battalion.current_state)
	var battalion_moving := state == "run"
	var positions: Dictionary = {}
	for member_index in members.keys():
		var visual := members.get(member_index) as Node3D
		if visual != null:
			positions[member_index] = visual.position
	for member_index in members.keys():
		if float(battalion.member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		var visual := members.get(member_index) as Node3D
		if visual == null or not visual.visible:
			continue
		var target: Vector3 = battalion.member_presentation_target(slot_index(member_index))
		var offset := Vector3(target.x - visual.position.x, 0.0, target.z - visual.position.z)
		var distance := offset.length()
		# Combat urgency: members hustle into the attack spread instead of
		# strolling into position while the enemy waits.
		var speed_cap := locomotor_speed * (COMBAT_CATCH_UP_FACTOR if state == "attack" else CATCH_UP_FACTOR)
		var desired_speed := speed_cap
		if distance < ARRIVE_RADIUS:
			desired_speed = speed_cap * (distance / ARRIVE_RADIUS)
		var velocity := Vector3.ZERO
		if distance > 0.001:
			velocity = offset / distance * desired_speed
		velocity += _separation(member_index, positions) * SEPARATION_PUSH
		var settled := distance <= SETTLE_DISTANCE and velocity.length() < 0.25
		if settled:
			velocity = Vector3.ZERO
		var step := velocity * delta
		if step.length() > distance + SEPARATION_RADIUS:
			step = step.limit_length(distance + SEPARATION_RADIUS)
		visual.position += step
		member_velocities[member_index] = velocity
		_face_member(visual, velocity, settled, delta)
		_sync_member_gait(member_index, velocity, settled, battalion_moving)


func _separation(member_index: int, positions: Dictionary) -> Vector3:
	var own: Vector3 = positions.get(member_index, Vector3.ZERO)
	var push := Vector3.ZERO
	for other_index in positions.keys():
		if other_index == member_index:
			continue
		if float(battalion.member_health_ratios.get(other_index, 1.0)) <= 0.0:
			continue
		var toward := own - (positions[other_index] as Vector3)
		toward.y = 0.0
		var distance := toward.length()
		if distance > 0.001 and distance < SEPARATION_RADIUS:
			push += toward / distance * (1.0 - distance / SEPARATION_RADIUS)
	return push


func _face_member(visual: Node3D, velocity: Vector3, settled: bool, delta: float) -> void:
	var desired_local_yaw := MEMBER_MODEL_YAW_OFFSET
	if not settled and velocity.length() > locomotor_speed * 0.12:
		desired_local_yaw = atan2(-velocity.x, -velocity.z) + MEMBER_MODEL_YAW_OFFSET
	var difference := wrapf(desired_local_yaw - visual.rotation.y, -PI, PI)
	var max_step := MEMBER_TURN_RATE * delta
	visual.rotation.y += clampf(difference, -max_step, max_step)


func _sync_member_gait(member_index: int, velocity: Vector3, settled: bool, battalion_moving: bool) -> void:
	var action := String(battalion.member_action_states.get(member_index, "idle"))
	if action != "run" and action != "idle":
		return
	var moving := not settled and velocity.length() > locomotor_speed * 0.15
	if moving and action != "run":
		battalion._play_member_state(member_index, "run", -1, false)
	elif not moving and action != "idle" and not battalion_moving:
		battalion._play_member_state(member_index, "idle", -1, false)
	if not moving:
		return
	# Foot-slide fix: run playback rate tracks the member's actual speed so
	# feet match ground travel; the authored stride pace is the locomotor speed.
	var ratio := clampf(velocity.length() / maxf(locomotor_speed, 0.5), RUN_ANIM_MIN_SCALE, RUN_ANIM_MAX_SCALE)
	var jitter := 0.96 + float(posmod(int(battalion.entity_id) * 5 + member_index * 7, 7)) * (0.08 / 6.0)
	for player_value in battalion.member_animation_players.get(member_index, []):
		var player := player_value as AnimationPlayer
		if player != null:
			player.speed_scale = jitter * ratio


func _reassign_slots() -> void:
	# Greedy nearest-slot assignment: on a large heading change members claim
	# the nearest slot of the reoriented formation instead of rotating the
	# block, producing the retail rank-crossing reform.
	var members: Dictionary = battalion.member_visuals
	var living: Array = []
	for member_index in members.keys():
		if float(battalion.member_health_ratios.get(member_index, 1.0)) > 0.0 and members.get(member_index) != null:
			living.append(member_index)
	var free_slots: Array = living.duplicate()
	var new_assignment: Dictionary = {}
	for member_index in living:
		var visual := members.get(member_index) as Node3D
		var best_slot: int = -1
		var best_distance := INF
		for slot_value in free_slots:
			var slot: int = int(slot_value)
			var target: Vector3 = battalion.member_formation_slot(slot)
			var distance := visual.position.distance_squared_to(target)
			if distance < best_distance:
				best_distance = distance
				best_slot = slot
		if best_slot >= 0:
			new_assignment[member_index] = best_slot
			free_slots.erase(best_slot)
	slot_of = new_assignment
