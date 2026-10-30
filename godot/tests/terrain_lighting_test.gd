## Integration: the terrain effect receives the overbright-scaled SAS sun colour, and the map's terrain light scale (GlobalLighting v8) only at
## the low terrain tier (RW 0x4E2B3A, guarded by the terrain rendering mode < 1; TerrainLightScale.h).
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
	_check_light_scale(builder, fs)
	_check_object_light_scale(builder, fs)
	print("TERRAIN LIGHTING PASS: ", checked, " materials")
	quit()

func _sun_colors(map: Node) -> Array:
	var out: Array = []
	for mesh in map.find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var mat = mesh.mesh.surface_get_material(surface)
			if mat is ShaderMaterial and mat.shader.code.contains("terrain_common"):
				out.append(mat.get_shader_parameter("sun_color"))
	return out

func _check_light_scale(builder, fs) -> void:
	# wor mirkwood stores the scale (0.5, 0.5, 0.45): the High tier (the default) draws the sun unscaled, the lowest terrain tier scales it
	var high = builder.build_map(fs, "map wor mirkwood", {"markers":false,"water":false,"roads":false})
	assert(high != null)
	var rep: Dictionary = builder.get_report()
	var scale: Vector3 = rep.lighting.terrain_light_scale
	assert(scale == Vector3(0.5, 0.5, 0.45), "wor mirkwood light scale")
	assert(rep.lighting.terrain_rendering_mode == 2)
	assert(rep.lighting.terrain_light_scale_applied == Vector3(1, 1, 1))
	var sun: Vector3 = rep.lighting.terrain_lights[0].diffuse * rep.lighting.effect_color_scale
	var colors: Array = _sun_colors(high)
	assert(colors.size() == rep.mesh.chunks, "every terrain material carries the sun colour")
	for c in colors:
		assert(c == sun, "High tier: the sun is not scaled")
	high.free()
	var low = builder.build_map(fs, "map wor mirkwood", {"markers":false,"water":false,"roads":false,
			"use_terrain_normal_map":false, "terrain_lod":0, "shader_lod":0})
	assert(low != null)
	rep = builder.get_report()
	assert(rep.lighting.terrain_rendering_mode == 0)
	assert(rep.lighting.terrain_light_scale_applied == scale)
	var d: Vector3 = rep.lighting.terrain_lights[0].diffuse
	var expected: Vector3 = Vector3(scale.x * d.x, scale.y * d.y, scale.z * d.z) * rep.lighting.effect_color_scale
	colors = _sun_colors(low)
	assert(colors.size() == rep.mesh.chunks, "every terrain material carries the sun colour")
	for c in colors:
		assert(c == expected, "lowest tier: the sun is scaled by the map's light scale")
	assert(expected != sun)
	low.free()

## The report and the consumer together (review of PR #147): MapObjectBuilder's `lighting.object_light_scale_applied` is the factor that scaled the
## light set it handed to W3D_Apply_Object_Lighting (which sets the global shader parameter w3d_lcol_obj the W3D shaders read; Godot cannot read
## that back outside the editor, so the report carries the set's directional colours as effect_object_light_colors). At the lowest tier light 0 is
## the objects light 0 diffuse * the map's scale * the SAS doubling; at the High tier it is unscaled.
## MapTerrainBuilder lights no object: it reports only the factor retail would use (object_light_scale_retail), the same Vector3.
func _object_light_column(objects, fs, options: Dictionary) -> Dictionary:
	var root = objects.build_objects(fs, "map wor mirkwood", options)
	assert(root != null, "wor mirkwood objects")
	var rep: Dictionary = objects.get_report()
	var lit: Dictionary = rep.lighting
	var colors: Array = lit.effect_object_light_colors
	root.free()
	return {"applied": lit.object_light_scale_applied, "light0": colors[0], "diffuse": Vector3(lit.diffuse.r, lit.diffuse.g, lit.diffuse.b),
		"effect": lit.effect_color_scale, "raw": lit.terrain_light_scale, "shader_lod": lit.shader_lod}

func _check_object_light_scale(terrain, fs) -> void:
	var objects = ClassDB.instantiate("MapObjectBuilder")
	assert(objects.setup(fs).ok)
	var base := {"max_objects": 1}
	var high: Dictionary = _object_light_column(objects, fs, base)
	assert(high.shader_lod == 2)
	assert(high.applied == Vector3(1, 1, 1), "High tier: the object lights are not scaled")
	assert(high.light0 == high.diffuse * high.effect, "High tier: the applied set holds the unscaled objects light")
	var low_options := base.duplicate()
	low_options.merge({"use_terrain_normal_map": false, "terrain_lod": 0, "shader_lod": 0})
	var low: Dictionary = _object_light_column(objects, fs, low_options)
	assert(low.shader_lod == 0)
	assert(low.raw == Vector3(0.5, 0.5, 0.45))
	assert(low.applied == low.raw, "shader LOD 0: the report names the map's scale")
	var expected: Vector3 = Vector3(low.applied.x * low.diffuse.x, low.applied.y * low.diffuse.y, low.applied.z * low.diffuse.z) * low.effect
	assert(low.light0 == expected, "shader LOD 0: the set handed to W3D_Apply_Object_Lighting carries the scaled light the report names")
	assert(low.light0 != high.light0)
	# the terrain builder names the same retail factor but applies none to objects
	var map = terrain.build_map(fs, "map wor mirkwood", {"markers": false, "water": false, "roads": false, "terrain": false,
			"use_terrain_normal_map": false, "terrain_lod": 0, "shader_lod": 0})
	assert(map != null)
	var lit: Dictionary = terrain.get_report().lighting
	assert(lit.object_light_scale_retail == low.applied)
	assert(not lit.has("object_light_scale_applied"), "the terrain builder lights no object")
	map.free()
	# an out-of-range tier is an error and no lighting is applied (the global keeps the low-tier value)
	var bad_options := base.duplicate()
	bad_options.merge({"use_terrain_normal_map": false, "terrain_lod": -1})
	var root = objects.build_objects(fs, "map wor mirkwood", bad_options)
	var rep: Dictionary = objects.get_report()
	assert(str(rep.errors).contains("TerrainLOD -1"))
	assert(rep.lighting.has("error") and not rep.lighting.has("object_light_scale_applied"))
	if root != null:
		root.free()
