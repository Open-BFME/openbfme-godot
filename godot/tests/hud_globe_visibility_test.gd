## Both editions' presentation: hidden Palantir globe viewports must stop rendering.
## Requires private RotWK/BFME2 assets and the extension; headless is sufficient.
extends SceneTree

var failures := 0
var world: Node
var hud: Node2D
var player: Node2D
var shared: Node2D
var camera: Camera3D
var views: Array[SubViewport] = []

func _initialize() -> void:
	call_deferred("_run")

func check(ok: bool, message: String) -> bool:
	print("HUD GLOBE ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1
	return ok

func modes(expected: int, message: String) -> void:
	var actual: Array[int] = []
	for view in views:
		actual.append(view.render_target_update_mode)
	check(actual.size()==2 and actual.all(func(value): return value==expected), message+": "+str(actual))

func settle() -> void:
	for _frame in range(8):
		await process_frame

func finish() -> void:
	if is_instance_valid(hud):
		hud.free()
	if is_instance_valid(shared):
		shared.free()
	if is_instance_valid(camera):
		camera.free()
	if is_instance_valid(world):
		world.clear_game_data(false)
		world.free()
	print("HUD GLOBE ", "PASS" if failures==0 else "FAILED")
	quit(0 if failures==0 else 1)

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("HUD GLOBE SKIP: requires ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	Engine.max_fps = 60
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail_paths(OS.get_environment("ROTWK_INSTALL"),OS.get_environment("BFME2_INSTALL"))
	if not check(mounted.ok,"mount: "+str(mounted.get("errors",[]))):
		finish()
		return
	world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not check(setup.ok,"setup: "+str(setup.get("errors",[]))):
		finish()
		return
	var loaded: Dictionary = world.load_map("map mp fall back 4p",{"slots":[{"player":"Player_1","faction":"FactionMen","human":true,"team":0,"start_index":0}],"seed":4711,"logic_thread":false})
	if not check(loaded.ok,"map: "+str(loaded.get("errors",[]))):
		finish()
		return
	world.set_auto_advance(false)
	var frame: int = world.get_frame()
	var state_hash: int = world.get_state_hash()
	camera = Camera3D.new()
	root.add_child(camera)
	hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var boot: Dictionary = hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3.ZERO})
	if not check(boot.ok,"HUD setup: "+str(boot.get("errors",[]))):
		finish()
		return
	for child in hud.get_children():
		if child.is_class("AptMenuPlayer"):
			player = child
	if not check(player!=null,"HUD owns a real native movie player"):
		finish()
		return
	for _frame in range(40):
		await process_frame
	for child in hud.get_children():
		if child is SubViewport and String(child.name).begins_with("PalantirGlobe"):
			views.append(child)
	if not check(views.size()==2,"both native globe layers created"):
		finish()
		return
	modes(SubViewport.UPDATE_ALWAYS,"visible globe renders")
	var level: int = hud.get_spellbook_state().palantir_level
	player.set_level_visible(level,false)
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"hidden movie stops both globe scenes")
	player.set_level_visible(level,true)
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"movie reveal resumes both scenes")
	# A cached render list must remain active without needing a full canvas rebuild.
	var unchanged: int = player.get_stats().unchanged_frames
	player.render(true)
	player.render(true)
	check(player.get_stats().unchanged_frames==unchanged+2,"both forced draws take the cached native path")
	modes(SubViewport.UPDATE_ALWAYS,"cached native redraw retains globe")
	# tolerance: render scheduling changes the count; this asserts that the native partial-redraw branch was exercised.
	check(player.get_stats().partial_frames>0,"movie hide/reveal exercises a partial retained redraw")
	player.hide()
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"hidden player stops both scenes without changing its movie levels")
	player.show()
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"player reveal resumes retained globe scenes")
	hud.hide()
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"hidden HUD owner stops both scenes")
	hud.show()
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"HUD reveal resumes both scenes")
	# Freeze menu updates, remove all movie levels, and exercise render's empty-list cleanup.
	hud.set_process(false)
	player.auto_process = false
	for id in range(64):
		if player.has_level(id):
			player.unload_level(id)
	player.render(true)
	modes(SubViewport.UPDATE_DISABLED,"empty canvas releases globe rendering")
	check(world.get_frame()==frame and world.get_state_hash()==state_hash,"visibility changes preserve simulation frame and state hash")
	# Free another HUD while its globe is still visible: child viewports can die before the owned movie player's
	# clearCanvas notification. The native hook must validate resource IDs rather than dereference freed children.
	hud.free()
	hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var again: Dictionary = hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3.ZERO})
	if check(again.ok,"second HUD setup for visible teardown"):
		for _frame in range(40):
			await process_frame
		var live_layers := 0
		for child in hud.get_children():
			if child is SubViewport and String(child.name).begins_with("PalantirGlobe") and child.render_target_update_mode==SubViewport.UPDATE_ALWAYS:
				live_layers += 1
		check(live_layers==2,"both globe scenes active immediately before HUD free")
	hud.free()
	hud = null
	check(world.get_frame()==frame and world.get_state_hash()==state_hash,"visible HUD teardown preserves simulation state")
	await shared_player_case(fs)
	finish()

# Gameplay attaches to a shell-owned player that outlives the HUD. Exercise its callbacks and detachment too.
func shared_player_case(fs: RefCounted) -> void:
	var frame: int = world.get_frame()
	var state_hash: int = world.get_state_hash()
	shared = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shared)
	var folder := OS.get_environment("OPENBFME_GAME_USER_DIR")
	if folder.is_empty():
		folder = OS.get_temp_dir()
	var config := {"options_file": folder.path_join("globe-options-%d.ini" % OS.get_process_id()),
		"skirmish_ini": folder.path_join("globe-skirmish-%d.ini" % OS.get_process_id()), "seed": 4711}
	var boot: Dictionary = shared.boot_shell(fs,world,config)
	if not check(boot.ok,"shared shell boot: "+str(boot.get("errors",[]))):
		return
	if not check(shared.shell_push("Palantir.apt"),"shared shell pushes the gameplay Palantir screen"):
		return
	hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var attached: Dictionary = hud.attach(fs,world,camera,shared,"Player_1",{"camera_start":Vector3.ZERO})
	if not check(attached.ok,"HUD attaches to shared shell: "+str(attached.get("errors",[]))):
		return
	for _frame in range(40):
		await process_frame
	views.clear()
	for child in hud.get_children():
		if child is SubViewport and String(child.name).begins_with("PalantirGlobe"):
			views.append(child)
	if not check(views.size()==2,"shared shell creates both native globe layers"):
		return
	modes(SubViewport.UPDATE_ALWAYS,"shared shell visible globe renders")
	var level: int = hud.get_spellbook_state().palantir_level
	shared.set_level_visible(level,false)
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"shared movie hide stops both scenes")
	shared.set_level_visible(level,true)
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"shared movie reveal resumes both scenes")
	shared.hide()
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"shared player hide stops both scenes")
	shared.show()
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"shared player reveal resumes both scenes")
	hud.hide()
	await settle()
	modes(SubViewport.UPDATE_DISABLED,"shared HUD hide stops both scenes")
	hud.show()
	await settle()
	modes(SubViewport.UPDATE_ALWAYS,"shared HUD reveal resumes both scenes")
	shared.render(true)
	shared.render(true)
	modes(SubViewport.UPDATE_ALWAYS,"shared cached redraw retains globe")
	hud.free()
	hud = null
	check(is_instance_valid(shared) and shared.is_booted(),"HUD free preserves booted shared shell")
	check(shared.shell_push("MainMenu.apt"),"shared shell opens a new screen after HUD hook detachment")
	for _frame in range(40):
		await process_frame
	check(shared.has_level(shared.shell_top_level()),"shared shell movie loads after HUD free")
	var rebuilds: int = shared.get_stats().rebuilds
	shared.render(true)
	check(shared.get_stats().rebuilds==rebuilds+1,"shared shell renders normally after HUD free")
	check(world.get_frame()==frame and world.get_state_hash()==state_hash,"shared shell lifecycle preserves simulation state")
