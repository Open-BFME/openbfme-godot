extends SceneTree
## Production-path regression for E-BL-202. Unlike the older synthetic SFX
## probe, this drives the live AnimationSound clock consumer and event-history
## battalion.defeated consumer that leaked hidden combat audio.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

const TREBUCHET_ID := "bfme2.object.gondor-trebuchet"
const FIGHTER_ID := "bfme2.object.gondor-fighter"
const CLOCK_ENTITY := 7101
const DEFEATED_ENTITY := 7102
const ATTACKER_ENTITY := 7103
const LISTENER := Vector2(100.0, 100.0)
const HIDDEN_POSITION := Vector2(1500.0, 1500.0)
const TREBUCHET_EVENT := "TrebuchetLaunchVoice"


class ClockBattalion extends Node:
	var object_id := TREBUCHET_ID
	var entity_id := CLOCK_ENTITY
	var member_count := 1
	var member_current_clips: Dictionary = {}
	var current_frame := 23.0
	var _mount_presentation_applied := false

	func member_clip_frame(_member_index: int) -> Dictionary:
		return {
			"clip": "GUSiegTreb_SKL.GUSiegTreb_ATAK",
			"frame": current_frame,
			"lengthFrames": 60.0,
			"backwards": false,
		}


var passed := 0
var failed := 0
var hidden_players := 0
var _runner_watchdog := RunnerWatchdogScript.new()
var _positions: Dictionary = {}
var _hidden_positions: Dictionary = {}
var _report: Dictionary = {
	"schema": "openbfme.audio-fog-production",
	"schemaVersion": 1,
	"sourceEvidence": "E-BL-202",
}


func _initialize() -> void:
	_runner_watchdog.start(self, "AUDIO_FOG_PRODUCTION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	_check("autoloads_available", content_db != null)
	if content_db == null:
		_finish(null, null)
		return
	var host_meta := PackCapabilityScript.resolve_host_slice_pack(content_db.pack_meta)
	var host_root := String(host_meta.get("root", ""))
	var rotwk_meta := _pack_meta(content_db.pack_meta, "rotwk-men-vslice")
	var authored_definition: Dictionary = content_db.get_retail_audio_event(TREBUCHET_EVENT)
	var authored_parameters := _parameters(authored_definition)
	var definition_root := String(authored_definition.get("_pack_root", ""))
	var definition_owner := _pack_id_for_root(content_db.pack_meta, definition_root)
	_check("real_host_pack_resolved", host_root != "", host_root)
	_check("rotwk_runtime_pack_mounted", not rotwk_meta.is_empty(), str(rotwk_meta.get("root", "")))
	_check(
		"rotwk_pack_does_not_claim_missing_audio_manifest",
		not (rotwk_meta.get("files", {}) as Dictionary).has("audioEvents"),
		str((rotwk_meta.get("files", {}) as Dictionary).get("audioEvents", ""))
	)
	_check(
		"supplemental_definition_provenance_is_not_rotwk",
		definition_owner == "bfme2-men-vslice"
			and String(authored_parameters.get("type", "")).to_lower().contains("world")
			and String(authored_parameters.get("type", "")).to_lower().contains("shrouded"),
		"owner=%s type=%s" % [definition_owner, authored_parameters.get("type", "")]
	)
	_report["provenance"] = {
		"rotwkRuntimePackId": String(rotwk_meta.get("id", "")),
		"rotwkRuntimePackRoot": String(rotwk_meta.get("root", "")),
		"rotwkAudioEventsDeclared": (rotwk_meta.get("files", {}) as Dictionary).has("audioEvents"),
		"definitionEventId": TREBUCHET_EVENT,
		"definitionPackId": definition_owner,
		"definitionSource": String(authored_definition.get("_source", "")),
		"definitionType": String(authored_parameters.get("type", "")),
		"definitionMinRange": float(authored_parameters.get("minrange", -1.0)),
		"definitionMaxRange": float(authored_parameters.get("maxrange", -1.0)),
		"parityClaim": "BFME2 supplemental attribution only; not RotWK v9.7.7 parity",
	}
	if host_root == "" or rotwk_meta.is_empty():
		_finish(null, null)
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(host_root, true)
	audio.configure_spatial_audio(_position_probe, _visibility_probe, _listener_probe, 1.0)
	_check(
		"real_rotwk_trebuchet_animation_contract_loaded",
		_has_animation_event(audio, TREBUCHET_ID, TREBUCHET_EVENT),
		str(audio.playable_unit_animation_sounds.get(TREBUCHET_ID, []))
	)
	var battalion := ClockBattalion.new()
	root.add_child(battalion)
	battalion.add_to_group("retail_battalion")

	# The actual per-frame production consumer must resolve the battalion's sim
	# position before it calls the mixer. Hidden ground produces no player.
	_positions[CLOCK_ENTITY] = HIDDEN_POSITION
	_hidden_positions[HIDDEN_POSITION] = true
	_trigger_clock(audio, battalion)
	hidden_players += _sounding_sfx(audio).size()
	_check("hidden_animation_clock_reaches_no_player", _sounding_sfx(audio).is_empty())
	_check(
		"hidden_animation_clock_records_exact_shroud_cull",
		int(audio.shrouded_sfx_drops.get(TREBUCHET_EVENT, 0)) == 1
			and String(audio.last_spatial_sfx_receipt.get("culled", "")) == "shroud"
			and audio.last_spatial_sfx_receipt.get("position") == HIDDEN_POSITION,
		str(audio.last_spatial_sfx_receipt)
	)

	# Visible controls retain the mounted definition's 140/800 range behavior.
	_hidden_positions.clear()
	_positions[CLOCK_ENTITY] = LISTENER + Vector2(100.0, 0.0)
	_silence(audio)
	_trigger_clock(audio, battalion)
	var near_player := _event_player(audio, TREBUCHET_EVENT)
	var near_db := float(near_player.volume_db) if near_player != null else 999.0
	_check(
		"visible_near_animation_clock_plays_at_full_range_gain",
		near_player != null and bool(audio.last_spatial_sfx_receipt.get("ranges_authored", false))
			and is_equal_approx(float(audio.last_spatial_sfx_receipt.get("gain", -1.0)), 1.0),
		str(audio.last_spatial_sfx_receipt)
	)
	_positions[CLOCK_ENTITY] = LISTENER + Vector2(280.0, 0.0)
	_silence(audio)
	_trigger_clock(audio, battalion)
	var far_player := _event_player(audio, TREBUCHET_EVENT)
	var far_db := float(far_player.volume_db) if far_player != null else 999.0
	_check(
		"visible_far_animation_clock_uses_authored_inverse_range",
		far_player != null and is_equal_approx(float(audio.last_spatial_sfx_receipt.get("gain", -1.0)), 0.5)
			and absf((near_db - far_db) - -linear_to_db(0.5)) < 0.01,
		"near_db=%s far_db=%s receipt=%s" % [near_db, far_db, audio.last_spatial_sfx_receipt]
	)
	_positions[CLOCK_ENTITY] = LISTENER + Vector2(800.0, 0.0)
	_silence(audio)
	_trigger_clock(audio, battalion)
	_check(
		"authored_max_range_blocks_animation_clock",
		_event_player(audio, TREBUCHET_EVENT) == null
			and int(audio.distance_sfx_drops.get(TREBUCHET_EVENT, 0)) == 1,
		str(audio.last_spatial_sfx_receipt)
	)

	# A disappeared/unresolvable battalion is blocked, never promoted to a flat
	# global sound. The exact event stays enumerable for owner follow-up.
	_positions.erase(CLOCK_ENTITY)
	_silence(audio)
	_trigger_clock(audio, battalion)
	hidden_players += _sounding_sfx(audio).size()
	_check(
		"unresolved_world_animation_is_blocked_and_enumerated",
		_event_player(audio, TREBUCHET_EVENT) == null
			and int(audio.unpositioned_world_sfx_blocks.get(TREBUCHET_EVENT, 0)) == 1
			and audio.sfx_semantics_gaps.has("missing-world-position:%s" % TREBUCHET_EVENT),
		str(audio.unpositioned_world_sfx_blocks)
	)

	# The real simulation event-history consumer must shroud-gate both the world
	# death voice and its bodyfall. It may not use the global voice player merely
	# because the bodyfall path was already spatialized.
	_positions[DEFEATED_ENTITY] = HIDDEN_POSITION
	_positions[ATTACKER_ENTITY] = HIDDEN_POSITION
	_hidden_positions[HIDDEN_POSITION] = true
	audio._entity_object_ids[DEFEATED_ENTITY] = FIGHTER_ID
	audio._entity_object_ids[ATTACKER_ENTITY] = FIGHTER_ID
	_silence(audio)
	audio._next_event_index = 0
	audio.sync_events([{
		"sequence": 51, "kind": "battalion.defeated",
		"entity_id": ATTACKER_ENTITY, "target_id": DEFEATED_ENTITY,
		"object_id": FIGHTER_ID,
	}])
	hidden_players += (1 if audio.voice_player.playing else 0) + _sounding_sfx(audio).size()
	_check("hidden_defeated_world_voice_reaches_no_player", not audio.voice_player.playing)
	_check("hidden_defeated_bodyfall_reaches_no_player", _sounding_sfx(audio).is_empty())
	_check(
		"hidden_defeated_routes_record_shroud_culls",
		int(audio.shrouded_sfx_drops.get("HumanVoiceDie", 0)) >= 1,
		str(audio.shrouded_sfx_drops)
	)

	_hidden_positions.clear()
	_positions[DEFEATED_ENTITY] = LISTENER
	_positions[ATTACKER_ENTITY] = LISTENER
	audio._entity_object_ids[DEFEATED_ENTITY] = FIGHTER_ID
	audio._entity_object_ids[ATTACKER_ENTITY] = FIGHTER_ID
	var visible_death_route: Dictionary = audio.route_roster_voice(FIGHTER_ID, "death", 52)
	_silence(audio)
	audio._next_event_index = 0
	audio.sync_events([{
		"sequence": 52, "kind": "battalion.defeated",
		"entity_id": ATTACKER_ENTITY, "target_id": DEFEATED_ENTITY,
		"object_id": FIGHTER_ID,
	}])
	_check(
		"visible_defeated_world_voice_follows_authored_route",
		audio.voice_player.playing
			and String(audio.voice_player.get_meta("retail_event_id", "")) == "HumanVoiceDie"
			and is_equal_approx(
				audio.voice_player.volume_db,
				UserSettingsScript.volume_to_db(audio.voice_sfx_volume, audio.muted)
					+ float(visible_death_route.get("volume_db", 0.0))
			)
			and is_equal_approx(
				audio.voice_player.pitch_scale,
				float(visible_death_route.get("pitch_scale", 1.0))
			),
		"event=%s volume=%s expected_volume=%s pitch=%s expected_pitch=%s" % [
			audio.voice_player.get_meta("retail_event_id", ""),
			audio.voice_player.volume_db,
			UserSettingsScript.volume_to_db(audio.voice_sfx_volume, audio.muted)
				+ float(visible_death_route.get("volume_db", 0.0)),
			audio.voice_player.pitch_scale,
			visible_death_route.get("pitch_scale", 1.0),
		]
	)
	_check("visible_defeated_bodyfall_follows_authored_route", not _sounding_sfx(audio).is_empty())

	_report["runtime"] = {
		"hiddenPlayers": hidden_players,
		"shroudedDrops": audio.shrouded_sfx_drops.duplicate(true),
		"distanceDrops": audio.distance_sfx_drops.duplicate(true),
		"unpositionedWorldBlocks": audio.unpositioned_world_sfx_blocks.duplicate(true),
		"semanticGaps": audio.sfx_semantics_gaps.keys(),
	}
	_finish(audio, battalion)


func _trigger_clock(audio, battalion: ClockBattalion) -> void:
	audio._consumed_clip_frames.clear()
	battalion.current_frame = 23.0
	audio.consume_battalion_animation_sound_clocks()
	battalion.current_frame = 24.0
	audio.consume_battalion_animation_sound_clocks()


func _position_probe(id: int, _is_structure: bool) -> Variant:
	return _positions.get(id, null)


func _visibility_probe(position: Vector2) -> bool:
	return not _hidden_positions.has(position)


func _listener_probe() -> Variant:
	return LISTENER


func _pack_meta(rows: Array, id: String) -> Dictionary:
	for row_value in rows:
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("id", "")) == id:
			return row_value as Dictionary
	return {}


func _pack_id_for_root(rows: Array, pack_root: String) -> String:
	for row_value in rows:
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("root", "")) == pack_root:
			return String((row_value as Dictionary).get("id", ""))
	return ""


func _parameters(definition: Dictionary) -> Dictionary:
	var output: Dictionary = {}
	for row_value in definition.get("parameters", []) as Array:
		if typeof(row_value) == TYPE_DICTIONARY:
			var row := row_value as Dictionary
			output[String(row.get("field", "")).to_lower()] = String(row.get("value", ""))
	return output


func _has_animation_event(audio, object_id: String, event_id: String) -> bool:
	for row_value in audio.playable_unit_animation_sounds.get(object_id, []) as Array:
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("eventId", "")) == event_id:
			return true
	return false


func _event_player(audio, event_id: String) -> AudioStreamPlayer:
	for player in audio.sfx_players:
		if player != null and is_instance_valid(player) and player.playing and String(player.get_meta("retail_event_id", "")) == event_id:
			return player
	return null


func _sounding_sfx(audio) -> Array:
	var result: Array = []
	for player in audio.sfx_players:
		if player != null and is_instance_valid(player) and player.playing:
			result.append(player)
	return result


func _silence(audio) -> void:
	if audio.voice_player != null:
		audio.voice_player.stop()
	for player in audio.sfx_players:
		if player != null and is_instance_valid(player):
			player.stop()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish(audio, battalion) -> void:
	_report["passed"] = passed
	_report["failed"] = failed
	_report["hiddenPlayers"] = hidden_players
	var report_path := OS.get_environment("OPENBFME_AUDIO_FOG_REPORT")
	if report_path != "":
		DirAccess.make_dir_recursive_absolute(report_path.get_base_dir())
		var file := FileAccess.open(report_path, FileAccess.WRITE)
		if file != null:
			file.store_string(JSON.stringify(_report, "  ") + "\n")
			file.close()
	if audio != null:
		audio.dispose()
		audio.queue_free()
	if battalion != null:
		battalion.queue_free()
	print("AUDIO_FOG_PRODUCTION_RUNNER_RESULT passed=%d failed=%d hidden_players=%d" % [passed, failed, hidden_players])
	quit(0 if failed == 0 and hidden_players == 0 else 1)
