## Lane UI-5: the Play screen of "Parchment & Steel": the OPENBFME title, the italic subtitle, the gold divider, the status chips (update
## state, channel, version), "Your games" and, on parchment, "What's New" from the installed release's notes. The bottom bar with Play
## belongs to the window (scripts/main.gd). It only shows; main.gd fills it in.
extends Control

signal download_games
signal show_news

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Ornament := preload("res://scripts/ui/ornament.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ReleaseNotes := preload("res://scripts/core/release_notes.gd")
const Paths := preload("res://scripts/ui/paths.gd")

const NOTES_SHOWN := 6

var state_chip: PanelContainer
var channel_chip: PanelContainer
var version_chip: PanelContainer
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
	var left := W.vbox(0)
	left.position = Vector2(0, 0)
	left.custom_minimum_size.x = 480
	add_child(left)
	var h1 := W.label("OPENBFME")
	Style.text(h1, "Cinzel", 700, 58, Style.ACCENT, 3)
	left.add_child(h1)
	var sub := W.label("The Rise of the Witch-king, rebuilt")
	Style.text(sub, "CormorantGaramond-Italic", 500, 24, Style.TEXT2)
	left.add_child(sub)
	var div := Ornament.make("divider")
	div.custom_minimum_size = Vector2(196, 30)
	div.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	left.add_child(div)
	var spacer := Control.new()
	spacer.custom_minimum_size.y = 8
	left.add_child(spacer)
	var chips := W.hbox(8)
	state_chip = W.chip("")
	channel_chip = W.chip("")
	version_chip = W.chip("")
	for c in [state_chip, channel_chip, version_chip]:
		chips.add_child(c)
	left.add_child(chips)
	var gap := Control.new()
	gap.custom_minimum_size.y = 18
	left.add_child(gap)

	# Your games
	var p := W.panel(6)
	p.panel.custom_minimum_size.x = 470
	p.panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	left.add_child(p.panel)
	p.box.add_child(W.h3("Your games"))
	var status := W.label("")
	_games_status = status
	var names := W.label("RotWK 2.01 · BFME II 1.06")
	p.box.add_child(W.row(names, status, true))
	games_found = W.vbox(4)
	games_where = W.label("", "Muted")
	games_where.clip_text = true
	games_where.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	games_found.add_child(games_where)
	p.box.add_child(games_found)
	games_card = W.vbox(8)
	games_card.add_child(W.wrap("The Rise of the Witch-king 2.01 and Battle for Middle-earth II 1.06 were not found.", "Muted"))
	var grow := W.hbox(12)
	games_button = W.button("Download the games", "download", "GoldButton", 16)
	games_button.pressed.connect(func() -> void: download_games.emit())
	grow.add_child(games_button)
	var already := W.wrap("Installed already? The game finds them when it starts.", "Muted")
	already.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grow.add_child(already)
	games_card.add_child(grow)
	p.box.add_child(games_card)

	# What's New, on parchment, at the right
	var parch := W.parchment(4, Vector2(26, 24))
	_notes_card = parch.card
	_notes_card.anchor_left = 1.0
	_notes_card.anchor_right = 1.0
	_notes_card.offset_left = -360
	_notes_card.offset_right = -6
	_notes_card.offset_top = 10
	add_child(_notes_card)
	resized.connect(func() -> void: _notes_card.offset_bottom = _notes_card.offset_top + clampf(size.y * 0.7, 300.0, 470.0))
	var title := W.parch_label("What's New", "title", 24)
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
	more.text = "All the notes in News  ›"
	more.underline = LinkButton.UNDERLINE_MODE_ON_HOVER
	more.add_theme_color_override("font_color", Style.PARCH_MUTED)
	more.add_theme_color_override("font_hover_color", Style.PARCH_TITLE)
	more.add_theme_font_override("font", Style.font("CormorantGaramond-Italic", 600))
	more.add_theme_font_size_override("font_size", 16)
	more.size_flags_horizontal = Control.SIZE_SHRINK_END
	more.pressed.connect(func() -> void: show_news.emit())
	parch.box.add_child(more)
	set_notes("", "The notes of your version appear here after the first update check.")


## found: the games' state; aio: the download is offered (the setting)
func set_games(found: bool, aio: bool, where: String) -> void:
	_games_status.text = "✓ Ready" if found else "Not found"
	_games_status.add_theme_color_override("font_color", Style.GOOD if found else Style.WARN)
	games_found.visible = found
	games_where.text = where
	games_card.visible = not found and aio


## the card's subtitle (the release) and its "What's new" items from `notes` (a release body); `empty` when there are none
func set_notes(release: String, notes: String, empty := "") -> void:
	_notes_sub.text = release
	for c in _notes_box.get_children():
		c.queue_free()
	var items := ReleaseNotes.items(notes, "What's new", NOTES_SHOWN)
	if items.is_empty() and notes != "":
		items = ReleaseNotes.items(notes, "What's fixed", NOTES_SHOWN)
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
