## Lane UI-3: Download the games (lane AIO-1's download, shown only while no game folders are found). One line names the source and links
## its page; nothing downloads until the player presses Download (that press is the consent scripts/core/aio_install.gd logs).
extends Control

signal download(folder: String)

const W := preload("res://scripts/ui/widgets.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")

var folder := ""
var _what: Label
var _where: Label
var _dialog: FileDialog


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var m := W.modal("Download the games", 640)
	add_child(m.root)
	visible = false
	m.close.pressed.connect(func() -> void: visible = false)
	var body: VBoxContainer = m.body
	_what = W.wrap("")
	body.add_child(_what)
	var src := W.wrap("Files from the All In One BFME Launcher service (BFME Foundation, Patch 2.22 team).", "Muted")
	body.add_child(src)
	var link := LinkButton.new()
	link.text = AioInstall.PAGE.trim_prefix("https://")
	link.uri = AioInstall.PAGE
	body.add_child(link)
	var to := W.section(body, "Install to")
	var row := W.hbox(10)
	_where = W.label("")
	_where.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_where.clip_text = true
	_where.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	row.add_child(_where)
	var change := W.button("Change…", "folder")
	change.pressed.connect(func() -> void:
		_dialog.current_dir = folder if DirAccess.dir_exists_absolute(folder) else Paths.home()
		_dialog.popup_centered_ratio(0.75))
	row.add_child(change)
	to.add_child(row)
	var buttons := W.hbox(10)
	buttons.add_child(W.spacer())
	var cancel := W.button("Not now")
	cancel.pressed.connect(func() -> void: visible = false)
	buttons.add_child(cancel)
	var go := W.button("Download", "download", "GoldButton")
	go.custom_minimum_size.x = 160
	go.pressed.connect(func() -> void:
		visible = false
		download.emit(folder))
	buttons.add_child(go)
	body.add_child(buttons)
	_dialog = FileDialog.new()
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.title = "Where should the games go?"
	_dialog.dir_selected.connect(func(d: String) -> void: set_folder(d))
	add_child(_dialog)


## gb: the download's size; packages: AioInstall.packages (their folder names)
func setup(dir: String, gb: float, packages: Array) -> void:
	var names := PackedStringArray()
	for p in packages:
		names.append(str(p.get("label", p.get("folder", ""))))
	_what.text = "%s, about %.1f GB." % [" and ".join(names), gb] if not names.is_empty() else "About %.1f GB." % gb
	set_folder(dir)


func set_folder(dir: String) -> void:
	folder = dir
	_where.text = Paths.short(dir) if dir != "" else "(pick a folder)"
