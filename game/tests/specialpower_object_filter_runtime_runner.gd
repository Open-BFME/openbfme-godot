extends SceneTree
## field:specialpower.ObjectFilter

const Support = preload("res://tests/specialpower_filter_runtime_support.gd")
const WATCHDOG_FRAMES := 900

var _frames := 0
var _reported := false


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames > WATCHDOG_FRAMES and not _reported:
		_report({"ok": false, "detail": "watchdog: the runner aborted before reporting"})
	return false


func _run() -> void:
	_report(Support.new().run("field:specialpower.ObjectFilter"))


func _report(result: Dictionary) -> void:
	_reported = true
	var ok := bool(result.get("ok", false))
	print("SPECIALPOWER_OBJECT_FILTER_RESULT passed=%d failed=%d detail=%s" % [
		1 if ok else 0, 0 if ok else 1, String(result.get("detail", "")),
	])
	quit(0 if ok else 1)
