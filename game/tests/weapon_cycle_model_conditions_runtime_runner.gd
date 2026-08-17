extends SceneTree
## Live SAGE weapon-cycle model conditions published by the SIM.
##
## Retail splits a ranged attack into PREATTACK (the PreAttackDelay windup) and
## FIRING_OR_RELOADING (the FiringDuration release plus the reload span) and
## binds a different clip to each — gondorarcher.ini:236-288 authors
## PREATTACK_A -> GUArcher_ATKF1 and FIRING_OR_RELOADING_A -> GUArcher_ATKF2.
## The presenter could not raise either token because nothing exposed the sim's
## weapon-cycle phase; this runner pins that contract:
## `member_weapon_condition_tokens(entity_id)` and its fail-loud companion
## `weapon_condition_deferred_reasons(entity_id)`.
##
## The tokens are DERIVED from the authoritative combat schedule on every call,
## so the runner also proves the query cannot move `state_hash()`.

const Sim := preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 23
const PRE_PROJECTILE_LANE_NO_FIRE_HASH := "f8e9830ffe895af6b9f43a24bf8456e97585b579a8996bf3bfb27a9d6e5f37b6"

## Fixture shaped like the archer: 3-tick windup, 2-tick firing duration,
## 5-tick reload. One member, so the stagger window cannot interleave cycles.
const WINDUP_TICKS := 3
const FIRING_TICKS := 2
const RELOAD_TICKS := 5
const SCAN_TICKS := 40

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "WEAPON_CYCLE_MODEL_CONDITIONS", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_run_archer_cycle()
	_run_weapon_set_conditions()
	_run_deferred_reasons()
	_run_no_projectile_hash_contract()
	_finish()


func _run_no_projectile_hash_contract() -> void:
	var sim = _sim(_archer_rule())
	var authoritative: Dictionary = sim._authoritative_state()
	var actual := String(sim.state_hash())
	_check(
		"a match with no projectile fired keeps the pre-lane hash and absent table",
		actual == PRE_PROJECTILE_LANE_NO_FIRE_HASH
			and not authoritative.has("projectiles")
			and not authoritative.has("next_projectile_id"),
		"expected=%s actual=%s keys=%s" % [PRE_PROJECTILE_LANE_NO_FIRE_HASH, actual, str(authoritative.keys())]
	)


func _run_archer_cycle() -> void:
	var sim = _sim(_archer_rule())
	var attacker: Array[int] = [1]
	_check("attack order is accepted", sim.issue_attack(attacker, 2) == 1)
	var timeline: Array = []
	for _index in range(SCAN_TICKS):
		sim.advance(1)
		var per_member: Array = sim.member_weapon_condition_tokens(1)
		if per_member.size() != 1:
			_check("the sim publishes one token set per member", false, str(per_member))
			return
		timeline.append((per_member[0] as Array).duplicate())
	_check("battalion is engaged for the whole scan", String(sim.entity(1).get("state", "")) == "attack")
	var before_hash := String(sim.state_hash())
	sim.member_weapon_condition_tokens(1)
	_check(
		"deriving the tokens does not touch authoritative state",
		String(sim.state_hash()) == before_hash
	)

	var pre_run := _first_run(timeline, "PREATTACK_A")
	_check(
		"PREATTACK_A spans exactly the authored PreAttackDelay",
		pre_run.y == WINDUP_TICKS,
		"run=%s timeline=%s" % [str(pre_run), str(timeline)]
	)
	var fire_run := _first_run(timeline, "FIRING_A")
	_check(
		"FIRING_A spans exactly the authored FiringDuration",
		fire_run.y == FIRING_TICKS,
		"run=%s timeline=%s" % [str(fire_run), str(timeline)]
	)
	_check(
		"FIRING_A opens on the tick after the windup closes",
		pre_run.x >= 0 and fire_run.x == pre_run.x + pre_run.y,
		"pre=%s fire=%s" % [str(pre_run), str(fire_run)]
	)
	# The next cycle's offset moves with MEMBER_ATTACK_STAGGER_WINDOW_TICKS (the
	# sim staggers each member's swing inside the cadence), so this pins the
	# repeated SHAPE; archer_cadence_runner owns the cadence numbers.
	var next_pre := _first_run(timeline, "PREATTACK_A", fire_run.x + fire_run.y)
	var next_fire := _first_run(timeline, "FIRING_A", fire_run.x + fire_run.y)
	_check(
		"the next cycle repeats the same windup and firing spans",
		next_pre.y == WINDUP_TICKS and next_fire.y == FIRING_TICKS
			and next_fire.x == next_pre.x + next_pre.y,
		"pre=%s fire=%s timeline=%s" % [str(next_pre), str(next_fire), str(timeline)]
	)

	var composite_failures: Array = []
	var exclusive_failures: Array = []
	var reload_failures: Array = []
	for index in range(timeline.size()):
		var tokens: Array = timeline[index]
		var winding: bool = tokens.has("PREATTACK_A")
		var firing: bool = tokens.has("FIRING_A")
		if (winding or firing) != tokens.has("FIRING_OR_PREATTACK_A"):
			composite_failures.append(index)
		if firing and not tokens.has("FIRING_OR_RELOADING_A"):
			composite_failures.append(index)
		if winding and (firing or tokens.has("FIRING_OR_RELOADING_A")):
			exclusive_failures.append(index)
		# Every tick between the release and the next windup is the reload span
		# retail folds into FIRING_OR_RELOADING.
		if index >= fire_run.x + fire_run.y and index < next_pre.x and not tokens.has("FIRING_OR_RELOADING_A"):
			reload_failures.append(index)
	_check(
		"FIRING_OR_PREATTACK_A and FIRING_OR_RELOADING_A follow their parts",
		composite_failures.is_empty(),
		str(composite_failures)
	)
	_check(
		"the windup never overlaps firing or reloading",
		exclusive_failures.is_empty(),
		str(exclusive_failures)
	)
	_check(
		"FIRING_OR_RELOADING_A covers the reload span",
		reload_failures.is_empty(),
		str(reload_failures)
	)
	_check(
		"the pre-stagger ticks raise nothing",
		(timeline[0] as Array).is_empty(),
		str(timeline[0])
	)

	sim.issue_move(attacker, Vector2(40.0, 40.0))
	sim.advance(1)
	var cleared: Array = sim.member_weapon_condition_tokens(1)
	_check(
		"leaving the engagement clears every weapon-cycle flag",
		cleared.size() == 1 and (cleared[0] as Array).is_empty(),
		"state=%s tokens=%s" % [String(sim.entity(1).get("state", "")), str(cleared)]
	)


func _run_weapon_set_conditions() -> void:
	var sim = _sim(_toggle_rule())
	var row: Dictionary = sim.entities[1]
	_check(
		"the base set raises no weapon-set condition",
		(sim.member_weapon_condition_tokens(1)[0] as Array).is_empty()
	)
	_check(
		"an engaged toggle set raises its retail token",
		sim._apply_weapon_mode(row, "weaponset_toggle_1")
			and (sim.member_weapon_condition_tokens(1)[0] as Array) == ["WEAPONSET_TOGGLE_1"]
	)
	# The toggle profile carries SECONDARY, so its cycle tokens must be the _B
	# family, not the default set's _A.
	row["state"] = "attack"
	row["member_weapon_modes"] = ["weaponset_toggle_1"]
	row["member_attack_start_ticks"] = [-1]
	row["member_attack_hit_ticks"] = [sim.tick_index + WINDUP_TICKS]
	var slot_tokens: Array = sim.member_weapon_condition_tokens(1)[0]
	_check(
		"the slot letter follows the authored WeaponSlot",
		slot_tokens.has("PREATTACK_B") and slot_tokens.has("FIRING_OR_PREATTACK_B") and not slot_tokens.has("PREATTACK_A"),
		str(slot_tokens)
	)
	row["state"] = "idle"
	_check(
		"the close-range set raises CLOSE_RANGE",
		sim._apply_weapon_mode(row, "close")
			and (sim.member_weapon_condition_tokens(1)[0] as Array) == ["CLOSE_RANGE"]
	)
	row["weapon_set_flags"] = ["WEAPONSET_TOGGLE_1"]
	_check(
		"live weapon-set flags union into the token set",
		(sim.member_weapon_condition_tokens(1)[0] as Array) == ["CLOSE_RANGE", "WEAPONSET_TOGGLE_1"],
		str(sim.member_weapon_condition_tokens(1))
	)
	row["member_health"] = [0]
	_check(
		"a dead member raises nothing",
		(sim.member_weapon_condition_tokens(1)[0] as Array).is_empty()
	)


func _run_deferred_reasons() -> void:
	var reasons: Array = _sim(_archer_rule()).weapon_condition_deferred_reasons(1)
	_check(
		"an authored slot and firing duration are not receipted as gaps",
		not reasons.has("weapon-slot-unauthored:default") and not reasons.has("firing-duration-zero:default"),
		str(reasons)
	)
	_check(
		"the instant weapon swap is receipted, not faked as SWAPPING_TO_WEAPONSET_*",
		reasons.has("swapping-to-weaponset-not-modelled") and reasons.has("weapon-slot-d-absent-from-retail-corpus"),
		str(reasons)
	)
	var blind := _archer_rule()
	var blind_mode: Dictionary = (blind["weapon_modes"] as Dictionary)["default"]
	blind_mode.erase("weapon_slot")
	blind_mode["firing_duration_ticks"] = 0
	var blind_sim = _sim(blind)
	var blind_reasons: Array = blind_sim.weapon_condition_deferred_reasons(1)
	_check(
		"an unauthored WeaponSlot and a zero FiringDuration are both receipted",
		blind_reasons.has("weapon-slot-unauthored:default") and blind_reasons.has("firing-duration-zero:default"),
		str(blind_reasons)
	)
	var blind_attacker: Array[int] = [1]
	blind_sim.issue_attack(blind_attacker, 2)
	blind_sim.advance(WINDUP_TICKS + 2)
	_check(
		"a unit whose slot cannot be named raises no lettered token",
		(blind_sim.member_weapon_condition_tokens(1)[0] as Array).is_empty(),
		str(blind_sim.member_weapon_condition_tokens(1))
	)


func _sim(attacker_rule: Dictionary):
	var target_rule := _archer_rule()
	target_rule["horde_id"] = "FixtureTarget"
	target_rule["member_health"] = 4000
	var rules := {"FixtureCycle": attacker_rule, "FixtureTarget": target_rule}
	# setup() seeds the default roster before this runner clears it; give those
	# object ids a rule so the seeding does not emit unrelated push_errors.
	for object_id in [
		Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID,
	]:
		rules[object_id] = target_rule
	var sim = Sim.new()
	sim.setup({}, {
		"unit_rules": rules,
		"source_map_transform_scale": 0.1,
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Cycle", "FixtureCycle", "FixtureCycle", 0, attacker_rule)
	sim._add_battalion(2, Sim.ENEMY_TEAM, Vector2(2.0, 0.0), "Target", "FixtureTarget", "FixtureTarget", 0, target_rule)
	return sim


func _weapon_mode(slot: String, firing_ticks: int) -> Dictionary:
	return {
		"name": "Fixture%s" % slot.capitalize(),
		"weapon_slot": slot,
		"attack_range": 10.0,
		"attack_range_source": 100.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"delay_between_shots_ms": float(RELOAD_TICKS) * 100.0,
		"pre_attack_delay_ms": float(WINDUP_TICKS) * 100.0,
		"firing_duration_ms": float(firing_ticks) * 100.0,
		"attack_period_ticks": WINDUP_TICKS + firing_ticks + RELOAD_TICKS,
		"pre_attack_ticks": WINDUP_TICKS,
		"firing_duration_ticks": firing_ticks,
		"member_damage": 1,
	}


func _archer_rule() -> Dictionary:
	return {
		"horde_id": "FixtureCycle",
		"category": "ranged-infantry",
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 10.0,
		"attack_range_source": 100.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 10.0,
		"vision_range_source": 100.0,
		"delay_between_shots_ms": float(RELOAD_TICKS) * 100.0,
		"pre_attack_delay_ms": float(WINDUP_TICKS) * 100.0,
		"firing_duration_ms": float(FIRING_TICKS) * 100.0,
		"attack_period_ticks": WINDUP_TICKS + FIRING_TICKS + RELOAD_TICKS,
		"pre_attack_ticks": WINDUP_TICKS,
		"firing_duration_ticks": FIRING_TICKS,
		"member_damage": 1,
		"member_health": 400,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"weapon_modes": {"default": _weapon_mode("primary", FIRING_TICKS)},
		"default_weapon_mode": "default",
		"close_weapon_mode": "",
		"provenance": {},
	}


func _toggle_rule() -> Dictionary:
	var rule := _archer_rule()
	var modes: Dictionary = rule["weapon_modes"]
	modes["weaponset_toggle_1"] = _weapon_mode("secondary", FIRING_TICKS)
	modes["close"] = _weapon_mode("secondary", FIRING_TICKS)
	rule["close_weapon_mode"] = "close"
	return rule


func _first_run(timeline: Array, token: String, from_index: int = 0) -> Vector2i:
	## Start index and length of the first contiguous run of `token`.
	var start := -1
	for index in range(maxi(0, from_index), timeline.size()):
		var present: bool = (timeline[index] as Array).has(token)
		if present and start < 0:
			start = index
		elif not present and start >= 0:
			return Vector2i(start, index - start)
	if start < 0:
		return Vector2i(-1, 0)
	return Vector2i(start, timeline.size() - start)


func _check(label: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("WEAPON_CYCLE_MODEL_CONDITIONS_FAIL %s %s" % [label, detail])


func _finish() -> void:
	print("WEAPON_CYCLE_MODEL_CONDITIONS_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
