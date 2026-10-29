## Integration: the terrain effect receives the overbright-scaled SAS sun colour.
extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var map = builder.build_map(fs, "map mp harlindon", {"markers":false,"water":false})
	assert(map != null)
	var report: Dictionary = builder.get_report()
	assert(report.lighting.effect_color_scale == 2.0)
	var expected: Vector3 = report.lighting.terrain_lights[0].diffuse * 2.0
	var checked := 0
	for mesh in map.find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var mat = mesh.mesh.surface_get_material(surface)
			if mat is ShaderMaterial and mat.shader.code.contains("terrain_common") and mesh.get_parent().name != "Roads": # the roads share the include (road_shading_test.gd)
				assert(mat.get_shader_parameter("sun_color") == expected)
				assert(mat.get_shader_parameter("overbright") == true)
				checked += 1
	assert(checked == report.mesh.chunks, "Every terrain material must receive the SAS colour")
	assert(checked == 324, "Harlindon terrain chunk coverage changed")
	map.free()
	print("TERRAIN LIGHTING PASS: ", checked, " materials")
	quit()
