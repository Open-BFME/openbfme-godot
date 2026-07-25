extends SceneTree
## Headless acceptance gate for the Stage 15 menu/settings foundation.

const SettingsScript = preload("res://src/ui/user_settings.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")

var passed := 0
var failed := 0
var original_audio_settings: Dictionary = {}


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE15_MENU_RUNNER")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	original_audio_settings = SettingsScript.load_audio()
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame

	_check("repository_theme_applied", menu.theme != null and menu.theme.has_stylebox("normal", "Button"))
	_check("main_page_is_default", String(menu.get_current_page()) == "main")
	_check("main_page_is_uncluttered", _visible(menu, "Center/Solo") and _visible(menu, "Center/Options") and _visible(menu, "Center/Quit"))
	_check("proof_stages_hidden_by_default", _all_stages_visible(menu, false))
	_check("retail_launch_hidden_until_solo", not _retail_launch_visible(menu))

	_check("solo_page_accepts_navigation", bool(menu.show_page("solo")))
	_check("solo_page_preserves_retail_launch", _retail_launch_visible(menu))
	_check("solo_page_has_no_legacy_launch", menu.get_node_or_null("Center/Start") == null and menu.get_node_or_null("Center/LegacyGrid") == null)
	_check("solo_page_hides_main_actions", not _visible(menu, "Center/Solo") and not _visible(menu, "Center/Options"))

	_check("developer_page_accepts_navigation", bool(menu.show_page("developer")))
	_check("developer_page_preserves_all_stages", _all_stages_visible(menu, true))
	_check("developer_page_preserves_tests", _visible(menu, "Center/Tests"))
	_check("developer_page_is_not_main_clutter", not _visible(menu, "Center/Solo") and not _visible(menu, "DeveloperAccess"))
	_check("unknown_page_rejected", not bool(menu.show_page("missing")) and String(menu.get_current_page()) == "developer")

	_check("options_page_accepts_navigation", bool(menu.show_page("options")))
	# The retail-style options screen owns audio now; the same persistence
	# contract is asserted against its controls and ACCEPT flow.
	var music_slider := menu.find_child("MusicSlider", true, false) as HSlider
	var voice_slider := menu.find_child("SoundFxSlider", true, false) as HSlider
	var mute_toggle := menu.find_child("MuteToggle", true, false) as CheckButton
	var accept_button := menu.find_child("AcceptButton", true, false) as Button
	_check("audio_controls_present", music_slider != null and music_slider.visible and voice_slider != null and voice_slider.visible and mute_toggle != null and mute_toggle.visible and accept_button != null)
	music_slider.value = 0.37
	voice_slider.value = 0.63
	mute_toggle.button_pressed = true
	accept_button.emit_signal("pressed")
	var persisted: Dictionary = SettingsScript.load_audio()
	_check("music_setting_persists", is_equal_approx(float(persisted["music_volume"]), 0.37), str(persisted))
	_check("voice_sfx_setting_persists", is_equal_approx(float(persisted["voice_sfx_volume"]), 0.63), str(persisted))
	_check("mute_setting_persists", bool(persisted["muted"]), str(persisted))

	var detached_audio = AudioScript.new()
	detached_audio.set_music_volume(0.21)
	detached_audio.set_voice_sfx_volume(0.42)
	detached_audio.set_muted(true)
	_check("audio_setters_are_headless_safe", is_equal_approx(detached_audio.get_music_volume(), 0.21) and is_equal_approx(detached_audio.get_voice_volume(), 0.42) and detached_audio.is_muted())
	detached_audio.free()

	var retail_audio = AudioScript.new()
	root.add_child(retail_audio)
	var closure_without_pack: bool = retail_audio.configure("user://stage15-empty-pack", false)
	_check("empty_test_pack_does_not_fake_closure", not closure_without_pack)
	_check("retail_audio_loads_persisted_levels", is_equal_approx(retail_audio.get_music_volume(), 0.37) and is_equal_approx(retail_audio.get_voice_sfx_volume(), 0.63) and retail_audio.is_muted())
	_check("mute_applies_to_players", is_equal_approx(retail_audio.music_player.volume_db, SettingsScript.SILENT_DB) and is_equal_approx(retail_audio.voice_player.volume_db, SettingsScript.SILENT_DB))
	retail_audio.set_muted(false)
	_check("unmute_restores_independent_levels", is_equal_approx(retail_audio.music_player.volume_db, SettingsScript.volume_to_db(0.37)) and is_equal_approx(retail_audio.voice_player.volume_db, SettingsScript.volume_to_db(0.63)))

	retail_audio.dispose()
	retail_audio.free()
	menu.queue_free()
	await process_frame
	_finish()


func _retail_launch_visible(menu: Node) -> bool:
	## The PLAY button lives inside the GAME SETUP flyout after the setup
	## rework; visibility still tracks the solo page exactly.
	var button := menu.find_child("Retail", true, false) as Control
	return button != null and button.is_visible_in_tree()


func _visible(root_node: Node, path: String) -> bool:
	var control := root_node.get_node_or_null(path) as Control
	return control != null and control.visible


func _all_stages_visible(menu: Node, expected: bool) -> bool:
	for stage_index in range(1, 10):
		if _visible(menu, "Center/Stage%d" % stage_index) != expected:
			return false
	return true


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("STAGE15_MENU PASS %s" % name)
	else:
		failed += 1
		printerr("STAGE15_MENU FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _restore_original_settings() -> void:
	if original_audio_settings.is_empty():
		return
	SettingsScript.save_audio(
		float(original_audio_settings["music_volume"]),
		float(original_audio_settings["voice_sfx_volume"]),
		bool(original_audio_settings["muted"])
	)


func _finish() -> void:
	_restore_original_settings()
	print("STAGE15_MENU_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
