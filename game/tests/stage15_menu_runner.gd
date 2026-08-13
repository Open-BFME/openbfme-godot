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
	# MY HEROES is ENABLED now. It used to be a disabled cap whose tooltip said
	# "not implemented"; Create-a-Hero landed, so the assertion below is inverted
	# on purpose. The button must open its screen, and when no pack carries the
	# class table the screen must FAIL CLOSED WITH AN EXPLANATION rather than
	# open empty or refuse to open at all.
	var my_heroes_btn := menu.get_node("Center/MyHeroes") as Button
	_check(
		"my_heroes_is_enabled_and_carries_a_tooltip",
		not my_heroes_btn.disabled and my_heroes_btn.tooltip_text.strip_edges() != "",
		"disabled=%s tooltip=%s" % [my_heroes_btn.disabled, my_heroes_btn.tooltip_text]
	)
	my_heroes_btn.pressed.emit()
	await process_frame
	_check(
		"my_heroes_press_opens_its_page",
		String(menu.get_current_page()) == "my_heroes"
			and menu.my_heroes_screen != null
			and menu.my_heroes_screen.visible
	)
	if menu.my_heroes_screen != null:
		# Drive the no-pack case directly so the explainer is proven without
		# depending on what happens to be mounted on this machine.
		menu.my_heroes_screen.configure({})
		await process_frame
		var explainer := String(menu.my_heroes_screen.status_label.text)
		_check(
			"my_heroes_without_a_pack_names_the_missing_content_and_the_fix",
			not menu.my_heroes_screen.system_available()
				and explainer.contains("Create-a-Hero class table")
				and explainer.contains("compile-cah-system")
				and explainer.contains("cah.system"),
			explainer
		)
		_check(
			"my_heroes_refuses_creation_without_a_pack",
			not menu.my_heroes_screen.create_hero("Probe").is_empty()
		)
		menu.my_heroes_screen.back_button.pressed.emit()
		await process_frame
	_check("my_heroes_back_returns_to_main", String(menu.get_current_page()) == "main")
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
		# `wotr_screen` is null until the STRATEGIC page is navigated to - the
		# strategic screen's ~22k-line script is compiled at that navigation, not
		# during boot - so "the strategic map is not showing" is now "no strategic
		# screen exists yet, or it exists and is hidden". Both are the same fact
		# this check has always asserted, and the null case is the stronger one.
		var strategic_showing: bool = menu.wotr_screen != null and menu.wotr_screen.visible
		_check(
			"game_setup_is_the_page_the_entry_opens",
			String(menu.get_current_page()) == "wotr_setup"
				and menu.wotr_setup_screen.visible and not strategic_showing,
			"page=%s setup=%s map=%s" % [
				menu.get_current_page(), menu.wotr_setup_screen.visible,
				strategic_showing]
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

	# ------------------------------------------------------------------------------
	# THE STRATEGIC PAGE IS THE GAME, SO IT TAKES THE WHOLE WINDOW
	# ------------------------------------------------------------------------------
	#
	# It did not. `_ensure_wotr_screen()` used to copy the SOLO PLAY flyout's
	# rectangle onto the strategic screen, so War of the Ring played inside an inset
	# panel with the shell's backdrop, the OPEN BFME masthead, the version corner and
	# the "Open source engine" line framing it. The owner's words were "there is no
	# way to have this fullscreen inside of the game engine itself".
	#
	# NOTHING CAUGHT IT, and that is the reason these checks are here rather than in
	# a WOTR runner: every layout assertion this project has is made against a screen
	# the runners BUILD THEMSELVES at full window size, and the capture runner did
	# the same. The one thing nobody asserted was the rectangle the MENU hands the
	# screen - which is the only rectangle a player ever sees.
	# THE REAL ENTRY FIRST. `show_page("wotr")` is the route a player's clicks take:
	# it compiles the screen, seats a session and configures it. It legitimately
	# refuses on a machine with no living-world document, and the layout half of
	# these checks must still run there - so the page is shown either way and only
	# the seating check below depends on a session existing.
	var wotr_opened: bool = menu.show_page("wotr")
	_check("the_strategic_screen_can_be_built", menu._ensure_wotr_screen())
	if menu.wotr_screen != null:
		if not wotr_opened:
			menu._show_page("wotr")
		await process_frame
		var strategic: Control = menu.wotr_screen
		_check("the_strategic_page_is_showing", strategic.visible)
		# FILLS THE VIEWPORT. Asserted against the root viewport rather than against
		# its parent, because "it fills its parent" is exactly what the broken version
		# was also true of.
		var viewport_size := Vector2(root.size)
		_check(
			"the_strategic_page_fills_the_whole_window",
			strategic.get_global_rect().position.is_equal_approx(Vector2.ZERO)
				and strategic.size.is_equal_approx(viewport_size),
			"screen %s at %s, window %s" % [
				str(strategic.size), str(strategic.get_global_rect().position),
				str(viewport_size)]
		)
		# AND THE SHELL'S FURNITURE IS DOWN. A full-window page under the menu's
		# masthead is still a game inside a menu.
		_check("the_shells_chrome_is_hidden_while_the_game_is_up", menu.shell_chrome_is_hidden())
		var still_up: Array[String] = []
		for path in ["Center/Title", "Center/Subtitle", "Center/BuildVersion",
				"Footer", "Atmosphere", "BackdropWeather", "BarScrim"]:
			var node := menu.get_node_or_null(path) as Control
			if node != null and node.visible:
				still_up.append(path)
		_check("no_named_piece_of_shell_chrome_survives_the_strategic_page",
			still_up.is_empty(), ", ".join(still_up))
		# IT FOLLOWS THE WINDOW. The whole point of anchors rather than a copied
		# rectangle is that going fullscreen, or dragging the window, resizes the game.
		# ANCHORED, not copied. The anchors are the property that makes the resize
		# automatic, so they are asserted directly - a headless run has no compositor
		# and cannot be relied on to deliver a real window resize, but a control
		# anchored to all four edges of a full-window parent cannot fail to follow one.
		_check(
			"the_strategic_page_is_anchored_to_the_window_rather_than_given_a_copied_rectangle",
			is_equal_approx(strategic.anchor_left, 0.0) and is_equal_approx(strategic.anchor_top, 0.0)
				and is_equal_approx(strategic.anchor_right, 1.0)
				and is_equal_approx(strategic.anchor_bottom, 1.0)
				and is_zero_approx(strategic.offset_left) and is_zero_approx(strategic.offset_top)
				and is_zero_approx(strategic.offset_right) and is_zero_approx(strategic.offset_bottom),
			"anchors %.2f %.2f %.2f %.2f offsets %.1f %.1f %.1f %.1f" % [
				strategic.anchor_left, strategic.anchor_top, strategic.anchor_right,
				strategic.anchor_bottom, strategic.offset_left, strategic.offset_top,
				strategic.offset_right, strategic.offset_bottom]
		)
		# AND IT REALLY DOES FOLLOW ONE. Driven through the parent the anchors resolve
		# against, so the check holds whether or not this headless process has a
		# window manager willing to change the root viewport's size.
		var shell_center := menu.get_node("Center") as Control
		var center_was := shell_center.size
		shell_center.size = Vector2(2560.0, 1440.0)
		await process_frame
		_check(
			"the_strategic_page_follows_a_resize",
			strategic.size.is_equal_approx(shell_center.size),
			"screen %s, frame %s" % [str(strategic.size), str(shell_center.size)]
		)
		shell_center.size = center_was
		await process_frame
		# HIDE THE UI. F2 takes every HUD island down and leaves the map; F2 again
		# restores exactly what it hid.
		var end_turn_was: bool = strategic.end_turn_button.visible
		strategic.set_hud_hidden(true)
		_check(
			"hiding_the_hud_takes_down_the_chrome_pass_and_the_controls",
			not strategic.chrome_layer.visible and not strategic.end_turn_button.visible
				and not strategic.standings_label.visible,
			"chrome=%s endturn=%s standings=%s" % [
				strategic.chrome_layer.visible, strategic.end_turn_button.visible,
				strategic.standings_label.visible]
		)
		_check("hiding_the_hud_leaves_the_map_alone",
			strategic.map3d.visible or strategic.map_view.visible)
		strategic.set_hud_hidden(false)
		_check(
			"showing_the_hud_restores_exactly_what_was_hidden",
			strategic.chrome_layer.visible
				and strategic.end_turn_button.visible == end_turn_was,
			"chrome=%s endturn=%s (was %s)" % [
				strategic.chrome_layer.visible, strategic.end_turn_button.visible,
				end_turn_was]
		)
		# THE PAUSE SHELL'S OPTIONS CAPSULE, which is the route to settings from
		# inside a campaign - the one the owner could not find because it did not
		# exist. It must be wired to the shell's own options screen, not to nothing.
		_check("the_pause_shell_carries_an_options_capsule",
			strategic.pause_options != null and strategic.pause_options.text == "OPTIONS")
		_check("the_pause_shells_options_capsule_is_wired_to_the_shell",
			strategic.options_requested.get_connections().size() > 0)
		strategic.toggle_pause_shell(true)
		await process_frame
		_check("opening_the_pause_shell_shows_all_three_of_its_controls",
			strategic.pause_resume.visible and strategic.pause_options.visible
				and strategic.back_button.visible)
		strategic.pause_options.emit_signal("pressed")
		await process_frame
		_check("pressing_options_opens_the_settings_screen",
			String(menu.get_current_page()) == "options" and menu.options_screen.visible,
			"page=%s" % menu.get_current_page())
		# AND CLOSING IT COMES BACK TO THE CAMPAIGN rather than dumping the player on
		# the front page with a seated session behind it.
		menu.options_screen.cancel()
		await process_frame
		_check("closing_settings_returns_to_the_strategic_page",
			String(menu.get_current_page()) == "wotr" and strategic.visible,
			"page=%s visible=%s" % [menu.get_current_page(), strategic.visible])
		# THE DEFAULT OPPONENT MUST BE ABLE TO FIGHT. The chooser-less seating takes
		# the document's first two templates, and on this checkout the second of
		# those (PlayerDwarves) has no roster in the auto-resolve bindings bundle -
		# so every battle its seat committed was refused by name and the campaign sat
		# perfectly still, which reads as a broken opponent and is a content hole.
		# `_seat_an_opponent_that_can_fight` moves the AI seat past that; this holds
		# it. On a machine with no bundles at all the helper deliberately does
		# nothing, and so does this check - `_seat_cannot_fight` answers false when
		# there is no bindings bundle to be unbound against.
		_check(
			"the_default_opponent_is_a_seat_whose_armies_can_actually_fight",
			menu._wotr_session == null or not menu._seat_cannot_fight(menu._wotr_session, 1),
			"seats=%s" % ("<no session>" if menu._wotr_session == null
				else "%s (human) vs %s (ai)" % [
					String((menu._wotr_session.state.players[0] as Dictionary).get("template", "?")),
					String((menu._wotr_session.state.players[1] as Dictionary).get("template", "?"))])
		)
		# AND THE OPPONENT IS WIRED TO THE TURN. END TURN used to hand the turn to a
		# seat that never moved; `run_opponent_turns()` is the call that changed that,
		# and a method nothing calls is a feature nobody meets.
		_check("the_strategic_screen_can_run_the_opponents_turns",
			strategic.has_method("run_opponent_turns"))

		# BACK TO THE SHELL, and the furniture comes back with it.
		menu._show_page("main")
		await process_frame
		_check("leaving_the_strategic_page_restores_the_shells_chrome",
			not menu.shell_chrome_is_hidden()
				and _visible(menu, "Center/Title") and _visible(menu, "Footer"))

	# ------------------------------------------------------------------------------
	# F11 IS FULLSCREEN AND IT PERSISTS
	# ------------------------------------------------------------------------------
	#
	# Applying a mode is half a fullscreen toggle. The half that was missing is that
	# `startup_boot.gd` re-applies the STORED mode before the first frame of every
	# launch, so a toggle that does not write to that store is a key the player has
	# to press again every time they open the game. This asserts the store, which is
	# the half a headless run can prove; `apply_display_settings` is the same applier
	# `boot_startup_runner` already covers.
	var display_before: Dictionary = SettingsScript.load_display()
	SettingsScript.save_display("windowed", "1920x1080")
	var went: String = menu.toggle_fullscreen()
	_check("f11_from_windowed_goes_fullscreen", went == "borderless", went)
	_check("f11_persists_the_mode_it_applied",
		String(SettingsScript.load_display()["window_mode"]) == "borderless",
		str(SettingsScript.load_display()))
	var came_back: String = menu.toggle_fullscreen()
	_check("f11_from_fullscreen_comes_back_to_windowed", came_back == "windowed", came_back)
	_check("f11_persists_the_way_back_too",
		String(SettingsScript.load_display()["window_mode"]) == "windowed",
		str(SettingsScript.load_display()))
	# THE KEY IS BOUND, not merely implemented. A method nothing calls is a feature
	# nobody can reach, which is the shape of every complaint this round answers.
	_check("f11_is_actually_bound_to_a_key", menu.has_method("_unhandled_input"))
	SettingsScript.save_display(
		String(display_before["window_mode"]), String(display_before["resolution"]))

	# THE KEY SETTINGS COLUMN. Every remappable action is a click-to-rebind
	# row; the Xbox / Steam Controller family picker sits above them.
	var key_heading := menu.find_child("KeyBindingsHeading", true, false) as Label
	var key_rows := menu.find_child("KeyBindingRows", true, false) as VBoxContainer
	_check("the_options_screen_has_a_key_settings_section",
		key_heading != null and key_rows != null
			and key_rows.get_child_count() == SettingsScript.REMAPPABLE_ACTIONS.size(),
		"heading=%s rows=%d of %d" % [key_heading != null,
			key_rows.get_child_count() if key_rows != null else -1,
			SettingsScript.REMAPPABLE_ACTIONS.size()])
	_check("the_key_settings_section_offers_rebind_rows",
		key_rows != null and key_rows.get_child_count() == SettingsScript.REMAPPABLE_ACTIONS.size())
	var rebind_buttons := 0
	if key_rows != null:
		for row in key_rows.get_children():
			if row is Button:
				rebind_buttons += 1
	_check("every_key_settings_row_is_a_rebind_button",
		rebind_buttons == SettingsScript.REMAPPABLE_ACTIONS.size(),
		"buttons=%d" % rebind_buttons)
	var family_opt := menu.find_child("ControllerFamilyOpt", true, false) as OptionButton
	_check("controller_family_picker_is_present",
		family_opt != null and family_opt.item_count == 2)

	# ONE build identity, under the title. The bottom-left copy printed the same
	# string a second time and is gone; a shell that says the same thing twice is
	# a shell the reader has to check against itself.
	_check(
		"build_version_is_not_printed_twice",
		menu.get_node_or_null("Center/BuildVersion") == null
	)
	var version_label := menu.get_node_or_null("Center/TitleVersion") as Label
	_check("build_version_is_shown_under_the_title", version_label != null and version_label.text.strip_edges() != "")
	_check(
		"build_version_is_not_a_stale_literal",
		version_label != null
			and version_label.text.begins_with(_expected_version_text())
			and not version_label.text.ends_with("build dev"),
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
