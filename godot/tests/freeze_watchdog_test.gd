extends SceneTree
## FREEZE-1: a main thread that stops finishing frames is reported in the session log: a FREEZE line, every thread's stack beside the log
## (macOS /usr/bin/sample, Linux gdb / eu-stack) and "FREEZE over". Uses the Release autoload's real session log (user://logs) and removes it and its captures at the end.
var _frames := 0
var _waited_from := 0


func _process(_delta: float) -> bool:
	_frames += 1
	var release = root.get_node_or_null("Release")
	if release == null or release.logger == null:
		return _fail("the Release autoload has no session log")
	if _frames == 5:
		OS.delay_msec(7000) # one frozen frame: longer than Release.FREEZE_SECONDS
		_waited_from = Time.get_ticks_msec()
	if _frames <= 5:
		return false
	var text := FileAccess.get_file_as_string(release.log_path)
	# the capture (about 2 s) may finish after the freeze has ended
	if not text.contains("\nFREEZE thread stacks: ") and Time.get_ticks_msec() - _waited_from < 15000:
		return false
	for line in ["\nFREEZE the main thread has not finished a frame for 5.", "\nFREEZE over: the main thread was stalled for 7.", "\nFREEZE thread stacks: "]:
		if not text.contains(line):
			return _fail("no line " + line.strip_edges())
	var stacks_path: String = release.log_path + ".freeze-1.txt"
	if OS.get_name() != "Windows" and not text.contains("FREEZE thread stacks: no stack tool"):
		if FileAccess.get_file_as_string(stacks_path).length() < 1000:
			return _fail("no thread stacks beside the log")
	print("FREEZE WATCHDOG PASS")
	_remove_logs(release.log_path)
	quit(0)
	return false


func _fail(why: String) -> bool:
	printerr("FREEZE WATCHDOG FAIL: ", why)
	quit(1)
	return true


func _remove_logs(log_path: String) -> void:
	for suffix in ["", ".freeze-1.txt", ".freeze-2.txt"]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(log_path + suffix))
