## Lane UI-5: Settings, three tabs as in the prototype. General: the channel, the free camera, updating by itself, offering the game
## download. Display: the game's display options are set in the game (lane OPTS-1's screen has not reached this launcher's tree yet).
## About & logs: the versions, the log folder, the installed versions and older releases (a rollback is the player's pick), the fonts'
## licence. Lane UI-3's Settings dialog as a screen (its game folders moved to Game files). It only shows and asks; scripts/main.gd does
## the work (signals).
extends Control

signal channel_picked(channel: String)
signal free_camera_toggled(on: bool)
signal auto_update_toggled(on: bool)
signal show_online_count_toggled(on: bool)
signal aio_toggled(on: bool)
signal use_version(version: String)
signal install_release(tag: String)
signal open_logs
signal replay_association_toggled(on: bool)

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Semver := preload("res://scripts/core/semver.gd")

const TABS := [["general", "General"], ["display", "Display"], ["about", "About & logs"]]
const CHANNELS := ["stable", "preview"]  # lane NIGHTLY-1: shown as Stable, Nightly (in this order)

var tab := "general"
var _tabs := {}
var _pages := {}
var _channel: OptionButton
var _free_camera: CheckButton
var _auto: CheckButton
var _aio: CheckButton
var _count: CheckButton
var _replay_association: CheckButton
var _launcher: Label
var _game: Label
var _versions: ItemList
var _use: Button
var _releases: OptionButton
var _install: Button


func _init(macos_preview := false) -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Settings"))
	var bar := W.hbox(4)
	var group := ButtonGroup.new()
	for t in TABS:
		var b := Button.new()
		b.text = t[1]
		b.toggle_mode = true
		b.button_group = group
		b.theme_type_variation = "TabButton"
		b.pressed.connect(show_tab.bind(t[0]))
		bar.add_child(b)
		_tabs[t[0]] = b
	v.add_child(bar)
	var s := W.scroll(0)
	v.add_child(s.scroll)

	# General
	var g := _page(s.box, "general")
	_channel = OptionButton.new()
	_channel.add_item(Semver.channel_name("stable"))   # lane NIGHTLY-1 (owner, 2026-10-21): "Stable", the tested numbered releases ("Alpha 0.4.5")
	_channel.add_item(Semver.channel_name("preview"))  # "Nightly": a new test build twice a day (NIGHTLY-2)
	_channel.item_selected.connect(func(i: int) -> void: channel_picked.emit(CHANNELS[i]))
	g.add_child(W.row(_with_note("Channel", "Nightly gets a new test build twice a day; Stable gets the tested releases."), _channel))
	_free_camera = CheckButton.new()
	_free_camera.tooltip_text = "Applies the next time the game starts. In a game, Ctrl+Z toggles it."
	_free_camera.toggled.connect(func(on: bool) -> void: free_camera_toggled.emit(on))
	g.add_child(W.row(_with_note("Free camera", "Zoom out to the whole map (not retail)."), _free_camera))
	_auto = CheckButton.new()
	_auto.tooltip_text = "Check for updates on startup" if macos_preview else "Update automatically in the background"
	_auto.toggled.connect(func(on: bool) -> void: auto_update_toggled.emit(on))
	g.add_child(W.row(_with_note("Check for updates on startup" if macos_preview else "Update automatically in the background",
		"Checks the official release version only. Mac preview updates are manual; nothing is installed." if macos_preview else
		"New versions download while the launcher is open or you play, and start with your next Play."), _auto))
	_count = CheckButton.new()
	_count.tooltip_text = "Show how many players are online"
	_count.toggled.connect(func(on: bool) -> void: show_online_count_toggled.emit(on))
	g.add_child(W.row(_with_note("Show how many players are online",
		"Asks the online lobby for one total (a number, no personal data) when the launcher opens and every few minutes while you look at it. " +
		"The game's menus follow this. Off: nothing is asked."), _count))
	_aio = CheckButton.new()
	_aio.tooltip_text = "Offer to download the games"
	_aio.toggled.connect(func(on: bool) -> void: aio_toggled.emit(on))
	g.add_child(W.row(_with_note("Offer to download the games", "Game files: the All In One download."), _aio, true))

	_replay_association = CheckButton.new()
	_replay_association.toggled.connect(func(on: bool): replay_association_toggled.emit(on))
	g.add_child(W.row(_with_note("Open .replay and .obfmesave files with OpenBFME", "Per-user file associations, no administrator rights. On macOS, change the default app in Finder's Get Info; off refuses Finder file opens."), _replay_association))

	# Display
	var d := _page(s.box, "display")
	d.add_child(W.row(_with_note("Display settings are in the game",
		"Window mode, resolution and the other display settings are chosen in the game's options for now. They will show here " +
		"once the launcher can set them."), null, true))

	# About & logs
	var a := _page(s.box, "about")
	_launcher = W.label("", "Muted")
	a.add_child(W.row("Launcher", _launcher))
	_game = W.label("", "Muted")
	a.add_child(W.row("Game", _game))
	var logs := W.button("Open log folder", "folder", "", 15)
	logs.pressed.connect(func() -> void: open_logs.emit())
	a.add_child(W.row(_with_note("Logs", "Attach launcher.log to a bug report."), logs))
	var vrow := W.hbox(16)
	vrow.custom_minimum_size.y = 40
	var left := W.vbox(8)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(W.label("Installed versions"))
	_versions = ItemList.new()
	_versions.custom_minimum_size = Vector2(0, 74)
	_versions.size_flags_vertical = Control.SIZE_EXPAND_FILL  # LAUNCHERUI-1: the two columns end in buttons on one line
	left.add_child(_versions)
	_use = W.button("Play this version", "play", "", 14)
	_use.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_use.pressed.connect(_on_use)
	left.add_child(_use)
	vrow.add_child(left)
	var right := W.vbox(8)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(W.label("Older versions"))
	right.add_child(W.wrap("Install another release of the channel, also an older one.", "Muted"))
	_releases = OptionButton.new()
	_releases.tooltip_text = "Choose an older release to install"
	right.add_child(_releases)
	var fill := Control.new()
	fill.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(fill)
	_install = W.button("Install", "download", "", 15)
	_install.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_install.pressed.connect(func() -> void:
		if _releases.item_count > 0:
			install_release.emit(str(_releases.get_item_metadata(_releases.selected))))
	right.add_child(_install)
	vrow.add_child(right)
	var pad := MarginContainer.new()
	pad.add_theme_constant_override("margin_top", 10)
	pad.add_theme_constant_override("margin_bottom", 10)
	pad.add_child(vrow)
	a.add_child(pad)
	a.add_child(HSeparator.new())
	a.add_child(W.row(_with_note("Fonts", "Cinzel, Cormorant Garamond and Inter, under the SIL Open Font License 1.1."), null, true))
	show_tab("general")


## a panel page of the tab `key`
func _page(parent: Control, key: String) -> VBoxContainer:
	var p := W.panel(0)
	p.panel.custom_minimum_size.x = 640
	p.panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	parent.add_child(p.panel)
	_pages[key] = p.panel
	return p.box


## a setting's name with a muted line below
static func _with_note(name: String, note: String) -> VBoxContainer:
	var v := W.vbox(1)
	var n := W.label(name)
	Style.text(n, "Inter", 500, 14, Style.TEXT)
	v.add_child(n)
	var m := W.wrap(note, "Muted")
	v.add_child(m)
	return v


func show_tab(key: String) -> void:
	tab = key
	for k in _pages:
		_pages[k].visible = k == key
		_tabs[k].set_pressed_no_signal(k == key)


func set_channel(ch: String) -> void:
	_channel.select(CHANNELS.find(ch))


func set_busy(busy: bool) -> void:
	_channel.disabled = busy
	_use.disabled = busy or _versions.item_count == 0 or _versions.get_item_metadata(0) == ""
	_releases.disabled = busy or _releases.item_count == 0
	_install.disabled = busy or _releases.item_count == 0
	_aio.disabled = busy


func set_options(free_camera: bool, auto_update: bool, aio_on: bool, launcher: String) -> void:
	_free_camera.set_pressed_no_signal(free_camera)
	_auto.set_pressed_no_signal(auto_update)
	_aio.set_pressed_no_signal(aio_on)
	_launcher.text = launcher


func set_online_count(on: bool) -> void:
	_count.set_pressed_no_signal(on)


func set_replay_association(on: bool) -> void:
	_replay_association.set_pressed_no_signal(on)


## installed: [{version}], play: the version Play starts, releases: the channel's tags (newest first)
func set_versions(installed: Array, play: String, releases: Array) -> void:
	_game.text = Semver.display(play) if play != "" else "nothing installed yet"
	_versions.clear()
	for i in installed:
		var label: String = Semver.display(i.version)
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
		_releases.add_item(Semver.display(t))   # lane VERSION-1: "Alpha 0.4.5"; the tag is the item's metadata
		_releases.set_item_metadata(_releases.item_count - 1, t)


func _on_use() -> void:
	var sel := _versions.get_selected_items()
	if sel.is_empty():
		return
	var v: String = _versions.get_item_metadata(sel[0])
	if v != "":
		use_version.emit(v)
