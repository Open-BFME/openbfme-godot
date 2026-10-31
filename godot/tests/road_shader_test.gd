## Numeric test of godot/shaders/road.gdshader, the port of terrain.fxo PSDefault_Array element 2 (ps_2_0 blob 37 @0xc160, RenderingMode 2). Needs a GPU
## and a window (NOT --headless):
##
##   godot --path godot --script res://tests/road_shader_test.gd        (exit 0 pass, 1 fail, 77 skip: headless)
##
## A horizontal road quad (SAGE normal (0, 0, 1), like retail's road vertices) is rendered from straight above by the real shader and its centre pixel
## read back. The expectation is written out from the disassembly, not from the shader source:
##   N = normalize(2 * nm.rgb - 1)  (z is the blue texel, NOT rebuilt from xy as the terrain's element 0 does)
##   diff = max(N.L, 0); spec = (N.L > 0 && N.H > 0) ? (N.H)^1200 * nm.a * 0.9 : 0   (the specular mask is the normal map's ALPHA)
##   lit = (vertex colour + 0.5 * lightning) * albedo + sun * (diff * albedo + spec), sun = overbright ? 0.5 * sun : sun, lit *= 2 when overbright
##   rgb = lerp(fog, lit * cloud * macro, fogF) * shroud;  alpha = base.a * vertex.a
## The sun is straight up (L = (0, 0, 1)) and the camera straight above the quad centre, so the half vector is (0, 0, 1) as well at that pixel.
## The shader hands over the exact sRGB decode (output_gamma 2.2) so the byte read back is retail's gamma-space value times 255.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
const TOL := 3.0 # bytes # tolerance: 8-bit readback of float shader maths (as shader_test.gd)
const CAMERA_HEIGHT := 100.0
const HALF := 4.0

var _failures := 0
var _vp: SubViewport
var _quad: MeshInstance3D


func _initialize() -> void:
	var code: int = await _run()
	print("ROAD SHADER %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _frames(n: int) -> void:
	for i in n:
		await process_frame
	await RenderingServer.frame_post_draw


func _image(rgba: Color, size := 2) -> ImageTexture:
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	img.fill(rgba)
	return ImageTexture.create_from_image(img)


func _setup() -> void:
	_vp = SubViewport.new()
	_vp.size = Vector2i(64, 64)
	_vp.own_world_3d = true
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 2.0 * HALF
	cam.position = Vector3(0, CAMERA_HEIGHT, 0)
	cam.rotation_degrees = Vector3(-90, 0, 0)
	_vp.add_child(cam)
	cam.current = true
	_quad = MeshInstance3D.new()
	_vp.add_child(_quad)


## The road ribbon as MapTerrainBuilder makes it: Godot frame (x, z, -y), normal = SAGE (0, 0, 1) = (0, 1, 0), vertex colour with alpha.
func _mesh(vertex_colour: Color) -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-HALF, 0, HALF), Vector3(HALF, 0, HALF), Vector3(HALF, 0, -HALF), Vector3(-HALF, 0, -HALF)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP])
	arrays[Mesh.ARRAY_COLOR] = PackedColorArray([vertex_colour, vertex_colour, vertex_colour, vertex_colour])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


## Renders one case and returns the centre pixel in bytes. c: albedo (rgba), normal (rgba bytes), vcol, sun, lightning, overbright, fog, cloud, macro, shroud.
func _render(c: Dictionary) -> Color:
	_quad.mesh = _mesh(c.vcol)
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/road.gdshader")
	mat.set_shader_parameter("road_tex", _image(c.albedo))
	mat.set_shader_parameter("road_normal_tex", _image(c.normal))
	mat.set_shader_parameter("sun_dir_sage", Vector3(0, 0, 1))
	mat.set_shader_parameter("sun_color", c.sun)
	mat.set_shader_parameter("lightning_color", c.lightning)
	mat.set_shader_parameter("overbright", c.overbright)
	mat.set_shader_parameter("fog_enabled", 1.0 if c.fog != null else 0.0)
	if c.fog != null:
		mat.set_shader_parameter("fog_color", c.fog.color)
		mat.set_shader_parameter("fog_range_start", c.fog.start)
		mat.set_shader_parameter("fog_range_end", c.fog.end)
	mat.set_shader_parameter("cloud_tex", _image(c.cloud))
	mat.set_shader_parameter("macro_tex", _image(c.macro))
	mat.set_shader_parameter("shroud_enabled", c.shroud != null)
	if c.shroud != null:
		mat.set_shader_parameter("shroud_color", c.shroud.color)
		mat.set_shader_parameter("shroud_tex", _image(Color(c.shroud.level, 0, 0, 1)))
	mat.set_shader_parameter("output_gamma", c.get("output_gamma", 2.2))
	_quad.material_override = mat
	await _frames(6)
	var px := _vp.get_texture().get_image().get_pixel(_vp.size.x / 2, _vp.size.y / 2)
	return Color(px.r * 255.0, px.g * 255.0, px.b * 255.0, px.a * 255.0)


func _case(what: String, c: Dictionary, want: Vector3) -> void:
	var got := await _render(c)
	var ok := absf(got.r - want.x) <= TOL and absf(got.g - want.y) <= TOL and absf(got.b - want.z) <= TOL # tolerance: see TOL
	print("  %s %s: got (%.1f,%.1f,%.1f) want (%.1f,%.1f,%.1f)" % ["ok  " if ok else "FAIL", what, got.r, got.g, got.b, want.x * 1.0, want.y * 1.0, want.z * 1.0])
	if not ok:
		_failures += 1


func _base(overrides := {}) -> Dictionary:
	var c := {
		"albedo": Color(0.5, 0.6, 0.7, 1.0), "normal": Color8(128, 128, 255, 0), "vcol": Color(0.3, 0.4, 0.5, 1.0),
		"sun": Vector3(0.8, 0.6, 0.4), "lightning": Vector3.ZERO, "overbright": true, "fog": null,
		"cloud": Color.WHITE, "macro": Color.WHITE, "shroud": null,
	}
	c.merge(overrides, true)
	return c


## Retail's arithmetic for the centre pixel (L = H = (0, 0, 1) there), per channel, in bytes. n: the normal texel in bytes (r, g, b, a).
func _expected(c: Dictionary, fog_f := 1.0, fog_colour := Vector3.ZERO, shroud := Vector3.ONE) -> Vector3:
	var nm: Color = c.normal
	var n := Vector3(2.0 * nm.r - 1.0, 2.0 * nm.g - 1.0, 2.0 * nm.b - 1.0).normalized()
	var n_dot_l := n.z
	var n_dot_h := n.z
	var diff := maxf(n_dot_l, 0.0) if n_dot_l > 0.0 else 0.0
	var spec := 0.0
	if n_dot_l > 0.0 and n_dot_h > 0.0:
		spec = pow(n_dot_h, 1200.0) * nm.a * 0.9
	var a: Color = c.albedo
	var v: Color = c.vcol
	var sun: Vector3 = c.sun * (0.5 if c.overbright else 1.0)
	var out := Vector3.ZERO
	for i in 3:
		var alb: float = a[i]
		var lit: float = (v[i] + 0.5 * c.lightning[i]) * alb + sun[i] * (diff * alb + spec)
		if c.overbright:
			lit *= 2.0
		var cloud: Color = c.cloud
		var macro: Color = c.macro
		lit = lit * cloud[i] * macro[i]
		lit = lerpf(fog_colour[i], lit, fog_f) * shroud[i]
		out[i] = clampf(lit, 0.0, 1.0) * 255.0
	return out


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: this test renders; run it without --headless")
		return EXIT_SKIP
	if not ResourceLoader.exists("res://shaders/road.gdshader"):
		print("FAIL: res://shaders/road.gdshader is missing")
		return EXIT_FAIL
	print("road shader test: renderer %s" % RenderingServer.get_video_adapter_name())
	_setup()
	await _frames(2)

	# the fallback normal texel of a road without a _nrm texture: (127, 127, 255, 0): flat, no specular
	var c := _base({"normal": Color8(127, 127, 255, 0)})
	await _case("overbright, fallback normal texel", c, _expected(c))
	# the specular mask is the normal map's alpha: the same texel with alpha 255 adds sun * (N.H)^1200 * 0.9 before the 2x
	c = _base({"normal": Color8(128, 128, 255, 255), "albedo": Color(0.2, 0.25, 0.3, 1.0), "vcol": Color(0.1, 0.1, 0.1, 1.0), "sun": Vector3(0.3, 0.2, 0.1)})
	await _case("specular from the normal alpha", c, _expected(c))
	c = _base({"normal": Color8(128, 128, 255, 0), "albedo": Color(0.2, 0.25, 0.3, 1.0), "vcol": Color(0.1, 0.1, 0.1, 1.0), "sun": Vector3(0.3, 0.2, 0.1)})
	await _case("no specular with the normal alpha 0", c, _expected(c))
	# no overbright: the sun is not halved and nothing is doubled; lightning adds 0.5 * LightningColor to the vertex colour
	c = _base({"overbright": false, "lightning": Vector3(0.2, 0.4, 0.6)})
	await _case("no overbright, lightning", c, _expected(c))
	# element 2 takes z from the blue texel: (204, 128, 64) is N = (0.6, 0, -0.5) normalised: facing away from the sun, no diffuse (element 0 would rebuild z = +0.8)
	c = _base({"normal": Color8(204, 128, 64, 0)})
	await _case("z is the texel's blue (not rebuilt)", c, _expected(c))
	# cloud and macro multiply the lit colour
	c = _base({"cloud": Color(0.5, 0.5, 0.5, 1.0), "macro": Color(0.8, 0.6, 0.4, 1.0)})
	await _case("cloud x macro", c, _expected(c))
	# the shroud texture's red times the shroud colour
	c = _base({"shroud": {"color": Vector3(1.0, 0.5, 0.25), "level": 0.5}})
	await _case("shroud", c, _expected(c, 1.0, Vector3.ZERO, Vector3(1.0, 0.5, 0.25) * 0.5))
	# fog lerps toward the fog colour by the per-vertex factor 1 - clamp((eye distance - start) / (end - start)); the corner distance is
	# interpolated, and the centre of this symmetric quad carries the corner value
	var fog := {"color": Vector3(0.25, 0.5, 0.75), "start": 0.0, "end": 200.0}
	c = _base({"fog": fog})
	var corner := Vector3(HALF, CAMERA_HEIGHT, HALF).length()
	await _case("fog", c, _expected(c, 1.0 - corner / 200.0, fog.color))
	# alpha = base.a * vertex.a, handed over in gamma space (output_gamma 1) onto black: the displayed byte is the sRGB encode of 0.25 * rgb
	c = _base({"albedo": Color(0.5, 0.6, 0.7, 0.5), "vcol": Color(0.3, 0.4, 0.5, 0.5), "output_gamma": 1.0})
	var rgb := _expected(c) / 255.0
	var want := Vector3(Color(0.25 * rgb.x, 0.25 * rgb.y, 0.25 * rgb.z).linear_to_srgb().r, Color(0.25 * rgb.x, 0.25 * rgb.y, 0.25 * rgb.z).linear_to_srgb().g,
		Color(0.25 * rgb.x, 0.25 * rgb.y, 0.25 * rgb.z).linear_to_srgb().b) * 255.0
	await _case("alpha = base.a * vertex.a", c, want)
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
