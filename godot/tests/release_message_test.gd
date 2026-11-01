## godot --headless --path godot --script res://tests/release_message_test.gd
extends SceneTree

func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	var release_messages := root.get_node("Release")
	var report := "Please attach this file and the version above to the OpenBFME Discord #bug-reports forum or a GitHub issue."
	var no_log := "No session log was created. Report this error in the OpenBFME Discord #bug-reports forum or GitHub issues. Include your OpenBFME version."
	var ok: bool = release_messages.REPORT_HINT == report
	ok = ok and release_messages.fatal_text("Startup failed.", "", "") == "Startup failed.\n\nOpenBFME cannot continue.\n\n" + no_log
	ok = ok and release_messages.fatal_text("Startup failed.", "OpenBFME test", "") == "Startup failed.\n\nOpenBFME cannot continue.\n\nVersion: OpenBFME test\n" + no_log
	ok = ok and release_messages.fatal_text("Startup failed.", "OpenBFME test", "session.log") == "Startup failed.\n\nOpenBFME cannot continue.\n\nVersion: OpenBFME test\nLog file: session.log\n\n" + report
	print("RELEASE MESSAGES ", "OK" if ok else "FAIL")
	quit(0 if ok else 1)
