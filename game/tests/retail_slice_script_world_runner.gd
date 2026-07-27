extends SceneTree

## Proof runner for RetailSliceScriptWorld - the first SageScriptWorld backed
## by the real RetailSliceSim.
##
## Covers, in order:
##   1. bindings        - player/team name bindings validate against the
##                        roster, stay 1:1 where owner() needs it, and reject
##                        rebinds
##   2. base world      - capability set, world_frame, debug sink
##   3. players facet   - exists/faction/command points/building_count/
##                        relation_to, with the strict refusals for unbound
##                        names and unmodeled building classes
##   4. teams facet     - exists/unit_count/owner/stop, including the
##                        disband refusal
##   5. orders facet    - move_to/attack_move_to (POSITION), attack (TEAM
##                        target, exact-tie lowest-id pick), stand_ground,
##                        plus scope and target-kind refusals
##   6. combat facet    - spellbook power ready/cast, player_all_destroyed
##   7. progression     - upgrade reads, build_upgrade research queueing,
##                        the science surface, any_hero_reached_rank
##   8. economy facet   - money read/set/give with per-player refusal
##   9. meta facet      - player_count, multiplayer_outcome across a real
##                        victory resolution
##  10. read-only-ness  - every implemented QUERY leaves state_hash()
##                        untouched across repeated evaluation
##  11. determinism     - two independently built sims driven through two
##                        independently built worlds give identical answers
##                        and identical state hashes, including the
##                        tie-breaking attack target pick
##
## Every fixture is SYNTHETIC (the pin runner's harness rules); no retail
## install or content pack is required.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_slice_script_world_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ParamTypes = preload("res://src/script/script_param_types.gd")

const PLAYER := "PlayerOne"
const ENEMY := "EnemyOne"
const PLAYER_TEAM_NAME := "teamPlayerOne"
const ENEMY_TEAM_NAME := "teamEnemyOne"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_bindings()
	_test_base_world()
	_test_players_exists_and_faction()
	_test_players_command_points()
	_test_players_building_count()
	_test_players_relation_to()
	_test_teams_reads()
	_test_teams_stop()
	_test_orders_move_and_attack_move()
	_test_orders_scope_and_target_refusals()
	_test_orders_attack_nearest()
	_test_orders_attack_exact_tie_prefers_lowest_id()
	_test_orders_stand_ground()
	_test_combat_player_all_destroyed()
	_test_science_and_powers()
	_test_progression_upgrades()
	_test_progression_hero_rank()
	_test_economy_money()
	_test_meta_outcomes()
	_test_queries_are_read_only()
	_test_twin_worlds_agree()
	print("RETAIL_SLICE_SCRIPT_WORLD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _check_hit(name: String, query: SageWorldQuery, expected: Variant) -> void:
	_check("%s (answered)" % name, query.ok)
	if query.ok:
		_check("%s (value)" % name, query.value == expected)


func _check_refused(name: String, query: SageWorldQuery) -> void:
	_check("%s (refused)" % name, not query.ok and query.detail != "")


# --- Fixtures -------------------------------------------------------------


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
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
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _make_sim(roster: Array = []) -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	if not roster.is_empty():
		sim.configure_team_roster(roster)
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_team(PLAYER_TEAM_NAME, SimScript.PLAYER_TEAM)
	world.bind_team(ENEMY_TEAM_NAME, SimScript.ENEMY_TEAM)
	return world


func _spellbook_document() -> Dictionary:
	return {
		"schema": "openbfme.spellbook-runtime",
		"registration": {
			"spellBook": {"intrinsicSciences": []},
			"powerTree": {
				"sciences": [
					{
						"id": "SCIENCE_TestHeal",
						"purchase": {"slot": 1},
						"pointCostMP": {"value": 1},
						"prerequisiteGroups": [],
					},
				],
				"powers": [
					{
						"id": "SpellBookTestHeal",
						"requiredSciences": ["SCIENCE_TestHeal"],
						"cast": {"slot": 1, "options": ["NEED_TARGET_POS"], "iconIds": []},
						"reloadTimeMs": {"value": 5000.0},
						"radiusCursorRadius": {"value": 40.0},
						"effect": {
							"module": "PlayerHealSpecialPower",
							"fields": [
								{"key": "HealAmount", "value": "0.5"},
								{"key": "HealAsPercent", "value": "Yes"},
								{"key": "HealAffects", "value": "ANY"},
								{"key": "HealRadius", "value": "50", "resolved": 50.0},
							],
							"references": {},
						},
					},
				],
			},
			"leaves": {},
		},
	}


func _structure_id_of_kind(sim: RetailSliceSim, team: int, kind: String) -> int:
	for structure_id in sim.structure_ids(team):
		if String((sim.structures[structure_id] as Dictionary).get("structure_kind", "")) == kind:
			return structure_id
	return 0


func _inject_research_contract(sim: RetailSliceSim) -> void:
	## Post-setup injection of a synthetic barracks research so the upgrade
	## surface is exercisable without a forge-bearing pack. The per-team
	## contract tables alias this global dict for the default roster.
	sim._structure_upgrade_contracts["Upgrade_TestTech"] = {
		"structure_kind": "barracks",
		"cost": 300,
		"duration_ticks": 10,
		"level_cap": 99,
		"levels_to_gain": 0,
		"cancelable": true,
		"to_command_set": "",
		"team_tech": true,
	}


# --- 1. Bindings ----------------------------------------------------------


func _test_bindings() -> void:
	var sim := _make_sim()
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	_check("bind_player accepts a rostered team", world.bind_player(PLAYER, 0))
	_check("bind_player rejects an unknown team", not world.bind_player("Ghost", 7))
	_check("bind_player rejects an empty name", not world.bind_player("", 1))
	_check("bind_player is idempotent for the same pair", world.bind_player(PLAYER, 0))
	_check(
		"bind_player rejects rebinding a name to another team",
		not world.bind_player(PLAYER, 1)
	)
	_check(
		"bind_player rejects a second name on a bound team",
		not world.bind_player("PlayerOneAlias", 0)
	)
	_check("bind_team accepts a rostered team", world.bind_team(PLAYER_TEAM_NAME, 0))
	_check("bind_team allows an alias onto the same team", world.bind_team("teamAlias", 0))
	_check(
		"bind_team rejects rebinding a name to another team",
		not world.bind_team(PLAYER_TEAM_NAME, 1)
	)
	_check("bind_team rejects an unknown team", not world.bind_team("teamGhost", 42))


# --- 2. Base world --------------------------------------------------------


func _test_base_world() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check("supports player money", world.supports(SageScriptWorld.CAP_PLAYER_MONEY))
	_check("supports debug output", world.supports(SageScriptWorld.CAP_DEBUG_OUTPUT))
	_check(
		"refuses CAP_RANDOM (no deterministic stream in the sim)",
		not world.supports(SageScriptWorld.CAP_RANDOM)
	)
	_check("world_frame starts at the sim tick", world.world_frame() == 0)
	sim.advance(3)
	_check("world_frame follows the sim tick", world.world_frame() == 3)
	_check("debug_message is accepted", world.debug_message("DEBUG_STRING", "hello"))


# --- 3. Players -----------------------------------------------------------


func _test_players_exists_and_faction() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit("players.exists for a bound player", world.players().exists(PLAYER), true)
	_check_hit(
		"players.exists is false for an unbound name (exhaustive bindings)",
		world.players().exists("Nobody"),
		false
	)
	_check_refused(
		"players.faction refuses when the descriptor has no faction",
		world.players().faction(PLAYER)
	)
	var faction_sim := _make_sim([
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
	])
	var faction_world := _make_world(faction_sim)
	_check_hit(
		"players.faction answers the descriptor faction",
		faction_world.players().faction(PLAYER),
		"men"
	)


func _test_players_command_points() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var total := world.players().command_points_total(PLAYER)
	_check_hit("players.command_points_total is the cap", total, sim.command_point_cap)
	var used_before := world.players().command_points_used(PLAYER)
	_check(
		"players.command_points_used answers the committed points",
		used_before.ok and used_before.as_int() == sim.command_points_for_team(0)
	)
	var available_before := world.players().command_points_available(PLAYER)
	_check(
		"players.command_points_available is cap minus committed before queueing",
		available_before.ok
		and available_before.as_int() == sim.command_point_cap - used_before.as_int()
	)
	var barracks := _structure_id_of_kind(sim, 0, "barracks")
	var queued: Dictionary = sim.queue_unit(0, barracks, SimScript.SOLDIER_HORDE_ID)
	_check("fixture: soldier horde queues at the barracks", bool(queued.get("ok", false)))
	var reserved := int((queued.get("item", {}) as Dictionary).get("command_points", 0))
	_check("fixture: the queued soldier reserves command points", reserved > 0)
	var available_after := world.players().command_points_available(PLAYER)
	_check(
		"players.command_points_available subtracts queue-reserved points",
		available_after.ok
		and available_after.as_int() == available_before.as_int() - reserved
	)
	_check_refused(
		"players.command_points_available refuses an unbound player",
		world.players().command_points_available("Nobody")
	)


func _test_players_building_count() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var seeded := sim.living_structure_ids(0).size()
	_check_hit(
		"players.building_count with empty class counts every living structure",
		world.players().building_count(PLAYER, ""),
		seeded
	)
	_check_hit(
		"players.building_count counts one farm",
		world.players().building_count(PLAYER, "farm"),
		1
	)
	var farm := _structure_id_of_kind(sim, 0, "farm")
	(sim.structures[farm] as Dictionary)["health"] = 0
	_check_hit(
		"players.building_count drops a razed farm",
		world.players().building_count(PLAYER, "farm"),
		0
	)
	_check_hit(
		"players.building_count empty-class drops the razed farm too",
		world.players().building_count(PLAYER, ""),
		seeded - 1
	)
	_check_refused(
		"players.building_count refuses an unmodeled building class",
		world.players().building_count(PLAYER, "inn")
	)


func _test_players_relation_to() -> void:
	var relation: Dictionary = ParamTypes.ENUMS["RELATION"]
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"players.relation_to hostile roster pair is Enemy",
		world.players().relation_to(PLAYER, ENEMY),
		int(relation["Enemy"])
	)
	_check_hit(
		"players.relation_to self is Friend",
		world.players().relation_to(PLAYER, PLAYER),
		int(relation["Friend"])
	)
	_check_refused(
		"players.relation_to refuses an unbound other player",
		world.players().relation_to(PLAYER, "Nobody")
	)
	var allied_sim := _make_sim([
		{"team": 0, "faction": "", "is_ai": false, "alliance": "east"},
		{"team": 1, "faction": "", "is_ai": true, "alliance": "east"},
	])
	var allied_world := _make_world(allied_sim)
	_check_hit(
		"players.relation_to shared alliance is Friend",
		allied_world.players().relation_to(PLAYER, ENEMY),
		int(relation["Friend"])
	)


# --- 4. Teams -------------------------------------------------------------


func _test_teams_reads() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit("teams.exists for a bound team", world.teams().exists(PLAYER_TEAM_NAME), true)
	_check_hit(
		"teams.exists is false for an unbound name", world.teams().exists("teamGhost"), false
	)
	_check_hit(
		"teams.unit_count counts living battalions",
		world.teams().unit_count(PLAYER_TEAM_NAME),
		sim.living_ids(0).size()
	)
	var before := sim.living_ids(0).size()
	(sim.entities[2] as Dictionary)["health"] = 0
	_check_hit(
		"teams.unit_count drops a dead battalion",
		world.teams().unit_count(PLAYER_TEAM_NAME),
		before - 1
	)
	_check_hit(
		"teams.owner answers the bound player name",
		world.teams().owner(PLAYER_TEAM_NAME),
		PLAYER
	)
	var half_bound: RetailSliceScriptWorld = WorldScript.new(sim)
	half_bound.bind_team(PLAYER_TEAM_NAME, 0)
	_check_refused(
		"teams.owner refuses when no player name is bound to the team",
		half_bound.teams().owner(PLAYER_TEAM_NAME)
	)


func _test_teams_stop() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"fixture: move order puts the team in motion",
		world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_position(Vector3(-20.0, 0.0, -14.0))
		)
	)
	_check(
		"fixture: a battalion is running", String((sim.entities[1] as Dictionary)["state"]) == "run"
	)
	_check("teams.stop is accepted", world.teams().stop(PLAYER_TEAM_NAME, false))
	_check(
		"teams.stop idles the battalion",
		String((sim.entities[1] as Dictionary)["state"]) == "idle"
	)
	_check(
		"teams.stop refuses the disband variant",
		not world.teams().stop(PLAYER_TEAM_NAME, true)
	)
	_check(
		"teams.stop refuses an unbound team", not world.teams().stop("teamGhost", false)
	)


# --- 5. Orders ------------------------------------------------------------


func _test_orders_move_and_attack_move() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"orders.move_to TEAM scope POSITION target is accepted",
		world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_position(Vector3(-20.0, 5.0, -14.0))
		)
	)
	var row: Dictionary = sim.entities[1]
	_check("move order lands as order_kind move", String(row["order_kind"]) == "move")
	_check(
		"move destination maps world (x,z) onto sim (x,y)",
		Vector2(row["destination"]).distance_to(Vector2(-20.0, -14.0)) < 0.001
	)
	_check(
		"orders.attack_move_to PLAYER scope is accepted",
		world.orders().attack_move_to(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			SageScriptWorld.target_position(Vector3(-18.0, 0.0, -10.0))
		)
	)
	_check(
		"attack-move order lands as order_kind attack_move",
		String((sim.entities[1] as Dictionary)["order_kind"]) == "attack_move"
	)


func _test_orders_scope_and_target_refusals() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"orders.move_to refuses UNIT scope (no object-name binding)",
		not world.orders().move_to(
			SageScriptWorld.Scope.UNIT,
			"SomeNamedUnit",
			SageScriptWorld.target_position(Vector3.ZERO)
		)
	)
	_check(
		"orders.move_to refuses a WAYPOINT target (no map geometry)",
		not world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_waypoint("Waypoint01")
		)
	)
	_check(
		"orders.attack refuses an OBJECT target (no object-name binding)",
		not world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_object("SomeNamedUnit")
		)
	)
	_check(
		"orders.attack refuses an unbound target team",
		not world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team("teamGhost")
		)
	)


func _position_enemy_spread(sim: RetailSliceSim, near_a: Vector2, near_b: Vector2) -> void:
	## Attacker 1 at the origin; enemy 101/102 at the given spots; the other
	## enemy battalions far away so they never win the pick.
	(sim.entities[1] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[101] as Dictionary)["position"] = near_a
	(sim.entities[102] as Dictionary)["position"] = near_b
	(sim.entities[103] as Dictionary)["position"] = Vector2(50.0, 50.0)
	(sim.entities[104] as Dictionary)["position"] = Vector2(60.0, 60.0)


func _test_orders_attack_nearest() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(0.0, 5.0))
	_check(
		"orders.attack TEAM target is accepted",
		world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team(ENEMY_TEAM_NAME)
		)
	)
	_check(
		"orders.attack picks the strictly nearest target member",
		int((sim.entities[1] as Dictionary)["target_id"]) == 102
	)


func _test_orders_attack_exact_tie_prefers_lowest_id() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	# 101 and 102 are EXACTLY equidistant from the lowest-id attacker
	# (distance_squared 100.0 on both sides): only the documented total order
	# (distance, then lowest id) decides.
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(-10.0, 0.0))
	_check(
		"orders.attack tie order is accepted",
		world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team(ENEMY_TEAM_NAME)
		)
	)
	_check(
		"orders.attack exact tie resolves to the lowest candidate id",
		int((sim.entities[1] as Dictionary)["target_id"]) == 101
	)
	_check(
		"orders.attack refuses when the target team has no living member",
		not _attack_on_emptied_team()
	)


func _attack_on_emptied_team() -> bool:
	var sim := _make_sim()
	var world := _make_world(sim)
	for id in sim.living_ids(1):
		(sim.entities[id] as Dictionary)["health"] = 0
	return world.orders().attack(
		SageScriptWorld.Scope.TEAM,
		PLAYER_TEAM_NAME,
		SageScriptWorld.target_team(ENEMY_TEAM_NAME)
	)


func _test_orders_stand_ground() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"orders.stand_ground TEAM scope is accepted",
		world.orders().stand_ground(SageScriptWorld.Scope.TEAM, PLAYER_TEAM_NAME)
	)
	var all_hold := true
	for id in sim.living_ids(0):
		if String((sim.entities[id] as Dictionary)["stance"]) != "HoldGround":
			all_hold = false
	_check("orders.stand_ground sets the retail HoldGround stance", all_hold)
	_check(
		"orders.stand_ground refuses UNIT scope",
		not world.orders().stand_ground(SageScriptWorld.Scope.UNIT, "SomeNamedUnit")
	)


# --- 6. Combat ------------------------------------------------------------


func _test_combat_player_all_destroyed() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"combat.player_all_destroyed is false while anything lives",
		world.combat().player_all_destroyed(ENEMY, false),
		false
	)
	for id in sim.living_ids(1):
		(sim.entities[id] as Dictionary)["health"] = 0
	_check_hit(
		"combat.player_all_destroyed stays false while structures live",
		world.combat().player_all_destroyed(ENEMY, false),
		false
	)
	for structure_id in sim.living_structure_ids(1):
		(sim.structures[structure_id] as Dictionary)["health"] = 0
	_check_hit(
		"combat.player_all_destroyed is true after the full wipe",
		world.combat().player_all_destroyed(ENEMY, false),
		true
	)
	_check_refused(
		"combat.player_all_destroyed refuses the build-facilities-only variant",
		world.combat().player_all_destroyed(ENEMY, true)
	)


# --- 7. Sciences and powers -----------------------------------------------


func _test_science_and_powers() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_refused(
		"progression.has_science refuses without a spellbook document",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check(
		"fixture: synthetic spellbook document configures",
		sim.configure_spellbook_runtime(_spellbook_document())
	)
	_check_hit(
		"progression.science_purchase_points answers the seeded pool",
		world.progression().science_purchase_points(PLAYER),
		sim.power_points(0)
	)
	_check_hit(
		"progression.has_science is false before purchase",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal"),
		false
	)
	_check_refused(
		"progression.has_science refuses a science outside the tree",
		world.progression().has_science(PLAYER, "SCIENCE_Bogus")
	)
	_check_hit(
		"combat.special_power_ready is false before the power is owned",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		false
	)
	_check_hit(
		"progression.can_purchase_science is true with points in hand",
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal"),
		true
	)
	_check(
		"progression.purchase_science buys the power",
		world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check_hit(
		"progression.has_science is true after purchase",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal"),
		true
	)
	_check_hit(
		"progression.can_purchase_science is false once owned",
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal"),
		false
	)
	_check(
		"progression.purchase_science refuses a second purchase",
		not world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check_hit(
		"combat.special_power_ready is true when owned and off cooldown",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		true
	)
	_check_refused(
		"combat.special_power_ready refuses a power outside the document",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookBogus"
		)
	)
	# Wound a member so the heal has a target, then cast at its position.
	var row: Dictionary = sim.entities[1]
	(row["member_health"] as Array)[0] = 100
	row["health"] = 100
	var at := Vector2(row["position"])
	_check(
		"combat.fire_special_power casts the owned power",
		world.combat().fire_special_power(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3(at.x, 0.0, at.y))
		)
	)
	_check("the cast actually healed the battalion", int(row["health"]) > 100)
	_check_hit(
		"combat.special_power_ready is false while recharging",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		false
	)
	_check(
		"combat.fire_special_power refuses while recharging",
		not world.combat().fire_special_power(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3(at.x, 0.0, at.y))
		)
	)
	_check(
		"combat.fire_special_power refuses TEAM scope",
		not world.combat().fire_special_power(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3.ZERO)
		)
	)


# --- 8. Progression upgrades ----------------------------------------------


func _test_progression_upgrades() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_inject_research_contract(sim)
	_check_refused(
		"progression.has_upgrade refuses an unmodeled upgrade",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_Bogus")
	)
	_check_refused(
		"progression.has_upgrade refuses TEAM scope",
		world.progression().has_upgrade(
			SageScriptWorld.Scope.TEAM, PLAYER_TEAM_NAME, "Upgrade_TestTech"
		)
	)
	_check_hit(
		"progression.has_upgrade is false before research",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"),
		false
	)
	var resources_before := sim.resources_for_team(0)
	_check(
		"progression.build_upgrade queues the research",
		world.progression().build_upgrade(PLAYER, "Upgrade_TestTech")
	)
	_check(
		"build_upgrade charged the contract cost",
		sim.resources_for_team(0) == resources_before - 300
	)
	var barracks := _structure_id_of_kind(sim, 0, "barracks")
	_check(
		"build_upgrade queued at the lowest living structure of the kind",
		sim.structure_upgrade_queue_state(barracks).size() == 1
	)
	_check(
		"progression.build_upgrade refuses an unknown upgrade",
		not world.progression().build_upgrade(PLAYER, "Upgrade_Bogus")
	)
	sim.advance(12)
	_check_hit(
		"progression.has_upgrade is true after the research completes",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"),
		true
	)
	_check_hit(
		"progression.unit_count_with_upgrade is zero before any horde equips",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech"),
		0
	)
	(sim.entities[1] as Dictionary)["applied_upgrades"] = {"Upgrade_TestTech": 1}
	_check_hit(
		"progression.unit_count_with_upgrade counts an equipped horde",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech"),
		1
	)
	_check_refused(
		"progression.unit_count_with_upgrade refuses an unmodeled upgrade",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_Bogus")
	)


func _test_progression_hero_rank() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"any_hero_reached_rank is false with no heroes fielded",
		world.progression().any_hero_reached_rank(PLAYER, 2),
		false
	)
	# Promote the archer battalion to an authored hero at level 3.
	var hero: Dictionary = sim.entities[2]
	hero["category"] = "hero"
	hero["level"] = 3
	hero["experience_xp"] = 0
	sim._unit_experience_rules[String(hero["unit_type"])] = {
		"initial_rank": 1,
		"max_level": 5,
		"levels": [],
	}
	_check_hit(
		"any_hero_reached_rank sees the authored hero at rank",
		world.progression().any_hero_reached_rank(PLAYER, 3),
		true
	)
	_check_hit(
		"any_hero_reached_rank is false above the hero's rank",
		world.progression().any_hero_reached_rank(PLAYER, 4),
		false
	)
	# An UNAUTHORED hero makes the negative answer unknowable - but a positive
	# answer from an authored hero still stands.
	(sim.entities[3] as Dictionary)["category"] = "hero"
	_check_hit(
		"an authored hero at rank still answers true alongside an unauthored one",
		world.progression().any_hero_reached_rank(PLAYER, 3),
		true
	)
	_check_refused(
		"the negative refuses while an unauthored hero's rank is unknown",
		world.progression().any_hero_reached_rank(PLAYER, 4)
	)


# --- 9. Economy -----------------------------------------------------------


func _test_economy_money() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"economy.money answers the team resources",
		world.economy().money(PLAYER),
		10000
	)
	_check("economy.set_money is accepted", world.economy().set_money(PLAYER, 5000))
	_check("economy.set_money wrote the sim", sim.resources_for_team(0) == 5000)
	_check("economy.give_money adds", world.economy().give_money(PLAYER, 250))
	_check("economy.give_money wrote the sim", sim.resources_for_team(0) == 5250)
	_check("economy.give_money subtracts", world.economy().give_money(PLAYER, -250))
	_check("economy.give_money subtraction wrote the sim", sim.resources_for_team(0) == 5000)
	_check_refused(
		"economy.money refuses an unbound player", world.economy().money("Nobody")
	)
	_check(
		"economy.set_money refuses an unbound player",
		not world.economy().set_money("Nobody", 1)
	)
	_check(
		"legacy player_money reads a bound player", world.player_money(PLAYER) == 5000
	)


# --- 10. Meta -------------------------------------------------------------


func _test_meta_outcomes() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"meta.player_count counts the rostered teams",
		world.meta().player_count(false),
		2
	)
	_check_hit(
		"meta.multiplayer_outcome defeat is false while the match runs",
		world.meta().multiplayer_outcome(ENEMY, "defeat"),
		false
	)
	_check_hit(
		"meta.multiplayer_outcome allied_victory is false while the match runs",
		world.meta().multiplayer_outcome(PLAYER, "allied_victory"),
		false
	)
	_check_refused(
		"meta.multiplayer_outcome refuses an unknown outcome token",
		world.meta().multiplayer_outcome(PLAYER, "sideways")
	)
	# Raze the enemy fortress and let the sim resolve victory for real.
	var enemy_fortress := _structure_id_of_kind(sim, 1, "fortress")
	(sim.structures[enemy_fortress] as Dictionary)["health"] = 0
	sim.advance(1)
	_check("fixture: the sim resolved a winner", sim.winner == 0)
	_check_hit(
		"meta.multiplayer_outcome defeat is true for the razed player",
		world.meta().multiplayer_outcome(ENEMY, "defeat"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome allied_defeat is true for the razed player",
		world.meta().multiplayer_outcome(ENEMY, "allied_defeat"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome allied_victory is true for the winner",
		world.meta().multiplayer_outcome(PLAYER, "allied_victory"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome defeat is false for the winner",
		world.meta().multiplayer_outcome(PLAYER, "defeat"),
		false
	)


# --- 11. Read-only sweep --------------------------------------------------


func _test_queries_are_read_only() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_inject_research_contract(sim)
	sim.configure_spellbook_runtime(_spellbook_document())
	sim.advance(5)
	var before := sim.state_hash()
	for _round in range(3):
		world.world_frame()
		world.players().exists(PLAYER)
		world.players().exists("Nobody")
		world.players().faction(PLAYER)
		world.players().command_points_available(PLAYER)
		world.players().command_points_total(PLAYER)
		world.players().command_points_used(PLAYER)
		world.players().building_count(PLAYER, "")
		world.players().building_count(PLAYER, "farm")
		world.players().building_count(PLAYER, "inn")
		world.players().relation_to(PLAYER, ENEMY)
		world.teams().exists(PLAYER_TEAM_NAME)
		world.teams().unit_count(PLAYER_TEAM_NAME)
		world.teams().owner(PLAYER_TEAM_NAME)
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		)
		world.combat().player_all_destroyed(ENEMY, false)
		world.progression().has_upgrade(
			SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"
		)
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech")
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal")
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal")
		world.progression().science_purchase_points(PLAYER)
		world.progression().any_hero_reached_rank(PLAYER, 2)
		world.economy().money(PLAYER)
		world.meta().player_count(true)
		world.meta().multiplayer_outcome(PLAYER, "defeat")
		world.meta().multiplayer_outcome(PLAYER, "allied_victory")
		world.meta().multiplayer_outcome(PLAYER, "allied_defeat")
	_check(
		"every implemented query leaves the authoritative state hash untouched",
		sim.state_hash() == before
	)


# --- 12. Twin determinism -------------------------------------------------


func _twin_fixture() -> Array:
	## One deterministic build of the sim+world pair, driven ONLY through
	## world-facing commands after setup, so two invocations must agree
	## bit-for-bit.
	var sim := _make_sim()
	var world := _make_world(sim)
	_inject_research_contract(sim)
	sim.configure_spellbook_runtime(_spellbook_document())
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(-10.0, 0.0))
	world.economy().set_money(PLAYER, 8000)
	world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	world.progression().build_upgrade(PLAYER, "Upgrade_TestTech")
	world.orders().attack(
		SageScriptWorld.Scope.TEAM,
		PLAYER_TEAM_NAME,
		SageScriptWorld.target_team(ENEMY_TEAM_NAME)
	)
	world.orders().stand_ground(SageScriptWorld.Scope.PLAYER, ENEMY)
	sim.advance(30)
	return [sim, world]


func _query_battery(world: RetailSliceScriptWorld) -> Array:
	return [
		world.world_frame(),
		world.players().command_points_available(PLAYER).value,
		world.players().command_points_used(PLAYER).value,
		world.players().building_count(PLAYER, "").value,
		world.players().relation_to(PLAYER, ENEMY).value,
		world.teams().unit_count(PLAYER_TEAM_NAME).value,
		world.teams().unit_count(ENEMY_TEAM_NAME).value,
		world.teams().owner(ENEMY_TEAM_NAME).value,
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		).value,
		world.combat().player_all_destroyed(ENEMY, false).value,
		world.progression().has_upgrade(
			SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"
		).value,
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal").value,
		world.progression().science_purchase_points(PLAYER).value,
		world.economy().money(PLAYER).value,
		world.meta().player_count(false).value,
		world.meta().multiplayer_outcome(PLAYER, "defeat").value,
	]


func _test_twin_worlds_agree() -> void:
	var first := _twin_fixture()
	var second := _twin_fixture()
	var sim_a: RetailSliceSim = first[0]
	var sim_b: RetailSliceSim = second[0]
	_check(
		"twin sims driven through twin worlds share one state hash",
		sim_a.state_hash() == sim_b.state_hash()
	)
	_check(
		"twin worlds picked the identical tie-broken attack target",
		int((sim_a.entities[1] as Dictionary)["target_id"])
		== int((sim_b.entities[1] as Dictionary)["target_id"])
	)
	var battery_a := _query_battery(first[1])
	var battery_b := _query_battery(second[1])
	_check("twin worlds answer the full query battery identically", battery_a == battery_b)
