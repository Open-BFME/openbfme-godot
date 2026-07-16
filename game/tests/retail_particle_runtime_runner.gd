extends SceneTree
## Legal-safe focused gate for the fail-closed Fords particle runtime handoff.

const UNRESOLVED_IDS: Array[String] = [
	"BuildingContructDust", "PCTMediumDust", "RDTMediumExplosion",
	"RDTMediumExplosionLight", "SmokeBuildingLarge", "SmokeBuildingMediumRubble",
	"FireBuildingLarge", "FireBuildingMedium", "FireBuildingSmall",
	"SmokeBuildingMedium",
]
const DUPLICATE_IDS: Array[String] = [
	"BuildingContructDust", "PCTMediumDust", "RDTMediumExplosion",
	"RDTMediumExplosionLight", "SmokeBuildingLarge", "SmokeBuildingMediumRubble",
	"WaterRipplesSmall",
	"FireBuildingLarge", "FireBuildingMedium", "FireBuildingSmall",
	"SmokeBuildingMedium",
]
const TEXTURE_IDS: Array[String] = [
	"fords-particle-texture-excloud01-08329be62e65",
	"fords-particle-texture-execlipseblur-596e97b4470f",
	"fords-particle-texture-exexplo01-ab1f36b65adf",
	"fords-particle-texture-exfireembr-9fea2e1cc870",
	"fords-particle-texture-exgimliaxespecial-79eb6afd4f53",
	"fords-particle-texture-exshockwav-d1243f3478af",
	"fords-particle-texture-exsmokeplume-e65a73daad65",
	"fords-particle-texture-exsmokeplume3-c83e29d59206",
	"fords-particle-texture-exsmokepuf07-d8a205342cd7",
	"fords-particle-texture-excloud06hires-e907c5e5ea99",
	"fords-particle-texture-exexplo03-b65da601b095",
	"fords-particle-texture-exfire01-bfa9bd53f3f3",
	"fords-particle-texture-exfirescroll3-2ee911cd1b32",
	"fords-particle-texture-exwater01-c5ed9ebf0bd0",
	"fords-particle-texture-exwater04-f8c5bf5ab482",
]
const DEFINITION_ROWS := [
	["BuildingContructDust", "FXParticleSystem", "5109847b2768", 6],
	["BuildingContructDust", "ParticleSystem", "e9188913b1c2", 8],
	["BuildingDamaged", "FXParticleSystem", "07539a553329", 7],
	["PCTMediumDust", "FXParticleSystem", "372676128d3e", 6],
	["PCTMediumDust", "ParticleSystem", "83ee06f5abab", 0],
	["RDTMediumExplosion", "FXParticleSystem", "48968321d2fa", 3],
	["RDTMediumExplosion", "ParticleSystem", "13153c94a1e4", 3],
	["RDTMediumExplosionLight", "FXParticleSystem", "f3319cede367", 2],
	["RDTMediumExplosionLight", "ParticleSystem", "d74643b3e571", 2],
	["SmokeBuildingLarge", "FXParticleSystem", "9cef24efd8a9", 6],
	["SmokeBuildingLarge", "ParticleSystem", "164ec54a6582", 0],
	["SmokeBuildingMediumRubble", "FXParticleSystem", "5084deb21118", 6],
	["SmokeBuildingMediumRubble", "ParticleSystem", "9db357e9f0e1", 0],
	["UntamedAllegiance", "FXParticleSystem", "5f8bf92c92bd", 1],
	["UntamedAllegiance2", "FXParticleSystem", "c56a4cbcc8dd", 4],
	["WaterRipplesSmall", "FXParticleSystem", "875373f7d4aa", 5],
	["WaterRipplesSmall", "ParticleSystem", "c2399c663e48", 5],
	["BuildingContructDustCastles", "FXParticleSystem", "4e871e052307", 6],
	["BuildingDamagedBig", "FXParticleSystem", "dc51a5a83d2e", 7],
	["FireBuildingLarge", "ParticleSystem", "e7b2e7e5dcb0", 11],
	["FireBuildingLarge", "FXParticleSystem", "d2a4d6096324", 12],
	["FireBuildingMedium", "ParticleSystem", "932191ad2573", 11],
	["FireBuildingMedium", "FXParticleSystem", "9aeb28d9a451", 12],
	["FireBuildingSmall", "ParticleSystem", "1fb86d5c18d2", 11],
	["FireBuildingSmall", "FXParticleSystem", "c4bddc444758", 12],
	["FortressExplosion", "FXParticleSystem", "c85037c54c48", 10],
	["FueltheFiresEmbers", "FXParticleSystem", "b14d31d995a4", 4],
	["MenFortressProxy", "FXParticleSystem", "5c8c0073dc6d", 13],
	["MenFortressSpray", "FXParticleSystem", "530e479ed9a5", 14],
	["MenFortressSpray02", "FXParticleSystem", "2b3c8ac16e01", 13],
	["MenFortressSteam", "FXParticleSystem", "4716d073d6f0", 9],
	["PCTFortressDust", "FXParticleSystem", "766aa9b0c9ab", 6],
	["SmokeBuildingMedium", "ParticleSystem", "a4758ea83eed", 0],
	["SmokeBuildingMedium", "FXParticleSystem", "761140ad018f", 0],
	["trollCageDust", "FXParticleSystem", "f70907b15a1f", 8],
]

var passed := 0
var failed := 0
var fixture_root := ""
var controller_script


func _initialize() -> void:
	create_timer(20.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	controller_script = load("res://src/retail_slice/retail_particle_controller.gd")
	_check("controller_script_compiles", controller_script != null)
	if controller_script == null:
		_finish()
		return
	fixture_root = ProjectSettings.globalize_path("user://openbfme-particle-runtime-fixture")
	_cleanup_fixture()
	_check("fixture_directory_created", DirAccess.make_dir_recursive_absolute(fixture_root) == OK)
	var document := _contract_document()
	_check("fixture_closure_written", _write_fixture(document))

	var presentation := Node3D.new()
	presentation.name = "ParticlePresentationFixture"
	root.add_child(presentation)
	var controller = controller_script.new()
	root.add_child(controller)
	_check(
		"exact_contract_configures",
		bool(controller.configure_from_pack(
			fixture_root,
			fixture_root.path_join("maps/fords-of-isen-ii"),
			0.1,
			Callable(self, "_source_to_local"),
			presentation
		)),
		String(controller.error)
	)
	_check("contract_and_presentation_are_ready", bool(controller.contract_declared) and bool(controller.contract_ready) and bool(controller.presentation_ready))
	_check("all_private_definition_and_texture_leaves_validated", int(controller.validated_definition_count) == 35 and int(controller.validated_texture_count) == 15)
	_check("only_explicit_provisional_selection_is_enabled", int(controller.provisional_runtime_selection_count) == 1 and int(controller.instantiated_emitter_count) == 7)
	_check("ten_cross_family_collisions_remain_unselected", int(controller.unresolved_family_selection_count) == 10 and _has_diagnostic(controller.diagnostics, "cross-family-precedence-unresolved"))
	_check("provisional_render_is_not_claimed_as_parity", not bool(controller.parity_ready) and _has_diagnostic(controller.diagnostics, "water-ripple-render-translation-provisional"))
	_check("source_indices_are_deterministic", controller.source_placement_indices == [11, 18, 25, 32, 39, 46, 53], str(controller.source_placement_indices))
	var ripple_container := presentation.get_node_or_null("RetailWaterRipples")
	_check("seven_visible_gpu_emitters_instantiated", ripple_container != null and ripple_container.get_child_count() == 7 and _gpu_emitter_count(ripple_container) == 7)
	if ripple_container != null and ripple_container.get_child_count() == 7:
		var first := ripple_container.get_child(0) as GPUParticles3D
		_check("retail_selection_metadata_is_explicit", String(first.get_meta("particle_system_id", "")) == "WaterRipplesSmall" and String(first.get_meta("selected_kind", "")) == "FXParticleSystem" and String(first.get_meta("selection_status", "")) == "provisional-explicit-runtime-selection")
		_check("source_anchor_offset_is_applied_before_map_transform", first.position.is_equal_approx(Vector3(0.23329276, 0.2, 0.23090822)), str(first.position))
	else:
		_check("retail_selection_metadata_is_explicit", false)
		_check("source_anchor_offset_is_applied_before_map_transform", false)

	var corrupted := document.duplicate(true)
	var corrupted_binding: Dictionary = (corrupted.objectBindings as Array)[3]
	var corrupted_resolution: Dictionary = (((corrupted_binding.systems as Array)[0] as Dictionary).familyResolution as Dictionary)
	corrupted_resolution["generalizesToOtherDuplicateIdentifiers"] = true
	var corrupt_controller = controller_script.new()
	root.add_child(corrupt_controller)
	_check(
		"provisional_selection_cannot_be_generalized",
		not bool(corrupt_controller.configure_document(
			corrupted, fixture_root, fixture_root.path_join("maps/fords-of-isen-ii"),
			0.1, Callable(self, "_source_to_local"), presentation
		)) and String(corrupt_controller.error).contains("provisional"),
		String(corrupt_controller.error)
	)

	var unresolved_selected := document.duplicate(true)
	var cave: Dictionary = (unresolved_selected.objectBindings as Array)[0]
	var pct: Dictionary = (cave.systems as Array)[1]
	(pct.familyResolution as Dictionary)["selectedKind"] = "FXParticleSystem"
	var unresolved_controller = controller_script.new()
	root.add_child(unresolved_controller)
	_check(
		"unresolved_family_cannot_be_silently_selected",
		not bool(unresolved_controller.configure_document(
			unresolved_selected, fixture_root, fixture_root.path_join("maps/fords-of-isen-ii"),
			0.1, Callable(self, "_source_to_local"), presentation
		)) and String(unresolved_controller.error).contains("silently selected"),
		String(unresolved_controller.error)
	)

	var missing_texture_path := fixture_root.path_join("assets/textures/effects/%s.png" % TEXTURE_IDS[0])
	_check("fixture_texture_removed", DirAccess.remove_absolute(missing_texture_path) == OK)
	var missing_texture_controller = controller_script.new()
	root.add_child(missing_texture_controller)
	_check(
		"missing_private_texture_fails_closed",
		not bool(missing_texture_controller.configure_document(
			document, fixture_root, fixture_root.path_join("maps/fords-of-isen-ii"),
			0.1, Callable(self, "_source_to_local"), presentation
		)) and String(missing_texture_controller.error).contains("texture is missing"),
		String(missing_texture_controller.error)
	)

	print("RETAIL_PARTICLE_RUNTIME_METRICS definitions=%d textures=%d provisional=%d unresolved=%d emitters=%d parity_ready=%s diagnostics=%d" % [
		controller.validated_definition_count,
		controller.validated_texture_count,
		controller.provisional_runtime_selection_count,
		controller.unresolved_family_selection_count,
		controller.instantiated_emitter_count,
		str(controller.parity_ready),
		controller.diagnostics.size(),
	])
	controller.queue_free()
	corrupt_controller.queue_free()
	unresolved_controller.queue_free()
	missing_texture_controller.queue_free()
	presentation.queue_free()
	await process_frame
	await process_frame
	_cleanup_fixture()
	_finish()


func _contract_document() -> Dictionary:
	var registry: Array[Dictionary] = []
	for row_value in DEFINITION_ROWS:
		var row: Array = row_value
		var name := String(row[0])
		var kind := String(row[1])
		var suffix := String(row[2])
		var texture_id := TEXTURE_IDS[int(row[3])]
		var slug := name.to_lower()
		var resource_id := "fords-particle-def-%s-%s" % [slug, suffix]
		registry.append({
			"definitionId": name,
			"kind": kind,
			"definitionResourceId": resource_id,
			"definitionOutputJson": "effects/particles/definitions/%s.json" % resource_id,
			"sourceBlockSha256": resource_id.sha256_text(),
			"particleNameIds": ["Fixture.tga"],
			"textureResourceIds": [texture_id],
		})
	var provisional := {
		"particleSystemId": "WaterRipplesSmall",
		"selectedKind": "FXParticleSystem",
		"status": "provisional-explicit-runtime-selection",
		"crossFamilyPrecedenceProven": false,
		"generalizesToOtherDuplicateIdentifiers": false,
		"visibleFieldsMateriallyEquivalent": true,
		"materialDiscriminator": "priority/culling",
		"oracleAggregateSha256": "5dacb5477f89ba3dfcfa0b3450ade12fe7ebd79e4c7221463efd44e390108905",
	}
	return {
		"schema": "openbfme.fords-particle-bindings",
		"schemaVersion": 0,
		"sourceCensusAggregateSha256": "fixture-census".sha256_text(),
		"definitionRegistry": registry,
		"familyResolution": {
			"status": "provisional-selection-with-cross-family-precedence-unresolved",
			"noGeneralPrecedenceRule": true,
			"duplicateIdentifierSystemIds": DUPLICATE_IDS.duplicate(),
			"unresolvedDuplicateIdentifierSystemIds": UNRESOLVED_IDS.duplicate(),
			"provisionalRuntimeSelections": [provisional.duplicate(true)],
		},
		"fxLists": [{}, {}, {}, {}, {}, {}, {}, {}],
		"objectBindings": [
			_structure_binding("CaveTrollLair", 2, true),
			_inn_binding(),
			_structure_binding("WargLair", 4, true),
			{
				"typeName": "WtrRiplsSmall",
				"placementCount": 7,
				"anchor": {
					"bone": "waterRippleBone",
					"sourceResourceId": "fords-particle-anchor-wtrripls-small",
					"sourceVirtualModel": "art/w3d/p_/p_wtrriplssmall.w3d",
				},
				"attachments": [{
					"field": "ParticleSysBone", "anchorBone": "waterRippleBone",
					"particleSystemId": "WaterRipplesSmall", "options": [],
				}],
				"fxRoots": [],
				"systems": [{
					"particleSystemId": "WaterRipplesSmall",
					"definitionCandidates": [],
					"familyResolution": provisional.duplicate(true),
				}],
			},
		],
	}


func _structure_binding(type_name: String, count: int, include_medium_smoke: bool) -> Dictionary:
	var systems: Array[Dictionary] = [
		_exact_system("BuildingDamaged"),
		_unresolved_system("PCTMediumDust"),
		_unresolved_system("RDTMediumExplosion"),
		_unresolved_system("RDTMediumExplosionLight"),
	]
	if include_medium_smoke:
		systems.append(_unresolved_system("SmokeBuildingMediumRubble"))
	systems.append(_exact_system("UntamedAllegiance"))
	systems.append(_exact_system("UntamedAllegiance2"))
	return {"typeName": type_name, "placementCount": count, "attachments": [], "fxRoots": [], "systems": systems}


func _inn_binding() -> Dictionary:
	return {
		"typeName": "Inn", "placementCount": 2, "attachments": [], "fxRoots": [],
		"systems": [
			_unresolved_system("BuildingContructDust"), _exact_system("BuildingDamaged"),
			_unresolved_system("PCTMediumDust"), _unresolved_system("RDTMediumExplosion"),
			_unresolved_system("RDTMediumExplosionLight"), _unresolved_system("SmokeBuildingLarge"),
		],
	}


func _exact_system(system_id: String) -> Dictionary:
	return {"particleSystemId": system_id, "definitionCandidates": [], "familyResolution": {"selectedKind": "FXParticleSystem", "status": "exact-single-authored-family"}}


func _unresolved_system(system_id: String) -> Dictionary:
	return {"particleSystemId": system_id, "definitionCandidates": [], "familyResolution": {"crossFamilyPrecedenceProven": false, "selectedKind": null, "status": "unresolved-cross-family-precedence"}}


func _write_fixture(document: Dictionary) -> bool:
	if not _write_json(fixture_root.path_join("pack.json"), {"files": {"fordsParticleBindings": "effects/fords-particle-bindings.json"}}):
		return false
	if not _write_json(fixture_root.path_join("effects/fords-particle-bindings.json"), document):
		return false
	for row_value in document.definitionRegistry as Array:
		var row: Dictionary = row_value
		var entries: Array = _ripple_entries() if String(row.definitionId) == "WaterRipplesSmall" and String(row.kind) == "FXParticleSystem" else [{"type": "assignment", "field": "Fixture", "value": "1"}]
		if not _write_json(fixture_root.path_join(String(row.definitionOutputJson)), {
			"schema": "openbfme.sage-particle-definition", "schemaVersion": 0,
			"name": row.definitionId, "kind": row.kind,
			"entries": entries, "source": {"sha256": row.sourceBlockSha256},
		}):
			return false
	var image := Image.create(2, 2, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	for texture_id in TEXTURE_IDS:
		var path := fixture_root.path_join("assets/textures/effects/%s.png" % texture_id)
		if DirAccess.make_dir_recursive_absolute(path.get_base_dir()) != OK or image.save_png(path) != OK:
			return false
	var objects: Array[Dictionary] = []
	for i in range(7):
		objects.append({
			"index": 11 + i * 7, "typeName": "WtrRiplsSmall", "roadType": 0,
			"godotPosition": [1.0 + i, 2.0, 3.0 - i], "godotYawRadians": 0.0,
			"sagePosition": [1.0 + i, 2.0, 3.0 - i],
		})
	return _write_json(fixture_root.path_join("maps/fords-of-isen-ii/objects.json"), {
		"schema": "openbfme.sage-map-objects", "schemaVersion": 0,
		"count": objects.size(), "objects": objects,
	})


func _ripple_entries() -> Array[Dictionary]:
	return [
		_block("System", null, {
			"Priority": "VERY_LOW_OR_ABOVE", "ParticleName": "EXShockWav.tga",
			"Lifetime": "25 50", "Size": "0.4 0.6", "BurstDelay": "5 10",
			"BurstCount": "1 1", "IsGroundAligned": "Yes",
		}),
		_block("Color", "DefaultColor", {"Color2": "R:255 G:255 B:255 2", "Color3": "R:0 G:0 B:0 50"}),
		_block("Update", "DefaultUpdate", {"SizeRate": "0.3 0.5", "SizeRateDamping": "0.9 0.95", "AngularDamping": "0.5 0.5", "AngularDampingXY": "1 1"}),
		_block("Physics", "DefaultPhysics", {"VelocityDamping": "0.9 0.9"}),
		_block("EmissionVelocity", "OutwardEmissionVelocity", {"Speed": "0 0.24"}),
		_block("EmissionVolume", "PointEmissionVolume", {}),
		_block("Draw", "DefaultDraw", {}),
	]


func _block(field: String, selector: Variant, assignments: Dictionary) -> Dictionary:
	var entries: Array[Dictionary] = []
	for key_value in assignments:
		var key := String(key_value)
		entries.append({"type": "assignment", "field": key, "value": assignments[key]})
	return {"type": "block", "field": field, "selector": selector, "entries": entries}


func _source_to_local(source: Vector3) -> Vector3:
	return source * 0.1


func _gpu_emitter_count(parent: Node) -> int:
	var count := 0
	for child in parent.get_children():
		if child is GPUParticles3D:
			count += 1
	return count


func _has_diagnostic(rows: Array, code: String) -> bool:
	for row_value in rows:
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("code", "")) == code:
			return true
	return false


func _write_json(path: String, document: Dictionary) -> bool:
	if DirAccess.make_dir_recursive_absolute(path.get_base_dir()) != OK:
		return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(document, "  ") + "\n")
	file.close()
	return true


func _cleanup_fixture() -> void:
	_remove_tree(fixture_root)


func _remove_tree(path: String) -> void:
	if path == "" or not DirAccess.dir_exists_absolute(path):
		return
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var entry := directory.get_next()
	while entry != "":
		if entry != "." and entry != "..":
			var child := path.path_join(entry)
			if directory.current_is_dir():
				_remove_tree(child)
			else:
				DirAccess.remove_absolute(child)
		entry = directory.get_next()
	directory.list_dir_end()
	DirAccess.remove_absolute(path)


func _check(label: String, condition: bool, detail := "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
		return
	failed += 1
	push_error("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_PARTICLE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _watchdog_timeout() -> void:
	push_error("FAIL retail_particle_runtime_watchdog")
	quit(2)
