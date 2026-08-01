extends SceneTree
## Granular boot-cost probe: separates autoload (ContentDB.reload) cost from
## scene load cost, then re-times a warm reload and the individual ContentDB
## load stages. Diagnostic only; never used by gates.

var _t0 := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "LOAD_PROBE_RUNNER")
	_t0 = Time.get_ticks_msec()
	print("PROBE harness_start_ms=%d" % _t0)
	call_deferred("_run")


func _mark(label: String, mark: int) -> int:
	var now := Time.get_ticks_msec()
	print("PROBE %s delta_ms=%d total_ms=%d" % [label, now - mark, now - _t0])
	return now


func _run() -> void:
	var mark := _mark("first_frame(autoloads_done)", _t0)
	# Warm reload: measures steady-state ContentDB.reload with OS caches hot.
	var content_db := root.get_node_or_null("/root/ContentDB")
	if content_db == null:
		print("PROBE content_db_missing")
		quit(1)
		return
	content_db.call("reload")
	mark = _mark("contentdb_reload_warm", mark)
	# Fresh scene load ignoring the resource cache.
	ResourceLoader.load("res://scenes/retail_vertical_slice.tscn", "", ResourceLoader.CACHE_MODE_IGNORE)
	mark = _mark("scene_load_uncached", mark)
	quit(0)
