extends RefCounted
## Abort watchdog for `extends SceneTree` headless runners.
##
## Every runner in this directory has the same shape: `_initialize()` defers to
## `_run()`, and the ONLY call to `quit()` lives at the end of `_run()` (or its
## `_finish()` helper). A GDScript runtime error inside `_run()` unwinds the
## whole call stack straight back to the engine, so `quit()` is never reached
## and the headless process idles forever with no result line. That failure mode
## has cost hours of wall clock (one orphaned run sat for eight hours) and it is
## strictly worse than a red test: a hang looks like "still working".
##
## This helper drives a `process_frame` tick that survives the aborted coroutine
## and turns the hang into a loud non-zero exit.
##
## Usage (hard deadline only — safe to bolt onto any runner):
##     const Watchdog := preload("res://tests/runner_watchdog.gd")
##     var _watchdog := Watchdog.new()
##     func _initialize() -> void:
##         _watchdog.start(self, "MY_RUNNER")
##         call_deferred("_run")
##
## Usage (adds fast stall detection — needs one hook in the assert helper):
##     _watchdog.start(self, "MY_RUNNER", 0, 0, true)
##     ...
##     func _check(name, ...) -> void:
##         _watchdog.note(name)
##
## Both bounds are overridable from the environment so a deliberately long soak
## can raise them without editing code:
##     OPENBFME_RUNNER_DEADLINE_MS, OPENBFME_RUNNER_STALL_MS

## Hard upper bound on total runner wall clock. Generous on purpose: this is the
## catch-all for a runner that wedges somewhere the stall probe cannot see.
const DEFAULT_DEADLINE_MS := 1_200_000
## Bound on the gap between two consecutive `note()` calls. Only armed when the
## runner opts in by calling `note()`. Sized well above the slowest observed
## quiet stretch (multi-faction pack load plus the scripted battle replay).
const DEFAULT_STALL_MS := 300_000

var _tree: SceneTree = null
var _prefix := "RUNNER"
var _deadline_ms := DEFAULT_DEADLINE_MS
var _stall_ms := 0
var _started_msec := 0
var _last_note_msec := 0
var _last_note := "<none>"
var _stopped := false
var _aborted := false
var _result_provider := Callable()


func start(tree: SceneTree, prefix: String, deadline_ms: int = 0, stall_ms: int = 0, track_stalls: bool = false) -> void:
	_tree = tree
	_prefix = prefix
	_deadline_ms = _env_int("OPENBFME_RUNNER_DEADLINE_MS", deadline_ms if deadline_ms > 0 else DEFAULT_DEADLINE_MS)
	if track_stalls:
		_stall_ms = _env_int("OPENBFME_RUNNER_STALL_MS", stall_ms if stall_ms > 0 else DEFAULT_STALL_MS)
	_started_msec = Time.get_ticks_msec()
	_last_note_msec = _started_msec
	if not _tree.process_frame.is_connected(_tick):
		_tree.process_frame.connect(_tick)


## Record forward progress. Restarts the stall clock.
func note(label: String) -> void:
	_last_note_msec = Time.get_ticks_msec()
	_last_note = label


## Optional. Supply a `func() -> Vector2i` returning (passed, failed) so an
## abort still reports the counts the runner had reached instead of zeroes.
func set_result_provider(provider: Callable) -> void:
	_result_provider = provider


## Called once the runner has produced its result line and is about to quit.
func stop() -> void:
	_stopped = true
	if _tree != null and _tree.process_frame.is_connected(_tick):
		_tree.process_frame.disconnect(_tick)


func _tick() -> void:
	if _stopped or _aborted or _tree == null:
		return
	var now := Time.get_ticks_msec()
	if _stall_ms > 0 and now - _last_note_msec > _stall_ms:
		_abort(
			"no progress for %.1fs after '%s' — the runner body almost certainly aborted on a GDScript runtime error (look above for the engine's SCRIPT ERROR line)"
			% [float(now - _last_note_msec) / 1000.0, _last_note]
		)
		return
	if now - _started_msec > _deadline_ms:
		_abort(
			"exceeded the %.0fs hard deadline (last progress: '%s')"
			% [float(_deadline_ms) / 1000.0, _last_note]
		)


func _abort(reason: String) -> void:
	_aborted = true
	push_error("%s watchdog abort: %s" % [_prefix, reason])
	print("%s FAIL runner_completed_without_watchdog_abort %s" % [_prefix, reason])
	var counts := Vector2i.ZERO
	if _result_provider.is_valid():
		counts = _result_provider.call()
	print(
		"%s_RESULT passed=%d failed=%d (WATCHDOG ABORT — the run never reached its own result line)"
		% [_prefix, counts.x, counts.y + 1]
	)
	stop()
	_tree.quit(1)


func _env_int(key: String, fallback: int) -> int:
	var raw := OS.get_environment(key).strip_edges()
	if raw.is_valid_int():
		var parsed := int(raw)
		if parsed > 0:
			return parsed
	return fallback
