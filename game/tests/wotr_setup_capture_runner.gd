extends SceneTree

## SCREENSHOT THE GAME SETUP SCREEN, so a claim about how it LOOKS can be
## checked by looking at it.
##
## The same reasoning as `wotr_capture_runner.gd`: headless Godot does not
## render, so every assertion in this lane can prove the bundles parsed and the
## rows were built and none of them can prove the screen is worth looking at.
## This opens a real window, drives the screen the way a player does, lets the
## renderer settle and writes PNGs.
##
## IT ASSERTS NOTHING. It is a camera, not a test, and says so rather than
## reporting a pass that would mean nothing. `wotr_setup_runner.gd` is the test.
##
## Usage:
##   Godot_v4.7 --path game --script tests/wotr_setup_capture_runner.gd -- --out <dir>
## with `OPENBFME_LIVING_WORLD_DOC`, `OPENBFME_LIVING_MAP_REGIONS` and
## `OPENBFME_WOTR_SETUP_STRINGS` (or the bundles beside the region geometry)
## pointing at the document and bundles.
##
## POINT `OPENBFME_SHELL_ART` AT THE **ROTWK** EFFECTIVE-ASSETS ROOT, not the
## edition-neutral one. `workspace/retail-work/cache/effective-assets` is BFME2's
## layer: its `apt_MenuExport_*` sheets are the GREEN menu set, its
## `apt_MainMenu_1` is a pre-order advertisement, and it has no
## `apt_MenuExport_3` at all. The thorned BLUE-STEEL shell this screen's oracle
## shows is on `workspace/retail-work/editions/rotwk/cache/layered-effective-
## assets`, and `wotr_setup_chrome.ART_PIECES` is measured against THOSE sheets.
## `OPENBFME_SHELL_FONT` wants `<that root>/albertusmt.otf`.

const SetupScreenScript = preload("res://src/ui/wotr_setup_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")

const SETTLE_FRAMES := 30
## THE CAPTURE SIZE IS THE ORACLE'S SIZE, EXACTLY: 2560x1440.
##
## Round two asked the WINDOW for 1600x900 and borderless, and the PNGs came out
## 2560x1351 - aspect 1.895 - because a desktop window is not a promise: the
## compositor scales and clamps it to what the display can actually show. The
## critic read the result correctly and dismissively: "That is a windowed capture
## with the frame cropped off, i.e. a dev build being screenshotted out of a
## window", and "it broadcasts 'windowed dev build' in any side-by-side."
##
## SO THE SCREEN IS NOT RENDERED INTO THE WINDOW AT ALL. It is rendered into a
## `SubViewport` of exactly this size and the SUBVIEWPORT's texture is saved, so
## the PNG's dimensions are a property of this file and not of the machine's
## monitor. The host window still exists (Godot needs a real rendering context)
## and is small on purpose.
const CAPTURE_SIZE := Vector2i(2560, 1440)
const HOST_WINDOW_SIZE := Vector2i(960, 540)

var _out_dir := ""
var _screen: Control = null
var _viewport: SubViewport = null
var _frames := 0
var _shot := 0
var _applied := false
var _plan: Array[Dictionary] = []


func _initialize() -> void:
	_out_dir = _argument("--out", "user://wotr-setup-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.borderless = true
	window.size = HOST_WINDOW_SIZE
	window.title = "OpenBFME - War of the Ring game setup capture"

	_viewport = SubViewport.new()
	_viewport.size = CAPTURE_SIZE
	# ALWAYS, not ONCE: the plan drives the screen between shots and a viewport
	# that updated once would photograph the first state five times.
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	window.add_child(_viewport)

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.02, 0.03, 0.045)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(backdrop)

	_screen = SetupScreenScript.new()
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(_screen)

	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		_screen.configure({}, null, [], String(found.get("reason", "")))
		print("[setup-capture] NO DOCUMENT - the screen draws its refusal, which is also worth a picture.")
	else:
		var document: Dictionary = found["document"] as Dictionary
		var world = load("res://src/wotr/wotr_world.gd").new()
		if not world.load_from_dict(document, ""):
			_screen.configure({}, null, [], "the document did not load: %s" % str(world.errors))
		else:
			var probe := SessionScript.new()
			probe.world = world
			# Every bound faction is treated as fieldable here, the same way the
			# round-trip and capture runners do, because this runner boots no
			# content pack and has no availability map to read.
			var availability: Dictionary = {}
			for pack_faction in SessionScript.FACTION_BINDINGS.values():
				availability[String(pack_faction)] = ""
			_screen.pack_faction_availability = availability
			_screen.configure(document, probe, [], "")
			print("[setup-capture] configured on %s" % String(found["path"]))
	for line in _screen.describe_load():
		print("[setup-capture] %s" % String(line))

	_plan = [
		{"name": "01-map-tab", "action": ""},
		{"name": "02-map-tab-starts-placed", "action": "place_starts"},
		{"name": "03-map-tab-scenario-open", "action": "open_scenario"},
		{"name": "04-map-tab-territory-hovered", "action": "hover_region"},
		{"name": "05-player-table-army-open", "action": "open_army"},
		{"name": "06-rules-tab", "action": "rules"},
		{"name": "07-absence-overlay", "action": "absences"},
	]
	print("[setup-capture] writing to %s" % _out_dir)


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	if _shot >= _plan.size():
		print("[setup-capture] wrote %d image(s). This runner asserts nothing; it is a camera." % _plan.size())
		return true
	var step := _plan[_shot]
	# The action lands a whole settle period before the shot: `_process` runs
	# before the frame is drawn, so capturing in the same visit photographs the
	# state the screen was in BEFORE the action. That defect cost a previous
	# lane an entire capture set and the fix is kept here verbatim.
	if not _applied:
		_apply(String(step["action"]))
		_applied = true
		return false
	_applied = false
	var image: Image = _viewport.get_texture().get_image()
	var path: String = _out_dir.path_join("%s.png" % String(step["name"]))
	var error := image.save_png(path)
	if error != OK:
		push_error("[setup-capture] could not write %s (error %d)" % [path, error])
	else:
		print("[setup-capture] %s  %dx%d" % [path, image.get_width(), image.get_height()])
	_shot += 1
	return false


func _apply(action: String) -> void:
	if action.is_empty() or _screen == null:
		return
	match action:
		"place_starts":
			# THE FREEFORM START PLACEMENT, driven through the screen's own
			# `_pick_start_region` rather than by writing `seat_starts` here, so a
			# capture cannot show a placement the screen would not accept. The
			# regions are chosen from the seats' own ADJACENT-FREE choices - the
			# first drawn region no seat already holds - because this runner has no
			# authored answer either and must not pretend to.
			var preview = _screen.map_preview
			if preview != null:
				for region_value in preview.drawn_regions:
					if _screen._next_unplaced_seat() < 0:
						break
					_screen._pick_start_region(String(region_value))
				print("[setup-capture] start territories: %s" % ", ".join(
					Array(_screen.start_regions())))
		"absences":
			_screen.active_tab = _screen.TAB_MAP
			_screen._relayout()
			_screen.toggle_absences(true)
		"open_scenario":
			_screen.toggle_absences(false)
			_open("scenario")
		"hover_region":
			# A region THE DOCUMENT names, chosen by the selected scenario's own
			# ownership rather than written into this file.
			_screen._open_menu = ""
			var preview = _screen.map_preview
			if preview != null and not preview.drawn_regions.is_empty():
				var target := String(preview.drawn_regions[0])
				for region_id in preview.owner_colors.keys():
					target = String(region_id)
					break
				preview.hover_region = target
				preview.queue_redraw()
				_screen._on_region_hovered(target)
				print("[setup-capture] territory hovered: %s" % target)
		"open_army":
			_open("army_0")
		"rules":
			_screen._open_menu = ""
			_screen.active_tab = _screen.TAB_RULES
			_screen._relayout()
	_screen.queue_redraw()


func _open(menu_id: String) -> void:
	# Drive the screen through its own hit table - the one the LAST PAINT built,
	# never a rectangle written here - so a capture cannot show a popup the
	# screen would not actually open. No `await`: a coroutine that suspends
	# inside `_process` aborts silently and looks exactly like a hang.
	for hit in _screen._hits:
		if String(hit["id"]) != menu_id:
			continue
		_screen._open_menu = menu_id
		_screen._open_rect = hit["rect"] as Rect2
		_screen._open_options = _screen._menu_options(menu_id)
		_screen._open_selected = _screen._selected_value(menu_id)
		_screen._hover_option = 1 if _screen._open_options.size() > 1 else 0
		print("[setup-capture] opened %s with %d option(s)" % [
			menu_id, _screen._open_options.size()])
		_screen.queue_redraw()
		return
	print("[setup-capture] %s is not on the screen's hit table" % menu_id)


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
