extends Panel

## THE WAR OF THE RING GAME SETUP SCREEN.
##
## Until this screen existed, WAR OF THE RING dropped straight into a running
## campaign: the seats were the first two convertible factions in sorted order
## and the scenario was the first startable one, both chosen by
## `main_menu._start_wotr_session()` with a comment admitting it. That was an
## honest stated limit and it is now lifted - THE PLAYER CHOOSES, and the
## session still decides.
##
## WHAT IT IS. Retail's two-tab setup screen: a MAP tab carrying the scenario
## picker, retail's own scenario description and a territory-shaded preview of
## Middle-earth; a RULES tab carrying retail's five rule rows and two
## checkboxes; and, under both, the seat table - one row per player, with Army,
## Hero, Team, Colour and Handicap.
##
## ---------------------------------------------------------------------------
## THE FOUR RULES IT KEEPS
## ---------------------------------------------------------------------------
##
## 1. NOTHING ON IT IS INVENTED. Every label is a retail string-table key
##    resolved through `wotr_setup_strings.gd` (the shell namespaces) or
##    `wotr_strings.gd` (the strategic ones). A key the table does not carry
##    shows AS THE KEY and is counted in `unresolved_keys`, which the screen
##    prints and a runner asserts BY NAME. A dropdown whose options are not in
##    any shipped file is drawn UNAVAILABLE carrying the reason - it never gets
##    invented options. The clearest case is the tactical phase timer: retail's
##    `VALUE:NoTimer` resolves, retail's numeric second counts live only in the
##    executable, and this screen offers "No Timer" and nothing else.
##
## 2. THE FRAME IS HAND-BUILT AND SAYS SO. `wotr_chrome.gd`'s blue-steel half
##    draws every panel, tab, glass inset, dropdown plate, checkbox, swatch and
##    button here, because retail's frame art is a nine-slice naming three .tga
##    files that are in no archive. The territory SHAPES on the map preview are
##    retail's own `lmr_fill.w3d` / `lmr_border.w3d` triangles, unmodified.
##
## 3. WHAT IS CHOSEN HERE REACHES THE SESSION, NOT A PARALLEL PATH. PLAY emits
##    `play_requested` carrying scenario + seats, and the menu hands those to
##    `WotrSession.begin()` - the same call the old fixed seating used. The
##    session still refuses a missing document, still refuses fewer than two
##    fieldable factions, and still refuses a scenario with no authored
##    ownership. Setup CHOOSES; the session DECIDES.
##
## 4. NOTHING CHOSEN HERE SNEAKS PAST THE COMMITMENT. Scenario, army and team
##    are arguments to `begin()`, so they are inside the strategic state before
##    turn one and ride every hash after it. Colour is presentation and is
##    dropped by `handoff_payload()`. EVERYTHING ELSE - the rule rows, the
##    handicaps, the AI tier - has no carrier in the strategic layer, so it is
##    drawn LOCKED with its reason rather than wired in as an extra argument to
##    a battle. That desync has been fixed twice on this branch and this screen
##    does not reopen it.

signal back_requested
## PLAY. Carries `{scenario, seats}` where `seats` is the array
## `WotrSession.begin()` takes: `{template, team, controller}` per seat.
signal play_requested(setup: Dictionary)

const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")
const BindingsScript = preload("res://src/wotr/wotr_setup_bindings.gd")
const SetupStringsScript = preload("res://src/wotr/wotr_setup_strings.gd")
const StringsScript = preload("res://src/wotr/wotr_strings.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const WorldScript = preload("res://src/wotr/wotr_world.gd")
const MapPreviewScript = preload("res://src/ui/wotr_setup_map_preview.gd")

const TAB_MAP := 0
const TAB_RULES := 1

const MIN_SEATS := 2
const MAX_SEATS := 6

const TITLE_FONT := 26
const HEADING_FONT := 19
const LABEL_FONT := 14
const BODY_FONT := 13
const SMALL_FONT := 11

const TEXT := Color("#cfdcea")
const TEXT_BRIGHT := Color("#eaf3ff")
const TEXT_DIM := Color("#7d8ea1")
const TEXT_LOCKED := Color("#66798d")
const TEXT_WARN := Color("#e0b070")

## Which tab is showing. Presentation.
var active_tab := TAB_MAP

## The living-world document, verbatim, and the world read from it. Read-only
## here: this screen never mutates either, and never starts a session.
var document: Dictionary = {}
var world: WorldScript = null

## `{template, pack_faction, unavailable_reason}` per seatable player template,
## in the session's own sorted order.
var seat_options: Array[Dictionary] = []
## The chosen seating. One entry per row:
## `{option_index, team, controller, color_slot, handicap_index}`.
var seats: Array[Dictionary] = []

## Rule row id -> the option index the player picked. A row absent from here has
## never been touched and shows its own authored default; the table stays the
## single source of what the defaults ARE.
var rule_choice: Dictionary = {}
## Scenario rows: `{name, display, description, startable, reason, max_players}`.
var scenarios: Array[Dictionary] = []
var scenario_index := 0

## Why setup cannot proceed at all, or "". Drawn in place of everything.
var unavailable_reason := ""
## The last message from the menu or a refusal, drawn on the bottom bar.
var message := ""

## Retail keys asked for that no bundle carried, sorted. PUBLIC and asserted BY
## NAME rather than by a count tolerance.
var unresolved_keys: PackedStringArray = PackedStringArray()
## One line per thing retail has that this screen does not, for the log and the
## report. Never a number on its own.
var absences: PackedStringArray = PackedStringArray()

var setup_strings: SetupStringsScript = null
var setup_strings_reason := ""
var strings: StringsScript = null
var strings_reason := ""
var geometry: RegionGeometryScript = null
var geometry_reason := ""

var map_preview: Control = null
var _territory_text := ""
var _hover_region := ""

## Hit rectangles, rebuilt every layout. `{id, rect, kind, ...}`.
var _hits: Array[Dictionary] = []
## The open dropdown's id, or "".
var _open_menu := ""
var _open_rect := Rect2()
var _open_options: Array[Dictionary] = []
var _open_selected := 0
## First visible row of the open list. Presentation, and reset every open.
var _open_scroll := 0
var _hover_option := -1
var _hover_hit := ""

var _font: Font = null
var _bold: Font = null


func _ready() -> void:
	_ensure_children()
	if not resized.is_connected(_relayout):
		resized.connect(_relayout)


## Build the map preview, IDEMPOTENTLY, and never rely on `_ready` having run.
##
## A `SceneTree` runner adds this control during `_initialize()`, before the root
## window is fully in the tree, so Godot defers `_ready` past the runner's own
## `configure()` call. The first capture this lane took had a black map for
## exactly that reason: `configure()` handed retail's geometry to a child that
## did not exist yet, and nothing said so. Both entry points call this now.
func _ensure_children() -> void:
	if _font == null:
		_font = ThemeDB.fallback_font
		_bold = _font
	mouse_filter = Control.MOUSE_FILTER_STOP
	focus_mode = Control.FOCUS_ALL
	if map_preview != null:
		return
	map_preview = MapPreviewScript.new()
	map_preview.name = "MapPreview"
	map_preview.visible = false
	map_preview.region_hovered.connect(_on_region_hovered)
	add_child(map_preview)


# --- configuration -----------------------------------------------------------

## Take everything the screen needs, all of it read-only.
##
## `session_probe` is a `WotrSession` with only its `world` set - the menu builds
## one to ask `seat_options()` - so this screen never constructs strategic state
## and never has to be trusted not to.
func configure(
		source_document: Dictionary,
		session_probe,
		pack_roots: Array,
		reason: String) -> void:
	_ensure_children()
	unavailable_reason = reason
	document = source_document
	_load_bundles(pack_roots)
	if not reason.is_empty():
		_relayout()
		queue_redraw()
		return
	world = WorldScript.new()
	if not world.load_from_dict(document, ""):
		unavailable_reason = "the living-world document did not load: %s" % str(world.errors)
		world = null
		_relayout()
		queue_redraw()
		return
	_read_seat_options(session_probe)
	_read_scenarios(session_probe)
	_seat_default_rows()
	_bind_geometry()
	_collect_absences()
	_relayout()
	queue_redraw()


## THE GEOMETRY BUNDLE IS FOUND FIRST, ON PURPOSE. Both string bundles live
## BESIDE the region geometry - that is the documented layout and what
## `OPENBFME_LIVING_MAP_REGIONS` points at - so the directory the geometry was
## actually found in is the strongest root either of them has. Searching the
## mounted pack roots alone found neither, and the screen came up showing
## retail's keys with a real bundle sitting on disk two directories away.
func _load_bundles(pack_roots: Array) -> void:
	var roots: Array = []
	for value in pack_roots:
		var root := String(value).strip_edges()
		if not root.is_empty():
			roots.append(root)
	geometry = RegionGeometryScript.new()
	var geometry_found: Dictionary = geometry.locate_and_load(roots)
	geometry_reason = "" if bool(geometry_found.get("ok", false)) else String(geometry_found.get("reason", ""))
	var string_roots := roots.duplicate()
	if geometry.loaded and not geometry.bundle_root.is_empty():
		string_roots.append(geometry.bundle_root)
	setup_strings = SetupStringsScript.new()
	var setup_found: Dictionary = setup_strings.locate_and_load(string_roots)
	setup_strings_reason = "" if bool(setup_found.get("ok", false)) else String(setup_found["reason"])
	strings = StringsScript.new()
	var strings_found: Dictionary = strings.locate_and_load(string_roots)
	strings_reason = "" if bool(strings_found.get("ok", false)) else String(strings_found["reason"])


## THE ARMY LIST. Every player template the session would offer, INCLUDING the
## ones it would refuse: a faction whose pack conversion is missing is listed
## with its reason rather than silently absent, so "where is Angmar" has an
## answer on screen.
func _read_seat_options(session_probe) -> void:
	seat_options = []
	if session_probe == null:
		return
	for option in session_probe.seat_options(_availability()):
		seat_options.append({
			"template": String(option["template"]),
			"pack_faction": String(option["pack_faction"]),
			"unavailable_reason": String(option["unavailable_reason"]),
		})


## The menu owns the real availability map; when this screen is driven directly
## (by the capture runner) every bound faction is treated as fieldable, which is
## the same thing the round-trip runner does and is stated in both.
var pack_faction_availability: Dictionary = {}


func _availability() -> Dictionary:
	return pack_faction_availability


## THE SCENARIO LIST: every scenario in the document's own campaign, in the
## document's order, each one carrying whether a session can actually start on
## it and, when it cannot, THE REASON.
##
## Retail's freeform scenarios - "War of the Ring" itself among them - claim no
## territory for anybody, so `WotrSession.begin()` has no ownership to apply and
## refuses. They are listed anyway, with that reason, because hiding retail's
## headline scenario would be a worse answer than showing why it cannot start.
func _read_scenarios(session_probe) -> void:
	scenarios = []
	scenario_index = 0
	if world == null:
		return
	var startable: Array = []
	if session_probe != null:
		startable = Array(session_probe.startable_scenarios(MIN_SEATS))
	var raw_rows: Array = document.get("scenarios", []) as Array
	var tags_by_name: Dictionary = {}
	for value in raw_rows:
		var row := value as Dictionary
		tags_by_name[String(row.get("name", ""))] = row.get("displayTags", {}) as Dictionary
	for name_value in world.scenario_names:
		var name := String(name_value)
		var record := world.scenario(name)
		if String(record.get("region_campaign", "")) != world.campaign_name:
			continue
		var tags: Dictionary = tags_by_name.get(name, {}) as Dictionary
		var sets: Array = record.get("ownership_sets", []) as Array
		var reason := ""
		if not startable.has(name):
			if sets.is_empty():
				reason = (
					"this scenario claims no territory for any player, so the "
					+ "strategic layer has no ownership to apply")
			else:
				reason = (
					"this scenario authors %d ownership set(s); a session needs at "
					+ "least %d, each with regions") % [sets.size(), MIN_SEATS]
		scenarios.append({
			"name": name,
			"display_key": String(tags.get("displayName", "")),
			"description_key": String(tags.get("displayDescription", "")),
			"objectives_key": String(tags.get("displayObjectives", "")),
			"game_type_key": String(tags.get("displayGameType", "")),
			"max_players": int(record.get("max_players", 0)),
			"ownership_sets": sets.size(),
			"startable": startable.has(name),
			"reason": reason,
		})
	# Open on a scenario that can actually start, so PLAY works on arrival. The
	# list order is retail's; only the CURSOR moves.
	for index in range(scenarios.size()):
		if bool(scenarios[index]["startable"]):
			scenario_index = index
			break


## The opening seating: the first fieldable armies, seat 0 human and the rest
## machine, one team each, colours in `multiplayer.ini` slot order. AUTHORED and
## recorded in `wotr_setup_bindings.gd`; it is a starting position the player
## then changes, not a claim about retail's own defaults.
func _seat_default_rows() -> void:
	seats = []
	var fieldable: Array[int] = []
	for index in range(seat_options.size()):
		if String(seat_options[index]["unavailable_reason"]).is_empty():
			fieldable.append(index)
	var count := mini(maxi(fieldable.size(), 0), MIN_SEATS)
	for row in range(count):
		seats.append({
			"option_index": fieldable[row],
			"team": row + 1,
			"controller": "human" if row == 0 else "ai",
			"color_slot": int(BindingsScript.default_color(row)["slot"]),
			"handicap_index": BindingsScript.HANDICAP_DEFAULT,
		})


func _bind_geometry() -> void:
	if map_preview == null:
		return
	if world == null:
		return
	map_preview.set_geometry(geometry, world.region_ids)
	_refresh_ownership()


## Shade the preview from the SELECTED scenario's own ownership sets. Set N goes
## to seat N, which is exactly how `wotr_state.apply_ownership_sets()` deals
## them out, so the picture and the campaign cannot disagree.
func _refresh_ownership() -> void:
	if map_preview == null or world == null:
		return
	var colors: Dictionary = {}
	var record := _selected_scenario_record()
	var sets: Array = record.get("ownership_sets", []) as Array
	for index in range(sets.size()):
		if index >= seats.size():
			break
		var tint := _seat_color(index)
		for region_value in (sets[index] as Dictionary).get("regions", PackedStringArray()) as PackedStringArray:
			colors[String(region_value)] = tint
	map_preview.set_ownership(colors)


func _selected_scenario_record() -> Dictionary:
	if world == null or scenarios.is_empty():
		return {}
	return world.scenario(String(scenarios[scenario_index]["name"]))


## Everything retail's screen has that this one does not, named one line at a
## time. A count on its own would hide which one is missing.
func _collect_absences() -> void:
	var lines: Array[String] = []
	if not setup_strings_reason.is_empty():
		lines.append(setup_strings_reason)
	if not strings_reason.is_empty():
		lines.append(strings_reason)
	if not geometry_reason.is_empty():
		lines.append("map preview: %s" % geometry_reason)
	elif geometry != null and geometry.loaded and map_preview != null:
		var missing: PackedStringArray = map_preview.regions_without_geometry
		if not missing.is_empty():
			lines.append("%d region(s) the document declares carry no shape in the geometry bundle and are NOT drawn: %s" % [
				missing.size(), ", ".join(Array(missing))])
	lines.append(
		"retail's frame art (SCShellUserInterface512_001.tga, "
		+ "MainMenuRuleruserinterface.tga, Ruler) is in no archive; every panel, "
		+ "tab, dropdown plate, checkbox and button on this screen is hand-built "
		+ "in wotr_chrome.gd. The territory shapes are retail's own.")
	lines.append(
		"hero DISPLAY NAMES live in the OBJECT: namespace, which neither string "
		+ "bundle converts, so the Hero column shows retail's own template ids.")
	for row in BindingsScript.RULE_ROWS:
		if not String(row.get("absent_reason", "")).is_empty():
			lines.append("%s: %s" % [_label(String(row["label_key"])), String(row["absent_reason"])])
	var victory: Dictionary = BindingsScript.victory_options(
		document.get("victoryTypes", []) as Array)
	if not String(victory["reason"]).is_empty():
		lines.append("%s: %s" % [_label("RULE:VictoryType"), String(victory["reason"])])
	absences = PackedStringArray(lines)


## One line per fact worth printing at load.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	lines.append("setup strings: %s" % (
		"%d keys from %s" % [setup_strings.count(), setup_strings.source_path]
		if setup_strings != null and setup_strings.loaded else setup_strings_reason))
	lines.append("living-world strings: %s" % (
		"%d keys from %s" % [strings.count(), strings.source_path]
		if strings != null and strings.loaded else strings_reason))
	lines.append("scenarios: %d in campaign, %d startable" % [
		scenarios.size(), _startable_count()])
	lines.append("armies: %d offered, %d fieldable" % [
		seat_options.size(), _fieldable_count()])
	if map_preview != null:
		lines.append("map preview: %d regions shaded from retail geometry, %d without a shape" % [
			map_preview.drawn_regions.size(), map_preview.regions_without_geometry.size()])
	for line in absences:
		lines.append("ABSENT: %s" % line)
	if not unresolved_keys.is_empty():
		lines.append("UNRESOLVED KEYS (%d): %s" % [
			unresolved_keys.size(), ", ".join(Array(unresolved_keys))])
	return PackedStringArray(lines)


func _startable_count() -> int:
	var total := 0
	for row in scenarios:
		if bool(row["startable"]):
			total += 1
	return total


func _fieldable_count() -> int:
	var total := 0
	for row in seat_options:
		if String(row["unavailable_reason"]).is_empty():
			total += 1
	return total


# --- string resolution --------------------------------------------------------

## Retail's text for a shell key, or THE KEY ITSELF when no bundle carries it.
## Every miss is recorded so the screen can name it.
func _label(key: String) -> String:
	if key.is_empty():
		return ""
	var value := ""
	if setup_strings != null:
		value = setup_strings.text(key)
	if value.is_empty() and strings != null:
		value = strings.text(key)
	if value.is_empty():
		_record_unresolved(key)
		return key
	return value


func _record_unresolved(key: String) -> void:
	if Array(unresolved_keys).has(key):
		return
	var keys: Array[String] = []
	for value in unresolved_keys:
		keys.append(String(value))
	keys.append(key)
	keys.sort()
	unresolved_keys = PackedStringArray(keys)


# --- the choices the player makes ---------------------------------------------

func scenario_name() -> String:
	if scenarios.is_empty():
		return ""
	return String(scenarios[scenario_index]["name"])


## The seat array `WotrSession.begin()` takes, built from the table. Nothing
## else is added: colour and handicap stay here, because neither belongs in
## strategic state.
func seat_payload() -> Array:
	var payload: Array = []
	for row in seats:
		var option := seat_options[int(row["option_index"])] as Dictionary
		payload.append({
			"template": String(option["template"]),
			"team": int(row["team"]),
			"controller": String(row["controller"]),
			# RETAIL'S OWN RUNG, not the index into the list. The strategic layer
			# validates it against retail's ladder and refuses anything off it,
			# so sending an index would send a number retail never wrote.
			"handicap": int(BindingsScript.HANDICAP_LEVELS[int(row["handicap_index"])]),
		})
	return payload


## The RULES tab's campaign-wide choices, in the vocabulary the strategic layer
## stores rather than the `VALUE:` keys the screen shows. Only rows that
## actually reach the strategic layer are included: a locked row contributes
## nothing, because contributing a value nothing carries is how a control comes
## to look like it works.
func rules_payload() -> Dictionary:
	var payload: Dictionary = {}
	for row in BindingsScript.RULE_ROWS:
		if String(row.get("reaches", "")) != "strategic":
			continue
		var field := String(row.get("state_field", ""))
		var states: Array = row.get("values_state", []) as Array
		if field.is_empty() or states.is_empty():
			continue
		var index := _rule_index(row, states.size())
		payload[field] = String(states[index])
	return payload


## Which option a rule row is currently showing. The player's pick when they
## have made one, else the row's own authored default - clamped, because a
## document with fewer victory types than the table has rows would otherwise
## index past the end.
func _rule_index(row: Dictionary, count: int) -> int:
	if count <= 0:
		return 0
	var chosen := int(rule_choice.get(String(row.get("id", "")), int(row.get("default", 0))))
	return clampi(chosen, 0, count - 1)


func _rule_row(id: String) -> Dictionary:
	for row in BindingsScript.RULE_ROWS:
		if String(row["id"]) == id:
			return row
	return {}


## What choosing this option will actually mean, in the strategic layer's own
## words. Shown in the dropdown so the consequence is visible at the moment of
## choosing rather than discovered at the first battle.
func _rule_state_note(row: Dictionary, index: int) -> String:
	var states: Array = row.get("values_state", []) as Array
	if index < 0 or index >= states.size():
		return ""
	match String(states[index]):
		"auto_resolve_and_rts":
			return "both are offered; the strategic screen shows an ATTACK and an AUTO-RESOLVE button and the choice is recorded per battle"
		"auto_resolve":
			return "every battle is decided by retail's auto-resolve tables and this project's dice"
		"rts":
			return "every battle is fought in the tactical layer"
	return ""


## "" when PLAY can be pressed, else the reason it cannot - the same shape the
## session's own refusals take, and shown in the same place.
func play_refusal() -> String:
	if not unavailable_reason.is_empty():
		return unavailable_reason
	if scenarios.is_empty():
		return "this campaign carries no scenario"
	var scenario := scenarios[scenario_index] as Dictionary
	if not bool(scenario["startable"]):
		return "%s cannot start: %s" % [_scenario_display(scenario), String(scenario["reason"])]
	if seats.size() < MIN_SEATS:
		return "a War of the Ring session needs at least %d seats; %d are seated" % [
			MIN_SEATS, seats.size()]
	var sets: Array = _selected_scenario_record().get("ownership_sets", []) as Array
	if sets.size() < seats.size():
		return "%s authors ownership for %d players; %d are seated" % [
			_scenario_display(scenario), sets.size(), seats.size()]
	var used: Dictionary = {}
	for row in seats:
		var template := String((seat_options[int(row["option_index"])] as Dictionary)["template"])
		if used.has(template):
			return "two seats are both playing %s" % template
		used[template] = true
	var human := 0
	for row in seats:
		if String(row["controller"]) == "human":
			human += 1
	if human == 0:
		return "no seat is played by a person"
	return ""


func _scenario_display(row: Dictionary) -> String:
	var key := String(row.get("display_key", ""))
	if key.is_empty():
		return String(row.get("name", ""))
	return _label(key)


func _seat_color(index: int) -> Color:
	if index >= seats.size():
		return ChromeScript.STEEL_DARK
	var slot := int(seats[index]["color_slot"])
	for entry in BindingsScript.COLORS:
		if int(entry["slot"]) == slot:
			return entry["rgb"] as Color
	return ChromeScript.STEEL_DARK


func _color_entry(slot: int) -> Dictionary:
	for entry in BindingsScript.COLORS:
		if int(entry["slot"]) == slot:
			return entry
	return BindingsScript.COLORS[0]


# --- layout -------------------------------------------------------------------

const PAD := 26.0
const TITLE_H := 46.0
const HEADING_H := 30.0
const TAB_H := 30.0
const TAB_W := 132.0
const BAR_H := 46.0
const ROW_H := 28.0

var _panel_rect := Rect2()
var _table_rect := Rect2()


## THE RECTANGLES ARE PURE MATH AND ARE RECOMPUTED EVERY PAINT.
##
## They used to be cached by a `resized` handler alone, and the first capture
## this lane took showed the whole screen collapsed into a 40-pixel strip: the
## handler had run once at a size the control has since grown out of, and
## nothing recomputed. A layout that depends on having been told about every
## resize is a layout that is wrong the one time it is not told.
func _compute_rects() -> void:
	var width := maxf(size.x, 320.0)
	var height := maxf(size.y, 320.0)
	var top := PAD + TITLE_H + 6.0 + HEADING_H + 8.0 + TAB_H
	var bar_top := height - PAD - BAR_H
	var body_height := maxf(bar_top - top - 14.0, 120.0)
	# The seat table takes as much as its rows need and no more; the tab panel
	# takes the rest. Retail's table sits under both tabs and is always visible.
	var rows := maxi(seats.size(), MIN_SEATS)
	var table_height := clampf(ROW_H * float(rows) + 76.0, 90.0, body_height * 0.55)
	_panel_rect = Rect2(PAD, top, width - PAD * 2.0,
		maxf(body_height - table_height - 12.0, 80.0))
	_table_rect = Rect2(PAD, _panel_rect.position.y + _panel_rect.size.y + 12.0,
		width - PAD * 2.0, table_height)


func _relayout() -> void:
	_compute_rects()
	_place_map_preview()
	queue_redraw()


## The map preview is a real child Control (it has to be: it picks a territory
## out of retail's own triangles under the cursor), so its rectangle is SET,
## never drawn. Deferred, because this is also called from inside `_draw`.
func _place_map_preview() -> void:
	if map_preview == null:
		return
	var showing := active_tab == TAB_MAP and unavailable_reason.is_empty() and world != null
	var target := _map_rect()
	if map_preview.visible != showing:
		map_preview.set_deferred("visible", showing)
	if not showing:
		return
	if not map_preview.position.is_equal_approx(target.position):
		map_preview.set_deferred("position", target.position)
	if not map_preview.size.is_equal_approx(target.size):
		map_preview.set_deferred("size", target.size)


## The MAP tab's three columns: scenario picker and description, the map itself,
## and the territory description - retail's own arrangement.
func _map_columns() -> Array[Rect2]:
	var inner := _panel_rect.grow(-16.0)
	var left := inner.size.x * 0.28
	var right := inner.size.x * 0.26
	var centre := inner.size.x - left - right - 24.0
	return [
		Rect2(inner.position, Vector2(left, inner.size.y)),
		Rect2(inner.position + Vector2(left + 12.0, 0.0), Vector2(centre, inner.size.y)),
		Rect2(inner.position + Vector2(left + centre + 24.0, 0.0), Vector2(right, inner.size.y)),
	]


func _map_rect() -> Rect2:
	var columns := _map_columns()
	return columns[1]


# --- painting -----------------------------------------------------------------

func _draw() -> void:
	var width := size.x
	var height := size.y
	_hits = []
	_compute_rects()
	_place_map_preview()
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.03, 0.045, 1.0))
	ChromeScript.draw_steel_frame(self, Rect2(8.0, 8.0, width - 16.0, height - 16.0))

	var title_rect := Rect2(PAD, PAD - 10.0, width - PAD * 2.0, TITLE_H)
	ChromeScript.draw_steel_title_plate(self, title_rect)
	_centre_text(_label(BindingsScript.SHELL_KEYS["title"]),
		title_rect, TITLE_FONT, ChromeScript.STEEL_ICE)

	var heading_rect := Rect2(PAD, title_rect.position.y + TITLE_H + 4.0,
		width - PAD * 2.0, HEADING_H)
	_centre_text(_label(BindingsScript.SHELL_KEYS["heading"]),
		heading_rect, HEADING_FONT, ChromeScript.STEEL_PALE)

	if not unavailable_reason.is_empty():
		_draw_refusal()
		return

	_draw_tabs(heading_rect.position.y + HEADING_H + 6.0)
	ChromeScript.draw_steel_frame(self, _panel_rect, 1)
	if active_tab == TAB_MAP:
		_draw_map_tab()
	else:
		_draw_rules_tab()
	_draw_table()
	_draw_bottom_bar()
	if not _open_menu.is_empty():
		_draw_open_menu()


func _draw_refusal() -> void:
	var box := Rect2(PAD, size.y * 0.32, size.x - PAD * 2.0, size.y * 0.36)
	ChromeScript.draw_inset_glass(self, box)
	_wrap_text(unavailable_reason, box.grow(-18.0), BODY_FONT, TEXT_WARN)


func _draw_tabs(at_y: float) -> void:
	var x := _panel_rect.position.x + 18.0
	var labels := [
		{"id": "tab_map", "key": BindingsScript.SHELL_KEYS["tab_map"], "index": TAB_MAP},
		{"id": "tab_rules", "key": BindingsScript.SHELL_KEYS["tab_rules"], "index": TAB_RULES},
	]
	for entry in labels:
		var rect := Rect2(x, at_y, TAB_W, TAB_H)
		var active: bool = int(entry["index"]) == active_tab
		ChromeScript.draw_tab(self, rect, active)
		_centre_text(_label(String(entry["key"])), rect, LABEL_FONT,
			ChromeScript.STEEL_ICE if active else TEXT_DIM)
		_hits.append({"id": String(entry["id"]), "rect": rect, "kind": "tab",
			"index": int(entry["index"])})
		x += TAB_W + 6.0


# --- MAP tab -------------------------------------------------------------------

func _draw_map_tab() -> void:
	var columns := _map_columns()
	var left := columns[0]
	var right := columns[2]

	# Scenario picker.
	var label_rect := Rect2(left.position, Vector2(left.size.x, 20.0))
	_text(_label(BindingsScript.SHELL_KEYS["scenario"]),
		label_rect.position + Vector2(2.0, 15.0), LABEL_FONT, ChromeScript.STEEL_PALE)
	var picker := Rect2(left.position + Vector2(0.0, 22.0), Vector2(left.size.x, 26.0))
	var scenario_row: Dictionary = scenarios[scenario_index] if not scenarios.is_empty() else {}
	_draw_menu_field("scenario", picker,
		_scenario_display(scenario_row) if not scenario_row.is_empty() else "",
		not scenarios.is_empty(), "")

	# Scenario description - retail's own text, verbatim, escapes and all.
	var description_label := picker.position + Vector2(0.0, 34.0)
	_text(_label(BindingsScript.SHELL_KEYS["scenario_description"]),
		description_label + Vector2(2.0, 12.0), LABEL_FONT, ChromeScript.STEEL_PALE)
	var description_box := Rect2(
		description_label + Vector2(0.0, 20.0), Vector2(left.size.x, 150.0))
	ChromeScript.draw_inset_glass(self, description_box)
	_wrap_text(_scenario_description(scenario_row), description_box.grow(-12.0),
		BODY_FONT, TEXT)

	# WHAT IS NOT RETAIL'S, ON THE SCREEN ITSELF. Retail puts nothing here; this
	# panel is Open BFME's own and is labelled as Open BFME's, because a claim
	# that only reaches a log file is a claim nobody reads. Every line in it is
	# generated by `_collect_absences()` from what actually failed to resolve.
	var notes_top := description_box.position.y + description_box.size.y + 18.0
	_text("OPEN BFME - WHAT ON THIS SCREEN IS NOT RETAIL'S",
		Vector2(left.position.x + 2.0, notes_top - 5.0), SMALL_FONT, TEXT_DIM)
	var notes_box := Rect2(Vector2(left.position.x, notes_top),
		Vector2(left.size.x, maxf(60.0, left.position.y + left.size.y - notes_top)))
	ChromeScript.draw_inset_glass(self, notes_box)
	var body: Array[String] = []
	for line in absences:
		body.append("- %s" % String(line))
	if not unresolved_keys.is_empty():
		body.append("- %d retail key(s) no bundle carried, shown as keys: %s" % [
			unresolved_keys.size(), ", ".join(Array(unresolved_keys))])
	_wrap_text("\n".join(body), notes_box.grow(-10.0), SMALL_FONT, TEXT_DIM)

	# The map itself is a child Control; only its label is drawn here.
	if not scenario_row.is_empty() and not bool(scenario_row.get("startable", false)):
		var note := Rect2(_map_rect().position + Vector2(0.0, _map_rect().size.y - 34.0),
			Vector2(_map_rect().size.x, 30.0))
		draw_rect(note, Color(0.0, 0.0, 0.0, 0.62))
		_wrap_text(String(scenario_row.get("reason", "")), note.grow(-6.0), SMALL_FONT, TEXT_WARN)

	# Territory description - empty until a region is hovered, exactly as retail.
	_text(_label(BindingsScript.SHELL_KEYS["territory_description"]),
		right.position + Vector2(2.0, 15.0), LABEL_FONT, ChromeScript.STEEL_PALE)
	var territory_box := Rect2(right.position + Vector2(0.0, 22.0),
		Vector2(right.size.x, right.size.y - 22.0))
	ChromeScript.draw_inset_glass(self, territory_box)
	_wrap_text(_territory_text, territory_box.grow(-12.0), BODY_FONT, TEXT)


## Retail's scenario description, verbatim. Retail writes `\n` as two literal
## characters in the table and the converter preserves them; this turns them
## into line breaks for display and changes nothing else.
func _scenario_description(row: Dictionary) -> String:
	if row.is_empty():
		return ""
	var key := String(row.get("description_key", ""))
	if key.is_empty():
		return "this scenario carries no displayDescription tag"
	var body := ""
	if strings != null:
		body = strings.text(key)
	if body.is_empty() and setup_strings != null:
		body = setup_strings.text(key)
	if body.is_empty():
		_record_unresolved(key)
		return key
	return body.replace("\\n", "\n")


func _on_region_hovered(region_id: String) -> void:
	if region_id == _hover_region:
		return
	_hover_region = region_id
	_territory_text = _describe_region(region_id)
	queue_redraw()


## What retail puts in the Territory Description box: the region's own name, the
## territory it belongs to, and who claims it under this scenario. Every part is
## the document's; a name that does not resolve shows retail's id, which is what
## the strategic screen does for the same reason.
func _describe_region(region_id: String) -> String:
	if region_id.is_empty() or world == null:
		return ""
	var region := world.region(region_id)
	if region.is_empty():
		return ""
	var lines: Array[String] = []
	var display_key := String(region.get("display_name", ""))
	var name := ""
	if strings != null and not display_key.is_empty():
		name = strings.text(display_key)
	if name.is_empty():
		name = region_id
	lines.append(name)
	# The territory's own key is `territory` on the territory record, not
	# `display_name`; reading the wrong one silently produced a two-line panel
	# where retail shows three.
	var territory := world.territory_of(region_id)
	var territory_key := String(territory.get("territory", ""))
	if not territory_key.is_empty() and strings != null:
		var territory_name := strings.text(territory_key)
		lines.append(territory_name if not territory_name.is_empty() else territory_key)
	var sets: Array = _selected_scenario_record().get("ownership_sets", []) as Array
	var claimed := ""
	for index in range(sets.size()):
		if index >= seats.size():
			break
		var regions: PackedStringArray = (sets[index] as Dictionary).get(
			"regions", PackedStringArray()) as PackedStringArray
		if Array(regions).has(region_id):
			claimed = _army_label(index)
			break
	lines.append("claimed by %s" % claimed if not claimed.is_empty() else "unclaimed at start")
	return "\n".join(lines)


# --- RULES tab -----------------------------------------------------------------

func _draw_rules_tab() -> void:
	var inner := _panel_rect.grow(-18.0)
	var label_width := inner.size.x * 0.26
	var field_width := minf(300.0, inner.size.x * 0.30)
	var y := inner.position.y + 4.0
	for row in BindingsScript.RULE_ROWS:
		var label_key := String(row["label_key"])
		_text(_label(label_key), Vector2(inner.position.x, y + 18.0), LABEL_FONT, TEXT)
		var field := Rect2(inner.position.x + label_width, y, field_width, 26.0)
		var options := _rule_options(row)
		var enabled := not options.is_empty() and not String(row.get("reaches", "")).is_empty()
		var shown := ""
		if options.is_empty():
			shown = "unavailable"
		else:
			shown = String(options[_rule_index(row, options.size())]["text"])
		# `rule:` prefixed, because the seat-column ids are `<kind>_<row>` and a
		# rule id like `battle_type` splits into two parts exactly the same way -
		# an unprefixed id would have been picked up as seat row "type".
		_draw_menu_field("rule:" + String(row["id"]), field, shown, enabled, "")
		# THE REASON, ON SCREEN, WHEN THERE IS ONE. Two of these rows used to be
		# locked because no auto-resolve path existed; one does now, so they are
		# live controls and carry a statement of where the choice LANDS instead.
		var reason := String(row.get("locked_reason", ""))
		if enabled and reason.is_empty():
			reason = "reaches WotrSession.begin() -> state.%s, inside the strategic hash" % String(
				row.get("state_field", "?"))
		if options.is_empty():
			reason = String(row.get("absent_reason", ""))
			if reason.is_empty():
				var victory: Dictionary = BindingsScript.victory_options(
					document.get("victoryTypes", []) as Array)
				reason = String(victory["reason"])
		_wrap_text(reason,
			Rect2(field.position.x + field_width + 14.0, y - 1.0,
				maxf(80.0, inner.size.x - label_width - field_width - 14.0), 28.0),
			SMALL_FONT, TEXT_LOCKED)
		y += 34.0

	y += 6.0
	for row in BindingsScript.RULE_CHECKBOXES:
		var box := Rect2(inner.position.x + 2.0, y + 3.0, 18.0, 18.0)
		ChromeScript.draw_checkbox(self, box, bool(row["default"]), false)
		_text(_label(String(row["label_key"])),
			Vector2(box.position.x + 28.0, y + 17.0), LABEL_FONT, TEXT_LOCKED)
		_wrap_text(String(row.get("locked_reason", "")),
			Rect2(inner.position.x + label_width + 2.0, y, inner.size.x - label_width - 4.0, 26.0),
			SMALL_FONT, TEXT_LOCKED)
		y += 28.0

	var reset := Rect2(inner.position.x, inner.position.y + inner.size.y - 32.0, 128.0, 30.0)
	ChromeScript.draw_steel_button(self, reset, 3)
	_centre_text(_label(BindingsScript.SHELL_KEYS["button_reset"]), reset, LABEL_FONT, TEXT_LOCKED)
	_wrap_text(
		"every row is drawn from retail's own RULE:/VALUE: keys. BATTLE TYPE and "
		+ "BATTLE TYPE PRIORITY are now LIVE - they land in the strategic state "
		+ "and ride the version-3 commitment. The rest stay LOCKED because "
		+ "nothing carries them, and each says which.",
		Rect2(reset.position.x + 142.0, reset.position.y - 6.0,
			inner.size.x - 150.0, 44.0), SMALL_FONT, TEXT_DIM)


## The options one rule row offers, resolved. Never invented: a row whose values
## are not in a shipped file returns EMPTY and is drawn unavailable.
func _rule_options(row: Dictionary) -> Array[Dictionary]:
	var options: Array[Dictionary] = []
	if String(row["id"]) == "victory_type":
		var victory: Dictionary = BindingsScript.victory_options(
			document.get("victoryTypes", []) as Array)
		for entry in victory["options"] as Array:
			var record := entry as Dictionary
			var text := _label(String(record["label_key"]))
			var qualifier := String(record["qualifier_key"])
			if not qualifier.is_empty():
				text = "%s - %s" % [text, _label(qualifier)]
			options.append({"text": text, "value": int(record["index"])})
		return options
	for key_value in row["values"] as Array:
		var key := String(key_value)
		var text := ""
		if strings != null:
			text = strings.text(key)
		if text.is_empty() and setup_strings != null:
			text = setup_strings.text(key)
		if text.is_empty():
			_record_unresolved(key)
			continue
		options.append({"text": text, "value": options.size()})
	return options


# --- the seat table ------------------------------------------------------------

const COLUMN_WEIGHTS := [0.20, 0.22, 0.24, 0.10, 0.12, 0.12]


func _draw_table() -> void:
	ChromeScript.draw_steel_frame(self, _table_rect, 1)
	var inner := _table_rect.grow(-14.0)
	var keys := [
		"column_player", "column_army", "column_hero",
		"column_team", "column_color", "column_handicap",
	]
	var x := inner.position.x
	var widths: Array[float] = []
	for weight in COLUMN_WEIGHTS:
		widths.append(inner.size.x * float(weight))
	for index in range(keys.size()):
		_text(_label(BindingsScript.SHELL_KEYS[keys[index]]),
			Vector2(x + 4.0, inner.position.y + 14.0), LABEL_FONT, ChromeScript.STEEL_PALE)
		x += widths[index]
	ChromeScript.draw_row_rule(self,
		Vector2(inner.position.x, inner.position.y + 20.0), inner.size.x, true)

	var y := inner.position.y + 24.0
	for row_index in range(seats.size()):
		var row := seats[row_index] as Dictionary
		var band := Rect2(inner.position.x, y, inner.size.x, ROW_H)
		ChromeScript.draw_row_band(self, band,
			2 if String(row["controller"]) == "human" else (1 if row_index % 2 == 1 else 0))
		x = inner.position.x
		var cell_y := y + 2.0
		# Player: a person, or the AI tier - which is FIXED, and says so.
		var human := String(row["controller"]) == "human"
		_draw_menu_field("player_%d" % row_index,
			Rect2(x + 2.0, cell_y, widths[0] - 8.0, ROW_H - 4.0),
			_label(BindingsScript.AI_TIER_KEYS[BindingsScript.AI_TIER_FIXED]) if not human else "Player 1",
			true, "")
		x += widths[0]
		_draw_menu_field("army_%d" % row_index,
			Rect2(x + 2.0, cell_y, widths[1] - 8.0, ROW_H - 4.0),
			_army_label(row_index), true, "")
		x += widths[1]
		# Hero: retail's own template id. See `absences`.
		_text(_hero_text(row_index), Vector2(x + 6.0, y + 19.0), BODY_FONT, TEXT_DIM)
		x += widths[2]
		_draw_menu_field("team_%d" % row_index,
			Rect2(x + 2.0, cell_y, widths[3] - 8.0, ROW_H - 4.0),
			str(int(row["team"])), true, "")
		x += widths[3]
		var swatch := Rect2(x + 6.0, y + 6.0, 26.0, ROW_H - 12.0)
		ChromeScript.draw_swatch(self, swatch, _seat_color(row_index))
		_text(_label(String(_color_entry(int(row["color_slot"]))["name_key"])),
			Vector2(swatch.position.x + 34.0, y + 19.0), BODY_FONT, TEXT)
		_hits.append({"id": "color_%d" % row_index,
			"rect": Rect2(x + 2.0, cell_y, widths[4] - 8.0, ROW_H - 4.0), "kind": "menu"})
		x += widths[4]
		# Handicap: retail's own ladder, and now a REAL CONTROL. It used to be
		# drawn as a dead value plate because nothing carried it; the seat row
		# carries it now, the version-3 commitment carries it, and it scales
		# retail's own auto-resolve multipliers.
		var handicap := Rect2(x + 4.0, y + 5.0, widths[5] - 14.0, ROW_H - 10.0)
		_draw_menu_field("handicap_%d" % row_index, handicap,
			"%d%%" % BindingsScript.HANDICAP_LEVELS[int(row["handicap_index"])], true, "")
		ChromeScript.draw_row_rule(self, Vector2(inner.position.x, y + ROW_H), inner.size.x)
		y += ROW_H

	# Add and remove a seat. Retail seats 2-6; the scenario's own ownership sets
	# and the fieldable army count cap it, and both caps are shown.
	var add := Rect2(inner.position.x, y + 6.0, 116.0, 24.0)
	var can_add := seats.size() < _seat_ceiling()
	ChromeScript.draw_steel_button(self, add, 0 if can_add else 3)
	_centre_text("ADD PLAYER", add, SMALL_FONT, TEXT if can_add else TEXT_LOCKED)
	if can_add:
		_hits.append({"id": "add_seat", "rect": add, "kind": "button"})
	var remove := Rect2(add.position.x + 124.0, add.position.y, 132.0, 24.0)
	var can_remove := seats.size() > MIN_SEATS
	ChromeScript.draw_steel_button(self, remove, 0 if can_remove else 3)
	_centre_text("REMOVE PLAYER", remove, SMALL_FONT, TEXT if can_remove else TEXT_LOCKED)
	if can_remove:
		_hits.append({"id": "remove_seat", "rect": remove, "kind": "button"})
	_text("%d of %d armies fieldable | %s seats %d | handicap is live; AI tier is locked" % [
			_fieldable_count(), seat_options.size(),
			_scenario_display(scenarios[scenario_index]) if not scenarios.is_empty() else "-",
			_selected_scenario_record().get("ownership_sets", []).size()],
		Vector2(remove.position.x + 146.0, add.position.y + 16.0), SMALL_FONT, TEXT_DIM)


## The most seats this scenario and this pack can carry: retail's own six, the
## scenario's authored ownership sets, and the armies the tactical layer can
## actually field, whichever is smallest.
func _seat_ceiling() -> int:
	var sets: Array = _selected_scenario_record().get("ownership_sets", []) as Array
	return mini(mini(MAX_SEATS, sets.size()), _fieldable_count())


func _army_label(row_index: int) -> String:
	if row_index >= seats.size():
		return ""
	var option := seat_options[int(seats[row_index]["option_index"])] as Dictionary
	var template := String(option["template"])
	var key := String(BindingsScript.ARMY_SIDE_KEYS.get(template, ""))
	if key.is_empty():
		return template
	return _label(key)


func _hero_text(row_index: int) -> String:
	if world == null or row_index >= seats.size():
		return ""
	var template := String((seat_options[int(seats[row_index]["option_index"])] as Dictionary)["template"])
	var raw_rows: Array = document.get("scenarios", []) as Array
	for value in raw_rows:
		var row := value as Dictionary
		if String(row.get("name", "")) != scenario_name():
			continue
		for army_value in row.get("actArmies", []) as Array:
			var army := army_value as Dictionary
			var hero := String(army.get("heroTemplateName", ""))
			if hero.is_empty():
				continue
			if not Array(army.get("spawnForTemplates", []) as Array).has(template):
				continue
			return hero
	return "-"


# --- bottom bar ----------------------------------------------------------------

func _draw_bottom_bar() -> void:
	var y := size.y - PAD - BAR_H
	var width := 168.0
	var main_menu := Rect2(PAD, y, width, BAR_H - 8.0)
	ChromeScript.draw_steel_button(self, main_menu, 1 if _hover_hit == "main_menu" else 0)
	_centre_text(_label(BindingsScript.SHELL_KEYS["button_main_menu"]),
		main_menu, LABEL_FONT, TEXT_BRIGHT)
	_hits.append({"id": "main_menu", "rect": main_menu, "kind": "button"})

	var profile := Rect2(PAD + width + 12.0, y, width, BAR_H - 8.0)
	ChromeScript.draw_steel_button(self, profile, 3)
	_centre_text(_label(BindingsScript.SHELL_KEYS["button_profile"]),
		profile, LABEL_FONT, TEXT_LOCKED)

	var refusal := play_refusal()
	var play := Rect2(size.x - PAD - width, y, width, BAR_H - 8.0)
	ChromeScript.draw_steel_button(self, play,
		3 if not refusal.is_empty() else (1 if _hover_hit == "play" else 2))
	_centre_text(_label(BindingsScript.SHELL_KEYS["button_play"]),
		play, LABEL_FONT, TEXT_LOCKED if not refusal.is_empty() else TEXT_BRIGHT)
	if refusal.is_empty():
		_hits.append({"id": "play", "rect": play, "kind": "button"})

	var note := refusal if not refusal.is_empty() else message
	if not note.is_empty():
		_wrap_text(note,
			Rect2(profile.position.x + width + 16.0, y - 2.0,
				play.position.x - profile.position.x - width - 30.0, BAR_H),
			SMALL_FONT, TEXT_WARN if not refusal.is_empty() else TEXT_DIM)


# --- dropdowns ------------------------------------------------------------------

func _draw_menu_field(id: String, rect: Rect2, text: String, enabled: bool, _reason: String) -> void:
	ChromeScript.draw_dropdown(self, rect, enabled)
	var clip := Rect2(rect.position + Vector2(7.0, 0.0),
		Vector2(maxf(10.0, rect.size.x - 30.0), rect.size.y))
	_clipped_text(text, clip, BODY_FONT, TEXT if enabled else TEXT_LOCKED)
	if enabled:
		_hits.append({"id": id, "rect": rect, "kind": "menu"})


## The options a menu id offers, resolved fresh so a list can never go stale
## against the seating it describes.
func _menu_options(id: String) -> Array[Dictionary]:
	var options: Array[Dictionary] = []
	if id.begins_with("rule:"):
		var row := _rule_row(id.substr(5))
		if row.is_empty():
			return options
		var resolved := _rule_options(row)
		for index in range(resolved.size()):
			options.append({
				"text": String((resolved[index] as Dictionary)["text"]),
				"value": index,
				"enabled": true,
				"note": _rule_state_note(row, index),
			})
		return options
	if id == "scenario":
		for index in range(scenarios.size()):
			var row := scenarios[index] as Dictionary
			options.append({
				"text": _scenario_display(row),
				"value": index,
				"enabled": bool(row["startable"]),
				"note": String(row["reason"]),
			})
		return options
	var parts := id.split("_")
	if parts.size() != 2:
		return options
	var row_index := int(parts[1])
	match String(parts[0]):
		"player":
			options.append({"text": "Player 1", "value": 0, "enabled": true, "note": ""})
			options.append({
				"text": _label(BindingsScript.AI_TIER_KEYS[BindingsScript.AI_TIER_FIXED]),
				"value": 1, "enabled": true,
				"note": BindingsScript.AI_TIER_LOCKED_REASON,
			})
		"army":
			for index in range(seat_options.size()):
				var option := seat_options[index] as Dictionary
				var reason := String(option["unavailable_reason"])
				var taken := false
				for other in range(seats.size()):
					if other != row_index and int(seats[other]["option_index"]) == index:
						taken = true
				options.append({
					"text": _army_label_for_option(index),
					"value": index,
					"enabled": reason.is_empty() and not taken,
					"note": reason if not reason.is_empty() else (
						"already taken by another seat" if taken else ""),
				})
		"team":
			for team in range(1, MAX_SEATS + 1):
				options.append({"text": str(team), "value": team, "enabled": true, "note": ""})
		"handicap":
			# Retail's 21 rungs, in retail's own order. The note is retail's own
			# algebra, which the auto-resolve runner verifies exactly: every
			# rung's ArmorMultiplier is the reciprocal of its WeaponMultiplier.
			for index in range(BindingsScript.HANDICAP_LEVELS.size()):
				options.append({
					"text": "%d%%" % BindingsScript.HANDICAP_LEVELS[index],
					"value": index,
					"enabled": true,
					"note": "" if index == 0 else BindingsScript.HANDICAP_NOTE,
				})
		"color":
			for entry in BindingsScript.COLORS:
				var slot := int(entry["slot"])
				var taken := false
				for other in range(seats.size()):
					if other != row_index and int(seats[other]["color_slot"]) == slot:
						taken = true
				options.append({
					"text": _label(String(entry["name_key"])),
					"value": slot,
					"enabled": not taken,
					"note": "already taken by another seat" if taken else "",
					"swatch": entry["rgb"],
				})
	return options


func _army_label_for_option(index: int) -> String:
	var option := seat_options[index] as Dictionary
	var template := String(option["template"])
	var key := String(BindingsScript.ARMY_SIDE_KEYS.get(template, ""))
	return template if key.is_empty() else _label(key)


func _selected_value(id: String) -> int:
	if id.begins_with("rule:"):
		var row := _rule_row(id.substr(5))
		return _rule_index(row, _rule_options(row).size()) if not row.is_empty() else 0
	if id == "scenario":
		return scenario_index
	var parts := id.split("_")
	if parts.size() != 2:
		return 0
	var row := seats[int(parts[1])] as Dictionary
	match String(parts[0]):
		"player":
			return 0 if String(row["controller"]) == "human" else 1
		"army":
			return int(row["option_index"])
		"team":
			return int(row["team"])
		"color":
			return int(row["color_slot"])
		"handicap":
			return int(row["handicap_index"])
	return 0


func _apply_choice(id: String, value: int) -> void:
	if id.begins_with("rule:"):
		rule_choice[id.substr(5)] = value
		return
	if id == "scenario":
		scenario_index = clampi(value, 0, maxi(scenarios.size() - 1, 0))
		# The seating may no longer fit the new scenario's ownership sets; trim
		# rather than start a session the strategic layer would refuse.
		while seats.size() > maxi(_seat_ceiling(), MIN_SEATS):
			seats.pop_back()
		_refresh_ownership()
		return
	var parts := id.split("_")
	if parts.size() != 2:
		return
	var row := seats[int(parts[1])] as Dictionary
	match String(parts[0]):
		"player":
			row["controller"] = "human" if value == 0 else "ai"
			# Exactly one seat is this machine's, the way the old fixed seating
			# had exactly one. Which seat is a choice; how many is not.
			if value == 0:
				for other in range(seats.size()):
					if other != int(parts[1]):
						(seats[other] as Dictionary)["controller"] = "ai"
		"army":
			row["option_index"] = value
		"team":
			row["team"] = value
		"color":
			row["color_slot"] = value
			_refresh_ownership()
		"handicap":
			row["handicap_index"] = clampi(value, 0, BindingsScript.HANDICAP_LEVELS.size() - 1)
	_refresh_ownership()


const MENU_ROW_H := 24.0


## The popup's geometry, computed the SAME WAY by the painter and the picker.
##
## Returns `{box, first, count}`. The list is SCROLLED, not overflowed: retail's
## own scenario list is 43 entries long and the first version of this drew all
## 43, straight through the seat table and out of the bottom of the window.
func _open_menu_geometry() -> Dictionary:
	var rows := _open_options.size()
	var height := minf(float(rows) * MENU_ROW_H + 8.0, size.y * 0.58)
	var visible := maxi(1, int((height - 8.0) / MENU_ROW_H))
	var top := _open_rect.position.y + _open_rect.size.y + 2.0
	if top + height > size.y - 10.0:
		top = maxf(10.0, _open_rect.position.y - height - 2.0)
	var first := clampi(_open_scroll, 0, maxi(0, rows - visible))
	return {
		"box": Rect2(_open_rect.position.x, top,
			maxf(_open_rect.size.x, 180.0), float(mini(visible, rows)) * MENU_ROW_H + 8.0),
		"first": first,
		"count": mini(visible, rows),
	}


func _draw_open_menu() -> void:
	var rows := _open_options.size()
	if rows <= 0:
		return
	var row_height := MENU_ROW_H
	var geometry := _open_menu_geometry()
	var box := geometry["box"] as Rect2
	var first := int(geometry["first"])
	var count := int(geometry["count"])
	draw_rect(box.grow(3.0), Color(0.0, 0.0, 0.0, 0.85))
	ChromeScript.draw_steel_frame(self, box, 2)
	if rows > count:
		# The scroll indicator: retail's lists carry a bar, and a list that
		# silently hides 30 of its 43 entries is a list that lies.
		var track := Rect2(box.position.x + box.size.x - 6.0, box.position.y + 4.0,
			3.0, box.size.y - 8.0)
		draw_rect(track, Color(0.0, 0.0, 0.0, 0.6))
		var span := track.size.y * float(count) / float(rows)
		draw_rect(Rect2(track.position.x,
			track.position.y + track.size.y * float(first) / float(rows), 3.0, span),
			Color(ChromeScript.STEEL_PALE.r, ChromeScript.STEEL_PALE.g,
				ChromeScript.STEEL_PALE.b, 0.55))
	var y := box.position.y + 4.0
	for index in range(first, mini(first + count, rows)):
		var option := _open_options[index] as Dictionary
		var row := Rect2(box.position.x + 3.0, y, box.size.x - 6.0, row_height)
		var enabled := bool(option.get("enabled", true))
		if index == _hover_option and enabled:
			draw_rect(row, Color(ChromeScript.STEEL_BRIGHT.r, ChromeScript.STEEL_BRIGHT.g,
				ChromeScript.STEEL_BRIGHT.b, 0.30))
		if int(option["value"]) == _open_selected:
			draw_rect(row, Color(ChromeScript.STEEL_PALE.r, ChromeScript.STEEL_PALE.g,
				ChromeScript.STEEL_PALE.b, 0.12))
			draw_rect(row, Color(ChromeScript.STEEL_PALE.r, ChromeScript.STEEL_PALE.g,
				ChromeScript.STEEL_PALE.b, 0.35), false, 1.0)
		var text_x := row.position.x + 8.0
		if option.has("swatch"):
			ChromeScript.draw_swatch(self,
				Rect2(row.position.x + 6.0, row.position.y + 5.0, 20.0, row_height - 10.0),
				option["swatch"] as Color)
			text_x += 28.0
		_clipped_text(String(option["text"]),
			Rect2(Vector2(text_x, row.position.y), Vector2(row.size.x - 16.0, row_height)),
			BODY_FONT, TEXT if enabled else TEXT_LOCKED)
		# A disabled option keeps its reason beside it. It is not hidden: an
		# option that vanished would be a question with no answer on screen.
		var note := String(option.get("note", ""))
		if not note.is_empty() and index == _hover_option:
			var tip := Rect2(box.position.x + box.size.x + 8.0, row.position.y,
				minf(300.0, size.x - box.position.x - box.size.x - 20.0), 46.0)
			draw_rect(tip, Color(0.0, 0.0, 0.0, 0.88))
			_wrap_text(note, tip.grow(-6.0), SMALL_FONT, TEXT_WARN)
		y += row_height


# --- input -----------------------------------------------------------------------

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var at := (event as InputEventMouseMotion).position
		if not _open_menu.is_empty():
			var index := _option_at(at)
			if index != _hover_option:
				_hover_option = index
				queue_redraw()
			return
		var found := ""
		for hit in _hits:
			if (hit["rect"] as Rect2).has_point(at):
				found = String(hit["id"])
		if found != _hover_hit:
			_hover_hit = found
			queue_redraw()
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	if not button.pressed:
		return
	if not _open_menu.is_empty() and button.button_index in [
			MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var step := -3 if button.button_index == MOUSE_BUTTON_WHEEL_UP else 3
		_open_scroll = clampi(_open_scroll + step, 0,
			maxi(0, _open_options.size() - int(_open_menu_geometry()["count"])))
		_hover_option = _option_at(button.position)
		queue_redraw()
		return
	if button.button_index != MOUSE_BUTTON_LEFT:
		return
	if not _open_menu.is_empty():
		var index := _option_at(button.position)
		if index >= 0:
			var option := _open_options[index] as Dictionary
			if bool(option.get("enabled", true)):
				_apply_choice(_open_menu, int(option["value"]))
		_open_menu = ""
		_hover_option = -1
		_relayout()
		queue_redraw()
		return
	for hit in _hits:
		if not (hit["rect"] as Rect2).has_point(button.position):
			continue
		match String(hit["kind"]):
			"tab":
				active_tab = int(hit["index"])
				_relayout()
			"button":
				_press(String(hit["id"]))
			"menu":
				_open_menu = String(hit["id"])
				_open_rect = hit["rect"] as Rect2
				_open_options = _menu_options(_open_menu)
				_open_selected = _selected_value(_open_menu)
				_hover_option = -1
				# Open ON the current choice, not at the top: a 43-entry scenario
				# list that opened at row 0 would hide the row it is showing.
				_open_scroll = 0
				for index in range(_open_options.size()):
					if int((_open_options[index] as Dictionary)["value"]) == _open_selected:
						_open_scroll = maxi(0, index - 2)
						break
		queue_redraw()
		return


func _press(id: String) -> void:
	match id:
		"main_menu":
			back_requested.emit()
		"play":
			var refusal := play_refusal()
			if not refusal.is_empty():
				message = refusal
				return
			play_requested.emit({
				"scenario": scenario_name(),
				"seats": seat_payload(),
				# The campaign-wide RULES choices. A listener that ignores this
				# key gets retail's own screen defaults, which is exactly what
				# the screen was showing.
				"rules": rules_payload(),
			})
		"add_seat":
			if seats.size() >= _seat_ceiling():
				return
			var used: Dictionary = {}
			var used_slots: Dictionary = {}
			for row in seats:
				used[int(row["option_index"])] = true
				used_slots[int(row["color_slot"])] = true
			for index in range(seat_options.size()):
				if used.has(index):
					continue
				if not String(seat_options[index]["unavailable_reason"]).is_empty():
					continue
				var slot := int(BindingsScript.default_color(seats.size())["slot"])
				for entry in BindingsScript.COLORS:
					if not used_slots.has(int(entry["slot"])):
						slot = int(entry["slot"])
						break
				seats.append({
					"option_index": index,
					"team": seats.size() + 1,
					"controller": "ai",
					"color_slot": slot,
					"handicap_index": BindingsScript.HANDICAP_DEFAULT,
				})
				break
			_refresh_ownership()
		"remove_seat":
			if seats.size() <= MIN_SEATS:
				return
			seats.pop_back()
			if not _has_human():
				(seats[0] as Dictionary)["controller"] = "human"
			_refresh_ownership()


func _has_human() -> bool:
	for row in seats:
		if String(row["controller"]) == "human":
			return true
	return false


func _option_at(at: Vector2) -> int:
	var rows := _open_options.size()
	if rows <= 0:
		return -1
	var geometry := _open_menu_geometry()
	var box := geometry["box"] as Rect2
	if not box.has_point(at):
		return -1
	var index := int(geometry["first"]) + int((at.y - box.position.y - 4.0) / MENU_ROW_H)
	return index if index >= 0 and index < rows else -1


func show_message(text: String) -> void:
	message = text
	queue_redraw()


# --- text helpers ----------------------------------------------------------------

func _text(body: String, at: Vector2, font_size: int, tint: Color) -> void:
	if body.is_empty():
		return
	draw_string(_font, at, body, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, tint)


func _clipped_text(body: String, rect: Rect2, font_size: int, tint: Color) -> void:
	if body.is_empty():
		return
	draw_string(_font, rect.position + Vector2(0.0, rect.size.y * 0.5 + float(font_size) * 0.36),
		body, HORIZONTAL_ALIGNMENT_LEFT, rect.size.x, font_size, tint,
		TextServer.JUSTIFICATION_NONE, TextServer.DIRECTION_AUTO, TextServer.ORIENTATION_HORIZONTAL)


func _centre_text(body: String, rect: Rect2, font_size: int, tint: Color) -> void:
	if body.is_empty():
		return
	draw_string(_font, rect.position + Vector2(0.0, rect.size.y * 0.5 + float(font_size) * 0.36),
		body, HORIZONTAL_ALIGNMENT_CENTER, rect.size.x, font_size, tint)


func _wrap_text(body: String, rect: Rect2, font_size: int, tint: Color) -> void:
	if body.is_empty() or rect.size.x <= 12.0:
		return
	draw_multiline_string(_font, rect.position + Vector2(0.0, float(font_size) + 2.0),
		body, HORIZONTAL_ALIGNMENT_LEFT, rect.size.x, font_size,
		maxi(1, int(rect.size.y / (float(font_size) + 4.0))), tint)
