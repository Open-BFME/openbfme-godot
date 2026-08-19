extends SceneTree
## Fortresses and farms generate passive income in a REAL match (owner
## 2026-08-19: "farms / castles / fortresses dont passively generate resources
## like they should in the basegame").
##
## Retail authors the keep's income on the fortress composite piece
## (fortress.ini:983 MenFortressCitadel `AutoDepositUpdate DepositTiming =
## GENERIC_KEEP_MONEY_TIME (6000 ms) DepositAmount = GENERIC_KEEP_MONEY_AMOUNT
## (25)`, gamedata.ini:147-148). The manifest filed those rows under the
## DEFERRED table (the piece is engine-spawned), and the runtime only bound
## the per-kind executable table, so every fortress earned nothing. This boots
## the Men slice and measures resources against the authored numbers.
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/fortress_passive_income_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const BOOT_DEADLINE_MS := 240000
## GENERIC_KEEP_MONEY_TIME 6000 ms at TICK_SECONDS 0.1 = 60 ticks; 600 ticks
## = exactly 10 deposits of GENERIC_KEEP_MONEY_AMOUNT 25.
const MEASURE_TICKS := 600
const KEEP_AMOUNT := 25
const KEEP_INTERVAL_TICKS := 60
var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _init() -> void:
	_runner_watchdog.start(self, "FORTRESS_PASSIVE_INCOME_RUNNER", 480000)
	await process_frame
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", "rotwk.map.adorn-river")
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("scene_loads", scene != null):
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("men_slice_ready", bool(slice.ready_ok), String(slice.failure_reason)):
		_finish()
		return
	var sim = slice.simulation
	# The citadel piece carries the authored AutoDepositUpdate rows.
	var citadel_id := -1
	for sid in sim.structure_ids():
		var row: Dictionary = sim.structure(sid)
		if int(row.get("team", -1)) == sim.PLAYER_TEAM and String(row.get("source_object_id", "")) == "MenFortressCitadel":
			citadel_id = int(sid)
			break
	_check("player_fortress_citadel_piece_present", citadel_id > 0)
	if citadel_id > 0:
		var citadel: Dictionary = sim.structure(citadel_id)
		var rules: Array = citadel.get("auto_deposit_rules", []) as Array
		_check("citadel_binds_authored_auto_deposit_rows", rules.size() == 1, "rules=%d" % rules.size())
		if rules.size() == 1:
			var rule := rules[0] as Dictionary
			var amount := int((rule.get("depositAmount", {}) as Dictionary).get("value", 0))
			var interval := int((rule.get("depositTiming", {}) as Dictionary).get("simulationTicks", 0))
			_check("citadel_rows_are_the_authored_numbers", amount == KEEP_AMOUNT and interval == KEEP_INTERVAL_TICKS,
				"amount=%d interval_ticks=%d" % [amount, interval])
		_check("citadel_legacy_clock_suppressed", int(citadel.get("income_per_payout", -1)) == 0)
	# Nothing built: the fortress alone must earn exactly the authored stream.
	var r0 := int(sim.resources_for_team(sim.PLAYER_TEAM))
	var enemy0 := int(sim.resources_for_team(sim.ENEMY_TEAM))
	for i in MEASURE_TICKS:
		sim.tick()
	var r1 := int(sim.resources_for_team(sim.PLAYER_TEAM))
	var expected := (MEASURE_TICKS / KEEP_INTERVAL_TICKS) * KEEP_AMOUNT
	_check("fortress_earns_the_authored_passive_income", r1 - r0 == expected,
		"delta=%d expected=%d (600 ticks, %d per %d ticks)" % [r1 - r0, expected, KEEP_AMOUNT, KEEP_INTERVAL_TICKS])
	# The AI's fortress earns too (same authored module; the AI may have spent
	# some, so only require it is not flat-broke-frozen like before).
	var enemy1 := int(sim.resources_for_team(sim.ENEMY_TEAM))
	var enemy_citadel_bound := false
	for sid in sim.structure_ids():
		var row: Dictionary = sim.structure(sid)
		if int(row.get("team", -1)) == sim.ENEMY_TEAM and String(row.get("source_object_id", "")) == "MenFortressCitadel":
			enemy_citadel_bound = (row.get("auto_deposit_rules", []) as Array).size() == 1
			break
	_check("enemy_fortress_citadel_binds_too", enemy_citadel_bound)
	print("FORTRESS_PASSIVE_INCOME NOTE enemy resources %d -> %d over %d ticks" % [enemy0, enemy1, MEASURE_TICKS])
	root.remove_child(slice)
	slice.free()
	await process_frame
	_finish()


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		_passed += 1
		print("FORTRESS_PASSIVE_INCOME PASS %s" % name)
	else:
		_failed += 1
		print("FORTRESS_PASSIVE_INCOME FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("FORTRESS_PASSIVE_INCOME_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
