## Integration: the roads of a retail map are drawn with the terrain's lighting (lane ROADS-1; STOPS S-036). Headless, needs ROTWK_INSTALL / BFME2_INSTALL:
##
##   godot --headless --path godot --script res://tests/road_shading_test.gd
##
## Retail draws a road with terrain.fxo "Default", RenderingMode 2, under the terrain's light environment (W3DRoadBufferDrawRoads.cpp:349-365, FlatHeightMap.cpp:816):
## every road material is the terrain material's sun, cloud, macro and fog inputs plus the road texture and a normal texture, which is the road's _nrm companion
## when it exists and the 1x1 (127, 127, 255, 0) texel of W3DRoadBuffer + 0x18 otherwise; the road mesh carries the SAGE normal (0, 0, 1) = Godot (0, 1, 0).
extends SceneTree

const MAP := "map mp weathertop"
const WANT_NORMAL_MAPS := 1 # road types of the map with a _nrm companion
const WANT_FALLBACKS := 1   # road types of the map drawn with the 1x1 fallback
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


func _run() -> void:
	if not ClassDB.class_exists("MapTerrainBuilder"):
		print("SKIP: the openbfme extension is not loaded")
		quit(EXIT_SKIP)
		return
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(EXIT_SKIP)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount_retail %s" % [mount.get("errors", [])])
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var map = builder.build_map(fs, MAP, {"markers": false, "water": false})
	_check(map != null, "%s builds" % MAP)
	if map == null:
		quit(1)
		return
	var report: Dictionary = builder.get_report()
	var roads: Dictionary = report.roads
	print("  info roads report: %s" % [roads])
	_check(int(roads.strips) > 0, "the map has road strips (%d)" % int(roads.strips))
	_check(String(roads.shading) == "terrain.fxo Default, RenderingMode 2: PSDefault_Array element 2", "the report names the shader: %s" % roads.shading)
	_check((report.stops as Array).has("S-036"), "S-036 is reported with the map")
	for e in (report.errors as Array):
		_check(not String(e).contains("_nrm") and not String(e).to_lower().contains("road"), "no road error: %s" % e)

	var terrain_mat: ShaderMaterial = null
	for mi in map.get_node("Terrain").find_children("*", "MeshInstance3D", true, false):
		terrain_mat = (mi as MeshInstance3D).mesh.surface_get_material(0)
		break
	_check(terrain_mat != null, "the map has a terrain material")

	var normal_maps := 0
	var fallbacks := 0
	var materials := 0
	var normal_ok := true
	for mi in map.get_node("Roads").find_children("*", "MeshInstance3D", true, false):
		var mesh: ArrayMesh = (mi as MeshInstance3D).mesh
		var mat: ShaderMaterial = mesh.surface_get_material(0)
		materials += 1
		_check(mat.shader.code.contains("terrain_common"), "%s uses the terrain's shader include" % mi.name)
		# the lighting is the terrain's: the sun, the lightning, cloud, macro and fog inputs are the terrain material's own values
		for p in ["sun_dir_sage", "sun_color", "overbright", "cloud_tex", "macro_tex", "cloud_scale", "cloud_offset_per_second", "map_size", "map_border_width",
				"fog_enabled", "fog_color", "fog_range_start", "fog_range_end"]:
			var want = terrain_mat.get_shader_parameter(p)
			var got = mat.get_shader_parameter(p)
			if typeof(want) == TYPE_OBJECT:
				_check(got == want, "%s: %s is the terrain's texture" % [mi.name, p])
			else:
				_check(got == want, "%s: %s = %s (the terrain's %s)" % [mi.name, p, got, want])
		_check(mat.get_shader_parameter("overbright") == true, "%s: overbright is the boolean the terrain include reads" % mi.name)
		var base: Texture2D = mat.get_shader_parameter("road_tex")
		var nrm: Texture2D = mat.get_shader_parameter("road_normal_tex")
		_check(base != null and base.get_width() > 1, "%s: road_tex is the road texture (%dx%d)" % [mi.name, base.get_width() if base else 0, base.get_height() if base else 0])
		_check(nrm != null, "%s: road_normal_tex is bound" % mi.name)
		if nrm != null:
			if nrm.get_width() == 1 and nrm.get_height() == 1:
				fallbacks += 1
				var px: Color = nrm.get_image().get_pixel(0, 0)
				_check(px == Color8(127, 127, 255, 0), "%s: the fallback normal texel is (127, 127, 255, 0): %s" % [mi.name, px])
			else:
				normal_maps += 1
				_check(String(nrm.get_meta("retail_path", "")).to_lower().contains("_nrm"), "%s: the normal map is a _nrm texture (%s)" % [mi.name, nrm.get_meta("retail_path", "")])
		# the vertices carry the SAGE normal (0, 0, 1) in Godot's frame (x, z, -y): (0, 1, 0)
		var arrays := mesh.surface_get_arrays(0)
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		normal_ok = normal_ok and normals.size() == (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() and normals.size() > 0
		for n in normals:
			normal_ok = normal_ok and (n - Vector3.UP).length() < 0.001 # tolerance: Godot stores the mesh normal compressed (z reads back -0.000015)
	_check(normal_ok, "every road vertex has the normal (0, 1, 0) in Godot's frame (SAGE (0, 0, 1))")
	_check(materials > 0, "the map has road materials (%d)" % materials)
	_check(normal_maps == int(roads.normal_maps), "the materials with a normal map (%d) are the report's (%d)" % [normal_maps, int(roads.normal_maps)])
	_check(fallbacks == int(roads.fallback_normals), "the materials with the fallback normal (%d) are the report's (%d)" % [fallbacks, int(roads.fallback_normals)])
	_check(normal_maps == WANT_NORMAL_MAPS, "%d road textures of %s have a _nrm companion" % [WANT_NORMAL_MAPS, MAP])
	_check(fallbacks == WANT_FALLBACKS, "%d road textures of %s draw with the 1x1 fallback" % [WANT_FALLBACKS, MAP])
	_check(normal_maps + fallbacks == materials, "every road material has exactly one of the two")

	# The same map with the terrain mesh disabled: retail's light environment is the map's, not the terrain mesh's (FlatHeightMap.cpp:768-791, :816), so the
	# roads still take the map's sun, cloud, macro and fog inputs instead of the shader defaults (a white sun, overbright on, no fog)
	var no_terrain = builder.build_map(fs, MAP, {"markers": false, "water": false, "terrain": false})
	_check(no_terrain != null, "%s builds with the terrain disabled" % MAP)
	if no_terrain != null:
		_check(no_terrain.get_node_or_null("Terrain") == null, "the terrain-disabled build has no Terrain node")
		var road_meshes: Array = no_terrain.get_node("Roads").find_children("*", "MeshInstance3D", true, false)
		_check(road_meshes.size() == materials, "the terrain-disabled build has the same road materials (%d)" % road_meshes.size())
		_check(terrain_mat.get_shader_parameter("sun_color") != Vector3.ONE, "the map's sun colour is not the shader default (%s)" % [terrain_mat.get_shader_parameter("sun_color")])
		for mi in road_meshes:
			var nmat: ShaderMaterial = (mi as MeshInstance3D).mesh.surface_get_material(0)
			for p in ["sun_dir_sage", "sun_color", "overbright", "cloud_scale", "cloud_offset_per_second", "macro_stretched", "map_size", "map_border_width",
					"fog_enabled", "fog_color", "fog_range_start", "fog_range_end"]:
				_check(nmat.get_shader_parameter(p) == terrain_mat.get_shader_parameter(p), "%s without terrain: %s = %s (the terrain build's %s)" % [mi.name, p, nmat.get_shader_parameter(p), terrain_mat.get_shader_parameter(p)])
			for p in ["cloud_tex", "macro_tex"]:
				var tex: Texture2D = nmat.get_shader_parameter(p)
				var want: Texture2D = terrain_mat.get_shader_parameter(p)
				_check(tex != null and want != null and tex.get_meta("retail_path", "") == want.get_meta("retail_path", ""), "%s without terrain: %s is %s" % [mi.name, p, tex.get_meta("retail_path", "") if tex else "missing"])
		no_terrain.free()
	map.free()
	print("ROAD SHADING %s" % ["PASS" if _failures == 0 else "FAIL"])
	quit(0 if _failures == 0 else 1)
