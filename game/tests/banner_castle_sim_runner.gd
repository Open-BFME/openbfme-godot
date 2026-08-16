extends SceneTree

## Headless pure-sim proof for banner spawn/death/respawn and castle BSE pads.
## No ContentDB / pack load — injects contracts the same shape the convert pack emits.
##
##   Godot --headless --path game -s res://tests/banner_castle_sim_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var _runner_watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0
var SimClass: GDScript


func _initialize() -> void:
	_runner_watchdog.start(self, "BANNER_CASTLE_SIM_RUNNER")
	call_deferred("_run")


func _run() -> void:
	# Load after autoloads so class_name / parse-order matches other runners.
	SimClass = load("res://src/retail_slice/retail_slice_sim.gd") as GDScript
	if SimClass == null:
		printerr("BANNER_CASTLE_SIM_RUNNER FAIL cannot load retail_slice_sim.gd")
		quit(1)
		return
	_test_banner_spawn_and_respawn()
	_test_banner_destroy_horde()
	_test_castle_bse_pads()
	_test_castle_melee_approach()
	_test_castle_corridor_is_bounded()
	_test_castle_pathing_lifecycle()
	_test_structure_deflection_tangential_slide()
	_test_eviction_scope()
	_test_structure_surface_range()
	_test_tangential_slide_near_zero_band()
	_test_moving_unit_tangential_compounding()
	_test_banner_carrier_eviction_exclusion()
	_test_auto_acquire_structure_footprint()
	_test_small_structure_footprint_fallback()
	if failed == 0:
		print("BANNER_CASTLE_SIM_RUNNER PASS checks=%d" % passed)
		quit(0)
	else:
		printerr("BANNER_CASTLE_SIM_RUNNER FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)


func _check(cond: bool, name: String, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])


func _make_sim() -> Object:
	var sim: Object = SimClass.new()
	sim.setup({}, {
		"member_health": 100,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
		"unit_rules": {
			"bfme2.object.gondor-fighter-horde": {
				"horde_id": "bfme2.object.gondor-fighter-horde",
				"speed": 5.5,
				"speed_source": 55.0,
				"acceleration": 1.0,
				"acceleration_source": 10.0,
				"turn_rate_degrees_per_second": 180.0,
				"braking": 1.0,
				"braking_source": 10.0,
				"attack_range": 1.15,
				"attack_range_source": 11.5,
				"minimum_attack_range": 0.0,
				"minimum_attack_range_source": 0.0,
				"vision_range": 40.0,
				"vision_range_source": 400.0,
				"delay_between_shots_ms": 600.0,
				"pre_attack_delay_ms": 200.0,
				"firing_duration_ms": 200.0,
				"attack_period_ticks": 10,
				"pre_attack_ticks": 2,
				"firing_duration_ticks": 2,
				"member_damage": 10,
				"member_health": 100,
				"member_count": 5,
				"formation_positions": [Vector3.ZERO],
				"provenance": {},
				"is_builder": false,
			},
			"bfme2.object.gondor-infantry-banner": {
				"horde_id": "bfme2.object.gondor-infantry-banner",
				"speed": 5.5,
				"speed_source": 55.0,
				"acceleration": 1.0,
				"acceleration_source": 10.0,
				"turn_rate_degrees_per_second": 180.0,
				"braking": 1.0,
				"braking_source": 10.0,
				"attack_range": 1.15,
				"attack_range_source": 11.5,
				"minimum_attack_range": 0.0,
				"minimum_attack_range_source": 0.0,
				"vision_range": 40.0,
				"vision_range_source": 400.0,
				"delay_between_shots_ms": 600.0,
				"pre_attack_delay_ms": 200.0,
				"firing_duration_ms": 200.0,
				"attack_period_ticks": 10,
				"pre_attack_ticks": 2,
				"firing_duration_ticks": 2,
				"member_damage": 1,
				"member_health": 80,
				"member_count": 1,
				"formation_positions": [Vector3.ZERO],
				"provenance": {},
				"is_builder": false,
			},
		},
		"faction_manifest": {"structure_armor": {}},
	})
	sim.ai_enabled = false
	return sim


func _test_banner_spawn_and_respawn() -> void:
	print("--- banner spawn + respawn ---")
	var sim: Object = _make_sim()
	sim._unit_banner_carriers["bfme2.object.gondor-fighter-horde"] = {
		"object_id": "bfme2.object.gondor-infantry-banner",
		"source_banner_object_id": "GondorInfantryBanner",
		"min_level": 2,
		"offset_source": Vector2(70.0, 0.0),
		"destroy_horde_on_death": false,
		"banner_max_health": 80,
	}
	# 10000ms / 100ms tick = 100 ticks
	sim._banner_respawn_ticks_by_object["bfme2.object.gondor-infantry-banner"] = 3
	sim._banner_respawn_ticks_by_object["GondorInfantryBanner"] = 3

	var horde_id := 501
	sim._next_dynamic_id[0] = 600
	sim._add_battalion(
		horde_id,
		0,
		Vector2(10.0, 20.0),
		"Fighter Horde",
		"bfme2.object.gondor-fighter-horde",
		"bfme2.object.gondor-fighter-horde",
		0
	)
	var horde: Dictionary = sim.entities[horde_id]
	horde["level"] = 1
	sim._refresh_banner_carrier_state(horde)
	_check(not bool(horde.get("banner_carrier_spawned", false)), "no banner at rank 1")
	_check(int(horde.get("banner_entity_id", 0)) == 0, "no banner entity at rank 1")

	horde["level"] = 2
	sim._refresh_banner_carrier_state(horde)
	_check(bool(horde.get("banner_carrier_spawned", false)), "banner spawned at rank 2")
	var banner_id := int(horde.get("banner_entity_id", 0))
	_check(banner_id != 0 and sim.entities.has(banner_id), "banner entity exists")
	if banner_id != 0 and sim.entities.has(banner_id):
		var banner: Dictionary = sim.entities[banner_id]
		_check(bool(banner.get("is_banner_carrier", false)), "banner flagged is_banner_carrier")
		var expected := Vector2(10.0, 20.0) + Vector2(7.0, 0.0)  # 70 * 0.1
		_check(
			Vector2(banner.get("position", Vector2.ZERO)).distance_to(expected) < 0.001,
			"banner offset 70 source -> 7 sim",
			"pos=%s expected=%s" % [banner.get("position"), expected]
		)

	# Kill banner -> respawn arm
	sim.script_kill_entity(banner_id)
	_check(not sim.entities.has(banner_id), "banner entity removed on death")
	_check(not bool(horde.get("banner_carrier_spawned", false)), "parent clears spawned flag")
	_check(int(horde.get("banner_respawn_ticks_remaining", -1)) == 3, "respawn timer armed to 3")
	_check(int(horde.get("health", 0)) > 0, "parent horde still alive (destroy=false)")

	# Drive the banner step directly (full tick also works; keep the test focused).
	sim.tick_index += 1
	sim._step_banner_carriers()  # 3 -> 2
	sim.tick_index += 1
	sim._step_banner_carriers()  # 2 -> 1
	_check(int(horde.get("banner_respawn_ticks_remaining", -1)) == 1, "countdown 1", "got=%s" % horde.get("banner_respawn_ticks_remaining"))
	sim.tick_index += 1
	sim._step_banner_carriers()  # 1 -> 0 + respawn
	_check(bool(horde.get("banner_carrier_spawned", false)), "banner respawned")
	var new_banner := int(horde.get("banner_entity_id", 0))
	_check(new_banner != 0 and new_banner != banner_id and sim.entities.has(new_banner), "new banner entity id", "new=%s old=%s" % [new_banner, banner_id])


func _test_banner_destroy_horde() -> void:
	print("--- banner destroy horde ---")
	var sim: Object = _make_sim()
	sim._unit_banner_carriers["bfme2.object.gondor-fighter-horde"] = {
		"object_id": "bfme2.object.gondor-infantry-banner",
		"source_banner_object_id": "AngmarThrallMasterBanner",
		"min_level": 0,
		"offset_source": Vector2.ZERO,
		"destroy_horde_on_death": true,
		"banner_max_health": 80,
	}
	var horde_id := 701
	sim._next_dynamic_id[0] = 800
	sim._add_battalion(
		horde_id,
		0,
		Vector2.ZERO,
		"Thrall",
		"bfme2.object.gondor-fighter-horde",
		"bfme2.object.gondor-fighter-horde",
		0
	)
	var horde: Dictionary = sim.entities[horde_id]
	horde["level"] = 1
	# Treat this horde as a live summon so banner-carrier destroy-horde exercises
	# the same lifetime registry and command-point bookkeeping as retail thralls.
	horde["command_points"] = 10
	sim.base_loop_enabled = true
	sim.team_command_points[0] = 10
	sim._summon_despawn_ticks[horde_id] = sim.tick_index + 2
	horde["summon_lifetime_death_type"] = "FADED"
	sim._refresh_banner_carrier_state(horde)
	var banner_id := int(horde.get("banner_entity_id", 0))
	_check(banner_id != 0, "thrall banner spawns at minLevel 0")
	sim.script_kill_entity(banner_id)
	_check(int(horde.get("health", 1)) == 0, "destroyHorde kills parent")
	_check(String(horde.get("state", "")) == "death", "parent state death")
	_check(
		not sim._summon_despawn_ticks.has(horde_id),
		"banner-killed summoned horde erases lifetime registry"
	)
	_check(
		bool(horde.get("command_points_released", false))
			and int(sim.team_command_points.get(0, -1)) == 0,
		"banner-killed summoned horde releases command points once"
	)
	var expired_before := _event_count(sim, "power.summon_expired")
	for _tick in 3:
		sim.winner = -1
		sim.tick()
	_check(
		_event_count(sim, "power.summon_expired") == expired_before,
		"banner-killed summoned horde emits no bogus expiry"
	)
	var corpse_expire_tick := int(horde.get("corpse_expire_tick", -1))
	sim.tick_index = corpse_expire_tick
	sim._cleanup_expired_corpses()
	_check(not sim.entities.has(horde_id), "banner-killed summoned horde corpse is reaped")


func _event_count(sim: Object, kind: String) -> int:
	var count := 0
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			count += 1
	return count


func _test_castle_bse_pads() -> void:
	print("--- castle BSE pads ---")
	var sim: Object = _make_sim()
	sim.configure_castle_behaviors({
		"MenFortress": {
			"faction": "Men",
			"castle_template_token": "Fortress_Men",
			"pieces": [
				{"index": 0, "source_object_id": "MenFortressExpansionPadSide", "object_id": "MenFortressExpansionPadSide", "maximum_health": 100, "offset_source": Vector2(0.0, 62.0), "angle_radians": 1.5708},
				{"index": 1, "source_object_id": "MenFortressExpansionPadSide", "object_id": "MenFortressExpansionPadSide", "maximum_health": 100, "offset_source": Vector2(0.0, -62.0), "angle_radians": -1.5708},
				{"index": 2, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 100, "offset_source": Vector2(-64.0, -64.0), "angle_radians": -2.356},
				{"index": 3, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 100, "offset_source": Vector2(64.0, -64.0), "angle_radians": -0.785},
				{"index": 4, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 100, "offset_source": Vector2(-64.0, 64.0), "angle_radians": 2.356},
				{"index": 5, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 100, "offset_source": Vector2(64.0, 64.0), "angle_radians": 0.785},
				{"index": 6, "source_object_id": "MenFortressCitadel", "object_id": "MenFortressCitadel", "maximum_health": 5000, "offset_source": Vector2.ZERO, "angle_radians": 0.0},
			],
		}
	})
	var fortress_id := 50
	sim.structures[fortress_id] = {
		"id": fortress_id,
		"team": 0,
		"kind": "structure",
		"structure_kind": "fortress",
		"name": "Fortress",
		"source_object_id": "MenFortress",
		"position": Vector2(100.0, 200.0),
		"health": 5000,
		"maximum_health": 5000,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
	}
	sim._seed_expansion_pads_for(fortress_id)
	var pads: Array = sim.expansion_pad_states(fortress_id)
	_check(pads.size() == 6, "6 expansion pads from BSE (citadel separate)", "count=%d" % pads.size())
	_check(bool((sim.structures[fortress_id] as Dictionary).get("castle_behavior_unpacked", false)), "castle_behavior_unpacked flag")
	# Citadel spawned
	var citadel_count := 0
	for sid in sim.structure_ids():
		var s: Dictionary = sim.structures[sid]
		if String(s.get("source_object_id", "")) == "MenFortressCitadel":
			citadel_count += 1
			_check(
				Vector2(s.get("position", Vector2.ZERO)).distance_to(Vector2(100.0, 200.0)) < 0.001,
				"citadel at fortress center"
			)
	_check(citadel_count == 1, "exactly one citadel piece structure")
	# Side pad at 0,62 * 0.1 = 0,6.2
	var found_side := false
	for pad_value in pads:
		var pad: Dictionary = pad_value
		if String(pad.get("pad_kind", "")) == "side":
			var pos: Vector2 = pad.get("position", Vector2.ZERO)
			if absf(pos.y - 206.2) < 0.01 or absf(pos.y - 193.8) < 0.01:
				found_side = true
	_check(found_side, "side pad uses scaled BSE offset (62*0.1)")


# --- Castle-footprint melee approach ----------------------------------------
# A fortress's CastleBehavior pieces are authored INSIDE the fortress
# footprint: MenFortressCitadel sits on the fortress origin (offset_source
# Vector2.ZERO, see the BSE fixture above) and every expansion pad within 64
# source units of it. At the retail map transform that is a citadel exactly on
# the fortress centre and pads 1.64-2.40 sim units out (measured live,
# .private/scratch/opus24-probe1.out.log). Movement deflection gives every
# completed structure a blocking disc, so treating those pieces as independent
# obstacles rings the fortress with a ~5.2-unit wall - wider than any melee
# AttackRange even after round 20 made the range gate surface-to-surface (a
# fortress subtracts its authored 1.9604 footprint, not the 4.6 movement ring;
# see _test_structure_surface_range and retail_slice_sim.gd
# _target_footprint_radius). Melee could therefore never hit a fortress or one
# of its pads; only ranged units ever landed a blow.
#
# Retail's Fords of Isen II map transform factor: 0.02649232738129
# (retail_map_data runtime).
const RETAIL_MAP_TRANSFORM_SCALE := 0.02649232738129
# retail_slice_sim.gd STRUCTURE_BLOCK_RADIUS default for an unlisted
# structure_kind (castle_piece).
const DEFAULT_STRUCTURE_BLOCK_RADIUS := 2.8


func _add_structure(sim: Object, structure_id: int, team: int, kind: String, source_object_id: String, at: Vector2, health: int) -> void:
	sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": kind,
		"name": source_object_id,
		"source_object_id": source_object_id,
		"position": at,
		"health": health,
		"maximum_health": health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
	}


## Re-derive every source-scaled unit-rule value at the fixture's CURRENT map
## transform. Each of these fields is `<field>_source * source_map_transform_scale`
## by construction in the live compiler, so swapping the transform without
## re-deriving them leaves the fixture internally inconsistent.
##
## IT WAS INCONSISTENT, and the inconsistency was load-bearing. _make_sim builds
## its rules at scale 0.1 (speed 5.5 = 55.0 source * 0.1);
## _make_castle_pathing_sim then swapped the transform to the retail
## 0.02649232738129 for the castle GEOMETRY but left the unit rules alone, so
## every castle-pathing test ran units at 0.55 sim units per tick against
## retail-scale structures — 3.8x the retail-correct 0.1457. Round 19 then cited
## that 0.55 as the measured melee step when deriving the transit push budget in
## _deflect_around_structures, which made the derivation an artifact of the
## fixture rather than a fact about the game.
const SCALED_UNIT_RULE_FIELDS := [
	"speed", "acceleration", "braking", "attack_range", "minimum_attack_range", "vision_range"
]


func _rescale_unit_rules(sim: Object, scale: float) -> void:
	var unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	for horde_id in unit_rules.keys():
		var rule: Dictionary = unit_rules[horde_id] as Dictionary
		for field in SCALED_UNIT_RULE_FIELDS:
			var source_key: String = "%s_source" % field
			if not rule.has(source_key):
				continue
			rule[field] = float(rule[source_key]) * scale


func _make_castle_pathing_sim() -> Object:
	var sim: Object = _make_sim()
	# Retail scale so the seeded pieces land where the live slice puts them —
	# and so do the units, see _rescale_unit_rules.
	sim._rules["source_map_transform_scale"] = RETAIL_MAP_TRANSFORM_SCALE
	_rescale_unit_rules(sim, RETAIL_MAP_TRANSFORM_SCALE)
	sim.configure_castle_behaviors({
		"MenFortress": {
			"faction": "Men",
			"castle_template_token": "Fortress_Men",
			"pieces": [
				{"index": 0, "source_object_id": "MenFortressExpansionPadSide", "object_id": "MenFortressExpansionPadSide", "maximum_health": 15000, "offset_source": Vector2(0.0, 62.0), "angle_radians": 1.5708},
				{"index": 1, "source_object_id": "MenFortressExpansionPadSide", "object_id": "MenFortressExpansionPadSide", "maximum_health": 15000, "offset_source": Vector2(0.0, -62.0), "angle_radians": -1.5708},
				{"index": 2, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 15000, "offset_source": Vector2(-64.0, -64.0), "angle_radians": -2.356},
				{"index": 3, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 15000, "offset_source": Vector2(64.0, -64.0), "angle_radians": -0.785},
				{"index": 4, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 15000, "offset_source": Vector2(-64.0, 64.0), "angle_radians": 2.356},
				{"index": 5, "source_object_id": "MenFortressExpansionPadCorner", "object_id": "MenFortressExpansionPadCorner", "maximum_health": 15000, "offset_source": Vector2(64.0, 64.0), "angle_radians": 0.785},
				{"index": 6, "source_object_id": "MenFortressCitadel", "object_id": "MenFortressCitadel", "maximum_health": 7500, "offset_source": Vector2.ZERO, "angle_radians": 0.0},
			],
		}
	})
	# Victory resolution eliminates a team the moment it has no living
	# battalions, and a resolved match parks every unit in "victory" and clears
	# its route - which would end these approach runs before they start. Park a
	# hostile sentinel far outside vision (40 sim units) so the match stays live.
	_add_melee_horde(sim, 999, 1, Vector2(600.0, 600.0))
	return sim


## Spawn a melee horde whose AttackRange is retail's 11.5 source units at the
## retail transform (0.305 sim) - the same figure the live slice reports for a
## melee row (.private/scratch/opus24-probe1.out.log).
func _add_melee_horde(sim: Object, horde_id: int, team: int, at: Vector2) -> Dictionary:
	sim._next_dynamic_id[team] = horde_id + 100
	sim._add_battalion(
		horde_id,
		team,
		at,
		"Fighter Horde",
		"bfme2.object.gondor-fighter-horde",
		"bfme2.object.gondor-fighter-horde",
		0
	)
	var horde: Dictionary = sim.entities[horde_id]
	horde["attack_range"] = 11.5 * RETAIL_MAP_TRANSFORM_SCALE
	horde["attack_range_source"] = 11.5
	return horde


func _advance_until_state(sim: Object, horde_id: int, state: String, limit: int) -> int:
	for tick in range(limit):
		sim.winner = -1
		sim.tick()
		if String((sim.entities[horde_id] as Dictionary).get("state", "")) == state:
			return tick + 1
	return -1


func _test_castle_melee_approach() -> void:
	print("--- castle footprint melee approach ---")
	# Case 1: melee ordered onto the fortress itself.
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	sim._seed_expansion_pads_for(fortress_id)
	var pieces: Array = (sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", [])
	_check(pieces.size() == 7, "castle group has 7 pieces", "count=%d" % pieces.size())
	var attacker := _add_melee_horde(sim, 501, 0, fortress_at + Vector2(-20.0, 0.0))
	_check(sim.issue_attack([501] as Array[int], fortress_id) == 1, "melee accepts fortress attack order")
	var reached := _advance_until_state(sim, 501, "attack", 600)
	_check(
		reached > 0,
		"melee reaches attack state against padded fortress",
		"state=%s distance=%.3f" % [
			String(attacker.get("state", "")),
			Vector2(attacker.get("position", Vector2.ZERO)).distance_to(fortress_at),
		]
	)

	# Case 2: melee ordered onto one of the fortress's own expansion pads.
	var pad_sim: Object = _make_castle_pathing_sim()
	_add_structure(pad_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	pad_sim._seed_expansion_pads_for(fortress_id)
	var pad_id := int((pad_sim.expansion_pad_states(fortress_id)[0] as Dictionary).get("castle_piece_structure_id", 0))
	var pad_position := Vector2((pad_sim.structures[pad_id] as Dictionary).get("position", Vector2.ZERO))
	var pad_attacker := _add_melee_horde(pad_sim, 502, 0, fortress_at + Vector2(-20.0, 0.0))
	_check(pad_sim.issue_attack([502] as Array[int], pad_id) == 1, "melee accepts pad attack order")
	var pad_reached := _advance_until_state(pad_sim, 502, "attack", 600)
	_check(
		pad_reached > 0,
		"melee reaches attack state against a castle expansion pad",
		"state=%s distance=%.3f" % [
			String(pad_attacker.get("state", "")),
			Vector2(pad_attacker.get("position", Vector2.ZERO)).distance_to(pad_position),
		]
	)

	# Case 3: a structure OUTSIDE the target's castle group still deflects.
	# The barracks sits EXACTLY on the straight line from the spawn to the
	# fortress, so a unit that ignored non-group footprints would clip through
	# it.
	#
	# ON-AXIS, DELIBERATELY (round 19). Round 18 offset this barracks 1.5 to
	# dodge a deadlock it had diagnosed but not fixed: with a purely radial push,
	# a centre exactly on the line of travel makes the push exactly anti-parallel
	# to the step, so the unit is shoved back onto the same point every tick and
	# parks on the ring forever. _tangential_slide_point closes that, so the
	# harder case is now the one under test — see
	# _test_structure_deflection_tangential_slide for the isolated proof and the
	# failing-first evidence.
	var gap_sim: Object = _make_castle_pathing_sim()
	_add_structure(gap_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	gap_sim._seed_expansion_pads_for(fortress_id)
	var barracks_id := 60
	var barracks_at := fortress_at + Vector2(-10.0, 0.0)
	_add_structure(gap_sim, barracks_id, 1, "barracks", "MenBarracks", barracks_at, 2000)
	var gap_attacker := _add_melee_horde(gap_sim, 503, 0, fortress_at + Vector2(-20.0, 0.0))
	gap_sim.issue_attack([503] as Array[int], fortress_id)
	var min_barracks_gap := 1e9
	for _tick in range(600):
		gap_sim.winner = -1
		gap_sim.tick()
		min_barracks_gap = minf(
			min_barracks_gap,
			Vector2(gap_attacker.get("position", Vector2.ZERO)).distance_to(barracks_at)
		)
	_check(
		min_barracks_gap >= 2.8 - 0.001,
		"non-group structure still deflects an attacker in transit",
		"closest=%.3f" % min_barracks_gap
	)
	_check(
		String(gap_attacker.get("state", "")) == "attack",
		"attacker still arrives past the deflecting barracks",
		"state=%s pos=%s barracks_gap=%.3f fortress_gap=%.3f route=%d" % [
			String(gap_attacker.get("state", "")),
			str(Vector2(gap_attacker.get("position", Vector2.ZERO))),
			Vector2(gap_attacker.get("position", Vector2.ZERO)).distance_to(barracks_at),
			Vector2(gap_attacker.get("position", Vector2.ZERO)).distance_to(fortress_at),
			(gap_attacker.get("route", []) as Array).size(),
		]
	)

	# Case 4: no attack order -> the castle footprint blocks exactly as before,
	# including for the fortress's own team.
	var move_sim: Object = _make_castle_pathing_sim()
	_add_structure(move_sim, fortress_id, 0, "fortress", "MenFortress", fortress_at, 7500)
	move_sim._seed_expansion_pads_for(fortress_id)
	var walker := _add_melee_horde(move_sim, 504, 0, fortress_at + Vector2(-20.0, 0.0))
	move_sim.issue_move([504] as Array[int], fortress_at)
	var min_piece_gap := 1e9
	for _tick in range(600):
		move_sim.winner = -1
		move_sim.tick()
		for piece_value in (move_sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
			var piece: Dictionary = move_sim.structures[int(piece_value)]
			min_piece_gap = minf(
				min_piece_gap,
				Vector2(walker.get("position", Vector2.ZERO)).distance_to(Vector2(piece.get("position", Vector2.ZERO)))
			)
	_check(
		min_piece_gap >= DEFAULT_STRUCTURE_BLOCK_RADIUS - 0.001,
		"friendly castle footprint keeps blocking a plain move order",
		"closest=%.3f" % min_piece_gap
	)


func _pass_through_for(sim: Object, at: Vector2, target_id: int, target_kind: String = "structure") -> Dictionary:
	return sim._castle_footprint_pass_through(at, target_id, target_kind)


func _corner_pad_ids(sim: Object, fortress_id: int) -> Dictionary:
	## The EAST and WEST corner pieces of the seeded MenFortress, resolved by
	## measured position rather than by index order.
	var fortress_at := Vector2((sim.structures[fortress_id] as Dictionary).get("position", Vector2.ZERO))
	var east: Array[int] = []
	var west: Array[int] = []
	for piece_value in (sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
		var piece_id := int(piece_value)
		var offset: Vector2 = Vector2((sim.structures[piece_id] as Dictionary).get("position", Vector2.ZERO)) - fortress_at
		if absf(offset.y) < 0.5:
			# The citadel and the two side pads sit on the castle's own x=0 axis.
			continue
		if offset.x > 1.0:
			east.append(piece_id)
		elif offset.x < -1.0:
			west.append(piece_id)
	return {"east": east, "west": west}


func _test_castle_corridor_is_bounded() -> void:
	## Round 18 replaces round 17's WHOLE-GROUP immunity. An attack order onto one
	## castle member must open only the members the walking line actually crosses,
	## and must close behind the unit.
	##
	## Numbers are measured off the seeded MenFortress, which reproduces the live
	## slice geometry in .private/scratch/opus24-probe1.out.log: fortress radius
	## 4.6 on the origin, citadel radius 2.8 exactly on it, side pads 1.643 out,
	## corner pads 2.398 out, every piece radius 2.8, so the pass threshold is
	## 2.8 + CASTLE_CORRIDOR_MARGIN = 3.15 and the two decisive spacings are
	## 2.398 (passes) against 3.391 (does not; round 18 reported 3.392 for BOTH
	## west pads — they are not equidistant and the second one is 4.796. Both are
	## asserted below to 0.001 instead of being quoted).
	print("--- castle corridor is bounded ---")
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	sim._seed_expansion_pads_for(fortress_id)
	var corners: Dictionary = _corner_pad_ids(sim, fortress_id)
	var east_ids: Array = corners["east"]
	var west_ids: Array = corners["west"]
	_check(east_ids.size() == 2 and west_ids.size() == 2,
		"castle fixture exposes two east and two west corner pieces",
		"east=%s west=%s" % [east_ids, west_ids])

	# Approaching the fortress CENTRE from outside: every piece overlaps the
	# target, so the whole group is on the line and opens. That is the bound not
	# BINDING, not the bound being absent - the next case proves the difference.
	var from_west := fortress_at + Vector2(-20.0, 0.0)
	var open_centre: Dictionary = _pass_through_for(sim, from_west, fortress_id)
	_check(open_centre.size() == 8,
		"attacking the fortress centre opens the pieces the line crosses",
		"open=%d ids=%s" % [open_centre.size(), open_centre.keys()])

	# THE REGRESSION ROUND 17 SHIPPED: attacking an EAST corner piece from the
	# east must NOT open the WEST pieces. They sit 3.391 and 4.796 off the
	# walking line, above the 3.15 threshold. Under whole-group immunity they
	# opened too.
	var east_target := int(east_ids[0])
	var east_at := Vector2((sim.structures[east_target] as Dictionary).get("position", Vector2.ZERO))
	var from_east := east_at + (east_at - fortress_at).normalized() * 3.0
	# MEASURE the geometry the bound is derived from, rather than quoting it.
	var west_offsets: Array[float] = []
	for west_value in west_ids:
		west_offsets.append(sim._point_segment_distance(
			Vector2((sim.structures[int(west_value)] as Dictionary).get("position", Vector2.ZERO)),
			from_east, east_at))
	west_offsets.sort()
	_check(
		west_offsets.size() == 2
			and absf(west_offsets[0] - 3.391) <= 0.001
			and absf(west_offsets[1] - 4.796) <= 0.001,
		"the west pads' measured offsets are 3.391 and 4.796, not one shared 3.392",
		"offsets=%s" % [west_offsets])
	_check(
		absf(sim._point_segment_distance(fortress_at, from_west, fortress_at) - 0.0) <= 0.001,
		"the corridor test measures distance to the SEGMENT, not the infinite line",
		"d=%.4f" % sim._point_segment_distance(fortress_at, from_west, fortress_at))
	var open_far: Dictionary = _pass_through_for(sim, from_east, east_target)
	var west_still_blocked := true
	for west_value in west_ids:
		if open_far.has(int(west_value)):
			west_still_blocked = false
	_check(west_still_blocked,
		"far-side attack does NOT open the near wall on the opposite side",
		"open=%s west=%s" % [open_far.keys(), west_ids])
	_check(open_far.has(east_target),
		"the ordered target is always open", "open=%s" % [open_far.keys()])
	_check(open_far.size() < 8,
		"the corridor is strictly smaller than the whole castle group",
		"open=%d of 8" % open_far.size())

	# Crossing an UNRELATED structure: a unit attacking something that is not part
	# of the castle gets no castle exemption at all.
	var outsider_id := 70
	_add_structure(sim, outsider_id, 1, "barracks", "MenBarracks", fortress_at + Vector2(12.0, 0.0), 2000)
	var open_outsider: Dictionary = _pass_through_for(sim, from_west, outsider_id)
	_check(open_outsider.size() == 1 and open_outsider.has(outsider_id),
		"attacking a non-castle structure opens only that structure",
		"open=%s" % [open_outsider.keys()])

	# ID-ALIAS GUARD: battalion ids and structure ids share a counter space but
	# live in different tables, so a BATTALION target whose id collides with a
	# structure id must not open that structure.
	var open_alias: Dictionary = _pass_through_for(sim, from_west, fortress_id, "battalion")
	_check(open_alias.is_empty(),
		"a battalion target never opens a structure with the same id",
		"open=%s" % [open_alias.keys()])
	# ... and a plain move order (no target) is still the byte-identical no-op.
	_check(_pass_through_for(sim, from_west, 0, "battalion").is_empty(),
		"a plain move order opens nothing")


func _distance_to_nearest_piece(sim: Object, fortress_id: int, at: Vector2) -> float:
	var nearest := Vector2(
		(sim.structures[fortress_id] as Dictionary).get("position", Vector2.ZERO)
	).distance_to(at)
	for piece_value in (sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
		nearest = minf(nearest, Vector2(
			(sim.structures[int(piece_value)] as Dictionary).get("position", Vector2.ZERO)
		).distance_to(at))
	return nearest


func _test_castle_pathing_lifecycle() -> void:
	## The exemption ends with the ORDER, so every way an order can end has to
	## leave the unit outside the footprint again - and it has to get there by
	## WALKING. The old deflection SNAPPED a unit from a fortress centre to the
	## ring in one tick (4.6 sim units in 1/30 s) and skipped a unit standing
	## exactly on the centre entirely, so that one stayed clipped forever.
	print("--- castle pathing lifecycle ---")
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	var step: float = SimScript.STRUCTURE_EVICTION_STEP
	var fortress_radius: float = SimScript.STRUCTURE_BLOCK_RADIUS["fortress"]

	# --- eviction is a walk, not a teleport, and d == 0 cannot persist --------
	# The fortress here is FRIENDLY (team 0) on purpose. An idle melee sitting
	# inside an ENEMY castle auto-acquires it, which reopens the corridor
	# legitimately - that interaction is asserted separately at the end of this
	# test. This case isolates the eviction itself.
	var sim: Object = _make_castle_pathing_sim()
	_add_structure(sim, fortress_id, 0, "fortress", "MenFortress", fortress_at, 7500)
	var clipped := _add_melee_horde(sim, 601, 0, fortress_at)
	clipped["position"] = fortress_at
	clipped["route"] = []
	clipped["target_id"] = 0
	clipped["target_kind"] = "battalion"
	sim._spatial_sync(clipped)
	var biggest_jump := 0.0
	var ticks_to_clear := -1
	for tick in range(400):
		sim.winner = -1
		var before := Vector2(clipped.get("position", Vector2.ZERO))
		sim.tick()
		var after := Vector2(clipped.get("position", Vector2.ZERO))
		biggest_jump = maxf(biggest_jump, before.distance_to(after))
		if ticks_to_clear < 0 and after.distance_to(fortress_at) >= fortress_radius - 0.001:
			ticks_to_clear = tick + 1
	_check(ticks_to_clear > 0,
		"a unit standing exactly on a footprint centre is evicted",
		"pos=%s d=%.3f" % [
			str(Vector2(clipped.get("position", Vector2.ZERO))),
			Vector2(clipped.get("position", Vector2.ZERO)).distance_to(fortress_at)])
	_check(ticks_to_clear > 1,
		"eviction takes more than one tick (it is a walk, not a teleport)",
		"ticks=%d" % ticks_to_clear)
	_check(biggest_jump <= step + 0.001,
		"no eviction step exceeds STRUCTURE_EVICTION_STEP",
		"biggest=%.4f step=%.4f" % [biggest_jump, step])

	# --- stop: the order ends, the footprint reasserts itself ----------------
	var stop_sim: Object = _make_castle_pathing_sim()
	_add_structure(stop_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	stop_sim._seed_expansion_pads_for(fortress_id)
	var stopper := _add_melee_horde(stop_sim, 602, 0, fortress_at + Vector2(-20.0, 0.0))
	stop_sim.issue_attack([602] as Array[int], fortress_id)
	_advance_until_state(stop_sim, 602, "attack", 600)
	var inside_on_stop: float = Vector2(stopper.get("position", Vector2.ZERO)).distance_to(fortress_at)
	stop_sim.issue_stop([602] as Array[int])
	# Stop cancels the ORDER; it does not make the battalion pacifist. Without
	# this the horde re-acquires the same fortress on the very next mood scan and
	# keeps its corridor, which is correct behaviour but tests the wrong thing.
	stopper["auto_acquire_enabled"] = false
	for _tick in range(400):
		stop_sim.winner = -1
		stop_sim.tick()
	_check(inside_on_stop < fortress_radius,
		"the attacker really was inside the footprint before stopping",
		"d=%.3f" % inside_on_stop)
	_check(
		Vector2(stopper.get("position", Vector2.ZERO)).distance_to(fortress_at) >= fortress_radius - 0.001,
		"stop evicts an attacker left inside the castle footprint",
		"d=%.3f" % Vector2(stopper.get("position", Vector2.ZERO)).distance_to(fortress_at))

	# --- target death: same, with no order to cancel -------------------------
	var death_sim: Object = _make_castle_pathing_sim()
	_add_structure(death_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	death_sim._seed_expansion_pads_for(fortress_id)
	var killer := _add_melee_horde(death_sim, 603, 0, fortress_at + Vector2(-20.0, 0.0))
	death_sim.issue_attack([603] as Array[int], fortress_id)
	_advance_until_state(death_sim, 603, "attack", 600)
	(death_sim.structures[fortress_id] as Dictionary)["health"] = 0
	killer["auto_acquire_enabled"] = false  # same reason as the stop case above
	for _tick in range(400):
		death_sim.winner = -1
		death_sim.tick()
	var nearest_after_death := _distance_to_nearest_piece(
		death_sim, fortress_id, Vector2(killer.get("position", Vector2.ZERO)))
	_check(nearest_after_death >= DEFAULT_STRUCTURE_BLOCK_RADIUS - 0.001,
		"a dead target still evicts the attacker from the surviving pieces",
		"nearest=%.3f pos=%s" % [nearest_after_death, str(Vector2(killer.get("position", Vector2.ZERO)))])

	# --- retarget: the old castle corridor closes behind the unit ------------
	var retarget_sim: Object = _make_castle_pathing_sim()
	_add_structure(retarget_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	retarget_sim._seed_expansion_pads_for(fortress_id)
	var retargeter := _add_melee_horde(retarget_sim, 604, 0, fortress_at + Vector2(-20.0, 0.0))
	retarget_sim.issue_attack([604] as Array[int], fortress_id)
	_advance_until_state(retarget_sim, 604, "attack", 600)
	var far_enemy := _add_melee_horde(retarget_sim, 605, 1, fortress_at + Vector2(-60.0, 0.0))
	retarget_sim.issue_attack([604] as Array[int], 605)
	for _tick in range(600):
		retarget_sim.winner = -1
		retarget_sim.tick()
	_check(
		Vector2(retargeter.get("position", Vector2.ZERO)).distance_to(fortress_at) >= fortress_radius - 0.001,
		"a retargeted attacker leaves the castle footprint",
		"d=%.3f target=%d enemy=%s" % [
			Vector2(retargeter.get("position", Vector2.ZERO)).distance_to(fortress_at),
			int(retargeter.get("target_id", 0)),
			str(Vector2(far_enemy.get("position", Vector2.ZERO)))])

	# --- auto-acquire: an unordered melee that picks up a castle piece by
	#     itself gets exactly the same bounded corridor, not a wider one -------
	var auto_sim: Object = _make_castle_pathing_sim()
	_add_structure(auto_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	auto_sim._seed_expansion_pads_for(fortress_id)
	var auto_corners: Dictionary = _corner_pad_ids(auto_sim, fortress_id)
	var auto_target := int((auto_corners["east"] as Array)[0])
	var auto_at := Vector2((auto_sim.structures[auto_target] as Dictionary).get("position", Vector2.ZERO))
	var acquired_from := auto_at + (auto_at - fortress_at).normalized() * 3.0
	var acquirer := _add_melee_horde(auto_sim, 606, 0, acquired_from)
	# Auto-acquisition writes target_kind itself; drive it the way _step_attacks
	# does and then read the corridor it would get.
	acquirer["target_id"] = auto_target
	acquirer["target_kind"] = "structure"
	var acquired_open: Dictionary = _pass_through_for(auto_sim, acquired_from, auto_target, "structure")
	var auto_west: Array = auto_corners["west"]
	var auto_blocked := true
	for west_value in auto_west:
		if acquired_open.has(int(west_value)):
			auto_blocked = false
	_check(auto_blocked,
		"an auto-acquired castle piece gets the same bounded corridor",
		"open=%s west=%s" % [acquired_open.keys(), auto_west])

	# --- the interaction the three cases above deliberately suppressed --------
	# An IDLE melee sitting inside an ENEMY castle is not a stray unit: it
	# auto-acquires the castle and is attacking it, so the corridor stays open
	# and eviction must NOT fight the engagement. Asserted here so the
	# auto_acquire_enabled=false lines above read as isolation, not as a dodge.
	var engaged_sim: Object = _make_castle_pathing_sim()
	_add_structure(engaged_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	var squatter := _add_melee_horde(engaged_sim, 607, 0, fortress_at)
	squatter["position"] = fortress_at
	squatter["route"] = []
	engaged_sim._spatial_sync(squatter)
	for _tick in range(60):
		engaged_sim.winner = -1
		engaged_sim.tick()
	_check(
		int(squatter.get("target_id", 0)) == fortress_id
			and String(squatter.get("target_kind", "")) == "structure"
			and Vector2(squatter.get("position", Vector2.ZERO)).distance_to(fortress_at) < fortress_radius,
		"an idle melee inside an enemy castle acquires it and keeps its corridor",
		"target=%d kind=%s d=%.3f" % [
			int(squatter.get("target_id", 0)), String(squatter.get("target_kind", "")),
			Vector2(squatter.get("position", Vector2.ZERO)).distance_to(fortress_at)])


func _test_structure_deflection_tangential_slide() -> void:
	## THE ON-AXIS DEADLOCK. A purely radial deflection push is exactly
	## anti-parallel to the step whenever the blocking structure's centre sits on
	## the line of travel, so the unit steps forward and is shoved straight back
	## onto the same ring point, forever. Round 18 diagnosed this, dodged it by
	## offsetting the fixture barracks 1.5 units off the axis, and left it open.
	##
	## MEASURED deadlock, reproduced exactly by this fixture with the slide
	## removed (.private/scratch/opus26-banner-FAILFIRST-tangential.err.log):
	## an attacker at (80, 200) ordered onto a fortress at (100, 200) with a
	## barracks at (90, 200) parks at (87.2, 200.0) — precisely
	## barracks + (-2.8, 0) — with route length 1 still unconsumed after 600
	## ticks. _step_route's 3-tick stall escape never fires: the attack state
	## re-assigns the route every tick, which resets route_stall_ticks.
	print("--- tangential slide around an on-axis footprint ---")
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	var barracks_id := 61
	var barracks_at := fortress_at + Vector2(-10.0, 0.0)
	_add_structure(sim, barracks_id, 1, "barracks", "MenBarracks", barracks_at, 2000)
	var spawn_at := fortress_at + Vector2(-20.0, 0.0)
	var attacker := _add_melee_horde(sim, 701, 0, spawn_at)
	_check(
		absf((fortress_at - spawn_at).cross(barracks_at - spawn_at)) <= 0.000001,
		"the fixture really is ON-AXIS (spawn, barracks and target are collinear)",
		"cross=%.9f" % (fortress_at - spawn_at).cross(barracks_at - spawn_at))
	sim.issue_attack([701] as Array[int], fortress_id)
	var worst_clearance := 1e9
	var reached := -1
	for tick in range(600):
		sim.winner = -1
		sim.tick()
		var here := Vector2(attacker.get("position", Vector2.ZERO))
		worst_clearance = minf(worst_clearance, here.distance_to(barracks_at))
		if reached < 0 and String(attacker.get("state", "")) == "attack":
			reached = tick + 1
	_check(reached > 0,
		"an attacker walks AROUND a footprint centred on its line of travel",
		"state=%s pos=%s route=%d" % [
			String(attacker.get("state", "")),
			str(Vector2(attacker.get("position", Vector2.ZERO))),
			(attacker.get("route", []) as Array).size()])
	_check(worst_clearance >= DEFAULT_STRUCTURE_BLOCK_RADIUS - 0.001,
		"the slide never trades the deadlock for a clip through the footprint",
		"worst=%.4f" % worst_clearance)
	_check(
		Vector2(attacker.get("position", Vector2.ZERO)).distance_to(fortress_at)
			< Vector2(attacker.get("position", Vector2.ZERO)).distance_to(barracks_at),
		"the attacker ends past the obstacle, not parked on its ring",
		"pos=%s barracks_gap=%.3f fortress_gap=%.3f" % [
			str(Vector2(attacker.get("position", Vector2.ZERO))),
			Vector2(attacker.get("position", Vector2.ZERO)).distance_to(barracks_at),
			Vector2(attacker.get("position", Vector2.ZERO)).distance_to(fortress_at)])

	# --- the side choice is a pure, deterministic function ---------------------
	# Lockstep peers must pick the same side from the same two vectors and from
	# nothing else. The EXACTLY-ZERO cross product is the deadlock case, so its
	# fallback is the one that matters most.
	var centre := Vector2(10.0, 10.0)
	var radius := 2.0
	var head_on: Vector2 = sim._tangential_slide_point(centre, radius, Vector2.LEFT, Vector2.RIGHT * 0.5)
	_check(
		absf(head_on.distance_to(centre) - radius) <= 0.000001,
		"a tangential slide stays exactly on the disc it is sliding around",
		"d=%.9f" % head_on.distance_to(centre))
	_check(
		head_on != centre + Vector2.LEFT * radius,
		"an exactly anti-parallel step still makes angular progress",
		"point=%s" % str(head_on))
	_check(
		sim._tangential_slide_point(centre, radius, Vector2.LEFT, Vector2.RIGHT * 0.5)
			== sim._tangential_slide_point(centre, radius, Vector2.LEFT, Vector2.RIGHT * 0.5),
		"the same two vectors always produce the same point")
	# Off-axis, the side follows the drift the unit already has, so the slide
	# never fights the approach: a step angled south of the inward radial keeps
	# going south.
	var drift_south: Vector2 = sim._tangential_slide_point(
		centre, radius, Vector2.LEFT, Vector2(1.0, -0.2).normalized() * 0.5)
	var drift_north: Vector2 = sim._tangential_slide_point(
		centre, radius, Vector2.LEFT, Vector2(1.0, 0.2).normalized() * 0.5)
	_check(drift_south.y < centre.y and drift_north.y > centre.y,
		"the side follows the step's own drift when it is not exactly on-axis",
		"south=%s north=%s" % [str(drift_south), str(drift_north)])


func _test_eviction_scope() -> void:
	## Eviction has to be narrow. Round 18 shipped it wide enough to fight three
	## things it was never meant to touch: the authored production doorway, a
	## live engagement, and itself across overlapping castle discs.
	print("--- eviction scope ---")
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	var step: float = SimScript.STRUCTURE_EVICTION_STEP

	# --- the production doorway is INSIDE the footprint, by contract -----------
	# QueueProductionExitUpdate creates a horde at
	# producer + PRODUCTION_DOOR_INSET_RADIUS (0.9) and walks it to
	# PRODUCTION_EXIT_RADIUS (4.25); a producer's block radius is 2.6-3.0, so the
	# authored create point is ~1.7-2.1 units inside its own disc.
	# _step_production_exit owns the position for the whole animation (an exact
	# smoothstep lerp, route empty, state "run"), so round 18's pass nudged EVERY
	# produced unit sideways out of its authored exit lane.
	var exit_sim: Object = _make_castle_pathing_sim()
	var barracks_id := 62
	var barracks_at := Vector2(100.0, 200.0)
	_add_structure(exit_sim, barracks_id, 0, "barracks", "MenBarracks", barracks_at, 2000)
	var exit_direction := Vector2.RIGHT
	var door_point: Vector2 = barracks_at + exit_direction * SimScript.PRODUCTION_DOOR_INSET_RADIUS
	var create_point: Vector2 = barracks_at + exit_direction * SimScript.PRODUCTION_EXIT_RADIUS
	var produced := _add_melee_horde(exit_sim, 702, 0, door_point)
	produced["position"] = door_point
	produced["route"] = []
	produced["facing"] = exit_direction
	produced["production_producer_id"] = barracks_id
	produced["production_exit_start_tick"] = int(exit_sim.tick_index)
	produced["production_exit_duration_ticks"] = SimScript.PRODUCTION_EXIT_DURATION_TICKS
	produced["production_exit_progress"] = 0.0
	produced["production_exit_origin"] = door_point
	produced["production_exit_destination"] = create_point
	produced["production_rally"] = create_point
	exit_sim._spatial_sync(produced)
	_check(
		door_point.distance_to(barracks_at) < DEFAULT_STRUCTURE_BLOCK_RADIUS,
		"the authored doorway really is inside the producer's own footprint",
		"door=%.3f radius=%.3f" % [door_point.distance_to(barracks_at), DEFAULT_STRUCTURE_BLOCK_RADIUS])
	var worst_lateral := 0.0
	var start_tick := int(exit_sim.tick_index)
	for _tick in range(SimScript.PRODUCTION_EXIT_DURATION_TICKS):
		exit_sim.winner = -1
		exit_sim.tick()
		var elapsed := maxi(0, int(exit_sim.tick_index) - start_tick)
		var progress := clampf(
			float(elapsed) / float(SimScript.PRODUCTION_EXIT_DURATION_TICKS), 0.0, 1.0)
		var authored := door_point.lerp(create_point, smoothstep(0.0, 1.0, progress))
		worst_lateral = maxf(
			worst_lateral,
			Vector2(produced.get("position", Vector2.ZERO)).distance_to(authored))
	_check(worst_lateral <= 0.000001,
		"a unit mid production-exit follows the authored doorway lerp exactly",
		"worst_deviation=%.6f pos=%s" % [
			worst_lateral, str(Vector2(produced.get("position", Vector2.ZERO)))])
	# ... and the exemption is bounded: once the animation ends the unit is
	# outside the footprint anyway, and eviction is live again.
	_check(int(produced.get("production_exit_start_tick", 0)) < 0,
		"the production-exit exemption ends with the animation",
		"start_tick=%d" % int(produced.get("production_exit_start_tick", 0)))

	# --- an engagement is not fought with ------------------------------------
	# The castle corridor only ever exempts STRUCTURE targets. A melee horde
	# fighting an enemy BATTALION that is standing on a footprint — defenders
	# backed against their own barracks — was pushed out of its own weapon range
	# every tick and had to walk back in.
	#
	# THE GEOMETRY THAT MAKES IT BITE. Radial eviction is not a rigid translation
	# of the pair: two units at the same radius but different bearings are pushed
	# apart, because the arc between two bearings grows with the radius. Seeded
	# at radius 2.0 on a 2.8 disc, 8 degrees apart, they stand
	# 2 * 2.0 * sin(4 deg) = 0.279 apart — inside the melee 0.3046 — and a single
	# eviction to the rim opens that to 2 * 2.8 * sin(4 deg) = 0.391, which is
	# OUTSIDE it. The engagement breaks and both have to walk back in.
	var engage_sim: Object = _make_castle_pathing_sim()
	var wall_id := 63
	var wall_at := Vector2(100.0, 200.0)
	_add_structure(engage_sim, wall_id, 1, "barracks", "MenBarracks", wall_at, 2000)
	var seed_radius := 2.0
	var bearing := deg_to_rad(8.0)
	var defender := _add_melee_horde(engage_sim, 704, 1, wall_at + Vector2(seed_radius, 0.0))
	var brawler := _add_melee_horde(
		engage_sim, 703, 0,
		wall_at + Vector2(cos(bearing), sin(bearing)) * seed_radius)
	for horde_value in [defender, brawler]:
		var horde: Dictionary = horde_value
		horde["position"] = Vector2(horde.get("position", Vector2.ZERO))
		horde["route"] = []
		engage_sim._spatial_sync(horde)
	var melee_range: float = float(brawler.get("attack_range", 0.0))
	var seeded_separation: float = Vector2(brawler.get("position", Vector2.ZERO)).distance_to(
		Vector2(defender.get("position", Vector2.ZERO)))
	_check(
		Vector2(brawler.get("position", Vector2.ZERO)).distance_to(wall_at)
			< DEFAULT_STRUCTURE_BLOCK_RADIUS
			and seeded_separation <= melee_range,
		"the brawler starts inside the footprint AND inside its own weapon range",
		"footprint_d=%.4f separation=%.4f range=%.4f" % [
			Vector2(brawler.get("position", Vector2.ZERO)).distance_to(wall_at),
			seeded_separation, melee_range])
	_check(
		2.0 * DEFAULT_STRUCTURE_BLOCK_RADIUS * sin(bearing * 0.5) > melee_range,
		"and evicting BOTH to the rim would break that range — the case under test",
		"rim_separation=%.4f range=%.4f" % [
			2.0 * DEFAULT_STRUCTURE_BLOCK_RADIUS * sin(bearing * 0.5), melee_range])
	engage_sim.issue_attack([703] as Array[int], 704)
	var in_range_ticks := 0
	var out_of_range_while_engaged := 0
	var worst_separation := 0.0
	for _tick in range(60):
		engage_sim.winner = -1
		engage_sim.tick()
		if int(defender.get("health", 0)) <= 0:
			break
		if int(brawler.get("target_id", 0)) != 704:
			continue
		in_range_ticks += 1
		var separation: float = Vector2(brawler.get("position", Vector2.ZERO)).distance_to(
			Vector2(defender.get("position", Vector2.ZERO)))
		worst_separation = maxf(worst_separation, separation)
		if separation > melee_range + 0.001:
			out_of_range_while_engaged += 1
	_check(in_range_ticks > 0,
		"the brawler does engage a battalion standing on a footprint",
		"engaged_ticks=%d state=%s" % [in_range_ticks, String(brawler.get("state", ""))])
	_check(out_of_range_while_engaged == 0,
		"eviction never pushes an engaged battalion out of its own weapon range",
		"violations=%d of %d engaged ticks worst_separation=%.4f range=%.4f" % [
			out_of_range_while_engaged, in_range_ticks, worst_separation, melee_range])

	# --- overlapping castle discs do not compound the push --------------------
	# Round 18 bounded each structure's push separately. A unit on a fortress
	# origin is inside the fortress, the citadel and up to two pads at once, so
	# four separate 0.35 pushes could compound into a 1.4 jump in one tick. The
	# lifecycle test only ever exercised a BARE fortress, which is exactly one
	# disc, so the compounding case was untested.
	var pile_sim: Object = _make_castle_pathing_sim()
	_add_structure(pile_sim, fortress_id, 0, "fortress", "MenFortress", fortress_at, 7500)
	pile_sim._seed_expansion_pads_for(fortress_id)
	var overlapping := 0
	for piece_value in (pile_sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
		if Vector2((pile_sim.structures[int(piece_value)] as Dictionary).get(
			"position", Vector2.ZERO)).distance_to(fortress_at) < DEFAULT_STRUCTURE_BLOCK_RADIUS:
			overlapping += 1
	_check(overlapping >= 3,
		"the fortress centre really is inside several castle discs at once",
		"overlapping_pieces=%d" % overlapping)
	var piled := _add_melee_horde(pile_sim, 705, 0, fortress_at)
	piled["position"] = fortress_at
	piled["route"] = []
	piled["target_id"] = 0
	piled["target_kind"] = "battalion"
	piled["auto_acquire_enabled"] = false
	pile_sim._spatial_sync(piled)
	var biggest := 0.0
	for _tick in range(600):
		pile_sim.winner = -1
		var before := Vector2(piled.get("position", Vector2.ZERO))
		pile_sim.tick()
		biggest = maxf(biggest, before.distance_to(Vector2(piled.get("position", Vector2.ZERO))))
	_check(biggest <= step + 0.001,
		"overlapping castle discs never compound into more than one step per tick",
		"biggest=%.4f step=%.4f overlapping=%d" % [biggest, step, overlapping])

	# --- several units evicting at once settle, and stay settled --------------
	# Eviction moves units, units push each other apart, and both feed back into
	# the next tick's eviction. Prove the loop CONVERGES rather than finding a
	# limit cycle: every unit ends clear of every disc, and nothing moves at all
	# over the final stretch.
	var crowd_sim: Object = _make_castle_pathing_sim()
	_add_structure(crowd_sim, fortress_id, 0, "fortress", "MenFortress", fortress_at, 7500)
	crowd_sim._seed_expansion_pads_for(fortress_id)
	var crowd: Array[int] = []
	var seeds: Array[Vector2] = [
		Vector2(0.0, 0.0), Vector2(0.4, 0.0), Vector2(-0.4, 0.2),
		Vector2(1.2, -1.2), Vector2(-1.6, -0.8), Vector2(0.0, 1.9),
	]
	for index in seeds.size():
		var member := _add_melee_horde(crowd_sim, 710 + index, 0, fortress_at + seeds[index])
		member["position"] = fortress_at + seeds[index]
		member["route"] = []
		member["target_id"] = 0
		member["target_kind"] = "battalion"
		member["auto_acquire_enabled"] = false
		crowd_sim._spatial_sync(member)
		crowd.append(710 + index)
	var settle_positions: Dictionary = {}
	var moved_after_settle := 0.0
	for tick in range(1200):
		crowd_sim.winner = -1
		crowd_sim.tick()
		if tick == 1099:
			for member_id in crowd:
				settle_positions[member_id] = Vector2(
					(crowd_sim.entities[member_id] as Dictionary).get("position", Vector2.ZERO))
		elif tick >= 1100:
			for member_id in crowd:
				moved_after_settle = maxf(moved_after_settle, Vector2(
					(crowd_sim.entities[member_id] as Dictionary).get("position", Vector2.ZERO)
				).distance_to(settle_positions[member_id]))
	var worst_penetration := 0.0
	var worst_member := 0
	for member_id in crowd:
		var here := Vector2((crowd_sim.entities[member_id] as Dictionary).get("position", Vector2.ZERO))
		var penetration: float = maxf(
			0.0, float(SimScript.STRUCTURE_BLOCK_RADIUS["fortress"]) - here.distance_to(fortress_at))
		for piece_value in (crowd_sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
			penetration = maxf(penetration, DEFAULT_STRUCTURE_BLOCK_RADIUS - here.distance_to(
				Vector2((crowd_sim.structures[int(piece_value)] as Dictionary).get("position", Vector2.ZERO))))
		if penetration > worst_penetration:
			worst_penetration = penetration
			worst_member = member_id
	_check(worst_penetration <= 0.001,
		"every unit in a seeded castle pile-up ends clear of every disc",
		"worst=%.4f on %d" % [worst_penetration, worst_member])
	_check(moved_after_settle <= 0.000001,
		"the multi-unit eviction settles instead of finding a limit cycle",
		"movement_over_last_100_ticks=%.6f" % moved_after_settle)


## MenFortress authored footprint, verbatim from the selected pack's compiled
## geometry (`data/playable-structures/menfortress.json`,
## `registration.gameplay.geometry.footprint.radius`), which is the union of the
## BOX primary (GeometryMajorRadius 64, fortress.ini:1254-1256) and its four
## AdditionalGeometry plot pieces at GeometryOffset 64/-64 with radius 10
## (fortress.ini:1259-1265): 64 + 10 = 74 source units.
const MENFORTRESS_FOOTPRINT_SOURCE_RADIUS := 74.0
## GondorBarracks, same document family (`gondorbarracks.json`).
const GONDORBARRACKS_FOOTPRINT_SOURCE_RADIUS := 50.0


func _add_wall_melee_horde(sim: Object, horde_id: int, team: int, at: Vector2) -> Dictionary:
	## _add_melee_horde writes retail's 11.5-source melee range onto the ROW, but
	## the first _step_attacks tick calls _apply_weapon_mode, which copies
	## attack_range back out of `weapon_modes` — and the fixture unit rule authors
	## 1.15 there. MEASURED on the failing-first run: an attacker ordered onto the
	## fortress settled at 1.110 from its centre, i.e. it engaged at 1.15, not at
	## the 0.305 the helper's comment claims
	## (.private/scratch/opus28-banner_castle_sim_runner-FAILFIRST.err.log).
	##
	## Patch the MODE as well so the range gate actually sees retail's melee
	## reach. The existing 71 checks in this file deliberately keep the un-patched
	## helper: re-deriving their authored expectations is a separate change.
	var horde := _add_melee_horde(sim, horde_id, team, at)
	var modes: Dictionary = horde.get("weapon_modes", {}) as Dictionary
	for mode_key in modes.keys():
		var mode: Dictionary = modes[mode_key] as Dictionary
		mode["attack_range"] = 11.5 * RETAIL_MAP_TRANSFORM_SCALE
		mode["attack_range_source"] = 11.5
	return horde


func _test_structure_surface_range() -> void:
	## SURFACE-TO-SURFACE RANGE AGAINST A STRUCTURE.
	##
	## The sim tested weapon range CENTRE-TO-CENTRE, copied from a partial
	## open-source reimplementation that compares raw translation distance
	## against AttackRange. The original engine instead measures the GAP between
	## the two objects' bounding circles.
	##
	## The consequence was measured live: a melee horde ordered onto the enemy
	## fortress ended in state `attack` at d=0.24 and then d=0.00 — INSIDE the
	## fortress, on its centre — because a 0.305 AttackRange against a
	## 1.96-radius footprint is only satisfiable at the centre. Melee is supposed
	## to stand at the wall and swing.
	##
	## The four checks below bracket the new boundary from both sides so the fix
	## cannot be "add a big number":
	##   wall-stand              — the attacker stops AT the footprint, never inside
	##   in-range-at-the-edge    — standing on the wall is already in range
	##   in-range-just-inside-the-ceiling — radius + AttackRange - eps engages
	##   out-of-range-beyond-it  — radius + AttackRange + 0.5 does NOT
	## The last one is the guard against expanding by the MOVEMENT block radius
	## (fortress 4.6) instead of the authored footprint (1.9604): 4.6 + 0.305
	## would swallow it.
	print("--- surface-to-surface range against structures ---")
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	var footprint: float = MENFORTRESS_FOOTPRINT_SOURCE_RADIUS * RETAIL_MAP_TRANSFORM_SCALE
	var melee_range: float = 11.5 * RETAIL_MAP_TRANSFORM_SCALE

	# --- wall-stand ----------------------------------------------------------
	var sim: Object = _make_castle_pathing_sim()
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	sim._seed_expansion_pads_for(fortress_id)
	(sim.structures[fortress_id] as Dictionary)["footprint_radius_source"] = (
		MENFORTRESS_FOOTPRINT_SOURCE_RADIUS
	)
	var attacker := _add_wall_melee_horde(sim, 801, 0, fortress_at + Vector2(-20.0, 0.0))
	sim.issue_attack([801] as Array[int], fortress_id)
	var reached := _advance_until_state(sim, 801, "attack", 600)
	var closest := 1e9
	for _tick in range(300):
		sim.winner = -1
		sim.tick()
		closest = minf(
			closest, Vector2(attacker.get("position", Vector2.ZERO)).distance_to(fortress_at)
		)
	_check(
		reached > 0,
		"melee still reaches attack against the fortress",
		"state=%s" % String(attacker.get("state", ""))
	)
	_check(
		closest >= footprint - 0.001,
		"melee stands AT the fortress footprint instead of walking to its centre",
		"closest=%.4f footprint=%.4f" % [closest, footprint]
	)
	_check(
		closest <= footprint + melee_range + 0.001,
		"and it is close enough to actually be swinging",
		"closest=%.4f ceiling=%.4f" % [closest, footprint + melee_range]
	)

	# --- the boundary, measured from a standing start ------------------------
	# auto-acquire off and a plain attack order: whether the unit engages is then
	# purely the range predicate, not the approach.
	var edge_ticks := _engage_ticks_at(fortress_at + Vector2(-(footprint), 0.0), fortress_at)
	_check(
		edge_ticks > 0,
		"a unit standing exactly on the footprint edge is in range",
		"ticks=%d" % edge_ticks
	)
	var inside_ceiling := _engage_ticks_at(
		fortress_at + Vector2(-(footprint + melee_range - 0.01), 0.0), fortress_at
	)
	_check(
		inside_ceiling > 0,
		"a unit just inside footprint+AttackRange is in range",
		"ticks=%d d=%.4f ceiling=%.4f" % [
			inside_ceiling, footprint + melee_range - 0.01, footprint + melee_range
		]
	)
	var beyond := _engage_ticks_at(
		fortress_at + Vector2(-(footprint + melee_range + 0.5), 0.0), fortress_at
	)
	_check(
		beyond < 0,
		"a unit beyond footprint+AttackRange is NOT in range (block radius 4.6 would swallow this)",
		"ticks=%d d=%.4f ceiling=%.4f" % [
			beyond, footprint + melee_range + 0.5, footprint + melee_range
		]
	)

	# --- a plain (non-castle) structure gets the same treatment --------------
	# The castle group is the LOUD case, but the semantics are per-object: a
	# barracks is a single structure with its own authored 50-source footprint.
	var barracks_sim: Object = _make_castle_pathing_sim()
	var barracks_at := Vector2(100.0, 200.0)
	var barracks_footprint: float = (
		GONDORBARRACKS_FOOTPRINT_SOURCE_RADIUS * RETAIL_MAP_TRANSFORM_SCALE
	)
	_add_structure(barracks_sim, 70, 1, "barracks", "GondorBarracks", barracks_at, 2000)
	(barracks_sim.structures[70] as Dictionary)["footprint_radius_source"] = (
		GONDORBARRACKS_FOOTPRINT_SOURCE_RADIUS
	)
	var barracks_attacker := _add_wall_melee_horde(barracks_sim, 805, 0, barracks_at + Vector2(-8.0, 0.0))
	barracks_sim.issue_attack([805] as Array[int], 70)
	var barracks_reached := _advance_until_state(barracks_sim, 805, "attack", 600)
	var barracks_closest := 1e9
	for _tick in range(300):
		barracks_sim.winner = -1
		barracks_sim.tick()
		barracks_closest = minf(
			barracks_closest,
			Vector2(barracks_attacker.get("position", Vector2.ZERO)).distance_to(barracks_at)
		)
	_check(
		barracks_reached > 0 and barracks_closest >= barracks_footprint - 0.001,
		"melee stands at a plain structure's footprint too",
		"reached=%d closest=%.4f footprint=%.4f" % [
			barracks_reached, barracks_closest, barracks_footprint
		]
	)

	# --- the ContentDB path, and the fallback under it -----------------------
	# With no explicit row value the resolver must read the SELECTED PACK. This
	# runner boots autoloads, so ContentDB is live and `MenFortress` resolves to
	# the compiled 74.0 (asserted first — otherwise the fallback below could be
	# passing for the wrong reason).
	var db_sim: Object = _make_castle_pathing_sim()
	_add_structure(db_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	var db_measured := -1.0
	if db_sim.has_method("_structure_footprint_radius"):
		db_measured = float(db_sim._structure_footprint_radius(
			db_sim.structures[fortress_id] as Dictionary
		))
	_check(
		absf(db_measured - footprint) <= 0.0001,
		"an unannotated fortress row resolves 74.0 from the selected pack's compiled geometry",
		"got=%.4f expected=%.4f" % [db_measured, footprint]
	)

	# A pack published before the geometry projection landed, or an object the
	# pack does not carry, must not silently collapse back to centre-to-centre.
	# The resolver falls back to RetailSelectionPick's published source-unit
	# defaults (fortress 64.0 = the authored GeometryMajorRadius, else 50.0).
	var bare_sim: Object = _make_castle_pathing_sim()
	_add_structure(
		bare_sim, fortress_id, 1, "fortress", "ObjectAbsentFromEveryPack", fortress_at, 7500
	)
	var bare_expected: float = 64.0 * RETAIL_MAP_TRANSFORM_SCALE
	var bare_measured := -1.0
	if bare_sim.has_method("_structure_footprint_radius"):
		bare_measured = float(bare_sim._structure_footprint_radius(
			bare_sim.structures[fortress_id] as Dictionary
		))
	_check(
		absf(bare_measured - bare_expected) <= 0.0001,
		"a structure with no compiled geometry falls back to the authored 64.0 fortress radius",
		"got=%.4f expected=%.4f" % [bare_measured, bare_expected]
	)


func _engage_ticks_at(at: Vector2, fortress_at: Vector2) -> int:
	## Park a melee horde at `at`, order it onto a fortress seeded at
	## `fortress_at`, and return the tick it entered `attack` on — or -1 if it
	## never did within 3 ticks. Three ticks is deliberately short: a unit that
	## has to WALK cannot arrive inside it (retail melee steps 0.1457 sim units
	## per tick - 55 source units/second at 0.02649232738129 and TICK_SECONDS
	## 0.1, and the fixture is rescaled to match, see _rescale_unit_rules), so
	## this measures the range predicate and nothing else. The earlier "~0.55"
	## here was the pre-rescale fixture artifact.
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	(sim.structures[fortress_id] as Dictionary)["footprint_radius_source"] = (
		MENFORTRESS_FOOTPRINT_SOURCE_RADIUS
	)
	var horde := _add_wall_melee_horde(sim, 810, 0, at)
	horde["auto_acquire_enabled"] = false
	sim.issue_attack([810] as Array[int], fortress_id)
	for tick in range(3):
		sim.winner = -1
		sim.tick()
		if String(horde.get("state", "")) == "attack":
			return tick + 1
	return -1


func _test_tangential_slide_near_zero_band() -> void:
	## THE NEAR-ZERO BAND. _tangential_slide_point picks its side from the sign of
	## `radial_direction.cross(travel_step)`. Snapping only the EXACTLY-zero cross
	## left a band around the axis where the value is nonzero but its sign is the
	## least trustworthy bit in the computation: `x1*y2 - y1*x2` is a difference of
	## two nearly equal products, which is precisely the expression a fused
	## multiply-add contracts differently on different microarchitectures.
	##
	## WHY NO RUNNER CAN CATCH THE REAL FAILURE. Both peers in
	## retail_lockstep_determinism_runner and retail_lockstep_network_runner are
	## the SAME binary on the SAME machine, so they contract identically and always
	## agree. A heterogeneous match is the only thing that exposes it, and there is
	## no such thing in this test tree. So the property is asserted structurally
	## instead: inside the band the side must not depend on the cross product at
	## all, which makes the FMA-sensitive bit unreachable.
	print("--- tangential slide near-zero band ---")
	var sim: Object = _make_castle_pathing_sim()
	var centre := Vector2(10.0, 10.0)
	var radius := 2.0
	var radial := Vector2.LEFT
	var fixed_side: Vector2 = sim._tangential_slide_point(centre, radius, radial, Vector2.RIGHT * 0.5)
	# Perturbations either side of the axis, all with |cross| under the 1e-6
	# epsilon this file already uses for a degenerate vector. Signed both ways so
	# a sign-following implementation must disagree with itself.
	for perturbation in [1e-9, -1e-9, 1e-8, -1e-8, 5e-7, -5e-7]:
		var step := Vector2(0.5, float(perturbation))
		var cross: float = radial.cross(step)
		_check(
			absf(cross) <= 0.000001,
			"the probe step is inside the near-zero band (perturbation %.9f)" % perturbation,
			"cross=%.12f" % cross
		)
		_check(
			sim._tangential_slide_point(centre, radius, radial, step).is_equal_approx(fixed_side),
			"a near-axis step takes the FIXED side, not a cancelling cross sign (%.9f)" % perturbation,
			"got=%s fixed=%s cross=%.12f" % [
				str(sim._tangential_slide_point(centre, radius, radial, step)), str(fixed_side), cross
			]
		)
	# And the band is a band, not a new default: a step whose cross is clearly
	# outside it still follows its own drift.
	var outside_south: Vector2 = sim._tangential_slide_point(
		centre, radius, radial, Vector2(0.5, -0.001)
	)
	var outside_north: Vector2 = sim._tangential_slide_point(
		centre, radius, radial, Vector2(0.5, 0.001)
	)
	_check(
		outside_south.y < centre.y and outside_north.y > centre.y,
		"outside the band the side still follows the step's own drift",
		"south=%s north=%s" % [str(outside_south), str(outside_north)]
	)


func _test_moving_unit_tangential_compounding() -> void:
	## THE MOVING CASE, which the existing budget test never exercised: it seeds
	## `route = []`, so travel_step is zero and only the radial eviction path runs.
	## A unit that is actually WALKING re-seats tangentially, and the question this
	## answers is whether those re-seats compound across N overlapping discs in one
	## tick - a castle is exactly that (citadel on the fortress origin, six pads
	## 1.64-2.40 out, 2.8 radii, so a unit near the centre is inside four at once).
	##
	## THE BOUND, stated so it can be checked rather than hoped for: the radial
	## component is capped by `push_budget` (STRUCTURE_EVICTION_STEP or the travel
	## step, whichever is larger - see _deflect_around_structures), and the
	## tangential re-seat moves the unit ALONG a disc it is already on, so it adds
	## at most one travel-step of arc per disc. The total single-tick displacement
	## is therefore bounded by push_budget + N * |travel_step|, and - the part that
	## matters - it must never place the unit INSIDE any disc it is not allowed
	## through.
	print("--- moving-unit tangential compounding across overlapping discs ---")
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	# The castle is the WALKER'S OWN (team 0) and auto-acquire is off, so nothing
	# can open a pass-through corridor: this is the pure movement case. A hostile
	# castle would be auto-acquired, put the unit in `attack`, and legitimately
	# let it inside - which is the corridor's job, not a compounding failure.
	_add_structure(sim, fortress_id, 0, "fortress", "MenFortress", fortress_at, 7500)
	sim._seed_expansion_pads_for(fortress_id)
	var pieces: Array = (sim.structures[fortress_id] as Dictionary).get("castle_piece_structure_ids", [])
	_check(pieces.size() == 7, "overlapping-disc fixture has the 7-piece castle", "count=%d" % pieces.size())
	var walker := _add_melee_horde(sim, 901, 0, fortress_at + Vector2(-20.0, 0.0))
	walker["auto_acquire_enabled"] = false
	walker["target_id"] = 0
	sim.issue_move([901] as Array[int], fortress_at + Vector2(20.0, 0.0))
	var travel_step: float = float(walker.get("speed", 0.0)) * SimScript.TICK_SECONDS
	_check(
		travel_step > 0.0 and travel_step <= 0.3047 + 0.0001,
		"the walker steps at a retail-scaled speed (the fixture rescale is in force)",
		"step=%.4f ceiling=%.4f" % [travel_step, 0.3047]
	)
	var budget: float = maxf(float(SimScript.STRUCTURE_EVICTION_STEP), travel_step)
	var disc_count: int = pieces.size() + 1  # the pads/citadel plus the fortress itself
	var bound: float = budget + float(disc_count) * travel_step
	var worst_jump := 0.0
	var worst_penetration := 0.0
	var previous := Vector2(walker.get("position", Vector2.ZERO))
	for _tick in range(600):
		sim.winner = -1
		sim.tick()
		var here := Vector2(walker.get("position", Vector2.ZERO))
		worst_jump = maxf(worst_jump, here.distance_to(previous))
		previous = here
		for piece_value in pieces:
			var piece: Dictionary = sim.structures[int(piece_value)]
			worst_penetration = maxf(
				worst_penetration,
				DEFAULT_STRUCTURE_BLOCK_RADIUS
					- here.distance_to(Vector2(piece.get("position", Vector2.ZERO)))
			)
	_check(
		worst_jump <= bound + 0.001,
		"per-tick displacement stays inside push_budget + N*travel_step across discs",
		"worst=%.4f bound=%.4f (budget=%.4f discs=%d step=%.4f)" % [
			worst_jump, bound, budget, disc_count, travel_step
		]
	)
	_check(
		worst_penetration <= 0.001,
		"a moving unit is never re-seated INSIDE a blocking castle piece",
		"worst_penetration=%.4f" % worst_penetration
	)


func _test_banner_carrier_eviction_exclusion() -> void:
	## A BANNER CARRIER HAS NO POSITION OF ITS OWN. _step_banner_carriers glues it
	## to its parent, and it runs IMMEDIATELY BEFORE _step_structure_eviction in
	## the same tick. Evicting it therefore writes a value that the next tick's
	## glue discards, while corrupting the authoritative position everything else
	## reads in between.
	##
	## The fixture is the ordinary defensive posture, not a contrivance: a horde
	## parked against its own castle wall puts its banner inside a footprint.
	print("--- banner carriers are not evicted ---")
	var sim: Object = _make_castle_pathing_sim()
	sim._unit_banner_carriers["bfme2.object.gondor-fighter-horde"] = {
		"object_id": "bfme2.object.gondor-infantry-banner",
		"source_banner_object_id": "GondorInfantryBanner",
		"min_level": 0,
		"offset_source": Vector2.ZERO,
		"destroy_horde_on_death": false,
		"banner_max_health": 80,
	}
	var barracks_at := Vector2(100.0, 200.0)
	_add_structure(sim, 70, 0, "barracks", "GondorBarracks", barracks_at, 2000)
	# Park the horde ON the barracks centre so its banner (zero offset) is as deep
	# inside the footprint as it can be.
	var horde := _add_melee_horde(sim, 902, 0, barracks_at)
	horde["route"] = []
	horde["target_id"] = 0
	horde["auto_acquire_enabled"] = false
	sim._spatial_sync(horde)
	sim._refresh_banner_carrier_state(horde)
	var banner_id := int(horde.get("banner_entity_id", 0))
	_check(banner_id != 0 and sim.entities.has(banner_id), "banner carrier spawned for the fixture")
	if banner_id == 0 or not sim.entities.has(banner_id):
		return
	var banner: Dictionary = sim.entities[banner_id]
	_check(
		bool(banner.get("is_banner_carrier", false)),
		"the fixture banner is flagged is_banner_carrier"
	)
	_check(
		Vector2(banner.get("position", Vector2.ZERO)).distance_to(barracks_at)
			< DEFAULT_STRUCTURE_BLOCK_RADIUS,
		"the banner starts inside the structure footprint",
		"d=%.3f" % Vector2(banner.get("position", Vector2.ZERO)).distance_to(barracks_at)
	)
	# Drive eviction alone, with the glue NOT re-run afterwards, so any nudge it
	# applies is visible instead of being immediately overwritten.
	var before := Vector2(banner.get("position", Vector2.ZERO))
	var parent_before := Vector2(horde.get("position", Vector2.ZERO))
	sim._step_structure_eviction()
	_check(
		before.is_equal_approx(Vector2(banner.get("position", Vector2.ZERO))),
		"structure eviction leaves a banner carrier's position alone",
		"before=%s after=%s" % [str(before), str(Vector2(banner.get("position", Vector2.ZERO)))]
	)
	# The exclusion is narrow: the PARENT horde standing in the same footprint is
	# still evicted, so this is not a blanket opt-out.
	_check(
		not parent_before.is_equal_approx(Vector2(horde.get("position", Vector2.ZERO))),
		"the parent horde in the same footprint IS still evicted",
		"before=%s after=%s" % [
			str(parent_before), str(Vector2(horde.get("position", Vector2.ZERO)))
		]
	)
	# And the banner follows its parent out, through the glue that owns it.
	sim._step_banner_carriers()
	_check(
		Vector2(banner.get("position", Vector2.ZERO)).is_equal_approx(
			Vector2(horde.get("position", Vector2.ZERO))
		),
		"the banner follows the evicted parent through its own glue pass",
		"banner=%s parent=%s" % [
			str(Vector2(banner.get("position", Vector2.ZERO))),
			str(Vector2(horde.get("position", Vector2.ZERO))),
		]
	)


func _test_auto_acquire_structure_footprint() -> void:
	## ACQUISITION AND RANGE MUST USE THE SAME SEMANTIC. Round 20 made firing at a
	## structure surface-to-surface but left _nearest_auto_target centre-to-centre,
	## so a HoldGround melee horde standing AT a fortress wall - in weapon range,
	## able to hit it the instant it was ordered to - was six times outside its own
	## acquisition limit and never picked the building up.
	##
	## HoldGround is the sharp case because it clamps the acquisition limit to the
	## unit's own AttackRange (0.305 sim for retail melee) rather than to vision.
	print("--- auto-acquire uses the structure footprint ---")
	var sim: Object = _make_castle_pathing_sim()
	var fortress_id := 50
	var fortress_at := Vector2(100.0, 200.0)
	_add_structure(sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	(sim.structures[fortress_id] as Dictionary)["footprint_radius_source"] = (
		MENFORTRESS_FOOTPRINT_SOURCE_RADIUS
	)
	var footprint: float = MENFORTRESS_FOOTPRINT_SOURCE_RADIUS * RETAIL_MAP_TRANSFORM_SCALE
	var melee_range: float = 11.5 * RETAIL_MAP_TRANSFORM_SCALE
	# Stand it at the wall: exactly on the footprint, i.e. surface distance 0.
	var horde := _add_wall_melee_horde(sim, 903, 0, fortress_at + Vector2(-footprint, 0.0))
	horde["stance"] = "HoldGround"
	var acquired: Dictionary = sim._nearest_auto_target(horde)
	_check(
		int(acquired.get("id", 0)) == fortress_id and String(acquired.get("kind", "")) == "structure",
		"a HoldGround melee horde at the wall auto-acquires the fortress",
		"got=%s footprint=%.4f range=%.4f" % [str(acquired), footprint, melee_range]
	)
	# The boundary from the other side: beyond footprint + AttackRange it must NOT
	# acquire, so the fix cannot be "acquire everything".
	var far_sim: Object = _make_castle_pathing_sim()
	_add_structure(far_sim, fortress_id, 1, "fortress", "MenFortress", fortress_at, 7500)
	(far_sim.structures[fortress_id] as Dictionary)["footprint_radius_source"] = (
		MENFORTRESS_FOOTPRINT_SOURCE_RADIUS
	)
	var far_horde := _add_wall_melee_horde(
		far_sim, 904, 0, fortress_at + Vector2(-(footprint + melee_range + 0.5), 0.0)
	)
	far_horde["stance"] = "HoldGround"
	var far_acquired: Dictionary = far_sim._nearest_auto_target(far_horde)
	_check(
		int(far_acquired.get("id", 0)) == 0,
		"beyond footprint+AttackRange it still does NOT acquire",
		"got=%s" % str(far_acquired)
	)


func _test_small_structure_footprint_fallback() -> void:
	## THE FALLBACK IS A FLOOR, NOT A GUESS. A structure whose document carries no
	## compiled geometry used to fall back to 50.0 source units - over-expanding
	## most of the corpus (censused: median authored radius 48, and 104 of 196
	## documents are below 50) and handing free weapon reach and free acquisition
	## range to exactly the objects whose real size is unknown.
	##
	## COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS is 5.0: the smallest authored radius
	## in any pack (the fortress expansion pads), i.e. the greatest value that
	## cannot exceed a real footprint. The fortress fallback is unchanged at 64.0,
	## which is MenFortress's authored GeometryMajorRadius verbatim.
	print("--- small-structure footprint fallback ---")
	var sim: Object = _make_castle_pathing_sim()
	_add_structure(sim, 71, 1, "barracks", "ObjectAbsentFromEveryPack", Vector2(100.0, 200.0), 2000)
	var measured: float = float(sim._structure_footprint_radius(sim.structures[71] as Dictionary))
	var expected: float = (
		float(SimScript.COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS) * RETAIL_MAP_TRANSFORM_SCALE
	)
	_check(
		absf(measured - expected) <= 0.0001,
		"a non-fortress structure with no compiled geometry falls back to the 5.0 floor",
		"got=%.4f expected=%.4f" % [measured, expected]
	)
	_check(
		measured < 50.0 * RETAIL_MAP_TRANSFORM_SCALE,
		"and it is strictly smaller than the old 50.0 selection default",
		"got=%.4f old=%.4f" % [measured, 50.0 * RETAIL_MAP_TRANSFORM_SCALE]
	)
	# The memo must not answer for a key the ContentDB lookup would treat
	# differently: the registry is an exact Dictionary hit, so two ids differing
	# only in case are two different lookups. This runner loads no packs, so both
	# ids miss and the assertion is that they miss INDEPENDENTLY - the shouted id
	# must not be able to borrow a lowered-key entry from the exact one.
	_add_structure(sim, 72, 1, "barracks", "GondorBarracks", Vector2(140.0, 200.0), 2000)
	(sim.structures[72] as Dictionary)["footprint_radius_source"] = (
		GONDORBARRACKS_FOOTPRINT_SOURCE_RADIUS
	)
	var pinned: float = float(sim._structure_footprint_radius(sim.structures[72] as Dictionary))
	_check(
		absf(pinned - GONDORBARRACKS_FOOTPRINT_SOURCE_RADIUS * RETAIL_MAP_TRANSFORM_SCALE) <= 0.0001,
		"an explicit row footprint still wins over both the lookup and the fallback",
		"got=%.4f" % pinned
	)
	_check(
		not sim._structure_footprint_source_cache.has("objectabsentfromeverypack|barracks"),
		"the memo is keyed on the exact id the ContentDB lookup uses, not a lowered one",
		"keys=%s" % str(sim._structure_footprint_source_cache.keys())
	)
