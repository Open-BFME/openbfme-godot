## Numeric shader test: renders the real terrain shader and reads pixels back.
##
##   godot --path godot --script res://tests/shader_test.gd        (needs a GPU and a window; NOT --headless)
##
## One 10 x 10 cell is drawn through MapTerrainBuilder.build_test_patch, i.e. the same TerrainComposite data,
## mesh layout and res://shaders/terrain.gdshader a map's terrain uses, in debug mode 7 (the composite of the raw
## layer albedos, converted to linear ONCE). The expectations are the retail arithmetic, written out by hand:
## D3D9 blends in gamma space, so a half-alpha white over black is 0.5 (byte 128), not the 0.73 a blend in
## Godot's linear framebuffer gives; two half layers give 1 - 0.5 * 0.5 = 0.75 (191); a cell's corner alphas
## are interpolated over the layer's own diagonal; a cliff UV that leaves its class block wraps back into it.
## Godot sRGB-encodes the linear output and the shader hands over the exact sRGB decode (lane RENDER-4; it used a 2.2 power
## before, which darkened the dark end: 0.1 read back as 19), so the byte read back is the gamma-space value times 255.
##
## Phase 2 (needs ROTWK_INSTALL and BFME2_INSTALL, else it is reported as skipped): "map wor fangorn" (standing water,
## rivers, a post effect, cliffs) is built through MapTerrainBuilder, rendered for a few frames by the GPU and read
## back; the image must be neither blank nor saturated and must hold many distinct colours (a terrain shader that
## fails to compile or samples garbage renders flat). The shader is exercised for real, not just the scene tree.
##
## Exit codes: 0 pass, 1 fail, 77 skip (no display / no extension).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
const TOL := 3 # bytes

# atlas: 16 x 4 texels, four 4-px blocks: black, white, red, blue
const BLACK_U := 0.125
const WHITE_U := 0.375
const RED_U := 0.625
const BLUE_U := 0.875
const V := 0.5

var _failures := 0
var _phase := 1 # 1 = numeric patch cases, 2 = the map render
var _map_viewport: SubViewport
var _map_ok := false
var _case := -1
var _frame := 0
var _cases: Array = []
var _viewport: SubViewport
var _patch: Node3D
var _builder: RefCounted


func _initialize() -> void:
	if not ClassDB.class_exists("MapTerrainBuilder"):
		print("SKIP: the openbfme extension is not loaded")
		quit(EXIT_SKIP)
		return
	if DisplayServer.get_name() == "headless":
		print("SKIP: this test renders; run it without --headless")
		quit(EXIT_SKIP)
		return
	_builder = ClassDB.instantiate("MapTerrainBuilder")
	_cases = _make_cases()
	print("shader test: %d cases, renderer %s" % [_cases.size(), RenderingServer.get_video_adapter_name()])


func _atlas() -> Image:
	var img := Image.create(16, 4, false, Image.FORMAT_RGBA8)
	for y in 4:
		for x in 16:
			var c := Color.BLACK
			if x >= 12:
				c = Color(0, 0, 1)
			elif x >= 8:
				c = Color(1, 0, 0)
			elif x >= 4:
				c = Color.WHITE
			img.set_pixel(x, y, c)
	return img


func _uv(u: float) -> PackedVector2Array:
	return PackedVector2Array([Vector2(u, V), Vector2(u, V), Vector2(u, V), Vector2(u, V)])


func _alpha(a: float) -> PackedFloat32Array:
	return PackedFloat32Array([a, a, a, a])


func _layer(u: float, alpha: PackedFloat32Array, flip := false, wrap = null) -> Dictionary:
	var d := {"uv": _uv(u), "alpha": alpha, "flip": flip}
	if wrap != null:
		d["wrap"] = wrap
	return d


func _make_cases() -> Array:
	var cases: Array = []
	# name, spec, expected rgb bytes
	cases.append(["half-alpha white over black is 0.5 in retail space", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, _alpha(0.5))]}, Color8(128, 128, 128)])
	cases.append(["quarter-alpha white over black is 0.25", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, _alpha(0.25))]}, Color8(64, 64, 64)])
	# lane RENDER-4: the dark end of the curve, where a 2.2 power instead of the sRGB decode lost a quarter of the value (byte 19)
	cases.append(["tenth-alpha white over black is 0.1 (the hand-over is the exact sRGB decode)", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, _alpha(0.1))]}, Color8(26, 26, 26)])
	cases.append(["full-alpha layer replaces the base", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, _alpha(1.0))]}, Color8(255, 255, 255)])
	cases.append(["two half layers: 1 - 0.5 * 0.5 = 0.75", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, _alpha(0.5)), _layer(WHITE_U, _alpha(0.5))]}, Color8(191, 191, 191)])
	cases.append(["no blend layer: the base layer alone", {"uv0": _uv(RED_U), "layers": []}, Color8(255, 0, 0)])
	# corner alpha (1,0,0,0): the SW-NE diagonal passes through the centre (0.5 * 1 + 0.5 * 0 = 0.5), the SE-NW one does not (0).
	# The pixel read is (16,16) of 32: its centre is at cell-local (16.5/32, 1 - 16.5/32) = (0.5156, 0.4844), just below
	# the diagonal, where the straight layer's alpha is 1 - lx = 0.4844 (byte 123.5), by the triangle (SW,SE,NE) of ZH.
	var corner := PackedFloat32Array([1.0, 0.0, 0.0, 0.0])
	cases.append(["layer straight (SW-NE diagonal): corner alpha 1,0,0,0 is 0.5 at the centre", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, corner, false)]}, Color8(124, 124, 124)])
	cases.append(["layer flipped (SE-NW diagonal): the same corners are 0 at the centre", {"uv0": _uv(BLACK_U), "layers": [_layer(WHITE_U, corner, true)]}, Color8(0, 0, 0)])
	# the mesh is drawn with the BASE diagonal, the layer keeps its own: a flipped layer on a straight base cell
	cases.append(["layer flip is independent of the mesh diagonal (base flipped, layer straight)", {"uv0": _uv(BLACK_U), "flip": true, "layers": [_layer(WHITE_U, corner, false)]}, Color8(124, 124, 124)])
	# wrap: the red block is x 0.5..0.75; u = 0.80 is past its edge and would read the blue block unwrapped
	var red_block := Rect2(0.5, 0.0, 0.25, 1.0)
	cases.append(["cliff UV past the class edge wraps back into the class (red, not the blue neighbour)", {"uv0": _uv(BLACK_U), "layers": [_layer(0.80, _alpha(1.0), false, red_block)]}, Color8(255, 0, 0)])
	cases.append(["the same UV without a wrap rect leaks into the neighbour (blue) - what the wrap prevents", {"uv0": _uv(BLACK_U), "layers": [_layer(0.80, _alpha(1.0))]}, Color8(0, 0, 255)])
	cases.append(["base cliff layer past its class edge wraps (white block 0.25..0.5, u = 0.55 -> 0.30 white)", {"uv0": _uv(0.55), "base_wrap": Rect2(0.25, 0.0, 0.25, 1.0), "layers": []}, Color8(255, 255, 255)])
	return cases


func _process(_delta: float) -> bool:
	if _phase == 2:
		_frame += 1
		if _frame >= 6:
			_check_map()
			print("SHADER TEST %s" % ["PASS" if _failures == 0 else "FAIL"])
			quit(EXIT_PASS if _failures == 0 else EXIT_FAIL)
		return false
	if _case >= _cases.size():
		return false
	if _case < 0 or _frame >= 3:
		if _case >= 0:
			_check_current()
		_case += 1
		_frame = 0
		if _case >= _cases.size():
			if _phase == 1:
				_phase = 2
				_frame = 0
				_start_map()
				return false
			return false
		_start_case()
		return false
	_frame += 1
	return false


func _start_case() -> void:
	if _viewport != null:
		_viewport.queue_free()
	_viewport = SubViewport.new()
	_viewport.size = Vector2i(32, 32)
	_viewport.own_world_3d = true
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_viewport)
	var spec: Dictionary = _cases[_case][1]
	spec = spec.duplicate()
	spec["base_atlas"] = _atlas()
	_patch = _builder.build_test_patch(spec)
	if _patch == null:
		print("  FAIL ", _cases[_case][0], ": build_test_patch returned null")
		_failures += 1
		return
	_viewport.add_child(_patch)
	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 10.0
	cam.near = 0.1
	cam.far = 50.0
	_viewport.add_child(cam)
	cam.position = Vector3(5.0, 10.0, -5.0) # above the cell (G = (x, z, -y)), looking straight down
	cam.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	cam.current = true


func _check_current() -> void:
	if _patch == null:
		return
	var img := _viewport.get_texture().get_image()
	var px := img.get_pixel(16, 16)
	var want: Color = _cases[_case][2]
	var got := px.to_html(false)
	var ok := (absi(roundi(px.r * 255.0) - roundi(want.r * 255.0)) <= TOL
		and absi(roundi(px.g * 255.0) - roundi(want.g * 255.0)) <= TOL
		and absi(roundi(px.b * 255.0) - roundi(want.b * 255.0)) <= TOL)
	print("  %s %s: got (%d,%d,%d) want (%d,%d,%d)" % ["ok  " if ok else "FAIL", _cases[_case][0], roundi(px.r * 255.0), roundi(px.g * 255.0), roundi(px.b * 255.0), roundi(want.r * 255.0), roundi(want.g * 255.0), roundi(want.b * 255.0)])
	if not ok:
		_failures += 1


func _start_map() -> void:
	var rotwk := OS.get_environment("ROTWK_INSTALL")
	var bfme2 := OS.get_environment("BFME2_INSTALL")
	if rotwk.is_empty() or bfme2.is_empty():
		print("  SKIP map render: set ROTWK_INSTALL and BFME2_INSTALL")
		_frame = 99
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("  FAIL map render: mount_retail: ", mount.errors)
		_failures += 1
		_frame = 99
		return
	var node: Node3D = _builder.build_map(fs, "map wor fangorn", {"fog": false})
	var report: Dictionary = _builder.get_report()
	var other: Array = []
	for e in report.errors:
		if not String(e).contains("SkyEnv"):
			other.append(e)
	if node == null or not other.is_empty():
		print("  FAIL map render: build errors ", other)
		_failures += 1
		_frame = 99
		return
	_map_viewport = SubViewport.new()
	_map_viewport.size = Vector2i(640, 360)
	_map_viewport.own_world_3d = true
	_map_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_map_viewport)
	_map_viewport.add_child(node)
	var cam := Camera3D.new()
	cam.far = 20000.0
	_map_viewport.add_child(cam)
	var c: Dictionary = report.camera
	var center: Vector3 = c.center
	var size := maxf(c.size_x, c.size_y)
	cam.position = center + Vector3(0, size * 0.55, size * 0.62)
	cam.look_at(center)
	cam.current = true
	_map_ok = true


func _check_map() -> void:
	if not _map_ok:
		return
	var img := _map_viewport.get_texture().get_image()
	var seen := {}
	var lum := 0.0
	var n := 0
	for y in range(0, img.get_height(), 4):
		for x in range(0, img.get_width(), 4):
			var p := img.get_pixel(x, y)
			seen[(int(p.r * 63.0) << 12) | (int(p.g * 63.0) << 6) | int(p.b * 63.0)] = true
			lum += 0.299 * p.r + 0.587 * p.g + 0.114 * p.b
			n += 1
	lum /= float(n)
	print("  map render: %d distinct 18-bit colours over %d samples, mean luminance %.3f" % [seen.size(), n, lum])
	_check_ok(seen.size() > 400, "the rendered map has many distinct colours (%d)" % seen.size())
	_check_ok(lum > 0.05 and lum < 0.9, "the rendered map is neither black nor blown out (mean luminance %.3f)" % lum)


func _check_ok(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1
