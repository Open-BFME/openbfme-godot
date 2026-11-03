## Player colours must use baked variants for both units and buildings.
extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		quit(77);return
	create_timer(120).timeout.connect(func(): push_error("Player colour test timed out");quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	var slots: Array=[]
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	for i in 2: slots[i]={"state":6,"name":"Colour%d"%i,"accepted":true,"color":i,"start_pos":i,"player_template":3,"team":i}
	_check(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":"maps/map mp evendim/map mp evendim.map",
		"map_crc":0,"map_size":0,"map_mask":0,"seed":4242,"starting_cash":10000,"slots":slots}).ok)
	if not _check(world.start_new_game({"logic_thread":false,"wall_rebalance":false}).ok):
		_finish("")
		return
	world.set_shroud_drawn(false)
	for template in ["GondorFighter","GondorBarracks"]:
		var variants: Array=[]
		for player in [3,4]:
			var id: int=world.create_object(template,player,1800+player*300,1800,0)
			_check(id>0,"Object creation failed") # tolerance: allocated object IDs must be positive.
			for i in 3: world.advance(0.2)
			var info: Dictionary=world.get_object(id)
			_check(not info.shown_models.is_empty(),str(info))
			var model: String=info.shown_models[0]
			_check(model.contains("#1&"),str(info))
			variants.append(model)
		_check(variants[0]!=variants[1],str(variants))
	# Read back a real unit's baked house texture: a pure-green primary colour
	# must leave no red/blue texels and retain nonzero shaded green texels.
	var renderer=ClassDB.instantiate("W3DInstancer")
	root.add_child(renderer)
	if not _check(renderer.setup(fs).ok):
		_finish("")
		return
	renderer.set_house_colors_enabled(true)
	var model: int=renderer.add_model_colored(world.get_template_model("GondorFighter"),1,PackedInt64Array([0xff00ff00]))
	_check(model>=0) # tolerance: model handles are nonnegative.
	var packed_note := false
	for note in renderer.get_model_report(model).notes:
		if str(note).replace("\\", "/").contains("[S-1482] house texture art/compiledtextures/hc/hc_gumanatarms04.jpg: packed PNG colour multiplication and alpha replacement are inferred; the loader merge was not read"):
			packed_note = true
	_check(packed_note, str(renderer.get_model_report(model).notes))
	_check(renderer.add_instance(model,Transform3D.IDENTITY)>=0) # tolerance: instance handles are nonnegative.
	for i in 3: await process_frame
	var textures := 0
	var green := false
	for child in renderer.get_children():
		if not child is MultiMeshInstance3D: continue
		var mesh: Mesh=child.multimesh.mesh
		for surface in mesh.get_surface_count():
			var material: ShaderMaterial=mesh.surface_get_material(surface)
			if material==null: continue
			var texture=material.get_shader_parameter("w3d_hc_tex")
			if texture==null: continue
			var pixels: Image=texture.get_image()
			_check(not pixels.is_empty())
			for y in pixels.get_height():
				for x in pixels.get_width():
					var pixel: Color=pixels.get_pixel(x,y)
					_check(pixel.r==0 and pixel.b==0,str(pixel))
					green=green or pixel.g!=0
			textures+=1
	_check(textures>0) # tolerance: this retail unit must contain at least one house texture.
	_check(green)
	_finish("PLAYER HOUSE COLOUR PASS")
