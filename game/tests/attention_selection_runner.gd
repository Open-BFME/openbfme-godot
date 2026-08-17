extends SceneTree
## Selection presentation + at-attention animation.
##
## Retail truth (PURE RETAIL rotwk tree,
## workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * object/goodfaction/units/men/gondorfighter.ini:519-531
##       AnimationState = SELECTED, StateName = STATE_Selected, loop ATNB
##       (GUManMocap_ATNB). Script: if previous state was STATE_Idle, play
##       TRANS_IdleToSelected.
##   * object/goodfaction/units/men/gondorfighter.ini:664-670
##       TransitionState TRANS_IdleToSelected plays GUManMocap_ATNA once.
##   * object/goodfaction/units/men/gondorarcher.ini:499-514 / :530-536
##       Same pair: GUArcher_ATNB loop, GUArcher_ATNA once.
##
## The pack compiler used to emit SELECTED as unmapped-runtime-state, so
## content_db never projected selectionTransition / selected. The battalion
## then snapped back to idle the moment the one-shot ended.

const Pick = preload("res://src/retail_slice/retail_selection_pick.gd")
const BATTALION_SCRIPT_PATH := "res://src/retail_slice/retail_battalion.gd"
const STRUCTURE_SCRIPT_PATH := "res://src/retail_slice/retail_structure.gd"
const FORDS_SOURCE_SCALE := 0.026492

const EXPECTED_TESTS := 5
const EXPECTED_CHECKS := 29

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0
var _completed: Array[String] = []


func _initialize() -> void:
	_watchdog.start(self, "ATTENTION_SELECTION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_content_db_projects_selected_from_authored_states()
	_test_select_plays_transition_then_loops_selected()
	_test_move_order_leaves_selected()
	_test_battalion_always_has_a_selection_ring()
	_test_structure_ring_matches_pick_and_visual_center()

	if _completed.size() != EXPECTED_TESTS:
		failed += 1
		push_error("ATTENTION_SELECTION_FAIL liveness: %d/%d tests reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		push_error("ATTENTION_SELECTION_FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, EXPECTED_CHECKS
		])
	print("ATTENTION_SELECTION_RESULT passed=%d failed=%d tests=%d checks=%d" % [
		passed, failed, _completed.size(), ran
	])
	quit(0 if failed == 0 else 1)


func _test_content_db_projects_selected_from_authored_states() -> void:
	## Existing packs already converted the clips. Projection must not wait
	## for a republish: read authoredAnimationStates (semanticState is null
	## on current rotwk-men-vslice) and emit selectionTransition + selected.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_autoload_available", db != null)
	if db == null:
		_completed.append("content-db-projection")
		return
	var document := {
		"objectId": "GondorFighterHorde",
		"_pack_root": "fixture",
		"_source": "fixture",
		"registration": {
			"composition": {"primaryMemberObjectId": "GondorFighter"},
			"visual": {
				"components": [{"default": true, "output": "assets/models/fixture.glb"}],
				"coreAnimations": {
					"idle": [{"identifier": "GUManMocap_IDLA", "conditions": []}],
					"move": [{"identifier": "GUManMocap_RUNA", "conditions": ["MOVING"]}],
					"attack": [{"identifier": "GUManMocap_ATKA", "conditions": ["FIRING_OR_PREATTACK_A"]}],
					"death": [{"identifier": "GUManMocap_DIEA", "conditions": ["DYING"]}],
				},
				"authoredAnimationStates": [
					{
						"identifier": "GUManMocap_ATNA",
						"conditions": ["TRANS_IdleToSelected"],
						"semanticState": null,
						"runtimeSupport": "packaged-unimplemented",
					},
					{
						"identifier": "GUManMocap_ATNB",
						"conditions": ["SELECTED"],
						"semanticState": null,
						"runtimeSupport": "packaged-unimplemented",
					},
					{
						"identifier": "GUManMocap_ATND",
						"conditions": ["TRANS_SelectedToIdle"],
						"semanticState": null,
						"runtimeSupport": "packaged-unimplemented",
					},
					{
						"identifier": "GUManMocap_IDLA",
						"conditions": ["SELECTED"],
						"semanticState": null,
						"runtimeSupport": "packaged-unimplemented",
					},
				],
			},
		},
	}
	var projection: Dictionary = db._playable_unit_projection(document)
	_check("projection_is_non_empty", not projection.is_empty())
	var states: Dictionary = ((projection.get("capability", {}) as Dictionary).get("states", {}) as Dictionary)
	_check("projects_selection_transition", states.has("selectionTransition"))
	_check("projects_selected_loop", states.has("selected"))
	var transition_clips: Array = ((states.get("selectionTransition", {}) as Dictionary).get("clips", []) as Array)
	var selected_clips: Array = ((states.get("selected", {}) as Dictionary).get("clips", []) as Array)
	_check("transition_clip_is_atna", transition_clips.has("GUManMocap_ATNA"))
	_check("selected_clip_is_atnb", selected_clips.has("GUManMocap_ATNB"))
	_check("selected_prefers_atnb_over_idla", String(selected_clips[0]) == "GUManMocap_ATNB" if not selected_clips.is_empty() else false)
	_check("selected_to_idle_is_not_projected", not selected_clips.has("GUManMocap_ATND") and not transition_clips.has("GUManMocap_ATND"))
	_check(
		"selected_mode_is_loop",
		String((states.get("selected", {}) as Dictionary).get("mode", "")) == "loop"
	)
	_check(
		"transition_mode_is_once",
		String((states.get("selectionTransition", {}) as Dictionary).get("mode", "")) == "once"
	)
	_completed.append("content-db-projection")


func _test_select_plays_transition_then_loops_selected() -> void:
	var battalion := _make_attention_battalion("GUManMocap_ATNA", "GUManMocap_ATNB")
	if battalion == null:
		_completed.append("select-attention")
		return
	battalion.set_action_state("idle", true)
	_check("starts_idle", String(battalion.member_action_states.get(0, "")) == "idle")
	battalion.set_selected(true)
	_check(
		"select_enters_selection_transition",
		String(battalion.member_action_states.get(0, "")) == "selectionTransition"
	)
	_check(
		"transition_clip_is_bound",
		String(battalion.member_current_clips.get(0, "")).contains("ATNA")
	)
	_finish_one_shot(battalion, 0)
	_check(
		"transition_settles_to_selected_not_idle",
		String(battalion.member_action_states.get(0, "")) == "selected"
	)
	_check(
		"selected_clip_is_atnb",
		String(battalion.member_current_clips.get(0, "")).contains("ATNB")
	)
	battalion.queue_free()
	_completed.append("select-attention")


func _test_move_order_leaves_selected() -> void:
	var battalion := _make_attention_battalion("GUArcher_ATNA", "GUArcher_ATNB")
	if battalion == null:
		_completed.append("leave-attention")
		return
	battalion.set_action_state("idle", true)
	battalion.set_selected(true)
	_finish_one_shot(battalion, 0)
	_check("archer_is_at_attention", String(battalion.member_action_states.get(0, "")) == "selected")
	battalion.set_action_state("run", true)
	_check("move_order_leaves_selected", String(battalion.member_action_states.get(0, "")) == "run")
	_check("run_clip_is_bound", String(battalion.member_current_clips.get(0, "")).contains("RUNA"))
	battalion.queue_free()
	_completed.append("leave-attention")


func _test_battalion_always_has_a_selection_ring() -> void:
	## Heroes / porter / infantry must show a ring when selected even if the
	## SHADOW_MERGE_DECAL contract fails to bind. The ring sits under the live
	## visual, not at a stale origin off to the side.
	var battalion_script: GDScript = load(BATTALION_SCRIPT_PATH)
	_check("battalion_script_compiles_for_ring", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		_completed.append("selection-ring")
		return
	var battalion: Node3D = battalion_script.new()
	root.add_child(battalion)
	battalion.private_parity_mode_active = true
	battalion.member_count = 1
	battalion._source_unit_scale = FORDS_SOURCE_SCALE
	battalion._member_selection_radius = Pick.world_radius_from_source(8.0, FORDS_SOURCE_SCALE)
	var visual := Node3D.new()
	battalion.add_child(visual)
	visual.position = Vector3(1.4, 0.0, -0.6)
	## Retail members are yawed +90° (`retail_battalion.gd` _build_members).
	## A local AABB centre of (0.25, 0, -0.10) must land at (-0.10, 0, -0.25)
	## after that yaw — adding the local offset raw parks the ring on the side.
	visual.rotation.y = PI * 0.5
	battalion.member_visuals[0] = visual
	battalion.member_health_ratios[0] = 1.0
	battalion.member_visual_centers[0] = Vector3(0.25, 0.0, -0.10)
	battalion.source_selection_decal = null
	battalion._ensure_synthetic_member_selection_rings()
	_check("parity_mode_still_builds_a_ring_when_decal_is_absent", battalion.member_selection_rings.has(0))
	battalion.set_selected(true)
	var ring: MeshInstance3D = battalion.member_selection_rings.get(0)
	_check("ring_is_visible_when_selected", ring != null and ring.visible)
	if ring != null:
		battalion._update_legal_safe_member_overlays()
		var expected := visual.position + visual.basis * Vector3(0.25, 0.0, -0.10)
		_check(
			"ring_centers_on_the_live_visual",
			absf(ring.position.x - expected.x) < 0.001 and absf(ring.position.z - expected.z) < 0.001
		)
	else:
		_check("ring_centers_on_the_live_visual", false)
	battalion.queue_free()
	_completed.append("selection-ring")


func _test_structure_ring_matches_pick_and_visual_center() -> void:
	var structure_script: GDScript = load(STRUCTURE_SCRIPT_PATH)
	_check("structure_script_compiles_for_ring", structure_script != null and structure_script.can_instantiate())
	if structure_script == null or not structure_script.can_instantiate():
		_completed.append("structure-ring")
		return
	var structure: Node3D = structure_script.new()
	root.add_child(structure)
	structure.structure_kind = "well"
	structure.health_ratio = 1.0
	structure.selection_radius_source = "compiled-retail-geometry"
	structure.pick_radius = Pick.world_radius_from_source(35.6, FORDS_SOURCE_SCALE)
	structure._source_unit_scale = FORDS_SOURCE_SCALE
	structure._build_markers()
	var visual_bounds := AABB(Vector3(-12.0, 0.0, 18.0), Vector3(40.0, 20.0, 40.0))
	structure._apply_visual_bounds_selection_radius(visual_bounds, FORDS_SOURCE_SCALE)
	structure.set_selected(true)
	var ring: MeshInstance3D = structure._selection_ring
	_check("structure_builds_a_selection_ring", ring != null)
	if ring == null:
		_completed.append("structure-ring")
		structure.queue_free()
		return
	_check("structure_ring_is_visible_when_selected", ring.visible)
	var mesh := ring.mesh as TorusMesh
	_check("structure_ring_is_sized_to_the_pick_radius", mesh != null and absf(mesh.inner_radius - structure.pick_radius) < 0.02)
	var expected_center := Vector2(
		(visual_bounds.position.x + visual_bounds.size.x * 0.5) * FORDS_SOURCE_SCALE,
		(visual_bounds.position.z + visual_bounds.size.z * 0.5) * FORDS_SOURCE_SCALE
	)
	_check(
		"structure_ring_centers_on_the_visual",
		absf(ring.position.x - expected_center.x) < 0.02 and absf(ring.position.z - expected_center.y) < 0.02
	)
	structure.queue_free()
	_completed.append("structure-ring")


func _make_attention_battalion(transition_clip: String, selected_clip: String) -> Node3D:
	var battalion_script: GDScript = load(BATTALION_SCRIPT_PATH)
	_check("battalion_script_compiles", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		return null
	var battalion: Node3D = battalion_script.new()
	root.add_child(battalion)
	battalion.member_count = 1
	battalion.health_ratio = 1.0
	battalion.current_state = "idle"
	battalion.clip_sets = {
		"idle": ["GUManMocap_IDLA"],
		"run": ["GUManMocap_RUNA"],
		"attack": ["GUManMocap_ATKA"],
		"death": ["GUManMocap_DIEA"],
		"selectionTransition": [transition_clip],
		"selected": [selected_clip],
	}
	battalion.clip_map = {
		"idle": "GUManMocap_IDLA",
		"run": "GUManMocap_RUNA",
		"attack": "GUManMocap_ATKA",
		"death": "GUManMocap_DIEA",
		"selectionTransition": transition_clip,
		"selected": selected_clip,
	}
	var visual := Node3D.new()
	battalion.add_child(visual)
	battalion.member_visuals[0] = visual
	battalion.member_health_ratios[0] = 1.0
	battalion.member_action_states[0] = "idle"
	var player := AnimationPlayer.new()
	visual.add_child(player)
	var library := AnimationLibrary.new()
	for clip_name in ["GUManMocap_IDLA", "GUManMocap_RUNA", "GUManMocap_ATKA", "GUManMocap_DIEA", transition_clip, selected_clip]:
		var animation := Animation.new()
		animation.length = 0.15 if String(clip_name).contains("ATNA") else 0.80
		animation.loop_mode = Animation.LOOP_LINEAR if String(clip_name).contains("ATNB") or String(clip_name).contains("IDLA") or String(clip_name).contains("RUNA") else Animation.LOOP_NONE
		library.add_animation(clip_name, animation)
	player.add_animation_library("", library)
	battalion.member_animation_players[0] = [player]
	battalion.animation_players.append(player)
	return battalion


func _finish_one_shot(battalion: Node3D, member_index: int) -> void:
	for player_value in battalion.member_animation_players.get(member_index, []):
		var player := player_value as AnimationPlayer
		if player != null and player.is_playing():
			player.seek(player.current_animation_length, true)
			player.stop()
	battalion._process(0.05)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("ATTENTION_SELECTION_FAIL %s" % label)
