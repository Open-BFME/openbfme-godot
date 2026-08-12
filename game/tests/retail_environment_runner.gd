extends SceneTree
## Focused gate for source-proven Fords AFTERNOON/NORMAL environment binding.

const MAP_ID := "bfme2.map.fords-of-isen-ii"
const ORACLE_SHA256 := "c1f300fcf6fed6f225d1b04f50b14fab04883641d8b5b36762be6cfcb58e9a59"
const SOURCE_PATH := "res://src/retail_slice/retail_vertical_slice.gd"

var passed := 0
var failed := 0
var finished := false


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_ENVIRONMENT_RUNNER")
	create_timer(30.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var slice_script = load(SOURCE_PATH)
	_check("environment_runner_dependencies", mod_loader != null and map_data_script != null and slice_script != null)
	if mod_loader == null or map_data_script == null or slice_script == null:
		_finish()
		return

	var map_data = _load_runtime_map_data(mod_loader, map_data_script)
	_check("exact_fords_runtime_map_loads", map_data != null and bool(map_data.ready), String(map_data.error) if map_data != null else "no private runtime pack")
	if map_data == null or not map_data.ready:
		_finish()
		return

	var slice = slice_script.new()
	slice.source_map_data = map_data
	slice._build_environment()
	var configure_error: String = slice._configure_source_environment()
	_check("source_environment_configures", configure_error == "", configure_error)
	var environment: Environment = slice.world_environment.environment if slice.world_environment != null else null
	var metadata: Dictionary = slice.environment_runtime_metadata
	var sky: Dictionary = metadata.get("sky", {}) as Dictionary
	var water_reflection: Dictionary = metadata.get("water_reflection", {}) as Dictionary
	var fog: Dictionary = metadata.get("fog", {}) as Dictionary
	var lighting: Dictionary = metadata.get("lighting", {}) as Dictionary
	var camera_data: Dictionary = metadata.get("camera", {}) as Dictionary
	var source_text := FileAccess.get_file_as_string(SOURCE_PATH)
	var environment_path := String(map_data.map_root).path_join("environment.json")
	var environment_document := mod_loader._read_json(environment_path) as Dictionary
	var authored_fog := environment_document.get("fog", {}) as Dictionary

	_check("oracle_identity_is_pinned", String(metadata.get("oracle_sha256", "")) == ORACLE_SHA256 and String(slice.FORDS_ENVIRONMENT_ORACLE_SHA256) == ORACLE_SHA256)
	_check("active_fords_time_and_weather_are_exact", String(metadata.get("time_of_day", "")) == "AFTERNOON" and String(metadata.get("weather", "")) == "NORMAL")
	_check("retail_skybox_contract_is_identified_without_guessing_texture_set", bool(sky.get("draw_skybox_in_source", false)) and String(sky.get("model_source", "")) == "art/w3d/ne/new_skybox.w3d" and String(sky.get("texture_set_source", "")) == "data/ini/environment.ini" and not bool(sky.get("map_texture_set_override_present", true)) and String(sky.get("status", "")).contains("unresolved") and String(sky.get("runtime_background", "")) == "neutral-black-map-edge-backdrop")
	_check("skyenv_is_classified_as_water_reflection_not_world_sky", String(water_reflection.get("source_leaf", "")) == "SkyEnv.tga" and int(water_reflection.get("standing_water_area_count", 0)) == 4 and String(water_reflection.get("status", "")).contains("unresolved") and not sky.has("source_leaf"))
	# Owner-directed dark-neutral placeholder pending the named retail
	# new_skybox.w3d + environment.ini texture-set closure. Per-map fog data
	# controls distance fog only; it does not select this backdrop.
	_check("map_edge_void_uses_neutral_black_backdrop", environment != null and environment.background_mode == Environment.BG_COLOR and _color_near(environment.background_color, Color.BLACK) and environment.sky == null)
	_check("procedural_sky_class_is_absent", not source_text.contains("ProceduralSkyMaterial"))

	var local_scale := float(map_data.local_transform_scale)
	var authored_start := float(authored_fog.get("start", -1.0))
	var authored_end := float(authored_fog.get("end", -1.0))
	_check("loaded_map_environment_document_is_valid", String(environment_document.get("schema", "")) == "openbfme.fords-environment" and String(environment_document.get("mapId", "")) == String(map_data.map_id) and String(authored_fog.get("mode", "")) == "linear")
	_check("applied_fog_traces_loaded_map_document", String(fog.get("compiled_document", "")) == environment_path and String(fog.get("source_path", "")) == "maps/map mp fords of isen ii/map.ini")
	_check("fog_source_values_are_exact", bool(fog.get("enabled_in_source", false)) and _color_near(fog.get("color", Color.TRANSPARENT), Color(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0, 1.0)) and is_equal_approx(float(fog.get("start_source", 0.0)), authored_start) and is_equal_approx(float(fog.get("end_source", 0.0)), authored_end))
	_check("fog_distances_use_exact_map_scale", is_equal_approx(float(fog.get("start_local", 0.0)), authored_start * local_scale) and is_equal_approx(float(fog.get("end_local", 0.0)), authored_end * local_scale))
	var fog_contract := fog.get("runtime_contract", {}) as Dictionary
	_check("exact_linear_fog_compositor_is_bound", environment != null and not environment.fog_enabled and slice.world_environment != null and slice.world_environment.compositor != null and slice.linear_fog != null and slice.linear_fog.is_configured() and bool(fog.get("runtime_enabled", false)) and _color_near(environment.fog_light_color, Color(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0, 1.0)))
	_check("linear_fog_contract_uses_loaded_scaled_curve", is_equal_approx(float(fog_contract.get("source_start", 0.0)), authored_start) and is_equal_approx(float(fog_contract.get("source_end", 0.0)), authored_end) and is_equal_approx(float(fog_contract.get("local_units_per_source_unit", 0.0)), local_scale) and is_equal_approx(float(fog_contract.get("fog_start_local", 0.0)), authored_start * local_scale) and is_equal_approx(float(fog_contract.get("fog_end_local", 0.0)), authored_end * local_scale) and String(fog_contract.get("curve", "")).begins_with("clamp"))
	_check("presenter_has_no_hardcoded_fog_distance_binding", not source_text.contains("linear_fog.configure_fords") and not source_text.contains("FORDS_FOG_START_SOURCE * local_scale") and not source_text.contains("FORDS_FOG_END_SOURCE * local_scale"))
	_check("linear_fog_gpu_oracle_gap_remains_explicit", String(fog.get("renderer_status", "")).contains("rendered-gate") and String(fog_contract.get("transparent_depth_status", "")).contains("unresolved") and String(fog_contract.get("sky_depth_status", "")).contains("reverse-z clear depth"))
	_check("old_exponential_fog_guesses_are_removed", not source_text.contains("fog_density") and not source_text.contains("fog_height") and not source_text.contains("536f70"))

	_check("camera_source_constraints_are_exact", is_equal_approx(float(camera_data.get("minimum_height_source", 0.0)), 120.0) and is_equal_approx(float(camera_data.get("maximum_height_source", 0.0)), 300.0) and is_equal_approx(float(camera_data.get("pitch_above_horizontal_degrees", 0.0)), 37.5) and is_equal_approx(float(camera_data.get("authored_yaw_degrees", INF)), 0.0) and is_equal_approx(float(camera_data.get("scroll_speed_scalar", 0.0)), 1.0))
	_check("camera_ground_bounds_are_exact", is_equal_approx(float(camera_data.get("ground_minimum_source", 0.0)), 260.0) and is_equal_approx(float(camera_data.get("ground_maximum_source", 0.0)), 380.0))
	_check("camera_distances_use_exact_map_scale", is_equal_approx(float(camera_data.get("minimum_height_local", 0.0)), 120.0 * local_scale) and is_equal_approx(float(camera_data.get("maximum_height_local", 0.0)), 300.0 * local_scale))
	_check("common_source_named_camera_fov_is_bound", is_equal_approx(deg_to_rad(float(slice.camera.fov)), 0.8726646304130554) and is_equal_approx(float(camera_data.get("common_named_camera_fov_radians", 0.0)), 0.8726646304130554))
	_check("skirmish_camera_starts_at_source_max_height", is_equal_approx(float(slice.camera_zoom), 1.0) and is_equal_approx(float(slice.camera_zoom_target), 1.0) and is_equal_approx(float(camera_data.get("current_height_source", 0.0)), 300.0))
	_check("camera_pitch_geometry_is_exact", _camera_pitch_is_exact(slice, map_data))
	_check("camera_home_yaw_source_offset_is_exact", _camera_yaw_is_exact(camera_data))
	_check("camera_live_yaw_metadata_is_published", _camera_live_yaw_is_published(camera_data))
	_check("camera_ground_sample_stays_in_source_bounds", _camera_ground_is_bounded(slice, map_data))
	_check("camera_focus_reaches_authored_map_bounds", _camera_focus_clamps_to_authored_bounds(slice, map_data))

	_check("three_source_lighting_domains_are_retained", int(lighting.get("source_domain_count", 0)) == 3 and int(lighting.get("lights_per_domain", 0)) == 3 and lighting.get("runtime_directional_domains", []) == ["object", "infantry"] and int(lighting.get("runtime_directional_light_count", 0)) == 6 and slice.source_environment_lights.size() == 6)
	_check("source_light_values_and_basis_are_exact", _all_source_lights_are_exact(slice))
	_check("source_lighting_domains_have_distinct_cull_masks", _lighting_masks_are_exact(slice))
	_check("retail_terrain_and_object_ambient_are_bound", environment != null and environment.ambient_light_source == Environment.AMBIENT_SOURCE_COLOR and _color_near(environment.ambient_light_color, Color(11.0 / 255.0, 9.0 / 255.0, 11.0 / 255.0, 1.0)) and is_equal_approx(environment.ambient_light_energy, 1.0) and int(lighting.get("nonzero_ambient_row_count", 0)) == 3 and bool(lighting.get("ambient_is_domain_specific", false)) and bool(lighting.get("ambient_runtime_enabled", false)) and lighting.get("ambient_runtime_domains", []) == ["terrain", "object"] and lighting.get("ambient_unresolved_domains", []) == [] and slice._source_ambient_sum("terrain").is_equal_approx(Vector3(22.0 / 255.0, 18.0 / 255.0, 22.0 / 255.0)) and slice._source_ambient_sum("object").is_equal_approx(Vector3(11.0 / 255.0, 9.0 / 255.0, 11.0 / 255.0)) and slice._source_ambient_sum("infantry").is_equal_approx(Vector3.ZERO) and String(lighting.get("ambient_status", "")).contains("object-ambient-scene-approved-equivalence"))
	# Approved equivalence: retail draws shadows via decals (shadow mapping
	# disabled in source); the runtime binds blob decals at the exact source
	# shadow color while directional shadow maps stay off.
	_check("retail_shadow_decal_equivalence_is_bound", _all_shadows_disabled(slice) and bool(lighting.get("source_shadow_volumes", false)) and bool(lighting.get("source_shadow_decals", false)) and not bool(lighting.get("source_shadow_mapping", true)) and int(lighting.get("source_shadow_color_argb", -1)) == 1073741824 and _color_near(lighting.get("source_shadow_color", Color.TRANSPARENT), Color(0.0, 0.0, 0.0, 64.0 / 255.0)) and bool(lighting.get("shadow_runtime_enabled", false)) and String(lighting.get("shadow_status", "")).contains("approved-decal-equivalence"))
	_check("invented_color_grade_and_sun_are_removed", environment != null and environment.tonemap_mode == Environment.TONE_MAPPER_LINEAR and not environment.adjustment_enabled and not source_text.contains("ffe8bd") and not source_text.contains("rotation_degrees = Vector3(-58"))
	_check("geometry_domain_assignment_is_deterministic", _geometry_layer_assignment_works(slice))

	var expected_zoom := 1.0 - 10.0 / (300.0 - 120.0)
	slice._nudge_camera_zoom(-1)
	_check("mouse_wheel_uses_exact_ten_source_unit_step", is_equal_approx(float(slice.camera_zoom), expected_zoom) and is_equal_approx(float(slice.camera_zoom_target), expected_zoom) and is_equal_approx(float((slice.environment_runtime_metadata.get("camera", {}) as Dictionary).get("current_height_source", 0.0)), 290.0))
	_check("unresolved_keyboard_base_rate_is_not_claimed_as_retail", String(camera_data.get("keyboard_base_status", "")).contains("unresolved") and is_equal_approx(float(slice.FORDS_CAMERA_SCROLL_SPEED_SCALAR), 1.0))

	print("RETAIL_ENVIRONMENT_METRICS oracle=%s scale=%.9f fog_start=%.6f fog_end=%.6f lights=%d fov=%.6f camera_height=%.6f" % [
		ORACLE_SHA256,
		local_scale,
		float(fog.get("start_local", 0.0)),
		float(fog.get("end_local", 0.0)),
		slice.source_environment_lights.size(),
		float(slice.camera.fov),
		float((slice.environment_runtime_metadata.get("camera", {}) as Dictionary).get("current_height_local", 0.0)),
	])
	slice.free()
	_finish()


func _load_runtime_map_data(mod_loader, map_data_script):
	var explicit_root := OS.get_environment("OPENBFME_TERRAIN_PACK").replace("\\", "/").trim_suffix("/")
	var pack_root := explicit_root
	if pack_root == "":
		var content_root := OS.get_environment("OPENBFME_CONTENT").replace("\\", "/").trim_suffix("/")
		var selection_path := content_root.path_join("selection.json") if content_root != "" else ""
		pack_root = mod_loader.selected_user_pack_root(content_root, selection_path) if selection_path != "" else ""
	if pack_root == "" or pack_root.begins_with("res://") or not DirAccess.dir_exists_absolute(pack_root):
		return null
	var pack_value: Variant = mod_loader._read_json(pack_root.path_join("pack.json"))
	if typeof(pack_value) != TYPE_DICTIONARY:
		return null
	var entry_map := String(((pack_value as Dictionary).get("files", {}) as Dictionary).get("entryMap", ""))
	var map_path: String = mod_loader.resolve_pack_path(pack_root, entry_map)
	var map_value: Variant = mod_loader._read_json(map_path) if map_path != "" else null
	if typeof(map_value) != TYPE_DICTIONARY:
		return null
	var map_definition := (map_value as Dictionary).duplicate(true)
	if String(map_definition.get("id", "")) != MAP_ID:
		return null
	map_definition["_source"] = map_path
	map_definition["_pack_root"] = pack_root
	var map_data = map_data_script.new()
	map_data.load_from_pack(pack_root, map_definition)
	return map_data


func _camera_pitch_is_exact(slice, map_data) -> bool:
	var camera_data := slice.environment_runtime_metadata.get("camera", {}) as Dictionary
	var offset: Vector3 = camera_data.get("current_local_offset", Vector3.ZERO)
	var local_height := 300.0 * float(map_data.local_transform_scale)
	var horizontal := Vector2(offset.x, offset.z).length()
	return is_equal_approx(offset.y, local_height) and is_equal_approx(rad_to_deg(atan2(offset.y, horizontal)), 37.5)


func _camera_yaw_is_exact(camera_data: Dictionary) -> bool:
	# Owner-feel per-seat heading (camera_home_yaw) changes the source
	# offset's DIRECTION; its magnitude stays the exact gamedata.ini depth
	# at the default 300 height. This detached runner has home yaw 0.
	var source_offset: Vector3 = camera_data.get("current_source_offset", Vector3.INF)
	var expected_depth := 300.0 / tan(deg_to_rad(37.5))
	return is_equal_approx(Vector2(source_offset.x, source_offset.y).length(), expected_depth) and is_equal_approx(source_offset.z, 300.0)


func _camera_live_yaw_is_published(camera_data: Dictionary) -> bool:
	# yaw_degrees is the live heading (authored + home + user). authored_yaw_degrees
	# is the gamedata.ini default (0.0). This detached runner has no seat, so
	# live == authored == 0. A live match publishes the per-seat heading here.
	var authored := float(camera_data.get("authored_yaw_degrees", INF))
	var live := float(camera_data.get("live_yaw_degrees", INF))
	var published := float(camera_data.get("yaw_degrees", INF))
	return is_equal_approx(authored, 0.0) and is_equal_approx(live, published) and is_finite(live)


func _camera_ground_is_bounded(slice, map_data) -> bool:
	var ground_local := float((slice.environment_runtime_metadata.get("camera", {}) as Dictionary).get("current_target_ground_local", INF))
	var ground_source := float(map_data.reference_elevation) + ground_local / float(map_data.local_transform_scale)
	return ground_source >= 260.0 - 0.0001 and ground_source <= 380.0 + 0.0001


func _camera_focus_clamps_to_authored_bounds(slice, map_data) -> bool:
	# The look-at must reach the authored playable QUAD (map_outline), not
	# the local AABB of that quad. Push past one edge midpoint; the clamp
	# must land on that midpoint. No screen-margin inset.
	var outline: PackedVector2Array = map_data.map_outline
	if outline.size() < 4:
		return false
	var a: Vector2 = outline[0]
	var b: Vector2 = outline[1]
	var mid := (a + b) * 0.5
	var edge := b - a
	var n := Vector2(edge.y, -edge.x).normalized()
	if Geometry2D.is_point_in_polygon(mid + n * 0.1, outline):
		n = -n
	slice.camera_focus = mid + n * 16.0
	slice._clamp_camera_focus()
	var focus: Vector2 = slice.camera_focus
	return focus.is_equal_approx(mid)


func _all_source_lights_are_exact(slice) -> bool:
	var expected_count := 0
	for domain in ["object", "infantry"]:
		var rows: Array = slice.FORDS_AFTERNOON_LIGHT_RIGS[domain]
		for row_value in rows:
			var row := row_value as Dictionary
			if expected_count >= slice.source_environment_lights.size():
				return false
			var light: DirectionalLight3D = slice.source_environment_lights[expected_count]
			var source_color: Vector3 = slice._array_vector3(row.get("color", []))
			var source_ambient: Vector3 = slice._array_vector3(row.get("ambient", []))
			var source_direction: Vector3 = slice._array_vector3(row.get("direction", []))
			var local_direction: Vector3 = slice._sage_direction_to_local(source_direction)
			if String(light.get_meta("retail_domain", "")) != domain or String(light.get_meta("retail_light_name", "")) != String(row.get("name", "")) or not _color_near(light.light_color, Color(source_color.x, source_color.y, source_color.z, 1.0)) or not Vector3(light.get_meta("source_ambient", Vector3.INF)).is_equal_approx(source_ambient) or not Vector3(light.get_meta("source_ray_direction_sage", Vector3.INF)).is_equal_approx(source_direction) or not Vector3(light.get_meta("source_ray_direction_local", Vector3.INF)).is_equal_approx(local_direction) or not (-light.basis.z).is_equal_approx(local_direction):
				return false
			expected_count += 1
	return expected_count == 6


func _lighting_masks_are_exact(slice) -> bool:
	var expected := {
		"terrain": int(slice.TERRAIN_LIGHT_LAYER),
		"object": int(slice.OBJECT_LIGHT_LAYER),
		"infantry": int(slice.INFANTRY_LIGHT_LAYER),
	}
	for light in slice.source_environment_lights:
		var domain := String(light.get_meta("retail_domain", ""))
		if not expected.has(domain) or int(light.light_cull_mask) != int(expected[domain]):
			return false
	return true


func _all_shadows_disabled(slice) -> bool:
	for light in slice.source_environment_lights:
		if light.shadow_enabled:
			return false
	return true


func _geometry_layer_assignment_works(slice) -> bool:
	var probe := Node3D.new()
	var child := MeshInstance3D.new()
	var nested := Node3D.new()
	var grandchild := MeshInstance3D.new()
	probe.add_child(child)
	probe.add_child(nested)
	nested.add_child(grandchild)
	slice._assign_geometry_light_layer(probe, int(slice.OBJECT_LIGHT_LAYER))
	var result := child.layers == int(slice.OBJECT_LIGHT_LAYER) and grandchild.layers == int(slice.OBJECT_LIGHT_LAYER)
	probe.free()
	return result


func _color_near(value: Variant, expected: Color) -> bool:
	if not value is Color:
		return false
	var actual := value as Color
	return is_equal_approx(actual.r, expected.r) and is_equal_approx(actual.g, expected.g) and is_equal_approx(actual.b, expected.b) and is_equal_approx(actual.a, expected.a)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_ENVIRONMENT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_ENVIRONMENT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if finished:
		return
	finished = true
	print("RETAIL_ENVIRONMENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _watchdog_timeout() -> void:
	if finished:
		return
	failed += 1
	printerr("RETAIL_ENVIRONMENT FAIL watchdog_timeout")
	_finish()
