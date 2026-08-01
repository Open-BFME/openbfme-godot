extends SceneTree

## THE MAP LANE'S OWN CAPTURE STAGE: retail's 3D strategic map, at a stated size,
## with NO strategic HUD in front of it.
##
## WHY IT EXISTS, and it is not "a second capture runner for convenience".
##
##   1. `wotr_capture_runner` photographs the whole screen, which means it loads
##      `src/ui/wotr_screen.gd`. That file belongs to another stream, and while
##      that stream is mid-edit it does not parse - at which point the capture
##      runner photographs the MAIN MENU and says "the shell refused to open War
##      of the Ring". Three consecutive runs of this round were lost that way.
##      This stage reaches the map view directly and cannot be blocked by a file
##      it does not load.
##
##   2. It photographs the map WITHOUT the HUD on top, which is the only way to
##      look at what the map layer is actually doing at the bottom of the panel -
##      the tray feather, the framing bias and the compass all live in the band
##      the HUD normally covers, and a capture with the tray in front of them
##      shows a tray.
##
## IT IS NOT A SUBSTITUTE FOR `wotr_capture_runner` and must not become one: the
## composed screen is what ships and what gets reviewed. This is the lane's
## microscope, not its shop window.
##
## WHAT IT DRAWS. Retail's own living-world document, at the campaign's own
## opening state, with a seat selected and its legal attack targets live - so the
## selection curtain, the legal-target reticles, the banners and the build ring
## are all in the frame rather than being states a reader has to imagine.
##
##   "$GODOT" --path game --script res://tests/wotr_map_stage_runner.gd -- \
##       --out <dir> [--size 2560x1440] [--at 80,80]
##
## The stage is a `SubViewport` at the asked-for size, so the picture is that size
## whatever the host window is; the window itself is small and ON SCREEN, because
## the owner watches these runs.

const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")
const GeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const MarkerScript = preload("res://src/wotr/wotr_marker_models.gd")
const UiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const DisplayNamesScript = preload("res://src/wotr/wotr_display_names.gd")

## The seat colours the composed screen uses. Read from the chrome's own constant
## would couple this to a file it deliberately does not load, so they are restated
## here and the restatement is the point: if the two ever disagree, this stage is
## photographing colours the player never sees. `wotr_map3d_runner` asserts the
## rendered separation of exactly these six.
const SEAT_COLORS: Array[Color] = [
	Color("#4d7fd6"), Color("#c8483f"), Color("#5aa552"), Color("#d0b03c"),
	Color("#a763c9"), Color("#3fb0ad"),
]
const NEUTRAL_COLOR := Color("#5a6656")

var _out_dir := "user://wotr-map-stage"
var _stage_size := Vector2i(2560, 1440)
var _view: Control = null
var _stage: SubViewport = null
var _session = null
var _display_names = null
var _frames := 0
var _shot := 0
var _shots: Array[Dictionary] = []


func _argument(name: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in args.size():
		if String(args[index]) == name and index + 1 < args.size():
			return String(args[index + 1])
	return fallback


func _initialize() -> void:
	print("== WAR OF THE RING MAP STAGE ==")
	_out_dir = _argument("--out", _out_dir)
	var wanted := _argument("--size", "2560x1440").split("x")
	if wanted.size() == 2:
		_stage_size = Vector2i(int(wanted[0]), int(wanted[1]))
	DirAccess.make_dir_recursive_absolute(_out_dir)

	# A small, VISIBLE host window. The owner watches these runs; the stage's own
	# size is independent of it.
	var host := DisplayServer.window_get_size()
	DisplayServer.window_set_size(Vector2i(960, 560))
	var at := _argument("--at", "80,80").split(",")
	if at.size() == 2:
		DisplayServer.window_set_position(Vector2i(int(at[0]), int(at[1])))
	print("[stage] host window was %s, now %s; stage %s" % [
		str(host), str(DisplayServer.window_get_size()), str(_stage_size)])

	var bundle := BundleScript.new()
	var located: Dictionary = bundle.locate_and_load([])
	if not bool(located.get("ok", false)):
		push_error("[stage] no map bundle: %s" % String(located["reason"]))
		quit(1)
		return

	var geometry = GeometryScript.new()
	geometry.locate_and_load([])
	var roots: Array = []
	var probed: Dictionary = GeometryScript.probe([])
	if bool(probed.get("found", false)):
		roots.append(String(probed["root"]))
	roots.append(GeometryScript.USER_BUNDLE)
	var markers = MarkerScript.new()
	var marker_load: Dictionary = markers.locate_and_load(roots)
	var ui = UiScript.new()
	var ui_load: Dictionary = ui.locate_and_load(roots)

	# THE PROJECT STRETCHES ITS CANVAS (`canvas_items`, base 1920x1080), and that
	# transform reaches inside a SubViewport parented to the root Window: with a
	# 960-wide host window the scale is 0.5, so a Control anchored full-rect in a
	# 2560x1440 stage measured 5120x2880 and the camera was fitted to a panel twice
	# the size of the picture. The stage is a MEASURING INSTRUMENT and must be in
	# pixels, so the stretch is switched off for this process only.
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED

	_stage = SubViewport.new()
	_stage.size = _stage_size
	_stage.transparent_bg = false
	_stage.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_stage)

	_view = MapViewScript.new()
	_view.build()
	# NOT ANCHORED. A full-rect Control inside this stage measured 5120x2880 for a
	# 2560x1440 SubViewport - the root window's own content scale reaches the
	# layout pass - and a camera fitted to a panel twice the picture's size is a
	# measuring instrument that lies. The size is written and re-written by hand.
	_view.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_view.size = Vector2(_stage_size)
	_stage.add_child(_view)
	_view.set_bundle(bundle, "")
	_view.set_region_geometry(geometry, String(located.get("reason", "")))
	_view.set_markers(markers, String(marker_load.get("reason", "")))
	_view.set_ui(ui, String(ui_load.get("reason", "")))
	_view.owner_colors = SEAT_COLORS
	_view.neutral_color = NEUTRAL_COLOR
	_view._on_resized()

	_display_names = DisplayNamesScript.new()
	_display_names.locate_and_load(roots)
	_session = _load_session()
	if _session == null:
		push_error("[stage] no living-world session; set OPENBFME_LIVING_WORLD_DOC")
		quit(1)
		return
	_push_state(ui)
	_shots = _script()
	print("[stage] %d shot(s) into %s" % [_shots.size(), _out_dir])


## Every picture this stage takes, in order, as `{name, before}` where `before` is
## a callable run on the view before the shot. Kept as data so the list of states
## photographed is readable in one place.
func _script() -> Array[Dictionary]:
	return [
		# THE LAYOUT SETTLES ON A FRAME, NOT IN `_initialize`. The view is anchored
		# full-rect inside the stage and Godot resolves that on the first layout
		# pass, so a camera fitted before it has been fitted to the wrong panel -
		# which is exactly what the first run of this stage photographed.
		{"name": "01-opening", "before": func() -> void:
			_view.size = Vector2(_stage_size)
			_view._on_resized()
			_view.reset_camera()
			print("[stage] view size %s, inner viewport %s, camera %s" % [
				str(_view.size), str(_view.viewport.size), str(_view.camera_state())])},
		{"name": "02-no-hud-full-bleed", "before": func() -> void:
			_view.set_hud_keep_out([])},
		{"name": "03-hud-islands", "before": func() -> void:
			_view.set_hud_keep_out(_hud_islands())},
		{"name": "04-build-ring", "before": func() -> void: _open_the_ring()},
		{"name": "05-build-ring-hovered", "before": func() -> void:
			_view.hover_build_entry_at(_first_entry_id())},
		{"name": "06-pulse-midway", "before": func() -> void:
			_view.drive_target_pulse(MapViewScript.TARGET_PULSE_SECONDS * 0.5)},
	]


func _hud_islands() -> Array:
	# The composed screen's own island geometry at 2560x1440, scaled to the stage.
	var scale := Vector2(float(_stage_size.x) / 2560.0, float(_stage_size.y) / 1440.0)
	var boxes := [
		Rect2(2.6, 0.0, 366.4, 334.4), Rect2(0.0, 960.0, 960.0, 480.0),
		Rect2(702.3, 1035.2, 1853.3, 390.8), Rect2(2142.0, 0.6, 415.0, 106.9),
	]
	var scaled: Array = []
	for box in boxes:
		var rect := box as Rect2
		scaled.append(Rect2(rect.position * scale, rect.size * scale))
	return scaled


func _first_entry_id() -> String:
	var entries: Array = _view.radial_entries
	return String((entries[0] as Dictionary).get("id", "")) if not entries.is_empty() else ""


func _open_the_ring() -> void:
	for region_id in _view.shaded_regions:
		var spots: Array = _session.world.region(
			String(region_id)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		_view.set_overlays(_armies(), _plots(), _names(),
			{"region": String(region_id), "index": 0}, _offerings(String(region_id)))
		return


func _push_state(ui) -> void:
	var adjacency: Dictionary = {}
	for region_id in _session.world.region_ids:
		adjacency[String(region_id)] = _session.world.neighbours(String(region_id))
	var rows: Array[Dictionary] = _session.region_rows()
	# A REAL SELECTION, TAKEN FROM THE STATE rather than named here: the first
	# region the human seat holds that has somewhere to attack. Without one the
	# stage photographs the resting map and the marks this lane is working on are
	# all in states nobody can see.
	var staging: PackedStringArray = _session.staging_regions()
	var selection := ""
	var targets := PackedStringArray()
	for value in staging:
		var candidate := String(value)
		var reachable: PackedStringArray = _session.attack_targets(candidate)
		if not reachable.is_empty():
			selection = candidate
			targets = reachable
			break
	_session.selected_region = selection
	_view.set_regions(rows, adjacency, staging, targets, selection, "")
	_view.set_overlays(_armies(), _plots(), _names(), {}, [] as Array[Dictionary])
	print("[stage] selection %s with %d legal target(s), %d staging region(s)" % [
		selection, targets.size(), staging.size()])


func _armies() -> Dictionary:
	var by_region: Dictionary = {}
	var ids: Array[int] = []
	for key in _session.state.armies.keys():
		ids.append(int(key))
	ids.sort()
	for army_id in ids:
		var army := _session.state.armies[army_id] as Dictionary
		var region_id := String(army.get("region", ""))
		if region_id.is_empty():
			continue
		var owner := int(army.get("owner", StateScript.NEUTRAL))
		var template := ""
		if owner >= 0 and owner < _session.state.players.size():
			template = String((_session.state.players[owner] as Dictionary).get("template", ""))
		var roster := String(army.get("roster", ""))
		var portrait: Dictionary = {"id": ""}
		var marker: Dictionary = {"icon": "", "size": ""}
		if _view.has_ui():
			portrait = _view.ui.army_portrait(
				roster, String(army.get("hero_template", "")), template)
			marker = _view.ui.army_marker(roster, template)
		var stacks: Array = by_region.get(region_id, []) as Array
		stacks.append({
			"owner": owner, "template": template,
			"kind": String(army.get("kind", "")), "label": roster,
			"portrait_id": String(portrait.get("id", "")),
			"icon": String(marker.get("icon", "")),
			"size": String(marker.get("size", "")),
		})
		by_region[region_id] = stacks
	return by_region


func _plots() -> Dictionary:
	var by_region: Dictionary = {}
	for region_id in _session.world.region_ids:
		var spots: Array = _session.world.region(
			String(region_id)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		var points: Array[Vector2] = []
		for spot in spots:
			var row := spot as Dictionary
			points.append(Vector2(float(row.get("x", 0)), float(row.get("y", 0))))
		by_region[String(region_id)] = points
	return by_region


func _names() -> Dictionary:
	var labels: Dictionary = {}
	for region_id in _session.world.region_ids:
		var key := String(region_id)
		labels[key] = _display_names.living_world_label(
			String(_session.world.region(key).get("display_name", "")), key)
	return labels


## Retail's own `LivingWorldBuilding` offerings for whoever holds the region, in
## the shape `_draw_radial_menu` reads. Nothing is invented: an offering with no
## `ConstructButtonImage` reaches the ring with an empty `image_id` and the ring
## draws its own "no icon" refusal.
func _offerings(region_id: String) -> Array[Dictionary]:
	var entries: Array[Dictionary] = []
	for row_value in _session.build_options(region_id, 0):
		var row := row_value as Dictionary
		entries.append({
			"id": String(row.get("building", "")),
			"image_id": String(row.get("button_image", "")),
			# Retail's own construct-button title through the string table, and the
			# tag itself when the table does not carry it - never derived from the
			# id. The composed screen resolves the same tag the same way.
			"title": _display_names.living_world_label(
				String(row.get("build_title_tag", "")), String(row.get("building", ""))),
			# THE PRICE IS CARRIED AND THE RING DOES NOT DRAW IT. See
			# `wotr_map_view.ring_caption`: the map letters names only, and this
			# stage hands the same entries the composed screen does so that the
			# capture proves the map is dropping the price rather than never
			# having been offered one.
			"cost": str(int(row.get("cost", 0))),
		})
		if entries.size() == 4:
			break
	return entries


func _load_session():
	var located: Dictionary = SessionScript.locate_document([])
	if not bool(located.get("ok", false)):
		return null
	var document := located["document"] as Dictionary
	var probe = SessionScript.new()
	var world = load("res://src/wotr/wotr_world.gd").new()
	if not world.load_from_dict(document, ""):
		return null
	probe.world = world
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	var seats: Array = []
	for option in probe.seat_options(availability):
		seats.append({
			"template": String(option["template"]), "team": seats.size() + 1,
			"controller": StateScript.CONTROLLER_HUMAN if seats.is_empty()
				else StateScript.CONTROLLER_AI,
		})
		if seats.size() == 2:
			break
	var scenarios := probe.startable_scenarios(2)
	if scenarios.is_empty() or seats.size() < 2:
		return null
	var session = SessionScript.new()
	if not session.begin(document, world.campaign_name, String(scenarios[0]), seats):
		return null
	return session


func _process(_delta: float) -> bool:
	if _shots.is_empty():
		return true
	_frames += 1
	# A handful of frames per shot: the map view rebuilds markers and re-fits the
	# camera off signals, and a texture read on the frame a state changed catches
	# the previous one.
	if _frames % 6 != 0:
		return false
	if _shot >= _shots.size():
		print("[stage] done")
		return true
	var shot := _shots[_shot] as Dictionary
	if _frames % 12 == 6:
		(shot["before"] as Callable).call()
		return false
	var image := _stage.get_texture().get_image()
	var path := _out_dir.path_join("%s.png" % String(shot["name"]))
	image.save_png(path)
	print("[stage] %s  %dx%d" % [path, image.get_width(), image.get_height()])
	_shot += 1
	return false
