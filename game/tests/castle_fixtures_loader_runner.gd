extends SceneTree
## Castle map fixtures loader proof (lane L2a).
##
## maps/<slug>/fixtures.json is the gameplay counterpart to
## object-bindings.json (design 4.1). The loader must validate it and refuse
## every malformed shape with a NAMED diagnostic — never a silent degrade to
## decoration. The valid document below mirrors the retail Erebor pins
## (EreborGateDoors 20000 HP / DefaultWallArmor / three named gate
## geometries / Player_1 owner; EBGarrisonableTower ContainMax 3).

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 26

const V2_CONTRACT := {
	"version": 2,
	"family": "retail-castle-siege-skirmish",
	"gameplayStatus": "blocked-named-gaps",
	"admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
	"required": [
		"walkable-walls",
		"defendable-gates",
		"wall-garrisons",
		"skirmish-ai-libraries",
	],
}

const GATE_FIXTURE := {
	"typeName": "EreborGateDoors",
	"role": "gate",
	"index": 2489,
	"position": [3585.2, 300.0, -3686.58],
	"angle": 2.3322623,
	"originalOwner": "Player_1/teamPlayer_1",
	"initialHealth": 100,
	"indestructible": false,
	"enabled": true,
	"targetable": false,
	"kindOf": ["STRUCTURE", "IMMOBILE", "SELECTABLE", "BLOCKING_GATE", "WALL_GATE"],
	"maxHealth": 20000.0,
	"armor": "DefaultWallArmor",
	"deathRule": "keep-object",
	"gate": {
		"openByDefault": true,
		"resetMilliseconds": 5000,
		"percentOpenForPathing": 50,
		"geometries": {
			"Closed": {"shape": "BOX", "majorRadius": 130.0, "minorRadius": 7.5, "height": 140, "offset": [0.0, 0.0, 0.0]},
			"OpenLeft": {"shape": "BOX", "majorRadius": 7.5, "minorRadius": 60.0, "height": 140, "offset": [-115.0, 68.0, 0.0]},
			"OpenRight": {"shape": "BOX", "majorRadius": 7.5, "minorRadius": 60.0, "height": 140, "offset": [115.0, 68.0, 0.0]},
		},
	},
}

const GARRISON_FIXTURE := {
	"typeName": "EBGarrisonableTower",
	"role": "garrison",
	"index": 967,
	"position": [759.58, 300.0, -1944.35],
	"angle": 0.0,
	"originalOwner": "PlyrCivilian/teamPlyrCivilian",
	"initialHealth": 100,
	"indestructible": false,
	"enabled": true,
	"targetable": false,
	"kindOf": ["STRUCTURE", "SELECTABLE", "IMMOBILE", "GARRISON"],
	"maxHealth": 1500,
	"armor": "StructureArmor",
	"garrison": {"containMax": 3, "allowOwnPlayerInsideOverride": true, "numberOfExitPaths": 1},
}

const FLAG_FIXTURE := {
	"typeName": "CaptureFlag",
	"role": "structure",
	"index": 12,
	"position": [100.0, 300.0, -100.0],
	"angle": 0.0,
	"originalOwner": "PlyrCivilian/teamPlyrCivilian",
	"kindOf": ["STRUCTURE", "IMMOBILE"],
	"maxHealth": 3000.0,
	# The recorded SAGE passthrough: no authored ArmorSet, never an invented one.
	"armor": null,
}

const VALID_DOCUMENT := {
	"schema": "openbfme.sage-map-fixtures",
	"schemaVersion": 0,
	"capabilities": [
		"walkable-walls",
		"defendable-gates",
		"wall-garrisons",
		"skirmish-ai-libraries",
	],
	"count": 3,
	"fixtures": [GATE_FIXTURE, GARRISON_FIXTURE, FLAG_FIXTURE],
	"omitted": [
		{"typeName": "EBMineCartD", "reason": "no-authored-body-maxhealth"},
	],
}

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_FIXTURES_LOADER", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _load(data, document: Variant, with_contract: bool = true) -> bool:
	if with_contract:
		data._load_castle_siege_contract(V2_CONTRACT.duplicate(true))
	return bool(data._load_fixtures(document))


func _run() -> void:
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_check("map_data_script_parses", map_data_script != null)
	if map_data_script == null:
		_finish()
		return

	# Absent document: a pre-L2a pack, loads as no fixtures.
	var plain = map_data_script.new()
	_check("absent_fixtures_loads", _load(plain, null), String(plain.error))
	_check("absent_fixtures_surface_empty", plain.map_fixtures.is_empty())

	# The valid Erebor-shaped document loads and surfaces every field.
	var valid = map_data_script.new()
	_check("valid_document_loads", _load(valid, VALID_DOCUMENT.duplicate(true)),
		String(valid.error))
	_check("valid_fixtures_surface",
		valid.map_fixtures.size() == 3
		and String(valid.map_fixtures[0]["typeName"]) == "EreborGateDoors"
		and String(valid.map_fixtures[0]["originalOwner"]) == "Player_1/teamPlayer_1",
		str(valid.map_fixtures))
	var gate_block: Dictionary = valid.map_fixtures[0].get("gate", {})
	var geometries: Dictionary = gate_block.get("geometries", {})
	_check("valid_gate_block_surfaces",
		bool(gate_block.get("openByDefault", false))
		and int(gate_block.get("resetMilliseconds", 0)) == 5000
		and geometries.has("Closed") and geometries.has("OpenLeft") and geometries.has("OpenRight"),
		str(gate_block))
	_check("valid_garrison_block_surfaces",
		int((valid.map_fixtures[1].get("garrison", {}) as Dictionary).get("containMax", 0)) == 3)
	_check("omitted_list_surfaces",
		valid.fixtures_omitted.size() == 1
		and String(valid.fixtures_omitted[0]["typeName"]) == "EBMineCartD",
		str(valid.fixtures_omitted))
	_check("null_armor_passthrough_surfaces",
		valid.map_fixtures[2].has("armor") and valid.map_fixtures[2]["armor"] == null,
		str(valid.map_fixtures[2]))

	# Every malformed shape refuses with its named diagnostic.
	var wrong_schema = map_data_script.new()
	var bad_schema := VALID_DOCUMENT.duplicate(true)
	bad_schema["schema"] = "openbfme.bogus"
	_check("wrong_schema_fails_closed",
		not _load(wrong_schema, bad_schema)
		and String(wrong_schema.error) == "unexpected cooked fixtures schema",
		String(wrong_schema.error))

	var no_contract = map_data_script.new()
	_check("fixtures_without_contract_fails_closed",
		not _load(no_contract, VALID_DOCUMENT.duplicate(true), false)
		and String(no_contract.error) == "fixtures document without a castleSiege contract",
		String(no_contract.error))

	var disagree = map_data_script.new()
	var disagree_doc := VALID_DOCUMENT.duplicate(true)
	disagree_doc["capabilities"] = ["defendable-gates", "wall-garrisons", "skirmish-ai-libraries"]
	_check("capabilities_disagree_fails_closed",
		not _load(disagree, disagree_doc)
		and String(disagree.error) == "fixtures capabilities disagree with the castleSiege contract",
		String(disagree.error))

	var bad_caps = map_data_script.new()
	var bad_caps_doc := VALID_DOCUMENT.duplicate(true)
	bad_caps_doc["capabilities"] = ["walkable-walls", "moonbeams"]
	_check("invalid_capabilities_fails_closed",
		not _load(bad_caps, bad_caps_doc)
		and String(bad_caps.error) == "invalid fixtures capability inventory",
		String(bad_caps.error))

	var bad_count = map_data_script.new()
	var bad_count_doc := VALID_DOCUMENT.duplicate(true)
	bad_count_doc["count"] = 99
	_check("count_mismatch_fails_closed",
		not _load(bad_count, bad_count_doc)
		and String(bad_count.error) == "cooked fixtures count does not match its metadata",
		String(bad_count.error))

	var empty_rows = map_data_script.new()
	var empty_doc := VALID_DOCUMENT.duplicate(true)
	empty_doc["fixtures"] = []
	empty_doc["count"] = 0
	_check("empty_fixtures_fails_closed",
		not _load(empty_rows, empty_doc)
		and String(empty_rows.error) == "cooked fixtures document has no fixture rows",
		String(empty_rows.error))

	var bad_role = map_data_script.new()
	var bad_role_doc := VALID_DOCUMENT.duplicate(true)
	bad_role_doc["fixtures"][2]["role"] = "moonbeam"
	_check("unknown_role_fails_closed",
		not _load(bad_role, bad_role_doc)
		and String(bad_role.error) == "castle fixture has an unknown role",
		String(bad_role.error))

	var dup_index = map_data_script.new()
	var dup_doc := VALID_DOCUMENT.duplicate(true)
	dup_doc["fixtures"][1]["index"] = 2489
	_check("duplicate_index_fails_closed",
		not _load(dup_index, dup_doc)
		and String(dup_index.error) == "castle fixture placement index is duplicated",
		String(dup_index.error))

	var bad_owner = map_data_script.new()
	var bad_owner_doc := VALID_DOCUMENT.duplicate(true)
	bad_owner_doc["fixtures"][0]["originalOwner"] = ""
	_check("invalid_owner_fails_closed",
		not _load(bad_owner, bad_owner_doc)
		and String(bad_owner.error) == "castle fixture has an invalid originalOwner",
		String(bad_owner.error))

	var bad_health = map_data_script.new()
	var bad_health_doc := VALID_DOCUMENT.duplicate(true)
	bad_health_doc["fixtures"][0]["maxHealth"] = 0
	_check("invalid_health_fails_closed",
		not _load(bad_health, bad_health_doc)
		and String(bad_health.error) == "castle fixture has an invalid maxHealth",
		String(bad_health.error))

	var bad_armor = map_data_script.new()
	var bad_armor_doc := VALID_DOCUMENT.duplicate(true)
	bad_armor_doc["fixtures"][0]["armor"] = ""
	_check("invalid_armor_fails_closed",
		not _load(bad_armor, bad_armor_doc)
		and String(bad_armor.error) == "castle fixture has an invalid armor",
		String(bad_armor.error))

	var gate_no_geo = map_data_script.new()
	var gate_no_geo_doc := VALID_DOCUMENT.duplicate(true)
	gate_no_geo_doc["fixtures"][0]["gate"]["geometries"] = {}
	_check("gate_without_geometries_fails_closed",
		not _load(gate_no_geo, gate_no_geo_doc)
		and String(gate_no_geo.error) == "gate fixture has invalid named geometries",
		String(gate_no_geo.error))

	var gate_bad_block = map_data_script.new()
	var gate_bad_doc := VALID_DOCUMENT.duplicate(true)
	gate_bad_doc["fixtures"][0]["gate"]["resetMilliseconds"] = 0
	_check("gate_bad_module_block_fails_closed",
		not _load(gate_bad_block, gate_bad_doc)
		and String(gate_bad_block.error) == "gate fixture has an invalid gate module block",
		String(gate_bad_block.error))

	var bad_contain = map_data_script.new()
	var bad_contain_doc := VALID_DOCUMENT.duplicate(true)
	bad_contain_doc["fixtures"][1]["garrison"]["containMax"] = 0
	_check("garrison_bad_contain_max_fails_closed",
		not _load(bad_contain, bad_contain_doc)
		and String(bad_contain.error) == "garrison fixture has an invalid containMax",
		String(bad_contain.error))

	var no_garrison = map_data_script.new()
	var no_garrison_doc := VALID_DOCUMENT.duplicate(true)
	no_garrison_doc["fixtures"][1].erase("garrison")
	_check("garrison_missing_block_fails_closed",
		not _load(no_garrison, no_garrison_doc)
		and String(no_garrison.error) == "garrison fixture is missing its garrison module block",
		String(no_garrison.error))

	var mismatch = map_data_script.new()
	var mismatch_doc := VALID_DOCUMENT.duplicate(true)
	mismatch_doc["fixtures"][2]["gate"] = GATE_FIXTURE["gate"]
	_check("role_block_mismatch_fails_closed",
		not _load(mismatch, mismatch_doc)
		and String(mismatch.error) == "castle fixture role disagrees with its module block",
		String(mismatch.error))

	var bad_omitted = map_data_script.new()
	var bad_omitted_doc := VALID_DOCUMENT.duplicate(true)
	bad_omitted_doc["omitted"] = [{"typeName": "EBMineCartD", "reason": "made-up"}]
	_check("invalid_omitted_entry_fails_closed",
		not _load(bad_omitted, bad_omitted_doc)
		and String(bad_omitted.error) == "map fixtures document has an invalid omitted entry",
		String(bad_omitted.error))

	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_FIXTURES_LOADER PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_FIXTURES_LOADER FAIL %s %s" % [name, detail])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("CASTLE_FIXTURES_LOADER FAIL liveness ran %d checks, expected %d" % [ran, EXPECTED_CHECKS])
	print("CASTLE_FIXTURES_LOADER_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
