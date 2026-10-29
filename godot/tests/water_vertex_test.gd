## GPU check that the standing water computes what retail's vertex shaders compute per vertex, and the pixel shaders only interpolate:
##   * the range-fog factor (watershader.fxo vs blob 19 `mad oD1.w, c32.x, -r0.w, r1.x`, blob 9 oT6.w, blob 13 oD1.x; ps blob 4 `lrp r1.xyz, v1.w, r2, c6`)
##   * BumpSpecular_M's half-vector (vs blob 9 `nrm r4.xyz, r3` -> `mov oT6.xyz, r4`; ps blob 8 `dp3_sat_pp r5.x, r4, t6`: no second normalisation)
## Run with a GPU: godot --rendering-method gl_compatibility --path godot --script res://tests/water_vertex_test.gd
## It needs no retail data: the production shader is compiled with its output replaced by the varying under test.
extends SceneTree

const WIDTH := 640
const HEIGHT := 64
const FOG_START := 150.0
const FOG_END := 250.0
const CAMERA_HEIGHT := 100.0

func _initialize() -> void:
	call_deferred("_run")

func _material(code: String, probe: String) -> ShaderMaterial:
	var shader := Shader.new()
	if not code.contains("ALBEDO = retail_output(col);") or not code.contains("ALPHA = clamp(alpha, 0.0, 1.0);"):
		push_error("water_standing.gdshader no longer has the output lines this test replaces")
		quit(1)
		return null
	shader.code = code.replace("ALBEDO = retail_output(col);", "ALBEDO = %s;" % probe).replace("ALPHA = clamp(alpha, 0.0, 1.0);", "ALPHA = 1.0;")
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("fog_enabled", 1.0)
	material.set_shader_parameter("fog_range_start", FOG_START)
	material.set_shader_parameter("fog_range_end", FOG_END)
	material.set_shader_parameter("specular_direction", Vector3(0, 0, 1))
	return material

## a horizontal triangle mesh at y = 0 from explicit vertices (SAGE x, -z in Godot) and triangle indices
func _mesh(vertices: PackedVector3Array, indices: PackedInt32Array, material: ShaderMaterial) -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	return mesh

## renders `mesh` under a top-down orthographic camera at (0, CAMERA_HEIGHT, 0), one pixel per world unit, and returns the image
func _render(mesh: ArrayMesh) -> Image:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(WIDTH, HEIGHT)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	viewport.add_child(instance)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = float(HEIGHT) # the height in world units: WIDTH / HEIGHT times that wide, so one pixel is one unit
	viewport.add_child(camera)
	camera.position = Vector3(0, CAMERA_HEIGHT, 0)
	camera.look_at(Vector3.ZERO, Vector3(0, 0, -1)) # screen right = +x, screen up = -z
	camera.current = true
	for frame in 5:
		await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	viewport.queue_free()
	return image

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _fog_at_distance(distance: float) -> float:
	return 1.0 - clampf((distance - FOG_START) / (FOG_END - FOG_START), 0.0, 1.0)

func _run() -> void:
	if RenderingServer.get_current_rendering_method() != "gl_compatibility":
		_fail("Run with --rendering-method gl_compatibility for raw varying readback")
		return
	var code := FileAccess.get_file_as_string("res://shaders/water_standing.gdshader")
	# BumpSpecular_M's pixel shader must use the interpolated half-vector as it is
	if not code.contains("dot(n, v_half)") or code.contains("normalize(v_half)"):
		_fail("BumpSpecular_M must use the interpolated per-vertex half-vector without normalising it")
		return

	# 1. fog start and end boundaries over a strip of vertices (camera at the origin, 100 above the plane): a vertex at distance d gets fog_at_distance(d);
	#    between vertices the factor is interpolated linearly in screen space
	var radii: Array[float] = []
	for distance in [CAMERA_HEIGHT, FOG_START, 200.0, FOG_END, 300.0, 400.0]:
		radii.append(sqrt(distance * distance - CAMERA_HEIGHT * CAMERA_HEIGHT))
	var vertices := PackedVector3Array()
	var indices := PackedInt32Array()
	for r in radii:
		vertices.append(Vector3(r, 0, -20))
		vertices.append(Vector3(r, 0, 20))
	for i in radii.size() - 1:
		var a := i * 2
		indices.append_array(PackedInt32Array([a, a + 2, a + 1, a + 1, a + 2, a + 3]))
	var fog_image: Image = await _render(_mesh(vertices, indices, _material(code, "vec3(v_fog)")))
	var vertex_fog: Array[float] = []
	for r in radii:
		vertex_fog.append(_fog_at_distance(sqrt(r * r + CAMERA_HEIGHT * CAMERA_HEIGHT)))
	if not (is_equal_approx(vertex_fog[0], 1.0) and is_equal_approx(vertex_fog[1], 1.0) and is_equal_approx(vertex_fog[2], 0.5) and is_equal_approx(vertex_fog[3], 0.0) and is_equal_approx(vertex_fog[5], 0.0)):
		_fail("test setup: vertex fog factors %s" % str(vertex_fog))
		return
	for pixel in range(WIDTH / 2 + 2, WIDTH - 2, 7):
		var x := float(pixel) + 0.5 - WIDTH / 2.0
		var expected := 0.0
		for i in radii.size() - 1:
			if x >= radii[i] and x <= radii[i + 1]:
				expected = lerpf(vertex_fog[i], vertex_fog[i + 1], (x - radii[i]) / (radii[i + 1] - radii[i]))
		var actual := fog_image.get_pixel(pixel, HEIGHT / 2).r
		if absf(actual - expected) > 0.03: # tolerance: 8-bit render target and the pixel centre's sub-pixel offset (measured worst case 0.024)
			_fail("fog at x = %.1f: %.3f, interpolated per-vertex value %.3f" % [x, actual, expected])
			return
	# exactly at the end vertex and beyond it the fog is complete; at the start vertex and below it there is none
	if not is_equal_approx(fog_image.get_pixel(WIDTH / 2 + 2, HEIGHT / 2).r, 1.0) or fog_image.get_pixel(WIDTH - 3, HEIGHT / 2).r != 0.0:
		_fail("fog below the start distance or beyond the end distance is not 1 / 0")
		return

	# 2. one quad whose four corners are all beyond the end distance and whose centre is nearer than the start distance: retail's per-vertex factor is 0
	#    at the centre (every corner is 0), a per-fragment factor would be 1
	var quad := PackedVector3Array([Vector3(-300, 0, -300), Vector3(300, 0, -300), Vector3(-300, 0, 300), Vector3(300, 0, 300)])
	var quad_indices := PackedInt32Array([0, 1, 2, 2, 1, 3])
	var quad_image: Image = await _render(_mesh(quad, quad_indices, _material(code, "vec3(v_fog)")))
	var centre_fog := quad_image.get_pixel(WIDTH / 2, HEIGHT / 2).r
	if centre_fog != 0.0:
		_fail("fog at the centre of a quad with four fully fogged corners is %.3f (a per-fragment factor would be 1.0)" % centre_fog)
		return

	# 3. BumpSpecular_M's half-vector: normalised at each vertex, interpolated unnormalised. A quad's four corners have eye directions that differ, so the
	#    centre's interpolated vector is shorter than 1 (a per-fragment or re-normalised vector has length 1)
	var corners := PackedVector3Array([Vector3(-200, 0, -200), Vector3(200, 0, -200), Vector3(-200, 0, 200), Vector3(200, 0, 200)])
	var expected_sum := Vector3.ZERO
	for corner in corners:
		var eye := (Vector3(0, CAMERA_HEIGHT, 0) - corner).normalized()
		var sage_eye := Vector3(eye.x, -eye.z, eye.y)
		expected_sum += (sage_eye + Vector3(0, 0, 1)).normalized()
	var expected_length := (expected_sum / 4.0).length()
	var half_image: Image = await _render(_mesh(corners, quad_indices, _material(code, "vec3(length(v_half))")))
	var actual_length := half_image.get_pixel(WIDTH / 2, HEIGHT / 2).r
	print("half-vector length at the centre: ", actual_length, " expected ", expected_length)
	if absf(actual_length - expected_length) > 0.02 or expected_length > 0.9: # tolerance: 8-bit readback; the second term guards the setup
		_fail("interpolated half-vector length %.3f, expected %.3f (a per-fragment vector is 1.0)" % [actual_length, expected_length])
		return
	print("PASS: per-vertex water fog (start and end boundaries, interpolation) and BumpSpecular_M half-vector")
	quit(0)
