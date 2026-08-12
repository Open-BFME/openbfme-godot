class_name RetailCommand
extends RefCounted

const COMMAND_TYPES: Array[String] = [
	"issue_move",
	"issue_attack",
	"issue_attack_move",
	"issue_stop",
	"issue_toggle_stance",
	"issue_set_stance",
	"issue_toggle_formation",
	"issue_set_formation",
	"issue_construct",
	"issue_expansion_construct",
	"queue_unit",
	"queue_structure_upgrade",
	"queue_battalion_upgrade",
	"cancel_queued_unit",
	"purchase_power",
	"reset_spellbook_purchases",
	"accept_spellbook_purchases",
	"set_spellbook_orb_open",
	"cast_power",
	"cast_heal",
	"cast_rally",
	"cast_ability",
	"set_structure_rally",
	"sell_structure",
	"pause",
	"resume",
]


static func validate(cmd: Dictionary) -> bool:
	if cmd.size() != 5:
		return false
	for key in cmd.keys():
		if typeof(key) != TYPE_STRING or not ["tick", "team", "seq", "type", "args"].has(String(key)):
			return false
	if typeof(cmd.get("tick")) != TYPE_INT or int(cmd["tick"]) < 0:
		return false
	if typeof(cmd.get("team")) != TYPE_INT or int(cmd["team"]) < 0:
		return false
	if typeof(cmd.get("seq")) != TYPE_INT or int(cmd["seq"]) < 0:
		return false
	if typeof(cmd.get("type")) != TYPE_STRING or not COMMAND_TYPES.has(String(cmd["type"])):
		return false
	if typeof(cmd.get("args")) != TYPE_DICTIONARY:
		return false
	return _is_primitive_dictionary(cmd["args"] as Dictionary)


static func encode(cmd: Dictionary) -> PackedByteArray:
	if not validate(cmd):
		return PackedByteArray()
	return var_to_bytes(cmd)


static func decode(bytes: PackedByteArray) -> Dictionary:
	if bytes.is_empty():
		return {}
	var value: Variant = bytes_to_var(bytes)
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var cmd := value as Dictionary
	return cmd if validate(cmd) else {}


static func _is_primitive_dictionary(value: Dictionary) -> bool:
	for key in value.keys():
		if typeof(key) != TYPE_STRING or not _is_primitive_value(value[key]):
			return false
	return true


static func _is_primitive_value(value: Variant) -> bool:
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_VECTOR2:
			return true
		TYPE_ARRAY:
			for item in value as Array:
				if not _is_primitive_value(item):
					return false
			return true
	return false
