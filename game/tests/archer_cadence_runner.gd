extends SceneTree
## Archer fire-rate must match the retail-authored weapon cadence, and the
## firing animation must finish early at the authored speed factor.
##
## Retail truth (PURE RotWK 2.01 tree,
## workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * weapon.ini:4230-4241 GondorArcherBow: DelayBetweenShots = 0,
##     PreAttackDelay = 1000 (gamedata.ini:1857), PreAttackType = PER_POSITION
##     (:4233), PreAttackRandomAmount = 200 (:4232, compiled, deterministic
##     use deferred), FiringDuration = 0, ClipSize = 1,
##     ClipReloadTime = Min:1500 Max:2000 (weapon.ini:4239,
##     macros at gamedata.ini:1858-1859), ContinuousFireCoast =
##     GONDOR_ARCHER_BOW_RELOADTIME_MAX (:4241).
##   * weapon.ini:10400-10409 MordorArcherBow: same shape, PER_POSITION.
##   * weapon.ini:10808 HaradrimBow: PreAttackType = PER_SHOT — every volley
##     charges the windup.
##   * PER_POSITION: PreAttackDelay is charged only on a NEW target/attack
##     position (and the first shot). Sustained fire on a stationary target
##     cycles at firing + clip reload only → Gondor 1500-2000 ms. Deterministic
##     Max convention → 2000 ms = 20 ticks.
##   * ContinuousFireCoast is NOT a universal clip-reload stand-in. Five
##     retail counterexamples: CreateAHeroBasicRangedWeapon (coast 2000 vs
##     reload max 1500), LegolasHawkStrike / DwarvenMenOfDaleBlackArrows /
##     GoblinArcherPoisonArrows (no coast), WildSpiderRiderBow (coast
##     deliberately commented out). The proxy is legal ONLY when the pack
##     provenance authors coast as a *_RELOADTIME_MAX token (34 of 35 shipped
##     combos).
##   * gondorarcher.ini:251/261 comments UseWeaponTiming OUT; :262 authors
##     AnimationSpeedFactorRange = 1.2 1.3 so the fire clip finishes before
##     the randomized reload.
##
## Run:
##   OPENBFME_CONTENT=<repo>/workspace/content-packs godot --headless --path game \
##     --script res://tests/archer_cadence_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## 76.0 / 2868.7 — the Fords of Isen map transform scale (see
## selection_pick_footprint_runner.gd header).
const FORDS_SOURCE_SCALE := 0.026492
## Sustained PER_POSITION cycle: ClipReloadTime Max 2000 ms = 20 ticks.
## Windup (PreAttackDelay 1000 ms = 10 ticks) is added only on the first
## shot / target switch, so first swing-to-swing is 30 and the rest are 20.
const AUTHORED_RELOAD_TICKS := 20
const AUTHORED_WINDUP_TICKS := 10
const AUTHORED_FIRST_CYCLE_TICKS := AUTHORED_WINDUP_TICKS + AUTHORED_RELOAD_TICKS
## gondorarcher.ini:262 min of AnimationSpeedFactorRange 1.2 1.3.
const AUTHORED_FIRE_SPEED_FACTOR := 1.2
## Authored ClipReloadTime Min (gamedata.ini:1858).
const AUTHORED_RELOAD_MIN_SECONDS := 1.5

## LIVENESS: a mid-body script error aborts the enclosing function without
## propagating. Completion is marked only at the END of each section, and
## the exact check count must match EXPECTED_CHECKS (camera-runner pattern).
const EXPECTED_TESTS := 5
const EXPECTED_CHECKS := 56

var passed := 0
var failed := 0
var _completed: Array[String] = []
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ARCHER_CADENCE")
	call_deferred("_run")


func _run() -> void:
	_test_adapter_bridges_the_dropped_clip_reload()
	_test_mounted_pack_archer_cadence_matches_retail()
	_test_sim_measured_cadence_matches_authored()
	_test_per_shot_keeps_every_volley_windup()
	_test_firing_animation_finishes_early_at_authored_speed()

	if _completed.size() != EXPECTED_TESTS:
		failed += 1
		push_error("ARCHER_CADENCE_FAIL liveness: %d/%d sections reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		push_error("ARCHER_CADENCE_FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, EXPECTED_CHECKS
		])
	print("ARCHER_CADENCE_RESULT passed=%d failed=%d sections=%d checks=%d" % [
		passed, failed, _completed.size(), ran
	])
	quit(0 if failed == 0 else 1)


## The combat block exactly as the CURRENT men pack carries it (cooked before
## the importer learned the ranged form): no clipReloadTimeMs key at all.
## The coast expression is the pack provenance that makes the proxy legal.
func _pack_shaped_simulation(combat_overrides: Dictionary = {}) -> Dictionary:
	var combat := {
		"attackRange": 300.0,
		"delayBetweenShotsMs": 0.0,
		"preAttackDelayMs": 1000.0,
		"preAttackType": "PER_POSITION",
		"preAttackRandomAmountMs": 200.0,
		"firingDurationMs": 0.0,
		"damage": 35,
		"damageType": "pierce",
		"clipSize": 1,
		"continuousFireOne": 0,
		"continuousFireCoastMs": 2000.0,
		"continuousFireCoastExpression": "GONDOR_ARCHER_BOW_RELOADTIME_MAX",
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
		_completed.append("adapter-bridge")
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
	_check(
		"pack_shaped_pre_attack_type_is_per_position",
		String(rule.get("pre_attack_type", "")) == "PER_POSITION"
	)
	_check(
		"pre_attack_random_amount_is_compiled",
		is_equal_approx(float(rule.get("pre_attack_random_amount_ms", 0.0)), 200.0)
	)

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

	# Literal coast with no *_RELOADTIME_MAX token (CreateAHero shape: coast
	# 2000 vs reload max 1500) must NOT silently proxy.
	var literal := Adapter.normalized_unit_rule(
		_pack_shaped_simulation({
			"continuousFireCoastExpression": "2000",
			"continuousFireCoastMs": 2000.0,
		}),
		FORDS_SOURCE_SCALE
	)
	_check(
		"literal_coast_does_not_proxy",
		String((literal.get("provenance", {}) as Dictionary).get("clip_reload_source", "")) == "unresolved"
	)
	_check("literal_coast_reload_stays_zero", is_equal_approx(float(literal.get("clip_reload_time_ms", -1.0)), 0.0))

	# No clip reload and no coast: loud unresolved, not a silent invention.
	var bare := Adapter.normalized_unit_rule(
		_pack_shaped_simulation({
			"continuousFireCoastMs": 0.0,
			"continuousFireCoastExpression": "",
		}),
		FORDS_SOURCE_SCALE
	)
	_check("no_evidence_no_reload", is_equal_approx(float(bare.get("clip_reload_time_ms", -1.0)), 0.0))
	_check(
		"no_evidence_marks_unresolved",
		String((bare.get("provenance", {}) as Dictionary).get("clip_reload_source", "")) == "unresolved"
	)
	_completed.append("adapter-bridge")


func _test_mounted_pack_archer_cadence_matches_retail() -> void:
	## Oracle against the mounted packs: Men archers AND a second faction
	## (Mordor) must carry the authored cycle. Skips loudly with no content;
	## FAILS when OPENBFME_CONTENT is set and the document is missing.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_autoload_available", db != null)
	if db == null:
		_completed.append("mounted-pack-cadence")
		return
	db.reload()
	var content_requested := OS.get_environment("OPENBFME_CONTENT").strip_edges() != ""
	var cases := {
		"GondorArcher": {"reload_ms": 2000.0, "pre_attack_ms": 1000.0, "pre_attack_type": "PER_POSITION"},
		"MordorArcher": {"reload_ms": 2000.0, "pre_attack_ms": 1000.0, "pre_attack_type": "PER_POSITION"},
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
		_check(
			"%s_pre_attack_type_is_per_position" % member_id,
			String(rule.get("pre_attack_type", "")) == String(expected["pre_attack_type"])
		)
		var coast_source := String((rule.get("provenance", {}) as Dictionary).get("clip_reload_source", ""))
		_check(
			"%s_clip_reload_source_is_legal" % member_id,
			coast_source == "clipReloadTimeMs" or coast_source == "continuousFireCoastMs-proxy"
		)
	_completed.append("mounted-pack-cadence")


func _test_sim_measured_cadence_matches_authored() -> void:
	## Behavioural measurement: a Gondor archer horde with the adapter-produced
	## PER_POSITION rule attacks a durable target. First volley includes the
	## windup; sustained volleys are firing + clip reload only (20 ticks).
	var rule := Adapter.normalized_unit_rule(_pack_shaped_simulation(), 0.1)
	_check("measured_rule_normalizes", not rule.is_empty())
	if rule.is_empty():
		_completed.append("sim-measured-cadence")
		return
	var sim = SimScript.new()
	sim.setup({}, {"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(), 
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
	(sim.entities[102] as Dictionary)["position"] = Vector2(1.0, 1.0)
	(sim.entities[102] as Dictionary)["destination"] = Vector2(1.0, 1.0)
	for entity_id in [2, 101, 102]:
		var durable: Array = []
		for value in Array((sim.entities[entity_id] as Dictionary).get("member_health", [])):
			durable.append(100000)
		(sim.entities[entity_id] as Dictionary)["member_health"] = durable
	_check("attack_order_accepted", sim.issue_attack([2], 101) == 1)
	sim.advance(AUTHORED_FIRST_CYCLE_TICKS + AUTHORED_RELOAD_TICKS * 4 + 10)
	var swing_ticks: Array[int] = []
	var charged: Array[bool] = []
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "combat.swing" and int(event.get("entity_id", 0)) == 2:
			swing_ticks.append(int(event.get("tick", -1)))
			charged.append(bool(event.get("charged_pre_attack", false)))
	_check("several_volleys_measured", swing_ticks.size() >= 4, str(swing_ticks))
	if swing_ticks.size() < 4:
		_completed.append("sim-measured-cadence")
		return
	_check("first_volley_charges_windup", charged.size() > 0 and charged[0])
	var sustained_all_uncharged := true
	for index in range(1, charged.size()):
		if charged[index]:
			sustained_all_uncharged = false
	_check("sustained_volleys_skip_windup", sustained_all_uncharged, str(charged))
	var intervals: Array[int] = []
	for index in range(1, swing_ticks.size()):
		intervals.append(swing_ticks[index] - swing_ticks[index - 1])
	_check(
		"first_interval_includes_windup",
		intervals.size() > 0 and intervals[0] == AUTHORED_FIRST_CYCLE_TICKS,
		"intervals=%s expected_first=%d" % [str(intervals), AUTHORED_FIRST_CYCLE_TICKS]
	)
	var sustained_ok := true
	for index in range(1, intervals.size()):
		if intervals[index] != AUTHORED_RELOAD_TICKS:
			sustained_ok = false
	_check(
		"sustained_ticks_between_volleys_are_reload_only",
		sustained_ok and intervals.size() >= 2,
		"intervals=%s expected_sustained=%d (ClipReloadTime Max 2000 ms; PER_POSITION skips PreAttackDelay; weapon.ini:4233/4239)" % [
			str(intervals), AUTHORED_RELOAD_TICKS
		]
	)
	var any_regression_interval := false
	for interval in intervals:
		if interval <= 14:
			any_regression_interval = true
	_check("no_instant_reset_intervals", not any_regression_interval, str(intervals))

	# Target switch re-charges the PER_POSITION windup.
	_check("retarget_accepted", sim.issue_attack([2], 102) == 1)
	var before_switch := swing_ticks.size()
	sim.advance(AUTHORED_FIRST_CYCLE_TICKS + AUTHORED_RELOAD_TICKS + 5)
	var switch_charged := false
	for event_value in sim.events:
		var event := event_value as Dictionary
		if (
			String(event.get("kind", "")) == "combat.swing"
			and int(event.get("entity_id", 0)) == 2
			and int(event.get("tick", -1)) > swing_ticks[swing_ticks.size() - 1]
		):
			if bool(event.get("charged_pre_attack", false)):
				switch_charged = true
				break
	_check("target_switch_recharges_windup", switch_charged)
	_check("retarget_produced_a_new_volley", _swing_count(sim, 2) > before_switch)
	_completed.append("sim-measured-cadence")


func _test_per_shot_keeps_every_volley_windup() -> void:
	## HaradrimBow (weapon.ini:10808) is PER_SHOT: every volley includes
	## PreAttackDelay. Delay 900 + pre 2100 + firing 700 is a different
	## shape; this fixture keeps Gondor numbers and only flips the type so
	## the interval isolates the type semantics.
	var rule := Adapter.normalized_unit_rule(
		_pack_shaped_simulation({"preAttackType": "PER_SHOT"}), 0.1
	)
	_check("per_shot_rule_normalizes", not rule.is_empty())
	if rule.is_empty():
		_completed.append("per-shot-windup")
		return
	_check("per_shot_type_honored", String(rule.get("pre_attack_type", "")) == "PER_SHOT")
	var sim = SimScript.new()
	sim.setup({}, {"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(), 
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
	for entity_id in [2, 101]:
		var durable: Array = []
		for value in Array((sim.entities[entity_id] as Dictionary).get("member_health", [])):
			durable.append(100000)
		(sim.entities[entity_id] as Dictionary)["member_health"] = durable
	_check("per_shot_attack_accepted", sim.issue_attack([2], 101) == 1)
	sim.advance(AUTHORED_FIRST_CYCLE_TICKS * 4 + 10)
	var intervals: Array[int] = []
	var last_tick := -1
	var all_charged := true
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) != "combat.swing" or int(event.get("entity_id", 0)) != 2:
			continue
		if not bool(event.get("charged_pre_attack", false)):
			all_charged = false
		var tick := int(event.get("tick", -1))
		if last_tick >= 0:
			intervals.append(tick - last_tick)
		last_tick = tick
	_check("per_shot_several_volleys", intervals.size() >= 2, str(intervals))
	var all_windup := true
	for interval in intervals:
		if interval != AUTHORED_FIRST_CYCLE_TICKS:
			all_windup = false
	_check(
		"per_shot_every_interval_includes_windup",
		all_windup and intervals.size() >= 2,
		"intervals=%s expected=%d" % [str(intervals), AUTHORED_FIRST_CYCLE_TICKS]
	)
	_check("per_shot_every_volley_charges", all_charged)
	_completed.append("per-shot-windup")


func _test_firing_animation_finishes_early_at_authored_speed() -> void:
	## Retail binds PREATTACK_A -> ATKF1 and FIRING_OR_RELOADING_A -> ATKF2
	## (gondorarcher.ini:236-288). The fire clip plays at the authored speed
	## factor and finishes before the min reload — it is not stretched.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_available_for_projection", db != null)
	if db == null:
		_completed.append("animation-binding")
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
						{"identifier": "GUArcher_ATKF2", "conditions": ["FIRING_OR_RELOADING_A"], "AnimationSpeedFactorRange": [1.2, 1.3]},
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
		var fire_state: Dictionary = states["attackRangedFire"] as Dictionary
		_check(
			"fire_binds_the_authored_reload_clip",
			Array(fire_state.get("clips", [])) == ["GUArcher_ATKF2"]
		)
		_check("fire_does_not_use_weapon_timing", fire_state.get("useWeaponTiming", true) == false)
		_check(
			"fire_speed_factor_is_the_authored_min",
			is_equal_approx(float(fire_state.get("speedFactor", 0.0)), AUTHORED_FIRE_SPEED_FACTOR)
		)

	# Behavioural: a real AnimationPlayer plays the fire clip at the authored
	# factor; effective duration (length / speed_scale) <= min authored reload.
	var battalion_script: Script = load("res://src/retail_slice/retail_battalion.gd")
	_check("battalion_script_loads", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		_completed.append("animation-binding")
		return
	var battalion = battalion_script.new()
	root.add_child(battalion)
	battalion.attack_fire_speed_factor = AUTHORED_FIRE_SPEED_FACTOR
	var player := AnimationPlayer.new()
	var animation := Animation.new()
	animation.length = 1.8
	var library := AnimationLibrary.new()
	library.add_animation("GUArcher_ATKF2", animation)
	player.add_animation_library("", library)
	battalion.add_child(player)
	battalion._play_member_clip(player, "GUArcher_ATKF2", "attack_ranged_fire", 0, 0.0, false)
	_check("fire_clip_actually_plays", player.is_playing() and player.current_animation == "GUArcher_ATKF2")
	_check(
		"fire_clip_plays_at_authored_speed",
		is_equal_approx(player.speed_scale, AUTHORED_FIRE_SPEED_FACTOR),
		"speed_scale=%s" % str(player.speed_scale)
	)
	var effective := animation.length / maxf(player.speed_scale, 0.001)
	_check(
		"fire_clip_effective_duration_le_min_reload",
		effective <= AUTHORED_RELOAD_MIN_SECONDS + 0.0001,
		"effective=%s min_reload=%s" % [str(effective), str(AUTHORED_RELOAD_MIN_SECONDS)]
	)
	_check("fire_clip_is_not_stretched_to_reload", player.speed_scale >= AUTHORED_FIRE_SPEED_FACTOR - 0.001)
	battalion.queue_free()
	_completed.append("animation-binding")


func _swing_count(sim, entity_id: int) -> int:
	var count := 0
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "combat.swing" and int(event.get("entity_id", 0)) == entity_id:
			count += 1
	return count


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
