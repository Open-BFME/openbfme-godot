## Lane UI-5: Settings, three tabs as in the prototype. General: the channel, the free camera, updating by itself, offering the game
## download. Display: shared window mode and resolution for both games.
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
signal language_picked(code: String)  # lane L10N-1: "" = Automatic
signal display_changed(key: String, value: String)
signal display_refresh_requested

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Semver := preload("res://scripts/core/semver.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")
const GameDisplay := preload("res://scripts/core/game_display.gd")

const TABS := [["general", "launcher.settings.tab_general"], ["display", "launcher.settings.tab_display"], ["about", "launcher.settings.tab_about"]]
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
var _language: OptionButton
var _language_note: Label
var _language_codes: Array = []
var _launcher: Label
var _game: Label
var _versions: ItemList
var _use: Button
var _releases: OptionButton
var _install: Button
var _window_mode: OptionButton
var _resolution: OptionButton
var _display_note: Label
var _display_error: Label


func _init(macos_preview := false) -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2(L10n.t("launcher.settings.title")))
	var bar := W.hbox(4)
	var group := ButtonGroup.new()
	for t in TABS:
		var b := Button.new()
		b.text = L10n.t(t[1])
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
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.channel"), L10n.t("launcher.settings.channel_note")), _channel))
	_free_camera = CheckButton.new()
	_free_camera.tooltip_text = L10n.t("launcher.settings.free_camera_tooltip")
	_free_camera.toggled.connect(func(on: bool) -> void: free_camera_toggled.emit(on))
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.free_camera"), L10n.t("launcher.settings.free_camera_note")), _free_camera))
	_auto = CheckButton.new()
	_auto.tooltip_text = L10n.t("launcher.settings.check_on_startup" if macos_preview else "launcher.settings.auto_update")
	_auto.toggled.connect(func(on: bool) -> void: auto_update_toggled.emit(on))
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.check_on_startup" if macos_preview else "launcher.settings.auto_update"),
		L10n.t("launcher.settings.check_on_startup_note" if macos_preview else "launcher.settings.auto_update_note")), _auto))
	_count = CheckButton.new()
	_count.tooltip_text = L10n.t("launcher.settings.online_count")
	_count.toggled.connect(func(on: bool) -> void: show_online_count_toggled.emit(on))
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.online_count"), L10n.t("launcher.settings.online_count_note")), _count))
	_aio = CheckButton.new()
	_aio.tooltip_text = L10n.t("launcher.settings.aio")
	_aio.toggled.connect(func(on: bool) -> void: aio_toggled.emit(on))
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.aio"), L10n.t("launcher.settings.aio_note")), _aio, true))

	_replay_association = CheckButton.new()
	_replay_association.tooltip_text = L10n.t("launcher.settings.replay_association")  # first_run_test: a toggle without text needs one
	_replay_association.toggled.connect(func(on: bool): replay_association_toggled.emit(on))
	g.add_child(W.row(_with_note(L10n.t("launcher.settings.replay_association"), L10n.t("launcher.settings.replay_association_note")), _replay_association))
	# lane L10N-1: the language of the launcher's own text (Automatic: the game's, where OpenBFME has a translation)
	_language = OptionButton.new()
	_language.tooltip_text = L10n.t("launcher.settings.language_note")
	_language.item_selected.connect(func(i: int) -> void: language_picked.emit(_language_codes[i]))
	var lang_name := _with_note(L10n.t("launcher.settings.language"), "")
	_language_note = lang_name.get_child(1)
	g.add_child(W.row(lang_name, _language, true))

	# Display
	var d := _page(s.box, "display")
	_window_mode = OptionButton.new()
	_window_mode.tooltip_text = L10n.t("launcher.settings.window_mode")
	for label in [L10n.t("launcher.settings.mode_windowed"), L10n.t("launcher.settings.mode_borderless"), L10n.t("launcher.settings.mode_fullscreen")]:
		_window_mode.add_item(label)
	_window_mode.item_selected.connect(func(i: int): display_changed.emit("window_mode", GameDisplay.MODES[i]))
	d.add_child(W.row(_with_note(L10n.t("launcher.settings.window_mode"), L10n.t("launcher.settings.display_shared")), _window_mode))
	_resolution = OptionButton.new()
	_resolution.tooltip_text = L10n.t("launcher.settings.resolution")
	_resolution.get_popup().max_size.y = 400
	_resolution.item_selected.connect(func(i: int):
		if not _resolution.is_item_separator(i):
			display_changed.emit("resolution", str(_resolution.get_item_metadata(i))))
	var resolution_label := _with_note(L10n.t("launcher.settings.resolution"), "")
	_display_note = resolution_label.get_child(1)
	d.add_child(W.row(resolution_label, _resolution))
	d.add_child(W.row(_with_note(L10n.t("launcher.settings.display_in_game"), L10n.t("launcher.settings.display_in_game_note")), null, true))
	_display_error = W.wrap("", "Muted")
	_display_error.add_theme_color_override("font_color", Style.BAD)
	_display_error.visible = false
	d.add_child(_display_error)

	# About & logs
	var a := _page(s.box, "about")
	_launcher = W.label("", "Muted")
	a.add_child(W.row(L10n.t("launcher.settings.launcher_version"), _launcher))
	_game = W.label("", "Muted")
	a.add_child(W.row(L10n.t("launcher.settings.game_version"), _game))
	var logs := W.button(L10n.t("launcher.settings.open_logs"), "folder", "", 15)
	logs.pressed.connect(func() -> void: open_logs.emit())
	a.add_child(W.row(_with_note(L10n.t("launcher.settings.logs"), L10n.t("launcher.settings.logs_note")), logs))
	var vrow := W.hbox(16)
	vrow.custom_minimum_size.y = 40
	var left := W.vbox(8)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(W.label(L10n.t("launcher.settings.installed_versions")))
	_versions = ItemList.new()
	_versions.custom_minimum_size = Vector2(0, 74)
	_versions.size_flags_vertical = Control.SIZE_EXPAND_FILL  # LAUNCHERUI-1: the two columns end in buttons on one line
	left.add_child(_versions)
	_use = W.button(L10n.t("launcher.settings.play_this_version"), "play", "", 14)
	_use.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_use.pressed.connect(_on_use)
	left.add_child(_use)
	vrow.add_child(left)
	var right := W.vbox(8)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(W.label(L10n.t("launcher.settings.older_versions")))
	right.add_child(W.wrap(L10n.t("launcher.settings.older_versions_note"), "Muted"))
	_releases = OptionButton.new()
	_releases.tooltip_text = L10n.t("launcher.settings.older_versions_tooltip")
	right.add_child(_releases)
	var fill := Control.new()
	fill.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(fill)
	_install = W.button(L10n.t("launcher.settings.install"), "download", "", 15)
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
	a.add_child(W.row(_with_note(L10n.t("launcher.settings.fonts"), L10n.t("launcher.settings.fonts_note")), null, true))
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
	if key == "display":
		display_refresh_requested.emit()


func set_display(display: Dictionary) -> void:
	_window_mode.select(GameDisplay.MODES.find(display.window_mode))
	_resolution.clear()
	_resolution.add_item(L10n.t("launcher.settings.resolution_auto"))
	_resolution.set_item_metadata(0, "")
	_resolution.select(0)
	for group in GameDisplay.grouped_choices(display.screen_size, display.resolution):
		_resolution.add_separator(group.aspect)
		for size in group.sizes:
			_resolution.add_item("%d × %d" % [size.x, size.y])
			_resolution.set_item_metadata(_resolution.item_count - 1, "%dx%d" % [size.x, size.y])
			if size == display.resolution:
				_resolution.select(_resolution.item_count - 1)
	_display_note.text = L10n.t("launcher.settings.resolution_note")
	if display.screen_size.x >= 320 and display.screen_size.y >= 240:
		_display_note.text += " " + L10n.t("launcher.settings.desktop_resolution", {"width": display.screen_size.x, "height": display.screen_size.y})
	_display_error.text = display.error
	_display_error.visible = display.error != ""
	_window_mode.disabled = display.error != ""
	_resolution.disabled = display.error != ""


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
	_game.text = Semver.display(play) if play != "" else L10n.t("launcher.settings.nothing_installed_short")
	_versions.clear()
	for i in installed:
		var label: String = Semver.display(i.version)
		if i.version == play:
			label += "    " + L10n.t("launcher.settings.play_starts_this")
		_versions.add_item(label)
		_versions.set_item_metadata(_versions.item_count - 1, i.version)
	if installed.is_empty():
		_versions.add_item(L10n.t("launcher.settings.nothing_installed"))
		_versions.set_item_disabled(0, true)
		_versions.set_item_metadata(0, "")
	_releases.clear()
	for t in releases:
		_releases.add_item(Semver.display(t))   # lane VERSION-1: "Alpha 0.4.5"; the tag is the item's metadata
		_releases.set_item_metadata(_releases.item_count - 1, t)


## lane L10N-1: the Language choices, [{code, label}] ("" = Automatic, first), the one in use, and what Automatic found
func set_languages(choices: Array, selected: String, automatic_note: String) -> void:
	_language_note.text = automatic_note + " " + L10n.t("launcher.settings.language_restart")
	_language.clear()
	_language_codes.clear()
	for c in choices:
		_language.add_item(c.label)
		_language_codes.append(c.code)
	_language.select(maxi(0, _language_codes.find(selected)))


func _on_use() -> void:
	var sel := _versions.get_selected_items()
	if sel.is_empty():
		return
	var v: String = _versions.get_item_metadata(sel[0])
	if v != "":
		use_version.emit(v)
