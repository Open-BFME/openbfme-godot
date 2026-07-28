extends Panel

## THE WAR OF THE RING STRATEGIC SCREEN: the map, the armies, and the one button
## that starts a battle.
##
## It draws RETAIL'S OWN 3D MAP when one has been converted. `livingmap.w3d` -
## the whole of Middle-earth as 64 sub-objects with retail's compiled textures -
## decodes through this project's W3D scanner with no unsupported chunks, and
## `openbfme_importer.livingmap_bundle` turns it into the bundle
## `wotr_map_bundle.gd` loads. Regions sit on that mesh at their AUTHORED world
## coordinates, because the document's `centerPoint` values and the map's
## vertices are the same coordinate space at scale 1 - measured, and the
## measurement travels in the bundle manifest.
##
## With no bundle converted the screen falls back to the flat 2D region graph it
## has always drawn, and SAYS which one the owner is looking at - in the log, in
## the mode line, and in a banner across the top of the fallback itself carrying
## every path that was searched and the command that produces a bundle. It used
## to fall back in complete silence: no print, no warning, nothing in the log, so
## the only way to find out was to notice the map looked wrong. That is the class
## of defect this branch exists to remove, and it had shipped here.
##
## WHAT ELSE IS ON SCREEN, all of it derived from the strategic state: whose turn
## it is and whether that seat is human, what a click will do right now, a seat
## table (regions held, armies, heroes, command points on the board), and a key
## for every colour and ring the map draws.
##
## Still missing from retail's presentation, and named on screen rather than
## faked: the `LivingWorldUI.apt` shell (799 KB of Flash this project does not
## read), the `LW:DisplayName*` string table (so regions carry retail's own ids),
## army models walking between regions, the turn-phase banner, and retail's
## ambient animation (the Eye of Sauron, circling eagles and fellbeasts, drifting
## cloud borders).
##
## THREE RULES IT KEEPS:
##
## 1. NOTHING ON SCREEN IS INVENTED. A region retail did not give a custom centre
##    point (BFME2 authors exactly one: Rhun) is NOT placed at a plausible
##    coordinate - it is listed separately and labelled. The battlefield a battle
##    is fought on is labelled a stand-in, because it is one. A map sub-object
##    whose texture did not resolve is drawn flat grey, never with a substitute.
##
## 2. SELECTION IS PRESENTATION UNTIL IT IS COMMITTED. `selected_region`,
##    `selected_target` and the hover highlight live on the session's
##    presentation fields, never enter a hash, and never reach the simulation.
##    The moment they decide a battle they go through `session.commit_attack()`,
##    which mints the commitment the strategic hash covers - the ONLY door.
##
## 3. A REFUSAL IS SHOWN, NOT SWALLOWED. Every path that cannot proceed prints
##    the strategic layer's own reason into the message line.

signal back_requested
## Emitted with the session's commit result once a battle has been ADMITTED into
## the strategic state. The menu owns the scene change; this screen owns the
## commitment.
signal battle_committed(configured: Dictionary)
signal turn_ended

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const StringsScript = preload("res://src/wotr/wotr_strings.gd")
const MacrosScript = preload("res://src/wotr/wotr_macros.gd")
const LivingWorldUiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const MarkerModelsScript = preload("res://src/wotr/wotr_marker_models.gd")
const RegionImagesScript = preload("res://src/wotr/wotr_region_images.gd")
const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")

## Retail's own region-bonus wording, keyed by the living-world document's own
## bonus field. The VALUE is a string-table key, so what the player reads is
## retail's text with retail's placeholders filled from retail's numbers - this
## table is only the mapping between the two, and every entry is a field the
## document actually carries.
##
## `%d%%` in retail's text is a literal percent sign after the number; `%d` alone
## is a plain count. `_format_bonus()` handles both, and a formatter this project
## cannot fill is shown raw rather than mangled.
const BONUS_STRING_KEYS := {
	"army": "LW:RegionBonusArmy",
	"attack": "LW:RegionAttackBonus",
	"buildingDiscount": "LW:RegionBuildingDiscountBonus",
	"defense": "LW:RegionDefenseBonus",
	"discountedBarracksUnits": "LW:RegionBarracksUnitDiscountBonus",
	"discountedHeroUnits": "LW:RegionHeroDiscountBonus",
	"discountedSiegeUnits": "LW:RegionSeigeDiscountBonus",
	"experience": "LW:RegionExperienceBonus",
	"extraStartResources": "LW:RegionExtraResourcesBonus",
	"fertileTerritory": "LW:RegionTreasuryBonus",
	"freeBuilder": "LW:RegionFreeBuildersBonus",
	"freeInnUnits": "LW:RegionFreeInnUnitsBonus",
	"legendary": "LW:RegionLegendaryBonus",
	"resource": "LW:RegionBonusResource",
}
## The order retail's own panel lists bonuses in is not recorded in any file this
## lane read, so they are listed in a FIXED ALPHABETICAL order by field name.
## That is a stated presentation choice, not a claim about retail.
const BONUS_ORDER := [
	"fertileTerritory", "army", "legendary", "resource", "attack", "defense",
	"experience", "buildingDiscount", "discountedBarracksUnits",
	"discountedHeroUnits", "discountedSiegeUnits", "extraStartResources",
	"freeBuilder", "freeInnUnits",
]
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")
const RulesScript = preload("res://src/wotr/wotr_autoresolve_rules.gd")
const AutoResolveBattleScript = preload("res://src/wotr/wotr_autoresolve_battle.gd")

## THE TWO COLOURS THE BATTLE REPORT USES, and the only distinction it draws.
## Retail's numbers in one, this project's in the other, side by side on the
## same line so a player never has to remember which is which. The same
## discipline this branch already applies to the marker magnification, the
## hand-built chrome and the `structures 0 / 3 plots` line.
const RETAIL_COLOR := "#9fd18a"
const PROJECT_COLOR := "#e8b45c"

## Seat colours. Fixed by seat index and never player-chosen: a colour is
## presentation, and a presentation value that varied per session would be one
## more thing to keep out of the hash.
const SEAT_COLORS: Array[Color] = [
	Color("#4d7fd6"), Color("#c8483f"), Color("#5aa552"), Color("#d0b03c"),
	Color("#a763c9"), Color("#3fb0ad"),
]
const NEUTRAL_COLOR := Color("#5a6656")

const MAP_INSET := 44.0
const MARKER_RADIUS := 11.0
## The height of the "this is not retail's map" banner over the flat fallback.
const FALLBACK_BANNER_HEIGHT := 158.0
## Lines of the refusal the banner has room for. The full text always reaches the
## launch log; the banner shows the headline, the present-but-broken bundles and
## the command that fixes it, which are the first lines by construction.
const FALLBACK_BANNER_LINES := 8

var session: SessionScript = null
## Why War of the Ring is unavailable, or "" when it is. Non-empty means the map
## area shows the reason and nothing else.
var unavailable_reason := ""
## Pack map ids the tactical layer can actually boot, supplied by the menu.
var available_map_ids: Array = []

## The brass shell, painted under every other control. Presentation only.
var chrome_layer: Control
var heading_label: Label
var status_label: Label
## The flat 2D region graph. Kept as the honest fallback for when no living-map
## bundle has been converted, and labelled as a fallback when it is showing.
var map_view: Control
## Retail's 3D map, when a bundle is available.
var map3d: Control
var map_mode_label: Label
var map_bundle: BundleScript = null
## Retail's per-region territory geometry, when converted.
var region_geometry: RegionGeometryScript = null
## Why there is no territory shading, or "" when there is.
var region_geometry_reason := ""
## Retail's string table, when converted. Null means regions carry retail ids.
var strings: StringsScript = null
var strings_reason := ""
## Retail's gamedata `#define` table, so a macro bonus shows retail's number.
var macros: MacrosScript = null
var macros_reason := ""
## Retail's UI surface: the buildable structures, the recruitable armies and the
## atlas crop behind every icon. Null means banners carry no portrait and there
## is no build menu, and the screen says so.
var ui: LivingWorldUiScript = null
var ui_reason := ""
## Retail's own 3D marker models - the army banners, the marching columns and the
## build-plot foundation decals - or null with the reason.
var markers: MarkerModelsScript = null
var markers_reason := ""
## Retail's own portraits of the regions themselves, or null with the reason.
var region_images: RegionImagesScript = null
var region_images_reason := ""
## The build plot the radial menu is open on, as `{region, index}` or `{}`.
## PRESENTATION ONLY - it lives here, not on the session, and reaches nothing.
var selected_plot: Dictionary = {}
## Why there is no 3D map, or "" when there is one.
var map_reason := ""
## Whose turn it is, in one line, at the size a player reads first.
var turn_banner: Label
## RETAIL'S HEADER NUMBERS: the seat's purse and its command points. Retail puts
## these across the top of its strategic shell; this is the same two facts read
## out of the same data. There is NO PHASE BAR beside them and there will not be
## one - see `_refresh_header()`.
var header_label: Label
## What a click will do RIGHT NOW, given the current selection.
var hint_label: Label
## The marker key: what each fill and each ring on the map means.
var legend_label: Control
## Every seat's standing: regions held, armies, command points.
var standings_label: RichTextLabel
var detail_label: RichTextLabel
## RETAIL'S OWN PORTRAIT OF THE REGION under the pointer, and the line that says
## which authored field it came from - or which one retail names and does not
## define.
var region_portrait_frame: Control
var region_portrait_caption: Label
var message_label: Label
var unplaced_label: Label
var unplaced_host: VBoxContainer
var attack_button: Button
var auto_resolve_button: Button
var end_turn_button: Button
var back_button: Button

## THE BATTLE RESULT SCREEN. Hidden until a battle is auto-resolved, then it
## covers the map with the WORKING - what each side rolled, what modified it,
## and why the outcome fell the way it did. It is a report rather than an
## animation, and it says so, because retail's animated auto-resolve
## presentation is not converted and drawing one would be inventing it.
var report_backdrop: ColorRect
var report_text: RichTextLabel
var report_close: Button
## The last auto-resolve, kept so the report can be redrawn and so a test can
## read what the screen was showing. Presentation only: never hashed, never
## handed to anything, cleared when the report closes.
var last_auto_resolve: Dictionary = {}

var _rows: Array[Dictionary] = []
var _row_by_id: Dictionary = {}
var _targets: PackedStringArray = PackedStringArray()
var _moves: PackedStringArray = PackedStringArray()
var _staging: PackedStringArray = PackedStringArray()
var _screen_positions: Dictionary = {}


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	if heading_label == null:
		build()
	report_map_availability()


## Say at STARTUP whether retail's 3D map will be there, without paying for the
## 2.5 MB mesh and 48 textures a full load costs. The exported build that shipped
## the flat fallback wrote a 234-byte log with one content line in it; a launch
## log that does not mention the map at all is how a map goes missing quietly.
## This runs before any pack root is known, so it reports the environment and
## user-data candidates - the two the owner controls directly.
func report_map_availability() -> void:
	var probed: Dictionary = BundleScript.probe([])
	if bool(probed.get("found", false)):
		print("[WotrMap] startup: a living-map bundle is present at %s [%s]; it is read when War of the Ring opens." % [
			String(probed.get("root", "")), String(probed.get("origin", ""))])
		return
	var places: Array[String] = []
	for row in probed.get("rows", []) as Array:
		places.append("%s [%s]" % [String((row as Dictionary)["root"]), String((row as Dictionary)["origin"])])
	print("[WotrMap] startup: NO living-map bundle in any candidate location, so War of the Ring will draw its flat 2D region graph. Looked at: %s. Set %s to a bundle directory produced by `python -m openbfme_importer.livingmap_bundle`." % [
		"; ".join(places), BundleScript.BUNDLE_ENV])


func build() -> void:
	# THE FRAME, UNDER EVERYTHING. Retail's strategic screen is a warm brass shell
	# with inset panels, not a set of labels on a black field. This Control paints
	# that shell and ignores the mouse, so it changes how the screen looks and
	# nothing about how it behaves.
	chrome_layer = Control.new()
	chrome_layer.name = "Chrome"
	chrome_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	chrome_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	chrome_layer.draw.connect(_draw_chrome)
	add_child(chrome_layer)

	heading_label = Label.new()
	heading_label.name = "Heading"
	heading_label.text = "WAR OF THE RING"
	heading_label.position = Vector2(30, 16)
	heading_label.add_theme_font_size_override("font_size", 26)
	heading_label.add_theme_color_override("font_color", ThemeScript.GOLD_BRIGHT)
	add_child(heading_label)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.position = Vector2(330, 26)
	status_label.custom_minimum_size = Vector2(1500, 20)
	status_label.size = Vector2(1500, 20)
	status_label.add_theme_font_size_override("font_size", 14)
	status_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(status_label)

	# WHOSE TURN IT IS AND WHAT A CLICK WILL DO. Retail puts this in its
	# `LivingWorldUI.apt` shell, which is not converted; this is the same two
	# facts drawn from the strategic state rather than a reproduction of retail's
	# frame.
	turn_banner = Label.new()
	turn_banner.name = "TurnBanner"
	turn_banner.position = Vector2(30, 70)
	turn_banner.custom_minimum_size = Vector2(790, 22)
	turn_banner.size = Vector2(790, 22)
	turn_banner.add_theme_font_size_override("font_size", 19)
	turn_banner.add_theme_color_override("font_color", ThemeScript.GOLD_BRIGHT)
	add_child(turn_banner)

	header_label = Label.new()
	header_label.name = "Header"
	header_label.position = Vector2(824, 70)
	header_label.custom_minimum_size = Vector2(440, 22)
	header_label.size = Vector2(440, 22)
	header_label.add_theme_font_size_override("font_size", 17)
	header_label.add_theme_color_override("font_color", ThemeScript.GOLD)
	add_child(header_label)

	hint_label = Label.new()
	hint_label.name = "Hint"
	hint_label.position = Vector2(30, 94)
	hint_label.custom_minimum_size = Vector2(1230, 20)
	hint_label.add_theme_font_size_override("font_size", 15)
	hint_label.add_theme_color_override("font_color", ThemeScript.TEXT_LEAF)
	add_child(hint_label)

	map_view = Control.new()
	map_view.name = "MapView"
	map_view.position = Vector2(24, 118)
	map_view.custom_minimum_size = Vector2(1240, 548)
	map_view.size = Vector2(1240, 548)
	map_view.mouse_filter = Control.MOUSE_FILTER_STOP
	map_view.draw.connect(_draw_map)
	map_view.gui_input.connect(_on_map_input)
	add_child(map_view)

	map3d = MapViewScript.new()
	map3d.name = "Map3D"
	map3d.position = map_view.position
	map3d.custom_minimum_size = map_view.custom_minimum_size
	map3d.size = map_view.size
	map3d.visible = false
	map3d.region_clicked.connect(_on_region_clicked)
	map3d.region_hovered.connect(_on_region_hovered)
	map3d.plot_clicked.connect(_on_plot_clicked)
	# The mode line's banner and label counts only exist after the paint, so they
	# are re-read here rather than during `refresh()`. Updating a Label does not
	# ask the map to redraw, so this cannot loop.
	map3d.overlay_painted.connect(_refresh_map_mode_label)
	add_child(map3d)

	# WHAT THE RINGS AND COLOURS MEAN. The map draws five different rings and one
	# fill colour per seat; without this the player has to guess, and guessing at
	# a strategic map is how a turn gets wasted.
	legend_label = Control.new()
	legend_label.name = "Legend"
	legend_label.draw.connect(_draw_legend)
	legend_label.position = Vector2(24, 672)
	legend_label.custom_minimum_size = Vector2(1240, 24)
	legend_label.size = Vector2(1240, 24)
	legend_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(legend_label)

	message_label = Label.new()
	message_label.name = "Message"
	message_label.position = Vector2(24, 700)
	message_label.custom_minimum_size = Vector2(1240, 24)
	message_label.add_theme_font_size_override("font_size", 15)
	message_label.add_theme_color_override("font_color", ThemeScript.GOLD)
	add_child(message_label)

	map_mode_label = Label.new()
	map_mode_label.name = "MapMode"
	# Below the message line, so it never overlaps the map area.
	map_mode_label.position = Vector2(24, 724)
	map_mode_label.custom_minimum_size = Vector2(1240, 56)
	map_mode_label.size = Vector2(1240, 56)
	map_mode_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	map_mode_label.add_theme_font_size_override("font_size", 12)
	map_mode_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(map_mode_label)

	var side_x := 1290.0
	standings_label = RichTextLabel.new()
	standings_label.name = "Standings"
	standings_label.bbcode_enabled = true
	standings_label.fit_content = false
	standings_label.scroll_active = true
	standings_label.position = Vector2(side_x, 70)
	standings_label.custom_minimum_size = Vector2(524, 196)
	standings_label.size = Vector2(524, 196)
	standings_label.add_theme_font_size_override("normal_font_size", 15)
	add_child(standings_label)

	detail_label = RichTextLabel.new()
	detail_label.name = "Detail"
	detail_label.bbcode_enabled = true
	detail_label.fit_content = false
	detail_label.scroll_active = true
	detail_label.position = Vector2(side_x, 280)
	detail_label.custom_minimum_size = Vector2(524, 252)
	detail_label.size = Vector2(524, 252)
	detail_label.add_theme_font_size_override("normal_font_size", 16)
	add_child(detail_label)

	# The unplaced block sits BETWEEN the detail panel and the buttons, and its
	# heading wraps to two lines. It gets exactly the band 544..628; the buttons
	# start at 636 and nothing may reach them.
	unplaced_label = Label.new()
	unplaced_label.name = "UnplacedHeading"
	unplaced_label.position = Vector2(side_x, 544)
	unplaced_label.custom_minimum_size = Vector2(548, 40)
	unplaced_label.size = Vector2(548, 40)
	unplaced_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	unplaced_label.add_theme_font_size_override("font_size", 13)
	unplaced_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(unplaced_label)

	unplaced_host = VBoxContainer.new()
	unplaced_host.name = "UnplacedRegions"
	unplaced_host.position = Vector2(side_x, 588)
	unplaced_host.custom_minimum_size = Vector2(548, 0)
	unplaced_host.size = Vector2(548, 0)
	add_child(unplaced_host)

	attack_button = Button.new()
	attack_button.name = "Attack"
	attack_button.text = "ATTACK"
	attack_button.position = Vector2(side_x, 636)
	attack_button.custom_minimum_size = Vector2(250, 44)
	attack_button.size = Vector2(250, 44)
	attack_button.disabled = true
	attack_button.pressed.connect(_on_attack_pressed)
	add_child(attack_button)

	end_turn_button = Button.new()
	end_turn_button.name = "EndTurn"
	end_turn_button.text = "END TURN"
	end_turn_button.position = Vector2(side_x + 262, 636)
	end_turn_button.custom_minimum_size = Vector2(250, 44)
	end_turn_button.size = Vector2(250, 44)
	end_turn_button.pressed.connect(_on_end_turn_pressed)
	add_child(end_turn_button)

	# AUTO-RESOLVE. Retail's own RULES tab offers "Auto Resolve and RTS", which
	# means BOTH are offered and the player picks per battle; this is that pick.
	# It goes through the SAME door ATTACK does - `session.commit_attack()` -
	# because the choice has to be recorded in the commitment the strategic hash
	# covers, not applied afterwards to a battle that was committed as something
	# else.
	auto_resolve_button = Button.new()
	auto_resolve_button.name = "AutoResolve"
	auto_resolve_button.text = "AUTO-RESOLVE"
	auto_resolve_button.position = Vector2(side_x + 262, 690)
	auto_resolve_button.custom_minimum_size = Vector2(250, 40)
	auto_resolve_button.size = Vector2(250, 40)
	auto_resolve_button.disabled = true
	auto_resolve_button.pressed.connect(_on_auto_resolve_pressed)
	add_child(auto_resolve_button)

	report_backdrop = ColorRect.new()
	report_backdrop.name = "BattleReportBackdrop"
	report_backdrop.color = Color(0.04, 0.05, 0.06, 0.96)
	report_backdrop.visible = false
	report_backdrop.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(report_backdrop)

	report_text = RichTextLabel.new()
	report_text.name = "BattleReport"
	report_text.bbcode_enabled = true
	report_text.scroll_active = true
	report_text.fit_content = false
	report_text.visible = false
	add_child(report_text)

	report_close = Button.new()
	report_close.name = "BattleReportClose"
	report_close.text = "CLOSE"
	report_close.visible = false
	report_close.pressed.connect(close_battle_report)
	add_child(report_close)

	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.position = Vector2(side_x, 690)
	back_button.custom_minimum_size = Vector2(250, 40)
	back_button.size = Vector2(250, 40)
	back_button.pressed.connect(func() -> void: back_requested.emit())
	add_child(back_button)

	# RETAIL'S PORTRAIT OF THE REGION UNDER THE POINTER. Sits directly above the
	# region card, because that is what it is a picture of. Sized here and moved
	# by `_relayout()` like everything else.
	region_portrait_frame = Control.new()
	region_portrait_frame.name = "RegionPortraitFrame"
	region_portrait_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	region_portrait_frame.draw.connect(_draw_region_portrait)
	add_child(region_portrait_frame)

	region_portrait_caption = Label.new()
	region_portrait_caption.name = "RegionPortraitCaption"
	region_portrait_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	region_portrait_caption.add_theme_font_size_override("font_size", 13)
	region_portrait_caption.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	region_portrait_caption.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(region_portrait_caption)

	resized.connect(_relayout)
	_relayout()


# --- layout -------------------------------------------------------------------

## THE SCREEN WAS AUTHORED AT ~1860x800 AND NAILED TO IT. Every control carried a
## hand-written pixel position, so on the 2560x1351 window the owner actually
## runs it in, retail's Middle-earth occupied the top-left 1240x548 of a 2560-wide
## panel and the other 40% of the screen was empty black.
##
## This gives the window back. The rule is stated rather than tuned:
##
##   * the SIDEBAR keeps a readable measure and no more - it is text, and text
##     set 900 pixels wide is harder to read, not easier - so it takes a fixed
##     column against the right edge, widening only as far as SIDE_MAX;
##   * the MAP takes EVERYTHING ELSE, both axes. It is the one thing on this
##     screen that gets better the bigger it is;
##   * the top band (title, turn, hint) and the bottom band (legend, message,
##     conversion report) keep their authored heights, because they are single
##     lines of type that do not improve with room.
##
## Nothing here is scaled: no font is stretched and no control is squashed. At
## the authored size the result is the authored layout, which is the property the
## runner asserts rather than a screenshot nobody re-reads.
const DESIGN_SIZE := Vector2(1860.0, 800.0)
const LAYOUT_MARGIN := 24.0
## The sidebar's authored measure, and the most it may grow to on a wide screen.
const SIDE_MIN := 524.0
const SIDE_MAX := 620.0
## The narrowest the sidebar may ever be. THE SIDEBAR GIVES WAY FIRST: on a
## window too narrow to hold both floors it shrinks below its authored measure
## rather than sliding over the map, because a panel drawn on top of Middle-earth
## is worse than a panel set narrow. Reached at 1100 pixels of window width.
const SIDE_FLOOR := 260.0
## What the map may not shrink below.
const MAP_MIN := Vector2(760.0, 380.0)
## The authored heights of the two bands the map sits between.
const TOP_BAND := 118.0
const BOTTOM_BAND := 162.0


func _relayout() -> void:
	if heading_label == null:
		return
	var frame := size
	if frame.x < 1.0 or frame.y < 1.0:
		frame = DESIGN_SIZE
	# The sidebar column, right-anchored, between its authored measure and a
	# ceiling - and never so wide that the map falls under its floor.
	var side_width := clampf(frame.x * 0.28, SIDE_MIN, SIDE_MAX)
	side_width = clampf(
		frame.x - MAP_MIN.x - LAYOUT_MARGIN * 3.0, SIDE_FLOOR, side_width)
	var side_x := frame.x - side_width - LAYOUT_MARGIN
	# THE MAP TAKES EXACTLY WHAT IS LEFT, with no floor of its own applied here -
	# a floor applied to the map instead of to the sidebar is precisely how a
	# panel ends up drawn over Middle-earth, which is what the runner caught at
	# 1280x1024. The map's floor is honoured by the clamp above.
	var map_width := maxf(side_x - LAYOUT_MARGIN * 2.0, 200.0)
	var map_height := maxf(frame.y - TOP_BAND - BOTTOM_BAND - LAYOUT_MARGIN, MAP_MIN.y)

	status_label.size.x = maxf(side_x - status_label.position.x - LAYOUT_MARGIN, 200.0)
	status_label.custom_minimum_size.x = status_label.size.x
	turn_banner.size.x = maxf(map_width * 0.62, 320.0)
	turn_banner.custom_minimum_size.x = turn_banner.size.x
	header_label.position.x = LAYOUT_MARGIN + 6.0 + turn_banner.size.x + 10.0
	header_label.size.x = maxf(side_x - header_label.position.x - LAYOUT_MARGIN, 200.0)
	header_label.custom_minimum_size.x = header_label.size.x
	hint_label.size.x = map_width
	hint_label.custom_minimum_size.x = map_width
	# CLIPPED, NOT OVERFLOWING. These are single lines whose length depends on
	# the strategic state, and an unclipped one wrote itself across the seat
	# table the moment a region name got long.
	for line_label in [status_label, turn_banner, header_label, hint_label]:
		line_label.clip_text = true

	for view in [map_view, map3d]:
		view.position = Vector2(LAYOUT_MARGIN, TOP_BAND)
		view.custom_minimum_size = Vector2(map_width, map_height)
		view.size = Vector2(map_width, map_height)

	var below := TOP_BAND + map_height + 6.0
	legend_label.position = Vector2(LAYOUT_MARGIN, below)
	legend_label.custom_minimum_size = Vector2(map_width, 24.0)
	legend_label.size = Vector2(map_width, 24.0)
	message_label.position = Vector2(LAYOUT_MARGIN, below + 28.0)
	message_label.custom_minimum_size = Vector2(map_width, 24.0)
	message_label.size = Vector2(map_width, 24.0)
	map_mode_label.position = Vector2(LAYOUT_MARGIN, below + 52.0)
	var mode_height := maxf(frame.y - map_mode_label.position.y - LAYOUT_MARGIN, 40.0)
	map_mode_label.custom_minimum_size = Vector2(map_width, mode_height)
	map_mode_label.size = Vector2(map_width, mode_height)

	# THE SIDEBAR, laid out from the BOTTOM UP: the buttons are the thing that
	# must never be off the panel or under something else, so they are placed
	# against the bottom edge first and the panels take what is left.
	var button_width := (side_width - 12.0) * 0.5
	back_button.position = Vector2(side_x, frame.y - 40.0 - LAYOUT_MARGIN)
	back_button.size = Vector2(button_width, 40.0)
	back_button.custom_minimum_size = back_button.size
	# AUTO-RESOLVE sits beside MAIN MENU on the bottom row rather than beside
	# ATTACK, so the two ways of deciding a battle are not adjacent and cannot be
	# hit by accident for each other.
	auto_resolve_button.position = Vector2(side_x + button_width + 12.0, back_button.position.y)
	auto_resolve_button.size = Vector2(button_width, 40.0)
	auto_resolve_button.custom_minimum_size = auto_resolve_button.size

	report_backdrop.position = Vector2(LAYOUT_MARGIN, LAYOUT_MARGIN)
	report_backdrop.size = Vector2(frame.x - LAYOUT_MARGIN * 2.0, frame.y - LAYOUT_MARGIN * 2.0)
	report_text.position = report_backdrop.position + Vector2(28.0, 24.0)
	report_text.size = report_backdrop.size - Vector2(56.0, 96.0)
	report_close.size = Vector2(180.0, 40.0)
	report_close.custom_minimum_size = report_close.size
	report_close.position = Vector2(
		report_backdrop.position.x + report_backdrop.size.x - 208.0,
		report_backdrop.position.y + report_backdrop.size.y - 56.0)

	var action_y := back_button.position.y - 54.0
	attack_button.position = Vector2(side_x, action_y)
	attack_button.size = Vector2(button_width, 44.0)
	attack_button.custom_minimum_size = attack_button.size
	end_turn_button.position = Vector2(side_x + button_width + 12.0, action_y)
	end_turn_button.size = Vector2(button_width, 44.0)
	end_turn_button.custom_minimum_size = end_turn_button.size

	# The unplaced block keeps its authored 84-pixel band, directly above the
	# buttons, and the two scrolling panels split everything above THAT.
	var unplaced_y := action_y - 92.0
	unplaced_label.position = Vector2(side_x, unplaced_y)
	unplaced_label.custom_minimum_size = Vector2(side_width, 40.0)
	unplaced_label.size = Vector2(side_width, 40.0)
	unplaced_host.position = Vector2(side_x, unplaced_y + 44.0)
	unplaced_host.custom_minimum_size = Vector2(side_width, 0.0)
	unplaced_host.size = Vector2(side_width, 0.0)

	# The portrait plate sits between the standings and the region card, at a
	# fixed 4:3 that is retail's own aspect for these images.
	var panel_top := 70.0
	var panel_space := maxf(unplaced_y - 10.0 - panel_top, 200.0)
	var portrait_height := clampf(panel_space * 0.24, 84.0, 190.0)
	var standings_height := maxf((panel_space - portrait_height - 30.0) * 0.42, 90.0)
	standings_label.position = Vector2(side_x, panel_top)
	standings_label.custom_minimum_size = Vector2(side_width, standings_height)
	standings_label.size = Vector2(side_width, standings_height)

	var portrait_y := panel_top + standings_height + 10.0
	region_portrait_frame.position = Vector2(side_x, portrait_y)
	region_portrait_frame.custom_minimum_size = Vector2(side_width, portrait_height)
	region_portrait_frame.size = Vector2(side_width, portrait_height)
	region_portrait_caption.position = Vector2(
		side_x + portrait_height * 4.0 / 3.0 + 12.0, portrait_y + 4.0)
	region_portrait_caption.size = Vector2(
		maxf(side_width - portrait_height * 4.0 / 3.0 - 12.0, 80.0), portrait_height - 8.0)
	region_portrait_caption.custom_minimum_size = region_portrait_caption.size

	var detail_y := portrait_y + portrait_height + 10.0
	detail_label.position = Vector2(side_x, detail_y)
	var detail_height := maxf(unplaced_y - 10.0 - detail_y, 120.0)
	detail_label.custom_minimum_size = Vector2(side_width, detail_height)
	detail_label.size = Vector2(side_width, detail_height)

	if chrome_layer != null:
		chrome_layer.queue_redraw()
	if legend_label != null:
		legend_label.queue_redraw()
	region_portrait_frame.queue_redraw()


## Bind a live session, or NO session plus the reason there is none. Both are
## legitimate states and the screen shows either honestly; what it never does is
## show a map when `bound_session` is null.
func configure(bound_session, map_ids: Array, reason: String, pack_roots: Array = []) -> void:
	if heading_label == null:
		build()
	session = bound_session
	available_map_ids = map_ids.duplicate()
	unavailable_reason = reason
	# RETAIL'S AUTO-RESOLVE TABLES AND THE UNIT BINDINGS, searched the same way
	# and in the same order as every other bundle: the mounted packs first, then
	# the documented environment override. A failure is NOT an error state - the
	# campaign runs fine without them, tactical battles are unaffected, and the
	# AUTO-RESOLVE button carries the loader's own reason - so the return is used
	# for the tooltip rather than checked here.
	if session != null:
		var loaded: Dictionary = session.load_auto_resolve(pack_roots)
		print("[wotr] auto-resolve: %s" % (
			"tables %s + bindings %s" % [
				String(loaded["rules_path"]).get_file(),
				String(loaded["bindings_path"]).get_file()]
			if bool(loaded.get("ok", false))
			else "UNAVAILABLE - " + String(loaded.get("reason", "")).split(".")[0]))
		if not session.auto_resolve_unbound_templates.is_empty():
			print("[wotr] auto-resolve: %d unit template(s) have no auto-resolve data in any retail object file and will not fight: %s" % [
				session.auto_resolve_unbound_templates.size(),
				", ".join(Array(session.auto_resolve_unbound_templates))])
	load_map_bundle(pack_roots)
	refresh()


## Find and load retail's converted 3D map. Separate from `configure()` so a test
## can drive it directly, and so a failure to load the MAP never stops the
## strategic layer from working - the 2D fallback is a real screen, not an error
## state.
## THE 90-REGION NUMBER, SPLIT SO IT STOPS NEEDING A FOOTNOTE.
##
## The region-image bundle carries 90 rows and every report that quoted it said
## "90 regions". Thirty-eight of those rows are `Region_1` .. `Region_38`: they
## carry no display name, no `RegionPortrait` and no `Fortress.Portrait`, and
## they are why the geometry probe reads `shadedRegions=52 ... unshaded=38`.
## Retail's own living-world document says what they are - it declares three
## `RegionCampaign` blocks, and their region lists are disjoint from each other
## except that two of them are IDENTICAL:
##
##   DefaultCampaign   52 regions, all named, all with a portrait
##   EvilCampaign      38 regions, `Region_1`..`Region_38`
##   GoodCampaign      the SAME 38 ids, not 38 more
##
## 52 + 38 = 90, which is exactly the bundle's row count, so nothing is dropped
## and nothing is double-counted. THE ROWS STAY. This is a reporting fix: the
## placeholders are still loaded, still addressable and still listed - they are
## just no longer added to a number that reads as "regions you can play".
##
## THE SPLIT IS MADE ON THE ROWS THEMSELVES, not on the id spelling. A row counts
## as a placeholder when it names NO art of any kind - no region portrait, no
## fortress portrait, no fortress display name. That is a property of the data
## rather than of the naming convention, so a placeholder that retail one day
## called something else would still be counted as one, and a real region that
## happened to be called `Region_7` would not.
func region_portrait_census() -> Dictionary:
	var playable: Array[String] = []
	var placeholders: Array[String] = []
	if region_images == null or not region_images.loaded:
		return {"playable": playable, "placeholders": placeholders, "rows": 0}
	var ids: Array[String] = []
	for key in region_images.regions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var row: Dictionary = region_images.regions[region_id] as Dictionary
		var bare := (
			String(row.get("regionPortrait", "")).is_empty()
			and String(row.get("fortressPortrait", "")).is_empty()
			and String(row.get("fortressDisplayName", "")).is_empty())
		if bare:
			placeholders.append(region_id)
		else:
			playable.append(region_id)
	return {
		"playable": playable, "placeholders": placeholders, "rows": ids.size(),
	}


## The census as one line, stating the ARITHMETIC rather than the total, because
## the total on its own is the thing that was misleading.
##
## `compact` is for the on-screen conversion report, which is a fixed panel that
## already runs long; the launch log gets the whole sentence, including which of
## retail's campaign blocks the placeholder rows come from. The two say the same
## thing and neither rounds a number.
func region_portrait_census_line(compact: bool = false) -> String:
	var census: Dictionary = region_portrait_census()
	var playable: Array = census["playable"] as Array
	var placeholders: Array = census["placeholders"] as Array
	if int(census["rows"]) <= 0:
		return "REGION CENSUS: no region-image bundle, so there is no region census to split."
	if placeholders.is_empty():
		return ("REGION CENSUS: %d region row(s), all of them named and carrying "
			+ "art; no placeholder rows in this bundle.") % playable.size()
	if compact:
		return ("REGION CENSUS: %d row(s) = %d PLAYABLE + %d PLACEHOLDER (%s and "
			+ "%d more, none with a name or a portrait of any kind - retail "
			+ "declares them in its EvilCampaign and GoodCampaign blocks, which "
			+ "list the SAME ids rather than a set each). The placeholder rows "
			+ "are KEPT, not dropped; they are not part of the playable count.") % [
				int(census["rows"]), playable.size(), placeholders.size(),
				String(placeholders[0]), placeholders.size() - 1]
	var shown := placeholders.slice(0, mini(placeholders.size(), 4))
	return ("REGION CENSUS: %d row(s) = %d PLAYABLE region(s) + %d PLACEHOLDER "
		+ "row(s), %d + %d = %d. The placeholders carry no name and no portrait "
		+ "of any kind (%s%s); retail's own living-world document declares them "
		+ "in its EvilCampaign and GoodCampaign blocks, which list the SAME %d "
		+ "ids rather than %d each, beside DefaultCampaign's %d named regions. "
		+ "They are kept and addressable, not dropped - they are simply not part "
		+ "of the playable count.") % [
			int(census["rows"]), playable.size(), placeholders.size(),
			playable.size(), placeholders.size(), int(census["rows"]),
			", ".join(shown), "" if placeholders.size() <= shown.size() else ", ...",
			placeholders.size(), placeholders.size(), playable.size()]


func load_map_bundle(pack_roots: Array = []) -> bool:
	if heading_label == null:
		build()
	var bundle := BundleScript.new()
	var located: Dictionary = bundle.locate_and_load(pack_roots)
	if bool(located.get("ok", false)):
		map_bundle = bundle
		map_reason = ""
		print("[WotrMap] retail 3D map LOADED from %s [%s]" % [
			String(located.get("root", "")), String(located.get("origin", ""))])
		for line in bundle.describe_load():
			print("[WotrMap]   %s" % line)
		# A map that loaded WITH holes in it is not a clean load, and the log has
		# to distinguish the two or a degraded map reads as a good one.
		if not bundle.warnings.is_empty():
			push_warning("[WotrMap] the retail map loaded with %d texture problem(s): %s" % [
				bundle.warnings.size(), ", ".join(Array(bundle.warnings))])
	else:
		map_bundle = null
		map_reason = String(located.get("reason", ""))
		# LOUDLY. The whole point of this block: falling back to the flat 2D graph
		# used to be completely silent - no print, no warning, nothing in the log -
		# so the only way to discover it was to notice the map looked wrong. Both
		# channels now carry it, and the reason names every path, its origin and
		# the command that produces a bundle.
		push_error("[WotrMap] %s" % map_reason)
		for line in map_reason.split("\n"):
			print("[WotrMap] %s" % line)
	map3d.set_bundle(map_bundle, map_reason)
	load_region_geometry(pack_roots)
	var has_3d: bool = map3d.has_map()
	map3d.visible = has_3d
	map_view.visible = not has_3d
	# "The bundle parsed" and "there is a map on screen" are different claims.
	# If the bundle loaded but nothing was instanced, say so rather than showing
	# an empty black viewport that looks like a rendering bug.
	if has_3d and map3d.drawn_mesh_count() <= 0:
		push_error("[WotrMap] the bundle at %s loaded but produced NO drawable sub-objects; showing the flat 2D fallback instead."
			% String(located.get("root", "")))
		map_reason = ("The living-map bundle at %s loaded but produced no drawable "
			+ "sub-objects, so there is nothing to show. Every sub-object in it was "
			+ "an impassable volume, an ambient card or a shader-only surface.") % String(located.get("root", ""))
		map_bundle = null
		map3d.set_bundle(null, map_reason)
		map3d.visible = false
		map_view.visible = true
		return false
	return has_3d


## Find and load retail's per-region territory geometry. Independent of the map
## bundle on purpose: retail's Middle-earth can be on screen with no territory
## shapes converted, and that is a state the screen reports rather than hides.
func load_region_geometry(pack_roots: Array = []) -> bool:
	if heading_label == null:
		build()
	var geometry := RegionGeometryScript.new()
	var located: Dictionary = geometry.locate_and_load(pack_roots)
	if bool(located.get("ok", false)):
		region_geometry = geometry
		region_geometry_reason = ""
		print("[WotrMap] region TERRITORY GEOMETRY loaded from %s [%s]" % [
			String(located.get("root", "")), String(located.get("origin", ""))])
		for line in geometry.describe_load():
			print("[WotrMap]   %s" % line)
		if geometry.regions_without_geometry.size() > 0:
			push_warning("[WotrMap] %d region(s) in the document have NO territory mesh and will not be shaded: %s" % [
				geometry.regions_without_geometry.size(),
				", ".join(Array(geometry.regions_without_geometry))])
	else:
		region_geometry = null
		region_geometry_reason = String(located.get("reason", ""))
		# LOUDLY, on both channels, for the same reason the map bundle does: a
		# strategic map that quietly stopped shading territories would just look
		# like a rendering bug.
		push_error("[WotrMap] %s" % region_geometry_reason)
		for line in region_geometry_reason.split("\n"):
			print("[WotrMap] %s" % line)
	map3d.set_region_geometry(region_geometry, region_geometry_reason)
	_load_strings(located, pack_roots)
	return region_geometry != null


## Retail's string table, looked for beside whichever region-geometry bundle was
## found and in the mounted packs. A miss is a REPORTED miss: regions keep their
## retail ids and the screen says the table is not converted.
func _load_strings(located: Dictionary, pack_roots: Array) -> void:
	var roots: Array = []
	var geometry_root := String(located.get("root", ""))
	if not geometry_root.is_empty():
		roots.append(geometry_root)
	for root in pack_roots:
		roots.append(String(root).path_join(RegionGeometryScript.PACK_BUNDLE_RELATIVE))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	var table := StringsScript.new()
	var found: Dictionary = table.locate_and_load(roots)
	if bool(found.get("ok", false)):
		strings = table
		strings_reason = ""
		print("[WotrStrings] retail string table loaded from %s: %d strings" % [
			String(found.get("path", "")), table.count()])
	else:
		strings = null
		strings_reason = String(found.get("reason", ""))
		push_warning("[WotrStrings] %s" % strings_reason)
		print("[WotrStrings] %s" % strings_reason)

	# RETAIL'S UI SURFACE, looked for in the same places. Independent of the
	# string table on purpose: names and portraits fail separately and a screen
	# that reported one for the other would send a reader to the wrong bundle.
	var ui_bundle := LivingWorldUiScript.new()
	var ui_found: Dictionary = ui_bundle.locate_and_load(roots)
	if bool(ui_found.get("ok", false)):
		ui = ui_bundle
		ui_reason = ""
		print("[WotrUI] retail living-world UI bundle loaded from %s" % String(ui_found.get("path", "")))
		for line in ui_bundle.describe_load():
			print("[WotrUI]   %s" % line)
	else:
		ui = null
		ui_reason = String(ui_found.get("reason", ""))
		push_warning("[WotrUI] %s" % ui_reason)
		for line in ui_reason.split("\n"):
			print("[WotrUI] %s" % line)
	map3d.set_ui(ui, ui_reason)

	# RETAIL'S 3D MARKER MODELS, looked for in the same places and failing
	# independently again: retail's portraits can be converted with none of its
	# banner geometry, and the screen has to be able to say which of the two is
	# missing rather than "the markers look wrong".
	var marker_bundle := MarkerModelsScript.new()
	var markers_found: Dictionary = marker_bundle.locate_and_load(roots)
	if bool(markers_found.get("ok", false)):
		markers = marker_bundle
		markers_reason = ""
		print("[WotrMarkers] retail 3D marker models loaded from %s" % String(markers_found.get("path", "")))
		for line in marker_bundle.describe_load():
			print("[WotrMarkers]   %s" % line)
	else:
		markers = null
		markers_reason = String(markers_found.get("reason", ""))
		push_warning("[WotrMarkers] %s" % markers_reason)
		for line in markers_reason.split("\n"):
			print("[WotrMarkers] %s" % line)
	map3d.set_markers(markers, markers_reason)

	# RETAIL'S OWN PORTRAITS OF THE REGIONS, for the region card. Independent
	# again: the marker geometry and the card art come from different documents
	# and fail for different reasons.
	var region_image_bundle := RegionImagesScript.new()
	var region_images_found: Dictionary = region_image_bundle.locate_and_load(roots)
	if bool(region_images_found.get("ok", false)):
		region_images = region_image_bundle
		region_images_reason = ""
		print("[WotrRegionArt] retail region portraits loaded from %s" % String(region_images_found.get("path", "")))
		for line in region_image_bundle.describe_load():
			print("[WotrRegionArt]   %s" % line)
		print("[WotrRegionArt]   %s" % region_portrait_census_line())
	else:
		region_images = null
		region_images_reason = String(region_images_found.get("reason", ""))
		push_warning("[WotrRegionArt] %s" % region_images_reason)
		for line in region_images_reason.split("\n"):
			print("[WotrRegionArt] %s" % line)

	var macro_table := MacrosScript.new()
	var macros_found: Dictionary = macro_table.locate_and_load(roots)
	if bool(macros_found.get("ok", false)):
		macros = macro_table
		macros_reason = ""
		print("[WotrStrings] retail gamedata #define table loaded from %s: %d defines" % [
			String(macros_found.get("path", "")), macro_table.defines.size()])
	else:
		macros = null
		macros_reason = String(macros_found.get("reason", ""))
		push_warning("[WotrStrings] %s" % macros_reason)
		print("[WotrStrings] %s" % macros_reason)


func refresh() -> void:
	if heading_label == null:
		return
	_rows = []
	_row_by_id = {}
	_targets = PackedStringArray()
	_moves = PackedStringArray()
	_staging = PackedStringArray()
	if session == null or session.state == null:
		status_label.text = "UNAVAILABLE"
		turn_banner.text = "WAR OF THE RING IS UNAVAILABLE"
		header_label.text = ""
		hint_label.text = unavailable_reason
		standings_label.text = ""
		detail_label.text = "[color=#e1c77d]War of the Ring is unavailable.[/color]\n\n%s" % unavailable_reason
		attack_button.disabled = true
		attack_button.tooltip_text = "There is no strategic session to attack in."
		auto_resolve_button.disabled = true
		auto_resolve_button.tooltip_text = "There is no strategic session to auto-resolve in."
		end_turn_button.disabled = true
		end_turn_button.tooltip_text = "There is no strategic session whose turn could pass."
		_clear_unplaced()
		unplaced_label.text = ""
		map_view.queue_redraw()
		legend_label.queue_redraw()
		_refresh_map_mode_label()
		return

	_rows = session.region_rows()
	for row in _rows:
		_row_by_id[String(row["id"])] = row
	_staging = session.staging_regions()
	if not session.selected_region.is_empty():
		_targets = session.attack_targets(session.selected_region)
		_moves = session.movement_targets(session.selected_region)

	var state: StateScript = session.state
	var seat := state.active_player()
	var seat_row: Dictionary = state.players[seat] as Dictionary if seat != StateScript.NEUTRAL else {}
	# The provenance line: which document, from where, which campaign, which
	# scenario. It stays because "the map looks wrong" is usually "a different
	# document loaded than you think".
	status_label.text = "DOCUMENT %s (%s)   CAMPAIGN %s   SCENARIO %s   %d regions   %d armies" % [
		session.document_path.get_file(),
		session.document_source,
		session.world.campaign_name,
		session.scenario_name,
		session.world.region_ids.size(),
		state.armies.size(),
	]
	_refresh_turn_banner(state, seat, seat_row)
	_refresh_header(state, seat, seat_row)
	end_turn_button.disabled = not state.pending_battle.is_empty()
	end_turn_button.tooltip_text = (
		"A battle for %s is still in flight; it must resolve before the turn passes."
			% String(state.pending_battle.get("region", ""))
		if not state.pending_battle.is_empty()
		else "Pass the turn to the next seat.")
	attack_button.disabled = not can_attack_now()
	attack_button.tooltip_text = _attack_button_reason()
	# AUTO-RESOLVE needs the same committable attack ATTACK does, plus the two
	# converted bundles. When either is missing the button is disabled and the
	# tooltip is the loader's own reason naming every path it searched - never a
	# bare "unavailable", and never a battle quietly fought on invented numbers.
	var can_auto := can_attack_now() and session.autoresolve != null \
		and session.autoresolve_bindings != null \
		and String(state.battle_type) != StateScript.BATTLE_TYPE_RTS
	auto_resolve_button.disabled = not can_auto
	auto_resolve_button.tooltip_text = _auto_resolve_button_reason(state)
	_refresh_standings(state)
	# THE MAP FIRST. `_rebuild_unplaced()` reports which regions the map could
	# not place, so it has to run AFTER the map has placed them - otherwise it
	# reports the previous frame's answer, and on the first frame it reports
	# "nothing was placed". That is exactly how Rhun came to be listed as absent
	# on the same screen whose mode line said it had been placed.
	_refresh_map()
	_rebuild_unplaced()
	_refresh_detail()
	legend_label.queue_redraw()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## Push the strategic picture into whichever map is showing. Strictly one-way:
## the map is handed already-computed rows and never writes anything back.
func _refresh_map() -> void:
	if map3d != null and map3d.has_map():
		var adjacency: Dictionary = {}
		for region_id in session.world.region_ids:
			adjacency[String(region_id)] = session.world.neighbours(String(region_id))
		map3d.owner_colors = SEAT_COLORS
		map3d.neutral_color = NEUTRAL_COLOR
		map3d.set_regions(
			_rows, adjacency, _staging, _targets,
			session.selected_region, session.selected_target)
		map3d.set_overlays(
			_army_stacks_by_region(), _plots_by_region(), _display_names(),
			selected_plot, _radial_entries(), _plot_icons_by_region())
		_refresh_map_mode_label()
		return
	map_view.queue_redraw()
	_refresh_map_mode_label()


func _refresh_map_mode_label() -> void:
	if map_mode_label == null:
		return
	if map3d == null or not map3d.has_map():
		# The full reason is drawn on the map itself and printed to the launch
		# log; this line exists so the mode is unambiguous even at a glance.
		map_mode_label.text = (
			"MAP: flat 2D region graph (FALLBACK) - retail's 3D Middle-earth did NOT load. "
			+ "The reason is printed on the map above and in the launch log under [WotrMap].")
		return
	var notes: Array[String] = []
	notes.append("MAP: retail livingmap.w3d, %d sub-objects, %d drawn, %d regions placed at authored world coordinates" % [
		map_bundle.sub_objects.size(), map3d.drawn_mesh_count(), map3d.placed_regions.size()])
	# TERRITORY SHADING: what is filled, and what is not.
	if map3d.has_territories():
		notes.append("TERRITORIES: retail lmr_fill.w3d / lmr_border.w3d, %d regions filled with their owner's colour, %d triangles" % [
			map3d.shaded_regions.size(), region_geometry.total_triangles])
		if map3d.unshaded_regions.size() > 0:
			notes.append("NOT SHADED (%d): the bundle carries no fill mesh for these, so they keep a marker and no territory - %s" % [
				map3d.unshaded_regions.size(), ", ".join(Array(map3d.unshaded_regions))])
	else:
		notes.append("TERRITORIES: NOT SHADED - regions are drawn as markers. %s" % region_geometry_reason.split("\n")[0])
	if map3d.centroid_placed_regions.size() > 0:
		notes.append("%d region(s) placed from a centroid DERIVED from retail's own fill triangles rather than an authored centre point: %s" % [
			map3d.centroid_placed_regions.size(), ", ".join(Array(map3d.centroid_placed_regions))])
	if strings != null:
		notes.append("NAMES: retail data/lotr.str, %d strings; %d key(s) asked for and absent" % [
			strings.count(), strings.missing_keys.size()])
	else:
		notes.append("NAMES: NOT CONVERTED - regions carry retail's own ids")
	# THE REGION CENSUS, SPLIT. The 90 in the region-art bundle is 52 playable
	# regions plus 38 placeholder rows, and reporting the total on its own is
	# what made `unshaded=38` read as a conversion gap rather than as retail's
	# own second-campaign block.
	if region_images != null and region_images.loaded:
		notes.append(region_portrait_census_line(true))
	# LABELS: how many names are on screen and how many were held back so the
	# rest could be read. A label quietly dropped is exactly the kind of thing
	# that looks like a rendering bug, so the count is stated.
	notes.append("LABELS: %d drawn, %d held back where they would have overlapped a label already placed" % [
		map3d.labels_drawn, map3d.labels_suppressed])
	# PORTRAITS: what the banners are actually carrying.
	if ui != null:
		notes.append("BANNERS: %d army banner(s) drawn from retail MappedImage crops - %d of %d image ids resolved across %d atlases" % [
			map3d.banners_drawn, int(ui.totals.get("imageIdsResolved", 0)),
			int(ui.totals.get("imageIdsRequested", 0)), int(ui.totals.get("atlases", 0))])
		var crops_missing: Array = ui.gaps.get("cropsWithoutAtlas", []) as Array
		if not crops_missing.is_empty():
			notes.append("NO ATLAS (%d): retail's own data names these images and ships no texture for them, so they are drawn as an empty slot - %s" % [
				crops_missing.size(),
				", ".join(crops_missing.map(func(v: Variant) -> String: return String(v)))])
		if not map3d.banners_without_portrait.is_empty():
			var bare: Array[String] = []
			for key in map3d.banners_without_portrait.keys():
				bare.append("%s (%s)" % [String(key), String(map3d.banners_without_portrait[key])])
			bare.sort()
			notes.append("BANNERS WITHOUT A PORTRAIT (%d), drawn as a bare faction plate: %s" % [
				bare.size(), ", ".join(bare)])
		if not ui.missing_images.is_empty():
			var unresolved: Array[String] = []
			for key in ui.missing_images.keys():
				unresolved.append("%s - %s" % [String(key), String(ui.missing_images[key])])
			unresolved.sort()
			notes.append("IMAGE IDS ASKED FOR AND NOT DRAWN (%d): %s" % [
				unresolved.size(), ", ".join(unresolved)])
	else:
		notes.append("BANNERS: NOT CONVERTED - army stacks carry no portrait. %s" % ui_reason.split("\n")[0])
	# THE 3D MARKERS: what is standing on the map as retail's own geometry, and
	# what is still a flat stand-in with the reason. Two different claims.
	if markers != null:
		notes.append("MARKERS: retail's own W3D marker models, %d of %d converted - %d meshes, %d triangles across %d families and %d slots. %d army stack(s) and %d build plot(s) are standing as retail geometry." % [
			int(markers.totals.get("modelsConverted", 0)), int(markers.totals.get("modelsNamed", 0)),
			int(markers.totals.get("meshes", 0)), int(markers.totals.get("triangles", 0)),
			int(markers.totals.get("families", 0)), int(markers.totals.get("slots", 0)),
			map3d.army_markers_standing, map3d.plot_markers_standing])
		# THE ONE NUMBER IN THE MARKERS THAT IS NOT RETAIL'S, said out loud.
		notes.append("MARKER SIZE: retail's own Scale, ZOffset and OrientAngle, times a PRESENTATION magnification of x%.2f at this framing - x1.00 (retail's exact authored size) at zoom %.2f and below, capped at x%.2f. Retail's camera never pulls back as far as this one can, and at retail's true size a banner is about fifteen pixels across the whole map." % [
			map3d.marker_magnification(), map3d.MARKER_TRUE_ZOOM,
			map3d.MARKER_MAX_MAGNIFICATION])
		# STRUCTURES ARE CONVERTED AND DELIBERATELY NOT PLACED, stated rather than
		# left as a silent absence: nothing built them, so there is nothing there.
		var building_families := int((markers.totals.get("familiesByKind", {}) as Dictionary).get("building", 0))
		notes.append("STRUCTURE MODELS: %d LivingWorldBuildingIcon famil(ies) are converted and NONE is placed - construction is not simulated, so no structure exists on any plot to stand one on." % building_families)
		if not markers.unresolved_models.is_empty():
			var absent: Array[String] = []
			for key in markers.unresolved_models.keys():
				absent.append("%s (%s)" % [String(key), String(markers.unresolved_models[key])])
			absent.sort()
			notes.append("MARKER MODELS NOT CONVERTED (%d): %s" % [absent.size(), ", ".join(absent)])
		for pair in [["ARMY STACKS", map3d.army_markers_flat], ["BUILD PLOTS", map3d.plot_markers_flat]]:
			var table := pair[1] as Dictionary
			if table.is_empty():
				continue
			var flat: Array[String] = []
			for key in table.keys():
				flat.append("%s - %s" % [String(key), String(table[key])])
			flat.sort()
			notes.append("%s STILL DRAWN FLAT (%d): %s" % [String(pair[0]), flat.size(), ", ".join(flat)])
	else:
		notes.append("MARKERS: NOT CONVERTED - armies are flat plates and build plots are flat rings. %s" % markers_reason.split("\n")[0])
	# HAND-BUILT, SAID OUT LOUD. Retail's map surround genuinely does not resolve
	# - its frame art names three .tga files that are in no archive under any
	# name, and the APT vector shapes are masks rather than filigree - so the
	# parchment band, the corner studs and the compass rose are this project's
	# own drawing in retail's palette. Calling them retail art would be the same
	# dishonesty as an invented number.
	notes.append("MAP SURROUND: HAND-BUILT, not converted - the parchment band, the gold rule, the four corner studs and the compass rose are drawn in retail's style because retail's own frame art resolves to nothing. The rose is not decoration: it turns with the camera's yaw, so it never claims north is up while the map has been orbited.")
	if not map_bundle.warnings.is_empty():
		notes.append("%d texture problem(s): %s" % [
			map_bundle.warnings.size(), ", ".join(Array(map_bundle.warnings))])
	if map3d.unplaced_regions.size() > 0:
		notes.append("%d region(s) unplaced (no authored centre point)" % map3d.unplaced_regions.size())
	if map3d.unsampled_heights.size() > 0:
		notes.append("%d region height(s) not sampled from terrain" % map3d.unsampled_heights.size())
	if map_bundle.untextured_sub_objects.size() > 0:
		notes.append("%d sub-object(s) drawn untextured: %s" % [
			map_bundle.untextured_sub_objects.size(),
			", ".join(Array(map_bundle.untextured_sub_objects))])
	# Everything retail draws that this lane does not, named on screen. A map
	# quietly missing its rivers and its ocean shader would look finished.
	var not_drawn: Array[String] = []
	for entry in map_bundle.sub_objects:
		if bool(entry["collision"]) or bool(entry["ambient"]) or bool(entry["shader_only"]):
			not_drawn.append(entry["name"] as String)
	not_drawn.sort()
	if not_drawn.size() > 0:
		notes.append("NOT DRAWN (%d): retail's impassable volumes, its animated ambient cards and multi-stage water overlays - %s" % [
			not_drawn.size(), ", ".join(not_drawn)])
	map_mode_label.text = "   |   ".join(notes)


## True when the current selection is a legal, committable attack.
func can_attack_now() -> bool:
	if session == null or session.state == null:
		return false
	if not session.state.pending_battle.is_empty():
		return false
	if session.selected_region.is_empty() or session.selected_target.is_empty():
		return false
	return Array(_targets).has(session.selected_target)


## Select a region to stage from. Returns false (with a shown reason) when the
## region cannot stage an attack this turn.
func select_region(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if not _row_by_id.has(region_id):
		_message("%s is not a region of this campaign." % region_id)
		return false
	if not Array(_staging).has(region_id):
		var owner := session.state.owner_of(region_id)
		if owner != session.state.active_player():
			_message("%s is not yours to attack from." % _display_of(region_id))
		else:
			_message("%s holds no army to attack with." % _display_of(region_id))
		session.selected_region = ""
		session.selected_target = ""
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	# The build ring belongs to a plot in a region; staging somewhere else closes
	# it rather than leaving it hanging over ground the player has left.
	if String(selected_plot.get("region", "")) != region_id:
		selected_plot = {}
	_message("")
	refresh()
	return true


## Choose the adjacent region to attack. Refuses anything the strategic layer
## does not report as attackable from the staged region.
func select_target(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to attack from first.")
		return false
	if not Array(_targets).has(region_id):
		_message("%s cannot be attacked from %s." % [_display_of(region_id), _display_of(session.selected_region)])
		return false
	session.selected_target = region_id
	_message("")
	refresh()
	return true


## Commit the selected attack. THE ONLY path from this screen to a battle, and it
## carries the target region and nothing else - the attacker, the armies, the
## factions and the ground are all derived by the strategic layer and recorded in
## the commitment.
func commit_selected_attack() -> Dictionary:
	if not can_attack_now():
		_message("There is no committable attack selected.")
		return {"ok": false}
	var configured: Dictionary = session.commit_attack(session.selected_target, available_map_ids)
	if not bool(configured.get("ok", false)):
		_message("Attack refused: %s" % ", ".join(Array(configured.get("refusals", PackedStringArray()))))
		refresh()
		return configured
	refresh()
	battle_committed.emit(configured)
	return configured


## AUTO-RESOLVE THE SELECTED ATTACK, and show the working.
##
## It commits and resolves in one press, because a half-committed auto-resolve -
## a battle admitted into the strategic state that the player then has no way to
## finish - would strand the campaign with a transaction open. Both halves go
## through the session; this screen decides nothing.
func auto_resolve_selected_attack() -> Dictionary:
	if not can_attack_now():
		_message("There is no committable attack selected.")
		return {"ok": false}
	var committed: Dictionary = session.commit_attack(
		session.selected_target, available_map_ids, StateScript.BATTLE_TYPE_AUTO_RESOLVE)
	if not bool(committed.get("ok", false)):
		_message("Auto-resolve refused: %s" % ", ".join(
			Array(committed.get("refusals", PackedStringArray()))))
		refresh()
		return committed
	var resolved: Dictionary = session.auto_resolve_pending_battle()
	if not bool(resolved.get("ok", false)):
		# The commitment is still open if the resolution refused. Say so rather
		# than leaving the player looking at an unchanged map with no explanation.
		_message("Auto-resolve refused: %s" % ", ".join(
			Array(resolved.get("refusals", PackedStringArray()))))
		refresh()
		return resolved
	last_auto_resolve = {
		"commitment": committed.get("commitment", {}),
		"outcome": resolved.get("outcome", {}),
		"applied": resolved.get("applied", {}),
		"seed": String(resolved.get("seed", "")),
	}
	_show_battle_report()
	refresh()
	return resolved


func close_battle_report() -> void:
	last_auto_resolve = {}
	report_backdrop.visible = false
	report_text.visible = false
	report_close.visible = false
	refresh()


func _show_battle_report() -> void:
	report_text.text = battle_report_bbcode()
	report_backdrop.visible = true
	report_text.visible = true
	report_close.visible = true
	report_close.grab_focus()


## THE WORKING, as the player reads it.
##
## THE WHOLE POINT OF THIS SCREEN is that it is obvious which numbers are EA's
## and which are this project's. Retail's are printed in one colour and name the
## retail file they came from; this project's are printed in another, marked
## PROJECT, and name the row of `wotr_autoresolve_rules.gd` a modder would edit
## to change them. There is no third category and nothing is unattributed.
func battle_report_bbcode() -> String:
	if last_auto_resolve.is_empty():
		return ""
	var commitment: Dictionary = last_auto_resolve.get("commitment", {})
	var outcome: Dictionary = last_auto_resolve.get("outcome", {})
	var applied: Dictionary = last_auto_resolve.get("applied", {})
	var lines: Array[String] = []

	lines.append("[b][font_size=26]BATTLE FOR %s[/font_size][/b]" % _display_of(
		String(commitment.get("region", ""))).to_upper())
	var winner := String(outcome.get("winner", ""))
	var headline := "UNDECIDED"
	if winner == "attacker":
		headline = "ATTACKER WINS"
	elif winner == "defender":
		headline = "DEFENDER HOLDS"
	lines.append("[b][font_size=20]%s[/font_size][/b]  after %d round(s)" % [
		headline, int(outcome.get("rounds", 0))])
	lines.append("[color=#c8c2b0]%s[/color]" % String(outcome.get("reason", "")))
	lines.append("")

	# THE KEY, FIRST, so nothing below has to be guessed at.
	lines.append("[b]HOW TO READ THIS[/b]")
	lines.append("  [color=%s]RETAIL[/color]   a number EA authored. The retail file it came from is named beside it." % RETAIL_COLOR)
	lines.append("  [color=%s]PROJECT[/color]  a rule retail never states. Open BFME chose it; the row it lives in is named beside it, in game/src/wotr/wotr_autoresolve_rules.gd, and you can edit it." % PROJECT_COLOR)
	lines.append("")

	lines.append("[b]THE DICE ARE OURS, AND THEY ARE SEEDED FROM THIS BATTLE[/b]")
	lines.append("  [color=%s]PROJECT[/color]  Risk dice: the striking unit rolls %d, the unit struck rolls %d, top %d paired highest against highest, ties to the %s. Two pairs won = full damage, one = half, none = nothing." % [
		PROJECT_COLOR,
		RulesScript.int_value("attacker_dice", 3), RulesScript.int_value("defender_dice", 2),
		RulesScript.int_value("pairs_compared", 2),
		"defender" if RulesScript.bool_value("ties_to_defender", true) else "attacker"])
	lines.append("  [color=%s]PROJECT[/color]  they REPLACE retail's own MissPercentChance roll, whose seed retail never states. Average landed fraction 0.5396 against retail's own 0.5000." % PROJECT_COLOR)
	lines.append("  [color=#8fa4bd]seed[/color] %s" % String(last_auto_resolve.get("seed", "")))
	lines.append("  [color=#8fa4bd]the seed is the SHA-256 of this battle's commitment - region, turn, both seats, both factions, both handicaps, the battlefield and every army id. No clock is read anywhere on this path, so every player's copy of this campaign rolled exactly these dice.[/color]")
	lines.append("")

	lines.append("[b]THE SIDES[/b]")
	for role in ["attacker", "defender"]:
		var seat := int(commitment.get(role, -1))
		var side: Dictionary = outcome.get(role, {})
		var handicap: Dictionary = side.get("handicap", {})
		lines.append("  [b]%s[/b] seat %d, %s, handicap %d%%  [color=%s]RETAIL[/color] weapon x%s / armour x%s (livingworldautoresolvehandicaps.ini)" % [
			role.to_upper(), seat, String(commitment.get("%s_faction" % role, "")),
			int(commitment.get("%s_handicap" % role, 0)), RETAIL_COLOR,
			str(handicap.get("weaponMultiplier", 1.0)), str(handicap.get("armorMultiplier", 1.0))])
		var survivors: Array = side.get("survivors", [])
		var lost: PackedStringArray = side.get("lost", PackedStringArray())
		lines.append("    %d survived, %d lost%s" % [
			survivors.size(), lost.size(),
			"" if lost.is_empty() else ": " + ", ".join(Array(lost))])
		for row in survivors:
			var unit: Dictionary = row
			lines.append("      %s  %.1f / %.1f hp  [color=%s]RETAIL[/color] %s, %s vs %s" % [
				String(unit.get("template", "?")),
				float(int(unit.get("hitpoints_milli", 0))) / 1000.0,
				float(int(unit.get("max_hitpoints_milli", 0))) / 1000.0,
				RETAIL_COLOR, String(unit.get("body", "")),
				String(unit.get("weapon", "")), String(unit.get("armor", ""))])
	lines.append("")

	lines.append("[b]THE ROUNDS[/b]")
	for entry in outcome.get("log", []) as Array:
		var round_row: Dictionary = entry
		lines.append("  [b]round %d[/b] - %d strike(s), %.1f total damage%s" % [
			int(round_row.get("round", 0)), int(round_row.get("strikeCount", 0)),
			float(int(round_row.get("damageMilli", 0))) / 1000.0,
			"" if bool(round_row.get("detailed", false))
				else "  [color=#8fa4bd](abbreviated: only the first %d rounds carry every die)[/color]"
					% AutoResolveBattleScript.DETAILED_ROUNDS])
		for strike_value in round_row.get("strikes", []) as Array:
			var strike: Dictionary = strike_value
			var contest: Dictionary = strike.get("contest", {})
			lines.append("    %s (%s) strikes %s (%s) for %.1f" % [
				String(strike.get("attacker", "?")), String(strike.get("attackerType", "")),
				String(strike.get("defender", "?")), String(strike.get("defenderType", "")),
				float(int(strike.get("damageMilli", 0))) / 1000.0])
			for factor_value in strike.get("factors", []) as Array:
				var factor: Dictionary = factor_value
				var owner := String(factor.get("owner", ""))
				lines.append("        [color=%s]%s[/color] %-22s x%.3f   %s" % [
					RETAIL_COLOR if owner == "retail" else PROJECT_COLOR,
					"RETAIL " if owner == "retail" else "PROJECT",
					String(factor.get("name", "")),
					float(int(factor.get("milli", 0))) / 1000.0,
					String(factor.get("source", ""))])
			if not contest.is_empty():
				lines.append("        [color=%s]the dice[/color] attacker %s vs defender %s -> %d of %d pairs won" % [
					PROJECT_COLOR, str(contest.get("attackerDice", [])),
					str(contest.get("defenderDice", [])), int(contest.get("pairsWon", 0)),
					(contest.get("pairs", []) as Array).size()])
	if int(outcome.get("abbreviatedRounds", 0)) > 0:
		lines.append("  [color=#8fa4bd]%d further round(s) are summarised rather than itemised. That is a limit of this REPORT, not of the battle: every round was fought in full.[/color]"
			% int(outcome.get("abbreviatedRounds", 0)))
	lines.append("")

	lines.append("[b]WHAT IT DID TO THE CAMPAIGN[/b]")
	lines.append("  region %s%s" % [
		_display_of(String(applied.get("region", ""))),
		" CHANGED HANDS" if bool(applied.get("captured", false)) else " did not change hands"])
	lines.append("  armies destroyed: %s" % _ids_or_none(applied.get("armies_lost", PackedInt32Array())))
	lines.append("  armies reduced:   %s" % _ids_or_none(applied.get("armies_reduced", PackedInt32Array())))
	lines.append("  armies advanced:  %s" % _ids_or_none(applied.get("armies_advanced", PackedInt32Array())))
	for reason in applied.get("refusals", PackedStringArray()) as PackedStringArray:
		lines.append("  [color=#e0a24a]not fully applied: %s[/color]" % String(reason))
	var unresolved: PackedStringArray = outcome.get("unresolved", PackedStringArray())
	if not unresolved.is_empty():
		lines.append("")
		lines.append("[b]WHAT DID NOT RESOLVE[/b] - named rather than substituted with a number")
		for note in unresolved:
			lines.append("  [color=#e0a24a]%s[/color]" % String(note))
	lines.append("")

	lines.append("[b]EVERY RULE THIS BATTLE RAN UNDER[/b] - the whole editable table, in one place")
	for row_value in outcome.get("rules", []) as Array:
		var rule: Dictionary = row_value
		var owner := String(rule.get("owner", ""))
		lines.append("  [color=%s]%s[/color] %s" % [
			RETAIL_COLOR if owner == "retail" else PROJECT_COLOR,
			"RETAIL " if owner == "retail" else "PROJECT", String(rule.get("text", ""))])
	return "\n".join(lines)


static func _ids_or_none(ids: PackedInt32Array) -> String:
	if ids.is_empty():
		return "none"
	var parts: Array[String] = []
	for value in ids:
		parts.append(str(int(value)))
	return ", ".join(parts)


func _on_auto_resolve_pressed() -> void:
	auto_resolve_selected_attack()


func end_turn() -> void:
	if session == null or session.state == null:
		return
	if not session.state.pending_battle.is_empty():
		_message("A battle is still in flight; it must resolve before the turn passes.")
		return
	session.state.advance_turn()
	session.selected_region = ""
	session.selected_target = ""
	_message("")
	refresh()
	turn_ended.emit()


func show_message(text: String) -> void:
	_message(text)


# --- turn, standings, legend --------------------------------------------------

## The one line a player reads first, and under it the one line that says what a
## click does next. Both are derived from the strategic state; neither invents a
## phase retail has that this lane does not model.
func _refresh_turn_banner(state: StateScript, seat: int, seat_row: Dictionary) -> void:
	var controller := String(seat_row.get("controller", "?"))
	var whose := "YOUR MOVE" if controller == StateScript.CONTROLLER_HUMAN else "AI SEAT"
	turn_banner.text = "TURN %d   ROUND %d   %s - SEAT %d, %s (%s)" % [
		state.turn_index + 1, state.round_index() + 1, whose, seat,
		String(seat_row.get("template", "?")), controller]
	turn_banner.add_theme_color_override("font_color", _owner_color(seat))
	hint_label.text = _hint_text(state)


## RETAIL'S HEADER NUMBERS, and only the ones that are real.
##
## The owner's screenshot reads "Player Bonuses 3000" across the top.
## `ScenarioStartResources = 3000` is retail's own field on every playable
## `LivingWorldPlayerTemplate`, and it is what this shows - labelled as the
## STARTING purse, because no treasury is simulated and a bare "3000" beside a
## turn counter would read as a live balance that goes up and down.
##
## The command-point pair is retail's own economy too: `MaxWorldCP = 4500` from
## the template, against the command points this seat actually has standing on
## the board, summed from the armies in the authoritative state.
##
## THERE IS NO PHASE BAR AND THERE WILL NOT BE ONE HERE.
## `data/ini/livingworldlogic.ini` is 192 bytes of comment, there is no
## `mprules.ini` anywhere in the archives, and retail's phase list lives in the
## executable. There is nothing to convert, so building one would be fabrication;
## it is named in the NOT CONVERTED line instead.
func _refresh_header(state: StateScript, seat: int, seat_row: Dictionary) -> void:
	if header_label == null:
		return
	if seat == StateScript.NEUTRAL:
		header_label.text = ""
		return
	var template: Dictionary = session.world.player_templates.get(
		String(seat_row.get("template", "")), {}) as Dictionary
	var purse := int(template.get("scenario_start_resources", -1))
	var max_cp := int(template.get("max_world_cp", -1))
	var on_board := 0
	for army_id in state.armies.keys():
		var army := state.armies[army_id] as Dictionary
		if int(army.get("owner", StateScript.NEUTRAL)) == seat:
			on_board += int(army.get("command_points", 0))
	var parts: Array[String] = []
	parts.append("TREASURE %s" % (str(purse) if purse >= 0 else "not authored"))
	parts.append("COMMAND POINTS %d/%s" % [on_board, str(max_cp) if max_cp >= 0 else "?"])
	header_label.text = "   ".join(parts)
	header_label.tooltip_text = (
		"TREASURE is retail's ScenarioStartResources for this seat's "
		+ "LivingWorldPlayerTemplate - the STARTING purse. No treasury is "
		+ "simulated, so it does not move.\nCOMMAND POINTS is the total carried by "
		+ "this seat's armies in the authoritative state, against retail's "
		+ "MaxWorldCP for the template.\nThere is no turn-phase bar: retail's phase "
		+ "list is hardcoded in the executable and livingworldlogic.ini ships empty.")


# --- the army banners, the plots and the build menu ---------------------------

## One row per army stack, keyed by region, with retail's own portrait id.
##
## THE PORTRAIT LINK IS RETAIL'S, never a resemblance: the recruit button that
## builds that same `PlayerArmy`, failing that the one for that same
## `HeroTemplateName`, failing that the owning template's
## `GarrisonSelectionPortraitName`. All three are authored fields. An army none
## of them reaches carries no portrait id at all and the map draws a bare plate.
func _army_stacks_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if session == null or session.state == null:
		return by_region
	var army_ids: Array[int] = []
	for key in session.state.armies.keys():
		army_ids.append(int(key))
	army_ids.sort()
	for army_id in army_ids:
		var army := session.state.armies[army_id] as Dictionary
		var region_id := String(army.get("region", ""))
		if region_id.is_empty():
			continue
		var owner := int(army.get("owner", StateScript.NEUTRAL))
		var template := ""
		if owner >= 0 and owner < session.state.players.size():
			template = String((session.state.players[owner] as Dictionary).get("template", ""))
		var roster := String(army.get("roster", ""))
		var portrait: Dictionary = {"id": "", "source": ""}
		var marker: Dictionary = {"icon": "", "size": "", "source": ""}
		if ui != null:
			portrait = ui.army_portrait(roster, String(army.get("hero_template", "")), template)
			# THE 3D MARKER FAMILY, by the same discipline as the portrait: the
			# `ArmyToSpawn` block that recruits this same `PlayerArmy`, failing
			# that the seat template's own `DefaultArmyIconName`. Both authored.
			marker = ui.army_marker(roster, template)
		var stacks: Array = by_region.get(region_id, []) as Array
		stacks.append({
			"owner": owner,
			"template": template,
			"kind": String(army.get("kind", "")),
			"label": _army_label(roster),
			"portrait_id": String(portrait.get("id", "")),
			"portrait_source": String(portrait.get("source", "")),
			"icon": String(marker.get("icon", "")),
			"size": String(marker.get("size", "")),
			"icon_source": String(marker.get("source", "")),
		})
		by_region[region_id] = stacks
	return by_region


## The name an army stack is shown under: retail's own `DisplayNameTag` through
## the string table, or the roster id when the table does not carry it. Never
## derived from the id.
func _army_label(roster: String) -> String:
	if session == null or session.world == null:
		return roster
	var record: Dictionary = session.world.player_armies.get(roster, {}) as Dictionary
	var key := String(record.get("display_name_tag", ""))
	if key.is_empty() or strings == null:
		return roster
	var text := strings.text(key)
	return text if not text.is_empty() else roster


## Retail's own authored `BuildingSpot` points per region, as map coordinates.
## Nothing is placed here - every point is one retail wrote down.
func _plots_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if session == null or session.world == null:
		return by_region
	for region_id in session.world.region_ids:
		var spots: Array = session.world.region(String(region_id)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		var points: Array[Vector2] = []
		for spot in spots:
			var row := spot as Dictionary
			points.append(Vector2(float(row.get("x", 0)), float(row.get("y", 0))))
		by_region[String(region_id)] = points
	return by_region


## The `LivingWorldBuildPlotIcon` family retail decals each region's plots with -
## the OWNING SEAT's own `BuildPlotIconName`, which is why an unowned region gets
## none rather than a default. Authored link only.
func _plot_icons_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if ui == null or session == null or session.state == null:
		return by_region
	for region_id in session.world.region_ids:
		var id := String(region_id)
		var owner := session.state.owner_of(id)
		if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
			continue
		var template := String((session.state.players[owner] as Dictionary).get("template", ""))
		var family := ui.build_plot_icon_id(template)
		if not family.is_empty():
			by_region[id] = family
	return by_region


func _display_names() -> Dictionary:
	var labels: Dictionary = {}
	for row in _rows:
		var region_id := String(row["id"])
		labels[region_id] = _display_of(region_id)
	return labels


## WHAT RETAIL OFFERS ON THE OPEN PLOT: every `LivingWorldBuilding` retail marks
## `AvailableTo` the owning seat's template, with retail's own
## `ConstructButtonImage`, `ConstructButtonTitle` and `StrategicResourceCost`.
##
## NOTHING HERE BUILDS. Construction is not in the simulation and this screen
## says so beside the ring rather than letting a clickable icon imply otherwise.
func _radial_entries() -> Array[Dictionary]:
	var entries: Array[Dictionary] = []
	if ui == null or selected_plot.is_empty() or session == null or session.state == null:
		return entries
	var region_id := String(selected_plot.get("region", ""))
	var owner := session.state.owner_of(region_id)
	if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
		return entries
	var template := String((session.state.players[owner] as Dictionary).get("template", ""))
	for row in ui.buildings_for(template):
		entries.append({
			"id": String(row.get("id", "")),
			"image_id": String(row.get("constructButtonImage", "")),
			"title": _string_or_key(String(row.get("constructButtonTitle", ""))),
			"cost": _building_cost(String(row.get("strategicResourceCost", ""))),
			"turns": String(row.get("turnsToBuild", "")),
		})
	return entries


## A building's cost in retail's own numbers. Retail authors it as a macro
## (`StrategicResourceCost = WOTR_FORTRESS_COST`), so it goes through the same
## gamedata `#define` table the region bonuses do - and an unresolved macro is
## shown BY NAME rather than filled in with a number nobody read.
func _building_cost(raw: String) -> String:
	if raw.is_empty():
		return ""
	if raw.is_valid_int():
		return raw
	if macros == null:
		return "%s (no #define table)" % raw
	var resolved: Dictionary = macros.resolve(raw)
	if bool(resolved.get("ok", false)):
		return str(int(float(resolved["value"])))
	return "%s UNRESOLVED" % raw


## Open or close the radial build menu on one plot. PRESENTATION ONLY: this
## writes a field on the screen, nothing else, and reaches no simulation state.
func _on_plot_clicked(region_id: String, index: int) -> void:
	if selected_plot.get("region", "") == region_id and int(selected_plot.get("index", -1)) == index:
		selected_plot = {}
	else:
		selected_plot = {"region": region_id, "index": index}
	refresh()


## What clicking will do, spelled out. The click rule is genuinely two-sided -
## stage, then attack or march - and a map that does not say so is a map you have
## to learn by making mistakes on.
func _hint_text(state: StateScript) -> String:
	if not state.pending_battle.is_empty():
		return "A battle for %s is in flight. Resolve it before anything else moves." % _display_of(
			String(state.pending_battle.get("region", "")))
	if session.selected_region.is_empty():
		if _staging.is_empty():
			return "This seat has no army standing in a region it owns, so nothing can be staged. END TURN passes play on."
		return "Click one of your %d armed regions (gold-ringed) to stage from." % _staging.size()
	var parts: Array[String] = []
	parts.append("Staged at %s." % _display_of(session.selected_region))
	if not _targets.is_empty():
		parts.append("Click a RED-ringed region to pick it as the attack target (%d offered)." % _targets.size())
	if not _moves.is_empty():
		parts.append("Click a PALE-ringed region to march there (%d reachable)." % _moves.size())
	if _targets.is_empty() and _moves.is_empty():
		parts.append("Nothing adjacent can be attacked or marched to from here.")
	if not session.selected_target.is_empty():
		parts.append("Target %s chosen - press ATTACK to commit." % _display_of(session.selected_target))
	return "  ".join(parts)


## Why ATTACK is greyed out, in the tooltip, so a disabled button is never a
## dead end the player has to guess at.
func _attack_button_reason() -> String:
	if session == null or session.state == null:
		return "There is no strategic session."
	if not session.state.pending_battle.is_empty():
		return "A battle is already in flight."
	if session.selected_region.is_empty():
		return "Stage from one of your own armed regions first."
	if session.selected_target.is_empty():
		return "Choose an adjacent region to attack."
	if not Array(_targets).has(session.selected_target):
		return "%s cannot be attacked from %s." % [
			_display_of(session.selected_target), _display_of(session.selected_region)]
	return "Commit the attack on %s. This is the only path from this screen to a battle." % _display_of(
		session.selected_target)


## EVERY SEAT'S STANDING, from the authoritative state and nothing else: regions
## held, armies standing, command points on the board, and the starting world and
## hero command points the document authored for the template. Read-only - this
## panel computes from `state` and writes nothing back.
## Why AUTO-RESOLVE cannot be pressed, or what it will do. The loader's own
## reason verbatim when the data is missing, because "unavailable" sends nobody
## anywhere and the reason names every path that was searched.
func _auto_resolve_button_reason(state: StateScript) -> String:
	if session.autoresolve == null or session.autoresolve_bindings == null:
		return (session.auto_resolve_reason if not session.auto_resolve_reason.is_empty()
			else "retail's auto-resolve tables have not been loaded for this session")
	if String(state.battle_type) == StateScript.BATTLE_TYPE_RTS:
		return ("this campaign's RULES tab is set to RTS, so every battle is fought in the "
			+ "tactical layer and there is nothing to auto-resolve")
	if not can_attack_now():
		return _attack_button_reason()
	if not session.auto_resolve_unbound_templates.is_empty():
		return ("Decide this battle with retail's auto-resolve tables and this project's dice. "
			+ "%d unit template(s) in this campaign have no auto-resolve data in any retail "
			+ "object file and will not fight: %s") % [
			session.auto_resolve_unbound_templates.size(),
			", ".join(Array(session.auto_resolve_unbound_templates))]
	return "Decide this battle with retail's auto-resolve tables and this project's dice."


func _refresh_standings(state: StateScript) -> void:
	var lines: Array[String] = []
	var active := state.active_player()
	lines.append("[color=#e1c77d]SEATS[/color]")
	var claimed := 0
	for index in range(state.players.size()):
		var seat_row := state.players[index] as Dictionary
		var regions := state.regions_owned_by(index)
		claimed += regions.size()
		var army_count := 0
		var command_points := 0
		var heroes := 0
		for army_id in state.armies.keys():
			var army := state.armies[army_id] as Dictionary
			if int(army.get("owner", StateScript.NEUTRAL)) != index:
				continue
			army_count += 1
			command_points += int(army.get("command_points", 0))
			if String(army.get("kind", "")) == StateScript.ARMY_HERO:
				heroes += 1
		var marker := ">" if index == active else " "
		var color := _owner_color(index)
		lines.append("%s [color=#%s]%s[/color]  %s%s" % [
			marker, color.to_html(false), String(seat_row.get("template", "?")),
			String(seat_row.get("controller", "?")),
			"  [color=#c8483f]DEFEATED[/color]" if bool(seat_row.get("defeated", false)) else ""])
		lines.append("    regions %d   armies %d (%d hero)   CP on the board %d" % [
			regions.size(), army_count, heroes, command_points])
	var neutral := session.world.region_ids.size() - claimed
	lines.append("  [color=#a9b39a]unclaimed  regions %d[/color]" % neutral)
	lines.append("")
	# NOT DRAWN, and named. Retail's own strategic shell is a Flash movie this
	# project does not read, and the region names are string-table keys with no
	# converted table behind them, so regions carry retail's own ids.
	# NOT DRAWN, and named. Kept current: the string table and the region
	# territory shapes HAVE now been converted, so claiming they have not would
	# be its own dishonesty - but the ~25 strategic APT movies, the army banners
	# carrying unit portraits, the radial build menu and the turn-phase bar are
	# all still absent, and that is what this line is for.
	var absent: Array[String] = []
	if strings == null:
		absent.append("retail's LW: string table (regions carry retail ids)")
	if region_geometry == null:
		absent.append("retail's region territory shapes (regions are markers, not filled territories)")
	absent.append("retail's ~25 strategic APT movies (StrategicHUD, StrategicPalantir, StrategicDetails*, the radial build menu host) - none is read")
	if ui == null:
		absent.append("army banner portraits and the build menu (retail's MappedImage atlases are not converted)")
	else:
		# CONVERTED, so not claimed absent - but what is still missing INSIDE them
		# is named, because a half-converted surface reported as done is the same
		# defect as one reported as absent.
		absent.append("construction itself - the build ring shows retail's real offer and changes no state, because no building system exists in the simulation")
	# THE MARKER MODELS, kept current the same way. They ARE converted now - all
	# 81 of them - so claiming they are not would be its own dishonesty; what is
	# still not on the map is the 28 structure families, and the reason is a
	# missing SIMULATION rather than a missing conversion.
	if markers == null:
		absent.append("the 3D marker models retail draws armies and plots with - the LivingWorldArmyIcon banners, the LivingWorldBuildingIcon structures and the LivingWorldBuildPlotIcon foundation decals are all in the archives and NONE is converted here; the map draws flat plates and rings in their place")
	else:
		absent.append("retail's structure models standing on built plots - all %d LivingWorldBuildingIcon famil(ies) ARE converted and none is placed, because construction is not simulated and there is no structure on any plot to draw" % int((markers.totals.get("familiesByKind", {}) as Dictionary).get("building", 0)))
		absent.append("the marker ANIMATIONS - retail fades, glows and marches these models; every one here is standing still, which is a state this screen names rather than a motion it invents")
	absent.append("the turn-phase bar (retail's phase list is hardcoded in the executable; livingworldlogic.ini ships EMPTY, 192 bytes of comment, and there is no mprules.ini anywhere in the archives)")
	absent.append("army models marching between regions")
	lines.append("[color=#a9b39a]NOT CONVERTED, so not shown: %s.[/color]" % "; ".join(absent))
	standings_label.text = "\n".join(lines)


## The marker key. Drawn rather than written so the colours in it are the SAME
## values the map draws with - a legend that restates colours in prose can drift
## from the map it explains.
func _draw_legend() -> void:
	var font := get_theme_default_font()
	if font == null:
		return
	# CHIPS, NOT A WORD ROW. The old strip read `KEY: staged attackable selected`
	# in flat text, which is what a debug overlay looks like. Same information,
	# presented as the inset plates retail uses.
	var chips: Array = [
		["staged", Color(0.85, 0.92, 0.75, 0.9)],
		["attackable", Color("#c8483f")],
		["selected", ThemeScript.GOLD_BRIGHT],
		["target", Color("#e8623f")],
		["unclaimed", NEUTRAL_COLOR],
	]
	if session != null and session.state != null:
		for index in range(session.state.players.size()):
			var seat_row := session.state.players[index] as Dictionary
			chips.append([String(seat_row.get("template", "seat %d" % index)),
				_owner_color(index)])
	var x := 4.0
	var height := 24.0
	var y := (legend_label.size.y - height) * 0.5
	for entry in chips:
		var label := String((entry as Array)[0])
		var tint := (entry as Array)[1] as Color
		var width := 30.0 + font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
		var box := Rect2(Vector2(x, y), Vector2(width, height))
		ChromeScript.draw_chip(legend_label, box, tint)
		legend_label.draw_string(font, Vector2(x + 22.0, y + height * 0.5 + 4.5), label,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.TEXT_LEAF)
		x += width + 7.0
	# How to drive the camera. The owner asked for skirmish-style freedom; a
	# control nobody can find is not a control, so the bindings are on the screen.
	legend_label.draw_string(font, Vector2(x + 12.0, y + height * 0.5 + 4.5),
		"WHEEL zoom  |  RIGHT-DRAG pan  |  MIDDLE-DRAG or SHIFT+DRAG orbit  |  banner = an army  |  ring = a build plot",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.PARCHMENT_DIM)


## THE SHELL. Every rectangle here is hand-built in retail's language - warm
## brass rules, inset fields, corner studs, a title plate with a gold rule under
## it - and NONE of it is retail art. Retail's own frame images
## (`FrameT`/`FrameB`/`FrameL`/`FrameR`/`FrameCorner*`) and its title rules
## (`Ruler`, `MainMenuRuler`) all name textures that are IN NO ARCHIVE
## (`SCShellUserInterface512_001.tga`, `MainMenuRuleruserinterface.tga`), so
## there was nothing to convert and this project will not paint a picture and
## call it retail's. What IS retail art is on the map - the portraits, the
## faction standards, the radial ring - and the report line says which.
func _draw_chrome() -> void:
	var width := size.x if size.x > 0.0 else 1860.0
	var height := size.y if size.y > 0.0 else 1000.0
	chrome_layer.draw_rect(Rect2(Vector2.ZERO, Vector2(width, height)),
		Color(0.045, 0.042, 0.036, 1.0))
	# The title plate across the top, with the heading and the provenance in it.
	ChromeScript.draw_title_plate(chrome_layer, Rect2(16.0, 8.0, width - 32.0, 52.0))
	# The map, framed like a map table rather than floating in the dark.
	var map_rect := Rect2(map_view.position - Vector2(6.0, 6.0),
		map_view.size + Vector2(12.0, 12.0))
	ChromeScript.draw_panel(chrome_layer, map_rect)
	# The key strip and the report card under it.
	ChromeScript.draw_panel(chrome_layer,
		Rect2(legend_label.position - Vector2(6.0, 4.0),
			legend_label.size + Vector2(12.0, 8.0)), 1)
	ChromeScript.draw_panel(chrome_layer,
		Rect2(map_mode_label.position - Vector2(8.0, 22.0),
			map_mode_label.size + Vector2(16.0, 30.0)), 1)
	var font := get_theme_default_font()
	if font != null:
		chrome_layer.draw_string(font,
			map_mode_label.position + Vector2(2.0, -8.0),
			"CONVERSION REPORT - WHAT IS RETAIL'S, AND WHAT IS NOT",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, ThemeScript.GOLD)
	# The side column: the seat table, the region card, and the buttons.
	ChromeScript.draw_panel(chrome_layer,
		Rect2(standings_label.position - Vector2(10.0, 8.0),
			standings_label.size + Vector2(20.0, 16.0)))
	ChromeScript.draw_panel(chrome_layer,
		Rect2(detail_label.position - Vector2(10.0, 8.0),
			detail_label.size + Vector2(20.0, 16.0)))
	# The header's two numbers, each on its own recessed plate.
	if header_label != null and not header_label.text.is_empty():
		ChromeScript.draw_value_plate(chrome_layer,
			Rect2(header_label.position - Vector2(10.0, 3.0),
				Vector2(header_label.size.x + 20.0, header_label.size.y + 6.0)))


# --- detail panel ------------------------------------------------------------

## RETAIL'S REGION PANEL, in retail's own words.
##
## The screenshot the owner sent reads, over Mordor:
##
##     Mordor
##     +500 Treasure
##     3 Build Plots
##     Territory of Region: Mordor
##     Unified Region Bonus: Discount when Building Barracks Units
##
## Every one of those five lines is retail data, and none of it is written here:
##
##   * "Mordor" is `LW:DisplayNameMordor` from `data/lotr.str`.
##   * "+500 Treasure" is the format string `LW:RegionTreasuryBonus`
##     (`+%d Treasure`) filled from `FertileTerritoryBonus`, which retail authors
##     as the macro `FERTILE_TERRITORY_BONUS` and `gamedata.ini` defines as 500.
##   * "3 Build Plots" is `LW:NumberOfBuildPlotsPlural` (`\n%d Build Plots`)
##     filled from the count of `BuildingSpot` lines the region authors.
##   * "Territory of Region: %ls" is `LW:TerritoryPartOfRegion`, filled from the
##     territory whose member list contains this region.
##   * "Unified Region Bonus: %ls" is `LW:UnifiedRegionBonus`, filled from that
##     territory's own bonuses through the same formatter table.
##
## WHAT IS SHOWN WHEN A PIECE IS MISSING. A macro the `#define` table does not
## resolve prints its NAME and the word unresolved. A format string the table
## does not carry prints the KEY. A region in no territory says so. Nothing here
## substitutes a plausible number for one it could not read - which is the whole
## reason the macro table exists rather than a literal 500 in this file.
func _region_panel_lines(region_id: String) -> Array[String]:
	var lines: Array[String] = []
	if session == null or session.world == null:
		return lines
	var region := session.world.region(region_id)
	if region.is_empty():
		return lines

	lines.append("[color=#e1c77d]%s[/color]" % _display_of(region_id))
	if strings == null:
		lines.append("  [color=#a9b39a]retail's string table is not converted, so this is retail's region id, not its name[/color]")

	var bonuses := region.get("bonuses", {}) as Dictionary
	var macro_names := region.get("bonus_macros", {}) as Dictionary
	var printed := 0
	for field in BONUS_ORDER:
		var line := _format_bonus(field, bonuses, macro_names)
		if line.is_empty():
			continue
		lines.append("  %s" % line)
		printed += 1
	if printed == 0:
		var none := _string_or_key("LW:NoBonus")
		lines.append("  %s" % none)

	var plots := int(region.get("building_spot_count", 0))
	var plot_key := "LW:NumberOfBuildPlotsSingle" if plots == 1 else "LW:NumberOfBuildPlotsPlural"
	lines.append("  %s" % _fill_count(plot_key, plots))

	var territory := session.world.territory_of(region_id)
	if territory.is_empty():
		lines.append("  [color=#a9b39a]this region belongs to no territory group in this campaign[/color]")
	else:
		var territory_name := _string_or_key(String(territory.get("territory", "")))
		lines.append("  %s" % _fill_text("LW:TerritoryPartOfRegion", territory_name))
		var territory_bonuses := territory.get("bonuses", {}) as Dictionary
		var unified: Array[String] = []
		for field in BONUS_ORDER:
			var line := _format_bonus(field, territory_bonuses, {})
			if not line.is_empty():
				unified.append(line)
		if unified.is_empty():
			lines.append("  %s" % _fill_text("LW:UnifiedRegionBonus", _string_or_key("LW:NoBonus")))
		else:
			lines.append("  %s" % _fill_text("LW:UnifiedRegionBonus", ", ".join(unified)))
		# The retail id rides along with the label, because retail's table maps
		# more than one region onto the same English name (`Arnor` reads
		# "Arthedain", `Buckland` reads "The North Downs") and a member list with
		# the same word twice in it reads as a bug rather than as retail's own
		# text. Both are shown so neither claim is lost.
		lines.append("  [color=#a9b39a]territory members: %s[/color]" % ", ".join(
			Array(territory.get("regions", PackedStringArray())).map(func(id: Variant) -> String:
				var member := String(id)
				var label := _display_of(member)
				return label if label == member else "%s (%s)" % [label, member])))

	var cp_limit := int(region.get("cp_limit", -1))
	if cp_limit >= 0:
		lines.append("  [color=#a9b39a]command point limit %d (ally %d)[/color]" % [
			cp_limit, int(region.get("ally_cp_limit", -1))])
	return lines


## One bonus line in retail's own wording, or "" when the region does not carry
## that bonus at all. A MACRO the `#define` table cannot resolve is named rather
## than filled in with a number that was never read.
func _format_bonus(field: String, bonuses: Dictionary, macro_names: Dictionary) -> String:
	var key := String(BONUS_STRING_KEYS.get(field, ""))
	if key.is_empty():
		return ""
	var macro_name := String(macro_names.get(field, ""))
	if not macro_name.is_empty():
		var resolved: Dictionary = macros.resolve(macro_name) if macros != null else {"ok": false, "raw": ""}
		if bool(resolved.get("ok", false)):
			return _fill_count(key, int(float(resolved["value"])))
		var raw := String(resolved.get("raw", ""))
		return "[color=#c8483f]%s: %s UNRESOLVED%s[/color]" % [
			_string_or_key(key), macro_name,
			(" (retail's body is %s, which is an expression, not a number)" % raw) if not raw.is_empty() else
			" (no gamedata #define table is converted)"]
	var amount := int(bonuses.get(field, 0))
	if amount == 0:
		return ""
	return _fill_count(key, amount)


## Fill a retail format string that takes one number. Retail writes `%d` for a
## count and `%d%%` for a percentage; both are handled, and a string carrying
## neither is returned untouched rather than mangled.
func _fill_count(key: String, amount: int) -> String:
	var template := _string_or_key(key).replace("\\n", "").strip_edges()
	if template.contains("%d%%"):
		return template.replace("%d%%", "%d%%" % amount)
	if template.contains("%d"):
		return template.replace("%d", str(amount))
	return template


## Fill a retail format string that takes one string (`%ls`).
func _fill_text(key: String, value: String) -> String:
	var template := _string_or_key(key).replace("\\n", " ").strip_edges()
	if template.contains("%ls"):
		return template.replace("%ls", value)
	return "%s %s" % [template, value]


## Retail's text for a key, or the KEY ITSELF when the table does not carry it.
## Showing the key is deliberate: it is visibly not a name, so a missing string
## can never be mistaken for retail's wording.
func _string_or_key(key: String) -> String:
	if strings == null:
		return key
	var value := strings.text(key)
	return value if not value.is_empty() else key


## THE OPEN BUILD PLOT, with the two counters retail puts beside it.
##
## Retail's plot panel carries a plots counter and a command-point counter. Both
## are real here and both are read, not written: the plots figure is STRUCTURES
## STANDING over the region's own authored `BuildingSpot` count, and structures
## standing is zero because construction is not simulated - which is stated,
## not hidden behind a plausible number. The command-point figure is the sum the
## authoritative state carries for that region against the region's own
## authored `CommandPointLimit`.
func _build_plot_panel_lines() -> Array[String]:
	var lines: Array[String] = []
	if selected_plot.is_empty() or session == null or session.state == null:
		return lines
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var region := session.world.region(region_id)
	if region.is_empty():
		return lines
	var total := int(region.get("building_spot_count", 0))
	var owner := session.state.owner_of(region_id)
	lines.append("[color=#e1c77d]BUILD PLOT %d of %d - %s[/color]" % [
		index + 1, total, _display_of(region_id)])
	# "0/3": structures standing over authored plots. The numerator is zero and
	# says why, rather than being a number this screen made up.
	lines.append("  structures %d / %d plots   [color=#a9b39a](no structure is standing anywhere: construction is NOT simulated, so the numerator is zero by construction, not by measurement)[/color]" % [
		0, total])
	var cp_used := session.state.command_points_in_region(region_id, owner) if owner != StateScript.NEUTRAL else 0
	lines.append("  command points %d / %d   [color=#a9b39a](retail's own CommandPointLimit for this region)[/color]" % [
		cp_used, session.world.region_cp_limit(region_id)])
	var restrictions: Array = region.get("restrict_buildings", []) as Array
	for restriction in restrictions:
		var row := restriction as Dictionary
		lines.append("  [color=#a9b39a]retail restricts this region to %d x %s[/color]" % [
			int(row.get("numberAllowed", 0)),
			", ".join(Array(row.get("buildings", [])).map(func(v: Variant) -> String: return String(v)))])
	if ui == null:
		lines.append("  [color=#c8483f]no build menu: retail's UI bundle is not converted, so there are no structure icons to offer.[/color]")
		return lines
	if owner == StateScript.NEUTRAL:
		lines.append("  [color=#a9b39a]this region is unclaimed, and retail's structures are authored per faction (AvailableTo), so there is nothing to offer on it[/color]")
		return lines
	var entries := _radial_entries()
	lines.append("  [color=#a9b39a]%d structure(s) retail marks AvailableTo this seat, drawn as a ring around the plot. NOTHING HERE BUILDS - construction is not in the simulation, and no click on the ring changes any state.[/color]" % entries.size())
	var without_icon: Array[String] = []
	for entry in entries:
		if not ui.has_image(String(entry["image_id"])):
			without_icon.append("%s (%s)" % [String(entry["id"]), String(entry["image_id"])])
	if not without_icon.is_empty():
		lines.append("  [color=#c8483f]%d structure(s) have NO icon and are drawn as an empty slot: %s[/color]" % [
			without_icon.size(), ", ".join(without_icon)])
	lines.append("")
	return lines


## THE REGION THE CARD IS ABOUT, by the same precedence the card itself uses:
## the attack target, then whatever is under the pointer, then the selection.
func _card_region() -> String:
	if session == null:
		return ""
	if not session.selected_target.is_empty():
		return session.selected_target
	if not session.hover_region.is_empty():
		return session.hover_region
	return session.selected_region


## RETAIL'S OWN PORTRAIT OF THAT REGION, drawn at retail's own crop.
##
## The plate is ALWAYS drawn, even empty, so the card does not jump every time
## the pointer crosses a region retail authors no picture for - and an empty one
## SAYS WHY beside it. Retail names three fortress portraits it defines nowhere
## (`BPCAmonSul`, `BPCCarnDum`, `BPCFornost`); the nearest ids in the archives
## are `BPCFornostGate` and `BPCFornostCitadel`, which are different pictures of
## different things, and none of the three is bridged to them.
func _draw_region_portrait() -> void:
	var frame := region_portrait_frame
	var height := frame.size.y
	var plate := Rect2(Vector2.ZERO, Vector2(height * 4.0 / 3.0, height))
	frame.draw_rect(plate.grow(2.0), Color(0.05, 0.06, 0.05, 0.92))
	var region_id := _card_region()
	var found: Dictionary = {"texture": null, "id": "", "requested": "", "source": "", "reason": ""}
	if region_images != null and not region_id.is_empty():
		found = region_images.region_portrait(region_id)
	var texture: Texture2D = found["texture"]
	if texture != null:
		frame.draw_texture_rect(texture, plate, false)
	else:
		# NOT A STAND-IN PICTURE: a flat plate that is visibly not retail art.
		frame.draw_rect(plate, Color(0.13, 0.14, 0.11, 0.85))
	frame.draw_rect(plate, ThemeScript.GOLD, false, 1.0)
	var caption := ""
	if region_id.is_empty():
		caption = "no region is selected or under the pointer"
	elif region_images == null:
		caption = "NO PORTRAIT BUNDLE: %s" % region_images_reason.split(".")[0]
	elif texture != null:
		caption = "%s\n[%s = %s]" % [_display_of(region_id), String(found["source"]), String(found["id"])]
	elif not String(found["requested"]).is_empty():
		caption = "%s\nNO PICTURE: %s" % [_display_of(region_id), String(found["reason"])]
	else:
		caption = "%s\nretail's data names no portrait for this region" % _display_of(region_id)
	region_portrait_caption.text = caption


func _refresh_detail() -> void:
	if region_portrait_frame != null:
		region_portrait_frame.queue_redraw()
	var lines: Array[String] = []
	var state: StateScript = session.state
	# THE REGION PANEL FIRST. Retail's strategic screen leads with the region the
	# player is pointing at, and until now this screen led with a list.
	var focus := session.selected_target
	if focus.is_empty():
		focus = session.hover_region
	if focus.is_empty():
		focus = session.selected_region
	# THE OPEN PLOT FIRST when there is one. The detail panel scrolls, and a plot
	# panel written under the region panel was below the fold the moment it was
	# opened - which is the same as not writing it.
	lines.append_array(_build_plot_panel_lines())
	if not focus.is_empty() and _row_by_id.has(focus):
		lines.append_array(_region_panel_lines(focus))
		lines.append("")
	lines.append("[color=#e1c77d]YOUR REGIONS WITH AN ARMY[/color]")
	if _staging.is_empty():
		lines.append("  none - this seat has no army standing in a region it owns")
	for region_id in _staging:
		var row := _row_by_id[region_id] as Dictionary
		lines.append("  %s   armies %d   CP %d" % [_display_of(region_id), int(row["armies"]), int(row["command_points"])])
	lines.append("")
	if session.selected_region.is_empty():
		lines.append("Select one of your regions to see what it can attack.")
	else:
		lines.append("[color=#e1c77d]ATTACKING FROM %s[/color]" % _display_of(session.selected_region))
		if _targets.is_empty():
			lines.append("  no adjacent region can be attacked from here")
		if not _moves.is_empty():
			lines.append("  march to: %s" % ", ".join(Array(_moves)))
		for target in _targets:
			var marker := ">" if target == session.selected_target else " "
			var row := _row_by_id[target] as Dictionary
			lines.append("%s %s   owner %s   armies %d" % [
				marker, _display_of(target), _owner_name(int(row["owner"])), int(row["armies"])])
	lines.append("")
	if not session.selected_target.is_empty():
		var target_row := _row_by_id[session.selected_target] as Dictionary
		var region_map := String(target_row["map_name"])
		var bindings := session.battlefield_bindings(available_map_ids)
		var battlefield := String(bindings.get(region_map, ""))
		lines.append("[color=#e1c77d]BATTLE[/color]")
		lines.append("  region map   %s" % region_map)
		if battlefield.is_empty():
			lines.append("  [color=#c8483f]battlefield   NONE - no pack map is available to stand in for it, so this attack cannot be fought[/color]")
		else:
			lines.append("  battlefield  %s" % battlefield)
			lines.append("  [color=#a9b39a]STAND-IN: no MAP WOR region map is cooked in any content pack, so this battle is fought on a converted skirmish map instead. The battlefield is recorded in the commitment.[/color]")
	if not state.pending_battle.is_empty():
		lines.append("")
		lines.append("[color=#c8483f]A battle for %s is in flight.[/color]" % _display_of(String(state.pending_battle.get("region", ""))))
	detail_label.text = "\n".join(lines)


func _rebuild_unplaced() -> void:
	_clear_unplaced()
	var unplaced: Array[Dictionary] = []
	# A region with no authored centre point that the map DID place from a
	# centroid derived off retail's own fill triangles is ON the map, and listing
	# it here as absent would contradict the mode line two lines below it. That
	# contradiction shipped in the first frame this lane captured.
	var placed_from_geometry := PackedStringArray()
	if map3d != null and map3d.has_map():
		placed_from_geometry = map3d.centroid_placed_regions
	for row in _rows:
		if bool(row["has_position"]):
			continue
		if Array(placed_from_geometry).has(String(row["id"])):
			continue
		unplaced.append(row)
	if unplaced.is_empty():
		unplaced_label.text = ""
		return
	# NOT drawn on the map, and said so. Retail falls back to the region's own
	# sub-object when `CustomCenterPoint` is absent - and `livingmap.w3d` does NOT
	# carry per-region sub-objects. Its 64 sub-objects are 20 terrain tiles, the
	# coast and water, the impassable volumes, the ambient cards and eleven named
	# landmarks; there is no `Rhun` mesh in it to take a centre from. So the
	# position genuinely does not exist in the converted map, and a coordinate
	# chosen here would be invented map data.
	# Retail falls back to the region's own sub-object when `CustomCenterPoint`
	# is absent. `livingmap.w3d` carries no per-region mesh - but `lmr_fill.w3d`
	# does, and when it is converted the centroid of retail's own triangles
	# places these regions. This list is therefore only the regions that have
	# NEITHER, and it shrinks to nothing once the region bundle is present.
	unplaced_label.text = ("NOT ON THE MAP (%d): no authored centre point, and no region fill mesh "
		+ "to take a centroid from either.") % unplaced.size()
	for row in unplaced:
		var button := Button.new()
		var region_id := String(row["id"])
		button.name = "Unplaced_%s" % region_id
		button.text = "%s   (%s)" % [_display_of(region_id), _owner_name(int(row["owner"]))]
		button.custom_minimum_size = Vector2(0, 26)
		button.pressed.connect(_on_region_clicked.bind(region_id))
		unplaced_host.add_child(button)


func _clear_unplaced() -> void:
	if unplaced_host == null:
		return
	for child in unplaced_host.get_children():
		unplaced_host.remove_child(child)
		child.queue_free()


# --- the map -----------------------------------------------------------------

func _draw_map() -> void:
	var size := map_view.size
	if session == null or session.state == null:
		map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
		_draw_fallback_banner(size)
		return
	_screen_positions = _compute_screen_positions(size)
	map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
	_draw_fallback_banner(size)
	# Edges first, so markers sit on top of them.
	for region_id in session.world.region_ids:
		if not _screen_positions.has(region_id):
			continue
		for neighbour in session.world.neighbours(region_id):
			if not _screen_positions.has(neighbour):
				continue
			if String(neighbour) < String(region_id):
				continue
			map_view.draw_line(
				_screen_positions[region_id], _screen_positions[neighbour],
				Color(0.35, 0.45, 0.32, 0.45), 1.5)
	var font := get_theme_default_font()
	for row in _rows:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var color := _owner_color(int(row["owner"]))
		var radius := MARKER_RADIUS + (2.0 if int(row["armies"]) > 0 else 0.0)
		map_view.draw_circle(point, radius, color)
		map_view.draw_arc(point, radius, 0.0, TAU, 24, Color(0.05, 0.09, 0.05, 0.9), 2.0)
		if region_id == session.selected_region:
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, ThemeScript.GOLD_BRIGHT, 3.0)
		elif Array(_targets).has(region_id):
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, Color("#c8483f"), 2.0)
		elif Array(_staging).has(region_id):
			map_view.draw_arc(point, radius + 4.0, 0.0, TAU, 28, Color(0.85, 0.92, 0.75, 0.55), 1.5)
		if region_id == session.selected_target:
			map_view.draw_arc(point, radius + 10.0, 0.0, TAU, 28, Color("#e8623f"), 3.0)
		if font != null:
			var label := "%s%s" % [region_id, "  x%d" % int(row["armies"]) if int(row["armies"]) > 0 else ""]
			map_view.draw_string(font, point + Vector2(radius + 5.0, 4.0), label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.TEXT_LEAF)


## WHY THE PLAYER IS LOOKING AT A DIAGRAM INSTEAD OF MIDDLE-EARTH, drawn on the
## thing itself. The flat graph already called itself a fallback in a 13px line
## under the map; that was not enough for the owner to notice, let alone act on.
## The reason is the loader's own multi-line refusal - every path it looked at,
## where each came from, and the command that produces a bundle.
func _draw_fallback_banner(size: Vector2) -> void:
	if map_reason.is_empty():
		return
	var font := get_theme_default_font()
	if font == null:
		return
	var banner := Rect2(Vector2.ZERO, Vector2(size.x, FALLBACK_BANNER_HEIGHT))
	map_view.draw_rect(banner, Color(0.16, 0.06, 0.05, 0.92))
	map_view.draw_line(
		Vector2(0.0, FALLBACK_BANNER_HEIGHT), Vector2(size.x, FALLBACK_BANNER_HEIGHT),
		Color("#c8483f"), 2.0)
	map_view.draw_string(
		font, Vector2(18.0, 26.0),
		"FLAT 2D REGION GRAPH (FALLBACK) - THIS IS NOT RETAIL'S MAP",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color("#e8623f"))
	map_view.draw_multiline_string(
		font, Vector2(18.0, 48.0), map_reason, HORIZONTAL_ALIGNMENT_LEFT,
		size.x - 36.0, 13, FALLBACK_BANNER_LINES, ThemeScript.PARCHMENT_DIM)


## Authored region coordinates scaled into the view. Pure presentation: the
## transform is derived from the authored extent every frame and reaches nothing
## but the drawing. Regions without an authored point are ABSENT from the result
## rather than defaulted into a corner.
func _compute_screen_positions(size: Vector2) -> Dictionary:
	var placed: Array[Dictionary] = []
	for row in _rows:
		if bool(row["has_position"]):
			placed.append(row)
	var positions: Dictionary = {}
	if placed.is_empty():
		return positions
	var minimum := (placed[0]["position"] as Vector2)
	var maximum := minimum
	for row in placed:
		var point := row["position"] as Vector2
		minimum = Vector2(minf(minimum.x, point.x), minf(minimum.y, point.y))
		maximum = Vector2(maxf(maximum.x, point.x), maxf(maximum.y, point.y))
	var span := maximum - minimum
	# The fallback banner owns the top of the view when it is up, so the graph is
	# fitted into what is left. A marker under an explanation of why the marker is
	# there instead of a map would be its own small dishonesty.
	var banner := FALLBACK_BANNER_HEIGHT if not map_reason.is_empty() else 0.0
	var usable := size - Vector2(MAP_INSET * 2.0, MAP_INSET * 2.0 + banner)
	var scale_x := usable.x / span.x if span.x > 0.0 else 1.0
	var scale_y := usable.y / span.y if span.y > 0.0 else 1.0
	var factor := minf(scale_x, scale_y)
	for row in placed:
		var point := row["position"] as Vector2
		var local := (point - minimum) * factor
		# Retail's strategic Y grows northward; screen Y grows down.
		positions[String(row["id"])] = Vector2(
			MAP_INSET + local.x,
			size.y - MAP_INSET - local.y)
	return positions


func _on_map_input(event: InputEvent) -> void:
	if session == null or session.state == null:
		return
	if event is InputEventMouseMotion:
		var hovered := _region_at(event.position)
		if hovered != session.hover_region:
			session.hover_region = hovered
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	if not button.pressed or button.button_index != MOUSE_BUTTON_LEFT:
		return
	var region_id := _region_at(button.position)
	if region_id.is_empty():
		return
	_on_region_clicked(region_id)


## Hover from the 3D map. Presentation only: it lands on the session's per-seat
## presentation field, which `authoritative_state()` does not read and no hash
## covers.
func _on_region_hovered(region_id: String) -> void:
	if session == null:
		return
	if session.hover_region == region_id:
		return
	session.hover_region = region_id
	# The region panel follows the pointer, the way retail's does. Presentation
	# only: `hover_region` is a presentation field no hash covers.
	_refresh_detail()


## A click means "stage here" on one of your own armed regions and "attack here"
## on anything the staged region can reach. Deterministic and total: the target
## reading is tried first only when a staging region is already chosen, so a
## region that is both never depends on click order.
func _on_region_clicked(region_id: String) -> void:
	if not session.selected_region.is_empty():
		# The two readings are DISJOINT by construction - an attack target is a
		# region this seat does not own and a march target is one it does - so the
		# order below is a statement of that fact rather than a tie-break.
		if Array(_targets).has(region_id):
			select_target(region_id)
			return
		if Array(_moves).has(region_id):
			move_to(region_id)
			return
	select_region(region_id)


## March the staged region's armies into an adjacent region this seat owns. This
## is a STRATEGIC COMMAND, not a selection: it changes the authoritative state
## and therefore the hash, which is exactly what it should do - retail seats both
## sides deep in their own territory, so without it no attack is ever reachable.
func move_to(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to march from first.")
		return false
	var from_region := session.selected_region
	var result: Dictionary = session.move_armies(from_region, region_id)
	if not bool(result.get("ok", false)):
		_message("March refused: %s" % ", ".join(Array(result.get("refusals", PackedStringArray()))))
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	_message("Marched %d army/armies from %s to %s." % [
		(result["moved"] as PackedInt32Array).size(), _display_of(from_region), _display_of(region_id)])
	refresh()
	return true


func _region_at(point: Vector2) -> String:
	var best := ""
	var best_distance := MARKER_RADIUS + 6.0
	var ids: Array[String] = []
	for key in _screen_positions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var distance := (point - (_screen_positions[region_id] as Vector2)).length()
		if distance <= best_distance:
			best = region_id
			best_distance = distance
	return best


# --- internals ---------------------------------------------------------------

func _on_attack_pressed() -> void:
	commit_selected_attack()


func _on_end_turn_pressed() -> void:
	end_turn()


func _message(text: String) -> void:
	if message_label != null:
		message_label.text = text


## The label a region is shown under. The document's `displayName` is a STRING
## TABLE KEY (`LW:DisplayNameArnor`), not a name. When retail's table has been
## converted this is RETAIL'S OWN ENGLISH TEXT, verbatim - including the places
## where retail's key and its text disagree (`LW:DisplayNameArnor` reads
## "Arthedain", `Buckland` reads "The North Downs"), because those are retail's
## words and not this project's to correct.
##
## With no table, or with no entry for this region's key, the label falls back to
## retail's own region id. It never derives a name from the id: retail's own
## disagreements above are the proof that such a derivation would be fiction.
func _display_of(region_id: String) -> String:
	if strings == null or session == null or session.world == null:
		return region_id
	var key := String(session.world.region(region_id).get("display_name", ""))
	var label := strings.text(key)
	return label if not label.is_empty() else region_id


func _owner_name(owner: int) -> String:
	if session == null or session.state == null:
		return "?"
	if owner == StateScript.NEUTRAL:
		return "unclaimed"
	if owner < 0 or owner >= session.state.players.size():
		return "?"
	return String((session.state.players[owner] as Dictionary).get("template", "seat %d" % owner))


func _owner_color(owner: int) -> Color:
	if owner == StateScript.NEUTRAL or owner < 0:
		return NEUTRAL_COLOR
	return SEAT_COLORS[owner % SEAT_COLORS.size()]
