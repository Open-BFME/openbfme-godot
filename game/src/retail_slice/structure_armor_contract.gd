class_name StructureArmorContract
extends RefCounted


static func scenario_runtime_kind(value: String) -> String:
	var source := value.strip_edges().to_lower()
	var normalized := ""
	for index in source.length():
		var code := source.unicode_at(index)
		if (code >= 48 and code <= 57) or (code >= 97 and code <= 122):
			normalized += String.chr(code)
		elif normalized != "" and not normalized.ends_with("_"):
			normalized += "_"
	normalized = normalized.trim_suffix("_")
	match normalized:
		"captureflag":
			return "capture_flag"
		"signalfire":
			return "signal_fire"
		_:
			return normalized


static func scenario_document_kind(document: Dictionary) -> String:
	var registration := document.get("registration", {}) as Dictionary
	var admission := registration.get("scenarioAdmission", {}) as Dictionary
	var role := String(admission.get("role", ""))
	if role == "lair":
		return "lair"
	if role != "neutral-structure":
		return ""
	var source := String(document.get("slug", ""))
	if source.strip_edges() == "":
		source = String(document.get("objectId", ""))
	return scenario_runtime_kind(source)


static func normalize_registration_armor(document: Dictionary) -> Dictionary:
	var registration := document.get("registration", {}) as Dictionary
	var gameplay := registration.get("gameplay", {}) as Dictionary
	if not gameplay.has("armor"):
		return {"present": false}
	return normalize_armor(gameplay.get("armor"))


static func normalize_armor(armor_value: Variant) -> Dictionary:
	if typeof(armor_value) != TYPE_DICTIONARY:
		return {"error": "armor is not a dictionary"}
	var armor := armor_value as Dictionary
	if not armor.has("setId"):
		return {"error": "armor has no setId"}
	var set_id_value: Variant = armor.get("setId")
	if set_id_value == null:
		var semantic := String(armor.get("semantic", "")).strip_edges()
		if semantic == "" or armor.has("table"):
			return {"error": "null setId lacks exact passthrough evidence"}
		return {"present": true, "table": {
			"set_id": "",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
			"passthrough": true,
			"semantic": semantic,
		}}
	if typeof(set_id_value) != TYPE_STRING or String(set_id_value).strip_edges() == "":
		return {"error": "setId is not a nonempty string"}
	if typeof(armor.get("table")) != TYPE_DICTIONARY:
		return {"error": "typed ArmorSet has no compiled table"}
	var table := armor.get("table") as Dictionary
	if table.has("setId") and String(table.get("setId", "")) != String(set_id_value):
		return {"error": "compiled table setId does not match armor setId"}
	for required_key in ["damageScalar", "default", "scalars"]:
		if not table.has(required_key):
			return {"error": "compiled armor table has no %s" % required_key}
	if typeof(table.get("damageScalar")) != TYPE_DICTIONARY:
		return {"error": "damageScalar is not a dictionary"}
	if typeof(table.get("default")) != TYPE_DICTIONARY:
		return {"error": "default scalar is not a dictionary"}
	if typeof(table.get("scalars")) != TYPE_DICTIONARY:
		return {"error": "scalars is not a dictionary"}
	var damage_result := _percent((table.get("damageScalar") as Dictionary).get("percent"), "damageScalar")
	if damage_result.has("error"):
		return damage_result
	var default_result := _percent((table.get("default") as Dictionary).get("percent"), "default")
	if default_result.has("error"):
		return default_result
	var default_scalar := float(default_result.value) / 100.0
	var scalars := {"default": default_scalar}
	var authored_scalars := table.get("scalars") as Dictionary
	for key_value in authored_scalars.keys():
		var row_value: Variant = authored_scalars.get(key_value)
		if typeof(row_value) != TYPE_DICTIONARY:
			return {"error": "scalar '%s' is not a dictionary" % String(key_value)}
		var scalar_result := _percent((row_value as Dictionary).get("percent"), "scalar '%s'" % String(key_value))
		if scalar_result.has("error"):
			return scalar_result
		scalars[String(key_value)] = float(scalar_result.value) / 100.0
	var compiled := {
		"set_id": String(set_id_value),
		"damage_scalar": float(damage_result.value) / 100.0,
		"scalars": scalars,
	}
	var flanked_value: Variant = table.get("flankedPenalty", table.get("flanked_penalty", null))
	if flanked_value != null:
		var raw_flanked: Variant = flanked_value
		if typeof(flanked_value) == TYPE_DICTIONARY:
			raw_flanked = (flanked_value as Dictionary).get("percent")
		var flanked_result := _percent(raw_flanked, "flankedPenalty")
		if flanked_result.has("error"):
			return flanked_result
		compiled["flanked_penalty"] = float(flanked_result.value) / 100.0
	return {"present": true, "table": compiled}


static func _percent(value: Variant, label: String) -> Dictionary:
	if typeof(value) not in [TYPE_FLOAT, TYPE_INT]:
		return {"error": "%s percent is not numeric" % label}
	var percent := float(value)
	if not is_finite(percent) or percent < 0.0:
		return {"error": "%s percent is negative or nonfinite" % label}
	return {"value": percent}
