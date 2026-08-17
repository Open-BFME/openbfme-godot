extends SceneTree

const Manifest = preload("res://src/retail_slice/retail_faction_manifest.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var authored := _document("FactoryArmor", "")
	var no_set := _document(null, "no authored ArmorSet on the object; SAGE applies unmodified damage")
	var authored_table := Manifest._compiled_structure_armor(authored)
	var no_set_table := Manifest._compiled_structure_armor(no_set)
	var missing_set_id := Manifest._compiled_structure_armor({"registration": {"gameplay": {"armor": {"semantic": "missing key"}}}})
	var empty_set_id := Manifest._compiled_structure_armor(_document("", "empty id is malformed"))
	var empty_semantic := Manifest._compiled_structure_armor(_document(null, ""))
	var contradictory_null := _document(null, "claims passthrough")
	(((contradictory_null.registration as Dictionary).gameplay as Dictionary).armor as Dictionary)["table"] = {"default": {"percent": 20.0}}
	var contradictory_table := Manifest._compiled_structure_armor(contradictory_null)
	var wrong_type_set_id := Manifest._compiled_structure_armor(_document(7, "wrong type"))
	_check("authored_armor_ini_table_compiles", authored_table.get("set_id") == "FactoryArmor")
	_check(
		"explicit_null_set_compiles_unmodified_damage",
		String(no_set_table.get("set_id", "missing")) == ""
		and float(no_set_table.get("damage_scalar", 0.0)) == 1.0
		and (no_set_table.get("scalars", {}) as Dictionary) == {"default": 1.0},
	)
	_check("missing_set_id_fails_closed", missing_set_id.is_empty())
	_check("empty_set_id_fails_closed", empty_set_id.is_empty())
	_check("null_set_requires_semantic", empty_semantic.is_empty())
	_check("null_set_rejects_contradictory_table", contradictory_table.is_empty())
	_check("wrong_type_set_id_fails_closed", wrong_type_set_id.is_empty())
	var sim := Sim.new()
	sim._structure_kinds.assign(["barracks", "statue"])
	sim._structure_armor = {"barracks": authored_table, "statue": no_set_table}
	sim._record_structure_armor_provisionals()
	_check("explicit_null_structure_is_not_provisional", not sim.structure_armor_provisional_kinds.has("statue"))
	_check("structure_with_table_not_in_provisionals", not sim.structure_armor_provisional_kinds.has("barracks"))
	print("STRUCTURE_ARMOR_TABLES_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document(set_id: Variant, semantic: String) -> Dictionary:
	var armor := {"setId": set_id, "semantic": semantic}
	if set_id != null:
		armor["table"] = {"damageScalar": {"percent": 100.0}, "default": {"percent": 25.0}, "scalars": {"slash": {"percent": 20.0}}}
	return {"registration": {"gameplay": {"armor": armor}}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("STRUCTURE_ARMOR_TABLES FAIL: %s" % label)
