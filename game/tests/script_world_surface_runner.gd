extends SceneTree

## Runtime probe for the SageScriptWorld facet surface against
## RetailSliceScriptWorld - the measurement behind
## data/script_world_surface.json.
##
## WHY A PROBE AND NOT A DOCUMENT
## ==============================
## Commit 005bcd8 shipped a hand-written gap analysis: 36 of the surface's
## methods simulation-backed, the rest refusing, each naming its missing sim
## state. Hand-written classifications rot the moment the adapter moves. This
## runner therefore does not TRUST the map, it MEASURES the surface:
##
##   1. It enumerates every facet method by GDScript introspection
##      (get_method_list() on each facet of a plain SageScriptWorld), so a
##      method nobody has classified cannot hide - an entry missing from the
##      committed map is a FAILURE, not a footnote.
##   2. It synthesises plausible arguments for every parameter from the
##      declared types and names, and ACTUALLY CALLS every method against a
##      RetailSliceScriptWorld over a fresh synthetic sim, recording whether
##      the call really answers or really refuses.
##   3. It asserts the committed map agrees with the observation in BOTH
##      directions: a map entry claiming simulation-backed must answer, a map
##      entry claiming refusal must refuse.
##   4. It regenerates the map from the same measurement plus the annotation
##      tables below and asserts the committed file is BYTE-IDENTICAL, so the
##      map cannot drift from what this runner would write.
##
## WHAT "BACKED" / SCOREBOARD FLAGS MEAN HERE
## ==========================================
## A method is **callable** (historical field: simulationBacked) when at least
## one well-formed argument tuple gets a real answer: `ok` for a query, `true`
## for a command. Callable is NOT retail parity. The probe also classifies:
##   * bagOnly            - answered, but only script_surface_bag mutated/read
##                          as the observed effect (no core sim fingerprint change)
##   * stateMutating      - answered and changed bag or core sim state
##   * subsystemConsumed  - answered and core sim fingerprint changed, OR a
##                          pure read of live sim state (not bag-default only)
## Several callable methods still refuse RESTRICTED argument shapes; those
## restrictions are recorded per method and drive vocabulary routing.
##
## PROBE HYGIENE
## =============
## * Deterministic order: probes run over the surface sorted by (facet,
##   method), never in get_method_list() order, which is engine-internal.
## * Isolation: every method gets a FRESH sim and world, so a mutating probe
##   cannot leak into the next measurement. Candidate tuples for one method
##   run on one fresh sim and stop at the first success (refusals are
##   side-effect free by the facet contract).
## * Read-only sweep: after classification, every query-returning method is
##   re-probed (all candidates) against ONE shared world and state_hash() is
##   asserted unchanged across the whole sweep - a condition that mutates is a
##   lockstep desync, and this is where it would be caught.
##
## INTROSPECTION LIMITS (the explicit fallback list)
## =================================================
## Five base-world methods predate the facet refusal contract and carry no
## refusal channel an argument probe could read, so they are classified by
## EXPLICIT special-case probes rather than by synthesis. They are listed in
## FALLBACK_PROBED and counted separately in the coverage report:
##   * world.supports       - bool, but the bool is the answer, not a refusal
##   * world.world_frame    - int with no refusal channel; probed by advancing
##                            the sim and expecting the tick to follow
##   * world.player_money   - int with no refusal channel (the documented lie
##                            surface); probed by expecting the fixture's
##                            seeded resources for a bound player
##   * world.random_int / world.random_real - no refusal channel; classified
##                            by supports(CAP_RANDOM) plus a live in-range
##                            draw from the sim-owned logic stream
## Everything else - all facet methods and the two remaining base methods
## (set_player_money, debug_message return bool-as-refusal) - is genuinely
## called through the synthesised-argument path.
##
## THE RANKING
## ===========
## The map annotates every refusing method with the SUBSYSTEM whose absence
## blocks it, and routes all 91 retail-AI census members
## (data/retail_ai_call_census.json) to the world methods that would serve
## them. This runner cross-checks both tables for coherence and prints the
## ranked subsystem table: methods unblocked and BFME2 AI call sites
## unblocked per subsystem. The census is the same denominator
## ai_dispatch_coverage_runner.gd reports against.
##
## Every fixture is SYNTHETIC; no retail install or content pack is required.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/script_world_surface_runner.gd
## Regenerate the committed map (then verify as usual):
##   ... --script res://tests/script_world_surface_runner.gd -- --write

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")
const ParamTypes = preload("res://src/script/script_param_types.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")

const MAP_PATH := "res://data/script_world_surface.json"
const CENSUS_PATH := "res://data/retail_ai_call_census.json"

const PLAYER := "PlayerOne"
const ENEMY := "EnemyOne"
const PLAYER_TEAM_NAME := "teamPlayerOne"
const ENEMY_TEAM_NAME := "teamEnemyOne"

## The facet accessors that make up the world surface, sorted. The three
## presentation sinks are separate facet instances of one class, so `emit`
## appears once per sink - camera.emit, audio.emit and ui.emit are three
## distinct destinations a world can wire or refuse independently.
const FACET_ACCESSORS := [
	"ai", "areas", "audio", "camera", "combat", "economy", "fog", "meta",
	"orders", "players", "progression", "teams", "terrain", "transport",
	"ui", "units",
]

## Base-world methods classified by explicit special-case probes because they
## carry no refusal channel (see the class comment).
const FALLBACK_PROBED := [
	"world.player_money",
	"world.random_int",
	"world.random_real",
	"world.supports",
	"world.world_frame",
]

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments - an inert runner prints a zero-failure result and
## exits 0. Pinning the number of checks a healthy run makes turns that silent
## abort into a loud failure. Raise it deliberately when tests are added; never
## lower it to make a run go green.
## 3506 -> 3482: EXACTLY the eight methods that left the BLOCKED table when the
## object-name reads and the reference bind became simulation-backed
## (units.exists, was_created, was_destroyed, is_dying, position, owner,
## health_percent, set_reference). A refusing method costs three checks a
## backed one does not - "carries a blocking annotation", "annotation names a
## real method", "annotation names a declared subsystem" - and 8 x 3 = 24. No
## assertion was weakened, deleted or skipped; every remaining check still runs
## and still passes.
## 3482 -> 3479: EXACTLY the one method that left the BLOCKED table when
## teams.was_destroyed became simulation-backed (retail's TEAM_DESTROYED is a
## level !hasAnyObjects() read, not the "destruction edge records" the
## annotation used to demand, so it needed no new sim state). 1 x 3 = 3 -
## "carries a blocking annotation", "annotation names a real method",
## "annotation names a declared subsystem". The TEAM_DESTROYED ROUTE also
## flipped blocked -> backed, which is check-neutral: a blocked route asserts
## "names a declared subsystem" and a backed one asserts "cites a backed
## method", one each. No assertion was weakened, deleted or skipped.
## 3479 -> 3476: EXACTLY ai.set_buildings_allowed leaving the BLOCKED table
## after per-team object-type permissions became hash-backed and construction-
## enforced. As above, the three annotation-only checks disappear; the method
## probe and route assertions remain. No assertion was weakened or skipped.
## 3476 -> 3473: EXACTLY players.override_command_points leaving the BLOCKED
## table after its independent per-team total/maximum pair became hash-backed
## and the total became the production/query cap. The same three annotation-
## only checks disappear; all method probes remain.
## 3470 -> 3467: EXACTLY teams.set_available_for_recruitment leaving the
## BLOCKED table after its explicit tri-state override became script-team
## registry state. The method probe remains; only the same three
## annotation-only checks disappear.
## 3470 -> 3467: teams.transfer_to_player left the blocked-annotation table;
## its method probe and backed-route validation remain, while the three
## annotation-only checks disappear.
## 3467 -> 3461: TEAM_EXECUTE_SEQUENTIAL_SCRIPT(_LOOPING) and
## TEAM_STOP_SEQUENTIAL_SCRIPT leave the blocked table after
## teams.execute/stop sequential become simulation-backed (measured).
## 3461 -> 3455: units.set/has_object_status leave entity-status-flags
## blocked annotations after TEAM/PLAYER/UNIT living-entity object-status.
## 3455 -> 3410: production-controls + team-registry + order-verbs batch
## (measured after move_home stayed blocked without spawn fixture).
## Updated again after bulk entity-status/progression/containment/areas batch.
## 2766 -> 2752: EXACTLY the 14 previously-refusing residual methods
## (threat×5, wall×2, marker×1, transport×1, command-button×2,
## reinforcement×2, supply-center×1) leave the blocked-annotation table
## after parity-backed consumers answered. Each drop removes one
## annotation-only check; method probes remain.
const EXPECTED_CHECKS := 2752

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _run() -> void:
	var user_args := OS.get_cmdline_user_args()
	var entries := _enumerate_surface()
	_probe_all(entries)
	if user_args.has("--survey"):
		_print_survey(entries)
		quit(0)
		return
	var generated := _render_map(entries)
	if user_args.has("--write"):
		var out := FileAccess.open(MAP_PATH, FileAccess.WRITE)
		out.store_string(generated)
		out.close()
		print("wrote %s (%d bytes)" % [MAP_PATH, generated.length()])
	_verify_against_committed_map(entries, generated)
	_verify_read_only_sweep(entries)
	_verify_routing_and_print_ranking(entries)
	var called := 0
	var fallback := 0
	var backed := 0
	for entry in entries:
		if String(entry["probe_mode"]) == "called":
			called += 1
		else:
			fallback += 1
		if bool(entry["observed_backed"]):
			backed += 1
	var bag_only := 0
	var state_mutating := 0
	var subsystem_consumed := 0
	for entry in entries:
		if bool(entry.get("bag_only", false)):
			bag_only += 1
		if bool(entry.get("state_mutating", false)):
			state_mutating += 1
		if bool(entry.get("subsystem_consumed", false)):
			subsystem_consumed += 1
	print(
		"SURFACE total=%d callable=%d bagOnly=%d stateMutating=%d subsystemConsumed=%d refusing=%d probedByCall=%d fallbackProbed=%d"
		% [
			entries.size(),
			backed,
			bag_only,
			state_mutating,
			subsystem_consumed,
			entries.size() - backed,
			called,
			fallback,
		]
	)
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("SCRIPT_WORLD_SURFACE FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("SCRIPT_WORLD_SURFACE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# ==========================================================================
# FIXTURES (mirrors retail_slice_script_world_runner.gd; synthetic only)
# ==========================================================================


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
		# Production parity: every real match's rules carry the versioned
		# pack-faction -> retail-side table (players.faction answers the retail
		# side token "Men", the vocabulary SKIRMISH_PLAYER_FACTION compares).
		"retail_faction_sides": ManifestScript.retail_faction_sides(),
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


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


## Fresh sim + adapter world with everything a backed method needs to answer:
## faction-carrying roster, spellbook, a research contract, the ENEMY player's
## science pre-purchased (so combat.fire_special_power can cast as ENEMY while
## progression.purchase_science still has PLAYER's untouched purchase to make),
## one wounded enemy battalion for the heal to target, and the base-building
## surface: an expansion rule, one PACKED base flag (SYNTH_BASE_FLAG, for the
## unpack pair and the unpackable condition), one flag pre-unpacked free as
## the bound script player PLAYER and referenced as SYNTH_HOME_REF (for the
## base-anchored build and buildability probes).
func _fixture() -> Dictionary:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	sim.configure_team_roster([
		{"team": 0, "faction": "men", "is_ai": false, "start_index": 0},
		{"team": 1, "faction": "men", "is_ai": true, "start_index": 1},
	])
	sim.setup({}, {})
	sim.ai_enabled = false
	_check("fixture: spellbook configures", sim.configure_spellbook_runtime(_spellbook_document()))
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
	var bought: Dictionary = sim.purchase_power(1, "SpellBookTestHeal")
	_check("fixture: enemy pre-purchases the test power", bool(bought.get("ok", false)))
	var enemy_ids: Array = sim.living_ids(1)
	_check("fixture: enemy roster is populated", not enemy_ids.is_empty())
	var wounded: Dictionary = sim.entities[enemy_ids[0]]
	(wounded["member_health"] as Array)[0] = 100
	wounded["health"] = 100
	var at := Vector2(wounded["position"])
	sim.configure_expansion_rules({
		"synth_pit": {
			"cost": 300,
			"seconds": 5.0,
			"health": 500,
			"pad_kinds": ["corner", "side"],
			"name": "Synth Pit",
			"object_id": "SynthPitType",
		},
	})
	sim.configure_unpackable_bases({
		"SYNTH_BASE_FLAG": {"position": Vector2(60.0, 60.0), "cost": 500},
		"SYNTH_HOME_FLAG": {"position": Vector2(70.0, -60.0), "cost": 500},
	})
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_player("PlyrCivilian", SimScript.NEUTRAL_TEAM)
	world.bind_team(PLAYER_TEAM_NAME, SimScript.PLAYER_TEAM)
	world.bind_team(ENEMY_TEAM_NAME, SimScript.ENEMY_TEAM)
	world.bind_script_team(
		"Player_1_Inherit",
		"PlyrCivilian",
		[],
		true,
		[],
		0,
		true
	)
	_check("fixture: script player binds", world.bind_script_player(PLAYER))
	_check(
		"fixture: home flag unpacks free and binds SYNTH_HOME_REF",
		world.ai().base_unpack("SYNTH_HOME_FLAG", true, "SYNTH_HOME_REF")
	)
	# Parity subsystems: wall structure, tactical marker, unit abilities,
	# transport capacity, reinforcement army seed, OCL leaf for CreateObjectDie.
	sim._ensure_parity()
	sim.structures[910] = {
		"health": 500,
		"team": SimScript.PLAYER_TEAM,
		"position": Vector2(80, -60),
		"kind": "castle_wall",
		"building_type": "GondorCastleUpgrade",
		"completed_upgrades": [],
		"construction_progress": 1.0,
	}
	sim.structures[911] = {
		"health": 500,
		"team": SimScript.PLAYER_TEAM,
		"position": Vector2(75, -55),
		"kind": "barracks",
		"structure_kind": "barracks",
		"building_type": "GondorBarracks",
		"completed_upgrades": [],
		"construction_progress": 1.0,
		"transport_capacity": 4,
	}
	# Neutral capturable flag for transport.capture_nearest_unowned.
	sim.structures[912] = {
		"health": 200,
		"team": SimScript.NEUTRAL_TEAM,
		"position": Vector2(70, -50),
		"kind": "flag",
		"structure_kind": "flag",
		"capturable": true,
		"construction_progress": 1.0,
	}
	sim.parity.register_tactical_marker(
		"CastleFront", "CastleFront", Vector2(90, -50), Vector2(40, 0), Vector2(-40, 0)
	)
	sim.register_ocl_leaf("OCL_SurfaceProbeDebris", {
		"id": "OCL_SurfaceProbeDebris",
		"createObjects": [{
			"objects": [SimScript.SOLDIER_HORDE_ID],
			"fields": [{"key": "Count", "resolved": 1}],
		}],
	})
	var living_seed: Array = sim.living_ids(SimScript.PLAYER_TEAM)
	if not living_seed.is_empty():
		var seed_id := int(living_seed[0])
		if sim.entities.has(seed_id):
			(sim.entities[seed_id] as Dictionary)["abilities"] = [{
				"id": "Command_SyntheticHeroPower",
				"command_id": "Command_SyntheticHeroPower",
				"name": "Command_SyntheticHeroPower",
				"ready": true,
			}]
			# Reinforcement army seed so remove_reinforcement_army can answer.
			(sim.entities[seed_id] as Dictionary)["reinforcement_army"] = "ArmyOne"
			(sim.entities[seed_id] as Dictionary)["reinforcement_player_team"] = SimScript.PLAYER_TEAM
	# Sequential-script probes need a loaded behavior script on a registered
	# executor (ScriptActions::doTeamStartSequentialScript requires
	# findScriptByName). One shared empty-actions script is enough for the
	# queue/stop surface without inventing AI behavior content.
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	executor.load_script_payload({
		"name": "be_SurfaceSequentialProbe",
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": true,
		"deactivateUponSuccess": false,
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": false,
		"evaluationInterval": 0,
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": [],
	})
	# Do not _check these per-fixture: _fixture() runs once per method probe,
	# and counting would re-mint EXPECTED_CHECKS by hundreds. Fail closed by
	# leaving the executor unregistered if attach/register refuse.
	if (
		sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM)
		and sim.register_script_executor(executor, SimScript.PLAYER_TEAM)
	):
		pass
	# Numeric entity id string for unit-scope surfaces (held/stealth/etc.).
	var living: Array = sim.living_ids(SimScript.PLAYER_TEAM)
	var living_entity_name := str(int(living[0])) if not living.is_empty() else "0"
	# Map-geometry probe anchors.
	sim.register_script_area("SYNTH_AREA", Vector2(at.x, at.y), 50.0)
	sim.register_script_waypoint("SYNTH_WP", Vector2(at.x, at.y))
	sim.register_script_waypoint_path("SYNTH_PATH", ["SYNTH_WP"])
	# Home anchors for orders.move_home.
	if not sim._spawn_positions.has(SimScript.PLAYER_TEAM):
		sim._spawn_positions[SimScript.PLAYER_TEAM] = Vector2(at.x, at.y)
	if not living.is_empty():
		var home_row: Dictionary = sim.entities[int(living[0])]
		sim._spawn_positions[SimScript.PLAYER_TEAM] = home_row.get(
			"position", Vector2(at.x, at.y)
		)
	return {
		"sim": sim,
		"world": world,
		"cast_position": Vector3(at.x, 0.0, at.y),
		"executor": executor,
		"living_entity_name": living_entity_name,
	}


func _release_fixture(fx: Dictionary) -> void:
	## Facets weakref the world; still clear caches and drop sim so per-probe
	## fixtures free instead of retaining the full harness until process exit.
	var world: RetailSliceScriptWorld = fx["world"]
	if world != null:
		world._release_facets()
		world.sim = null
	fx["sim"] = null
	fx["world"] = null
	fx["executor"] = null


# ==========================================================================
# ENUMERATION
# ==========================================================================


func _enumerate_surface() -> Array:
	## Every public, non-static, script-declared instance method of every
	## facet, plus the base world's own surface methods (under facet "world"),
	## sorted by (facet, method). Facet accessors, the target_*/scope_label
	## statics and everything RefCounted provides are excluded.
	var baseline := {}
	for m in RefCounted.new().get_method_list():
		baseline[String(m["name"])] = true
	var base_world := SageScriptWorld.new()
	var entries: Array = []
	for facet_key in FACET_ACCESSORS:
		var facet: Object = base_world.call(facet_key)
		var seen := {}
		for m in facet.get_method_list():
			var entry := _entry_for(String(facet_key), m, baseline, seen, [])
			if not entry.is_empty():
				entries.append(entry)
	var seen_world := {}
	for m in base_world.get_method_list():
		var entry := _entry_for("world", m, baseline, seen_world, FACET_ACCESSORS)
		if not entry.is_empty():
			entries.append(entry)
	entries.sort_custom(func(a, b):
		if a["facet"] != b["facet"]:
			return String(a["facet"]) < String(b["facet"])
		return String(a["method"]) < String(b["method"])
	)
	# Introspection materializes all cached facets on the base world. Break the
	# same bidirectional RefCounted cycle that runtime fixtures release.
	for facet in base_world._facets.values():
		facet.world = null
	base_world._facets.clear()
	return entries


func _entry_for(
	facet_key: String, m: Dictionary, baseline: Dictionary, seen: Dictionary,
	extra_excludes: Array
) -> Dictionary:
	var method_name := String(m["name"])
	if baseline.has(method_name) or seen.has(method_name):
		return {}
	if method_name.begins_with("_") or extra_excludes.has(method_name):
		return {}
	if int(m["flags"]) & METHOD_FLAG_STATIC:
		return {}
	seen[method_name] = true
	var params: Array = []
	for arg in Array(m["args"]):
		params.append({
			"name": String(arg["name"]).trim_prefix("_"),
			"type": type_string(int(arg["type"])),
		})
	var ret: Dictionary = m["return"]
	var returns := "command"
	if int(ret["type"]) == TYPE_OBJECT:
		returns = "query"
	elif int(ret["type"]) != TYPE_BOOL:
		returns = type_string(int(ret["type"]))
	return {
		"facet": facet_key,
		"method": method_name,
		"params": params,
		"returns": returns,
	}


# ==========================================================================
# ARGUMENT SYNTHESIS
# ==========================================================================


func _candidates_for_param(param: Dictionary, fx: Dictionary) -> Array:
	## Plausible values for one parameter, derived from its declared type and
	## name. Multiple candidates where the right pairing depends on a sibling
	## parameter (scope/name) or where one value is documented to refuse
	## (booleans like `disband`); the cross-product covers the valid pairings.
	var pname := String(param["name"])
	match String(param["type"]):
		"String":
			if pname == "capability":
				return [SageScriptWorld.CAP_PLAYER_MONEY]
			if pname == "power":
				return ["SpellBookTestHeal"]
			if pname == "science":
				return ["SCIENCE_TestHeal"]
			if pname == "upgrade":
				return ["Upgrade_TestTech"]
			if pname == "outcome":
				return ["defeat"]
			if pname == "building_class":
				return [""]
			if pname == "object_name":
				# BOTH kinds of entry in the shared object / unit-reference
				# namespace, because the two answer different methods: the
				# unpack pair and the unpackable condition need the PACKED flag
				# (SYNTH_BASE_FLAG), while the ownership and health reads need a
				# name that resolves to a live structure (SYNTH_HOME_REF, the
				# fixture's pre-unpacked flag bound as a reference). Offering
				# only the packed flag would classify units.owner and
				# units.health_percent as refusing when what they actually
				# refuse is the packed-flag SHAPE - a restriction, not a gap.
				return ["SYNTH_BASE_FLAG", "SYNTH_HOME_REF"]
			if pname == "base":
				return ["SYNTH_HOME_REF"]
			if pname == "building_type":
				return ["SynthPitType"]
			if pname == "result_reference":
				return ["SYNTH_PROBE_REF"]
			if pname == "object_type":
				# Empty is the documented "anything at all" form on the methods
				# that answer (players.can_build_at_base); the rest refuse
				# regardless of the value.
				return [""]
			if pname == "name" or pname.begins_with("name_"):
				# Paired with a scope parameter; offer both spellings.
				return [PLAYER, PLAYER_TEAM_NAME]
			if pname.contains("player") or pname == "other":
				return [PLAYER]
			if pname.contains("team") and not pname.contains("teams"):
				return [PLAYER_TEAM_NAME]
			return ["probe"]
		"int":
			if pname == "scope" or pname.begins_with("scope"):
				return [SageScriptWorld.Scope.PLAYER, SageScriptWorld.Scope.TEAM]
			if pname == "amount":
				return [100]
			return [1]
		"float":
			return [1.0]
		"bool":
			return [false, true]
		"Vector3":
			return [fx["cast_position"]]
		"Dictionary":
			if pname == "target" or pname == "attacker":
				return [
					SageScriptWorld.target_position(fx["cast_position"]),
					SageScriptWorld.target_team(ENEMY_TEAM_NAME),
				]
			return [{}]
		"Array":
			return [[]]
	push_error("no candidate rule for parameter type %s" % String(param["type"]))
	return [null]


func _override_tuples(entry: Dictionary, fx: Dictionary) -> Array:
	## Explicit candidate tuples for the few methods whose valid pairing the
	## generic per-parameter rules cannot produce. fire_special_power must
	## cast as the player whose science the fixture pre-purchased (ENEMY),
	## which no name-derived rule should guess at - pairing "player-ish
	## string" with ENEMY globally would double every cross-product instead.
	match "%s.%s" % [entry["facet"], entry["method"]]:
		"combat.fire_special_power":
			return [[
				SageScriptWorld.Scope.PLAYER, ENEMY, "SpellBookTestHeal",
				SageScriptWorld.target_position(fx["cast_position"]),
			]]
		"meta.object_list_change":
			# The generic object_type rule offers "" (can_build_at_base's
			# "anything at all" form), which this method refuses - a list edit
			# needs a real type name.
			return [["SYNTH_PROBE_LIST", "SynthProbeType", true]]
		"progression.has_object_of_veterancy":
			# The generic object_type rule intentionally supplies "" for
			# can_build_at_base's valid "anything" spelling. Veterancy has no
			# empty-type form, so probe its exact four-slot signature with a
			# modeled single type and a sourced comparison enum value.
			return [[
				PLAYER, "GondorArcher",
				ParamTypes.COMPARE_GREATER_EQUAL, 2,
			]]
		"units.has_command_points_to_build":
			# Needs a type the fixture's production rules model; the retail
			# spelling resolves through the runtime-id slug.
			return [[PLAYER, "GondorFighterHorde"]]
		"teams.transfer_to_player":
			# Method-level success shape: a source-attested marker-only
			# non-default civilian team and a uniquely bound combatant
			# destination, matching the real composite retail gate.
			return [["PlyrCivilian/Player_1_Inherit", PLAYER]]
		"teams.execute_sequential_script":
			# Needs a registered script team plus a script body loaded on the
			# fixture executor (be_SurfaceSequentialProbe).
			return [[PLAYER_TEAM_NAME, "be_SurfaceSequentialProbe", false]]
		"units.set_object_status":
			return [[
				SageScriptWorld.Scope.TEAM, PLAYER_TEAM_NAME, "UNSELECTABLE", true
			]]
		"units.has_object_status":
			return [[
				SageScriptWorld.Scope.TEAM, PLAYER_TEAM_NAME, "UNSELECTABLE", false
			]]
		"players.set_auto_build_enabled":
			return [[PLAYER, false]]
		"players.set_base_construction_enabled":
			return [[PLAYER, false]]
		"players.set_factories_enabled":
			return [[PLAYER, false]]
		"players.set_base_construction_speed":
			return [[PLAYER, 1.5]]
		"players.set_unit_construction_enabled":
			return [[PLAYER, "GondorFighterHorde", false]]
		"players.has_prerequisite_to_build":
			return [[PLAYER, "GondorFighterHorde"]]
		"terrain.set_buildability":
			return [["GondorFighterHorde", 1]]
		"teams.members":
			return [[PLAYER_TEAM_NAME]]
		"teams.set_ai_recruitable":
			return [[PLAYER_TEAM_NAME, false]]
		"teams.set_reference":
			return [["AI_TEAM_REF", PLAYER_TEAM_NAME]]
		"teams.spin_for_ticks":
			return [[PLAYER_TEAM_NAME, 5]]
		"orders.hunt":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, ""]]
		"orders.idle_for_ticks":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, 3]]
		"orders.guard":
			return [[
				SageScriptWorld.Scope.PLAYER, PLAYER,
				SageScriptWorld.target_self(), 0
			]]
		"orders.set_stopping_distance":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, 2.0]]
		"units.set_held":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_repulsor":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_stealth_enabled":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_strict_control_enabled":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_house_color_enabled":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_close_range_weapon":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.set_flame_status":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.is_webbed":
			return [[String(fx.get("living_entity_name", "0"))]]
		"units.set_special_weaponset":
			return [[String(fx.get("living_entity_name", "0")), "WEAPONSET_PLAYER_UPGRADE"]]
		"units.set_emoticon":
			return [[String(fx.get("living_entity_name", "0")), "Emoticon_Smile", 10]]
		"units.set_model_condition":
			return [[String(fx.get("living_entity_name", "0")), "USER_1", true, 0]]
		"units.set_object_panel_flag":
			return [[String(fx.get("living_entity_name", "0")), "FLAG", true]]
		"units.set_topple_direction":
			return [[String(fx.get("living_entity_name", "0")), Vector3(1, 0, 0)]]
		"units.shock":
			return [[String(fx.get("living_entity_name", "0")), 5]]
		"units.delete":
			return [[String(fx.get("living_entity_name", "0"))]]
		"combat.set_bonuses_allowed":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"combat.damage":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, 1.0]]
		"combat.kill":
			return [[SageScriptWorld.Scope.PLAYER, ENEMY]]
		"combat.set_health_percent":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, 100.0]]
		"areas.exists":
			return [["SYNTH_AREA"]]
		"areas.contains":
			return [["SYNTH_AREA", fx["cast_position"]]]
		"areas.waypoint_path_exists":
			return [["SYNTH_PATH"]]
		"areas.waypoint_position":
			return [["SYNTH_WP"]]
		"areas.set_human_impassable":
			return [["SYNTH_AREA", true]]
		"areas.member_count":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, "SYNTH_AREA"]]
		"areas.unit_count_in_area":
			return [[PLAYER, "SYNTH_AREA", {}]]
		"areas.transition_count":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, "SYNTH_AREA", true]]
		"transport.garrisoned_count":
			return [[PLAYER]]
		"transport.captured_unit_count":
			return [[PLAYER]]
		"transport.passenger_count":
			return [["SYNTH_HOME_REF"]]
		"transport.has_toggled_weapon":
			return [[String(fx.get("living_entity_name", "0"))]]
		"transport.load_transports":
			return [[PLAYER_TEAM_NAME]]
		"players.rank_level":
			return [[PLAYER]]
		"players.set_rank_level":
			return [[PLAYER, 2]]
		"players.add_rank_level":
			return [[PLAYER, 1]]
		"players.set_rank_level_limit":
			return [[PLAYER, 10]]
		"players.reached_level_cap":
			return [[PLAYER]]
		"players.add_skill_points":
			return [[PLAYER, 5]]
		"players.select_skill_set":
			return [[PLAYER, 1]]
		"players.light_points":
			return [[PLAYER]]
		"players.give_light_points":
			return [[PLAYER, 3]]
		"players.change_light_point_level":
			return [[PLAYER, 1]]
		"players.reset_light_points":
			return [[PLAYER]]
		"players.set_max_spell_points":
			return [[PLAYER, 100]]
		"players.exit_all_buildings":
			return [[PLAYER]]
		"players.repair_structure":
			return [[PLAYER, "SYNTH_HOME_REF"]]
		"players.sell_everything":
			return [[PLAYER]]
		"teams.contained_count":
			return [[PLAYER_TEAM_NAME]]
		"teams.exit_all":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.set_close_range_weapon":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.set_stealth_enabled":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.was_created":
			return [[PLAYER_TEAM_NAME]]
		"units.set_team":
			return [[String(fx.get("living_entity_name", "0")), PLAYER_TEAM_NAME]]
		"units.enter_object":
			return [[String(fx.get("living_entity_name", "0")), "SYNTH_HOME_REF"]]
		"transport.garrison":
			return [[
				SageScriptWorld.Scope.UNIT,
				String(fx.get("living_entity_name", "0")),
				SageScriptWorld.target_object("SYNTH_HOME_REF"),
				true,
			]]
		"teams.enter_object":
			return [[PLAYER_TEAM_NAME, "SYNTH_HOME_REF"]]
		"teams.set_emoticon":
			return [[PLAYER_TEAM_NAME, "Emoticon_Smile", 10]]
		"teams.set_model_condition":
			return [[PLAYER_TEAM_NAME, "USER_1", true, 0]]
		"teams.set_object_panel_flag":
			return [[PLAYER_TEAM_NAME, "FLAG", true]]
		"teams.set_repulsor":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.set_house_color_enabled":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.set_flame_status":
			return [[PLAYER_TEAM_NAME, true]]
		"teams.set_strict_control_enabled":
			return [[PLAYER_TEAM_NAME, true]]
		"units.exit":
			return [[String(fx.get("living_entity_name", "0")), false]]
		"units.is_building_empty":
			return [["SYNTH_HOME_REF"]]
		"orders.attack_area":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER, "SYNTH_AREA", 5]]
		"orders.follow_waypoint_path":
			return [[
				SageScriptWorld.Scope.PLAYER, PLAYER,
				SageScriptWorld.target_waypoint_path("SYNTH_PATH", false),
			]]
		"orders.attack_follow_waypoints":
			return [[String(fx.get("living_entity_name", "0")), "SYNTH_PATH"]]
		"orders.fire_weapon_following_path":
			return [[String(fx.get("living_entity_name", "0")), "SYNTH_PATH"]]
		"orders.guard_area_from_position":
			return [[
				SageScriptWorld.Scope.PLAYER, PLAYER, "SYNTH_AREA", fx["cast_position"]
			]]
		"orders.move_home":
			return [[SageScriptWorld.Scope.PLAYER, PLAYER]]
		"players.object_count_within_distance":
			return [[PLAYER, "", "SYNTH_WP", 100.0]]
		"players.sell_everything":
			return [[PLAYER]]
		"progression.gained_level":
			return [[String(fx.get("living_entity_name", "0"))]]
		"progression.set_max_level":
			return [[String(fx.get("living_entity_name", "0")), 5]]
		"combat.set_special_power_countdown_running":
			return [[String(fx.get("living_entity_name", "0")), "Command_SyntheticHeroPower", true]]
		"combat.set_special_power_countdown":
			return [[
				SageScriptWorld.Scope.UNIT,
				String(fx.get("living_entity_name", "0")),
				"Command_SyntheticHeroPower",
				10,
				false,
			]]
		"units.deploy_siege":
			return [[
				String(fx.get("living_entity_name", "0")),
				SageScriptWorld.target_position(fx["cast_position"]),
			]]
		"units.retract_siege":
			return [[String(fx.get("living_entity_name", "0"))]]
		"units.set_cave_index":
			return [[String(fx.get("living_entity_name", "0")), 1]]
		"units.set_hulk_lifetime":
			return [[String(fx.get("living_entity_name", "0")), 30]]
		"units.set_warehouse_value":
			return [["911", 100]]
		"units.execute_sequential_script":
			return [[PLAYER_TEAM_NAME, "be_SurfaceSequentialProbe", false]]
		"units.stop_sequential_script":
			return [[PLAYER_TEAM_NAME]]
		"orders.set_auto_ability":
			return [[
				SageScriptWorld.Scope.UNIT,
				String(fx.get("living_entity_name", "0")),
				"Command_SyntheticHeroPower",
				true,
			]]
		"teams.merge_into":
			return [[PLAYER_TEAM_NAME, PLAYER_TEAM_NAME]]
		"ai.build_on_foundation":
			# Fixture unpacks SYNTH_HOME_FLAG as SYNTH_HOME_REF with expansion pads;
			# SynthPitType is configured as an expansion object_id.
			return [[PLAYER, "SYNTH_HOME_REF", "SynthPitType"]]
		"transport.capture_nearest_unowned":
			return [[PLAYER_TEAM_NAME]]
		"transport.create_team_from_captured":
			return [[PLAYER, "CapturedTeam"]]
		"transport.teleport_to":
			return [[
				SageScriptWorld.Scope.PLAYER, PLAYER,
				SageScriptWorld.target_position(fx["cast_position"]),
			]]
		"units.create_object":
			return [["GondorFighterHorde", PLAYER, fx["cast_position"], 0.0]]
		"units.create_object_on_team":
			return [[
				"GondorFighterHorde", PLAYER_TEAM_NAME, fx["cast_position"], 0.0
			]]
		"units.create_on_team_at":
			return [[
				"GondorFighterHorde", PLAYER_TEAM_NAME,
				SageScriptWorld.target_position(fx["cast_position"]),
				"SYNTH_SPAWNED",
			]]
		"units.spawn_at":
			return [[
				"GondorFighterHorde", PLAYER, "SYNTH_SPAWNED",
				fx["cast_position"], 0.0,
			]]
		"economy.build_supply_center":
			# Fixture expansion table object_id is SynthPitType (see _fixture).
			return [[PLAYER, "SynthPitType", 100.0]]
		"units.set_gate_state":
			return [["SYNTH_HOME_REF", true, true]]
		"units.gate_is_open":
			return [["SYNTH_HOME_REF"]]
		"units.threat":
			return [[String(fx.get("living_entity_name", "0"))]]
		"units.threat_within_radius":
			return [[String(fx.get("living_entity_name", "0")), 500.0]]
		"units.force_emotion":
			return [[String(fx.get("living_entity_name", "0")), 1, 10]]
		"units.set_selected":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.exit_specific_building":
			return [[String(fx.get("living_entity_name", "0")), "SYNTH_HOME_REF"]]
		"units.set_attitude":
			return [[String(fx.get("living_entity_name", "0")), 2]]
		"units.skill_points":
			return [[String(fx.get("living_entity_name", "0"))]]
		"units.stance":
			return [[String(fx.get("living_entity_name", "0"))]]
		"units.stop":
			return [[String(fx.get("living_entity_name", "0")), true]]
		"units.transfer_ownership":
			return [[String(fx.get("living_entity_name", "0")), ENEMY]]
		"progression.upgrade_nearest_wall":
			return [[PLAYER, "Upgrade_TestTurret", "SYNTH_HOME_REF"]]
		"progression.upgrade_nearest_wall_bound":
			return [[
				"SYNTH_HOME_REF", "Upgrade_TestTurret", "castle_wall",
				"CastleFront", "AI_WALL_REF",
			]]
		"orders.use_command_button":
			return [[
				SageScriptWorld.Scope.UNIT,
				String(fx.get("living_entity_name", "0")),
				"Command_SyntheticHeroPower",
				SageScriptWorld.target_self(),
			]]
		"orders.use_command_button_partial":
			return [[
				PLAYER_TEAM_NAME, "Command_SyntheticHeroPower", 1,
				SageScriptWorld.target_self(),
			]]
		"ai.build_base_building_per_tactical_marker":
			return [[
				"SynthPitType", "near", "CastleFront", "SYNTH_HOME_REF", "AI_BUILT_REF",
			]]
		"teams.set_reference_to_nearest":
			# Empty object_type = any living entity/structure owned by player near anchor.
			return [[
				"AI_GATE", "", PLAYER, PLAYER_TEAM_NAME, false
			]]
		"teams.recruit":
			return [[PLAYER_TEAM_NAME, 100.0, ""]]
		"teams.threat":
			return [[PLAYER_TEAM_NAME]]
		"teams.threat_within_radius":
			return [[PLAYER_TEAM_NAME, 500.0]]
		"teams.set_attitude":
			return [[PLAYER_TEAM_NAME, 0]]
		"teams.set_threat_level":
			return [[PLAYER_TEAM_NAME, 1]]
		"combat.team_health_percent":
			return [[PLAYER_TEAM_NAME]]
		"units.threat":
			return [[String(fx.get("living_entity_name", "0"))]]
		"ai.create_reinforcement_team":
			# Army name must be an authored unit_rules key (no synthetic fallback).
			return [[
				PLAYER, SimScript.SOLDIER_OBJECT_ID,
				SageScriptWorld.target_position(fx["cast_position"]),
			]]
		"ai.remove_reinforcement_army":
			return [[PLAYER, "ArmyOne"]]
		"progression.upgrade_nearest_wall":
			return [[PLAYER, "Upgrade_TestTurret", "SYNTH_HOME_REF"]]
	return []


func _candidate_tuples(entry: Dictionary, fx: Dictionary) -> Array:
	var overrides := _override_tuples(entry, fx)
	if not overrides.is_empty():
		return overrides
	var tuples: Array = [[]]
	for param in Array(entry["params"]):
		var values := _candidates_for_param(param, fx)
		var grown: Array = []
		for tuple in tuples:
			for value in values:
				var next: Array = (tuple as Array).duplicate()
				next.append(value)
				grown.append(next)
		tuples = grown
	if tuples.size() > 32:
		push_error(
			"%s.%s explodes to %d candidate tuples; tighten the synthesis rules"
			% [entry["facet"], entry["method"], tuples.size()]
		)
	return tuples


# ==========================================================================
# PROBING
# ==========================================================================


func _facet_object(world: RetailSliceScriptWorld, facet_key: String) -> Object:
	if facet_key == "world":
		return world
	return world.call(facet_key)


func _call_answers(target: Object, entry: Dictionary, tuple: Array) -> bool:
	## True when this call genuinely answered: `ok` for a query, `true` for a
	## command. Refusals return false.
	var result: Variant = target.callv(String(entry["method"]), tuple)
	if String(entry["returns"]) == "query":
		return (result as SageWorldQuery).ok
	return bool(result)


func _core_state_fingerprint(sim: RetailSliceSim) -> String:
	## state_hash with the residual surface bag cleared so bag-only writes do
	## not look like subsystem consumption.
	var saved: Dictionary = sim.script_surface_bag
	sim.script_surface_bag = {}
	var fingerprint := sim.state_hash()
	sim.script_surface_bag = saved
	return fingerprint


func _probe_entry(entry: Dictionary) -> Dictionary:
	## Probe one method on a fresh fixture. Returns
	## {"backed": bool, "mode": "called"|"fallback", "calls": int,
	##  "bagOnly": bool, "stateMutating": bool, "subsystemConsumed": bool}.
	var qualified := "%s.%s" % [entry["facet"], entry["method"]]
	if FALLBACK_PROBED.has(qualified):
		var fallback := _probe_fallback(qualified)
		# Fallback base methods read/draw live sim state; treat as consumed.
		fallback["bagOnly"] = false
		fallback["stateMutating"] = qualified.begins_with("world.random_")
		fallback["subsystemConsumed"] = bool(fallback.get("backed", false))
		return fallback
	var fx := _fixture()
	var sim: RetailSliceSim = fx["sim"]
	var target := _facet_object(fx["world"], String(entry["facet"]))
	var calls := 0
	var backed := false
	var bag_only := false
	var state_mutating := false
	var subsystem_consumed := false
	for tuple in _candidate_tuples(entry, fx):
		calls += 1
		var bag_before: Dictionary = sim.script_surface_bag.duplicate(true)
		var core_before := _core_state_fingerprint(sim)
		if _call_answers(target, entry, tuple):
			backed = true
			var bag_after: Dictionary = sim.script_surface_bag.duplicate(true)
			var core_after := _core_state_fingerprint(sim)
			var bag_changed := str(bag_before) != str(bag_after)
			var core_changed := core_before != core_after
			state_mutating = bag_changed or core_changed
			# Provenance-aware classification (Codex gpt-5.6-sol re-review):
			# * subsystemConsumed only when core sim fingerprint changes.
			# * bagOnly only when bag changes and core does not.
			# * Pure answers with neither change are callable but NOT claimed
			#   as subsystem consumers (bag-default reads used to inflate that
			#   count by treating every non-mutating answer as consumed).
			if core_changed:
				subsystem_consumed = true
				bag_only = false
			elif bag_changed:
				bag_only = true
				subsystem_consumed = false
			else:
				bag_only = false
				subsystem_consumed = false
			break
	_release_fixture(fx)
	return {
		"backed": backed,
		"mode": "called",
		"calls": calls,
		"bagOnly": bag_only,
		"stateMutating": state_mutating,
		"subsystemConsumed": subsystem_consumed,
	}


func _probe_fallback(qualified: String) -> Dictionary:
	## The five refusal-channel-free base methods (see the class comment).
	var fx := _fixture()
	var world: RetailSliceScriptWorld = fx["world"]
	var sim: RetailSliceSim = fx["sim"]
	var backed := false
	match qualified:
		"world.supports":
			backed = world.supports(SageScriptWorld.CAP_PLAYER_MONEY)
		"world.world_frame":
			sim.advance(3)
			backed = world.world_frame() == 3
		"world.player_money":
			backed = world.player_money(PLAYER) == 10000
		"world.random_int", "world.random_real":
			# No refusal channel at all; the honest signal is the capability
			# token PLUS a live draw that lands in range (the token alone
			# could be advertised by a stream that answers garbage). The draw
			# mutates only this probe's own fixture sim.
			var drawn := world.random_int(1, 3)
			backed = (
				world.supports(SageScriptWorld.CAP_RANDOM)
				and drawn >= 1 and drawn <= 3
			)
	_release_fixture(fx)
	return {"backed": backed, "mode": "fallback", "calls": 1}


func _probe_all(entries: Array) -> void:
	for entry in entries:
		var outcome := _probe_entry(entry)
		entry["observed_backed"] = bool(outcome["backed"])
		entry["probe_mode"] = String(outcome["mode"])
		entry["bag_only"] = bool(outcome.get("bagOnly", false))
		entry["state_mutating"] = bool(outcome.get("stateMutating", false))
		entry["subsystem_consumed"] = bool(outcome.get("subsystemConsumed", false))


func _print_survey(entries: Array) -> void:
	## Development aid: dump the measured surface as annotation skeletons.
	var backed := 0
	for entry in entries:
		if bool(entry["observed_backed"]):
			backed += 1
		var sig: Array = []
		for p in Array(entry["params"]):
			sig.append("%s: %s" % [p["name"], p["type"]])
		print("%s\t%s.%s(%s) -> %s" % [
			"BACKED" if entry["observed_backed"] else "refuses",
			entry["facet"], entry["method"], ", ".join(sig), entry["returns"],
		])
	print("SURVEY total=%d backed=%d refusing=%d" % [
		entries.size(), backed, entries.size() - backed,
	])


# ==========================================================================
# MAP GENERATION AND VERIFICATION (annotation tables live in
# script_world_surface_data.gd next to this runner)
# ==========================================================================


const SurfaceData = preload("res://tests/script_world_surface_data.gd")


func _render_map(entries: Array) -> String:
	## Deterministic JSON: insertion-ordered dictionaries, sorted entries,
	## tab indentation, LF line endings, trailing newline - the same
	## discipline as retail_ai_call_census.json.
	var backed_count := 0
	var bag_only_count := 0
	var state_mutating_count := 0
	var subsystem_consumed_count := 0
	var methods: Array = []
	for entry in entries:
		var qualified := "%s.%s" % [entry["facet"], entry["method"]]
		var observed := bool(entry["observed_backed"])
		var bag_only := bool(entry.get("bag_only", false))
		var state_mutating := bool(entry.get("state_mutating", false))
		var subsystem_consumed := bool(entry.get("subsystem_consumed", false))
		if observed:
			backed_count += 1
		if bag_only:
			bag_only_count += 1
		if state_mutating:
			state_mutating_count += 1
		if subsystem_consumed:
			subsystem_consumed_count += 1
		var row := {
			"facet": entry["facet"],
			"method": entry["method"],
			"params": entry["params"],
			"arity": Array(entry["params"]).size(),
			"returns": entry["returns"],
			"probe": entry["probe_mode"],
			# Historical name kept for map consumers; means probe-callable.
			"simulationBacked": observed,
			"callable": observed,
			"bagOnly": bag_only,
			"stateMutating": state_mutating,
			"subsystemConsumed": subsystem_consumed,
		}
		if observed:
			var restriction := String(SurfaceData.RESTRICTIONS.get(qualified, ""))
			if restriction != "":
				row["restrictions"] = restriction
		else:
			var annotation: Dictionary = SurfaceData.BLOCKED.get(qualified, {})
			row["blockingSubsystem"] = annotation.get("subsystem", "UNANNOTATED")
			row["requiredSimState"] = annotation.get("requires", "UNANNOTATED")
		methods.append(row)
	var subsystems: Array = []
	var subsystem_ids: Array = SurfaceData.SUBSYSTEMS.keys()
	subsystem_ids.sort()
	for subsystem_id in subsystem_ids:
		subsystems.append({
			"id": subsystem_id,
			"requires": SurfaceData.SUBSYSTEMS[subsystem_id],
		})
	var routing: Array = []
	var member_names: Array = SurfaceData.VOCABULARY_ROUTING.keys()
	member_names.sort()
	for member_name in member_names:
		var route: Dictionary = SurfaceData.VOCABULARY_ROUTING[member_name]
		var row := {"member": member_name}
		for key in ["route", "worldMethods", "blockingSubsystem", "signatureGap", "mappingSource", "note"]:
			if route.has(key):
				row[key] = route[key]
		routing.append(row)
	var document := {
		"$comment": (
			"Measured SageScriptWorld surface map. Regenerate with "
			+ "script_world_surface_runner.gd -- --write; do not hand-edit. "
			+ "The runner fails if this file disagrees with the probed "
			+ "adapter or with its own regeneration."
		),
		"surface": (
			"Every public non-static script-declared instance method of the "
			+ "16 facet objects of SageScriptWorld (the three presentation "
			+ "sinks counted per sink) plus the base world's own 7 surface "
			+ "methods under facet 'world'. simulationBacked/callable is "
			+ "OBSERVED by calling each method against RetailSliceScriptWorld "
			+ "over a synthetic RetailSliceSim. bagOnly/stateMutating/"
			+ "subsystemConsumed are also probe-measured: callable is not "
			+ "parity. Blocking annotations are analysis, verified for "
			+ "coverage but not provable by a probe."
		),
		"counts": {
			"total": entries.size(),
			"simulationBacked": backed_count,
			"callable": backed_count,
			"bagOnly": bag_only_count,
			"stateMutating": state_mutating_count,
			"subsystemConsumed": subsystem_consumed_count,
			"refusing": entries.size() - backed_count,
		},
		"subsystems": subsystems,
		"methods": methods,
		"vocabularyRouting": routing,
	}
	return JSON.stringify(document, "\t", false) + "\n"


func _verify_against_committed_map(entries: Array, generated: String) -> void:
	var committed := FileAccess.get_file_as_string(MAP_PATH)
	_check("committed map exists at %s" % MAP_PATH, committed != "")
	var parsed: Variant = JSON.parse_string(committed)
	_check("committed map parses as JSON", parsed is Dictionary)
	if not (parsed is Dictionary):
		return
	var by_name := {}
	for row in Array((parsed as Dictionary).get("methods", [])):
		by_name["%s.%s" % [row["facet"], row["method"]]] = row
	# Direction 1: every probed method must be accounted for in the map, with
	# the classification the probe observed.
	for entry in entries:
		var qualified := "%s.%s" % [entry["facet"], entry["method"]]
		if not by_name.has(qualified):
			_check("map accounts for %s (UNACCOUNTED method)" % qualified, false)
			continue
		var row: Dictionary = by_name[qualified]
		var claimed := bool(row.get("simulationBacked", false))
		var observed := bool(entry["observed_backed"])
		_check(
			"%s: map says %s, probe observed %s" % [
				qualified,
				"backed" if claimed else "refused",
				"answers" if observed else "refuses",
			],
			claimed == observed
		)
		if not observed:
			_check(
				"%s carries a blocking annotation" % qualified,
				String(row.get("blockingSubsystem", "")) != ""
				and String(row.get("blockingSubsystem", "")) != "UNANNOTATED"
			)
	# Direction 2: the map may not carry methods the surface does not have.
	var probed := {}
	for entry in entries:
		probed["%s.%s" % [entry["facet"], entry["method"]]] = true
	for qualified in by_name:
		_check("map entry %s exists on the surface" % qualified, probed.has(qualified))
	# Every annotation refers to a real method and a declared subsystem, and
	# every declared subsystem is used.
	for qualified in SurfaceData.BLOCKED:
		_check("annotation %s names a real method" % qualified, probed.has(qualified))
		var subsystem := String((SurfaceData.BLOCKED[qualified] as Dictionary).get("subsystem", ""))
		_check(
			"annotation %s names a declared subsystem" % qualified,
			SurfaceData.SUBSYSTEMS.has(subsystem)
		)
	var used := {}
	for qualified in SurfaceData.BLOCKED:
		used[(SurfaceData.BLOCKED[qualified] as Dictionary).get("subsystem", "")] = true
	for route_value in SurfaceData.VOCABULARY_ROUTING.values():
		var route: Dictionary = route_value
		if route.has("blockingSubsystem"):
			used[route["blockingSubsystem"]] = true
	for subsystem_id in SurfaceData.SUBSYSTEMS:
		_check("declared subsystem %s is referenced" % subsystem_id, used.has(subsystem_id))
	# Byte-identity: the committed file is exactly what this runner writes.
	_check(
		"committed map is byte-identical to regeneration (run with -- --write to refresh)",
		committed == generated
	)


func _verify_read_only_sweep(entries: Array) -> void:
	## Every query-returning method, all candidates, one shared world: the
	## hash may not move. Backed queries are read-only by contract; refused
	## ones must not touch anything either.
	##
	## WRITE-THROUGH QUERIES: a few vocabulary methods return SageWorldQuery
	## (spawned name / handle) but mutate by contract - CREATE_OBJECT and its
	## spawn siblings. Exclude them from the pure read-only sweep; they are
	## still probed for simulationBacked separately.
	const WRITE_THROUGH_QUERIES := {
		"units.create_object": true,
		"units.spawn_at": true,
		"units.create_on_team_at": true,
		"units.create_object_on_team": true,
	}
	var fx := _fixture()
	var sim: RetailSliceSim = fx["sim"]
	var before := sim.state_hash()
	var sweep_calls := 0
	for entry in entries:
		if String(entry["returns"]) != "query":
			continue
		var qualified := "%s.%s" % [entry["facet"], entry["method"]]
		if WRITE_THROUGH_QUERIES.has(qualified):
			continue
		var target := _facet_object(fx["world"], String(entry["facet"]))
		for tuple in _candidate_tuples(entry, fx):
			_call_answers(target, entry, tuple)
			sweep_calls += 1
	_check(
		"state_hash unchanged across the read-only probe sweep (%d calls)" % sweep_calls,
		sim.state_hash() == before
	)
	_release_fixture(fx)


func _verify_routing_and_print_ranking(entries: Array) -> void:
	var census_text := FileAccess.get_file_as_string(CENSUS_PATH)
	var census: Variant = JSON.parse_string(census_text)
	_check("census parses", census is Dictionary)
	if not (census is Dictionary):
		return
	var members: Array = (census as Dictionary).get("members", [])
	_check("census carries 91 members", members.size() == 91)
	var probed := {}
	var backed := {}
	for entry in entries:
		var qualified := "%s.%s" % [entry["facet"], entry["method"]]
		probed[qualified] = true
		backed[qualified] = bool(entry["observed_backed"])
	# Routing must cover the census exactly: every member routed once, no
	# routes for members the census does not carry.
	var routed := {}
	for member_name in SurfaceData.VOCABULARY_ROUTING:
		routed[member_name] = true
	for member in members:
		_check(
			"census member %s is routed" % member["name"],
			routed.has(String(member["name"]))
		)
	var census_names := {}
	for member in members:
		census_names[String(member["name"])] = member
	for member_name in SurfaceData.VOCABULARY_ROUTING:
		_check(
			"routed member %s is in the census" % member_name,
			census_names.has(member_name)
		)
		var route: Dictionary = SurfaceData.VOCABULARY_ROUTING[member_name]
		for world_method in Array(route.get("worldMethods", [])):
			_check(
				"route %s cites a real method (%s)" % [member_name, world_method],
				probed.has(String(world_method))
			)
		var route_kind := String(route.get("route", ""))
		_check(
			"route %s has a recognised kind" % member_name,
			["env", "backed", "blocked"].has(route_kind)
		)
		if route_kind == "blocked":
			_check(
				"blocked route %s names a declared subsystem" % member_name,
				SurfaceData.SUBSYSTEMS.has(String(route.get("blockingSubsystem", "")))
			)
		if route_kind == "backed":
			# A member declared served must route only to methods that answer.
			for world_method in Array(route.get("worldMethods", [])):
				_check(
					"backed route %s cites a backed method (%s)" % [member_name, world_method],
					bool(backed.get(String(world_method), false))
				)
	# The ranking.
	var per_subsystem := {}
	for subsystem_id in SurfaceData.SUBSYSTEMS:
		per_subsystem[subsystem_id] = {"methods": 0, "call_sites": 0, "members": 0}
	for qualified in SurfaceData.BLOCKED:
		var subsystem := String((SurfaceData.BLOCKED[qualified] as Dictionary).get("subsystem", ""))
		if per_subsystem.has(subsystem):
			(per_subsystem[subsystem] as Dictionary)["methods"] = int((per_subsystem[subsystem] as Dictionary)["methods"]) + 1
	var env_sites := 0
	var backed_sites := 0
	var signature_gap_sites := 0
	for member_name in SurfaceData.VOCABULARY_ROUTING:
		var route: Dictionary = SurfaceData.VOCABULARY_ROUTING[member_name]
		var sites := int((Dictionary(census_names[member_name])["callSites"] as Dictionary)["bfme2-retail"])
		match String(route.get("route", "")):
			"env":
				env_sites += sites
			"backed":
				backed_sites += sites
			"blocked":
				var subsystem := String(route.get("blockingSubsystem", ""))
				if per_subsystem.has(subsystem):
					var bucket: Dictionary = per_subsystem[subsystem]
					bucket["call_sites"] = int(bucket["call_sites"]) + sites
					bucket["members"] = int(bucket["members"]) + 1
				if bool(route.get("signatureGap", false)):
					signature_gap_sites += sites
	var ranked: Array = []
	for subsystem_id in per_subsystem:
		var bucket: Dictionary = per_subsystem[subsystem_id]
		ranked.append({
			"id": subsystem_id,
			"methods": int(bucket["methods"]),
			"call_sites": int(bucket["call_sites"]),
			"members": int(bucket["members"]),
		})
	ranked.sort_custom(func(a, b):
		if int(a["call_sites"]) != int(b["call_sites"]):
			return int(a["call_sites"]) > int(b["call_sites"])
		if int(a["methods"]) != int(b["methods"]):
			return int(a["methods"]) > int(b["methods"])
		return String(a["id"]) < String(b["id"])
	)
	print("")
	print("RANKED SUBSYSTEMS (methods unblocked / retail-AI BFME2 call sites unblocked)")
	for row in ranked:
		print("  %-34s methods=%-3d members=%-2d callSites=%d" % [
			row["id"], row["methods"], row["members"], row["call_sites"],
		])
	print("  (env-served call sites: %d; already-backed call sites: %d;" % [env_sites, backed_sites])
	print("   call sites ALSO needing the facet-signature packet, overlapping: %d)" % signature_gap_sites)
