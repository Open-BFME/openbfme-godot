extends SceneTree

## Cross-platform state pin for selected-pack ordinary projectile combat.
##
## Unlike retail_state_pin_runner.gd, this frozen fixture MUST observe real
## projectiles and damage before accepting its final state hash. It binds the
## exact selected Men pack and derives every combat-facing fixture constant from
## its compiled playable-unit documents:
##
##   rotwk-men-vslice/51d4885433869fa6290498eeac597b8cc8ac79e540d073dbf03c9c0d0184df3c/
##     data/playable-units/gondorarcherhorde.json
##       objectId GondorArcherHorde; primary member GondorArcher; attack range
##       300; pre-attack 1000 ms; projectile GondorArcherArrow at 321 units/s;
##       DamageNugget 35 PIERCE at weapon.ini:4317.
##     data/playable-units/gondortrebuchet.json
##       objectId GondorTrebuchet; attack range 500; pre-attack 1200 ms;
##       firing duration 5400 ms; projectile GondorTrebuchetRockProjectile at
##       321 units/s; DamageNugget 600 SIEGE, radius 100, taper 50 at
##       weapon.ini:3165.
##     data/playable-units/gondorfighterhorde.json
##       objectId GondorFighterHorde; primary member GondorFighter; member
##       health 200 and count 15 (the source-backed durable targets).
##
## Positions use identity source scale. The archer and trebuchet targets are
## therefore exactly one authored attack range away (300 and 500). The two
## splash targets sit radius/2 = 50 from the trebuchet impact. The trebuchet
## lane is separated on Y by its authored range (500), outside the archer and
## fighter vision/range lanes. PIN_TICKS is the authored 12-tick pre-attack +
## 54-tick firing duration + ceil(500 / 321 / 0.1) = 16 flight ticks = 82.
##
## Fixed seed: logic_random_seed=0, retail_slice_sim.gd's documented default.
##
## EXPECTED_HASH measured twice on the unmodified simulation on 2026-08-20.
## Both independent runs produced 709def7c... with identical coverage totals:
##   workspace/logs/q16-new-pin-measure-1.txt
##   workspace/logs/q16-new-pin-measure-2.txt
## A differing hash after mint is a defect, never an invitation to refresh the
## value silently.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const WatchdogScript = preload("res://tests/runner_watchdog.gd")

const SELECTED_MEN_PACK := "rotwk-men-vslice/51d4885433869fa6290498eeac597b8cc8ac79e540d073dbf03c9c0d0184df3c"
const ARCHER_SOURCE_ID := "GondorArcherHorde"
const TREBUCHET_SOURCE_ID := "GondorTrebuchet"
const TARGET_SOURCE_ID := "GondorFighterHorde"
const ARCHER_DESCRIPTOR_SHA256 := "2f879111412cd7f0502158591e70f63578ae629c08fdf7bef3cd227d9bbb4003"
const TREBUCHET_DESCRIPTOR_SHA256 := "557026cbaf69dbc9fa7c275a4c164d9a0a9d971625b730c65db4aa295994a7bc"
const TARGET_DESCRIPTOR_SHA256 := "a9ffc68f41ea23ae55cbf8021afd9d5a55091ed195064472d8a64c568cb83836"

const SOURCE_SCALE := 1.0
const ARCHER_RANGE_SOURCE := 300.0
const TREBUCHET_RANGE_SOURCE := 500.0
const TREBUCHET_RADIUS_SOURCE := 100.0
const SPLASH_OFFSET_SOURCE := TREBUCHET_RADIUS_SOURCE / 2.0
const ARCHER_POSITION := Vector2.ZERO
const ARCHER_TARGET_POSITION := Vector2(ARCHER_RANGE_SOURCE, 0.0)
const TREBUCHET_POSITION := Vector2(0.0, TREBUCHET_RANGE_SOURCE)
const TREBUCHET_TARGET_POSITION := Vector2(TREBUCHET_RANGE_SOURCE, TREBUCHET_RANGE_SOURCE)
const SPLASH_TARGET_A_POSITION := Vector2(TREBUCHET_RANGE_SOURCE, TREBUCHET_RANGE_SOURCE + SPLASH_OFFSET_SOURCE)
const SPLASH_TARGET_B_POSITION := Vector2(TREBUCHET_RANGE_SOURCE, TREBUCHET_RANGE_SOURCE - SPLASH_OFFSET_SOURCE)
const PIN_TICKS := 82
const LOGIC_RANDOM_SEED := 0

## Harness ids retain retail_slice_sim.gd DEFAULT_SPAWN_ROSTER's established
## id band; the object identities and rules on every row come from the pack.
const TREBUCHET_ID := 1
const ARCHER_ID := 2
const ARCHER_TARGET_ID := 101
const TREBUCHET_TARGET_ID := 102
const SPLASH_TARGET_A_ID := 103
const SPLASH_TARGET_B_ID := 104

const EXPECTED_HASH := "709def7cadaf6c91079697a343f437f71d6a2b10d71238248f57b171f8486e7f"

var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_PROJECTILE_PIN")
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_fail("ContentDB autoload is unavailable")
		return

	var archer_document := _selected_document(content_db, ARCHER_SOURCE_ID, ARCHER_DESCRIPTOR_SHA256)
	var trebuchet_document := _selected_document(content_db, TREBUCHET_SOURCE_ID, TREBUCHET_DESCRIPTOR_SHA256)
	var target_document := _selected_document(content_db, TARGET_SOURCE_ID, TARGET_DESCRIPTOR_SHA256)
	if archer_document.is_empty() or trebuchet_document.is_empty() or target_document.is_empty():
		return

	var archer_rule := _normalized_rule(archer_document, ARCHER_SOURCE_ID)
	var trebuchet_rule := _normalized_rule(trebuchet_document, TREBUCHET_SOURCE_ID)
	var target_rule := _normalized_rule(target_document, TARGET_SOURCE_ID)
	if archer_rule.is_empty() or trebuchet_rule.is_empty() or target_rule.is_empty():
		return
	if not _rules_match_authored_fixture(archer_rule, trebuchet_rule, target_rule):
		return

	var sim = SimScript.new()
	sim.setup({}, {
		"spawn_initial_battalions": false,
		"logic_random_seed": LOGIC_RANDOM_SEED,
		# This combat-only pin seeds no structures. Restrict the legacy fallback
		# manifest to its one compiled armor kind so unused farm/barracks kinds do
		# not emit the known Q11 provisional-armor errors.
		"faction_manifest": {
			"structure_kinds": ["fortress"],
			"seed_structure_kinds": ["fortress"],
			"spawn_roster": [],
		},
		"unit_rules": {
			String(archer_rule["object_id"]): archer_rule,
			String(trebuchet_rule["object_id"]): trebuchet_rule,
			String(target_rule["object_id"]): target_rule,
		},
	})
	sim.ai_enabled = false
	if String(sim.configuration_error) != "":
		_fail("simulation configuration failed: %s" % String(sim.configuration_error))
		return

	_add_source_battalion(sim, TREBUCHET_ID, 0, TREBUCHET_POSITION, trebuchet_rule)
	_add_source_battalion(sim, ARCHER_ID, 0, ARCHER_POSITION, archer_rule)
	_add_source_battalion(sim, ARCHER_TARGET_ID, 1, ARCHER_TARGET_POSITION, target_rule)
	_add_source_battalion(sim, TREBUCHET_TARGET_ID, 1, TREBUCHET_TARGET_POSITION, target_rule)
	_add_source_battalion(sim, SPLASH_TARGET_A_ID, 1, SPLASH_TARGET_A_POSITION, target_rule)
	_add_source_battalion(sim, SPLASH_TARGET_B_ID, 1, SPLASH_TARGET_B_POSITION, target_rule)
	if sim.entities.size() != 6:
		_fail("source-backed fixture spawned %d/6 battalions" % sim.entities.size())
		return

	var initial_health := {}
	for target_id in [ARCHER_TARGET_ID, TREBUCHET_TARGET_ID, SPLASH_TARGET_A_ID, SPLASH_TARGET_B_ID]:
		initial_health[target_id] = int((sim.entities[target_id] as Dictionary).get("health", 0))

	if sim.issue_attack([ARCHER_ID] as Array[int], ARCHER_TARGET_ID, 0) != 1:
		_fail("selected-pack Gondor Archer attack order was rejected")
		return
	if sim.issue_attack([TREBUCHET_ID] as Array[int], TREBUCHET_TARGET_ID, 0) != 1:
		_fail("selected-pack Gondor Trebuchet attack order was rejected")
		return

	var saw_projectiles := false
	var saw_archer_projectile := false
	var saw_trebuchet_projectile := false
	var saw_component_rows := false
	var saw_radius_component := false
	var maximum_projectiles := 0
	for _tick in range(PIN_TICKS):
		sim.tick()
		maximum_projectiles = maxi(maximum_projectiles, sim.projectiles.size())
		if sim.projectiles.is_empty():
			continue
		saw_projectiles = true
		for projectile_value in sim.projectiles.values():
			var projectile := projectile_value as Dictionary
			var projectile_object_id := String(projectile.get("projectile_object_id", ""))
			saw_archer_projectile = saw_archer_projectile or projectile_object_id == "GondorArcherArrow"
			saw_trebuchet_projectile = saw_trebuchet_projectile or projectile_object_id == "GondorTrebuchetRockProjectile"
			var components := projectile.get("damage_components", []) as Array
			saw_component_rows = saw_component_rows or not components.is_empty()
			for component_value in components:
				if float((component_value as Dictionary).get("radius", 0.0)) > 0.0:
					saw_radius_component = true

	if not saw_projectiles or not saw_archer_projectile or not saw_trebuchet_projectile:
		_fail("fixture went quiet: saw=%s archer=%s trebuchet=%s max=%d" % [saw_projectiles, saw_archer_projectile, saw_trebuchet_projectile, maximum_projectiles])
		return
	if not saw_component_rows or not saw_radius_component:
		_fail("projectile rows did not carry compiled damage components/radius")
		return
	if not _damaged(sim, ARCHER_TARGET_ID, initial_health):
		_fail("Gondor Archer projectile dealt no damage")
		return
	if not _damaged(sim, TREBUCHET_TARGET_ID, initial_health):
		_fail("Gondor Trebuchet projectile dealt no direct damage")
		return
	if not _damaged(sim, SPLASH_TARGET_A_ID, initial_health) or not _damaged(sim, SPLASH_TARGET_B_ID, initial_health):
		_fail("authored trebuchet radius failed to damage both radius/2 targets")
		return

	var impact_ids := {}
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "combat.projectile_impact":
			impact_ids[String(event.get("projectile_object_id", ""))] = true
	if not impact_ids.has("GondorArcherArrow") or not impact_ids.has("GondorTrebuchetRockProjectile"):
		_fail("projectile impact events missing: %s" % str(impact_ids.keys()))
		return

	var hash := String(sim.state_hash())
	print("RETAIL_PROJECTILE_PIN_COVERAGE ticks=%d max_projectiles=%d archer_damage=%d trebuchet_damage=%d splash_a_damage=%d splash_b_damage=%d" % [
		PIN_TICKS,
		maximum_projectiles,
		int(initial_health[ARCHER_TARGET_ID]) - int((sim.entities[ARCHER_TARGET_ID] as Dictionary).get("health", 0)),
		int(initial_health[TREBUCHET_TARGET_ID]) - int((sim.entities[TREBUCHET_TARGET_ID] as Dictionary).get("health", 0)),
		int(initial_health[SPLASH_TARGET_A_ID]) - int((sim.entities[SPLASH_TARGET_A_ID] as Dictionary).get("health", 0)),
		int(initial_health[SPLASH_TARGET_B_ID]) - int((sim.entities[SPLASH_TARGET_B_ID] as Dictionary).get("health", 0)),
	])
	print("RETAIL_PROJECTILE_PIN ticks=%d hash=%s" % [PIN_TICKS, hash])
	if hash != EXPECTED_HASH:
		_fail("behaviour moved: got %s, pinned %s" % [hash, EXPECTED_HASH])
		return
	print("RETAIL_PROJECTILE_PIN OK hash matches the pinned value")
	quit(0)


func _selected_document(content_db, source_id: String, descriptor_sha256: String) -> Dictionary:
	var document: Dictionary = content_db.get_playable_unit_runtime(source_id)
	if document.is_empty():
		_fail("selected content carries no %s playable-unit document" % source_id)
		return {}
	var pack_root := String(document.get("_pack_root", "")).replace("\\", "/").trim_suffix("/")
	if not pack_root.to_lower().ends_with(SELECTED_MEN_PACK.to_lower()):
		_fail("%s resolved from wrong pack: %s" % [source_id, pack_root])
		return {}
	if String(document.get("objectId", "")) != source_id:
		_fail("selected document identity changed for %s" % source_id)
		return {}
	if String(document.get("descriptorSha256", "")) != descriptor_sha256:
		_fail("selected document descriptor changed for %s" % source_id)
		return {}
	return document


func _normalized_rule(document: Dictionary, source_id: String) -> Dictionary:
	var simulation := Adapter.simulation_rule(document)
	if simulation.is_empty():
		_fail("%s has no resolved simulation rule" % source_id)
		return {}
	var rule := Adapter.normalized_unit_rule(simulation, SOURCE_SCALE)
	if rule.is_empty():
		_fail("%s simulation rule did not normalize" % source_id)
		return {}
	rule["object_id"] = String(simulation.get("object_id", ""))
	return rule


func _rules_match_authored_fixture(archer: Dictionary, trebuchet: Dictionary, target: Dictionary) -> bool:
	var archer_components := archer.get("damage_components", []) as Array
	var trebuchet_components := trebuchet.get("damage_components", []) as Array
	var ok := (
		String(archer.get("projectile_object_id", "")) == "GondorArcherArrow"
		and is_equal_approx(float(archer.get("projectile_speed_source", 0.0)), 321.0)
		and is_equal_approx(float(archer.get("attack_range_source", 0.0)), ARCHER_RANGE_SOURCE)
		and String(archer.get("damage_type", "")) == "pierce"
		and String(trebuchet.get("projectile_object_id", "")) == "GondorTrebuchetRockProjectile"
		and is_equal_approx(float(trebuchet.get("projectile_speed_source", 0.0)), 321.0)
		and is_equal_approx(float(trebuchet.get("attack_range_source", 0.0)), TREBUCHET_RANGE_SOURCE)
		and trebuchet_components.size() == 1
		and is_equal_approx(float((trebuchet_components[0] as Dictionary).get("radius", 0.0)), TREBUCHET_RADIUS_SOURCE)
		and is_equal_approx(float((trebuchet_components[0] as Dictionary).get("damage_taper_off", 0.0)), 50.0)
		and int(target.get("member_count", 0)) == 15
		and int(target.get("member_health", 0)) == 200
	)
	if not ok:
		_fail("selected-pack combat facts no longer match the frozen fixture: archer=%s trebuchet=%s target=%s" % [
			str({
				"projectile": archer.get("projectile_object_id", ""),
				"speed": archer.get("projectile_speed_source", null),
				"range": archer.get("attack_range_source", null),
				"components": archer_components,
			}),
			str({
				"projectile": trebuchet.get("projectile_object_id", ""),
				"speed": trebuchet.get("projectile_speed_source", null),
				"range": trebuchet.get("attack_range_source", null),
				"components": trebuchet_components,
			}),
			str({"members": target.get("member_count", null), "health": target.get("member_health", null)}),
		])
	return ok


func _add_source_battalion(sim, id: int, team: int, position: Vector2, rule: Dictionary) -> void:
	var object_id := String(rule.get("object_id", ""))
	var unit_type := String(rule.get("horde_id", ""))
	sim._add_battalion(id, team, position, unit_type, object_id, unit_type, 0, rule)


func _damaged(sim, target_id: int, initial_health: Dictionary) -> bool:
	return int((sim.entities[target_id] as Dictionary).get("health", 0)) < int(initial_health[target_id])


func _fail(reason: String) -> void:
	printerr("RETAIL_PROJECTILE_PIN FAIL %s" % reason)
	quit(1)
