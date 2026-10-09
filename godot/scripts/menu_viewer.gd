## The retail menus on screen (menus-apt.md step A8): mounts pure RotWK 2.01, boots the Apt player exactly as the headless tests do
## (AptLevel0 as level 0, MainMenu as level 1), draws it through the AptMenuPlayer extension class and lets you click through.
##
##   godot --path godot res://scenes/menu_viewer.tscn -- [options]
##
## Mouse: the real mouse drives the player (window -> stage mapping in AptCanvas.h).  F1 overlay, F2 stretch/fit, F3 component
## placeholders, Esc pops a screen.  Every fscommand the movies send is printed ("FSCOMMAND <command> (<argument>)").
##
## Options (after `--`):
##   --movie=<Name>          the movie of level 1 instead of MainMenu (any of the 86 retail movies)
##   --res=1024x768          window size (default: the project's 1280x720)
##   --fit                   keep the stage aspect (default: stretch to the window, the donor rule)
##   --placeholders          draw the native-component rectangles
##   --clicks=a,b            click the buttons at/under these paths (of the top screen), --settle player frames after each
##   --hover=<path>          after the clicks, move the pointer onto the button at/under this path
##   --settle=<frames>       player frames to run after each click (default 60)
##   --screenshot=<path>     after the clicks, save the viewport and quit
##   --measure=<frames>      after the clicks, run this many frames and print FRAMETIME statistics, then quit
##   --check                 run the clicks without a screenshot (headless runs), then quit
##   --report                print the player's report (unverified rules, errors, missing labels, fonts) before quitting
##   --ops                   print one line per canvas operation after the clicks
##   --verbose               print traces and extern reads too
##   --audio-report          print the audio manager's report and stats before quitting (the run always checks that the shell music plays)
extends Node2D

const STAGE_LEVEL0 := 0
const FIRST_SCREEN_LEVEL := 1

# The engine side of the shell, stubbed (A4 is not built, stop S-139): which movie an fscommand of the main menu opens.  Menus-apt.md 1.2-1.10
# lists the screens; Shell::push of each is the donor fact for Skirmish only (ShowSkirmish.cpp:36-41).
const SCREEN_COMMANDS := {
	"AptMainMenu::Skirmish": "Skirmish",
	"AptMainMenu::Options": "Options",
	"AptMainMenu::CreateAHero": "CreateAHero",
	"AptMainMenu::LAN": "LanLobby",
	"AptMainMenu::LoadGame": "SaveLoad",
	"AptMainMenu::LoadReplay": "SaveLoad",
}
# Commands after which the stub shell pops the top screen (names of the screens' own back buttons).
const POP_COMMANDS := ["::Back", "::Cancel", "::Exit", "::ReturnToGame"]

# The engine side of the extern object (menus-apt.md 3.3: extern.InGame must be provided and truthy; BFME2 handlers 0x0081273C...).  Retail facts
# live here, not in the extension.
const EXTERN_VALUES := {
	"InGame": "1",
	"InBetaDemo": "0",
	"InDreamMachineDemo": "0",
	"DoTrace": "0",
	"MainMenuUnlockBonusCampaign": "0",
}
# Instances of the symbols these movies export are native components (menus-apt.md 3.3: the GameWindowGadgets placeholders).
const COMPONENT_MOVIES := ["GameWindowGadgets"]

var _res := Vector2i(0, 0)
var _fit := false
var _placeholders := false
var _clicks: PackedStringArray = []
var _screenshot_path := ""
var _measure_frames := 0
var _print_report := false
var _verbose := false
var _check := false
var _movie := "MainMenu"
var _hover := ""
var _settle := 60
var _print_ops := false
var _audio_report := false

var _fs: RefCounted
var _player: Node2D
var _audio: Node
var _stack: Array[int] = []        # levels of the screens, bottom first
var _label: Label
var _log: Array[String] = []
var _frame_times: Array[float] = []
var _shell_actions: Array[String] = []
var _automated := false
var _frames := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--res="):
			var p := arg.substr(6).split("x")
			_res = Vector2i(int(p[0]), int(p[1]))
		elif arg == "--fit":
			_fit = true
		elif arg == "--placeholders":
			_placeholders = true
		elif arg.begins_with("--clicks="):
			_clicks = arg.substr(9).split(",", false)
		elif arg.begins_with("--screenshot="):
			_screenshot_path = arg.substr(13)
		elif arg.begins_with("--measure="):
			_measure_frames = int(arg.substr(10))
		elif arg == "--report":
			_print_report = true
		elif arg == "--verbose":
			_verbose = true
		elif arg.begins_with("--movie="):
			_movie = arg.substr(8)
		elif arg.begins_with("--hover="):
			_hover = arg.substr(8)
		elif arg.begins_with("--settle="):
			_settle = int(arg.substr(9))
		elif arg == "--ops":
			_print_ops = true
		elif arg == "--check":
			_check = true
		elif arg == "--audio-report":
			_audio_report = true
	# the 3D shell-map background is a blank placeholder (stop S-138): a dark backdrop so the menu art reads
	RenderingServer.set_default_clear_color(Color(0.035, 0.045, 0.06))
	_automated = not _screenshot_path.is_empty() or _measure_frames > 0 or _check
	if _res != Vector2i(0, 0):
		get_window().size = _res
		get_window().content_scale_size = Vector2i(0, 0)
	if not ClassDB.class_exists("AptMenuPlayer"):
		_fail("the openbfme extension is not loaded (or too old); run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_player = ClassDB.instantiate("AptMenuPlayer")
	add_child(_player)
	_player.fit = _fit
	_player.show_placeholders = _placeholders
	var boot: Dictionary = _player.boot(_fs, {
		"levels": [[STAGE_LEVEL0, "AptLevel0"], [FIRST_SCREEN_LEVEL, _movie]],
		"extern_values": EXTERN_VALUES,
		"component_movies": COMPONENT_MOVIES,
	})
	if not boot.ok:
		_fail("boot failed:\n" + "\n".join(boot.errors))
		return
	print("MENUVIEW booted in %.0f ms: %d strings (%d duplicate labels), fonts %s" % [boot.load_ms, boot.strings, boot.duplicate_labels, boot.fonts])
	_stack.append(FIRST_SCREEN_LEVEL)
	if not _boot_audio():
		return
	_build_overlay()
	if _automated:
		_player.auto_process = false
		_run_automation()
	else:
		set_process(true)


# The shell's audio (AUDIO-1): the APT PlaySound fscommand and the main menu music go through GameAudio, the retail audio manager.
func _boot_audio() -> bool:
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class (too old); run build.bat")
		return false
	_audio = ClassDB.instantiate("GameAudio")
	add_child(_audio)
	var booted: Dictionary = _audio.boot(_fs, {"seed": 1})
	if not booted.ok:
		_fail("audio boot failed:\n" + "\n".join(booted.errors))
		return false
	print("MENUVIEW audio booted: %d events, pools %d/%d/%d" % [booted.events, booted.sample_count_2d, booted.sample_count_3d, booted.stream_count])
	if _movie == "MainMenu":
		var handle: int = _audio.play_shell_music(false)
		print("MENUVIEW shell music requested: LowLODShellMusic -> handle %d" % handle)
	return true


func _audio_wait(seconds: float) -> void:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


func _fail(message: String) -> void:
	printerr("MENUVIEW FAIL: ", message)
	if _automated or DisplayServer.get_name() == "headless":
		get_tree().quit(1)


func _build_overlay() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 10
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(8, 8)
	_label.add_theme_color_override("font_color", Color(1, 1, 0.6))
	_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_label.add_theme_constant_override("outline_size", 4)
	_label.visible = not _automated
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(_label)


func _process(delta: float) -> void:
	_frames += 1
	if _frames == 20 and _movie == "MainMenu":
		# the engine reveals the main menu once its first frames ran (S-138)
		var shown: Dictionary = _player.invoke(FIRST_SCREEN_LEVEL, "ShowMainMenu", PackedStringArray())
		if not shown.ok:
			printerr("MENUVIEW FAIL: ShowMainMenu: ", shown.error)
	_pump_events()
	var stats: Dictionary = _player.get_stats()
	var lines: Array[String] = ["%.1f ms/frame (%.0f fps)  rebuild: list %.0f us, canvas %.0f us, submit %.0f us  ops %d, triangles %d, items %d" % [
		delta * 1000.0, 1.0 / maxf(delta, 0.0001), stats.list_us, stats.canvas_us, stats.submit_us, stats.last_ops, stats.last_triangles, stats.canvas_items]]
	lines.append("screens %s  [F1 overlay, F2 %s, F3 placeholders, Esc back]" % [str(_stack), "fit" if not _player.fit else "stretch"])
	for entry in _log.slice(maxi(0, _log.size() - 6)):
		lines.append(entry)
	_label.text = "\n".join(lines)


func _input(event: InputEvent) -> void:
	if _verbose and (event is InputEventMouseButton or (event is InputEventMouseMotion and _frames % 30 == 0)):
		print("MENUVIEW input ", event, " -> stage ", _player.window_to_stage((event as InputEventMouse).position))


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_F1:
			_label.visible = not _label.visible
		KEY_F2:
			_player.fit = not _player.fit
		KEY_F3:
			_player.show_placeholders = not _player.show_placeholders
		KEY_ESCAPE:
			_pop_screen("Esc")


# ---- engine side (stubbed) --------------------------------------------------------------------------------------------------------

func _pump_events() -> void:
	for e: Dictionary in _player.take_events():
		match e.kind:
			"fscommand":
				var line := "FSCOMMAND %s (%s)" % [e.a, e.b]
				_remember(line, e.frame)
				_on_fscommand(e.a, e.b)
			"load":
				_remember("LOADMOVIE %s -> %s" % [e.a, e.b], e.frame)
			"url":
				_remember("GETURL %s %s" % [e.a, e.b], e.frame)
			"extern_unknown":
				_remember("EXTERN READ with no provider: %s" % e.a, e.frame)
			"extern_set":
				_remember("EXTERN SET %s = %s" % [e.a, e.b], e.frame)
			"script_error":
				_remember("SCRIPT ERROR %s" % e.a, e.frame)
			"extern_get":
				if _verbose:
					_remember("EXTERN GET %s = %s" % [e.a, e.b], e.frame)
			"trace":
				if _verbose:
					_remember("TRACE %s" % e.a, e.frame)


func _remember(line: String, frame: int) -> void:
	var text := "[%d] %s" % [frame, line]
	_log.append(text)
	print(text)


func _on_fscommand(command: String, argument: String) -> void:
	if command == "PlaySound":
		# FSCommand:PlaySound (the shell's button sounds, e.g. Gui_ShellMapMouseOver): the argument is the audio event name
		var handle: int = _audio.play_shell_sound(argument)
		_remember("AUDIO PlaySound %s -> handle %d" % [argument, handle], 0)
		return
	if SCREEN_COMMANDS.has(command):
		_push_screen(SCREEN_COMMANDS[command], command, argument)
		return
	for suffix in POP_COMMANDS:
		if command.ends_with(suffix) and _stack.size() > 1:
			_pop_screen(command)
			return


func _push_screen(movie: String, command: String, argument: String) -> void:
	var level: int = _stack.back() + 1
	var result: Dictionary = _player.load_movie(level, movie)
	if not result.ok:
		_remember("SHELL push %s failed: %s" % [movie, result.error], 0)
		return
	_player.set_level_visible(_stack.back(), false)
	_stack.append(level)
	_audio.play_submenu_music()
	_remember("SHELL push %s as _level%d (for %s '%s')" % [movie, level, command, argument], 0)


func _pop_screen(why: String) -> void:
	if _stack.size() <= 1:
		return
	var level: int = _stack.pop_back()
	_player.unload_level(level)
	_player.set_level_visible(_stack.back(), true)
	if _stack.size() == 1 and _movie == "MainMenu":
		_audio.play_shell_music(false)
	_remember("SHELL pop _level%d (%s)" % [level, why], 0)


# ---- scripted run (screenshots, measurements) ------------------------------------------------------------------------------------------

func _step(frames: int) -> void:
	for i in frames:
		_player.tick(0.033)
		_player.render(false)
		_pump_events()


func _mouse_event(stage_position: Vector2, pressed: bool, with_button: bool) -> void:
	# through the viewport, so the extension's _input and the window -> stage mapping are in the path
	var window_position: Vector2 = _player.stage_to_window(stage_position)
	if with_button:
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTON_LEFT
		button.pressed = pressed
		button.position = window_position
		button.global_position = window_position
		get_viewport().push_input(button)
	else:
		var motion := InputEventMouseMotion.new()
		motion.position = window_position
		motion.global_position = window_position
		get_viewport().push_input(motion)


func _click(path: String) -> bool:
	var button: Dictionary = _player.find_button(_stack.back(), path)
	if not button.found:
		printerr("MENUVIEW FAIL: no button at or under '%s' on _level%d" % [path, _stack.back()])
		return false
	var p := Vector2(button.x, button.y)
	_mouse_event(p, false, false)
	_step(1)
	_mouse_event(p, true, true)
	_step(1)
	_mouse_event(p, false, true)
	_step(1)
	print("MENUVIEW clicked %s (%s) at stage (%.1f, %.1f)" % [path, button.path, p.x, p.y])
	return true


func _run_automation() -> void:
	_step(10)
	# the engine reveals the main menu (menus-apt.md 1.2: ShowMainMenu is defined by the movie; who calls it is unverified, S-139)
	if _movie == "MainMenu":
		var shown: Dictionary = _player.invoke(FIRST_SCREEN_LEVEL, "ShowMainMenu", PackedStringArray())
		if not shown.ok:
			printerr("MENUVIEW FAIL: ShowMainMenu: ", shown.error)
	_step(60)
	for path in _clicks:
		if not _click(path):
			get_tree().quit(1)
			return
		_step(_settle)
	if not _hover.is_empty():
		var target: Dictionary = _player.find_button(_stack.back(), _hover)
		if not target.found:
			printerr("MENUVIEW FAIL: no button at or under '%s' to hover" % _hover)
			get_tree().quit(1)
			return
		_mouse_event(Vector2(target.x, target.y), false, false)
		_step(_settle)
	await get_tree().process_frame
	await get_tree().process_frame
	await _audio_wait(0.6)
	if not _check_audio():
		get_tree().quit(1)
		return
	if _measure_frames > 0:
		await _measure()
	if not _screenshot_path.is_empty():
		await RenderingServer.frame_post_draw
		var image := get_viewport().get_texture().get_image()
		DirAccess.make_dir_recursive_absolute(_screenshot_path.get_base_dir())
		var err := image.save_png(_screenshot_path)
		print("MENUVIEW screenshot %s -> %s (%dx%d)" % [_screenshot_path, error_string(err), image.get_width(), image.get_height()])
	if _print_ops:
		for line in _player.describe_ops():
			print("OP ", line)
	if _print_report:
		print("MENUVIEW report ", JSON.stringify(_player.get_report(), "  "))
		print("MENUVIEW stats ", JSON.stringify(_player.get_stats(), "  "))
	_audio.shutdown()
	await _audio_wait(0.3) # the engine releases stopped playbacks on its next mixes
	get_tree().quit(0)


# The headless audio check: the main menu music plays (a MusicTrack of the LowLODShellMusic multisound) and nothing the shell played failed.
func _check_audio() -> bool:
	var ok := true
	var report: Dictionary = _audio.get_report()
	if _movie == "MainMenu":
		var playing: bool = _audio.is_music_playing()
		print("AUDIOCHECK shell music playing=%s track=%s screens=%s" % [playing, _audio.get_music_track(), str(_stack)])
		if not playing:
			printerr("AUDIOCHECK FAIL: the shell music is not playing")
			ok = false
	if report.play_failures > 0 or report.unknown_events > 0:
		printerr("AUDIOCHECK FAIL: %d play failures, %d unknown events: %s" % [report.play_failures, report.unknown_events, str(report.errors)])
		ok = false
	if _audio_report:
		print("AUDIOCHECK report ", JSON.stringify(report, "  "))
		print("AUDIOCHECK stats ", JSON.stringify(_audio.get_stats(), "  "))
		print("AUDIOCHECK unverified ", JSON.stringify(_audio.get_unverified(), "  "))
	return ok


func _measure() -> void:
	# Real-time frames at the display's rate: the player advances by the measured delta, the canvas is rebuilt on each player step.
	_player.auto_process = true
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	var viewport_rid := get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(viewport_rid, true)
	for i in 30:
		await get_tree().process_frame
	_player.reset_timing()
	var frame_times: Array[float] = []
	var last := Time.get_ticks_usec()
	for i in _measure_frames:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		frame_times.append(float(now - last) / 1000.0)
		last = now
	frame_times.sort()
	var total := 0.0
	for t in frame_times:
		total += t
	var stats: Dictionary = _player.get_stats()
	print("FRAMETIME frames=%d mean=%.2f ms median=%.2f ms p95=%.2f ms max=%.2f ms (%.1f fps) render_cpu=%.2f ms render_gpu=%.2f ms" % [
		frame_times.size(), total / frame_times.size(), frame_times[frame_times.size() / 2], frame_times[int(frame_times.size() * 0.95)], frame_times.back(),
		1000.0 * frame_times.size() / total, RenderingServer.viewport_get_measured_render_time_cpu(viewport_rid), RenderingServer.viewport_get_measured_render_time_gpu(viewport_rid)])
	print("FRAMETIME player: step %.0f us per frame, rebuild: render list %.0f us + canvas build %.0f us + canvas submit %.0f us (%d rebuilds in %d frames), ops %d, triangles %d, canvas items %d" % [
		stats.steps_us, stats.list_us, stats.canvas_us, stats.submit_us, stats.rebuilds, stats.frames, stats.last_ops, stats.last_triangles, stats.canvas_items])
	print("FRAMETIME adapter='%s' resolution=%s textures loaded: %d (%.1f ms)" % [
		RenderingServer.get_video_adapter_name(), str(get_viewport().get_visible_rect().size), stats.texture_loads, stats.texture_load_ms])
