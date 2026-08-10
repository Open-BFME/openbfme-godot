extends SceneTree
## Windowed eyeball capture of the builder sidebar frame (user bug #6): renders
## RetailSideCommandBar + RetailSideCommandFrame over a flat map-coloured
## backdrop and saves a PNG so the result can be compared against the retail
## reference strip. Not a gate — tests/side_command_frame_runner.gd is the gate.
##
##   OPENBFME_SIDEBAR_CAPTURE=<abs .png>   output path (required)
##   OPENBFME_SIDEBAR_FACTION=<key>        faction key for the art slot lookup
##   OPENBFME_SIDEBAR_COUNT=<n>            socket count (default 9, retail Men)
##
## The socket ring art here is a placeholder disc: in game the sockets wear the
## retail palantir atlas socket bound by retail_hud.gd. Everything else — frame
## art, geometry, column placement — is the shipping code path.

const BarScript := preload("res://src/retail_slice/retail_side_command_bar.gd")
const VIEWPORT := Vector2i(1920, 1080)


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := OS.get_environment("OPENBFME_SIDEBAR_CAPTURE").strip_edges()
	if output == "" or output.get_extension().to_lower() != "png":
		push_error("OPENBFME_SIDEBAR_CAPTURE must name a .png output path")
		quit(1)
		return
	if DisplayServer.get_name() == "headless":
		push_error("sidebar capture needs a windowed renderer")
		quit(1)
		return
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(VIEWPORT)
	root.size = VIEWPORT
	await process_frame

	var backdrop := ColorRect.new()
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.34, 0.40, 0.30)
	root.add_child(backdrop)

	var bar = BarScript.new()
	root.add_child(bar)
	bar._build()
	bar.bind_socket_texture(_socket_texture())
	var faction := OS.get_environment("OPENBFME_SIDEBAR_FACTION").strip_edges()
	if faction == "":
		faction = "men"
	var pack_roots: Array = []
	var pack_root := OS.get_environment("OPENBFME_SIDEBAR_PACK_ROOT").strip_edges()
	if pack_root != "":
		pack_roots.append(pack_root)
	var resolved := String(bar.bind_faction(faction, pack_roots))
	print("[sidebar-capture] faction=%s art=%s" % [faction, resolved if resolved != "" else "<procedural default>"])

	var count := 9
	var count_text := OS.get_environment("OPENBFME_SIDEBAR_COUNT")
	if count_text.is_valid_int():
		count = clampi(int(count_text), 1, 24)
	var constructs: Array = []
	for index in count:
		constructs.append({
			"kind": "kind_%d" % index,
			"icon": _icon_texture(index, count),
			"title": "Kind %d" % index,
			"description": "",
		})
	bar.configure_from_constructs(constructs)
	bar.layout_for_viewport(Vector2(VIEWPORT))
	bar.set_builder_visible(true)
	bar.modulate.a = 1.0
	bar.visible = true

	for _index in 30:
		await process_frame
	var image := root.get_texture().get_image()
	if image == null:
		push_error("no framebuffer image")
		quit(1)
		return
	if image.save_png(output) != OK:
		push_error("could not write %s" % output)
		quit(1)
		return
	print("[sidebar-capture] wrote %s" % output)
	quit(0)


func _socket_texture() -> Texture2D:
	var size := 128
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	var center := Vector2(size * 0.5, size * 0.5)
	for y in size:
		for x in size:
			var distance := Vector2(x, y).distance_to(center)
			if distance > size * 0.5:
				continue
			var edge: float = size * 0.5 - distance
			if edge < 7.0:
				image.set_pixel(x, y, Color(0.22, 0.20, 0.17, 1.0).lerp(Color(0.62, 0.58, 0.48, 1.0), edge / 7.0))
			else:
				image.set_pixel(x, y, Color(0.10, 0.11, 0.10, 0.55))
	return ImageTexture.create_from_image(image)


func _icon_texture(index: int, total: int) -> Texture2D:
	var size := 96
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	var hue := float(index) / float(maxi(1, total))
	var body := Color.from_hsv(hue, 0.35, 0.72)
	for y in size:
		for x in size:
			if Vector2(x, y).distance_to(Vector2(size * 0.5, size * 0.5)) <= size * 0.44:
				image.set_pixel(x, y, body)
	return ImageTexture.create_from_image(image)
