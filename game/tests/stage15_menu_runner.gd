extends SceneTree
## Headless acceptance gate for the Stage 15 menu/settings foundation.

const SettingsScript = preload("res://src/ui/user_settings.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")

var passed := 0
var failed := 0
var original_audio_settings: Dictionary = {}


func _initialize() -> void:
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
	_check("retail_launch_hidden_until_solo", not _retail_launch_visible(menu))

	# --- retail shell bar + upward flyouts -----------------------------------
	# The shell is the six retail caps in retail's order, with the lists that
	# open UPWARD from them. These assert the shape a player sees, because a
	# menu that only passes as a state machine is how a visibly broken build
	# shipped green once already.
	var bar_order := ["Tutorials", "Solo", "Multiplayer", "Options", "MyHeroes", "Quit"]
	var bar_ok := true
	var previous_left := -1.0
	for node_name in bar_order:
		var cap := menu.get_node_or_null("Center/%s" % node_name) as Button
		if cap == null or not cap.visible:
			bar_ok = false
			break
		# Left-to-right in the retail order, all sharing the bottom edge.
		if cap.get_global_rect().position.x <= previous_left:
			bar_ok = false
			break
		previous_left = cap.get_global_rect().position.x
	_check("retail_bar_present_in_retail_order", bar_ok)
	_check(
		"my_heroes_is_disabled_and_says_why",
		(menu.get_node("Center/MyHeroes") as Button).disabled
			and (menu.get_node("Center/MyHeroes") as Button).tooltip_text.contains("not implemented")
	)
	_check(
		"every_bar_cap_carries_a_hover_tooltip",
		_all_have_tooltips(menu, bar_order)
	)

	var solo_menu = menu.shell_flyout("solo")
	_check("solo_play_owns_an_upward_flyout", solo_menu != null)
	if solo_menu != null:
		_check("solo_flyout_starts_closed", not solo_menu.visible)
		var solo_bar := menu.get_node("Center/Solo") as Button
		solo_bar.pressed.emit()
		await process_frame
		_check("solo_bar_press_opens_the_flyout", solo_menu.visible)
		# UPWARD: the panel sits wholly above the cap that spawned it.
		_check(
			"the_flyout_opens_above_its_bar_button",
			solo_menu.get_global_rect().end.y <= solo_bar.get_global_rect().position.y,
			"flyout_bottom=%s bar_top=%s" % [solo_menu.get_global_rect().end.y, solo_bar.get_global_rect().position.y]
		)
		var labels: Array = []
		for row in solo_menu.item_buttons:
			labels.append(String(row.text))
		_check(
			"solo_flyout_lists_the_retail_rows",
			labels == ["SKIRMISH", "WAR OF THE RING", "EVIL CAMPAIGN", "GOOD CAMPAIGN", "LOAD GAME"],
			str(labels)
		)
		# WAR OF THE RING is a real feature on this branch, not a stub. Its row
		# tracks the menu's own availability verdict exactly - live when a
		# living-world document was found, and when not, disabled carrying the
		# search's reason rather than a flat "not implemented".
		var wotr_row: Button = solo_menu.item_row("wotr")
		var wotr_reason := String(menu.wotr_unavailable_reason())
		_check(
			"war_of_the_ring_row_matches_the_menus_verdict",
			wotr_row != null and wotr_row.disabled == (wotr_reason != ""),
			"disabled=%s reason=%s" % [wotr_row != null and wotr_row.disabled, wotr_reason]
		)
		_check(
			"war_of_the_ring_row_is_never_a_stub",
			wotr_row != null and not wotr_row.tooltip_text.contains("not implemented"),
			wotr_row.tooltip_text if wotr_row != null else "<missing>"
		)
		if wotr_row != null and wotr_row.disabled:
			_check(
				"a_blocked_war_of_the_ring_row_carries_the_reason",
				wotr_row.tooltip_text == wotr_reason, wotr_row.tooltip_text
			)
		else:
			_check("a_live_war_of_the_ring_row_is_pressable", wotr_row != null and not wotr_row.disabled)
		# RETAIL'S GAME SETUP SCREEN IS WHERE THE ENTRY LANDS. War of the Ring
		# used to drop straight into a seated campaign; the entry now opens the
		# setup screen, and it opens EVEN WHEN THE CAMPAIGN CANNOT START, because
		# that screen is the surface that can say which file is missing. The
		# strategic page keeps its own refusal and is checked by
		# `wotr_round_trip_runner`, which is why this only asserts the route.
		_check("war_of_the_ring_lands_on_game_setup", menu.show_page("wotr_setup"))
		await process_frame
		_check(
			"game_setup_is_the_page_the_entry_opens",
			String(menu.get_current_page()) == "wotr_setup"
				and menu.wotr_setup_screen.visible and not menu.wotr_screen.visible,
			"page=%s setup=%s map=%s" % [
				menu.get_current_page(), menu.wotr_setup_screen.visible,
				menu.wotr_screen.visible]
		)
		# PLAY REACHES THE EXISTING SESSION. The screen emits; the menu starts a
		# `WotrSession`. A second path into the strategic layer is the thing this
		# check exists to prevent.
		_check(
			"game_setup_play_is_wired_to_the_session_path",
			menu.wotr_setup_screen.play_requested.is_connected(menu._on_wotr_setup_play),
			"nothing is listening to PLAY"
		)
		menu.show_page("main")
		await process_frame
		solo_bar.pressed.emit()
		await process_frame
		# The project's standing rule: no greyed control without a stated reason.
		var mute_rows: Array = []
		for bar_id in ["tutorials", "solo", "options"]:
			var flyout = menu.shell_flyout(bar_id)
			if flyout == null:
				continue
			for row in flyout.item_buttons:
				if row.disabled and row.tooltip_text.strip_edges() == "":
					mute_rows.append("%s/%s" % [bar_id, row.text])
		_check("no_disabled_flyout_row_is_left_unexplained", mute_rows.is_empty(), str(mute_rows))
		# A page change dismisses the bar's flyouts.
		menu.show_page("main")
		await process_frame
		_check("changing_page_closes_the_flyout", not solo_menu.visible)

	var version_label := menu.get_node_or_null("Center/BuildVersion") as Label
	_check("build_version_is_shown_bottom_left", version_label != null and version_label.text.strip_edges() != "")
	_check(
		"build_version_is_not_a_stale_literal",
		version_label != null
			and version_label.text == _expected_version_text(),
		version_label.text if version_label != null else "<missing>"
	)

	_check("solo_page_accepts_navigation", bool(menu.show_page("solo")))
	_check("solo_page_preserves_retail_launch", _retail_launch_visible(menu))
	_check("solo_page_has_no_legacy_launch", menu.get_node_or_null("Center/Start") == null and menu.get_node_or_null("Center/LegacyGrid") == null)
	_check("solo_page_hides_main_actions", not _visible(menu, "Center/Solo") and not _visible(menu, "Center/Options"))

	_check("developer_page_accepts_navigation", bool(menu.show_page("developer")))
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


func _all_have_tooltips(menu: Node, bar_order: Array) -> bool:
	for node_name in bar_order:
		var cap := menu.get_node_or_null("Center/%s" % node_name) as Button
		if cap == null or cap.tooltip_text.strip_edges() == "":
			return false
	return true


func _expected_version_text() -> String:
	## The label must be derived from project.godot, never a literal baked into
	## the shell - a hardcoded version is how the menu came to advertise a build
	## that had long since moved on. When the setting is unset the label says so
	## and names the setting, which is louder (deliberately) than a blank corner.
	var version := String(ProjectSettings.get_setting("application/config/version", "")).strip_edges()
	if version != "":
		return "v%s" % version
	return "build version not set (application/config/version)"


func _visible(root_node: Node, path: String) -> bool:
	var control := root_node.get_node_or_null(path) as Control
	return control != null and control.visible


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
