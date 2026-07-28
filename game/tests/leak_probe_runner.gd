extends SceneTree
## Minimal boot/teardown probe used by leak_assertion_runner.gd.
##
## Boots the menu shell and the retail vertical slice once each, frees them
## through the production teardown paths, and quits. The parent runner scans
## this process's combined output for exit-time ObjectDB/RID leak diagnostics
## (which a Godot process can only observe from outside itself).

const WATCHDOG_MS := 180000

var _started_ms := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "LEAK_PROBE_RUNNER")
	_started_ms = Time.get_ticks_msec()
	process_frame.connect(_watchdog)
	call_deferred("_run")


func _watchdog() -> void:
	if Time.get_ticks_msec() - _started_ms > WATCHDOG_MS:
		printerr("LEAK_PROBE watchdog expired after %d ms" % WATCHDOG_MS)
		quit(2)


func _run() -> void:
	root.size = Vector2i(1920, 1080)

	var menu_scene: PackedScene = load("res://scenes/boot.tscn")
	if menu_scene == null:
		printerr("LEAK_PROBE boot.tscn failed to parse")
		quit(1)
		return
	var menu := menu_scene.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	menu.queue_free()
	await process_frame
	await process_frame

	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null:
		printerr("LEAK_PROBE retail_vertical_slice.tscn failed to parse")
		quit(1)
		return
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	if not bool(slice.ready_ok):
		printerr("LEAK_PROBE slice not ready: %s" % String(slice.failure_reason))
		slice.queue_free()
		await process_frame
		quit(1)
		return
	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	await process_frame

	print("LEAK_PROBE_RESULT ok")
	quit(0)
