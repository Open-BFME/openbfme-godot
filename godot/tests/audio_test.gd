## Headless audio test against real retail files (AUDIO-1).
##
##   godot --headless --path godot --script res://tests/audio_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL; without them it prints SKIP and exits 77 (never passes silently). Exit codes: 0 pass, 1 fail, 77 skip.
## Checks the Godot half of the audio device through the real GameAudio class: boot, the shell music of the main menu, a shell button sound,
## a positional sound culled by distance, the options sliders, the report.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

# AHSV_NoSound / AHSV_Muted / AHSV_NotForLocal ... (AudioHandleSpecialValues): handles below 5 are not instances
const FIRST_HANDLE := 5

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("AUDIOTEST %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _wait(seconds: float) -> void:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		await process_frame


# plays the shell button sound at the given sound volume; returns [peak of the first 512 captured frames from its onset, peak of the rest]
func _onset_peaks(audio: Node, capture: AudioEffectCapture, volume: float) -> Array:
	audio.set_volume("sound", volume)
	await _wait(1.0)
	capture.clear_buffer()
	audio.play_shell_sound("Gui_ShellMapMouseOver")
	await _wait(0.5)
	var buf: PackedVector2Array = capture.get_buffer(capture.get_frames_available())
	var onset := -1
	var first := 0.0
	var rest := 0.0
	for i in buf.size():
		var a := maxf(absf(buf[i].x), absf(buf[i].y))
		if onset < 0 and a > 0.0:
			onset = i
		if onset >= 0:
			if i < onset + 512:
				first = maxf(first, a)
			else:
				rest = maxf(rest, a)
	return [first, rest]


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL to run the retail audio test")
		return EXIT_SKIP
	if not ClassDB.class_exists("GameAudio"):
		print("FAIL: the openbfme extension has no GameAudio class (run build.bat, then godot --headless --import)")
		return EXIT_FAIL
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", "\n  ".join(mount.errors))
		return EXIT_FAIL
	var audio: Node = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	var booted: Dictionary = audio.boot(fs, {"seed": 7})
	_check(booted.ok, "boot: " + str(booted.get("errors", [])))
	if not booted.ok:
		return EXIT_FAIL
	_check(booted.events == 7988, "7,988 audio events parsed (%d)" % booted.events)
	_check(booted.sample_count_2d == 4 and booted.sample_count_3d == 25 and booted.stream_count == 3, "voice pools 4 / 25 / 3 from AudioSettings.ini")
	_check(audio.get_event_names(2).size() == 6630, "6,630 non-default AudioEvent names (%d)" % audio.get_event_names(2).size())
	_check(audio.get_event_names(5).size() == 608, "608 Multisound names")
	_check(audio.is_valid_event("Gui_ShellMapMouseOver"), "the shell button sound exists")
	_check(not audio.is_valid_event("NoSuchEvent"), "an unknown event is invalid")

	# QACRASH-1: a voice's first mix buffer plays at the voice's own volume (Godot starts a playback at its target gain), on a fresh slot
	# and on a slot whose last voice was loud: captured at the master bus, the first buffer after the onset stays under the quiet volume
	var capture := AudioEffectCapture.new()
	capture.buffer_length = 2.0
	AudioServer.add_bus_effect(0, capture)
	var quiet: Array = await _onset_peaks(audio, capture, 0.01)  # a fresh slot
	var loud: Array = await _onset_peaks(audio, capture, 1.0)    # the same slot, after the quiet voice
	var reused: Array = await _onset_peaks(audio, capture, 0.01) # the same slot, after the loud voice
	audio.set_volume("sound", 1.0)
	AudioServer.remove_bus_effect(0, AudioServer.get_bus_effect_count(0) - 1)
	print("  onset peaks [first buffer, rest]: quiet %s, loud %s, quiet after loud %s" % [quiet, loud, reused])
	var ok: bool = quiet[1] > 0.0 and loud[1] > 0.0 and reused[1] > 0.0
	_check(ok, "the button sound is captured at the master bus at both volumes")
	# the quiet voice's gain relative to the loud one, from the steady part; its first buffer may exceed that by a factor 2 at most
	var bound: float = 2.0 * loud[0] * quiet[1] / maxf(loud[1], 1e-12)
	_check(ok and quiet[0] <= bound, "a quiet voice on a fresh slot starts quiet (first-buffer peak %s, bound %s)" % [quiet[0], bound])
	_check(ok and reused[0] <= bound, "a quiet voice after a loud one on the same slot starts quiet (first-buffer peak %s, bound %s)" % [reused[0], bound])

	# the main menu music: MiscAudio LowLODShellMusic is a looping PLAY_ONE multisound of MusicTracks
	var music: int = audio.play_shell_music(false)
	_check(music >= FIRST_HANDLE, "play_shell_music returns an instance handle (%d)" % music)
	await _wait(0.5)
	_check(audio.is_music_playing(), "the shell music is playing (track %s)" % audio.get_music_track())
	_check(audio.get_stats().playing_streams == 1, "one stream voice")

	# the engine entry points (AudioApi) reach this manager while it is booted
	_check(audio.engine_api_installed(), "AudioApi has the booted manager installed")
	var via_api: int = audio.engine_api_play_ui_sound("Gui_ShellMapMouseOver")
	_check(via_api >= FIRST_HANDLE, "AudioApi::playUiSound plays through the installed manager (%d)" % via_api)
	await _wait(0.3)

	# a finished voice returns its pool slot: more sounds than the pool holds finish and are replaced (some are culled by the event's Limit)
	var pool: int = booted.pool_size
	var played_before: int = audio.get_report().played
	var plays := 0
	while plays < 400 and audio.get_report().played - played_before < pool + 15:
		audio.play_shell_sound("Gui_ShellMapMouseOver")
		plays += 1
		await _wait(0.12)
	await _wait(0.4)
	var played_after: int = audio.get_report().played
	_check(played_after - played_before >= pool + 15, "%d sounds played through a pool of %d (%d plays asked)" % [played_after - played_before, pool, plays])
	_check(audio.get_report().play_failures == 0, "no play failure while recycling the pool: " + str(audio.get_report().errors))

	# the shell's PlaySound fscommand
	var click: int = audio.play_shell_sound("Gui_ShellMapMouseOver")
	_check(click >= FIRST_HANDLE, "play_shell_sound plays the button sound (%d)" % click)
	await _wait(0.3)

	# positional: far beyond MaxRange is culled; close plays; the report counts it
	audio.set_listener(Vector3.ZERO, Vector3(0, 1, 0))
	var far: int = audio.play_sound_at("VolumeSampleSoundFX", Vector3(100000, 0, 0))
	_check(far < FIRST_HANDLE, "a world sound 100000 units away is culled (handle %d)" % far)
	var near: int = audio.play_sound_at("VolumeSampleSoundFX", Vector3(50, 0, 0))
	_check(near >= FIRST_HANDLE, "the same sound 50 units away plays (%d)" % near)
	await _wait(0.3)
	_check(audio.get_report().culled_distance == 1, "the report counts one distance cull")
	# lane AUDIO-5: the near sound is due right of the listener at its height: Miles Fast 2D puts it entirely in the right channel, which
	# the device reaches with its voice bus panner at +1
	var hard_right := false
	for b in AudioServer.bus_count:
		if AudioServer.get_bus_name(b).begins_with("OBFME_Voice_"):
			for e in AudioServer.get_bus_effect_count(b): # QACRASH-1: [AudioEffectAmplify, AudioEffectPanner]
				var panner := AudioServer.get_bus_effect(b, e) as AudioEffectPanner
				hard_right = hard_right or (panner != null and absf(panner.pan - 1.0) < 0.001)
	_check(hard_right, "a voice bus pans the sound due right of the listener fully right")
	# retail's microphone (RW 0x45235B) for the default tactical camera looking north at (1000, 1000)
	audio.update_microphone(Vector3(1000, 609.032388, 300), Vector3(1000, 1000, 0), true)
	var mic: Dictionary = audio.get_listener()
	_check(mic.position.distance_to(Vector3(1000, 938.815356, 117.371571)) < 0.01 and mic.forward.distance_to(Vector3(0, 1, 0)) < 0.0001,
			"update_microphone puts the listener at retail's microphone (%s, facing %s)" % [mic.position, mic.forward])

	# the options sliders
	audio.set_volume("music", 0.25)
	_check(absf(audio.get_volume("music") - 0.25) < 0.001, "set_volume / get_volume music")
	await _wait(0.2)
	audio.stop_music(false)
	await _wait(0.2)
	_check(not audio.is_music_playing(), "stop_music ends the music")

	var report: Dictionary = audio.get_report()
	_check(report.play_failures == 0, "no play failures: " + str(report.errors))
	audio.shutdown()
	_check(not audio.engine_api_installed(), "shutdown uninstalls the engine entry points")
	var ticks_after_shutdown: int = audio.get_report().ticks
	await _wait(0.3)
	_check(audio.get_report().ticks == ticks_after_shutdown, "no processing after shutdown")
	audio.queue_free()
	await process_frame
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
