extends SceneTree
## Placement ghost visuals (owner playtest 2026-08-18, queue Q36).
##
## Retail authors NO placement outline; the only ground decal while placing a
## farm is the TerrainResourceClientBehavior claim area (farm.ini
## ModuleTag_NewMoney / Radius = GONDOR_FARM_MONEY_RANGE) - a translucent
## white disc. Validity is the ghost itself: its own colours at ~50% when the
## site is valid, red-tinted when it is not. This runner boots the RotWK Men
## skirmish slice, arms farm construction with a builder selected, and asserts
## that shape on the spawned ghost node tree.
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/placement_ghost_visuals_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const BOOT_DEADLINE_MS := 240000
var _runner_watchdog := RunnerWatchdogScript.new()
const ROTWK_SKIRMISH_MAP := "rotwk.map.adorn-river"

var _passed := 0
var _failed := 0


func _init() -> void:
	_runner_watchdog.start(self, "PLACEMENT_GHOST_RUNNER", 480000)
	await process_frame
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", ROTWK_SKIRMISH_MAP)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("scene_loads", scene != null, "vertical slice scene did not load"):
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("slice_ready", bool(slice.ready_ok), "failure=%s" % String(slice.failure_reason)):
		_finish()
		return
	var simulation = slice.simulation
	# Select a builder (the arm path requires a selection with a build rule).
	var builder_id := -1
	for id in simulation.entity_ids():
		if bool(simulation.entity(id).get("is_builder", false)) and int(simulation.entity(id).get("team", -1)) == simulation.PLAYER_TEAM:
			builder_id = int(id)
			break
	if not _check("player_builder_present", builder_id > 0, "no player builder in the skirmish start"):
		_finish()
		return
	var selection: Array[int] = [builder_id]
	simulation.selected_ids = selection
	slice.call("_arm_construction", "farm")
	await process_frame
	await process_frame
	var ghost: Node = slice.get("construction_ghost")
	if not _check("ghost_spawned", ghost != null, "construction_ghost is null after arming farm"):
		_finish()
		return
	var names: Array[String] = []
	for child in ghost.get_children():
		names.append(String(child.name))
	_check("no_footprint_outline", not names.has("FootprintCircle"), "children=%s" % str(names))
	var disc := ghost.get_node_or_null("EffectivenessRing") as MeshInstance3D
	_check("farm_claim_disc_present", disc != null, "children=%s" % str(names))
	if disc != null:
		var material := disc.material_override as StandardMaterial3D
		var color := material.albedo_color if material != null else Color.BLACK
		_check(
			"claim_disc_is_translucent_white",
			material != null and is_equal_approx(color.r, 1.0) and is_equal_approx(color.g, 1.0)
			and is_equal_approx(color.b, 1.0) and color.a > 0.15 and color.a < 0.5,
			"albedo=%s" % str(color)
		)
		var arrays := (disc.mesh as ArrayMesh).surface_get_arrays(0) if disc.mesh is ArrayMesh else []
		var vertex_count := int((arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()) if not arrays.is_empty() else 0
		# A filled disc fan: 3 vertices per segment, first vertex of each
		# triangle at the origin. A ring would carry 6 per segment, none at 0.
		var has_center := false
		if vertex_count > 0:
			for v in (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array):
				if v.is_zero_approx():
					has_center = true
					break
		_check("claim_area_is_filled_disc_not_ring", has_center and vertex_count % 3 == 0, "vertices=%d center=%s" % [vertex_count, str(has_center)])
	# Ghost model surfaces: translucent (~50%), not green.
	var ghost_model := ghost.get_node_or_null("GhostModel")
	_check("ghost_model_present", ghost_model != null, "children=%s" % str(names))
	if ghost_model != null:
		var green_hits := 0
		var opaque_hits := 0
		var surfaces := 0
		var stack: Array = [ghost_model]
		while not stack.is_empty():
			var node = stack.pop_back()
			if node is MeshInstance3D:
				var mi := node as MeshInstance3D
				var count := mi.mesh.get_surface_count() if mi.mesh != null else 0
				for i in count:
					var m := mi.get_active_material(i)
					if m is BaseMaterial3D:
						surfaces += 1
						var c := (m as BaseMaterial3D).albedo_color
						if c.g > 0.7 and c.r < 0.5 and c.b < 0.5:
							green_hits += 1
						if c.a > 0.95:
							opaque_hits += 1
			for c in node.get_children():
				stack.append(c)
		_check("ghost_surfaces_found", surfaces > 0, "surfaces=%d" % surfaces)
		_check("ghost_has_no_green_tint", green_hits == 0, "green_surfaces=%d/%d" % [green_hits, surfaces])
		_check("ghost_is_translucent", opaque_hits == 0, "opaque_surfaces=%d/%d" % [opaque_hits, surfaces])
	root.remove_child(slice)
	slice.free()
	await process_frame
	_finish()


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		_passed += 1
		print("PLACEMENT_GHOST PASS %s" % name)
	else:
		_failed += 1
		print("PLACEMENT_GHOST FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("PLACEMENT_GHOST_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
