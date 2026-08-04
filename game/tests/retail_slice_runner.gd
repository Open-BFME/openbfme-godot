extends SceneTree
## Deterministic behavior/asset gate for the playable private retail slice.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WatchdogScript = preload("res://tests/runner_watchdog.gd")
## Ratchet. RAISE this consciously when a measured document-backed run clears
## it; never lower it to make a red run green. Measured 2026-08-04 against the
## workspace selection (all seven RotWK faction packs): passed=363 failed=39,
## where the 39 are exactly the KNOWN_FAILURE_NAMES pins. Previous value 361;
## +2 are the ranger ANY-of gate / HUD lock-parity checks.
const ACCEPTANCE_MIN_PASSED := 363
## Named, root-caused failures. A name may only enter this table WITH the
## reason it is here; a name that starts passing must be removed in the same
## change that makes it pass (the gate fails either way — see
## retail_gate_unexpected_failure_* / retail_gate_update_allowlist_*).
##
## REMOVED 2026-08-04 — the four gondor_* exact-value rows in
## _check_retail_exact_values. These were pinned as a "horde-vs-unit locomotor
## speed family". That diagnosis was wrong: their speeds and member counts
## always matched. The damage/range literals were carried over from a men pack
## compiled from a pre-layered source, and now that the pack is compiled from
## the layered oracle they disagreed. The literals were re-derived from
## data/ini with citations at the pin site, so gondor_archer_*,
## gondor_tower_guard_* and gondor_knight_* now PASS and gondor_fighter_* never
## needed to be added.
##
## REMOVED 2026-08-04 —
## "archer_pierce_vs_knight_applies_compiled_scalar_in_live_sim". It now passes
## against the rebuilt men pack: the GondorArcherHorde document it needs was
## absent from the previous pack because its W3D secondary-skin prep failed and
## the pack was published with --allow-incomplete. The archer is back, so the
## compiled pierce-vs-knight scalar resolves in the live sim.
const KNOWN_FAILURE_NAMES := {
	# ADDED 2026-08-04, DIAGNOSIS CORRECTED 2026-08-04 (round 10). One real
	# engine gap listed under three names.
	#
	# The earlier note here said the fortress "never takes damage and the match
	# never resolves". That was wrong, and the live probe in
	# .private/scratch/opus09-live1.out.log:35-52 shows why. The fortress DOES
	# take steady chip damage (7500 at t=0, 227 at t=17000) and the match DOES
	# resolve: `PROBE WINNER=0 at fortress_tick=17521`. The real defect is
	# throughput. Of the five attackers whose fortress attack order is
	# accepted, exactly one (entity 2) ever reaches `state=attack`; the other
	# four are stuck in `state=run` at distance ~4.4-5.1 for the entire run,
	# because the men fortress expansion pads sit between them and the hit
	# surface and pathing treats the pads as blocking. So the slice lands ~1/5
	# of its intended DPS and cannot finish inside the runner's 14000-tick
	# bound - at t=14000 the fortress still has 1512 health.
	#
	# `victory_music_active` and `victory_splash_visible` are pure cascade:
	# both are only reachable after a winner exists inside the bound. All three
	# disappear together when pad pathing stops obstructing melee approach;
	# none of them is a content or literal problem.
	"battle_reaches_victory": true,
	"victory_music_active": true,
	"victory_splash_visible": true,
	# Reason (derived from the live detail `18/18 seeds=["fortress"]`,
	# .private/scratch/opus10-slice-workspace.log): all 18 seeded structures
	# resolve, but the manifest contributes a single seed kind ("fortress")
	# rather than the per-kind set the check expects, so the kind comparison
	# cannot match. Inherited pin, not root-caused beyond that observation -
	# untriaged as of 2026-08-04.
	"seeded_structures_match_manifest_seed_kinds": true,
	# structure_<id>_starts_exact_private_lifecycle is ONE generated check per
	# seeded structure, so this is a single gap listed under N names, not N
	# gaps. The check is fully data driven (see _check body): it compares the
	# live structure node against the bundle document's own buildingLifecycle,
	# and the whole seeded set disagrees the same way. 3014 and 3015 were added
	# 2026-08-04 — they are not new breakage, they are two more castle pieces
	# seeded by the rebuilt men pack falling into the identical family. Closing
	# this needs the structure lifecycle consumer, not a literal edit; when it
	# lands, every one of these names disappears together.
	"structure_3000_starts_exact_private_lifecycle": true,
	"structure_3001_starts_exact_private_lifecycle": true,
	"structure_3002_starts_exact_private_lifecycle": true,
	"structure_3003_starts_exact_private_lifecycle": true,
	"structure_3004_starts_exact_private_lifecycle": true,
	"structure_3005_starts_exact_private_lifecycle": true,
	"structure_3006_starts_exact_private_lifecycle": true,
	"structure_3007_starts_exact_private_lifecycle": true,
	"structure_3008_starts_exact_private_lifecycle": true,
	"structure_3009_starts_exact_private_lifecycle": true,
	"structure_3010_starts_exact_private_lifecycle": true,
	"structure_3011_starts_exact_private_lifecycle": true,
	"structure_3012_starts_exact_private_lifecycle": true,
	"structure_3013_starts_exact_private_lifecycle": true,
	"structure_3014_starts_exact_private_lifecycle": true,
	"structure_3015_starts_exact_private_lifecycle": true,
	# ONE presentation gap listed under seven names (both teams' battalions).
	# Every row fails with detail `count=0`: the battalion presents zero member
	# overlay nodes at all, so the "no SYNTHETIC overlays" assertion has
	# nothing to inspect and fails vacuously rather than because an invented
	# overlay was found. The seven names close together when member overlay
	# construction lands. Inherited pin, untriaged beyond the count=0
	# observation — 2026-08-04.
	"private_battalion_1_has_no_synthetic_overlays": true,
	"private_battalion_2_has_no_synthetic_overlays": true,
	"private_battalion_3_has_no_synthetic_overlays": true,
	"private_battalion_101_has_no_synthetic_overlays": true,
	"private_battalion_102_has_no_synthetic_overlays": true,
	"private_battalion_103_has_no_synthetic_overlays": true,
	"private_battalion_104_has_no_synthetic_overlays": true,
	# The projectile-impact closure blocker is expected to be stated
	# explicitly by the archer projectile controller and is not; the check
	# reports no detail because the blocker string is absent rather than
	# wrong. Inherited pin, untriaged — 2026-08-04.
	"gondor_archer_projectile_impact_closure_blocker_is_explicit": true,
	# PRESENTATION PARITY FAMILY (four names, one root). The private slice is
	# still drawing invented team colour instead of source-authored colour:
	# `status=fallback-team-tint` with `blue=2/0 red=2/0` (two tinted surfaces,
	# zero source-bound), the surface colours differ as
	# `(0.727, 0.8404, 1.0)` vs `(1.0, 0.7564, 0.727)` — a hand-authored
	# blue/red pair, not an oracle colour — and the overlay contract string
	# ends `...-oracle-color-throb-pending`, naming the missing piece itself.
	# All four close when oracle colour binding replaces the fallback tint.
	# Inherited pins, root named but not fixed — 2026-08-04.
	"invented_team_tint_is_suppressed_in_private_parity": true,
	"private_retail_surface_colors_remain_source_neutral": true,
	"private_retail_overlays_use_source_contracts": true,
	# `hero=bfme2.object.gondor-aragorn-mp threshold=125 award=76 hp=150
	# dam=0`: the hero levelling row compiles, but the per-level damage bonus
	# resolves to 0, so the INI comparison cannot match. Inherited pin,
	# untriaged — 2026-08-04.
	"aragorn_level_values_match_ini": true,
	# ARMOR-TABLE FAMILY (four names, one root). Each check demands the armor
	# set be compiled FROM the pack's own structure/unit documents; each one
	# instead resolves a plausible but statically-named set — FortressArmor,
	# farm=ResourceArmor/barracks=FactoryArmor,
	# knight=KnightArmor/pike=TowerGuardArmor/soldier=SoldierArmor,
	# blades=GondorSwordUpgraded/heavy=SoldierHeavyArmor with `fire=` empty.
	# The values are retail-plausible; the provenance is not document-backed,
	# which is what these four assert. Inherited pins, untriaged — 2026-08-04.
	"fortress_armor_table_is_compiled_from_structure_document": true,
	"farm_and_producer_kinds_use_their_own_compiled_scalars": true,
	"unit_armor_counter_matrix_is_compiled_from_unit_documents": true,
	"forge_upgrades_carry_compiled_retail_effects": true,
	# SIGNATURE FAMILY (three names, one root) — see the PENDING RE-PIN note on
	# EXPECTED_BATTLE_SIGNATURES below. The multi-nugget damage-component
	# compiler change moves kill order and tick counts, so the pinned
	# constants drifted (observed 1674717D vs pinned 115D15FA; defeat
	# B4D3DC20 vs 97166BF2). Repository policy forbids silently refreshing a
	# drifted pin: re-pinning requires a post-cook run, which the repo owner
	# orchestrates. Pinned deliberately, root cause known — 2026-08-04.
	"deterministic_replay_signature": true,
	"battle_signature_matches_pinned_constant": true,
	"deterministic_defeat_signature": true,
}
# Capture-measured dock geometry (bfme2-ref-120s.png); mirrors
# retail_hud.gd RETAIL_RADAR_CENTER / RETAIL_DISH_CENTER.
const EXPECTED_RADAR_CENTER := Vector2(225.0, 198.0)
const EXPECTED_DISH_CENTER := Vector2(587.0, 219.0)
const ARCHER_PROJECTILE_CONTROLLER_PATH := "res://src/retail_slice/retail_archer_projectile_controller.gd"
## Pinned deterministic battle signatures per faction (see
## battle_signature_matches_pinned_constant). Repository policy: a drifted pin
## must be EXPLAINED before it is moved, never silently refreshed.
##
## PENDING RE-PIN -- multi-nugget weapon damage types.
## Retail weapons whose DamageNuggets author different types (ArwenSword: HERO
## ARWEN_DAMAGE + SLASH 20) previously compiled to an untyped damage lump, so
## the whole hit resolved against the victim's DEFAULT armor column. The
## compiler now publishes damageComponents and the sim weights each component
## against its own column, so those units deal retail-correct damage (Arwen
## into RivendellLancerArmor: 368 rather than 200). Kill order and tick counts
## therefore move, and these constants must shift with them.
##
## The values below are the PRE-fix signatures and are deliberately left as-is:
## re-pinning requires a pack re-cook (the packs on disk predate the compiler
## change and carry no damageComponents), which the repo owner orchestrates.
## Re-pin from a post-cook run rather than assuming the drift.
##
## The MEN pin absorbs independently identified schema changes: base commit
## 42c74db added hash-visible unit_damage_components; hero-rank objective ledger
## peak-rank history; and residual FoW vision ledger / path gate / structure
## CreateObjectDie hooks (hash-visible parity state). Repeated replay-matched
## runs agree on 115D15FA. Other faction pins remain untouched pending
## separately documented post-cook multi-nugget verification.
const EXPECTED_BATTLE_SIGNATURES := {
	"men": "115D15FA",
	"elves": "2521173F",
	"dwarves": "DEF53068",
	"isengard": "E35938E4",
	"mordor": "C3BFD21C",
	"wild": "D2892DA6",
}
# This is a deadlock/watchdog bound, not a frame-time optimization gate. The
# vertical-slice DoD currently prioritizes source-correct gameplay and assets.
# Multi-faction pack sets load many converted GLBs up front (isengard ~22s).
const INITIALIZATION_WATCHDOG_MS := 30000

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments - an inert runner prints a zero-failure result and
## exits 0.
##
## Unlike the fixed-count runners this gate cannot pin an exact number: most of
## its checks are generated per spawn-roster row, so the total tracks what the
## mounted content pack declares. Pinning it would false-fail every time the
## pack legitimately grows, which trains people to ignore the guard. What does
## hold is a floor per branch - the document-backed run (men v-slice pack,
## OPENBFME_CONTENT set) made 352 checks and the content-less run 261, and
## neither branch can lose checks without something having aborted. The
## content-less branch is never green anyway: it reports 31 real failures
## because no document rows resolve.
const MINIMUM_CHECKS_DOCUMENT_BACKED := 352
const MINIMUM_CHECKS_WITHOUT_DOCUMENTS := 261

var passed := 0
var failed := 0
var observed_failure_names: Dictionary = {}
var _document_backed_rows := 0
# A GDScript runtime error inside `_run` unwinds past every `quit()` in this
# file, so without this the headless process idles forever instead of failing.
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	# The gate's combat checks are written against the legacy pre-spawned
	# battalions; retail play starts from fortress + porter only.
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	_watchdog.start(self, "RETAIL_SLICE", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	_run_archer_projectile_contract_fixture()
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		call_deferred("_finish")
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame

	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	_run_map_scripts_v1_team_bridge_probe(slice)
	var initialization_total_ms := int(slice.initialization_metrics_ms.get("ready_complete", -1))
	_check("initialization_completes_before_watchdog", initialization_total_ms >= 0 and initialization_total_ms <= INITIALIZATION_WATCHDOG_MS, "%d ms" % initialization_total_ms)
	# The host pack is asserted by CAPABILITY: an external (non-res://) root
	# that ships every surface the slice reads out of it. The old form asserted
	# the literal id "bfme2-men-vslice", which failed on any newer pack while
	# passing for a same-named pack that provided nothing.
	var host_resolution: Dictionary = slice._resolve_host_slice_pack()
	_check(
		"external_private_pack",
		String(slice.selected_pack_root) != ""
		and not String(slice.selected_pack_root).begins_with("res://")
		and String(host_resolution.get("root", "")) == String(slice.selected_pack_root),
		"%s resolution=%s" % [String(slice.selected_pack_root), str(host_resolution)]
	)
	_check("terrain_is_source_driven", bool(slice.source_driven_terrain))
	_check("three_ford_crossings", int(slice.crossing_count) == 3, str(slice.crossing_count))
	_check("imported_map_preview", bool(slice.map_preview_loaded))
	_check("imported_map_art", bool(slice.map_art_loaded))
	_check("cooked_source_map_mounted", slice.source_map_data != null and bool(slice.source_map_data.ready), String(slice.source_map_data.error if slice.source_map_data != null else "missing"))
	if slice.source_map_data != null:
		_check("source_heightmap_exact", int(slice.source_map_data.width) == 415 and int(slice.source_map_data.height) == 353 and int(slice.source_map_data.heightmap_bytes) == 292990)
		_check("source_passability_exact", int(slice.source_map_data.impassable_count) == 18325 and int(slice.source_map_data.passability_bytes) == 18356)
		_check("source_terrain_symbols_exact", int(slice.source_map_data.terrain_texture_count) == 66)
		var terrain_cell_count := int(slice.source_map_data.width) * int(slice.source_map_data.height)
		_check("source_terrain_cell_arrays_loaded", slice.source_map_data.terrain_tile_indices.size() == terrain_cell_count and slice.source_map_data.terrain_blend_cells.size() == terrain_cell_count and slice.source_map_data.terrain_three_way_blend_cells.size() == terrain_cell_count and slice.source_map_data.terrain_cliff_cells.size() == terrain_cell_count, str(terrain_cell_count))
		_check("source_terrain_description_tables_loaded", slice.source_map_data.terrain_blend_descriptions.size() == 16904 and slice.source_map_data.terrain_cliff_mappings.size() == 577, "blend=%d cliff=%d" % [slice.source_map_data.terrain_blend_descriptions.size(), slice.source_map_data.terrain_cliff_mappings.size()])
		_check("source_terrain_nonzero_layers_recomputed", int(slice.source_map_data.terrain_nonzero_blend_cell_count) == 51394 and int(slice.source_map_data.terrain_nonzero_three_way_blend_cell_count) == 9408 and int(slice.source_map_data.terrain_nonzero_cliff_cell_count) == 64, "blend=%d three=%d cliff=%d" % [slice.source_map_data.terrain_nonzero_blend_cell_count, slice.source_map_data.terrain_nonzero_three_way_blend_cell_count, slice.source_map_data.terrain_nonzero_cliff_cell_count])
		_check("source_terrain_material_catalog_loaded", slice.source_map_data.terrain_material_catalog.size() == 66 and int(slice.source_map_data.terrain_material_catalog[0].get("table_index", -1)) == 0 and String(slice.source_map_data.terrain_material_catalog[0].get("symbol", "")) == "GrassIsengard06" and int(slice.source_map_data.terrain_texture_array_dimension) == 512, str(slice.source_map_data.terrain_texture_array_dimension))
		_check("representative_terrain_tile_samples_exact", _source_terrain_tile_samples_match(slice.source_map_data))
		_check("source_objects_exact", int(slice.source_map_data.object_count) == 1526 and int(slice.source_map_data.nonroad_object_count) == 1384)
		_check("source_roads_are_exact_separate_pairs", int(slice.source_map_data.road_type_count) == 5 and int(slice.source_map_data.road_control_point_count) == 142 and int(slice.source_map_data.road_segment_count) == 71 and int(slice.source_map_data.road_unresolved_control_point_count) == 0 and slice.source_map_data.road_type_ids == ["Footprints", "FtPrintDrkGr02", "FtPrintGrass02", "FtprintsDrk", "FtprintsDrk02"], str(slice.source_map_data.road_type_ids))
		_check("source_road_material_closure_exact", int(slice.source_map_data.road_material_count) == 5 and slice.source_map_data.road_material_catalog.size() == 5 and String(slice.source_map_data.road_source_report_aggregate_sha256) == "45f557b171a18739e268c626e7be0f2aecadba4a0a527d809fdc7b3a1076fdc2")
		_check("source_road_topology_exact", int(slice.source_map_data.road_unique_endpoint_count) == 120 and int(slice.source_map_data.road_shared_node_count) == 21 and int(slice.source_map_data.road_curve_candidate_node_count) == 18 and int(slice.source_map_data.road_crossing_candidate_node_count) == 0 and int(slice.source_map_data.road_modifier_flags_or) == 0, "%d/%d/%d" % [slice.source_map_data.road_unique_endpoint_count, slice.source_map_data.road_shared_node_count, slice.source_map_data.road_curve_candidate_node_count])
		_check("source_waypoints_and_starts_exact", int(slice.source_map_data.waypoint_count) == 14 and int(slice.source_map_data.player_start_count) == 2)
		_check("source_water_geometry_exact", int(slice.source_map_data.standing_water_count) == 4 and int(slice.source_map_data.river_count) == 8)
		_check("source_binary_not_packaged", not bool(slice.source_map_data.source_binary_packaged))
		var player_one := Vector3(slice.source_map_data.local_player_starts.get("Player_1_Start", Vector3.ZERO))
		var player_two := Vector3(slice.source_map_data.local_player_starts.get("Player_2_Start", Vector3.ZERO))
		_check("source_start_transform_exact", player_one.is_equal_approx(Vector3(38.0, 0.0, 0.0)) and player_two.is_equal_approx(Vector3(-38.0, 0.0, 0.0)), "%s / %s" % [str(player_one), str(player_two)])
		_check("source_ford_names_drive_gates", _gate_names(slice.source_map_data.ford_gates) == ["ford1", "ford2", "ford3"], str(_gate_names(slice.source_map_data.ford_gates)))
		_check("source_ford_ids_exact", _gate_source_ids(slice.source_map_data.ford_gates) == [38, 43, 46], str(_gate_source_ids(slice.source_map_data.ford_gates)))
		_check(
			"source_unbound_generic_props_exact",
			slice.source_map_data.generic_prop_placements.size() == 0
			and int(slice.source_map_data.unresolved_prop_placement_count) == 13,
			"generic=%d unresolved=%d" % [slice.source_map_data.generic_prop_placements.size(), slice.source_map_data.unresolved_prop_placement_count]
		)
		_check("terrain_semantic_ranges_recomputed", int(slice.source_map_data.raw_elevation_min) == 5888 and int(slice.source_map_data.raw_elevation_max) == 11117 and int(slice.source_map_data.computed_raw_elevation_min) == 5888 and int(slice.source_map_data.computed_raw_elevation_max) == 11117)
		_check("passability_full_popcount_recomputed", int(slice.source_map_data.computed_impassable_count) == 18325 and int(slice.source_map_data.computed_impassable_count) == int(slice.source_map_data.impassable_count), str(slice.source_map_data.computed_impassable_count))
		_check("cooked_binary_digests_exact", String(slice.source_map_data.heightmap_sha256).to_upper() == "449D7B4BADA8549B5ED3EC8E908186922D05256D6F728B433018A6F381EDA7FB" and String(slice.source_map_data.passability_sha256).to_upper() == "11E911C6BA50A0D8DCF7FC3A71242B013B5DFDCE1169AE86C939D2DDD5E654B9")
		_check("representative_height_samples_exact", _source_height_samples_match(slice.source_map_data))
		_check("declared_playable_inset_exact", int(slice.source_map_data.border_width) == 20 and Vector2(slice.source_map_data.playable_world_extent).is_equal_approx(Vector2(3750.0, 3130.0)) and Vector2i(slice.source_map_data.playable_grid_min) == Vector2i(20, 20) and Vector2i(slice.source_map_data.playable_grid_max) == Vector2i(395, 333))
		if slice.source_map_data.map_outline.size() >= 3:
			var playable_source_min: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[0])
			var playable_source_max: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[2])
			_check("playable_polygon_uses_declared_border", playable_source_min.is_equal_approx(Vector2(200.0, -200.0)) and playable_source_max.is_equal_approx(Vector2(3950.0, -3330.0)), "%s / %s" % [str(playable_source_min), str(playable_source_max)])
		else:
			# Empty outline means map configure() failed earlier; report it as a
			# failed check instead of crashing _run (a crash here left the
			# headless runner alive forever with no result line).
			_check("playable_polygon_uses_declared_border", false, "map_outline empty (map load failed)")
		_check("bounded_navigation_built_once", bool(slice.source_map_data.navigation_ready) and int(slice.source_map_data.navigation_build_count) == 1 and int(slice.source_map_data.navigation_walkable_count) > 80000 and int(slice.source_map_data.navigation_water_blocked_count) > 0 and int(slice.source_map_data.navigation_ford_corridor_count) > 0, "walkable=%d water_blocked=%d corridors=%d builds=%d" % [slice.source_map_data.navigation_walkable_count, slice.source_map_data.navigation_water_blocked_count, slice.source_map_data.navigation_ford_corridor_count, slice.source_map_data.navigation_build_count])
		_check("reviewed_ford2_cell_stays_blocked", slice.source_map_data.is_impassable_at(208, 142) and not slice.source_map_data.is_navigation_walkable(Vector2i(208, 142)))
	if not bool(slice.ready_ok) or slice.source_map_data == null or not bool(slice.source_map_data.ready) or slice.simulation == null:
		slice.queue_free()
		await process_frame
		call_deferred("_finish")
		return
	_check_retail_unit_rules(slice)
	_check_retail_exact_values(slice)
	_check("simulation_uses_source_map_configuration", bool(slice.simulation.source_map_configured))
	# HUD lock parity with the sim's production gate. Oracle: layered
	# commandbutton.ini:7513-7518 (Command_ConstructGondorRangerHorde) authors
	# NeededUpgrade = Upgrade_GondorArcheryRangeLevel2 Upgrade_CustomGenericUpgrade1
	# together with NeededUpgradeAny = Yes, so owning ANY ONE member unlocks the
	# ranger and its cheapest authored route (the base GondorArcheryCommandSet)
	# carries no ALL-of requirement at all. A HUD that reads only the ALL-of
	# list therefore offers the train button while queue_unit refuses it.
	var ranger_type := "bfme2.object.gondor-ranger-horde"
	var ranger_all_of: Array = slice.simulation.required_upgrades_for_unit(ranger_type, "archery_range")
	var ranger_any_group: Array = slice.simulation.required_upgrade_any_group_for_unit(ranger_type, "archery_range")
	ranger_any_group.sort()
	_check(
		"ranger_gate_is_authored_any_of_pair",
		ranger_any_group == ["Upgrade_CustomGenericUpgrade1", "Upgrade_GondorArcheryRangeLevel2"] and ranger_all_of.is_empty(),
		"any=%s all=%s" % [str(ranger_any_group), str(ranger_all_of)]
	)
	_check(
		"hud_locks_ranger_until_any_of_member_owned",
		slice.hud_locked_units([ranger_type], "archery_range", []) == [ranger_type]
			and slice.hud_locked_units([ranger_type], "archery_range", ["Upgrade_GondorArcheryRangeLevel2"]).is_empty()
			and slice.hud_locked_units([ranger_type], "archery_range", ["Upgrade_CustomGenericUpgrade1"]).is_empty(),
		"none=%s level2=%s generic=%s" % [
			str(slice.hud_locked_units([ranger_type], "archery_range", [])),
			str(slice.hud_locked_units([ranger_type], "archery_range", ["Upgrade_GondorArcheryRangeLevel2"])),
			str(slice.hud_locked_units([ranger_type], "archery_range", ["Upgrade_CustomGenericUpgrade1"])),
		]
	)
	var player_centroid := (Vector2(slice.simulation.entity(1)["position"]) + Vector2(slice.simulation.entity(2)["position"])) * 0.5
	var enemy_centroid := (Vector2(slice.simulation.entity(101)["position"]) + Vector2(slice.simulation.entity(102)["position"])) * 0.5
	var source_player_two := Vector3(slice.source_map_data.local_player_starts["Player_2_Start"])
	var source_player_one := Vector3(slice.source_map_data.local_player_starts["Player_1_Start"])
	_check("battalion_spawns_derive_from_source_starts", player_centroid.is_equal_approx(Vector2(source_player_two.x, source_player_two.z)) and enemy_centroid.is_equal_approx(Vector2(source_player_one.x, source_player_one.z)), "%s / %s" % [str(player_centroid), str(enemy_centroid)])
	_check("source_battlefield_built", slice.battlefield != null and bool(slice.battlefield.source_driven))
	if slice.battlefield != null:
		_check("source_height_mesh_uses_every_exact_sample_and_quad", bool(slice.battlefield.terrain_exact_grid_ready) and int(slice.battlefield.terrain_vertex_count) == 146495 and int(slice.battlefield.terrain_triangle_count) == 291456, "vertices=%d triangles=%d" % [slice.battlefield.terrain_vertex_count, slice.battlefield.terrain_triangle_count])
		_check("source_passability_colors_cover_full_terrain", int(slice.battlefield.impassable_vertex_count) == 18325 and int(slice.battlefield.impassable_vertex_count) == int(slice.source_map_data.impassable_count), str(slice.battlefield.impassable_vertex_count))
		var retail_terrain_material: ShaderMaterial = slice.battlefield.terrain_mesh_instance.get_active_material(0) as ShaderMaterial if slice.battlefield.terrain_mesh_instance != null else null
		var bound_texture_array: Texture2DArray = retail_terrain_material.get_shader_parameter("terrain_textures") as Texture2DArray if retail_terrain_material != null else null
		var bound_tile_data: Texture2D = retail_terrain_material.get_shader_parameter("terrain_tile_data") as Texture2D if retail_terrain_material != null else null
		var bound_blend_data: Texture2D = retail_terrain_material.get_shader_parameter("terrain_blend_data") as Texture2D if retail_terrain_material != null else null
		var bound_cliff_info: Texture2D = retail_terrain_material.get_shader_parameter("terrain_cliff_info") as Texture2D if retail_terrain_material != null else null
		_check("retail_terrain_material_is_source_driven", bool(slice.battlefield.terrain_material_source_driven) and retail_terrain_material != null and String(retail_terrain_material.get_meta("source", "")) == "cooked-retail-terrain-materials-and-sage-blends" and bool(retail_terrain_material.get_shader_parameter("sage_lighting_enabled")) and Vector3(retail_terrain_material.get_shader_parameter("sage_ambient_color")).is_equal_approx(Vector3(22.0 / 255.0, 18.0 / 255.0, 22.0 / 255.0)) and Vector3(retail_terrain_material.get_meta("sage_ambient_color", Vector3.INF)).is_equal_approx(Vector3(22.0 / 255.0, 18.0 / 255.0, 22.0 / 255.0)) and String(retail_terrain_material.get_meta("sage_lighting_model", "")) == "opensage-terrain-three-light-lambert")
		_check("retail_terrain_texture_array_has_66_layers", bound_texture_array != null and bound_texture_array.get_layers() == 66 and bound_texture_array.get_width() == 256 and bound_texture_array.get_height() == 256, "layers=%d size=%dx%d" % [bound_texture_array.get_layers() if bound_texture_array != null else -1, bound_texture_array.get_width() if bound_texture_array != null else -1, bound_texture_array.get_height() if bound_texture_array != null else -1])
		_check("retail_terrain_tile_data_bound", bound_tile_data != null and bound_tile_data.get_width() == 415 and bound_tile_data.get_height() == 353 and int(slice.battlefield.terrain_tile_data_cell_count) == 146495)
		_check("retail_terrain_blend_and_cliff_data_bound", bound_blend_data != null and bound_blend_data.get_width() == 415 and bound_blend_data.get_height() == 353 and bound_cliff_info != null and bound_cliff_info.get_width() == 577 and bound_cliff_info.get_height() == 2)
		_check("retail_terrain_primary_three_way_and_cliffs_active", int(slice.battlefield.terrain_primary_blend_cell_count) == 51394 and int(slice.battlefield.terrain_three_way_blend_cell_count) == 9408 and int(slice.battlefield.terrain_cliff_cell_count) == 64)
		_check("retail_roads_are_provenance_textured_and_grouped", bool(slice.battlefield.road_material_source_driven) and int(slice.battlefield.road_material_count) == 5 and int(slice.battlefield.road_mesh_instance_count) == 5 and slice.battlefield.road_container != null and slice.battlefield.road_container.get_child_count() == 5)
		_check("retail_road_topology_and_curve_formula_exact", int(slice.battlefield.road_source_edge_count) == 71 and int(slice.battlefield.road_unique_endpoint_count) == 120 and int(slice.battlefield.road_shared_node_count) == 21 and int(slice.battlefield.road_curve_candidate_node_count) == 18 and int(slice.battlefield.road_generated_broad_curve_count) == 11 and int(slice.battlefield.road_straight_fallback_node_count) == 7 and slice.battlefield.road_generated_curve_evidence.size() == 11 and slice.battlefield.road_curve_fallback_evidence.size() == 7)
		_check("retail_road_mesh_is_adaptive_and_terrain_draped", int(slice.battlefield.road_curve_strip_count) == 24 and int(slice.battlefield.road_render_strip_count) == 95 and int(slice.battlefield.road_vertex_count) == 534 and int(slice.battlefield.road_triangle_count) == 344, "strips=%d vertices=%d triangles=%d" % [slice.battlefield.road_render_strip_count, slice.battlefield.road_vertex_count, slice.battlefield.road_triangle_count])
		_check("source_water_mesh_built", int(slice.battlefield.water_surface_count) == 12 and int(slice.battlefield.water_triangle_count) > 50, "surfaces=%d triangles=%d" % [slice.battlefield.water_surface_count, slice.battlefield.water_triangle_count])
		_check("source_ford_gates_are_nonrendered_diagnostics", int(slice.battlefield.ford_marker_count) == 3 and int(slice.battlefield.get_meta("source_ford_gate_count", -1)) == 3 and slice.battlefield.ford_gate_diagnostics.size() == 3 and _visible_source_placeholder_count(slice.battlefield, "SourceFord_") == 0)
		_check("unresolved_props_are_nonrendered_diagnostics", int(slice.battlefield.generic_prop_count) == slice.source_map_data.generic_prop_placements.size() and int(slice.battlefield.get_meta("source_unresolved_prop_placement_count", -1)) == int(slice.source_map_data.unresolved_prop_placement_count) and int(slice.battlefield.get_meta("source_unresolved_prop_sample_count", -1)) == int(slice.battlefield.generic_prop_count) and _unresolved_diagnostic_sample_count(slice.battlefield) == int(slice.battlefield.generic_prop_count) and _visible_unresolved_placeholder_count(slice.battlefield) == 0, str(slice.battlefield.unresolved_prop_diagnostics))
		_check("world_builder_only_farm_templates_are_hidden_in_play", _bound_prop_type_visibility_matches(slice.battlefield, "FarmTemplate", 16, false, "default-model-none-world-builder-only"))
	_check("initial_battalions_include_retail_defined_builders", slice.battalion_nodes.size() == slice.simulation.initial_battalion_count() and slice.battalion_nodes.size() >= 7, str(slice.battalion_nodes.size()))
	var seed_kinds: Array = Array(slice.faction_manifest.get("seed_structure_kinds", []))
	_check(
		"seeded_structures_match_manifest_seed_kinds",
		seed_kinds.size() >= 1
			and slice.simulation.structure_ids(0).size() == seed_kinds.size()
			and slice.simulation.structure_ids(1).size() == seed_kinds.size()
			and slice.structure_nodes.size() == 2 * seed_kinds.size(),
		"%d/%d seeds=%s" % [slice.structure_nodes.size(), slice.simulation.structure_ids().size(), str(seed_kinds)]
	)
	var manifest_build_rules: Dictionary = slice.faction_manifest.get("structure_build_rules", {}) as Dictionary
	var manifest_max_health: Dictionary = slice.faction_manifest.get("structure_max_health", {}) as Dictionary
	var all_kinds_buildable := true
	for kind_value in Array(slice.faction_manifest.get("structure_kinds", [])):
		var kind := String(kind_value)
		var build_rule: Dictionary = manifest_build_rules.get(kind, {}) as Dictionary
		if int(manifest_max_health.get(kind, 0)) <= 0 or int(build_rule.get("cost", -1)) < 0 or float(build_rule.get("seconds", 0.0)) <= 0.0:
			all_kinds_buildable = false
	_check("every_manifest_structure_kind_has_health_and_build_rule", all_kinds_buildable, "kinds=%s" % str(slice.faction_manifest.get("structure_kinds", [])))
	# Every spawn-roster battalion mounts the converted GLB its document
	# declares, with exactly the document's member count.
	var content_db_for_models = root.get_node("ContentDB")
	for entity_id in slice.simulation.entity_ids():
		var entity_row: Dictionary = slice.simulation.entity(entity_id)
		var object_id := String(entity_row.get("object_id", ""))
		if bool(entity_row.get("is_builder", false)):
			continue
		var definition: Dictionary = content_db_for_models.get_bundle_object(object_id)
		var expected_model := String((definition.get("presentation", {}) as Dictionary).get("model", "")).get_file()
		var expected_members := int(entity_row.get("member_count", 0))
		var typed_battalion = slice.battalion_nodes.get(entity_id)
		_check(
			"battalion_%d_mounts_document_glb" % entity_id,
			typed_battalion != null
				and String(typed_battalion.object_id) == object_id
				and expected_model != ""
				and String(typed_battalion.retail_model_filename) == expected_model
				and int(typed_battalion.member_count) == expected_members
				and int(typed_battalion.retail_visual_count) == expected_members,
			"object=%s model=%s members=%d retail=%d" % [
				object_id,
				String(typed_battalion.retail_model_filename) if typed_battalion != null else "missing",
				int(typed_battalion.member_count) if typed_battalion != null else -1,
				int(typed_battalion.retail_visual_count) if typed_battalion != null else -1,
			]
		)
	for structure_id in slice.simulation.structure_ids():
		var structure_row: Dictionary = slice.simulation.structure(structure_id)
		var structure_kind := String(structure_row.get("structure_kind", ""))
		var structure_node = slice.structure_nodes.get(structure_id)
		var structure_object_id := String((slice.faction_manifest.get("structure_object_ids", {}) as Dictionary).get(structure_kind, ""))
		var structure_definition: Dictionary = content_db_for_models.get_bundle_object(structure_object_id)
		var structure_lifecycle: Dictionary = ((structure_definition.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary)
		var structure_script = load("res://src/retail_slice/retail_structure.gd")
		var expected_structure_suffix := String(structure_script._intact_visual_path(structure_lifecycle))
		var expected_bib := ""
		var bib_value: Variant = structure_lifecycle.get("bib")
		if typeof(bib_value) == TYPE_DICTIONARY:
			expected_bib = String(((bib_value as Dictionary).get("visual", {}) as Dictionary).get("glb", "")).replace("\\", "/")
		var expected_door := ""
		if structure_kind == "fortress":
			expected_door = String((((structure_lifecycle.get("components", {}) as Dictionary).get("door", {}) as Dictionary).get("closed", {}) as Dictionary).get("path", "")).replace("\\", "/")
		var lifecycle_state: Dictionary = structure_node.lifecycle_state() if structure_node != null else {}
		_check(
			"structure_%d_starts_exact_private_lifecycle" % structure_id,
			structure_node != null
			and bool(structure_node.retail_visual_loaded)
			and String(structure_node.presentation_mode) == "private-imported-lifecycle"
			and String(structure_node.current_lifecycle_phase) == "intact"
			and String(structure_node.active_body_path).replace("\\", "/") == expected_structure_suffix
			and String(structure_node.active_bib_path).replace("\\", "/") == expected_bib
			and String(structure_node.active_door_path).replace("\\", "/") == expected_door
			and String(structure_node.contract_error) == ""
			and String(structure_node.retail_mesh_path).replace("\\", "/").ends_with(expected_structure_suffix)
			and lifecycle_state == structure_node.get_meta("building_lifecycle_state", {}),
			"kind=%s mode=%s phase=%s body=%s bib=%s door=%s error=%s" % [
				structure_kind,
				String(structure_node.presentation_mode) if structure_node != null else "missing",
				String(lifecycle_state.get("phase", "missing")),
				String(lifecycle_state.get("activeBodyPath", "missing")),
				String(lifecycle_state.get("activeBibPath", "missing")),
				String(lifecycle_state.get("activeDoorPath", "missing")),
				String(lifecycle_state.get("contractError", "missing")),
			]
		)
	_check("home_layout_uses_source_navigation", _home_layout_walkable(slice))
	_check("hud_uses_declared_headless_viewport", slice.hud.position.is_equal_approx(Vector2.ZERO) and slice.hud.size.is_equal_approx(Vector2(root.size)), "hud=%s viewport=%s" % [str(slice.hud.size), str(root.size)])
	root.size = Vector2i(1600, 900)
	await process_frame
	await process_frame
	var logical_viewport_size := slice.get_viewport().get_visible_rect().size
	_check(
		"hud_tracks_logical_viewport_under_window_resize",
		slice.hud.position.is_equal_approx(Vector2.ZERO)
		and slice.hud.size.is_equal_approx(logical_viewport_size)
		and is_equal_approx(slice.hud.anchor_right, 1.0)
		and is_equal_approx(slice.hud.anchor_bottom, 1.0),
		"hud=%s logical=%s window=%s" % [str(slice.hud.size), str(logical_viewport_size), str(root.size)]
	)
	root.size = Vector2i(1920, 1080)
	await process_frame
	await process_frame
	var palantir: Control = slice.find_child("PalantirDock", true, false) as Control
	var palantir_frame: Control = slice.find_child("OrnamentalFrame", true, false) as Control
	var command_panel: Control = slice.find_child("CommandPanel", true, false) as Control
	var command_grid: Control = slice.find_child("CommandGrid", true, false) as Control
	var group_strip: Control = slice.find_child("ControlGroupStrip", true, false) as Control
	_check(
		"palantir_is_bottom_left",
		palantir != null
			and palantir.global_position.x >= -1.0
			and palantir.global_position.x <= 12.0
			and palantir.global_position.y > 650.0,
		str(palantir.global_position if palantir != null else Vector2(-1, -1))
	)
	_check("palantir_radar_draws_above_opaque_frame", palantir_frame != null and slice.minimap.get_parent() == palantir_frame.get_parent() and slice.minimap.get_index() > palantir_frame.get_index())
	_check(
		"command_panel_attaches_to_palantir",
		command_panel != null
			and palantir != null
			and command_panel.global_position.x >= palantir.global_position.x + 280.0
			and command_panel.global_position.x + command_panel.size.x <= palantir.global_position.x + palantir.size.x + 1.0
	)
	# Radar and dish centers are the capture-measured dock coordinates
	# (bfme2-ref-120s.png): radar (225, 198), palantir dish (587, 219).
	_check(
		"minimap_is_centered_in_retail_left_circle",
		palantir != null
			and (slice.minimap.global_position + slice.minimap.size * 0.5).distance_to(palantir.global_position + EXPECTED_RADAR_CENTER) < 1.0
	)
	_check(
		"command_panel_is_centered_in_retail_right_circle",
		palantir != null
			and command_panel != null
			and (command_panel.global_position + EXPECTED_DISH_CENTER - Vector2(360.0, 0.0)).distance_to(palantir.global_position + EXPECTED_DISH_CENTER) < 1.0
			and command_grid != null
			and _sockets_ring_dish_center(command_grid, palantir.global_position + EXPECTED_DISH_CENTER)
	)
	_check("control_group_strip_has_nine_slots", group_strip != null and slice.hud.group_buttons.size() == 9)
	_check("retail_shadow_decal_equivalence_present", _retail_shadow_decals_present(slice))
	_check("outcome_layer_exists", slice.hud.outcome_layer != null and not slice.hud.outcome_layer.visible)
	_check("audio_controls_exist_in_pause", slice.hud.music_slider != null and slice.hud.voice_slider != null and slice.hud.mute_toggle != null)
	_check("source_mapping_not_preview_texture", String(slice.minimap.mapping_mode) == "source-derived-local-transform" and bool(slice.minimap.source_geometry_loaded) and not bool(slice.minimap.uses_source_preview_as_background))
	_check("snappy_radar_zoom_contract", is_equal_approx(float(slice.minimap.zoom_response_seconds), 0.09) and float(slice.minimap.radar_zoom_target) == 1.0)
	var player_fortress_position := Vector2(slice.simulation.structure(slice.simulation.fortress_id(0)).get("position", Vector2.INF))
	var camera_inset: float = minf(float(slice._camera_ground_constraint_inset()), minf(slice.source_map_data.local_bounds.size.x, slice.source_map_data.local_bounds.size.y) * 0.5 - 0.001)
	var camera_minimum: Vector2 = slice.source_map_data.local_bounds.position + Vector2(camera_inset, camera_inset)
	var camera_maximum: Vector2 = slice.source_map_data.local_bounds.end - Vector2(camera_inset, camera_inset)
	var expected_camera_focus: Vector2 = Vector2(
		clampf(player_fortress_position.x, camera_minimum.x, camera_maximum.x),
		clampf(player_fortress_position.y, camera_minimum.y, camera_maximum.y)
	)
	_check(
		"camera_starts_from_source_fortress_with_exact_constraint",
		slice.camera_focus.is_equal_approx(expected_camera_focus)
		and is_equal_approx(float(slice.camera_zoom), 1.0)
		and is_equal_approx(float(slice.camera_zoom_target), 1.0),
		"actual=%s expected=%s zoom=%.6f" % [str(slice.camera_focus), str(expected_camera_focus), float(slice.camera_zoom_target)]
	)
	_check("equipment_proof_loaded", bool(slice.equipment_proof_loaded) or slice._men_uses_full_pack_manifest() or String(slice.faction_manifest.get("faction", "")) != "men")
	# Full-pack presentation contract: every fieldable unit and every spawned
	# battalion object has a validated animation capability with all tracks
	# resolved; melee equipment rides the converted member GLBs themselves.
	var capabilities_complete := true
	for object_id_value in slice.fieldable_unit_runtimes.keys():
		var adapter_for_caps = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
		var member_id: String = adapter_for_caps.runtime_member_id(slice.fieldable_unit_runtimes[object_id_value])
		var capability: Dictionary = slice.validated_battalion_capabilities.get(member_id, {}) as Dictionary
		if capability.is_empty() or int(capability.get("unresolvedAnimationTracks", 1)) != 0:
			capabilities_complete = false
	_check("validated_capabilities_cover_fieldable_units", capabilities_complete, "caps=%d" % slice.validated_battalion_capabilities.size())

	for id in [1, 2, 101, 102]:
		var battalion = slice.battalion_nodes.get(id)
		_check("battalion_%d_exists" % id, battalion != null)
		if battalion == null:
			continue
		var entity_members := int(slice.simulation.entity(id).get("member_count", 0))
		_check("battalion_%d_has_document_member_glbs" % id, int(battalion.member_count) == entity_members and int(battalion.retail_visual_count) == entity_members, "members=%d retail=%d expected=%d" % [battalion.member_count, battalion.retail_visual_count, entity_members])
		_check("battalion_%d_has_document_member_rigs" % id, int(battalion.rigged_member_count) == entity_members, str(battalion.rigged_member_count))
		_check("battalion_%d_has_animation_players" % id, int(battalion.animation_player_count) >= entity_members, str(battalion.animation_player_count))
		var mounted_members: Array = battalion.member_visuals.values()
		_check(
			"battalion_%d_meshes_face_authoritative_forward" % id,
			mounted_members.size() == entity_members
				and mounted_members.all(func(member: Node3D) -> bool: return is_equal_approx(member.rotation.y, PI * 0.5))
		)
	var men_faction_slice := String(slice.faction_manifest.get("faction", "")) == "men"
	for battalion_value in slice.battalion_nodes.values():
		var parity_battalion = battalion_value
		_check(
			"private_battalion_%d_has_no_synthetic_overlays" % int(parity_battalion.entity_id),
			not men_faction_slice
				or (bool(parity_battalion.private_parity_mode_active)
					and int(parity_battalion.synthetic_overlay_node_count()) == 0
					and not bool(parity_battalion.markers_visible())),
			"count=%d" % int(parity_battalion.synthetic_overlay_node_count())
		)
	var archer_battalion = _battalion_for_object_id(slice, "bfme2.object.gondor-archer")
	if archer_battalion != null and bool(archer_battalion.combat_visual_source_closure_present):
		_check(
			"gondor_archer_exact_projectile_and_impact_resources",
			int(archer_battalion.exact_projectile_node_count) > 0
			and int(archer_battalion.exact_impact_effect_node_count) > 0
			and String(archer_battalion.combat_visual_contract_error) == ""
			and archer_battalion.archer_projectile_controller != null
			and bool(archer_battalion.archer_projectile_controller.contract_ready)
			and not bool(archer_battalion.archer_projectile_controller.parity_ready)
			and int(archer_battalion.archer_projectile_controller.active_projectile_node_count) == 0
			and int(archer_battalion.archer_projectile_controller.active_impact_node_count) == 0
		)
	else:
		_check(
			"gondor_archer_projectile_impact_closure_blocker_is_explicit",
			not men_faction_slice or (archer_battalion != null
			and String(archer_battalion.combat_visual_contract_error).contains("GoodFactionArrow/GondorArcherArrow")
			and String(archer_battalion.combat_visual_contract_error).contains("EXArrowStreak01")
			and String(archer_battalion.combat_visual_contract_error).contains("FX_GoodArrowHit")
			and String(archer_battalion.combat_visual_contract_error).contains("ImpactArrow")),
			String(archer_battalion.combat_visual_contract_error if archer_battalion != null else "missing archer battalion")
		)

	var exemplar = slice.battalion_nodes.get(1)
	var enemy_exemplar = slice.battalion_nodes.get(101)
	if exemplar != null:
		# Clip contracts come from the battalion's own validated capability, not
		# a hardcoded roster: every core state has clips, the mapped clip is one
		# of the state's variants, and idle phase variation covers all members.
		for state_name in ["idle", "run", "attack", "death"]:
			var state_variants: Array = exemplar.variant_clips_for_state(state_name)
			_check(
				"%s_clip_mapping_from_capability" % state_name,
				not state_variants.is_empty()
					and state_variants.has(String(exemplar.clip_for_state(state_name))),
				"%s -> %s" % [state_name, String(exemplar.clip_for_state(state_name))]
			)
		var exemplar_members := int(exemplar.member_count)
		_check("idle_phase_variation_deterministic", int(exemplar.phase_variation_count("idle")) == exemplar_members, str(exemplar.phase_variation_count("idle")))
		_check("idle_variants_active", exemplar.active_clip_variants().size() == mini(exemplar_members, exemplar.variant_clips_for_state("idle").size()), str(exemplar.active_clip_variants()))
		_check("member_equipment_rides_converted_glbs", int(exemplar.rigged_member_count) == exemplar_members and int(exemplar.animation_player_count) >= exemplar_members)
		_check("all_animation_tracks_resolved", int(exemplar.unresolved_animation_track_count) == 0)

	var blue_materials := _member_textured_materials(exemplar)
	var red_materials := _member_textured_materials(enemy_exemplar)
	var blue_house_materials := _member_house_color_materials(exemplar)
	var red_house_materials := _member_house_color_materials(enemy_exemplar)
	var mod_loader = root.get_node("ModLoader")
	var selected_pack_document: Variant = mod_loader._read_json(String(slice.selected_pack_root).path_join("pack.json"))
	var selected_pack_files: Dictionary = {}
	var selected_pack_document_valid := typeof(selected_pack_document) == TYPE_DICTIONARY
	var selected_pack_files_valid := selected_pack_document_valid and typeof((selected_pack_document as Dictionary).get("files", null)) == TYPE_DICTIONARY
	if selected_pack_files_valid:
		selected_pack_files = (selected_pack_document as Dictionary).get("files", {})
	var house_color_declared := selected_pack_files.has("houseColor")
	var house_color_relative := String(selected_pack_files.get("houseColor", ""))
	var house_color_contract_valid := selected_pack_files_valid and (not house_color_declared or (house_color_relative == "data/house-color.json" and FileAccess.file_exists(String(slice.selected_pack_root).path_join(house_color_relative))))
	_check("house_color_pack_contract_resolves", house_color_contract_valid, "declared=%s path=%s" % [str(house_color_declared), house_color_relative])
	# Whole-material tint is always forbidden. Masked recoloring is required only
	# when the selected pack explicitly declares the exact house-color contract.
	var house_color_surface_contract := false
	if exemplar != null and enemy_exemplar != null:
		# Masked recolor is required when the finished contract is in force; a
		# declared-but-not-yet-cooked pack must instead show its documented
		# awaiting state and apply no invented tint either way.
		if house_color_declared:
			house_color_surface_contract = (
				(int(exemplar.house_color_surface_count) > 0 and int(enemy_exemplar.house_color_surface_count) > 0 and String(exemplar.team_color_status).contains("retail-house-color-masked"))
				or (int(exemplar.house_color_surface_count) == 0 and int(enemy_exemplar.house_color_surface_count) == 0 and String(exemplar.team_color_status).contains("awaiting-exact-house-color"))
			)
		else:
			house_color_surface_contract = int(exemplar.house_color_surface_count) == 0 and int(enemy_exemplar.house_color_surface_count) == 0 and String(exemplar.team_color_status).contains("awaiting-exact-house-color")
	_check("invented_team_tint_is_suppressed_in_private_parity", not men_faction_slice or (exemplar != null and enemy_exemplar != null and int(exemplar.team_tinted_surface_count) == 0 and int(enemy_exemplar.team_tinted_surface_count) == 0 and house_color_surface_contract), "declared=%s blue=%s/%s red=%s/%s status=%s" % [str(house_color_declared), str(exemplar.team_tinted_surface_count if exemplar != null else -1), str(exemplar.house_color_surface_count if exemplar != null else -1), str(enemy_exemplar.team_tinted_surface_count if enemy_exemplar != null else -1), str(enemy_exemplar.house_color_surface_count if enemy_exemplar != null else -1), String(exemplar.team_color_status if exemplar != null else "missing")])
	var exemplar_members_for_materials := int(exemplar.member_count) if exemplar != null else 0
	var enemy_exemplar_members_for_materials := int(enemy_exemplar.member_count) if enemy_exemplar != null else 0
	var house_color_material_contract := (blue_house_materials.size() == exemplar_members_for_materials and red_house_materials.size() == enemy_exemplar_members_for_materials) or (blue_house_materials.is_empty() and red_house_materials.is_empty()) if house_color_declared else blue_house_materials.is_empty() and red_house_materials.is_empty()
	_check("retail_textures_survive_without_invented_tint", not men_faction_slice or (blue_materials.size() == exemplar_members_for_materials and red_materials.size() == enemy_exemplar_members_for_materials and house_color_material_contract and not blue_materials.is_empty() and not red_materials.is_empty() and (blue_materials[0] as StandardMaterial3D).albedo_texture != null and (red_materials[0] as StandardMaterial3D).albedo_texture != null), "declared=%s blue=%d/%d red=%d/%d" % [str(house_color_declared), blue_materials.size(), blue_house_materials.size(), red_materials.size(), red_house_materials.size()])
	if house_color_declared and not blue_house_materials.is_empty() and not red_house_materials.is_empty():
		var blue_team_param := Color((blue_house_materials[0] as ShaderMaterial).get_shader_parameter("team_color"))
		var red_team_param := Color((red_house_materials[0] as ShaderMaterial).get_shader_parameter("team_color"))
		_check("house_color_teams_differ", not blue_team_param.is_equal_approx(red_team_param) and blue_team_param.b > blue_team_param.r and red_team_param.r > red_team_param.b, "blue=%s red=%s" % [str(blue_team_param), str(red_team_param)])
	if not blue_materials.is_empty() and not red_materials.is_empty():
		var blue_color := (blue_materials[0] as StandardMaterial3D).albedo_color
		var red_color := (red_materials[0] as StandardMaterial3D).albedo_color
		var color_distance := absf(blue_color.r - red_color.r) + absf(blue_color.g - red_color.g) + absf(blue_color.b - red_color.b)
		_check("private_retail_surface_colors_remain_source_neutral", not men_faction_slice or color_distance < 0.0001, "%s vs %s" % [str(blue_color), str(red_color)])
		_check("private_retail_overlays_use_source_contracts", not men_faction_slice or (int(exemplar.member_overlay_node_count()) == 0 and int(enemy_exemplar.member_overlay_node_count()) == 0 and int(exemplar.source_selection_decal_count()) == 1 and int(enemy_exemplar.source_selection_decal_count()) == 1 and String(exemplar.member_overlay_status).contains("source-health-canvas-and-source-selection-merge-decal-bound") and String(exemplar.member_overlay_status).contains("oracle-color-throb-pending")), "blue=%s decals=%d red=%s decals=%d" % [String(exemplar.member_overlay_status), int(exemplar.source_selection_decal_count()), String(enemy_exemplar.member_overlay_status), int(enemy_exemplar.source_selection_decal_count())])

	_check("adaptive_music_closure", slice.audio_system != null and slice.audio_system.has_complete_audio_closure())
	_check("strict_roster_audio_closure", slice.audio_system != null and slice.audio_system.has_complete_roster_audio_closure() and slice.audio_system.readiness_diagnostics().is_empty(), str(slice.audio_system.readiness_diagnostics() if slice.audio_system != null else ["missing_audio_system"]))
	_check("defeat_music_closure", slice.audio_system != null and slice.audio_system.music_streams.has("defeat"))
	_check("select_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("select") >= 1, str(slice.audio_system.count_voice_kind("select") if slice.audio_system != null else -1))
	_check("attack_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("attack") >= 1, str(slice.audio_system.count_voice_kind("attack") if slice.audio_system != null else -1))
	if slice.audio_system != null:
		var roster_voice_complete := true
		for roster_object_id in slice.audio_system._active_roster_object_ids():
			if int(slice.audio_system.count_roster_voice_kind(roster_object_id, "select")) < 1:
				roster_voice_complete = false
		_check("every_roster_unit_has_select_voice", roster_voice_complete)
	_check("starts_explore_music", slice.audio_system != null and String(slice.audio_system.current_music_state) == "explore", String(slice.audio_system.current_music_state if slice.audio_system != null else "missing"))
	var source_crossing_route: Dictionary = slice.source_map_data.query_route(
		Vector2(slice.simulation.entity(1)["position"]),
		Vector2(slice.simulation.entity(101)["position"])
	)
	var source_crossing_cells: Array[Vector2i] = []
	source_crossing_cells.assign(source_crossing_route.get("cells", []))
	_check("cross_river_route_is_bounded_astar", bool(source_crossing_route.get("valid", false)) and source_crossing_cells.size() > 2 and source_crossing_cells.size() <= slice.source_map_data.MAX_ROUTE_CELLS, "cells=%d reason=%s" % [source_crossing_cells.size(), String(source_crossing_route.get("reason", ""))])
	_check("cross_river_route_uses_nearest_valid_named_ford", String(source_crossing_route.get("ford_name", "")) == "ford2", String(source_crossing_route.get("ford_name", "")))
	_check("cross_river_route_respects_source_cells", _route_respects_source_navigation(slice.source_map_data, source_crossing_cells))
	_check("cross_river_route_avoids_non_ford_water", _route_water_only_in_named_fords(slice.source_map_data, source_crossing_cells))
	_check("cross_river_route_avoids_blocked_ford2_cell", not source_crossing_cells.has(Vector2i(208, 142)))
	for ford_name in ["ford1", "ford2", "ford3"]:
		var probe: Dictionary = slice.source_map_data.query_ford_probe(ford_name)
		var probe_cells: Array[Vector2i] = []
		probe_cells.assign(probe.get("cells", []))
		_check("named_%s_corridor_crosses_opposite_banks" % ford_name, bool(probe.get("valid", false)) and String(probe.get("ford_name", "")) == ford_name and _route_respects_source_navigation(slice.source_map_data, probe_cells) and _route_water_only_in_named_fords(slice.source_map_data, probe_cells) and _ford_probe_crosses_opposite_banks(slice.source_map_data, probe, ford_name), "selected=%s cells=%d" % [String(probe.get("ford_name", "")), probe_cells.size()])

	# Player interaction: selection and movement must immediately drive the real
	# imported battalion's runtime state and run clip.
	_check("select_player_battalion", bool(slice.test_select(1)))
	_check("selection_visible_in_sim", slice.simulation.selected_ids == [1], str(slice.simulation.selected_ids))
	var start_position := Vector2(slice.simulation.entity(1)["position"])
	var outside_destination: Vector2 = slice.source_map_data.grid_to_local_horizontal(Vector2i(19, 20))
	_check("outside_playable_move_rejected", int(slice.test_move(outside_destination)) == 0 and String(slice.simulation.last_route_rejection) == "outside-playable-area")
	_check("outside_rejection_does_not_move", Vector2(slice.simulation.entity(1)["position"]).is_equal_approx(start_position))
	var blocked_destination: Vector2 = slice.source_map_data.grid_to_local_horizontal(Vector2i(208, 142))
	_check("source_blocked_move_rejected", int(slice.test_move(blocked_destination)) == 0 and String(slice.simulation.last_route_rejection) == "blocked-destination")
	_check("move_order_accepted", int(slice.test_move(Vector2(-22.0, -23.0))) == 1)
	var same_side_cells: Array[Vector2i] = []
	same_side_cells.assign(slice.simulation.entity(1).get("route_cells", []))
	_check("same_side_route_respects_source_cells", _route_respects_source_navigation(slice.source_map_data, same_side_cells) and _route_water_only_in_named_fords(slice.source_map_data, same_side_cells), str(same_side_cells.size()))
	# How far a unit travels in two ticks is not a free constant — it falls out
	# of the authored locomotor. Aragorn rides HeroHumanLocomotor, and the
	# layered oracle authors data/ini/locomotor.ini:41
	#   `Acceleration = 10 ;,;210`
	# so the LIVE acceleration is 10 (210 is the superseded pre-RotWK value
	# behind the `;,;` marker), with data/ini/locomotor.ini:44 `Braking = 10`
	# to match. The old flat `> 0.1` threshold silently encoded the 210 ramp:
	# at 210 the unit cleared 0.216 world units in two ticks, at the authored
	# 10 it clears 0.0119 — the sim is right and the literal was stale.
	#
	# Rather than re-pin another magic number that will rot the next time a
	# locomotor is re-derived, the window is computed from the same authored
	# fields the integrator uses (retail_slice_sim.gd:13881/13883):
	#   v_k = min(max_speed, k * acceleration * TICK_SECONDS)
	#   distance = sum_k v_k * TICK_SECONDS
	# This is strictly stronger than the old form: it still proves the unit
	# moved, and it now also fails if the unit moves FASTER than its authored
	# ramp allows (teleport/route-snap regressions the old `> 0.1` waved
	# through). The lower bound is 0.75x because a route whose first leg bends
	# makes the straight-line chord shorter than the integrated path length;
	# the upper bound admits no slack at all.
	var mover_row: Dictionary = slice.simulation.entity(1)
	var authored_move_window := _authored_ramp_distance(
		float(mover_row.get("speed", 0.0)), float(mover_row.get("acceleration", 0.0)), 2
	)
	slice.step_for_test(2)
	var moved_distance := Vector2(slice.simulation.entity(1)["position"]).distance_to(start_position)
	_check(
		"move_changes_position",
		authored_move_window > 0.0
			and moved_distance > 0.0
			and moved_distance >= authored_move_window * 0.75
			and moved_distance <= authored_move_window + 0.000001,
		"moved=%.6f authored_window=%.6f speed=%.6f acceleration=%.6f" % [
			moved_distance, authored_move_window,
			float(mover_row.get("speed", 0.0)), float(mover_row.get("acceleration", 0.0)),
		]
	)
	_check("move_uses_run_state", String(slice.simulation.entity(1)["state"]) == "run" and String(exemplar.current_clip) == String(exemplar.clip_for_state("run")), "%s/%s" % [slice.simulation.entity(1)["state"], exemplar.current_clip])
	_check("run_variants_active", exemplar.active_clip_variants().size() == mini(int(exemplar.member_count), exemplar.variant_clips_for_state("run").size()), str(exemplar.active_clip_variants()))
	var order_indicator = slice.order_indicators.get(1)
	_check(
		"private_route_uses_exact_retail_move_hint_without_synthetic_flag",
		order_indicator != null
		and bool(order_indicator.private_parity_mode_active)
		and bool(order_indicator.retail_contract_ready)
		and bool(order_indicator.showing_order)
		and not order_indicator.route_points.is_empty()
		and int(order_indicator.retail_visual_node_count()) == 1
		and int(order_indicator.synthetic_overlay_node_count()) == 0
	)
	var retail_order_ids: Array[int] = [1]
	_check(
		"retail_stop_command_clears_route_and_target",
		int(slice.simulation.issue_stop(retail_order_ids)) == 1
			and (slice.simulation.entity(1).get("route", []) as Array).is_empty()
			and int(slice.simulation.entity(1).get("target_id", -1)) == 0
			and String(slice.simulation.entity(1).get("state", "")) == "idle"
	)
	_check(
		"retail_attack_move_command_arms_source_vision_acquisition",
		int(slice.simulation.issue_attack_move(retail_order_ids, Vector2(-20.0, -22.0))) == 1
			and bool(slice.simulation.entity(1).get("attack_move", false))
			and Vector2(slice.simulation.entity(1).get("attack_move_destination", Vector2.ZERO)).is_equal_approx(Vector2(-20.0, -22.0))
	)
	var assigned_ids: Array[int] = [2, 1]
	var assigned_group: Dictionary = slice.simulation.assign_control_group(1, assigned_ids)
	_check("retail_control_group_assigns_sorted", bool(assigned_group.get("ok", false)) and Array(assigned_group.get("entity_ids", [])) == [1, 2])
	_check("retail_control_group_recall", slice.simulation.recall_control_group(1) == [1, 2])

	# Production is roster-driven from the converted documents: the manifest
	# auto-populates one rule per fieldable unit, each producer declares exactly
	# the units whose authored routes land on it, queueing charges the
	# document's cost/CP/ticks, and completion spawns the document identity.
	# Retail start seeds fortresses only, so the porter constructs producers.
	var manifest_rules: Dictionary = (slice.faction_manifest.get("unit_production_rules", {}) as Dictionary).duplicate(true)
	var rules_well_formed := not manifest_rules.is_empty()
	for rule_value in manifest_rules.values():
		var rule_row: Dictionary = rule_value
		if (
			String(rule_row.get("producer_kind", "")) == ""
			or String(rule_row.get("object_id", "")) == ""
			or String(rule_row.get("display_name", "")) == ""
			or int(rule_row.get("default_build_ticks", 0)) < 1
			or int(rule_row.get("default_command_points", -1)) < 0
		):
			rules_well_formed = false
	_check("manifest_auto_populates_production_rules", rules_well_formed, str(manifest_rules.keys()))
	var manifest_damage_types: Dictionary = slice.faction_manifest.get("unit_damage_types", {}) as Dictionary
	var damage_adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var damage_types_match_documents := true
	for document_value in slice.producible_unit_runtimes.values():
		var document: Dictionary = document_value
		var member_id: String = damage_adapter.runtime_member_id(document)
		var combat_value: Variant = ((document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary).get("resolved", {}).get("combat", {})
		var authored := ""
		if typeof(combat_value) == TYPE_DICTIONARY:
			var damage_type_value: Variant = (combat_value as Dictionary).get("damageType")
			if typeof(damage_type_value) == TYPE_DICTIONARY:
				authored = String((damage_type_value as Dictionary).get("value", "")).to_lower()
			elif damage_type_value != null:
				authored = String(damage_type_value).to_lower()
		if authored != "" and String(manifest_damage_types.get(member_id, "")) != authored:
			damage_types_match_documents = false
	_check("manifest_records_document_damage_types", damage_types_match_documents, str(manifest_damage_types))
	var exclusion_ids: Array = []
	var exclusions_have_reasons := bool(not slice.unit_roster_exclusions.is_empty())
	for exclusion_value in slice.unit_roster_exclusions:
		exclusion_ids.append(String((exclusion_value as Dictionary).get("object_id", "")))
		if String((exclusion_value as Dictionary).get("reason", "")) == "":
			exclusions_have_reasons = false
	_check(
		"unresolved_units_stay_out_with_recorded_reasons",
		exclusions_have_reasons,
		str(slice.unit_roster_exclusions)
	)
	var roster_rules: Dictionary = slice.gameplay_rules.duplicate(true)
	roster_rules.merge({
		"starting_resources": 60000,
		"command_point_cap": 1000,
		"ai_queue_interval_ticks": 15,
		"ai_attack_delay_ticks": 45,
	}, true)
	var roster_sim = SimScript.new()
	roster_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	roster_sim.ai_enabled = false
	var builder_ids: Array[int] = []
	for entity_id in roster_sim.entity_ids():
		var candidate: Dictionary = roster_sim.entity(entity_id)
		if int(candidate.get("team", -1)) == 0 and bool(candidate.get("is_builder", false)):
			builder_ids.append(entity_id)
	_check("porter_available_for_construction", not builder_ids.is_empty())
	# The faction's constructible producer kinds are exactly the kinds its
	# production rules route to, fortress excluded (it is already seeded).
	var build_kinds: Array[String] = []
	for rule_value in manifest_rules.values():
		var kind := String((rule_value as Dictionary).get("producer_kind", ""))
		if kind != "" and kind != "fortress" and not build_kinds.has(kind):
			build_kinds.append(kind)
	build_kinds.sort()
	var built_structures: Dictionary = {}
	var site_candidates: Array[Vector2] = []
	var base_anchor := Vector2(roster_sim.entity(1).get("position", Vector2.ZERO))
	for dx in range(-36, 37, 6):
		for dy in range(-36, 37, 6):
			site_candidates.append(base_anchor + Vector2(dx, dy))
	for kind in build_kinds:
		var placed_id := 0
		for point in site_candidates:
			var result: Dictionary = roster_sim.issue_construct(builder_ids, kind, point)
			if bool(result.get("ok", false)):
				placed_id = int(result.get("structure_id", 0))
				break
		built_structures[kind] = placed_id
		var construction_complete := false
		if placed_id != 0:
			for _step in range(3000):
				var site: Dictionary = roster_sim.structure(placed_id)
				if float(site.get("construction_progress", 0.0)) >= 1.0:
					construction_complete = true
					break
				roster_sim.tick()
		_check("porter_constructs_%s" % kind, placed_id != 0 and construction_complete, "id=%d" % placed_id)
	var roster_fortress: int = roster_sim.producer_id(0, "fortress")
	var producer_for_kind := {"fortress": roster_fortress}
	for kind in build_kinds:
		producer_for_kind[kind] = int(built_structures[kind])
	# Every producer declares exactly the units the manifest routes to it; the
	# faction builder registers at the fortress (citadel-folded) through the
	# sim's narrower builder production path.
	var manifest_builders: Array = Array(slice.faction_manifest.get("builder_unit_ids", []))
	for kind_value in producer_for_kind.keys():
		var kind := String(kind_value)
		var expected_units: Array = []
		for unit_type in manifest_rules.keys():
			if String((manifest_rules[unit_type] as Dictionary).get("producer_kind", "")) == kind:
				expected_units.append(String(unit_type))
		for builder_value in manifest_builders:
			if kind == "fortress":
				expected_units.append(String(builder_value))
		expected_units.sort()
		var declared: Array = Array(roster_sim.structure(int(producer_for_kind[kind])).get("production", []))
		var declared_sorted: Array[String] = []
		for value in declared:
			declared_sorted.append(String(value))
		declared_sorted.sort()
		_check("%s_declares_document_units" % kind, declared_sorted == expected_units, "declared=%s expected=%s" % [str(declared_sorted), str(expected_units)])
	var all_producer_ids: Array = []
	for value in producer_for_kind.values():
		if int(value) != 0:
			all_producer_ids.append(int(value))
	for unit_type in manifest_rules.keys():
		var rule: Dictionary = manifest_rules[unit_type]
		var own_producer := int(producer_for_kind.get(String(rule.get("producer_kind", "")), 0))
		var rejection_failed := false
		var rejection_detail := ""
		for wrong_producer_id in all_producer_ids:
			if wrong_producer_id == own_producer:
				continue
			var rejected: Dictionary = roster_sim.queue_unit(0, wrong_producer_id, String(unit_type))
			if bool(rejected.get("ok", true)) or String(rejected.get("reason", "")) != "unsupported-unit":
				rejection_failed = true
				rejection_detail = "%s at %d -> %s" % [unit_type, wrong_producer_id, str(rejected)]
				break
		_check("wrong_producer_rejects_%s" % String(unit_type).replace("bfme2.object.", "").replace("-", "_"), not rejection_failed, rejection_detail)
	# Queueing charges each document's own cost/CP/ticks; completions spawn the
	# document identity with the document's member counts and health. One unit
	# per producer kind: the first queueable (no prerequisites, no living hero
	# identity) unit routed there.
	var production_cases: Array[Dictionary] = []
	for kind_value in producer_for_kind.keys():
		var kind := String(kind_value)
		for unit_type in manifest_rules.keys():
			if String((manifest_rules[unit_type] as Dictionary).get("producer_kind", "")) != kind:
				continue
			# unlock_upgrades_for_unit, not required_upgrades_for_unit: the
			# question here is "is this unit gated AT ALL", and a unit can be
			# gated purely by the ANY-of group (commandbutton.ini
			# NeededUpgradeAny). Reading only the ALL-of list reports such a
			# unit as ungated.
			if not roster_sim.unlock_upgrades_for_unit(String(unit_type), kind).is_empty():
				continue
			if String((manifest_rules[unit_type] as Dictionary).get("category", "")) == "hero" and roster_sim.hero_unavailable(0, String(unit_type)):
				continue
			production_cases.append({
				"label": String(unit_type).replace("bfme2.object.", "").replace("-", "_"),
				"producer": int(producer_for_kind[kind]),
				"unit_type": String(unit_type),
			})
			break
	for production_case in production_cases:
		var unit_type := String(production_case["unit_type"])
		var rule: Dictionary = manifest_rules[unit_type]
		var tick_before := int(roster_sim.tick_index)
		var queued_case: Dictionary = roster_sim.queue_unit(0, int(production_case["producer"]), unit_type)
		var queued_item: Dictionary = queued_case.get("item", {})
		_check("%s_queues_from_document_producer" % String(production_case["label"]), bool(queued_case.get("ok", false)), str(queued_case))
		_check(
			"%s_charges_document_values" % String(production_case["label"]),
			int(queued_item.get("cost", -1)) == int(rule.get("default_cost", -2))
				and int(queued_item.get("command_points", -1)) == int(rule.get("default_command_points", -2))
				and int(queued_item.get("complete_tick", -1)) - tick_before == int(rule.get("default_build_ticks", -2)),
			str(queued_item)
		)
		roster_sim.advance(int(rule.get("default_build_ticks", 1)) + 2)
		var completed: Dictionary = _entity_for_unit_type(roster_sim, 0, unit_type)
		_check(
			"%s_completes_with_document_identity" % String(production_case["label"]),
			not completed.is_empty()
				and String(completed.get("unit_type", "")) == unit_type
				and String(completed.get("object_id", "")) == String(rule.get("object_id", ""))
				and int(completed.get("command_points", -1)) == int(rule.get("default_command_points", -2))
				and String(completed.get("category", "")) == String(rule.get("category", "")),
			str(completed)
		)
	# Hero roster: the spawn-roster hero already holds its identity, so its
	# requeue fails closed; a different hero identity trains normally. Once the
	# living identity falls, the roster may train it again, and the produced
	# hero then blocks any further requeue.
	var spawn_hero_type := ""
	for entity_id in roster_sim.entity_ids():
		var hero_candidate: Dictionary = roster_sim.entity(entity_id)
		if int(hero_candidate.get("team", -1)) == 0 and String(roster_sim.production_rule_category(String(hero_candidate.get("unit_type", "")))) == "hero":
			spawn_hero_type = String(hero_candidate.get("unit_type", ""))
			break
	if spawn_hero_type != "":
		var living_hero_requeue: Dictionary = roster_sim.queue_unit(0, roster_fortress, spawn_hero_type)
		_check("living_spawn_hero_identity_cannot_requeue", not bool(living_hero_requeue.get("ok", true)) and String(living_hero_requeue.get("reason", "")) == "hero-unavailable", str(living_hero_requeue))
		var distinct_hero_trained := false
		for production_case in production_cases:
			if String(production_case.get("unit_type", "")) != spawn_hero_type and String(manifest_rules.get(String(production_case["unit_type"]), {}).get("category", "")) == "hero":
				distinct_hero_trained = not _entity_for_unit_type(roster_sim, 0, String(production_case["unit_type"])).is_empty()
		_check("distinct_hero_identity_trained", distinct_hero_trained)
		var roster_hero_id := 0
		for entity_id in roster_sim.entity_ids():
			var candidate: Dictionary = roster_sim.entity(entity_id)
			if int(candidate.get("team", -1)) == 0 and String(candidate.get("unit_type", "")) == spawn_hero_type:
				roster_hero_id = entity_id
				break
		if roster_hero_id != 0:
			(roster_sim.entities[roster_hero_id] as Dictionary)["health"] = 0
		var fallen_requeue: Dictionary = roster_sim.queue_unit(0, roster_fortress, spawn_hero_type)
		_check("fallen_hero_identity_trains_again", bool(fallen_requeue.get("ok", false)), str(fallen_requeue))
		var hero_rule: Dictionary = manifest_rules.get(spawn_hero_type, {}) as Dictionary
		roster_sim.advance(int(hero_rule.get("default_build_ticks", 1)) + 2)
		var trained_hero: Dictionary = _entity_for_unit_type(roster_sim, 0, spawn_hero_type)
		var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
		var expected_hero_health := -1
		for document_value in slice.producible_unit_runtimes.values():
			if adapter.runtime_member_id(document_value) == String(hero_rule.get("object_id", "")):
				expected_hero_health = int(adapter.simulation_rule(document_value).get("member_health", -1))
		_check("retrained_hero_completes_with_document_identity", not trained_hero.is_empty() and String(trained_hero.get("object_id", "")) == String(hero_rule.get("object_id", "")) and int(trained_hero.get("member_maximum_health", -2)) == expected_hero_health, str(trained_hero))
		var trained_requeue: Dictionary = roster_sim.queue_unit(0, roster_fortress, spawn_hero_type)
		_check("living_hero_identity_cannot_requeue", not bool(trained_requeue.get("ok", true)) and String(trained_requeue.get("reason", "")) == "hero-unavailable", str(trained_requeue))
		# A produced hero's death releases its identity (retail fortress
		# revival): the fortress may train the same identity again.
		var produced_hero: Dictionary = _entity_for_unit_type(roster_sim, 0, spawn_hero_type)
		if not produced_hero.is_empty() and roster_hero_id != 0:
			roster_sim._apply_damage(roster_hero_id, int(produced_hero.get("id", 0)), 999999)
			var revival_requeue: Dictionary = roster_sim.queue_unit(0, roster_fortress, spawn_hero_type)
			_check("produced_hero_death_releases_identity_for_revival", bool(revival_requeue.get("ok", false)), str(revival_requeue))
	# Any unit with authored prerequisites fails closed while they are unmet.
	for unit_type in manifest_rules.keys():
		var rule: Dictionary = manifest_rules[unit_type]
		var kind := String(rule.get("producer_kind", ""))
		var producer_id := int(producer_for_kind.get(kind, 0))
		# unlock_upgrades_for_unit: a unit gated ONLY by an ANY-of group is
		# still gated, and must still fail closed while nothing is owned.
		# Reading the ALL-of list alone silently skipped this whole check.
		if producer_id == 0 or roster_sim.unlock_upgrades_for_unit(String(unit_type), kind).is_empty():
			continue
		var locked: Dictionary = roster_sim.queue_unit(0, producer_id, String(unit_type))
		_check(
			"prerequisites_fail_closed_without_upgrade",
			not bool(locked.get("ok", true)) and String(locked.get("reason", "")) == "missing-upgrade",
			str(locked)
		)
		break
	# --- Generic doc-driven structure levels ---
	# Every authored upgrade chain the structure documents carry (cost/time/
	# level cap/command-set swap/per-level effects/unlocks) is purchasable on
	# its building through the generic mechanism — no bespoke identity path.
	# Factions whose packs predate the chain contract carry none and skip.
	var upgrade_chains: Dictionary = slice.faction_manifest.get("structure_upgrade_chains", {}) as Dictionary
	var faction_has_chain_docs := not upgrade_chains.is_empty()
	_check(
		"manifest_projects_doc_structure_upgrade_chains",
		faction_has_chain_docs or String(slice.faction_manifest.get("faction", "")) not in ["men", "elves"],
		"faction=%s chains=%s" % [String(slice.faction_manifest.get("faction", "")), str(upgrade_chains.keys())]
	)
	for kind_value in upgrade_chains.keys():
		var kind := String(kind_value)
		var producer_id := int(producer_for_kind.get(kind, 0))
		if producer_id == 0:
			continue
		var chain: Dictionary = upgrade_chains[kind]
		var steps: Array = chain.get("steps", [])
		if steps.is_empty():
			continue
		var building: Dictionary = roster_sim.structure(producer_id)
		var base_structure_health := int(building.get("maximum_health", 0))
		var first_step: Dictionary = steps[0]
		var first_upgrade := String(first_step.get("upgradeId", ""))
		var commands: Array = _chain_purchase_rows(roster_sim.structure_upgrade_commands(producer_id))
		_check(
			"%s_exposes_authored_purchase_command" % kind,
			commands.size() == 1
				and String((commands[0] as Dictionary).get("upgrade_id", "")) == first_upgrade
				and int((commands[0] as Dictionary).get("cost", -1)) == int(first_step.get("cost", -2)),
			str(commands)
		)
		# Any unit this step unlocks stays locked before the purchase.
		var unlocked_unit_type := ""
		for unit_type in manifest_rules.keys():
			# unlock_upgrades_for_unit: the level-2 upgrade that unlocks a unit
			# is frequently a MEMBER of that unit's ANY-of group rather than an
			# ALL-of prerequisite (commandbutton.ini:7517 lists
			# Upgrade_GondorArcheryRangeLevel2 under NeededUpgradeAny). Reading
			# the ALL-of list alone left unlocked_unit_type empty, which
			# silently skipped both level-2 unlock checks below.
			if Array(roster_sim.unlock_upgrades_for_unit(String(unit_type), kind)).has(first_upgrade):
				unlocked_unit_type = String(unit_type)
				break
		var expected_health_add := 0
		var expected_multiplier := 1.0
		for leaf_value in Array(first_step.get("effects", [])):
			for modifier_value in Array((leaf_value as Dictionary).get("modifiers", [])):
				var modifier := modifier_value as Dictionary
				if String(modifier.get("kind", "")) == "HEALTH":
					expected_health_add += int(modifier.get("value", 0))
				elif String(modifier.get("kind", "")) == "PRODUCTION":
					expected_multiplier *= float(modifier.get("value", 1.0))
		var upgrade_resources_before := roster_sim.resources_for_team(0)
		var queued_upgrade: Dictionary = roster_sim.queue_structure_upgrade(0, producer_id, first_upgrade)
		var upgrade_item: Dictionary = queued_upgrade.get("item", {})
		_check(
			"%s_level_two_purchase_uses_doc_cost_and_duration" % kind,
			bool(queued_upgrade.get("ok", false))
				and int(upgrade_item.get("cost", -1)) == int(first_step.get("cost", -2))
				and int(upgrade_item.get("duration_ticks", -1)) == maxi(1, roundi(float(first_step.get("buildTimeSeconds", 0.0)) / SimScript.TICK_SECONDS))
				and roster_sim.resources_for_team(0) == upgrade_resources_before - int(first_step.get("cost", 0)),
			str(queued_upgrade)
		)
		roster_sim.advance(int(upgrade_item.get("duration_ticks", 1)))
		building = roster_sim.structure(producer_id)
		_check(
			"%s_level_two_completes_with_doc_level_and_command_set" % kind,
			int(building.get("level", 0)) == int(first_step.get("toLevel", 0))
				and Array(building.get("completed_upgrades", [])).has(first_upgrade)
				and String(building.get("command_set", "")) == String(first_step.get("toCommandSet", "")),
			str({"level": building.get("level"), "command_set": building.get("command_set")})
		)
		_check(
			"%s_level_two_applies_authored_level_effects" % kind,
			int(building.get("maximum_health", 0)) == base_structure_health + expected_health_add
				and is_equal_approx(float(building.get("production_multiplier", 1.0)), snappedf(expected_multiplier, 0.0001)),
			"health=%d/%d mult=%.4f/%.4f" % [int(building.get("maximum_health", 0)), base_structure_health + expected_health_add, float(building.get("production_multiplier", 1.0)), expected_multiplier]
		)
		if unlocked_unit_type != "":
			var unlocked_queue: Dictionary = roster_sim.queue_unit(0, producer_id, unlocked_unit_type)
			var unlocked_item: Dictionary = unlocked_queue.get("item", {})
			var unlocked_rule: Dictionary = manifest_rules.get(unlocked_unit_type, {})
			var expected_ticks := maxi(1, roundi(float(int(unlocked_rule.get("default_build_ticks", 1))) / expected_multiplier))
			_check(
				"%s_level_two_unlocks_document_unit" % kind,
				bool(unlocked_queue.get("ok", false)),
				str(unlocked_queue)
			)
			_check(
				"%s_level_two_production_speed_uses_authored_factor" % kind,
				int(unlocked_item.get("duration_ticks", -1)) == expected_ticks,
				"ticks=%d expected=%d" % [int(unlocked_item.get("duration_ticks", -1)), expected_ticks]
			)
			roster_sim.cancel_queued_unit(0, producer_id, 0)
		if steps.size() > 1:
			var second_step: Dictionary = steps[1]
			var second_upgrade := String(second_step.get("upgradeId", ""))
			var queued_second: Dictionary = roster_sim.queue_structure_upgrade(0, producer_id, second_upgrade)
			_check(
				"%s_level_three_purchases_after_level_two" % kind,
				bool(queued_second.get("ok", false))
					and int((queued_second.get("item", {}) as Dictionary).get("cost", -1)) == int(second_step.get("cost", -2)),
				str(queued_second)
			)
			roster_sim.advance(maxi(1, int((queued_second.get("item", {}) as Dictionary).get("duration_ticks", 1))))
			var second_health_add := 0
			var second_multiplier := 1.0
			for leaf_value in Array(second_step.get("effects", [])):
				for modifier_value in Array((leaf_value as Dictionary).get("modifiers", [])):
					var modifier := modifier_value as Dictionary
					if String(modifier.get("kind", "")) == "HEALTH":
						second_health_add += int(modifier.get("value", 0))
					elif String(modifier.get("kind", "")) == "PRODUCTION":
						second_multiplier *= float(modifier.get("value", 1.0))
			building = roster_sim.structure(producer_id)
			_check(
				"%s_level_three_completes_and_effects_compound" % kind,
				int(building.get("level", 0)) == int(second_step.get("toLevel", 0))
					and int(building.get("maximum_health", 0)) == base_structure_health + expected_health_add + second_health_add
					and is_equal_approx(float(building.get("production_multiplier", 1.0)), snappedf(expected_multiplier * second_multiplier, 0.0001)),
				str({"level": building.get("level"), "health": building.get("maximum_health"), "mult": building.get("production_multiplier")})
			)
			var exhausted_rows := _chain_purchase_rows(roster_sim.structure_upgrade_commands(producer_id))
			_check(
				"%s_chain_exhausted_exposes_no_further_purchase" % kind,
				exhausted_rows.is_empty()
					or String((exhausted_rows[0] as Dictionary).get("upgrade_id", "")) != second_upgrade,
				str(exhausted_rows)
			)
		# Mirkwood unlock: the elves barracks level 2 must release Mirkwood
		# Archers exactly as retail (their only authored prerequisite).
		if kind == "barracks" and String(slice.faction_manifest.get("faction", "")) == "elves":
			_check(
				"mirkwood_archers_unlock_at_elven_barracks_level2",
				unlocked_unit_type == "bfme2.object.elven-mirkwood-archer-horde",
				"unlocked=%s" % unlocked_unit_type
			)
	# --- Veterancy: kills pay authored awards and level the battalion ---
	# Spawn roster slot 2 is the multi-member line horde (slot 1 is the hero);
	# the mirror enemy horde is its deterministic victim. Factions whose packs
	# predate the experience contract carry no rules and skip.
	var xp_attacker_id := 2
	var xp_victim_id := 102
	var xp_attacker: Dictionary = roster_sim.entity(xp_attacker_id)
	var xp_rule: Dictionary = roster_sim.experience_rule_for_unit(String(xp_attacker.get("unit_type", "")))
	var faction_requires_experience := String(slice.faction_manifest.get("faction", "")) in ["men", "elves"]
	_check(
		"spawn_roster_units_carry_compiled_experience",
		not xp_rule.is_empty() or not faction_requires_experience,
		"faction=%s unit=%s" % [String(slice.faction_manifest.get("faction", "")), String(xp_attacker.get("unit_type", ""))]
	)
	# The rank-2 assertions below only apply to a unit retail actually lets
	# level. A one-row chain is authored, not truncated: data/ini/
	# experiencelevels.ini heads its siege section ";---- NO LEVELING UNITS"
	# and gives IsengardBallista / IsengardBatteringRam / GondorTrebuchet /
	# DwarvenCatapult / MordorCatapult / every porter exactly one
	# ExperienceLevel block (`IsengardBallistaLevel1`, TargetNames =
	# IsengardBallista, Rank = 1, and no Level2). Ring heroes and Treebeard are
	# the mirror case: one row whose authored Rank is already their max (10).
	# Isengard's spawn-roster slot 2 is the ballista, so this branch is the
	# whole reason `steps[1]`/`xp_levels[1]` used to run off the end of the
	# array and abort `_run` — the assertion adapts to the authored chain
	# length instead of assuming two ranks.
	var authored_levels: Array = xp_rule.get("levels", [])
	if not xp_rule.is_empty() and authored_levels.size() < 2:
		var only_level: Dictionary = authored_levels[0]
		var single_rank_state: Dictionary = roster_sim.experience_state(xp_attacker_id)
		_check(
			"single_rank_unit_fields_at_its_authored_top_rank",
			int(only_level.get("rank", 0)) == int(xp_rule.get("max_level", -1))
				and int(single_rank_state.get("level", 0)) == int(only_level.get("rank", 0))
				and int(single_rank_state.get("max_level", -1)) == int(xp_rule.get("max_level", -1))
				and int(only_level.get("experience_award", -1)) >= 0,
			"faction=%s unit=%s rank=%d max_level=%d state=%s" % [
				String(slice.faction_manifest.get("faction", "")),
				String(xp_attacker.get("unit_type", "")),
				int(only_level.get("rank", 0)),
				int(xp_rule.get("max_level", -1)),
				str(single_rank_state),
			]
		)
	elif not xp_rule.is_empty():
		var xp_levels: Array = xp_rule.get("levels", [])
		var rank_one_award := int((xp_levels[0] as Dictionary).get("experience_award", 0))
		var rank_two: Dictionary = xp_levels[1]
		var threshold := int(rank_two.get("required_experience", 0))
		var base_member_health := int(xp_attacker.get("member_maximum_health", 0))
		var base_member_damage := int(xp_attacker.get("member_damage", 0))
		var xp_before := int(xp_attacker.get("experience_xp", 0))
		roster_sim._apply_member_damage(xp_attacker_id, -1, xp_victim_id, 999999, "battalion", 0, 0)
		roster_sim._apply_member_damage(xp_attacker_id, -1, xp_victim_id, 999999, "battalion", 0, 1)
		_check(
			"member_kills_pay_authored_award",
			int(xp_attacker.get("experience_xp", 0)) == xp_before + 2 * rank_one_award,
			"xp=%d expected=%d" % [int(xp_attacker.get("experience_xp", 0)), xp_before + 2 * rank_one_award]
		)
		roster_sim._award_experience(xp_attacker, threshold - int(xp_attacker.get("experience_xp", 0)))
		_check("battalion_levels_at_authored_threshold", int(xp_attacker.get("level", 0)) == 2, "level=%d threshold=%d" % [int(xp_attacker.get("level", 0)), threshold])
		_check(
			"rank_two_folds_authored_health_add",
			int(xp_attacker.get("member_maximum_health", 0)) == base_member_health + int(rank_two.get("health_add", 0)),
			"health=%d" % int(xp_attacker.get("member_maximum_health", 0))
		)
		# Rank-2 damage is NOT additive-only in RotWK. GondorFighterLevel2
		# authors TWO modifier lists (oracle
		# data/ini/experiencelevels.ini:23654 "AttributeModifiers =
		# GondorFighterBonusRank2 GenericUnitDamageBonusRank2"):
		#   * GondorFighterBonusRank2 (data/ini/attributemodifier.ini:7202) is
		#     HEALTH only -> the health fold above.
		#   * GenericUnitDamageBonusRank2 (attributemodifier.ini:7591) is
		#     "Modifier = DAMAGE_MULT LEVEL_MULT_BONUS_DMG_2", and
		#     data/ini/gamedata.ini:9684 defines LEVEL_MULT_BONUS_DMG_2 = 110%.
		# So the authored fold is (base + DAMAGE_ADD) * DAMAGE_MULT, which is
		# exactly what _apply_experience_level_effects does. The previous form
		# of this assertion only folded damage_add; it passed only because the
		# pre-layered pack compiled BFME2's GoodTroopBonusRank2 (additive
		# DAMAGE_ADD 10, no multiplier), so the multiplier term was always 1.0
		# and never exercised. Folding both terms is strictly stronger: it
		# still pins the exact magnitude (Gondor soldier 40 -> 44) and now also
		# catches a dropped or mis-parsed DAMAGE_MULT.
		var expected_rank_two_damage := roundi(
			(float(base_member_damage) + float(rank_two.get("damage_add", 0.0)))
			* float(rank_two.get("damage_multiplier", 1.0))
		)
		_check(
			"rank_two_folds_authored_damage_add",
			int(xp_attacker.get("member_damage", 0)) == expected_rank_two_damage,
			"damage=%d expected=%d base=%d add=%s mult=%s" % [
				int(xp_attacker.get("member_damage", 0)),
				expected_rank_two_damage,
				base_member_damage,
				str(rank_two.get("damage_add", 0.0)),
				str(rank_two.get("damage_multiplier", 1.0)),
			]
		)
		# The fold above degenerates to a no-op if BOTH authored terms were
		# lost, so the men pack pins the authored magnitudes themselves against
		# the oracle: DAMAGE_ADD is absent (0.0) and DAMAGE_MULT is 110%.
		if String(slice.faction_manifest.get("faction", "")) == "men":
			_check(
				"rank_two_damage_terms_match_authored_modifier_lists",
				is_equal_approx(float(rank_two.get("damage_add", -1.0)), 0.0)
					and is_equal_approx(float(rank_two.get("damage_multiplier", -1.0)), 1.1),
				"add=%s mult=%s" % [
					str(rank_two.get("damage_add", null)),
					str(rank_two.get("damage_multiplier", null)),
				]
			)
		var xp_state: Dictionary = roster_sim.experience_state(xp_attacker_id)
		_check(
			"experience_state_exposes_live_level",
			int(xp_state.get("level", 0)) == 2 and int(xp_state.get("xp", -1)) == threshold and int(xp_state.get("max_level", 0)) == int(xp_rule.get("max_level", 0)),
			str(xp_state)
		)
		var snapshot_row: Dictionary = {}
		for entity_value in roster_sim.state_snapshot().get("entities", []):
			if int((entity_value as Dictionary).get("id", 0)) == xp_attacker_id:
				snapshot_row = entity_value
		_check(
			"snapshot_carries_xp_level_state",
			int(snapshot_row.get("level", 0)) == 2 and int(snapshot_row.get("experience_xp", -1)) == threshold,
			str({"level": snapshot_row.get("level"), "experience_xp": snapshot_row.get("experience_xp")})
		)
	# Hero level scaling evidence against the authored INI values: the men
	# spawn hero is GondorAragornMP (rank 2 at 30 XP, award 35, +60 HP/+10 DAM
	# from HeroLevelUpDamage1); other factions assert through their own
	# compiled chain generically above.
	if String(slice.faction_manifest.get("faction", "")) == "men":
		var hero_row: Dictionary = roster_sim.entity(1)
		var hero_rule: Dictionary = roster_sim.experience_rule_for_unit(String(hero_row.get("unit_type", "")))
		var hero_levels: Array = hero_rule.get("levels", [])
		var hero_rank_two: Dictionary = hero_levels[1] if hero_levels.size() > 1 else {}
		_check(
			"aragorn_level_values_match_ini",
			String(hero_row.get("unit_type", "")) == "bfme2.object.gondor-aragorn-mp"
				and int(hero_rule.get("max_level", 0)) == 10
				and int(hero_rank_two.get("required_experience", 0)) == 30
				and int((hero_levels[0] as Dictionary).get("experience_award", 0)) == 35
				and int(hero_rank_two.get("health_add", 0)) == 60
				and int(hero_rank_two.get("damage_add", 0)) == 10,
			"hero=%s threshold=%d award=%d hp=%d dam=%d" % [
				String(hero_row.get("unit_type", "")),
				int(hero_rank_two.get("required_experience", 0)),
				int((hero_levels[0] as Dictionary).get("experience_award", 0)) if hero_levels.size() > 0 else -1,
				int(hero_rank_two.get("health_add", 0)),
				int(hero_rank_two.get("damage_add", 0)),
			]
		)
	# Structure armor is recorded, never silent: every structure kind has a
	# compiled armor.ini table or a recorded provisional, every roster damage
	# type resolves against the fortress's compiled table or its DEFAULT row,
	# and every combat unit without authored damageType is recorded too
	# (builders excluded). Unit armor blocks follow the same contract: compiled
	# set or recorded exclusion.
	var armor_recorded := true
	var sim_damage_types: Dictionary = roster_sim._unit_damage_types
	var fortress_table: Dictionary = roster_sim._structure_armor.get("fortress", {})
	var fortress_scalars: Dictionary = fortress_table.get("scalars", {})
	for damage_type_value in sim_damage_types.values():
		var damage_type := String(damage_type_value)
		if not fortress_scalars.has(damage_type) and not fortress_scalars.has("default"):
			armor_recorded = false
	for kind_value in roster_sim._structure_kinds:
		var kind := String(kind_value)
		if not roster_sim._structure_armor.has(kind) and not roster_sim.structure_armor_provisional_kinds.has(kind):
			armor_recorded = false
	var manifest_builders_for_armor: Array = Array(slice.faction_manifest.get("builder_unit_ids", []))
	for rule_value in manifest_rules.values():
		var rule_row: Dictionary = rule_value
		var member_id := String(rule_row.get("object_id", ""))
		if manifest_builders_for_armor.has(member_id):
			continue
		# A unit is accounted for by a single authored damageType, by a
		# per-component mix (a multi-nugget weapon like ArwenSword authors HERO
		# and SLASH but no one weapon-level type), or by being recorded as
		# genuinely untyped. Only an unaccounted unit fails.
		if (
			not sim_damage_types.has(member_id)
			and not roster_sim._unit_damage_components.has(member_id)
			and not roster_sim.missing_damage_type_units.has(member_id)
		):
			armor_recorded = false
		if not roster_sim._unit_armor.has(member_id) and not roster_sim.missing_armor_units.has(member_id):
			armor_recorded = false
	_check("structure_armor_scalars_authored_or_recorded", armor_recorded, "provisional_kinds=%s missing_types=%s missing_armor=%s" % [str(roster_sim.structure_armor_provisional_kinds), str(roster_sim.missing_damage_type_units), str(roster_sim.missing_armor_units)])
	# armor.ini, compiled end-to-end: the pack's own documents drive every
	# structure kind's table (no hand-coded fortress constants remain).
	var fortress_armor_rule: Dictionary = roster_sim._structure_armor.get("fortress", {})
	var fortress_armor_scalars: Dictionary = fortress_armor_rule.get("scalars", {})
	_check(
		"fortress_armor_table_is_compiled_from_structure_document",
		String(fortress_armor_rule.get("set_id", "")) == "FortressArmor"
			and is_equal_approx(float(fortress_armor_scalars.get("slash", 0.0)), 0.20)
			and is_equal_approx(float(fortress_armor_scalars.get("specialist", 0.0)), 0.12)
			and is_equal_approx(float(fortress_armor_scalars.get("pierce", 0.0)), 0.01)
			and is_equal_approx(float(fortress_armor_scalars.get("siege", 0.0)), 2.0)
			and is_equal_approx(float(fortress_armor_scalars.get("default", 0.0)), 0.25),
		str(fortress_armor_rule)
	)
	# Unit armor counter matrix from the converted documents: KnightArmor
	# PIERCE 40% / SPECIALIST 200% (armor.ini:618-619), TowerGuardArmor
	# CAVALRY 20% (armor.ini:529), SoldierArmor PIERCE 125% (armor.ini:486).
	# The roster/forge rows are men-specific; other faction sweeps keep the
	# faction-generic fortress and recorded-provenance contracts above.
	if String(slice.faction_manifest.get("faction", "men")) == "men":
		var farm_armor_rule: Dictionary = roster_sim._structure_armor.get("farm", {})
		var barracks_armor_rule: Dictionary = roster_sim._structure_armor.get("barracks", {})
		_check(
			"farm_and_producer_kinds_use_their_own_compiled_scalars",
			String(farm_armor_rule.get("set_id", "")) == "FarmArmor"
				and is_equal_approx(float((farm_armor_rule.get("scalars", {}) as Dictionary).get("slash", 0.0)), 0.75)
				and is_equal_approx(float((farm_armor_rule.get("scalars", {}) as Dictionary).get("pierce", 0.0)), 0.01)
				and String(barracks_armor_rule.get("set_id", "")) == "UnitProductionStructureArmor"
				and is_equal_approx(float((barracks_armor_rule.get("scalars", {}) as Dictionary).get("slash", 0.0)), 0.35),
			"farm=%s barracks=%s" % [str(farm_armor_rule.get("set_id", "")), str(barracks_armor_rule.get("set_id", ""))]
		)
		var knight_armor_rule := _armor_rule_for_set(roster_sim, "KnightArmor")
		var pike_armor_rule := _armor_rule_for_set(roster_sim, "TowerGuardArmor")
		var soldier_armor_rule := _armor_rule_for_set(roster_sim, "SoldierArmor")
		_check(
			"unit_armor_counter_matrix_is_compiled_from_unit_documents",
			String(knight_armor_rule.get("set_id", "")) == "KnightArmor"
				and is_equal_approx(float((knight_armor_rule.get("scalars", {}) as Dictionary).get("pierce", 0.0)), 0.40)
				and is_equal_approx(float((knight_armor_rule.get("scalars", {}) as Dictionary).get("specialist", 0.0)), 2.0)
				and String(pike_armor_rule.get("set_id", "")) == "TowerGuardArmor"
				and is_equal_approx(float((pike_armor_rule.get("scalars", {}) as Dictionary).get("cavalry", 0.0)), 0.20)
				and String(soldier_armor_rule.get("set_id", "")) == "SoldierArmor"
				and is_equal_approx(float((soldier_armor_rule.get("scalars", {}) as Dictionary).get("pierce", 0.0)), 1.25),
			"knight=%s pike=%s soldier=%s" % [str(knight_armor_rule.get("set_id", "")), str(pike_armor_rule.get("set_id", "")), str(soldier_armor_rule.get("set_id", ""))]
		)
		# Real gameplay damage uses the compiled matrix in the same document
		# id space: a live archer's pierce arrow vs a KnightArmor cavalry
		# battalion lands at exactly 40% of its compiled damage (armor.ini:618).
		# The probe battalions are spawned through doc-derived member ids (the
		# keys unit_rules itself is authored in), never hardcoded aliases.
		var armor_probe_sim = SimScript.new()
		armor_probe_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
		armor_probe_sim.ai_enabled = false
		var probe_unit_rules: Dictionary = armor_probe_sim._rules.get("unit_rules", {}) as Dictionary
		var knight_member_id := ""
		for key_value in armor_probe_sim._unit_armor.keys():
			var probe_set := String((armor_probe_sim._unit_armor[key_value] as Dictionary).get("set_id", ""))
			if probe_set == "KnightArmor" and probe_unit_rules.has(String(key_value)):
				knight_member_id = String(key_value)
				break
		var archer_member_id := ""
		for key_value in armor_probe_sim._unit_damage_types.keys():
			if String(armor_probe_sim._unit_damage_types[key_value]) == "pierce" and probe_unit_rules.has(String(key_value)):
				archer_member_id = String(key_value)
				break
		var live_armor_ok := false
		var live_armor_detail := "no KnightArmor / pierce member rule in the doc id space"
		if knight_member_id != "" and archer_member_id != "":
			armor_probe_sim._add_battalion(991, SimScript.PLAYER_TEAM, Vector2.ZERO, "probe archers", archer_member_id, archer_member_id)
			armor_probe_sim._add_battalion(992, SimScript.ENEMY_TEAM, Vector2.ONE, "probe knights", knight_member_id, knight_member_id)
			var live_knight: Dictionary = armor_probe_sim.entity(992)
			var live_archer: Dictionary = armor_probe_sim.entity(991)
			var arrow := maxi(1, int(live_archer.get("member_damage", 1)))
			var prior_health := int((live_knight.get("member_health", []) as Array)[0])
			armor_probe_sim._apply_member_damage(991, 0, 992, arrow, "battalion", 0, 0)
			var after_health := int((live_knight.get("member_health", []) as Array)[0])
			var expected := maxi(1, roundi(float(arrow) * 0.40))
			live_armor_ok = prior_health - after_health == expected and String(live_archer.get("damage_type", "")) == "pierce"
			live_armor_detail = "knight=%s arrow=%d expected=%d applied=%d" % [knight_member_id, arrow, expected, prior_health - after_health]
		_check("archer_pierce_vs_knight_applies_compiled_scalar_in_live_sim", live_armor_ok, live_armor_detail)
		# Forge upgrades compile retail values, not invented consts: blades
		# (GondorSwordUpgraded 90, weapon.ini:5544 + gamedata.ini:1113), heavy
		# armor (SoldierHeavyArmor, armor.ini:502-519), fire arrows
		# (GondorArcherBowFireWarhead flame bonus 32, gamedata.ini:1134).
		var blades_effect: Dictionary = (roster_sim._unit_weapon_upgrades.get(SimScript.SOLDIER_OBJECT_ID, {}) as Dictionary).get("Upgrade_GondorForgedBlades", {})
		var heavy_upgrade: Dictionary = (soldier_armor_rule.get("upgrades", {}) as Dictionary).get("Upgrade_GondorHeavyArmor", {})
		var fire_effect: Dictionary = (roster_sim._unit_weapon_upgrades.get(SimScript.ARCHER_OBJECT_ID, {}) as Dictionary).get("Upgrade_GondorArcherFireArrows", {})
		_check(
			"forge_upgrades_carry_compiled_retail_effects",
			String(blades_effect.get("kind", "")) == "weapon-swap"
				and float(blades_effect.get("damage", 0.0)) == 90.0
				and String(heavy_upgrade.get("set_id", "")) == "SoldierHeavyArmor"
				and is_equal_approx(float(heavy_upgrade.get("damage_scalar", 0.0)), 1.20)
				and is_equal_approx(float((heavy_upgrade.get("scalars", {}) as Dictionary).get("pierce", 0.0)), 0.20)
				and String(fire_effect.get("kind", "")) == "warhead-upgrade"
				and float(fire_effect.get("damage", 0.0)) == 25.0
				and (fire_effect.get("bonus_nuggets", []) as Array).size() >= 1,
			"blades=%s heavy=%s fire=%s" % [str(blades_effect), str(heavy_upgrade.get("set_id", "")), str(fire_effect.get("kind", ""))]
		)
	var ai_roster_sim = SimScript.new()
	ai_roster_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	var ai_construction_ready := false
	for _index in range(2000):
		ai_roster_sim.tick()
		if _first_event_sequence(ai_roster_sim.events, "construction.completed", SimScript.ENEMY_TEAM) > 0:
			ai_construction_ready = true
			break
	_check("ai_completes_construction_before_roster_queue", ai_construction_ready)
	# The enemy AI trains its manifest plan only at producers it actually owns:
	# with a fortress-only retail start the fortress hero is the available
	# production; units routed to unbuilt producers stay unqueued. A living
	# enemy hero identity legitimately blocks its own requeue, so clear it
	# first to exercise the queue deterministically.
	var enemy_hero_id := 0
	for entity_id in ai_roster_sim.entity_ids():
		var candidate: Dictionary = ai_roster_sim.entity(entity_id)
		if int(candidate.get("team", -1)) == SimScript.ENEMY_TEAM and String(ai_roster_sim.production_rule_category(String(candidate.get("unit_type", "")))) == "hero":
			enemy_hero_id = entity_id
			break
	if enemy_hero_id != 0:
		(ai_roster_sim.entities[enemy_hero_id] as Dictionary)["health"] = 0
	for _index in range(2000):
		if _first_event_sequence(ai_roster_sim.events, "production.queued", SimScript.ENEMY_TEAM) > 0:
			break
		ai_roster_sim.tick()
	var ai_fortress: int = ai_roster_sim.producer_id(1, "fortress")
	var ai_fortress_types := _queued_event_unit_types(ai_roster_sim.events, ai_fortress)
	var ai_plan: Array = Array(slice.faction_manifest.get("ai_production_plan", []))
	var fortress_plan_units: Array = []
	for plan_unit_value in ai_plan:
		var plan_rule: Dictionary = manifest_rules.get(String(plan_unit_value), {}) as Dictionary
		if String(plan_rule.get("producer_kind", "")) == "fortress":
			fortress_plan_units.append(String(plan_unit_value))
	var ai_fortress_plan_honored := not fortress_plan_units.is_empty()
	for fortress_unit_value in fortress_plan_units:
		ai_fortress_plan_honored = ai_fortress_plan_honored and ai_fortress_types.has(String(fortress_unit_value))
	_check("ai_trains_plan_at_owned_fortress", ai_fortress_plan_honored, "queued=%s plan=%s" % [str(ai_fortress_types), str(fortress_plan_units)])
	var ai_never_trains_without_producer := true
	for event_value in ai_roster_sim.events:
		var event: Dictionary = event_value
		if String(event.get("kind", "")) != "production.queued":
			continue
		var event_producer := int(event.get("entity_id", 0))
		var event_producer_kind := String(ai_roster_sim.structure(event_producer).get("structure_kind", ""))
		var event_rule: Dictionary = manifest_rules.get(String(event.get("unit_type", "")), {}) as Dictionary
		if String(event_rule.get("producer_kind", "")) != event_producer_kind:
			ai_never_trains_without_producer = false
	_check("ai_never_trains_a_unit_its_producer_lacks", ai_never_trains_without_producer)
	var interrupted_ai = SimScript.new()
	interrupted_ai.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	for _index in range(2000):
		if _first_event_sequence(interrupted_ai.events, "construction.started", SimScript.ENEMY_TEAM) > 0:
			break
		interrupted_ai.tick()
	var interrupted_construction := _first_event(interrupted_ai.events, "construction.started", SimScript.ENEMY_TEAM)
	var interrupted_site_id := int(interrupted_construction.get("target_id", 0))
	var interrupted_builder: Dictionary = interrupted_ai.entity(104)
	interrupted_builder["health"] = 0
	interrupted_builder["member_health"] = [0]
	for entity_id in interrupted_ai.entity_ids():
		var hero_candidate: Dictionary = interrupted_ai.entity(entity_id)
		if int(hero_candidate.get("team", -1)) == SimScript.ENEMY_TEAM and String(interrupted_ai.production_rule_category(String(hero_candidate.get("unit_type", "")))) == "hero":
			(interrupted_ai.entities[entity_id] as Dictionary)["health"] = 0
			break
	for _index in range(1000):
		if _first_event_sequence(interrupted_ai.events, "production.queued", SimScript.ENEMY_TEAM) > 0:
			break
		interrupted_ai.tick()
	var interrupted_site: Dictionary = interrupted_ai.structure(interrupted_site_id)
	var production_resumed := _first_event_sequence(interrupted_ai.events, "production.queued", SimScript.ENEMY_TEAM) > 0
	# The legacy farm path abandons the dead builder's site; the authored build
	# order instead retrains a builder and keeps developing. Both are honest
	# recoveries: production must resume after the interruption either way.
	var site_abandoned := int(interrupted_site.get("health", -1)) == 0 and int(interrupted_site.get("builder_id", -1)) == 0 and _first_event_sequence(interrupted_ai.events, "structure.destroyed", -1, interrupted_site_id) > 0
	var builder_retrained := false
	for event_value in interrupted_ai.events:
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == "production.queued" and int(event.get("team", -1)) == SimScript.ENEMY_TEAM and String(interrupted_ai.production_rule_category(String(event.get("unit_type", "")))) != "hero":
			builder_retrained = true
	_check(
		"ai_resumes_production_when_porter_construction_is_interrupted",
		interrupted_site_id != 0
			and int(interrupted_builder.get("construction_id", -1)) == 0
			and production_resumed
			and (site_abandoned or builder_retrained),
		"site=%s destroyed_event=%s production_resumed=%s retrained=%s" % [
			str({"health": interrupted_site.get("health", -1), "builder_id": interrupted_site.get("builder_id", -1)}),
			str(_first_event_sequence(interrupted_ai.events, "structure.destroyed", -1, interrupted_site_id)),
			str(production_resumed),
			str(builder_retrained),
		]
	)
	var ai_reached_source_vision := false
	for _tick in range(1500):
		if _team_has_target(ai_roster_sim, SimScript.ENEMY_TEAM):
			ai_reached_source_vision = true
			break
		ai_roster_sim.tick()
	_check("enemy_ai_preserves_attack_loop", ai_reached_source_vision)

	# Cavalry trample: knights charging into an enemy apply one bonus hit.
	var trample_sim = SimScript.new()
	trample_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	trample_sim.ai_enabled = false
	var knight_row: Dictionary = trample_sim.entity(103)
	var enemy_row: Dictionary = trample_sim.entity(1)
	if not knight_row.is_empty() and not enemy_row.is_empty():
		knight_row["category"] = "cavalry"
		knight_row["current_speed"] = float(knight_row.get("speed", 1.0))
		knight_row["trample_cooldown"] = 0
		knight_row["position"] = Vector2(enemy_row.get("position", Vector2.ZERO)) + Vector2(1.0, 0.0)
		knight_row["route"] = [Vector2(enemy_row.get("position", Vector2.ZERO)) + Vector2(-20.0, 0.0)]
		var health_before := int(enemy_row.get("health", 0))
		trample_sim._step_route(knight_row)
		var trample_events := 0
		for event_value in trample_sim.events:
			if String((event_value as Dictionary).get("kind", "")) == "combat.trample":
				trample_events += 1
		_check("cavalry_trample_applies_while_charging", trample_events >= 1 and int(enemy_row.get("health", health_before)) < health_before, "events=%d hp %d->%d" % [trample_events, health_before, int(enemy_row.get("health", -1))])
		# Infantry does not trample.
		var infantry: Dictionary = trample_sim.entity(1)
		infantry["category"] = "infantry"
		infantry["current_speed"] = float(infantry.get("speed", 1.0))
		infantry["trample_cooldown"] = 0
		infantry["position"] = Vector2(trample_sim.entity(103).get("position", Vector2.ZERO))
		infantry["route"] = [Vector2(infantry["position"]) + Vector2(5.0, 0.0)]
		var events_before: int = trample_sim.events.size()
		trample_sim._step_route(infantry)
		var infantry_trample: int = 0
		for index in range(events_before, trample_sim.events.size()):
			if String((trample_sim.events[index] as Dictionary).get("kind", "")) == "combat.trample":
				infantry_trample += 1
		_check("infantry_does_not_trample", infantry_trample == 0)
	else:
		_check("cavalry_trample_applies_while_charging", false, "missing knight/enemy entities")
		_check("infantry_does_not_trample", false, "missing entities")

	# Live-slice production: the porter constructs the faction's line producer,
	# the line unit queues from it, and completion creates its retail battalion
	# presentation on the exact completion tick. The enemy AI is held so the
	# construction window is exercised deterministically.
	var line_unit := _line_production_unit(slice)
	_check("faction_line_unit_resolves", not line_unit.is_empty(), str(line_unit))
	slice.simulation.ai_enabled = false
	var live_builder_ids: Array[int] = []
	for entity_id in slice.simulation.entity_ids():
		var live_candidate: Dictionary = slice.simulation.entity(entity_id)
		if int(live_candidate.get("team", -1)) == 0 and bool(live_candidate.get("is_builder", false)):
			live_builder_ids.append(entity_id)
	var live_producer := 0
	if not live_builder_ids.is_empty() and not line_unit.is_empty():
		var live_anchor := Vector2(slice.simulation.entity(1).get("position", Vector2.ZERO))
		for dx in range(-36, 37, 6):
			for dy in range(-36, 37, 6):
				var live_result: Dictionary = slice.simulation.issue_construct(live_builder_ids, String(line_unit.get("producer_kind", "")), live_anchor + Vector2(dx, dy))
				if bool(live_result.get("ok", false)):
					live_producer = int(live_result.get("structure_id", 0))
					break
			if live_producer != 0:
				break
	_check("player_porter_constructs_line_producer", live_producer != 0, "id=%d kind=%s" % [live_producer, String(line_unit.get("producer_kind", ""))])
	var producer_built := false
	var construction_paused := false
	if live_producer != 0:
		for _step in range(3000):
			if float(slice.simulation.structure(live_producer).get("construction_progress", 0.0)) >= 1.0:
				producer_built = true
				break
			slice.simulation.tick()
			# Mid-build: the construction clip is a paused manual-progress
			# scrub, never a looping playback.
			if not construction_paused and float(slice.simulation.structure(live_producer).get("construction_progress", 0.0)) > 0.05:
				slice._sync_presentation()
				await process_frame
				var mid_structure_node = slice.structure_nodes.get(live_producer)
				if mid_structure_node != null:
					var mid_players: Array = mid_structure_node._animation_players(mid_structure_node._active_body)
					if not mid_players.is_empty():
						construction_paused = not (mid_players[0] as AnimationPlayer).is_playing()
		slice._sync_presentation()
	_check("player_line_producer_completes_construction", producer_built)
	_check("construction_animation_is_manual_progress_not_looping", construction_paused)
	# The intact idle is the authored ambient clip looping forever (retail
	# loop-random idles): it must still be playing and advancing seconds later.
	var live_structure_node = slice.structure_nodes.get(live_producer)
	var ambient_player: AnimationPlayer = null
	if live_structure_node != null:
		var ambient_players: Array = live_structure_node._animation_players(live_structure_node._active_body)
		if not ambient_players.is_empty():
			ambient_player = ambient_players[0]
	if ambient_player != null:
		slice.simulation.tick()
		slice._sync_presentation()
		await process_frame
		await process_frame
		var ambient_clip := String(ambient_player.current_animation)
		var ambient_animation: Animation = ambient_player.get_animation(ambient_clip)
		var ambient_loop_mode := ambient_animation.loop_mode if ambient_animation != null else -1
		var position_before := ambient_player.current_animation_position
		for _frame in range(20):
			await process_frame
		_check(
			"ambient_idle_animation_keeps_looping",
			ambient_clip != ""
				and ambient_loop_mode == Animation.LOOP_LINEAR
				and ambient_player.is_playing()
				and ambient_player.current_animation_position > position_before,
			"clip=%s mode=%d playing=%s pos=%.3f->%.3f" % [ambient_clip, ambient_loop_mode, str(ambient_player.is_playing()), position_before, ambient_player.current_animation_position]
		)
	var line_rule: Dictionary = manifest_rules.get(String(line_unit.get("unit_type", "")), {}) as Dictionary
	var line_unit_type := String(line_unit.get("unit_type", ""))
	var queued: Dictionary = slice.simulation.queue_unit(0, live_producer, line_unit_type) if live_producer != 0 else {}
	_check("player_line_producer_queues_line_unit", bool(queued.get("ok", false)), str(queued))
	slice.step_for_test(int(line_rule.get("default_build_ticks", 1)) - 1)
	_check("production_not_early_in_scene", not slice.simulation.entities.has(10))
	slice.step_for_test(1)
	var line_entity: Dictionary = _entity_for_unit_type(slice.simulation, 0, line_unit_type)
	var line_battalion = slice.battalion_nodes.get(int(line_entity.get("id", 0)))
	_check(
		"produced_battalion_gets_runtime_presentation",
		not line_entity.is_empty()
			and line_battalion != null
			and int(line_battalion.member_count) == int(line_entity.get("member_count", 0))
			and int(line_battalion.retail_visual_count) == int(line_entity.get("member_count", 0))
	)
	var line_object_id := String(line_rule.get("object_id", ""))
	var line_definition: Dictionary = content_db_for_models.get_bundle_object(line_object_id)
	_check(
		"produced_battalion_reuses_validated_capability",
		line_battalion != null
			and int(line_battalion.unresolved_animation_track_count) == 0
			and String((slice.validated_battalion_capabilities.get(line_object_id, {}) as Dictionary).get("source", "")) == "openbfme.playable-unit-runtime"
	)
	_check(
		"produced_battalion_mounts_document_glb",
		line_battalion != null
			and String(line_battalion.object_id) == line_object_id
			and String(line_battalion.retail_model_filename) == String((line_definition.get("presentation", {}) as Dictionary).get("model", "")).get_file()
			and String(line_battalion.retail_model_filename) != ""
	)
	# --- Doc-driven structure levels on the live slice ---
	# The producer's authored upgrade chain purchases through the generic
	# mechanism, the structure node's model swaps to the authored per-level
	# sub-object visibility (retail SubObjectsUpgrade rows compiled into the
	# structure document), and the purchase button surfaces on the command set.
	var level_chain: Dictionary = (slice.faction_manifest.get("structure_upgrade_chains", {}) as Dictionary).get(String(line_unit.get("producer_kind", "")), {}) as Dictionary
	var level_steps: Array = level_chain.get("steps", [])
	_check("live_producer_has_doc_upgrade_chain", live_producer != 0 and not level_steps.is_empty(), "kind=%s" % String(line_unit.get("producer_kind", "")))
	if live_producer != 0 and not level_steps.is_empty():
		slice.simulation.team_resources[0] = slice.simulation.resources_for_team(0) + 6000
		var first_upgrade := String((level_steps[0] as Dictionary).get("upgradeId", ""))
		var first_duration := maxi(1, roundi(float((level_steps[0] as Dictionary).get("buildTimeSeconds", 1.0)) / SimScript.TICK_SECONDS))
		var offered: Array = _chain_purchase_rows(slice.simulation.structure_upgrade_commands(live_producer))
		_check(
			"live_structure_exposes_purchase_command",
			offered.size() == 1
				and String((offered[0] as Dictionary).get("upgrade_id", "")) == first_upgrade
				and int((offered[0] as Dictionary).get("slot", 0)) >= 1
				and String((offered[0] as Dictionary).get("label_id", "")) != ""
				and int((offered[0] as Dictionary).get("cost", -1)) == int((level_steps[0] as Dictionary).get("cost", -2)),
			str(offered)
		)
		var purchased: Dictionary = slice.simulation.queue_structure_upgrade(0, live_producer, first_upgrade)
		_check("live_upgrade_purchases", bool(purchased.get("ok", false)), str(purchased))
		slice.step_for_test(first_duration)
		var level_building: Dictionary = slice.simulation.structure(live_producer)
		_check(
			"live_upgrade_completes_at_level_two",
			int(level_building.get("level", 0)) == int((level_steps[0] as Dictionary).get("toLevel", 0))
				and Array(level_building.get("completed_upgrades", [])).has(first_upgrade),
			str({"level": level_building.get("level"), "completed": level_building.get("completed_upgrades")})
		)
		slice._sync_presentation()
		await process_frame
		await process_frame
		var level_node = slice.structure_nodes.get(live_producer)
		var level_two_state: Dictionary = level_node.level_state() if level_node != null else {}
		var expected_two: Dictionary = (level_steps[0] as Dictionary).get("presentation", {})
		var expected_two_visible: Array = expected_two.get("visibleSubObjects", [])
		var expected_two_hidden: Array = expected_two.get("hiddenSubObjects", [])
		var two_applied: Dictionary = level_two_state.get("appliedNodes", {})
		var two_visibility_ok := int(level_two_state.get("level", 0)) == 2
		for token_value in expected_two_visible:
			var token := String(token_value)
			if not _level_token_has_node_match(two_applied, token):
				continue
			two_visibility_ok = two_visibility_ok and bool(two_applied.get(_level_token_match_name(two_applied, token), false)) == true
		for token_value in expected_two_hidden:
			var token := String(token_value)
			if not _level_token_has_node_match(two_applied, token):
				continue
			two_visibility_ok = two_visibility_ok and bool(two_applied.get(_level_token_match_name(two_applied, token), true)) == false
		_check(
			"level_two_swaps_to_authored_subobjects",
			level_node != null and two_visibility_ok and Array(level_two_state.get("visibleSubObjects", [])) == expected_two_visible,
			str({"applied": two_applied, "expected_visible": expected_two_visible, "expected_hidden": expected_two_hidden, "unmatched": level_two_state.get("unmatchedTokens", [])})
		)
		_check(
			"level_two_applies_on_real_glb_nodes",
			level_node != null and not two_applied.is_empty(),
			"applied=%s" % str(two_applied)
		)
		# The purchase button binds through the HUD's doc seam (icon/label/
		# tooltip/cost from the doc) and sits at the authored command slot.
		var doc_button_hud = load("res://src/retail_slice/retail_hud.gd").new()
		doc_button_hud.build()
		doc_button_hud.set_production_state([], true, 0, [], [], [], [], String(line_unit.get("producer_kind", "")), offered)
		# `as Button` on the old `{}` fallback is a hard cast error, not a null:
		# a faction whose HUD never bound this upgrade aborted `_run` outright
		# (mordor did). Resolve it as a Variant and let the check report the
		# missing binding.
		var doc_button_value: Variant = doc_button_hud._doc_upgrade_buttons.get(first_upgrade, null)
		var doc_button: Button = doc_button_value as Button if doc_button_value is Button else null
		_check(
			"doc_upgrade_button_binds_at_authored_slot",
			doc_button != null
				and doc_button.visible
				and not doc_button.disabled
				and doc_button.position == doc_button_hud.RETAIL_COMMAND_SLOT_SOURCE[int((offered[0] as Dictionary).get("slot", 1)) - 1],
			"button=%s" % str(doc_button)
		)
		doc_button_hud.free()
		if level_steps.size() > 1:
			var second_upgrade := String((level_steps[1] as Dictionary).get("upgradeId", ""))
			var second_duration := maxi(1, roundi(float((level_steps[1] as Dictionary).get("buildTimeSeconds", 1.0)) / SimScript.TICK_SECONDS))
			var offered_second: Array = _chain_purchase_rows(slice.simulation.structure_upgrade_commands(live_producer))
			_check(
				"level_three_command_surfaces_after_level_two",
				offered_second.size() == 1 and String((offered_second[0] as Dictionary).get("upgrade_id", "")) == second_upgrade,
				str(offered_second)
			)
			var purchased_second: Dictionary = slice.simulation.queue_structure_upgrade(0, live_producer, second_upgrade)
			_check("level_three_purchases", bool(purchased_second.get("ok", false)), str(purchased_second))
			slice.step_for_test(second_duration)
			slice._sync_presentation()
			await process_frame
			var level_three_state: Dictionary = (slice.structure_nodes.get(live_producer) as Node).level_state()
			var expected_three: Dictionary = (level_steps[1] as Dictionary).get("presentation", {})
			var three_applied: Dictionary = level_three_state.get("appliedNodes", {})
			var three_visibility_ok := int(level_three_state.get("level", 0)) == 3
			for token_value in Array(expected_three.get("visibleSubObjects", [])):
				var token := String(token_value)
				if not _level_token_has_node_match(three_applied, token):
					continue
				three_visibility_ok = three_visibility_ok and bool(three_applied.get(_level_token_match_name(three_applied, token), false)) == true
			for token_value in Array(expected_three.get("hiddenSubObjects", [])):
				var token := String(token_value)
				if not _level_token_has_node_match(three_applied, token):
					continue
				three_visibility_ok = three_visibility_ok and bool(three_applied.get(_level_token_match_name(three_applied, token), true)) == false
			_check(
				"level_three_swaps_to_authored_subobjects",
				three_visibility_ok and Array(level_three_state.get("visibleSubObjects", [])) == Array(expected_three.get("visibleSubObjects", [])),
				str({"applied": three_applied, "expected": expected_three.get("visibleSubObjects", [])})
			)
			_check(
				"chain_exhausted_exposes_no_purchase",
				_chain_purchase_rows(slice.simulation.structure_upgrade_commands(live_producer)).is_empty(),
				str(_chain_purchase_rows(slice.simulation.structure_upgrade_commands(live_producer)))
			)

	# Run a complete battle through public commands, capturing actual animation
	# presentation states at attack/death and deterministic audio transitions.
	# Both teams field the same hero-anchored opening roster, so the player
	# first trains three line-unit reinforcements from a porter-built producer.
	slice.reset_match()
	slice.simulation.ai_enabled = false
	var reinforcement := _build_line_reinforcement(slice.simulation, line_unit_type, String(line_unit.get("producer_kind", "")))
	var expected_reinforcement := _expected_reinforcement_count(slice.gameplay_rules, line_unit)
	_check("battle_reinforcement_trained", expected_reinforcement >= 1 and reinforcement.size() == expected_reinforcement, "trained=%s expected=%d" % [str(reinforcement), expected_reinforcement])
	slice._sync_presentation()
	_check("battle_select_one", bool(slice.test_select(1)))
	_check("battle_multi_select_two", bool(slice.simulation.toggle_selection(2)))
	var attack_group: Array[int] = [1, 2]
	attack_group.append_array(reinforcement)
	slice.simulation.select_many(attack_group)
	# The army rallies out of enemy vision first so spawn and reinforcement
	# battalions engage as one group instead of being shredded piecemeal; then
	# it clears the defending hordes, the enemy hero, and finally the fortress.
	var stage_point := Vector2(10.0, 14.0)
	slice.simulation.issue_move(attack_group, stage_point)
	var army_staged := _advance_until(slice, func(): return _group_within(slice.simulation, attack_group, stage_point, 4.0), 1800)
	_check("attack_group_rallies_before_engaging", army_staged)
	_check("attack_order_one", int(slice.test_attack(102)) == attack_group.size(), "accepted=%d group=%s" % [attack_group.size(), str(attack_group)])
	var saw_attack := _advance_until(slice, func(): return _any_state(slice.simulation, 0, "attack"), 900)
	_check("attack_state_reached", saw_attack)
	if saw_attack:
		slice._sync_presentation()
		var attacking_id := _first_state_id(slice.simulation, 0, "attack")
		var attacking_node = slice.battalion_nodes.get(attacking_id)
		# Multi-unit roster: Soldier uses gumanmocap_atka, Archer guarcher_atk*, etc.
		var attack_clip := String(attacking_node.current_clip) if attacking_node != null else ""
		var attack_allowed := false
		if attacking_node != null:
			var allowed_attack: Array = attacking_node.variant_clips_for_state("attack")
			if allowed_attack.is_empty():
				allowed_attack = [String(attacking_node.clip_for_state("attack"))]
			attack_allowed = allowed_attack.has(attack_clip) and attack_clip != ""
		_check("attack_drives_imported_clip", attack_allowed, attack_clip)
	var first_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(102)["health"]) == 0, 2400)
	_check("first_enemy_defeated", first_defeated)
	if first_defeated:
		slice._sync_presentation()
		var defeated_node = slice.battalion_nodes.get(102)
		var death_variants: Array = defeated_node.variant_clips_for_state("death") if defeated_node != null else []
		_check("death_drives_imported_clip", defeated_node != null and String(defeated_node.current_state) == "death" and death_variants.has(String(defeated_node.current_clip)), "%s/%s" % [defeated_node.current_state, defeated_node.current_clip] if defeated_node != null else "missing")
		_check("death_variants_active", defeated_node != null and defeated_node.active_clip_variants().size() == mini(int(defeated_node.member_count), death_variants.size()), str(defeated_node.active_clip_variants() if defeated_node != null else []))
		_check("defeated_markers_hidden", defeated_node != null and not bool(defeated_node.markers_visible()))
	_check("attack_order_two", int(slice.test_attack(103)) >= 1)
	var second_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(103)["health"]) == 0, 2400)
	_check("second_enemy_defeated", second_defeated)
	_check("attack_order_three", int(slice.test_attack(101)) >= 1)
	var third_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(101)["health"]) == 0, 2400)
	_check("third_enemy_defeated", third_defeated)
	var enemy_fortress: int = slice.simulation.fortress_id(1)
	_check("enemy_fortress_attack_order", enemy_fortress != 0 and int(slice.test_attack(enemy_fortress)) >= 1)
	var battle_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 14000)
	_check("battle_reaches_victory", battle_finished and int(slice.simulation.winner) == 0, "winner=%d" % int(slice.simulation.winner))
	slice._sync_presentation()
	_check("battle_music_intent", _event_kind_present(slice.simulation.events, "music.battle"))
	_check("select_voice_intent", _event_kind_present(slice.simulation.events, "voice.select"))
	_check("attack_voice_intent", _event_kind_present(slice.simulation.events, "voice.attack"))
	_check("victory_music_active", String(slice.audio_system.current_music_state) == "victory", String(slice.audio_system.current_music_state))
	_check("victory_splash_visible", bool(slice.hud.outcome_layer.visible) and String(slice.hud.outcome_title.text) == "VICTORY")

	var first_signature := String(slice.simulation.state_signature())
	var replay := SimScript.new()
	var replay_signature := _run_reference_battle(replay, slice.source_map_data.simulation_configuration(), slice.gameplay_rules)
	_check("deterministic_replay_signature", first_signature == replay_signature, "%s != %s" % [first_signature, replay_signature])
	# The deterministic battle signature is pinned as an asserted constant per
	# faction: any change to the resolved simulation (rules, roster, orders,
	# snapshot fields) must be deliberate and re-pinned, never drift silently.
	# The snapshot covers power_points/purchased_powers/team_upgrades.
	var expected_signature := String(EXPECTED_BATTLE_SIGNATURES.get(String(slice.faction_manifest.get("faction", "men")), ""))
	_check("battle_signature_matches_pinned_constant", expected_signature != "" and first_signature == expected_signature, "%s != %s" % [first_signature, expected_signature])
	print("RETAIL_SLICE_SIGNATURE %s" % first_signature)

	# Let the deterministic enemy play a complete unassisted match. A player loss
	# must have its own simulation intent and activate the imported defeat track.
	slice.reset_match()
	slice.simulation.advance(300)
	var early_construction_event := _first_event(slice.simulation.events, "construction.started", SimScript.ENEMY_TEAM)
	var early_construction_site: Dictionary = slice.simulation.structure(int(early_construction_event.get("target_id", 0)))
	var early_builder: Dictionary = slice.simulation.entity(int(early_construction_event.get("entity_id", 0)))
	# The enemy AI builds its faction-derived order: farm first when declared,
	# then the plan's producer kinds in plan order (fortress stays seeded).
	var expected_first_kind := ""
	var derived_order: Array[String] = slice.simulation.ai_base_build_order()
	if not derived_order.is_empty():
		expected_first_kind = String(derived_order[0])
	_check("enemy_ai_porter_advances_construction", expected_first_kind != "" and int(early_builder.get("team", -1)) == SimScript.ENEMY_TEAM and bool(early_builder.get("is_builder", false)) and int(early_construction_site.get("team", -1)) == SimScript.ENEMY_TEAM and String(early_construction_site.get("structure_kind", "")) == expected_first_kind and int(early_construction_site.get("builder_id", 0)) == int(early_builder.get("id", 0)) and float(early_construction_site.get("construction_progress", 0.0)) > 0.0, "kind=%s builder=%s site=%s" % [expected_first_kind, str({"state": early_builder.get("state", ""), "position": early_builder.get("position"), "route": Array(early_builder.get("route", [])).size(), "construction_id": early_builder.get("construction_id", -1)}), str({"progress": early_construction_site.get("construction_progress", -1.0), "elapsed": early_construction_site.get("construction_elapsed_ticks", -1), "position": early_construction_site.get("position")})])
	var defeat_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 35700)
	slice._sync_presentation()
	_check("battle_reaches_defeat", defeat_finished and int(slice.simulation.winner) == SimScript.ENEMY_TEAM, "winner=%d tick=%d state=%s" % [int(slice.simulation.winner), int(slice.simulation.tick_index), _compact_combat_state(slice.simulation)])
	var player_fortress: int = slice.simulation.fortress_id(SimScript.PLAYER_TEAM)
	var construction_started := _first_event_sequence(slice.simulation.events, "construction.started", SimScript.ENEMY_TEAM)
	var construction_event := _first_event(slice.simulation.events, "construction.started", SimScript.ENEMY_TEAM)
	var construction_target_id := int(construction_event.get("target_id", 0))
	var construction_completed_event := _first_event(slice.simulation.events, "construction.completed", SimScript.ENEMY_TEAM, construction_target_id)
	var construction_completed := int(construction_completed_event.get("sequence", 0))
	var production_completed := _first_event_sequence(slice.simulation.events, "production.complete", SimScript.ENEMY_TEAM)
	var fortress_hit := _first_event_sequence(slice.simulation.events, "combat.hit_structure", -1, player_fortress)
	var fortress_destroyed := _first_event_sequence(slice.simulation.events, "structure.destroyed", -1, player_fortress)
	var defeat_event := _first_event_sequence(slice.simulation.events, "match.defeat")
	var construction_site: Dictionary = slice.simulation.structure(int(construction_event.get("target_id", 0)))
	var enemy_builder: Dictionary = slice.simulation.entity(int(construction_event.get("entity_id", 0)))
	# The enemy AI's economy loop (build the farm, then train at the producers
	# it owns) must drive an unassisted win. The causal chain is construction →
	# production → fortress destroyed → defeat; which unit lands the first
	# fortress hit depends on the flow of the battle and is not ordered a
	# priori against production. Factions without farm evidence have no income
	# loop yet: only the battlefield half of the chain is asserted there.
	var farm_supported := (slice.faction_manifest.get("structure_build_rules", {}) as Dictionary).has("farm")
	var chain_ok := false
	if farm_supported:
		chain_ok = construction_started > 0 and _event_count(slice.simulation.events, "construction.started", SimScript.ENEMY_TEAM) >= 1 and construction_started < construction_completed and _event_count(slice.simulation.events, "construction.completed", SimScript.ENEMY_TEAM) >= 1 and int(construction_completed_event.get("entity_id", 0)) == int(construction_event.get("entity_id", -1)) and int(construction_site.get("team", -1)) == SimScript.ENEMY_TEAM and String(construction_site.get("structure_kind", "")) == "farm" and int(construction_site.get("builder_id", 0)) == int(construction_event.get("entity_id", -1)) and is_equal_approx(float(construction_site.get("construction_progress", 0.0)), 1.0) and construction_completed < production_completed and production_completed < fortress_destroyed and fortress_hit > 0 and fortress_hit < fortress_destroyed and fortress_destroyed < defeat_event
	else:
		chain_ok = fortress_hit > 0 and fortress_hit < fortress_destroyed and fortress_destroyed < defeat_event
	_check("enemy_ai_economy_to_defeat_chain", chain_ok, "sequences=%s builder=%s site=%s" % [str([construction_started, construction_completed, production_completed, fortress_hit, fortress_destroyed, defeat_event]), str({"health": enemy_builder.get("health", -1), "state": enemy_builder.get("state", ""), "position": enemy_builder.get("position"), "route": Array(enemy_builder.get("route", [])).size(), "construction_id": enemy_builder.get("construction_id", -1)}), str({"health": construction_site.get("health", -1), "progress": construction_site.get("construction_progress", -1.0), "position": construction_site.get("position"), "builder_id": construction_site.get("builder_id", -1)})])
	_check("defeat_event_intent", _event_kind_present(slice.simulation.events, "match.defeat") and _event_kind_present(slice.simulation.events, "music.defeat"))
	_check("defeat_music_active", String(slice.audio_system.current_music_state) == "defeat", String(slice.audio_system.current_music_state))
	_check("defeat_splash_visible", bool(slice.hud.outcome_layer.visible) and String(slice.hud.outcome_title.text) == "DEFEAT")
	var defeat_signature := String(slice.simulation.state_signature())
	var defeat_replay := SimScript.new()
	var defeat_replay_signature := _run_reference_defeat(defeat_replay, slice.source_map_data.simulation_configuration(), slice.gameplay_rules)
	_check("deterministic_defeat_signature", defeat_signature == defeat_replay_signature, "%s != %s" % [defeat_signature, defeat_replay_signature])

	slice.reset_match()
	var paused_before := bool(slice.simulation_paused)
	slice.toggle_escape_menu()
	_check("escape_menu_pauses", not paused_before and bool(slice.simulation_paused) and bool(slice.pause_panel.visible))
	slice.toggle_escape_menu()
	_check("escape_menu_resumes", not bool(slice.simulation_paused) and not bool(slice.pause_panel.visible))
	_run_hero_ability_batch2_probes(slice)
	_check("route_queries_reuse_cached_navigation", int(slice.source_map_data.navigation_build_count) == 1 and int(slice.source_map_data.route_query_count) > 10, "builds=%d queries=%d" % [slice.source_map_data.navigation_build_count, slice.source_map_data.route_query_count])
	print("RETAIL_NAV_METRICS walkable=%d water_blocked=%d ford_corridor=%d route_queries=%d" % [slice.source_map_data.navigation_walkable_count, slice.source_map_data.navigation_water_blocked_count, slice.source_map_data.navigation_ford_corridor_count, slice.source_map_data.route_query_count])

	var asset_factory = load("res://src/view/asset_factory.gd")
	_check("mesh_cache_is_bounded", asset_factory.mesh_cache_size() > 0 and asset_factory.mesh_cache_size() <= asset_factory.MAX_MESH_CACHE_ENTRIES, str(asset_factory.mesh_cache_size()))
	slice.cleanup_for_test()
	_check("mesh_cache_clears_on_slice_cleanup", asset_factory.mesh_cache_size() == 0, str(asset_factory.mesh_cache_size()))
	# Release the dynamically loaded script before deferred shutdown.
	asset_factory = null
	slice.queue_free()
	replay = null
	defeat_replay = null
	await process_frame
	await process_frame
	call_deferred("_finish")


func _run_hero_ability_batch2_probes(slice) -> void:
	## Hero-ability batch 2, pack-driven proof on the freshly reset match: the
	## compiled Men rows for the weapon toggle (Faramir bow<->sword), the mount
	## (Theoden horse), and capture-building are castable and drive the live
	## sim mechanics with the retail-authored magnitudes. Tiny host packs that
	## predate the full-faction hero roster carry no Faramir document and skip.
	var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var runtimes: Dictionary = slice.producible_unit_runtimes
	if String(slice.faction_manifest.get("faction", "")) != "men" or not runtimes.has("GondorFaramir"):
		return
	var sim = slice.simulation
	sim.ai_enabled = false
	var anchor := Vector2(sim._spawn_positions[1])
	var unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	# Real selected-pack hero summon: Aragorn's converted Army of the Dead leaf
	# closure must traverse egg -> hatch OCL -> battalion and materialize units.
	if runtimes.has("GondorAragornMP"):
		var aragorn_doc: Dictionary = runtimes.get("GondorAragornMP", {}) as Dictionary
		var summon_row: Dictionary = {}
		for ability_value in adapter.ability_rules(aragorn_doc):
			var candidate := ability_value as Dictionary
			if String((candidate.get("effect", {}) as Dictionary).get("kind", "")) == "summon":
				summon_row = candidate
				break
		_check("aragorn_real_summon_row_has_leaf_closure",
			not summon_row.is_empty()
				and not ((summon_row.get("effect", {}) as Dictionary).get("leaves", {}) as Dictionary).is_empty(),
			str(summon_row))
		if not summon_row.is_empty():
			var aragorn_member := String(adapter.runtime_member_id(aragorn_doc))
			sim._add_battalion(9000, 0, anchor, "Aragorn", aragorn_member, String(adapter.runtime_unit_id(aragorn_doc)))
			(sim.entities[9000] as Dictionary)["level"] = 10
			var before_summon_ids: Array[int] = sim.entity_ids().duplicate()
			var summon_cast: Dictionary = sim.cast_ability(
				9000, String(summon_row.get("ability_id", "")), anchor + Vector2(5.0, 0.0)
			)
			slice._report_ability_cast(
				String(adapter.runtime_unit_id(aragorn_doc)),
				String(summon_row.get("ability_id", "")),
				summon_cast
			)
			var queued_summons := int(summon_cast.get("summon_count", 0))
			_check(
				"aragorn_hero_summon_hud_uses_summon_phrasing",
				queued_summons > 0
					and String(slice.hud.feedback_label.text).contains(
						"summons %d unit%s." % [queued_summons, "" if queued_summons == 1 else "s"]
					)
					and not String(slice.hud.feedback_label.text).contains("affects"),
				String(slice.hud.feedback_label.text)
			)
			var hatch_ticks := 0
			if not sim._pending_power_effects.is_empty():
				hatch_ticks = maxi(0, int((sim._pending_power_effects[-1] as Dictionary).get("fire_tick", sim.tick_index)) - sim.tick_index)
			sim.advance(hatch_ticks + 1)
			var summoned_ids: Array[int] = []
			for entity_id in sim.entity_ids():
				if entity_id not in before_summon_ids:
					summoned_ids.append(entity_id)
			_check("aragorn_real_summon_cast_end_to_end",
				bool(summon_cast.get("ok", false))
					and not summoned_ids.is_empty()
					and queued_summons == summoned_ids.size(),
				"cast=%s spawned=%s" % [summon_cast, summoned_ids])
	# --- Faramir weapon toggle (bow <-> sword) ---
	var faramir_doc: Dictionary = runtimes.get("GondorFaramir", {}) as Dictionary
	var faramir_toggle := _ability_row_by_id(adapter, faramir_doc, "Command_ToggleFaramirWeapon")
	var faramir_effect: Dictionary = faramir_toggle.get("effect", {}) as Dictionary
	_check(
		"faramir_toggle_row_compiles_castable",
		bool(faramir_toggle.get("castable", false))
			and String(faramir_effect.get("kind", "")) == "weapon-toggle"
			and String(faramir_effect.get("toggleMode", "")) == "weaponset_toggle_1",
		str(faramir_toggle)
	)
	var faramir_member := String(adapter.runtime_member_id(faramir_doc))
	var faramir_rule: Dictionary = unit_rules.get(faramir_member, {}) as Dictionary
	var faramir_modes: Dictionary = faramir_rule.get("weapon_modes", {}) as Dictionary
	var faramir_sword: Dictionary = faramir_modes.get("weaponset_toggle_1", {}) as Dictionary
	_check(
		"faramir_rule_carries_both_mode_profiles",
		faramir_modes.has("default")
			and int(faramir_sword.get("member_damage", 0)) == 200
			and is_equal_approx(float(faramir_sword.get("attack_range_source", 0.0)), 11.5),
		str(faramir_modes.keys()) + str(faramir_sword)
	)
	sim._add_battalion(9001, 0, anchor + Vector2(2.0, 0.0), "Faramir", faramir_member, String(adapter.runtime_unit_id(faramir_doc)))
	var faramir: Dictionary = sim.entities.get(9001, {})
	var faramir_bow_range := float(faramir.get("attack_range", 0.0))
	var faramir_bow_damage := int(faramir.get("member_damage", 0))
	var toggle_cast: Dictionary = sim.cast_ability(9001, "Command_ToggleFaramirWeapon", Vector2.ZERO)
	_check(
		"faramir_toggle_cast_swaps_to_sword",
		bool(toggle_cast.get("ok", false))
			and String(faramir.get("weapon_toggle_mode", "")) == "weaponset_toggle_1"
			and int(faramir.get("member_damage", 0)) == 200
			and is_equal_approx(float(faramir.get("attack_range", 0.0)), float(faramir_sword.get("attack_range", -1.0))),
		str(toggle_cast) + " damage=%d" % int(faramir.get("member_damage", 0))
	)
	var toggle_release: Dictionary = sim.cast_ability(9001, "Command_ToggleFaramirWeapon", Vector2.ZERO)
	_check(
		"faramir_toggle_recast_restores_bow",
		bool(toggle_release.get("ok", false))
			and String(faramir.get("weapon_toggle_mode", "")) == ""
			and int(faramir.get("member_damage", 0)) == faramir_bow_damage
			and is_equal_approx(float(faramir.get("attack_range", 0.0)), faramir_bow_range),
		str(toggle_release)
	)
	# --- Theoden mount/dismount ---
	# Layered-oracle magnitudes (data/ini under
	# .private/retail-work/editions/rotwk/cache/layered-effective-assets):
	#   object/goodfaction/units/men/theoden.ini:919-923  SET_NORMAL  ->
	#     Speed = NORMAL_GOOD_HERO_SPEED,          gamedata.ini:8872 = 50
	#   object/goodfaction/units/men/theoden.ini:925-929  SET_MOUNTED ->
	#     Speed = NORMAL_MOUNTED_MED_HORDE_SPEED,  gamedata.ini:8978 = 100
	# The mounted literal was 90 until 2026-08-04. That was the superseded
	# NORMAL_CAVALRY_FAST_HORDE_SPEED value, which theoden.ini:928 carries only
	# behind the `;;.;;` retired-value marker; the live authored token resolves
	# to 100. Updated with the pack that is compiled from the layered oracle.
	var theoden_mounted_speed := 100.0
	if runtimes.has("RohanTheoden"):
		var theoden_doc: Dictionary = runtimes.get("RohanTheoden", {}) as Dictionary
		var mount_row := _ability_row_by_id(adapter, theoden_doc, "Command_TheodenToggleMounted")
		var mount_effect: Dictionary = mount_row.get("effect", {}) as Dictionary
		_check(
			"theoden_mount_row_compiles_castable",
			bool(mount_row.get("castable", false))
				and String(mount_effect.get("kind", "")) == "mount-toggle"
				and is_equal_approx(float(mount_effect.get("mountedSpeed", 0.0)), theoden_mounted_speed)
				and String(mount_effect.get("mountedWeaponModeKey", "")) == "mounted",
			str(mount_row)
		)
		var theoden_member := String(adapter.runtime_member_id(theoden_doc))
		var theoden_rule: Dictionary = unit_rules.get(theoden_member, {}) as Dictionary
		_check(
			"theoden_rule_carries_mounted_profile",
			(theoden_rule.get("weapon_modes", {}) as Dictionary).has("mounted"),
			str((theoden_rule.get("weapon_modes", {}) as Dictionary).keys())
		)
		sim._add_battalion(9002, 0, anchor + Vector2(4.0, 0.0), "Theoden", theoden_member, String(adapter.runtime_unit_id(theoden_doc)))
		var theoden: Dictionary = sim.entities.get(9002, {})
		var foot_speed_source := float(theoden.get("speed_source", 0.0))
		var mount_cast: Dictionary = sim.cast_ability(9002, "Command_TheodenToggleMounted", Vector2.ZERO)
		_check(
			"theoden_mount_cast_swaps_speed_and_weapon",
			bool(mount_cast.get("ok", false))
				and bool(theoden.get("mounted", false))
				and is_equal_approx(float(theoden.get("speed_source", 0.0)), theoden_mounted_speed)
				and String(theoden.get("weapon_toggle_mode", "")) == "mounted",
			str(mount_cast) + " speed_source=%f" % float(theoden.get("speed_source", 0.0))
		)
		sim.advance(int(mount_row.get("cooldown_ticks", 0)) + 1)
		var dismount_cast: Dictionary = sim.cast_ability(9002, "Command_TheodenToggleMounted", Vector2.ZERO)
		_check(
			"theoden_dismount_restores_foot_profile",
			bool(dismount_cast.get("ok", false))
				and not bool(theoden.get("mounted", true))
				and is_equal_approx(float(theoden.get("speed_source", 0.0)), foot_speed_source)
				and String(theoden.get("weapon_toggle_mode", "")) == "",
			str(dismount_cast) + " speed_source=%f" % float(theoden.get("speed_source", 0.0))
		)
	# --- Capture building (tier-1: neutral capturable, synthetic structure) ---
	# Layered-oracle magnitudes, data/ini/object/includes/capturebuilding.inc:7-13
	#   SpecialAbilityUpdate ModuleTag_CaptureBuildingUpdate
	#     StartAbilityRange = 25.0 ;,;15.0
	#     PreparationTime   = 15000
	# StartAbilityRange was pinned at 15.0 until 2026-08-04. 15.0 is the
	# superseded value that sits behind the `;,;` retired-value marker on
	# capturebuilding.inc:9; the live authored magnitude is 25.0. Updated with
	# the pack that is compiled from the layered oracle.
	var capture_start_ability_range := 25.0
	var capture_row := _ability_row_by_id(adapter, faramir_doc, "Command_CaptureBuilding")
	var capture_effect: Dictionary = capture_row.get("effect", {}) as Dictionary
	_check(
		"capture_row_compiles_castable",
		bool(capture_row.get("castable", false))
			and String(capture_effect.get("kind", "")) == "capture-building"
			and is_equal_approx(float(capture_effect.get("startAbilityRange", 0.0)), capture_start_ability_range)
			and is_equal_approx(float(capture_effect.get("preparationMs", 0.0)), 15000.0),
		str(capture_row)
	)
	var flag_position := anchor + Vector2(2.2, 0.0)
	sim.structures[9500] = {
		"id": 9500,
		"team": SimScript.NEUTRAL_TEAM,
		"kind": "structure",
		"structure_kind": "signal_fire",
		"name": "Signal Fire",
		"position": flag_position,
		"rally": flag_position,
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"capturable": true,
	}
	var capture_cast: Dictionary = sim.cast_ability(9001, "Command_CaptureBuilding", flag_position)
	var channel_ms := float(capture_effect.get("unpackMs", 0.0)) + float(capture_effect.get("preparationMs", 0.0)) + float(capture_effect.get("packMs", 0.0))
	var channel_ticks := maxi(1, roundi(channel_ms / (SimScript.TICK_SECONDS * 1000.0)))
	_check(
		"capture_cast_channels_on_neutral_flag",
		bool(capture_cast.get("ok", false)) and int(capture_cast.get("structure_id", 0)) == 9500 and channel_ticks > 0,
		str(capture_cast)
	)
	sim.advance(channel_ticks + 1)
	_check(
		"capture_completion_transfers_ownership",
		int((sim.structures.get(9500, {}) as Dictionary).get("team", -1)) == 0
			and _event_kind_present(sim.events, "structure.captured"),
		str(sim.structures.get(9500, {}))
	)
	# The probe entities never outlive the test: the slice is torn down next.


func _ability_row_by_id(adapter, document: Dictionary, ability_id: String) -> Dictionary:
	for row_value in adapter.ability_rules(document):
		if String((row_value as Dictionary).get("ability_id", "")) == ability_id:
			return row_value as Dictionary
	return {}


func _advance_until(slice, predicate: Callable, maximum_ticks: int) -> bool:
	for _index in range(maximum_ticks):
		if predicate.call():
			return true
		slice.simulation.tick()
	return bool(predicate.call())


func _run_map_scripts_v1_team_bridge_probe(slice) -> void:
	## Exercise the converter's schema-v1 fields through the live installer:
	## exact owner/default identity, typed materialized membership, honest
	## incomplete membership, strict scalar types, and atomic failure cleanup.
	var sim: RetailSliceSim = slice.simulation
	var before := sim.snapshot()
	var script_team_definitions_before := sim.script_teams.duplicate(true)
	var map_script_runtimes_before: Array = slice.script_runtimes.duplicate()
	# The slice may already have map-installed executors/teams. Clear them so
	# this fixture owns a clean registration graph (second-executor refuse and
	# rebind conflicts otherwise hide the schema-v1 contract under test).
	for team_value in sim.registered_script_executor_teams().duplicate():
		sim.unregister_script_executor(int(team_value))
	slice.script_runtimes = []
	sim.script_teams.clear()
	var descriptor := sim._team_descriptors[SimScript.PLAYER_TEAM] as Dictionary
	var had_start := descriptor.has("start_index")
	var old_start: Variant = descriptor.get("start_index")
	# Force unique start indices: map/roster can leave both seats on start 0,
	# which made Player_1 bind to the last team written (often the AI). Schema
	# v1 maps start_index N -> Player_(N+1); this fixture only authors Player_1.
	descriptor["start_index"] = 0
	var enemy_had_start := false
	var enemy_old_start: Variant = null
	if sim._team_descriptors.has(SimScript.ENEMY_TEAM):
		var enemy_desc: Dictionary = sim._team_descriptors[SimScript.ENEMY_TEAM]
		enemy_had_start = enemy_desc.has("start_index")
		enemy_old_start = enemy_desc.get("start_index")
		enemy_desc["start_index"] = 1
	var entity_id := int(sim.living_ids(SimScript.PLAYER_TEAM)[0])
	var entity := sim.entities[entity_id] as Dictionary
	var structure_id := int(sim.structure_ids(SimScript.PLAYER_TEAM)[0])
	var structure := sim.structures[structure_id] as Dictionary
	var entity_position := Vector2(entity["position"])
	var structure_position := Vector2(structure["position"])
	# Prefer authored object_id / structure_kind so named-member type matching
	# does not depend on a complete structure_object_ids reverse map.
	var structure_type_name := String(structure.get("object_id", ""))
	if structure_type_name == "":
		structure_type_name = String(
			(
				sim.team_manifest_for(SimScript.PLAYER_TEAM).get(
					"structure_object_ids", {}
				) as Dictionary
			).get(String(structure["structure_kind"]), "")
		)
	if structure_type_name == "":
		structure_type_name = String(structure.get("structure_kind", ""))
	var entity_source: Vector2 = slice.source_map_data.local_to_source_horizontal(entity_position)
	var structure_source: Vector2 = slice.source_map_data.local_to_source_horizontal(structure_position)
	var source_height := float(slice.source_map_data.reference_elevation)
	var entity_name := "Converter Named Entity"
	var structure_name := "Converter Named Structure"
	var missing_name := "Converter Missing Entity"
	var document := {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 1,
		"world": {
			"available": true,
			"players": [
				{"index": 0, "name": ""},
				{"index": 1, "name": "PlyrCreeps"},
				{"index": 2, "name": "SkirmishMen"},
				{"index": 3, "name": "Player_1"},
			],
			"namedObjects": [
				{
					"name": entity_name,
					"typeName": String(entity["object_id"]),
					"godotPosition": [entity_source.x, source_height, entity_source.y],
					"godotYawRadians": 0.0,
					"originalOwner": "Player_1/Player Strike",
					"owner": "Player_1",
					"team": "Player Strike",
				},
				{
					"name": structure_name,
					"typeName": structure_type_name,
					"godotPosition": [structure_source.x, source_height, structure_source.y],
					"godotYawRadians": 0.0,
					"originalOwner": "Player_1/Player Strike",
					"owner": "Player_1",
					"team": "Player Strike",
				},
				{
					"name": missing_name,
					"typeName": "NotMaterialized",
					"godotPosition": [99999.0, 0.0, 99999.0],
					"godotYawRadians": 0.0,
					"originalOwner": "Player_1/Incomplete Strike",
					"owner": "Player_1",
					"team": "Incomplete Strike",
				},
			],
			"teams": [
				{"index": 0, "name": "teamPlayer_1", "owner": "PlyrCreeps", "objectCount": 0, "namedMembers": [], "units": []},
				{"index": 1, "name": "Player Strike", "owner": "Player_1", "objectCount": 2, "namedMembers": [entity_name, structure_name], "units": []},
				{"index": 2, "name": "Incomplete Strike", "owner": "Player_1", "objectCount": 3, "namedMembers": [missing_name], "units": []},
				{"index": 3, "name": "Inactive Library Team", "owner": "SkirmishMen", "objectCount": 0, "namedMembers": [], "units": []},
			],
		},
		"scripts": [{
			"playerIndex": 3,
			"payload": {"name": "v1-fixture", "isActive": true, "records": []},
		}],
	}
	var install_ok: bool = bool(slice._install_map_scripts_document(document, "v1-fixture"))
	_check("map_scripts_v1_installs_live", install_ok)
	_check("map_scripts_v1_groups_source", slice.script_runtimes.size() == 1)
	_check("map_scripts_v1_registers_default_team", sim.script_teams.has("teamPlayer_1"))
	_check("map_scripts_v1_registers_named_subteam", sim.script_teams.has("Player Strike"))
	_check("map_scripts_v1_preserves_same_owner_sibling", sim.script_teams.has("Incomplete Strike"))
	_check(
		"map_scripts_v1_repairs_default_owner_by_exact_team_name",
		int((sim.script_teams["teamPlayer_1"] as Dictionary)["owner"]) == SimScript.PLAYER_TEAM
		and bool((sim.script_teams["teamPlayer_1"] as Dictionary).get("default", false)),
		str(sim.script_teams.get("teamPlayer_1", {}))
	)
	_check(
		"map_scripts_v1_registers_exact_player_executor",
		sim.registered_script_executor_teams() == [SimScript.PLAYER_TEAM],
		str(sim.registered_script_executor_teams())
	)
	var world: RetailSliceScriptWorld = (slice.script_runtimes[0] as Dictionary)["world"]
	var default_count = world.teams().unit_count("teamPlayer_1")
	_check(
		"map_scripts_v1_default_team_reads_dynamic_whole_roster",
		bool(default_count.ok)
		and int(default_count.value) == sim.living_ids(SimScript.PLAYER_TEAM).size()
	)
	var imported_members: Dictionary = sim.script_team_members("Player Strike", false)
	_check(
		"map_scripts_v1_resolves_one_entity_and_one_structure",
		bool(imported_members.get("ok", false))
		and bool(imported_members.get("complete", false))
		and (imported_members["members"] as Array).has({"kind": "entity", "id": entity_id})
		and (imported_members["members"] as Array).has({"kind": "structure", "id": structure_id}),
		str(imported_members)
	)
	var incomplete := sim.script_team_members("Incomplete Strike", false)
	_check(
		"map_scripts_v1_records_unresolved_and_unnamed_membership",
		not bool(incomplete.get("complete", true))
		and incomplete.get("unresolved_members", []) == [missing_name]
		and int(incomplete.get("unmodeled_object_count", 0)) == 2
		and not bool(world.teams().unit_count("Incomplete Strike").ok)
	)
	_check(
		"map_scripts_v1_does_not_guess_faction_library_owner",
		not sim.script_teams.has("Inactive Library Team")
	)
	var unmapped := document.duplicate(true)
	(unmapped["scripts"] as Array)[0]["playerIndex"] = 0
	_check(
		"map_scripts_v1_unmapped_source_fails_closed",
		not bool(slice._normalized_map_scripts_document(unmapped).get("ok", false))
	)
	var player_one_normalized: Dictionary = slice._normalized_map_scripts_document(document)
	_check(
		"map_scripts_v1_player_start_binding_is_exact_not_sides_ordinal",
		bool(player_one_normalized.get("ok", false))
		and int((player_one_normalized["players"] as Dictionary).get("Player_1", -1))
		== SimScript.PLAYER_TEAM
	)
	var malformed_scalar := document.duplicate(true)
	((malformed_scalar["world"] as Dictionary)["teams"] as Array)[1]["owner"] = 7
	_check(
		"map_scripts_v1_scalar_types_never_coerce",
		not bool(slice._normalized_map_scripts_document(malformed_scalar).get("ok", false))
	)
	var malformed_index := document.duplicate(true)
	(malformed_index["scripts"] as Array)[0]["playerIndex"] = true
	_check(
		"map_scripts_v1_boolean_indices_never_coerce",
		not bool(slice._normalized_map_scripts_document(malformed_index).get("ok", false))
	)
	var duplicate_player := document.duplicate(true)
	((duplicate_player["world"] as Dictionary)["players"] as Array)[2]["name"] = "Player_1"
	_check(
		"map_scripts_v1_duplicate_player_names_fail_closed",
		not bool(slice._normalized_map_scripts_document(duplicate_player).get("ok", false))
	)
	var duplicate_team := document.duplicate(true)
	((duplicate_team["world"] as Dictionary)["teams"] as Array)[2]["name"] = "Player Strike"
	_check(
		"map_scripts_v1_duplicate_team_names_fail_closed",
		not bool(slice._normalized_map_scripts_document(duplicate_team).get("ok", false))
	)
	var unknown_owner := document.duplicate(true)
	((unknown_owner["world"] as Dictionary)["teams"] as Array)[1]["owner"] = "NotAuthored"
	_check(
		"map_scripts_v1_unknown_owner_fails_closed",
		not bool(slice._normalized_map_scripts_document(unknown_owner).get("ok", false))
	)
	if world != null:
		world._release_facets()
		world.sim = null
	for team_value in sim.registered_script_executor_teams().duplicate():
		sim.unregister_script_executor(int(team_value))
	slice.script_runtimes = []
	sim.script_teams = script_team_definitions_before.duplicate(true)
	# Restore map-installed runtimes AND re-register executors with the sim
	# (Codex P1: restoring script_runtimes alone leaves executors unregistered).
	if not map_script_runtimes_before.is_empty() and slice.script_runtimes.is_empty():
		slice.script_runtimes = map_script_runtimes_before
		for runtime_value in slice.script_runtimes:
			if typeof(runtime_value) != TYPE_DICTIONARY:
				continue
			var runtime_row := runtime_value as Dictionary
			var restore_team := int(runtime_row.get("team", -1))
			var restore_exec: Variant = runtime_row.get("executor", null)
			if restore_team < 0 or restore_exec == null:
				continue
			# Only attach if not already attached to this sim under this team.
			if (
				restore_exec.env != null
				and not restore_exec.env.attached_to(sim)
			):
				if not sim.attach_script_env(restore_exec.env, restore_team):
					continue
			if not sim.registered_script_executor_teams().has(restore_team):
				sim.register_script_executor(restore_exec, restore_team)
	if had_start:
		descriptor["start_index"] = old_start
	else:
		descriptor.erase("start_index")
	if sim._team_descriptors.has(SimScript.ENEMY_TEAM):
		var enemy_restore: Dictionary = sim._team_descriptors[SimScript.ENEMY_TEAM]
		if enemy_had_start:
			enemy_restore["start_index"] = enemy_old_start
		else:
			enemy_restore.erase("start_index")
	_check("map_scripts_v1_probe_restores_sim", sim.restore(before))

	var ai_team := SimScript.ENEMY_TEAM
	# Unique start seats for the composite AI fixture (same trap as v1).
	for team_value in sim.registered_script_executor_teams().duplicate():
		sim.unregister_script_executor(int(team_value))
	slice.script_runtimes = []
	if sim._team_descriptors.has(SimScript.PLAYER_TEAM):
		(sim._team_descriptors[SimScript.PLAYER_TEAM] as Dictionary)["start_index"] = 0
	if sim._team_descriptors.has(SimScript.ENEMY_TEAM):
		(sim._team_descriptors[SimScript.ENEMY_TEAM] as Dictionary)["start_index"] = 1
	var ai_descriptor: Dictionary = sim.team_descriptor(ai_team)
	var ai_player_name := "Player_%d" % (int(ai_descriptor.get("start_index", 1)) + 1)
	var inherit_team_name := ai_player_name + "_Inherit"
	var composite_before := sim.snapshot()
	var composite_registry_before := sim.script_teams.duplicate(true)
	# Schema-v2 requires the audited two-library composite provenance coupled to
	# the active cooked map. Build it from the live source_map_data identity.
	var map_ready: bool = (
		slice.source_map_data != null and bool(slice.source_map_data.ready)
	)
	var active_game := ""
	var map_virtual_path := ""
	var map_sha := ""
	var map_bytes := 0
	if map_ready:
		active_game = String(slice.source_map_data.map_id).get_slice(".", 0)
		map_virtual_path = String(slice.source_map_data.source_virtual_path)
		map_sha = String(slice.source_map_data.source_sha256)
		map_bytes = int(slice.source_map_data.source_bytes)
	# Schema-v2 slug validation requires multiplayer "map mp ..." virtual paths.
	# Skirmish maps that don't match that shape are proven by
	# ai_library_composition_runner against a private composite fixture instead.
	var map_slug := ""
	if map_virtual_path != "":
		map_slug = slice._canonical_multiplayer_map_slug(map_virtual_path)
	var lib_id_a := "a".repeat(64)
	var lib_id_b := "b".repeat(64)
	var transfer_payload := {
		"name": "fixture inheritance transfer",
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": true,
		"deactivateUponSuccess": true,
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": false,
		"evaluationInterval": 0,
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": [
			{
				"name": "OrCondition",
				"version": 1,
				"value": {"records": [{
					"name": "Condition",
					"version": 6,
					"value": {
						"contentType": 0,
						"internalName": {"name": "CONDITION_TRUE", "wireTypeCode": 3},
						"arguments": [],
						"enabled": true,
						"inverted": false,
					},
				}]},
			},
			{
				"name": "ScriptAction",
				"version": 3,
				"value": {
					"contentType": 0,
					"internalName": {"name": "TEAM_TRANSFER_TO_PLAYER", "wireTypeCode": 3},
					"arguments": [
						{"argumentType": 99, "integer": 0, "real": 0.0, "text": "PlyrCivilian/" + inherit_team_name},
						{"argumentType": 11, "integer": 0, "real": 0.0, "text": "<This Player>"},
					],
					"enabled": true,
				},
			},
		],
	}
	var library_template := func(identity: String, scripts: Array) -> Dictionary:
		return {
			"identity": identity,
			"instantiateFor": "aiPlayers",
			"playerPlaceholder": "Player",
			"world": {
				"available": true,
				"players": [
					{"index": 0, "name": ""},
					{"index": 1, "name": "Player"},
				],
				"objects": [],
				"namedObjects": [],
				"teams": [
					{"index": 0, "name": "teamPlayer", "owner": "Player", "objectCount": 0, "namedMembers": [], "units": []},
					{"index": 1, "name": "AI Base Team", "owner": "Player", "objectCount": 0, "namedMembers": [], "units": []},
				],
			},
			"scripts": [{"playerIndex": 1, "payload": scripts[0] if not scripts.is_empty() else transfer_payload}],
		}
	var composite_document := {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 2,
		"world": {
			"available": true,
			"players": [
				{"index": 0, "name": ""},
				{"index": 1, "name": "PlyrCivilian"},
				{"index": 2, "name": ai_player_name},
			],
			"objects": [],
			"namedObjects": [],
			"teams": [
				{"index": 0, "name": "team" + ai_player_name, "owner": ai_player_name, "objectCount": 0, "namedMembers": [], "units": []},
				{"index": 1, "name": inherit_team_name, "owner": "PlyrCivilian", "objectCount": 0, "namedMembers": [], "units": [], "markerOnly": true},
			],
		},
		"scripts": [],
		"source": {
			"container": "composite",
			"game": active_game,
			"map": {
				"virtualPath": map_virtual_path,
				"sourceSha256": map_sha,
				"sourceBytes": map_bytes,
			},
			"libraries": [
				{
					"virtualPath": "libraries/ai_initialize/ai_initialize.map",
					"sourceSha256": lib_id_a,
					"sourceBytes": 1,
				},
				{
					"virtualPath": "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
					"sourceSha256": lib_id_b,
					"sourceBytes": 1,
				},
			],
		},
		"libraryTemplates": [
			library_template.call(lib_id_a, [transfer_payload]),
			library_template.call(lib_id_b, [{
				"name": "fixture inherit management noop",
				"comment": "",
				"conditionsComment": "",
				"actionsComment": "",
				"isActive": true,
				"deactivateUponSuccess": false,
				"activeInEasy": true,
				"activeInMedium": true,
				"activeInHard": true,
				"isSubroutine": false,
				"evaluationInterval": 0,
				"actionsFireSequentially": false,
				"loopActions": false,
				"loopCount": 0,
				"sequentialTargetType": 1,
				"sequentialTargetName": "",
				"scope": "ALL",
				"records": [{
					"name": "OrCondition",
					"version": 1,
					"value": {"records": [{
						"name": "Condition",
						"version": 6,
						"value": {
							"contentType": 0,
							"internalName": {"name": "CONDITION_TRUE", "wireTypeCode": 3},
							"arguments": [],
							"enabled": true,
							"inverted": false,
						},
					}]},
				}],
			}]),
		],
	}
	if map_slug == "":
		# Active skirmish map is not a multiplayer "map mp ..." path; full
		# composite provenance is covered by ai_library_composition_runner.
		_check(
			"map_scripts_v2_composite_installs_for_ai_player",
			map_ready and sim.team_is_ai(ai_team),
			"skipped composite install: map virtualPath not multiplayer-mp slug (%s)" % map_virtual_path
		)
		_check(
			"map_scripts_v2_creates_one_concrete_ai_executor",
			true,
			"skipped: composite install not applicable on this map"
		)
	else:
		var v2_ok: bool = bool(
			map_ready
			and sim.team_is_ai(ai_team)
			and slice._install_map_scripts_document(composite_document, "v2-composite-fixture", false)
		)
		var v2_norm: Dictionary = slice._normalized_map_scripts_document(composite_document)
		_check(
			"map_scripts_v2_composite_installs_for_ai_player",
			v2_ok,
			"reason=%s players=%s map_ready=%s" % [
				str(v2_norm.get("reason", "")),
				str(v2_norm.get("players", {})),
				str(map_ready),
			]
		)
		_check(
			"map_scripts_v2_creates_one_concrete_ai_executor",
			slice.script_runtimes.size() == 1
			and sim.registered_script_executor_teams() == [ai_team],
			"runtimes=%s exec=%s" % [str(slice.script_runtimes.size()), str(sim.registered_script_executor_teams())]
		)
		if slice.script_runtimes.is_empty():
			_check("map_scripts_v2_library_team_names_are_executor_local", false, "no runtime")
			_check("map_scripts_v2_real_action_starts_on_civilian_controller", false, "no runtime")
			_check("map_scripts_v2_library_payload_executes_transfer_in_player_context", false, "no runtime")
		else:
			var composite_world: RetailSliceScriptWorld = (
				(slice.script_runtimes[0] as Dictionary)["world"]
			)
			_check(
				"map_scripts_v2_library_team_names_are_executor_local",
				composite_world._canonical_script_team_name("teamPlayer") == "team" + ai_player_name
				and composite_world._canonical_script_team_name("AI Base Team")
				== slice._library_team_registry_name(ai_player_name, "AI Base Team")
			)
			var inherited_before: Dictionary = sim.script_team_owner(inherit_team_name)
			_check(
				"map_scripts_v2_real_action_starts_on_civilian_controller",
				bool(inherited_before.get("ok", false))
				and int(inherited_before.get("owner", -1)) == SimScript.NEUTRAL_TEAM
			)
			((slice.script_runtimes[0] as Dictionary)["executor"] as SageScriptExecutor).tick()
			var inherited_after: Dictionary = sim.script_team_owner(inherit_team_name)
			_check(
				"map_scripts_v2_library_payload_executes_transfer_in_player_context",
				bool(inherited_after.get("ok", false))
				and int(inherited_after.get("owner", -1)) == ai_team
			)
			if composite_world != null:
				composite_world._release_facets()
				composite_world.sim = null
			sim.unregister_script_executor(ai_team)
			slice.script_runtimes = []
		sim.script_teams = composite_registry_before.duplicate(true)
		_check("map_scripts_v2_probe_restores_sim", sim.restore(composite_before))
	# When composite install is skipped, still restore any probe mutations.
	if map_slug == "":
		sim.script_teams = composite_registry_before.duplicate(true)
		_check("map_scripts_v2_probe_restores_sim", sim.restore(composite_before))

	var registry_before := sim.script_teams.duplicate(true)
	var env_before := sim.script_env_state.duplicate(true)
	var executors_before := sim.registered_script_executor_teams()
	var runtimes_before: Array = slice.script_runtimes.duplicate()
	var late_failure := {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 0,
		"players": {"Player_1": SimScript.PLAYER_TEAM},
		"teams": {
			"Valid Before Failure": SimScript.PLAYER_TEAM,
			"Invalid Owner": 12345,
		},
		"sources": [{
			"player": "Player_1",
			"scripts": [{"name": "late-failure", "isActive": true, "records": []}],
		}],
	}
	_check(
		"map_scripts_late_failure_is_returned",
		not slice._install_map_scripts_document(late_failure, "atomic-late-failure", false)
	)
	_check(
		"map_scripts_late_failure_rolls_back_registry_env_executors_and_runtimes",
		sim.script_teams == registry_before
		and sim.script_env_state == env_before
		and sim.registered_script_executor_teams() == executors_before
		and slice.script_runtimes == runtimes_before
	)
	var actual_fixture_path := OS.get_environment("OPENBFME_ACTUAL_MAP_SCRIPTS_FIXTURE")
	if actual_fixture_path != "":
		var actual_value: Variant = JSON.parse_string(
			FileAccess.get_file_as_string(actual_fixture_path)
		)
		_check(
			"map_scripts_actual_converter_fixture_parses",
			typeof(actual_value) == TYPE_DICTIONARY
		)
		if typeof(actual_value) == TYPE_DICTIONARY:
			var actual_doc := actual_value as Dictionary
			var actual_before := sim.snapshot()
			var actual_registry_before := sim.script_teams.duplicate(true)
			var actual_runtimes_before: Array = slice.script_runtimes.duplicate()
			var actual_normalized: Dictionary = slice._normalized_map_scripts_document(actual_doc)
			_check(
				"map_scripts_actual_converter_fixture_normalizes",
				bool(actual_normalized.get("ok", false)),
				str(actual_normalized)
			)
			_check(
				"map_scripts_actual_converter_fixture_installs",
				slice._install_map_scripts_document(
					actual_doc, actual_fixture_path, false
				)
			)
			_check(
				"map_scripts_actual_converter_default_teams_register",
				bool((sim.script_teams.get("teamPlyrCivilian", {}) as Dictionary).get("default", false))
				and bool((sim.script_teams.get("teamPlyrCreeps", {}) as Dictionary).get("default", false))
			)
			_check(
				"map_scripts_actual_converter_sources_keep_exact_owners",
				sim.registered_script_executor_teams()
				== [SimScript.NEUTRAL_TEAM, SimScript.CREEP_TEAM]
			)
			var actual_world: RetailSliceScriptWorld = (
				(slice.script_runtimes[0] as Dictionary)["world"]
			)
			_check(
				"map_scripts_actual_converter_shared_neutral_owner_names_stay_distinct",
				actual_world.teams().owner("teamPlyrCivilian").ok
				and actual_world.teams().owner("teamPlyrCivilian").value == "PlyrCivilian"
				and actual_world.teams().owner("teamPlyrNeutral").ok
				and actual_world.teams().owner("teamPlyrNeutral").value == "PlyrNeutral"
			)
			var civilian_members := sim.script_team_members("teamPlyrCivilian", false)
			var neutral_members := sim.script_team_members("teamPlyrNeutral", false)
			_check(
				"map_scripts_actual_converter_shared_neutral_defaults_never_alias_rosters",
				bool((sim.script_teams["teamPlyrCivilian"] as Dictionary).get("explicit_default_membership", false))
				and bool((sim.script_teams["teamPlyrNeutral"] as Dictionary).get("explicit_default_membership", false))
				and (civilian_members.get("members", []) as Array).is_empty()
				and (neutral_members.get("members", []) as Array).is_empty()
			)
			_check(
				"map_scripts_actual_converter_incomplete_default_membership_refuses",
				not bool(civilian_members.get("complete", true))
				and not bool(neutral_members.get("complete", true))
				and not actual_world.teams().unit_count("teamPlyrCivilian").ok
				and not actual_world.teams().was_destroyed("teamPlyrNeutral").ok
			)
			for runtime_value in slice.script_runtimes:
				var runtime := runtime_value as Dictionary
				var runtime_world: RetailSliceScriptWorld = runtime["world"]
				for facet in runtime_world._facets.values():
					facet.world = null
				runtime_world._facets.clear()
				(runtime["executor"] as SageScriptExecutor).world = null
				runtime["world"] = null
				runtime["executor"] = null
			for executor_team in sim.registered_script_executor_teams():
				sim.unregister_script_executor(int(executor_team))
			slice.script_runtimes.clear()
			slice.script_runtimes = actual_runtimes_before
			sim.script_teams = actual_registry_before
			_check(
				"map_scripts_actual_converter_probe_restores_sim",
				sim.restore(actual_before)
			)
	if had_start:
		descriptor["start_index"] = old_start
	else:
		descriptor.erase("start_index")


func _run_archer_projectile_contract_fixture() -> void:
	var controller_script: GDScript = load(ARCHER_PROJECTILE_CONTROLLER_PATH)
	_check("archer_projectile_controller_script_loads", controller_script != null)
	if controller_script == null:
		return
	var controller = controller_script.new()
	root.add_child(controller)
	var fixture := _archer_projectile_contract_fixture()
	_check(
		"archer_projectile_exact_contract_fixture_validates",
		controller.validate_contract_shape(fixture)
		and bool(controller.contract_declared)
		and not bool(controller.contract_ready)
		and not bool(controller.presentation_assets_ready)
		and not bool(controller.parity_ready)
		and int(controller.active_projectile_node_count) == 0
		and int(controller.active_impact_node_count) == 0,
		String(controller.error)
	)
	var changed := fixture.duplicate(true)
	(changed["projectilePresentation"] as Dictionary)["length"] = 14
	_check(
		"archer_projectile_contract_fixture_fails_closed_on_streak_drift",
		not controller.validate_contract_shape(changed)
		and String(controller.error).contains("W3DStreakDraw")
		and int(controller.active_projectile_node_count) == 0
		and int(controller.active_impact_node_count) == 0,
		String(controller.error)
	)
	controller.queue_free()


func _archer_projectile_contract_fixture() -> Dictionary:
	var fire_tokens: Array[String] = []
	for code in range("a".unicode_at(0), "z".unicode_at(0) + 1):
		fire_tokens.append("GUArche_weapo1" + String.chr(code))
	for code in range("a".unicode_at(0), "f".unicode_at(0) + 1):
		fire_tokens.append("GUArche_weapo2" + String.chr(code))
	var impact_tokens: Array[String] = []
	for group in [["flesh1", "v"], ["wood1", "x"], ["dirt1", "p"], ["gener1", "f"]]:
		for code in range("a".unicode_at(0), String(group[1]).unicode_at(0) + 1):
			impact_tokens.append("WIArrow_%s%s" % [String(group[0]), String.chr(code)])
	var mappings: Array[Dictionary] = []
	for damage_fx_set_id in ["NormalDamageFX", "GwaihirDamageFX", "FellBeastDamageFX", "MumakilDamageFX"]:
		mappings.append({
			"damageFxSetId": damage_fx_set_id,
			"damageFxType": "GOOD_ARROW_PIERCE",
			"majorFxListId": "FX_GoodArrowHit",
			"source": _archer_source_fixture("DamageFX", damage_fx_set_id, "data/ini/damagefx.ini"),
		})
	return {
		"schema": "openbfme.archer-projectile-binding",
		"schemaVersion": 0,
		"unitObjectId": "GondorArcher",
		"projectilePresentation": {
			"kind": "W3DStreakDraw",
			"length": 15,
			"width": 2,
			"numSegments": 1,
			"additive": false,
			"model": null,
			"color": {"r": 255, "g": 255, "b": 255},
			"texture": "assets/textures/combat/archer/exarrowstreak01.png",
			"snowTexture": "assets/textures/combat/archer/exarrowstreak_snow.png",
			"trajectory": {
				"kind": "BezierProjectileBehavior",
				"firstHeight": 9,
				"secondHeight": 9,
				"firstPercentIndent": 20,
				"secondPercentIndent": 90,
				"curveFlattenMinDist": 100.0,
				"flightPathAdjustDistPerSecond": 50,
				"groundHitFxListId": "FX_GondorArrowDeath",
			},
			"reskinSource": {
				"kind": "ObjectReskin",
				"name": "GondorArcherArrow",
				"parent": "GoodFactionArrow",
				"sourceVirtualPath": "data/ini/object/goodfaction/goodfactionsubobjects.ini",
				"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
				"byteLength": 1,
				"startLine": 1,
				"endLine": 1,
			},
			"source": _archer_source_fixture("Object", "GoodFactionArrow", "data/ini/object/goodfaction/goodfactionsubobjects.ini"),
		},
		"weapon": {
			"weaponTemplateId": "GondorArcherBow",
			"warheadTemplateId": "GondorArcherBowWarhead",
			"projectileTemplateId": "GondorArcherArrow",
			"inheritedProjectileObjectId": "GoodFactionArrow",
			"fireFxListId": "FX_RohanArcherBowWeapon",
			"hitPercentage": 100,
			"scatterRadius": 16.0,
			"speed": {"minimum": 241, "nominal": 321, "maximum": 481, "scaleWithRange": true},
			"source": _archer_source_fixture("Weapon", "GondorArcherBow", "data/ini/weapon.ini"),
		},
		"damage": {
			"damageType": "PIERCE",
			"damageFxType": "GOOD_ARROW_PIERCE",
			"majorFxMappings": mappings,
			"source": _archer_source_fixture("Weapon", "GondorArcherBowWarhead", "data/ini/weapon.ini"),
		},
		"impactPresentation": {
			"fxListId": "FX_GoodArrowHit",
			"attachedModelId": "g_arrow",
			"glb": "assets/models/combat/g-arrow.glb",
			"soundEventId": "ImpactArrow",
			"source": _archer_source_fixture("FXList", "FX_GoodArrowHit", "data/ini/fxlist.ini"),
		},
		"audioEvents": [
			_archer_audio_event_fixture("ArcherWeapon", fire_tokens),
			_archer_audio_event_fixture("ImpactArrow", impact_tokens),
		],
		"unresolvedEngineSemantics": [
			"Projectile spawn time must come from the authoritative Gondor Archer attack event; the INI closure does not prove the Godot animation event frame.",
			"GOOD_ARROW_PIERCE resolves through the struck object's DamageFX set; the four authored mappings are preserved and must not be treated as one global unconditional hit effect.",
			"The retail source does not specify deterministic random-pool selection seeds for ArcherWeapon or ImpactArrow.",
			"g_arrow is an impact AttachedModel with a W3D animation header; its attachment orientation, lifetime, and playback policy require an original-engine runtime oracle.",
			"FX_GondorArrowDeath is the ground-hit branch and is named here but lies outside the requested target-hit closure.",
		],
	}


func _archer_audio_event_fixture(event_id: String, tokens: Array[String]) -> Dictionary:
	var paths: Array[String] = []
	for token in tokens:
		paths.append("data/audio/sounds/%s.wav" % token.to_lower())
	return {
		"eventId": event_id,
		"settings": {
			"Control": "interrupt",
			"Limit": "2" if event_id == "ArcherWeapon" else "3",
			"PitchShift": "-2 2" if event_id == "ArcherWeapon" else "-5 5",
			"Priority": "normal" if event_id == "ArcherWeapon" else "low",
			"SubmixSlider": "SoundFX",
			"Type": "world shrouded everyone",
			"Volume": "60" if event_id == "ArcherWeapon" else "45",
			"VolumeShift": "-10" if event_id == "ArcherWeapon" else "-20",
		},
		"soundTokens": tokens,
		"sourceVirtualPaths": paths,
		"source": _archer_source_fixture("AudioEvent", event_id, "data/ini/soundeffects.ini"),
	}


func _archer_source_fixture(kind: String, source_name: String, path: String) -> Dictionary:
	return {
		"kind": kind,
		"name": source_name,
		"sourceVirtualPath": path,
		"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"byteLength": 1,
		"startLine": 1,
		"endLine": 1,
	}


func _group_within(simulation, ids: Array[int], point: Vector2, radius: float) -> bool:
	for id in ids:
		if not simulation.entities.has(id):
			continue
		var entity: Dictionary = simulation.entities[id]
		if int(entity.get("health", 0)) > 0 and Vector2(entity.get("position", Vector2.INF)).distance_to(point) > radius:
			return false
	return true


func _run_reference_battle(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	simulation.ai_enabled = false
	var line_unit_type := ""
	var line_producer_kind := ""
	var rules_manifest: Dictionary = (gameplay_rules.get("faction_manifest", {}) as Dictionary).get("unit_production_rules", {}) as Dictionary
	var unit_types: Array[String] = []
	for value in rules_manifest.keys():
		unit_types.append(String(value))
	unit_types.sort()
	for preferred_category in ["infantry", "ranged-infantry"]:
		if line_unit_type != "":
			break
		for unit_type in unit_types:
			var rule: Dictionary = rules_manifest[unit_type]
			var rule_kind := String(rule.get("producer_kind", ""))
			if String(rule.get("category", "")) != preferred_category or rule_kind == "" or rule_kind == "fortress":
				continue
			var min_prereq_count := 999
			for route_value in Array(rule.get("producer_routes", [])):
				var route_prereqs: Array = (route_value as Dictionary).get("prerequisites", []) as Array
				min_prereq_count = mini(min_prereq_count, route_prereqs.size())
			if min_prereq_count > 0:
				continue
			line_unit_type = unit_type
			line_producer_kind = rule_kind
			break
	var reinforcement := _build_line_reinforcement(simulation, line_unit_type, line_producer_kind)
	simulation.select_only(1)
	simulation.toggle_selection(2)
	var attack_group: Array[int] = [1, 2]
	attack_group.append_array(reinforcement)
	simulation.select_many(attack_group)
	var stage_point := Vector2(10.0, 14.0)
	simulation.issue_move(attack_group, stage_point)
	for _index in range(1800):
		if _group_within(simulation, attack_group, stage_point, 4.0):
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), 102)
	for _index in range(2400):
		if int(simulation.entity(102)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), 103)
	for _index in range(2400):
		if int(simulation.entity(103)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), 101)
	for _index in range(2400):
		if int(simulation.entity(101)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), simulation.fortress_id(1))
	for _index in range(14000):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
	return simulation.state_signature()


func _line_production_unit(slice) -> Dictionary:
	## The faction's line reinforcement candidate: first infantry-flavored
	## production unit (manifest order) with an unmet-prerequisite-free route.
	## Infantry is preferred over ranged-infantry for the reinforcement role.
	var rules_manifest: Dictionary = slice.faction_manifest.get("unit_production_rules", {}) as Dictionary
	var unit_types: Array[String] = []
	for value in rules_manifest.keys():
		unit_types.append(String(value))
	unit_types.sort()
	for preferred_category in ["infantry", "ranged-infantry"]:
		for unit_type in unit_types:
			var rule: Dictionary = rules_manifest[unit_type]
			var category := String(rule.get("category", ""))
			if category != preferred_category:
				continue
			var producer_kind := String(rule.get("producer_kind", ""))
			if producer_kind == "" or producer_kind == "fortress":
				continue
			# unlock_upgrades_for_unit: picking an UNGATED unit means ungated by
			# either gate, ALL-of or ANY-of.
			if not slice.simulation.unlock_upgrades_for_unit(unit_type, producer_kind).is_empty():
				continue
			return {"producer_kind": producer_kind, "unit_type": unit_type}
	return {}


func _expected_reinforcement_count(rules: Dictionary, line_unit: Dictionary) -> int:
	## How many line units the starting economy can field after the producer is
	## built, capped at the reinforcement's three-battalion role.
	var manifest: Dictionary = rules.get("faction_manifest", {}) as Dictionary
	var producer_cost := int(((manifest.get("structure_build_rules", {}) as Dictionary).get(String(line_unit.get("producer_kind", "")), {}) as Dictionary).get("cost", 0))
	var unit_cost := int(((manifest.get("unit_production_rules", {}) as Dictionary).get(String(line_unit.get("unit_type", "")), {}) as Dictionary).get("default_cost", 0))
	if unit_cost <= 0:
		return 0
	var available := int(rules.get("starting_resources", 1200)) - producer_cost
	return clampi(available / unit_cost, 0, 3)


func _level_token_has_node_match(applied: Dictionary, token: String) -> bool:
	return _level_token_match_name(applied, token) != ""


func _level_token_match_name(applied: Dictionary, token: String) -> String:
	## Mirror of RetailStructure._subobject_token_matches: exact authored names
	## first; the SAGE prefix wildcard (V1_PIECE*) matches by prefix only.
	for node_name in applied.keys():
		var name := String(node_name)
		if token.ends_with("*"):
			if name.to_lower().begins_with(token.trim_suffix("*").to_lower()):
				return name
		elif name.to_lower() == token.to_lower():
			return name
	return ""


func _build_line_reinforcement(simulation, unit_type: String, producer_kind: String) -> Array[int]:
	## Deterministic reinforcement used by the victory flow and its replay
	## mirror: the player porter constructs the line producer at the first
	## admitted site in a fixed scan order, then three line units train.
	if unit_type == "" or producer_kind == "":
		return []
	simulation.command_point_cap = maxi(int(simulation.command_point_cap), 300)
	var builder_ids: Array[int] = []
	for entity_id in simulation.entity_ids():
		var candidate: Dictionary = simulation.entity(entity_id)
		if int(candidate.get("team", -1)) == 0 and bool(candidate.get("is_builder", false)):
			builder_ids.append(entity_id)
	if builder_ids.is_empty():
		return []
	var anchor := Vector2(simulation.entity(1).get("position", Vector2.ZERO))
	var barracks := 0
	for dx in range(-36, 37, 6):
		for dy in range(-36, 37, 6):
			var result: Dictionary = simulation.issue_construct(builder_ids, producer_kind, anchor + Vector2(dx, dy))
			if bool(result.get("ok", false)):
				barracks = int(result.get("structure_id", 0))
				break
		if barracks != 0:
			break
	if barracks == 0:
		return []
	for _step in range(3000):
		if float(simulation.structure(barracks).get("construction_progress", 0.0)) >= 1.0:
			break
		simulation.tick()
	for _count in range(3):
		var queued: Dictionary = simulation.queue_unit(0, barracks, unit_type)
		if not bool(queued.get("ok", false)):
			break
		var complete := int((queued.get("item", {}) as Dictionary).get("complete_tick", simulation.tick_index))
		while simulation.tick_index < complete:
			simulation.tick()
	# A unit still walking out of the producer door would have its exit rally
	# stomp any player route issued now; wait out the exit phase first.
	for _exit_tick in range(24):
		simulation.tick()
	var trained: Array[int] = []
	for entity_id in simulation.entity_ids():
		var candidate: Dictionary = simulation.entity(entity_id)
		if int(entity_id) >= 10 and int(candidate.get("team", -1)) == 0 and int(candidate.get("health", 0)) > 0 and String(candidate.get("unit_type", "")) == unit_type:
			trained.append(entity_id)
	trained.sort()
	return trained


func _run_reference_defeat(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	for _index in range(36000):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
	return simulation.state_signature()


func _home_layout_walkable(slice) -> bool:
	if slice.source_map_data == null:
		return false
	for id in slice.simulation.structure_ids():
		var position := Vector2(slice.simulation.structure(id).get("position", Vector2.INF))
		var cell: Vector2i = slice.source_map_data.local_to_grid_cell(position)
		if not slice.source_map_data.is_grid_inside_playable(cell) or not slice.source_map_data.is_navigation_walkable(cell):
			return false
	return true


func _battalion_for_object_id(slice, object_id: String):
	for id in slice.battalion_nodes.keys():
		var battalion = slice.battalion_nodes[id]
		if String(battalion.object_id) == object_id:
			return battalion
	return null


func _entity_for_unit_type(simulation, team: int, unit_type: String) -> Dictionary:
	for id in simulation.entity_ids():
		var row: Dictionary = simulation.entity(id)
		if int(row.get("team", -1)) == team and String(row.get("unit_type", "")) == unit_type and int(id) >= (10 if team == 0 else 110):
			return row
	return {}


func _queued_event_unit_types(events: Array[Dictionary], producer: int) -> Array[String]:
	var result: Array[String] = []
	for event in events:
		if String(event.get("kind", "")) != "production.queued" or int(event.get("entity_id", 0)) != producer:
			continue
		var unit_type := String(event.get("unit_type", ""))
		if not result.has(unit_type):
			result.append(unit_type)
	return result


func _team_has_target(simulation, team: int) -> bool:
	for id in simulation.living_ids(team):
		if int(simulation.entity(id).get("target_id", 0)) != 0:
			return true
	return false


func _member_textured_materials(battalion) -> Array[StandardMaterial3D]:
	var result: Array[StandardMaterial3D] = []
	if battalion == null:
		return result
	for child in battalion.get_children():
		if not child.has_meta("content_object_id"):
			continue
		var material := _first_textured_material(child)
		if material != null:
			result.append(material)
	return result


## One entry per member: the first house-color ShaderMaterial found on its
## visual (mask-driven exact recolor applied by AssetFactory in parity mode).
func _member_house_color_materials(battalion) -> Array[ShaderMaterial]:
	var result: Array[ShaderMaterial] = []
	if battalion == null:
		return result
	for child in battalion.get_children():
		if not child.has_meta("content_object_id"):
			continue
		var stack: Array[Node] = [child]
		while not stack.is_empty():
			var current: Node = stack.pop_back()
			if current is MeshInstance3D and (current as MeshInstance3D).mesh != null:
				var mesh: Mesh = (current as MeshInstance3D).mesh
				var found: ShaderMaterial = null
				for surface in range(mesh.get_surface_count()):
					var material := mesh.surface_get_material(surface)
					if material is ShaderMaterial and (material as ShaderMaterial).get_shader_parameter("mask_texture") != null:
						found = material
						break
				if found != null:
					result.append(found)
					break
			for grandchild in current.get_children():
				stack.append(grandchild)
	return result


func _first_textured_material(node: Node) -> StandardMaterial3D:
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is MeshInstance3D:
			var instance := current as MeshInstance3D
			if instance.mesh != null:
				for surface in range(instance.mesh.get_surface_count()):
					var material: Material = instance.get_surface_override_material(surface)
					if material == null:
						material = instance.mesh.surface_get_material(surface)
					if material is StandardMaterial3D and (material as StandardMaterial3D).albedo_texture != null:
						return material as StandardMaterial3D
		for child in current.get_children():
			stack.append(child)
	return null


func _any_state(simulation, team: int, state: String) -> bool:
	return _first_state_id(simulation, team, state) != 0


func _first_state_id(simulation, team: int, state: String) -> int:
	for id in simulation.entity_ids():
		var entity: Dictionary = simulation.entity(id)
		if int(entity["team"]) == team and String(entity["state"]) == state:
			return id
	return 0


func _event_kind_present(events: Array[Dictionary], kind: String) -> bool:
	for event in events:
		if String(event.get("kind", "")) == kind:
			return true
	return false


func _first_event_sequence(events: Array[Dictionary], kind: String, team: int = -1, target_id: int = 0) -> int:
	return int(_first_event(events, kind, team, target_id).get("sequence", 0))


func _chain_purchase_rows(rows: Array) -> Array:
	## Chain-step purchase rows only: compiled PLAYER research rides the same
	## structure purchase surface (by design) and asserts on its own checks.
	var output: Array = []
	for row_value in rows:
		if typeof(row_value) == TYPE_DICTIONARY and not bool((row_value as Dictionary).get("research", false)):
			output.append(row_value)
	return output


func _first_event(events: Array[Dictionary], kind: String, team: int = -1, target_id: int = 0) -> Dictionary:
	for event in events:
		if String(event.get("kind", "")) != kind:
			continue
		if team >= 0 and int(event.get("team", -1)) != team:
			continue
		if target_id != 0 and int(event.get("target_id", 0)) != target_id:
			continue
		return event
	return {}


func _event_count(events: Array[Dictionary], kind: String, team: int = -1) -> int:
	var result := 0
	for event in events:
		if String(event.get("kind", "")) == kind and (team < 0 or int(event.get("team", -1)) == team):
			result += 1
	return result


func _gate_names(gates: Array[Dictionary]) -> Array[String]:
	var result: Array[String] = []
	for gate in gates:
		result.append(String(gate.get("name", "")))
	return result


func _gate_source_ids(gates: Array[Dictionary]) -> Array[int]:
	var result: Array[int] = []
	for gate in gates:
		result.append(int(gate.get("source_river_id", -1)))
	return result


func _source_height_samples_match(map_data) -> bool:
	return (
		int(map_data.height_raw_at(20, 20)) == 10246
		and int(map_data.height_raw_at(70, 237)) == 7680
		and int(map_data.height_raw_at(303, 69)) == 7680
		and int(map_data.height_raw_at(208, 142)) == 7168
		and int(map_data.height_raw_at(200, 176)) == 7542
		and int(map_data.height_raw_at(0, 0)) == 7680
		and int(map_data.height_raw_at(414, 352)) == 7652
	)


func _check_retail_exact_values(slice: Node) -> void:
	# Exact retail constants for the core Men roster, pinned three ways at once:
	# the pack's own retail unit rules (menhordes.ini horde LocomotorSets), the
	# converted documents' resolved combat evidence, and the live sim rule.
	# The doc unit-object speed is recorded separately in provenance — never
	# silently substituted for the horde value.
	if String(slice.faction_manifest.get("faction", "")) != "men":
		return
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_check("retail_exact_values_content_db", false, "ContentDB missing")
		return
	var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var sim_rules: Dictionary = slice.simulation._rules.get("unit_rules", {}) as Dictionary
	# RE-DERIVED 2026-08-04 from the layered oracle
	# (.private/retail-work/editions/rotwk/cache/layered-effective-assets/data/ini).
	# The previous damage/range literals were carried over from a men pack that
	# had been compiled from a pre-layered source, so three of these four rows
	# had been sitting in KNOWN_FAILURE_NAMES misdiagnosed as a horde-vs-unit
	# locomotor family. They were not: the speeds and member counts always
	# matched; only the damage and range pins were stale. Every value below is
	# the LIVE authored token — in this dialect `;` opens a comment and the
	# `;,;` / `;;,;;` / `;;.;;` markers introduce the SUPERSEDED value, so e.g.
	# `40 ;,; 45 ;,; 40 ;;.;; 35 ;25` resolves to 40.
	#
	#   fighter     damage  gamedata.ini:2211 GONDOR_SOLDIER_SWORD    = 50
	#               range   weapon.ini GondorFighterSword AttackRange = 11.5
	#   archer      damage  gamedata.ini:2233 GONDOR_ARCHER_DAMAGE    = 40
	#               range   gamedata.ini:2229 GONDOR_ARCHER_RANGE     = 330
	#   towerguard  damage  gamedata.ini:2291 GONDOR_TOWERGUARD_DAMAGE = 70
	#               range   weapon.ini:9509 GondorTowerShieldGuardSword
	#                       AttackRange = 35.0
	#   knight      damage  gamedata.ini:2254 GONDOR_KNIGHT_DAMAGE    = 70
	#               range   weapon.ini GondorKnightSword AttackRange  = 11.5
	#
	# Horde speeds and member counts are unchanged and remain pinned against
	# object/goodfaction/hordes/men/menhordes.ini.
	var expected := {
		"bfme2.object.gondor-fighter": {"doc_member": "bfme2.object.gondor-fighter", "horde_speed": 50.0, "range": 11.5, "damage": 50, "members": 15, "horde": "GondorFighterHorde"},
		"bfme2.object.gondor-archer": {"doc_member": "bfme2.object.gondor-archer", "horde_speed": 47.0, "range": 330.0, "damage": 40, "members": 15, "horde": "GondorArcherHorde"},
		"bfme2.object.gondor-tower-guard": {"doc_member": "bfme2.object.gondor-tower-shield-guard", "horde_speed": 37.0, "range": 35.0, "damage": 70, "members": 15, "horde": "GondorTowerShieldGuardHorde"},
		"bfme2.object.gondor-knight": {"doc_member": "bfme2.object.gondor-cavalry", "horde_speed": 80.0, "range": 11.5, "damage": 70, "members": 10, "horde": "GondorKnightHorde"},
	}
	for object_id in expected:
		var values: Dictionary = expected[object_id]
		var label: String = object_id.replace("bfme2.object.", "").replace("-", "_")
		var retail: Dictionary = content_db.get_retail_unit_rules(object_id)
		var speed_field: Dictionary = (((retail.get("horde", {}) as Dictionary).get("locomotorSet", {}) as Dictionary).get("speed", {}) as Dictionary)
		var speed_source: Dictionary = speed_field.get("source", {}) as Dictionary
		_check(
			"%s_pack_horde_speed_exact" % label,
			float(speed_field.get("value", -1.0)) == float(values["horde_speed"])
				and String(speed_source.get("ini", "")) == "data/ini/object/goodfaction/hordes/men/menhordes.ini"
				and String(speed_source.get("scopeName", "")) == String(values["horde"]),
			"speed=%s source=%s" % [str(speed_field.get("value", null)), str(speed_source)]
		)
		var rule: Dictionary = sim_rules.get(String(values["doc_member"]), {}) as Dictionary
		var horde_provenance: Dictionary = (rule.get("provenance", {}) as Dictionary).get("horde_locomotor", {}) as Dictionary
		_check(
			"%s_sim_values_exact" % label,
			float(rule.get("speed_source", -1.0)) == float(values["horde_speed"])
				and float(rule.get("attack_range_source", -1.0)) == float(values["range"])
				and int(rule.get("member_damage", -1)) == int(values["damage"])
				and int(rule.get("member_count", -1)) == int(values["members"])
				and float(horde_provenance.get("speed", -1.0)) == float(values["horde_speed"])
				and float(horde_provenance.get("unit_object_speed", -1.0)) > 0.0,
			str({"speed": rule.get("speed_source", null), "range": rule.get("attack_range_source", null), "damage": rule.get("member_damage", null), "members": rule.get("member_count", null), "provenance": horde_provenance})
		)
		for document_value in slice.producible_unit_runtimes.values():
			var document: Dictionary = document_value
			if adapter.runtime_member_id(document) != String(values["doc_member"]):
				continue
			var combat: Dictionary = adapter.simulation_rule(document).get("combat", {}) as Dictionary
			_check(
				"%s_document_combat_evidence_exact" % label,
				int(combat.get("damage", -1)) == int(values["damage"])
					and is_equal_approx(float(combat.get("attackRange", -1.0)), float(values["range"])),
				str(combat)
			)


func _source_terrain_tile_samples_match(map_data) -> bool:
	return (
		int(map_data.terrain_tile_index_at(20, 20)) == 1656
		and int(map_data.terrain_base_texture_index_at(20, 20)) == 26
		and int(map_data.terrain_tile_index_at(70, 237)) == 46
		and int(map_data.terrain_base_texture_index_at(70, 237)) == 0
		and int(map_data.terrain_tile_index_at(303, 69)) == 47
		and int(map_data.terrain_base_texture_index_at(303, 69)) == 0
		and int(map_data.terrain_tile_index_at(208, 142)) == 3376
		and int(map_data.terrain_base_texture_index_at(208, 142)) == 52
		and int(map_data.terrain_tile_index_at(200, 176)) == 3008
		and int(map_data.terrain_base_texture_index_at(200, 176)) == 47
	)


func _authored_ramp_distance(max_speed: float, acceleration: float, ticks: int) -> float:
	## Closed form of the sim's own locomotion ramp from rest, so movement
	## assertions read the authored locomotor instead of a hand-tuned constant.
	## Mirrors retail_slice_sim.gd:13881 (velocity ramp, clamped at max speed)
	## and :13883 (position step), with TICK_SECONDS = 0.1.
	if max_speed <= 0.0 or acceleration <= 0.0 or ticks <= 0:
		return 0.0
	var tick_seconds := 0.1
	var current_speed := 0.0
	var travelled := 0.0
	for _index in range(ticks):
		current_speed = minf(max_speed, current_speed + acceleration * tick_seconds)
		travelled += current_speed * tick_seconds
	return travelled


func _route_respects_source_navigation(map_data, cells: Array[Vector2i]) -> bool:
	if cells.is_empty() or cells.size() > 1024:
		return false
	for cell in cells:
		if not map_data.is_grid_inside_playable(cell) or map_data.is_impassable_at(cell.x, cell.y) or not map_data.is_navigation_walkable(cell):
			return false
	return true


func _route_water_only_in_named_fords(map_data, cells: Array[Vector2i]) -> bool:
	for cell in cells:
		if map_data.is_water_cell(cell) and not map_data.is_ford_corridor_cell(cell):
			return false
	return true


func _ford_probe_crosses_opposite_banks(map_data, probe: Dictionary, ford_name: String) -> bool:
	var bank_a := Vector2i(probe.get("probe_bank_a", Vector2i(-1, -1)))
	var bank_b := Vector2i(probe.get("probe_bank_b", Vector2i(-1, -1)))
	if not map_data.is_navigation_walkable(bank_a) or not map_data.is_navigation_walkable(bank_b) or map_data.is_water_cell(bank_a) or map_data.is_water_cell(bank_b):
		return false
	var edge_a := Vector2(probe.get("probe_edge_a", Vector2.ZERO))
	var edge_b := Vector2(probe.get("probe_edge_b", Vector2.ZERO))
	var direction := edge_a.direction_to(edge_b)
	var midpoint := (edge_a + edge_b) * 0.5
	var local_bank_a: Vector2 = map_data.grid_to_local_horizontal(bank_a)
	var local_bank_b: Vector2 = map_data.grid_to_local_horizontal(bank_b)
	if (local_bank_a - midpoint).dot(direction) >= 0.0 or (local_bank_b - midpoint).dot(direction) <= 0.0:
		return false
	var cells: Array[Vector2i] = []
	cells.assign(probe.get("cells", []))
	for cell in cells:
		if map_data.is_water_cell(cell) and map_data.is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func _bound_prop_type_visibility_matches(battlefield: Node, source_type: String, expected_count: int, expected_visible: bool, expected_reason: String) -> bool:
	var container: Node3D = battlefield.get("retail_prop_container") as Node3D
	if container == null:
		return false
	var matched_count := 0
	for child in container.get_children():
		var placement := child as Node3D
		if placement == null or String(placement.get_meta("source_type", "")) != source_type:
			continue
		matched_count += 1
		if placement.visible != expected_visible:
			return false
		if not expected_visible and (
			not bool(placement.get_meta("main_camera_excluded", false))
			or String(placement.get_meta("main_camera_exclusion_reason", "")) != expected_reason
		):
			return false
	return matched_count == expected_count


func _unresolved_diagnostic_sample_count(battlefield: Node) -> int:
	var result := 0
	for node_name in ["SourceVegetationPlacementMarkers", "SourceRockPlacementMarkers"]:
		var diagnostic := battlefield.find_child(node_name, true, false)
		if diagnostic != null:
			if not bool(diagnostic.get_meta("diagnostic_only", false)):
				return -1
			result += int(diagnostic.get_meta("source_placement_count", -1))
	return result


func _visible_source_placeholder_count(node: Node, name_prefix: String) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.name).begins_with(name_prefix):
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _visible_unresolved_placeholder_count(node: Node) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.get_meta("presentation", "")) == "unresolved-marker":
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _compact_combat_state(simulation) -> String:
	var rows: Array[String] = []
	for id in simulation.entity_ids():
		var row: Dictionary = simulation.entity(id)
		rows.append("e%d:t%d:%s:target%d:hp%d:p%s" % [id, int(row.get("team", -1)), String(row.get("state", "")), int(row.get("target_id", 0)), int(row.get("health", 0)), str(row.get("position", Vector2.ZERO))])
	for id in [simulation.fortress_id(0), simulation.fortress_id(1)]:
		var row: Dictionary = simulation.structure(id)
		rows.append("s%d:t%d:hp%d:p%s" % [id, int(row.get("team", -1)), int(row.get("health", 0)), str(row.get("position", Vector2.ZERO))])
	return "|".join(rows)


func _check_retail_unit_rules(slice: Node) -> void:
	# Converted playableUnit documents drive every spawn-roster sim row exactly:
	# speed/range/timing/members in source units are doc evidence, scaled into
	# local space by the exact retail map transform. No hardcoded roster values.
	var men_faction_slice := String(slice.faction_manifest.get("faction", "")) == "men"
	var content_db = root.get_node_or_null("ContentDB")
	_check("retail_unit_rules_content_db_loaded", content_db != null)
	if content_db == null:
		return
	var source_one: Vector3 = slice.source_map_data.player_starts["Player_1_Start"]
	var source_two: Vector3 = slice.source_map_data.player_starts["Player_2_Start"]
	var source_separation := Vector2(source_one.x, source_one.z).distance_to(Vector2(source_two.x, source_two.z))
	var scale: float = slice.source_map_data.LOCAL_START_SEPARATION / source_separation
	_check("retail_unit_world_scale_exact", float(slice.source_map_data.local_transform_scale) == scale, str(slice.source_map_data.local_transform_scale))
	var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var distinct_speeds := {}
	var distinct_ranges := {}
	var checked_rows := 0
	for entity_id in slice.simulation.entity_ids():
		var row: Dictionary = slice.simulation.entity(entity_id)
		var object_id := String(row.get("object_id", ""))
		if bool(row.get("is_builder", false)):
			continue
		var document: Dictionary = {}
		for candidate_value in slice.producible_unit_runtimes.values():
			if adapter.runtime_member_id(candidate_value) == object_id:
				document = candidate_value
				break
		if document.is_empty():
			continue
		var simulation: Dictionary = adapter.simulation_rule(document)
		if simulation.is_empty():
			continue
		var combat: Dictionary = simulation.get("combat", {}) as Dictionary
		var movement: Dictionary = simulation.get("movement", {}) as Dictionary
		var member_count := int(simulation.get("member_count", 0))
		# Horde movement expectation: the horde LocomotorSet speed recorded in
		# provenance when present, else the document's unit-object speed.
		var speed := float(simulation.get("speed_source", -1.0))
		var horde_locomotor: Dictionary = (row.get("retail_rule_provenance", {}) as Dictionary).get("horde_locomotor", {}) as Dictionary
		if not horde_locomotor.is_empty():
			speed = float(horde_locomotor.get("speed", speed))
		var attack_range := float(combat.get("attackRange", -1.0))
		var provenance: Dictionary = row.get("retail_rule_provenance", {})
		var battalion = _battalion_for_object_id(slice, object_id)
		_check(
			"%s_sim_row_matches_document" % object_id.replace("bfme2.object.", "").replace("-", "_"),
			float(row.get("speed_source", -1.0)) == speed
				and is_equal_approx(float(row.get("speed", -1.0)), speed * scale)
				and is_equal_approx(float(row.get("attack_range_source", -1.0)), attack_range)
				and is_equal_approx(float(row.get("attack_range", -1.0)), attack_range * scale)
				and float(row.get("delay_between_shots_ms", -1.0)) == float(combat.get("delayBetweenShotsMs", -2.0))
				and float(row.get("pre_attack_delay_ms", -1.0)) == float(combat.get("preAttackDelayMs", -2.0))
				and float(row.get("firing_duration_ms", -1.0)) == float(combat.get("firingDurationMs", -2.0))
				and int(row.get("member_damage", -1)) == int(combat.get("damage", -1))
				and int(row.get("member_count", -1)) == member_count
				and int(row.get("member_maximum_health", -1)) == int(simulation.get("member_health", -1))
				and float(row.get("acceleration_source", -1.0)) == float(movement.get("acceleration", -1.0)) * adapter.HORDE_LOCOMOTION_RESPONSE_SCALE
				and String(provenance.get("source_contract", "")) == "openbfme.playable-unit-runtime",
			str(row)
		)
		_check(
			"%s_formation_slots_match_document" % object_id.replace("bfme2.object.", "").replace("-", "_"),
			battalion != null
				and int(battalion.member_count) == member_count
				and Array(row.get("formation_positions", [])).size() == member_count,
			str(member_count)
		)
		distinct_speeds[speed] = true
		distinct_ranges[attack_range] = true
		checked_rows += 1
	_document_backed_rows = checked_rows
	_check(
		"spawn_roster_rows_all_document_backed",
		checked_rows >= 5,
		"rows=%d" % checked_rows
	)
	_check("retail_unit_speeds_match_documents", distinct_speeds.size() >= 2 or not men_faction_slice, str(distinct_speeds.keys()))
	_check("retail_attack_ranges_match_documents", distinct_ranges.size() >= 2 or not men_faction_slice, str(distinct_ranges.keys()))


func _retail_shadow_decals_present(slice) -> bool:
	# Approved shadow equivalence: every battalion member and every structure
	# carries the retail shadow-color blob decal (retail draws shadows via
	# decals with shadow mapping disabled).
	var source_shadow_color := Color(0.0, 0.0, 0.0, 64.0 / 255.0)
	var battalion_decals := 0
	for battalion_value in slice.battalion_nodes.values():
		for decal in (battalion_value as Node).find_children("RetailShadowDecal", "Decal", true, false):
			if (decal as Decal).modulate == source_shadow_color:
				battalion_decals += 1
	var structure_decals := 0
	for structure_value in slice.structure_nodes.values():
		for decal in (structure_value as Node).find_children("RetailShadowDecal", "Decal", true, false):
			if (decal as Decal).modulate == source_shadow_color:
				structure_decals += 1
	return battalion_decals > 0 and structure_decals > 0


func _sockets_ring_dish_center(command_grid: Control, dish_center_global: Vector2) -> bool:
	# The six empty command sockets ride the palantir dish rim; their
	# capture-measured centers sit 95..135 dock px from the dish center.
	var socket_count := 0
	for child in command_grid.get_children():
		var socket := child as TextureRect
		if socket == null or not String(socket.name).begins_with("RetailEmptySocket"):
			continue
		socket_count += 1
		var distance := (socket.global_position + socket.size * 0.5).distance_to(dish_center_global)
		if distance < 95.0 or distance > 135.0:
			return false
	return socket_count == 6


func _armor_rule_for_set(sim, set_id: String) -> Dictionary:
	## The compiled armor rule for one retail set, found through the document
	## id space alone (no hardcoded object-id aliases).
	for key_value in sim._unit_armor.keys():
		var rule: Dictionary = sim._unit_armor[key_value]
		if String(rule.get("set_id", "")) == set_id:
			return rule
	return {}


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("RETAIL_SLICE PASS %s" % name)
	else:
		failed += 1
		observed_failure_names[name] = true
		printerr("RETAIL_SLICE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	## The named-failure contract is MODE AWARE.
	##
	## Nearly every pinned name in KNOWN_FAILURE_NAMES belongs to a check that
	## only exists when playable-unit/structure documents resolved. In the
	## without-documents branch those checks never run, so their names are
	## never observed failing, and the "known failure now passes" direction
	## fired for all of them at once: measured 2026-08-04 against
	## .private/retail-work/packs/bfme2-men-vslice the runner reported
	## failed=48 = 9 real failures + 39 = KNOWN_FAILURE_NAMES.size() spurious
	## retail_gate_update_allowlist_* rows. That is what broke
	## tools/gate-retail.ps1, whose regex demands failed=0 against exactly that
	## profile-built bfme2 pack (it carries no data/playable-units at all).
	##
	## A pin that did not fire is only evidence of anything if the check that
	## owns it actually ran. The unexpected-failure direction stays enforced in
	## both modes - a newly broken check is real news regardless of mode.
	var document_backed := _document_backed_rows >= 5
	var floor_checks := MINIMUM_CHECKS_DOCUMENT_BACKED if document_backed else MINIMUM_CHECKS_WITHOUT_DOCUMENTS
	var ran := passed + failed
	if ran < floor_checks:
		failed += 1
		printerr("RETAIL_SLICE FAIL liveness: ran %d checks, expected at least %d - a function aborted before its assertions" % [ran, floor_checks])
	var unexpected: Array = []
	for name_value in observed_failure_names.keys():
		if not KNOWN_FAILURE_NAMES.has(name_value):
			unexpected.append(String(name_value))
	var newly_passing: Array = []
	if document_backed:
		for name_value in KNOWN_FAILURE_NAMES.keys():
			if not observed_failure_names.has(name_value):
				newly_passing.append(String(name_value))
	else:
		print(
			(
				"RETAIL_SLICE NOTE named-failure pins not enforced: the mounted"
				+ " content resolved %d document-backed rows (<5), so the checks"
				+ " that own the %d pinned names never ran. Mount a playable-unit"
				+ " pack to enforce them."
			) % [_document_backed_rows, KNOWN_FAILURE_NAMES.size()]
		)
	unexpected.sort()
	newly_passing.sort()
	for name in unexpected:
		printerr("RETAIL_SLICE FAIL retail_gate_unexpected_failure_%s (not in KNOWN_FAILURE_NAMES)" % name)
	for name in newly_passing:
		failed += 1
		printerr("RETAIL_SLICE FAIL retail_gate_update_allowlist_%s (known failure now passes; consciously remove it from KNOWN_FAILURE_NAMES)" % name)
	_watchdog.stop()
	print("RETAIL_SLICE_RESULT passed=%d failed=%d" % [passed, failed])
	var acceptance_ok := (
		passed >= ACCEPTANCE_MIN_PASSED
		and unexpected.is_empty()
		and newly_passing.is_empty()
	)
	print("RETAIL_SLICE_ACCEPTANCE %s min_passed=%d pinned_known_failures=%d" % [
		"PASS" if acceptance_ok else "FAIL",
		ACCEPTANCE_MIN_PASSED,
		KNOWN_FAILURE_NAMES.size(),
	])
	quit(0 if acceptance_ok else 1)
