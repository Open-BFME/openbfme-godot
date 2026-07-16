extends SceneTree

const SelectionScript = preload("res://src/retail_slice/retail_selection_decal.gd")
const LOCAL_SCALE := 0.02649232738129

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_check("source_identity_constants", SelectionScript.CONTRACT_SCHEMA == "openbfme.men-selection-decal" and SelectionScript.SOURCE_STYLE == "SHADOW_MERGE_DECAL")
	_check(
		"player_selection_color_is_retail_oracle_blue",
		SelectionScript.PLAYER_SELECTION_COLOR.is_equal_approx(Color(0.314, 0.380, 0.624, 1.0))
	)
	_check("source_member_limit", SelectionScript.SOURCE_MEMBER_LIMIT == 40)
	_check("source_texture_dimensions", SelectionScript.SOURCE_TEXTURE_SIZE == Vector2i(256, 256))

	var missing := SelectionScript.new()
	root.add_child(missing)
	var positions := _formation_positions()
	var missing_error: String = missing.configure(ProjectSettings.globalize_path("user://missing-selection-pack"), LOCAL_SCALE, positions)
	_check("missing_contract_fails_closed", missing_error.contains("no Men selection decal contract") and not missing.contract_ready)
	missing.queue_free()

	var pack_root := ProjectSettings.globalize_path("user://retail-selection-decal-fixture")
	_write_fixture(pack_root)
	var selection := SelectionScript.new()
	root.add_child(selection)
	var configure_error: String = selection.configure(pack_root, LOCAL_SCALE, positions)
	_check("exact_contract_configures", configure_error == "" and selection.contract_ready, configure_error)
	_check("exact_source_bounds_retained", is_equal_approx(selection.source_min_radius, 50.0) and is_equal_approx(selection.source_max_radius, 200.0) and is_equal_approx(selection.source_opacity_min, 0.8) and is_equal_approx(selection.source_opacity_max, 1.0))
	var lazy_decal := selection.find_child("SourceMenSelectionMergeDecal", false, false) as Decal
	_check("pixel_merge_is_deferred_until_first_selection", lazy_decal != null and lazy_decal.texture_albedo == null)
	selection.set_selected(true)
	_check("all_fifteen_members_merge", selection.source_member_count == 15 and selection.merged_texture_size.x > 0 and selection.merged_texture_size.y > 0)
	_check("one_projected_decal_not_fifteen_rings", selection.projected_decal_count() == 1 and selection.find_child("SourceMenSelectionMergeDecal", false, false) is Decal)
	var decal := selection.find_child("SourceMenSelectionMergeDecal", false, false) as Decal
	var merged_image := decal.texture_albedo.get_image() if decal != null and decal.texture_albedo != null else null
	_check(
		"merged_outline_has_no_opaque_interior_carpet",
		merged_image != null
			and merged_image.get_pixel(merged_image.get_width() / 2, merged_image.get_height() / 2).a < 0.05
	)
	_check("selection_visibility_is_authoritative", decal != null and decal.visible)
	var original_size := selection.merged_texture_size
	var engagement_positions := positions.duplicate()
	engagement_positions[0] = engagement_positions[0] + Vector3(-2.0, 0.0, -1.0)
	selection.set_member_positions(engagement_positions)
	_check(
		"member_reflow_rebuilds_source_merge_footprint",
		selection.merged_texture_size.x > original_size.x
			and selection.merged_texture_size.y > original_size.y,
		"%s -> %s" % [str(original_size), str(selection.merged_texture_size)]
	)
	var center_columns_only: Array[bool] = []
	for index in range(15):
		center_columns_only.append(index % 5 > 0 and index % 5 < 4)
	selection.set_living_mask(center_columns_only)
	_check("dead_outer_members_shrink_merged_footprint", selection.merged_texture_size.x < original_size.x and selection.merged_texture_size.y == original_size.y)
	_check("source_status_keeps_oracle_gap_explicit", selection.source_status.contains("source-textures-radii-and-merge-style-bound") and selection.source_status.contains("oracle-color-throb-pending"))
	selection.queue_free()
	await process_frame

	print("RETAIL_SELECTION_DECAL_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _formation_positions() -> Array[Vector3]:
	var result: Array[Vector3] = []
	for index in range(15):
		var column := index % 5
		var row := index / 5
		result.append(Vector3((float(column) - 2.0) * 1.4, 0.0, (float(row) - 1.0) * 1.25))
	return result


func _write_fixture(pack_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("effects"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/textures/selection"))
	var texture_paths := [
		"assets/textures/selection/decal_g_level1.png",
		"assets/textures/selection/decal_good_co.png",
	]
	for texture_path in texture_paths:
		var image := Image.create(256, 256, false, Image.FORMAT_RGBA8)
		image.fill(Color.TRANSPARENT)
		image.fill_rect(Rect2i(32, 32, 192, 192), Color.WHITE)
		image.save_png(pack_root.path_join(texture_path))
	var contract := {
		"schema": "openbfme.men-selection-decal",
		"schemaVersion": 0,
		"experienceLevel": "MenGenericLevel1",
		"style": "SHADOW_MERGE_DECAL",
		"textures": texture_paths,
		"opacityMin": 0.8,
		"opacityMax": 1.0,
		"minRadiusSource": 50.0,
		"maxRadiusSource": 200.0,
		"maxSelectedUnits": 40,
		"source": {
			"ini": "data/ini/experiencelevels.ini",
			"textures": SelectionScript.SOURCE_TEXTURE_EVIDENCE,
		},
	}
	var file := FileAccess.open(pack_root.path_join("effects/men-selection-decal.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(contract))
	file.close()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_SELECTION_DECAL PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_SELECTION_DECAL FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
