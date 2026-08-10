extends SceneTree
## Pins the per-faction builder sidebar frame (user bug #6): the right-edge
## build column must live inside an ornate vertical frame container, the frame
## must be right-anchored and carry the retail reference proportions, the
## faction's building icons must sit on the frame's icon column, and a
## per-faction drop-in PNG must resolve through the documented slot.
##
## Failing-first teeth:
##   * delete the frame child from RetailSideCommandBar -> frame_exists red
##   * unpin the right edge / aspect -> frame_right_anchored / frame_aspect red
##   * lay buttons outside the frame -> buttons_inside_frame red
##   * drop the per-faction lookup pass -> faction_art_beats_default red
##
## Headless: no content pack, no window. Fixture PNGs are written under a
## temp root so the drop-in contract is exercised against real files.

const BarScript := preload("res://src/retail_slice/retail_side_command_bar.gd")
const FrameScript := preload("res://src/retail_slice/retail_side_command_frame.gd")

var passed := 0
var failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("SIDE_COMMAND_FRAME PASS %s" % name)
	else:
		failed += 1
		print("SIDE_COMMAND_FRAME FAIL %s | %s" % [name, detail])


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	_check_reference_oracle()
	_check_geometry_contract()
	_check_frame_widget()
	_check_bar_layout()
	_check_art_slot()
	_check_default_texture()
	_check_hud_wiring()
	print("SIDE_COMMAND_FRAME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)


func _check_reference_oracle() -> void:
	## EXTERNAL ORACLE. The socket column is not allowed to be self-asserted: at
	## the viewport where the frame renders at the reference crop's native size,
	## the sockets the shipping layout produces must land on the ring positions
	## measured off that crop with a pixel grid (147 x 1074; ring walls at
	## x = 32 and x = 140, ring height 84, nine centres 185 .. 875).
	## Change a ratio without re-measuring the reference and this goes red.
	var viewport := Vector2(1920.0, FrameScript.REFERENCE_ART_SIZE.y / FrameScript.FRAME_HEIGHT_RATIO)
	var rect: Rect2 = FrameScript.frame_rect(viewport)
	_check(
		"oracle_frame_renders_at_reference_size",
		rect.size.distance_to(FrameScript.REFERENCE_ART_SIZE) < 0.75,
		"%s vs %s" % [str(rect.size), str(FrameScript.REFERENCE_ART_SIZE)]
	)
	var icon: Vector2 = FrameScript.icon_size(viewport)
	var expected_width: float = FrameScript.REFERENCE_ICON_RIGHT - FrameScript.REFERENCE_ICON_LEFT
	_check(
		"oracle_socket_width_matches_reference",
		absf(icon.x - expected_width) <= 1.5,
		"%f vs %f" % [icon.x, expected_width]
	)
	_check(
		"oracle_socket_height_matches_reference",
		absf(icon.y - FrameScript.REFERENCE_ICON_HEIGHT) <= 1.5,
		"%f vs %f" % [icon.y, FrameScript.REFERENCE_ICON_HEIGHT]
	)
	var left_in_frame: float = FrameScript.icon_column_center_x(viewport) - icon.x * 0.5 - rect.position.x
	_check(
		"oracle_socket_left_wall_matches_reference",
		absf(left_in_frame - FrameScript.REFERENCE_ICON_LEFT) <= 1.5,
		"%f vs %f" % [left_in_frame, FrameScript.REFERENCE_ICON_LEFT]
	)

	# In-tree: out of the tree a Control never applies its combined minimum size,
	# so socket rects measured there can differ from what the player sees.
	var bar = BarScript.new()
	root.add_child(bar)
	bar._build()
	var constructs: Array = []
	for index in FrameScript.REFERENCE_ICON_CENTERS_Y.size():
		constructs.append({"kind": "kind_%d" % index, "icon": null, "title": "", "description": ""})
	bar.configure_from_constructs(constructs)
	bar.layout_for_viewport(viewport)
	var worst := 0.0
	var worst_detail := ""
	var buttons: Array = bar.side_buttons()
	for index in buttons.size():
		var center_y: float = buttons[index].position.y + buttons[index].size.y * 0.5 - rect.position.y
		var expected: float = FrameScript.REFERENCE_ICON_CENTERS_Y[index]
		var error := absf(center_y - expected)
		if error > worst:
			worst = error
			worst_detail = "row %d: %f vs %f" % [index, center_y, expected]
	_check("oracle_socket_centres_match_reference", worst <= 2.0, "worst=%f %s" % [worst, worst_detail])
	bar.free()


func _check_geometry_contract() -> void:
	var viewport := Vector2(1920.0, 1080.0)
	var rect: Rect2 = FrameScript.frame_rect(viewport)
	_check(
		"frame_right_anchored",
		is_equal_approx(rect.end.x, viewport.x) and rect.position.x > viewport.x * 0.5,
		"rect=%s" % str(rect)
	)
	_check(
		"frame_vertical_span",
		rect.size.y > viewport.y * 0.85 and rect.position.y >= 0.0 and rect.end.y <= viewport.y,
		"rect=%s" % str(rect)
	)
	# Proportions measured off the retail reference crop (147 x 1074 px).
	var aspect := rect.size.x / rect.size.y
	_check(
		"frame_aspect_matches_reference",
		absf(aspect - FrameScript.FRAME_ASPECT) < 0.005 and absf(FrameScript.FRAME_ASPECT - (147.0 / 1074.0)) < 0.002,
		"aspect=%f contract=%f" % [aspect, FrameScript.FRAME_ASPECT]
	)
	# A 4:3 viewport must still pin the frame to the right edge.
	var narrow: Rect2 = FrameScript.frame_rect(Vector2(1024.0, 768.0))
	_check(
		"frame_right_anchored_narrow",
		is_equal_approx(narrow.end.x, 1024.0) and narrow.size.x > 0.0,
		"rect=%s" % str(narrow)
	)
	var center_x: float = FrameScript.icon_column_center_x(viewport)
	_check(
		"icon_column_inside_frame",
		center_x > rect.position.x and center_x < rect.end.x,
		"center=%f rect=%s" % [center_x, str(rect)]
	)


func _check_frame_widget() -> void:
	var bar = BarScript.new()
	bar._build()
	var frame = bar.frame_node()
	_check("frame_exists", frame != null and frame is Control, str(frame))
	if frame == null:
		return
	_check("frame_named", String(frame.name) == FrameScript.FRAME_NODE_NAME, String(frame.name))
	_check("frame_is_child_of_bar", frame.get_parent() == bar, str(frame.get_parent()))
	_check("frame_behind_buttons", bar.get_child(0) == frame, "index=%d" % frame.get_index())
	_check("frame_ignores_mouse", frame.mouse_filter == Control.MOUSE_FILTER_IGNORE, str(frame.mouse_filter))
	bar.free()


func _check_bar_layout() -> void:
	var bar = BarScript.new()
	root.add_child(bar)
	bar._build()
	var kinds := ["farm", "barracks", "archery_range", "stable", "well", "forge", "battle_tower"]
	var constructs: Array = []
	for kind in kinds:
		var icon := ImageTexture.create_from_image(Image.create(16, 16, false, Image.FORMAT_RGBA8))
		constructs.append({"kind": kind, "icon": icon, "title": kind, "description": ""})
	bar.configure_from_constructs(constructs)
	var buttons: Array = bar.side_buttons()
	_check("bar_holds_faction_buildings", buttons.size() == kinds.size(), "buttons=%d" % buttons.size())
	var seen: Array[String] = []
	var iconed := 0
	for button in buttons:
		seen.append(String(button.get_meta("construct_kind", "")))
		if button.icon != null:
			iconed += 1
	_check("bar_keeps_kind_order", seen == kinds, str(seen))
	_check("bar_keeps_building_icons", iconed == kinds.size(), "iconed=%d" % iconed)

	var viewport := Vector2(1920.0, 1080.0)
	bar.layout_for_viewport(viewport)
	var rect: Rect2 = FrameScript.frame_rect(viewport)
	var frame = bar.frame_node()
	_check(
		"frame_node_matches_contract_rect",
		frame.position.is_equal_approx(rect.position) and frame.size.is_equal_approx(rect.size),
		"node=%s contract=%s" % [str(Rect2(frame.position, frame.size)), str(rect)]
	)
	var all_inside := true
	var centered := true
	var expected_center: float = FrameScript.icon_column_center_x(viewport)
	var worst := ""
	for button in buttons:
		var button_rect := Rect2(button.position, button.size)
		if button_rect.position.y < rect.position.y - 0.5 or button_rect.end.y > rect.end.y + 0.5:
			all_inside = false
			worst = str(button_rect)
		if button_rect.end.x > rect.end.x + 0.5 or button_rect.position.x < rect.position.x - 0.5:
			all_inside = false
			worst = str(button_rect)
		if absf(button_rect.get_center().x - expected_center) > 1.0:
			centered = false
			worst = str(button_rect)
	_check("buttons_inside_frame", all_inside, "frame=%s worst=%s" % [str(rect), worst])
	_check("buttons_on_icon_column", centered, "center=%f worst=%s" % [expected_center, worst])
	# Buttons must not overlap into a stack: the column pitch keeps them ordered.
	var ordered := true
	for index in range(1, buttons.size()):
		if buttons[index].position.y <= buttons[index - 1].position.y:
			ordered = false
	_check("buttons_ordered_down_column", ordered)

	# The frame hides and shows with the builder selection, like the sockets.
	bar.set_builder_visible(true)
	_check("frame_visible_with_builder", bar.visible and frame.visible, "bar=%s frame=%s" % [str(bar.visible), str(frame.visible)])
	bar.set_builder_visible(false)
	_check("frame_hidden_without_builder", not bar.builder_bar_shown(), str(bar.builder_bar_shown()))
	bar.free()
	_check_crowded_roster()


func _check_crowded_roster() -> void:
	## Retail's porter set is nine sockets; ours can carry more. A crowded roster
	## must shrink to fit the SAME band, never spill past the frame ends and never
	## leave the icon column. 14 sockets is past the point where natural-size
	## sockets stop fitting, so this exercises the shrink path in-tree.
	for count in [12, 14]:
		var bar = BarScript.new()
		root.add_child(bar)
		bar._build()
		var constructs: Array = []
		for index in count:
			constructs.append({"kind": "kind_%d" % index, "icon": null, "title": "", "description": ""})
		bar.configure_from_constructs(constructs)
		var viewport := Vector2(1920.0, 1080.0)
		bar.layout_for_viewport(viewport)
		var rect: Rect2 = FrameScript.frame_rect(viewport)
		var band: Vector2 = FrameScript.icon_band(viewport)
		var center_x: float = FrameScript.icon_column_center_x(viewport)
		var buttons: Array = bar.side_buttons()
		var inside: bool = buttons.size() == count
		var single_column := true
		var worst := ""
		for index in buttons.size():
			var button_rect := Rect2(buttons[index].position, buttons[index].size)
			if button_rect.position.y < band.x - 0.5 or button_rect.end.y > band.y + 0.5:
				inside = false
				worst = str(button_rect)
			if button_rect.position.x < rect.position.x - 0.5 or button_rect.end.x > rect.end.x + 0.5:
				inside = false
				worst = str(button_rect)
			if absf(button_rect.get_center().x - center_x) > 1.0:
				single_column = false
				worst = str(button_rect)
			if index > 0 and button_rect.position.y <= buttons[index - 1].position.y:
				inside = false
				worst = str(button_rect)
		_check("crowded_%d_sockets_stay_in_band" % count, inside, "band=%s worst=%s" % [str(band), worst])
		_check("crowded_%d_sockets_stay_one_column" % count, single_column, "center=%f worst=%s" % [center_x, worst])
		bar.free()


func _check_art_slot() -> void:
	var root := "user://test_sidebar_frames"
	var alt_root := "user://test_sidebar_frames_alt"
	for dir in [root, alt_root]:
		DirAccess.make_dir_recursive_absolute(dir)
	_write_fixture_png("%s/sidebar_frame_default.png" % root)
	_write_fixture_png("%s/sidebar_frame_dwarves.png" % alt_root)

	# Higher-priority root only has the default; the faction file lives in the
	# lower-priority root. Faction art must still win — that is the whole point
	# of the drop-in slot.
	var roots := [root, alt_root]
	var dwarves: String = FrameScript.resolve_frame_art_path("dwarves", roots)
	_check(
		"faction_art_beats_default",
		dwarves == "%s/sidebar_frame_dwarves.png" % alt_root,
		dwarves
	)
	var mordor: String = FrameScript.resolve_frame_art_path("mordor", roots)
	_check("unknown_faction_falls_back_to_default", mordor == "%s/sidebar_frame_default.png" % root, mordor)
	var empty: String = FrameScript.resolve_frame_art_path("mordor", [alt_root])
	_check("no_art_resolves_empty", empty == "", empty)
	var cased: String = FrameScript.resolve_frame_art_path("DWARVES", roots)
	_check("faction_lookup_is_case_folded", cased == "%s/sidebar_frame_dwarves.png" % alt_root, cased)

	# bind_faction takes MOUNTED PACK ROOTS, so the pack-relative slot is what a
	# shipped content pack must carry.
	var pack_root := "user://test_sidebar_pack"
	var pack_slot := "%s/%s" % [pack_root, FrameScript.PACK_ART_RELATIVE]
	DirAccess.make_dir_recursive_absolute(pack_slot)
	_write_fixture_png("%s/sidebar_frame_dwarves.png" % pack_slot)

	var bar = BarScript.new()
	bar._build()
	var bound: String = bar.bind_faction("dwarves", [pack_root])
	_check("bind_faction_reports_slot", bound == "%s/sidebar_frame_dwarves.png" % pack_slot, bound)
	_check("bind_faction_loads_texture", bar.frame_node().frame_art() != null, "null texture")
	var none: String = bar.bind_faction("mordor", [pack_root])
	_check("bind_faction_without_art_is_procedural", none == "" and bar.frame_node().frame_art() == null, none)
	bar.free()
	DirAccess.remove_absolute("%s/sidebar_frame_dwarves.png" % pack_slot)
	var walk := pack_slot
	while walk != "" and walk.begins_with(pack_root):
		DirAccess.remove_absolute(walk)
		walk = walk.get_base_dir()

	# The documented search roots must include the user drop-in directory and
	# each mounted pack, so the owner can add art without a repo rebuild.
	var search: Array = FrameScript.default_search_roots(["C:/packs/men"])
	_check(
		"search_roots_expose_user_slot",
		search.has(FrameScript.USER_ART_ROOT) and search[0] == FrameScript.USER_ART_ROOT,
		str(search)
	)
	_check(
		"search_roots_expose_pack_and_base",
		search.has("C:/packs/men/%s" % FrameScript.PACK_ART_RELATIVE) and search.has(FrameScript.BASE_ART_ROOT),
		str(search)
	)

	for path in [
		"%s/sidebar_frame_default.png" % root,
		"%s/sidebar_frame_dwarves.png" % alt_root,
	]:
		DirAccess.remove_absolute(path)
	for dir in [root, alt_root]:
		DirAccess.remove_absolute(dir)


func _check_default_texture() -> void:
	# The shipped default is generated in-repo (no retail bytes): it must be a
	# real transparent-edged strip at the contract aspect, not a solid block.
	var texture: Texture2D = FrameScript.default_frame_texture()
	_check("default_texture_built", texture != null, str(texture))
	if texture == null:
		return
	var size := texture.get_size()
	_check(
		"default_texture_uses_contract_size",
		size.is_equal_approx(FrameScript.REFERENCE_ART_SIZE),
		"%s vs %s" % [str(size), str(FrameScript.REFERENCE_ART_SIZE)]
	)
	var image := texture.get_image()
	var opaque := 0
	var transparent := 0
	for y in range(0, image.get_height(), 7):
		for x in range(image.get_width()):
			if image.get_pixel(x, y).a > 0.9:
				opaque += 1
			elif image.get_pixel(x, y).a < 0.05:
				transparent += 1
	_check("default_texture_has_body", opaque > 200, "opaque=%d" % opaque)
	_check("default_texture_has_transparency", transparent > 200, "transparent=%d" % transparent)
	# Alpha profile ACROSS THE SOCKET COLUMN (x 32..140 in reference px). The left
	# of the column must be clear so the icons read against the map, and the band
	# must be opaque behind their right side - that split IS the retail
	# composition. Sampling outside 32..140 proves nothing about the icon column.
	var column_left: float = FrameScript.REFERENCE_ICON_LEFT
	var column_right: float = FrameScript.REFERENCE_ICON_RIGHT
	var band_left: float = FrameScript.REFERENCE_ART_SIZE.x * FrameScript.BAND_LEFT_RATIO
	_check(
		"band_edge_splits_the_socket_column",
		band_left > column_left + 8.0 and band_left < column_right - 8.0,
		"band_left=%f column=%f..%f" % [band_left, column_left, column_right]
	)
	var clear_x := int(column_left + (band_left - column_left) * 0.5)
	var backed_x := int(band_left + (column_right - band_left) * 0.5)
	var clear_alpha := 0.0
	var backed_alpha := 0.0
	var samples := 0
	for y in range(200, 800, 11):
		clear_alpha += image.get_pixel(clear_x, y).a
		backed_alpha += image.get_pixel(backed_x, y).a
		samples += 1
	_check(
		"socket_column_clear_left_of_band",
		clear_alpha < 1.0,
		"x=%d alpha_sum=%f over %d rows" % [clear_x, clear_alpha, samples]
	)
	_check(
		"socket_column_backed_right_of_band",
		backed_alpha > float(samples) * 0.8,
		"x=%d alpha_sum=%f over %d rows" % [backed_x, backed_alpha, samples]
	)


func _check_hud_wiring() -> void:
	## End-to-end: selecting a builder reveals the framed column AND picks up that
	## faction's drop-in art from the documented user slot.
	##
	## The fixture faction key is deliberately NOT a real faction. This has to
	## write into the production user slot to prove the real lookup path, and an
	## aborted run can leave the file behind - under a real key that stray white
	## PNG would become the player's actual frame for that faction.
	var user_slot: String = FrameScript.USER_ART_ROOT
	var fixture_faction := "zz_runner_fixture"
	var fixture := "%s/sidebar_frame_%s.png" % [user_slot, fixture_faction]
	if FileAccess.file_exists(fixture):
		_check("hud_user_slot_fixture_free", false, "refusing to overwrite owner art at %s" % fixture)
		return
	DirAccess.make_dir_recursive_absolute(user_slot)
	_write_fixture_png(fixture)

	var hud_script: Script = load("res://src/retail_slice/retail_hud.gd")
	var hud = hud_script.new()
	root.add_child(hud)
	hud.build()
	hud.configure_manifest_construct_kinds(["farm", "barracks", "well"], fixture_faction)
	hud.set_unit_selection_state(
		[91] as Array[int],
		{91: {"object_id": "bfme2.object.dwarven-builder", "is_builder": true}}
	)
	var bar = hud.retail_side_command_bar
	_check("hud_sidebar_exists", bar != null, str(bar))
	if bar != null:
		var frame = bar.frame_node()
		_check("hud_sidebar_has_frame", frame != null, str(frame))
		_check("hud_sidebar_visible_for_builder", bar.builder_bar_shown(), str(bar.builder_bar_shown()))
		if frame != null:
			_check(
				"hud_binds_faction_frame_art",
				frame.frame_art_path() == fixture,
				"%s vs %s" % [frame.frame_art_path(), fixture]
			)
			var viewport: Vector2 = hud.get_viewport_rect().size
			var rect: Rect2 = FrameScript.frame_rect(viewport)
			_check(
				"hud_frame_right_anchored",
				absf((frame.position.x + frame.size.x) - rect.end.x) < 0.5 and frame.size.x > 0.0,
				"frame=%s contract=%s" % [str(Rect2(frame.position, frame.size)), str(rect)]
			)
	hud.queue_free()
	DirAccess.remove_absolute(fixture)


func _write_fixture_png(path: String) -> void:
	var image := Image.create(8, 64, false, Image.FORMAT_RGBA8)
	image.fill(Color(1.0, 1.0, 1.0, 1.0))
	image.save_png(path)
