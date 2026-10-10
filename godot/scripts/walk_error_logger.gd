## OpenBFME (lane CAH-2 r2): counts the GDScript runtime errors logged while game.gd --menu-walk runs (OS.add_logger), so a walk step stopped by a
## script error fails the walk instead of passing with the failures it never counted. Engine errors (ERROR_TYPE_ERROR, e.g. a missing texture the
## W3D path reports) are not counted: they are the other reports' business.
extends Logger

var _mutex := Mutex.new()
var _count := 0
var _first := ""


func _log_error(function: String, file: String, line: int, code: String, rationale: String, _editor_notify: bool, error_type: int, _script_backtraces: Array[ScriptBacktrace]) -> void:
	if error_type != ERROR_TYPE_SCRIPT:
		return
	_mutex.lock()
	_count += 1
	if _first.is_empty():
		_first = "%s:%d %s: %s" % [file, line, function, rationale if not rationale.is_empty() else code]
	_mutex.unlock()


func script_errors() -> int:
	_mutex.lock()
	var n := _count
	_mutex.unlock()
	return n


func first_error() -> String:
	_mutex.lock()
	var f := _first
	_mutex.unlock()
	return f
