extends SceneTree

const BuildInfo := preload("res://scripts/core/build_info.gd")

class TestWindow:
	extends "res://scripts/main.gd"

	func _ready() -> void:
		pass

	func _process(_delta: float) -> void:
		pass

	func _exit_tree() -> void:
		pass

	func _play_blocked() -> bool:
		return false

	func _refresh_installed() -> void:
		pass

	func _set_buttons() -> void:
		pass

	func _show_progress(_show: bool) -> void:
		pass

	func _refresh_head() -> void:
		pass

class CompletingUpdater:
	extends "res://scripts/core/updater.gd"

	var worker: Thread
	var finish: Semaphore

	func _init(thread: Thread, release: Semaphore) -> void:
		super(BuildInfo.new())
		worker = thread
		finish = release

	func play_version() -> String:
		return "v0.1.0"

	func play(_version: String, _args := PackedStringArray(), _before_start := Callable()) -> Dictionary:
		# End the worker inside Play, without letting the main thread handle its deferred result yet.
		finish.post()
		var deadline := Time.get_ticks_msec() + 120000
		while worker.is_alive() and Time.get_ticks_msec() < deadline:
			OS.delay_msec(1)
		return {"ok": not worker.is_alive()}


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var window := TestWindow.new()
	root.add_child(window)
	var finish := Semaphore.new()
	var entered := Semaphore.new()
	window._thread = Thread.new()
	window._job_kind = "check"
	window.updater = CompletingUpdater.new(window._thread, finish)
	window._thread.start(func() -> void:
		entered.post()
		finish.wait())
	entered.wait()
	window._on_play()
	if not window._quit_when_done or window._thread.is_alive():
		print("BACKGROUND PLAY lost the pending completion")
		window._thread.wait_to_finish()
		window._thread = null
		quit(1)
		return
	print("BACKGROUND PLAY pending completion kept")
	if OS.get_cmdline_user_args()[0] == "success":
		window._finish("Installed v0.2.0.")
	else:
		var error := "Update rejected: the package does not match the signed manifest"
		window._finish(error, error)
		if window._quit_when_done or window.updater.get_state("launcher", "unseen_error", "") != error:
			quit(1)
			return
		print("BACKGROUND PLAY failure kept for the next start")
		quit(0)
