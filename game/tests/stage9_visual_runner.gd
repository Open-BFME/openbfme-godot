extends SceneTree
## godot --headless --path game -s res://tests/stage9_visual_runner.gd

var passed: int = 0
var failed: int = 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/stage9_lab.tscn")
	_check("stage9_lab_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var lab: Node = packed.instantiate()
	root.add_child(lab)
	current_scene = lab
	await process_frame
	await process_frame
	var world: RefCounted = lab.get("world")
	_check("stage9_real_lab_instantiates", world != null)
	if world == null:
		_finish()
		return
	_test_external_hud_and_no_hardware(lab, world)
	_test_visual_lifecycle(lab, world)
	_test_holder_consequences(lab)
	_test_visual_outcomes(lab)
	var prior: WeakRef = weakref(lab)
	lab.queue_free()
	current_scene = null
	await process_frame
	await process_frame
	_check("stage9_visual_cleanup_releases_scene", prior.get_ref() == null)
	_finish()


func _test_external_hud_and_no_hardware(lab: Node, world: RefCounted) -> void:
	var document: Dictionary = lab.get("rules_document")
	_check("visual_loads_external_auric_loop_rules", String(document.get("schema", "")) == "openbfme.relic-ring-objective" and int(document.get("rulesVersion", 0)) == 1)
	_check("visual_hud_binds_lifecycle_buttons", Dictionary(lab.get("buttons")).size() == 13 and Dictionary(lab.get("buttons")).has("spawn") and Dictionary(lab.get("buttons")).has("victory") and Dictionary(lab.get("buttons")).has("toggle_objective"))
	_check("visual_lab_has_no_audio_hardware_nodes", not _contains_audio_player(lab))
	_check("visual_audio_log_starts_with_explore_music_hook", String((lab.get("audio_label") as Label).text).contains("music.explore"))
	_check("visual_status_exposes_tick_and_hash", String((lab.get("status_label") as Label).text).contains("HASH") and String((lab.get("status_label") as Label).text).contains(world.state_hash_text()))
	var board: Rect2 = lab.call("board_rect")
	var presentation: Dictionary = lab.call("presentation_snapshot")
	_check("visual_board_has_substantial_nonempty_rect", board.size.x >= 800.0 and board.size.y >= 500.0 and board.get_area() > 400000.0)
	_check("visual_board_geometry_is_above_background", int(lab.get_node("Background").z_index) < 0)
	_check("visual_board_snapshot_has_grid_sides_and_strongholds", int(presentation.get("grid_cells", 0)) == 260 and Array(presentation.get("contenders", [])).size() == 2 and Array(presentation.get("strongholds", [])).size() == 2 and String(presentation.get("ring", {}).get("state", "")) == "dormant")
	var toggle_button := Dictionary(lab.get("buttons"))["toggle_objective"] as Button
	toggle_button.emit_signal("pressed")
	_check("visual_optional_objective_disables_for_classic_mode", not world.objective_enabled and String((lab.get("status_label") as Label).text).contains("OBJECTIVE OFF") and String(lab.call("spawn_objective").get("reason", "")) == "objective_disabled")
	toggle_button.emit_signal("pressed")
	_check("visual_optional_objective_reenables", world.objective_enabled)


func _test_visual_lifecycle(lab: Node, world: RefCounted) -> void:
	lab.call("reset_lab")
	world = lab.get("world")
	var dormant_signature: String = String(lab.call("presentation_signature"))
	var buttons: Dictionary = lab.get("buttons")
	(buttons["spawn"] as Button).emit_signal("pressed")
	_check("bound_spawn_button_creates_objective", String(world.ring["state"]) == "spawned")
	var spawn_signature: String = String(lab.call("presentation_signature"))
	var spawn_presentation: Dictionary = lab.call("presentation_snapshot")
	_check("rendered_ring_changes_dormant_to_spawned", spawn_signature != dormant_signature and String(Dictionary(spawn_presentation["ring"])["state"]) == "spawned")
	_check("spawn_audio_id_is_visible", String((lab.get("audio_label") as Label).text).contains("objective.relic.spawn"))
	lab.call("move_team_to_ring", 0)
	var move_signature: String = String(lab.call("presentation_signature"))
	_check("rendered_contender_changes_on_move_order", move_signature != spawn_signature and String(world.entity(int(lab.get("blue_id")))["order"]) == "move")
	lab.call("place_team_at_ring", 0)
	(buttons["claim_blue"] as Button).emit_signal("pressed")
	_check("bound_blue_claim_sets_holder", String(world.ring["state"]) == "held" and int(world.ring["holder_id"]) == int(lab.get("blue_id")))
	var claim_signature: String = String(lab.call("presentation_signature"))
	var claim_presentation: Dictionary = lab.call("presentation_snapshot")
	var claim_juice: Array = claim_presentation["juice"]
	_check("rendered_ring_changes_to_held_with_claim_juice", claim_signature != move_signature and String(Dictionary(claim_presentation["ring"])["state"]) == "held" and String(Dictionary(claim_juice[-1])["kind"]) == "claim")
	_check("holder_consequences_are_readable", String((lab.get("holder_label") as Label).text).contains("SPEED 80%") and String((lab.get("holder_label") as Label).text).contains("DAMAGE 750") and String((lab.get("holder_label") as Label).text).contains("REVEALED YES"))
	(buttons["defeat_holder"] as Button).emit_signal("pressed")
	_check("bound_defeat_holder_drops_objective", String(world.ring["state"]) == "dropped" and int(world.ring["holder_id"]) == 0)
	var drop_presentation: Dictionary = lab.call("presentation_snapshot")
	var drop_juice: Array = drop_presentation["juice"]
	_check("rendered_ring_changes_to_drop_with_clean_juice", String(lab.call("presentation_signature")) != claim_signature and String(Dictionary(drop_presentation["ring"])["state"]) == "dropped" and String(Dictionary(drop_juice[-1])["kind"]) == "drop")
	_check("drop_audio_id_is_visible", String((lab.get("audio_label") as Label).text).contains("objective.relic.drop"))
	_check("clean_hit_feedback_and_event_are_visible", String((lab.get("feedback_label") as Label).text).contains("non-gory") and String((lab.get("audio_label") as Label).text).contains("combat.clean-impact"))
	lab.call("place_team_at_ring", 1)
	_check("visual_early_reclaim_rejected", String(lab.call("claim_for_team", 1).get("reason", "")) == "reclaim_delay")
	(buttons["advance_delay"] as Button).emit_signal("pressed")
	var reclaimed: Dictionary = lab.call("claim_for_team", 1)
	_check("visual_reclaim_after_exact_delay", bool(reclaimed.get("ok", false)) and String(reclaimed.get("kind", "")) == "reclaim" and int(world.ring["holder_id"]) == int(lab.get("red_id")))
	_check("reclaim_audio_id_is_visible", String((lab.get("audio_label") as Label).text).contains("objective.relic.reclaim"))


func _test_holder_consequences(lab: Node) -> void:
	lab.call("reset_lab")
	var world: RefCounted = lab.get("world")
	lab.call("spawn_objective")
	lab.call("place_team_at_ring", 0)
	lab.call("claim_for_team", 0)
	var blue_id: int = int(lab.get("blue_id"))
	var red_id: int = int(lab.get("red_id"))
	world.order_move(blue_id, Vector2i(14, 6))
	world.order_move(red_id, Vector2i(12, 6))
	lab.call("advance_ticks", 5)
	_check("visual_holder_speed_penalty_changes_movement", Vector2i(world.entity(blue_id)["cell"]) == Vector2i(13, 6) and Vector2i(world.entity(red_id)["cell"]) == Vector2i(12, 6))
	_check("visual_holder_damage_bonus_is_live", int(world.effective_damage(blue_id)) == 750 and int(world.effective_damage(red_id)) == 600)
	_check("visual_ring_tracks_moving_holder", Vector2i(world.ring["position"]) == Vector2i(world.entity(blue_id)["cell"]))


func _test_visual_outcomes(lab: Node) -> void:
	# Resolve the current holder's exact external hold duration.
	var world: RefCounted = lab.get("world")
	lab.call("advance_to_victory")
	_check("visual_exact_hold_declares_blue_victory", int(world.winner) == 0 and int(world.loser) == 1 and String(world.victory_reason) == "ring_hold")
	_check("visual_victory_and_loss_audio_ids_are_visible", String((lab.get("audio_label") as Label).text).contains("match.relic.victory") and String((lab.get("audio_label") as Label).text).contains("match.relic.loss"))
	_check("visual_music_state_hooks_are_visible", String((lab.get("audio_label") as Label).text).contains("music.explore") and String((lab.get("audio_label") as Label).text).contains("music.contest") and String((lab.get("audio_label") as Label).text).contains("music.victory") and String((lab.get("audio_label") as Label).text).contains("music.defeat"))
	_check("visual_audio_sequences_are_readable", String((lab.get("audio_label") as Label).text).contains("01") and world.audio.events.size() == 8)

	lab.call("reset_lab")
	world = lab.get("world")
	lab.call("spawn_objective")
	lab.call("place_team_at_ring", 0)
	lab.call("claim_for_team", 0)
	var loss: Dictionary = lab.call("destroy_stronghold", 0)
	_check("visual_stronghold_loss_overrides_holder_progress", bool(loss.get("ok", false)) and int(world.winner) == 1 and int(world.loser) == 0 and String(world.victory_reason) == "stronghold")
	_check("visual_losing_holder_drops_before_outcome", String(world.ring["state"]) == "dropped")
	var kinds: Array[String] = []
	for row: Dictionary in world.audio.events:
		kinds.append(String(row["kind"]))
	_check("visual_loss_audio_order_is_deterministic", kinds == ["music_explore", "spawn", "music_contest", "claim", "drop", "victory", "loss", "music_victory", "music_defeat"])
	_check("visual_final_world_valid", world.validate_state() == "", world.validate_state())


func _contains_audio_player(node: Node) -> bool:
	if node is AudioStreamPlayer or node is AudioStreamPlayer2D or node is AudioStreamPlayer3D:
		return true
	for child: Node in node.get_children():
		if _contains_audio_player(child):
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE9_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE9_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
