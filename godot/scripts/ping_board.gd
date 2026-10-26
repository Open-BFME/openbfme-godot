## Ping wheel (NOT RETAIL, an OpenBFME addition): the pings a player places with the wheel (scripts/ping_wheel.gd), shown where they point.
## A ping is a kind and a point on the map (SAGE x, y). Each one shows, for PING_SECONDS:
##   * on the ground: the player's beacon model (the BeaconName of the player's PlayerTemplate, RotWK's MultiplayerBeacon: GameWorld
##     get_player_beacon_template / get_template_model, read from the player's own game files, so a mod's beacon shows), a ring in the kind's colour that
##     pulses out from it and the kind's icon floating above it;
##   * on the minimap: a radar event (InGameHudNode.create_radar_event, the radar's own 4 second event): RADAR_EVENT_BEACON (6, the Palantir's PingBeacon
##     ping) for every kind but "danger", which is RADAR_EVENT_UNDER_ATTACK (3, PingAttack, the red one). Like a script's radar event it is also the
##     radar's event position (Radar::noteEvaPosition), so Space (the CommandMap's VIEW_LAST_RADAR_EVENT) looks at the last ping;
##   * a sound: MiscAudio BeaconPlacedSound (RotWK: "Beacon was placed by ally"), or for "danger" the radar's own GenericRadarEvent, which the radar asks
##     for when it first draws a non-beacon event (Radar::drawEvents).
## A refused ping (too many too fast: BURST in BURST_WINDOW_MS) plays MiscAudio NoCanDoSound.
##
## Presentation only. Nothing here reaches the simulation: no game message, no logic command, no random numbers, no logic state. The radar, the camera and
## the audio are client state (GameClient/Radar.h: "Everything here is client state"); the beacon model is drawn by its own W3DInstancer, not as an object.
##
## Multiplayer seam. request_ping() is the local player's ping: it checks the rate limit and then calls receive_ping() for the local player. A network game
## would replace that last call with a message to every ally ({player, kind, x, y}) and call receive_ping() when one arrives (with the same per-sender rate
## limit, allowed()); receive_ping() is the only entry the presentation needs. This change leaves network games alone: game.gd does not install the wheel in
## them, so nobody sees a ping the other players cannot.
extends Node3D

## the wheel's kinds, by index (the wheel's sectors name these). "radar": the radar event type; "sound": the MiscAudio field played ("" for none)
const KINDS := [
	{"id": "here", "label": "Look here", "color": Color(1.0, 0.85, 0.35), "radar": 6, "sound": "BeaconPlacedSound"},
	{"id": "attack", "label": "Attack here", "color": Color(1.0, 0.5, 0.18), "radar": 6, "sound": "BeaconPlacedSound"},
	{"id": "reinforce", "label": "Reinforcements", "color": Color(0.45, 0.85, 0.35), "radar": 6, "sound": "BeaconPlacedSound"},
	{"id": "danger", "label": "Danger, fall back", "color": Color(1.0, 0.22, 0.2), "radar": 3, "sound": ""},
	{"id": "assist", "label": "Need help", "color": Color(0.4, 0.7, 1.0), "radar": 6, "sound": "BeaconPlacedSound"},
]
const PING_SECONDS := 4.0     # the radar event's life (Radar::createEvent's default, what a script's radar event gets)
const FADE_SECONDS := 0.5     # the radar fades its event the last half second (RW 0x6D93EB); the marker does the same
const BURST := 3              # at most BURST pings ...
const BURST_WINDOW_MS := 5000 # ... in this many milliseconds, per player
const ICON_PIXELS := 128

var hud: Node    # InGameHudNode (the radar), or null (tests)
var world: Node  # GameWorld (ground heights, the beacon template), or null
var audio: Node  # GameAudio (play_misc), or null
var local_player := -1
var beacon_model := ""
var errors: Array = []
var shown_total := 0          # pings shown (tests, reports)
var refused_total := 0
var last := {}                # the last ping shown: {player, kind, x, y} (tests)

var _inst: Node3D             # W3DInstancer of the beacon model
var _model := -1
var _icons: Array = []        # per kind: the SubViewport its icon is drawn in (its texture is the floating icon's)
var _recent := {}             # player -> [ticks_msec of their pings in the window]
var _markers: Array = []      # {node, ring, icon, beacon (instance id or -1), age, kind}
var _pending: Array = []      # [kind, world_xy] waiting for a render frame the logic is held in (the ground height is a live read, as in move_hints.gd)


## hud / world / audio / fs may be null (the UI test); `player` is the local player's index (the beacon template's player)
func setup(p_hud: Node, p_world: Node, p_audio: Node, fs: RefCounted, player: int) -> Dictionary:
	hud = p_hud
	world = p_world
	audio = p_audio
	local_player = player
	name = "Pings"
	_make_icons()
	if world != null and fs != null:
		var template: String = world.get_player_beacon_template(player)
		beacon_model = world.get_template_model(template) if template != "" else ""
		if beacon_model == "":
			errors.append("the local player's PlayerTemplate names no beacon with a model (BeaconName '%s'): pings show without the beacon" % template)
		else:
			_inst = ClassDB.instantiate("W3DInstancer")
			add_child(_inst)
			var r: Dictionary = _inst.setup(fs)
			if not r.get("ok", false):
				errors.append("beacon instancer: %s" % [r.get("errors", [])])
			else:
				_model = _inst.add_model(beacon_model)
				if _model < 0:
					errors.append("beacon model %s: %s" % [beacon_model, _inst.get_model_report(_model)])
	return {"ok": errors.is_empty(), "errors": errors, "beacon": beacon_model}


static func kind_index(id: String) -> int:
	for i in KINDS.size():
		if KINDS[i].id == id:
			return i
	return -1


## would `player` be allowed another ping now (the rate limit; drops the window's old entries)
func allowed(player: int, now_ms: int = -1) -> bool:
	var now := Time.get_ticks_msec() if now_ms < 0 else now_ms
	var times: Array = _recent.get(player, [])
	times = times.filter(func(t): return now - int(t) < BURST_WINDOW_MS)
	_recent[player] = times
	return times.size() < BURST


## the local player's ping (the wheel's release). False when refused: an unknown kind, or the rate limit (NoCanDoSound)
func request_ping(kind: int, world_xy: Vector2, now_ms: int = -1) -> bool:
	if kind < 0 or kind >= KINDS.size():
		return false
	var now := Time.get_ticks_msec() if now_ms < 0 else now_ms
	if not allowed(local_player, now):
		refused_total += 1
		_play("NoCanDoSound")
		print("GAME ping refused: %d pings in %d s at most" % [BURST, BURST_WINDOW_MS / 1000])
		return false
	_recent[local_player].append(now)
	receive_ping(local_player, kind, world_xy) # the multiplayer seam: a network game sends the ping to its allies here
	return true


## show a ping of `player` (the local player's, or later an ally's from the network)
func receive_ping(player: int, kind: int, world_xy: Vector2) -> void:
	if kind < 0 or kind >= KINDS.size():
		return
	var k: Dictionary = KINDS[kind]
	shown_total += 1
	last = {"player": player, "kind": kind, "x": world_xy.x, "y": world_xy.y}
	print("GAME ping %s at (%.0f, %.0f) by player %d" % [k.id, world_xy.x, world_xy.y, player])
	if world == null or world.is_logic_held():
		_add_marker(kind, world_xy)
	else:
		_pending.append([kind, world_xy])
	if hud != null:
		hud.create_radar_event(world_xy, int(k.radar), true) # and the radar's event position: Space looks at it, as at a script's radar event
	_play(str(k.sound))


func marker_count() -> int:
	return _markers.size() + _pending.size()


func _play(field: String) -> void:
	if audio != null and field != "":
		audio.play_misc(field)


func _process(delta: float) -> void:
	var held: bool = world == null or world.is_logic_held() # the ground height is a live read (move_hints.gd does the same)
	if held and not _pending.is_empty():
		for p in _pending:
			_add_marker(p[0], p[1])
		_pending.clear()
	for i in range(_markers.size() - 1, -1, -1):
		var m: Dictionary = _markers[i]
		m.age += delta
		if m.age >= PING_SECONDS:
			_remove_marker(m)
			_markers.remove_at(i)
			continue
		var color: Color = KINDS[m.kind].color
		var fade := clampf((PING_SECONDS - m.age) / FADE_SECONDS, 0.0, 1.0)
		# the ring: three pulses out from the beacon, each 0.8 s, then it rests at full size
		var pulse := fmod(m.age, 0.8) / 0.8 if m.age < 2.4 else 1.0
		var ring: MeshInstance3D = m.ring
		ring.scale = Vector3.ONE * lerpf(0.35, 1.0, pulse)
		(ring.material_override as StandardMaterial3D).albedo_color = Color(color, (1.0 - 0.6 * pulse) * fade)
		var icon: Sprite3D = m.icon
		icon.position.y = 34.0 + 3.0 * sin(m.age * 4.0)
		icon.modulate = Color(1, 1, 1, fade)
		if m.beacon >= 0:
			_inst.set_instance_opacity(m.beacon, fade)


func _add_marker(kind: int, xy: Vector2) -> void:
	var z: float = world.get_ground_height(xy.x, xy.y) if world != null else 0.0
	var node := Node3D.new()
	node.position = Vector3(xy.x, z, -xy.y) # SAGE (x, y, z up) -> Godot (x, z, -y)
	add_child(node)
	var ring := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 20.0
	torus.outer_radius = 24.0
	torus.rings = 48
	ring.mesh = torus
	ring.scale = Vector3.ONE * 0.35
	ring.position.y = 1.0
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true
	mat.render_priority = 1
	mat.albedo_color = KINDS[kind].color
	ring.material_override = mat
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.add_child(ring)
	var icon := Sprite3D.new()
	icon.texture = _icons[kind].get_texture()
	icon.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	icon.no_depth_test = true
	icon.render_priority = 2
	icon.pixel_size = 0.2
	icon.position.y = 34.0
	icon.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.add_child(icon)
	var beacon := -1
	if _model >= 0:
		beacon = _inst.add_instance(_model, Transform3D(Basis.IDENTITY, node.position))
	_markers.append({"node": node, "ring": ring, "icon": icon, "beacon": beacon, "age": 0.0, "kind": kind})


func _remove_marker(m: Dictionary) -> void:
	if m.beacon >= 0 and _inst != null:
		_inst.remove_instance(m.beacon)
	(m.node as Node3D).queue_free()


## each kind's icon drawn once into its own small viewport (the floating icon's texture)
func _make_icons() -> void:
	const Icon := preload("res://scripts/ping_icon.gd")
	for i in KINDS.size():
		var vp := SubViewport.new()
		vp.size = Vector2i(ICON_PIXELS, ICON_PIXELS)
		vp.transparent_bg = true
		vp.render_target_update_mode = SubViewport.UPDATE_ONCE
		var c: Control = Icon.new()
		c.kind = KINDS[i].id
		c.color = KINDS[i].color
		c.backdrop = true
		c.size = Vector2(ICON_PIXELS, ICON_PIXELS)
		vp.add_child(c)
		add_child(vp)
		_icons.append(vp)
