extends SceneTree
## Fortress build-plot presentation gate (user bugs #4 and #9).
##
## RETAIL TRUTH (pure retail object INI, quoted through the compiled pack
## documents this slice actually loads):
##   * A fortress unpacks its CastleBehavior pieces once. The expansion plots
##     are the `*FortressExpansionPadCorner` / `*FortressExpansionPadSide`
##     objects — one object per authored plot slot, each with its own
##     W3DModelDraw body plus a W3DFloorDraw bib. Retail therefore renders
##     EXACTLY ONE plot plate per slot; a second engine-spawned copy of the
##     same plate is ours, not retail's.
##   * Those pad objects carry `ImmortalBody` (MaxHealth 15000), the only
##     ImmortalBody structures in every published faction pack. An ImmortalBody
##     can never lose health, so retail never floats a health bar over an empty
##     build plot. A damageable structure (StructureBody/ActiveBody) does.
##
## Both rows are asserted against the LIVE slice scene, not against a summary:
## the runner counts the plot geometry actually rendered at each authored pad
## position and reads the health bar nodes' visibility off the scene tree.
##
## Run:
##   Godot --headless --path game --script res://tests/fortress_plot_presentation_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

## Synthetic (non-model) meshes the presentation attaches to structures and
## markers. They are chrome, never plot geometry, so they never count as a
## rendered plot plate.
const SYNTHETIC_MESH_NAMES: Array[String] = [
	"PadRing",
	"SelectionRing",
	"HealthBack",
	"HealthFill",
	"BuildBack",
	"BuildFill",
]
## World-unit tolerance for "this node sits on that pad". Both the castle-piece
## structure node and the pad marker are placed at the pad centre exactly.
const PAD_MATCH_RADIUS := 0.5

var passed := 0
var failed := 0
var _watchdog := RunnerWatchdogScript.new()
var _slice = null
var _content_db = null


func _initialize() -> void:
	_watchdog.start(self, "FORTRESS_PLOT_PRESENTATION", 900_000)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1280, 720)
	var content_db = root.get_node_or_null("/root/ContentDB")
	_content_db = content_db
	if content_db != null and (content_db.pack_roots as Array).is_empty():
		var workspace := ProjectSettings.globalize_path("res://../workspace/content-packs")
		OS.set_environment("OPENBFME_CONTENT", workspace)
		content_db.reload()
	_check("content_roots_loaded", content_db != null and not (content_db.pack_roots as Array).is_empty())

	_test_immortal_body_is_exactly_the_build_plots(content_db)

	# The doubled plates were reported against the DWARVEN fortress; every
	# faction unpacks the same CastleBehavior shape, so this runner is meant to
	# be run per faction through the slice's own OPENBFME_SLICE_FACTION seam
	# (unset = the host men pack).
	var faction := OS.get_environment("OPENBFME_SLICE_FACTION")
	print("FORTRESS_PLOT_PRESENTATION faction=%s" % ("men (default)" if faction == "" else faction))

	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	_slice = packed.instantiate()
	root.add_child(_slice)
	await process_frame
	await process_frame
	_check("slice_ready", bool(_slice.ready_ok), String(_slice.failure_reason))
	if not bool(_slice.ready_ok):
		_finish()
		return
	_slice._sync_presentation()
	await process_frame

	await _test_plot_presentation()
	_finish()


## Oracle guard for the health-bar gate: the gate keys on the compiled body
## module, so ImmortalBody must mean "build plot" in the loaded content. If a
## future pack gives some other structure an ImmortalBody this row goes red
## before the gate can silently hide that structure's bar.
func _test_immortal_body_is_exactly_the_build_plots(content_db) -> void:
	if content_db == null:
		return
	var immortal: Array[String] = []
	var non_plot: Array[String] = []
	for source_id_value in content_db.get_playable_structure_runtimes().keys():
		var document: Dictionary = content_db.get_playable_structure_runtime(String(source_id_value))
		var primary: Variant = (
			((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
			.get("health", {}) as Dictionary
		).get("primary", {})
		if typeof(primary) != TYPE_DICTIONARY:
			continue
		if String((primary as Dictionary).get("module", "")) != "ImmortalBody":
			continue
		var object_id := String(document.get("objectId", source_id_value))
		immortal.append(object_id)
		if not object_id.to_lower().contains("expansionpad"):
			non_plot.append(object_id)
	_check("immortal_body_structures_exist", not immortal.is_empty(), str(immortal))
	_check("immortal_body_is_only_build_plots", non_plot.is_empty(), str(non_plot))


func _test_plot_presentation() -> void:
	var simulation = _slice.simulation
	var fortress_id := 0
	var pads: Array = []
	for fortress_id_value in simulation.expansion_pads.keys():
		var candidate := int(fortress_id_value)
		var candidate_pads: Array = simulation.expansion_pad_states(candidate)
		if candidate_pads.is_empty():
			continue
		if int(simulation.structure(candidate).get("team", -1)) != 0:
			continue
		fortress_id = candidate
		pads = candidate_pads
		break
	_check("player_fortress_has_expansion_pads", fortress_id != 0 and not pads.is_empty(), "fortress=%d pads=%d" % [fortress_id, pads.size()])
	if fortress_id == 0 or pads.is_empty():
		return
	# Retail plots come from CastleBehavior pieces, never from an inferred ring.
	var castle_backed := true
	for pad_value in pads:
		if int((pad_value as Dictionary).get("castle_piece_structure_id", 0)) == 0:
			castle_backed = false
	_check("pads_are_castle_behavior_pieces", castle_backed, str(pads))
	# The authored slot count is retail's own castle template (the compiled
	# `bases/fortress_*/fortress_*.bse` rows), e.g. Fortress_Dwarven = 4 corner
	# pads + 2 side pads + 1 citadel. The live sim must seed exactly the pad
	# rows that template carries — no inferred ring, no dropped slot.
	var authored_pads := _authored_pad_count(int(simulation.structure(fortress_id).get("team", -1)))
	_check(
		"pad_count_equals_the_retail_castle_template",
		authored_pads > 0 and pads.size() == authored_pads,
		"live=%d authored=%d" % [pads.size(), authored_pads]
	)

	var doubled: Array[String] = []
	var missing: Array[String] = []
	var barred: Array[String] = []
	for index in pads.size():
		var pad: Dictionary = pads[index]
		if int(pad.get("expansion_structure_id", 0)) != 0:
			continue
		var center := Vector2(pad.get("position", Vector2.ZERO))
		var providers := _plot_geometry_providers(center)
		if providers.size() > 1:
			doubled.append("pad%d:%s" % [index, str(providers)])
		elif providers.is_empty():
			missing.append("pad%d" % index)
		var piece_id := int(pad.get("castle_piece_structure_id", 0))
		var piece_node = _slice.structure_nodes.get(piece_id)
		if piece_node != null and _health_bar_visible(piece_node):
			barred.append("pad%d:structure%d" % [index, piece_id])
	# BUG #4: two overlapping plot plates per slot (castle-piece structure plus
	# the engine-spawned pad marker re-instancing the same bib model).
	_check("one_plot_visual_per_free_pad", doubled.is_empty(), str(doubled))
	# The same bug stated for every pad at once, including pads whose marker the
	# old code never got around to creating: the highlight layer must carry no
	# converted model geometry anywhere.
	var model_bearing_markers: Array[String] = []
	var marker_nodes: Array[Node] = []
	_collect_named(_slice, "ExpansionPadMarker", marker_nodes)
	for marker in marker_nodes:
		if _renders_model_geometry(marker):
			model_bearing_markers.append(str((marker as Node3D).global_position))
	_check("pad_markers_carry_no_plot_model", model_bearing_markers.is_empty(), str(model_bearing_markers))
	_check("every_free_pad_still_renders_its_plot", missing.is_empty(), str(missing))
	# BUG #9: health bars floating over empty plots.
	_check("empty_plot_has_no_health_bar", barred.is_empty(), str(barred))

	# Control rows: the gate must not swallow damageable structures' bars.
	var fortress_node = _slice.structure_nodes.get(fortress_id)
	_check(
		"full_health_unselected_fortress_hides_its_health_bar",
		fortress_node != null and not _health_bar_visible(fortress_node),
		"node=%s" % str(fortress_node)
	)
	if fortress_node != null:
		_test_damage_or_selection_health_bar_visibility(fortress_node, simulation.structure(fortress_id))
		_test_health_bar_geometry_is_footprint_bounded_and_billboarded()
	await _test_occupied_plot_shows_a_bar(fortress_id)


## An expansion raised on a plot is a damageable structure and DOES carry a
## health bar. Same gate, opposite side.
func _test_occupied_plot_shows_a_bar(fortress_id: int) -> void:
	var simulation = _slice.simulation
	var kinds: Array = simulation.expansion_commands_for(fortress_id)
	_check("fortress_offers_an_expansion", not kinds.is_empty(), str(kinds))
	if kinds.is_empty():
		return
	simulation.team_resources[0] = simulation.resources_for_team(0) + 20000
	var result: Dictionary = simulation.issue_expansion_construct(0, fortress_id, String(kinds[0]))
	_check("expansion_construct_accepted", bool(result.get("ok", false)), str(result))
	if not bool(result.get("ok", false)):
		return
	var expansion_id := int(result.get("structure_id", 0))
	_slice.step_for_test(int(result.get("build_ticks", 1)) + 2)
	_slice._sync_presentation()
	await process_frame
	await process_frame
	var expansion_node = _slice.structure_nodes.get(expansion_id)
	_check(
		"full_health_unselected_expansion_hides_its_health_bar",
		expansion_node != null and not _health_bar_visible(expansion_node),
		"node=%s progress=%s" % [str(expansion_node), str(simulation.structure(expansion_id).get("construction_progress", -1.0))]
	)
	if expansion_node != null:
		expansion_node.set_selected(true)
		expansion_node.sync_state(simulation.structure(expansion_id))
		_check("selected_expansion_shows_its_health_bar", _health_bar_visible(expansion_node))


## Expansion-pad rows in the fortress document's compiled CastleBehavior — the
## retail `.bse` castle template, read through the same manifest the slice uses
## to resolve this team's fortress.
func _authored_pad_count(team: int) -> int:
	var sources: Variant = _slice.simulation.structure_source_object_ids_for_team(team).get("fortress", [])
	var fortress_source := ""
	if typeof(sources) == TYPE_ARRAY and not (sources as Array).is_empty():
		fortress_source = String((sources as Array)[0])
	elif typeof(sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		fortress_source = String(sources)
	if fortress_source == "" or _content_db == null:
		return 0
	var document: Dictionary = _content_db.get_playable_structure_runtime(fortress_source)
	var castle: Dictionary = (
		((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
		.get("castleBehavior", {}) as Dictionary
	)
	var count := 0
	for piece_value in Array(castle.get("pieces", [])):
		if String((piece_value as Dictionary).get("objectId", "")).to_lower().contains("expansionpad"):
			count += 1
	return count


## Every distinct node that renders real (non-chrome) geometry at this pad
## centre: the castle-piece structure node, plus any pad marker the
## presentation spawns on top of it.
func _plot_geometry_providers(center: Vector2) -> Array[String]:
	var providers: Array[String] = []
	var candidates: Array[Node] = []
	for node_value in _slice.structure_nodes.values():
		candidates.append(node_value as Node)
	_collect_named(_slice, "ExpansionPadMarker", candidates)
	for candidate in candidates:
		var node3d := candidate as Node3D
		if node3d == null or not node3d.is_inside_tree() or not node3d.is_visible_in_tree():
			continue
		var here := Vector2(node3d.global_position.x, node3d.global_position.z)
		if here.distance_to(center) > PAD_MATCH_RADIUS:
			continue
		if _renders_model_geometry(node3d):
			providers.append(node3d.name)
	providers.sort()
	return providers


func _collect_named(root_node: Node, node_name: String, into: Array[Node]) -> void:
	if root_node.name == node_name:
		into.append(root_node)
	for child in root_node.get_children():
		_collect_named(child, node_name, into)


func _renders_model_geometry(node: Node) -> bool:
	var mesh := node as MeshInstance3D
	if mesh != null and mesh.mesh != null and mesh.is_visible_in_tree() and not SYNTHETIC_MESH_NAMES.has(String(mesh.name)):
		return true
	for child in node.get_children():
		if _renders_model_geometry(child):
			return true
	return false


func _health_bar_visible(structure_node: Node) -> bool:
	for bar_name in ["HealthBack", "HealthFill"]:
		var bar := structure_node.get_node_or_null(NodePath(bar_name)) as Node3D
		if bar != null and bar.is_visible_in_tree():
			return true
	return false


func _test_damage_or_selection_health_bar_visibility(structure_node, entity: Dictionary) -> void:
	structure_node.set_selected(true)
	structure_node.sync_state(entity)
	_check("selected_full_health_fortress_shows_its_health_bar", _health_bar_visible(structure_node))
	structure_node.set_selected(false)
	var damaged := entity.duplicate(true)
	damaged["health"] = maxi(1, int(entity.get("maximum_health", 1)) - 1)
	structure_node.sync_state(damaged)
	_check("damaged_unselected_fortress_shows_its_health_bar", _health_bar_visible(structure_node))
	structure_node.sync_state(entity)


func _test_health_bar_geometry_is_footprint_bounded_and_billboarded() -> void:
	var footprint_mismatches: Array[String] = []
	var roof_mismatches: Array[String] = []
	var non_billboarded: Array[String] = []
	for structure_value in _slice.structure_nodes.values():
		var structure = structure_value
		if structure == null or not structure.draws_health_bar():
			continue
		var back := structure.get_node_or_null("HealthBack") as Sprite3D
		if back == null:
			footprint_mismatches.append("%s:no-fixed-pixel-sprite" % structure.name)
			continue
		var width := float(structure._health_bar_width)
		var footprint_width := float(structure.pick_radius) * 2.0
		if absf(width - footprint_width) > 0.02:
			footprint_mismatches.append("%s:width=%.3f footprint=%.3f source=%s" % [
				structure.name, width, footprint_width, String(structure.selection_radius_source)
			])
		var intact_body := structure._active_body as Node3D
		if intact_body != null:
			# Production records the transformed intact visual AABB top directly;
			# the contract target height is not the rendered top when source-unit
			# scale is authoritative in the selected pack.
			var roof_y := float(structure._visual_top_y)
			var bar_top := back.position.y
			if bar_top < roof_y or bar_top - roof_y > 0.20:
				roof_mismatches.append("%s:roof=%.3f bar_top=%.3f gap=%.3f" % [
					structure.name, roof_y, bar_top, bar_top - roof_y
				])
		else:
			roof_mismatches.append("%s:no-active-body" % structure.name)
		if back.billboard != BaseMaterial3D.BILLBOARD_ENABLED or not back.fixed_size:
			non_billboarded.append(structure.name)
	_check("health_bars_match_each_structure_footprint", footprint_mismatches.is_empty(), str(footprint_mismatches))
	_check("health_bar_top_hugs_measured_visual_roof", roof_mismatches.is_empty(), str(roof_mismatches))
	_check("health_bars_are_fixed_pixel_screen_facing_billboards", non_billboarded.is_empty(), str(non_billboarded))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s %s" % [name, detail])


func _finish() -> void:
	print("FORTRESS_PLOT_PRESENTATION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
