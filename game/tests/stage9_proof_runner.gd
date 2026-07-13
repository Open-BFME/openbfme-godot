extends SceneTree
## godot --headless --path game -s res://tests/stage9_proof_runner.gd

const Stage9World = preload("res://src/proof_stage9/proof_world.gd")
const AudioRouter = preload("res://src/proof_stage9/audio_event_router.gd")

var passed: int = 0
var failed: int = 0
var document: Dictionary = {}
var repeat_hash: String = "00000000"
var audio_count: int = 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_load_rules()
	_test_external_contract_and_audio_router()
	if document.is_empty():
		_finish()
		return
	_test_spawn_claim_and_holder_consequences()
	_test_drop_delay_and_reclaim()
	_test_ring_hold_victory_and_loss_audio()
	_test_stronghold_loss_interaction()
	_test_hash_coverage_and_repeat()
	_finish()


func _load_rules() -> void:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/stage9_ring_rules.json")
	_check("stage9_ring_rules_file_exists", FileAccess.file_exists(path), path)
	var parser := JSON.new()
	var error: int = parser.parse(FileAccess.get_file_as_string(path))
	_check("stage9_ring_rules_json_parses", error == OK and typeof(parser.data) == TYPE_DICTIONARY)
	if error == OK and typeof(parser.data) == TYPE_DICTIONARY:
		document = parser.data


func _test_external_contract_and_audio_router() -> void:
	if document.is_empty(): return
	_check("ring_schema_is_legal_safe", String(document.get("schema", "")) == "openbfme.relic-ring-objective" and String(document.get("objective", {}).get("displayName", "")) == "Auric Loop")
	_check("holder_consequences_are_external", int(document.objective.holderSpeedPermille) == 800 and int(document.objective.holderDamagePermille) == 1250 and int(document.objective.victoryHoldTicks) == 12)
	var world: Stage9World = _world()
	_check("world_configures_external_ring_rules", world.validate_state() == "" and String(world.rules["displayName"]) == "Auric Loop")
	var bad: Dictionary = document.duplicate(true)
	bad["unexpected"] = true
	var strict := Stage9World.new() as Stage9World
	_check("ring_document_unknown_field_rejected", strict.setup(bad) == "invalid_ring_document")
	var router := AudioRouter.new() as AudioRouter
	_check("audio_router_accepts_external_routes", router.configure(document.audioEvents) == "")
	var routed: Dictionary = router.route("spawn", 7)
	_check("audio_event_is_observable_without_hardware", bool(routed.get("ok", false)) and router.events.size() == 1 and String(router.events[0]["event_id"]) == "objective.relic.spawn" and router is RefCounted)
	_check("audio_event_has_stable_sequence_tick_category", int(router.events[0]["sequence"]) == 1 and int(router.events[0]["tick"]) == 7 and String(router.events[0]["category"]) == "objective")
	_check("unknown_audio_route_rejected", String(router.route("invented", 8).get("reason", "")) == "unknown_audio_route")
	var optional_world: Stage9World = _world()
	var enabled_hash: int = optional_world.state_hash()
	_check("objective_can_be_disabled_for_classic_mode", bool(optional_world.set_objective_enabled(false).get("ok", false)) and not optional_world.objective_enabled and String(optional_world.spawn_ring().get("reason", "")) == "objective_disabled")
	_check("objective_toggle_is_hash_visible", optional_world.state_hash() != enabled_hash)
	_check("objective_can_be_reenabled", bool(optional_world.set_objective_enabled(true).get("ok", false)) and optional_world.objective_enabled and bool(optional_world.spawn_ring().get("ok", false)))


func _test_spawn_claim_and_holder_consequences() -> void:
	var world: Stage9World = _world()
	var spawn := Vector2i(9, 6)
	var blue: int = world.add_unit(0, spawn, 500, 100)
	var red: int = world.add_unit(1, Vector2i(15, 6), 500, 100)
	_check("claim_before_spawn_rejected", String(world.claim_ring(blue).get("reason", "")) == "ring_not_claimable")
	var spawn_result: Dictionary = world.spawn_ring()
	_check("ring_spawns_at_external_cell", bool(spawn_result.get("ok", false)) and String(world.ring["state"]) == "spawned" and Vector2i(world.ring["position"]) == spawn)
	_check("duplicate_ring_spawn_rejected", String(world.spawn_ring().get("reason", "")) == "ring_already_spawned")
	_check("out_of_range_claim_rejected", String(world.claim_ring(red).get("reason", "")) == "holder_out_of_range")
	var claim: Dictionary = world.claim_ring(blue)
	_check("ring_claim_sets_unique_holder", bool(claim.get("ok", false)) and String(world.ring["state"]) == "held" and int(world.ring["holder_id"]) == blue and int(world.ring["claim_count"]) == 1)
	_check("holder_speed_penalty_is_authoritative", world.effective_speed_permille(blue) == 800 and world.effective_speed_permille(red) == 1000)
	_check("holder_damage_bonus_is_authoritative", world.effective_damage(blue) == 125 and world.effective_damage(red) == 100)
	world.order_move(blue, Vector2i(14, 6))
	world.order_move(red, Vector2i(19, 6))
	world.advance(5)
	_check("holder_moves_four_cells_per_five_ticks", Vector2i(world.entity(blue)["cell"]) == Vector2i(13, 6) and Vector2i(world.entity(red)["cell"]) == Vector2i(19, 6))
	_check("ring_follows_holder", Vector2i(world.ring["position"]) == Vector2i(world.entity(blue)["cell"]))
	_check("spawn_and_claim_audio_routes_observable", world.audio.events_for("spawn").size() == 1 and world.audio.events_for("claim").size() == 1)


func _test_drop_delay_and_reclaim() -> void:
	var world: Stage9World = _world()
	var spawn := Vector2i(9, 6)
	var blue: int = world.add_unit(0, spawn, 200, 100)
	var red: int = world.add_unit(1, spawn, 500, 250)
	world.spawn_ring()
	world.claim_ring(blue)
	var defeat: Dictionary = world.damage_unit(blue, red)
	_check("holder_defeat_uses_combat_and_drops_ring", bool(defeat.get("defeated", false)) and String(world.ring["state"]) == "dropped" and int(world.ring["holder_id"]) == 0)
	_check("drop_records_cell_and_reclaim_tick", Vector2i(world.ring["position"]) == spawn and int(world.ring["reclaim_available_tick"]) == 2)
	_check("early_reclaim_rejected", String(world.claim_ring(red).get("reason", "")) == "reclaim_delay")
	world.advance(1)
	_check("reclaim_still_rejected_one_tick_early", String(world.claim_ring(red).get("reason", "")) == "reclaim_delay")
	world.advance(1)
	var reclaimed: Dictionary = world.claim_ring(red)
	_check("enemy_reclaims_after_exact_delay", bool(reclaimed.get("ok", false)) and String(reclaimed.get("kind", "")) == "reclaim" and int(world.ring["holder_id"]) == red and int(world.ring["claim_count"]) == 2)
	_check("drop_and_reclaim_audio_each_route_once", world.audio.events_for("drop").size() == 1 and world.audio.events_for("reclaim").size() == 1)
	_check("clean_hit_event_routes_without_gore_or_hardware", world.audio.events_for("hit").size() == 1 and String(world.audio.events_for("hit")[0]["event_id"]) == "combat.clean-impact")
	_check("drop_reclaim_world_valid", world.validate_state() == "", world.validate_state())


func _test_ring_hold_victory_and_loss_audio() -> void:
	var world: Stage9World = _world()
	var red: int = world.add_unit(1, Vector2i(9, 6), 500, 100)
	world.spawn_ring()
	world.claim_ring(red)
	world.advance(11)
	_check("ring_victory_not_early", world.winner == -1 and int(world.ring["ticks_held"]) == 11)
	world.advance(1)
	_check("ring_hold_declares_deterministic_winner_loser", world.winner == 1 and world.loser == 0 and world.victory_reason == "ring_hold")
	world.advance(5)
	_check("ring_victory_freezes_future_ticks", int(world.tick_index) == 12)
	var victory_events: Array[Dictionary] = world.audio.events_for("victory")
	var loss_events: Array[Dictionary] = world.audio.events_for("loss")
	_check("victory_and_loss_audio_are_both_observable", victory_events.size() == 1 and loss_events.size() == 1 and int(victory_events[0]["team"]) == 1 and int(loss_events[0]["team"]) == 0)
	var sequences: Array[int] = []
	for row: Dictionary in world.audio.events:
		sequences.append(int(row["sequence"]))
	_check("audio_event_sequences_are_contiguous", sequences == [1, 2, 3, 4, 5, 6, 7, 8])
	_check("music_state_hooks_route_explore_contest_and_outcome", world.audio.events_for("music_explore").size() == 1 and world.audio.events_for("music_contest").size() == 1 and world.audio.events_for("music_victory").size() == 1 and world.audio.events_for("music_defeat").size() == 1 and world.music_state == "victory")
	audio_count = world.audio.events.size()


func _test_stronghold_loss_interaction() -> void:
	var world: Stage9World = _world()
	var blue: int = world.add_unit(0, Vector2i(9, 6), 500, 100)
	world.spawn_ring()
	world.claim_ring(blue)
	world.advance(3)
	var result: Dictionary = world.destroy_stronghold(0)
	_check("stronghold_loss_overrides_ring_progress", bool(result.get("ok", false)) and world.winner == 1 and world.loser == 0 and world.victory_reason == "stronghold")
	_check("losing_holder_drops_ring_before_outcome", String(world.ring["state"]) == "dropped" and int(world.ring["holder_id"]) == 0)
	var kinds: Array[String] = []
	for row: Dictionary in world.audio.events:
		kinds.append(String(row["kind"]))
	_check("stronghold_loss_audio_order_is_drop_victory_loss", kinds == ["music_explore", "spawn", "music_contest", "claim", "drop", "victory", "loss", "music_victory", "music_defeat"])
	_check("second_outcome_rejected", String(world.destroy_stronghold(1).get("reason", "")) == "invalid_stronghold")


func _test_hash_coverage_and_repeat() -> void:
	var base_world: Stage9World = _repeat_world()
	var base_hash: int = base_world.state_hash()
	base_world.ring["ticks_held"] = int(base_world.ring["ticks_held"]) + 1
	_check("hash_covers_ring_progress", base_world.state_hash() != base_hash)
	base_world.ring["ticks_held"] = int(base_world.ring["ticks_held"]) - 1
	base_world.entity(1)["movement_credit"] = 99
	_check("hash_covers_holder_movement_credit", base_world.state_hash() != base_hash)
	base_world.entity(1)["movement_credit"] = 0
	base_world.audio.route("drop", base_world.tick_index, 0, 1)
	_check("hash_covers_observable_audio_events", base_world.state_hash() != base_hash)
	var first: Stage9World = _repeat_world()
	var second: Stage9World = _repeat_world()
	first.advance(7)
	second.advance(7)
	repeat_hash = first.state_hash_text()
	_check("repeat_stage9_hash_equal", first.state_hash() == second.state_hash(), repeat_hash)
	_check("repeat_stage9_snapshot_equal", JSON.stringify(first.snapshot()) == JSON.stringify(second.snapshot()))
	_check("repeat_stage9_state_valid", first.validate_state() == "" and second.validate_state() == "", first.validate_state())


func _world() -> Stage9World:
	var world := Stage9World.new() as Stage9World
	var error: String = world.setup(document)
	if error != "": _check("stage9_world_setup", false, error)
	return world


func _repeat_world() -> Stage9World:
	var world: Stage9World = _world()
	var blue: int = world.add_unit(0, Vector2i(9, 6), 500, 100)
	world.add_unit(1, Vector2i(15, 6), 500, 100)
	world.spawn_ring()
	world.claim_ring(blue)
	world.order_move(blue, Vector2i(14, 6))
	return world


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE9_METRICS repeat_hash=%s audio_events=%d assertions=%d" % [repeat_hash, audio_count, passed])
		print("STAGE9_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE9_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
