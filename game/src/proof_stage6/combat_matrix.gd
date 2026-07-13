class_name Stage6CombatMatrix
extends RefCounted
## Integer-only damage/armor resolution with researched faction modifiers.

var catalog: RefCounted


func _init(p_catalog: RefCounted) -> void:
	catalog = p_catalog


func resolve(attacker_unit_id: String, defender_unit_id: String, attacker_upgrades: Array, defender_upgrades: Array) -> Dictionary:
	var attacker: Dictionary = catalog.unit(attacker_unit_id)
	var defender: Dictionary = catalog.unit(defender_unit_id)
	if attacker.is_empty() or defender.is_empty():
		return {"ok": false, "reason": "unknown_unit", "damage": 0}
	var armor: Dictionary = catalog.armor(String(defender["armorClass"]))
	var ratio: Dictionary = Dictionary(armor.get("multipliers", {})).get(String(attacker["damageType"]), {})
	if ratio.is_empty():
		return {"ok": false, "reason": "missing_matrix_entry", "damage": 0}
	var damage_bonus: int = _damage_bonus(String(attacker["damageType"]), attacker_upgrades)
	var armor_bonus: int = _armor_bonus(defender_upgrades)
	var numerator: int = int(attacker["baseDamage"]) * int(ratio["numerator"]) * (1000 + damage_bonus) * 1000
	var denominator: int = int(ratio["denominator"]) * 1000 * (1000 + armor_bonus)
	var damage: int = maxi(1, numerator / denominator)
	return {
		"ok": true,
		"reason": "",
		"damage": damage,
		"base_damage": int(attacker["baseDamage"]),
		"damage_type": String(attacker["damageType"]),
		"armor_class": String(defender["armorClass"]),
		"matrix_numerator": int(ratio["numerator"]),
		"matrix_denominator": int(ratio["denominator"]),
		"damage_bonus_permille": damage_bonus,
		"armor_bonus_permille": armor_bonus,
	}


func _damage_bonus(damage_type: String, completed: Array) -> int:
	var result: int = 0
	for raw_id: Variant in completed:
		var definition: Dictionary = catalog.upgrade(String(raw_id))
		if String(definition.get("damageType", "")) == damage_type:
			result += int(definition.get("damageBonusPermille", 0))
	return result


func _armor_bonus(completed: Array) -> int:
	var result: int = 0
	for raw_id: Variant in completed:
		result += int(catalog.upgrade(String(raw_id)).get("armorBonusPermille", 0))
	return result
