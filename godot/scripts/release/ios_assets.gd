## iOS external assets: native directory picker; data stays on the attached drive.
extends CanvasLayer
signal finished(result: Dictionary)
var _setup: RefCounted
var _fs: RefCounted
var _status: Label
var _last_path := ""

func run(fs: RefCounted, setup: RefCounted) -> Dictionary:
	_fs = fs
	_setup = setup
	layer = 90
	var background := ColorRect.new()
	background.color = Color("10191e")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 40)
	add_child(margin)
	var box := VBoxContainer.new()
	margin.add_child(box)
	var title := Label.new()
	title.text = "OpenBFME — game data on USB drive"
	title.add_theme_font_size_override("font_size", 28)
	box.add_child(title)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.text = "Connect your drive, then choose OpenBFME or its retail folder in Files. The data stays on the drive. Keep it connected while playing."
	box.add_child(_status)
	var choose := Button.new()
	choose.text = "Choose folder on drive"
	choose.custom_minimum_size.y = 64
	choose.pressed.connect(func() -> void:
		_last_path = ""
		_setup.pick_external_assets())
	box.add_child(choose)
	call_deferred("_start_access")
	return await finished

func _start_access() -> void:
	# Let iPadOS finish activating the scene before presenting its Files browser.
	await get_tree().create_timer(0.5).timeout
	var access: Dictionary = _setup.external_assets()
	if access.state == "selected":
		_check()
	else:
		_setup.pick_external_assets()

func _process(_delta: float) -> void:
	if _setup != null:
		_check()

func _check() -> void:
	var access: Dictionary = _setup.external_assets()
	if access.state == "error":
		_status.text = str(access.error)
		return
	if access.state != "selected" or access.path == _last_path:
		return
	_last_path = access.path
	var base: String = access.path
	if DirAccess.dir_exists_absolute(base.path_join("retail")):
		base = base.path_join("retail")
	var rotwk := base.path_join("rotwk")
	var bfme2 := base.path_join("bfme2")
	_status.text = "Checking game archives on the drive…"
	var mount: Dictionary = _fs.mount_retail_paths(rotwk, bfme2)
	if not mount.ok:
		_status.text = "Choose the folder containing rotwk and bfme2.\n" + "\n".join(mount.errors)
		return
	var saved: Dictionary = _setup.remember(rotwk, bfme2)
	if not saved.ok:
		_status.text = str(saved.error)
		return
	set_process(false)
	print("RELEASE game folders: iOS external drive")
	finished.emit(mount)
