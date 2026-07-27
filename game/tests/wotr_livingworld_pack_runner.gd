extends SceneTree

## War of the Ring strategic layer against a SHIPPED living-world document.
##
## `wotr_strategic_runner.gd` proves the rules on a fixture authored in the
## test itself. This runner closes the other half of the contract: the
## importer's `openbfme.living-world` document — produced from retail, shipped
## in a content pack — actually loads, carries a usable region graph, and
## builds real strategic state. No fixture, no invented region names.
##
## Document discovery, in order:
##   1. `OPENBFME_LIVING_WORLD_DOC` — explicit path to a generated document
##      (the importer's `living-world` CLI command writes one).
##   2. `data/living-world.json` inside the packs selected by
##      `OPENBFME_CONTENT/selection.json` (active pack first, then
##      supplemental packs).
##
## When no document exists anywhere this runner exits 3 with a loud message:
## a pack built before the living-world lane is a real, reportable state and
## must never read as green.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

## LIVENESS. A GDScript runtime error aborts the enclosing function silently;
## pinning the check count turns that abort into a loud failure. Raise this
## deliberately when checks are added; never lower it to go green.
const EXPECTED_CHECKS := 20

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var found := _find_document()
	if found.is_empty():
		printerr("WOTR_LIVINGWORLD MISSING no living-world document: set "
			+ "OPENBFME_LIVING_WORLD_DOC or select a pack that ships "
			+ "data/living-world.json (packs built before the living-world "
			+ "lane do not)")
		quit(3)
		return
	var source_path := String(found.get("path", ""))
	print("WOTR_LIVINGWORLD document %s" % source_path)
	var document: Dictionary = found.get("document", {}) as Dictionary
	_test_document_shape(document)
	var world := WorldScript.new()
	_test_world_load(world, document)
	if world.region_ids.is_empty():
		_finish()
		return
	_test_graph(world, document)
	_test_strategic_state(world, document)
	_finish()


# --- discovery ---------------------------------------------------------------

func _find_document() -> Dictionary:
	var explicit := OS.get_environment("OPENBFME_LIVING_WORLD_DOC")
	if not explicit.is_empty():
		var parsed: Variant = _read_json(explicit)
		if parsed is Dictionary:
			return {"path": explicit, "document": parsed}
		printerr("WOTR_LIVINGWORLD FAIL explicit document did not parse: %s" % explicit)
		return {}
	var content_root := OS.get_environment("OPENBFME_CONTENT")
	if content_root.is_empty():
		return {}
	var selection: Variant = _read_json(content_root.path_join("selection.json"))
	if not (selection is Dictionary):
		return {}
	var candidates: Array[String] = []
	var active := String((selection as Dictionary).get("activePack", ""))
	if not active.is_empty():
		candidates.append(active)
	for extra in (selection as Dictionary).get("supplementalPacks", []) as Array:
		candidates.append(String(extra))
	for relative in candidates:
		var path := content_root.path_join(relative).path_join("data/living-world.json")
		if FileAccess.file_exists(path):
			var parsed: Variant = _read_json(path)
			if parsed is Dictionary:
				return {"path": path, "document": parsed}
			printerr("WOTR_LIVINGWORLD FAIL pack document did not parse: %s" % path)
			return {}
	return {}


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var handle := FileAccess.open(path, FileAccess.READ)
	if handle == null:
		return null
	var text := handle.get_as_text()
	handle.close()
	return JSON.parse_string(text)


# --- document shape ----------------------------------------------------------

func _test_document_shape(document: Dictionary) -> void:
	_check("document_declares_the_living_world_schema",
		String(document.get("schema", "")) == WorldScript.SCHEMA
			and int(document.get("schemaVersion", -1)) == WorldScript.SCHEMA_VERSION)
	# The document's own counters must agree with its body, so a hand-edited
	# or truncated document cannot keep quoting the importer's numbers.
	var region_total := 0
	var connection_total := 0
	for campaign in document.get("regionCampaigns", []) as Array:
		for region in (campaign as Dictionary).get("regions", []) as Array:
			region_total += 1
			connection_total += ((region as Dictionary).get("connections", []) as Array).size()
	_check("document_counters_match_the_authored_body",
		region_total == int(document.get("regionCount", -1))
			and connection_total == int(document.get("connectionCount", -1)),
		"counted %d regions / %d connections, document claims %d / %d" % [
			region_total, connection_total,
			int(document.get("regionCount", -1)),
			int(document.get("connectionCount", -1))])
	# Retail authors every connection in both directions in the shipped WOTR
	# graph (the importer gaps any one-way edge and both editions measure
	# zero). The strategic layer symmetrises on load, so a dropped half-edge
	# would otherwise be silently repaired — assert it never ships.
	var symmetric := true
	for campaign in document.get("regionCampaigns", []) as Array:
		var links: Dictionary = {}
		for region in (campaign as Dictionary).get("regions", []) as Array:
			var row := region as Dictionary
			var targets: Dictionary = {}
			for link in row.get("connections", []) as Array:
				targets[String((link as Dictionary).get("region", ""))] = true
			links[String(row.get("id", ""))] = targets
		for source_id in links:
			for target_id in links[source_id] as Dictionary:
				var back: Dictionary = links.get(target_id, {}) as Dictionary
				if not back.has(source_id):
					symmetric = false
	_check("every_authored_connection_is_reciprocal", symmetric)


# --- world -------------------------------------------------------------------

func _test_world_load(world: WorldScript, document: Dictionary) -> void:
	_check("world_loads_the_shipped_document_fail_closed",
		world.load_from_dict(document), str(world.errors))
	_check("auto_selection_prefers_a_campaign_with_edges",
		not world.campaign_name.is_empty(), world.campaign_name)
	_check("import_gaps_are_carried_through_unchanged",
		world.import_gaps.size() == (document.get("gaps", []) as Array).size())


func _test_graph(world: WorldScript, _document: Dictionary) -> void:
	# 38 regions is the measured BFME2 1.06 WOTR map; RotWK 2.01 measures 52.
	# Any shipped retail-derived document must reach at least the smaller.
	_check("the_map_carries_the_measured_retail_region_count",
		world.region_ids.size() >= 38, "regions=%d" % world.region_ids.size())
	var edges := 0
	for region_id in world.region_ids:
		edges += world.neighbours(region_id).size()
	_check("the_map_carries_edges", edges > 0)
	# Every region reaches every other: retail authors one continuous Middle
	# Earth, and an unreachable region could never change hands by movement.
	var seen: Dictionary = {}
	var stack: Array[String] = [world.region_ids[0]]
	while not stack.is_empty():
		var current := stack.pop_back() as String
		if seen.has(current):
			continue
		seen[current] = true
		for neighbour in world.neighbours(current):
			stack.append(neighbour)
	_check("every_region_is_reachable_from_every_other",
		seen.size() == world.region_ids.size(),
		"reached %d of %d" % [seen.size(), world.region_ids.size()])
	_check("cp_limits_resolve_with_retail_defaults",
		world.region_cp_limit(world.region_ids[0]) > 0
			and world.region_ally_cp_limit(world.region_ids[0]) > 0)
	_check("player_templates_carry_a_command_point_economy",
		_some_template_has_economy(world))
	_check("rts_handoff_settings_are_authored",
		int(world.rts_settings.get("seconds_per_reinforcement", -1)) > 0
			and int(world.rts_settings.get("starting_cash_rts", -1)) > 0)
	# Every region a scenario of the loaded campaign touches must exist.
	var references_resolve := true
	for name in world.scenario_names:
		var scenario := world.scenario(name)
		if String(scenario.get("region_campaign", "")) != world.campaign_name:
			continue
		for ownership in scenario.get("ownership_sets", []) as Array:
			var row := ownership as Dictionary
			for region_id in row.get("regions", PackedStringArray()) as PackedStringArray:
				if not world.has_region(region_id):
					references_resolve = false
			for spawn in row.get("spawn_armies", []) as Array:
				if not world.has_region(String((spawn as Dictionary).get("region", ""))):
					references_resolve = false
	_check("every_scenario_region_reference_resolves", references_resolve)


func _some_template_has_economy(world: WorldScript) -> bool:
	for name in world.player_templates:
		if int((world.player_templates[name] as Dictionary).get("starting_world_cp", -1)) > 0:
			return true
	return false


# --- strategic state ---------------------------------------------------------

func _test_strategic_state(world: WorldScript, _document: Dictionary) -> void:
	var scenario_name := _usable_scenario(world)
	_check("a_multiplayer_scenario_with_ownership_exists", not scenario_name.is_empty())
	if scenario_name.is_empty():
		return
	var seats := _seats_from_templates(world)
	var state := StateScript.new()
	_check("real_world_and_templates_seat_a_session",
		seats.size() == 2 and state.setup(world, seats))
	_check("setup_starts_every_region_neutral",
		state.regions_owned_by(StateScript.NEUTRAL).size() == world.region_ids.size())
	_check("the_scenario_ownership_sets_apply",
		state.apply_ownership_sets(scenario_name))
	_check("both_seats_own_authored_territory",
		state.regions_owned_by(0).size() > 0 and state.regions_owned_by(1).size() > 0,
		"seat0=%d seat1=%d" % [state.regions_owned_by(0).size(), state.regions_owned_by(1).size()])
	var first_hash := String(state.state_hash())
	_check("the_real_state_hashes_deterministically",
		not first_hash.is_empty() and first_hash == String(state.state_hash()))
	var snapshot: PackedByteArray = state.snapshot()
	var restored := StateScript.new()
	var round_trip := restored.setup(world, seats) and restored.restore(snapshot)
	_check("the_real_state_round_trips_through_snapshot",
		round_trip and String(restored.state_hash()) == first_hash)


func _usable_scenario(world: WorldScript) -> String:
	for name in world.scenario_names:
		var scenario := world.scenario(name)
		if String(scenario.get("region_campaign", "")) != world.campaign_name:
			continue
		if int(scenario.get("max_players", 0)) < 2:
			continue
		var sets: Array = scenario.get("ownership_sets", []) as Array
		if sets.size() < 2:
			continue
		var both_seats_start_owned := true
		for index in range(2):
			if ((sets[index] as Dictionary).get("regions", PackedStringArray()) as PackedStringArray).is_empty():
				both_seats_start_owned = false
		if both_seats_start_owned:
			return name
	return ""


func _seats_from_templates(world: WorldScript) -> Array:
	var seats: Array = []
	var names: Array = world.player_templates.keys()
	names.sort()
	for name in names:
		var template := world.player_templates[name] as Dictionary
		if int(template.get("starting_world_cp", -1)) <= 0:
			continue
		seats.append({"template": String(name), "team": seats.size() + 1})
		if seats.size() == 2:
			break
	return seats


# --- harness -----------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WOTR_LIVINGWORLD PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_LIVINGWORLD FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("WOTR_LIVINGWORLD FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("WOTR_LIVINGWORLD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
