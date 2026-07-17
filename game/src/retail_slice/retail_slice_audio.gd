class_name RetailSliceAudio
extends Node

const MAX_OBSERVABILITY_LOG_ENTRIES := 2048
const OBSERVABILITY_LOG_TRIM_COUNT := 512
## Routes deterministic simulation intents through contained retail audio
## manifests. Logical SAGE event IDs, not filename guesses, select the leaves.

const UserSettingsScript = preload("res://src/ui/user_settings.gd")

const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const TOWER_GUARD_OBJECT_ID := "bfme2.object.gondor-tower-guard"
const KNIGHT_OBJECT_ID := "bfme2.object.gondor-knight"
const ROSTER_OBJECT_IDS: Array[String] = [
	SOLDIER_OBJECT_ID,
	ARCHER_OBJECT_ID,
	TOWER_GUARD_OBJECT_ID,
	KNIGHT_OBJECT_ID,
]
const REQUIRED_VOICE_KINDS: Array[String] = ["select", "move", "attack", "death"]
const ROSTER_VOICE_EVENT_IDS: Dictionary = {
	SOLDIER_OBJECT_ID: {
		"select": ["GondorSoldierVoiceSelectMS", "gondorFighter.select"],
		"move": ["GondorSoldierVoiceMove"],
		"attack": ["GondorSoldierVoiceAttack", "gondorFighter.attack"],
		"death": ["HumanVoiceDie"],
	},
	ARCHER_OBJECT_ID: {
		"select": ["GondorArcherVoiceSelectMS"],
		"move": ["GondorArcherVoiceMove"],
		"attack": ["GondorArcherVoiceAttack"],
		"death": ["HumanVoiceDie"],
	},
	TOWER_GUARD_OBJECT_ID: {
		"select": ["TowerGuardVoiceSelectMS"],
		"move": ["TowerGuardVoiceMove"],
		"attack": ["TowerGuardVoiceAttack"],
		"death": ["HumanVoiceDie"],
	},
	KNIGHT_OBJECT_ID: {
		"select": ["GondorKnightVoiceSelectMS"],
		"move": ["GondorKnightVoiceMove"],
		"attack": ["GondorKnightVoiceAttack"],
		"death": ["HumanVoiceDie"],
	},
}
const REQUIRED_SFX_EVENT_IDS: Array[String] = [
	"ArrowDrawBow",
	"SwordShingClean1ForHordes",
	"BodyFallSoldier",
	"ImpactHorse",
	"BuildingLightDamageStone",
	"BuildingHeavyDamageStone",
	"BuildingSink",
]
const LEGACY_ATTACK_EVENT_IDS: Array[String] = [
	"gondorFighter.attack",
	"gondorFighter.attackBuilding",
	"gondorFighter.attackCharge",
]
const INITIAL_ENTITY_OBJECT_IDS: Dictionary = {
	1: SOLDIER_OBJECT_ID,
	2: ARCHER_OBJECT_ID,
	101: SOLDIER_OBJECT_ID,
	102: TOWER_GUARD_OBJECT_ID,
	103: KNIGHT_OBJECT_ID,
}
const FORDS_AMBIENT_TYPE_EVENT_IDS: Dictionary = {
	"Amb_BirdsAmonHen1": "Amb_BirdsAmonHen1",
	"Amb_BirdsAmonHen2": "Amb_BirdsAmonHen2",
	"Amb_BirdsIthilien1Loop": "Amb_MTBirds1Loop",
	"Amb_BirdsIthilien2Loop": "Amb_MTBirds2Loop",
	"Amb_CritterDesert1": "Amb_CritterDesert1",
	"Amb_WaterRiver1Loop": "Amb_WaterRiver1Loop",
	"AmbStream_AmonHenForest1": "AmbientAmonHenForest1Stream",
}
const FORDS_AMBIENT_PLACEMENT_COUNT := 50

const MUSIC_STATES: Array[String] = ["explore", "battle", "victory", "defeat"]
const MUSIC_CROSSFADE_SECONDS := 1.5

var pack_root := ""
var playback_enabled := true
# Intent and routing histories exist for focused diagnostics only. Production
# keeps the latest route status but does not retain per-event dictionary copies.
var observability_enabled := false
# music_player always references the currently active (audible / most recently
# selected) player; _music_player_alt holds the other side of the crossfade pair.
var music_player: AudioStreamPlayer
var _music_player_alt: AudioStreamPlayer
var voice_player: AudioStreamPlayer
var sfx_player: AudioStreamPlayer
# music_streams keeps the legacy state -> primary AudioStream view (the exact
# <state>.mp3 leaf) so existing closure gates stay intact.
var music_streams: Dictionary = {}
# music_playlists maps state -> Array[AudioStream]; music_playlist_paths mirrors
# it with the resolved on-disk paths for deterministic runner assertions.
var music_playlists: Dictionary = {}
var music_playlist_paths: Dictionary = {}
# Deterministic playlist bookkeeping. current_music_track_index tracks the leaf
# playing for current_music_state; music_transition_log records every advance so
# headless runners can audit playlist behaviour without audio output.
var current_music_track_index := -1
var music_transition_log: Array[Dictionary] = []
var _music_active_index := -1
var _music_last_index := -1
var _music_fade_tween: Tween = null
var _music_rng := RandomNumberGenerator.new()
# Compatibility views used by the existing slice gate. Strict roster readiness
# is exposed separately and never substitutes Soldier samples for another unit.
var voice_streams: Dictionary = {"select": [], "attack": []}
var audio_event_routes: Dictionary = {}
var declared_structure_lifecycle_audio_active := false
var roster_voice_routes: Dictionary = {}
var route_failures: Dictionary = {}
var missing_required_events: Array[String] = []
var intent_log: Array[Dictionary] = []
var routing_log: Array[Dictionary] = []
var ambient_players: Array[AudioStreamPlayer3D] = []
var ambient_emitters: Array[Dictionary] = []
var ambient_diagnostics: Array[String] = []
var ambient_contract_declared := false
var ambient_parity_ready := false
var current_music_state := ""
var last_route_result: Dictionary = {}
var _stream_cache: Dictionary = {}
var _entity_object_ids: Dictionary = {}
var _next_event_index := 0
var _next_ui_sequence := 1000000000
var music_volume := UserSettingsScript.DEFAULT_MUSIC_VOLUME
var voice_sfx_volume := UserSettingsScript.DEFAULT_VOICE_SFX_VOLUME
var muted := UserSettingsScript.DEFAULT_MUTED


func configure(selected_pack_root: String, enable_playback: bool = true) -> bool:
	pack_root = selected_pack_root
	playback_enabled = enable_playback
	_ensure_players()
	_reset_music_playback()
	music_streams.clear()
	music_playlists.clear()
	music_playlist_paths.clear()
	music_transition_log.clear()
	voice_streams = {"select": [], "attack": []}
	audio_event_routes.clear()
	roster_voice_routes.clear()
	route_failures.clear()
	missing_required_events.clear()
	intent_log.clear()
	routing_log.clear()
	last_route_result.clear()
	_stream_cache.clear()
	declared_structure_lifecycle_audio_active = false
	_entity_object_ids = INITIAL_ENTITY_OBJECT_IDS.duplicate(true)
	_next_event_index = 0
	current_music_state = ""
	_load_user_settings()
	_load_music()
	_load_declared_audio_routes()
	_configure_fords_ambient_from_pack()
	_bind_roster_voice_routes()
	_refresh_compatibility_views()
	_collect_readiness_diagnostics()
	return has_complete_audio_closure()


func _ensure_players() -> void:
	if music_player == null or not is_instance_valid(music_player):
		music_player = AudioStreamPlayer.new()
		music_player.name = "RetailMusic"
		add_child(music_player)
		music_player.finished.connect(_on_music_finished.bind(music_player))
	if _music_player_alt == null or not is_instance_valid(_music_player_alt):
		_music_player_alt = AudioStreamPlayer.new()
		_music_player_alt.name = "RetailMusicAlt"
		add_child(_music_player_alt)
		_music_player_alt.finished.connect(_on_music_finished.bind(_music_player_alt))
	if voice_player == null or not is_instance_valid(voice_player):
		voice_player = AudioStreamPlayer.new()
		voice_player.name = "RetailVoice"
		add_child(voice_player)
	if sfx_player == null or not is_instance_valid(sfx_player):
		sfx_player = AudioStreamPlayer.new()
		sfx_player.name = "RetailSfx"
		add_child(sfx_player)
	_apply_volume_levels()


func _load_user_settings() -> void:
	var settings: Dictionary = UserSettingsScript.load_audio()
	music_volume = float(settings["music_volume"])
	voice_sfx_volume = float(settings["voice_sfx_volume"])
	muted = bool(settings["muted"])
	_apply_volume_levels()


func set_music_volume(value: float, persist: bool = false) -> void:
	music_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_music_volume() -> float:
	return music_volume


func set_voice_sfx_volume(value: float, persist: bool = false) -> void:
	voice_sfx_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_voice_sfx_volume() -> float:
	return voice_sfx_volume


func set_voice_volume(value: float, persist: bool = false) -> void:
	set_voice_sfx_volume(value, persist)


func get_voice_volume() -> float:
	return get_voice_sfx_volume()


func set_muted(value: bool, persist: bool = false) -> void:
	muted = value
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func is_muted() -> bool:
	return muted


func _apply_volume_levels() -> void:
	# A volume/mute change is authoritative: finalize any in-flight crossfade so
	# the active player lands exactly on the configured level and the idle side
	# stays silent. This keeps mute/volume deterministic instead of racing a tween.
	_finalize_music_fade()
	if music_player != null and is_instance_valid(music_player):
		music_player.volume_db = _music_target_db()
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB
	if voice_player != null and is_instance_valid(voice_player):
		voice_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)
	if sfx_player != null and is_instance_valid(sfx_player):
		sfx_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)
	for ambient_player in ambient_players:
		if ambient_player != null and is_instance_valid(ambient_player):
			ambient_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)


func has_complete_audio_closure() -> bool:
	# Kept as the playable-slice compatibility gate until the private profile
	# contains all four units. It does not claim strict roster readiness.
	return _has_music_closure() and (voice_streams["select"] as Array).size() > 0 and (voice_streams["attack"] as Array).size() > 0


func has_complete_roster_audio_closure() -> bool:
	return _has_music_closure() and missing_required_events.is_empty()


func readiness_diagnostics() -> Array[String]:
	return missing_required_events.duplicate()


func has_complete_fords_ambient_closure() -> bool:
	return ambient_contract_declared and ambient_parity_ready and ambient_emitters.size() == FORDS_AMBIENT_PLACEMENT_COUNT


func fords_ambient_readiness_diagnostics() -> Array[String]:
	return ambient_diagnostics.duplicate()


func _has_music_closure() -> bool:
	return music_streams.has("explore") and music_streams.has("battle") and music_streams.has("victory") and music_streams.has("defeat")


func _load_music() -> void:
	# Build a per-state playlist from every music leaf the pack actually ships.
	# A file belongs to a state when its basename equals the state or is prefixed
	# with "<state>-"/"<state>_" (e.g. battle-alternate.mp3 joins the battle
	# playlist). Orphan tracks that match no state (e.g. building.mp3) are ignored
	# rather than invented into a playlist. music_streams keeps the exact
	# <state>.mp3 leaf as the primary so legacy closure gates stay identical.
	var music_dir := pack_root.path_join("assets/audio/music")
	var files_by_state := _scan_music_files(music_dir)
	for state in MUSIC_STATES:
		var ordered_names: Array = files_by_state.get(state, [])
		var streams: Array[AudioStream] = []
		var paths: Array[String] = []
		for name in ordered_names:
			var path := music_dir.path_join(String(name))
			var stream := _load_stream(path)
			if stream != null:
				streams.append(stream)
				paths.append(path)
		if streams.is_empty():
			continue
		music_playlists[state] = streams
		music_playlist_paths[state] = paths
		music_streams[state] = streams[0]


func _scan_music_files(music_dir: String) -> Dictionary:
	var result: Dictionary = {}
	for state in MUSIC_STATES:
		result[state] = []
	var directory := DirAccess.open(music_dir)
	if directory == null:
		return result
	var names: Array[String] = []
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while file_name != "":
		if not directory.current_is_dir() and file_name.get_extension().to_lower() == "mp3":
			names.append(file_name)
		file_name = directory.get_next()
	directory.list_dir_end()
	names.sort_custom(func(a: String, b: String) -> bool: return a.to_lower() < b.to_lower())
	for name in names:
		var state := _music_state_for_file(name)
		if state != "":
			(result[state] as Array).append(name)
	# Ensure the exact <state>.mp3 leaf leads its playlist so the primary view is
	# stable regardless of alphabetical ordering.
	for state in MUSIC_STATES:
		var ordered: Array = result[state]
		var exact := "%s.mp3" % state
		if ordered.has(exact) and String(ordered[0]).to_lower() != exact:
			ordered.erase(exact)
			ordered.insert(0, exact)
	return result


func _music_state_for_file(file_name: String) -> String:
	var basename := file_name.get_basename().to_lower()
	for state in MUSIC_STATES:
		if basename == state or basename.begins_with("%s-" % state) or basename.begins_with("%s_" % state):
			return state
	return ""


func _load_declared_audio_routes() -> void:
	var requested: Dictionary = {}
	for object_id in ROSTER_OBJECT_IDS:
		var by_kind: Dictionary = ROSTER_VOICE_EVENT_IDS[object_id]
		for kind in REQUIRED_VOICE_KINDS:
			for event_id in Array(by_kind[kind]):
				requested[String(event_id).to_lower()] = String(event_id)
	for event_id in REQUIRED_SFX_EVENT_IDS:
		requested[event_id.to_lower()] = event_id
	for event_id in FORDS_AMBIENT_TYPE_EVENT_IDS.values():
		requested[String(event_id).to_lower()] = String(event_id)
	for event_id in requested.values():
		var built := _build_content_db_route(String(event_id))
		if bool(built.get("ok", false)):
			audio_event_routes[String(event_id).to_lower()] = built["route"]
		else:
			route_failures[String(event_id).to_lower()] = String(built.get("reason", "invalid_event"))
	_load_v0_pack_routes()


func _build_content_db_route(event_id: String) -> Dictionary:
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return {"ok": false, "reason": "content_db_unavailable"}
	var event_definition: Dictionary = content_db.call("get_retail_audio_event", event_id)
	var collected := _collect_content_db_leaves(content_db, event_id, {})
	if not bool(collected.get("ok", false)):
		return collected
	var leaves: Array = collected.get("leaves", [])
	if leaves.is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	return {
		"ok": true,
		"route": {
			"event_id": event_id,
			"source": "content-db-v1",
			"definition": event_definition.duplicate(true),
			"leaves": leaves,
		},
	}


func _configure_fords_ambient_from_pack() -> void:
	_clear_ambient_players()
	ambient_emitters.clear()
	ambient_diagnostics.clear()
	ambient_contract_declared = false
	ambient_parity_ready = false
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null or pack_root == "":
		_add_ambient_diagnostic("map-contract:mod-loader-unavailable")
		return
	var map_path := String(mod_loader.call("resolve_pack_path", pack_root, "maps/fords-of-isen-ii/map.json"))
	var map_value: Variant = _read_json_without_diagnostics(map_path)
	if typeof(map_value) != TYPE_DICTIONARY:
		_add_ambient_diagnostic("map-contract:missing-fords-map")
		return
	var map_document := map_value as Dictionary
	if String(map_document.get("schema", "")) != "openbfme.map" or String(map_document.get("id", "")) != "bfme2.map.fords-of-isen-ii":
		_add_ambient_diagnostic("map-contract:invalid-fords-map")
		return
	var objects_relative := String(map_document.get("objects", ""))
	if objects_relative == "" or not bool(mod_loader.call("is_safe_relative_path", objects_relative)):
		_add_ambient_diagnostic("map-contract:invalid-objects-path")
		return
	var objects_path := String(mod_loader.call("resolve_pack_path", pack_root, "maps/fords-of-isen-ii".path_join(objects_relative)))
	var objects_value: Variant = _read_json_without_diagnostics(objects_path)
	if typeof(objects_value) != TYPE_DICTIONARY or typeof((objects_value as Dictionary).get("objects", null)) != TYPE_ARRAY:
		_add_ambient_diagnostic("map-contract:invalid-objects-document")
		return
	var placements: Array = []
	for placement_value in Array((objects_value as Dictionary).get("objects", [])):
		if typeof(placement_value) != TYPE_DICTIONARY:
			continue
		var placement := placement_value as Dictionary
		if FORDS_AMBIENT_TYPE_EVENT_IDS.has(String(placement.get("typeName", ""))):
			placements.append(placement)
	placements.sort_custom(func(a: Variant, b: Variant) -> bool: return int((a as Dictionary).get("index", -1)) < int((b as Dictionary).get("index", -1)))
	ambient_contract_declared = placements.size() == FORDS_AMBIENT_PLACEMENT_COUNT
	if not ambient_contract_declared:
		_add_ambient_diagnostic("map-contract:placement-count:%d" % placements.size())
	for placement_value in placements:
		_instantiate_fords_ambient_emitter(placement_value as Dictionary)
	ambient_parity_ready = ambient_contract_declared and ambient_emitters.size() == placements.size() and ambient_diagnostics.is_empty()


func _instantiate_fords_ambient_emitter(placement: Dictionary) -> void:
	var type_name := String(placement.get("typeName", ""))
	var event_id := String(FORDS_AMBIENT_TYPE_EVENT_IDS.get(type_name, ""))
	var placement_index := int(placement.get("index", -1))
	var position_value: Variant = placement.get("godotPosition", null)
	if event_id == "" or placement_index < 0 or typeof(position_value) != TYPE_ARRAY or (position_value as Array).size() != 3:
		_add_ambient_diagnostic("map-contract:invalid-placement:%d" % placement_index)
		return
	var route: Dictionary = audio_event_routes.get(event_id.to_lower(), {})
	if route.is_empty() or String(route.get("source", "")) != "content-db-v1":
		_add_ambient_diagnostic("missing-event:%s:%s" % [type_name, event_id])
		return
	var selected := _select_ambient_leaf(route, placement_index + 1)
	if selected.is_empty():
		_add_ambient_diagnostic("invalid-route:%s" % event_id)
		return
	var stream: Variant = _stream_for_leaf(selected)
	var path := String(selected.get("path", ""))
	if not (stream is AudioStream) or not _is_resolved_audio_path(path):
		_add_ambient_diagnostic("invalid-stream:%s" % event_id)
		return
	var definition: Dictionary = route.get("definition", {})
	var parameters := _ambient_parameters(definition)
	for gap in _ambient_parameter_gaps(event_id, definition, parameters):
		_add_ambient_diagnostic(gap)
	var player := AudioStreamPlayer3D.new()
	player.name = "RetailAmbient_%04d_%s" % [placement_index, type_name]
	player.stream = stream as AudioStream
	var source_position := position_value as Array
	player.position = Vector3(float(source_position[0]), float(source_position[1]), float(source_position[2]))
	var min_range := _strict_positive_float(String(parameters.get("minrange", "")))
	var max_range := _strict_positive_float(String(parameters.get("maxrange", "")))
	if min_range > 0.0 and max_range >= min_range:
		player.unit_size = min_range
		player.max_distance = max_range
	player.set_meta("retail_audio_event_id", event_id)
	player.set_meta("retail_audio_type_name", type_name)
	player.set_meta("retail_audio_placement_index", placement_index)
	player.set_meta("retail_audio_path", path)
	player.set_meta("retail_audio_parameters", parameters.duplicate(true))
	player.set_meta("retail_audio_loop_control", String(parameters.get("control", "")).to_lower() == "loop")
	add_child(player)
	ambient_players.append(player)
	ambient_emitters.append({
		"event_id": event_id,
		"type_name": type_name,
		"placement_index": placement_index,
		"path": path,
		"position": player.position,
		"min_range": min_range,
		"max_range": max_range,
		"loop": String(parameters.get("control", "")).to_lower() == "loop",
		"player": player,
	})
	_apply_volume_levels()
	if playback_enabled:
		player.play()


func _select_ambient_leaf(route: Dictionary, sequence: int) -> Dictionary:
	var leaves: Array = route.get("leaves", [])
	var total_weight := 0
	for leaf_value in leaves:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return {}
		total_weight += maxi(0, int((leaf_value as Dictionary).get("weight", 0)))
	if total_weight <= 0:
		return {}
	var slot := posmod(sequence - 1, total_weight)
	for leaf_value in leaves:
		var leaf := leaf_value as Dictionary
		var weight := maxi(0, int(leaf.get("weight", 0)))
		if slot < weight:
			return leaf
		slot -= weight
	return {}


func _ambient_parameters(definition: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	var values: Variant = definition.get("parameters", null)
	if typeof(values) != TYPE_ARRAY:
		return result
	for row_value in values as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var field := String(row.get("field", "")).strip_edges().to_lower()
		if field != "":
			result[field] = String(row.get("value", "")).strip_edges()
	return result


func _ambient_parameter_gaps(event_id: String, definition: Dictionary, parameters: Dictionary = {}) -> Array[String]:
	var parsed := parameters if not parameters.is_empty() else _ambient_parameters(definition)
	var gaps: Array[String] = []
	var min_range := _strict_positive_float(String(parsed.get("minrange", "")))
	var max_range := _strict_positive_float(String(parsed.get("maxrange", "")))
	if min_range <= 0.0 or max_range < min_range:
		gaps.append("unsupported-range:%s:%s:%s" % [event_id, String(parsed.get("minrange", "missing")), String(parsed.get("maxrange", "missing"))])
	else:
		gaps.append("unsupported-attenuation-curve:%s" % event_id)
	if String(parsed.get("control", "")).to_lower() == "loop":
		gaps.append("unsupported-loop-scheduler:%s" % event_id)
	else:
		gaps.append("unsupported-control:%s:%s" % [event_id, String(parsed.get("control", "missing"))])
	for field in ["priority", "limit", "pitchshift", "delay", "volumeshift", "volume", "submixslider", "attack", "decay"]:
		if parsed.has(field):
			gaps.append("unsupported-%s:%s:%s" % [field, event_id, String(parsed[field])])
	if not String(parsed.get("type", "")).to_lower().split(" ").has("world"):
		gaps.append("unsupported-type:%s:%s" % [event_id, String(parsed.get("type", "missing"))])
	return gaps


func _strict_positive_float(value: String) -> float:
	if not value.is_valid_float():
		return -1.0
	var parsed := value.to_float()
	return parsed if parsed > 0.0 else -1.0


func _add_ambient_diagnostic(value: String) -> void:
	if value != "" and not ambient_diagnostics.has(value):
		ambient_diagnostics.append(value)
		ambient_diagnostics.sort()


func _clear_ambient_players() -> void:
	for player in ambient_players:
		if player != null and is_instance_valid(player):
			player.stop()
			player.free()
	ambient_players.clear()


func _collect_content_db_leaves(content_db: Node, event_id: String, stack: Dictionary) -> Dictionary:
	var key := event_id.to_lower()
	if stack.has(key):
		return {"ok": false, "reason": "cyclic_event_definition"}
	stack[key] = true
	var event: Dictionary = content_db.call("get_retail_audio_event", event_id)
	if not event.is_empty():
		var result := _collect_sample_references(content_db, event.get("sounds", []))
		stack.erase(key)
		return result
	var multisound: Dictionary = content_db.call("get_retail_audio_multisound", event_id)
	if multisound.is_empty():
		stack.erase(key)
		return {"ok": false, "reason": "missing_event"}
	var references: Variant = multisound.get("subsounds", [])
	if typeof(references) != TYPE_ARRAY or (references as Array).is_empty():
		stack.erase(key)
		return {"ok": false, "reason": "multisound_has_no_subsounds"}
	var leaves: Array[Dictionary] = []
	for reference_value in references:
		if typeof(reference_value) != TYPE_DICTIONARY:
			stack.erase(key)
			return {"ok": false, "reason": "invalid_multisound_reference"}
		var reference := reference_value as Dictionary
		var child_id := String(reference.get("id", ""))
		var weight := _reference_weight(reference)
		if child_id == "" or weight <= 0:
			stack.erase(key)
			return {"ok": false, "reason": "invalid_multisound_reference"}
		var child := _collect_content_db_leaves(content_db, child_id, stack)
		if not bool(child.get("ok", false)):
			stack.erase(key)
			return child
		for leaf_value in Array(child.get("leaves", [])):
			var leaf := (leaf_value as Dictionary).duplicate()
			leaf["weight"] = mini(1_000_000, int(leaf.get("weight", 1)) * weight)
			leaves.append(leaf)
	stack.erase(key)
	return {"ok": true, "leaves": leaves}


func _collect_sample_references(content_db: Node, references: Variant) -> Dictionary:
	if typeof(references) != TYPE_ARRAY or (references as Array).is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	var samples_value: Variant = content_db.get("retail_audio_samples")
	if typeof(samples_value) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "sample_index_unavailable"}
	var sample_index := samples_value as Dictionary
	var leaves: Array[Dictionary] = []
	for reference_value in references:
		if typeof(reference_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "invalid_sample_reference"}
		var reference := reference_value as Dictionary
		var sample_id := String(reference.get("id", ""))
		var weight := _reference_weight(reference)
		if sample_id == "" or weight <= 0:
			return {"ok": false, "reason": "invalid_sample_reference"}
		var sample: Dictionary = sample_index.get(sample_id.to_lower(), {})
		if sample.is_empty():
			return {"ok": false, "reason": "missing_sample:%s" % sample_id}
		var path := String(content_db.call("resolve_retail_audio_sample_path", sample_id))
		# ContentDB can expose hundreds of leaves through the small set of
		# required logical events.  Prove containment and the file header now,
		# then decode only the leaf selected for playback.  This keeps startup
		# bounded without weakening the retail closure check.
		if not _is_resolved_audio_path(path) or not _has_supported_audio_header(path):
			return {"ok": false, "reason": "invalid_sample:%s" % sample_id}
		leaves.append({
			"sample_id": sample_id,
			"path": path,
			"stream": null,
			"validated_path": true,
			"weight": weight,
		})
	return {"ok": true, "leaves": leaves}


func _reference_weight(reference: Dictionary) -> int:
	var value: Variant = reference.get("weight", 1)
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return 0
	return clampi(int(value), 0, 1_000_000)


func _load_v0_pack_routes() -> void:
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null or pack_root == "":
		return
	var pack_path := String(mod_loader.call("resolve_pack_path", pack_root, "pack.json"))
	var pack: Variant = _read_json_without_diagnostics(pack_path)
	if typeof(pack) != TYPE_DICTIONARY:
		return
	var files: Variant = (pack as Dictionary).get("files", {})
	if typeof(files) != TYPE_DICTIONARY:
		return
	var relative := String((files as Dictionary).get("audioEvents", ""))
	var manifest_path := String(mod_loader.call("resolve_pack_path", pack_root, relative))
	var document: Variant = _read_json_without_diagnostics(manifest_path)
	if typeof(document) != TYPE_DICTIONARY:
		return
	var manifest := document as Dictionary
	if String(manifest.get("schema", "")) != "openbfme.audio-events" or int(manifest.get("schemaVersion", -1)) != 0:
		return
	var events: Variant = manifest.get("events", {})
	if typeof(events) != TYPE_DICTIONARY:
		return
	for event_key in (events as Dictionary).keys():
		var event_id := String(event_key)
		var row_value: Variant = (events as Dictionary)[event_key]
		if event_id == "" or typeof(row_value) != TYPE_DICTIONARY:
			continue
		var built := _build_v0_route(mod_loader, event_id, row_value as Dictionary)
		var key := event_id.to_lower()
		if bool(built.get("ok", false)):
			audio_event_routes[key] = built["route"]
			route_failures.erase(key)
		else:
			route_failures[key] = String(built.get("reason", "invalid_event"))


func _build_v0_route(mod_loader: Node, event_id: String, row: Dictionary) -> Dictionary:
	var relative_directory := String(row.get("directory", ""))
	var pattern := String(row.get("pattern", ""))
	if relative_directory == "" or not bool(mod_loader.call("is_safe_relative_path", relative_directory)):
		return {"ok": false, "reason": "unsafe_event_directory"}
	if pattern == "" or pattern.contains("/") or pattern.contains("\\") or not pattern.to_lower().ends_with(".wav"):
		return {"ok": false, "reason": "invalid_event_pattern"}
	var directory_path := String(mod_loader.call("resolve_pack_path", pack_root, relative_directory))
	var directory := DirAccess.open(directory_path)
	if directory == null:
		return {"ok": false, "reason": "missing_event_directory"}
	var names: Dictionary = {}
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while file_name != "":
		if not directory.current_is_dir() and file_name.matchn(pattern):
			names[file_name.to_lower()] = file_name
		file_name = directory.get_next()
	directory.list_dir_end()
	var additional: Variant = row.get("additional", [])
	if typeof(additional) != TYPE_ARRAY:
		return {"ok": false, "reason": "invalid_additional_samples"}
	for additional_value in additional:
		var additional_name := String(additional_value)
		if additional_name == "" or additional_name.get_file() != additional_name or not additional_name.to_lower().ends_with(".wav"):
			return {"ok": false, "reason": "invalid_additional_sample"}
		names[additional_name.to_lower()] = additional_name
	var ordered_names: Array = names.values()
	ordered_names.sort_custom(func(a: Variant, b: Variant) -> bool: return String(a).to_lower() < String(b).to_lower())
	if ordered_names.is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	var leaves: Array[Dictionary] = []
	for name_value in ordered_names:
		var name := String(name_value)
		var path := String(mod_loader.call("resolve_pack_path", pack_root, relative_directory.path_join(name)))
		var stream := _load_stream(path)
		if stream == null:
			return {"ok": false, "reason": "invalid_sample:%s" % name}
		leaves.append({
			"sample_id": name.get_basename(),
			"path": path,
			"stream": stream,
			"weight": 1,
		})
	return {
		"ok": true,
		"route": {
			"event_id": event_id,
			"source": "declared-pack-v0",
			"leaves": leaves,
		},
	}


func _read_json_without_diagnostics(path: String) -> Variant:
	if path == "" or not FileAccess.file_exists(path):
		return null
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var parser := JSON.new()
	if parser.parse(file.get_as_text()) != OK:
		return null
	return parser.data


func _bind_roster_voice_routes() -> void:
	for object_id in ROSTER_OBJECT_IDS:
		var by_kind: Dictionary = ROSTER_VOICE_EVENT_IDS[object_id]
		var bound: Dictionary = {}
		for kind in REQUIRED_VOICE_KINDS:
			for event_id_value in Array(by_kind[kind]):
				var event_id := String(event_id_value)
				var route: Dictionary = audio_event_routes.get(event_id.to_lower(), {})
				if not route.is_empty():
					bound[kind] = route
					break
		roster_voice_routes[object_id] = bound


func _refresh_compatibility_views() -> void:
	voice_streams = {"select": [], "attack": []}
	var soldier: Dictionary = roster_voice_routes.get(SOLDIER_OBJECT_ID, {})
	_append_unique_streams(voice_streams["select"] as Array, soldier.get("select", {}))
	for event_id in LEGACY_ATTACK_EVENT_IDS:
		_append_unique_streams(voice_streams["attack"] as Array, audio_event_routes.get(event_id.to_lower(), {}))
	if (voice_streams["attack"] as Array).is_empty():
		_append_unique_streams(voice_streams["attack"] as Array, soldier.get("attack", {}))


func _append_unique_streams(target: Array, route_value: Variant) -> void:
	if typeof(route_value) != TYPE_DICTIONARY:
		return
	var seen_paths: Dictionary = {}
	for existing in target:
		if existing is AudioStream and existing.has_meta("retail_audio_path"):
			seen_paths[String(existing.get_meta("retail_audio_path"))] = true
	for leaf_value in Array((route_value as Dictionary).get("leaves", [])):
		if typeof(leaf_value) != TYPE_DICTIONARY:
			continue
		var leaf := leaf_value as Dictionary
		var stream: Variant = _stream_for_leaf(leaf)
		var path := String(leaf.get("path", ""))
		if stream is AudioStream and not seen_paths.has(path):
			(stream as AudioStream).set_meta("retail_audio_path", path)
			target.append(stream)
			seen_paths[path] = true


func _collect_readiness_diagnostics() -> void:
	missing_required_events.clear()
	for state in ["explore", "battle", "victory", "defeat"]:
		if not music_streams.has(state):
			missing_required_events.append("missing_music_state:%s" % state)
	for object_id in ROSTER_OBJECT_IDS:
		var bound: Dictionary = roster_voice_routes.get(object_id, {})
		var expected: Dictionary = ROSTER_VOICE_EVENT_IDS[object_id]
		for kind in REQUIRED_VOICE_KINDS:
			if not bound.has(kind):
				missing_required_events.append("missing_voice_event:%s:%s:%s" % [object_id, kind, String(Array(expected[kind])[0])])
	for event_id in REQUIRED_SFX_EVENT_IDS:
		if not audio_event_routes.has(event_id.to_lower()):
			missing_required_events.append("missing_sfx_event:%s" % event_id)
	missing_required_events.sort()


func sync_events(events: Array[Dictionary]) -> void:
	while _next_event_index < events.size():
		var event: Dictionary = events[_next_event_index]
		_consume_event(event)
		_next_event_index += 1


func acknowledge_event_history_compaction(retained_count: int) -> void:
	_next_event_index = retained_count


func set_declared_structure_lifecycle_audio_active(active: bool) -> void:
	declared_structure_lifecycle_audio_active = active


func _consume_event(event: Dictionary) -> void:
	var kind := String(event.get("kind", ""))
	var sequence := int(event.get("sequence", 0))
	var entity_id := int(event.get("entity_id", 0))
	var target_id := int(event.get("target_id", 0))
	if kind == "production.complete":
		var produced_object_id := String(event.get("object_id", ""))
		if produced_object_id != "" and ROSTER_OBJECT_IDS.has(produced_object_id):
			_entity_object_ids[target_id] = produced_object_id
	elif kind.begins_with("music."):
		_set_music(kind.trim_prefix("music."))
	elif kind == "voice.select":
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "select", sequence), voice_player)
	elif kind == "order.move" or kind == "voice.move":
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "move", sequence), voice_player)
	elif kind == "voice.attack":
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "attack", sequence), voice_player)
	elif kind == "battalion.defeated":
		var defeated_object_id := _object_id_for_event(event, target_id)
		_play_routed(route_roster_voice(defeated_object_id, "death", sequence), voice_player)
		_play_routed(route_audio_event("ImpactHorse" if defeated_object_id == KNIGHT_OBJECT_ID else "BodyFallSoldier", sequence), sfx_player)
		if not INITIAL_ENTITY_OBJECT_IDS.has(target_id):
			_entity_object_ids.erase(target_id)
	elif kind == "combat.swing":
		var attacker_object_id := _object_id_for_event(event, entity_id)
		_play_routed(route_audio_event("ArrowDrawBow" if attacker_object_id == ARCHER_OBJECT_ID else "SwordShingClean1ForHordes", sequence), sfx_player)
	elif kind == "combat.hit_structure":
		if not declared_structure_lifecycle_audio_active:
			_play_routed(route_audio_event("BuildingLightDamageStone", sequence), sfx_player)
	elif kind == "structure.destroyed":
		if not declared_structure_lifecycle_audio_active:
			_play_routed(route_audio_event("BuildingHeavyDamageStone", sequence), sfx_player)
			_play_routed(route_audio_event("BuildingSink", sequence), sfx_player)
	_append_bounded_observability(intent_log, event.duplicate(true))


func _object_id_for_event(event: Dictionary, entity_id: int) -> String:
	var declared := String(event.get("object_id", ""))
	if ROSTER_OBJECT_IDS.has(declared):
		return declared
	return String(_entity_object_ids.get(entity_id, ""))


func route_roster_voice(object_id: String, kind: String, sequence: int) -> Dictionary:
	if not ROSTER_OBJECT_IDS.has(object_id):
		return _rejection("unknown_roster_object", "", object_id, kind, sequence)
	if not REQUIRED_VOICE_KINDS.has(kind):
		return _rejection("unknown_voice_kind", "", object_id, kind, sequence)
	var by_kind: Dictionary = roster_voice_routes.get(object_id, {})
	if not by_kind.has(kind):
		var expected: Dictionary = ROSTER_VOICE_EVENT_IDS[object_id]
		return _rejection("missing_event", String(Array(expected[kind])[0]), object_id, kind, sequence)
	return _route_definition(by_kind[kind], sequence, object_id, kind)


func route_audio_event(event_id: String, sequence: int) -> Dictionary:
	var route: Dictionary = audio_event_routes.get(event_id.to_lower(), {})
	if route.is_empty():
		return _rejection(String(route_failures.get(event_id.to_lower(), "missing_event")), event_id, "", "sfx", sequence)
	return _route_definition(route, sequence, "", "sfx")


func play_ui_event(event_id: String) -> Dictionary:
	var sequence := _next_ui_sequence
	_next_ui_sequence += 1
	var result := route_audio_event(event_id, sequence)
	if bool(result.get("ok", false)):
		_play_routed(result, sfx_player)
	_append_bounded_observability(intent_log, {
		"kind": "ui.sound",
		"sequence": sequence,
		"event_id": event_id,
		"accepted": bool(result.get("ok", false)),
		"reason": String(result.get("reason", "")),
	})
	return _observable_route_result(result) if bool(result.get("ok", false)) else result.duplicate(true)


func play_declared_structure_event(event_id: String, sequence: int, structure_id: int, phase: String) -> Dictionary:
	## Lifecycle contracts provide the event ID. This seam deliberately refuses
	## empty/unknown IDs and never substitutes the generic building sounds used
	## by legacy simulation events.
	if event_id == "" or structure_id <= 0 or phase == "":
		return _rejection("invalid_structure_lifecycle_event", event_id, "", phase, sequence)
	var result := route_audio_event(event_id, sequence)
	result["structure_id"] = structure_id
	result["phase"] = phase
	if bool(result.get("ok", false)):
		_play_routed(result, sfx_player)
	_append_bounded_observability(intent_log, {
		"kind": "structure.lifecycle.audio",
		"sequence": sequence,
		"entity_id": structure_id,
		"phase": phase,
		"event_id": event_id,
		"accepted": bool(result.get("ok", false)),
		"reason": String(result.get("reason", "")),
	})
	return _observable_route_result(result) if bool(result.get("ok", false)) else result.duplicate(true)


func _route_definition(route: Dictionary, sequence: int, object_id: String, kind: String) -> Dictionary:
	var event_id := String(route.get("event_id", ""))
	var leaves_value: Variant = route.get("leaves", [])
	if event_id == "" or typeof(leaves_value) != TYPE_ARRAY or (leaves_value as Array).is_empty():
		return _rejection("invalid_event", event_id, object_id, kind, sequence)
	var leaves := leaves_value as Array
	var total_weight := 0
	for leaf_value in leaves:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return _rejection("corrupt_event", event_id, object_id, kind, sequence)
		var leaf := leaf_value as Dictionary
		var stream: Variant = leaf.get("stream")
		var path := String(leaf.get("path", ""))
		var weight := int(leaf.get("weight", 0))
		var lazy_validated := bool(leaf.get("validated_path", false))
		if (not (stream is AudioStream) and not lazy_validated) or path == "" or weight <= 0 or not _is_resolved_audio_path(path):
			return _rejection("corrupt_event", event_id, object_id, kind, sequence)
		total_weight += weight
	if total_weight <= 0:
		return _rejection("corrupt_event", event_id, object_id, kind, sequence)
	var slot := posmod(sequence - 1, total_weight)
	var selected_index := 0
	var selected: Dictionary = leaves[0]
	for index in leaves.size():
		var leaf := leaves[index] as Dictionary
		var weight := int(leaf.get("weight", 1))
		if slot < weight:
			selected = leaf
			selected_index = index
			break
		slot -= weight
	var selected_stream := _stream_for_leaf(selected)
	if selected_stream == null:
		return _rejection("corrupt_event", event_id, object_id, kind, sequence)
	var result := {
		"ok": true,
		"event_id": event_id,
		"object_id": object_id,
		"kind": kind,
		"sequence": sequence,
		"variation_index": selected_index,
		"sample_id": String(selected.get("sample_id", "")),
		"path": String(selected.get("path", "")),
		"stream": selected_stream,
		"source": String(route.get("source", "")),
	}
	last_route_result = result.duplicate()
	_append_bounded_observability(routing_log, _observable_route_result(result))
	return result


func _stream_for_leaf(leaf: Dictionary) -> AudioStream:
	var existing: Variant = leaf.get("stream")
	if existing is AudioStream:
		return existing as AudioStream
	if not bool(leaf.get("validated_path", false)):
		return null
	var stream := _load_stream(String(leaf.get("path", "")))
	if stream != null:
		leaf["stream"] = stream
	return stream


func _rejection(reason: String, event_id: String, object_id: String, kind: String, sequence: int) -> Dictionary:
	var result := {
		"ok": false,
		"reason": reason,
		"event_id": event_id,
		"object_id": object_id,
		"kind": kind,
		"sequence": sequence,
	}
	last_route_result = result.duplicate()
	_append_bounded_observability(routing_log, result.duplicate())
	return result


func _observable_route_result(result: Dictionary) -> Dictionary:
	var copy := result.duplicate()
	copy.erase("stream")
	return copy


func _play_routed(result: Dictionary, player: AudioStreamPlayer) -> void:
	if not bool(result.get("ok", false)) or not playback_enabled or player == null:
		return
	player.stream = result.get("stream") as AudioStream
	player.play()


func _set_music(state: String) -> void:
	# Sim entry point. A repeated state event is a no-op so an unrelated event
	# burst never restarts the current playlist; a genuine change crossfades to
	# the first leaf of the target state's playlist.
	if state == current_music_state and music_playlists.has(state) and _music_active_index >= 0:
		current_music_state = state
		return
	current_music_state = state
	if not music_playlists.has(state):
		# No playlist for this state (never happens for the shipped four states);
		# preserve deterministic bookkeeping without inventing silence sources.
		_music_last_index = _music_active_index
		_music_active_index = -1
		current_music_track_index = -1
		return
	_transition_music(state, 0, "state-change")


func _transition_music(state: String, target_index: int, reason: String) -> void:
	var playlist: Array = music_playlists.get(state, [])
	if playlist.is_empty():
		return
	var index := clampi(target_index, 0, playlist.size() - 1)
	current_music_state = state
	_music_last_index = _music_active_index
	_music_active_index = index
	current_music_track_index = index
	var target_stream := playlist[index] as AudioStream
	# Swap the crossfade pair so music_player always references the incoming leaf.
	var fade_out := music_player
	var fade_in := _music_player_alt
	music_player = fade_in
	_music_player_alt = fade_out
	music_player.stream = target_stream
	music_transition_log.append({
		"state": state,
		"from_index": _music_last_index,
		"to_index": index,
		"reason": reason,
		"crossfade": playback_enabled,
	})
	if not playback_enabled:
		# Headless bookkeeping only: keep the observable level coherent without
		# emitting audio so runners can assert playlist state deterministically.
		music_player.volume_db = _music_target_db()
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB
		return
	_start_music_crossfade(fade_out)


func _start_music_crossfade(fade_out: AudioStreamPlayer) -> void:
	if _music_fade_tween != null and _music_fade_tween.is_valid():
		_music_fade_tween.kill()
	var target_db := _music_target_db()
	music_player.volume_db = UserSettingsScript.SILENT_DB
	music_player.play()
	var fade_out_active := fade_out != null and is_instance_valid(fade_out) and fade_out.playing
	_music_fade_tween = create_tween()
	_music_fade_tween.set_parallel(true)
	_music_fade_tween.tween_property(music_player, "volume_db", target_db, MUSIC_CROSSFADE_SECONDS)
	if fade_out_active:
		_music_fade_tween.tween_property(fade_out, "volume_db", UserSettingsScript.SILENT_DB, MUSIC_CROSSFADE_SECONDS)
	_music_fade_tween.chain().tween_callback(_stop_faded_out_player.bind(fade_out))


func _stop_faded_out_player(player: AudioStreamPlayer) -> void:
	if player != null and is_instance_valid(player) and player != music_player:
		player.stop()
		player.volume_db = UserSettingsScript.SILENT_DB


func _finalize_music_fade() -> void:
	if _music_fade_tween != null and _music_fade_tween.is_valid():
		_music_fade_tween.kill()
	_music_fade_tween = null
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.stop()
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB


func _on_music_finished(finished_player: AudioStreamPlayer) -> void:
	# Only the active leaf reaching its natural end advances the playlist; the
	# fading-out side is stopped, not naturally finished, so it never advances.
	if finished_player != music_player:
		return
	if current_music_state == "" or not music_playlists.has(current_music_state):
		return
	var next_index := _choose_next_music_index(current_music_state, _music_active_index)
	_transition_music(current_music_state, next_index, "advance")


func _choose_next_music_index(state: String, current_index: int) -> int:
	# Single-track states loop that track (never silence). Multi-track states pick
	# a random index that is never the one just played (shuffle, no immediate
	# repeat).
	var playlist: Array = music_playlists.get(state, [])
	var count := playlist.size()
	if count <= 0:
		return -1
	if count == 1:
		return 0
	var next := current_index
	while next == current_index:
		next = _music_rng.randi_range(0, count - 1)
	return next


func _music_target_db() -> float:
	return UserSettingsScript.volume_to_db(music_volume, muted)


func _reset_music_playback() -> void:
	_finalize_music_fade()
	if music_player != null and is_instance_valid(music_player):
		music_player.stop()
		music_player.stream = null
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.stop()
		_music_player_alt.stream = null
	_music_active_index = -1
	_music_last_index = -1
	current_music_track_index = -1
	_music_rng.randomize()


func _load_stream(path: String) -> AudioStream:
	# The selected retail manifest intentionally reuses many physical samples
	# across logical SAGE events.  Decode each immutable pack leaf once during a
	# configure pass instead of rereading and reparsing the same WAV repeatedly.
	# Routes still carry the concrete AudioStream, so corrupt injected routes
	# continue to fail closed in _route_definition.
	var cache_key := path.replace("\\", "/").to_lower()
	var cached: Variant = _stream_cache.get(cache_key)
	if cached is AudioStream:
		return cached as AudioStream
	if not _is_resolved_audio_path(path) or not _has_supported_audio_header(path):
		return null
	var stream: AudioStream = null
	match path.get_extension().to_lower():
		"wav":
			stream = AudioStreamWAV.load_from_file(path)
		"ogg":
			stream = AudioStreamOggVorbis.load_from_file(path)
		"mp3":
			stream = AudioStreamMP3.load_from_file(path)
	if stream != null:
		_stream_cache[cache_key] = stream
	return stream


func _is_resolved_audio_path(path: String) -> bool:
	var content_db := get_node_or_null("/root/ContentDB")
	return content_db != null and content_db.has_method("is_resolved_asset_path") and bool(content_db.call("is_resolved_asset_path", path))


func _has_supported_audio_header(path: String) -> bool:
	var extension := path.get_extension().to_lower()
	if extension != "wav" and extension != "ogg" and extension != "mp3":
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return false
	var header := file.get_buffer(12)
	if extension == "wav":
		return header.size() >= 12 and _ascii_at(header, 0, 4) == "RIFF" and _ascii_at(header, 8, 4) == "WAVE"
	if extension == "ogg":
		return header.size() >= 4 and _ascii_at(header, 0, 4) == "OggS"
	return header.size() >= 3 and (_ascii_at(header, 0, 3) == "ID3" or (int(header[0]) == 0xff and (int(header[1]) & 0xe0) == 0xe0))


func _ascii_at(bytes: PackedByteArray, offset: int, length: int) -> String:
	if offset < 0 or length < 0 or offset + length > bytes.size():
		return ""
	return bytes.slice(offset, offset + length).get_string_from_ascii()


func count_voice_kind(kind: String) -> int:
	return (voice_streams.get(kind, []) as Array).size()


func count_roster_voice_kind(object_id: String, kind: String) -> int:
	var by_kind: Dictionary = roster_voice_routes.get(object_id, {})
	var route: Dictionary = by_kind.get(kind, {})
	return Array(route.get("leaves", [])).size()


func _append_bounded_observability(log: Array[Dictionary], row: Dictionary) -> void:
	if not observability_enabled:
		return
	log.append(row)
	if log.size() <= MAX_OBSERVABILITY_LOG_ENTRIES:
		return
	var retained := log.slice(OBSERVABILITY_LOG_TRIM_COUNT)
	log.clear()
	log.append_array(retained)


func stop_all() -> void:
	_finalize_music_fade()
	for player in [music_player, _music_player_alt, voice_player, sfx_player]:
		if player != null and is_instance_valid(player):
			player.stop()
			player.stream = null
	music_streams.clear()
	music_playlists.clear()
	music_playlist_paths.clear()
	music_transition_log.clear()
	_music_active_index = -1
	_music_last_index = -1
	current_music_track_index = -1
	voice_streams = {"select": [], "attack": []}
	audio_event_routes.clear()
	roster_voice_routes.clear()
	_stream_cache.clear()
	_clear_ambient_players()
	ambient_emitters.clear()
	ambient_contract_declared = false
	ambient_parity_ready = false
	declared_structure_lifecycle_audio_active = false


func dispose() -> void:
	stop_all()
	for player in [music_player, _music_player_alt, voice_player, sfx_player]:
		if player != null and is_instance_valid(player):
			player.free()
	music_player = null
	_music_player_alt = null
	voice_player = null
	sfx_player = null


func _exit_tree() -> void:
	stop_all()
