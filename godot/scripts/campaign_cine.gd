## Lane SCRIPT-1: the presentation of the map script engine's client requests in a viewer (map_viewer.gd --campaign). The logic's ScriptEngine records
## the camera, letterbox, caption and fade actions (ScriptClientRequest); GameWorld.update_script_view applies them on the render clock
## (GameClient/ScriptCameraDirector: ZH W3DView moveCameraTo / resetCamera / rotateCamera as donor, stop S-1182) and this node places the camera and
## draws the letterbox bars, the military caption (the label's game text) and the fade. Every request is printed (SCRIPTREQ lines) for the reports.
extends Node

var world: Node3D
var camera: Camera3D
var target := Vector3.ZERO   # SAGE x, y, z the camera looks at
var angle := 0.0             # SAGE camera angle (radians)
var distance := 520.0        # camera distance from the look-at point (inference: the RotWK default framing is CAM-1's; S-1182)
var pitch := deg_to_rad(-40.0)
var _top: ColorRect
var _bottom: ColorRect
var _caption: Label
var _fade: ColorRect
var _status: Label
var _note: Label         # lane SCRIPT-2: DISPLAY_NOTIFICATION_BOX's text
var _objectives: Label   # lane SCRIPT-2: SHOW_MISSION_OBJECTIVE / MARK_MISSION_OBJECTIVE_COMPLETED
var _audio: Node         # lane SCRIPT-2: GameAudio (the listener follows the camera)
var drive := ""          # lane SCRIPT-2: "angmar" plays the first objective's part of the player (map_viewer --cine-drive); lane SCRIPT-3: "rhudaur" the whole mission
var _drive_frame := 0     # lane SCRIPT-3: the frame the current drive step started
var _look := ""           # lane SCRIPT-3: the unit / team the camera follows between the script's own moves
var _drive_step := 0
var _base_distance := 520.0
var _base_pitch := deg_to_rad(-40.0)
var hud_mode := false      # lane CAMP-1: in the game (scripts/campaign_flow.gd) the HUD's camera rules outside the cinematics (letterbox, a script move, input off)
var movie_hook := Callable() # lane CAMP-1H: PLAY_MOVIE_IN_GAME's title goes there (scripts/campaign_flow.gd plays it with scripts/movie_player.gd)
var dev_overlay := true      # lane CAMP-2: the developer text (logic frame, camera / input state, the objective list): the viewers' only; the game sets it from --dev-overlay
var input_hook := Callable() # lane OWNERFIX-1: the scripts' DISABLE_INPUT / ENABLE_INPUT (bool enable) (game.gd _script_input)
var _ui: CanvasLayer = null # lane SAVE-UI1: the presentation's layer (set_covered)
var control_bar_hook := Callable() # lane CAMP-2: HideControlBar / ShowControlBar (bool hidden) for CAMERA_LETTERBOX_BEGIN / END and HIDE_UI / SHOW_UI (game.gd)
var hud: Object = null       # lane CAMPUI-1: the game's HUD (GodotInGameHud): in hud_mode the script's camera moves drive its TacticalCamera
var shell: Object = null     # lane CAMPUI-1: the shell (AptMenuPlayer): the caption's font (shell_font, the menus' font substitution)
var _caption_draw: Control   # lane CAMPUI-1: the military caption, typed (RW 0x69CE5F) and drawn (RW 0x69B7BE)
var _caption_style := {}     # MilitaryCaptionSettings (InGameUI.ini + language.ini), {} until read
var _caption_serial := 0     # the director's SHOW_MILITARY_CAPTION count last started
var _sub: Dictionary = {}    # the subtitle being typed (RW 0x69FF85's 0x48-byte record), {} none
var _letterbox_on := false   # lane CAMPUI-1: W3DDisplay::enableLetterBox (RW 0x443C81): + 0xD8 and the fade start + 0xDC
var _letterbox_since := 0
var _was_moving := false     # lane CAMPUI-1: the script camera's last state handed to the HUD camera
var _pending_reset := false  # a RESET_CAMERA whose move has not ended yet
var _zoom_pitch_noted := false
var _box_ready := false      # lane CAMPUI-1: the game's InGameNotificationBox.apt (AptMenuPlayer.notification_box) is up
var objectives_open := Callable() # lane CAMPUI-1: () -> int: 2 the objectives screen is open, 1 the powers screen, 0 none (the box's location, RW 0x69BFAE)


## lane SAVE-UI1: a menu over the game (the quit menu and the screens it opens) covers the script presentation: retail draws the in-game UI before the
## window manager's movies (ZH W3DDisplay::draw: TheInGameUI, then the windows), so the letterbox, captions and notifications are under the menu
func set_covered(covered: bool) -> void:
	if _ui != null:
		_ui.visible = not covered


func setup(w: Node3D, cam: Camera3D, start: Vector3, ui_parent: Node, audio: Node = null) -> void:
	process_priority = -50 # lane RENDERWAIT-1 r2: before GameWorld's process, inside the render frame's client window
	_audio = audio
	world = w
	camera = cam
	target = start
	var ui := CanvasLayer.new()
	ui.layer = 5
	_ui = ui
	_top = ColorRect.new()
	_top.color = Color.BLACK
	_bottom = ColorRect.new()
	_bottom.color = Color.BLACK
	_fade = ColorRect.new()
	_fade.color = Color(0, 0, 0, 0)
	_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_caption = Label.new() # lane CAMPUI-1: kept (empty, hidden) for the readers of the caption's text (game.gd _dev_text_on_screen reads _caption_text())
	_caption.set_meta("game_text", true) # lane CAMP-2: SHOW_MILITARY_CAPTION's game text (game.gd _dev_text_on_screen)
	_caption.visible = false
	_caption_draw = Control.new()
	_caption_draw.set_meta("game_text", true)
	_caption_draw.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_caption_draw.draw.connect(_draw_caption)
	_status = Label.new()
	_status.add_theme_color_override("font_outline_color", Color.BLACK)
	_status.add_theme_constant_override("outline_size", 4)
	ui.add_child(_fade)
	ui.add_child(_top)
	ui.add_child(_bottom)
	ui.add_child(_caption)
	ui.add_child(_caption_draw)
	ui.add_child(_status)
	_note = Label.new()
	_note.set_meta("game_text", true) # lane CAMP-2: DISPLAY_NOTIFICATION_BOX's game text
	_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_note.add_theme_font_size_override("font_size", 18)
	_note.add_theme_color_override("font_color", Color(1.0, 0.85, 0.4))
	_note.add_theme_color_override("font_outline_color", Color.BLACK)
	_note.add_theme_constant_override("outline_size", 5)
	ui.add_child(_note)
	_objectives = Label.new()
	_objectives.add_theme_font_size_override("font_size", 15)
	_objectives.add_theme_color_override("font_outline_color", Color.BLACK)
	_objectives.add_theme_constant_override("outline_size", 4)
	ui.add_child(_objectives)
	ui_parent.add_child(ui)
	if hud_mode and shell != null:
		var b: Dictionary = shell.notification_box({"kind": "begin"})
		_box_ready = bool(b.get("ok", false))
		if not _box_ready:
			printerr("CINE notification box: ", b.get("error", "?"))
	if not hud_mode:
		_place_camera()


var _pending_boxes: Array = [] # merge queue q24: notification boxes shown in the next render frame that holds the logic


func _process(delta: float) -> void:
	if world == null:
		return
	# lane RENDERWAIT-1 r2 (Sol r1 #2): the live reads of this function (the test drives, the terrain's height under the script's target, a drive's
	# follow) happen only in a render frame that holds the logic; the script requests, their camera and the followed object come from published data
	var held: bool = world.is_logic_held()
	if held:
		_drive()
	var camera_request := false # lane CAMPUI-1: a camera action arrived this frame
	for r in world.take_script_requests():
		if String(r.action) in ["MOVE_CAMERA_TO", "RESET_CAMERA", "ROTATE_CAMERA", "CAMERA_LOOK_TOWARD_WAYPOINT", "CAMERA_FOLLOW_NAMED", "CAMERA_STOP_FOLLOW"]:
			camera_request = true
		if r.action == "RESET_CAMERA":
			_pending_reset = true
		var ps: Array = []
		for p in r.params:
			ps.append(p.string if not String(p.string).is_empty() else (str(p.real) if p.type == 1 else str(p.int)))
		print("SCRIPTREQ frame %d %s(%s) [%s]" % [r.frame, r.action, ", ".join(ps), r.script])
		if r.action == "MISSION_OBJECTIVE_SOUND":
			# lane CAMPUI-1: the proof log reads what the audio side played half a second later (merge queue q24, RENDERWAIT-1: in a timer callback, not per frame)
			get_tree().create_timer(0.5).timeout.connect(_report_sound_check, CONNECT_ONE_SHOT)
		if r.action == "PLAY_MOVIE_IN_GAME" and movie_hook.is_valid() and r.params.size() > 0:
			movie_hook.call(String(r.params[0].string))
		# lane CAMP-2: ScriptActions::executeAction (RW 0x7CAFA5): cases 118 / 119 doLetterBoxMode (RW 0x7BC8B7: HideControlBar(true) or ShowControlBar(false),
		# then the display's letterbox), cases 347 / 348 HIDE_UI / SHOW_UI (HideControlBar(true) / ShowControlBar(false)); in the requests' order
		# lane OWNERFIX-1: DISABLE_INPUT / ENABLE_INPUT (RW 0x7BC586 / 0x7BC5E9): the HUD's input flag, the selection and the pointer (InGameHud::scriptInput)
		if input_hook.is_valid():
			if r.action == "DISABLE_INPUT":
				input_hook.call(false)
			elif r.action == "ENABLE_INPUT":
				input_hook.call(true)
		if hud_mode and hud != null and r.params.size() > 0 and r.action in ["FLASH_SPELL_STORE_BUTTON", "FLASH_OBJECTIVES_BUTTON", "FLASH_PLANNING_MODE_BUTTON"]:
			# lane CAMPUI-1: RW 0x7BDF4E .. -> RW 0x6D455F(slot, seconds); a negative count does nothing
			var slot := ["FLASH_SPELL_STORE_BUTTON", "FLASH_OBJECTIVES_BUTTON", "FLASH_PLANNING_MODE_BUTTON"].find(String(r.action))
			if int(r.params[0].int) >= 0:
				hud.flash_palantir_button(slot, int(r.params[0].int))
		if _box_ready and (r.action == "DISPLAY_NOTIFICATION_BOX" or r.action == "DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE"):
			# merge queue q24 (RENDERWAIT-1): the image override reads the live template list, so a box waits for a render frame that holds the logic
			_pending_boxes.append(r)
		if control_bar_hook.is_valid():
			if r.action == "CAMERA_LETTERBOX_BEGIN" or r.action == "HIDE_UI":
				control_bar_hook.call(true)
			elif r.action == "CAMERA_LETTERBOX_END" or r.action == "SHOW_UI":
				control_bar_hook.call(false)
	if hud_mode and hud != null and not _was_moving: # lane CAMPUI-1 (Sol r1 F1): also on the frame a new move arrives
		# lane CAMPUI-1: between the script's camera moves the HUD's camera is the player's; a move starts from where it looks now
		var cam: Dictionary = hud.get_camera()
		if cam.has("position"):
			target = Vector3(cam.position.x, cam.position.y, target.z)
			angle = float(cam.angle)
	var st: Dictionary = world.update_script_view(delta * 1000.0, {"x": target.x, "y": target.y, "z": target.z, "angle": angle})
	if st.has_target and (_look.is_empty() or st.moving) and (not hud_mode or st.moving or camera_request or int(st.get("following", 0)) != 0): # a drive's follow (lane SCRIPT-3) holds the camera between the script's moves; in the game the HUD camera is the player's between moves (an instant move or reset ends in the frame it arrives)
		target = Vector3(st.x, st.y, world.get_ground_height(st.x, st.y) if held else target.z) # the height follows in the next held frame
	if st.has_angle:
		angle = st.angle
	if not st.moving and held:
		_look_at_now() # lane SCRIPT-3: a drive's follow between the script's own camera moves
	if hud_mode and hud != null:
		_drive_hud_camera(st, camera_request)
	else:
		# ZOOM_CAMERA / PITCH_CAMERA: factors of the default framing (S-1182), the viewers' own camera
		distance = _base_distance * float(st.get("zoom", 1.0))
		pitch = _base_pitch * float(st.get("pitch", 1.0))
		_place_camera()
	if _audio != null and not hud_mode: # lane CAMPUI-1: in the game the listener is the game's (it follows the HUD camera); here only the caption's click
		_audio.set_listener(Vector3(target.x, target.y, target.z), Vector3(sin(angle), cos(angle), 0.0))
	var size := get_viewport().get_visible_rect().size
	# lane CAMPUI-1: W3DDisplay::renderLetterBox (RW 0x443939, ZH's non-SLIDE_LETTERBOX path): bars of (H - 0.5625 W) / 2 (none at 16:9 and wider), black at
	# alpha fade * 255, the fade 1 s (0.001 / ms) from enableLetterBox (RW 0x443C81); fading out, only the top bar is drawn
	if bool(st.letterbox) != _letterbox_on:
		_letterbox_on = bool(st.letterbox)
		_letterbox_since = Time.get_ticks_msec()
		if hud != null:
			hud.camera_set_zoom_limited(not _letterbox_on) # RW 0x443C81: TheTacticalView slot 0x1C setZoomLimited(!enable)
	var fade_in := clampf(float(Time.get_ticks_msec() - _letterbox_since) * 0.001, 0.0, 1.0)
	var level := fade_in if _letterbox_on else 1.0 - fade_in
	var bar := maxf(0.0, (size.y - 0.5625 * size.x) * 0.5)
	_top.position = Vector2.ZERO
	_top.size = Vector2(size.x, bar)
	_bottom.position = Vector2(0, size.y - bar)
	_bottom.size = Vector2(size.x, bar)
	_top.color = Color(0, 0, 0, level)
	_bottom.color = Color(0, 0, 0, level)
	_top.visible = bar > 0.0 and level > 0.0
	_bottom.visible = bar > 0.0 and level > 0.0 and _letterbox_on
	_fade.position = Vector2.ZERO
	_fade.size = size
	_fade.color = Color(0, 0, 0, st.fade)
	_caption.text = ""
	_update_caption(st, size)
	_status.visible = dev_overlay # lane CAMP-2: developer text never shows in the real game (the owner saw "frame 90   input disabled")
	_objectives.visible = dev_overlay
	_status.text = "frame %d   %s%s" % [world.get_presented_frame(), "camera moving   " if st.moving else "", "input disabled" if st.input_disabled else ""] if dev_overlay else ""
	_status.position = Vector2(12, bar + 8.0)
	if held and not _pending_boxes.is_empty():
		for r in _pending_boxes:
			_show_notification(r)
		_pending_boxes.clear()
	_note.text = "" if _box_ready else st.get("notification_text", "") # lane CAMPUI-1: in the game the box is InGameNotificationBox.apt
	if _box_ready:
		var u: Dictionary = shell.notification_box({"kind": "update", "now_ms": Time.get_ticks_msec(), "letterboxed": _letterbox_on})
		var audio := String(u.get("audio", ""))
		if not audio.is_empty() and _audio != null:
			var handle: int = _audio.play_sound(audio) # RW 0x8EB8CA: the type's OpenAudio (Gui_DaveBox by default)
			print("CINE notification opened: %s (open audio %s, handle %d)" % [String(u.get("title", "")), audio, handle])
	_note.position = Vector2(size.x * 0.15, bar + 40.0)
	_note.size = Vector2(size.x * 0.7, 60.0)
	var lines: PackedStringArray = []
	for o in st.get("objectives", []):
		lines.append(("[x] " if o.completed else "[ ] ") + "objective %d" % o.index)
	_objectives.text = "\n".join(lines) if dev_overlay else ""
	_objectives.position = Vector2(size.x - 220.0, bar + 40.0)


## lane CAMPUI-1: in the game the script's camera actions move the HUD's own camera (the player's TacticalCamera: retail has one view), so the framing is
## retail's default (RW 0x6311ED: InitialCameraPosition, the map's camera pitch / height) and the player keeps the camera where a cinematic leaves it.
## MOVE_CAMERA_TO / the follow: View::lookAt each frame of the move; RESET_CAMERA: at its end the view's resetCamera (slot 0xC8: default zoom / pitch);
## ROTATE_CAMERA / CAMERA_LOOK_TOWARD_WAYPOINT: the angle. ZOOM_CAMERA / PITCH_CAMERA factors are not applied to the HUD camera (stop S-1182, reported once)
func _drive_hud_camera(st: Dictionary, camera_request: bool) -> void:
	var moving := bool(st.moving)
	var following := int(st.get("following", 0)) != 0
	if bool(st.has_target) and (moving or _was_moving or camera_request or following):
		hud.camera_look_at(Vector2(target.x, target.y))
	if bool(st.has_angle) and (moving or _was_moving or camera_request):
		var cam: Dictionary = hud.get_camera()
		hud.camera_set_angle_pitch(angle, float(cam.get("pitch", 0.0)))
	if _pending_reset and not moving:
		_pending_reset = false
		hud.camera_reset(Vector2(target.x, target.y))
	_was_moving = moving
	if not _zoom_pitch_noted and (absf(float(st.get("zoom", 1.0)) - 1.0) > 0.0001 or absf(float(st.get("pitch", 1.0)) - 1.0) > 0.0001):
		_zoom_pitch_noted = true
		print("CINE STOP [S-1182] ZOOM_CAMERA / PITCH_CAMERA (zoom %.3f, pitch %.3f) are not applied to the game's camera" % [float(st.get("zoom", 1.0)), float(st.get("pitch", 1.0))])


## lane CAMPUI-1 (stop S-1711 ported): the military caption. InGameUI::militarySubtitle (RW 0x69FF85): the label's text plus a space, its duration (ms),
## the real-time clock; one display string per line (at most 4), the first line's x offset -width / 2 when MilitaryCaptionCentered (RW 0x69CDAA: the width
## up to the next newline, so a centred line is placed for its whole width and typed from its left). update (RW 0x69CE5F): the elapsed time grows by
## min(dt, 100) ms unless the game is paused; while typing, when the next character is due (+ 0x34 < + 0x38) one character goes in (spaces at once with
## it), MiscAudio's MissionBriefingCharacterClick (MiscAudio + 0xBC) per character, the next due MilitaryCaptionDelayMS later; a newline starts the next
## line (a fifth ends the typing); after the last character the lifetime is max(duration, now + 2 s); past it the alpha drops by (int)(dt * 0.1 + 1)
## a frame and the caption goes at 0. Drawn (RW 0x69B7BE) at (floor(W * x + 0.5) + offset, floor(H * y + 0.5) + line * height) in the colour with a
## one-pixel black drop shadow. INFERENCE: the incoming character's 30-pixel slide over its DelayMS (RW 0x69B7BE's + 0x28 string) is not drawn
func _update_caption(st: Dictionary, size: Vector2) -> void:
	var serial := int(st.get("caption_serial", 0))
	if serial != _caption_serial:
		_caption_serial = serial
		_start_caption(String(st.get("caption_text", "")), float(st.get("caption_duration_ms", 0.0)), size)
	if not _sub.is_empty():
		_tick_caption()
	_caption_draw.position = Vector2.ZERO
	_caption_draw.size = size
	_caption_draw.queue_redraw()


func _caption_font(size_x: float) -> Dictionary:
	if _caption_style.is_empty():
		_caption_style = world.get_military_caption_settings()
		if not _caption_style.get("ok", false):
			printerr("CINE caption settings: ", _caption_style.get("error", "?"))
	if not _caption_style.get("ok", false) or shell == null:
		return {}
	# GlobalLanguage::adjustFontSize (RW 0x5E9BEF): floor(size * xResolution / 1024)
	var px := floorf(float(_caption_style.point_size) * size_x / 1024.0)
	var f: Dictionary = shell.shell_font(String(_caption_style.font), px)
	return f


func _start_caption(text: String, duration_ms: float, size: Vector2) -> void:
	_sub = {}
	if text.is_empty() or duration_ms <= 0.0:
		return # RW 0x69FF85: an empty text or a duration <= 0 makes no subtitle
	var f := _caption_font(size.x)
	if f.is_empty():
		return
	_sub = {"text": text + " ", "index": 0, "lines": [""], "offsets": [0.0], "line": 0, "lifetime": duration_ms, "due": 0.0, "elapsed": 0.0,
		"last": Time.get_ticks_msec(), "alpha": int(_caption_style.color.a8), "done": false, "font": f.font, "size": float(f.size)}
	if bool(_caption_style.centered):
		_sub.offsets[0] = -floorf(_line_width(0) / 2.0)


func _line_width(from: int) -> float:
	var text: String = _sub.text
	var end := text.find("\n", from)
	var part := text.substr(from, -1 if end < 0 else end - from)
	var font: Font = _sub.font
	return font.get_string_size(part, HORIZONTAL_ALIGNMENT_LEFT, -1, int(_sub.size)).x


func _tick_caption() -> void:
	var now := Time.get_ticks_msec()
	var dt := mini(now - int(_sub.last), 100)
	_sub.last = now
	if get_tree().paused:
		return
	_sub.elapsed = float(_sub.elapsed) + dt
	var text: String = _sub.text
	if not _sub.done:
		if float(_sub.due) < float(_sub.elapsed):
			var ch := text[int(_sub.index)]
			while ch == " ":
				_sub.lines[int(_sub.line)] += " "
				_sub.index = int(_sub.index) + 1
				if int(_sub.index) >= text.length():
					break
				ch = text[int(_sub.index)]
			if ch == "\n":
				_sub.line = int(_sub.line) + 1
				if int(_sub.line) < 4:
					_sub.lines.append("")
					_sub.offsets.append(-floorf(_line_width(int(_sub.index) + 1) / 2.0) if bool(_caption_style.centered) else 0.0)
					_sub.due = float(_sub.elapsed) + float(_caption_style.delay_ms)
				else:
					_sub.index = text.length()
			else:
				_sub.lines[int(_sub.line)] += ch
				if _audio != null:
					_audio.play_misc("MissionBriefingCharacterClick")
				_sub.due = float(_sub.elapsed) + float(_caption_style.delay_ms)
			_sub.index = int(_sub.index) + 1
			if int(_sub.index) >= text.length():
				_sub.lifetime = maxf(float(_sub.lifetime), float(_sub.elapsed) + 2000.0)
				_sub.done = true
	elif float(_sub.lifetime) < float(_sub.elapsed):
		var a := int(_sub.alpha) - int(float(dt) * 0.1 + 1.0)
		if a < 0:
			_sub = {}
		else:
			_sub.alpha = a


func _draw_caption() -> void:
	if _sub.is_empty():
		return
	var size := _caption_draw.size
	var x0 := floorf(size.x * float(_caption_style.position.x) + 0.5)
	var y := floorf(size.y * float(_caption_style.position.y) + 0.5)
	var font: Font = _sub.font
	var px := int(_sub.size)
	var c: Color = _caption_style.color
	c.a8 = int(_sub.alpha)
	var shadow := Color(0, 0, 0, c.a)
	var height := font.get_height(px)
	for i in range(_sub.lines.size()):
		var x := x0 + float(_sub.offsets[i])
		var base := y + font.get_ascent(px)
		_caption_draw.draw_string(font, Vector2(x + 1.0, base + 1.0), String(_sub.lines[i]), HORIZONTAL_ALIGNMENT_LEFT, -1, px, shadow)
		_caption_draw.draw_string(font, Vector2(x, base), String(_sub.lines[i]), HORIZONTAL_ALIGNMENT_LEFT, -1, px, c)
		y += height


## lane CAMPUI-1: DISPLAY_NOTIFICATION_BOX(type, label, seconds) / ..._WITH_OBJECT_TYPE_IMAGE_OVERRIDE(type, label, seconds, template) (RW 0x7C5507): the
## type's notice with the label's text, the template's ButtonImage replacing the icon (RW 0x6D1305: when the template exists), at the location the open
## screens give (RW 0x69BFAE: the objectives screen, else the powers screen, else the main spot; opaque over a screen)
func _show_notification(r: Dictionary) -> void:
	var p: Array = r.params
	if p.size() < 3:
		return
	var req := {"kind": "show", "type": String(p[0].string), "label": String(p[1].string), "seconds": int(p[2].int)}
	if r.action == "DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE" and p.size() > 3:
		var img: Dictionary = world.get_template_button_image(String(p[3].string))
		if img.get("ok", false):
			req["override"] = true
			req["override_image"] = String(img.image)
	var loc: int = int(objectives_open.call()) if objectives_open.is_valid() else 0
	req["location"] = loc
	req["opaque"] = loc != 0
	var out: Dictionary = shell.notification_box(req)
	print("CINE notification %s %s: %s" % [req.type, req.label, "queued" if out.get("ok", false) else str(out.get("error", "?"))])


## lane CAMPUI-1: the box's DoClose (RW 0x8EB2D8; the objectives screen closes it as it opens) and its enabled flag (HideControlBar RW 0x804576 disables,
## ShowControlBar RW 0x8046A1 enables)
func close_notification() -> void:
	if _box_ready:
		shell.notification_box({"kind": "close"})


func set_notification_enabled(on: bool) -> void:
	if _box_ready:
		shell.notification_box({"kind": "enable", "on": on})


## the box's state (tests, the proof captures)
func notification_state() -> Dictionary:
	return shell.notification_box({"kind": "state"}) if _box_ready else {}


func _exit_tree() -> void:
	if _box_ready and shell != null:
		shell.notification_box({"kind": "end"})
		_box_ready = false


## lane CAMPUI-1: the caption's text being shown ("" none), for the reports and checks
func caption_shown() -> String:
	if _sub.is_empty():
		return ""
	return "\n".join(PackedStringArray(_sub.lines))


func _place_camera() -> void:
	var look := Vector3(target.x, target.z, -target.y)
	# the camera behind the look-at point along the SAGE angle (angle 0 looks north, +y SAGE = -z Godot)
	var back := Vector3(-sin(angle), 0.0, cos(angle))
	camera.position = look + back * distance * cos(pitch) + Vector3(0, distance * sin(-pitch), 0)
	camera.look_at(look)


## lane SCRIPT-2: the player's part of MAP ANG Angmar's first objective (the heroes reach Rogash, his attackers fall), by the GameWorld test hooks
func _drive() -> void:
	if drive.begins_with("kill:"):
		_drive_kill()
		return
	if drive == "rhudaur":
		_drive_rhudaur()
		return
	if drive != "angmar":
		return
	var f: int = world.get_frame()
	if _drive_step == 0 and f >= 200:
		_drive_step = 1
		for h in ["Witch King", "Morgomir"]:
			print("DRIVE place %s: %s" % [h, world.debug_script_place(h, "AT - Rogash Becomes Vulnerable")])
	elif _drive_step == 1 and f >= 204:
		_drive_step = 2
		for n in ["Rogash Attacker 1", "Rogash Attacker 2", "Surrender Monkeys"]:
			print("DRIVE kill %s: %s" % [n, world.debug_script_kill(n)])


## lane SCRIPT-3: the player's part of MAP ANG Rhudaur (the steps of test_script3_mission.cpp's playthrough), by the GameWorld test hooks: Hwaldar's
## barricade falls, the Witch King is seen at each village (the warriors hunt, TEAM_HUNT) and they are beaten, then the forts and King Argeleb fall;
## the map's scripts do the rest (the crew's waypoint path, the traitors, the objectives, the victory sequence). The camera looks at the action.
func _follow(what: String) -> void:
	_look = what


func _look_at_now() -> void:
	if _look.is_empty():
		return
	var d: Dictionary = world.debug_script_team(_look) if _look.contains("/") else world.debug_script_unit(_look)
	if d.get("ok", false):
		var goal := Vector3(d.x, d.y, d.z)
		target = target.lerp(goal, 0.08)


func _team_at(team: String) -> Dictionary:
	return world.debug_script_team(team)


func _drive_rhudaur() -> void:
	var f: int = world.get_frame()
	var villages := ["PlyrWildmen/SW Village Warriors", "PlyrWildmen/NW Village Warriors", "PlyrWildmen/NE Village Warriors"]
	match _drive_step:
		0:
			if f >= 172:
				print("DRIVE kill barricade: %s" % world.debug_script_kill("Hwaldar Barricade 1"))
				_follow("Hwaldar's Crew 1")
				_drive_step = 1
				_drive_frame = f
		1, 3, 5:
			# the Witch King is seen at a village: its warriors hunt
			if f >= _drive_frame + 30:
				var v: String = villages[(_drive_step - 1) / 2]
				var at := _team_at(v)
				if at.get("ok", false):
					print("DRIVE Witch King to %s: %s" % [v, world.debug_script_place_at("Witch King", float(at.x) + 120.0, float(at.y))])
				_follow(v)
				_drive_step += 1
				_drive_frame = f
		2, 4, 6:
			# the fight, then the village falls
			if f >= _drive_frame + 45:
				var v: String = villages[(_drive_step - 2) / 2]
				print("DRIVE beat %s: %s" % [v, world.debug_script_kill_team(v)])
				_drive_step += 1
				_drive_frame = f
		7:
			# the SE loyalists run away along their path (Hwaldar is free)
			var at := _team_at("PlyrWildmen/SE Village Warriors")
			if at.get("ok", false):
				print("DRIVE Witch King to SE: %s" % world.debug_script_place_at("Witch King", float(at.x), float(at.y)))
			_follow("PlyrWildmen/SE Village Warriors")
			_drive_step = 8
			_drive_frame = f
		8:
			if f >= _drive_frame + 110:
				_follow("North Fortress")
				_drive_step = 9
				_drive_frame = f
		9:
			if f >= _drive_frame + 20:
				for n in ["North Fortress", "South Fortress", "King Argeleb"]:
					print("DRIVE kill %s: %s" % [n, world.debug_script_kill(n)])
				_drive_step = 10
				_drive_frame = f
		10:
			if f >= _drive_frame + 25:
				_follow("Witch King")
				_drive_step = 11


## lane CAMP-1: --cine-drive=kill:<name>@<frame> (the montage of scripts/campaign_flow.gd's missions): the camera follows the named unit from 150 frames
## before, then the test hook kills it (the mission's decisive moment: a tower, a citadel, the Witch King); the map's scripts play the rest
func _drive_kill() -> void:
	var spec := drive.substr(5)
	var at := int(spec.get_slice("@", 1))
	var name := spec.get_slice("@", 0)
	var f: int = world.get_frame()
	if _drive_step == 0 and f >= at - 150:
		_follow(name)
		_drive_step = 1
	elif _drive_step == 1 and f >= at:
		print("DRIVE kill %s: %s" % [name, world.debug_script_kill(name)])
		_drive_step = 2


func _report_sound_check() -> void:
	if world == null:
		return
	var rep: Dictionary = world.get_script_report()
	print("CINE script audio played %d, last %s" % [int(rep.get("audio_played", -1)), str(rep.get("audio_last", []))])
