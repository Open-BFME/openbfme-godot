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

const EXPECTED_WITH_DOCUMENT := 45
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
	_map(screen, world)
	_play(screen, document, world)
	screen.queue_free()


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
	var startable := Array(probe.startable_scenarios(2))
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
	# Retail's headline scenario claims nothing and therefore cannot start. It
	# must still be OFFERED - hiding it would answer "where is War of the Ring"
	# with silence - and PLAY must refuse it by name.
	var freeform := -1
	for index in range(screen.scenarios.size()):
		if int(screen.scenarios[index]["ownership_sets"]) == 0:
			freeform = index
			break
	if freeform >= 0:
		var opened: int = screen.scenario_index
		screen._apply_choice("scenario", freeform)
		_check("a freeform scenario is offered and PLAY refuses it with the reason",
			not screen.play_refusal().is_empty(), screen.play_refusal())
		screen._apply_choice("scenario", opened)
	else:
		_check("a freeform scenario is offered and PLAY refuses it with the reason",
			false, "no freeform scenario in this document to test with")


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
	_check("the preview shades exactly the regions the scenario claims",
		_ownership_matches(screen), "")
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
	_check("PLAY is offered once a startable scenario is selected",
		screen.play_refusal().is_empty(), screen.play_refusal())
	var payload: Array = screen.seat_payload()
	var extra: Array[String] = []
	for seat_value in payload:
		var seat := seat_value as Dictionary
		for key in seat.keys():
			if not ["template", "team", "controller"].has(String(key)):
				extra.append(String(key))
	# NOTHING SNEAKS PAST. Colour, handicap and every locked rule row stay on
	# this screen; the session is handed the three fields it has always taken.
	_check("the seat payload carries template, team and controller and nothing else",
		extra.is_empty(), "extra fields: %s" % ", ".join(extra))

	var session := SessionScript.new()
	var began: bool = session.begin(
		document, world.campaign_name, screen.scenario_name(), payload)
	_check("the session begins on exactly what the screen chose",
		began, ", ".join(Array(session.refusals)))
	if not began:
		return
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
