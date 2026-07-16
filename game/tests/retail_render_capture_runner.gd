extends SceneTree
## Non-headless private capture gate for the composed Men/Fords retail slice.

const SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const REQUIRED_PRIVATE_FRAGMENT := "/.private/scratch/rendered-qa/"
const MAX_READY_FRAMES := 600
const SETTLE_FRAMES := 12


func _initialize() -> void:
	create_timer(120.0, true, false, true).timeout.connect(_fail.bind("render capture watchdog timeout"))
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		_fail("render capture requires a non-headless Forward+ window")
		return
	var output := OS.get_environment("OPENBFME_CAPTURE_PATH").replace("\\", "/")
	if output == "" or not output.contains(REQUIRED_PRIVATE_FRAGMENT) or output.get_extension().to_lower() != "png":
		_fail("OPENBFME_CAPTURE_PATH must be a PNG below .private/scratch/rendered-qa")
		return
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(Vector2i(1920, 1080))
	root.size = Vector2i(1920, 1080)
	await process_frame
	await process_frame
	var packed := load(SCENE_PATH) as PackedScene
	if packed == null:
		_fail("retail vertical-slice scene did not load")
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	for _index in MAX_READY_FRAMES:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not bool(slice.ready_ok):
		_fail("retail slice did not become ready: %s" % String(slice.failure_reason))
		return
	var focus_text := OS.get_environment("OPENBFME_CAPTURE_CAMERA_FOCUS")
	var focus_battalion_text := OS.get_environment("OPENBFME_CAPTURE_FOCUS_BATTALION")
	if focus_battalion_text != "":
		if not focus_battalion_text.is_valid_int() or not slice.battalion_nodes.has(int(focus_battalion_text)):
			_fail("OPENBFME_CAPTURE_FOCUS_BATTALION must name a live battalion")
			return
		var focus_entity: Dictionary = slice.simulation.entity(int(focus_battalion_text))
		slice.camera_focus = Vector2(focus_entity.get("position", Vector2.ZERO))
	if focus_text != "":
		var focus_parts := focus_text.split(",", false)
		if focus_parts.size() != 2 or not String(focus_parts[0]).is_valid_float() or not String(focus_parts[1]).is_valid_float():
			_fail("OPENBFME_CAPTURE_CAMERA_FOCUS must be x,z")
			return
		slice.camera_focus = Vector2(float(focus_parts[0]), float(focus_parts[1]))
	var zoom_text := OS.get_environment("OPENBFME_CAPTURE_CAMERA_ZOOM")
	if zoom_text != "":
		if not zoom_text.is_valid_float() or float(zoom_text) < 0.0 or float(zoom_text) > 1.0:
			_fail("OPENBFME_CAPTURE_CAMERA_ZOOM must be in 0..1")
			return
		slice.camera_zoom = float(zoom_text)
		slice.camera_zoom_target = float(zoom_text)
	if focus_text != "" or focus_battalion_text != "" or zoom_text != "":
		if OS.get_environment("OPENBFME_CAPTURE_UNCLAMPED") != "1":
			slice._clamp_camera_focus()
		slice._apply_camera_transform()
	var selected_text := OS.get_environment("OPENBFME_CAPTURE_SELECT_BATTALION")
	if selected_text != "":
		if not selected_text.is_valid_int() or not slice.battalion_nodes.has(int(selected_text)):
			_fail("OPENBFME_CAPTURE_SELECT_BATTALION must name a live battalion")
			return
		if not bool(slice.simulation.select_only(int(selected_text))):
			_fail("OPENBFME_CAPTURE_SELECT_BATTALION could not select the battalion")
			return
		slice._sync_presentation()
	var move_offset_text := OS.get_environment("OPENBFME_CAPTURE_MOVE_OFFSET").strip_edges()
	if move_offset_text != "":
		if selected_text == "":
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET requires OPENBFME_CAPTURE_SELECT_BATTALION")
			return
		var move_parts := move_offset_text.split(",", false)
		if move_parts.size() != 2 or not String(move_parts[0]).is_valid_float() or not String(move_parts[1]).is_valid_float():
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET must be x,z")
			return
		var selected_entity: Dictionary = slice.simulation.entity(int(selected_text))
		var selected_position := selected_entity.get("position", Vector2.ZERO) as Vector2
		var destination := selected_position + Vector2(float(move_parts[0]), float(move_parts[1]))
		if int(slice.test_move(destination)) != 1:
			_fail("OPENBFME_CAPTURE_MOVE_OFFSET could not issue the authoritative move")
			return
		slice.camera_focus = selected_position.lerp(destination, 0.5)
		slice._clamp_camera_focus()
		slice._apply_camera_transform()
		slice._sync_presentation()
		print("RETAIL_RENDER_MOVE_HINT battalion=%s destination=%s" % [selected_text, destination])
	var attack_pair_text := OS.get_environment("OPENBFME_CAPTURE_ATTACK_PAIR").strip_edges()
	if attack_pair_text != "" and not _pose_attack_pair(slice, attack_pair_text):
		return
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_FOG") == "1":
		slice.world_environment.compositor = null
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_ROADS") == "1" and slice.battlefield.road_container != null:
		slice.battlefield.road_container.visible = false
	if OS.get_environment("OPENBFME_CAPTURE_DISABLE_PROPS") == "1" and slice.battlefield.retail_prop_container != null:
		slice.battlefield.retail_prop_container.visible = false
	var hidden_prop_types := OS.get_environment("OPENBFME_CAPTURE_HIDE_PROP_TYPE").split(",", false)
	if not hidden_prop_types.is_empty() and slice.battlefield.retail_prop_container != null:
		for placement_value in slice.battlefield.retail_prop_container.get_children():
			var placement := placement_value as Node3D
			if hidden_prop_types.has(String(placement.get_meta("source_type", ""))):
				placement.visible = false
	if OS.get_environment("OPENBFME_CAPTURE_UNSHADED_TERRAIN") == "1":
		_make_terrain_unshaded(slice.battlefield)
	if OS.get_environment("OPENBFME_CAPTURE_FORCE_TERRAIN_LIGHT_LAYER_ONE") == "1":
		slice.battlefield.terrain_mesh_instance.layers = 1
		for light_value in slice.source_environment_lights:
			var light := light_value as DirectionalLight3D
			if String(light.get_meta("retail_domain", "")) == "terrain":
				light.light_cull_mask = 1
	if OS.get_environment("OPENBFME_CAPTURE_ADD_TEST_TERRAIN_LIGHT") == "1":
		var test_light := DirectionalLight3D.new()
		test_light.name = "CaptureDiagnosticTerrainLight"
		test_light.light_color = Color.WHITE
		test_light.light_energy = 1.0
		test_light.light_cull_mask = slice.battlefield.terrain_mesh_instance.layers
		test_light.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
		slice.add_child(test_light)
	if OS.get_environment("OPENBFME_CAPTURE_INVERT_DOMAIN_LIGHTS") == "1":
		for light_value in slice.source_environment_lights:
			var light := light_value as DirectionalLight3D
			var source_ray := Vector3(light.get_meta("source_ray_direction_local", Vector3.DOWN))
			light.basis = Basis.looking_at(-source_ray, Vector3.UP)
	var settle_frames := SETTLE_FRAMES
	var settle_text := OS.get_environment("OPENBFME_CAPTURE_SETTLE_FRAMES").strip_edges()
	if settle_text != "":
		if not settle_text.is_valid_int() or int(settle_text) < 1 or int(settle_text) > 120:
			_fail("OPENBFME_CAPTURE_SETTLE_FRAMES must be in 1..120")
			return
		settle_frames = int(settle_text)
	for _index in settle_frames:
		await process_frame
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if image == null or image.is_empty() or image.get_width() != 1920 or image.get_height() != 1080:
		_fail("rendered viewport capture has dimensions %dx%d instead of 1920x1080" % [image.get_width() if image != null else -1, image.get_height() if image != null else -1])
		return
	var parent := output.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(parent) != OK:
		_fail("private render-capture directory could not be created")
		return
	var save_error := image.save_png(output)
	if save_error != OK or not FileAccess.file_exists(output):
		_fail("rendered PNG could not be written")
		return
	var camera_metadata: Dictionary = slice.camera.get_meta("retail_camera", {}) as Dictionary
	var structure_diagnostics := _structure_diagnostics(slice)
	var geometry_diagnostics := _geometry_diagnostics(slice)
	var prop_type_diagnostics := _prop_type_diagnostics(slice.battlefield.retail_prop_container)
	print("RETAIL_RENDER_CAMERA position=%s focus=%s local_ground=%s metadata=%s" % [
		slice.camera.global_position,
		slice.camera_focus,
		slice.source_map_data.local_ground_height(slice.camera_focus),
		camera_metadata,
	])
	print("RETAIL_RENDER_STRUCTURES %s" % JSON.stringify(structure_diagnostics))
	print("RETAIL_RENDER_UNIT_MATERIALS %s" % JSON.stringify(_unit_material_diagnostics(slice)))
	print("RETAIL_RENDER_LIGHTING terrain_layers=%d ambient=%s lights=%s" % [
		int(slice.battlefield.terrain_mesh_instance.layers),
		slice.battlefield.terrain_material.get_shader_parameter("sage_ambient_color"),
		JSON.stringify(_light_diagnostics(slice)),
	])
	print("RETAIL_RENDER_CAMERA_GEOMETRY %s" % JSON.stringify(geometry_diagnostics))
	print("RETAIL_RENDER_FLAT_PROP_TYPES %s" % JSON.stringify(prop_type_diagnostics))
	print("RETAIL_RENDER_CAPTURE_OK path=%s width=%d height=%d pack=%s fog_dispatches=%d" % [
		output,
		image.get_width(),
		image.get_height(),
		String(slice.selected_pack_root),
		int(slice.linear_fog.execution_metrics().get("dispatch_count", 0)) if slice.linear_fog != null else 0,
	])
	slice.queue_free()
	await process_frame
	quit(0)


func _pose_attack_pair(slice: Node, pair_text: String) -> bool:
	var parts := pair_text.split(",", false)
	if parts.size() != 2 or not String(parts[0]).is_valid_int() or not String(parts[1]).is_valid_int():
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR must be attacker_id,target_id")
		return false
	var attacker_id := int(parts[0])
	var target_id := int(parts[1])
	if not slice.battalion_nodes.has(attacker_id) or not slice.battalion_nodes.has(target_id):
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR must name two live battalions")
		return false
	var attacker_row := slice.simulation.entities.get(attacker_id) as Dictionary
	var target_row := slice.simulation.entities.get(target_id) as Dictionary
	if attacker_row == null or target_row == null:
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR entities are unavailable")
		return false
	var attacker_position := attacker_row.get("position", Vector2.ZERO) as Vector2
	var target_position := attacker_position + Vector2(3.0, 0.0)
	target_row["position"] = target_position
	target_row["destination"] = target_position
	target_row["route"] = []
	target_row["route_cells"] = []
	target_row["route_cursor"] = 0
	var attacker_ids: Array[int] = [attacker_id]
	if not bool(slice.simulation.select_only(attacker_id)) or int(slice.simulation.issue_attack(attacker_ids, target_id)) != 1:
		_fail("OPENBFME_CAPTURE_ATTACK_PAIR could not issue the authoritative attack")
		return false
	var advance_ticks := 1
	var advance_text := OS.get_environment("OPENBFME_CAPTURE_ADVANCE_TICKS").strip_edges()
	if advance_text != "":
		if not advance_text.is_valid_int() or int(advance_text) < 1 or int(advance_text) > 120:
			_fail("OPENBFME_CAPTURE_ADVANCE_TICKS must be in 1..120")
			return false
		advance_ticks = int(advance_text)
	slice.simulation.advance(advance_ticks)
	slice.camera_focus = attacker_position.lerp(target_position, 0.5)
	slice._clamp_camera_focus()
	slice._apply_camera_transform()
	slice._sync_presentation()
	print("RETAIL_RENDER_ATTACK_POSE attacker=%d target=%d advance_ticks=%d" % [attacker_id, target_id, advance_ticks])
	return true


func _light_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for light_value in slice.source_environment_lights:
		var light := light_value as DirectionalLight3D
		result.append({
			"domain": String(light.get_meta("retail_domain", "")),
			"name": String(light.get_meta("retail_light_name", "")),
			"mask": light.light_cull_mask,
			"visible": light.is_visible_in_tree(),
			"ray": -light.global_basis.z,
		})
	return result


func _unit_material_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var ids: Array = slice.battalion_nodes.keys()
	ids.sort()
	for id_value in ids.slice(0, mini(2, ids.size())):
		var battalion := slice.battalion_nodes[id_value] as Node3D
		var pending: Array[Node] = [battalion]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null and not String(current.name).begins_with("Member") and not String(current.name).begins_with("Battalion"):
				var mesh_instance := current as MeshInstance3D
				for surface in range(mesh_instance.mesh.get_surface_count()):
					var material := mesh_instance.get_active_material(surface)
					if material is StandardMaterial3D:
						var standard := material as StandardMaterial3D
						result.append({
							"battalion": int(id_value),
							"mesh": String(mesh_instance.name),
							"layers": mesh_instance.layers,
							"surface": surface,
							"albedo": standard.albedo_color,
							"has_texture": standard.albedo_texture != null,
							"shading_mode": standard.shading_mode,
							"emission_enabled": standard.emission_enabled,
							"transparency": standard.transparency,
							"cull_mode": standard.cull_mode,
						})
						if result.size() >= 12:
							return result
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
	return result


func _structure_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for id_value in slice.structure_nodes.keys():
		var structure: Node3D = slice.structure_nodes[id_value] as Node3D
		var bounds := AABB()
		var has_bounds := false
		var pending: Array[Node] = [structure]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
				var mesh_instance := current as MeshInstance3D
				var mesh_bounds := mesh_instance.global_transform * mesh_instance.get_aabb()
				bounds = bounds.merge(mesh_bounds) if has_bounds else mesh_bounds
				has_bounds = true
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
		result.append({
			"id": int(id_value),
			"kind": String(structure.structure_kind),
			"position": structure.global_position,
			"shared_scale": float(structure.shared_uniform_scale),
			"bounds": bounds if has_bounds else AABB(),
			"camera_inside_bounds": has_bounds and bounds.has_point(slice.camera.global_position),
		})
	return result


func _geometry_diagnostics(slice: Node) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var pending: Array[Node] = [slice]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
			var mesh_instance := current as MeshInstance3D
			var bounds := mesh_instance.global_transform * mesh_instance.get_aabb()
			var camera_inside := bounds.has_point(slice.camera.global_position)
			var very_large := maxf(bounds.size.x, maxf(bounds.size.y, bounds.size.z)) >= 100.0
			if camera_inside or very_large:
				result.append({
					"path": String(slice.get_path_to(mesh_instance)),
					"bounds": bounds,
					"visible": mesh_instance.is_visible_in_tree(),
					"camera_inside_bounds": camera_inside,
				})
		for child_value in current.get_children():
			if child_value is Node:
				pending.append(child_value as Node)
	return result


func _make_terrain_unshaded(battlefield: Node) -> void:
	var source: ShaderMaterial = battlefield.terrain_material as ShaderMaterial
	if source == null or source.shader == null:
		return
	source.set_shader_parameter("sage_lighting_enabled", false)


func _prop_type_diagnostics(container: Node3D) -> Array[Dictionary]:
	var by_type: Dictionary = {}
	if container == null:
		return []
	for placement_value in container.get_children():
		var placement := placement_value as Node3D
		var type_name := String(placement.get_meta("source_type", ""))
		var bounds := AABB()
		var has_bounds := false
		var pending: Array[Node] = [placement]
		while not pending.is_empty():
			var current: Node = pending.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
				var mesh := current as MeshInstance3D
				var mesh_bounds := mesh.global_transform * mesh.get_aabb()
				bounds = bounds.merge(mesh_bounds) if has_bounds else mesh_bounds
				has_bounds = true
			for child_value in current.get_children():
				if child_value is Node:
					pending.append(child_value as Node)
		if not has_bounds or bounds.size.y > 0.2:
			continue
		var row: Dictionary = by_type.get(type_name, {
			"type": type_name,
			"count": 0,
			"maximum_horizontal_span": 0.0,
			"maximum_height": 0.0,
		})
		row.count = int(row.count) + 1
		row.maximum_horizontal_span = maxf(float(row.maximum_horizontal_span), maxf(bounds.size.x, bounds.size.z))
		row.maximum_height = maxf(float(row.maximum_height), bounds.size.y)
		by_type[type_name] = row
	var result: Array[Dictionary] = []
	for row_value in by_type.values():
		result.append(row_value as Dictionary)
	result.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return String(left.type) < String(right.type))
	return result


func _fail(message: String) -> void:
	printerr("RETAIL_RENDER_CAPTURE_FAIL %s" % message)
	quit(1)
