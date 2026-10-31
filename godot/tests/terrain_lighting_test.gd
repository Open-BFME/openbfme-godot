## Integration: the terrain effect receives the overbright-scaled SAS sun colour, and the map's terrain light scale (GlobalLighting v8) only at
## the low terrain tier (RW 0x4E2B3A, guarded by the terrain rendering mode < 1; TerrainLightScale.h).
extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR in the official build and the run carries on, so every check goes through _check: a failure is
## recorded, printed as FAIL and the run exits 1. A check that later code depends on (a map that did not build) stops its function.
var _failures: Array[String] = []

func _check(ok: bool, reason: String) -> bool:
	if not ok:
		_failures.append(reason)
		printerr("FAIL: ", reason)
	return ok

func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok, "mount_retail"):
		_finish(0)
		return
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var map = builder.build_map(fs, "map mp harlindon", {"markers":false,"water":false})
	if not _check(map != null, "map mp harlindon builds"):
		_finish(0)
		return
	var report: Dictionary = builder.get_report()
	_check(report.lighting.effect_color_scale == 2.0, "mp harlindon effect_color_scale is 2.0")
	var expected: Vector3 = report.lighting.terrain_lights[0].diffuse * 2.0
	var checked := 0
	for mesh in map.find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var mat = mesh.mesh.surface_get_material(surface)
			if mat is ShaderMaterial and mat.shader.code.contains("terrain_common") and mesh.get_parent().name != "Roads": # the roads share the include (road_shading_test.gd)
				_check(mat.get_shader_parameter("sun_color") == expected, "mp harlindon terrain material sun_color is the SAS colour")
				_check(mat.get_shader_parameter("overbright") == true, "mp harlindon terrain material is overbright")
				checked += 1
	_check(checked == report.mesh.chunks, "Every terrain material must receive the SAS colour")
	_check(checked == 324, "Harlindon terrain chunk coverage changed")
	map.free()
	_check_light_scale(builder, fs)
	_check_object_light_scale(builder, fs)
	_finish(checked)

func _finish(checked: int) -> void:
	if _failures.is_empty():
		print("TERRAIN LIGHTING PASS: ", checked, " materials")
		quit()
		return
	print("TERRAIN LIGHTING FAIL: ", _failures.size(), " check(s) failed")
	quit(1)

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
	if not _check(high != null, "wor mirkwood High tier terrain builds"):
		return
	var rep: Dictionary = builder.get_report()
	var scale: Vector3 = rep.lighting.terrain_light_scale
	_check(scale == Vector3(0.5, 0.5, 0.45), "wor mirkwood light scale")
	_check(rep.lighting.terrain_rendering_mode == 2, "wor mirkwood default terrain rendering mode is 2")
	_check(rep.lighting.terrain_light_scale_applied == Vector3(1, 1, 1), "High tier: terrain_light_scale_applied is 1")
	var sun: Vector3 = rep.lighting.terrain_lights[0].diffuse * rep.lighting.effect_color_scale
	var colors: Array = _sun_colors(high)
	_check(colors.size() == rep.mesh.chunks, "High tier: every terrain material carries the sun colour")
	_check(colors.all(func(c): return c == sun), "High tier: the sun is not scaled")
	high.free()
	var low = builder.build_map(fs, "map wor mirkwood", {"markers":false,"water":false,"roads":false,
			"use_terrain_normal_map":false, "terrain_lod":0, "shader_lod":0})
	if not _check(low != null, "wor mirkwood lowest tier terrain builds"):
		return
	rep = builder.get_report()
	_check(rep.lighting.terrain_rendering_mode == 0, "lowest tier: terrain rendering mode is 0")
	_check(rep.lighting.terrain_light_scale_applied == scale, "lowest tier: terrain_light_scale_applied is the map's scale")
	var d: Vector3 = rep.lighting.terrain_lights[0].diffuse
	var expected: Vector3 = Vector3(scale.x * d.x, scale.y * d.y, scale.z * d.z) * rep.lighting.effect_color_scale
	colors = _sun_colors(low)
	_check(colors.size() == rep.mesh.chunks, "lowest tier: every terrain material carries the sun colour")
	_check(colors.all(func(c): return c == expected), "lowest tier: the sun is scaled by the map's light scale")
	_check(expected != sun, "lowest tier: the scaled sun differs from the unscaled one")
	low.free()

## The report and the consumer together (review of PR #147): MapObjectBuilder's `lighting.object_light_scale_applied` is the factor that scaled the
## light set it handed to W3D_Apply_Object_Lighting (which sets the global shader parameter w3d_lcol_obj the W3D shaders read; Godot cannot read
## that back outside the editor, so the report carries the set's directional colours as effect_object_light_colors). At the lowest tier light 0 is
## the objects light 0 diffuse * the map's scale * the SAS doubling; at the High tier it is unscaled.
## MapTerrainBuilder lights no object: it reports only the factor retail would use (object_light_scale_retail), the same Vector3.
func _object_light_column(objects, fs, options: Dictionary) -> Dictionary:
	var root = objects.build_objects(fs, "map wor mirkwood", options)
	if not _check(root != null, "wor mirkwood objects build"):
		return {}
	var rep: Dictionary = objects.get_report()
	var lit: Dictionary = rep.lighting
	var colors: Array = lit.effect_object_light_colors
	root.free()
	return {"applied": lit.object_light_scale_applied, "light0": colors[0], "diffuse": Vector3(lit.diffuse.r, lit.diffuse.g, lit.diffuse.b),
		"effect": lit.effect_color_scale, "raw": lit.terrain_light_scale, "shader_lod": lit.shader_lod}

func _check_object_light_scale(terrain, fs) -> void:
	var objects = ClassDB.instantiate("MapObjectBuilder")
	if not _check(objects.setup(fs).ok, "MapObjectBuilder.setup"):
		return
	var base := {"max_objects": 1}
	var high: Dictionary = _object_light_column(objects, fs, base)
	if high.is_empty():
		return
	_check(high.shader_lod == 2, "High tier: shader LOD is 2")
	_check(high.applied == Vector3(1, 1, 1), "High tier: the object lights are not scaled")
	_check(high.light0 == high.diffuse * high.effect, "High tier: the applied set holds the unscaled objects light")
	var low_options := base.duplicate()
	low_options.merge({"use_terrain_normal_map": false, "terrain_lod": 0, "shader_lod": 0})
	var low: Dictionary = _object_light_column(objects, fs, low_options)
	if low.is_empty():
		return
	_check(low.shader_lod == 0, "shader LOD 0: shader LOD is 0")
	_check(low.raw == Vector3(0.5, 0.5, 0.45), "wor mirkwood raw object light scale")
	_check(low.applied == low.raw, "shader LOD 0: the report names the map's scale")
	var expected: Vector3 = Vector3(low.applied.x * low.diffuse.x, low.applied.y * low.diffuse.y, low.applied.z * low.diffuse.z) * low.effect
	_check(low.light0 == expected, "shader LOD 0: the set handed to W3D_Apply_Object_Lighting carries the scaled light the report names")
	_check(low.light0 != high.light0, "shader LOD 0: the object light differs from the High tier's")
	# the terrain builder names the same retail factor but applies none to objects
	var map = terrain.build_map(fs, "map wor mirkwood", {"markers": false, "water": false, "roads": false, "terrain": false,
			"use_terrain_normal_map": false, "terrain_lod": 0, "shader_lod": 0})
	if not _check(map != null, "wor mirkwood terrain (no terrain mesh) builds"):
		return
	var lit: Dictionary = terrain.get_report().lighting
	_check(lit.object_light_scale_retail == low.applied, "the terrain builder names the same retail object light scale")
	_check(not lit.has("object_light_scale_applied"), "the terrain builder lights no object")
	map.free()
	# an out-of-range tier is an error and no lighting is applied (the global keeps the low-tier value)
	var bad_options := base.duplicate()
	bad_options.merge({"use_terrain_normal_map": false, "terrain_lod": -1})
	var root = objects.build_objects(fs, "map wor mirkwood", bad_options)
	var rep: Dictionary = objects.get_report()
	_check(str(rep.errors).contains("TerrainLOD -1"), "an out-of-range tier is reported as an error")
	_check(rep.lighting.has("error") and not rep.lighting.has("object_light_scale_applied"), "an out-of-range tier applies no object lighting")
	if root != null:
		root.free()
