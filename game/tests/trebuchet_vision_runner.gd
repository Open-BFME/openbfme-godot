extends SceneTree

## Failing-first proof for the owner report "the trebuchet gives no vision"
## (lane kimi-bug-world-fx, bug 3).
##
## ORACLES (retail effective-assets, ROTWK 2.01):
##   * data/ini/object/goodfaction/units/men/trebuchet.ini:380-381 -
##     GondorTrebuchet authors VisionRange = GONDOR_TREBUCHET_VISION_RANGE and
##     ShroudClearingRange = SHROUD_CLEAR_ARTILLERY.
##   * data/ini/gamedata.ini:1976 - GONDOR_TREBUCHET_VISION_RANGE = 500.
##   * data/ini/gamedata.ini:40    - SHROUD_CLEAR_ARTILLERY = 400.
## The deshroud radius is the ShroudClearingRange, NOT the VisionRange
## (retail_fog_of_war.gd header; MenFortressCitadel 400/800 proves the two are
## independent). So the grid must CLEAR at 300 source units and must NOT clear
## at 450 source units - inside VisionRange 500 but outside the authored
## deshroud 400. A runner that only checks "clears somewhere" cannot tell a
## vision-fallback from the authored shroud radius.
##
## THE TRAP THIS RUNNER PINS. The fog lane's report names the horde/member
## trap: members author tiny clearing ranges, hordes author the real one, and a
## lone vehicle is both. This runner builds the sim from the REAL pack registry
## and faction manifest (the goal_production_matrix_runner pattern), reads the
## configured rule for the trebuchet, spawns it through the real
## `_add_battalion` row builder, and asserts the fog grid around it.
##
## Invocation:
##   <godot> --headless --path game --script res://tests/trebuchet_vision_runner.gd
## with OPENBFME_CONTENT pointing at the content packs.

## NOT preloaded. retail_slice_sim.gd names script-engine global classes, and
## in `--script` mode the runner's own script is compiled before the autoloads
## and global class table are registered, so a preload fails to resolve them
## and takes the whole runner down with it. Loaded at call time instead, which
## is what banner_castle_sim_runner.gd does for the same reason.
const Watchdog := preload("res://tests/runner_watchdog.gd")
const SIM_SCRIPT_PATH := "res://src/retail_slice/retail_slice_sim.gd"
const MANIFEST_SCRIPT_PATH := "res://src/retail_slice/retail_faction_manifest.gd"
const SLICE_SCRIPT_PATH := "res://src/retail_slice/retail_vertical_slice.gd"
const MAP_DATA_SCRIPT_PATH := "res://src/retail_slice/retail_map_data.gd"

## Source->sim transform used by the headless matrix runners (the Fords slice
## uses 0.02649232738129; 0.1 keeps the arithmetic legible and changes nothing
## about the path under test).
const SCALE := 0.1
## Retail-authored source values for GondorTrebuchet.
const TREBUCHET_VISION_SOURCE := 500.0
const TREBUCHET_SHROUD_SOURCE := 400.0
## ContentDB keys the playable-unit registry by retail source object id; the
## sim's unit_rules use the adapter runtime slug.
const TREBUCHET_SOURCE_ID := "GondorTrebuchet"
const TREBUCHET_OBJECT_ID := "bfme2.object.gondor-trebuchet"

const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "TREBUCHET_VISION", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var SimClass: GDScript = load(SIM_SCRIPT_PATH) as GDScript
	var ManifestClass: GDScript = load(MANIFEST_SCRIPT_PATH) as GDScript
	if SimClass == null or ManifestClass == null:
		printerr("TREBUCHET_VISION FAIL cannot load sim/manifest scripts")
		_watchdog.stop()
		quit(1)
		return
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		printerr("TREBUCHET_VISION FAIL no ContentDB autoload")
		_watchdog.stop()
		quit(1)
		return
	await process_frame
	await process_frame
	var units: Dictionary = (
		content_db.call("get_playable_unit_runtimes")
		if content_db.has_method("get_playable_unit_runtimes")
		else {}
	)
	var structures: Dictionary = (
		content_db.call("get_playable_structure_runtimes")
		if content_db.has_method("get_playable_structure_runtimes")
		else {}
	)
	_check(
		"the selected pack ships a playable-unit runtime for the trebuchet",
		units.has(TREBUCHET_SOURCE_ID),
		"registry keys with 'trebuchet': %s" % [units.keys().filter(func(k): return String(k).contains("Trebuchet"))]
	)
	if not units.has(TREBUCHET_SOURCE_ID):
		_finish()
		return
	var slice_script = load(SLICE_SCRIPT_PATH)
	var slice = slice_script.new()
	var map_data = load(MAP_DATA_SCRIPT_PATH).new()
	map_data.local_transform_scale = SCALE
	slice.source_map_data = map_data
	slice._classify_faction_units("men")
	var fieldable: Dictionary = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var manifest: Dictionary = ManifestClass.from_registries("men", fieldable, structures)
	var manifest_err := String(manifest.get("_error", ""))
	_check("the men faction manifest builds", manifest_err == "", manifest_err)
	var builder_unit_rules: Dictionary = {}
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		var br: Dictionary = slice._faction_builder_unit_rule(builder_id)
		if not br.is_empty():
			builder_unit_rules[builder_id] = br
	slice.free()
	if manifest_err != "":
		_finish()
		return

	var sim = SimClass.new()
	sim._apply_gameplay_rules(
		{
			"enable_base_loop": true,
			"enable_fog_of_war": true,
			"faction_manifest": manifest,
			"playable_unit_runtimes": producible,
			"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
			"unit_rules": builder_unit_rules,
			"starting_resources": 1000000,
			"source_map_transform_scale": SCALE,
		}
	)
	if String(sim.configuration_error) != "":
		_check("the sim configures with fog enabled", false, "configure: " + String(sim.configuration_error))
		_finish()
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	_check(
		"the sim configures with fog enabled",
		String(sim.configuration_error) == "" and sim.fog_of_war_enabled,
		"setup error='%s' fog_enabled=%s" % [String(sim.configuration_error), sim.fog_of_war_enabled]
	)

	# Isolate the trebuchet: the base loop seeds a fortress whose own looks
	# would clear the same ground and mask a trebuchet failure.
	for seeded_id in sim.entity_ids():
		sim.entities.erase(seeded_id)
	for seeded_sid in sim.structure_ids():
		sim.structures.erase(seeded_sid)

	var rule: Dictionary = (sim._rules.get("unit_rules", {}) as Dictionary).get(TREBUCHET_OBJECT_ID, {}) as Dictionary
	_check(
		"the configured rule carries the retail vision 500 and deshroud 400 in source units",
		absf(float(rule.get("vision_range_source", 0.0)) - TREBUCHET_VISION_SOURCE) < 0.001
			and absf(float(rule.get("shroud_clearing_range_source", 0.0)) - TREBUCHET_SHROUD_SOURCE) < 0.001,
		"vision_range_source=%s shroud_clearing_range_source=%s"
			% [rule.get("vision_range_source", "<absent>"), rule.get("shroud_clearing_range_source", "<absent>")]
	)
	if rule.is_empty():
		_finish()
		return

	# Spawn through the real production row builder (`unit_rule_override` is the
	# same code path production uses), next to the player's own base so team 0
	# exists and the tick runs the full step list.
	var entity_id := 4901
	sim._add_battalion(
		entity_id, 0, Vector2.ZERO, "GondorTrebuchet",
		TREBUCHET_OBJECT_ID, String(rule.get("horde_id", TREBUCHET_OBJECT_ID)), -1, rule
	)
	var row: Dictionary = sim.entities.get(entity_id, {}) as Dictionary
	_check(
		"the entity row keeps vision and deshroud scaled into sim space",
		absf(float(row.get("vision_range", 0.0)) - TREBUCHET_VISION_SOURCE * SCALE) < 0.0001
			and absf(float(row.get("shroud_clearing_range", 0.0)) - TREBUCHET_SHROUD_SOURCE * SCALE) < 0.0001,
		"row vision_range=%s shroud_clearing_range=%s"
			% [row.get("vision_range", "<absent>"), row.get("shroud_clearing_range", "<absent>")]
	)
	_check(
		"the deshroud radius resolves to the authored 400, not the 500 vision fallback",
		absf(sim._shroud_clearing_radius(row) - TREBUCHET_SHROUD_SOURCE * SCALE) < 0.0001,
		"radius=%.6f" % sim._shroud_clearing_radius(row)
	)

	sim.tick()
	var fog = sim.fog_of_war()
	var treb_pos: Vector2 = row.get("position", Vector2.ZERO)
	var near_point := treb_pos + Vector2(300.0 * SCALE, 0.0)
	var mid_point := treb_pos + Vector2(450.0 * SCALE, 0.0)
	_check(
		"fog is CLEAR 300 source units from the trebuchet after one tick",
		fog.is_visible(0, near_point),
		"state=%d" % int(fog.state_at(0, near_point))
	)
	_check(
		"fog is NOT cleared 450 source units out - inside vision 500, past deshroud 400",
		not fog.is_visible(0, mid_point),
		"state=%d (visible would mean the radius came from vision, not ShroudClearingRange)"
			% int(fog.state_at(0, mid_point))
	)
	_finish()


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"TREBUCHET_VISION FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("TREBUCHET_VISION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])
