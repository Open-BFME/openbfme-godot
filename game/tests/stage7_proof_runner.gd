extends SceneTree
## Deterministic Stage 7 AI plan, difficulty, finite-resource, and victory proof.

const WorldScript = preload("res://src/proof_stage7/proof_world.gd")

var passed: int = 0
var failed: int = 0
var document: Dictionary = {}
var repeat_hash: String = "00000000"
var difficulty_hashes: Dictionary = {}
var difficulty_ticks: Dictionary = {}
var standard_victories: int = 0
var starvation_victories: int = 0
var no_softlocks: int = 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE7_PROOF_RUNNER")
	call_deferred("_run")


func _run() -> void:
	document = _load_document()
	_check("external_ai_strategy_document_loads", not document.is_empty())
	if document.is_empty():
		_finish()
		return
	_test_catalog_and_plan()
	_test_declared_tick_sequence()
	_test_all_difficulties()
	_test_starvation_no_softlock()
	_test_repeat_replay_hash()
	_finish()


func _test_catalog_and_plan() -> void:
	var world := WorldScript.new()
	_check("normal_strategy_configures", world.configure(document, "normal") == "")
	_check("three_difficulties_are_declared", world.catalog.difficulty_ids() == ["easy", "hard", "normal"])
	_check("difficulty_think_rates_are_distinct", int(world.catalog.difficulty("easy")["thinkIntervalTicks"]) == 3 and int(world.catalog.difficulty("normal")["thinkIntervalTicks"]) == 2 and int(world.catalog.difficulty("hard")["thinkIntervalTicks"]) == 1)
	_check("difficulty_income_rates_are_distinct", int(world.catalog.difficulty("easy")["incomePermille"]) == 750 and int(world.catalog.difficulty("normal")["incomePermille"]) == 1000 and int(world.catalog.difficulty("hard")["incomePermille"]) == 1250)
	_check("difficulty_attack_rates_are_distinct", int(world.catalog.difficulty("easy")["attackPermille"]) == 900 and int(world.catalog.difficulty("normal")["attackPermille"]) == 1000 and int(world.catalog.difficulty("hard")["attackPermille"]) == 1150)
	_check("one_data_driven_plan_is_available", world.catalog.plan_ids() == ["measured_opening"])
	var steps: Array = world.catalog.plan("measured_opening")["steps"]
	_check("plan_has_four_ordered_steps", steps.size() == 4)
	_check("plan_actions_are_build_train_train_attack", [String(steps[0]["action"]), String(steps[1]["action"]), String(steps[2]["action"]), String(steps[3]["action"])] == ["build", "train", "train", "attack"])
	_check("plan_costs_are_data_driven", int(steps[0]["cost"]) == 100 and int(steps[1]["cost"]) == 80 and int(steps[2]["cost"]) == 80)
	_check("scenario_deposit_is_finite", int(world.scenario["finiteResourceAmount"]) == 240 and int(world.scenario["harvestAmount"]) == 40)
	_check("scenario_has_bounded_proof_budget", int(world.scenario["maximumProofTicks"]) == 200)
	var unknown := WorldScript.new()
	_check("unknown_difficulty_is_rejected", unknown.configure(document, "impossible") == "unknown_difficulty")
	var invalid := document.duplicate(true)
	invalid["plans"][0]["steps"][1]["action"] = "cheat"
	_check("unknown_plan_action_is_rejected", WorldScript.new().configure(invalid, "normal") != "")
	invalid = document.duplicate(true)
	invalid["scenario"]["infiniteIncome"] = true
	_check("unknown_infinite_resource_field_is_rejected", WorldScript.new().configure(invalid, "normal") != "")


func _test_declared_tick_sequence() -> void:
	var world := _new_world("normal")
	_check("initial_state_uses_declared_resources", world.resources == 160 and world.finite_resource_remaining == 240 and world.army_size == 1)
	world.tick()
	_check("first_think_starts_build_at_tick_one", world.tick_index == 1 and String(world.active_job.get("action", "")) == "build" and int(world.active_job.get("complete_tick", 0)) == 3)
	_check("build_cost_is_debited_transactionally", world.resources == 60 and world.plan_index == 0)
	world.tick()
	_check("build_does_not_complete_early", not world.extractor_built and world.plan_index == 0)
	world.tick()
	_check("build_completes_on_declared_tick", world.extractor_built and world.plan_index == 1 and world.active_job.is_empty())
	_check("finite_income_wait_does_not_skip_viable_train", world.events_of_type("skip").is_empty() and world.resources == 60)
	world.advance(2)
	_check("first_harvest_is_exact", world.tick_index == 5 and world.resources == 20 and world.finite_resource_remaining == 200)
	_check("first_train_starts_after_finite_payout", String(world.active_job.get("action", "")) == "train" and int(world.active_job.get("complete_tick", 0)) == 7)
	world.advance(2)
	_check("first_train_completes_to_concrete_army_count", world.army_size == 2 and world.plan_index == 2)
	_check("sequence_state_is_valid", world.validate_state() == "")


func _test_all_difficulties() -> void:
	for difficulty_id: String in ["easy", "normal", "hard"]:
		var world := _new_world(difficulty_id)
		var result: Dictionary = world.run_until_terminal()
		if bool(result["victory"]):
			standard_victories += 1
		_check("%s_victory_loop_completes" % difficulty_id, bool(result["victory"]) and world.enemy_fortress_health == 0)
		_check("%s_finishes_inside_data_budget" % difficulty_id, int(result["tick"]) <= int(world.scenario["maximumProofTicks"]))
		_check("%s_builds_one_extractor" % difficulty_id, _completed_action_count(world, "build") == 1 and world.extractor_built)
		_check("%s_trains_two_units" % difficulty_id, _completed_action_count(world, "train") == 2 and world.army_size == 3)
		_check("%s_uses_finite_harvests" % difficulty_id, not world.events_of_type("harvest").is_empty() and _withdrawn_total(world) <= int(world.scenario["finiteResourceAmount"]))
		_check("%s_issues_planned_attack" % difficulty_id, world.events_of_type("attack_order").size() == 1 and not bool(world.events_of_type("attack_order")[0]["details"]["fallback"]))
		_check("%s_emits_attack_waves" % difficulty_id, not world.events_of_type("attack_wave").is_empty())
		_check("%s_emits_one_victory_event" % difficulty_id, world.events_of_type("victory").size() == 1)
		_check("%s_never_spends_below_zero" % difficulty_id, world.resources >= 0 and world.finite_resource_remaining >= 0)
		_check("%s_final_state_is_valid" % difficulty_id, world.validate_state() == "")
		difficulty_hashes[difficulty_id] = world.state_hash_text()
		difficulty_ticks[difficulty_id] = world.tick_index
	_check("difficulty_replay_hashes_are_distinguishable", Dictionary(difficulty_hashes).values().duplicate().reduce(func(accum: Dictionary, value: Variant) -> Dictionary: accum[value] = true; return accum, {}).size() == 3)
	_check("difficulty_victory_timing_is_distinguishable", int(difficulty_ticks["easy"]) != int(difficulty_ticks["hard"]))
	var normal := _new_world("normal")
	normal.run_until_terminal()
	_check("normal_proof_consumes_deposit_to_zero", normal.finite_resource_remaining == 0)


func _test_starvation_no_softlock() -> void:
	for difficulty_id: String in ["easy", "normal", "hard"]:
		var world := _new_world(difficulty_id)
		world.force_starvation_probe()
		var result: Dictionary = world.run_until_terminal()
		if bool(result["victory"]):
			starvation_victories += 1
		if bool(result["victory"]) and int(result["elapsed_ticks"]) < int(world.scenario["maximumProofTicks"]):
			no_softlocks += 1
		_check("%s_starved_probe_reaches_victory" % difficulty_id, bool(result["victory"]))
		_check("%s_starved_probe_is_bounded" % difficulty_id, int(result["elapsed_ticks"]) < int(world.scenario["maximumProofTicks"]))
		_check("%s_starved_probe_skips_three_unaffordable_steps" % difficulty_id, world.events_of_type("skip").size() == 3)
		_check("%s_starved_probe_uses_fallback_attack" % difficulty_id, world.fallback_attack and bool(world.events_of_type("attack_order")[0]["details"]["fallback"]))
		_check("%s_starved_probe_keeps_free_garrison" % difficulty_id, world.army_size == 1)
		_check("%s_starved_probe_has_no_phantom_income" % difficulty_id, world.resources == 0 and world.finite_resource_remaining == 0 and world.events_of_type("harvest").is_empty())
		_check("%s_starved_probe_state_is_valid" % difficulty_id, world.validate_state() == "")


func _test_repeat_replay_hash() -> void:
	var first := _new_world("normal")
	var second := _new_world("normal")
	first.run_until_terminal()
	second.run_until_terminal()
	repeat_hash = first.state_hash_text()
	_check("normal_repeat_replay_hash_equal", first.state_hash() == second.state_hash(), repeat_hash)
	_check("normal_repeat_command_log_equal", first.command_log == second.command_log)
	var starved_first := _new_world("hard")
	var starved_second := _new_world("hard")
	starved_first.force_starvation_probe()
	starved_second.force_starvation_probe()
	starved_first.run_until_terminal()
	starved_second.run_until_terminal()
	_check("starvation_repeat_replay_hash_equal", starved_first.state_hash() == starved_second.state_hash())
	var base_hash: int = first.state_hash()
	first.resources += 1
	_check("hash_covers_remaining_resources", first.state_hash() != base_hash)
	first.resources -= 1
	_check("hash_restores_after_resource_mutation", first.state_hash() == base_hash)
	first.next_think_tick += 1
	_check("hash_covers_future_ai_schedule", first.state_hash() != base_hash)


func _new_world(difficulty_id: String) -> RefCounted:
	var world := WorldScript.new()
	assert(world.configure(document, difficulty_id) == "")
	return world


func _completed_action_count(world: RefCounted, action: String) -> int:
	var count: int = 0
	for event: Dictionary in world.events_of_type("job_completed"):
		if String(event["details"]["action"]) == action:
			count += 1
	return count


func _withdrawn_total(world: RefCounted) -> int:
	var result: int = 0
	for event: Dictionary in world.events_of_type("harvest"):
		result += int(event["details"]["withdrawn"])
	return result


func _load_document() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://../content/openbfme-test/data/ai_strategies.json")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed if typeof(parsed) == TYPE_DICTIONARY else {}


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE7_METRICS repeat_hash=%s assertions=%d easy_tick=%s normal_tick=%s hard_tick=%s victories=%d starvation_victories=%d no_softlocks=%d" % [repeat_hash, passed, str(difficulty_ticks.get("easy", "n/a")), str(difficulty_ticks.get("normal", "n/a")), str(difficulty_ticks.get("hard", "n/a")), standard_victories, starvation_victories, no_softlocks])
		print("STAGE7_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE7_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
