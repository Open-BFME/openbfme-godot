## GPU integration: a real W3D model casts onto the real terrain shader.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	assert(DisplayServer.get_name() != "headless", "This test needs a GPU")
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var atlas = Image.create(4,4,false,Image.FORMAT_RGBA8)
	atlas.fill(Color.WHITE)
	var terrain = builder.build_test_patch({"base_atlas":atlas,"uv0":PackedVector2Array([Vector2(.5,.5),Vector2(.5,.5),Vector2(.5,.5),Vector2(.5,.5)]),"layers":[]})
	root.add_child(terrain)
	var model = inst.add_model("GBWALLN")
	assert(model>=0,str(inst.get_errors()))
	var transform = Transform3D(Basis.IDENTITY.scaled(Vector3.ONE*.035),Vector3(5,0,-5))
	assert(inst.add_instance(model,transform)>=0)
	inst.update_now()
	var light = DirectionalLight3D.new()
	light.rotation_degrees=Vector3(-45,-45,0)
	light.shadow_enabled=false
	light.directional_shadow_max_distance=40
	root.add_child(light)
	var camera = Camera3D.new()
	camera.projection=Camera3D.PROJECTION_ORTHOGONAL
	camera.size=10
	camera.position=Vector3(5,15,-5)
	camera.rotation_degrees=Vector3(-90,0,0)
	root.add_child(camera)
	camera.current=true
	root.size=Vector2i(512,512)
	RenderingServer.global_shader_parameter_set("w3d_shadow_strength",.65)
	for i in 12: await process_frame
	await RenderingServer.frame_post_draw
	var baseline: Image=root.get_texture().get_image()
	light.shadow_enabled=true
	for i in 12: await process_frame
	await RenderingServer.frame_post_draw
	var shadowed: Image=root.get_texture().get_image()
	var darkened:=0
	var unchanged:=0
	for y in range(0,512,2):
		for x in range(0,512,2):
			var before=baseline.get_pixel(x,y).get_luminance()
			var after=shadowed.get_pixel(x,y).get_luminance()
			if before-after>.1: darkened+=1
			if abs(before-after)<.02: unchanged+=1
	assert(darkened>20,"No model shadow on terrain: %d pixels" % darkened)
	assert(unchanged>40000,"Unshadowed terrain lighting changed")
	shadowed.save_png("/tmp/openbfme-shadows.png")
	print("SHADOWS PASS: %d shadowed samples, %d unchanged" % [darkened,unchanged])
	quit()
