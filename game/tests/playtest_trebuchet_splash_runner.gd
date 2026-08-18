extends SceneTree
## PLAYTEST (Q13 / v0.2.4 alpha): ordinary-weapon projectiles with real flight
## time and retail warhead splash, staged on the REAL selected Men pack.
##
## This is a playtest, not a unit test: it stands a Gondor trebuchet in front of
## a live enemy structure, fires the pack's own authored weapon, and reads what
## the sim did tick by tick.
##
## ORACLES (all read out of the SELECTED pack, never out of the sim):
##   rotwk-men-vslice/a0fde4ac...  data/playable-units/gondortrebuchet.json
##     registration.simulation.resolved.combat:
##       weaponId              GondorTrebuchetRock        (weapon.ini:3110+)
##       projectileObjectId    GondorTrebuchetRockProjectile
##       projectileSpeed       321        (weapon.ini:3118)
##       attackRange           500        GONDOR_TREBUCHET_RANGE
##       minimumAttackRange    300        GONDOR_TREBUCHET_MINRANGE
##       damageComponents      [{value 600, damageType SIEGE, radius 100,
##                               damageTaperOff 50, deathType EXPLODED}]
##       radiusDamageAffects   "ENEMIES NEUTRALS ALLIES"
##   data/playable-structures/gondorfarm.json  ResourceArmor SIEGE = 150%
##   data/playable-units/gondortrebuchet.json  TrebuchetArmor SIEGE = 100%
##
## The taper oracle is the sim-independent retail reading in
## retail_slice_sim._tapered_radius_amount: amount * (1 - taper/100 * d/R).
## This runner recomputes it here, from the pack numbers and the victim's
## position AT THE IMPACT TICK, and compares against the observed health delta.
## Nothing is asserted against a value the sim produced.
##
## Scale is 1.0 so every number printed is a retail source unit.
##
## Invocation:
##   <godot> --headless --path game --script res://tests/playtest_trebuchet_splash_runner.gd
## with OPENBFME_CONTENT pointing at workspace/content-packs.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const SIM_SCRIPT_PATH := "res://src/retail_slice/retail_slice_sim.gd"
const MANIFEST_SCRIPT_PATH := "res://src/retail_slice/retail_faction_manifest.gd"
const SLICE_SCRIPT_PATH := "res://src/retail_slice/retail_vertical_slice.gd"
const MAP_DATA_SCRIPT_PATH := "res://src/retail_slice/retail_map_data.gd"

const SCALE := 1.0
const TREBUCHET_SOURCE_ID := "GondorTrebuchet"
const TOTAL_TICKS := 620
const EXPECTED_CHECKS := 18

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()
var _log: Array[String] = []


func _initialize() -> void:
	_watchdog.start(self, "PLAYTEST_TREBUCHET", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _say(line: String) -> void:
	_log.append(line)
	print(line)


func _run() -> void:
	if OS.get_environment("OPENBFME_PLAYTEST_CAPTURE") == "1":
		await _run_capture()
		return
	if OS.get_environment("OPENBFME_PLAYTEST_BOTMATCH") == "1":
		await _run_botmatch()
		return
	var SimClass: GDScript = load(SIM_SCRIPT_PATH) as GDScript
	var ManifestClass: GDScript = load(MANIFEST_SCRIPT_PATH) as GDScript
	var content_db = root.get_node_or_null("ContentDB")
	if SimClass == null or ManifestClass == null or content_db == null:
		printerr("PLAYTEST_TREBUCHET FAIL cannot load sim/manifest/ContentDB")
		_watchdog.stop()
		quit(1)
		return
	await process_frame
	await process_frame

	var structure_runtimes: Dictionary = (
		content_db.call("get_playable_structure_runtimes")
		if content_db.has_method("get_playable_structure_runtimes")
		else {}
	)
	var unit_runtimes: Dictionary = (
		content_db.call("get_playable_unit_runtimes")
		if content_db.has_method("get_playable_unit_runtimes")
		else {}
	)
	_check(
		"the selected pack ships the Gondor trebuchet playable-unit runtime",
		unit_runtimes.has(TREBUCHET_SOURCE_ID),
		"trebuchet-ish keys: %s" % [unit_runtimes.keys().filter(func(k): return String(k).to_lower().contains("trebuchet"))]
	)
	if not unit_runtimes.has(TREBUCHET_SOURCE_ID):
		_finish()
		return

	var slice = load(SLICE_SCRIPT_PATH).new()
	var map_data = load(MAP_DATA_SCRIPT_PATH).new()
	map_data.local_transform_scale = SCALE
	slice.source_map_data = map_data
	slice._classify_faction_units("men")
	var fieldable: Dictionary = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var manifest: Dictionary = ManifestClass.from_registries("men", fieldable, structure_runtimes)
	var manifest_error := String(manifest.get("_error", ""))
	var builder_unit_rules: Dictionary = {}
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_rule: Dictionary = slice._faction_builder_unit_rule(String(builder_value))
		if not builder_rule.is_empty():
			builder_unit_rules[String(builder_value)] = builder_rule
	slice.free()
	_check("the men faction manifest builds from the selected pack", manifest_error == "", manifest_error)
	if manifest_error != "":
		_finish()
		return

	var sim = SimClass.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"enable_fog_of_war": false,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_unit_rules,
		"starting_resources": 1000000,
		"source_map_transform_scale": SCALE,
	})
	if String(sim.configuration_error) != "":
		_check("the sim configures", false, String(sim.configuration_error))
		_finish()
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	_check("the sim configures on the real men manifest", String(sim.configuration_error) == "", String(sim.configuration_error))

	# ---- read the compiled weapon out of the configured rule (the finding) ----
	var all_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	var trebuchet_object_id := ""
	for key_value in all_rules.keys():
		var key := String(key_value)
		if key.contains("gondor-trebuchet"):
			trebuchet_object_id = key
			break
	if trebuchet_object_id == "":
		_check("the configured unit_rules carry a gondor-trebuchet rule", false, "keys=%d" % all_rules.size())
		_finish()
		return
	var rule: Dictionary = all_rules[trebuchet_object_id] as Dictionary
	var components: Array = rule.get("damage_components", []) as Array
	_say("COMPILED BLOCK %s" % trebuchet_object_id)
	_say("  projectile_object_id = '%s'" % String(rule.get("projectile_object_id", "<absent>")))
	_say("  projectile_speed     = %s (source %s)" % [rule.get("projectile_speed", "<absent>"), rule.get("projectile_speed_source", "<absent>")])
	_say("  attack_range         = %s   minimum_attack_range = %s" % [rule.get("attack_range", "?"), rule.get("minimum_attack_range", "?")])
	_say("  damage_type          = '%s'  radius_damage_affects = '%s'" % [String(rule.get("damage_type", "")), String(rule.get("radius_damage_affects", ""))])
	_say("  member_damage        = %s" % rule.get("member_damage", "?"))
	for component_value in components:
		_say("  component            = %s" % [component_value])
	_check(
		"the shipped trebuchet weapon carries the projectile contract (id + speed)",
		String(rule.get("projectile_object_id", "")) == "GondorTrebuchetRockProjectile"
			and absf(float(rule.get("projectile_speed_source", 0.0)) - 321.0) < 0.001,
		"id='%s' speed_source=%s" % [rule.get("projectile_object_id", "<absent>"), rule.get("projectile_speed_source", "<absent>")]
	)
	var splash_radius := 0.0
	var splash_value := 0.0
	var splash_taper := 0.0
	if not components.is_empty():
		var first := components[0] as Dictionary
		splash_radius = float(first.get("radius", 0.0))
		splash_value = float(first.get("value", 0.0))
		splash_taper = float(first.get("damage_taper_off", 0.0))
	_check(
		"the shipped warhead carries retail splash: 600 SIEGE, radius 100, taper 50",
		components.size() == 1
			and absf(splash_value - 600.0) < 0.001
			and absf(splash_radius - 100.0) < 0.001
			and absf(splash_taper - 50.0) < 0.001,
		"components=%s" % [components]
	)

	# ---- stage the fight -------------------------------------------------
	# Clear every seeded battalion and every seeded structure, then stand ONE
	# enemy farm at the origin. A farm is used because its retail armour row is
	# an unambiguous external oracle: armor.ini ResourceArmor SIEGE = 150%.
	for seeded_id in sim.entity_ids():
		sim.entities.erase(seeded_id)
	for seeded_sid in sim.structure_ids():
		sim.structures.erase(int(seeded_sid))
	sim._structure_footprint_radius_cache.clear()
	sim._note_structure_table_mutation()
	var enemy_max_health: Dictionary = sim.structure_max_health_for_team(1)
	_check(
		"the men manifest ships a farm structure kind with a compiled armour table",
		enemy_max_health.has("farm") and not (sim._structure_armor.get("farm", {}) as Dictionary).is_empty(),
		"kinds=%s armour_kinds=%s" % [enemy_max_health.keys(), sim._structure_armor.keys()]
	)
	if not enemy_max_health.has("farm"):
		_finish()
		return
	var target_structure_id := 9001
	var farm_health: int = int(enemy_max_health["farm"])
	sim.structures[target_structure_id] = {
		"id": target_structure_id,
		"team": 1,
		"kind": "structure",
		"structure_kind": "farm",
		"name": "Farm",
		"position": Vector2.ZERO,
		"rally": Vector2(0.0, 60.0),
		"health": farm_health,
		"maximum_health": farm_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
	}
	sim._note_structure_table_mutation()
	var structure_row: Dictionary = sim.structures[target_structure_id]
	var structure_kind := String(structure_row.get("structure_kind", "?"))
	var structure_max := int(structure_row.get("maximum_health", 0))
	_say("TARGET structure id=%d kind=%s team=%d health=%d footprint=%.2f"
		% [target_structure_id, structure_kind, int(structure_row.get("team", -1)), structure_max,
			sim._target_footprint_radius(target_structure_id, "structure")])

	# Attacker: one Men trebuchet battalion, team 0, 460 source units out (the
	# authored firing band is 300..500 so it fires without moving).
	var attacker_id := 7101
	sim._add_battalion(
		attacker_id, 0, Vector2(-460.0, 0.0), TREBUCHET_SOURCE_ID,
		trebuchet_object_id, String(rule.get("horde_id", trebuchet_object_id)), -1, rule
	)
	# Three enemy battalions. Trebuchets are used as the splash victims on
	# purpose: 1 member x 2000 hp means the splash number is READABLE - an
	# archer would simply lose one 150 hp man to any amount over 150 and the
	# taper would be invisible.
	# The three enemy trebuchets counter-batter the attacker to death by ~tick
	# 200 if left alone (real behaviour, wrong subject). The attacker is pinned
	# indestructible so the playtest can watch several full shot cycles; nothing
	# about its own weapon, flight time or warhead is altered.
	(sim.entities[attacker_id] as Dictionary)["indestructible"] = true
	var near_id := 7201
	var mid_id := 7202
	var far_id := 7203
	sim._add_battalion(near_id, 1, Vector2(0.0, 8.0), TREBUCHET_SOURCE_ID, trebuchet_object_id, String(rule.get("horde_id", trebuchet_object_id)), -1, rule)
	sim._add_battalion(mid_id, 1, Vector2(0.0, 75.0), TREBUCHET_SOURCE_ID, trebuchet_object_id, String(rule.get("horde_id", trebuchet_object_id)), -1, rule)
	sim._add_battalion(far_id, 1, Vector2(0.0, 130.0), TREBUCHET_SOURCE_ID, trebuchet_object_id, String(rule.get("horde_id", trebuchet_object_id)), -1, rule)
	for victim_id in [near_id, mid_id, far_id]:
		var vrow: Dictionary = sim.entities[victim_id]
		vrow["destination"] = Vector2(vrow["position"])
		vrow["stance"] = "HoldGround"
		# Splash dummies only. Left armed, three enemy trebuchets counter-batter
		# the attacker to death by tick ~200 and the playtest never gets past
		# its second shot; that is real behaviour but it is not what is under
		# test here, so the victims are marked noncombatant and simply stand.
		vrow["noncombatant"] = true
	_check(
		"attacker and three enemy battalions are on the field",
		sim.entities.has(attacker_id) and sim.entities.has(near_id) and sim.entities.has(mid_id) and sim.entities.has(far_id),
		"entities=%s" % [sim.entity_ids()]
	)
	if not sim.entities.has(attacker_id):
		_finish()
		return

	var attack_ids: Array[int] = [attacker_id]
	var accepted: int = sim.issue_attack(attack_ids, target_structure_id, 0)
	_check("the attack order on the enemy structure is accepted", accepted == 1, "accepted=%d rejection='%s'" % [accepted, String(sim.last_route_rejection)])

	# ---- run the fight tick by tick -------------------------------------
	var event_cursor := 0
	var launch_tick := -1
	var claimed_impact_tick := -1
	var observed_impact_tick := -1
	var projectile_token := -1
	var launch_count := 0
	var impact_count := 0
	var structure_health_at_launch := -1
	var structure_health_before_impact := -1
	var damage_between_launch_and_impact := 0
	var pre_impact := {}
	var post_impact := {}
	var pre_impact_distance := {}
	var structure_delta := 0

	for _step in range(TOTAL_TICKS):
		var before_structure := int((sim.structures.get(target_structure_id, {}) as Dictionary).get("health", 0))
		var snapshot_positions := {}
		var snapshot_health := {}
		for victim_id in [near_id, mid_id, far_id]:
			if sim.entities.has(victim_id):
				snapshot_positions[victim_id] = Vector2((sim.entities[victim_id] as Dictionary).get("position", Vector2.ZERO))
				snapshot_health[victim_id] = int((sim.entities[victim_id] as Dictionary).get("health", 0))
		var about_to_impact: bool = (
			launch_tick >= 0 and observed_impact_tick < 0
			and claimed_impact_tick >= 0 and sim.tick_index + 1 >= claimed_impact_tick
		)
		if about_to_impact:
			structure_health_before_impact = before_structure
			pre_impact = snapshot_health.duplicate()
			pre_impact_distance = {}
			for victim_id in snapshot_positions.keys():
				pre_impact_distance[victim_id] = Vector2(snapshot_positions[victim_id]).distance_to(Vector2(structure_row.get("position", Vector2.ZERO)))

		sim.tick()

		while event_cursor < sim.events.size():
			var event: Dictionary = sim.events[event_cursor]
			event_cursor += 1
			var kind := String(event.get("kind", ""))
			if int(event.get("entity_id", 0)) != attacker_id:
				continue
			if kind == "combat.projectile_launched":
				launch_count += 1
				_say("LAUNCH tick=%d token=%d claimed_impact_tick=%d projectile='%s'"
					% [int(event.get("tick", -1)), int(event.get("projectile_token", -1)),
						int(event.get("impact_tick", -1)), String(event.get("projectile_object_id", ""))])
				if launch_tick < 0:
					launch_tick = int(event.get("tick", -1))
					projectile_token = int(event.get("projectile_token", -1))
					claimed_impact_tick = int(event.get("impact_tick", -1))
					structure_health_at_launch = int((sim.structures.get(target_structure_id, {}) as Dictionary).get("health", 0))
			elif kind == "combat.projectile_impact":
				impact_count += 1
				_say("IMPACT-EVENT tick=%d token=%d" % [int(event.get("tick", -1)), int(event.get("projectile_token", -1))])
				if observed_impact_tick < 0 and int(event.get("projectile_token", -1)) == projectile_token:
					observed_impact_tick = int(event.get("tick", -1))
					structure_delta = structure_health_before_impact - int((sim.structures.get(target_structure_id, {}) as Dictionary).get("health", 0))
					for victim_id in [near_id, mid_id, far_id]:
						post_impact[victim_id] = int((sim.entities.get(victim_id, {}) as Dictionary).get("health", 0))
					_say("IMPACT tick=%d token=%d" % [observed_impact_tick, projectile_token])

		if launch_tick >= 0 and observed_impact_tick < 0:
			var mid_flight_structure := int((sim.structures.get(target_structure_id, {}) as Dictionary).get("health", 0))
			if mid_flight_structure < structure_health_at_launch:
				damage_between_launch_and_impact = structure_health_at_launch - mid_flight_structure

	# ---- verdicts --------------------------------------------------------
	_check(
		"the trebuchet launched a projectile (combat.projectile_launched fired)",
		launch_tick >= 0,
		"launch_count=%d attacker_state='%s' target=%s"
			% [launch_count, String((sim.entities.get(attacker_id, {}) as Dictionary).get("state", "")),
				(sim.entities.get(attacker_id, {}) as Dictionary).get("target_id", "?")]
	)
	if launch_tick < 0:
		_finish()
		return
	var flight_ticks := claimed_impact_tick - launch_tick
	_say("FLIGHT launch=%d impact=%d ticks=%d (%.2f s at %.3f s/tick)"
		% [launch_tick, observed_impact_tick, flight_ticks, flight_ticks * 0.1, 0.1])
	_check(
		"flight time is real: impact is strictly later than launch",
		flight_ticks >= 1 and observed_impact_tick == claimed_impact_tick,
		"launch=%d claimed_impact=%d observed_impact=%d" % [launch_tick, claimed_impact_tick, observed_impact_tick]
	)
	# 460 source units at speed 321/s, 0.1 s ticks -> ceil(14.33) = 15 ticks.
	_check(
		"flight time matches distance/authored speed (ceil(460/321/0.1) = 15 ticks)",
		flight_ticks == 15,
		"flight_ticks=%d" % flight_ticks
	)
	_check(
		"NO damage landed on the structure between launch and impact",
		damage_between_launch_and_impact == 0,
		"damage seen mid-flight=%d" % damage_between_launch_and_impact
	)

	var structure_expected := int(round(600.0 * 1.5))  # ResourceArmor SIEGE 150%
	_say("STRUCTURE health %d -> %d  delta=%d (expected single hit 600 x 150%% SIEGE = %d)"
		% [structure_health_before_impact, structure_health_before_impact - structure_delta, structure_delta, structure_expected])
	_check(
		"at impact the structure took damage",
		structure_delta > 0,
		"delta=%d" % structure_delta
	)
	_check(
		"the direct-hit structure took the hit ONCE - not double-hit by its own splash",
		structure_delta == structure_expected,
		"delta=%d expected=%d (double would be %d)" % [structure_delta, structure_expected, structure_expected * 2]
	)

	var labels := {near_id: "NEAR", mid_id: "MID", far_id: "FAR"}
	var deltas := {}
	for victim_id in [near_id, mid_id, far_id]:
		var before_health := int(pre_impact.get(victim_id, 0))
		var after_health := int(post_impact.get(victim_id, 0))
		var distance := float(pre_impact_distance.get(victim_id, -1.0))
		var expected := _tapered(splash_value, distance, splash_radius, splash_taper)
		deltas[victim_id] = before_health - after_health
		_say("%s id=%d dist=%.2f  health %d -> %d  delta=%d  expected(taper oracle)=%d"
			% [labels[victim_id], victim_id, distance, before_health, after_health, before_health - after_health, expected])
	var near_delta := int(deltas[near_id])
	var mid_delta := int(deltas[mid_id])
	var far_delta := int(deltas[far_id])
	var near_expected := _tapered(splash_value, float(pre_impact_distance.get(near_id, -1.0)), splash_radius, splash_taper)
	var mid_expected := _tapered(splash_value, float(pre_impact_distance.get(mid_id, -1.0)), splash_radius, splash_taper)

	_check(
		"the NEAR battalion took the near-full nugget damage through TrebuchetArmor SIEGE 100%",
		near_delta == near_expected and near_delta > 0,
		"near_delta=%d expected=%d" % [near_delta, near_expected]
	)
	_check(
		"the MID battalion took a TAPERED, strictly smaller amount",
		mid_delta == mid_expected and mid_delta > 0 and mid_delta < near_delta,
		"mid_delta=%d expected=%d near_delta=%d" % [mid_delta, mid_expected, near_delta]
	)
	_check(
		"the FAR battalion (outside radius 100) took ZERO",
		far_delta == 0,
		"far_delta=%d" % far_delta
	)
	_say("EVENT TOTALS launched=%d impacted=%d over %d ticks" % [launch_count, impact_count, sim.tick_index])
	_say("ATTACKER final health=%s state='%s' target=%s"
		% [(sim.entities.get(attacker_id, {}) as Dictionary).get("health", "<gone>"),
			String((sim.entities.get(attacker_id, {}) as Dictionary).get("state", "")),
			(sim.entities.get(attacker_id, {}) as Dictionary).get("target_id", "?")])
	_say("STRUCTURE final health=%s of %d" % [(sim.structures.get(target_structure_id, {}) as Dictionary).get("health", "<destroyed>"), structure_max])
	_check(
		"the sim ran >= 600 ticks and kept launching and resolving projectiles",
		sim.tick_index >= 600 and launch_count >= 3 and impact_count >= 3,
		"ticks=%d launched=%d impacted=%d in_flight=%d" % [sim.tick_index, launch_count, impact_count, sim.projectiles.size()]
	)
	_finish()


func _tapered(amount: float, distance: float, radius: float, taper_off: float) -> int:
	## Independent restatement of the OpenSAGE DamageTaperOff reading. Kept out
	## of the sim on purpose: the sim must not be its own oracle.
	if amount <= 0.0 or radius <= 0.0 or distance < 0.0 or distance > radius:
		return 0
	var edge_loss: float = clampf(taper_off, 0.0, 100.0) / 100.0
	var multiplier: float = 1.0 - edge_loss * clampf(distance / radius, 0.0, 1.0)
	return maxi(0, int(round(amount * multiplier)))


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"PLAYTEST_TREBUCHET FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
				% [ran, EXPECTED_CHECKS]
		)
	print("PLAYTEST_TREBUCHET_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])


## ---------------------------------------------------------------------------
## PART B - THE CAMERA. Same shot, rendered.
##
## Asserts nothing about damage; Part A above is the proof. This stages a
## trebuchet against a live enemy structure inside the REAL vertical-slice
## scene, lets the scene run at real speed, and writes four PNGs: before the
## shot leaves, mid-flight, at impact, and one second later. Opt in with
## OPENBFME_PLAYTEST_CAPTURE=1 and a non-headless window.
## ---------------------------------------------------------------------------

const CAPTURE_SCENE := "res://scenes/retail_vertical_slice.tscn"


func _capture_dir() -> String:
	var configured := OS.get_environment("OPENBFME_PLAYTEST_CAPTURE_DIR").strip_edges()
	if configured != "":
		return configured.replace("\\", "/")
	return ProjectSettings.globalize_path("res://../workspace/logs/playtest-v024/frames").replace("\\", "/")


func _shoot(label: String, sim_tick: int) -> void:
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if image == null:
		_say("CAPTURE MISS %s (no viewport image)" % label)
		return
	var path := "%s/%s.png" % [_capture_dir(), label]
	var error := image.save_png(path)
	_say("CAPTURE %s tick=%d -> %s (err=%d)" % [label, sim_tick, path, error])


func _run_capture() -> void:
	if DisplayServer.get_name() == "headless":
		_say("CAPTURE SKIPPED: headless display server; Part B needs a window")
		_watchdog.stop()
		quit(0)
		return
	DirAccess.make_dir_recursive_absolute(_capture_dir())
	var size := Vector2i(1600, 900)
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(size)
	root.size = size
	await process_frame
	var packed := load(CAPTURE_SCENE) as PackedScene
	if packed == null:
		_say("CAPTURE FAILED: vertical-slice scene did not load")
		_watchdog.stop()
		quit(1)
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	var ready_frames := 0
	while ready_frames < 4000 and not bool(slice.ready_ok) and String(slice.failure_reason) == "":
		ready_frames += 1
		await process_frame
	if not bool(slice.ready_ok):
		_say("CAPTURE FAILED: slice never became ready: %s" % String(slice.failure_reason))
		_watchdog.stop()
		quit(1)
		return
	_say("CAPTURE slice ready after %d frames" % ready_frames)
	var sim = slice.simulation
	sim.ai_enabled = false
	var scale: float = float(slice.source_map_data.local_transform_scale)

	var cursor := 0
	var target_structure_id := 0
	var player_anchor := Vector2.ZERO
	for sid_value in sim.structure_ids():
		var srow: Dictionary = sim.structure(int(sid_value))
		if int(srow.get("team", -1)) == 1 and target_structure_id == 0:
			target_structure_id = int(sid_value)
		elif int(srow.get("team", -1)) == 0 and player_anchor == Vector2.ZERO:
			player_anchor = Vector2(srow.get("position", Vector2.ZERO))
	if target_structure_id == 0:
		_say("CAPTURE FAILED: the booted slice has no enemy structure to shoot")
		_watchdog.stop()
		quit(1)
		return
	var target_position := Vector2(sim.structure(target_structure_id).get("position", Vector2.ZERO))
	var approach := (player_anchor - target_position)
	if approach.length() < 0.001:
		approach = Vector2(-1.0, 0.0)
	approach = approach.normalized()

	var all_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	var trebuchet_object_id := ""
	var archer_object_id := ""
	for key_value in all_rules.keys():
		var key := String(key_value)
		if trebuchet_object_id == "" and key.contains("gondor-trebuchet"):
			trebuchet_object_id = key
		if archer_object_id == "" and key.contains("gondor-archer"):
			archer_object_id = key
	if trebuchet_object_id == "":
		_say("CAPTURE FAILED: no gondor-trebuchet rule in the booted slice")
		_watchdog.stop()
		quit(1)
		return
	var attacker_id := 7101
	sim._add_battalion(
		attacker_id, 0, target_position + approach * (380.0 * scale), TREBUCHET_SOURCE_ID,
		trebuchet_object_id, String((all_rules[trebuchet_object_id] as Dictionary).get("horde_id", trebuchet_object_id)),
		-1, all_rules[trebuchet_object_id] as Dictionary
	)
	# 380 source units: inside the authored 300..500 firing band AND inside the
	# trebuchet's own 400-unit ShroudClearingRange, so the target it is shooting
	# is actually deshrouded and visible in the frame.
	if archer_object_id != "":
		var archer_rule: Dictionary = all_rules[archer_object_id] as Dictionary
		sim._add_battalion(
			7201, 1, target_position + approach.orthogonal() * (25.0 * scale), "GondorArcherHorde",
			archer_object_id, String(archer_rule.get("horde_id", archer_object_id)), -1, archer_rule
		)
	slice.camera_focus = target_position.lerp(Vector2(sim.entity(attacker_id).get("position", Vector2.ZERO)), 0.5)
	slice.camera_zoom = 1.0
	slice.camera_zoom_target = 1.0
	slice._clamp_camera_focus()
	slice._apply_camera_transform()
	# Let the world stream in and draw before anything is ordered: the first
	# ~2 s of a slice boot render black while terrain and GLBs land, so a
	# "before the shot" frame taken earlier is a picture of nothing. The
	# attacker is held noncombatant across the settle so its authored
	# AutoAcquireEnemiesWhenIdle does not spend the first shot off-camera.
	(sim.entities[attacker_id] as Dictionary)["noncombatant"] = true
	for _settle in 180:
		await process_frame
	await _shoot("01-before-launch", int(sim.tick_index))
	(sim.entities[attacker_id] as Dictionary)["noncombatant"] = false
	cursor = sim.events.size()
	var attack_ids: Array[int] = [attacker_id]
	var accepted: int = sim.issue_attack(attack_ids, target_structure_id, 0)
	_say("CAPTURE staged: attacker=%d target_structure=%d accepted=%d scale=%.6f" % [attacker_id, target_structure_id, accepted, scale])

	var launch_tick := -1
	var impact_tick := -1
	var shot_mid := false
	var shot_impact := false
	var frames := 0
	while frames < 6000:
		frames += 1
		await process_frame
		while cursor < sim.events.size():
			var event: Dictionary = sim.events[cursor]
			cursor += 1
			if int(event.get("entity_id", 0)) != attacker_id:
				continue
			if String(event.get("kind", "")) == "combat.projectile_launched" and launch_tick < 0:
				launch_tick = int(event.get("tick", -1))
				impact_tick = int(event.get("impact_tick", -1))
				_say("CAPTURE launch tick=%d impact tick=%d" % [launch_tick, impact_tick])
		if launch_tick >= 0 and not shot_mid and int(sim.tick_index) >= launch_tick + int(maxi(1, (impact_tick - launch_tick) / 2)):
			shot_mid = true
			await _shoot("02-mid-flight", int(sim.tick_index))
		if impact_tick >= 0 and not shot_impact and int(sim.tick_index) >= impact_tick:
			shot_impact = true
			await _shoot("03-impact", int(sim.tick_index))
		if shot_impact and int(sim.tick_index) >= impact_tick + 10:
			await _shoot("04-one-second-after", int(sim.tick_index))
			break
	if not shot_impact:
		_say("CAPTURE INCOMPLETE: no impact observed in %d frames (launch_tick=%d)" % [frames, launch_tick])
	_say("CAPTURE done at sim tick %d" % int(sim.tick_index))
	_watchdog.stop()
	quit(0)


## ---------------------------------------------------------------------------
## PART C - A SHORT BOT MATCH, so the headline feature can be seen firing in
## free play rather than only in a staged fixture.
##
## Men vs Men, both seats driven by the shipped AI controller, on the real
## booted slice's own gameplay rules and map configuration (the
## retail_ai_ladder_runner construction). Counts what actually happened.
## Opt in with OPENBFME_PLAYTEST_BOTMATCH=1. Asserts nothing; it reports.
## ---------------------------------------------------------------------------

const BOTMATCH_TICKS := 2000
const BOTMATCH_BUILDER := "bfme2.object.men-porter"


func _run_botmatch() -> void:
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	root.size = Vector2i(1920, 1080)
	var SimClass: GDScript = load(SIM_SCRIPT_PATH) as GDScript
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if packed == null or SimClass == null:
		_say("BOTMATCH FAILED: slice scene or sim script did not load")
		_watchdog.stop()
		quit(1)
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	if not bool(slice.ready_ok) or slice.source_map_data == null:
		_say("BOTMATCH FAILED: slice not ready: %s" % String(slice.failure_reason))
		_watchdog.stop()
		quit(1)
		return
	var base_rules: Dictionary = (slice.gameplay_rules as Dictionary).duplicate(true)
	var map_config: Dictionary = slice.source_map_data.simulation_configuration()
	_say("BOTMATCH slice ready; map='%s'" % String(map_config.get("name", map_config.get("map_name", "?"))))

	var sim = SimClass.new()
	sim.configure_team_roster([
		{"team": 0, "faction": "", "is_ai": true, "difficulty": "hard"},
		{"team": 1, "faction": "", "is_ai": true, "difficulty": "hard"},
	])
	var rules := base_rules.duplicate(true)
	rules["enable_base_loop"] = true
	rules["spawn_initial_battalions"] = false
	sim.setup(map_config.duplicate(true), rules)
	sim.ai_enabled = true
	sim._add_battalion(3, 0, sim._builder_spawn_position(0), "Builder-0", BOTMATCH_BUILDER, BOTMATCH_BUILDER, 0)
	sim._add_battalion(103, 1, sim._builder_spawn_position(1), "Builder-1", BOTMATCH_BUILDER, BOTMATCH_BUILDER, 0)

	var started := Time.get_ticks_msec()
	var census: Dictionary = {}
	var cursor := 0
	var trained: Dictionary = {0: 0, 1: 0}
	var built: Dictionary = {0: 0, 1: 0}
	for _step in range(BOTMATCH_TICKS):
		if sim.winner != -1:
			break
		sim.tick()
		while cursor < sim.events.size():
			var event: Dictionary = sim.events[cursor]
			cursor += 1
			var kind := String(event.get("kind", ""))
			census[kind] = int(census.get(kind, 0)) + 1
			var team := int(event.get("team", -1))
			if kind in ["production.unit_completed", "unit.trained", "production.completed"]:
				trained[team] = int(trained.get(team, 0)) + 1
			if kind in ["structure.completed", "construction.completed", "structure.constructed"]:
				built[team] = int(built.get(team, 0)) + 1
	var elapsed_ms: int = maxi(1, Time.get_ticks_msec() - started)
	_say("BOTMATCH ran %d ticks in %.2f s = %.1f ticks/s" % [int(sim.tick_index), float(elapsed_ms) / 1000.0, float(sim.tick_index) * 1000.0 / float(elapsed_ms)])
	for team in [0, 1]:
		var structure_count := 0
		for sid_value in sim.structure_ids(team):
			structure_count += 1
		_say("BOTMATCH team %d: battalions=%d structures=%d resources=%s army_value=%d"
			% [team, sim.living_ids(team).size(), structure_count, sim.resources_for_team(team), _army_value(sim, team)])
	var kinds: Array = census.keys()
	kinds.sort()
	_say("BOTMATCH event census (%d kinds):" % kinds.size())
	for kind_value in kinds:
		_say("  %-46s %d" % [String(kind_value), int(census[kind_value])])
	_say("BOTMATCH PROJECTILES launched=%d impact=%d cancelled=%d structure_launched=%d"
		% [int(census.get("combat.projectile_launched", 0)), int(census.get("combat.projectile_impact", 0)),
			int(census.get("combat.projectile_cancelled", 0)), int(census.get("combat.structure_projectile_launched", 0))])
	_say("BOTMATCH winner=%d" % int(sim.winner))
	slice.queue_free()
	await process_frame
	_watchdog.stop()
	quit(0)


func _army_value(sim, team: int) -> int:
	var total := 0
	for id_value in sim.living_ids(team):
		for value in (sim.entities[int(id_value)] as Dictionary).get("member_health", []):
			total += int(value)
	return total
