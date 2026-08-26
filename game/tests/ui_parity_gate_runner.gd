extends SceneTree
## UI PARITY GATE + CAMERA (Q90). Walks the major screens the way a player does,
## photographs each one for the owner's eyeball diff against reference/INDEX.md
## (REF-01..52, retail BFME2 1.06 at 2560x1440), and asserts a STRUCTURAL
## checklist per screen against the live scene tree - node/label presence, not
## pixel diffing, because a Godot render will never be pixel-identical to a
## retail jpg.
##
## FAILING CHECKS ARE FINDINGS. A red that honestly names a missing retail
## element (retail's Voice/Ambient/Movie sliders, the pause menu's SETTINGS and
## LOAD entries, the MULTIPLAYER flyout) is the deliverable of this gate; do not
## weaken a check to make it pass.
##
## Screens walked, in order (PNGs land in workspace/review/ui-parity/):
##   01-main-menu-idle      REF-07 (bar caps REF-01..06)
##   02-solo-flyout         REF-02/03
##   03-options-flyout      REF-05
##   04-game-setup-map      REF-09
##   05-game-setup-rules    REF-11
##   06-settings            REF-13/14
##   07-in-game-hud         REF-25/52 (default skirmish setup, default map)
##   08-pause-menu          REF-16
##
## Usage (WINDOWED - a camera needs a window, refuses under --headless):
##   Godot_v4.7-stable_win64_console.exe --path game --script res://tests/ui_parity_gate_runner.gd
## Optional user args: -- --out <absolute dir>
##
## Companion runner: ui_parity_capture_runner.gd is a pure no-assert HUD camera
## that boots the slice directly; THIS one owns the menu screens and the gate.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

## Task-mandated hard deadline: the harness kills itself at 240 s.
const DEADLINE_MS := 240_000
const WINDOW_SIZE := Vector2i(1280, 720)  # 0.5 x the 2560x1440 reference captures
const MENU_SETTLE_FRAMES := 30
const HUD_SETTLE_FRAMES := 120
const MAX_WAIT_FRAMES := 3000

var _watchdog := RunnerWatchdogScript.new()
var _out_dir := ""
var _passed := 0
var _failed := 0
var _captured: Array[String] = []


func _initialize() -> void:
	if DisplayServer.get_name() == "headless":
		# Under --headless nothing is ever presented and get_image() photographs
		# nothing (or stalls). Refuse loudly instead of writing black rectangles.
		printerr("UI_PARITY_GATE REFUSED: run WINDOWED (drop --headless); this runner photographs the screen")
		quit(2)
		return
	# The slice must read the menu's recorded GameState, not stale env overrides.
	for env_name in ["OPENBFME_SLICE_FACTION", "OPENBFME_SLICE_MAP", "OPENBFME_MP",
			"OPENBFME_MP_ADDRESS", "OPENBFME_MP_PORT", "OPENBFME_STARTER_ARMY",
			"OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	_out_dir = _argument("--out",
		ProjectSettings.globalize_path("res://../workspace/review/ui-parity"))
	DirAccess.make_dir_recursive_absolute(_out_dir)
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(WINDOW_SIZE)
	root.size = WINDOW_SIZE
	root.title = "OpenBFME - UI parity gate"
	_watchdog.start(self, "UI_PARITY_GATE", DEADLINE_MS)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(_passed, _failed))
	print("[ui-parity] window=%s out=%s" % [str(WINDOW_SIZE), _out_dir])
	call_deferred("_run")


func _run() -> void:
	# ---- boot the shell -------------------------------------------------------
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("REF-07", "boot_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var menu := packed.instantiate()
	root.add_child(menu)
	var frames := 0
	while not bool(menu.get("_skirmish_options_ready")) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	_check("REF-07", "menu_became_ready", bool(menu.get("_skirmish_options_ready")),
		"frames=%d" % frames)
	if not bool(menu.get("_skirmish_options_ready")):
		print("[ui-parity] BLOCKED: menu never became ready; every later screen is unreachable")
		_finish()
		return

	# ---- 01 main menu idle (REF-07; bar caps REF-01..06) ----------------------
	await _settle(MENU_SETTLE_FRAMES)
	await _capture("01-main-menu-idle")
	_check_bar_button(menu, "REF-02", "Center/Solo", "SOLO PLAY")
	_check_bar_button(menu, "REF-01", "Center/Multiplayer", "MULTIPLAYER")
	_check_bar_button(menu, "REF-04", "Center/Tutorials", "TUTORIALS")
	_check_bar_button(menu, "REF-05", "Center/Options", "OPTIONS")
	_check_bar_button(menu, "REF-06", "Center/Quit", "QUIT")
	var backdrop := menu.get("backdrop_art") as CanvasItem
	_check("REF-07", "main_menu_has_backdrop_art",
		backdrop != null and backdrop.visible)

	# ---- 02 SOLO PLAY flyout (REF-02) -----------------------------------------
	menu._toggle_shell_flyout("solo")
	await _settle(MENU_SETTLE_FRAMES)
	await _capture("02-solo-flyout")
	var solo_flyout_panel := menu.shell_flyout("solo") as Control
	_check("REF-02", "solo_flyout_opens",
		solo_flyout_panel != null and solo_flyout_panel.visible)
	var solo_texts := _tree_texts(solo_flyout_panel)
	_check("REF-02", "solo_flyout_lists_SKIRMISH", _any_contains(solo_texts, "SKIRMISH"))
	_check("REF-02", "solo_flyout_lists_WAR_OF_THE_RING", _any_contains(solo_texts, "WAR OF THE RING"))
	_check("REF-02", "solo_flyout_lists_EVIL_CAMPAIGN", _any_contains(solo_texts, "EVIL CAMPAIGN"))
	_check("REF-02", "solo_flyout_lists_GOOD_CAMPAIGN", _any_contains(solo_texts, "GOOD CAMPAIGN"))
	_check("REF-02", "solo_flyout_lists_LOAD_GAME", _any_contains(solo_texts, "LOAD GAME"))
	menu._close_shell_flyouts()

	# Retail's MULTIPLAYER bar cap opens a flyout (REPLAYS/NETWORK/ONLINE,
	# REF-01). The engine registers flyouts only for tutorials/solo/options, so
	# this names the gap rather than pretending the cap alone is parity.
	var mp_flyout: Variant = menu.shell_flyout("multiplayer")
	_check("REF-01", "multiplayer_flyout_exists_with_REPLAYS_NETWORK_ONLINE",
		mp_flyout != null, "no multiplayer flyout registered")

	# TUTORIALS flyout structure (REF-04) - gated off the panel without a shot.
	var tut_texts := _tree_texts(menu.shell_flyout("tutorials") as Control)
	_check("REF-04", "tutorials_flyout_lists_BASIC_TUTORIAL", _any_contains(tut_texts, "BASIC TUTORIAL"))
	_check("REF-04", "tutorials_flyout_lists_ADVANCED_TUTORIAL", _any_contains(tut_texts, "ADVANCED TUTORIAL"))
	_check("REF-04", "tutorials_flyout_lists_WOTR_TUTORIAL", _any_contains(tut_texts, "WAR OF THE RING TUTORIAL"))

	# ---- 03 OPTIONS flyout (REF-05) -------------------------------------------
	menu._toggle_shell_flyout("options")
	await _settle(MENU_SETTLE_FRAMES)
	await _capture("03-options-flyout")
	var options_flyout_panel := menu.shell_flyout("options") as Control
	var opt_texts := _tree_texts(options_flyout_panel)
	_check("REF-05", "options_flyout_opens",
		options_flyout_panel != null and options_flyout_panel.visible)
	_check("REF-05", "options_flyout_lists_SETTINGS", _any_contains(opt_texts, "SETTINGS"))
	_check("REF-05", "options_flyout_lists_CUSTOM_SETTINGS", _any_contains(opt_texts, "CUSTOM SETTINGS"))
	_check("REF-05", "options_flyout_lists_CREDITS", _any_contains(opt_texts, "CREDITS"))
	# Retail's CUSTOM SETTINGS opens the 10-slider screen (REF-15); here the
	# item is a disabled stub, which is a named gap, not a crash.
	_check("REF-15", "custom_settings_item_is_reachable",
		_button_with_text_enabled(options_flyout_panel, "CUSTOM SETTINGS"),
		"item present but disabled; no CUSTOM SETTINGS screen exists")
	menu._close_shell_flyouts()

	# ---- 04 skirmish GAME SETUP, MAP tab (REF-09) -----------------------------
	var opened: bool = menu.show_page("solo")
	_check("REF-09", "game_setup_page_opens", opened)
	var setup := menu.get_node_or_null("Center/SoloFlyout")
	_check("REF-09", "game_setup_panel_exists", setup != null)
	if setup != null:
		await _settle(MENU_SETTLE_FRAMES)
		await _capture("04-game-setup-map")
		var map_tab := setup.get("map_tab_btn") as Button
		var rules_tab := setup.get("rules_tab_btn") as Button
		_check("REF-09", "game_setup_has_MAP_tab", map_tab != null and map_tab.text == "MAP")
		_check("REF-09", "game_setup_has_RULES_tab", rules_tab != null and rules_tab.text == "RULES")
		var map_rows := setup.get("map_rows") as Array
		_check("REF-09", "map_list_has_rows", map_rows != null and map_rows.size() > 0,
			"rows=%d" % (map_rows.size() if map_rows != null else -1))
		var setup_texts := _tree_texts(setup as Control)
		for header in ["Army", "Hero", "Team", "Color", "Handicap"]:
			_check("REF-09", "player_rows_show_%s_column" % header,
				_any_contains(setup_texts, header))
		var preview := setup.get("preview_image") as TextureRect
		_check("REF-09", "map_preview_shows_an_image",
			preview != null and preview.visible and preview.texture != null,
			"no parchment preview texture" if preview == null or preview.texture == null else "")
		var desc := setup.get("description_label") as Label
		_check("REF-09", "map_description_is_populated",
			desc != null and desc.text.strip_edges() != "")

		# ---- 05 GAME SETUP, RULES tab (REF-11) --------------------------------
		if rules_tab != null:
			rules_tab.pressed.emit()
		await _settle(MENU_SETTLE_FRAMES)
		await _capture("05-game-setup-rules")
		var rules_texts := _tree_texts(setup as Control)
		_check("REF-11", "rules_tab_shows_Initial_Resources",
			setup.get("initial_resources_opt") != null and _any_contains(rules_texts, "Initial Resources"))
		_check("REF-11", "rules_tab_shows_Command_Point_Factor",
			setup.get("cp_factor_opt") != null and _any_contains(rules_texts, "Command Point Factor"))
		_check("REF-11", "rules_tab_shows_Allow_Custom_Heroes",
			setup.get("custom_heroes_toggle") != null and _any_contains(rules_texts, "Allow Custom Heroes"))
		_check("REF-11", "rules_tab_shows_Allow_Ring_Heroes",
			setup.get("ring_heroes_toggle") != null and _any_contains(rules_texts, "Allow Ring Heroes"))
		_check("REF-11", "rules_tab_has_RESET", setup.get("rules_reset_btn") != null)
		# Retail's Hero dropdown ships six Create-a-Hero presets
		# (Hadhod/Berethor/Idrial/Krashnak/Thrugg/Morwen, REF-11); the engine
		# lists only the player's saved heroes, so a fresh profile shows "-".
		var hero_dropdowns := setup.get("hero_dropdowns") as Array
		var hero_items: int = (hero_dropdowns[0] as OptionButton).item_count \
			if hero_dropdowns != null and hero_dropdowns.size() > 0 else -1
		_check("REF-11", "hero_dropdown_offers_default_hero_presets", hero_items > 1,
			"items=%d; retail ships 6 CAH presets, engine lists saved heroes only" % hero_items)
		if map_tab != null:
			map_tab.pressed.emit()

	# ---- 06 SETTINGS screen (REF-13/14) ---------------------------------------
	_check("REF-13", "settings_page_opens", menu.show_page("options"))
	await _settle(MENU_SETTLE_FRAMES)
	await _capture("06-settings")
	var options_screen := menu.get("options_screen") as Control
	_check("REF-13", "settings_screen_exists",
		options_screen != null and options_screen.visible)
	var s_texts := _tree_texts(options_screen)
	_check("REF-13", "settings_has_Resolution_dropdown",
		options_screen != null and options_screen.get("resolution_opt") != null)
	_check("REF-14", "settings_has_Graphics_preset_dropdown",
		options_screen != null and options_screen.get("preset_opt") != null)
	_check("REF-13", "settings_has_Show_All_Health_Bars", _any_contains(s_texts, "Show All Health Bars"))
	_check("REF-13", "settings_has_Scroll_Speed", _any_contains(s_texts, "Scroll Speed"))
	_check("REF-13", "settings_has_Music_slider",
		options_screen != null and options_screen.get("music_slider") != null)
	_check("REF-13", "settings_has_Sound_FX_slider",
		options_screen != null and options_screen.get("sfx_slider") != null)
	# Retail 1.06 carries five audio sliders + brightness (REF-13/14); the
	# engine carries two sliders and a mute toggle. Named gaps:
	_check("REF-13", "settings_has_Voice_slider", _any_contains(s_texts, "Voice"))
	_check("REF-13", "settings_has_Ambient_slider", _any_contains(s_texts, "Ambient"))
	_check("REF-13", "settings_has_Movie_slider", _any_contains(s_texts, "Movie"))
	_check("REF-14", "settings_has_Brightness_slider", _any_contains(s_texts, "Brightness"))
	_check("REF-13", "settings_has_CANCEL", _any_contains(s_texts, "CANCEL"))
	_check("REF-13", "settings_has_RESET_TO_DEFAULTS", _any_contains(s_texts, "RESET TO DEFAULTS"))
	_check("REF-13", "settings_has_ACCEPT", _any_contains(s_texts, "ACCEPT"))
	menu.show_page("main")

	# ---- launch the default skirmish ------------------------------------------
	# Default setup: two rows, default map pick - the state REF-52 baselines.
	var gate_error := String(menu.retail_launch_error())
	_check("REF-52", "default_skirmish_gate_opens", gate_error == "", gate_error)
	var recorded: bool = gate_error == "" and bool(menu.apply_skirmish_selection())
	_check("REF-52", "default_skirmish_launch_recorded", recorded)
	menu.queue_free()
	await process_frame
	await process_frame
	if not recorded:
		print("[ui-parity] BLOCKED: skirmish launch refused (%s); HUD and pause screens unreachable" % gate_error)
		_finish()
		return

	var slice = load("res://scenes/retail_vertical_slice.tscn").instantiate()
	root.add_child(slice)
	var boot_frames := 0
	while not bool(slice.get("ready_ok")) and String(slice.get("failure_reason")) == "" \
			and boot_frames < MAX_WAIT_FRAMES:
		boot_frames += 1
		await process_frame
	var slice_up: bool = bool(slice.get("ready_ok"))
	_check("REF-52", "match_boots", slice_up,
		"failure=%s frames=%d" % [String(slice.get("failure_reason")), boot_frames])

	# ---- 07 in-game HUD (REF-25/52) -------------------------------------------
	await _settle(HUD_SETTLE_FRAMES)
	await _capture("07-in-game-hud")
	if slice_up:
		var hud = slice.get("hud")
		_check("REF-52", "hud_exists", hud != null)
		if hud != null:
			var dock := (hud as Node).get_node_or_null("PalantirDock") as CanvasItem
			_check("REF-52", "hud_has_palantir_dock", dock != null and dock.visible)
			var minimap := hud.get("minimap") as CanvasItem
			_check("REF-52", "hud_has_minimap_radar", minimap != null and minimap.visible)
			var resources := hud.get("resource_label") as Label
			_check("REF-25", "hud_shows_resource_counter",
				resources != null and resources.text.strip_edges() != "",
				resources.text if resources != null else "<missing>")
			var cp := hud.get("command_points_label") as Label
			_check("REF-25", "hud_shows_command_points_as_used_over_cap",
				cp != null and cp.text.contains("/"),
				cp.text if cp != null else "<missing>")
			_check("REF-25", "hud_has_event_feed", hud.get("event_feed") != null)
			var orbs: Variant = hud.get("orb_buttons")
			_check("REF-22", "hud_has_powers_orb_button",
				typeof(orbs) == TYPE_DICTIONARY and (orbs as Dictionary).has("powers"))
			_check("REF-52", "hud_has_command_panel",
				(hud as Node).get_node_or_null("CommandPanel") != null)

		# ---- 08 pause menu (REF-16) -------------------------------------------
		slice.toggle_escape_menu()
		await _settle(MENU_SETTLE_FRAMES)
		await _capture("08-pause-menu")
		var pause_panel: Control = hud.get("pause_panel") if hud != null else null
		_check("REF-16", "pause_menu_opens_on_escape",
			pause_panel != null and pause_panel.visible)
		var p_texts := _tree_texts(pause_panel)
		_check("REF-16", "pause_menu_has_RESUME", _any_contains(p_texts, "Resume"))
		_check("REF-16", "pause_menu_has_SAVE", _any_contains(p_texts, "Save"))
		_check("REF-16", "pause_menu_has_RESTART", _any_contains(p_texts, "Restart"))
		_check("REF-16", "pause_menu_has_EXIT",
			_any_contains(p_texts, "Quit") or _any_contains(p_texts, "Main Menu"))
		# Retail's pause menu carries SETTINGS and LOAD entries (REF-16); the
		# engine's carries neither. Named gaps:
		_check("REF-16", "pause_menu_has_SETTINGS", _any_contains(p_texts, "Settings"))
		_check("REF-16", "pause_menu_has_LOAD", _any_contains_word(p_texts, "Load"))
		slice.toggle_escape_menu()
		await process_frame
	else:
		print("[ui-parity] BLOCKED: slice never became ready; HUD/pause checks recorded as the match_boots FAIL above")

	if slice.has_method("cleanup_for_test"):
		slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	_finish()


# ---------------------------------------------------------------------------- #

func _capture(shot_name: String) -> void:
	await RenderingServer.frame_post_draw
	var image: Image = root.get_texture().get_image()
	var path := "%s/%s.png" % [_out_dir, shot_name]
	var err := image.save_png(path)
	if err == OK:
		_captured.append(path)
		print("[ui-parity] captured %s (%dx%d)" % [path, image.get_width(), image.get_height()])
	else:
		_check("REF-00", "capture_%s_saved" % shot_name, false, "save_png err=%d" % err)


func _settle(count: int) -> void:
	for _i in range(count):
		await process_frame


func _check_bar_button(menu: Node, ref: String, path: String, wanted: String) -> void:
	var button := menu.get_node_or_null(path) as Button
	_check(ref, "menu_bar_shows_%s" % wanted.replace(" ", "_"),
		button != null and button.visible and button.text == wanted,
		button.text if button != null else "<missing node %s>" % path)


func _tree_texts(from: Control) -> Array[String]:
	## Every text string in a subtree - Button/Label/CheckButton captions -
	## regardless of per-node visibility (structure, not paint).
	var out: Array[String] = []
	if from == null:
		return out
	var stack: Array[Node] = [from]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		var text: Variant = node.get("text")
		if typeof(text) == TYPE_STRING and String(text).strip_edges() != "":
			out.append(String(text))
	return out


func _any_contains(texts: Array[String], needle: String) -> bool:
	var lowered := needle.to_lower()
	for text in texts:
		if text.to_lower().contains(lowered):
			return true
	return false


func _any_contains_word(texts: Array[String], word: String) -> bool:
	## Whole-word variant: "Load" must not be satisfied by "Download" etc.
	var pattern := RegEx.create_from_string("(?i)\\b%s\\b" % word)
	for text in texts:
		if pattern.search(text) != null:
			return true
	return false


func _button_with_text_enabled(from: Control, wanted: String) -> bool:
	if from == null:
		return false
	var stack: Array[Node] = [from]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if node is Button and (node as Button).text.to_upper().contains(wanted.to_upper()):
			return not (node as Button).disabled
	return false


func _check(ref: String, name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("UI_PARITY PASS [%s] %s" % [ref, name])
	else:
		_failed += 1
		print("UI_PARITY FAIL [%s] %s%s" % [ref, name,
			" (%s)" % detail if detail != "" else ""])


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback


func _finish() -> void:
	print("[ui-parity] wrote %d capture(s) to %s" % [_captured.size(), _out_dir])
	print("UI_PARITY_GATE_RESULT passed=%d failed=%d" % [_passed, _failed])
	_watchdog.stop()
	quit(0 if _failed == 0 else 1)
