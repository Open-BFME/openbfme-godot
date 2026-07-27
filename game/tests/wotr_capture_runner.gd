extends SceneTree

## SCREENSHOT THE WAR OF THE RING SCREEN, so a claim about how it LOOKS can be
## checked by looking at it.
##
## Everything else in this lane runs headless, and headless Godot does not
## render: it can prove the bundle parsed, the meshes were instanced and the
## regions were placed, and it cannot prove Middle-earth is on the screen. That
## gap is exactly where "the 3D map is a quarter of the panel" and "regions are
## coloured dots" both survived review. So this runner opens a real window,
## drives the screen the way the menu does, lets the renderer settle, and writes
## PNGs.
##
## It asserts nothing. It is a camera, not a test, and it says so rather than
## reporting a pass that would mean nothing.
##
## Usage:
##   Godot_v4.7 --path game --script tests/wotr_capture_runner.gd -- --out <dir>
## with `OPENBFME_LIVING_WORLD_DOC`, `OPENBFME_LIVING_MAP` and
## `OPENBFME_LIVING_MAP_REGIONS` pointing at the document and bundles.

const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")

## Frames to let pass before capturing. The 3D map is drawn into a SubViewport
## with `UPDATE_ALWAYS`, and the first frames of a fresh viewport are empty.
const SETTLE_FRAMES := 45
const WINDOW_SIZE := Vector2i(1860, 800)

var _out_dir := ""
var _screen: Control = null
var _frames := 0
var _shot := 0
var _plan: Array[Dictionary] = []


func _initialize() -> void:
	_out_dir = _argument("--out", "user://wotr-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.size = WINDOW_SIZE
	window.title = "OpenBFME - War of the Ring capture"

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.05, 0.06, 0.05)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	window.add_child(backdrop)

	_screen = ScreenScript.new()
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	window.add_child(_screen)

	var session := _seat_a_session()
	if session != null:
		_screen.configure(session, [], "", [])
		print("[capture] session seated on %s" % session.document_path)
	else:
		_screen.configure(null, [], "no living-world document could be seated", [])
		print("[capture] NO SESSION - the screen will draw its refusal, which is also worth a picture.")

	# The shots, in order. Each is a name plus something to do to the screen
	# first, so the captures show different states rather than the same frame
	# three times.
	_plan = [
		{"name": "01-opening", "action": ""},
		{"name": "02-region-hovered", "action": "hover"},
		{"name": "03-staged", "action": "stage"},
	]
	print("[capture] writing to %s" % _out_dir)


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	if _shot >= _plan.size():
		print("[capture] wrote %d image(s). This runner asserts nothing; it is a camera." % _plan.size())
		return true
	var step := _plan[_shot]
	_apply(String(step["action"]))
	var image: Image = root.get_texture().get_image()
	var path: String = _out_dir.path_join("%s.png" % String(step["name"]))
	var error := image.save_png(path)
	if error != OK:
		push_error("[capture] could not write %s (error %d)" % [path, error])
	else:
		print("[capture] %s  %dx%d" % [path, image.get_width(), image.get_height()])
	_shot += 1
	return false


## Seat a session on the real document, the same way the round-trip runner does:
## find the document, take the first two seatable templates, start the first
## scenario that admits two seats. Nothing about the world is written here.
func _seat_a_session() -> SessionScript:
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		print("[capture] no document: %s" % String(found.get("reason", "")))
		return null
	var document: Dictionary = found["document"] as Dictionary
	var world = load("res://src/wotr/wotr_world.gd").new()
	if not world.load_from_dict(document, ""):
		print("[capture] the document did not load: %s" % str(world.errors))
		return null
	var probe := SessionScript.new()
	probe.world = world
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	var seats: Array = []
	for option in probe.seat_options(availability):
		seats.append({
			"template": String(option["template"]),
			"team": seats.size() + 1,
			"controller": "human" if seats.is_empty() else "ai",
		})
		if seats.size() == 2:
			break
	var scenarios := probe.startable_scenarios(2)
	if scenarios.is_empty() or seats.size() < 2:
		print("[capture] the document offers no two-seat start")
		return null
	var session := SessionScript.new()
	if not session.begin(document, world.campaign_name, String(scenarios[0]), seats):
		print("[capture] the session refused to begin: %s" % str(session.refusals))
		return null
	session.document_path = String(found["path"])
	session.document_source = String(found["source"])
	return session


func _apply(action: String) -> void:
	if action.is_empty() or _screen == null or _screen.session == null:
		return
	match action:
		"hover":
			# Point at whatever region the strategic layer says the active seat
			# could stage from - a real region, chosen by the state, not a name
			# written into this file.
			var staging: PackedStringArray = _screen.session.staging_regions()
			if not staging.is_empty():
				_screen._on_region_hovered(staging[0])
				_screen.refresh()
		"stage":
			var staging: PackedStringArray = _screen.session.staging_regions()
			if not staging.is_empty():
				_screen.select_region(staging[0])
				var targets: PackedStringArray = _screen.session.attack_targets(staging[0])
				if not targets.is_empty():
					_screen.select_target(targets[0])


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
