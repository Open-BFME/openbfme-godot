class_name Stage4ProgressionSystem
extends RefCounted
## Deterministic veterancy, stance modifiers, and concrete-member replenishment.

const DEFINITION_KEYS: Array[String] = [
	"rank_thresholds",
	"replenish_delay_ticks",
	"replenish_health_permille",
	"replenish_interval_ticks",
	"replenish_members_per_interval",
	"stances",
	"xp_damage_permille",
	"xp_per_champion_defeat",
	"xp_per_member_defeat",
]
const STANCE_KEYS: Array[String] = [
	"armor_permille",
	"code",
	"damage_permille",
	"speed_permille",
]

var rank_thresholds: Array[int] = []
var replenish_health_permille: int = 1000
var xp_per_member_defeat: int = 0
var xp_per_champion_defeat: int = 0
var xp_damage_permille: int = 0
var replenish_delay_ticks: int = 0
var replenish_interval_ticks: int = 1
var replenish_members_per_interval: int = 1
var _stances: Dictionary = {}


func configure(definition: Dictionary) -> String:
	var key_error: String = _require_exact_keys(definition, DEFINITION_KEYS, "progression")
	if key_error != "":
		return key_error
	if typeof(definition["rank_thresholds"]) != TYPE_ARRAY:
		return "progression.rank_thresholds must be an array"
	var raw_thresholds: Array = definition["rank_thresholds"]
	if raw_thresholds.is_empty():
		return "progression.rank_thresholds must not be empty"
	var parsed_thresholds: Array[int] = []
	var previous: int = -1
	for index: int in range(raw_thresholds.size()):
		if not _is_integer_value(raw_thresholds[index]):
			return "progression.rank_thresholds[%d] must be an integer" % index
		var threshold: int = int(raw_thresholds[index])
		if threshold < 0 or threshold <= previous:
			return "progression.rank_thresholds must be strictly increasing"
		parsed_thresholds.append(threshold)
		previous = threshold
	if parsed_thresholds[0] != 0:
		return "progression.rank_thresholds[0] must be zero"
	if not _is_integer_value(definition["replenish_health_permille"]):
		return "progression.replenish_health_permille must be an integer"
	var parsed_replenish: int = int(definition["replenish_health_permille"])
	if parsed_replenish < 1 or parsed_replenish > 1000:
		return "progression.replenish_health_permille must be in 1..1000"
	for field: String in ["xp_damage_permille", "xp_per_member_defeat", "xp_per_champion_defeat", "replenish_delay_ticks", "replenish_interval_ticks", "replenish_members_per_interval"]:
		if not _is_integer_value(definition[field]):
			return "progression.%s must be an integer" % field
		if int(definition[field]) < 0:
			return "progression.%s must not be negative" % field
	if int(definition["replenish_interval_ticks"]) < 1 or int(definition["replenish_members_per_interval"]) < 1:
		return "progression replenishment interval and member count must be positive"
	if typeof(definition["stances"]) != TYPE_ARRAY:
		return "progression.stances must be an array"
	var raw_stances: Array = definition["stances"]
	if raw_stances.is_empty():
		return "progression.stances must not be empty"
	var parsed_stances: Dictionary = {}
	for index: int in range(raw_stances.size()):
		if typeof(raw_stances[index]) != TYPE_DICTIONARY:
			return "progression.stances[%d] must be a dictionary" % index
		var stance: Dictionary = raw_stances[index]
		key_error = _require_exact_keys(stance, STANCE_KEYS, "progression.stances[%d]" % index)
		if key_error != "":
			return key_error
		if typeof(stance["code"]) != TYPE_STRING or String(stance["code"]).is_empty():
			return "progression.stances[%d].code must be a non-empty string" % index
		var code: String = String(stance["code"])
		if parsed_stances.has(code):
			return "progression stance code %s is duplicated" % code
		for field: String in ["damage_permille", "armor_permille", "speed_permille"]:
			if not _is_integer_value(stance[field]) or int(stance[field]) < 100 or int(stance[field]) > 3000:
				return "progression.stances[%d].%s must be an integer in 100..3000" % [index, field]
		parsed_stances[code] = stance.duplicate(true)
	if not parsed_stances.has("balanced"):
		return "progression.stances must define balanced"
	rank_thresholds = parsed_thresholds
	replenish_health_permille = parsed_replenish
	xp_per_member_defeat = int(definition["xp_per_member_defeat"])
	xp_per_champion_defeat = int(definition["xp_per_champion_defeat"])
	xp_damage_permille = int(definition["xp_damage_permille"])
	replenish_delay_ticks = int(definition["replenish_delay_ticks"])
	replenish_interval_ticks = int(definition["replenish_interval_ticks"])
	replenish_members_per_interval = int(definition["replenish_members_per_interval"])
	_stances = parsed_stances
	return ""


func rank_for_xp(xp: int) -> int:
	var rank: int = 1
	for index: int in range(rank_thresholds.size()):
		if xp < rank_thresholds[index]:
			break
		rank = index + 1
	return rank


func award_xp(entity: Dictionary, amount: int) -> Dictionary:
	if amount <= 0 or not bool(entity.get("alive", false)):
		return {"ok": false, "reason": "invalid_xp_award", "old_rank": int(entity.get("rank", 1)), "new_rank": int(entity.get("rank", 1))}
	var old_rank: int = int(entity.get("rank", 1))
	entity["xp"] = int(entity.get("xp", 0)) + amount
	entity["rank"] = rank_for_xp(int(entity["xp"]))
	return {"ok": true, "reason": "", "old_rank": old_rank, "new_rank": int(entity["rank"])}


func set_stance(entity: Dictionary, stance_code: String) -> Dictionary:
	if not bool(entity.get("alive", false)):
		return {"ok": false, "reason": "entity_dead"}
	if not _stances.has(stance_code):
		return {"ok": false, "reason": "unknown_stance"}
	entity["stance"] = stance_code
	return {"ok": true, "reason": ""}


func stance_definition(stance_code: String) -> Dictionary:
	if not _stances.has(stance_code):
		return {}
	return Dictionary(_stances[stance_code]).duplicate(true)


func damage_after_stances(base_damage: int, source: Dictionary, target: Dictionary) -> int:
	if base_damage <= 0:
		return 0
	var source_stance: Dictionary = _stances.get(String(source.get("stance", "balanced")), _stances.get("balanced", {}))
	var target_stance: Dictionary = _stances.get(String(target.get("stance", "balanced")), _stances.get("balanced", {}))
	var damage_permille: int = int(source_stance.get("damage_permille", 1000))
	var armor_permille: int = maxi(1, int(target_stance.get("armor_permille", 1000)) + int(target.get("toggle_armor_bonus_permille", 0)))
	var numerator: int = base_damage * damage_permille
	return maxi(1, floori(float(numerator) / float(armor_permille)))


func speed_permille(entity: Dictionary) -> int:
	var stance: Dictionary = _stances.get(String(entity.get("stance", "balanced")), _stances.get("balanced", {}))
	return maxi(0, int(stance.get("speed_permille", 1000)) - int(entity.get("toggle_speed_penalty_permille", 0)))


func replenish(squad: Dictionary, member_count: int) -> Dictionary:
	if member_count <= 0 or String(squad.get("kind", "")) != "squad":
		return {"ok": false, "reason": "invalid_replenishment", "restored_ids": []}
	var raw_members: Variant = squad.get("members", [])
	if typeof(raw_members) != TYPE_ARRAY:
		return {"ok": false, "reason": "invalid_roster", "restored_ids": []}
	var candidates: Array[Dictionary] = []
	for member_value: Variant in raw_members:
		if typeof(member_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "invalid_roster", "restored_ids": []}
		var member: Dictionary = member_value
		if not bool(member.get("alive", false)):
			candidates.append(member)
	candidates.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left.get("id", 0)) < int(right.get("id", 0)))
	if candidates.is_empty():
		return {"ok": false, "reason": "squad_full", "restored_ids": []}
	var restored_ids: Array[int] = []
	for member: Dictionary in candidates:
		if restored_ids.size() >= member_count:
			break
		var restored_health: int = maxi(1, floori(float(int(member.get("max_health", 1)) * replenish_health_permille) / 1000.0))
		member["health"] = restored_health
		member["alive"] = true
		restored_ids.append(int(member["id"]))
	squad["alive"] = living_member_count(squad) > 0
	return {"ok": true, "reason": "", "restored_ids": restored_ids}


func living_member_count(squad: Dictionary) -> int:
	var count: int = 0
	var raw_members: Variant = squad.get("members", [])
	if typeof(raw_members) != TYPE_ARRAY:
		return 0
	for member_value: Variant in raw_members:
		if typeof(member_value) == TYPE_DICTIONARY and bool(Dictionary(member_value).get("alive", false)):
			count += 1
	return count


func definitions_snapshot() -> Dictionary:
	var stance_codes: Array[String] = []
	for key: Variant in _stances.keys():
		stance_codes.append(String(key))
	stance_codes.sort()
	var stance_rows: Array[Dictionary] = []
	for code: String in stance_codes:
		stance_rows.append(Dictionary(_stances[code]).duplicate(true))
	return {
		"rank_thresholds": rank_thresholds.duplicate(),
		"replenish_health_permille": replenish_health_permille,
		"xp_per_member_defeat": xp_per_member_defeat,
		"xp_per_champion_defeat": xp_per_champion_defeat,
		"xp_damage_permille": xp_damage_permille,
		"replenish_delay_ticks": replenish_delay_ticks,
		"replenish_interval_ticks": replenish_interval_ticks,
		"replenish_members_per_interval": replenish_members_per_interval,
		"stances": stance_rows,
	}


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer_value(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))
