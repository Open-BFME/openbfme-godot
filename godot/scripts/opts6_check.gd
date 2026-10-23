## Lane OPTS-6: game.gd --opts6-check (the menus at the window's size, --opts6-size=WxH) and --opts6-game (with --auto: the menus over a live game).
## Everything is driven through Godot's input (mouse events pushed into the viewport, key and joypad events through Input.parse_input_event) and judged
## by its effect read back from the engine:
##   * hit areas: every control of every tab of the OpenBFME settings (the rows' gadgets, the tabs), Options.apt's bottom bar and the main menu's buttons
##     are pressed (or, for the buttons that would leave the screen, pointed at) just inside and just outside each edge of the shape they draw: inside
##     has the effect (a slider moves, a box ticks, a list opens, an entry takes the keys, a tab shows, RESET TO DEFAULTS resets, a button lights up),
##     outside has none. The shape is the one drawn: the gadgets' own draw commands (gadget_draw_list), the tabs' selection bar, the buttons' art bounds.
##     A stated margin: a check box's window keeps 3 stage pixels above and below its box (OpenBFMEOptionsPage::kCheckBoxMargin). Zero controls, a
##     control with no drawn shape or a tab without controls fails.
##   * keyboard: main-menu groups/entries use arrows and Enter; settings use Tab, arrows, Enter, Space, PageUp / PageDown and Esc; the effects
##     (the tab shown, a slider's value and the music volume, a box, a choice, an open list closed by Esc with the page still up, Esc = CANCEL).
##   * controller: the same with joypad events (D-pad, A, B, LB / RB, the right stick, the right trigger).
##   * stale focus: the focus of a closed screen is not kept by the next one.
## run_game: the quit menu's Options over a live game: the quit menu's and the Palantir's buttons cannot be hit or clicked through it, the arrows move
## the menu's focus and not the camera, no world decoration is drawn over it, closing it brings the quit menu back and closing that the Palantir.
## Prints "OPTS6 <name>: ok" / "OPTS6 <name>: FAIL <why>" and "OPTS6 RESULT: <n> ok, <m> failed"; tests/opts6_test.gd runs it. --opts6-shots=DIR
## (windowed) saves pictures.
extends RefCounted

## The REQUIRED inventories: what each screen must offer, written here by name and NOT read from the live page (a list built from what the page shows can
## never miss a control: Sol's review removed Retail.Brightness from the checker's inventory and the checker still passed with 8 controls). A control
## the page lacks, or one it has that these lists do not name, fails by name. The Options rows are the keys of OpenBFMEOptionsPage (retail rows
## "Retail.<gadget>", the OpenBFME extras "OpenBFME<name>"); the menus' controls are the Apt buttons' clip names. OPTS6_SABOTAGE proves the checker fails
## closed (tests/opts6_test.gd): "inventory:<name>" drops a name from these lists, "page:<name>" hides it from the page's side.
const REQUIRED_TABS := ["Display", "Graphics", "Audio", "Camera", "Interface", "Online", "Performance", "Controls"]
const REQUIRED_ROWS := {
	"Display": ["OpenBFMEWidescreen", "Retail.Brightness", "max_fps", "render_scale", "resolution", "scaler", "show_fps", "vsync", "window_mode"],
	"Graphics": ["OpenBFMEAnisotropic", "OpenBFMEAntiAliasing", "OpenBFMELook", "OpenBFMESoftParticles", "Retail.AdvancedOption0", "Retail.AdvancedOption1",
		"Retail.AdvancedOption2", "Retail.AdvancedOption3", "Retail.AdvancedOption4", "Retail.AdvancedOption5", "Retail.AdvancedOption6", "Retail.AdvancedOption7",
		"Retail.AdvancedOption8", "Retail.Detail"],
	"Audio": ["OpenBFMEAudioDevice", "OpenBFMEMasterVolume", "OpenBFMEMuteUnfocused", "Retail.AmbientVolume", "Retail.EAX3", "Retail.HighAudioQuality",
		"Retail.MovieVolume", "Retail.MusicVolume", "Retail.SoundFxVolume", "Retail.VoiceVolume"],
	"Camera": ["OpenBFMEConfineMouse", "OpenBFMEEdgeScroll", "OpenBFMEFreeCamera", "OpenBFMEInvertDragScroll", "OpenBFMEMaxZoomOut", "OpenBFMERotateSpeed",
		"Retail.ScrollSpeed"],
	"Interface": ["AlternateMouseSetup", "OpenBFMEGameClock", "OpenBFMEHealthBars", "OpenBFMEMainMenuStyle", "OpenBFMEPingKey", "OpenBFMEPingWheel", "OpenBFMEUIScale", "Retail.HealthBars"],
	"Online": ["OpenBFMEShareResults", "OpenBFMEShowPing", "Retail.OnlineIp", "Retail.OnlinePortNum", "Retail.RefreshNat", "Retail.SendDelay",
		"Social.challenges", "Social.hidden", "Social.invites", "Social.presence", "Social.requests"], # ONLINEMENU-1: who may ask, invite, challenge, see you
	"Performance": ["OpenBFMEUberPerformance"],
	"Controls": [],
}
const REQUIRED_BAR := ["accept", "cancel", "reset"]
const REQUIRED_MAIN := ["MultiPlayNav", "MyHeroes", "OptionsNav", "QuitMainMenu", "SoloPlayNav", "corner"] # the corner button (a 194 x 105 box) has no name
const REQUIRED_SUB := {
	"SoloPlayNav": ["BonusCampaign", "Expansion1Campaign", "LoadGame", "Skirmish", "WarOfTheRing"],
	"MultiPlayNav": ["LocalNetwork", "Online", "Replay"],
	"OptionsNav": ["AdvancedSettings", "Credits", "Settings"],
}
## The main menu's nav buttons by click effect: nav button -> the sub-menu button that must be pressable after a press (the sub-menu opened). The retail Hit
## records of the three nav buttons are skewed quads: a press in the corner of the box they leave open (the right and top inside points of the box
## probes, found by the hit test and listed as notes by the pointer check) opens nothing; MyHeroes (opens Create-a-Hero, a screen with its own loading),
## QuitMainMenu (a press ends the process) and the unnamed corner button are covered by the pointer check alone, by name, with the reason
const MAIN_NAV := {"SoloPlayNav": "Skirmish", "MultiPlayNav": "Replay", "OptionsNav": "Settings"}
const MAIN_NAV_SKEWED := []
const MAIN_POINTER_ONLY := ["MyHeroes", "QuitMainMenu", "corner"]
const CHECK_MARGIN := 3.0 # stage pixels (OpenBFMEOptionsPage::kCheckBoxMargin)
const IN := 2.0           # window pixels inside an edge
const OUT := 3.0          # window pixels outside an edge (plus a stated margin)

var game: Node
var shell: Node
var screen: CanvasLayer
var shots := ""
var ok_count := 0
var fail_count := 0
var size_tag := ""
var probed := 0
var lap_start := 0


func sabotage_name(kind: String) -> String:
	var s := OS.get_environment("OPTS6_SABOTAGE")
	return s.substr(kind.length() + 1) if s.begins_with(kind + ":") else ""


## the inventory a screen must offer (the list above, minus what OPTS6_SABOTAGE=inventory:<name> drops)
func required(list: Array) -> Array:
	var out := list.duplicate()
	out.erase(sabotage_name("inventory"))
	out.sort()
	return out


## what the screen shows (minus what OPTS6_SABOTAGE=page:<name> hides from this side)
func shown(list: Array) -> Array:
	var out := list.duplicate()
	out.erase(sabotage_name("page"))
	out.sort()
	return out


## the controls of a screen against its required inventory, by name, both ways
func check_inventory(label: String, want: Array, have: Array) -> void:
	var missing := PackedStringArray()
	var extra := PackedStringArray()
	for n in want:
		if not have.has(n):
			missing.append(String(n))
	for n in have:
		if not want.has(n):
			extra.append(String(n))
	check("%s: the controls are exactly the required inventory (%d)" % [label, want.size()], missing.is_empty() and extra.is_empty() and have.size() == want.size(),
		"missing %s; not in the required list %s" % [str(missing), str(extra)])


## the name of an Apt button for the inventories: its clip's name (QuitMainMenu.bttn -> QuitMainMenu, MultiPlayNav.instance80 -> MultiPlayNav), "corner" for
## the unnamed one on the level
func button_leaf(path: String) -> String:
	var parts := path.split(".")
	var i := parts.size() - 1
	while i > 0 and (parts[i].begins_with("instance") or parts[i].begins_with("bttn")):
		i -= 1
	return "corner" if i == 0 else parts[i]


func lap(name: String) -> void:
	print("OPTS6 TIME %s: %.0f s" % [name, (Time.get_ticks_msec() - lap_start) / 1000.0])
	lap_start = Time.get_ticks_msec()


func run(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	shots = p_shots
	lap_start = Time.get_ticks_msec()
	await _size()
	await game._wait_main_ready()
	await game._step(60)
	if OS.get_environment("OPTS6_ONLY") != "keys":
		await main_menu_hits()
		lap("main menu pointer")
		if OS.get_environment("OPTS6_ONLY") != "inventory":
			await main_menu_effects()
			lap("main menu click effects")
		if not await open_options():
			return finish()
		await settings_hits()
		lap("settings tabs")
		if OS.get_environment("OPTS6_ONLY") == "inventory":
			return finish()
		await bar_hits()
		lap("bottom bar")
		check("hit areas: controls probed at %s (fail closed)" % size_tag, probed > 0, "none")
		await click_bar("cancel")
		await game._step(90)
	await keyboard_walk()
	lap("keyboard walk")
	await controller_walk()
	lap("controller walk")
	await other_screens()
	lap("other screens")
	return finish()


func run_game(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	shots = p_shots
	await _size()
	check("a live game with its HUD", game._hud != null and game._state == game.State.PLAYING, "state %d" % game._state)
	var shell_level: int = shell.shell_top_level()
	# a unit of ours selected and looked at (setup: its health bar is a world decoration to hide)
	var own: Array = game._world.get_player_objects(game._local_name)
	if not own.is_empty():
		game._hud.camera_look_at(Vector2(own[0].x, own[0].y))
		game._hud.select_object(int(own[0].id))
	await game._wait_seconds(2.0)
	# what is behind the menus: every hittable button of every level now (the Palantir), then the quit menu's
	var behind := hittable_all(-1)
	var levels := PackedStringArray()
	for level in 12:
		levels.append("%d:%d" % [level, shell.list_buttons(level).size()])
	check("the game shows buttons to click (the Palantir)", not behind.is_empty(), "buttons per level %s, shell level %d" % [", ".join(levels), shell_level])
	var decorations := int(game._hud.get_frame_timings().get("icon_ops", 0))
	check("the world draws its decorations before the menu (health bars: AllHealthBars = yes)", decorations > 0, "icon_ops %d" % decorations)
	# input that began before the menu opened ends with it (Sol's review: hold Right, open the pause menu, let Right go under it: the camera kept moving)
	await held_input_cases()
	await game._press_escape()
	await game._wait_seconds(2.0)
	var quit: Dictionary = shell.quit_menu({"kind": "state"})
	var quit_buttons := []
	for b in shell.list_buttons(quit.get("level", -1)):
		if b.hittable:
			quit_buttons.append(b)
	check("the quit menu shows its 6 buttons (OPTIONS, SAVE, LOAD, RESTART, EXIT, RESUME GAME)", quit_buttons.size() == 6, str(quit_buttons.size()))
	# keyboard in the quit menu: the arrows focus its buttons
	await key(KEY_DOWN)
	var nav: Dictionary = shell.menu_nav_state()
	check("the arrows focus a quit menu button", nav.level == quit.level and str(nav.focus).begins_with("_level%d." % quit.level) and nav.frame, str(nav.focus))
	await quit_menu_effects(quit)
	if not await game._click_quit("Options"):
		check("the quit menu's Options", false, "no Options button")
		return finish()
	for i in 40:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(10)
	check("Options over the game", top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	await game._step(30)
	await shot("opts6-in-game-options")
	# 1. nothing behind can be hit: the quit menu's buttons and the Palantir's
	var still := PackedStringArray()
	for b in quit_buttons + behind:
		for x in shell.list_buttons(int(String(b.path).get_slice(".", 0).trim_prefix("_level"))):
			if x.path == b.path and x.hittable:
				still.append(String(b.path))
	check("behind Options nothing can be hit (the quit menu's %d buttons, the Palantir's %d)" % [quit_buttons.size(), behind.size()], still.is_empty(), ", ".join(still))
	# ... nor clicked: a click where each was does nothing behind (the quit menu's actions, the shell's screens); the page may take it (a row there)
	var log_before: int = game._quit_log.size()
	var stack_before: PackedStringArray = shell.shell_stack()
	var menu_rects := []
	for t in shell.menu_nav_state().get("targets", []):
		menu_rects.append(t.rect)
	var clicked := 0
	for b in quit_buttons + behind:
		var mine := false
		for r in menu_rects:
			mine = mine or (r as Rect2).grow(4).has_point(Vector2(b.x, b.y))
		if mine:
			continue # Options' own control is there (CANCEL over the Palantir's corner): a click on it is the menu's
		clicked += 1
		var tab_before: String = screen.current_tab()
		await click_stage(Vector2(b.x, b.y))
		if screen.current_tab() != tab_before:
			await click_tab_named(tab_before)
	check("a click where SAVE / LOAD / RESTART / EXIT / RESUME and the Palantir's buttons are (%d places) does nothing behind Options" % clicked, clicked > 0 and game._quit_log.size() == log_before
		and shell.shell_stack() == stack_before and screen.is_open(), "quit log %s, stack %s" % [str(game._quit_log.slice(log_before)), str(shell.shell_stack())])
	# 2. no world decoration over it
	await game._step(10)
	check("no health bar or other world decoration is drawn over Options", int(game._hud.get_frame_timings().get("icon_ops", -1)) == 0,
		str(game._hud.get_frame_timings().get("icon_ops")))
	# 3. the keys are the menu's: the arrows move its focus, not the camera; the HUD counted them as the menu's
	var cam: Vector2 = game._hud.get_camera().position
	var keys_before := int(game._hud.get_state().get("keys", {}).get("menu_key_events", 0))
	for k in [KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_LEFT, KEY_UP]:
		await key(k)
	await game._step(20)
	nav = shell.menu_nav_state()
	check("over the game the arrows move the menu's focus (%s)" % str(nav.focus), str(nav.focus).begins_with("tab:") or str(nav.focus).begins_with("row:"), str(nav))
	check("... and not the camera", game._hud.get_camera().position == cam, "%s -> %s" % [cam, game._hud.get_camera().position])
	check("... the HUD handed them to the menu", int(game._hud.get_state().get("keys", {}).get("menu_key_events", 0)) >= keys_before + 10, # tolerance: presses and releases
		str(game._hud.get_state().get("keys", {})))
	# 4. closing: Esc closes Options, the quit menu is back with EVERY button (the movie was unloaded under Options and is loaded again: RW 0x812A37 /
	# 0x812913), a second visit works (opened by a real click on the restored OPTIONS, closed by ACCEPT), then Esc closes the menu and the Palantir takes clicks again
	await key(KEY_ESCAPE)
	await game._wait_seconds(2.0)
	await shot("opts6-quit-menu-after-options")
	var lost := await quit_buttons_restored(quit_buttons, quit.level)
	check("Esc closes Options and the quit menu is back with all its %d buttons (OPTIONS too)" % quit_buttons.size(), not screen.is_open() and lost.is_empty(), "not back: " + ", ".join(lost))
	for visit in [2, 3]:
		var opened: bool = await game._click_quit("Options")
		for i in 40:
			if top() == "Options.apt" and screen.is_open():
				break
			await game._step(10)
		check("visit %d: the restored OPTIONS button opens the settings again (a real click)" % visit, opened and top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
		await game._step(30)
		if visit == 2:
			await key(KEY_ESCAPE) # CANCEL
		else:
			await click_bar("accept") # ACCEPT (no edit: the file stays as it was)
		await game._wait_seconds(2.0)
		lost = await quit_buttons_restored(quit_buttons, quit.level)
		check("visit %d: closed by %s, the quit menu is back with all its buttons" % [visit, "Esc" if visit == 2 else "ACCEPT"], not screen.is_open() and lost.is_empty(), ", ".join(lost))
	nav = shell.menu_nav_state()
	check("the quit menu does not keep the closed Options' focus", not str(nav.focus).begins_with("row:") and not str(nav.focus).begins_with("tab:"), str(nav.focus))
	await game._press_escape()
	await game._wait_seconds(1.0)
	var again := hittable_all(-1)
	var missing := PackedStringArray()
	for b in behind:
		var found := false
		for x in again:
			found = found or x.path == b.path
		if not found:
			missing.append(String(b.path))
	check("after the quit menu closes the Palantir's buttons take clicks again", missing.is_empty() and not game._quit_open, ", ".join(missing))
	check("the shell's level did not change", shell.shell_top_level() == shell_level, "")
	# with no menu up the keys are the game's again: the arrow scrolls the camera, the HUD hands nothing to the menus
	check("with no menu up nothing has the menus' input (the Palantir is the HUD)", not shell.menu_has_input(), str(shell.menu_nav_state().get("level")))
	var cam_before: Vector2 = game._hud.get_camera().position
	var menu_keys := int(game._hud.get_state().get("keys", {}).get("menu_key_events", 0))
	var down := InputEventKey.new()
	down.keycode = KEY_LEFT
	down.physical_keycode = KEY_LEFT
	down.pressed = true
	Input.parse_input_event(down)
	await game._wait_seconds(1.0)
	var up := down.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	await game._step(10)
	check("with no menu up the arrow scrolls the camera (%s -> %s)" % [cam_before, game._hud.get_camera().position], game._hud.get_camera().position != cam_before
		and int(game._hud.get_state().get("keys", {}).get("menu_key_events", 0)) == menu_keys, "keys " + str(game._hud.get_state().get("keys", {})) + ", paused " + str(game.get_tree().paused))
	return finish()


## every button of the quit menu is hittable again after a screen over it closed (matched by its clip: a pressed button's clip may hold a new instance); the
## names of those that are not
func quit_buttons_restored(quit_buttons: Array, level: int) -> PackedStringArray:
	var lost := PackedStringArray()
	for attempt in 40: # the movie loads again and plays its intro in
		lost.clear()
		var now: Array = shell.list_buttons(level)
		for q in quit_buttons:
			var clip := String(q.path).substr(0, String(q.path).rfind(".") + 1)
			var found := false
			for b in now:
				found = found or (b.hittable and String(b.path).begins_with(clip))
			if not found:
				lost.append("%s (at its centre: '%s')" % [q.path, shell.hit_test_stage(Vector2(q.x, q.y))])
		if lost.is_empty():
			break
		await game._step(10)
	return lost


## Click-effect coverage of the quit menu's six buttons (in a game): each is pressed just inside and just outside each edge of its hit box (the Hit records'
## box the window manager reports): inside has the button's effect, outside none. The effects: OPTIONS the settings, SAVE / LOAD the save screen, RESTART and
## EXIT their confirmation (a YES button appears), RESUME GAME closes the menu. Each probe starts from the restored menu (the reset: Esc / NO / Esc again).
## An outside point that lies inside another button's box is not a probe of this button (counted by name in the check's message).
func quit_menu_effects(quit: Dictionary) -> void:
	var boxes := {}
	for b in shell.list_buttons(quit.level):
		if b.hittable and b.has("hit_bounds"):
			for n in ["Options", "Save", "Load", "Restart", "ExitMission", "Resume"]:
				if String(b.path).contains("." + n + "."):
					boxes[n] = b.hit_bounds
	var inventory := ["ExitMission", "Load", "Options", "Restart", "Resume", "Save"]
	var have := boxes.keys()
	have.sort()
	check("the quit menu's required buttons all have a hit box to probe (%s)" % ", ".join(inventory), have == inventory, str(have))
	var bad := PackedStringArray()
	var skipped := PackedStringArray()
	for n in inventory:
		if not boxes.has(n):
			continue
		var box: Rect2 = boxes[n]
		for p in probes(window_rect(box), 0.0, 0.5):
			var point: Vector2 = p[0]
			var on_other := false
			for m in boxes:
				on_other = on_other or (m != n and window_rect(boxes[m]).has_point(point))
			if not p[1] and on_other:
				skipped.append("%s %s" % [n, p[2]]) # the point is on a neighbour's box
				continue
			await quit_menu_restored(quit)
			await move(Vector2(4, 4))
			await click(point)
			# the effect comes after the button's press animation (the movie fires its command ~20 frames after the release): an inside press is waited
			# for up to 5 s, an outside one is watched for 2.5 s
			var effect := false
			for i in (50 if p[1] else 25):
				effect = await quit_effect(n)
				if effect:
					break
				await game._step(6)
			if effect != p[1]:
				bad.append("%s %s: %s" % [n, p[2], "effect" if effect else "no effect"])
			await quit_effect_reset(n)
	await quit_menu_restored(quit)
	check("the quit menu's buttons at %s: each effect just inside every edge of its box and none just outside (%d probes on a neighbour skipped: %s)" % [
		size_tag, skipped.size(), ", ".join(skipped)], bad.is_empty(), "; ".join(bad))
	probed += 1


func quit_effect(name: String) -> bool:
	match name:
		"Options":
			return top() == "Options.apt" and screen.is_open()
		"Save", "Load":
			return top() == "SaveLoad.apt"
		"Restart", "ExitMission":
			return not game._quit_button("Yes").is_empty()
		"Resume":
			return not game._quit_open
	return false


func quit_effect_reset(name: String) -> void:
	match name:
		"Options":
			if top() == "Options.apt":
				await key(KEY_ESCAPE)
		"Save", "Load":
			if top() == "SaveLoad.apt":
				await game._press_escape()
		"Restart", "ExitMission":
			if not game._quit_button("No").is_empty():
				await game._click_quit("No")
		"Resume":
			if not game._quit_open:
				await game._press_escape()
	await game._wait_seconds(1.0)


## the quit menu is open, nothing over it, every button hittable (waits for the movie's intro)
func quit_menu_restored(quit: Dictionary) -> void:
	for i in 60:
		var n := 0
		for b in shell.list_buttons(quit.level):
			n += 1 if b.hittable and b.has("hit_bounds") else 0
		if game._quit_open and top() != "Options.apt" and top() != "SaveLoad.apt" and n >= 6 and game._quit_button("Yes").is_empty():
			return
		await game._step(10)


func camera_position() -> Vector2:
	return game._hud.get_camera().position


func key_event(code: int, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code as Key
	e.physical_keycode = code as Key
	e.pressed = pressed
	Input.parse_input_event(e)
	await game._step(2)


func mouse_event(p: Vector2, button: int, pressed: bool) -> void:
	var b := InputEventMouseButton.new()
	b.button_index = button as MouseButton
	b.pressed = pressed
	b.position = p
	b.global_position = p
	game.get_viewport().push_input(b)
	await game._step(2)


## Input that began before a menu opened (a held arrow key, a held right-button drag scroll) is cancelled when the menu opens, and its release under the menu
## changes nothing: the camera stands still behind the menu (measured before and after the release) and moves again by the next press. Each case has its
## control (the held input does move the camera).
func held_input_cases() -> void:
	# a held arrow key
	var c0 := camera_position()
	await key_event(KEY_RIGHT, true)
	await game._wait_seconds(1.0)
	var c_held := camera_position()
	check("control: a held Right scrolls the camera (%s -> %s)" % [c0, c_held], c_held != c0, "")
	await game._press_escape()
	await game._wait_seconds(2.0)
	var modal: bool = game._hud.get_state().get("keys", {}).get("modal", false)
	var m1 := camera_position()
	await game._wait_seconds(1.0)
	var m2 := camera_position()
	await key_event(KEY_RIGHT, false) # released under the menu
	await game._wait_seconds(1.5)
	var m3 := camera_position()
	check("a key held when the quit menu opens is cancelled: the camera stands behind the menu and after the release under it (%s %s %s)" % [m1, m2, m3],
		modal and m1 == m2 and m2 == m3, "modal %s" % modal)
	await game._press_escape() # the menu closes
	await game._wait_seconds(1.5)
	var a1 := camera_position()
	await game._wait_seconds(1.0)
	check("after the menu closes the released key moves nothing", camera_position() == a1 and not game._quit_open, str(camera_position()))
	# (the control's Right ran the camera to the map's edge, where it cannot go on: the later presses go Left)
	await key_event(KEY_LEFT, true)
	await game._wait_seconds(0.6)
	await key_event(KEY_LEFT, false)
	check("... and a new press scrolls again", camera_position() != a1, str(camera_position()))
	# a held right-button drag (leftwards: the control's Right ran the camera to the map's edge) (the mouse scroll of RotWK's LookAtTranslator): the pointer a little off the centre of the stage, away from the HUD
	var win: Vector2 = game.get_viewport().get_visible_rect().size
	var p0 := Vector2(win.x * 0.5, win.y * 0.4)
	var d0 := camera_position()
	await move(p0)
	await mouse_event(p0, MOUSE_BUTTON_RIGHT, true)
	await move(p0 - Vector2(80, 0))
	await game._wait_seconds(1.0)
	var d_held := camera_position()
	check("control: a held right-button drag scrolls the camera (%s -> %s)" % [d0, d_held], d_held != d0, "")
	await game._press_escape()
	await game._wait_seconds(2.0)
	var e1 := camera_position()
	await game._wait_seconds(1.0)
	var e2 := camera_position()
	await mouse_event(p0 - Vector2(80, 0), MOUSE_BUTTON_RIGHT, false) # released under the menu
	await game._wait_seconds(1.5)
	var e3 := camera_position()
	check("a right-button drag held when the quit menu opens is cancelled (%s %s %s)" % [e1, e2, e3], e1 == e2 and e2 == e3, "")
	await game._press_escape()
	await game._wait_seconds(1.5)
	var f1 := camera_position()
	await game._wait_seconds(1.0)
	check("after the menu closes the released button moves nothing", camera_position() == f1 and not game._quit_open, str(camera_position()))
	await move(Vector2(4, 4))


# ---------------------------------------------------------------------------------------------------------------------------------
# hit areas
# ---------------------------------------------------------------------------------------------------------------------------------
## The retail menus' buttons keep RotWK's own hit shapes (their movies' Hit records, which need not be the art's rectangle, and the nav buttons animate
## under the pointer). What must hold at every window size is that the pointer lands where the menus draw: each button of the main menu is pointed at
## just inside and just outside each edge of its Hit box, mapped to the window as the menus are drawn (stage_to_window), through the window's input; the
## pointer the Apt input then has must be that stage point (within 0.01 stage pixel), and at least one inside point of each button must be one its
## Hit records take (the points of a non-rectangular shape that differ are listed as a note).
func main_menu_hits() -> void:
	await game._step(30)
	var level: int = shell.shell_top_level()
	var buttons := []
	for b in shell.list_buttons(level):
		if b.hittable and b.has("hit_bounds"):
			buttons.append(b)
	var leaves := []
	for b in buttons:
		leaves.append(button_leaf(String(b.path)))
	check_inventory("the main menu", required(REQUIRED_MAIN), shown(leaves))
	if OS.get_environment("OPTS6_ONLY") == "inventory":
		return
	if OS.get_environment("OPTS6_INVENTORY") == "1":
		var names := []
		for b in buttons:
			names.append("%s %s hit=%s bounds=%s" % [b.path, int(b.state), b.hit_bounds, b.get("bounds")])
		print("OPTS6 INVENTORY main menu: ", " | ".join(names))
	check("main menu: its 6 buttons to probe (SOLO PLAY, MULTIPLAYER, OPTIONS, MY HEROES, QUIT and the corner one)", buttons.size() == 6, str(buttons.size()))
	var bad := PackedStringArray()
	var shapes := PackedStringArray()
	for b in buttons:
		var clip := String(b.path).get_slice(".instance", 0) if String(b.path).contains(".instance") else String(b.path)
		var taken_inside := 0
		var stage_box: Rect2 = b.hit_bounds
		for p in probes(window_rect(stage_box), 0.0, 0.5):
			await move(p[0])
			var pointer: Vector2 = shell.get_cursor_tooltip().get("pointer", Vector2(-1, -1))
			var want: Vector2 = shell.window_to_stage(p[0])
			if pointer.distance_to(want) > 0.01 or (shell.stage_to_window(pointer) as Vector2).distance_to(p[0]) > 0.05:
				bad.append("%s %s: the pointer is at %s, drawn there is %s" % [b.path, p[2], pointer, want])
			var taken := String(shell.hit_test_stage(want)).begins_with(clip)
			if taken and p[1]:
				taken_inside += 1
			if taken != p[1]:
				shapes.append("%s %s" % [b.path, p[2]])
		if taken_inside == 0:
			bad.append("%s: no inside point is taken" % b.path)
		probed += 1
	if not shapes.is_empty():
		print("OPTS6 note: retail Hit shapes not filling their box at: ", ", ".join(shapes))
	check("main menu at %s: the pointer lands where each of %d buttons is drawn (its Hit box's edges)" % [size_tag, buttons.size()], bad.is_empty(), "; ".join(bad))
	await move(Vector2(4, 4))
	await game._step(120)


func main_menu_effects() -> void:
	await game._step(30)
	var level: int = shell.shell_top_level()
	var boxes := {}
	for b in shell.list_buttons(level):
		if b.hittable and b.has("hit_bounds"):
			boxes[button_leaf(String(b.path))] = b
	var have := boxes.keys()
	have.sort()
	var covered := MAIN_NAV.keys() + MAIN_POINTER_ONLY
	covered.sort()
	check("the main menu's click-effect probes and its pointer-only buttons cover exactly its buttons", have == covered, "buttons %s, covered %s" % [str(have), str(covered)])
	var bad := PackedStringArray()
	for nav in MAIN_NAV:
		if not boxes.has(nav):
			continue
		var sub: String = MAIN_NAV[nav]
		if OS.get_environment("OPTS6_INVENTORY") == "1":
			# diagnosis (OPTS6_INVENTORY=1): how far above the Hit box a press still opens the sub-menu, and what the Apt hit test answers there
			var wr := window_rect(boxes[nav].hit_bounds)
			for dist in [0.0, 2.0, 4.0, 6.0, 10.0, 16.0, 24.0]:
				await main_menu_closed(sub)
				await move(rest_window())
				var pt := Vector2(wr.get_center().x, wr.position.y - dist)
				await click(pt)
				var op := false
				for i in 25:
					op = main_sub_open(level, sub)
					if op:
						break
					await game._step(6)
				print("OPTS6 DIAG %s: a press %.0f px above the Hit box %s opens %s; the Apt hit test there: '%s'; the box %s, the drawn bounds %s" % [nav, dist, str(wr), str(op),
					shell.hit_test_stage(shell.window_to_stage(pt)), str(boxes[nav].hit_bounds), str(boxes[nav].get("bounds"))])
		for p in probes(window_rect(boxes[nav].hit_bounds), 0.0, 0.5):
			if p[2] == "top outside":
				continue # the nav clip's over-state hit area reaches up over its sub-menu strip (measured: a press up to 24 px above the box opens it): named, not probed
			await main_menu_closed(sub)
			await move(rest_window())
			await click(p[0])
			var opened := false
			for i in (40 if p[1] else 20):
				opened = main_sub_open(level, sub)
				if opened:
					break
				await game._step(6)
			var want: bool = p[1] and not (MAIN_NAV_SKEWED.has(p[2]))
			if opened != want:
				bad.append("%s %s: the sub-menu %s" % [nav, p[2], "opened" if opened else "stayed closed"])
		probed += 1
	await main_menu_closed("Skirmish")
	await main_menu_closed("Replay")
	await main_menu_closed("Settings")
	check("main menu at %s: SOLO PLAY / MULTIPLAYER / OPTIONS open their sub-menu by a press just inside every edge (but the skewed corners) and not just outside" % size_tag,
		bad.is_empty(), "; ".join(bad))


func main_sub_open(level: int, sub: String) -> bool:
	for b in shell.list_buttons(level):
		if b.hittable and b.get("seen", true) and String(b.path).contains("." + sub + "."):
			return true
	return false


## every sub-menu closed: the pointer away from them (the movie closes a sub-menu when the pointer reaches its click-away area)
func main_menu_closed(sub: String) -> void:
	var level: int = shell.shell_top_level()
	for i in 40:
		if not main_sub_open(level, "Skirmish") and not main_sub_open(level, "Replay") and not main_sub_open(level, "Settings"):
			return
		await move(rest_window())
		await game._step(15)


## the pointer's resting place between presses on the front-end screens: no control is there (stage 512, 200; the corner button of the main menu is at the
## top left and takes the pointer there)
func rest_window() -> Vector2:
	return shell.stage_to_window(Vector2(512, 200))


func settings_hits() -> void:
	for tab in screen.TABS:
		await click_tab_named(tab)
		var rows: Dictionary = page().get("rows", {})
		check_inventory("the %s tab" % tab, required(REQUIRED_ROWS.get(tab, [])), shown(rows.keys()))
		if OS.get_environment("OPTS6_ONLY") == "inventory":
			continue
		if OS.get_environment("OPTS6_INVENTORY") == "1":
			var inv := []
			for key in rows:
				inv.append("%s:%s:%s" % [key, rows[key].kind, "on" if rows[key].enabled else "off"])
			inv.sort()
			print("OPTS6 INVENTORY %s: %s | buttons %s | tabs %d" % [tab, ", ".join(inv), str(page().get("buttons", {}).keys()), page().get("tabs", []).size()])
		var n := 0
		var bad := PackedStringArray()
		for key in rows:
			var r: Dictionary = rows[key]
			if not r.enabled or not r.has("rect") or not ["choice", "check", "slider", "entry"].has(r.kind):
				continue
			await move(Vector2(4, 4)) # the shape at rest: a pointed slider adds ZH's hover outline 2 stage pixels around its bar (W3D slider image draw)
			var drawn := drawn_rect(r.rect)
			if drawn.size == Vector2.ZERO:
				bad.append("%s: no drawn shape" % key)
				continue
			if OS.get_environment("OPTS6_DEBUG") == key:
				print("OPTS6 debug %s: stage %s window %s drawn %s" % [key, r.rect, window_rect(r.rect), drawn])
				for c in shell.gadget_draw_list():
					if c.has("root_stage_rect") and (c.root_stage_rect as Rect2).is_equal_approx(r.rect):
						print("OPTS6 debug   ", c.kind, " ", c.window, " ", c.rect, " ", c.image)
			bad.append_array(await probe_row(key, r, drawn))
			n += 1
			probed += 1
		var greyed := 0
		for key in rows:
			greyed += 0 if rows[key].enabled else 1
		if tab == "Controls":
			check("Controls: the hot key list is shown (read only)", page().has("list"), "")
		elif n == 0 and greyed > 0 and bad.is_empty():
			check("%s: every row is greyed here (%d), nothing to press" % [tab, greyed], true, "")
		else:
			check("%s at %s: %d controls react just inside their drawn shape and not just outside" % [tab, size_tag, n], n > 0 and bad.is_empty(), "; ".join(bad))
	check_inventory("the tab bar", required(REQUIRED_TABS), shown(Array(screen.TABS)))
	check("the page offers a tab bar box for each of the %d required tabs" % REQUIRED_TABS.size(), page().get("tabs", []).size() == REQUIRED_TABS.size(), str(page().get("tabs", []).size()))
	check_inventory("the bottom bar", required(REQUIRED_BAR), shown(page().get("buttons", {}).keys()))
	if OS.get_environment("OPTS6_ONLY") == "inventory":
		return
	# the tabs
	var tabs: Array = page().get("tabs", [])
	var bad_tabs := PackedStringArray()
	for i in tabs.size():
		var r: Rect2 = window_rect(tabs[i])
		for p in probes(r, 0.0, 0.5):
			await click_tab_named(screen.TABS[(i + 1) % tabs.size()])
			await click(p[0])
			var shown: bool = screen.current_tab() == screen.TABS[i]
			if shown != p[1]:
				bad_tabs.append("%s %s" % [screen.TABS[i], p[2]])
		probed += 1
	check("the %d tabs show just inside their bar and not just outside" % tabs.size(), tabs.size() == screen.TABS.size() and bad_tabs.is_empty(), "; ".join(bad_tabs))


func probe_row(key: String, r: Dictionary, drawn: Rect2) -> PackedStringArray:
	var bad := PackedStringArray()
	var kind: String = r.kind
	var margin := CHECK_MARGIN * scale().y if kind == "check" else 0.0
	var original: int = int(r.value)
	var mid: int = (int(r.min) + int(r.max)) / 2
	var list := probes(drawn, margin, 0.25 if kind == "slider" else 0.5)
	if kind == "entry":
		list.sort_custom(func(a, b): return not a[1] and b[1]) # outside first: the inside presses give the entry the keys
	for p in list:
		if shell.menu_nav_state().get("open_list", false):
			await key(KEY_ESCAPE) # an outside press that found the neighbour's list box opened it: closed before the next press
		await move(Vector2(4, 4)) # each press from away, as a separate click
		match kind:
			"slider":
				await choose(key, mid)
				await click(p[0])
				var v := int(page().rows[key].value)
				if (v != mid) != p[1]:
					bad.append("%s %s: %d (from %d; the press went to %s)" % [key, p[2], v, mid, str(shell.gadget_pick(shell.window_to_stage(p[0])))])
			"check":
				await choose(key, 0)
				await game._step(40) # a second click within the double click time is a double click (ZH: no toggle), not a test of the area
				await click(p[0])
				var v := int(page().rows[key].value)
				if (v == 1) != p[1]:
					bad.append("%s %s: %d (the press went to %s)" % [key, p[2], v, str(shell.gadget_pick(shell.window_to_stage(p[0])))])
			"choice":
				await click(p[0])
				var open: bool = shell.menu_nav_state().get("open_list", false)
				if open != p[1]:
					bad.append("%s %s: list %s" % [key, p[2], "open" if open else "closed"])
				if open:
					await key(KEY_ESCAPE)
					if shell.menu_nav_state().get("open_list", false) or not screen.is_open():
						bad.append("%s: Esc did not close its list alone" % key)

			"entry":
				await click(p[0])
				var f = shell.menu_nav_state().get("key_focus_rect")
				var focused: bool = f != null and (f as Rect2).is_equal_approx(r.rect)
				if focused != p[1]:
					bad.append("%s %s: keys %s" % [key, p[2], "taken" if focused else "not taken"])
	if kind == "slider" or kind == "check":
		await choose(key, original)
	if kind == "choice":
		bad.append_array(await probe_dropdown_items(key, r, drawn))
		await choose(key, original)
	return bad


## The drawn rows of the open drop-down list: [{ rect (window), text }] top to bottom, and the list's own window rectangle (`list`), from the gadget draw
## commands of the lone root (the open list is drawn over everything)
func open_list_rows(items: int) -> Dictionary:
	var rows := []
	var list := Rect2()
	var bars := []
	for c in shell.gadget_draw_list():
		if not c.get("lone", false):
			continue
		if c.has("root_rect"):
			list = c.root_rect
		if c.kind == "text":
			# a text command carries its origin only (the window position of its top left), not a box
			if not String(c.text).is_empty():
				rows.append({"top": (c.rect as Rect2).position.y, "text": String(c.text)})
		elif c.kind == "fill" or c.kind == "rect" or c.kind == "image":
			bars.append((c.rect as Rect2).abs())
	rows.sort_custom(func(a, b): return a.top < b.top)
	# the combo box's own selection text is drawn in the same root, above the items: with one text more than items the first is that
	if rows.size() == items + 1:
		rows.pop_front()
	return {"rows": rows, "list": list, "bars": bars}


## the exact rectangle of the highlighted (selected) row among the drawn bars: the narrowest bar that spans most of the list's width and is about one row
## tall (`pitch` +- 60 %); an empty Rect2 when none
func highlighted_row(bars: Array, list: Rect2, pitch: float) -> Rect2:
	var best := Rect2()
	for b in bars:
		var r: Rect2 = b
		if r.size.x >= list.size.x * 0.7 and r.size.y >= pitch * 0.6 and r.size.y <= pitch * 1.6 and (best.size == Vector2.ZERO or r.size.y < best.size.y):
			best = r
	return best


## Click-effect coverage of a combo box's drop-down ITEMS: each item of the open list is pressed just inside its top and bottom edges (the row boundaries are
## the midpoints between the drawn rows): the item is chosen (the value is its index); just outside the list on its left and right nothing is chosen (the list
## closes, the value stays). Every item of the row is covered by name; a list that draws a different number of items than the row has fails.
func probe_dropdown_items(key: String, r: Dictionary, drawn: Rect2) -> PackedStringArray:
	var bad := PackedStringArray()
	var n := int(r.get("items", 0))
	if n < 2:
		return bad # nothing to choose between (one entry)
	# the list shows at most 10 items (the drop-down's height, the rest scroll): the resolution's 19 are 10 drawn
	var visible := mini(n, 10)
	await move(Vector2(4, 4))
	# resolution: choosing one lays the whole page out again at another size (the window's), so its rows are not chosen here (the list is counted and
	# drawn; the display changes are the settings' own checks, opts5): the one stated exception, by name
	var chooses: bool = key != "resolution"
	if chooses:
		await choose(key, 0)
	await click(drawn.get_center())
	var shown := open_list_rows(visible)
	var rows: Array = shown.rows
	if rows.size() != visible or (shown.list as Rect2).size == Vector2.ZERO:
		bad.append("%s: the open list draws %d items in %s, expected %d of the row's %d (list open %s, top %s, texts %s)" % [key, rows.size(), str(shown.list), visible, n,
			str(shell.menu_nav_state().get("open_list", false)), top(), str(rows.map(func(r): return r.text))])
		if shell.menu_nav_state().get("open_list", false):
			await key(KEY_ESCAPE)
		return bad
	var list: Rect2 = shown.list
	var pitch: float = (rows[visible - 1].top - rows[0].top) / float(visible - 1)
	var bar := highlighted_row(shown.bars, list, pitch)
	# the list window under the pointer while it is open: the rows' hit zones start at ITS top (ZH GadgetListBox rowAtMouse: the window's y plus k rows), not at
	# the drawn row: ZH's W3DListBox draws the entries at y + 4 (drawListBoxText( ... y + 4 ...); BFME2 decomp W3DListBox.cpp the same), a donor fact kept
	var pick: Dictionary = shell.gadget_pick(shell.window_to_stage(Vector2(list.get_center().x, rows[0].top + 4.0)))
	await key(KEY_ESCAPE)
	if not chooses:
		probed += 1
		return bad
	if bar.size == Vector2.ZERO or not pick.get("found", false):
		bad.append("%s: no highlighted row among the open list's bars %s (list %s, pitch %.1f), or no list window under the pointer: %s" % [key, str(shown.bars), str(list), pitch, str(pick)])
		return bad
	var zone_top: float = window_rect(pick.rect).position.y # the first row's hit zone
	if OS.get_environment("OPTS6_INVENTORY") == "1":
		print("OPTS6 DIAG %s: the list window %s starts %.1f px above the highlighted row %s (drawn offset)" % [key, str(window_rect(pick.rect)), bar.position.y - zone_top, str(bar)])
	if OS.get_environment("OPTS6_INVENTORY") == "1" and key == "Retail.Detail":
		# diagnosis: which item a press at each pixel of the first two rows chooses (the real row boundaries)
		var scan := PackedStringArray()
		var y0: int = int(bar.position.y) - 4
		for dy in range(0, int(pitch) * 2 + 8, 2):
			await move(Vector2(4, 4))
			await choose(key, 5)
			await click(drawn.get_center())
			await click(Vector2(list.get_center().x, y0 + dy))
			scan.append("%d:%d" % [dy - 4, int(page().rows[key].value)])
			if shell.menu_nav_state().get("open_list", false):
				await key(KEY_ESCAPE)
		print("OPTS6 DIAG Retail.Detail: item chosen by a press dy px from the highlight's top (bar %s, pitch %.1f, list %s, the window under the press %s): %s" % [str(bar), pitch, str(list), str(pick), " ".join(scan)])
	for k in visible:
		var top: float = zone_top + pitch * k
		var bottom: float = top + pitch
		var centre: float = (top + bottom) * 0.5
		var x: float = list.get_center().x
		var points := [
			[Vector2(x, top + IN), true, "top inside"], [Vector2(x, bottom - IN), true, "bottom inside"],
			[Vector2(list.position.x - OUT, centre), false, "left outside"], [Vector2(list.end.x + OUT, centre), false, "right outside"]]
		for p in points:
			if not p[1] and k > 0:
				continue # the list's own left and right edges are probed once (they are not the item's)
			await move(Vector2(4, 4))
			var base := 1 if k == 0 else 0
			await choose(key, base)
			await click(drawn.get_center()) # open
			if not shell.menu_nav_state().get("open_list", false):
				bad.append("%s item %d %s: the list did not open" % [key, k, p[2]])
				continue
			await click(p[0])
			var v := int(page().rows[key].value)
			if p[1] and v != k:
				bad.append("%s item %d (%s) %s: value %d, not chosen (the press at %s, the row %.1f..%.1f)" % [key, k, rows[k].text, p[2], v, str(p[0]), top, bottom])
			if not p[1] and v != base:
				bad.append("%s item %d (%s) %s: value %d, changed from %d" % [key, k, rows[k].text, p[2], v, base])
			if shell.menu_nav_state().get("open_list", false):
				await key(KEY_ESCAPE)
	probed += 1
	return bad


## the bottom bar: RESET TO DEFAULTS clicked (Brightness 80 back to 50 inside, kept outside); CANCEL and ACCEPT pointed at (they would close the screen)
func bar_hits() -> void:
	await click_tab_named("Display")
	var buttons: Dictionary = page().get("buttons", {})
	check("the bottom bar's three buttons", buttons.size() == 3, str(buttons.keys()))
	var level: int = shell.shell_top_level()
	var paths := {"cancel": "Buttons.Cancel.bttn", "accept": "Buttons.Accept.bttn"}
	var bad := PackedStringArray()
	for which in ["cancel", "accept"]:
		var path := ""
		for b in shell.list_buttons(level):
			if String(b.path).ends_with(paths[which]):
				path = b.path
		if path.is_empty() or not buttons.has(which):
			bad.append("%s: not found" % which)
			continue
		for p in probes(window_rect(buttons[which]), 0.0, 0.5):
			await move(Vector2(4, 4))
			await move(p[0])
			var state := button_state(level, path)
			if (state == 2) != p[1]:
				bad.append("%s %s: state %d" % [which, p[2], state])
		probed += 1
	if buttons.has("reset"):
		for p in probes(window_rect(buttons.reset), 0.0, 0.5):
			await choose("Retail.Brightness", 80)
			await click(p[0])
			await game._step(6)
			var v := int(page().rows["Retail.Brightness"].value)
			if (v == 50) != p[1]:
				bad.append("reset %s: brightness %d" % [p[2], v])
		probed += 1
	check("the bottom bar at %s: CANCEL / ACCEPT light up and RESET TO DEFAULTS resets just inside, nothing just outside" % size_tag, bad.is_empty(), "; ".join(bad))
	await bar_click_effects(buttons)


## CANCEL and ACCEPT by click effect (the review: they were only hovered): a press just inside any edge of the drawn button closes the settings, one just outside
## does not; after a close the settings are opened again by the main menu's buttons (a real click on each)
func bar_click_effects(buttons: Dictionary) -> void:
	var bad := PackedStringArray()
	for which in ["cancel", "accept"]:
		if not buttons.has(which):
			bad.append("%s: no box" % which)
			continue
		for p in probes(window_rect(buttons[which]), 0.0, 0.5):
			await move(Vector2(4, 4))
			await click(p[0])
			var closed := false
			for i in (40 if p[1] else 20):
				closed = not screen.is_open()
				if closed:
					break
				await game._step(6)
			if closed != p[1]:
				bad.append("%s %s: the settings %s" % [which, p[2], "closed" if closed else "stayed"])
			if closed:
				var why: String = await reopen_options()
				if not why.is_empty():
					bad.append("%s %s: not opened again: %s" % [which, p[2], why])
					break
		probed += 1
	check("the bottom bar at %s: CANCEL and ACCEPT close the settings just inside every edge and not just outside" % size_tag, bad.is_empty(), "; ".join(bad))


## the settings opened again from the main menu (OPTIONS, then SETTINGS), without a report line
func reopen_options() -> String:
	await game._wait_main_ready()
	await game._step(60)
	var main_level: int = shell.shell_top_level()
	if main_sub_open(main_level, "Skirmish") or main_sub_open(main_level, "Replay"):
		await main_menu_closed("Skirmish") # another nav's sub-menu is still open from an earlier press: closed by the pointer leaving it
		await game._step(30)
	for name in ["OptionsNav", "Settings"]:
		if name == "OptionsNav" and main_sub_open(main_level, "Settings"):
			continue # the sub-menu is still open from the last visit: its button is there to press (OPTIONS again would close it)
		var target := {}
		for i in 30:
			for b in shell.list_buttons(main_level):
				var path := String(b.path)
				if b.hittable and b.get("seen", true) and (path.contains("." + name + ".") or path.ends_with("." + name)):
					target = b
			if not target.is_empty():
				break
			await game._step(15)
		if target.is_empty():
			var seen_btns := []
			for b in shell.list_buttons(main_level):
				if b.hittable:
					seen_btns.append(String(b.path).get_slice(".", 1))
			return "no %s button to press (top %s, hittable %s)" % [name, top(), str(seen_btns)]
		await click_stage(Vector2(target.x, target.y))
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	await game._step(30)
	return "" if screen.is_open() else "the settings did not come up (top %s)" % top()


## [point, inside?, name] for the four edges: IN pixels inside, OUT (+ `margin`) outside; the top and bottom ones at `along` of the width
func probes(r: Rect2, margin: float, along: float) -> Array:
	var cy := r.position.y + r.size.y * 0.5
	var qx := r.position.x + r.size.x * along
	return [
		[Vector2(r.position.x + IN, cy), true, "left inside"], [Vector2(r.position.x - OUT, cy), false, "left outside"],
		[Vector2(r.end.x - IN, cy), true, "right inside"], [Vector2(r.end.x + OUT, cy), false, "right outside"],
		[Vector2(qx, r.position.y + IN), true, "top inside"], [Vector2(qx, r.position.y - OUT - margin), false, "top outside"],
		[Vector2(qx, r.end.y - IN), true, "bottom inside"], [Vector2(qx, r.end.y + OUT + margin), false, "bottom outside"],
	]


## the window rectangle a row's control is drawn in: its gadget's own draw commands (not its text), found by its window's stage rectangle, and the
## page's box under it (OpenBFMEOptionsPage::decorate: the well of a combo box, a slider, an entry)
func drawn_rect(stage: Rect2) -> Rect2:
	var out := Rect2()
	var any := false
	var w := window_rect(stage)
	for c in shell.gadget_draw_list():
		if c.kind == "text" or c.kind == "clip" or c.kind == "clip_end":
			continue
		var own: bool = c.has("root_stage_rect") and (c.root_stage_rect as Rect2).is_equal_approx(stage)
		var well: bool = c.window == "OpenBFMEPage" and (c.kind == "fill" or c.kind == "rect") and (c.rect as Rect2).abs().is_equal_approx(w)
		if not own and not well:
			continue
		var r: Rect2 = (c.rect as Rect2).abs()
		out = r if not any else out.merge(r)
		any = true
	return out


# ---------------------------------------------------------------------------------------------------------------------------------
# keyboard and controller
# ---------------------------------------------------------------------------------------------------------------------------------
func keyboard_walk() -> void:
	await game._wait_main_ready()
	await game._step(30)
	await shot("opts6-main-menu-idle")
	# Main-menu arrows select authored groups and entries; Enter activates SETTINGS.

	if not await focus_and_press("OptionsNav", KEY_RIGHT):
		return
	await game._step(60)
	if not await focus_and_press("Settings", KEY_UP):
		return
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	check("keyboard: the main menu's OPTIONS > SETTINGS opened the settings", top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	if not screen.is_open():
		return
	await game._step(30)
	var nav: Dictionary = shell.menu_nav_state()
	check("keyboard: a new screen starts without a focus (stale focus not kept)", str(nav.focus).is_empty(), str(nav.focus))
	# PageDown x2: Audio
	await key(KEY_PAGEDOWN)
	await key(KEY_PAGEDOWN)
	await game._step(10)
	check("keyboard: PageDown twice shows the Audio tab", screen.current_tab() == "Audio", screen.current_tab())
	await shot("opts6-keyboard-audio-tab")
	# walk to the Music slider with Tab / Down and raise it with Right
	if await focus_target("row:Retail.MusicVolume"):
		var before := int(page().rows["Retail.MusicVolume"].value)
		await key(KEY_RIGHT)
		await key(KEY_RIGHT)
		var now := int(page().rows["Retail.MusicVolume"].value)
		var step := maxi(1, (int(page().rows["Retail.MusicVolume"].max) - int(page().rows["Retail.MusicVolume"].min)) / 20)
		check("keyboard: Right twice raises Music by two steps (%d -> %d) and the music volume follows" % [before, now], now == mini(before + 2 * step,
			int(page().rows["Retail.MusicVolume"].max)) and game._audio != null and is_equal_approx(game._audio.get_volume("music"), now / 100.0), str(now))
		nav = shell.menu_nav_state()
		check("keyboard: the focus frame is on the Music slider", nav.frame and (nav.focus_rect as Rect2).is_equal_approx(page().rows["Retail.MusicVolume"].rect), str(nav))
		await shot("opts6-keyboard-music-focus")
	# a box: Space ticks it
	if await focus_target("row:OpenBFMEMuteUnfocused"):
		var before := int(page().rows["OpenBFMEMuteUnfocused"].value)
		await key(KEY_SPACE)
		check("keyboard: Space toggles 'Mute in the background'", int(page().rows["OpenBFMEMuteUnfocused"].value) == 1 - before, "")
	# a choice: Enter opens its list, Down chooses the next entry, Esc closes the list and the page stays
	await key(KEY_PAGEUP)
	await key(KEY_PAGEUP)
	check("keyboard: PageUp twice back to Display", screen.current_tab() == "Display", screen.current_tab())
	if await focus_target("row:Retail.Brightness"):
		await key(KEY_LEFT)
		check("keyboard: Left lowers Brightness", int(page().rows["Retail.Brightness"].value) < 50, str(page().rows["Retail.Brightness"].value))
	await key(KEY_PAGEDOWN)
	if await focus_target("row:Retail.Detail"):
		var before := int(page().rows["Retail.Detail"].value)
		await key(KEY_ENTER)
		check("keyboard: Enter opens the Graphics list", await until_list(true), ", ".join(Array(shell.menu_nav_state().log).slice(-8)))
		await shot("opts6-keyboard-open-list")
		await key(KEY_ESCAPE)
		check("keyboard: Esc closes the open list and the settings stay", await until_list(false) and screen.is_open(), "")
		if not screen.is_open():
			return
		await key(KEY_ENTER)
		await until_list(true)
		await key(KEY_DOWN)
		await key(KEY_ENTER) # Down previews the choice; Enter commits it.
		var now := int(page().rows["Retail.Detail"].value)
		check("keyboard: Down then Enter in the open list chooses the next level (%d -> %d)" % [before, now], now == before + 1, ", ".join(Array(shell.menu_nav_state().log).slice(-8)))
		if shell.menu_nav_state().get("open_list", false):
			await key(KEY_ESCAPE)
	# Esc: CANCEL (the brightness, the volume back)
	await key(KEY_ESCAPE)
	await game._step(90)
	check("keyboard: Esc is CANCEL (the settings close)", not screen.is_open(), str(shell.shell_stack()))


func controller_walk() -> void:
	await game._wait_main_ready()
	await game._step(30)
	# the main menu with the D-pad and A
	if not await focus_and_press("OptionsNav", -JOY_BUTTON_DPAD_RIGHT):
		return
	await game._step(60)
	if not await focus_and_press("Settings", -JOY_BUTTON_DPAD_UP):
		return
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	check("controller: OPTIONS > SETTINGS opened with the D-pad and A", top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	if not screen.is_open():
		return
	await joy_button(JOY_BUTTON_RIGHT_SHOULDER)
	await joy_button(JOY_BUTTON_RIGHT_SHOULDER)
	check("controller: RB twice shows the Audio tab", screen.current_tab() == "Audio", screen.current_tab())
	if await focus_target("row:Retail.SoundFxVolume", true):
		var before := int(page().rows["Retail.SoundFxVolume"].value)
		await joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1.0)
		await joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0)
		var after_trigger := int(page().rows["Retail.SoundFxVolume"].value)
		await joy_axis(JOY_AXIS_RIGHT_X, -1.0)
		await joy_axis(JOY_AXIS_RIGHT_X, 0.0)
		var after_stick := int(page().rows["Retail.SoundFxVolume"].value)
		check("controller: the right trigger raises Sound FX (%d -> %d), the right stick lowers it (%d)" % [before, after_trigger, after_stick],
			after_trigger > before and after_stick == before or (before == int(page().rows["Retail.SoundFxVolume"].max) and after_stick < before), "")
		await shot("opts6-controller-sfx-focus")
	await joy_button(JOY_BUTTON_LEFT_SHOULDER)
	check("controller: LB goes back a tab", screen.current_tab() == "Graphics", screen.current_tab())
	await joy_button(JOY_BUTTON_B)
	await game._step(90)
	check("controller: B is CANCEL (the settings close)", not screen.is_open(), str(shell.shell_stack()))
	var log: PackedStringArray = game._menu_pad.take_log()
	check("controller: the pad's events went through as keys (%d)" % log.size(), log.size() >= 10, ", ".join(log)) # tolerance: presses, releases, repeats


# ---------------------------------------------------------------------------------------------------------------------------------
# the other screens (the review: skirmish, campaign, online and LAN were unverified)
# ---------------------------------------------------------------------------------------------------------------------------------
## Walks one screen with the keyboard: its navigation targets exist; Tab reaches every one of them exactly once per round (the focus ids seen == the targets)
## and Shift+Tab walks them back; every button the pointer can press on the screen's level is a target (nothing is mouse-only); Enter on the focused target
## presses it (a button's effect, read by `after_press`); the screen's Back (Esc) leaves it for `back_to`. `inventory` names, by the last element of the
## path, the targets the screen must offer (fail closed: a missing one, or an empty target list, fails). Returns the target ids.
func walk_screen(label: String, screen_file: String, inventory: Array, back_to: String) -> Array:
	await game._step(30)
	var level: int = shell.shell_top_level()
	var nav: Dictionary = shell.menu_nav_state()
	var ids := []
	for t in nav.get("targets", []):
		ids.append(String(t.id))
	check("%s: the screen is up with navigation targets (%d)" % [label, ids.size()], top() == screen_file and level >= 0 and not ids.is_empty() and int(nav.level) == level,
		"top %s, level %d, nav level %s, %d targets" % [top(), level, str(nav.get("level")), ids.size()])
	if ids.is_empty():
		return ids
	# the controls the keys reach are exactly the screen's required inventory (ids without the `_levelN.` of the slot the movie loaded in)
	var plain := []
	for id in ids:
		plain.append(String(id).substr(String(id).find(".") + 1))
	check_inventory(label, required(inventory), shown(plain))
	# every button the pointer can press here is a target
	var mouse_only := PackedStringArray()
	for b in shell.list_buttons(level):
		# what a player can see: a hidden or faded button, or one laid out outside the 1024 x 768 stage, that the hit list still has is not a control
		if b.hittable and b.get("seen", true) and b.x >= 0 and b.x <= 1024 and b.y >= 0 and b.y <= 768:
			var inside := false
			for t in nav.targets:
				var r: Rect2 = t.rect
				inside = inside or r.grow(6).has_point(Vector2(b.x, b.y))
			if not inside:
				mouse_only.append("%s at (%d, %d)" % [b.path, b.x, b.y])
	check("%s: nothing the pointer can press is missing from the targets" % label, mouse_only.is_empty(), ", ".join(mouse_only))
	# Tab: the keys reach every target (the first key shows the focus; Tab past the last may first close a sub-menu's click-away area: a press that does not
	# move the focus is repeated up to three times)
	await key(KEY_TAB)
	var seen := []
	var stuck := 0
	for i in ids.size() * 2 + 8:
		var f := String(shell.menu_nav_state().focus)
		if not f.is_empty() and not seen.has(f):
			seen.append(f)
		if seen.size() == ids.size():
			break
		await key(KEY_TAB)
		if String(shell.menu_nav_state().focus) == f:
			stuck += 1
			await game._step(90) # a sub-menu's closing animation (the pointer left its click-away area) plays out before the next press
			if stuck > 3:
				var st: Dictionary = shell.menu_nav_state()
				print("OPTS6 DIAG %s: Tab does not move the focus from %s; catchers %s; the navigation's last steps: %s" % [label, f, str(st.get("catchers")), str(Array(st.get("log", [])).slice(-8))])
				break
		else:
			stuck = 0
	var a := ids.duplicate()
	a.sort()
	var b2 := seen.duplicate()
	b2.sort()
	check("%s: Tab reaches each of the %d targets" % [label, ids.size()], a == b2, "reached %d: %s; targets %s" % [seen.size(), str(seen), str(ids)])
	# Shift+Tab goes back: from the first target Tab then Shift+Tab is the same target again
	var first := String(ids[0])
	for i in ids.size() + 2:
		if String(shell.menu_nav_state().focus) == first:
			break
		await key(KEY_TAB)
	await key(KEY_TAB)
	var next := String(shell.menu_nav_state().focus)
	await key(KEY_TAB, true)
	check("%s: Shift+Tab returns to the target Tab left" % label, String(shell.menu_nav_state().focus) == first and next != first or ids.size() == 1, "%s -> %s -> %s" % [first, next, shell.menu_nav_state().focus])
	# Back leaves (a screen with a sub-menu open closes it first: up to three presses)
	var presses := 0
	for i in 3:
		await key(KEY_ESCAPE)
		presses += 1
		await game._step(60)
		if OS.get_environment("OPTS6_INVENTORY") == "1" and top() != back_to:
			var st2: Dictionary = shell.menu_nav_state()
			print("OPTS6 DIAG %s: after Esc press %d the screen is still up; focus %s, catchers %s, the navigation's last steps: %s" % [label, presses, st2.get("focus"), str(st2.get("catchers")),
				str(Array(st2.get("log", [])).slice(-6))])
		if top() == back_to:
			break
	check("%s: Esc leaves it (back to %s, %d press(es))" % [label, back_to, presses], top() == back_to, "top %s, stack %s" % [top(), str(shell.shell_stack())])
	return ids


## print what the keyboard can reach now (OPTS6_INVENTORY=1: the data the screens' required inventories are written from)
func print_targets(label: String) -> void:
	if OS.get_environment("OPTS6_INVENTORY") != "1":
		return
	var ids := []
	for t in shell.menu_nav_state().get("targets", []):
		ids.append("%s %s" % [t.id, t.rect])
	print("OPTS6 INVENTORY targets %s (top %s): %s" % [label, top(), " | ".join(ids)])


## the screens reached from the main menu with the keyboard only: each row is a main menu nav button, the sub-menu button under it, the screen it opens, the
## screen Esc leaves it for and the controls it must offer
const SCREENS := [
	{"nav": "SoloPlayNav", "sub": "Skirmish", "walk": KEY_UP, "file": "Skirmish.apt", "back": "MainMenu.apt", "label": "skirmish setup", "need": "skirmish", "prep": "profile"},
	{"nav": "SoloPlayNav", "sub": "LoadGame", "walk": KEY_UP, "file": "SaveLoad.apt", "back": "MainMenu.apt", "label": "campaign / skirmish load screen", "need": "load"},
	{"nav": "MultiPlayNav", "sub": "LocalNetwork", "walk": KEY_UP, "file": "LanLobby.apt", "back": "MainMenu.apt", "label": "LAN lobby", "need": "lan"},
	{"nav": "MultiPlayNav", "sub": "Replay", "walk": KEY_UP, "file": "SaveLoad.apt", "back": "MainMenu.apt", "label": "replay list", "need": "replay"},
	{"nav": "MultiPlayNav", "sub": "Online", "walk": KEY_UP, "file": "OnlineShell.apt", "back": "MainMenu.apt", "label": "online login", "need": "online", "prep": "nickname"},
]


## The controls each screen must offer to the keys, by id without the `_levelN.` prefix: written from the screens' movies (their clip and instance names), not
## read from the run. The skirmish setup is the one of the default map of this install (8 slots: six gadgets each)
func screen_inventory(name: String) -> Array:
	match name:
		"skirmish":
			var out := ["OpenPlay.MpMapAndSettings.TabMap.instance7", "OpenPlay.MpMapAndSettings.TabButtons.RulesTab.bttn",
				"OpenPlay.MpMapAndSettings.MapsTabSelected.MapsTab_Assorted.ArrowTab.OnSortIcon", "OpenPlay.MpMapAndSettings.MapsTabSelected.MapsTab_Assorted.NameTab.OnSortName",
				"OpenPlay.MpMapAndSettings.MapsTabSelected.MapsTab_Assorted.PlayersTab.OnSortPlayers", "OpenPlay.MpMapAndSettings.MapsTabSelected.MapsTab_Assorted.instance22.MapList",
				"OpenPlay.MpMapAndSettings.MapsTabSelected.instance1.MapInfo.MapInfo", "lobby.MainMenu.bttn", "ProfileNav.bttn", "lobby.StartGame.bttn"]
			for slot in 8:
				for g in ["Player", "PlayerTemplate", "Hero", "Team", "Handicap", "Color"]:
					out.append("OpenPlay.instance1.PlayerList.~%d.%s" % [slot, g])
			return out
		"load":
			return ["UI.$Campaign.instance10", "UI.TabButtons.Skirmish.Button", "UI.TabButtons.WOTR.Button", "UI.3~AutoSaveList", "UI.3~GameList", "MainButtons.Cancel.bttn"]
		"replay":
			return ["UI.2~AutoSaveList", "UI.2~GameList", "MainButtons.Cancel.bttn"]
		"online":
			# the login page once its intro has played in: the nickname entry, EXIT and LOG IN (the page's other clips are hidden or faded then),
			# and (DEVPORT-6, merged with MACPLAY-1) the seal's button, which opens the OpenBFME website (AptOnlineShell.cpp
			# "AptOnline::Login::GameSpy" -> ShellAction::OpenProjectWebsite; godot/tests/online_branding_test.gd clicks it)
			return ["OnlineLogin.Login.componentGroup.OnlineLogin::Nickname", "OnlineLogin.Exit.bttn", "OnlineLogin.Login.LogIn.bttn",
				"OnlineLogin.GameSpy.bttn"]
		"lan":
			return ["LanOpenPlay.LobbyMain.Refresh.buttonRefresh", "LanOpenPlay.LobbyMain.instance224.OnSortStatus.OnSortHeroType",
				"LanOpenPlay.LobbyMain.instance224.OnSortName.OnSortName", "LanOpenPlay.LobbyMain.instance224.OnSortMap.OnSortHeroMap",
				"LanOpenPlay.LobbyMain.instance224.OnSortPlayers.OnSortPlayers", "LanOpenPlay.LobbyMain.TopRightFrame.LanLobby::NameEntry",
				"LanOpenPlay.LobbyMain.instance224.LanLobby::CustomGamesList", "LanOpenPlay.LobbyMain.TopRightFrame.CreateGame.instance11",
				"LanOpenPlay.LobbyMain.instance1.TabMap.instance7", "LanOpenPlay.LobbyMain.instance1.TabButtons.RulesTab.bttn",
				"LanOpenPlay.LobbyMain.instance1.MapsTabSelected.MapsTab_Assorted.ArrowTab.OnSortIcon", "LanOpenPlay.LobbyMain.instance1.MapsTabSelected.MapsTab_Assorted.NameTab.OnSortName",
				"LanOpenPlay.LobbyMain.instance1.MapsTabSelected.MapsTab_Assorted.PlayersTab.OnSortPlayers", "LanOpenPlay.LobbyMain.instance1.MapsTabSelected.MapsTab_Assorted.instance22.MapList",
				"LanOpenPlay.LobbyMain.instance1.MapsTabSelected.instance1.MapInfo.MapInfo", "MainButtons.MainMenu.bttn", "MainButtons.Options.bttn", "instance82.MessengerButton.bttn"]
	return []


## the buttons a main menu nav sub-menu offers now (the targets under its clip) against REQUIRED_SUB
func sub_menu_inventory(nav: String) -> void:
	var entries: PackedStringArray = shell.menu_nav_state().get("main_entries", PackedStringArray())
	check_inventory("the %s sub-menu" % nav, required(REQUIRED_SUB[nav]), shown(Array(entries)))


## Skirmish's first visit asks for a profile name in a modal box (ProfilePopup): by keys only: Tab to the name entry, the name typed, Tab to SELECT, Enter.
## The box's controls must be reachable (they are the only targets while it is up); the lobby behind it takes no key
func skirmish_profile_box() -> void:
	print_targets("the skirmish profile box")
	var entry := ""
	var cancel := ""
	for t in shell.menu_nav_state().get("targets", []):
		var id := String(t.id)
		if id.ends_with("Skirmish::CreatePersonaEntry"):
			entry = id
		if id.ends_with("ProfilePopup.Main.Cancel.bttn"):
			cancel = id
	check("the skirmish profile box offers its name entry and CANCEL to the keys (it is modal: the lobby behind it has no target)", not entry.is_empty() and not cancel.is_empty(),
		"entry '%s', cancel '%s'" % [entry, cancel])
	if entry.is_empty():
		return
	# the entry is focused by the navigation (its keys go to the entry: an Enter on it with no name accepts the box, so none is pressed): the name is typed
	if not await focus_target(entry):
		return
	for ch in "navtester":
		await type_char(ch)
	print_targets("the skirmish profile box after the name was typed")
	# SELECT is enabled by a name: now it is a target, and Enter on it closes the box into the lobby
	var select := ""
	for t in shell.menu_nav_state().get("targets", []):
		if String(t.id).ends_with("ProfilePopup.Main.Select.bttn"):
			select = String(t.id)
	check("the typed name enables SELECT for the keys", not select.is_empty(), str(shell.menu_nav_state().get("targets", []).map(func(t): return t.id)))
	if select.is_empty() or not await focus_target(select):
		return
	await key(KEY_ENTER)
	await game._step(150)
	check("SELECT by the keyboard closes the box into the lobby", not hittable_all(-1).any(func(b): return String(b.path).contains("ProfilePopup.Main.Select")), "")


## lane ONLINEUX-1: the online login's LOG IN follows the nickname (RW 0x97A668 / BFME2 0x5706D4: disabled while the name is empty), so a first visit
## (no remembered name) offers it to no key until a name is typed: by keys only, the name entry is focused and a name typed; then LOG IN is a target
## (and the screen's inventory below has it)
func online_login_nickname() -> void:
	var entry := ""
	var login := false
	# the name entry is a target once the login page has played in: a cold first run (the first size on a loaded host) needs more than the
	# 120 steps the caller gave every screen's intro
	for attempt in 40:
		for t in shell.menu_nav_state().get("targets", []):
			var id := String(t.id)
			if id.ends_with("OnlineLogin.Login.componentGroup.OnlineLogin::Nickname"):
				entry = id
			login = login or id.ends_with("OnlineLogin.Login.LogIn.bttn")
		if not entry.is_empty():
			break
		await game._step(15)
	check("the online login offers its name entry to the keys, and no LOG IN while the name is empty (RW 0x97A668)", not entry.is_empty() and not login,
		"entry '%s', LOG IN a target: %s" % [entry, str(login)])
	if entry.is_empty() or not await focus_target(entry):
		return
	for ch in "navtester":
		await type_char(ch)
	await game._step(30)


func type_char(ch: String) -> void:
	for pressed in [true, false]:
		var e := InputEventKey.new()
		e.keycode = OS.find_keycode_from_string(ch.to_upper()) as Key
		e.physical_keycode = e.keycode
		e.unicode = ch.unicode_at(0)
		e.pressed = pressed
		Input.parse_input_event(e)
		await game._step(2)


func other_screens() -> void:
	await game._wait_main_ready()
	await game._step(30)
	for row in SCREENS:
		if not await focus_and_press(row.nav, KEY_RIGHT):
			continue
		await game._step(60)
		print_targets("the %s sub-menu" % row.nav)
		sub_menu_inventory(row.nav)
		if not await focus_and_press(row.sub, row.walk):
			continue
		for i in 40:
			if top() == row.file:
				break
			await game._step(15)
		await game._step(120) # a screen's own intro (its bottom bar, its first-visit box) plays in
		if row.get("prep", "") == "profile":
			await skirmish_profile_box()
		elif row.get("prep", "") == "nickname":
			await online_login_nickname()
		print_targets(row.label)
		await walk_screen(row.label, row.file, screen_inventory(row.need), row.back)
		await game._step(60)
	# the other sub-menus' contents (data for the inventories: the campaign, multiplayer and heroes entries)
	for nav in ["OptionsNav"]: # the Multiplayer sub-menu is opened by its screens' rows above
		if await focus_and_press(nav, KEY_RIGHT):
			await game._step(60)
			print_targets("the %s sub-menu" % nav)
			sub_menu_inventory(nav)
			await key(KEY_ESCAPE)
			await game._step(60)


## Select an authored main-menu group, or activate one of its entries.
func focus_and_press(name: String, walk: int) -> bool:
	for i in 40: # tolerance: bounded traversal exceeds every retail group's entry count.
		var nav: Dictionary = shell.menu_nav_state()
		if String(nav.get("main_focus", "")) == name:
			if name in nav.get("main_groups", []):
				# Populate main_entries after the authored opening animation, without activating an entry.
				await game._step(60)
				if walk < 0:
					await joy_button(JOY_BUTTON_DPAD_UP)
				else:
					await key(KEY_UP)
			elif walk < 0:
				await joy_button(JOY_BUTTON_A)
			else:
				await key(KEY_ENTER)
			return true
		if walk < 0:
			await joy_button(-walk)
		else:
			await key(walk)
	var st: Dictionary = shell.menu_nav_state()
	check("reach %s without the mouse" % name, false, "focus %s; groups %s; entries %s" % [st.get("main_focus"), st.get("main_groups"), st.get("main_entries")])
	return false

# lane MENUFIX-3: the list opens / closes on the shell's next ticks; on a loaded host (the UI-2 gate at 2560x1440) a fixed few frames were not enough,
# so wait for the state (at most ~6 s) instead of judging after key()'s frames
func until_list(open: bool) -> bool:
	for i in 75:
		if bool(shell.menu_nav_state().get("open_list", not open)) == open:
			return true
		await game._step(5)
	return false


## Tab, or D-pad right into the rows then vertically toward the requested row.
func focus_target(id: String, pad := false) -> bool:
	if pad:
		await joy_button(JOY_BUTTON_DPAD_RIGHT) # the first press shows the focus (on the tab)
		if str(shell.menu_nav_state().focus).begins_with("tab:"):
			await joy_button(JOY_BUTTON_DPAD_RIGHT)
	for i in 60:
		if str(shell.menu_nav_state().focus) == id and shell.menu_nav_state().frame:
			return true
		if pad:
			var state: Dictionary = shell.menu_nav_state()
			var current_y := 0.0
			var target_y := 0.0
			for target in state.get("targets", []):
				if String(target.id) == String(state.focus): current_y = (target.rect as Rect2).get_center().y
				if String(target.id) == id: target_y = (target.rect as Rect2).get_center().y
			await joy_button(JOY_BUTTON_DPAD_UP if target_y < current_y else JOY_BUTTON_DPAD_DOWN)
		else:
			await key(KEY_TAB)
	check("reach %s without the mouse" % id, false, str(shell.menu_nav_state().focus))
	return false


# ---------------------------------------------------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------------------------------------------------
func _size() -> void:
	var root: Window = game.get_tree().root
	if game._opts6_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts6_size
	elif DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
	await game._step(20)
	size_tag = "%dx%d" % [root.size.x, root.size.y]


func open_options() -> bool:
	await game._wait_main_ready()
	var main_level: int = shell.shell_top_level()
	for name in ["OptionsNav", "Settings"]:
		var target := {}
		for i in 20:
			for b in shell.list_buttons(main_level):
				var path := String(b.path)
				if b.hittable and (path.contains("." + name + ".") or path.ends_with("." + name)):
					target = b
			if not target.is_empty():
				break
			await game._step(15)
		if target.is_empty():
			check("open Options", false, "cannot press " + name)
			return false
		await click_stage(Vector2(target.x, target.y))
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	await game._step(30)
	check("Options opens on the settings", top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	return screen.is_open()


func hittable_all(skip: int) -> Array:
	var out := []
	for level in 12:
		if level == skip:
			continue
		for b in shell.list_buttons(level):
			if b.hittable:
				out.append(b)
	return out


## the state of the button at `path`, or of the one its clip holds now (a nav clip's animation makes a new unnamed instance: "<clip>.instanceN")
func button_state(level: int, path: String) -> int:
	var clip := path.get_slice(".instance", 0) if path.contains(".instance") else ""
	var best := -1
	for b in shell.list_buttons(level):
		if b.path == path:
			return int(b.state)
		if not clip.is_empty() and String(b.path).begins_with(clip + ".instance"):
			best = maxi(best, int(b.state))
	return best


func scale() -> Vector2:
	var a: Vector2 = shell.stage_to_window(Vector2(0, 0))
	var b: Vector2 = shell.stage_to_window(Vector2(100, 100))
	return (b - a) / 100.0


func window_rect(stage: Rect2) -> Rect2:
	var a: Vector2 = shell.stage_to_window(stage.position)
	var b: Vector2 = shell.stage_to_window(stage.end)
	return Rect2(a, b - a)


func top() -> String:
	var stack: PackedStringArray = shell.shell_stack()
	return "" if stack.is_empty() else stack[-1]


func page() -> Dictionary:
	return shell.openbfme_page_state()


func choose(key: String, value: int) -> void:
	var r: Dictionary = shell.openbfme_page_choose(key, value)
	if not r.get("ok", false):
		check("choose %s = %d" % [key, value], false, str(r.get("error", "?")))
	await game._step(3)


func move(p: Vector2) -> void:
	var m := InputEventMouseMotion.new()
	m.position = p
	m.global_position = p
	game.get_viewport().push_input(m)
	await game._step(3)


func click(p: Vector2) -> void:
	await move(p)
	for pressed in [true, false]:
		var b := InputEventMouseButton.new()
		b.button_index = MOUSE_BUTTON_LEFT
		b.pressed = pressed
		b.position = p
		b.global_position = p
		game.get_viewport().push_input(b)
		await game._step(2)
	await game._step(4)


func click_stage(p: Vector2) -> void:
	await click(shell.stage_to_window(p))


func click_tab_named(tab: String) -> void:
	if screen.current_tab() == tab:
		return
	var tabs: Array = page().get("tabs", [])
	var i: int = screen.TABS.find(tab)
	if i < 0 or i >= tabs.size():
		check("the tab %s is on the page" % tab, false, str(tabs))
		return
	await click_stage((tabs[i] as Rect2).get_center())
	await game._step(6)


func click_bar(which: String) -> void:
	var buttons: Dictionary = page().get("buttons", {})
	if buttons.has(which):
		await click_stage((buttons[which] as Rect2).get_center())
		await game._step(10)


func key(code: int, shift := false) -> void:
	for pressed in [true, false]:
		var e := InputEventKey.new()
		e.keycode = code as Key
		e.physical_keycode = code as Key
		e.shift_pressed = shift
		e.pressed = pressed
		Input.parse_input_event(e)
		await game._step(3)
	await game._step(3)


func joy_button(button: int) -> void:
	for pressed in [true, false]:
		var e := InputEventJoypadButton.new()
		e.button_index = button as JoyButton
		e.pressed = pressed
		Input.parse_input_event(e)
		await game._step(3)
	await game._step(3)


func joy_axis(axis: int, value: float) -> void:
	var e := InputEventJoypadMotion.new()
	e.axis = axis as JoyAxis
	e.axis_value = value
	Input.parse_input_event(e)
	await game._step(4)


func shot(name: String) -> void:
	if shots.is_empty() or DisplayServer.get_name() == "headless":
		return
	await game._step(8)
	var image := game.get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(shots)
	var path := shots.path_join("%s-%s.png" % [name, size_tag])
	print("OPTS6 shot %s: %s" % [path.get_file(), error_string(image.save_png(path))])


func check(name: String, ok: bool, why: String) -> void:
	if ok:
		ok_count += 1
		print("OPTS6 %s: ok" % name)
	else:
		fail_count += 1
		print("OPTS6 %s: FAIL %s" % [name, why])


func finish() -> int:
	print("OPTS6 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count
