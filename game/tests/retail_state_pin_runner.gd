extends SceneTree

## Cross-platform state pin for the GDScript authoritative simulation.
##
## Emits one line:  RETAIL_STATE_PIN ticks=<n> hash=<sha256>
##
## Purpose: the C# engine has CrossPlatformPinTests, which proves its
## fixed-point kernel is bit-identical on Windows and Linux. The GDScript
## simulation - which is the one that actually runs the game - has no such
## proof. Every existing determinism assertion compares two sims inside a
## single process, which cannot detect a platform-dependent result. CI runs
## this runner on Windows and Ubuntu and compares the emitted hash.
##
## The fixture below is DELIBERATELY FROZEN. It duplicates the setup in
## retail_lockstep_determinism_runner.gd rather than sharing it, because a pin
## whose scenario can drift is not a pin - any change to the shared fixture
## would silently change the hash and destroy the comparison's meaning. If this
## fixture must change, treat it as minting a new pin and say so explicitly.
##
## KNOWN COVERAGE GAP - read before trusting a green result.
## This scenario does NOT exercise Godot's AStarGrid2D. The simulation reaches
## pathfinding through an injected `route_provider` (retail_slice_sim.gd
## _query_route); this harness injects none, so routing takes the bounded
## direct fallback. AStarGrid2D uses float32 costs and an internal priority
## queue whose tie-breaking is an implementation detail of the engine, with no
## cross-platform or cross-version stability guarantee - it is the single most
## likely source of divergence and the one the project cannot repair itself.
## A green pin here therefore proves the simulation's own float math, ordering,
## and trigonometry agree across platforms. It proves nothing about pathing.
## Closing that gap needs a second pin backed by a real route_provider.
##
## What this DOES cover: entity and structure state, economy, combat, damage,
## stance and formation changes, production, construction, AI ticks, and the
## six transcendental sites (sin/cos/rotated/angle) reachable from them.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const FactionManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const PIN_TICKS := 3000

## The frozen behavioural value, asserted in-runner.
##
## Until this constant existed the pin proved CROSS-PLATFORM DETERMINISM ONLY.
## CI ran this runner on Windows and Ubuntu and failed if the two disagreed with
## each other - so any change that moved the hash IDENTICALLY on both platforms
## passed silently, and every occurrence of this value in the tree was a comment.
## The property the commit history claims it proves - behavioural stability - was
## being checked by humans reading a printed string.
##
## Both checks are kept because they catch different things: this one fails when
## behaviour moves, the CI comparison fails when the platforms diverge. A change
## can do either without the other.
##
## A DIFFERING HASH IS A DEFECT, NEVER A NEW BASELINE. Re-minting this value is
## the owner's decision alone and must be stated explicitly as minting a new pin,
## exactly as the frozen-fixture note above requires.
##
## ---------------------------------------------------------------------------
## PIN RE-MINTED 2026-08-04 (Round 12 "retail rebase"). THIS IS A CONSCIOUS MINT.
##
## Superseded value: b177804c0457caccc670a6c8e3aa7f2cb74f76f8a1feeef93e3d0128feac0301
## New value:        e5ba939111c0efad0d5d2b581b3fc86bae900a142782126334a33957e00a9f4d
##
## WHY, measured (bisect logs: workspace/scratch/opus18-pin-*.log):
##
## 1. The old value was minted against `retail_slice_sim.gd` at 55f0d874
##    (2026-07-27 07:32) and is exactly reproducible from that file today - the
##    original mint was honest and this harness still measures what it claims.
## 2. It has been RED on `main` itself since the public snapshot a1d207a
##    (2026-08-01): a1d207a's sim.gd hashes faa2b500..., and HEAD c9ca840's
##    hashes e5ba9391.... The red is NOT the working tree - reverting
##    retail_slice_sim.gd to HEAD reproduces e5ba9391 byte for byte, so every
##    uncommitted Round-12 runtime change is pin-NEUTRAL.
## 3. It moved MANY times, not once. Probing the pre-squash checkpoint history
##    yields at least four distinct intermediate values
##    (b177804c -> 425f94ac -> 7ce4e2dd -> faa2b500 -> e5ba9391) across a week in
##    which this file grew 11,696 -> 17,135 lines and gained ~100 functions.
##    That is authored feature work landing, not one regression.
## 4. Part of the drift is STRUCTURAL and unavoidable: the hashed state grew new
##    keys. `unit_damage_components` was added to `_authoritative_state()` and
##    `_state_hash_static_keys()` before a1d207a; `unit_banner_carriers` in
##    c9ca840. Any growth of the hashed state moves this hash even at identical
##    behaviour. (Removing both keys again yields e25bd4a3..., still not the old
##    value, so authored behaviour moved too - both causes are real.)
## 5. Root cause of the SILENCE, now fixed: this runner was CI-only. It was never
##    invoked by `tools/gate-retail.ps1`, so a week of local development never
##    saw it fail. It is wired into that gate as of this change, which is what
##    stops the next week of drift from going unnoticed.
##
## The re-mint records the CURRENT authored behaviour of the simulation. It does
## not endorse the drift retroactively; it restores a live, enforced baseline.
## ---------------------------------------------------------------------------
## ---------------------------------------------------------------------------
## RE-MINT 2026-08-04 (round 18) - ONE cause, bisected, not a blanket refresh.
##
## Superseded value: e5ba939111c0efad0d5d2b581b3fc86bae900a142782126334a33957e00a9f4d
## New value:        a436bb5989a026ee0be6674ac514c1035784dbe6fc92281ddfbb78cc79e0a05a
##
## WHY, measured. Round 18 made two changes to structure pathing. Each was
## reverted ALONE on this tree and this runner re-run
## (workspace/scratch/opus25-retail_state_pin_runner-REVERT-{evict,corridor,all}.out.log):
##
##   revert the bounded-castle CORRIDOR only  -> a436bb59... (unchanged)
##       The corridor is pin-NEUTRAL. Neither pinned scenario issues an attack
##       order onto a castle group inside its tick window.
##   revert the EVICTION only                 -> e5ba9391... (the old value, exactly)
##   revert BOTH                              -> e5ba9391... (the old value, exactly)
##
## So the entire move is `_step_structure_eviction` plus the bounded
## displacement in `_deflect_around_structures`, and it is the pinned SCENARIO
## that moved, not the harness: the old hash was recording battalions clipped
## INSIDE structure footprints. Two defects put them there and kept them there —
## deflection ran only for units with a live route, so a stopped or attacking
## unit was never pushed out at all, and a unit standing exactly on a footprint
## centre was skipped outright by a `distance > 0.001` guard. The new value
## records those units standing outside the walls instead of inside them.
##
## Reproducible: the new hash was measured twice on this tree, identical both
## times (same log). This re-mint changes ONE constant and names the one hunk
## that earned it; nothing else in the pin moved.
## ---------------------------------------------------------------------------
## ---------------------------------------------------------------------------
## ROUND 19: NOT RE-MINTED, and that is a MEASURED result, not an assumption.
##
## Round 19 changed structure pathing again — tangential slide on the travel
## path, a total (rather than per-structure) eviction push budget, and three new
## eviction exemptions (production exit, live engagement, hoisted id list).
## Every hunk was reverted ALONE on this tree and this runner re-run
## (workspace/scratch/opus26-pin-{state,scripted}-{new,revert-*}.out.log):
##
##   revert the tangential SLIDE      -> a436bb59… (unchanged)
##   revert the production-exit SKIP  -> a436bb59… (unchanged)
##   revert the ENGAGEMENT skip       -> a436bb59… (unchanged)
##   revert the total push BUDGET     -> a436bb59… (unchanged)
##   revert ALL FOUR                  -> a436bb59… (unchanged)
##
## WHY IT DID NOT MOVE — measured, because "pin-neutral" is a claim about
## coverage as much as about correctness, and a green pin that never exercised
## the change proves nothing. `OPENBFME_PIN_DUMP` (below) dumps the
## authoritative positions; `OPENBFME_PIN_DUMP_TICK` adds earlier samples.
##
##   AT THE PIN TICK: 4 entities, 0 moved, 0 inside any footprint, in every one
##   of the six variants. The fixture keeps exactly two structures — barracks
##   1003 at (-39, 11) r 2.8 and the constructed farm 3000 at (-28, -8) r 2.6 —
##   and no unit is standing in either at t=3000.
##
##   MID-RUN IT *IS* EXERCISED, and the difference is exactly one eviction step.
##   The queued horde (entity 10) emerges from barracks 1003 around t=210 and
##   its authored doorway is INSIDE that barracks. Against `revert-prodexit`:
##     t=211  penetration 1.7851 vs 1.4351   (0.3500 = STRUCTURE_EVICTION_STEP)
##     t=214  penetration 1.2681 vs 0.9181   (0.3500)
##     t=217  penetration 0.5030 vs 0.1530   (0.3500)
##     t=220  byte-identical, and identical at every later sample
##   The nudge does not accumulate: _step_production_exit rewrites the position
##   from its authored lerp on the next tick, so the trajectory reconverges and
##   the FINAL state — the only state this pin hashes — is untouched.
##
## NAMED COVERAGE GAP, not a clean bill of health. This pin samples t=3000 only,
## so a defect whose whole effect is transient is invisible to it by
## construction. The production-exit defect above was caught by
## banner_castle_sim_runner (`a unit mid production-exit follows the authored
## doorway lerp exactly`, deviation 0.349998 before the fix), not here. Reading
## "the pin did not move" as "nothing changed" would be wrong.
## ---------------------------------------------------------------------------
## ---------------------------------------------------------------------------
## RE-MINT 2026-08-15 - PROVISIONAL CREEP STATE RETIREMENT ONLY.
##
## Superseded value: a436bb5989a026ee0be6674ac514c1035784dbe6fc92281ddfbb78cc79e0a05a
## New value:        0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc
##
## The retired hard-coded creep lane had four inert keys in every authoritative
## snapshot: creep_lairs_enabled=false, creep_lair_placements=[], and the two
## next-creep ID cursors. The selected descriptor/scenario lane supersedes it.
## A scratch-only diagnostic inserted exactly those four keys into the current
## final snapshot and reproduced the superseded hash byte-for-byte. No entity,
## structure, position, economy, command, RNG, or gameplay value differed.
## ---------------------------------------------------------------------------
## ---------------------------------------------------------------------------
## RE-MINT 2026-08-19 - LOCO-B RETAIL HEADING-BOUNDED MOVEMENT.
## OWNER-AUTHORIZED IN THE LOCO-B BRIEF; THIS IS A CONSCIOUS MINT.
##
## Superseded value: 0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc
## New value:        b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58
##
## WHY: `_step_route` used to rotate `facing` by the authored TurnRate but then
## translated along the raw waypoint vector, so hashed positions still snapped
## to the requested direction. LOCO-B translates ground movement along the
## bounded facing instead. Acceleration and braking remain the existing authored
## values; rows with no positive authored rate keep snap/direct movement.
##
## The original LOCO-B measurement was pre-merge and is not accepted as the
## final pin. After merging main c0a4f2c (castle gate discs, fixture ownership,
## formation-modifier plumbing), the merged tree was measured twice from
## scratch. Both runs independently produced the same b025d162... digest:
##   workspace/logs/loco-b-rework-state-pin-measure-1.txt
##   workspace/logs/loco-b-rework-state-pin-measure-2.txt
## Main's additions are therefore measured pin-neutral for this non-castle
## scenario; the old 0e4bcdbf... -> b025d162... movement remains attributable
## solely to LOCO-B's heading-bounded ground translation described above.
## ---------------------------------------------------------------------------
## ---------------------------------------------------------------------------
## RE-MINT 2026-08-22 - CONSTRUCT ARRIVAL AT THE FOOTPRINT EDGE.
## OWNER-DIRECTED BUG HUNT ("things that dont work well"); CONSCIOUS MINT.
##
## Superseded value: b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58
## New value:        2d13b881c59bf5b3707f630878a4308b8cbe947d525e302028da1f2d9433fdc1
##
## WHY: a builder's construct order counted as arrived only with an empty
## route or within a flat 2.0 of the site CENTRE. A route whose last waypoint
## is the centre (no nav-grid obstruction for the fresh site) left the porter
## pressed against the site's own collision disc - placement radius plus its
## body, 5.2 units for a fortress - and stranded it there with the order still
## armed (Angmar porter, second fortress: fortress_command_surface_runner
## angmar 97/2 -> 116/0). Arrival is now `placement radius + 2.0` from the
## centre: retail porters build from the foundation's edge. Porters in the
## pinned scenario therefore begin building a few ticks earlier, which moves
## every downstream construction/economy value in the hash.
##
## Measured twice from scratch on the same tree; both runs produced
## 2d13b881...:
##   workspace/logs/arrival-statepin-1.txt
##   workspace/logs/arrival-statepin-2.txt
## Lockstep 5/0, member combat 115/0, castle AI 104/1 (same named red),
## pathing and projectile pins unchanged on this tree.
## ---------------------------------------------------------------------------
const EXPECTED_HASH := "2d13b881c59bf5b3707f630878a4308b8cbe947d525e302028da1f2d9433fdc1"
const SUBMIT_THROUGH_TICK := 1500


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = _make_sim()
	var accepted := true
	for cmd in _scripted_log():
		if int(cmd["tick"]) <= SUBMIT_THROUGH_TICK:
			accepted = sim.submit_command(cmd) and accepted
	if not accepted:
		printerr("RETAIL_STATE_PIN FAIL a scripted command was rejected during submission")
		quit(1)
		return

	# OPT-IN, HASH-NEUTRAL: an extra positions dump at an arbitrary earlier tick.
	# The pin only ever samples the FINAL state, so a change whose effect is
	# transient — a perturbation that a fixed rally or waypoint later erases —
	# leaves the hash untouched and is invisible here. Measuring the intermediate
	# tick is what turns "the pin did not move" into a statement about WHY.
	var early_ticks: Dictionary = {}
	for value in OS.get_environment("OPENBFME_PIN_DUMP_TICK").strip_edges().split(",", false):
		var early_tick := int(String(value).strip_edges())
		if early_tick > 0:
			early_ticks[early_tick] = true
	for tick in range(1, PIN_TICKS + 1):
		sim.tick()
		if early_ticks.has(tick):
			_dump_positions(sim, ".tick%d" % tick)

	var hash := String(sim.state_hash())
	_dump_positions(sim)
	# Emitted BEFORE the assertion so CI's cross-platform comparison job still
	# has a line to read even on a behavioural failure - the two checks answer
	# different questions and a failure of one must not blind the other.
	print("RETAIL_STATE_PIN ticks=%d hash=%s" % [PIN_TICKS, hash])
	if hash != EXPECTED_HASH:
		printerr(
			(
				"RETAIL_STATE_PIN FAIL behaviour moved: got %s, pinned %s. "
				+ "This is a DEFECT, not a new baseline - something changed the "
				+ "simulation. Re-minting is the owner's decision alone."
			) % [hash, EXPECTED_HASH]
		)
		quit(1)
		return
	print("RETAIL_STATE_PIN OK hash matches the pinned value")
	quit(0)


func _dump_positions(sim, suffix: String = "") -> void:
	## OPT-IN, HASH-NEUTRAL. Writes the authoritative entity positions at the pin
	## tick to `OPENBFME_PIN_DUMP` so a re-mint can be MEASURED — which entities
	## moved, by how far, and whether they were inside a structure footprint
	## before and after — instead of asserted in a comment. Reading state cannot
	## change it; with the variable unset this function does nothing at all.
	var out_path := OS.get_environment("OPENBFME_PIN_DUMP").strip_edges()
	if out_path == "":
		return
	out_path += suffix
	var rows: Array = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		var position := Vector2(row.get("position", Vector2.ZERO))
		var deepest := 0.0
		var inside_id := 0
		for structure_id in sim.structure_ids():
			var structure_row: Dictionary = sim.structures[structure_id]
			if int(structure_row.get("health", 0)) <= 0:
				continue
			if float(structure_row.get("construction_progress", 1.0)) < 1.0:
				continue
			var radius := float(SimScript.STRUCTURE_BLOCK_RADIUS.get(
				String(structure_row.get("structure_kind", "")), 2.8))
			var penetration := radius - position.distance_to(
				Vector2(structure_row.get("position", Vector2.ZERO)))
			if penetration > deepest:
				deepest = penetration
				inside_id = structure_id
		rows.append({
			"id": id,
			"team": int(row.get("team", -1)),
			"x": position.x,
			"y": position.y,
			"state": String(row.get("state", "")),
			"health": int(row.get("health", 0)),
			"inside_structure": inside_id,
			"penetration": deepest,
		})
	# The structures are dumped too, because "no entity is inside a footprint" is
	# only meaningful alongside where the footprints actually are — see the
	# coverage note at the pin constant.
	var structure_rows: Array = []
	for structure_id in sim.structure_ids():
		var structure_row: Dictionary = sim.structures[structure_id]
		var at := Vector2(structure_row.get("position", Vector2.ZERO))
		structure_rows.append({
			"id": structure_id,
			"team": int(structure_row.get("team", -1)),
			"kind": String(structure_row.get("structure_kind", "")),
			"x": at.x,
			"y": at.y,
			"health": int(structure_row.get("health", 0)),
			"construction_progress": float(structure_row.get("construction_progress", 1.0)),
			"block_radius": float(SimScript.STRUCTURE_BLOCK_RADIUS.get(
				String(structure_row.get("structure_kind", "")), 2.8)),
		})
	var file := FileAccess.open(out_path, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify(
			{"ticks": int(sim.tick_index), "entities": rows, "structures": structure_rows}, "\t"))
		file.close()
		print("RETAIL_STATE_PIN dumped positions to ", out_path)


func _make_sim():
	var sim = SimScript.new()
	# Load REAL registries from ContentDB (7 factions, 137 units, 155 structures)
	var tree = self
	var content_db = tree.root.get_node_or_null("ContentDB") if tree != null else null
	var unit_runtimes: Dictionary = {}
	var structure_runtimes: Dictionary = {}
	if content_db != null and content_db.has_method("get_playable_unit_registries"):
		unit_runtimes = content_db.get_playable_unit_registries()
	if content_db != null and content_db.has_method("get_playable_structure_registries"):
		structure_runtimes = content_db.get_playable_structure_registries()
	sim._rules = _harness_rules(unit_runtimes, structure_runtimes)
	sim.setup({}, {})
	if sim.configuration_error != "":
		printerr("RETAIL_STATE_PIN FAIL configuration error: %s" % sim.configuration_error)
		quit(1)
		return sim
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	return sim


func _harness_rules(unit_runtimes: Dictionary = {}, structure_runtimes: Dictionary = {}) -> Dictionary:
	var manifest := FactionManifestScript.from_registries("men", unit_runtimes, structure_runtimes, false)
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"faction_manifest": manifest,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _scripted_log() -> Array[Dictionary]:
	return [
		_command(1, 1, "issue_move", {"ids": [1], "destination": Vector2(-20.0, -14.0)}),
		_command(3, 2, "issue_attack_move", {"ids": [2], "destination": Vector2(0.0, 14.0)}),
		_command(5, 3, "issue_toggle_stance", {"ids": [1]}),
		_command(7, 4, "issue_construct", {"ids": [3], "structure_kind": "farm", "position": Vector2(-28.0, -8.0)}),
		_command(9, 5, "queue_unit", {"producer": 1003, "unit_type": "bfme2.object.gondor-fighter-horde"}),
		_command(15, 6, "issue_attack", {"ids": [1], "target_id": 101}),
		_command(30, 8, "issue_stop", {"ids": [2]}),
		_command(30, 7, "issue_move", {"ids": [2], "destination": Vector2(-5.0, 12.0)}),
		_command(1600, 9, "issue_set_stance", {"ids": [1], "stance": "HoldGround"}),
		_command(2000, 10, "issue_set_formation", {"ids": [2], "formation": "Block"}),
		_command(2500, 11, "issue_stop", {"ids": [1, 2]}),
	]
