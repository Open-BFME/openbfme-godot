## Lane SPELL-3: the spell book as a player uses it, in a real skirmish, by input events (PLAY-1's helpers: mouse motion, clicks and keys pushed into the
## viewport). game.gd runs it after the start with --spell3=<scenario>[,<scenario>...] (with --auto and --faction); every step prints a "SPELL3 ..." line,
## screenshots go to --screens as spell3-<faction>-<name>.png and "SPELL3 RESULT: <n> ok, <m> failed" ends the run.
##   spellbook  the local player gets power points (a logged test hook: retail earns them by kills / rank, which takes too long for a harness), the
##              Palantir's powers button opens SpellStore.apt, every power that can be bought is clicked tier by tier, ACCEPT buys them; then every
##              spell book slot of the Palantir is clicked: a power that wants a position shows its targeting ring, a left click on the ground casts it;
##              the result per power: bought, the slot ready, the cast taken (the slot recharges), the objects the cast made and the damage it did
##   spellbook_idle  (lane HUDREDRAW-1) the HUD's cost with the store open: power points given (the test hook above), 10 s with the store closed, then the
##              Palantir's powers button opens it and it is held open 10 s; per window "SPELL3 APT <window> ..." prints the render frames, fps, the main thread's
##              CPU per frame and the Apt player's get_stats (rebuilds, unchanged_frames, list_us, canvas_us, submit_us, the levels redrawn / kept); run with --perf
##              for the GAME PERF lines
##   spellrc    (lane SPELLRC-1, the owner: "you can't right click mouse move with spellbook powers out") power points given (the test hook above), a unit
##              selected by a click, a power bought by mouse; with the store open a right click on the ground orders the move; then a spell book power that
##              wants a position is pressed (its targeting): a right DRAG on the ground scrolls the camera and the power is still waiting (RotWK's LookAt
##              translator sees the press; the GUI command translator RW 0x83D7AD ends a command only on a click that was not dragged), a right CLICK cancels
##              it without an order (RW 0x83DB8D), and the next right click orders the move
extends "res://scripts/play1_player.gd"

var faction := ""
var results: Array = []  # one Dictionary per power


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	faction = str(game._faction) if "_faction" in game else ""
	await _run_scenarios(scenarios)


func shot(name: String) -> void:
	game._save_named("spell3-%s-%s" % [faction, name])


func bar() -> Dictionary:
	return world.get_spell_bar(game._local_index)


func power_of_button(index: int) -> Dictionary:
	for b in bar().get("buttons", []):
		if int(b.index) == index:
			return b
	return {}


func base_position() -> Vector2:
	for o in own_objects():
		if o.commandcenter:
			return Vector2(o.x, o.y)
	return Vector2()


func enemy_base() -> Vector2:
	for o in world.get_player_objects(enemy_name()):
		if o.commandcenter:
			return Vector2(o.x, o.y)
	return Vector2()


func object_ids(player: String) -> Dictionary:
	var out := {}
	for o in world.get_player_objects(player):
		out[int(o.id)] = o
	return out


func health_sum(objs: Dictionary) -> float:
	var h := 0.0
	for id in objs:
		var d: Dictionary = world.get_object(id)
		h += float(d.get("health", 0.0))
	return h


## the Palantir's slot k on the screen
func slot_pixel(k: int) -> Vector2:
	var path: String = str(hud.get_spellbook_state().get("path", ""))
	if path.begins_with("_level"):
		path = path.substr(path.find(".") + 1)
	for cand in ["%s.Spell%d", "%s.Spells.Spell%d", "%s.Buttons.Spell%d", "%s.Spell%dClip"]:
		var px := palantir_button(cand % [path, k + 1])
		if px.x >= 0:
			return px
	return Vector2(-1, -1)


## the harvestable tree with the most harvestable neighbours within 180 (Devastation's radius), searched near the base; the base itself when none
func grove(base: Vector2) -> Vector2:
	var trees: Array = world.get_logic_trees(base.x, base.y, 1600.0).filter(func(t): return t.harvestable)
	var best := base
	var best_n := 0
	for t in trees:
		var n := 0
		for u in trees:
			if Vector2(t.x, t.y).distance_to(Vector2(u.x, u.y)) < 180.0:
				n += 1
		if n > best_n:
			best_n = n
			best = Vector2(t.x, t.y)
	print("SPELL3 grove: %d harvestable trees near the base, the best spot has %d within 180 at %s" % [trees.size(), best_n, best])
	return best


## every power the store sells now, clicked tier by tier (each click retried while the movie's button has not taken it); the failures, if any
func buy_all(st: Dictionary) -> Array:
	var fails: Array = []
	var lvl: int = st.level
	for round in 30:
		st = store_state()
		var first: int = st.states.find("_active")
		if first < 0:
			break
		var px := shell_button(lvl, "SpellStore.Buttons.Spell%d" % (first + 1))
		var clicks0: int = int(st.get("spell_clicks", 0))
		var picked := false
		var attempts := 0
		for attempt in 6:
			attempts += 1
			await click(px)
			await game._wait_seconds(1.2) # the generic button calls back when its _down animation ends
			if store_state().states[first] != "_active":
				picked = true
				break
			# a missed click: the button clip's state, for the report (review r1: the store's occasional dead click under load)
			var lines: Array = str(shell.dump_tree(lvl, 14)).split("\n")
			var name := "Spell%d" % (first + 1)
			var hovered: Control = game.get_viewport().gui_get_hovered_control()
			print("SPELL3 store: click %d on %s missed (fps %.0f): %s; find_button %s; the pointer %s; the hovered Godot control %s; the store's state %s" % [attempt + 1,
				name, Engine.get_frames_per_second(), " | ".join(lines.filter(func(l): return l.contains("'" + name + "'") or l.contains(name + ".")).slice(0, 8)),
				JSON.stringify(shell.find_button(lvl, "SpellStore.Buttons." + name)), game.get_viewport().get_mouse_position(),
				str(hovered.get_path()) if hovered != null else "none", JSON.stringify(store_state())])
			if attempt == 2:
				# as a player would: the pointer leaves the power and comes back after the movie had time to finish its animation
				await move_to(window_size() * Vector2(0.5, 0.95))
				await game._wait_seconds(3.0)
		var s3 := store_state()
		print("SPELL3 store: click power %d (%s) at %s: %s -> %s after %d click(s), OnBttnSpell calls %d -> %d (arg '%s'), points %d" % [first + 1,
			st.sciences[first] if first < st.sciences.size() else "?", px, "_active", s3.states[first], attempts, clicks0, int(s3.get("spell_clicks", 0)),
			str(s3.get("spell_arg", "")), s3.points])
		if not picked:
			fails.append("store: a click on power %d (%s) did not pick it" % [first + 1, st.sciences[first] if first < st.sciences.size() else "?"])
			break
	return fails



# ---- review r1: one slot cast by input and its effect judged by the power's class ----------------------------------------------------------------------

## the probes a power is judged on, put down at the aim point by a logged test hook (create_object): two own soldiers (one wounded: the heal's target),
## two enemy soldiers and an enemy economy building (the debuffs' and the damage powers' targets)
func plant_probes(aim: Vector2, spread: float = 1.0) -> Dictionary:
	var me: int = game._local_index
	var foe := -1
	for p in world.get_economy().players:
		if p.playable and p.name == enemy_name():
			foe = int(p.index)
	var probes := {"own": [], "foe": []}
	for i in 2:
		probes.own.append(world.create_object("GondorFighter", me, aim.x + (-30.0 + 20.0 * i) * spread, aim.y + 25.0 * spread, 0.0))
		probes.foe.append(world.create_object("MordorFighter", foe, aim.x + (-30.0 + 20.0 * i) * spread, aim.y - 25.0 * spread, 0.0))
	# every probe stands `spread` times further out (review r1 #1: Lone Tower and the summons are refused within their ForbiddenObjectRange of any soldier or
	# structure (NO_SUMMON_(STRUCTURE_)NEAR_OBJECT_FILTER lists INFANTRY, HERO, CAVALRY ...), so their spot is asked with the probes out of that range
	# around it: still measured as one group at the cast spot, just not on top of it)
	probes.foe.append(world.create_object("MordorSlaughterHouse", foe, aim.x + 60.0 * spread, aim.y - 60.0 * spread, 0.0))
	probes.own.append(world.create_object("GondorFarm", me, aim.x - 60.0 * spread, aim.y + 70.0 * spread, 0.0)) # Rebuild's HealAffects = STRUCTURE
	# a creep (CREEP_OBJECTFILTER: NeutralWarg ...) of the creeps' player, for Untamed Allegiance
	var creeps := -1
	for p in world.get_economy().players:
		if not p.playable and str(p.name).to_lower().contains("creep"):
			creeps = int(p.index)
	probes["creep"] = world.create_object("NeutralWarg", creeps, aim.x + 15.0, aim.y + 10.0, 0.0) if creeps >= 0 else -1 # within Untamed Allegiance's 60
	probes["creep_player"] = creeps
	# the probes are passive targets: NO_ATTACK (the mood scan then gives them no target: RW 0x668A43), so they do not fight each other or anything near
	for id in probes.own + probes.foe + [probes.creep]:
		if int(id) >= 0:
			world.debug_set_object_status(int(id), "NO_ATTACK", true)
	var hurt: Dictionary = world.debug_damage_object(int(probes.own[0]), 60.0, 0)
	var hurt2: Dictionary = world.debug_damage_object(int(probes.own[2]), 400.0, 0)
	print("SPELL3 TEST HOOK: probes at %s: own %s, enemy %s (player %d), creep %d (player %d); wounded: soldier %d -> %s, farm %d -> %s" % [aim, str(probes.own),
		str(probes.foe), foe, int(probes.creep), int(probes.creep_player), int(probes.own[0]), str(hurt.get("health", "?")), int(probes.own[2]), str(hurt2.get("health", "?"))])
	return probes


func probe_health(ids: Array) -> float:
	var h := 0.0
	for id in ids:
		h += float(world.get_object(int(id)).get("health", 0.0))
	return h


func probe_each(ids: Array) -> Array:
	var out := []
	for id in ids:
		out.append(snappedf(float(world.get_object(int(id)).get("health", 0.0)), 0.1))
	return out


func probe_positions(ids: Array) -> Dictionary:
	var at := {}
	for id in ids:
		var o: Dictionary = world.get_object(int(id))
		if o.get("ok", false):
			at[int(id)] = Vector3(float(o.x), float(o.y), float(o.z))
	return at


## the largest distance an object of `at` moved since (a probe that died counts as moved only when it also moved)
func probe_moved(at: Dictionary) -> float:
	var most := 0.0
	for id in at:
		var o: Dictionary = world.get_object(int(id))
		if o.get("ok", false):
			most = maxf(most, Vector2(float(o.x), float(o.y)).distance_to(Vector2(at[id].x, at[id].y)))
	return most


func cancel_targeting_and_clean(probes: Dictionary, at: Vector2) -> void:
	await click(to_pixel(at.x, at.y), MOUSE_BUTTON_RIGHT) # a right click cancels the targeting
	remove_probes(probes)


func remove_probes(probes: Dictionary) -> void:
	for id in probes.own + probes.foe + [probes.get("creep", -1)]:
		if int(id) >= 0 and world.get_object(int(id)).get("ok", false):
			world.destroy_object(int(id))


## the probes alone at `aim` for as long as a cast is judged: the enemy ones must lose nothing (the damage checks measure the cast alone)
func baseline(aim: Vector2) -> String:
	var probes := plant_probes(aim)
	var foe0 := probe_health(probes.foe)
	var at0 := probe_positions([probes.foe[0], probes.foe[1]])
	await game._wait_seconds(7.0)
	var lost := foe0 - probe_health(probes.foe)
	var moved := probe_moved(at0)
	remove_probes(probes)
	await game._step(4)
	print("SPELL3 baseline at %s: the enemy probes lost %.0f and moved %.0f in 7 s without a cast" % [aim, lost, moved])
	return "" if lost == 0.0 and moved < 10.0 else "the aim %s is not quiet: the enemy probes lost %.0f and moved %.0f without a cast" % [aim, lost, moved]


## lane SPELL-4 (review r1 #1): the spot RotWK's location test accepts for the power of slot k, asked of the HUD's own radius cursor WITH the probes standing
## where they are. The slot is pressed, the cursor moved over `spot` (then, when it is red, over a ring around it) and the targeting cancelled with a right
## click (nothing is cast). { ok, spot, refused (the aim was red), cursor (the state over `spot`) }
func accepted_spot(k: int, spot: Vector2, bad: Array = [], force_move: bool = false) -> Dictionary:
	var out := {"ok": false, "spot": spot, "refused": false, "cursor": {}}
	var spx := slot_pixel(k)
	if spx.x < 0:
		return out
	await click(spx)
	await game._step(4)
	if not hud.get_spellbook_state().get("targeting", false):
		return out
	await move_to(to_pixel(spot.x, spot.y))
	await game._step(4)
	var rc: Dictionary = hud.get_spellbook_state().get("radius_cursor", {})
	out.cursor = rc
	if bool(rc.get("valid", true)) and not force_move:
		out.ok = true
	else:
		out.refused = not bool(rc.get("valid", true))
		for radius in [150.0, 250.0, 350.0, 500.0, 700.0, 1000.0, 1400.0]:
			for i in 16:
				var c: Vector2 = spot + Vector2(cos(i * PI / 8.0), sin(i * PI / 8.0)) * radius
				if bad.any(func(b: Vector2) -> bool: return b.distance_to(c) < 100.0):
					continue # a spot where the probes were not quiet (our defences shoot the enemy probes there)
				await move_to(to_pixel(c.x, c.y))
				await game._step(2)
				if bool(hud.get_spellbook_state().get("radius_cursor", {}).get("valid", false)):
					out.spot = c
					out.ok = true
					break
			if out.ok:
				break
	await click(to_pixel(out.spot.x, out.spot.y), MOUSE_BUTTON_RIGHT) # cancel: the cursor goes, nothing is cast
	await game._step(4)
	return out


## the slot k clicked (and the ground at `aim` when it wants a target); the result judged by what the power's class must do:
##   OCLSpecialPower: objects made (the damage OCLs: the enemy probes lose health); PlayerHealSpecialPower: the wounded probe healed;
##   a power with an attribute modifier: victims; the weather powers: the map weather modifier; Devastation: trees felled; Taint / Elven Wood: the area object;
##   PlayerUpgrade: triggered; ProductionSpeedBonus: a bonus; Untamed Allegiance: a creep defected (n/a without creeps); Scavenger: a passive, active
func cast_and_judge(k: int, aim: Vector2) -> Dictionary:
	var me: int = game._local_index
	var state: Dictionary = hud.get_spellbook_state()
	var slots: Array = state.get("slots", [])
	var b := power_of_button(int(state.get("slot_buttons", [])[k]))
	var power: String = str(b.get("power", "?"))
	var r := {"faction": faction, "slot": k + 1, "power": power, "state": slots[k]}
	# review r3: no weather or map-wide weather modifier of an earlier cast lingers into this one (logged test hook); the weather powers must make their own
	var wc: Dictionary = world.debug_clear_weather()
	if not wc.get("ok", false):
		r["result"] = "FAIL"
		r["why"] = "the weather could not be cleared before the cast: %s" % str(wc.get("error", ""))
		return r
	if int(wc.get("weather_before", 0)) != 0 or str(wc.get("modifier_before", "")) != "":
		print("SPELL3 TEST HOOK: weather %d and map modifier '%s' of an earlier cast cleared before %s" % [int(wc.weather_before), str(wc.modifier_before), power])
	await game._step(2)
	var eff0: Dictionary = world.get_spell_power_effect(me, power)
	if int(eff0.get("weather", 0)) != 0 or str(eff0.get("weather_modifier", "")) != "":
		r["result"] = "FAIL"
		r["why"] = "not isolated: the weather %d / map modifier '%s' remain after the clear" % [int(eff0.get("weather", 0)), str(eff0.get("weather_modifier", ""))]
		return r
	r["class"] = eff0.get("class", "?")
	if slots[k] == "_static":
		# NONPRESSABLE powers start through the purchase button's CommandTrigger.
		r["result"] = "ok" if eff0.get("active", false) or int(eff0.get("triggers", 0)) > 0 else "FAIL"
		r["why"] = "passive (_static), active %s, triggers %d" % [str(eff0.get("active", "-")), int(eff0.get("triggers", 0))]
		print("SPELL3 power %s: %s" % [power, r.why])
		return r
	if slots[k] != "_up":
		r["result"] = "FAIL"
		r["why"] = "slot %s (owned %s usable %s ready %s)" % [slots[k], str(b.get("owned")), str(b.get("usable")), str(b.get("ready"))]
		return r
	var trees0: int = world.get_logic_trees(aim.x, aim.y, 180.0).size()
	# review r2: a quiet window before the cast: for 2 s nothing may hurt or move any probe (an earlier cast's leftovers, an attack wave); else the probes
	# are planted again after 5 s, three times at most, then the cast is not judged
	var probes := {}
	var farm_drift := 0.0 # the own farm's health per second without a cast
	var quiet := ""
	var wants_spot: bool = bool(b.get("needs_position", false))
	var accepted := false
	var refused_first := false
	# lane SPELL-4 (review r1 #1): the probes are planted AT the spot the cast will go to, and measured there: when the HUD refuses the aim (red) the
	# accepted spot is found with the probes standing (their structures can be what refuses it), the probes are planted again there and the spot asked
	# once more, up to four rounds; the cast then goes to that same spot
	var aim_first := aim
	var bad_spots: Array = []
	var spread := 1.0
	for spot_round in 12:
		for attempt in 3:
			probes = plant_probes(aim, spread)
			hud.camera_look_at(aim)
			await game._step(8)
			var all_ids: Array = [probes.own[0], probes.own[1]] + probes.foe # the own farm drifts on its own for some factions: measured, not required still
			if spread > 1.0:
				all_ids = [probes.own[0], probes.own[1], probes.foe[0], probes.foe[1]] # the spread-out structures stand wherever they stand: only the soldiers must be quiet
			# Devastation is judged by the trees it fells, at the grove beside our base, where our defences shoot the enemy probes: there the window
			# requires the trees and the own soldiers unchanged
			var by_trees: bool = str(eff0.get("class", "")) == "DevastateSpecialPower"
			if by_trees:
				all_ids = [probes.own[0], probes.own[1]]
			var trees_q0: int = world.get_logic_trees(aim.x, aim.y, 180.0).size()
			var h0 := probe_each(all_ids)
			var farm0 := probe_health([probes.own[2]])
			var at0 := probe_positions(probes.foe.slice(0, 2) + probes.own.slice(0, 2))
			await game._wait_seconds(2.0)
			var h1 := probe_each(all_ids)
			farm_drift = (probe_health([probes.own[2]]) - farm0) / 2.0
			var moved := probe_moved(at0)
			var trees_q1: int = world.get_logic_trees(aim.x, aim.y, 180.0).size()
			if by_trees:
				moved = 0.0 # the enemy probes are not part of its judgement
			quiet = "" if h0 == h1 and moved < 10.0 and trees_q0 == trees_q1 else "the probes changed in the quiet window before the cast: health %s -> %s, moved %.0f, trees %d -> %d" % [
				str(h0), str(h1), moved, trees_q0, trees_q1]
			if quiet == "":
				break
			print("SPELL3 %s: %s (attempt %d)" % [power, quiet, attempt + 1])
			remove_probes(probes)
			await game._wait_seconds(5.0)
		# a tower / barricade is judged by the object it makes (no probe health is read), so an unquiet window does not invalidate it; every other power needs a quiet window
		if quiet != "" and (power.contains("LoneTower") or power.contains("Barricade")):
			print("SPELL4 cursor %s: the probes were not quiet (%s); this power is judged by the object it makes, not by probe health: kept" % [power, quiet])
			quiet = ""
		if quiet != "":
			remove_probes(probes)
			if wants_spot and spot_round < 11:
				# a spot that is not quiet (our defences shoot the enemy probes there): it is skipped and the next accepted spot looked for
				print("SPELL4 cursor %s: the accepted spot %s is not quiet (%s); another one is looked for" % [power, str(aim), quiet])
				bad_spots.append(aim)
				var next: Dictionary = await accepted_spot(k, aim, bad_spots, true)
				if not next.ok:
					r["result"] = "FAIL"
					r["why"] = "no quiet spot the HUD accepts for %s near %s" % [power, str(aim_first)]
					return r
				aim = next.spot
				refused_first = true
				trees0 = world.get_logic_trees(aim.x, aim.y, 180.0).size()
				await game._step(4)
				continue
			r["result"] = "FAIL"
			r["why"] = "not isolated: " + quiet
			return r
		if not wants_spot:
			accepted = true
			break
		var found: Dictionary = await accepted_spot(k, aim, bad_spots)
		if not found.ok:
			# red at every ring spot WITH the probes where they stand: a power whose test needs a qualifying object in its radius (NEEDS_OBJECT_FILTER: Industry,
			# Blight, Snowbind ...) is only valid where the probes are, so the probes walk: planted at each candidate spot in turn and the cursor asked there
			var walked := false
			for radius in [250.0, 400.0, 600.0, 850.0, 1200.0]:
				for i in 12:
					var c: Vector2 = aim_first + Vector2(cos(i * PI / 6.0), sin(i * PI / 6.0)) * radius
					if bad_spots.any(func(b: Vector2) -> bool: return b.distance_to(c) < 100.0):
						continue
					remove_probes(probes)
					probes = plant_probes(c, spread)
					hud.camera_look_at(c)
					await game._step(6)
					var chk: Dictionary = await accepted_spot(k, c)
					if chk.ok and not chk.refused:
						aim = c
						walked = true
						break
				if walked:
					break
			remove_probes(probes)
			if not walked:
				r["result"] = "FAIL"
				r["why"] = "no spot the HUD accepts for %s near %s (the cursor stayed red at every spot tried, with the probes walked to each)" % [power, str(aim_first)]
				return r
			print("SPELL4 cursor %s: the aim %s is red everywhere with the probes in place; the probes walked to the spot %s the HUD accepts" % [power, str(aim_first), str(aim)])
			refused_first = true
			trees0 = world.get_logic_trees(aim.x, aim.y, 180.0).size()
			await game._step(4)
			continue
		if not found.refused:
			accepted = true
			r["cursor"] = found.cursor
			break
		refused_first = true
		if aim != aim_first and spread == 1.0:
			# a spot found with the probes elsewhere is refused again once they stand there: their own structures refuse it. They are spread out and the first
			# aim asked again
			spread = 4.0
			print("SPELL4 cursor %s: %s is refused by the probes themselves (the filter lists soldiers and structures); they are spread %.1fx and the first aim %s asked again" % [power, str(aim), spread, str(aim_first)])
			remove_probes(probes)
			aim = aim_first
			trees0 = world.get_logic_trees(aim.x, aim.y, 180.0).size()
			await game._step(4)
			continue
		print("SPELL4 cursor %s: the aim %s is refused (red); the probes move to the accepted spot %s (round %d)" % [power, str(aim), str(found.spot), spot_round + 1])
		shot("cursor-invalid-" + power)
		remove_probes(probes)
		aim = found.spot
		trees0 = world.get_logic_trees(aim.x, aim.y, 180.0).size()
		await game._step(4)
	if not accepted:
		remove_probes(probes)
		r["result"] = "FAIL"
		r["why"] = "the accepted spot kept moving: the HUD refused %s after the probes were planted at it, twelve rounds" % power
		return r
	r["spot"] = [aim.x, aim.y]
	r["aim_refused_first"] = refused_first
	var own_each0 := probe_each(probes.own)
	var cast_t0 := Time.get_ticks_msec()
	var own0 := probe_health([probes.own[0], probes.own[2]])
	var foe0 := probe_health(probes.foe)
	var foe_at0 := probe_positions([probes.foe[0], probes.foe[1]])
	var mine0 := object_ids(game._local_name)
	var spx := slot_pixel(k)
	if spx.x < 0:
		remove_probes(probes)
		r["result"] = "FAIL"
		r["why"] = "the slot %d was not found" % (k + 1)
		return r
	await click(spx)
	await game._step(4)
	var cast_t := Time.get_ticks_msec()
	if hud.get_spellbook_state().get("targeting", false):
		await move_to(to_pixel(aim.x, aim.y))
		await game._step(4)
		var rc: Dictionary = hud.get_spellbook_state().get("radius_cursor", {})
		print("SPELL4 cursor %s: %s" % [power, JSON.stringify(rc)])
		r["cursor"] = rc
		if not bool(rc.get("valid", true)):
			# the spot was accepted a moment ago with these probes: it must still be (a refused click casts nothing)
			await cancel_targeting_and_clean(probes, aim)
			r["result"] = "FAIL"
			r["why"] = "the accepted spot %s is red again at the cast" % str(aim)
			return r
		shot("cursor-" + power)
		await click(to_pixel(aim.x, aim.y))
		cast_t = Time.get_ticks_msec()
	# the cast's sound (review r1): the audio manager's playing events sampled for 2 s after the cast; the power's InitiateAtLocationSound (or InitiateSound)
	var want_sound: String = str(eff0.get("initiate_at_location_sound", ""))
	if want_sound == "":
		want_sound = str(eff0.get("initiate_sound", ""))
	var heard := false
	if want_sound != "" and game._audio != null:
		# a Multisound plays one or all of its subsounds under their own names
		var names: Array = [want_sound.to_lower()]
		for sub in game._audio.get_event_info(want_sound).get("subsounds", []):
			names.append(str(sub).to_lower())
		for i in 10:
			for n in game._audio.get_playing():
				heard = heard or names.has(str(n).to_lower())
			if heard:
				break
			await game._wait_seconds(0.2)
	r["sound"] = want_sound
	r["sound_playing"] = heard
	# lane SPELL-4: an early look (the heal / leadership glows fade in over 12 drawn frames and Heal's lasts 2.4 s)
	var since := (Time.get_ticks_msec() - cast_t) / 1000.0
	if since < 0.8:
		await game._wait_seconds(0.8 - since)
	shot("effect-early-" + power)
	if world.has_method("get_buff_state"):
		var bs: Dictionary = world.get_buff_state()
		print("SPELL4 buffs after %s: %d shown, %d instance updates, errors %s, first %s" % [power, int(bs.count), int(bs.drawn), JSON.stringify(bs.errors), JSON.stringify(bs.buffs.slice(0, 3))])
	await game._wait_seconds(1.7)
	shot("effect-" + power)
	await game._wait_seconds(4.0) # the volleys' later phases, the eggs' hatching
	var eff: Dictionary = world.get_spell_power_effect(me, power)
	var after: Array = hud.get_spellbook_state().get("slots", [])
	var made: Array = []
	var mine1 := object_ids(game._local_name)
	for id in mine1:
		if not mine0.has(id) and not probes.own.has(id):
			made.append(mine1[id].template)
	var healed := probe_health([probes.own[0], probes.own[2]]) - own0
	var own_each1 := probe_each(probes.own)
	var farm_expected: float = own_each0[2] + farm_drift * float(Time.get_ticks_msec() - cast_t0) / 1000.0
	var damage := foe0 - probe_health(probes.foe)
	var flung := probe_moved(foe_at0) # a shockwave (MetaImpactNugget: Undermine) throws the enemy soldiers; the quiet baseline leaves them standing
	var trees1: int = world.get_logic_trees(aim.x, aim.y, 180.0).size()
	var creep_owner: String = str(world.get_object(int(probes.creep)).get("owner", "")) if int(probes.creep) >= 0 else ""
	remove_probes(probes)
	# review r2: the cast is isolated: what it left (summons, towers, effect objects of the player) goes before the next cast, by the logged test hook
	var cleared := 0
	for id in object_ids(game._local_name):
		if not mine0.has(id) and not (id in probes.own):
			var o: Dictionary = world.get_object(int(id))
			if o.get("ok", false):
				world.destroy_object(int(id)) # not damage: the Army of the Dead takes none, and a dying summon still hurts what stands near
				cleared += 1
	# the cleared objects are gone before the next cast; what a destroyed one leaves (a death OCL) goes too; review r3: a cleanup that does not finish
	# in 15 s fails the cast (the next one would not be isolated)
	var left: Array = []
	for i in 60:
		left = []
		for id in object_ids(game._local_name):
			if not mine0.has(id) and not (id in probes.own):
				left.append(id)
				world.destroy_object(int(id))
		if left.is_empty():
			break
		await game._wait_seconds(0.25)
	await game._wait_seconds(2.0)
	var cls: String = str(eff.get("class", "?"))
	var triggered: bool = int(eff.get("triggers", 0)) > int(eff0.get("triggers", 0))
	var ok := false
	var why := ""
	var stop := ""
	var damage_ocl: bool = bool(eff.get("fires_weapons", false)) # the OCL's object fires weapons (FireWeaponUpdate) or floods: the enemy probes must lose health
	match cls:
		"OCLSpecialPower":
			# a damage OCL must hurt the enemy probes; any other (summons, towers, pings) must leave the player new live objects (a summon's egg hatches)
			var live_made := made.filter(func(t): return t != "SuperweaponPing")
			# a weapon OCL hurts the enemy probes, or (a MetaImpactNugget's shockwave, Undermine) throws them
			ok = int(eff.get("created", 0)) > 0 and ((damage > 0.0 or flung >= 10.0) if damage_ocl else (not live_made.is_empty() or damage > 0.0)) # a bombard's projectiles leave nothing but damage
			why = "made %d object(s) %s, the player's new objects %s; enemy probes lost %.0f, the enemy soldiers thrown %.0f" % [int(eff.get("created", 0)),
				JSON.stringify(eff.get("created_names", [])), JSON.stringify(live_made.slice(0, 4)), damage, flung]
			if int(eff.get("created", 0)) == 0 and int(eff.get("unported", 0)) > int(eff0.get("unported", 0)):
				why += " (an unported create location: stop S-931)"
		"PlayerHealSpecialPower":
			# the soldier (Heal) or the farm (Rebuild: HealAffects STRUCTURE) gains: the farm measured against its own drift without a cast
			ok = triggered and int(eff.get("healed", 0)) > 0 and (own_each1[0] > own_each0[0] or own_each1[2] > farm_expected + 1.0)
			why = "healed %d object(s), the wounded probes (a soldier, a farm) +%.0f (each own probe %s -> %s; the farm without a cast would be %.0f)" % [int(eff.get("healed", 0)), healed, str(own_each0), str(own_each1), farm_expected]
		"DevastateSpecialPower":
			ok = triggered and trees1 < trees0
			why = "harvestable trees within 180: %d -> %d" % [trees0, trees1]
		"CloudBreakSpecialPower":
			ok = triggered and int(eff.get("sunbeams", 0)) > int(eff0.get("sunbeams", 0)) and int(eff.get("weather", 0)) != int(eff0.get("weather", 0))
			why = "weather %d -> %d, sunbeams %d -> %d" % [int(eff0.get("weather", 0)), int(eff.get("weather", 0)), int(eff0.get("sunbeams", 0)), int(eff.get("sunbeams", 0))]
		"DarknessSpecialPower", "FreezingRainSpecialPower":
			# ChangeWeather (Darkness CLOUDY, Freezing Rain RAINY) and, when the power names one, its weather-based attribute modifier on the map
			ok = triggered and int(eff.get("weather", 0)) != 0 and (not bool(eff.get("has_modifier", false)) or str(eff.get("weather_modifier", "")) != "")
			why = "weather %d, map modifier '%s', sunbeams %s" % [int(eff.get("weather", 0)), str(eff.get("weather_modifier", "")), str(eff.get("sunbeams", "-"))]
		"TaintSpecialPower", "ElvenWoodSpecialPower":
			# review r3: the area object is made, but its terrain area's bonuses are not ported (S-856: RW 0xAD49B0 finds no object in a TAINT / ELVEN_WOOD
			# area): reported as that stop, not as a pass
			ok = false
			why = "area object %s, victims %d; the terrain area's bonuses are not ported (stop S-856)" % [str(eff.get("area_object", false)), int(eff.get("victims", 0))]
			if triggered and bool(eff.get("area_object", false)):
				stop = "stop S-856"
		"PlayerUpgradeSpecialPower":
			ok = triggered
			why = "the player upgrade granted (triggers %d)" % int(eff.get("triggers", 0))
		"ProductionSpeedBonus":
			ok = triggered and int(eff.get("bonuses", 0)) > int(eff0.get("bonuses", 0))
			why = "production bonuses %d" % int(eff.get("bonuses", 0))
		"UntamedAllegianceSpecialPower":
			ok = triggered and int(eff.get("defected", 0)) > int(eff0.get("defected", 0)) and creep_owner == game._local_name
			why = "defected %d -> %d; the creep probe's owner now '%s' (the caster '%s')" % [int(eff0.get("defected", 0)), int(eff.get("defected", 0)), creep_owner, game._local_name]
		_:
			ok = triggered and (not bool(eff.get("has_modifier", false)) or int(eff.get("victims", 0)) > int(eff0.get("victims", 0)))
			why = "modifier victims %d -> %d" % [int(eff0.get("victims", 0)), int(eff.get("victims", 0))]
	r["result"] = "ok" if ok else "FAIL"
	if not ok and why.contains("stop S-931"):
		r["result"] = "stop S-931" # the registered gap (Dragon Strike): reported, not counted as a harness failure
	if not ok and stop != "":
		r["result"] = stop
	if not left.is_empty():
		r["result"] = "FAIL"
		why += "; not isolated: %d object(s) of the cast still there after 15 s of cleanup (%s)" % [left.size(), JSON.stringify(left.slice(0, 5))]
	r["why"] = why
	r["made"] = made
	r["recharging"] = k < after.size() and after[k] == "_notReady"
	r["cleared"] = cleared
	print("SPELL3 power %s (%s): %s: %s; slot now %s; sound %s %s" % [power, cls, r.result, why, after[k] if k < after.size() else "?", r.sound,
		"playing" if r.sound_playing else ("NOT playing" if r.sound != "" else "(none in the INI)")])
	return r


func open_store() -> Dictionary:
	var magic := palantir_button("PalantirButtons.Buttons.PlayerMagic")
	if magic.x < 0:
		return {}
	await click(magic)
	for i in 20: # the store's opening animation
		await game._wait_seconds(0.25)
		var st := store_state()
		if st.get("open", false) and st.get("initialized", false):
			break
	await game._wait_seconds(1.0)
	return store_state()


func _scenario_spellbook() -> String:
	var fails: Array = []
	var me: int = game._local_index
	var pts: int = world.debug_add_science_points(me, 200)
	print("SPELL3 TEST HOOK: player %d (%s) given 200 power points -> %d" % [me, faction, pts])
	var base := base_position()
	var enemy := enemy_base()
	hud.camera_look_at(base)
	await game._step(10)
	# ---- the store: buy every power, tier by tier ------------------------------------------------------------------------------------------------------
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	print("SPELL3 store: set %s, points %d, states %s, sciences %s" % [st.get("set", ""), st.points, JSON.stringify(st.states), JSON.stringify(st.sciences)])
	shot("store-open")
	fails.append_array(await buy_all(st))
	var lvl: int = st.level
	shot("store-picked")
	await click(shell_button(lvl, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	if store_state().get("open", false):
		fails.append("ACCEPT did not close the store")
	var sb: Dictionary = world.get_spellbook(me)
	print("SPELL3 store: after ACCEPT the player has %s, points %s" % [JSON.stringify(sb.get("sciences", [])), str(sb.get("points", "?"))])
	# ---- the spell book: every slot ---------------------------------------------------------------------------------------------------------------------
	# a point on land in front of the own base, explored and in view (the middle of a map can be water: Evendim's lake)
	var target := base.lerp(enemy, 0.12)
	# review r2: the aim must be quiet: probes planted there and left alone for the time a cast is judged lose nothing (no fortress arrows, no strays), so the
	# damage a cast is judged on is the cast's
	var grove_at := grove(base)
	var aim_found := false
	var cands: Array = [target, grove_at, base.lerp(enemy, 0.2), base.lerp(enemy, 0.08) + Vector2(250, 0), base.lerp(enemy, 0.08) + Vector2(-250, 0)]
	if OS.get_environment("SPELL4_GROVE_AIM") == "1":
		# lane SPELL-4 (the faction sweep): the grove beside the base first (inland on every faction's start: no water in the powers' radius)
		cands = [grove_at, target, base.lerp(enemy, 0.2), base.lerp(enemy, 0.08) + Vector2(250, 0), base.lerp(enemy, 0.08) + Vector2(-250, 0)]
	if OS.get_environment("SPELL4_OPEN_AIM") == "1":
		# lane SPELL-4 (proof shots): open ground first, the grove (its trees hide the units) last
		cands = [target, base.lerp(enemy, 0.2), base.lerp(enemy, 0.08) + Vector2(250, 0), base.lerp(enemy, 0.08) + Vector2(-250, 0), base.lerp(enemy, 0.3), grove_at]
	for cand in cands:
		if await baseline(cand) == "":
			target = cand
			aim_found = true
			break
	if not aim_found:
		return "no quiet aim point near the base (every candidate's probes were hurt without a cast)"
	print("SPELL3 aim: %s" % target)
	# (Devastation aims at the grove and is judged by the trees it fells, not by damage: the grove needs no baseline)
	for k in 24:
		var state: Dictionary = hud.get_spellbook_state()
		var slots: Array = state.get("slots", [])
		if k >= slots.size() or slots[k] == "_unused":
			break
		var aim := target
		var power: String = str(power_of_button(int(state.get("slot_buttons", [])[k])).get("power", "?"))
		if power.contains("Devastation"):
			aim = grove(base) # Devastation fells trees: the grove of harvestable map trees nearest the base (the logic's tree list, TerrainTrees)
		var r: Dictionary = await cast_and_judge(k, aim)
		results.append(r)
		if r.result == "FAIL":
			fails.append("%s: %s" % [r.power, r.why])
		elif r.result != "ok":
			print("SPELL3 known stop: %s: %s" % [r.power, r.why])
		hud.camera_look_at(base)
		await game._step(6)
	print("SPELL3 TABLE " + JSON.stringify(results))
	return "; ".join(fails)


## lane HUDREDRAW-1: one measured window of `sec` seconds (or `count` render frames when > 0): the frames drawn, fps, the main thread's CPU per frame and the
## shell player's stats over the window
func apt_window(tag: String, sec: float, count: int = 0) -> Dictionary:
	shell.reset_timing()
	var s0: Dictionary = shell.get_stats()
	var f0 := Engine.get_process_frames() # (a headless run draws no frames)
	var t0 := Time.get_ticks_usec()
	var c0: float = game._main_thread_cpu_ms()
	if count > 0:
		for i in count:
			await game.get_tree().process_frame
	else:
		await game._wait_seconds(sec)
	var frames := Engine.get_process_frames() - f0
	var elapsed := (Time.get_ticks_usec() - t0) / 1.0e6
	var cpu: float = (game._main_thread_cpu_ms() - c0) / maxf(frames, 1)
	var s1: Dictionary = shell.get_stats()
	var w := {"window": tag, "frames": frames, "fps": frames / maxf(elapsed, 0.001), "main_cpu_ms_per_frame": cpu,
		"rebuilds": s1.get("rebuilds", 0), "unchanged_frames": int(s1.get("unchanged_frames", 0)) - int(s0.get("unchanged_frames", 0)),
		"list_us": s1.get("list_us", 0.0), "canvas_us": s1.get("canvas_us", 0.0), "submit_us": s1.get("submit_us", 0.0), "last_ops": s1.get("last_ops", 0),
		"canvas_items": s1.get("canvas_items", 0), "apt_notes": [s0.get("apt_notes", 0), s1.get("apt_notes", 0)], "wm_notes": [s0.get("wm_notes", 0),
		s1.get("wm_notes", 0)]}
	if s1.has("partial_frames"):
		w["partial_frames"] = int(s1.partial_frames) - int(s0.get("partial_frames", 0))
	if s1.has("levels"):
		var lv := {}
		var l0: Dictionary = s0.get("levels", {})
		for k in s1.levels:
			var a: Dictionary = s1.levels[k]
			var b: Dictionary = l0.get(k, {})
			lv[k] = {"movie": a.get("movie", ""), "redrawn": int(a.get("redrawn", 0)) - int(b.get("redrawn", 0)), "kept": int(a.get("kept", 0)) - int(b.get("kept", 0)),
				"segments_redrawn": int(a.get("segments_redrawn", 0)) - int(b.get("segments_redrawn", 0)), "segments": int(a.get("segments", 0))}
		w["levels"] = lv
	print("SPELL3 APT %s %s" % [tag, JSON.stringify(w)])
	return w


func _scenario_spellbook_idle() -> String:
	var me: int = game._local_index
	var pts: int = world.debug_add_science_points(me, 200)
	print("SPELL3 TEST HOOK: player %d (%s) given 200 power points -> %d" % [me, faction, pts])
	hud.camera_look_at(base_position())
	await game._step(10)
	await game._wait_seconds(2.0)
	var closed := await apt_window("closed", 10.0)
	shot("idle-closed-steady") # the fps overlay (--fps) after 10 s closed
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	shot("idle-store-open")
	var opened := await apt_window("open", 10.0)
	shot("idle-store-open-steady") # after 10 s open
	print("SPELL3 APT summary: fps closed %.1f open %.1f; main CPU ms/frame closed %.2f open %.2f" % [closed.fps, opened.fps, closed.main_cpu_ms_per_frame,
		opened.main_cpu_ms_per_frame])
	return ""


## lane HUDREDRAW-1 (godot/tests/hudredraw_test.gd): the store held open costs the frame about what the HUD costs closed. Points given (the test hook), 300
## frames closed, the store opened by its Palantir button, 300 frames open. FAIL when the open window's main thread CPU per frame exceeds the closed one's by
## more than --hudredraw-budget (a fraction, default 0.6: a regression guard; a headless run has no canvas submission, the part the retained segments
## save, so its open / closed ratio reads x1.25 - x1.45 with or without them; the windowed measurement is spellbook_idle); when a rebuild of the open window drew everything again (gadgets aside, the list must be planned);
## when the Palantir or the store drew again more than 4 of their segments in a frame on average (the Palantir's pulsing powers button and money counter,
## the store's three looping `_active` buttons; everything else of them is kept); or when the Apt / window manager note lists grew in either window
func _scenario_hudredraw() -> String:
	var budget := 0.6
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--hudredraw-budget="):
			budget = float(a.get_slice("=", 1))
	var me: int = game._local_index
	var pts: int = world.debug_add_science_points(me, 200)
	print("SPELL3 TEST HOOK: player %d (%s) given 200 power points -> %d" % [me, faction, pts])
	hud.camera_look_at(base_position())
	await game._step(10)
	await game._wait_seconds(2.0)
	var pal: int = int(hud.get_spellbook_state().get("palantir_level", -1))
	var fails: Array = []
	var closed := await apt_window("closed", 0.0, 300)
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	var lvl: int = int(st.level)
	var active := 0
	for x in st.get("states", []):
		active += 1 if str(x) == "_active" else 0
	for i in 90: # the store's fade-in, in frames (a wall-clock wait is short of it when frames render slowly: Movie Maker, a loaded host)
		await game.get_tree().process_frame
	var opened := await apt_window("open", 0.0, 300)
	print("SPELL3 hudredraw: Palantir level %d, store level %d (%d powers _active); main CPU ms/frame closed %.3f open %.3f (x%.3f, budget x%.2f)" % [pal, lvl,
		active, closed.main_cpu_ms_per_frame, opened.main_cpu_ms_per_frame, opened.main_cpu_ms_per_frame / maxf(closed.main_cpu_ms_per_frame, 0.001), 1.0 + budget])
	if active == 0:
		fails.append("no store power is _active (nothing loops: the window proves nothing)")
	if opened.main_cpu_ms_per_frame > closed.main_cpu_ms_per_frame * (1.0 + budget): # tolerance: a CPU timing on a shared host; the budget is the claim
		fails.append("the open store costs %.3f ms a frame, closed %.3f (over x%.2f)" % [opened.main_cpu_ms_per_frame, closed.main_cpu_ms_per_frame, 1.0 + budget])
	var levels: Dictionary = opened.get("levels", {})
	for lv in [pal, lvl]:
		var l: Dictionary = levels.get(lv, {})
		if l.is_empty():
			fails.append("no level stats for level %d: %s" % [lv, JSON.stringify(levels)])
			continue
		var per := float(l.segments_redrawn) / maxf(float(l.redrawn), 1.0)
		print("SPELL3 hudredraw: level %d (%s): %d segments; drawn again in %d of %d rebuilds, %.2f segments each" % [lv, l.movie, int(l.segments),
			int(l.redrawn), int(opened.rebuilds), per])
		if per > 4.0: # tolerance: the frames' Apt steps vary with the render rate; the claim is a few segments of the level's 50-odd
			fails.append("level %d drew again %.2f of its %d segments per rebuild" % [lv, per, int(l.segments)])
	var partial := int(opened.get("partial_frames", 0))
	if partial == 0:
		fails.append("no rebuild drew only the changed segments")
	if partial + int(opened.unchanged_frames) != int(opened.rebuilds):
		fails.append("%d of the open window's %d rebuilds drew everything again" % [int(opened.rebuilds) - partial - int(opened.unchanged_frames), int(opened.rebuilds)])
	for w in [closed, opened]:
		if int(w.apt_notes[0]) != int(w.apt_notes[1]) or int(w.wm_notes[0]) != int(w.wm_notes[1]):
			fails.append("the note lists grew in the %s window: apt %s, window manager %s" % [w.window, str(w.apt_notes), str(w.wm_notes)])
	return "; ".join(fails)


## the store's first power, probed: its button clip's states around a mouse press and release (the owner's Angmar store took no click)
func _scenario_storeprobe() -> String:
	var me: int = game._local_index
	world.debug_add_science_points(me, 50)
	print("SPELL3 TEST HOOK: player %d given 50 power points" % me)
	var st := await open_store()
	if not st.get("open", false):
		return "the store did not open"
	var lvl: int = st.level
	var px := shell_button(lvl, "SpellStore.Buttons.Spell1")
	var dump := func(tag: String) -> void:
		var lines: Array = str(shell.dump_tree(lvl, 14)).split("\n")
		var keep: Array = lines.filter(func(l): return l.contains("Spell1") or l.contains("Spell2."))
		print("SPELL3 probe %s: store %s clicks %d; the input %s: %s" % [tag, JSON.stringify(store_state().get("states", []).slice(0, 3)), int(store_state().get("spell_clicks", 0)),
			JSON.stringify(shell.find_button(lvl, "SpellStore.Buttons.Spell1")), "\n  ".join(keep.slice(0, 30))])
	dump.call("open")
	print("SPELL3 probe traces: %s" % " | ".join(store_state().get("traces", [])))
	await move_to(px)
	await game._wait_seconds(0.5)
	dump.call("hover")
	press(px, MOUSE_BUTTON_LEFT, true)
	await game.get_tree().process_frame
	await game.get_tree().process_frame
	dump.call("down")
	press(px, MOUSE_BUTTON_LEFT, false)
	for i in 6:
		await game._wait_seconds(0.25)
		dump.call("up+%d" % i)
	return "" if store_state().get("states", [])[0] != "_active" else "the click did not pick power 1"


## the powers named by --spell3-powers=<SpecialPower>[,...] (bought through the store, cast by the spell book slot), each filmed as a frame sequence:
## spell3-<faction>-seq-<power>-<nn>.png every 0.4 s for 6 s, the camera on the target
func _scenario_sequence() -> String:
	var wanted: Array = []
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--spell3-powers="):
			wanted = Array(a.substr(16).split(",", false))
	if wanted.is_empty():
		return "no --spell3-powers"
	var me: int = game._local_index
	world.debug_add_science_points(me, 200)
	print("SPELL3 TEST HOOK: player %d (%s) given 200 power points" % [me, faction])
	var base := base_position()
	var enemy := enemy_base()
	var st := await open_store()
	if not st.get("open", false):
		return "the store did not open"
	var buy_fails: Array = await buy_all(st)
	await click(shell_button(st.level, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	var fails: Array = buy_fails
	for power in wanted:
		var state: Dictionary = hud.get_spellbook_state()
		var k := -1
		for i in state.get("slots", []).size():
			if str(power_of_button(int(state.slot_buttons[i])).get("power", "")) == power:
				k = i
		if k < 0:
			fails.append("%s: not in the spell book" % power)
			continue
		var aim := base.lerp(enemy, 0.12)
		if power.contains("Devastation"):
			aim = grove(base)
		# lane SPELL-4 (the proof clip, SPELL4_SEQ_CLIP=1): the probes stand at the aim (own wounded soldiers to heal / lift), the cursor glides over them
		# before the click, and the frames come every 0.1 s for 4 s
		var clip := OS.get_environment("SPELL4_SEQ_CLIP") == "1"
		if clip:
			plant_probes(aim)
		hud.camera_look_at(aim)
		await game._step(8)
		await click(slot_pixel(k))
		await game._step(4)
		var frame_n := 0
		if hud.get_spellbook_state().get("targeting", false):
			if clip:
				for i in 12:
					var c := aim + Vector2(-180.0 + 15.0 * i, 60.0 - 5.0 * i)
					await move_to(to_pixel(c.x, c.y))
					await game._wait_seconds(0.1)
					shot("seq-%s-%03d" % [power, frame_n])
					frame_n += 1
			await move_to(to_pixel(aim.x, aim.y))
			await game._step(4)
			shot("seq-%s-cursor" % power)
			await click(to_pixel(aim.x, aim.y))
		if clip:
			for n in 40:
				await game._wait_seconds(0.1)
				shot("seq-%s-%03d" % [power, frame_n])
				frame_n += 1
		else:
			for n in 15:
				await game._wait_seconds(0.4)
				shot("seq-%s-%02d" % [power, n])
		print("SPELL3 sequence %s: slot %d now %s" % [power, k + 1, hud.get_spellbook_state().slots[k]])
	return "; ".join(fails)


## the army sent at the enemy by input, as a player does: Q (SELECT_ALL, the game's key map) selects every unit, the view goes to the enemy's base and a
## right click on the ground there orders the attack
func march() -> void:
	# the camera on the army first: Q takes what the player sees
	var army := local_units()
	var centre := Vector2()
	for u in army:
		centre += Vector2(u.x, u.y)
	centre = centre / maxf(1.0, army.size()) if not army.is_empty() else base_position()
	hud.camera_look_at(centre)
	await game._step(6)
	var key_q := InputEventKey.new()
	key_q.keycode = KEY_Q
	key_q.physical_keycode = KEY_Q
	key_q.pressed = true
	Input.parse_input_event(key_q)
	await game._step(2)
	var up: InputEventKey = key_q.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	await game._step(4)
	# the target (review r2 r3: an army sent at the enemy's base or army lost it three times without the rank): the army holds before our base, its
	# fortress beside it, and attacks the enemy units that come within 1500 of the base; a right click on one is the attack order
	var goal := base_position().lerp(enemy_base(), 0.12)
	var best := 1500.0
	var what := "ground"
	for o in world.get_player_objects(enemy_name()):
		var d := Vector2(o.x, o.y).distance_to(base_position())
		if not o.structure and d < best and float(o.get("health", 1.0)) > 0.0:
			best = d
			goal = Vector2(o.x, o.y)
			what = "%s %d" % [o.template, int(o.id)]
	print("SPELL3 earned: our base %s, the enemy %s base %s, the target %s at %s" % [base_position(), enemy_name(), enemy_base(), what, goal])
	hud.camera_look_at(goal)
	await game._step(6)
	await click(to_pixel(goal.x, goal.y), MOUSE_BUTTON_RIGHT)
	var units := local_units()
	var dist := 0.0
	for u in units:
		dist += Vector2(u.x, u.y).distance_to(goal)
	print("SPELL3 earned: Q selected %d object(s), right click toward the enemy base at %s; %d own units, mean distance %.0f; enemy objects %d; messages %s" % [
		hud.get_selection().size(), goal, units.size(), dist / maxf(1.0, units.size()), world.get_player_objects(enemy_name()).size(),
		JSON.stringify(messages_since(maxi(0, hud.get_message_log().size() - 3)))])
	# the army's state (review r2 r3: the army lost without kills): model conditions that stop the mood scan, the AI states
	var cond_count := {}
	var ai_states := {}
	for u in units:
		var o: Dictionary = world.get_object(int(u.id))
		for c in o.get("conditions", []):
			if str(c) in ["JUST_BUILT", "STONED", "COMING_OUT_OF_FACTORY", "PREPARING"]:
				cond_count[str(c)] = int(cond_count.get(str(c), 0)) + 1
		ai_states[str(o.get("ai_state", "-"))] = int(ai_states.get(str(o.get("ai_state", "-")), 0)) + 1
	print("SPELL3 earned: the army's gate conditions %s, AI states %s" % [JSON.stringify(cond_count), JSON.stringify(ai_states)])
	hud.camera_look_at(base_position())
	await game._step(4)


## review r1: no injected points. PLAY-1's input-driven skirmish (a worker builds a barracks, it trains, the soldiers are sent at the enemy by a drag box and a
## radar right click) plays, the game runs on (four times speed) while the soldiers fight, until the player's power points have grown past what the game
## started with (rank by kills: XP-1's skill points); then the store buys what those points buy by mouse, ACCEPT, and the first bought slot is cast and
## judged (cast_and_judge, probes planted at the aim: the only test hook of this scenario)
func _scenario_earned() -> String:
	var me: int = game._local_index
	var sb0: Dictionary = world.get_spellbook(me)
	print("SPELL3 earned: at the start rank %s, points %s, skill points %s" % [str(sb0.get("rank")), str(sb0.get("points")), str(sb0.get("skill_points"))])
	var sk: String = await _scenario_skirmish()
	print("SPELL3 earned: the skirmish by input: %s" % ("ok" if sk == "" else sk))
	world.set_time_scale(4.0)
	var sb: Dictionary = world.get_spellbook(me)
	var waited := 0.0
	# until the points earned in play alone buy a tier-1 power (5): the purchase must then spend more than the game started with
	# the army strikes the nearest enemy building (kills, then rank) until the player ranked up at least once: points earned in play
	while int(sb.get("points", 0)) <= int(sb0.get("points", 0)) and waited < 900.0:
		if int(waited) % 20 == 0 and local_units().size() > 0:
			await march()
		await game._wait_seconds(5.0)
		waited += 5.0
		sb = world.get_spellbook(me)
		if not sb.get("ok", false) or base_position() == Vector2():
			world.set_time_scale(1.0)
			return "the game ended or the base fell before the points were earned (%.0f s)" % waited
		if int(waited) % 60 == 0:
			print("SPELL3 earned: %.0f s (x4): rank %s, points %s, skill points %s, frame %d" % [waited, str(sb.get("rank")), str(sb.get("points")), str(sb.get("skill_points")), int(world.get_economy().frame)])
	world.set_time_scale(1.0)
	print("SPELL3 earned: after %.0f s: rank %s, points %s, skill points %s" % [waited, str(sb.get("rank")), str(sb.get("points")), str(sb.get("skill_points"))])
	if int(sb.get("points", 0)) <= int(sb0.get("points", 0)):
		return "no power points were earned in %.0f s of play (%s, the game started with %s)" % [waited, str(sb.get("points")), str(sb0.get("points"))]
	var base := base_position()
	hud.camera_look_at(base)
	await game._step(10)
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store"
	shot("earned-store-open")
	var fails: Array = await buy_all(st)
	await click(shell_button(st.level, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	var bought: Dictionary = world.get_spellbook(me)
	var spent: int = int(sb.get("points", 0)) - int(bought.get("points", 0))
	print("SPELL3 earned: bought %s, spent %d of %s points (%s earned in play)" % [JSON.stringify(bought.get("sciences", [])), spent, str(sb.get("points")),
		int(sb.get("points", 0)) - int(sb0.get("points", 0))])
	if spent <= 0:
		fails.append("the purchase spent no points")
	var slots: Array = hud.get_spellbook_state().get("slots", [])
	var k := slots.find("_up")
	if k < 0:
		fails.append("no bought power is ready in the spell book (%s)" % JSON.stringify(slots))
		return "; ".join(fails)
	var r: Dictionary = await cast_and_judge(k, base.lerp(enemy_base(), 0.12))
	results.append(r)
	print("SPELL3 TABLE " + JSON.stringify(results))
	if r.result != "ok":
		fails.append("%s: %s" % [r.power, r.why])
	return "; ".join(fails)


## lane SPELLRC-1: an empty ground pixel near `around` that is not over the GUI (none: x < 0)
func spellrc_ground(around: Vector2) -> Vector2:
	for off in [Vector2(220, 60), Vector2(-220, 60), Vector2(200, -80), Vector2(-200, -80), Vector2(300, 140), Vector2(-300, 140), Vector2(380, -120), Vector2(-380, -120)]:
		var target: Vector2 = around + off
		var w: Vector2 = hud.pixel_to_world(target)
		var clear := w.x > -1.0e8 and on_screen(target)
		for o in world.get_player_objects(game._local_name):
			if Vector2(o.x, o.y).distance_to(w) < 60.0:
				clear = false
		if not clear:
			continue
		await move_to(target)
		await game._step(4)
		if not hud.get_state().get("over_gui", false):
			return target
	return Vector2(-1, -1)


## lane SPELLRC-1: a right click at `p`; the MSG_DO_MOVETO it made
func spellrc_right_click(p: Vector2, what: String) -> int:
	var before := log_count("MSG_DO_MOVETO")
	await click(p, MOUSE_BUTTON_RIGHT)
	await game._step(6)
	var ml: PackedStringArray = hud.get_message_log()
	var moved := log_count("MSG_DO_MOVETO") - before
	print("SPELL3 spellrc: %s: right click at %s: MSG_DO_MOVETO %d, targeting %s; the last messages %s" % [what, p, moved,
		str(hud.get_spellbook_state().targeting), ml.slice(maxi(0, ml.size() - 3))])
	return moved


func _scenario_spellrc() -> String:
	await _headless_window()
	var fails: Array = []
	var me: int = game._local_index
	world.debug_add_science_points(me, 200)
	print("SPELL3 TEST HOOK: player %d (%s) given 200 power points" % [me, faction])
	# a unit of the player, selected by a click
	var unit: Dictionary = {}
	for u in local_units():
		hud.camera_look_at(Vector2(u.x, u.y))
		await game._step(10)
		if on_screen(to_pixel(u.x, u.y)):
			unit = u
			break
	if unit.is_empty():
		var us := local_units()
		return "no local unit on the screen (window %s, %d units%s)" % [window_size(), us.size(), (", the first at pixel %s" % to_pixel(us[0].x, us[0].y)) if not us.is_empty() else ""]
	await click(to_pixel(unit.x, unit.y))
	await game._step(20)
	if hud.get_selection().is_empty():
		return "the left click on the unit selected nothing"
	# the store: every power it sells now; with it open a right click on the ground beside it is still the move order
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	# the first power the points buy (each click is a moment of the clip; buy_all would buy the whole tier)
	var first: int = st.states.find("_active")
	if first < 0:
		return "the store sells nothing (%s)" % str(st.states)
	for attempt in 4:
		await click(shell_button(st.level, "SpellStore.Buttons.Spell%d" % (first + 1)))
		await game._wait_seconds(1.2) # the generic button calls back when its _down animation ends
		if store_state().states[first] != "_active":
			break
	var up := to_pixel(unit.x, unit.y)
	var ground := await spellrc_ground(up)
	if ground.x < 0:
		return "no empty ground beside the store"
	shot("spellrc-store-open")
	await game._wait_seconds(1.0) # (a watchable clip: each step held a moment)
	if await spellrc_right_click(ground, "store open") != 1:
		fails.append("a right click on the ground with the store open did not order the move")
	await click(shell_button(st.level, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	if store_state().get("open", false):
		return "ACCEPT did not close the store"
	# a spell book power that wants a position, pressed: its targeting
	var k := -1
	var state: Dictionary = hud.get_spellbook_state()
	for i in state.get("slots", []).size():
		var b := power_of_button(int(state.slot_buttons[i]))
		if state.slots[i] == "_up" and bool(b.get("needs_position", false)) and k < 0:
			k = i
	if k < 0:
		return "no bought power that wants a position is up on the spell book (%s)" % str(state.get("slots", []))
	var unit_now: Dictionary = world.get_object(int(unit.id))
	up = to_pixel(float(unit_now.get("x", unit.x)), float(unit_now.get("y", unit.y)))
	await click(slot_pixel(k))
	await game._step(6)
	state = hud.get_spellbook_state()
	print("SPELL3 spellrc: slot %d (%s) pressed: targeting %s" % [k + 1, state.get("target_power", ""), str(state.targeting)])
	if not state.targeting:
		return "the spell book slot %d did not start its targeting" % (k + 1)
	ground = await spellrc_ground(up)
	if ground.x < 0:
		return "no empty ground for the right drag"
	shot("spellrc-aiming")
	await game._wait_seconds(1.5)
	# the right drag: the camera scrolls (the LookAt translator's RMB scroll), the power still waits, no order
	var cam0: Vector2 = hud.get_camera().position
	var moves0 := log_count("MSG_DO_MOVETO")
	press(ground, MOUSE_BUTTON_RIGHT, true)
	await game._step(2)
	for i in 10:
		var m := InputEventMouseMotion.new()
		m.position = ground + Vector2(18 * (i + 1), 0)
		m.global_position = m.position
		m.button_mask = MOUSE_BUTTON_MASK_RIGHT
		if OS.get_environment("PLAY1_NO_WARP") != "1":
			Input.warp_mouse(m.position)
		_vp().push_input(m)
		await game._step(2)
	press(ground + Vector2(180, 0), MOUSE_BUTTON_RIGHT, false)
	await game._step(6)
	await game._wait_seconds(1.5)
	var cam1: Vector2 = hud.get_camera().position
	state = hud.get_spellbook_state()
	print("SPELL3 spellrc: right drag: camera %s -> %s (moved %.0f), targeting %s, MSG_DO_MOVETO %d" % [cam0, cam1, cam0.distance_to(cam1), str(state.targeting),
		log_count("MSG_DO_MOVETO") - moves0])
	if cam0.distance_to(cam1) < 1.0:
		fails.append("a right drag with the power aimed did not scroll the camera")
	if not state.targeting:
		fails.append("a right drag cancelled the aimed power (RotWK keeps it: only a click cancels)")
	if log_count("MSG_DO_MOVETO") != moves0:
		fails.append("a right drag ordered a move")
	# a right click: the power is cancelled, nothing is ordered
	if not state.targeting:
		await click(slot_pixel(k))
		await game._step(6)
	unit_now = world.get_object(int(unit.id))
	hud.camera_look_at(Vector2(float(unit_now.get("x", unit.x)), float(unit_now.get("y", unit.y)))) # the drag scrolled the unit away: back to it
	await game._step(10)
	ground = await spellrc_ground(to_pixel(float(unit_now.get("x", unit.x)), float(unit_now.get("y", unit.y))))
	if ground.x < 0:
		return "; ".join(fails + ["no empty ground for the right click"])
	await game._wait_seconds(1.0)
	var moved := await spellrc_right_click(ground, "power aimed")
	if hud.get_spellbook_state().targeting:
		fails.append("a right click did not cancel the aimed power")
	if moved != 0:
		fails.append("the right click that cancelled the power also ordered a move")
	# the next right click orders the move
	shot("spellrc-after-cancel")
	await game._wait_seconds(1.0)
	if await spellrc_right_click(ground, "after the cancel") != 1:
		fails.append("a right click after the cancel did not order the move")
	await game._wait_seconds(3.0) # the unit walks there
	return "; ".join(fails)
