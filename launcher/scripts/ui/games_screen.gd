## Lane UI-5: Game files. Which games were found and where (shortened: the full path only behind "Show folder"), "Choose again" (the game
## asks for its folders on its next start), and the All In One download (lane AIO-1: what, from where, into which folder, its progress
## with Pause and Cancel). Lane UI-3's Settings folders and its "Download the games" dialog, moved here; scripts/main.gd does the work.
## Nothing downloads until the player presses Download: that press is the consent scripts/core/aio_install.gd logs.
## Community PR #36: when Battle for Middle-earth I is the chosen game, the download panel offers the optional BFME1 package instead
## (set_offer), labelled as coming from BFME Arena, a third party; it is never offered otherwise.
extends Control

signal forget_folders
signal show_folder(path: String)
signal verify_files(key: String, path: String)
signal download(folder: String)
signal pause_pressed
signal cancel_pressed
signal languages_changed(text: String, voice: String)  # issue #49: codes of AioInstall.offered_languages (voice "" = the text language's)

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const DL_TITLE := "launcher.games_screen.dl_title"
const DL_SOURCE := "launcher.games_screen.dl_source"
const BFME1_TITLE := "launcher.games_screen.bfme1_title"
const BFME1_SOURCE := "launcher.games_screen.bfme1_source"

var folder := ""
var offer_bfme1 := false  # the download panel offers the optional BFME1 package (set_offer)
var _found_title: Label
var _games: VBoxContainer
var _folders: VBoxContainer
var _forget: Button
var _what: Label
var _dl_title: Label
var _source: Label
var _where: Label
var _go: Button
var _off: Label
var _progress: VBoxContainer
var _bar: ProgressBar
var _bar_label: Label
var _pause: Button
var _cancel: Button
var _dialog: FileDialog
var _languages: HBoxContainer
var _text_lang: OptionButton
var _voice_lang: OptionButton
var _codes: Array = []
var _verify_buttons: Array[Button] = []
var _busy := false


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2(L10n.t("launcher.games_screen.title")))
	var s := W.scroll(Style.SCREEN_GAP)
	v.add_child(s.scroll)
	var grid := W.hbox(16)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.box.add_child(grid)

	var found := W.panel(4)
	found.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	found.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(found.panel)
	_found_title = W.h3(L10n.t("launcher.games_screen.found"))
	found.box.add_child(_found_title)
	_games = W.vbox(0)
	found.box.add_child(_games)
	_folders = W.vbox(6)
	found.box.add_child(_folders)
	_forget = W.button(L10n.t("launcher.games_screen.choose_again"), "refresh", "", 15)
	_forget.tooltip_text = L10n.t("launcher.games_screen.choose_again_tooltip")
	_forget.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_forget.pressed.connect(func() -> void: forget_folders.emit())
	found.box.add_child(_forget)

	var dl := W.panel(8)
	dl.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	dl.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(dl.panel)
	_dl_title = W.h3(L10n.t(DL_TITLE))
	dl.box.add_child(_dl_title)
	_what = W.wrap("", "Muted")
	dl.box.add_child(_what)
	_source = W.wrap(L10n.t(DL_SOURCE), "Muted")
	dl.box.add_child(_source)
	var link := LinkButton.new()
	link.text = AioInstall.PAGE.trim_prefix("https://")
	link.uri = AioInstall.PAGE
	link.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	dl.box.add_child(link)
	var row := W.hbox(10)
	var to := W.label(L10n.t("launcher.games_screen.install_to"), "Muted")
	row.add_child(to)
	_where = W.trim_label("")
	_where.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(_where)
	var change := W.button(L10n.t("launcher.games_screen.change"), "folder", "", 15)
	change.pressed.connect(open_picker)
	row.add_child(change)
	dl.box.add_child(row)
	# issue #49: the game's text language and, separately, its voices (Language: the text and speech of that version; Voices: another
	# version's speech archive)
	_languages = W.hbox(10)
	_languages.add_child(W.label(L10n.t("launcher.games_screen.language"), "Muted"))
	_text_lang = OptionButton.new()
	_text_lang.tooltip_text = L10n.t("launcher.games_screen.language_tooltip")
	_text_lang.item_selected.connect(func(_i: int) -> void: _emit_languages())
	_languages.add_child(_text_lang)
	_languages.add_child(W.label(L10n.t("launcher.games_screen.voices"), "Muted"))
	_voice_lang = OptionButton.new()
	_voice_lang.tooltip_text = L10n.t("launcher.games_screen.voices_tooltip")
	_voice_lang.item_selected.connect(func(_i: int) -> void: _emit_languages())
	_languages.add_child(_voice_lang)
	dl.box.add_child(_languages)
	_progress = W.vbox(6)
	_progress.visible = false
	_bar_label = W.trim_label("", "Muted")
	_progress.add_child(_bar_label)
	_bar = W.progress_bar()
	_progress.add_child(_bar)
	dl.box.add_child(_progress)
	var buttons := W.hbox(8)
	_go = W.button(L10n.t("launcher.games_screen.download"), "download", "", 16)
	_go.pressed.connect(func() -> void: download.emit(folder))
	buttons.add_child(_go)
	_pause = W.button(L10n.t("launcher.games_screen.pause"), "pause", "", 14)
	_pause.pressed.connect(func() -> void: pause_pressed.emit())
	buttons.add_child(_pause)
	_cancel = W.button(L10n.t("common.cancel"), "stop", "", 14)
	_cancel.pressed.connect(func() -> void: cancel_pressed.emit())
	buttons.add_child(_cancel)
	dl.box.add_child(buttons)
	_off = W.wrap(L10n.t("launcher.games_screen.offer_off"), "Muted")
	dl.box.add_child(_off)
	_dialog = FileDialog.new()
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.title = L10n.t("launcher.games_screen.dialog_title")
	_dialog.dir_selected.connect(func(d: String) -> void: set_folder(d))
	add_child(_dialog)
	set_download_state(false, false, false, true)


## the folder picker ("Change…"); `demo` (the layout test) opens it on the home folder
func open_picker(demo := false) -> void:
	_dialog.current_dir = Paths.home() if demo or not DirAccess.dir_exists_absolute(folder) else folder
	_dialog.popup_centered_ratio(0.75)


## folders: Paths.game_folders(); demo: the screenshots' made-up folders (Show folder stays enabled)
func set_folders(f: Dictionary, demo := false) -> void:
	_found_title.text = L10n.t("launcher.games_screen.found_caps") if f.found else L10n.t("launcher.games_screen.not_found_caps")
	_verify_buttons.clear()
	for c in _games.get_children() + _folders.get_children():
		c.queue_free()
	var games := [[L10n.t("launcher.games_screen.game_rotwk"), f.rotwk, "rotwk"], [L10n.t("launcher.games_screen.game_bfme2"), f.bfme2, "bfme2"]]
	if String(f.get("bfme1", "")) != "":
		games.push_front([L10n.t("launcher.games_screen.game_bfme1"), f.bfme1, "bfme1"]) # community PR #36: the optional BFME1 folder, listed when one was found
	for i in games.size():
		var ok := String(games[i][1]) != "" and (demo or DirAccess.dir_exists_absolute(games[i][1]))
		var mark := W.label("✓" if ok else "—")
		mark.add_theme_color_override("font_color", Style.GOOD if ok else Style.TEXT3)
		_games.add_child(W.row(String(games[i][0]), mark, i == games.size() - 1))
	if f.rotwk == "" and f.bfme2 == "":
		_folders.add_child(W.wrap(L10n.t("launcher.games_screen.not_chosen"), "Muted"))
	else:
		for g in games:
			if String(g[1]) == "":
				continue
			var row := W.hbox(10)
			var p := W.trim_label(Paths.short(g[1]), "Muted")
			p.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_child(p)
			var show := W.button(L10n.t("launcher.games_screen.show_folder"), "folder", "", 14)
			var full: String = g[1]
			show.disabled = not demo and not DirAccess.dir_exists_absolute(full)
			show.pressed.connect(func() -> void: show_folder.emit(full))
			row.add_child(show)
			_folders.add_child(row)
			if g[2] != "bfme1" and DirAccess.dir_exists_absolute(full):
				var verify := W.button(L10n.t("launcher.games_screen.verify_files"), "refresh", "", 14)
				verify.size_flags_horizontal = Control.SIZE_SHRINK_END
				verify.disabled = _busy
				var key: String = g[2]
				verify.pressed.connect(func() -> void: verify_files.emit(key, full))
				_verify_buttons.append(verify)
				row.add_child(verify)
	_forget.visible = f.source == "config"


## gb: the download's size; packages: AioInstall.chosen() (their folder names)
func setup(dir: String, gb: float, packages: Array, bfme1 := false) -> void:
	set_offer(bfme1, gb, packages)
	set_folder(dir)


## community PR #36: which download the panel offers (the base games, or with bfme1 the optional BFME1 package); the folder stays
func set_offer(bfme1: bool, gb: float, packages: Array) -> void:
	offer_bfme1 = bfme1
	_dl_title.text = L10n.t(BFME1_TITLE if bfme1 else DL_TITLE).to_upper()  # W.h3 shows section titles in capitals
	_source.text = L10n.t(BFME1_SOURCE if bfme1 else DL_SOURCE)
	var names := PackedStringArray()
	for p in packages:
		names.append(str(p.get("label", p.get("folder", ""))))
	_what.text = L10n.t("launcher.games_screen.what_named", {"names": " and ".join(names), "gb": gb}) if not names.is_empty() else L10n.t("launcher.games_screen.what_size", {"gb": gb})
	if _languages != null:
		_languages.visible = not bfme1  # the optional BFME1 package has its English files only


## issue #49: the languages the download offers ([{code, name}], AioInstall.offered_languages) and the chosen ones (voice "" = the text's)
func set_languages(offered: Array, text: String, voice: String) -> void:
	_codes = offered.map(func(l): return l.code)
	_text_lang.clear()
	_voice_lang.clear()
	_voice_lang.add_item(L10n.t("launcher.games_screen.voice_same"))
	for l in offered:
		_text_lang.add_item(String(l.name))
		_voice_lang.add_item(String(l.name))
	_text_lang.select(maxi(_codes.find(text), 0))
	_voice_lang.select(_codes.find(voice) + 1 if voice != "" else 0)


func _emit_languages() -> void:
	if _codes.is_empty():
		return
	var text: String = _codes[maxi(_text_lang.selected, 0)]
	var voice: String = "" if _voice_lang.selected <= 0 else _codes[_voice_lang.selected - 1]
	languages_changed.emit(text, voice)


func set_folder(dir: String) -> void:
	folder = dir
	_where.text = Paths.short(dir) if dir != "" else L10n.t("launcher.games_screen.pick_folder")


## running: the download runs; paused: it is paused (Resume continues it); busy: another job runs; aio: the setting is on
func set_download_state(running: bool, paused: bool, busy: bool, aio: bool) -> void:
	_busy = running or paused or busy
	_forget.disabled = _busy
	for verify in _verify_buttons:
		verify.disabled = _busy
	_go.visible = not running and not paused
	_go.disabled = busy or not aio
	_text_lang.disabled = running or paused or busy  # a download keeps the languages it started with
	_voice_lang.disabled = running or paused or busy
	_pause.visible = running or paused
	_cancel.visible = running or paused
	_pause.text = L10n.t("launcher.games_screen.resume") if paused else L10n.t("launcher.games_screen.pause")
	_pause.icon = VectorIcon.make("resume" if paused else "pause", 14)
	_progress.visible = running or paused
	_off.visible = not aio


func set_progress(text: String, done: int, total: int) -> void:
	_bar_label.text = text
	_bar.max_value = maxi(total, 1)
	_bar.value = done
