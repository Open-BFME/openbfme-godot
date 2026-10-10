## Lane INPUT-1: the keyboard as a player uses it, replayed by input events. Every key is an InputEventKey given to Input.parse_input_event (the path the
## OS keyboard takes: the viewport, InGameHudNode::_input, the HUD's message stream), every click an InputEventMouseButton (play1_player.gd's helpers).
## game.gd runs it after the start with --input1=<scenario>[,<scenario>...] (with --auto); each scenario prints "INPUT1 <scenario>: ok" or
## "INPUT1 <scenario>: FAIL <why>", and "INPUT1 RESULT: <n> ok, <m> failed" ends the run.
##   groups    the control groups: a click selects a unit, Ctrl+1 makes group 1, a click selects another unit, 1 selects group 1 again (the HUD and the
##             logic), Shift+1 adds group 1 to the selection, Ctrl+2 / 2 a second group, 1 pressed twice within RotWK's 5 logic frames looks at the group
##   keys      every GAME record of the install's CommandMap is pressed as a keyboard sends it (its modifiers, its key, the transition it waits for):
##             the meta message it must make is checked (the HUD's key diagnosis); prints one "INPUT1 key <NAME> <keys>: fired / NOT FIRED" line each
##   camera    Ctrl+F1 stores the view, the camera moves, F1 brings it back; numpad 5 resets the camera; Ctrl+Z toggles the free camera (not retail)
##   hotkeys   the fortress is selected and the letter its first enabled build button's label marks with '&' is pressed: the button's command runs
##   modifiers left Ctrl and right Ctrl down, left Ctrl up, the pointer moves (its event says Ctrl): 1 still makes group 1 (MSG_CREATE_TEAM1); right Ctrl's
##             release lost, the pointer's next event (no Ctrl) releases it: 1 selects the group
##   misc      F12 saves a screenshot, ` opens and closes the spell store, S stops the selection (MSG_DO_STOP)
extends "res://scripts/play1_player.gd"


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	if DisplayServer.get_name() == "headless" and window_size().x < 640:
		# a headless run's window is 64 x 64: the clicks need a screen the units fit on (the pixels are the camera's projection; nothing is drawn)
		game.get_tree().root.size = Vector2i(1280, 720)
		await game._step(10)
		print("INPUT1 headless: the window is %s" % window_size())
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("INPUT1 %s: ok" % s)
		else:
			fail_count += 1
			print("INPUT1 %s: FAIL %s" % [s, r])
	print("INPUT1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


# ---- keys ------------------------------------------------------------------------------------------------------------------------------------------------

var _mods := {"ctrl": false, "shift": false, "alt": false}


func _key_event(code: Key, down: bool) -> InputEventKey:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = down
	e.ctrl_pressed = _mods.ctrl
	e.shift_pressed = _mods.shift
	e.alt_pressed = _mods.alt
	return e


## a key with modifiers held, as a keyboard sends it: the modifiers go down first, the key down and up, the modifiers up
func key(code: Key, mods: Array = []) -> void:
	for m in mods:
		_mods[m] = true
		var mk: Key = KEY_CTRL if m == "ctrl" else (KEY_SHIFT if m == "shift" else KEY_ALT)
		Input.parse_input_event(_key_event(mk, true))
		await game._step(2)
	Input.parse_input_event(_key_event(code, true))
	await game._step(2)
	Input.parse_input_event(_key_event(code, false))
	await game._step(2)
	for m in mods:
		_mods[m] = false
		var mk: Key = KEY_CTRL if m == "ctrl" else (KEY_SHIFT if m == "shift" else KEY_ALT)
		Input.parse_input_event(_key_event(mk, false))
		await game._step(2)


func ids(sel: Array) -> Array:
	var out: Array = []
	for s in sel:
		out.append(int(s.get("id", -1)) if s is Dictionary else int(s))
	out.sort()
	return out


## two of the local player's objects that show on one screen, units first (a structure is selectable and groupable too)
func two_units() -> Array:
	var all: Array = world.get_player_objects(game._local_name)
	var units: Array = local_units()
	for o in all:
		if o.structure:
			units.append(o)
	print("INPUT1 local objects: ", units.map(func(o): return "%s(%d)%s" % [o.template, o.id, " S" if o.structure else ""]))
	for a in units:
		hud.camera_look_at(Vector2(a.x, a.y))
		await game._step(10)
		var pa := to_pixel(a.x, a.y)
		if not on_screen(pa):
			print("INPUT1 %s(%d) at (%.0f, %.0f) is at pixel %s, window %s: not on the screen" % [a.template, a.id, a.x, a.y, pa, window_size()])
			continue
		for b in units:
			if b.id == a.id:
				continue
			var pb := to_pixel(b.x, b.y)
			if on_screen(pb) and pa.distance_to(pb) > 40.0:
				return [a, b]
	return []


func select_by_click(u: Dictionary) -> Array:
	await click(to_pixel(u.x, u.y))
	await game._step(10)
	return ids(hud.get_selection())


# ---- scenarios -------------------------------------------------------------------------------------------------------------------------------------------

func _scenario_groups() -> String:
	var pair: Array = await two_units()
	if pair.is_empty():
		return "no two local units on one screen (%d units)" % local_units().size()
	var a: Dictionary = pair[0]
	var b: Dictionary = pair[1]
	print("INPUT1 groups: unit A %s (%d), unit B %s (%d)" % [a.template, a.id, b.template, b.id])
	var sel_a := await select_by_click(a)
	print("INPUT1 groups: a click on A selects ", sel_a)
	if sel_a.is_empty():
		return "the click on unit A selected nothing"
	var log0 := log_count("MSG_CREATE_TEAM1")
	await key(KEY_1, ["ctrl"])
	await game._step(10)
	print("INPUT1 groups: Ctrl+1 -> MSG_CREATE_TEAM1 x%d; the last messages %s" % [log_count("MSG_CREATE_TEAM1") - log0, hud.get_message_log().slice(-4)])
	if log_count("MSG_CREATE_TEAM1") - log0 != 1:
		var stops := log_count("MSG_DO_STOP")
		await key(KEY_S)
		await game._step(10)
		print("INPUT1 groups: diagnosis: S alone -> MSG_DO_STOP x%d" % (log_count("MSG_DO_STOP") - stops))
		await key(KEY_1)
		await game._step(10)
		print("INPUT1 groups: diagnosis: 1 alone -> the last messages %s" % [hud.get_message_log().slice(-3)])
		return "Ctrl+1 did not send MSG_CREATE_TEAM1 (hud state %s)" % str(hud.get_state().get("keys", {}))
	var sel_b := await select_by_click(b)
	print("INPUT1 groups: a click on B selects ", sel_b)
	if sel_b.is_empty() or sel_b == sel_a:
		return "the click on unit B did not change the selection (%s)" % str(sel_b)
	await game._wait_seconds(1.5) # past RotWK's double-press window (5 logic frames)
	await key(KEY_1)
	await game._step(15)
	var sel_1 := ids(hud.get_selection())
	var logic_1 := ids(hud.get_logic_selection())
	print("INPUT1 groups: 1 selects %s (logic %s)" % [sel_1, logic_1])
	if sel_1 != sel_a:
		return "1 selected %s, group 1 is %s" % [str(sel_1), str(sel_a)]
	if logic_1 != sel_a:
		return "the logic's selection after 1 is %s, group 1 is %s" % [str(logic_1), str(sel_a)]
	# Ctrl+2 on B, then Shift+1 adds group 1
	await select_by_click(b)
	await key(KEY_2, ["ctrl"])
	await game._wait_seconds(1.5)
	await key(KEY_1, ["shift"])
	await game._step(15)
	var both := sel_a + sel_b
	both.sort()
	var sel_add := ids(hud.get_selection())
	print("INPUT1 groups: B selected, Shift+1 gives ", sel_add)
	if sel_add != both:
		return "Shift+1 gave %s, expected %s" % [str(sel_add), str(both)]
	await game._wait_seconds(1.5)
	await key(KEY_2)
	await game._step(15)
	var sel_2 := ids(hud.get_selection())
	print("INPUT1 groups: 2 selects ", sel_2)
	if sel_2 != sel_b:
		return "2 selected %s, group 2 is %s" % [str(sel_2), str(sel_b)]
	# a double press of 1 looks at group 1: move the camera away first
	hud.camera_look_at(Vector2(b.x + 600.0, b.y + 600.0))
	await game._step(10)
	await game._wait_seconds(1.5)
	await key(KEY_1)
	await key(KEY_1)
	await game._step(20)
	var cam: Dictionary = hud.get_camera()
	var d: float = cam.position.distance_to(Vector2(a.x, a.y))
	print("INPUT1 groups: after 1, 1 the camera looks at %s, %.0f from A" % [cam.position, d])
	if d > 150.0:
		return "the double press of 1 did not look at group 1 (%.0f away)" % d
	shot("groups")
	return ""


# ---- the CommandMap walk ---------------------------------------------------------------------------------------------------------------------------------

## the CommandMap key names (RW 0xBF0B50) as Godot keys
const KEY_OF := {"KEY_ESC": KEY_ESCAPE, "KEY_BACKSPACE": KEY_BACKSPACE, "KEY_ENTER": KEY_ENTER, "KEY_SPACE": KEY_SPACE, "KEY_TAB": KEY_TAB,
	"KEY_MINUS": KEY_MINUS, "KEY_EQUAL": KEY_EQUAL, "KEY_LBRACKET": KEY_BRACKETLEFT, "KEY_RBRACKET": KEY_BRACKETRIGHT, "KEY_SEMICOLON": KEY_SEMICOLON,
	"KEY_APOSTROPHE": KEY_APOSTROPHE, "KEY_TICK": KEY_QUOTELEFT, "KEY_BACKSLASH": KEY_BACKSLASH, "KEY_COMMA": KEY_COMMA, "KEY_PERIOD": KEY_PERIOD,
	"KEY_SLASH": KEY_SLASH, "KEY_UP": KEY_UP, "KEY_DOWN": KEY_DOWN, "KEY_LEFT": KEY_LEFT, "KEY_RIGHT": KEY_RIGHT, "KEY_HOME": KEY_HOME, "KEY_END": KEY_END,
	"KEY_PGUP": KEY_PAGEUP, "KEY_PGDN": KEY_PAGEDOWN, "KEY_INS": KEY_INSERT, "KEY_DEL": KEY_DELETE, "KEY_KPSLASH": KEY_KP_DIVIDE}


func godot_key(name: String) -> Key:
	if KEY_OF.has(name):
		return KEY_OF[name]
	var rest := name.trim_prefix("KEY_")
	if rest.length() == 1:
		return OS.find_keycode_from_string(rest)
	if rest.begins_with("KP") and rest.length() == 3:
		return KEY_KP_0 + int(rest.substr(2)) as Key
	if rest.begins_with("F"):
		return OS.find_keycode_from_string(rest)
	return KEY_NONE


func metas() -> Dictionary:
	return hud.get_state().get("keys", {})


func _scenario_keys() -> String:
	var map: Array = hud.get_command_map()
	if map.size() == 0:
		return "the HUD has no CommandMap"
	# these change what the next keys meet (the quit menu pauses, the players screen takes the input): checked last, or elsewhere
	var last := ["DIPLOMACY"]
	var skip := ["OPTIONS"] # Esc: the quit menu (game.gd, END-2; end_test.gd covers it)
	var ordered: Array = []
	for rec in map:
		if rec.game and not rec.name in last and not rec.name in skip:
			ordered.append(rec)
	for rec in map:
		if rec.name in last:
			ordered.append(rec)
	var failed: PackedStringArray = []
	for rec in ordered:
		var mods: Array = []
		if rec.ctrl:
			mods.append("ctrl")
		if rec.shift:
			mods.append("shift")
		if rec.alt:
			mods.append("alt")
		var before: Dictionary = metas()
		var label := "+".join(mods + [rec.key]) + (" (up)" if rec.transition == 1 else "")
		if rec.key == "KEY_NONE":
			# a modifier record: the modifier alone (BEGIN on the press, END on the release)
			var mk: Key = KEY_CTRL if rec.ctrl else (KEY_SHIFT if rec.shift else KEY_ALT)
			_mods[mods[0]] = true
			Input.parse_input_event(_key_event(mk, true))
			await game._step(2)
			var after_down: Dictionary = metas()
			_mods[mods[0]] = false
			Input.parse_input_event(_key_event(mk, false))
			await game._step(2)
			var after_up: Dictionary = metas()
			var fired: bool = (after_down.last_meta == rec.name and rec.transition == 0) or (after_up.last_meta == rec.name and rec.transition == 1)
			print("INPUT1 key %s %s: %s" % [rec.name, label, "fired" if fired else "NOT FIRED (last %s / %s)" % [after_down.last_meta, after_up.last_meta]])
			if not fired:
				failed.append(rec.name)
			continue
		var gk := godot_key(rec.key)
		if gk == KEY_NONE:
			print("INPUT1 key %s %s: NOT FIRED (no Godot key for %s)" % [rec.name, label, rec.key])
			failed.append(rec.name)
			continue
		await key(gk, mods)
		await game._step(3)
		var after: Dictionary = metas()
		var made: int = after.metas - before.metas
		var recent: PackedStringArray = after.get("recent", PackedStringArray())
		var ok: bool = made >= 1 and Array(recent.slice(maxi(0, recent.size() - made))).has(rec.name)
		print("INPUT1 key %s %s: %s" % [rec.name, label, "fired" if ok else "NOT FIRED (metas %d: %s)" % [made, recent.slice(maxi(0, recent.size() - made))]])
		if not ok:
			failed.append(rec.name)
		if rec.name == "SPELL_STORE":
			await key(gk, mods) # ` again closes the store (RW 0x71C6AF)
			await game._step(3)
	shot("keys")
	if not failed.is_empty():
		return "%d of %d CommandMap records did not fire: %s" % [failed.size(), ordered.size(), ", ".join(failed)]
	return ""


func _scenario_camera() -> String:
	var c0: Dictionary = hud.get_camera()
	await key(KEY_F1, ["ctrl"]) # SAVE_VIEW1
	hud.camera_look_at(c0.position + Vector2(500, 400))
	await game._step(10)
	var moved: Dictionary = hud.get_camera()
	await key(KEY_F1) # VIEW_VIEW1
	await game._step(10)
	var back: Dictionary = hud.get_camera()
	print("INPUT1 camera: stored %s, moved to %s, F1 -> %s" % [c0.position, moved.position, back.position])
	if back.position.distance_to(c0.position) > 2.0:
		return "F1 did not bring the stored view back (%s, stored %s)" % [back.position, c0.position]
	var free0: bool = back.free_camera
	await key(KEY_Z, ["ctrl"])
	await game._step(5)
	var free1: bool = hud.get_camera().free_camera
	await key(KEY_Z, ["ctrl"])
	await game._step(5)
	var free2: bool = hud.get_camera().free_camera
	print("INPUT1 camera: free camera %s -> Ctrl+Z %s -> Ctrl+Z %s (toggles %d)" % [free0, free1, free2, hud.get_camera().get("free_camera_toggles", -1)])
	if free1 == free0 or free2 != free0:
		return "Ctrl+Z did not toggle the free camera"
	await wheel(window_size() * Vector2(0.5, 0.4), -20)
	await game._step(10)
	await key(KEY_KP_5) # CAMERA_RESET
	await game._step(10)
	var r: Dictionary = hud.get_camera()
	print("INPUT1 camera: numpad 5 -> height %.0f (max %.0f)" % [r.height_above_ground, r.max_height])
	if absf(r.height_above_ground - r.max_height) > 1.0:
		return "numpad 5 did not reset the camera height"
	return ""


func _scenario_misc() -> String:
	var st0: Dictionary = hud.get_state()
	await key(KEY_F12) # TAKE_SCREENSHOT
	await game._step(5)
	var shot_file: String = hud.get_state().get("screenshot", "")
	print("INPUT1 misc: F12 -> %s" % shot_file)
	if shot_file == "" or shot_file.begins_with("failed") and DisplayServer.get_name() != "headless":
		return "F12 saved no screenshot"
	await key(KEY_QUOTELEFT) # SPELL_STORE (on the release)
	await game._step(10)
	var open1: bool = hud.get_state().get("spell_store_open", false)
	await key(KEY_QUOTELEFT)
	await game._step(10)
	var open2: bool = hud.get_state().get("spell_store_open", true)
	print("INPUT1 misc: ` -> spell store open %s, ` again -> %s" % [open1, open2])
	if not open1 or open2:
		return "` did not open and close the spell store"
	var units := local_units()
	if not units.is_empty():
		await select_by_click(units[0])
		var stops := log_count("MSG_DO_STOP")
		await key(KEY_S)
		await game._step(5)
		print("INPUT1 misc: S -> MSG_DO_STOP x%d" % (log_count("MSG_DO_STOP") - stops))
		# RotWK sends it twice when the selection's Stop button reads "&Stop": the STOP record takes the key's press (MetaEventTranslator), the
		# HotKeyTranslator its release (RW 0x75B068 sees MSG_RAW_KEY_UP only)
		if log_count("MSG_DO_STOP") - stops < 1:
			return "S sent no MSG_DO_STOP"
	return ""


func _scenario_hotkeys() -> String:
	var fortress: Dictionary = {}
	for o in world.get_player_objects(game._local_name):
		if o.structure and String(o.template).ends_with("Fortress"):
			fortress = o
	if fortress.is_empty():
		return "no fortress"
	hud.camera_look_at(Vector2(fortress.x, fortress.y))
	await game._step(10)
	await click(to_pixel(fortress.x, fortress.y))
	await game._step(10)
	var buttons: Array = hud.get_command_buttons()
	var st: Dictionary = hud.get_state()
	var hk: Array = st.get("hotkeys", [])
	print("INPUT1 hotkeys: selection %s, %d buttons, hotkeys %s" % [hud.get_selection(), buttons.size(), hk])
	var pick: Dictionary = {}
	for h in hk:
		if h.enabled:
			pick = h
			break
	if pick.is_empty():
		return "the fortress's buttons register no enabled hotkey"
	var before: PackedStringArray = hud.get_message_log()
	var k: Key = OS.find_keycode_from_string(String(pick.key).to_upper())
	await key(k)
	await game._step(10)
	var after: PackedStringArray = hud.get_message_log()
	var new_msgs := after.slice(before.size())
	var outcome: Dictionary = hud.get_state().get("hotkey_outcomes", {})
	print("INPUT1 hotkeys: %s (slot %d) -> outcomes %s, messages %s, placing %s" % [pick.key, pick.slot, outcome, new_msgs, hud.get_placement()])
	if outcome.get("enabled", 0) < 1:
		return "the hotkey %s pressed no button" % pick.key
	return ""


func _modifier_event(code: Key, down: bool, right: bool, ctrl_held: bool) -> InputEventKey:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.location = KEY_LOCATION_RIGHT if right else KEY_LOCATION_LEFT
	e.pressed = down
	e.ctrl_pressed = ctrl_held
	return e


func _pointer(p: Vector2, ctrl: bool) -> void:
	var m := InputEventMouseMotion.new()
	m.position = p
	m.global_position = p
	m.ctrl_pressed = ctrl
	Input.parse_input_event(m)
	await game._step(2)


func _scenario_modifiers() -> String:
	var units := local_units()
	if units.is_empty():
		return "no local unit"
	hud.camera_look_at(Vector2(units[0].x, units[0].y))
	await game._step(10)
	await select_by_click(units[0])
	var centre := window_size() * Vector2(0.5, 0.3)
	Input.parse_input_event(_modifier_event(KEY_CTRL, true, false, true))
	await game._step(2)
	Input.parse_input_event(_modifier_event(KEY_CTRL, true, true, true))
	await game._step(2)
	Input.parse_input_event(_modifier_event(KEY_CTRL, false, false, true)) # left up, right still held
	await game._step(2)
	await _pointer(centre, true)
	var st: Dictionary = metas()
	print("INPUT1 modifiers: left Ctrl up with right Ctrl held: key state %d" % st.key_state)
	var before := log_count("MSG_CREATE_TEAM3")
	var e := InputEventKey.new()
	e.keycode = KEY_3
	e.physical_keycode = KEY_3
	e.ctrl_pressed = true
	e.pressed = true
	Input.parse_input_event(e)
	await game._step(2)
	var u := e.duplicate()
	u.pressed = false
	Input.parse_input_event(u)
	await game._step(5)
	var made := log_count("MSG_CREATE_TEAM3") - before
	print("INPUT1 modifiers: 3 with right Ctrl held -> MSG_CREATE_TEAM3 x%d" % made)
	if made != 1:
		return "right Ctrl alone did not hold Ctrl (key state %d)" % metas().key_state
	# the right release never reaches the window; the pointer's next event says no Ctrl
	await _pointer(centre + Vector2(10, 0), false)
	var st2: Dictionary = metas()
	print("INPUT1 modifiers: after a lost release the pointer says no Ctrl: key state %d" % st2.key_state)
	if st2.key_state != 0:
		return "the pointer's flags did not release the lost Ctrl (key state %d)" % st2.key_state
	return ""
