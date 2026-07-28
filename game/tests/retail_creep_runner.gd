extends SceneTree

## Neutral creep-lair gate (BFME2 PlyrCreeps camps per the measured creep
## contract). Boots the real private Men slice once for the actual gameplay
## rules + Fords map configuration, then constructs sims directly:
##   (a) map-driven lair exposure: 6 authored Fords lairs (2 CaveTrollLair +
##       4 WargLair) with authored positions, plus the other four maps'
##       authored tables through the same map-data lane,
##   (b) OFF-flag inertness: with the creep rule absent the placements are
##       inert — snapshot JSON is byte-identical with and without them (the
##       pinned 3CB9CA98 battle signature is gated by retail_slice_runner),
##   (c) seeding/burst: 2000 HP lairs, family bursts (1 troll / 2 wargs),
##       measured leash ranges, replacement delays in ticks,
##   (d) creep hostility vs multiple rostered teams without ever joining
##       victory resolution,
##   (e) aggro on CREEP_VISION, chase, leash break, forced return home,
##   (f) lair -> 500 HP hole -> treasure -> team resource reward chain with
##       permanent clear (no rebuild after the hole dies),
##   (g) SpawnBehavior replacement respawn timer,
##   (h) RebuildHoleBehavior lair regrow after 120 s when the hole survives,
##   (i) twin-run determinism (signature + state hash) with creeps enabled,
##   (j) unconverted lair families (Dagorlad goblin/drake) seed fully in the
##       sim and fail closed into recorded provisional art status.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const FIGHTER := "bfme2.object.gondor-fighter"
const FIGHTER_HORDE := "bfme2.object.gondor-fighter-horde"
const FIVE_MAPS_PACK_ID := "bfme2-five-maps-106-private"
const MAP_CATALOG_MAX_BYTES := 1024 * 1024

var passed := 0
var failed := 0
var slice = null
var base_rules: Dictionary = {}
var map_config: Dictionary = {}
var scale := 1.0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_CREEP_RUNNER")
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok) or slice.source_map_data == null:
		_finish()
		return

	base_rules = slice.gameplay_rules.duplicate(true)
	map_config = slice.source_map_data.simulation_configuration()
	scale = float(base_rules.get("source_map_transform_scale", 0.0))
	_check("source_scale_positive", scale > 0.0, str(scale))

	_run_map_exposure()
	_run_off_flag_inertness()
	_run_seeding_and_hostility()
	_run_aggro_leash_return()
	_run_reward_chain()
	_run_respawn_timer()
	_run_rebuild_path()
	_run_victory_exclusion()
	_run_twin_determinism()
	_run_other_maps()

	slice.queue_free()
	_finish()


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

func _creep_rules() -> Dictionary:
	var rules := base_rules.duplicate(true)
	rules["enable_base_loop"] = false
	rules["spawn_initial_battalions"] = false
	rules["enable_creep_lairs"] = true
	return rules


func _make_sim(rules: Dictionary, configuration: Dictionary):
	var sim = SimScript.new()
	sim.setup(configuration.duplicate(true), rules.duplicate(true))
	sim.ai_enabled = false
	return sim


func _lair_ids(sim, family: String = "") -> Array[int]:
	var result: Array[int] = []
	for id in sim.structure_ids(SimScript.CREEP_TEAM):
		var row: Dictionary = sim.structures[id]
		if String(row.get("structure_kind", "")) != "creep_lair":
			continue
		if family != "" and String(row.get("creep_family", "")) != family:
			continue
		result.append(id)
	return result


func _hole_ids(sim) -> Array[int]:
	var result: Array[int] = []
	for id in sim.structure_ids(SimScript.CREEP_TEAM):
		if String((sim.structures[id] as Dictionary).get("structure_kind", "")) == "creep_hole":
			result.append(id)
	return result


func _guard_ids(sim, lair_id: int = 0) -> Array[int]:
	var result: Array[int] = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) != SimScript.CREEP_TEAM or not row.has("creep_lair_id"):
			continue
		if lair_id != 0 and int(row.get("creep_lair_id", 0)) != lair_id:
			continue
		result.append(id)
	return result


func _living_guard_ids(sim, lair_id: int = 0) -> Array[int]:
	var result: Array[int] = []
	for id in _guard_ids(sim, lair_id):
		if int((sim.entities[id] as Dictionary).get("health", 0)) > 0:
			result.append(id)
	return result


func _add_keepers(sim) -> void:
	## One living battalion per rostered team, far apart, so victory never
	## resolves mid-scenario and freezes the sim.
	sim._add_battalion(801, 0, Vector2(sim._spawn_positions[1]), "Keeper0", FIGHTER, FIGHTER_HORDE)
	sim._add_battalion(901, 1, Vector2(sim._spawn_positions[101]), "Keeper1", FIGHTER, FIGHTER_HORDE)


func _first_event(sim, kind: String) -> Dictionary:
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == kind:
			return event
	return {}


# ---------------------------------------------------------------------------
# (a) Map-driven lair exposure
# ---------------------------------------------------------------------------

func _run_map_exposure() -> void:
	var placements: Array = map_config.get("creep_lair_placements", [])
	_check("fords_authors_six_lairs", placements.size() == 6, str(placements.size()))
	var counts := {}
	var indices: Array[int] = []
	for placement_value in placements:
		var placement := placement_value as Dictionary
		var type_name := String(placement.get("type_name", ""))
		counts[type_name] = int(counts.get(type_name, 0)) + 1
		indices.append(int(placement.get("source_index", -1)))
		_check("fords_lair_bound_%d" % int(placement.get("source_index", -1)), String(placement.get("binding_status", "")) == "bound", String(placement.get("binding_status", "")))
	indices.sort()
	_check("fords_lair_type_counts", int(counts.get("CaveTrollLair", 0)) == 2 and int(counts.get("WargLair", 0)) == 4, str(counts))
	_check("fords_lair_source_indices", indices == [42, 43, 249, 250, 499, 500], str(indices))
	# Authored position spot check: CaveTrollLair 1483 (source index 42) sits
	# at the contract's godotPosition, mapped through the map's own transform.
	var expected_local: Vector3 = slice.source_map_data.source_to_local(Vector3(548.412, 300.0, -386.488))
	var found := false
	for placement_value in placements:
		var placement := placement_value as Dictionary
		if int(placement.get("source_index", -1)) != 42:
			continue
		var position := Vector2(placement.get("position", Vector2.INF))
		found = position.distance_to(Vector2(expected_local.x, expected_local.z)) < 0.01
	_check("fords_troll_lair_position_authored", found, str(expected_local))


# ---------------------------------------------------------------------------
# (b) OFF-flag inertness
# ---------------------------------------------------------------------------

func _run_off_flag_inertness() -> void:
	var rules := base_rules.duplicate(true)
	rules["enable_base_loop"] = false
	rules["spawn_initial_battalions"] = false
	var config_without := map_config.duplicate(true)
	config_without.erase("creep_lair_placements")
	var sim_with = _make_sim(rules, map_config)
	var sim_without = _make_sim(rules, config_without)
	_add_keepers(sim_with)
	_add_keepers(sim_without)
	_check("off_flag_no_creep_structures", sim_with.structure_ids(SimScript.CREEP_TEAM).is_empty())
	_check("off_flag_no_creeps_snapshot_key", not sim_with.state_snapshot().has("creeps"))
	sim_with.advance(200)
	sim_without.advance(200)
	_check(
		"off_flag_placements_inert_byte_identical",
		JSON.stringify(sim_with.state_snapshot()) == JSON.stringify(sim_without.state_snapshot()),
		"%s != %s" % [sim_with.state_signature(), sim_without.state_signature()]
	)


# ---------------------------------------------------------------------------
# (c)+(d) Seeding, burst counts, hostility predicate
# ---------------------------------------------------------------------------

func _run_seeding_and_hostility() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	var troll_lairs := _lair_ids(sim, "CaveTrollLair")
	var warg_lairs := _lair_ids(sim, "WargLair")
	_check("seeded_lair_families", troll_lairs.size() == 2 and warg_lairs.size() == 4, "troll=%d warg=%d" % [troll_lairs.size(), warg_lairs.size()])
	var lairs_ok := not troll_lairs.is_empty() and not warg_lairs.is_empty()
	if lairs_ok:
		var troll: Dictionary = sim.structures[troll_lairs[0]]
		var warg: Dictionary = sim.structures[warg_lairs[0]]
		_check("lair_health_2000", int(troll.get("health", 0)) == 2000 and int(warg.get("health", 0)) == 2000)
		_check("troll_replace_delay_120s", int(troll.get("creep_replace_delay_ticks", 0)) == 1200, str(troll.get("creep_replace_delay_ticks")))
		_check("warg_replace_delay_45s", int(warg.get("creep_replace_delay_ticks", 0)) == 450, str(warg.get("creep_replace_delay_ticks")))
		_check("troll_treasure_large_4_chests", int(troll.get("creep_treasure_chests", 0)) == 4)
		_check("warg_treasure_medium_3_chests", int(warg.get("creep_treasure_chests", 0)) == 3)
		var troll_guards := _living_guard_ids(sim, troll_lairs[0])
		var warg_guards := _living_guard_ids(sim, warg_lairs[0])
		_check("troll_burst_1_guard", troll_guards.size() == 1, str(troll_guards.size()))
		_check("warg_burst_2_guards", warg_guards.size() == 2, str(warg_guards.size()))
		_check("total_burst_10_guards", _living_guard_ids(sim).size() == 10, str(_living_guard_ids(sim).size()))
		if not troll_guards.is_empty() and not warg_guards.is_empty():
			var troll_guard: Dictionary = sim.entities[troll_guards[0]]
			var warg_guard: Dictionary = sim.entities[warg_guards[0]]
			_check("troll_guard_hp_3000", int(troll_guard.get("health", 0)) == 3000, str(troll_guard.get("health")))
			_check("warg_guard_hp_800", int(warg_guard.get("health", 0)) == 800, str(warg_guard.get("health")))
			_check(
				"guard_leash_ranges_scaled",
				absf(float(troll_guard.get("creep_guard_max_range", 0.0)) - 250.0 * scale) < 0.001
				and absf(float(troll_guard.get("creep_guard_wander_range", 0.0)) - 80.0 * scale) < 0.001,
				str(troll_guard.get("creep_guard_max_range"))
			)
			_check(
				"guard_vision_creep_vision_scaled",
				absf(float(troll_guard.get("vision_range", 0.0)) - 200.0 * scale) < 0.001,
				str(troll_guard.get("vision_range"))
			)
			_check(
				"guard_spawned_near_lair",
				Vector2(troll_guard.get("position", Vector2.INF)).distance_to(Vector2((sim.structures[troll_lairs[0]] as Dictionary).get("position", Vector2.ZERO))) < 6.0
			)
	_check("creep_team_not_combatant", not sim._is_combatant_team(SimScript.CREEP_TEAM))
	_check(
		"creep_hostile_to_all_rostered_teams",
		sim._is_hostile(SimScript.CREEP_TEAM, 0) and sim._is_hostile(0, SimScript.CREEP_TEAM)
		and sim._is_hostile(SimScript.CREEP_TEAM, 1) and sim._is_hostile(1, SimScript.CREEP_TEAM)
	)
	_check(
		"creep_not_hostile_to_neutral_or_self",
		not sim._is_hostile(SimScript.CREEP_TEAM, SimScript.NEUTRAL_TEAM)
		and not sim._is_hostile(SimScript.CREEP_TEAM, SimScript.CREEP_TEAM)
	)
	_check("creeps_snapshot_key_present", sim.state_snapshot().has("creeps"))


# ---------------------------------------------------------------------------
# (e) Aggro on CREEP_VISION, chase, leash break, forced return
# ---------------------------------------------------------------------------

func _run_aggro_leash_return() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	_add_keepers(sim)
	var troll_lairs := _lair_ids(sim, "CaveTrollLair")
	if troll_lairs.is_empty():
		_check("aggro_fixture_available", false)
		return
	var lair_id := troll_lairs[0]
	var home := Vector2((sim.structures[lair_id] as Dictionary).get("position", Vector2.ZERO))
	var guards := _living_guard_ids(sim, lair_id)
	if guards.is_empty():
		_check("aggro_fixture_available", false)
		return
	var guard_id := guards[0]
	var vision := 200.0 * scale
	var leash := 250.0 * scale
	# Intruder walks inside CREEP_VISION of the guard's home. HoldGround keeps
	# it from counter-chasing the returning guard later in the scenario.
	sim._add_battalion(701, 0, home + Vector2(vision * 0.5, 0.0), "Intruder", FIGHTER, FIGHTER_HORDE)
	sim.issue_set_stance([701], "HoldGround", 0)
	var aggroed := false
	for _tick in range(40):
		sim.tick()
		if int((sim.entities[guard_id] as Dictionary).get("target_id", 0)) == 701:
			aggroed = true
			break
	_check("guard_aggro_on_creep_vision", aggroed)
	# Chase: the guard closes distance while the target stays inside the leash.
	var distance_before := Vector2((sim.entities[guard_id] as Dictionary).get("position", Vector2.ZERO)).distance_to(Vector2((sim.entities[701] as Dictionary).get("position", Vector2.ZERO)))
	sim.advance(30)
	var distance_after := Vector2((sim.entities[guard_id] as Dictionary).get("position", Vector2.ZERO)).distance_to(Vector2((sim.entities[701] as Dictionary).get("position", Vector2.ZERO)))
	_check("guard_chases_target_within_leash", distance_after <= distance_before + 0.001, "%f -> %f" % [distance_before, distance_after])
	# The target leaves the leash: the guard breaks off and returns home.
	var intruder: Dictionary = sim.entities[701]
	intruder["position"] = home + Vector2(leash * 3.0, 0.0)
	intruder["destination"] = Vector2(intruder["position"])
	intruder["target_id"] = 0
	intruder["route"] = []
	var returned := false
	var dropped := false
	for _tick in range(600):
		sim.tick()
		var guard: Dictionary = sim.entities[guard_id]
		if int(guard.get("target_id", 0)) == 0:
			dropped = true
		if dropped and Vector2(guard.get("position", Vector2.INF)).distance_to(home) <= 80.0 * scale + 3.0 and not bool(guard.get("creep_returning", false)):
			returned = true
			break
	_check("guard_leash_break_drops_target", dropped)
	_check("guard_returns_home_after_leash_break", returned)
	var leash_event := _first_event(sim, "creep.guard_leash_return")
	_check("guard_leash_return_event_recorded", not leash_event.is_empty())


# ---------------------------------------------------------------------------
# (f) Lair -> hole -> treasure -> reward chain, permanent clear
# ---------------------------------------------------------------------------

func _run_reward_chain() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	_add_keepers(sim)
	var troll_lairs := _lair_ids(sim, "CaveTrollLair")
	if troll_lairs.is_empty():
		_check("reward_fixture_available", false)
		return
	var lair_id := troll_lairs[0]
	sim.tick()
	sim._apply_structure_damage(801, lair_id, 4000)
	_check("lair_dies_to_structure_damage", int((sim.structures[lair_id] as Dictionary).get("health", 1)) == 0)
	var holes := _hole_ids(sim)
	_check("lair_death_exposes_hole", holes.size() == 1, str(holes.size()))
	if holes.is_empty():
		return
	var hole_id := holes[0]
	var hole: Dictionary = sim.structures[hole_id]
	_check("hole_health_500", int(hole.get("health", 0)) == 500 and int(hole.get("maximum_health", 0)) == 500)
	_check("hole_not_auto_acquirable", bool(hole.get("not_auto_acquirable", false)))
	_check("hole_exposed_event_recorded", not _first_event(sim, "creep.hole_exposed").is_empty())
	var resources_before: int = sim.resources_for_team(0)
	sim._apply_structure_damage(801, hole_id, 1000)
	var awarded: int = sim.resources_for_team(0) - resources_before
	_check("treasure_awards_4_chests_160_200_each", awarded >= 4 * 160 and awarded <= 4 * 200, str(awarded))
	_check("treasure_event_recorded", not _first_event(sim, "creep.treasure_collected").is_empty())
	_check("camp_cleared_event_recorded", not _first_event(sim, "creep.camp_cleared").is_empty())
	_check("lair_marked_cleared", bool((sim.structures[lair_id] as Dictionary).get("creep_cleared", false)))
	# Permanent clear: well past the 120 s rebuild window nothing regrows.
	sim.advance(1300)
	_check("cleared_camp_never_rebuilds", int((sim.structures[lair_id] as Dictionary).get("health", 1)) == 0)


# ---------------------------------------------------------------------------
# (g) SpawnBehavior replacement respawn timer
# ---------------------------------------------------------------------------

func _run_respawn_timer() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	_add_keepers(sim)
	var warg_lairs := _lair_ids(sim, "WargLair")
	if warg_lairs.is_empty():
		_check("respawn_fixture_available", false)
		return
	var lair_id := warg_lairs[0]
	var guards := _living_guard_ids(sim, lair_id)
	if guards.size() != 2:
		_check("respawn_fixture_available", false, str(guards.size()))
		return
	sim._apply_member_damage(0, -1, guards[0], 999999, "battalion", 0)
	_check("warg_guard_killed", int((sim.entities[guards[0]] as Dictionary).get("health", 1)) == 0)
	sim.advance(400)
	_check("respawn_waits_full_45s_delay", _living_guard_ids(sim, lair_id).size() == 1, str(_living_guard_ids(sim, lair_id).size()))
	sim.advance(60)
	_check("guard_respawns_after_replace_delay", _living_guard_ids(sim, lair_id).size() == 2, str(_living_guard_ids(sim, lair_id).size()))


# ---------------------------------------------------------------------------
# (h) RebuildHoleBehavior: surviving hole regrows the camp in 120 s
# ---------------------------------------------------------------------------

func _run_rebuild_path() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	_add_keepers(sim)
	var troll_lairs := _lair_ids(sim, "CaveTrollLair")
	if troll_lairs.size() < 2:
		_check("rebuild_fixture_available", false)
		return
	var lair_id := troll_lairs[1]
	sim.tick()
	sim._apply_structure_damage(801, lair_id, 4000)
	var holes := _hole_ids(sim)
	if holes.is_empty():
		_check("rebuild_fixture_available", false)
		return
	sim.advance(1210)
	_check("hole_survival_rebuilds_lair", int((sim.structures[lair_id] as Dictionary).get("health", 0)) == 2000, str((sim.structures[lair_id] as Dictionary).get("health")))
	_check("rebuild_consumes_hole", _hole_ids(sim).is_empty())
	_check("rebuilt_lair_bursts_guards", _living_guard_ids(sim, lair_id).size() == 1, str(_living_guard_ids(sim, lair_id).size()))
	_check("rebuild_event_recorded", not _first_event(sim, "creep.lair_rebuilt").is_empty())


# ---------------------------------------------------------------------------
# (d) Victory: creeps are never a participant
# ---------------------------------------------------------------------------

func _run_victory_exclusion() -> void:
	var sim = _make_sim(_creep_rules(), map_config)
	_add_keepers(sim)
	sim.tick()
	_check("no_winner_with_two_rostered_teams", int(sim.winner) == -1)
	sim._apply_damage(0, 901, 10000000)
	sim.tick()
	_check("winner_resolves_despite_living_creeps", int(sim.winner) == 0, "winner=%d living_creeps=%d" % [int(sim.winner), _living_guard_ids(sim).size()])
	_check("creeps_alive_at_resolution", _living_guard_ids(sim).size() == 10, str(_living_guard_ids(sim).size()))


# ---------------------------------------------------------------------------
# (i) Twin determinism with creeps enabled
# ---------------------------------------------------------------------------

func _build_combat_sim():
	var sim = _make_sim(_creep_rules(), map_config)
	var warg_lairs := _lair_ids(sim, "WargLair")
	if warg_lairs.is_empty():
		return sim
	var camp := Vector2((sim.structures[warg_lairs[0]] as Dictionary).get("position", Vector2.ZERO))
	sim._add_battalion(801, 0, Vector2(sim._spawn_positions[1]), "Keeper0", FIGHTER, FIGHTER_HORDE)
	sim._add_battalion(901, 1, Vector2(sim._spawn_positions[101]), "Keeper1", FIGHTER, FIGHTER_HORDE)
	sim._add_battalion(702, 0, camp + Vector2(150.0 * scale, 0.0), "Raider0", FIGHTER, FIGHTER_HORDE)
	sim._add_battalion(902, 1, camp + Vector2(0.0, 150.0 * scale), "Raider1", FIGHTER, FIGHTER_HORDE)
	sim.issue_attack_move([702], camp, 0)
	sim.issue_attack_move([902], camp, 1)
	return sim


func _run_twin_determinism() -> void:
	var sim_a = _build_combat_sim()
	var sim_b = _build_combat_sim()
	var signatures_equal := true
	var divergence_tick := -1
	for tick in range(1, 401):
		sim_a.tick()
		sim_b.tick()
		if tick % 50 == 0 and sim_a.state_signature() != sim_b.state_signature():
			signatures_equal = false
			divergence_tick = tick
			break
	_check("twin_signature_equal_over_400_ticks", signatures_equal and sim_a.state_signature() == sim_b.state_signature(), "diverged at %d" % divergence_tick)
	_check("twin_state_hash_equal", sim_a.state_hash() == sim_b.state_hash())
	var events_observed := not _first_event(sim_a, "creep.guard_aggro").is_empty() or not _first_event(sim_a, "creep.guard_leash_return").is_empty()
	_check("twin_run_exercised_creep_ai", events_observed)


# ---------------------------------------------------------------------------
# (j) Other maps: authored tables ride the same map-driven lane
# ---------------------------------------------------------------------------

func _run_other_maps() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	if mod_loader == null:
		_check("five_maps_pack_reachable", false, "ModLoader missing")
		return
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var pack_root: String = mod_loader.resolve_pack_path(content_root, FIVE_MAPS_PACK_ID)
	_check("five_maps_pack_reachable", pack_root != "" and DirAccess.dir_exists_absolute(pack_root), pack_root)
	if pack_root == "":
		return
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var expected := {
		"bfme2.map.dagorlad": {"lairs": 10, "families": {"MoriarGoblinLair": 8, "FireDrakeLair": 2}},
		"bfme2.map.mordor": {"lairs": 8, "families": {"CaveTrollLair": 4, "MoriarGoblinLair": 2, "FireDrakeLair": 2}},
		"bfme2.map.mount-doom": {"lairs": 9, "families": {"MoriarGoblinLair": 6, "FireDrakeLair": 2, "CaveTrollLair": 1}},
		"bfme2.map.rivendell": {"lairs": 2, "families": {"WargLair": 2}},
	}
	var dagorlad_config: Dictionary = {}
	var dagorlad_scale := 0.0
	for map_id in expected.keys():
		var definition := _catalog_definition(mod_loader, pack_root, String(map_id))
		if definition.is_empty():
			_check("%s catalog_definition" % map_id, false)
			continue
		var map_data = map_data_script.new()
		if not bool(map_data.load_from_pack(pack_root, definition)):
			_check("%s map_data_loads" % map_id, false, String(map_data.error))
			continue
		var placements: Array = map_data.creep_lair_placements
		var row: Dictionary = expected[map_id]
		_check("%s lair_total" % map_id, placements.size() == int(row.get("lairs", -1)), str(placements.size()))
		var counts := {}
		for placement_value in placements:
			var type_name := String((placement_value as Dictionary).get("type_name", ""))
			counts[type_name] = int(counts.get(type_name, 0)) + 1
		var families_ok := true
		for family_name in (row.get("families", {}) as Dictionary).keys():
			if int(counts.get(family_name, 0)) != int((row.get("families", {}) as Dictionary)[family_name]):
				families_ok = false
		_check("%s lair_families" % map_id, families_ok, str(counts))
		if String(map_id) == "bfme2.map.dagorlad":
			dagorlad_config = map_data.simulation_configuration()
			dagorlad_scale = float(map_data.local_transform_scale)
	# Sim seeding on Dagorlad: goblin/drake lair art is unconverted — the sim
	# camps must seed fully (not vanish) and record provisional art status.
	if dagorlad_config.is_empty():
		_check("dagorlad_sim_seeds", false)
		return
	var dagorlad_rules := _creep_rules()
	dagorlad_rules["source_map_transform_scale"] = dagorlad_scale
	var sim = _make_sim(dagorlad_rules, dagorlad_config)
	var goblin_lairs := _lair_ids(sim, "MoriarGoblinLair")
	var drake_lairs := _lair_ids(sim, "FireDrakeLair")
	_check("dagorlad_sim_seeds", goblin_lairs.size() == 8 and drake_lairs.size() == 2, "goblin=%d drake=%d" % [goblin_lairs.size(), drake_lairs.size()])
	_check("dagorlad_guard_total", _living_guard_ids(sim).size() == 8 * 8 + 2 * 1, str(_living_guard_ids(sim).size()))
	if not goblin_lairs.is_empty():
		var goblin_guards := _living_guard_ids(sim, goblin_lairs[0])
		var swordsmen := 0
		var archers := 0
		for guard_id in goblin_guards:
			match String((sim.entities[guard_id] as Dictionary).get("object_id", "")):
				"bfme2.object.creep-goblin-swordsman":
					swordsmen += 1
				"bfme2.object.creep-goblin-archer":
					archers += 1
		_check("dagorlad_goblin_burst_mix", swordsmen == 4 and archers == 4, "swords=%d archers=%d" % [swordsmen, archers])
		var art_recorded := true
		for lair_id in goblin_lairs + drake_lairs:
			if String((sim.structures[lair_id] as Dictionary).get("creep_art_status", "")) == "":
				art_recorded = false
		_check("dagorlad_provisional_art_recorded", art_recorded)
	if not drake_lairs.is_empty():
		var drake_guards := _living_guard_ids(sim, drake_lairs[0])
		if not drake_guards.is_empty():
			_check("drake_guard_hp_4000", int((sim.entities[drake_guards[0]] as Dictionary).get("health", 0)) == 4000)
			_check(
				"drake_leash_420_scaled",
				absf(float((sim.entities[drake_guards[0]] as Dictionary).get("creep_guard_max_range", 0.0)) - 420.0 * dagorlad_scale) < 0.001,
				str((sim.entities[drake_guards[0]] as Dictionary).get("creep_guard_max_range"))
			)


func _catalog_definition(mod_loader, pack_root: String, map_id: String) -> Dictionary:
	## Mirrors retail_map_data_runner: the definition is the catalog row merged
	## with the map document, carrying resolved _source/_pack_root paths.
	var catalog := _read_bounded(mod_loader, pack_root, "data/maps.json", MAP_CATALOG_MAX_BYTES)
	if String(catalog.get("schema", "")) != "openbfme.map-catalog":
		return {}
	for row_value in catalog.get("maps", []) as Array:
		var row := row_value as Dictionary
		if row == null or String(row.get("id", "")) != map_id:
			continue
		var map_relative := String(row.get("map", ""))
		if map_relative == "" or not mod_loader.is_safe_relative_path(map_relative):
			return {}
		var map_doc := _read_bounded(mod_loader, pack_root, map_relative, 2 * 1024 * 1024)
		if map_doc.is_empty() or String(map_doc.get("id", "")) != map_id:
			return {}
		var merged := row.duplicate(true)
		merged.merge(map_doc, true)
		merged["map"] = map_relative
		merged["_source"] = mod_loader.resolve_pack_path(pack_root, map_relative)
		merged["_pack_root"] = pack_root
		return merged
	return {}


func _read_bounded(mod_loader, pack_root: String, relative: String, maximum_bytes: int) -> Dictionary:
	var path: String = mod_loader.resolve_pack_path(pack_root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	file.close()
	var raw: Variant = mod_loader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


# ---------------------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_CREEP PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_CREEP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_CREEP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
