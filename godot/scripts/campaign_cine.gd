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
var control_bar_hook := Callable() # lane CAMP-2: HideControlBar / ShowControlBar (bool hidden) for CAMERA_LETTERBOX_BEGIN / END and HIDE_UI / SHOW_UI (game.gd)


func setup(w: Node3D, cam: Camera3D, start: Vector3, ui_parent: Node, audio: Node = null) -> void:
	_audio = audio
	world = w
	camera = cam
	target = start
	var ui := CanvasLayer.new()
	ui.layer = 5
	_top = ColorRect.new()
	_top.color = Color.BLACK
	_bottom = ColorRect.new()
	_bottom.color = Color.BLACK
	_fade = ColorRect.new()
	_fade.color = Color(0, 0, 0, 0)
	_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_caption = Label.new()
	_caption.set_meta("game_text", true) # lane CAMP-2: SHOW_MILITARY_CAPTION's game text (game.gd _dev_text_on_screen)
	_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_caption.add_theme_font_size_override("font_size", 22)
	_caption.add_theme_color_override("font_color", Color(0.95, 0.9, 0.75))
	_caption.add_theme_color_override("font_outline_color", Color.BLACK)
	_caption.add_theme_constant_override("outline_size", 6)
	_status = Label.new()
	_status.add_theme_color_override("font_outline_color", Color.BLACK)
	_status.add_theme_constant_override("outline_size", 4)
	ui.add_child(_fade)
	ui.add_child(_top)
	ui.add_child(_bottom)
	ui.add_child(_caption)
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
	_place_camera()


func _process(delta: float) -> void:
	if world == null:
		return
	_drive()
	for r in world.take_script_requests():
		var ps: Array = []
		for p in r.params:
			ps.append(p.string if not String(p.string).is_empty() else (str(p.real) if p.type == 1 else str(p.int)))
		print("SCRIPTREQ frame %d %s(%s) [%s]" % [r.frame, r.action, ", ".join(ps), r.script])
		if r.action == "PLAY_MOVIE_IN_GAME" and movie_hook.is_valid() and r.params.size() > 0:
			movie_hook.call(String(r.params[0].string))
		# lane CAMP-2: ScriptActions::executeAction (RW 0x7CAFA5): cases 118 / 119 doLetterBoxMode (RW 0x7BC8B7: HideControlBar(true) or ShowControlBar(false),
		# then the display's letterbox), cases 347 / 348 HIDE_UI / SHOW_UI (HideControlBar(true) / ShowControlBar(false)); in the requests' order
		if control_bar_hook.is_valid():
			if r.action == "CAMERA_LETTERBOX_BEGIN" or r.action == "HIDE_UI":
				control_bar_hook.call(true)
			elif r.action == "CAMERA_LETTERBOX_END" or r.action == "SHOW_UI":
				control_bar_hook.call(false)
	var st: Dictionary = world.update_script_view(delta * 1000.0, {"x": target.x, "y": target.y, "z": target.z, "angle": angle})
	if st.has_target and (_look.is_empty() or st.moving): # a drive's follow (lane SCRIPT-3) holds the camera between the script's moves
		target = Vector3(st.x, st.y, world.get_ground_height(st.x, st.y))
	if st.has_angle:
		angle = st.angle
	if not st.moving:
		_look_at_now() # lane SCRIPT-3: a drive's follow between the script's own camera moves
	# ZOOM_CAMERA / PITCH_CAMERA: factors of the default framing (S-1182)
	distance = _base_distance * float(st.get("zoom", 1.0))
	pitch = _base_pitch * float(st.get("pitch", 1.0))
	if not hud_mode or st.letterbox or st.moving or st.input_disabled:
		_place_camera()
	if _audio != null:
		_audio.set_listener(Vector3(target.x, target.y, target.z), Vector3(sin(angle), cos(angle), 0.0))
	var size := get_viewport().get_visible_rect().size
	var bar := size.y * 0.12 if st.letterbox else 0.0
	_top.position = Vector2.ZERO
	_top.size = Vector2(size.x, bar)
	_bottom.position = Vector2(0, size.y - bar)
	_bottom.size = Vector2(size.x, bar)
	_top.visible = st.letterbox
	_bottom.visible = st.letterbox
	_fade.position = Vector2.ZERO
	_fade.size = size
	_fade.color = Color(0, 0, 0, st.fade)
	_caption.text = st.caption_text
	_caption.position = Vector2(size.x * 0.1, size.y - bar - 120.0)
	_caption.size = Vector2(size.x * 0.8, 100.0)
	_status.visible = dev_overlay # lane CAMP-2: developer text never shows in the real game (the owner saw "frame 90   input disabled")
	_objectives.visible = dev_overlay
	_status.text = "frame %d   %s%s" % [world.get_frame(), "camera moving   " if st.moving else "", "input disabled" if st.input_disabled else ""] if dev_overlay else ""
	_status.position = Vector2(12, bar + 8.0)
	_note.text = st.get("notification_text", "")
	_note.position = Vector2(size.x * 0.15, bar + 40.0)
	_note.size = Vector2(size.x * 0.7, 60.0)
	var lines: PackedStringArray = []
	for o in st.get("objectives", []):
		lines.append(("[x] " if o.completed else "[ ] ") + "objective %d" % o.index)
	_objectives.text = "\n".join(lines) if dev_overlay else ""
	_objectives.position = Vector2(size.x - 220.0, bar + 40.0)


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
