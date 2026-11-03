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
	create_timer(120).timeout.connect(func(): printerr("FORMATION TIMEOUT"); quit(1))
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	var loaded: Dictionary = world.load_map("map mp fall back 4p",{"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]})
	if not _check(loaded.ok):
		_finish("")
		return
	var player := -1
	for candidate in world.get_economy().players:
		if candidate.name == "Player_1": player = int(candidate.index)
	_check(player != -1)
	# Remove map creep/neutral units so combat does not interrupt arrival measurements.
	for id in world.get_object_ids(): world.destroy_object(id)
	world.advance(0.2)
	var units := PackedInt64Array()
	for i in 4: units.append(world.create_object("GondorKnightHorde" if i<3 else "GondorArcherHorde",player,1900+i*140,2300,0))
	if "--hero-preview" in OS.get_cmdline_user_args(): units.append(world.create_object("GondorAragorn",player,2300,2400,0))
	for id in units: _check(world.get_object(id).ok)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	if not _check(hud.setup(fs,world,camera,"Player_1",{"test_hooks":true,"camera_start":Vector3(2100,0,-2000),"alternate_mouse":not ("--standard" in OS.get_cmdline_user_args())}).ok):
		_finish("")
		return
	_check(hud.select_objects_with_message(units)==units.size())
	world.advance(0.2)
	for i in 4: await process_frame
	var start: Vector2 = hud.world_to_pixel(Vector2(2100,2100))
	hud.inject_mouse_move(start)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,true,start)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,true,start)
	for i in 3: await process_frame
	var preview: Dictionary = hud.get_placement_ghost_state()
	_check(preview.formation_visible and preview.formation_slots.size()==units.size(),str(preview))
	_check(preview.formation_textures.has("FPCavSingle.tga"),str(preview))
	_check(preview.formation_textures.has("FPFacingArrowHead.tga"),str(preview))
	_check(preview.formation_textures.has("FPFacingArrowBody.tga"),str(preview))
	_check(preview.formation_textures.has("FPSmallFacingArrow.tga"),str(preview))
	var narrow: Array = preview.formation_slots
	var end: Vector2 = hud.world_to_pixel(Vector2(2400,2100))
	hud.inject_mouse_move(end)
	for i in 3: await process_frame
	preview = hud.get_placement_ghost_state()
	_check(preview.formation_visible and preview.formation_slots.size()==units.size(),str(preview))
	_check(preview.formation_slots != narrow,"Drag did not change formation width")
	_check(hud.get_selection().size()==units.size(),"Formation changed selection")
	if DisplayServer.get_name() != "headless" and not OS.get_environment("FORMATION_SHOT").is_empty():
		await RenderingServer.frame_post_draw
		_check(root.get_texture().get_image().save_png(OS.get_environment("FORMATION_SHOT")) == OK)
	if "--preview-only" in OS.get_cmdline_user_args() or "--hero-preview" in OS.get_cmdline_user_args():
		_finish("MIXED FORMATION PREVIEW PASS")
		return
	var slots: Array = preview.formation_slots

	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,false,end)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,false,end)
	for i in 3: await process_frame
	_check("MSG_DO_MOVETO_FORMATION" in str(hud.get_message_log()))
	await create_timer(2.1).timeout
	_check(not hud.get_placement_ghost_state().formation_visible)
	_check(hud.get_selection().size()==units.size(),"Releasing the chord deselected units")
	for i in 150: world.advance(0.2)
	for slot in slots:
		var unit: Dictionary = world.get_object(slot.id)
		_check(unit.ok,str([unit,slot]))
		var center := Vector2.ZERO
		var members: Array = unit.get("members",[])
		for member in members:
			var m = world.get_object(member)
			center += Vector2(m.x,m.y)
		if members.is_empty(): center = Vector2(unit.x,unit.y)
		else: center /= members.size()
		# tolerance: locomotor stopping distance plus snapping to the 10-unit navigation grid.
		_check(center.distance_to(Vector2(slot.x,slot.y))<15,str([center,slot]))
		# tolerance: the recovered final-facing completion threshold is 0.31415927 radians.
		_check(abs(angle_difference(unit.angle,slot.angle))<0.31415927,str([unit,slot]))
	_finish("MIXED FORMATION PASS")
