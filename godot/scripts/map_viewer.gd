## Map viewer: mounts pure RotWK 2.01, builds one retail map's terrain (heightmap in three blend layers,
## standing water, rivers, roads) and its objects (trees, props, floors, buildings, units, hordes with their retail
## models and idle animations, through the W3D instancer) through the openbfme extension, and shows it with a
## free camera. The object markers of the first map lane stay available (--markers).
##
## Free camera: hold the right mouse button and move the mouse to look, WASD to fly (Q/E down/up),
## Shift fast, Ctrl slow, mouse wheel changes the speed. Key 1/2/3/4/5 toggle terrain / water+rivers /
## roads / markers / objects, M cycles the cliff-UV mode (rebuilds), F1 hides the overlay.
##
## Command line (after `--`):
##   --map=<name>            map directory/file stem, e.g. "map mp evendim" (default)
##   --screenshot=<path>     save the viewport after the warm-up frames and quit
##   --cam=overview|low|top|horde|structure|unit|tinted  camera preset for the screenshot (default overview; the last three look at the
##                           first horde member / structure / unit of the object layer; `tinted`: a unit of a side the map colours)
##   --measure=<frames>      after warm-up, record this many frames and print FRAMETIME stats, then quit
##   --opt=key=value         terrain builder options (repeatable): cliff_uv=atlas, vertex_color=all, resolve_objects=true ...
##   --objects=on|off        the object layer (default on); --markers draws the coloured markers too
##   --objopt=key=value      object builder options (repeatable): animations=false, max_objects=500 ...
##   --live                  run the map as LIVE objects (GameWorld: retail's scheduler ticks the objects, hordes create their members,
##                           drawables follow them) instead of the static object layer; --advance=<seconds> runs that much game time first
##                           (in 0.1 s steps) before the screenshot; --slots=1:FactionMen,2:FactionMordor gives the map's Player_N those
##                           factions (lobby slots; the first is human; teams 0, 1, 0, 1 ...)
##   --produce=<Building>:<Unit>,<Unit>...  (with --live; lane PROD-1) creates the building for player index --player=<n> (default 2: the first
##                           lobby slot after the neutral and civilian players, see the player list the viewer prints) at --at=x,y (SAGE
##                           coordinates, default the map centre), sets a rally point --rally=<dx>,<dy> away (default 0,-120) and queues the
##                           units by player command; --advance then needs to be long enough for them to be built (they are advanced in 0.1 s steps)
##                           (the produced units and hordes WALK to the rally point: lane MOVE-1's AI runs production's commands)
##   Orders (lane MOVE-1; with --live, through the lockstep command path: a select and a move message, executed by the logic's next frame):
##     left click an object (units and hordes with an AI) to select it, Shift adds; left click the ground to order the selection there, Shift+click
##     appends a waypoint; X stops the selection; the overlay shows the first selected object's movement state and model conditions
##   --order=<Template>:<dx>,<dy>[,<type>[,<degrees>]]   order the first object of that template to its own position + (dx, dy) in SAGE units
##                           (type move|force|attack|formation|waypoint; degrees: the final facing of a formation move); --order-at=<seconds>
##                           of game time (default 0) is when the order is given
##   --spawn=<Template>:<player index>:<x>,<y>[,<degrees>]   (repeatable; GameWorld.create_object, the map-less stand-in for a placed object) create an object for
##                           that player (index: see the player list the viewer prints) before anything else; --order and --follow then mean the spawned object of that template
##   --follow=<Template>     the camera follows the first object of that template (offset --follow-dist=<units>, default 160)
##   --pause                 after --advance, stop the game clock (a deterministic screenshot of a movement phase)
##   --econ=<seconds>        (with --live) print every player's money and command points (ECON line) every that many seconds of game time
##                           (default 0 = off); the HUD label shows them in any live run
##   --report                print the builder reports as JSON
##   --pathfind              the pathfinder debug view (lane PATH-1): overlays the classified pathfind cells (water blue, deep water dark blue,
##                           cliff red, obstacle yellow, rubble orange, impassable green, pinched clear cells cyan) and, with --pf-from / --pf-to,
##                           the computed path (the optimised polyline in white, the cell chain in orange); key 6 toggles it
##   --pf-auto               pick the two endpoints (far apart, zone-connected, the longest search)
##   --pf-from=x,y --pf-to=x,y   SAGE world positions (x east, y north) of the path request; --pf-opt=key=value: find_path options
##                           (water=true, cliff=true); --pf-planes: also show the ImpassabilityToPlayers / ExtraPass planes
##   --cam=pathfind          (with --pathfind) a top view framing the path
##   --no-audio              (lane SCRIPT-2) a --campaign run without the retail audio
##   --cine-drive=angmar     (lane SCRIPT-2) MAP ANG Angmar: after the intro the heroes are placed at Rogash and his attackers killed (test hooks), so
##                           the "Rogash joins the Witch King" cinematic plays; =rhudaur (lane SCRIPT-3) MAP ANG Rhudaur to its victory: the barricade, the
##                           villages (the warriors hunt), the forts and King Argeleb by test hooks, the rest by the map's scripts
##   --campaign              (lane SCRIPT-1; implies --live) the map as a campaign mission: its own sides, the map script engine runs its scripts, and the
##                           script requests (camera moves, letterbox, captions, fades) drive the view (scripts/campaign_cine.gd); --cine-start=<waypoint>
##                           names the waypoint the camera starts at (default InitialCameraPosition)
extends Node3D

const WARMUP_FRAMES := 24

var _map_name := "map mp evendim"
var _screenshot_path := ""
var _cam_preset := "overview"
var _measure_frames := 0
var _print_report := false
var _options := {}
var _objects_on := true
var _markers_on := false
var _obj_options := {}
var _objects_builder: RefCounted
var _live := false
var _advance_seconds := 0.0
var _slots_arg := ""
var _produce_arg := ""
var _produce_player := 2
var _produce_at := Vector2.INF
var _produce_rally := Vector2(0, -120)
var _order_arg := ""
var _spawn_args: Array = []
var _spawned := {}
var _order_at := 0.0
var _follow_template := ""
var _follow_id := 0
var _follow_dist := 160.0
var _pause_after_advance := false
var _selected: Array = []
var _live_label: Label
var _live_marker: MeshInstance3D
var _econ_every := 0.0       # --econ=<seconds>: print the ECON line that often (game time)
var _econ_last_frame := -1
var _label_base := ""        # the label text without the live economy lines
var _game_world: Node3D
var _focus := {}
var _world_env: WorldEnvironment
var _sun: DirectionalLight3D

var _builder: RefCounted
var _fs: RefCounted
var _root: Node3D
var _camera: Camera3D
var _label: Label
var _yaw := 0.0
var _pitch := -0.9
var _speed := 400.0
var _look := false
var _cam_pos := Vector3.INF   # --campos=x,y,z in Godot space (overrides the preset)
var _cam_look := Vector3.INF  # --camlook=x,y,z
var _campaign := false        # --campaign (lane SCRIPT-1)
var _campaign_audio := true   # --no-audio turns the campaign's retail audio off (lane SCRIPT-2)
var _cine_drive := ""         # --cine-drive=angmar: the player's part of MAP ANG Angmar's first objective done by test hooks (lane SCRIPT-2)
var _audio: Node
var _cine_start := "InitialCameraPosition"
var _cine: Node
var _pf_on := false           # --pathfind
var _pf_planes := false
var _pf_auto := false         # --pf-auto: pick two far apart connected clear cells
var _pf_from := Vector2.INF   # --pf-from=x,y (SAGE)
var _pf_to := Vector2.INF
var _pf_options := {}
var _pf_view: RefCounted
var _pf_focus := Vector3.INF  # centre of the computed path (Godot space)
var _pf_extent := 0.0         # size of the path's bounding box (Godot units)
var _frames := 0
var _frame_times: Array[float] = []
var _viewport_rid: RID


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--screenshot="):
			_screenshot_path = arg.substr(13)
		elif arg.begins_with("--cam="):
			_cam_preset = arg.substr(6)
		elif arg.begins_with("--measure="):
			_measure_frames = int(arg.substr(10))
		elif arg.begins_with("--campos="):
			var p := arg.substr(9).split(",")
			_cam_pos = Vector3(float(p[0]), float(p[1]), float(p[2]))
		elif arg.begins_with("--camlook="):
			var p := arg.substr(10).split(",")
			_cam_look = Vector3(float(p[0]), float(p[1]), float(p[2]))
		elif arg.begins_with("--opt="):
			var kv := arg.substr(6).split("=")
			if kv.size() == 2:
				var v: Variant = kv[1]
				if v == "true":
					v = true
				elif v == "false":
					v = false
				elif String(v).is_valid_int():
					v = int(v)
				_options[kv[0]] = v
		elif arg == "--live":
			_live = true
		elif arg.begins_with("--cine-drive="):
			_cine_drive = arg.substr(13)
		elif arg == "--no-audio":
			_campaign_audio = false
		elif arg == "--campaign":
			_live = true
			_campaign = true
		elif arg.begins_with("--cine-start="):
			_cine_start = arg.substr(13)
		elif arg.begins_with("--advance="):
			_advance_seconds = float(arg.substr(10))
		elif arg.begins_with("--produce="):
			_produce_arg = arg.substr(10)
		elif arg == "--at=auto":
			_auto_spot = true
		elif arg.begins_with("--player="):
			_produce_player = int(arg.substr(9))
		elif arg.begins_with("--at=") and arg != "--at=auto":
			var p := arg.substr(5).split(",")
			_produce_at = Vector2(float(p[0]), float(p[1]))
		elif arg.begins_with("--rally="):
			var p := arg.substr(8).split(",")
			_produce_rally = Vector2(float(p[0]), float(p[1]))
		elif arg.begins_with("--econ="):
			_econ_every = float(arg.substr(7))
		elif arg.begins_with("--slots="):
			_slots_arg = arg.substr(8)
		elif arg.begins_with("--spawn="):
			_spawn_args.append(arg.substr(8))
		elif arg.begins_with("--order="):
			_order_arg = arg.substr(8)
		elif arg.begins_with("--order-at="):
			_order_at = float(arg.substr(11))
		elif arg.begins_with("--follow="):
			_follow_template = arg.substr(9)
		elif arg.begins_with("--follow-dist="):
			_follow_dist = float(arg.substr(14))
		elif arg == "--pause":
			_pause_after_advance = true
		elif arg == "--report":
			_print_report = true
		elif arg == "--pathfind":
			_pf_on = true
		elif arg == "--pf-auto":
			_pf_auto = true
		elif arg == "--pf-planes":
			_pf_planes = true
		elif arg.begins_with("--pf-from="):
			var p := arg.substr(10).split(",")
			_pf_from = Vector2(float(p[0]), float(p[1]))
		elif arg.begins_with("--pf-to="):
			var p := arg.substr(8).split(",")
			_pf_to = Vector2(float(p[0]), float(p[1]))
		elif arg.begins_with("--pf-opt="):
			var kv := arg.substr(9).split("=")
			if kv.size() == 2:
				_pf_options[kv[0]] = (kv[1] == "true")
		elif arg.begins_with("--objects="):
			_objects_on = arg.substr(10) != "off"
		elif arg == "--markers":
			_markers_on = true
		elif arg.begins_with("--objopt="):
			var kv := arg.substr(9).split("=")
			if kv.size() == 2:
				var v: Variant = kv[1]
				if v == "true":
					v = true
				elif v == "false":
					v = false
				elif String(v).is_valid_int():
					v = int(v)
				_obj_options[kv[0]] = v

	if not _options.has("markers"):
		_options["markers"] = _markers_on or not _objects_on  # the object layer replaces the markers
	if _cam_preset != "low" and not _options.has("fog"):
		_options["fog"] = false  # an overview sees far beyond the map.ini fog range; the retail camera does not
	_build_stage()
	if not ClassDB.class_exists("MapTerrainBuilder"):
		_fail("openbfme extension is not loaded (or too old); run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_builder = ClassDB.instantiate("MapTerrainBuilder")
	_load_map()
	if _measure_frames > 0:
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		_viewport_rid = get_viewport().get_viewport_rid()
		RenderingServer.viewport_set_measure_render_time(_viewport_rid, true)


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world := WorldEnvironment.new()
	world.environment = env
	_world_env = world
	add_child(world)

	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)

	var ui := CanvasLayer.new()
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	ui.add_child(_label)
	_live_label = Label.new()
	_live_label.position = Vector2(12, 90)
	_live_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_live_label.add_theme_constant_override("outline_size", 4)
	ui.add_child(_live_label)
	add_child(ui)


func _fail(message: String) -> void:
	push_error(message)
	print("MAPVIEW FAIL: ", message)
	_label.text = "ERROR: " + message
	_label.modulate = Color(1, 0.4, 0.4)
	if not _screenshot_path.is_empty() or _measure_frames > 0:
		get_tree().quit(1)


func _load_map() -> void:
	if _root != null:
		_root.queue_free()
		_root = null
	var t0 := Time.get_ticks_msec()
	_root = _builder.build_map(_fs, _map_name, _options)
	var report: Dictionary = _builder.get_report()
	var wall := Time.get_ticks_msec() - t0
	if _root == null:
		_fail("build_map failed: %s" % [report.errors])
		return
	add_child(_root)
	var info: Dictionary = report.map_info
	var mesh: Dictionary = report.get("mesh", {})
	print("MAPVIEW built '%s' in %d ms: %dx%d border %d, %d chunks, %d vertices, %d triangles (wall %d ms)" % [
		_map_name, report.timings_ms.total, info.width, info.height, info.border,
		mesh.get("chunks", 0), mesh.get("vertices", 0), mesh.get("triangles", 0), wall])
	print("MAPVIEW timings: ", JSON.stringify(report.timings_ms))
	print("MAPVIEW errors: %d" % report.errors.size())
	for e in report.errors:
		print("  error: ", e)
	# reported, never silent: install contamination (S-039) and the post effects (lane RENDER-4: LookupTablePostEffect applied, S-1650; any other is an error)
	for f in report.get("loose_files", []):
		print("  CONTAMINATION: ", f.line)
	for p in report.get("post_effects", []):
		print("  post effect '%s' (lookup %s, blend %.3f) %s" % [p.name, p.lookup_image, p.blend_factor, "applied (S-1650)" if p.get("applied", false) else "NOT applied: " + str(p.get("error", ""))])
	if _print_report:
		print("MAPVIEW REPORT ", JSON.stringify(report))
	_update_label(report)
	if _objects_on and _live:
		_build_live()
	elif _objects_on:
		_build_objects(report)
	if _pf_on:
		_build_pathfind()
	_set_camera_preset(report)


## The pathfinder debug view: the classified cells as a translucent vertex-coloured mesh draped on the terrain and the computed path.
func _build_pathfind() -> void:
	if not ClassDB.class_exists("PathfindView"):
		_fail("openbfme extension has no PathfindView; run the build")
		return
	_pf_view = ClassDB.instantiate("PathfindView")
	var t0 := Time.get_ticks_msec()
	var rep: Dictionary = _pf_view.build(_fs, _map_name, {"objects": _objects_on})
	print("PATHFIND built '%s' in %d ms (wall): ok=%s %dx%d cells, structures %d, fences %d" % [
		_map_name, Time.get_ticks_msec() - t0, rep.get("ok", false), rep.get("width", 0), rep.get("height", 0), rep.get("structures", 0), rep.get("fences", 0)])
	print("PATHFIND cells: ", JSON.stringify(rep.get("cells", {})))
	print("PATHFIND timings: ", JSON.stringify(rep.get("timings_ms", {})))
	for e in rep.get("errors", []):
		print("  pathfind error: ", e)
	for st in rep.get("stops", []):
		print("  pathfind stop: ", String(st).substr(0, 160))
	if not rep.get("ok", false):
		_fail("pathfind build failed")
		return
	var node := Node3D.new()
	node.name = "Pathfind"
	var ov: Dictionary = _pf_view.get_overlay({"pinched": true, "planes": _pf_planes, "lift": 1.5})
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = ov.vertices
	arrays[Mesh.ARRAY_COLOR] = ov.colors
	var mesh := ArrayMesh.new()
	if ov.vertices.size() > 0:
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	node.add_child(mi)
	print("PATHFIND overlay: %d coloured cells, %d vertices" % [ov.count, ov.vertices.size()])
	if _pf_auto:
		var ep: Dictionary = _pf_view.pick_endpoints({})
		if ep.get("ok", false):
			_pf_from = ep.from
			_pf_to = ep.to
			print("PATHFIND auto endpoints %s -> %s" % [_pf_from, _pf_to])
		else:
			print("PATHFIND auto endpoints: none found")
	if _pf_from != Vector2.INF and _pf_to != Vector2.INF:
		var res: Dictionary = _pf_view.find_path(_pf_from, _pf_to, _pf_options)
		print("PATHFIND path %s -> %s: found=%s length=%.1f, %d optimised nodes, %d cell nodes, %.3f ms, cells %d" % [
			_pf_from, _pf_to, res.found, res.get("length", 0.0), res.get("optimized", PackedVector3Array()).size(),
			res.get("cells", PackedVector3Array()).size(), res.seconds * 1000.0, res.get("cells_examined", 0)])
		if res.found:
			var lo := Vector3(INF, INF, INF)
			var hi := Vector3(-INF, -INF, -INF)
			for v in res.optimized:
				lo = lo.min(v)
				hi = hi.max(v)
			_pf_focus = (lo + hi) * 0.5
			_pf_extent = maxf(hi.x - lo.x, hi.z - lo.z)
			# ribbons scale with the framing so a path across a whole map stays visible
			var wscale := maxf(_pf_extent / 1000.0, 1.0)
			node.add_child(_path_mesh(res.cells, Color(1.0, 0.55, 0.1), 3.0 * wscale))
			node.add_child(_path_mesh(res.optimized, Color(1, 1, 1), 6.0 * wscale))
	_root.add_child(node)


## A ribbon (flat strip of the given width in the XZ plane) along a polyline, as one vertex-coloured mesh.
func _path_mesh(points: PackedVector3Array, color: Color, width: float) -> MeshInstance3D:
	var verts := PackedVector3Array()
	var cols := PackedColorArray()
	for i in range(points.size() - 1):
		var a := points[i]
		var b := points[i + 1]
		var d := Vector3(b.x - a.x, 0, b.z - a.z)
		if d.length() < 0.001:
			continue
		var n := Vector3(-d.z, 0, d.x).normalized() * width * 0.5
		verts.append_array([a - n, a + n, b + n, a - n, b + n, b - n])
		for k in range(6):
			cols.append(color)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_COLOR] = cols
	var mesh := ArrayMesh.new()
	if verts.size() > 0:
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.no_depth_test = true
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	return mi


func _build_objects(_terrain_report: Dictionary) -> void:
	if not ClassDB.class_exists("MapObjectBuilder"):
		_fail("openbfme extension has no MapObjectBuilder; run the build")
		return
	if _objects_builder == null:
		_objects_builder = ClassDB.instantiate("MapObjectBuilder")
		var t_setup := Time.get_ticks_msec()
		var setup: Dictionary = _objects_builder.setup(_fs)
		print("MAPOBJ object world: ok=%s, %d templates, load %.1f s (wall %d ms)" % [setup.ok, setup.get("templates", 0), setup.get("seconds", 0.0), Time.get_ticks_msec() - t_setup])
		if not setup.ok:
			for e in setup.errors:
				print("  object world error: ", e)
			_fail("object world failed to load")
			return
	var t0 := Time.get_ticks_msec()
	var objects: Node3D = _objects_builder.build_objects(_fs, _map_name, _obj_options)
	var rep: Dictionary = _objects_builder.get_report()
	print("MAPOBJ built objects of '%s' in %d ms (wall)" % [_map_name, Time.get_ticks_msec() - t0])
	print("MAPOBJ timings: ", JSON.stringify(rep.timings_ms))
	if objects == null:
		_fail("build_objects failed: %s" % [rep.errors])
		return
	_root.add_child(objects)
	_apply_object_lighting(rep)
	_focus = rep.get("focus", {})
	var o: Dictionary = rep.objects
	var r: Dictionary = rep.runtime
	var i: Dictionary = rep.instancing
	print("MAPOBJ %d objects: drawables %d, models %d; placed %d (model draws %d: animated %d, static %d; trees %d, props %d, floors %d), %d distinct models" % [
		o.total, o.drawables, o.drawn_models, r.placed_models, r.model_draws, r.animated, r.static_models, r.tree_draws, r.prop_draws, r.floor_draws, r.distinct_models])
	print("MAPOBJ instances: static %d, animated %d, missing models %d, pose failures %d, hidden-sub-object instances %d" % [
		i.static_instances, i.animated_instances, i.missing_models, i.pose_failures, i.instances_with_hidden_sub_objects])
	print("MAPOBJ fates: ", JSON.stringify(o.by_fate))
	print("MAPOBJ not drawn: ", JSON.stringify(o.not_drawn))
	print("MAPOBJ errors: %d" % rep.errors.size())
	for e in rep.errors.slice(0, 30):
		print("  object error: ", e)
	for w in rep.instancer_warnings:
		print("  object warning: ", w)
	for st in rep.stops:
		print("  object stop: ", String(st).substr(0, 140))
	if _print_report:
		print("MAPOBJ REPORT ", JSON.stringify(rep))


## The live game: GameWorld owns the logic, the drawables and the two instancers; the viewer only feeds it time.
func _build_live() -> void:
	if not ClassDB.class_exists("GameWorld"):
		_fail("openbfme extension has no GameWorld; run the build")
		return
	if _game_world != null:
		_game_world.queue_free()
		_game_world = null
	_game_world = ClassDB.instantiate("GameWorld")
	var t_setup := Time.get_ticks_msec()
	var setup: Dictionary = _game_world.setup(_fs)
	print("LIVE object world: ok=%s, %d templates, %d playable factions, load %.1f s (wall %d ms)" % [setup.ok, setup.get("templates", 0), setup.get("factions", 0), setup.get("seconds", 0.0), Time.get_ticks_msec() - t_setup])
	if not setup.ok:
		for e in setup.errors:
			print("  object world error: ", e)
		_fail("object world failed to load")
		return
	var opts := _obj_options.duplicate()
	if not _slots_arg.is_empty():
		var slots: Array = []
		var n := 0
		for part in _slots_arg.split(","):
			var kv := part.split(":")
			if kv.size() == 2:
				slots.append({"player": "Player_" + kv[0], "faction": kv[1], "human": n == 0, "team": n % 2, "start_index": n})
				n += 1
		opts["slots"] = slots
	if _campaign:
		opts["campaign"] = true
		if not _cine_drive.is_empty():
			opts["test_hooks"] = true
	var t0 := Time.get_ticks_msec()
	var rep: Dictionary = _game_world.load_map(_map_name, opts)
	print("LIVE built '%s' in %d ms (wall); timings %s" % [_map_name, Time.get_ticks_msec() - t0, JSON.stringify(rep.timings_ms)])
	if not rep.ok:
		for e in rep.errors.slice(0, 30):
			print("  live error: ", e)
		_fail("load_map failed")
		return
	_root.add_child(_game_world)
	_focus = rep.get("focus", {})
	_apply_object_lighting(rep)
	var o: Dictionary = rep.objects
	var l: Dictionary = rep.logic
	var d: Dictionary = rep.drawables
	print("LIVE %d objects (%d from the map list, %d hordes made %d members), %d update modules + %d sleeping, drawables %d (model draws %d, animated %d, shown %d)" % [
		l.objects, o.created, o.hordes, o.contained, l.update_modules, l.sleeping_modules, d.live, d.model_draws, d.animated_model_draws, d.models_shown])
	for p in rep.players:
		print("  player ", p)
	print("LIVE unported module classes: %d; helper shells: %s" % [l.unported_modules.size(), JSON.stringify(l.helper_shells)])
	print("LIVE client-only layer: ", JSON.stringify(rep.client_only))
	print("LIVE errors: %d" % rep.errors.size())
	for e in rep.errors.slice(0, 30):
		print("  live error: ", e)
	for st in rep.stops:
		print("  live stop: ", String(st).substr(0, 150))
	if _print_report:
		print("LIVE REPORT ", JSON.stringify(rep))
	if _campaign:
		var sr: Dictionary = _game_world.get_script_report()
		print("SCRIPTS loaded=%s scripts=%d groups=%d" % [sr.loaded, sr.get("scripts", 0), sr.get("groups", 0)])
		for line in sr.get("setup", []):
			print("  script setup: ", line)
		var wp: Dictionary = _game_world.get_waypoint(_cine_start)
		var start := Vector3(wp.x, wp.y, wp.z) if wp.ok else Vector3(_focus.get("x", 0.0), _focus.get("y", 0.0), 0.0)
		_label.visible = false # the cinematic shows the scene, not the viewer's overlays
		_live_label.visible = false
		if _campaign_audio:
			_boot_campaign_audio()
		_cine = load("res://scripts/campaign_cine.gd").new()
		add_child(_cine)
		_cine.setup(_game_world, _camera, start, self, _audio)
		_cine.drive = _cine_drive
	for sp: String in _spawn_args:
		var f: PackedStringArray = sp.split(":")
		var xy: PackedStringArray = f[2].split(",")
		var sid: int = _game_world.create_object(f[0], int(f[1]), float(xy[0]), float(xy[1]), deg_to_rad(float(xy[2])) if xy.size() > 2 else 0.0)
		print("SPAWN %s for player %s at (%s, %s): object %d" % [f[0], f[1], xy[0], xy[1], sid])
		if sid > 0:
			_spawned[f[0]] = sid
	if not _produce_arg.is_empty():
		_start_production(rep)
	var steps := int(_advance_seconds / 0.1)
	var t1 := Time.get_ticks_msec()
	var order_done := _order_arg.is_empty()
	if not _follow_template.is_empty():
		_follow_id = _find_template(_follow_template)
		print("LIVE follow '%s': object %d" % [_follow_template, _follow_id])
	for k in maxi(steps, 1 if not order_done else 0):
		if not order_done and k * 0.1 >= _order_at:
			order_done = true
			_order_from_arg()
		if k < steps:
			_game_world.advance(0.1)
			_update_economy_overlay()
	if not order_done:
		_order_from_arg()
	if _pause_after_advance:
		_game_world.set_paused(true)
	if not _produce_arg.is_empty():
		_report_production()
	if steps > 0:
		print("LIVE advanced %.1f s in %d ms: frame %d, hash %d, objects %d" % [_advance_seconds, Time.get_ticks_msec() - t1, _game_world.get_frame(), _game_world.get_state_hash(), _game_world.get_object_count()])


## lane SCRIPT-2: the retail audio manager for a campaign mission (the scripts' sounds, speech and music reach it through GameWorld.attach_audio)
func _boot_campaign_audio() -> void:
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class; run build.bat")
		return
	_audio = ClassDB.instantiate("GameAudio")
	_audio.name = "GameAudio"
	add_child(_audio)
	var b: Dictionary = _audio.boot(_fs, {"seed": 4711})
	if not b.get("ok", false):
		_fail("GameAudio boot failed: %s" % [b.get("errors", [])])
		return
	var at: Dictionary = _game_world.attach_audio()
	print("AUDIO boot: %d events, attach %s" % [b.get("events", 0), at])
	if not at.get("ok", false):
		_fail("attach_audio failed: %s" % [at])


var _producer := -1
var _auto_spot := false


## a flat spot (ground height within 1.5 over a 160-unit square) with no object within 300 and ground above the water line of the spot's neighbours: scanned on a grid
func _find_open_spot() -> Vector2:
	var pts: Array = []
	for id in _game_world.get_object_ids():
		var o: Dictionary = _game_world.get_object(id)
		pts.append(Vector2(o.x, o.y))
	var best := Vector2.INF
	var best_score := 1e30
	var y := 600.0
	while y < 5000.0:
		var x := 600.0
		while x < 5000.0:
			var h: float = _game_world.get_ground_height(x, y)
			var flat := true
			for dx in [-80.0, 0.0, 80.0]:
				for dy in [-80.0, 0.0, 80.0]:
					if absf(_game_world.get_ground_height(x + dx, y + dy) - h) > 1.5:
						flat = false
			if flat and h > 60.0:
				var d := 1e30
				for p in pts:
					d = minf(d, p.distance_to(Vector2(x, y)))
				if d > 300.0 and -d < best_score:
					best_score = -d
					best = Vector2(x, y)
			x += 100.0
		y += 100.0
	print("PRODUCE open spot ", best, " clearance ", -best_score)
	return best
var _produced_templates: Array = []


## PROD-1: the building and the queue commands (all through the player command path, GameWorld.queue_unit / set_rally_point).
func _start_production(rep: Dictionary) -> void:
	var parts := _produce_arg.split(":")
	if parts.size() != 2:
		print("PRODUCE bad --produce, expected Building:Unit,Unit")
		return
	var at := _produce_at
	if at == Vector2.INF and _auto_spot:
		at = _find_open_spot()
	if at == Vector2.INF:
		var cam: Dictionary = rep.get("camera", {})
		at = Vector2(0, 0)
		if cam.has("center"):
			var c: Vector3 = cam.center
			at = Vector2(c.x, -c.z)
	_producer = _game_world.create_object(parts[0], _produce_player, at.x, at.y, 0.0)
	print("PRODUCE building %s for player %d at (%.0f, %.0f) -> object %d, ground %s" % [parts[0], _produce_player, at.x, at.y, _producer, _game_world.get_object(_producer).get("z")])
	if _producer < 0:
		print("PRODUCE errors ", _game_world.get_production(_produce_player, 1).get("errors", []))
		return
	var bo: Dictionary = _game_world.get_object(_producer)
	_focus["structure"] = Vector3(at.x, bo.get("z", 0.0), -at.y)
	_game_world.set_rally_point(_produce_player, _producer, at.x + _produce_rally.x, at.y + _produce_rally.y)
	for u in parts[1].split(","):
		_game_world.queue_unit(_produce_player, _producer, u, false)
		_produced_templates.append(u)
	_game_world.advance(0.25) # one logic frame: the commands run


func _report_production() -> void:
	if _producer < 0:
		return
	print("PRODUCE stats ", JSON.stringify(_game_world.get_stats()))
	var pr: Dictionary = _game_world.get_production(_produce_player, _producer)
	print("PRODUCE money %s queue %s" % [pr.get("money"), JSON.stringify(pr.get("queue"))])
	print("PRODUCE errors %s dispatch %s unhandled %s" % [JSON.stringify(pr.get("errors")), JSON.stringify(pr.get("dispatch_errors")), JSON.stringify(pr.get("unhandled"))])
	var b: Dictionary = _game_world.get_object(_producer)
	var seen := 0
	for id in _game_world.get_object_ids():
		var o: Dictionary = _game_world.get_object(id)
		if o.get("owner") == b.get("owner"):
			print("PRODUCE owned: %d %s at (%.0f, %.0f)" % [id, o.template, o.x, o.y])
			seen += 1
			if seen <= 4:
				print("PRODUCE   ", o.template, " obj (%.0f, %.0f, %.0f) drawable at %s models %s instances %s posed %s" % [o.x, o.y, o.z, o.get("drawable_position"), o.get("models"), o.get("instances"), o.get("posed_entries")])
	print("PRODUCE %d objects owned by the player; building at (%.0f, %.0f)" % [seen, b.get("x", 0.0), b.get("y", 0.0)])
func _find_template(name: String) -> int:
	if _spawned.has(name):
		return _spawned[name]
	return _game_world.find_object_by_template(name)


## --order=<Template>:<dx>,<dy>[,<type>[,<degrees>]]
func _order_from_arg() -> void:
	var parts := _order_arg.split(":")
	if parts.size() != 2:
		print("ORDER bad --order argument: ", _order_arg)
		return
	var id: int = _find_template(parts[0])
	if id <= 0:
		print("ORDER no object of template '%s'" % parts[0])
		return
	var o: Dictionary = _game_world.get_object(id)
	var f := parts[1].split(",")
	var opts := {}
	if f.size() > 2:
		opts["type"] = f[2]
	if f.size() > 3:
		opts["angle"] = deg_to_rad(float(f[3]))
	var r: Dictionary = _game_world.order_move([id], o.x + float(f[0]), o.y + float(f[1]), opts)
	print("ORDER %s #%d from (%.0f, %.0f) by (%s, %s): %s" % [parts[0], id, o.x, o.y, f[0], f[1], JSON.stringify(r)])


## Left click: select the unit or horde nearest to the ray, else order the selection to the ground point under the cursor.
func _on_live_click(pos: Vector2, shift: bool) -> void:
	var from := _camera.project_ray_origin(pos)
	var dir := _camera.project_ray_normal(pos)
	var best := 0
	var best_d := INF
	for id in _game_world.get_object_ids():
		var o: Dictionary = _game_world.get_object(id)
		if not o.get("ok", false) or not o.get("has_ai", false) or o.contained_by != 0 or o.structure:
			continue
		var p := Vector3(o.x, o.z + 8.0, -o.y)
		var v := p - from
		var t := v.dot(dir)
		if t <= 0.0:
			continue
		var d := (v - dir * t).length()
		if d < 15.0 + 0.02 * t and d < best_d:
			best = id
			best_d = d
	if best != 0:
		if not shift:
			_selected.clear()
		if not _selected.has(best):
			_selected.append(best)
		print("SELECT ", _selected)
		return
	if _selected.is_empty():
		return
	var first: Dictionary = _game_world.get_object(_selected[0])
	var plane := Plane(Vector3.UP, first.z)
	var hit: Variant = plane.intersects_ray(from, dir)
	if hit == null:
		return
	var target: Vector3 = hit
	var r: Dictionary = _game_world.order_move(_selected, target.x, -target.z, {"type": "waypoint" if shift else "move"})
	print("ORDER click -> (%.0f, %.0f): %s" % [target.x, -target.z, JSON.stringify(r)])
	if _live_marker == null:
		_live_marker = MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 6.0
		sphere.height = 12.0
		_live_marker.mesh = sphere
		var m := StandardMaterial3D.new()
		m.albedo_color = Color(1, 0.2, 0.1)
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		_live_marker.material_override = m
		add_child(_live_marker)
	_live_marker.position = target + Vector3(0, 6, 0)


func _live_overlay() -> void:
	if _live_label == null or _game_world == null:
		return
	if _selected.is_empty():
		_live_label.text = ""
		return
	var o: Dictionary = _game_world.get_object(_selected[0])
	if not o.get("ok", false):
		_live_label.text = "selected object gone"
		return
	_live_label.text = "#%d %s  at (%.0f, %.0f) angle %.0f  %s speed %.1f/frame state %d\nmodel conditions: %s\nframe %d  hash %d  commands %s" % [
		_selected[0], o.template, o.x, o.y, rad_to_deg(o.angle), "MOVING" if o.get("moving", false) else ("idle" if o.get("idle", false) else "busy"),
		o.get("speed", 0.0), o.get("ai_state", -1), ", ".join(o.get("conditions", [])), _game_world.get_frame(), _game_world.get_state_hash(),
		JSON.stringify(_game_world.get_stats().get("commands", {}))]


## The models are lit by the map's object lighting at its time of day (GlobalLighting objects[0]): a DirectionalLight3D from the
## diffuse colour and the light direction, the ambient colour in the environment (the terrain shader is unshaded and ignores both).
func _apply_object_lighting(rep: Dictionary) -> void:
	if not rep.has("lighting"):
		print("MAPOBJ no GlobalLighting chunk: object lighting is the viewer default")
		_world_env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		_world_env.environment.ambient_light_color = Color(0.6, 0.6, 0.6)
		return
	var lit: Dictionary = rep.lighting
	_world_env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	_world_env.environment.ambient_light_color = lit.ambient
	_world_env.environment.ambient_light_energy = 1.0
	if _sun != null:
		_sun.queue_free()
	_sun = DirectionalLight3D.new()
	_sun.name = "ObjectSun"
	_sun.light_color = lit.diffuse
	_sun.light_energy = 1.0
	_sun.shadow_enabled = false
	var dir: Vector3 = lit.direction
	if dir.length() > 0.0001:
		_sun.transform = Transform3D(Basis.looking_at(dir.normalized(), Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.RIGHT), Vector3.ZERO)
	add_child(_sun)
	print("MAPOBJ object lighting: tod %d ambient %s diffuse %s direction %s" % [lit.time_of_day, lit.ambient, lit.diffuse, lit.direction])


func _set_camera_preset(report: Dictionary) -> void:
	var cam: Dictionary = report.camera
	var center: Vector3 = cam.center
	var size := maxf(cam.size_x, cam.size_y)
	match _cam_preset:
		"low":
			_camera.position = center + Vector3(0, size * 0.10, size * 0.30)
			_camera.look_at(center + Vector3(0, 0, -size * 0.05))
		"top":
			_camera.position = center + Vector3(0, size * 1.0, 0.001)
			_camera.look_at(center, Vector3.FORWARD)
		_:
			_camera.position = center + Vector3(0, size * 0.55, size * 0.62)
			_camera.look_at(center)
	if (_objects_builder != null or _game_world != null) and (_cam_preset == "horde" or _cam_preset == "structure" or _cam_preset == "unit" or _cam_preset == "tinted"):
		var focus: Dictionary = _focus
		if focus.has(_cam_preset):
			var f: Vector3 = focus[_cam_preset]
			var dist := 140.0 if _cam_preset == "structure" else 70.0
			_camera.position = f + Vector3(dist * 0.4, dist * 0.7, dist)
			_camera.look_at(f + Vector3(0, 8, 0))
		else:
			print("MAPOBJ no '%s' focus point on this map" % _cam_preset)
	if _cam_preset == "pathfind" and _pf_focus != Vector3.INF:
		var h := maxf(_pf_extent * 1.4, 400.0)
		_camera.position = _pf_focus + Vector3(0, h, 0.001)
		_camera.look_at(_pf_focus, Vector3.FORWARD)
	if _cam_pos != Vector3.INF:
		_camera.position = _cam_pos
		_camera.look_at(_cam_look if _cam_look != Vector3.INF else center)
	var rot := _camera.rotation
	_pitch = rot.x
	_yaw = rot.y
	_speed = size * 0.25


func _update_label(report: Dictionary) -> void:
	var m: Dictionary = report.get("mesh", {})
	var a: Dictionary = report.get("atlas", {})
	_label.text = "%s   cells %d  blend %d  extra %d  cliff-UV %d  atlas %dx%d  classes %d (flat-normal %d)\ncliff UV: %s   vertex colour: %s   stops: %s" % [
		_map_name, m.get("cells", 0), m.get("blend_cells", 0), m.get("extra_blend_cells", 0), m.get("cliff_uv_cells", 0),
		a.get("width", 0), a.get("height", 0), a.get("classes", 0), a.get("missing_normal_map", []).size(),
		m.get("cliff_uv_unit", "?"), m.get("vertex_color_mode", "?"), ",".join(report.stops)]
	var notes: PackedStringArray = []
	for f in report.get("loose_files", []):
		notes.append("S-039 contamination: loose %s %s (%d bytes)" % [f.install, f.path, f.size])
	for p in report.get("post_effects", []):
		notes.append("S-031: post effect %s not applied" % p.name)
	if not notes.is_empty():
		_label.text += "\n" + "   |   ".join(notes)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT and event.pressed and _game_world != null:
			_on_live_click(event.position, event.shift_pressed)
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			_look = event.pressed
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if _look else Input.MOUSE_MODE_VISIBLE
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_speed *= 1.2
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_speed /= 1.2
	elif event is InputEventMouseMotion and _look:
		_yaw -= event.relative.x * 0.003
		_pitch = clampf(_pitch - event.relative.y * 0.003, -1.55, 1.55)
	elif event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_1: _toggle("Terrain")
			KEY_2:
				_toggle("StandingWater")
				_toggle("Rivers")
			KEY_3: _toggle("Roads")
			KEY_4: _toggle("ObjectMarkers")
			KEY_5: _toggle("MapObjects")
			KEY_6: _toggle("Pathfind")
			KEY_F1: _label.visible = not _label.visible
			KEY_X:
				if _game_world != null and not _selected.is_empty():
					print("ORDER stop: ", JSON.stringify(_game_world.order_stop(_selected)))
			KEY_M:
				_options["cliff_uv"] = "atlas" if _options.get("cliff_uv", "hypothesis") == "hypothesis" else "hypothesis"
				_load_map()


func _toggle(node_name: String) -> void:
	if _root != null and _root.has_node(node_name):
		var n: Node3D = _root.get_node(node_name)
		n.visible = not n.visible


func _process(delta: float) -> void:
	var move := Vector3.ZERO
	if Input.is_key_pressed(KEY_W): move.z -= 1
	if Input.is_key_pressed(KEY_S): move.z += 1
	if Input.is_key_pressed(KEY_A): move.x -= 1
	if Input.is_key_pressed(KEY_D): move.x += 1
	if Input.is_key_pressed(KEY_E): move.y += 1
	if Input.is_key_pressed(KEY_Q): move.y -= 1
	var factor := 3.0 if Input.is_key_pressed(KEY_SHIFT) else (0.2 if Input.is_key_pressed(KEY_CTRL) else 1.0)
	_camera.rotation = Vector3(_pitch, _yaw, 0)
	if move != Vector3.ZERO:
		_camera.position += _camera.global_transform.basis * move.normalized() * _speed * factor * delta

	if _root == null:
		return
	if _objects_builder != null:
		_objects_builder.advance(delta)
	if _game_world != null:
		_game_world.advance(delta)
		_live_overlay()
		if _follow_id <= 0 and not _follow_template.is_empty() and _frames % 30 == 0:
			_follow_id = _find_template(_follow_template) # an object that does not exist yet at load (a produced horde)
			if _follow_id > 0:
				_selected = [_follow_id]
		if _follow_id > 0:
			var fo: Dictionary = _game_world.get_object(_follow_id)
			if fo.get("ok", false):
				var fp := Vector3(fo.x, fo.z + 8.0, -fo.y)
				_camera.position = fp + Vector3(_follow_dist * 0.4, _follow_dist * 0.8, _follow_dist)
				_camera.look_at(fp)
				_pitch = _camera.rotation.x
				_yaw = _camera.rotation.y
		_update_economy_overlay()
	_frames += 1
	if _measure_frames > 0:
		if _frames > WARMUP_FRAMES:
			_frame_times.append(delta * 1000.0)
			if _frame_times.size() >= _measure_frames:
				_report_frame_times()
				get_tree().quit(0)
	elif not _screenshot_path.is_empty() and _frames == WARMUP_FRAMES:
		var image := get_viewport().get_texture().get_image()
		DirAccess.make_dir_recursive_absolute(_screenshot_path.get_base_dir())
		var err := image.save_png(_screenshot_path)
		print("MAPVIEW screenshot %s -> %s (%dx%d)" % [_screenshot_path, error_string(err), image.get_width(), image.get_height()])
		get_tree().quit(0 if err == OK else 1)


## Lane ECON-1: each player's money and command points, in the HUD label for every logic frame and as an ECON line every --econ seconds of game time
## (5 logic frames a second).
func _update_economy_overlay() -> void:
	var frame: int = _game_world.get_frame()
	if frame == _econ_last_frame:
		return
	_econ_last_frame = frame
	var eco: Dictionary = _game_world.get_economy()
	if eco.is_empty():
		return
	var lines: PackedStringArray = []
	for p in eco.players:
		if not p.playable and p.earned == 0 and p.spent == 0 and p.cp_used == 0:
			continue # the map's placeholder sides that nothing has happened to
		lines.append("%s %s  $%d (+%d / -%d)  CP %d/%d (free %d)" % [p.name, p.faction.trim_prefix("Faction"), p.money, p.earned, p.spent, p.cp_used, p.cp_limit, p.cp_available])
	if _label != null:
		if _label_base.is_empty() or not _label.text.begins_with(_label_base):
			_label_base = _label.text
		_label.text = _label_base + "\nframe %d (%.1f s)  economy:\n" % [frame, frame / 5.0] + "\n".join(lines)
	if _econ_every > 0.0 and frame % maxi(1, int(_econ_every * 5.0)) == 0:
		print("ECON frame %d (%.1f s): %s" % [frame, frame / 5.0, " | ".join(lines)])


func _report_frame_times() -> void:
	var sorted := _frame_times.duplicate()
	sorted.sort()
	var total := 0.0
	for t in sorted:
		total += t
	var n := sorted.size()
	var cpu := RenderingServer.viewport_get_measured_render_time_cpu(_viewport_rid)
	var gpu := RenderingServer.viewport_get_measured_render_time_gpu(_viewport_rid)
	print("FRAMETIME map='%s' frames=%d mean=%.2f ms median=%.2f ms p95=%.2f ms max=%.2f ms (%.1f fps) render_cpu=%.2f ms render_gpu=%.2f ms" % [
		_map_name, n, total / n, sorted[n / 2], sorted[int(n * 0.95)], sorted[n - 1], 1000.0 / (total / n), cpu, gpu])
	if _game_world != null:
		var gs: Dictionary = _game_world.get_stats()
		print("FRAMETIME live: frame %d, %d objects, %d animated drawables, advance %.2f ms (logic %.2f ms, sync %.2f ms), dropped logic frames %d" % [
			gs.frame, gs.objects, gs.animated_drawables, gs.last_advance_ms, gs.last_logic_ms, gs.last_sync_ms, gs.dropped_frames])
	if _objects_builder != null:
		var st: Dictionary = _objects_builder.get_stats()
		var anim: Dictionary = st.get("animated", {})
		print("FRAMETIME objects: animated drawables %d, draw-module advance %.2f ms (CPU, per frame), animated instancer pose %.2f ms + upload %.2f ms, static instancer instances %d" % [
			st.animated_drawables, st.last_advance_ms, anim.get("pose_ms", 0.0), anim.get("upload_ms", 0.0), st.get("static", {}).get("instances", 0)])
	print("FRAMETIME draw_calls=%d objects=%d primitives=%d adapter='%s' resolution=%s" % [
		Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME),
		Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		RenderingServer.get_video_adapter_name(), get_viewport().get_visible_rect().size])
