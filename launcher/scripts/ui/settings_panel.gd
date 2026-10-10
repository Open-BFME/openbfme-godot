## Lane UI-3: Settings, behind the gear. The channel, the game folders (shortened, the full path only behind "Show folder"), the
## free camera, the offer to download the games, the installed versions and older releases (a rollback is the player's pick), the log
## folder. It only shows and asks; scripts/main.gd does the work (signals).
extends Control

signal channel_picked(channel: String)
signal free_camera_toggled(on: bool)
signal aio_toggled(on: bool)
signal forget_folders
signal use_version(version: String)
signal install_release(tag: String)
signal open_logs
signal show_folder(path: String)

const W := preload("res://scripts/ui/widgets.gd")
const Paths := preload("res://scripts/ui/paths.gd")

var card: PanelContainer
var _stable: Button
var _preview: Button
var _folders: VBoxContainer
var _forget: Button
var _free_camera: CheckBox
var _aio: CheckBox
var _versions: ItemList
var _use: Button
var _releases: OptionButton
var _install: Button
var _about: Label
var _scroll: ScrollContainer


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var m := W.modal("Settings", 760)
	var root: Control = m.root
	add_child(root)
	visible = false
	m.close.pressed.connect(func() -> void: visible = false)
	card = m.card
	_scroll = ScrollContainer.new()
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.custom_minimum_size = Vector2(0, 420)
	m.body.add_child(_scroll)
	var box := W.vbox(16)
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scroll.add_child(box)

	var ch := W.section(box, "Channel")
	var row := W.hbox(4)
	var group := ButtonGroup.new()
	_stable = _seg("Stable", group)
	_preview = _seg("Preview", group)
	row.add_child(_stable)
	row.add_child(_preview)
	row.add_child(W.label("   Preview gets test builds first.", "Muted"))
	ch.add_child(row)
	_stable.pressed.connect(func() -> void: channel_picked.emit("stable"))
	_preview.pressed.connect(func() -> void: channel_picked.emit("preview"))

	var gf := W.section(box, "Game folders")
	_folders = W.vbox(6)
	gf.add_child(_folders)
	var frow := W.hbox(10)
	_forget = W.button("Choose again")
	_forget.tooltip_text = "The game asks for the folders on its next start."
	_forget.pressed.connect(func() -> void: forget_folders.emit())
	frow.add_child(_forget)
	_aio = CheckBox.new()
	_aio.text = "Offer to download the games"
	_aio.toggled.connect(func(on: bool) -> void: aio_toggled.emit(on))
	frow.add_child(_aio)
	gf.add_child(frow)

	var game := W.section(box, "Game")
	_free_camera = CheckBox.new()
	_free_camera.text = "Free camera: zoom out further than the original game"
	_free_camera.tooltip_text = "Applies the next time the game starts. In a game, Ctrl+Z toggles it."
	_free_camera.toggled.connect(func(on: bool) -> void: free_camera_toggled.emit(on))
	game.add_child(_free_camera)

	var ver := W.section(box, "Versions")
	var vrow := W.hbox(16)
	var left := W.vbox(8)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_versions = ItemList.new()
	_versions.custom_minimum_size = Vector2(0, 74)
	left.add_child(_versions)
	_use = W.button("Play this version", "play")
	_use.pressed.connect(_on_use)
	left.add_child(_use)
	vrow.add_child(left)
	var right := W.vbox(8)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(W.wrap("Install another release of the channel, also an older one.", "Muted"))
	_releases = OptionButton.new()
	right.add_child(_releases)
	_install = W.button("Install", "download")
	_install.pressed.connect(func() -> void:
		if _releases.item_count > 0:
			install_release.emit(_releases.get_item_text(_releases.selected)))
	right.add_child(_install)
	vrow.add_child(right)
	ver.add_child(vrow)

	var logs := W.section(box, "Logs")
	var lrow := W.hbox(12)
	var open := W.button("Open log folder", "folder")
	open.pressed.connect(func() -> void: open_logs.emit())
	lrow.add_child(open)
	lrow.add_child(W.label("Attach launcher.log to a bug report.", "Muted"))
	logs.add_child(lrow)
	_about = W.label("", "Muted")
	box.add_child(_about)
	resized.connect(_fit)


func _seg(text: String, group: ButtonGroup) -> Button:
	var b := Button.new()
	b.text = text
	b.toggle_mode = true
	b.button_group = group
	b.theme_type_variation = "SegButton"
	return b


## the scroll area fills what the window has (the card stays inside 1280×720 and the Deck's 1280×800)
func _fit() -> void:
	_scroll.custom_minimum_size.y = clampf(size.y - 150, 240, 900)


func set_channel(ch: String) -> void:
	_stable.set_pressed_no_signal(ch == "stable")
	_preview.set_pressed_no_signal(ch == "preview")


func set_busy(busy: bool) -> void:
	_stable.disabled = busy
	_preview.disabled = busy
	_use.disabled = busy or _versions.item_count == 0
	_install.disabled = busy or _releases.item_count == 0
	_aio.disabled = busy


func set_options(free_camera: bool, aio_on: bool, about: String) -> void:
	_free_camera.set_pressed_no_signal(free_camera)
	_aio.set_pressed_no_signal(aio_on)
	_about.text = about


## folders: Paths.game_folders(); demo: the screenshots' made-up folders (Show folder stays enabled)
func set_folders(f: Dictionary, demo := false) -> void:
	for c in _folders.get_children():
		c.queue_free()
	if f.rotwk == "" and f.bfme2 == "":
		_folders.add_child(W.wrap("Not chosen yet. The game looks for them on its first start.", "Muted"))
	else:
		for g in [["The Rise of the Witch-king", f.rotwk], ["Battle for Middle-earth II", f.bfme2]]:
			var row := W.hbox(12)
			var name := W.label(g[0])
			name.custom_minimum_size.x = 230
			row.add_child(name)
			var p := W.label(Paths.short(g[1]), "Muted")
			p.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			p.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
			p.clip_text = true
			row.add_child(p)
			var show := W.button("Show folder", "folder")
			var full: String = g[1]
			show.disabled = not demo and not DirAccess.dir_exists_absolute(full)
			show.pressed.connect(func() -> void: show_folder.emit(full))
			row.add_child(show)
			_folders.add_child(row)
	_forget.visible = f.source == "config"


## installed: [{version}], play: the version Play starts, releases: the channel's tags (newest first)
func set_versions(installed: Array, play: String, releases: Array) -> void:
	_versions.clear()
	for i in installed:
		var label: String = i.version
		if i.version == play:
			label += "    (Play starts this)"
		_versions.add_item(label)
		_versions.set_item_metadata(_versions.item_count - 1, i.version)
	if installed.is_empty():
		_versions.add_item("Nothing installed yet")
		_versions.set_item_disabled(0, true)
		_versions.set_item_metadata(0, "")
	_releases.clear()
	for t in releases:
		_releases.add_item(t)


func _on_use() -> void:
	var sel := _versions.get_selected_items()
	if sel.is_empty():
		return
	var v: String = _versions.get_item_metadata(sel[0])
	if v != "":
		use_version.emit(v)
