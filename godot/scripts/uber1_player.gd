## Lane UBER-1: the Uber performance mode's game gate (docs/UBER.md), scripted: game.gd --auto --map=<an 8-player map> --uber1=<troops>
## [--uber1-seconds=S] [--options-file=F with OpenBFMEUberPerformance = yes / no]. One count per run (a second battle would be made on top of the
## first: run the game again for each count). In the real client (the logic on its worker, the
## replay recording and its per-frame hash, drawables, terrain, HUD): two armies of hordes made in the middle of the map, the local player's facing the
## opponent's, until GameWorld.get_census() counts at least that many living troops (horde members one by one); every horde ordered to attack the
## horde facing it (GameWorld.order_attack: the player command path); 3 s to settle, then the render frames are timed for S seconds (default 20).
## Prints per count "UBER1 battle <n>: ..." with the average fps, the 1% low (the fps of the 99th percentile frame), the worst frame, the logic frames
## run per second (5 = the logic kept its clock), the living troops at the start and the end of the window, the profile and the renderer; the
## owner's bar is 144 fps with the 1% low at 144 or more (the line says met / missed: a measurement, not a failure). "UBER1 RESULT: <n> ok, <m>
## failed" ends the run (a failure is a battle that could not be made or measured).
extends RefCounted

var game: Node
var hud: Node
var world: Node
var ok_count := 0
var fail_count := 0

const HORDES := ["GondorFighterHorde", "GondorFighterHorde", "GondorTowerShieldGuardHorde", "GondorArcherHorde"]
const FOES := ["MordorFighterHorde", "MordorFighterHorde", "MordorFighterHorde", "MordorArcherHorde"]
const SPACING := 75.0
const GAP := 160.0
const MARGIN := 200.0


func run(p_game: Node, troops: int) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	var seconds := 20.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--uber1-seconds="):
			seconds = a.substr(16).to_float()
	var r: String = await _battle(troops, seconds)
	if r == "":
		ok_count += 1
	else:
		fail_count += 1
		print("UBER1 battle %d: FAIL %s" % [troops, r])
	print("UBER1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


func _local_index() -> int:
	for p in world.get_economy().get("players", []):
		if p.name == game._local_name:
			return int(p.index)
	return -1


func _enemy_index() -> int:
	for p in world.get_economy().get("players", []):
		if p.playable and p.name != game._local_name:
			return int(p.index)
	return -1


func _battle(troops: int, seconds: float) -> String:
	var me := _local_index()
	var foe := _enemy_index()
	if me < 0 or foe < 0:
		return "no local player or opponent"
	var census: Dictionary = world.get_census()
	if not census.has("extent_x"):
		return "no census / extent (no game)"
	var cx: float = float(census.extent_x) * 0.5
	var cy: float = float(census.extent_y) * 0.5
	var rows := int((float(census.extent_y) - 2.0 * MARGIN) / SPACING)
	var base_troops: int = int(census.troops)
	var west: Array = []
	var east: Array = []
	var t0 := Time.get_ticks_msec()
	var column := 0
	var made := 0
	while made < troops:
		var dx := GAP + SPACING * column
		if cx - dx < MARGIN:
			return "%d troops do not fit the map (%d made)" % [troops, made]
		for r in range(rows):
			var y := MARGIN + SPACING * r + float(column % 2) * SPACING * 0.5
			var a: int = world.create_object(HORDES[(r + column) % 4], me, cx - dx, y, 0.0)
			var b: int = world.create_object(FOES[(r + column) % 4], foe, cx + dx, y, PI)
			if a <= 0 or b <= 0:
				return "create_object failed: %s" % [str(world.get_command_errors()) if world.has_method("get_command_errors") else "?"]
			west.append(a)
			east.append(b)
			if west.size() % 16 == 0:
				made = int(world.get_census().troops) - base_troops
				if made >= troops:
					break
		made = int(world.get_census().troops) - base_troops
		column += 1
	var spawn_s := (Time.get_ticks_msec() - t0) / 1000.0
	await game._step(4)
	for i in range(west.size()):
		world.order_attack([west[i]], east[i])
		world.order_attack([east[i]], west[i])
	hud.camera_look_at(Vector2(cx, cy))
	await game._step(2)
	var settle := Time.get_ticks_msec()
	while Time.get_ticks_msec() - settle < 3000:
		await game.get_tree().process_frame
	# the window
	var start_census: Dictionary = world.get_census()
	var f0: int = world.get_frame()
	var times: Array = []
	var w0 := Time.get_ticks_usec()
	var last := w0
	while Time.get_ticks_usec() - w0 < int(seconds * 1000000.0):
		await game.get_tree().process_frame
		var now := Time.get_ticks_usec()
		times.append((now - last) / 1000.0)
		last = now
	var wall := (Time.get_ticks_usec() - w0) / 1000000.0
	var logic_frames: int = world.get_frame() - f0
	var end_census: Dictionary = world.get_census()
	if times.is_empty():
		return "no frames in the window"
	var sorted := times.duplicate()
	sorted.sort()
	var sum := 0.0
	for t in times:
		sum += t
	var n := times.size()
	var p99: float = sorted[mini(n - 1, int(n * 0.99))]
	var worst: float = sorted[n - 1]
	var avg_fps := n / wall
	var low_fps := 1000.0 / maxf(p99, 0.001)
	var met := avg_fps >= 144.0 and low_fps >= 144.0
	print("UBER1 battle %d: profile %s, living troops %d at the window's start, %d at its end (%d hordes made, spawn %.1f s); %d render frames in %.1f s: average %.1f fps (%.2f ms), 1%% low %.1f fps (p99 %.2f ms), worst frame %.2f ms; logic %.2f frames/s (5 = on its clock); window %s, renderer %s, %s; owner's bar 144 fps: %s" % [
		troops, world.simulation_profile(), int(start_census.troops) - base_troops, int(end_census.troops) - base_troops, west.size() + east.size(), spawn_s, n,
		wall, avg_fps, sum / n, low_fps, p99, worst, logic_frames / wall, str(game.get_viewport().get_visible_rect().size), DisplayServer.get_name(),
		RenderingServer.get_video_adapter_name(), "met" if met else "missed"])
	return ""
