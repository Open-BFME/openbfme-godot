extends SceneTree
## Fortress command-surface gate (bugs A/B/C of the castle system).
##
## Retail oracle (PURE RotWK retail tree,
## .private/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##
##   * commandset.ini <Faction>FortressExpansionPad{Corner,Side}CommandSet gives
##     every fortress build plot that faction's own building menu — Angmar's
##     stone thrower / battle tower / kennel / wall hub (commandset.ini:3538),
##     Men's trebuchet / arrow tower / garrison dormitory / wall hub (:4094).
##   * commandset.ini <Faction>FortressCommandSet slots 8-14 are the fortress
##     improvement (OBJECT_UPGRADE) page — MordorFortressCommandSet DoomPyres /
##     LavaMoat / FireArrows / MagmaCauldrons / MorgulSorcery / GorgorothSpire
##     (:4655), AngmarFortressCommandSet Banners / Spikes / IceMunitions /
##     HouseOfLamentation / IceWalls / Sanctum (:3507). Each button buys a
##     *Trigger* upgrade that a `CastleUpgrade` behavior converts into the real
##     one (angmarfortress.ini:1282-1286).
##   * commandset.ini slots 15-24 are the hero revive page; the roster is
##     playertemplate.ini BuildableHeroesMP (FactionAngmar = Hwaldar, Karsh,
##     Morgramir, Rogash, Witchking).
##
## Sections
##   1. Fortress plots offer the authored building set (per faction).
##   2. Fortress hero roster is non-empty and matches the retail names.
##   3. The CastleUpgrade purchase path: a fortress improvement contract is
##      purchasable and its trigger hands out the real upgrade to the whole
##      castle. Driven by a fixture surface because no shipped pack carries the
##      castleUpgrades surface yet (see .private/scratch/opus16-fortress-report.md).
##
## Run:
##   OPENBFME_CONTENT=<repo>/.private/content-packs godot --headless --path game \
##     --script res://tests/fortress_command_surface_runner.gd
## One faction per process: OPENBFME_SLICE_FACTION selects it (default angmar).

const BOOT_DEADLINE_MS := 300000
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

## Retail expectations per faction, transcribed from the oracle cited above.
## `plots` are the runtime expansion kinds the pad command sets authorize;
## `heroes` are the BuildableHeroesMP roster entries (CreateAHero excluded — the
## create-a-hero system is its own lane).
const FACTION_EXPECTATIONS := {
	"angmar": {
		"plots": ["battletowerexpansion", "catapultexpansion", "kennelexpansion", "wall_hub_small_expansion"],
		"heroes": ["AngmarHwaldar", "AngmarKarsh", "AngmarMorgramir", "AngmarRogash", "AngmarWitchking"],
	},
	"men": {
		"plots": ["arrow_tower_expansion", "garrison_dormitory", "trebuchet_expansion", "trebuchet_side_expansion", "wall_hub_small_expansion"],
		"heroes": ["GondorAragornMP", "GondorBoromir", "GondorFaramir", "GondorGandalf", "RohanEomer", "RohanEowyn", "RohanTheoden"],
	},
	"wild": {
		"plots": ["arrowdenexpansion", "burrowsexpansion", "giantsentryexpansion", "spiderholesexpansion"],
		"heroes": ["WildAzog", "WildGoblinKing", "WildShelob"],
	},
}

var passed := 0
var failed := 0
var _faction := "angmar"


func _initialize() -> void:
	_runner_watchdog.start(self, "FORTRESS_COMMAND_SURFACE_RUNNER")
	_faction = OS.get_environment("OPENBFME_SLICE_FACTION").strip_edges().to_lower()
	if _faction == "":
		_faction = "angmar"
		OS.set_environment("OPENBFME_SLICE_FACTION", _faction)
	for env_name in ["OPENBFME_SLICE_MAP", "OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	call_deferred("_run")


func _run() -> void:
	_check_castle_upgrade_purchase_path()
	if not FACTION_EXPECTATIONS.has(_faction):
		print("RESULT fortress_surface faction=%s SKIPPED (no transcribed retail expectation)" % _faction)
		return _finish()
	var expectation: Dictionary = FACTION_EXPECTATIONS[_faction]

	root.size = Vector2i(1920, 1080)
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null:
		_check("slice_scene_loads", false, "res://scenes/retail_vertical_slice.tscn did not load")
		return _finish()
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("%s_slice_boots" % _faction, bool(slice.ready_ok), String(slice.failure_reason)):
		return _finish()

	var sim = slice.simulation
	var team := 0
	var fortress := int(sim.fortress_id(team))
	if not _check("%s_fortress_exists" % _faction, fortress != 0, "team 0 seeded no fortress"):
		return _finish()

	# --- Section 1: plots offer the authored building set ---------------------
	var pads: Array = sim.expansion_pad_states(fortress)
	_check("%s_fortress_has_pads" % _faction, not pads.is_empty(), "fortress unpacked no expansion pads")
	var offered: Array = []
	for kind_value in sim.expansion_commands_for(fortress):
		offered.append(String(kind_value))
	offered.sort()
	var expected_plots: Array = (expectation["plots"] as Array).duplicate()
	expected_plots.sort()
	_check(
		"%s_plots_offer_authored_buildings" % _faction,
		offered == expected_plots,
		"expected %s, got %s" % [str(expected_plots), str(offered)]
	)
	# Every offered kind must actually be buildable on a pad the retail command
	# set authorizes — an offer with no matching free pad is a phantom button.
	for kind_value in offered:
		var rule: Dictionary = sim._expansion_build_rules.get(String(kind_value), {})
		var pad_kinds: Array = rule.get("pad_kinds", [])
		_check(
			"%s_plot_%s_binds_a_pad_kind" % [_faction, String(kind_value)],
			not pad_kinds.is_empty(),
			"expansion '%s' records no pad kinds" % String(kind_value)
		)

	# --- Section 2: hero roster ----------------------------------------------
	var production: Array = (sim.structure(fortress) as Dictionary).get("production", [])
	var hud = slice.hud
	# Retail identity, not runtime slug: the HUD hero specs carry the authored
	# source object id (AngmarKarsh), which is what the retail roster names.
	var hero_sources: Array = []
	var hero_unit_ids: Dictionary = {}
	for spec_value in (hud._hero_command_specs if hud != null else []):
		var spec: Dictionary = spec_value
		if String(spec.get("producer_kind", "")) != "fortress":
			continue
		if not production.has(String(spec.get("unit_id", ""))):
			continue
		hero_sources.append(String(spec.get("source_object_id", "")))
		hero_unit_ids[String(spec.get("unit_id", ""))] = true
	hero_sources.sort()
	var expected_heroes: Array = (expectation["heroes"] as Array).duplicate()
	expected_heroes.sort()
	_check(
		"%s_fortress_hero_roster_non_empty" % _faction,
		not hero_sources.is_empty(),
		"fortress offered no heroes at all"
	)
	_check(
		"%s_fortress_hero_roster_matches_retail" % _faction,
		hero_sources == expected_heroes,
		"expected %s, got %s" % [str(expected_heroes), str(hero_sources)]
	)
	# The HUD must carry a button for each of them, or the roster is invisible.
	var hero_buttons: Dictionary = hud.hero_buttons if hud != null else {}
	var bound := 0
	for unit_id_value in hero_unit_ids.keys():
		if hero_buttons.has(String(unit_id_value)):
			bound += 1
	_check(
		"%s_fortress_hero_roster_is_bound_in_the_hud" % _faction,
		bound == hero_sources.size(),
		"%d of %d fortress heroes have a HUD button" % [bound, hero_sources.size()]
	)
	_finish()


func _check_castle_upgrade_purchase_path() -> void:
	## Fixture-driven: the shipped packs carry the CastleUpgrade *modules* but no
	## purchasable castleUpgrades surface (the compiler skips OBJECT_UPGRADE
	## buttons — playable_structure_compiler.py:1442), so this section proves the
	## runtime half end to end against an authored surface of the exact shape the
	## compiler must emit.
	var sim = SimScript.new()
	sim._rules = {
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
		"structure_castle_upgrades": {
			"fortress": {
				"upgrades": [{
					"upgradeId": "Upgrade_AngmarFortressIceWallsTrigger",
					"grantsUpgradeId": "Upgrade_AngmarFortressIceWalls",
					"cost": 500,
					"buildTimeSeconds": 30.0,
					"slot": 12,
					"commandId": "Command_PurchaseUpgradeAngmarFortressIceWalls",
					"labelId": "CONTROLBAR:AngmarFortressIceWalls",
					"tooltipId": "CONTROLBAR:ToolTipAngmarFortressIceWalls",
					"buttonImageId": "BAFortress_IceWalls",
				}],
			},
		},
	}
	sim.setup({}, {})
	sim.ai_enabled = false
	_check("castle_upgrade_surface_compiles", String(sim.configuration_error) == "", String(sim.configuration_error))
	var contracts: Dictionary = sim.structure_upgrade_contracts_for_team(0)
	var contract: Dictionary = contracts.get("Upgrade_AngmarFortressIceWallsTrigger", {})
	_check("castle_upgrade_registers_a_contract", not contract.is_empty(), "no contract for the trigger upgrade")
	_check(
		"castle_upgrade_contract_records_the_granted_upgrade",
		String(contract.get("grants_upgrade_id", "")) == "Upgrade_AngmarFortressIceWalls",
		str(contract)
	)

	# The trigger -> real upgrade hop itself, read from the same opaque-authored
	# moduleContracts row the packs already ship (angmarfortresscitadel.json,
	# module "CastleUpgrade", runtimeStatus "deferred").
	sim.register_castle_upgrade_grants("AngmarFortressCitadel", [{
		"module": "CastleUpgrade",
		"tag": "ModuleTag_PassOutAngmarStoneworkUpgrade",
		"fields": {
			"TriggeredBy": {"authored": "Upgrade_AngmarFortressIceWallsTrigger"},
			"Upgrade": {"authored": "Upgrade_AngmarFortressIceWalls"},
			"WallUpgradeRadius": {"authored": "ANGMAR_FORTRESS_WALL_EFFECTIVE_RADIUS"},
		},
	}])
	var grants: Array = sim.castle_upgrade_grants_for("Upgrade_AngmarFortressIceWallsTrigger")
	_check("castle_upgrade_module_indexes_the_grant", grants.size() == 1, str(grants))

	var fortress := int(sim.fortress_id(0))
	if not _check("castle_upgrade_fixture_has_a_fortress", fortress != 0, "no team-0 fortress"):
		return
	var building: Dictionary = sim.structures[fortress]
	building["structure_kind"] = "fortress"
	# One castle piece so the pass-out is observable beyond the fortress itself.
	var piece_id := 90001
	sim.structures[piece_id] = {
		"id": piece_id,
		"team": 0,
		"kind": "structure",
		"structure_kind": "castle_piece",
		"position": building.get("position", Vector2.ZERO),
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
	}
	building["castle_piece_structure_ids"] = [piece_id]

	var offered: Array = sim.structure_upgrade_commands(fortress)
	var offered_ids: Array = []
	for row_value in offered:
		offered_ids.append(String((row_value as Dictionary).get("upgrade_id", "")))
	_check(
		"castle_upgrade_is_offered_on_the_fortress",
		offered_ids.has("Upgrade_AngmarFortressIceWallsTrigger"),
		str(offered_ids)
	)

	sim.team_resources[0] = 10000
	var queued: Dictionary = sim.queue_structure_upgrade(0, fortress, "Upgrade_AngmarFortressIceWallsTrigger")
	_check("castle_upgrade_purchase_is_accepted", bool(queued.get("ok", false)), String(queued.get("reason", "")))
	var duration := int(contract.get("duration_ticks", 1))
	for _tick in range(duration + 2):
		sim.tick()
	var completed: Array = (sim.structure(fortress) as Dictionary).get("completed_upgrades", [])
	_check(
		"castle_upgrade_trigger_completes",
		completed.has("Upgrade_AngmarFortressIceWallsTrigger"),
		str(completed)
	)
	_check(
		"castle_upgrade_grants_the_real_upgrade_to_the_fortress",
		completed.has("Upgrade_AngmarFortressIceWalls"),
		str(completed)
	)
	var piece_completed: Array = (sim.structure(piece_id) as Dictionary).get("completed_upgrades", [])
	_check(
		"castle_upgrade_passes_the_real_upgrade_to_the_castle_pieces",
		piece_completed.has("Upgrade_AngmarFortressIceWalls"),
		str(piece_completed)
	)
	# The improvement must not blank the fortress's command set (it is not a
	# level chain step).
	_check(
		"castle_upgrade_does_not_swap_the_command_set",
		int((sim.structure(fortress) as Dictionary).get("level", 1)) == 1,
		"fortress level moved on a non-level improvement"
	)

## Minimal synthetic unit rule (the shape the script-world harness uses) so the
## fixture sim boots its base loop without a converted pack.
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


func _check(name: String, ok: bool, detail: String) -> bool:
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	print("RESULT fortress_command_surface faction=%s passed=%d failed=%d" % [_faction, passed, failed])
	quit(0 if failed == 0 else 1)
