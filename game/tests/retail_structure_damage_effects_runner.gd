extends SceneTree
## Focused legal-safe gate for the source-proven Men damage route contract.
## It uses authored identifiers and fixture geometry; it does not claim retail
## particle rendering, event timing, or pack conversion completion.

const STRUCTURE_SOURCE := "res://src/retail_slice/retail_structure.gd"
const SPECS := [
	{"kind": "fortress", "object_id": "bfme2.object.men-fortress", "maximum": 7500, "damaged": 2500, "really": 1250, "prefix": "men-fortress", "build": "gbfortress_abl", "idle": null, "d2": "gbfortress_d2an", "d3": "gbfortress_d3an", "source": "MenFortressCitadel", "collapse_height": 155, "collapse_enter": "FX_FortressCollapse", "collapse_fx": {"initial": "FX_FortressCollapse"}, "collapse_audio": "BuildingSink", "construction_audio": "BuildingBigConstructionLoop", "terminal_smoke": false},
	{"kind": "farm", "object_id": "bfme2.object.men-farm", "maximum": 2000, "damaged": 1333, "really": 667, "prefix": "men-farm", "build": "gbfarm_abld", "idle": "gbfarm_idla", "d2": "gbfarm_d2an", "d3": "gbfarm_d3an", "source": "GondorFarm", "collapse_height": 66, "collapse_enter": "FX_BuildingReallyDamaged", "collapse_fx": {"almost-final": "FX_StructureAlmostCollapse", "initial": "FX_StructureMediumCollapse"}, "collapse_audio": null, "construction_audio": null, "terminal_smoke": true},
	{"kind": "barracks", "object_id": "bfme2.object.men-barracks", "maximum": 3000, "damaged": 2000, "really": 1000, "prefix": "men-barracks", "build": "gbbarracks_abld", "idle": "gbbarracks_2ida", "d2": "gbbarracks_d2an", "d3": "gbbarracks_d3an", "source": "GondorBarracks", "collapse_height": 155, "collapse_enter": "FX_StructureMediumCollapse", "collapse_fx": {"almost-final": "FX_StructureAlmostCollapse", "initial": "FX_StructureMediumCollapse"}, "collapse_audio": null, "construction_audio": null, "terminal_smoke": true},
	{"kind": "archery_range", "object_id": "bfme2.object.men-archery-range", "maximum": 3000, "damaged": 2000, "really": 1000, "prefix": "men-archery-range", "build": "gbarcheryn_abld", "idle": "gbarcheryn_idla", "d2": "gbarcheryn_d2an", "d3": "gbarcheryn_d3an", "source": "GondorArcherRange", "collapse_height": 130, "collapse_enter": "FX_StructureMediumCollapse", "collapse_fx": {"almost-final": "FX_StructureAlmostCollapse", "initial": "FX_StructureMediumCollapse"}, "collapse_audio": null, "construction_audio": null, "terminal_smoke": true},
	{"kind": "stable", "object_id": "bfme2.object.men-stable", "maximum": 3000, "damaged": 2000, "really": 1000, "prefix": "men-stable", "build": "gbstable_abld", "idle": "gbstable_idla", "d2": "gbstable_d2an", "d3": "gbstable_d3an", "source": "GondorStable", "collapse_height": 136, "collapse_enter": "FX_StructureMediumCollapse", "collapse_fx": {"almost-final": "FX_StructureAlmostCollapse", "initial": "FX_StructureMediumCollapse"}, "collapse_audio": null, "construction_audio": null, "terminal_smoke": true},
]

var passed := 0
var failed := 0
var structure_script


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_STRUCTURE_DAMAGE_EFFECTS_RUNNER")
	create_timer(30.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	structure_script = load(STRUCTURE_SOURCE)
	_check("runner_dependency", structure_script != null)
	if structure_script == null:
		_finish()
		return
	_test_route_registry_boundary()
	for spec_value in SPECS:
		_test_structure(spec_value as Dictionary)
	await process_frame
	await process_frame
	_finish()


func _test_route_registry_boundary() -> void:
	var lifecycle := _lifecycle(SPECS[1] as Dictionary)
	var registry := _route_registry()
	_check(
		"exact_selected_pack_route_identifiers_validate",
		structure_script.validate_declared_route_registry(lifecycle, registry) == ""
	)
	var bad_audio := lifecycle.duplicate(true)
	bad_audio.audioEvents.collapse = "InventedBuildingDamageSound"
	_check(
		"unknown_audio_identifier_fails_closed",
		String(structure_script.validate_declared_route_registry(bad_audio, registry)).contains("InventedBuildingDamageSound")
	)
	var bad_fx := lifecycle.duplicate(true)
	bad_fx.effects.enteringStateFx.damaged = "FX_InventedDamage"
	_check(
		"unknown_fx_identifier_noops_until_effects_cook",
		structure_script.validate_declared_route_registry(bad_fx, registry) == ""
	)
	var bad_particle := lifecycle.duplicate(true)
	bad_particle.effects.particleAttachments[0].particleSystemId = "InventedRubbleSmoke"
	_check(
		"unknown_particle_identifier_noops_until_effects_cook",
		structure_script.validate_declared_route_registry(bad_particle, registry) == ""
	)


func _test_structure(spec: Dictionary) -> void:
	var lifecycle := _lifecycle(spec)
	var fixtures := _fixture_visuals(lifecycle)
	var requests: Array[Dictionary] = []
	var structure = structure_script.new()
	structure.lifecycle_route_requested.connect(func(request: Dictionary) -> void: requests.append(request.duplicate(true)))
	var entity := _entity(spec)
	var stages_detached_route := String(spec.kind) == "farm"
	if stages_detached_route:
		entity.health = int(spec.damaged)
	structure.configure_fixture(entity, lifecycle, fixtures, 8000.0)
	if stages_detached_route:
		structure.set_authoritative_lifecycle_phase("really-damaged")
	var route_was_deferred := requests.is_empty()
	var staged_position := Vector3(7.0, 0.0, 11.0)
	structure.position = staged_position
	root.add_child(structure)
	var staged_route_ok := true
	if stages_detached_route:
		staged_route_ok = (
			route_was_deferred
			and requests.size() == 1
			and _route_request_matches(requests[0], structure, entity, lifecycle, "really-damaged", staged_position)
		)
		requests.clear()
		root.remove_child(structure)
		structure.set_authoritative_lifecycle_phase("damaged")
		structure.set_authoritative_lifecycle_phase("intact")
		structure.position = Vector3(9.0, 0.0, 13.0)
		root.add_child(structure)
		staged_route_ok = staged_route_ok and requests.is_empty()
		root.remove_child(structure)
		structure.set_authoritative_lifecycle_phase("damaged")
		structure.set_authoritative_lifecycle_phase("really-damaged")
		var reentry_position := Vector3(13.0, 0.0, 17.0)
		structure.position = reentry_position
		root.add_child(structure)
		staged_route_ok = (
			staged_route_ok
			and requests.size() == 1
			and _route_request_matches(requests[0], structure, entity, lifecycle, "really-damaged", reentry_position)
		)
		structure.set_authoritative_lifecycle_phase("damaged")
		staged_route_ok = (
			staged_route_ok
			and requests.size() == 2
			and _route_request_matches(requests[1], structure, entity, lifecycle, "damaged", reentry_position)
		)
		requests.clear()
	var label := String(spec.kind)
	_check("%s_v1_contract_loads" % label, structure.contract_error == "" and structure.current_lifecycle_phase == ("damaged" if stages_detached_route else "intact") and staged_route_ok)
	_check("%s_exact_thresholds" % label, int(lifecycle.simulationFacts.maximumHealth) == int(spec.maximum) and int(lifecycle.simulationFacts.damageStateRule.damagedThreshold) == int(spec.damaged) and int(lifecycle.simulationFacts.damageStateRule.reallyDamagedThreshold) == int(spec.really))

	entity.health = int(spec.damaged) + 1
	structure.sync_state(entity)
	_check("%s_above_damaged_threshold_is_intact" % label, structure.current_lifecycle_phase == "intact")
	entity.health = int(spec.damaged)
	structure.sync_state(entity)
	_check("%s_damaged_threshold_is_authoritative" % label, structure.current_lifecycle_phase == "damaged")
	_check("%s_damaged_routes_exact_ids" % label, structure.active_entering_fx == String(lifecycle.effects.enteringStateFx.damaged) and structure.active_audio_event == "")
	entity.health = int(spec.really) + 1
	structure.sync_state(entity)
	_check("%s_above_really_damaged_threshold_stays_damaged" % label, structure.current_lifecycle_phase == "damaged")
	entity.health = int(spec.really)
	structure.sync_state(entity)
	_check("%s_really_damaged_threshold_is_authoritative" % label, structure.current_lifecycle_phase == "really-damaged")
	_check("%s_really_damaged_routes_exact_ids" % label, structure.active_entering_fx == String(lifecycle.effects.enteringStateFx["really-damaged"]) and structure.active_audio_event == "" and structure.active_animation_clip == String(spec.d2))
	entity.health = 0
	structure.sync_state(entity)
	_check("%s_zero_health_enters_authored_collapse" % label, structure.current_lifecycle_phase == "collapsing")
	_check("%s_collapsing_routes_exact_ids" % label, structure.active_entering_fx == String(spec.collapse_enter) and structure.active_audio_event == ("" if spec.collapse_audio == null else String(spec.collapse_audio)) and structure.active_animation_clip == String(spec.d3))
	_check("%s_collapse_timer_is_not_guessed" % label, not bool(structure.request_declared_lifecycle_transition()) and structure.transition_error.contains("collapse completion timing is unresolved"))

	_check("%s_rubble_is_authoritative" % label, bool(structure.set_authoritative_lifecycle_phase("rubble")))
	_check("%s_rubble_uses_declared_model" % label, structure.active_visual_mode == "glb" and structure.active_body_path.ends_with("/rubble.glb") and structure.active_bib_path == "")
	_check("%s_post_rubble_timer_is_not_guessed" % label, not bool(structure.request_declared_lifecycle_transition()) and structure.transition_error.contains("post-rubble transition timing is unresolved"))

	_check("%s_post_rubble_is_authoritative" % label, bool(structure.set_authoritative_lifecycle_phase("post-rubble")))
	var post_state: Dictionary = structure.lifecycle_state()
	_check("%s_post_rubble_is_exact_no_render" % label, bool(post_state.terminalNoRender) and structure.active_body_path == "" and structure.active_bib_path == "" and not bool(post_state.retainedRubble) and not structure.get_node("HealthBack").visible and not structure.get_node("HealthFill").visible)
	_check("%s_post_rubble_routes_exact_smoke_id" % label, structure.active_particle_system_ids == (["SmokeBuildingMediumRubble"] if bool(spec.terminal_smoke) else []))

	_check("%s_post_collapse_is_authoritative" % label, bool(structure.set_authoritative_lifecycle_phase("post-collapse")))
	var collapse_state: Dictionary = structure.lifecycle_state()
	_check("%s_post_collapse_is_exact_no_render" % label, bool(collapse_state.terminalNoRender) and structure.active_particle_system_ids == (["SmokeBuildingMediumRubble"] if bool(spec.terminal_smoke) else []))
	_check("%s_route_requests_are_contract_only" % label, _requests_are_contract_only(requests))
	structure.free()
	_free_fixtures(fixtures)


func _route_request_matches(
	request: Dictionary,
	structure: Node,
	entity: Dictionary,
	lifecycle: Dictionary,
	phase: String,
	expected_position: Vector3
) -> bool:
	return (
		String(request.get("phase", "")) == phase
		and int(request.get("entityId", -1)) == int(entity.id)
		and String(request.get("objectId", "")) == "fixture.%s" % String(entity.structure_kind)
		and String(request.get("enteringFx", "")) == String(lifecycle.effects.enteringStateFx.get(phase, ""))
		and String(request.get("audioEvent", "")) == String(structure.active_audio_event)
		and request.get("particleSystemIds", []) == structure.active_particle_system_ids
		and (request.get("worldPosition", Vector3.INF) as Vector3).is_equal_approx(expected_position)
	)


func _route_registry() -> Dictionary:
	return {
		"audioEvents": {
			"buildingsink": "selected-pack-event",
			"buildingbigconstructionloop": "selected-pack-event",
		},
		"fxLists": {
			"FX_BuildingDamaged": "source-identified-runtime-emitter-unimplemented",
			"FX_BuildingReallyDamaged": "blocked-unresolved-cross-family-precedence",
			"FX_FortressDamaged": "source-identified-runtime-emitter-unimplemented",
			"FX_FortressReallyDamaged": "blocked-unresolved-cross-family-precedence",
			"FX_FortressCollapse": "blocked-unresolved-cross-family-precedence",
			"FX_StructureMediumCollapse": "blocked-unresolved-cross-family-precedence",
			"FX_StructureAlmostCollapse": "blocked-unresolved-cross-family-precedence",
		},
		"particleSystems": {
			"SmokeBuildingMediumRubble": "blocked-unresolved-cross-family-precedence",
		},
	}


func _lifecycle(spec: Dictionary) -> Dictionary:
	var root_path := "assets/fixtures/%s" % String(spec.prefix)
	var components: Dictionary = {}
	if String(spec.kind) == "fortress":
		components = {
			"door": {
				"closed": {"path": "%s/door-closed.glb" % root_path},
				"construction": {"path": "%s/door-construction.glb" % root_path},
				"rubble": {"path": "%s/door-rubble.glb" % root_path},
			},
		}
	var particle_attachments: Array = []
	if bool(spec.terminal_smoke):
		particle_attachments = [
			{"sourceConditions": ["POST_RUBBLE"], "bone": "NONE", "particleSystemId": "SmokeBuildingMediumRubble", "options": [], "sourceObject": String(spec.source)},
			{"sourceConditions": ["POST_COLLAPSE"], "bone": "NONE", "particleSystemId": "SmokeBuildingMediumRubble", "options": [], "sourceObject": String(spec.source)},
		]
	return {
		"schema": "openbfme.building-lifecycle-presentation",
		"schemaVersion": 1,
		"objectId": String(spec.object_id),
		"initialPhase": "intact",
		"phases": [
			_phase("construction", [["AWAITING_CONSTRUCTION"], ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"]], "glb", "%s/construction.glb" % root_path, String(spec.build), "manual-progress", "intact"),
			_phase("intact", [[]], "glb", "%s/intact.glb" % root_path, spec.idle, "none" if spec.idle == null else "loop", "damaged"),
			_phase("damaged", [["DAMAGED"]], "glb", "%s/damaged.glb" % root_path, null, "none", "really-damaged"),
			_phase("really-damaged", [["REALLYDAMAGED"]], "glb", "%s/really-damaged.glb" % root_path, String(spec.d2), "once", "collapsing"),
			_phase("collapsing", [["COLLAPSING"]], "glb", "%s/rubble.glb" % root_path, String(spec.d3), "once", "rubble"),
			_phase("rubble", [["RUBBLE"]], "glb", "%s/rubble.glb" % root_path, null, "none", "post-rubble"),
			_phase("post-rubble", [["POST_RUBBLE"]], "no-render", "None", null, "none", null),
			_phase("post-collapse", [["POST_COLLAPSE"]], "no-render", "None", null, "none", null),
		],
		"bib": {"sourceConditions": [], "visual": {"mode": "glb", "glb": "%s/bib.glb" % root_path}, "duringConstruction": false},
		"components": components,
		"audioEvents": {"collapse": spec.collapse_audio, "construction": spec.construction_audio},
		"effects": {
			"enteringStateFx": {"damaged": "FX_FortressDamaged" if String(spec.kind) == "fortress" else "FX_BuildingDamaged", "really-damaged": "FX_FortressReallyDamaged" if String(spec.kind) == "fortress" else "FX_BuildingReallyDamaged", "collapsing": String(spec.collapse_enter)},
			"collapseUpdateFx": (spec.collapse_fx as Dictionary).duplicate(true),
			"particleAttachments": particle_attachments,
			"definitionTranslationStatus": "source-identified-cross-family-selection-unresolved",
		},
		"simulationFacts": {
			"maximumHealth": int(spec.maximum),
			"damageStateRule": {"damagedThreshold": int(spec.damaged), "reallyDamagedThreshold": int(spec.really)},
			"initialHealth": {"derivedHitPoints": int(spec.maximum), "mapAuthoredPercent": 100, "status": "proven-full-health-map-placement"},
			"construction": {"animation": String(spec.build), "animationMode": "MANUAL", "buildTimeSeconds": 30},
			"collapse": {"animationFrameCount": 91, "animationFrameRate": 30, "animationVirtualPath": "art/w3d/gb/%s_d3an.w3d" % String(spec.prefix), "bigBurstFrequency": 4, "collapseDamping": 0.5, "collapseHeight": int(spec.collapse_height), "destroyObjectWhenDone": true, "exactTotalTimingStatus": "blocked-on-bfme2-runtime-oracle", "fxLists": (spec.collapse_fx as Dictionary).duplicate(true), "maxBurstDelayMilliseconds": 800, "maxCollapseDelayMilliseconds": 0, "maxShudder": 0.6, "minBurstDelayMilliseconds": 250, "minCollapseDelayMilliseconds": 0, "module": "StructureCollapseUpdate", "sourceObject": String(spec.source)},
			"postRubble": {"terminalDuration": "source-Model-None-after-POST_RUBBLE", "exactPostRubbleTransitionTimingStatus": "blocked-on-bfme2-runtime-oracle"},
			"captureInitialState": {"initialHealthPercent": 100, "initialPhase": "intact", "mapPlacementCount": 2, "owner": "Men"},
		},
		"rebuildHole": null,
	}


func _phase(phase: String, conditions: Array, visual_mode: String, value: String, clip: Variant, mode: String, next_phase: Variant) -> Dictionary:
	var visual := {"mode": visual_mode}
	if visual_mode == "glb":
		visual["glb"] = value
	else:
		visual["sourceIdentifier"] = value
	return {"phase": phase, "sourceConditionSets": conditions, "visual": visual, "animation": {"clip": clip, "mode": mode}, "nextPhase": next_phase, "transitionAuthority": "deterministic-simulation"}


func _fixture_visuals(lifecycle: Dictionary) -> Dictionary:
	var clips_by_path: Dictionary = {}
	for phase_value in lifecycle.phases:
		var phase: Dictionary = phase_value
		if String(phase.visual.mode) != "glb":
			continue
		var path := String(phase.visual.glb)
		if not clips_by_path.has(path):
			clips_by_path[path] = []
		if typeof(phase.animation.clip) == TYPE_STRING and not (clips_by_path[path] as Array).has(String(phase.animation.clip)):
			(clips_by_path[path] as Array).append(String(phase.animation.clip))
	clips_by_path[String(lifecycle.bib.visual.glb)] = []
	var components: Dictionary = lifecycle.get("components", {}) as Dictionary
	var door: Dictionary = components.get("door", {}) as Dictionary
	for door_value in door.values():
		var door_row: Dictionary = door_value
		clips_by_path[String(door_row.path)] = []
	var result: Dictionary = {}
	for path_value in clips_by_path:
		result[String(path_value)] = _fixture_model(clips_by_path[path_value] as Array)
	return result


func _fixture_model(clips: Array) -> Node3D:
	var node := Node3D.new()
	var mesh_instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(4.0, 4.0, 4.0)
	mesh_instance.mesh = mesh
	mesh_instance.position.y = 2.0
	node.add_child(mesh_instance)
	if not clips.is_empty():
		var player := AnimationPlayer.new()
		var library := AnimationLibrary.new()
		for value in clips:
			var animation := Animation.new()
			animation.length = 2.0
			library.add_animation(String(value), animation)
		player.add_animation_library("", library)
		node.add_child(player)
	return node


func _entity(spec: Dictionary) -> Dictionary:
	return {"id": 100 + int(spec.maximum), "team": 0, "structure_kind": String(spec.kind), "maximum_health": int(spec.maximum), "health": int(spec.maximum), "construction_progress": 1.0}


func _requests_are_contract_only(requests: Array[Dictionary]) -> bool:
	if requests.size() < 3 or requests.size() > 5:
		return false
	var allowed_fx := ["", "FX_BuildingDamaged", "FX_BuildingReallyDamaged", "FX_StructureMediumCollapse", "FX_FortressDamaged", "FX_FortressReallyDamaged", "FX_FortressCollapse"]
	var allowed_audio := ["", "BuildingSink"]
	for request in requests:
		if String(request.get("enteringFx", "")) not in allowed_fx or String(request.get("audioEvent", "")) not in allowed_audio:
			return false
		for value in request.get("particleSystemIds", []) as Array:
			if String(value) != "SmokeBuildingMediumRubble":
				return false
	return true


func _free_fixtures(fixtures: Dictionary) -> void:
	for value in fixtures.values():
		if value is Node:
			(value as Node).free()


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
		print("RETAIL_STRUCTURE_DAMAGE_EFFECTS PASS %s" % label)
	else:
		failed += 1
		push_error("RETAIL_STRUCTURE_DAMAGE_EFFECTS FAIL %s" % label)


func _watchdog_timeout() -> void:
	if failed == 0:
		failed += 1
		push_error("RETAIL_STRUCTURE_DAMAGE_EFFECTS FAIL watchdog_timeout")
	_finish()


func _finish() -> void:
	print("RETAIL_STRUCTURE_DAMAGE_EFFECTS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
