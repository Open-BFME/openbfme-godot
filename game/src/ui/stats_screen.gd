extends Panel
## Retail skirmish STATS screen (REF-12) as an honest stub: the faction crest
## row and the streak table render in the retail look, but no values are
## tracked yet — every figure is a recorded "—", never an invented statistic.
## Crests are styled tiles; no converted crest art exists in the packs today.

signal back_pressed

const FACTIONS: Array[Dictionary] = [
	{"id": "men", "name": "Men"},
	{"id": "elves", "name": "Elves"},
	{"id": "dwarves", "name": "Dwarves"},
	{"id": "isengard", "name": "Isengard"},
	{"id": "mordor", "name": "Mordor"},
	{"id": "wild", "name": "Goblins"},
]
const STREAK_ROWS: Array[String] = [
	"Current win streak",
	"Current loss streak",
	"Longest win streak",
	"Worst loss streak",
	"Career wins",
]

var crest_row: HBoxContainer
var streak_grid: GridContainer
var back_btn: Button


func _ready() -> void:
	_build()


func _build() -> void:
	var title := _label(self, "Title", "skirmish", Vector2(0, 8), Vector2(size.x, 52), 40, Color("9ec97e"))
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	var subtitle := _label(self, "Subtitle", "STATS", Vector2(0, 58), Vector2(size.x, 26), 17, Color("d8e6da"))
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER

	_label(self, "SelectFaction", "Select Faction", Vector2(0, 96), Vector2(size.x, 24), 15, Color("b7dc94")).horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	crest_row = HBoxContainer.new()
	crest_row.name = "CrestRow"
	crest_row.position = Vector2(30, 126)
	crest_row.size = Vector2(size.x - 60, 150)
	crest_row.alignment = BoxContainer.ALIGNMENT_CENTER
	crest_row.add_theme_constant_override("separation", 16)
	add_child(crest_row)
	for faction in FACTIONS:
		crest_row.add_child(_crest_tile(String(faction["name"])))

	var table := Panel.new()
	table.name = "StreakPanel"
	table.theme_type_variation = "OverlayPanel"
	table.position = Vector2(120, 300)
	table.size = Vector2(size.x - 240, 330)
	add_child(table)
	streak_grid = GridContainer.new()
	streak_grid.name = "StreakGrid"
	streak_grid.columns = 3
	streak_grid.position = Vector2(16, 12)
	streak_grid.size = Vector2(table.size.x - 32, 300)
	streak_grid.add_theme_constant_override("h_separation", 24)
	streak_grid.add_theme_constant_override("v_separation", 10)
	table.add_child(streak_grid)
	streak_grid.add_child(_cell("", 15, Color("b7dc94")))
	streak_grid.add_child(_cell("Faction", 15, Color("b7dc94"))
	)
	streak_grid.add_child(_cell("Overall", 15, Color("b7dc94")))
	for row_text in STREAK_ROWS:
		streak_grid.add_child(_cell(row_text, 16, Color("d8e6da")))
		streak_grid.add_child(_cell("—", 16, Color("a9c08a")))
		streak_grid.add_child(_cell("—", 16, Color("a9c08a")))

	_label(self, "TrackedNote", "Match statistics are not tracked yet.", Vector2(0, 644), Vector2(size.x, 24), 14, Color("a9c08a")).horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	back_btn = Button.new()
	back_btn.name = "StatsBack"
	back_btn.text = "BACK"
	back_btn.custom_minimum_size = Vector2(240, 52)
	back_btn.position = Vector2(30, size.y - 66)
	back_btn.size = Vector2(240, 52)
	back_btn.pressed.connect(func() -> void: back_pressed.emit())
	add_child(back_btn)


func _crest_tile(faction_name: String) -> Panel:
	var tile := Panel.new()
	tile.name = "Crest%s" % faction_name.replace(" ", "")
	tile.theme_type_variation = "FlyoutPanel"
	tile.custom_minimum_size = Vector2(270, 140)
	var name_label := _label(tile, "Name", faction_name, Vector2(0, 12), Vector2(270, 28), 19, Color("cfe0b0"))
	name_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	var rank := _label(tile, "Rank", "—", Vector2(0, 44), Vector2(270, 24), 14, Color("a9c08a"))
	rank.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	var diamond := _label(tile, "Crest", "◆", Vector2(0, 72), Vector2(270, 52), 40, Color("3f6b46"))
	diamond.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	return tile


func _cell(text: String, font_size: int, color: Color) -> Label:
	var cell := Label.new()
	cell.text = text
	cell.add_theme_font_size_override("font_size", font_size)
	cell.add_theme_color_override("font_color", color)
	cell.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return cell


func _label(parent: Control, node_name: String, text: String, position: Vector2, label_size: Vector2, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = position
	label.size = label_size
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	parent.add_child(label)
	return label
