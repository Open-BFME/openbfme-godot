extends SceneTree
## Exercises the real snapshot/view boundary without retail or donor assets.

var passed: int = 0
var failed: int = 0
var owned_nodes: Array[Node] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	_test_playable_scene_boot()
	_test_interpolated_member_motion()
	_test_presented_render_schedules()
	for node in owned_nodes:
		if is_instance_valid(node):
			node.free()
	owned_nodes.clear()
	await process_frame
	await process_frame
	if failed == 0:
		print("STAGE1_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE1_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])

func _test_playable_scene_boot() -> void:
	var packed := load("res://scenes/stage1_arena.tscn") as PackedScene
	_check("playable_scene_loads", packed != null)
	if packed == null:
		return
	var arena := packed.instantiate()
	arena.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(arena)
	owned_nodes.append(arena)
	var world: Stage1World = arena.get("world")
	var view: Node = arena.get_node_or_null("Stage1View")
	var camera: Camera3D = arena.get_node_or_null("CameraRig/Camera3D")
	var hud: Node = arena.get_node_or_null("Stage1Hud")
	_check("playable_scene_world_ready", world != null and world.hordes.size() == 2)
	_check("playable_scene_real_members", world != null and world.living_member_count(Stage1Types.Team.BLUE) == 15 and world.living_member_count(Stage1Types.Team.RED) == 15)
	_check("playable_scene_camera_ready", camera != null and camera.current)
	_check("playable_scene_hud_ready", hud != null and hud.get("selection_box") != null)
	_check("playable_scene_decorative_hud_ignores_mouse", _controls_ignore_mouse(hud.get_node("Root/TopPanel")) and _controls_ignore_mouse(hud.get_node("Root/SideStrip")))
	if world != null and view != null:
		var no_selection: Array[int] = []
		view.call("render_snapshot", world.snapshot(), 1.0, no_selection)
		var melee: Array = view.get("_melee_meshes")
		var rendered := 0
		for mesh: MultiMeshInstance3D in melee:
			rendered += mesh.multimesh.instance_count
		_check("playable_scene_member_instances", rendered > 0, "melee_instances=%d" % rendered)
		var blue_horde: Dictionary = world.snapshot().hordes[0]
		var blue_member: Dictionary = blue_horde.members[0]
		var member_world: Vector3 = view.call("fixed_to_world", blue_member.position, 0.35)
		var member_screen := camera.unproject_position(member_world) + Vector2(48.0, 0.0)
		var press := InputEventMouseButton.new()
		press.button_index = MOUSE_BUTTON_LEFT
		press.pressed = true
		press.position = member_screen
		arena.call("_unhandled_input", press)
		var release := InputEventMouseButton.new()
		release.button_index = MOUSE_BUTTON_LEFT
		release.pressed = false
		release.position = member_screen
		arena.call("_unhandled_input", release)
		var selected: Array = arena.get("selected_ids")
		_check("playable_scene_forgiving_click_selection", selected.has(int(blue_horde.id)), "selected=%s" % str(selected))
		arena.set("selected_ids", [])
		var short_drag_press := InputEventMouseButton.new()
		short_drag_press.button_index = MOUSE_BUTTON_LEFT
		short_drag_press.pressed = true
		short_drag_press.position = member_screen - Vector2(5.0, 0.0)
		arena.call("_unhandled_input", short_drag_press)
		var short_drag_release := InputEventMouseButton.new()
		short_drag_release.button_index = MOUSE_BUTTON_LEFT
		short_drag_release.pressed = false
		short_drag_release.position = member_screen + Vector2(5.0, 0.0)
		arena.call("_unhandled_input", short_drag_release)
		selected = arena.get("selected_ids")
		_check("playable_scene_short_drag_selects_member", selected.has(int(blue_horde.id)), "selected=%s" % str(selected))

func _controls_ignore_mouse(node: Node) -> bool:
	if node is Control and (node as Control).mouse_filter != Control.MOUSE_FILTER_IGNORE:
		return false
	for child in node.get_children():
		if not _controls_ignore_mouse(child):
			return false
	return true

func _test_interpolated_member_motion() -> void:
	var view_script := load("res://src/stage1/stage1_view.gd")
	var view := view_script.new() as Node3D
	root.add_child(view)
	owned_nodes.append(view)
	var world := Stage1World.new(24, 16)
	world.setup_empty(false)
	var horde := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(3, 8)), 15, 5)
	world.order_move([horde.id], Stage1Grid.cell_center(Vector2i(18, 8)))
	world.advance(3)
	var snapshot := world.snapshot()
	view.call("configure", snapshot)
	var selection: Array[int] = [horde.id]
	view.call("render_snapshot", snapshot, 0.0, selection)
	var melee: Array = view.get("_melee_meshes")
	var blue_melee := melee[Stage1Types.Team.BLUE] as MultiMeshInstance3D
	view.call("render_snapshot", snapshot, 1.0, selection)
	view.call("render_snapshot", snapshot, 0.5, selection)
	var first_horde: Dictionary = snapshot.hordes[0]
	var first_member: Dictionary = first_horde.members[0]
	var previous_fixed: Vector2i = view.call("_interpolated_fixed", first_member.previous_position, first_member.position, 0.0)
	var current_fixed: Vector2i = view.call("_interpolated_fixed", first_member.previous_position, first_member.position, 1.0)
	var halfway_fixed: Vector2i = view.call("_interpolated_fixed", first_member.previous_position, first_member.position, 0.5)
	var previous_world: Vector3 = view.call("fixed_to_world", previous_fixed, 0.0)
	var current_world: Vector3 = view.call("fixed_to_world", current_fixed, 0.0)
	var halfway_world: Vector3 = view.call("fixed_to_world", halfway_fixed, 0.0)
	var midpoint := (previous_world + current_world) * 0.5
	_check("snapshot_has_tick_motion", previous_fixed != current_fixed, "fixed=%s_to_%s" % [str(previous_fixed), str(current_fixed)])
	_check("view_interpolates_between_snapshots", halfway_world.distance_to(midpoint) < 0.002, "delta=%.5f" % halfway_world.distance_to(midpoint))
	_check("view_writes_member_instances", blue_melee.multimesh.instance_count > 0)
	var selection_views: Dictionary = view.get("_selection_views")
	_check("selection_feedback_is_render_owned", selection_views.has(horde.id))

func _test_presented_render_schedules() -> void:
	var expected_hash := -1
	var all_equal := true
	for rate in [60, 120, 144, 240]:
		var view_script := load("res://src/stage1/stage1_view.gd")
		var view := view_script.new() as Node3D
		root.add_child(view)
		owned_nodes.append(view)
		var world := Stage1World.new()
		world.setup_demo(2, 15)
		var snapshot := world.snapshot()
		view.call("configure", snapshot)
		var no_selection: Array[int] = []
		var accumulator := 0
		for _frame in rate * 3:
			accumulator += Stage1World.TICKS_PER_SECOND
			while accumulator >= rate:
				world.tick()
				snapshot = world.snapshot()
				accumulator -= rate
			view.call("render_snapshot", snapshot, float(accumulator) / float(rate), no_selection)
		var hash := world.state_hash()
		if expected_hash < 0:
			expected_hash = hash
		else:
			all_equal = all_equal and hash == expected_hash
		var melee: Array = view.get("_melee_meshes")
		var rendered := 0
		for mesh: MultiMeshInstance3D in melee:
			rendered += mesh.multimesh.instance_count
		_check("presented_%dhz_has_members" % rate, rendered > 0)
	_check("presented_schedules_share_hash", all_equal, "hash=%08X" % expected_hash)
