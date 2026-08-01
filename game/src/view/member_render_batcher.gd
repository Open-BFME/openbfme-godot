class_name MemberRenderBatcher
extends Node3D
## Presentation-only scalability layer for battalion member visuals.
##
## Today every battalion member is an individually duplicated skinned GLB
## subtree (Skeleton3D + MeshInstance3D + AnimationPlayer) plus a contact-shadow
## Decal. That is ~6-9 nodes and 2+ draw calls per soldier with no instancing, so
## an army of 10,000 costs ~90,000 nodes and ~10,000 skeleton updates per frame.
##
## This node adds three presentation-only behaviours, driven by camera distance:
##   1. INSTANCING - distant members stop drawing their skinned subtree and are
##      drawn instead as instances of one shared MultiMesh per (model, team).
##      MultiMesh cannot instance a skinned mesh, so skinned_pose_baker.gd bakes
##      a static posed proxy once per group (see that file for the fail-closed
##      contract).
##   2. LOD - contact shadow decals and per-member overlays stop drawing with
##      distance, per member_lod_policy.gd.
##   3. ANIMATION CULLING - distant/hidden members stop advancing their
##      AnimationPlayers, and resync on return with no pose drift.
##
## SIM SEPARATION: this node never reads or writes simulation state. It reads
## presentation nodes (member visuals, animation players) and the battalion's
## already-published presentation fields, and writes only visibility, MultiMesh
## instance transforms, and animation playback position.
##
## NO SILENT FALLBACK: if a model cannot be baked into an instanceable proxy the
## group is recorded in `batch_refusals()`, reported once, and every member of
## that group keeps drawing its full skinned subtree. A refused model costs what
## it costs today - it never silently vanishes or renders wrong.

const PoseBaker = preload("res://src/view/skinned_pose_baker.gd")
const LodPolicy = preload("res://src/view/member_lod_policy.gd")

## Member action states whose clips are one-shot and whose completion drives
## other presentation logic: retail_battalion._settle_finished_corpses() waits
## for the death clip to end, and its _process() returns attacking members to
## idle when the attack clip ends. Freezing these would strand that logic, so
## they are never animation-culled however far away they are.
const NEVER_ANIMATION_CULLED_STATES := [
	"death", "attack", "attack_melee", "attack_ranged_pre", "attack_ranged_fire",
	"selectionTransition", "construct",
]

## MultiMesh instance_count reallocation drops all instance data, so capacity is
## grown in blocks and never shrunk during a match; the live count is carried by
## visible_instance_count, which is cheap to change.
const CAPACITY_BLOCK := 64

## Distant members barely move in screen space, so their instance transforms are
## refreshed every Nth update rather than every frame.
const FAR_TRANSFORM_UPDATE_INTERVAL := 3


class GroupRecord:
	extends RefCounted
	var key := ""
	var mesh_path := ""
	var team := 0
	var multimesh_instance: MultiMeshInstance3D
	var multimesh: MultiMesh
	var baked_vertex_count := 0
	var baked_surface_count := 0
	var capacity := 0
	## CPU-side mirror of the instance transforms written this pass. Required for
	## headless assertions: under the dummy rasterizer MultiMesh discards instance
	## data, so MultiMesh.get_instance_transform() cannot be read back.
	var live_transforms: Array[Transform3D] = []


class BattalionRecord:
	extends RefCounted
	var battalion_ref: WeakRef
	var tier: int = LodPolicy.Tier.NEAR
	var tier_applied := false
	var animation_culled := false
	var culled_seconds := 0.0
	## member_index -> Array of {player, animation, position}
	var suspended_players: Dictionary = {}


var policy := LodPolicy.new()

var _battalion_records: Array[BattalionRecord] = []
var _groups: Dictionary = {}
var _refusals: Dictionary = {}
var _reported_refusals: Dictionary = {}
var _camera: Camera3D
var _frame := 0

# Diagnostics consumed by tests and the perf soak runner.
var batched_member_count := 0
var skinned_member_count := 0
var tier_counts: Dictionary = {}


func _ready() -> void:
	name = "MemberRenderBatcher" if name == "" else name


## Explicit camera injection. Headless runners have no current camera until a
## frame has been processed, and the slice knows its own gameplay camera, so the
## LOD source is passed in rather than discovered.
func set_camera(camera: Camera3D) -> void:
	_camera = camera


func camera() -> Camera3D:
	if _camera != null and is_instance_valid(_camera):
		return _camera
	if is_inside_tree():
		var viewport := get_viewport()
		if viewport != null:
			return viewport.get_camera_3d()
	return null


func register_battalion(battalion: Node3D) -> void:
	if battalion == null or not is_instance_valid(battalion):
		return
	for record in _battalion_records:
		if record.battalion_ref.get_ref() == battalion:
			return
	var record := BattalionRecord.new()
	record.battalion_ref = weakref(battalion)
	_battalion_records.append(record)


func unregister_battalion(battalion: Node3D) -> void:
	for index in range(_battalion_records.size() - 1, -1, -1):
		if _battalion_records[index].battalion_ref.get_ref() == battalion:
			_restore_battalion(_battalion_records[index])
			_battalion_records.remove_at(index)


func registered_battalion_count() -> int:
	return _battalion_records.size()


## Models that could not be turned into an instanceable proxy, and why. Empty on
## a fully batched match. Never cleared silently.
func batch_refusals() -> Dictionary:
	return _refusals.duplicate(true)


func batch_group_count() -> int:
	return _groups.size()


func batch_group_keys() -> Array:
	var keys: Array = _groups.keys()
	keys.sort()
	return keys


## Live instance count for a group, read from the MultiMesh itself.
func batch_instance_count(group_key: String) -> int:
	var group: GroupRecord = _groups.get(group_key)
	if group == null or group.multimesh == null:
		return 0
	return group.multimesh.visible_instance_count


func batch_capacity(group_key: String) -> int:
	var group: GroupRecord = _groups.get(group_key)
	return group.capacity if group != null else 0


## CPU-side mirror of the instance transforms. Assert against this rather than
## MultiMesh.get_instance_transform(), which returns identity under --headless.
func batch_instance_transforms(group_key: String) -> Array[Transform3D]:
	var group: GroupRecord = _groups.get(group_key)
	if group == null:
		return [] as Array[Transform3D]
	return group.live_transforms.duplicate()


func batch_vertex_count(group_key: String) -> int:
	var group: GroupRecord = _groups.get(group_key)
	return group.baked_vertex_count if group != null else 0


func battalion_tier(battalion: Node3D) -> int:
	for record in _battalion_records:
		if record.battalion_ref.get_ref() == battalion:
			return record.tier
	return -1


## Main entry, called once per frame from the slice's presentation sync.
func update(delta: float) -> void:
	_frame += 1
	var active_camera := camera()
	var camera_position := active_camera.global_position if active_camera != null else Vector3.ZERO
	var have_camera := active_camera != null

	for group in _groups.values():
		(group as GroupRecord).live_transforms.clear()

	batched_member_count = 0
	skinned_member_count = 0
	tier_counts = {}

	for index in range(_battalion_records.size() - 1, -1, -1):
		var record := _battalion_records[index]
		var battalion := record.battalion_ref.get_ref() as Node3D
		if battalion == null or not is_instance_valid(battalion) or not battalion.is_inside_tree():
			_battalion_records.remove_at(index)
			continue
		var distance := (
			camera_position.distance_to(battalion.global_position) if have_camera else -1.0
		)
		var tier := policy.classify(distance, battalion.visible)
		tier_counts[tier] = int(tier_counts.get(tier, 0)) + 1
		if tier != record.tier or not record.tier_applied:
			_apply_tier(record, battalion, tier)
		_update_animation_culling(record, battalion, delta)
		if LodPolicy.draws_batched_instances(tier):
			_accumulate_instances(record, battalion)
		else:
			skinned_member_count += int(battalion.member_count)

	_flush_groups()


## Apply the visibility side of a tier change. Runs only on transition, never
## per frame, so a stable camera costs nothing here.
func _apply_tier(record: BattalionRecord, battalion: Node3D, tier: int) -> void:
	record.tier = tier
	record.tier_applied = true
	var group_key := _group_key(battalion)
	var instanceable := (
		LodPolicy.draws_batched_instances(tier) and _ensure_group(battalion, group_key) != null
	)
	var show_skinned := LodPolicy.draws_skinned_geometry(tier) or not instanceable
	if tier == LodPolicy.Tier.CULLED:
		show_skinned = false
	var show_decals := LodPolicy.draws_member_decals(tier)

	for member_index in battalion.member_visuals.keys():
		var visual := battalion.member_visuals.get(member_index) as Node3D
		if visual == null or not is_instance_valid(visual):
			continue
		# A dead member holds a death pose the baked idle proxy cannot represent,
		# so corpses always keep their own geometry until the sim expires them.
		var is_corpse := float(battalion.member_health_ratios.get(member_index, 1.0)) <= 0.0
		for child in visual.get_children():
			if child is Decal:
				(child as Decal).visible = show_decals
			elif child is Node3D:
				(child as Node3D).visible = show_skinned or is_corpse
	_apply_overlay_detail(battalion, tier)


func _apply_overlay_detail(battalion: Node3D, tier: int) -> void:
	# retail_battalion rewrites its own overlay visibility every frame, so it owns
	# the final say; this only publishes the tier for it to honour.
	if "presentation_detail_level" in battalion:
		battalion.presentation_detail_level = (
			0 if LodPolicy.draws_member_overlays(tier) else 1
		)


func _accumulate_instances(record: BattalionRecord, battalion: Node3D) -> void:
	var group_key := _group_key(battalion)
	var group := _ensure_group(battalion, group_key)
	if group == null:
		# Refused model: members kept their skinned geometry in _apply_tier.
		skinned_member_count += int(battalion.member_count)
		return
	var to_local_space := global_transform.affine_inverse()
	for member_index in battalion.member_visuals.keys():
		var visual := battalion.member_visuals.get(member_index) as Node3D
		if visual == null or not is_instance_valid(visual) or not visual.visible:
			continue
		if float(battalion.member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		group.live_transforms.append(to_local_space * visual.global_transform)
		batched_member_count += 1


func _flush_groups() -> void:
	var refresh := _frame % FAR_TRANSFORM_UPDATE_INTERVAL == 0
	for group_value in _groups.values():
		var group := group_value as GroupRecord
		var live := group.live_transforms.size()
		if group.multimesh == null:
			continue
		if live > group.capacity:
			# Growing instance_count discards existing instance data, so every
			# transform is rewritten below regardless of the refresh cadence.
			var wanted := int(ceil(float(live) / float(CAPACITY_BLOCK))) * CAPACITY_BLOCK
			group.capacity = wanted
			group.multimesh.instance_count = wanted
			refresh = true
		group.multimesh.visible_instance_count = live
		group.multimesh_instance.visible = live > 0
		if not refresh:
			continue
		for index in live:
			group.multimesh.set_instance_transform(index, group.live_transforms[index])


func _group_key(battalion: Node3D) -> String:
	return "%s|%d" % [String(battalion.object_id), int(battalion.team)]


## Create (and bake) the MultiMesh group for a battalion's model+team, or return
## null when the model has been refused. Refusals are sticky and reported once.
func _ensure_group(battalion: Node3D, group_key: String) -> GroupRecord:
	if _groups.has(group_key):
		return _groups[group_key]
	if _refusals.has(group_key):
		return null
	var donor := _first_bakeable_member(battalion)
	if donor == null:
		# Not a refusal: the battalion may simply have no member in the tree yet.
		return null
	var result: Array = PoseBaker.bake(donor)
	var baked = result[0]
	var reason := String(result[1])
	if baked == null:
		_record_refusal(group_key, reason)
		return null

	var group := GroupRecord.new()
	group.key = group_key
	group.mesh_path = String(donor.get_meta("mesh_path", ""))
	group.team = int(battalion.team)
	group.baked_vertex_count = baked.vertex_count
	group.baked_surface_count = baked.surface_count
	group.multimesh = MultiMesh.new()
	group.multimesh.transform_format = MultiMesh.TRANSFORM_3D
	group.multimesh.mesh = baked.mesh
	group.multimesh.instance_count = CAPACITY_BLOCK
	group.multimesh.visible_instance_count = 0
	group.capacity = CAPACITY_BLOCK
	group.multimesh_instance = MultiMeshInstance3D.new()
	group.multimesh_instance.name = "MemberBatch_%s_team%d" % [
		String(battalion.object_id).get_file().replace(".", "_"), int(battalion.team)
	]
	group.multimesh_instance.multimesh = group.multimesh
	group.multimesh_instance.visible = false
	# Distant proxies must not re-light the scene differently from the skinned
	# members they stand in for; inherit the battalion's own geometry layers.
	group.multimesh_instance.layers = _donor_layers(donor)
	add_child(group.multimesh_instance)
	_groups[group_key] = group
	return group


func _donor_layers(donor: Node3D) -> int:
	var stack: Array[Node] = [donor]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		if node is GeometryInstance3D:
			return (node as GeometryInstance3D).layers
		for child in node.get_children():
			stack.append(child)
	return 1


func _first_bakeable_member(battalion: Node3D) -> Node3D:
	for member_index in battalion.member_visuals.keys():
		var visual := battalion.member_visuals.get(member_index) as Node3D
		if visual == null or not is_instance_valid(visual) or not visual.is_inside_tree():
			continue
		if float(battalion.member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		return visual
	return null


func _record_refusal(group_key: String, reason: String) -> void:
	_refusals[group_key] = reason
	if _reported_refusals.has(group_key):
		return
	_reported_refusals[group_key] = true
	# Loud and once: a refused group silently costs full price forever, so it
	# must be visible in any log that captured the match.
	push_warning("[MemberRenderBatcher] not instancing '%s': %s" % [group_key, reason])
	print("[MemberRenderBatcher] not instancing '%s': %s" % [group_key, reason])


# ---------------------------------------------------------------- animation --

func _update_animation_culling(record: BattalionRecord, battalion: Node3D, delta: float) -> void:
	var should_cull := LodPolicy.culls_looping_animation(record.tier)
	if should_cull:
		record.culled_seconds += maxf(0.0, delta)
		if not record.animation_culled:
			_suspend_animation(record, battalion)
	elif record.animation_culled:
		_resume_animation(record, battalion)


func _suspend_animation(record: BattalionRecord, battalion: Node3D) -> void:
	record.animation_culled = true
	record.culled_seconds = 0.0
	record.suspended_players.clear()
	for member_index in battalion.member_animation_players.keys():
		var state := String(battalion.member_action_states.get(member_index, "idle"))
		if state in NEVER_ANIMATION_CULLED_STATES:
			continue
		if float(battalion.member_health_ratios.get(member_index, 1.0)) <= 0.0:
			continue
		var saved: Array = []
		for player_value in battalion.member_animation_players.get(member_index, []):
			var player := player_value as AnimationPlayer
			if player == null or not is_instance_valid(player) or not player.is_playing():
				continue
			saved.append({
				"player": player,
				"animation": player.current_animation,
				"position": player.current_animation_position,
			})
			# active=false freezes advancement while leaving is_playing() true and
			# current_animation intact, so retail_battalion's per-frame
			# is_playing() polling does not see the clip as finished and restart
			# it every frame. (Verified against Godot 4.7 headless.)
			player.active = false
		if not saved.is_empty():
			record.suspended_players[member_index] = saved


## Resume with no visible pose drift: each player is advanced to exactly where it
## would have been had it kept running, so members keep the per-member phase
## offsets that stop a battalion animating in lockstep.
func _resume_animation(record: BattalionRecord, battalion: Node3D) -> void:
	var elapsed := record.culled_seconds
	record.animation_culled = false
	record.culled_seconds = 0.0
	for member_index in record.suspended_players.keys():
		for entry_value in record.suspended_players[member_index]:
			var entry: Dictionary = entry_value
			var player := entry["player"] as AnimationPlayer
			if player == null or not is_instance_valid(player):
				continue
			# Order matters: seek() is ignored while active is false.
			player.active = true
			if player.current_animation != String(entry["animation"]):
				# The sim changed this member's state while it was culled, so a
				# fresh clip is already playing with its own phase applied.
				continue
			var length := player.current_animation_length
			if length <= 0.0:
				continue
			var advanced := float(entry["position"]) + elapsed * absf(player.speed_scale)
			player.seek(fposmod(advanced, length), true)
	record.suspended_players.clear()


func _restore_battalion(record: BattalionRecord) -> void:
	var battalion := record.battalion_ref.get_ref() as Node3D
	if battalion == null or not is_instance_valid(battalion):
		return
	if record.animation_culled:
		_resume_animation(record, battalion)
	for member_index in battalion.member_visuals.keys():
		var visual := battalion.member_visuals.get(member_index) as Node3D
		if visual == null or not is_instance_valid(visual):
			continue
		for child in visual.get_children():
			if child is Node3D:
				(child as Node3D).visible = true
	_apply_overlay_detail(battalion, LodPolicy.Tier.NEAR)


## Drop every batch group and restore every registered battalion to full detail.
func reset() -> void:
	for record in _battalion_records:
		_restore_battalion(record)
	_battalion_records.clear()
	for group_value in _groups.values():
		var group := group_value as GroupRecord
		if group.multimesh_instance != null and is_instance_valid(group.multimesh_instance):
			group.multimesh_instance.queue_free()
	_groups.clear()
	_refusals.clear()
	_reported_refusals.clear()
	batched_member_count = 0
	skinned_member_count = 0
	tier_counts = {}
