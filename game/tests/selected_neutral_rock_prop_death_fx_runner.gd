extends SceneTree

const Watchdog = preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var watchdog = Watchdog.new()
var fixture_shake_count := 0


func _initialize() -> void:
	watchdog.start(self, "SELECTED_NEUTRAL_ROCK_PROP_DEATH_FX_RUNNER")
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	OS.set_environment("OPENBFME_SCENARIO_MAP_PLACEMENTS", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1280, 720)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check(packed != null, "scene_parses")
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check(bool(slice.ready_ok), "selected_slice_ready", String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.queue_free()
		_finish()
		return

	slice.battlefield.observability_enabled = true
	var prop_id := int(slice.simulation.spawn_scenario_prop("RockBigTroll", Vector2(17.0, 23.0), "map-placement"))
	_check(prop_id > 0, "rock_prop_admitted")
	slice._sync_presentation()
	var visual = slice.scenario_prop_nodes.get(prop_id)
	_check(visual != null, "rock_prop_visual_instantiated")
	if visual != null:
		_check(visual.has_method("consume_authoritative_removal_presentation"), "prop_visual_exposes_removal_route")
		var receipt: Variant = visual.get("removal_presentation")
		_check(typeof(receipt) == TYPE_DICTIONARY, "typed_removal_receipt")
		if typeof(receipt) == TYPE_DICTIONARY:
			var row := receipt as Dictionary
			_check(String(row.get("module", "")) == "FXListDie", "exact_module")
			_check(String(row.get("deathFx", "")) == "FX_RockImpactHit", "exact_death_fx")
			_check(String(row.get("runtimeStatus", "")) == "deferred", "deferred_status_preserved")
			_check(String(row.get("sourceIni", "")) == "data/ini/object/nature/naturerocks.ini" and int(row.get("line", 0)) == 390, "exact_field_provenance")

	# The presentation layer observes an already-authoritative removal. This is
	# deliberately a test mutation: production does not invent prop health or a
	# death timer merely to make the authored FXListDie reachable.
	slice.simulation.scenario_props.erase(prop_id)
	slice._sync_presentation()
	_check(not slice.scenario_prop_nodes.has(prop_id), "authoritative_removal_removes_visual")
	var route: Dictionary = slice.battlefield.last_scenario_prop_death_route
	_check(String(route.get("objectId", "")) == "RockBigTroll", "route_keeps_object_identity", str(route))
	_check(String(route.get("deathFx", "")) == "FX_RockImpactHit", "route_keeps_exact_fx", str(route))
	_check(String(route.get("status", "")) == "sealed-authored-fx-route-presented", "selected_land_route_is_sealed_and_presented", str(route))
	_check(bool(route.get("presented", false)) and int(route.get("emitterCount", 0)) == 2, "selected_land_route_uses_two_authored_emitters", str(route))
	_check((route.get("particleSystemIds", []) as Array) == ["TrebuchetImpactDebris", "TrebuchetImpactDust"], "selected_land_route_preserves_authored_particle_order", str(route))
	_check(int(route.get("audioPlayerCount", 0)) == 1 and int(route.get("shakeCount", 0)) == 1, "selected_land_route_executes_audio_and_shake", str(route))
	var route_count: int = int(slice.battlefield.scenario_prop_death_route_log.size())
	slice._sync_presentation()
	_check(slice.battlefield.scenario_prop_death_route_log.size() == route_count, "removal_route_is_exactly_once")

	# A passive prop without FXListDie removes silently.
	var web_id := int(slice.simulation.spawn_scenario_prop("SpiderWebs01", Vector2(31.0, 37.0), "map-placement"))
	slice._sync_presentation()
	slice.simulation.scenario_props.erase(web_id)
	slice._sync_presentation()
	_check(slice.battlefield.scenario_prop_death_route_log.size() == route_count, "undeclared_prop_does_not_invent_fx")

	# The selected sealed binding also preserves the mutually exclusive water
	# nuggets. Route the exact document through the water predicate before the
	# synthetic corruption controls below.
	var content_db = root.get_node("ContentDB")
	var sealed_doc: Dictionary = content_db.get_scenario_prop_runtime("rotwk", "RockBigTroll", "map-placement")
	_check(not sealed_doc.is_empty(), "selected_rock_prop_document_present")
	if sealed_doc.is_empty():
		slice.cleanup_for_test()
		slice.queue_free()
		await process_frame
		_finish()
		return
	var scenario_visual_script = load("res://src/retail_slice/retail_scenario_visual.gd")
	var selected_water_visual = scenario_visual_script.new()
	_check(selected_water_visual.configure(sealed_doc, {"position": Vector2(41, 43)}, slice.source_map_data.local_transform_scale, "prop"), "selected_water_binding_configures", String(selected_water_visual.contract_error))
	if String(selected_water_visual.contract_error) == "":
		slice.add_child(selected_water_visual)
		var water_request: Dictionary = selected_water_visual.consume_authoritative_removal_presentation()
		water_request["surfaceKind"] = "water"
		water_request["position"] = Vector3(41, 0, 43)
		var water_route: Dictionary = slice.battlefield.route_scenario_prop_death_effect(
			water_request,
			Callable(slice.audio_system, "play_sealed_scenario_event"),
			Callable(self, "_fixture_shake")
		)
		_check(String(water_route.get("status", "")) == "sealed-authored-fx-route-presented", "selected_water_route_is_sealed_and_presented", str(water_route))
		_check((water_route.get("particleSystemIds", []) as Array) == ["ProjectileSplash01a", "ProjectileSplash02a", "ProjectileSplashSml03"], "selected_water_route_preserves_three_authored_splashes", str(water_route))
		_check(int(water_route.get("emitterCount", 0)) == 3 and int(water_route.get("audioPlayerCount", 0)) == 1 and int(water_route.get("shakeCount", 0)) == 1, "selected_water_route_executes_exact_effect_set", str(water_route))
	selected_water_visual.queue_free()

	# Exercise corruption controls without mutating the selected sealed pack.
	sealed_doc = sealed_doc.duplicate(true)
	(sealed_doc.presentation as Dictionary)["deathFxBinding"] = _sealed_binding()
	var sealed_visual = scenario_visual_script.new()
	_check(sealed_visual.configure(sealed_doc, {"position": Vector2(41, 43)}, slice.source_map_data.local_transform_scale, "prop"), "sealed_binding_configures", String(sealed_visual.contract_error))
	if String(sealed_visual.contract_error) == "":
		slice.add_child(sealed_visual)
		var request: Dictionary = sealed_visual.consume_authoritative_removal_presentation()
		_check(typeof(request.get("deathFxBinding")) == TYPE_DICTIONARY, "sealed_route_consumed_by_visual")
	sealed_visual.queue_free()
	var broken_doc := sealed_doc.duplicate(true)
	((broken_doc.presentation as Dictionary).deathFxBinding as Dictionary).fxListId = "FX_Invented"
	var broken_visual = scenario_visual_script.new()
	_check(not broken_visual.configure(broken_doc, {"position": Vector2.ZERO}, slice.source_map_data.local_transform_scale, "prop"), "drifted_sealed_binding_fails_closed")
	broken_visual.free()

	# The selected Wild pack already carries the same retail-proven debris/dust
	# particle definitions and ImpactEntRock PCM leaves. Assemble an in-memory
	# sealed route from those real artifacts so execution is tested before the
	# neutral pack is recooked.
	fixture_shake_count = 0
	var live := _live_land_binding(content_db)
	_check(not live.is_empty(), "selected_wild_land_binding_sources_present")
	if not live.is_empty():
		var live_request := {
			"schema": "openbfme.scenario-prop-removal-presentation-route", "schemaVersion": 0,
			"objectId": "RockBigTroll", "module": "FXListDie", "carrier": "Behavior",
			"deathFx": "FX_RockImpactHit", "sourceIni": "data/ini/object/nature/naturerocks.ini", "line": 390,
			"trigger": "authoritative-scenario-prop-removal", "position": Vector3(11, 0, 13),
			"sourceYaw": 0.75, "sourceScale": slice.source_map_data.local_transform_scale, "surfaceKind": "land", "packRoot": live.pack_root,
			"runtimeStatus": "deferred", "deathFxBinding": live.binding,
		}
		var live_route: Dictionary = slice.battlefield.route_scenario_prop_death_effect(
			live_request,
			Callable(slice.audio_system, "play_sealed_scenario_event"),
			Callable(self, "_fixture_shake")
		)
		_check(bool(live_route.get("presented", false)), "sealed_land_route_presented", str(live_route))
		_check(int(live_route.get("emitterCount", 0)) == 2, "land_predicate_selects_debris_and_dust", str(live_route))
		_check(int(live_route.get("audioPlayerCount", 0)) == 1 and int(live_route.get("shakeCount", 0)) == 1 and fixture_shake_count == 1, "audio_and_subtle_shake_execute", str(live_route))
		_check((live_route.get("particleSystemIds", []) as Array) == ["TrebuchetImpactDebris", "TrebuchetImpactDust"], "ordered_particle_nuggets_preserved", str(live_route))
		_check(String(slice.audio_system.last_route_result.get("source", "")) == "sealed-neutral-prop-death-fx", "audio_uses_existing_retail_sfx_router")
		_check(String(slice.audio_system.last_route_result.get("event_id", "")) == "ImpactEntRock" and int(slice.audio_system.last_route_result.get("limit", 0)) == 3, "sealed_audio_event_semantics_preserved")

	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	_finish()


func _check(condition: bool, label: String, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("%s%s" % [label, " :: " + detail if detail != "" else ""])


func _finish() -> void:
	watchdog.stop()
	if failed == 0:
		print("SELECTED_NEUTRAL_ROCK_PROP_DEATH_FX_OK passed=%d" % passed)
		quit(0)
	else:
		print("SELECTED_NEUTRAL_ROCK_PROP_DEATH_FX_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)


func _sealed_binding() -> Dictionary:
	var span := {"startLine": 1, "endLine": 1, "byteLength": 1, "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
	var names := ["TrebuchetImpactDebris", "TrebuchetImpactDust", "ProjectileSplash01a", "ProjectileSplash02a", "ProjectileSplash03"]
	var land := func(particle_name: String) -> Dictionary:
		return {"kind": "ParticleSystem", "assignments": [
			{"field": "Name", "value": particle_name, "sourceSpan": span.duplicate()},
			{"field": "OrientToObject", "value": "Yes", "sourceSpan": span.duplicate()},
			{"field": "OnlyIfOnLand", "value": "Yes", "sourceSpan": span.duplicate()},
		], "sourceSpan": span.duplicate()}
	var water := func(particle_name: String) -> Dictionary:
		return {"kind": "ParticleSystem", "assignments": [
			{"field": "Name", "value": particle_name, "sourceSpan": span.duplicate()},
			{"field": "Offset", "value": "X:0.0 Y:0.0 Z:1.0", "sourceSpan": span.duplicate()},
			{"field": "OnlyIfOnWater", "value": "Yes", "sourceSpan": span.duplicate()},
		], "sourceSpan": span.duplicate()}
	var nuggets: Array = [land.call(names[0]), land.call(names[1])]
	nuggets.append({"kind": "ViewShake", "assignments": [{"field": "Type", "value": "SUBTLE", "sourceSpan": span.duplicate()}], "sourceSpan": span.duplicate()})
	nuggets.append({"kind": "Sound", "assignments": [{"field": "Name", "value": "ImpactEntRock", "sourceSpan": span.duplicate()}], "sourceSpan": span.duplicate()})
	for particle_name in names.slice(2):
		nuggets.append(water.call(particle_name))
	return {
		"schema": "openbfme.neutral-prop-death-fx-binding", "schemaVersion": 0,
		"objectId": "RockBigTroll", "fxListId": "FX_RockImpactHit",
		"moduleReceipt": {}, "sourceSpan": span.duplicate(),
		"authoredNuggets": nuggets, "presentationStatus": "sealed-authored-route",
		"particleClosureSha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
		"audioClosure": {"rootIds": ["ImpactEntRock"], "events": [], "multisounds": [], "tracks": [], "sampleIds": ["a", "b", "c", "d", "e"]},
		"audioBindings": {"ImpactEntRock": [
			"assets/audio/neutral-props/rockbigtroll/a.wav", "assets/audio/neutral-props/rockbigtroll/b.wav",
			"assets/audio/neutral-props/rockbigtroll/c.wav", "assets/audio/neutral-props/rockbigtroll/d.wav",
			"assets/audio/neutral-props/rockbigtroll/e.wav",
		]},
		"particleBindings": {
			"schema": "openbfme.ability-fx-bindings", "schemaVersion": 0,
			"presentableFxListIds": ["FX_RockImpactHit"], "unresolved": [],
			"definitionRegistry": [{"definitionId": "TrebuchetImpactDebris"}],
			"textures": [{"resourceId": "fixture"}],
			"fxLists": [{"fxListId": "FX_RockImpactHit", "particleSystemIds": names, "hasViewShake": true, "audioEventIds": ["ImpactEntRock"]}],
		},
	}


func _live_land_binding(content_db: Node) -> Dictionary:
	var drogoth: Dictionary = content_db.get_playable_unit_runtime("Drogoth")
	var troll: Dictionary = content_db.get_playable_unit_runtime("GoblinCaveTroll")
	if drogoth.is_empty() or troll.is_empty():
		return {}
	var source_value: Variant = (drogoth.get("registration", {}) as Dictionary).get("fxBindings")
	var troll_audio_value: Variant = (troll.get("registration", {}) as Dictionary).get("audioBindings")
	if typeof(source_value) != TYPE_DICTIONARY or typeof(troll_audio_value) != TYPE_DICTIONARY:
		return {}
	var source := source_value as Dictionary
	var troll_audio := troll_audio_value as Dictionary
	if not troll_audio.has("ImpactEntRock"):
		return {}
	var definitions: Array = []
	var texture_ids: Dictionary = {}
	for row_value in source.definitionRegistry as Array:
		var row := row_value as Dictionary
		if bool(row.get("selectedForRuntime", false)) and String(row.get("definitionId", "")) in ["TrebuchetImpactDebris", "TrebuchetImpactDust"]:
			definitions.append(row.duplicate(true))
			for texture_id in row.get("textureResourceIds", []) as Array:
				texture_ids[String(texture_id)] = true
	var textures: Array = []
	for row_value in source.textures as Array:
		var row := row_value as Dictionary
		if texture_ids.has(String(row.get("resourceId", ""))):
			textures.append(row.duplicate(true))
	var binding := _sealed_binding()
	binding.particleBindings.definitionRegistry = definitions
	binding.particleBindings.textures = textures
	binding.particleBindings.fxLists[0].particleSystemIds = ["TrebuchetImpactDebris", "TrebuchetImpactDust"]
	binding.authoredNuggets = [
		binding.authoredNuggets[0], binding.authoredNuggets[1],
		binding.authoredNuggets[2], binding.authoredNuggets[3],
	]
	# The neutral compiler uses the effective block's last authored Sounds row,
	# matching SAGE's ordered flat-field semantics. Keep that exact five-leaf
	# shape in this pre-recook fixture instead of borrowing a global registry row.
	var sample_ids: Array = [
		"WIImpac_entRo2a", "WIImpac_entRo2b", "WIImpac_entRo2c",
		"WIImpac_entRo2d", "WIImpac_entRo2e",
	]
	var sounds: Array = []
	for sample_id in sample_ids:
		sounds.append({"id": sample_id})
	var event := {
		"id": "ImpactEntRock", "sounds": sounds,
		"parameters": [
			{"field": "Control", "value": "interrupt"},
			{"field": "Priority", "value": "lowest"},
			{"field": "Limit", "value": "3"},
			{"field": "PitchShift", "value": "-10 10"},
			{"field": "Volume", "value": "110"},
			{"field": "Type", "value": "world shrouded everyone"},
			{"field": "SubmixSlider", "value": "SoundFX"},
		],
	}
	binding.audioClosure = {
		"rootIds": ["ImpactEntRock"], "events": [event.duplicate(true)],
		"multisounds": [], "sampleIds": sample_ids, "tracks": [],
	}
	binding.audioBindings = {"ImpactEntRock": troll_audio["ImpactEntRock"]}
	return {"binding": binding, "pack_root": String(drogoth.get("_pack_root", ""))}


func _fixture_shake(shake_type: String, _origin: Vector3) -> bool:
	if shake_type != "SUBTLE":
		return false
	fixture_shake_count += 1
	return true
