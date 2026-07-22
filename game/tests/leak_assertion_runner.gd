extends SceneTree
## Leak-assertion gate (verification survey gap #4).
##
## Two layers:
## 1. In-process cycling: boot and free the menu shell and the retail vertical
##    slice repeatedly, and assert that object / node / resource / orphan-node
##    counts return to the post-warmup baseline after every cycle. The first
##    cycle is a warmup that populates static caches (ContentDB tables, theme
##    and shader caches); growth after that is a leak.
## 2. Exit-diagnostic scan: spawn a child Godot process running
##    leak_probe_runner.gd and fail on any ObjectDB / RID / instance-dependency
##    leak diagnostic in its output, because a process cannot observe its own
##    exit-time leak report.

const MEASURED_CYCLES := 2
const SETTLE_FRAMES := 8

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)

	# Warmup cycle populates every static cache before the baseline snapshot.
	var warmup := await _boot_free_cycle()
	_check("warmup_cycle_boots_menu_and_slice", warmup, "menu or slice failed to become ready")
	if not warmup:
		_finish()
		return
	var baseline := await _settled_counts()
	print("LEAK_ASSERTION baseline objects=%d resources=%d nodes=%d orphans=%d" % [
		baseline["objects"], baseline["resources"], baseline["nodes"], baseline["orphans"],
	])

	for cycle in range(1, MEASURED_CYCLES + 1):
		var cycle_ok := await _boot_free_cycle()
		_check("cycle_%d_boots_menu_and_slice" % cycle, cycle_ok)
		var counts := await _settled_counts()
		_check(
			"cycle_%d_objects_return_to_baseline" % cycle,
			int(counts["objects"]) <= int(baseline["objects"]),
			"objects %d > baseline %d" % [counts["objects"], baseline["objects"]]
		)
		_check(
			"cycle_%d_resources_return_to_baseline" % cycle,
			int(counts["resources"]) <= int(baseline["resources"]),
			"resources %d > baseline %d" % [counts["resources"], baseline["resources"]]
		)
		_check(
			"cycle_%d_nodes_return_to_baseline" % cycle,
			int(counts["nodes"]) <= int(baseline["nodes"]),
			"nodes %d > baseline %d" % [counts["nodes"], baseline["nodes"]]
		)
		_check(
			"cycle_%d_orphan_nodes_return_to_baseline" % cycle,
			int(counts["orphans"]) <= int(baseline["orphans"]),
			"orphans %d > baseline %d" % [counts["orphans"], baseline["orphans"]]
		)

	_run_exit_diagnostic_probe()
	_finish()


func _boot_free_cycle() -> bool:
	var ok := true

	var menu_scene: PackedScene = load("res://scenes/boot.tscn")
	if menu_scene == null:
		return false
	var menu := menu_scene.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	ok = ok and menu.theme != null
	menu.queue_free()
	await process_frame
	await process_frame

	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null:
		return false
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	ok = ok and bool(slice.ready_ok)
	if not bool(slice.ready_ok):
		printerr("LEAK_ASSERTION slice not ready: %s" % String(slice.failure_reason))
	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	await process_frame
	return ok


func _settled_counts() -> Dictionary:
	for _index in range(SETTLE_FRAMES):
		await process_frame
	return {
		"objects": int(Performance.get_monitor(Performance.OBJECT_COUNT)),
		"resources": int(Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT)),
		"nodes": int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)),
		"orphans": int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)),
	}


func _run_exit_diagnostic_probe() -> void:
	var output: Array = []
	var exit_code := OS.execute(
		OS.get_executable_path(),
		[
			"--headless",
			"--path", ProjectSettings.globalize_path("res://"),
			"--script", "res://tests/leak_probe_runner.gd",
		],
		output,
		true
	)
	var combined := ""
	for chunk in output:
		combined += String(chunk)
	_check("probe_process_exits_zero", exit_code == 0, "exit=%d" % exit_code)
	_check("probe_reports_completion", combined.contains("LEAK_PROBE_RESULT ok"))
	var leak_markers := [
		"instances were leaked at exit",
		"RID allocations of type",
		"Leaked instance",
		"still in use",
		"Orphan StringName",
		"Resources still in use at exit",
	]
	for marker in leak_markers:
		var found := combined.contains(marker)
		_check("probe_output_free_of_marker(%s)" % marker, not found, _first_line_with(combined, marker))


func _first_line_with(text: String, marker: String) -> String:
	for line in text.split("\n"):
		if line.contains(marker):
			return line.strip_edges()
	return ""


func _finish() -> void:
	print("LEAK_ASSERTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("LEAK_ASSERTION PASS %s" % name)
	else:
		failed += 1
		printerr("LEAK_ASSERTION FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
