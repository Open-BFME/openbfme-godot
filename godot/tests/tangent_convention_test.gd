## Godot's BINORMAL convention (lane RENDER-1, stop S-023). Needs a real display:
##
##   godot --path godot --script res://tests/tangent_convention_test.gd        (NOT --headless)
##
## The instancer hands Godot TANGENT = (file tangent, w) and the generated FX shader reads BINORMAL as the file's bitangent
## (normalmapped.fxo's vertex shader negates it: frame x = -binormal). That holds only if Godot computes
## BINORMAL = cross(NORMAL, TANGENT.xyz) * TANGENT.w. Measured here on the running Godot, by drawing a triangle whose vertex data is known and
## reading BINORMAL back as a colour, in the vertex stage (through a varying) and in the fragment stage, with an uncompressed surface and one
## with ARRAY_FLAG_COMPRESS_ATTRIBUTES (the octahedral / packed vertex format). Godot 4.7.2: N = (0, 0, 1), T = (1, 0, 0) gives (0, +w, 0).
## Exit codes: 0 pass, 1 fail, 77 skip (headless).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("TANGENT CONVENTION %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


## BINORMAL as the vertex or fragment stage sees it for a triangle that covers the view: normal n, tangent t, w, as a vector of -1 / 0 / +1.
func _binormal(n: Vector3, t: Vector3, w: float, compress: bool, stage: String) -> Vector3i:
	var vp := SubViewport.new()
	vp.size = Vector2i(32, 32)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 2.0
	cam.position = Vector3(0, 0, 4)
	vp.add_child(cam)
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-5, -5, 0), Vector3(5, -5, 0), Vector3(0, 5, 0)])
	arr[Mesh.ARRAY_NORMAL] = PackedVector3Array([n, n, n])
	var tg := PackedFloat32Array()
	for i in 3:
		tg.append_array([t.x, t.y, t.z, w])
	arr[Mesh.ARRAY_TANGENT] = tg
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr, [], {}, Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES if compress else 0)
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	var mat := ShaderMaterial.new()
	mat.shader = Shader.new()
	var code := "shader_type spatial; render_mode unshaded, cull_disabled; varying vec3 vb;\n"
	code += "void vertex() { vb = BINORMAL; }\n"
	code += "void fragment() { ALBEDO = %s * 0.5 + 0.5; }" % ("vb" if stage == "vertex" else "BINORMAL")
	mat.shader.code = code
	mi.material_override = mat
	vp.add_child(mi)
	for i in 4:
		await process_frame
	await RenderingServer.frame_post_draw
	# the viewport is sRGB-encoded; the colour holds (BINORMAL * 0.5 + 0.5) as a linear value
	var c := vp.get_texture().get_image().get_pixel(16, 16).srgb_to_linear()
	vp.queue_free()
	return Vector3i(roundi(c.r * 2.0 - 1.0), roundi(c.g * 2.0 - 1.0), roundi(c.b * 2.0 - 1.0))


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: this test renders; run it without --headless")
		return EXIT_SKIP
	print("Godot %s, renderer %s" % [Engine.get_version_info().string, RenderingServer.get_video_adapter_name()])
	# n, t, w -> the expected cross(n, t) * w
	var cases := [
		[Vector3(0, 0, 1), Vector3(1, 0, 0), 1.0, Vector3i(0, 1, 0)],
		[Vector3(0, 0, 1), Vector3(1, 0, 0), -1.0, Vector3i(0, -1, 0)],
		[Vector3(0, 0, 1), Vector3(0, 1, 0), 1.0, Vector3i(-1, 0, 0)],
		[Vector3(0, 0, 1), Vector3(0, 1, 0), -1.0, Vector3i(1, 0, 0)],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), 1.0, Vector3i(0, 0, -1)],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), -1.0, Vector3i(0, 0, 1)],
	]
	for stage in ["vertex", "fragment"]:
		for compress in [false, true]:
			for c in cases:
				var got: Vector3i = await _binormal(c[0], c[1], c[2], compress, stage)
				_check(got == c[3], "%s stage, compress_attributes=%s: N=%s T=%s w=%+d -> BINORMAL %s (cross(N, T) * w = %s)" % [stage, compress, c[0], c[1], int(c[2]), got, c[3]])
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
