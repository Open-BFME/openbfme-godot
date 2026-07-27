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
##                    + 6 (the screen labels the 2D fallback and stays sealed)
##                    = 8
##   with a bundle    = 2 (bundle located, no spurious reason)
##                    + 7 (sub-objects, landmarks, triangles, vertices, tiles,
##                         textures resolved, untextured really untextured)
##                    + 4 (grid exact, grid is 5x4, nine pairs, agreement bound)
##                    + 4 (view has map, regions placed, Rhun refused, heights in
##                         terrain)
##                    + 7 (the screen shows the 3D map, names what it does not
##                         draw, and stays sealed)
##                    = 24
const EXPECTED_CHECKS_NO_BUNDLE := 8
const EXPECTED_CHECKS_WITH_BUNDLE := 24

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
	if have_bundle:
		_check_bundle(bundle)
		_check_coordinate_space(bundle)
		_check_view_places_regions(bundle)
	_check_screen_fallback_is_labelled(have_bundle)

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
	else:
		_check(label.to_lower().contains("fallback"),
			"the 2D label calls itself a fallback", label)

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
