extends SceneTree
## Locomotion Phase A behavioural gate: every unit that gets a move order moves,
## and no unit's movement comes from an invented constant.
##
## The sim no longer fabricates acceleration, braking or a turn rate. `_step_route`
## refuses to translate a row whose acceleration or braking is unauthored and says
## so with a `unauthored locomotor …` push_error; `_retail_turn_rate_degrees` does
## the same for a missing rate. That fail-closed design is only safe if the shipped
## data really does author those fields, so this runner boots the real RotWK Men
## skirmish off the live selection and proves it on the real roster instead of a
## fixture.
##
## Authored sources (workspace/retail-extract/data/ini):
##   locomotor.ini:142  HumanLocomotor    Acceleration/Braking 510, TurnTime 500
##   locomotor.ini:1026 HorseLocomotor    Acceleration 1500, Braking 2000, TurnTime 1500
##   locomotor.ini:1683 CatapultLocomotor Acceleration/Braking 1000, TurnTime 1000
##   locomotor.ini:3167 WhirlpoolLocomotor is the ONE retail template that authors
##                      no Acceleration/Braking/TurnTime, and its only referent
##                      (ElvenWhirlpool) is a stationary map effect.
##
## Run:
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/retail_authored_movement_runner.gd

const BOOT_DEADLINE_MS := 300000
const ROTWK_SKIRMISH_MAP := "rotwk.map.adorn-river"
const MOVE_TICKS := 40
const MOVE_DISTANCE := 20.0

## LIVENESS. A GDScript runtime error aborts the enclosing function silently, so a
## runner with no pinned check count can report zero failures having run nothing.
## Raise deliberately when checks are added; never lower it to go green.
const EXPECTED_CHECKS := 13

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_AUTHORED_MOVEMENT_RUNNER")
	for env_name in ["OPENBFME_MP", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	# The starter army is what puts infantry, cavalry, siege and a hero on the
	# field; without it a skirmish start is one porter and this runner would be
	# proving nothing about the classes the brief names.
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	print("AUTHORED_MOVEMENT_HARNESS require_stderr_clean=SCRIPT_ERROR,Invalid access,unauthored locomotor")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", ROTWK_SKIRMISH_MAP)
	var game_state := root.get_node_or_null("GameState")
	if game_state != null:
		game_state.set("retail_player_faction", "men")
		game_state.set("retail_map_id", ROTWK_SKIRMISH_MAP)

	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("slice_scene_loads", scene != null, "vertical slice scene did not load"):
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("slice_ready_ok", bool(slice.ready_ok), "failure=%s" % String(slice.failure_reason)):
		_finish()
		return
	var simulation = slice.simulation if slice.get("simulation") != null else null
	if not _check("simulation_present", simulation != null, "simulation is null"):
		_finish()
		return

	_check_every_row_is_authored(simulation)
	_check_every_unit_rule_is_authored(simulation)
	_check_no_reform_threshold_is_invented(simulation)
	await _check_categories_move(simulation)
	_finish()


func _check_every_unit_rule_is_authored(simulation) -> void:
	## Position deltas can only cover what the start roster fields (hero,
	## infantry, ranged infantry, builder on this map). The configured unit
	## RULES cover the whole fieldable faction, cavalry and siege included, so
	## the authored-values claim is proved over classes this map never spawns.
	##
	## KNOWN GAP, named not hidden: bfme2.object.gondor-trebuchet. Its M3
	## contract (data/m3/trebuchet-runtime.json) was cooked before the
	## CatapultLocomotor binding existed, so it ships no acceleration/braking/
	## turn rate. m3_pack_expansion.py now emits all three; the gap closes on
	## the next recook. Until then the sim refuses to move it and says so.
	const KNOWN_UNAUTHORED := ["bfme2.object.gondor-trebuchet"]
	var rules: Dictionary = simulation._rules.get("unit_rules", {}) as Dictionary
	var unexpected: PackedStringArray = []
	var known_seen: PackedStringArray = []
	for rule_id_value in rules.keys():
		var rule_id := String(rule_id_value)
		var rule: Dictionary = rules[rule_id_value] as Dictionary
		var gaps: PackedStringArray = []
		for field_name in ["acceleration", "braking", "turn_rate_degrees_per_second"]:
			if float(rule.get(field_name, 0.0)) <= 0.0:
				gaps.append(field_name)
		if gaps.is_empty():
			continue
		if KNOWN_UNAUTHORED.has(rule_id):
			known_seen.append(rule_id)
			continue
		unexpected.append("%s:%s" % [rule_id, ",".join(gaps)])
	print(
		"AUTHORED_MOVEMENT_UNIT_RULES count=%d known_gaps=%s"
		% [rules.size(), ", ".join(known_seen) if known_seen.size() > 0 else "<none>"]
	)
	_check("unit_rules_present", rules.size() > 0, "no configured unit rules")
	_check(
		"every_unit_rule_carries_authored_movement_or_is_a_named_gap",
		unexpected.is_empty(),
		"unauthored=%s" % ", ".join(unexpected)
	)


# ------------------------------------------------------------- authored -----


func _check_every_row_is_authored(simulation) -> void:
	## Deliverable: no shipped unit reaches the mover without the three fields
	## the mover consumes. A gap here is what used to be papered over with
	## a ten-times-max-speed ramp invented at the mover.
	var unauthored: PackedStringArray = []
	var counted := 0
	for id_value in simulation.entities.keys():
		var row: Dictionary = simulation.entities[int(id_value)]
		if not row.has("speed"):
			continue
		counted += 1
		for field_name in ["acceleration", "braking", "turn_rate_degrees_per_second"]:
			if float(row.get(field_name, 0.0)) > 0.0:
				continue
			unauthored.append(
				"%s:%s" % [String(row.get("horde_id", row.get("source_object_id", "?"))), field_name]
			)
	_check("roster_has_rows", counted > 0, "no spawned rows carry a speed")
	_check(
		"every_spawned_row_carries_authored_movement",
		unauthored.is_empty(),
		"unauthored=%s" % ", ".join(unauthored)
	)


func _check_no_reform_threshold_is_invented(simulation) -> void:
	## Retail authors MaxTurnWithoutReform on 12 of 128 templates. Where it is
	## absent the threshold must be the explicit "no reform gate" -1.0, never a
	## category-keyed 45 or 100.
	var invented: PackedStringArray = []
	for id_value in simulation.entities.keys():
		var row: Dictionary = simulation.entities[int(id_value)]
		if not row.has("speed"):
			continue
		var threshold := float(simulation._retail_reform_threshold_degrees(row))
		var authored := float(row.get("max_turn_without_reform_degrees", 0.0))
		if authored > 0.0:
			if not is_equal_approx(threshold, authored):
				invented.append(
					"%s:%f!=%f" % [String(row.get("horde_id", "?")), threshold, authored]
				)
			continue
		if threshold >= 0.0:
			invented.append("%s:unauthored-but-%f" % [String(row.get("horde_id", "?")), threshold])
	_check(
		"reform_threshold_is_authored_or_absent",
		invented.is_empty(),
		"invented=%s" % ", ".join(invented)
	)


# ----------------------------------------------------------- behavioural ----


func _check_categories_move(simulation) -> void:
	## The behavioural proof the previous attempt never obtained: order a real
	## unit of each class to move and watch the position change.
	var census := {}
	for id_value in simulation.entities.keys():
		var census_row: Dictionary = simulation.entities[int(id_value)]
		if not census_row.has("speed") or float(census_row.get("speed", 0.0)) <= 0.0:
			continue
		var key := String(census_row.get("category", "<none>"))
		census[key] = int(census.get(key, 0)) + 1
	print("AUTHORED_MOVEMENT_CATEGORY_CENSUS %s" % str(census))
	var wanted := ["infantry", "ranged-infantry", "cavalry", "siege", "hero"]
	for extra in census.keys():
		if not wanted.has(String(extra)):
			wanted.append(String(extra))
	var moved_any := false
	for category in wanted:
		var id := _first_row_of_category(simulation, category)
		if id <= 0:
			# Not every skirmish start fields every class; say so rather than
			# scoring a silent pass.
			print("AUTHORED_MOVEMENT_SKIP category=%s reason=not-on-the-start-roster" % category)
			continue
		var row: Dictionary = simulation.entities[id]
		var before := Vector2(row["position"])
		var destination := before + Vector2.RIGHT * MOVE_DISTANCE
		var accepted: int = simulation.issue_move(
			[id] as Array[int], destination, "order.move", int(row.get("team", 1))
		)
		print(
			"AUTHORED_MOVEMENT_ORDER category=%s id=%d team=%d accepted=%d route=%d state=%s from=%s to=%s reject=%s"
			% [
				category,
				id,
				int(row.get("team", -1)),
				accepted,
				(row.get("route", []) as Array).size(),
				String(row.get("state", "?")),
				str(before),
				str(destination),
				String(simulation.last_route_rejection),
			]
		)
		for _tick in range(MOVE_TICKS):
			simulation.tick()
			await process_frame
		var after := Vector2(simulation.entities[id]["position"])
		var delta := before.distance_to(after)
		moved_any = moved_any or delta > 0.0
		_check(
			"%s_moves_on_a_move_order" % category.replace("-", "_"),
			delta > 0.0,
			"id=%d horde=%s delta=%f accel=%f braking=%f turn=%f" % [
				id,
				String(row.get("horde_id", "?")),
				delta,
				float(row.get("acceleration", 0.0)),
				float(row.get("braking", 0.0)),
				float(row.get("turn_rate_degrees_per_second", 0.0)),
			]
		)
	_check("at_least_one_class_moved", moved_any, "no ordered unit changed position")


func _first_row_of_category(simulation, category: String) -> int:
	var best := 0
	for id_value in simulation.entities.keys():
		var id := int(id_value)
		var row: Dictionary = simulation.entities[id]
		if String(row.get("category", "")) != category:
			continue
		if not row.has("position") or not row.has("speed"):
			continue
		if float(row.get("speed", 0.0)) <= 0.0:
			continue
		if best == 0 or id < best:
			best = id
	return best


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		if detail == "":
			print("FAIL %s" % name)
		else:
			print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	var total := passed + failed
	if total < EXPECTED_CHECKS:
		failed += 1
		print(
			"FAIL runner_liveness :: ran %d checks, expected at least %d (a silent coroutine abort)"
			% [total, EXPECTED_CHECKS]
		)
	print("AUTHORED_MOVEMENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
