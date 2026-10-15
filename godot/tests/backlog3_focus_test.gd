## Headless test of the HUD's keys at a focus loss (lane BACKLOG3, S-454, review r1): a scroll / rotate / zoom key held when the window loses the focus stops
## moving the camera (HudInput::resetKeys, the LookAt tick RW 0x83BAF8), a key press that waited for the logic worker (the deferred input queue) is dropped
## at the focus loss and never replayed, a queued release is kept, and a fresh press afterwards works again. Both focus notifications are covered
## (NOTIFICATION_WM_WINDOW_FOCUS_OUT, NOTIFICATION_APPLICATION_FOCUS_OUT). The events go through the viewport (the node's _input), as the window's do.
##
##   godot --headless --path godot --script res://tests/backlog3_focus_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const MAP := "map mp fall back 4p"

var _failures := 0
var _hud: Node
var _world: Node3D


func _initialize() -> void:
	var code: int = await _run()
	print("BACKLOG3FOCUS %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _wait(seconds: float) -> void:
	await create_timer(seconds).timeout


func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = pressed
	root.push_input(e)


func _keys() -> Dictionary:
	return _hud.get_state().get("keys", {})


## the camera value a key moves: the scroll's position, the rotation's angle, the zoom's height
func _metric(kind: String) -> Variant:
	var c: Dictionary = _hud.get_camera()
	match kind:
		"scroll":
			return c.position
		"rotate":
			return c.angle
		_:
			return c.height_above_ground


## the camera at rest (its zoom eases after the last key): the value once two samples 0.3 s apart agree
func _settled(kind: String) -> Variant:
	var last: Variant = _metric(kind)
	for i in 20:
		await _wait(0.3)
		var now: Variant = _metric(kind)
		if now == last:
			return now
		last = now
	return null


func _case(what: int, what_name: String, kind: String, code: Key) -> void:
	var tag := "%s, %s" % [what_name, kind]
	var rest: Variant = await _settled(kind)
	_check(rest != null, "%s: the camera rests before the case" % tag)
	# control: a held key moves the camera, and goes on moving
	var step := 0.2 if kind == "zoom" else 0.4 # the zoom has height limits: short holds stay inside them
	_key(code, true)
	await _wait(step)
	var m1: Variant = _metric(kind)
	await _wait(step)
	var m2: Variant = _metric(kind)
	_check(m1 != rest and m2 != m1, "%s: the held key moves the camera (%s -> %s -> %s)" % [tag, rest, m1, m2])
	# the focus goes while the key is held: no release arrives, the camera stops
	var resets: int = _keys().focus_resets
	_hud.notification(what)
	_check(_keys().focus_resets == resets + 1, "%s: the HUD saw the focus loss" % tag)
	var stopped: Variant = await _settled(kind)
	_check(stopped != null, "%s: the camera stops after the focus loss with the key held" % tag)
	await _wait(0.6)
	_check(_metric(kind) == stopped, "%s: and stays where it stopped (%s)" % [tag, stopped])
	# a press that waits for the logic worker when the focus goes is dropped: replayed, it would hold the key with no release to follow
	var before: Dictionary = _keys()
	_hud.hold_input(true)
	_key(code, true)
	var queued: Dictionary = _keys()
	_check(queued.pending == before.pending + 1 and queued.deferred == before.deferred + 1, "%s: the press waits in the queue (%d pending)" % [tag, queued.pending])
	_hud.notification(what)
	var after: Dictionary = _keys()
	_check(after.pending == before.pending and after.dropped == before.dropped + 1, "%s: the focus loss drops the waiting press (%d pending, %d dropped)" % [tag, after.pending, after.dropped])
	_hud.hold_input(false)
	await _wait(0.8)
	_check(_metric(kind) == stopped, "%s: the dropped press never moves the camera (%s)" % [tag, _metric(kind)])
	# a waiting release is kept (it only ends what the HUD holds) and replayed
	_hud.hold_input(true)
	_key(code, false)
	_hud.notification(what)
	var kept: Dictionary = _keys()
	_check(kept.pending == after.pending + 1 and kept.dropped == after.dropped, "%s: a waiting release stays in the queue" % tag)
	_hud.hold_input(false)
	await _wait(0.4)
	_check(_keys().pending == 0, "%s: and is replayed" % tag)
	# a fresh press afterwards works, and its release stops it
	_key(code, true)
	await _wait(step)
	var again: Variant = _metric(kind)
	_check(again != stopped, "%s: a fresh press moves the camera again (%s -> %s)" % [tag, stopped, again])
	_key(code, false)
	var end: Variant = await _settled(kind)
	_check(end != null, "%s: its release stops the camera" % tag)


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("  mount failed ", mount.errors)
		return 1
	_world = ClassDB.instantiate("GameWorld")
	root.add_child(_world)
	var setup: Dictionary = _world.setup(fs)
	if not setup.ok:
		print("  setup failed ", setup.errors)
		return 1
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(MAP, {"slots": slots, "seed": 4711})
	if not rep.ok:
		print("  load_map failed ", rep.errors.slice(0, 5))
		return 1
	_world.set_logic_thread(false)
	# the middle of the map's objects: room to scroll both ways
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
		hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	var c := (lo + hi) * 0.5
	var camera := Camera3D.new()
	root.add_child(camera)
	var layer := CanvasLayer.new()
	root.add_child(layer)
	_hud = ClassDB.instantiate("InGameHudNode")
	layer.add_child(_hud)
	await process_frame
	var hs: Dictionary = _hud.setup(fs, _world, camera, "Player_1", {"camera_start": Vector3(c.x, 0.0, -c.y), "edge_scroll": false})
	if not hs.ok:
		print("  HUD setup failed ", hs.errors)
		return 1
	await _wait(0.5)
	var keys: Dictionary = _keys()
	_check(keys.has("pending") and keys.has("dropped") and keys.has("focus_resets"), "the HUD reports its input queue: %s" % [keys])
	if _failures != 0:
		return 1
	# scroll, rotate and zoom under each notification (the zoom goes in, then out, to stay inside the height limits)
	await _case(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT, "window focus out", "scroll", KEY_RIGHT)
	await _case(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT, "window focus out", "rotate", KEY_KP_6)
	await _case(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT, "window focus out", "zoom", KEY_KP_8)
	await _case(Node.NOTIFICATION_APPLICATION_FOCUS_OUT, "application focus out", "scroll", KEY_LEFT)
	await _case(Node.NOTIFICATION_APPLICATION_FOCUS_OUT, "application focus out", "rotate", KEY_KP_4)
	await _case(Node.NOTIFICATION_APPLICATION_FOCUS_OUT, "application focus out", "zoom", KEY_KP_2)
	return 0 if _failures == 0 else 1
