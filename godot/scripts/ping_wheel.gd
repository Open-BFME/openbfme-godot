## Ping wheel (NOT RETAIL, an OpenBFME addition): hold the ping key over the map and a wheel opens at the pointer. Move the pointer towards a
## sector and let go of the key: a ping of that kind is placed where the wheel opened (scripts/ping_board.gd shows it). Letting go in the middle places
## "Look here". A left click also places the selected ping; a right click, Esc or the window losing the focus closes the wheel without one.
##   up     Attack here        right  Reinforcements
##   down   Danger, fall back  left   Need help
## The key is an Options.ini choice (Interface tab: OpenBFMEPingKey):
##   * G held (the default). G is RotWK's key too: the CommandMap's STANCE_HOLDGROUND and the hotkey of many command buttons (the letter after '&' in
##     their labels). So G is shared by time: held for HOLD_MS it opens the wheel; let go sooner (a tap) and the wheel hands the press and the release
##     to the game, which then acts on it as on any G (the stance, the button). The game sees a tap's G when the key comes up, not when it went down. A
##     key or a click before HOLD_MS also hands the G to the game first, and so does a G over a pixel with no point to ping (the HUD's buttons).
##   * Alt+G: no CommandMap record uses KEY_G with ALT, and with Alt held a command button's letter does nothing (RotWK's HotKeyTranslator refuses a key
##     with Ctrl or Alt, RW 0x75B0E1, GameClient/MessageStream/HotKey.cpp); J: no CommandMap record and no command button hotkey uses it; or one of the
##     mouse's two side buttons. tools/opts/test_ping_wheel_keys.py checks these against the retail files. If a mod's CommandMap binds Alt+G or J, the
##     wheel steps aside and the mod keeps the key (the Ctrl+Z free camera does the same, GodotInGameHud.cpp).
## The node sits after the HUD in the game's tree, so it sees the key before the HUD does, and a key or click it uses goes no further.
## Presentation only: it reads the pointer and the picked ground point; nothing it does reaches the simulation.
extends CanvasLayer

const PingIcon := preload("res://scripts/ping_icon.gd")
const PingBoard := preload("res://scripts/ping_board.gd")

## the key choices of OpenBFMEPingKey (scripts/game_options.gd EXTRAS): a keyboard key ("key", held with Alt when "alt"; "hold": shared with the game by
## time, see above; "commandmap": its CommandMap name, which the retail test checks) or a mouse button ("button")
const BINDINGS := {
	"g": {"label": "G (hold)", "key": KEY_G, "alt": false, "hold": true},
	"altg": {"label": "Alt+G", "key": KEY_G, "alt": true, "commandmap": "KEY_G"},
	"j": {"label": "J", "key": KEY_J, "alt": false, "commandmap": "KEY_J"},
	"mouse4": {"label": "Mouse back button", "button": MOUSE_BUTTON_XBUTTON1},
	"mouse5": {"label": "Mouse forward button", "button": MOUSE_BUTTON_XBUTTON2},
}
const DEFAULT_BINDING := "g"
const HOLD_MS := 180      # a held G opens the wheel after this long; a shorter tap is the game's G
## the sectors clockwise from the right (screen angle 0, 90, 180, 270 degrees), as ping_board.gd KINDS indexes; the middle is kind 0 ("here")
const SECTOR_KINDS := [2, 3, 4, 1]
const RADIUS := 130.0     # at 1080 lines
const DEAD_ZONE := 0.34   # of the radius: the middle ("here")
const OPEN_SECONDS := 0.08
const REPLAY_META := &"ping_wheel_replay"

signal pinged(kind: int, world_xy: Vector2, accepted: bool)

var board: Node              # scripts/ping_board.gd
var binding := DEFAULT_BINDING
var enabled := true
var can_open := Callable()   # () -> bool: a game is on and no menu has the input (game.gd); unset: always
var pick := Callable()       # (pixel: Vector2) -> Vector2 SAGE x, y; x < -1e8 for none (InGameHudNode.ping_point: the minimap's point over the minimap)
var commandmap: Array = []  # the game's CommandMap records (InGameHudNode.get_command_map): a record on the key keeps it (a mod's)
var font: Font
var hold_ms := HOLD_MS
var opened_total := 0
var taps_passed := 0         # G taps handed to the game (tests)
var blocked_by := ""         # the CommandMap record that keeps the key, "" when the key is the wheel's

var _open := false
var _centre := Vector2.ZERO
var _world := Vector2.ZERO
var _pointer := Vector2.ZERO
var _pointer_seen := false
var _age := 0.0
var _swallow_button := -1    # a mouse button whose release the wheel takes (its press closed the wheel)
var _key_held := false       # the binding's key or button went down for the wheel: its repeats and its release are the wheel's, even after Esc or a click
                             # closed the wheel (a G released after Alt would otherwise reach the HUD as a command button's hotkey)
var _pending: InputEventKey  # a held binding's press, waiting for HOLD_MS (null: none)
var _pending_ms := 0
var _pending_at := Vector2.ZERO
var _passthrough := false    # the binding's press was handed to the game: its repeats and release are the game's too
var _canvas: Control


func _ready() -> void:
	layer = 110
	name = "PingWheel"
	_canvas = Control.new()
	_canvas.name = "Wheel"
	_canvas.set_anchors_preset(Control.PRESET_FULL_RECT)
	_canvas.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_canvas.draw.connect(_draw_wheel)
	add_child(_canvas)


## set the key from the option's value; an unknown value is an error the caller reports (the option stays on its default)
func set_binding(value: String) -> bool:
	if not BINDINGS.has(value):
		return false
	binding = value
	return true


## the options' settings (OpenBFMEPingWheel on, OpenBFMEPingKey): from the game's start and every edit of the Options screen
func configure(on: bool, key: String) -> void:
	if not set_binding(key):
		printerr("GAME ping wheel: OpenBFMEPingKey '%s' is not one of %s; the key stays %s" % [key, ", ".join(BINDINGS.keys()), binding])
	blocked_by = commandmap_clash(commandmap)
	enabled = on and blocked_by.is_empty()
	if not enabled:
		close(false)
		_drop_pending()
	var why := "" if blocked_by.is_empty() else " (the CommandMap's %s uses %s)" % [blocked_by, BINDINGS[binding].label]
	print("GAME ping wheel: %s, key %s%s" % ["on" if enabled else "off", BINDINGS[binding].label, why])


## the CommandMap record (InGameHudNode.get_command_map) that already uses the key with the binding's modifiers (any record on J; one with Alt on G):
## a mod's record keeps it. "" when free. A held G shares the key with the game by design (a tap is the game's), so nothing keeps it
func commandmap_clash(records: Array) -> String:
	var b: Dictionary = BINDINGS[binding]
	if not b.has("commandmap"):
		return ""
	for r in records:
		if str(r.get("key", "")) == b.commandmap and (not b.alt or bool(r.get("alt", false))):
			return str(r.get("name", "?"))
	return ""


func is_open() -> bool:
	return _open


## a held binding's press is waiting for HOLD_MS (tests)
func is_pending() -> bool:
	return _pending != null


## the kind the pointer selects now (ping_board.gd KINDS index)
func selection() -> int:
	var v := _pointer - _centre
	if v.length() < _radius() * DEAD_ZONE:
		return 0
	var sector := wrapi(roundi(v.angle() / (PI * 0.5)), 0, 4)
	return SECTOR_KINDS[sector]


func _radius() -> float:
	var s := get_viewport().get_visible_rect().size if is_inside_tree() else Vector2(1920, 1080)
	return RADIUS * clampf(minf(s.x / 1920.0, s.y / 1080.0), 0.85, 2.0) # never smaller than a hand's width on a small window


func _input(event: InputEvent) -> void:
	if event.has_meta(REPLAY_META):
		return # a tap's G handed back to the game
	var motion := event as InputEventMouseMotion
	if motion != null:
		_pointer = motion.position
		_pointer_seen = true
		if _open:
			_canvas.queue_redraw()
		return
	var key := event as InputEventKey
	var button := event as InputEventMouseButton
	if _passthrough and _is_binding(event):
		if not event.is_pressed():
			_passthrough = false
		return # the game's key
	if _pending != null:
		if _is_binding(event):
			_handled()
			if not event.is_pressed():
				_tap() # let go before HOLD_MS: the game's G
			return
		if (key != null and key.pressed and not key.echo) or (button != null and button.pressed):
			_hand_over() # another key or a click first: the G was the game's (it reaches the game before this event's own handlers act on it)
		return
	if button != null and button.button_index == _swallow_button and not button.pressed:
		_swallow_button = -1
		_handled()
		return
	if _key_held and not _open and _is_binding(event):
		_handled()
		if not event.is_pressed():
			_key_held = false
		return
	if _open:
		if _is_binding(event) and not event.is_pressed():
			_key_held = false
		if key != null and key.keycode == KEY_ESCAPE and key.pressed:
			_handled()
			close(false)
		elif _is_binding(event) and not event.is_pressed():
			_handled()
			close(true)
		elif _is_binding(event) or (key != null and key.echo):
			_handled()
		elif button != null and button.pressed and (button.button_index == MOUSE_BUTTON_LEFT or button.button_index == MOUSE_BUTTON_RIGHT):
			_handled()
			_swallow_button = button.button_index
			close(button.button_index == MOUSE_BUTTON_LEFT)
		return
	if not enabled or not _is_binding(event) or not event.is_pressed() or (key != null and key.echo):
		return
	if key != null and (key.ctrl_pressed or key.shift_pressed or key.meta_pressed or key.alt_pressed != bool(BINDINGS[binding].alt)):
		return # Ctrl+G, Alt+G with the held G, G without Alt with Alt+G, Ctrl+J ... stay the game's
	if can_open.is_valid() and not can_open.call():
		return
	if not _pointer_seen:
		_pointer = get_viewport().get_mouse_position()
	_handled()
	if bool(BINDINGS[binding].get("hold", false)):
		_pending = key.duplicate()
		_pending_ms = Time.get_ticks_msec()
		_pending_at = _pointer
		return
	_key_held = true # the key is the wheel's until it is released, even over a pixel with no point (the wheel then stays shut)
	open_at(_pointer)


## open the wheel at `pixel` (the ping goes to the point under it); false when that pixel has none
func open_at(pixel: Vector2) -> bool:
	var w: Vector2 = pick.call(pixel) if pick.is_valid() else pixel
	if w.x < -1e8:
		return false
	_open = true
	_centre = pixel
	_pointer = pixel
	_world = w
	_age = 0.0
	opened_total += 1
	_canvas.queue_redraw()
	return true


## close the wheel; `place`: place the selected ping
func close(place: bool) -> void:
	if not _open:
		return
	var kind := selection()
	_open = false
	_canvas.queue_redraw()
	if not place:
		return
	var accepted: bool = board != null and board.request_ping(kind, _world)
	pinged.emit(kind, _world, accepted)


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		close(false)
		_drop_pending()
		_key_held = false # the release goes to the other window
		_passthrough = false


func _process(delta: float) -> void:
	if _pending != null and Time.get_ticks_msec() - _pending_ms >= hold_ms:
		var at := _pending_at
		if can_open.is_valid() and not can_open.call():
			_hand_over() # a menu came up meanwhile: the G is the game's
		else:
			var pointer := _pointer # a quick flick towards a choice during the wait counts: the wheel opens where G went down, the pointer stays where it is
			if open_at(at):
				_pointer = pointer
				_pending = null
				_key_held = true
				_canvas.queue_redraw()
				return
			_hand_over() # nothing to ping under the pointer (the HUD's buttons): the G is the game's (a button's hotkey)
	if _open and _age < OPEN_SECONDS:
		_age += delta
		_canvas.queue_redraw()


## a held binding let go before HOLD_MS: the game gets its press and its release
func _tap() -> void:
	var down := _pending
	_pending = null
	var up: InputEventKey = down.duplicate()
	up.pressed = false
	_replay(down)
	_replay(up)
	taps_passed += 1


## the held binding's press goes to the game; its repeats and its release follow it there
func _hand_over() -> void:
	var down := _pending
	_pending = null
	_passthrough = true
	_replay(down)
	taps_passed += 1


func _drop_pending() -> void:
	_pending = null


func _replay(e: InputEventKey) -> void:
	e.set_meta(REPLAY_META, true)
	Input.parse_input_event(e)


## the binding's key or button, pressed or released, whatever the modifiers (the release closes the wheel even when Alt went up first)
func _is_binding(event: InputEvent) -> bool:
	var b: Dictionary = BINDINGS[binding]
	if b.has("key"):
		var key := event as InputEventKey
		return key != null and (key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode) == b.key
	var button := event as InputEventMouseButton
	return button != null and button.button_index == b.button


func _handled() -> void:
	if is_inside_tree():
		get_viewport().set_input_as_handled()


# ---- drawing --------------------------------------------------------------------------------------------------------------------------------------------

func _draw_wheel() -> void:
	if not _open:
		return
	var grow := clampf(_age / OPEN_SECONDS, 0.0, 1.0)
	var R := _radius() * lerpf(0.8, 1.0, grow)
	var r0 := R * DEAD_ZONE
	var c := _centre
	var sel := selection()
	var alpha := lerpf(0.4, 1.0, grow)
	var gold := Color(PingIcon.GOLD, alpha)
	var rim := maxf(2.0, R * 0.022)
	# the four sectors: a dark ring, the selected one lit in its kind's colour
	for s in 4:
		var kind: int = SECTOR_KINDS[s]
		var col: Color = PingBoard.KINDS[kind].color
		var a0 := (s - 0.5) * PI * 0.5
		var poly := _sector(c, r0 * 1.08, R, a0 + 0.035, a0 + PI * 0.5 - 0.035)
		var fill := Color(col.darkened(0.45), 0.86 * alpha) if kind == sel else Color(PingIcon.DARK, 0.72 * alpha)
		_canvas.draw_colored_polygon(poly, fill)
		var outline := poly.duplicate()
		outline.append(poly[0])
		_canvas.draw_polyline(outline, Color(col, alpha) if kind == sel else Color(gold, 0.75 * alpha), rim * (1.5 if kind == sel else 1.0), true)
		var mid := c + Vector2.from_angle(a0 + PI * 0.25) * (r0 * 1.08 + R) * 0.5
		var icon_r := R * (0.2 if kind == sel else 0.17)
		PingIcon.draw_glyph(_canvas, PingBoard.KINDS[kind].id, mid, icon_r, Color(col, alpha) if kind == sel else Color(col.lerp(Color.WHITE, 0.25), 0.8 * alpha))
	# the middle: "Look here"
	_canvas.draw_circle(c, r0, Color(PingBoard.KINDS[0].color.darkened(0.5), 0.88 * alpha) if sel == 0 else Color(PingIcon.DARK, 0.8 * alpha))
	_canvas.draw_arc(c, r0, 0.0, TAU, 40, gold if sel != 0 else Color(PingBoard.KINDS[0].color, alpha), rim * (1.5 if sel == 0 else 1.0), true)
	PingIcon.draw_glyph(_canvas, "here", c, r0 * 0.55, Color(PingBoard.KINDS[0].color, alpha))
	# the pointer's direction
	var v := _pointer - c
	if v.length() > r0:
		_canvas.draw_line(c + v.normalized() * r0, c + v.normalized() * minf(v.length(), R), Color(gold, 0.6 * alpha), rim, true)
	# the selected kind's name under the wheel
	var f := font if font != null else ThemeDB.fallback_font
	var fs := maxi(16, roundi(R * 0.19))
	var text: String = PingBoard.KINDS[sel].label
	var tw := f.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x
	var at := c + Vector2(-tw * 0.5, R + fs * 1.3)
	_canvas.draw_string_outline(f, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, maxi(3, fs / 5), Color(0, 0, 0, alpha))
	_canvas.draw_string(f, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(PingBoard.KINDS[sel].color.lerp(Color.WHITE, 0.35), alpha))


## an annulus sector between radii r0 and r1 and angles a0 .. a1 (screen angles, clockwise from the right)
static func _sector(c: Vector2, r0: float, r1: float, a0: float, a1: float) -> PackedVector2Array:
	const STEPS := 16
	var pts := PackedVector2Array()
	for i in STEPS + 1:
		pts.append(c + Vector2.from_angle(lerpf(a0, a1, float(i) / STEPS)) * r1)
	for i in range(STEPS, -1, -1):
		pts.append(c + Vector2.from_angle(lerpf(a0, a1, float(i) / STEPS)) * r0)
	return pts
