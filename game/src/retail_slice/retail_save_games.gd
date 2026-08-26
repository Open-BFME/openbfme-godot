extends RefCounted
## Save-game files for the skirmish slice (Q84). One JSON file per save under
## user://saves: the launch facts the shell needs to reboot the exact match
## (faction/map/rules/team setup + the mounted pack), plus the sim's full
## authoritative snapshot. Everything validates fail-closed by name — a save
## from another pack or a malformed file is REFUSED with its reason, never
## half-loaded.

const SCHEMA := "openbfme.save-game"
const SCHEMA_VERSION := 0
const SAVE_DIR := "user://saves"
const MAX_SAVE_BYTES := 64 * 1024 * 1024


static func save_directory() -> String:
	return SAVE_DIR


static func write_save(name: String, launch: Dictionary, simulation) -> Dictionary:
	## `launch` carries the shell handoff facts (see retail_vertical_slice
	## launch_facts()); `simulation` is the live sim. Returns {ok, path|reason}.
	if simulation == null:
		return {"ok": false, "reason": "no live simulation to save"}
	if String(launch.get("pack_root", "")) == "":
		return {"ok": false, "reason": "launch facts carry no pack root; refusing an unattributable save"}
	var cleaned := _safe_name(name)
	if cleaned == "":
		return {"ok": false, "reason": "save name is empty after sanitizing"}
	var absent := DirAccess.make_dir_recursive_absolute(SAVE_DIR)
	if absent != OK and absent != ERR_ALREADY_EXISTS:
		return {"ok": false, "reason": "cannot create %s (%s)" % [SAVE_DIR, error_string(absent)]}
	var snapshot: PackedByteArray = simulation.snapshot()
	if snapshot.is_empty():
		return {"ok": false, "reason": "simulation snapshot is empty"}
	var document := {
		"schema": SCHEMA,
		"schemaVersion": SCHEMA_VERSION,
		"name": cleaned,
		"savedAtUnixMs": int(Time.get_unix_time_from_system() * 1000.0),
		"tick": int(simulation.tick_index),
		"packRoot": String(launch.get("pack_root", "")),
		"playerFaction": String(launch.get("player_faction", "")),
		"mapId": String(launch.get("map_id", "")),
		# The shell's launch keys VERBATIM — the load path replays these onto
		# GameState so the boot compiles the exact same match configuration.
		"gameState": launch.get("game_state", {}),
		"stateSignature": String(simulation.state_signature()),
		"snapshotBase64": Marshalls.raw_to_base64(snapshot),
	}
	var path := "%s/%s.json" % [SAVE_DIR, cleaned]
	var handle := FileAccess.open(path, FileAccess.WRITE)
	if handle == null:
		return {"ok": false, "reason": "cannot write %s (%s)" % [path, error_string(FileAccess.get_open_error())]}
	handle.store_string(JSON.stringify(document))
	handle.close()
	return {"ok": true, "path": path, "tick": int(document["tick"])}


static func list_saves() -> Array:
	## Every *.json under SAVE_DIR, newest first. Malformed files are LISTED
	## with their named refusal (loadable=false) rather than silently hidden.
	var rows: Array = []
	var dir := DirAccess.open(SAVE_DIR)
	if dir == null:
		return rows
	for file_name in dir.get_files():
		if not file_name.to_lower().ends_with(".json"):
			continue
		var path := "%s/%s" % [SAVE_DIR, file_name]
		var header := read_header(path)
		header["path"] = path
		header["fileName"] = file_name
		rows.append(header)
	rows.sort_custom(func(a, b): return int(a.get("savedAtUnixMs", 0)) > int(b.get("savedAtUnixMs", 0)))
	return rows


static func read_header(path: String) -> Dictionary:
	## The save minus its snapshot payload; {loadable:false, reason} on refusal.
	var document := _read_document(path)
	if document.has("reason"):
		return {"loadable": false, "reason": String(document["reason"])}
	var header := document.duplicate()
	header.erase("snapshotBase64")
	header["loadable"] = true
	return header


static func read_snapshot(path: String) -> Dictionary:
	## {ok, bytes|reason}: the full authoritative snapshot for sim.restore().
	var document := _read_document(path)
	if document.has("reason"):
		return {"ok": false, "reason": String(document["reason"])}
	var bytes := Marshalls.base64_to_raw(String(document.get("snapshotBase64", "")))
	if bytes.is_empty():
		return {"ok": false, "reason": "save %s carries no snapshot payload" % path}
	return {"ok": true, "bytes": bytes, "header": read_header(path)}


static func _read_document(path: String) -> Dictionary:
	var handle := FileAccess.open(path, FileAccess.READ)
	if handle == null:
		return {"reason": "cannot open %s (%s)" % [path, error_string(FileAccess.get_open_error())]}
	if handle.get_length() > MAX_SAVE_BYTES:
		return {"reason": "save exceeds %d MB bound" % (MAX_SAVE_BYTES / 1048576)}
	var text := handle.get_as_text()
	handle.close()
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return {"reason": "save is not a JSON object"}
	var document := parsed as Dictionary
	if String(document.get("schema", "")) != SCHEMA:
		return {"reason": "schema is %s, not %s" % [String(document.get("schema", "?")), SCHEMA]}
	if int(document.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return {"reason": "schemaVersion %s is unsupported" % str(document.get("schemaVersion"))}
	for required in ["packRoot", "playerFaction", "mapId", "gameState", "snapshotBase64", "tick"]:
		if not document.has(required):
			return {"reason": "save is missing '%s'" % required}
	return document


static func _safe_name(name: String) -> String:
	var cleaned := ""
	for character in name.strip_edges():
		if character.to_lower() in "abcdefghijklmnopqrstuvwxyz0123456789-_ ":
			cleaned += character
	return cleaned.strip_edges().replace(" ", "_").substr(0, 64)
