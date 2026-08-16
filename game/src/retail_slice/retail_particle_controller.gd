class_name RetailParticleController
extends Node3D
## Fail-closed presenter for the bounded Fords retail particle handoff.
##
## Retail registers only TheFXParticleSystemManager. The selected sealed pack's
## older contract is admitted only at its exact census/oracle identity and is
## upgraded to that proven FX-family rule without changing pack bytes.

const MAX_DOCUMENT_BYTES := 2 * 1024 * 1024
const MAX_DEFINITION_BYTES := 256 * 1024
const EXPECTED_DEFINITION_COUNT := 35
const EXPECTED_TEXTURE_COUNT := 15
const EXPECTED_FX_LIST_COUNT := 8
const EXPECTED_BINDING_COUNT := 4
const EXPECTED_PLACEMENT_COUNT := 15
const EXPECTED_RIPPLE_PLACEMENT_COUNT := 7
const EXPECTED_UNRESOLVED_SYSTEM_IDS: Array[String] = [
	"BuildingContructDust",
	"PCTMediumDust",
	"RDTMediumExplosion",
	"RDTMediumExplosionLight",
	"SmokeBuildingLarge",
	"SmokeBuildingMediumRubble",
	"FireBuildingLarge",
	"FireBuildingMedium",
	"FireBuildingSmall",
	"SmokeBuildingMedium",
]
const EXPECTED_BOUND_UNRESOLVED_SYSTEM_IDS: Array[String] = [
	"BuildingContructDust",
	"PCTMediumDust",
	"RDTMediumExplosion",
	"RDTMediumExplosionLight",
	"SmokeBuildingLarge",
	"SmokeBuildingMediumRubble",
]
const EXPECTED_DUPLICATE_SYSTEM_IDS: Array[String] = [
	"BuildingContructDust",
	"PCTMediumDust",
	"RDTMediumExplosion",
	"RDTMediumExplosionLight",
	"SmokeBuildingLarge",
	"SmokeBuildingMediumRubble",
	"WaterRipplesSmall",
	"FireBuildingLarge",
	"FireBuildingMedium",
	"FireBuildingSmall",
	"SmokeBuildingMedium",
]
const EXPECTED_BINDING_PLACEMENTS := {
	"CaveTrollLair": 2,
	"Inn": 2,
	"WargLair": 4,
	"WtrRiplsSmall": 7,
}
const RIPPLE_DEFINITION_ID := "fords-particle-def-waterripplessmall-875373f7d4aa"
const RIPPLE_TEXTURE_ID := "fords-particle-texture-exshockwav-d1243f3478af"
const RIPPLE_SOURCE_MODEL := "art/w3d/p_/p_wtrriplssmall.w3d"
const RIPPLE_ANCHOR_RESOURCE_ID := "fords-particle-anchor-wtrripls-small"
const RIPPLE_ANCHOR_BONE := "waterRippleBone"
const RIPPLE_ORACLE_SHA256 := "5dacb5477f89ba3dfcfa0b3450ade12fe7ebd79e4c7221463efd44e390108905"
const LEGACY_SELECTED_CENSUS_SHA256 := "135e52baa19ebe7e922fdea7904e6725d3e3e7985cce923d35332a7ff83a3265"
# P_WtrRiplsSmall pivot WATERRIPPLEBONE, converted from W3D X/Y/Z to
# Godot-source X/Z/-Y before the map's source-to-local transform is applied.
const RIPPLE_ANCHOR_OFFSET_SOURCE := Vector3(1.3329274654388428, 0.0, -0.6909178495407104)

var contract_declared := false
var contract_ready := false
var presentation_ready := false
var parity_ready := false
var error := ""
var validated_definition_count := 0
var validated_texture_count := 0
var provisional_runtime_selection_count := 0
var unresolved_family_selection_count := 0
var instantiated_emitter_count := 0
var source_placement_indices: Array[int] = []
var diagnostics: Array[Dictionary] = []

var _definition_by_key: Dictionary = {}
var _texture_image_by_id: Dictionary = {}
var _legacy_family_resolution_upgraded := false


func configure_from_pack(
	pack_root: String,
	map_root: String,
	local_transform_scale: float,
	source_to_local: Callable,
	presentation_parent: Node3D
) -> bool:
	_reset()
	var pack_path := ModLoader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return _fail("particle runtime requires a contained pack document")
	var pack_value: Variant = ModLoader._read_json(pack_path)
	if typeof(pack_value) != TYPE_DICTIONARY:
		return _fail("particle runtime pack document is invalid")
	var files_value: Variant = (pack_value as Dictionary).get("files", {})
	if typeof(files_value) != TYPE_DICTIONARY:
		return _fail("particle runtime pack files table is invalid")
	var files := files_value as Dictionary
	if not files.has("fordsParticleBindings"):
		return true
	contract_declared = true
	var contract_relative := String(files.get("fordsParticleBindings", ""))
	var contract_path := ModLoader.resolve_pack_path(pack_root, contract_relative)
	if not _safe_json_path(pack_root, contract_path):
		return _fail("declared particle runtime document is missing or escaped")
	var document := _read_bounded_json(contract_path, MAX_DOCUMENT_BYTES)
	if document.is_empty():
		return _fail("particle runtime document is invalid or empty")
	return configure_document(
		document,
		pack_root,
		map_root,
		local_transform_scale,
		source_to_local,
		presentation_parent
	)


func configure_document(
	document: Dictionary,
	pack_root: String,
	map_root: String,
	local_transform_scale: float,
	source_to_local: Callable,
	presentation_parent: Node3D
) -> bool:
	_reset()
	contract_declared = true
	if presentation_parent == null:
		return _fail("particle runtime has no presentation parent")
	if not is_finite(local_transform_scale) or local_transform_scale <= 0.0:
		return _fail("particle runtime map scale is invalid")
	if not source_to_local.is_valid():
		return _fail("particle runtime source transform is unavailable")
	if not ModLoader.path_is_within(pack_root, map_root):
		return _fail("particle runtime map root escaped the selected pack")
	if not _validate_contract(document, pack_root):
		return false
	var ripple_placements := _load_ripple_placements(map_root, source_to_local)
	if ripple_placements.size() != EXPECTED_RIPPLE_PLACEMENT_COUNT:
		return _fail("particle runtime map does not contain the exact seven ripple anchors")
	if not _instantiate_ripples(ripple_placements, local_transform_scale, presentation_parent):
		return false
	contract_ready = true
	presentation_ready = instantiated_emitter_count == EXPECTED_RIPPLE_PLACEMENT_COUNT
	# Family selection is proven. The SAGE-to-Godot lifetime/size-rate visual
	# oracle is still not exact, so this remains presentation-ready but not 1:1.
	parity_ready = false
	diagnostics.append({
		"code": "water-ripple-render-translation-provisional",
		"systemId": "WaterRipplesSmall",
		"selectedKind": "FXParticleSystem",
		"emitterCount": instantiated_emitter_count,
		"impact": "family and placement are exact; lifetime and size-rate timing still require a retail visual oracle",
	})
	_set_runtime_metadata()
	return true


func _validate_contract(document: Dictionary, pack_root: String) -> bool:
	if String(document.get("schema", "")) != "openbfme.fords-particle-bindings" or int(document.get("schemaVersion", -1)) != 0:
		return _fail("unexpected Fords particle runtime schema")
	if not _is_sha256(String(document.get("sourceCensusAggregateSha256", ""))):
		return _fail("particle runtime source census identity is invalid")
	if not _validate_family_resolution(document.get("familyResolution", {})):
		return false
	if _legacy_family_resolution_upgraded and String(document.get("sourceCensusAggregateSha256", "")) != LEGACY_SELECTED_CENSUS_SHA256:
		return _fail("legacy particle family proof is not from the exact selected census")
	if not _validate_registry(document.get("definitionRegistry", []), pack_root):
		return false
	if not _validate_bindings(document.get("objectBindings", [])):
		return false
	var fx_lists_value: Variant = document.get("fxLists", [])
	if typeof(fx_lists_value) != TYPE_ARRAY or (fx_lists_value as Array).size() != EXPECTED_FX_LIST_COUNT:
		return _fail("particle runtime FX-list closure changed")
	return true


func _validate_family_resolution(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("particle family-resolution evidence is missing")
	var family := value as Dictionary
	if (
		String(family.get("status", "")) != "provisional-selection-with-cross-family-precedence-unresolved"
		or not bool(family.get("noGeneralPrecedenceRule", false))
		or _string_array(family.get("duplicateIdentifierSystemIds", [])) != EXPECTED_DUPLICATE_SYSTEM_IDS
		or _string_array(family.get("unresolvedDuplicateIdentifierSystemIds", [])) != EXPECTED_UNRESOLVED_SYSTEM_IDS
	):
		return _validate_proven_family_resolution(family)
	var selections_value: Variant = family.get("provisionalRuntimeSelections", [])
	if typeof(selections_value) != TYPE_ARRAY or (selections_value as Array).size() != 1:
		return _fail("particle runtime must declare exactly one provisional selection")
	var selection_value: Variant = (selections_value as Array)[0]
	if typeof(selection_value) != TYPE_DICTIONARY:
		return _fail("particle provisional selection is invalid")
	var selection := selection_value as Dictionary
	if (
		String(selection.get("particleSystemId", "")) != "WaterRipplesSmall"
		or String(selection.get("selectedKind", "")) != "FXParticleSystem"
		or String(selection.get("status", "")) != "provisional-explicit-runtime-selection"
		or bool(selection.get("crossFamilyPrecedenceProven", true))
		or bool(selection.get("generalizesToOtherDuplicateIdentifiers", true))
		or String(selection.get("oracleAggregateSha256", "")) != RIPPLE_ORACLE_SHA256
	):
		return _fail("WaterRipplesSmall provisional selection evidence changed")
	# Compatibility admission for the exact selected sealed pack. Retail binary
	# evidence now proves these old unresolved rows all select the FX candidate.
	_legacy_family_resolution_upgraded = true
	provisional_runtime_selection_count = 0
	unresolved_family_selection_count = 0
	return true


func _validate_proven_family_resolution(family: Dictionary) -> bool:
	if (
		String(family.get("status", "")) != "proven-effective-fx-manager-family"
		or bool(family.get("noGeneralPrecedenceRule", true))
		or _string_array(family.get("duplicateIdentifierSystemIds", [])) != EXPECTED_DUPLICATE_SYSTEM_IDS
		or not _string_array(family.get("unresolvedDuplicateIdentifierSystemIds", [])).is_empty()
	):
		return _fail("particle FX-manager family proof changed")
	var semantics_value: Variant = family.get("duplicateSemantics", {})
	if typeof(semantics_value) != TYPE_DICTIONARY:
		return _fail("particle duplicate semantics are missing")
	var semantics := semantics_value as Dictionary
	if String(semantics.get("crossFamilyPrecedence", "")) != "proven-fx-manager-only" or bool(semantics.get("legacySubsystemActive", true)):
		return _fail("particle FX-manager-only evidence changed")
	var selections_value: Variant = family.get("runtimeSelections", [])
	if typeof(selections_value) != TYPE_ARRAY or (selections_value as Array).size() != EXPECTED_DUPLICATE_SYSTEM_IDS.size():
		return _fail("particle proven runtime selection closure changed")
	var seen: Dictionary = {}
	for value in selections_value as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return _fail("particle proven runtime selection is invalid")
		var selection := value as Dictionary
		var system_id := String(selection.get("particleSystemId", ""))
		if seen.has(system_id) or not EXPECTED_DUPLICATE_SYSTEM_IDS.has(system_id) or String(selection.get("selectedKind", "")) != "FXParticleSystem" or String(selection.get("status", "")) != "proven-effective-fx-manager-family" or not bool(selection.get("crossFamilyPrecedenceProven", false)):
			return _fail("particle proven runtime selection changed")
		seen[system_id] = true
	provisional_runtime_selection_count = 0
	unresolved_family_selection_count = 0
	return true


func _validate_registry(value: Variant, pack_root: String) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != EXPECTED_DEFINITION_COUNT:
		return _fail("particle definition registry does not contain exactly %d entries" % EXPECTED_DEFINITION_COUNT)
	var seen_keys: Dictionary = {}
	var texture_ids: Dictionary = {}
	for row_value in value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("particle definition registry contains a non-document")
		var row := row_value as Dictionary
		var name := String(row.get("definitionId", ""))
		var kind := String(row.get("kind", ""))
		var resource_id := String(row.get("definitionResourceId", ""))
		var relative := String(row.get("definitionOutputJson", ""))
		var digest := String(row.get("sourceBlockSha256", ""))
		var key := "%s\n%s" % [kind, name]
		if (
			name == ""
			or not ["ParticleSystem", "FXParticleSystem"].has(kind)
			or resource_id == ""
			or seen_keys.has(key)
			or not _is_sha256(digest)
		):
			return _fail("particle definition registry identity is unsafe or duplicate")
		var definition_path := ModLoader.resolve_pack_path(pack_root, relative)
		if not _safe_json_path(pack_root, definition_path):
			return _fail("converted particle definition is missing or escaped: %s" % resource_id)
		var definition := _read_bounded_json(definition_path, MAX_DEFINITION_BYTES)
		if not _validate_definition_document(definition, name, kind, digest):
			return false
		var row_texture_ids := _string_array(row.get("textureResourceIds", []))
		if row_texture_ids.is_empty():
			return _fail("particle definition has no exact source texture")
		for texture_id in row_texture_ids:
			texture_ids[texture_id] = true
		seen_keys[key] = true
		_definition_by_key[key] = definition
	if seen_keys.size() != EXPECTED_DEFINITION_COUNT or not seen_keys.has("FXParticleSystem\nWaterRipplesSmall"):
		return _fail("particle definition registry closure is incomplete")
	if texture_ids.size() != EXPECTED_TEXTURE_COUNT or not texture_ids.has(RIPPLE_TEXTURE_ID):
		return _fail("particle texture closure does not contain exactly %d retail leaves" % EXPECTED_TEXTURE_COUNT)
	for texture_id_value in texture_ids:
		var texture_id := String(texture_id_value)
		var texture_path := ModLoader.resolve_pack_path(pack_root, "assets/textures/effects/%s.png" % texture_id)
		if texture_path == "" or not FileAccess.file_exists(texture_path) or not ModLoader.path_is_within(pack_root, texture_path):
			return _fail("converted particle texture is missing or escaped: %s" % texture_id)
		var image := Image.new()
		if image.load(texture_path) != OK or image.is_empty() or image.get_width() > 4096 or image.get_height() > 4096:
			return _fail("converted particle texture is invalid: %s" % texture_id)
		_texture_image_by_id[texture_id] = image
	validated_definition_count = seen_keys.size()
	validated_texture_count = texture_ids.size()
	return true


func _validate_definition_document(document: Dictionary, name: String, kind: String, digest: String) -> bool:
	if (
		document.is_empty()
		or String(document.get("schema", "")) != "openbfme.sage-particle-definition"
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("name", "")) != name
		or String(document.get("kind", "")) != kind
	):
		return _fail("converted particle definition disagrees with its registry identity")
	var source_value: Variant = document.get("source", {})
	var entries_value: Variant = document.get("entries", [])
	if (
		typeof(source_value) != TYPE_DICTIONARY
		or String((source_value as Dictionary).get("sha256", "")) != digest
		or typeof(entries_value) != TYPE_ARRAY
		or (entries_value as Array).is_empty()
	):
		return _fail("converted particle definition lacks exact source evidence")
	if name == "WaterRipplesSmall" and kind == "FXParticleSystem":
		return _validate_ripple_definition_entries(entries_value as Array)
	return true


func _validate_ripple_definition_entries(entries: Array) -> bool:
	var blocks := _top_level_block_assignments(entries)
	var expected := {
		"System": {
			"Priority": "VERY_LOW_OR_ABOVE", "ParticleName": "EXShockWav.tga",
			"Lifetime": "25 50", "Size": "0.4 0.6", "BurstDelay": "5 10",
			"BurstCount": "1 1", "IsGroundAligned": "Yes",
		},
		"Color\nDefaultColor": {"Color2": "R:255 G:255 B:255 2", "Color3": "R:0 G:0 B:0 50"},
		"Update\nDefaultUpdate": {
			"SizeRate": "0.3 0.5", "SizeRateDamping": "0.9 0.95",
			"AngularDamping": "0.5 0.5", "AngularDampingXY": "1 1",
		},
		"Physics\nDefaultPhysics": {"VelocityDamping": "0.9 0.9"},
		"EmissionVelocity\nOutwardEmissionVelocity": {"Speed": "0 0.24"},
		"EmissionVolume\nPointEmissionVolume": {},
		"Draw\nDefaultDraw": {},
	}
	if blocks != expected:
		return _fail("WaterRipplesSmall converted parameters changed")
	return true


func _top_level_block_assignments(entries: Array) -> Dictionary:
	var result: Dictionary = {}
	for entry_value in entries:
		if typeof(entry_value) != TYPE_DICTIONARY:
			return {}
		var entry := entry_value as Dictionary
		if String(entry.get("type", "")) != "block":
			return {}
		var field := String(entry.get("field", ""))
		var selector_value: Variant = entry.get("selector", null)
		var key := field
		if selector_value != null:
			key += "\n" + String(selector_value)
		if key == "" or result.has(key) or typeof(entry.get("entries", [])) != TYPE_ARRAY:
			return {}
		var assignments: Dictionary = {}
		for child_value in entry.get("entries", []) as Array:
			if typeof(child_value) != TYPE_DICTIONARY:
				return {}
			var child := child_value as Dictionary
			var child_field := String(child.get("field", ""))
			if String(child.get("type", "")) != "assignment" or child_field == "" or assignments.has(child_field):
				return {}
			assignments[child_field] = String(child.get("value", ""))
		result[key] = assignments
	return result


func _validate_bindings(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != EXPECTED_BINDING_COUNT:
		return _fail("particle object-binding closure changed")
	var seen: Dictionary = {}
	var placement_count := 0
	var unresolved_seen: Dictionary = {}
	var provisional_seen := 0
	for binding_value in value as Array:
		if typeof(binding_value) != TYPE_DICTIONARY:
			return _fail("particle object binding is invalid")
		var binding := binding_value as Dictionary
		var type_name := String(binding.get("typeName", ""))
		var count := int(binding.get("placementCount", -1))
		if seen.has(type_name) or not EXPECTED_BINDING_PLACEMENTS.has(type_name) or count != int(EXPECTED_BINDING_PLACEMENTS[type_name]):
			return _fail("particle object binding does not exactly cover Fords placements")
		seen[type_name] = true
		placement_count += count
		var systems_value: Variant = binding.get("systems", [])
		if typeof(systems_value) != TYPE_ARRAY or (systems_value as Array).is_empty():
			return _fail("particle object binding has no systems")
		for system_value in systems_value as Array:
			if typeof(system_value) != TYPE_DICTIONARY:
				return _fail("particle bound system is invalid")
			var system := system_value as Dictionary
			var system_id := String(system.get("particleSystemId", ""))
			var resolution_value: Variant = system.get("familyResolution", {})
			if typeof(resolution_value) != TYPE_DICTIONARY:
				return _fail("particle bound system lacks family resolution")
			var resolution := resolution_value as Dictionary
			var status := String(resolution.get("status", ""))
			if status == "proven-effective-fx-manager-family":
				if not EXPECTED_DUPLICATE_SYSTEM_IDS.has(system_id) or String(resolution.get("selectedKind", "")) != "FXParticleSystem" or not bool(resolution.get("crossFamilyPrecedenceProven", false)):
					return _fail("proven particle family selection changed")
			elif status == "unresolved-cross-family-precedence":
				if _legacy_family_resolution_upgraded and EXPECTED_BOUND_UNRESOLVED_SYSTEM_IDS.has(system_id):
					if resolution.get("selectedKind", "sentinel") != null:
						return _fail("unresolved particle family was silently selected")
					unresolved_seen[system_id] = true
					continue
				if resolution.get("selectedKind", "sentinel") != null or not EXPECTED_BOUND_UNRESOLVED_SYSTEM_IDS.has(system_id):
					return _fail("unresolved particle family was silently selected")
				unresolved_seen[system_id] = true
			elif status == "provisional-explicit-runtime-selection":
				if (
					type_name != "WtrRiplsSmall"
					or system_id != "WaterRipplesSmall"
					or String(resolution.get("selectedKind", "")) != "FXParticleSystem"
					or bool(resolution.get("crossFamilyPrecedenceProven", true))
					or bool(resolution.get("generalizesToOtherDuplicateIdentifiers", true))
					or String(resolution.get("oracleAggregateSha256", "")) != RIPPLE_ORACLE_SHA256
				):
					return _fail("unexpected provisional particle selection")
				if not _legacy_family_resolution_upgraded:
					provisional_seen += 1
			elif status != "exact-single-authored-family":
				return _fail("particle binding has an unsupported family resolution")
		if type_name == "WtrRiplsSmall" and not _validate_ripple_binding(binding):
			return false
	if seen.size() != EXPECTED_BINDING_COUNT or placement_count != EXPECTED_PLACEMENT_COUNT:
		return _fail("particle object bindings do not cover the exact 15 placements")
	var unresolved_ids: Array[String] = []
	for system_id_value in unresolved_seen:
		unresolved_ids.append(String(system_id_value))
	unresolved_ids.sort()
	var expected_sorted := EXPECTED_BOUND_UNRESOLVED_SYSTEM_IDS.duplicate()
	expected_sorted.sort()
	if _legacy_family_resolution_upgraded:
		if unresolved_ids != expected_sorted or provisional_seen != 0:
			return _fail("legacy particle family-resolution coverage changed")
	elif not unresolved_ids.is_empty() or provisional_seen != 0:
		return _fail("particle family-resolution coverage changed")
	return true


func _validate_ripple_binding(binding: Dictionary) -> bool:
	var anchor_value: Variant = binding.get("anchor", {})
	if typeof(anchor_value) != TYPE_DICTIONARY:
		return _fail("ripple binding has no anchor")
	var anchor := anchor_value as Dictionary
	if (
		String(anchor.get("bone", "")) != RIPPLE_ANCHOR_BONE
		or String(anchor.get("sourceResourceId", "")) != RIPPLE_ANCHOR_RESOURCE_ID
		or String(anchor.get("sourceVirtualModel", "")) != RIPPLE_SOURCE_MODEL
	):
		return _fail("ripple anchor evidence changed")
	var attachments_value: Variant = binding.get("attachments", [])
	if typeof(attachments_value) != TYPE_ARRAY or (attachments_value as Array).size() != 1:
		return _fail("ripple attachment count changed")
	var attachment_value: Variant = (attachments_value as Array)[0]
	if typeof(attachment_value) != TYPE_DICTIONARY:
		return _fail("ripple attachment is invalid")
	var attachment := attachment_value as Dictionary
	if (
		String(attachment.get("anchorBone", "")) != RIPPLE_ANCHOR_BONE
		or String(attachment.get("particleSystemId", "")) != "WaterRipplesSmall"
		or String(attachment.get("field", "")) != "ParticleSysBone"
	):
		return _fail("ripple attachment evidence changed")
	return true


func _load_ripple_placements(map_root: String, source_to_local: Callable) -> Array[Dictionary]:
	var objects_path := ModLoader.resolve_pack_path(map_root, "objects.json")
	if not _safe_json_path(map_root, objects_path):
		_fail("particle runtime cooked map objects are missing or escaped")
		return []
	var document := _read_bounded_json(objects_path, MAX_DOCUMENT_BYTES)
	if String(document.get("schema", "")) != "openbfme.sage-map-objects" or int(document.get("schemaVersion", -1)) != 0:
		_fail("particle runtime cooked object schema is invalid")
		return []
	var objects_value: Variant = document.get("objects", [])
	if typeof(objects_value) != TYPE_ARRAY or int(document.get("count", -1)) != (objects_value as Array).size():
		_fail("particle runtime cooked object count is invalid")
		return []
	var placements: Array[Dictionary] = []
	var indices: Dictionary = {}
	for object_value in objects_value as Array:
		if typeof(object_value) != TYPE_DICTIONARY:
			_fail("particle runtime cooked object is invalid")
			return []
		var object := object_value as Dictionary
		if String(object.get("typeName", "")) != "WtrRiplsSmall":
			continue
		var source_index := int(object.get("index", -1))
		var road_type := int(object.get("roadType", -1))
		var position := _vector3(object.get("godotPosition", []))
		var yaw := float(object.get("godotYawRadians", NAN))
		if source_index < 0 or indices.has(source_index) or road_type != 0 or position == Vector3.INF or not is_finite(yaw):
			_fail("ripple source placement is invalid")
			return []
		var rotated_offset := Basis(Vector3.UP, yaw) * RIPPLE_ANCHOR_OFFSET_SOURCE
		var local_value: Variant = source_to_local.call(position + rotated_offset)
		if typeof(local_value) != TYPE_VECTOR3 or not _finite_vector3(local_value as Vector3):
			_fail("ripple anchor source transform failed")
			return []
		placements.append({"source_index": source_index, "position": local_value, "yaw": yaw})
		indices[source_index] = true
	placements.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.source_index) < int(b.source_index))
	return placements


func _instantiate_ripples(placements: Array[Dictionary], local_scale: float, presentation_parent: Node3D) -> bool:
	var definition: Dictionary = _definition_by_key.get("FXParticleSystem\nWaterRipplesSmall", {})
	var image: Image = _texture_image_by_id.get(RIPPLE_TEXTURE_ID)
	if definition.is_empty() or image == null:
		return _fail("selected ripple definition or texture was not validated")
	var texture := ImageTexture.create_from_image(image)
	if texture == null:
		return _fail("selected ripple texture could not create a runtime image")
	var container := Node3D.new()
	container.name = "RetailWaterRipples"
	container.set_meta("presentation", "proven-effective-fx-manager-family")
	for placement in placements:
		var emitter := _make_ripple_emitter(texture, local_scale)
		if emitter == null:
			container.free()
			return _fail("selected ripple emitter could not be constructed")
		var source_index := int(placement.source_index)
		emitter.name = "WaterRipplesSmall_%04d" % source_index
		emitter.position = Vector3(placement.position)
		emitter.rotation.y = float(placement.yaw)
		emitter.set_meta("source_index", source_index)
		emitter.set_meta("source_type", "WtrRiplsSmall")
		emitter.set_meta("particle_system_id", "WaterRipplesSmall")
		emitter.set_meta("selected_kind", "FXParticleSystem")
		emitter.set_meta("selection_status", "proven-effective-fx-manager-family")
		emitter.set_meta("anchor_bone", RIPPLE_ANCHOR_BONE)
		container.add_child(emitter)
		source_placement_indices.append(source_index)
	presentation_parent.add_child(container)
	instantiated_emitter_count = container.get_child_count()
	return instantiated_emitter_count == EXPECTED_RIPPLE_PLACEMENT_COUNT


func _make_ripple_emitter(texture: Texture2D, local_scale: float) -> GPUParticles3D:
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.billboard_mode = BaseMaterial3D.BILLBOARD_DISABLED
	material.albedo_texture = texture
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	quad.orientation = PlaneMesh.FACE_Y
	quad.material = material
	var process := ParticleProcessMaterial.new()
	process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	process.direction = Vector3.RIGHT
	process.spread = 180.0
	process.initial_velocity_min = 0.0
	process.initial_velocity_max = 0.24 * local_scale * 30.0
	process.damping_min = 0.9
	process.damping_max = 0.9
	process.scale_min = 0.4 * local_scale
	process.scale_max = 0.6 * local_scale
	var scale_curve := CurveTexture.new()
	var curve := Curve.new()
	curve.add_point(Vector2(0.0, 1.0))
	curve.add_point(Vector2(1.0, 12.0))
	scale_curve.curve = curve
	process.scale_curve = scale_curve
	var color_ramp := GradientTexture1D.new()
	var gradient := Gradient.new()
	gradient.set_color(0, Color(1.0, 1.0, 1.0, 0.65))
	gradient.set_color(1, Color(0.0, 0.0, 0.0, 0.0))
	color_ramp.gradient = gradient
	process.color_ramp = color_ramp
	var emitter := GPUParticles3D.new()
	emitter.amount = 6
	emitter.lifetime = 50.0 / 30.0
	emitter.randomness = 0.5
	emitter.fixed_fps = 30
	emitter.preprocess = emitter.lifetime
	emitter.visibility_aabb = AABB(Vector3(-8.0, -1.0, -8.0), Vector3(16.0, 2.0, 16.0))
	emitter.process_material = process
	emitter.draw_pass_1 = quad
	emitter.emitting = true
	return emitter


func _read_bounded_json(path: String, maximum_bytes: int) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	var payload := file.get_buffer(file.get_length())
	file.close()
	var parsed: Variant = JSON.parse_string(payload.get_string_from_utf8())
	return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}


func _safe_json_path(root_path: String, path: String) -> bool:
	return path != "" and path.get_extension().to_lower() == "json" and FileAccess.file_exists(path) and ModLoader.path_is_within(root_path, path)


func _string_array(value: Variant) -> Array[String]:
	var result: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return result
	for item in value as Array:
		if typeof(item) != TYPE_STRING:
			return []
		result.append(String(item))
	return result


func _vector3(value: Variant) -> Vector3:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 3:
		return Vector3.INF
	var values := value as Array
	if not _finite_number(values[0]) or not _finite_number(values[1]) or not _finite_number(values[2]):
		return Vector3.INF
	return Vector3(float(values[0]), float(values[1]), float(values[2]))


func _finite_number(value: Variant) -> bool:
	return (typeof(value) == TYPE_INT or typeof(value) == TYPE_FLOAT) and is_finite(float(value))


func _finite_vector3(value: Vector3) -> bool:
	return is_finite(value.x) and is_finite(value.y) and is_finite(value.z)


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for index in range(value.length()):
		var code := value.unicode_at(index)
		if not (code >= 48 and code <= 57) and not (code >= 97 and code <= 102):
			return false
	return true


func _set_runtime_metadata() -> void:
	set_meta("contract_ready", contract_ready)
	set_meta("presentation_ready", presentation_ready)
	set_meta("parity_ready", parity_ready)
	set_meta("validated_definition_count", validated_definition_count)
	set_meta("validated_texture_count", validated_texture_count)
	set_meta("provisional_runtime_selection_count", provisional_runtime_selection_count)
	set_meta("unresolved_family_selection_count", unresolved_family_selection_count)
	set_meta("instantiated_emitter_count", instantiated_emitter_count)
	set_meta("source_placement_indices", source_placement_indices.duplicate())
	set_meta("diagnostics", diagnostics.duplicate(true))


func _reset() -> void:
	for child in get_children():
		child.queue_free()
	contract_declared = false
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	error = ""
	validated_definition_count = 0
	validated_texture_count = 0
	provisional_runtime_selection_count = 0
	unresolved_family_selection_count = 0
	instantiated_emitter_count = 0
	source_placement_indices.clear()
	diagnostics.clear()
	_definition_by_key.clear()
	_texture_image_by_id.clear()
	_legacy_family_resolution_upgraded = false


func _fail(message: String) -> bool:
	error = message
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	return false
