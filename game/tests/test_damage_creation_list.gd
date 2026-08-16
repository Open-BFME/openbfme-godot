extends SceneTree

const DamageCreation = preload("res://src/retail_slice/damage_creation.gd")

var passed := 0
var failed := 0
var spawned: Array[Dictionary] = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var rows := [{
		"objectCreationListId": "OCL_FixtureChunks",
		"damageType": "CATAPULT_ROCK",
		"modelCondition": "FRONT_DESTROYED",
		"createObjects": [{"objectNames": ["FixtureChunk"], "count": 2}],
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 12,
	}]
	var result := DamageCreation.spawn_on_death(
		rows, "CATAPULT_ROCK", ["FRONT_DESTROYED"], Vector2(4, 9), Callable(self, "_spawn")
	)
	_check("DamageCreationList_matches_damage_and_model_condition", result.get("matchedRows") == 1)
	_check("destruction_spawns_authored_debris_count", spawned.size() == 2)
	_check("spawned_debris_keeps_authored_object_identity", spawned.all(func(row): return row.object_id == "FixtureChunk"))
	_check("spawned_debris_inherits_parent_death_position", spawned.all(func(row): return row.position == Vector2(4, 9)))
	var skipped := DamageCreation.spawn_on_death(rows, "FLAME", ["FRONT_DESTROYED"], Vector2.ZERO, Callable(self, "_spawn"))
	_check("wrong_damage_type_spawns_nothing", skipped.get("spawnedObjects") == 0 and spawned.size() == 2)
	print("DAMAGE_CREATION_LIST_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _spawn(object_id: String, position: Vector2, source: Dictionary) -> void:
	spawned.append({"object_id": object_id, "position": position, "source": source})


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("DAMAGE_CREATION_LIST FAIL: %s" % label)
