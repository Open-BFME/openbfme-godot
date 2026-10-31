## Lane AIDATA-1: the AIData fixes driven through the real game (a skirmish started by --auto through the real lobby).
## game.gd runs it with --aidata1=<scenario>[,<scenario>...]; every step prints an "AIDATA1 ..." line, screenshots go to --screens as aidata1-<name>.png
## and "AIDATA1 RESULT: <n> ok, <m> failed" ends the run. Objects are placed with the logged test hooks (create_object, auto-acquire off).
##   rundown   Knights of Dol Amroth (a cavalry horde) ordered to attack an enemy Mountain Troll that is ordered to walk away: with AIData's
##             ChaseFromBehindLimit run-down the riders hit the troll while it is still walking (before it stops)
extends "res://scripts/spell3_player.gd"


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	faction = str(game._faction) if "_faction" in game else ""
	await _run_scenarios(scenarios)


func shot(name: String) -> void:
	game._save_named("aidata1-%s" % name)


## the members of a horde (or the object itself when it has none)
func members_of(id: int) -> Array:
	var d: Dictionary = world.get_object(id)
	var m: Array = d.get("members", [])
	return m if not m.is_empty() else [id]


func enemy_index() -> int:
	for p in world.get_economy().get("players", []):
		if p.playable and p.name == enemy_name():
			return int(p.index)
	return -1


func enemy_name() -> String:
	for p in world.get_economy().get("players", []):
		if p.playable and p.name != game._local_name:
			return p.name
	return ""


## the neutral creeps' player (no skirmish AI commands it, hostile to everybody) or -1
func creeps_index() -> int:
	for p in world.get_economy().get("players", []):
		if not p.playable and str(p.name).to_lower().contains("creep"):
			return int(p.index)
	return -1


func _scenario_rundown() -> String:
	var base := base_position()
	# the troll belongs to the creeps' player: the skirmish AI of a real enemy would take it over and walk it somewhere else
	var foe := creeps_index()
	if foe < 0:
		foe = enemy_index()
	if base == Vector2() or foe < 0:
		return "no base or no enemy player"
	print("AIDATA1 rundown: the troll's player index %d" % foe)
	# (the hook is refused in a started skirmish; both sides hold explicit orders below, so auto-acquire does not matter)
	print("AIDATA1 rundown: TEST HOOK auto-acquire off: %s" % str(world.debug_set_auto_acquire(false)))
	# open ground near the base: a candidate spot that is water, cliff or walled in is dropped when a probe soldier ordered along the chase line does not walk
	var max_hp := 0.0
	var goal := Vector2()
	var troll := -1
	var knights := -1
	var spot := Vector2()
	for k in 16:
		var dir := Vector2.from_angle(TAU * float(k) / 16.0 + 0.3)
		spot = base + dir * (700.0 + 150.0 * float(k % 3))
		goal = spot + dir * 1100.0
		var probe := int(world.create_object("MordorFighter", foe, spot.x, spot.y, dir.angle()))
		hud.camera_look_at(spot)
		await game._wait_seconds(1.0)
		var before := Vector2(float(world.get_object(probe).get("x", 0.0)), float(world.get_object(probe).get("y", 0.0)))
		world.order_move([probe], goal.x, goal.y)
		await game._wait_seconds(1.5)
		var after := Vector2(float(world.get_object(probe).get("x", 0.0)), float(world.get_object(probe).get("y", 0.0)))
		var walked := before.distance_to(after)
		world.debug_damage_object(probe, 1.0e9, 0)
		print("AIDATA1 rundown: candidate %d at (%.0f, %.0f): the probe walked %.0f" % [k, spot.x, spot.y, walked])
		if walked < 25.0:
			continue
		var t_at := spot + dir * 150.0
		troll = int(world.create_object("MordorMountainTroll", foe, t_at.x, t_at.y, dir.angle()))
		knights = int(world.create_object("GondorKnightsofDolHorde", int(game._local_index), spot.x, spot.y, dir.angle()))
		print("AIDATA1 TEST HOOK create troll %d (player %d) knights %d at (%.0f, %.0f), heading %.2f rad" % [troll, foe, knights, spot.x, spot.y, dir.angle()])
		if troll <= 0 or knights <= 0:
			return "the objects were not created"
		hud.camera_look_at(spot + dir * 150.0)
		await game._wait_seconds(1.5)
		max_hp = float(world.get_object(troll).get("max_health", 0.0))
		# the walk-away and the attack order in one go (the riders must still see the troll: 150 apart)
		world.order_move([troll], goal.x, goal.y)
		break
	if troll <= 0:
		return "no open spot found where a soldier walks"
	print("AIDATA1 rundown: troll walking to (%.0f, %.0f), video frame %d" % [goal.x, goal.y, Engine.get_frames_drawn()])
	var at: Dictionary = world.order_attack([knights], troll) # the horde itself is ordered, as the retail test does
	print("AIDATA1 rundown: knights ordered to attack: %s" % str(at))
	await game._wait_seconds(0.5)
	var f0 := int(world.get_frame())
	var hit_frame := -1
	var troll_moving_at_hit := false
	var shot_done := false
	var last_log := -100
	var steps := 0
	var last_frame := f0
	while steps < 6000:
		await game.get_tree().process_frame
		steps += 1
		if int(world.get_frame()) == last_frame:
			continue # one pass per logic frame
		last_frame = int(world.get_frame())
		var td: Dictionary = world.get_object(troll)
		if td.is_empty():
			break
		var tp := Vector2(float(td.get("x", 0.0)), float(td.get("y", 0.0)))
		var kp := Vector2()
		var ms := members_of(knights) # the riders' mean position (the horde object itself stays where it was made)
		for m in ms:
			var kd: Dictionary = world.get_object(int(m))
			kp += Vector2(float(kd.get("x", 0.0)), float(kd.get("y", 0.0))) / float(ms.size())
		hud.camera_look_at((tp + kp) * 0.5)
		var f := int(world.get_frame()) - f0
		var hp := float(td.get("health", max_hp))
		if f - last_log >= 5:
			last_log = f
			print("AIDATA1 rundown: frame +%d troll (%.0f, %.0f) health %.0f/%.0f, knight (%.0f, %.0f), gap %.0f, troll to goal %.0f" % [f, tp.x, tp.y, hp, max_hp, kp.x, kp.y, tp.distance_to(kp), tp.distance_to(goal)])
		if hit_frame < 0 and hp < max_hp:
			hit_frame = f
			troll_moving_at_hit = tp.distance_to(goal) > 60.0
			print("AIDATA1 rundown: FIRST HIT at frame +%d, troll at (%.0f, %.0f), %.0f from its goal (still walking: %s)" % [f, tp.x, tp.y, tp.distance_to(goal), str(troll_moving_at_hit)])
			await game._wait_seconds(0.4)
			shot("rundown")
			shot_done = true
		if shot_done and f > hit_frame + 240:
			break
		if hit_frame < 0 and f > 250:
			break
	print("AIDATA1 rundown: first hit at frame +%d" % hit_frame)
	await game._wait_seconds(1.0)
	if hit_frame < 0:
		return "the riders never hit the fleeing troll"
	if not troll_moving_at_hit:
		return "the first hit came only after the troll had stopped"
	return ""
