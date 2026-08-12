extends SceneTree
## Archer fire-rate must match the retail-authored weapon cadence, and the
## firing animation must span the reload.
##
## Retail truth (PURE RotWK 2.01 tree,
## .private/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * weapon.ini:4230-4241 GondorArcherBow: DelayBetweenShots = 0,
##     PreAttackDelay = 1000 (gamedata.ini:1857), FiringDuration = 0,
##     ClipSize = 1, ClipReloadTime = Min:1500 Max:2000 (weapon.ini:4239,
##     macros at gamedata.ini:1858-1859), ContinuousFireCoast = 2000 (:4241).
##   * weapon.ini:10400-10409 MordorArcherBow: same shape, reload
##     Min:1500 Max:2000 (gamedata.ini:4562-4563).
##   * Deterministic runtime convention: a ranged field fires on its authored
##     Max (spellbook resolvedMax, retail_slice_sim.gd _spellbook_weapon_field)
##     -> Gondor/Mordor volley cycle = 1000 + 2000 = 3000 ms = 30 sim ticks.
##   * gondorarcher.ini:236-288: PREATTACK_A -> GUArcher_ATKF1,
##     FIRING_OR_RELOADING_A -> GUArcher_ATKF2,
##     FIRING_OR_PREATTACK_B -> GUArcher_ATKD (UseWeaponTiming = Yes).
##
## The bug: packs cooked before the importer learned the `Min:X Max:Y` form
## carry no clipReloadTimeMs at all, so the reload collapsed to 0 ms and the
## cycle became pre-attack only: 14 ticks (stagger floor) instead of 30 -
## the owner's "archers fire way too fast, the reset on their arrows is
## instant". The adapter bridges those packs from ContinuousFireCoast, which
## retail authors exactly equal to ClipReloadTime's Max on every clip-1
## archer weapon in the corpus (cited above).
##
## Run:
##   OPENBFME_CONTENT=<repo>/.private/content-packs godot --headless --path game \
##     --script res://tests/archer_cadence_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## 76.0 / 2868.7 — the Fords of Isen map transform scale (see
## selection_pick_footprint_runner.gd header).
const FORDS_SOURCE_SCALE := 0.026492
## Retail-authored Gondor/Mordor archer cadence, in sim ticks (0.1 s each):
## PreAttackDelay 1000 ms + ClipReloadTime Max 2000 ms = 3000 ms.
const AUTHORED_CYCLE_TICKS := 30

## LIVENESS (rulebook T3): every section appends its name and the run only
## reports green when all of them arrived. Raise on new sections; never lower.
const EXPECTED_TESTS := 4
const MINIMUM_CHECKS := 18

var passed := 0
var failed := 0
var _completed: Array[String] = []
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ARCHER_CADENCE")
	call_deferred("_run")


func _run() -> void:
	_test_adapter_bridges_the_dropped_clip_reload()
	_completed.append("adapter-bridge")
	_test_mounted_pack_archer_cadence_matches_retail()
	_completed.append("mounted-pack-cadence")
	_test_sim_measured_cadence_matches_authored()
	_completed.append("sim-measured-cadence")
	_test_firing_animation_states_split_and_bound()
	_completed.append("animation-binding")

	if _completed.size() != EXPECTED_TESTS:
		failed += 1
		push_error("ARCHER_CADENCE_FAIL liveness: %d/%d sections reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	if _completed.size() == EXPECTED_TESTS and passed + failed < MINIMUM_CHECKS:
		failed += 1
		push_error("ARCHER_CADENCE_FAIL liveness: only %d checks ran, expected at least %d" % [
			passed + failed, MINIMUM_CHECKS
		])
	print("ARCHER_CADENCE_RESULT passed=%d failed=%d sections=%d" % [passed, failed, _completed.size()])
	quit(0 if failed == 0 else 1)


## The combat block exactly as the CURRENT men pack carries it (cooked before
## the importer learned the ranged form): no clipReloadTimeMs key at all.
func _pack_shaped_simulation(combat_overrides: Dictionary = {}) -> Dictionary:
	var combat := {
		"attackRange": 300.0,
		"delayBetweenShotsMs": 0.0,
		"preAttackDelayMs": 1000.0,
		"firingDurationMs": 0.0,
		"damage": 35,
		"damageType": "pierce",
		"clipSize": 1,
		"continuousFireOne": 0,
		"continuousFireCoastMs": 2000.0,
		"weaponId": "GondorArcherBow",
		"weaponSlot": "PRIMARY",
	}
	for key in combat_overrides.keys():
		combat[key] = combat_overrides[key]
	return {
		"unit_type": SimScript.ARCHER_OBJECT_ID,
		"source_object_id": "GondorArcherHorde",
		"category": "infantry",
		"member_count": 2,
		"member_health": 100,
		"speed_source": 32.0,
		"vision_range_source": 350.0,
		"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
		"combat": combat,
		"formation": {"positions": [{"x": 0.0, "y": 0.0}, {"x": 10.0, "y": 0.0}]},
	}


func _test_adapter_bridges_the_dropped_clip_reload() -> void:
	var rule := Adapter.normalized_unit_rule(_pack_shaped_simulation(), FORDS_SOURCE_SCALE)
	_check("pack_shaped_rule_normalizes", not rule.is_empty())
	if rule.is_empty():
		return
	_check(
		"dropped_clip_reload_bridges_to_authored_max",
		is_equal_approx(float(rule.get("clip_reload_time_ms", 0.0)), 2000.0)
	)
	_check(
		"bridge_is_loud_in_provenance",
		String((rule.get("provenance", {}) as Dictionary).get("clip_reload_source", "")) == "continuousFireCoastMs-proxy"
	)
	_check("bridged_period_ticks_is_the_authored_20", int(rule.get("attack_period_ticks", 0)) == 20)

	# A re-cooked pack (importer fixed) carries the real field; it must win
	# over the proxy and mark itself accordingly.
	var cooked := Adapter.normalized_unit_rule(
		_pack_shaped_simulation({"clipReloadTimeMs": 1750.0}), FORDS_SOURCE_SCALE
	)
	_check("cooked_clip_reload_wins", is_equal_approx(float(cooked.get("clip_reload_time_ms", 0.0)), 1750.0))
	_check(
		"cooked_clip_reload_marks_the_real_field",
		String((cooked.get("provenance", {}) as Dictionary).get("clip_reload_source", "")) == "clipReloadTimeMs"
	)

	# No clip reload and no coast: nothing is invented.
	var bare := Adapter.normalized_unit_rule(
		_pack_shaped_simulation({"continuousFireCoastMs": 0.0}), FORDS_SOURCE_SCALE
	)
	_check("no_evidence_no_reload", is_equal_approx(float(bare.get("clip_reload_time_ms", -1.0)), 0.0))
	_check(
		"no_evidence_marks_none",
		String((bare.get("provenance", {}) as Dictionary).get("clip_reload_source", "")) == "none"
	)


func _test_mounted_pack_archer_cadence_matches_retail() -> void:
	## Oracle against the mounted packs: Men archers AND a second faction
	## (Mordor) must carry the authored cycle. Skips loudly with no content;
	## FAILS when OPENBFME_CONTENT is set and the document is missing.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_autoload_available", db != null)
	if db == null:
		return
	db.reload()
	var content_requested := OS.get_environment("OPENBFME_CONTENT").strip_edges() != ""
	var cases := {
		"GondorArcher": {"reload_ms": 2000.0, "pre_attack_ms": 1000.0},
		"MordorArcher": {"reload_ms": 2000.0, "pre_attack_ms": 1000.0},
	}
	for member_id in cases.keys():
		var document: Dictionary = db.get_playable_unit_runtime_for_member(String(member_id))
		if document.is_empty():
			if content_requested:
				_check("mounted_pack_carries_%s" % member_id, false)
			else:
				print("ARCHER_CADENCE_SKIP no_pack_for_%s" % member_id)
			continue
		var simulation := Adapter.simulation_rule(document)
		_check("%s_simulation_rule_resolves" % member_id, not simulation.is_empty())
		if simulation.is_empty():
			continue
		var rule := Adapter.normalized_unit_rule(simulation, FORDS_SOURCE_SCALE)
		_check("%s_unit_rule_normalizes" % member_id, not rule.is_empty())
		if rule.is_empty():
			continue
		var expected: Dictionary = cases[member_id]
		_check(
			"%s_clip_reload_is_the_authored_max" % member_id,
			is_equal_approx(float(rule.get("clip_reload_time_ms", 0.0)), float(expected["reload_ms"]))
		)
		_check(
			"%s_pre_attack_is_authored" % member_id,
			is_equal_approx(float(rule.get("pre_attack_delay_ms", 0.0)), float(expected["pre_attack_ms"]))
		)
		_check(
			"%s_delay_between_shots_is_authored_zero" % member_id,
			is_equal_approx(float(rule.get("delay_between_shots_ms", -1.0)), 0.0)
		)


func _test_sim_measured_cadence_matches_authored() -> void:
	## Behavioural measurement, not a property self-oracle: a Gondor archer
	## horde with the adapter-produced rule attacks a durable target; the
	## ticks between combat.swing events are the actual volley cadence.
	var rule := Adapter.normalized_unit_rule(_pack_shaped_simulation(), 0.1)
	_check("measured_rule_normalizes", not rule.is_empty())
	if rule.is_empty():
		return
	var sim = SimScript.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _harness_rule(SimScript.SOLDIER_HORDE_ID),
			SimScript.ARCHER_OBJECT_ID: rule,
			SimScript.TOWER_GUARD_OBJECT_ID: _harness_rule(SimScript.TOWER_GUARD_OBJECT_ID),
			SimScript.KNIGHT_OBJECT_ID: _harness_rule(SimScript.KNIGHT_OBJECT_ID),
		},
	})
	sim.ai_enabled = false
	(sim.entities[2] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[2] as Dictionary)["destination"] = Vector2.ZERO
	(sim.entities[101] as Dictionary)["position"] = Vector2(1.0, 0.0)
	(sim.entities[101] as Dictionary)["destination"] = Vector2(1.0, 0.0)
	# Both sides must survive several volleys so the interval is measurable.
	for entity_id in [2, 101]:
		var durable: Array = []
		for value in Array((sim.entities[entity_id] as Dictionary).get("member_health", [])):
			durable.append(100000)
		(sim.entities[entity_id] as Dictionary)["member_health"] = durable
	_check("attack_order_accepted", sim.issue_attack([2], 101) == 1)
	sim.advance(AUTHORED_CYCLE_TICKS * 4 + 10)
	var swing_ticks: Array[int] = []
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "combat.swing" and int(event.get("entity_id", 0)) == 2:
			swing_ticks.append(int(event.get("tick", -1)))
	_check("several_volleys_measured", swing_ticks.size() >= 3, str(swing_ticks))
	if swing_ticks.size() < 3:
		return
	var intervals: Array[int] = []
	for index in range(1, swing_ticks.size()):
		intervals.append(swing_ticks[index] - swing_ticks[index - 1])
	var all_authored := true
	for interval in intervals:
		if interval != AUTHORED_CYCLE_TICKS:
			all_authored = false
	_check(
		"measured_ticks_between_volleys_match_authored_cadence",
		all_authored,
		"intervals=%s expected=%d (PreAttackDelay 1000 ms + ClipReloadTime Max 2000 ms; weapon.ini:4231/4239)" % [
			str(intervals), AUTHORED_CYCLE_TICKS
		]
	)
	# The regression this gates: pre-fix the cycle collapsed to the stagger
	# floor of 14 ticks (pre-attack only, reload 0).
	var any_regression_interval := false
	for interval in intervals:
		if interval <= 14:
			any_regression_interval = true
	_check("no_instant_reset_intervals", not any_regression_interval, str(intervals))


func _test_firing_animation_states_split_and_bound() -> void:
	## Retail binds PREATTACK_A -> ATKF1 and FIRING_OR_RELOADING_A -> ATKF2
	## (gondorarcher.ini:236-288). The projection must surface those as the
	## attackRangedPre / attackRangedFire states the battalion already
	## consumes, and the fire clip must be bound to the authored reload span.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_available_for_projection", db != null)
	if db == null:
		return
	var document := {
		"objectId": "SyntheticArcherHorde",
		"registration": {
			"composition": {"primaryMemberObjectId": "SyntheticArcher"},
			"visual": {
				"components": [{"default": true, "output": "synthetic.glb"}],
				"coreAnimations": {
					"attack": [
						{"identifier": "GUArcher_ATKD", "conditions": ["FIRING_OR_PREATTACK_B"]},
						{"identifier": "GUArcher_ATKF1", "conditions": ["PREATTACK_A"]},
						{"identifier": "GUArcher_ATKF2", "conditions": ["FIRING_OR_RELOADING_A"]},
						{"identifier": "GUArcher_RUNB", "conditions": ["ATTACKING", "MOVING"]},
					],
				},
			},
		},
	}
	var projection: Dictionary = db._playable_unit_projection(document)
	var states: Dictionary = (projection.get("capability", {}) as Dictionary).get("states", {})
	_check("ranged_pre_state_projected", states.has("attackRangedPre"))
	_check("ranged_fire_state_projected", states.has("attackRangedFire"))
	if states.has("attackRangedPre"):
		_check(
			"preattack_binds_the_authored_windup_clip",
			Array((states["attackRangedPre"] as Dictionary).get("clips", [])) == ["GUArcher_ATKF1"]
		)
	if states.has("attackRangedFire"):
		_check(
			"fire_binds_the_authored_reload_clip",
			Array((states["attackRangedFire"] as Dictionary).get("clips", [])) == ["GUArcher_ATKF2"]
		)
	# The battalion must stretch the fire clip across the authored reload, and
	# the slice must feed it the sim's reload. Wiring checks, same pattern as
	# attack_cursor_runner's slice-wiring test.
	var battalion_source := FileAccess.get_file_as_string("res://src/retail_slice/retail_battalion.gd")
	_check("battalion_source_readable", battalion_source.length() > 0)
	var clip_start := battalion_source.find("func _play_member_clip")
	var clip_body := battalion_source.substr(clip_start, battalion_source.find("func _process", clip_start) - clip_start)
	_check(
		"fire_clip_is_bound_to_the_authored_reload",
		clip_start >= 0 and clip_body.contains("attack_ranged_fire") and clip_body.contains("attack_reload_seconds")
	)
	var slice_source := FileAccess.get_file_as_string("res://src/retail_slice/retail_vertical_slice.gd")
	_check(
		"slice_feeds_the_sim_reload_to_the_battalion",
		slice_source.contains("set_attack_reload_seconds(_entity_attack_reload_seconds(entity))")
	)


func _harness_rule(horde_id: String) -> Dictionary:
	## Mirrors retail_member_combat_runner's _unit_rule: the sim's entity
	## materialization reads these keys directly, so a missing key aborts the
	## spawn mid-row.
	var positions: Array[Vector3] = []
	for index in range(5):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 17.5,
		"vision_range_source": 175.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_count": 5,
		"formation_positions": positions,
		"provenance": {},
	}


func _check(label: String, condition: bool, detail := "") -> bool:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("ARCHER_CADENCE_FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])
	return condition
