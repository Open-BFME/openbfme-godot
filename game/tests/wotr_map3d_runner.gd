extends SceneTree

## RETAIL 3D STRATEGIC MAP: does it load, do regions land in the right place, and
## does the screen still refuse to invent anything?
##
## This runner is about PRESENTATION and it proves presentation claims. It never
## asserts on a strategic hash, because it must not be able to move one: if this
## file could change a hash the change would be a defect, and the round-trip
## runner (`wotr_round_trip_runner.gd`) is what guards that.
##
## It runs in two modes, and BOTH are real:
##
##  * with `OPENBFME_LIVING_MAP` pointing at a converted bundle, it checks the
##    map loads whole and that regions land on retail's own coordinates;
##  * with no bundle at all, it checks the screen falls back to the 2D graph,
##    SAYS it is a fallback, and still works.
##
## Exit 0 when every check passes, 1 otherwise.

const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

## LIVENESS. These are exact totals, not floors, so a check that stops running is
## a failure rather than a quietly smaller number. Both are counted, not guessed:
##
##   with no bundle   = 2 (the refusal is actionable)
##                    + 1 (the refusal names every candidate it looked at)
##                    + 3 (a deliberately corrupt bundle is NAMED, and does not
##                         end the search)
##                    + 1 (the view instanced nothing, and says so)
##                    + 6 (the screen labels the 2D fallback and stays sealed)
##                    + 1 (the fallback label sends the owner to the log)
##                    + 2 (the region census is split, and the placeholder rows
##                         are kept rather than dropped)
##                    = 16
##   with a bundle    = 2 (bundle located, no spurious reason)
##                    + 3 (a deliberately corrupt bundle is NAMED, and does not
##                         end the search)
##                    + 7 (sub-objects, landmarks, triangles, vertices, tiles,
##                         textures resolved, untextured really untextured)
##                    + 4 (grid exact, grid is 5x4, nine pairs, agreement bound)
##                    + 4 (view has map, regions placed, Rhun refused, heights in
##                         terrain)
##                    + 1 (the view instanced retail's drawable sub-objects)
##                    + 7 (the screen shows the 3D map, names what it does not
##                         draw, and stays sealed)
##                    + 1 (the 3D label reports how many meshes are on screen)
##                    + 3 (the whole map is inside the panel, centred in it, and
##                         the framing does not slide as the zoom moves)
##                    + 2 (the region census is split, and the placeholder rows
##                         are kept rather than dropped)
##                    = 34
##
## THE ARITHMETIC OF THE TWO MOVES. Nothing was removed and nothing was weakened.
## With a bundle: 29 + 3 framing + 2 census = 34. With no bundle: 14 + 2 census
## = 16 - the census checks run in both modes because the region-image bundle is
## found independently of the map bundle, and the framing ones do not, because a
## framing check needs a map to frame.
const EXPECTED_CHECKS_NO_BUNDLE := 16
const EXPECTED_CHECKS_WITH_BUNDLE := 34

## Retail's 64 sub-objects minus the 30 this lane holds back (the impassable
## volumes, the animated ambient cards and the multi-stage water overlays).
const EXPECTED_DRAWN_SUB_OBJECTS := 34

## Retail's own sub-object names, quoted from `livingworld.ini`'s `LivingMap`
## MapObject declaration. If the converter ever stops resolving one of these the
## map has silently lost a landmark, which is exactly the failure this catches.
const REQUIRED_SUB_OBJECTS := [
	"BORDERCLOUD", "TEXT PLANE", "LM_MINASTIRITH", "LM_DOLGULDUR", "LM_EREBOR",
	"LM_BLACKGATE", "LM_CIRITHONGUL", "LM_HELMSDEEP", "LM_ORTHANCTOWER",
	"LM_RIVENDELL", "LM_CARNDUM", "LM_FORNOST",
]

## Retail's 20 terrain tiles.
const TERRAIN_TILE_COUNT := 20

var _passed := 0
var _failed := 0


func _init() -> void:
	var bundle := BundleScript.new()
	var located: Dictionary = bundle.locate_and_load([])
	var have_bundle := bool(located.get("ok", false))

	print("== WAR OF THE RING 3D MAP ==")
	if have_bundle:
		print("bundle: %s" % located["root"])
	else:
		print("bundle: NONE - %s" % located["reason"])

	_check_reason_is_actionable(located, have_bundle)
	_check_a_broken_bundle_is_named()
	if have_bundle:
		_check_bundle(bundle)
		_check_coordinate_space(bundle)
		_check_view_places_regions(bundle)
	_check_screen_fallback_is_labelled(have_bundle)
	_check_the_region_census_is_split()

	var expected := EXPECTED_CHECKS_NO_BUNDLE
	if have_bundle:
		expected = EXPECTED_CHECKS_WITH_BUNDLE
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, expected, _passed, _failed])
	if total != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			total, expected])
		quit(1)
		return
	quit(1 if _failed > 0 else 0)


# --- checks -------------------------------------------------------------------

func _check_reason_is_actionable(located: Dictionary, have_bundle: bool) -> void:
	if have_bundle:
		_ok("bundle located", "root %s" % located["root"])
		_check(String(located.get("reason", "")).is_empty(),
			"a successful load carries no reason", String(located.get("reason", "")))
		return
	var reason := String(located.get("reason", ""))
	# A refusal that does not tell the owner what to run is a dead end.
	_check(reason.contains("livingmap_bundle"),
		"the no-map reason names the converter to run", reason)
	_check(reason.contains(BundleScript.BUNDLE_ENV),
		"the no-map reason names the environment variable", reason)
	# "Not found" without the list of places is what makes a silent fallback
	# impossible to diagnose from a log.
	_check(reason.contains("Looked in") and reason.contains(BundleScript.USER_BUNDLE),
		"the no-map reason names every place it looked", reason)


## THE DEFECT THIS RUNNER SHIPPED. A bundle that could not be read used to end
## the search and, on the real screen, produce a flat 2D map and a completely
## silent log. Both halves are asserted here: the bad candidate is NAMED with
## what was wrong with it, and the search carries on past it.
##
## Deliberately corrupt: a manifest that is not JSON at all, written into a
## throwaway pack root under `user://`.
func _check_a_broken_bundle_is_named() -> void:
	var pack_root := "user://wotr_map3d_runner_broken_pack"
	var bundle_dir := pack_root.path_join(BundleScript.PACK_BUNDLE_RELATIVE)
	DirAccess.make_dir_recursive_absolute(bundle_dir)
	var handle := FileAccess.open(bundle_dir.path_join("manifest.json"), FileAccess.WRITE)
	if handle == null:
		_fail("the corrupt-bundle probe could write its fixture", bundle_dir)
		_fail("the corrupt candidate is reported as unreadable", "no fixture")
		_fail("a corrupt candidate does not end the search", "no fixture")
		return
	handle.store_string("this is not a manifest")
	handle.close()

	var probe_bundle = BundleScript.new()
	var probed: Dictionary = probe_bundle.locate_and_load([pack_root])
	var row: Dictionary = {}
	for entry in probe_bundle.searched_roots:
		if String(entry["root"]) == bundle_dir:
			row = entry
			break
	_check(not row.is_empty() and String(row["verdict"]) == "unreadable",
		"the corrupt candidate is reported as unreadable, not skipped in silence",
		str(row))
	_check(String(row.get("detail", "")).contains("manifest"),
		"the corrupt candidate's row says what was wrong with it",
		String(row.get("detail", "")))
	# It must NOT have stopped there: the environment override and the user-data
	# location are both later in the order, and one stale pack must never be able
	# to hide a good bundle behind it.
	_check(probe_bundle.searched_roots.size() >= 2,
		"a corrupt candidate does not end the search",
		"%d candidate(s) examined, verdicts %s" % [
			probe_bundle.searched_roots.size(),
			str(probed.get("ok", false))])

	# Leave nothing behind: a stale fixture under user:// would be a candidate
	# root on the NEXT run, which is precisely the class of surprise this runner
	# exists to catch.
	DirAccess.remove_absolute(bundle_dir.path_join("manifest.json"))
	DirAccess.remove_absolute(bundle_dir)
	DirAccess.remove_absolute(pack_root.path_join("data"))
	DirAccess.remove_absolute(pack_root)


func _check_bundle(bundle) -> void:
	_check(bundle.sub_objects.size() == 64,
		"all 64 retail sub-objects loaded", "got %d" % bundle.sub_objects.size())

	var missing: Array[String] = []
	for name in REQUIRED_SUB_OBJECTS:
		if bundle.sub_object(name).is_empty():
			missing.append(String(name))
	_check(missing.is_empty(),
		"every sub-object livingworld.ini names resolved", "missing %s" % str(missing))

	var triangles := 0
	var vertices := 0
	var tiles := 0
	for entry in bundle.sub_objects:
		triangles += int(entry["triangle_count"])
		vertices += int(entry["vertex_count"])
		var name := String(entry["name"])
		if name.begins_with("LM_") and name.substr(3).is_valid_int():
			tiles += 1
	_check(triangles == 69002, "retail triangle count preserved", "got %d" % triangles)
	_check(vertices == 57182, "retail vertex count preserved", "got %d" % vertices)
	_check(tiles == TERRAIN_TILE_COUNT,
		"all %d terrain tiles present" % TERRAIN_TILE_COUNT, "got %d" % tiles)

	# A texture that did not resolve must be REPORTED, not swapped for a stand-in.
	# Zero unresolved is the good case; a non-zero count is allowed but must be
	# visible, which the screen asserts separately.
	_check(bundle.unresolved_textures.size() == 0,
		"every declared texture resolved from the catalog",
		"unresolved %s" % str(bundle.unresolved_textures))

	var untextured_have_no_texture := true
	for name in bundle.untextured_sub_objects:
		var entry: Dictionary = bundle.sub_object(String(name))
		if bool(entry.get("textured", false)):
			untextured_have_no_texture = false
	_check(untextured_have_no_texture,
		"sub-objects reported untextured really carry no texture", "")


func _check_coordinate_space(bundle) -> void:
	# THE PROOF THAT THE BONE TRANSFORM IS RIGHT. Retail's 20 terrain tiles form a
	# grid; if the transform were wrong they would overlap or scatter.
	var proof: Dictionary = bundle.terrain_grid_proof
	_check(bool(proof.get("exact", false)),
		"terrain tiles occupy a grid exactly once each",
		"cells %s collisions %s" % [proof.get("cellsOccupiedExactlyOnce"), proof.get("collisions")])
	_check(int(proof.get("columns", 0)) == 5 and int(proof.get("rows", 0)) == 4,
		"terrain grid is retail's 5x4",
		"got %sx%s" % [proof.get("columns"), proof.get("rows")])

	# THE PROOF THAT MAP SPACE AND DOCUMENT SPACE ARE ONE SPACE. Nine landmark
	# sub-objects are bound to bones whose translations must sit on top of the
	# regions the document places at the same names. A scale or offset error would
	# put these thousands of units apart, not tens.
	var agreement: Dictionary = bundle.landmark_agreement
	_check(int(agreement.get("measuredPairs", 0)) == 9,
		"all nine landmark/region pairs were measured",
		"got %s" % agreement.get("measuredPairs"))
	var worst := float(agreement.get("worstSeparation", 99999.0))
	var extent: Dictionary = bundle.terrain_extent
	var width := float(extent["x_max"]) - float(extent["x_min"])
	# 5% of the map width. The measured worst is ~140 units on a 6021-unit map
	# (2.3%); anything approaching 5% would mean the spaces had drifted apart.
	_check(worst < width * 0.05,
		"landmarks agree with authored region centres to better than 5% of map width",
		"worst %.1f units, map %.1f wide" % [worst, width])


func _check_view_places_regions(bundle) -> void:
	var view = MapViewScript.new()
	view.build()
	view.size = Vector2(1240, 620)
	view.set_bundle(bundle, "")
	_check(view.has_map(), "the view reports a map", "")
	# "The bundle parsed" and "there is geometry on screen" are two claims, and
	# only the second one is what a player sees. Exact, not a floor.
	_check(view.drawn_mesh_count() == EXPECTED_DRAWN_SUB_OBJECTS,
		"the view instanced retail's %d drawable sub-objects" % EXPECTED_DRAWN_SUB_OBJECTS,
		"instanced %d" % view.drawn_mesh_count())

	# THE FRAMING, before the regions, so that a missing living-world document
	# cannot take these three with it: the fit needs a map, not a session.
	_check_the_framing_fills_the_panel(view)

	# Feed the view retail's own region rows, built straight from the document.
	var session := _load_session()
	if session == null:
		_fail("session could not be built from the living-world document",
			"set OPENBFME_LIVING_WORLD_DOC")
		# Keep the check count honest: the three checks below cannot run, so
		# report them as failures rather than silently skipping.
		_fail("regions placed on the map", "no session")
		_fail("Rhun is refused a position", "no session")
		_fail("sampled heights sit inside the terrain", "no session")
		view.free()
		return

	var rows: Array[Dictionary] = session.region_rows()
	var adjacency: Dictionary = {}
	for region_id in session.world.region_ids:
		adjacency[String(region_id)] = session.world.neighbours(String(region_id))
	view.set_regions(rows, adjacency, PackedStringArray(), PackedStringArray(), "", "")

	var authored := 0
	for row in rows:
		if bool(row["has_position"]):
			authored += 1
	_check(view.placed_regions.size() == authored,
		"every region with an authored centre point is placed",
		"placed %d of %d authored" % [view.placed_regions.size(), authored])

	# The honesty rule, asserted rather than trusted: Rhun has no authored centre
	# point and livingmap.w3d carries no per-region sub-object to take one from,
	# so it must be listed unplaced and must NOT appear on the map.
	_check(Array(view.unplaced_regions).has("Rhun")
			and not Array(view.placed_regions).has("Rhun"),
		"Rhun is refused a position rather than given a plausible one",
		"unplaced %s" % str(view.unplaced_regions))

	# Heights are SAMPLED from retail terrain, so every placed region must sit
	# within the terrain's own vertical extent - a marker floating above the map
	# would mean the sample was invented.
	var z_min := float(bundle.terrain_extent["z_min"]) - 1.0
	var z_max := float(bundle.terrain_extent["z_max"]) + 1.0
	var outside: Array[String] = []
	for region_id in view.placed_regions:
		var authored_row: Dictionary = {}
		for row in rows:
			if String(row["id"]) == String(region_id):
				authored_row = row
				break
		var point := authored_row["position"] as Vector2
		var sampled: Dictionary = bundle.sample_height(point.x, point.y)
		if not bool(sampled["ok"]):
			continue
		var height := float(sampled["height"])
		if height < z_min or height > z_max:
			outside.append(String(region_id))
	_check(outside.is_empty(),
		"sampled region heights sit inside retail's terrain extent",
		"outside %s" % str(outside))

	view.free()


## THE PROJECTED FOOTPRINT, and the three things the fit before this one got
## wrong.
##
## That fit measured retail's terrain BOUNDING BOX and reduced its on-screen
## height to `depth * sin(pitch) + relief * cos(pitch)` - one number, symmetric,
## and therefore a description of a rectangle. A pitched perspective camera does
## not draw a rectangle. It draws a TRAPEZOID: the south edge is nearer the
## camera and projects wide, the north edge is further and projects narrow, and
## the two do not straddle the panel's centre line.
##
## Measured on the shipped bundle at the capture window - panel 1264x496,
## pitch -52, zoom 1 - the ground quad projected to y 71.9..568.5 on a panel 496
## tall. So 72 px of Middle-earth's south coast was cut off the bottom, a 72 px
## band along the top was empty, 18.28% of the map was not on screen at all, and
## nothing said so. That last part is the reason these checks exist: the defect
## was invisible to every assertion this lane had.
##
## All three are asserted AT A NON-ZERO YAW as well as at retail's opening orbit,
## and the third at a non-default zoom - the same discipline the resize property
## in `wotr_living_world_ui_runner` is held to, and for the same reason. A
## framing fix that only holds at yaw 0 and zoom 1 is a screenshot, not a fix.
func _check_the_framing_fills_the_panel(view) -> void:
	# Retail's own opening orbit, then a turned camera at a shallow angle and a
	# turned camera at a steep one.
	var orbits := [
		[0.0, MapViewScript.DEFAULT_PITCH_DEGREES], [1.1, -21.0], [-2.3, -70.0]]
	# AND AT MORE THAN ONE PANEL SHAPE, because the defect was aspect-dependent
	# and a single panel is how it survived. These are the map panel's REAL
	# measured sizes at the three windows the layout is asserted at - 1264x496 at
	# the authored 1860x800, 1868x1047 at the owner's 2560x1351, 760x396 at the
	# 1100x700 floor - plus this runner's own historical 1240x620. The
	# orthographic fit this replaces overflowed 1264x496 by 72 px and fitted
	# 1240x620 almost exactly, which is precisely why testing one shape proved
	# nothing.
	var panels := [
		Vector2(1264.0, 496.0), Vector2(1868.0, 1047.0),
		Vector2(760.0, 396.0), Vector2(1240.0, 620.0)]
	var original: Vector2 = view.size
	var worst_overshoot := 0.0
	var overshoot_at := ""
	var worst_offset := 0.0
	var offset_at := ""
	var framings := 0
	for panel_size in panels:
		view.size = panel_size as Vector2
		view._on_resized()
		for orbit in orbits:
			framings += 1
			view.reset_camera()
			view.set_orbit(float(orbit[0]), float(orbit[1]))
			var panel := Vector2(view.viewport.size)
			var box := _projected_footprint(view)
			# OVERSHOOT: how far outside the panel the footprint reaches, in px.
			var overshoot := maxf(
				maxf(-box.position.x, -box.position.y),
				maxf(box.end.x - panel.x, box.end.y - panel.y))
			if overshoot > worst_overshoot:
				worst_overshoot = overshoot
				overshoot_at = "panel %s yaw %.1f pitch %.0f: box %s" % [
					panel, orbit[0], orbit[1], box]
			# OFF-CENTRE: the gap between the two slacks, as a fraction of the
			# panel. This was 72 px - 14.5% of the panel height - before the fix.
			var offset := maxf(
				absf(box.position.x - (panel.x - box.end.x)) / maxf(panel.x, 1.0),
				absf(box.position.y - (panel.y - box.end.y)) / maxf(panel.y, 1.0))
			if offset > worst_offset:
				worst_offset = offset
				offset_at = "panel %s yaw %.1f pitch %.0f: %.1f/%.1f left/right, %.1f/%.1f top/bottom" % [
					panel, orbit[0], orbit[1],
					box.position.x, panel.x - box.end.x,
					box.position.y, panel.y - box.end.y]
	view.size = original
	view._on_resized()

	_check(worst_overshoot <= 0.5,
		"retail's whole map projects inside the panel at every orbit and panel shape",
		"worst overshoot %.2f px over %d framing(s)%s" % [
			worst_overshoot, framings,
			"" if overshoot_at.is_empty() else " (" + overshoot_at + ")"])
	# One percent of the panel. Not a matter of taste: the defect this replaces
	# was 14.5% of the panel height, so a tolerance fourteen times tighter than
	# the thing it guards against cannot be met by accident.
	_check(worst_offset <= 0.01,
		"the map is centred in the panel rather than riding low in it",
		"worst slack difference %.2f%% of the panel over %d framing(s)%s" % [
			100.0 * worst_offset, framings,
			"" if offset_at.is_empty() else " (" + offset_at + ")"])

	# THE CENTRING SCALES WITH THE ZOOM, and this is why that matters. It shifts
	# the camera AND the point it looks at, so if it did not scale with the
	# distance it would grow into a huge offset as the player zoomed in and
	# `focus_region` would carry its region off the panel. Scaled, it is a
	# CONSTANT offset on screen: the aimed point projects to the same pixel at
	# every zoom. Checked at a non-zero yaw, across the whole 33.8x range.
	view.reset_camera()
	view.set_orbit(1.1, -21.0)
	var aimed: Vector3 = view.camera_state()["target"]
	var seen: Array[Vector2] = []
	for zoom in [1.0, 0.31, MapViewScript.MIN_ZOOM]:
		view.focus_region("", float(zoom))
		seen.append(_project(view, aimed))
	var drift := 0.0
	for point in seen:
		drift = maxf(drift, point.distance_to(seen[0]))
	_check(drift <= 1.0,
		"zooming does not slide the framing across the panel",
		"the aimed point moved %.2f px across zoom 1.00 / 0.31 / %.2f (%s)" % [
			drift, MapViewScript.MIN_ZOOM, str(seen)])
	view.reset_camera()


## The screen-space bounding box of retail's own terrain extent - all eight
## corners, projected through the camera the fit actually placed.
##
## The projection is done here rather than by `Camera3D.unproject_position`
## because this runner drives a view that is not in a scene tree, and that method
## refuses with "Camera is not inside scene" and returns (0, 0) for every point -
## which would pass every check below silently, the worst possible failure for a
## framing test. It reads the camera's own TRANSFORM, which is what
## `look_at_from_position` wrote, so it is still checking the camera that was
## placed rather than re-running the arithmetic that placed it: a wrong distance
## or a wrong centring moves the transform and shows up here.
##
## `transform` and not `global_transform`: the camera's parent is a SubViewport,
## which carries no 3D transform of its own, so the two are the same thing - the
## view's own `_pan()` reads the basis the same way and says why.
func _project(view, world: Vector3) -> Vector2:
	var local: Vector3 = view.camera.transform.affine_inverse() * world
	var depth := maxf(-local.z, 0.0001)
	var panel := Vector2(view.viewport.size)
	var half_vertical := tan(deg_to_rad(view.camera.fov * 0.5))
	var half_horizontal := half_vertical * (panel.x / maxf(panel.y, 1.0))
	return Vector2(
		(local.x / (depth * half_horizontal) + 1.0) * 0.5 * panel.x,
		(1.0 - local.y / (depth * half_vertical)) * 0.5 * panel.y)


func _projected_footprint(view) -> Rect2:
	var extent: Dictionary = view.bundle.terrain_extent
	var low := Vector2(INF, INF)
	var high := Vector2(-INF, -INF)
	for xi in [float(extent["x_min"]), float(extent["x_max"])]:
		for yi in [float(extent["y_min"]), float(extent["y_max"])]:
			for zi in [float(extent["z_min"]), float(extent["z_max"])]:
				var point := _project(
					view, BundleScript.world_to_godot(xi, yi, zi))
				low.x = minf(low.x, point.x)
				low.y = minf(low.y, point.y)
				high.x = maxf(high.x, point.x)
				high.y = maxf(high.y, point.y)
	return Rect2(low, high - low)


## THE 90 THAT NEEDED A FOOTNOTE.
##
## Every report that quoted the region-image bundle said "90 regions", and the
## geometry probe said `shadedRegions=52 ... unshaded=38` - which reads as a
## conversion gap and is not one. Thirty-eight of those 90 rows are retail's own
## `Region_1`..`Region_38` placeholders: no display name, no `RegionPortrait`,
## no `Fortress.Portrait`. Retail's living-world document declares them in its
## EvilCampaign and GoodCampaign blocks, which list the SAME 38 ids rather than
## 38 each, beside DefaultCampaign's 52 named regions. 52 + 38 = 90.
##
## THE FIX WAS A REPORTING FIX AND THESE TWO CHECKS SAY SO: the first asserts the
## arithmetic of the split, and the second asserts that the placeholder rows are
## STILL THERE afterwards. Splitting a census by deleting the awkward half would
## satisfy the first on its own, which is exactly why the second exists.
func _check_the_region_census_is_split() -> void:
	var screen = ScreenScript.new()
	screen.build()
	screen.load_map_bundle([])
	var census: Dictionary = screen.region_portrait_census()
	var rows := int(census["rows"])
	var playable: Array = census["playable"] as Array
	var placeholders: Array = census["placeholders"] as Array
	var line: String = screen.region_portrait_census_line()

	if rows <= 0:
		# No region-image bundle. The census must SAY that rather than report a
		# confident zero, which would be the same silent-nothing this lane keeps
		# finding.
		_check(line.contains("no region-image bundle"),
			"with no region-image bundle the census says so rather than reporting zero",
			line)
		_check(playable.is_empty() and placeholders.is_empty(),
			"with no region-image bundle the census claims no regions of either kind",
			"%d playable, %d placeholder" % [playable.size(), placeholders.size()])
		screen.queue_free()
		return

	# THE ARITHMETIC, stated in the check's own detail so the numbers are in the
	# log rather than only in a comment.
	var adds_up := playable.size() + placeholders.size() == rows
	var every_placeholder_is_bare := true
	var wrong: Array[String] = []
	for region_id in placeholders:
		var row: Dictionary = screen.region_images.regions[String(region_id)] as Dictionary
		if not (String(row.get("regionPortrait", "")).is_empty()
				and String(row.get("fortressPortrait", "")).is_empty()
				and String(row.get("fortressDisplayName", "")).is_empty()):
			every_placeholder_is_bare = false
			wrong.append(String(region_id))
	_check(adds_up and every_placeholder_is_bare and not playable.is_empty(),
		"the region census splits the placeholder rows out of the playable count",
		"%d playable + %d placeholder = %d row(s)%s" % [
			playable.size(), placeholders.size(), rows,
			"" if wrong.is_empty() else "; NOT BARE: " + ", ".join(wrong)])

	# THE ROWS ARE KEPT. Every id on both sides of the split is still in the
	# bundle, the bundle's own total still agrees, and the report NAMES the
	# placeholders rather than reducing them to a number.
	var missing: Array[String] = []
	for region_id in placeholders + playable:
		if not screen.region_images.regions.has(String(region_id)):
			missing.append(String(region_id))
	var bundle_total := int(screen.region_images.totals.get("regions", -1))
	var names_them := placeholders.is_empty() or line.contains(String(placeholders[0]))
	_check(missing.is_empty() and bundle_total == rows and names_them,
		"the placeholder rows are kept and named rather than dropped from the bundle",
		"%d row(s) held, bundle totals say %d, the report names %s" % [
			rows, bundle_total,
			"none" if placeholders.is_empty() else String(placeholders[0])])
	screen.queue_free()


func _check_screen_fallback_is_labelled(have_bundle: bool) -> void:
	var screen = ScreenScript.new()
	screen.build()
	screen.load_map_bundle([])

	if have_bundle:
		_check(screen.map3d.visible and not screen.map_view.visible,
			"with a bundle the screen shows the 3D map and hides the 2D graph", "")
		_check(screen.map_reason.is_empty(),
			"with a bundle the screen carries no map refusal", screen.map_reason)
	else:
		_check(screen.map_view.visible and not screen.map3d.visible,
			"with no bundle the screen shows the 2D graph", "")
		_check(not screen.map_reason.is_empty(),
			"with no bundle the screen carries the reason", "")
		_check(screen.map3d.drawn_mesh_count() == 0,
			"with no bundle the 3D view instanced nothing", "")

	screen.refresh()
	var label: String = screen.map_mode_label.text
	_check(not label.is_empty(), "the screen states which map it is showing", "")
	if have_bundle:
		_check(label.contains("livingmap.w3d"),
			"the 3D label names the retail source", label)
		# Retail sub-objects this lane does not draw must be NAMED on screen. A
		# map that silently dropped its ocean and its rivers would look complete.
		_check(label.contains("NOT DRAWN") and label.contains("WATER")
				and label.contains("RIVERS"),
			"the 3D label names the sub-objects it does not draw", label)
		_check(label.contains("%d drawn" % EXPECTED_DRAWN_SUB_OBJECTS),
			"the 3D label reports how many sub-objects are on screen", label)
	else:
		_check(label.to_lower().contains("fallback"),
			"the 2D label calls itself a fallback", label)
		# The label is one line; the diagnosis is many. It must point at where the
		# diagnosis actually is, or the owner is back to guessing.
		_check(label.contains("[WotrMap]") and label.contains("log"),
			"the 2D label sends the owner to the launch log for the reason", label)

	# The rule the whole lane exists to keep: presentation must not be able to
	# reach the simulation. The screen holds the map; the session holds selection.
	_check(not screen.has_method("set_authoritative_state"),
		"the screen cannot write authoritative state", "")
	_check(screen.map3d.get("bundle") != null or not have_bundle,
		"the 3D view holds the bundle", "")

	screen.free()


# --- harness ------------------------------------------------------------------

func _check(condition: bool, what: String, detail: String) -> void:
	if condition:
		_ok(what, "")
	else:
		_fail(what, detail)


func _ok(what: String, detail: String) -> void:
	_passed += 1
	if detail.is_empty():
		print("  PASS  %s" % what)
	else:
		print("  PASS  %s (%s)" % [what, detail])


func _fail(what: String, detail: String) -> void:
	_failed += 1
	print("  FAIL  %s :: %s" % [what, detail])


## Build a session on the REAL living-world document, seated exactly the way the
## round-trip runner seats one. Returns null when no document is available, which
## the caller reports rather than skipping over.
func _load_session() -> SessionScript:
	var located: Dictionary = SessionScript.locate_document([])
	if not bool(located.get("ok", false)):
		return null
	var document := located["document"] as Dictionary

	var probe := SessionScript.new()
	var probe_world = load("res://src/wotr/wotr_world.gd").new()
	if not probe_world.load_from_dict(document, ""):
		return null
	probe.world = probe_world

	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	var options := probe.seat_options(availability)
	var seats: Array = []
	for option in options:
		seats.append({
			"template": String(option["template"]),
			"team": seats.size() + 1,
			"controller": StateScript.CONTROLLER_HUMAN if seats.is_empty() else StateScript.CONTROLLER_AI,
		})
		if seats.size() == 2:
			break
	var scenarios := probe.startable_scenarios(2)
	if scenarios.is_empty() or seats.size() < 2:
		return null

	var session := SessionScript.new()
	if not session.begin(document, probe_world.campaign_name, String(scenarios[0]), seats):
		return null
	return session
