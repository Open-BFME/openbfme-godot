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
## The HUD's shared type scale, read rather than restated: the build ring's
## captions are lettering set over the map and must obey the same floor.
const HudChromeScript = preload("res://src/wotr/wotr_hud_chrome.gd")

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
##                    + 3 (the whole map is inside the panel at zoom 1, centred
##                         in it, and the framing does not slide as the zoom
##                         moves)
##                    + 4 (the OPENING framing is full-bleed; the zoom-out clamp
##                         cannot pull back far enough to reveal the slab end;
##                         the clamp is the COMPUTED per-aspect ceiling rather
##                         than a constant tuned at one window shape; and at
##                         that ceiling the four panel CORNERS are terrain, not
##                         merely cloud)
##                    + 2 (the region census is split, and the placeholder rows
##                         are kept rather than dropped)
##                    = 38
##
## ROUND 3 RAISED THIS FROM 36 TO 38, and tightened one of the checks that was
## already here rather than adding two beside it. The old "maximum pull-back is
## clamped" check asserted the clamp landed on the CONSTANT `MAX_ZOOM`, and
## allowed the four panel corners to show the border cloud. Both were too
## generous, and the picture proved it: at 1860x800 - the window the capture
## runner photographs, and the one a blind art review judged - the terrain
## slab's own western cut edge ran down the left quarter of the frame as a hard
## diagonal, and this check passed anyway, because the cut was outside the 20%
## interior grid and the corners past it were legitimately cloud. So the clamp
## is now asserted against the view's own COMPUTED per-aspect ceiling, the
## corners are required to be TERRAIN at that ceiling, and the panel list
## includes the shapes that actually get photographed.
##
## THE ARITHMETIC OF THE MOVES. Nothing was removed and nothing was weakened.
## With a bundle: 29 + 3 framing + 2 census = 34, RAISED to 36 by the two
## full-bleed checks - the containment and centring checks still run, pinned
## explicitly to zoom 1.0 (the fit they have always asserted), because the
## OPENING zoom is no longer 1.0: retail's map overfills the screen, and the
## default framing is now checked for exactly that. With no bundle: 14 + 2
## census = 16 - the census checks run in both modes because the region-image
## bundle is found independently of the map bundle, and the framing ones do
## not, because a framing check needs a map to frame.
##
## ROUND 4 RAISES THE BUNDLE TOTAL FROM 38 TO 43 with five checks on ONE defect,
## because a blind review of the judged capture reported "no faction ownership
## colour on the map, only a red selection wash" and the investigation found a
## real renderer bug behind it rather than a taste miss. The band materials are
## written with albedo over 1.0 so the environment's bloom threshold catches
## them, the environment tonemaps LINEAR, and four of the six seat colours had
## TWO channels over 1.0 - so they clipped to white and lost their hue on the way
## to the screen. Seats 0 (#4d7fd6) and 5 (#3fb0ad) rendered as the SAME pale
## cyan, hue 180 both. Nothing in this file could see that, because everything
## here asserted on geometry and placement and nothing asserted on what colour
## reached the frame.
##
## So the five new checks all assert on `rendered_band_color` and on the live
## materials `territory_materials` hands back - never on the seat constants:
##
##   + 1 (every seat that owns a region renders a band that still carries its own
##        hue and real chroma after the tonemapper clips it)
##   + 1 (the seat palette renders as SIX separable colours, not four whites)
##   + 1 (with N seats owning regions, N distinct owner tints are on the drawn
##        materials, and every owned region carries one)
##   + 1 (selection is a different KIND of mark from ownership: an achromatic
##        core inside a halo that is still the owner's hue, not the same hue
##        slightly brighter)
##   + 1 (an unclaimed region is given no owner tint, and retail's own selection
##        geometry is named as a gap rather than substituted)
##
## ROUND 5 RAISES THE BUNDLE TOTAL FROM 43 TO 46, and REWRITES two of round 4's
## five rather than adding beside them, because the fact they were written
## against changed: the converter now writes the `edge` and `highlight` layers at
## a measured 52/52 coverage, so retail's own `RegionSelectionEffect` and
## `HomeRegionHighlight` meshes exist to be drawn and the stand-ins are gone.
## Both rewrites are STRICTLY STRONGER than what they replace:
##
##   * round 4's "the selected region's band goes achromatic" asserted the
##     STAND-IN. It is now "the selection ring is retail's own separate mesh, it
##     is lit on the selected region and on no other, it clears the glow
##     threshold, AND the ownership band under it still carries the owner's hue
##     at full chroma" - the last clause is a property the round-4 path FAILED by
##     construction.
##   * round 4's "the gap sentence names lmr_edge.w3d" asserted that a STRING was
##     well written. It is now "both layers are present, the converter reports no
##     hole in either, and every drawn region carries both meshes as live
##     materials", plus the one gap this view still owns (nothing hands in a
##     seat's capital, so no home region is guessed).
##
## And two checks are genuinely new:
##
##   + 1 (every attack target carries its OWN ring, in the ATTACKING seat's hue,
##        nothing outside the target set is ringed, and the ring is quieter than
##        the selection's - the "flat uniform wash of near-pure red with no
##        per-region separation" defect, in the frame rather than in the intent)
##   + 1 (the additive border shoulders are displaced INWARDS, concentric with
##        retail's own band, so an owned region's glow can never land on a
##        neighbour's ground - the merged-blob and the magenta-stroke defects,
##        which are one sign error reported twice)
##
##   + 1 (retail's `LMR_Edge` is a standing CURTAIN - no ground area, rising clear
##        of its own terrain - and its `LMR_Highlight` is a flat inner band, on
##        every region. Neither had ever been drawn by anything in this project,
##        the working assumption about the edge was wrong, and the colouring above
##        is built on both facts)
##
## The arithmetic: 43 rewritten in place, nothing removed, + 3 = 46.
##
## ROUND 7 RAISES THE BUNDLE TOTAL FROM 50 TO 58 with eight checks on the camera
## the owner actually played - see `_check_the_camera_answers_to_the_player` for
## the quote and for each one's defect. Nothing was removed and one existing check
## was TIGHTENED rather than added beside: "the opening framing presents at least
## 70% of retail's own map area" is now 90%, which the 12-degree perspective lens
## could not reach at any placement (it measured 78.1%) and the parallel
## projection measures 95.8%.
##
##   + 1 (the camera is a PARALLEL projection, framed by the fit times the zoom -
##        the projection is what bounded the reachable coverage, not the tuning)
##   + 1 (the view opens at the cut-edge rule's own ceiling rather than at a
##        constant sitting under it, which is "zoom all the way out" as a
##        property)
##   + 1 (WASD and the arrows are both bound, to the same four directions)
##   + 1 (a HELD key pans, and one second of it covers the same fraction of the
##        picture at every zoom rather than a fixed number of world units)
##   + 1 (a focused text field takes the keyboard away from the camera and gives
##        it back)
##   + 1 (a wheel notch keeps the ground under the pointer under the pointer)
##   + 1 (a right-drag carries the ground it grabbed, at a non-default pitch -
##        the pitch term the old fixed-fraction pan was missing)
##   + 2 (a pan LEANS INTO the cut-edge wall - it neither buys its way through it
##        with the player's zoom nor stops dead against it. Round 8 restated this
##        from a single "and STOPS" claim; see the check itself for the two
##        measurements that forced the restatement)
## THE TWO ENDS OF THE CUT-EDGE WALL'S BEHAVIOUR. Neither is a tuned number: the
## floor on the zoom is comfortably above what the DIVE this rule exists to stop
## leaves behind (26-36% kept in one second, measured) and comfortably below what
## the live lean keeps (89%); the floor on the travel is comfortably above what
## the DEAD STOP left (4.6 units, measured) and comfortably below what the lean
## covers (334 units). A regression to either failure mode misses its floor by
## most of an order of magnitude.
const PAN_WALL_ZOOM_KEPT_FLOOR := 0.85
const PAN_WALL_MIN_TRAVEL := 200.0

const EXPECTED_CHECKS_NO_BUNDLE := 16
## ROUND 8 RAISES THE BUNDLE TOTAL FROM 58 TO 64 with six checks on ONE defect
## the owner reported in his own words: "when I hover over an area it should
## light up and allow me to select the ENTIRE area instead of just the node point
## that has a lower selection." Picking was a 16-pixel disc around the region's
## marker, and nothing in this file could see that, because everything here
## asserted on where a region was PLACED and nothing asserted on what the pointer
## could HIT. See `_check_picking_is_by_the_regions_own_polygon`.
## AND FROM 64 TO 65 by splitting the cut-edge wall's single "it stops" assertion
## into the two independent claims it always confused - see check 8 in
## `_check_the_camera_answers_to_the_player`.
## AND FROM 65 TO 67 with the build ring's input path, which had none at all - see
## `_check_the_build_ring_can_be_pointed_at`.
## AND FROM 67 TO 68 WITH THE REFRAMING. The camera's subject changed from retail's
## terrain slab to the playable region set, and THREE existing checks were RESTATED
## IN PLACE rather than deleted - each one carries the old wording, the defect it
## was written for, and the measurement that made it the wrong statement of the new
## intent:
##   * "retail's whole map projects inside the panel" -> "the whole PLAYABLE REGION
##     SET projects inside the panel" (the old form passed while Harad hung 117.6
##     px off the bottom of a 1860x800 frame);
##   * "the map is centred in the panel rather than riding low in it" -> "the
##     framing sits dead centre EXCEPT WHERE RETAIL'S OWN CUT EDGE FORBIDS IT",
##     which is strictly stronger than the tolerance it replaces;
##   * "the opening framing presents AT LEAST 90% of retail's own map area" ->
##     "the opening framing spends the panel on the playable board", the same
##     number turned from a floor into a ceiling.
## The one genuinely new check is the +1:
##   + 1 (every one of retail's playable regions is inside the panel at the OPENING
##        framing, after `zoom_ceiling()` has had its say - the containment check
##        pins the fit, this pins the picture)
## AND FROM 69 TO 70 with the build ring's HUD clearance - the ring clamped
## against the panel only, and the chrome stream photographed it opening on top of
## the Treasury plate with two icons off the screen edge.
## AND FROM 68 TO 69 with the mouseover flare's falloff - the flare was a flat
## additive wash for seven rounds and nothing asserted otherwise. See
## `_check_the_hover_flare_falls_off`.
## THE CUT-EDGE PROPERTY IS UNTOUCHED. "Maximum pull-back is clamped before the
## slab's cut edge can enter the frame at any supported aspect" reads exactly as it
## did; it is the one thing that must survive a reframing, because it is what stops
## the raw polygon silhouette reappearing.
## ROUND 9 RAISES THE BUNDLE TOTAL FROM 70 TO 76 with six checks, all of them in
## `_check_the_round_nine_map_intent`, all against one adversarial art-direction
## review of the round-8 capture. Nothing was removed and one existing check was
## RESTATED rather than deleted (the dead-centre framing rule - see the note over
## it for the second licence it now carries and the three conditions that licence
## has to satisfy). The six:
##
##   + 1 (the build ring writes NO price on the map at all and no name until a
##        slot is pointed at - the review's own "delete the on-map text label
##        layer, costs and all", asserted with entries that DO carry costs so the
##        view is proved to be dropping them)
##   + 1 (at the opening framing the map is lettered by retail's own engraved text
##        plane and this view writes no region names on it, and the threshold is
##        asserted at BOTH ends so "cull everything always" fails too)
##   + 1 (Mordor, the Black Gate, Cirith Ungol, Minas Tirith and Osgiliath are
##        clear of the band the HUD stands on, not merely on the panel - "you hid
##        Mordor", answered where the player can see it)
##   + 1 (the occluded band fails closed: the chrome's islands win when they cover
##        the foot of the panel across its whole width, and the stated assumption
##        wins when they do not, because the framing bias and the cut-edge inset
##        are both solved against that number)
##   + 1 (the aerial haze is keyed to the theatre rather than to the panel, and an
##        empty board is given none - "do not add UI there, add air", with the
##        difference between air and a vignette made testable)
##   + 1 (the legal-target reticles and the selection curtain breathe, and a board
##        with nothing to act on does not pay for it)
const EXPECTED_CHECKS_WITH_BUNDLE := 76

## HOW MUCH HUE A BAND MAY LOSE ON THE WAY TO THE SCREEN, in degrees. Not zero,
## and it cannot be: the band is driven over 1.0 so its brightest channel clips
## on purpose, and a clipped channel drags the hue towards the nearest primary.
## Measured across the six seat colours the worst drift is 20.0 degrees (seat 4,
## violet, which lands on magenta); under the round-3 path the worst was 38.1
## degrees (seat 0, blue, which landed on cyan) with the chroma gone as well.
const BAND_HUE_TOLERANCE := 22.0
## The chroma a rendered band must still have. Round 3's six seats rendered at
## saturations 0.13 to 0.62 - the 0.13 is seat 4, a magenta that reached the
## screen as very nearly white. All six now land between 0.56 and 1.00.
const BAND_SATURATION_FLOOR := 0.55
## How far apart two seats' rendered bands must be, in degrees of hue, for a
## player to tell two owners apart. Round 3 put seats 0 and 5 at ZERO degrees
## apart. They are now 26.1 degrees apart, which is the closest pair.
const BAND_HUE_SEPARATION := 24.0
## The chroma ceiling for a SELECTED region's core. Selection is achromatic by
## construction (retail's own `neutralRegion` white), so this is well clear of
## `BAND_SATURATION_FLOOR` - the gap between the two is the assertion that the
## two marks cannot be confused for each other.
const SELECTION_CORE_SATURATION_CEILING := 0.10

## Retail's 64 sub-objects minus the 19 this lane holds back (the impassable
## volumes, the RIVERS underlay, the two lava frame-sequence planes, and the
## six vertex-alpha smoke cards). RAISED from 34 to 38 when the
## water/cloud/text surfaces stood, and from 38 to 45 in round 2: the six
## LM_COAST strips now composite BOTH of retail's shoreline stages (foam over
## reflection), and Mount Doom's PLANE03 cross-fades retail's own two frames
## inside frame 0's authored alpha. NOT 51: the Orthanc and lava-vent cards
## were tried and measured out - both their frames are flat mid-grey sheets,
## the plume shape lives in vertex alpha the bundle does not carry, and they
## rendered as the grey rectangles they are. They stay held back and named.
const EXPECTED_DRAWN_SUB_OBJECTS := 45

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

## RETAIL'S REGION TERRITORY GEOMETRY, LOADED ONCE FOR THE WHOLE RUN.
##
## It used to be loaded inside `_check_ownership_reaches_the_screen` alone, and
## in round 8 that stopped being good enough: the camera's framing is now solved
## against the PLAYABLE REGION SET rather than against retail's terrain slab (see
## `wotr_map_view.gd:_framing_box`), so a framing check run on a view with no
## region bundle bound is a check of the FALLBACK framing, not of the one the
## player gets. Loading it here and handing the same instance to every view that
## needs one keeps that from happening again by construction, and costs one load
## instead of three.
var _geometry = null
var _geometry_reason := ""
var _geometry_probed := false


## The shared geometry, or null plus a reason a caller can print. Loaded lazily so
## a run with no map bundle never pays for it.
func _region_geometry():
	if _geometry_probed:
		return _geometry
	_geometry_probed = true
	var geometry = load("res://src/wotr/wotr_region_geometry.gd").new()
	var located: Dictionary = geometry.locate_and_load([])
	if not bool(located.get("ok", false)):
		_geometry_reason = "region territory geometry unavailable: %s" % String(
			located.get("reason", ""))
		return null
	_geometry = geometry
	return _geometry


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
	# cannot take these checks with it: the fit needs a map, not a session.
	#
	# ON ITS OWN VIEW, WITH RETAIL'S REGION GEOMETRY BOUND, and both halves of that
	# are deliberate. Bound, because round 8 solves the fit against the PLAYABLE
	# REGION SET (`wotr_map_view.gd:_framing_box`) and a framing check on a view
	# with no region bundle would be checking the slab fallback rather than the
	# camera the player gets. Its own view, because binding the geometry also gives
	# the view retail's polygon CENTROIDS, and the two placement checks below exist
	# precisely to assert what happens when a region has no authored centre and no
	# centroid to fall back on.
	var framing_view = MapViewScript.new()
	framing_view.build()
	framing_view.size = Vector2(1240, 620)
	framing_view.set_bundle(bundle, "")
	var framing_geometry = _region_geometry()
	if framing_geometry != null:
		framing_view.set_region_geometry(framing_geometry, "")
	print("[framing] the camera frames: %s" % framing_view.framing_source())
	_check_the_framing_fills_the_panel(framing_view)
	_check_the_hover_flare_falls_off(framing_view)
	framing_view.free()
	_check_the_camera_answers_to_the_player(bundle)

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

	_check_ownership_reaches_the_screen(view, rows, adjacency)
	_check_picking_is_by_the_regions_own_polygon(view, bundle)
	_check_the_build_ring_can_be_pointed_at(view, session)
	_check_the_round_nine_map_intent(view, session)

	view.free()


## THE BUILD RING'S ICONS ARE REACHABLE BY THE POINTER.
##
## THE OWNER'S WORDS: "I cannot click on the buildings or build them with the
## icons as they don't light up and do not allow me to build them." Both halves
## of that were ONE absence, and it is the kind only a check like this one can
## catch: `radial_slots()` has computed a hit box per entry for several rounds,
## every capture of the ring photographed correctly placed icons, and NOTHING
## EVER TESTED THOSE BOXES AGAINST THE MOUSE. The ring was a picture of a menu.
## There was no signal to emit either, so the screen had nothing to connect.
##
## Asserted on the RESOLUTION rather than on the signal, because a signal that
## fires with the wrong entry is the same defect wearing a different face: the
## centre of every slot must resolve to that slot's own entry, and a point off
## the ring must resolve to nothing at all.
func _check_the_build_ring_can_be_pointed_at(view, session) -> void:
	# Retail's own build entries are the screen's to offer; this runner is about
	# the INPUT PATH, so the ring is opened on a synthetic offer whose ids are
	# distinguishable. The boxes are `radial_slots()`'s, which is the code the
	# player's pointer is actually tested against.
	var entries: Array[Dictionary] = []
	for index in 4:
		entries.append({"id": "entry_%d" % index, "title": "Entry %d" % index})
	# RETAIL'S OWN AUTHORED `BuildingSpot` POINTS, for the first drawn region that
	# has any. Nothing is placed here: the ring has to open somewhere a plot really
	# is, or the boxes under test are boxes no player could ever reach.
	var plots: Dictionary = {}
	var region_id := ""
	for candidate in view.shaded_regions:
		var spots: Array = session.world.region(
			String(candidate)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		var points: Array[Vector2] = []
		for spot in spots:
			var row := spot as Dictionary
			points.append(Vector2(float(row.get("x", 0)), float(row.get("y", 0))))
		region_id = String(candidate)
		plots[region_id] = points
		break
	if region_id.is_empty() or plots.is_empty():
		_fail("every build-ring slot resolves to its own entry under the pointer",
			"no drawn region with an authored build plot to open the ring on")
		_fail("a point off the build ring resolves to no entry",
			"no drawn region with an authored build plot to open the ring on")
		return
	view.set_overlays({}, plots, {}, {"region": region_id, "index": 0}, entries)
	# The projection the ring is placed from is recomputed on the draw; asked for
	# directly here because a headless run never paints.
	view._plot_screen_positions = view.project_plots()
	var slots: Array[Dictionary] = view.radial_slots()
	var wrong: Array[String] = []
	if slots.size() != entries.size():
		wrong.append("the ring offered %d slot(s) for %d entries" % [
			slots.size(), entries.size()])
	for index in slots.size():
		var box := slots[index]["box"] as Rect2
		var wanted := String((slots[index]["entry"] as Dictionary)["id"])
		var got: Dictionary = view.build_entry_at(box.get_center())
		if String(got.get("id", "")) != wanted:
			wrong.append("the centre of slot %d (%s) resolved to '%s'" % [
				index, wanted, String(got.get("id", "<nothing>"))])
	_check(wrong.is_empty() and not slots.is_empty(),
		"every build-ring slot resolves to its own entry under the pointer",
		"%d slot(s); %s" % [slots.size(), str(wrong)])
	# WELL OUTSIDE THE RING, so this cannot pass by the boxes merely being small:
	# twice the ring's own radius from its centre.
	var away: Vector2 = view.radial_centre() + Vector2(view.radial_radius() * 2.5, 0.0)
	_check(view.build_entry_at(away).is_empty(),
		"a point off the build ring resolves to no entry",
		"probed %s, %.0f px from the ring's centre" % [
			str(away), view.radial_radius() * 2.5])

	# THE RING OPENS CLEAR OF THE HUD, NOT MERELY INSIDE THE PANEL.
	#
	# The chrome stream photographed the defect this pins
	# (`captures/chrome-r2-final/15b-structure-raised.png`): the ring landed in the
	# top-left corner ON TOP of the Treasury plate, with two of its icons cut off
	# by the screen edge. `radial_centre()` clamped against the PANEL and nothing
	# else, which is the right rule for a decoration and the wrong one for a
	# control - the ring's icons spend treasure now, and a live button under
	# another panel is a misclick factory.
	#
	# THE RECTANGLES ARE HANDED IN, NOT GUESSED. This view does not own the HUD, so
	# the check drives `set_hud_keep_out` with the chrome's own island geometry at
	# the 2560x1440 window - the stats plate, the palantir dish and the details
	# tray, at the positions the capture runner prints for them - and asserts the
	# ring clears all three AND still fits the panel. Every slot box is checked,
	# not only the centre, because it is the boxes the pointer is tested against.
	var islands: Array[Rect2] = [
		Rect2(2.6, 0.0, 366.4, 334.4),        # StrategicStats
		Rect2(0.0, 960.0, 960.0, 480.0),      # StrategicPalantir
		Rect2(702.3, 1035.2, 1853.3, 390.8),  # StrategicDetailsTray
		Rect2(2142.0, 0.6, 415.0, 106.9),     # StrategicEndTurnButton
	]
	var original: Vector2 = view.size
	view.size = Vector2(2560.0, 1440.0)
	view._on_resized()
	view.set_hud_keep_out(islands)
	var collisions: Array[String] = []
	# EVERY AUTHORED PLOT OF THE REGION, not one: the defect only shows for a plot
	# that projects under an island, and which plot that is depends on the framing.
	var plot_count: int = (plots[region_id] as Array).size()
	for plot_index in plot_count:
		view.set_overlays({}, plots, {}, {"region": region_id, "index": plot_index},
			entries)
		view._plot_screen_positions = view.project_plots()
		var centre: Vector2 = view.radial_centre()
		if centre == Vector2.ZERO:
			continue
		var reach: float = view.radial_radius() \
			+ MapViewScript.RADIAL_ICON * view._view_scale() * 0.5 \
			+ MapViewScript.RADIAL_CAPTION_GAP
		var footprint := Rect2(centre - Vector2(reach, reach),
			Vector2(reach * 2.0, reach * 2.0))
		for island in islands:
			if island.intersects(footprint):
				collisions.append("plot %d: the ring at %s overlaps %s" % [
					plot_index, str(centre), str(island)])
		for slot in view.radial_slots():
			var box := slot["box"] as Rect2
			if box.position.x < 0.0 or box.position.y < 0.0 \
					or box.end.x > 2560.0 or box.end.y > 1440.0:
				collisions.append("plot %d: slot box %s is off the panel" % [
					plot_index, str(box)])
	view.set_hud_keep_out([])
	view.size = original
	view._on_resized()
	_check(collisions.is_empty(),
		"the build ring opens clear of the HUD's own islands, not merely inside the panel",
		"%d plot(s) checked against %d island(s)%s" % [
			plot_count, islands.size(),
			"" if collisions.is_empty() else "; " + "; ".join(collisions)])
	view.set_overlays({}, {}, {}, {}, [] as Array[Dictionary])


## ------------------------------------------------------------------------------
## ROUND 9: WHAT THE MAP LAYER MAY WRITE ON MIDDLE-EARTH, WHERE IT AIMS, AND THAT
## IT MOVES. Six checks, all against one art-direction review of the round-8
## capture, all pinned as the properties rather than as the pixels.
## ------------------------------------------------------------------------------
##
## The review's own verdict on the frame was that its single highest-value change
## was "delete the on-map text label layer, costs and all", and its verdict on the
## tray trade was "the trade was right in principle and wrong in execution ... you
## have to pay for it, and you currently do not". These are the receipts.
func _check_the_round_nine_map_intent(view, session) -> void:
	var original: Vector2 = view.size
	view.size = Vector2(2560.0, 1440.0)
	view._on_resized()
	view.reset_camera()
	var panel := Vector2(view.viewport.size)

	# 1. NO PRICE EVER REACHES THE TERRAIN, AND AT MOST ONE NAME DOES.
	#
	# The ring is opened on a real authored plot with entries that DO carry costs -
	# the point is that the view drops them, not that it was never offered any -
	# and the caption set is read at rest and under the pointer.
	var entries: Array[Dictionary] = []
	for index in 4:
		entries.append({
			"id": "entry_%d" % index, "title": "Structure %d" % index,
			"cost": "1500",
		})
	var plots: Dictionary = {}
	var region_id := ""
	for candidate in view.shaded_regions:
		var spots: Array = session.world.region(
			String(candidate)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		var points: Array[Vector2] = []
		for spot in spots:
			var row := spot as Dictionary
			points.append(Vector2(float(row.get("x", 0)), float(row.get("y", 0))))
		region_id = String(candidate)
		plots[region_id] = points
		break
	var lettering: Array[String] = []
	if region_id.is_empty():
		lettering.append("no drawn region with an authored build plot to open the ring on")
	else:
		view.set_overlays({}, plots, {}, {"region": region_id, "index": 0}, entries)
		view._plot_screen_positions = view.project_plots()
		var at_rest: PackedStringArray = view.ring_captions()
		if at_rest.size() != 0:
			lettering.append("the ring letters %d slot(s) at rest: %s" % [
				at_rest.size(), str(at_rest)])
		view.hover_build_entry_at("entry_2")
		var hovered: PackedStringArray = view.ring_captions()
		if hovered.size() != 1:
			lettering.append("the ring letters %d slot(s) under the pointer, not 1: %s"
				% [hovered.size(), str(hovered)])
		for caption in hovered:
			if String(caption).contains("1500"):
				lettering.append("a price reached the terrain: '%s'" % String(caption))
			if String(caption) != "Structure 2":
				lettering.append("the lettered slot is not the one under the pointer: '%s'"
					% String(caption))
		view.hover_build_entry_at("")
		if view.ring_captions().size() != 0:
			lettering.append("the caption survived the pointer leaving the ring")
	_check(lettering.is_empty(),
		"the build ring writes no price on the map and no name until a slot is pointed at",
		"" if lettering.is_empty() else "; ".join(lettering))
	view.set_overlays({}, {}, {}, {}, [] as Array[Dictionary])

	# 2. AND THE REGION NAMES ARE RETAIL'S ENGRAVING, NOT THIS VIEW'S TYPE, at the
	#    framing the screen opens on. Asserted at BOTH ends of the threshold, so a
	#    regression that culls everything at every zoom fails as loudly as one that
	#    puts the sans names back over the engraving.
	view.reset_camera()
	var lettered_at_opening: bool = view.map_is_lettered_by_the_text_plane()
	view.focus_region("", MapViewScript.MIN_ZOOM)
	var lettered_up_close: bool = view.map_is_lettered_by_the_text_plane()
	view.reset_camera()
	_check(lettered_at_opening and not lettered_up_close,
		"at the opening framing the map is lettered by retail's own text plane and this view writes no region names on it",
		"opening=%s, closest=%s (threshold %.2f of the reachable pull-back)" % [
			lettered_at_opening, lettered_up_close, MapViewScript.LABEL_REVEAL_FRACTION])

	# 3. MORDOR IS NOT HIDDEN. The review: "You hid Mordor. In a Middle-earth
	#    strategy layer, Mordor is not a peripheral province, it is the
	#    antagonist ... if the camera can be biased at all, bias it south." The
	#    existing south-east check requires these five to be on the PANEL; this one
	#    requires them to be clear of the band the HUD stands on, which is the
	#    thing the player can actually see. Every coordinate is a retail
	#    sub-object's own baked bound.
	var occluded: float = view.occluded_bottom()
	var visible_floor := panel.y - occluded
	var buried: Array[String] = []
	var seen: Array[String] = []
	for name in ["LM_MINASTIRITH", "LM_OSGILLIATH", "LM_BLACKGATE",
			"LM_CIRITHONGUL", "LM_LAVA01THISON"]:
		var row: Dictionary = view.bundle.sub_object(String(name))
		if row.is_empty():
			buried.append("%s is not in the bundle at all" % String(name))
			continue
		var centre: Vector3 = ((row["bounds_min"] as Vector3)
			+ (row["bounds_max"] as Vector3)) * 0.5
		var at := _project(view, BundleScript.world_to_godot(
			centre.x, centre.y, centre.z))
		if at.y > visible_floor:
			buried.append("%s projects to y %.0f, under the %.0f px the HUD stands on"
				% [String(name), at.y, occluded])
	# AND THE PROVINCES THEMSELVES, WHICH IS THE HALF THAT PINS THE BIAS. The five
	# landmarks above already cleared the tray at round 8's framing - measured, and
	# it is why they are not enough on their own: Minas Tirith's marker sat 22 px
	# above the tray's edge and the PROVINCE OF MORDOR's own centre was 22 px
	# BELOW it. These four are the ones the southward bias moves across the line
	# (Mordor 1057 -> 979, Osgiliath 1047 -> 968, Minas Tirith 1009 -> 931, Mount
	# Doom 958 -> 879 at 2560x1440), so removing the bias fails this.
	var geometry = _region_geometry()
	for name in ["Mordor", "Osgiliath", "Minas_Tirith", "Mount_Doom"]:
		if geometry == null:
			buried.append("no region geometry to place %s with" % String(name))
			continue
		var mesh: ArrayMesh = geometry.region_mesh(String(name), geometry.FILL_LAYER)
		if mesh == null or mesh.get_surface_count() == 0:
			buried.append("%s has no fill mesh in the bundle" % String(name))
			continue
		var at := _project(view, mesh.get_aabb().get_center())
		seen.append("%s y %.0f" % [String(name), at.y])
		if at.y > visible_floor:
			buried.append("the province of %s projects to y %.0f, under the %.0f px the HUD stands on"
				% [String(name), at.y, occluded])
	_check(buried.is_empty(),
		"Mordor, the Black Gate, Cirith Ungol, Minas Tirith and Osgiliath are clear of the tray, not merely on the panel",
		"visible field is y 0..%.0f of %.0f (%s)%s" % [
			visible_floor, panel.y, ", ".join(seen),
			"" if buried.is_empty() else "; " + "; ".join(buried)])

	# 4. THE OCCLUDED BAND FAILS CLOSED. This view does not own the HUD, so the
	#    band it hands over is a STATED assumption that the chrome's own islands
	#    override - and a set of islands that does NOT cover the foot of the panel
	#    across its whole width must fall back to the assumption rather than
	#    reporting a band the player can see the map through. The bias and the
	#    cut-edge inset are both solved against this number, so a wrong one here is
	#    a raw slab rim on screen.
	var stated: float = panel.y * MapViewScript.HUD_OCCLUDED_BOTTOM_FRACTION
	view.set_hud_keep_out([])
	var with_nothing: float = view.occluded_bottom()
	# A tray that spans only the right half of the foot: not occlusion.
	view.set_hud_keep_out([Rect2(1280.0, 1035.0, 1280.0, 405.0)])
	var with_a_gap: float = view.occluded_bottom()
	# The composed screen's own islands, which together do cover the foot.
	view.set_hud_keep_out([
		Rect2(0.0, 960.0, 960.0, 480.0), Rect2(702.3, 1035.2, 1853.3, 404.8)])
	var with_the_hud: float = view.occluded_bottom()
	view.set_hud_keep_out([])
	_check(is_equal_approx(with_nothing, stated) and is_equal_approx(with_a_gap, stated)
			and with_the_hud > stated,
		"the band the map hands to the HUD is the chrome's own when the chrome covers the foot, and the stated assumption when it does not",
		"nothing handed in %.1f, half-covered foot %.1f, real islands %.1f, stated %.1f" % [
			with_nothing, with_a_gap, with_the_hud, stated])

	# 5. THE AIR IS KEYED TO THE WAR, NOT TO THE PANEL. The review: "the right 55%
	#    of the map is a dead zone at full visual weight ... do not add UI there -
	#    add air." A haze centred on the middle of the frame would be a vignette
	#    wearing the idea's clothes, so what is asserted is that the clear window
	#    sits on the regions the player is acting on and that an empty board draws
	#    nothing at all.
	view._project_positions()
	var focus: Dictionary = view.theatre_focus()
	var air: Array[String] = []
	if int(focus["regions"]) <= 0:
		air.append("no theatre was found on a live board")
	else:
		var centre := focus["centre"] as Vector2
		# It must sit on the seat's own holdings, so the midpoint of their screen
		# positions is what it is compared against - not the panel's midpoint.
		var owned: Array[Vector2] = []
		for row in view.rows:
			if int(row["owner"]) >= 0 and view._screen_positions.has(String(row["id"])):
				owned.append(view._screen_positions[String(row["id"])] as Vector2)
		if owned.is_empty():
			air.append("no owned region projected, so the claim cannot be tested")
		else:
			var midpoint := Vector2.ZERO
			for point in owned:
				midpoint += point
			midpoint /= float(owned.size())
			if centre.distance_to(midpoint) > panel.length() * 0.12:
				air.append("the clear window is %.0f px from the owned regions' own midpoint"
					% centre.distance_to(midpoint))
			if centre.distance_to(panel * 0.5) < 1.0:
				air.append("the clear window is the panel's centre, which is a vignette")
	var empty_view = MapViewScript.new()
	empty_view.build()
	empty_view.size = Vector2(1240, 620)
	if int(empty_view.theatre_focus()["regions"]) != 0:
		air.append("a board with no regions still claims a theatre")
	empty_view.free()
	_check(air.is_empty(),
		"the aerial haze is keyed to the theatre the player is fighting over, and an empty board is given none",
		"theatre %s%s" % [str(focus), "" if air.is_empty() else "; " + "; ".join(air)])

	# 6. AND IT MOVES. The review: "nothing in this frame telegraphs that it is
	#    animated ... it needs at minimum a lit, pulsing 'you can act here' state,
	#    and there isn't one." Both marks that carry the breath are asserted, and
	#    so is the rule that a board with nothing to act on does not pay for it.
	var motion: Array[String] = []
	var idle_phase: float = view.target_pulse_phase()
	view.set_regions(view.rows, view.neighbours_by_region, PackedStringArray(),
		PackedStringArray(), "", "")
	view.drive_target_pulse(0.4)
	if not is_equal_approx(view.target_pulse_phase(), idle_phase):
		motion.append("the pulse advanced with nothing to act on")
	if not is_equal_approx(view.selection_rim_gain(), 1.0):
		motion.append("the selection breathes with nothing selected (gain %.3f)"
			% view.selection_rim_gain())
	var acting := ""
	var reachable := PackedStringArray()
	for row in view.rows:
		if int(row["owner"]) < 0:
			continue
		var neighbours: PackedStringArray = view.neighbours_by_region.get(
			String(row["id"]), PackedStringArray())
		if neighbours.is_empty():
			continue
		acting = String(row["id"])
		reachable = neighbours
		break
	view.set_regions(view.rows, view.neighbours_by_region, PackedStringArray(),
		reachable, acting, "")
	var before_phase: float = view.target_pulse_phase()
	var before_gain: float = view.selection_rim_gain()
	view.drive_target_pulse(MapViewScript.TARGET_PULSE_SECONDS * 0.35)
	if is_equal_approx(view.target_pulse_phase(), before_phase):
		motion.append("the legal-target pulse did not advance with %d target(s)"
			% reachable.size())
	if is_equal_approx(view.selection_rim_gain(), before_gain):
		motion.append("the selection curtain's opacity did not move (%.3f both times)"
			% before_gain)
	if view.selection_rim_gain() < MapViewScript.SELECTION_RIM_FLOOR - 0.001 \
			or view.selection_rim_gain() > 1.001:
		motion.append("the selection breath left its stated range (%.3f)"
			% view.selection_rim_gain())
	_check(motion.is_empty() and not acting.is_empty(),
		"the legal-target reticles and the selection curtain breathe, and a board with nothing to act on does not pay for it",
		"acted from '%s' with %d target(s)%s" % [acting, reachable.size(),
			"" if motion.is_empty() else "; " + "; ".join(motion)])

	view.size = original
	view._on_resized()
	view.reset_camera()


# --- picking ------------------------------------------------------------------

## HOW FAR FROM A REGION'S MARKER A SAMPLE HAS TO LAND, in panel pixels, before
## it counts as evidence that picking is by the AREA rather than by the node.
##
## NOT A ROUND NUMBER. `wotr_map_view` picked by `MARKER_RADIUS + PICK_SLOP` = 16
## px, so a sample at 60 px is nearly four times outside everything the old
## implementation could ever have answered. A test at 20 px would pass on the
## defect this exists to pin.
const PICK_SAMPLE_MIN_SCREEN_GAP := 60.0
## How many of the 52 drawn regions must offer such a sample at the opening
## framing. It is not 52 because a small province seen from the whole-map framing
## can be under 60 px across on screen and honestly has no such point; measured on
## the shipped bundle, 45 do.
const PICK_SAMPLE_MIN_REGIONS := 45
## THE CEILING ON ONE POLYGON PICK, in milliseconds. Picking runs on pointer
## motion and on clicks, never from `_process`, so it is not in the frame budget
## `wotr_frame_budget_runner` pins - but a pointer sweep can fire well over a
## hundred motion events a second, and anything near a millisecond each would be
## felt as lag on the one interaction this screen is mostly made of.
const PICK_QUERY_BUDGET_MS := 0.20
const PICK_QUERY_SAMPLES := 400


## PICKING IS BY THE REGION'S OWN POLYGON, NOT BY ITS NODE.
##
## The owner's report, verbatim: "when I hover over an area it should light up
## and allow me to select the ENTIRE area instead of just the node point that has
## a lower selection." What the view did was measure the distance to the region's
## MARKER and answer only within sixteen pixels of it, so the overwhelming
## majority of every province was dead to the pointer.
##
## Every check here is about the AREA. The first two establish that the index is
## built from retail's own `LMR_Fill` triangles and that every drawn region can
## be hit somewhere inside itself; the third is the one that would have caught
## the defect - it samples a point deliberately far from the node and requires
## the province to answer anyway.
func _check_picking_is_by_the_regions_own_polygon(view, bundle) -> void:
	var geometry = view.region_geometry
	if geometry == null or not geometry.loaded:
		var why := "no region territory geometry is bound to the view"
		_fail("the pick index is built from retail's own LMR_Fill triangles", why)
		_fail("every drawn region is picked at a point inside its own polygon", why)
		_fail("a point outside every polygon picks no region", why)
		_fail("a province answers the pointer far away from its own node marker", why)
		_fail("open water picks no region", why)
		_fail("one polygon pick stays inside its stated budget", why)
		return

	# 1. THE INDEX IS RETAIL'S GEOMETRY. Counted against the manifest's own
	#    per-mesh triangle counts for the fill meshes that are bound to a REGION,
	#    so an index that quietly swallowed the impassable land masses (which
	#    belong to no region and must pick nothing) fails here.
	var stats: Dictionary = geometry.pick_index_stats()
	var expected_triangles := 0
	for record in geometry.records:
		var row := record as Dictionary
		if String(row.get("layer", "")) != "fill":
			continue
		var region_value: Variant = row.get("regionId", null)
		if region_value == null or String(region_value).is_empty():
			continue
		expected_triangles += int(row.get("triangleCount", 0))
	_check(int(stats["triangles"]) == expected_triangles
			and int(stats["regions"]) == geometry.shaded_region_count(),
		"the pick index is built from retail's own LMR_Fill triangles",
		"%d triangle(s) over %d region(s) in %d cell(s), built in %.1f ms" % [
			int(stats["triangles"]), int(stats["regions"]), int(stats["cells"]),
			float(stats["build_ms"])])

	# 2. EVERY DRAWN REGION IS HITTABLE INSIDE ITSELF. The sample is the centroid
	#    of that region's LARGEST fill triangle, which is guaranteed to be inside
	#    the polygon - unlike the area-weighted centroid of a concave province,
	#    which can land in a neighbour.
	var interior: Dictionary = {}
	for region_id in geometry.by_region.keys():
		var point: Variant = _largest_triangle_centroid(geometry, String(region_id))
		if point != null:
			interior[String(region_id)] = point
	var wrong: Array[String] = []
	for key in interior.keys():
		var region_id := String(key)
		var at := interior[key] as Vector3
		var picked: String = geometry.region_at_position(at.x, at.z)
		if picked != region_id:
			wrong.append("%s -> %s" % [region_id, "nothing" if picked.is_empty() else picked])
	wrong.sort()
	_check(wrong.is_empty() and interior.size() == geometry.shaded_region_count(),
		"every drawn region is picked at a point inside its own polygon",
		"%d of %d region(s); wrong: %s" % [
			interior.size() - wrong.size(), geometry.shaded_region_count(), str(wrong)])

	# 3. AND NOTHING OUTSIDE IS. Far off the slab in both axes, so this cannot be
	#    passed by an index that answers with the nearest region rather than the
	#    containing one - which is exactly what the old marker-distance pick did.
	var extent: Dictionary = bundle.terrain_extent
	var far := BundleScript.world_to_godot(
		float(extent["x_min"]) - 20000.0, float(extent["y_min"]) - 20000.0, 0.0)
	_check(geometry.region_at_position(far.x, far.z).is_empty(),
		"a point outside every polygon picks no region",
		"20000 units off retail's own slab")

	# 4. THE OWNER'S DEFECT, PINNED. A point inside the province and at least
	#    `PICK_SAMPLE_MIN_SCREEN_GAP` panel pixels away from its node marker must
	#    still pick it. Every one of these samples is outside anything the old
	#    16-pixel marker test could have answered.
	# AT RETAIL'S OWN 16:9 FRAME, which is the window this screen is judged in and
	# the one the capture set is taken at. It is not cosmetic here: how far a
	# province reaches in PIXELS is a function of the panel, and at the runner's
	# small default panel most of Middle-earth is under sixty pixels across.
	view.size = Vector2(2560, 1440)
	view._on_resized()
	view.reset_camera()
	var world_positions: Dictionary = view._world_positions
	var panel := Vector2(view.viewport.size)
	var tested := 0
	var missed: Array[String] = []
	for key in interior.keys():
		var region_id := String(key)
		if not world_positions.has(region_id):
			continue
		var node_pixel := _project_to_pixel(view, world_positions[region_id] as Vector3, panel)
		var sample: Variant = _offset_interior_pixel(geometry, view, region_id, node_pixel, panel)
		if sample == null:
			continue
		tested += 1
		var picked: String = view.region_at(sample as Vector2)
		if picked != region_id:
			missed.append("%s -> %s" % [region_id, "nothing" if picked.is_empty() else picked])
	missed.sort()
	_check(missed.is_empty() and tested >= PICK_SAMPLE_MIN_REGIONS,
		"a province answers the pointer far away from its own node marker",
		"%d region(s) sampled at least %.0f px off their node; wrong: %s" % [
			tested, PICK_SAMPLE_MIN_SCREEN_GAP, str(missed)])

	# 5. OPEN WATER IS NOT A PROVINCE. Retail's slab runs a column of painted
	#    seabed down its western edge (see `wotr_map_view.zoom_ceiling`), and no
	#    region's fill covers it - so a pixel over it must pick nothing rather
	#    than the nearest coast.
	var seabed := BundleScript.world_to_godot(
		float(extent["x_min"]) + 40.0,
		(float(extent["y_min"]) + float(extent["y_max"])) * 0.5, 0.0)
	var seabed_pixel := _project_to_pixel(view, seabed, panel)
	_check(view.region_at(seabed_pixel).is_empty(),
		"open water picks no region",
		"the slab's western seabed column at %s" % str(seabed_pixel))

	# 6. AND IT IS CHEAP. Timed over a sweep of the panel rather than one lucky
	#    pixel, so a pick that misses everything (the expensive case - it walks
	#    the grid to the far side instead of stopping at a hit) is in the average.
	var started := Time.get_ticks_usec()
	for step in PICK_QUERY_SAMPLES:
		var fraction := float(step) / float(PICK_QUERY_SAMPLES)
		view.region_at(Vector2(panel.x * fraction, panel.y * (1.0 - fraction)))
	var per_query := float(Time.get_ticks_usec() - started) / 1000.0 / float(PICK_QUERY_SAMPLES)
	_check(per_query <= PICK_QUERY_BUDGET_MS,
		"one polygon pick stays inside its stated budget",
		"%.3f ms per pick over %d picks, budget %.2f ms" % [
			per_query, PICK_QUERY_SAMPLES, PICK_QUERY_BUDGET_MS])


## WHERE A WORLD POINT LANDS ON THE PANEL, under the view's own parallel
## projection. This is the exact inverse of `wotr_map_view._pointer_ray`, written
## out here rather than taken from `Camera3D.unproject_position`, because this
## runner drives the view OUTSIDE a scene tree and Godot's projection helpers
## refuse there - silently enough that a check built on them passed by comparing
## zeroes to zeroes for one round.
func _project_to_pixel(view, world: Vector3, panel: Vector2) -> Vector2:
	var transform: Transform3D = view.camera.transform
	var relative := world - transform.origin
	var half_vertical: float = maxf(view._camera_distance * view._zoom, 0.0001) * 0.5
	var half_horizontal := half_vertical * (panel.x / maxf(panel.y, 1.0))
	return Vector2(
		(relative.dot(transform.basis.x) / half_horizontal * 0.5 + 0.5) * panel.x,
		(0.5 - relative.dot(transform.basis.y) / half_vertical * 0.5) * panel.y)


## The centroid of a region's largest fill triangle, or null. Guaranteed to be
## INSIDE retail's own polygon, which the area-weighted centroid of a concave
## province is not.
func _largest_triangle_centroid(geometry, region_id: String) -> Variant:
	var mesh: ArrayMesh = geometry.region_mesh(region_id, "fill")
	if mesh == null or mesh.get_surface_count() == 0:
		return null
	var arrays: Array = mesh.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array
	var best := Vector3.ZERO
	var best_area := -1.0
	for i in range(0, indices.size() - 2, 3):
		var a := vertices[indices[i]]
		var b := vertices[indices[i + 1]]
		var c := vertices[indices[i + 2]]
		var area := absf((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z))
		if area > best_area:
			best_area = area
			best = (a + b + c) / 3.0
	return null if best_area <= 0.0 else best


## A panel pixel inside this region's own polygon, on the panel, and at least
## `PICK_SAMPLE_MIN_SCREEN_GAP` from the region's node marker. Null when the
## province offers none - a small one seen from the whole-map framing honestly
## does not.
##
## AMONG THE CANDIDATES IT TAKES THE BIGGEST TRIANGLE, not the most distant one,
## and the difference matters. The most distant point in a draped province is on
## its fringe, where a neighbour's ridge can genuinely stand in front of it: the
## fill meshes lie on retail's terrain and are depth-tested against it, so at a
## pitched camera a far province's edge really is BEHIND the hill next door and
## the pick that answers "the hill next door" is answering correctly. Testing
## there would be testing the terrain, not the picking. A large interior triangle
## is unambiguously the province's own ground and is still far outside anything
## the old sixteen-pixel node test could reach.
func _offset_interior_pixel(
	geometry, view, region_id: String, node_pixel: Vector2, panel: Vector2
) -> Variant:
	var mesh: ArrayMesh = geometry.region_mesh(region_id, "fill")
	if mesh == null or mesh.get_surface_count() == 0:
		return null
	var arrays: Array = mesh.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array
	var best: Variant = null
	var best_area := 0.0
	# Every seventh triangle: 55k projections across 52 regions would dominate
	# this runner's wall clock, and the sample is dense enough that the answer
	# does not move.
	for i in range(0, indices.size() - 2, 21):
		var a := vertices[indices[i]]
		var b := vertices[indices[i + 1]]
		var c := vertices[indices[i + 2]]
		var area := absf((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z))
		if area <= best_area:
			continue
		var pixel := _project_to_pixel(view, (a + b + c) / 3.0, panel)
		if pixel.x < 2.0 or pixel.y < 2.0 or pixel.x > panel.x - 2.0 or pixel.y > panel.y - 2.0:
			continue
		if (pixel - node_pixel).length() < PICK_SAMPLE_MIN_SCREEN_GAP:
			continue
		best_area = area
		best = pixel
	return best


# --- ownership colour ---------------------------------------------------------

## DOES THE STRATEGIC LAYER ACTUALLY SHOW WHO HOLDS WHAT?
##
## This exists because a blind review of the judged capture said it does not -
## "no faction ownership colour on the map, only a red selection wash" - and the
## investigation found that claim was half true for a reason nothing in this file
## could catch. Every check below therefore asserts on the colour that REACHES
## THE FRAME (`rendered_band_color`, and the live materials
## `territory_materials` hands back, both put through the linear tonemapper's
## clip) rather than on the seat constants or on the intent of
## `_apply_territory_colors`. The gap between those two is where the defect
## lived: the intent was six distinct hues, the frame got one red, one yellow and
## four whites.
##
## The seat palette comes from the SCREEN (`ScreenScript.SEAT_COLORS`), the same
## place the running game gets it, so a change there is a change here.
func _check_ownership_reaches_the_screen(
		view, rows: Array[Dictionary], adjacency: Dictionary) -> void:
	# THE TERRITORY GEOMETRY IS BOUND HERE and nowhere else in this runner: every
	# check above is about the map bundle, and territories come from a SEPARATE
	# bundle on purpose (retail's Middle-earth can be on screen with no region
	# shapes converted). Without it there are no fill materials to assert on, and
	# a check that silently found none would report exactly the same "everything
	# passed" as one that found them all correct.
	var geometry = _region_geometry()
	if geometry == null:
		var why := _geometry_reason
		# Eight failures, not a skip: the LIVENESS total is exact, and a lane that
		# cannot see the colours must say so eight times rather than shrink.
		_fail("every seat holding ground renders a band that keeps its own hue and chroma", why)
		_fail("the six seat colours render as six separable bands", why)
		_fail("distinct owner tints are on the drawn map", why)
		_fail("selection is retail's own LMR_Edge ring", why)
		_fail("retail's selection and home-region meshes are bound for every drawn region", why)
		_fail("every attack target carries its own ring in the attacker's hue", why)
		_fail("the border shoulders fall inside their own region", why)
		_fail("LMR_Edge is a standing curtain and LMR_Highlight a flat inner band", why)
		return
	view.set_region_geometry(geometry, "")

	view.owner_colors = ScreenScript.SEAT_COLORS
	view.neutral_color = ScreenScript.NEUTRAL_COLOR
	view.set_regions(rows, adjacency, PackedStringArray(), PackedStringArray(), "", "")

	var owning_seats: Array[int] = []
	var region_of_seat: Dictionary = {}
	var neutral_region := ""
	for row in rows:
		var owner := int(row["owner"])
		var region_id := String(row["id"])
		if owner < 0:
			if neutral_region.is_empty() and not view.territory_materials(region_id).is_empty():
				neutral_region = region_id
			continue
		if not owning_seats.has(owner):
			owning_seats.append(owner)
			region_of_seat[owner] = []
		(region_of_seat[owner] as Array).append(region_id)
	owning_seats.sort()

	# 1. EVERY SEAT THAT HOLDS GROUND STILL LOOKS LIKE ITSELF ON SCREEN. A band
	#    that clipped to white would pass any check written against the seat
	#    colour and fail this one.
	var wrong: Array[String] = []
	for seat in owning_seats:
		var seat_color: Color = ScreenScript.SEAT_COLORS[seat]
		var shown: Color = view.rendered_band_color(seat)
		var drift := _hue_gap(seat_color.h, shown.h)
		if drift > BAND_HUE_TOLERANCE or shown.s < BAND_SATURATION_FLOOR:
			wrong.append("seat %d %s -> rendered hue %.1f (drift %.1f) saturation %.2f" % [
				seat, seat_color.to_html(false), shown.h * 360.0, drift, shown.s])
	_check(wrong.is_empty() and not owning_seats.is_empty(),
		"every seat holding ground renders a band that keeps its own hue and chroma",
		"%d seats own regions; %s" % [owning_seats.size(),
			"offenders " + str(wrong) if not wrong.is_empty() else "all within tolerance"])

	# 2. THE WHOLE PALETTE, not just the two seats this scenario happens to seat.
	#    A six-player game is what the palette is for, and under the round-3 path
	#    seats 0 and 5 rendered as the identical pale cyan.
	var collisions: Array[String] = []
	for a in range(ScreenScript.SEAT_COLORS.size()):
		for b in range(a + 1, ScreenScript.SEAT_COLORS.size()):
			var gap := _hue_gap(view.rendered_band_color(a).h, view.rendered_band_color(b).h)
			if gap < BAND_HUE_SEPARATION:
				collisions.append("seats %d and %d are %.1f degrees apart" % [a, b, gap])
	_check(collisions.is_empty(),
		"the six seat colours render as six separable bands",
		"collisions " + str(collisions) if not collisions.is_empty()
			else "closest pair is at least %.0f degrees apart" % BAND_HUE_SEPARATION)

	# 3. AND IT IS ON THE MAP, region by region. `rendered_band_color` is what the
	#    seat WOULD look like; this is what every one of its holdings is actually
	#    painted with. The count of distinct tints must equal the count of seats -
	#    the exact claim the review denied.
	var tints: Dictionary = {}
	var untinted: Array[String] = []
	for seat in owning_seats:
		var expected_fill: Color = view._fill_color(ScreenScript.SEAT_COLORS[seat])
		for region_id in region_of_seat[seat] as Array:
			var materials: Dictionary = view.territory_materials(String(region_id))
			var fill := materials.get("fill", null) as StandardMaterial3D
			if fill == null:
				untinted.append("%s has no fill material" % region_id)
				continue
			var painted: Color = fill.albedo_color
			if _hue_gap(painted.h, expected_fill.h) > 0.5 \
					or absf(painted.a - MapViewScript.TERRITORY_ALPHA) > 0.001 \
					or painted.s < MapViewScript.FILL_SATURATION_FLOOR - 0.001:
				untinted.append("%s painted %s a=%.2f, expected seat %d" % [
					region_id, painted.to_html(false), painted.a, seat])
			tints[painted.to_html(false)] = true
	_check(untinted.is_empty() and tints.size() == owning_seats.size(),
		"with %d seats owning regions, %d distinct owner tints are on the drawn map" % [
			owning_seats.size(), owning_seats.size()],
		"distinct tints %d; %s" % [tints.size(),
			"faults " + str(untinted) if not untinted.is_empty() else "every holding carries its owner"])

	# 4. SELECTION IS A DIFFERENT PIECE OF ART FROM OWNERSHIP, which is how retail
	#    separates them: `RegionSelectionEffect` draws `LMR_Edge` and
	#    `BordersEffect` draws `LMR_Border`. Round 4 had no edge layer to draw and
	#    stood in for it by driving the selected region's BAND to white, so this
	#    check asserted the band went achromatic. That stand-in is gone, and the
	#    replacement claim is STRICTLY STRONGER: the ring is retail's own separate
	#    mesh, it is lit on the selected region and on no other, AND the ownership
	#    band under it must now keep the owner's hue at full chroma - which the
	#    round-4 path could not have passed, because it destroyed exactly that.
	var faults: Array[String] = []
	# Only holdings that carry retail's border strip AND its selection edge can be
	# compared: a region with a fill mesh and neither has no mark to make.
	var bordered: Array[String] = []
	if not owning_seats.is_empty():
		for region_id in region_of_seat[owning_seats[0]] as Array:
			var slot: Dictionary = view.territory_materials(String(region_id))
			if slot.get("border", null) != null and slot.get("glow", null) != null \
					and slot.get("selection_edge", null) != null:
				bordered.append(String(region_id))
	if bordered.size() < 2:
		faults.append("no seat holds two regions carrying both retail's border strip and its selection edge")
	else:
		var seat: int = owning_seats[0]
		var chosen := bordered[0]
		var other := bordered[1]
		view.set_regions(rows, adjacency, PackedStringArray(), PackedStringArray(), chosen, "")
		var chosen_ring: Color = (view.territory_materials(chosen)["selection_edge"]
			as StandardMaterial3D).albedo_color
		var other_ring: Color = (view.territory_materials(other)["selection_edge"]
			as StandardMaterial3D).albedo_color
		var chosen_core: Color = MapViewScript.clipped(
			(view.territory_materials(chosen)["border"] as StandardMaterial3D).albedo_color)
		var chosen_halo: Color = (view.territory_materials(chosen)["glow"] as StandardMaterial3D).albedo_color
		var other_core: Color = MapViewScript.clipped(
			(view.territory_materials(other)["border"] as StandardMaterial3D).albedo_color)
		var seat_hue: float = ScreenScript.SEAT_COLORS[seat].h
		# The ring is lit, achromatic, and its brightness sits in a BAND with a
		# ceiling as well as a floor.
		#
		# ROUND 6 ASSERTED THE OPPOSITE CEILING - "driven PAST the glow threshold
		# so it carries the same soft shoulder the band does" - and the capture
		# that assertion was passing over is the evidence it was wrong. In
		# `.captures/wotr-stream-a/r6-final/03-staged.png` the selection curtain is
		# a white sheet spread across half of Arnor with no readable outline
		# anywhere in it, and a blind review named it "a generic soft outer bloom".
		#
		# The band's shoulder and the curtain's are not the same problem. The band
		# is `lmr_border.w3d`, a ~6.5-unit ribbon about TWO PIXELS wide at the
		# strategic framing, and without bloom it cannot be seen across a room -
		# that is what `BORDER_HDR_GAIN` is for and it is untouched. `LMR_Edge` is a
		# WALL about 57 units tall that already projects to some fourteen pixels,
		# and blooming it adds twenty more of isotropic halo on each side of
		# something that was never thin. So the curtain's weighting is retail's own
		# geometry - the wall reads wider where it faces the camera squarely and
		# narrower edge-on, which no blur can imitate - and the glow buffer is left
		# to the ribbon that needs it.
		#
		# This is a stronger claim than the one it replaces, not a relaxed one: the
		# old form pinned one side of the number, this pins both.
		if chosen_ring.a <= 0.0:
			faults.append("the selected region's LMR_Edge ring is not drawn (alpha %.2f)" % chosen_ring.a)
		if MapViewScript.clipped(chosen_ring).s > SELECTION_CORE_SATURATION_CEILING:
			faults.append("the selection ring is chromatic (saturation %.2f)"
				% MapViewScript.clipped(chosen_ring).s)
		var ring_peak: float = maxf(chosen_ring.r, maxf(chosen_ring.g, chosen_ring.b))
		# The floor: over 1.0, so the achromatic core CLIPS to full white against
		# the terrain rather than reading as a grey wall.
		if ring_peak <= 1.0:
			faults.append("the selection ring's core does not clip to white (peak %.2f)" % ring_peak)
		# The ceiling: at or under the environment's own threshold, so it never
		# reaches the glow buffer. Read off the view's constant rather than
		# restated, so moving one moves both.
		if ring_peak > MapViewScript.GLOW_HDR_THRESHOLD:
			faults.append("the selection curtain blooms (peak %.2f over the %.2f threshold)"
				% [ring_peak, MapViewScript.GLOW_HDR_THRESHOLD])
		# And it is ONE region's mark: the seat's other holding gets no ring at all.
		if other_ring.a > 0.0:
			faults.append("an unselected holding is also ringed (alpha %.2f)" % other_ring.a)
		# THE OWNERSHIP BAND SURVIVES SELECTION. This is the half round 4 could not
		# assert, because round 4 broke it on purpose.
		if chosen_core.s < BAND_SATURATION_FLOOR \
				or _hue_gap(chosen_core.h, seat_hue) > BAND_HUE_TOLERANCE:
			faults.append("the selected region's band stopped naming its owner (hue %.1f saturation %.2f)"
				% [chosen_core.h * 360.0, chosen_core.s])
		if _hue_gap(chosen_halo.h, seat_hue) > BAND_HUE_TOLERANCE or chosen_halo.a <= 0.0:
			faults.append("the selected region's halo stopped naming its owner (hue %.1f alpha %.2f)"
				% [chosen_halo.h * 360.0, chosen_halo.a])
		if other_core.s < BAND_SATURATION_FLOOR \
				or _hue_gap(other_core.h, seat_hue) > BAND_HUE_TOLERANCE:
			faults.append("the same seat's unselected holding lost its ownership mark (hue %.1f saturation %.2f)"
				% [other_core.h * 360.0, other_core.s])
		# The two marks are separated by CHROMA as well as by geometry, so the frame
		# still tells them apart where the two shapes overlap.
		if absf(MapViewScript.clipped(chosen_ring).s - chosen_core.s) \
				< BAND_SATURATION_FLOOR - SELECTION_CORE_SATURATION_CEILING:
			faults.append("ring and band are separated by only %.2f of chroma"
				% absf(MapViewScript.clipped(chosen_ring).s - chosen_core.s))
	_check(faults.is_empty(),
		"selection is retail's own LMR_Edge ring and ownership survives underneath it",
		"faults " + str(faults) if not faults.is_empty()
			else "achromatic ring on retail's selection mesh over an intact owner band")

	# 5. THE HONESTY HALF. An unclaimed region must be given NO owner tint - a
	#    neutral wearing a seat colour would make the count above meaningless -
	#    and retail's own selection geometry, which IS shipped and IS NOT in the
	#    bundle, must be named rather than substituted.
	var honest: Array[String] = []
	if neutral_region.is_empty():
		honest.append("the scenario leaves no neutral region to check")
	else:
		var neutral_materials: Dictionary = view.territory_materials(neutral_region)
		var neutral_fill := (neutral_materials["fill"] as StandardMaterial3D).albedo_color
		if absf(neutral_fill.a - MapViewScript.NEUTRAL_TERRITORY_ALPHA) > 0.001:
			honest.append("%s is filled at alpha %.2f, not the neutral alpha"
				% [neutral_region, neutral_fill.a])
		for seat in owning_seats:
			if _hue_gap(neutral_fill.h, ScreenScript.SEAT_COLORS[seat].h) < BAND_HUE_SEPARATION:
				honest.append("%s wears seat %d's hue" % [neutral_region, seat])
	# RETAIL'S SELECTION AND HOME-REGION GEOMETRY IS BOUND, NOT SUBSTITUTED. Round
	# 4 asserted that a gap SENTENCE named the unconverted meshes; the converter
	# writes both layers now, so the claim is the stronger one - every region that
	# is drawn at all carries both meshes, and the converter reports no hole in
	# either layer.
	if not geometry.has_layer("edge") or not geometry.has_layer("highlight"):
		honest.append("the bundle carries no edge and/or highlight layer (%s)"
			% str(Array(geometry.layers_present)))
	if not geometry.selection_geometry_gap().is_empty():
		honest.append("selection geometry gap: %s" % geometry.selection_geometry_gap())
	if not geometry.home_highlight_gap().is_empty():
		honest.append("home highlight gap: %s" % geometry.home_highlight_gap())
	if view.selection_edge_count() != view.shaded_regions.size():
		honest.append("%d of %d drawn regions carry retail's LMR_Edge" % [
			view.selection_edge_count(), view.shaded_regions.size()])
	if view.home_highlight_count() != view.shaded_regions.size():
		honest.append("%d of %d drawn regions carry retail's LMR_Highlight" % [
			view.home_highlight_count(), view.shaded_regions.size()])
	# AND THE ONE GAP THIS VIEW STILL OWNS is stated rather than papered over: the
	# highlight mesh is bound, nothing hands in a seat's capital, and no region is
	# guessed into being one.
	var home_gap: String = MapViewScript.HOME_REGION_BINDING_GAP
	if not (home_gap.contains("lmr_highlight.w3d") and home_gap.contains("capital")
			and home_gap.contains("set_home_regions")):
		honest.append("the home-region binding gap does not say what is missing or how to supply it")
	_check(honest.is_empty(),
		"unclaimed ground carries no owner tint, and retail's own selection and home-region meshes are bound for every drawn region",
		"faults " + str(honest) if not honest.is_empty()
			else "neutral stays neutral; %d regions carry LMR_Edge and LMR_Highlight" % view.selection_edge_count())

	# 6. THE ATTACK TARGETS ARE MARKED ONE BY ONE, IN THE ATTACKER'S OWN HUE.
	#    A blind review of round 4 called the target set "a flat, uniform wash of
	#    near-pure red ... no per-region separation ... the loudest thing in the
	#    frame and the least crafted", and it was reading a real defect: a target
	#    got nothing but a bump in its OWNER'S fill alpha, so inside a red seat's
	#    holdings the valid set was invisible and the whole west of the map read as
	#    one red mass. Every target now carries retail's own `LMR_Edge` ring.
	var target_faults: Array[String] = []
	if owning_seats.size() < 2:
		target_faults.append("the scenario seats fewer than two owners, so there is nothing to attack")
	else:
		var attacker: int = owning_seats[0]
		var defender: int = owning_seats[1]
		var from_region := String((region_of_seat[attacker] as Array)[0])
		var marked: Array[String] = []
		for region_id in region_of_seat[defender] as Array:
			if view.territory_materials(String(region_id)).get("selection_edge", null) != null:
				marked.append(String(region_id))
		if marked.size() < 3:
			target_faults.append("the defender holds fewer than three ringable regions")
		else:
			var chosen_targets := PackedStringArray([marked[0], marked[1]])
			var untouched := marked[marked.size() - 1]
			view.set_regions(rows, adjacency, PackedStringArray([from_region]),
				chosen_targets, from_region, "")
			var ring: Color = view.target_ring_color(view._framing_fraction())
			var attacker_hue: float = ScreenScript.SEAT_COLORS[attacker].h
			var defender_hue: float = ScreenScript.SEAT_COLORS[defender].h
			# EVERY target carries the ring, so the valid set still reads as a set -
			# the one thing the review said the round-4 wash did better than retail.
			for region_id in Array(chosen_targets):
				var lit: Color = (view.territory_materials(String(region_id))["selection_edge"]
					as StandardMaterial3D).albedo_color
				if lit.a <= 0.0 or not lit.is_equal_approx(ring):
					target_faults.append("%s is not ringed with the target colour (%s vs %s)" % [
						region_id, lit.to_html(false), ring.to_html(false)])
			# And nothing else does, so the ring means "target", not "red region".
			var spare: Color = (view.territory_materials(untouched)["selection_edge"]
				as StandardMaterial3D).albedo_color
			if spare.a > 0.0:
				target_faults.append("%s is ringed and is not a target" % untouched)
			# The ring is the ATTACKER'S hue, not the defender's and not an invented
			# one - a seventh colour on a six-seat map is the defect, not the fix.
			var shown: Color = MapViewScript.clipped(ring)
			if _hue_gap(shown.h, attacker_hue) > BAND_HUE_TOLERANCE:
				target_faults.append("the target ring is hue %.1f, the attacking seat is %.1f" % [
					shown.h * 360.0, attacker_hue * 360.0])
			if _hue_gap(shown.h, defender_hue) < BAND_HUE_SEPARATION:
				target_faults.append("the target ring is within %.0f degrees of the defender's own band"
					% BAND_HUE_SEPARATION)
			# And it is QUIETER than the selection ring, which is the ordering the
			# review said was inverted.
			var target_peak: float = maxf(ring.r, maxf(ring.g, ring.b))
			var selection_peak: float = MapViewScript.SELECTION_CORE_COLOR.r \
				* MapViewScript.SELECTION_EDGE_GAIN
			if target_peak >= selection_peak:
				target_faults.append("the target ring (peak %.2f) is at least as loud as the selection ring (peak %.2f)"
					% [target_peak, selection_peak])
			if shown.s < 0.20:
				target_faults.append("the target ring has no chroma left (saturation %.2f)" % shown.s)
			# AND IT DOES NOT BLOOM. Same argument as the selection curtain
			# above: a fourteen-pixel wall does not need an isotropic halo, and
			# several of these stand on screen at once.
			if target_peak > MapViewScript.GLOW_HDR_THRESHOLD:
				target_faults.append("the target curtain blooms (peak %.2f over the %.2f threshold)"
					% [target_peak, MapViewScript.GLOW_HDR_THRESHOLD])
	_check(target_faults.is_empty(),
		"every attack target carries its own ring in the attacker's hue, and nothing else does",
		"faults " + str(target_faults) if not target_faults.is_empty()
			else "per-region target rings, attacker-hued, quieter than the selection ring")

	# 6b. TWO TRANSLUCENT LAYERS MUST NOT STACK INTO A THIRD COLOUR, and this is
	#     the check that pins the RULE rather than the numbers that implement it.
	#
	#     THE DEFECT IT COMES FROM WAS PHOTOGRAPHED, not reasoned about.
	#     `.captures/wotr-stream-a/r6-final/01-opening.png` and `03-staged.png` are
	#     the same camera at the same window; the only difference between them is
	#     that Angmar is staged with its targets offered. Over the map area 28.1%
	#     of the pixels changed, by a mean of +17.3 red and +26.6 blue. The red is
	#     nine adjacent Dwarven holdings ALL flaring to the hover alpha at once,
	#     because every one of them was a valid target and targets flared the fill;
	#     the blue is the attacker's LMR_Edge curtains standing on those same
	#     regions. A blind review called the result "a flat multiply-red ... plus a
	#     blue overlay producing a muddy indeterminate purple where they cross",
	#     and it was describing arithmetic: two translucent layers of different hue
	#     summing to a colour that names neither seat.
	#
	#     THE RULE: a region carries the ATTACKER'S mark or its OWNER'S paint,
	#     never both at strength. Three ways of saying it, all asserted here:
	#
	#       * a targeted region's fill is pulled DOWN, not flared - strictly under
	#         the plain ownership alpha, let alone the hover one;
	#       * the fill and the curtain TOGETHER cover the ground with no more paint
	#         than the single most-painted state on the map is allowed
	#         (TERRITORY_ALPHA_SELECTED), so the stack can never be heavier than
	#         any one layer is ever permitted to be on its own;
	#       * and HOVER STILL FLARES, because MouseoverEffectFlareup is retail's
	#         own effect on LMR_Fill. That is the half that stops this rule from
	#         being satisfied by turning everything down until nothing shows.
	var overlap_faults: Array[String] = []
	if owning_seats.size() < 2:
		overlap_faults.append("the scenario seats fewer than two owners, so nothing can be targeted")
	else:
		var attacker_seat: int = owning_seats[0]
		var defender_seat: int = owning_seats[1]
		var from_id := String((region_of_seat[attacker_seat] as Array)[0])
		var victim := ""
		for region_id in region_of_seat[defender_seat] as Array:
			var slot: Dictionary = view.territory_materials(String(region_id))
			if slot.get("selection_edge", null) != null and slot.get("fill", null) != null:
				victim = String(region_id)
				break
		if victim.is_empty():
			overlap_faults.append("the defender holds no region carrying both a fill and an edge")
		else:
			view.set_regions(rows, adjacency, PackedStringArray([from_id]),
				PackedStringArray([victim]), from_id, "")
			var under: Color = (view.territory_materials(victim)["fill"]
				as StandardMaterial3D).albedo_color
			var curtain: Color = (view.territory_materials(victim)["selection_edge"]
				as StandardMaterial3D).albedo_color
			if under.a >= MapViewScript.TERRITORY_ALPHA:
				overlap_faults.append("the targeted region's fill is %.2f, not under the plain ownership alpha %.2f"
					% [under.a, MapViewScript.TERRITORY_ALPHA])
			# The composite coverage of the two layers, which is what the eye
			# actually receives where the wall crosses the ground it stands on.
			var stacked: float = under.a + curtain.a * (1.0 - under.a)
			if stacked > MapViewScript.TERRITORY_ALPHA_SELECTED:
				overlap_faults.append("fill %.2f under curtain %.2f stack to %.2f, over the heaviest single state %.2f"
					% [under.a, curtain.a, stacked, MapViewScript.TERRITORY_ALPHA_SELECTED])
			view.set_regions(rows, adjacency, PackedStringArray(),
				PackedStringArray(), "", "")
			view.hover_region = victim
			view._apply_territory_colors()
			var hovered: Color = (view.territory_materials(victim)["fill"]
				as StandardMaterial3D).albedo_color
			view.hover_region = ""
			view._apply_territory_colors()
			if not is_equal_approx(hovered.a, MapViewScript.TERRITORY_ALPHA_HOVER):
				overlap_faults.append("hover no longer flares the fill (%.2f, expected %.2f)"
					% [hovered.a, MapViewScript.TERRITORY_ALPHA_HOVER])
	_check(overlap_faults.is_empty(),
		"an attack curtain and the fill under it never stack into a third colour, and hover still flares",
		"faults " + str(overlap_faults) if not overlap_faults.is_empty()
			else "the owner's paint gives way under the attacker's mark; mouseover flareup intact")

	# 6c. THE BUILD RING'S CAPTIONS DO NOT COLLIDE, and this is the first test that
	#     has ever asserted it. A blind review of round 6 reported "Hall of the
	#     King's Men 5 / Dark Iron Forge 500 / Angmar Fortress 1500 / Mill ... at
	#     ~10px unstyled white, overlapping each other and colliding with the
	#     embossed ARNOR cartouche". The placement was rewritten in response and
	#     then shipped UNVERIFIED, because the arithmetic lived inside a `_draw`
	#     callback and Godot refuses `draw_*` outside NOTIFICATION_DRAW - so the
	#     only harness that could reach it had to stand up a real redraw, and the
	#     one that was tried hung. `radial_caption_plate()` is that decision split
	#     out as a pure function, and this is the check it existed to make possible.
	#
	#     THE FIXTURE IS THE ONE THAT FAILED: five slots on a ring at even angles
	#     with the real Angmar strings, which are wider than the arc between two
	#     adjacent slots - so a fixed offset under each icon MUST collide and only
	#     a placement that consults the plates already down can avoid it.
	var caption_faults: Array[String] = []
	# The engine's own fallback face. The check is about PLACEMENT, and placement
	# only needs a font that can measure a string.
	var caption_font: Font = ThemeDB.fallback_font
	var ring_centre := Vector2(640.0, 360.0)
	var ring_radius := MapViewScript.RADIAL_RADIUS
	var captions := [
		"Hall of the King's Men  500", "Dark Iron Forge  500",
		"Angmar Fortress  1500", "Mill  0", "Angmar Fortress  1500",
	]
	var placed_plates: Array[Rect2] = []
	var dropped := 0
	for index in range(captions.size()):
		var angle: float = TAU * float(index) / float(captions.size()) - PI * 0.5
		var at := ring_centre + Vector2(cos(angle), sin(angle)) * ring_radius
		var icon_box := Rect2(at - Vector2(MapViewScript.RADIAL_ICON, MapViewScript.RADIAL_ICON) * 0.5,
			Vector2(MapViewScript.RADIAL_ICON, MapViewScript.RADIAL_ICON))
		var plate: Rect2 = view.radial_caption_plate(
			caption_font, icon_box, String(captions[index]), ring_centre, placed_plates)
		if plate.size.x <= 0.0 or plate.size.y <= 0.0:
			dropped += 1
			continue
		# INSIDE THE PANEL. A plate that runs off the edge is clipped away and the
		# slot is captionless for no visible reason.
		if plate.position.x < 0.0 or plate.position.y < 0.0 				or plate.end.x > view.size.x or plate.end.y > view.size.y:
			caption_faults.append("caption %d is outside the panel (%s in %s)"
				% [index, str(plate), str(view.size)])
		# AND CLEAR OF EVERY PLATE ALREADY DOWN. This is the reported defect.
		for taken in placed_plates:
			if taken.intersects(plate):
				caption_faults.append("caption %d overlaps an earlier plate (%s vs %s)"
					% [index, str(plate), str(taken)])
		# The type is at or above the HUD's shared floor for lettering set over the
		# map, read from the chrome rather than restated - the "~10px" the review
		# measured was a literal written at this one call site and nowhere else.
		var floor_px: int = maxi(HudChromeScript.TYPE_MAP_FLOOR, 12)
		if plate.size.y < float(floor_px):
			caption_faults.append("caption %d is set at %.0fpx, under the map floor %d"
				% [index, plate.size.y, floor_px])
		placed_plates.append(plate)
	# A caption with nowhere to go is DROPPED, not stacked - but the ring must not
	# drop so many that it stops being a ring with names on it.
	if placed_plates.size() < 4:
		caption_faults.append("only %d of %d captions were placed (%d dropped)"
			% [placed_plates.size(), captions.size(), dropped])
	# AND THE PLACEMENT IS NOT A FIXED OFFSET. If every plate sat under its own
	# icon the five would be at five different x's and the same relative y; the
	# whole point is that later captions get moved. At least one must have been.
	var moved := false
	for index in range(placed_plates.size()):
		var angle2: float = TAU * float(index) / float(captions.size()) - PI * 0.5
		var at2 := ring_centre + Vector2(cos(angle2), sin(angle2)) * ring_radius
		var below := at2.y + MapViewScript.RADIAL_ICON * 0.5 + MapViewScript.RADIAL_CAPTION_GAP
		if absf((placed_plates[index] as Rect2).position.y - below) > 1.0:
			moved = true
			break
	if not moved:
		caption_faults.append("every caption sat at its default offset, so the collision search never ran")
	_check(caption_faults.is_empty(),
		"the build ring's captions are plated, sized to the map floor and never laid on each other",
		"faults " + str(caption_faults) if not caption_faults.is_empty()
			else "%d captions placed, %d dropped, none overlapping" % [placed_plates.size(), dropped])

	# 7. THE SHOULDER RINGS FALL INSIDE THE REGION THEY BELONG TO. An additive ring
	#    pushed OUTWARDS lands on the neighbour's ground, and that one sign is where
	#    two separately-reported round-4 map defects both came from: adjacent
	#    holdings of one seat merged into a single wash with "no per-region
	#    separation", and a blue ring laid over a red neighbour's red-brown ground
	#    rendered MAGENTA, which the review read as an unexplained debug stroke.
	#    Asserted on the GEOMETRY the view actually asks for, not on the constant,
	#    so a sign flip inside `border_shoulder_mesh` would fail it too.
	var shoulder_faults: Array[String] = []
	var sample := ""
	for region_id in view.shaded_regions:
		if geometry.region_mesh(String(region_id), "border") != null \
				and geometry.derived_centroids.has(String(region_id)):
			sample = String(region_id)
			break
	if sample.is_empty():
		shoulder_faults.append("no drawn region carries both a border mesh and a derived centroid")
	else:
		var band_aabb: AABB = (geometry.region_mesh(sample, "border") as ArrayMesh).get_aabb()
		for outset in MapViewScript.BORDER_SHOULDER_OUTSETS:
			var distance: float = float(outset)
			if distance >= 0.0:
				shoulder_faults.append("shoulder displacement %.1f is not inward" % distance)
				continue
			var ring_mesh: ArrayMesh = geometry.border_shoulder_mesh(sample, distance)
			if ring_mesh == null:
				shoulder_faults.append("no shoulder mesh at %.1f" % distance)
				continue
			var ring_aabb: AABB = ring_mesh.get_aabb()
			# An inward ring is strictly SMALLER on both ground axes than the band it
			# is derived from, so it cannot reach a neighbour's territory...
			if ring_aabb.size.x >= band_aabb.size.x or ring_aabb.size.z >= band_aabb.size.z:
				shoulder_faults.append("the shoulder at %.1f is not inside the band (%s vs %s)" % [
					distance, str(ring_aabb.size), str(band_aabb.size)])
			# ...and it is smaller by exactly twice the displacement, which is what
			# says it is CONCENTRIC rather than merely smaller.
			var shrink: float = band_aabb.size.x - ring_aabb.size.x
			if absf(shrink - 2.0 * absf(distance)) > 1.5:
				shoulder_faults.append("the shoulder at %.1f shrank the band by %.2f, expected %.2f" % [
					distance, shrink, 2.0 * absf(distance)])
	_check(shoulder_faults.is_empty(),
		"the additive border shoulders fall inside their own region, never on a neighbour's ground",
		"faults " + str(shoulder_faults) if not shoulder_faults.is_empty()
			else "both shoulders of %s are concentric and inside retail's band" % sample)

	# 8. WHAT RETAIL'S TWO NEW MESHES ACTUALLY ARE, asserted rather than assumed.
	#    Nothing in this project had ever drawn `LMR_Edge` or `LMR_Highlight`, and
	#    the working assumption - a tighter version of the border ribbon - was
	#    WRONG in a way that cost a capture: `LMR_Edge` encloses no ground area at
	#    all and stands UP off the terrain. It is a vertical curtain around the
	#    province, and lighting it like a stroke blooms into a white blob the size
	#    of the province. `LMR_Highlight` is the opposite: a wide inner band lying
	#    flat on the ground, about a tenth of the region's own area.
	#
	#    Both facts are what this view's colouring is built on, so both are pinned.
	#    A converter that started emitting a flat ribbon for the edge, or a curtain
	#    for the highlight, would light it with numbers chosen for the other thing
	#    and nothing else here would notice.
	var shape_faults: Array[String] = []
	var measured := 0
	for region_id in view.shaded_regions:
		var fill_mesh: ArrayMesh = geometry.region_mesh(String(region_id), "fill")
		var edge_mesh: ArrayMesh = geometry.selection_mesh(String(region_id))
		var highlight_mesh: ArrayMesh = geometry.home_highlight_mesh(String(region_id))
		if fill_mesh == null or edge_mesh == null or highlight_mesh == null:
			continue
		measured += 1
		var fill_area := _ground_area(fill_mesh)
		if fill_area <= 0.0:
			shape_faults.append("%s has a fill with no ground area" % region_id)
			continue
		# THE EDGE IS A CURTAIN: its triangles are (near) vertical, so they project
		# to almost nothing on the ground, and it rises clear of the terrain it
		# stands on.
		# FLATNESS is the ground-projected area over the true surface area: 0 for a
		# wall, 1 for a mesh lying in the ground plane. MEASURED over all 52 regions
		# the two layers do not overlap anywhere near these bounds - the edge runs
		# 0.000 to 0.023 and the highlight 0.720 to 1.000 - so this is a wide gate
		# around a bimodal fact, not a threshold tuned to squeeze past.
		var edge_flatness := _flatness(edge_mesh)
		if edge_flatness > 0.05:
			shape_faults.append("%s: LMR_Edge flatness %.3f, so it is lying down rather than standing"
				% [region_id, edge_flatness])
		if _ground_area(edge_mesh) > fill_area * 0.05:
			shape_faults.append("%s: LMR_Edge shadows %.1f%% of the fill's ground area"
				% [region_id, 100.0 * _ground_area(edge_mesh) / fill_area])
		# THE HIGHLIGHT IS A GROUND BAND: flat, and a real slice of the region's own
		# area that is never the whole of it. Measured range is 9.9% to 36.9%.
		var highlight_flatness := _flatness(highlight_mesh)
		if highlight_flatness < 0.60:
			shape_faults.append("%s: LMR_Highlight flatness %.3f, so it is standing rather than lying down"
				% [region_id, highlight_flatness])
		var share := _ground_area(highlight_mesh) / fill_area
		if share < 0.05 or share > 0.60:
			shape_faults.append("%s: LMR_Highlight covers %.1f%% of the fill, which is neither an inner band nor a fill"
				% [region_id, 100.0 * share])
	if measured == 0:
		shape_faults.append("no region carried all three of fill, edge and highlight")
	_check(shape_faults.is_empty(),
		"retail's LMR_Edge is a standing curtain and its LMR_Highlight is a flat inner band, on every region",
		"faults " + str(shape_faults) if not shape_faults.is_empty()
			else "%d regions measured; every edge encloses no ground and stands clear of it" % measured)


## THE AREA A MESH COVERS ON THE GROUND PLANE, summed over its triangles with the
## y axis dropped. This is how a curtain is told from a ribbon without assuming
## anything about how either was authored: a vertical mesh projects to zero here
## however many triangles it has, and a flat one projects to its true area.
func _ground_area(mesh: ArrayMesh) -> float:
	var arrays: Array = mesh.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array
	var total := 0.0
	for triangle in range(indices.size() / 3):
		var a := vertices[indices[triangle * 3]]
		var b := vertices[indices[triangle * 3 + 1]]
		var c := vertices[indices[triangle * 3 + 2]]
		total += absf((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z)) * 0.5
	return total


## HOW FLAT A MESH IS: its ground-projected area over its true surface area. 1.0
## is a mesh lying in the ground plane, 0.0 a mesh standing perpendicular to it.
## This is what tells a curtain from a ribbon without assuming anything about how
## either was authored.
func _flatness(mesh: ArrayMesh) -> float:
	var arrays: Array = mesh.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array
	var solid := 0.0
	for triangle in range(indices.size() / 3):
		var a := vertices[indices[triangle * 3]]
		var b := vertices[indices[triangle * 3 + 1]]
		var c := vertices[indices[triangle * 3 + 2]]
		solid += (b - a).cross(c - a).length() * 0.5
	if solid <= 0.0:
		return 0.0
	return _ground_area(mesh) / solid


## The shortest distance between two hues, in DEGREES, taking the wrap at 360
## into account. Both arguments are Godot's 0..1 hue.
func _hue_gap(a: float, b: float) -> float:
	var delta: float = absf(a - b) * 360.0
	return minf(delta, 360.0 - delta)


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
## CAN THE PLAYER ACTUALLY DRIVE THIS CAMERA - the round-7 checks, and every one
## of them exists because the owner played the build and reported the opposite.
##
## Direct quote: "I want to be able to zoom all the way out and use WASD to move
## the camera." Neither worked. There was no keyboard binding of any kind, and the
## pull-back stopped at a CONSTANT sitting under the computed cut-edge ceiling, so
## the furthest the player could get was a number rather than the guarantee.
##
## The view under test is put INSIDE THE TREE, unlike the one the framing checks
## drive, because three of these properties are about focus and visibility and
## those are meaningless on a detached control: `_pan_axis` asks the viewport who
## owns the keyboard, and `_process` refuses on a hidden view.
func _check_the_camera_answers_to_the_player(bundle) -> void:
	# A SUBVIEWPORT RATHER THAN THE ROOT WINDOW, and that is not a convenience. A
	# headless run's root window is not visible and owns no GUI focus, so a view
	# parented to it reports `is_visible_in_tree() == false` - which is exactly the
	# guard `_process` refuses on - and `grab_focus()` on a control under it does
	# nothing. A SubViewport is a real GUI viewport in a headless run: visibility
	# resolves, and it has its own focus owner. So these checks drive the same code
	# paths a player does rather than a degenerate version of them.
	var host_viewport := SubViewport.new()
	host_viewport.size = Vector2i(2560, 1440)
	get_root().add_child(host_viewport)
	var host := Control.new()
	host.set_anchors_preset(Control.PRESET_FULL_RECT)
	host.size = Vector2(2560.0, 1440.0)
	host_viewport.add_child(host)
	var view = MapViewScript.new()
	view.build()
	host.add_child(view)
	view.set_anchors_preset(Control.PRESET_FULL_RECT)
	view.size = Vector2(2560.0, 1440.0)
	view._on_resized()
	view.set_bundle(bundle, "")
	# Same reason as in `_check_view_places_regions`: the camera is framed on the
	# playable region set, so a camera check on a view without it drives the
	# fallback framing rather than the player's.
	var driven_geometry = _region_geometry()
	if driven_geometry != null:
		view.set_region_geometry(driven_geometry, "")
	# The edge-scroll drive reads the real pointer, which in a headless run sits
	# whereever the platform says; it is off for these checks so that a mouse
	# resting in a corner cannot move the camera under a keyboard assertion.
	view.edge_scroll_enabled = false
	view.reset_camera()

	# 1. THE PROJECTION ITSELF. `zoom_ceiling()` bounds the camera's ground
	#    footprint to retail's own rectangle, and a PERSPECTIVE frustum lays a
	#    trapezoid on the ground - so its widest legal picture is limited by its
	#    own narrow end, which is the 78.1% the 12-degree lens reached and could
	#    not beat. Asserted as the projection AND as the size the fit wrote, so a
	#    camera that is nominally orthogonal but framed by something else fails.
	var fitted := float(view.camera_state()["distance"]) * float(view.camera_state()["zoom"])
	_check(view.camera.projection == Camera3D.PROJECTION_ORTHOGONAL
			and is_equal_approx(float(view.camera.size), fitted),
		"the strategic camera is a parallel projection framed by the fit times the player's zoom",
		"projection %d, size %.1f against fit x zoom %.1f" % [
			int(view.camera.projection), float(view.camera.size), fitted])

	# 2. THE OPENING FRAMING IS THE GUARANTEE, NOT A CONSTANT UNDER IT. This is
	#    the owner's "zoom all the way out" as a property: what the view opens on
	#    must be the furthest the cut-edge rule allows at this panel, and the
	#    stated cap must be the whole-map framing rather than a tuned fraction of
	#    it. Before round 7 the cap was 0.88 against a ceiling of ~0.80 at 16:9,
	#    which happens to look the same at ONE aspect and throws map away at every
	#    other.
	var opened := float(view.camera_state()["zoom"])
	_check(is_equal_approx(MapViewScript.DEFAULT_ZOOM, MapViewScript.MAX_ZOOM)
			and is_equal_approx(opened, float(view.zoom_ceiling())),
		"the view opens at the pull-back the cut-edge rule allows, not at a constant under it",
		"opened at %.4f, ceiling %.4f, DEFAULT_ZOOM %.3f, MAX_ZOOM %.3f" % [
			opened, float(view.zoom_ceiling()),
			MapViewScript.DEFAULT_ZOOM, MapViewScript.MAX_ZOOM])

	# 3. THE BINDING THE OWNER ASKED FOR, by name. WASD and the arrows both, eight
	#    keys, four directions, and the two sets must agree with each other - an
	#    arrow bound opposite to its letter is a binding bug no camera test would
	#    otherwise catch.
	var keys: Dictionary = MapViewScript.PAN_KEYS
	var bindings_agree: bool = (
		keys.size() == 8
		and keys.get(KEY_W, Vector2.ZERO) == Vector2(0.0, 1.0)
		and keys.get(KEY_S, Vector2.ZERO) == Vector2(0.0, -1.0)
		and keys.get(KEY_A, Vector2.ZERO) == Vector2(-1.0, 0.0)
		and keys.get(KEY_D, Vector2.ZERO) == Vector2(1.0, 0.0)
		and keys.get(KEY_UP, Vector2.ZERO) == keys.get(KEY_W, Vector2.ONE)
		and keys.get(KEY_DOWN, Vector2.ZERO) == keys.get(KEY_S, Vector2.ONE)
		and keys.get(KEY_LEFT, Vector2.ZERO) == keys.get(KEY_A, Vector2.ONE)
		and keys.get(KEY_RIGHT, Vector2.ZERO) == keys.get(KEY_D, Vector2.ONE))
	_check(bindings_agree,
		"WASD and the arrow keys are both bound, to the same four directions",
		str(keys))

	# 4. A HELD KEY ACTUALLY MOVES THE CAMERA, AND THE SPEED SCALES WITH THE ZOOM.
	#    Both halves matter and the second is the one a fixed units-per-second
	#    rule would fail: at the closest framing a speed sized for the whole board
	#    throws the map off the panel in a single tap. The speed is a fraction of
	#    the PICTURE per second, so the ground covered in one second at a given
	#    zoom must be proportional to that zoom - checked as a RATIO against the
	#    zoom ratio, which is a claim about the rule rather than about a constant.
	#    BOTH ZOOMS ARE INSIDE THE REACHABLE RANGE RATHER THAN ON ITS EDGE. A zoom
	#    pinned to the cut-edge ceiling cannot pan at all by design - see the wall
	#    check at the end of this function - so measuring the speed rule there
	#    would measure the wall instead.
	var moved: Array[float] = []
	var zooms := [0.30, MapViewScript.MIN_ZOOM]
	for zoom in zooms:
		view.reset_camera()
		view.focus_region("", float(zoom))
		var start: Vector3 = view.camera_state()["target"]
		_hold(KEY_D, true)
		# `drive_camera` rather than `_process`: the visibility gate `_process`
		# owns cannot be satisfied in a headless run at all - a headless root
		# window is never visible - so going through it would test nothing. The
		# drive underneath it is the whole of what a held key does.
		view.drive_camera(1.0)
		_hold(KEY_D, false)
		moved.append(((view.camera_state()["target"] as Vector3) - start).length())
	var zoom_ratio: float = float(zooms[0]) / maxf(float(zooms[1]), 0.0001)
	var travel_ratio: float = moved[0] / maxf(moved[1], 0.0001)
	_check(moved[0] > 1.0 and moved[1] > 0.0
			and absf(travel_ratio - zoom_ratio) <= zoom_ratio * 0.02,
		"a held key pans the camera, and one second of it covers the same fraction of the picture at every zoom",
		"moved %.1f units at zoom %.3f and %.1f at %.3f (travel ratio %.2f against zoom ratio %.2f)" % [
			moved[0], zooms[0], moved[1], zooms[1], travel_ratio, zoom_ratio])

	# 5. TYPING MUST NOT DRIVE MIDDLE-EARTH. The strategic surface has text entry
	#    on it, and a `W` typed into a field that also slides the map west is the
	#    kind of defect that survives every automated check and is unbearable in
	#    five seconds of play.
	#    Asserted on the RULE rather than through the focus owner, because focus
	#    cannot be granted in a headless run - `grab_focus()` needs a visible tree
	#    and a headless root window is never visible - so a check that went through
	#    it would pass on a view that never consults focus at all. Every class the
	#    strategic surface can put a caret in is named, and a Button is named too:
	#    a rule that swallowed the keyboard whenever ANYTHING had focus would stop
	#    the camera dead the moment the player clicked End Turn.
	var line := LineEdit.new()
	var text := TextEdit.new()
	var spin := SpinBox.new()
	var button := Button.new()
	var takes_keyboard := (
		MapViewScript.keyboard_is_owned_by_text(line)
		and MapViewScript.keyboard_is_owned_by_text(text)
		and MapViewScript.keyboard_is_owned_by_text(spin)
		and not MapViewScript.keyboard_is_owned_by_text(button)
		and not MapViewScript.keyboard_is_owned_by_text(null))
	_check(takes_keyboard,
		"a focused text field takes the keyboard away from the camera, and a focused button does not",
		"LineEdit %s, TextEdit %s, SpinBox %s, Button %s, nothing %s" % [
			MapViewScript.keyboard_is_owned_by_text(line),
			MapViewScript.keyboard_is_owned_by_text(text),
			MapViewScript.keyboard_is_owned_by_text(spin),
			MapViewScript.keyboard_is_owned_by_text(button),
			MapViewScript.keyboard_is_owned_by_text(null)])
	line.queue_free()
	text.queue_free()
	spin.queue_free()
	button.queue_free()

	# 6. THE ZOOM LANDS WHERE THE POINTER IS. Measured on the GROUND, not on the
	#    zoom: the world point under an off-centre pixel must still be under that
	#    pixel after a notch. The centre-anchored behaviour this replaces is
	#    measured in the same units in the same breath, so the check reports how
	#    much the province the player was pointing at used to run away from him.
	view.reset_camera()
	var pointer := Vector2(view.viewport.size) * Vector2(0.80, 0.24)
	var anchored := _anchor_drift(view, pointer, true)
	view.reset_camera()
	var centred := _anchor_drift(view, pointer, false)
	var slab_width := float(bundle.terrain_extent["x_max"]) \
		- float(bundle.terrain_extent["x_min"])
	_check(anchored >= 0.0 and anchored <= slab_width * 0.002 and centred > anchored * 8.0,
		"a wheel notch keeps the ground under the pointer under the pointer",
		"the aimed ground moved %.1f world units anchored against %.1f toward the centre, on a %.0f-unit map" % [
			anchored, centred, slab_width])

	# 7. AND A DRAG CARRIES THE GROUND WITH IT, which is the same property for the
	#    other gesture and was NOT true before round 7: the pan was a fixed
	#    fraction of the fit scalar per pixel, correct at one panel height and one
	#    pitch and wrong at every other, so the map slid faster or slower than the
	#    hand. Asserted at a NON-DEFAULT pitch, because the pitch term is exactly
	#    the one the old rule was missing.
	view.reset_camera()
	view.set_orbit(0.6, -28.0)
	# AT A ZOOM THE CEILING IS NOT CUTTING SHORT. A pan re-solves the cut-edge
	# ceiling, so a request pinned to it is legitimately re-resolved as the camera
	# moves - the picture's SCALE changes mid-drag, and no pan rule can hold an
	# anchor through a scale change. That is the clamp doing its job; asserting
	# through it would be asserting on the clamp rather than on the pan.
	view.focus_region("", 0.20)
	var grab := Vector2(view.viewport.size) * Vector2(0.35, 0.6)
	var drag := Vector2(140.0, -90.0)
	var grabbed: Variant = view._ground_point_at(grab)
	view._pan(drag)
	var dropped: Variant = view._ground_point_at(grab + drag)
	var follow := -1.0
	if grabbed != null and dropped != null:
		follow = Vector2((grabbed as Vector3).x, (grabbed as Vector3).z).distance_to(
			Vector2((dropped as Vector3).x, (dropped as Vector3).z))
	_check(follow >= 0.0 and follow <= slab_width * 0.002,
		"a right-drag carries the ground it grabbed under the pointer, at any pitch",
		"the grabbed ground ended %.1f world units from the pointer on a %.0f-unit map" % [
			follow, slab_width])

	# 8. THE CUT-EDGE WALL, AND WHAT PRESSING INTO IT COSTS. THIS ASSERTION WAS
	#    RESTATED IN ROUND 8 AND THE OLD ONE IS NOT DELETED, IT IS SUPERSEDED -
	#    read this before putting it back.
	#
	#    IT USED TO READ "a pan slides up to the cut-edge wall and STOPS, rather
	#    than buying its way through it with the player's zoom", asserted as
	#    `zoom >= pinned - 0.005` after a second of held key. That was written for
	#    a real defect a photograph found: with the view opening AT the ceiling and
	#    the ceiling solved against the live pan, an unwalled pan drove the zoom
	#    down behind the camera and one second of held `D` left the player looking
	#    at a single province. MEASURED here on the shipped bundle at 2560x1440,
	#    unwalled: 1,926 units of travel for 64% of the zoom, gone in one second.
	#
	#    WHAT THE OLD ASSERTION ALSO PINNED, WITHOUT SAYING SO, IS THE OWNER'S
	#    ROUND-8 COMPLAINT. "It feels very cramped and awful to manoeuvre." A dead
	#    stop at the ceiling is exactly what he was describing, and it is measured
	#    too: with the wall as it stood, one second of held `D` from the opening
	#    framing moved the camera 4.6 world units across a 6,021-unit map. The
	#    camera was bolted down until the player thought to zoom in first, and the
	#    assertion above said that was correct.
	#
	#    SO THE PROPERTY IS NOW THE ONE THAT WAS ALWAYS MEANT: the wall must not be
	#    BOUGHT THROUGH, and it must not be a dead end either. It is asserted as
	#    two claims because they fail independently and a single number cannot say
	#    which broke - the zoom must survive the second (which is what stops the
	#    dive) and the camera must have gone somewhere (which is what stops the
	#    lock). Measured on the shipped bundle: 89% of the zoom kept and 334 units
	#    travelled at 2560x1440, and the same figures to within a unit at 1860x800.
	view.reset_camera()
	var pinned := float(view.camera_state()["zoom"])
	var pinned_at: Vector3 = view.camera_state()["target"]
	_hold(KEY_D, true)
	for _frame in 30:
		view.drive_camera(1.0 / 30.0)
	_hold(KEY_D, false)
	var after_holding := float(view.camera_state()["zoom"])
	var travelled: float = (
		(view.camera_state()["target"] as Vector3) - pinned_at).length()
	_check(after_holding >= pinned * PAN_WALL_ZOOM_KEPT_FLOOR,
		"a pan leans into the cut-edge wall rather than buying its way through it with the player's zoom",
		"a second of held key at maximum pull-back kept %.0f%% of the zoom (%.4f -> %.4f), floor %.0f%%" % [
			100.0 * after_holding / maxf(pinned, 0.0001), pinned, after_holding,
			100.0 * PAN_WALL_ZOOM_KEPT_FLOOR])
	_check(travelled >= PAN_WALL_MIN_TRAVEL,
		"a pan at maximum pull-back actually moves the camera rather than stopping dead",
		"a second of held key at maximum pull-back travelled %.0f world units, floor %.0f" % [
			travelled, PAN_WALL_MIN_TRAVEL])

	view.reset_camera()
	host_viewport.queue_free()


## Press or release a key at the input singleton, so `Input.is_key_pressed` -
## which is what the camera's own poll reads - reports it. Driving the real
## singleton rather than a seam in the view is deliberate: a seam would let the
## poll itself be wrong and still pass.
func _hold(keycode: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = keycode
	event.physical_keycode = keycode
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()


## How far the ground under `pixel` moves over one wheel notch, in world units,
## either through the pointer-anchored path or through the bare zoom.
func _anchor_drift(view, pixel: Vector2, anchored: bool) -> float:
	var before: Variant = view._ground_point_at(pixel)
	if anchored:
		view._zoom_towards(float(view.camera_state()["zoom"]) / MapViewScript.ZOOM_STEP, pixel)
	else:
		view._set_zoom(float(view.camera_state()["zoom"]) / MapViewScript.ZOOM_STEP)
	var after: Variant = view._ground_point_at(pixel)
	if before == null or after == null:
		return -1.0
	return Vector2((before as Vector3).x, (before as Vector3).z).distance_to(
		Vector2((after as Vector3).x, (after as Vector3).z))


## THE MOUSEOVER FLARE HAS A FALLOFF, ASSERTED ON THE FIELD ITSELF.
##
## The flare was a flat additive wash at one alpha over retail's whole `LMR_Fill`
## footprint for seven rounds - light that ends on a hard edge and draws the
## province's raw silhouette as a sheet of colour, on the interaction the player
## spends the most time inside. It is a shader now
## (`shaders/wotr_region_flare.gdshader`) driven by a rim-distance field baked into
## the fill mesh's vertex colour by `wotr_region_geometry.gd:fill_falloff_mesh`.
##
## WHAT IS CHECKED IS THE FIELD, NOT THE SHADER, and that is deliberate: a shader
## cannot be sampled from a headless run, and the defect the field can have is the
## one that matters. Three properties, all measured on retail's own meshes:
##
##   * EVERY drawn region has a baked falloff. A region that quietly fell back to
##     the flat wash is exactly the regression this replaces.
##   * The field REACHES BOTH ENDS. It must touch ~0 (a vertex on the province's
##     outline) and 1 (its deepest interior point) - a constant field would pass
##     any "there is a colour array" check and would be the flat wash again.
##   * The field is ZERO ON THE RIM, stated in WORLD UNITS rather than as a
##     fraction. The field is normalised per region - `1.0` is that province's own
##     deepest point - so "under 0.05" means five units on Buckland and three
##     hundred on Rhun, and the first version of this check failed Buckland for a
##     reading that was correct. `falloff_depth_units` hands back the scale that
##     was divided out, so what is asserted is that a fill vertex sitting `d`
##     units from retail's own ribbon reads no more than `d` units deep, plus the
##     grid's own resolution.
func _check_the_hover_flare_falls_off(view) -> void:
	var geometry = _region_geometry()
	if geometry == null:
		_fail("the mouseover flare falls off to nothing at the province's own rim",
			_geometry_reason)
		return
	var faults: Array[String] = []
	var lowest := INF
	var highest := -INF
	var worst_rim := 0.0
	var worst_rim_at := ""
	var baked := 0
	for key in geometry.by_region.keys():
		var region_id := String(key)
		var mesh: ArrayMesh = geometry.fill_falloff_mesh(region_id)
		if mesh == null:
			faults.append("%s has no baked falloff" % region_id)
			continue
		baked += 1
		var arrays: Array = mesh.surface_get_arrays(0)
		var colors := arrays[Mesh.ARRAY_COLOR] as PackedColorArray
		var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
		if colors.size() != vertices.size() or colors.is_empty():
			faults.append("%s carries %d colour(s) for %d vertex(es)" % [
				region_id, colors.size(), vertices.size()])
			continue
		var here_low := INF
		var here_high := -INF
		for value in colors:
			here_low = minf(here_low, value.r)
			here_high = maxf(here_high, value.r)
		lowest = minf(lowest, here_low)
		highest = maxf(highest, here_high)
		if here_low > 0.02 or here_high < 0.98:
			faults.append("%s spans only %.3f..%.3f" % [region_id, here_low, here_high])
		# THE RIM READS ZERO. Retail's own border ribbon for this region is the rim,
		# so any fill vertex sitting on top of a ribbon vertex must be dark.
		var rim: ArrayMesh = geometry.region_mesh(region_id, geometry.BORDER_LAYER)
		if rim == null:
			continue
		var ribbon := (rim.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]) as PackedVector3Array
		# A sparse stride: this is a spot check over thousands of vertices, not a
		# second implementation of the bake.
		var stride := maxi(ribbon.size() / 24, 1)
		for rim_index in range(0, ribbon.size(), stride):
			var ribbon_point := ribbon[rim_index]
			var nearest := INF
			var nearest_depth := 0.0
			var fill_stride := maxi(vertices.size() / 200, 1)
			for fill_index in range(0, vertices.size(), fill_stride):
				var offset := Vector2(vertices[fill_index].x - ribbon_point.x,
					vertices[fill_index].z - ribbon_point.z)
				var distance := offset.length()
				if distance < nearest:
					nearest = distance
					nearest_depth = colors[fill_index].r
			# Only judge a ribbon point that a sampled fill vertex actually sits on.
			if nearest > 6.0:
				continue
			# IN WORLD UNITS. A vertex `nearest` units from the ribbon may honestly
			# read that deep; what it may not do is read deeper. The allowance is
			# the distance transform's own resolution - one cell of a 64-square
			# grid is 1.6% of the region, and the chamfer adds a few percent on top.
			var scale_units := float(geometry.falloff_depth_units(region_id))
			var reads_units := nearest_depth * scale_units
			var allowed := nearest + 0.06 * scale_units
			var overshoot := reads_units - allowed
			if overshoot > worst_rim:
				worst_rim = overshoot
				worst_rim_at = "%s reads %.1f units deep %.1f units from the ribbon (allowed %.1f)" % [
					region_id, reads_units, nearest, allowed]
	if worst_rim > 0.0:
		faults.append("a fill vertex on the rim reads too deep: %s" % worst_rim_at)
	var flat: PackedStringArray = view.flat_flare_regions
	if not flat.is_empty():
		faults.append("the view reports %d region(s) still on the flat wash: %s" % [
			flat.size(), ", ".join(flat)])
	_check(faults.is_empty(),
		"the mouseover flare falls off to nothing at the province's own rim",
		"%d region(s) baked in %.0f ms, field spans %.3f..%.3f, worst rim overshoot %.2f units%s" % [
			baked, geometry.falloff_build_ms, lowest, highest, worst_rim,
			"" if faults.is_empty() else "; " + "; ".join(faults)])


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
	var unforced: Array[String] = []
	for panel_size in panels:
		view.size = panel_size as Vector2
		view._on_resized()
		for orbit in orbits:
			framings += 1
			view.reset_camera()
			view.set_orbit(float(orbit[0]), float(orbit[1]))
			# PINNED TO ZOOM 1.0, THE FIT ITSELF. The opening zoom is now BELOW
			# 1.0 on purpose - retail's map overfills the screen - so the
			# containment property lives at zoom 1 (the distance `_fit_distance`
			# solves for) and is asserted there. Written directly because the
			# public zoom clamp deliberately cannot reach 1.0 any more; the
			# full-bleed checks below are what hold the reachable range.
			view._zoom = 1.0
			view._apply_camera()
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
			# panel. This was 72 px - 14.5% of the panel height - before the fix,
			# and it is NOT required to be zero any more; see the check below for
			# what replaced that requirement and why.
			var offset := maxf(
				absf(box.position.x - (panel.x - box.end.x)) / maxf(panel.x, 1.0),
				absf(box.position.y - (panel.y - box.end.y)) / maxf(panel.y, 1.0))
			if offset > worst_offset:
				worst_offset = offset
				offset_at = "panel %s yaw %.1f pitch %.0f: %.1f/%.1f left/right, %.1f/%.1f top/bottom" % [
					panel, orbit[0], orbit[1],
					box.position.x, panel.x - box.end.x,
					box.position.y, panel.y - box.end.y]
			# AND IF IT IS OFF CENTRE, PROVE IT HAD TO BE. The framing box is a BOX
			# about the point the camera is aimed at, so its eight corners come in
			# +- pairs about that point and the span's midpoint is exactly zero on
			# both screen axes at every basis. "Dead centre" is therefore literally
			# `_framing_offset == (0, 0)`, and the question "did the centring have to
			# move?" can be asked by writing that in and looking.
			if offset > 0.005:
				var live: Vector2 = view._framing_offset
				view._framing_offset = Vector2.ZERO
				view._apply_camera()
				var forced: bool = view.slab_cut_edge_is_in_frame(1.0)
				view._framing_offset = live
				view._apply_camera()
				# THE SECOND LICENCE, ADDED IN ROUND 9. See the restatement under
				# this check for why the rim is no longer the only thing allowed to
				# move the framing, and for the bound the bias is held to.
				var per_pixel: float = float(view._camera_distance) / maxf(panel.y, 1.0)
				var allowance: float = view.occluded_bottom() * per_pixel
				var biased: bool = live.y < 0.0 and absf(live.y) <= allowance + 1.0
				# The bias is on the LIFT axis only, so an across-axis offset is
				# still forced-or-nothing exactly as it always was.
				var across_offset: float = absf(box.position.x - (panel.x - box.end.x)) \
					/ maxf(panel.x, 1.0)
				if not forced and not (biased and across_offset <= 0.005):
					unforced.append(
						"panel %s yaw %.1f pitch %.0f is %.1f%% off centre with centring %s, dead centre would NOT have shown the cut, and the offset is not a southward bias inside the %.1f-unit occluded band (across %.2f%%)" % [
							panel, orbit[0], orbit[1], 100.0 * offset, str(live),
							allowance, 100.0 * across_offset])
	view.size = original
	view._on_resized()

	# RESTATED IN ROUND 8, AND THE OLD WORDING IS RECORDED HERE RATHER THAN
	# DELETED. It used to read "RETAIL'S WHOLE MAP projects inside the panel at
	# every orbit and panel shape", measured over `terrain_extent` - the 6021 x
	# 4819 rectangle of twenty terrain tiles. That was the right assertion while
	# retail's own framing was the target. It is the wrong one now: a third of that
	# rectangle is painted seabed and empty ice that no region is ever placed on,
	# framing it is what made the board read small, and the owner's verdict on the
	# result was "it feels very cramped and awful to manoeuvre, it feels like the
	# FOV or resolution is very low". So the SUBJECT of the framing changed - see
	# `wotr_map_view.gd:_framing_box` - and this assertion changed with it, to the
	# thing the new framing actually promises: EVERY PLAYABLE REGION IS ON THE
	# PANEL. It is not weaker. The old form permitted a camera that framed the slab
	# and dropped a province off the bottom edge, and that is exactly what round 7
	# shipped: measured at 1860x800, Harad projected 117.6 px past the bottom of
	# the panel while the top of the frame was spent on open ocean, and this check
	# passed.
	_check(worst_overshoot <= 0.5,
		"the whole playable region set projects inside the panel at every orbit and panel shape",
		"worst overshoot %.2f px over %d framing(s)%s" % [
			worst_overshoot, framings,
			"" if overshoot_at.is_empty() else " (" + overshoot_at + ")"])
	# RESTATED IN ROUND 8. THE OLD WORDING IS RECORDED HERE RATHER THAN DELETED.
	#
	# It used to read "the map is centred in the panel rather than riding low in
	# it", asserted as "the two slacks differ by under 1% of the panel". It was
	# written for a real defect - an orthographic estimate of a perspective
	# trapezoid left 72 px of Middle-earth's south coast below the panel and a 72
	# px empty band along the top - and dead centre was the right answer while the
	# subject of the fit was retail's own slab, because the slab has no rim inside
	# itself to avoid.
	#
	# THE PLAYABLE REGION SET DOES. It sits off-centre inside retail's slab: the
	# slab's own southern cut is 182 units behind Harad and its northern cut is 910
	# units beyond Forodwaith, so a picture deep enough to hold every region and
	# centred dead on the region set reaches past the southern cut and shows the
	# raw silhouette. Measured at 2560x1351, centring the framing exactly costs 6%
	# of the pull-back to `zoom_ceiling()`; at 1100x700 it costs 17%.
	#
	# So the property is now the one that was always meant - THE FRAMING IS DEAD
	# CENTRE UNLESS RETAIL'S OWN RIM FORBIDS IT - and it is asserted directly:
	# wherever the centring is off centre by more than half a percent of the panel,
	# writing dead centre back in must put the slab's cut edge in frame. That is
	# STRICTLY STRONGER than a tolerance. A tolerance can be met by a framing that
	# is off centre for no reason at all as long as it is off centre by a little;
	# this cannot, and the 72-px defect fails it exactly as loudly as before,
	# because a fit that rides 14.5% low with the cut nowhere near the frame is
	# precisely an unforced offset.
	# RESTATED AGAIN IN ROUND 9, AND THE ROUND-8 WORDING IS KEPT ABOVE RATHER THAN
	# DELETED, because the round-8 property is still half of this one.
	#
	# WHAT CHANGED IN THE VIEW. An art-direction review of the round-8 capture
	# reported "You hid Mordor ... if the camera can be biased at all, bias it
	# south", and it could be: the fit leaves about 95 px of slack above the region
	# set at 2560x1440 and none below, and the only thing that had been stopping
	# the picture rising into it was the cut-edge rule being asked about the WHOLE
	# panel when the bottom `occluded_bottom()` pixels are under the strategic tray
	# - and are inked by the map itself (`_draw_tray_feather`) whether the tray is
	# there or not. So the framing now spends that slack southwards on purpose.
	#
	# WHY THIS IS STILL NOT A TOLERANCE. Three things are required of any offset
	# that the rim does not force, and all three have to hold together:
	#
	#   * it is on the LIFT axis only - an across-axis offset is forced-or-nothing
	#     exactly as it was, so a fit that drifts sideways for no reason still
	#     fails;
	#   * it is SOUTHWARD, i.e. the picture rises. The 72-px defect this check was
	#     originally written for rode LOW in the panel, which is the opposite sign,
	#     and it fails here exactly as loudly as it did before;
	#   * it is no larger than the occluded band itself, in the view's own world
	#     units. An unbounded "bias" that walked the board off the top of the panel
	#     is a different defect and this refuses it - as does the containment check
	#     above, independently.
	_check(unforced.is_empty(),
		"the framing sits dead centre in the panel except where retail's cut edge forbids it or the southward tray bias claims it",
		"worst slack difference %.2f%% of the panel over %d framing(s)%s%s" % [
			100.0 * worst_offset, framings,
			"" if offset_at.is_empty() else " (" + offset_at + ")",
			"" if unforced.is_empty() else "; UNFORCED: " + "; ".join(unforced)])

	# THE CENTRING SCALES WITH THE ZOOM, and this is why that matters. It shifts
	# the camera AND the point it looks at, so if it did not scale with the
	# distance it would grow into a huge offset as the player zoomed in and
	# `focus_region` would carry its region off the panel. Scaled, it is a
	# CONSTANT offset on screen: the aimed point projects to the same pixel at
	# every zoom. Checked at a non-zero yaw, across the whole ~21x range.
	view.reset_camera()
	view.set_orbit(1.1, -21.0)
	var aimed: Vector3 = view.camera_state()["target"]
	var seen: Array[Vector2] = []
	for zoom in [MapViewScript.MAX_ZOOM, 0.31, MapViewScript.MIN_ZOOM]:
		view.focus_region("", float(zoom))
		seen.append(_project(view, aimed))
	var drift := 0.0
	for point in seen:
		drift = maxf(drift, point.distance_to(seen[0]))
	_check(drift <= 1.0,
		"zooming does not slide the framing across the panel",
		"the aimed point moved %.2f px across zoom %.2f / 0.31 / %.2f (%s)" % [
			drift, MapViewScript.MAX_ZOOM, MapViewScript.MIN_ZOOM, str(seen)])
	view.reset_camera()

	_check_the_opening_framing_is_full_bleed(view)


## RETAIL'S MAP FILLS THE SCREEN. In every reference capture of the strategic
## screen the map bleeds off all four edges - the only place a map edge shows
## is the western sea melting into the border cloud, and no capture anywhere
## shows the terrain slab's own rectangular cut. Round 1's opening framing
## showed exactly that cut, and no assertion said so; these two checks are why
## it cannot come back.
##
## WHAT IS ASSERTED, at every panel shape the containment checks use:
##
##  * at the OPENING framing (`reset_camera()`), a 3x3 grid across the panel's
##    interior (20/50/80% on both axes) ray-hits retail's own terrain - the
##    picture is Middle-earth wall to wall, not a slab in a void - and the four
##    panel corners hit terrain or the border cloud, never the void;
##  * at MAXIMUM PULL-BACK (`focus_region("", 99)`, which the clamp resolves to
##    the view's own computed `zoom_ceiling()`), the same holds AND MORE: the
##    four corners must be TERRAIN, not merely cloud. That is the whole point of
##    the ceiling being computed per aspect rather than tuned once - it exists so
##    that no supported panel shape can put the slab's cut edge in the picture,
##    and "the corner is cloud" is exactly the answer that let the cut through;
##  * the clamp lands ON that computed ceiling, and the ceiling is never above
##    the stated constant `MAX_ZOOM`, so the reachable range is always inside the
##    range the constants advertise.
##
## THE PANEL LIST IS THE ONE THAT GETS PHOTOGRAPHED. 1860x800 and 2560x1351 are
## the capture runner's own two windows, and with the map full-bleed the map
## panel IS the window; 1264x496 and 760x396 are the older inset panel sizes and
## are kept because a check that stops covering a shape it used to cover is a
## weakened check. 1860x800 is 2.33:1 - the aspect a blind art review judged, and
## the one the old fixed ceiling was wrong for.
##
## A pixel "hits terrain" when the ray through it crosses the terrain's OWN
## x/y footprint at both the bottom and the top of retail's height range - if
## both bracketing planes are crossed inside the footprint, every surface
## between them is too, so the test cannot be fooled by relief. The cloud test
## uses the BORDERCLOUD mesh's real baked extent, not a chosen margin.
func _check_the_opening_framing_is_full_bleed(view) -> void:
	var extent: Dictionary = view.bundle.terrain_extent
	var low := BundleScript.world_to_godot(
		float(extent["x_min"]), float(extent["y_min"]), 0.0)
	var high := BundleScript.world_to_godot(
		float(extent["x_max"]), float(extent["y_max"]), 0.0)
	var terrain_rect := Rect2(
		Vector2(minf(low.x, high.x), minf(low.z, high.z)),
		Vector2(absf(high.x - low.x), absf(high.z - low.z)))
	var cloud_mesh: ArrayMesh = view.bundle.sub_object("BORDERCLOUD")["mesh"]
	var cloud_aabb: AABB = cloud_mesh.get_aabb()
	var cloud_rect := Rect2(
		Vector2(cloud_aabb.position.x, cloud_aabb.position.z),
		Vector2(cloud_aabb.size.x, cloud_aabb.size.z))
	var cloud_height := cloud_aabb.position.y + cloud_aabb.size.y * 0.5
	# Godot y is retail z (`world_to_godot` is (x, z, -y)); the two bracketing
	# ground planes are retail's own height range.
	var plane_heights := [float(extent["z_min"]), float(extent["z_max"])]

	var panels := [
		Vector2(1264.0, 496.0), Vector2(1868.0, 1047.0),
		Vector2(760.0, 396.0), Vector2(1240.0, 620.0),
		Vector2(1860.0, 800.0), Vector2(2560.0, 1351.0), Vector2(1100.0, 700.0)]
	var original: Vector2 = view.size
	var failures := {"opening": PackedStringArray(), "clamped": PackedStringArray()}
	# Every panel's clamped zoom and the ceiling the view says it computed, so a
	# single number cannot hide a shape that disagreed with it.
	var clamped_zooms: Array[String] = []
	var ceiling_disagreements: Array[String] = []
	var ceiling_over_constant: Array[String] = []
	for mode in ["opening", "clamped"]:
		for panel_size in panels:
			view.size = panel_size as Vector2
			view._on_resized()
			view.reset_camera()
			if mode == "clamped":
				view.focus_region("", 99.0)
				var reached := float(view.camera_state()["zoom"])
				var ceiling := float(view.zoom_ceiling())
				clamped_zooms.append("%s -> %.3f" % [str(panel_size), reached])
				if not is_equal_approx(reached, ceiling):
					ceiling_disagreements.append(
						"panel %s clamped to %.4f but zoom_ceiling() says %.4f" % [
							str(panel_size), reached, ceiling])
				if ceiling > MapViewScript.MAX_ZOOM + 0.0001:
					ceiling_over_constant.append(
						"panel %s ceiling %.4f is above the stated MAX_ZOOM %.4f" % [
							str(panel_size), ceiling, MapViewScript.MAX_ZOOM])
			var panel := Vector2(view.viewport.size)
			# The interior grid: terrain, nothing but terrain.
			for u in [0.2, 0.5, 0.8]:
				for v in [0.2, 0.5, 0.8]:
					var pixel := Vector2(panel.x * float(u), panel.y * float(v))
					if not _pixel_hits_terrain(view, pixel, plane_heights, terrain_rect):
						failures[mode].append("panel %s interior %s misses terrain" % [
							str(panel), str(pixel)])
			# THE SLAB'S CUT EDGE, asserted as the property itself rather than
			# through a stand-in for it. `slab_cut_edge_is_in_frame()` clips the
			# twelve edges of retail's own terrain box against the frustum in
			# closed form, so what is asked here is the exact question the defect
			# is about: is the raw straight silhouette where the tile slab stops
			# and the void begins anywhere in the picture. Asserted at BOTH the
			# opening framing and maximum pull-back, at every panel shape. The
			# round-2 regression ran that cut down the left quarter of a 2.33:1
			# frame - the pixels ray-traced back to retail x = -2784, which is
			# `terrain_extent.x_min` to within a unit - and it must fail here if
			# it ever comes back.
			#
			# THIS REPLACES "the four corners must be TERRAIN at maximum
			# pull-back", which was the same guarantee stated as a proxy and was
			# read back as a ban on ocean and cloud at a frame corner. Retail's
			# own capture has open sea under weather in three of its four
			# corners, and that sea is retail's own sea-painted western tiles -
			# `LM_01`/`LM_06`/`LM_11`/`LM_16`, whose boundary vertices all sit
			# below the WATER plane's top face. Ocean at a corner was never the
			# defect; the cut is.
			if view.slab_cut_edge_is_in_frame(float(view.camera_state()["zoom"])):
				failures[mode].append(
					"panel %s: the terrain slab's cut edge is in frame" % str(panel))
			# And a corner must still be map rather than void: terrain, or
			# retail's own border cloud beyond it.
			for corner in [Vector2(0, 0), Vector2(panel.x, 0),
					Vector2(0, panel.y), Vector2(panel.x, panel.y)]:
				if _pixel_hits_terrain(view, corner, plane_heights, terrain_rect):
					continue
				var hit: Variant = _ground_hit(view, corner, cloud_height)
				if hit == null or not cloud_rect.has_point(hit as Vector2):
					failures[mode].append("panel %s corner %s shows the void" % [
						str(panel), str(corner)])
	view.size = original
	view._on_resized()
	view.reset_camera()

	var opening: PackedStringArray = failures["opening"]
	var clamped: PackedStringArray = failures["clamped"]
	_check(opening.is_empty(),
		"the opening framing is full-bleed: terrain wall to wall, cloud at worst in the corners",
		"; ".join(opening))
	_check(clamped.is_empty(),
		"maximum pull-back is clamped before the slab's cut edge can enter the frame at any supported aspect",
		"%s (clamped to %s)" % ["; ".join(clamped), ", ".join(clamped_zooms)])
	_check(ceiling_disagreements.is_empty(),
		"the pull-back the player reaches IS the view's own computed per-aspect ceiling",
		"; ".join(ceiling_disagreements))
	_check(ceiling_over_constant.is_empty(),
		"the computed ceiling never exceeds the stated MAX_ZOOM, so the reachable range stays inside the advertised one",
		"; ".join(ceiling_over_constant))
	_check_the_opening_framing_reaches_the_south_east(view)


## HOW MUCH OF MIDDLE-EARTH THE PLAYER IS ACTUALLY SHOWN, pinned so that it
## cannot quietly shrink again.
##
## Two blind reviews in a row reported the same thing about this map: "loses the
## whole south-east: no Mordor lava channels, no Barad-dur marker, no inland
## sea", and both times it was read as a texture or a grade problem. It was
## neither. It was FRAMING - four of those five items were simply off the panel -
## and nothing in this runner could see it, because every framing check here
## asked whether the picture was clean and none asked whether it was BIG.
##
## So this asserts the size of the picture, two ways, and both are measured
## against retail's own numbers rather than chosen:
##
##  1. THE AREA. The ground quad the camera actually frames at the opening
##     framing, over retail's own 6021 x 4819 terrain rectangle. Retail's own
##     capture frames essentially the whole slab (its four frame corners solve to
##     roughly (-2781, 3163), (3088, 3033), (-2533, -1362) and (3336, -1492)), and
##     no framing that keeps the cut edge out can reach 100% of a rectangle with
##     a trapezoid. The floor is 0.70: the old 45-degree lens could not exceed
##     0.50 at this aspect however it was placed, so a lens or pitch regression
##     that undoes the recovery cannot pass this by accident.
##
##  2. THE SOUTH-EAST ITSELF, by name. Five of retail's own sub-objects that live
##     in the quarter of the map the reviews said was missing must project INSIDE
##     the panel at the opening framing: Mordor's lava field, the Black Gate,
##     Cirith Ungol, Osgiliath and Minas Tirith. Every coordinate is the bundle's
##     own baked bounds, so nothing here is a chosen point.
func _check_the_opening_framing_reaches_the_south_east(view) -> void:
	var original: Vector2 = view.size
	# RETAIL'S OWN ASPECT, which is the frame the oracle was captured in and the
	# one the coverage number above is quoted for.
	view.size = Vector2(2560.0, 1440.0)
	view._on_resized()
	view.reset_camera()
	var panel := Vector2(view.viewport.size)
	var extent: Dictionary = view.bundle.terrain_extent
	var slab_area := (float(extent["x_max"]) - float(extent["x_min"])) \
		* (float(extent["y_max"]) - float(extent["y_min"]))
	# The framed ground quad, at retail's own mid height so the measurement is of
	# the ground the player sees rather than of a plane nothing stands on.
	var height := (float(extent["z_min"]) + float(extent["z_max"])) * 0.5
	var quad: Array[Vector2] = []
	var reached_ground := true
	for pixel in [Vector2(0, 0), Vector2(panel.x, 0),
			Vector2(panel.x, panel.y), Vector2(0, panel.y)]:
		var hit: Variant = _ground_hit(view, pixel, height)
		if hit == null:
			reached_ground = false
			break
		# `_ground_hit` returns godot x/z; `world_to_godot` is (x, z, -y).
		quad.append(Vector2((hit as Vector2).x, -(hit as Vector2).y))
	var covered := 0.0
	if reached_ground:
		var doubled := 0.0
		for index in quad.size():
			var here: Vector2 = quad[index]
			var next: Vector2 = quad[(index + 1) % quad.size()]
			doubled += here.x * next.y - next.x * here.y
		covered = absf(doubled) * 0.5 / maxf(slab_area, 1.0)
	# THIS ASSERTION WAS TURNED UPSIDE DOWN IN ROUND 8 AND THE OLD ONE IS RECORDED
	# HERE RATHER THAN DELETED. Read this before putting a floor back.
	#
	# IT USED TO READ "the opening framing presents AT LEAST 90% of retail's own
	# map area", and the number had a history: 0.70 against a 12-degree
	# PERSPECTIVE lens that reached 78.1% and could not do better, raised to 0.90
	# when the camera became parallel and reached 95.8%. Both were the right shape
	# of assertion while PARITY WITH RETAIL was the target - retail frames its own
	# slab, so framing less of the slab than retail did was a defect.
	#
	# UNDER THE NEW BRIEF FRAMING MORE OF THE SLAB IS THE DEFECT. A third of that
	# rectangle is ground no region is ever placed on - retail's painted seabed
	# column in the west, the empty ice north of Forodwaith - and every percent of
	# it in frame is a percent of the panel not spent on the board. The owner's
	# words were "it feels very cramped and awful to manoeuvre, it feels like the
	# FOV or resolution is very low, like from the 2005 game, we can do better than
	# this". So the picture now frames the PLAYABLE REGION SET, and the same number
	# that used to be a floor is a CEILING: 95.8% of the slab was the old framing
	# and 75.8% is the new one, and the board is drawn 1.12x larger across and
	# 1.26x larger in area for it.
	#
	# A CEILING ON ITS OWN WOULD BE TRIVIAL TO SATISFY - point the camera at one
	# province - so it is asserted TOGETHER WITH the containment check above and
	# with the region-by-region check below, which forbid exactly that. The pair is
	# what pins the intent: as much board as possible, with every province of it on
	# the panel.
	#
	# The measurement is printed on PASS as well as on failure, because "how much
	# of Middle-earth does the player actually see" is the number the owner
	# complained about and a silent PASS does not report it.
	var region_box: AABB = AABB()
	var geometry = _region_geometry()
	if geometry != null:
		region_box = geometry.playable_bounds()
	var region_area := maxf(region_box.size.x * region_box.size.z, 1.0)
	var coverage_detail := "framed %.1f%% of the %.0f x %.0f slab, %.1f%% of the %.0f x %.0f playable region set (framing: %s)%s" % [
		100.0 * covered,
		float(extent["x_max"]) - float(extent["x_min"]),
		float(extent["y_max"]) - float(extent["y_min"]),
		100.0 * covered * slab_area / region_area,
		region_box.size.x, region_box.size.z, view.framing_source(),
		"" if reached_ground else " (a frame corner never reached the ground)"]
	if reached_ground and covered <= 0.80:
		_ok("the opening framing spends the panel on the playable board, not on retail's empty sea and ice",
			coverage_detail)
	else:
		_fail("the opening framing spends the panel on the playable board, not on retail's empty sea and ice",
			coverage_detail)

	# AND EVERY PROVINCE OF IT IS ON THE PANEL AT THE OPENING FRAMING. This is the
	# other half of the pair, and it is asserted at the OPENING zoom rather than at
	# zoom 1.0 - the containment check above pins the FIT, this pins the PICTURE
	# the player is handed, after `zoom_ceiling()` has had its say. The two are the
	# same at retail's own aspect and they are not at every aspect, which is
	# exactly why both exist.
	#
	# THE TOLERANCE IS 2 PX AND IT IS MEASURED, NOT CHOSEN. Harad's fill reaches
	# within 1.3 lift-units of the projection of the slab's own southern wall, so
	# the opening zoom is trimmed to 0.9991 and the framing loses six tenths of a
	# pixel on each edge with it. 2 px is above that and three orders of magnitude
	# below the 117.6 px by which round 7 lost Harad entirely at 1860x800.
	var off_panel: Array[String] = []
	if geometry != null:
		for key in geometry.by_region.keys():
			var mesh: ArrayMesh = geometry.region_mesh(String(key), geometry.FILL_LAYER)
			if mesh == null or mesh.get_surface_count() == 0:
				continue
			var aabb: AABB = mesh.get_aabb()
			var worst := 0.0
			for corner in 8:
				var at := _project(view, aabb.get_endpoint(corner))
				worst = maxf(worst, maxf(-at.x, -at.y))
				worst = maxf(worst, maxf(at.x - panel.x, at.y - panel.y))
			if worst > 2.0:
				off_panel.append("%s by %.1f px" % [String(key), worst])
	_check(geometry != null and off_panel.is_empty(),
		"every one of retail's playable regions is inside the panel at the opening framing",
		"%d region(s) off a %s panel%s%s" % [
			off_panel.size(), str(panel),
			"" if off_panel.is_empty() else ": " + "; ".join(off_panel),
			"" if geometry != null else " - " + _geometry_reason])

	# THE SOUTH-EAST, BY NAME. Every point is a retail sub-object's own baked
	# bound, so this cannot drift with a taste change.
	var south_east := [
		"LM_LAVA01THISON", "LM_BLACKGATE", "LM_CIRITHONGUL", "LM_MINASTIRITH",
		"LM_OSGILLIATH",
	]
	var offscreen: Array[String] = []
	for name in south_east:
		var row: Dictionary = view.bundle.sub_object(String(name))
		if row.is_empty():
			offscreen.append("%s is not in the bundle at all" % String(name))
			continue
		# The sub-object's own centre, from its baked bounds. Not a corner of the
		# bounding box: `LM_LAVA01THISON`'s box corner is out over Rhun's southern
		# march where no lava is drawn, and asserting on it would be asserting on
		# an artefact of the box rather than on the art.
		var centre: Vector3 = ((row["bounds_min"] as Vector3)
			+ (row["bounds_max"] as Vector3)) * 0.5
		var at := _project(view, BundleScript.world_to_godot(
			centre.x, centre.y, centre.z))
		if at.x < 0.0 or at.y < 0.0 or at.x > panel.x or at.y > panel.y:
			offscreen.append("%s (retail %.0f, %.0f) projects to %s, off a %s panel" % [
				String(name), centre.x, centre.y, str(at), str(panel)])
	_check(offscreen.is_empty(),
		"the south-east - Mordor's lava, the Black Gate, Cirith Ungol, Minas Tirith - is in the opening frame",
		"; ".join(offscreen))
	view.size = original
	view._on_resized()
	view.reset_camera()


## Where the ray through `pixel` crosses the horizontal plane at `height`, as
## the crossing's godot x/z, or null when it never does. Built from the
## camera's own transform - the one `look_at_from_position` wrote - for the
## same reason `_project` is: this runner drives a view outside a scene tree.
## ROUND 7 MADE IT PARALLEL, with the map view's camera. Under a parallel
## projection every pixel's ray runs along the SAME direction and only the origin
## moves across the film plane, which is what the two basis terms below are; the
## half-extents are world units (`camera.size`) rather than tangents of an angle.
func _ground_hit(view, pixel: Vector2, height: float) -> Variant:
	var panel := Vector2(view.viewport.size)
	var half_vertical := maxf(float(view.camera.size), 0.0001) * 0.5
	var half_horizontal := half_vertical * (panel.x / maxf(panel.y, 1.0))
	var transform: Transform3D = view.camera.transform
	var origin := transform.origin \
		+ transform.basis.x * ((pixel.x / maxf(panel.x, 1.0) * 2.0 - 1.0) * half_horizontal) \
		+ transform.basis.y * ((1.0 - pixel.y / maxf(panel.y, 1.0) * 2.0) * half_vertical)
	var direction := -transform.basis.z
	if absf(direction.y) < 0.0001:
		return null
	var t := (height - origin.y) / direction.y
	if t <= 0.0:
		return null
	var hit := origin + direction * t
	return Vector2(hit.x, hit.z)


## True when the ray through `pixel` crosses the terrain footprint at BOTH
## bracketing heights - which means it crosses every surface between them
## inside the footprint too, whatever the relief does.
func _pixel_hits_terrain(
	view, pixel: Vector2, plane_heights: Array, terrain_rect: Rect2
) -> bool:
	for height in plane_heights:
		var hit: Variant = _ground_hit(view, pixel, float(height))
		if hit == null or not terrain_rect.has_point(hit as Vector2):
			return false
	return true


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
## PARALLEL, with the camera: no divide by the depth at all, which is the whole
## difference the projection change makes here.
func _project(view, world: Vector3) -> Vector2:
	var local: Vector3 = view.camera.transform.affine_inverse() * world
	var panel := Vector2(view.viewport.size)
	var half_vertical := maxf(float(view.camera.size), 0.0001) * 0.5
	var half_horizontal := half_vertical * (panel.x / maxf(panel.y, 1.0))
	return Vector2(
		(local.x / half_horizontal + 1.0) * 0.5 * panel.x,
		(1.0 - local.y / half_vertical) * 0.5 * panel.y)


## THE SCREEN-SPACE BOUNDING BOX OF THE THING THE CAMERA IS FRAMING - every one of
## retail's own region fill meshes when the region bundle loaded, and retail's
## terrain extent when it did not, which is exactly the fallback the view itself
## takes (`wotr_map_view.gd:_framing_box`). Measured from the REGION MESHES rather
## than from the view's own box so this is an independent measurement of the
## subject rather than a restatement of the fit's own input.
func _projected_footprint(view) -> Rect2:
	var low := Vector2(INF, INF)
	var high := Vector2(-INF, -INF)
	var corners: Array[Vector3] = []
	var geometry = _region_geometry()
	if geometry != null:
		for key in geometry.by_region.keys():
			var mesh: ArrayMesh = geometry.region_mesh(
				String(key), geometry.FILL_LAYER)
			if mesh == null or mesh.get_surface_count() == 0:
				continue
			var aabb: AABB = mesh.get_aabb()
			for corner in 8:
				corners.append(aabb.get_endpoint(corner))
	if corners.is_empty():
		var extent: Dictionary = view.bundle.terrain_extent
		for xi in [float(extent["x_min"]), float(extent["x_max"])]:
			for yi in [float(extent["y_min"]), float(extent["y_max"])]:
				for zi in [float(extent["z_min"]), float(extent["z_max"])]:
					corners.append(BundleScript.world_to_godot(xi, yi, zi))
	for world in corners:
		var point := _project(view, world)
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
		# map that silently dropped its rivers and its smoke would look
		# complete. WATER left this list when the water shader was implemented
		# and the LM_COAST strips and PLANE03 left it in round 2 - all are
		# DRAWN now, so requiring them under NOT DRAWN would be asserting a
		# falsehood; the check is exactly as strong on the surfaces still held
		# back (the RIVERS underlay, the lava frame-sequence planes, and the
		# vertex-alpha smoke cards round 2 tried and measured out).
		_check(label.contains("NOT DRAWN") and label.contains("LAVAPLANE1")
				and label.contains("LM_SMOKE01") and label.contains("RIVERS"),
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
