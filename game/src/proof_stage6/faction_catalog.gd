class_name Stage6FactionCatalog
extends RefCounted
## Strict legal-safe faction, roster, upgrade, armor, and primitive-art catalog.

const ROOT_KEYS: Array[String] = ["armorClasses", "damageTypes", "factions", "rulesVersion", "schema", "schemaVersion", "upgrades"]
const ARMOR_KEYS: Array[String] = ["id", "multipliers"]
const UPGRADE_KEYS: Array[String] = ["armorBonusPermille", "cost", "damageBonusPermille", "damageType", "displayName", "id", "researchTicks"]
const FACTION_KEYS: Array[String] = ["displayName", "id", "roster", "startingResources", "teamColor", "upgradeIds"]
const UNIT_KEYS: Array[String] = ["armorClass", "art", "baseDamage", "cooldownTicks", "damageType", "displayName", "maximumHealth", "rangeCells", "unitId"]
const ART_KEYS: Array[String] = ["material", "shape", "source"]
const RATIO_KEYS: Array[String] = ["denominator", "numerator"]
const ALLOWED_SHAPES: Array[String] = ["box", "capsule", "cylinder", "diamond", "hexagon", "sphere"]

var damage_types: Array[String] = []
var armor_classes: Dictionary = {}
var upgrades: Dictionary = {}
var factions: Dictionary = {}
var units: Dictionary = {}
var unit_factions: Dictionary = {}


func configure(document: Dictionary) -> String:
	_clear()
	var error := _require_exact_keys(document, ROOT_KEYS, "root")
	if error != "":
		return error
	if String(document.get("schema", "")) != "openbfme.faction-rosters" or int(document.get("schemaVersion", -1)) != 0 or int(document.get("rulesVersion", -1)) != 1:
		return "unsupported faction roster schema"
	if typeof(document.get("damageTypes")) != TYPE_ARRAY or typeof(document.get("armorClasses")) != TYPE_ARRAY or typeof(document.get("upgrades")) != TYPE_ARRAY or typeof(document.get("factions")) != TYPE_ARRAY:
		return "catalog collections must be arrays"
	for raw_type: Variant in Array(document["damageTypes"]):
		if typeof(raw_type) != TYPE_STRING or String(raw_type).is_empty() or damage_types.has(String(raw_type)):
			return "damage types must be unique non-empty strings"
		damage_types.append(String(raw_type))
	if damage_types.size() < 2:
		return "at least two damage types are required"
	error = _parse_armor(Array(document["armorClasses"]))
	if error != "":
		return error
	error = _parse_upgrades(Array(document["upgrades"]))
	if error != "":
		return error
	error = _parse_factions(Array(document["factions"]))
	if error != "":
		return error
	if factions.size() < 2:
		return "multiple factions are required"
	return ""


func faction_ids() -> Array[String]:
	return _sorted_string_keys(factions)


func unit_ids() -> Array[String]:
	return _sorted_string_keys(units)


func upgrade_ids() -> Array[String]:
	return _sorted_string_keys(upgrades)


func faction(faction_id: String) -> Dictionary:
	return Dictionary(factions.get(faction_id, {})).duplicate(true)


func unit(unit_id: String) -> Dictionary:
	return Dictionary(units.get(unit_id, {})).duplicate(true)


func upgrade(upgrade_id: String) -> Dictionary:
	return Dictionary(upgrades.get(upgrade_id, {})).duplicate(true)


func armor(armor_id: String) -> Dictionary:
	return Dictionary(armor_classes.get(armor_id, {})).duplicate(true)


func faction_for_unit(unit_id: String) -> String:
	return String(unit_factions.get(unit_id, ""))


func roster_for(faction_id: String) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var row: Dictionary = factions.get(faction_id, {})
	for raw_unit: Variant in Array(row.get("roster", [])):
		result.append(Dictionary(raw_unit).duplicate(true))
	result.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return String(left["unitId"]) < String(right["unitId"]))
	return result


func resolve_art(unit_id: String) -> Dictionary:
	var row: Dictionary = units.get(unit_id, {})
	if row.is_empty():
		return {}
	var art: Dictionary = row.get("art", {})
	if String(art.get("source", "")) != "generated-primitive":
		return {}
	return {
		"unit_id": unit_id,
		"source": "generated-primitive",
		"shape": String(art.get("shape", "")),
		"material": String(art.get("material", "")),
		"resolved": true,
	}


func art_coverage() -> Dictionary:
	var resolved: int = 0
	var missing: Array[String] = []
	for unit_id: String in unit_ids():
		if resolve_art(unit_id).is_empty():
			missing.append(unit_id)
		else:
			resolved += 1
	return {"total": units.size(), "resolved": resolved, "missing": missing, "legal_safe": missing.is_empty()}


func definitions_snapshot() -> Dictionary:
	var armor_rows: Array[Dictionary] = []
	for armor_id: String in _sorted_string_keys(armor_classes):
		armor_rows.append(Dictionary(armor_classes[armor_id]).duplicate(true))
	var upgrade_rows: Array[Dictionary] = []
	for upgrade_id: String in upgrade_ids():
		upgrade_rows.append(Dictionary(upgrades[upgrade_id]).duplicate(true))
	var faction_rows: Array[Dictionary] = []
	for faction_id: String in faction_ids():
		faction_rows.append(Dictionary(factions[faction_id]).duplicate(true))
	return {
		"damage_types": damage_types.duplicate(),
		"armor_classes": armor_rows,
		"upgrades": upgrade_rows,
		"factions": faction_rows,
	}


func _parse_armor(rows: Array) -> String:
	for index: int in range(rows.size()):
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return "armorClasses[%d] must be an object" % index
		var row: Dictionary = rows[index]
		var error := _require_exact_keys(row, ARMOR_KEYS, "armorClasses[%d]" % index)
		if error != "":
			return error
		var armor_id := String(row.get("id", ""))
		if armor_id.is_empty() or armor_classes.has(armor_id) or typeof(row.get("multipliers")) != TYPE_DICTIONARY:
			return "armor class ids and multipliers are invalid"
		var multipliers: Dictionary = row["multipliers"]
		if multipliers.size() != damage_types.size():
			return "armor class %s must cover every damage type" % armor_id
		for damage_type: String in damage_types:
			if typeof(multipliers.get(damage_type)) != TYPE_DICTIONARY:
				return "armor class %s is missing %s" % [armor_id, damage_type]
			var ratio: Dictionary = multipliers[damage_type]
			error = _require_exact_keys(ratio, RATIO_KEYS, "armor ratio")
			if error != "" or not _is_integer(ratio.get("numerator")) or not _is_integer(ratio.get("denominator")) or int(ratio["numerator"]) <= 0 or int(ratio["denominator"]) <= 0:
				return "armor ratio for %s/%s is invalid" % [armor_id, damage_type]
		armor_classes[armor_id] = row.duplicate(true)
	return ""


func _parse_upgrades(rows: Array) -> String:
	for index: int in range(rows.size()):
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return "upgrades[%d] must be an object" % index
		var row: Dictionary = rows[index]
		var error := _require_exact_keys(row, UPGRADE_KEYS, "upgrades[%d]" % index)
		if error != "":
			return error
		var upgrade_id := String(row.get("id", ""))
		if upgrade_id.is_empty() or upgrades.has(upgrade_id) or String(row.get("displayName", "")).is_empty() or not damage_types.has(String(row.get("damageType", ""))):
			return "upgrade identity or damage type is invalid"
		for field: String in ["cost", "researchTicks", "damageBonusPermille", "armorBonusPermille"]:
			if not _is_integer(row.get(field)) or int(row[field]) < 0:
				return "upgrade %s field %s is invalid" % [upgrade_id, field]
		if int(row["researchTicks"]) <= 0:
			return "upgrade research duration must be positive"
		upgrades[upgrade_id] = row.duplicate(true)
	return ""


func _parse_factions(rows: Array) -> String:
	for index: int in range(rows.size()):
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return "factions[%d] must be an object" % index
		var row: Dictionary = rows[index]
		var error := _require_exact_keys(row, FACTION_KEYS, "factions[%d]" % index)
		if error != "":
			return error
		var faction_id := String(row.get("id", ""))
		if faction_id.is_empty() or factions.has(faction_id) or String(row.get("displayName", "")).is_empty() or not _is_integer(row.get("startingResources")) or int(row["startingResources"]) < 0:
			return "faction identity or resources are invalid"
		if not Color.html_is_valid(String(row.get("teamColor", ""))):
			return "faction %s has an invalid team color" % faction_id
		if typeof(row.get("upgradeIds")) != TYPE_ARRAY or typeof(row.get("roster")) != TYPE_ARRAY or Array(row["roster"]).is_empty():
			return "faction %s must declare upgrades and a roster" % faction_id
		var available: Dictionary = {}
		for raw_upgrade_id: Variant in Array(row["upgradeIds"]):
			var upgrade_id := String(raw_upgrade_id)
			if not upgrades.has(upgrade_id) or available.has(upgrade_id):
				return "faction %s has an invalid upgrade reference" % faction_id
			available[upgrade_id] = true
		var normalized_roster: Array[Dictionary] = []
		for raw_unit: Variant in Array(row["roster"]):
			if typeof(raw_unit) != TYPE_DICTIONARY:
				return "faction %s roster entry must be an object" % faction_id
			var unit_row: Dictionary = raw_unit
			error = _validate_unit(unit_row)
			if error != "":
				return error
			var unit_id := String(unit_row["unitId"])
			units[unit_id] = unit_row.duplicate(true)
			unit_factions[unit_id] = faction_id
			normalized_roster.append(unit_row.duplicate(true))
		normalized_roster.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return String(left["unitId"]) < String(right["unitId"]))
		var normalized := row.duplicate(true)
		normalized["roster"] = normalized_roster
		var upgrade_ids: Array = normalized["upgradeIds"]
		upgrade_ids.sort()
		normalized["upgradeIds"] = upgrade_ids
		factions[faction_id] = normalized
	return ""


func _validate_unit(row: Dictionary) -> String:
	var error := _require_exact_keys(row, UNIT_KEYS, "unit")
	if error != "":
		return error
	var unit_id := String(row.get("unitId", ""))
	if unit_id.is_empty() or units.has(unit_id) or String(row.get("displayName", "")).is_empty():
		return "unit identity is invalid"
	if not armor_classes.has(String(row.get("armorClass", ""))) or not damage_types.has(String(row.get("damageType", ""))):
		return "unit %s has an invalid armor or damage type" % unit_id
	for field: String in ["baseDamage", "maximumHealth", "rangeCells", "cooldownTicks"]:
		if not _is_integer(row.get(field)) or int(row[field]) <= 0:
			return "unit %s field %s must be positive" % [unit_id, field]
	if typeof(row.get("art")) != TYPE_DICTIONARY:
		return "unit %s art must be an object" % unit_id
	var art: Dictionary = row["art"]
	error = _require_exact_keys(art, ART_KEYS, "unit art")
	if error != "":
		return error
	if String(art.get("source", "")) != "generated-primitive" or not ALLOWED_SHAPES.has(String(art.get("shape", ""))) or not String(art.get("material", "")).begins_with("team-color-"):
		return "unit %s art is not legal-safe generated primitive data" % unit_id
	return ""


func _clear() -> void:
	damage_types.clear()
	armor_classes.clear()
	upgrades.clear()
	factions.clear()
	units.clear()
	unit_factions.clear()


static func _sorted_string_keys(dictionary: Dictionary) -> Array[String]:
	var result: Array[String] = []
	for key: Variant in dictionary.keys():
		result.append(String(key))
	result.sort()
	return result


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))
