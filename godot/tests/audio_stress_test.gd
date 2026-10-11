## Headless audio stress test against real retail files (QACRASH-1).
##
##   godot --headless --path godot --script res://tests/audio_stress_test.gd [-- --seconds=<n>]
##
## Needs ROTWK_INSTALL and BFME2_INSTALL; without them it prints SKIP and exits 77 (never passes silently). Exit codes: 0 pass, 1 fail, 77 skip.
## The QA farm's audio-thread SIGSEGV: Godot 4.7.2 replaces a playback's bus details on every AudioStreamPlayer.volume_db write and frees the old
## ones two main-loop frames later, racing its mix thread. This test keeps the whole voice pool busy with positional sounds while the listener moves
## every frame (the core then updates every voice's volume and pan every audio tick) on an uncapped main loop, and checks that the volume reaches
## the voices through each slot bus's AudioEffectAmplify while no pooled AudioStreamPlayer's volume_db ever leaves 0 dB. Run it under
## tools/audio/starve_mix_thread.sh to also starve the mix thread (the condition that turned the race into a crash).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

const FIRST_HANDLE := 5
const VOICE_BUS_PREFIX := "OBFME_Voice_"
const ST_WORLD := 0x0002 # AudioEventInfo.h SoundType bits
const AC_LOOP := 0x01    # AudioEventInfo.h AudioControl bits

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("AUDIOSTRESS %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL to run the retail audio stress test")
		return EXIT_SKIP
	if not ClassDB.class_exists("GameAudio"):
		print("FAIL: the openbfme extension has no GameAudio class (run build.bat, then godot --headless --import)")
		return EXIT_FAIL
	var seconds := 20.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--seconds="):
			seconds = float(a.substr(10))
	# the main loop as fast as it goes: the bus-details graveyard ages by main-loop frames
	Engine.max_fps = 0
	OS.low_processor_usage_mode_sleep_usec = 0

	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", "\n  ".join(mount.errors))
		return EXIT_FAIL
	var audio: Node = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	var booted: Dictionary = audio.boot(fs, {"seed": 11})
	_check(booted.ok, "boot: " + str(booted.get("errors", [])))
	if not booted.ok:
		return EXIT_FAIL

	var players: Array[AudioStreamPlayer] = []
	for c in audio.get_children():
		if c is AudioStreamPlayer:
			players.append(c)
	_check(players.size() == booted.pool_size, "one AudioStreamPlayer per pooled voice (%d / %d)" % [players.size(), booted.pool_size])
	# (a layout failure is reported but the stress still runs: the same script measures a build without the fix)
	var amps: Array[AudioEffectAmplify] = []
	var bad_buses := 0
	for p in players:
		var bus := AudioServer.get_bus_index(p.bus)
		var ok := bus >= 0 and String(p.bus).begins_with(VOICE_BUS_PREFIX) and AudioServer.get_bus_effect_count(bus) == 2
		ok = ok and AudioServer.get_bus_effect(bus, 0) is AudioEffectAmplify and AudioServer.get_bus_effect(bus, 1) is AudioEffectPanner
		if ok:
			amps.append(AudioServer.get_bus_effect(bus, 0))
		else:
			bad_buses += 1
	_check(bad_buses == 0, "every voice bus carries [AudioEffectAmplify, AudioEffectPanner] (%d do not)" % bad_buses)

	# looping world sounds keep their voices: many different events, so no single event's Limit caps the pool
	var events: Array[String] = []
	for n in audio.get_event_names(2):
		var info: Dictionary = audio.get_event_info(n)
		if (int(info.type_bits) & ST_WORLD) and (int(info.control_bits) & AC_LOOP) and not info.files.is_empty() and float(info.max_range) >= 300.0:
			events.append(n)
	events.sort()
	_check(events.size() >= 40, "%d looping world events to play" % events.size())
	if events.is_empty():
		return EXIT_FAIL

	var frames := 0
	var plays := 0
	var started := 0
	var handles: Array[int] = []
	var next_play := 0.0
	var max_playing := 0
	var player_volume_writes := 0
	var amp_values := {}
	var t0 := Time.get_ticks_msec()
	var until := t0 + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		frames += 1
		var t := float(Time.get_ticks_msec() - t0) / 1000.0
		# the listener circles the sounds: distance and pan of every voice change every audio tick
		audio.set_listener(Vector3(cos(t) * 120.0, sin(t) * 120.0, 0.0), Vector3(-sin(t), cos(t), 0.0))
		if t >= next_play:
			next_play += 0.05
			var h: int = audio.play_sound_at(events[plays % events.size()], Vector3(float(plays % 9) * 30.0 - 120.0, float(plays % 7) * 30.0 - 90.0, 0.0))
			plays += 1
			started += int(h >= FIRST_HANDLE)
			if h >= FIRST_HANDLE:
				handles.append(h)
			# keep the pool turning over: the oldest looping sound stops
			if handles.size() > 30:
				audio.remove_event(handles.pop_front())
		var playing := 0
		for i in players.size():
			if players[i].playing:
				playing += 1
			if players[i].volume_db != 0.0:
				player_volume_writes += 1
		for a in amps:
			amp_values[snappedf(a.volume_db, 0.5)] = true
		max_playing = maxi(max_playing, playing)
		await process_frame
	var elapsed := float(Time.get_ticks_msec() - t0) / 1000.0
	print("  %d frames in %.1f s (%.0f / s), %d sounds asked, %d started, at most %d voices at once, %d distinct amplify volumes" % [
		frames, elapsed, frames / elapsed, plays, started, max_playing, amp_values.size()])
	_check(frames / elapsed > 200.0, "the main loop ran uncapped (%.0f frames / s)" % (frames / elapsed))
	_check(max_playing >= 8, "the pool was busy (at most %d voices at once)" % max_playing)
	_check(amp_values.size() >= 8, "the core's volumes reached the voices through the amplify effects (%d distinct)" % amp_values.size())
	_check(player_volume_writes == 0, "no pooled AudioStreamPlayer left 0 dB (%d samples did)" % player_volume_writes)
	var report: Dictionary = audio.get_report()
	_check(report.play_failures == 0 or report.errors.all(func(e): return String(e).contains("pool is exhausted")),
		"no play failure other than a full pool: " + str(report.errors.slice(0, 5)))

	audio.shutdown()
	audio.queue_free()
	await process_frame
	await process_frame
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
