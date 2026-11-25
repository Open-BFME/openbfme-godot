## Lane CRASHDIALOG-1: the process the crash handler starts after a crash (WindowsCrashDump: --crash-report=<process id>; game.gd comes straight here). No game
## boots here, so a crash at the game's start cannot repeat itself, and a window of its own shows the dialog while the desktop is back. It waits for the
## crashed process to end (the handlers after the dump still write its log), then Release finds that run's marker, as a next start would.
extends Control

const WAIT_SECONDS := 20.0


func _ready() -> void:
	var window := get_window()
	window.mode = Window.MODE_WINDOWED
	window.borderless = false
	window.min_size = Vector2i(720, 560)
	window.move_to_center()
	window.close_requested.connect(func() -> void: get_tree().quit(0))
	var background := ColorRect.new()
	background.color = Color("0b0b0d")
	background.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var preparing := Label.new()
	preparing.text = "OpenBFME closed unexpectedly. %s" % Release.CRASH_DIALOG_WORDS.preparing
	preparing.add_theme_color_override("font_color", Color("cdbf9f"))
	preparing.set_anchors_preset(Control.PRESET_CENTER)
	preparing.grow_horizontal = Control.GROW_DIRECTION_BOTH
	preparing.grow_vertical = Control.GROW_DIRECTION_BOTH
	add_child(preparing)
	var waited := 0.0
	var pid: int = Release.crash_report_pid()
	while Release.info != null and Release.info.process_alive(pid) and waited < WAIT_SECONDS:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if Release.info == null or Release.info.process_alive(pid):
		printerr("RELEASE CRASH the crashed process is still running: no dialog")
		get_tree().quit(1)
		return
	preparing.queue_free()
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	if not Release.show_crash_after_crash(center):
		get_tree().quit(0)
		return
	# as tall as the dialog needs: a game of eight factions wraps more lines than the window's first size holds
	var fit := func() -> void:
		window.size = Vector2i(window.size.x, maxi(window.min_size.y, int(center.get_combined_minimum_size().y) + 32))
	Release.crash_dialog.minimum_size_changed.connect(fit, CONNECT_DEFERRED)
	fit.call_deferred()
