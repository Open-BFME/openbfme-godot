extends SceneTree

## The last link of the science chain, closed and PROVEN: the retail AI, run
## through the production seam (sim.tick() stepping a wired executor over the
## decoded skirmish libraries), EARNS power points from combat its own units
## win and SPENDS them on a science - PLAYER_PURCHASE_SCIENCE executes, the
## science is held afterwards, and the points are gone. All three asserted,
## because any one alone can pass while the transaction is half-done.
##
## Where this differs from retail_script_census_runner.gd (the combat-less
## control, deliberately left untouched):
##
##   1. THE PRODUCTION SPELLBOOK SHAPE. The census configures no spellbook
##      document; production does (retail_vertical_slice.gd
##      _configure_simulation_spellbook). This runner configures the selected
##      pack's own Men openbfme.spellbook-runtime document, resolved
##      pack-root-scoped exactly like retail_spellbook_runner.gd, and SKIPS
##      LOUDLY when the mounted pack carries none - the stale durable
##      fallback pack ships no spellbook, and a green run against it would be
##      the confidently-wrong-numbers failure mode this codebase exists to
##      prevent. The pack identity the run actually loaded is printed either
##      way.
##
##   2. COMBAT THAT EARNS THE POINTS. SCIENCE_Heal / SCIENCE_RallyingCall
##      cost 5 power points (from the doc, not assumed); setup() seeds 1; the
##      kill economy pays 1 point per POWER_POINT_KILLS(=3) battalion kills
##      (award_power_kill, the same path a live match uses). So the shortest
##      honest path to affordability is 12 enemy battalion kills. This
##      scenario fields them: twelve enemy battalions and twenty-four
##      attackers meet at midfield, and every point the AI spends was banked
##      by a battalion.defeated -> award_power_kill event inside the tick
##      loop. NO POINTS ARE SEEDED beyond the representative setup() single
##      point, and the kill rate and cost are the production defaults.
##
## The purchase itself is the RETAIL SCRIPT'S decision: "Men of the West
## Spell List Choice" draws from the sim-owned logic random stream (55f0d87),
## selecting spell list 1, 2 or 3; each list's lead purchase script gates on
## COUNTER == n AND PLAYER_CAN_PURCHASE_SCIENCE and then fires
## PLAYER_PURCHASE_SCIENCE for a 5-point tier-1 science (lists 1 and 2 lead
## with SCIENCE_Heal, list 3's live variants with SCIENCE_RallyingCall).
## This runner does not care which list the stream picks - it asserts the
## purchase completed and reconciles every point against the event log.
##
## DIAGNOSTIC-GATED like the census: SKIPs (exit 0, loudly, verifying
## NOTHING) when the script contract or the pack spellbook is absent. When
## both are present, every check is a hard assertion (exit 1 on failure).
##
## Invocation (real pack REQUIRED for a meaningful run):
##   OPENBFME_CONTENT=<men-pack bundle root> \
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_script_purchase_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const CONTRACT_RELATIVE_PATH := ".private/retail-work/reports/skirmish-script-contract/skirmish_script_contract.json"
const RUN_TICKS := 600
## Combat begins AFTER the purchase scripts' first post-counter evaluation
## window (evaluationInterval 10s = 100 ticks), so the run provably contains
## both answers of the affordability gate: an evaluation that finds the team
## POOR (1 seeded point, counter already drawn) and, after the kills bank the
## points, one that finds it rich and buys. Without this window a broken
## affordability check is invisible - the purchase tick would not move.
const ATTACK_START_TICK := 150
const SPELL_CHOICE_COUNTER := "Men of the West Spell List Choice"

const PLAYER := "CensusPlayer"
const ENEMY := "CensusEnemy"

## 12 kills at the production rate of 3 kills/point buys the 4 points that,
## with the 1 point setup() seeds, afford the 5-point tier-1 science.
const VICTIM_COUNT := 12
const ATTACKERS_PER_VICTIM := 2

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var contract_path := _contract_path()
	if not FileAccess.file_exists(contract_path):
		print("RETAIL_SCRIPT_PURCHASE SKIP contract absent at %s - THIS RUN VERIFIED NOTHING" % contract_path)
		quit(0)
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(contract_path))
	if typeof(parsed) != TYPE_DICTIONARY:
		printerr("RETAIL_SCRIPT_PURCHASE FAIL contract JSON did not parse")
		quit(1)
		return

	# --- Pack identity, CONFIRMED before anything is concluded. The stale
	# durable fallback pack ships no spellbook and has produced confidently
	# wrong results before (the known trap 55f0d87 names). Print what actually
	# mounted, then gate on the spellbook it carries.
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		print("RETAIL_SCRIPT_PURCHASE SKIP ContentDB autoload absent - THIS RUN VERIFIED NOTHING")
		quit(0)
		return
	var soldier_definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var pack_root := String(soldier_definition.get("_pack_root", ""))
	print("PACK_IDENTITY root=%s" % (pack_root if pack_root != "" else "<no pack resolved gondor-fighter>"))
	var spellbook_doc := _men_spellbook_document(content_db, pack_root)
	var spellbook_faction := String((spellbook_doc.get("target", {}) as Dictionary).get("faction", ""))
	print("PACK_IDENTITY spellbook_faction=%s sciences=%d" % [
		spellbook_faction if spellbook_faction != "" else "<none>",
		(((spellbook_doc.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary).get("sciences", []) as Array).size(),
	])
	if pack_root == "" or spellbook_doc.is_empty() or spellbook_faction != "Men":
		# LOUD SKIP, never a vacuous pass: without the production spellbook
		# shape the purchase chain cannot be exercised, and a green result
		# here would test nothing. This is the stale-durable-pack signature.
		print("RETAIL_SCRIPT_PURCHASE SKIP mounted pack carries no Men spellbook document (root=%s) - THIS RUN VERIFIED NOTHING; point OPENBFME_CONTENT at the real men pack" % (pack_root if pack_root != "" else "<none>"))
		quit(0)
		return

	# --- Sim, wired exactly like the census, plus the two production pieces
	# the census leaves out: the spellbook document and combat.
	var sim = _make_sim()
	_check("spellbook_configures_from_pack_doc",
		sim.configure_spellbook_runtime(spellbook_doc) and sim.spellbook_error() == "",
		sim.spellbook_error())
	sim.setup({}, {})
	sim.ai_enabled = true
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	_check("spellbook_survives_setup", sim.spellbook_available(), sim.spellbook_error())
	_check("seeded_points_are_one_not_five",
		sim.power_points(SimScript.PLAYER_TEAM) == 1,
		str(sim.power_points(SimScript.PLAYER_TEAM)))

	var world = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(PLAYER)
	var executor = ExecutorScript.new(world)

	var loaded := 0
	for source_value in (parsed as Dictionary).get("sources", []) as Array:
		for row_value in (source_value as Dictionary).get("scripts", []) as Array:
			var payload: Dictionary = (row_value as Dictionary).get("payload", {})
			if not payload.is_empty():
				executor.load_script_payload(payload)
				loaded += 1
	_check("contract_scripts_loaded", loaded > 0, str(loaded))
	if not sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM) \
			or not sim.register_script_executor(executor, SimScript.PLAYER_TEAM):
		printerr("RETAIL_SCRIPT_PURCHASE FAIL wiring refused")
		quit(1)
		return

	# --- The tick loop: combat and scripts advance together, exactly like a
	# live match. Attack orders are re-issued only for attackers that have
	# gone idle while their victim lives (deterministic scenario control - a
	# fixed cadence over sorted ids, no randomness).
	for tick_number in RUN_TICKS:
		if tick_number >= ATTACK_START_TICK and tick_number % 10 == 0:
			_press_attacks(sim)
		sim.tick()

	# --- The evidence, reconciled from the sim's own event log.
	var earned := 0
	var kills_of_enemy := 0
	var purchases: Array[Dictionary] = []
	var point_earn_sequences: Array[int] = []
	for event_value in sim.events:
		var event := event_value as Dictionary
		match String(event.get("kind", "")):
			"power.point_earned":
				if int(event.get("team", -1)) == SimScript.PLAYER_TEAM:
					earned += 1
					point_earn_sequences.append(int(event.get("sequence", 0)))
			"power.purchased":
				if int(event.get("team", -1)) == SimScript.PLAYER_TEAM:
					purchases.append(event)
			"battalion.defeated":
				if int(event.get("team", -1)) == SimScript.ENEMY_TEAM:
					kills_of_enemy += 1

	print("PURCHASE_CENSUS ticks=%d scripts=%d kills_of_enemy=%d points_earned=%d points_final=%d" % [
		RUN_TICKS, loaded, kills_of_enemy, earned, sim.power_points(SimScript.PLAYER_TEAM),
	])
	print("PURCHASE_CENSUS spell_choice_counter=%d logic_random_draws=%d" % [
		executor.env.counter(SPELL_CHOICE_COUNTER), sim.logic_random_draws,
	])
	print("EXECUTED condition SKIRMISH_PLAYER_FACTION=%d" % int(executor.condition_executions.get("SKIRMISH_PLAYER_FACTION", 0)))
	print("EXECUTED condition PLAYER_CAN_PURCHASE_SCIENCE=%d" % int(executor.condition_executions.get("PLAYER_CAN_PURCHASE_SCIENCE", 0)))
	print("EXECUTED action PLAYER_PURCHASE_SCIENCE=%d" % int(executor.action_executions.get("PLAYER_PURCHASE_SCIENCE", 0)))
	for purchase in purchases:
		print("PURCHASED tick=%d power=%s science=%s cost=%d" % [
			int(purchase.get("tick", -1)), String(purchase.get("power_id", "")),
			String(purchase.get("science_id", "")), int(purchase.get("cost", 0)),
		])

	# 1. The combat economy did its part: at least the 4 points the purchase
	# needs were EARNED from kills, none seeded.
	_check("combat_killed_enough_battalions", kills_of_enemy >= VICTIM_COUNT, str(kills_of_enemy))
	_check("points_earned_from_kills", earned >= 4, str(earned))

	# 2. The chain reached the pair: the faction gate answered, the counter
	# drew, the affordability condition executed against the real tree.
	_check("spell_choice_counter_drawn",
		executor.env.counter(SPELL_CHOICE_COUNTER) >= 1 and executor.env.counter(SPELL_CHOICE_COUNTER) <= 3,
		str(executor.env.counter(SPELL_CHOICE_COUNTER)))
	_check("can_purchase_science_executed",
		int(executor.condition_executions.get("PLAYER_CAN_PURCHASE_SCIENCE", 0)) > 0,
		str(int(executor.condition_executions.get("PLAYER_CAN_PURCHASE_SCIENCE", 0))))

	# 3. THE PURCHASE COMPLETED - all three legs, because any one alone can
	# pass while the transaction is half-done.
	# 3a. The action executed.
	_check("player_purchase_science_executed",
		int(executor.action_executions.get("PLAYER_PURCHASE_SCIENCE", 0)) >= 1,
		str(int(executor.action_executions.get("PLAYER_PURCHASE_SCIENCE", 0))))
	_check("purchase_event_recorded", not purchases.is_empty(), str(purchases.size()))
	# 3b. The science is held afterwards (and it is a real purchase, not the
	# intrinsic faction science every match starts with).
	var owned: Array = sim.owned_sciences(SimScript.PLAYER_TEAM)
	var held_ok := not purchases.is_empty()
	var power_held_ok := not purchases.is_empty()
	var non_intrinsic_ok := not purchases.is_empty()
	for purchase in purchases:
		var science_id := String(purchase.get("science_id", ""))
		var power_id := String(purchase.get("power_id", ""))
		if not owned.has(science_id):
			held_ok = false
		if not sim.has_power(SimScript.PLAYER_TEAM, power_id):
			power_held_ok = false
		if science_id == "SCIENCE_MEN" or science_id == "":
			non_intrinsic_ok = false
	_check("purchased_science_now_held", held_ok, str(owned))
	_check("purchased_power_now_held", power_held_ok, str(sim.purchased_powers.get(SimScript.PLAYER_TEAM, [])))
	_check("purchase_is_not_the_intrinsic_science", non_intrinsic_ok, str(purchases))
	# 3c. The points were actually spent: every point reconciles against the
	# event log (1 seeded + earned - spent == held), the spend is at least the
	# tier-1 cost, and the balance never went negative.
	var spent := 0
	for purchase in purchases:
		spent += int(purchase.get("cost", 0))
	var final_points: int = sim.power_points(SimScript.PLAYER_TEAM)
	_check("points_actually_spent", spent >= 5, str(spent))
	_check("points_reconcile_seed_plus_earned_minus_spent",
		final_points == 1 + earned - spent and final_points >= 0,
		"final=%d seeded=1 earned=%d spent=%d" % [final_points, earned, spent])
	# 3d. Affordability held AT the purchase: by the time the first purchase
	# was minted, enough point-earn events had already happened that seed(1) +
	# earned-so-far covered its cost. Reddens if the affordability check is
	# broken (a purchase before the kills paid for it).
	if not purchases.is_empty():
		var first_purchase := purchases[0]
		var first_sequence := int(first_purchase.get("sequence", 0))
		var earned_before := 0
		for sequence in point_earn_sequences:
			if sequence < first_sequence:
				earned_before += 1
		_check("purchase_waited_for_affordability",
			1 + earned_before >= int(first_purchase.get("cost", 0)),
			"earned_before=%d cost=%d" % [earned_before, int(first_purchase.get("cost", 0))])

	print("RETAIL_SCRIPT_PURCHASE RESULT passed=%d failed=%d" % [passed, failed])
	if failed > 0:
		quit(1)
		return
	quit(0)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		printerr("FAIL %s%s" % [name, "" if detail == "" else " (%s)" % detail])


func _press_attacks(sim) -> void:
	## Deterministic order pressure: each attacker that is idle while its
	## paired victim lives is re-ordered onto it. Ids ascend, so peers (and
	## reruns) issue identical command sequences.
	for pair_index in VICTIM_COUNT:
		var victim_id := 200 + pair_index
		if not sim.entities.has(victim_id) or int((sim.entities[victim_id] as Dictionary).get("health", 0)) <= 0:
			continue
		for attacker_offset in ATTACKERS_PER_VICTIM:
			var attacker_id := 300 + pair_index * ATTACKERS_PER_VICTIM + attacker_offset
			if not sim.entities.has(attacker_id):
				continue
			var attacker := sim.entities[attacker_id] as Dictionary
			if int(attacker.get("health", 0)) <= 0 or int(attacker.get("target_id", 0)) != 0:
				continue
			var ids: Array[int] = [attacker_id]
			sim.issue_attack(ids, victim_id, SimScript.PLAYER_TEAM)


func _contract_path() -> String:
	var override := OS.get_environment("OPENBFME_SCRIPT_CONTRACT").strip_edges()
	if override != "":
		return override
	var game_root := ProjectSettings.globalize_path("res://")
	return game_root.rstrip("/").get_base_dir().path_join(CONTRACT_RELATIVE_PATH)


func _men_spellbook_document(content_db, pack_root: String) -> Dictionary:
	## Pack-root-scoped resolution, mirroring retail_spellbook_runner.gd: the
	## registry spellbook slot is last-pack-wins across mounted packs, and the
	## Men gate needs the ACTIVE pack's own Men document.
	if pack_root == "":
		return {}
	var mod_loader = root.get_node_or_null("ModLoader")
	if mod_loader == null:
		return {}
	var pack_document := mod_loader._read_json(pack_root.path_join("pack.json")) as Dictionary
	var files: Dictionary = pack_document.get("files", {}) as Dictionary
	var keys: Array[String] = []
	for key_value in files.keys():
		if String(key_value).begins_with("spellbook."):
			keys.append(String(key_value))
	keys.sort()
	for key in keys:
		var relative := String(files.get(key, ""))
		if relative == "":
			continue
		var document := mod_loader._read_json(mod_loader.resolve_pack_path(pack_root, relative)) as Dictionary
		if document.is_empty() or String(document.get("schema", "")) != "openbfme.spellbook-runtime":
			continue
		if String((document.get("target", {}) as Dictionary).get("faction", "")) == "Men":
			return document
	return {}


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	# Factions on the roster in the PRODUCTION descriptor shape (lowercase
	# pack faction ids, translated by the sim through the injected
	# retail_faction_sides table) - the census's hard-won rule, kept.
	sim.configure_team_roster([
		{"team": SimScript.PLAYER_TEAM, "faction": "men", "is_ai": false},
		{"team": SimScript.ENEMY_TEAM, "faction": "isengard", "is_ai": false},
	])
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"retail_faction_sides": ManifestScript.retail_faction_sides(),
		# The combat scenario, injected through the manifest seam setup()
		# actually reads (_apply_gameplay_rules -> _configure_faction_manifest
		# resets the roster from this key every setup; a directly-poked
		# _spawn_roster does not survive). Rules are match configuration on
		# every peer and the roster is snapshotted ("spawn_roster"), so
		# nothing here leaks outside the hash boundary.
		"faction_manifest": {"spawn_roster": _battle_roster()},
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _battle_roster() -> Array:
	## The default roster plus a midfield battle line: twelve enemy battalions,
	## each paired with two attackers spawned 7.0 units away - OUTSIDE the
	## scenario vision range of 3.0, so idle auto-acquisition cannot start the
	## fight early; combat begins when the explicit attack orders land at
	## ATTACK_START_TICK, and the kills that fund the purchase resolve through
	## the sim's own combat and award_power_kill path - the same events a live
	## match banks points from. The ENEMY roster descriptor sets is_ai false
	## so the built-in AI controller does not march the victims home
	## mid-fight; the AI UNDER TEST is the script executor on PLAYER_TEAM,
	## which runs regardless.
	var roster: Array = (SimScript.DEFAULT_SPAWN_ROSTER as Array).duplicate(true)
	for pair_index in VICTIM_COUNT:
		var line_y := -11.5 + 2.0 * pair_index
		roster.append({
			"id": 200 + pair_index, "team": SimScript.ENEMY_TEAM,
			"position": Vector2(0.5, line_y),
			"name": "Purchase Victim %d" % pair_index,
			"object_id": SimScript.SOLDIER_OBJECT_ID, "unit_type": SimScript.SOLDIER_HORDE_ID,
		})
		for attacker_offset in ATTACKERS_PER_VICTIM:
			roster.append({
				"id": 300 + pair_index * ATTACKERS_PER_VICTIM + attacker_offset,
				"team": SimScript.PLAYER_TEAM,
				"position": Vector2(-6.5, line_y + (0.4 if attacker_offset == 1 else -0.4)),
				"name": "Purchase Attacker %d.%d" % [pair_index, attacker_offset],
				"object_id": SimScript.SOLDIER_OBJECT_ID, "unit_type": SimScript.SOLDIER_HORDE_ID,
			})
	return roster


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
		# Vision is deliberately SHORT (the census uses 40.0): idle battalions
		# auto-acquire anything hostile inside vision, and with map-wide sight
		# the battle would start at tick 0 no matter when the attack orders
		# land - erasing the poor-evaluation window the affordability
		# assertion needs.
		"vision_range": 3.0,
		"vision_range_source": 30.0,
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
