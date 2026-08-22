extends SceneTree
## Owner defect 2026-08-22 (lane CASTLE, task A): the v0.2.8 Minas Tirith boot
## wrote 99,466 identical
##   `CASTLE_AI_REJECT ... site_source=generic-navigation-cell:... reason=unsupported-structure`
## lines - one construct dry-run and one print for EVERY navigation cell, every
## tick, for a structure kind the team has no build rule for.
## `unsupported-structure` is decided by the team's build-rule table before
## `_issue_construct_for_team` reads the requested position, so no site the scan
## could pick would change the answer.
##
## Sealed synthetic fixture: no content pack, no retail GLB, no map. The stub
## route provider publishes a 61x61 all-walkable navigation grid, so pre-fix the
## four `_try_castle_ai_construction` calls below cost 4 x 3,721 = 14,884
## dry-runs and 14,884 prints; post-fix they cost 4 dry-runs and 1 print.
##
## Counters are read through Object.get() so this file also RUNS (and fails
## cleanly, rather than aborting mid-script) against a sim that predates them.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 6
const GRID_MAX := 60
const AI_TEAM := 1
## Deliberately absent from every faction's structure_build_rules table, which
## is exactly the state Minas Tirith's teams were in for `farm`.
const UNSUPPORTED_KIND := "castle-ai-site-scan-unsupported-kind"

var passed := 0
var failed := 0


class StubRouteProvider extends RefCounted:
	const STUB_GRID_MAX := 60
	var navigation_grid_min := Vector2i.ZERO
	var navigation_grid_max := Vector2i(STUB_GRID_MAX, STUB_GRID_MAX)
	var walkable_queries := 0

	func local_to_grid_cell(position: Vector2) -> Vector2i:
		return Vector2i(int(round(position.x)), int(round(position.y)))

	func grid_to_local_horizontal(cell: Vector2i) -> Vector2:
		return Vector2(float(cell.x), float(cell.y))

	func is_navigation_walkable(cell: Vector2i) -> bool:
		walkable_queries += 1
		return (
			cell.x >= navigation_grid_min.x and cell.x <= navigation_grid_max.x
			and cell.y >= navigation_grid_min.y and cell.y <= navigation_grid_max.y
		)


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = _fixture_sim()
	var builder_ids: Array[int] = [_fixture_builder(sim)]
	var ai_state: Dictionary = {}
	var cells := (GRID_MAX + 1) * (GRID_MAX + 1)

	var built: bool = sim._try_castle_ai_construction(AI_TEAM, ai_state, builder_ids, UNSUPPORTED_KIND)
	_check("unsupported_kind_is_refused", not built, "built=%s" % str(built))
	_check(
		"kind_refusal_reason_recorded",
		String(ai_state.get("last_site_rejection", "")) == "kind-level:unsupported-structure",
		"last_site_rejection=%s" % String(ai_state.get("last_site_rejection", ""))
	)
	# The whole point: ONE receipt for a kind-level refusal, not one per cell.
	_check(
		"single_dry_run_for_unsupported_kind",
		_counter(sim, "castle_ai_site_dry_runs") == 1,
		"dry_runs=%d cells=%d" % [_counter(sim, "castle_ai_site_dry_runs"), cells]
	)
	_check(
		"one_summary_print_not_one_per_cell",
		_counter(sim, "castle_ai_site_reject_prints") == 1,
		"prints=%d cells=%d" % [_counter(sim, "castle_ai_site_reject_prints"), cells]
	)
	_check(
		"cell_scan_never_started",
		int(sim.route_provider.walkable_queries) == 0,
		"walkable_queries=%d" % int(sim.route_provider.walkable_queries)
	)
	# The AI retries every tick; repeats must not re-print or re-scan either.
	for _repeat in range(3):
		sim._try_castle_ai_construction(AI_TEAM, ai_state, builder_ids, UNSUPPORTED_KIND)
	_check(
		"repeat_ticks_stay_bounded",
		_counter(sim, "castle_ai_site_dry_runs") == 4 and _counter(sim, "castle_ai_site_reject_prints") == 1,
		"dry_runs=%d prints=%d cells_per_tick_pre_fix=%d" % [
			_counter(sim, "castle_ai_site_dry_runs"),
			_counter(sim, "castle_ai_site_reject_prints"),
			cells,
		]
	)
	_finish()


func _counter(sim, property_name: String) -> int:
	var value: Variant = sim.get(property_name)
	if value == null:
		# Pre-fix sims have no such counter. -1 fails every assertion above
		# without aborting the script mid-run.
		return -1
	return int(value)


func _fixture_sim():
	var sim = Sim.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"source_map_transform_scale": 0.1,
	})
	sim.route_provider = StubRouteProvider.new()
	return sim


func _fixture_builder(sim) -> int:
	# The site search reads only the builder's position, so one sealed
	# synthetic porter row is the whole fixture.
	var id := 1
	sim.entities[id] = {
		"id": id,
		"team": AI_TEAM,
		"kind": "unit",
		"name": "SyntheticPorter",
		"is_builder": true,
		"position": Vector2(30.0, 30.0),
		"health": 100,
		"maximum_health": 100,
		"state": "idle",
		"construction_id": 0,
	}
	return id


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CASTLE_AI_SITE_SCAN PASS " + label)
	else:
		failed += 1
		push_error("CASTLE_AI_SITE_SCAN FAIL %s%s" % [label, (" (%s)" % detail) if detail != "" else ""])


func _finish() -> void:
	print("CASTLE_AI_SITE_SCAN_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)
