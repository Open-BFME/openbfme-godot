class_name Stage7ProofWorld
extends RefCounted
## Deterministic finite-resource AI opening and complete victory-loop proof.

const CatalogScript = preload("res://src/proof_stage7/strategy_catalog.gd")
const PlannerScript = preload("res://src/proof_stage7/ai_planner.gd")

var catalog: RefCounted
var planner: RefCounted
var difficulty_id: String = "normal"
var plan_id: String = ""
var difficulty: Dictionary = {}
var scenario: Dictionary = {}

var tick_index: int = 0
var resources: int = 0
var finite_resource_remaining: int = 0
var extractor_built: bool = false
var army_size: int = 0
var enemy_fortress_health: int = 0
var enemy_fortress_maximum: int = 0
var plan_index: int = 0
var active_job: Dictionary = {}
var attack_started: bool = false
var fallback_attack: bool = false
var victory: bool = false
var next_think_tick: int = 0
var next_harvest_tick: int = 0
var next_attack_tick: int = 0
var command_log: Array[Dictionary] = []
var _next_event_sequence: int = 1
var _configured: bool = false


func configure(document: Dictionary, p_difficulty_id: String = "normal", p_plan_id: String = "") -> String:
	var next_catalog := CatalogScript.new()
	var error: String = next_catalog.configure(document)
	if error != "":
		return error
	var resolved_plan_id := p_plan_id
	if resolved_plan_id == "":
		resolved_plan_id = String(next_catalog.plan_ids()[0])
	var next_difficulty: Dictionary = next_catalog.difficulty(p_difficulty_id)
	var next_plan: Dictionary = next_catalog.plan(resolved_plan_id)
	if next_difficulty.is_empty():
		return "unknown_difficulty"
	if next_plan.is_empty():
		return "unknown_plan"
	var next_planner := PlannerScript.new()
	error = next_planner.configure(next_plan)
	if error != "":
		return error
	catalog = next_catalog
	planner = next_planner
	difficulty_id = p_difficulty_id
	plan_id = resolved_plan_id
	difficulty = next_difficulty
	scenario = next_catalog.scenario.duplicate(true)
	_configured = true
	reset()
	return ""


func reset() -> void:
	assert(_configured)
	tick_index = 0
	resources = int(scenario["startingResources"])
	finite_resource_remaining = int(scenario["finiteResourceAmount"])
	extractor_built = false
	army_size = int(scenario["startingArmy"])
	enemy_fortress_maximum = int(scenario["enemyFortressHealth"])
	enemy_fortress_health = enemy_fortress_maximum
	plan_index = 0
	active_job = {}
	attack_started = false
	fallback_attack = false
	victory = false
	next_think_tick = 1
	next_harvest_tick = 0
	next_attack_tick = 0
	command_log.clear()
	_next_event_sequence = 1
	_record("reset", {"difficulty": difficulty_id, "plan": plan_id})


func tick() -> void:
	if victory:
		return
	tick_index += 1
	_complete_job_if_due()
	_harvest_if_due()
	if active_job.is_empty() and not attack_started and tick_index >= next_think_tick:
		_run_ai_decision()
	if attack_started and tick_index >= next_attack_tick:
		_resolve_attack_wave()


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func run_until_terminal(maximum_ticks: int = -1) -> Dictionary:
	var limit: int = int(scenario["maximumProofTicks"]) if maximum_ticks < 0 else maximum_ticks
	var start_tick: int = tick_index
	while not victory and tick_index - start_tick < limit:
		tick()
	return {
		"ok": victory,
		"victory": victory,
		"tick": tick_index,
		"elapsed_ticks": tick_index - start_tick,
		"reason": "victory" if victory else "tick_budget_exhausted",
	}


func force_starvation_probe() -> void:
	resources = 0
	finite_resource_remaining = 0
	extractor_built = false
	active_job = {}
	plan_index = 0
	attack_started = false
	fallback_attack = false
	next_think_tick = tick_index + 1
	_record("probe", {"mode": "finite_starvation"})


func events_of_type(event_type: String) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for event: Dictionary in command_log:
		if String(event.get("type", "")) == event_type:
			result.append(event.duplicate(true))
	return result


func current_step() -> Dictionary:
	return planner.step(plan_index)


func can_gain_resources() -> bool:
	return extractor_built and finite_resource_remaining > 0


func snapshot() -> Dictionary:
	return {
		"authority": "gdscript-proof",
		"rules_version": 1,
		"definitions": catalog.definitions_snapshot(),
		"difficulty_id": difficulty_id,
		"plan_id": plan_id,
		"tick": tick_index,
		"resources": resources,
		"finite_resource_remaining": finite_resource_remaining,
		"extractor_built": extractor_built,
		"army_size": army_size,
		"enemy_fortress_health": enemy_fortress_health,
		"enemy_fortress_maximum": enemy_fortress_maximum,
		"plan_index": plan_index,
		"active_job": active_job.duplicate(true),
		"attack_started": attack_started,
		"fallback_attack": fallback_attack,
		"victory": victory,
		"next_think_tick": next_think_tick,
		"next_harvest_tick": next_harvest_tick,
		"next_attack_tick": next_attack_tick,
		"next_event_sequence": _next_event_sequence,
		"command_log": command_log.duplicate(true),
	}


func state_hash() -> int:
	var value: int = 0x811C9DC5
	for byte: int in JSON.stringify(snapshot()).to_utf8_buffer():
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	if not _configured or catalog == null or planner == null:
		return "world_not_configured"
	if resources < 0 or finite_resource_remaining < 0 or army_size < 1:
		return "negative_economy_or_empty_fallback_army"
	if enemy_fortress_health < 0 or enemy_fortress_health > enemy_fortress_maximum or victory != (enemy_fortress_health == 0):
		return "fortress_victory_mismatch"
	if plan_index < 0 or plan_index > planner.step_count():
		return "plan_index_out_of_range"
	if not active_job.is_empty():
		if not ["build", "train"].has(String(active_job.get("action", ""))) or int(active_job.get("complete_tick", 0)) <= int(active_job.get("started_tick", -1)):
			return "invalid_active_job"
	var prior_sequence: int = 0
	for event: Dictionary in command_log:
		if int(event.get("sequence", 0)) <= prior_sequence or int(event.get("tick", -1)) < 0:
			return "invalid_command_log_order"
		prior_sequence = int(event["sequence"])
	return ""


func _run_ai_decision() -> void:
	next_think_tick = tick_index + int(difficulty["thinkIntervalTicks"])
	var decision: Dictionary = planner.decide(plan_index, resources, army_size, can_gain_resources())
	match String(decision.get("decision", "idle")):
		"start_job":
			_start_job(decision["step"])
		"skip":
			var step: Dictionary = decision["step"]
			_record("skip", {"plan_index": plan_index, "action": String(step["action"]), "object_id": String(step["objectId"]), "reason": String(decision["reason"])})
			plan_index += 1
		"attack":
			attack_started = true
			fallback_attack = bool(decision.get("fallback", false))
			plan_index += 1
			next_attack_tick = tick_index
			_record("attack_order", {"army_size": army_size, "fallback": fallback_attack})
		"wait":
			pass
		_:
			pass


func _start_job(step: Dictionary) -> void:
	var cost: int = int(step["cost"])
	resources -= cost
	active_job = {
		"plan_index": plan_index,
		"action": String(step["action"]),
		"object_id": String(step["objectId"]),
		"cost": cost,
		"started_tick": tick_index,
		"complete_tick": tick_index + int(step["durationTicks"]),
	}
	_record("job_started", active_job)


func _complete_job_if_due() -> void:
	if active_job.is_empty() or tick_index < int(active_job["complete_tick"]):
		return
	var finished := active_job.duplicate(true)
	if String(active_job["action"]) == "build":
		extractor_built = true
		next_harvest_tick = tick_index + int(scenario["harvestIntervalTicks"])
	else:
		army_size += 1
	active_job = {}
	plan_index += 1
	_record("job_completed", {"action": String(finished["action"]), "object_id": String(finished["object_id"]), "plan_index": int(finished["plan_index"])})


func _harvest_if_due() -> void:
	if not extractor_built or finite_resource_remaining <= 0 or tick_index < next_harvest_tick:
		return
	var withdrawn: int = mini(int(scenario["harvestAmount"]), finite_resource_remaining)
	finite_resource_remaining -= withdrawn
	var credited: int = maxi(1, withdrawn * int(difficulty["incomePermille"]) / 1000)
	resources += credited
	next_harvest_tick = tick_index + int(scenario["harvestIntervalTicks"])
	_record("harvest", {"withdrawn": withdrawn, "credited": credited, "remaining": finite_resource_remaining})


func _resolve_attack_wave() -> void:
	var damage: int = maxi(1, int(scenario["baseAttackDamage"]) * army_size * int(difficulty["attackPermille"]) / 1000)
	var old_health: int = enemy_fortress_health
	enemy_fortress_health = maxi(0, enemy_fortress_health - damage)
	_record("attack_wave", {"army_size": army_size, "damage": old_health - enemy_fortress_health, "fortress_health": enemy_fortress_health})
	if enemy_fortress_health == 0:
		victory = true
		_record("victory", {"difficulty": difficulty_id, "fallback": fallback_attack, "tick": tick_index})
		return
	next_attack_tick = tick_index + int(scenario["attackIntervalTicks"])


func _record(event_type: String, details: Dictionary) -> void:
	var event := {"sequence": _next_event_sequence, "tick": tick_index, "type": event_type, "details": details.duplicate(true)}
	_next_event_sequence += 1
	command_log.append(event)
