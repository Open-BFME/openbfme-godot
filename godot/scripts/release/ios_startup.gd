extends CanvasLayer
## Draw before parsing/instantiating the game and retain feedback during data mounting.
var status: Label
var began := Time.get_ticks_msec()

func _ready() -> void:
	layer = 80
	add_to_group("ios_startup")
	var background := ColorRect.new()
	background.color = Color("10191e")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	status = Label.new()
	status.text = "Starting OpenBFME…"
	status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	status.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	status.add_theme_font_size_override("font_size", 28)
	add_child(status)
	print("STARTUP iOS loading screen ready at %d ms" % Time.get_ticks_msec())
	await RenderingServer.frame_post_draw
	ResourceLoader.load_threaded_request("res://scenes/game.tscn")
	while ResourceLoader.load_threaded_get_status("res://scenes/game.tscn") == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		await get_tree().process_frame
	var scene = ResourceLoader.load_threaded_get("res://scenes/game.tscn")
	if scene == null:
		status.text = "Could not load OpenBFME."
		return
	print("STARTUP game scene loaded in %d ms" % (Time.get_ticks_msec() - began))
	get_tree().root.add_child(scene.instantiate())

func stage(text: String) -> void:
	status.text = text
	print("STARTUP %s at %d ms" % [text, Time.get_ticks_msec()])

func finished() -> void:
	print("STARTUP initial loading complete at %d ms" % Time.get_ticks_msec())
	queue_free()
