class_name NativeAudio
extends Node3D
## Snapshot/event adapter for the existing retail audio and music presenter.

const AudioScript := preload("res://src/retail_slice/retail_slice_audio.gd")

var configured := false
var error := ""
var audio
var play_counts := {
	"select": 0, "move": 0, "attack": 0, "fire": 0, "impact": 0,
	"death": 0, "sound": 0, "eva_unit_lost": 0, "eva_under_attack": 0,
	"eva_structure_lost": 0, "music": 0, "ambient": 0,
}
var route_log: Array[Dictionary] = []
var voice_resolution: Dictionary = {}

var _catalog: Array[Dictionary] = []
var _template_rows: Dictionary = {}
var _runtime_by_template: Dictionary = {}
var _object_id_by_template: Dictionary = {}
var _object_positions: Dictionary = {}
var _object_owner: Dictionary = {}
var _object_template: Dictionary = {}
var _listener := Vector2.ZERO
var _last_tick := -1
var _sequence := 1
var _battle_music := false
var _last_combat_tick := -100000
var _last_eva_tick: Dictionary = {}


func configure(
	template_rows: Array[Dictionary],
	map_pack_root: String,
	content_root: String,
	enable_playback: bool
) -> bool:
	error = ""
	_catalog = template_rows.duplicate(true)
	_index_templates()
	var runtimes := _matching_runtime_documents()
	if runtimes.is_empty():
		return _fail("no mounted playable-unit documents matched native templates")
	var eva_contract := {"eva_events": _eva_events(content_root, "men")}
	audio = AudioScript.new()
	audio.name = "RetailSliceAudio"
	add_child(audio)
	var closure: bool = audio.configure(
		map_pack_root, enable_playback, runtimes, eva_contract, "Men", 0
	)
	audio.observability_enabled = true
	audio.configure_spatial_audio(
		func(object_id: int, _is_structure: bool = false) -> Variant:
			return _object_positions.get(object_id, null),
		func(_position: Vector2) -> bool: return true,
		func() -> Vector2: return _listener,
		1.0
	)
	_resolve_voice_rows()
	play_counts.ambient = audio.ambient_emitters.size()
	_present_music("explore")
	configured = not audio.audio_event_routes.is_empty() and not voice_resolution.is_empty()
	if not closure:
		print("NATIVE_AUDIO_CLOSURE degraded=%s" % "; ".join(audio.readiness_diagnostics()))
	if not configured:
		return _fail("retail audio routes or native voice mappings are empty")
	print("NATIVE_AUDIO_READY templates=%d voices=%d ambient=%d playback=%s" % [
		_runtime_by_template.size(), voice_resolution.size(), audio.ambient_emitters.size(), enable_playback
	])
	return true


func set_listener(world_position: Vector3) -> void:
	_listener = Vector2(world_position.x, world_position.z)


func submit_snapshot(snapshot: Dictionary) -> bool:
	if not configured:
		return false
	var tick := int(snapshot.get("tick", -1))
	if tick <= _last_tick:
		return false
	_last_tick = tick
	_index_snapshot(snapshot)
	var events := snapshot.get("events", []) as Array
	for event_index in events.size():
		var value: Variant = events[event_index]
		if value is Dictionary:
			_consume_snapshot_event(value as Dictionary, tick, event_index)
	if _battle_music and tick - _last_combat_tick >= 300:
		_present_music("explore")
		_battle_music = false
	return true


func present_selection(horde_ids: Array[int], snapshot: Dictionary) -> void:
	if not configured or horde_ids.is_empty():
		return
	_index_snapshot(snapshot)
	var member := _first_horde_member(int(horde_ids[0]), snapshot)
	var object_id := _runtime_object_id(member)
	_present_route("select", audio.route_roster_voice(object_id, "select", _take_sequence()), member)


func present_order(command_type: String, horde_ids: Array[int], snapshot: Dictionary) -> void:
	if not configured or horde_ids.is_empty():
		return
	_index_snapshot(snapshot)
	var member := _first_horde_member(int(horde_ids[0]), snapshot)
	var kind := "attack" if command_type in ["attack", "attack_move"] else "move"
	var object_id := _runtime_object_id(member)
	_present_route(kind, audio.route_roster_voice(object_id, kind, _take_sequence()), member)


func resolve_voice_rows(template_name: String) -> Dictionary:
	return (voice_resolution.get(template_name.to_lower(), {}) as Dictionary).duplicate(true)


func play_count_total() -> int:
	var total := 0
	for value in play_counts.values():
		total += int(value)
	return total


func _consume_snapshot_event(event: Dictionary, tick: int, event_index: int) -> void:
	var kind := String(event.get("kind", ""))
	var object_id := int(event.get("object", 0))
	var target_id := int(event.get("target", 0))
	var sequence := tick * 1000 + event_index + 1
	var authored_name := String(event.get("name", ""))
	match kind:
		"fire":
			var runtime_id := _runtime_object_id(object_id)
			var route: Dictionary = audio.call("_route_weapon_swing", runtime_id, sequence)
			if not authored_name.is_empty():
				route = audio.route_audio_event(authored_name, sequence)
			_present_route("fire", route, object_id)
			_enter_battle_music(tick)
		"damage":
			if not authored_name.is_empty():
				_present_route("impact", audio.route_audio_event(authored_name, sequence), target_id if target_id > 0 else object_id)
			if int(_object_owner.get(target_id, -1)) == 0 and _eva_due("under_attack", tick, 300):
				_present_eva("UnitUnderAttack", "eva_under_attack", sequence, tick)
			_enter_battle_music(tick)
		"death":
			var dead := target_id if target_id > 0 else object_id
			var runtime_id := _runtime_object_id(dead)
			_present_route("death", audio.route_roster_voice(runtime_id, "death", sequence), dead)
			_present_route("death", audio.call("_route_bodyfall", runtime_id, sequence), dead, false)
			if int(_object_owner.get(dead, -1)) == 0:
				if _runtime_by_object(dead).is_empty():
					_present_eva("CampDestroyed", "eva_structure_lost", sequence, tick)
				else:
					# RotWK has no generic UnitLost EVA; the unit's authored death voice is
					# the retail announcement. Count the derived local-player loss here.
					play_counts.eva_unit_lost = int(play_counts.eva_unit_lost) + 1
			_enter_battle_music(tick)
		"sound":
			if not authored_name.is_empty():
				_present_route("sound", audio.route_audio_event(authored_name, sequence), object_id)
		"ability", "build_start", "build_done", "capture", "upgrade":
			if not authored_name.is_empty():
				_present_route("sound", audio.route_audio_event(authored_name, sequence), object_id)


func _present_route(kind: String, route: Dictionary, snapshot_object_id: int, count_rejection: bool = true) -> void:
	var accepted := bool(route.get("ok", false))
	if accepted:
		play_counts[kind] = int(play_counts.get(kind, 0)) + 1
		var world_position: Variant = _object_positions.get(snapshot_object_id, null)
		if kind in ["select", "move", "attack"]:
			audio.call("_play_world_routed", route, audio.voice_player, world_position)
		else:
			audio.call("_play_sfx", route, world_position, kind in ["fire", "impact", "death"])
	elif count_rejection:
		print("NATIVE_AUDIO_ROUTE_MISS kind=%s event=%s reason=%s" % [
			kind, String(route.get("event_id", "")), String(route.get("reason", ""))
		])
	var receipt := route.duplicate()
	receipt.erase("stream")
	receipt["native_kind"] = kind
	receipt["snapshot_object"] = snapshot_object_id
	receipt["position"] = _object_positions.get(snapshot_object_id, null)
	route_log.append(receipt)


func _present_eva(eva_id: String, count_kind: String, sequence: int, tick: int) -> void:
	var route: Dictionary = audio.play_eva_event(eva_id, sequence, tick * 33)
	if bool(route.get("ok", false)):
		play_counts[count_kind] = int(play_counts.get(count_kind, 0)) + 1
	var receipt := route.duplicate()
	receipt.erase("stream")
	receipt["native_kind"] = count_kind
	route_log.append(receipt)


func _present_music(state: String) -> void:
	var events: Array[Dictionary] = [{"kind": "music.%s" % state, "sequence": _take_sequence()}]
	audio.sync_events(events)
	audio.acknowledge_event_history_compaction(0)
	play_counts.music = int(play_counts.music) + 1


func _enter_battle_music(tick: int) -> void:
	_last_combat_tick = tick
	if not _battle_music:
		_present_music("battle")
		_battle_music = true


func _index_templates() -> void:
	_template_rows.clear()
	for index in _catalog.size():
		var row := _catalog[index]
		_template_rows[index] = row


func _matching_runtime_documents() -> Dictionary:
	_runtime_by_template.clear()
	_object_id_by_template.clear()
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return {}
	var all: Dictionary = content_db.call("get_playable_unit_runtimes")
	var wanted: Dictionary = {}
	for row in _catalog:
		wanted[String(row.get("name", "")).to_lower()] = true
	var output: Dictionary = {}
	for key in all:
		var document := all[key] as Dictionary
		var horde_name := String(document.get("objectId", ""))
		var composition := (document.get("registration", {}) as Dictionary).get("composition", {}) as Dictionary
		var member_name := String(composition.get("primaryMemberObjectId", horde_name))
		if not wanted.has(horde_name.to_lower()) and not wanted.has(member_name.to_lower()):
			continue
		var runtime_id := _runtime_id(member_name)
		output[key] = document
		_runtime_by_template[horde_name.to_lower()] = document
		_runtime_by_template[member_name.to_lower()] = document
		_object_id_by_template[horde_name.to_lower()] = runtime_id
		_object_id_by_template[member_name.to_lower()] = runtime_id
	return output


func _resolve_voice_rows() -> void:
	voice_resolution.clear()
	for template_key in _runtime_by_template:
		var object_id := String(_object_id_by_template.get(template_key, ""))
		var row: Dictionary = {"object_id": object_id, "events": {}, "line_count": 0}
		for kind in ["select", "move", "attack", "death"]:
			var route: Dictionary = audio.route_roster_voice(object_id, kind, 1)
			(row.events as Dictionary)[kind] = String(route.get("event_id", ""))
			row.line_count = int(row.line_count) + audio.count_roster_voice_kind(object_id, kind)
		voice_resolution[template_key] = row


func _index_snapshot(snapshot: Dictionary) -> void:
	_object_positions.clear()
	_object_owner.clear()
	_object_template.clear()
	var objects := snapshot.get("objects", {}) as Dictionary
	var ids := objects.get("id", []) as Array
	var owners := objects.get("owner", []) as Array
	var templates := objects.get("template", []) as Array
	var xs := objects.get("x", []) as Array
	var zs := objects.get("z", []) as Array
	for index in ids.size():
		var id := int(ids[index])
		_object_positions[id] = Vector2(float(xs[index]), float(zs[index]))
		_object_owner[id] = int(owners[index])
		_object_template[id] = int(templates[index])


func _runtime_object_id(snapshot_object_id: int) -> String:
	var template_index := int(_object_template.get(snapshot_object_id, -1))
	var row := _template_rows.get(template_index, {}) as Dictionary
	return String(_object_id_by_template.get(String(row.get("name", "")).to_lower(), ""))


func _runtime_by_object(snapshot_object_id: int) -> Dictionary:
	var template_index := int(_object_template.get(snapshot_object_id, -1))
	var row := _template_rows.get(template_index, {}) as Dictionary
	return _runtime_by_template.get(String(row.get("name", "")).to_lower(), {}) as Dictionary


func _first_horde_member(horde_id: int, snapshot: Dictionary) -> int:
	for value in snapshot.get("hordes", []) as Array:
		var horde := value as Dictionary
		if int(horde.get("id", 0)) == horde_id:
			var members := horde.get("members", []) as Array
			return 0 if members.is_empty() else int(members[0])
	return 0


func _eva_due(key: String, tick: int, cadence_ticks: int) -> bool:
	var previous := int(_last_eva_tick.get(key, -1000000))
	if tick - previous < cadence_ticks:
		return false
	_last_eva_tick[key] = tick
	return true


func _take_sequence() -> int:
	var value := _sequence
	_sequence += 1
	return value


static func _eva_events(content_root: String, side: String) -> Dictionary:
	var selection := _read_json(content_root.path_join("selection.json"))
	for value in selection.get("supplementalPacks", []) as Array:
		var reference := String(value)
		if not reference.to_lower().contains("%s-eva-overlay" % side.to_lower()):
			continue
		var document := _read_json(content_root.path_join(reference).path_join("data/eva_events.json"))
		return (document.get("events", {}) as Dictionary).duplicate(true)
	return {}


static func _read_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return parsed as Dictionary if parsed is Dictionary else {}


static func _runtime_id(source_id: String) -> String:
	var slug := ""
	var previous_dash := false
	for index in source_id.length():
		var code := source_id.unicode_at(index)
		var upper := code >= 65 and code <= 90
		var lower := code >= 97 and code <= 122
		var digit := code >= 48 and code <= 57
		if upper and index > 0 and not previous_dash:
			var previous := source_id.unicode_at(index - 1)
			if (previous >= 97 and previous <= 122) or (previous >= 48 and previous <= 57):
				slug += "-"
		if upper or lower or digit:
			slug += String.chr(code).to_lower()
			previous_dash = false
		elif not previous_dash and not slug.is_empty():
			slug += "-"
			previous_dash = true
	return "bfme2.object.%s" % slug.trim_suffix("-")


func _fail(message: String) -> bool:
	error = message
	push_error("[NativeAudio] %s" % message)
	return false
