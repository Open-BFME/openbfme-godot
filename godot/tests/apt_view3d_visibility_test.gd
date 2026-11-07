## Both editions' presentation: real retained View3D scenes stop hidden rendering/pose work.
## Private retail assets; headless supported. Run in an isolated project/user directory.
extends SceneTree

# A sibling processed after the player and its instancers reveals it in the same frame.
class LateReveal extends Node:
	signal revealed
	var target: CanvasItem
	func _process(_delta: float) -> void:
		target.show()
		set_process(false)
		revealed.emit()

var failures := 0
var parent: Node2D
var player: Node2D
var world: Node
var views: Array[SubViewport] = []
var active: Array[SubViewport] = []
var simulation_frame := -1
var simulation_hash := -1

func _initialize() -> void:
	call_deferred("run")

func check(ok: bool, message: String) -> bool:
	print("APT VIEW3D ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1
	return ok

func modes(expected: int, selected: Array[SubViewport], message: String) -> void:
	check(not selected.is_empty() and selected.all(func(v): return v.render_target_update_mode == expected), message)

func current_membership(candidates: Array[SubViewport]) -> Array[SubViewport]:
	# APT retained-slot membership is independent of each viewport's actual update mode.
	# The black viewport owns the instancer; its white partner shares the same World3D.
	var frames: Dictionary = player.get_backdrop_state().model_animation_frames
	var names := frames.keys().map(func(path): return str(path).get_slice("|", 1).to_lower())
	var worlds := []
	for view in candidates:
		for child in view.get_children():
			if child.is_class("W3DInstancer"):
				var name: String = child.get_model_report(0).get("name", "")
				if names.has(name.to_lower()):
					worlds.append(view.find_world_3d())
	var selected: Array[SubViewport] = []
	for view in candidates:
		if worlds.has(view.find_world_3d()):
			selected.append(view)
	check(selected.size() == frames.size() * 2, "each current APT slot resolves to exactly two viewports")
	return selected

func matches_apt_pose(inst: Node) -> bool:
	# Compare the uploaded palette with an explicit reference at the APT clock's exposed frame.
	# Capture first: evaluating the reference must not repair the result being checked.
	var actual: PackedFloat32Array = inst.get_instance_palette(0)
	var name: String = inst.get_model_report(0).get("name", "")
	var frames: Dictionary = player.get_backdrop_state().model_animation_frames
	for path in frames:
		if str(path).to_lower().ends_with("|" + name.to_lower()):
			inst.set_instance_pose(0, name + "." + name, float(frames[path]))
			inst.update_now()
			return actual == inst.get_instance_palette(0)
	return false

func settle() -> void:
	for _frame in range(6):
		await process_frame

func finish() -> void:
	Engine.time_scale = 1.0
	if is_instance_valid(parent):
		parent.free()
	if is_instance_valid(world) and simulation_frame != -1:
		check(world.get_frame() == simulation_frame and world.get_state_hash() == simulation_hash, "presentation and visible teardown preserve simulation frame and hash")
	if is_instance_valid(world):
		world.free()
	print("APT VIEW3D ", "PASS" if failures == 0 else "FAILED")
	quit(0 if failures == 0 else 1)

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("APT VIEW3D SKIP: requires private ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	create_timer(30).timeout.connect(func(): printerr("APT VIEW3D timeout"); quit(1))
	Engine.max_fps = 60
	root.size = Vector2i(1024, 768)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not check(fs.mount_retail().ok, "retail mount"):
		finish()
		return
	world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not check(setup.ok, "world setup: " + str(setup.get("errors", []))):
		finish()
		return
	var loaded: Dictionary = world.load_map("map mp fall back 4p", {"slots": [{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0}], "seed": 4711, "logic_thread": false})
	if not check(loaded.ok, "map loaded for simulation state pin: " + str(loaded.get("errors", []))):
		finish()
		return
	world.set_auto_advance(false)
	simulation_frame = world.get_frame()
	simulation_hash = world.get_state_hash()
	parent = Node2D.new()
	root.add_child(parent)
	player = ClassDB.instantiate("AptMenuPlayer")
	player.auto_process = false
	parent.add_child(player)
	var config := {"options_file": ProjectSettings.globalize_path("user://view3d-options.ini"),
		"skirmish_ini": ProjectSettings.globalize_path("user://view3d-skirmish.ini")}
	if not check(player.boot_shell(fs, world, config).ok, "shell boot"):
		finish()
		return
	for frame in range(6):
		player.tick(0.033)
	var background: int = player.get_backdrop_state().background_level
	var opened: Dictionary = player.shell_invoke(background, "ShowInGameBackground", PackedStringArray())
	check(opened.get("ok", false), "real in-game background movie shown")
	var rings: Dictionary = player.end_game_screen({"kind": "show_end_game", "evil": true, "text": "GUI:Defeated"})
	check(rings.get("ok", false), "independent GuiFX ring level")
	var ring_level: int = rings.get("level", -1)
	for frame in range(30):
		player.tick(0.033)
		player.render(true)
	for child in player.get_children():
		if child is SubViewport:
			views.append(child)
	active = current_membership(views)
	# tolerance: the number of real View3D clips varies with the privately mounted presentation data.
	if not check(active.size() >= 2 and active.size() % 2 == 0, "real paired active View3D viewports: " + str(player.get_backdrop_state())):
		finish()
		return
	var ids := active.map(func(v): return v.get_instance_id())
	player.render(true)
	var unchanged_before: int = player.get_stats().unchanged_frames
	player.render(true)
	player.render(true)
	check(player.get_stats().unchanged_frames == unchanged_before + 2, "both forced draws exercise cached canvas path")
	modes(SubViewport.UPDATE_ALWAYS, active, "cached canvas retains active views")
	player.hide()
	modes(SubViewport.UPDATE_DISABLED, views, "hidden player disables scenes without processing")
	player.render(false)
	modes(SubViewport.UPDATE_DISABLED, views, "clean render early return remains hidden")
	player.show()
	modes(SubViewport.UPDATE_ALWAYS, active, "retained player reveal resumes scenes")
	parent.hide()
	modes(SubViewport.UPDATE_DISABLED, views, "hidden CanvasItem ancestor disables scenes")
	player.render(true)
	modes(SubViewport.UPDATE_DISABLED, views, "cached redraw cannot enable hidden ancestor")
	parent.show()
	modes(SubViewport.UPDATE_ALWAYS, active, "ancestor reveal resumes retained scenes")
	check(active.map(func(v): return v.get_instance_id()) == ids, "hide/reveal retains viewport resources")

	# Find two distinct real animation poses. Pending pose changes must not upload while hidden,
	# even though the owned instancer's independent process callback keeps its animation clock.
	var inst: Node
	var clip := ""
	var pose_a := PackedFloat32Array()
	var frame_b := 0.0
	for view in active:
		for child in view.get_children():
			if not child.is_class("W3DInstancer"):
				continue
			var name: String = child.get_model_report(0).get("name", "")
			var candidate_clip := name + "." + name
			var info: Dictionary = child.get_clip_info(0, candidate_clip)
			if not info.get("ok", false) or int(info.frames) < 2:
				continue
			child.set_instance_pose(0, candidate_clip, 0.0)
			child.update_now()
			var first: PackedFloat32Array = child.get_instance_palette(0)
			for frame in [float(info.frames - 1), float(info.frames) / 2.0]:
				child.set_instance_pose(0, candidate_clip, frame)
				child.update_now()
				var other: PackedFloat32Array = child.get_instance_palette(0)
				if first != other:
					inst = child
					clip = candidate_clip
					pose_a = first
					frame_b = frame
					break
			if inst != null:
				break
		if inst != null:
			break
	if check(inst != null, "retail animation supplies two independent reference poses"):
		inst.set_instance_pose(0, clip, 0.0)
		inst.update_now()
		var control = ClassDB.instantiate("W3DInstancer")
		root.add_child(control)
		inst.set_time_scale(2.0)
		control.set_time_scale(2.0)
		inst.set_global_time(0.0)
		control.set_global_time(0.0)
		parent.hide()
		inst.set_instance_pose(0, clip, frame_b)
		Engine.time_scale = 0.5
		await settle()
		# tolerance: elapsed time is scheduled by Godot; the control receives exactly the same process deltas.
		check(inst.get_global_time() > 0.0 and inst.get_global_time() == control.get_global_time(),
			"hidden clock matches ordinary instancer under Engine and instance time scales")
		check(inst.get_instance_palette(0) == pose_a, "hidden auto process leaves pending pose unuploaded")
		inst.set_playing(false)
		var stopped: float = inst.get_global_time()
		await settle()
		check(inst.get_global_time() == stopped, "hidden Playing=false preserves clock")
		inst.set_playing(true)
		parent.show()
		check(matches_apt_pose(inst), "reveal immediately applies current APT pose instead of hand-set pending pose")
		await settle()
		Engine.time_scale = 1.0
		control.free()


	# Exercise the player's ordinary _process/advance path as well as the child's callback.
	# Real-time View3D clocks continue; neither its cameras nor its bone palettes change hidden.
	var palettes := {}
	var cameras := {}
	for view in active:
		for child in view.get_children():
			if child.is_class("W3DInstancer"):
				palettes[child] = child.get_instance_palette(0)
			elif child is Camera3D:
				cameras[child] = child.transform
	var clock_before: Dictionary = player.get_backdrop_state().model_animation_frames
	parent.hide()
	player.auto_process = true
	player.render(true)
	await settle()
	player.auto_process = false
	var clock_after: Dictionary = player.get_backdrop_state().model_animation_frames
	# tolerance: the private clips choose their own modes/lengths; at least one animated background must progress over six real ticks.
	check(clock_after != clock_before, "hidden ordinary player processing still advances View3D clocks")
	check(palettes.keys().all(func(node): return node.get_instance_palette(0) == palettes[node]), "hidden player advance does not upload bone palettes")
	check(cameras.keys().all(func(camera): return camera.transform == cameras[camera]), "hidden player advance does not move cameras")
	modes(SubViewport.UPDATE_DISABLED, views, "ordinary hidden processing leaves every viewport disabled")
	# One more hidden frame runs the player and child instancers first; a later sibling reveals.
	var late := LateReveal.new()
	late.target = parent
	late.process_priority = 100
	player.auto_process = true
	root.add_child(late)
	await late.revealed
	player.auto_process = false
	# This runs inside the sibling callback, before another player process or rendered frame.
	var reveal_frames: Dictionary = player.get_backdrop_state().model_animation_frames
	var reveal_instances := palettes.keys().filter(func(node):
		var name: String = node.get_model_report(0).get("name", "")
		return reveal_frames.keys().any(func(path): return str(path).to_lower().ends_with("|" + name.to_lower())))
	# tolerance: retail presentation data determines how many animated View3D instances remain on reveal.
	check(not reveal_instances.is_empty() and reveal_instances.all(func(node): return matches_apt_pose(node)), "late sibling reveal uploads current APT poses before first rendered frame")
	late.queue_free()
	player.auto_process = true
	await settle()
	player.auto_process = false
	# tolerance: at least one real animated clip must upload its progressed pose after reveal.
	check(palettes.keys().any(func(node): return node.get_instance_palette(0) != palettes[node]), "visible ordinary player processing resumes pose uploads")
	# Ordinary processing advances retail movies: some old background clips may have left the canvas.
	# Derive expected membership from retained APT slots, never from the modes being tested.
	active = current_membership(views)
	modes(SubViewport.UPDATE_ALWAYS, active, "ordinary player reveal resumes retained viewports")

	# Change one retained native clip without removing its level: the real partial-canvas path.
	var top: int = ring_level
	var partial_before: int = player.get_stats().partial_frames
	check(player.set_clip_property(top, "EvilMovie.Evil3d.EvilW3D", "_alpha", 50.0), "change one ring placeholder")
	player.render(true)
	# tolerance: a retail movie can split the changed clip into multiple native canvas segments.
	check(player.get_stats().partial_frames > partial_before, "partial retained canvas path exercised")
	modes(SubViewport.UPDATE_ALWAYS, active, "partial redraw preserves changed and retained views")
	check(player.set_clip_property(top, "EvilMovie.Evil3d.EvilW3D", "_alpha", 100.0), "restore ring placeholder")
	player.render(true)
	# Removing a whole level requires a full canvas rebuild; its old slots must stop while others remain.
	player.set_level_visible(top, false)
	player.render(true)
	var retained := current_membership(views)
	modes(SubViewport.UPDATE_ALWAYS, retained, "remaining APT slots keep both viewport partners enabled")
	# tolerance: data-dependent clip count; at least one background pair stays while the ring pair stops.
	check(not retained.is_empty() and retained.size() < active.size(), "removed level stops ring views and keeps background")
	for view in active:
		if not retained.has(view):
			check(view.render_target_update_mode == SubViewport.UPDATE_DISABLED, "removed ring layer is disabled")
	player.set_level_visible(top, true)
	player.render(true)
	modes(SubViewport.UPDATE_ALWAYS, active, "partial restore reuses active slots")
	root.remove_child(parent)
	modes(SubViewport.UPDATE_DISABLED, views, "tree exit disables retained scenes")
	root.add_child(parent)
	modes(SubViewport.UPDATE_ALWAYS, active, "tree reentry restores retained scenes")
	for level in range(64):
		player.unload_level(level)
	player.render(true)
	modes(SubViewport.UPDATE_DISABLED, views, "empty level stack clears every View3D")
	# A second real player creates its scenes while already hidden, then is freed while visible.
	parent.free()
	parent = Node2D.new()
	parent.hide()
	root.add_child(parent)
	player = ClassDB.instantiate("AptMenuPlayer")
	player.auto_process = false
	parent.add_child(player)
	if not check(player.boot_shell(fs, world, config).ok, "second shell boots beneath hidden ancestor"):
		finish()
		return
	for frame in range(6):
		player.tick(0.033)
	var hidden_background: int = player.get_backdrop_state().background_level
	check(player.shell_invoke(hidden_background, "ShowInGameBackground", PackedStringArray()).get("ok", false), "hidden shell opens background scene")
	check(player.end_game_screen({"kind": "show_end_game", "evil": true, "text": "GUI:Defeated"}).get("ok", false), "hidden shell opens ring scene")
	for frame in range(30):
		player.tick(0.033)
		player.render(true)
	var hidden_views: Array[SubViewport] = []
	for child in player.get_children():
		if child is SubViewport:
			hidden_views.append(child)
	modes(SubViewport.UPDATE_DISABLED, hidden_views, "scenes created hidden start disabled")
	parent.show()
	var visible_views: Array[SubViewport] = []
	for view in hidden_views:
		if view.render_target_update_mode == SubViewport.UPDATE_ALWAYS:
			visible_views.append(view)
	# tolerance: mounted presentation data chooses the number of currently used paired clips.
	check(visible_views.size() >= 2 and visible_views.size() % 2 == 0, "new hidden scenes reveal as active pairs")
	# Parent deletion frees active viewports before the retained-canvas destructor runs.
	finish()
