## Real BFME1 audio regression: RANDOM paths and localized voice files.
## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_audio_test.gd
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 AUDIO SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var audio = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	assert(audio.boot(fs, {"seed": 7}).ok)
	for event in ["Gui_ShellMapMouseOver", "GondorSoldierVoiceSelect", "GondorSoldierVoiceMove"]:
		var info: Dictionary = audio.get_event_info(event)
		assert((info.control_bits & 64) == 0, event)
		# tolerance: any positive duration confirms the installed sample resolves and decodes; clip lengths vary.
		assert(audio.get_audio_length_ms(event) > 0, event + " must resolve and decode")
	# tolerance: handles below 5 are reserved failure/filter values; allocated playback handles start at 5.
	assert(audio.play_shell_sound("Gui_ShellMapMouseOver") >= 5)
	for i in 10: await process_frame
	var report: Dictionary = audio.get_report()
	assert(report.played == 1 and report.play_failures == 0, str(report))
	audio.shutdown()
	audio.free()
	print("BFME1 AUDIO PASS: UI sound and localized selection/move voices decode; UI playback succeeds")
	quit()
