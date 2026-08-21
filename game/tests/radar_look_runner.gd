extends SceneTree
## Pins the retail radar's authored house-color seam and absence of invented
## dark marker halos. This is deliberately a headless contract runner: Godot's
## headless SubViewport does not present reliably, so a pixel wait can hang.

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
const HouseColorScript := preload("res://src/retail_slice/retail_house_color.gd")
const SELECTED_BLUE := Color8(70, 91, 156)

var passed := 0
var failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("RADAR_LOOK PASS %s" % name)
	else:
		failed += 1
		print("RADAR_LOOK FAIL %s | %s" % [name, detail])


func _close(actual: Color, expected: Color, tolerance := 0.0001) -> bool:
	return (
		absf(actual.r - expected.r) <= tolerance
		and absf(actual.g - expected.g) <= tolerance
		and absf(actual.b - expected.b) <= tolerance
	)


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	HouseColorScript.team_color_overrides[0] = SELECTED_BLUE
	var minimap = MinimapScript.new()
	var has_color_seam := minimap.has_method("blip_color_for_team")
	_check(
		"radar_exposes_the_house_color_seam",
		has_color_seam,
		"RetailMinimap.blip_color_for_team is absent"
	)
	if has_color_seam:
		var actual := Color(minimap.call("blip_color_for_team", 0))
		_check(
			"radar_blip_uses_selected_house_color",
			_close(actual, SELECTED_BLUE),
			"actual=%s expected=%s" % [actual, SELECTED_BLUE]
		)
	else:
		_check("radar_blip_uses_selected_house_color", false, "color seam unavailable")

	var source := FileAccess.get_file_as_string("res://src/retail_slice/retail_minimap.gd")
	_check(
		"unit_blip_has_no_invented_dark_halo",
		not source.contains("draw_circle(point, 3.4, Color(0.16, 0.11, 0.05, 0.85))"),
		"legacy 3.4px dark halo draw remains"
	)
	_check(
		"structure_blip_has_no_invented_dark_halo",
		not source.contains("Vector2(6.0, 6.0)), Color(0.16, 0.11, 0.05, 0.85)"),
		"legacy 6px dark halo draw remains"
	)

	var has_edge_seam := minimap.has_method("bind_retail_view_box_edge")
	_check(
		"radar_exposes_the_authored_view_box_edge_seam",
		has_edge_seam,
		"RetailMinimap.bind_retail_view_box_edge is absent"
	)
	if has_edge_seam:
		var wrong_size := ImageTexture.create_from_image(
			Image.create(8, 8, false, Image.FORMAT_RGBA8)
		)
		var authored_crop := ImageTexture.create_from_image(
			Image.create(7, 8, false, Image.FORMAT_RGBA8)
		)
		_check(
			"view_box_edge_rejects_wrong_crop_dimensions",
			not bool(minimap.call("bind_retail_view_box_edge", wrong_size))
		)
		_check(
			"view_box_edge_accepts_the_authored_7_by_8_crop",
			bool(minimap.call("bind_retail_view_box_edge", authored_crop))
				and bool(minimap.get("uses_retail_view_box_edge"))
				and String(minimap.get("view_box_edge_source")) == "RadarViewBoxEdge"
		)
	else:
		_check("view_box_edge_rejects_wrong_crop_dimensions", false, "binding seam unavailable")
		_check("view_box_edge_accepts_the_authored_7_by_8_crop", false, "binding seam unavailable")

	HouseColorScript.team_color_overrides.clear()
	minimap.free()
	print("RADAR_LOOK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
