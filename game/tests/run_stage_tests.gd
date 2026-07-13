extends RefCounted
## Legacy Stage 1-9 regression suite. Stage 10 wraps this with release gates.

const SimWorldScript = preload("res://src/sim/sim_world.gd")
const SimTypesScript = preload("res://src/sim/sim_types.gd")
const AIScript = preload("res://src/sim/skirmish_ai.gd")
const SaveScript = preload("res://src/sim/save_game.gd")

var passed: int = 0
var failed: int = 0
var lines: PackedStringArray = PackedStringArray()

func run_all() -> String:
	passed = 0
	failed = 0
	lines = PackedStringArray()
	if ContentDB.units.is_empty():
		ContentDB.reload()
	_test_content_pack()
	_test_stage1_combat()
	_test_stage2_economy()
	_test_stage3_walls_fog()
	_test_stage4_heroes()
	_test_stage5_powers()
	_test_stage6_factions_matrix()
	_test_stage6_art_inventory()
	_test_stage7_ai()
	_test_stage8_save_maps()
	_test_stage9_ring()
	var summary := "STAGE TESTS: %d passed, %d failed" % [passed, failed]
	lines.append(summary)
	return "\n".join(lines)

func _ok(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		lines.append("PASS  %s" % name)
	else:
		failed += 1
		lines.append("FAIL  %s  %s" % [name, detail])

func _test_content_pack() -> void:
	_ok("content has soldier", ContentDB.units.has("soldier"))
	_ok("content has fortress", ContentDB.buildings.has("fortress") or ContentDB.buildings.has("g_fortress"))
	_ok("content has blademaster", ContentDB.abilities.has("blademaster"))
	_ok("content has gondor faction", ContentDB.factions.has("gondor"))
	_ok("globals start_resources", ContentDB.globals.has("start_resources"))
	_ok("content has powers", ContentDB.powers.size() >= 6)
	_ok("content has 4 factions", ContentDB.factions.size() >= 4)
	_ok("content has 4 maps", ContentDB.maps.size() >= 4)
	_ok("content has damage_matrix", not ContentDB.damage_matrix.is_empty())
	_ok("content has research", ContentDB.research.size() >= 4)

func _test_stage1_combat() -> void:
	GameState.reset_match()
	GameState.stage_flags["fear"] = false
	var w = SimWorldScript.new(1)
	GameState.world = w
	var s = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	var o = w.spawn_battalion("orc", GameState.SIDE_ENEMY, Vector2(4, 0), "mordor")
	s.order.type = SimTypesScript.OrderType.ATTACK
	s.target_id = o.id
	s.target_kind = SimTypesScript.EntityKind.BATTALION
	o.order.type = SimTypesScript.OrderType.ATTACK
	o.target_id = s.id
	for i in 800:
		w.tick(0.1)
		if s.dead or o.dead:
			break
	_ok("stage1 combat resolves death", s.dead or o.dead, "s.hp=%.1f o.hp=%.1f" % [s.hp, o.hp])
	GameState.reset_match()
	w = SimWorldScript.new(2)
	GameState.world = w
	var m = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	w.order_move([m.id], Vector2(30, 0))
	for i in 80:
		w.tick(0.1)
	_ok("stage1 move reaches goal", m.pos.x > 20.0, "x=%.2f" % m.pos.x)
	GameState.reset_match()
	w = SimWorldScript.new(3)
	GameState.world = w
	var f = w.spawn_building("mfortress", GameState.SIDE_ENEMY, Vector2(10, 0), true, "mordor")
	var atk = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	for i in 50:
		w.take_damage_building(f, 200.0, atk)
		if f.dead:
			break
	_ok("stage1 fortress death ends match", GameState.game_over and f.dead)

func _test_stage2_economy() -> void:
	GameState.reset_match()
	GameState.stage_flags["economy"] = true
	var w = SimWorldScript.new(4)
	GameState.world = w
	w.spawn_building("fortress", GameState.SIDE_PLAYER, Vector2(0, 0), true, "gondor")
	var farm = w.spawn_building("farm", GameState.SIDE_PLAYER, Vector2(15, 0), true, "gondor")
	var farm2 = w.spawn_building("farm", GameState.SIDE_PLAYER, Vector2(20, 0), true, "gondor")
	var start_res: float = float(GameState.resources[0])
	for i in 30:
		w.tick(0.1)
	var gained: float = float(GameState.resources[0]) - start_res
	_ok("stage2 farms generate income", gained > 0.0, "gained=%.2f" % gained)
	var eff1: float = w.farm_efficiency(farm)
	_ok("stage2 farm efficiency < 1 when clustered", eff1 < 0.99, "eff=%.2f" % eff1)
	GameState.resources[0] = 5000.0
	var barracks = w.spawn_building("barracks", GameState.SIDE_PLAYER, Vector2(0, 20), true, "gondor")
	var before: int = w.count_battalions(GameState.SIDE_PLAYER)
	var enq: bool = w.enqueue_train(barracks.id, "soldier")
	_ok("stage2 enqueue train", enq, "queue=%s built=%s" % [str(barracks.train_queue), str(barracks.built)])
	for i in 200:
		w.tick(0.1)
		if w.count_battalions(GameState.SIDE_PLAYER) > before:
			break
	var after: int = w.count_battalions(GameState.SIDE_PLAYER)
	_ok("stage2 train completes", after > before, "before=%d after=%d queue=%s" % [before, after, str(barracks.train_queue)])
	_ok("stage2 reject far placement", not w.can_place("farm", GameState.SIDE_PLAYER, Vector2(100, 100)))

func _test_stage3_walls_fog() -> void:
	GameState.reset_match()
	GameState.stage_flags["fog"] = true
	var w = SimWorldScript.new(5)
	GameState.world = w
	w.fog_enabled = true
	w.spawn_building("wall", GameState.SIDE_PLAYER, Vector2(0, 10), true, "gondor")
	var gate = w.spawn_building("gate", GameState.SIDE_PLAYER, Vector2(5, 10), true, "gondor")
	gate.gate_open = false
	w.rebuild_obstacles()
	_ok("stage3 wall obstacle registered", w.obstacles.size() >= 2)
	var unit = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	w._update_fog()
	_ok("stage3 fog visible at unit", w.is_visible_to_player(unit.pos))
	_ok("stage3 fog not visible far", not w.is_visible_to_player(Vector2(100, 100)))
	w.spawn_building("tower", GameState.SIDE_PLAYER, Vector2(0, 0), true, "gondor")
	var orc = w.spawn_battalion("orc", GameState.SIDE_ENEMY, Vector2(20, 0), "mordor")
	var hp0: float = orc.hp
	for i in 50:
		w.tick(0.1)
	_ok("stage3 tower damages enemy in range", orc.hp < hp0 or orc.dead)

func _test_stage4_heroes() -> void:
	GameState.reset_match()
	GameState.stage_flags["abilities"] = true
	GameState.stage_flags["fear"] = true
	GameState.stage_flags["veterancy"] = true
	var w = SimWorldScript.new(6)
	GameState.world = w
	var hero = w.spawn_battalion("aragorn", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	var orc = w.spawn_battalion("orc", GameState.SIDE_ENEMY, Vector2(5, 0), "mordor")
	_ok("stage4 blademaster casts", w.try_cast_ability(hero.id, "blademaster"))
	_ok("stage4 blademaster damages", orc.hp < orc.hp_max or orc.dead)
	hero.rank = 1
	_ok("stage4 ability rank gate", not w.try_cast_ability(hero.id, "athelas"))
	hero.rank = 2
	hero.ability_cds["athelas"] = 0.0
	var heal_target = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(2, 0), "gondor")
	heal_target.hp = 100
	_ok("stage4 athelas at rank 2", w.try_cast_ability(hero.id, "athelas") and heal_target.hp > 100.0)
	w.spawn_battalion("nazgul", GameState.SIDE_ENEMY, Vector2(50, 50), "mordor")
	var victim = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(52, 50), "gondor")
	victim.rank = 1
	for i in 5:
		w.tick(0.1)
	_ok("stage4 terror applies fear", victim.fear_t > 0.0 or victim.cowering)
	w.set_stance([hero.id], GameState.Stance.HOLD)
	_ok("stage4 stance set", hero.stance == GameState.Stance.HOLD)
	var naz = w.spawn_battalion("nazgul", GameState.SIDE_ENEMY, Vector2(80, 80), "mordor")
	w._kill_battalion(hero, naz)
	_ok("stage4 hero death tracked", GameState.hero_deaths.has("aragorn"))
	var fort = w.spawn_building("fortress", GameState.SIDE_PLAYER, Vector2(-40, -40), true, "gondor")
	GameState.resources[0] = 5000.0
	_ok("stage4 can queue revive", w.enqueue_train(fort.id, "aragorn"))
	var v = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(-10, -10), "gondor")
	w._award_xp(v, 900)
	_ok("stage4 veterancy ranks up", v.rank >= 3, "rank=%d" % v.rank)

func _test_stage5_powers() -> void:
	GameState.reset_match()
	GameState.stage_flags["powers"] = true
	var w = SimWorldScript.new(7)
	GameState.world = w
	GameState.add_pp(GameState.SIDE_PLAYER, 100)
	var ally = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	ally.hp = 100
	# unlock+cast heal (tier 1)
	_ok("stage5 unlock heal", GameState.unlock_power(GameState.SIDE_PLAYER, "heal"))
	var pp_before: float = float(GameState.power_points[0])
	_ok("stage5 cast heal", w.cast_power(GameState.SIDE_PLAYER, "heal", Vector2(0, 0)))
	_ok("stage5 heal restores hp", ally.hp > 100.0, "hp=%.1f" % ally.hp)
	_ok("stage5 powers_cast stat", int(GameState.stats.get("powers_cast", 0)) >= 1)
	# tier gating: sunflare needs requires_tier_spent 1
	GameState.add_pp(GameState.SIDE_PLAYER, 50)
	_ok("stage5 can unlock sunflare after tier spend", GameState.can_unlock_power(GameState.SIDE_PLAYER, "sunflare"))
	_ok("stage5 unlock sunflare", GameState.unlock_power(GameState.SIDE_PLAYER, "sunflare"))
	var foe = w.spawn_battalion("orc", GameState.SIDE_ENEMY, Vector2(5, 0), "mordor")
	var fhp: float = foe.hp
	_ok("stage5 cast sunflare", w.cast_power(GameState.SIDE_PLAYER, "sunflare", Vector2(5, 0)))
	_ok("stage5 sunflare damages", foe.hp < fhp or foe.dead)
	# weather
	GameState.add_pp(GameState.SIDE_PLAYER, 50)
	# darkness requires tier 1 spent - already have 2 unlocks
	_ok("stage5 unlock darkness", GameState.unlock_power(GameState.SIDE_PLAYER, "darkness"))
	_ok("stage5 cast darkness", w.cast_power(GameState.SIDE_PLAYER, "darkness", Vector2.ZERO))
	_ok("stage5 weather active", GameState.weather.has("id"))
	# quake buildings
	GameState.add_pp(GameState.SIDE_PLAYER, 50)
	# earthquake needs requires_tier_spent 2
	_ok("stage5 unlock earthquake", GameState.unlock_power(GameState.SIDE_PLAYER, "earthquake"))
	var bld = w.spawn_building("orcpit", GameState.SIDE_ENEMY, Vector2(20, 0), true, "mordor")
	var bhp: float = bld.hp
	_ok("stage5 cast earthquake", w.cast_power(GameState.SIDE_PLAYER, "earthquake", Vector2(20, 0)))
	_ok("stage5 earthquake damages buildings", bld.hp < bhp or bld.dead)

func _test_stage6_factions_matrix() -> void:
	GameState.reset_match()
	for fid in ["gondor", "mordor", "elves", "goblins"]:
		var f: Dictionary = ContentDB.get_faction(fid)
		_ok("stage6 faction %s loads" % fid, not f.is_empty() and f.has("buildings"))
		var fort := String(f.get("fortress", ""))
		_ok("stage6 faction %s fortress def" % fid, ContentDB.buildings.has(fort))
	# damage matrix changes outcome
	var slash_inf := ContentDB.matrix_mul("slash", "infantry_light")
	var pierce_mon := ContentDB.matrix_mul("pierce", "monster")
	var siege_bld := ContentDB.matrix_mul("siege", "building")
	_ok("stage6 matrix slash infantry ~1", slash_inf > 0.8 and slash_inf < 1.2)
	_ok("stage6 matrix pierce vs monster penalty", pierce_mon < 0.8)
	_ok("stage6 matrix siege vs building bonus", siege_bld >= 1.5)
	var w = SimWorldScript.new(8)
	GameState.world = w
	var archer = w.spawn_battalion("archer", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	var troll = w.spawn_battalion("troll", GameState.SIDE_ENEMY, Vector2(3, 0), "mordor")
	var mul = w.damage_mul(archer, troll, null)
	_ok("stage6 archer vs troll mul uses matrix", mul < 1.0, "mul=%.2f" % mul)
	# research
	GameState.resources[0] = 5000.0
	var bar = w.spawn_building("barracks", GameState.SIDE_PLAYER, Vector2(0, 10), true, "gondor")
	var rkey := "forgedBlades" if ContentDB.research.has("forgedBlades") else "forged_blades"
	_ok("stage6 enqueue research", w.enqueue_research(bar.id, rkey))
	for i in 250:
		w.tick(0.1)
		if GameState.has_upgrade(0, rkey):
			break
	_ok("stage6 research completes", GameState.has_upgrade(0, rkey))
	# elves/goblins units spawn
	_ok("stage6 spawn lorienwarrior", w.spawn_battalion("lorienwarrior", GameState.SIDE_PLAYER, Vector2(-20, 0), "elves") != null)
	_ok("stage6 spawn goblinwarrior", w.spawn_battalion("goblinwarrior", GameState.SIDE_ENEMY, Vector2(20, 0), "goblins") != null)

func _test_stage6_art_inventory() -> void:
	var roster: Dictionary = ContentDB.train_build_roster_ids()
	var units: Array = roster.get("units", [])
	var buildings: Array = roster.get("buildings", [])
	_ok("stage6 art roster units non-empty", units.size() >= 8, "n=%d" % units.size())
	_ok("stage6 art roster buildings non-empty", buildings.size() >= 8, "n=%d" % buildings.size())
	var missing_mesh := 0
	var missing_icon := 0
	var tiny_or_blockout := 0
	var bad_ids: Array = []
	for uid in units:
		var def: Dictionary = ContentDB.get_unit(String(uid))
		var mp := ContentDB.resolve_mesh_path(def)
		var ip := ContentDB.resolve_icon_path(def)
		if mp == "":
			missing_mesh += 1
			bad_ids.append(uid)
		elif mp.ends_with(".obj"):
			var abs_p := ProjectSettings.globalize_path(mp) if mp.begins_with("res://") else mp
			var f := FileAccess.open(abs_p, FileAccess.READ)
			if f == null or f.get_length() < 1000:
				tiny_or_blockout += 1
				bad_ids.append("%s:tiny_obj" % uid)
		if ip == "":
			missing_icon += 1
		# load visual and reject single BoxMesh/Capsule blockout
		var vis = AssetFactory.make_unit_visual(String(uid), 0)
		if AssetFactory.is_blockout_visual(vis):
			tiny_or_blockout += 1
			bad_ids.append("%s:blockout_kind=%s" % [uid, str(vis.get_meta("mesh_kind", ""))])
		var kind := String(vis.get_meta("mesh_kind", ""))
		if kind == "single_box":
			tiny_or_blockout += 1
		vis.free()
	for bid in buildings:
		var def2: Dictionary = ContentDB.get_building(String(bid))
		var mp2 := ContentDB.resolve_mesh_path(def2)
		if mp2 == "":
			missing_mesh += 1
			bad_ids.append(bid)
		elif mp2.ends_with(".obj"):
			var abs2 := ProjectSettings.globalize_path(mp2) if mp2.begins_with("res://") else mp2
			var f2 := FileAccess.open(abs2, FileAccess.READ)
			if f2 == null or f2.get_length() < 1000:
				tiny_or_blockout += 1
				bad_ids.append("%s:tiny_obj" % bid)
		if ContentDB.resolve_icon_path(def2) == "":
			missing_icon += 1
		var bvis = AssetFactory.make_building_visual(String(bid), 0)
		if AssetFactory.is_blockout_visual(bvis):
			tiny_or_blockout += 1
			bad_ids.append("%s:blockout" % bid)
		bvis.free()
	_ok("stage6 art all roster meshes resolve", missing_mesh == 0, "missing=%s" % str(bad_ids))
	_ok("stage6 art all roster icons resolve", missing_icon == 0, "missing_icon=%d" % missing_icon)
	_ok("stage6 art no tiny-obj/single-box blockout", tiny_or_blockout == 0, str(bad_ids))
	var uvis = AssetFactory.make_unit_visual("soldier", 0)
	_ok("stage6 art soldier visual authored", uvis.get_meta("authored", false) == true)
	var mesh_meta: String = String(uvis.get_meta("mesh_path", ""))
	_ok("stage6 art soldier mesh is glb or substantial", mesh_meta.ends_with(".glb") or mesh_meta.ends_with(".obj") or mesh_meta.begins_with("kit_multipart:"), mesh_meta)
	_ok("stage6 art soldier not blockout", not AssetFactory.is_blockout_visual(uvis))
	uvis.free()
	# known previously-stub ids must be non-box now
	for sid in ["knight", "fortress", "tower", "nazgul", "trebuchet"]:
		var sv = AssetFactory.make_unit_visual(sid, 0) if ContentDB.units.has(sid) else AssetFactory.make_building_visual(sid, 0)
		_ok("stage6 art %s non-blockout" % sid, not AssetFactory.is_blockout_visual(sv), str(sv.get_meta("mesh_kind", "")))
		sv.free()

func _test_stage7_ai() -> void:
	GameState.reset_match()
	GameState.enemy_faction = "mordor"
	GameState.difficulty = "normal"
	var w = SimWorldScript.new(10)
	GameState.world = w
	w.spawn_building("mfortress", GameState.SIDE_ENEMY, Vector2(70, 70), true, "mordor")
	w.spawn_building("fortress", GameState.SIDE_PLAYER, Vector2(-70, -70), true, "gondor")
	GameState.resources[1] = 3000.0
	var ai = AIScript.new()
	ai.setup(GameState.SIDE_ENEMY, "mordor", "normal")
	GameState.ai = ai
	for i in 400:
		ai.tick(w, 0.1)
		w.tick(0.1)
	_ok("stage7 AI builds something", int(ai.counters.get("builds", 0)) >= 1, str(ai.counters))
	_ok("stage7 AI trains or waves", int(ai.counters.get("trains", 0)) + int(ai.counters.get("waves", 0)) >= 1, str(ai.counters))
	_ok("stage7 AI no softlock resources finite", float(GameState.resources[1]) >= 0.0)
	# hard vs easy diff tables exist
	var diffs: Dictionary = ContentDB.globals.get("difficulties", {})
	_ok("stage7 difficulties defined", diffs.has("easy") and diffs.has("hard"))

func _test_stage8_save_maps() -> void:
	_ok("stage8 maps count>=4", ContentDB.maps.size() >= 4)
	for mid in ["anduin", "misty", "fangorn", "brown"]:
		_ok("stage8 map %s" % mid, ContentDB.maps.has(mid))
	GameState.reset_match()
	var w = SimWorldScript.new(11)
	GameState.world = w
	GameState.player_faction = "gondor"
	GameState.map_id = "fangorn"
	var s = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(3, 4), "gondor")
	s.hp = 321
	s.order.type = SimTypesScript.OrderType.MOVE
	s.has_move_goal = true
	s.move_goal = Vector2(50, 50)
	w.spawn_building("fortress", GameState.SIDE_PLAYER, Vector2(0, 0), true, "gondor")
	var data: Dictionary = SaveScript.serialize(w)
	_ok("stage8 serialize has battalions", data.has("battalions") and (data["battalions"] as Array).size() >= 1)
	# mutate then restore
	s.hp = 1
	s.pos = Vector2(99, 99)
	_ok("stage8 deserialize", SaveScript.deserialize(w, data))
	var s2 = w.get_battalion(s.id)
	_ok("stage8 restore hp", s2 != null and absf(float(s2.hp) - 321.0) < 0.1, "hp=%s" % (s2.hp if s2 else -1))
	_ok("stage8 restore pos", s2 != null and absf(s2.pos.x - 3.0) < 0.1)
	_ok("stage8 restore map_id", GameState.map_id == "fangorn")
	# file save/load
	_ok("stage8 save_to_user", SaveScript.save_to_user("test_slot", w))
	s2.hp = 10
	_ok("stage8 load_from_user", SaveScript.load_from_user("test_slot", w))
	var s3 = w.get_battalion(s.id)
	_ok("stage8 file restore hp", s3 != null and absf(float(s3.hp) - 321.0) < 0.1)
	# control groups + formation state
	GameState.set_control_group(1, [s.id])
	GameState.formation = "wedge"
	_ok("stage8 control group", GameState.control_groups.has(1))
	_ok("stage8 formation", GameState.formation == "wedge")
	# formation slots must differ by type (drives real order_move)
	var line0: Vector2 = w.formation_slot(0, 5, "line")
	var line1: Vector2 = w.formation_slot(1, 5, "line")
	var wedge0: Vector2 = w.formation_slot(0, 5, "wedge")
	var wedge1: Vector2 = w.formation_slot(1, 5, "wedge")
	var col1: Vector2 = w.formation_slot(1, 5, "column")
	_ok("stage8 line spreads on x", absf(line1.x - line0.x) > 1.0, "l0=%s l1=%s" % [line0, line1])
	_ok("stage8 wedge differs from line", wedge1.distance_to(line1) > 0.5, "w1=%s l1=%s" % [wedge1, line1])
	_ok("stage8 column stacks on z", absf(col1.y) > 1.0 and absf(col1.x) < 0.1, "col1=%s" % col1)
	# order_move with formation produces different goals
	GameState.reset_match()
	w = SimWorldScript.new(21)
	GameState.world = w
	var a = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	var b = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(1, 0), "gondor")
	var c = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(2, 0), "gondor")
	GameState.formation = "line"
	w.order_move([a.id, b.id, c.id], Vector2(40, 0), "line")
	var line_goals := [a.move_goal, b.move_goal, c.move_goal]
	GameState.formation = "wedge"
	w.order_move([a.id, b.id, c.id], Vector2(40, 0), "wedge")
	var wedge_goals := [a.move_goal, b.move_goal, c.move_goal]
	var diff := 0.0
	for i in 3:
		diff += line_goals[i].distance_to(wedge_goals[i])
	_ok("stage8 order_move wedge goals differ from line", diff > 1.0, "diff=%.2f" % diff)

func _test_stage9_ring() -> void:
	GameState.reset_match()
	GameState.stage_flags["ring"] = true
	var w = SimWorldScript.new(12)
	GameState.world = w
	w.spawn_ring(Vector2(0, 0))
	_ok("stage9 ring active", GameState.ring.get("active", false) == true)
	var hero = w.spawn_battalion("aragorn", GameState.SIDE_PLAYER, Vector2(1, 0), "gondor")
	for i in 5:
		w.tick_ring(0.1)
	_ok("stage9 hero claims ring", int(GameState.ring.get("holder_id", -1)) == hero.id)
	w._kill_battalion(hero, null)
	_ok("stage9 ring drops on death", GameState.ring.get("dropped", false) == true)
	# combat SFX wired through _do_attack → GameAudio.play_combat
	GameAudio.combat_sfx_calls = 0
	GameState.reset_match()
	w = SimWorldScript.new(13)
	GameState.world = w
	var s = w.spawn_battalion("soldier", GameState.SIDE_PLAYER, Vector2(0, 0), "gondor")
	var o = w.spawn_battalion("orc", GameState.SIDE_ENEMY, Vector2(3, 0), "mordor")
	s.order.type = SimTypesScript.OrderType.ATTACK
	s.target_id = o.id
	s.target_kind = SimTypesScript.EntityKind.BATTALION
	for i in 30:
		w.tick(0.1)
	_ok("stage9 combat sfx fired", GameAudio.combat_sfx_calls > 0, "calls=%d" % GameAudio.combat_sfx_calls)
