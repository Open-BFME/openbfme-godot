## Authored standing-water materials, map depth inputs and GPU compilation.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check_pca_animation(source: ShaderMaterial) -> bool:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(128, 128)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var material: ShaderMaterial = source.duplicate(false)
	material.set_shader_parameter("blend_terrain_alpha", false)
	material.set_shader_parameter("material_diffuse", Vector4(0.15, 0.3, 0.35, 1.0))
	material.set_shader_parameter("output_gamma", 2.2)
	material.set_shader_parameter("pca_frame_rate", 0.0)
	material.set_shader_parameter("wave_geometry_enabled", false)
	var mesh := PlaneMesh.new()
	mesh.size = Vector2(800, 800)
	mesh.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position = Vector3(0, 800, 600)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	for frame in 5:
		await process_frame
	await RenderingServer.frame_post_draw
	var frozen := viewport.get_texture().get_image().get_data()
	await create_timer(0.15).timeout
	await RenderingServer.frame_post_draw
	var still_frozen := viewport.get_texture().get_image().get_data()
	material.set_shader_parameter("pca_frame_rate", 21.0)
	await create_timer(0.35).timeout
	await RenderingServer.frame_post_draw
	var animated := viewport.get_texture().get_image().get_data()
	viewport.queue_free()
	return frozen == still_frozen and frozen != animated

func _check_wave_geometry(source: ShaderMaterial) -> bool:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(256,256)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.transparent_bg = true
	viewport.own_world_3d = true
	root.add_child(viewport)
	var material: ShaderMaterial = source.duplicate(false)
	material.set_shader_parameter("pca_frame_rate",0.0)
	material.set_shader_parameter("wave_geometry_enabled",true)
	material.set_shader_parameter("wave_time_scale",0.0)
	material.set_shader_parameter("material_diffuse",Vector4(0.15,0.3,0.35,1.0))
	material.set_shader_parameter("output_gamma",2.2)
	var vertices := PackedVector3Array()
	var shore := PackedFloat32Array()
	var directions := PackedFloat32Array()
	var indices := PackedInt32Array()
	for y in 21:
		for x in 21:
			vertices.append(Vector3(x*20,0,-y*20))
			shore.append_array(PackedFloat32Array([200,200,-20,1000]))
			directions.append_array(PackedFloat32Array([1,0,100,0]))
	for y in 20:
		for x in 20:
			var i := y*21+x
			indices.append_array(PackedInt32Array([i,i+22,i+21,i,i+1,i+22]))
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX]=vertices
	arrays[Mesh.ARRAY_CUSTOM0]=shore
	arrays[Mesh.ARRAY_CUSTOM1]=directions
	arrays[Mesh.ARRAY_INDEX]=indices
	var mesh := ArrayMesh.new()
	var flags := (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT) | (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays,[],{},flags)
	mesh.surface_set_material(0,material)
	var instance := MeshInstance3D.new()
	instance.mesh=mesh
	instance.extra_cull_margin=100
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position=Vector3(200,180,300)
	camera.look_at(Vector3(200,0,-200))
	camera.current=true
	for frame in 5:
		await process_frame
	await RenderingServer.frame_post_draw
	var frozen := viewport.get_texture().get_image().get_data()
	await create_timer(0.15).timeout
	await RenderingServer.frame_post_draw
	var still := viewport.get_texture().get_image().get_data()
	material.set_shader_parameter("wave_time_scale",1.0)
	await create_timer(0.5).timeout
	await RenderingServer.frame_post_draw
	var animated := viewport.get_texture().get_image().get_data()
	var silhouette_changed := false
	for i in range(3,frozen.size(),4):
		if (frozen[i]==0) != (animated[i]==0):
			silhouette_changed=true
			break
	print("WAVE GPU frozen stable=",frozen==still," image changed=",frozen!=animated," silhouette=",silhouette_changed," amplitude=",material.get_shader_parameter("gerstner0"))
	viewport.queue_free()
	return frozen==still and silhouette_changed

## a water-only build of a retail map at a WaterLOD: [report, root node]
func _water_build(fs, map_name: String, lod: int) -> Array:
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var node = builder.build_map(fs, map_name, {"markers":false, "terrain":false, "rivers":false, "roads":false, "water_lod":lod})
	return [builder.get_report(), node]

func _material_of(node: Node, index: int) -> ShaderMaterial:
	return node.get_node("StandingWater").get_child(index).mesh.surface_get_material(0)

func _any_stop(report: Dictionary, needle: String) -> bool:
	for line in report.water.material_stops:
		if String(line).contains(needle):
			return true
	return false

## WaterLOD picks the W3D file and the technique (FXShaderSetup::InitializeShader); the RotWK facts are pinned in tools/render/test_water_binary_facts.py
## and engine/tests/test_water_retail.cpp. Returns "" or the first failure.
func _check_lod_resolution(fs) -> String:
	# Harlindon: waterplanepca authors PCA_U
	var expected := {
		0: ["WaterPlanePCAL", "PCA_L", false],
		1: ["WaterPlanePCA", "PCA_M", false],
		2: ["WaterPlanePCA", "PCA", true],
		3: ["WaterPlanePCA", "PCA_U", true],
	}
	for lod in expected:
		var built := _water_build(fs, "map mp harlindon", lod)
		var report: Dictionary = built[0]
		var info: Dictionary = report.water.materials[0]
		if not report.errors.is_empty():
			return "harlindon lod %d: %s" % [lod, str(report.errors)]
		if info.shader_stem != expected[lod][0] or info.technique != expected[lod][1] or info.authored_technique != ("PCA_L" if lod == 0 else "PCA_U") or info.water_lod != lod:
			return "harlindon lod %d resolved %s" % [lod, str(info)]
		if info.pca_enabled != expected[lod][2]:
			return "harlindon lod %d pca_enabled %s" % [lod, str(info.pca_enabled)]
		if not expected[lod][2] and not _any_stop(report, "is not ported; drawn with the BumpSpecular_M path"):
			return "harlindon lod %d: the unported %s is not reported: %s" % [lod, expected[lod][1], str(report.water.material_stops)]
		if lod >= 2:
			var material := _material_of(built[1], 0)
			if material.get_shader_parameter("pca_mode") != (1 if lod == 2 else 2):
				return "harlindon lod %d pca_mode %s" % [lod, str(material.get_shader_parameter("pca_mode"))]
			# PCA: 0.15 steepness and two basis textures; PCA_U: 0.33, three and the noise texture
			if not is_equal_approx(material.get_shader_parameter("wave_steepness"), 0.15 if lod == 2 else 0.33):
				return "harlindon lod %d wave_steepness %s" % [lod, str(material.get_shader_parameter("wave_steepness"))]
			if (material.get_shader_parameter("pca_tex2") != null) != (lod == 3) or (material.get_shader_parameter("pca_noise_tex") != null) != (lod == 3):
				return "harlindon lod %d basis textures" % lod
			if material.get_shader_parameter("clamp_diffuse") != (lod == 2):
				return "harlindon lod %d clamp_diffuse" % lod
			if not _any_stop(report, "ReflectionOn and WaterLOD >= 2"):
				return "harlindon lod %d: ReflectionOn is Yes and the unported reflection is not reported" % lod
		built[1].free()

	# Adorn River's only area (97 points) fails the ear clipper: not drawn, reported (S-035; the pin is in test_water_retail.cpp)
	var adorn := _water_build(fs, "map mp adorn river", 2)
	if adorn[0].water.areas != 0 or adorn[0].water.triangulation_failed != 1 or adorn[0].water.areas_in_map != 1:
		return "adorn river: the rejected polygon is not reported: %s" % str(adorn[0].water)
	adorn[1].free()
	# Fords of Isen II (wtr_riv01, ReflectionStrength 0.8, and wtr_riv02, 0): BumpSpecular at High, BumpSpecular_M at Medium
	for map_name in ["map mp fords of isen ii"]:
		for lod in [1, 2]:
			var built := _water_build(fs, map_name, lod)
			var report: Dictionary = built[0]
			if not report.errors.is_empty():
				return "%s lod %d: %s" % [map_name, lod, str(report.errors)]
			var seen_strengths := []
			for i in report.water.materials.size():
				var info: Dictionary = report.water.materials[i]
				var material := _material_of(built[1], i)
				if info.technique != ("BumpSpecular" if lod == 2 else "BumpSpecular_M") or info.authored_technique != "BumpSpecular":
					return "%s lod %d resolved %s" % [map_name, lod, str(info)]
				if material.get_shader_parameter("pca_mode") != null or material.get_shader_parameter("bump_high") != (lod == 2) or material.get_shader_parameter("clamp_diffuse") != true:
					return "%s lod %d bump parameters" % [map_name, lod]
				if not seen_strengths.has(snappedf(material.get_shader_parameter("reflection_strength"), 0.01)):
					seen_strengths.append(snappedf(material.get_shader_parameter("reflection_strength"), 0.01))
				# the half-vector's light is DirectionalLight[1]; BumpSpecular sends it through oD1, which clamps it to 0..1
				var direction: Vector3 = material.get_shader_parameter("specular_direction")
				if lod == 2 and direction != direction.clamp(Vector3.ZERO, Vector3.ONE):
					return "%s lod %d specular_direction %s is not clamped to 0..1" % [map_name, lod, str(direction)]
				# the map's hardware fog (Fords of Isen II's map.ini Weather: HardwareFogStart 350, End 2000, colour 220 226 235)
				if material.get_shader_parameter("fog_enabled") != 1.0 or material.get_shader_parameter("fog_range_start") != 350.0 or material.get_shader_parameter("fog_range_end") != 2000.0:
					return "%s lod %d fog %s" % [map_name, lod, str(material.get_shader_parameter("fog_range_start"))]
				if material.get_shader_parameter("fog_color") != Vector3(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0):
					return "%s lod %d fog colour" % [map_name, lod]
			seen_strengths.sort()
			if seen_strengths != [0.0, 0.8]:
				return "%s lod %d reflection strengths %s" % [map_name, lod, str(seen_strengths)]
			if _any_stop(report, "ReflectionOn and WaterLOD >= 2") != (lod == 2):
				return "%s lod %d: the unported reflection is reported iff WaterLOD >= 2: %s" % [map_name, lod, str(report.water.material_stops)]
			built[1].free()
	return ""

func _run() -> void:
	create_timer(240).timeout.connect(func(): push_error("Standing water test timed out"); quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not (fs.mount_retail().ok):
		push_error('fs.mount_retail().ok')
		quit(1)
		return
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var map = builder.build_map(fs, "map mp harlindon", {"markers":false, "water_lod":3}) # UltraHigh: PCA_U; the lower levels are _check_lod_resolution's
	if not (map != null):
		push_error('map != null')
		quit(1)
		return
	root.add_child(map)
	var report: Dictionary = builder.get_report()
	print("WATER MATERIALS ", report.water)
	if not (report.errors.is_empty()):
		push_error(str(report.errors))
		quit(1)
		return
	if not (report.water.materials.size() == 1):
		push_error('report.water.materials.size() == 1')
		quit(1)
		return
	if not (report.water.materials[0].color == Vector4(0.15, 0.3, 0.35, 0.9)):
		push_error('report.water.materials[0].color == Vector4(0.15, 0.3, 0.35, 0.9)')
		quit(1)
		return
	if not (report.water.materials[0].bump_uv_scale == Vector2(15, 15)):
		push_error('report.water.materials[0].bump_uv_scale == Vector2(15, 15)')
		quit(1)
		return
	if not (report.water.max_alpha_depth == 20.0):
		push_error('report.water.max_alpha_depth == 20.0')
		quit(1)
		return
	if not (report.water.deep_water_alpha == 1.0):
		push_error('report.water.deep_water_alpha == 1.0')
		quit(1)
		return
	if not (report.water.materials[0].technique_index == 5):
		push_error('report.water.materials[0].technique_index == 5')
		quit(1)
		return
	if not (report.water.materials[0].technique == "PCA_U"):
		push_error('report.water.materials[0].technique == "PCA_U"')
		quit(1)
		return
	if not (report.water.materials[0].pca_enabled):
		push_error('report.water.materials[0].pca_enabled')
		quit(1)
		return
	if not (report.water.materials[0].pca_frames == 30):
		push_error('report.water.materials[0].pca_frames == 30')
		quit(1)
		return
	if not (report.water.materials[0].grid_spacing == 40.0):
		push_error('report.water.materials[0].grid_spacing == 40.0')
		quit(1)
		return
	if not (report.water.materials[0].wave_vertices > 0):
		push_error('report.water.materials[0].wave_vertices > 0')
		quit(1)
		return
	if not (report.water.materials[0].pca_frame_rate == 21.0):
		push_error('report.water.materials[0].pca_frame_rate == 21.0')
		quit(1)
		return
	var checked := 0
	var authored := 0
	for info in report.water.materials:
		if info.authored:
			authored += 1
	for mesh in map.get_node("StandingWater").get_children():
		var material: ShaderMaterial = mesh.mesh.surface_get_material(0)
		if not (material != null):
			push_error('material != null')
			quit(1)
			return
		if not (material.get_shader_parameter("terrain_height_tex") != null):
			push_error('material.get_shader_parameter("terrain_height_tex") != null')
			quit(1)
			return
		if not (material.get_shader_parameter("pca_mode") == 2):
			push_error('material.get_shader_parameter("pca_mode") == 2')
			quit(1)
			return
		if not (material.get_shader_parameter("gerstner0") == Vector4(50,0.033,0.45,1)):
			push_error('material.get_shader_parameter("gerstner0") == Vector4(50,0.033,0.45,1)')
			quit(1)
			return
		if not (material.get_shader_parameter("gerstner1") == Vector4(40,0.031,0.5,1)):
			push_error('material.get_shader_parameter("gerstner1") == Vector4(40,0.031,0.5,1)')
			quit(1)
			return
		if not (material.get_shader_parameter("gerstner2") == Vector4(30,0.029,0.54,1)):
			push_error('material.get_shader_parameter("gerstner2") == Vector4(30,0.029,0.54,1)')
			quit(1)
			return
		var coefficients: Texture2D = material.get_shader_parameter("pca_coefficients")
		if not (coefficients != null and coefficients.get_width() == 30 and coefficients.get_height() == 10):
			push_error('coefficients != null and coefficients.get_width() == 30 and coefficients.get_height() == 10')
			quit(1)
			return
		if not (material.get_shader_parameter("pca_tex0") != null):
			push_error('material.get_shader_parameter("pca_tex0") != null')
			quit(1)
			return
		if not (material.get_shader_parameter("pca_tex1") != null):
			push_error('material.get_shader_parameter("pca_tex1") != null')
			quit(1)
			return
		if not (material.get_shader_parameter("pca_tex2") != null):
			push_error('material.get_shader_parameter("pca_tex2") != null')
			quit(1)
			return
		if not (material.get_shader_parameter("material_diffuse") == report.water.materials[checked].color):
			push_error('material.get_shader_parameter("material_diffuse") == report.water.materials[checked].color')
			quit(1)
			return
		if not (material.get_shader_parameter("bump_uv_scale") == report.water.materials[checked].bump_uv_scale):
			push_error('material.get_shader_parameter("bump_uv_scale") == report.water.materials[checked].bump_uv_scale')
			quit(1)
			return
		checked += 1
	if not (checked == report.water.areas):
		push_error('checked == report.water.areas')
		quit(1)
		return
	if not (authored == checked):
		push_error('authored == checked')
		quit(1)
		return
	if not (checked > 0):
		push_error('checked > 0')
		quit(1)
		return
	var lod_error := _check_lod_resolution(fs)
	if not lod_error.is_empty():
		push_error(lod_error)
		quit(1)
		return
	if DisplayServer.get_name() != "headless":
		var water_mesh = map.get_node("StandingWater").get_child(0)
		if not (await _check_wave_geometry(water_mesh.mesh.surface_get_material(0))):
			push_error("Gerstner waves must change the mesh silhouette while frozen time stays stable")
			quit(1)
			return
		if not (await _check_pca_animation(water_mesh.mesh.surface_get_material(0))):
			push_error("PCA coefficients must animate the water while a zero-rate track remains stable")
			quit(1)
			return
		root.size = Vector2i(1000, 700)
		var camera := Camera3D.new()
		root.add_child(camera)
		camera.position = Vector3(2500, 1500, -1500)
		camera.look_at(Vector3(1700, 0, -2500))
		camera.far = 15000
		camera.current = true
		for frame in 30:
			await process_frame
		await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		if not (image != null and not image.is_empty()):
			push_error('image != null and not image.is_empty()')
			quit(1)
			return
		var capture := OS.get_environment("OPENBFME_WATER_SCREENSHOT")
		if not capture.is_empty():
			if not (image.save_png(capture) == OK):
				push_error('image.save_png(capture) == OK')
				quit(1)
				return
	map.queue_free()
	print("STANDING WATER PASS: ", checked, " materials")
	quit()
