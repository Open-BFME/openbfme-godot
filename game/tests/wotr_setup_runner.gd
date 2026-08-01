extends SceneTree

## THE WAR OF THE RING GAME SETUP SCREEN, asserted.
##
## Two runs, both required, and the LIVENESS CONSTANTS below are what makes the
## second one worth anything: a runner that "passes" because it skipped its
## checks is the failure this project keeps designing against.
##
##   with a living-world document : EXPECTED_WITH_DOCUMENT checks
##   with none                    : EXPECTED_WITHOUT_DOCUMENT checks
##
## WHAT IT ASSERTS THAT MATTERS:
##
## * Every label the screen shows resolves out of retail's own string table, and
##   the ones that do not are named BY NAME - not counted against a tolerance.
##   `unresolved_keys` must be EMPTY, and the assertion prints the keys when it
##   is not, because "3 of 17 resolved" tells nobody which three.
## * Retail's five rule rows carry retail's five `RULE:` labels and retail's own
##   `VALUE:` options, and NO ROW HAS AN OPTION THAT IS NOT A RETAIL KEY. The
##   timer row offers exactly one option, because exactly one is in a shipped
##   file.
## * The scenario list is the document's whole campaign, the unstartable ones
##   are present AND carry a reason, and PLAY refuses on one of them.
## * PLAY reaches `WotrSession.begin()` and the SESSION decides - the seats and
##   scenario the screen chose are the ones the strategic state ends up holding,
##   and a seating the layer refuses is refused rather than worked around.
## * Nothing chosen on the screen leaves it except scenario and seats. The
##   payload is asserted to carry those three fields per seat AND NOTHING ELSE,
##   which is the mechanical form of "no setting sneaks past the commitment".
##
## Usage:
##   Godot_v4.7 --path game --headless --script tests/wotr_setup_runner.gd

const SetupScreenScript = preload("res://src/ui/wotr_setup_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const BindingsScript = preload("res://src/wotr/wotr_setup_bindings.gd")
const WorldScript = preload("res://src/wotr/wotr_world.gd")

## 45 as it stood, minus the two rows that no longer state a locked reason
## because they are no longer locked (battle_type, battle_type_priority), plus
## three: the handicap rungs are on retail's ladder, the RULES payload carries
## exactly the rows that reach the strategic layer, and the handicap column's
## DECLARED reach matches what the payload really carries. 45 - 2 + 3 = 46.
## Round two adds three more: every act-army hero resolves to retail's display
## name, every colour block carries BOTH retail colours (lobby chip and
## strategic tint) and they differ, and the opening seats wear the reference
## capture's own colour order. 46 + 3 = 49.
## Round two's second pass adds three more, all about the ground the map preview
## stands its territories on: the terrain is retail's own living-map tiles or its
## absence is on the screen's own list, no tile is drawn without the colour map
## retail bound to it, and every tile is placed by retail's authored UVs rather
## than by a projection this screen invented. 49 + 3 = 52.
##
## ROUND THREE ADDS NINE, and every one of them pins something the blind
## adversarial read caught:
##   * the screen opens on retail's OWN default - the freeform "War of the Ring"
##     - rather than falling through to the first preset scenario, which is what
##     put all 52 regions under a saturated colour wash;
##   * the freeform seats are seeded from the SCENARIO'S OWN `defaultStartSpots`
##     and no further, so nothing is dealt out that retail did not author;
##   * PLAY refuses a freeform scenario until every seat has a start territory,
##     and is offered once they do;
##   * the session refuses start regions alongside a scenario that authors its
##     own ownership, and refuses two seats in one territory;
##   * the map shades EXACTLY the seats' start territories on a freeform
##     scenario and exactly the ownership sets on a preset one;
##   * the masthead is set in retail's own display face or its absence is NAMED;
##   * no string on the player surface names this project (the footer developer
##     line is gone and the absence list is behind F1, which still holds it);
##   * the Scenario Description carries retail's victory block, not a trimmed
##     copy that happens to fit;
##   * the masthead face is bound or NAMED, F1 really opens and closes the
##     absence list, and the human seat's name is a neutral default rather than
##     this machine's account.
## Twelve in all, and none of them replaces an old one - the two checks that
## changed (the freeform refusal and PLAY being offered) kept their slots and
## only their conditions moved. 52 + 12 = 64.
const EXPECTED_WITH_DOCUMENT := 64
const EXPECTED_WITHOUT_DOCUMENT := 6

var _passed := 0
var _failed := 0
var _ran := 0


func _initialize() -> void:
	var found: Dictionary = SessionScript.locate_document([])
	if bool(found.get("ok", false)):
		_with_document(found)
		_finish(EXPECTED_WITH_DOCUMENT)
	else:
		_without_document(String(found.get("reason", "")))
		_finish(EXPECTED_WITHOUT_DOCUMENT)
	quit(1 if _failed > 0 else 0)


# --- with a document -----------------------------------------------------------

func _with_document(found: Dictionary) -> void:
	var document: Dictionary = found["document"] as Dictionary
	var world := WorldScript.new()
	if not world.load_from_dict(document, ""):
		_check("the document loads", false, str(world.errors))
		return
	var probe := SessionScript.new()
	probe.world = world
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""

	var screen = SetupScreenScript.new()
	root.add_child(screen)
	screen.size = Vector2(1860.0, 900.0)
	screen.pack_faction_availability = availability
	screen.configure(document, probe, [], "")
	for line in screen.describe_load():
		print("[wotr-setup] %s" % String(line))

	_bundles(screen)
	_labels(screen)
	_rules(screen)
	_scenarios(screen, world, probe)
	_seats(screen)
	_surface(screen)
	_map(screen, world)
	_play(screen, document, world)
	screen.queue_free()


## THE PLAYER SURFACE: what a stranger reads, and what they must not.
func _surface(screen) -> void:
	# THE MASTHEAD FACE. Omnia LT Std is a PROVEN binding now - rendered against
	# the oracle's own glyph mask at 0.83 intersection-over-union, against 0.18 for
	# Albertus MT and 0.17 for SachaWynterTight - so either the file is loaded or
	# the screen NAMES its absence. What it may never do again is silently set the
	# game's own title in a lookalike serif, which the blind read identified as the
	# single decisive tell.
	var display_named := false
	for line in screen.absences:
		if String(line) == screen.display_font_reason and not screen.display_font_reason.is_empty():
			display_named = true
			break
	_check("the masthead is set in retail's own display face, or its absence is named",
		(screen._display_font != null and screen.display_font_reason.is_empty())
			or display_named,
		screen.display_font_reason if not screen.display_font_reason.is_empty()
			else "loaded but a reason is recorded")
	# THE ABSENCE LIST IS REACHABLE AND IS NOT ON THE SCREEN. Round two printed
	# "OPEN BFME - 3 NOTES ON WHAT HERE IS NOT RETAIL'S" into the bottom-left of
	# the player table; the blind read put it first on its defect list and said
	# "This alone ends the conversation." It is now behind F1 - closed on arrival,
	# still complete, still one key away.
	_check("the absence list is complete and closed until it is asked for",
		not screen.show_absences and not screen.absence_lines().is_empty(),
		"open=%s, %d line(s)" % [str(screen.show_absences), screen.absence_lines().size()])
	screen.toggle_absences()
	var opened: bool = screen.show_absences
	screen.toggle_absences(false)
	_check("F1's toggle really opens it and really closes it again",
		opened and not screen.show_absences, "")
	# THE HUMAN SEAT'S NAME IS NEUTRAL. Round two showed the OS account name, so
	# every capture shipped the developer's first name in seat one.
	var profile := String(screen._player_name())
	var account := OS.get_environment("USERNAME").strip_edges()
	_check("the human seat carries a neutral profile name, not this machine's account",
		profile == screen.DEFAULT_PROFILE_NAME
			and (account.is_empty() or profile != account),
		"seat shows '%s', account is '%s'" % [profile, account])
	# THE SCENARIO DESCRIPTION IS RETAIL'S IN FULL. The blind read caught the
	# shortfall as content: "A omits the victory-condition block entirely" - and
	# the reason it was omitted was that there was no scrollbar to overflow into.
	var row := screen.scenarios[screen.scenario_index] as Dictionary
	var body := String(screen._scenario_description(row))
	var victory: Dictionary = screen._selected_victory_type()
	var type_text := ""
	var objectives := ""
	if not victory.is_empty():
		type_text = String(screen._label(String(victory.get("label_key", ""))))
		objectives = String(screen._string_or_key(String(victory.get("objectives_key", ""))))
	_check("the scenario description carries retail's own victory block, not a trimmed copy",
		not victory.is_empty() and not type_text.is_empty()
			and body.contains(type_text) and body.contains(objectives.replace("\\n", "\n")),
		body)


func _bundles(screen) -> void:
	_check("the setup string bundle loaded",
		screen.setup_strings != null and screen.setup_strings.loaded,
		screen.setup_strings_reason)
	_check("the strategic string bundle loaded",
		screen.strings != null and screen.strings.loaded, screen.strings_reason)
	# The bundle is a converted artefact and its own schema check already ran;
	# what matters here is that it carries the namespaces THIS screen needs.
	var wanted := [
		"APT:GameSetup", "APT:TabMap", "APT:TabRules", "Apt:Scenario",
		"APT:Side", "APT:HeaderTeam", "APT:HdrHandicap", "APT:StartGame",
		"SIDE:Wild", "Color:Blue", "GUI:MediumAI",
	]
	var absent: Array[String] = []
	for key in wanted:
		if not screen.setup_strings.has(String(key)):
			absent.append(String(key))
	_check("the setup bundle carries every namespace the screen reads",
		absent.is_empty(), "absent: %s" % ", ".join(absent))
	# Retail's own trap, asserted so a later "tidy-up" cannot fold the case.
	_check("Apt:Scenario is the lowercase spelling and APT:Scenario does not exist",
		screen.setup_strings.has("Apt:Scenario") and not screen.setup_strings.has("APT:Scenario"),
		"retail writes this one key with a lowercase tail")
	_check("SIDE:Wild reads Goblins, so the Army column is not derived from the id",
		screen.setup_strings.text("SIDE:Wild") == "Goblins",
		screen.setup_strings.text("SIDE:Wild"))


func _labels(screen) -> void:
	# EVERY SHELL KEY, resolved. Named individually on failure.
	var absent: Array[String] = []
	for element in BindingsScript.SHELL_KEYS.keys():
		var key := String(BindingsScript.SHELL_KEYS[element])
		if screen.setup_strings.text(key).is_empty():
			absent.append("%s (%s)" % [key, String(element)])
	_check("every shell element resolves to retail text",
		absent.is_empty(), "unresolved: %s" % ", ".join(absent))
	_check("the Army column header is retail's APT:Side, which reads Army",
		screen.setup_strings.text(String(BindingsScript.SHELL_KEYS["column_army"])) == "Army",
		"")
	_check("nothing on the screen fell back to a raw retail key",
		screen.unresolved_keys.is_empty(),
		"keys shown raw: %s" % ", ".join(Array(screen.unresolved_keys)))


func _rules(screen) -> void:
	_check("retail's five rule rows are the five on the tab",
		BindingsScript.RULE_ROWS.size() == 5, str(BindingsScript.RULE_ROWS.size()))
	# RETAIL'S TWELVE `RULE:` KEYS, ALL TWELVE ACCOUNTED FOR. The arithmetic, in
	# full: 5 dropdown rows on this tab + 7 entries in `UNUSED_RULE_KEYS` = 12,
	# and two of those seven (`AllowCustomHeroes`, `AllowRingHeroes`) are the
	# checkboxes, whose own entries say "shown here, as a checkbox". So the
	# screen shows 5 + 2 = 7 and names the other 5 as belonging elsewhere.
	var shown_as_checkbox := 0
	for key in BindingsScript.UNUSED_RULE_KEYS.keys():
		if String(BindingsScript.UNUSED_RULE_KEYS[key]).begins_with("shown here"):
			shown_as_checkbox += 1
	_check("retail's twelve RULE: keys are all accounted for",
		BindingsScript.RULE_ROWS.size() + BindingsScript.UNUSED_RULE_KEYS.size() == 12
			and shown_as_checkbox == BindingsScript.RULE_CHECKBOXES.size(),
		"%d rows + %d other entries, %d of them the checkboxes" % [
			BindingsScript.RULE_ROWS.size(), BindingsScript.UNUSED_RULE_KEYS.size(),
			shown_as_checkbox])
	var invented: Array[String] = []
	for row in BindingsScript.RULE_ROWS:
		for key_value in row["values"] as Array:
			var key := String(key_value)
			if not key.begins_with("VALUE:"):
				invented.append("%s -> %s" % [String(row["id"]), key])
			elif not screen.strings.has(key):
				invented.append("%s -> %s (not in the table)" % [String(row["id"]), key])
	_check("no rule row offers an option that is not a retail VALUE: key",
		invented.is_empty(), ", ".join(invented))
	# THE ABSENCE, ASSERTED. The timer row must offer exactly the one option
	# retail actually ships, and never a number someone typed in.
	var timer := BindingsScript.RULE_ROWS[0] as Dictionary
	_check("the timer row offers only VALUE:NoTimer and names what is missing",
		(timer["values"] as Array).size() == 1
			and String((timer["values"] as Array)[0]) == "VALUE:NoTimer"
			and not String(timer["absent_reason"]).is_empty(),
		str(timer["values"]))
	for row in BindingsScript.RULE_ROWS:
		if not String(row.get("reaches", "")).is_empty():
			continue
		_check("the %s row states why it is locked" % String(row["id"]),
			not String(row.get("locked_reason", "")).is_empty(), "")
	_check("both checkboxes carry a retail RULE: label",
		BindingsScript.RULE_CHECKBOXES.size() == 2
			and screen.strings.has(String(BindingsScript.RULE_CHECKBOXES[0]["label_key"]))
			and screen.strings.has(String(BindingsScript.RULE_CHECKBOXES[1]["label_key"])),
		"")
	# The victory row is data-dependent, and the honest answer for BFME2 is that
	# there is none. Either it offers RotWK's list or it offers NOTHING WITH A
	# REASON - never an invented list.
	var victory: Dictionary = BindingsScript.victory_options(
		screen.document.get("victoryTypes", []) as Array)
	var options: Array = victory["options"] as Array
	_check("victory types are retail's ordered seven or an empty list with a reason",
		(options.is_empty() and not String(victory["reason"]).is_empty())
			or options.size() == BindingsScript.VICTORY_TYPES.size(),
		"%d option(s), reason: %s" % [options.size(), String(victory["reason"])])
	_check("the transcribed victory table is retail's seven in file order",
		BindingsScript.VICTORY_TYPES.size() == 7
			and String(BindingsScript.VICTORY_TYPES[0]["label_key"]) == "LWScenario:WOTRGameType002",
		"")


func _scenarios(screen, world, probe) -> void:
	var in_campaign := 0
	for name in world.scenario_names:
		if String(world.scenario(String(name)).get("region_campaign", "")) == world.campaign_name:
			in_campaign += 1
	_check("the list is the campaign's whole scenario set",
		screen.scenarios.size() == in_campaign,
		"%d listed, %d in campaign" % [screen.scenarios.size(), in_campaign])
	# `true` = INCLUDE THE FREEFORM ONES, which is what the screen asks for. A
	# freeform scenario is startable when every seat can be given a territory, and
	# `startable_scenarios(2)` alone still means what it always meant.
	var startable := Array(probe.startable_scenarios(2, true))
	var mislabelled: Array[String] = []
	var reasonless: Array[String] = []
	for row in screen.scenarios:
		var name := String(row["name"])
		if bool(row["startable"]) != startable.has(name):
			mislabelled.append(name)
		if not bool(row["startable"]) and String(row["reason"]).is_empty():
			reasonless.append(name)
	_check("startable matches the session's own answer, scenario by scenario",
		mislabelled.is_empty(), ", ".join(mislabelled))
	_check("every unstartable scenario carries the reason it cannot start",
		reasonless.is_empty(), ", ".join(reasonless))
	_check("the screen opens on a scenario that can actually start",
		not screen.scenarios.is_empty()
			and bool(screen.scenarios[screen.scenario_index]["startable"]),
		"")
	# RETAIL'S HEADLINE SCENARIO IS FREEFORM AND IT IS THE ONE THE SCREEN OPENS ON.
	# Round two could not start it, so the cursor fell through to the first PRESET
	# scenario - `WOTRScenario007`, whose retail display name really is "War of the
	# Ring (6, T)" and whose six ownership sets claim all 52 regions. Both of the
	# blind read's disqualifying MAP-tab findings (the raw `(6, T)` tuple in the
	# dropdown and the saturated flood over the whole landmass) were that one
	# fallback, so this pins the cursor where retail puts it.
	var opened_row := screen.scenarios[screen.scenario_index] as Dictionary
	_check("the screen opens on retail's own freeform default scenario",
		bool(opened_row.get("freeform", false))
			and int(opened_row.get("ownership_sets", -1)) == 0,
		"opened on %s (%d ownership set(s), freeform=%s)" % [
			String(opened_row.get("name", "")), int(opened_row.get("ownership_sets", -1)),
			str(opened_row.get("freeform", false))])
	# AND ITS DISPLAY NAME IS A NAME, not an internal tuple. This is the string a
	# stranger reads first on the MAP tab.
	_check("the scenario dropdown shows a display name with no internal tuple in it",
		not screen._scenario_display(opened_row).contains("(")
			and not screen._scenario_display(opened_row).contains(","),
		screen._scenario_display(opened_row))
	# THE SEATS ARE SEEDED FROM THE SCENARIO'S OWN `defaultStartSpots` AND NO
	# FURTHER. Every seeded start must BE one of the authored spots, in the
	# document's own order, and every seat past them must be EMPTY - dealing out a
	# region retail did not author is precisely the invented parity this forbids.
	var spots: PackedStringArray = opened_row.get("default_start_spots", PackedStringArray())
	var seeded_wrong: Array[String] = []
	for index in range(screen.seats.size()):
		var start := String(screen.seat_starts[index]) if index < screen.seat_starts.size() else ""
		if index < spots.size():
			if start != String(spots[index]):
				seeded_wrong.append("seat %d seeded '%s', scenario authors '%s'" % [
					index, start, String(spots[index])])
		elif not start.is_empty():
			seeded_wrong.append("seat %d was given '%s' and the scenario authors no spot for it"
				% [index, start])
	_check("freeform seats are seeded from the scenario's own defaultStartSpots and no further",
		seeded_wrong.is_empty() and not spots.is_empty(),
		", ".join(seeded_wrong) if not seeded_wrong.is_empty() else "the scenario authors no spots")
	# PLAY REFUSES UNTIL EVERY SEAT HAS ONE, and the refusal says how many are
	# missing and which seat is next.
	var refusal := String(screen.play_refusal())
	_check("a freeform scenario refuses PLAY until every seat has a start territory",
		not refusal.is_empty() and refusal.to_lower().contains("freeform"), refusal)


func _seats(screen) -> void:
	_check("the screen seats at least two players to begin with",
		screen.seats.size() >= 2, str(screen.seats.size()))
	_check("exactly one seat is this machine's",
		_humans(screen) == 1, str(_humans(screen)))
	_check("every army the document offers is listed, fieldable or not",
		screen.seat_options.size() > 0, "")
	# An army another seat already holds is DISABLED WITH A NOTE, not hidden.
	var options: Array = screen._menu_options("army_0")
	var taken := 0
	for option in options:
		if not bool((option as Dictionary)["enabled"]):
			taken += 1
			_check_once("a taken or unconverted army is offered disabled with a note",
				not String((option as Dictionary)["note"]).is_empty(), "")
	_check("the army list shows every option including the unavailable ones",
		options.size() == screen.seat_options.size(),
		"%d listed, %d offered" % [options.size(), screen.seat_options.size()])
	_check("the colour list is the six AvailableInWotR blocks of multiplayer.ini",
		BindingsScript.COLORS.size() == 6, str(BindingsScript.COLORS.size()))
	# BOTH retail colours per block, and they must DIFFER: `ui` is the muted
	# RGBColor lobby chip and `rgb` the saturated LivingWorldColor map tint.
	# On all six WotR blocks retail authors them apart, and a table where they
	# match is a table someone flattened back to one neon set.
	var flattened: Array[String] = []
	for entry in BindingsScript.COLORS:
		var record := entry as Dictionary
		if not record.has("ui") or not record.has("rgb") \
				or (record["ui"] as Color).is_equal_approx(record["rgb"] as Color):
			flattened.append(String(record.get("block", "?")))
	_check("every colour block carries retail's lobby chip AND its map tint, distinct",
		flattened.is_empty(), ", ".join(flattened))
	# The opening table wears the retail capture's own colours, top to bottom:
	# Gold, Red, Blue, Green, Orange, Purple by block - NOT slot order, which
	# was round one's guess and repainted the whole opening table.
	var miscoloured: Array[String] = []
	for index in range(screen.seats.size()):
		var expected_slot: int = BindingsScript.DEFAULT_COLOR_SLOTS[
			index % BindingsScript.DEFAULT_COLOR_SLOTS.size()]
		if int(screen.seats[index]["color_slot"]) != expected_slot:
			miscoloured.append("seat %d wears slot %d, capture shows %d" % [
				index, int(screen.seats[index]["color_slot"]), expected_slot])
	_check("the opening seats wear the retail capture's colour order",
		miscoloured.is_empty(), ", ".join(miscoloured))
	# EVERY act-army hero the document names resolves to retail text through
	# HERO_DISPLAY_KEYS and the OBJECT: entries of the setup bundle. A raw
	# template id in the Hero column was the loudest in-development tell on
	# the first capture, and this pins it shut BY NAME.
	_check("every act-army hero resolves to a retail display name",
		screen.hero_name_misses.is_empty(),
		"unresolved heroes: %s" % ", ".join(Array(screen.hero_name_misses)))
	_check("the handicap ladder is retail's 21 levels from 0 to 100",
		BindingsScript.HANDICAP_LEVELS.size() == 21
			and BindingsScript.HANDICAP_LEVELS[0] == 0
			and BindingsScript.HANDICAP_LEVELS[20] == 100,
		str(BindingsScript.HANDICAP_LEVELS.size()))
	# Changing an army must actually change the payload - a control that draws a
	# new value and hands over the old one is the worst kind of working screen.
	var before := String((screen.seat_payload()[0] as Dictionary)["template"])
	var other := -1
	for option in screen._menu_options("army_0"):
		if bool((option as Dictionary)["enabled"]) \
				and int((option as Dictionary)["value"]) != int(screen.seats[0]["option_index"]):
			other = int((option as Dictionary)["value"])
			break
	if other >= 0:
		screen._apply_choice("army_0", other)
		_check("choosing an army changes the seat the session would be given",
			String((screen.seat_payload()[0] as Dictionary)["template"]) != before, "")
	else:
		_check("choosing an army changes the seat the session would be given",
			false, "no second fieldable army to switch to")


func _humans(screen) -> int:
	var total := 0
	for row in screen.seats:
		if String(row["controller"]) == "human":
			total += 1
	return total


func _map(screen, world) -> void:
	var preview = screen.map_preview
	_check("the map preview exists before the screen is ever drawn", preview != null, "")
	if preview == null:
		return
	# EVERY REGION THE DOCUMENT DECLARES IS EITHER DRAWN OR NAMED. Not counted:
	# named, so "which one is missing" has an answer.
	var accounted: int = preview.drawn_regions.size() + preview.regions_without_geometry.size()
	_check("every declared region is either shaded or named as shapeless",
		accounted == world.region_ids.size(),
		"%d drawn + %d named = %d, document declares %d" % [
			preview.drawn_regions.size(), preview.regions_without_geometry.size(),
			accounted, world.region_ids.size()])
	_check("retail's territory shapes are what the preview draws",
		preview.drawn_regions.size() > 0, preview.unavailable_reason)

	# THE GROUND UNDER THE SHAPES IS RETAIL'S TOO, or the screen says it is not.
	# These three are the mechanical form of "the map preview is retail's painted
	# Middle-earth, not a picture of it and not a stand-in colour":
	#   * either every terrain tile came from the living-map bundle, or the
	#     preview carries the reason it has none AND the screen repeats it on the
	#     absence list - never a silently empty map;
	#   * no tile is drawn without a texture retail's own bundle bound to it,
	#     which is what a procedural fallback would look like from here;
	#   * the UV list is the same length as the vertex list on every tile, so the
	#     texture is being placed by retail's authored coordinates rather than by
	#     a projection this screen invented.
	var terrain_reason := String(preview.terrain_reason)
	var terrain_named := false
	for line in screen.absences:
		if String(line).contains(terrain_reason) and not terrain_reason.is_empty():
			terrain_named = true
			break
	_check("the preview's terrain is retail's own, or its absence is on the screen",
		(terrain_reason.is_empty() and preview._terrain.size() > 0) or terrain_named,
		terrain_reason if not terrain_reason.is_empty() else "no tile survived flattening")
	var untextured: PackedStringArray = preview.terrain_tiles_without_texture
	_check("no terrain tile is drawn without the colour map retail bound to it",
		untextured.is_empty(), ", ".join(Array(untextured)))
	var uvs_authored := true
	for tile in preview._terrain:
		var record := tile as Dictionary
		if (record["uvs"] as PackedVector2Array).size() \
				!= (record["points"] as PackedVector2Array).size():
			uvs_authored = false
			break
	_check("every terrain tile is textured by retail's authored UVs, one per vertex",
		uvs_authored, "a tile's UV count does not match its vertex count")
	_check("the preview shades exactly the regions the scenario claims",
		_ownership_matches(screen), "")
	# THE FLOOD GUARD. On the scenario the screen OPENS ON, the tinted territories
	# must be a light annotation on retail's painting - one per seat at most, which
	# is what a freeform start is - and never the whole landmass. Round two opened
	# on a preset scenario that claims all 52 regions for six seats, and the blind
	# read called the result "in effect, a diagnostic readout rendered as
	# production art. Disqualifying."
	var shaded: int = screen.map_preview.owner_colors.size()
	_check("the opening scenario tints no more territories than it has seats",
		shaded <= screen.seats.size(),
		"%d of %d regions tinted for %d seats" % [
			shaded, world.region_ids.size(), screen.seats.size()])
	# The territory panel is empty until a region is pointed at, exactly as
	# retail's is, and then carries the region's own converted name.
	_check("the territory panel is empty before anything is hovered",
		screen._territory_text.is_empty(), screen._territory_text)
	if not preview.drawn_regions.is_empty():
		var region_id := String(preview.drawn_regions[0])
		screen._on_region_hovered(region_id)
		_check("hovering a territory describes it from the document",
			not screen._territory_text.is_empty()
				and screen._territory_text.split("\n").size() >= 2,
			screen._territory_text)


func _ownership_matches(screen) -> bool:
	var expected: Dictionary = {}
	var sets: Array = screen._selected_scenario_record().get("ownership_sets", []) as Array
	for index in range(mini(sets.size(), screen.seats.size())):
		for region_value in (sets[index] as Dictionary).get("regions", PackedStringArray()) as PackedStringArray:
			expected[String(region_value)] = true
	# A FREEFORM SCENARIO CLAIMS NOTHING, so what it shades is the seats' own
	# start territories - and those are the only other thing it may shade.
	for index in range(screen.seat_starts.size()):
		var start := String(screen.seat_starts[index])
		if not start.is_empty() and index < screen.seats.size():
			expected[start] = true
	var shaded: Dictionary = screen.map_preview.owner_colors
	if expected.size() != shaded.size():
		return false
	for key in expected.keys():
		if not shaded.has(key):
			return false
	return true


## PLAY. The one path that matters: what the screen chose is what the strategic
## state ends up holding, and the session still decides.
func _play(screen, document: Dictionary, world) -> void:
	# GIVE EVERY SEAT A START TERRITORY, through the screen's own picker rather
	# than by writing `seat_starts` here, so what is asserted below is a state the
	# screen can actually be driven into. The regions are the first drawn ones no
	# seat holds; the runner has no authored answer either and must not invent a
	# better one than the screen would accept.
	if screen._scenario_is_freeform():
		for region_value in screen.map_preview.drawn_regions:
			if screen._next_unplaced_seat() < 0:
				break
			screen._pick_start_region(String(region_value))
	_check("placing a start territory for every seat makes PLAY offered",
		screen.play_refusal().is_empty(), screen.play_refusal())
	var payload: Array = screen.seat_payload()
	var extra: Array[String] = []
	for seat_value in payload:
		var seat := seat_value as Dictionary
		for key in seat.keys():
			if not ["template", "team", "controller", "handicap"].has(String(key)):
				extra.append(String(key))
	# NOTHING SNEAKS PAST. Colour and every still-locked rule row stay on this
	# screen. HANDICAP has joined the payload, and it is not sneaking: it is
	# authoritative strategic state on `players[].handicap`, it rides the hash,
	# and it reaches a battle only through the version-3 commitment's own
	# `attacker_handicap` / `defender_handicap` fields - never as a side
	# argument, which is the shape that caused the two desyncs this branch fixed.
	_check("the seat payload carries template, team, controller and handicap and nothing else",
		extra.is_empty(), "extra fields: %s" % ", ".join(extra))
	var rungs: Array[String] = []
	for seat_value in payload:
		if not StateScript.is_authored_handicap(int((seat_value as Dictionary)["handicap"])):
			rungs.append(str((seat_value as Dictionary)["handicap"]))
	_check("every handicap in the payload is a rung retail actually authored",
		rungs.is_empty(), "off-ladder: %s" % ", ".join(rungs))
	# THE RULES TAB'S CHOICES TRAVEL TOO, in the strategic layer's vocabulary
	# rather than as `VALUE:` keys, and ONLY the rows that really reach it.
	var rules: Dictionary = screen.rules_payload()
	_check("the rules payload carries exactly the two rows that reach the strategic layer",
		rules.size() == 2 and StateScript.BATTLE_TYPES.has(String(rules.get("battle_type", "")))
			and StateScript.BATTLE_PRIORITIES.has(String(rules.get("battle_type_priority", ""))),
		str(rules))
	# WHAT THE TABLE CLAIMS AND WHAT THE PAYLOAD DOES MUST AGREE. `reaches` is
	# read by the screen to decide whether a control is live and by a reader to
	# decide whether to believe it; mutation M9 set the handicap column back to
	# "nothing carries it" and every check stayed green while the payload went on
	# carrying it. A column that is live in fact and locked in the table is worse
	# than either, because the table is what a reviewer trusts.
	var declared := String(BindingsScript.SEAT_COLUMN_REACH.get("handicap", ""))
	var carried := (payload[0] as Dictionary).has("handicap")
	_check("the handicap column's declared reach matches what the payload actually carries",
		(declared == "strategic") == carried and declared == "strategic",
		"table says '%s', payload carries it: %s" % [declared, str(carried)])

	var starts: PackedStringArray = screen.start_regions()
	var session := SessionScript.new()
	var began: bool = session.begin(
		document, world.campaign_name, screen.scenario_name(), payload, {}, starts)
	_check("the session begins on exactly what the screen chose",
		began, ", ".join(Array(session.refusals)))
	if not began:
		return
	# THE FREEFORM START REALLY CLAIMED THE TERRITORIES. A start the state did not
	# apply would leave every region neutral and the campaign unplayable, and the
	# screen would have no way to know.
	if not starts.is_empty():
		var unclaimed: Array[String] = []
		for index in range(starts.size()):
			if session.state.owner_of(String(starts[index])) != index:
				unclaimed.append("%s is owned by %d, not seat %d" % [
					String(starts[index]), session.state.owner_of(String(starts[index])), index])
		_check("every seat's freeform start territory is that seat's in the strategic state",
			unclaimed.is_empty(), ", ".join(unclaimed))
	else:
		_check("every seat's freeform start territory is that seat's in the strategic state",
			false, "the screen offered no start regions to check")
	# TWO SOURCES OF TRUTH ARE REFUSED. A preset scenario authors where every seat
	# begins; handing it start regions as well is how a campaign comes to disagree
	# with the picture of itself, so the session refuses rather than merging.
	var preset := ""
	for name in world.scenario_names:
		if not (world.scenario(String(name)).get("ownership_sets", []) as Array).is_empty():
			preset = String(name)
			break
	var mixed := SessionScript.new()
	_check("the session refuses start regions alongside a scenario that authors ownership",
		not preset.is_empty() and not mixed.begin(
			document, world.campaign_name, preset, payload, {},
			PackedStringArray(["Mordor", "Rivendell"])),
		"preset scenario: %s" % preset)
	# AND IT REFUSES A FREEFORM START THAT PUTS TWO SEATS IN ONE TERRITORY, rather
	# than letting the second one quietly take it off the first.
	var doubled: Array[String] = []
	for index in range(payload.size()):
		doubled.append(String(starts[0]) if not starts.is_empty() else "Mordor")
	var clashing := SessionScript.new()
	_check("the session refuses a freeform start with two seats in one territory",
		not clashing.begin(document, world.campaign_name, screen.scenario_name(),
			payload, {}, PackedStringArray(doubled)),
		", ".join(Array(clashing.refusals)))
	_check("the strategic state holds the screen's scenario",
		session.scenario_name == screen.scenario_name(),
		"%s vs %s" % [session.scenario_name, screen.scenario_name()])
	var mismatched: Array[String] = []
	for index in range(payload.size()):
		var chosen := payload[index] as Dictionary
		var seated := session.state.players[index] as Dictionary
		if String(seated.get("template", "")) != String(chosen["template"]):
			mismatched.append("seat %d template" % index)
		if int(seated.get("team", -1)) != int(chosen["team"]):
			mismatched.append("seat %d team" % index)
		if String(seated.get("controller", "")) != String(chosen["controller"]):
			mismatched.append("seat %d controller" % index)
	_check("every seat the screen chose is the seat the state holds",
		mismatched.is_empty(), ", ".join(mismatched))
	# THE SESSION STILL REFUSES. One seat is not a session, whatever the screen
	# put on screen, and this asserts the refusal is the session's own.
	var lone := SessionScript.new()
	_check("the session still refuses a seating it will not accept",
		not lone.begin(document, world.campaign_name, screen.scenario_name(),
			[payload[0]]),
		"")


# --- with no document ------------------------------------------------------------

func _without_document(reason: String) -> void:
	var screen = SetupScreenScript.new()
	root.add_child(screen)
	screen.size = Vector2(1860.0, 900.0)
	screen.configure({}, null, [], reason)
	for line in screen.describe_load():
		print("[wotr-setup] %s" % String(line))
	# IT OPENS. The strategic page refuses without a document, because a map with
	# no world is a blank Middle-earth; this screen is the surface that can say
	# WHICH FILE IS MISSING, so it builds itself and draws the sentence.
	_check("the screen builds itself and holds no world",
		screen.map_preview != null and screen.world == null, "")
	_check("it carries the document search's own reason",
		screen.unavailable_reason == reason and not reason.is_empty(), reason)
	_check("the reason names a way out",
		reason.to_lower().contains("openbfme") or reason.to_lower().contains("import"),
		reason)
	_check("PLAY refuses and says the same thing",
		screen.play_refusal() == reason, screen.play_refusal())
	_check("no scenario is offered from nothing", screen.scenarios.is_empty(), "")
	_check("no seat is invented", screen.seats.is_empty(), "")
	screen.queue_free()


# --- harness ------------------------------------------------------------------------

var _once: Dictionary = {}


func _check_once(name: String, condition: bool, detail: String) -> void:
	if _once.has(name):
		return
	_once[name] = true
	_check(name, condition, detail)


func _check(name: String, condition: bool, detail: String) -> void:
	_ran += 1
	if condition:
		_passed += 1
		print("  PASS  %s" % name)
	else:
		_failed += 1
		print("  FAIL  %s :: %s" % [name, detail])


func _finish(expected: int) -> void:
	print("WOTR_SETUP checks run %d (expected %d), passed %d, failed %d" % [
		_ran, expected, _passed, _failed])
	if _ran != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			_ran, expected])
		_failed += 1
