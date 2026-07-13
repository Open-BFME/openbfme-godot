class_name Stage8SaveCodec
extends RefCounted
## Canonical text and user:// slot transport; no rendering or platform UI required.

const SLOT_PREFIX: String = "user://openbfme_stage8_slot_"


static func encode(snapshot: Dictionary) -> String:
	return JSON.stringify(snapshot, "", true)


static func decode(text: String) -> Dictionary:
	var parser := JSON.new()
	if parser.parse(text) != OK:
		return {}
	var parsed: Variant = parser.data
	return parsed if typeof(parsed) == TYPE_DICTIONARY else {}


static func slot_path(slot: int) -> String:
	return SLOT_PREFIX + "%d.json" % slot


static func write_slot(slot: int, snapshot: Dictionary) -> Dictionary:
	if slot < 1 or slot > 9:
		return {"ok": false, "reason": "invalid_slot"}
	var file: FileAccess = FileAccess.open(slot_path(slot), FileAccess.WRITE)
	if file == null:
		return {"ok": false, "reason": "slot_open_failed"}
	var text: String = encode(snapshot)
	file.store_string(text)
	file.close()
	return {"ok": true, "reason": "", "bytes": text.to_utf8_buffer().size(), "path": slot_path(slot)}


static func read_slot(slot: int) -> Dictionary:
	if slot < 1 or slot > 9:
		return {"ok": false, "reason": "invalid_slot", "snapshot": {}}
	var path: String = slot_path(slot)
	if not FileAccess.file_exists(path):
		return {"ok": false, "reason": "slot_missing", "snapshot": {}}
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"ok": false, "reason": "slot_open_failed", "snapshot": {}}
	var text: String = file.get_as_text()
	file.close()
	var snapshot: Dictionary = decode(text)
	if snapshot.is_empty():
		return {"ok": false, "reason": "slot_invalid_json", "snapshot": {}}
	return {"ok": true, "reason": "", "bytes": text.to_utf8_buffer().size(), "snapshot": snapshot}


static func delete_slot(slot: int) -> void:
	var path: String = slot_path(slot)
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
