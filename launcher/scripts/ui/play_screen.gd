## Lane UI-5: the Play screen of "Parchment & Steel": the OPENBFME title, the italic subtitle, the gold divider, the status chips (update
## state, channel, version), "Your games" and, on parchment, "What's New" from the installed release's notes. The bottom bar with Play
## belongs to the window (scripts/main.gd). It only shows; main.gd fills it in.
extends HBoxContainer

signal game_selected(game: String)
var game_picker: OptionButton
var missing_games: Label

signal download_games
signal show_news
signal open_replay
signal open_save

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Ornament := preload("res://scripts/ui/ornament.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ReleaseNotes := preload("res://scripts/core/release_notes.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const NOTES_SHOWN := 6
const NOTES_W := 360.0   # the What's New card's width

var state_chip: PanelContainer
var channel_chip: PanelContainer
var version_chip: PanelContainer
var _chips: HFlowContainer
var _left: VBoxContainer
var _games_panel: PanelContainer
var players_chip: PanelContainer   # lane ONLINECOUNT-1: "N players online" (hidden while the count is off or unknown)
var games_card: Control    # the games were not found (and the download is offered)
var games_found: Control   # the games were found: where
var games_where: Label
var games_button: Button
var _games_status: Label
var _notes_sub: Label
var _notes_box: VBoxContainer
var _notes_scroll: ScrollContainer
var _notes_fade: TextureRect
var _notes_card: PanelContainer


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	add_theme_constant_override("separation", 16)
	var left := W.vbox(0)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.custom_minimum_size.x = 480
	add_child(left)
	var h1 := W.label("OPENBFME")
	Style.text(h1, "Cinzel", 700, 58, Style.ACCENT, 3)
	left.add_child(h1)
	var sub := W.label(L10n.t("launcher.play_screen.subtitle"))
	Style.text(sub, "CormorantGaramond-Italic", 500, 24, Style.TEXT2)
	left.add_child(sub)
	var div := Ornament.make("divider")
	div.custom_minimum_size = Vector2(196, 30)
	div.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	left.add_child(div)
	var spacer := Control.new()
	spacer.custom_minimum_size.y = 8
	left.add_child(spacer)
	# the chips wrap (Sol r1: the row's minimum width grew past the 480 px column and the count chip crossed What's New)
	_left = left
	var chips := HFlowContainer.new()
	chips.add_theme_constant_override("h_separation", 8)
	chips.add_theme_constant_override("v_separation", 8)
	_chips = chips
	state_chip = W.chip("")
	channel_chip = W.chip("")
	version_chip = W.chip("")
	players_chip = W.chip("")
	players_chip.visible = false
	players_chip.tooltip_text = L10n.t("launcher.play_screen.players_tooltip")
	for c in [state_chip, channel_chip, version_chip, players_chip]:
		chips.add_child(c)
	left.add_child(chips)
	var gap := Control.new()
	gap.custom_minimum_size.y = 18
	left.add_child(gap)

	# Your games
	var p := W.panel(6)
	p.panel.custom_minimum_size.x = 470
	_games_panel = p.panel
	p.panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	left.add_child(p.panel)
	p.box.add_child(W.h3(L10n.t("launcher.play_screen.your_games")))
	# the open buttons side by side at their own width (stretched across the panel their icons stood apart from the text: the layout check)
	var open_row := W.hbox(10)
	var replay := W.button(L10n.t("launcher.play_screen.open_replay"), "folder", L10n.t("launcher.play_screen.open_replay_tip"), 15)
	replay.pressed.connect(func(): open_replay.emit())
	open_row.add_child(replay)
	var save := W.button(L10n.t("launcher.play_screen.open_save"), "folder", L10n.t("launcher.play_screen.open_save_tip"), 15)
	save.pressed.connect(func(): open_save.emit())
	open_row.add_child(save)
	p.box.add_child(open_row)
	# community PR #36: the game Play starts, in the games row where the names were (a row of its own made the panel's bottom almost meet What's
	# New's: the layout check); its items name the versions
	game_picker = OptionButton.new()
	game_picker.add_item("RotWK 2.01 · BFME II 1.06")
	game_picker.add_item("Battle for Middle-earth I 1.03")
	game_picker.item_selected.connect(func(index: int): game_selected.emit("bfme1" if index == 1 else "bfme2"))
	var status := W.label("")
	_games_status = status
	p.box.add_child(W.row(game_picker, status, true))
	games_found = W.vbox(4)
	games_where = W.trim_label("", "Muted")
	games_found.add_child(games_where)
	games_found.add_child(W.wrap(L10n.t("launcher.play_screen.checked_at_start"), "Muted"))
	p.box.add_child(games_found)
	games_card = W.vbox(8)
	missing_games = W.wrap(L10n.t("launcher.play_screen.not_found"), "Muted")
	games_card.add_child(missing_games)
	var grow := W.hbox(12)
	games_button = W.button(L10n.t("launcher.play_screen.download_games"), "download", "GoldButton", 16)
	games_button.pressed.connect(func() -> void: download_games.emit())
	grow.add_child(games_button)
	var already := W.wrap(L10n.t("launcher.play_screen.installed_already"), "Muted")
	already.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grow.add_child(already)
	games_card.add_child(grow)
	p.box.add_child(games_card)

	# What's New, on parchment, at the right
	var parch := W.parchment(4, Vector2(26, 24))
	_notes_card = parch.card
	_notes_card.custom_minimum_size.x = NOTES_W
	_notes_card.size_flags_vertical = Control.SIZE_SHRINK_BEGIN   # the card's top is the title block's top, its right the screen's margin
	add_child(_notes_card)
	resized.connect(func() -> void: _notes_card.custom_minimum_size.y = clampf(size.y * 0.7, 300.0, 470.0))
	var title := W.parch_label(L10n.t("launcher.play_screen.whats_new"), "title", 24)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	parch.box.add_child(title)
	_notes_sub = W.parch_label("", "sub", 17)
	_notes_sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	parch.box.add_child(_notes_sub)
	# Sol r2: more notes than fit never end in a cut line without a cue: a thin parchment scrollbar, a soft fade over the last lines
	# while there is more below, and the way to all the notes in News
	var holder := Control.new()
	holder.size_flags_vertical = Control.SIZE_EXPAND_FILL
	holder.clip_contents = true
	parch.box.add_child(holder)
	var clip := ScrollContainer.new()
	clip.set_anchors_preset(Control.PRESET_FULL_RECT)
	clip.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	clip.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	var bar := clip.get_v_scroll_bar()
	bar.custom_minimum_size.x = 5
	bar.add_theme_stylebox_override("scroll", Style.box(Color(0.35, 0.24, 0.1, 0.12), Color(0, 0, 0, 0), 0, 3))
	for k in ["grabber", "grabber_highlight", "grabber_pressed"]:
		bar.add_theme_stylebox_override(k, Style.box(Color("8a6a3a") if k == "grabber" else Color("6b4f25"), Color(0, 0, 0, 0), 0, 3))
	holder.add_child(clip)
	_notes_scroll = clip
	var room := MarginContainer.new()  # the text keeps clear of the scrollbar
	room.add_theme_constant_override("margin_right", 12)
	room.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	clip.add_child(room)
	_notes_box = W.vbox(5)
	_notes_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	room.add_child(_notes_box)
	_notes_fade = TextureRect.new()
	_notes_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_notes_fade.anchor_top = 1.0
	_notes_fade.anchor_right = 1.0
	_notes_fade.anchor_bottom = 1.0
	_notes_fade.offset_top = -38
	_notes_fade.offset_right = -9
	_notes_fade.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_notes_fade.stretch_mode = TextureRect.STRETCH_SCALE
	var fg := GradientTexture2D.new()
	fg.gradient = Gradient.new()
	fg.gradient.set_color(0, Color(0.92, 0.855, 0.7, 0.0))
	fg.gradient.set_color(1, Color(0.92, 0.855, 0.7, 1.0))
	fg.fill_from = Vector2(0, 0)
	fg.fill_to = Vector2(0, 1)
	fg.width = 4
	fg.height = 32
	_notes_fade.texture = fg
	holder.add_child(_notes_fade)
	bar.changed.connect(_update_fade)
	bar.value_changed.connect(func(_v: float) -> void: _update_fade())
	var more := LinkButton.new()
	more.text = L10n.t("launcher.play_screen.all_notes")
	more.underline = LinkButton.UNDERLINE_MODE_ON_HOVER
	more.add_theme_color_override("font_color", Style.PARCH_MUTED)
	more.add_theme_color_override("font_hover_color", Style.PARCH_TITLE)
	more.add_theme_font_override("font", Style.font("CormorantGaramond-Italic", 600))
	more.add_theme_font_size_override("font_size", 16)
	more.size_flags_horizontal = Control.SIZE_SHRINK_END
	more.pressed.connect(func() -> void: show_news.emit())
	parch.box.add_child(more)
	# the left column is at most as wide as the room left of the card (480 px when there is room): at a narrow layout the chips wrap sooner
	resized.connect(_fit_left)
	_fit_left()
	set_notes("", L10n.t("launcher.play_screen.notes_placeholder"))


func _fit_left() -> void:
	var room := maxf(size.x - 366.0 - 12.0, 260.0)
	_left.custom_minimum_size.x = minf(480.0, room)
	_games_panel.custom_minimum_size.x = minf(470.0, room - 10.0)
	_left.size.x = 0.0  # a container's size follows its new minimum


## the chips whose box meets What's New's card (names; empty: none do). The flow container's minimum width is its widest chip, so the column stays 480 px and the chips wrap
func chip_overlaps() -> PackedStringArray:
	var out := PackedStringArray()
	if _notes_card == null or not _notes_card.is_visible_in_tree():
		return out
	var card := _notes_card.get_global_rect()
	for c in [state_chip, channel_chip, version_chip, players_chip, _games_panel]:
		if c.is_visible_in_tree() and c.get_global_rect().intersects(card):
			out.append(c.get_child(0).text if c != _games_panel else L10n.t("launcher.play_screen.your_games"))
	return out


## the online player count's text ("" hides the chip: off, not fetched, or the lobby did not answer)
func set_players(text: String) -> void:
	W.set_chip(players_chip, "●  " + text, Style.GOOD) if text != "" else null
	players_chip.visible = text != ""


## found: the games' state; aio: the download is offered (the setting)
func set_games(found: bool, aio: bool, where: String) -> void:
	_games_status.text = L10n.t("launcher.play_screen.folders_found") if found else L10n.t("launcher.play_screen.folders_missing")
	_games_status.add_theme_color_override("font_color", Style.GOOD if found else Style.WARN)
	games_found.visible = found
	games_where.text = where
	games_where.tooltip_text = games_where.text
	games_card.visible = not found and aio


## the card's subtitle (the release) and its "What's new" / "Added" items from `notes` (a release body); `empty` when there are none
func set_notes(release: String, notes: String, empty := "") -> void:
	_notes_sub.text = release
	for c in _notes_box.get_children():
		c.queue_free()
	var items := ReleaseNotes.play_items(notes, NOTES_SHOWN)   # added (What's new / Added), then fixed (What's fixed / Bug fixes), then changed
	if items.is_empty():
		var l := W.parch_label(Paths.scrub(empty if empty != "" else notes), "body", 17)
		_notes_box.add_child(l)
		return
	for it in items:
		_notes_box.add_child(W.parch_item(Paths.scrub(it), 17))
	_update_fade.call_deferred()


## the fade shows while there are notes below the visible ones
func _update_fade() -> void:
	var bar := _notes_scroll.get_v_scroll_bar()
	_notes_fade.visible = bar.max_value - bar.page > bar.value + 1.0
