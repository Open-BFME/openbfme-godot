## Lane BANNER-1: the owner's "banner carriers don't auto replace units if they are lvl 2", played in the real client. game.gd runs it with --banner1=refill
## (with --auto for a skirmish); it prints "BANNER1 refill: ok" or "BANNER1 refill: FAIL <why>" and its measured lines; shots go to --screens as
## play1-banner1-*.png.
##   refill   a rank-2 Gondor soldier horde (its banner carrier joins at rank two, BannerCarrierMinLevel 1) fights a Mordor orc horde of the other player with
##            auto-acquire on and loses members; once the orcs are dead it stands idle, and its banner carrier makes one member every IdleSpawnRate after
##            the combat windows (BannerCarrierUpdate RW 0x89ACE4, HordeContain::attackedWithin RW 0x86EE52) until the horde is full again; each new member
##            arrives at the horde's rank (RW 0x873AE3's experience pool). The staging (units placed, the kills that earn the rank) uses the GameWorld test hooks.
extends "res://scripts/devport7_player.gd"


## the object's ExperienceTracker rank (GameWorld.get_ability_state's "level")
func _rank(id: int) -> int:
	return int(world.get_ability_state(id).get("level", -1))


## losses: as refill without the orcs: five soldiers are struck down (a logged test hook), then the carrier refills the horde (the clips' variant: a skirmish
## map's other armies can reach the spot during a slow movie capture)
func _scenario_losses() -> String:
	return await _refill(false)


func _scenario_refill() -> String:
	return await _refill(true)


func _refill(fight: bool) -> String:
	await _headless_window()
	var fortress := own_named("Fortress")
	if fortress.is_empty():
		fortress = own_named("Citadel")
	if fortress.is_empty():
		return "no fortress of the local player"
	var enemy := -1
	for pl in world.get_economy().players:
		if pl.playable and str(pl.name) != game._local_name and enemy < 0:
			enemy = int(pl.index)
	if enemy < 0:
		return "no other playable player"
	var base := quiet_spot(Vector2(fortress.x, fortress.y))
	await look_by_radar(base + Vector2(60, 0))
	var horde := place("GondorFighterHorde", base)
	if horde <= 0:
		return "GondorFighterHorde could not be placed"
	await frames(5)
	# rank two the way a horde earns it: kills credited to one of its soldiers (the horde's experience pool, which a new member takes, RW 0x873AE3);
	# the victims, orcs of the other player, stand far away
	var killer: int = payload(horde)[0]
	var batches := 0
	while _rank(horde) < 2 and batches < 40:
		var victims: Array = []
		for i in 10:
			victims.append(int(world.create_object("MordorFighter", enemy, base.x, base.y + 1500.0, 0.0)))
		await frames(1)
		for v in victims:
			if v > 0:
				world.debug_damage_object(v, 100000.0, killer)
		await frames(1)
		batches += 1
	print("BANNER1 refill: horde %d rank %d after %d batches of credited kills" % [horde, _rank(horde), batches])
	if _rank(horde) != 2:
		return "the horde did not reach rank 2 through kills (rank %d)" % _rank(horde)
	var t := 0
	while banner_of(horde) == 0 and t < 150:
		await frames(1)
		t += 1
	if banner_of(horde) == 0:
		return "the rank-2 horde got no banner carrier in 150 frames"
	var full := payload(horde).size()
	await frames(10)
	shot("banner1-before-fight")
	var foes: Array = []
	for k in (1 if fight else 0):
		var foe := int(world.create_object("MordorFighterHorde", enemy, base.x + 140.0, base.y, PI))
		if foe <= 0:
			return "MordorFighterHorde could not be placed for player %d: %s" % [enemy, str(world.get_report().get("command_errors", []))]
		foes.append(foe)
	var f0: int = world.get_frame()
	var alive := foes.size()
	while alive > 0 and world.get_frame() - f0 < 1500:
		await frames(5)
		alive = 0
		for foe in foes:
			if obj(foe).get("ok", false):
				alive += 1
		if (world.get_frame() - f0) % 50 < 5:
			print("BANNER1 refill: fighting, frame %d: soldiers %d, orc hordes standing %d" % [world.get_frame(), payload(horde).size(), alive])
	# the clip shows several replacements: when the fight cost fewer than five men, more are struck down (a logged test hook; the refill is the same)
	var staged := 0
	while payload(horde).size() > full - 5 and payload(horde).size() > 1:
		hit(payload(horde)[0], 100000.0)
		staged += 1
		await frames(1)
	if staged > 0:
		print("BANNER1 refill: %d more soldiers struck down by the test hook (the fight cost %d)" % [staged, full - payload(horde).size() - staged])
	var left := payload(horde).size()
	print("BANNER1 refill: the fight took %d frames; members %d -> %d (banner %d)" % [world.get_frame() - f0, full, left, banner_of(horde)])
	shot("banner1-after-fight")
	if fight and alive > 0:
		return "%d orc hordes still stand after 1500 frames" % alive
	if left >= full:
		return "the fight cost no member (%d of %d): nothing to refill" % [left, full]
	var f1: int = world.get_frame()
	var seen := {}
	for m in payload(horde):
		seen[m] = true
	var ranks: Array = []
	while payload(horde).size() < full and world.get_frame() - f1 < 900:
		await frames(5)
		for m in payload(horde):
			if not seen.has(m):
				seen[m] = true
				ranks.append(_rank(m))
				print("BANNER1 refill: member %d joined at frame %d (+%d), rank %d" % [m, world.get_frame(), world.get_frame() - f1, _rank(m)])
	var back := payload(horde).size()
	shot("banner1-refilled")
	print("BANNER1 refill: members %d -> %d after %d frames idle; new members' ranks %s" % [left, back, world.get_frame() - f1, str(ranks)])
	if back != full:
		return "the banner carrier did not refill the horde (%d -> %d -> %d in %d frames)" % [full, left, back, world.get_frame() - f1]
	for r in ranks:
		if r != 2:
			return "a new member arrived at rank %d, not the horde's 2 (%s)" % [r, str(ranks)]
	await frames(20)
	return ""
