extends SceneTree

const Manifest = preload("res://src/retail_slice/retail_faction_manifest.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var authored := _document("FactoryArmor", "")
	var no_set := _document(null, "no authored ArmorSet on the object; Manifest returns empty dict")
	var authored_table := Manifest._compiled_structure_armor(authored)
	var no_set_table := Manifest._compiled_structure_armor(no_set)
	_check("authored_armor_ini_table_compiles", authored_table.get("set_id") == "FactoryArmor")
	_check("no_set_compiles_to_empty_dict", no_set_table.is_empty())
	_check("empty_dict_causes_sim_to_record_provisional", not authored_table.is_empty())
	var sim := Sim.new()
	sim._structure_kinds.assign(["barracks", "statue"])
	sim._structure_armor = {"barracks": authored_table}
	sim._record_structure_armor_provisionals()
	_check("structure_missing_armor_table_recorded_in_provisionals", sim.structure_armor_provisional_kinds.has("statue"))
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
