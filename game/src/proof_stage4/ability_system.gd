class_name Stage4AbilitySystem
extends RefCounted
## Strict data-driven target validation and deterministic ability execution.

const DEFINITION_KEYS: Array[String] = [
	"activation_mode",
	"code",
	"cooldown_ticks",
	"effect",
	"magnitude",
	"name",
	"rank_required",
	"range_cells",
	"secondary_magnitude",
	"target_mode",
]
const TARGET_MODES: Array[String] = ["self", "position", "friendly_entity", "hostile_entity"]
const EFFECTS_FOR_MODE: Dictionary = {
	"self": ["self_heal", "guard_toggle"],
	"position": ["position_dash"],
	"friendly_entity": ["replenish"],
	"hostile_entity": ["damage_knockback"],
}

var _definitions: Dictionary = {}


func configure(definitions: Array[Dictionary]) -> String:
	if definitions.is_empty():
		return "abilities must not be empty"
	var parsed: Dictionary = {}
	var seen_modes: Dictionary = {}
	for index: int in range(definitions.size()):
		var definition: Dictionary = definitions[index]
		var key_error: String = _require_exact_keys(definition, DEFINITION_KEYS, "abilities[%d]" % index)
		if key_error != "":
			return key_error
		for field: String in ["code", "cooldown_ticks", "magnitude", "rank_required", "range_cells", "secondary_magnitude"]:
			if not _is_integer_value(definition[field]):
				return "abilities[%d].%s must be an integer" % [index, field]
		if typeof(definition["name"]) != TYPE_STRING or String(definition["name"]).is_empty():
			return "abilities[%d].name must be a non-empty string" % index
		if typeof(definition["target_mode"]) != TYPE_STRING or not TARGET_MODES.has(String(definition["target_mode"])):
			return "abilities[%d].target_mode is invalid" % index
		if typeof(definition["activation_mode"]) != TYPE_STRING or not ["instant", "toggle"].has(String(definition["activation_mode"])):
			return "abilities[%d].activation_mode is invalid" % index
		if typeof(definition["effect"]) != TYPE_STRING:
			return "abilities[%d].effect must be a string" % index
		var mode: String = String(definition["target_mode"])
		var effect: String = String(definition["effect"])
		if not Array(EFFECTS_FOR_MODE[mode]).has(effect):
			return "abilities[%d].effect does not match target_mode" % index
		var activation: String = String(definition["activation_mode"])
		if (activation == "toggle") != (mode == "self" and effect == "guard_toggle"):
			return "abilities[%d] toggle activation must use self guard_toggle" % index
		var code: int = int(definition["code"])
		if code <= 0 or parsed.has(code):
			return "abilities[%d].code must be positive and unique" % index
		if int(definition["rank_required"]) < 1:
			return "abilities[%d].rank_required must be positive" % index
		if int(definition["cooldown_ticks"]) < 0 or int(definition["range_cells"]) < 0:
			return "abilities[%d] cooldown and range must not be negative" % index
		if int(definition["magnitude"]) < 0 or int(definition["secondary_magnitude"]) < 0:
			return "abilities[%d] magnitudes must not be negative" % index
		parsed[code] = definition.duplicate(true)
		seen_modes[mode] = true
	for mode: String in TARGET_MODES:
		if not seen_modes.has(mode):
			return "abilities must cover target mode %s" % mode
	_definitions = parsed
	return ""


func cast(world: Object, caster_id: int, ability_code: int, target_spec: Dictionary) -> Dictionary:
	if not _definitions.has(ability_code):
		return _failure("unknown_ability")
	var caster: Dictionary = world.entity(caster_id)
	if caster.is_empty() or String(caster.get("kind", "")) != "champion":
		return _failure("invalid_caster")
	if not bool(caster.get("alive", false)):
		return _failure("caster_dead")
	var definition: Dictionary = _definitions[ability_code]
	if int(caster.get("rank", 1)) < int(definition["rank_required"]):
		return _failure("rank_locked")
	var cooldowns: Dictionary = caster.get("cooldowns", {})
	var ready_tick: int = int(cooldowns.get(ability_code, 0))
	if int(world.tick_index) < ready_tick:
		var cooldown_failure: Dictionary = _failure("cooldown")
		cooldown_failure["ready_tick"] = ready_tick
		return cooldown_failure
	var validation: Dictionary = _validate_target(world, caster, definition, target_spec)
	if not bool(validation.get("ok", false)):
		return validation
	var result: Dictionary = _execute(world, caster, definition, validation)
	if not bool(result.get("ok", false)):
		return result
	ready_tick = int(world.tick_index) + int(definition["cooldown_ticks"])
	cooldowns[ability_code] = ready_tick
	caster["cooldowns"] = cooldowns
	result["ability_code"] = ability_code
	result["ready_tick"] = ready_tick
	return result


func definition(ability_code: int) -> Dictionary:
	if not _definitions.has(ability_code):
		return {}
	return Dictionary(_definitions[ability_code]).duplicate(true)


func definitions_snapshot() -> Array[Dictionary]:
	var codes: Array[int] = []
	for key: Variant in _definitions.keys():
		codes.append(int(key))
	codes.sort()
	var rows: Array[Dictionary] = []
	for code: int in codes:
		rows.append(Dictionary(_definitions[code]).duplicate(true))
	return rows


func _validate_target(world: Object, caster: Dictionary, definition: Dictionary, target_spec: Dictionary) -> Dictionary:
	var mode: String = String(definition["target_mode"])
	if mode == "self":
		if not target_spec.is_empty():
			return _failure("unexpected_target")
		return {"ok": true, "reason": "", "target_entity": caster}
	if mode == "position":
		if target_spec.size() != 1 or not target_spec.has("position") or typeof(target_spec["position"]) != TYPE_VECTOR2I:
			return _failure("invalid_position_target")
		var position: Vector2i = target_spec["position"]
		if not bool(world.contains_cell(position)):
			return _failure("position_out_of_bounds")
		if int(world.distance_cells(Vector2i(caster["position"]), position)) > int(definition["range_cells"]):
			return _failure("target_out_of_range")
		return {"ok": true, "reason": "", "target_position": position}
	if target_spec.size() != 1 or not target_spec.has("entity_id") or typeof(target_spec["entity_id"]) != TYPE_INT:
		return _failure("invalid_entity_target")
	var target: Dictionary = world.entity(int(target_spec["entity_id"]))
	if target.is_empty() or not bool(target.get("alive", false)):
		return _failure("target_dead_or_missing")
	if int(world.distance_cells(Vector2i(caster["position"]), Vector2i(target["position"]))) > int(definition["range_cells"]):
		return _failure("target_out_of_range")
	var same_team: bool = int(caster["team"]) == int(target["team"])
	if mode == "friendly_entity" and not same_team:
		return _failure("target_not_friendly")
	if mode == "hostile_entity" and same_team:
		return _failure("target_not_hostile")
	return {"ok": true, "reason": "", "target_entity": target}


func _execute(world: Object, caster: Dictionary, definition: Dictionary, target: Dictionary) -> Dictionary:
	var effect: String = String(definition["effect"])
	if effect == "self_heal":
		var healed: int = int(world.heal_entity(int(caster["id"]), int(definition["magnitude"])))
		if healed <= 0:
			return _failure("already_full_health")
		return {"ok": true, "reason": "", "healed": healed}
	if effect == "guard_toggle":
		return world.toggle_guard(
			int(caster["id"]),
			int(definition["code"]),
			int(definition["magnitude"]),
			int(definition["secondary_magnitude"])
		)
	if effect == "position_dash":
		return world.move_entity_safely(int(caster["id"]), Vector2i(target["target_position"]), int(definition["secondary_magnitude"]))
	if effect == "replenish":
		return world.replenish_squad(int(Dictionary(target["target_entity"])["id"]), int(definition["magnitude"]))
	if effect == "damage_knockback":
		var hostile: Dictionary = target["target_entity"]
		var source_position: Vector2i = caster["position"]
		var damage: Dictionary = world.damage_entity(int(hostile["id"]), int(definition["magnitude"]), int(caster["id"]))
		if not bool(damage.get("ok", false)):
			return damage
		var knockback: Dictionary = {"ok": true, "reason": "", "moved_cells": 0}
		if bool(hostile.get("alive", false)):
			knockback = world.apply_knockback(int(hostile["id"]), source_position, int(definition["secondary_magnitude"]))
		return {
			"ok": true,
			"reason": "",
			"damage": int(damage.get("damage", 0)),
			"knockback_cells": int(knockback.get("moved_cells", 0)),
		}
	return _failure("unsupported_effect")


static func _failure(reason: String) -> Dictionary:
	return {"ok": false, "reason": reason}


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer_value(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))
