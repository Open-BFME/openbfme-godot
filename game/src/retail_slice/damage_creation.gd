class_name RetailDamageCreation
extends RefCounted

## Executes the repeated Body.DamageCreationList rows carried by a compiled
## object descriptor. Every selected row resolves to converted OCL leaves;
## unresolved or malformed leaves fail closed instead of inventing debris.


static func spawn_on_death(
	rows: Array,
	damage_type: String,
	model_conditions: Array,
	position: Vector2,
	spawn: Callable
) -> Dictionary:
	if not spawn.is_valid():
		return {"ok": false, "error": "damage creation has no spawn consumer", "matchedRows": 0, "spawnedObjects": 0}
	var matched := 0
	var spawned := 0
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return {"ok": false, "error": "DamageCreationList row is not a dictionary", "matchedRows": matched, "spawnedObjects": spawned}
		var row := row_value as Dictionary
		if not _valid_row(row):
			return {"ok": false, "error": "DamageCreationList row is malformed", "matchedRows": matched, "spawnedObjects": spawned}
		if String(row.damageType).to_upper() != damage_type.to_upper():
			continue
		var condition := String(row.get("modelCondition", ""))
		if condition != "" and not model_conditions.has(condition):
			continue
		matched += 1
		for create_value in row.createObjects as Array:
			var create := create_value as Dictionary
			for object_name_value in create.objectNames as Array:
				for _index in range(int(create.count)):
					spawn.call(String(object_name_value), position, row)
					spawned += 1
	return {"ok": true, "matchedRows": matched, "spawnedObjects": spawned}


static func _valid_row(row: Dictionary) -> bool:
	if String(row.get("objectCreationListId", "")) == "" or String(row.get("damageType", "")) == "":
		return false
	var creates: Variant = row.get("createObjects")
	if typeof(creates) != TYPE_ARRAY or (creates as Array).is_empty():
		return false
	for create_value in creates as Array:
		if typeof(create_value) != TYPE_DICTIONARY:
			return false
		var create := create_value as Dictionary
		if typeof(create.get("objectNames")) != TYPE_ARRAY or (create.objectNames as Array).is_empty() or int(create.get("count", 0)) <= 0:
			return false
		for name_value in create.objectNames as Array:
			if typeof(name_value) != TYPE_STRING or String(name_value) == "":
				return false
	return true
