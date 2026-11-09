## Lane OWNERFIX-2: the owner's playtest items of 2026-10-17 driven through the real game (a skirmish started by --auto through the real lobby).
## game.gd runs it with --ownerfix2=<scenario>[,<scenario>...]; every step prints an "OWNERFIX2 ..." line, screenshots go to --screens as
## ownerfix2-<name>.png and "OWNERFIX2 RESULT: <n> ok, <m> failed" ends the run. Objects are placed with the logged test hooks (create_object, damage).
##   auras     a Men statue (PassiveAreaEffectBehavior ModifierName GenericHeroLeadership, EffectRadius 200) and a well (HealPercentPerSecond 2%, PingDelay 3 s):
##             infantry inside the radius get the leadership list / heal back after a hit; the same troops outside the radius get nothing (control)
##   cp        (with --lobby-cp=<percent>) the local player's command point cap is the 2-player MP pair's cap times percent / 100 and the HUD's
##             command point text shows usage / limit with that cap once the bonus passes the old ceiling
##   handicap  (with --lobby-handicap=0:<h>) the local player's handicap is h and a unit made now carries HandicapPercent<-h>; the AI (slot 1, 0) none
##   farms     a resource building placed through the real build ghost: the placement text shows the claimable percentage, the claim circles of the own /
##             allied Visible claimants are drawn as arcs (lane ECOCIRCLE-2); overlapping farms earn less
extends "res://scripts/spell3_player.gd"


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	faction = str(game._faction) if "_faction" in game else ""
	await _run_scenarios(scenarios)


func shot(name: String) -> void:
	game._save_named("ownerfix2-%s" % name)


func me_player() -> Dictionary:
	for p in world.get_economy().get("players", []):
		if int(p.index) == int(game._local_index):
			return p
	return {}


## the members of a horde (or the object itself when it has none)
func members_of(id: int) -> Array:
	var d: Dictionary = world.get_object(id)
	var m: Array = d.get("members", [])
	return m if not m.is_empty() else [id]


func create(template: String, at: Vector2) -> int:
	var id := int(world.create_object(template, int(game._local_index), at.x, at.y, 0.0))
	print("OWNERFIX2 TEST HOOK create %s at (%.0f, %.0f): %d" % [template, at.x, at.y, id])
	return id


func has_list(id: int, list: String) -> bool:
	return (world.get_object(id).get("modifiers", []) as Array).has(list)


## the statue's leadership and the well's heal on troops inside the radius; the same troops far away as the control
func _scenario_auras() -> String:
	var base := base_position()
	if base == Vector2():
		return "no base"
	print("OWNERFIX2 auras: TEST HOOK auto-acquire off: %s" % str(world.debug_set_auto_acquire(false)))
	# an open field beside the castle: the statue, a horde 80 away from it, a control horde 600 away from both
	var spot := base + Vector2(0, -450)
	var statue := create("GondorStatue", spot)
	var near := create("GondorFighterHorde", spot + Vector2(80, 0))
	var far := create("GondorFighterHorde", spot + Vector2(-600, 0))
	if statue <= 0 or near <= 0 or far <= 0:
		return "the objects were not created"
	var sd: Dictionary = world.get_object(statue)
	print("OWNERFIX2 auras: statue under construction %s, health %s" % [str(sd.get("under_construction")), str(sd.get("health"))])
	hud.camera_look_at(spot)
	await game._wait_seconds(4.0)
	var near_with := 0
	var near_m := members_of(near)
	for m in near_m:
		near_with += 1 if has_list(int(m), "GenericHeroLeadership") else 0
	var far_with := 0
	var far_m := members_of(far)
	for m in far_m:
		far_with += 1 if has_list(int(m), "GenericHeroLeadership") else 0
	print("OWNERFIX2 auras: statue: near horde %d / %d members with GenericHeroLeadership (%s), far horde %d / %d" % [near_with, near_m.size(),
		str(world.get_object(int(near_m[0])).get("modifiers")), far_with, far_m.size()])
	shot("auras-statue")
	var why := ""
	if near_m.size() < 2 or near_with != near_m.size():
		why += "statue: %d of %d members near it have GenericHeroLeadership; " % [near_with, near_m.size()]
	if far_with != 0:
		why += "statue: %d members 600 away have it; " % far_with
	# the well: hurt both hordes' members, the near one must heal back (HealPercentPerSecond 2% every 3 s, not within 4 s of damage), the far one not
	var wspot := base + Vector2(450, -450)
	var well := create("GondorWell", wspot)
	var hurt := create("GondorFighterHorde", wspot + Vector2(60, 0))
	var ctrl := create("GondorFighterHorde", wspot + Vector2(700, 0))
	await game._wait_seconds(1.0)
	var hm := members_of(hurt)
	var cm := members_of(ctrl)
	for m in hm + cm:
		var d: Dictionary = world.get_object(int(m))
		world.debug_damage_object(int(m), float(d.get("max_health", 0.0)) * 0.5, 0)
	await game._wait_seconds(1.0)
	var h0 := health_of(hm)
	var c0 := health_of(cm)
	hud.camera_look_at(wspot)
	await game._wait_seconds(10.0)
	var h1 := health_of(hm)
	var c1 := health_of(cm)
	print("OWNERFIX2 auras: well %d: near horde health %.1f -> %.1f, control horde %.1f -> %.1f" % [well, h0, h1, c0, c1])
	shot("auras-well")
	if not (h1 > h0):
		why += "well: the hurt troops beside it did not heal (%.1f -> %.1f); " % [h0, h1]
	if c1 != c0:
		why += "well: the control troops 700 away changed health (%.1f -> %.1f); " % [c0, c1]
	return why


func health_of(ids: Array) -> float:
	var h := 0.0
	for m in ids:
		h += float(world.get_object(int(m)).get("health", 0.0))
	return h


## the lobby's Command Points factor (--lobby-cp) in the game: RotWK scales the CAP (RW 0x6A7DE2: cap * percent / 100 of the 2-player pair
## GoodCommandPointsMP2 = 100 1000); the HUD shows usage / limit, limit = min(cap, base + bonus). Resource buildings raise the bonus past 1000: with the factor
## above 100 the limit passes 1000, at 100 it stops there
func _scenario_cp() -> String:
	var percent := int(game._lobby_cp) if game._lobby_cp > 0 else 100
	var p := me_player()
	if p.is_empty():
		return "no local player in the economy"
	var want_cap := 1000 * percent / 100
	print("OWNERFIX2 cp: lobby %d%%: cap %d base %d bonus %d limit %d (the HUD: %s)" % [percent, int(p.cp_cap), int(p.cp_base), int(p.cp_bonus), int(p.cp_limit),
		str(hud.get_state().get("control_bar", {}).get("command_points"))])
	if int(p.cp_cap) != want_cap:
		return "the command point cap is %d, the lobby's %d%% of 1000 is %d" % [int(p.cp_cap), percent, want_cap]
	# farms (CommandPointBonus) until the bonus passes the 1x ceiling
	var base := base_position()
	var k := 0
	while int(me_player().cp_bonus) + int(me_player().cp_base) <= 1300 and k < 40:
		var at := base + Vector2(-900 + 150 * (k % 12), -700 - 150 * int(k / 12))
		var id := int(world.create_object("GondorFarm", int(game._local_index), at.x, at.y, 0.0))
		if id > 0:
			world.debug_build_complete(id)
		k += 1
		await game._step(2)
	await game._wait_seconds(1.0)
	p = me_player()
	var text := str(hud.get_state().get("control_bar", {}).get("command_points"))
	print("OWNERFIX2 cp: after %d TEST HOOK farms: cap %d base %d bonus %d limit %d, the HUD shows %s" % [k, int(p.cp_cap), int(p.cp_base), int(p.cp_bonus),
		int(p.cp_limit), text])
	shot("cp-%d" % percent)
	var want_limit: int = min(want_cap, int(p.cp_base) + int(p.cp_bonus))
	if int(p.cp_limit) != want_limit:
		return "the limit is %d, min(cap %d, base + bonus %d) is %d" % [int(p.cp_limit), want_cap, int(p.cp_base) + int(p.cp_bonus), want_limit]
	if not text.ends_with("/%d" % want_limit):
		return "the HUD's command points '%s' do not show the limit %d" % [text, want_limit]
	return ""


## the lobby handicap (--lobby-handicap=0:<h>): the local player's handicap and HandicapPercent<-h> on a unit made now (HEALTH_MULT); the AI's unit none
func _scenario_handicap() -> String:
	var p := me_player()
	var h := int(p.get("handicap", 0))
	var ai: Dictionary = {}
	for q in world.get_economy().get("players", []):
		if int(q.index) != int(game._local_index) and bool(q.playable):
			ai = q
	print("OWNERFIX2 handicap: the local player's handicap %d, the other player's %s" % [h, str(ai.get("handicap"))])
	if h == 0:
		return "the local player has no handicap (start with --lobby-handicap=0:<h>)"
	var base := base_position()
	var a := int(world.create_object("GondorFighter", int(game._local_index), base.x + 300, base.y - 300, 0.0))
	var b := int(world.create_object("GondorFighter", int(ai.get("index", -1)), base.x + 400, base.y - 300, 0.0))
	await game._step(4)
	var da: Dictionary = world.get_object(a)
	var db: Dictionary = world.get_object(b)
	var list := "HandicapPercent%d" % (-h)
	print("OWNERFIX2 handicap: TEST HOOK own fighter %s max health %s, the other player's fighter %s max health %s" % [str(da.get("modifiers")),
		str(da.get("max_health")), str(db.get("modifiers")), str(db.get("max_health"))])
	if not (da.get("modifiers", []) as Array).has(list):
		return "the local player's new unit lacks %s" % list
	for n in db.get("modifiers", []):
		if str(n).begins_with("HandicapPercent"):
			return "the other player's unit has %s" % n
	if not (float(da.get("max_health", 0)) < float(db.get("max_health", 0))):
		return "the handicapped unit's health %s is not below the other's %s" % [str(da.get("max_health")), str(db.get("max_health"))]
	return ""


## a farm placed through the real build ghost: the placement text shows the claimable percentage; over our own farm it drops, the colour band follows
func _scenario_farms() -> String:
	await _headless_window()
	var base := base_position()
	# the builder and its farm button
	var builder: Dictionary = {}
	var button: Dictionary = {}
	for o in own_objects():
		if o.structure:
			continue
		await look_by_radar(Vector2(o.x, o.y))
		if not await select_object(o):
			continue
		await game._step(4)
		for bb in hud.get_command_buttons():
			if bb.command == "DOZER_CONSTRUCT" and str(bb.template).to_lower().contains("farm"):
				button = bb
		if not button.is_empty():
			builder = o
			break
	if builder.is_empty():
		return "no own unit offers a farm DOZER_CONSTRUCT button"
	await look_by_radar(base + Vector2(0, -400))
	var bp := palantir_button(button.frame)
	if bp.x < 0:
		return "the farm button is not on the Palantir"
	await click(bp)
	await game._step(6)
	if not bool(hud.get_placement().get("placing", false)):
		return "the farm button started no placement"
	# a legal site in the middle of the screen
	var mid := window_size() * Vector2(0.5, 0.45)
	var site := Vector2(-1, -1)
	for k in 30:
		var px: Vector2 = mid + Vector2((k % 6) - 2.5, int(k / 6) - 2.0) * Vector2(60, 40)
		await move_to(px)
		await game._step(4)
		if int(hud.get_placement().get("legal", -1)) == 0:
			site = px
			break
	if site.x < 0:
		return "no legal farm site near the middle of the screen"
	await game._step(4)
	var rf: Dictionary = hud.get_spellbook_state().get("resource_feedback", {})
	print("OWNERFIX2 farms: free ground: %s" % JSON.stringify(rf).left(400))
	await shot_with_cursor("farms-place-free", site)
	if not bool(rf.get("placement_active", false)) or not bool(rf.get("text_drawn", false)) or not bool(rf.get("place_decal_drawn", false)):
		return "no placement feedback drawn (%s)" % JSON.stringify(rf).left(300)
	var free_percent := int(rf.percent)
	# place it (a click), then the second farm's ghost over the first one
	await click(site)
	await game._step(10)
	var placed: Dictionary = hud.get_placement()
	print("OWNERFIX2 farms: placed; still placing %s" % str(placed.get("placing", false)))
	var farm_id := 0
	for o in own_objects():
		if str(o.template).to_lower().contains("farm"):
			farm_id = int(o.id)
	if farm_id > 0:
		print("OWNERFIX2 farms: TEST HOOK the farm %d finished: %s" % [farm_id, str(world.debug_build_complete(farm_id))])
	await game._step(6)
	if not bool(hud.get_placement().get("placing", false)):
		await select_object(world.get_object(int(builder.id)))
		await game._step(4)
		await click(palantir_button(button.frame))
		await game._step(6)
	# a legal site beside the first farm (its disc overlaps the first one's): the share drops below free ground's but stays above 0
	var over := Vector2(-1, -1)
	for k in 24:
		var px: Vector2 = site + Vector2(60 + 20 * k, 10)
		await move_to(px)
		await game._step(4)
		if int(hud.get_placement().get("legal", -1)) == 0:
			over = px
			break
	if over.x < 0:
		return "no legal farm site beside the first farm"
	await game._step(6)
	rf = hud.get_spellbook_state().get("resource_feedback", {})
	print("OWNERFIX2 farms: beside the first farm: %s" % JSON.stringify(rf).left(400))
	# lane ECOCIRCLE-2: the placement's claim circles (RW 0x6A2CC8's collector, RW 0x8E9744's segments): the first farm is an own Visible claimant
	var circle_count: int = (rf.get("claim_circles", []) as Array).size()
	var arc_count := int(rf.get("claim_arcs_drawn", 0))
	print("OWNERFIX2 farms: claim circles collected %d, claim arcs drawn %d" % [circle_count, arc_count])
	await shot_with_cursor("farms-place-arcs", over)
	if circle_count < 1:
		return "the placement collected no claim circles beside the first farm"
	if arc_count < 1:
		return "the placement drew no claim arcs beside the first farm"
	var over_percent := int(rf.get("percent", -1))
	if not (over_percent < free_percent and over_percent > 0):
		return "the claimable share beside our own farm (%d%%) is not between 0 and free ground's (%d%%)" % [over_percent, free_percent]
	# leave the placement (Esc), select the farm: its claim decal
	await key(KEY_ESCAPE)
	await game._step(6)
	if farm_id > 0:
		await select_object(world.get_object(farm_id))
		await game._step(6)
		rf = hud.get_spellbook_state().get("resource_feedback", {})
		print("OWNERFIX2 farms: the farm selected: claim decals %s drawn %s" % [JSON.stringify(rf.get("claim_decals")).left(300), str(rf.get("claim_decals_drawn"))])
		shot("farms-selected")
		if int(rf.get("claim_decals_drawn", 0)) < 1:
			return "the selected farm shows no claim decal"
	return ""


func key(code: Key) -> void:
	Input.parse_input_event(_key_event(code, true, false))
	await game._step(1)
	Input.parse_input_event(_key_event(code, false, false))
	await game._step(1)


## the builder's own button for a template whose name contains `part`; places it at a legal site near the screen's middle with real input; the new
## object's id (0 none)
var last_ghost: Dictionary = {}


func build_with_builder(part: String, look_at: Vector2) -> int:
	var builder: Dictionary = {}
	var button: Dictionary = {}
	for o in own_objects():
		if o.structure:
			continue
		await look_by_radar(Vector2(o.x, o.y))
		if not await select_object(o):
			continue
		await game._step(4)
		for bb in hud.get_command_buttons():
			if bb.command == "DOZER_CONSTRUCT" and str(bb.template).to_lower().contains(part):
				button = bb
		if not button.is_empty():
			builder = o
			break
	if builder.is_empty():
		print("OWNERFIX2 build: no own unit offers a DOZER_CONSTRUCT button for '%s'" % part)
		return 0
	await look_by_radar(look_at)
	await click(palantir_button(button.frame))
	await game._step(6)
	if not bool(hud.get_placement().get("placing", false)):
		print("OWNERFIX2 build: the %s button started no placement" % button.template)
		return 0
	var before := {}
	for o in own_objects():
		before[int(o.id)] = true
	var mid := window_size() * Vector2(0.5, 0.45)
	for k in 30:
		var px: Vector2 = mid + Vector2((k % 6) - 2.5, int(k / 6) - 2.0) * Vector2(60, 40)
		await move_to(px)
		await game._step(4)
		if int(hud.get_placement().get("legal", -1)) == 0:
			await game._step(4)
			last_ghost = ghost_icon0()
			print("OWNERFIX2 build: the %s ghost: shadow decal '%s' size %s" % [button.template, str(last_ghost.get("shadow_decal", "")), str(last_ghost.get("shadow_decal_size", 0))])
			shot("build-ghost-" + part)
			await click(px)
			await game._step(10)
			break
	for o in own_objects():
		if not before.has(int(o.id)) and str(o.template) == str(button.template):
			print("OWNERFIX2 build: %s placed by %s: %d" % [button.template, builder.template, int(o.id)])
			return int(o.id)
	print("OWNERFIX2 build: %s was not placed" % button.template)
	return 0


func fx_played(name: String) -> int:
	return int(world.get_fx_report().get("played", {}).get(name, 0))


func own_of(template: String) -> int:
	for o in own_objects():
		if str(o.template) == template:
			return int(o.id)
	return 0


## runs the game at 4x until the builder's `template` stands finished; its id (0: it never did)
func finish(id: int, template: String) -> int:
	world.set_time_scale(4.0)
	var waited := 0
	var done := 0
	while waited < 300:
		var cur := own_of(template)
		var d: Dictionary = world.get_object(cur)
		if waited % 10 == 0:
			print("OWNERFIX2 builtauras: %d s: %s %d under construction %s %s health %s" % [waited, template, cur, str(d.get("under_construction")), str(d.get("construction_percent")), str(d.get("health"))])
		if cur > 0 and not bool(d.get("under_construction", true)):
			done = cur
			break
		await game._wait_seconds(1.0)
		waited += 1
	world.set_time_scale(1.0)
	return done


## a statue and a well built by the player's builder through the real ghost, one after the other (the construction runs at 4x), then troops beside them:
## the leadership list and the heal, and their FXLists played (FX_GenericLeadership of the ModifierList, the well's HealFX FX_SpellHealUnitHealBuff)
func _scenario_builtauras() -> String:
	await _headless_window()
	var base := base_position()
	var statue := await build_with_builder("statue", base.lerp(enemy_base(), 0.08) + Vector2(0, -150))
	statue = await finish(statue, "GondorStatue")
	var well := await build_with_builder("well", base.lerp(enemy_base(), 0.08) + Vector2(0, 150))
	var well_ghost := last_ghost
	well = await finish(well, "GondorWell")
	if statue <= 0 or well <= 0:
		return "the builder did not build a statue and a well (statue %d, well %d)" % [statue, well]
	var sd: Dictionary = world.get_object(statue)
	var wd: Dictionary = world.get_object(well)
	# the selected statue's aura ring: its ExperienceLevel GondorStatue1 SelectionDecal (decal_hero_good, GONDOR_STATUE_AOE_RADIUS_DECAL = 440)
	for o in own_objects():
		if int(o.id) == statue:
			await look_by_radar(Vector2(o.x, o.y))
			await select_object(o)
	await game._step(6)
	var ring := {}
	for e in hud.get_state().get("selection_decals", []):
		if int(e.object) == statue:
			ring = e
	print("OWNERFIX2 builtauras: the selected statue's selection decal %s (drawn %s)" % [str(ring), str(hud.get_state().get("selection_decals_drawn"))])
	shot("builtauras-statue-selected")
	var near_s := create("GondorFighterHorde", Vector2(sd.x, sd.y) + Vector2(70, 40))
	var near_w := create("GondorFighterHorde", Vector2(wd.x, wd.y) + Vector2(70, 40))
	await game._wait_seconds(1.0)
	var wm := members_of(near_w)
	for m in wm:
		world.debug_damage_object(int(m), float(world.get_object(int(m)).get("max_health", 0.0)) * 0.5, 0)
	var h0 := health_of(wm)
	hud.camera_look_at(Vector2(sd.x, sd.y))
	await game._wait_seconds(5.0)
	var sm := members_of(near_s)
	var with := 0
	for m in sm:
		with += 1 if has_list(int(m), "GenericHeroLeadership") else 0
	var lead_buffs := buffs_on(sm, "leadership")
	shot("builtauras-statue")
	hud.camera_look_at(Vector2(wd.x, wd.y))
	await game._wait_seconds(6.0)
	var h1 := health_of(wm)
	var heal_buffs := buffs_on(wm, "healing")
	shot("builtauras-well")
	var bs: Dictionary = world.get_buff_state()
	print("OWNERFIX2 builtauras: statue: %d / %d members with GenericHeroLeadership, %d with a leadership buff (the FX_GenericLeadership BuffNugget); well: health %.1f -> %.1f, %d members with a healing buff; buffs %d drawn %d errors %s" % [
		with, sm.size(), lead_buffs, h0, h1, heal_buffs, int(bs.get("count", 0)), int(bs.get("drawn", 0)), str(bs.get("errors"))])
	var why := ""
	if sm.size() < 2 or with != sm.size():
		why += "the built statue gave %d of %d members leadership; " % [with, sm.size()]
	if not (h1 > h0):
		why += "the built well healed nobody (%.1f -> %.1f); " % [h0, h1]
	if lead_buffs == 0:
		why += "no leadership buff on the statue's troops (FX_GenericLeadership's BuffNugget); "
	if heal_buffs == 0:
		why += "no healing buff on the well's troops (FX_SpellHealUnitHealBuff's BuffNugget); "
	if str(well_ghost.get("shadow_decal", "")).to_lower() != "decal_hero_good.tga" or float(well_ghost.get("shadow_decal_size", 0)) != 440.0:
		why += "the well's placement ghost shows no 440 decal_hero_good area (%s); " % str(well_ghost)
	if ring.is_empty() or float(ring.get("size", 0)) != 440.0:
		why += "the selected statue shows no 440 aura ring (%s); " % str(ring)
	return why


## how many of `ids` carry a client buff whose thing's name contains `kind` (LeadershipBuffInfantryLvl1, HealingBuff, ...)
func buffs_on(ids: Array, kind: String) -> int:
	var n := 0
	var want := {}
	for m in ids:
		want[int(m)] = true
	var seen := {}
	for b in world.get_buff_state().get("buffs", []):
		if want.has(int(b.object)) and str(b.thing).to_lower().contains(kind) and not seen.has(int(b.object)):
			seen[int(b.object)] = true
			n += 1
	return n


## every other kind of structure aura of the retail data, each made with the test hook beside a fresh horde of its owner: the statues / wells of the other
## factions (PassiveAreaEffectBehavior), the Wild skull totem (AttributeModifierAuraUpdate StartsActive), the forbidden pools (AutoHealBehavior Radius 250,
## KindOf INFANTRY) and the Men fortress banners (AttributeModifierAuraUpdate TriggeredBy Upgrade_MenFortressBanners, bought at the citadel)
## a logged test hook (Sol's review: isolate the cause): the enemy's every object but its fortress and every creep dies, so no raid reaches a case's
## troops (the game goes on)
func calm_enemy() -> void:
	var keep := ""
	for o in world.get_player_objects(enemy_name()):
		if o.commandcenter:
			keep = str(o.template)
	print("OWNERFIX2 TEST HOOK enemy objects but %s killed: %s" % [keep, str(world.debug_kill_player_objects(enemy_name(), keep))])
	# the map's creeps (their lairs between the bases send raiders too)
	print("OWNERFIX2 TEST HOOK PlyrCreeps objects killed: %s" % str(world.debug_kill_player_objects("PlyrCreeps", "")))


func _scenario_allauras() -> String:
	var base := base_position()
	var me := int(game._local_index)
	var cases := [
		["ArnorStatue", "GenericHeroLeadership", false], ["ElvenStatue", "ElvenStatueLeadership", false], ["DwarvenStatue", "GenericHeroLeadership", false],
		["WildSkullTotem", "GenericHeroLeadership", false], ["ArnorWell", "", true], ["GondorForbiddenPool", "", true], ["ArnorForbiddenPool", "", true],
	]
	var dir := (enemy_base() - base).normalized()
	var side := Vector2(-dir.y, dir.x)
	var why := ""
	var k := 0
	for c in cases:
		var at: Vector2 = base - dir * 380.0 + side * (float(k) - 1.0) * 320.0 # behind the castle, away from the enemy and the lairs between; 320 apart: every
		# horde (80 off its own structure) is more than 300 from the others (the pools heal within 250, the statues lead within 220); from -320 on that
		# side (at -960 / -640 the first two statues stood off the playable map, y < 0, and led nobody)
		k += 1
		# Sol's review: the troops first, without the structure (the control: no other case's structure may reach them), then the structure
		calm_enemy()
		var h := create("GondorFighterHorde", at + dir * 80.0)
		if h <= 0:
			why += "%s: the horde was not created; " % c[0]
			continue
		await game._step(3)
		var mm := members_of(h)
		if bool(c[2]):
			for m in mm:
				world.debug_damage_object(int(m), float(world.get_object(int(m)).get("max_health", 0.0)) * 0.5, 0)
		await game._step(3)
		var c0 := health_of(mm)
		await game._wait_seconds(8.0) # longer than the 4 second damage gate: a heal from elsewhere would show here
		var c1 := health_of(mm)
		var before := 0
		if str(c[1]) != "":
			for m in mm:
				before += 1 if has_list(int(m), str(c[1])) else 0
		print("OWNERFIX2 allauras: %s control (no structure): %d / %d members with '%s', health %.1f -> %.1f" % [c[0], before, mm.size(), c[1], c0, c1])
		if before != 0 or c1 != c0:
			why += "%s: the troops are not isolated (%d with %s, health %.1f -> %.1f without the structure); " % [c[0], before, c[1], c0, c1]
			continue
		var s := create(str(c[0]), at)
		if s <= 0:
			why += "%s: not created; " % c[0]
			continue
		await game._step(3)
		var h0 := health_of(mm)
		await game._wait_seconds(8.0)
		var h1 := health_of(mm)
		var with := 0
		if str(c[1]) != "":
			for m in mm:
				with += 1 if has_list(int(m), str(c[1])) else 0
		print("OWNERFIX2 allauras: %s: %d / %d members with '%s', health %.1f -> %.1f" % [c[0], with, mm.size(), c[1], h0, h1])
		if str(c[1]) != "" and with != mm.size():
			why += "%s: %d of %d members have %s; " % [c[0], with, mm.size(), c[1]]
		if bool(c[2]) and not (h1 > h0):
			why += "%s: no heal (%.1f -> %.1f); " % [c[0], h0, h1]
	# the fortress's own auras: Upgrade_MenFortressBanners (AttributeModifierAuraUpdate TriggeredBy, MenFortressBannersLeadership within 300) and
	# Upgrade_MenFortressHouseOfHealing (PassiveAreaEffectBehavior UpgradeRequired, 3% a second within 200), bought at the citadel (MenFortressCommandSet)
	var citadel := 0
	for o in own_objects():
		if str(o.template).contains("FortressCitadel"):
			citadel = int(o.id)
	if citadel == 0:
		return why + "no own FortressCitadel: the fortress banners and the House of Healing were not checked; " # Sol's review: a missing case fails
	# Sol's review: the troops first, measured before the upgrades exist (the control: no other source may reach them), then the upgrades
	calm_enemy()
	var cd: Dictionary = world.get_object(citadel)
	# behind the citadel (away from the statues placed toward the enemy: a LEADERSHIP list already on a unit blocks a second one, ReplaceInCategoryIfLongest)
	var hh := create("GondorFighterHorde", Vector2(cd.x, cd.y) + side * 170.0) # inside the House of Healing's 200 (at 250 one member was)
	await game._step(3)
	var mm := members_of(hh)
	for m in mm:
		world.debug_damage_object(int(m), float(world.get_object(int(m)).get("max_health", 0.0)) * 0.5, 0)
	await game._step(3)
	var c0 := health_of(mm)
	await game._wait_seconds(8.0)
	var c1 := health_of(mm)
	var before := 0
	for m in mm:
		before += 1 if has_list(int(m), "MenFortressBannersLeadership") else 0
	print("OWNERFIX2 allauras: fortress control (no upgrades): %d / %d members with MenFortressBannersLeadership, health %.1f -> %.1f" % [before, mm.size(), c0, c1])
	if before != 0 or c1 != c0:
		return why + "fortress: the troops are not isolated (%d with MenFortressBannersLeadership, health %.1f -> %.1f without the upgrades); " % [before, c0, c1]
	world.give_money(me, 10000)
	world.queue_upgrade(me, citadel, "Upgrade_MenFortressBanners")
	await game._wait_seconds(1.0)
	world.queue_upgrade(me, citadel, "Upgrade_MenFortressHouseOfHealing")
	var waited := 0
	while waited < 90:
		var ups: Array = world.get_upgrades(citadel).get("object", [])
		if ups.has("Upgrade_MenFortressBanners") and ups.has("Upgrade_MenFortressHouseOfHealing"):
			break
		if waited % 10 == 0:
			print("OWNERFIX2 allauras: %d s: citadel upgrades %s, production %s" % [waited, str(ups), JSON.stringify(world.get_production(me, citadel)).left(300)])
		await game._wait_seconds(1.0)
		waited += 1
	print("OWNERFIX2 allauras: %s upgrades %s" % [cd.get("template"), str(world.get_upgrades(citadel).get("object"))])
	var h0 := health_of(mm)
	await game._wait_seconds(8.0)
	var h1 := health_of(mm)
	var with := 0
	for m in mm:
		with += 1 if has_list(int(m), "MenFortressBannersLeadership") else 0
	print("OWNERFIX2 allauras: fortress: %d / %d members with MenFortressBannersLeadership (%s); House of Healing health %.1f -> %.1f" % [with, mm.size(),
		str(world.get_object(int(mm[0])).get("modifiers")), h0, h1])
	if with != mm.size():
		why += "fortress banners: %d of %d members; " % [with, mm.size()]
	if not (h1 > h0):
		why += "House of Healing: no heal (%.1f -> %.1f); " % [h0, h1]
	return why
