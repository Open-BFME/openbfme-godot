extends SceneTree
## Focused four-unit retail audio routing audit. This runner never writes or
## copies retail payloads and exercises the enabled Godot playback path.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish([])
		return
	content_db.reload()
	var soldier_definition: Dictionary = content_db.get_bundle_object(AudioScript.SOLDIER_OBJECT_ID)
	var selected_pack_root := String(soldier_definition.get("_pack_root", ""))
	var external_root := OS.get_environment("OPENBFME_CONTENT")
	_check("selected_private_pack_root_available", selected_pack_root != "" and external_root != "" and mod_loader.path_is_within(external_root, selected_pack_root), selected_pack_root)
	if selected_pack_root == "":
		_finish([])
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	var compatibility_ready: bool = audio.configure(selected_pack_root, true)
	_check("legacy_soldier_music_compatibility_ready", compatibility_ready)
	_check("retail_playback_is_enabled", audio.playback_enabled)
	_check("non_spatial_players_use_real_godot_players", audio.music_player is AudioStreamPlayer and audio.voice_player is AudioStreamPlayer and audio.sfx_player is AudioStreamPlayer)
	_check("strict_four_unit_roster_audio_ready", audio.has_complete_roster_audio_closure())
	_check("legacy_select_leaf_count_preserved", audio.count_voice_kind("select") == 10, str(audio.count_voice_kind("select")))
	_check("soldier_attack_event_has_exact_six_retail_leaves", audio.count_voice_kind("attack") == 6, str(audio.count_voice_kind("attack")))

	for state in ["explore", "battle", "victory", "defeat"]:
		_check("music_%s_loaded" % state, audio.music_streams.has(state))
	var music_events: Array[Dictionary] = [
		{"sequence": 1, "kind": "music.explore", "entity_id": 0, "target_id": 0},
		{"sequence": 2, "kind": "music.battle", "entity_id": 0, "target_id": 0},
		{"sequence": 3, "kind": "music.victory", "entity_id": 0, "target_id": 0},
		{"sequence": 4, "kind": "music.defeat", "entity_id": 0, "target_id": 0},
	]
	audio.sync_events(music_events)
	_check("music_state_machine_intact", audio.current_music_state == "defeat", audio.current_music_state)
	_check("enabled_music_player_uses_declared_defeat_stream", audio.music_player.stream == audio.music_streams.get("defeat") and audio.music_player.playing)

	for object_id in AudioScript.ROSTER_OBJECT_IDS:
		var expected_by_kind: Dictionary = AudioScript.ROSTER_VOICE_EVENT_IDS[object_id]
		for kind in AudioScript.REQUIRED_VOICE_KINDS:
			var routed: Dictionary = audio.route_roster_voice(object_id, kind, 1)
			var expected_event := String(Array(expected_by_kind[kind])[0])
			var label := "%s_%s" % [_short_id(object_id), kind]
			_check(
				"%s_routes_exact_event" % label,
				bool(routed.get("ok", false))
				and String(routed.get("event_id", "")) == expected_event
				and String(routed.get("object_id", "")) == object_id
				and String(routed.get("kind", "")) == kind,
				str(routed)
			)
			_check("%s_uses_content_db_v1" % label, String(routed.get("source", "")) == "content-db-v1", str(routed))
			_check("%s_is_contained_header_valid_private_wav" % label, _is_valid_private_wav_route(routed, mod_loader, external_root), str(routed))
			var bound_by_kind: Dictionary = audio.roster_voice_routes.get(object_id, {})
			var route_definition: Dictionary = bound_by_kind.get(kind, {})
			var total_weight := _route_total_weight(route_definition)
			var repeated: Dictionary = audio.route_roster_voice(object_id, kind, 1)
			var wrapped: Dictionary = audio.route_roster_voice(object_id, kind, total_weight + 1)
			_check(
				"%s_variation_is_deterministic" % label,
				total_weight > 0
				and _route_signature(routed) == _route_signature(repeated)
				and _route_signature(routed) == _route_signature(wrapped),
				"weight=%d first=%s repeated=%s wrapped=%s" % [total_weight, _route_signature(routed), _route_signature(repeated), _route_signature(wrapped)]
			)

	var diagnostics: Array[String] = audio.readiness_diagnostics()
	_check("strict_readiness_has_zero_diagnostics", diagnostics.is_empty(), str(diagnostics))
	var ambient_diagnostics: Array[String] = audio.fords_ambient_readiness_diagnostics()
	_check("fords_ambient_map_contract_declares_exact_50_placements", audio.ambient_contract_declared, str(ambient_diagnostics))
	if audio.ambient_emitters.is_empty():
		_check("missing_ambient_pack_closure_fails_visibly", not ambient_diagnostics.is_empty() and _diagnostics_have_prefix(ambient_diagnostics, "missing-event:"), str(ambient_diagnostics))
	else:
		_check("available_ambient_streams_create_spatial_players", _valid_spatial_ambient_players(audio.ambient_emitters, mod_loader, external_root), str(ambient_diagnostics))
	var unsupported_semantics: Array[String] = audio._ambient_parameter_gaps("FocusedAmbient", {
		"parameters": [
			{"field": "Control", "value": "loop"},
			{"field": "Priority", "value": "lowest"},
			{"field": "Limit", "value": "2"},
			{"field": "PitchShift", "value": "-5 5"},
			{"field": "MinRange", "value": "300"},
			{"field": "MaxRange", "value": "800"},
			{"field": "Type", "value": "world everyone"},
			{"field": "SubmixSlider", "value": "Ambient"},
		]
	})
	_check("unsupported_range_loop_submix_priority_pitch_semantics_fail_visibly", _diagnostics_have_prefix(unsupported_semantics, "unsupported-attenuation-curve:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-loop-scheduler:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-submixslider:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-priority:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-pitchshift:"), str(unsupported_semantics))

	var soldier_select_1: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 1)
	var soldier_select_2: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 2)
	var soldier_select_11: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 11)
	_check("soldier_select_variation_advances", String(soldier_select_1.get("path", "")) != String(soldier_select_2.get("path", "")))
	_check("soldier_select_variation_wraps_deterministically", String(soldier_select_1.get("path", "")) == String(soldier_select_11.get("path", "")))
	var soldier_attack_1: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "attack", 1)
	var soldier_attack_7: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "attack", 7)
	_check("soldier_attack_variation_wraps_deterministically", String(soldier_attack_1.get("path", "")) == String(soldier_attack_7.get("path", "")))

	for event_id in AudioScript.REQUIRED_SFX_EVENT_IDS:
		var routed_sfx: Dictionary = audio.route_audio_event(event_id, 1)
		_check("sfx_%s_routes_to_private_leaf" % event_id.to_snake_case(), bool(routed_sfx.get("ok", false)) and mod_loader.path_is_within(external_root, String(routed_sfx.get("path", ""))), str(routed_sfx))
	var bow_1: Dictionary = audio.route_audio_event("ArrowDrawBow", 1)
	var bow_2: Dictionary = audio.route_audio_event("ArrowDrawBow", 2)
	var bow_11: Dictionary = audio.route_audio_event("ArrowDrawBow", 11)
	_check("sfx_variation_advances", String(bow_1.get("path", "")) != String(bow_2.get("path", "")))
	_check("sfx_variation_wraps_deterministically", String(bow_1.get("path", "")) == String(bow_11.get("path", "")))

	var missing: Dictionary = audio.route_audio_event("DefinitelyMissingRetailEvent", 9)
	_check("missing_event_rejected_without_fallback", not bool(missing.get("ok", true)) and String(missing.get("reason", "")) == "missing_event")
	var unknown_roster: Dictionary = audio.route_roster_voice("bfme2.object.not-a-men-unit", "select", 1)
	_check("unknown_roster_object_fails_closed", not bool(unknown_roster.get("ok", true)) and String(unknown_roster.get("reason", "")) == "unknown_roster_object")
	var unknown_kind: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "retreat", 1)
	_check("unknown_voice_kind_fails_closed", not bool(unknown_kind.get("ok", true)) and String(unknown_kind.get("reason", "")) == "unknown_voice_kind")
	var valid_path := String(bow_1.get("path", ""))
	audio.audio_event_routes["corrupt.test"] = {
		"event_id": "Corrupt.Test",
		"source": "focused-runner",
		"leaves": [{"sample_id": "broken", "path": valid_path, "stream": null, "weight": 1}],
	}
	var corrupt: Dictionary = audio.route_audio_event("Corrupt.Test", 1)
	_check("corrupt_event_rejected_before_playback", not bool(corrupt.get("ok", true)) and String(corrupt.get("reason", "")) == "corrupt_event")
	audio.audio_event_routes.erase("corrupt.test")

	var intent_events: Array[Dictionary] = music_events.duplicate(true)
	intent_events.append_array([
		{"sequence": 5, "kind": "voice.select", "entity_id": 2, "target_id": 0},
		{"sequence": 6, "kind": "order.move", "entity_id": 102, "target_id": 0},
		{"sequence": 7, "kind": "voice.attack", "entity_id": 103, "target_id": 101},
		{"sequence": 8, "kind": "combat.swing", "entity_id": 2, "target_id": 101},
		{"sequence": 9, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 2001},
		{"sequence": 10, "kind": "battalion.defeated", "entity_id": 1, "target_id": 103},
		{"sequence": 11, "kind": "structure.destroyed", "entity_id": 1, "target_id": 2001},
		{"sequence": 12, "kind": "production.complete", "entity_id": 1003, "target_id": 10, "object_id": AudioScript.KNIGHT_OBJECT_ID},
		{"sequence": 13, "kind": "voice.select", "entity_id": 10, "target_id": 0},
		{"sequence": 14, "kind": "battalion.defeated", "entity_id": 101, "target_id": 10},
	])
	audio._next_event_index = 4
	var horse_impacts_before_intents := _routing_log_count(audio.routing_log, "ImpactHorse", true)
	audio.sync_events(intent_events)
	var routed_horse_impacts := _routing_log_count(audio.routing_log, "ImpactHorse", true) - horse_impacts_before_intents
	_check("archer_swing_routes_bow_sfx", _routing_log_has(audio.routing_log, "ArrowDrawBow", true))
	_check("building_hit_routes_stone_sfx", _routing_log_has(audio.routing_log, "BuildingLightDamageStone", true))
	_check("knight_defeat_routes_horse_impact", routed_horse_impacts == 2 and String(audio._entity_object_ids.get(103, "")) == AudioScript.KNIGHT_OBJECT_ID, "routed_impacts=%d fixed_103=%s" % [routed_horse_impacts, String(audio._entity_object_ids.get(103, ""))])
	_check("structure_destroy_routes_heavy_stone_sfx", _routing_log_has(audio.routing_log, "BuildingHeavyDamageStone", true))
	_check("structure_destroy_routes_sink_sfx", _routing_log_has(audio.routing_log, "BuildingSink", true))
	_check("dynamic_production_tracks_exact_object_id", _routing_log_has(audio.routing_log, "GondorKnightVoiceSelectMS", true) and not audio._entity_object_ids.has(10))
	_check("enabled_voice_and_sfx_players_received_real_pack_streams", audio.voice_player.playing and audio.voice_player.stream is AudioStreamWAV and audio.sfx_player.playing and audio.sfx_player.stream is AudioStreamWAV)

	audio.dispose()
	audio.free()
	# Give the audio server a bounded teardown window after this function returns:
	# routed-result dictionaries release their streams immediately, while active
	# playback objects are retired by the audio thread before leak accounting.
	create_timer(0.5, true, false, true).timeout.connect(_finish.bind(diagnostics))


func _short_id(object_id: String) -> String:
	return object_id.trim_prefix("bfme2.object.").replace("-", "_")


func _is_valid_private_wav_route(route: Dictionary, mod_loader: Node, external_root: String) -> bool:
	var path := String(route.get("path", ""))
	var stream: Variant = route.get("stream")
	if (
		not bool(route.get("ok", false))
		or external_root == ""
		or not mod_loader.call("path_is_within", external_root, path)
		or not path.to_lower().ends_with(".wav")
		or not FileAccess.file_exists(path)
		or not (stream is AudioStreamWAV)
	):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() < 44:
		return false
	var riff := file.get_buffer(4).get_string_from_ascii()
	file.seek(8)
	var wave := file.get_buffer(4).get_string_from_ascii()
	return riff == "RIFF" and wave == "WAVE" and (stream as AudioStreamWAV).get_data().size() > 0


func _route_total_weight(route: Dictionary) -> int:
	var total := 0
	for leaf_value in Array(route.get("leaves", [])):
		if typeof(leaf_value) == TYPE_DICTIONARY:
			total += maxi(0, int((leaf_value as Dictionary).get("weight", 0)))
	return total


func _route_signature(route: Dictionary) -> String:
	return "%s|%s|%d" % [
		String(route.get("sample_id", "")),
		String(route.get("path", "")),
		int(route.get("variation_index", -1)),
	]


func _routing_log_has(log: Array[Dictionary], event_id: String, expected_ok: bool) -> bool:
	for row in log:
		if String(row.get("event_id", "")) == event_id and bool(row.get("ok", false)) == expected_ok:
			return true
	return false


func _routing_log_count(log: Array[Dictionary], event_id: String, expected_ok: bool) -> int:
	var count := 0
	for row in log:
		if String(row.get("event_id", "")) == event_id and bool(row.get("ok", false)) == expected_ok:
			count += 1
	return count


func _diagnostics_have_prefix(diagnostics: Array[String], prefix: String) -> bool:
	for diagnostic in diagnostics:
		if diagnostic.begins_with(prefix):
			return true
	return false


func _valid_spatial_ambient_players(emitters: Array[Dictionary], mod_loader: Node, external_root: String) -> bool:
	if emitters.size() != AudioScript.FORDS_AMBIENT_PLACEMENT_COUNT:
		return false
	for emitter in emitters:
		var player: Variant = emitter.get("player")
		var path := String(emitter.get("path", ""))
		if not (player is AudioStreamPlayer3D) or not mod_loader.path_is_within(external_root, path) or not FileAccess.file_exists(path):
			return false
		if (player as AudioStreamPlayer3D).stream == null or (player as AudioStreamPlayer3D).position != emitter.get("position"):
			return false
	return true


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish(diagnostics: Array[String]) -> void:
	for diagnostic in diagnostics:
		print("AUDIO_READINESS_GAP %s" % diagnostic)
	print("RETAIL_FOUR_UNIT_AUDIO_RESULT passed=%d failed=%d missing=%d" % [passed, failed, diagnostics.size()])
	quit(0 if failed == 0 else 1)
