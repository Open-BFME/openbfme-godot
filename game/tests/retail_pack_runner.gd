extends SceneTree
## End-to-end audit of the currently selected private retail pack.

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	var mod_loader = root.get_node_or_null("ModLoader")
	var game_audio = root.get_node_or_null("GameAudio")
	_check("autoloads", content_db != null and mod_loader != null and game_audio != null)
	if content_db == null or mod_loader == null or game_audio == null:
		_finish()
		return

	content_db.reload()
	var active_member: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var selected: String = String(active_member.get("_pack_root", ""))
	_check("selected_pack_exists", selected != "" and DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(selected)), selected)
	_check("selected_pack_is_external", not selected.begins_with("res://"), selected)
	_check("retail_pack_mounted", content_db.pack_roots.has(selected), selected)
	var expected_pack_id := "bfme2-men-vslice"
	var pack_meta: Variant = mod_loader._read_json(selected.path_join("pack.json"))
	_check("pack_manifest_v0", typeof(pack_meta) == TYPE_DICTIONARY and String((pack_meta as Dictionary).get("id", "")) == expected_pack_id)
	_check("pack_is_private", typeof(pack_meta) == TYPE_DICTIONARY and not bool((pack_meta as Dictionary).get("redistributable", true)))

	var object_id := "bfme2.object.gondor-fighter"
	var horde_id := "bfme2.object.gondor-fighter-horde"
	_check("member_definition", content_db.bundle_objects.has(object_id))
	_check("horde_definition", content_db.bundle_objects.has(horde_id))
	_check("fords_definition", content_db.bundle_maps.has("bfme2.map.fords-of-isen-ii"))
	_check("animation_capability", content_db.animation_capabilities.has("bfme2.animation.gondor-fighter"))

	var definition: Dictionary = content_db.get_bundle_object(object_id)
	var model_path: String = content_db.resolve_mesh_path(definition)
	_check("model_is_selected_pack_relative", mod_loader.path_is_within(selected, model_path) and model_path.ends_with("gondor-fighter.glb"), model_path)
	_check("model_substantial", FileAccess.get_file_as_bytes(model_path).size() > 1000000)

	var asset_factory = load("res://src/view/asset_factory.gd")
	var visual: Node3D = asset_factory.make_bundle_object_visual(object_id, 0)
	if visual != null:
		root.add_child(visual)
		await process_frame
	_check("retail_glb_loaded", visual != null and bool(visual.get_meta("authored", false)))
	_check("retail_rig_loaded", visual != null and bool(visual.get_meta("has_skeleton", false)))
	var clips: Array = visual.get_meta("animation_clips", []) if visual != null else []
	_check("all_core_clips_loaded", clips.size() == 23, "clips=%d %s" % [clips.size(), str(clips)])
	for expected in ["gumanmocap_idlb", "gumanmocap_runb", "gumanmocap_atka", "gumanmocap_dieb"]:
		_check("clip_%s" % expected, _clip_list_contains(clips, expected), str(clips))

	var metrics := _scene_metrics(visual)
	_check("scene_has_meshes", int(metrics.get("meshes", 0)) >= 1, str(metrics))
	_check("scene_has_skeleton", int(metrics.get("skeletons", 0)) == 1, str(metrics))
	_check("scene_has_animation_player", int(metrics.get("players", 0)) >= 1, str(metrics))
	_check("scene_has_materials", int(metrics.get("materials", 0)) >= 1, str(metrics))
	if visual != null:
		visual.queue_free()
		await process_frame
	asset_factory.clear_mesh_cache()

	var additional_units := [
		{"id": "gondor-archer", "clips": ["guarcher_idla", "guarcher_runa", "guarcher_atkd", "guarcher_diea"]},
		{"id": "gondor-tower-guard", "clips": ["gutowergrd_idla", "gutowergrd_runa", "gutowergrd_atka", "gutowergrd_diea"]},
		{"id": "gondor-knight", "clips": ["gucavalry_idla", "gucavalry_runa", "gucavalry_atka", "gucavalry_diea"]},
	]
	for spec: Dictionary in additional_units:
		var unit_id := String(spec["id"])
		var unit_path := selected.path_join("assets/models/units/%s.glb" % unit_id)
		_check("%s_model_substantial" % unit_id, FileAccess.get_file_as_bytes(unit_path).size() > 1024, unit_path)
		var unit_visual: Node3D = asset_factory._try_load_model(unit_path)
		if unit_visual != null:
			asset_factory._annotate_rig_and_animation(unit_visual, unit_visual)
			unit_visual.set_meta("authored", true)
			root.add_child(unit_visual)
			await process_frame
		_check("%s_glb_loaded" % unit_id, unit_visual != null and bool(unit_visual.get_meta("authored", false)))
		var unit_metrics := _scene_metrics(unit_visual)
		_check("%s_rig_material_animation" % unit_id, int(unit_metrics.get("skeletons", 0)) == 1 and int(unit_metrics.get("players", 0)) >= 1 and int(unit_metrics.get("materials", 0)) >= 1, str(unit_metrics))
		var unit_clips: Array = unit_visual.get_meta("animation_clips", []) if unit_visual != null else []
		_check("%s_core_clips" % unit_id, _clip_list_contains_all(unit_clips, spec["clips"]), str(unit_clips))
		if unit_visual != null:
			unit_visual.queue_free()
			await process_frame
		asset_factory.clear_mesh_cache()

	var structure_script = load("res://src/retail_slice/retail_structure.gd")
	var structure_specs := [
		{"id": "bfme2.object.men-fortress", "kind": "fortress", "maximum": 7500},
		{"id": "bfme2.object.men-farm", "kind": "farm", "maximum": 2000},
		{"id": "bfme2.object.men-barracks", "kind": "barracks", "maximum": 3000},
		{"id": "bfme2.object.men-archery-range", "kind": "archery_range", "maximum": 3000},
		{"id": "bfme2.object.men-stable", "kind": "stable", "maximum": 3000},
	]
	for structure_spec: Dictionary in structure_specs:
		var structure_object_id := String(structure_spec["id"])
		var structure_kind := String(structure_spec["kind"])
		var structure_label := structure_object_id.trim_prefix("bfme2.object.").replace("-", "_")
		var structure_definition: Dictionary = content_db.get_bundle_object(structure_object_id)
		var structure_presentation: Dictionary = structure_definition.get("presentation", {}) as Dictionary
		var lifecycle: Dictionary = structure_presentation.get("buildingLifecycle", {}) as Dictionary
		var lifecycle_contract_error: String = structure_script.validate_lifecycle_contract(
			lifecycle,
			structure_kind,
			String(structure_presentation.get("model", "")),
			int(structure_spec["maximum"]),
			structure_object_id
		)
		var lifecycle_asset_error: String = structure_script.preflight_lifecycle_assets(
			lifecycle,
			structure_kind,
			selected
		)
		_check(
			"%s_exact_lifecycle_contract" % structure_label,
			not structure_definition.is_empty()
			and String(structure_definition.get("_pack_root", "")) == selected
			and lifecycle_contract_error == "",
			lifecycle_contract_error
		)
		_check("%s_all_lifecycle_assets_preflight" % structure_label, lifecycle_asset_error == "", lifecycle_asset_error)

		var lifecycle_assets := _lifecycle_asset_contract(lifecycle)
		var lifecycle_paths: Dictionary = lifecycle_assets["paths"]
		var lifecycle_clips: Dictionary = lifecycle_assets["clips"]
		for phase in ["construction", "intact", "damaged", "reallyDamaged", "rubble", "bib"]:
			var relative_path := String(lifecycle_paths.get(phase, ""))
			var phase_path: String = content_db.resolve_asset(relative_path, selected)
			var phase_label := "%s_%s" % [structure_label, phase]
			_check(
				"%s_contained_substantial_glb" % phase_label,
				phase_path != ""
				and mod_loader.path_is_within(selected, phase_path)
				and FileAccess.file_exists(phase_path)
				and FileAccess.get_file_as_bytes(phase_path).size() > 1024,
				phase_path
			)
			var phase_visual: Node3D = asset_factory._try_load_model(phase_path)
			if phase_visual != null:
				asset_factory._annotate_rig_and_animation(phase_visual, phase_visual)
				root.add_child(phase_visual)
				await process_frame
			var phase_metrics := _scene_metrics(phase_visual)
			var phase_clip_contract: Dictionary = lifecycle_clips.get(phase, {}) as Dictionary
			var phase_expected_clips: Array = phase_clip_contract.get("names", []) as Array
			_check(
				"%s_hierarchy_material" % phase_label,
				phase_visual != null
				and int(phase_metrics.get("meshes", 0)) >= 1
				and (phase_expected_clips.is_empty() or int(phase_metrics.get("skeletons", 0)) == 1)
				and int(phase_metrics.get("materials", 0)) >= 1,
				str(phase_metrics)
			)
			var phase_actual_clips: Array = phase_visual.get_meta("animation_clips", []) if phase_visual != null else []
			_check(
				"%s_declared_clips_exact" % phase_label,
				_clip_sets_equal(phase_actual_clips, phase_expected_clips),
				"actual=%s expected=%s" % [str(phase_actual_clips), str(phase_expected_clips)]
			)
			if phase_visual != null:
				phase_visual.queue_free()
				await process_frame
			asset_factory.clear_mesh_cache()

		if structure_kind == "fortress":
			var components: Dictionary = lifecycle.get("components", {}) as Dictionary
			var doors: Dictionary = components.get("door", {}) as Dictionary
			for door_phase in ["construction", "closed", "rubble"]:
				var door_contract: Dictionary = doors.get(door_phase, {}) as Dictionary
				var door_relative := String(door_contract.get("path", ""))
				var door_path: String = content_db.resolve_asset(door_relative, selected)
				var door_label := "men_fortress_door_%s" % door_phase
				_check(
					"%s_ready_contained_glb" % door_label,
					String(door_contract.get("status", "")) == "ready"
					and String(door_contract.get("resourceId", "")) != ""
					and door_path != ""
					and mod_loader.path_is_within(selected, door_path)
					and FileAccess.file_exists(door_path)
					and FileAccess.get_file_as_bytes(door_path).size() > 1024,
					door_path
				)
				var door_visual: Node3D = asset_factory._try_load_model(door_path)
				if door_visual != null:
					asset_factory._annotate_rig_and_animation(door_visual, door_visual)
					root.add_child(door_visual)
					await process_frame
				var door_metrics := _scene_metrics(door_visual)
				var door_clips: Array = door_visual.get_meta("animation_clips", []) if door_visual != null else []
				var expected_door_clip_count := 0 if door_phase == "closed" else 1
				_check(
					"%s_split_action_animation" % door_label,
					door_visual != null
					and int(door_metrics.get("skeletons", 0)) == 1
					and int(door_metrics.get("materials", 0)) >= 1
					and door_clips.size() == expected_door_clip_count,
					"metrics=%s clips=%s" % [str(door_metrics), str(door_clips)]
				)
				if door_visual != null:
					door_visual.queue_free()
					await process_frame
				asset_factory.clear_mesh_cache()

	var prop_path := selected.path_join("assets/models/props/ptgrass15-ba132fe13194.glb")
	_check("ptgrass15_model_substantial", FileAccess.get_file_as_bytes(prop_path).size() > 1024, prop_path)
	var prop_visual: Node3D = asset_factory._try_load_model(prop_path)
	if prop_visual != null:
		asset_factory._annotate_rig_and_animation(prop_visual, prop_visual)
		root.add_child(prop_visual)
		await process_frame
	var prop_metrics := _scene_metrics(prop_visual)
	_check("ptgrass15_static_material", prop_visual != null and int(prop_metrics.get("skeletons", 0)) == 0 and int(prop_metrics.get("materials", 0)) >= 1, str(prop_metrics))
	_check("ptgrass15_zero_clip_contract", prop_visual != null and (prop_visual.get_meta("animation_clips", []) as Array).is_empty())
	if prop_visual != null:
		prop_visual.queue_free()
		await process_frame
	asset_factory.clear_mesh_cache()

	var map_root := selected.path_join("maps/fords-of-isen-ii")
	var terrain_meta: Variant = mod_loader._read_json(map_root.path_join("terrain.json"))
	_check("terrain_source_layers_present", typeof(terrain_meta) == TYPE_DICTIONARY and _source_layer_files_valid(map_root, terrain_meta as Dictionary))
	var terrain_materials: Variant = mod_loader._read_json(map_root.path_join("materials/terrain-materials.json"))
	_check("terrain_material_manifest_66", typeof(terrain_materials) == TYPE_DICTIONARY and int((terrain_materials as Dictionary).get("symbolCount", 0)) == 66 and int((terrain_materials as Dictionary).get("textureCount", 0)) == 66 and (terrain_materials as Dictionary).get("materials", []).size() == 66)
	_check("terrain_material_png_closure", typeof(terrain_materials) == TYPE_DICTIONARY and _terrain_png_closure_valid(map_root.path_join("materials"), terrain_materials as Dictionary))
	var bindings: Variant = mod_loader._read_json(map_root.path_join("object-bindings.json"))
	var binding_summary: Dictionary = (bindings as Dictionary).get("summary", {}) if typeof(bindings) == TYPE_DICTIONARY else {}
	_check("object_binding_inventory_exact", typeof(bindings) == TYPE_DICTIONARY and (bindings as Dictionary).get("records", []).size() == 86 and int(binding_summary.get("placementCount", 0)) == 1384)
	_check("object_binding_status_exact", int(binding_summary.get("boundTypeCount", 0)) == 58 and int(binding_summary.get("boundPlacementCount", 0)) == 1257 and int(binding_summary.get("logicalTypeCount", 0)) == 26 and int(binding_summary.get("logicalPlacementCount", 0)) == 114 and int(binding_summary.get("resolvedTypeCount", 0)) == 84 and int(binding_summary.get("resolvedPlacementCount", 0)) == 1371 and int(binding_summary.get("unresolvedTypeCount", 0)) == 2 and int(binding_summary.get("unresolvedPlacementCount", 0)) == 13, str(binding_summary))
	_check("ptgrass15_binding_exact", typeof(bindings) == TYPE_DICTIONARY and _binding_matches(bindings as Dictionary, "PTGrass15", "assets/models/props/ptgrass15-ba132fe13194.glb"))

	var portrait_path: String = content_db.resolve_icon_path(definition)
	var portrait: Texture2D = asset_factory.load_texture_asset(portrait_path)
	var portrait_contract: Dictionary = content_db.get_retail_ui_image("UPGondor_Soldier")
	_check(
		"portrait_loaded",
		portrait != null
		and portrait_path == content_db.resolve_asset(String(portrait_contract.get("path", "")), selected)
		and portrait.get_width() == int(portrait_contract.get("width", -1))
		and portrait.get_height() == int(portrait_contract.get("height", -1))
	)
	portrait = null
	var bundle_manifest: Variant = mod_loader._read_json(selected.path_join("provenance/manifest.json"))
	var additional_portraits := [
		{"label": "gondor-archer", "id": "UPGondor_Archer"},
		{"label": "gondor-towerguard", "id": "UPGondor_TowerGuard"},
		{"label": "gondor-knight", "id": "UPGondor_Knight"},
	]
	for portrait_spec: Dictionary in additional_portraits:
		var portrait_id := String(portrait_spec["id"])
		var additional_portrait_contract: Dictionary = content_db.get_retail_ui_image(portrait_id)
		var additional_portrait_relative := String(additional_portrait_contract.get("path", ""))
		var additional_portrait_path := selected.path_join(additional_portrait_relative)
		var additional_portrait: Texture2D = asset_factory.load_texture_asset(additional_portrait_path)
		_check(
			"%s_portrait_png" % String(portrait_spec["label"]),
			typeof(bundle_manifest) == TYPE_DICTIONARY
				and _bundle_file_matches(bundle_manifest as Dictionary, selected, additional_portrait_relative)
				and additional_portrait != null
				and additional_portrait.get_width() == int(additional_portrait_contract.get("width", -1))
				and additional_portrait.get_height() == int(additional_portrait_contract.get("height", -1)),
			additional_portrait_path
		)
		additional_portrait = null
	var command_ref: String = String((definition.get("presentation", {}) as Dictionary).get("commandIcon", ""))
	var command_path: String = content_db.resolve_asset(command_ref, selected)
	var command_icon: Texture2D = asset_factory.load_texture_asset(command_path)
	_check(
		"command_icon_loaded",
		typeof(bundle_manifest) == TYPE_DICTIONARY
			and _bundle_file_matches(bundle_manifest as Dictionary, selected, command_ref)
			and command_icon != null
			and command_icon.get_width() == 64
			and command_icon.get_height() == 64
	)
	command_icon = null
	var additional_icon_ids := [
		"WOR_GondorArcher",
		"WOR_GondorTowerGuard",
		"WOR_GondorKnights",
		"BGBarracks_Soldiers",
		"BGArcheryRange_Archers",
		"BGBarracks_TowerGuard",
		"BGStables_Knights",
	]
	for additional_icon_id: String in additional_icon_ids:
		var additional_icon_contract: Dictionary = content_db.get_retail_ui_image(additional_icon_id)
		var additional_icon_relative := String(additional_icon_contract.get("path", ""))
		var additional_icon_path := selected.path_join(additional_icon_relative)
		var additional_icon: Texture2D = asset_factory.load_texture_asset(additional_icon_path)
		_check(
			"%s_icon_png" % additional_icon_id.to_snake_case(),
			typeof(bundle_manifest) == TYPE_DICTIONARY
				and _bundle_file_matches(bundle_manifest as Dictionary, selected, additional_icon_relative)
				and additional_icon != null
				and additional_icon.get_width() == int(additional_icon_contract.get("width", -1))
				and additional_icon.get_height() == int(additional_icon_contract.get("height", -1)),
			additional_icon_path
		)
		additional_icon = null

	game_audio._load_pack_audio()
	_check("explore_music_loaded", game_audio._streams.has("music_explore"))
	_check("battle_music_loaded", game_audio._streams.has("music_battle"))
	_check("victory_music_loaded", game_audio._streams.has("music_victory"))
	_check("defeat_music_loaded", game_audio._streams.has("music_defeat"))
	for music_key in ["explore", "battle", "victory", "defeat"]:
		var music_path := selected.path_join("assets/audio/music/%s.mp3" % music_key)
		_check("raw_music_%s" % music_key, game_audio._load_audio_stream(music_path) != null, music_path)
	var audio_sample_count: int = content_db.retail_audio_samples.size()
	# 1,542 roster samples plus the four retail HUD UI-sound leaves added by
	# bundle 69fd5efe.
	_check("audio_sample_closure_1546", audio_sample_count == 1546, "samples=%d" % audio_sample_count)
	var representative_voice_path: String = content_db.resolve_retail_audio_sample_path("GUSoldg_voisela")
	_check(
		"raw_voice_loads",
		mod_loader.path_is_within(selected, representative_voice_path)
		and representative_voice_path.ends_with("assets/audio/men/gusoldg_voisela.wav")
		and game_audio._load_audio_stream(representative_voice_path) != null,
		representative_voice_path
	)

	var forbidden := _find_forbidden_payloads(selected)
	_check("no_donor_runtime_payloads", forbidden.is_empty(), str(forbidden))
	var provenance: Variant = mod_loader._read_json(selected.path_join("provenance/manifest.json"))
	var provenance_sha_pattern := RegEx.new()
	provenance_sha_pattern.compile("^[0-9a-f]{64}$")
	_check(
		"provenance_present",
		typeof(provenance) == TYPE_DICTIONARY
		and String((provenance as Dictionary).get("contract", "")) == "openbfme.retail-import-provenance-v1"
		and not (provenance as Dictionary).get("entries", []).is_empty()
		and not (provenance as Dictionary).get("source_archives", []).is_empty()
		and (provenance as Dictionary).get("incomplete", []).is_empty()
		and provenance_sha_pattern.search(String((provenance as Dictionary).get("profile_sha256", ""))) != null
	)
	var provenance_text := FileAccess.get_file_as_string(selected.path_join("provenance/manifest.json"))
	var drive_path_pattern := RegEx.new()
	drive_path_pattern.compile("[A-Za-z]:[/\\\\]")
	_check("provenance_has_no_install_path", not provenance_text.contains("source_install") and drive_path_pattern.search(provenance_text) == null)

	game_audio.music_player.stop()
	game_audio.music_player.stream = null
	game_audio.sfx_player.stop()
	game_audio.sfx_player.stream = null
	game_audio._streams.clear()
	await process_frame
	_finish()


func _lifecycle_asset_contract(lifecycle: Dictionary) -> Dictionary:
	if int(lifecycle.get("schemaVersion", -1)) != 1:
		return {
			"paths": lifecycle.get("paths", {}) as Dictionary,
			"clips": lifecycle.get("clips", {}) as Dictionary,
		}
	var rows: Dictionary = {}
	for value in lifecycle.get("phases", []) as Array:
		if typeof(value) == TYPE_DICTIONARY:
			rows[String((value as Dictionary).get("phase", ""))] = value
	var phase_names := {
		"construction": "construction",
		"intact": "intact",
		"damaged": "damaged",
		"reallyDamaged": "really-damaged",
		"rubble": "collapsing",
	}
	var paths: Dictionary = {}
	var clips: Dictionary = {}
	for output_name in phase_names:
		var row: Dictionary = rows.get(phase_names[output_name], {}) as Dictionary
		var visual: Dictionary = row.get("visual", {}) as Dictionary
		var animation: Dictionary = row.get("animation", {}) as Dictionary
		paths[output_name] = String(visual.get("glb", ""))
		var names: Array = []
		var clip_value: Variant = animation.get("clip")
		var clip_name := "" if clip_value == null else String(clip_value)
		if clip_name != "":
			names.append(clip_name)
		for alternate in animation.get("alternateClips", []) as Array:
			names.append(String(alternate))
		clips[output_name] = {
			"mode": String(animation.get("mode", "none")),
			"names": names,
		}
	var bib: Dictionary = lifecycle.get("bib", {}) as Dictionary
	var bib_visual: Dictionary = bib.get("visual", {}) as Dictionary
	paths["bib"] = String(bib_visual.get("glb", ""))
	clips["bib"] = {"mode": "none", "names": []}
	return {"paths": paths, "clips": clips}


func _clip_list_contains(clips: Array, expected: String) -> bool:
	for clip in clips:
		var value := String(clip).to_lower()
		if value == expected or value.ends_with("/" + expected):
			return true
	return false


func _clip_list_contains_all(clips: Array, expected: Array) -> bool:
	for name in expected:
		if not _clip_list_contains(clips, String(name)):
			return false
	return true


func _clip_sets_equal(actual: Array, expected: Array) -> bool:
	return actual.size() == expected.size() and _clip_list_contains_all(actual, expected)


func _source_layer_files_valid(map_root: String, terrain: Dictionary) -> bool:
	var source_layers: Dictionary = terrain.get("sourceLayers", {})
	var checked := 0
	for group_name in ["layers", "descriptionTables"]:
		var group: Dictionary = source_layers.get(group_name, {})
		for key in group:
			var entry: Dictionary = group[key]
			var path := map_root.path_join(String(entry.get("path", "")))
			if not FileAccess.file_exists(path):
				return false
			if FileAccess.get_file_as_bytes(path).size() != int(entry.get("byteLength", -1)):
				return false
			if FileAccess.get_sha256(path).to_lower() != String(entry.get("sha256", "")).to_lower():
				return false
			checked += 1
	return checked == 6


func _terrain_png_closure_valid(material_root: String, manifest: Dictionary) -> bool:
	var textures: Array = manifest.get("textures", [])
	if textures.size() != 66:
		return false
	for value in textures:
		var entry: Dictionary = value
		var path := material_root.path_join(String(entry.get("png", "")))
		if not FileAccess.file_exists(path) or FileAccess.get_file_as_bytes(path).is_empty():
			return false
		if FileAccess.get_sha256(path).to_lower() != String(entry.get("pngSha256", "")).to_lower():
			return false
	return true


func _bundle_file_matches(manifest: Dictionary, root_path: String, relative_path: String) -> bool:
	var path := root_path.path_join(relative_path)
	if not FileAccess.file_exists(path):
		return false
	for value in manifest.get("bundle_files", []):
		var entry: Dictionary = value
		if String(entry.get("path", "")) != relative_path:
			continue
		return (
			FileAccess.get_file_as_bytes(path).size() == int(entry.get("size", -1))
			and FileAccess.get_sha256(path).to_lower() == String(entry.get("sha256", "")).to_lower()
		)
	return false


func _binding_matches(document: Dictionary, type_name: String, glb: String) -> bool:
	for value in document.get("records", []):
		var entry: Dictionary = value
		if String(entry.get("typeName", "")) == type_name:
			return String(entry.get("status", "")) == "bound" and String(entry.get("matchMethod", "")) == "exact-type-name" and String(entry.get("glb", "")) == glb
	return false


func _scene_metrics(node: Node) -> Dictionary:
	var result := {"meshes": 0, "skeletons": 0, "players": 0, "materials": 0}
	if node == null:
		return result
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is MeshInstance3D:
			result["meshes"] += 1
			var mesh := (current as MeshInstance3D).mesh
			if mesh:
				for surface in range(mesh.get_surface_count()):
					if mesh.surface_get_material(surface) != null:
						result["materials"] += 1
		elif current is Skeleton3D:
			result["skeletons"] += 1
		elif current is AnimationPlayer:
			result["players"] += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _count_extension(path: String, extension: String) -> int:
	var directory := DirAccess.open(path)
	if directory == null:
		return 0
	var count := 0
	directory.list_dir_begin()
	var name := directory.get_next()
	while name != "":
		if not directory.current_is_dir() and name.to_lower().ends_with(extension):
			count += 1
		name = directory.get_next()
	directory.list_dir_end()
	return count


func _find_forbidden_payloads(root_path: String) -> Array[String]:
	var forbidden: Array[String] = []
	var stack: Array[String] = [root_path]
	while not stack.is_empty():
		var current: String = stack.pop_back()
		var directory := DirAccess.open(current)
		if directory == null:
			continue
		directory.list_dir_begin()
		var name := directory.get_next()
		while name != "":
			if not name.begins_with("."):
				var child: String = current.path_join(name)
				if directory.current_is_dir():
					if not (current == root_path and name == "sources"):
						stack.append(child)
				elif name.get_extension().to_lower() in ["big", "w3d", "dds", "tga", "map"]:
					forbidden.append(child)
			name = directory.get_next()
		directory.list_dir_end()
	return forbidden


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_PACK PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_PACK FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_PACK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
