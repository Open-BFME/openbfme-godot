## Lane UI-5: Game files. Which games were found and where (shortened: the full path only behind "Show folder"), "Choose again" (the game
## asks for its folders on its next start), and the All In One download (lane AIO-1: what, from where, into which folder, its progress
## with Pause and Cancel). Lane UI-3's Settings folders and its "Download the games" dialog, moved here; scripts/main.gd does the work.
## Nothing downloads until the player presses Download: that press is the consent scripts/core/aio_install.gd logs.
extends Control

signal forget_folders
signal show_folder(path: String)
signal download(folder: String)
signal pause_pressed
signal cancel_pressed

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")

var folder := ""
var _found_title: Label
var _games: VBoxContainer
var _folders: VBoxContainer
var _forget: Button
var _what: Label
var _where: Label
var _go: Button
var _off: Label
var _progress: VBoxContainer
var _bar: ProgressBar
var _bar_label: Label
var _pause: Button
var _cancel: Button
var _dialog: FileDialog


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Game files"))
	var s := W.scroll(Style.SCREEN_GAP)
	v.add_child(s.scroll)
	var grid := W.hbox(16)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.box.add_child(grid)

	var found := W.panel(4)
	found.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	found.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(found.panel)
	_found_title = W.h3("Found")
	found.box.add_child(_found_title)
	_games = W.vbox(0)
	found.box.add_child(_games)
	_folders = W.vbox(6)
	found.box.add_child(_folders)
	_forget = W.button("Choose again", "refresh", "", 15)
	_forget.tooltip_text = "The game asks for the folders on its next start."
	_forget.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_forget.pressed.connect(func() -> void: forget_folders.emit())
	found.box.add_child(_forget)

	var dl := W.panel(8)
	dl.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	dl.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(dl.panel)
	dl.box.add_child(W.h3("Download the games"))
	_what = W.wrap("", "Muted")
	dl.box.add_child(_what)
	dl.box.add_child(W.wrap("Files from the All In One BFME Launcher service (BFME Foundation, Patch 2.22 team). Every file is checked.", "Muted"))
	var link := LinkButton.new()
	link.text = AioInstall.PAGE.trim_prefix("https://")
	link.uri = AioInstall.PAGE
	link.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	dl.box.add_child(link)
	var row := W.hbox(10)
	var to := W.label("Install to", "Muted")
	row.add_child(to)
	_where = W.trim_label("")
	_where.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(_where)
	var change := W.button("Change…", "folder", "", 15)
	change.pressed.connect(open_picker)
	row.add_child(change)
	dl.box.add_child(row)
	_progress = W.vbox(6)
	_progress.visible = false
	_bar_label = W.trim_label("", "Muted")
	_progress.add_child(_bar_label)
	_bar = W.progress_bar()
	_progress.add_child(_bar)
	dl.box.add_child(_progress)
	var buttons := W.hbox(8)
	_go = W.button("Download", "download", "", 16)
	_go.pressed.connect(func() -> void: download.emit(folder))
	buttons.add_child(_go)
	_pause = W.button("Pause", "pause", "", 14)
	_pause.pressed.connect(func() -> void: pause_pressed.emit())
	buttons.add_child(_pause)
	_cancel = W.button("Cancel", "stop", "", 14)
	_cancel.pressed.connect(func() -> void: cancel_pressed.emit())
	buttons.add_child(_cancel)
	dl.box.add_child(buttons)
	_off = W.wrap("Offering the download is off (Settings, General).", "Muted")
	dl.box.add_child(_off)
	_dialog = FileDialog.new()
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.title = "Where should the games go?"
	_dialog.dir_selected.connect(func(d: String) -> void: set_folder(d))
	add_child(_dialog)
	set_download_state(false, false, false, true)


## the folder picker ("Change…"); `demo` (the layout test) opens it on the home folder
func open_picker(demo := false) -> void:
	_dialog.current_dir = Paths.home() if demo or not DirAccess.dir_exists_absolute(folder) else folder
	_dialog.popup_centered_ratio(0.75)


## folders: Paths.game_folders(); demo: the screenshots' made-up folders (Show folder stays enabled)
func set_folders(f: Dictionary, demo := false) -> void:
	_found_title.text = "FOUND" if f.found else "NOT FOUND"
	for c in _games.get_children() + _folders.get_children():
		c.queue_free()
	var games := [["The Rise of the Witch-king 2.01", f.rotwk], ["Battle for Middle-earth II 1.06", f.bfme2]]
	for i in games.size():
		var ok := String(games[i][1]) != "" and (demo or DirAccess.dir_exists_absolute(games[i][1]))
		var mark := W.label("✓" if ok else "—")
		mark.add_theme_color_override("font_color", Style.GOOD if ok else Style.TEXT3)
		_games.add_child(W.row(String(games[i][0]), mark, i == games.size() - 1))
	if f.rotwk == "" and f.bfme2 == "":
		_folders.add_child(W.wrap("Not chosen yet. The game looks for them on its first start.", "Muted"))
	else:
		for g in games:
			if String(g[1]) == "":
				continue
			var row := W.hbox(10)
			var p := W.trim_label(Paths.short(g[1]), "Muted")
			p.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_child(p)
			var show := W.button("Show folder", "folder", "", 14)
			var full: String = g[1]
			show.disabled = not demo and not DirAccess.dir_exists_absolute(full)
			show.pressed.connect(func() -> void: show_folder.emit(full))
			row.add_child(show)
			_folders.add_child(row)
	_forget.visible = f.source == "config"


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


## running: the download runs; paused: it is paused (Resume continues it); busy: another job runs; aio: the setting is on
func set_download_state(running: bool, paused: bool, busy: bool, aio: bool) -> void:
	_go.visible = not running and not paused
	_go.disabled = busy or not aio
	_pause.visible = running or paused
	_cancel.visible = running or paused
	_pause.text = "Resume" if paused else "Pause"
	_pause.icon = VectorIcon.make("resume" if paused else "pause", 14)
	_progress.visible = running or paused
	_off.visible = not aio


func set_progress(text: String, done: int, total: int) -> void:
	_bar_label.text = text
	_bar.max_value = maxi(total, 1)
	_bar.value = done
