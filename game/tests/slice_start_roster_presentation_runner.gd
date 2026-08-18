extends SceneTree
## Menu-path roster presentation gate for a RotWK Men skirmish and the Fords
## scenario — the two owner surfaces that v0.2.4 died on.
##
## v0.2.4 printed RETAIL SLICE UNAVAILABLE / "Faction roster presentation
## validation failed" because `_load_required_presentation_definitions` →
## `_validate_retail_structure_lifecycle` ran
## RetailStructure.validate_lifecycle_contract on the legacy objects.json
## men-fortress (construction facts ABSENT, phase-0 still manual-progress).
## In debug Godot that unguarded `facts["construction"]` aborts the function
## and the return-value check fails OPEN; the only debug-observable symptom
## is the SCRIPT ERROR / Invalid access line. The harness (this runner's
## combined stdout+stderr, plus gate-m2-focused `$forbiddenDiagnostics`)
## must therefore assert ZERO of those lines. Pre-fix baseline: 11 in
## workspace/logs/q14fin-retail_slice_runner.err; 8 in
## workspace/logs/hotfix-0241-slice-start-before.err.
##
## Surfaces (genuinely different — the default MAP_ID is already Fords, so
## an empty OPENBFME_SLICE_MAP is NOT a RotWK skirmish):
##   (a) rotwk.map.adorn-river + Men host (a catalog RotWK skirmish map)
##   (b) bfme2.map.fords-of-isen-ii + Men (the Fords scenario)
## Live selection must be mounted: ModLoader.active_pack_root contains
## rotwk-men-vslice digest a0fde4ac.
##
## Run:
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/slice_start_roster_presentation_runner.gd

const BOOT_DEADLINE_MS := 300000
const FORDS_MAP := "bfme2.map.fords-of-isen-ii"
const ROTWK_SKIRMISH_MAP := "rotwk.map.adorn-river"
const ROTWK_MEN_DIGEST := "a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0"
const MEN_FORTRESS_OBJECT_ID := "bfme2.object.men-fortress"
const SHIPPED_MEN_VSLICE := "bfme2-men-vslice/7de517bf146582f10741750b50d63f9955c42d1fe2aa13200757fc6fb29f217a"
## Mirrors RetailSliceSim.CREEP_TEAM. Not a preload: the slice script chain
## resolves autoloads after SceneTree --script compile.
const CREEP_TEAM := 9999
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()
var _seen_maps: PackedStringArray = []


func _initialize() -> void:
	_runner_watchdog.start(self, "SLICE_START_ROSTER_PRESENTATION_RUNNER")
	for env_name in ["OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	print("SLICE_START_ROSTER_HARNESS require_stderr_clean=SCRIPT_ERROR,Invalid access")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	# (a) RotWK Men skirmish — a rotwk.map.* id, not the Fords default.
	await _run_path("rotwk_skirmish_men", "men", ROTWK_SKIRMISH_MAP)
	# (b) BFME2 Men "Destroy the enemy fortress" / Fords scenario.
	await _run_path("fords_bfme2_men", "men", FORDS_MAP)
	_check(
		"surfaces_are_distinct",
		_seen_maps.size() == 2 and _seen_maps[0] != _seen_maps[1],
		"maps=%s" % ", ".join(_seen_maps)
	)
	_check_shipped_men_fortress_contract()
	_finish()


func _run_path(label: String, faction: String, map_id: String) -> void:
	OS.set_environment("OPENBFME_SLICE_FACTION", faction)
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	var game_state := root.get_node_or_null("GameState")
	if game_state != null:
		game_state.set("retail_player_faction", faction)
		game_state.set("retail_map_id", map_id)

	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("%s_scene_loads" % label, scene != null, "vertical slice scene did not load"):
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break

	var failure := String(slice.failure_reason)
	var pack_root := String(slice.selected_pack_root)
	var resolved_map := String(slice.map_id)
	_seen_maps.append(resolved_map)
	var active_root := ""
	var mod_loader := root.get_node_or_null("ModLoader")
	if mod_loader != null:
		active_root = String(mod_loader.get("active_pack_root"))
	print(
		"SLICE_START_ROSTER_PATH %s ready_ok=%s host_pack=%s active_pack=%s map=%s failure=%s"
		% [
			label,
			str(bool(slice.ready_ok)),
			pack_root,
			active_root if active_root != "" else "<empty>",
			resolved_map,
			failure if failure != "" else "<empty>",
		]
	)

	# ready_ok first: a dead slice must not be scored as a clean roster.
	if not _check(
		"%s_ready_ok" % label,
		bool(slice.ready_ok),
		"failure=%s" % failure
	):
		root.remove_child(slice)
		slice.free()
		await process_frame
		OS.set_environment("OPENBFME_SLICE_MAP", "")
		return

	var simulation = slice.simulation if slice.get("simulation") != null else null
	_check("%s_simulation_present" % label, simulation != null, "simulation is null")
	var structure_count := 0
	if simulation != null and simulation.has_method("structure_ids"):
		structure_count = int(simulation.structure_ids().size())
	_check(
		"%s_structure_set_nonempty" % label,
		structure_count > 0,
		"structure_ids=%d" % structure_count
	)
	_check(
		"%s_mounted_rotwk_men_digest" % label,
		active_root.replace("\\", "/").contains(ROTWK_MEN_DIGEST),
		"active_pack=%s" % active_root
	)
	if map_id == FORDS_MAP:
		_check(
			"%s_map_is_fords" % label,
			resolved_map == FORDS_MAP,
			"map=%s" % resolved_map
		)
	else:
		_check(
			"%s_map_is_rotwk_skirmish" % label,
			resolved_map.begins_with("rotwk.map.") and resolved_map == map_id,
			"map=%s expected=%s" % [resolved_map, map_id]
		)

	var structure_failures := _structure_retail_visual_failures(slice)
	var joined := ", ".join(structure_failures)
	_check(
		"%s_no_structure_retail_visuals" % label,
		not failure.contains("structure_retail_visuals") and not joined.contains("buildingLifecycle"),
		"failure=%s nodes=%s" % [failure, joined]
	)
	_check(
		"%s_no_missing_node" % label,
		not failure.contains("missing-node") and not joined.contains("missing-node"),
		"failure=%s nodes=%s" % [failure, joined]
	)
	_check(
		"%s_no_construction_disagree" % label,
		not failure.contains("construction facts and phase animation disagree")
			and not joined.contains("construction facts and phase animation disagree"),
		"failure=%s nodes=%s" % [failure, joined]
	)
	# Secondary diagnostic only — do not re-enter the mutating
	# `_load_required_presentation_definitions` on a booted slice.
	_check(
		"%s_boot_failure_reason_empty" % label,
		failure == "",
		"failure=%s" % failure
	)

	root.remove_child(slice)
	slice.free()
	await process_frame
	OS.set_environment("OPENBFME_SLICE_MAP", "")


func _check_shipped_men_fortress_contract() -> void:
	## External oracle: the shipped objects.json document, not a fixture.
	## Post-34e6cfa the bib/routes/rebuild-hole checks after :730 run on this
	## doc for the first time; print the real return and do not paper it over.
	var lifecycle := _load_shipped_men_fortress_lifecycle()
	if lifecycle.is_empty():
		_check("shipped_men_fortress_contract", false, "could not load shipped objects.json men-fortress")
		return
	var facts: Dictionary = lifecycle.get("simulationFacts", {}) as Dictionary
	print(
		"MEN_FORTRESS_FACTS has_construction=%s keys=%s"
		% [str(facts.has("construction")), str(facts.keys())]
	)
	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd")
	var result: String = structure_script.validate_lifecycle_contract(
		lifecycle, "fortress", "", 0, MEN_FORTRESS_OBJECT_ID
	)
	print("MEN_FORTRESS_CONTRACT result=%s" % (result if result != "" else "<empty>"))
	_check(
		"shipped_men_fortress_contract",
		result == "",
		result if result != "" else "<empty>"
	)


func _load_shipped_men_fortress_lifecycle() -> Dictionary:
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if content_root == "":
		print("MEN_FORTRESS_CONTRACT refuse=OPENBFME_CONTENT unset")
		return {}
	var objects_path := content_root.path_join(SHIPPED_MEN_VSLICE).path_join("data/objects.json")
	if not FileAccess.file_exists(objects_path):
		print("MEN_FORTRESS_CONTRACT refuse=missing %s" % objects_path)
		return {}
	var file := FileAccess.open(objects_path, FileAccess.READ)
	if file == null:
		print("MEN_FORTRESS_CONTRACT refuse=unreadable %s" % objects_path)
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		print("MEN_FORTRESS_CONTRACT refuse=objects.json is not an object")
		return {}
	var objects: Variant = (parsed as Dictionary).get("objects")
	if typeof(objects) != TYPE_ARRAY:
		print("MEN_FORTRESS_CONTRACT refuse=objects.json has no objects list")
		return {}
	for row_value in objects as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row: Dictionary = row_value
		if String(row.get("id", "")) != MEN_FORTRESS_OBJECT_ID:
			continue
		var presentation: Dictionary = row.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = presentation.get("buildingLifecycle", {}) as Dictionary
		return lifecycle
	print("MEN_FORTRESS_CONTRACT refuse=bfme2.object.men-fortress missing from objects.json")
	return {}


func _structure_retail_visual_failures(slice) -> PackedStringArray:
	## Same exclusions the slice uses when it names `structure_retail_visuals`
	## failures: creep lairs without a scenario receipt, and bound map props.
	var out: PackedStringArray = []
	var simulation = slice.simulation if slice.get("simulation") != null else null
	var structure_nodes: Dictionary = slice.structure_nodes if slice.get("structure_nodes") != null else {}
	if simulation == null or not simulation.has_method("structure_ids"):
		return out
	for structure_id_value in simulation.structure_ids():
		var structure_id := int(structure_id_value)
		var entity: Dictionary = simulation.structure(structure_id)
		if int(entity.get("team", -1)) == CREEP_TEAM and not entity.has("scenario_lifecycle_receipt"):
			continue
		if String(entity.get("presentation", "")) == "bound-map-prop":
			continue
		var structure_node = structure_nodes.get(structure_id)
		if structure_node == null:
			out.append("%d:missing-node" % structure_id)
			continue
		var contract_error := String(structure_node.get("contract_error"))
		if contract_error != "":
			out.append("%d:%s" % [structure_id, contract_error])
	return out


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		if detail == "":
			print("FAIL %s" % name)
		else:
			print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	print("SLICE_START_ROSTER_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
