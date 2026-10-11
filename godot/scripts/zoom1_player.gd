## Lane ZOOM-1: the Max zoom-out and the always-on health bars, scripted (game.gd --auto --zoom1=<scenario>[,<scenario>...]). Each scenario prints
## "ZOOM1 <scenario>: ok" or "ZOOM1 <scenario>: FAIL <why>"; screenshots go to --screens as zoom1-<name>.png; "ZOOM1 RESULT: <n> ok, <m> failed" ends the run.
## With --warp=<seconds> the game first runs that much game time fast (--opponents=7 on an 8-player map: a late game with the computers' armies).
##   zoom   the camera at the local start (after `bars`: over the two armies) and at the map's corner, at 1x / 1.5x / 2x / 2.5x / 3x retail's highest camera (the Max zoom-out set to 3 for the
##          run, the height set as the wheel sets it): the screenshots, the camera's far plane and fog shift, how far the far plane reaches beyond the
##          ground at the top of the view and the black share; fails when the far plane cuts the ground off (a hole) at any factor
##   perf   the render frame time at 1x and at 2.5x over the same spot (the local start, or the armies of `bars` when it ran first), alternating 1x, 2.5x,
##          1x, 2.5x: mean / p95 / fps over --zoom1-seconds (default 6) each, and the mean of each factor; run it with --vsync=off for the uncapped figures
##          (headless: the dummy renderer, so the figures are the CPU side only)
##   bars   both armies in view (a test hook creates them: the local player's soldiers, Rohirrim, a hero and a barracks; the enemy's orcs, a troll and an
##          orc pit), the Health bars option "Always (all players)" (OpenBFMEHealthBars always) with AllHealthBars off, nothing selected: every unit's and
##          building's bar is drawn (the HUD's icon list against the objects on the screen); a second picture with retail's setting has none; an enemy
##          horde made out of the local player's sight but on the screen at 2.5x has none (r2)
extends RefCounted

var game: Node
var hud: Node
var world: Node
var ok_count := 0
var fail_count := 0
var _created: Array = []
var _bars_spot := Vector2()

const FACTORS := [1.0, 1.5, 2.0, 2.5, 3.0]


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	if game._warp > 0.0:
		# --warp (lane PERF-1's fast forward): a late game first, the AIs' armies and bases built
		var w0 := Time.get_ticks_msec()
		world.set_time_scale(game._warp_scale)
		while world.get_frame() < int(game._warp * 5.0):
			await game._step(30)
		world.set_time_scale(1.0)
		print("ZOOM1 warp: logic frame %d after %.1f s, %d objects" % [world.get_frame(), (Time.get_ticks_msec() - w0) / 1000.0,
			world.get_object_count() if world.has_method("get_object_count") else -1])
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("ZOOM1 %s: ok" % s)
		else:
			fail_count += 1
			print("ZOOM1 %s: FAIL %s" % [s, r])
	print("ZOOM1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


func shot(name: String) -> void:
	game._save_named("zoom1-" + name)


func _vp() -> Viewport:
	return game.get_viewport()


func _local_index() -> int:
	for p in world.get_economy().get("players", []):
		if p.name == game._local_name:
			return int(p.index)
	return -1


func _enemy() -> Dictionary:
	for p in world.get_economy().get("players", []):
		if p.playable and p.name != game._local_name:
			return p
	return {}


func _base_of(player: String) -> Vector2:
	for o in world.get_player_objects(player):
		if o.get("commandcenter", false):
			return Vector2(o.x, o.y)
	return Vector2()


## the camera over `p` at `factor` times the map's highest camera, settled (the zoom follows the height with CameraAdjustSpeed every client frame)
func _view(p: Vector2, factor: float) -> Dictionary:
	hud.camera_look_at(p)
	var c: Dictionary = hud.get_camera()
	hud.camera_set_height(float(c.max_height) * factor)
	for i in 6:
		await game._step(10)
		hud.camera_look_at(p)
	await game._step(4)
	return hud.get_camera()


## the share of black pixels in the band above the Palantir (the top 70% of the window): the clear colour beyond the map and the shroud's black border
## (information: retail's border is black too)
func _hole_share() -> float:
	var img := _vp().get_texture().get_image()
	if img == null:
		return -1.0
	var w := img.get_width()
	var h := int(img.get_height() * 0.7)
	var black := 0
	var n := 0
	for y in range(0, h, 4):
		for x in range(0, w, 4):
			var c := img.get_pixel(x, y)
			n += 1
			if c.r < 0.01 and c.g < 0.01 and c.b < 0.01:
				black += 1
	return float(black) / float(maxi(n, 1))


## how far the far plane reaches beyond the ground seen at the top corners of the window (view depth; negative: the far clip cuts the ground off: a hole)
func _far_margin(ground: float) -> float:
	var cam: Camera3D = _vp().get_camera_3d()
	if cam == null:
		return -1.0e9
	var size := _vp().get_visible_rect().size
	var margin := 1.0e9
	for p in [Vector2(0, 0), Vector2(size.x - 1, 0), Vector2(size.x * 0.5, 0)]:
		var o := cam.project_ray_origin(p)
		var d := cam.project_ray_normal(p)
		if d.y >= -0.0001:
			return -1.0e9 # the top of the view sees the sky: the ground never ends there
		var t := (ground - o.y) / d.y
		var depth := t * d.dot(-cam.global_transform.basis.z)
		margin = minf(margin, cam.far - depth)
	return margin


func _scenario_zoom() -> String:
	var why := ""
	hud.set_view_options({"zoom_out_factor": 3.0})
	var start := _bars_spot if _bars_spot != Vector2() else _base_of(game._local_name)
	var lim: Vector4 = hud.get_camera().constraint
	var corner := Vector2(lim.x, lim.y)
	var spots := [["battle" if _bars_spot != Vector2() else "start", start], ["edge", corner]]
	var windowed := DisplayServer.get_name() != "headless"
	for spot in spots:
		for f in FACTORS:
			var c := await _view(spot[1], f)
			var dark := _hole_share() if windowed else -1.0
			var margin := _far_margin(float(c.ground_level))
			var name := "%s-%sx" % [spot[0], str(f)]
			shot(name)
			print("ZOOM1 zoom %s: height %.0f (max %.0f, limit %.0f), eye %s, far %.0f (%.0f beyond the farthest ground in view), fog shift %.0f, black share %.4f" % [
				name, c.height_above_ground, c.max_height, c.zoom_out_limit, str(c.eye), c.far, margin, c.fog_shift, dark])
			if absf(float(c.height_above_ground) - float(c.max_height) * f) > 0.5:
				why = "the camera did not reach %sx (height %.1f)" % [str(f), c.height_above_ground]
			if margin < 0.0:
				why = "%s: the far plane cuts the ground off %.0f before the top of the view" % [name, -margin]
	# back to the setting in effect
	hud.set_view_options({"zoom_out_factor": game._zoom_out_setting()})
	hud.camera_set_height(float(hud.get_camera().max_height))
	await game._step(30)
	return why


func _frame_times(seconds: float) -> Dictionary:
	var times: Array = []
	# r2: Godot's own measurement of the root viewport's rendering, per frame: the GPU time and the render thread's CPU time (what the far view costs,
	# apart from the game's per-frame work on the main thread)
	var vp_rid := _vp().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp_rid, true)
	var gpu := 0.0
	var rcpu := 0.0
	var t0 := Time.get_ticks_usec()
	var last := t0
	while Time.get_ticks_usec() - t0 < int(seconds * 1000000.0):
		await game.get_tree().process_frame
		var now := Time.get_ticks_usec()
		times.append((now - last) / 1000.0)
		last = now
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp_rid)
		rcpu += RenderingServer.viewport_get_measured_render_time_cpu(vp_rid)
	times.sort()
	var sum := 0.0
	for t in times:
		sum += t
	var n := times.size()
	return {"frames": n, "mean_ms": sum / maxf(n, 1), "p95_ms": times[mini(n - 1, int(n * 0.95))] if n > 0 else 0.0, "fps": n / seconds,
		"gpu_ms": gpu / maxf(n, 1), "render_cpu_ms": rcpu / maxf(n, 1)}


func _scenario_perf() -> String:
	var seconds := 6.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--zoom1-seconds="):
			seconds = a.substr(16).to_float()
	var spot := _bars_spot if _bars_spot != Vector2() else _base_of(game._local_name)
	hud.set_view_options({"zoom_out_factor": 3.0})
	# 1x and 2.5x twice, alternating (the game goes on meanwhile: a battle that ends between two windows moves both), then the mean of each
	var sums := {1.0: 0.0, 2.5: 0.0}
	var gpus := {1.0: 0.0, 2.5: 0.0}
	var rcpus := {1.0: 0.0, 2.5: 0.0}
	for f in [1.0, 2.5, 1.0, 2.5]:
		await _view(spot, f)
		await game._step(30)
		var r := await _frame_times(seconds)
		sums[f] += r.mean_ms / 2.0
		gpus[f] += r.gpu_ms / 2.0
		rcpus[f] += r.render_cpu_ms / 2.0
		print("ZOOM1 perf %sx: %d frames in %.0f s, %.1f fps, mean %.2f ms, p95 %.2f ms, GPU %.2f ms, render CPU %.2f ms, window %s, objects %d, display %s, %s" % [
			str(f), r.frames, seconds, r.fps, r.mean_ms, r.p95_ms, r.gpu_ms, r.render_cpu_ms, str(_vp().get_visible_rect().size),
			world.get_object_count() if world.has_method("get_object_count") else -1, DisplayServer.get_name(), RenderingServer.get_video_adapter_name()])
	print("ZOOM1 perf summary: frame 1x %.2f ms (%.1f fps), 2.5x %.2f ms (%.1f fps), 2.5x / 1x = %.2f; GPU 1x %.2f ms, 2.5x %.2f ms; render CPU 1x %.2f ms, 2.5x %.2f ms" % [
		sums[1.0], 1000.0 / maxf(sums[1.0], 0.001), sums[2.5], 1000.0 / maxf(sums[2.5], 0.001), sums[2.5] / maxf(sums[1.0], 0.001), gpus[1.0], gpus[2.5],
		rcpus[1.0], rcpus[2.5]])
	hud.set_view_options({"zoom_out_factor": game._zoom_out_setting()})
	hud.camera_set_height(float(hud.get_camera().max_height))
	await game._step(30)
	return ""


func _make(template: String, player: int, p: Vector2, angle: float) -> int:
	var id: int = world.create_object(template, player, p.x, p.y, angle)
	print("ZOOM1 TEST HOOK: create_object %s for player %d at (%.0f, %.0f) -> %d" % [template, player, p.x, p.y, id])
	if id > 0:
		_created.append(id)
	return id


func _scenario_bars() -> String:
	var me := _local_index()
	var enemy := _enemy()
	if me < 0 or enemy.is_empty():
		return "no local player or enemy"
	var base := _base_of(game._local_name)
	var dir := (_base_of(enemy.name) - base).normalized()
	if dir == Vector2():
		dir = Vector2(1, 0)
	var side := Vector2(-dir.y, dir.x)
	var spot := base + dir * 520.0
	_bars_spot = spot
	var ang := dir.angle()
	var ei := int(enemy.index)
	var ids := {}
	ids["own soldiers"] = _make("GondorFighterHorde", me, spot - side * 120.0, ang)
	ids["own archers"] = _make("GondorArcherHorde", me, spot - side * 120.0 - dir * 90.0, ang)
	ids["own cavalry"] = _make("RohanRohirrimHorde", me, spot + side * 120.0, ang)
	ids["own hero"] = _make("GondorBoromir", me, spot - dir * 40.0, ang)
	ids["own barracks"] = _make("GondorBarracks", me, spot - side * 360.0 + dir * 40.0, ang)
	ids["enemy orcs"] = _make("MordorFighterHorde", ei, spot + dir * 200.0 - side * 120.0, ang + PI)
	ids["enemy archers"] = _make("MordorArcherHorde", ei, spot + dir * 290.0 - side * 120.0, ang + PI)
	ids["enemy troll"] = _make("MordorAttackTroll", ei, spot + dir * 200.0 + side * 120.0, ang + PI)
	ids["enemy pit"] = _make("MordorOrcPit", ei, spot + dir * 340.0 - side * 330.0, ang + PI)
	for k in ids:
		if int(ids[k]) <= 0:
			return "%s not created: %s" % [k, str(world.get_command_errors() if world.has_method("get_command_errors") else "")]
	await game._step(20)
	# some of them hurt, so the bars differ (the test hook of HUD-5's levels scenario)
	for hurt in [["own hero", 0.45], ["enemy troll", 0.7], ["enemy pit", 0.5], ["own barracks", 0.25]]:
		var o: Dictionary = world.get_object(int(ids[hurt[0]]))
		var amount := float(o.get("max_health", o.get("health", 0.0))) * float(hurt[1])
		var r: Dictionary = world.debug_damage_object(int(ids[hurt[0]]), amount, 0)
		print("ZOOM1 TEST HOOK: debug_damage_object %s %.0f -> %s" % [hurt[0], amount, JSON.stringify(r)])
	# the units of every horde: each one wants its bar
	var want := {}
	for k in ids:
		var o: Dictionary = world.get_object(int(ids[k]))
		var members: Array = o.get("members", [])
		if members.is_empty():
			members = [int(ids[k])]
		for m in members:
			# r2: an object out of the local player's sight (status above PARTIAL_CLEAR, RW 0x4853EB) rightly has none: only the seen ones want a bar
			if int(world.get_object_shroud_status(int(m))) <= 2:
				want[int(m)] = k
	hud.set_view_options({"zoom_out_factor": 3.0, "health_bar_mode": "retail", "all_health_bars": false})
	var c := await _view(spot + dir * 100.0 - side * 90.0, 2.0)
	await game._step(10)
	var retail: Dictionary = hud.get_icon_ui()
	shot("bars-retail")
	print("ZOOM1 bars retail (nothing selected): %d bars" % int(retail.get("health_bars", 0)))
	hud.set_view_options({"health_bar_mode": "always", "all_health_bars": false})
	await game._step(10)
	var always: Dictionary = hud.get_icon_ui()
	# r2: at this picture, the made objects still alive and in the local player's sight (out of sight: no bar, RW 0x4853EB)
	var in_sight := {}
	for id in want:
		var o: Dictionary = world.get_object(int(id))
		if o.get("ok", false) and float(o.get("health", 0.0)) > 0.0 and int(world.get_object_shroud_status(int(id))) <= 2:
			in_sight[id] = true
	shot("bars-always")
	print("ZOOM1 bars always (all players): %d bars for %d units and buildings made in view, %d of them alive and in sight; camera height %.0f" % [
		int(always.get("health_bars", 0)), want.size(), in_sight.size(),
		c.height_above_ground])
	await _view(spot + dir * 100.0 - side * 90.0, 2.5)
	await game._step(10)
	var far: Dictionary = hud.get_icon_ui()
	shot("bars-always-2.5x")
	print("ZOOM1 bars always at 2.5x: %d bars" % int(far.get("health_bars", 0)))
	# lane ZOOM-1 r2: an enemy horde out of the local player's sight but on the screen shows no bar (RW 0x4853EB: status above PARTIAL_CLEAR, no icons)
	var unseen_why := ""
	var size := _vp().get_visible_rect().size
	var unseen_at := Vector2()
	for k in range(15, 5, -1): # the farthest spot on the screen first (deepest out of sight)
		for sd in [0.0, -1.0, 1.0]:
			var p: Vector2 = spot + dir * (100.0 * k) + side * (250.0 * sd)
			var px: Vector2 = hud.world_to_pixel(p)
			if unseen_at == Vector2() and int(world.get_shroud_status_at(p.x, p.y)) >= 1 and px.x > 40 and px.y > 40 and px.x < size.x - 40 and px.y < size.y * 0.6:
				unseen_at = p
	var unseen_members: Array = []
	if unseen_at == Vector2():
		unseen_why = "no spot out of sight on the screen at 2.5x"
	else:
		var hid := _make("MordorFighterHorde", ei, unseen_at, ang + PI)
		await game._step(20)
		unseen_members = world.get_object(hid).get("members", [])
		var hidden_ui: Dictionary = hud.get_icon_ui()
		shot("bars-always-unseen-enemy")
		var statuses := []
		var barred_ids := {}
		for id in hidden_ui.get("bar_objects", []):
			barred_ids[int(id)] = true
		# per member: out of sight (status above PARTIAL_CLEAR) no bar; a member at the edge of the sight may be seen and then has one
		var unseen_n := 0
		var leaked := 0
		var seen_shown := 0
		for m in unseen_members:
			var st := int(world.get_object_shroud_status(int(m)))
			statuses.append(st)
			if st > 2:
				unseen_n += 1
				leaked += 1 if barred_ids.has(int(m)) else 0
			elif barred_ids.has(int(m)):
				seen_shown += 1
		print("ZOOM1 bars: an enemy horde made out of sight at (%.0f, %.0f), on the screen at %s: object shroud statuses %s; %d members out of sight, %d of them with a bar; %d seen members with a bar" % [
			unseen_at.x, unseen_at.y, str(hud.world_to_pixel(unseen_at)), str(statuses), unseen_n, leaked, seen_shown])
		if unseen_n < 5:
			unseen_why = "only %d members of the test horde are out of sight: the check proves too little" % unseen_n
		elif leaked > 0:
			unseen_why = "%d bars over enemy units the local player does not see" % leaked
	# back to the options in effect
	if game._game_options != null:
		hud.set_view_options(game._game_options.view_options(game._game_options.current()))
	if int(retail.get("health_bars", 0)) != 0:
		return "retail's setting drew %d bars with nothing selected" % int(retail.get("health_bars", 0))
	var barred := {}
	for id in always.get("bar_objects", []):
		barred[int(id)] = true
	var missing := []
	for id in want:
		if in_sight.has(id) and not barred.has(id):
			missing.append("%s %d" % [want[id], id])
	if not missing.is_empty():
		return "Always drew no bar over %s (%d bars)" % [", ".join(missing), int(always.get("health_bars", 0))]
	return unseen_why
