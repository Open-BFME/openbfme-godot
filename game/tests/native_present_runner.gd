extends SceneTree
## Headless proof that native presentation resolves the private retail map.

const MapDocumentScript := preload("res://src/map/map_document.gd")
const NativeTerrainScript := preload("res://src/present/native_terrain.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var map_path := OS.get_environment("OPENBFME_MAP").strip_edges()
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var document = MapDocumentScript.new()
	_check("map_v1_loaded", document.load_path(map_path))
	var terrain = NativeTerrainScript.new()
	root.add_child(terrain)
	_check("terrain_configured", terrain.configure(document, content_root))
	_check("cooked_path_selected", terrain.presentation_path == "cooked-retail-map")
	_check("texture_layers", terrain.texture_layer_count > 0)
	_check("water_polygons", terrain.water_polygon_count > 0)
	_check("water_triangles", terrain.water_triangle_count > 0)
	_check("blend_cells", terrain.primary_blend_cell_count > 0)
	print("NATIVE_PRESENT_TERRAIN textures=%d primary_blends=%d three_way=%d cliffs=%d water_polygons=%d water_triangles=%d cooked=%s" % [
		terrain.texture_layer_count,
		terrain.primary_blend_cell_count,
		terrain.three_way_blend_cell_count,
		terrain.cliff_cell_count,
		terrain.water_polygon_count,
		terrain.water_triangle_count,
		terrain.cooked_map_path,
	])
	terrain.queue_free()
	print("NATIVE_PRESENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_PRESENT FAIL %s" % label)
