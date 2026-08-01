extends SceneTree

## RETAIL'S REGION GEOMETRY, AND THE FIVE EFFECTS THAT ARE FIVE DIFFERENT SHAPES.
##
## THE CLAIM UNDER TEST. `data/ini/livingworldregioneffects.ini` separates its
## region effects BY GEOMETRY, not by colour:
##
##     BordersEffect            geometry LMR_Border
##     FilledOwnershipEffect    geometry LMR_Fill
##     MouseoverEffectFlareup   geometry LMR_Fill
##     RegionSelectionEffect    geometry LMR_Edge
##     HomeRegionHighlight      geometry LMR_Highlight
##
## The bundle used to carry only fill, border and territory, so selection and
## home-region had no retail art behind them and the screen recoloured the
## ownership band to stand in for both. A blind review read every owned red
## region as a selection wash, correctly. This runner holds the fix: the bundle
## carries `edge` and `highlight`, they are DIFFERENT MESHES from the border
## rather than the same one under another name, and the accessors that hand them
## out fail CLOSED - a region with no edge mesh gets null and a printable
## sentence, never a substitute.
##
## WHY "DIFFERENT MESHES" IS ASSERTED AND NOT ASSUMED. The failure this guards
## against does not look like a crash. If `edge` were accidentally bound to the
## border model, every check about counts and coverage would still pass and the
## screen would draw the same strip twice, which is the exact confusion the whole
## exercise exists to end. So the vertex counts and bounds of the three layers
## are compared per region and required to differ.
##
## THE STALE-ASSET TRAP. `art/w3d/lm/lmr_seledge.w3d` is shipped, is named like a
## selection edge, and is NOT one: no effect references LMR_SelEdge, RotWK ships
## no override for it (the catalog resolves it to the BFME2 layer and the two
## layers are byte-identical), and its meshes are BFME2's region set - it carries
## ARNOR/BUCKLAND/GONDOR/OSGILIATH/THE_DEAD_MARSHE, which are not RotWK regions,
## and covers only 33 of 52. The converter records that as a NAMED GAP and this
## runner asserts the sentence survives into the bundle, so a later lane cannot
## "fix" the missing selection art by reaching for the wrong file.
##
## LIVENESS. The expected check count is exact, not a floor, so a check that
## stops running fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script res://tests/wotr_region_geometry_runner.gd
## With no bundle only the no-bundle half runs; that is the 10/10 mode.

const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## RETAIL'S OWN MESH COUNTS, measured off the shipped RotWK models rather than
## chosen. Asserted by VALUE so a converter that starts dropping meshes fails
## here rather than drawing a thinner Middle-earth that still photographs.
##
##   lmr_fill.w3d       62 = 52 regions + 10 IMPASSABLE masses
##   lmr_border.w3d     52 = one outline per region
##   lmr_edge.w3d       54 = 52 regions + 1 IMPASSABLE + SHAPE32 (see below)
##   lmr_highlight.w3d  52 = one glow per region
##   lmr_regfill.w3d     7 = the seven territory groups
const RETAIL_LAYER_MESHES := {
	"fill": 62, "border": 52, "territory": 7, "edge": 54, "highlight": 52,
}
## Every one of the 52 playable RotWK regions must be drawable in all four
## per-region layers. Not "most": a hole here is a region that goes dark when
## selected, and the point of converting the edge layer was to stop guessing.
const RETAIL_REGIONS_PER_LAYER := 52
## The four per-region layers, in the order the effects are declared.
const PER_REGION_LAYERS := ["fill", "border", "edge", "highlight"]
## The one mesh in `lmr_edge.w3d` that matches no region and is not IMPASSABLE.
## Named by VALUE so a second stray cannot hide behind a tolerance. It is an
## authoring leftover - a Max shape name - and it is bound to nothing.
const RETAIL_EDGE_STRAY_MESH := "SHAPE32"
## The gap the CONVERTER names about `lmr_seledge.w3d`, asserted by name.
const SELEDGE_GAP_NAME := "SELEDGE_IS_STALE_BFME2_GEOMETRY"
## Layers that must NOT be in the bundle. `seledge` is convertible on request for
## measurement, and shipping it would put BFME2-era outlines on 33 RotWK regions.
const REFUSED_LAYERS := ["seledge"]

## A region every RotWK campaign carries, used as the worked example for the
## "these are different shapes" comparison. Picked because it is one of the
## regions whose `subObject` id equals its region id, so a failure here is about
## geometry and not about the Rhudaur/Rhudaul spelling bridge.
const SAMPLE_REGION := "Mordor"
## The region retail spells one way and names its mesh another. The bundle must
## carry all four layers for it THROUGH the document's `subObject` link, because
## nothing else bridges `Rhudaur` to `RHUDAUL`.
const SPELLING_BRIDGE_REGION := "Rhudaur"

## LIVENESS.
const CHECKS_WITH_BUNDLE := 59
const CHECKS_WITHOUT_BUNDLE := 10

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING REGION GEOMETRY ==")
	var geometry = RegionGeometryScript.new()
	var located: Dictionary = geometry.locate_and_load([])
	var bound := bool(located.get("ok", false))
	print("bundle: %s" % (String(located["root"]) if bound
		else "NONE - " + String(located["reason"]).split("\n")[0]))

	_check_an_absent_bundle_names_every_path_it_tried(geometry, located, bound)
	if bound:
		for line in geometry.describe_load():
			print("  source: %s" % line)
		_check_the_bundle_carries_retails_own_five_layers(geometry)
		_check_every_region_is_drawable_in_every_per_region_layer(geometry)
		_check_selection_and_home_are_different_shapes_from_the_border(geometry)
		_check_the_effect_accessors_return_the_layer_retail_names(geometry)
		_check_a_region_with_no_mesh_gets_null_and_never_a_substitute(geometry)
		_check_the_spelling_bridge_carries_every_layer(geometry)
		_check_the_stale_seledge_asset_is_named_and_not_shipped(geometry)
		_check_the_gap_sentences_are_empty_only_when_the_art_is_there(geometry)

	var expected := CHECKS_WITH_BUNDLE if bound else CHECKS_WITHOUT_BUNDLE
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, expected, _passed, _failed])
	if total != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			total, expected])
		quit(1)
		return
	quit(1 if _failed > 0 else 0)


# --- the checks -----------------------------------------------------------------

## An absent bundle must produce a REASON naming every path tried and the exact
## command that makes one, not a bare false. This runs in both modes because a
## loader that stops explaining itself is a regression whether or not the machine
## happens to have the data.
func _check_an_absent_bundle_names_every_path_it_tried(
	geometry, located: Dictionary, bound: bool
) -> void:
	var missing = RegionGeometryScript.new()
	var refused: bool = missing.load_from("res://tests/does-not-exist")
	_check("a_missing_bundle_is_refused_rather_than_half_loaded",
		not refused and not missing.loaded and missing.errors.size() > 0,
		"errors: %d" % missing.errors.size())

	var reason := String(located.get("reason", ""))
	if bound:
		_check("a_bound_bundle_reports_no_reason", reason.is_empty())
		_check("a_bound_bundle_names_its_root", not String(located["root"]).is_empty())
	else:
		_check("an_absent_bundle_reports_a_reason", not reason.is_empty())
		_check("the_reason_names_the_converter_command",
			reason.contains("openbfme_importer.livingmap_regions"))

	_check("the_search_records_every_root_it_tried",
		geometry.searched_roots.size() >= 1,
		"%d roots" % geometry.searched_roots.size())

	# The gap accessors must be safe to call on a loader that never loaded, and
	# must SAY SO rather than returning "" (which a caller would read as "the art
	# is present and complete").
	_check("selection_gap_on_an_unloaded_loader_says_no_bundle",
		missing.selection_geometry_gap().contains("no region-geometry bundle"))
	_check("home_gap_on_an_unloaded_loader_says_no_bundle",
		missing.home_highlight_gap().contains("no region-geometry bundle"))
	_check("an_unloaded_loader_hands_out_no_selection_mesh",
		missing.selection_mesh(SAMPLE_REGION) == null)
	_check("an_unloaded_loader_hands_out_no_home_mesh",
		missing.home_highlight_mesh(SAMPLE_REGION) == null)
	_check("an_unloaded_loader_claims_no_layers",
		missing.layers_present.is_empty() and not missing.has_layer("edge"))
	_check("an_unloaded_loader_counts_no_regions_in_a_layer",
		missing.layer_region_count("edge") == 0)


## Retail's five models, at retail's own mesh counts.
func _check_the_bundle_carries_retails_own_five_layers(geometry) -> void:
	for layer in RETAIL_LAYER_MESHES.keys():
		_check("the_bundle_carries_the_%s_layer" % layer, geometry.has_layer(layer))

	var by_layer: Dictionary = {}
	for row in geometry.records:
		var layer := String((row as Dictionary).get("layer", ""))
		by_layer[layer] = int(by_layer.get(layer, 0)) + 1
	for layer in RETAIL_LAYER_MESHES.keys():
		_check("the_%s_layer_carries_retails_own_%d_meshes" % [
			layer, int(RETAIL_LAYER_MESHES[layer])],
			int(by_layer.get(layer, 0)) == int(RETAIL_LAYER_MESHES[layer]),
			"%d meshes" % int(by_layer.get(layer, 0)))

	for layer in REFUSED_LAYERS:
		_check("the_bundle_does_not_ship_the_%s_layer" % layer,
			not geometry.has_layer(layer))


## Every region drawable in every per-region layer. Counted from the meshes that
## LOADED, not from the manifest's claim.
func _check_every_region_is_drawable_in_every_per_region_layer(geometry) -> void:
	for layer in PER_REGION_LAYERS:
		var count: int = geometry.layer_region_count(layer)
		_check("all_%d_regions_are_drawable_in_the_%s_layer" % [
			RETAIL_REGIONS_PER_LAYER, layer],
			count == RETAIL_REGIONS_PER_LAYER, "%d regions" % count)
		var holes: PackedStringArray = geometry.regions_without_layer(layer)
		_check("no_drawn_region_is_missing_its_%s_mesh" % layer,
			holes.is_empty(), "holes: %s" % ", ".join(Array(holes)))

	# The one authoring leftover in lmr_edge.w3d is bound to no region, by name.
	var strays: Array[String] = []
	for row in geometry.records:
		var record := row as Dictionary
		if String(record.get("layer", "")) != "edge":
			continue
		if record.get("regionId", null) == null and not bool(record.get("impassable", false)):
			strays.append(String(record.get("mesh", "?")))
	strays.sort()
	_check("the_only_unbound_edge_mesh_is_retails_own_%s" % RETAIL_EDGE_STRAY_MESH,
		strays == [RETAIL_EDGE_STRAY_MESH], "strays: %s" % ", ".join(strays))


## THE CHECK THAT MATTERS MOST. If `edge` or `highlight` were bound to the border
## model, everything above would still pass and the screen would draw the same
## strip under three names. Retail authored three different pieces of art, so
## they must differ in vertex count or in bounds for a real region.
func _check_selection_and_home_are_different_shapes_from_the_border(geometry) -> void:
	var border: ArrayMesh = geometry.region_mesh(SAMPLE_REGION, "border")
	var edge: ArrayMesh = geometry.selection_mesh(SAMPLE_REGION)
	var highlight: ArrayMesh = geometry.home_highlight_mesh(SAMPLE_REGION)
	_check("the_sample_region_carries_all_three_marks",
		border != null and edge != null and highlight != null)
	if border == null or edge == null or highlight == null:
		# Not a skip: the three checks below are replaced by three failures so the
		# LIVENESS total stays exact.
		_check("selection_differs_from_border", false, "a mesh was missing")
		_check("home_highlight_differs_from_border", false, "a mesh was missing")
		_check("selection_differs_from_home_highlight", false, "a mesh was missing")
		return
	_check("selection_differs_from_border",
		not _same_shape(border, edge),
		"border %d verts, edge %d verts" % [
			_vertex_count(border), _vertex_count(edge)])
	_check("home_highlight_differs_from_border",
		not _same_shape(border, highlight),
		"border %d verts, highlight %d verts" % [
			_vertex_count(border), _vertex_count(highlight)])
	_check("selection_differs_from_home_highlight",
		not _same_shape(edge, highlight),
		"edge %d verts, highlight %d verts" % [
			_vertex_count(edge), _vertex_count(highlight)])


## The named accessors must hand out the layer RETAIL names for that effect, and
## must be the same object `region_mesh` returns for it - no second decode, no
## quietly different mesh behind a friendlier name.
func _check_the_effect_accessors_return_the_layer_retail_names(geometry) -> void:
	_check("selection_layer_is_retails_LMR_Edge_layer",
		RegionGeometryScript.SELECTION_LAYER == "edge")
	_check("home_highlight_layer_is_retails_LMR_Highlight_layer",
		RegionGeometryScript.HOME_HIGHLIGHT_LAYER == "highlight")
	_check("selection_mesh_is_the_edge_layer_mesh",
		geometry.selection_mesh(SAMPLE_REGION)
			== geometry.region_mesh(SAMPLE_REGION, "edge"))
	_check("home_highlight_mesh_is_the_highlight_layer_mesh",
		geometry.home_highlight_mesh(SAMPLE_REGION)
			== geometry.region_mesh(SAMPLE_REGION, "highlight"))


## A region the bundle knows nothing about gets NULL from every accessor. The
## caller draws nothing; it never receives a neighbour's shape.
func _check_a_region_with_no_mesh_gets_null_and_never_a_substitute(geometry) -> void:
	var absent := "Region_1"
	_check("a_region_with_no_geometry_is_not_claimed",
		not geometry.has_region(absent))
	_check("a_region_with_no_geometry_gets_no_selection_mesh",
		geometry.selection_mesh(absent) == null)
	_check("a_region_with_no_geometry_gets_no_home_mesh",
		geometry.home_highlight_mesh(absent) == null)
	_check("the_document_holes_are_reported_by_name",
		geometry.regions_without_geometry.has(absent),
		"%d regions without geometry" % geometry.regions_without_geometry.size())


## `Rhudaur` is spelled `RHUDAUL` in every one of retail's models. Only the
## document's own `subObject` link is allowed to bridge that, and it must bridge
## it for the NEW layers too, not just for fill and border.
func _check_the_spelling_bridge_carries_every_layer(geometry) -> void:
	for layer in PER_REGION_LAYERS:
		_check("the_spelling_bridge_carries_the_%s_layer" % layer,
			geometry.region_mesh(SPELLING_BRIDGE_REGION, layer) != null)


## The stale asset must be NAMED in the bundle and ABSENT from it. Both halves
## matter: naming it without refusing it would ship BFME2 outlines, and refusing
## it without naming it would leave the next lane to rediscover the trap.
func _check_the_stale_seledge_asset_is_named_and_not_shipped(geometry) -> void:
	_check("the_converter_named_the_seledge_gap",
		geometry.named_gaps.has(SELEDGE_GAP_NAME),
		"gaps: %s" % ", ".join(geometry.named_gaps.keys()))
	var text := String(geometry.named_gaps.get(SELEDGE_GAP_NAME, ""))
	_check("the_seledge_gap_names_the_file_and_the_reason",
		text.contains("lmr_seledge.w3d") and text.contains("LMR_SelEdge")
			and text.contains("BFME2"))
	_check("the_seledge_gap_points_at_the_mesh_that_IS_right",
		text.contains("lmr_edge.w3d"))
	_check("the_named_gaps_render_as_printable_lines",
		geometry.named_gap_lines().size() == geometry.named_gaps.size()
			and geometry.named_gap_lines().size() > 0)
	for source in geometry.source_summary:
		var path := String((source as Dictionary).get("virtualPath", ""))
		_check("the_shipped_source_%s_is_not_the_stale_asset" % path.get_file(),
			not path.contains("seledge"))


## The gap sentences are the contract with the caller: empty means "draw retail's
## art for every region", non-empty means "say this and draw nothing". They must
## be empty ONLY when the art is genuinely complete.
func _check_the_gap_sentences_are_empty_only_when_the_art_is_there(geometry) -> void:
	_check("selection_geometry_reports_no_gap_when_all_52_are_present",
		geometry.selection_geometry_gap().is_empty(),
		geometry.selection_geometry_gap())
	_check("home_highlight_reports_no_gap_when_all_52_are_present",
		geometry.home_highlight_gap().is_empty(),
		geometry.home_highlight_gap())

	# And a loader that carries the regions but NOT the layer must say so by
	# name. Built by loading the same bundle and dropping the edge layer, so the
	# sentence is exercised rather than merely written.
	var stripped = RegionGeometryScript.new()
	stripped.load_from(geometry.bundle_root)
	stripped.layers_present = PackedStringArray(["fill", "border", "territory"])
	var gap: String = stripped.selection_geometry_gap()
	_check("a_bundle_without_the_edge_layer_names_the_missing_layer",
		gap.contains("lmr_edge.w3d") and gap.contains("no edge layer"), gap)
	_check("that_sentence_promises_no_substitute",
		gap.contains("no substitute is invented"), gap)


# --- helpers ---------------------------------------------------------------------

func _vertex_count(mesh: ArrayMesh) -> int:
	return (mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()


## Two meshes are "the same shape" only if they have the same vertex count AND
## the same axis-aligned bounds. Deliberately strict in the direction that makes
## a FALSE PASS hard: near-identical art would still be reported as different, so
## this check can only fail when the two really are the same model.
func _same_shape(a: ArrayMesh, b: ArrayMesh) -> bool:
	if _vertex_count(a) != _vertex_count(b):
		return false
	var box_a := a.get_aabb()
	var box_b := b.get_aabb()
	return box_a.position.is_equal_approx(box_b.position) \
		and box_a.size.is_equal_approx(box_b.size)


func _check(what: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
	else:
		_failed += 1
		print("  FAIL  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
