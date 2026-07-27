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
## WHAT "BACKED" MEANS HERE
## ========================
## A method is simulation-backed when at least one well-formed argument tuple
## (bound player/team names, POSITION targets, PLAYER scope, modelled
## science/upgrade/power ids) gets a real answer: `ok` for a query, `true` for
## a command. Several backed methods still refuse RESTRICTED argument shapes
## (orders.move_to refuses non-POSITION targets, teams.stop refuses disband);
## those restrictions are recorded per method in the map's `restrictions`
## field and drive the vocabulary routing, not the backed flag.
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
	print(
		"SURFACE total=%d backed=%d refusing=%d probedByCall=%d fallbackProbed=%d"
		% [entries.size(), backed, entries.size() - backed, called, fallback]
	)
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
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
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
	world.bind_team(PLAYER_TEAM_NAME, SimScript.PLAYER_TEAM)
	world.bind_team(ENEMY_TEAM_NAME, SimScript.ENEMY_TEAM)
	_check("fixture: script player binds", world.bind_script_player(PLAYER))
	_check(
		"fixture: home flag unpacks free and binds SYNTH_HOME_REF",
		world.ai().base_unpack("SYNTH_HOME_FLAG", true, "SYNTH_HOME_REF")
	)
	return {
		"sim": sim,
		"world": world,
		"cast_position": Vector3(at.x, 0.0, at.y),
	}


func _release_fixture(fx: Dictionary) -> void:
	## Break the world <-> facet reference cycle so per-probe fixtures free
	## instead of leaking one world + facet set per method at exit.
	var world: RetailSliceScriptWorld = fx["world"]
	for facet in world._facets.values():
		facet.world = null
	world._facets.clear()


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
				# The unpack pair and the unpackable condition take the fixture's
				# packed base flag; every other object_name method refuses
				# regardless of the value (no object-name registry).
				return ["SYNTH_BASE_FLAG"]
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
		"units.has_command_points_to_build":
			# Needs a type the fixture's production rules model; the retail
			# spelling resolves through the runtime-id slug.
			return [[PLAYER, "GondorFighterHorde"]]
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


func _probe_entry(entry: Dictionary) -> Dictionary:
	## Probe one method on a fresh fixture. Returns
	## {"backed": bool, "mode": "called"|"fallback", "calls": int}.
	var qualified := "%s.%s" % [entry["facet"], entry["method"]]
	if FALLBACK_PROBED.has(qualified):
		return _probe_fallback(qualified)
	var fx := _fixture()
	var target := _facet_object(fx["world"], String(entry["facet"]))
	var calls := 0
	var backed := false
	for tuple in _candidate_tuples(entry, fx):
		calls += 1
		if _call_answers(target, entry, tuple):
			backed = true
			break
	_release_fixture(fx)
	return {"backed": backed, "mode": "called", "calls": calls}


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
	var methods: Array = []
	for entry in entries:
		var qualified := "%s.%s" % [entry["facet"], entry["method"]]
		var observed := bool(entry["observed_backed"])
		if observed:
			backed_count += 1
		var row := {
			"facet": entry["facet"],
			"method": entry["method"],
			"params": entry["params"],
			"arity": Array(entry["params"]).size(),
			"returns": entry["returns"],
			"probe": entry["probe_mode"],
			"simulationBacked": observed,
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
			+ "methods under facet 'world'. simulationBacked is OBSERVED by "
			+ "calling each method against RetailSliceScriptWorld over a "
			+ "synthetic RetailSliceSim; blocking annotations are analysis, "
			+ "verified for coverage but not provable by a probe."
		),
		"counts": {
			"total": entries.size(),
			"simulationBacked": backed_count,
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
	var fx := _fixture()
	var sim: RetailSliceSim = fx["sim"]
	var before := sim.state_hash()
	var sweep_calls := 0
	for entry in entries:
		if String(entry["returns"]) != "query":
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
