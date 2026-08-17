extends SceneTree

const BFME2_PACK := "bfme2-neutral-vslice/ccc75c1d6e3272581f6a98ca0d8d56f4040b0ae68d14cda8c1afc6152c8819ce"
const ROTWK_PACK := "rotwk-neutral-vslice/6032e4568e34970105370ffe86dd7e88ca4b11c1350b3b1815fbd95bc0edc278"
const BFME2_CATALOG := "f44d335b6c967bb28a9bb6341f77ab5fbc4b4d84f87b37ca18194a61293c5f89"
const BFME2_PROFILE := "929570a2c4fb3b966e60c4f95fa9d6f2c86b58aa46743f8368400281ed6354c9"
const BFME2_DEPENDENCY := "66b2f853b9ffbb671d74394cbeab5d0d6e752f54c5d98c942939c646dda61dfe"
const ROTWK_CATALOG := "50bff32c12c50fd84a4e7960048dec9e62b3bd396e16395b0db2bfa37eabc10d"
const ROTWK_PROFILE := "571eaa23b654611c9b59521f2e5db6e2022cb7f3cfaa04882bf76c4d06384d1e"
const ROTWK_DEPENDENCY := "2aed05d7ede3ab396c78da77ddfdcedf5e49bd051bacaef13e670231d2fd94ec"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var db = root.get_node_or_null("ContentDB")
	if db == null:
		_fail("ContentDB autoload is missing")
		_finish()
		return
	var selected_roots: Array[String] = []
	for root_value in db.pack_roots:
		selected_roots.append(String(root_value).replace("\\", "/"))
	_check(_has_suffix(selected_roots, BFME2_PACK), "selected graph pins exact sealed BFME2 neutral pack")
	_check(_has_suffix(selected_roots, ROTWK_PACK), "selected graph pins exact sealed RotWK neutral pack")

	var receipts: Dictionary = db.get_neutral_pack_receipts()
	_check(receipts.size() == 2 and receipts.has("bfme2") and receipts.has("rotwk"), "exactly two edition receipts are indexed")
	var bfme2 := db.get_neutral_pack_receipt("bfme2") as Dictionary
	var rotwk := db.get_neutral_pack_receipt("rotwk") as Dictionary
	_check(_receipt_identity(bfme2, BFME2_CATALOG, BFME2_PROFILE, BFME2_DEPENDENCY, 73, 2, BFME2_PACK), "BFME2 receipt binds selected pack and sealed identities")
	_check(_receipt_identity(rotwk, ROTWK_CATALOG, ROTWK_PROFILE, ROTWK_DEPENDENCY, 88, 1, ROTWK_PACK), "RotWK receipt binds selected pack and sealed identities")
	var rotwk_meta := _pack_meta(db.pack_meta, "rotwk-neutral-vslice", ROTWK_PACK)
	var receipt_unsigned := rotwk.duplicate(true)
	for metadata_key in ["_source", "_pack_root", "_pack_file_key"]:
		receipt_unsigned.erase(metadata_key)
	_check(not rotwk_meta.is_empty() and db._validate_neutral_pack_receipt(receipt_unsigned, rotwk_meta.get("files", {}) as Dictionary, rotwk_meta), "selected RotWK receipt satisfies runtime schema and declared binding validation")
	var tampered_receipt := receipt_unsigned.duplicate(true)
	tampered_receipt.catalogSha256 = "0".repeat(64)
	_check(not db._validate_neutral_pack_receipt(tampered_receipt, rotwk_meta.get("files", {}) as Dictionary, rotwk_meta), "receipt validator rejects catalog identity drift")
	# Receipt cardinality is descriptor-driven, not a frozen retail census. A
	# newly compiled map-rooted structure must validate through the same strict
	# row/path/hash envelope without teaching ContentDB a new magic total.
	var expanded_receipt := receipt_unsigned.duplicate(true)
	var expanded_rows := expanded_receipt.get("rows", []) as Array
	var fixture_row := (expanded_rows[0] as Dictionary).duplicate(true)
	fixture_row["objectId"] = "FixtureMapStructure"
	fixture_row["runtimeDomain"] = "structure"
	fixture_row["packFileKey"] = "playableStructure.fixture-map-structure"
	fixture_row["runtimePath"] = "data/playable-structures/fixture-map-structure.json"
	expanded_rows.append(fixture_row)
	expanded_receipt["rows"] = expanded_rows
	expanded_receipt["objectCount"] = expanded_rows.size()
	expanded_receipt["receiptSha256"] = "1".repeat(64)
	var expanded_declared := (rotwk_meta.get("files", {}) as Dictionary).duplicate(true)
	expanded_declared[fixture_row["packFileKey"]] = fixture_row["runtimePath"]
	var expanded_meta := rotwk_meta.duplicate(true)
	expanded_meta["neutralProfileReceiptSha256"] = expanded_receipt["receiptSha256"]
	_check(db._validate_neutral_pack_receipt(expanded_receipt, expanded_declared, expanded_meta), "receipt validator accepts a bounded descriptor-driven structure count")
	var mismatched_count := expanded_receipt.duplicate(true)
	mismatched_count["objectCount"] = int(expanded_receipt["objectCount"]) + 1
	_check(not db._validate_neutral_pack_receipt(mismatched_count, expanded_declared, expanded_meta), "receipt validator rejects a declared object-count mismatch")

	var units: Dictionary = db.get_scenario_unit_runtimes("rotwk")
	var structures: Dictionary = db.get_scenario_structure_runtimes("rotwk")
	var props: Dictionary = db.get_scenario_prop_runtimes("rotwk")
	var pickups: Dictionary = db.get_scenario_pickup_runtimes("rotwk")
	var bfme2_units: Dictionary = db.get_scenario_unit_runtimes("bfme2")
	var bfme2_structures: Dictionary = db.get_scenario_structure_runtimes("bfme2")
	var bfme2_props: Dictionary = db.get_scenario_prop_runtimes("bfme2")
	var bfme2_pickups: Dictionary = db.get_scenario_pickup_runtimes("bfme2")
	_check(units.size() == 48 and structures.size() == 28 and props.size() == 12 and pickups.size() == 1, "live RotWK domain counts are 48/28/12/1")
	_check(bfme2_units.size() == 42 and bfme2_structures.size() == 19 and bfme2_props.size() == 12 and bfme2_pickups.size() == 2, "live BFME2 domain counts are 42/19/12/2")

	var expected := _receipt_domain_ids(rotwk)
	_check(_folded_ids(units) == expected.unit, "unit registry identities exactly match RotWK receipt")
	_check(_folded_ids(structures) == expected.structure, "structure registry identities exactly match RotWK receipt")
	_check(_folded_ids(props) == expected.prop, "prop registry identities exactly match RotWK receipt")
	_check(_folded_ids(pickups) == expected.pickup, "pickup registry identities exactly match RotWK receipt")
	var bfme2_expected := _receipt_domain_ids(bfme2)
	_check(_folded_ids(bfme2_units) == bfme2_expected.unit, "unit registry identities exactly match BFME2 receipt")
	_check(_folded_ids(bfme2_structures) == bfme2_expected.structure, "structure registry identities exactly match BFME2 receipt")
	_check(_folded_ids(bfme2_props) == bfme2_expected.prop, "prop registry identities exactly match BFME2 receipt")
	_check(_folded_ids(bfme2_pickups) == bfme2_expected.pickup, "pickup registry identities exactly match BFME2 receipt")
	var all_ids: Dictionary = {}
	var duplicate := false
	for registry in [units, structures, props, pickups]:
		for object_id_value in registry.keys():
			var folded := String(object_id_value).to_lower()
			if all_ids.has(folded):
				duplicate = true
			all_ids[folded] = true
	_check(not duplicate and all_ids.size() == 89, "scenario registries have no duplicate or cross-domain identity")
	var shared := 0
	var bfme2_all := _union_ids([bfme2_units, bfme2_structures, bfme2_props, bfme2_pickups])
	for folded_value in all_ids.keys():
		if bfme2_all.has(folded_value): shared += 1
	_check(shared == 70, "edition registries expose the expected 70-identity intersection")

	var receipt_rows := _receipt_rows_by_id(rotwk)
	var provenance_ok := true
	var provenance_mismatches: Array[String] = []
	for registry in [units, structures, props, pickups]:
		for object_id_value in registry.keys():
			var object_id := String(object_id_value)
			var document := registry[object_id_value] as Dictionary
			var receipt_row := receipt_rows.get(object_id.to_lower(), {}) as Dictionary
			if (
				receipt_row.is_empty()
				or not String(document.get("_pack_root", "")).replace("\\", "/").ends_with(ROTWK_PACK)
				or String(document.get("_pack_file_key", "")) != String(receipt_row.get("packFileKey", ""))
				or (
					String(receipt_row.get("runtimeDomain", "")) != "prop"
					and String(document.get("descriptorSha256", "")) != String(receipt_row.get("descriptorSha256", ""))
				)
			):
				provenance_ok = false
				provenance_mismatches.append("%s:%s:%s:%s" % [object_id, String(document.get("_pack_file_key", "")), String(document.get("descriptorSha256", "")), String(receipt_row.get("descriptorSha256", ""))])
	_check(provenance_ok, "every live scenario row is the RotWK receipt-bound runtime mismatches=%s" % ";".join(provenance_mismatches.slice(0, 3)))
	_check(_edition_provenance_ok([bfme2_units, bfme2_structures, bfme2_props, bfme2_pickups], bfme2, BFME2_PACK), "every BFME2 row retains BFME2 receipt provenance")
	var bfme2_chest: Dictionary = db.get_scenario_pickup_runtime("bfme2", "TreasureChest1", "object-creation-list")
	var rotwk_chest: Dictionary = db.get_scenario_pickup_runtime("rotwk", "TreasureChest1", "object-creation-list")
	_check(not bfme2_chest.is_empty() and not rotwk_chest.is_empty() and String(bfme2_chest.get("runtimeSha256", "")) != String(rotwk_chest.get("runtimeSha256", "")), "shared TreasureChest1 retains distinct edition runtime bytes")
	_check(db.get_scenario_unit_runtime("invalid", "NeutralWarg", "lair-spawn").is_empty(), "invalid edition fails closed without fallback")

	for representative in ["AngmarOrcWarriors_Placed", "DireWolfLair", "BarrowWight", "CaveTrollLair", "SpiderWebs01", "TreasureChest1"]:
		var document := _scenario_document([units, structures, props, pickups], representative)
		_check(not document.is_empty() and String(document.get("_pack_root", "")).replace("\\", "/").ends_with(ROTWK_PACK), "representative %s resolves RotWK provenance" % representative)

	var no_production_leak := true
	var cross_edition_ids := all_ids.duplicate()
	for value in bfme2_all.keys(): cross_edition_ids[value] = true
	for object_id_value in cross_edition_ids.keys():
		var object_id := String(object_id_value)
		if (
			not db.get_playable_unit_runtime(object_id).is_empty()
			or not db.get_playable_structure_runtime(object_id).is_empty()
			or db.playable_unit_runtime_member_index.has(object_id)
		):
			no_production_leak = false
	_check(no_production_leak, "neutral identities do not enter playable unit, structure or member registries")
	_check(db.get_playable_faction_ids().size() == 7, "neutral packs do not create a faction or HUD roster")

	# Re-admitting an already-selected edition begins an atomic replacement
	# window. Its old rows and cached snapshots must disappear immediately, while
	# the other edition remains live and provenance-addressable.
	var rotwk_root := String(rotwk.get("_pack_root", ""))
	_check(db._load_neutral_pack_receipt(rotwk_root, rotwk_meta.get("files", {}) as Dictionary, rotwk_meta), "same-edition receipt replacement is accepted")
	_check(
		db.get_scenario_unit_runtimes("rotwk").is_empty()
		and db.get_scenario_structure_runtimes("rotwk").is_empty()
		and db.get_scenario_prop_runtimes("rotwk").is_empty()
		and db.get_scenario_pickup_runtimes("rotwk").is_empty()
		and db.get_scenario_unit_runtimes("bfme2").size() == 42
		and db.get_scenario_structure_runtimes("bfme2").size() == 19
		and db.get_scenario_prop_runtimes("bfme2").size() == 12
		and db.get_scenario_pickup_runtimes("bfme2").size() == 2,
		"same-edition replacement invalidates only RotWK snapshots and preserves BFME2",
	)
	_finish()


func _receipt_identity(receipt: Dictionary, catalog: String, profile: String, dependency: String, objects: int, pickups: int, pack_suffix: String) -> bool:
	return (
		String(receipt.get("catalogSha256", "")) == catalog
		and String(receipt.get("receiptSha256", "")) == profile
		and String(receipt.get("dependencyArtifactSha256", "")) == dependency
		and int(receipt.get("objectCount", -1)) == objects
		and int(receipt.get("dependencyObjectCount", -1)) == pickups
		and String(receipt.get("_pack_root", "")).replace("\\", "/").ends_with(pack_suffix)
	)


func _receipt_domain_ids(receipt: Dictionary) -> Dictionary:
	var result := {"unit": {}, "structure": {}, "prop": {}, "pickup": {}}
	for row_value in receipt.get("rows", []) as Array:
		var row := row_value as Dictionary
		(result[String(row.get("runtimeDomain", ""))] as Dictionary)[String(row.get("objectId", "")).to_lower()] = true
	for row_value in receipt.get("dependencyRows", []) as Array:
		var row := row_value as Dictionary
		(result.pickup as Dictionary)[String(row.get("objectId", "")).to_lower()] = true
	return result


func _receipt_rows_by_id(receipt: Dictionary) -> Dictionary:
	var result := {}
	for field in ["rows", "dependencyRows"]:
		for row_value in receipt.get(field, []) as Array:
			var row := row_value as Dictionary
			result[String(row.get("objectId", "")).to_lower()] = row
	return result


func _folded_ids(registry: Dictionary) -> Dictionary:
	var result := {}
	for value in registry.keys():
		result[String(value).to_lower()] = true
	return result


func _union_ids(registries: Array) -> Dictionary:
	var result := {}
	for registry_value in registries:
		for object_id_value in (registry_value as Dictionary).keys():
			result[String(object_id_value).to_lower()] = true
	return result


func _edition_provenance_ok(registries: Array, receipt: Dictionary, pack_suffix: String) -> bool:
	var rows := _receipt_rows_by_id(receipt)
	for registry_value in registries:
		for object_id_value in (registry_value as Dictionary).keys():
			var document := (registry_value as Dictionary)[object_id_value] as Dictionary
			var row := rows.get(String(object_id_value).to_lower(), {}) as Dictionary
			if row.is_empty() or not String(document.get("_pack_root", "")).replace("\\", "/").ends_with(pack_suffix):
				return false
			if String(document.get("_pack_file_key", "")) != String(row.get("packFileKey", "")):
				return false
	return true


func _scenario_document(registries: Array, object_id: String) -> Dictionary:
	for registry_value in registries:
		var registry := registry_value as Dictionary
		for key_value in registry.keys():
			if String(key_value).to_lower() == object_id.to_lower():
				return (registry[key_value] as Dictionary).duplicate(true)
	return {}


func _has_suffix(values: Array[String], suffix: String) -> bool:
	for value in values:
		if value.ends_with(suffix):
			return true
	return false


func _pack_meta(rows: Array, pack_id: String, suffix: String) -> Dictionary:
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		if String(row.get("id", "")) == pack_id and String(row.get("root", "")).replace("\\", "/").ends_with(suffix):
			return row
	return {}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("SELECTED_NEUTRAL_PACK_ACCEPTANCE_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SELECTED_NEUTRAL_PACK_ACCEPTANCE_OK passed=%d" % passed)
		quit(0)
	else:
		print("SELECTED_NEUTRAL_PACK_ACCEPTANCE_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)
