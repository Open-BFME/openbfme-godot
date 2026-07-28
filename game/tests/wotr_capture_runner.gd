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

## THE TWO WINDOWS THE LAYOUT IS ASSERTED AT AND WAS NEVER PHOTOGRAPHED AT.
##
## `wotr_region_card_runner` holds the layout to five window sizes and asserts
## two properties at the ends of that range: at 1100x700 the map must not shrink
## below its stated floor and the sidebar must give way instead, and at
## 2560x1351 - the owner's own window, which is why that odd number is in the
## runner - the map must be nearly twice the area it has at the authored size.
## Both were arithmetic only. Every shot this runner took was at 1860x800, so
## "the sidebar gives way rather than sliding over Middle-earth" and "a bigger
## window is a bigger map" were claims with no picture behind them, which is
## exactly the gap this runner exists to close.
##
## NEITHER NUMBER IS CHOSEN HERE. Both are transcribed from
## `wotr_region_card_runner.gd`, which is where they are asserted: 1100x700 from
## `the_map_never_shrinks_below_its_stated_floor`, and 2560x1351 - the owner's
## own window, which is why it is an odd number rather than a round one - from
## that runner's `SIZES` list and its
## `the_map_grows_with_the_window_rather_than_staying_at_its_authored_size`.
## Picking a different pair here would photograph a layout nobody checks.
const LAYOUT_FLOOR_WINDOW := Vector2i(1100, 700)
const OWNERS_WINDOW := Vector2i(2560, 1351)

var _out_dir := ""
var _screen: Control = null
var _frames := 0
var _shot := 0
## Whether this shot's action has already been applied. See `_process`.
var _applied := false
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
		# The build plots and the ring of structures around one. Retail's screen
		# is as much a builder as a map, and a capture set that never opens the
		# ring cannot show whether the ring works.
		{"name": "04-build-plot", "action": "plot"},
		# THE CAMERA. The owner asked to "zoom around the 3d map and zoom way in
		# and out like in a regular skirmish match", so the set has to show both
		# ends of the range and an angle that is not the default one - a camera
		# claim nobody photographed is a claim nobody checked.
		{"name": "05-zoomed-in", "action": "zoom_in"},
		{"name": "06-orbited", "action": "orbit"},
		{"name": "07-zoomed-out", "action": "zoom_out"},
		# THE TWO WINDOWS THE LAYOUT IS ASSERTED AT AND WAS NEVER PHOTOGRAPHED
		# AT. Both reset the camera first, so what differs between 08, 09 and 01
		# is the WINDOW and nothing else: three shots of one framing at three
		# sizes is a comparison, three shots of three cameras is not.
		{"name": "08-layout-floor", "action": "reset", "window": LAYOUT_FLOOR_WINDOW},
		{"name": "09-owners-window", "action": "reset", "window": OWNERS_WINDOW},
		# BACK TO THE AUTHORED SIZE, and this is not a spare picture: the layout
		# has just been driven down to its floor and back up again, so if a
		# control does not come back, 10 and 01 differ and the pair says so.
		{"name": "10-back-at-the-authored-size", "action": "reset", "window": WINDOW_SIZE},
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
	# THE ACTION IS APPLIED A WHOLE SETTLE PERIOD BEFORE THE SHOT. `_process` runs
	# before the frame is drawn, so `root.get_texture()` here still holds the
	# PREVIOUS frame - applying and capturing in one visit photographed the state
	# the screen was in before the action, and every shot in the first capture set
	# this lane took was one step stale because of it. `_applied` makes the two
	# halves separate visits.
	if not _applied:
		# THE WINDOW FIRST, then the action. A resize relayouts the screen and
		# re-fits the camera, so doing it after would photograph a camera fitted
		# to the previous panel; and it is done in the SAME visit as the action so
		# that a whole settle period still passes before the shutter.
		if step.has("window"):
			var wanted := step["window"] as Vector2i
			root.size = wanted
			if _screen != null:
				_screen.size = Vector2(wanted)
				_screen._relayout()
			print("[capture] window %s -> screen %s, map panel %s" % [
				str(wanted), str(_screen.size),
				"none" if _screen == null else str(_screen.map3d.size)])
		_apply(String(step["action"]))
		_applied = true
		return false
	_applied = false
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
		"reset":
			# Retail's opening framing, so a window shot is about the WINDOW.
			_screen.map3d.reset_camera()
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
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
		"plot":
			# The first region THE STATE says this seat owns that authors a build
			# plot - chosen by the document, not written into this file.
			# Prefer a region the seat can actually STAGE from, because
			# `select_region` refuses anything else and the ring would then open
			# over a region the screen is not looking at.
			var owned: Array[String] = []
			for region_id in _screen.session.staging_regions():
				owned.append(String(region_id))
			for region_id in _screen.session.state.regions_owned_by(
					_screen.session.state.active_player()):
				if not owned.has(String(region_id)):
					owned.append(String(region_id))
			for region_id in owned:
				var region: Dictionary = _screen.session.world.region(String(region_id))
				if int(region.get("building_spot_count", 0)) <= 0:
					continue
				_screen.select_region(String(region_id))
				_screen._on_plot_clicked(String(region_id), 0)
				print("[capture] build ring opened on %s plot 1 of %d" % [
					String(region_id), int(region.get("building_spot_count", 0))])
				break
		"zoom_in":
			# Onto the selected region, at the deep end of the range, so the shot
			# shows what "zoom way in" actually looks like rather than a nudge.
			_screen.map3d.focus_region(_screen.session.selected_region, 0.10)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"orbit":
			_screen.map3d.set_orbit(0.9, -24.0)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"zoom_out":
			_screen.map3d.set_orbit(0.0, -52.0)
			_screen.map3d.focus_region("", 1.3)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
