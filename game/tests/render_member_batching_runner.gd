extends SceneTree
## Headless gate for the presentation scalability layer:
##   src/view/member_lod_policy.gd      - distance -> LOD tier
##   src/view/skinned_pose_baker.gd     - skinned GLB -> static posed proxy
##   src/view/member_render_batcher.gd  - MultiMesh instancing + animation culling
##
## FPS cannot be measured headless, so this asserts the STRUCTURAL properties
## that make the frame cheap: that N battalions collapse into a small fixed
## number of MultiMesh groups, that instance counts and transforms are exact,
## that decals/overlays drop with distance, and that culled animation resumes
## without pose drift.
##
## Headless caveat encoded below: under the dummy rasterizer a MultiMesh discards
## instance data, so MultiMesh.get_instance_transform() reads back identity.
## Transform assertions therefore use the batcher's CPU-side mirror
## (batch_instance_transforms), while instance/visible counts - which do survive -
## are read from the MultiMesh itself.

const BatcherScript = preload("res://src/view/member_render_batcher.gd")
const PolicyScript = preload("res://src/view/member_lod_policy.gd")
const BakerScript = preload("res://src/view/skinned_pose_baker.gd")

const FIGHTER := "bfme2.object.gondor-fighter"
const ARCHER := "bfme2.object.gondor-archer"

var passed := 0
var failed := 0

var BattalionScript: Script
var asset_factory


func _initialize() -> void:
	_run()


func _run() -> void:
	# ContentDB resolves pack content on its first frames; nothing below can
	# load a real member GLB before that has happened.
	await process_frame
	await process_frame
	BattalionScript = load("res://src/retail_slice/retail_battalion.gd")
	asset_factory = load("res://src/view/asset_factory.gd")

	_test_lod_policy()
	_test_health_overlay_distance_gate()
	await _test_pose_baker()
	await _test_batching()
	await _test_lod_visibility()
	await _test_animation_culling()
	await _test_refusal_is_explicit()

	print("RENDER_MEMBER_BATCHING_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# ------------------------------------------------------------------ policy --

func _test_lod_policy() -> void:
	var policy = PolicyScript.new()
	policy.configure(70.0, 150.0, 320.0)
	_check("policy_near_below_near_distance", policy.classify(10.0) == PolicyScript.Tier.NEAR)
	_check("policy_mid_between_thresholds", policy.classify(100.0) == PolicyScript.Tier.MID)
	_check("policy_far_beyond_mid", policy.classify(200.0) == PolicyScript.Tier.FAR)
	_check("policy_culled_beyond_far", policy.classify(9000.0) == PolicyScript.Tier.CULLED)
	_check("policy_invisible_battalion_is_culled", policy.classify(1.0, false) == PolicyScript.Tier.CULLED)
	_check(
		"policy_unknown_distance_keeps_full_detail",
		policy.classify(-1.0) == PolicyScript.Tier.NEAR and policy.classify(NAN) == PolicyScript.Tier.NEAR
	)
	policy.enabled = false
	_check("policy_disabled_restores_full_detail", policy.classify(9000.0) == PolicyScript.Tier.NEAR)
	policy.enabled = true

	# Thresholds stay monotonic even when configured inverted, so a bad config
	# cannot silently invert the tier order.
	policy.configure(300.0, 50.0, 10.0)
	_check(
		"policy_thresholds_stay_monotonic",
		policy.near_distance <= policy.mid_distance and policy.mid_distance <= policy.far_distance
	)
	policy.configure(70.0, 150.0, 320.0)

	_check("policy_near_draws_everything",
		PolicyScript.draws_skinned_geometry(PolicyScript.Tier.NEAR)
		and PolicyScript.draws_member_decals(PolicyScript.Tier.NEAR)
		and PolicyScript.draws_member_overlays(PolicyScript.Tier.NEAR)
		and not PolicyScript.culls_looping_animation(PolicyScript.Tier.NEAR))
	_check("policy_mid_keeps_geometry_drops_decoration",
		PolicyScript.draws_skinned_geometry(PolicyScript.Tier.MID)
		and not PolicyScript.draws_member_decals(PolicyScript.Tier.MID)
		and not PolicyScript.draws_member_overlays(PolicyScript.Tier.MID)
		and not PolicyScript.culls_looping_animation(PolicyScript.Tier.MID))
	_check("policy_far_batches_and_culls_animation",
		not PolicyScript.draws_skinned_geometry(PolicyScript.Tier.FAR)
		and PolicyScript.draws_batched_instances(PolicyScript.Tier.FAR)
		and PolicyScript.culls_looping_animation(PolicyScript.Tier.FAR))
	_check("policy_culled_draws_nothing",
		not PolicyScript.draws_skinned_geometry(PolicyScript.Tier.CULLED)
		and not PolicyScript.draws_batched_instances(PolicyScript.Tier.CULLED)
		and PolicyScript.culls_looping_animation(PolicyScript.Tier.CULLED))


# --------------------------------------------------- health overlay gate --

func _test_health_overlay_distance_gate() -> void:
	var overlay_script = load("res://src/retail_slice/retail_member_health_overlay.gd")
	_check("overlay_gate_draws_close_battalions", overlay_script.should_draw_battalion_at_distance(10.0))
	_check(
		"overlay_gate_draws_at_the_threshold",
		overlay_script.should_draw_battalion_at_distance(overlay_script.SOURCE_MAXIMUM_OVERLAY_DISTANCE)
	)
	_check(
		"overlay_gate_skips_distant_battalions",
		not overlay_script.should_draw_battalion_at_distance(overlay_script.SOURCE_MAXIMUM_OVERLAY_DISTANCE + 1.0)
	)
	# A camera without a usable position must never silently blank the overlay.
	_check("overlay_gate_draws_on_unknown_distance", overlay_script.should_draw_battalion_at_distance(NAN))
	_check("overlay_gate_draws_on_negative_distance", overlay_script.should_draw_battalion_at_distance(-1.0))


# ------------------------------------------------------------------- baker --

func _test_pose_baker() -> void:
	for object_id in [FIGHTER, ARCHER]:
		var visual: Node3D = asset_factory.make_bundle_object_visual(object_id, 0, 0.0)
		if visual == null:
			_check("baker_visual_available_%s" % object_id, false)
			continue
		root.add_child(visual)
		await process_frame
		var result: Array = BakerScript.bake(visual)
		var baked = result[0]
		var reason := String(result[1])
		_check("baker_bakes_%s" % object_id, baked != null, reason)
		if baked != null:
			_check("baker_%s_has_surfaces" % object_id, baked.surface_count > 0 and baked.mesh.get_surface_count() == baked.surface_count)
			_check("baker_%s_has_vertices" % object_id, baked.vertex_count > 0, str(baked.vertex_count))
			_check("baker_%s_baked_a_skinned_surface" % object_id, baked.skinned_surfaces > 0, str(baked.skinned_surfaces))
			# The fail-closed guard: a correct skinning convention reproduces the
			# source vertices at the rest pose to within float noise.
			_check(
				"baker_%s_rest_identity_holds" % object_id,
				baked.rest_identity_max_deviation <= BakerScript.REST_IDENTITY_TOLERANCE,
				"deviation=%.7f" % baked.rest_identity_max_deviation
			)
			var materials_present := true
			for surface in baked.surface_count:
				if baked.mesh.surface_get_material(surface) == null:
					materials_present = false
			_check("baker_%s_preserves_surface_materials" % object_id, materials_present)
			# A static proxy must carry no bone/weight channels or Godot would
			# treat it as skinned again inside the MultiMesh.
			var skin_free := true
			for surface in int(baked.mesh.get_surface_count()):
				var format: int = baked.mesh.surface_get_format(surface)
				if (format & Mesh.ARRAY_FORMAT_BONES) != 0 or (format & Mesh.ARRAY_FORMAT_WEIGHTS) != 0:
					skin_free = false
			_check("baker_%s_proxy_is_not_skinned" % object_id, skin_free)
			# The proxy must occupy the same space as the member it replaces.
			var proxy_aabb: AABB = baked.mesh.get_aabb()
			_check(
				"baker_%s_proxy_matches_member_footprint" % object_id,
				proxy_aabb.size.y > 0.5 and proxy_aabb.size.y < 6.0 and absf(proxy_aabb.position.y) < 0.5,
				str(proxy_aabb)
			)
		visual.queue_free()
	await process_frame

	_check("baker_refuses_null_visual", BakerScript.bake(null)[0] == null)
	var detached := Node3D.new()
	_check("baker_refuses_out_of_tree_visual", BakerScript.bake(detached)[0] == null)
	detached.free()


# ---------------------------------------------------------------- batching --

func _test_batching() -> void:
	var batcher = BatcherScript.new()
	root.add_child(batcher)
	var camera := Camera3D.new()
	root.add_child(camera)
	# Far enough that every battalion classifies FAR, not CULLED.
	camera.global_position = Vector3(0.0, 0.0, 200.0)
	batcher.set_camera(camera)

	# 2 models x 2 teams x 2 battalions each = 8 battalions, 48 members.
	var battalions: Array = []
	var members_per_battalion := 6
	var index := 0
	for object_id in [FIGHTER, ARCHER]:
		for team in [0, 1]:
			for _copy in 2:
				var battalion = _make_battalion(index, team, object_id, members_per_battalion)
				battalion.global_position = Vector3(float(index) * 4.0, 0.0, 0.0)
				battalions.append(battalion)
				batcher.register_battalion(battalion)
				index += 1
	await process_frame

	var total_members := 0
	for battalion in battalions:
		total_members += int(battalion.member_count)
	_check("batching_built_expected_members", total_members == 8 * members_per_battalion, str(total_members))
	_check("batching_registered_all_battalions", batcher.registered_battalion_count() == 8)

	batcher.update(0.016)
	batcher.update(0.016)
	batcher.update(0.016)

	var refusals: Dictionary = batcher.batch_refusals()
	_check("batching_no_refusals_for_retail_models", refusals.is_empty(), str(refusals))

	# THE HEADLINE: 48 individually skinned members collapse to one MultiMesh
	# group per (model, team) - 4 groups, not 48 subtrees.
	_check("batching_groups_are_per_model_and_team", batcher.batch_group_count() == 4, str(batcher.batch_group_keys()))
	var multimesh_nodes := 0
	for child in batcher.get_children():
		if child is MultiMeshInstance3D:
			multimesh_nodes += 1
	_check("batching_emits_one_multimesh_node_per_group", multimesh_nodes == 4, str(multimesh_nodes))

	var summed := 0
	var all_counts: Array = []
	for key in batcher.batch_group_keys():
		var count: int = batcher.batch_instance_count(key)
		all_counts.append("%s=%d" % [key, count])
		summed += count
	_check("batching_instances_cover_every_live_member", summed == total_members, str(all_counts))
	_check("batching_reports_batched_member_count", batcher.batched_member_count == total_members, str(batcher.batched_member_count))
	_check(
		"batching_each_group_holds_its_own_members",
		batcher.batch_instance_count("%s|0" % FIGHTER) == 2 * members_per_battalion
			and batcher.batch_instance_count("%s|1" % ARCHER) == 2 * members_per_battalion,
		str(all_counts)
	)
	_check(
		"batching_capacity_is_blocked_not_per_instance",
		batcher.batch_capacity("%s|0" % FIGHTER) % BatcherScript.CAPACITY_BLOCK == 0
			and batcher.batch_capacity("%s|0" % FIGHTER) >= 2 * members_per_battalion
	)

	# Instance transforms must equal the members' actual world transforms.
	var expected: Array[Transform3D] = []
	for battalion in battalions:
		if String(battalion.object_id) != FIGHTER or int(battalion.team) != 0:
			continue
		for member_index in battalion.member_visuals.keys():
			var visual := battalion.member_visuals.get(member_index) as Node3D
			expected.append(batcher.global_transform.affine_inverse() * visual.global_transform)
	var actual: Array[Transform3D] = batcher.batch_instance_transforms("%s|0" % FIGHTER)
	_check("batching_transform_count_matches", actual.size() == expected.size(), "%d vs %d" % [actual.size(), expected.size()])
	# Order-independent on purpose: the batcher walks its battalion records in
	# reverse so it can drop stale ones mid-iteration, and MultiMesh rendering
	# does not depend on instance order. The property that matters is that the
	# set of instance transforms is exactly the set of member world transforms.
	var unmatched: Array[Transform3D] = actual.duplicate()
	var transforms_match := actual.size() == expected.size()
	var missing := ""
	for wanted in expected:
		var found := -1
		for candidate_index in unmatched.size():
			if unmatched[candidate_index].is_equal_approx(wanted):
				found = candidate_index
				break
		if found < 0:
			transforms_match = false
			missing = str(wanted)
			break
		unmatched.remove_at(found)
	_check("batching_instance_transforms_are_member_world_transforms", transforms_match, missing)

	# Members must have actually stopped drawing their skinned geometry.
	var skinned_visible := 0
	var decals_visible := 0
	for battalion in battalions:
		for member_index in battalion.member_visuals.keys():
			var visual := battalion.member_visuals.get(member_index) as Node3D
			for child in visual.get_children():
				if child is Decal:
					decals_visible += int((child as Decal).visible)
				elif child is Node3D:
					skinned_visible += int((child as Node3D).visible)
	_check("batching_hides_skinned_geometry_at_far_tier", skinned_visible == 0, str(skinned_visible))
	_check("batching_hides_contact_decals_at_far_tier", decals_visible == 0, str(decals_visible))

	# A dead member holds a death pose the idle proxy cannot represent, so it
	# keeps its own geometry and leaves the batch.
	var victim = battalions[0]
	victim.member_health_ratios[0] = 0.0
	batcher.update(0.016)
	batcher.update(0.016)
	batcher.update(0.016)
	_check(
		"batching_excludes_dead_members",
		batcher.batch_instance_count("%s|0" % FIGHTER) == 2 * members_per_battalion - 1,
		str(batcher.batch_instance_count("%s|0" % FIGHTER))
	)

	# Returning the camera to close range must restore full detail exactly.
	camera.global_position = Vector3.ZERO
	batcher.update(0.016)
	batcher.update(0.016)
	batcher.update(0.016)
	var restored_skinned := 0
	var restored_decals := 0
	for battalion in battalions:
		for member_index in battalion.member_visuals.keys():
			var visual := battalion.member_visuals.get(member_index) as Node3D
			for child in visual.get_children():
				if child is Decal:
					restored_decals += int((child as Decal).visible)
				elif child is Node3D:
					restored_skinned += int((child as Node3D).visible)
	_check("batching_restores_skinned_geometry_up_close", restored_skinned == total_members, str(restored_skinned))
	_check("batching_restores_contact_decals_up_close", restored_decals == total_members, str(restored_decals))
	_check("batching_drops_all_instances_up_close", batcher.batched_member_count == 0, str(batcher.batched_member_count))
	var hidden_batches := 0
	for child in batcher.get_children():
		if child is MultiMeshInstance3D and not (child as MultiMeshInstance3D).visible:
			hidden_batches += 1
	_check("batching_hides_empty_multimesh_nodes", hidden_batches == 4, str(hidden_batches))

	batcher.reset()
	for battalion in battalions:
		battalion.queue_free()
	camera.queue_free()
	batcher.queue_free()
	await process_frame


# ---------------------------------------------------------- LOD visibility --

func _test_lod_visibility() -> void:
	var batcher = BatcherScript.new()
	root.add_child(batcher)
	var camera := Camera3D.new()
	root.add_child(camera)
	batcher.set_camera(camera)
	var battalion = _make_battalion(900, 0, FIGHTER, 4)
	battalion.global_position = Vector3.ZERO
	batcher.register_battalion(battalion)
	await process_frame

	# MID tier: skinned members still draw, per-member decoration does not.
	camera.global_position = Vector3(0.0, 0.0, 100.0)
	batcher.update(0.016)
	_check("lod_mid_tier_selected", batcher.battalion_tier(battalion) == PolicyScript.Tier.MID, str(batcher.battalion_tier(battalion)))
	var mid_skinned := 0
	var mid_decals := 0
	for member_index in battalion.member_visuals.keys():
		for child in (battalion.member_visuals[member_index] as Node3D).get_children():
			if child is Decal:
				mid_decals += int((child as Decal).visible)
			elif child is Node3D:
				mid_skinned += int((child as Node3D).visible)
	_check("lod_mid_keeps_skinned_geometry", mid_skinned == battalion.member_count, str(mid_skinned))
	_check("lod_mid_drops_contact_decals", mid_decals == 0, str(mid_decals))
	_check("lod_mid_publishes_reduced_detail_to_battalion", battalion.presentation_detail_level == 1)

	camera.global_position = Vector3.ZERO
	batcher.update(0.016)
	_check("lod_near_tier_selected", batcher.battalion_tier(battalion) == PolicyScript.Tier.NEAR)
	_check("lod_near_restores_full_detail_to_battalion", battalion.presentation_detail_level == 0)

	# An invisible battalion is culled whatever the distance.
	battalion.visible = false
	batcher.update(0.016)
	_check("lod_invisible_battalion_is_culled", batcher.battalion_tier(battalion) == PolicyScript.Tier.CULLED)
	battalion.visible = true
	batcher.update(0.016)

	batcher.reset()
	battalion.queue_free()
	camera.queue_free()
	batcher.queue_free()
	await process_frame


# -------------------------------------------------------- animation culling --

func _test_animation_culling() -> void:
	var batcher = BatcherScript.new()
	root.add_child(batcher)
	var camera := Camera3D.new()
	root.add_child(camera)
	batcher.set_camera(camera)
	var battalion = _make_battalion(910, 0, FIGHTER, 4)
	battalion.global_position = Vector3.ZERO
	batcher.register_battalion(battalion)
	await process_frame

	# Drive real clips directly: the capability map is not wired in this runner,
	# and the property under test is playback advancement, not clip selection.
	var players: Array[AnimationPlayer] = []
	for member_index in battalion.member_animation_players.keys():
		for player_value in battalion.member_animation_players[member_index]:
			players.append(player_value as AnimationPlayer)
	_check("animation_players_present", players.size() >= battalion.member_count, str(players.size()))
	if players.is_empty():
		batcher.reset()
		return

	var clip := ""
	for candidate in players[0].get_animation_list():
		if players[0].get_animation(String(candidate)).length > 0.5:
			clip = String(candidate)
			break
	_check("animation_clip_available", clip != "", clip)

	# Give each member a distinct phase, exactly as _play_member_clip does, so a
	# drifting resync shows up as a battalion snapping into lockstep.
	var length := players[0].get_animation(clip).length
	var phases: Array[float] = []
	for player_index in players.size():
		var player := players[player_index]
		player.speed_scale = 1.0
		player.play(clip)
		player.seek(length * (float(player_index) + 0.5) / float(players.size() + 1), true)
		phases.append(player.current_animation_position)

	camera.global_position = Vector3(0.0, 0.0, 200.0)
	batcher.update(0.016)
	_check("animation_far_tier_selected", batcher.battalion_tier(battalion) == PolicyScript.Tier.FAR)
	_check("animation_suspended_players_report_playing", players[0].is_playing())
	_check("animation_suspended_players_are_inactive", not players[0].active)
	_check("animation_suspended_keeps_current_animation", players[0].current_animation == clip)

	var frozen: Array[float] = []
	for player in players:
		frozen.append(player.current_animation_position)
	await process_frame
	await process_frame
	await process_frame
	var still_frozen := true
	for player_index in players.size():
		if absf(players[player_index].current_animation_position - frozen[player_index]) > 0.0001:
			still_frozen = false
	_check("animation_does_not_advance_while_culled", still_frozen)

	# Accumulate culled time, then bring the battalion back into view.
	var culled_seconds := 0.0
	for _step in 10:
		batcher.update(0.1)
		culled_seconds += 0.1
	camera.global_position = Vector3.ZERO
	batcher.update(0.016)

	_check("animation_resumes_active", players[0].active)
	_check("animation_resumes_playing", players[0].is_playing())
	var resync_ok := true
	var drift_detail := ""
	for player_index in players.size():
		var wanted := fposmod(frozen[player_index] + culled_seconds, length)
		var got := players[player_index].current_animation_position
		if absf(wanted - got) > 0.02:
			resync_ok = false
			drift_detail = "member %d wanted %.4f got %.4f" % [player_index, wanted, got]
	_check("animation_resyncs_to_where_it_would_have_been", resync_ok, drift_detail)

	# The phase spread must survive the round trip: a lockstep battalion is the
	# visible symptom of a resync that snaps everyone to the same position.
	var distinct: Dictionary = {}
	for player in players:
		distinct[snappedf(player.current_animation_position, 0.01)] = true
	_check("animation_preserves_per_member_phase_spread", distinct.size() == players.size(), str(distinct.size()))

	# One-shot states must never be frozen: retail_battalion waits on their end.
	for member_index in battalion.member_action_states.keys():
		battalion.member_action_states[member_index] = "death"
	camera.global_position = Vector3(0.0, 0.0, 200.0)
	batcher.update(0.016)
	var one_shot_still_active := true
	for player in players:
		if not player.active:
			one_shot_still_active = false
	_check("animation_never_culls_one_shot_states", one_shot_still_active)

	batcher.reset()
	battalion.queue_free()
	camera.queue_free()
	batcher.queue_free()
	await process_frame


# ---------------------------------------------------------------- refusals --

func _test_refusal_is_explicit() -> void:
	var batcher = BatcherScript.new()
	root.add_child(batcher)
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.global_position = Vector3(0.0, 0.0, 200.0)
	batcher.set_camera(camera)

	# A member whose geometry is a primitive (the asset_factory multipart kit
	# used when a model will not load) carries no surface arrays to re-emit.
	var battalion = BattalionScript.new()
	battalion.entity_id = 990
	battalion.team = 0
	battalion.object_id = "test.unbakeable"
	battalion.member_count = 3
	root.add_child(battalion)
	for member_index in 3:
		var visual := Node3D.new()
		visual.position = Vector3(float(member_index), 0.0, 0.0)
		var primitive := MeshInstance3D.new()
		primitive.mesh = BoxMesh.new()
		visual.add_child(primitive)
		battalion.add_child(visual)
		battalion.member_visuals[member_index] = visual
		battalion.member_health_ratios[member_index] = 1.0
		battalion.member_action_states[member_index] = "idle"
	batcher.register_battalion(battalion)
	await process_frame

	batcher.update(0.016)
	batcher.update(0.016)
	var refusals: Dictionary = batcher.batch_refusals()
	_check("refusal_is_recorded", refusals.has("test.unbakeable|0"), str(refusals))
	_check(
		"refusal_states_a_reason",
		String(refusals.get("test.unbakeable|0", "")).length() > 10,
		str(refusals.get("test.unbakeable|0", ""))
	)
	_check("refusal_creates_no_batch_group", batcher.batch_group_count() == 0, str(batcher.batch_group_keys()))
	# The explicit, visible fallback: a refused model keeps drawing in full.
	var visible_geometry := 0
	for member_index in battalion.member_visuals.keys():
		for child in (battalion.member_visuals[member_index] as Node3D).get_children():
			if child is Node3D:
				visible_geometry += int((child as Node3D).visible)
	_check("refusal_keeps_full_skinned_geometry_visible", visible_geometry == 3, str(visible_geometry))
	_check("refusal_counts_members_as_unbatched", batcher.batched_member_count == 0, str(batcher.batched_member_count))

	batcher.reset()
	battalion.queue_free()
	camera.queue_free()
	batcher.queue_free()
	await process_frame


# ----------------------------------------------------------------- helpers --

func _make_battalion(id: int, team: int, object_id: String, members: int):
	var battalion = BattalionScript.new()
	var positions: Array = []
	for member_index in members:
		positions.append(Vector3(float(member_index % 3) * 1.2, 0.0, float(member_index / 3) * 1.2))
	root.add_child(battalion)
	battalion.configure(id, team, object_id, {}, members, 0.0, positions)
	return battalion


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RENDER_MEMBER_BATCHING PASS %s" % name)
	else:
		failed += 1
		printerr("RENDER_MEMBER_BATCHING FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
