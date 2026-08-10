extends SceneTree
## A created hero, IN A MATCH. Saved, offered by the fortress, bought through
## the lockstep command queue, spawned, hit, healed, cast, levelled.
##
## WHY THIS RUNNER EXISTS. cah_create_a_hero_runner proves the arithmetic and
## the document; every assertion it makes stops at the document's edge. Until
## this file nothing had ever spawned a created hero in a running simulation, so
## "the sliders work" rested entirely on the numbers being written down
## correctly - and two of the five (armour, self-heal) were being written down
## and then dropped on the floor. Everything here runs on the MOUNTED PACK's own
## compiled Create-a-Hero table and the real Men faction slice; nothing is
## synthesized except the player's own saved profile, which is written through
## the same persistence API the MY HEROES screen uses.
##
## THE PURCHASE GOES THROUGH THE COMMAND QUEUE. `submit_command` +
## `advance` is the same path a lockstep peer's click takes, so a hero that
## cannot be bought deterministically fails here rather than in a match.
##
## Env: OPENBFME_CONTENT - the content root to mount (as every goal runner).

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

const FACTION := "men"
const PLAYER_TEAM := 0
const ENEMY_TEAM := 1
const MAP_SCALE := 0.1
const FLOOD_RESOURCES := 100000000
const FLOOD_COMMAND_POINTS := 100000000
## Hero of the West / subclass 0, which authors Men among its UsableFactions.
const CLASS_INDEX := 0
const SUB_CLASS_INDEX := 0
## Level 1, no prerequisite, a compiled weapon blast: castable the tick the hero
## lands, which is what makes it the power this runner can prove.
const SPEAR_POWER := "Command_CreateAHeroThrowSpear_Level1"
## Rank 4 is the first authored level that grants an upgrade, so it is the rank
## that proves a level-up does something rather than only counting.
const LEVEL_FOUR_XP := 300
const LEVEL_FOUR_UPGRADE := "Upgrade_CreateAHeroGloriousCharge"

## LIVENESS. A GDScript runtime error aborts its function without propagating,
## so a broken run would print zero failures. Raise deliberately; never lower.
const EXPECTED_CHECKS := 50

var passed := 0
var failed := 0
var _saved_hero_ids: Array[String] = []


func _initialize() -> void:
	_runner_watchdog.start(self, "CAH_MATCH")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	if content_db == null:
		printerr("CAH_MATCH FAIL no ContentDB autoload")
		quit(1)
		return
	await process_frame
	await process_frame

	var system_value: Variant = content_db.get("cah_system_runtime")
	var system: Dictionary = (
		system_value as Dictionary if typeof(system_value) == TYPE_DICTIONARY else {}
	)
	_check(CahHeroes.system_is_valid(system), "the mounted pack carries a compiled CAH table")
	if not CahHeroes.system_is_valid(system):
		_finish()
		return

	var profile := _write_profile(system)
	if profile.is_empty():
		_finish()
		return

	var sub_row := CahHeroes.sub_class_row(system, CLASS_INDEX, SUB_CLASS_INDEX)
	var stats := CahHeroes.computed_stats(
		system, sub_row, profile.get("attributes", {}) as Dictionary,
		profile.get("powers", []) as Array
	)
	var baseline := CahHeroes.combat_baseline(system, profile)
	_report_baseline(system, profile, baseline)
	_test_baseline_fallback(system, profile)
	_test_baseline_contract(system, profile)

	_test_skirmish_ownership_follows_the_human(system)
	_test_created_heroes_survive_a_worker_thread(system)

	var slice = _classified_slice(system)
	if slice == null:
		_finish()
		return
	var fieldable: Dictionary = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var builder_rules: Dictionary = {}
	var manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var manifest: Dictionary = manifest_script.from_registries(
		FACTION,
		fieldable,
		content_db.call("get_playable_structure_runtimes")
			if content_db.has_method("get_playable_structure_runtimes") else {}
	)
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_rule: Dictionary = slice._faction_builder_unit_rule(String(builder_value))
		if not builder_rule.is_empty():
			builder_rules[String(builder_value)] = builder_rule
	slice.free()

	var object_id := "CreateAHero__%s" % String(profile.get("heroId", ""))
	_check(producible.has(object_id), "the created hero enters the faction's producible roster")
	if not producible.has(object_id):
		_finish()
		return
	var document: Dictionary = producible[object_id] as Dictionary
	var unit_type := Adapter.runtime_unit_id(document)
	var rule := Adapter.simulation_rule(document)
	_check(
		is_equal_approx(float(rule.get("innate_armor_scalar", 1.0)), float(stats["armorScalar"])),
		"the armour slider reaches the simulation rule as a damage-taken scalar"
	)
	_check(
		is_equal_approx(
			float(rule.get("auto_heal_multiplier", 1.0)), float(stats["autoHealMultiplier"])
		),
		"the self-heal slider reaches the simulation rule"
	)

	var sim = _match_sim(manifest, producible, builder_rules)
	if sim == null:
		_finish()
		return

	var fortress := int(sim.fortress_id(PLAYER_TEAM))
	_check(fortress != 0, "the player's fortress is seeded")
	var offered: Array = Array((sim.structure(fortress) as Dictionary).get("production", []))
	_check(offered.has(unit_type), "the fortress offers the created hero's build command")
	_check(
		int(sim.created_hero_owner_team(unit_type)) == PLAYER_TEAM,
		"the created hero is owned by the seat that made it"
	)

	# --- THE PURCHASE, through the deterministic command queue ---------------
	var accepted: bool = sim.submit_command({
		"tick": sim.tick_index + 1,
		"team": PLAYER_TEAM,
		"seq": 1,
		"type": "queue_unit",
		"args": {"producer": fortress, "unit_type": unit_type},
	})
	_check(accepted, "the hero purchase is accepted by the lockstep command queue")
	var before_ids: Array = sim.entity_ids()
	var resources_before := int(sim.resources_for_team(PLAYER_TEAM))
	sim.advance(2)
	var queue: Array = sim.production_queue_state(fortress)
	_check(queue.size() == 1, "the purchase reaches the fortress queue")
	_check(
		resources_before - int(sim.resources_for_team(PLAYER_TEAM)) == int(stats["buildCost"]),
		"the purchase charges the computed build cost (%d)" % int(stats["buildCost"])
	)

	var hero_id := _await_spawn(sim, unit_type, before_ids)
	_check(hero_id != 0, "the created hero spawns from the fortress")
	if hero_id == 0:
		_finish()
		return
	var hero: Dictionary = sim.entity(hero_id)
	_check(
		int(hero.get("member_maximum_health", 0)) == int(stats["health"]),
		"the spawned hero carries the health the slider produced (%d)" % int(stats["health"])
	)
	var expected_damage := maxi(1, int(round(
		float((baseline["combat"] as Dictionary).get("damage", 0.0))
		* float(stats["damageMultiplier"])
	)))
	_check(
		int(hero.get("member_damage", 0)) == expected_damage,
		"the spawned hero swings for weapon damage times the slider (%d)" % expected_damage
	)
	_check(
		is_equal_approx(
			float(hero.get("innate_armor_scalar", 1.0)), float(stats["armorScalar"])
		),
		"the spawned hero carries its innate armour scalar"
	)
	_check(
		is_equal_approx(
			float(hero.get("auto_heal_multiplier", 1.0)), float(stats["autoHealMultiplier"])
		),
		"the spawned hero carries its self-heal multiplier"
	)
	_check(int(hero.get("level", 0)) == 1, "the spawned hero enters at the authored rank 1")

	# --- ARMOUR IS REAL: the scalar actually mitigates a hit ----------------
	_test_armour_mitigates(sim, hero_id, float(stats["armorScalar"]))
	# --- SELF-HEAL IS REAL: the multiplier actually scales regeneration -----
	_test_regeneration_scales(sim, hero_id, float(stats["autoHealMultiplier"]))
	# --- A POWER FIRES ------------------------------------------------------
	_test_power_cast(sim, hero_id, unit_type)
	# --- XP LEVELS THE HERO AND LANDS ITS UPGRADE --------------------------
	_test_level_up(sim, hero_id)

	_finish()


func _write_profile(system: Dictionary) -> Dictionary:
	## A hero saved through the real persistence API, exactly as the MY HEROES
	## screen writes one, so the roster reads it back the way a player's would.
	var profile := CahHeroes.new_profile(system, "Beregond", CLASS_INDEX, SUB_CLASS_INDEX)
	profile["powers"] = [SPEAR_POWER]
	var refusals := CahHeroes.validate_profile(system, profile)
	_check(refusals.is_empty(), "the saved hero is legal against the mounted table: %s" % str(refusals))
	var error := CahHeroes.save_profile(profile)
	_check(error == "", "the hero saves through the real persistence API: %s" % error)
	if error != "":
		return {}
	_saved_hero_ids.append(String(profile.get("heroId", "")))
	return profile


func _report_baseline(system: Dictionary, profile: Dictionary, baseline: Dictionary) -> void:
	## THE MOUNTED PACK NOW STATES THE RETAIL NUMBERS, so this asserts that they
	## are the ones being used - not that a fallback happened to survive. The
	## values are read back out of the pack's own weapon row rather than typed in
	## here, so the check follows a rebalance instead of pinning one build; the
	## literals below it are the 2026-08-09 measured values, present so a silent
	## change of meaning is still caught.
	var limitations: Array = baseline.get("limitations", []) as Array
	for limitation in limitations:
		print("CAH_MATCH LIMITATION %s" % String(limitation))
	var combat: Dictionary = baseline["combat"] as Dictionary
	print(
		"CAH_MATCH BASELINE speed=%.1f damage=%.1f range=%.1f reload=%.0f preattack=%.0f weapon=%s"
		% [
			float(baseline["speed"]), float(combat.get("damage", 0.0)),
			float(combat.get("attackRange", 0.0)),
			float(combat.get("delayBetweenShotsMs", 0.0)),
			float(combat.get("preAttackDelayMs", 0.0)),
			String(baseline.get("weaponUpgradeName", "")),
		]
	)
	_check(
		not limitations.has(CahHeroes.LIMITATION_OBJECT_BASELINE)
			and not limitations.has(CahHeroes.LIMITATION_WEAPON_COMBAT),
		"the mounted pack states the object baseline and the weapon's combat"
	)
	var weapon_row := CahHeroes.weapon_appearance_row(
		system, profile.get("appearance", {}) as Dictionary
	)
	var authored: Dictionary = weapon_row.get("combat", {}) as Dictionary
	_check(
		is_equal_approx(float(combat.get("damage", -1.0)), float(authored.get("damage", -2.0)))
			and is_equal_approx(
				float(combat.get("attackRange", -1.0)), float(authored.get("attackRange", -2.0))
			)
			and is_equal_approx(
				float(combat.get("delayBetweenShotsMs", -1.0)),
				float(authored.get("delayBetweenShotsMs", -2.0))
			)
			and is_equal_approx(
				float(combat.get("firingDurationMs", -1.0)),
				float(authored.get("firingDurationMs", -2.0))
			),
		"the chosen weapon's own compiled damage, range, reload and firing time are used"
	)
	_check(
		is_equal_approx(
			float(combat.get("preAttackDelayMs", -1.0)), float(authored.get("preAttackMs", -2.0))
		),
		"the compiler's preAttackMs reaches the contract's preAttackDelayMs"
	)
	_check(
		is_equal_approx(
			float(baseline["speed"]),
			float(CahHeroes.object_baseline(system).get("speed", -1.0))
		)
			and is_equal_approx(CahHeroes.base_health_value(system), 2000.0),
		"the compiled object baseline drives speed and base health"
	)
	_check(
		is_equal_approx(float(combat.get("attackRange", 0.0)), 11.5)
			and is_equal_approx(float(combat.get("damage", 0.0)), 150.0)
			and is_equal_approx(float(baseline["speed"]), 50.0),
		"the retail numbers are the measured ones (range 11.5, damage 150, speed 50)"
	)


func _test_baseline_fallback(system: Dictionary, profile: Dictionary) -> void:
	## The live pack states the contract now, so the GRACEFUL path is no longer
	## exercised by simply running. It is kept honest here against a copy of the
	## mounted table with the contract stripped back out - what an older pack, or
	## a pack whose weapon compile failed, actually looks like.
	var stripped := system.duplicate(true)
	var registration: Dictionary = stripped["registration"] as Dictionary
	registration.erase("objectBaseline")
	(registration.get("system", {}) as Dictionary).erase("objectBaseline")
	for option_value in (registration.get("appearanceOptions", []) as Array):
		(option_value as Dictionary).erase("combat")
	var baseline := CahHeroes.combat_baseline(stripped, profile)
	var limitations: Array = baseline.get("limitations", []) as Array
	var combat: Dictionary = baseline["combat"] as Dictionary
	_check(
		limitations.has(CahHeroes.LIMITATION_OBJECT_BASELINE)
			and limitations.has(CahHeroes.LIMITATION_WEAPON_COMBAT),
		"a pack without the contract names both limitations"
	)
	_check(
		is_equal_approx(float(baseline["speed"]), CahHeroes.FALLBACK_SPEED)
			and is_equal_approx(
				float(combat.get("damage", 0.0)), float(CahHeroes.FALLBACK_COMBAT["damage"])
			)
			and is_equal_approx(
				float(combat.get("attackRange", 0.0)),
				float(CahHeroes.FALLBACK_COMBAT["attackRange"])
			),
		"a pack without the contract still fields a hero that moves and swings"
	)
	_check(
		is_equal_approx(CahHeroes.base_health_value(stripped), 2000.0),
		"base health falls back to the object's own MaxHealth"
	)


## The retail numbers the importer states once its baseline join lands: an
## objectBaseline on the system and a `combat` block on the weapon bling.
const CONTRACT_BASELINE := {
	"speed": 32.0,
	"baseHealth": 1000.0,
	"movement": {"acceleration": 99.0, "braking": 98.0, "turnRateDegreesPerSecond": 97.0},
}
const CONTRACT_WEAPON_COMBAT := {
	"damage": 275.0,
	"attackRange": 22.0,
	"minimumAttackRange": 1.0,
	"delayBetweenShotsMs": 900.0,
	"preAttackDelayMs": 100.0,
	"firingDurationMs": 200.0,
	"damageType": "HERO",
}


func _test_baseline_contract(system: Dictionary, profile: Dictionary) -> void:
	## THE OTHER HALF OF THE GRACEFUL FALLBACK. A pack that predates the retail
	## baseline join must field a hero on stated fallbacks; a pack that carries
	## it must be BELIEVED. Both halves are asserted here so neither can rot: the
	## match above proves the fallback path against the pack that is actually
	## mounted, and this proves the consuming path against the contract.
	var fixture := system.duplicate(true)
	var registration: Dictionary = fixture["registration"] as Dictionary
	registration["objectBaseline"] = CONTRACT_BASELINE.duplicate(true)
	var worn := String(
		(profile.get("appearance", {}) as Dictionary).get(
			CahHeroes.weapon_group_name(system), ""
		)
	)
	for option_value in (registration.get("appearanceOptions", []) as Array):
		var option := option_value as Dictionary
		if String(option.get("upgradeName", "")) == worn:
			option["combat"] = CONTRACT_WEAPON_COMBAT.duplicate(true)
	var baseline := CahHeroes.combat_baseline(fixture, profile)
	var combat: Dictionary = baseline["combat"] as Dictionary
	_check(
		(baseline["limitations"] as Array).is_empty(),
		"a pack carrying the baseline contract reports no limitation"
	)
	_check(
		is_equal_approx(float(baseline["speed"]), 32.0)
			and is_equal_approx(float(combat.get("damage", 0.0)), 275.0)
			and is_equal_approx(float(combat.get("attackRange", 0.0)), 22.0)
			and is_equal_approx(float(combat.get("delayBetweenShotsMs", 0.0)), 900.0),
		"the chosen weapon's compiled combat drives damage, range and reload"
	)
	_check(
		is_equal_approx(CahHeroes.base_health_value(fixture), 1000.0),
		"the object baseline drives base health"
	)
	var sub_row := CahHeroes.sub_class_row(fixture, CLASS_INDEX, SUB_CLASS_INDEX)
	var stats := CahHeroes.computed_stats(
		fixture, sub_row, profile.get("attributes", {}) as Dictionary,
		profile.get("powers", []) as Array
	)
	var document := CahHeroes.roster_document(fixture, profile, "MenFortress", 9)
	var simulation: Dictionary = (
		(document["registration"] as Dictionary)["simulation"] as Dictionary
	)
	var document_combat: Dictionary = simulation["combat"] as Dictionary
	_check(
		int(stats["health"]) == int(round(1000.0 * float(stats["healthMultiplier"])))
			and int(simulation["memberHealth"]) == int(stats["health"]),
		"the document's health is the contract base times the slider"
	)
	_check(
		int(document_combat["damage"]) == int(round(275.0 * float(stats["damageMultiplier"])))
			and is_equal_approx(float(simulation["speed"]), 32.0)
			and is_equal_approx(float(document_combat["attackRange"]), 22.0)
			and is_equal_approx(
				float((simulation["movement"] as Dictionary)["acceleration"]), 99.0
			),
		"the document carries the contract numbers in source units"
	)


func _test_created_heroes_survive_a_worker_thread(system: Dictionary) -> void:
	## THE SWEEP RUNS OFF THE TREE, ON A POOLED WORKER. Godot refuses node lookups
	## from a non-main thread, so a slice built there cannot ASK for the mounted
	## class table - it has to be handed it, exactly as the sweep is already
	## handed its unit, structure and pack-index snapshots. Before that hand-off
	## existed the classification simply found no table, and every created hero
	## was quietly missing from the availability answer the menu shows while the
	## game logged an error per faction.
	##
	## Both halves are pinned: injected, the hero is there; NOT injected, it is
	## not - so the plumbing cannot be tidied away as redundant.
	var outcome := {"injected": 0, "bare": 0}
	var worker := Thread.new()
	var started := worker.start(func() -> void:
		outcome["injected"] = _classify_on_this_thread(system)
		outcome["bare"] = _classify_on_this_thread({})
	)
	_check(started == OK, "the sweep worker thread starts")
	if started == OK:
		worker.wait_to_finish()
	_check(
		int(outcome["injected"]) >= 1,
		"a created hero reaches the roster from a worker thread off the scene tree (found %d)"
			% int(outcome["injected"])
	)
	_check(
		int(outcome["bare"]) == 0,
		"a worker that is handed no class table finds no created hero, which is why it is handed one"
	)


func _classify_on_this_thread(system: Dictionary) -> int:
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = slice_script.new()
	var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
	map_data.local_transform_scale = MAP_SCALE
	slice.source_map_data = map_data
	slice._classify_faction_units(FACTION, {}, {}, {}, null, system)
	var found := 0
	for object_id in (slice.producible_unit_runtimes as Dictionary).keys():
		if String(object_id).begins_with("CreateAHero__"):
			found += 1
	slice.free()
	return found


func _test_skirmish_ownership_follows_the_human(system: Dictionary) -> void:
	## THE HUMAN IS NOT ALWAYS ON ROW 0. A skirmish roster hands each row its own
	## sim team out of TEAM_ID_POOL, so a created hero owned by a hardcoded
	## "team 0" is offered to whichever AI happens to sit there and refused to
	## the player who made it. The owner is the team the HUMAN row carries.
	var script := GDScript.new()
	script.source_code = "extends Node\nvar retail_team_setup: Array = []\n"
	script.reload()
	var state := Node.new()
	state.set_script(script)
	state.set("retail_team_setup", [
		{"team": 0, "faction": FACTION, "controller": "ai"},
		{"team": 1, "faction": FACTION, "controller": "ai"},
		{"team": 3, "faction": FACTION, "controller": "human"},
	])
	var slice = _classified_slice(system, state)
	var created: Dictionary = slice.producible_unit_runtimes as Dictionary
	var owner_team := -99
	for object_id in created:
		if not String(object_id).begins_with("CreateAHero__"):
			continue
		var record: Dictionary = (
			((created[object_id] as Dictionary).get("registration", {}) as Dictionary)
				.get("createAHero", {}) as Dictionary
		)
		owner_team = int(record.get("ownerTeam", -99))
	_check(
		owner_team == 3,
		"a skirmish created hero is owned by the human's row team (got %d, expected 3)" % owner_team
	)
	slice.free()
	state.free()


func _classified_slice(system: Dictionary, game_state = null):
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = slice_script.new()
	var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
	map_data.local_transform_scale = MAP_SCALE
	slice.source_map_data = map_data
	# ONE classification, with the class table and the setup injected the way the
	# skirmish sweep injects them on its worker. Calling _add_created_heroes a
	# second time by hand would collide with the roster the first pass built.
	slice._classify_faction_units(FACTION, {}, {}, {}, game_state, system)
	return slice


func _match_sim(manifest: Dictionary, producible: Dictionary, builder_rules: Dictionary):
	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		# No starting armies: two idle fortresses cannot end the match under the
		# runner, and nothing walks into the hero mid-assertion.
		"spawn_initial_battalions": false,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_rules,
		"starting_resources": FLOOD_RESOURCES,
		"command_point_cap": FLOOD_COMMAND_POINTS,
		"source_map_transform_scale": MAP_SCALE,
	})
	_check(
		String(sim.configuration_error) == "",
		"the match configures with the created hero on the roster: %s" % String(sim.configuration_error)
	)
	if String(sim.configuration_error) != "":
		return null
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	_check(String(sim.configuration_error) == "", "the match sets up")
	return sim if String(sim.configuration_error) == "" else null


func _await_spawn(sim, unit_type: String, before_ids: Array) -> int:
	for _tick in range(6000):
		sim.tick()
		for entity_id in sim.entity_ids():
			if before_ids.has(entity_id):
				continue
			if String((sim.entity(entity_id) as Dictionary).get("unit_type", "")) == unit_type:
				return int(entity_id)
	return 0


func _test_armour_mitigates(sim, hero_id: int, armor_scalar: float) -> void:
	## The same raw hit, with and without the innate scalar, on the same hero.
	## Comparing the two is what proves the number is applied rather than merely
	## carried: an unread scalar makes both deltas identical.
	var hero: Dictionary = sim.entities[hero_id]
	var maximum := int(hero.get("member_maximum_health", 0))
	hero["member_health"] = [maximum]
	hero["health"] = maximum
	sim._apply_damage(0, hero_id, 1000)
	var scaled_loss := maximum - int(hero.get("health", 0))
	hero["member_health"] = [maximum]
	hero["health"] = maximum
	hero.erase("innate_armor_scalar")
	sim._apply_damage(0, hero_id, 1000)
	var raw_loss := maximum - int(hero.get("health", 0))
	hero["innate_armor_scalar"] = armor_scalar
	hero["member_health"] = [maximum]
	hero["health"] = maximum
	_check(
		raw_loss > 0 and scaled_loss > 0,
		"the hero can be damaged at all (raw=%d scaled=%d)" % [raw_loss, scaled_loss]
	)
	_check(
		scaled_loss == maxi(0, roundi(float(raw_loss) * armor_scalar)),
		"the armour scalar %.2f mitigates the hit (%d of %d)" % [armor_scalar, scaled_loss, raw_loss]
	)


func _test_regeneration_scales(sim, hero_id: int, auto_heal: float) -> void:
	var hero: Dictionary = sim.entities[hero_id]
	var maximum := int(hero.get("member_maximum_health", 0))
	var wounded := maxi(1, maximum / 2)
	hero["member_health"] = [wounded]
	hero["health"] = wounded
	# Out of combat: the regeneration gate is time since the last hit, so the
	# damage tick is pushed back rather than the gate being bypassed.
	hero["last_damage_tick"] = -1000000
	var before := int(hero.get("health", 0))
	sim.advance(10)
	var healed := int(hero.get("health", 0)) - before
	var per_tick := maxi(1, roundi(
		float(maximum) * SimScript.HERO_REGEN_PERCENT_PER_SECOND * auto_heal * SimScript.TICK_SECONDS
	))
	_check(healed > 0, "the hero regenerates out of combat (%d over ten ticks)" % healed)
	_check(
		healed == per_tick * 10,
		"the self-heal multiplier %.2f scales regeneration (%d, expected %d)"
			% [auto_heal, healed, per_tick * 10]
	)


func _test_power_cast(sim, hero_id: int, unit_type: String) -> void:
	## The selected power, cast at a living enemy through the sim's own cast
	## surface, with the enemy's health as the proof it fired.
	var rules: Array = sim.ability_rules_for_unit(unit_type)
	var spear: Dictionary = {}
	for rule_value in rules:
		if String((rule_value as Dictionary).get("ability_id", "")) == SPEAR_POWER:
			spear = rule_value as Dictionary
	_check(not spear.is_empty(), "the hero's selected power registers as an ability rule")
	if spear.is_empty():
		return
	_check(bool(spear.get("castable", false)), "the compiled power reports castable")
	var hero: Dictionary = sim.entities[hero_id]
	hero["stance"] = "HoldGround"
	var target_id := 990001
	var target_position := Vector2(hero.get("position", Vector2.ZERO)) + Vector2(1.0, 0.0)
	sim._add_battalion(
		target_id, ENEMY_TEAM, target_position, "Target", String(hero.get("object_id", "")),
		unit_type, 0
	)
	var target: Dictionary = sim.entities[target_id]
	target["member_health"] = [900000]
	target["health"] = 900000
	target["maximum_health"] = 900000
	target["member_maximum_health"] = 900000
	target.erase("innate_armor_scalar")
	var health_before := int(target.get("health", 0))
	# THE CAST GOES THROUGH THE COMMAND QUEUE too, so what is proved here is the
	# path a lockstep peer's click actually takes rather than a direct call the
	# network never makes.
	var submitted: bool = sim.submit_command({
		"tick": sim.tick_index + 1,
		"team": PLAYER_TEAM,
		"seq": 2,
		"type": "cast_ability",
		"args": {
			"hero_id": hero_id,
			"ability_id": SPEAR_POWER,
			"target_point": target_position,
		},
	})
	_check(submitted, "the cast is accepted by the lockstep command queue")
	sim.advance(1)
	var cast: Dictionary = sim.last_command_result if sim.last_command_result != null else {}
	_check(
		bool(cast.get("ok", false)),
		"the power casts at the enemy: %s" % String(cast.get("reason", ""))
	)
	_check(
		int(target.get("health", 0)) < health_before,
		"the cast damages the enemy (%d -> %d)" % [health_before, int(target.get("health", 0))]
	)
	_check(
		String(sim.cast_ability(hero_id, SPEAR_POWER, target_position, PLAYER_TEAM).get("reason", ""))
			== "cooldown-active",
		"the power honours its authored cooldown"
	)
	sim.entities.erase(target_id)


func _test_level_up(sim, hero_id: int) -> void:
	var hero: Dictionary = sim.entities[hero_id]
	var health_before := int(hero.get("member_maximum_health", 0))
	var damage_before := int(hero.get("member_damage", 0))
	sim._award_experience(hero, LEVEL_FOUR_XP)
	_check(
		int(hero.get("level", 0)) >= 4,
		"the authored thresholds level the hero through the XP pipeline (rank %d)"
			% int(hero.get("level", 0))
	)
	_check(
		int(hero.get("member_maximum_health", 0)) > health_before
			or int(hero.get("member_damage", 0)) > damage_before,
		"levelling folds the authored per-level stat effects"
	)
	_check(
		(hero.get("applied_upgrades", {}) as Dictionary).has(LEVEL_FOUR_UPGRADE),
		"the rank-4 level-up lands its authored upgrade (%s)" % LEVEL_FOUR_UPGRADE
	)


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("CAH_MATCH PASS %s" % label)
	else:
		failed += 1
		printerr("CAH_MATCH FAIL %s" % label)


func _finish() -> void:
	for hero_id in _saved_hero_ids:
		CahHeroes.delete_profile(hero_id)
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"CAH_MATCH FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("CAH_MATCH_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
