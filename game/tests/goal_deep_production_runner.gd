extends SceneTree
## DEEP production matrix — the whole build tree, per faction, on a real sim.
##
## goal_production_matrix_runner.gd proves ONE unit can be queued at ONE
## structure (the seeded fortress) per faction. That leaves the actual product
## claim unproven: every building buildable, and every unit and hero each
## building offers actually producible. This runner closes that gap.
##
## Per faction, on one real sim:
##   1. CONSTRUCT every kind in structure_build_rules_for_team through the real
##      builder flow — a live porter/builder entity, issue_construct, the cost
##      actually deducted, the builder walking to the site, and the authored
##      construction_build_ticks actually elapsing. No debug_finish_team_work,
##      no direct structure injection.
##   2. From every completed structure (plus the seeded fortress), QUEUE AND
##      COMPLETE every unit and hero it offers on its authored command set.
##      Rows the sim refuses with `missing-upgrade` are retried after the
##      required upgrade is PURCHASED through queue_structure_upgrade and
##      ticked to completion — the real purchase path, not a flag flip.
##   3. Assert each produced entity spawns with the right unit_type, the right
##      team, and the authored command-point cost.
##
## FAIL CLOSED. A row that cannot be exercised is never silently skipped: it is
## recorded as a NAMED block with the sim's own refusal reason, and the summary
## enumerates exercised vs blocked per faction. Only rows whose block reason is
## in BLOCK_REASONS_ARE_ENGINE_TRUTH count as named-blocked rather than failed;
## anything else is a failure.
##
## Env:
##   OPENBFME_CONTENT              — content root to mount (as every goal runner)
##   OPENBFME_DEEP_PRODUCTION_OUT  — optional JSON report path
##   OPENBFME_DEEP_PRODUCTION_FACTIONS — optional comma list to narrow the run

const FACTIONS: Array[String] = [
	"men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar",
]
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const PLAYER_TEAM := 0
## Resource and command-point floods, both through the normal rules dictionary
## (the pattern goal_production_matrix_runner already uses). Nothing here
## bypasses a payment: costs are still deducted and asserted, there is simply
## always enough to pay them.
const FLOOD_RESOURCES := 100000000
const FLOOD_COMMAND_POINTS := 100000000

## Bounds. These are watchdogs against a silent hang, not gameplay tuning: a
## builder that has not finished in this many ticks is reported as a named
## block with its live progress, never quietly dropped.
const CONSTRUCTION_TICK_BOUND := 6000
const PRODUCTION_TICK_BOUND := 4000
const UPGRADE_TICK_BOUND := 4000

## Deterministic placement search. issue_construct rejects overlapping
## footprints, so candidate sites are probed with its own dry_run in a fixed
## ring order and the first legal one wins — same order every run.
const PLACEMENT_RING_RADII: Array[float] = [14.0, 20.0, 26.0, 32.0, 38.0, 44.0]
const PLACEMENT_RING_STEPS := 16
## No site may be placed this close to a hostile structure. Defensive
## structures (battle towers, sentry towers) shoot on their own, and one placed
## in range of the enemy fortress ends the match by itself.
const ENEMY_EXCLUSION_RADIUS := 45.0

## A block reason that is engine/retail truth rather than a defect. Everything
## NOT in this table is a hard failure, which is what makes the runner fail
## closed: a new refusal reason cannot quietly become an accepted skip.
const BLOCK_REASONS_ARE_ENGINE_TRUTH := {
	# Retail heroes are one-of-a-kind per player. A hero already fielded from
	# an earlier structure legitimately cannot be produced again; the row is
	# recorded against the second structure naming where it WAS exercised.
	"hero-already-exercised": true,
	# Same rule, but the hero was already on the field before the first tick:
	# the faction's seeded spawn roster fields it at match start, so its
	# production button is correctly dead for the whole match.
	"hero-fielded-at-match-start": true,
	# The seeded fortress already occupies the faction's fortress footprint and
	# retail allows exactly one; re-constructing it is not a product claim.
	"fortress-already-seeded": true,
	# A row in STALE_PACK_ANY_OF_GATE_ROWS: documented, dated, evidence-backed,
	# and self-invalidating (see the conscious-update check in _run).
	"stale-pack-any-of-gate": true,
}

## STALE-PACK ALLOWLIST — now EMPTY. Cleared 2026-08-04 by the retail rebase.
##
## It held 21 rows across six faction packs, all one root cause: those packs
## were cooked from the fan-patched (Unofficial 2.02) tree, whose command
## buttons gate tier-2/tier-3 units on `Upgrade_CustomGenericUpgrade1/2` — the
## CUSTOM_MAPS_UPGRADES block (upgrade.ini:3812) that nothing in a skirmish ever
## grants. The units were unbuildable in the shipped bundles.
##
## PURE RETAIL 2.01 does not author those gates at all. Measured examples:
##   commandbutton.ini:8714-8722  Command_ConstructElvenMirkwoodArcherHorde
##       — no `Options = NEED_UPGRADE`, no `NeededUpgrade`, no
##         `NeededUpgradeAny`: the unit is UNGATED in retail.
##   commandbutton.ini:7997-8001  Command_ConstructIsengardBerserkerHorde
##       — `NeededUpgrade = Upgrade_IsengardUrukPitLevel3` only.
##   commandbutton.ini:6016-6020  Command_ConstructDwarvenZerkerHorde
##       — `NeededUpgrade = Upgrade_DwarvenBarracksLevel3` only.
## `Upgrade_CustomGenericUpgrade1/2` appears on none of them in retail. The
## earlier header here cited `commandbutton.ini:10135`, a LAYERED line, and the
## quoted text still carried the `;,;` patch merge markers.
##
## All 21 rows were re-measured against the freshly cooked pure-retail packs and
## every one now produces. Two caveats recorded rather than hidden:
##   * the two `elves_*` rows are cleared on this runner's own verdict, but the
##     elves faction currently fails `sim_configure` (see below), so they are
##     effectively UNVERIFIED until elves configures again;
##   * elves fails with "Playable-unit runtime 'ElvenRivendellArcherHorde' has
##     an unresolved banner carrier target" — its banner did not convert,
##     because pure retail authors a dangling `ArmorUpgrade` on it. That is a
##     real, open defect, not a pin to absorb.
const STALE_PACK_ANY_OF_GATE_ROWS := {}
## How many authored upgrade steps a single structure may be walked through
## while trying to open its gated production rows. Retail level chains are
## L1->L2->L3 plus research, so this is generous.
const UPGRADE_CHAIN_BOUND := 8

var passed := 0
var failed := 0
var blocked := 0
var report: Dictionary = {"factions": {}, "summary": {}}
## Why each candidate builder could not be trained, for the failure detail.
var _builder_refusals: Array[String] = []
## Stale-pack rows actually observed still closed on this run.
var _observed_stale_rows: Dictionary = {}


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		printerr("DEEP_PRODUCTION FAIL no ContentDB autoload")
		quit(1)
		return
	await process_frame
	await process_frame
	var structure_runtimes: Dictionary = (
		content_db.call("get_playable_structure_runtimes")
		if content_db.has_method("get_playable_structure_runtimes")
		else {}
	)
	var manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	for faction_id in _requested_factions():
		_run_faction(faction_id, structure_runtimes, manifest_script, slice_script)
	# CONSCIOUS UPDATE. A stale-pack row that now opens means the faction pack
	# was re-cooked: the entry must be REMOVED in the same change, exactly like
	# retail_slice_runner.gd's KNOWN_FAILURE_NAMES contract. Only enforced on a
	# full-roster run, since a narrowed run legitimately never reaches most
	# entries.
	if _requested_factions().size() == FACTIONS.size():
		var recovered: Array[String] = []
		for row_value in STALE_PACK_ANY_OF_GATE_ROWS.keys():
			if not _observed_stale_rows.has(String(row_value)):
				recovered.append(String(row_value))
		recovered.sort()
		for row in recovered:
			failed += 1
			printerr(
				"DEEP_PRODUCTION FAIL deep_production_update_allowlist_%s " % row
				+ "(stale-pack row now produces; remove it from "
				+ "STALE_PACK_ANY_OF_GATE_ROWS in the change that re-cooked the pack)"
			)
	report["summary"] = {
		"passed": passed,
		"failed": failed,
		"blocked": blocked,
		"stale_pack_rows_observed": _observed_stale_rows.keys().size(),
		"stale_pack_rows_pinned": STALE_PACK_ANY_OF_GATE_ROWS.size(),
	}
	_write_report()
	_print_faction_table()
	print(
		"DEEP_PRODUCTION_RESULT passed=%d failed=%d named_blocked=%d"
		% [passed, failed, blocked]
	)
	quit(0 if failed == 0 else 1)


func _requested_factions() -> Array[String]:
	var requested := OS.get_environment("OPENBFME_DEEP_PRODUCTION_FACTIONS").strip_edges()
	if requested == "":
		return FACTIONS
	var chosen: Array[String] = []
	for part in requested.split(",", false):
		var name := String(part).strip_edges()
		if name != "" and FACTIONS.has(name):
			chosen.append(name)
	return chosen if not chosen.is_empty() else FACTIONS


func _run_faction(
	faction_id: String,
	structure_runtimes: Dictionary,
	manifest_script,
	slice_script,
) -> void:
	var faction_report: Dictionary = {
		"structures": {},
		"exercised": 0,
		"blocked": 0,
		"failed": 0,
		"blocked_rows": [],
	}
	report["factions"][faction_id] = faction_report

	var slice = slice_script.new()
	var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
	map_data.local_transform_scale = 0.1
	slice.source_map_data = map_data
	slice._classify_faction_units(faction_id)
	var fieldable: Dictionary = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var manifest: Dictionary = manifest_script.from_registries(
		faction_id, fieldable, structure_runtimes
	)
	var manifest_error := String(manifest.get("_error", ""))
	var builder_unit_rules: Dictionary = {}
	if manifest_error == "":
		for builder_value in manifest.get("builder_unit_ids", []) as Array:
			var builder_rule: Dictionary = slice._faction_builder_unit_rule(String(builder_value))
			if not builder_rule.is_empty():
				builder_unit_rules[String(builder_value)] = builder_rule
	slice.free()
	if not _check(faction_id, "manifest", manifest_error == "", manifest_error):
		return

	var sim = SimScript.new()
	# Both rostered teams share one alliance, so `_is_hostile` is false between
	# them. Two consequences, both required here: nothing auto-acquires anything
	# (a growing team-0 army otherwise marches on the enemy fortress and razes
	# it), and `_resolve_victory` never resolves. Measured 2026-08-04 without
	# this: the match ended mid-phase-2 with winner=0, tick() became a no-op and
	# the last row (men workshop trebuchet) reported a phantom 4000-tick hang.
	# A solo roster is not an alternative — one team wins at tick 1 and every
	# queue_unit then answers `match-unavailable`.
	# The alliance is stamped onto the sim's OWN descriptors after the first
	# setup(), never onto hand-written ones: a replacement roster loses the
	# faction binding the manifest installs and the fortress can then train
	# nothing at all (measured: `men_builder_trained` failed outright).
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		# PRODUCTION-ONLY MATCH. Both teams are rostered (a solo roster wins at
		# tick 1 and `queue_unit` then answers `match-unavailable` forever), but
		# neither side is given a starting army. Measured 2026-08-04 with the
		# default rosters: the enemy battalions walk into the base and kill the
		# porter mid-build, which stalls the site permanently (`men_construct_
		# forge` died at progress=0.442). With no armies the two fortresses just
		# stand there and every construction runs to completion. The builder is
		# then produced from the fortress, which is the real retail opening.
		"spawn_initial_battalions": false,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_unit_rules,
		"starting_resources": FLOOD_RESOURCES,
		"command_point_cap": FLOOD_COMMAND_POINTS,
		"source_map_transform_scale": 0.1,
	})
	if not _check(
		faction_id, "sim_configure", String(sim.configuration_error) == "",
		String(sim.configuration_error)
	):
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	if not _check(
		faction_id, "sim_setup", String(sim.configuration_error) == "",
		String(sim.configuration_error)
	):
		return
	## NOTE ON MATCH SHAPE, measured 2026-08-04, so this is not re-litigated:
	## the two teams must stay MUTUALLY HOSTILE. A solo roster and an all-allied
	## roster both end the match at tick 1 — `_resolve_victory` resolves as soon
	## as no two surviving teams are hostile — after which tick() is a no-op and
	## every queue_unit answers `match-unavailable`. What made the match end
	## mid-run instead was construction drifting into weapons range of the enemy
	## fortress; that is fixed at the placement site (see _find_site).

	# Heroes exercised anywhere in this faction's match, so a second offer of
	# the same hero is reported as engine truth rather than a mystery refusal.
	var heroes_exercised: Dictionary = {}
	# Heroes the faction's seeded spawn roster already fields at match start.
	for entity_id in sim.entity_ids():
		var seeded_row: Dictionary = sim.entity(entity_id)
		if int(seeded_row.get("team", -1)) != PLAYER_TEAM:
			continue
		var seeded_type := String(seeded_row.get("unit_type", ""))
		var seeded_rule: Dictionary = sim._unit_production_rules.get(seeded_type, {})
		if String(seeded_rule.get("category", "")) == "hero":
			heroes_exercised[seeded_type] = "@match-start-roster"

	# PHASE 1 — construct every buildable kind through the real builder flow,
	# BEFORE any army exists. Construction needs the builder to stand at the
	# site in `state == "construct"` for the authored number of ticks;
	# interleaving unit production first puts both sides' armies on the field
	# and a porter that gets pulled off its site stalls the build forever.
	var fortress := int(sim.fortress_id(PLAYER_TEAM))
	var built: Array = [{"kind": "fortress", "id": fortress}]
	if not _check(
		faction_id, "fortress_seeded", fortress != 0, "fortress_id returned 0"
	):
		return
	# PHASE 0 — the real opening: the fortress trains the faction's builder.
	_builder_refusals.clear()
	if not _check(
		faction_id, "builder_trained", _train_builder(sim, manifest, fortress),
		"no manifest builder unit (%s) could be trained at the fortress: %s" % [
			manifest.get("builder_unit_ids", []), _builder_refusals
		]
	):
		return
	var build_rules: Dictionary = sim.structure_build_rules_for_team(PLAYER_TEAM)
	var kinds: Array[String] = []
	for kind_value in build_rules.keys():
		kinds.append(String(kind_value))
	kinds.sort()
	var seeded_kinds: Array = sim.seed_structure_kinds_for_team(PLAYER_TEAM)
	for kind in kinds:
		if seeded_kinds.has(kind):
			_block(
				faction_id, "construct_%s" % kind, "fortress-already-seeded",
				"seeded at match start (seed kinds: %s)" % [seeded_kinds],
				faction_report
			)
			continue
		var structure_id := _construct(sim, faction_id, kind, faction_report)
		if structure_id != 0:
			built.append({"kind": kind, "id": structure_id})
		# The match must still be live. A resolved match makes tick() a no-op
		# and every later production row would fail as an unexplained hang
		# instead of naming the construction that ended it.
		if not _check(
			faction_id, "match_live_after_construct_%s" % kind, int(sim.winner) == -1,
			"winner=%d after constructing %s; enemy fortress %d health=%s" % [
				int(sim.winner), kind, int(sim.fortress_id(1)),
				(sim.structure(int(sim.fortress_id(1))) as Dictionary).get("health", "absent"),
			]
		):
			break

	# PHASE 2 — every unit and hero every standing structure offers.
	for entry_value in built:
		var entry: Dictionary = entry_value
		if int(entry.get("id", 0)) == 0:
			continue
		_exercise_structure(
			sim, faction_id, int(entry["id"]), String(entry["kind"]),
			faction_report, heroes_exercised
		)


func _construct(sim, faction_id: String, kind: String, faction_report: Dictionary) -> int:
	## Real builder flow: a live builder entity, a legal site, the authored
	## cost deducted, and the authored construction ticks actually elapsing.
	var key := "construct_%s" % kind
	var builder := _builder_entity(sim)
	if builder == 0:
		_fail(faction_id, key, "no living builder entity on team 0", faction_report)
		return 0
	var position: Variant = _find_site(sim, kind)
	if position == null:
		_fail(
			faction_id, key,
			"no legal site found in %d probed positions" % [
				PLACEMENT_RING_RADII.size() * PLACEMENT_RING_STEPS
			],
			faction_report
		)
		return 0
	var resources_before := int(sim.resources_for_team(PLAYER_TEAM))
	var result: Dictionary = sim.issue_construct(
		[builder] as Array[int], kind, position, false, PLAYER_TEAM
	)
	if not bool(result.get("ok", false)):
		_fail(
			faction_id, key,
			"issue_construct refused: %s" % String(result.get("reason", "?")),
			faction_report
		)
		return 0
	var structure_id := int(result.get("structure_id", 0))
	var cost := int(result.get("cost", 0))
	var build_ticks := int(result.get("build_ticks", 0))
	if not _check(
		faction_id, "%s_cost_paid" % key,
		int(sim.resources_for_team(PLAYER_TEAM)) == resources_before - cost,
		"expected %d, got %d (cost %d)" % [
			resources_before - cost, int(sim.resources_for_team(PLAYER_TEAM)), cost
		]
	):
		return 0
	var start_tick := int(sim.tick_index)
	var ticked := 0
	while ticked < CONSTRUCTION_TICK_BOUND:
		sim.tick()
		ticked += 1
		if float((sim.structure(structure_id) as Dictionary).get("construction_progress", 0.0)) >= 1.0:
			break
	var row: Dictionary = sim.structure(structure_id)
	var progress := float(row.get("construction_progress", 0.0))
	if progress < 1.0:
		_fail(
			faction_id, key,
			"construction stalled at progress=%.3f after %d ticks (builder %d, elapsed=%d/%d)" % [
				progress, ticked, builder,
				int(row.get("construction_elapsed_ticks", 0)),
				int(row.get("construction_build_ticks", 0)),
			],
			faction_report
		)
		return 0
	# Build TIME honored: the site cannot have completed in fewer ticks than
	# its own authored construction_build_ticks, because the builder also has
	# to walk there first.
	var elapsed := int(sim.tick_index) - start_tick
	_check(
		faction_id, "%s_build_time_honored" % key, elapsed >= build_ticks,
		"completed in %d ticks but authored build_ticks=%d" % [elapsed, build_ticks]
	)
	_check(
		faction_id, "%s_owned_by_builder_team" % key,
		int(row.get("team", -1)) == PLAYER_TEAM and int(row.get("health", 0)) > 0,
		"team=%s health=%s" % [row.get("team", "?"), row.get("health", "?")]
	)
	return structure_id


func _find_site(sim, kind: String):
	## Deterministic ring probe around the team's OWN fortress, using
	## issue_construct's own dry_run legality check so the site this runner
	## picks is a site the sim accepts.
	##
	## Centring on the fortress rather than on the builder is load bearing. The
	## builder ends each job standing at the site it just finished, so a
	## builder-centred probe walks the base steadily across the map; measured
	## 2026-08-04 that put a men `battle_tower` inside weapons range of the
	## enemy fortress, the tower razed it over the following ~15k ticks, the
	## match resolved with winner=0 mid-production, and the final row reported a
	## phantom hang because tick() had quietly become a no-op. Sites are also
	## held clear of every hostile structure by ENEMY_EXCLUSION_RADIUS.
	var origin := Vector2(
		(sim.structure(int(sim.fortress_id(PLAYER_TEAM))) as Dictionary).get(
			"position", Vector2.ZERO
		)
	)
	var builder := _builder_entity(sim)
	for radius in PLACEMENT_RING_RADII:
		for step in range(PLACEMENT_RING_STEPS):
			var angle := TAU * float(step) / float(PLACEMENT_RING_STEPS)
			var candidate := origin + Vector2(cos(angle), sin(angle)) * radius
			if _too_close_to_hostile(sim, candidate):
				continue
			var probe: Dictionary = sim.issue_construct(
				[builder] as Array[int], kind, candidate, true, PLAYER_TEAM
			)
			if bool(probe.get("ok", false)):
				return candidate
	return null


func _too_close_to_hostile(sim, candidate: Vector2) -> bool:
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if int(row.get("team", -1)) == PLAYER_TEAM:
			continue
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(candidate) < ENEMY_EXCLUSION_RADIUS:
			return true
	return false


func _train_builder(sim, manifest: Dictionary, fortress: int) -> bool:
	## Produce one of the manifest's builder units at the fortress and tick it
	## out. No injection: this is the same queue_unit path every other row uses.
	if _builder_entity(sim) != 0:
		return true
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_type := String(builder_value)
		var queued: Dictionary = sim.queue_unit(PLAYER_TEAM, fortress, builder_type)
		if not bool(queued.get("ok", false)):
			_builder_refusals.append("%s: %s" % [builder_type, queued.get("reason", "?")])
			continue
		var ticked := 0
		while ticked < PRODUCTION_TICK_BOUND:
			sim.tick()
			ticked += 1
			if sim.production_queue_state(fortress).is_empty():
				break
		if _builder_entity(sim) != 0:
			return true
		_builder_refusals.append(
			"%s: queued ok but no builder entity appeared (queue still %d, winner=%d)"
			% [builder_type, sim.production_queue_state(fortress).size(), int(sim.winner)]
		)
	return false


func _builder_entity(sim) -> int:
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entity(entity_id)
		if (
			int(row.get("team", -1)) == PLAYER_TEAM
			and int(row.get("health", 0)) > 0
			and bool(row.get("is_builder", false))
		):
			return int(entity_id)
	return 0


func _exercise_structure(
	sim,
	faction_id: String,
	structure_id: int,
	kind: String,
	faction_report: Dictionary,
	heroes_exercised: Dictionary,
) -> void:
	## Queue and complete EVERY unit and hero this structure offers.
	var offered: Array = Array((sim.structure(structure_id) as Dictionary).get("production", []))
	var unit_types: Array[String] = []
	for offered_value in offered:
		unit_types.append(String(offered_value))
	unit_types.sort()
	(faction_report["structures"] as Dictionary)[kind] = {
		"structure_id": structure_id,
		"offered": unit_types.size(),
		"rows": {},
	}
	if unit_types.is_empty():
		# Not every structure produces units (walls, statues, wells, tech
		# buildings). That is authored truth, and it is recorded rather than
		# skipped.
		((faction_report["structures"] as Dictionary)[kind] as Dictionary)["rows"] = {
			"_none": "structure offers no production rows",
		}
		return
	# Rows still closed by an authored gate after this pass. The gate is opened
	# by PURCHASING the structure's authored upgrade chain, one real step at a
	# time, and every newly opened row is then exercised.
	var pending: Array[String] = []
	for unit_type in unit_types:
		if not _exercise_row(
			sim, faction_id, structure_id, kind, unit_type, faction_report, heroes_exercised
		):
			pending.append(unit_type)
	var steps := 0
	while not pending.is_empty() and steps < UPGRADE_CHAIN_BOUND:
		var bought := _purchase_next_chain_step(sim, faction_id, structure_id, kind)
		if bought == "":
			break
		steps += 1
		var still_pending: Array[String] = []
		for unit_type in pending:
			if not _exercise_row(
				sim, faction_id, structure_id, kind, unit_type, faction_report,
				heroes_exercised
			):
				still_pending.append(unit_type)
		pending = still_pending
	# Anything still closed after the whole authored upgrade surface was bought.
	for unit_type in pending:
		var final: Dictionary = sim.queue_unit(PLAYER_TEAM, structure_id, unit_type)
		var key := "%s_%s" % [kind, _short(unit_type)]
		var detail := (
			"row stayed closed after purchasing %d authored upgrade step(s); " % [steps]
			+ "queue_unit: %s%s" % [
				String(final.get("reason", "?")),
				" (needs %s; structure sells %s)" % [
					sim.unlock_upgrades_for_unit(unit_type, kind),
					_offered_upgrade_ids(sim, structure_id),
				] if final.has("required_upgrade") else "",
			]
		)
		var qualified := "%s_%s" % [faction_id, key]
		if STALE_PACK_ANY_OF_GATE_ROWS.has(qualified):
			_observed_stale_rows[qualified] = true
			_block(
				faction_id, key, "stale-pack-any-of-gate", detail, faction_report
			)
		else:
			_fail(faction_id, key, detail, faction_report)


func _exercise_row(
	sim,
	faction_id: String,
	structure_id: int,
	kind: String,
	unit_type: String,
	faction_report: Dictionary,
	heroes_exercised: Dictionary,
) -> bool:
	## Returns true once this row is resolved (exercised, or named-blocked as
	## engine truth, or failed). Returns FALSE only when the row is still shut
	## behind an authored production gate, which is the caller's signal to buy
	## the next real upgrade step and try again.
	var key := "%s_%s" % [kind, _short(unit_type)]
	if heroes_exercised.has(unit_type):
		var where := String(heroes_exercised[unit_type])
		if where == "@match-start-roster":
			_block(
				faction_id, key, "hero-fielded-at-match-start",
				"the faction's seeded spawn roster already fields this "
				+ "one-of-a-kind hero, so its button is correctly dead",
				faction_report
			)
		else:
			_block(
				faction_id, key, "hero-already-exercised",
				"one-of-a-kind hero already produced at '%s' this match" % [where],
				faction_report
			)
		return true
	var before_ids: Array = sim.entity_ids()
	var command_points_before := int(sim.command_points_for_team(PLAYER_TEAM))
	var queued: Dictionary = sim.queue_unit(PLAYER_TEAM, structure_id, unit_type)
	if (
		not bool(queued.get("ok", false))
		and String(queued.get("reason", "")) == "missing-upgrade"
	):
		# Authored production gate. Do NOT chase the single id the sim names:
		# it reports the first member of the ALL-of/ANY-of closure, and for
		# BFME2 tier-2/3 rows that is `Upgrade_CustomGenericUpgrade1`, a
		# custom-map upgrade no structure sells. The pack authors the row's
		# real closure as prerequisiteAnyOf (verified against commandbutton.ini
		# NeededUpgrade, e.g. `Upgrade_GondorStableLevel2 Upgrade_StructureLevel2
		# Upgrade_CustomGenericUpgrade1`), and the structure's own level chain
		# satisfies it. Hand the row back so the caller buys that chain.
		return false
	if not bool(queued.get("ok", false)):
		_fail(
			faction_id, key,
			"queue_unit refused: %s%s" % [
				String(queued.get("reason", "?")),
				" (%s)" % String(queued.get("required_upgrade", "")) if queued.has("required_upgrade") else "",
			],
			faction_report
		)
		return true
	var item: Dictionary = queued.get("item", {})
	var authored_command_points := int(item.get("command_points", 0))
	var complete_tick := int(item.get("complete_tick", int(sim.tick_index) + 1))
	# The bound follows the row's OWN authored completion tick (plus slack for
	# the spawn/exit step), so a legitimately slow unit is not reported as a
	# hang; PRODUCTION_TICK_BOUND is only the floor.
	var bound := maxi(PRODUCTION_TICK_BOUND, complete_tick - int(sim.tick_index) + 600)
	var ticked := 0
	while ticked < bound:
		sim.tick()
		ticked += 1
		if sim.production_queue_state(structure_id).is_empty():
			break
	var spawned := 0
	for entity_id in sim.entity_ids():
		if before_ids.has(entity_id):
			continue
		var row: Dictionary = sim.entity(entity_id)
		if String(row.get("unit_type", "")) == unit_type:
			spawned = int(entity_id)
			break
	if spawned == 0:
		_fail(
			faction_id, key,
			"queued ok but no entity of unit_type '%s' spawned: ticked %d/%d, "
			% [unit_type, ticked, bound]
			+ "authored complete_tick=%d, sim tick=%d, queue still holds %d row(s), "
			% [complete_tick, int(sim.tick_index), sim.production_queue_state(structure_id).size()]
			+ "winner=%d producer_health=%s clock_paused=%s" % [
				int(sim.winner),
				(sim.structure(structure_id) as Dictionary).get("health", "?"),
				sim.clock_paused,
			],
			faction_report
		)
		return true
	var spawned_row: Dictionary = sim.entity(spawned)
	_check(
		faction_id, "%s_unit_type" % key,
		String(spawned_row.get("unit_type", "")) == unit_type,
		"got '%s'" % String(spawned_row.get("unit_type", ""))
	)
	_check(
		faction_id, "%s_team" % key,
		int(spawned_row.get("team", -1)) == PLAYER_TEAM,
		"got team %s" % [spawned_row.get("team", "?")]
	)
	var command_points_after := int(sim.command_points_for_team(PLAYER_TEAM))
	_check(
		faction_id, "%s_command_points" % key,
		command_points_after - command_points_before == authored_command_points,
		"cp delta %d, authored %d" % [
			command_points_after - command_points_before, authored_command_points
		]
	)
	((faction_report["structures"] as Dictionary)[kind] as Dictionary)["rows"][unit_type] = {
		"entity": spawned,
		"command_points": authored_command_points,
		"ticks": ticked,
	}
	var production_rule: Dictionary = sim._unit_production_rules.get(unit_type, {})
	if String(production_rule.get("category", "")) == "hero":
		heroes_exercised[unit_type] = kind
	return true


func _offered_upgrade_ids(sim, structure_id: int) -> Array[String]:
	var ids: Array[String] = []
	for command in sim.structure_upgrade_commands(structure_id):
		ids.append(String((command as Dictionary).get("upgrade_id", "")))
	return ids


func _purchase_next_chain_step(sim, faction_id: String, structure_id: int, kind: String) -> String:
	## Buy the next authored upgrade step this structure is actually selling,
	## through queue_structure_upgrade, and tick it to completion. Returns the
	## purchased upgrade id, or "" when the structure has nothing purchasable
	## left. Steps whose own authored gate is unmet are skipped this round —
	## buying an earlier step is what opens them.
	for command_value in sim.structure_upgrade_commands(structure_id):
		var command: Dictionary = command_value
		if command.has("gate_satisfied") and not bool(command["gate_satisfied"]):
			continue
		var upgrade_id := String(command.get("upgrade_id", ""))
		if upgrade_id == "":
			continue
		var result: Dictionary = sim.queue_structure_upgrade(
			PLAYER_TEAM, structure_id, upgrade_id
		)
		if not bool(result.get("ok", false)):
			continue
		var ticked := 0
		while ticked < UPGRADE_TICK_BOUND:
			sim.tick()
			ticked += 1
			if sim.structure_upgrade_queue_state(structure_id).is_empty():
				break
		var completed: Array = Array(
			(sim.structure(structure_id) as Dictionary).get("completed_upgrades", [])
		)
		_check(
			faction_id, "%s_upgrade_%s" % [kind, upgrade_id],
			completed.has(upgrade_id),
			"purchased but not completed within %d ticks" % ticked
		)
		if completed.has(upgrade_id):
			return upgrade_id
		return ""
	return ""


func _short(unit_type: String) -> String:
	var parts := unit_type.split(".")
	return String(parts[parts.size() - 1]) if parts.size() > 0 else unit_type


func _check(faction_id: String, key: String, ok: bool, detail: String = "") -> bool:
	var qualified := "%s_%s" % [faction_id, key]
	if ok:
		passed += 1
		((report["factions"] as Dictionary)[faction_id] as Dictionary)["exercised"] = (
			int(((report["factions"] as Dictionary)[faction_id] as Dictionary).get("exercised", 0)) + 1
		)
		print("DEEP_PRODUCTION PASS %s" % qualified)
	else:
		failed += 1
		((report["factions"] as Dictionary)[faction_id] as Dictionary)["failed"] = (
			int(((report["factions"] as Dictionary)[faction_id] as Dictionary).get("failed", 0)) + 1
		)
		printerr("DEEP_PRODUCTION FAIL %s | %s" % [qualified, detail])
	return ok


func _fail(faction_id: String, key: String, detail: String, faction_report: Dictionary) -> void:
	_check(faction_id, key, false, detail)
	(faction_report["blocked_rows"] as Array).append(
		{"key": key, "reason": "FAILED", "detail": detail}
	)


func _block(
	faction_id: String, key: String, reason: String, detail: String,
	faction_report: Dictionary
) -> void:
	## A NAMED block. Only reasons the table admits as engine truth land here;
	## everything else must go through _fail.
	if not BLOCK_REASONS_ARE_ENGINE_TRUTH.has(reason):
		_fail(faction_id, key, "unrecognised block reason '%s': %s" % [reason, detail], faction_report)
		return
	blocked += 1
	faction_report["blocked"] = int(faction_report.get("blocked", 0)) + 1
	(faction_report["blocked_rows"] as Array).append(
		{"key": key, "reason": reason, "detail": detail}
	)
	print("DEEP_PRODUCTION BLOCKED %s_%s | %s | %s" % [faction_id, key, reason, detail])


func _print_faction_table() -> void:
	print("DEEP_PRODUCTION_TABLE faction exercised blocked failed structures")
	var faction_ids: Array[String] = []
	for faction_value in (report["factions"] as Dictionary).keys():
		faction_ids.append(String(faction_value))
	faction_ids.sort()
	for faction_id in faction_ids:
		var row: Dictionary = (report["factions"] as Dictionary)[faction_id]
		print("DEEP_PRODUCTION_TABLE %s %d %d %d %d" % [
			faction_id,
			int(row.get("exercised", 0)),
			int(row.get("blocked", 0)),
			int(row.get("failed", 0)),
			(row.get("structures", {}) as Dictionary).size(),
		])


func _write_report() -> void:
	var out_path := OS.get_environment("OPENBFME_DEEP_PRODUCTION_OUT").strip_edges()
	if out_path == "":
		return
	var handle := FileAccess.open(out_path, FileAccess.WRITE)
	if handle:
		handle.store_string(JSON.stringify(report, "\t"))
		handle.close()
