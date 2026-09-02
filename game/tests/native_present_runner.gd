extends SceneTree
## Headless proof that native presentation resolves the private retail map.

const MapDocumentScript := preload("res://src/map/map_document.gd")
const NativeTerrainScript := preload("res://src/present/native_terrain.gd")
const NativeAudioScript := preload("res://src/present/native_audio.gd")
const NativeFxScript := preload("res://src/present/native_fx.gd")

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
	var catalog: Array[Dictionary] = _audio_catalog_fixture()
	_check("audio_template_catalog", catalog.size() == 8)
	var native_audio = NativeAudioScript.new()
	root.add_child(native_audio)
	_check("audio_configured", native_audio.configure(catalog, terrain.pack_root, content_root, false))
	for horde_name in ["GondorFighterHorde", "GondorArcherHorde", "MordorFighterHorde", "MordorArcherHorde"]:
		var voice := native_audio.resolve_voice_rows(horde_name)
		_check("voice_%s" % horde_name, int(voice.get("line_count", 0)) > 0)
		print("NATIVE_PRESENT_VOICE template=%s object=%s lines=%d events=%s" % [
			horde_name, String(voice.get("object_id", "")), int(voice.get("line_count", 0)), JSON.stringify(voice.get("events", {}))
		])
	var fixture := _audio_snapshot(catalog)
	native_audio.present_selection([9001], fixture)
	native_audio.present_order("attack_move", [9001], fixture)
	_check("audio_event_replay", native_audio.submit_snapshot(fixture))
	_check("audio_voice_plays", int(native_audio.play_counts.select) > 0 and int(native_audio.play_counts.attack) > 0)
	_check("audio_fire_plays", int(native_audio.play_counts.fire) > 0)
	_check("audio_death_plays", int(native_audio.play_counts.death) > 0)
	print("NATIVE_PRESENT_AUDIO counts=%s routes=%d" % [JSON.stringify(native_audio.play_counts), native_audio.route_log.size()])
	var native_fx = NativeFxScript.new()
	root.add_child(native_fx)
	_check("fx_configured", native_fx.configure(catalog, terrain.map_data, terrain.pack_root))
	for horde_name in ["GondorFighterHorde", "GondorArcherHorde", "MordorFighterHorde", "MordorArcherHorde"]:
		var fx := native_fx.resolve_fx_names(horde_name)
		_check("fx_%s" % horde_name, not String(fx.get("weapon_fx", "")).is_empty())
		print("NATIVE_PRESENT_FX template=%s weapon=%s resolved=%s" % [
			horde_name, String(fx.get("weapon_fx", "")), JSON.stringify(fx.get("resolved", []))
		])
	_check("fx_event_replay", native_fx.submit_snapshot(fixture))
	_check("fx_fire_play", int(native_fx.play_counts.fire) > 0)
	_check("fx_impact_play", int(native_fx.play_counts.impact) > 0)
	_check("fx_death_play", int(native_fx.play_counts.death) > 0)
	print("NATIVE_PRESENT_FX_COUNTS counts=%s routes=%d" % [JSON.stringify(native_fx.play_counts), native_fx.route_log.size()])
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
	native_audio.queue_free()
	native_fx.queue_free()
	print("NATIVE_PRESENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_PRESENT FAIL %s" % label)


func _audio_snapshot(catalog: Array[Dictionary]) -> Dictionary:
	var fighter := _template_index(catalog, "GondorFighter")
	var archer := _template_index(catalog, "GondorArcher")
	return {
		"tick": 10,
		"objects": {
			"id": [101, 102], "template": [fighter, archer], "owner": [0, 1],
			"x": [1200.0, 1250.0], "y": [250.0, 250.0], "z": [800.0, 820.0],
		},
		"hordes": [{"id": 9001, "owner": 0, "template": _template_index(catalog, "GondorFighterHorde"), "members": [101], "formation": 0}],
		"events": [
			{"kind": "fire", "object": 101, "target": 102},
			{"kind": "damage", "object": 101, "target": 102, "amount": 10.0},
			{"kind": "death", "object": 101, "target": 102},
		],
	}


func _audio_catalog_fixture() -> Array[Dictionary]:
	return [
		{"name": "GondorFighterHorde", "horde": true},
		{"name": "GondorFighter", "horde": false},
		{"name": "GondorArcherHorde", "horde": true},
		{"name": "GondorArcher", "horde": false},
		{"name": "MordorFighterHorde", "horde": true},
		{"name": "MordorFighter", "horde": false},
		{"name": "MordorArcherHorde", "horde": true},
		{"name": "MordorArcher", "horde": false},
	]


func _template_index(catalog: Array[Dictionary], name: String) -> int:
	for index in catalog.size():
		if String(catalog[index].get("name", "")).nocasecmp_to(name) == 0:
			return index
	return -1
