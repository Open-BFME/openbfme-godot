class_name RetailScenarioDeathFxController
extends Node3D
## Executes a sealed neutral-prop death FX graph without a generic fallback.

var error := ""
var route_receipt: Dictionary = {}
var emitter_count := 0
var audio_player_count := 0
var shake_count := 0


func execute(
	binding: Dictionary,
	pack_root: String,
	origin: Vector3,
	yaw: float,
	local_scale: float,
	surface_kind: String,
	presentation_parent: Node3D,
	audio_route: Callable,
	shake_route: Callable
) -> bool:
	if surface_kind not in ["land", "water"] or presentation_parent == null or not is_finite(local_scale) or local_scale <= 0.0:
		return _fail("scenario death FX surface/presentation parent is invalid")
	var particles := binding.get("particleBindings", {}) as Dictionary
	var definitions := particles.get("definitionRegistry", []) as Array
	var textures := particles.get("textures", []) as Array
	var selected: Dictionary = {}
	for row_value in definitions:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("scenario death FX definition registry is invalid")
		var row := row_value as Dictionary
		if bool(row.get("selectedForRuntime", false)):
			var identifier := String(row.get("definitionId", ""))
			if identifier == "" or selected.has(identifier.to_lower()):
				return _fail("scenario death FX runtime definition selection is ambiguous")
			selected[identifier.to_lower()] = row
	var texture_by_id: Dictionary = {}
	for row_value in textures:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("scenario death FX texture registry is invalid")
		var row := row_value as Dictionary
		texture_by_id[String(row.get("resourceId", ""))] = row
	var active_particles: Array[String] = []
	var audio_ids: Array[String] = []
	var shake_types: Array[String] = []
	for nugget_value in binding.get("authoredNuggets", []) as Array:
		var nugget := nugget_value as Dictionary
		var kind := String(nugget.get("kind", ""))
		var fields := _assignments(nugget.get("assignments", []))
		match kind:
			"ParticleSystem":
				if bool(fields.get("onlyifonland", "").nocasecmp_to("Yes") == 0) and surface_kind != "land":
					continue
				if bool(fields.get("onlyifonwater", "").nocasecmp_to("Yes") == 0) and surface_kind != "water":
					continue
				var system_id := String(fields.get("name", ""))
				var definition := selected.get(system_id.to_lower(), {}) as Dictionary
				if definition.is_empty():
					return _fail("scenario death FX selected particle definition is absent: %s" % system_id)
				var offset := _source_offset(String(fields.get("offset", "X:0 Y:0 Z:0")))
				if offset == Vector3.INF:
					return _fail("scenario death FX particle offset is invalid")
				if String(fields.get("orienttoobject", "No")).nocasecmp_to("Yes") == 0:
					offset = Basis(Vector3.UP, yaw) * offset
				offset *= local_scale
				var emitter := _make_emitter(definition, texture_by_id, pack_root, local_scale)
				if emitter == null:
					return false
				emitter.name = "ScenarioDeathFx_%s_%02d" % [system_id, emitter_count]
				emitter.position = origin + offset
				emitter.rotation.y = yaw if String(fields.get("orienttoobject", "No")).nocasecmp_to("Yes") == 0 else 0.0
				emitter.set_meta("fx_list_id", String(binding.get("fxListId", "")))
				emitter.set_meta("particle_system_id", system_id)
				emitter.set_meta("surface_kind", surface_kind)
				emitter.set_meta("authored_nugget", nugget.duplicate(true))
				presentation_parent.add_child(emitter)
				emitter_count += 1
				active_particles.append(system_id)
			"Sound":
				audio_ids.append(String(fields.get("name", "")))
			"ViewShake":
				shake_types.append(String(fields.get("type", "")))
			_:
				return _fail("scenario death FX nugget kind is unsupported at runtime")
	if not _route_audio(binding, pack_root, origin, audio_ids, audio_route):
		return false
	for shake_type in shake_types:
		if shake_type != "SUBTLE" or not shake_route.is_valid() or shake_route.call(shake_type, origin) != true:
			return _fail("scenario death FX view shake route failed")
		shake_count += 1
	route_receipt = {
		"ok": true,
		"presented": true,
		"status": "sealed-authored-fx-route-presented",
		"fxListId": binding.get("fxListId"),
		"surfaceKind": surface_kind,
		"particleSystemIds": active_particles,
		"audioEventIds": audio_ids,
		"viewShakeTypes": shake_types,
		"emitterCount": emitter_count,
		"audioPlayerCount": audio_player_count,
		"shakeCount": shake_count,
	}
	set_meta("route_receipt", route_receipt.duplicate(true))
	return true


func _make_emitter(definition_row: Dictionary, texture_by_id: Dictionary, pack_root: String, local_scale: float) -> GPUParticles3D:
	var path := ModLoader.resolve_pack_path(pack_root, String(definition_row.get("definitionOutputJson", "")))
	if path == "" or not FileAccess.file_exists(path) or not ModLoader.path_is_within(pack_root, path):
		_fail("scenario death FX definition file is missing or escaped")
		return null
	var document: Variant = ModLoader._read_json(path)
	if typeof(document) != TYPE_DICTIONARY or String((document as Dictionary).get("name", "")) != String(definition_row.get("definitionId", "")):
		_fail("scenario death FX definition identity drifted")
		return null
	var scalars := _flatten_definition((document as Dictionary).get("entries", []))
	var texture_ids := definition_row.get("textureResourceIds", []) as Array
	if texture_ids.size() != 1:
		_fail("scenario death FX definition texture ownership is ambiguous")
		return null
	var texture_row := texture_by_id.get(String(texture_ids[0]), {}) as Dictionary
	var texture_path := ModLoader.resolve_pack_path(pack_root, String(texture_row.get("output", "")))
	if texture_path == "" or not FileAccess.file_exists(texture_path) or not ModLoader.path_is_within(pack_root, texture_path):
		_fail("scenario death FX texture is missing or escaped")
		return null
	var image := Image.new()
	if image.load(texture_path) != OK or image.is_empty():
		_fail("scenario death FX texture is invalid")
		return null
	var lifetime := _range(String(scalars.get("system/lifetime", "")))
	var size := _range(String(scalars.get("system/size", "")))
	var burst := _range(String(scalars.get("system/burstcount", "")))
	if lifetime == Vector2.INF or size == Vector2.INF or burst == Vector2.INF:
		_fail("scenario death FX required particle scalars are invalid")
		return null
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.billboard_mode = BaseMaterial3D.BILLBOARD_DISABLED if String(scalars.get("system/isgroundaligned", "No")).nocasecmp_to("Yes") == 0 else BaseMaterial3D.BILLBOARD_ENABLED
	material.albedo_texture = ImageTexture.create_from_image(image)
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	quad.orientation = PlaneMesh.FACE_Y if material.billboard_mode == BaseMaterial3D.BILLBOARD_DISABLED else PlaneMesh.FACE_Z
	quad.material = material
	var process := ParticleProcessMaterial.new()
	process.scale_min = maxf(0.001, size.x * local_scale)
	process.scale_max = maxf(process.scale_min, size.y * local_scale)
	var color := _authored_color(String(scalars.get("color/defaultcolor/color1", "")))
	if color.a > 0.0:
		process.color = color
	var gravity := float(scalars.get("physics/defaultphysics/gravity", "0"))
	process.gravity = Vector3(0.0, gravity * 30.0, 0.0)
	var damping := _range(String(scalars.get("physics/defaultphysics/velocitydamping", "1 1")))
	if damping != Vector2.INF:
		process.damping_min = maxf(0.0, (1.0 - damping.y) * 30.0)
		process.damping_max = maxf(process.damping_min, (1.0 - damping.x) * 30.0)
	var velocity_x := _range(String(scalars.get("emissionvelocity/orthoemissionvelocity/x", "")))
	var velocity_y := _range(String(scalars.get("emissionvelocity/orthoemissionvelocity/y", "")))
	var velocity_z := _range(String(scalars.get("emissionvelocity/orthoemissionvelocity/z", "")))
	if velocity_x != Vector2.INF and velocity_y != Vector2.INF and velocity_z != Vector2.INF:
		var source_mid := Vector3((velocity_x.x + velocity_x.y) * 0.5, (velocity_y.x + velocity_y.y) * 0.5, (velocity_z.x + velocity_z.y) * 0.5)
		var godot_mid := Vector3(source_mid.x, source_mid.z, -source_mid.y)
		if godot_mid.length_squared() > 0.000001:
			process.direction = godot_mid.normalized()
		process.spread = 180.0
		process.initial_velocity_min = minf(Vector3(velocity_x.x, velocity_z.x, -velocity_y.x).length(), Vector3(velocity_x.y, velocity_z.y, -velocity_y.y).length()) * 30.0 * local_scale
		process.initial_velocity_max = maxf(process.initial_velocity_min, maxf(velocity_x.y, maxf(velocity_y.y, velocity_z.y)) * 30.0 * local_scale)
	var sphere_radius_text := String(scalars.get("emissionvolume/sphereemissionvolume/radius", ""))
	if sphere_radius_text.is_valid_float():
		process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
		process.emission_sphere_radius = float(sphere_radius_text) * local_scale
	var line_start := _source_offset(String(scalars.get("emissionvolume/lineemissionvolume/startpoint", "")))
	var line_end := _source_offset(String(scalars.get("emissionvolume/lineemissionvolume/endpoint", "")))
	if line_start != Vector3.INF and line_end != Vector3.INF:
		process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
		process.emission_box_extents = (line_end - line_start).abs() * 0.5 * local_scale
	var emitter := GPUParticles3D.new()
	emitter.amount = maxi(1, ceili(burst.y))
	emitter.lifetime = maxf(1.0 / 30.0, lifetime.y / 30.0)
	emitter.one_shot = true
	emitter.explosiveness = 1.0
	emitter.fixed_fps = 30
	emitter.process_material = process
	emitter.draw_pass_1 = quad
	emitter.visibility_aabb = AABB(Vector3(-32, -8, -32), Vector3(64, 64, 64))
	emitter.set_meta("definition_sha256", definition_row.get("sourceBlockSha256"))
	emitter.set_meta("authored_scalars", scalars.duplicate(true))
	emitter.emitting = true
	return emitter


func _route_audio(binding: Dictionary, pack_root: String, origin: Vector3, audio_ids: Array[String], audio_route: Callable) -> bool:
	if not audio_route.is_valid():
		return _fail("scenario death FX audio route is unavailable")
	var bindings := binding.get("audioBindings", {}) as Dictionary
	var closure := binding.get("audioClosure", {}) as Dictionary
	var sequence := 1
	for event_id in audio_ids:
		var outputs := bindings.get(event_id, []) as Array
		if outputs.is_empty():
			return _fail("scenario death FX audio event has no leaves")
		var routed: Variant = audio_route.call(event_id, closure, outputs, pack_root, origin, sequence)
		if typeof(routed) != TYPE_DICTIONARY or not bool((routed as Dictionary).get("ok", false)):
			return _fail("scenario death FX audio event route failed: %s" % String((routed as Dictionary).get("reason", "invalid-route-receipt") if typeof(routed) == TYPE_DICTIONARY else "invalid-route-receipt"))
		audio_player_count += 1
		sequence += 1
	return true


func _assignments(value: Variant) -> Dictionary:
	var result: Dictionary = {}
	if typeof(value) != TYPE_ARRAY:
		return result
	for row_value in value as Array:
		if typeof(row_value) == TYPE_DICTIONARY:
			var row := row_value as Dictionary
			result[String(row.get("field", "")).to_lower()] = String(row.get("value", ""))
	return result


func _flatten_definition(value: Variant, prefix: String = "") -> Dictionary:
	var result: Dictionary = {}
	if typeof(value) != TYPE_ARRAY:
		return result
	for row_value in value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var field := String(row.get("field", "")).to_lower()
		var path := field if prefix == "" else prefix + "/" + field
		if String(row.get("type", "")) == "assignment":
			result[path] = String(row.get("value", ""))
		else:
			var selector_value: Variant = row.get("selector")
			var selector := String(selector_value).to_lower() if selector_value != null else ""
			var nested := path if selector == "" else path + "/" + selector
			result.merge(_flatten_definition(row.get("entries", []), nested), true)
	return result


func _range(value: String) -> Vector2:
	var parts := value.split(" ", false)
	if parts.is_empty() or parts.size() > 2 or not parts[0].is_valid_float() or (parts.size() == 2 and not parts[1].is_valid_float()):
		return Vector2.INF
	var first := float(parts[0])
	var second := float(parts[1]) if parts.size() == 2 else first
	return Vector2(minf(first, second), maxf(first, second))


func _source_offset(value: String) -> Vector3:
	var source := {"x": NAN, "y": NAN, "z": NAN}
	for token in value.split(" ", false):
		var pair := token.split(":", false)
		if pair.size() == 2 and source.has(String(pair[0]).to_lower()) and String(pair[1]).is_valid_float():
			source[String(pair[0]).to_lower()] = float(pair[1])
	if not is_finite(float(source.x)) or not is_finite(float(source.y)) or not is_finite(float(source.z)):
		return Vector3.INF
	# SAGE X/Y/Z -> Godot X/Z/-Y.
	return Vector3(float(source.x), float(source.z), -float(source.y))


func _authored_color(value: String) -> Color:
	var channels := {"r": -1.0, "g": -1.0, "b": -1.0}
	for token in value.split(" ", false):
		var pair := token.split(":", false)
		if pair.size() == 2 and channels.has(String(pair[0]).to_lower()) and String(pair[1]).is_valid_float():
			channels[String(pair[0]).to_lower()] = clampf(float(pair[1]) / 255.0, 0.0, 1.0)
	if float(channels.r) < 0.0 or float(channels.g) < 0.0 or float(channels.b) < 0.0:
		return Color(0, 0, 0, 0)
	return Color(float(channels.r), float(channels.g), float(channels.b), 1.0)


func _fail(reason: String) -> bool:
	error = reason
	set_meta("error", reason)
	return false
