extends SceneTree

## SCREENSHOT AN AUTO-RESOLVED BATTLE, so the claim "the screen shows the
## working, and it is obvious which numbers are retail's and which are ours" can
## be checked by LOOKING at it rather than by reading this sentence.
##
## Headless Godot does not render. `wotr_autoresolve_battle_runner.gd` can prove
## the dice are seeded from the commitment, that two peers agree, and that every
## factor is attributed - and none of that proves the report is legible. This
## opens a real window, drives the strategic screen the way a player does
## (select, target, AUTO-RESOLVE) and writes PNGs of the result.
##
## IT ASSERTS NOTHING. It is a camera, not a test, and says so rather than
## reporting a pass that would mean nothing.
##
## Usage:
##   Godot_v4.7 --path game --script tests/wotr_autoresolve_capture_runner.gd -- --out <dir>
## with `OPENBFME_LIVING_WORLD_DOC` and `OPENBFME_LIVING_WORLD_AUTORESOLVE`
## pointing at the document and the two auto-resolve bundles.

const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

const SETTLE_FRAMES := 30
const WINDOW_SIZE := Vector2i(1860, 980)

var _out_dir := ""
var _screen = null
var _session: SessionScript = null
var _frames := 0
var _shot := 0
var _applied := false
var _plan: Array[Dictionary] = []
var _target := ""


func _initialize() -> void:
	_out_dir = _argument("--out", "user://wotr-autoresolve-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.size = WINDOW_SIZE
	window.title = "OpenBFME - War of the Ring auto-resolve capture"

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.02, 0.03, 0.045)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	window.add_child(backdrop)

	_screen = ScreenScript.new()
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	window.add_child(_screen)

	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		_screen.configure(null, [], String(found.get("reason", "")))
		print("[autoresolve-capture] NO DOCUMENT - the screen draws its refusal, which is also worth a picture.")
		_plan = [{"name": "01-no-document", "action": ""}]
		return

	var document: Dictionary = found["document"] as Dictionary
	# The campaign rule is set to AUTO RESOLVE outright, so the capture shows the
	# path the owner asked to be enabled rather than the mixed default.
	_session = _seated(document)
	if _session == null:
		_screen.configure(null, [], "no scenario in this document seats two players with territory")
		_plan = [{"name": "01-unseatable", "action": ""}]
		return
	_screen.configure(_session, ["bfme2.map.dagorlad", "bfme2.map.fords-of-isen"], "")
	print("[autoresolve-capture] auto-resolve reason: %s" % (
		"(loaded)" if _session.autoresolve != null else _session.auto_resolve_reason))

	_plan = [
		{"name": "01-strategic-map", "action": ""},
		{"name": "02-attack-selected", "action": "select"},
		{"name": "03-battle-report-top", "action": "resolve"},
		{"name": "04-battle-report-rounds", "action": "scroll_rounds"},
		{"name": "05-battle-report-rules-table", "action": "scroll_end"},
	]
	print("[autoresolve-capture] writing to %s" % _out_dir)


func _seated(document: Dictionary) -> SessionScript:
	var seats := [
		{"template": "PlayerMen", "team": 1, "controller": "human", "handicap": 0},
		{"template": "PlayerMordor", "team": 2, "controller": "ai", "handicap": 25},
	]
	var rules := {"battle_type": "auto_resolve", "battle_type_priority": "auto_resolve"}
	for row in document.get("scenarios", []) as Array:
		var scenario := row as Dictionary
		if int(scenario.get("maxPlayers", 0)) < 2:
			continue
		if (scenario.get("ownershipSets", []) as Array).size() < 2:
			continue
		var session := SessionScript.new()
		session.load_auto_resolve([])
		if session.begin(document, String(scenario.get("regionCampaign", "")),
				String(scenario.get("name", "")), seats, rules):
			session.document_path = "capture"
			session.document_source = "env"
			return session
	return null


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	if _shot >= _plan.size():
		print("[autoresolve-capture] wrote %d image(s). This runner asserts nothing; it is a camera."
			% _plan.size())
		return true
	var step := _plan[_shot]
	# The action lands a whole settle period before the shot, for the reason
	# `wotr_setup_capture_runner` records: `_process` runs before the frame is
	# drawn, so capturing in the same visit photographs the state the screen was
	# in BEFORE the action.
	if not _applied:
		_apply(String(step["action"]))
		_applied = true
		return false
	_applied = false
	var image: Image = root.get_texture().get_image()
	var path: String = _out_dir.path_join("%s.png" % String(step["name"]))
	var error := image.save_png(path)
	if error != OK:
		push_error("[autoresolve-capture] could not write %s (error %d)" % [path, error])
	else:
		print("[autoresolve-capture] %s  %dx%d" % [path, image.get_width(), image.get_height()])
	_shot += 1
	return false


func _apply(action: String) -> void:
	if action.is_empty() or _screen == null or _session == null:
		return
	match action:
		"select":
			_target = _first_attackable()
			print("[autoresolve-capture] target: %s" % (_target if not _target.is_empty() else "NONE"))
			_screen.refresh()
		"resolve":
			if _target.is_empty():
				print("[autoresolve-capture] no legal attack was reachable; the map shot stands alone")
				return
			var result: Dictionary = _screen.auto_resolve_selected_attack()
			print("[autoresolve-capture] auto-resolve ok=%s seed=%s" % [
				str(result.get("ok", false)), String(result.get("seed", "")).substr(0, 16)])
			for reason in result.get("refusals", PackedStringArray()) as PackedStringArray:
				print("[autoresolve-capture]   refusal: %s" % String(reason))
		"scroll_rounds":
			_scroll(0.34)
		"scroll_end":
			_scroll(1.0)


## Scroll the report to a FRACTION of its content, so the three report shots
## show the top, the round-by-round working and the rules table rather than
## three pictures of the same paragraph.
func _scroll(fraction: float) -> void:
	if _screen.report_text == null or not _screen.report_text.visible:
		return
	var bar: VScrollBar = _screen.report_text.get_v_scroll_bar()
	if bar == null:
		return
	bar.value = bar.min_value + (bar.max_value - bar.page - bar.min_value) * clampf(fraction, 0.0, 1.0)


## The first legal attack, preferring a DEFENDED region so the picture shows a
## battle rather than a walkover. Garrisons the target if the shipped scenario
## seats the two sides out of contact, exactly as the battle runner does, and
## says so on the console so the screenshot is not mistaken for a stock game.
func _first_attackable() -> String:
	var fallback := ""
	for _step in range(40):
		for from_region in _session.staging_regions():
			for target in _session.attack_targets(String(from_region)):
				if not _session.state.armies_in_region(String(target)).is_empty():
					_session.selected_region = String(from_region)
					_session.selected_target = String(target)
					return String(target)
				if fallback.is_empty():
					fallback = String(target)
					_session.selected_region = String(from_region)
		var marched := false
		for from_region in _session.staging_regions():
			for to_region in _session.movement_targets(String(from_region)):
				if bool(_session.move_armies(String(from_region), String(to_region)).get("ok", false)):
					marched = true
					break
			if marched:
				break
		if not marched:
			break
	if fallback.is_empty():
		return ""
	_session.selected_target = fallback
	if _session.state.armies_in_region(fallback).is_empty():
		print("[autoresolve-capture] the two seats start out of contact, so the target is "
			+ "GARRISONED through the strategic layer's own place_army() to make the "
			+ "picture a real battle. The situation is constructed; the RESULT is not.")
		_garrison(fallback)
	return fallback


func _garrison(region_id: String) -> void:
	var owner := _session.state.owner_of(region_id)
	if owner == StateScript.NEUTRAL:
		return
	var names: Array[String] = []
	for key in _session.state.roster_units.keys():
		names.append(String(key))
	names.sort()
	for name in names:
		if (_session.state.roster_units[name] as Array).is_empty():
			continue
		_session.state.place_army(owner, region_id, name)
		return


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if String(args[index]) == flag:
			return String(args[index + 1])
	return fallback
