## Lane SELECT-1: the owner's report (10-22 nightly) through the real input: "if I sometimes select one unit in a battalion it doesn't select the whole
## battalion at all", and right-click attacks "feel off". game.gd runs it with --auto --faction=FactionMen --select1=clicks.
##   clicks  four of our battalions (Gondor fighters, archers, tower guards, Rohirrim): left clicks inside the soldiers' pick boxes beside their bodies (the box
##           RotWK's pick tests, RW 0x59C680: InGameHud.pick_box_pixels) must select the soldier's whole battalion; then right clicks beside enemy soldiers
##           of two enemy battalions (Mordor orcs, goblins) created then, with our fighters / archers selected, must order an attack (MSG_DO_ATTACK_OBJECT), not a move. Prints every click and "SELECT1 clicks: ok / FAIL <why>".
extends "res://scripts/play1_player.gd"


func player_index(name: String) -> int:
	for p in world.get_economy().players:
		if p.name == name:
			return int(p.index)
	return -1


func enemy_name() -> String:
	for p in world.get_economy().players:
		if p.playable and p.name != game._local_name:
			return p.name
	return ""


var last_probe := {}


func members_of(horde: int) -> Array:
	return world.get_object(horde).get("members", [])


# a click at point k of the soldier's pick box (0 the centre, 1 / 2 its -X / +X side, ...), the pixel taken again just before the press (the soldier moves)
func click_soldier(member: int, k: int, button: MouseButton) -> bool:
	# a soldier still walking into its rank moves off the box edge between the pixel and the click's release: wait until it stands (up to 4 s)
	for i in 120:
		if not bool(world.get_object(member).get("moving", false)):
			break
		await game._step(1)
	var pts: PackedVector2Array = hud.pick_box_pixels(member)
	if pts.size() <= k:
		return false
	await move_to(pts[k])
	pts = hud.pick_box_pixels(member)
	if pts.size() <= k:
		return false
	var p := pts[k]
	last_probe = hud.pick_probe(p, member) # the pick at the pixel just before the press (printed for a missed click)
	press(p, button, true)
	await game.get_tree().process_frame
	press(p, button, false)
	await game._step(3)
	return true


func _scenario_clicks() -> String:
	var spot := Vector2()
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			spot = Vector2(o.x, o.y)
			break
	var enemy := player_index(enemy_name())
	if enemy < 0:
		return "no enemy player"
	var centre := spot + Vector2(0, 300)
	var ours := [
		world.create_object("GondorFighterHorde", game._local_index, centre.x - 75, centre.y, 0.0),
		world.create_object("GondorArcherHorde", game._local_index, centre.x + 75, centre.y, 0.0),
		world.create_object("GondorTowerShieldGuardHorde", game._local_index, centre.x - 225, centre.y, 0.0),
		world.create_object("RohanRohirrimHorde", game._local_index, centre.x + 225, centre.y, 0.0)]
	for id in ours:
		if int(id) <= 0:
			return "a battalion could not be created: %s" % str(ours)
	await game._step(40)
	hud.camera_look_at(centre + Vector2(0, 10))
	await game._step(30)
	var failures: PackedStringArray = []
	var clicks := 0
	var missed := 0
	# 1. left clicks beside the bodies of soldiers of every battalion, alternating, so each click changes the selection
	for round in 3:
		for horde in ours:
			var ms: Array = members_of(horde)
			if ms.size() <= round:
				continue
			var member: int = ms[ms.size() - 1 - round]
			for k in [1, 2]:
				if not await click_soldier(member, k, MOUSE_BUTTON_LEFT):
					failures.append("soldier %d of %d has no pick box" % [member, horde])
					continue
				await game._step(20)
				var sel: Array = hud.get_selection()
				clicks += 1
				var ok := sel.size() == 1 and int(sel[0]) == int(horde)
				missed += 0 if ok else 1
				print("SELECT1 click: soldier %d of battalion %d (%s), box point %d -> selection %s %s" % [member, horde, world.get_object(horde).get("template", "?"), k,
					str(sel), "ok" if ok else "MISSED, probe %s, messages %s" % [JSON.stringify(last_probe), str(hud.get_message_log().slice(-4))]])
	print("SELECT1 clicks: %d of %d left clicks beside a soldier's body missed its battalion" % [missed, clicks])
	if missed > 0:
		failures.append("%d of %d left clicks missed the battalion" % [missed, clicks])
	# 2. right clicks beside enemy soldiers: our fighters on the orcs, our archers on the goblins (each battalion picked by a left click on a soldier
	# first; one right click per enemy battalion, before the melee mixes the soldiers: a nearer soldier of ours in front would take the click, as in RotWK)
	# the enemy battalions come now, in front of the fighters and the archers (created earlier they would already be fighting)
	var theirs := [
		world.create_object("MordorFighterHorde", enemy, centre.x - 90, centre.y + 190, PI),
		world.create_object("GoblinFighterHorde", enemy, centre.x + 90, centre.y + 190, PI)]
	for id in theirs:
		if int(id) <= 0:
			return "an enemy battalion could not be created: %s" % str(theirs)
	await game._step(20)
	var attacks0 := log_count("MSG_DO_ATTACK_OBJECT")
	var moves0 := log_count("MSG_DO_MOVETO")
	var rights := 0
	for i in 2:
		var a: Dictionary = world.get_object(ours[i])
		var b: Dictionary = world.get_object(theirs[i])
		hud.camera_look_at(Vector2((float(a.get("x", 0.0)) + float(b.get("x", 0.0))) * 0.5, (float(a.get("y", 0.0)) + float(b.get("y", 0.0))) * 0.5))
		await game._step(15)
		await click_soldier(members_of(ours[i])[0], 0, MOUSE_BUTTON_LEFT)
		await game._step(8)
		var ms: Array = members_of(theirs[i]).filter(func(m): return hud.pick_box_pixels(m).size() == 7)
		if ms.is_empty() or not await click_soldier(ms[0], 1, MOUSE_BUTTON_RIGHT):
			failures.append("no enemy soldier of battalion %d (%s) shows its whole pick box on screen" % [theirs[i], str(Vector2(float(b.get("x", 0.0)), float(b.get("y", 0.0))))])
			continue
		rights += 1
		await game._step(25)
		print("SELECT1 right click: battalion %s selected, enemy soldier %d of %d -> attack orders %d, move orders %d, probe %s" % [str(hud.get_selection()), ms[0],
			theirs[i], log_count("MSG_DO_ATTACK_OBJECT") - attacks0, log_count("MSG_DO_MOVETO") - moves0, JSON.stringify(last_probe)])
	var attacks := log_count("MSG_DO_ATTACK_OBJECT") - attacks0
	var moves := log_count("MSG_DO_MOVETO") - moves0
	print("SELECT1 right clicks: %d on enemy soldiers -> %d attack orders, %d move orders" % [rights, attacks, moves])
	if attacks != rights or moves != 0:
		failures.append("%d right clicks on enemy soldiers gave %d attacks and %d moves" % [rights, attacks, moves])
	# the fight the last order started
	await game._step(150)
	return "; ".join(failures)
