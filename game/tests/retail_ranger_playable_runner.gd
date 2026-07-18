extends SceneTree

const RANGER_MEMBER_ID := "bfme2.object.gondor-ranger"
const RANGER_HORDE_ID := "bfme2.object.gondor-ranger-horde"
const RANGER_CAPABILITY_ID := "bfme2.animation.gondor-ranger"
const ARCHERY_LEVEL_TWO_ID := "Upgrade_GondorArcheryRangeLevel2"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check("slice_ready_with_ranger_overlay", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.queue_free()
		await process_frame
		_finish()
		return

	var content_db = root.get_node("/root/ContentDB")
	var runtime: Dictionary = content_db.get_ranger_runtime()
	var member: Dictionary = content_db.get_bundle_object(RANGER_MEMBER_ID)
	var horde: Dictionary = content_db.get_bundle_object(RANGER_HORDE_ID)
	var capability: Dictionary = content_db.get_animation_capability(RANGER_CAPABILITY_ID)
	var member_root := String(member.get("_pack_root", ""))
	var mod_loader = root.get_node("/root/ModLoader")
	var overlay_meta: Dictionary = mod_loader._read_json(member_root.path_join("pack.json")) as Dictionary
	var reviewed_overlay_sha := OS.get_environment("OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256").to_lower()
	_check(
		"overlay_registers_typed_ranger_content",
		String(runtime.get("schema", "")) == "openbfme.ranger-runtime-contract"
			and String(member.get("kind", "")) == "member"
			and String(horde.get("kind", "")) == "battalion"
			and String(member.get("animationCapabilityId", "")) == RANGER_CAPABILITY_ID
			and String(overlay_meta.get("id", "")) == "bfme2-men-ranger-overlay"
			and reviewed_overlay_sha.length() == 64
			and String(runtime.get("_reviewed_content_sha256", "")) == reviewed_overlay_sha
			and String(runtime.get("_pack_root", "")) == member_root
			and String(horde.get("_pack_root", "")) == member_root
			and String(capability.get("_pack_root", "")) == member_root
	)
	var states: Dictionary = capability.get("states", {}) as Dictionary
	_check(
		"converted_core_clips_are_bound",
		_clips(states, "idle") == ["guranger_idla"]
			and _clips(states, "move") == ["guranger_runa"]
			and _clips(states, "attack") == ["guranger_atkd1"]
			and _clips(states, "death") == ["guranger_diea"]
	)

	var sim = slice.simulation
	sim.ai_enabled = false
	var archery: int = sim.producer_id(0, "archery_range")
	slice.selected_structure_id = archery
	sim.selected_ids.clear()
	slice._refresh_hud()
	var ranger_button := slice.hud.train_buttons.get(RANGER_HORDE_ID) as Button
	var upgrade_button := slice.hud.unit_action_buttons.get("upgrade_archery_range_level2") as Button
	_check(
		"level_one_hud_shows_locked_ranger_and_upgrade",
		ranger_button != null and ranger_button.visible and ranger_button.disabled and ranger_button.icon != null
			and upgrade_button != null and upgrade_button.visible and not upgrade_button.disabled and upgrade_button.icon != null
	)
	if ranger_button == null or upgrade_button == null:
		slice.queue_free()
		await process_frame
		_finish()
		return
	var locked: Dictionary = sim.queue_unit(0, archery, RANGER_HORDE_ID)
	_check(
		"ranger_training_fails_closed_before_level_two",
		not bool(locked.get("ok", true))
			and String(locked.get("reason", "")) == "missing-upgrade"
			and String(locked.get("required_upgrade", "")) == ARCHERY_LEVEL_TWO_ID
	)

	upgrade_button.pressed.emit()
	_check(
		"player_upgrade_command_queues_source_contract",
		sim.structure_upgrade_queue_state(archery).size() == 1
			and sim.resources_for_team(0) == 700
	)
	sim.advance(300)
	slice._sync_presentation()
	_check(
		"archery_range_reaches_level_two",
		int(sim.structure(archery).get("level", 0)) == 2
			and Array(sim.structure(archery).get("completed_upgrades", [])).has(ARCHERY_LEVEL_TWO_ID)
			and not ranger_button.disabled
			and not upgrade_button.visible
	)

	var resources_before_ranger: int = sim.resources_for_team(0)
	ranger_button.pressed.emit()
	var queued: Array[Dictionary] = sim.production_queue_state(archery)
	_check(
		"player_ranger_command_spends_source_cost",
		queued.size() == 1
			and String(queued[0].get("unit_type", "")) == RANGER_HORDE_ID
			and int(queued[0].get("cost", -1)) == 600
			and int(queued[0].get("command_points", -1)) == 70
			and sim.resources_for_team(0) == resources_before_ranger - 600
	)
	sim.advance(300)
	var ranger_id := 10
	var ranger: Dictionary = sim.entity(ranger_id)
	_check(
		"ranger_battalion_has_source_gameplay_values",
		String(ranger.get("object_id", "")) == RANGER_MEMBER_ID
			and String(ranger.get("unit_type", "")) == RANGER_HORDE_ID
			and int(ranger.get("member_count", 0)) == 10
			and int(ranger.get("member_maximum_health", 0)) == 300
			and int(ranger.get("command_points", 0)) == 70
			and int(ranger.get("member_damage", 0)) == 65
			and is_equal_approx(float(ranger.get("attack_range_source", 0.0)), 400.0)
			and int(ranger.get("clip_size", 0)) == 1
			and is_equal_approx(float(ranger.get("clip_reload_time_ms", 0.0)), 1366.0)
			and int(ranger.get("attack_period_ticks", 0)) == 18
			and int(ranger.get("continuous_fire_one", 0)) == 2
			and int(ranger.get("continuous_fire_coast_ticks", 0)) == 20
			and is_equal_approx(float(ranger.get("continuous_fire_rate_multiplier", 0.0)), 2.25)
			and sim.command_points_for_team(0) == 190,
		str(ranger)
	)
	# Retail holds a newly created horde at the production door before normal
	# player orders become authoritative.
	sim.advance(18)
	slice._sync_presentation()
	await process_frame
	var battalion = slice.battalion_nodes.get(ranger_id)
	_check(
		"ranger_battalion_mounts_ten_converted_models",
		battalion != null
			and String(battalion.retail_model_filename) == "gondorranger.glb"
			and int(battalion.member_count) == 10
			and int(battalion.retail_visual_count) == 10
			and int(battalion.rigged_member_count) == 10
			and String(battalion.weapon_launch_bone) == "ARROW"
			and battalion._first_skeleton(battalion).find_bone("ARROW") >= 0
			and battalion._first_skeleton(battalion).find_bone("ARROWNOCK_2") >= 0
			and bool(battalion.combat_visual_source_closure_present)
			and battalion.source_selection_decal != null
			and bool(battalion.source_selection_decal.contract_ready),
		str({
			"overlay": battalion.member_overlay_status if battalion != null else "missing",
			"launch_bone": battalion.weapon_launch_bone if battalion != null else "missing",
			"bones": _skeleton_bones(battalion._first_skeleton(battalion)) if battalion != null else [],
		})
	)
	if battalion == null:
		slice.queue_free()
		await process_frame
		_finish()
		return

	_check("player_can_select_ranger", sim.select_only(ranger_id))
	slice.selected_structure_id = 0
	slice._sync_presentation()
	_check(
		"ranger_selection_uses_retail_portrait",
		bool(battalion.selected)
			and String(slice.hud.selection_portrait.get_meta("retail_active_portrait_unit_id", "")) == RANGER_HORDE_ID
	)
	var start := Vector2(sim.entity(ranger_id).get("position", Vector2.ZERO))
	var ranger_ids: Array[int] = [ranger_id]
	var move_count: int = sim.issue_move(ranger_ids, start + Vector2(3.0, 0.0))
	sim.advance(2)
	_check(
		"player_can_move_ranger",
		move_count == 1 and Vector2(sim.entity(ranger_id).get("position", start)).distance_to(start) > 0.0
	)

	var enemy_id := 101
	(sim.entities[enemy_id] as Dictionary)["speed"] = 0.0
	(sim.entities[enemy_id] as Dictionary)["acceleration"] = 0.0
	var ranger_position := Vector2(sim.entity(ranger_id).get("position", start))
	var switch_distance := float(ranger.get("close_weapon_switch_distance", 0.0))
	var enemy_health_before := int(sim.entity(enemy_id).get("health", 0))
	(sim.entities[enemy_id] as Dictionary)["position"] = ranger_position + Vector2(switch_distance - 0.01, 0.0)
	sim.issue_attack(ranger_ids, enemy_id)
	sim.advance(5)
	(sim.entities[enemy_id] as Dictionary)["position"] = Vector2(sim.entity(ranger_id).get("position", start)) + Vector2(switch_distance, 0.0)
	sim.advance(3)
	_check(
		"deferred_sword_path_fails_closed_at_and_inside_source_boundary",
		is_equal_approx(float(ranger.get("close_weapon_switch_distance_source", 0.0)), 24.0)
			and int(sim.entity(enemy_id).get("health", 0)) == enemy_health_before
			and not _has_event(sim.events, "combat.member_fire", ranger_id)
			and not _has_event(sim.events, "combat.hit", ranger_id)
	)
	(sim.entities[enemy_id] as Dictionary)["position"] = Vector2(sim.entity(ranger_id).get("position", start)) + Vector2(switch_distance + 1.6, 0.0)
	var attack_count: int = sim.issue_attack(ranger_ids, enemy_id)
	sim.advance(8)
	var fired := _has_event(sim.events, "combat.swing", ranger_id)
	var hit := _has_event(sim.events, "combat.hit", ranger_id)
	_check("player_can_attack_with_ranger_bow", attack_count == 1 and fired and hit, str(sim.entity(ranger_id)))
	slice._sync_presentation()
	_check(
		"ranger_attack_uses_bow_presentation",
		String(battalion.current_state) == "attack"
			and int(battalion.archer_projectiles_presented) > 0
			and _active_projectiles_use_launch_bone(battalion.archer_projectile_controller, "ARROW")
	)
	sim.advance(30)
	var swing_ticks := _event_ticks(sim.events, "combat.swing", ranger_id)
	_check(
		"ranger_clip_reload_and_continuous_fire_drive_cadence",
		swing_ticks.size() >= 3
			and swing_ticks[1] - swing_ticks[0] == 18
			and swing_ticks[2] - swing_ticks[1] == 8,
		str(swing_ticks)
	)

	var routes_ok := true
	var route_results: Dictionary = {}
	for kind in ["select", "move", "attack", "death"]:
		var result: Dictionary = slice.audio_system.route_roster_voice(RANGER_MEMBER_ID, kind, 1)
		route_results[kind] = result
		routes_ok = routes_ok and bool(result.get("ok", false))
	var created_result: Dictionary = slice.audio_system.route_audio_event("RangerVoiceSalute", 1)
	route_results["RangerVoiceSalute"] = created_result
	routes_ok = routes_ok and bool(created_result.get("ok", false))
	var purchase_result: Dictionary = slice.audio_system.route_audio_event("GondorArcherVoiceBuy", 1)
	route_results["GondorArcherVoiceBuy"] = purchase_result
	var authored_purchase_silence := (
		String(((runtime.get("audioRoutes", {}) as Dictionary).get("purchase", {}) as Dictionary).get("id", ""))
			== "GondorArcherVoiceBuy"
		and not bool(purchase_result.get("ok", true))
		and String(purchase_result.get("reason", "")) == "event_has_no_samples"
	)
	var routes_contained := true
	for result_value in route_results.values():
		var result: Dictionary = result_value
		if bool(result.get("ok", false)):
			routes_contained = routes_contained and mod_loader.path_is_within(
				String(slice.selected_pack_root), String(result.get("path", ""))
			)
	_check(
		"ranger_voices_resolve_and_authored_purchase_stays_silent",
		routes_ok and authored_purchase_silence and routes_contained,
		str(route_results)
	)

	sim._apply_damage(enemy_id, ranger_id, 300)
	_check(
		"ranger_member_death_is_independent",
		_count_dead_members(sim.entity(ranger_id)) == 1
			and _has_event(sim.events, "battalion.member_defeated", enemy_id)
	)
	sim._apply_damage(enemy_id, ranger_id, 2700)
	_check(
		"ranger_defeat_releases_command_points",
		int(sim.entity(ranger_id).get("health", -1)) == 0
			and String(sim.entity(ranger_id).get("state", "")) == "death"
			and sim.command_points_for_team(0) == 120
			and _has_event(sim.events, "battalion.defeated", enemy_id)
	)
	slice._sync_presentation()
	_check("ranger_death_reaches_presentation", String(battalion.current_state) == "death")

	slice.queue_free()
	await process_frame
	_finish()


func _clips(states: Dictionary, state: String) -> Array:
	return Array((states.get(state, {}) as Dictionary).get("clips", []))


func _has_event(events: Array, kind: String, entity_id: int) -> bool:
	for value in events:
		var event: Dictionary = value as Dictionary
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			return true
	return false


func _count_dead_members(entity: Dictionary) -> int:
	var count := 0
	for health in Array(entity.get("member_health", [])):
		count += int(int(health) <= 0)
	return count


func _event_ticks(values: Array, kind: String, entity_id: int) -> Array[int]:
	var result: Array[int] = []
	for value in values:
		var event: Dictionary = value as Dictionary
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			result.append(int(event.get("tick", -1)))
	return result


func _active_projectiles_use_launch_bone(controller: Node, expected: String) -> bool:
	if controller == null:
		return false
	var found := false
	for child in controller.get_children():
		if child.has_meta("launch_bone"):
			found = true
			if String(child.get_meta("launch_bone")) != expected:
				return false
	return found


func _skeleton_bones(skeleton: Skeleton3D) -> Array[String]:
	var result: Array[String] = []
	if skeleton == null:
		return result
	for index in range(skeleton.get_bone_count()):
		result.append(skeleton.get_bone_name(index))
	return result


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_RANGER_PLAYABLE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_RANGER_PLAYABLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_RANGER_PLAYABLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
