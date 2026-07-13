class_name Stage9AudioEventRouter
extends RefCounted
## Observable deterministic audio intents; deliberately independent of audio hardware.

const ROUTE_KEYS: Array[String] = ["claim", "drop", "hit", "loss", "music_contest", "music_defeat", "music_explore", "music_victory", "reclaim", "spawn", "victory"]

var routes: Dictionary = {}
var events: Array[Dictionary] = []
var _next_sequence: int = 1


func configure(definition: Dictionary) -> String:
	if definition.size() != ROUTE_KEYS.size():
		return "invalid_audio_routes"
	var seen: Dictionary = {}
	for key: String in ROUTE_KEYS:
		if not definition.has(key) or typeof(definition[key]) != TYPE_STRING:
			return "invalid_audio_route_%s" % key
		var event_id: String = String(definition[key])
		if event_id.is_empty() or seen.has(event_id) or not event_id.contains("."):
			return "invalid_audio_event_id"
		seen[event_id] = true
	routes = definition.duplicate(true)
	reset()
	return ""


func reset() -> void:
	events.clear()
	_next_sequence = 1


func route(kind: String, tick: int, team: int = -1, entity_id: int = 0) -> Dictionary:
	if not routes.has(kind):
		return {"ok": false, "reason": "unknown_audio_route"}
	var event_id: String = String(routes[kind])
	var row: Dictionary = {
		"sequence": _next_sequence,
		"tick": tick,
		"kind": kind,
		"event_id": event_id,
		"category": event_id.get_slice(".", 0),
		"team": team,
		"entity_id": entity_id,
	}
	_next_sequence += 1
	events.append(row)
	return {"ok": true, "reason": "", "event": row.duplicate(true)}


func events_for(kind: String) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for row: Dictionary in events:
		if String(row["kind"]) == kind:
			result.append(row.duplicate(true))
	return result


func snapshot() -> Dictionary:
	return {"next_sequence": _next_sequence, "routes": routes.duplicate(true), "events": events.duplicate(true)}
